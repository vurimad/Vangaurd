#include <vanguard/rendering/geometry_graph_nodes.hpp>
#include <vanguard/rendering/frame_renderer.hpp>
#include <vanguard/rendering/render_node_impl_context.hpp>
#include <vanguard/rendering/renderer_draw_contract.hpp>
#include <vanguard/concurrency/atomic.hpp>
#include <vanguard/profiling/profiling.hpp>
#include <vanguard/system/assert.hpp>
#include <vanguard/system/time.hpp>

namespace vanguard::rendering
{
    namespace
    {
        enum Resource : u32
        {
            Views, VisibilityResults, VisibilityCounters, Visible,
            Work, Results, Counters, Bins, ActiveBins, Instances, TransparentInstances, Arguments,
            ShellPlans, ShellRanges, ShellCounters, DirectionalLights, ResourceCount
        };
        constexpr u32 Bit(Resource r) noexcept { return 1u << r; }
        constexpr u32 VisibilityInput = Bit(Views) | Bit(Visible) | Bit(VisibilityResults) | Bit(VisibilityCounters);
        struct Stage
        {
            const char* shader;
            u32 reads;
            u32 writes;
            u32 discard;
        };
        constexpr Stage Stages[] = {
            {"InitializeGpuSceneGeometryShellRanges", Bit(ShellPlans), Bit(ShellRanges)|Bit(ShellCounters)|Bit(Counters), Bit(ShellCounters)|Bit(Counters)},
            {"CullGpuSceneInstances", Bit(Views)|Bit(VisibilityResults), Bit(Visible)|Bit(VisibilityCounters), Bit(Visible)},
            {"ExpandAppendGpuSceneGeometry", VisibilityInput|Bit(Results), Bit(Work)|Bit(Counters)|Bit(Bins)|Bit(ActiveBins), Bit(Work)|Bit(ActiveBins)},
            {"AllocateGpuSceneGeometryBins", Bit(Results)|Bit(ActiveBins)|Bit(ShellRanges), Bit(Bins)|Bit(Counters)|Bit(ShellCounters)|Bit(Arguments), Bit(Arguments)},
            {"ScatterGpuSceneGeometryInstances", Bit(Work)|Bit(Results)|Bit(Counters)|Bit(Bins), Bit(Instances), Bit(Instances)}
        };
        static_assert(sizeof(Stages) / sizeof(Stages[0]) == static_cast<u32>(GeometryComputeStage::Count));
        concurrency::Atomic<u64> g_geometryInitializeTicks;
        concurrency::Atomic<u64> g_geometryInitializeUploadTicks;
        concurrency::Atomic<u64> g_geometryInitializeClearTicks;
        concurrency::Atomic<u32> g_geometryInitializeCalls;
        concurrency::Atomic<u64> g_geometryStageTicks[static_cast<u32>(GeometryComputeStage::Count)];
        concurrency::Atomic<u32> g_geometryStageCalls[static_cast<u32>(GeometryComputeStage::Count)];
        constexpr LogicalResourceName Names[] = {
            VG_RENDER_FLOW_NAME("Geometry.Views"), VG_RENDER_FLOW_NAME("Geometry.VisibilityResults"),
            VG_RENDER_FLOW_NAME("Geometry.VisibilityCounters"), VG_RENDER_FLOW_NAME("Geometry.Visible"),
            VG_RENDER_FLOW_NAME("Geometry.Work"), VG_RENDER_FLOW_NAME("Geometry.Results"), VG_RENDER_FLOW_NAME("Geometry.Counters"),
            VG_RENDER_FLOW_NAME("Geometry.Bins"), VG_RENDER_FLOW_NAME("Geometry.ActiveBins"),
            VG_RENDER_FLOW_NAME("Geometry.Instances"), VG_RENDER_FLOW_NAME("Geometry.TransparentInstances"),
            VG_RENDER_FLOW_NAME("Geometry.Arguments"), VG_RENDER_FLOW_NAME("Geometry.ShellPlans"),
            VG_RENDER_FLOW_NAME("Geometry.ShellRanges"), VG_RENDER_FLOW_NAME("Geometry.ShellCounters"), VG_RENDER_FLOW_NAME("Lighting.DirectionalSelections")
        };
        static_assert(sizeof(Names) / sizeof(Names[0]) == ResourceCount);
        constexpr LogicalResourceName CameraColorName = VG_RENDER_FLOW_NAME("CameraColor");
        constexpr LogicalResourceName DepthName = VG_RENDER_FLOW_NAME("Depth");
        constexpr LogicalResourceName FrameOutputName = VG_RENDER_FLOW_NAME("FrameOutput");
        constexpr LogicalResourceName DiagnosticSummaryName = VG_RENDER_FLOW_NAME("Geometry.DiagnosticSummary");
        constexpr LogicalResourceName DiagnosticReadbackName = VG_RENDER_FLOW_NAME("Geometry.DiagnosticReadback");
        constexpr LogicalResourceId GeometryFrameGraphResources::* ResourceIds[] = {
            &GeometryFrameGraphResources::views, &GeometryFrameGraphResources::resultRanges,
            &GeometryFrameGraphResources::counters, &GeometryFrameGraphResources::visibleInstances,
            &GeometryFrameGraphResources::geometryWork, &GeometryFrameGraphResources::geometryResults,
            &GeometryFrameGraphResources::geometryCounters, &GeometryFrameGraphResources::binItems,
            &GeometryFrameGraphResources::activeBins, &GeometryFrameGraphResources::drawInstances,
            &GeometryFrameGraphResources::transparentInstances, &GeometryFrameGraphResources::indirectArguments, &GeometryFrameGraphResources::shellPlans,
            &GeometryFrameGraphResources::shellRanges, &GeometryFrameGraphResources::shellCounters, &GeometryFrameGraphResources::directionalLights
        };
        static_assert(sizeof(ResourceIds) / sizeof(ResourceIds[0]) == ResourceCount);
        struct BufferInfo { u32 count; u32 stride; const void* data = nullptr; };
        template<typename T> BufferInfo Upload(containers::ArraySpan<const T> values) noexcept
        { return {values.Size(), sizeof(T), values.Data()}; }
        BufferInfo Describe(const GeometryFrameWork& work, Resource resource) noexcept
        {
            const auto capacity = work.GetCapacity();
            const auto& plan = work.GetVisibilityPlan();
            switch (resource)
            {
            case Views: return Upload(plan.views);
            case VisibilityResults: return Upload(plan.results);
            case VisibilityCounters: return {capacity.views, sizeof(GpuVisibilityCounters)};
            case Visible: return {capacity.visibleInstances, sizeof(GpuVisibleInstance)};
            case Work: return {capacity.geometryWorkItems, sizeof(GpuGeometryWork)};
            case Results: return Upload(work.GetGeometryResults());
            case Counters: return {capacity.views, sizeof(GpuGeometryCounters)};
            case Bins: return {capacity.geometryBins, sizeof(GpuGeometryBinItem)};
            case ActiveBins: return {capacity.geometryBins, sizeof(u32)};
            case Instances: return {capacity.geometryWorkItems, sizeof(MeshDrawInstance)};
            case TransparentInstances: return Upload(work.GetAllTransparentInstances());
            case Arguments: return {work.GetIndirectArgumentCapacity(), sizeof(GpuGeometryIndirectArguments)};
            case ShellPlans: return Upload(work.GetShellDraws());
            case ShellRanges: return {capacity.views * capacity.shellsPerView, sizeof(GpuGeometryShellRange)};
            case ShellCounters: return {work.GetShellDraws().Size(), sizeof(GpuGeometryShellCounters)};
            case DirectionalLights: return Upload(work.GetDirectionalLights());
            default: VG_FATAL("unknown geometry graph resource");
            }
        }
        constexpr u32 UploadMask = Bit(Views)|Bit(VisibilityResults)|Bit(Results)|Bit(TransparentInstances)|Bit(DirectionalLights);
        constexpr u32 ClearMask = Bit(VisibilityCounters)|Bit(Bins)|Bit(ShellRanges);
        RenderFlowNameTag Tag(Resource resource) noexcept { return RenderNodeImplContext::RTSharedNameTag(Names[resource]); }
        void Use(const RenderNodeImplContext& context, Resource resource, rhi::ResourceState state,
                 LogicalAccessIntent access = LogicalAccessIntent::Read, ResourceContentIntent content = ResourceContentIntent::Preserve) noexcept
        {
            context.RTUseBegin(Tag(resource), BufferUseDesc{state, access, content});
            context.RTUseEnd(Tag(resource));
        }
        const GeometryFrameWork& WorkFor(const RenderNodeImplContext& context)
        {
            const auto* work = context.GetGeometryFrameWork();
            if (work == nullptr || !work->GetVisibilityPlan().IsValid())
                VG_FATAL("geometry graph requires a prepared view-family plan");
            return *work;
        }
        constexpr LogicalResourceName Colors[] = {VG_RENDER_FLOW_NAME("GBuffer.BaseColor"), VG_RENDER_FLOW_NAME("GBuffer.NormalRoughness"),
                                                  VG_RENDER_FLOW_NAME("GBuffer.EmissiveMetallic"), VG_RENDER_FLOW_NAME("GBuffer.SpecularColor"),
                                                  VG_RENDER_FLOW_NAME("GBuffer.ViewDirection")};
        constexpr pipelines::AttachmentSignature DepthPrepassAttachments = MakeStandardDepthPrepassAttachments();
        constexpr pipelines::AttachmentSignature OpaqueAttachments = MakeStandardOpaqueAttachments();
        constexpr pipelines::AttachmentSignature TransparentAttachments = MakeStandardTransparentAttachments();
        static_assert(OpaqueAttachments.colorCount == sizeof(Colors) / sizeof(Colors[0]));
        static_assert(DepthPrepassAttachments.depthStencilFormat == OpaqueAttachments.depthStencilFormat);
        static_assert(DepthPrepassAttachments.depthStencilClass == OpaqueAttachments.depthStencilClass);
        static_assert(DepthPrepassAttachments.sampleCount == OpaqueAttachments.sampleCount);
        static_assert(TransparentAttachments.colorCount == 1u);
        static_assert(TransparentAttachments.depthStencilFormat == OpaqueAttachments.depthStencilFormat);
        static_assert(TransparentAttachments.sampleCount == OpaqueAttachments.sampleCount);
        void TextureUse(const RenderNodeImplContext& context, const LogicalResourceName name, rhi::ResourceState state, LogicalAccessIntent access,
                        ResourceContentIntent content = ResourceContentIntent::Preserve) noexcept
        {
            context.RTUseBegin(name, TextureUseDesc{state, {0,1,0,1}, access, content});
            context.RTUseEnd(name);
        }
        void SetupCamera(const RenderNodeImplContext& context, const rhi::RenderTargetSetup& targets)
        {
            VG_PROFILE_ZONE_NAMED("CameraGeometry/SetupCamera");
            const auto* view = context.GetView();
            if (view == nullptr)
                VG_FATAL("camera view is unavailable");
            const bool targetsSet = rhi::SetupRenderTargets(targets);
            if (!targetsSet)
                VG_FATAL("camera target setup failed");
            const bool viewportSet = rhi::SetViewport({0, 0, static_cast<f32>(view->rect.width), static_cast<f32>(view->rect.height), 0, 1});
            if (!viewportSet)
                VG_FATAL("camera viewport setup failed");
            const bool scissorsSet = rhi::SetScissors({0,0,static_cast<i32>(view->rect.width),static_cast<i32>(view->rect.height)});
            if (!scissorsSet)
                VG_FATAL("camera scissors setup failed");
        }
    }

