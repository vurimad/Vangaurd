#include <vanguard/rendering/render_graph_nodes.hpp>

#include <vanguard/rendering/render_node_impl_context.hpp>
#include <vanguard/rendering/frame_renderer.hpp>
#include <vanguard/rhi/rhi.hpp>

#include <vanguard/system/assert.hpp>

namespace vanguard::rendering
{
    RenderNodeFullscreenCopy::RenderNodeFullscreenCopy(const FrameRenderer& renderer, const FullscreenCopyDesc& desc) noexcept
        : m_pipeline(renderer.GetPipeline("FullscreenCopy")), m_source(desc.source)
    {
        if (desc.sourceName.Empty() || desc.targetName.Empty() || desc.sourceName == desc.targetName || !desc.source.IsValid() || !desc.sourceDescriptor.IsValid() || !desc.samplerDescriptor.IsValid())
            VG_FATAL("fullscreen copy requires distinct resource names and valid imported source descriptors");
        m_sourceName.Set(desc.sourceName);
        rhi::TextureDesc sourceDesc;
        if (!rhi::GetTextureDesc(m_source, sourceDesc) || sourceDesc.dimension != rhi::TextureDimension::Texture2D || sourceDesc.arraySize != 1 || sourceDesc.sampleCount != 1)
            VG_FATAL("fullscreen copy requires a single-sample non-array 2D source");
        m_targetName.Set(desc.targetName);
        m_constants = {desc.sourceDescriptor.GpuIndex(), desc.samplerDescriptor.GpuIndex(), {desc.uvScale[0], desc.uvScale[1]}, {desc.uvOffset[0], desc.uvOffset[1]}};
    }

    bool RenderNodeFullscreenCopy::DeclareResources(const RenderNodeImplContext& context, RenderFlowResourceFailure*) const noexcept
    {
        TextureUseDesc source;
        source.subresources = {0, 1, 0, 1};
        TextureUseDesc target;
        target.requiredState = rhi::ResourceState::RenderTarget;
        target.access = LogicalAccessIntent::Write;
        target.content = ResourceContentIntent::Discard;
        target.subresources = {0, 1, 0, 1};
        context.RTUseBegin(m_sourceName, source);
        context.RTUseBegin(m_targetName, target);
        context.RTUseEnd(m_targetName);
        context.RTUseEnd(m_sourceName);
        return true;
    }

    void RenderNodeFullscreenCopy::Execute(const RenderNodeImplContext& context, jobs::Builder*) const
    {
        const rhi::TextureRef source = context.RTTexture(m_sourceName).GetTexture();
        const rhi::TextureRef target = context.RTTexture(m_targetName).GetTexture();
        if (source != m_source || source == target || !target.IsValid())
            VG_FATAL("fullscreen copy source binding changed or aliases its target");
        rhi::Failure failure;
        rhi::TextureDesc targetDesc;
        if (!rhi::GetTextureDesc(target, targetDesc, &failure))
            VG_FATAL("fullscreen copy target description is unavailable");
        if (targetDesc.dimension != rhi::TextureDimension::Texture2D || targetDesc.arraySize != 1 || targetDesc.sampleCount != 1)
            VG_FATAL("fullscreen copy requires a single-sample non-array 2D target");
        const rhi::RenderTargetSetup targets{{{target, rhi::Format::Unknown, 0, 0, false}}, 1, {}};
        if (!rhi::SetPipeline(m_pipeline, &failure) || !rhi::SetupRenderTargets(targets, &failure) ||
            !rhi::SetViewport({0, 0, static_cast<f32>(targetDesc.extent.width), static_cast<f32>(targetDesc.extent.height), 0, 1}, &failure) ||
            !rhi::SetScissors({0, 0, static_cast<i32>(targetDesc.extent.width), static_cast<i32>(targetDesc.extent.height)}, &failure) ||
            !rhi::SetPushConstants(&m_constants, sizeof(m_constants), &failure) || !rhi::DrawPrimitive({3, 1, 0, 0}, &failure))
            VG_FATAL("fullscreen copy recording failed");
    }

