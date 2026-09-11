#include <vanguard/rendering/render_flow_resource_internal.hpp>

#include <vanguard/memory/memory.hpp>
#include <vanguard/memory/pool.hpp>
#include <vanguard/concurrency/synchronization.hpp>

#include <algorithm>
#include <new>

namespace vanguard::rendering::detail
{
    namespace
    {
        [[nodiscard]] bool ExtentEqual(const rhi::Extent3D& left, const rhi::Extent3D& right) noexcept
        {
            return left.width == right.width && left.height == right.height && left.depth == right.depth;
        }

        [[nodiscard]] bool TextureEqual(const rhi::TextureDesc& left, const rhi::TextureDesc& right) noexcept
        {
            return ExtentEqual(left.extent, right.extent) && left.dimension == right.dimension && left.format == right.format && left.mipCount == right.mipCount &&
                   left.arraySize == right.arraySize && left.sampleCount == right.sampleCount && left.usage == right.usage && left.initialState == right.initialState &&
                   left.virtualResource == right.virtualResource && left.keepInitialState == right.keepInitialState;
        }

        [[nodiscard]] bool BufferClassEqual(const rhi::BufferDesc& left, const rhi::BufferDesc& right) noexcept
        {
            return left.structureStride == right.structureStride && left.format == right.format && left.usage == right.usage && left.initialState == right.initialState &&
                   left.memoryType == right.memoryType && left.virtualResource == right.virtualResource && left.keepInitialState == right.keepInitialState;
        }

        [[nodiscard]] constexpr u64 HashField(const u64 hash, const u64 value) noexcept
        {
            return (hash ^ value) * 1099511628211ull;
        }

        [[nodiscard]] u64 CompatibilityKey(const FrameResourceDesc& desc) noexcept
        {
            u64 hash = HashField(14695981039346656037ull, static_cast<u64>(desc.kind));
            if (desc.kind == FrameResourceKind::Texture)
            {
                const rhi::TextureDesc& value = desc.texture.active;
                hash = HashField(hash, value.extent.width);
                hash = HashField(hash, value.extent.height);
                hash = HashField(hash, value.extent.depth);
                hash = HashField(hash, static_cast<u64>(value.dimension));
                hash = HashField(hash, static_cast<u64>(value.format));
                hash = HashField(hash, value.mipCount);
                hash = HashField(hash, value.arraySize);
                hash = HashField(hash, value.sampleCount);
                hash = HashField(hash, static_cast<u64>(value.usage));
                hash = HashField(hash, static_cast<u64>(value.initialState));
                hash = HashField(hash, value.keepInitialState ? 1u : 0u);
                return HashField(hash, value.virtualResource ? 1u : 0u);
            }
            const rhi::BufferDesc& value = desc.buffer.active;
            hash = HashField(hash, value.structureStride);
            hash = HashField(hash, static_cast<u64>(value.format));
            hash = HashField(hash, static_cast<u64>(value.usage));
            hash = HashField(hash, static_cast<u64>(value.initialState));
            hash = HashField(hash, value.keepInitialState ? 1u : 0u);
            hash = HashField(hash, static_cast<u64>(value.memoryType));
            return HashField(hash, value.virtualResource ? 1u : 0u);
        }

        [[nodiscard]] bool FencesComplete(const rhi::ResidencyFenceSet& fences) noexcept
        {
            return (fences.graphics == 0 || rhi::IsGpuFenceComplete({rhi::QueueType::Graphics, fences.graphics})) &&
                   (fences.compute == 0 || rhi::IsGpuFenceComplete({rhi::QueueType::Compute, fences.compute})) &&
                   (fences.copy == 0 || rhi::IsGpuFenceComplete({rhi::QueueType::Copy, fences.copy}));
        }

        [[nodiscard]] bool PoolFail(RenderFlowResourceFailure* const failure, const RenderFlowResourceFailureCode code, const char* const message,
                                    const RenderFlowResourceSessionState phase = RenderFlowResourceSessionState::Resolving) noexcept
        {
            if (failure != nullptr)
                *failure = {code, phase, {}, {}, {}, message};
            return false;
        }

        [[nodiscard]] bool InjectProviderFailure(DedicatedResourceProviderFailureInjection& injection, const DedicatedResourceProviderFailurePoint point,
                                                 RenderFlowResourceFailure* const failure, const char* const message) noexcept
        {
            if (injection.point != point)
                return false;
            if (injection.passesBeforeFailure != 0)
            {
                --injection.passesBeforeFailure;
                return false;
            }
            const RenderFlowResourceFailureCode code = injection.code;
            injection = {};
            static_cast<void>(PoolFail(failure, code, message));
            return true;
        }