    GeometryCpuTimingSample ConsumeGeometryCpuTimingSample() noexcept
    {
        GeometryCpuTimingSample sample;
        sample.initializeTicks = g_geometryInitializeTicks.Exchange(0);
        sample.initializeUploadTicks = g_geometryInitializeUploadTicks.Exchange(0);
        sample.initializeClearTicks = g_geometryInitializeClearTicks.Exchange(0);
        sample.initializeCalls = g_geometryInitializeCalls.Exchange(0);
        for (u32 stage = 0; stage < static_cast<u32>(GeometryComputeStage::Count); ++stage)
        {
            sample.stageTicks[stage] = g_geometryStageTicks[stage].Exchange(0);
            sample.stageCalls[stage] = g_geometryStageCalls[stage].Exchange(0);
        }
        return sample;
    }


    bool AreGeometryGraphPipelinesReady(const FrameRenderer& renderer, const bool gBufferOnly) noexcept
    {
        for (const auto& stage : Stages)
            if (!renderer.FindPipeline(stage.shader).IsValid()) return false;
        return renderer.FindPipeline("ResolveCameraOutput").IsValid() &&
               renderer.FindPipeline("ResolveCameraOutputDisplaySrgb").IsValid() &&
               renderer.FindPipeline(gBufferOnly ? "VisualizeGBuffer" : "DirectionalDiffuse").IsValid();
    }

    bool RenderNodeBeginCameraDependencies::DeclareResources(const RenderNodeImplContext& context, RenderFlowResourceFailure*) const noexcept
    {
        const u32 inputs = WorkFor(context).GetCameraInputMask(static_cast<u32>(context.GetCameraIndex()));
        for (u32 producer = 0; producer < MaximumRenderViewsPerFamily; ++producer)
        {
            if ((inputs & (u32{1} << producer)) == 0) continue;
            context.RTUseBegin(context.RTCameraNameTag(producer, CameraColorName),
                               TextureUseDesc{rhi::ResourceState::ShaderResourceGraphics, {0,1,0,1}, LogicalAccessIntent::Read});
        }
        return true;
    }

    bool RenderNodeEndCameraDependencies::DeclareResources(const RenderNodeImplContext& context, RenderFlowResourceFailure*) const noexcept
    {
        const u32 inputs = WorkFor(context).GetCameraInputMask(static_cast<u32>(context.GetCameraIndex()));
        for (u32 producer = 0; producer < MaximumRenderViewsPerFamily; ++producer)
        {
            if ((inputs & (u32{1} << producer)) == 0) continue;
            context.RTUseEnd(context.RTCameraNameTag(producer, CameraColorName));
        }
        return true;
    }

