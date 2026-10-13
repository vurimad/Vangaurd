#pragma once

#include <vanguard/concurrency/synchronization.hpp>
#include <vanguard/rendering/render_flow_resource_allocator.hpp>
#include <vanguard/rhi/rhi.hpp>

namespace vanguard::rendering::detail
{
    inline constexpr u32 InvalidDedicatedResourceEntry = 0xffffffffu;

    struct AllocatorNativeByteLedgerStats
    {
        u64 chargedBytes = 0;
        u64 textureBytes = 0;
        u64 bufferBytes = 0;
    };

    struct ResourcePoolTrimState
    {
        // Shared projection carried through every physical pool during one
        // serialized frame-start trim pass.
        u64 projectedTextureBytes = 0;
        u64 projectedBufferBytes = 0;
    };

    struct CanonicalResourceDescriptors
    {
        rhi::DescriptorHandle shaderResource;
        rhi::DescriptorHandle unorderedAccess;
        u64 lastDemandFrame = 0;
    };

    enum class CanonicalDescriptorPressureKind : u8
    {
        None,
        LocalLimit,
        SharedDomain
    };

    struct CanonicalDescriptorPressure
    {
        CanonicalDescriptorPressureKind kind = CanonicalDescriptorPressureKind::None;
        u32 requiredSlots = 0;

        [[nodiscard]] bool IsRequired() const noexcept
        {
            return kind != CanonicalDescriptorPressureKind::None && requiredSlots != 0;
        }
    };

    enum class CanonicalDescriptorOwnerClass : u8
    {
        Dedicated,
        Placed,
        RetainedImport
    };

    struct CanonicalDescriptorEvictionCandidate
    {
        CanonicalDescriptorOwnerClass ownerClass = CanonicalDescriptorOwnerClass::Dedicated;
        u32 entry = InvalidRenderFlowResourceIndex;
        u64 lastDemandFrame = 0;
        u32 descriptorCount = 0;

        [[nodiscard]] bool IsValid() const noexcept
        {
            return entry != InvalidRenderFlowResourceIndex && descriptorCount != 0;
        }
    };

    enum class CanonicalDescriptorEvictionResult : u8
    {
        Evicted,
        Skipped,
        Failed
    };

    [[nodiscard]] inline constexpr u32 CanonicalDescriptorReferenceCount(const CanonicalResourceDescriptors& descriptors) noexcept
    {
        return static_cast<u32>(descriptors.shaderResource.IsValid()) + static_cast<u32>(descriptors.unorderedAccess.IsValid());
    }

    struct AllocatorDescriptorLedgerStats
    {
        u32 liveSlots = 0;
        u32 reusableSlots = 0;
        u32 peakLiveSlots = 0;
        u32 peakReusableSlots = 0;
        u64 publicationHits = 0;
        u64 publicationMisses = 0;
        u64 pressureEvictions = 0;
        u64 maintenanceEvictions = 0;
        u64 rejectedReservations = 0;
    };

    class AllocatorDescriptorLedger final
    {
    public:
        AllocatorDescriptorLedger(u32 target, u32 maximum) noexcept : m_target(target), m_maximum(maximum) {}

        [[nodiscard]] bool CanReserve(u32 slots) const noexcept;
        [[nodiscard]] bool TryReserve(u32 slots) noexcept;
        void Release(u32 slots) noexcept;
        void MarkReusable(u32 slots) noexcept;
        void MarkNotReusable(u32 slots) noexcept;
        [[nodiscard]] bool TryMarkNotReusable(u32 slots) noexcept;
        void RecordPublicationHit(u64 count = 1) noexcept;
        void RecordPublicationMiss() noexcept;
        void RecordPressureEviction() noexcept;
        void RecordMaintenanceEviction() noexcept;
        [[nodiscard]] AllocatorDescriptorLedgerStats GetStats() const noexcept;
        [[nodiscard]] u32 GetTarget() const noexcept { return m_target; }
        [[nodiscard]] u32 GetMaximum() const noexcept { return m_maximum; }

    private:
        mutable concurrency::SpinLock m_lock;
        u32 m_target = 0;
        u32 m_maximum = 0;
        AllocatorDescriptorLedgerStats m_stats;
    };