        template <typename Entry> [[nodiscard]] rhi::ResourceRef EntryResource(const Entry& entry) noexcept
        {
            return entry.texture.IsValid() ? rhi::ResourceRef(entry.texture.GetRef()) : rhi::ResourceRef(entry.buffer.GetRef());
        }

        template <typename Entry> [[nodiscard]] bool PoolOwnsOnlyReference(const Entry& entry) noexcept
        {
            if (entry.texture.IsValid() == entry.buffer.IsValid())
                return false;
            return entry.texture.IsValid() ? rhi::GetRefCount(entry.texture.GetRef()) == 1 : rhi::GetRefCount(entry.buffer.GetRef()) == 1;
        }
    } // namespace

    bool AllocatorNativeByteLedger::CanReserve(const u64 bytes) const noexcept
    {
        concurrency::ScopedLock<concurrency::SpinLock> guard(m_lock);
        return bytes <= m_hardLimit - (m_stats.chargedBytes <= m_hardLimit ? m_stats.chargedBytes : m_hardLimit);
    }

    bool AllocatorNativeByteLedger::TryReserve(const FrameResourceKind kind, const u64 bytes) noexcept
    {
        if ((kind != FrameResourceKind::Texture && kind != FrameResourceKind::Buffer) || bytes == 0)
            return false;
        concurrency::ScopedLock<concurrency::SpinLock> guard(m_lock);
        if (bytes > m_hardLimit - (m_stats.chargedBytes <= m_hardLimit ? m_stats.chargedBytes : m_hardLimit))
            return false;
        m_stats.chargedBytes += bytes;
        if (kind == FrameResourceKind::Texture)
            m_stats.textureBytes += bytes;
        else
            m_stats.bufferBytes += bytes;
        return true;
    }

    void AllocatorNativeByteLedger::Release(const FrameResourceKind kind, const u64 bytes) noexcept
    {
        if ((kind != FrameResourceKind::Texture && kind != FrameResourceKind::Buffer) || bytes == 0)
            return;
        concurrency::ScopedLock<concurrency::SpinLock> guard(m_lock);
        if (bytes > m_stats.chargedBytes ||
            (kind == FrameResourceKind::Texture && bytes > m_stats.textureBytes) ||
            (kind == FrameResourceKind::Buffer && bytes > m_stats.bufferBytes))
            return;
        m_stats.chargedBytes -= bytes;
        if (kind == FrameResourceKind::Texture)
            m_stats.textureBytes -= bytes;
        else if (kind == FrameResourceKind::Buffer)
            m_stats.bufferBytes -= bytes;
    }

    AllocatorNativeByteLedgerStats AllocatorNativeByteLedger::GetStats() const noexcept
    {
        concurrency::ScopedLock<concurrency::SpinLock> guard(m_lock);
        return m_stats;
    }

    FrameResourceDesc DedicatedResourcePhysicalDesc(const FrameResourceDesc& desc) noexcept
    {
        FrameResourceDesc result = desc;
        // Whole textures are exact active-shape objects. Maximum texture shape
        // remains planning metadata for the future placed provider. Buffers use
        // their declared maximum as the immutable whole-object capacity.
        if (result.kind == FrameResourceKind::Buffer)
            result.buffer.active.size = result.buffer.maximumSize;
        return result;
    }

    bool DedicatedResourceCanSatisfy(const FrameResourceDesc& availablePhysical, const FrameResourceDesc& requested) noexcept
    {
        const FrameResourceDesc available = DedicatedResourcePhysicalDesc(availablePhysical);
        const FrameResourceDesc required = DedicatedResourcePhysicalDesc(requested);
        if (available.kind != required.kind)
            return false;
        if (required.kind == FrameResourceKind::Texture)
            return TextureEqual(available.texture.active, required.texture.active);
        return required.kind == FrameResourceKind::Buffer && BufferClassEqual(available.buffer.active, required.buffer.active) &&
               available.buffer.active.size >= required.buffer.active.size;
    }

    struct DedicatedResourcePool::Impl
    {
        struct Entry
        {
            FrameResourceDesc desc;
            rhi::Texture texture;
            rhi::Buffer buffer;
            rhi::MemoryRequirements requirements;
            rhi::ResidencyFenceSet safeAfter;
            rhi::NativeReleaseObservation releaseObservation;
            u64 lastUsedFrame = 0;
            u64 compatibilityKey = 0;
            u32 nextCompatible = InvalidDedicatedResourceEntry;
            DedicatedResourceState state = DedicatedResourceState::NativeReleased;
            bool reusableAfterRetirement = true;
        };