    void RenderNodeBeginCameraDependencies::Execute(const RenderNodeImplContext& context, jobs::Builder*) const
    {
        const u32 inputs = WorkFor(context).GetCameraInputMask(static_cast<u32>(context.GetCameraIndex()));
        for (u32 producer = 0; producer < MaximumRenderViewsPerFamily; ++producer)
        {
            if ((inputs & (u32{1} << producer)) == 0) continue;
            static_cast<void>(context.RTTexture(context.RTCameraNameTag(producer, CameraColorName)));
        }
    }

    bool RenderNodeDeclareCommonResourceAllocs::DeclareResources(const RenderNodeImplContext& context, RenderFlowResourceFailure*) const noexcept
    {
        const auto* view = context.GetView();
        if (view == nullptr) return false;
        FrameTextureDesc desc;
        desc.active.extent = {view->rect.width, view->rect.height, 1};
        desc.maximumExtent = desc.active.extent;
        desc.maximumMipCount = desc.active.mipCount;
        desc.active.keepInitialState = false;
        desc.active.sampleCount = OpaqueAttachments.sampleCount;
        desc.active.usage = rhi::TextureUsage::RenderTarget | rhi::TextureUsage::ShaderResource;
        for (u32 index = 0; index < OpaqueAttachments.colorCount; ++index)
        {
            desc.active.format = ToRhiFormat(OpaqueAttachments.colors[index].format);
            static_cast<void>(context.RTAlloc(Colors[index], desc));
        }
        // Keep the simple diffuse result linear/HDR; output conversion remains
        // the explicit resolve pass. No exposure or tone-mapping policy here.
        desc.active.format = rhi::Format::R16G16B16A16Float;
        static_cast<void>(context.RTAlloc(CameraColorName, desc));
        desc.active.format = ToRhiFormat(DepthPrepassAttachments.depthStencilFormat);
        desc.active.usage = rhi::TextureUsage::DepthStencil;
        static_cast<void>(context.RTAlloc(DepthName, desc));
        return true;
    }

    bool RenderNodeClearCameraTargets::DeclareResources(const RenderNodeImplContext& context, RenderFlowResourceFailure*) const noexcept
    {
        TextureUse(context, DepthName, rhi::ResourceState::DepthWrite, LogicalAccessIntent::Write, ResourceContentIntent::Discard);
        for (const LogicalResourceName name : Colors)
            TextureUse(context, name, rhi::ResourceState::RenderTarget, LogicalAccessIntent::Write, ResourceContentIntent::Discard);
        return true;
    }

    void RenderNodeClearCameraTargets::Execute(const RenderNodeImplContext& context, jobs::Builder*) const
    {
        const auto* view = context.GetView();
        const f32 clearDepth = view != nullptr && (view->flags & RenderViewFlags::ReverseDepth) != RenderViewFlags::None ? 0.0f : 1.0f;
        const bool depthCleared = rhi::ClearDepthTarget(context.RTTexture(DepthName).GetTexture(), clearDepth);
        if (!depthCleared)
            VG_FATAL("camera depth clear failed");
        for (const LogicalResourceName name : Colors)
        {
            const bool colorCleared = rhi::ClearColorTarget(context.RTTexture(name).GetTexture(), {});
            if (!colorCleared)
                VG_FATAL("camera GBuffer clear failed");
        }
    }

    namespace
    {
        bool DeclareGeometryDrawInputs(const FrameRenderer& renderer, const RenderNodeImplContext& context,
                                       RenderFlowResourceFailure* failure) noexcept
        {
            const bool sceneUsesDeclared = renderer.DeclareGeometrySceneUses(context, failure);
            if (!sceneUsesDeclared)
                return false;
            const bool arenaUsesDeclared = renderer.DeclareGeometryArenaUses(context, failure);
            if (!arenaUsesDeclared)
                return false;
            Use(context, Views, rhi::ResourceState::ShaderResourceGraphics);
            Use(context, Instances, rhi::ResourceState::VertexBuffer);
            Use(context, Arguments, rhi::ResourceState::IndirectArgument);
            Use(context, ShellCounters, rhi::ResourceState::IndirectArgument);
            return true;
        }

        bool DeclareGeometryDirectDrawInputs(const FrameRenderer& renderer, const RenderNodeImplContext& context,
                                             RenderFlowResourceFailure* failure) noexcept
        {
            if (!renderer.DeclareGeometrySceneUses(context, failure) || !renderer.DeclareGeometryArenaUses(context, failure))
                return false;
            Use(context, Views, rhi::ResourceState::ShaderResourceGraphics);
            return true;
        }

        void RecordGeometryDraw(const FrameRenderer& renderer, const RenderNodeImplContext& context, RenderPhaseId phase)
        {
            VG_PROFILE_ZONE_NAMED("CameraGeometry/RecordGeometryDraw");
            const auto& work = WorkFor(context);
            const auto directory = renderer.GetGeometryDirectoryBinding();
            const auto viewDescriptor = context.RTBuffer(Tag(Views)).GetShaderResourceDescriptor();
            if (!viewDescriptor.IsValid() || context.GetCameraIndex() < 0)
                VG_FATAL("geometry graphics view binding is unavailable");
            const StaticSurfaceDrawContext draw{directory.tableDirectoryDescriptor, directory.pageDirectoryDescriptor,
                                                viewDescriptor.GpuIndex(), static_cast<u32>(context.GetCameraIndex()),
                                                renderer.GetGeometryWorldCellSize()};
            const GeometryPhaseDrawBuffers buffers{context.RTBuffer(Tag(Instances)).GetBuffer(),
                                                    context.RTBuffer(Tag(Arguments)).GetBuffer(),
                                                    context.RTBuffer(Tag(ShellCounters)).GetBuffer()};
            rhi::Failure failure;
            const bool phaseRecorded = renderer.RecordGeometryPhase(work, phase, draw, buffers, &failure);
            if (!phaseRecorded)
                VG_FATAL("counted geometry phase recording failed");
        }
    }

    RenderNodeDrawOpaqueDepth::RenderNodeDrawOpaqueDepth(const FrameRenderer& renderer) noexcept
        : m_renderer(renderer), m_phase(renderer.GetGeometryPhase(standardRenderPhases::DepthPrepass)) {}

    bool RenderNodeDrawOpaqueDepth::DeclareResources(const RenderNodeImplContext& context, RenderFlowResourceFailure* failure) const noexcept
    {
        if (context.GetView() == nullptr) return false;
        if (!context.GetView()->phases.Contains(m_phase)) return true;
        const bool inputsDeclared = DeclareGeometryDrawInputs(m_renderer, context, failure);
        if (!inputsDeclared) return false;
        TextureUse(context, DepthName, rhi::ResourceState::DepthWrite, LogicalAccessIntent::Write);
        return true;
    }

    void RenderNodeDrawOpaqueDepth::Execute(const RenderNodeImplContext& context, jobs::Builder*) const
    {
        if (context.GetView() == nullptr || !context.GetView()->phases.Contains(m_phase)) return;
        rhi::RenderTargetSetup targets;
        targets.depthStencilTarget = {context.RTTexture(DepthName).GetTexture(), ToRhiFormat(DepthPrepassAttachments.depthStencilFormat)};
        SetupCamera(context, targets);
        RecordGeometryDraw(m_renderer, context, m_phase);
    }

