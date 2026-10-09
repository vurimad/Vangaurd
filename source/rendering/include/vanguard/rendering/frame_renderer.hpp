#pragma once

#include <vanguard/concurrency/atomic.hpp>
#include <vanguard/rendering/gpu_scene_runtime.hpp>
#include <vanguard/rendering/frame_gpu_timings.hpp>
#include <vanguard/rendering/geometry_frame_work.hpp>
#include <vanguard/rendering/geometry_diagnostics.hpp>
#include <vanguard/rendering/render_command_system.hpp>
#include <vanguard/rendering/render_flow_resource_allocator.hpp>
#include <vanguard/rendering/render_graph_cache.hpp>
#include <vanguard/rendering/render_shader_map.hpp>
#include <vanguard/rendering/render_pipeline_factory.hpp>
#include <vanguard/rhi/gpu_counters.hpp>

namespace vanguard::rendering
{
    class RenderNodeGraph;
    struct RenderNodeImplContext;

    namespace detail
    {
        enum class RetainedOutputFinalizationAction : u8
        {
            None,
            DeviceLost,
            Abandon,
            CompleteTexture,
            Present
        };

        [[nodiscard]] constexpr RetainedOutputFinalizationAction SelectRetainedOutputFinalizationAction(
            const bool outputValid, const RenderViewportOutputKind outputKind, const bool deviceLost,
            const bool allocatorCompleted, const bool frameFailed, const bool presentNodeReached) noexcept
        {
            if (!outputValid)
                return RetainedOutputFinalizationAction::None;
            if (deviceLost)
                return RetainedOutputFinalizationAction::DeviceLost;
            if (!allocatorCompleted || frameFailed || !presentNodeReached)
                return RetainedOutputFinalizationAction::Abandon;
            if (outputKind == RenderViewportOutputKind::Texture)
                return RetainedOutputFinalizationAction::CompleteTexture;
            if (outputKind == RenderViewportOutputKind::Presentation)
                return RetainedOutputFinalizationAction::Present;
            return RetainedOutputFinalizationAction::Abandon;
        }
    } // namespace detail

    struct NamedRenderPipeline
    {
        containers::StringView name;
        RenderPipelineRequest request;
    };

    /// Renderer-side frame driver. Owns the ordered preparation that occurs after RenderCommandSystem enters
    /// the renderer CPU chain and before render-graph jobs are dispatched.
    class FrameRenderer final
    {
    public:
        FrameRenderer(RenderSceneManager& scenes, RenderCameraStorage& cameras, GpuSceneRuntime& gpuScene,
                      const GeometryFrameWorkConfig& geometryConfig = {}, u32 geometryBinCapacity = 16'384,
                      const RenderGeometryBatcher* geometryBatcher = nullptr, u32 geometryShellCapacity = 4'096,
                      const GeometryAllocator* geometryAllocator = nullptr, const RenderPhaseRegistry* phases = nullptr) noexcept;

        FrameRenderer(const FrameRenderer&) = delete;
        FrameRenderer& operator=(const FrameRenderer&) = delete;

