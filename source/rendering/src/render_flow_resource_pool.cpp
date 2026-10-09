#include <vanguard/rendering/render_flow_resource_internal.hpp>

#include <vanguard/memory/memory.hpp>
#include <vanguard/memory/pool.hpp>
#include <vanguard/concurrency/synchronization.hpp>
#include <vanguard/profiling/profiling.hpp>

#include <algorithm>
#include <new>

namespace vanguard::rendering::detail
{
    namespace
    {
        [[nodiscard]] bool TextureEqual(const rhi::TextureDesc& left, const rhi::TextureDesc& right) noexcept
        {
            return PhysicalTextureDescEqual(left, right);
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

        [[nodiscard]] u64 CompatibilityKey(const DedicatedResourceClassKey& compatibility) noexcept
        {
            u64 hash = HashField(14695981039346656037ull, static_cast<u64>(compatibility.kind));
            if (compatibility.kind == FrameResourceKind::Texture)
            {
                hash = HashField(hash, compatibility.textureExtent.width);
                hash = HashField(hash, compatibility.textureExtent.height);
                hash = HashField(hash, compatibility.textureExtent.depth);
                hash = HashField(hash, static_cast<u64>(compatibility.textureDimension));
                hash = HashField(hash, static_cast<u64>(compatibility.format));
                hash = HashField(hash, compatibility.textureMipCount);
                hash = HashField(hash, compatibility.textureArraySize);
                hash = HashField(hash, compatibility.textureSampleCount);
                hash = HashField(hash, compatibility.usageBits);
                hash = HashField(hash, static_cast<u64>(compatibility.initialState));
                hash = HashField(hash, compatibility.keepInitialState ? 1u : 0u);
                return HashField(hash, compatibility.virtualResource ? 1u : 0u);
            }
            if (compatibility.kind == FrameResourceKind::Buffer)
            {
                hash = HashField(hash, compatibility.bufferStructureStride);
                hash = HashField(hash, static_cast<u64>(compatibility.format));
                hash = HashField(hash, compatibility.usageBits);
                hash = HashField(hash, static_cast<u64>(compatibility.initialState));
                hash = HashField(hash, compatibility.keepInitialState ? 1u : 0u);
                hash = HashField(hash, static_cast<u64>(compatibility.bufferMemoryType));
                return HashField(hash, compatibility.virtualResource ? 1u : 0u);
            }
            return hash;
        }

        [[nodiscard]] bool FencesComplete(const rhi::ResidencyFenceSet& fences) noexcept
        {
            return (fences.graphics == 0 || rhi::IsGpuFenceComplete({rhi::QueueType::Graphics, fences.graphics})) &&
                   (fences.compute == 0 || rhi::IsGpuFenceComplete({rhi::QueueType::Compute, fences.compute})) &&
                   (fences.copy == 0 || rhi::IsGpuFenceComplete({rhi::QueueType::Copy, fences.copy}));
        }

        [[nodiscard]] constexpr bool FenceSetsEqual(const rhi::ResidencyFenceSet& left, const rhi::ResidencyFenceSet& right) noexcept
        {
            return left.graphics == right.graphics && left.compute == right.compute && left.copy == right.copy;
        }

        template <typename Enum> [[nodiscard]] constexpr bool HasAny(const Enum value, const Enum flags) noexcept
        {
            return (static_cast<u32>(value) & static_cast<u32>(flags)) != 0;
        }

        [[nodiscard]] bool PoolFail(RenderFlowResourceFailure* const failure, const RenderFlowResourceFailureCode code, const char* const message,
                                    const RenderFlowResourceSessionState phase = RenderFlowResourceSessionState::Resolving) noexcept
        {
            if (failure != nullptr)
                *failure = {code, phase, {}, {}, {}, message};
            return false;
        }

        [[nodiscard]] bool InjectProviderFailure(DedicatedResourceProviderFailureInjection& injection, const DedicatedResourceProviderFailurePoint point,
                                                 RenderFlowResourceFailure* const failure, const char* const message,
                                                 const RenderFlowResourceSessionState phase = RenderFlowResourceSessionState::Resolving) noexcept
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
            static_cast<void>(PoolFail(failure, code, message, phase));
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
            const i32 expected = 1 + static_cast<i32>(CanonicalDescriptorReferenceCount(entry.descriptors));
            return entry.texture.IsValid() ? rhi::GetRefCount(entry.texture.GetRef()) == expected : rhi::GetRefCount(entry.buffer.GetRef()) == expected;
        }

        [[nodiscard]] rhi::DescriptorRetirement DescriptorRetirementFor(const rhi::ResidencyFenceSet& fences) noexcept
        {
            return {fences.graphics, fences.compute, fences.copy};
        }
    } // namespace

    bool AllocatorDescriptorLedger::CanReserve(const u32 slots) const noexcept
    {
        concurrency::ScopedLock<concurrency::SpinLock> guard(m_lock);
        return slots <= m_maximum - (m_stats.liveSlots <= m_maximum ? m_stats.liveSlots : m_maximum);
    }

    bool AllocatorDescriptorLedger::TryReserve(const u32 slots) noexcept
    {
        if (slots == 0)
            return true;
        concurrency::ScopedLock<concurrency::SpinLock> guard(m_lock);
        if (slots > m_maximum - (m_stats.liveSlots <= m_maximum ? m_stats.liveSlots : m_maximum))
        {
            ++m_stats.rejectedReservations;
            return false;
        }
        m_stats.liveSlots += slots;
        if (m_stats.peakLiveSlots < m_stats.liveSlots)
            m_stats.peakLiveSlots = m_stats.liveSlots;
        return true;
    }

    void AllocatorDescriptorLedger::Release(const u32 slots) noexcept
    {
        if (slots == 0)
            return;
        concurrency::ScopedLock<concurrency::SpinLock> guard(m_lock);
        VG_ASSERT(slots <= m_stats.liveSlots);
        if (slots <= m_stats.liveSlots)
            m_stats.liveSlots -= slots;
    }

    void AllocatorDescriptorLedger::MarkReusable(const u32 slots) noexcept
    {
        if (slots == 0)
            return;
        concurrency::ScopedLock<concurrency::SpinLock> guard(m_lock);
        VG_ASSERT(m_stats.reusableSlots <= m_stats.liveSlots && slots <= m_stats.liveSlots - m_stats.reusableSlots);
        if (slots > m_stats.liveSlots - m_stats.reusableSlots)
            return;
        m_stats.reusableSlots += slots;
        if (m_stats.peakReusableSlots < m_stats.reusableSlots)
            m_stats.peakReusableSlots = m_stats.reusableSlots;
    }

    void AllocatorDescriptorLedger::MarkNotReusable(const u32 slots) noexcept
    {
        if (slots == 0)
            return;
        concurrency::ScopedLock<concurrency::SpinLock> guard(m_lock);
        VG_ASSERT(slots <= m_stats.reusableSlots);
        if (slots <= m_stats.reusableSlots)
            m_stats.reusableSlots -= slots;
    }

    void AllocatorDescriptorLedger::RecordPublicationHit() noexcept
    {
        concurrency::ScopedLock<concurrency::SpinLock> guard(m_lock);
        ++m_stats.publicationHits;
    }

    void AllocatorDescriptorLedger::RecordPublicationMiss() noexcept
    {
        concurrency::ScopedLock<concurrency::SpinLock> guard(m_lock);
        ++m_stats.publicationMisses;
    }

    void AllocatorDescriptorLedger::RecordPressureEviction() noexcept
    {
        concurrency::ScopedLock<concurrency::SpinLock> guard(m_lock);
        ++m_stats.pressureEvictions;
    }

    void AllocatorDescriptorLedger::RecordMaintenanceEviction() noexcept
    {
        concurrency::ScopedLock<concurrency::SpinLock> guard(m_lock);
        ++m_stats.maintenanceEvictions;
    }

    AllocatorDescriptorLedgerStats AllocatorDescriptorLedger::GetStats() const noexcept
    {
        concurrency::ScopedLock<concurrency::SpinLock> guard(m_lock);
        return m_stats;
    }

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

    DedicatedResourceRequest BuildDedicatedResourceRequest(const FrameResourceDesc& desc) noexcept
    {
        DedicatedResourceRequest request;
        request.physicalDesc = DedicatedResourcePhysicalDesc(desc);
        request.compatibility.kind = request.physicalDesc.kind;
        if (request.physicalDesc.kind == FrameResourceKind::Texture)
        {
            const rhi::TextureDesc& value = request.physicalDesc.texture.active;
            request.compatibility.textureExtent = value.extent;
            request.compatibility.usageBits = static_cast<u32>(value.usage);
            request.compatibility.format = value.format;
            request.compatibility.initialState = value.initialState;
            request.compatibility.textureMipCount = value.mipCount;
            request.compatibility.textureArraySize = value.arraySize;
            request.compatibility.textureDimension = value.dimension;
            request.compatibility.textureSampleCount = value.sampleCount;
            request.compatibility.virtualResource = value.virtualResource;
            request.compatibility.keepInitialState = value.keepInitialState;
        }
        else if (request.physicalDesc.kind == FrameResourceKind::Buffer)
        {
            const rhi::BufferDesc& value = request.physicalDesc.buffer.active;
            request.compatibility.bufferStructureStride = value.structureStride;
            request.compatibility.usageBits = static_cast<u32>(value.usage);
            request.compatibility.format = value.format;
            request.compatibility.initialState = value.initialState;
            request.compatibility.bufferMemoryType = value.memoryType;
            request.compatibility.virtualResource = value.virtualResource;
            request.compatibility.keepInitialState = value.keepInitialState;
            request.capacity = value.size;
        }
        request.compatibility.hash = CompatibilityKey(request.compatibility);
        return request;
    }

    bool DedicatedResourceSameCompatibilityClass(const DedicatedResourceClassKey& left, const DedicatedResourceClassKey& right) noexcept
    {
        if (left.hash != right.hash || left.kind != right.kind)
            return false;
        if (left.kind == FrameResourceKind::Texture)
            return left.textureExtent.width == right.textureExtent.width && left.textureExtent.height == right.textureExtent.height && left.textureExtent.depth == right.textureExtent.depth &&
                   left.textureDimension == right.textureDimension && left.format == right.format && left.textureMipCount == right.textureMipCount && left.textureArraySize == right.textureArraySize &&
                   left.textureSampleCount == right.textureSampleCount && left.usageBits == right.usageBits && left.initialState == right.initialState && left.virtualResource == right.virtualResource &&
                   left.keepInitialState == right.keepInitialState;
        return left.kind == FrameResourceKind::Buffer && left.bufferStructureStride == right.bufferStructureStride && left.format == right.format && left.usageBits == right.usageBits &&
               left.initialState == right.initialState && left.bufferMemoryType == right.bufferMemoryType && left.virtualResource == right.virtualResource && left.keepInitialState == right.keepInitialState;
    }

    bool DedicatedResourceCanSatisfy(const DedicatedResourceClassKey& availableClass, const u64 availableCapacity, const DedicatedResourceClassKey& requestedClass,
                                     const u64 requestedCapacity) noexcept
    {
        if (!DedicatedResourceSameCompatibilityClass(availableClass, requestedClass))
            return false;
        return requestedClass.kind == FrameResourceKind::Texture || (requestedClass.kind == FrameResourceKind::Buffer && availableCapacity >= requestedCapacity);
    }

    struct DedicatedResourcePool::Impl
    {
        struct CompatibilityBucket
        {
            DedicatedResourceClassKey compatibility;
            u64 key = 0;
            u32 nextCollision = InvalidDedicatedResourceEntry;
            u32 textureRoot = InvalidDedicatedResourceEntry;
            u32 bufferRoot = InvalidDedicatedResourceEntry;
            u32 heldHead = InvalidDedicatedResourceEntry;
            u32 heldBucketPosition = InvalidDedicatedResourceEntry;
            u32 liveEntryCount = 0;
            u64 heldRefreshEpoch = 0;
            u64 heldRefreshFrame = ~u64{0};
        };

        struct Entry
        {
            FrameResourceDesc desc;
            rhi::Texture texture;
            rhi::Buffer buffer;
            CanonicalResourceDescriptors descriptors;
            rhi::MemoryRequirements requirements;
            rhi::ResidencyFenceSet safeAfter;
            rhi::NativeReleaseObservation releaseObservation;
            u64 lastUsedFrame = 0;
            u64 storeSerial = 0;
            u64 leaseSerial = 0;
            u64 capacity = 0;
            u32 compatibilityBucket = InvalidDedicatedResourceEntry;
            u32 previousHeld = InvalidDedicatedResourceEntry;
            u32 nextHeld = InvalidDedicatedResourceEntry;
            // Kind-exclusive intrusive AVL links. Texture trees preserve
            // newest-created selection by storeSerial; buffer trees preserve
            // best-fit capacity with newest-created tie-breaking.
            u32 reusableLeft = InvalidDedicatedResourceEntry;
            u32 reusableRight = InvalidDedicatedResourceEntry;
            u32 reusableParent = InvalidDedicatedResourceEntry;
            u32 reusablePosition = InvalidDedicatedResourceEntry;
            u32 descriptorReusablePosition = InvalidDedicatedResourceEntry;
            u32 pendingPosition = InvalidDedicatedResourceEntry;
            u16 reusableHeight = 1;
            DedicatedResourceState state = DedicatedResourceState::NativeReleased;
            bool reusableAfterRetirement = true;
            bool planReserved = false;
            bool reusableIndexed = false;
            bool heldIndexed = false;
        };