    RenderNodeDrawOpaqueGBuffer::RenderNodeDrawOpaqueGBuffer(const FrameRenderer& renderer) noexcept
        // Opaque and masked materials share this unordered, depth-writing phase.
        // Their cooked techniques select different pixel entries/PSOs.
        : m_renderer(renderer), m_phase(renderer.GetGeometryPhase(standardRenderPhases::Opaque)) {}

    bool RenderNodeDrawOpaqueGBuffer::DeclareResources(const RenderNodeImplContext& context, RenderFlowResourceFailure* failure) const noexcept
    {
        if (context.GetView() == nullptr) return false;
        if (!context.GetView()->phases.Contains(m_phase)) return true;
        const bool inputsDeclared = DeclareGeometryDrawInputs(m_renderer, context, failure);
        if (!inputsDeclared) return false;
        // Opaque remains depth-writable: cameras may legitimately omit a depth
        // prepass, and the cooked shell PSO defines its exact depth behavior.
        TextureUse(context, DepthName, rhi::ResourceState::DepthWrite, LogicalAccessIntent::Write);
        for (const LogicalResourceName name : Colors)
            TextureUse(context, name, rhi::ResourceState::RenderTarget, LogicalAccessIntent::Write);
        return true;
    }

    void RenderNodeDrawOpaqueGBuffer::Execute(const RenderNodeImplContext& context, jobs::Builder*) const
    {
        if (context.GetView() == nullptr || !context.GetView()->phases.Contains(m_phase)) return;
        rhi::RenderTargetSetup targets;
        targets.depthStencilTarget = {context.RTTexture(DepthName).GetTexture(), ToRhiFormat(OpaqueAttachments.depthStencilFormat)};
        targets.colorTargetCount = OpaqueAttachments.colorCount;
        for (u32 index = 0; index < OpaqueAttachments.colorCount; ++index)
            targets.colorTargets[index] = {context.RTTexture(Colors[index]).GetTexture(), ToRhiFormat(OpaqueAttachments.colors[index].format)};
        SetupCamera(context, targets);
        RecordGeometryDraw(m_renderer, context, m_phase);
    }

    RenderNodeDirectionalDiffuse::RenderNodeDirectionalDiffuse(const FrameRenderer& renderer) noexcept
        : m_renderer(renderer), m_pipeline(renderer.FindPipeline("DirectionalDiffuse")) {}

    bool RenderNodeDirectionalDiffuse::DeclareResources(const RenderNodeImplContext& context, RenderFlowResourceFailure* failure) const noexcept
    {
        if (!m_pipeline.IsValid() || context.GetView() == nullptr)
            return false;
        const bool sceneDeclared = m_renderer.DeclareGeometrySceneUses(context, failure);
        if (!sceneDeclared)
            return false;
        Use(context, DirectionalLights, rhi::ResourceState::ShaderResourceGraphics);
        for (const LogicalResourceName name : Colors)
            TextureUse(context, name, rhi::ResourceState::ShaderResourceGraphics, LogicalAccessIntent::Read);
        TextureUse(context, CameraColorName, rhi::ResourceState::RenderTarget, LogicalAccessIntent::Write, ResourceContentIntent::Discard);
        return true;
    }

    void RenderNodeDirectionalDiffuse::Execute(const RenderNodeImplContext& context, jobs::Builder*) const
    {
        const auto& work = WorkFor(context);
        const u32 viewIndex = static_cast<u32>(context.GetCameraIndex());
        const auto* view = context.GetView();
        if (view == nullptr || viewIndex >= work.GetDirectionalLights().Size())
            VG_FATAL("directional lighting requires its prepared view selection");
        const auto directory = m_renderer.GetGeometryDirectoryBinding();
        const auto selections = context.RTBuffer(Tag(DirectionalLights)).GetShaderResourceDescriptor();
        const auto baseColor = context.RTTexture(Colors[0]).GetShaderResourceDescriptor();
        const auto normal = context.RTTexture(Colors[1]).GetShaderResourceDescriptor();
        const auto emissiveMetallic = context.RTTexture(Colors[2]).GetShaderResourceDescriptor();
        const auto specularColor = context.RTTexture(Colors[3]).GetShaderResourceDescriptor();
        const auto viewDirection = context.RTTexture(Colors[4]).GetShaderResourceDescriptor();
        if (!selections.IsValid() || !baseColor.IsValid() || !normal.IsValid() || !emissiveMetallic.IsValid() ||
            !specularColor.IsValid() || !viewDirection.IsValid())
            VG_FATAL("directional lighting input descriptors are unavailable");
        const u32 constants[]{directory.tableDirectoryDescriptor, directory.pageDirectoryDescriptor,
                              selections.GpuIndex(), viewIndex, baseColor.GpuIndex(), normal.GpuIndex(),
                              emissiveMetallic.GpuIndex(), specularColor.GpuIndex(), viewDirection.GpuIndex(),
                              view->visibilityMask, 0u, 0u};
        static_assert(sizeof(constants) == 48);
        rhi::RenderTargetSetup targets;
        targets.colorTargetCount = 1;
        targets.colorTargets[0] = {context.RTTexture(CameraColorName).GetTexture(), rhi::Format::R16G16B16A16Float};
        const bool pipelineSet = rhi::SetPipeline(m_pipeline);
        if (!pipelineSet)
            VG_FATAL("directional diffuse pipeline setup failed");
        SetupCamera(context, targets);
        const bool constantsSet = rhi::SetPushConstants(constants, sizeof(constants));
        if (!constantsSet)
            VG_FATAL("directional diffuse constants setup failed");
        const bool drawn = rhi::DrawPrimitive({3, 1, 0, 0});
        if (!drawn)
            VG_FATAL("directional diffuse fullscreen recording failed");
    }

    RenderNodeDrawTransparent::RenderNodeDrawTransparent(const FrameRenderer& renderer) noexcept
        : m_renderer(renderer), m_phase(renderer.GetGeometryPhase(standardRenderPhases::Transparent)) {}

    bool RenderNodeDrawTransparent::DeclareResources(const RenderNodeImplContext& context, RenderFlowResourceFailure* failure) const noexcept
    {
        const auto* view = context.GetView();
        if (view == nullptr || context.GetCameraIndex() < 0)
            return false;
        if (!view->phases.Contains(m_phase) || WorkFor(context).GetTransparentInstances(static_cast<u32>(context.GetCameraIndex())).Empty())
            return true;
        if (!DeclareGeometryDirectDrawInputs(m_renderer, context, failure))
            return false;
        Use(context, DirectionalLights, rhi::ResourceState::ShaderResourceGraphics);
        Use(context, TransparentInstances, rhi::ResourceState::VertexBuffer);
        TextureUse(context, DepthName, rhi::ResourceState::DepthRead, LogicalAccessIntent::Read);
        TextureUse(context, CameraColorName, rhi::ResourceState::RenderTarget, LogicalAccessIntent::Write, ResourceContentIntent::Preserve);
        return true;
    }

