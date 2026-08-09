#include <vanguard/rhi/backend/resource_lifetime.hpp>

#include <vanguard/concurrency/atomic.hpp>
#include <vanguard/concurrency/synchronization.hpp>
#include <vanguard/jobs/jobs.hpp>
#include <vanguard/memory/memory.hpp>
#include <vanguard/system/assert.hpp>

#include <new>

namespace vanguard::rhi::backend
{
    namespace
    {
        constexpr u32 FreeReferenceCount = 0xffffffffu;
        constexpr u32 DestroyingReferenceCount = 0xfffffffeu;

        [[nodiscard]] constexpr u64 PackIdentity(const u32 generation, const u32 references) noexcept
        {
            return (static_cast<u64>(generation) << 32u) | references;
        }

        [[nodiscard]] constexpr u32 IdentityGeneration(const u64 identity) noexcept
        {
            return static_cast<u32>(identity >> 32u);
        }

        [[nodiscard]] constexpr u32 IdentityReferences(const u64 identity) noexcept
        {
            return static_cast<u32>(identity);
        }

        [[nodiscard]] constexpr u32 NextGeneration(const u32 generation) noexcept
        {
            const u32 next = generation + 1u;
            return next != 0 ? next : 1u;
        }

        [[nodiscard]] constexpr u32 QueueIndex(const QueueType queue) noexcept
        {
            return static_cast<u32>(queue);
        }

        [[nodiscard]] constexpr u32 NextPowerOfTwo(u32 value) noexcept
        {
            if (value < 2u) return 2u;
            --value;
            value |= value >> 1u;
            value |= value >> 2u;
            value |= value >> 4u;
            value |= value >> 8u;
            value |= value >> 16u;
            return value + 1u;
        }

        void UpdateMaximum(concurrency::Atomic<u32>& target, const u32 value) noexcept
        {
            u32 current = target.GetValue();
            while (current < value)
            {
                const u32 observed = target.CompareExchange(value, current);
                if (observed == current) return;
                current = observed;
            }
        }

        void UpdateMaximum(concurrency::Atomic<u64>& target, const u64 value) noexcept
        {
            u64 current = target.GetValue();
            while (current < value)
            {
                const u64 observed = target.CompareExchange(value, current);
                if (observed == current) return;
                current = observed;
            }
        }

        void Subtract(concurrency::Atomic<u64>& target, const u64 value) noexcept
        {
            u64 current = target.GetValue();
            for (;;)
            {
                VG_ASSERT_MSG(current >= value, "RHI atomic accounting underflow");
                const u64 observed = target.CompareExchange(current - value, current);
                if (observed == current) return;
                current = observed;
            }
        }

        struct ResourceSlot
        {
            concurrency::Atomic<u64> identity{PackIdentity(1u, FreeReferenceCount)};
            concurrency::Atomic<u64> lastUse[3];
            concurrency::Atomic<u32> retirementQueued{0};
            void* payload = nullptr;
            DestroyResourceCallback destroy = nullptr;
            void* destroyContext = nullptr;
        };

        class ResourceTable final
        {
        public:
            [[nodiscard]] bool Initialize(const ResourceKind resourceKind, const u32 requestedCapacity) noexcept
            {
                if (slots != nullptr || requestedCapacity == 0 || requestedCapacity > MaximumResourceReferenceIndex) return false;
                kind = resourceKind;
                capacity = requestedCapacity;
                slotBlock = memory::Allocate(memory::PoolId::Rendering, sizeof(ResourceSlot) * capacity, alignof(ResourceSlot));
                freeBlock = memory::Allocate(memory::PoolId::Rendering, sizeof(u32) * capacity, alignof(u32));
                if (!slotBlock || !freeBlock)
                {
                    memory::Free(slotBlock);
                    memory::Free(freeBlock);
                    capacity = 0;
                    return false;
                }

                slots = static_cast<ResourceSlot*>(slotBlock.address);
                freeIndices = static_cast<u32*>(freeBlock.address);
                for (u32 index = 0; index < capacity; ++index)
                {
                    new (&slots[index]) ResourceSlot();
                    freeIndices[index] = capacity - index - 1u;
                }
                freeCount = capacity;
                return true;
            }

            void Shutdown() noexcept
            {
                if (slots == nullptr) return;
                for (u32 index = 0; index < capacity; ++index) slots[index].~ResourceSlot();
                slots = nullptr;
                freeIndices = nullptr;
                capacity = 0;
                freeCount = 0;
                memory::Free(slotBlock);
                memory::Free(freeBlock);
            }