    // Shared admission authority for every allocator-owned native allocation.
    // Providers reserve before native creation and retain the charge through
    // GPU retirement and native-release observation.
    class AllocatorNativeByteLedger final
    {
    public:
        explicit AllocatorNativeByteLedger(u64 hardLimit) noexcept : m_hardLimit(hardLimit) {}

        [[nodiscard]] bool CanReserve(u64 bytes) const noexcept;
        [[nodiscard]] bool TryReserve(FrameResourceKind kind, u64 bytes) noexcept;
        void Release(FrameResourceKind kind, u64 bytes) noexcept;
        [[nodiscard]] AllocatorNativeByteLedgerStats GetStats() const noexcept;

    private:
        mutable concurrency::SpinLock m_lock;
        u64 m_hardLimit = ~u64{0};
        AllocatorNativeByteLedgerStats m_stats;
    };

    enum class DedicatedResourceState : u8
    {
        Assigned,
        PendingRetirement,
        PlanReady,
        Reusable,
        ProviderFailureQuarantine,
        EvictedPendingNativeDestruction,
        NativeReleased
    };

    // Compact immutable identity for dedicated whole-object compatibility. It
    // intentionally contains only fields that participate in reuse equivalence;
    // buffer capacity is carried separately because larger buffers may satisfy
    // smaller requests within one class.
    struct DedicatedResourceClassKey
    {
        u64 hash = 0;
        rhi::Extent3D textureExtent;
        u32 bufferStructureStride = 0;
        u32 usageBits = 0;
        rhi::Format format = rhi::Format::Unknown;
        rhi::ResourceState initialState = rhi::ResourceState::Common;
        u16 textureMipCount = 0;
        u16 textureArraySize = 0;
        FrameResourceKind kind = FrameResourceKind::Invalid;
        rhi::TextureDimension textureDimension = rhi::TextureDimension::Texture2D;
        rhi::MemoryType bufferMemoryType = rhi::MemoryType::DeviceLocal;
        u8 textureSampleCount = 0;
        bool virtualResource = false;
        bool keepInitialState = true;
    };

    struct DedicatedResourceRequest
    {
        FrameResourceDesc physicalDesc;
        DedicatedResourceClassKey compatibility;
        u64 capacity = 0;
        bool requiresCanonicalDescriptors = false;
    };

    struct DedicatedResourceAssignment
    {
        u32 entry = InvalidDedicatedResourceEntry;
        rhi::TextureRef texture;
        rhi::BufferRef buffer;
        rhi::MemoryRequirements requirements;
        u64 capacity = 0;
        rhi::DescriptorHandle shaderResource;
        rhi::DescriptorHandle unorderedAccess;

        [[nodiscard]] bool IsValid() const noexcept
        {
            return entry != InvalidDedicatedResourceEntry && (texture.IsValid() != buffer.IsValid()) && requirements.size != 0;
        }
    };

    struct DedicatedResourceLease
    {
        u32 entry = InvalidDedicatedResourceEntry;
        u64 storeSerial = 0;
        u64 leaseSerial = 0;

        [[nodiscard]] bool IsValid() const noexcept
        {
            return entry != InvalidDedicatedResourceEntry && storeSerial != 0 && leaseSerial != 0;
        }
    };

    enum class DedicatedResourceLeaseAcquireResult : u8
    {
        Acquired,
        Unavailable,
        Stale,
        Failed
    };

    struct DedicatedResourceTransfer
    {
        u32 entry = InvalidDedicatedResourceEntry;
    };

    // Canonical whole-object compatibility is shared by the cross-frame pool
    // and the same-frame lifetime planner. Keeping it here prevents the two
    // reuse paths from silently accepting different physical objects.
    [[nodiscard]] FrameResourceDesc DedicatedResourcePhysicalDesc(const FrameResourceDesc& desc) noexcept;
    [[nodiscard]] DedicatedResourceRequest BuildDedicatedResourceRequest(const FrameResourceDesc& desc) noexcept;
    [[nodiscard]] bool DedicatedResourceSameCompatibilityClass(const DedicatedResourceClassKey& left, const DedicatedResourceClassKey& right) noexcept;
    [[nodiscard]] bool DedicatedResourceCanSatisfy(const DedicatedResourceClassKey& availableClass, u64 availableCapacity,
                                                   const DedicatedResourceClassKey& requestedClass, u64 requestedCapacity) noexcept;