    void RenderNodeDrawTransparent::Execute(const RenderNodeImplContext& context, jobs::Builder*) const
    {
        const auto* view = context.GetView();
        const u32 viewIndex = static_cast<u32>(context.GetCameraIndex());
        const auto instances = WorkFor(context).GetTransparentInstances(viewIndex);
        if (view == nullptr || !view->phases.Contains(m_phase) || instances.Empty())
            return;
        const auto directory = m_renderer.GetGeometryDirectoryBinding();
        const auto viewDescriptor = context.RTBuffer(Tag(Views)).GetShaderResourceDescriptor();
        const auto lightDescriptor = context.RTBuffer(Tag(DirectionalLights)).GetShaderResourceDescriptor();
        if (!viewDescriptor.IsValid() || !lightDescriptor.IsValid())
            VG_FATAL("transparent geometry graphics bindings are unavailable");
        const StaticSurfaceDrawContext draw{directory.tableDirectoryDescriptor, directory.pageDirectoryDescriptor,
                                            viewDescriptor.GpuIndex(), viewIndex, m_renderer.GetGeometryWorldCellSize(),
                                            lightDescriptor.GpuIndex()};
        rhi::RenderTargetSetup targets;
        targets.depthStencilTarget = {context.RTTexture(DepthName).GetTexture(), ToRhiFormat(TransparentAttachments.depthStencilFormat)};
        targets.colorTargetCount = 1;
        targets.colorTargets[0] = {context.RTTexture(CameraColorName).GetTexture(), ToRhiFormat(TransparentAttachments.colors[0].format)};
        SetupCamera(context, targets);
        rhi::Failure drawFailure;
        if (!m_renderer.RecordTransparentGeometryPhase(WorkFor(context), m_phase, draw,
                                                       context.RTBuffer(Tag(TransparentInstances)).GetBuffer(), &drawFailure))
            VG_FATAL("sorted transparent geometry phase recording failed");
    }

    RenderNodeVisualizeGBuffer::RenderNodeVisualizeGBuffer(const FrameRenderer& renderer) noexcept
        : m_pipeline(renderer.FindPipeline("VisualizeGBuffer")) {}

    bool RenderNodeVisualizeGBuffer::DeclareResources(const RenderNodeImplContext& context, RenderFlowResourceFailure*) const noexcept
    {
        if (!m_pipeline.IsValid()) return false;
        TextureUse(context, Colors[0], rhi::ResourceState::ShaderResourceGraphics, LogicalAccessIntent::Read);
        TextureUse(context, CameraColorName, rhi::ResourceState::RenderTarget, LogicalAccessIntent::Write, ResourceContentIntent::Discard);
        return true;
    }

    namespace
    {
        void CopyCameraColor(const ResolvedTextureUse& source, const rhi::TextureRef target,
                             const rhi::PipelineRef pipeline, const RenderViewRect rect)
        {
            const auto descriptor = source.GetShaderResourceDescriptor();
            rhi::TextureDesc targetDesc;
            struct Constants { u32 source; u32 reserved[3]{}; } constants{descriptor.GpuIndex()};
            if (!descriptor.IsValid())
                VG_FATAL("camera visualization or output descriptor is unavailable");
            const bool targetDescAvailable = rhi::GetTextureDesc(target, targetDesc);
            if (!targetDescAvailable)
                VG_FATAL("camera visualization or output target description is unavailable");
            const rhi::RenderTargetSetup targets{{{target, targetDesc.format}}, 1, {}};
            if (!rect.IsValid() || rect.x >= targetDesc.extent.width || rect.y >= targetDesc.extent.height ||
                rect.width > targetDesc.extent.width - rect.x || rect.height > targetDesc.extent.height - rect.y ||
                static_cast<u64>(rect.x) + rect.width > 0x7fffffffull || static_cast<u64>(rect.y) + rect.height > 0x7fffffffull)
                VG_FATAL("camera output region is outside the acquired target");
            const bool pipelineSet = rhi::SetPipeline(pipeline);
            if (!pipelineSet)
                VG_FATAL("camera visualization or output pipeline setup failed");
            const bool targetsSet = rhi::SetupRenderTargets(targets);
            if (!targetsSet)
                VG_FATAL("camera visualization or output target setup failed");
            const bool viewportSet = rhi::SetViewport({static_cast<f32>(rect.x),static_cast<f32>(rect.y),static_cast<f32>(rect.width),static_cast<f32>(rect.height),0,1});
            if (!viewportSet)
                VG_FATAL("camera visualization or output viewport setup failed");
            const bool scissorsSet = rhi::SetScissors({static_cast<i32>(rect.x),static_cast<i32>(rect.y),static_cast<i32>(rect.width),static_cast<i32>(rect.height)});
            if (!scissorsSet)
                VG_FATAL("camera visualization or output scissors setup failed");
            const bool constantsSet = rhi::SetPushConstants(&constants, sizeof(constants));
            if (!constantsSet)
                VG_FATAL("camera visualization or output constants setup failed");
            const bool drawRecorded = rhi::DrawPrimitive({3,1,0,0});
            if (!drawRecorded)
                VG_FATAL("camera visualization or output draw recording failed");
        }
    }

    void RenderNodeVisualizeGBuffer::Execute(const RenderNodeImplContext& context, jobs::Builder*) const
    {
        const auto* view = context.GetView();
        if (view == nullptr) VG_FATAL("GBuffer visualization view is unavailable");
        const auto& source = context.RTTexture(Colors[0]);
        const auto target = context.RTTexture(CameraColorName).GetTexture();
        CopyCameraColor(source, target, m_pipeline, {0, 0, view->rect.width, view->rect.height});
    }

    RenderNodeComposeCameraOutput::RenderNodeComposeCameraOutput(const FrameRenderer& renderer) noexcept
        : m_sceneLinearPipeline(renderer.FindPipeline("ResolveCameraOutput")),
          m_displaySrgbPipeline(renderer.FindPipeline("ResolveCameraOutputDisplaySrgb")) {}

    bool RenderNodeComposeCameraOutput::DeclareResources(const RenderNodeImplContext& context, RenderFlowResourceFailure* const failure) const noexcept
    {
        if (!context.HasFrameOutput())
            return true;
        const RenderViewport* const viewport = context.GetViewport();
        rhi::TextureDesc outputDesc;
        CameraOutputResolveKind resolve;
        if (viewport == nullptr || !rhi::GetTextureDesc(context.GetFrameOutputTexture(), outputDesc) ||
            !SelectCameraOutputResolve(viewport->GetOutputKind(), viewport->GetOutputColorEncoding(), outputDesc.format, resolve))
        {
            if (failure != nullptr)
            {
                failure->code = RenderFlowResourceFailureCode::UnsupportedCapability;
                failure->phase = RenderFlowResourceSessionState::Planning;
                failure->message = "frame output encoding is unsupported for the acquired target format";
            }
            return false;
        }
        const rhi::PipelineRef pipeline = resolve == CameraOutputResolveKind::DisplaySrgb ? m_displaySrgbPipeline : m_sceneLinearPipeline;
        if (!pipeline.IsValid())
        {
            if (failure != nullptr)
            {
                failure->code = RenderFlowResourceFailureCode::IncompletePlanning;
                failure->phase = RenderFlowResourceSessionState::Planning;
                failure->message = "camera output resolve pipeline is missing from the prepared renderer catalog";
            }
            return false;
        }
        for (const auto& region : WorkFor(context).GetOutputRegions())
        {
            const auto source = context.RTCameraNameTag(region.viewIndex, CameraColorName);
            context.RTUseBegin(source, TextureUseDesc{rhi::ResourceState::ShaderResourceGraphics, {0,1,0,1}, LogicalAccessIntent::Read});
            context.RTUseEnd(source);
        }
        const auto output = context.RTSharedNameTag(FrameOutputName);
        context.RTUseBegin(output, TextureUseDesc{rhi::ResourceState::RenderTarget, {0,1,0,1}, LogicalAccessIntent::Write, ResourceContentIntent::Discard});
        context.RTUseEnd(output);
        return true;
    }