    void RenderNodeStartRender::Execute(const RenderNodeImplContext&, jobs::Builder*) const {}
    bool RenderNodeEndRender::DeclareResources(const RenderNodeImplContext& context, RenderFlowResourceFailure*) const noexcept
    {
        context.RTUsePresentationOutput();
        return true;
    }
    void RenderNodeEndRender::Execute(const RenderNodeImplContext& context, jobs::Builder*) const
    {
        if (context.HasPresentationOutput())
            static_cast<void>(context.RTPresentationOutput());
    }
    void RenderNodePresent::Execute(const RenderNodeImplContext& context, jobs::Builder*) const { context.MarkPresentNodeReached(); }
    void RenderNodeFlushTextureGrabs::Execute(const RenderNodeImplContext&, jobs::Builder*) const {}
    void RenderNodeFlushBufferGrabs::Execute(const RenderNodeImplContext&, jobs::Builder*) const {}
    void RenderNodeCleanupBatchDataAllocator::Execute(const RenderNodeImplContext&, jobs::Builder*) const {}
    void RenderNodeEndFrame::Execute(const RenderNodeImplContext&, jobs::Builder*) const {}
    bool RenderNodeDeclareCommonResourceAllocsFinalOnly::DeclareResources(const RenderNodeImplContext& context, RenderFlowResourceFailure*) const noexcept
    {
        context.RTImportPresentationOutput();
        return true;
    }
    void RenderNodeDeclareCommonResourceAllocsFinalOnly::Execute(const RenderNodeImplContext&, jobs::Builder*) const {}
    void RenderNodePrepareBlankRendering::Execute(const RenderNodeImplContext&, jobs::Builder*) const {}
    void RenderNodeRenderSkyScattering::Execute(const RenderNodeImplContext&, jobs::Builder*) const {}
    void RenderNodeReflectionProbes::Execute(const RenderNodeImplContext&, jobs::Builder*) const {}
    void RenderNodeGlobalIllumination::Execute(const RenderNodeImplContext&, jobs::Builder*) const {}
    void RenderNodeVolumetricFog::Execute(const RenderNodeImplContext&, jobs::Builder*) const {}
    void RenderNodeClearFinalColorTarget::Execute(const RenderNodeImplContext&, jobs::Builder*) const {}
    void RenderNodeDrawComposition::Execute(const RenderNodeImplContext&, jobs::Builder*) const {}
    void RenderNodeCompositionPostProcess::Execute(const RenderNodeImplContext&, jobs::Builder*) const {}
    void RenderNodeDrawHud::Execute(const RenderNodeImplContext&, jobs::Builder*) const {}
    void RenderNodeFullscreenVideo::Execute(const RenderNodeImplContext&, jobs::Builder*) const {}
    void RenderNodeRenderFinal2D::Execute(const RenderNodeImplContext&, jobs::Builder*) const {}
    void RenderNodeExtractionFinalColor::Execute(const RenderNodeImplContext&, jobs::Builder*) const {}

    RenderNodeSynchronize::RenderNodeSynchronize(const rhi::CommandListSyncType sync, const char* const name)
        : m_sync(sync), m_name(memory::pools::Rendering::GetInstance())
    {
        if (name != nullptr)
            m_name = name;
    }

    bool RenderNodeSynchronize::DeclareResources(const RenderNodeImplContext& context, RenderFlowResourceFailure* const) const noexcept
    {
        return context.SyncResourceQueue(m_sync);
    }

    void RenderNodeSynchronize::Execute(const RenderNodeImplContext& context, jobs::Builder* const builder) const
    {
        if (builder == nullptr)
            VG_FATAL("render-node queue synchronization requires a Jobs builder");
        if (m_name.Empty() || !context.SubmitCommandLists(m_name.AsChar(), m_sync, *builder))
        {
            RenderNodeImplContext& executionContext = const_cast<RenderNodeImplContext&>(context);
            executionContext.FailResourceOperation(RenderFlowResourceFailureCode::CapacityExceeded, "render-node command-list submission dispatch failed");
        }
    }
} // namespace vanguard::rendering