        Impl(const RenderFlowResourceAllocatorConfig& allocatorConfig, AllocatorNativeByteLedger& nativeByteLedger) noexcept
            : entries(memory::pools::Rendering::GetInstance()), freeEntries(memory::pools::Rendering::GetInstance()),
              compatibilityBuckets(memory::pools::Rendering::GetInstance()), freeCompatibilityBuckets(memory::pools::Rendering::GetInstance()), compatibleHeads(memory::pools::Rendering::GetInstance()),
              textureReusableTrim(memory::pools::Rendering::GetInstance()), bufferReusableTrim(memory::pools::Rendering::GetInstance()), descriptorReusableTrim(memory::pools::Rendering::GetInstance()),
              heldBuckets(memory::pools::Rendering::GetInstance()), pendingRetirement(memory::pools::Rendering::GetInstance()),
              pendingQuarantine(memory::pools::Rendering::GetInstance()), pendingDestruction(memory::pools::Rendering::GetInstance()),
              config(allocatorConfig), localDescriptorLedger(0, 0), ledger(&nativeByteLedger), descriptorLedger(&localDescriptorLedger)
        {
        }

        Impl(const RenderFlowResourceAllocatorConfig& allocatorConfig, AllocatorNativeByteLedger& nativeByteLedger,
             AllocatorDescriptorLedger& canonicalDescriptorLedger, const rhi::DescriptorDomainRef canonicalDescriptorDomain) noexcept
            : entries(memory::pools::Rendering::GetInstance()), freeEntries(memory::pools::Rendering::GetInstance()),
              compatibilityBuckets(memory::pools::Rendering::GetInstance()), freeCompatibilityBuckets(memory::pools::Rendering::GetInstance()), compatibleHeads(memory::pools::Rendering::GetInstance()),
              textureReusableTrim(memory::pools::Rendering::GetInstance()), bufferReusableTrim(memory::pools::Rendering::GetInstance()), descriptorReusableTrim(memory::pools::Rendering::GetInstance()),
              heldBuckets(memory::pools::Rendering::GetInstance()), pendingRetirement(memory::pools::Rendering::GetInstance()),
              pendingQuarantine(memory::pools::Rendering::GetInstance()), pendingDestruction(memory::pools::Rendering::GetInstance()),
              config(allocatorConfig), localDescriptorLedger(0, 0), ledger(&nativeByteLedger), descriptorLedger(&canonicalDescriptorLedger), descriptorDomain(canonicalDescriptorDomain)
        {
        }

        containers::DynamicArray<Entry> entries;
        containers::DynamicArray<u32> freeEntries;
        containers::DynamicArray<CompatibilityBucket> compatibilityBuckets;
        containers::DynamicArray<u32> freeCompatibilityBuckets;
        containers::HashMap<u64, u32> compatibleHeads;
        containers::DynamicArray<u32> textureReusableTrim;
        containers::DynamicArray<u32> bufferReusableTrim;
        containers::DynamicArray<u32> descriptorReusableTrim;
        containers::DynamicArray<u32> heldBuckets;
        containers::DynamicArray<u32> pendingRetirement;
        containers::DynamicArray<u32> pendingQuarantine;
        containers::DynamicArray<u32> pendingDestruction;
        mutable concurrency::SpinLock lock;
        RenderFlowResourceAllocatorConfig config;
        AllocatorDescriptorLedger localDescriptorLedger;
        AllocatorNativeByteLedger* ledger = nullptr;
        AllocatorDescriptorLedger* descriptorLedger = nullptr;
        rhi::DescriptorDomainRef descriptorDomain;
        DedicatedResourcePoolStats stats;
        DedicatedResourceProviderFailureInjection failureInjection;
        u64 nextStoreSerial = 1;
        u64 nextLeaseSerial = 1;
        u64 heldRefreshEpoch = 1;

        [[nodiscard]] bool RetireCanonicalDescriptors(Entry& entry, const rhi::DescriptorRetirement& retirement,
                                                      RenderFlowResourceFailure* const failure,
                                                      const RenderFlowResourceSessionState phase = RenderFlowResourceSessionState::Resolving,
                                                      bool* const deviceLostEvidence = nullptr) noexcept
        {
            if (!descriptorDomain.IsValid())
            {
                VG_ASSERT(CanonicalDescriptorReferenceCount(entry.descriptors) == 0);
                return true;
            }
            for (rhi::DescriptorHandle* const descriptor : {&entry.descriptors.shaderResource, &entry.descriptors.unorderedAccess})
            {
                if (!descriptor->IsValid())
                    continue;
                if (InjectProviderFailure(failureInjection, DedicatedResourceProviderFailurePoint::CanonicalDescriptorRetirement, failure,
                                          "injected canonical descriptor retirement-admission failure", phase))
                    return false;
                rhi::Failure rhiFailure;
                bool retired = false;
                {
                    VG_PROFILE_ZONE_NAMED("RenderFlow/CanonicalDescriptor/Retire");
                    retired = rhi::RetireDescriptor(descriptorDomain, *descriptor, retirement, &rhiFailure);
                }
                if (!retired)
                {
                    if (deviceLostEvidence != nullptr && rhiFailure.code == rhi::FailureCode::DeviceLost)
                        *deviceLostEvidence = true;
                    return PoolFail(failure, MapRhiFailure(rhiFailure, RhiFailureContext::BackendContract),
                                    rhiFailure.message[0] != '\0' ? rhiFailure.message : "canonical descriptor retirement failed", phase);
                }
                *descriptor = {};
                if (entry.descriptorReusablePosition != InvalidDedicatedResourceEntry || entry.state == DedicatedResourceState::PlanReady)
                    descriptorLedger->MarkNotReusable(1);
                descriptorLedger->Release(1);
            }
            if (CanonicalDescriptorReferenceCount(entry.descriptors) == 0)
            {
                if (entry.descriptorReusablePosition != InvalidDedicatedResourceEntry)
                    RemoveDescriptorReusable(entry, false);
                entry.descriptors.lastDemandFrame = 0;
            }
            return true;
        }

        [[nodiscard]] u32 ColdestReusableDescriptorEntry() const noexcept
        {
            u32 best = descriptorReusableTrim.Empty() ? InvalidDedicatedResourceEntry : descriptorReusableTrim[0];
            for (u32 index = 0; index < entries.Size(); ++index)
            {
                const Entry& entry = entries[index];
                if (entry.state != DedicatedResourceState::PlanReady || CanonicalDescriptorReferenceCount(entry.descriptors) == 0)
                    continue;
                if (best == InvalidDedicatedResourceEntry || DescriptorReusableLess(index, best))
                    best = index;
            }
            return best;
        }

        [[nodiscard]] bool EvictOneColdDescriptorBundle(RenderFlowResourceFailure* const failure,
                                                        const RenderFlowResourceSessionState phase = RenderFlowResourceSessionState::Resolving,
                                                        const bool pressure = true) noexcept
        {
            for (;;)
            {
                const u32 candidate = ColdestReusableDescriptorEntry();
                if (candidate == InvalidDedicatedResourceEntry)
                    return false;
                Entry& entry = entries[candidate];
                if ((entry.state != DedicatedResourceState::Reusable && entry.state != DedicatedResourceState::PlanReady) ||
                    CanonicalDescriptorReferenceCount(entry.descriptors) == 0)
                    VG_FATAL("dedicated descriptor cold index contains an ineligible entry");
                if (!PoolOwnsOnlyReference(entry))
                {
                    if (entry.state == DedicatedResourceState::PlanReady)
                    {
                        descriptorLedger->MarkNotReusable(CanonicalDescriptorReferenceCount(entry.descriptors));
                        entry.planReserved = false;
                        entry.state = DedicatedResourceState::Reusable;
                        AddHeld(candidate);
                    }
                    else
                    {
                        RemoveReusable(candidate);
                        AddHeld(candidate);
                    }
                    continue;
                }
                if (!RetireCanonicalDescriptors(entry, {}, failure, phase))
                    return false;
                if (pressure)
                    descriptorLedger->RecordPressureEviction();
                else
                    descriptorLedger->RecordMaintenanceEviction();
                return true;
            }
        }

        [[nodiscard]] bool MakeDescriptorLedgerHeadroom(const u32 required, RenderFlowResourceFailure* const failure) noexcept
        {
            if (descriptorLedger->CanReserve(required))
                return true;
            // Held entries are excluded while an external native owner exists.
            // Descriptor pressure is the point where we must discover owners
            // released since the last exact-class/native-pressure refresh, or a
            // legally evictable cold canonical bundle can remain invisible and
            // turn the local hard cap into a false CapacityExceeded result.
            RefreshAllHeld();
            while (!descriptorLedger->CanReserve(required) || descriptorLedger->GetStats().reusableSlots > descriptorLedger->GetTarget())
            {
                const u32 before = descriptorLedger->GetStats().liveSlots;
                if (!EvictOneColdDescriptorBundle(failure, RenderFlowResourceSessionState::Resolving, true) || descriptorLedger->GetStats().liveSlots >= before)
                    return false;
            }
            return true;
        }

        [[nodiscard]] bool RelieveSharedDescriptorPressure(const u32 required, RenderFlowResourceFailure* const failure) noexcept
        {
            if (!descriptorDomain.IsValid())
                return false;
            if (rhi::GetDescriptorDomainStats(descriptorDomain).free < required)
                RefreshAllHeld();
            while (rhi::GetDescriptorDomainStats(descriptorDomain).free < required)
            {
                const u32 before = descriptorLedger->GetStats().liveSlots;
                if (!EvictOneColdDescriptorBundle(failure, RenderFlowResourceSessionState::Resolving, true) || descriptorLedger->GetStats().liveSlots >= before)
                    return false;
            }
            return true;
        }