        Impl(const RenderFlowResourceAllocatorConfig& allocatorConfig, AllocatorNativeByteLedger& nativeByteLedger) noexcept
            : entries(memory::pools::Rendering::GetInstance()), freeEntries(memory::pools::Rendering::GetInstance()),
              compatibleHeads(memory::pools::Rendering::GetInstance()), config(allocatorConfig), ledger(&nativeByteLedger)
        {
        }

        containers::DynamicArray<Entry> entries;
        containers::DynamicArray<u32> freeEntries;
        containers::HashMap<u64, u32> compatibleHeads;
        mutable concurrency::SpinLock lock;
        RenderFlowResourceAllocatorConfig config;
        AllocatorNativeByteLedger* ledger = nullptr;
        DedicatedResourcePoolStats stats;
        DedicatedResourceProviderFailureInjection failureInjection;

        void AddCharge(const Entry& entry) noexcept
        {
            stats.chargedBytes += entry.requirements.size;
            if (entry.desc.kind == FrameResourceKind::Texture)
                stats.textureBytes += entry.requirements.size;
            else
                stats.bufferBytes += entry.requirements.size;
        }

        void RemoveCharge(const Entry& entry) noexcept
        {
            stats.chargedBytes -= entry.requirements.size;
            if (entry.desc.kind == FrameResourceKind::Texture)
                stats.textureBytes -= entry.requirements.size;
            else
                stats.bufferBytes -= entry.requirements.size;
            ledger->Release(entry.desc.kind, entry.requirements.size);
        }

        void StoreEntry(Entry&& entry, u32& index) noexcept
        {
            entry.compatibilityKey = CompatibilityKey(entry.desc);
            entry.nextCompatible = InvalidDedicatedResourceEntry;
            if (freeEntries.Size() == 0)
            {
                index = entries.Size();
                entries.PushBack(static_cast<Entry&&>(entry));
            }
            else
            {
                index = freeEntries.Back();
                freeEntries.PopBack();
                entries[index] = static_cast<Entry&&>(entry);
            }
            u32 head = InvalidDedicatedResourceEntry;
            static_cast<void>(compatibleHeads.Find(entries[index].compatibilityKey, head));
            entries[index].nextCompatible = head;
            static_cast<void>(compatibleHeads.Set(entries[index].compatibilityKey, index));
        }

        void ReleaseEntrySlot(const u32 index) noexcept
        {
            Entry& entry = entries[index];
            u32 head = InvalidDedicatedResourceEntry;
            if (compatibleHeads.Find(entry.compatibilityKey, head))
            {
                if (head == index)
                {
                    if (entry.nextCompatible == InvalidDedicatedResourceEntry)
                        static_cast<void>(compatibleHeads.Remove(entry.compatibilityKey));
                    else
                        static_cast<void>(compatibleHeads.Set(entry.compatibilityKey, entry.nextCompatible));
                }
                else
                {
                    u32 previous = head;
                    while (previous != InvalidDedicatedResourceEntry && entries[previous].nextCompatible != index)
                        previous = entries[previous].nextCompatible;
                    if (previous != InvalidDedicatedResourceEntry)
                        entries[previous].nextCompatible = entry.nextCompatible;
                }
            }
            entry = {};
            freeEntries.PushBack(index);
        }

        void QuarantineCreatedEntry(Entry&& entry) noexcept
        {
            AddCharge(entry);
            rhi::Failure observationFailure;
            entry.releaseObservation = rhi::ObserveNativeRelease(EntryResource(entry), &observationFailure);
            if (entry.releaseObservation.IsValid())
            {
                entry.texture.Reset();
                entry.buffer.Reset();
                entry.state = DedicatedResourceState::EvictedPendingNativeDestruction;
            }
            else
                entry.state = DedicatedResourceState::ProviderFailureQuarantine;
            u32 ignored = InvalidDedicatedResourceEntry;
            StoreEntry(static_cast<Entry&&>(entry), ignored);
            ++stats.pendingNativeDestruction;
        }
    };

    DedicatedResourcePool::DedicatedResourcePool(const RenderFlowResourceAllocatorConfig& config, AllocatorNativeByteLedger& ledger) noexcept
    {
        memory::MemoryBlock block = memory::Allocate(memory::PoolId::Rendering, sizeof(Impl), alignof(Impl));
        if (block)
            m_impl = new (block.address) Impl(config, ledger);
    }

    DedicatedResourcePool::~DedicatedResourcePool()
    {
        if (m_impl == nullptr)
            return;
        for (Impl::Entry& entry : m_impl->entries)
        {
            if (entry.releaseObservation.IsValid())
                rhi::ReleaseNativeReleaseObservation(entry.releaseObservation);
            entry.texture.Reset();
            entry.buffer.Reset();
            if (entry.requirements.size != 0)
                m_impl->RemoveCharge(entry);
        }
        Impl* const impl = m_impl;
        m_impl = nullptr;
        impl->~Impl();
        memory::MemoryBlock block{impl, sizeof(Impl), memory::PoolId::Rendering};
        memory::Free(block);
    }

