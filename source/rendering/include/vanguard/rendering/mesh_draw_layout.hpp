#pragma once

#include <vanguard/system/types.hpp>

namespace vanguard::meshes
{
    class MeshFile;
}

namespace vanguard::pipelines
{
    class PipelineFile;
}

namespace vanguard::rendering
{
    inline constexpr u32 MeshDrawInstanceBinding = 15;

    // Written by GPU draw expansion, consumed as two R32G32UInt attributes at
    // offsets 0/8 with per-instance step rate 1. firstInstance selects the record.
    struct MeshDrawInstance
    {
        u32 instance;
        u32 primitive;
        u32 geometry;
        u32 material;
    };
    static_assert(sizeof(MeshDrawInstance) == 16);

    // Matches static_surface_prefix.vsl. Shared per view/pass, not per material.
    struct StaticSurfaceDrawContext
    {
        u32 tableDirectoryDescriptor;
        u32 pageDirectoryDescriptor;
        u32 viewDescriptor;
        u32 viewIndex;
        f32 worldCellSize;
        u32 reserved = 0;
    };
    static_assert(sizeof(StaticSurfaceDrawContext) == 24);

    // Resource-time, allocation-free validation. Only bounded vertex-layout
    // records are inspected; never scene objects, residency records or draws.
    // Requires both VG_DRAW fields and attribute encodings supported by the
    // static-surface prefix, including its geometry-flag direction decode.
    [[nodiscard]] bool ValidateMeshDrawLayout(const meshes::MeshFile& mesh, u32 sourceSubmesh, const pipelines::PipelineFile& pipeline) noexcept;
} // namespace vanguard::rendering
