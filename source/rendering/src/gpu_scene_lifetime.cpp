#include <vanguard/rendering/gpu_scene_lifetime.hpp>

#include <vanguard/concurrency/thread.hpp>
#include <vanguard/containers/containers.hpp>
#include <vanguard/memory/memory.hpp>

#include <new>

namespace vanguard::rendering
{
    namespace
    {
        [[nodiscard]] constexpr bool IsValidTable(const GpuSceneTableKind table) noexcept
        {
            return static_cast<u32>(table) < GpuSceneTableCount;
        }

        [[nodiscard]] constexpr bool UsesIndividualSlots(const GpuSceneTableKind table) noexcept
        {
            return table == GpuSceneTableKind::Instance || table == GpuSceneTableKind::Motion || table == GpuSceneTableKind::Renderable || table == GpuSceneTableKind::PositionDecode ||
                   table == GpuSceneTableKind::Material || table == GpuSceneTableKind::MaterialSet || table == GpuSceneTableKind::Light || table == GpuSceneTableKind::Decal ||
                   table == GpuSceneTableKind::TextureResidency;
        }

        [[nodiscard]] constexpr u32 NextGeneration(const u32 generation) noexcept
        {
            const u32 next = generation + 1u;
            return next != 0 ? next : 1u;
        }

        [[nodiscard]] bool FencesComplete(const rhi::ResidencyFenceSet& fences) noexcept
        {
            return (fences.graphics == 0 || rhi::IsGpuFenceComplete({rhi::QueueType::Graphics, fences.graphics})) &&
                   (fences.compute == 0 || rhi::IsGpuFenceComplete({rhi::QueueType::Compute, fences.compute})) && (fences.copy == 0 || rhi::IsGpuFenceComplete({rhi::QueueType::Copy, fences.copy}));
        }

        [[nodiscard]] constexpr bool HasQueue(const GpuSceneQueueMask mask, const GpuSceneQueueMask queue) noexcept
        {
            return (mask & queue) != GpuSceneQueueMask::None;
        }

        [[nodiscard]] constexpr bool CoversQueues(const rhi::ResidencyFenceSet& fences, const GpuSceneQueueMask queues) noexcept
        {
            return (!HasQueue(queues, GpuSceneQueueMask::Graphics) || fences.Covers(rhi::QueueType::Graphics)) && (!HasQueue(queues, GpuSceneQueueMask::Compute) || fences.Covers(rhi::QueueType::Compute)) &&
                   (!HasQueue(queues, GpuSceneQueueMask::Copy) || fences.Covers(rhi::QueueType::Copy));
        }

        [[nodiscard]] constexpr u64 AllocationIdentity(const GpuSceneAllocation allocation) noexcept
        {
            return (static_cast<u64>(allocation.table) << 32u) | allocation.first;
        }

        void ClearFailure(GpuSceneLifetimeFailure* const failure) noexcept
        {
            if (failure != nullptr)
                *failure = {};
        }

        [[nodiscard]] bool Fail(GpuSceneLifetimeFailure* const failure, const GpuSceneLifetimeFailureCode code, const char* const message, const GpuSceneAllocation allocation = {},
                                const GpuSceneTablesFailure& tableFailure = {}) noexcept
        {
            if (failure != nullptr)
                *failure = {code, allocation, message, tableFailure};
            return false;
        }
    } // namespace

    struct GpuSceneLifetime::Impl
    {
        struct Slot
        {
            u32 generation = 0;
            GpuSceneAllocationState state = GpuSceneAllocationState::Invalid;
        };

        struct FreeRange
        {
            u32 first = 0;
            u32 count = 0;
        };

        struct RangeAllocation
        {
            GpuSceneAllocation allocation;
            GpuSceneAllocationState state = GpuSceneAllocationState::Invalid;
        };

        struct Table
        {
            Table() noexcept
                : slots(memory::pools::Rendering::GetInstance()), recycledSlots(memory::pools::Rendering::GetInstance()), freeRanges(memory::pools::Rendering::GetInstance()),
                  ranges(memory::pools::Rendering::GetInstance()), recycledRangeRecords(memory::pools::Rendering::GetInstance()), rangeLookup(memory::pools::Rendering::GetInstance())
            {
            }

            containers::DynamicArray<Slot> slots;
            containers::DynamicArray<u32> recycledSlots;
            containers::DynamicArray<FreeRange> freeRanges;
            containers::DynamicArray<RangeAllocation> ranges;
            containers::DynamicArray<u32> recycledRangeRecords;
            containers::HashMap<u32, u32> rangeLookup;
            GpuSceneTableLifetimeStats stats;
            u32 nextUnusedSlot = 0;
            u32 nextRangeGeneration = 0;
        };