            [[nodiscard]] ResourceRef Create(void* const payload, const DestroyResourceCallback destroy,
                                             void* const destroyContext, const u32 initialReferences) noexcept
            {
                if (initialReferences == 0 || initialReferences >= DestroyingReferenceCount) return {};
                concurrency::ScopedLock guard(freeLock);
                if (freeCount == 0) return {};
                const u32 index = freeIndices[--freeCount];
                ResourceSlot& slot = slots[index];
                const u64 oldIdentity = slot.identity.GetValue();
                const u32 generation = IdentityGeneration(oldIdentity);
                VG_ASSERT_MSG(IdentityReferences(oldIdentity) == FreeReferenceCount, "RHI free list contains a live resource slot");
                slot.payload = payload;
                slot.destroy = destroy;
                slot.destroyContext = destroyContext;
                slot.lastUse[0].SetValue(0);
                slot.lastUse[1].SetValue(0);
                slot.lastUse[2].SetValue(0);
                slot.retirementQueued.SetValue(0);
                slot.identity.SetValue(PackIdentity(generation, initialReferences));
                return ResourceRef::FromParts(kind, index, generation);
            }

            [[nodiscard]] ResourceSlot* Find(const ResourceRef resource) noexcept
            {
                return resource.Kind() == kind && resource.Index() < capacity ? &slots[resource.Index()] : nullptr;
            }

            [[nodiscard]] const ResourceSlot* Find(const ResourceRef resource) const noexcept
            {
                return resource.Kind() == kind && resource.Index() < capacity ? &slots[resource.Index()] : nullptr;
            }

            [[nodiscard]] void* GetPayload(const ResourceRef resource) noexcept
            {
                ResourceSlot* const slot = Find(resource);
                if (slot == nullptr) return nullptr;
                const u64 identity = slot->identity.GetValue();
                const u32 references = IdentityReferences(identity);
                return IdentityGeneration(identity) == resource.Generation() && references < DestroyingReferenceCount
                           ? slot->payload
                           : nullptr;
            }

            [[nodiscard]] const void* GetPayload(const ResourceRef resource) const noexcept
            {
                return const_cast<ResourceTable*>(this)->GetPayload(resource);
            }

            [[nodiscard]] bool TryClaimDestroy(const ResourceRef resource, void*& payload,
                                               DestroyResourceCallback& destroy, void*& destroyContext) noexcept
            {
                ResourceSlot* const slot = Find(resource);
                if (slot == nullptr) return false;
                const u64 expected = PackIdentity(resource.Generation(), 0);
                if (slot->identity.CompareExchange(PackIdentity(resource.Generation(), DestroyingReferenceCount), expected) != expected)
                    return false;
                payload = slot->payload;
                destroy = slot->destroy;
                destroyContext = slot->destroyContext;
                return true;
            }

            [[nodiscard]] bool TryClaimAny(const u32 index, ResourceRef& resource, u32& references, void*& payload,
                                           DestroyResourceCallback& destroy, void*& destroyContext) noexcept
            {
                if (index >= capacity) return false;
                ResourceSlot& slot = slots[index];
                u64 identity = slot.identity.GetValue();
                for (;;)
                {
                    references = IdentityReferences(identity);
                    if (references == FreeReferenceCount || references == DestroyingReferenceCount) return false;
                    const u32 generation = IdentityGeneration(identity);
                    const u64 destroying = PackIdentity(generation, DestroyingReferenceCount);
                    const u64 observed = slot.identity.CompareExchange(destroying, identity);
                    if (observed != identity)
                    {
                        identity = observed;
                        continue;
                    }
                    resource = ResourceRef::FromParts(kind, index, generation);
                    payload = slot.payload;
                    destroy = slot.destroy;
                    destroyContext = slot.destroyContext;
                    return true;
                }
            }

            void FinishDestroy(const ResourceRef resource) noexcept
            {
                ResourceSlot& slot = slots[resource.Index()];
                slot.payload = nullptr;
                slot.destroy = nullptr;
                slot.destroyContext = nullptr;
                slot.lastUse[0].SetValue(0);
                slot.lastUse[1].SetValue(0);
                slot.lastUse[2].SetValue(0);
                slot.retirementQueued.SetValue(0);
                const u32 generation = NextGeneration(resource.Generation());
                slot.identity.SetValue(PackIdentity(generation, FreeReferenceCount));
                concurrency::ScopedLock guard(freeLock);
                VG_ASSERT_MSG(freeCount < capacity, "RHI resource free list overflow");
                freeIndices[freeCount++] = resource.Index();
            }

            template <typename Visitor> void VisitSlots(Visitor&& visitor) noexcept
            {
                for (u32 index = 0; index < capacity; ++index) visitor(index, slots[index]);
            }

            ResourceKind kind = ResourceKind::None;
            u32 capacity = 0;
            ResourceSlot* slots = nullptr;

