#include <vanguard/rendering/frame_renderer.hpp>

#include <vanguard/jobs/jobs.hpp>
#include <vanguard/rendering/render_graph_nodes.hpp>
#include <vanguard/rendering/geometry_graph_nodes.hpp>
#include <vanguard/rendering/render_node_graph.hpp>
#include <vanguard/rendering/render_node_impl_context.hpp>
#include <vanguard/rendering/render_node_job.hpp>

#include <cmath>
#include <limits>
#include <cstdio>
#include <cstring>

namespace vanguard::rendering
{
    namespace
    {
        inline constexpr u64 RenderGraphRendererRevision = 5;
        inline constexpr u32 GeometryCandidateTargetBatchSize = 1024;

        // RenderView planes consume camera-relative positions. The CPU spatial
        // index stores scene-space bounds, so translate only its query planes.
        [[nodiscard]] bool BuildSceneQueryFrustum(const RenderView& view, const f32 worldCellSize, VisibilityFrustum& output) noexcept
        {
            if (!std::isfinite(worldCellSize) || worldCellSize <= 0.0f || view.frustum.planeCount == 0 ||
                view.frustum.planeCount > MaximumVisibilityFrustumPlanes)
                return false;
            f64 origin[3];
            for (u32 axis = 0; axis < 3; ++axis)
            {
                origin[axis] = static_cast<f64>(view.origin.worldCell[axis]) * worldCellSize + view.origin.localPosition[axis];
                if (!std::isfinite(origin[axis]))
                    return false;
            }
            output = view.frustum;
            for (u32 index = 0; index < output.planeCount; ++index)
            {
                auto& plane = output.planes[index];
                f64 distance = plane.distance;
                for (u32 axis = 0; axis < 3; ++axis)
                    distance -= static_cast<f64>(plane.normal[axis]) * origin[axis];
                if (!std::isfinite(distance) || std::abs(distance) > std::numeric_limits<f32>::max())
                    return false;
                plane.distance = static_cast<f32>(distance);
                // Round outward so narrowing does not shrink the visible half-space.
                if (static_cast<f64>(plane.distance) < distance)
                    plane.distance = std::nextafter(plane.distance, std::numeric_limits<f32>::infinity());
            }
            return true;
        }

        enum : u32
        {
            UniqueRenderNodeStartRender = 0,
            UniqueRenderNodeEndRender = 1,
            UniqueRenderNodeFlushTextureGrabs = 10,
            UniqueRenderNodeEndFrame = 13,
            UniqueRenderNodeCleanupBatchDataAllocator = 14,
            UniqueRenderNodeFinalFlush = 15,
            UniqueRenderNodePresent = 16,
            UniqueRenderNodeGeometryCompute = 21,
            UniqueRenderNodeComposeCameraOutput = 22,
            UniqueRenderNodeGeometryDiagnostics = 23
        };

        [[nodiscard]] const char* FailureMessage(const char* const message, const char* const fallback) noexcept
        {
            return message != nullptr && message[0] != '\0' ? message : fallback;
        }

        [[nodiscard]] bool FindViewIndex(const containers::ArraySpan<const RenderView> views, const RenderCameraHandle camera, u32& viewIndex) noexcept
        {
            for (u32 index = 0; index < views.Size(); ++index)
            {
                if (views[index].id.index != camera.index || views[index].id.generation != camera.generation)
                    continue;
                viewIndex = index;
                return true;
            }
            return false;
        }

        [[nodiscard]] const char* BuildRenderGraphKey(const RenderFrameInfo& frame, const PreparedRenderViewFamily* const family, RenderGraphKey& key) noexcept
        {
            key = {};
            key.mode = frame.GetMode();
            key.purpose = frame.GetPurpose();
            key.outputKind = frame.GetOutputKind();
            key.renderExtent = frame.GetRenderExtent();
            key.outputExtent = frame.GetOutputExtent();
            key.rendererRevision = RenderGraphRendererRevision;
            key.present = frame.ShouldPresent();
            const RenderFrameFeatures& features = frame.GetFeatures();
            key.featureMask = static_cast<u64>(features.debugView) | (static_cast<u64>(features.wireframe) << 8) | (static_cast<u64>(features.multilayerSelection) << 9) | (static_cast<u64>(family != nullptr && family->GetScene().IsValid()) << 10);
            key.featureMask |= static_cast<u64>(features.geometryDiagnostics) << 11;

            if (family == nullptr || frame.GetMode() == RenderingMode::OverlayOnly)
            {
                key.RebuildHash();
                return key.IsValid() ? nullptr : "render graph key is invalid";
            }

            const containers::ArraySpan<const RenderView> views = family->GetViews();
            const containers::ArraySpan<const RenderCameraDependency> dependencies = family->GetDependencies();
            if (views.Size() > key.views.Size() || dependencies.Size() > key.dependencies.Size())
                return "prepared view family exceeds render graph key capacity";

            key.viewCount = views.Size();
            for (u32 index = 0; index < views.Size(); ++index)
            {
                const RenderView& view = views[index];
                RenderViewGraphKey& viewKey = key.views[index];
                viewKey.mode = frame.GetMode();
                viewKey.purpose = view.purpose;
                viewKey.flags = view.flags;
                viewKey.phases = view.phases;
                viewKey.featureMask = key.featureMask;
                viewKey.width = view.rect.width;
                viewKey.height = view.rect.height;
                viewKey.RebuildHash();
                if (!viewKey.IsValid())
                    return "prepared render view produced an invalid graph key";
            }

            key.dependencyCount = dependencies.Size();
            for (u32 index = 0; index < dependencies.Size(); ++index)
            {
                const RenderCameraDependency& dependency = dependencies[index];
                RenderGraphDependencyKey& dependencyKey = key.dependencies[index];
                if (!FindViewIndex(views, dependency.parent, dependencyKey.parentViewIndex) || !FindViewIndex(views, dependency.child, dependencyKey.childViewIndex))
                    return "prepared camera dependency does not belong to its render view family";
                dependencyKey.outputs = dependency.outputs;
            }

            key.RebuildHash();
            return key.IsValid() ? nullptr : "render graph key is invalid";
        }

    } // namespace

    FrameRenderer::FrameRenderer(RenderSceneManager& scenes, RenderCameraStorage& cameras, GpuSceneRuntime& gpuScene,
                                 const GeometryFrameWorkConfig& geometryConfig, const u32 geometryBinCapacity,
                                 const RenderGeometryBatcher* const geometryBatcher, const u32 geometryShellCapacity,
                                 const GeometryAllocator* const geometryAllocator, const RenderPhaseRegistry* const phases) noexcept
        : m_scenes(scenes), m_cameras(cameras), m_gpuScene(gpuScene), m_geometryConfig(geometryConfig), m_geometryBinCapacity(geometryBinCapacity),
          m_geometryBatcher(geometryBatcher), m_geometryShellCapacity(geometryShellCapacity), m_geometryAllocator(geometryAllocator), m_phases(phases)
    {
    }


    bool FrameRenderer::RegisterGeometryImports(GeometryFrameWork& work, RenderFlowResourceFailure* failure) noexcept
    {
        if (!work.IsPrepared())
            return true;
        auto& imports = work.GetExternalBuffers();
        imports.Clear();
        containers::HashMap<u64, u32> importedBuffers{memory::pools::Rendering::GetInstance()};
        const auto add = [&](rhi::BufferRef buffer, bool scene, rhi::ResourceState state, rhi::GpuFence incoming) -> bool {
            u32 existing = 0;
            const u64 identity = rhi::ResourceRef(buffer).value;
            const bool alreadyImported = importedBuffers.Find(identity, existing);
            if (alreadyImported)
                return imports[existing].sceneTable == scene && imports[existing].readState == state;
            RetainedBufferImportDesc desc;
            desc.token = {identity};
            desc.buffer = buffer;
            const bool descriptorAvailable = rhi::GetBufferDesc(buffer, desc.expected);
            if (!descriptorAvailable)
                return false;
            desc.initialState = desc.expected.initialState;
            desc.terminalState = desc.expected.initialState;
            desc.initialQueue = incoming.IsValid() ? incoming.queue : rhi::QueueType::Graphics;
            desc.terminalQueue = rhi::QueueType::Graphics;
            desc.incomingWait = incoming;
            desc.readiness = incoming.IsValid() ? ImportReadinessKind::ExplicitFenceWait : ImportReadinessKind::SameQueueContinuation;
            GeometryExternalBuffer entry;
            entry.sceneTable = scene;
            entry.readState = state;
            std::snprintf(entry.name, sizeof(entry.name), "Geometry.External.%llu", static_cast<unsigned long long>(identity));
            // Import readiness follows the actual publication queue. CopySync
            // currently publishes GPU Scene on Graphics; submission ordering
            // satisfies that case without inserting a redundant native wait.
            const bool registered = m_resourceAllocator.RegisterImport(desc, entry.imported, failure);
            if (!registered)
                return false;
            const auto inserted = importedBuffers.Insert(identity, imports.Size());
            if (!inserted.IsSuccessful())
                return false;
            imports.PushBack(entry);
            return true;
        };
        for (const auto buffer : m_gpuScene.GetTables().GetConsumerBuffers())
        {
            const bool added = add(buffer, true, rhi::ResourceState::ShaderResourceGraphics | rhi::ResourceState::ShaderResourceCompute,
                                   m_gpuScene.GetPublicationFence());
            if (!added)
                return false;
        }
        if (m_geometryBatcher == nullptr || m_geometryAllocator == nullptr)
            return work.GetShellDraws().Empty();
        // Iterate each active phase only once, even when several views request
        // it. Native-buffer identity deduplication avoids repeated arena imports.
        u64 requestedPhases = 0;
        for (const auto& view : work.GetVisibilityPlan().views)
            requestedPhases |= static_cast<u64>(view.phaseMaskLow) | (static_cast<u64>(view.phaseMaskHigh) << 32u);
        for (u32 phaseIndex = 0; phaseIndex < MaximumRenderPhases; ++phaseIndex)
        {
            if ((requestedPhases & (u64{1} << phaseIndex)) == 0) continue;
            for (const auto& shell : m_geometryBatcher->GetPhaseShells({static_cast<u8>(phaseIndex)}))
            {
                GeometryVertexArenaView vertices;
                GeometryIndexArenaView indices;
                const bool verticesAvailable = m_geometryAllocator->GetVertexArena(shell.state.vertexArena, vertices);
                if (!verticesAvailable) return false;
                const bool indicesAvailable = m_geometryAllocator->GetIndexArena(shell.state.indexArena, indices);
                if (!indicesAvailable) return false;
                for (u32 index = 0; index < vertices.bindingCount; ++index)
                {
                    const bool vertexAdded = add(vertices.buffers[index], false, rhi::ResourceState::VertexBuffer, {});
                    if (!vertexAdded) return false;
                }
                const bool indexAdded = add(indices.buffer, false, rhi::ResourceState::IndexBuffer, {});
                if (!indexAdded) return false;
            }
        }
        return true;
    }