    void DedicatedResourcePool::PollUnlocked() noexcept
    {
        if (m_impl == nullptr || !rhi::IsInitialized())
            return;
        m_impl->stats.pendingRetirement = 0;
        m_impl->stats.pendingNativeDestruction = 0;
        for (Impl::Entry& entry : m_impl->entries)
        {
            if (entry.state == DedicatedResourceState::PendingRetirement)
            {
                if (FencesComplete(entry.safeAfter))
                {
                    entry.safeAfter = {};
                    if (entry.reusableAfterRetirement)
                        entry.state = DedicatedResourceState::Reusable;
                    else
                    {
                        // Reuse the observation-before-release quarantine path for an unknown execution state.
                        entry.state = DedicatedResourceState::ProviderFailureQuarantine;
                    }
                }
                else
                    ++m_impl->stats.pendingRetirement;
            }
            if (entry.state == DedicatedResourceState::ProviderFailureQuarantine)
            {
                rhi::Failure ignored;
                entry.releaseObservation = rhi::ObserveNativeRelease(EntryResource(entry), &ignored);
                if (entry.releaseObservation.IsValid())
                {
                    entry.texture.Reset();
                    entry.buffer.Reset();
                    entry.state = DedicatedResourceState::EvictedPendingNativeDestruction;
                }
                else
                {
                    ++m_impl->stats.pendingNativeDestruction;
                    continue;
                }
            }
            if (entry.state != DedicatedResourceState::EvictedPendingNativeDestruction)
                continue;
            if (!rhi::IsNativeReleaseComplete(entry.releaseObservation))
            {
                ++m_impl->stats.pendingNativeDestruction;
                continue;
            }
            rhi::ReleaseNativeReleaseObservation(entry.releaseObservation);
            m_impl->RemoveCharge(entry);
            m_impl->ReleaseEntrySlot(static_cast<u32>(&entry - m_impl->entries.TypedData()));
        }
    }

    void DedicatedResourcePool::Poll() noexcept
    {
        if (m_impl == nullptr)
            return;
        concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
        PollUnlocked();
    }