        private:
            memory::MemoryBlock slotBlock{};
            memory::MemoryBlock freeBlock{};
            u32* freeIndices = nullptr;
            u32 freeCount = 0;
            concurrency::SpinLock freeLock;
        };

        class RetirementQueue final
        {
            struct Cell
            {
                concurrency::Atomic<u64> sequence;
                ResourceRef resource{};
            };

        public:
            [[nodiscard]] bool Initialize(const u32 minimumCapacity) noexcept
            {
                capacity = NextPowerOfTwo(minimumCapacity);
                mask = capacity - 1u;
                block = memory::Allocate(memory::PoolId::Rendering, sizeof(Cell) * capacity, alignof(Cell));
                if (!block) return false;
                cells = static_cast<Cell*>(block.address);
                for (u32 index = 0; index < capacity; ++index) new (&cells[index]) Cell{concurrency::Atomic<u64>(index), {}};
                enqueue.SetValue(0);
                dequeue.SetValue(0);
                return true;
            }

            void Shutdown() noexcept
            {
                if (cells == nullptr) return;
                for (u32 index = 0; index < capacity; ++index) cells[index].~Cell();
                cells = nullptr;
                capacity = 0;
                mask = 0;
                memory::Free(block);
            }

            [[nodiscard]] bool Push(const ResourceRef resource) noexcept
            {
                u64 position = enqueue.GetValue();
                for (;;)
                {
                    Cell& cell = cells[static_cast<u32>(position) & mask];
                    const u64 sequence = cell.sequence.GetValue();
                    const i64 difference = static_cast<i64>(sequence) - static_cast<i64>(position);
                    if (difference == 0)
                    {
                        if (enqueue.CompareExchange(position + 1u, position) == position)
                        {
                            cell.resource = resource;
                            cell.sequence.SetValue(position + 1u);
                            return true;
                        }
                    }
                    else if (difference < 0)
                    {
                        return false;
                    }
                    else
                    {
                        position = enqueue.GetValue();
                    }
                }
            }

            [[nodiscard]] bool Pop(ResourceRef& resource) noexcept
            {
                u64 position = dequeue.GetValue();
                for (;;)
                {
                    Cell& cell = cells[static_cast<u32>(position) & mask];
                    const u64 sequence = cell.sequence.GetValue();
                    const i64 difference = static_cast<i64>(sequence) - static_cast<i64>(position + 1u);
                    if (difference == 0)
                    {
                        if (dequeue.CompareExchange(position + 1u, position) == position)
                        {
                            resource = cell.resource;
                            cell.resource = {};
                            cell.sequence.SetValue(position + capacity);
                            return true;
                        }
                    }
                    else if (difference < 0)
                    {
                        return false;
                    }
                    else
                    {
                        position = dequeue.GetValue();
                    }
                }
            }

        private:
            memory::MemoryBlock block{};
            Cell* cells = nullptr;
            u32 capacity = 0;
            u32 mask = 0;
            concurrency::Atomic<u64> enqueue;
            concurrency::Atomic<u64> dequeue;
        };

        struct RetirementBucket
        {
            concurrency::Atomic<u32> count;
            FenceSet fences{};
            bool finalized = false;
        };
    } // namespace

    u64 FenceSet::Get(const QueueType queue) const noexcept
    {
        if (queue == QueueType::Compute) return compute;
        if (queue == QueueType::Copy) return copy;
        return graphics;
    }

    void FenceSet::Include(const QueueType queue, const u64 value) noexcept
    {
        u64* target = &graphics;
        if (queue == QueueType::Compute) target = &compute;
        else if (queue == QueueType::Copy) target = &copy;
        if (*target < value) *target = value;
    }

    void FenceSet::Include(const FenceSet& other) noexcept
    {
        Include(QueueType::Graphics, other.graphics);
        Include(QueueType::Compute, other.compute);
        Include(QueueType::Copy, other.copy);
    }

    struct ResourceLifetimeManager::Impl
    {
        ResourceTable tables[static_cast<u32>(ResourceKind::Count)];
        RetirementQueue retirementQueue;
        RetirementBucket buckets[MaximumRetirementBuckets];
        u32 bucketCount = 0;
        concurrency::Atomic<u32> enqueueBucket;
        u32 nextFlushBucket = 0;
        concurrency::Atomic<u32> pendingEvictions;
        concurrency::SpinLock epochLock;
        concurrency::SpinLock evictionLock;
        concurrency::SpinLock scheduleLock;
        jobs::Counter reclamationCounter;
        jobs::JobName reclamationJobName{"RHI/RetireResources"};
        FenceCompleteCallback fenceComplete = nullptr;
        void* fenceContext = nullptr;
        concurrency::Atomic<u32> liveResources;
        concurrency::Atomic<u32> pendingRetirements;
        concurrency::Atomic<u32> peakPendingRetirements;
        concurrency::Atomic<u32> destroyingResources;
        concurrency::Atomic<u32> reclamationJobs;
        concurrency::Atomic<u64> totalReferences;
        concurrency::Atomic<u64> completedRetirements;
        concurrency::Atomic<u64> staleReferenceOperations;
        concurrency::Atomic<u64> retirementQueueRecoveries;
        concurrency::Atomic<u64> retirementBucketOverflows;
        bool initialized = false;

