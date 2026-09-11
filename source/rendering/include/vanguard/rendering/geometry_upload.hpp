#pragma once

#include <vanguard/rendering/geometry_allocator.hpp>

namespace vanguard::rendering
{
    inline constexpr u32 MaximumGeometryUploadSegments = 8;

    struct GeometryUploadConfig
    {
        u64 bytesPerSegment = 16ull * 1024ull * 1024ull;
        /// Maximum one-off mapped upload allocation used when a complete atomic batch exceeds a regular segment.
        u64 maximumOverflowBytes = 256ull * 1024ull * 1024ull;
        u32 segmentCount = 3;
        u32 maximumGeometriesPerBatch = 4096;
        u32 maximumCopiesPerBatch = 65'536;
        u64 stagingAlignment = 16;
    };

    struct GeometryUploadRequest
    {
        GeometryReservation geometry;
    };

    struct GeometryUploadSlice
    {
        void* destination = nullptr;
        u64 size = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return destination != nullptr && size != 0;
        }
    };

    /// Direct views into one selected persistently mapped segment. The producer writes final cooker-authored
    /// fixed-function bytes, then completes the whole geometry ticket exactly once.
    struct GeometryUploadReservation
    {
        GeometryUploadSlice vertexStreams[rhi::MaximumVertexBindings];
        GeometryUploadSlice indices;
        u32 bindingCount = 0;
        u32 ticket = 0xffffffffu;
        u32 batch = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return bindingCount != 0 && bindingCount <= rhi::MaximumVertexBindings && indices.IsValid() && ticket != 0xffffffffu && batch != 0;
        }
    };

    struct GeometryUploadResult
    {
        rhi::GpuFence completion;
        u32 geometryCount = 0;
        u32 copyCount = 0;
        u32 destinationBufferCount = 0;
        u64 uploadedBytes = 0;
        u64 stagingBytes = 0;
    };

    enum class GeometryUploadFailureCode : u8
    {
        None,
        NotInitialized,
        AlreadyInitialized,
        WrongThread,
        InvalidConfiguration,
        InvalidRequest,
        InvalidAllocationState,
        DuplicateAllocation,
        BatchAlreadyOpen,
        NoOpenBatch,
        BatchNotReady,
        StagingExhausted,
        GeometryCapacityExceeded,
        CopyCapacityExceeded,
        ArithmeticOverflow,
        RhiFailure,
        AllocatorFailure
    };

    struct GeometryUploadFailure
    {
        GeometryUploadFailureCode code = GeometryUploadFailureCode::None;
        u32 request = 0xffffffffu;
        const char* message = nullptr;
        rhi::Failure rhiFailure;
        GeometryAllocatorFailure allocatorFailure;
    };

    struct GeometryUploadStats
    {
        u64 batchesSubmitted = 0;
        u64 geometriesUploaded = 0;
        u64 copiesRecorded = 0;
        u64 bytesUploaded = 0;
        u64 stagingBytesCommitted = 0;
        u64 segmentBusyEvents = 0;
        u64 overflowBatches = 0;
        u64 overflowBytes = 0;
        u64 rejectedOperations = 0;
        u32 inFlightSegments = 0;
        u64 inFlightBytes = 0;
    };

    /// Renderer-serialized fixed-function geometry uploader. Producer jobs may fill and complete mapped
    /// reservations concurrently; Begin, Submit, and Cancel remain on the serialized renderer command chain.
    class GeometryUploader final
    {
    public:
        struct Impl;

        GeometryUploader() noexcept = default;
        ~GeometryUploader();

        GeometryUploader(const GeometryUploader&) = delete;
        GeometryUploader& operator=(const GeometryUploader&) = delete;

        [[nodiscard]] bool Initialize(GeometryAllocator& allocator, const GeometryUploadConfig& config = {}, GeometryUploadFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool Shutdown(GeometryUploadFailure* failure = nullptr) noexcept;
        void AbandonDevice() noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;

        [[nodiscard]] bool Begin(containers::ArraySpan<const GeometryUploadRequest> requests, containers::ArraySpan<GeometryUploadReservation> reservations, GeometryUploadFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool Complete(GeometryUploadReservation reservation, GeometryUploadFailure* failure = nullptr) noexcept;

        /// Submits one CopySync command list and atomically commits every geometry reservation only after
        /// successful submission. Placements retain request order.
        [[nodiscard]] bool Submit(containers::ArraySpan<GeometryPlacement> placements, GeometryUploadResult& result, GeometryUploadFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool Cancel(GeometryUploadFailure* failure = nullptr) noexcept;

        [[nodiscard]] GeometryUploadStats GetStats() const noexcept;

    private:
        Impl* m_impl = nullptr;
    };
} // namespace vanguard::rendering