        void ClearGraphCache() noexcept;
        // Startup only, before allocator initialization and graph construction.
        [[nodiscard]] RenderShaderResult InitializeShaders(containers::ArraySpan<const NamedRenderShader> shaders, rhi::Failure* failure = nullptr) noexcept;
        [[nodiscard]] const RenderShader* GetShader(containers::StringView name) const noexcept { return m_shaders.GetShader(name); }
        // Startup only, after cache/interface initialization and before graph construction or dispatch.
        [[nodiscard]] RenderPipelineResult InitializePipelines(containers::ArraySpan<const NamedRenderPipeline> pipelines, PipelineCache& cache) noexcept;
        [[nodiscard]] rhi::PipelineRef GetPipeline(containers::StringView name) const noexcept;
        [[nodiscard]] rhi::PipelineRef FindPipeline(containers::StringView name) const noexcept;
        [[nodiscard]] GpuSceneDirectoryBinding GetGeometryDirectoryBinding() const noexcept { return m_gpuScene.GetTables().GetDirectoryBinding(); }
        [[nodiscard]] f32 GetGeometryWorldCellSize() const noexcept { return m_gpuScene.GetScenePublisher().GetWorldCellSize(); }
        [[nodiscard]] RenderPhaseId GetGeometryPhase(RenderPhaseKey key) const noexcept { return m_phases != nullptr ? m_phases->Find(key) : RenderPhaseId{}; }
        [[nodiscard]] bool DeclareGeometrySceneUses(const RenderNodeImplContext& context, RenderFlowResourceFailure* failure) const noexcept;
        [[nodiscard]] bool DeclareGeometryArenaUses(const RenderNodeImplContext& context, RenderFlowResourceFailure* failure) const noexcept;
        [[nodiscard]] bool RecordGeometryPhase(const GeometryFrameWork& work, RenderPhaseId phase, const StaticSurfaceDrawContext& draw,
                                               const GeometryPhaseDrawBuffers& buffers, rhi::Failure* failure) const noexcept;
        // Joined shutdown: drop node borrows before releasing cache requests.
        void ClearPipelines() noexcept;
        [[nodiscard]] bool InitializeResourceAllocator(const RenderFlowResourceAllocatorConfig& config,
                                                       RenderFlowResourceFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool ClearResourceAllocatorCaches(RenderFlowResourceFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool ShutdownResourceAllocator(RenderFlowResourceFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool IsResourceAllocatorInitialized() const noexcept;
        [[nodiscard]] RenderFlowResourceAllocatorStats GetResourceAllocatorStats() const noexcept;
        // Single diagnostic consumer. Returns Pending without mapping until the
        // exact copy submission fence completes; never waits on the CPU.
        [[nodiscard]] GeometryDiagnosticsPoll PollGeometryDiagnostics(GeometryFrameDiagnostics& output, rhi::Failure* failure = nullptr) noexcept
        { return m_geometryDiagnostics.Poll(output, failure); }
        [[nodiscard]] u64 GetDroppedGeometryDiagnosticSamples() const noexcept { return m_geometryDiagnostics.GetDroppedSamples(); }
        [[nodiscard]] bool InitializeGpuTimings(rhi::Failure* failure = nullptr) noexcept;
        [[nodiscard]] bool ShutdownGpuTimings(rhi::Failure* failure = nullptr) noexcept;
        void AbandonGpuTimings() noexcept;
        [[nodiscard]] bool IsGpuTimingAvailable() const noexcept { return m_gpuTimings.IsInitialized(); }
        [[nodiscard]] FrameGpuTimingPoll PollGpuTimings(FrameGpuTimingSample& output, rhi::Failure* failure = nullptr) noexcept;
        [[nodiscard]] u64 GetDroppedGpuTimingFrames() const noexcept { return m_droppedGpuTimingFrames.GetValue(); }

        [[nodiscard]] RenderFrameExecutionStatus FrameTick(RenderFrameTickContext& context) noexcept;
        [[nodiscard]] RenderFrameExecutionStatus RenderFrame(RenderFrameContext& context) noexcept;

        [[nodiscard]] static RenderFrameExecutionStatus ExecuteFrameTick(RenderFrameTickContext& context, void* userData) noexcept;
        [[nodiscard]] static RenderFrameExecutionStatus ExecuteFrame(RenderFrameContext& context, void* userData) noexcept;

    private:
        friend class RenderNodeReduceGeometryDiagnostics;
        friend class RenderNodeBeginFrameGpuTiming;
        friend class RenderNodeBeginGeometryGpuTiming;
        friend class RenderNodeEndGeometryGpuTiming;
        friend class RenderNodeBeginCameraGpuTiming;
        friend class RenderNodeEndCameraGpuTiming;
        friend class RenderNodeEndFrameGpuTiming;
        [[nodiscard]] const GeometryFrameDiagnostics& GetGeometryDiagnosticPlan(u32 slot) const noexcept { return m_geometryDiagnostics.GetReport(slot); }
        void BuildRenderGraphBlank(RenderNodeGraph& graph, NodesContainer& nodes, bool isWireframe, bool isPrewarm) noexcept;
        void BuildRenderGraphHitProxies(RenderNodeGraph& graph, NodesContainer& nodes, bool isMultiLayer, bool isDebugView) noexcept;
        void BuildRenderGraphGBufferOnly(RenderNodeGraph& graph, NodesContainer& nodes, bool diagnostics) noexcept;
        void BuildRenderGraphCamera(RenderNodeGraph& graph, NodesContainer& nodes, const RenderView& view, bool gBufferOnly, bool diagnostics) noexcept;
        void BuildRenderGraphSafeMode(RenderNodeGraph& graph, NodesContainer& nodes, const RenderView& view) noexcept;
        void BuildRenderGraphNoScene(RenderNodeGraph& graph, NodesContainer& nodes, bool isWireframe) noexcept;
        void BuildRenderGraphTodvis(RenderNodeGraph& graph, NodesContainer& nodes, bool isDebugView) noexcept;
        void BuildRenderGraphDebugVisualization(RenderNodeGraph& graph, NodesContainer& nodes, const RenderView& view) noexcept;
        [[nodiscard]] RenderFrameExecutionStatus PrepareViewFamily(const RenderFrameInfo& frame, PreparedRenderViewFamily& preparedFamily,
                                                                   const PreparedRenderViewFamily*& family) noexcept;
        [[nodiscard]] RenderFrameExecutionStatus PrepareGeometryWork(RenderFrameContext& context, const PreparedRenderViewFamily& family) noexcept;
        [[nodiscard]] RenderFrameExecutionStatus RenderPreparedFrame(RenderFrameContext& context) noexcept;
        void BeginFrameGpuTiming(const RenderNodeImplContext& context) noexcept;
        void BeginGeometryGpuTiming(const RenderNodeImplContext& context) noexcept;
        void EndGeometryGpuTiming(const RenderNodeImplContext& context) noexcept;
        void BeginCameraGpuTiming(const RenderNodeImplContext& context) noexcept;
        void EndCameraGpuTiming(const RenderNodeImplContext& context) noexcept;
        void EndFrameGpuTiming(const RenderNodeImplContext& context) noexcept;
        void FinalizeGpuTiming(GeometryFrameWork& work, containers::ArraySpan<const CommandScopeExecutionReceipt> receipts,
                               bool deviceLost) noexcept;
        [[nodiscard]] bool RegisterGeometryImports(GeometryFrameWork& work, RenderFlowResourceFailure* failure) noexcept;
        [[nodiscard]] bool EnsureGeometryShellPlanBuffer(GeometryFrameWork& work, rhi::Failure* failure = nullptr) noexcept;
        [[nodiscard]] RenderFrameExecutionStatus ExecuteBuiltGraph(RenderFrameContext& context, RenderNodeGraph& graph,
                                                                   const FrameResourcePolicy& resourcePolicy) noexcept;
        void FinishBuiltGraph(RetainedRenderFrameRef& retainedFrame, RenderNodeGraph& graph) noexcept;

        RenderSceneManager& m_scenes;
        RenderCameraStorage& m_cameras;
        GpuSceneRuntime& m_gpuScene;
        GeometryFrameWorkConfig m_geometryConfig;
        u32 m_geometryBinCapacity = 0;
        const RenderGeometryBatcher* m_geometryBatcher = nullptr;
        u32 m_geometryShellCapacity = 0;
        const GeometryAllocator* m_geometryAllocator = nullptr;
        const RenderPhaseRegistry* m_phases = nullptr;
        RenderFlowResourceAllocator m_resourceAllocator;
        RenderShaderMap m_shaders;
        struct FeaturePipeline
        {
            containers::String name{memory::pools::Rendering::GetInstance()};
            PipelineRequest request;
        };
        containers::DynamicArray<FeaturePipeline> m_pipelines{memory::pools::Rendering::GetInstance()};
        RenderGraphCache m_graphCache;
        GeometryShellPlanCache m_geometryShellPlanCache;
        GeometryDiagnosticReadbacks m_geometryDiagnostics;
        rhi::GpuCounterSystem m_gpuTimings;
        concurrency::Atomic<u64> m_droppedGpuTimingFrames{0};
    };
} // namespace vanguard::rendering