        [[nodiscard]] ResourceTable* GetTable(const ResourceKind kind) noexcept
        {
            const u32 index = static_cast<u32>(kind);
            return index > 0 && index < static_cast<u32>(ResourceKind::Count) ? &tables[index] : nullptr;
        }

        [[nodiscard]] const ResourceTable* GetTable(const ResourceKind kind) const noexcept
        {
            const u32 index = static_cast<u32>(kind);
            return index > 0 && index < static_cast<u32>(ResourceKind::Count) ? &tables[index] : nullptr;
        }

        [[nodiscard]] bool AreFencesComplete(const FenceSet& fences) const noexcept
        {
            if (fenceComplete == nullptr) return fences.graphics == 0 && fences.compute == 0 && fences.copy == 0;
            return (fences.graphics == 0 || fenceComplete(fenceContext, QueueType::Graphics, fences.graphics)) &&
                   (fences.compute == 0 || fenceComplete(fenceContext, QueueType::Compute, fences.compute)) &&
                   (fences.copy == 0 || fenceComplete(fenceContext, QueueType::Copy, fences.copy));
        }

        [[nodiscard]] FenceSet GetLastUse(const ResourceSlot& slot) const noexcept
        {
            return {slot.lastUse[0].GetValue(), slot.lastUse[1].GetValue(), slot.lastUse[2].GetValue()};
        }

        [[nodiscard]] bool QueueRetirementLocked(const ResourceRef resource, ResourceSlot& slot,
                                                 const bool recovery) noexcept
        {
            if (slot.retirementQueued.Exchange(1u) != 0u) return true;
            if (!retirementQueue.Push(resource))
            {
                slot.retirementQueued.SetValue(0);
                static_cast<void>(retirementQueueRecoveries.Increment());
                return false;
            }
            const u32 bucketIndex = enqueueBucket.GetValue();
            static_cast<void>(buckets[bucketIndex].count.Increment());
            if (recovery) static_cast<void>(retirementQueueRecoveries.Increment());
            return true;
        }

        [[nodiscard]] bool QueueRetirement(const ResourceRef resource, ResourceSlot& slot,
                                           const bool recovery) noexcept
        {
            // Queue order and bucket counts form one publication transaction. Serializing only final-release
            // publication prevents an epoch advance from observing one half of that transaction. Ordinary
            // AddRef/Release operations that do not reach zero remain lock-free.
            concurrency::ScopedLock guard(epochLock);
            return QueueRetirementLocked(resource, slot, recovery);
        }

        void RecoverUnqueuedRetirements() noexcept
        {
            for (u32 kindIndex = 1; kindIndex < static_cast<u32>(ResourceKind::Count); ++kindIndex)
            {
                ResourceTable& table = tables[kindIndex];
                table.VisitSlots([&](const u32 index, ResourceSlot& slot) noexcept
                {
                    const u64 identity = slot.identity.GetValue();
                    if (IdentityReferences(identity) != 0 || slot.retirementQueued.GetValue() != 0) return;
                    const ResourceRef resource = ResourceRef::FromParts(static_cast<ResourceKind>(kindIndex), index,
                                                                        IdentityGeneration(identity));
                    static_cast<void>(QueueRetirement(resource, slot, true));
                });
            }
        }

        void PrepareEvictions() noexcept
        {
            concurrency::ScopedLock guard(epochLock);
            u32 evictionCount = 0;
            for (u32 visited = 0; visited < bucketCount; ++visited)
            {
                const u32 current = enqueueBucket.GetValue();
                if (nextFlushBucket == current) break;
                RetirementBucket& bucket = buckets[nextFlushBucket];
                if (!bucket.finalized || !AreFencesComplete(bucket.fences)) break;
                evictionCount += bucket.count.Exchange(0);
                bucket.fences = {};
                bucket.finalized = false;
                nextFlushBucket = (nextFlushBucket + 1u) % bucketCount;
            }
            if (evictionCount != 0) static_cast<void>(pendingEvictions.ExchangeAdd(evictionCount));
        }

        void Requeue(const ResourceRef resource, ResourceSlot& slot) noexcept
        {
            slot.retirementQueued.SetValue(0);
            static_cast<void>(QueueRetirement(resource, slot, false));
        }

