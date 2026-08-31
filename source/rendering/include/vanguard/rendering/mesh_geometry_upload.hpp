#pragma once

#include <vanguard/meshes/meshes.hpp>
#include <vanguard/rendering/geometry_upload.hpp>

namespace vanguard::rendering
{
    /// Non-owning bytes returned by a successful MeshPageSource read. The caller keeps the payload alive until
    /// PrepareMeshLodGeometryUpload returns; submitted bytes are then owned by GeometryUploader staging.
    struct VerifiedMeshPagePayload
    {
        u32 page = meshes::InvalidRecordIndex;
        containers::ArraySpan<const u8> bytes;
    };

    struct MeshGeometrySubmeshUpload
    {
        u32 submesh = meshes::InvalidRecordIndex;
        u32 geometry = InvalidGeometryIndex;
    };

    struct MeshGeometryStagingCopy
    {
        const u8* source = nullptr;
        void* destination = nullptr;
        u64 size = 0;
    };

    /// Unpublished ownership produced after every upload ticket for one complete LOD has been filled and completed.
    /// GeometryUploader still owns one open batch; the caller must next submit it or call CancelPreparedMeshLodUpload.
    struct PreparedMeshLodUpload
    {
        PreparedMeshLodUpload() noexcept;

        PreparedMeshLodUpload(const PreparedMeshLodUpload&) = delete;
        PreparedMeshLodUpload& operator=(const PreparedMeshLodUpload&) = delete;
        PreparedMeshLodUpload(PreparedMeshLodUpload&&) noexcept = default;
        PreparedMeshLodUpload& operator=(PreparedMeshLodUpload&&) noexcept = default;

        [[nodiscard]] bool IsValid() const noexcept;
        void Reset() noexcept;

        crypto::Digest256 meshContentFingerprint;
        containers::DynamicArray<u32> pages;
        containers::DynamicArray<GeometryReservation> geometries;
        containers::DynamicArray<MeshGeometrySubmeshUpload> submeshes;
        containers::DynamicArray<GeometryUploadReservation> uploadReservations;
        containers::DynamicArray<MeshGeometryStagingCopy> stagingCopies;
        u16 lod = 0xffffu;
        u64 vertexBytes = 0;
        u64 indexBytes = 0;
        bool stagingFilled = false;
    };

    enum class MeshGeometryUploadFailureCode : u8
    {
        None,
        InvalidArgument,
        InvalidState,
        InvalidLod,
        InvalidPageSet,
        InvalidPagePayload,
        InvalidGeometryLayout,
        ArithmeticOverflow,
        AllocationFailure,
        UploadFailure
    };

    struct MeshGeometryUploadFailure
    {
        MeshGeometryUploadFailureCode code = MeshGeometryUploadFailureCode::None;
        u32 submesh = meshes::InvalidRecordIndex;
        u32 page = meshes::InvalidRecordIndex;
        const char* message = nullptr;
        meshes::Result meshFailure = meshes::Result::Success;
        GeometryAllocatorFailure allocatorFailure;
        GeometryUploadFailure uploadFailure;
    };

    /// Converts one complete verified .vmesh LOD page set into an atomically reserved upload plan. This main-thread
    /// step performs no bulk byte copies, I/O, submission, or publication.
    [[nodiscard]] bool PrepareMeshLodGeometryUpload(const meshes::MeshFile& mesh, u16 lod, containers::ArraySpan<const VerifiedMeshPagePayload> payloads, GeometryAllocator& allocator,
                                                    GeometryUploader& uploader, PreparedMeshLodUpload& prepared, MeshGeometryUploadFailure* failure = nullptr) noexcept;

    /// Copies final cooker-authored bytes into mapped upload memory and completes every upload ticket. This is the
    /// worker-safe bulk-data step; the caller must keep every verified page payload alive until it returns.
    [[nodiscard]] bool FillPreparedMeshLodUpload(GeometryUploader& uploader, PreparedMeshLodUpload& prepared,
                                                 MeshGeometryUploadFailure* failure = nullptr) noexcept;

    /// Cancels the open uploader batch and every still-reserved geometry range produced by Prepare.
    [[nodiscard]] bool CancelPreparedMeshLodUpload(GeometryAllocator& allocator, GeometryUploader& uploader, PreparedMeshLodUpload& prepared,
                                                   MeshGeometryUploadFailure* failure = nullptr) noexcept;
} // namespace vanguard::rendering