        enum class EpochState : u8
        {
            Available,
            Open,
            Sealed
        };

        struct RetirementEpoch
        {
            RetirementEpoch() noexcept : allocations(memory::pools::Rendering::GetInstance()) {}

            containers::DynamicArray<GpuSceneAllocation> allocations;
            rhi::ResidencyFenceSet safeAfter;
            EpochState state = EpochState::Available;
        };

        explicit Impl(GpuSceneTables& owner, const GpuSceneLifetimeConfig& config) noexcept
            : batchIdentities(memory::pools::Rendering::GetInstance()), tablesOwner(&owner), retirementQueues(config.retirementQueues), epochCount(config.retirementEpochCount)
        {
            batchIdentities.Reserve(config.initialRetirementsPerEpoch);
            for (u32 index = 0; index < epochCount; ++index)
                epochs[index].allocations.Reserve(config.initialRetirementsPerEpoch);
            epochs[0].state = EpochState::Open;
            stats.retirementEpochCapacity = epochCount;
        }

        Table tables[GpuSceneTableCount];
        RetirementEpoch epochs[MaximumGpuSceneRetirementEpochs];
        containers::HashSet<u64> batchIdentities;
        GpuSceneTables* tablesOwner = nullptr;
        GpuSceneQueueMask retirementQueues = GpuSceneQueueMask::None;
        u32 epochCount = 0;
        u32 openEpoch = 0;
        u32 nextCollectEpoch = 0;
        GpuSceneLifetimeStats stats;

        [[nodiscard]] Table& GetTable(const GpuSceneTableKind table) noexcept
        {
            return tables[static_cast<u32>(table)];
        }

        [[nodiscard]] const Table& GetTable(const GpuSceneTableKind table) const noexcept
        {
            return tables[static_cast<u32>(table)];
        }

        void AddFreeRange(Table& table, FreeRange range) noexcept
        {
            for (u32 index = 0; index < table.freeRanges.Size();)
            {
                const FreeRange existing = table.freeRanges[index];
                const u64 rangeEnd = static_cast<u64>(range.first) + range.count;
                const u64 existingEnd = static_cast<u64>(existing.first) + existing.count;
                if (rangeEnd < existing.first || existingEnd < range.first)
                {
                    ++index;
                    continue;
                }
                const u32 first = range.first < existing.first ? range.first : existing.first;
                const u64 end = rangeEnd > existingEnd ? rangeEnd : existingEnd;
                range = {first, static_cast<u32>(end - first)};
                table.freeRanges.RemoveAt(index);
            }
            table.freeRanges.PushBack(range);
            table.stats.freeRanges = table.freeRanges.Size();
        }

        [[nodiscard]] bool GrowSlotTable(const GpuSceneTableKind kind, Table& table, GpuSceneLifetimeFailure* const failure) noexcept
        {
            GpuSceneTablesFailure tableFailure;
            if (!tablesOwner->EnsureCapacity(kind, table.nextUnusedSlot + 1u, &tableFailure))
                return Fail(failure, GpuSceneLifetimeFailureCode::TableGrowthFailure, "GPU Scene slot table growth failed", {}, tableFailure);
            const u32 capacity = tablesOwner->GetTableStats(kind).elementCapacity;
            if (capacity <= table.slots.Size())
                return Fail(failure, GpuSceneLifetimeFailureCode::CapacityExceeded, "GPU Scene slot metadata did not grow with its GPU table");
            const u32 previousCapacity = table.slots.Size();
            table.slots.Resize(capacity);
            table.stats.capacity = capacity;
            table.stats.free += capacity - previousCapacity;
            return true;
        }

        [[nodiscard]] bool GrowRangeTable(const GpuSceneTableKind kind, Table& table, const u32 minimumCount, GpuSceneLifetimeFailure* const failure) noexcept
        {
            if (minimumCount > 0xffffffffu - table.stats.capacity)
                return Fail(failure, GpuSceneLifetimeFailureCode::CapacityExceeded, "GPU Scene range capacity overflowed");
            GpuSceneTablesFailure tableFailure;
            if (!tablesOwner->EnsureCapacity(kind, table.stats.capacity + minimumCount, &tableFailure))
                return Fail(failure, GpuSceneLifetimeFailureCode::TableGrowthFailure, "GPU Scene range table growth failed", {}, tableFailure);
            const u32 capacity = tablesOwner->GetTableStats(kind).elementCapacity;
            if (capacity <= table.stats.capacity)
                return Fail(failure, GpuSceneLifetimeFailureCode::CapacityExceeded, "GPU Scene range table did not grow");
            const u32 previousCapacity = table.stats.capacity;
            AddFreeRange(table, {previousCapacity, capacity - previousCapacity});
            table.stats.capacity = capacity;
            table.stats.free += capacity - previousCapacity;
            return true;
        }