    bool DedicatedResourcePool::Acquire(const FrameResourceDesc& requested, const u64 frameSerial, DedicatedResourceAssignment& assignment,
                                    RenderFlowResourceFailure* const failure) noexcept
    {
        assignment = {};
        if (m_impl == nullptr)
            return PoolFail(failure, RenderFlowResourceFailureCode::CapacityExceeded, "dedicated-resource pool metadata allocation failed");
        concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
        const FrameResourceDesc desc = DedicatedResourcePhysicalDesc(requested);
        u32 best = InvalidDedicatedResourceEntry;
        u64 bestCapacity = ~u64{0};
        u32 index = InvalidDedicatedResourceEntry;
        static_cast<void>(m_impl->compatibleHeads.Find(CompatibilityKey(desc), index));
        while (index != InvalidDedicatedResourceEntry)
        {
            const Impl::Entry& entry = m_impl->entries[index];
            const u32 next = entry.nextCompatible;
            if (entry.state != DedicatedResourceState::Reusable || !DedicatedResourceCanSatisfy(entry.desc, desc) || !PoolOwnsOnlyReference(entry))
            {
                index = next;
                continue;
            }
            if (desc.kind == FrameResourceKind::Texture)
            {
                best = index;
                break;
            }
            else if (entry.desc.buffer.active.size < bestCapacity)
            {
                best = index;
                bestCapacity = entry.desc.buffer.active.size;
            }
            index = next;
        }
        if (best != InvalidDedicatedResourceEntry)
        {
            Impl::Entry& entry = m_impl->entries[best];
            entry.state = DedicatedResourceState::Assigned;
            entry.lastUsedFrame = frameSerial;
            assignment = {best, entry.texture.GetRef(), entry.buffer.GetRef(), entry.requirements, entry.desc};
            ++m_impl->stats.hits;
            return true;
        }

        if (InjectProviderFailure(m_impl->failureInjection, DedicatedResourceProviderFailurePoint::RequirementQuery, failure,
                                  "injected dedicated-resource requirement-query failure"))
            return false;
        rhi::MemoryRequirements reserved;
        rhi::Failure rhiFailure;
        const bool requirementsOk = desc.kind == FrameResourceKind::Texture ? rhi::GetMemoryRequirements(desc.texture.active, reserved, &rhiFailure)
                                                                            : rhi::GetMemoryRequirements(desc.buffer.active, reserved, &rhiFailure);
        if (!requirementsOk)
            return PoolFail(failure, MapRhiFailure(rhiFailure, RhiFailureContext::BackendContract), "dedicated-resource memory requirement query failed");
        if (reserved.size == 0 || reserved.alignment == 0)
            return PoolFail(failure, RenderFlowResourceFailureCode::BackendContractViolation, "dedicated-resource memory requirement query returned an invalid result");
        containers::DynamicArray<u32> evictionOrder{memory::pools::Rendering::GetInstance()};
        if (!m_impl->ledger->CanReserve(reserved.size))
        {
            // Routine retirement is polled at frame startup. Retry only when
            // pending native destruction could make this reservation fit.
            PollUnlocked();
            if (!m_impl->ledger->CanReserve(reserved.size))
                for (u32 candidateIndex = 0; candidateIndex < m_impl->entries.Size(); ++candidateIndex)
                {
                    const Impl::Entry& candidate = m_impl->entries[candidateIndex];
                    if (candidate.state == DedicatedResourceState::Reusable && PoolOwnsOnlyReference(candidate))
                        evictionOrder.PushBack(candidateIndex);
                }
        }
        std::sort(evictionOrder.Begin(), evictionOrder.End(), [this](const u32 left, const u32 right) noexcept
        {
            const Impl::Entry& leftEntry = m_impl->entries[left];
            const Impl::Entry& rightEntry = m_impl->entries[right];
            return leftEntry.lastUsedFrame != rightEntry.lastUsedFrame ? leftEntry.lastUsedFrame < rightEntry.lastUsedFrame : left < right;
        });
        for (const u32 candidateIndex : evictionOrder)
        {
            if (m_impl->ledger->CanReserve(reserved.size))
                break;
            Impl::Entry& candidate = m_impl->entries[candidateIndex];
            if (InjectProviderFailure(m_impl->failureInjection, DedicatedResourceProviderFailurePoint::NativeReleaseObservation, failure,
                                      "injected dedicated-resource native-release observation failure"))
                return false;
            const rhi::ResourceRef resource = EntryResource(candidate);
            candidate.releaseObservation = rhi::ObserveNativeRelease(resource, &rhiFailure);
            if (!candidate.releaseObservation.IsValid())
                return PoolFail(failure, MapRhiFailure(rhiFailure, RhiFailureContext::BackendContract),
                                rhiFailure.message[0] != '\0' ? rhiFailure.message : "dedicated-resource native-release observation failed");
            candidate.texture.Reset();
            candidate.buffer.Reset();
            candidate.state = DedicatedResourceState::EvictedPendingNativeDestruction;
            PollUnlocked();
        }
        if (!m_impl->ledger->TryReserve(desc.kind, reserved.size))
            return PoolFail(failure, RenderFlowResourceFailureCode::BudgetExceeded, "dedicated-resource hard native-byte budget is exhausted");

        if (InjectProviderFailure(m_impl->failureInjection, DedicatedResourceProviderFailurePoint::NativeCreation, failure,
                                  "injected dedicated-resource native-creation failure"))
        {
            m_impl->ledger->Release(desc.kind, reserved.size);
            return false;
        }
        Impl::Entry entry;
        entry.desc = desc;
        entry.lastUsedFrame = frameSerial;
        if (desc.kind == FrameResourceKind::Texture)
            entry.texture = rhi::Texture(rhi::AdoptReference, rhi::CreateTexture(desc.texture.active, {}, &rhiFailure));
        else
            entry.buffer = rhi::Buffer(rhi::AdoptReference, rhi::CreateBuffer(desc.buffer.active, {}, &rhiFailure));
        if (!entry.texture.IsValid() && !entry.buffer.IsValid())
        {
            m_impl->ledger->Release(desc.kind, reserved.size);
            return PoolFail(failure, MapRhiFailure(rhiFailure, RhiFailureContext::BackendContract), "dedicated-resource native creation failed");
        }
        entry.requirements = reserved;
        const auto failCreated = [&](const RenderFlowResourceFailureCode code, const char* const message) noexcept {
            const rhi::MemoryRequirements observed =
                entry.texture.IsValid() ? rhi::GetMemoryRequirements(entry.texture.GetRef()) : rhi::GetMemoryRequirements(entry.buffer.GetRef());
            if (observed.size != 0 && observed.alignment != 0)
                entry.requirements = observed;
            m_impl->QuarantineCreatedEntry(static_cast<Impl::Entry&&>(entry));
            return PoolFail(failure, code, message);
        };
        if (InjectProviderFailure(m_impl->failureInjection, DedicatedResourceProviderFailurePoint::AuthoritativeDescriptor, failure,
                                  "injected dedicated-resource authoritative-descriptor failure"))
            return failCreated(failure != nullptr ? failure->code : RenderFlowResourceFailureCode::DeviceLostOrBackendFailure,
                               failure != nullptr && failure->message != nullptr && failure->message[0] != '\0' ? failure->message : "injected dedicated-resource authoritative-descriptor failure");
        if (entry.texture.IsValid())
        {
            rhi::TextureDesc authoritativeDesc;
            if (!rhi::GetTextureDesc(entry.texture.GetRef(), authoritativeDesc, &rhiFailure))
                return failCreated(MapRhiFailure(rhiFailure, RhiFailureContext::BackendContract), "created texture descriptor query failed");
            if (!TextureEqual(authoritativeDesc, desc.texture.active))
                return failCreated(RenderFlowResourceFailureCode::BackendContractViolation,
                                   "created texture descriptor differs from its canonical creation descriptor");
        }
        else
        {
            rhi::BufferDesc authoritativeDesc;
            if (!rhi::GetBufferDesc(entry.buffer.GetRef(), authoritativeDesc, &rhiFailure))
                return failCreated(MapRhiFailure(rhiFailure, RhiFailureContext::BackendContract), "created buffer descriptor query failed");
            if (authoritativeDesc.size != desc.buffer.active.size || !BufferClassEqual(authoritativeDesc, desc.buffer.active))
                return failCreated(RenderFlowResourceFailureCode::BackendContractViolation,
                                   "created buffer descriptor differs from its canonical creation descriptor");
        }
        if (InjectProviderFailure(m_impl->failureInjection, DedicatedResourceProviderFailurePoint::AuthoritativeRequirements, failure,
                                  "injected dedicated-resource authoritative-requirements failure"))
            return failCreated(failure != nullptr ? failure->code : RenderFlowResourceFailureCode::DeviceLostOrBackendFailure,
                               failure != nullptr && failure->message != nullptr && failure->message[0] != '\0' ? failure->message : "injected dedicated-resource authoritative-requirements failure");
        const rhi::MemoryRequirements authoritative = entry.texture.IsValid() ? rhi::GetMemoryRequirements(entry.texture.GetRef()) : rhi::GetMemoryRequirements(entry.buffer.GetRef());
        if (authoritative.size == 0 || authoritative.alignment == 0 || authoritative.size != reserved.size || authoritative.alignment != reserved.alignment ||
            authoritative.compatibilityClass != reserved.compatibilityClass)
        {
            if (authoritative.size != 0 && authoritative.alignment != 0)
                entry.requirements = authoritative;
            return failCreated(RenderFlowResourceFailureCode::BackendContractViolation,
                               "created dedicated resource does not match pre-create memory requirements");
        }
        if (InjectProviderFailure(m_impl->failureInjection, DedicatedResourceProviderFailurePoint::PoolCommit, failure,
                                  "injected dedicated-resource pool-commit failure"))
            return failCreated(failure != nullptr ? failure->code : RenderFlowResourceFailureCode::DeviceLostOrBackendFailure,
                               failure != nullptr && failure->message != nullptr && failure->message[0] != '\0' ? failure->message : "injected dedicated-resource pool-commit failure");
        entry.requirements = authoritative;
        entry.state = DedicatedResourceState::Assigned;
        u32 storedIndex = InvalidDedicatedResourceEntry;
        m_impl->StoreEntry(static_cast<Impl::Entry&&>(entry), storedIndex);
        Impl::Entry& stored = m_impl->entries[storedIndex];
        m_impl->AddCharge(stored);
        ++m_impl->stats.misses;
        assignment = {storedIndex, stored.texture.GetRef(), stored.buffer.GetRef(), authoritative, stored.desc};
        return true;
    }