    void RenderNodeComposeCameraOutput::Execute(const RenderNodeImplContext& context, jobs::Builder*) const
    {
        if (!context.HasFrameOutput())
            return;
        const auto target = context.RTTexture(context.RTSharedNameTag(FrameOutputName)).GetTexture();
        const RenderViewport* const viewport = context.GetViewport();
        rhi::TextureDesc targetDesc;
        CameraOutputResolveKind resolve;
        if (viewport == nullptr || !rhi::GetTextureDesc(target, targetDesc) ||
            !SelectCameraOutputResolve(viewport->GetOutputKind(), viewport->GetOutputColorEncoding(), targetDesc.format, resolve))
            VG_FATAL("frame output encoding changed to an unsupported target after planning");
        const rhi::PipelineRef pipeline = resolve == CameraOutputResolveKind::DisplaySrgb ? m_displaySrgbPipeline : m_sceneLinearPipeline;
        if (!pipeline.IsValid())
            VG_FATAL("camera output resolve pipeline became unavailable after planning");
        // The only output writer clears once, then draws regions in caller order.
        // Camera jobs never race on the output; gaps are deterministic black.
        const bool cleared = rhi::ClearColorTarget(target, {0,0,0,1});
        if (!cleared) VG_FATAL("frame output clear failed");
        for (const auto& region : WorkFor(context).GetOutputRegions())
        {
            const auto& source = context.RTTexture(context.RTCameraNameTag(region.viewIndex, CameraColorName));
            CopyCameraColor(source, target, pipeline, region.rect);
        }
    }

    RenderNodeReduceGeometryDiagnostics::RenderNodeReduceGeometryDiagnostics(const FrameRenderer& renderer) noexcept
        : m_renderer(renderer) {}

    bool RenderNodeReduceGeometryDiagnostics::DeclareResources(const RenderNodeImplContext& context, RenderFlowResourceFailure*) const noexcept
    {
        const auto& work = WorkFor(context);
        if (work.diagnosticSlot == GeometryDiagnosticReadbacks::InvalidSlot) return true;
        if (!m_renderer.FindPipeline("GeometryDiagnostics").IsValid()) return false;
        FrameBufferDesc desc;
        desc.active.size = work.GetVisibilityPlan().views.Size() * sizeof(GeometryGpuDiagnostics);
        desc.maximumSize = desc.active.size;
        desc.active.structureStride = sizeof(GeometryGpuDiagnostics);
        desc.active.keepInitialState = false;
        desc.active.usage = rhi::BufferUsage::Structured | rhi::BufferUsage::UnorderedAccess | rhi::BufferUsage::CopySource;
        static_cast<void>(context.RTSharedAlloc(DiagnosticSummaryName, desc));
        for (const auto resource : {VisibilityCounters, Counters, ShellCounters, ShellPlans})
            Use(context, resource, rhi::ResourceState::ShaderResourceCompute);
        const auto output = context.RTSharedNameTag(DiagnosticSummaryName);
        context.RTUseBegin(output, BufferUseDesc{rhi::ResourceState::UnorderedAccess, LogicalAccessIntent::Write, ResourceContentIntent::Discard});
        context.RTUseEnd(output);
        return true;
    }

    void RenderNodeReduceGeometryDiagnostics::Execute(const RenderNodeImplContext& context, jobs::Builder*) const
    {
        const auto& work = WorkFor(context);
        if (work.diagnosticSlot == GeometryDiagnosticReadbacks::InvalidSlot) return;
        const auto& report = m_renderer.GetGeometryDiagnosticPlan(work.diagnosticSlot);
        const auto visibility = context.RTBuffer(Tag(VisibilityCounters)).GetShaderResourceDescriptor();
        const auto geometry = context.RTBuffer(Tag(Counters)).GetShaderResourceDescriptor();
        const auto counters = context.RTBuffer(Tag(ShellCounters)).GetShaderResourceDescriptor();
        const auto plans = context.RTBuffer(Tag(ShellPlans)).GetShaderResourceDescriptor();
        const auto output = context.RTBuffer(context.RTSharedNameTag(DiagnosticSummaryName)).GetUnorderedAccessDescriptor();
        if (!visibility.IsValid() || !geometry.IsValid() || !counters.IsValid() || !plans.IsValid() || !output.IsValid())
            VG_FATAL("geometry diagnostic descriptors are unavailable");
        const auto pipeline = m_renderer.FindPipeline("GeometryDiagnostics");
        const bool pipelineSet = rhi::SetPipeline(pipeline);
        if (!pipelineSet) VG_FATAL("geometry diagnostic pipeline setup failed");
        for (u32 view = 0; view < report.viewCount; ++view)
        {
            const auto& entry = report.views[view];
            const u32 constants[]{visibility.GpuIndex(), geometry.GpuIndex(), counters.GpuIndex(), plans.GpuIndex(),
                                  output.GpuIndex(), view, entry.firstShell, entry.plannedShells};
            static_assert(sizeof(constants) == 32);
            const bool constantsSet = rhi::SetPushConstants(constants, sizeof(constants));
            if (!constantsSet) VG_FATAL("geometry diagnostic constants setup failed");
            // Separate dispatches write disjoint view records; no shared cursor.
            const bool dispatched = rhi::DispatchCompute(1, 1, 1);
            if (!dispatched) VG_FATAL("geometry diagnostic reduction dispatch failed");
        }
    }

    bool RenderNodeReadbackGeometryDiagnostics::DeclareResources(const RenderNodeImplContext& context, RenderFlowResourceFailure*) const noexcept
    {
        const auto& work = WorkFor(context);
        if (work.diagnosticSlot == GeometryDiagnosticReadbacks::InvalidSlot) return true;
        const auto source = context.RTSharedNameTag(DiagnosticSummaryName);
        const auto destination = context.RTSharedInjectBuffer(DiagnosticReadbackName, work.diagnosticReadbackImport);
        context.RTUseBegin(source, BufferUseDesc{rhi::ResourceState::CopySource, LogicalAccessIntent::Read});
        context.RTUseEnd(source);
        context.RTUseBegin(destination, BufferUseDesc{rhi::ResourceState::CopyDestination, LogicalAccessIntent::Write, ResourceContentIntent::Discard});
        context.RTUseEnd(destination);
        return true;
    }

