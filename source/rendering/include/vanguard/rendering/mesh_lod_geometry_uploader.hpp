#pragma once

#include <vanguard/io/io.hpp>
#include <vanguard/resources/resources.hpp>
#include <vanguard/rendering/mesh_geometry_upload.hpp>

namespace vanguard::rendering
{
    inline constexpr u32 InvalidMeshLodGeometryUploadIndex = 0xffffffffu;

    struct MeshLodGeometryUploadRequestId
    {
        u32 index = InvalidMeshLodGeometryUploadIndex;
        u32 generation = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return index != InvalidMeshLodGeometryUploadIndex && generation != 0;
        }

        [[nodiscard]] constexpr bool operator==(const MeshLodGeometryUploadRequestId&) const noexcept = default;
    };

    enum class MeshLodGeometryUploadState : u8
    {
        Invalid,
        ReadingPages,
        ReadyForUpload,
        PreparingUpload,
        Complete,
        Failed
    };

    struct MeshLodGeometryUploaderConfig
    {
        u32 maximumRequests = 4096;
        u32 maximumLodsPerTick = 64;
        u64 maximumUploadBytesPerTick = 64ull * 1024ull * 1024ull;
        u64 maximumPendingUploadBytes = 256ull * 1024ull * 1024ull;
        u32 maximumPreparationDeferrals = 120;
    };

    enum class MeshLodGeometryUploadFailureCode : u8
    {
        None,
        NotInitialized,
        AlreadyInitialized,
        WrongThread,
        InvalidConfiguration,
        InvalidArgument,
        CapacityExceeded,
        StaleResourceGeneration,
        PageReadFailure,
        GeometryPreparationFailure,
        GeometrySubmissionFailure,
        GeometryRetirementFailure,
        LiveRequestsRemain
    };

    struct MeshLodGeometryUploadFailure
    {
        MeshLodGeometryUploadFailureCode code = MeshLodGeometryUploadFailureCode::None;
        MeshLodGeometryUploadRequestId request;
        u32 page = meshes::InvalidRecordIndex;
        const char* message = nullptr;
        meshes::Result pageFailure = meshes::Result::Success;
        MeshGeometryUploadFailure preparationFailure;
        GeometryUploadFailure submissionFailure;
        GeometryAllocatorFailure allocatorFailure;
    };

    /// Active physical geometry for one complete LOD, not yet visible through GPU Scene. The receiver must
    /// either publish every placement atomically in the next phase or retire every placement through the
    /// GeometryAllocator. The weak handle identifies the exact immutable resource generation that produced it.
    struct PendingMeshLodGeometry
    {
        PendingMeshLodGeometry() noexcept;
        ~PendingMeshLodGeometry();

        PendingMeshLodGeometry(const PendingMeshLodGeometry&) = delete;
        PendingMeshLodGeometry& operator=(const PendingMeshLodGeometry&) = delete;
        PendingMeshLodGeometry(PendingMeshLodGeometry&& other) noexcept;
        PendingMeshLodGeometry& operator=(PendingMeshLodGeometry&& other) noexcept;

        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] bool HasOwnership() const noexcept;
        /// Clears only an empty or moved-from value. Live geometry must use AbortPendingMeshLodGeometry.
        void Reset() noexcept;

        resources::WeakResourceHandle resource;
        crypto::Digest256 meshContentFingerprint;
        containers::DynamicArray<u32> pages;
        containers::DynamicArray<GeometryPlacement> geometries;
        containers::DynamicArray<MeshGeometrySubmeshUpload> submeshes;
        rhi::GpuFence copyCompletion;
        u16 lod = 0xffffu;
        u64 vertexBytes = 0;
        u64 indexBytes = 0;
    };

    /// Retires every unpublished placement. The ranges remain unavailable until the allocator's normal
    /// comprehensive retirement-fence epoch completes.
    [[nodiscard]] bool AbortPendingMeshLodGeometry(PendingMeshLodGeometry& geometry, GeometryAllocator& allocator, GeometryAllocatorFailure* failure = nullptr) noexcept;

    struct MeshLodGeometryUploaderStats
    {
        u64 requestsIssued = 0;
        u64 requestsCoalesced = 0;
        u64 requestsCancelled = 0;
        u64 requestsFailed = 0;
        u64 lodsSubmitted = 0;
        u64 bytesSubmitted = 0;
        u64 preparationDeferrals = 0;
        u32 liveRequests = 0;
        u32 readingRequests = 0;
        u32 readyRequests = 0;
        u32 preparingRequests = 0;
        u32 completeRequests = 0;
        u32 failedRequests = 0;
        u64 pendingUploadBytes = 0;
    };

    /// Renderer-serialized bridge from Phase 2 page requests to GeometryUploader submission. Page I/O remains
    /// owned by MeshPageSource/ResourceRangeReadQueue, which already supplies exact-range coalescing, retry, and
    /// byte-budget admission. Tick never waits for I/O or a GPU fence.
    class MeshLodGeometryUploader final
    {
    public:
        struct Impl;

        MeshLodGeometryUploader() noexcept = default;
        ~MeshLodGeometryUploader();

        MeshLodGeometryUploader(const MeshLodGeometryUploader&) = delete;
        MeshLodGeometryUploader& operator=(const MeshLodGeometryUploader&) = delete;

        [[nodiscard]] bool Initialize(GeometryAllocator& allocator, GeometryUploader& uploader, const MeshLodGeometryUploaderConfig& config = {}, MeshLodGeometryUploadFailure* failure = nullptr) noexcept;
        /// Refuses shutdown while requests remain. Call Cancel for pending/failed requests and TakeCompleted for
        /// completed ownership; this prevents unpublished active geometry from being silently leaked.
        [[nodiscard]] bool Shutdown(MeshLodGeometryUploadFailure* failure = nullptr) noexcept;
        void AbandonDevice() noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;

        /// Idempotent for the same resource generation and LOD: repeated demand returns the existing request id.
        [[nodiscard]] bool Request(const resources::ResourceHandle& resource, u16 lod, MeshLodGeometryUploadRequestId& request, io::AsyncPriority priority = io::eAsyncPriority_Streaming,
                                   MeshLodGeometryUploadFailure* failure = nullptr) noexcept;
        /// Cancels page interests and removes a non-complete request. Completed geometry ownership must be taken.
        [[nodiscard]] bool Cancel(MeshLodGeometryUploadRequestId request, MeshLodGeometryUploadFailure* failure = nullptr) noexcept;

        /// Polls finished page reads and submits ready complete LODs within the configured per-tick limits.
        [[nodiscard]] bool Tick(MeshLodGeometryUploadFailure* failure = nullptr) noexcept;

        [[nodiscard]] MeshLodGeometryUploadState GetState(MeshLodGeometryUploadRequestId request) const noexcept;
        [[nodiscard]] bool GetRequestFailure(MeshLodGeometryUploadRequestId request, MeshLodGeometryUploadFailure& failure) const noexcept;
        /// Moves active unpublished geometry to the caller and invalidates the request id.
        [[nodiscard]] bool TakeCompleted(MeshLodGeometryUploadRequestId request, PendingMeshLodGeometry& pendingGeometry, MeshLodGeometryUploadFailure* failure = nullptr) noexcept;
        [[nodiscard]] MeshLodGeometryUploaderStats GetStats() const noexcept;

    private:
        Impl* m_impl = nullptr;
    };
} // namespace vanguard::rendering
