#pragma once

#include <vanguard/rhi/rhi_types.hpp>
#include <vanguard/system/types.hpp>

namespace vanguard::meshes
{
    class MeshFile;
}

namespace vanguard::pipelines
{
    class PipelineFile;
}

namespace vanguard::shaders
{
    class ShaderFile;
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
        // Only the forward transparent entry consumes this descriptor. Opaque and
        // depth entries leave it invalid while preserving the same 24-byte ABI.
        u32 directionalLightSelectionDescriptor = 0xffffffffu;
    };
    static_assert(sizeof(StaticSurfaceDrawContext) == 24);

    // Canonical native fixed interface consumed by static_surface_prefix.vsl.
    // The returned descriptor borrows process-lifetime immutable entry storage.
    [[nodiscard]] rhi::BindingLayoutDesc GetStaticSurfaceDrawBindingLayout() noexcept;

    // Cooked-shader side of the same ABI. This validates the reflected b0
    // declaration directly instead of introducing a second material ABI owner.
    [[nodiscard]] bool ValidateStaticSurfaceShaderInterface(const shaders::ShaderFile& shader) noexcept;

    // Bounded pipeline-side identity for the counted-indirect static-surface path.
    // Reuses the existing binding-15/VG_DRAW ABI instead of inferring draw ABI from
    // a render-phase key or introducing a parallel material identity.
    [[nodiscard]] bool IsStaticSurfaceDrawPipeline(const pipelines::PipelineFile& pipeline) noexcept;

    // Resource-time, allocation-free validation. Only bounded vertex-layout
    // records are inspected; never scene objects, residency records or draws.
    // Requires both VG_DRAW fields and attribute encodings supported by the
    // static-surface prefix, including its geometry-flag direction decode.
    [[nodiscard]] bool ValidateMeshDrawLayout(const meshes::MeshFile& mesh, u32 sourceSubmesh, const pipelines::PipelineFile& pipeline) noexcept;
} // namespace vanguard::rendering
