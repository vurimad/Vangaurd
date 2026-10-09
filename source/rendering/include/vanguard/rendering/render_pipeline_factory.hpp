#pragma once

#include <vanguard/rendering/pipeline_cache.hpp>
#include <vanguard/rendering/render_shader.hpp>

namespace vanguard::rendering
{
    enum class RenderPipelineResult : u8
    {
        Success,
        InvalidArgument,
        MissingShader,
        StaleShader,
        DuplicateStage,
        UnsupportedState,
        NativeObjectFailure,
        CacheFailure
    };

    [[nodiscard]] const char* ToString(RenderPipelineResult result) noexcept;

    struct ResolvedRenderShader
    {
        resources::ResourceId resource = resources::InvalidResourceId;
        const RenderShader* shader = nullptr;
    };

    struct PipelineInterfaceResources
    {
        containers::ArraySpan<const rhi::BindingLayoutRef> bindingLayouts;
        containers::ArraySpan<const rhi::DescriptorDomainRef> descriptorDomains;
    };

    struct RenderPipelineRequest
    {
        const pipelines::PipelineFile* pipeline = nullptr;
        containers::ArraySpan<const ResolvedRenderShader> shaders;
        const pipelines::AttachmentSignature* attachments = nullptr;
        PipelineInterfaceResources interfaceResources;
        pipeline_cache::Priority priority = pipeline_cache::Priority::Normal;
        // Geometry changes winding without changing the cooked material program.
        // These graphics-only variants participate in the existing native cache key.
        bool mirrored = false;
        bool twoSided = false;
    };

    /// Pipeline and RHI format enums intentionally share stable numeric values.
    /// Keep the conversion in one rendering-layer helper so graph/cooker consumers
    /// do not grow independent format mapping tables.
    [[nodiscard]] constexpr rhi::Format ToRhiFormat(const pipelines::Format format) noexcept
    {
        static_assert(static_cast<u16>(pipelines::Format::R8UNorm) == static_cast<u16>(rhi::Format::R8UNorm));
        static_assert(static_cast<u16>(pipelines::Format::BC7UNormSrgb) == static_cast<u16>(rhi::Format::BC7UNormSrgb));
        return format < pipelines::Format::Count ? static_cast<rhi::Format>(format) : rhi::Format::Unknown;
    }

    [[nodiscard]] RenderPipelineResult RequestRenderPipeline(const RenderPipelineRequest& request, PipelineCache& cache, PipelineRequest& output) noexcept;
} // namespace vanguard::rendering
