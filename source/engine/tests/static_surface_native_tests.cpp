#include "static_surface_proof.hpp"

#include <vanguard/rendering/mesh_draw_layout.hpp>
#include <vanguard/rendering/render_pipeline_factory.hpp>

#include <cstdio>

namespace vanguard::engine::tests
{
    bool RunStaticSurfaceNativeProof()
    {
        const u32 stages = rhi::ShaderStageBit(rhi::ShaderStage::Vertex) | rhi::ShaderStageBit(rhi::ShaderStage::Pixel);
        rhi::DescriptorDomain resources(rhi::AdoptReference, rhi::CreateDescriptorDomain({rhi::DescriptorDomainKind::Resources, 16, 0, stages}));
        rhi::DescriptorDomain samplers(rhi::AdoptReference, rhi::CreateDescriptorDomain({rhi::DescriptorDomainKind::Samplers, 16, 0, stages}));
        const rhi::BindingLayoutEntry constants{0, sizeof(rendering::StaticSurfaceDrawContext), rhi::BindingType::PushConstants};
        rhi::BindingLayout layout(rhi::AdoptReference, rhi::RequestBindingLayout({&constants, 1, 0, stages}));
        struct Context
        {
            rendering::PipelineCache cache;
            rendering::PipelineInterfaceResources interfaces;
            u32 proofs = 0;
        } context;
        const rhi::BindingLayoutRef layoutRef = layout.GetRef();
        const rhi::DescriptorDomainRef domains[]{resources.GetRef(), samplers.GetRef()};
        context.interfaces = {{&layoutRef, 1}, domains};
        if (!resources || !samplers || !layout || !context.cache.Initialize())
            return false;
        const bool passed = material_tools::tests::RunStaticSurfaceProof(
            filesystem::paths::GetCurrentWorkingDirectory(),
            [](const shaders::ShaderFile& shader, const pipelines::PipelineFile& pipeline, void* data)
            {
                auto& context = *static_cast<Context*>(data);
                rendering::RenderShader nativeShader;
                if (nativeShader.Load(shader) != rendering::RenderShaderResult::Success)
                    return false;
                const rendering::ResolvedRenderShader resolved{1, &nativeShader};
                rendering::RenderPipelineRequest request;
                request.pipeline = &pipeline;
                request.shaders = {&resolved, 1};
                request.interfaceResources = context.interfaces;
                for (u32 variant = 0; variant < 3; ++variant)
                {
                    request.mirrored = variant == 1;
                    request.twoSided = variant == 2;
                    rendering::PipelineRequest prepared;
                    const auto result = rendering::RequestRenderPipeline(request, context.cache, prepared);
                    if (result != rendering::RenderPipelineResult::Success)
                    {
                        std::fprintf(stderr, "[staticSurfaceNative] request: %s\n", rendering::ToString(result));
                        return false;
                    }
                    prepared.Wait();
                    if (!prepared.HasSucceeded())
                    {
                        std::fprintf(stderr, "[staticSurfaceNative] native creation: %s\n", prepared.GetError().message);
                        return false;
                    }
                    ++context.proofs;
                }
                return true;
            },
            &context);
        context.cache.WaitIdle();
        const bool closed = context.cache.Shutdown();
        return passed && context.proofs == 24 && closed;
    }
} // namespace vanguard::engine::tests