        [[nodiscard]] bool EnsureCanonicalDescriptors(Entry& entry, const u64 frameSerial, RenderFlowResourceFailure* const failure,
                                                      CanonicalDescriptorPressure* const descriptorPressure = nullptr,
                                                      const bool recordPublicationStats = true) noexcept
        {
            if (descriptorPressure != nullptr)
                *descriptorPressure = {};
            if (!descriptorDomain.IsValid())
                return true;
            const bool buffer = entry.buffer.IsValid();
            if (buffer == entry.texture.IsValid())
                return PoolFail(failure, RenderFlowResourceFailureCode::BackendContractViolation, "dedicated canonical descriptor source kind is invalid");
            const bool needShaderResource = buffer ? HasAny(entry.desc.buffer.active.usage, rhi::BufferUsage::ShaderResource)
                                                   : HasAny(entry.desc.texture.active.usage, rhi::TextureUsage::ShaderResource);
            const bool needUnorderedAccess = buffer ? HasAny(entry.desc.buffer.active.usage, rhi::BufferUsage::UnorderedAccess)
                                                    : HasAny(entry.desc.texture.active.usage, rhi::TextureUsage::UnorderedAccess);
            const u32 missing = static_cast<u32>(needShaderResource && !entry.descriptors.shaderResource.IsValid()) +
                                static_cast<u32>(needUnorderedAccess && !entry.descriptors.unorderedAccess.IsValid());
            const u32 needed = static_cast<u32>(needShaderResource) + static_cast<u32>(needUnorderedAccess);
            if (missing == 0)
            {
                if (needed != 0)
                {
                    VG_PROFILE_ZONE_NAMED("RenderFlow/CanonicalDescriptor/Hit");
                    if (recordPublicationStats)
                        descriptorLedger->RecordPublicationHit();
                }
                entry.descriptors.lastDemandFrame = frameSerial;
                return true;
            }
            {
                VG_PROFILE_ZONE_NAMED("RenderFlow/CanonicalDescriptor/Miss");
                if (recordPublicationStats)
                    descriptorLedger->RecordPublicationMiss();
            }

            const u32 maximumAttempts = descriptorPressure != nullptr ? 1u : 2u;
            for (u32 attempt = 0; attempt < maximumAttempts; ++attempt)
            {
                if (!descriptorLedger->TryReserve(missing))
                {
                    if (descriptorPressure != nullptr)
                    {
                        *descriptorPressure = {CanonicalDescriptorPressureKind::LocalLimit, missing};
                        return false;
                    }
                    if (!MakeDescriptorLedgerHeadroom(missing, failure) || !descriptorLedger->TryReserve(missing))
                        return PoolFail(failure, RenderFlowResourceFailureCode::CapacityExceeded, "canonical descriptor cache hard limit is exhausted");
                }

                rhi::DescriptorHandle createdShaderResource;
                rhi::DescriptorHandle createdUnorderedAccess;
                rhi::Failure rhiFailure;
                bool allocationCapacityFailure = false;
                bool published = true;
                const auto create = [&](const bool writable, rhi::DescriptorHandle& created) noexcept
                {
                    {
                        VG_PROFILE_ZONE_NAMED("RenderFlow/CanonicalDescriptor/Allocate");
                        created = rhi::AllocateDescriptor(descriptorDomain, &rhiFailure);
                    }
                    if (!created.IsValid())
                    {
                        allocationCapacityFailure = rhiFailure.code == rhi::FailureCode::CapacityExceeded;
                        return false;
                    }
                    bool written = false;
                    {
                        VG_PROFILE_ZONE_NAMED("RenderFlow/CanonicalDescriptor/Write");
                        if (buffer)
                        {
                            const rhi::BufferUsage usage = entry.desc.buffer.active.usage;
                            const rhi::BindingType type = HasAny(usage, rhi::BufferUsage::Structured)
                                                              ? (writable ? rhi::BindingType::StructuredBufferUnorderedAccess : rhi::BindingType::StructuredBufferShaderResource)
                                                          : HasAny(usage, rhi::BufferUsage::Raw)
                                                              ? (writable ? rhi::BindingType::ByteAddressBufferUnorderedAccess : rhi::BindingType::ByteAddressBufferShaderResource)
                                                              : (writable ? rhi::BindingType::TypedBufferUnorderedAccess : rhi::BindingType::TypedBufferShaderResource);
                            written = rhi::WriteDescriptor(descriptorDomain, created, entry.buffer.GetRef(), type, {}, &rhiFailure);
                        }
                        else
                        {
                            written = rhi::WriteDescriptor(descriptorDomain, created, entry.texture.GetRef(),
                                                           writable ? rhi::BindingType::TextureUnorderedAccess : rhi::BindingType::TextureShaderResource, {}, &rhiFailure);
                        }
                    }
                    return written;
                };

                if (needShaderResource && !entry.descriptors.shaderResource.IsValid())
                    published = create(false, createdShaderResource);
                if (published && needUnorderedAccess && !entry.descriptors.unorderedAccess.IsValid())
                    published = create(true, createdUnorderedAccess);
                if (published)
                {
                    if (createdShaderResource.IsValid())
                        entry.descriptors.shaderResource = createdShaderResource;
                    if (createdUnorderedAccess.IsValid())
                        entry.descriptors.unorderedAccess = createdUnorderedAccess;
                    entry.descriptors.lastDemandFrame = frameSerial;
                    return true;
                }

                if (createdShaderResource.IsValid())
                {
                    VG_PROFILE_ZONE_NAMED("RenderFlow/CanonicalDescriptor/Retire");
                    static_cast<void>(rhi::RetireDescriptor(descriptorDomain, createdShaderResource, {}));
                }
                if (createdUnorderedAccess.IsValid())
                {
                    VG_PROFILE_ZONE_NAMED("RenderFlow/CanonicalDescriptor/Retire");
                    static_cast<void>(rhi::RetireDescriptor(descriptorDomain, createdUnorderedAccess, {}));
                }
                descriptorLedger->Release(missing);
                if (allocationCapacityFailure && descriptorPressure != nullptr)
                {
                    *descriptorPressure = {CanonicalDescriptorPressureKind::SharedDomain, missing};
                    return false;
                }
                if (!allocationCapacityFailure || attempt != 0 || !RelieveSharedDescriptorPressure(missing, failure))
                    return PoolFail(failure, allocationCapacityFailure ? RenderFlowResourceFailureCode::CapacityExceeded : MapRhiFailure(rhiFailure, RhiFailureContext::BackendContract),
                                    rhiFailure.message[0] != '\0' ? rhiFailure.message : "canonical descriptor publication failed");
            }
            return false;
        }

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

        [[nodiscard]] u32 FindCompatibilityBucket(const DedicatedResourceClassKey& compatibility, const bool create) noexcept
        {
            const u64 key = compatibility.hash;
            u32 bucket = InvalidDedicatedResourceEntry;
            static_cast<void>(compatibleHeads.Find(key, bucket));
            for (u32 current = bucket; current != InvalidDedicatedResourceEntry; current = compatibilityBuckets[current].nextCollision)
                if (DedicatedResourceSameCompatibilityClass(compatibilityBuckets[current].compatibility, compatibility))
                    return current;
            if (!create)
                return InvalidDedicatedResourceEntry;
            CompatibilityBucket created;
            created.compatibility = compatibility;
            created.key = key;
            created.nextCollision = bucket;
            u32 index = InvalidDedicatedResourceEntry;
            if (freeCompatibilityBuckets.Empty())
            {
                index = compatibilityBuckets.Size();
                compatibilityBuckets.PushBack(static_cast<CompatibilityBucket&&>(created));
            }
            else
            {
                index = freeCompatibilityBuckets.Back();
                freeCompatibilityBuckets.PopBack();
                compatibilityBuckets[index] = static_cast<CompatibilityBucket&&>(created);
            }
            static_cast<void>(compatibleHeads.Set(key, index));
            return index;
        }

        void ReleaseCompatibilityBucketReference(const u32 bucketIndex) noexcept
        {
            VG_ASSERT(bucketIndex < compatibilityBuckets.Size());
            CompatibilityBucket& bucket = compatibilityBuckets[bucketIndex];
            VG_ASSERT(bucket.liveEntryCount != 0);
            if (--bucket.liveEntryCount != 0)
                return;
            VG_ASSERT(bucket.textureRoot == InvalidDedicatedResourceEntry && bucket.bufferRoot == InvalidDedicatedResourceEntry && bucket.heldHead == InvalidDedicatedResourceEntry &&
                      bucket.heldBucketPosition == InvalidDedicatedResourceEntry);

            u32 head = InvalidDedicatedResourceEntry;
            const bool found = compatibleHeads.Find(bucket.key, head);
            VG_ASSERT(found && head != InvalidDedicatedResourceEntry);
            if (head == bucketIndex)
            {
                const u32 next = bucket.nextCollision;
                const bool erased = compatibleHeads.Remove(bucket.key).IsSuccessful();
                VG_ASSERT(erased);
                if (next != InvalidDedicatedResourceEntry)
                    static_cast<void>(compatibleHeads.Set(bucket.key, next));
            }
            else
            {
                u32 previous = head;
                while (previous != InvalidDedicatedResourceEntry && compatibilityBuckets[previous].nextCollision != bucketIndex)
                    previous = compatibilityBuckets[previous].nextCollision;
                VG_ASSERT(previous != InvalidDedicatedResourceEntry);
                compatibilityBuckets[previous].nextCollision = bucket.nextCollision;
            }
            bucket = {};
            freeCompatibilityBuckets.PushBack(bucketIndex);
        }

        [[nodiscard]] u16 ReusableHeight(const u32 index) const noexcept
        {
            return index == InvalidDedicatedResourceEntry ? 0u : entries[index].reusableHeight;
        }

        void UpdateReusableHeight(const u32 index) noexcept
        {
            if (index == InvalidDedicatedResourceEntry)
                return;
            Entry& entry = entries[index];
            const u16 left = ReusableHeight(entry.reusableLeft);
            const u16 right = ReusableHeight(entry.reusableRight);
            entry.reusableHeight = static_cast<u16>((left > right ? left : right) + 1u);
        }

        [[nodiscard]] bool ReusableTreeLess(const u32 left, const u32 right, const FrameResourceKind kind) const noexcept
        {
            const Entry& leftEntry = entries[left];
            const Entry& rightEntry = entries[right];
            if (kind == FrameResourceKind::Texture)
                return leftEntry.storeSerial != rightEntry.storeSerial ? leftEntry.storeSerial < rightEntry.storeSerial : left < right;
            const u64 leftCapacity = leftEntry.capacity;
            const u64 rightCapacity = rightEntry.capacity;
            return leftCapacity != rightCapacity ? leftCapacity < rightCapacity
                                                 : leftEntry.storeSerial != rightEntry.storeSerial ? leftEntry.storeSerial > rightEntry.storeSerial : left < right;
        }

        [[nodiscard]] u32& ReusableTreeRoot(const u32 bucketIndex, const FrameResourceKind kind) noexcept
        {
            CompatibilityBucket& bucket = compatibilityBuckets[bucketIndex];
            return kind == FrameResourceKind::Texture ? bucket.textureRoot : bucket.bufferRoot;
        }

        [[nodiscard]] u32 ReusableTreeRoot(const u32 bucketIndex, const FrameResourceKind kind) const noexcept
        {
            const CompatibilityBucket& bucket = compatibilityBuckets[bucketIndex];
            return kind == FrameResourceKind::Texture ? bucket.textureRoot : bucket.bufferRoot;
        }

        void ReplaceReusableParentLink(const u32 bucketIndex, const FrameResourceKind kind, const u32 parent, const u32 oldChild, const u32 newChild) noexcept
        {
            if (parent == InvalidDedicatedResourceEntry)
                ReusableTreeRoot(bucketIndex, kind) = newChild;
            else if (entries[parent].reusableLeft == oldChild)
                entries[parent].reusableLeft = newChild;
            else
                entries[parent].reusableRight = newChild;
            if (newChild != InvalidDedicatedResourceEntry)
                entries[newChild].reusableParent = parent;
        }

        u32 RotateReusableLeft(const u32 bucketIndex, const FrameResourceKind kind, const u32 root) noexcept
        {
            Entry& rootEntry = entries[root];
            const u32 pivot = rootEntry.reusableRight;
            VG_ASSERT(pivot != InvalidDedicatedResourceEntry);
            Entry& pivotEntry = entries[pivot];
            const u32 parent = rootEntry.reusableParent;
            const u32 middle = pivotEntry.reusableLeft;
            ReplaceReusableParentLink(bucketIndex, kind, parent, root, pivot);
            pivotEntry.reusableLeft = root;
            rootEntry.reusableParent = pivot;
            rootEntry.reusableRight = middle;
            if (middle != InvalidDedicatedResourceEntry)
                entries[middle].reusableParent = root;
            UpdateReusableHeight(root);
            UpdateReusableHeight(pivot);
            return pivot;
        }

        u32 RotateReusableRight(const u32 bucketIndex, const FrameResourceKind kind, const u32 root) noexcept
        {
            Entry& rootEntry = entries[root];
            const u32 pivot = rootEntry.reusableLeft;
            VG_ASSERT(pivot != InvalidDedicatedResourceEntry);
            Entry& pivotEntry = entries[pivot];
            const u32 parent = rootEntry.reusableParent;
            const u32 middle = pivotEntry.reusableRight;
            ReplaceReusableParentLink(bucketIndex, kind, parent, root, pivot);
            pivotEntry.reusableRight = root;
            rootEntry.reusableParent = pivot;
            rootEntry.reusableLeft = middle;
            if (middle != InvalidDedicatedResourceEntry)
                entries[middle].reusableParent = root;
            UpdateReusableHeight(root);
            UpdateReusableHeight(pivot);
            return pivot;
        }

        void RebalanceReusableFrom(const u32 bucketIndex, const FrameResourceKind kind, u32 index) noexcept
        {
            while (index != InvalidDedicatedResourceEntry)
            {
                UpdateReusableHeight(index);
                const i32 balance = static_cast<i32>(ReusableHeight(entries[index].reusableLeft)) - static_cast<i32>(ReusableHeight(entries[index].reusableRight));
                u32 subtreeRoot = index;
                if (balance > 1)
                {
                    const u32 left = entries[index].reusableLeft;
                    if (ReusableHeight(entries[left].reusableLeft) < ReusableHeight(entries[left].reusableRight))
                        static_cast<void>(RotateReusableLeft(bucketIndex, kind, left));
                    subtreeRoot = RotateReusableRight(bucketIndex, kind, index);
                }
                else if (balance < -1)
                {
                    const u32 right = entries[index].reusableRight;
                    if (ReusableHeight(entries[right].reusableRight) < ReusableHeight(entries[right].reusableLeft))
                        static_cast<void>(RotateReusableRight(bucketIndex, kind, right));
                    subtreeRoot = RotateReusableLeft(bucketIndex, kind, index);
                }
                index = entries[subtreeRoot].reusableParent;
            }
        }

        void InsertReusableTree(const u32 index) noexcept
        {
            Entry& entry = entries[index];
            const FrameResourceKind kind = entry.desc.kind;
            entry.reusableLeft = entry.reusableRight = entry.reusableParent = InvalidDedicatedResourceEntry;
            entry.reusableHeight = 1;
            u32& root = ReusableTreeRoot(entry.compatibilityBucket, kind);
            if (root == InvalidDedicatedResourceEntry)
            {
                root = index;
                return;
            }
            u32 current = root;
            for (;;)
            {
                if (ReusableTreeLess(index, current, kind))
                {
                    if (entries[current].reusableLeft == InvalidDedicatedResourceEntry)
                    {
                        entries[current].reusableLeft = index;
                        entry.reusableParent = current;
                        RebalanceReusableFrom(entry.compatibilityBucket, kind, current);
                        return;
                    }
                    current = entries[current].reusableLeft;
                }
                else
                {
                    if (entries[current].reusableRight == InvalidDedicatedResourceEntry)
                    {
                        entries[current].reusableRight = index;
                        entry.reusableParent = current;
                        RebalanceReusableFrom(entry.compatibilityBucket, kind, current);
                        return;
                    }
                    current = entries[current].reusableRight;
                }
            }
        }