        [[nodiscard]] RangeAllocation* FindRange(const GpuSceneAllocation allocation) noexcept
        {
            if (!IsValidTable(allocation.table))
                return nullptr;
            Table& table = GetTable(allocation.table);
            const u32* const rangeIndex = table.rangeLookup.FindPtr(allocation.first);
            if (rangeIndex == nullptr || *rangeIndex >= table.ranges.Size())
                return nullptr;
            RangeAllocation& range = table.ranges[*rangeIndex];
            return range.allocation == allocation ? &range : nullptr;
        }

        [[nodiscard]] const RangeAllocation* FindRange(const GpuSceneAllocation allocation) const noexcept
        {
            return const_cast<Impl*>(this)->FindRange(allocation);
        }

        [[nodiscard]] GpuSceneAllocationState GetState(const GpuSceneAllocation allocation) const noexcept
        {
            if (!allocation.IsValid())
                return GpuSceneAllocationState::Invalid;
            const Table& table = GetTable(allocation.table);
            if (UsesIndividualSlots(allocation.table))
            {
                if (allocation.count != 1 || allocation.first >= table.slots.Size())
                    return GpuSceneAllocationState::Invalid;
                const Slot& slot = table.slots[allocation.first];
                return slot.generation == allocation.generation ? slot.state : GpuSceneAllocationState::Invalid;
            }
            const RangeAllocation* const range = FindRange(allocation);
            return range != nullptr ? range->state : GpuSceneAllocationState::Invalid;
        }

        [[nodiscard]] bool ValidateBatch(const containers::ArraySpan<const GpuSceneAllocation> allocations, const GpuSceneAllocationState requiredState, GpuSceneLifetimeFailure* const failure,
                                         const char* const invalidStateMessage) noexcept
        {
            if (allocations.Size() == 0)
            {
                ++stats.rejectedOperations;
                return Fail(failure, GpuSceneLifetimeFailureCode::MismatchedBatch, "GPU Scene lifetime batch is empty");
            }

            batchIdentities.Clear();
            batchIdentities.Reserve(allocations.Size());
            for (const GpuSceneAllocation allocation : allocations)
            {
                if (GetState(allocation) != requiredState)
                {
                    ++stats.rejectedOperations;
                    if (allocation.IsValid() && IsValidTable(allocation.table))
                        ++GetTable(allocation.table).stats.staleOperations;
                    return Fail(failure, GpuSceneLifetimeFailureCode::InvalidState, invalidStateMessage, allocation);
                }
                if (!batchIdentities.Insert(AllocationIdentity(allocation)).IsSuccessful())
                {
                    ++stats.rejectedOperations;
                    return Fail(failure, GpuSceneLifetimeFailureCode::DuplicateAllocation, "GPU Scene lifetime batch contains a duplicate allocation", allocation);
                }
            }
            return true;
        }

        void FreeAllocation(const GpuSceneAllocation allocation) noexcept
        {
            Table& table = GetTable(allocation.table);
            if (UsesIndividualSlots(allocation.table))
            {
                Slot& slot = table.slots[allocation.first];
                slot.state = GpuSceneAllocationState::Invalid;
                table.recycledSlots.PushBack(allocation.first);
                ++table.stats.free;
                return;
            }

            u32* const rangeIndex = table.rangeLookup.FindPtr(allocation.first);
            if (rangeIndex == nullptr)
                return;
            RangeAllocation& range = table.ranges[*rangeIndex];
            const u32 releasedRangeIndex = *rangeIndex;
            static_cast<void>(table.rangeLookup.Remove(allocation.first));
            AddFreeRange(table, {allocation.first, allocation.count});
            range = {};
            table.recycledRangeRecords.PushBack(releasedRangeIndex);
            --table.stats.allocationRanges;
            table.stats.free += allocation.count;
        }
    };

    GpuSceneLifetime::~GpuSceneLifetime()
    {
        if (m_impl != nullptr)
            static_cast<void>(Shutdown());
    }