        void DestroyClaimed(const ResourceRef resource, ResourceTable& table, void* const payload,
                            const DestroyResourceCallback destroy, void* const destroyContext) noexcept
        {
            static_cast<void>(destroyingResources.Increment());
            if (destroy != nullptr) destroy(destroyContext, resource, payload);
            table.FinishDestroy(resource);
            static_cast<void>(destroyingResources.Decrement());
            static_cast<void>(pendingRetirements.Decrement());
            static_cast<void>(completedRetirements.Increment());
        }

        void ReclaimReady() noexcept
        {
            concurrency::ScopedLock guard(evictionLock);
            u32 count = pendingEvictions.Exchange(0);
            while (count-- != 0)
            {
                ResourceRef resource{};
                if (!retirementQueue.Pop(resource))
                {
                    // Bucket counts and queue entries are published independently. A failed pop should be
                    // impossible once the producer has published the bucket, but preserve the outstanding
                    // count so a transient publication race cannot strand retired resources forever.
                    static_cast<void>(pendingEvictions.ExchangeAdd(count + 1u));
                    static_cast<void>(retirementQueueRecoveries.Increment());
                    break;
                }
                ResourceTable* const table = GetTable(resource.Kind());
                ResourceSlot* const slot = table != nullptr ? table->Find(resource) : nullptr;
                if (slot == nullptr) continue;
                if (!AreFencesComplete(GetLastUse(*slot)))
                {
                    Requeue(resource, *slot);
                    continue;
                }
                void* payload = nullptr;
                void* destroyContext = nullptr;
                DestroyResourceCallback destroy = nullptr;
                if (!table->TryClaimDestroy(resource, payload, destroy, destroyContext)) continue;
                DestroyClaimed(resource, *table, payload, destroy, destroyContext);
            }
        }

        void ForceReclaimAfterGpuIdle() noexcept
        {
            concurrency::ScopedLock guard(evictionLock);
            ResourceRef resource{};
            while (retirementQueue.Pop(resource))
            {
                ResourceTable* const table = GetTable(resource.Kind());
                if (table == nullptr) continue;
                void* payload = nullptr;
                void* destroyContext = nullptr;
                DestroyResourceCallback destroy = nullptr;
                if (table->TryClaimDestroy(resource, payload, destroy, destroyContext))
                    DestroyClaimed(resource, *table, payload, destroy, destroyContext);
            }
            for (u32 kindIndex = 1; kindIndex < static_cast<u32>(ResourceKind::Count); ++kindIndex)
            {
                ResourceTable& table = tables[kindIndex];
                table.VisitSlots([&](const u32 index, ResourceSlot& slot) noexcept
                {
                    const u64 identity = slot.identity.GetValue();
                    if (IdentityReferences(identity) != 0) return;
                    const ResourceRef pending = ResourceRef::FromParts(static_cast<ResourceKind>(kindIndex), index,
                                                                       IdentityGeneration(identity));
                    void* payload = nullptr;
                    void* destroyContext = nullptr;
                    DestroyResourceCallback destroy = nullptr;
                    if (table.TryClaimDestroy(pending, payload, destroy, destroyContext))
                        DestroyClaimed(pending, table, payload, destroy, destroyContext);
                });
            }
            pendingEvictions.SetValue(0);
            for (u32 index = 0; index < bucketCount; ++index)
            {
                buckets[index].count.SetValue(0);
                buckets[index].fences = {};
                buckets[index].finalized = false;
            }
        }

        void ForceDestroyAllAfterGpuIdle() noexcept
        {
            ForceReclaimAfterGpuIdle();
            concurrency::ScopedLock guard(evictionLock);
            for (u32 kindIndex = 1; kindIndex < static_cast<u32>(ResourceKind::Count); ++kindIndex)
            {
                ResourceTable& table = tables[kindIndex];
                table.VisitSlots([&](const u32 index, ResourceSlot&) noexcept
                {
                    ResourceRef resource{};
                    u32 references = 0;
                    void* payload = nullptr;
                    void* destroyContext = nullptr;
                    DestroyResourceCallback destroy = nullptr;
                    if (!table.TryClaimAny(index, resource, references, payload, destroy, destroyContext)) return;

                    static_cast<void>(destroyingResources.Increment());
                    if (destroy != nullptr) destroy(destroyContext, resource, payload);
                    table.FinishDestroy(resource);
                    static_cast<void>(destroyingResources.Decrement());
                    if (references == 0)
                        static_cast<void>(pendingRetirements.Decrement());
                    else
                    {
                        static_cast<void>(liveResources.Decrement());
                        Subtract(totalReferences, references);
                    }
                    static_cast<void>(completedRetirements.Increment());
                });
            }

            pendingEvictions.SetValue(0);
            for (u32 index = 0; index < bucketCount; ++index)
            {
                buckets[index].count.SetValue(0);
                buckets[index].fences = {};
                buckets[index].finalized = false;
            }
        }