    struct DedicatedResourcePoolStats
    {
        u64 chargedBytes = 0;
        u64 textureBytes = 0;
        u64 bufferBytes = 0;
        u64 hits = 0;
        u64 misses = 0;
        u64 pendingRetirement = 0;
        u64 pendingNativeDestruction = 0;
    };

    // Private deterministic seam for provider rollback tests. This deliberately
    // stays below the public allocator API: production callers must observe real
    // RHI failures, while tests can stop one Acquire at each fallible boundary.
    enum class DedicatedResourceProviderFailurePoint : u8
    {
        None,
        RequirementQuery,
        NativeCreation,
        AuthoritativeDescriptor,
        AuthoritativeRequirements,
        NativeReleaseObservation,
        PoolCommit,
        CanonicalDescriptorRetirement
    };

    struct DedicatedResourceProviderFailureInjection
    {
        DedicatedResourceProviderFailurePoint point = DedicatedResourceProviderFailurePoint::None;
        // Matching visits allowed to complete before the one injected failure.
        u32 passesBeforeFailure = 0;
        RenderFlowResourceFailureCode code = RenderFlowResourceFailureCode::DeviceLostOrBackendFailure;
    };

    class DedicatedResourcePool final
    {
    public:
        // Descriptor-disabled compatibility constructor for narrow pool tests and
        // callers that do not participate in canonical descriptor ownership.
        DedicatedResourcePool(const RenderFlowResourceAllocatorConfig& config, AllocatorNativeByteLedger& nativeLedger) noexcept;
        DedicatedResourcePool(const RenderFlowResourceAllocatorConfig& config, AllocatorNativeByteLedger& nativeLedger,
                              AllocatorDescriptorLedger& descriptorLedger, rhi::DescriptorDomainRef descriptorDomain) noexcept;
        ~DedicatedResourcePool();
        DedicatedResourcePool(const DedicatedResourcePool&) = delete;
        DedicatedResourcePool& operator=(const DedicatedResourcePool&) = delete;

