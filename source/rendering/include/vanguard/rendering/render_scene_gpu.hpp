#pragma once

#include <vanguard/rendering/gpu_scene_upload.hpp>
#include <vanguard/rendering/render_scene.hpp>

namespace vanguard::rendering
{
    enum class RenderSceneGpuObjectKind : u8
    {
        Instance,
        Light,
        Decal
    };

    enum class RenderSceneGpuDirtyFlags : u32
    {
        None = 0,
        Initial = 1u << 0u,
        Transform = 1u << 1u,
        Visibility = 1u << 2u,
        Resources = 1u << 3u,
        Properties = 1u << 4u
    };

    [[nodiscard]] constexpr RenderSceneGpuDirtyFlags operator|(const RenderSceneGpuDirtyFlags left, const RenderSceneGpuDirtyFlags right) noexcept
    {
        return static_cast<RenderSceneGpuDirtyFlags>(static_cast<u32>(left) | static_cast<u32>(right));
    }

    struct RenderSceneGpuIdentity
    {
        RenderSceneGpuObjectKind kind = RenderSceneGpuObjectKind::Instance;
        GpuSceneAllocation allocation;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return allocation.IsValid();
        }
    };

    struct RenderSceneGpuMeshBinding
    {
        GpuRenderableHandle renderable;
        GpuMaterialSetHandle materialSet;
    };

    struct RenderSceneGpuBindingReceipt
    {
        RenderProxyHandle proxy;
        u64 revision = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return proxy.IsValid() && revision != 0;
        }
    };

    enum class RenderSceneGpuBindingStatus : u8
    {
        Pending,
        Accepted,
        Stale
    };

    struct RenderSceneGpuRetirement
    {
        RenderProxyHandle proxy;
        RenderSceneGpuIdentity identity;
    };

    struct RenderSceneGpuPublication
    {
        RenderSceneHandle scene;
        u64 mutationEpoch = 0;
        u64 serial = 0;
        u32 changeCount = 0;
        containers::ArraySpan<const RenderSceneGpuRetirement> retirements;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return scene.IsValid() && serial != 0;
        }
    };

    /// One disjoint source range in a frozen publication. Different non-empty ranges may be written concurrently.
    struct RenderSceneGpuWriteRange
    {
        u32 index = 0;
        u32 firstReservation = 0;
        u32 objectCount = 0;

        [[nodiscard]] constexpr bool HasWork() const noexcept
        {
            return objectCount != 0;
        }
    };

    /// Token for one phase-owned parallel dirty pass. Each Jobs group writes only to its preassigned slice.
    struct RenderSceneGpuDirtyBatch
    {
        RenderSceneHandle scene;
        u64 serial = 0;
        u32 groupCount = 0;
        u32 entriesPerGroup = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return scene.IsValid() && serial != 0 && groupCount != 0 && entriesPerGroup != 0;
        }
    };

    struct RenderSceneGpuConfig
    {
        f32 worldCellSize = 256.0f;
        /// Bounds conversion work per Jobs invocation without copying dirty indices into another queue.
        u32 maximumObjectsPerWriteRange = 1024;
    };

    enum class RenderSceneGpuFailureCode : u8
    {
        None,
        NotInitialized,
        AlreadyInitialized,
        WrongThread,
        InvalidConfiguration,
        InvalidHandle,
        InvalidState,
        Busy,
        LifetimeFailure,
        SourceUnavailable,
        ReservationMismatch
    };

    struct RenderSceneGpuFailure
    {
        RenderSceneGpuFailureCode code = RenderSceneGpuFailureCode::None;
        RenderSceneHandle scene;
        RenderProxyHandle proxy;
        const char* message = nullptr;
        GpuSceneLifetimeFailure lifetimeFailure;
    };

    struct RenderSceneGpuStats
    {
        u32 trackedInstances = 0;
        u32 trackedLights = 0;
        u32 trackedDecals = 0;
        u32 pendingChanges = 0;
        u32 pendingRetirements = 0;
        u64 publications = 0;
        u64 coalescedChanges = 0;
        u64 writtenObjects = 0;
        u64 cancelledPublications = 0;
        u64 rejectedOperations = 0;
    };

    /// Owns the sparse publication boundary between authoritative RenderScene proxy state and persistent GPU Scene tables.
    /// It retains identities and dirty metadata only; complete GPU payloads are written directly into mapped upload reservations.
    class RenderSceneGpuPublisher final
    {
    public:
        struct Impl;

        RenderSceneGpuPublisher() noexcept = default;
        ~RenderSceneGpuPublisher();

        RenderSceneGpuPublisher(const RenderSceneGpuPublisher&) = delete;
        RenderSceneGpuPublisher& operator=(const RenderSceneGpuPublisher&) = delete;

        [[nodiscard]] bool Initialize(RenderSceneManager& scenes, GpuSceneLifetime& lifetime, const RenderSceneGpuConfig& config = {}, RenderSceneGpuFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool Shutdown(RenderSceneGpuFailure* failure = nullptr) noexcept;
        void AbandonDevice() noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;
        /// Shared with consumers that reconstruct scene positions from RenderViewOrigin.
        [[nodiscard]] f32 GetWorldCellSize() const noexcept;

        [[nodiscard]] bool BindMesh(RenderProxyHandle proxy, const RenderSceneGpuMeshBinding& binding, RenderSceneGpuBindingReceipt& receipt,
                                    RenderSceneGpuFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool BindMesh(RenderProxyHandle proxy, const RenderSceneGpuMeshBinding& binding, RenderSceneGpuFailure* failure = nullptr) noexcept
        {
            RenderSceneGpuBindingReceipt receipt;
            return BindMesh(proxy, binding, receipt, failure);
        }
        /// Invalidates the resolved renderable/material-set binding while preserving the instance identity.
        /// Resource integration rebinds the proxy after the replacement definitions become resident.
        [[nodiscard]] bool ClearMeshBinding(RenderProxyHandle proxy, RenderSceneGpuBindingReceipt& receipt,
                                            RenderSceneGpuFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool ClearMeshBinding(RenderProxyHandle proxy, RenderSceneGpuFailure* failure = nullptr) noexcept
        {
            RenderSceneGpuBindingReceipt receipt;
            return ClearMeshBinding(proxy, receipt, failure);
        }
        [[nodiscard]] bool BindDecalMaterial(RenderProxyHandle proxy, GpuMaterialHandle material, RenderSceneGpuBindingReceipt& receipt, RenderSceneGpuFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool ClearDecalMaterialBinding(RenderProxyHandle proxy, RenderSceneGpuBindingReceipt* receipt = nullptr, RenderSceneGpuFailure* failure = nullptr) noexcept;
        [[nodiscard]] RenderSceneGpuBindingStatus PollBinding(const RenderSceneGpuBindingReceipt& receipt) const noexcept;
        /// One shared-lock acquisition for a changed-binding batch from one scene.
        [[nodiscard]] bool PollBindings(RenderSceneHandle scene, containers::ArraySpan<const RenderSceneGpuBindingReceipt> receipts,
                                        containers::ArraySpan<RenderSceneGpuBindingStatus> statuses) const noexcept;
        [[nodiscard]] bool GetIdentity(RenderProxyHandle proxy, RenderSceneGpuIdentity& identity) const noexcept;

        /// Freezes the coalesced changes for one completed RenderScene update epoch. This may execute on the renderer's
        /// serialized Jobs chain after scene-update dependencies; no RHI work is recorded or submitted.
        [[nodiscard]] bool Prepare(RenderSceneHandle scene, u64 mutationEpoch, RenderSceneGpuPublication& publication, RenderSceneGpuFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool BuildUploadRequests(const RenderSceneGpuPublication& publication, containers::DynamicArray<GpuSceneUploadRequest>& requests, RenderSceneGpuFailure* failure = nullptr) const noexcept;
        /// Returns the stable source-range count for this frozen publication. Empty ranges need no worker invocation.
        [[nodiscard]] u32 GetWriteRangeCount(const RenderSceneGpuPublication& publication) const noexcept;
        [[nodiscard]] bool GetWriteRange(const RenderSceneGpuPublication& publication, u32 rangeIndex, RenderSceneGpuWriteRange& range) const noexcept;
        /// Writes one disjoint range directly into reservations returned by GpuSceneUploader::Begin.
        /// The caller may execute different non-empty ranges concurrently and must invoke each exactly once.
        [[nodiscard]] bool WriteRange(const RenderSceneGpuPublication& publication, containers::ArraySpan<const GpuSceneUploadReservation> reservations, u32 rangeIndex,
                                      RenderSceneGpuFailure* failure = nullptr) noexcept;
        /// Verifies that every non-empty range was written. Call after the Jobs dependency joining range writers.
        [[nodiscard]] bool FinishWrites(const RenderSceneGpuPublication& publication, RenderSceneGpuFailure* failure = nullptr) noexcept;
        /// Closes a staged publication and forwards removals to GPU-safe cancellation or retirement.
        [[nodiscard]] bool Complete(const RenderSceneGpuPublication& publication, RenderSceneGpuFailure* failure = nullptr) noexcept;
        /// Discards a publication before staging and preserves its sealed dirty indices for retry.
        [[nodiscard]] bool Cancel(const RenderSceneGpuPublication& publication, RenderSceneGpuFailure* failure = nullptr) noexcept;

        [[nodiscard]] RenderSceneGpuStats GetStats() const noexcept;

    private:
        friend class RenderSceneManager;

        [[nodiscard]] bool AttachScene(RenderSceneHandle scene, u32 maximumProxies, u32 maximumParallelChanges) noexcept;
        [[nodiscard]] bool DetachScene(RenderSceneHandle scene) noexcept;
        [[nodiscard]] bool ReserveIdentity(RenderSceneGpuObjectKind kind, RenderSceneGpuIdentity& identity, RenderSceneGpuFailure* failure = nullptr) noexcept;
        void CancelIdentity(RenderSceneGpuIdentity identity) noexcept;
        [[nodiscard]] bool TrackProxy(RenderProxyHandle proxy, RenderSceneGpuIdentity identity) noexcept;
        [[nodiscard]] bool MarkProxyDirty(RenderProxyHandle proxy, RenderSceneGpuDirtyFlags dirty) noexcept;
        [[nodiscard]] bool BeginParallelDirty(RenderSceneHandle scene, u32 groupCount, u32 entryCount, RenderSceneGpuDirtyBatch& batch) noexcept;
        [[nodiscard]] bool MarkProxyDirty(const RenderSceneGpuDirtyBatch& batch, u32 group, RenderProxyHandle proxy, RenderSceneGpuDirtyFlags dirty) noexcept;
        [[nodiscard]] bool EndParallelDirty(const RenderSceneGpuDirtyBatch& batch) noexcept;
        void CancelParallelDirty(const RenderSceneGpuDirtyBatch& batch) noexcept;
        [[nodiscard]] bool RetireProxy(RenderProxyHandle proxy) noexcept;
        [[nodiscard]] bool AllowsMutation(RenderSceneHandle scene) const noexcept;

        Impl* m_impl = nullptr;
    };
} // namespace vanguard::rendering