    bool FrameRenderer::DeclareGeometrySceneUses(const RenderNodeImplContext& context, RenderFlowResourceFailure*) const noexcept
    {
        const auto* work = context.GetGeometryFrameWork();
        if (work == nullptr) return false;
        for (const auto& external : work->GetExternalBuffers())
        {
            if (!external.sceneTable) continue;
            const auto tag = context.RTSharedNameTag(external.name);
            context.RTUseBegin(tag, BufferUseDesc{external.readState});
            context.RTUseEnd(tag);
        }
        return true;
    }

    bool FrameRenderer::DeclareGeometryArenaUses(const RenderNodeImplContext& context, RenderFlowResourceFailure*) const noexcept
    {
        const auto* work = context.GetGeometryFrameWork();
        if (work == nullptr) return false;
        for (const auto& external : work->GetExternalBuffers())
        {
            if (external.sceneTable) continue;
            const auto tag = context.RTSharedNameTag(external.name);
            context.RTUseBegin(tag, BufferUseDesc{external.readState});
            context.RTUseEnd(tag);
        }
        return true;
    }

    bool FrameRenderer::RecordGeometryPhase(const GeometryFrameWork& work, RenderPhaseId phase, const StaticSurfaceDrawContext& draw,
                                            const GeometryPhaseDrawBuffers& buffers, rhi::Failure* failure) const noexcept
    {
        return m_geometryBatcher != nullptr && m_geometryAllocator != nullptr &&
               work.RecordPhase(*m_geometryBatcher, *m_geometryAllocator, phase, draw, buffers, failure);
    }

    void FrameRenderer::BuildRenderGraphBlank(RenderNodeGraph& graph, NodesContainer& nodes, const bool isWireframe, const bool isPrewarm) noexcept
    {
        static_cast<void>(isWireframe);
        NodeGraphFactory factory(&graph, &nodes);
        const NodeGroupId group = NodeGroupId::None;

        RENDER_UNIQUE_SIMPLE_COMMAND_LIST(group, "StartRender", UniqueRenderNodeStartRender, RenderNodeStartRender);
        RENDER_COMMAND_LIST(group, "RenderGraph_Blank")
        {
            ADD_SUBNODE("DeclCommonResAllocs", RenderNodeDeclareCommonResourceAllocsFinalOnly);
            ADD_SUBNODE("PrepSceneRendering", RenderNodePrepareBlankRendering);
            if (isPrewarm)
            {
                ADD_SUBNODE("RenderSkyScattering", RenderNodeRenderSkyScattering);
                ADD_SUBNODE("ReflectionProbes", RenderNodeReflectionProbes);
                ADD_SUBNODE("GlobalIllumination", RenderNodeGlobalIllumination);
                ADD_SUBNODE("VolumetricFog", RenderNodeVolumetricFog);
            }
            ADD_SUBNODE("ClearFinalColorTarget", RenderNodeClearFinalColorTarget);
            ADD_SUBNODE("DrawComposition", RenderNodeDrawComposition);
            ADD_SUBNODE("CompositionPostProcess", RenderNodeCompositionPostProcess);
            ADD_SUBNODE("DrawHUD", RenderNodeDrawHud);
            ADD_SUBNODE("FullscreenVideo", RenderNodeFullscreenVideo);
            ADD_SUBNODE("RenderFinal2D", RenderNodeRenderFinal2D);
            ADD_SUBNODE("ExtractionFinalColor", RenderNodeExtractionFinalColor);
        }
        RENDER_UNIQUE_SIMPLE_COMMAND_LIST(group, "EndRender", UniqueRenderNodeEndRender, RenderNodeEndRender);
        ADD_UNIQUE(group, "FlushGpu", UniqueRenderNodeFinalFlush, RenderNodeSynchronize, rhi::CommandListSyncType::None, "Submit_RenderGraphBlank");
        ADD_UNIQUE(group, "Present", UniqueRenderNodePresent, RenderNodePresent);
        ADD_UNIQUE(group, "FlushTextureGrabs", UniqueRenderNodeFlushTextureGrabs, RenderNodeFlushTextureGrabs);
        ADD_UNIQUE(group, "CleanupBatchDataAllocator", UniqueRenderNodeCleanupBatchDataAllocator, RenderNodeCleanupBatchDataAllocator);
        ADD_UNIQUE(group, "EndFrame", UniqueRenderNodeEndFrame, RenderNodeEndFrame);

        factory.LinkGpu();
        factory.LinkCpu();
    }

    void FrameRenderer::BuildRenderGraphHitProxies(RenderNodeGraph& graph, NodesContainer& nodes, const bool, const bool) noexcept
    {
        NodeGraphFactory factory(&graph, &nodes);
        static_cast<void>(factory);
    }

    void FrameRenderer::BuildRenderGraphGBufferOnly(RenderNodeGraph& graph, NodesContainer& nodes, const bool diagnostics) noexcept
    {
        BuildRenderGraphCamera(graph, nodes, RenderView{}, true, diagnostics);
    }

    void FrameRenderer::BuildRenderGraphCamera(RenderNodeGraph& graph, NodesContainer& nodes, const RenderView&, const bool gBufferOnly, const bool diagnostics) noexcept
    {
        NodeGraphFactory factory(&graph, &nodes);
        const NodeGroupId group = NodeGroupId::None;
        RENDER_UNIQUE_SIMPLE_COMMAND_LIST(group, "StartRender", UniqueRenderNodeStartRender, RenderNodeStartRender);
        // Unique occurrences merge across cached camera fragments. Every stage
        // addresses the complete family plan once, with independent view ranges.
        RENDER_UNIQUE_COMMAND_LIST(group, UniqueRenderNodeGeometryCompute, "GeometryWork")
        {
            // Declaration-only child uses the group's planning writer and
            // records no GPU commands or separate submission.
            ADD_SUBNODE("DeclareGeometryWorkResources", RenderNodeDeclareGeometryWorkResources);
            ADD_SUBNODE("InitializeGeometryWork", RenderNodeInitializeGeometryWork);
            for (u32 stage = 0; stage < static_cast<u32>(GeometryComputeStage::Count); ++stage)
                ADD_SUBNODE("GeometryCompute", RenderNodeGeometryCompute, *this, static_cast<GeometryComputeStage>(stage));
        }
        RENDER_COMMAND_LIST(group, "CameraGeometry")
        {
            ADD_SUBNODE("BeginCameraDependencies", RenderNodeBeginCameraDependencies);
            ADD_SUBNODE("DeclareCommonResourceAllocs", RenderNodeDeclareCommonResourceAllocs);
            ADD_SUBNODE("ClearCameraTargets", RenderNodeClearCameraTargets);
            ADD_SUBNODE("DrawOpaqueDepth", RenderNodeDrawOpaqueDepth, *this);
            ADD_SUBNODE("DrawOpaqueGBuffer", RenderNodeDrawOpaqueGBuffer, *this);
            if (gBufferOnly)
            {
                ADD_SUBNODE("VisualizeGBuffer", RenderNodeVisualizeGBuffer, *this);
            }
            else
            {
                ADD_SUBNODE("DirectionalDiffuse", RenderNodeDirectionalDiffuse, *this);
            }
            ADD_SUBNODE("EndCameraDependencies", RenderNodeEndCameraDependencies);
        }
        RENDER_UNIQUE_SIMPLE_COMMAND_LIST(group, "ComposeCameraOutput", UniqueRenderNodeComposeCameraOutput, RenderNodeComposeCameraOutput, *this);
        if (diagnostics)
        {
            RENDER_UNIQUE_COMMAND_LIST(group, UniqueRenderNodeGeometryDiagnostics, "GeometryDiagnostics")
            {
                ADD_SUBNODE("ReduceGeometryDiagnostics", RenderNodeReduceGeometryDiagnostics, *this);
                ADD_SUBNODE("ReadbackGeometryDiagnostics", RenderNodeReadbackGeometryDiagnostics);
            }
        }
        RENDER_UNIQUE_SIMPLE_COMMAND_LIST(group, "EndRender", UniqueRenderNodeEndRender, RenderNodeEndRender);
        ADD_UNIQUE(group, "FlushGpu", UniqueRenderNodeFinalFlush, RenderNodeSynchronize, rhi::CommandListSyncType::None, "Submit_CameraGraph");
        ADD_UNIQUE(group, "Present", UniqueRenderNodePresent, RenderNodePresent);
        ADD_UNIQUE(group, "FlushTextureGrabs", UniqueRenderNodeFlushTextureGrabs, RenderNodeFlushTextureGrabs);
        ADD_UNIQUE(group, "CleanupBatchDataAllocator", UniqueRenderNodeCleanupBatchDataAllocator, RenderNodeCleanupBatchDataAllocator);
        ADD_UNIQUE(group, "EndFrame", UniqueRenderNodeEndFrame, RenderNodeEndFrame);
        factory.LinkGpu();
        factory.LinkCpu();
    }