        void RemoveReusableTree(const u32 index) noexcept
        {
            Entry& entry = entries[index];
            const u32 bucketIndex = entry.compatibilityBucket;
            const FrameResourceKind kind = entry.desc.kind;
            while (entry.reusableLeft != InvalidDedicatedResourceEntry && entry.reusableRight != InvalidDedicatedResourceEntry)
            {
                if (ReusableHeight(entry.reusableLeft) > ReusableHeight(entry.reusableRight))
                    static_cast<void>(RotateReusableRight(bucketIndex, kind, index));
                else
                    static_cast<void>(RotateReusableLeft(bucketIndex, kind, index));
            }
            const u32 parent = entry.reusableParent;
            const u32 child = entry.reusableLeft != InvalidDedicatedResourceEntry ? entry.reusableLeft : entry.reusableRight;
            ReplaceReusableParentLink(bucketIndex, kind, parent, index, child);
            entry.reusableLeft = entry.reusableRight = entry.reusableParent = InvalidDedicatedResourceEntry;
            entry.reusableHeight = 1;
            RebalanceReusableFrom(bucketIndex, kind, parent);
        }

        [[nodiscard]] u32 LowerBoundBufferReusable(const u32 bucketIndex, const u64 capacity) const noexcept
        {
            u32 current = compatibilityBuckets[bucketIndex].bufferRoot;
            u32 best = InvalidDedicatedResourceEntry;
            while (current != InvalidDedicatedResourceEntry)
            {
                const u64 currentCapacity = entries[current].capacity;
                if (currentCapacity >= capacity)
                {
                    best = current;
                    current = entries[current].reusableLeft;
                }
                else
                    current = entries[current].reusableRight;
            }
            return best;
        }

        [[nodiscard]] u32 NewestTextureReusable(const u32 bucketIndex) const noexcept
        {
            u32 current = compatibilityBuckets[bucketIndex].textureRoot;
            if (current == InvalidDedicatedResourceEntry)
                return current;
            while (entries[current].reusableRight != InvalidDedicatedResourceEntry)
                current = entries[current].reusableRight;
            return current;
        }

        [[nodiscard]] containers::DynamicArray<u32>& ReusableTrimHeap(const FrameResourceKind kind) noexcept
        {
            VG_ASSERT(kind == FrameResourceKind::Texture || kind == FrameResourceKind::Buffer);
            return kind == FrameResourceKind::Texture ? textureReusableTrim : bufferReusableTrim;
        }

        [[nodiscard]] const containers::DynamicArray<u32>& ReusableTrimHeap(const FrameResourceKind kind) const noexcept
        {
            VG_ASSERT(kind == FrameResourceKind::Texture || kind == FrameResourceKind::Buffer);
            return kind == FrameResourceKind::Texture ? textureReusableTrim : bufferReusableTrim;
        }

        [[nodiscard]] bool ReusableTrimLess(const u32 left, const u32 right) const noexcept
        {
            const Entry& leftEntry = entries[left];
            const Entry& rightEntry = entries[right];
            return leftEntry.lastUsedFrame != rightEntry.lastUsedFrame ? leftEntry.lastUsedFrame < rightEntry.lastUsedFrame : left < right;
        }

        void SwapReusableTrim(const FrameResourceKind kind, const u32 leftPosition, const u32 rightPosition) noexcept
        {
            containers::DynamicArray<u32>& heap = ReusableTrimHeap(kind);
            const u32 left = heap[leftPosition];
            const u32 right = heap[rightPosition];
            heap[leftPosition] = right;
            heap[rightPosition] = left;
            entries[left].reusablePosition = rightPosition;
            entries[right].reusablePosition = leftPosition;
        }

        void SiftReusableTrimUp(const FrameResourceKind kind, u32 position) noexcept
        {
            containers::DynamicArray<u32>& heap = ReusableTrimHeap(kind);
            while (position != 0)
            {
                const u32 parent = (position - 1u) / 2u;
                if (!ReusableTrimLess(heap[position], heap[parent]))
                    break;
                SwapReusableTrim(kind, position, parent);
                position = parent;
            }
        }

        void SiftReusableTrimDown(const FrameResourceKind kind, u32 position) noexcept
        {
            containers::DynamicArray<u32>& heap = ReusableTrimHeap(kind);
            for (;;)
            {
                const u32 left = position * 2u + 1u;
                if (left >= heap.Size())
                    break;
                const u32 right = left + 1u;
                u32 best = left;
                if (right < heap.Size() && ReusableTrimLess(heap[right], heap[left]))
                    best = right;
                if (!ReusableTrimLess(heap[best], heap[position]))
                    break;
                SwapReusableTrim(kind, position, best);
                position = best;
            }
        }

        void InsertReusableTrim(const u32 index) noexcept
        {
            Entry& entry = entries[index];
            VG_ASSERT(entry.reusablePosition == InvalidDedicatedResourceEntry);
            containers::DynamicArray<u32>& heap = ReusableTrimHeap(entry.desc.kind);
            entry.reusablePosition = heap.Size();
            heap.PushBack(index);
            SiftReusableTrimUp(entry.desc.kind, entry.reusablePosition);
        }

        void RemoveReusableTrim(const u32 index) noexcept
        {
            Entry& entry = entries[index];
            const u32 position = entry.reusablePosition;
            if (position == InvalidDedicatedResourceEntry)
                return;
            const FrameResourceKind kind = entry.desc.kind;
            containers::DynamicArray<u32>& heap = ReusableTrimHeap(kind);
            VG_ASSERT(position < heap.Size() && heap[position] == index);
            const u32 last = heap.Size() - 1u;
            if (position != last)
                SwapReusableTrim(kind, position, last);
            heap.PopBack();
            entry.reusablePosition = InvalidDedicatedResourceEntry;
            if (position < heap.Size())
            {
                const u32 moved = heap[position];
                SiftReusableTrimUp(kind, position);
                SiftReusableTrimDown(kind, entries[moved].reusablePosition);
            }
        }

        [[nodiscard]] bool DescriptorReusableLess(const u32 left, const u32 right) const noexcept
        {
            const Entry& leftEntry = entries[left];
            const Entry& rightEntry = entries[right];
            return leftEntry.descriptors.lastDemandFrame != rightEntry.descriptors.lastDemandFrame
                       ? leftEntry.descriptors.lastDemandFrame < rightEntry.descriptors.lastDemandFrame
                       : left < right;
        }

        void SwapDescriptorReusable(const u32 leftPosition, const u32 rightPosition) noexcept
        {
            const u32 left = descriptorReusableTrim[leftPosition];
            const u32 right = descriptorReusableTrim[rightPosition];
            descriptorReusableTrim[leftPosition] = right;
            descriptorReusableTrim[rightPosition] = left;
            entries[left].descriptorReusablePosition = rightPosition;
            entries[right].descriptorReusablePosition = leftPosition;
        }

        void SiftDescriptorReusableUp(u32 position) noexcept
        {
            while (position != 0)
            {
                const u32 parent = (position - 1u) / 2u;
                if (!DescriptorReusableLess(descriptorReusableTrim[position], descriptorReusableTrim[parent]))
                    break;
                SwapDescriptorReusable(position, parent);
                position = parent;
            }
        }

        void SiftDescriptorReusableDown(u32 position) noexcept
        {
            for (;;)
            {
                const u32 left = position * 2u + 1u;
                if (left >= descriptorReusableTrim.Size())
                    break;
                const u32 right = left + 1u;
                u32 best = left;
                if (right < descriptorReusableTrim.Size() && DescriptorReusableLess(descriptorReusableTrim[right], descriptorReusableTrim[left]))
                    best = right;
                if (!DescriptorReusableLess(descriptorReusableTrim[best], descriptorReusableTrim[position]))
                    break;
                SwapDescriptorReusable(position, best);
                position = best;
            }
        }

        void InsertDescriptorReusable(const u32 index) noexcept
        {
            Entry& entry = entries[index];
            const u32 descriptorCount = CanonicalDescriptorReferenceCount(entry.descriptors);
            if (descriptorCount == 0 || entry.descriptorReusablePosition != InvalidDedicatedResourceEntry)
                return;
            entry.descriptorReusablePosition = descriptorReusableTrim.Size();
            descriptorReusableTrim.PushBack(index);
            descriptorLedger->MarkReusable(descriptorCount);
            SiftDescriptorReusableUp(entry.descriptorReusablePosition);
        }

        void RemoveDescriptorReusable(Entry& entry, const bool updateLedger = true) noexcept
        {
            const u32 position = entry.descriptorReusablePosition;
            if (position == InvalidDedicatedResourceEntry)
                return;
            VG_ASSERT(position < descriptorReusableTrim.Size());
            const u32 index = descriptorReusableTrim[position];
            VG_ASSERT(&entries[index] == &entry);
            if (updateLedger)
                descriptorLedger->MarkNotReusable(CanonicalDescriptorReferenceCount(entry.descriptors));
            const u32 last = descriptorReusableTrim.Size() - 1u;
            if (position != last)
                SwapDescriptorReusable(position, last);
            descriptorReusableTrim.PopBack();
            entry.descriptorReusablePosition = InvalidDedicatedResourceEntry;
            if (position < descriptorReusableTrim.Size())
            {
                const u32 moved = descriptorReusableTrim[position];
                SiftDescriptorReusableUp(position);
                SiftDescriptorReusableDown(entries[moved].descriptorReusablePosition);
            }
        }

        [[nodiscard]] u32 OldestReusable(const FrameResourceKind kind) const noexcept
        {
            const containers::DynamicArray<u32>& heap = ReusableTrimHeap(kind);
            return heap.Empty() ? InvalidDedicatedResourceEntry : heap[0];
        }

        [[nodiscard]] u32 OldestReusableAny() const noexcept
        {
            const u32 texture = OldestReusable(FrameResourceKind::Texture);
            const u32 buffer = OldestReusable(FrameResourceKind::Buffer);
            if (texture == InvalidDedicatedResourceEntry)
                return buffer;
            if (buffer == InvalidDedicatedResourceEntry)
                return texture;
            return ReusableTrimLess(texture, buffer) ? texture : buffer;
        }

        [[nodiscard]] u32 OldestPlanReady(const FrameResourceKind kind) const noexcept
        {
            u32 best = InvalidDedicatedResourceEntry;
            for (u32 index = 0; index < entries.Size(); ++index)
            {
                const Entry& entry = entries[index];
                if (entry.state != DedicatedResourceState::PlanReady || entry.desc.kind != kind)
                    continue;
                if (best == InvalidDedicatedResourceEntry || ReusableTrimLess(index, best))
                    best = index;
            }
            return best;
        }

        [[nodiscard]] u32 OldestIdle(const FrameResourceKind kind) const noexcept
        {
            const u32 reusable = OldestReusable(kind);
            const u32 planned = OldestPlanReady(kind);
            if (reusable == InvalidDedicatedResourceEntry)
                return planned;
            if (planned == InvalidDedicatedResourceEntry)
                return reusable;
            return ReusableTrimLess(reusable, planned) ? reusable : planned;
        }

        [[nodiscard]] u32 OldestIdleAny() const noexcept
        {
            const u32 texture = OldestIdle(FrameResourceKind::Texture);
            const u32 buffer = OldestIdle(FrameResourceKind::Buffer);
            if (texture == InvalidDedicatedResourceEntry)
                return buffer;
            if (buffer == InvalidDedicatedResourceEntry)
                return texture;
            return ReusableTrimLess(texture, buffer) ? texture : buffer;
        }

        void AddReusable(const u32 index) noexcept
        {
            Entry& entry = entries[index];
            if (entry.reusableIndexed)
                return;
            if (!PoolOwnsOnlyReference(entry))
            {
                AddHeld(index);
                return;
            }
            RemoveHeld(index);
            if (entry.compatibilityBucket == InvalidDedicatedResourceEntry)
                entry.compatibilityBucket = FindCompatibilityBucket(BuildDedicatedResourceRequest(entry.desc).compatibility, true);
            InsertReusableTree(index);
            InsertReusableTrim(index);
            entry.reusableIndexed = true;
            InsertDescriptorReusable(index);
        }

        void RemoveReusable(const u32 index) noexcept
        {
            Entry& entry = entries[index];
            if (!entry.reusableIndexed)
                return;
            RemoveDescriptorReusable(entry);
            RemoveReusableTree(index);
            RemoveReusableTrim(index);
            entry.reusableIndexed = false;
        }

        void AddHeld(const u32 index) noexcept
        {
            Entry& entry = entries[index];
            if (entry.heldIndexed)
                return;
            VG_ASSERT(!entry.reusableIndexed && entry.state == DedicatedResourceState::Reusable);
            if (entry.compatibilityBucket == InvalidDedicatedResourceEntry)
                entry.compatibilityBucket = FindCompatibilityBucket(BuildDedicatedResourceRequest(entry.desc).compatibility, true);
            CompatibilityBucket& bucket = compatibilityBuckets[entry.compatibilityBucket];
            if (bucket.heldHead == InvalidDedicatedResourceEntry)
            {
                bucket.heldBucketPosition = heldBuckets.Size();
                heldBuckets.PushBack(entry.compatibilityBucket);
            }
            entry.previousHeld = InvalidDedicatedResourceEntry;
            entry.nextHeld = bucket.heldHead;
            if (bucket.heldHead != InvalidDedicatedResourceEntry)
                entries[bucket.heldHead].previousHeld = index;
            bucket.heldHead = index;
            entry.heldIndexed = true;
        }