        void ShutdownStorage() noexcept
        {
            retirementQueue.Shutdown();
            for (u32 index = 1; index < static_cast<u32>(ResourceKind::Count); ++index) tables[index].Shutdown();
            initialized = false;
        }
    };

    ResourceLifetimeManager::~ResourceLifetimeManager()
    {
        if (m_impl == nullptr) return;
        WaitForReclamation();
        VG_ASSERT_MSG(m_impl->liveResources.GetValue() == 0,
                      "RHI resource lifetime manager was destroyed while external resource owners remained");
        m_impl->ForceDestroyAllAfterGpuIdle();
        m_impl->ShutdownStorage();
        m_impl->~Impl();
        memory::MemoryBlock block{m_impl, sizeof(Impl), memory::PoolId::Rendering};
        memory::Free(block);
        m_impl = nullptr;
    }

    bool ResourceLifetimeManager::Initialize(const ResourceLifetimeConfig& config,
                                             const FenceCompleteCallback fenceComplete, void* const fenceContext) noexcept
    {
        if (m_impl != nullptr || !memory::IsInitialized() || config.retirementBucketCount < 2 ||
            config.retirementBucketCount > MaximumRetirementBuckets)
            return false;
        memory::MemoryBlock block = memory::Allocate(memory::PoolId::Rendering, sizeof(Impl), alignof(Impl));
        if (!block) return false;
        m_impl = new (block.address) Impl();
        m_impl->fenceComplete = fenceComplete;
        m_impl->fenceContext = fenceContext;
        m_impl->bucketCount = config.retirementBucketCount;

        const u32 capacities[static_cast<u32>(ResourceKind::Count)] = {
            0, config.textureCapacity, config.bufferCapacity, config.heapCapacity, config.samplerStateCapacity,
            config.shaderCapacity, 1, config.pipelineCapacity, config.bindingLayoutCapacity,
            config.descriptorDomainCapacity, config.accelerationStructureCapacity, config.swapChainCapacity,
            config.commandListCapacity};
        u32 totalCapacity = 0;
        for (u32 index = 1; index < static_cast<u32>(ResourceKind::Count); ++index)
        {
            if (!m_impl->tables[index].Initialize(static_cast<ResourceKind>(index), capacities[index]))
            {
                m_impl->ShutdownStorage();
                m_impl->~Impl();
                memory::Free(block);
                m_impl = nullptr;
                return false;
            }
            totalCapacity += capacities[index];
        }
        if (!m_impl->retirementQueue.Initialize(totalCapacity))
        {
            m_impl->ShutdownStorage();
            m_impl->~Impl();
            memory::Free(block);
            m_impl = nullptr;
            return false;
        }
        m_impl->initialized = true;
        return true;
    }

    bool ResourceLifetimeManager::ShutdownAfterGpuIdle() noexcept
    {
        if (m_impl == nullptr || !m_impl->initialized) return false;
        WaitForReclamation();
        if (m_impl->liveResources.GetValue() != 0) return false;
        m_impl->ForceReclaimAfterGpuIdle();
        if (m_impl->pendingRetirements.GetValue() != 0 || m_impl->destroyingResources.GetValue() != 0) return false;
        m_impl->ShutdownStorage();
        m_impl->~Impl();
        memory::MemoryBlock block{m_impl, sizeof(Impl), memory::PoolId::Rendering};
        memory::Free(block);
        m_impl = nullptr;
        return true;
    }

    void ResourceLifetimeManager::ForceShutdownAfterGpuIdle() noexcept
    {
        if (m_impl == nullptr || !m_impl->initialized) return;
        WaitForReclamation();
        m_impl->ForceDestroyAllAfterGpuIdle();
        m_impl->ShutdownStorage();
        m_impl->~Impl();
        memory::MemoryBlock block{m_impl, sizeof(Impl), memory::PoolId::Rendering};
        memory::Free(block);
        m_impl = nullptr;
    }

    bool ResourceLifetimeManager::IsInitialized() const noexcept
    {
        return m_impl != nullptr && m_impl->initialized;
    }

    ResourceRef ResourceLifetimeManager::Create(const ResourceKind kind, void* const payload,
                                                const DestroyResourceCallback destroy, void* const destroyContext,
                                                const u32 initialReferences) noexcept
    {
        if (!IsInitialized()) return {};
        ResourceTable* const table = m_impl->GetTable(kind);
        if (table == nullptr) return {};
        const ResourceRef resource = table->Create(payload, destroy, destroyContext, initialReferences);
        if (resource)
        {
            static_cast<void>(m_impl->liveResources.Increment());
            static_cast<void>(m_impl->totalReferences.ExchangeAdd(initialReferences));
        }
        return resource;
    }