    void FrameRenderer::BuildRenderGraphSafeMode(RenderNodeGraph& graph, NodesContainer& nodes, const RenderView&) noexcept
    {
        NodeGraphFactory factory(&graph, &nodes);
        static_cast<void>(factory);
    }

    void FrameRenderer::BuildRenderGraphNoScene(RenderNodeGraph& graph, NodesContainer& nodes, const bool) noexcept
    {
        NodeGraphFactory factory(&graph, &nodes);
        static_cast<void>(factory);
    }

    void FrameRenderer::BuildRenderGraphTodvis(RenderNodeGraph& graph, NodesContainer& nodes, const bool) noexcept
    {
        NodeGraphFactory factory(&graph, &nodes);
        static_cast<void>(factory);
    }

    void FrameRenderer::BuildRenderGraphDebugVisualization(RenderNodeGraph& graph, NodesContainer& nodes, const RenderView&) noexcept
    {
        NodeGraphFactory factory(&graph, &nodes);
        static_cast<void>(factory);
    }

    void FrameRenderer::ClearGraphCache() noexcept
    {
        m_graphCache.Clear();
    }

    RenderShaderResult FrameRenderer::InitializeShaders(const containers::ArraySpan<const NamedRenderShader> shaders, rhi::Failure* const failure) noexcept
    {
        if (failure != nullptr)
            *failure = {};
        if (m_resourceAllocator.IsInitialized())
            return RenderShaderResult::InvalidState;
        return m_shaders.Init(shaders, failure);
    }

    bool FrameRenderer::InitializeResourceAllocator(const RenderFlowResourceAllocatorConfig& config, RenderFlowResourceFailure* const failure) noexcept
    {
        return m_resourceAllocator.Initialize(config, failure);
    }

    RenderPipelineResult FrameRenderer::InitializePipelines(const containers::ArraySpan<const NamedRenderPipeline> pipelines, PipelineCache& cache) noexcept
    {
        if (!m_pipelines.Empty() || !m_shaders.IsInitialized() || !cache.IsInitialized())
            return RenderPipelineResult::InvalidArgument;
        containers::DynamicArray<FeaturePipeline> building{memory::pools::Rendering::GetInstance()};
        building.Reserve(pipelines.Size());
        for (const NamedRenderPipeline& source : pipelines)
        {
            if (source.name.Size() == 0)
                return RenderPipelineResult::InvalidArgument;
            for (const FeaturePipeline& existing : building)
                if (containers::StringView(existing.name) == source.name)
                    return RenderPipelineResult::InvalidArgument;
            FeaturePipeline entry;
            entry.name.Set(source.name);
            const RenderPipelineResult result = RequestRenderPipeline(source.request, cache, entry.request);
            if (result != RenderPipelineResult::Success)
                return result;
            building.PushBack(std::move(entry));
        }
        // Admit every variant before waiting, retaining the cache's parallel creation.
        for (const FeaturePipeline& entry : building)
        {
            entry.request.Wait();
            if (!entry.request.HasSucceeded())
                return RenderPipelineResult::NativeObjectFailure;
        }
        m_pipelines = std::move(building);
        return RenderPipelineResult::Success;
    }

    rhi::PipelineRef FrameRenderer::FindPipeline(const containers::StringView name) const noexcept
    {
        for (const FeaturePipeline& entry : m_pipelines)
            if (containers::StringView(entry.name) == name)
                return entry.request.GetPipeline();
        return {};
    }

    rhi::PipelineRef FrameRenderer::GetPipeline(const containers::StringView name) const noexcept
    {
        for (const FeaturePipeline& entry : m_pipelines)
            if (containers::StringView(entry.name) == name)
                return entry.request.GetPipeline();
        VG_FATAL("required renderer pipeline is not present in the prepared catalog");
    }

    void FrameRenderer::ClearPipelines() noexcept
    {
        ClearGraphCache();
        m_geometryDiagnostics.Reset();
        m_pipelines.Clear();
    }

    bool FrameRenderer::ClearResourceAllocatorCaches(RenderFlowResourceFailure* const failure) noexcept
    {
        return m_resourceAllocator.ClearPersistentCaches(failure);
    }

    bool FrameRenderer::ShutdownResourceAllocator(RenderFlowResourceFailure* const failure) noexcept
    {
        ClearPipelines();
        if (!m_resourceAllocator.Shutdown(failure))
            return false;
        m_shaders.Clear();
        return true;
    }

    bool FrameRenderer::IsResourceAllocatorInitialized() const noexcept
    {
        return m_resourceAllocator.IsInitialized();
    }

    RenderFlowResourceAllocatorStats FrameRenderer::GetResourceAllocatorStats() const noexcept
    {
        return m_resourceAllocator.GetStats();
    }

    RenderFrameExecutionStatus FrameRenderer::FrameTick(RenderFrameTickContext& context) noexcept
    {
        if (!m_gpuScene.IsInitialized())
            return RenderFrameExecutionStatus::Success();

        GpuSceneRuntimeFailure failure;
        if (!m_gpuScene.Publish(context, &failure))
            return RenderFrameExecutionStatus::Failure(FailureMessage(failure.message, "GPU Scene publication dispatch failed"));
        return RenderFrameExecutionStatus::Success();
    }

    RenderFrameExecutionStatus FrameRenderer::RenderFrame(RenderFrameContext& context) noexcept
    {
        if (rhi::TestDeviceState() != rhi::DeviceState::Operational)
            return RenderFrameExecutionStatus::Failure("render device is unavailable before frame preparation");
        const RenderFrameInfo& frame = context.GetFrame();
        if (frame.GetGraph().graph != nullptr)
        {
            FrameResourcePolicy policy;
            policy.processEviction = frame.GetGraph().processResourceEviction;
            return ExecuteBuiltGraph(context, *frame.GetGraph().graph, policy);
        }
        PreparedRenderViewFamily& preparedFamily = context.GetPreparedViewFamily();
        const PreparedRenderViewFamily* family = frame.GetViewFamily().IsValid() ? &frame.GetViewFamily() : nullptr;

        const RenderFrameExecutionStatus preparation = PrepareViewFamily(frame, preparedFamily, family);
        if (!preparation)
            return preparation;

        if (frame.GetMode() != RenderingMode::OverlayOnly && frame.GetPurpose() != RenderFramePurpose::Blank && family != nullptr &&
            !family->GetViews().Empty() && family->GetScene().IsValid())
            return PrepareGeometryWork(context, *family);
        return RenderPreparedFrame(context);
    }