    void RenderNodeReadbackGeometryDiagnostics::Execute(const RenderNodeImplContext& context, jobs::Builder*) const
    {
        auto& work = *context.GetGeometryFrameWork();
        if (work.diagnosticSlot == GeometryDiagnosticReadbacks::InvalidSlot) return;
        const auto source = context.RTBuffer(context.RTSharedNameTag(DiagnosticSummaryName)).GetBuffer();
        const auto destination = context.RTBuffer(context.RTSharedNameTag(DiagnosticReadbackName)).GetBuffer();
        const bool copied = rhi::CopyBuffer(destination, 0, source, 0, work.GetVisibilityPlan().views.Size() * sizeof(GeometryGpuDiagnostics));
        if (!copied) VG_FATAL("geometry diagnostic copy recording failed");
        work.diagnosticCopyScope = context.GetCommandScope();
    }

    bool RenderNodeDeclareGeometryWorkResources::DeclareResources(const RenderNodeImplContext& context, RenderFlowResourceFailure*) const noexcept
    {
        const auto& work = WorkFor(context);
        context.RTImportFrameOutput();
        for (const auto& external : work.GetExternalBuffers())
            static_cast<void>(context.RTSharedInjectBuffer(external.GetLogicalName(), external.imported));
        for (u32 index = 0; index < ResourceCount; ++index)
        {
            const auto resource = static_cast<Resource>(index);
            if (resource == ShellPlans)
            {
                const auto tag = context.RTSharedInjectBuffer(Names[index], work.GetShellPlanImport());
                if (!tag.resource.IsValid())
                    return false;
                context.GetGeometryFrameWork()->GetGraphResources().*ResourceIds[index] = tag.resource;
                continue;
            }
            const auto info = Describe(work, resource);
            FrameBufferDesc desc;
            desc.active.size = static_cast<u64>(info.count != 0 ? info.count : 1u) * info.stride;
            desc.maximumSize = desc.active.size;
            desc.active.structureStride = info.stride;
            desc.active.keepInitialState = false;
            desc.active.usage = rhi::BufferUsage::Structured | rhi::BufferUsage::ShaderResource;
            if ((UploadMask & Bit(resource)) != 0)
                desc.active.usage = desc.active.usage | rhi::BufferUsage::CopyDestination;
            else
                desc.active.usage = desc.active.usage | rhi::BufferUsage::UnorderedAccess;
            if (resource == Instances || resource == TransparentInstances) desc.active.usage = desc.active.usage | rhi::BufferUsage::Vertex;
            if (resource == Arguments || resource == ShellCounters) desc.active.usage = desc.active.usage | rhi::BufferUsage::IndirectArguments;
            context.GetGeometryFrameWork()->GetGraphResources().*ResourceIds[index] = context.RTSharedAlloc(Names[index], desc).resource;
        }
        return true;
    }

    bool RenderNodeInitializeGeometryWork::DeclareResources(const RenderNodeImplContext& context, RenderFlowResourceFailure*) const noexcept
    {
        for (u32 index = 0; index < ResourceCount; ++index)
        {
            const auto resource = static_cast<Resource>(index);
            if ((UploadMask & Bit(resource)) != 0)
                Use(context, resource, rhi::ResourceState::CopyDestination, LogicalAccessIntent::Write, ResourceContentIntent::Discard);
            if ((ClearMask & Bit(resource)) != 0)
                Use(context, resource, rhi::ResourceState::UnorderedAccess, LogicalAccessIntent::Write, ResourceContentIntent::Discard);
        }
        return true;
    }
    void RenderNodeInitializeGeometryWork::Execute(const RenderNodeImplContext& context, jobs::Builder*) const
    {
        const u64 cpuBegin = system::GetMonotonicTicks();
        u64 uploadTicks = 0;
        u64 clearTicks = 0;
        const auto& work = WorkFor(context);
        for (u32 index = 0; index < ResourceCount; ++index)
        {
            const auto resource = static_cast<Resource>(index);
            if ((UploadMask & Bit(resource)) != 0)
            {
                VG_PROFILE_ZONE_NAMED("Geometry/Initialize/UploadResource");
                const auto info = Describe(work, resource);
                const auto buffer = context.RTBuffer(Tag(resource)).GetBuffer();
                if (info.count != 0)
                {
                    const u64 operationBegin = system::GetMonotonicTicks();
                    const bool bufferWritten = rhi::WriteBuffer(buffer, info.data, static_cast<u64>(info.count) * info.stride);
                    uploadTicks += system::GetMonotonicTicks() - operationBegin;
                    if (!bufferWritten)
                        VG_FATAL("geometry frame upload failed");
                }
            }
            if ((ClearMask & Bit(resource)) != 0)
            {
                VG_PROFILE_ZONE_NAMED("Geometry/Initialize/ClearResource");
                const u64 operationBegin = system::GetMonotonicTicks();
                const bool bufferCleared = rhi::ClearBufferUav(context.RTBuffer(Tag(resource)).GetBuffer(), 0);
                clearTicks += system::GetMonotonicTicks() - operationBegin;
                if (!bufferCleared)
                    VG_FATAL("geometry scratch initialization failed");
            }
        }
        static_cast<void>(g_geometryInitializeTicks.ExchangeAdd(system::GetMonotonicTicks() - cpuBegin));
        static_cast<void>(g_geometryInitializeUploadTicks.ExchangeAdd(uploadTicks));
        static_cast<void>(g_geometryInitializeClearTicks.ExchangeAdd(clearTicks));
        static_cast<void>(g_geometryInitializeCalls.Increment());
    }

    RenderNodeGeometryCompute::RenderNodeGeometryCompute(const FrameRenderer& renderer, GeometryComputeStage stage) noexcept
        : m_renderer(renderer), m_stage(stage), m_pipeline(renderer.FindPipeline(Stages[static_cast<u32>(stage)].shader)) {}
    const char* RenderNodeGeometryCompute::GetName() const noexcept { return Stages[static_cast<u32>(m_stage)].shader; }

    bool RenderNodeGeometryCompute::DeclareResources(const RenderNodeImplContext& context, RenderFlowResourceFailure* failure) const noexcept
    {
        if (!m_pipeline.IsValid())
        {
            if (failure != nullptr)
            {
                failure->code = RenderFlowResourceFailureCode::IncompletePlanning;
                failure->phase = RenderFlowResourceSessionState::Planning;
                failure->message = "geometry compute pipeline is missing from the prepared renderer catalog";
            }
            return false;
        }
        const bool sceneConsumer = m_stage == GeometryComputeStage::Cull || m_stage == GeometryComputeStage::ExpandAppend ||
                                   m_stage == GeometryComputeStage::AllocateBins;
        if (sceneConsumer)
        {
            const bool sceneUsesDeclared = m_renderer.DeclareGeometrySceneUses(context, failure);
            if (!sceneUsesDeclared)
                return false;
        }
        const auto& stage = Stages[static_cast<u32>(m_stage)];
        for (u32 index = 0; index < ResourceCount; ++index)
        {
            const auto resource = static_cast<Resource>(index);
            if ((stage.writes & Bit(resource)) != 0)
                Use(context, resource, rhi::ResourceState::UnorderedAccess,
                    (stage.discard & Bit(resource)) != 0 ? LogicalAccessIntent::Write : LogicalAccessIntent::ReadWrite,
                    (stage.discard & Bit(resource)) != 0 ? ResourceContentIntent::Discard : ResourceContentIntent::Preserve);
            else if ((stage.reads & Bit(resource)) != 0)
                Use(context, resource, rhi::ResourceState::ShaderResourceCompute);
        }
        return true;
    }