        void RemoveHeld(const u32 index) noexcept
        {
            Entry& entry = entries[index];
            if (!entry.heldIndexed)
                return;
            CompatibilityBucket& bucket = compatibilityBuckets[entry.compatibilityBucket];
            if (entry.previousHeld == InvalidDedicatedResourceEntry)
                bucket.heldHead = entry.nextHeld;
            else
                entries[entry.previousHeld].nextHeld = entry.nextHeld;
            if (entry.nextHeld != InvalidDedicatedResourceEntry)
                entries[entry.nextHeld].previousHeld = entry.previousHeld;
            entry.previousHeld = entry.nextHeld = InvalidDedicatedResourceEntry;
            entry.heldIndexed = false;
            if (bucket.heldHead == InvalidDedicatedResourceEntry)
            {
                const u32 position = bucket.heldBucketPosition;
                VG_ASSERT(position < heldBuckets.Size() && heldBuckets[position] == entry.compatibilityBucket);
                const u32 moved = heldBuckets.Back();
                heldBuckets[position] = moved;
                heldBuckets.PopBack();
                if (moved != entry.compatibilityBucket)
                    compatibilityBuckets[moved].heldBucketPosition = position;
                bucket.heldBucketPosition = InvalidDedicatedResourceEntry;
            }
        }

        void RefreshHeld(const u32 bucketIndex, const u64 frameSerial) noexcept
        {
            CompatibilityBucket& bucket = compatibilityBuckets[bucketIndex];
            if (bucket.heldRefreshEpoch == heldRefreshEpoch && bucket.heldRefreshFrame == frameSerial)
                return;
            bucket.heldRefreshEpoch = heldRefreshEpoch;
            bucket.heldRefreshFrame = frameSerial;
            for (u32 index = bucket.heldHead; index != InvalidDedicatedResourceEntry;)
            {
                const u32 next = entries[index].nextHeld;
                Entry& entry = entries[index];
                if (entry.state == DedicatedResourceState::Reusable && PoolOwnsOnlyReference(entry))
                {
                    RemoveHeld(index);
                    AddReusable(index);
                }
                index = next;
            }
        }

        void RefreshAllHeld() noexcept
        {
            for (u32 position = 0; position < heldBuckets.Size();)
            {
                const u32 bucketIndex = heldBuckets[position];
                CompatibilityBucket& bucket = compatibilityBuckets[bucketIndex];
                for (u32 index = bucket.heldHead; index != InvalidDedicatedResourceEntry;)
                {
                    const u32 next = entries[index].nextHeld;
                    Entry& entry = entries[index];
                    if (entry.state == DedicatedResourceState::Reusable && PoolOwnsOnlyReference(entry))
                    {
                        RemoveHeld(index);
                        AddReusable(index);
                    }
                    index = next;
                }
                if (position < heldBuckets.Size() && heldBuckets[position] == bucketIndex)
                    ++position;
            }
        }

        void AddPending(containers::DynamicArray<u32>& list, const u32 index) noexcept
        {
            Entry& entry = entries[index];
            VG_ASSERT(entry.pendingPosition == InvalidDedicatedResourceEntry);
            entry.pendingPosition = list.Size();
            list.PushBack(index);
        }

        void RemovePending(containers::DynamicArray<u32>& list, const u32 index) noexcept
        {
            Entry& entry = entries[index];
            const u32 position = entry.pendingPosition;
            VG_ASSERT(position < list.Size() && list[position] == index);
            const u32 moved = list.Back();
            list[position] = moved;
            list.PopBack();
            if (moved != index)
                entries[moved].pendingPosition = position;
            entry.pendingPosition = InvalidDedicatedResourceEntry;
        }

        void StoreEntry(Entry&& entry, const DedicatedResourceClassKey& compatibility, u32& index) noexcept
        {
            entry.compatibilityBucket = FindCompatibilityBucket(compatibility, true);
            VG_ASSERT(entry.compatibilityBucket != InvalidDedicatedResourceEntry);
            ++compatibilityBuckets[entry.compatibilityBucket].liveEntryCount;
            if (nextStoreSerial == 0 || nextStoreSerial == ~u64{0})
                VG_FATAL("dedicated resource store serial exhausted");
            entry.storeSerial = nextStoreSerial++;
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
        }

        void ReleaseEntrySlot(const u32 index) noexcept
        {
            Entry& entry = entries[index];
            RemoveReusable(index);
            RemoveHeld(index);
            VG_ASSERT(entry.pendingPosition == InvalidDedicatedResourceEntry);
            const u32 compatibilityBucket = entry.compatibilityBucket;
            entry = {};
            freeEntries.PushBack(index);
            if (compatibilityBucket != InvalidDedicatedResourceEntry)
                ReleaseCompatibilityBucketReference(compatibilityBucket);
        }

