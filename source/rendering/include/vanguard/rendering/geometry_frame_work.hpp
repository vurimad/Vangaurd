#pragma once

#include <vanguard/memory/memory.hpp>
#include <vanguard/rendering/gpu_scene_visibility.hpp>
#include <vanguard/rendering/render_flow_resource_allocator.hpp>
#include <vanguard/rendering/render_scene.hpp>
#include <vanguard/rendering/render_geometry_batcher.hpp>
#include <vanguard/rendering/mesh_draw_layout.hpp>

namespace vanguard::rendering
{
    struct GeometryFrameWorkConfig
    {
        // Explicit bounded expansion budget. Overflow is counted on the GPU;
        // increasing this value changes capacity, not graph topology.
        u32 maximumWorkItemsPerVisible = 8;
    };

    struct GeometryFrameWorkCapacity
    {
        u32 views = 0;
        u32 candidates = 0;
        u32 workRanges = 0;
        u32 visibleInstances = 0;
        u32 expansionWorkRanges = 0;
        u32 geometryWorkItems = 0;
        u32 geometryWorkRanges = 0;
        u32 geometryBins = 0;
        u32 binPrefixRanges = 0;
        u32 prefixBlocks = 0;
        // Shared spatial batch layout; result/range/failure slots are per view.
        u32 candidateBatches = 0;
        u32 shellDraws = 0;
        u32 shellsPerView = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return views != 0 && views <= MaximumRenderViewsPerFamily && candidates != InvalidGpuSceneIndex &&
                   workRanges != InvalidGpuSceneIndex && visibleInstances != InvalidGpuSceneIndex &&
                   expansionWorkRanges != InvalidGpuSceneIndex && geometryWorkItems != InvalidGpuSceneIndex &&
                   geometryWorkRanges != InvalidGpuSceneIndex && geometryBins != InvalidGpuSceneIndex &&
                   binPrefixRanges != InvalidGpuSceneIndex && prefixBlocks != InvalidGpuSceneIndex &&
                   static_cast<u64>(shellDraws) * sizeof(GpuGeometryShellCounters) <= 0xffffffffull &&
                   static_cast<u64>(views) * shellsPerView < InvalidGpuSceneIndex &&
                   static_cast<u64>(views) * candidateBatches < InvalidGpuSceneIndex;
        }
    };

    struct GeometryCandidateView
    {
        VisibilityQueryRequest request;
        GpuVisibilityCandidateReservation reservation;
        u32 diagnosticCandidates = 0;
        u32 diagnosticUnresolvedIdentities = 0;
    };

    // Logical resources are produced by the existing render-flow declaration
    // pass. They are frame execution identities, not persistent GPU ownership.
    struct GeometryFrameGraphResources
    {
        LogicalResourceId views;
        LogicalResourceId candidates;
        LogicalResourceId workRanges;
        LogicalResourceId resultRanges;
        LogicalResourceId visibleInstances;
        LogicalResourceId counters;
        LogicalResourceId expansionWorkRanges;
        LogicalResourceId expansionItems;
        LogicalResourceId geometryWork;
        LogicalResourceId geometryWorkRanges;
        LogicalResourceId binPrefixRanges;
        LogicalResourceId prefixBlocks;
        LogicalResourceId geometryResults;
        LogicalResourceId geometryCounters;
        LogicalResourceId binItems;
        LogicalResourceId drawInstances;
        LogicalResourceId indirectArguments;
        LogicalResourceId shellPlans;
        LogicalResourceId shellRanges;
        LogicalResourceId shellCounters;
        LogicalResourceId directionalLights;

        void Reset() noexcept { *this = {}; }
    };

    enum class GeometryFrameWorkResult : u8
    {
        Success,
        AlreadyPrepared,
        InvalidDescriptor,
        CapacityExceeded,
        InvalidVisibilityPlan,
        IncompatibleDepthConvention
    };

    struct GeometryPhaseDrawRange
    {
        u32 firstShell = 0;
        u32 shellCount = 0;
    };

    // Caller owns graph resource uses, targets and viewport/scissor setup.
    // All buffer bindings use offset zero; firstInstance is frame-absolute.
    struct GeometryPhaseDrawBuffers
    {
        rhi::BufferRef instances;
        rhi::BufferRef arguments;
        rhi::BufferRef counters;
    };

    struct GeometryExternalBuffer
    {
        char name[48]{};
        ImportedResourceId imported;
        rhi::ResourceState readState = rhi::ResourceState::Common;
        bool sceneTable = false;
    };

    // One retained frame owns one instance. Prepare makes one checked allocation
    // and partitions it into caller-owned visibility spans; candidate producers
    // later write those spans directly, without an intermediate candidate copy.
    struct GeometryOutputRegion
    {
        u32 viewIndex = 0;
        RenderViewRect rect;
    };

    class GeometryFrameWork final
    {
        static_assert(MaximumRenderViewsPerFamily <= 32);
    public:
        GeometryFrameWork() noexcept = default;
        ~GeometryFrameWork();
        GeometryFrameWork(const GeometryFrameWork&) = delete;
        GeometryFrameWork& operator=(const GeometryFrameWork&) = delete;
        GeometryFrameWork(GeometryFrameWork&&) = delete;
        GeometryFrameWork& operator=(GeometryFrameWork&&) = delete;

        [[nodiscard]] GeometryFrameWorkResult Prepare(u64 frameSerial, const GeometryFrameWorkCapacity& capacity) noexcept;
        void Reset() noexcept;

        [[nodiscard]] bool IsPrepared() const noexcept { return m_frameSerial != 0; }
        [[nodiscard]] u64 GetFrameSerial() const noexcept { return m_frameSerial; }
        // Optional diagnostic sample owned by FrameRenderer, never scene data.
        u32 diagnosticSlot = ~u32{0};
        ImportedResourceId diagnosticReadbackImport;
        CommandScopeId diagnosticCopyScope;
        mutable u32 diagnosticPipelineBinds[MaximumRenderViewsPerFamily]{};
        [[nodiscard]] containers::ArraySpan<const GeometryOutputRegion> GetOutputRegions() const noexcept { return {m_outputRegions, m_outputRegionCount}; }
        [[nodiscard]] u32 GetCameraInputMask(u32 viewIndex) const noexcept { return m_cameraInputMasks[viewIndex]; }
        void AddCameraInput(u32 consumer, u32 producer) noexcept { m_cameraInputMasks[consumer] |= u32{1} << producer; }
        [[nodiscard]] bool AddOutputRegion(const GeometryOutputRegion& region) noexcept
        {
            if (m_outputRegionCount >= MaximumRenderViewsPerFamily || region.viewIndex >= m_capacity.views || !region.rect.IsValid()) return false;
            m_outputRegions[m_outputRegionCount++] = region;
            return true;
        }
        [[nodiscard]] GeometryFrameWorkCapacity GetCapacity() const noexcept { return m_capacity; }
        [[nodiscard]] GpuVisibilityBuildStorage GetVisibilityBuildStorage() noexcept;
        [[nodiscard]] containers::ArraySpan<RenderSceneGpuCandidateBatch> GetCandidateBatches() noexcept { return m_candidateBatches; }
        [[nodiscard]] containers::ArraySpan<GeometryCandidateView> GetCandidateViews() noexcept { return m_candidateViews; }
        [[nodiscard]] containers::ArraySpan<const GeometryCandidateView> GetCandidateViews() const noexcept { return {m_candidateViews.Data(), m_candidateViews.Size()}; }
        [[nodiscard]] containers::ArraySpan<GpuDirectionalLightSelection> GetDirectionalLights() noexcept { return m_directionalLights; }
        [[nodiscard]] containers::ArraySpan<const GpuDirectionalLightSelection> GetDirectionalLights() const noexcept { return {m_directionalLights.Data(), m_directionalLights.Size()}; }
        [[nodiscard]] containers::ArraySpan<GpuVisibilityCandidateRange> GetCandidateRanges() noexcept { return m_candidateRanges; }
        [[nodiscard]] containers::ArraySpan<RenderSceneGpuCandidateBatchResult> GetCandidateResults() noexcept { return m_candidateResults; }
        [[nodiscard]] containers::ArraySpan<RenderSceneFailure> GetCandidateFailures() noexcept { return m_candidateFailures; }
        [[nodiscard]] containers::ArraySpan<GpuVisibilityCounters> GetCounters() noexcept { return m_counters; }
        [[nodiscard]] containers::ArraySpan<const GpuVisibilityCounters> GetCounters() const noexcept
        {
            return {m_counters.Data(), m_counters.Size()};
        }
        [[nodiscard]] containers::ArraySpan<const GpuGeometryExpansionWorkRange> GetExpansionWorkRanges() const noexcept
        {
            return {m_expansionWorkRanges.Data(), m_expansionWorkRangeCount};
        }
        [[nodiscard]] containers::ArraySpan<const GpuGeometryWorkRange> GetGeometryWorkRanges() const noexcept
        {
            return {m_geometryWorkRanges.Data(), m_geometryWorkRangeCount};
        }
        [[nodiscard]] containers::ArraySpan<const GpuGeometryPrefixWorkRange> GetBinPrefixRanges() const noexcept
        {
            return {m_binPrefixRanges.Data(), m_binPrefixRangeCount};
        }
        [[nodiscard]] u32 GetPrefixBlockCount() const noexcept { return m_prefixBlockCount; }
        [[nodiscard]] containers::ArraySpan<const GpuGeometryResultRange> GetGeometryResults() const noexcept
        {
            return {m_geometryResults.Data(), m_geometryResultCount};
        }

        [[nodiscard]] GeometryFrameWorkResult CommitVisibilityPlan(const GpuVisibilityPlan& plan) noexcept;
        [[nodiscard]] GeometryFrameWorkResult PlanShellDraws(const RenderGeometryBatcher* batcher) noexcept;
        [[nodiscard]] containers::ArraySpan<const GpuGeometryShellRange> GetShellDraws() const noexcept
        {
            return {m_shellDraws.Data(), m_shellDrawCount};
        }
        [[nodiscard]] u32 GetIndirectArgumentCapacity() const noexcept { return m_indirectArgumentCapacity; }
        // Recording body for a graph phase node. viewIndex is the compact frame
        // view ordinal. The caller declares GPU Scene/arena/stream/indirect uses
        // and establishes targets, viewport/scissor and descriptor domains.
        [[nodiscard]] bool RecordPhase(const RenderGeometryBatcher& batcher, const GeometryAllocator& geometry,
                                      RenderPhaseId phase, const StaticSurfaceDrawContext& drawContext,
                                      const GeometryPhaseDrawBuffers& buffers, rhi::Failure* failure = nullptr) const noexcept;
        void ClearVisibilityPlan() noexcept
        {
            m_visibilityPlan = {};
            m_shellDrawsPlanned = false;
        }
        [[nodiscard]] const GpuVisibilityPlan& GetVisibilityPlan() const noexcept { return m_visibilityPlan; }
        [[nodiscard]] GpuVisibilityPlanBuilder& GetVisibilityBuilder() noexcept;
        [[nodiscard]] RenderSceneGpuCandidatePlan& GetCandidatePlan() noexcept { return m_candidatePlan; }
        [[nodiscard]] const RenderSceneGpuCandidatePlan& GetCandidatePlan() const noexcept { return m_candidatePlan; }

        [[nodiscard]] GeometryFrameGraphResources& GetGraphResources() noexcept { return m_graphResources; }
        [[nodiscard]] const GeometryFrameGraphResources& GetGraphResources() const noexcept { return m_graphResources; }
        [[nodiscard]] containers::DynamicArray<GeometryExternalBuffer>& GetExternalBuffers() noexcept { return m_externalBuffers; }
        [[nodiscard]] const containers::DynamicArray<GeometryExternalBuffer>& GetExternalBuffers() const noexcept { return m_externalBuffers; }

    private:
        memory::MemoryBlock m_storage;
        containers::ArraySpan<GpuView> m_views;
        containers::ArraySpan<GpuDirectionalLightSelection> m_directionalLights;
        containers::ArraySpan<GpuInstanceIndex> m_candidates;
        containers::ArraySpan<GpuVisibilityWorkRange> m_workRanges;
        containers::ArraySpan<GpuVisibilityResultRange> m_resultRanges;
        containers::ArraySpan<GpuVisibilityCounters> m_counters;
        containers::ArraySpan<GpuGeometryExpansionWorkRange> m_expansionWorkRanges;
        containers::ArraySpan<GpuGeometryWorkRange> m_geometryWorkRanges;
        containers::ArraySpan<GpuGeometryPrefixWorkRange> m_binPrefixRanges;
        containers::ArraySpan<GpuGeometryResultRange> m_geometryResults;
        containers::ArraySpan<GpuGeometryShellRange> m_shellDraws;
        containers::ArraySpan<GeometryPhaseDrawRange> m_phaseDrawRanges;
        containers::ArraySpan<RenderSceneGpuCandidateBatch> m_candidateBatches;
        containers::ArraySpan<GeometryCandidateView> m_candidateViews;
        containers::ArraySpan<GpuVisibilityCandidateRange> m_candidateRanges;
        containers::ArraySpan<RenderSceneGpuCandidateBatchResult> m_candidateResults;
        containers::ArraySpan<RenderSceneFailure> m_candidateFailures;
        GpuVisibilityPlanBuilder* m_visibilityBuilder = nullptr;
        GpuVisibilityPlan m_visibilityPlan;
        RenderSceneGpuCandidatePlan m_candidatePlan;
        GeometryFrameGraphResources m_graphResources;
        // Import identities only; native ownership belongs to the allocator's
        // retained import records. Prepared once before resource declarations.
        containers::DynamicArray<GeometryExternalBuffer> m_externalBuffers{memory::pools::Rendering::GetInstance()};
        GeometryFrameWorkCapacity m_capacity;
        u32 m_expansionWorkRangeCount = 0;
        u32 m_geometryWorkRangeCount = 0;
        u32 m_binPrefixRangeCount = 0;
        u32 m_prefixBlockCount = 0;
        u32 m_geometryResultCount = 0;
        u32 m_shellDrawCount = 0;
        u32 m_indirectArgumentCapacity = 0;
        u64 m_catalogRevision = 0;
        bool m_shellDrawsPlanned = false;
        u64 m_frameSerial = 0;
        GeometryOutputRegion m_outputRegions[MaximumRenderViewsPerFamily]{};
        u32 m_cameraInputMasks[MaximumRenderViewsPerFamily]{};
        u32 m_outputRegionCount = 0;
    };
} // namespace vanguard::rendering