    void RenderNodeGeometryCompute::Execute(const RenderNodeImplContext& context, jobs::Builder*) const
    {
        VG_PROFILE_ZONE_NAMED("Geometry/Compute/Execute");
        const u64 cpuBegin = system::GetMonotonicTicks();
        const auto& work = WorkFor(context);
        const auto& plan = work.GetVisibilityPlan();
        const auto& stage = Stages[static_cast<u32>(m_stage)];
        const auto directory = m_renderer.GetGeometryDirectoryBinding();
        const auto descriptor = [&](Resource resource) -> u32 {
            if (((stage.reads | stage.writes) & Bit(resource)) == 0) return InvalidGpuDescriptorIndex;
            const auto& use = context.RTBuffer(Tag(resource));
            const auto handle = (stage.writes & Bit(resource)) != 0 ? use.GetUnorderedAccessDescriptor() : use.GetShaderResourceDescriptor();
            if (!handle.IsValid()) VG_FATAL("geometry graph descriptor is unavailable");
            return handle.GpuIndex();
        };
        const auto dispatch = [&](const auto& constants, u32 groupsX, u32 groupsY) {
            if (groupsX == 0 || groupsY == 0) return;
            VG_PROFILE_ZONE_NAMED("Geometry/Compute/DispatchRecord");
            if (groupsX > 65535u || groupsY > 65535u || !m_pipeline.IsValid())
                VG_FATAL("geometry compute pipeline is unavailable or dispatch exceeded the dimension limit");
            const bool pipelineSet = rhi::SetPipeline(m_pipeline);
            if (!pipelineSet)
                VG_FATAL("geometry compute pipeline setup failed");
            const bool constantsSet = rhi::SetPushConstants(&constants, sizeof(constants));
            if (!constantsSet)
                VG_FATAL("geometry compute constants setup failed");
            const bool dispatchRecorded = rhi::DispatchCompute(groupsX, groupsY, 1);
            if (!dispatchRecorded)
                VG_FATAL("geometry compute dispatch failed");
        };
        if (m_stage == GeometryComputeStage::Cull)
        {
            GpuVisibilityConstants c;
            c.tableDirectoryDescriptor = directory.tableDirectoryDescriptor; c.pageDirectoryDescriptor = directory.pageDirectoryDescriptor;
            c.viewDescriptor = descriptor(Views); c.resultRangeDescriptor = descriptor(VisibilityResults);
            c.visibleDescriptor = descriptor(Visible); c.counterDescriptor = descriptor(VisibilityCounters);
            c.sceneIndex = plan.sceneIndex; c.sceneGeneration = plan.sceneGeneration;
            c.instanceTraversalExtent = plan.instanceTraversalExtent; c.viewCount = plan.views.Size();
            c.worldCellSize = m_renderer.GetGeometryWorldCellSize();
            const u32 groups = (c.instanceTraversalExtent + GpuVisibilityThreadsPerGroup - 1u) / GpuVisibilityThreadsPerGroup;
            dispatch(c, groups, c.viewCount);
        }
        else if (m_stage == GeometryComputeStage::ExpandAppend)
        {
            GpuGeometryExpandConstants c;
            c.tableDirectoryDescriptor = directory.tableDirectoryDescriptor; c.pageDirectoryDescriptor = directory.pageDirectoryDescriptor;
            c.visibleDescriptor = descriptor(Visible); c.visibilityResultDescriptor = descriptor(VisibilityResults);
            c.visibilityCounterDescriptor = descriptor(VisibilityCounters); c.viewDescriptor = descriptor(Views);
            c.workDescriptor = descriptor(Work); c.resultDescriptor = descriptor(Results); c.counterDescriptor = descriptor(Counters);
            c.binItemDescriptor = descriptor(Bins); c.activeBinDescriptor = descriptor(ActiveBins); c.resultCount = plan.results.Size();
            if (c.resultCount == 0 || work.GetCapacity().visibleInstances % c.resultCount != 0)
                VG_FATAL("sparse geometry visible partitions are not uniform");
            c.visibleCapacityPerResult = work.GetCapacity().visibleInstances / c.resultCount;
            dispatch(c, (work.GetCapacity().visibleInstances + GpuVisibilityThreadsPerGroup - 1u) / GpuVisibilityThreadsPerGroup, 1u);
        }
        else if (m_stage == GeometryComputeStage::AllocateBins)
        {
            GpuGeometryAllocateConstants c;
            c.tableDirectoryDescriptor = directory.tableDirectoryDescriptor; c.pageDirectoryDescriptor = directory.pageDirectoryDescriptor;
            c.resultDescriptor = descriptor(Results); c.counterDescriptor = descriptor(Counters); c.binItemDescriptor = descriptor(Bins);
            c.activeBinDescriptor = descriptor(ActiveBins); c.shellRangeDescriptor = descriptor(ShellRanges);
            c.shellCounterDescriptor = descriptor(ShellCounters); c.argumentDescriptor = descriptor(Arguments);
            c.resultCount = plan.results.Size(); c.shellCapacity = work.GetCapacity().shellsPerView; c.shellPlanCount = work.GetShellDraws().Size();
            dispatch(c, c.resultCount, 1u);
        }
        else if (m_stage == GeometryComputeStage::ScatterInstances)
        {
            GpuGeometryScatterConstants c;
            c.workDescriptor = descriptor(Work); c.resultDescriptor = descriptor(Results); c.counterDescriptor = descriptor(Counters);
            c.binItemDescriptor = descriptor(Bins); c.instanceDescriptor = descriptor(Instances); c.resultCount = plan.results.Size();
            if (c.resultCount == 0 || work.GetCapacity().geometryWorkItems % c.resultCount != 0)
                VG_FATAL("sparse geometry work partitions are not uniform");
            c.workCapacityPerResult = work.GetCapacity().geometryWorkItems / c.resultCount;
            dispatch(c, (work.GetCapacity().geometryWorkItems + GpuVisibilityThreadsPerGroup - 1u) / GpuVisibilityThreadsPerGroup, 1u);
        }
        else if (m_stage == GeometryComputeStage::InitializeShells)
        {
            GpuGeometryInitializeConstants c;
            c.shellPlanDescriptor = descriptor(ShellPlans);
            c.shellRangeDescriptor = descriptor(ShellRanges); c.shellCounterDescriptor = descriptor(ShellCounters);
            c.counterDescriptor = descriptor(Counters); c.shellPlanCount = work.GetShellDraws().Size();
            c.shellCapacity = work.GetCapacity().shellsPerView; c.resultCount = plan.results.Size();
            const u32 shellGroups = (c.shellPlanCount + GpuVisibilityThreadsPerGroup - 1u) / GpuVisibilityThreadsPerGroup;
            const u32 counterGroups = (c.resultCount + GpuVisibilityThreadsPerGroup - 1u) / GpuVisibilityThreadsPerGroup;
            dispatch(c, shellGroups > counterGroups ? shellGroups : counterGroups, 1u);
        }
        const u32 stageIndex = static_cast<u32>(m_stage);
        static_cast<void>(g_geometryStageTicks[stageIndex].ExchangeAdd(system::GetMonotonicTicks() - cpuBegin));
        static_cast<void>(g_geometryStageCalls[stageIndex].Increment());
    }
}