    RenderFrameExecutionStatus FrameRenderer::RenderPreparedFrame(RenderFrameContext& context) noexcept
    {
        if (rhi::TestDeviceState() != rhi::DeviceState::Operational)
            return RenderFrameExecutionStatus::Failure("render device is unavailable before graph preparation");
        const RenderFrameInfo& frame = context.GetFrame();
        const RenderFrameFeatures& features = frame.GetFeatures();
        const PreparedRenderViewFamily* const family = frame.GetViewFamily().IsValid() ? &frame.GetViewFamily() :
            (context.GetPreparedViewFamily().IsValid() ? &context.GetPreparedViewFamily() : nullptr);
        RenderGraphKey graphKey;
        if (const char* const keyFailure = BuildRenderGraphKey(frame, family, graphKey))
            return RenderFrameExecutionStatus::Failure(keyFailure);

        const u32 cameraCount = frame.GetMode() != RenderingMode::OverlayOnly && family != nullptr ? family->GetViews().Size() : 0;
        const bool isBlankFrame = cameraCount == 0 || frame.GetPurpose() == RenderFramePurpose::Blank;
        if (!isBlankFrame && family != nullptr && family->GetScene().IsValid() &&
            (frame.GetMode() == RenderingMode::Shaded || frame.GetMode() == RenderingMode::ShadedNoAmbient || frame.GetMode() == RenderingMode::GBufferOnly) &&
            !AreGeometryGraphPipelinesReady(*this, frame.GetMode() == RenderingMode::GBufferOnly))
            return RenderFrameExecutionStatus::Failure("camera geometry requires its cooked renderer pipelines to be ready before graph construction");
        const u32 cameraSetupCount = isBlankFrame ? 1 : cameraCount;
        bool needsRebuild = false;
        RenderGraphCache::CacheEntry* const graphCacheEntry = m_graphCache.GetGraph(frame.GetSerial(), graphKey, cameraSetupCount, needsRebuild);
        if (graphCacheEntry == nullptr)
            return RenderFrameExecutionStatus::Failure("render graph cache lookup failed");
        if (needsRebuild)
        {
            if (isBlankFrame)
            {
                RenderGraphCache::CameraSetupData& storage = *graphCacheEntry->cameraBuildData.Front();
                BuildRenderGraphBlank(storage.graph, storage.nodes, features.wireframe, cameraCount != 0);
                graphCacheEntry->graph.AddGraph(storage.graph, true, 0, true);
            }
            else
            {
                const containers::ArraySpan<const RenderView> views = family->GetViews();
                for (u32 viewIndex = 0; viewIndex < views.Size(); ++viewIndex)
                {
                    const RenderView& view = views[viewIndex];
                    const RenderViewGraphKey& viewKey = graphKey.views[viewIndex];
                    RenderGraphCache::CameraSetupData* const storage = graphCacheEntry->FindCameraSetup(viewKey);
                    if (storage == nullptr)
                    {
                        graphCacheEntry->Reset();
                        return RenderFrameExecutionStatus::Failure("render graph cache has no storage for a prepared view");
                    }

                    if (storage->DoesNeedRebuild(viewKey))
                    {
                        switch (frame.GetMode())
                        {
                        case RenderingMode::Shaded:
                        case RenderingMode::ShadedNoAmbient:
                            if (!family->GetScene().IsValid())
                                BuildRenderGraphNoScene(storage->graph, storage->nodes, features.wireframe);
                            else if (features.debugView == RenderDebugView::Todvis)
                                BuildRenderGraphTodvis(storage->graph, storage->nodes, true);
                            else if (features.debugView == RenderDebugView::Visualization)
                                BuildRenderGraphDebugVisualization(storage->graph, storage->nodes, view);
                            else if (features.debugView == RenderDebugView::HitProxies)
                                BuildRenderGraphHitProxies(storage->graph, storage->nodes, false, true);
                            else
                                BuildRenderGraphCamera(storage->graph, storage->nodes, view, false, features.geometryDiagnostics);
                            break;
                        case RenderingMode::Selection:
                            BuildRenderGraphHitProxies(storage->graph, storage->nodes, features.multilayerSelection, false);
                            break;
                        case RenderingMode::SafeMode:
                            if (family->GetScene().IsValid())
                                BuildRenderGraphSafeMode(storage->graph, storage->nodes, view);
                            else
                                BuildRenderGraphNoScene(storage->graph, storage->nodes, features.wireframe);
                            break;
                        case RenderingMode::GBufferOnly:
                            BuildRenderGraphGBufferOnly(storage->graph, storage->nodes, features.geometryDiagnostics);
                            break;
                        case RenderingMode::TodvisBake:
                            BuildRenderGraphTodvis(storage->graph, storage->nodes, false);
                            break;
                        default:
                            graphCacheEntry->Reset();
                            return RenderFrameExecutionStatus::Failure("render frame selected an unsupported rendering mode");
                        }
                        if (storage->graph.GetNumNodes() == 0)
                        {
                            graphCacheEntry->Reset();
                            return RenderFrameExecutionStatus::Failure("selected per-view render graph builder is not implemented");
                        }
                    }

                    graphCacheEntry->graph.AddGraph(storage->graph, true, viewIndex, true);
                }
            }
            if (!isBlankFrame && graphKey.dependencyCount != 0)
            {
                auto& graph = graphCacheEntry->graph;
                RenderNodeGraph::ItemId cameraNodes[MaximumRenderViewsPerFamily];
                for (auto& node : cameraNodes) node = RenderNodeGraph::InvalidItemId;
                for (u32 index = 0; index < graph.GetNumNodes(); ++index)
                {
                    const auto node = graph.GetNode(index);
                    const auto& parameters = graph.GetNodeParameters(node);
                    const i32 camera = graph.GetNodeContext(node).m_cameraIndex;
                    if (camera >= 0 && static_cast<u32>(camera) < cameraCount && parameters.m_impl != nullptr &&
                        std::strcmp(parameters.m_impl->GetName(), "CameraGeometry") == 0)
                        cameraNodes[camera] = node;
                }
                // Only actual producer/consumer pairs are ordered. Independent
                // camera command lists retain parallel recording and GPU branches.
                for (u32 index = 0; index < graphKey.dependencyCount; ++index)
                {
                    const auto& dependency = graphKey.dependencies[index];
                    const auto producer = cameraNodes[dependency.childViewIndex];
                    const auto consumer = cameraNodes[dependency.parentViewIndex];
                    if (producer == RenderNodeGraph::InvalidItemId || consumer == RenderNodeGraph::InvalidItemId)
                    {
                        graphCacheEntry->Reset();
                        return RenderFrameExecutionStatus::Failure("camera dependency requires implemented camera graph endpoints");
                    }
                    static_cast<void>(graph.AddDependency(producer, consumer, RenderNodeDependencyType::Gpu));
                }
            }
            graphCacheEntry->graph.BuildRenderFlowGroups();
            graphCacheEntry->PostBuildClear();
        }

        FrameResourcePolicy resourcePolicy;
        resourcePolicy.enablePlacedResources = features.enablePlacedResources;
        resourcePolicy.processEviction = !features.gameMode || (frame.GetOutputKind() == RenderViewportOutputKind::Presentation && !isBlankFrame);
        return ExecuteBuiltGraph(context, graphCacheEntry->graph, resourcePolicy);
    }

    RenderFrameExecutionStatus FrameRenderer::ExecuteFrameTick(RenderFrameTickContext& context, void* const userData) noexcept
    {
        if (userData == nullptr)
            return RenderFrameExecutionStatus::Failure("FrameRenderer FrameTick target is unavailable");
        return static_cast<FrameRenderer*>(userData)->FrameTick(context);
    }

    RenderFrameExecutionStatus FrameRenderer::ExecuteFrame(RenderFrameContext& context, void* const userData) noexcept
    {
        if (userData == nullptr)
            return RenderFrameExecutionStatus::Failure("FrameRenderer RenderFrame target is unavailable");
        return static_cast<FrameRenderer*>(userData)->RenderFrame(context);
    }

    RenderFrameExecutionStatus FrameRenderer::PrepareViewFamily(const RenderFrameInfo& frame, PreparedRenderViewFamily& preparedFamily,
                                                                 const PreparedRenderViewFamily*& family) noexcept
    {
        if (family != nullptr || !frame.HasViewSetup())
            return RenderFrameExecutionStatus::Success();

        const RenderFrameViewSetup setup = frame.GetViewSetup();
        const RenderViewFamilyPrepareRequest request{setup.scene,
                                                     setup.rootCameras,
                                                     {frame.GetRenderExtent().width, frame.GetRenderExtent().height},
                                                     frame.GetSerial(),
                                                     setup.jitterIndex,
                                                     setup.enableTemporalJitter,
                                                     setup.forceCameraCut,
                                                     setup.outputRegions,
                                                     {frame.GetOutputExtent().width, frame.GetOutputExtent().height}};
        RenderCameraFailure failure;
        const bool familyPrepared = m_cameras.PrepareViewFamily(request, preparedFamily, &failure);
        if (!familyPrepared)
            return RenderFrameExecutionStatus::Failure(FailureMessage(failure.message, "render frame view-family preparation failed"));

        family = &preparedFamily;
        return RenderFrameExecutionStatus::Success();
    }