    void DedicatedResourcePool::Rollback(const u32 index) noexcept
    {
        if (m_impl == nullptr)
            return;
        concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
        if (index < m_impl->entries.Size() && m_impl->entries[index].state == DedicatedResourceState::Assigned)
            m_impl->entries[index].state = DedicatedResourceState::Reusable;
    }

    void DedicatedResourcePool::Retire(const u32 index, const rhi::ResidencyFenceSet& safeAfter, const bool reusable) noexcept
    {
        if (m_impl == nullptr)
            return;
        concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
        if (index >= m_impl->entries.Size() || m_impl->entries[index].state != DedicatedResourceState::Assigned)
            return;
        Impl::Entry& entry = m_impl->entries[index];
        entry.safeAfter = safeAfter;
        entry.reusableAfterRetirement = reusable;
        entry.state = DedicatedResourceState::PendingRetirement;
        if (FencesComplete(safeAfter))
        {
            entry.safeAfter = {};
            if (reusable)
                entry.state = DedicatedResourceState::Reusable;
            else
            {
                entry.state = DedicatedResourceState::ProviderFailureQuarantine;
                ++m_impl->stats.pendingNativeDestruction;
            }
        }
        if (entry.state == DedicatedResourceState::PendingRetirement)
            ++m_impl->stats.pendingRetirement;
    }

