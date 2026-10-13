#include <vanguard/rendering/frame_renderer.hpp>

#include <vanguard/jobs/jobs.hpp>
#include <vanguard/profiling/profiling.hpp>
#include <vanguard/rendering/render_graph_scheduler_probe.hpp>
#include <vanguard/rendering/render_graph_nodes.hpp>
#include <vanguard/rendering/geometry_graph_nodes.hpp>
#include <vanguard/rendering/mesh_residency.hpp>
#include <vanguard/rendering/render_node_graph.hpp>
#include <vanguard/rendering/render_node_impl_context.hpp>
#include <vanguard/rendering/render_node_job.hpp>
#include <vanguard/rendering/render_scene_gpu_read.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace vanguard::rendering
{
#if VG_BUILD_PROFILE
    class RenderNodeSchedulerProbeResourcePhase final : public RenderNodeImpl
    {
    public:
        explicit RenderNodeSchedulerProbeResourcePhase(const u32 phase) noexcept : m_phase(phase) {}
        [[nodiscard]] const char* GetName() const noexcept override { return "SchedulerProbeResources"; }
        [[nodiscard]] RenderNodeCommandListUsage GetCommandListUsage() const noexcept override { return RenderNodeCommandListUsage::None; }
        [[nodiscard]] bool DeclareResources(const RenderNodeImplContext& context, RenderFlowResourceFailure* failure) const noexcept override
        {
            return UseRenderGraphSchedulerProbeResources(context, m_phase, failure);
        }
        void Execute(const RenderNodeImplContext&, jobs::Builder*) const override {}
    private:
        u32 m_phase = 0;
    };
#endif

    class RenderNodeBeginFrameGpuTiming final : public RenderNodeImpl
    {
    public:
        explicit RenderNodeBeginFrameGpuTiming(FrameRenderer& renderer) noexcept : m_renderer(renderer) {}
        [[nodiscard]] const char* GetName() const noexcept override { return "BeginFrameGpuTiming"; }
        [[nodiscard]] RenderNodeCommandListUsage GetCommandListUsage() const noexcept override { return RenderNodeCommandListUsage::Require; }
        void Execute(const RenderNodeImplContext& context, jobs::Builder*) const override { m_renderer.BeginFrameGpuTiming(context); }
    private:
        FrameRenderer& m_renderer;
    };

    class RenderNodeBeginGeometryGpuTiming final : public RenderNodeImpl
    {
    public:
        explicit RenderNodeBeginGeometryGpuTiming(FrameRenderer& renderer) noexcept : m_renderer(renderer) {}
        [[nodiscard]] const char* GetName() const noexcept override { return "BeginGeometryGpuTiming"; }
        [[nodiscard]] RenderNodeCommandListUsage GetCommandListUsage() const noexcept override { return RenderNodeCommandListUsage::Require; }
        void Execute(const RenderNodeImplContext& context, jobs::Builder*) const override { m_renderer.BeginGeometryGpuTiming(context); }
    private:
        FrameRenderer& m_renderer;
    };

    class RenderNodeEndGeometryGpuTiming final : public RenderNodeImpl
    {
    public:
        explicit RenderNodeEndGeometryGpuTiming(FrameRenderer& renderer) noexcept : m_renderer(renderer) {}
        [[nodiscard]] const char* GetName() const noexcept override { return "EndGeometryGpuTiming"; }
        [[nodiscard]] RenderNodeCommandListUsage GetCommandListUsage() const noexcept override { return RenderNodeCommandListUsage::Require; }
        void Execute(const RenderNodeImplContext& context, jobs::Builder*) const override { m_renderer.EndGeometryGpuTiming(context); }
    private:
        FrameRenderer& m_renderer;
    };

    class RenderNodeBeginCameraGpuTiming final : public RenderNodeImpl
    {
    public:
        explicit RenderNodeBeginCameraGpuTiming(FrameRenderer& renderer) noexcept : m_renderer(renderer) {}
        [[nodiscard]] const char* GetName() const noexcept override { return "BeginCameraGpuTiming"; }
        [[nodiscard]] RenderNodeCommandListUsage GetCommandListUsage() const noexcept override { return RenderNodeCommandListUsage::Require; }
        void Execute(const RenderNodeImplContext& context, jobs::Builder*) const override { m_renderer.BeginCameraGpuTiming(context); }
    private:
        FrameRenderer& m_renderer;
    };

    class RenderNodeEndCameraGpuTiming final : public RenderNodeImpl
    {
    public:
        explicit RenderNodeEndCameraGpuTiming(FrameRenderer& renderer) noexcept : m_renderer(renderer) {}
        [[nodiscard]] const char* GetName() const noexcept override { return "EndCameraGpuTiming"; }
        [[nodiscard]] RenderNodeCommandListUsage GetCommandListUsage() const noexcept override { return RenderNodeCommandListUsage::Require; }
        void Execute(const RenderNodeImplContext& context, jobs::Builder*) const override { m_renderer.EndCameraGpuTiming(context); }
    private:
        FrameRenderer& m_renderer;
    };

    class RenderNodeEndFrameGpuTiming final : public RenderNodeImpl
    {
    public:
        explicit RenderNodeEndFrameGpuTiming(FrameRenderer& renderer) noexcept : m_renderer(renderer) {}
        [[nodiscard]] const char* GetName() const noexcept override { return "EndFrameGpuTiming"; }
        [[nodiscard]] RenderNodeCommandListUsage GetCommandListUsage() const noexcept override { return RenderNodeCommandListUsage::Require; }
        void Execute(const RenderNodeImplContext& context, jobs::Builder*) const override { m_renderer.EndFrameGpuTiming(context); }
    private:
        FrameRenderer& m_renderer;
    };

    namespace
    {
        inline constexpr u64 RenderGraphRendererRevision = 7;
        inline constexpr rhi::GpuCounterScopeId GeometryWorkGpuTimingScope = rhi::MakeGpuCounterScopeId("GeometryWork");
        inline constexpr rhi::GpuCounterScopeId CameraGeometryGpuTimingScope = rhi::MakeGpuCounterScopeId("CameraGeometry");
        inline constexpr u64 GeometryWorkCommandListRole = 0x47454f4d574f524bull;
        inline constexpr ResourceContractPatchId FrameOutputImportPatch{0x524631324f555401ull};
        inline constexpr ResourceContractPatchId FrameOutputExportPatch{0x524631324f555402ull};
        inline constexpr ResourceContractPatchId GeometryShellPlanPatch{0x5246313247454f01ull};
        inline constexpr ResourceContractPatchId GeometryDiagnosticPatch{0x5246313244494101ull};

        [[nodiscard]] constexpr u64 ContractMix(const u64 hash, const u64 value) noexcept
        {
            return (hash ^ (value + 0x9e3779b97f4a7c15ull + (hash << 6u) + (hash >> 2u))) * 0x100000001b3ull;
        }

        [[nodiscard]] constexpr ResourceContractPatchId GeometryContractPatch(const u64 domain, const u32 index,
                                                                               const u32 generation = 0,
                                                                               const u32 binding = 0) noexcept
        {
            u64 value = ContractMix(domain, index);
            value = ContractMix(value, generation);
            value = ContractMix(value, binding);
            return {value != 0 ? value : 1u};
        }

        [[nodiscard]] RenderFlowResourceContractRevisionKey BuildResourceContractRevision(
            const RenderGraphCache::CacheEntry& entry, const GeometryFrameWork& work, const bool diagnosticReadback,
            const bool hasFrameOutput, const RenderViewportOutputKind outputKind, const RenderOutputColorEncoding outputEncoding,
            const RenderTextureOutputContract* const textureOutputContract) noexcept
        {
            RenderFlowResourceContractRevisionKey key;
            key.hash = ContractMix(0xcbf29ce484222325ull, entry.graphKey.hash);
            const GeometryFrameWorkCapacity capacity = work.GetCapacity();
            key.views = capacity.views;
            key.visibleInstances = capacity.visibleInstances;
            key.geometryWorkItems = capacity.geometryWorkItems;
            key.geometryBins = capacity.geometryBins;
            key.shellDraws = capacity.shellDraws;
            key.shellsPerView = capacity.shellsPerView;
            key.indirectArgumentCapacity = work.GetIndirectArgumentCapacity();
            key.shellDrawCount = work.GetShellDraws().Size();
            key.diagnosticReadback = diagnosticReadback;
            const auto outputRegions = work.GetOutputRegions();
            key.outputRegionCount = outputRegions.Size();
            for (u32 index = 0; index < outputRegions.Size(); ++index)
                key.outputRegionViews[index] = outputRegions[index].viewIndex;
            key.hasFrameOutput = hasFrameOutput;
            key.outputKind = hasFrameOutput ? outputKind : RenderViewportOutputKind::Headless;
            key.outputEncoding = hasFrameOutput ? outputEncoding : RenderOutputColorEncoding::SceneLinear;
            key.hasTextureOutput = textureOutputContract != nullptr;
            if (hasFrameOutput)
                key.hash = ContractMix(ContractMix(key.hash, static_cast<u64>(outputKind)), static_cast<u64>(outputEncoding));
            if (textureOutputContract != nullptr)
            {
                // Slot/generation is dynamic, but terminal state/queue/readiness
                // authored by RTExportFrameOutput are part of the semantic plan.
                key.textureRestingState = textureOutputContract->restingState;
                key.textureRestingQueue = textureOutputContract->restingQueue;
                key.texturePublicationReadiness = textureOutputContract->publicationReadiness;
            }
            key.hash = ContractMix(key.hash, key.views);
            key.hash = ContractMix(key.hash, key.visibleInstances);
            key.hash = ContractMix(key.hash, key.geometryWorkItems);
            key.hash = ContractMix(key.hash, key.geometryBins);
            key.hash = ContractMix(key.hash, key.shellDraws);
            key.hash = ContractMix(key.hash, key.shellsPerView);
            key.hash = ContractMix(key.hash, key.indirectArgumentCapacity);
            key.hash = ContractMix(key.hash, key.shellDrawCount);
            key.hash = ContractMix(key.hash, key.diagnosticReadback ? 1u : 0u);
            key.hash = ContractMix(key.hash, key.outputRegionCount);
            for (u32 index = 0; index < key.outputRegionCount; ++index)
                key.hash = ContractMix(key.hash, key.outputRegionViews[index]);
            key.hash = ContractMix(key.hash, key.hasFrameOutput ? 1u : 0u);
            key.hash = ContractMix(key.hash, key.hasTextureOutput ? 1u : 0u);
            if (textureOutputContract != nullptr)
            {
                key.hash = ContractMix(key.hash, static_cast<u64>(key.textureRestingState));
                key.hash = ContractMix(key.hash, static_cast<u64>(key.textureRestingQueue));
                key.hash = ContractMix(key.hash, static_cast<u64>(key.texturePublicationReadiness));
            }
            if (key.hash == 0)
                key.hash = 1;
            return key;
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

    bool FrameRenderer::EnsureGeometryShellPlanBuffer(GeometryFrameWork& work, rhi::Failure* const failure) noexcept
    {
        concurrency::ScopedLock<concurrency::SpinLock> guard(m_geometryShellPlanCache.lock);
        auto& cache = m_geometryShellPlanCache;
        const u64 expectedSerial = work.GetShellPlanCacheSerial();
        if (!work.IsPrepared() || expectedSerial == 0 || !cache.valid || cache.serial != expectedSerial)
            return false;
        if (!cache.gpuBuffer.IsValid() || cache.gpuBufferSerial != cache.serial)
        {
            rhi::BufferDesc desc;
            desc.size = static_cast<u64>(cache.shellDraws.Size() != 0 ? cache.shellDraws.Size() : 1u) * sizeof(GpuGeometryShellRange);
            desc.structureStride = sizeof(GpuGeometryShellRange);
            desc.usage = rhi::BufferUsage::Structured | rhi::BufferUsage::ShaderResource;
            desc.initialState = rhi::ResourceState::Common;
            GpuGeometryShellRange emptyPlan{};
            const rhi::BufferInitData initialData{cache.shellDraws.Empty() ? static_cast<const void*>(&emptyPlan) : cache.shellDraws.Data(), desc.size};
            rhi::Buffer replacement = rhi::Buffer::Create(desc, initialData, failure);
            if (!replacement.IsValid())
                return false;
            rhi::SetResourceDebugName(replacement.GetRef(), "Geometry Persistent Shell Plans");
            cache.gpuBuffer = static_cast<rhi::Buffer&&>(replacement);
            cache.gpuBufferSerial = cache.serial;
        }
        work.PinShellPlanBuffer(cache.gpuBuffer);
        return true;
    }


    bool FrameRenderer::RegisterGeometryImports(GeometryFrameWork& work, RenderFlowResourceFailure* failure) noexcept
    {
        if (!work.IsPrepared())
            return true;
        work.SetShellPlanImport({});
        const rhi::BufferRef shellPlanBuffer = work.GetShellPlanBuffer();
        if (!shellPlanBuffer.IsValid())
            return false;
        RetainedBufferImportDesc shellPlanDesc;
        shellPlanDesc.buffer = shellPlanBuffer;
        shellPlanDesc.token = {rhi::ResourceRef(shellPlanDesc.buffer).value};
        shellPlanDesc.contractPatch = GeometryShellPlanPatch;
        if (!rhi::GetBufferDesc(shellPlanDesc.buffer, shellPlanDesc.expected))
            return false;
        shellPlanDesc.initialState = shellPlanDesc.expected.initialState;
        shellPlanDesc.terminalState = shellPlanDesc.expected.initialState;
        shellPlanDesc.initialQueue = rhi::QueueType::Graphics;
        shellPlanDesc.terminalQueue = rhi::QueueType::Graphics;
        shellPlanDesc.readiness = ImportReadinessKind::SameQueueContinuation;
        ImportedResourceId shellPlanImport;
        if (!m_resourceAllocator.RegisterImport(shellPlanDesc, shellPlanImport, failure))
            return false;
        work.SetShellPlanImport(shellPlanImport);
        auto& imports = work.GetExternalBuffers();
        imports.Clear();
        containers::HashMap<u64, u32> importedBuffers{memory::pools::Rendering::GetInstance()};
        const auto add = [&](rhi::BufferRef buffer, bool scene, rhi::ResourceState state, rhi::GpuFence incoming,
                             const ResourceContractPatchId contractPatch, const char* const semanticName) -> bool {
            u32 existing = 0;
            const u64 identity = rhi::ResourceRef(buffer).value;
            const bool alreadyImported = importedBuffers.Find(identity, existing);
            if (alreadyImported)
                return imports[existing].sceneTable == scene && imports[existing].readState == state;
            RetainedBufferImportDesc desc;
            desc.token = {identity};
            desc.contractPatch = contractPatch;
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
            std::snprintf(entry.name, sizeof(entry.name), "%s", semanticName);
            entry.nameHash = LogicalResourceName::CalculateHash(containers::StringView(entry.name));
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
        const auto sceneBuffers = m_gpuScene.GetTables().GetConsumerBuffers();
        for (u32 bufferIndex = 0; bufferIndex < sceneBuffers.Size(); ++bufferIndex)
        {
            char semanticName[96]{};
            std::snprintf(semanticName, sizeof(semanticName), "Geometry.SceneTable.%u", bufferIndex);
            const bool added = add(sceneBuffers[bufferIndex], true,
                                   rhi::ResourceState::ShaderResourceGraphics | rhi::ResourceState::ShaderResourceCompute,
                                   m_gpuScene.GetPublicationFence(), GeometryContractPatch(0x5246313253434e45ull, bufferIndex), semanticName);
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
                    char semanticName[96]{};
                    std::snprintf(semanticName, sizeof(semanticName), "Geometry.Vertex.%u.%u.%u", vertices.id.index, vertices.id.generation, index);
                    const bool vertexAdded = add(vertices.buffers[index], false, rhi::ResourceState::VertexBuffer, {},
                                                 GeometryContractPatch(0x5246313256455254ull, vertices.id.index, vertices.id.generation, index), semanticName);
                    if (!vertexAdded) return false;
                }
                char semanticName[96]{};
                std::snprintf(semanticName, sizeof(semanticName), "Geometry.Index.%u.%u", indices.id.index, indices.id.generation);
                const bool indexAdded = add(indices.buffer, false, rhi::ResourceState::IndexBuffer, {},
                                            GeometryContractPatch(0x52463132494e4458ull, indices.id.index, indices.id.generation), semanticName);
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
            const auto tag = context.RTSharedNameTag(external.GetLogicalName());
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
            const auto tag = context.RTSharedNameTag(external.GetLogicalName());
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

    bool FrameRenderer::RecordTransparentGeometryPhase(const GeometryFrameWork& work, const RenderPhaseId phase,
                                                       const StaticSurfaceDrawContext& draw, const rhi::BufferRef instances,
                                                       rhi::Failure* const failure) const noexcept
    {
        return m_geometryBatcher != nullptr && m_geometryAllocator != nullptr &&
               work.RecordTransparentPhase(*m_geometryBatcher, *m_geometryAllocator, phase, draw, instances, failure);
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
        const RenderNodeGraph::ItemId startRenderId =
            factory.BeginCommandListGroup(group, RenderNodeType::Unique, RenderNodeSubtype(UniqueRenderNodeStartRender), "StartRender");
        if (auto scopedCommandList = ScopedCloseCommandList(factory))
        {
            ADD_SUBNODE("BeginFrameGpuTiming", RenderNodeBeginFrameGpuTiming, *this);
            ADD_SUBNODE("StartRender", RenderNodeStartRender);
        }
        // Unique occurrences merge across cached camera fragments. Every stage
        // addresses the complete family plan once, with independent view ranges.
        RENDER_UNIQUE_BOUNDED_UPLOAD_COMMAND_LIST(group, UniqueRenderNodeGeometryCompute, "GeometryWork", GeometryWorkCommandListRole)
        {
            ADD_SUBNODE("BeginGeometryGpuTiming", RenderNodeBeginGeometryGpuTiming, *this);
            // Declaration-only child uses the group's planning writer and
            // records no GPU commands or separate submission.
            ADD_SUBNODE("DeclareGeometryWorkResources", RenderNodeDeclareGeometryWorkResources);
            ADD_SUBNODE("InitializeGeometryWork", RenderNodeInitializeGeometryWork);
            for (u32 stage = 0; stage < static_cast<u32>(GeometryComputeStage::Count); ++stage)
                ADD_SUBNODE("GeometryCompute", RenderNodeGeometryCompute, *this, static_cast<GeometryComputeStage>(stage));
            ADD_SUBNODE("EndGeometryGpuTiming", RenderNodeEndGeometryGpuTiming, *this);
        }
#if VG_BUILD_PROFILE
        if (IsRenderGraphSchedulerProbeEnabled())
        {
            for (u32 phase = 0; phase < 10; ++phase)
            {
                RENDER_COMMAND_LIST(group, "SchedulerProbeResources")
                {
                    ADD_SUBNODE("SchedulerProbeResources", RenderNodeSchedulerProbeResourcePhase, phase);
                }
            }
        }
#endif
        RENDER_COMMAND_LIST(group, "CameraGeometry")
        {
            ADD_SUBNODE("BeginCameraGpuTiming", RenderNodeBeginCameraGpuTiming, *this);
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
                ADD_SUBNODE("DrawTransparent", RenderNodeDrawTransparent, *this);
            }
            ADD_SUBNODE("EndCameraDependencies", RenderNodeEndCameraDependencies);
            ADD_SUBNODE("EndCameraGpuTiming", RenderNodeEndCameraGpuTiming, *this);
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
        const RenderNodeGraph::ItemId endRenderId =
            factory.BeginCommandListGroup(group, RenderNodeType::Unique, RenderNodeSubtype(UniqueRenderNodeEndRender), "EndRender");
        if (auto scopedCommandList = ScopedCloseCommandList(factory))
        {
            ADD_SUBNODE("EndRender", RenderNodeEndRender);
            ADD_SUBNODE("EndFrameGpuTiming", RenderNodeEndFrameGpuTiming, *this);
        }
        const RenderNodeGraph::ItemId finalFlushId =
            ADD_UNIQUE(group, "FlushGpu", UniqueRenderNodeFinalFlush, RenderNodeSynchronize, rhi::CommandListSyncType::None, "Submit_CameraGraph");
        const RenderNodeGraph::ItemId presentId = ADD_UNIQUE(group, "Present", UniqueRenderNodePresent, RenderNodePresent);
        const RenderNodeGraph::ItemId flushTextureGrabsId =
            ADD_UNIQUE(group, "FlushTextureGrabs", UniqueRenderNodeFlushTextureGrabs, RenderNodeFlushTextureGrabs);
        const RenderNodeGraph::ItemId cleanupBatchDataAllocatorId =
            ADD_UNIQUE(group, "CleanupBatchDataAllocator", UniqueRenderNodeCleanupBatchDataAllocator, RenderNodeCleanupBatchDataAllocator);
        const RenderNodeGraph::ItemId endFrameId = ADD_UNIQUE(group, "EndFrame", UniqueRenderNodeEndFrame, RenderNodeEndFrame);

#if VG_BUILD_PROFILE
        if (IsRenderGraphSchedulerProbeEnabled())
            AppendRenderGraphSchedulerProbe(factory, startRenderId, endRenderId);
#endif

        // GPU submission order remains declaration-ordered, while CPU recording follows the
        // explicit branch/join model instead of serializing every command-list occurrence with LinkCpu().
        factory.LinkGpu();
        factory.LinkCpuToNextGpu(startRenderId);
        factory.LinkCpuToPreviousGpu(endRenderId);
        factory.LinkCpuToPreviousGpu(finalFlushId);
        factory.Link(finalFlushId, presentId, RenderNodeDependencyType::Cpu);
        factory.Link(presentId, flushTextureGrabsId, RenderNodeDependencyType::Cpu);
        factory.Link(flushTextureGrabsId, cleanupBatchDataAllocatorId, RenderNodeDependencyType::Cpu);
        factory.Link(cleanupBatchDataAllocatorId, endFrameId, RenderNodeDependencyType::Cpu);
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

    bool FrameRenderer::InitializeGpuTimings(rhi::Failure* const failure) noexcept
    {
        if (failure != nullptr)
            *failure = {};
        if (m_gpuTimings.IsInitialized())
        {
            if (failure != nullptr)
            {
                failure->code = rhi::FailureCode::AlreadyInitialized;
                std::snprintf(failure->message, sizeof(failure->message), "%s", "renderer GPU timings are already initialized");
            }
            return false;
        }
        if (!rhi::GetCapabilities().timestampQueries)
            return true;

        rhi::GpuCounterConfig config;
        config.bufferedFrames = 4;
        config.maximumScopesPerFrame = MaximumRenderViewsPerFamily + 1u;
        config.maximumRegisteredScopes = 2;
        config.queue = rhi::QueueType::Graphics;
        config.collectPipelineStatistics = false;
        if (!m_gpuTimings.Initialize(config, failure))
            return false;
        if (!m_gpuTimings.RegisterScope(GeometryWorkGpuTimingScope, "GeometryWork", failure) ||
            !m_gpuTimings.RegisterScope(CameraGeometryGpuTimingScope, "CameraGeometry", failure))
        {
            static_cast<void>(m_gpuTimings.Shutdown());
            return false;
        }
        m_droppedGpuTimingFrames.SetValue(0);
        return true;
    }

    bool FrameRenderer::ShutdownGpuTimings(rhi::Failure* const failure) noexcept
    {
        if (failure != nullptr)
            *failure = {};
        if (!m_gpuTimings.IsInitialized())
            return true;
        static_cast<void>(m_gpuTimings.Collect(failure));
        if (failure != nullptr && failure->code != rhi::FailureCode::None)
            return false;
        rhi::GpuCounterFrameView ready;
        while (m_gpuTimings.GetOldestReadyFrame(ready, nullptr))
            if (!m_gpuTimings.ConsumeFrame(ready.handle, failure))
                return false;
        return m_gpuTimings.Shutdown(failure);
    }

    void FrameRenderer::AbandonGpuTimings() noexcept
    {
        m_gpuTimings.AbandonDevice();
    }

    FrameGpuTimingPoll FrameRenderer::PollGpuTimings(FrameGpuTimingSample& output, rhi::Failure* const failure) noexcept
    {
        output = {};
        if (failure != nullptr)
            *failure = {};
        if (!m_gpuTimings.IsInitialized())
            return FrameGpuTimingPoll::Empty;
        static_cast<void>(m_gpuTimings.Collect(failure));
        if (failure != nullptr && failure->code != rhi::FailureCode::None)
            return FrameGpuTimingPoll::Failed;

        rhi::GpuCounterFrameView frame;
        rhi::Failure readyFailure;
        if (!m_gpuTimings.GetOldestReadyFrame(frame, &readyFailure))
        {
            const rhi::GpuCounterStats stats = m_gpuTimings.GetStats();
            if (readyFailure.code != rhi::FailureCode::Busy)
            {
                if (failure != nullptr)
                    *failure = readyFailure;
                return FrameGpuTimingPoll::Failed;
            }
            return stats.recordingFrames != 0 || stats.awaitingSubmissionFrames != 0 || stats.pendingGpuFrames != 0
                       ? FrameGpuTimingPoll::Pending
                       : FrameGpuTimingPoll::Empty;
        }

        output.frameSerial = frame.frameNumber;
        output.droppedScopes = frame.droppedSamples;
        for (u32 index = 0; index < frame.sampleCount; ++index)
        {
            const rhi::GpuCounterSample& sample = frame.samples[index];
            if (!sample.valid || sample.gpuEnd < sample.gpuBegin)
                continue;
            const u64 elapsed = rhi::GpuCounterTicksToNanoseconds(sample.gpuEnd - sample.gpuBegin, frame.calibration.gpuFrequency);
            if (sample.scope == GeometryWorkGpuTimingScope)
                output.geometryWorkNanoseconds += elapsed;
            else if (sample.scope == CameraGeometryGpuTimingScope)
            {
                output.cameraGeometryNanoseconds += elapsed;
                ++output.cameraGeometrySamples;
            }
        }
        if (!m_gpuTimings.ConsumeFrame(frame.handle, failure))
            return FrameGpuTimingPoll::Failed;
        return FrameGpuTimingPoll::Ready;
    }

    void FrameRenderer::BeginFrameGpuTiming(const RenderNodeImplContext& context) noexcept
    {
        GeometryFrameWork* const work = context.GetGeometryFrameWork();
        if (!m_gpuTimings.IsInitialized() || work == nullptr || !work->IsPrepared() || work->gpuTimingFrame.IsValid())
            return;
        rhi::Failure failure;
        if (!m_gpuTimings.BeginFrame(context.GetFrameInfo().GetSerial(), work->gpuTimingFrame, &failure))
        {
            static_cast<void>(m_droppedGpuTimingFrames.Increment());
            return;
        }
    }

    void FrameRenderer::BeginGeometryGpuTiming(const RenderNodeImplContext& context) noexcept
    {
        GeometryFrameWork* const work = context.GetGeometryFrameWork();
        if (!m_gpuTimings.IsInitialized() || work == nullptr || !work->gpuTimingFrame.IsValid() || work->geometryTimingScope.IsValid())
            return;
        rhi::Failure failure;
        if (!m_gpuTimings.BeginScope(work->gpuTimingFrame, GeometryWorkGpuTimingScope, work->geometryTimingScope, &failure))
        {
            static_cast<void>(m_droppedGpuTimingFrames.Increment());
        }
    }

    void FrameRenderer::EndGeometryGpuTiming(const RenderNodeImplContext& context) noexcept
    {
        GeometryFrameWork* const work = context.GetGeometryFrameWork();
        if (work != nullptr && work->geometryTimingScope.IsValid())
            static_cast<void>(m_gpuTimings.EndScope(work->geometryTimingScope));
    }

    void FrameRenderer::BeginCameraGpuTiming(const RenderNodeImplContext& context) noexcept
    {
        GeometryFrameWork* const work = context.GetGeometryFrameWork();
        const i32 cameraIndex = context.GetCameraIndex();
        if (!m_gpuTimings.IsInitialized() || work == nullptr || !work->gpuTimingFrame.IsValid() || cameraIndex < 0 ||
            static_cast<u32>(cameraIndex) >= MaximumRenderViewsPerFamily)
            return;
        rhi::GpuCounterScopeToken& scope = work->cameraTimingScopes[static_cast<u32>(cameraIndex)];
        if (!scope.IsValid())
            static_cast<void>(m_gpuTimings.BeginScope(work->gpuTimingFrame, CameraGeometryGpuTimingScope, scope));
    }

    void FrameRenderer::EndCameraGpuTiming(const RenderNodeImplContext& context) noexcept
    {
        GeometryFrameWork* const work = context.GetGeometryFrameWork();
        const i32 cameraIndex = context.GetCameraIndex();
        if (work == nullptr || cameraIndex < 0 || static_cast<u32>(cameraIndex) >= MaximumRenderViewsPerFamily)
            return;
        rhi::GpuCounterScopeToken& scope = work->cameraTimingScopes[static_cast<u32>(cameraIndex)];
        if (scope.IsValid())
            static_cast<void>(m_gpuTimings.EndScope(scope));
    }

    void FrameRenderer::EndFrameGpuTiming(const RenderNodeImplContext& context) noexcept
    {
        GeometryFrameWork* const work = context.GetGeometryFrameWork();
        if (!m_gpuTimings.IsInitialized() || work == nullptr || !work->gpuTimingFrame.IsValid())
            return;
        rhi::Failure failure;
        if (!m_gpuTimings.EndFrame(work->gpuTimingFrame, &failure))
        {
            if (m_gpuTimings.GetFrameState(work->gpuTimingFrame) != rhi::GpuCounterFrameState::Recording)
                static_cast<void>(m_gpuTimings.DiscardFrame(work->gpuTimingFrame));
            work->gpuTimingFrame = {};
            static_cast<void>(m_droppedGpuTimingFrames.Increment());
            return;
        }
        work->gpuTimingResolveScope = context.GetCommandScope();
    }

    void FrameRenderer::FinalizeGpuTiming(GeometryFrameWork& work,
                                          const containers::ArraySpan<const CommandScopeExecutionReceipt> receipts,
                                          const bool deviceLost) noexcept
    {
        if (!m_gpuTimings.IsInitialized() || !work.gpuTimingFrame.IsValid())
            return;
        if (deviceLost)
            return;
        if (!work.gpuTimingResolveScope.IsValid())
        {
            if (m_gpuTimings.DiscardFrame(work.gpuTimingFrame))
                work.gpuTimingFrame = {};
            return;
        }
        for (const CommandScopeExecutionReceipt& receipt : receipts)
        {
            if (receipt.scope != work.gpuTimingResolveScope)
                continue;
            if (receipt.completion == CommandScopeCompletionKind::Submitted && receipt.fence.IsValid() &&
                m_gpuTimings.CommitFrame(work.gpuTimingFrame, receipt.fence))
            {
                work.gpuTimingFrame = {};
                return;
            }
            if (receipt.completion == CommandScopeCompletionKind::DiscardedBeforeSubmission &&
                m_gpuTimings.DiscardFrame(work.gpuTimingFrame))
                work.gpuTimingFrame = {};
            return;
        }
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
        {
            // Shell catalog revisions restart when the geometry batcher is
            // reinitialized. Invalidate both the device-owned snapshot and its
            // CPU identity at the renderer/device boundary; retained frames pin
            // their own immutable buffer references until terminal destruction.
            concurrency::ScopedLock<concurrency::SpinLock> guard(m_geometryShellPlanCache.lock);
            auto& cache = m_geometryShellPlanCache;
            cache.gpuBuffer.Reset();
            cache.gpuBufferSerial = 0;
            cache.shellDraws.Clear();
            cache.catalogRevision = 0;
            cache.viewCount = 0;
            cache.shellsPerView = 0;
            cache.binsPerView = 0;
            cache.indirectArgumentCapacity = 0;
            cache.valid = false;
        }
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
            return ExecuteBuiltGraph(context, *frame.GetGraph().graph, policy, nullptr);
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
        return ExecuteBuiltGraph(context, graphCacheEntry->graph, resourcePolicy, graphCacheEntry);
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
            return RenderFrameExecutionStatus::Failure("geometry preparation requires the current prepared view family");
        RetainedRenderFrameRef retainedFrame = context.RetainFrame();
        GeometryFrameWork& work = retainedFrame.GetGeometryFrameWork();
        if (work.IsPrepared())
            return RenderFrameExecutionStatus::Failure("geometry frame work is already prepared");

        const RenderViewFamily& viewFamily = family.GetFamily();
        RenderSceneSnapshot sceneSnapshot;
        if (!m_scenes.GetSnapshot(family.GetScene(), sceneSnapshot) || sceneSnapshot.state != RenderSceneState::Alive ||
            sceneSnapshot.framePrepared || sceneSnapshot.pendingProxyMutations != 0 ||
            sceneSnapshot.completedMutationEpoch != viewFamily.sceneVersion)
            return RenderFrameExecutionStatus::Failure("geometry preparation requires the exact completed scene epoch");
        if (!m_gpuScene.IsScenePublicationSubmitted(family.GetScene(), viewFamily.sceneVersion))
            return RenderFrameExecutionStatus::Skipped("geometry preparation deferred until the exact GPU Scene epoch is submitted");
        const u32 instanceTraversalExtent = m_gpuScene.GetLifetime().GetTraversalExtent(GpuSceneTableKind::Instance);
        RenderSceneGpuStats sceneGpuStats;
        if (!m_gpuScene.GetScenePublisher().GetSceneStats(family.GetScene(), sceneGpuStats) || sceneGpuStats.trackedInstances > sceneSnapshot.activeProxies)
            return RenderFrameExecutionStatus::Failure("geometry GPU Scene instance accounting is inconsistent with the completed scene epoch");
        const u32 visibleCapacityPerView = sceneGpuStats.trackedInstances != 0 ? sceneGpuStats.trackedInstances : 1u;
        RenderSceneFailure sceneFailure;

        if (m_geometryConfig.maximumWorkItemsPerVisible == 0 || m_geometryBinCapacity == 0 || m_geometryBinCapacity == InvalidGpuSceneIndex)
            return RenderFrameExecutionStatus::Failure("geometry frame-work capacity policy is invalid");
        if (sceneGpuStats.trackedInstances != 0 && m_geometryBatcher == nullptr)
            return RenderFrameExecutionStatus::Failure("GPU Scene mesh instances require the residency-owned geometry batcher");
        if (m_geometryBatcher != nullptr && (m_geometryShellCapacity == 0 || m_geometryShellCapacity == InvalidGpuSceneIndex))
            return RenderFrameExecutionStatus::Failure("geometry shell capacity policy is invalid");

        const u64 totalVisible = static_cast<u64>(views.Size()) * visibleCapacityPerView;
        const u64 workPerView = static_cast<u64>(visibleCapacityPerView) * m_geometryConfig.maximumWorkItemsPerVisible;
        if (workPerView >= InvalidGpuSceneIndex)
            return RenderFrameExecutionStatus::Failure("geometry per-view work capacity overflowed");
        const u64 totalGeometryWork = static_cast<u64>(views.Size()) * workPerView;
        const u64 totalGeometryBins = static_cast<u64>(views.Size()) * m_geometryBinCapacity;
        const u64 totalShellDraws = m_geometryBatcher != nullptr
                                       ? static_cast<u64>(views.Size()) * m_geometryBatcher->GetCatalogStats().activeShells : 0;
        const u64 totalShellSlots = static_cast<u64>(views.Size()) * m_geometryShellCapacity;
        const u64 instanceGroups = (static_cast<u64>(instanceTraversalExtent) + GpuVisibilityThreadsPerGroup - 1u) / GpuVisibilityThreadsPerGroup;
        const u64 geometryGroups = (totalGeometryWork + GpuVisibilityThreadsPerGroup - 1u) / GpuVisibilityThreadsPerGroup;
        if (instanceGroups > 65535u || geometryGroups > 65535u || (totalShellDraws + 127u) / 128u > 65535u)
            return RenderFrameExecutionStatus::Failure("geometry work exceeds the current GPU dispatch capacity");
        if (totalVisible >= InvalidGpuSceneIndex || totalGeometryWork >= InvalidGpuSceneIndex || totalGeometryBins >= InvalidGpuSceneIndex ||
            totalShellDraws * sizeof(GpuGeometryShellCounters) > 0xffffffffull || totalShellSlots >= InvalidGpuSceneIndex)
            return RenderFrameExecutionStatus::Failure("geometry view-family capacity calculation overflowed");
        const GeometryFrameWorkCapacity capacity{views.Size(), static_cast<u32>(totalVisible), static_cast<u32>(totalGeometryWork),
                                                 static_cast<u32>(totalGeometryBins), static_cast<u32>(totalShellDraws), m_geometryShellCapacity};
        if (work.Prepare(context.GetFrame().GetSerial(), capacity) != GeometryFrameWorkResult::Success)
            return RenderFrameExecutionStatus::Failure("retained geometry frame-work allocation failed");
        if (context.GetFrame().GetMode() == RenderingMode::Shaded || context.GetFrame().GetMode() == RenderingMode::ShadedNoAmbient)
        {
            const bool lightsSelected = m_scenes.CollectDirectionalLights(family.GetScene(), viewFamily.sceneVersion, views, work.GetDirectionalLights(), &sceneFailure);
            if (!lightsSelected)
                return RenderFrameExecutionStatus::Failure(FailureMessage(sceneFailure.message, "directional light selection failed"));
        }
        for (const auto& dependency : family.GetDependencies())
        {
            u32 producer = 0, consumer = 0;
            const bool producerFound = FindViewIndex(views, dependency.child, producer);
            const bool consumerFound = FindViewIndex(views, dependency.parent, consumer);
            if (!producerFound || !consumerFound || producer >= consumer)
                return RenderFrameExecutionStatus::Failure("camera dependency family is not in producer-before-consumer order");
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
                if (!found) return RenderFrameExecutionStatus::Failure("output region camera is disabled or absent from the prepared family");
                const bool added = work.AddOutputRegion({viewIndex, region.rect});
                if (!added) return RenderFrameExecutionStatus::Failure("geometry output region capacity exceeded");
            }
        }
        else
        {
            u32 outputView = 0;
            if (!setup.rootCameras.Empty())
            {
                const bool found = FindViewIndex(views, setup.rootCameras[0], outputView);
                if (!found) return RenderFrameExecutionStatus::Failure("frame output camera is not part of the prepared family");
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
                if (foundPrimary) return RenderFrameExecutionStatus::Failure("multiple primary output cameras require explicit output regions");
                foundPrimary = true;
                outputView = index;
            }
            const auto extent = frameInfo.GetOutputExtent();
            const bool added = work.AddOutputRegion({outputView, {0, 0, extent.width, extent.height}});
            if (!added) return RenderFrameExecutionStatus::Failure("frame output extent is invalid");
        }
        const GeometryFrameWorkResult visibility = work.BuildDirectVisibilityPlan(family.GetScene(), views, instanceTraversalExtent, visibleCapacityPerView);
        if (visibility != GeometryFrameWorkResult::Success)
            return RenderFrameExecutionStatus::Failure("geometry direct GPU visibility plan is invalid");
        const GeometryFrameWorkResult planned = work.PlanShellDraws(m_geometryBatcher, &m_geometryShellPlanCache);
        if (planned == GeometryFrameWorkResult::IncompatibleDepthConvention)
            return RenderFrameExecutionStatus::Failure("camera depth convention does not match the cooked geometry pipeline; matching PSO variants are required");
        if (planned != GeometryFrameWorkResult::Success)
            return RenderFrameExecutionStatus::Failure("geometry shell planning exceeded capacity or observed an invalid catalog");
        if (context.GetFrame().GetMode() == RenderingMode::Shaded || context.GetFrame().GetMode() == RenderingMode::ShadedNoAmbient)
        {
            const RenderFrameExecutionStatus transparent = PrepareTransparentGeometry(work, family);
            if (!transparent)
                return transparent;
        }
        rhi::Failure shellPlanFailure;
        if (!EnsureGeometryShellPlanBuffer(work, &shellPlanFailure))
            return RenderFrameExecutionStatus::Failure(FailureMessage(shellPlanFailure.message, "geometry persistent shell-plan publication failed"));
        return RenderPreparedFrame(context);
    }

    RenderFrameExecutionStatus FrameRenderer::PrepareTransparentGeometry(GeometryFrameWork& work,
                                                                          const PreparedRenderViewFamily& family) noexcept
    {
        work.m_transparentDraws.Clear();
        work.m_transparentInstances.Clear();
        for (auto& range : work.m_transparentDrawRanges)
            range = {};

        const RenderPhaseId transparentPhase = GetGeometryPhase(standardRenderPhases::Transparent);
        if (!transparentPhase.IsValid())
            return RenderFrameExecutionStatus::Failure("transparent geometry phase is unavailable");
        const auto views = family.GetViews();
        const u32 visibleCapacityPerView = views.Empty() ? 0u : work.GetCapacity().visibleInstances / views.Size();
        if (visibleCapacityPerView == 0u || m_geometryBatcher == nullptr)
            return RenderFrameExecutionStatus::Success();

        const f64 cellSize = static_cast<f64>(GetGeometryWorldCellSize());
        if (!(cellSize > 0.0) || !std::isfinite(cellSize))
            return RenderFrameExecutionStatus::Failure("transparent geometry requires a valid GPU Scene cell size");

        containers::DynamicArray<RenderProxyHandle> visible(memory::pools::Rendering::GetInstance());
        visible.Reserve(visibleCapacityPerView);
        if (visible.Capacity() < visibleCapacityPerView)
            return RenderFrameExecutionStatus::Failure("transparent geometry visibility scratch allocation failed");
        for (u32 viewIndex = 0; viewIndex < views.Size(); ++viewIndex)
        {
            const RenderView& view = views[viewIndex];
            GeometryTransparentDrawRange& range = work.m_transparentDrawRanges[viewIndex];
            range.firstDraw = work.m_transparentDraws.Size();
            if (!view.phases.Contains(transparentPhase))
                continue;

            visible.Clear();
            VisibilityQueryRequest request;
            request.scene = family.GetScene();
            request.mutationEpoch = family.GetFamily().sceneVersion;
            request.frustum = view.frustum;
            request.layerMask = view.layerMask;
            request.visibilityMask = view.visibilityMask;
            request.requiredFlags = RenderProxyVisibilityFlags::Visible;
            request.excludedFlags = RenderProxyVisibilityFlags::QueryOnly;
            request.payloadFilter = VisibilityQueryPayloadFilter::Mesh;
            request.maximumResults = visibleCapacityPerView;
            request.useBounds = false;
            request.useFrustum = true;
            VisibilityQueryResult visibilityResult;
            RenderSceneFailure sceneFailure;
            if (!m_scenes.CollectVisibleProxies(request, visible, visibilityResult, &sceneFailure) || !visibilityResult.completed)
                return RenderFrameExecutionStatus::Failure(FailureMessage(sceneFailure.message, "transparent geometry visibility collection failed"));

            const f64 camera[3]{static_cast<f64>(view.origin.worldCell[0]) * cellSize + view.origin.localPosition[0],
                                static_cast<f64>(view.origin.worldCell[1]) * cellSize + view.origin.localPosition[1],
                                static_cast<f64>(view.origin.worldCell[2]) * cellSize + view.origin.localPosition[2]};
            const bool reverseDepth = (view.flags & RenderViewFlags::ReverseDepth) != RenderViewFlags::None;
            for (const RenderProxyHandle proxy : visible)
            {
                RenderSceneGpuReadView proxyView;
                if (!m_scenes.ReadGpuProxy(proxy, proxyView) || !proxyView.HasCommonData() ||
                    proxyView.payloadKind != RenderProxyPayloadKind::Mesh || proxyView.gpuInstanceIndex == nullptr ||
                    *proxyView.gpuInstanceIndex == InvalidGpuSceneIndex)
                    continue;

                const RenderProxyTransform& transform = *proxyView.transform;
                f64 rowLengthSquared[3]{};
                const f32* rows[3]{transform.row0, transform.row1, transform.row2};
                bool validTransform = true;
                for (u32 row = 0; row < 3; ++row)
                {
                    rowLengthSquared[row] = static_cast<f64>(rows[row][0]) * rows[row][0] +
                                            static_cast<f64>(rows[row][1]) * rows[row][1] +
                                            static_cast<f64>(rows[row][2]) * rows[row][2];
                    validTransform = validTransform && std::isfinite(rowLengthSquared[row]) && rowLengthSquared[row] > 1.0e-16;
                }
                if (!validTransform)
                    continue;
                const f64 determinant =
                    static_cast<f64>(transform.row0[0]) *
                        (static_cast<f64>(transform.row1[1]) * transform.row2[2] - static_cast<f64>(transform.row1[2]) * transform.row2[1]) -
                    static_cast<f64>(transform.row0[1]) *
                        (static_cast<f64>(transform.row1[0]) * transform.row2[2] - static_cast<f64>(transform.row1[2]) * transform.row2[0]) +
                    static_cast<f64>(transform.row0[2]) *
                        (static_cast<f64>(transform.row1[0]) * transform.row2[1] - static_cast<f64>(transform.row1[1]) * transform.row2[0]);
                if (!std::isfinite(determinant))
                    continue;
                const bool mirrored = determinant < 0.0;

                const MeshDrawableBinding* const drawable = proxyView.meshDrawable;
                const MeshDrawableInfo* const drawableInfo = drawable != nullptr ? drawable->GetDrawable() : nullptr;
                if (drawableInfo == nullptr || !drawableInfo->renderable.IsValid() ||
                    drawableInfo->firstGpuPrimitive == InvalidGpuSceneIndex || drawableInfo->gpuPrimitiveCount == 0)
                    continue;

                for (u32 placementIndex = 0; placementIndex < drawableInfo->placementCount; ++placementIndex)
                {
                    const GeometryBatchPlacement* const placement = drawable->GetPlacement(placementIndex);
                    if (placement == nullptr || placement->normalState.phase != transparentPhase ||
                        placement->sourceSubmesh >= drawableInfo->gpuPrimitiveCount)
                        continue;
                    const GeometryBatchId shell = mirrored ? placement->mirroredShell : placement->normalShell;
                    const GeometryBatchId bin = mirrored ? placement->mirroredBin : placement->normalBin;
                    const GeometryBinKey& geometry = mirrored ? placement->mirroredGeometry : placement->normalGeometry;
                    if (!shell.IsValid() || !bin.IsValid() || !geometry.geometry.IsValid() || !placement->material.IsValid())
                        return RenderFrameExecutionStatus::Failure("transparent geometry placement is incomplete");
                    const f64 center[3]{
                        static_cast<f64>(transform.row0[3]) + static_cast<f64>(placement->sortCenter[0]) * transform.row0[0] +
                            static_cast<f64>(placement->sortCenter[1]) * transform.row1[0] + static_cast<f64>(placement->sortCenter[2]) * transform.row2[0],
                        static_cast<f64>(transform.row1[3]) + static_cast<f64>(placement->sortCenter[0]) * transform.row0[1] +
                            static_cast<f64>(placement->sortCenter[1]) * transform.row1[1] + static_cast<f64>(placement->sortCenter[2]) * transform.row2[1],
                        static_cast<f64>(transform.row2[3]) + static_cast<f64>(placement->sortCenter[0]) * transform.row0[2] +
                            static_cast<f64>(placement->sortCenter[1]) * transform.row1[2] + static_cast<f64>(placement->sortCenter[2]) * transform.row2[2]};
                    const f64 dx = center[0] - camera[0];
                    const f64 dy = center[1] - camera[1];
                    const f64 dz = center[2] - camera[2];
                    const f64 clipZ = dx * view.matrices.worldToClip[2] + dy * view.matrices.worldToClip[6] +
                                      dz * view.matrices.worldToClip[10] + view.matrices.worldToClip[14];
                    const f64 clipW = dx * view.matrices.worldToClip[3] + dy * view.matrices.worldToClip[7] +
                                      dz * view.matrices.worldToClip[11] + view.matrices.worldToClip[15];
                    if (!std::isfinite(clipZ) || !std::isfinite(clipW) || clipW == 0.0)
                        continue;
                    const f64 sortDepth = clipZ / clipW;
                    if (work.m_transparentDraws.Size() >= work.GetCapacity().geometryWorkItems)
                        return RenderFrameExecutionStatus::Failure("transparent geometry draw plan exceeds the bounded geometry work budget");
                    GeometryTransparentDraw draw;
                    draw.shell = shell;
                    draw.bin = bin;
                    draw.instance = {*proxyView.gpuInstanceIndex, drawableInfo->firstGpuPrimitive + placement->sourceSubmesh,
                                     geometry.geometry.index, placement->material.index};
                    draw.sortDepth = sortDepth;
                    draw.stableOrder = (static_cast<u64>(proxy.index) << 32u) | placementIndex;
                    const u32 before = work.m_transparentDraws.Size();
                    work.m_transparentDraws.PushBack(draw);
                    if (work.m_transparentDraws.Size() != before + 1u)
                        return RenderFrameExecutionStatus::Failure("transparent geometry draw plan allocation failed");
                }
            }

            range.drawCount = work.m_transparentDraws.Size() - range.firstDraw;
            if (range.drawCount > 1u)
            {
                auto* const begin = work.m_transparentDraws.TypedData() + range.firstDraw;
                std::sort(begin, begin + range.drawCount, [reverseDepth](const GeometryTransparentDraw& left, const GeometryTransparentDraw& right) noexcept
                          {
                              if (left.sortDepth != right.sortDepth)
                                  return reverseDepth ? left.sortDepth < right.sortDepth : left.sortDepth > right.sortDepth;
                              return left.stableOrder < right.stableOrder;
                          });
            }
            if (work.m_transparentInstances.Size() != range.firstDraw)
                return RenderFrameExecutionStatus::Failure("transparent geometry instance stream lost draw ordering");
            for (u32 ordinal = 0; ordinal < range.drawCount; ++ordinal)
            {
                const u32 before = work.m_transparentInstances.Size();
                work.m_transparentInstances.PushBack(work.m_transparentDraws[range.firstDraw + ordinal].instance);
                if (work.m_transparentInstances.Size() != before + 1u)
                    return RenderFrameExecutionStatus::Failure("transparent geometry instance stream allocation failed");
            }
        }
        return RenderFrameExecutionStatus::Success();
    }

    void FrameRenderer::FinishBuiltGraph(RetainedRenderFrameRef& retainedFrame, RenderNodeGraph& graph) noexcept
    {
        VG_PROFILE_ZONE_NAMED("RFCPU/Finish/Total");
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
        // Node execution and all continuations are joined at this point. Warm
        // binding templates borrow graph-cache contract storage, so sever those
        // views before either graph unlock below can permit cache eviction.
        retainedFrame.GetResourceBindings().ReleaseWarmContractViews();

        {
            VG_PROFILE_ZONE_NAMED("RFCPU/Finish/FinalizeSubmissions");
            commandLists.FinalizeSubmissions(retainedFrame.HasFailure());
        }
        if (commandLists.GetFirstSubmissionFailure(submissionFailure))
            retainedFrame.RecordFailure(FailureMessage(submissionFailure.message, "render-frame command-list submission failed"));
        {
            VG_PROFILE_ZONE_NAMED("RFCPU/Finish/FinalizeGpuTiming");
            FinalizeGpuTiming(retainedFrame.GetGeometryFrameWork(), commandLists.GetCommandScopeReceipts(), commandLists.WasDeviceLost());
        }

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
            bool allocatorSuccess = false;
            {
                VG_PROFILE_ZONE_NAMED("RFCPU/Finish/AllocatorFinish");
                allocatorSuccess = m_resourceAllocator.Finish(receipt, &terminalFailure);
            }
            if (!allocatorSuccess)
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
        bool ownerBoundaryPublished = false;
        if (output.IsValid())
        {
            const detail::RetainedOutputFinalizationAction outputAction =
                detail::SelectRetainedOutputFinalizationAction(output.IsValid(), output.GetKind(), commandLists.WasDeviceLost(),
                                                               allocatorCompleted, retainedFrame.HasFailure(), output.WasPresentNodeReached());
            if (outputAction == detail::RetainedOutputFinalizationAction::DeviceLost)
                output.m_viewport->DeviceLostOutput(output.m_acquisition);
            else if (outputAction == detail::RetainedOutputFinalizationAction::Abandon)
            {
                if (!output.WasPresentNodeReached() && !retainedFrame.HasFailure())
                    retainedFrame.RecordFailure("render graph did not execute its terminal output node");
                if (allocatorCompleted && output.GetKind() == RenderViewportOutputKind::Texture)
                {
                    const ExportSlotId exportSlot{output.m_resourceExportIndex, output.m_resourceExportGeneration};
                    PublishedResourceExport discardedExport;
                    RenderFlowResourceFailure discardFailure;
                    if (exportSlot.IsValid() && !m_resourceAllocator.TakeExport(exportSlot, discardedExport, &discardFailure) &&
                        discardFailure.code != RenderFlowResourceFailureCode::InvalidOrStaleIdentity)
                        retainedFrame.RecordFailure(discardFailure);
                    discardedExport.Reset();
                }
                ViewportFailure outputFailure;
                if (!output.m_viewport->AbandonOutput(output.m_acquisition, &outputFailure))
                {
                    retainedFrame.RecordFailure(FailureMessage(outputFailure.message, "render-frame output abandonment failed"));
                    output.m_viewport->DeviceLostOutput(output.m_acquisition);
                }
            }
            else if (outputAction == detail::RetainedOutputFinalizationAction::CompleteTexture)
            {
                const ExportSlotId exportSlot{output.m_resourceExportIndex, output.m_resourceExportGeneration};
                PublishedResourceExport exported;
                RenderFlowResourceFailure exportFailure;
                bool exportValid = exportSlot.IsValid() && m_resourceAllocator.TakeExport(exportSlot, exported, &exportFailure);
                RenderOutputPublicationReadinessKind readiness = RenderOutputPublicationReadinessKind::SameQueueContinuation;
                if (exportValid)
                {
                    if (exported.GetReadiness() == ExportReadinessKind::ExplicitFenceSignal)
                        readiness = RenderOutputPublicationReadinessKind::ExplicitFence;
                    else if (exported.GetReadiness() != ExportReadinessKind::SameQueueContinuation)
                        exportValid = false;
                    const RenderTextureOutputContract& contract = output.m_viewport->GetTextureOutputContract();
                    exportValid = exportValid && exported.GetKind() == FrameResourceKind::Texture && exported.GetTexture() == output.GetTexture() &&
                                  exported.GetTerminalState() == contract.restingState && exported.GetTerminalQueue() == contract.restingQueue &&
                                  readiness == contract.publicationReadiness && exported.GetReadyFence().IsValid() && exported.GetReadyFence().queue == contract.restingQueue;
                }
                ViewportFailure outputFailure;
                if (!exportValid)
                {
                    if (exportFailure.code != RenderFlowResourceFailureCode::None)
                        retainedFrame.RecordFailure(exportFailure);
                    else
                        retainedFrame.RecordFailure("render-frame texture output terminal export did not match its viewport contract");
                    if (!output.m_viewport->AbandonOutput(output.m_acquisition, &outputFailure))
                    {
                        retainedFrame.RecordFailure(FailureMessage(outputFailure.message, "render-frame texture output abandonment failed"));
                        output.m_viewport->DeviceLostOutput(output.m_acquisition);
                    }
                }
                else if (!output.CompleteTextureOutput(exported.GetTerminalState(), exported.GetTerminalQueue(), readiness, exported.GetReadyFence(), &outputFailure))
                {
                    retainedFrame.RecordFailure(FailureMessage(outputFailure.message, "render-frame texture output completion failed"));
                    if (output.m_acquisition.IsValid() && !output.m_viewport->AbandonOutput(output.m_acquisition, &outputFailure))
                        output.m_viewport->DeviceLostOutput(output.m_acquisition);
                }
                exported.Reset();
            }
            else if (outputAction == detail::RetainedOutputFinalizationAction::Present)
            {
                // Successful presentation is terminal output work, not a renderer-owner
                // dependency. Release every graph/camera/view owner first, then publish
                // the narrower boundary used by next-frame PreSimulation. The original
                // render-command tail remains active through Present and terminal
                // completion, preserving FlushFrame/viewport mutation/shutdown semantics.
                graph.ReleaseExclusiveUpdateFlag();
                retainedFrame.ClearJobsRenderFrame();
                retainedFrame.ReleaseOwnerStateForTerminalCompletion();
                // Snapshot device loss at the owner-safe point, before DXGI Present. Core GPU
                // removal/reset must still stop before Simulation, while a failure caused by
                // the later terminal Present remains a deterministic RenderUpdate failure.
                const rhi::DeviceState ownerDeviceState = rhi::IsInitialized() ? rhi::TestDeviceState() : rhi::DeviceState::Unknown;
                const bool ownerRequiresTerminalResolution =
                    ownerDeviceState == rhi::DeviceState::Removed || ownerDeviceState == rhi::DeviceState::ResetRequired;
                {
                    VG_PROFILE_ZONE_NAMED("RFCPU/Finish/PublishOwner");
                    retainedFrame.PublishOwnerCompletion(ownerRequiresTerminalResolution);
                }
                ownerBoundaryPublished = true;

                ViewportFailure outputFailure;
                bool presentSuccess = false;
                {
                    VG_PROFILE_ZONE_NAMED("RFCPU/Finish/ViewportPresent");
                    presentSuccess = output.m_viewport->Present(output.m_acquisition, &outputFailure);
                }
                if (!presentSuccess)
                {
                    retainedFrame.RecordFailure(FailureMessage(outputFailure.message, "render-frame presentation failed"));
                    output.m_viewport->DeviceLostOutput(output.m_acquisition);
                }
            }
            if (!output.m_acquisition.IsValid())
                output.m_viewport = nullptr;
        }

        auto& diagnosticWork = retainedFrame.GetGeometryFrameWork();
        {
            VG_PROFILE_ZONE_NAMED("RFCPU/Finish/Diagnostics");
            m_geometryDiagnostics.Complete(diagnosticWork.diagnosticSlot, diagnosticWork, commandLists.GetCommandScopeReceipts(), retainedFrame.HasFailure());
        }
        diagnosticWork.diagnosticSlot = GeometryDiagnosticReadbacks::InvalidSlot;
        if (!ownerBoundaryPublished)
        {
            graph.ReleaseExclusiveUpdateFlag();
            retainedFrame.ClearJobsRenderFrame();
            retainedFrame.ReleaseOwnerStateForTerminalCompletion();
            // Non-Present outputs reach their owner-safe point only after terminal output
            // finalization above. Snapshot core device removal/reset here as well so the next
            // PreSimulation cannot proceed merely because this frame had no DXGI Present.
            const rhi::DeviceState ownerDeviceState = rhi::IsInitialized() ? rhi::TestDeviceState() : rhi::DeviceState::Unknown;
            const bool ownerRequiresTerminalResolution =
                ownerDeviceState == rhi::DeviceState::Removed || ownerDeviceState == rhi::DeviceState::ResetRequired;
            retainedFrame.PublishOwnerCompletion(ownerRequiresTerminalResolution);
            ownerBoundaryPublished = true;
        }
        retainedFrame.PublishTerminalCompletion();
    }

    RenderFrameExecutionStatus FrameRenderer::ExecuteBuiltGraph(RenderFrameContext& context, RenderNodeGraph& graph,
                                                                 const FrameResourcePolicy& resourcePolicy,
                                                                 RenderGraphCache::CacheEntry* const graphCacheEntry) noexcept
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
                // NOTE: This exceptional setup-failure path synchronously joins the kickoff gate before cleanup. A future cleanup pass could replace
                // this wait with a preallocated FinishBuiltGraph continuation dependent on kickoffCounter, preserving failure robustness without occupying a worker.
                if (!kickoffCounter.Wait())
                    VG_FATAL("render-node setup failure could not join frame preparation before cleanup");
                FinishBuiltGraph(retainedFrame, *graphPointer);
            });
        jobs::Task resolveResourcesTask = jobs::Task::Create([this, retainedFrame, graphPointer = &graph, graphCacheEntry](const jobs::JobContext&) mutable noexcept
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
            const bool coldContractCompilation = graphCacheEntry != nullptr && m_resourceAllocator.GetState() == RenderFlowResourceSessionState::Planning;
            if (coldContractCompilation &&
                !graphPointer->CaptureResourceContract(graphCacheEntry->resourceContract, retainedFrame.GetResourceBindings(), m_resourceAllocator, &resourceFailure))
            {
                retainedFrame.RecordFailure(resourceFailure);
                m_resourceAllocator.CancelBeforePublication();
                return;
            }
            if (m_resourceAllocator.GetState() == RenderFlowResourceSessionState::Planning)
            {
                bool planningSealed = false;
                {
                    VG_PROFILE_ZONE_NAMED("RenderFlow/SealPlanning");
                    planningSealed = m_resourceAllocator.SealPlanning(PlanningJoinToken::CompletedSynchronously(), &resourceFailure);
                }
                if (!planningSealed)
                {
                    retainedFrame.RecordFailure(resourceFailure);
                    m_resourceAllocator.CancelBeforePublication();
                    return;
                }
            }
            else if (m_resourceAllocator.GetState() != RenderFlowResourceSessionState::CandidatesSealed)
            {
                resourceFailure.code = RenderFlowResourceFailureCode::InvalidPhase;
                resourceFailure.phase = m_resourceAllocator.GetState();
                resourceFailure.message = "resource contract entered Resolve from an invalid allocator phase";
                retainedFrame.RecordFailure(resourceFailure);
                m_resourceAllocator.CancelBeforePublication();
                return;
            }
            bool resourcesResolved = false;
            {
                VG_PROFILE_ZONE_NAMED("RenderFlow/Resolve");
                resourcesResolved = m_resourceAllocator.Resolve(nullptr, &resourceFailure);
            }
            if (!resourcesResolved)
            {
                retainedFrame.RecordFailure(resourceFailure);
                m_resourceAllocator.CancelBeforePublication();
                return;
            }
            if (coldContractCompilation)
            {
                const u64 publicationSerial = graphCacheEntry->resourceContract.GetPublicationSerial();
                if (publicationSerial != 0 && publicationSerial != graphCacheEntry->resourceContractPublicationSerial)
                {
                    const GeometryFrameWork& work = retainedFrame.GetGeometryFrameWork();
                    const RenderFrameOutputTransaction& currentOutput = retainedFrame.GetOutputTransaction();
                    const bool hasFrameOutput = currentOutput.IsValid();
                    const RenderViewportOutputKind outputKind = hasFrameOutput ? currentOutput.GetKind() : RenderViewportOutputKind::Presentation;
                    const RenderOutputColorEncoding outputEncoding =
                        hasFrameOutput ? currentOutput.m_viewport->GetOutputColorEncoding() : RenderOutputColorEncoding::SceneLinear;
                    const RenderTextureOutputContract* const textureOutputContract =
                        hasFrameOutput && outputKind == RenderViewportOutputKind::Texture ? &currentOutput.m_viewport->GetTextureOutputContract() : nullptr;
                    graphCacheEntry->resourceContractRevision = BuildResourceContractRevision(
                        *graphCacheEntry, work, work.diagnosticSlot != GeometryDiagnosticReadbacks::InvalidSlot,
                        hasFrameOutput, outputKind, outputEncoding, textureOutputContract);
                    graphCacheEntry->resourceContractPublicationSerial = publicationSerial;
                }
            }
        });
        jobs::Task finishResourceResolutionTask = jobs::Task::Create(
            [this, retainedFrame, graphPointer = &graph, kickoffDeferral = static_cast<jobs::CompletionDeferral&&>(kickoffDeferral)](const jobs::JobContext&) mutable noexcept
            {
                RenderFlowResourceFailure resourceFailure;
                if (!retainedFrame.HasFailure())
                {
                    bool executionBegun = false;
                    {
                        VG_PROFILE_ZONE_NAMED("RenderFlow/BeginExecution");
                        executionBegun = m_resourceAllocator.BeginExecution(&resourceFailure);
                    }
                    if (!executionBegun)
                        retainedFrame.RecordFailure(resourceFailure);
                }
                if (!retainedFrame.HasFailure())
                {
                    bool packetsPrepared = false;
                    {
                        VG_PROFILE_ZONE_NAMED("RenderFlow/PrepareExecutionPackets");
                        packetsPrepared = graphPointer->PrepareExecutionPackets(m_resourceAllocator, retainedFrame.GetFrameCommandLists(), retainedFrame.GetResourceBindings(), &resourceFailure);
                    }
                    if (!packetsPrepared)
                        retainedFrame.RecordFailure(resourceFailure);
                }
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
            if (!m_resourceAllocator.RegisterPresentationImport(output.GetBackBuffer(), FrameOutputImportPatch, imported, &resourceFailure))
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
            desc.contractPatch = FrameOutputImportPatch;
            rhi::Failure nativeFailure;
            if (!rhi::GetTextureDesc(desc.texture, desc.expected, &nativeFailure))
            {
                m_resourceAllocator.CancelBeforePublication();
                return RenderFrameExecutionStatus::Failure("render-flow texture output is unavailable");
            }
            const RenderTextureOutputContract& contract = output.m_viewport->GetTextureOutputContract();
            desc.initialState = contract.restingState;
            desc.terminalState = contract.restingState;
            desc.initialQueue = contract.restingQueue;
            desc.terminalQueue = contract.restingQueue;
            desc.readiness = ImportReadinessKind::SameQueueContinuation;
            ExportSlotId exportSlot;
            if (!m_resourceAllocator.ReserveExportSlot(FrameOutputExportPatch, exportSlot, &resourceFailure))
            {
                m_resourceAllocator.CancelBeforePublication();
                return RenderFrameExecutionStatus::Failure(FailureMessage(resourceFailure.message, "render-flow texture output export reservation failed"));
            }
            ImportedResourceId imported;
            if (!m_resourceAllocator.RegisterImport(desc, imported, &resourceFailure))
            {
                m_resourceAllocator.CancelBeforePublication();
                return RenderFrameExecutionStatus::Failure(FailureMessage(resourceFailure.message, "render-flow texture output import failed"));
            }
            output.m_resourceImportIndex = imported.index;
            output.m_resourceImportGeneration = imported.generation;
            output.m_resourceExportIndex = exportSlot.index;
            output.m_resourceExportGeneration = exportSlot.generation;
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
                desc.contractPatch = GeometryDiagnosticPatch;
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

        bool warmResourceContract = false;
        if (graphCacheEntry != nullptr)
        {
            const bool hasFrameOutput = output.IsValid();
            const RenderViewportOutputKind outputKind = hasFrameOutput ? output.GetKind() : RenderViewportOutputKind::Presentation;
            const RenderOutputColorEncoding outputEncoding =
                hasFrameOutput ? output.m_viewport->GetOutputColorEncoding() : RenderOutputColorEncoding::SceneLinear;
            const RenderTextureOutputContract* const textureOutputContract =
                hasFrameOutput && outputKind == RenderViewportOutputKind::Texture ? &output.m_viewport->GetTextureOutputContract() : nullptr;
            const RenderFlowResourceContractRevisionKey resourceContractRevision = BuildResourceContractRevision(
                *graphCacheEntry, diagnosticWork, diagnosticWork.diagnosticSlot != GeometryDiagnosticReadbacks::InvalidSlot,
                hasFrameOutput, outputKind, outputEncoding, textureOutputContract);
            if (graphCacheEntry->resourceContract.IsValid() &&
                graphCacheEntry->resourceContractPublicationSerial == graphCacheEntry->resourceContract.GetPublicationSerial() &&
                graphCacheEntry->resourceContractRevision == resourceContractRevision)
            {
                RenderFlowResourceFailure warmFailure;
                if (graph.InstantiateResourceContract(graphCacheEntry->resourceContract, resourceBindings, m_resourceAllocator, &warmFailure) &&
                    graph.PatchResourceContract(declarationContext, m_resourceAllocator, resourceBindings, &warmFailure) &&
                    m_resourceAllocator.BindResourceContract(graphCacheEntry->resourceContract, &warmFailure))
                {
                    warmResourceContract = true;
                }
            }
            if (!warmResourceContract)
            {
                // A partially-instantiated warm view may exist when patching or
                // backend-incarnation validation rejects the cache hit. Detach it
                // before cold compilation can eventually republish contract nodes.
                resourceBindings.ReleaseWarmContractViews();
                if (!m_resourceAllocator.BeginResourceContractCompilation(graphCacheEntry->resourceContract, &resourceFailure))
                {
                    m_geometryDiagnostics.Cancel(diagnosticWork.diagnosticSlot);
                    diagnosticWork.diagnosticSlot = GeometryDiagnosticReadbacks::InvalidSlot;
                    graph.ReleaseExclusiveUpdateFlag();
                    m_resourceAllocator.CancelBeforePublication();
                    return RenderFrameExecutionStatus::Failure(FailureMessage(resourceFailure.message, "resource-contract cold compilation setup failed"));
                }
            }
        }

        retainedFrame.InstallJobsRenderFrame();
        static jobs::JobName runRenderNodeJobsName{"RenderGraph/RunNodeJobs"};
        const bool nodeJobsDispatched = renderNodeJobsBuilder.Dispatch(runRenderNodeJobsName, static_cast<jobs::Task&&>(runRenderNodeJobsTask));
        if (!nodeJobsDispatched)
        {
            m_geometryDiagnostics.Cancel(diagnosticWork.diagnosticSlot);
            diagnosticWork.diagnosticSlot = GeometryDiagnosticReadbacks::InvalidSlot;
            retainedFrame.ClearJobsRenderFrame();
            resourceBindings.ReleaseWarmContractViews();
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
        if (!retainedFrame.HasFailure() && !warmResourceContract)
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