    RenderFrameExecutionStatus FrameRenderer::PrepareGeometryWork(RenderFrameContext& context, const PreparedRenderViewFamily& family) noexcept
    {
        const containers::ArraySpan<const RenderView> views = family.GetViews();
        if (views.Empty() || views.Size() > MaximumRenderViewsPerFamily || family.GetFamily().frameSerial != context.GetFrame().GetSerial())
            return RenderFrameExecutionStatus::Failure("geometry candidate production requires the current prepared view family");
        RetainedRenderFrameRef retainedFrame = context.RetainFrame();
        GeometryFrameWork& work = retainedFrame.GetGeometryFrameWork();
        if (work.IsPrepared())
            return RenderFrameExecutionStatus::Failure("geometry frame work is already prepared");

        const RenderViewFamily& viewFamily = family.GetFamily();
        VisibilityQueryRequest request;
        request.scene = family.GetScene();
        request.mutationEpoch = viewFamily.sceneVersion;
        const f32 worldCellSize = m_gpuScene.GetScenePublisher().GetWorldCellSize();
        if (!BuildSceneQueryFrustum(views[0], worldCellSize, request.frustum))
            return RenderFrameExecutionStatus::Failure("geometry CPU frustum cannot be represented in scene space");
        request.layerMask = views[0].layerMask;
        request.visibilityMask = views[0].visibilityMask;
        request.requiredFlags = RenderProxyVisibilityFlags::Visible;
        request.excludedFlags = RenderProxyVisibilityFlags::QueryOnly;
        request.payloadFilter = VisibilityQueryPayloadFilter::Mesh;
        request.maximumResults = ~u32{0};
        request.useBounds = false;
        request.useFrustum = true;

        RenderSceneGpuCandidatePlan candidatePlan;
        RenderSceneFailure sceneFailure;
        if (!m_scenes.PrepareGpuVisibilityCandidatePlan(request, GeometryCandidateTargetBatchSize, views, candidatePlan, &sceneFailure))
            return RenderFrameExecutionStatus::Failure(FailureMessage(sceneFailure.message, "geometry candidate planning failed"));

        const u32 visibleCapacity = candidatePlan.requiredCandidateCapacity != 0 ? candidatePlan.requiredCandidateCapacity : 1u;
        // This cleanup is only used before dispatch. Once writers start, their
        // joined epilogue owns both seal release and terminal frame completion.
        const auto failBeforeDispatch = [&](const char* message) noexcept -> RenderFrameExecutionStatus
        {
            if (work.IsPrepared())
                work.GetVisibilityBuilder().Cancel();
            RenderSceneFailure completionFailure;
            if (!m_scenes.CompleteGpuVisibilityCandidates(candidatePlan, &completionFailure))
                message = FailureMessage(completionFailure.message, "geometry candidate seal release failed");
            work.GetCandidatePlan() = {};
            return RenderFrameExecutionStatus::Failure(message);
        };

        if (m_geometryConfig.maximumWorkItemsPerVisible == 0 || m_geometryBinCapacity == 0 || m_geometryBinCapacity == InvalidGpuSceneIndex)
            return failBeforeDispatch("geometry frame-work capacity policy is invalid");
        if (candidatePlan.requiredCandidateCapacity != 0 && m_geometryBatcher == nullptr)
            return failBeforeDispatch("mesh candidates require the residency-owned geometry batcher");
        if (m_geometryBatcher != nullptr && (m_geometryShellCapacity == 0 || m_geometryShellCapacity == InvalidGpuSceneIndex))
            return failBeforeDispatch("geometry shell capacity policy is invalid");

        const u64 totalCandidates = static_cast<u64>(views.Size()) * candidatePlan.requiredCandidateCapacity;
        const u64 totalWorkRanges = static_cast<u64>(views.Size()) * candidatePlan.requiredWorkRangeCapacity;
        const u64 totalVisible = static_cast<u64>(views.Size()) * visibleCapacity;
        const u64 workPerView = static_cast<u64>(visibleCapacity) * m_geometryConfig.maximumWorkItemsPerVisible;
        if (workPerView >= InvalidGpuSceneIndex)
            return failBeforeDispatch("geometry per-view work capacity overflowed");
        const u64 totalGeometryWork = static_cast<u64>(views.Size()) * workPerView;
        const u64 totalExpansionRanges = static_cast<u64>(views.Size()) *
                                         ((static_cast<u64>(visibleCapacity) + GpuVisibilityThreadsPerGroup - 1u) / GpuVisibilityThreadsPerGroup);
        const u64 totalGeometryRanges = static_cast<u64>(views.Size()) *
                                        ((workPerView + GpuVisibilityThreadsPerGroup - 1u) / GpuVisibilityThreadsPerGroup);
        const u64 totalGeometryBins = static_cast<u64>(views.Size()) * m_geometryBinCapacity;
        const u64 totalBinPrefixRanges = static_cast<u64>(views.Size()) *
                                         ((static_cast<u64>(m_geometryBinCapacity) + GpuVisibilityThreadsPerGroup - 1u) /
                                          GpuVisibilityThreadsPerGroup);
        const u64 totalPrefixBlocks = totalExpansionRanges + totalBinPrefixRanges;
        const u64 totalWrites = static_cast<u64>(views.Size()) * candidatePlan.batchCount;
        const u64 totalShellDraws = m_geometryBatcher != nullptr
                                       ? static_cast<u64>(views.Size()) * m_geometryBatcher->GetCatalogStats().activeShells : 0;
        const u64 totalShellSlots = static_cast<u64>(views.Size()) * m_geometryShellCapacity;
        if (totalWorkRanges > 65535u || totalExpansionRanges > 65535u || totalGeometryRanges > 65535u ||
            totalBinPrefixRanges > 65535u || (totalShellDraws + 127u) / 128u > 65535u)
            return failBeforeDispatch("geometry work exceeds the current one-dimensional GPU dispatch capacity");
        if (totalCandidates >= InvalidGpuSceneIndex || totalWorkRanges >= InvalidGpuSceneIndex || totalVisible >= InvalidGpuSceneIndex ||
            totalExpansionRanges >= InvalidGpuSceneIndex || totalGeometryWork >= InvalidGpuSceneIndex ||
            totalGeometryRanges >= InvalidGpuSceneIndex || totalGeometryBins >= InvalidGpuSceneIndex ||
            totalBinPrefixRanges >= InvalidGpuSceneIndex || totalPrefixBlocks >= InvalidGpuSceneIndex || totalWrites >= InvalidGpuSceneIndex ||
            totalShellDraws * sizeof(GpuGeometryShellCounters) > 0xffffffffull || totalShellSlots >= InvalidGpuSceneIndex)
            return failBeforeDispatch("geometry view-family capacity calculation overflowed");
        const GeometryFrameWorkCapacity capacity{views.Size(), static_cast<u32>(totalCandidates), static_cast<u32>(totalWorkRanges),
                                                 static_cast<u32>(totalVisible), static_cast<u32>(totalExpansionRanges),
                                                 static_cast<u32>(totalGeometryWork), static_cast<u32>(totalGeometryRanges),
                                                 static_cast<u32>(totalGeometryBins), static_cast<u32>(totalBinPrefixRanges),
                                                 static_cast<u32>(totalPrefixBlocks), candidatePlan.batchCount,
                                                 static_cast<u32>(totalShellDraws), m_geometryShellCapacity};
        if (work.Prepare(context.GetFrame().GetSerial(), capacity) != GeometryFrameWorkResult::Success)
            return failBeforeDispatch("retained geometry frame-work allocation failed");
        work.GetCandidatePlan() = candidatePlan;
        if (context.GetFrame().GetMode() == RenderingMode::Shaded || context.GetFrame().GetMode() == RenderingMode::ShadedNoAmbient)
        {
            const bool lightsSelected = m_scenes.CollectDirectionalLights(candidatePlan, views, work.GetDirectionalLights(), &sceneFailure);
            if (!lightsSelected)
                return failBeforeDispatch(FailureMessage(sceneFailure.message, "directional light selection failed"));
        }
        for (const auto& dependency : family.GetDependencies())
        {
            u32 producer = 0, consumer = 0;
            const bool producerFound = FindViewIndex(views, dependency.child, producer);
            const bool consumerFound = FindViewIndex(views, dependency.parent, consumer);
            if (!producerFound || !consumerFound || producer >= consumer)
                return failBeforeDispatch("camera dependency family is not in producer-before-consumer order");
            // The simple path's final camera result is CameraColor as well.
            // A future post-processing path must split Final at its producer.
            if (dependency.outputs != RenderCameraDependencyOutputs::None)
                work.AddCameraInput(consumer, producer);
        }
        const auto& frameInfo = context.GetFrame();
        const auto setup = frameInfo.GetViewSetup();
        if (!setup.outputRegions.Empty())
        {
            for (const auto& region : setup.outputRegions)
            {
                u32 viewIndex = 0;
                const bool found = FindViewIndex(views, region.camera, viewIndex);
                if (!found) return failBeforeDispatch("output region camera is disabled or absent from the prepared family");
                const bool added = work.AddOutputRegion({viewIndex, region.rect});
                if (!added) return failBeforeDispatch("geometry output region capacity exceeded");
            }
        }
        else
        {
            u32 outputView = 0;
            if (!setup.rootCameras.Empty())
            {
                const bool found = FindViewIndex(views, setup.rootCameras[0], outputView);
                if (!found) return failBeforeDispatch("frame output camera is not part of the prepared family");
            }
            bool foundPrimary = false;
            for (u32 index = 0; index < views.Size(); ++index)
            {
                if ((views[index].flags & RenderViewFlags::Primary) == RenderViewFlags::None) continue;
                // A dependency camera must not steal its parent's output.
                if (!setup.rootCameras.Empty())
                {
                    bool isRoot = false;
                    for (const auto root : setup.rootCameras)
                        isRoot |= views[index].id.index == root.index && views[index].id.generation == root.generation;
                    if (!isRoot) continue;
                }
                if (foundPrimary) return failBeforeDispatch("multiple primary output cameras require explicit output regions");
                foundPrimary = true;
                outputView = index;
            }
            const auto extent = frameInfo.GetOutputExtent();
            const bool added = work.AddOutputRegion({outputView, {0, 0, extent.width, extent.height}});
            if (!added) return failBeforeDispatch("frame output extent is invalid");
        }
        const bool batchesBuilt = m_scenes.BuildGpuVisibilityCandidateBatches(candidatePlan, work.GetCandidateBatches(), &sceneFailure);
        if (!batchesBuilt)
            return failBeforeDispatch(FailureMessage(sceneFailure.message, "geometry candidate batch construction failed"));

        GpuVisibilityBuildFailure visibilityFailure;
        GpuVisibilityPlanBuilder& visibilityBuilder = work.GetVisibilityBuilder();
        const bool visibilityBegun = visibilityBuilder.Begin(work.GetVisibilityBuildStorage(), context.GetFrame().GetSerial(), &visibilityFailure);
        if (!visibilityBegun)
            return failBeforeDispatch(FailureMessage(visibilityFailure.message, "geometry visibility planning failed"));
        const auto candidateViews = work.GetCandidateViews();
        for (u32 viewIndex = 0; viewIndex < views.Size(); ++viewIndex)
        {
            const RenderView& view = views[viewIndex];
            GeometryCandidateView& candidateView = candidateViews[viewIndex];
            candidateView.request = request;
            if (viewIndex != 0)
            {
                const bool frustumBuilt = BuildSceneQueryFrustum(view, worldCellSize, candidateView.request.frustum);
                if (!frustumBuilt) return failBeforeDispatch("geometry CPU frustum cannot be represented in scene space");
            }
            candidateView.request.layerMask = view.layerMask;
            candidateView.request.visibilityMask = view.visibilityMask;
            GpuView gpuView;
            if (view.frameSerial != context.GetFrame().GetSerial())
                return failBeforeDispatch("geometry view belongs to another frame");
            const bool gpuViewBuilt = BuildGpuView(view, gpuView);
            if (!gpuViewBuilt) return failBeforeDispatch("geometry GPU view is invalid");
            const bool rangesReserved = visibilityBuilder.ReserveViewRanges(gpuView, candidatePlan.requiredCandidateCapacity, candidatePlan.requiredWorkRangeCapacity,
                                                                           visibleCapacity, candidateView.reservation, &visibilityFailure);
            if (!rangesReserved)
                return failBeforeDispatch(FailureMessage(visibilityFailure.message, "geometry visibility view reservation failed"));
        }

        if (candidatePlan.batchCount == 0)
        {
            FinishGeometryCandidates(retainedFrame);
            return retainedFrame.HasFailure() ? RenderFrameExecutionStatus::Failure(retainedFrame.GetFailureMessage()) : RenderPreparedFrame(context);
        }

        GeometryFrameWork* const workPointer = &work;
        const u32 writeCount = static_cast<u32>(totalWrites);
        const u32 groupCount = writeCount < jobs::GetWorkerCount() + 1u ? writeCount : jobs::GetWorkerCount() + 1u;
        jobs::ParallelTask writers = jobs::ParallelTask::Create(
            [scenes = &m_scenes, workPointer, groupCount, writeCount](const u32 group, const jobs::JobContext&) noexcept
            {
                const RenderSceneGpuCandidatePlan& plan = workPointer->GetCandidatePlan();
                const auto batches = workPointer->GetCandidateBatches();
                auto ranges = workPointer->GetCandidateRanges();
                auto results = workPointer->GetCandidateResults();
                auto failures = workPointer->GetCandidateFailures();
                const auto viewWork = workPointer->GetCandidateViews();
                // Contiguous balanced partitions keep neighboring result slots
                // together. No shared append cursor or per-candidate retain.
                const u32 first = static_cast<u32>(static_cast<u64>(writeCount) * group / groupCount);
                const u32 end = static_cast<u32>(static_cast<u64>(writeCount) * (group + 1u) / groupCount);
                for (u32 writeIndex = first; writeIndex < end; ++writeIndex)
                {
                    const GeometryCandidateView& view = viewWork[writeIndex / plan.batchCount];
                    const RenderSceneGpuCandidateBatch& batch = batches[writeIndex % plan.batchCount];
                    static_cast<void>(scenes->WriteGpuVisibilityCandidateBatch(plan, view.request, batch, view.reservation, ranges[writeIndex],
                                                                               results[writeIndex], &failures[writeIndex]));
                }
            });
        jobs::Task epilogue = jobs::Task::Create([this, retainedFrame](const jobs::JobContext& jobContext) mutable noexcept
        {
            FinishGeometryCandidates(retainedFrame);
            if (retainedFrame.HasFailure())
            {
                retainedFrame.PublishTerminalCompletion();
                return;
            }
            // Graph construction and resource preparation consume the finished
            // family plan, so they must execute inside this joined continuation.
            RenderFrameContext continuation(retainedFrame, jobContext);
            const RenderFrameExecutionStatus status = continuation.IsValid() ? RenderPreparedFrame(continuation) :
                RenderFrameExecutionStatus::Failure("geometry graph continuation builder allocation failed");
            if (status.IsFailure())
            {
                retainedFrame.RecordFailure(status.message);
                retainedFrame.PublishTerminalCompletion();
            }
            else if (status.IsSkipped())
                retainedFrame.PublishTerminalCompletion(true);
        });
        if (!writers || !epilogue)
            return failBeforeDispatch("geometry candidate job allocation failed");

        static jobs::JobName candidateWriteName{"Geometry.PrepareCandidates"};
        retainedFrame.DeferTerminalCompletion();
        if (!context.GetBuilder().DispatchParallel(candidateWriteName, groupCount, static_cast<jobs::ParallelTask&&>(writers),
                                                   static_cast<jobs::Task&&>(epilogue), 1u))
            return failBeforeDispatch("geometry candidate jobs could not be dispatched");
        return RenderFrameExecutionStatus::Success();
    }