    bool DedicatedResourcePool::TransferOwnership(const containers::ArraySpan<const u32> entries, RenderFlowResourceFailure* const failure) noexcept
    {
        if (m_impl == nullptr)
            return PoolFail(failure, RenderFlowResourceFailureCode::BackendContractViolation, "dedicated-resource pool is unavailable during export publication",
                            RenderFlowResourceSessionState::Executing);
        concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
        for (u32 position = 0; position < entries.Size(); ++position)
        {
            const u32 index = entries[position];
            if (index >= m_impl->entries.Size() || m_impl->entries[index].state != DedicatedResourceState::Assigned ||
                (m_impl->entries[index].texture.IsValid() == m_impl->entries[index].buffer.IsValid()) || m_impl->entries[index].requirements.size == 0)
                return PoolFail(failure, RenderFlowResourceFailureCode::BackendContractViolation,
                                "terminal export does not reference an assigned dedicated resource", RenderFlowResourceSessionState::Executing);
            const i32 references = m_impl->entries[index].texture.IsValid() ? rhi::GetRefCount(m_impl->entries[index].texture.GetRef())
                                                                            : rhi::GetRefCount(m_impl->entries[index].buffer.GetRef());
            // Finish creates exactly one publication owner before asking the
            // pool to relinquish its owner. Any additional reference escaped a
            // resolved-use scope and makes transfer/reuse unsafe.
            if (references != 2)
                return PoolFail(failure, RenderFlowResourceFailureCode::BackendContractViolation,
                                "terminal export resource has ownership outside the pool and its publication", RenderFlowResourceSessionState::Executing);
            for (u32 prior = 0; prior < position; ++prior)
                if (entries[prior] == index)
                    return PoolFail(failure, RenderFlowResourceFailureCode::BackendContractViolation,
                                    "one dedicated resource cannot be transferred to multiple export slots", RenderFlowResourceSessionState::Executing);
        }

        for (const u32 index : entries)
        {
            Impl::Entry& entry = m_impl->entries[index];
            m_impl->RemoveCharge(entry);
            entry.texture.Reset();
            entry.buffer.Reset();
            m_impl->ReleaseEntrySlot(index);
        }
        return true;
    }

    bool DedicatedResourcePool::TrimToSoftTargets(RenderFlowResourceFailure* const failure) noexcept
    {
        if (m_impl == nullptr)
            return true;
        const AllocatorNativeByteLedgerStats charged = m_impl->ledger->GetStats();
        ResourcePoolTrimState trim{charged.textureBytes, charged.bufferBytes};
        return TrimToSoftTargets(trim, failure);
    }

    bool DedicatedResourcePool::TrimToSoftTargets(ResourcePoolTrimState& trim, RenderFlowResourceFailure* const failure) noexcept
    {
        if (m_impl == nullptr || !rhi::IsInitialized())
            return true;
        concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
        containers::DynamicArray<u32> evictionOrder{memory::pools::Rendering::GetInstance()};
        for (u32 index = 0; index < m_impl->entries.Size(); ++index)
        {
            const Impl::Entry& candidate = m_impl->entries[index];
            if (candidate.state == DedicatedResourceState::Reusable && PoolOwnsOnlyReference(candidate))
                evictionOrder.PushBack(index);
        }
        std::sort(evictionOrder.Begin(), evictionOrder.End(), [this](const u32 left, const u32 right) noexcept
        {
            const Impl::Entry& leftEntry = m_impl->entries[left];
            const Impl::Entry& rightEntry = m_impl->entries[right];
            return leftEntry.lastUsedFrame != rightEntry.lastUsedFrame ? leftEntry.lastUsedFrame < rightEntry.lastUsedFrame : left < right;
        });
        for (const u32 index : evictionOrder)
        {
            Impl::Entry& entry = m_impl->entries[index];
            const bool overTarget = entry.desc.kind == FrameResourceKind::Texture ? trim.projectedTextureBytes > m_impl->config.textureSoftTargetBytes
                                                                                  : trim.projectedBufferBytes > m_impl->config.bufferSoftTargetBytes;
            if (!overTarget)
                continue;
            if (InjectProviderFailure(m_impl->failureInjection, DedicatedResourceProviderFailurePoint::NativeReleaseObservation, failure,
                                      "injected dedicated-resource native-release observation failure"))
                return false;
            const rhi::ResourceRef resource = EntryResource(entry);
            rhi::Failure observationFailure;
            entry.releaseObservation = rhi::ObserveNativeRelease(resource, &observationFailure);
            if (!entry.releaseObservation.IsValid())
                return PoolFail(failure, MapRhiFailure(observationFailure, RhiFailureContext::BackendContract),
                                observationFailure.message[0] != '\0' ? observationFailure.message : "dedicated-resource native-release observation failed",
                                RenderFlowResourceSessionState::Idle);
            entry.texture.Reset();
            entry.buffer.Reset();
            entry.state = DedicatedResourceState::EvictedPendingNativeDestruction;
            if (entry.desc.kind == FrameResourceKind::Texture)
                trim.projectedTextureBytes -= entry.requirements.size;
            else
                trim.projectedBufferBytes -= entry.requirements.size;
        }
        PollUnlocked();
        return true;
    }

