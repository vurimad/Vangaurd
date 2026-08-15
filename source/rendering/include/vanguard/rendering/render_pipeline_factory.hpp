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
    };

    [[nodiscard]] RenderPipelineResult RequestRenderPipeline(const RenderPipelineRequest& request, PipelineCache& cache,
                                                             PipelineRequest& output) noexcept;
} // namespace vanguard::rendering