    void FrameRenderer::FinishGeometryCandidates(RetainedRenderFrameRef& retainedFrame) noexcept
    {
        GeometryFrameWork& work = retainedFrame.GetGeometryFrameWork();
        const RenderSceneGpuCandidatePlan& candidatePlan = work.GetCandidatePlan();
        const auto results = work.GetCandidateResults();
        const auto failures = work.GetCandidateFailures();
        const bool collectDiagnostics = retainedFrame.GetFrame().GetFeatures().geometryDiagnostics;
        const char* failureMessage = nullptr;
        for (u32 writeIndex = 0; writeIndex < results.Size(); ++writeIndex)
        {
            if (results[writeIndex].completed && failures[writeIndex].code == RenderSceneFailureCode::None)
            {
                if (collectDiagnostics)
                {
                    auto& view = work.GetCandidateViews()[writeIndex / candidatePlan.batchCount];
                    view.diagnosticCandidates += work.GetCandidateRanges()[writeIndex].count;
                    view.diagnosticUnresolvedIdentities += results[writeIndex].unresolvedGpuIdentities;
                }
                continue;
            }
            failureMessage = FailureMessage(failures[writeIndex].message, "geometry candidate worker failed");
            break;
        }

        GpuVisibilityPlanBuilder& visibilityBuilder = work.GetVisibilityBuilder();
        if (failureMessage == nullptr)
        {
            GpuVisibilityBuildFailure visibilityFailure;
            const auto ranges = work.GetCandidateRanges();
            const auto candidateViews = work.GetCandidateViews();
            for (u32 viewIndex = 0; viewIndex < candidateViews.Size(); ++viewIndex)
            {
                const auto* const firstRange = candidatePlan.batchCount != 0 ? ranges.Data() + viewIndex * candidatePlan.batchCount : nullptr;
                const bool rangesCompleted = visibilityBuilder.CompleteViewRanges(candidateViews[viewIndex].reservation, {firstRange, candidatePlan.batchCount}, &visibilityFailure);
                if (!rangesCompleted)
                {
                    failureMessage = FailureMessage(visibilityFailure.message, "geometry candidate range completion failed");
                    break;
                }
            }
            if (failureMessage == nullptr)
            {
                GpuVisibilityPlan visibilityPlan;
                const bool finalized = visibilityBuilder.Finalize(visibilityPlan, &visibilityFailure);
                if (!finalized)
                    failureMessage = FailureMessage(visibilityFailure.message, "geometry visibility plan finalization failed");
                else
                {
                    const auto committed = work.CommitVisibilityPlan(visibilityPlan);
                    if (committed != GeometryFrameWorkResult::Success)
                        failureMessage = "geometry visibility plan does not belong to its retained frame storage";
                    else
                    {
                        const auto planned = work.PlanShellDraws(m_geometryBatcher);
                        if (planned == GeometryFrameWorkResult::IncompatibleDepthConvention)
                            failureMessage = "camera depth convention does not match the cooked geometry pipeline; matching PSO variants are required";
                        else if (planned != GeometryFrameWorkResult::Success)
                            failureMessage = "geometry shell planning exceeded capacity or observed an invalid catalog";
                    }
                }
            }
        }
        RenderSceneFailure completionFailure;
        const bool candidatesCompleted = m_scenes.CompleteGpuVisibilityCandidates(candidatePlan, &completionFailure);
        if (!candidatesCompleted && failureMessage == nullptr)
            failureMessage = FailureMessage(completionFailure.message, "geometry candidate seal release failed");
        work.GetCandidatePlan() = {};
        if (failureMessage != nullptr)
        {
            visibilityBuilder.Cancel();
            work.ClearVisibilityPlan();
            retainedFrame.RecordFailure(failureMessage);
        }
    }