    bool ResourceLifetimeManager::IsValid(const ResourceRef resource) const noexcept
    {
        if (!IsInitialized() || !resource.IsValid()) return false;
        const ResourceTable* const table = m_impl->GetTable(resource.Kind());
        const ResourceSlot* const slot = table != nullptr ? table->Find(resource) : nullptr;
        if (slot == nullptr) return false;
        const u64 identity = slot->identity.GetValue();
        const u32 references = IdentityReferences(identity);
        return IdentityGeneration(identity) == resource.Generation() && references > 0 && references < DestroyingReferenceCount;
    }

    bool ResourceLifetimeManager::AddRef(const ResourceRef resource) noexcept
    {
        if (!IsInitialized() || !resource.IsValid()) return false;
        ResourceTable* const table = m_impl->GetTable(resource.Kind());
        ResourceSlot* const slot = table != nullptr ? table->Find(resource) : nullptr;
        if (slot == nullptr)
        {
            static_cast<void>(m_impl->staleReferenceOperations.Increment());
            return false;
        }
        u64 identity = slot->identity.GetValue();
        for (;;)
        {
            const u32 references = IdentityReferences(identity);
            if (IdentityGeneration(identity) != resource.Generation() || references == 0 || references >= DestroyingReferenceCount - 1u)
            {
                static_cast<void>(m_impl->staleReferenceOperations.Increment());
                return false;
            }
            const u64 desired = PackIdentity(resource.Generation(), references + 1u);
            const u64 observed = slot->identity.CompareExchange(desired, identity);
            if (observed == identity)
            {
                static_cast<void>(m_impl->totalReferences.Increment());
                return true;
            }
            identity = observed;
        }
    }

    i32 ResourceLifetimeManager::Release(const ResourceRef resource) noexcept
    {
        if (!IsInitialized() || !resource.IsValid()) return -1;
        ResourceTable* const table = m_impl->GetTable(resource.Kind());
        ResourceSlot* const slot = table != nullptr ? table->Find(resource) : nullptr;
        if (slot == nullptr)
        {
            static_cast<void>(m_impl->staleReferenceOperations.Increment());
            return -1;
        }
        u64 identity = slot->identity.GetValue();
        for (;;)
        {
            const u32 references = IdentityReferences(identity);
            if (IdentityGeneration(identity) != resource.Generation() || references == 0 || references >= DestroyingReferenceCount)
            {
                static_cast<void>(m_impl->staleReferenceOperations.Increment());
                return -1;
            }
            const u32 remaining = references - 1u;
            const u64 desired = PackIdentity(resource.Generation(), remaining);
            const u64 observed = slot->identity.CompareExchange(desired, identity);
            if (observed != identity)
            {
                identity = observed;
                continue;
            }
            static_cast<void>(m_impl->totalReferences.Decrement());
            if (remaining == 0)
            {
                static_cast<void>(m_impl->liveResources.Decrement());
                const u32 pending = m_impl->pendingRetirements.Increment();
                UpdateMaximum(m_impl->peakPendingRetirements, pending);
                static_cast<void>(m_impl->QueueRetirement(resource, *slot, false));
            }
            return static_cast<i32>(remaining);
        }
    }

    i32 ResourceLifetimeManager::GetRefCount(const ResourceRef resource) const noexcept
    {
        if (!IsInitialized() || !resource.IsValid()) return -1;
        const ResourceTable* const table = m_impl->GetTable(resource.Kind());
        const ResourceSlot* const slot = table != nullptr ? table->Find(resource) : nullptr;
        if (slot == nullptr) return -1;
        const u64 identity = slot->identity.GetValue();
        const u32 references = IdentityReferences(identity);
        return IdentityGeneration(identity) == resource.Generation() && references < DestroyingReferenceCount
                   ? static_cast<i32>(references) : -1;
    }

    void* ResourceLifetimeManager::GetPayload(const ResourceRef resource) noexcept
    {
        if (!IsInitialized() || !resource.IsValid()) return nullptr;
        ResourceTable* const table = m_impl->GetTable(resource.Kind());
        return table != nullptr ? table->GetPayload(resource) : nullptr;
    }

    const void* ResourceLifetimeManager::GetPayload(const ResourceRef resource) const noexcept
    {
        return const_cast<ResourceLifetimeManager*>(this)->GetPayload(resource);
    }