    bool DedicatedResourcePool::ClearPersistentCache(RenderFlowResourceFailure* const failure) noexcept
    {
        if (m_impl == nullptr || !rhi::IsInitialized())
            return true;
        concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
        PollUnlocked();
        containers::DynamicArray<u32> candidates{memory::pools::Rendering::GetInstance()};
        containers::DynamicArray<rhi::NativeReleaseObservation> observations{memory::pools::Rendering::GetInstance()};
        for (u32 index = 0; index < m_impl->entries.Size(); ++index)
        {
            const Impl::Entry& entry = m_impl->entries[index];
            if (entry.state != DedicatedResourceState::Reusable && entry.state != DedicatedResourceState::PendingRetirement)
                continue;
            if (!PoolOwnsOnlyReference(entry))
                return PoolFail(failure, RenderFlowResourceFailureCode::BackendContractViolation,
                                "persistent cache contains a resource owner outside the allocator", RenderFlowResourceSessionState::Idle);
            candidates.PushBack(index);
        }
        observations.Reserve(candidates.Size());
        for (const u32 index : candidates)
        {
            if (InjectProviderFailure(m_impl->failureInjection, DedicatedResourceProviderFailurePoint::NativeReleaseObservation, failure,
                                      "injected persistent-cache native-release observation failure"))
            {
                for (rhi::NativeReleaseObservation& prepared : observations)
                    rhi::ReleaseNativeReleaseObservation(prepared);
                return false;
            }
            rhi::Failure observationFailure;
            rhi::NativeReleaseObservation observation = rhi::ObserveNativeRelease(EntryResource(m_impl->entries[index]), &observationFailure);
            if (!observation.IsValid())
            {
                for (rhi::NativeReleaseObservation& prepared : observations)
                    rhi::ReleaseNativeReleaseObservation(prepared);
                return PoolFail(failure, MapRhiFailure(observationFailure, RhiFailureContext::BackendContract),
                                observationFailure.message[0] != '\0' ? observationFailure.message : "persistent-cache native-release observation failed",
                                RenderFlowResourceSessionState::Idle);
            }
            observations.PushBack(observation);
        }
        for (u32 position = 0; position < candidates.Size(); ++position)
        {
            Impl::Entry& entry = m_impl->entries[candidates[position]];
            entry.releaseObservation = observations[position];
            observations[position] = {};
            entry.texture.Reset();
            entry.buffer.Reset();
            entry.safeAfter = {};
            entry.state = DedicatedResourceState::EvictedPendingNativeDestruction;
        }
        PollUnlocked();
        return true;
    }

    void DedicatedResourcePool::SetProviderFailureInjection(const DedicatedResourceProviderFailureInjection& injection) noexcept
    {
        if (m_impl == nullptr)
            return;
        concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
        m_impl->failureInjection = injection;
    }

    void DedicatedResourcePool::ClearProviderFailureInjection() noexcept
    {
        SetProviderFailureInjection({});
    }

    void DedicatedResourcePool::DeviceLost() noexcept
    {
        if (m_impl == nullptr)
            return;
        concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
        for (Impl::Entry& entry : m_impl->entries)
        {
            if (entry.releaseObservation.IsValid())
                rhi::ReleaseNativeReleaseObservation(entry.releaseObservation);
            entry.texture.Reset();
            entry.buffer.Reset();
            if (entry.requirements.size != 0)
                m_impl->RemoveCharge(entry);
            entry = {};
        }
        m_impl->entries.Clear();
        m_impl->freeEntries.Clear();
        m_impl->compatibleHeads.Clear();
        m_impl->stats = {};
    }

    DedicatedResourcePoolStats DedicatedResourcePool::GetStats() const noexcept
    {
        if (m_impl == nullptr)
            return {};
        concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
        const_cast<DedicatedResourcePool*>(this)->PollUnlocked();
        return m_impl->stats;
    }
} // namespace vanguard::rendering::detail