    void FrameRenderer::FinishBuiltGraph(RetainedRenderFrameRef& retainedFrame, RenderNodeGraph& graph) noexcept
    {
        // Called after the node branch and its continuations, or after setup
        // failed before dispatching any node and the kickoff gate has completed.
        // Allocator abandonment below must never serve as concurrent cancellation.
        RenderFrameCommandLists& commandLists = retainedFrame.GetFrameCommandLists();
        rhi::Failure submissionFailure;
        if (commandLists.GetFirstSubmissionFailure(submissionFailure))
            retainedFrame.RecordFailure(FailureMessage(submissionFailure.message, "render-frame recording or submission failed"));
        RenderFlowResourceFailure executionFailure;
        if (retainedFrame.GetResourceBindings().GetFirstExecutionFailure(executionFailure))
            retainedFrame.RecordFailure(executionFailure);

        commandLists.FinalizeSubmissions(retainedFrame.HasFailure());
        if (commandLists.GetFirstSubmissionFailure(submissionFailure))
            retainedFrame.RecordFailure(FailureMessage(submissionFailure.message, "render-frame command-list submission failed"));

        bool allocatorCompleted = false;
        const ExecutionGenerationId generation = m_resourceAllocator.GetExecutionGeneration();
        if (generation.IsValid())
        {
            const TerminalExecutionCompletionKind completion = commandLists.WasDeviceLost() ? TerminalExecutionCompletionKind::DeviceLost :
                                                               retainedFrame.HasFailure()     ? TerminalExecutionCompletionKind::Aborted :
                                                                                               TerminalExecutionCompletionKind::Completed;
            const TerminalExecutionReceipt receipt{generation, completion, commandLists.GetCommandScopeReceipts(), TerminalJoinToken::CompletedSynchronously(generation),
                                                   commandLists.GetQueueDependencyReceipts()};
            RenderFlowResourceFailure terminalFailure;
            if (!m_resourceAllocator.Finish(receipt, &terminalFailure))
            {
                retainedFrame.RecordFailure(terminalFailure);
                m_resourceAllocator.AbandonPublishedExecution();
            }
            else if (completion == TerminalExecutionCompletionKind::Completed)
            {
                allocatorCompleted = true;
                const PreparedRenderViewFamily& family = retainedFrame.GetFrame().GetViewFamily().IsValid() ? retainedFrame.GetFrame().GetViewFamily() :
                                                                                                             retainedFrame.GetPreparedViewFamily();
                if (family.IsValid())
                    family.Commit();
            }
        }
        else
        {
            m_resourceAllocator.CancelBeforePublication();
        }

        RenderFrameOutputTransaction& output = retainedFrame.GetOutputTransaction();
        if (output.IsValid())
        {
            if (commandLists.WasDeviceLost())
                output.m_viewport->DeviceLostOutput(output.m_acquisition);
            else if (!allocatorCompleted || retainedFrame.HasFailure() || !output.WasPresentNodeReached())
            {
                if (!output.WasPresentNodeReached() && !retainedFrame.HasFailure())
                    retainedFrame.RecordFailure("render graph did not execute its terminal output node");
                ViewportFailure outputFailure;
                if (!output.m_viewport->AbandonOutput(output.m_acquisition, &outputFailure))
                {
                    retainedFrame.RecordFailure(FailureMessage(outputFailure.message, "render-frame output abandonment failed"));
                    output.m_viewport->DeviceLostOutput(output.m_acquisition);
                }
            }
            else if (output.GetKind() == RenderViewportOutputKind::Texture)
            {
                ViewportFailure outputFailure;
                if (!output.m_viewport->CompleteOutput(output.m_acquisition, &outputFailure))
                    retainedFrame.RecordFailure(FailureMessage(outputFailure.message, "render-frame texture output completion failed"));
            }
            else
            {
                ViewportFailure outputFailure;
                if (!output.m_viewport->Present(output.m_acquisition, &outputFailure))
                {
                    retainedFrame.RecordFailure(FailureMessage(outputFailure.message, "render-frame presentation failed"));
                    output.m_viewport->DeviceLostOutput(output.m_acquisition);
                }
            }
            if (!output.m_acquisition.IsValid())
                output.m_viewport = nullptr;
        }

        auto& diagnosticWork = retainedFrame.GetGeometryFrameWork();
        m_geometryDiagnostics.Complete(diagnosticWork.diagnosticSlot, diagnosticWork, commandLists.GetCommandScopeReceipts(), retainedFrame.HasFailure());
        diagnosticWork.diagnosticSlot = GeometryDiagnosticReadbacks::InvalidSlot;
        graph.ReleaseExclusiveUpdateFlag();
        retainedFrame.ClearJobsRenderFrame();
        retainedFrame.PublishTerminalCompletion();
    }