    bool GpuSceneLifetime::Initialize(GpuSceneTables& tables, const GpuSceneLifetimeConfig& config, GpuSceneLifetimeFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl != nullptr)
            return Fail(failure, GpuSceneLifetimeFailureCode::AlreadyInitialized, "GPU Scene lifetime is already initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, GpuSceneLifetimeFailureCode::WrongThread, "GPU Scene lifetime must initialize on the main thread");
        constexpr u8 validQueueBits = static_cast<u8>(GpuSceneQueueMask::Graphics) | static_cast<u8>(GpuSceneQueueMask::Compute) | static_cast<u8>(GpuSceneQueueMask::Copy);
        if (!tables.IsInitialized() || config.retirementEpochCount < 2 || config.retirementEpochCount > MaximumGpuSceneRetirementEpochs || config.initialRetirementsPerEpoch == 0 ||
            config.retirementQueues == GpuSceneQueueMask::None || (static_cast<u8>(config.retirementQueues) & ~validQueueBits) != 0)
            return Fail(failure, GpuSceneLifetimeFailureCode::InvalidConfiguration, "GPU Scene lifetime configuration is invalid");

        memory::MemoryBlock block = memory::Allocate(memory::PoolId::Rendering, sizeof(Impl), alignof(Impl));
        if (!block)
            return Fail(failure, GpuSceneLifetimeFailureCode::CapacityExceeded, "GPU Scene lifetime metadata allocation failed");
        m_impl = ::new (block.address) Impl(tables, config);
        return true;
    }

    bool GpuSceneLifetime::Shutdown(GpuSceneLifetimeFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return true;
        if (!concurrency::IsMainThread())
            return Fail(failure, GpuSceneLifetimeFailureCode::WrongThread, "GPU Scene lifetime must shutdown on the main thread");
        if (m_impl->stats.allocated != 0 || m_impl->stats.active != 0 || m_impl->stats.retiring != 0)
            return Fail(failure, GpuSceneLifetimeFailureCode::LiveAllocationsRemain, "GPU Scene lifetime cannot shutdown while allocations remain live");
        if (m_impl->stats.pendingRetirements != 0 || m_impl->stats.sealedRetirements != 0 || m_impl->stats.sealedEpochs != 0)
            return Fail(failure, GpuSceneLifetimeFailureCode::UnsealedRetirementsRemain, "GPU Scene lifetime cannot shutdown with retirement work pending");

        Impl* const impl = m_impl;
        m_impl = nullptr;
        impl->~Impl();
        memory::MemoryBlock block{impl, sizeof(Impl), memory::PoolId::Rendering};
        memory::Free(block);
        return true;
    }

    void GpuSceneLifetime::AbandonDevice() noexcept
    {
        if (m_impl == nullptr)
            return;
        Impl* const impl = m_impl;
        m_impl = nullptr;
        impl->~Impl();
        memory::MemoryBlock block{impl, sizeof(Impl), memory::PoolId::Rendering};
        memory::Free(block);
    }

    bool GpuSceneLifetime::IsInitialized() const noexcept
    {
        return m_impl != nullptr;
    }

    bool GpuSceneLifetime::ReserveCapacity(const GpuSceneTableKind kind, const u32 requiredElements, GpuSceneLifetimeFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, GpuSceneLifetimeFailureCode::NotInitialized, "GPU Scene lifetime is not initialized");
        if (!concurrency::IsMainThread())
        {
            ++m_impl->stats.rejectedOperations;
            return Fail(failure, GpuSceneLifetimeFailureCode::WrongThread, "GPU Scene capacity reservation must run on the main thread");
        }
        if (!IsValidTable(kind) || requiredElements == 0 || IsGpuSceneParallelTable(kind))
        {
            ++m_impl->stats.rejectedOperations;
            return Fail(failure, GpuSceneLifetimeFailureCode::InvalidConfiguration, "GPU Scene capacity reservation is invalid");
        }

        Impl::Table& table = m_impl->GetTable(kind);
        if (requiredElements <= table.stats.capacity)
            return true;
        GpuSceneTablesFailure tableFailure;
        if (!m_impl->tablesOwner->EnsureCapacity(kind, requiredElements, &tableFailure))
            return Fail(failure, GpuSceneLifetimeFailureCode::TableGrowthFailure, "GPU Scene capacity reservation failed", {}, tableFailure);
        const u32 capacity = m_impl->tablesOwner->GetTableStats(kind).elementCapacity;
        if (capacity < requiredElements || capacity <= table.stats.capacity)
            return Fail(failure, GpuSceneLifetimeFailureCode::CapacityExceeded, "GPU Scene capacity reservation did not grow its table");

