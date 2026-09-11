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
        Reusable,
        ProviderFailureQuarantine,
        EvictedPendingNativeDestruction,
        NativeReleased
    };

    struct DedicatedResourceAssignment
    {
        u32 entry = InvalidDedicatedResourceEntry;
        rhi::TextureRef texture;
        rhi::BufferRef buffer;
        rhi::MemoryRequirements requirements;
        FrameResourceDesc physicalDesc;

        [[nodiscard]] bool IsValid() const noexcept
        {
            return entry != InvalidDedicatedResourceEntry && (texture.IsValid() != buffer.IsValid()) && requirements.size != 0;
        }
    };

    // Canonical whole-object compatibility is shared by the cross-frame pool
    // and the same-frame lifetime planner. Keeping it here prevents the two
    // reuse paths from silently accepting different physical objects.
    [[nodiscard]] FrameResourceDesc DedicatedResourcePhysicalDesc(const FrameResourceDesc& desc) noexcept;
    [[nodiscard]] bool DedicatedResourceCanSatisfy(const FrameResourceDesc& availablePhysical,
                                                const FrameResourceDesc& requested) noexcept;

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
        PoolCommit
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
        DedicatedResourcePool(const RenderFlowResourceAllocatorConfig& config, AllocatorNativeByteLedger& ledger) noexcept;
        ~DedicatedResourcePool();
        DedicatedResourcePool(const DedicatedResourcePool&) = delete;
        DedicatedResourcePool& operator=(const DedicatedResourcePool&) = delete;

        // Poll at the coordinator's frame boundary before the acquisition batch.
        [[nodiscard]] bool Acquire(const FrameResourceDesc& desc, u64 frameSerial, DedicatedResourceAssignment& assignment,
                                   RenderFlowResourceFailure* failure) noexcept;
        void Rollback(u32 entry) noexcept;
        // Unknown terminal state must not re-enter the pool even after its GPU fence completes.
        void Retire(u32 entry, const rhi::ResidencyFenceSet& safeAfter, bool reusable = true) noexcept;
        // The caller must already hold owning references for every entry. This
        // validates the complete batch before removing pool ownership and its
        // byte charges, so publication can remain all-or-nothing.
        [[nodiscard]] bool TransferOwnership(containers::ArraySpan<const u32> entries, RenderFlowResourceFailure* failure) noexcept;
        void Poll() noexcept;
        [[nodiscard]] bool TrimToSoftTargets(RenderFlowResourceFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool TrimToSoftTargets(ResourcePoolTrimState& trim, RenderFlowResourceFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool ClearPersistentCache(RenderFlowResourceFailure* failure = nullptr) noexcept;
        void DeviceLost() noexcept;
        void SetProviderFailureInjection(const DedicatedResourceProviderFailureInjection& injection) noexcept;
        void ClearProviderFailureInjection() noexcept;
        [[nodiscard]] DedicatedResourcePoolStats GetStats() const noexcept;

    private:
        struct Impl;
        void PollUnlocked() noexcept;
        Impl* m_impl = nullptr;
    };

    struct DedicatedResourceProviderTestAccess final
    {
        static void Set(RenderFlowResourceAllocator& allocator, const DedicatedResourceProviderFailureInjection& injection) noexcept;
        static void Clear(RenderFlowResourceAllocator& allocator) noexcept;
    };
} // namespace vanguard::rendering::detail