    RenderFrameExecutionStatus FrameRenderer::ExecuteBuiltGraph(RenderFrameContext& context, RenderNodeGraph& graph,
                                                                 const FrameResourcePolicy& resourcePolicy) noexcept
    {
        RetainedRenderFrameRef retainedFrame = context.RetainFrame();
        const RenderFrameInfo& frame = retainedFrame.GetFrame();
        RenderFrameCommandLists& frameCommandLists = retainedFrame.GetFrameCommandLists();
        RenderNodeResourceBindings& resourceBindings = retainedFrame.GetResourceBindings();
        RenderNodeResourcePreparationFailures& preparationFailures = retainedFrame.GetResourcePreparationFailures();
        const PreparedRenderViewFamily* const family = frame.GetViewFamily().IsValid() ? &frame.GetViewFamily() :
                                                                                       (retainedFrame.GetPreparedViewFamily().IsValid() ? &retainedFrame.GetPreparedViewFamily() : nullptr);

        frameCommandLists.PrepareForFrame(graph.GetNumNodes() + static_cast<u32>(ReservedFrameCommandList::Count));
        RenderNodeImplContext declarationContext;
        RenderNodeImplContext::InitData declarationInitData(context.GetDispatcherThreadIndex());
        declarationInitData.frame = &frame;
        declarationInitData.viewFamily = family;
        declarationInitData.cameraStorage = &m_cameras;
        declarationInitData.geometryFrameWork = &retainedFrame.GetGeometryFrameWork();
        declarationInitData.frameCommandLists = &frameCommandLists;
        declarationInitData.outputTransaction = &retainedFrame.GetOutputTransaction();
        declarationContext.Init(declarationInitData);

        jobs::Builder kickoffBuilder({jobs::Priority::RenderPath, jobs::Affinity::AnyWorker});
        jobs::Builder renderNodeJobsBuilder({jobs::Priority::RenderPath, jobs::Affinity::AnyWorker});
        if (!kickoffBuilder.IsValid() || !renderNodeJobsBuilder.IsValid())
            return RenderFrameExecutionStatus::Failure("render-graph branch builder allocation failed");
        jobs::Counter kickoffCounter = kickoffBuilder.ExtractCounter();
        if (!kickoffCounter.IsValid())
            return RenderFrameExecutionStatus::Failure("render-node kickoff counter allocation failed");
        jobs::CompletionDeferral kickoffDeferral = kickoffCounter.CreateDeferral("RenderGraph/NodesKickoff", &frame);
        if (!kickoffDeferral.IsValid())
            return RenderFrameExecutionStatus::Failure("render-node kickoff deferral allocation failed");
        // Keep failure cleanup behind the dispatching thread as well as resource resolution.
        jobs::CompletionDeferral setupDeferral = kickoffCounter.CreateDeferral("RenderGraph/FrameSetup", &frame);
        if (!setupDeferral.IsValid())
            return RenderFrameExecutionStatus::Failure("render-frame setup deferral allocation failed");

        jobs::Task nodesCompleteTask = jobs::Task::Create([this, retainedFrame, graphPointer = &graph](const jobs::JobContext&) mutable noexcept
        {
            FinishBuiltGraph(retainedFrame, *graphPointer);
        });
        if (!nodesCompleteTask)
            return RenderFrameExecutionStatus::Failure("render-node completion task allocation failed");
        jobs::Task runRenderNodeJobsTask = jobs::Task::Create(
            [this, retainedFrame, graphPointer = &graph, kickoffCounter = static_cast<jobs::Counter&&>(kickoffCounter),
             nodesCompleteTask = static_cast<jobs::Task&&>(nodesCompleteTask)](const jobs::JobContext& jobContext) mutable noexcept
            {
                RenderNodeImplContext baseContext;
                RenderNodeImplContext::InitData initData(jobContext.dispatcherThreadIndex);
                initData.frame = &retainedFrame.GetFrame();
                initData.viewFamily = retainedFrame.GetFrame().GetViewFamily().IsValid() ? &retainedFrame.GetFrame().GetViewFamily() : (retainedFrame.GetPreparedViewFamily().IsValid() ? &retainedFrame.GetPreparedViewFamily() : nullptr);
                initData.cameraStorage = &m_cameras;
                initData.geometryFrameWork = &retainedFrame.GetGeometryFrameWork();
                initData.frameCommandLists = &retainedFrame.GetFrameCommandLists();
                initData.outputTransaction = &retainedFrame.GetOutputTransaction();
                baseContext.Init(initData);

                RenderFlowResourceFailure schedulingFailure;
                if (RenderNodeJob::RunRenderNodeJobs(*graphPointer, baseContext, retainedFrame.GetResourceBindings(), kickoffCounter, jobContext,
                                                     static_cast<jobs::Task&&>(nodesCompleteTask), &schedulingFailure))
                    return;
                retainedFrame.RecordFailure(schedulingFailure);
                if (!kickoffCounter.Wait())
                    VG_FATAL("render-node setup failure could not join frame preparation before cleanup");
                FinishBuiltGraph(retainedFrame, *graphPointer);
            });
        jobs::Task resolveResourcesTask = jobs::Task::Create([this, retainedFrame](const jobs::JobContext&) mutable noexcept
        {
            RenderNodeResourcePreparationFailures& failures = retainedFrame.GetResourcePreparationFailures();
            if (retainedFrame.HasFailure() || failures.HasFailure())
            {
                if (failures.HasFailure())
                    retainedFrame.RecordFailure(failures.GetFailure());
                m_resourceAllocator.CancelBeforePublication();
                return;
            }

            RenderFlowResourceFailure resourceFailure;
            if (!m_resourceAllocator.SealPlanning(PlanningJoinToken::CompletedSynchronously(), &resourceFailure))
            {
                retainedFrame.RecordFailure(resourceFailure);
                m_resourceAllocator.CancelBeforePublication();
                return;
            }
            if (!m_resourceAllocator.Resolve(nullptr, &resourceFailure))
            {
                retainedFrame.RecordFailure(resourceFailure);
                m_resourceAllocator.CancelBeforePublication();
            }
        });
        jobs::Task finishResourceResolutionTask = jobs::Task::Create(
            [this, retainedFrame, graphPointer = &graph, kickoffDeferral = static_cast<jobs::CompletionDeferral&&>(kickoffDeferral)](const jobs::JobContext&) mutable noexcept
            {
                RenderFlowResourceFailure resourceFailure;
                if (!retainedFrame.HasFailure() && !m_resourceAllocator.BeginExecution(&resourceFailure))
                    retainedFrame.RecordFailure(resourceFailure);
                if (!retainedFrame.HasFailure() && !graphPointer->PrepareExecutionPackets(m_resourceAllocator, retainedFrame.GetFrameCommandLists(), retainedFrame.GetResourceBindings(), &resourceFailure))
                    retainedFrame.RecordFailure(resourceFailure);
                if (retainedFrame.HasFailure())
                    retainedFrame.GetResourceBindings().BlockExecution(RenderFlowResourceFailureCode::IncompleteExecution,
                                                                        "render-node execution was canceled before allocator publication completed");
                kickoffDeferral.Finish();
            });
        if (!runRenderNodeJobsTask || !resolveResourcesTask || !finishResourceResolutionTask)
            return RenderFrameExecutionStatus::Failure("render-graph orchestration task allocation failed");

        RenderFlowResourceFailure resourceFailure;
        if (!m_resourceAllocator.BeginFrame(frame.GetSerial(), resourcePolicy, &resourceFailure))
            return RenderFrameExecutionStatus::Failure(FailureMessage(resourceFailure.message, "render-flow allocator frame startup failed"));
        RenderFrameOutputTransaction& output = retainedFrame.GetOutputTransaction();
        if (output.IsValid() && output.GetKind() == RenderViewportOutputKind::Presentation)
        {
            ImportedResourceId imported;
            if (!m_resourceAllocator.RegisterPresentationImport(output.GetBackBuffer(), imported, &resourceFailure))
            {
                m_resourceAllocator.CancelBeforePublication();
                return RenderFrameExecutionStatus::Failure(FailureMessage(resourceFailure.message, "render-flow presentation import failed"));
            }
            output.m_resourceImportIndex = imported.index;
            output.m_resourceImportGeneration = imported.generation;
        }
        else if (output.IsValid() && output.GetKind() == RenderViewportOutputKind::Texture)
        {
            RetainedTextureImportDesc desc;
            desc.texture = output.GetTexture();
            desc.token = {rhi::ResourceRef(desc.texture).value};
            rhi::Failure nativeFailure;
            if (!rhi::GetTextureDesc(desc.texture, desc.expected, &nativeFailure))
            {
                m_resourceAllocator.CancelBeforePublication();
                return RenderFrameExecutionStatus::Failure("render-flow texture output is unavailable");
            }
            desc.initialState = desc.expected.initialState;
            desc.terminalState = desc.expected.initialState;
            ImportedResourceId imported;
            if (!m_resourceAllocator.RegisterImport(desc, imported, &resourceFailure))
            {
                m_resourceAllocator.CancelBeforePublication();
                return RenderFrameExecutionStatus::Failure(FailureMessage(resourceFailure.message, "render-flow texture output import failed"));
            }
            output.m_resourceImportIndex = imported.index;
            output.m_resourceImportGeneration = imported.generation;
        }
        if (family != nullptr && family->GetScene().IsValid() &&
            (frame.GetMode() == RenderingMode::Shaded || frame.GetMode() == RenderingMode::ShadedNoAmbient || frame.GetMode() == RenderingMode::GBufferOnly) &&
            !RegisterGeometryImports(retainedFrame.GetGeometryFrameWork(), &resourceFailure))
        {
            m_resourceAllocator.CancelBeforePublication();
            return RenderFrameExecutionStatus::Failure(FailureMessage(resourceFailure.message, "geometry external resource import failed"));
        }
        auto& diagnosticWork = retainedFrame.GetGeometryFrameWork();
        if (frame.GetFeatures().geometryDiagnostics && diagnosticWork.GetVisibilityPlan().IsValid())
        {
            if (FindPipeline("GeometryDiagnostics").IsValid())
                diagnosticWork.diagnosticSlot = m_geometryDiagnostics.Reserve(diagnosticWork, frame);
            else
                m_geometryDiagnostics.NoteDroppedSample();
            if (diagnosticWork.diagnosticSlot != GeometryDiagnosticReadbacks::InvalidSlot)
            {
                RetainedBufferImportDesc desc;
                desc.buffer = m_geometryDiagnostics.GetBuffer(diagnosticWork.diagnosticSlot);
                desc.token = {rhi::ResourceRef(desc.buffer).value};
                const bool described = rhi::GetBufferDesc(desc.buffer, desc.expected);
                desc.initialState = desc.terminalState = rhi::ResourceState::CopyDestination;
                bool imported = false;
                if (described)
                    imported = m_resourceAllocator.RegisterImport(desc, diagnosticWork.diagnosticReadbackImport, &resourceFailure);
                if (!imported)
                {
                    m_geometryDiagnostics.Cancel(diagnosticWork.diagnosticSlot);
                    diagnosticWork.diagnosticSlot = GeometryDiagnosticReadbacks::InvalidSlot;
                    m_resourceAllocator.CancelBeforePublication();
                    return RenderFrameExecutionStatus::Failure("geometry diagnostic readback import failed");
                }
            }
        }
        if (frame.GetGraph().registerImports != nullptr && !frame.GetGraph().registerImports(m_resourceAllocator, frame, &resourceFailure))
        {
            m_resourceAllocator.CancelBeforePublication();
            return RenderFrameExecutionStatus::Failure(FailureMessage(resourceFailure.message, "frame graph external resource import failed"));
        }
        const bool bindingsPrepared = graph.PrepareResourceBindings(resourceBindings, preparationFailures);
        if (!bindingsPrepared)
        {
            m_geometryDiagnostics.Cancel(diagnosticWork.diagnosticSlot);
            diagnosticWork.diagnosticSlot = GeometryDiagnosticReadbacks::InvalidSlot;
            m_resourceAllocator.CancelBeforePublication();
            return RenderFrameExecutionStatus::Failure(preparationFailures.GetFailure().message);
        }

        graph.AcquireExclusiveUpdateFlag();

        retainedFrame.InstallJobsRenderFrame();
        static jobs::JobName runRenderNodeJobsName{"RenderGraph/RunNodeJobs"};
        const bool nodeJobsDispatched = renderNodeJobsBuilder.Dispatch(runRenderNodeJobsName, static_cast<jobs::Task&&>(runRenderNodeJobsTask));
        if (!nodeJobsDispatched)
        {
            m_geometryDiagnostics.Cancel(diagnosticWork.diagnosticSlot);
            diagnosticWork.diagnosticSlot = GeometryDiagnosticReadbacks::InvalidSlot;
            retainedFrame.ClearJobsRenderFrame();
            graph.ReleaseExclusiveUpdateFlag();
            m_resourceAllocator.CancelBeforePublication();
            return RenderFrameExecutionStatus::Failure("render-node setup task dispatch failed");
        }
        retainedFrame.DeferTerminalCompletion();

        // Node-job construction overlaps storage preparation; node execution still waits on kickoff.
        const auto prepareCustomData = [&]() noexcept -> RenderFrameExecutionStatus
        {
            if (family == nullptr)
                return RenderFrameExecutionStatus::Success();
            rhi::Failure rhiFailure;
            rhi::CommandListRef storageData = rhi::CreateCommandList(rhi::CommandListType::Default, 0, &rhiFailure);
            if (!storageData.IsValid())
            {
                retainedFrame.RecordFailure(FailureMessage(rhiFailure.message, "StorageData command-list creation failed"));
                return RenderFrameExecutionStatus::Failure(retainedFrame.GetFailureMessage());
            }
            rhi::SetResourceDebugName(storageData, "StorageData");
            frameCommandLists.SetCommandList(static_cast<u32>(ReservedFrameCommandList::StorageData), storageData);
            if (!rhi::BindCommandList(storageData, &rhiFailure))
            {
                retainedFrame.RecordFailure(FailureMessage(rhiFailure.message, "StorageData command-list binding failed"));
                return RenderFrameExecutionStatus::Failure(retainedFrame.GetFailureMessage());
            }
            RenderCameraFailure cameraFailure;
            // Prepares scene custom data first, then each camera's custom data.
            const bool customDataPrepared = m_cameras.PrepareCustomData(declarationContext, &cameraFailure);
            rhi::UnbindCommandList();
            if (!customDataPrepared)
                return RenderFrameExecutionStatus::Failure(FailureMessage(cameraFailure.message, "render frame custom-data preparation failed"));
            if (!m_cameras.CheckCustomDataReadiness(*family, &cameraFailure))
                return RenderFrameExecutionStatus::Failure(FailureMessage(cameraFailure.message, "render frame custom data is not ready"));
            retainedFrame.GetFrameCustomData() = FrameCustomData(*family);
            if (!retainedFrame.GetFrameCustomData().IsValid())
                return RenderFrameExecutionStatus::Failure("render frame custom-data access could not retain its prepared view family");
            return RenderFrameExecutionStatus::Success();
        };
        const RenderFrameExecutionStatus customDataStatus = prepareCustomData();
        if (!customDataStatus)
            retainedFrame.RecordFailure(customDataStatus.message);
        if (!retainedFrame.HasFailure())
            graph.PrepareResourcesParallel(declarationContext, m_resourceAllocator, resourceBindings, preparationFailures, context.GetBuilder());

        static jobs::JobName resolveResourcesName{"RenderGraph/ResolveResources"};
        const bool resolveDispatched = context.GetBuilder().Dispatch(resolveResourcesName, static_cast<jobs::Task&&>(resolveResourcesTask));
        if (!resolveDispatched)
            VG_FATAL("preallocated resource resolution task could not be dispatched by the valid frame builder");
        static jobs::JobName finishResourceResolutionName{"RenderGraph/FinishResourceResolution"};
        if (!context.GetBuilder().Dispatch(finishResourceResolutionName, static_cast<jobs::Task&&>(finishResourceResolutionTask)))
            VG_FATAL("preallocated resource completion task could not be dispatched by the valid frame builder");
        context.GetBuilder().AddDependency(static_cast<jobs::Builder&&>(renderNodeJobsBuilder));
        setupDeferral.Finish();
        return RenderFrameExecutionStatus::Success();
    }
} // namespace vanguard::rendering