        const u32 previousCapacity = table.stats.capacity;
        if (UsesIndividualSlots(kind))
            table.slots.Resize(capacity);
        else
            m_impl->AddFreeRange(table, {previousCapacity, capacity - previousCapacity});
        table.stats.capacity = capacity;
        table.stats.free += capacity - previousCapacity;
        return true;
    }

    bool GpuSceneLifetime::Allocate(const GpuSceneTableKind kind, const u32 count, GpuSceneAllocation& allocation, GpuSceneLifetimeFailure* const failure) noexcept
    {
        ClearFailure(failure);
        allocation = {};
        if (m_impl == nullptr)
            return Fail(failure, GpuSceneLifetimeFailureCode::NotInitialized, "GPU Scene lifetime is not initialized");
        if (!concurrency::IsMainThread())
        {
            ++m_impl->stats.rejectedOperations;
            return Fail(failure, GpuSceneLifetimeFailureCode::WrongThread, "GPU Scene allocation must run on the main thread");
        }
        if (IsGpuSceneParallelTable(kind))
        {
            ++m_impl->stats.rejectedOperations;
            return Fail(failure, GpuSceneLifetimeFailureCode::ParallelTableHasNoIndependentLifetime, "GPU Scene parallel tables are owned by their same-index topology tables");
        }
        if (!IsValidTable(kind) || count == 0 || (UsesIndividualSlots(kind) && count != 1))
        {
            ++m_impl->stats.rejectedOperations;
            return Fail(failure, GpuSceneLifetimeFailureCode::InvalidConfiguration, "GPU Scene allocation request is invalid");
        }

        Impl::Table& table = m_impl->GetTable(kind);
        if (UsesIndividualSlots(kind))
        {
            u32 index = InvalidGpuSceneIndex;
            if (!table.recycledSlots.Empty())
                index = table.recycledSlots.PopBack();
            else
            {
                if (table.nextUnusedSlot == table.slots.Size() && !m_impl->GrowSlotTable(kind, table, failure))
                {
                    ++table.stats.failedAllocations;
                    return false;
                }
                index = table.nextUnusedSlot++;
            }
            Impl::Slot& slot = table.slots[index];
            slot.generation = NextGeneration(slot.generation);
            slot.state = GpuSceneAllocationState::Allocated;
            allocation = {kind, index, 1, slot.generation};
        }
        else
        {
            u32 bestRange = InvalidGpuSceneIndex;
            u32 bestWaste = 0xffffffffu;
            for (u32 index = 0; index < table.freeRanges.Size(); ++index)
            {
                const Impl::FreeRange& range = table.freeRanges[index];
                if (range.count < count)
                    continue;
                const u32 waste = range.count - count;
                if (waste < bestWaste)
                {
                    bestRange = index;
                    bestWaste = waste;
                    if (waste == 0)
                        break;
                }
            }
            if (bestRange == InvalidGpuSceneIndex)
            {
                if (!m_impl->GrowRangeTable(kind, table, count, failure))
                {
                    ++table.stats.failedAllocations;
                    return false;
                }
                for (u32 index = 0; index < table.freeRanges.Size(); ++index)
                    if (table.freeRanges[index].count >= count)
                    {
                        bestRange = index;
                        break;
                    }
            }
            if (bestRange == InvalidGpuSceneIndex)
            {
                ++table.stats.failedAllocations;
                return Fail(failure, GpuSceneLifetimeFailureCode::CapacityExceeded, "GPU Scene range allocator could not satisfy a grown table");
            }

            Impl::FreeRange& freeRange = table.freeRanges[bestRange];
            const u32 first = freeRange.first;
            freeRange.first += count;
            freeRange.count -= count;
            if (freeRange.count == 0)
                table.freeRanges.RemoveAt(bestRange);
            table.stats.freeRanges = table.freeRanges.Size();

            u32 rangeIndex = 0;
            if (!table.recycledRangeRecords.Empty())
                rangeIndex = table.recycledRangeRecords.PopBack();
            else
            {
                rangeIndex = table.ranges.Size();
                table.ranges.Grow(1);
            }
            table.nextRangeGeneration = NextGeneration(table.nextRangeGeneration);
            allocation = {kind, first, count, table.nextRangeGeneration};
            table.ranges[rangeIndex] = {allocation, GpuSceneAllocationState::Allocated};
            static_cast<void>(table.rangeLookup.Insert(first, rangeIndex));
            ++table.stats.allocationRanges;
        }

        ++table.stats.allocations;
        table.stats.allocated += count;
        table.stats.free -= count;
        ++m_impl->stats.allocations;
        m_impl->stats.allocated += count;
        return true;
    }

    bool GpuSceneLifetime::AllocateBatch(const containers::ArraySpan<const GpuSceneAllocationRequest> requests, const containers::ArraySpan<GpuSceneAllocation> allocations,
                                         GpuSceneLifetimeFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, GpuSceneLifetimeFailureCode::NotInitialized, "GPU Scene lifetime is not initialized");
        if (!concurrency::IsMainThread())
        {
            ++m_impl->stats.rejectedOperations;
            return Fail(failure, GpuSceneLifetimeFailureCode::WrongThread, "GPU Scene batch allocation must run on the main thread");
        }
        if (requests.Size() == 0 || requests.Size() != allocations.Size())
        {
            ++m_impl->stats.rejectedOperations;
            return Fail(failure, GpuSceneLifetimeFailureCode::MismatchedBatch, "GPU Scene allocation request and output batches do not match");
        }

        for (u32 index = 0; index < allocations.Size(); ++index)
            allocations[index] = {};
        for (const GpuSceneAllocationRequest request : requests)
        {
            if (!IsValidTable(request.table) || IsGpuSceneParallelTable(request.table) || request.count == 0 || (UsesIndividualSlots(request.table) && request.count != 1))
            {
                ++m_impl->stats.rejectedOperations;
                return Fail(failure, GpuSceneLifetimeFailureCode::InvalidConfiguration, "GPU Scene batch contains an invalid allocation request");
            }
        }

        u32 allocatedCount = 0;
        for (; allocatedCount < requests.Size(); ++allocatedCount)
        {
            const GpuSceneAllocationRequest request = requests[allocatedCount];
            if (!Allocate(request.table, request.count, allocations[allocatedCount], failure))
            {
                for (u32 rollback = 0; rollback < allocatedCount; ++rollback)
                {
                    static_cast<void>(Cancel(allocations[rollback]));
                    allocations[rollback] = {};
                }
                return false;
            }
        }
        return true;
    }

    bool GpuSceneLifetime::CommitInitialPublication(const GpuSceneAllocation allocation, GpuSceneLifetimeFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, GpuSceneLifetimeFailureCode::NotInitialized, "GPU Scene lifetime is not initialized", allocation);
        if (!concurrency::IsMainThread())
            return Fail(failure, GpuSceneLifetimeFailureCode::WrongThread, "GPU Scene initial publication must run on the main thread", allocation);
        if (m_impl->GetState(allocation) != GpuSceneAllocationState::Allocated)
        {
            ++m_impl->stats.rejectedOperations;
            if (allocation.IsValid() && IsValidTable(allocation.table))
                ++m_impl->GetTable(allocation.table).stats.staleOperations;
            return Fail(failure, GpuSceneLifetimeFailureCode::InvalidState, "only a current allocated GPU Scene identity can publish", allocation);
        }

        Impl::Table& table = m_impl->GetTable(allocation.table);
        if (UsesIndividualSlots(allocation.table))
            table.slots[allocation.first].state = GpuSceneAllocationState::Active;
        else
            m_impl->FindRange(allocation)->state = GpuSceneAllocationState::Active;
        table.stats.allocated -= allocation.count;
        table.stats.active += allocation.count;
        ++table.stats.activations;
        m_impl->stats.allocated -= allocation.count;
        m_impl->stats.active += allocation.count;
        return true;
    }

    bool GpuSceneLifetime::CommitInitialPublications(const containers::ArraySpan<const GpuSceneAllocation> allocations, GpuSceneLifetimeFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, GpuSceneLifetimeFailureCode::NotInitialized, "GPU Scene lifetime is not initialized");
        if (!concurrency::IsMainThread())
        {
            ++m_impl->stats.rejectedOperations;
            return Fail(failure, GpuSceneLifetimeFailureCode::WrongThread, "GPU Scene batch initial publication must run on the main thread");
        }
        if (!m_impl->ValidateBatch(allocations, GpuSceneAllocationState::Allocated, failure, "only current allocated GPU Scene identities can publish"))
            return false;

        for (const GpuSceneAllocation allocation : allocations)
        {
            Impl::Table& table = m_impl->GetTable(allocation.table);
            if (UsesIndividualSlots(allocation.table))
                table.slots[allocation.first].state = GpuSceneAllocationState::Active;
            else
                m_impl->FindRange(allocation)->state = GpuSceneAllocationState::Active;
            table.stats.allocated -= allocation.count;
            table.stats.active += allocation.count;
            ++table.stats.activations;
            m_impl->stats.allocated -= allocation.count;
            m_impl->stats.active += allocation.count;
        }
        return true;
    }

    bool GpuSceneLifetime::Cancel(const GpuSceneAllocation allocation, GpuSceneLifetimeFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, GpuSceneLifetimeFailureCode::NotInitialized, "GPU Scene lifetime is not initialized", allocation);
        if (!concurrency::IsMainThread())
            return Fail(failure, GpuSceneLifetimeFailureCode::WrongThread, "GPU Scene cancellation must run on the main thread", allocation);
        if (m_impl->GetState(allocation) != GpuSceneAllocationState::Allocated)
        {
            ++m_impl->stats.rejectedOperations;
            return Fail(failure, GpuSceneLifetimeFailureCode::InvalidState, "only an unactivated GPU Scene allocation can cancel", allocation);
        }
        Impl::Table& table = m_impl->GetTable(allocation.table);
        table.stats.allocated -= allocation.count;
        ++table.stats.cancellations;
        m_impl->stats.allocated -= allocation.count;
        m_impl->FreeAllocation(allocation);
        return true;
    }

    bool GpuSceneLifetime::CancelBatch(const containers::ArraySpan<const GpuSceneAllocation> allocations, GpuSceneLifetimeFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, GpuSceneLifetimeFailureCode::NotInitialized, "GPU Scene lifetime is not initialized");
        if (!concurrency::IsMainThread())
        {
            ++m_impl->stats.rejectedOperations;
            return Fail(failure, GpuSceneLifetimeFailureCode::WrongThread, "GPU Scene batch cancellation must run on the main thread");
        }
        if (!m_impl->ValidateBatch(allocations, GpuSceneAllocationState::Allocated, failure, "only unactivated GPU Scene allocations can cancel"))
            return false;

        for (const GpuSceneAllocation allocation : allocations)
        {
            Impl::Table& table = m_impl->GetTable(allocation.table);
            table.stats.allocated -= allocation.count;
            ++table.stats.cancellations;
            m_impl->stats.allocated -= allocation.count;
            m_impl->FreeAllocation(allocation);
        }
        return true;
    }

    bool GpuSceneLifetime::Retire(const GpuSceneAllocation allocation, GpuSceneLifetimeFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, GpuSceneLifetimeFailureCode::NotInitialized, "GPU Scene lifetime is not initialized", allocation);
        if (!concurrency::IsMainThread())
            return Fail(failure, GpuSceneLifetimeFailureCode::WrongThread, "GPU Scene retirement must run on the main thread", allocation);
        if (m_impl->GetState(allocation) != GpuSceneAllocationState::Active)
        {
            ++m_impl->stats.rejectedOperations;
            return Fail(failure, GpuSceneLifetimeFailureCode::InvalidState, "only an active GPU Scene allocation can retire", allocation);
        }

        Impl::Table& table = m_impl->GetTable(allocation.table);
        if (UsesIndividualSlots(allocation.table))
            table.slots[allocation.first].state = GpuSceneAllocationState::Retiring;
        else
            m_impl->FindRange(allocation)->state = GpuSceneAllocationState::Retiring;
        table.stats.active -= allocation.count;
        table.stats.retiring += allocation.count;
        ++table.stats.retirements;
        m_impl->epochs[m_impl->openEpoch].allocations.PushBack(allocation);
        m_impl->stats.active -= allocation.count;
        m_impl->stats.retiring += allocation.count;
        ++m_impl->stats.retirements;
        ++m_impl->stats.pendingRetirements;
        return true;
    }

    bool GpuSceneLifetime::RetireBatch(const containers::ArraySpan<const GpuSceneAllocation> allocations, GpuSceneLifetimeFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, GpuSceneLifetimeFailureCode::NotInitialized, "GPU Scene lifetime is not initialized");
        if (!concurrency::IsMainThread())
        {
            ++m_impl->stats.rejectedOperations;
            return Fail(failure, GpuSceneLifetimeFailureCode::WrongThread, "GPU Scene batch retirement must run on the main thread");
        }
        if (!m_impl->ValidateBatch(allocations, GpuSceneAllocationState::Active, failure, "only active GPU Scene allocations can retire"))
            return false;

        Impl::RetirementEpoch& epoch = m_impl->epochs[m_impl->openEpoch];
        epoch.allocations.Reserve(epoch.allocations.Size() + allocations.Size());
        for (const GpuSceneAllocation allocation : allocations)
        {
            Impl::Table& table = m_impl->GetTable(allocation.table);
            if (UsesIndividualSlots(allocation.table))
                table.slots[allocation.first].state = GpuSceneAllocationState::Retiring;
            else
                m_impl->FindRange(allocation)->state = GpuSceneAllocationState::Retiring;
            table.stats.active -= allocation.count;
            table.stats.retiring += allocation.count;
            ++table.stats.retirements;
            epoch.allocations.PushBack(allocation);
            m_impl->stats.active -= allocation.count;
            m_impl->stats.retiring += allocation.count;
            ++m_impl->stats.retirements;
            ++m_impl->stats.pendingRetirements;
        }
        return true;
    }

    bool GpuSceneLifetime::SealRetirements(const rhi::ResidencyFenceSet& safeAfter, GpuSceneLifetimeFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, GpuSceneLifetimeFailureCode::NotInitialized, "GPU Scene lifetime is not initialized");
        if (!concurrency::IsMainThread())
        {
            ++m_impl->stats.rejectedOperations;
            return Fail(failure, GpuSceneLifetimeFailureCode::WrongThread, "GPU Scene retirement sealing must run on the main thread");
        }

        Impl::RetirementEpoch& epoch = m_impl->epochs[m_impl->openEpoch];
        if (epoch.allocations.Empty())
            return true;
        if (!CoversQueues(safeAfter, m_impl->retirementQueues))
        {
            ++m_impl->stats.rejectedOperations;
            return Fail(failure, GpuSceneLifetimeFailureCode::MissingRetirementFence, "GPU Scene retirement epoch does not cover every configured queue");
        }

        const u32 nextEpoch = (m_impl->openEpoch + 1u) % m_impl->epochCount;
        if (m_impl->epochs[nextEpoch].state != Impl::EpochState::Available)
        {
            ++m_impl->stats.epochCapacityStalls;
            return Fail(failure, GpuSceneLifetimeFailureCode::RetirementEpochsExhausted, "GPU Scene retirement epoch ring is full");
        }

        epoch.safeAfter = safeAfter;
        epoch.state = Impl::EpochState::Sealed;
        m_impl->stats.pendingRetirements -= epoch.allocations.Size();
        m_impl->stats.sealedRetirements += epoch.allocations.Size();
        ++m_impl->stats.sealedEpochs;
        ++m_impl->stats.epochsSealed;

        m_impl->openEpoch = nextEpoch;
        Impl::RetirementEpoch& open = m_impl->epochs[nextEpoch];
        open.safeAfter = {};
        open.state = Impl::EpochState::Open;
        return true;
    }

    u32 GpuSceneLifetime::Collect(GpuSceneLifetimeFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
        {
            static_cast<void>(Fail(failure, GpuSceneLifetimeFailureCode::NotInitialized, "GPU Scene lifetime is not initialized"));
            return 0;
        }
        if (!concurrency::IsMainThread())
        {
            static_cast<void>(Fail(failure, GpuSceneLifetimeFailureCode::WrongThread, "GPU Scene retirement collection must run on the main thread"));
            return 0;
        }

        ++m_impl->stats.collectionPasses;
        u32 reclaimed = 0;
        for (u32 visited = 0; visited < m_impl->epochCount; ++visited)
        {
            Impl::RetirementEpoch& epoch = m_impl->epochs[m_impl->nextCollectEpoch];
            if (epoch.state != Impl::EpochState::Sealed)
                break;
            ++m_impl->stats.epochFencePolls;
            if (!FencesComplete(epoch.safeAfter))
                break;

            for (const GpuSceneAllocation allocation : epoch.allocations)
            {
                if (m_impl->GetState(allocation) != GpuSceneAllocationState::Retiring)
                {
                    static_cast<void>(Fail(failure, GpuSceneLifetimeFailureCode::InvalidState, "GPU Scene retirement epoch contains a stale allocation", allocation));
                    return reclaimed;
                }
            }
            for (const GpuSceneAllocation allocation : epoch.allocations)
            {
                Impl::Table& table = m_impl->GetTable(allocation.table);
                table.stats.retiring -= allocation.count;
                ++table.stats.reclaimed;
                m_impl->stats.retiring -= allocation.count;
                ++m_impl->stats.reclaimed;
                m_impl->FreeAllocation(allocation);
                ++reclaimed;
            }

            m_impl->stats.sealedRetirements -= epoch.allocations.Size();
            --m_impl->stats.sealedEpochs;
            epoch.allocations.Clear();
            epoch.safeAfter = {};
            epoch.state = Impl::EpochState::Available;
            m_impl->nextCollectEpoch = (m_impl->nextCollectEpoch + 1u) % m_impl->epochCount;
        }
        return reclaimed;
    }

    bool GpuSceneLifetime::IsValid(const GpuSceneAllocation allocation) const noexcept
    {
        return m_impl != nullptr && m_impl->GetState(allocation) != GpuSceneAllocationState::Invalid;
    }

    GpuSceneAllocationState GpuSceneLifetime::GetState(const GpuSceneAllocation allocation) const noexcept
    {
        return m_impl != nullptr ? m_impl->GetState(allocation) : GpuSceneAllocationState::Invalid;
    }

    GpuSceneTableLifetimeStats GpuSceneLifetime::GetTableStats(const GpuSceneTableKind table) const noexcept
    {
        return m_impl != nullptr && IsValidTable(table) ? m_impl->GetTable(table).stats : GpuSceneTableLifetimeStats{};
    }

    GpuSceneLifetimeStats GpuSceneLifetime::GetStats() const noexcept
    {
        return m_impl != nullptr ? m_impl->stats : GpuSceneLifetimeStats{};
    }
} // namespace vanguard::rendering