        // Poll at the coordinator's frame boundary before the acquisition batch.
        [[nodiscard]] bool Acquire(const FrameResourceDesc& desc, u64 frameSerial, DedicatedResourceAssignment& assignment,
                                   RenderFlowResourceFailure* failure) noexcept;
        [[nodiscard]] bool Acquire(const DedicatedResourceRequest& request, u64 frameSerial, DedicatedResourceAssignment& assignment,
                                   RenderFlowResourceFailure* failure, CanonicalDescriptorPressure* descriptorPressure = nullptr,
                                   bool recordPublicationStats = true) noexcept;
        [[nodiscard]] DedicatedResourceLeaseAcquireResult TryAcquireLease(const DedicatedResourceLease& lease, const DedicatedResourceRequest& request,
                                                                         u64 frameSerial, DedicatedResourceAssignment& assignment,
                                                                         RenderFlowResourceFailure* failure,
                                                                         CanonicalDescriptorPressure* descriptorPressure = nullptr,
                                                                         bool recordPublicationStats = true) noexcept;
        [[nodiscard]] bool ReserveLease(u32 entry, DedicatedResourceLease& lease) noexcept;
        void ReleaseLease(const DedicatedResourceLease& lease) noexcept;
        // An incompatible provider topology may need the native bytes, not just
        // the logical reservation. The generation-stamped identity remains a
        // pressure witness after ReleaseLease until that entry is reserved again:
        // ready/reusable owners enter the existing non-reusable destruction path,
        // while fence-blocked owners are marked to do so after retirement. This
        // never waits for the GPU.
        void EvictLease(const DedicatedResourceLease& lease) noexcept;
        [[nodiscard]] bool EnsureCanonicalDescriptors(u32 entry, u64 frameSerial, rhi::DescriptorHandle& shaderResource,
                                                      rhi::DescriptorHandle& unorderedAccess, RenderFlowResourceFailure* failure,
                                                      CanonicalDescriptorPressure* descriptorPressure = nullptr, bool recordPublicationStats = true) noexcept;
        void Rollback(u32 entry) noexcept;
        void Rollback(containers::ArraySpan<const u32> entries) noexcept;
        // Unknown terminal state must not re-enter the pool even after its GPU fence completes.
        void Retire(u32 entry, const rhi::ResidencyFenceSet& safeAfter, bool reusable = true) noexcept;
        void Retire(containers::ArraySpan<const u32> entries, const rhi::ResidencyFenceSet& safeAfter, bool reusable = true) noexcept;
        void Retire(containers::ArraySpan<const u32> reusableEntries, containers::ArraySpan<const u32> quarantinedEntries,
                    const rhi::ResidencyFenceSet& safeAfter) noexcept;
        // The caller must already hold owning references for every entry. This
        // validates the complete batch before removing pool ownership and its
        // byte charges, so publication can remain all-or-nothing.
        [[nodiscard]] bool TransferOwnership(containers::ArraySpan<const DedicatedResourceTransfer> entries,
                                             const rhi::ResidencyFenceSet& submittedFences,
                                             RenderFlowResourceFailure* failure) noexcept;
        void Poll() noexcept;
        [[nodiscard]] bool TrimToSoftTargets(RenderFlowResourceFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool TrimToSoftTargets(ResourcePoolTrimState& trim, RenderFlowResourceFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool TrimDescriptorCacheToTarget(RenderFlowResourceFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool GetColdestCanonicalDescriptorCandidate(CanonicalDescriptorEvictionCandidate& candidate) noexcept;
        [[nodiscard]] CanonicalDescriptorEvictionResult EvictCanonicalDescriptorCandidate(const CanonicalDescriptorEvictionCandidate& candidate, bool pressure,
                                                                                           RenderFlowResourceFailure* failure = nullptr) noexcept;
        void RefreshCanonicalDescriptorCandidates() noexcept;
        [[nodiscard]] bool ClearPersistentCache(RenderFlowResourceFailure* failure = nullptr) noexcept;
        // Healthy terminal path: admit canonical descriptor retirement while the
        // pool and allocator-owned domain stay alive, then release native owners
        // only after the coordinator has drained RHI retirement.
        [[nodiscard]] bool PrepareHealthyShutdown(bool& deviceLostEvidence, RenderFlowResourceFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool ValidateHealthyShutdownCommit(RenderFlowResourceFailure* failure = nullptr) const noexcept;
        void CommitHealthyShutdown() noexcept;
        void DeviceLost(const rhi::ResidencyFenceSet& submittedFences = {}) noexcept;
        void SetProviderFailureInjection(const DedicatedResourceProviderFailureInjection& injection) noexcept;
        void ClearProviderFailureInjection() noexcept;
        [[nodiscard]] DedicatedResourcePoolStats GetStats() const noexcept;

    private:
        friend struct DedicatedResourcePoolInvariantTestAccess;
        struct Impl;
        void PollUnlocked() noexcept;
        Impl* m_impl = nullptr;
    };

    struct DedicatedResourcePoolInvariantTestAccess final
    {
        static bool SetState(DedicatedResourcePool& pool, u32 entry, DedicatedResourceState state) noexcept;
    };

    struct DedicatedResourceProviderTestAccess final
    {
        static void Set(RenderFlowResourceAllocator& allocator, const DedicatedResourceProviderFailureInjection& injection) noexcept;
        static void Clear(RenderFlowResourceAllocator& allocator) noexcept;
    };

    enum class RenderFlowResourceShutdownFailurePoint : u8
    {
        None,
        RetirementFlush
    };

    struct RenderFlowResourceShutdownFailureInjection
    {
        RenderFlowResourceShutdownFailurePoint point = RenderFlowResourceShutdownFailurePoint::None;
    };

    struct RenderFlowResourceShutdownTestAccess final
    {
        static void Set(RenderFlowResourceAllocator& allocator, const RenderFlowResourceShutdownFailureInjection& injection) noexcept;
        static void Clear(RenderFlowResourceAllocator& allocator) noexcept;
        static void AbandonPublishedExecution(RenderFlowResourceAllocator& allocator) noexcept;
    };
} // namespace vanguard::rendering::detail