        void QuarantineCreatedEntry(Entry&& entry, const DedicatedResourceClassKey& compatibility) noexcept
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
            u32 stored = InvalidDedicatedResourceEntry;
            StoreEntry(static_cast<Entry&&>(entry), compatibility, stored);
            if (entries[stored].state == DedicatedResourceState::EvictedPendingNativeDestruction)
                AddPending(pendingDestruction, stored);
            else
                AddPending(pendingQuarantine, stored);
            ++stats.pendingNativeDestruction;
        }
    };

    DedicatedResourcePool::DedicatedResourcePool(const RenderFlowResourceAllocatorConfig& config, AllocatorNativeByteLedger& nativeLedger) noexcept
    {
        memory::MemoryBlock block = memory::Allocate(memory::PoolId::Rendering, sizeof(Impl), alignof(Impl));
        if (block)
            m_impl = new (block.address) Impl(config, nativeLedger);
    }

    bool DedicatedResourcePoolInvariantTestAccess::SetState(DedicatedResourcePool& pool, const u32 entry, const DedicatedResourceState state) noexcept
    {
        if (pool.m_impl == nullptr)
            return false;
        concurrency::ScopedLock<concurrency::SpinLock> guard(pool.m_impl->lock);
        if (entry >= pool.m_impl->entries.Size())
            return false;
        pool.m_impl->entries[entry].state = state;
        return true;
    }

    DedicatedResourcePool::DedicatedResourcePool(const RenderFlowResourceAllocatorConfig& config, AllocatorNativeByteLedger& nativeLedger,
                                                 AllocatorDescriptorLedger& descriptorLedger, const rhi::DescriptorDomainRef descriptorDomain) noexcept
    {
        memory::MemoryBlock block = memory::Allocate(memory::PoolId::Rendering, sizeof(Impl), alignof(Impl));
        if (block)
            m_impl = new (block.address) Impl(config, nativeLedger, descriptorLedger, descriptorDomain);
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
        rhi::ResidencyFenceSet testedFences;
        bool hasTestedFences = false;
        bool testedFencesComplete = false;
        u32 retainedRetirementCount = 0;
        const u32 pendingRetirementCount = m_impl->pendingRetirement.Size();
        for (u32 position = 0; position < pendingRetirementCount; ++position)
        {
            const u32 index = m_impl->pendingRetirement[position];
            Impl::Entry& entry = m_impl->entries[index];
            VG_ASSERT(entry.state == DedicatedResourceState::PendingRetirement);
            if (!hasTestedFences || !FenceSetsEqual(testedFences, entry.safeAfter))
            {
                testedFences = entry.safeAfter;
                testedFencesComplete = FencesComplete(entry.safeAfter);
                hasTestedFences = true;
            }
            if (!testedFencesComplete)
            {
                m_impl->pendingRetirement[retainedRetirementCount] = index;
                entry.pendingPosition = retainedRetirementCount;
                ++retainedRetirementCount;
                continue;
            }
            entry.pendingPosition = InvalidDedicatedResourceEntry;
            entry.safeAfter = {};
            if (entry.reusableAfterRetirement)
            {
                if (entry.planReserved && PoolOwnsOnlyReference(entry))
                {
                    entry.state = DedicatedResourceState::PlanReady;
                    m_impl->descriptorLedger->MarkReusable(CanonicalDescriptorReferenceCount(entry.descriptors));
                }
                else
                {
                    entry.planReserved = false;
                    entry.state = DedicatedResourceState::Reusable;
                    m_impl->AddReusable(index);
                }
            }
            else
            {
                entry.planReserved = false;
                entry.state = DedicatedResourceState::ProviderFailureQuarantine;
                m_impl->AddPending(m_impl->pendingQuarantine, index);
            }
        }
        m_impl->pendingRetirement.Resize(retainedRetirementCount);
        for (u32 position = 0; position < m_impl->pendingQuarantine.Size();)
        {
            const u32 index = m_impl->pendingQuarantine[position];
            Impl::Entry& entry = m_impl->entries[index];
            VG_ASSERT(entry.state == DedicatedResourceState::ProviderFailureQuarantine);
            if (CanonicalDescriptorReferenceCount(entry.descriptors) != 0 && !m_impl->RetireCanonicalDescriptors(entry, {}, nullptr))
            {
                ++position;
                continue;
            }
            rhi::Failure ignored;
            entry.releaseObservation = rhi::ObserveNativeRelease(EntryResource(entry), &ignored);
            if (!entry.releaseObservation.IsValid())
            {
                ++position;
                continue;
            }
            m_impl->RemovePending(m_impl->pendingQuarantine, index);
            entry.texture.Reset();
            entry.buffer.Reset();
            entry.state = DedicatedResourceState::EvictedPendingNativeDestruction;
            m_impl->AddPending(m_impl->pendingDestruction, index);
        }
        for (u32 position = 0; position < m_impl->pendingDestruction.Size();)
        {
            const u32 index = m_impl->pendingDestruction[position];
            Impl::Entry& entry = m_impl->entries[index];
            VG_ASSERT(entry.state == DedicatedResourceState::EvictedPendingNativeDestruction);
            if (!rhi::IsNativeReleaseComplete(entry.releaseObservation))
            {
                ++position;
                continue;
            }
            m_impl->RemovePending(m_impl->pendingDestruction, index);
            rhi::ReleaseNativeReleaseObservation(entry.releaseObservation);
            m_impl->RemoveCharge(entry);
            m_impl->ReleaseEntrySlot(index);
        }
        m_impl->stats.pendingRetirement = m_impl->pendingRetirement.Size();
        m_impl->stats.pendingNativeDestruction = m_impl->pendingQuarantine.Size() + m_impl->pendingDestruction.Size();
    }

    void DedicatedResourcePool::Poll() noexcept
    {
        if (m_impl == nullptr)
            return;
        concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
        if (++m_impl->heldRefreshEpoch == 0)
            m_impl->heldRefreshEpoch = 1;
        PollUnlocked();
    }

    bool DedicatedResourcePool::Acquire(const FrameResourceDesc& requested, const u64 frameSerial, DedicatedResourceAssignment& assignment,
                                        RenderFlowResourceFailure* const failure) noexcept
    {
        return Acquire(BuildDedicatedResourceRequest(requested), frameSerial, assignment, failure);
    }

    bool DedicatedResourcePool::Acquire(const DedicatedResourceRequest& request, const u64 frameSerial, DedicatedResourceAssignment& assignment,
                                        RenderFlowResourceFailure* const failure, CanonicalDescriptorPressure* const descriptorPressure,
                                        const bool recordPublicationStats) noexcept
    {
        assignment = {};
        if (descriptorPressure != nullptr)
            *descriptorPressure = {};
        if (m_impl == nullptr)
            return PoolFail(failure, RenderFlowResourceFailureCode::CapacityExceeded, "dedicated-resource pool metadata allocation failed");
        concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
        const FrameResourceDesc& desc = request.physicalDesc;
        const u32 bucketIndex = m_impl->FindCompatibilityBucket(request.compatibility, false);
        const auto findReusable = [&]() noexcept -> u32
        {
            if (bucketIndex == InvalidDedicatedResourceEntry)
                return InvalidDedicatedResourceEntry;
            const DedicatedResourceClassKey& bucketCompatibility = m_impl->compatibilityBuckets[bucketIndex].compatibility;
            if (desc.kind == FrameResourceKind::Texture)
            {
                for (;;)
                {
                    const u32 index = m_impl->NewestTextureReusable(bucketIndex);
                    if (index == InvalidDedicatedResourceEntry)
                        break;
                    Impl::Entry& entry = m_impl->entries[index];
                    if (!PoolOwnsOnlyReference(entry))
                    {
                        m_impl->RemoveReusable(index);
                        m_impl->AddHeld(index);
                        continue;
                    }
                    if (entry.state == DedicatedResourceState::Reusable &&
                        DedicatedResourceCanSatisfy(bucketCompatibility, entry.capacity, request.compatibility, request.capacity))
                        return index;
                    // Every texture in one compatibility bucket has the same
                    // exact active descriptor. A mismatch means the bucket
                    // metadata is inconsistent; do not silently choose an older
                    // entry and mask it.
                    break;
                }
            }
            else if (desc.kind == FrameResourceKind::Buffer)
            {
                for (;;)
                {
                    const u32 index = m_impl->LowerBoundBufferReusable(bucketIndex, request.capacity);
                    if (index == InvalidDedicatedResourceEntry)
                        break;
                    Impl::Entry& entry = m_impl->entries[index];
                    if (!PoolOwnsOnlyReference(entry))
                    {
                        m_impl->RemoveReusable(index);
                        m_impl->AddHeld(index);
                        continue;
                    }
                    if (entry.state == DedicatedResourceState::Reusable &&
                        DedicatedResourceCanSatisfy(bucketCompatibility, entry.capacity, request.compatibility, request.capacity))
                        return index;
                    // A lower-bound candidate in the exact compatibility class must
                    // satisfy the request unless metadata was corrupted.
                    break;
                }
            }
            return InvalidDedicatedResourceEntry;
        };
        const auto claimReusable = [&](const u32 index) noexcept -> bool
        {
            Impl::Entry& entry = m_impl->entries[index];
            m_impl->RemoveReusable(index);
            entry.state = DedicatedResourceState::Assigned;
            entry.lastUsedFrame = frameSerial;
            if (request.requiresCanonicalDescriptors && !m_impl->EnsureCanonicalDescriptors(entry, frameSerial, failure, descriptorPressure, recordPublicationStats))
            {
                entry.state = DedicatedResourceState::Reusable;
                m_impl->AddReusable(index);
                return false;
            }
            assignment = {index, entry.texture.GetRef(), entry.buffer.GetRef(), entry.requirements, entry.capacity,
                          request.requiresCanonicalDescriptors ? entry.descriptors.shaderResource : rhi::DescriptorHandle{},
                          request.requiresCanonicalDescriptors ? entry.descriptors.unorderedAccess : rhi::DescriptorHandle{}};
            ++m_impl->stats.hits;
            return true;
        };
        if (bucketIndex != InvalidDedicatedResourceEntry)
            m_impl->RefreshHeld(bucketIndex, frameSerial);
        const u32 best = findReusable();
        if (best != InvalidDedicatedResourceEntry)
            return claimReusable(best);

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
        if (!m_impl->ledger->CanReserve(reserved.size))
        {
            // Routine retirement is polled at frame startup. Retry only when
            // pending native destruction could make this reservation fit.
            PollUnlocked();
            m_impl->RefreshAllHeld();
            const u32 promoted = findReusable();
            if (promoted != InvalidDedicatedResourceEntry)
                return claimReusable(promoted);
            u64 projectedCharged = m_impl->ledger->GetStats().chargedBytes;
            const u64 hardLimit = m_impl->config.hardNativeByteLimit;
            bool evicted = false;
            if (!m_impl->ledger->CanReserve(reserved.size))
                while (projectedCharged > hardLimit || reserved.size > hardLimit - projectedCharged)
                {
                    const u32 candidateIndex = m_impl->OldestIdleAny();
                    if (candidateIndex == InvalidDedicatedResourceEntry)
                        break;
                    Impl::Entry& candidate = m_impl->entries[candidateIndex];
                    if (!PoolOwnsOnlyReference(candidate))
                    {
                        if (candidate.state == DedicatedResourceState::PlanReady)
                        {
                            m_impl->descriptorLedger->MarkNotReusable(CanonicalDescriptorReferenceCount(candidate.descriptors));
                            candidate.planReserved = false;
                            candidate.state = DedicatedResourceState::Reusable;
                            m_impl->AddHeld(candidateIndex);
                        }
                        else
                        {
                            m_impl->RemoveReusable(candidateIndex);
                            m_impl->AddHeld(candidateIndex);
                        }
                        continue;
                    }
                    if (!m_impl->RetireCanonicalDescriptors(candidate, {}, failure))
                        return false;
                    if (InjectProviderFailure(m_impl->failureInjection, DedicatedResourceProviderFailurePoint::NativeReleaseObservation, failure,
                                              "injected dedicated-resource native-release observation failure"))
                        return false;
                    const rhi::ResourceRef resource = EntryResource(candidate);
                    candidate.releaseObservation = rhi::ObserveNativeRelease(resource, &rhiFailure);
                    if (!candidate.releaseObservation.IsValid())
                        return PoolFail(failure, MapRhiFailure(rhiFailure, RhiFailureContext::BackendContract),
                                        rhiFailure.message[0] != '\0' ? rhiFailure.message : "dedicated-resource native-release observation failed");
                    if (candidate.state == DedicatedResourceState::Reusable)
                        m_impl->RemoveReusable(candidateIndex);
                    else
                        candidate.planReserved = false;
                    candidate.texture.Reset();
                    candidate.buffer.Reset();
                    candidate.state = DedicatedResourceState::EvictedPendingNativeDestruction;
                    m_impl->AddPending(m_impl->pendingDestruction, candidateIndex);
                    projectedCharged = candidate.requirements.size < projectedCharged ? projectedCharged - candidate.requirements.size : 0;
                    evicted = true;
                }
            if (evicted)
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
        entry.capacity = request.capacity;
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
            m_impl->QuarantineCreatedEntry(static_cast<Impl::Entry&&>(entry), request.compatibility);
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
        m_impl->StoreEntry(static_cast<Impl::Entry&&>(entry), request.compatibility, storedIndex);
        Impl::Entry& stored = m_impl->entries[storedIndex];
        m_impl->AddCharge(stored);
        ++m_impl->stats.misses;
        if (request.requiresCanonicalDescriptors && !m_impl->EnsureCanonicalDescriptors(stored, frameSerial, failure, descriptorPressure, recordPublicationStats))
        {
            stored.state = DedicatedResourceState::Reusable;
            m_impl->AddReusable(storedIndex);
            return false;
        }
        assignment = {storedIndex, stored.texture.GetRef(), stored.buffer.GetRef(), authoritative, stored.capacity,
                      request.requiresCanonicalDescriptors ? stored.descriptors.shaderResource : rhi::DescriptorHandle{},
                      request.requiresCanonicalDescriptors ? stored.descriptors.unorderedAccess : rhi::DescriptorHandle{}};
        return true;
    }

    DedicatedResourceLeaseAcquireResult DedicatedResourcePool::TryAcquireLease(const DedicatedResourceLease& lease, const DedicatedResourceRequest& request,
                                                                                const u64 frameSerial, DedicatedResourceAssignment& assignment,
                                                                                RenderFlowResourceFailure* const failure,
                                                                                CanonicalDescriptorPressure* const descriptorPressure,
                                                                                const bool recordPublicationStats) noexcept
    {
        assignment = {};
        if (descriptorPressure != nullptr)
            *descriptorPressure = {};
        if (m_impl == nullptr || !lease.IsValid())
            return DedicatedResourceLeaseAcquireResult::Stale;
        concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
        if (lease.entry >= m_impl->entries.Size())
            return DedicatedResourceLeaseAcquireResult::Stale;
        Impl::Entry& entry = m_impl->entries[lease.entry];
        if (!entry.planReserved || entry.storeSerial != lease.storeSerial || entry.leaseSerial != lease.leaseSerial)
            return DedicatedResourceLeaseAcquireResult::Stale;
        if (entry.state == DedicatedResourceState::PendingRetirement || entry.state == DedicatedResourceState::Assigned)
            return DedicatedResourceLeaseAcquireResult::Unavailable;
        if (entry.state != DedicatedResourceState::PlanReady)
            return DedicatedResourceLeaseAcquireResult::Stale;
        if (entry.compatibilityBucket == InvalidDedicatedResourceEntry || entry.compatibilityBucket >= m_impl->compatibilityBuckets.Size() ||
            !DedicatedResourceCanSatisfy(m_impl->compatibilityBuckets[entry.compatibilityBucket].compatibility, entry.capacity,
                                         request.compatibility, request.capacity))
        {
            static_cast<void>(PoolFail(failure, RenderFlowResourceFailureCode::BackendContractViolation,
                                       "persistent dedicated lease no longer satisfies its compiled lane contract"));
            return DedicatedResourceLeaseAcquireResult::Failed;
        }
        if (!PoolOwnsOnlyReference(entry))
        {
            m_impl->descriptorLedger->MarkNotReusable(CanonicalDescriptorReferenceCount(entry.descriptors));
            entry.planReserved = false;
            entry.state = DedicatedResourceState::Reusable;
            m_impl->AddHeld(lease.entry);
            return DedicatedResourceLeaseAcquireResult::Stale;
        }

        m_impl->descriptorLedger->MarkNotReusable(CanonicalDescriptorReferenceCount(entry.descriptors));
        entry.state = DedicatedResourceState::Assigned;
        entry.lastUsedFrame = frameSerial;
        if (request.requiresCanonicalDescriptors &&
            !m_impl->EnsureCanonicalDescriptors(entry, frameSerial, failure, descriptorPressure, recordPublicationStats))
        {
            entry.state = DedicatedResourceState::PlanReady;
            m_impl->descriptorLedger->MarkReusable(CanonicalDescriptorReferenceCount(entry.descriptors));
            return DedicatedResourceLeaseAcquireResult::Failed;
        }
        assignment = {lease.entry, entry.texture.GetRef(), entry.buffer.GetRef(), entry.requirements, entry.capacity,
                      request.requiresCanonicalDescriptors ? entry.descriptors.shaderResource : rhi::DescriptorHandle{},
                      request.requiresCanonicalDescriptors ? entry.descriptors.unorderedAccess : rhi::DescriptorHandle{}};
        ++m_impl->stats.hits;
        return DedicatedResourceLeaseAcquireResult::Acquired;
    }

    bool DedicatedResourcePool::ReserveLease(const u32 index, DedicatedResourceLease& lease) noexcept
    {
        lease = {};
        if (m_impl == nullptr)
            return false;
        concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
        if (index >= m_impl->entries.Size())
            return false;
        Impl::Entry& entry = m_impl->entries[index];
        if (entry.state != DedicatedResourceState::Assigned || entry.planReserved || entry.storeSerial == 0 ||
            (entry.texture.IsValid() == entry.buffer.IsValid()) || entry.requirements.size == 0)
            return false;
        if (m_impl->nextLeaseSerial == 0 || m_impl->nextLeaseSerial == ~u64{0})
            VG_FATAL("dedicated resource lease serial exhausted");
        entry.leaseSerial = m_impl->nextLeaseSerial++;
        entry.planReserved = true;
        lease = {index, entry.storeSerial, entry.leaseSerial};
        return true;
    }

    void DedicatedResourcePool::ReleaseLease(const DedicatedResourceLease& lease) noexcept
    {
        if (m_impl == nullptr || !lease.IsValid())
            return;
        concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
        if (lease.entry >= m_impl->entries.Size())
            return;
        Impl::Entry& entry = m_impl->entries[lease.entry];
        if (!entry.planReserved || entry.storeSerial != lease.storeSerial || entry.leaseSerial != lease.leaseSerial)
            return;
        entry.planReserved = false;
        if (entry.state == DedicatedResourceState::PlanReady)
        {
            m_impl->descriptorLedger->MarkNotReusable(CanonicalDescriptorReferenceCount(entry.descriptors));
            entry.state = DedicatedResourceState::Reusable;
            m_impl->AddReusable(lease.entry);
        }
    }

    void DedicatedResourcePool::EvictLease(const DedicatedResourceLease& lease) noexcept
    {
        if (m_impl == nullptr || !lease.IsValid())
            return;
        concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
        if (lease.entry >= m_impl->entries.Size())
            return;
        Impl::Entry& entry = m_impl->entries[lease.entry];
        if (entry.storeSerial != lease.storeSerial || entry.leaseSerial != lease.leaseSerial)
            return;

        // The caller may have dropped the weak reservation before discovering
        // cross-provider hard-budget pressure. leaseSerial still protects this
        // exact reservation lifetime from same-entry ABA, so the released token
        // remains a safe identity until the entry is reserved again.
        if (entry.state == DedicatedResourceState::PendingRetirement)
        {
            entry.planReserved = false;
            entry.reusableAfterRetirement = false;
            return;
        }

        // Assigned should not normally survive into a new frame, but dropping
        // the weak reservation is still the only safe action while a generation
        // owns the resource. Its eventual Retire remains authoritative.
        if (entry.state == DedicatedResourceState::Assigned)
        {
            if (entry.planReserved)
                entry.planReserved = false;
            return;
        }
        if (entry.state == DedicatedResourceState::Reusable)
        {
            if (entry.planReserved || !PoolOwnsOnlyReference(entry))
                return;
            m_impl->RemoveReusable(lease.entry);
            entry.reusableAfterRetirement = false;
            entry.state = DedicatedResourceState::ProviderFailureQuarantine;
            m_impl->AddPending(m_impl->pendingQuarantine, lease.entry);
            return;
        }
        if (entry.state != DedicatedResourceState::PlanReady || !entry.planReserved)
            return;

        entry.planReserved = false;
        if (!PoolOwnsOnlyReference(entry))
        {
            m_impl->descriptorLedger->MarkNotReusable(CanonicalDescriptorReferenceCount(entry.descriptors));
            entry.state = DedicatedResourceState::Reusable;
            m_impl->AddHeld(lease.entry);
            return;
        }

        // PlanReady descriptor bundles are accounted as reusable without living
        // in the normal descriptor heap, so leave that ledger class explicitly
        // before routing through the existing quarantine/destruction machinery.
        m_impl->descriptorLedger->MarkNotReusable(CanonicalDescriptorReferenceCount(entry.descriptors));
        entry.reusableAfterRetirement = false;
        entry.state = DedicatedResourceState::ProviderFailureQuarantine;
        m_impl->AddPending(m_impl->pendingQuarantine, lease.entry);
    }

    bool DedicatedResourcePool::EnsureCanonicalDescriptors(const u32 index, const u64 frameSerial, rhi::DescriptorHandle& shaderResource,
                                                           rhi::DescriptorHandle& unorderedAccess, RenderFlowResourceFailure* const failure,
                                                           CanonicalDescriptorPressure* const descriptorPressure, const bool recordPublicationStats) noexcept
    {
        shaderResource = {};
        unorderedAccess = {};
        if (descriptorPressure != nullptr)
            *descriptorPressure = {};
        if (m_impl == nullptr || index >= m_impl->entries.Size())
            return PoolFail(failure, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "dedicated canonical descriptor owner is invalid");
        concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
        Impl::Entry& entry = m_impl->entries[index];
        if (entry.state != DedicatedResourceState::Assigned || (entry.texture.IsValid() == entry.buffer.IsValid()))
            return PoolFail(failure, RenderFlowResourceFailureCode::InvalidPhase, "dedicated canonical descriptor owner is not assigned");
        if (!m_impl->EnsureCanonicalDescriptors(entry, frameSerial, failure, descriptorPressure, recordPublicationStats))
            return false;
        shaderResource = entry.descriptors.shaderResource;
        unorderedAccess = entry.descriptors.unorderedAccess;
        return true;
    }

    void DedicatedResourcePool::Rollback(const u32 index) noexcept
    {
        const u32 entries[] = {index};
        Rollback({entries, 1});
    }

    void DedicatedResourcePool::Rollback(const containers::ArraySpan<const u32> entries) noexcept
    {
        if (m_impl == nullptr || entries.Empty())
            return;
        concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
        for (const u32 index : entries)
        {
            if (index < m_impl->entries.Size() && m_impl->entries[index].state == DedicatedResourceState::Assigned)
            {
                Impl::Entry& entry = m_impl->entries[index];
                if (entry.planReserved && PoolOwnsOnlyReference(entry))
                {
                    entry.state = DedicatedResourceState::PlanReady;
                    m_impl->descriptorLedger->MarkReusable(CanonicalDescriptorReferenceCount(entry.descriptors));
                }
                else
                {
                    entry.planReserved = false;
                    entry.state = DedicatedResourceState::Reusable;
                    m_impl->AddReusable(index);
                }
            }
        }
    }

    void DedicatedResourcePool::Retire(const u32 index, const rhi::ResidencyFenceSet& safeAfter, const bool reusable) noexcept
    {
        const u32 entries[] = {index};
        Retire({entries, 1}, safeAfter, reusable);
    }

    void DedicatedResourcePool::Retire(const containers::ArraySpan<const u32> entries, const rhi::ResidencyFenceSet& safeAfter, const bool reusable) noexcept
    {
        if (reusable)
            Retire(entries, {}, safeAfter);
        else
            Retire({}, entries, safeAfter);
    }

    void DedicatedResourcePool::Retire(const containers::ArraySpan<const u32> reusableEntries, const containers::ArraySpan<const u32> quarantinedEntries,
                                       const rhi::ResidencyFenceSet& safeAfter) noexcept
    {
        if (m_impl == nullptr || (reusableEntries.Empty() && quarantinedEntries.Empty()))
            return;
        concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
        const bool fencesComplete = FencesComplete(safeAfter);
        const auto retireEntries = [&](const containers::ArraySpan<const u32> entries, const bool reusable) noexcept
        {
            for (const u32 index : entries)
            {
                if (index >= m_impl->entries.Size() || m_impl->entries[index].state != DedicatedResourceState::Assigned)
                    continue;
                Impl::Entry& entry = m_impl->entries[index];
                entry.safeAfter = safeAfter;
                entry.reusableAfterRetirement = reusable;
                if (!reusable)
                    entry.planReserved = false;
                entry.state = DedicatedResourceState::PendingRetirement;
                if (fencesComplete)
                {
                    entry.safeAfter = {};
                    if (reusable)
                    {
                        if (entry.planReserved && PoolOwnsOnlyReference(entry))
                        {
                            entry.state = DedicatedResourceState::PlanReady;
                            m_impl->descriptorLedger->MarkReusable(CanonicalDescriptorReferenceCount(entry.descriptors));
                        }
                        else
                        {
                            entry.planReserved = false;
                            entry.state = DedicatedResourceState::Reusable;
                            m_impl->AddReusable(index);
                        }
                    }
                    else
                    {
                        entry.state = DedicatedResourceState::ProviderFailureQuarantine;
                        m_impl->AddPending(m_impl->pendingQuarantine, index);
                    }
                }
                if (entry.state == DedicatedResourceState::PendingRetirement)
                    m_impl->AddPending(m_impl->pendingRetirement, index);
            }
        };
        retireEntries(reusableEntries, true);
        retireEntries(quarantinedEntries, false);
        m_impl->stats.pendingRetirement = m_impl->pendingRetirement.Size();
        m_impl->stats.pendingNativeDestruction = m_impl->pendingQuarantine.Size() + m_impl->pendingDestruction.Size();
    }

    bool DedicatedResourcePool::TransferOwnership(const containers::ArraySpan<const DedicatedResourceTransfer> entries,
                                                  const rhi::ResidencyFenceSet& submittedFences,
                                                  RenderFlowResourceFailure* const failure) noexcept
    {
        if (m_impl == nullptr)
            return PoolFail(failure, RenderFlowResourceFailureCode::BackendContractViolation, "dedicated-resource pool is unavailable during export publication",
                            RenderFlowResourceSessionState::Executing);
        concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
        ResolveScratchHashSet seenEntries;
        seenEntries.Reserve(entries.Size());
        for (const DedicatedResourceTransfer& transfer : entries)
        {
            const u32 index = transfer.entry;
            if (index >= m_impl->entries.Size() || m_impl->entries[index].state != DedicatedResourceState::Assigned ||
                (m_impl->entries[index].texture.IsValid() == m_impl->entries[index].buffer.IsValid()) || m_impl->entries[index].requirements.size == 0)
                return PoolFail(failure, RenderFlowResourceFailureCode::BackendContractViolation,
                                "terminal export does not reference an assigned dedicated resource", RenderFlowResourceSessionState::Executing);
            if (!seenEntries.Insert(index))
                return PoolFail(failure, RenderFlowResourceFailureCode::BackendContractViolation,
                                "one dedicated resource cannot be transferred to multiple export slots", RenderFlowResourceSessionState::Executing);
            const Impl::Entry& entry = m_impl->entries[index];
            const i32 references = entry.texture.IsValid() ? rhi::GetRefCount(entry.texture.GetRef()) : rhi::GetRefCount(entry.buffer.GetRef());
            // Finish creates exactly one publication owner before asking the
            // pool to relinquish its owner. Canonical descriptor slots remain
            // attached to the pool entry until this transfer admits their
            // submitted-fence retirement. Any reference beyond pool +
            // publication + canonical bundle escaped a resolved-use scope.
            const i32 expectedReferences = 2 + static_cast<i32>(CanonicalDescriptorReferenceCount(entry.descriptors));
            if (references != expectedReferences)
                return PoolFail(failure, RenderFlowResourceFailureCode::BackendContractViolation,
                                "terminal export resource has ownership outside the pool and its publication", RenderFlowResourceSessionState::Executing);
        }

        const rhi::DescriptorRetirement retirement = DescriptorRetirementFor(submittedFences);
        for (const DedicatedResourceTransfer& transfer : entries)
        {
            Impl::Entry& entry = m_impl->entries[transfer.entry];
            if (!m_impl->RetireCanonicalDescriptors(entry, retirement, failure, RenderFlowResourceSessionState::Executing))
                return false;
        }

        for (const DedicatedResourceTransfer& transfer : entries)
        {
            const u32 index = transfer.entry;
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
        for (const u32 index : m_impl->pendingQuarantine)
        {
            const Impl::Entry& entry = m_impl->entries[index];
            u64& projected = entry.desc.kind == FrameResourceKind::Texture ? trim.projectedTextureBytes : trim.projectedBufferBytes;
            projected = entry.requirements.size < projected ? projected - entry.requirements.size : 0;
        }
        for (const u32 index : m_impl->pendingDestruction)
        {
            const Impl::Entry& entry = m_impl->entries[index];
            u64& projected = entry.desc.kind == FrameResourceKind::Texture ? trim.projectedTextureBytes : trim.projectedBufferBytes;
            projected = entry.requirements.size < projected ? projected - entry.requirements.size : 0;
        }
        if (trim.projectedTextureBytes <= m_impl->config.textureSoftTargetBytes &&
            trim.projectedBufferBytes <= m_impl->config.bufferSoftTargetBytes)
            return true;
        m_impl->RefreshAllHeld();
        while (trim.projectedTextureBytes > m_impl->config.textureSoftTargetBytes || trim.projectedBufferBytes > m_impl->config.bufferSoftTargetBytes)
        {
            const bool textureOver = trim.projectedTextureBytes > m_impl->config.textureSoftTargetBytes;
            const bool bufferOver = trim.projectedBufferBytes > m_impl->config.bufferSoftTargetBytes;
            const u32 textureCandidate = textureOver ? m_impl->OldestIdle(FrameResourceKind::Texture) : InvalidDedicatedResourceEntry;
            const u32 bufferCandidate = bufferOver ? m_impl->OldestIdle(FrameResourceKind::Buffer) : InvalidDedicatedResourceEntry;
            u32 index = InvalidDedicatedResourceEntry;
            if (textureCandidate == InvalidDedicatedResourceEntry)
                index = bufferCandidate;
            else if (bufferCandidate == InvalidDedicatedResourceEntry)
                index = textureCandidate;
            else
                index = m_impl->ReusableTrimLess(textureCandidate, bufferCandidate) ? textureCandidate : bufferCandidate;
            if (index == InvalidDedicatedResourceEntry)
                break;
            Impl::Entry& entry = m_impl->entries[index];
            if (!PoolOwnsOnlyReference(entry))
            {
                if (entry.state == DedicatedResourceState::PlanReady)
                {
                    m_impl->descriptorLedger->MarkNotReusable(CanonicalDescriptorReferenceCount(entry.descriptors));
                    entry.planReserved = false;
                    entry.state = DedicatedResourceState::Reusable;
                    m_impl->AddHeld(index);
                }
                else
                {
                    m_impl->RemoveReusable(index);
                    m_impl->AddHeld(index);
                }
                continue;
            }
            if (!m_impl->RetireCanonicalDescriptors(entry, {}, failure, RenderFlowResourceSessionState::Idle))
                return false;
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
            if (entry.state == DedicatedResourceState::Reusable)
                m_impl->RemoveReusable(index);
            else
                entry.planReserved = false;
            entry.texture.Reset();
            entry.buffer.Reset();
            entry.state = DedicatedResourceState::EvictedPendingNativeDestruction;
            m_impl->AddPending(m_impl->pendingDestruction, index);
            if (entry.desc.kind == FrameResourceKind::Texture)
                trim.projectedTextureBytes -= entry.requirements.size;
            else
                trim.projectedBufferBytes -= entry.requirements.size;
        }
        PollUnlocked();
        return true;
    }

    bool DedicatedResourcePool::TrimDescriptorCacheToTarget(RenderFlowResourceFailure* const failure) noexcept
    {
        if (m_impl == nullptr || !m_impl->descriptorDomain.IsValid())
            return true;
        concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
        while (m_impl->descriptorLedger->GetStats().reusableSlots > m_impl->descriptorLedger->GetTarget())
        {
            const u32 before = m_impl->descriptorLedger->GetStats().liveSlots;
            if (!m_impl->EvictOneColdDescriptorBundle(failure, RenderFlowResourceSessionState::Idle, false))
            {
                // A reusable entry can gain an external RHI owner after it was
                // indexed. Eviction demotes such entries to Held, which removes
                // them from the reusable-cache ledger without retiring their
                // canonical descriptors. If that demotion alone satisfied the
                // soft target, maintenance succeeded; pressure paths still
                // require actual live-slot headroom and therefore keep their
                // stricter failure semantics.
                return m_impl->descriptorLedger->GetStats().reusableSlots <= m_impl->descriptorLedger->GetTarget();
            }
            if (m_impl->descriptorLedger->GetStats().liveSlots >= before)
                return PoolFail(failure, RenderFlowResourceFailureCode::BackendContractViolation,
                                "canonical descriptor cache eviction did not make progress", RenderFlowResourceSessionState::Idle);
        }
        return true;
    }

    bool DedicatedResourcePool::GetColdestCanonicalDescriptorCandidate(CanonicalDescriptorEvictionCandidate& candidate) noexcept
    {
        candidate = {};
        if (m_impl == nullptr || !m_impl->descriptorDomain.IsValid())
            return false;
        concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
        const u32 index = m_impl->ColdestReusableDescriptorEntry();
        if (index == InvalidDedicatedResourceEntry)
            return false;
        const Impl::Entry& entry = m_impl->entries[index];
        const u32 descriptorCount = CanonicalDescriptorReferenceCount(entry.descriptors);
        if ((entry.state != DedicatedResourceState::Reusable && entry.state != DedicatedResourceState::PlanReady) || descriptorCount == 0 ||
            (entry.state == DedicatedResourceState::Reusable && entry.descriptorReusablePosition == InvalidDedicatedResourceEntry))
            VG_FATAL("dedicated descriptor cold index contains an ineligible entry");
        candidate.ownerClass = CanonicalDescriptorOwnerClass::Dedicated;
        candidate.entry = index;
        candidate.lastDemandFrame = entry.descriptors.lastDemandFrame;
        candidate.descriptorCount = descriptorCount;
        return true;
    }

    CanonicalDescriptorEvictionResult DedicatedResourcePool::EvictCanonicalDescriptorCandidate(const CanonicalDescriptorEvictionCandidate& candidate, const bool pressure,
                                                                                                 RenderFlowResourceFailure* const failure) noexcept
    {
        if (m_impl == nullptr || !m_impl->descriptorDomain.IsValid() || candidate.ownerClass != CanonicalDescriptorOwnerClass::Dedicated ||
            candidate.entry >= m_impl->entries.Size())
            return CanonicalDescriptorEvictionResult::Skipped;
        concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
        Impl::Entry& entry = m_impl->entries[candidate.entry];
        const u32 descriptorCount = CanonicalDescriptorReferenceCount(entry.descriptors);
        if ((entry.state != DedicatedResourceState::Reusable && entry.state != DedicatedResourceState::PlanReady) || descriptorCount == 0 ||
            (entry.state == DedicatedResourceState::Reusable && entry.descriptorReusablePosition == InvalidDedicatedResourceEntry) ||
            entry.descriptors.lastDemandFrame != candidate.lastDemandFrame)
            return CanonicalDescriptorEvictionResult::Skipped;
        if (!PoolOwnsOnlyReference(entry))
        {
            if (entry.state == DedicatedResourceState::PlanReady)
            {
                m_impl->descriptorLedger->MarkNotReusable(descriptorCount);
                entry.planReserved = false;
                entry.state = DedicatedResourceState::Reusable;
                m_impl->AddHeld(candidate.entry);
            }
            else
            {
                m_impl->RemoveReusable(candidate.entry);
                m_impl->AddHeld(candidate.entry);
            }
            return CanonicalDescriptorEvictionResult::Skipped;
        }
        const RenderFlowResourceSessionState phase = pressure ? RenderFlowResourceSessionState::Resolving : RenderFlowResourceSessionState::Idle;
        if (!m_impl->RetireCanonicalDescriptors(entry, {}, failure, phase))
            return CanonicalDescriptorEvictionResult::Failed;
        if (pressure)
            m_impl->descriptorLedger->RecordPressureEviction();
        else
            m_impl->descriptorLedger->RecordMaintenanceEviction();
        return CanonicalDescriptorEvictionResult::Evicted;
    }

    void DedicatedResourcePool::RefreshCanonicalDescriptorCandidates() noexcept
    {
        if (m_impl == nullptr)
            return;
        concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
        m_impl->RefreshAllHeld();
    }

    bool DedicatedResourcePool::ClearPersistentCache(RenderFlowResourceFailure* const failure) noexcept
    {
        if (m_impl == nullptr || !rhi::IsInitialized())
            return true;
        concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
        PollUnlocked();
        m_impl->RefreshAllHeld();
        if (!m_impl->heldBuckets.Empty())
            return PoolFail(failure, RenderFlowResourceFailureCode::BackendContractViolation,
                            "persistent cache contains a resource owner outside the allocator", RenderFlowResourceSessionState::Idle);
        containers::DynamicArray<u32> candidates{memory::pools::Rendering::GetInstance()};
        containers::DynamicArray<rhi::NativeReleaseObservation> observations{memory::pools::Rendering::GetInstance()};
        candidates.Reserve(m_impl->textureReusableTrim.Size() + m_impl->bufferReusableTrim.Size() + m_impl->pendingRetirement.Size());
        for (const u32 index : m_impl->textureReusableTrim)
        {
            const Impl::Entry& entry = m_impl->entries[index];
            if (!PoolOwnsOnlyReference(entry))
                return PoolFail(failure, RenderFlowResourceFailureCode::BackendContractViolation,
                                "persistent cache contains a resource owner outside the allocator", RenderFlowResourceSessionState::Idle);
            candidates.PushBack(index);
        }
        for (const u32 index : m_impl->bufferReusableTrim)
        {
            const Impl::Entry& entry = m_impl->entries[index];
            if (!PoolOwnsOnlyReference(entry))
                return PoolFail(failure, RenderFlowResourceFailureCode::BackendContractViolation,
                                "persistent cache contains a resource owner outside the allocator", RenderFlowResourceSessionState::Idle);
            candidates.PushBack(index);
        }
        for (const u32 index : m_impl->pendingRetirement)
        {
            const Impl::Entry& entry = m_impl->entries[index];
            if (!PoolOwnsOnlyReference(entry))
                return PoolFail(failure, RenderFlowResourceFailureCode::BackendContractViolation,
                                "persistent cache contains a resource owner outside the allocator", RenderFlowResourceSessionState::Idle);
            candidates.PushBack(index);
        }
        for (u32 index = 0; index < m_impl->entries.Size(); ++index)
        {
            const Impl::Entry& entry = m_impl->entries[index];
            if (entry.state != DedicatedResourceState::PlanReady)
                continue;
            if (!PoolOwnsOnlyReference(entry))
                return PoolFail(failure, RenderFlowResourceFailureCode::BackendContractViolation,
                                "persistent plan cache contains a resource owner outside the allocator", RenderFlowResourceSessionState::Idle);
            candidates.PushBack(index);
        }
        observations.Reserve(candidates.Size());
        for (const u32 index : candidates)
        {
            Impl::Entry& entry = m_impl->entries[index];
            if (!m_impl->RetireCanonicalDescriptors(entry, DescriptorRetirementFor(entry.safeAfter), failure, RenderFlowResourceSessionState::Idle))
            {
                for (rhi::NativeReleaseObservation& prepared : observations)
                    rhi::ReleaseNativeReleaseObservation(prepared);
                return false;
            }
            if (InjectProviderFailure(m_impl->failureInjection, DedicatedResourceProviderFailurePoint::NativeReleaseObservation, failure,
                                      "injected persistent-cache native-release observation failure"))
            {
                for (rhi::NativeReleaseObservation& prepared : observations)
                    rhi::ReleaseNativeReleaseObservation(prepared);
                return false;
            }
            rhi::Failure observationFailure;
            rhi::NativeReleaseObservation observation = rhi::ObserveNativeRelease(EntryResource(entry), &observationFailure);
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
            const u32 index = candidates[position];
            Impl::Entry& entry = m_impl->entries[index];
            if (entry.state == DedicatedResourceState::Reusable)
                m_impl->RemoveReusable(index);
            else if (entry.state == DedicatedResourceState::PendingRetirement)
                m_impl->RemovePending(m_impl->pendingRetirement, index);
            else if (entry.state == DedicatedResourceState::PlanReady)
                entry.planReserved = false;
            entry.releaseObservation = observations[position];
            observations[position] = {};
            entry.texture.Reset();
            entry.buffer.Reset();
            entry.safeAfter = {};
            entry.state = DedicatedResourceState::EvictedPendingNativeDestruction;
            m_impl->AddPending(m_impl->pendingDestruction, index);
        }
        PollUnlocked();
        return true;
    }

    bool DedicatedResourcePool::PrepareHealthyShutdown(bool& deviceLostEvidence, RenderFlowResourceFailure* const failure) noexcept
    {
        deviceLostEvidence = false;
        if (m_impl == nullptr)
            return true;
        if (!rhi::IsInitialized())
            return PoolFail(failure, RenderFlowResourceFailureCode::NotInitialized,
                            "healthy dedicated-resource shutdown requires an initialized RHI", RenderFlowResourceSessionState::Idle);
        concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
        for (Impl::Entry& entry : m_impl->entries)
        {
            if (entry.state == DedicatedResourceState::Assigned)
                return PoolFail(failure, RenderFlowResourceFailureCode::InvalidPhase,
                                "healthy dedicated-resource shutdown still owns an assigned resource", RenderFlowResourceSessionState::Idle);
            const u32 descriptorReferences = CanonicalDescriptorReferenceCount(entry.descriptors);
            if ((entry.state == DedicatedResourceState::EvictedPendingNativeDestruction || entry.state == DedicatedResourceState::NativeReleased) && descriptorReferences != 0)
                return PoolFail(failure, RenderFlowResourceFailureCode::BackendContractViolation,
                                "healthy dedicated-resource shutdown found canonical descriptors detached from native ownership", RenderFlowResourceSessionState::Idle);
            if (descriptorReferences == 0)
                continue;
            const rhi::DescriptorRetirement retirement = entry.state == DedicatedResourceState::PendingRetirement
                                                              ? DescriptorRetirementFor(entry.safeAfter)
                                                              : rhi::DescriptorRetirement{};
            if (!m_impl->RetireCanonicalDescriptors(entry, retirement, failure, RenderFlowResourceSessionState::Idle, &deviceLostEvidence))
                return false;
        }
        return true;
    }

    bool DedicatedResourcePool::ValidateHealthyShutdownCommit(RenderFlowResourceFailure* const failure) const noexcept
    {
        if (m_impl == nullptr)
            return true;
        if (!rhi::IsInitialized())
            return PoolFail(failure, RenderFlowResourceFailureCode::NotInitialized,
                            "healthy dedicated-resource shutdown lost the RHI before commit", RenderFlowResourceSessionState::Idle);
        concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
        for (const Impl::Entry& entry : m_impl->entries)
        {
            if (CanonicalDescriptorReferenceCount(entry.descriptors) != 0)
                return PoolFail(failure, RenderFlowResourceFailureCode::BackendContractViolation,
                                "healthy dedicated-resource shutdown still owns canonical descriptors", RenderFlowResourceSessionState::Idle);
        }
        return true;
    }

    void DedicatedResourcePool::CommitHealthyShutdown() noexcept
    {
        if (m_impl == nullptr)
            return;
        if (!rhi::IsInitialized())
        {
            VG_FATAL("healthy dedicated-resource shutdown lost the RHI after commit preflight");
            return;
        }
        concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
        for (Impl::Entry& entry : m_impl->entries)
        {
            if (CanonicalDescriptorReferenceCount(entry.descriptors) != 0)
            {
                VG_FATAL("healthy dedicated-resource shutdown commit invariant lost canonical descriptor drain");
                return;
            }
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
        m_impl->compatibilityBuckets.Clear();
        m_impl->freeCompatibilityBuckets.Clear();
        m_impl->compatibleHeads.Clear();
        m_impl->textureReusableTrim.Clear();
        m_impl->bufferReusableTrim.Clear();
        m_impl->descriptorReusableTrim.Clear();
        m_impl->heldBuckets.Clear();
        m_impl->pendingRetirement.Clear();
        m_impl->pendingQuarantine.Clear();
        m_impl->pendingDestruction.Clear();
        m_impl->stats = {};
        m_impl->heldRefreshEpoch = 1;
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

    void DedicatedResourcePool::DeviceLost(const rhi::ResidencyFenceSet& submittedFences) noexcept
    {
        if (m_impl == nullptr)
            return;
        concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
        for (Impl::Entry& entry : m_impl->entries)
        {
            const u32 descriptorCount = CanonicalDescriptorReferenceCount(entry.descriptors);
            if (descriptorCount != 0)
            {
                const rhi::DescriptorRetirement retirement{
                    std::max(submittedFences.graphics, entry.safeAfter.graphics),
                    std::max(submittedFences.compute, entry.safeAfter.compute),
                    std::max(submittedFences.copy, entry.safeAfter.copy)};
                if (m_impl->descriptorDomain.IsValid() && rhi::IsInitialized())
                {
                    for (const rhi::DescriptorHandle descriptor : {entry.descriptors.shaderResource, entry.descriptors.unorderedAccess})
                    {
                        if (!descriptor.IsValid())
                            continue;
                        rhi::Failure ignored;
                        static_cast<void>(rhi::RetireDescriptor(m_impl->descriptorDomain, descriptor, retirement, &ignored));
                    }
                }
                if (entry.descriptorReusablePosition != InvalidDedicatedResourceEntry || entry.state == DedicatedResourceState::PlanReady)
                    m_impl->descriptorLedger->MarkNotReusable(descriptorCount);
                m_impl->descriptorLedger->Release(descriptorCount);
                entry.descriptors = {};
            }
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
        m_impl->compatibilityBuckets.Clear();
        m_impl->freeCompatibilityBuckets.Clear();
        m_impl->compatibleHeads.Clear();
        m_impl->textureReusableTrim.Clear();
        m_impl->bufferReusableTrim.Clear();
        m_impl->descriptorReusableTrim.Clear();
        m_impl->heldBuckets.Clear();
        m_impl->pendingRetirement.Clear();
        m_impl->pendingQuarantine.Clear();
        m_impl->pendingDestruction.Clear();
        m_impl->stats = {};
        m_impl->heldRefreshEpoch = 1;
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