    bool ResourceLifetimeManager::RecordUse(const ResourceRef resource, const QueueType queue,
                                            const u64 submissionFence) noexcept
    {
        if (submissionFence == 0)
        {
            if (m_impl != nullptr) static_cast<void>(m_impl->staleReferenceOperations.Increment());
            return false;
        }

        // Pin the slot while last-use metadata is updated. IsValid followed by a separate update is not
        // sufficient: the final external Release could otherwise retire, destroy and reuse the same slot
        // between those operations, allowing an old command list to modify the new generation.
        if (!AddRef(resource)) return false;
        ResourceSlot* const slot = m_impl->GetTable(resource.Kind())->Find(resource);
        UpdateMaximum(slot->lastUse[QueueIndex(queue)], submissionFence);
        const i32 remainingReferences = Release(resource);
        VG_ASSERT_MSG(remainingReferences >= 0, "RHI resource became invalid while recording its submission fence");
        return true;
    }

    void ResourceLifetimeManager::SealRetirementEpoch(const FenceSet& submittedFences) noexcept
    {
        if (!IsInitialized()) return;
        concurrency::ScopedLock guard(m_impl->epochLock);
        const u32 current = m_impl->enqueueBucket.GetValue();
        RetirementBucket& bucket = m_impl->buckets[current];
        if (bucket.count.GetValue() == 0) return;

        bucket.fences.Include(submittedFences);
        const u32 next = (current + 1u) % m_impl->bucketCount;
        if (next == m_impl->nextFlushBucket && m_impl->buckets[next].count.GetValue() != 0)
        {
            // Keep accepting retirements into the current bucket until the consumer frees space. Its fence set is
            // accumulated on every seal attempt, so delayed advancement is conservative and cannot release early.
            static_cast<void>(m_impl->retirementBucketOverflows.Increment());
            return;
        }

        bucket.finalized = true;
        RetirementBucket& nextBucket = m_impl->buckets[next];
        nextBucket.fences = {};
        nextBucket.finalized = false;
        VG_ASSERT_MSG(nextBucket.count.GetValue() == 0, "RHI advanced into a non-empty retirement bucket");
        m_impl->enqueueBucket.SetValue(next);
    }

    void ResourceLifetimeManager::CollectGarbage() noexcept
    {
        if (!IsInitialized()) return;
        m_impl->RecoverUnqueuedRetirements();
        m_impl->PrepareEvictions();
        if (m_impl->pendingEvictions.GetValue() == 0) return;

        if (!jobs::IsInitialized())
        {
            m_impl->ReclaimReady();
            return;
        }

        concurrency::ScopedLock guard(m_impl->scheduleLock);
        jobs::Builder builder({jobs::Priority::Latent, jobs::Affinity::AnyWorker});
        if (!builder.IsValid())
        {
            m_impl->ReclaimReady();
            return;
        }
        if (m_impl->reclamationCounter.IsValid()) builder.AddDependency(m_impl->reclamationCounter);
        Impl* const impl = m_impl;
        jobs::Task task = jobs::Task::Create([impl](const jobs::JobContext&) noexcept
        {
            impl->ReclaimReady();
            static_cast<void>(impl->reclamationJobs.Decrement());
        });
        if (!task)
        {
            m_impl->ReclaimReady();
            return;
        }
        static_cast<void>(m_impl->reclamationJobs.Increment());
        if (!builder.Dispatch(m_impl->reclamationJobName, static_cast<jobs::Task&&>(task), jobs::Fence::Full))
        {
            static_cast<void>(m_impl->reclamationJobs.Decrement());
            m_impl->ReclaimReady();
            return;
        }
        m_impl->reclamationCounter = builder.ExtractCounter();
    }

    void ResourceLifetimeManager::WaitForReclamation() noexcept
    {
        if (m_impl == nullptr) return;
        concurrency::ScopedLock guard(m_impl->scheduleLock);
        if (m_impl->reclamationCounter.IsValid())
        {
            static_cast<void>(m_impl->reclamationCounter.Wait(true));
            m_impl->reclamationCounter = {};
        }
    }

    ResourceLifetimeStats ResourceLifetimeManager::GetStats() const noexcept
    {
        if (m_impl == nullptr) return {};
        return {m_impl->liveResources.GetValue(), m_impl->pendingRetirements.GetValue(),
                m_impl->peakPendingRetirements.GetValue(), m_impl->destroyingResources.GetValue(),
                m_impl->totalReferences.GetValue(), m_impl->completedRetirements.GetValue(),
                m_impl->staleReferenceOperations.GetValue(), m_impl->retirementQueueRecoveries.GetValue(),
                m_impl->retirementBucketOverflows.GetValue(), m_impl->reclamationJobs.GetValue() != 0};
    }
} // namespace vanguard::rhi::backend
