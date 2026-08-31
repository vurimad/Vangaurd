#pragma once

#include <vanguard/rendering/gpu_scene_definitions.hpp>
#include <vanguard/rendering/mesh_lod_geometry_uploader.hpp>

namespace vanguard::rendering
{
    /// Owns uploaded physical geometry plus the GPU Scene geometry-definition references acquired for it.
    /// It is still pending: no primitive placement or renderable residency bit refers to these definitions.
    struct PendingMeshLodDefinitions
    {
        PendingMeshLodDefinitions() noexcept;
        ~PendingMeshLodDefinitions();

        PendingMeshLodDefinitions(const PendingMeshLodDefinitions&) = delete;
        PendingMeshLodDefinitions& operator=(const PendingMeshLodDefinitions&) = delete;
        PendingMeshLodDefinitions(PendingMeshLodDefinitions&& other) noexcept;
        PendingMeshLodDefinitions& operator=(PendingMeshLodDefinitions&& other) noexcept;

        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] bool HasOwnership() const noexcept;
        /// Clears only an empty or moved-from value. Live ownership must use AbortPendingMeshLodDefinitions.
        void Reset() noexcept;

        PendingMeshLodGeometry geometry;
        containers::DynamicArray<GpuGeometryHandle> geometryDefinitions;
        GpuSceneDefinitionPublication definitionBatch;
    };

    enum class MeshLodDefinitionFailureCode : u8
    {
        None,
        InvalidArgument,
        StaleResourceGeneration,
        InvalidGeometryMetadata,
        GeometryDefinitionFailure,
        GeometryDefinitionReleaseFailure,
        GeometryRetirementFailure
    };

    struct MeshLodDefinitionFailure
    {
        MeshLodDefinitionFailureCode code = MeshLodDefinitionFailureCode::None;
        u32 submesh = meshes::InvalidRecordIndex;
        const char* message = nullptr;
        GpuSceneDefinitionFailure definitionFailure;
        GeometryAllocatorFailure allocatorFailure;
    };

    /// Acquires immutable GPU Scene geometry definitions for every physical placement. Ownership moves
    /// from uploadedGeometry only after the complete definition batch succeeds. Failure leaves the caller's
    /// PendingMeshLodGeometry untouched so it can retry or retire all physical ranges.
    [[nodiscard]] bool PreparePendingMeshLodDefinitions(PendingMeshLodGeometry& uploadedGeometry, GpuSceneDefinitions& definitions,
                                                        PendingMeshLodDefinitions& pendingDefinitions,
                                                        MeshLodDefinitionFailure* failure = nullptr) noexcept;

    /// Releases acquired GPU Scene definitions and retires all unpublished physical geometry.
    [[nodiscard]] bool AbortPendingMeshLodDefinitions(PendingMeshLodDefinitions& pendingDefinitions, GpuSceneDefinitions& definitions,
                                                      GeometryAllocator& allocator, MeshLodDefinitionFailure* failure = nullptr) noexcept;
} // namespace vanguard::rendering
