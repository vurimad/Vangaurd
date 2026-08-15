#pragma once

#include <vanguard/containers/containers.hpp>
#include <vanguard/rendering/gpu_scene_lifetime.hpp>

namespace vanguard::rendering
{
    inline constexpr u32 MaximumGpuSceneUploadSegments = 8;

    struct GpuSceneUploadConfig
    {
        u64 bytesPerSegment = 16u * 1024u * 1024u;
        u32 segmentCount = 3;
        u32 maximumUpdatesPerBatch = 16'384;
        u32 maximumCopiesPerBatch = 32'768;
    };

    struct GpuSceneUploadRequest
    {
        GpuSceneAllocation allocation;
        u32 allocationOffset = 0;
        u32 elementCount = 0;
    };

    /// Direct view into the selected persistently mapped upload segment. The caller writes the final GPU
    /// table representation here and calls Complete before submitting the batch.
    struct GpuSceneUploadReservation
    {
        void* destination = nullptr;
        u64 size = 0;
        u32 ticket = 0xffffffffu;
        u32 batch = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return destination != nullptr && size != 0 && ticket != 0xffffffffu && batch != 0;
        }
    };

    struct GpuSceneUploadResult
    {
        rhi::GpuFence completion;
        u32 requestedUpdates = 0;
        u32 uniqueUpdates = 0;
        u32 copyCount = 0;
        u32 affectedPages = 0;
        u64 uploadedBytes = 0;
        u64 supersededBytes = 0;
    };

    enum class GpuSceneUploadFailureCode : u8
    {
        None,
        NotInitialized,
        AlreadyInitialized,
        WrongThread,
        InvalidConfiguration,
        InvalidRequest,
        InvalidAllocationState,
        OverlappingUpdates,
        BatchAlreadyOpen,
        NoOpenBatch,
        BatchNotReady,
        StagingExhausted,
        UpdateCapacityExceeded,
        CopyCapacityExceeded,
        RhiFailure,
        LifetimeFailure
    };

    struct GpuSceneUploadFailure
    {
        GpuSceneUploadFailureCode code = GpuSceneUploadFailureCode::None;
        u32 request = 0xffffffffu;
        const char* message = nullptr;
        rhi::Failure rhiFailure;
        GpuSceneLifetimeFailure lifetimeFailure;
    };

    struct GpuSceneUploadStats
    {
        u64 batchesSubmitted = 0;
        u64 requestedUpdates = 0;
        u64 uniqueUpdates = 0;
        u64 copiesRecorded = 0;
        u64 bytesUploaded = 0;
        u64 bytesSuperseded = 0;
        u64 segmentBusyEvents = 0;
        u64 rejectedOperations = 0;
    };

    /// Publishes sparse CPU changes into persistent GPU Scene pages. Payload bytes are never retained in
    /// CPU containers: callers write directly into a mapped upload segment selected for the batch.
    class GpuSceneUploader final
    {
    public:
        struct Impl;

        GpuSceneUploader() noexcept = default;
        ~GpuSceneUploader();

        GpuSceneUploader(const GpuSceneUploader&) = delete;
        GpuSceneUploader& operator=(const GpuSceneUploader&) = delete;

        [[nodiscard]] bool Initialize(GpuSceneTables& tables, GpuSceneLifetime& lifetime,
                                      const GpuSceneUploadConfig& config = {},
                                      GpuSceneUploadFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool Shutdown(GpuSceneUploadFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;

        /// Plans one batch in destination order. Exact duplicate ranges use last-request-wins semantics;
        /// the superseded request receives an invalid reservation. Other overlaps are rejected.
        [[nodiscard]] bool Begin(containers::ArraySpan<const GpuSceneUploadRequest> requests,
                                 containers::ArraySpan<GpuSceneUploadReservation> reservations,
                                 GpuSceneUploadFailure* failure = nullptr) noexcept;

        /// May be called by producer jobs after they finish writing their reservation.
        [[nodiscard]] bool Complete(GpuSceneUploadReservation reservation,
                                    GpuSceneUploadFailure* failure = nullptr) noexcept;

        /// Records one graphics-queue copy command list, submits it, and publishes newly allocated identities.
        [[nodiscard]] bool Submit(GpuSceneUploadResult& result,
                                  GpuSceneUploadFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool Cancel(GpuSceneUploadFailure* failure = nullptr) noexcept;

        [[nodiscard]] GpuSceneUploadStats GetStats() const noexcept;

    private:
        Impl* m_impl = nullptr;
    };
} // namespace vanguard::rendering
