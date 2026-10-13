#pragma once

#include <vanguard/concurrency/synchronization.hpp>
#include <vanguard/memory/memory.hpp>
#include <vanguard/rendering/gpu_scene_visibility.hpp>
#include <vanguard/rendering/render_flow_resource_allocator.hpp>
#include <vanguard/rendering/render_scene.hpp>
#include <vanguard/rendering/render_geometry_batcher.hpp>
#include <vanguard/rhi/gpu_counters.hpp>
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
        u32 visibleInstances = 0;
        u32 geometryWorkItems = 0;
        u32 geometryBins = 0;
        u32 shellDraws = 0;
        u32 shellsPerView = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return views != 0 && views <= MaximumRenderViewsPerFamily && visibleInstances != InvalidGpuSceneIndex &&
                   geometryWorkItems != InvalidGpuSceneIndex && geometryBins != InvalidGpuSceneIndex &&
                   static_cast<u64>(shellDraws) * sizeof(GpuGeometryShellCounters) <= 0xffffffffull &&
                   static_cast<u64>(views) * shellsPerView < InvalidGpuSceneIndex;
        }
    };

    // Logical resources are produced by the existing render-flow declaration
    // pass. They are frame execution identities, not persistent GPU ownership.
    struct GeometryFrameGraphResources
    {
        LogicalResourceId views;
        LogicalResourceId resultRanges;
        LogicalResourceId visibleInstances;
        LogicalResourceId counters;
        LogicalResourceId geometryWork;
        LogicalResourceId geometryResults;
        LogicalResourceId geometryCounters;
        LogicalResourceId binItems;
        LogicalResourceId activeBins;
        LogicalResourceId drawInstances;
        LogicalResourceId transparentInstances;
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

    struct GeometryShellPlanCache
    {
        struct ViewKey
        {
            u64 phases = 0;
            bool reverseDepth = false;

            [[nodiscard]] friend constexpr bool operator==(const ViewKey&, const ViewKey&) noexcept = default;
        };

        GeometryShellPlanCache() noexcept : shellDraws(memory::pools::Rendering::GetInstance()) {}

        concurrency::SpinLock lock;
        containers::DynamicArray<GpuGeometryShellRange> shellDraws;
        rhi::Buffer gpuBuffer;
        GeometryPhaseDrawRange phaseDrawRanges[MaximumRenderViewsPerFamily * MaximumRenderPhases]{};
        ViewKey views[MaximumRenderViewsPerFamily]{};
        u32 argumentOffsets[MaximumRenderViewsPerFamily]{};
        u32 argumentCapacities[MaximumRenderViewsPerFamily]{};
        u64 catalogRevision = 0;
        u32 viewCount = 0;
        u32 shellsPerView = 0;
        u32 binsPerView = 0;
        u32 indirectArgumentCapacity = 0;
        u64 serial = 0;
        u64 gpuBufferSerial = 0;
        bool valid = false;
    };

    // Caller owns graph resource uses, targets and viewport/scissor setup.
    // All buffer bindings use offset zero; firstInstance is frame-absolute.
    struct GeometryPhaseDrawBuffers
    {
        rhi::BufferRef instances;
        rhi::BufferRef arguments;
        rhi::BufferRef counters;
    };

    struct GeometryTransparentDraw
    {
        GeometryBatchId shell;
        GeometryBatchId bin;
        MeshDrawInstance instance;
        f64 sortDepth = 0.0;
        u64 stableOrder = 0;
    };

    struct GeometryTransparentDrawRange
    {
        u32 firstDraw = 0;
        u32 drawCount = 0;
    };

    struct GeometryExternalBuffer
    {
        [[nodiscard]] LogicalResourceName GetLogicalName() const noexcept
        {
            return LogicalResourceName(containers::StringView(name), nameHash, LogicalResourceName::PrehashedTag{});
        }

        char name[48]{};
        u32 nameHash = 0;
        ImportedResourceId imported;
        rhi::ResourceState readState = rhi::ResourceState::Common;
        bool sceneTable = false;
    };

    // One retained frame owns one instance. Prepare makes one checked allocation
    // for the CPU-authored view/result/shell topology; persistent GPU Scene data is
    // consumed directly and is never copied into frame-local candidate storage.
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
        // Optional bounded GPU timing state for this retained frame. Tokens are
        // indexed by compact prepared-family view, never persistent camera slot.
        rhi::GpuCounterFrameHandle gpuTimingFrame;
        rhi::GpuCounterScopeToken geometryTimingScope;
        rhi::GpuCounterScopeToken cameraTimingScopes[MaximumRenderViewsPerFamily]{};
        CommandScopeId gpuTimingResolveScope;
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
        /// Geometry hot path: build one GPU view/result partition per prepared
        /// view without manufacturing CPU candidate lists or work ranges.
        [[nodiscard]] GeometryFrameWorkResult BuildDirectVisibilityPlan(RenderSceneHandle scene, containers::ArraySpan<const RenderView> views,
                                                                       u32 instanceTraversalExtent, u32 visibleCapacityPerView) noexcept;
        [[nodiscard]] containers::ArraySpan<GpuDirectionalLightSelection> GetDirectionalLights() noexcept { return m_directionalLights; }
        [[nodiscard]] containers::ArraySpan<const GpuDirectionalLightSelection> GetDirectionalLights() const noexcept { return {m_directionalLights.Data(), m_directionalLights.Size()}; }
        [[nodiscard]] containers::ArraySpan<GpuVisibilityCounters> GetCounters() noexcept { return m_counters; }
        [[nodiscard]] containers::ArraySpan<const GpuVisibilityCounters> GetCounters() const noexcept
        {
            return {m_counters.Data(), m_counters.Size()};
        }
        [[nodiscard]] containers::ArraySpan<const GpuGeometryResultRange> GetGeometryResults() const noexcept
        {
            return {m_geometryResults.Data(), m_geometryResultCount};
        }

        [[nodiscard]] GeometryFrameWorkResult CommitVisibilityPlan(const GpuVisibilityPlan& plan) noexcept;
        [[nodiscard]] GeometryFrameWorkResult PlanShellDraws(const RenderGeometryBatcher* batcher, GeometryShellPlanCache* cache = nullptr) noexcept;
        [[nodiscard]] containers::ArraySpan<const GpuGeometryShellRange> GetShellDraws() const noexcept
        {
            return {m_shellDraws.Data(), m_shellDrawCount};
        }
        [[nodiscard]] u32 GetIndirectArgumentCapacity() const noexcept { return m_indirectArgumentCapacity; }
        [[nodiscard]] GeometryTransparentDrawRange GetTransparentDrawRange(u32 viewIndex) const noexcept
        {
            return viewIndex < MaximumRenderViewsPerFamily ? m_transparentDrawRanges[viewIndex] : GeometryTransparentDrawRange{};
        }
        [[nodiscard]] containers::ArraySpan<const MeshDrawInstance> GetTransparentInstances(u32 viewIndex) const noexcept
        {
            const GeometryTransparentDrawRange range = GetTransparentDrawRange(viewIndex);
            return range.firstDraw <= m_transparentInstances.Size() && range.drawCount <= m_transparentInstances.Size() - range.firstDraw
                       ? containers::ArraySpan<const MeshDrawInstance>{m_transparentInstances.TypedData() + range.firstDraw, range.drawCount}
                       : containers::ArraySpan<const MeshDrawInstance>{};
        }
        [[nodiscard]] containers::ArraySpan<const MeshDrawInstance> GetAllTransparentInstances() const noexcept
        {
            return {m_transparentInstances.TypedData(), m_transparentInstances.Size()};
        }
        // Recording body for a graph phase node. viewIndex is the compact frame
        // view ordinal. The caller declares GPU Scene/arena/stream/indirect uses
        // and establishes targets, viewport/scissor and descriptor domains.
        [[nodiscard]] bool RecordPhase(const RenderGeometryBatcher& batcher, const GeometryAllocator& geometry,
                                      RenderPhaseId phase, const StaticSurfaceDrawContext& drawContext,
                                      const GeometryPhaseDrawBuffers& buffers, rhi::Failure* failure = nullptr) const noexcept;
        [[nodiscard]] bool RecordTransparentPhase(const RenderGeometryBatcher& batcher, const GeometryAllocator& geometry,
                                                  RenderPhaseId phase, const StaticSurfaceDrawContext& drawContext,
                                                  rhi::BufferRef instances, rhi::Failure* failure = nullptr) const noexcept;
        void ClearVisibilityPlan() noexcept
        {
            m_visibilityPlan = {};
            m_shellDrawsPlanned = false;
        }
        [[nodiscard]] const GpuVisibilityPlan& GetVisibilityPlan() const noexcept { return m_visibilityPlan; }
        void SetShellPlanImport(ImportedResourceId imported) noexcept { m_shellPlanImport = imported; }
        [[nodiscard]] ImportedResourceId GetShellPlanImport() const noexcept { return m_shellPlanImport; }
        void PinShellPlanBuffer(const rhi::Buffer& buffer) noexcept { m_shellPlanBuffer = buffer; }
        [[nodiscard]] rhi::BufferRef GetShellPlanBuffer() const noexcept { return m_shellPlanBuffer.GetRef(); }
        [[nodiscard]] u64 GetShellPlanCacheSerial() const noexcept { return m_shellPlanCacheSerial; }

        [[nodiscard]] GeometryFrameGraphResources& GetGraphResources() noexcept { return m_graphResources; }
        [[nodiscard]] const GeometryFrameGraphResources& GetGraphResources() const noexcept { return m_graphResources; }
        [[nodiscard]] containers::DynamicArray<GeometryExternalBuffer>& GetExternalBuffers() noexcept { return m_externalBuffers; }
        [[nodiscard]] const containers::DynamicArray<GeometryExternalBuffer>& GetExternalBuffers() const noexcept { return m_externalBuffers; }

    private:
        friend class FrameRenderer;
        memory::MemoryBlock m_storage;
        containers::ArraySpan<GpuView> m_views;
        containers::ArraySpan<GpuDirectionalLightSelection> m_directionalLights;
        containers::ArraySpan<GpuVisibilityResultRange> m_resultRanges;
        containers::ArraySpan<GpuVisibilityCounters> m_counters;
        containers::ArraySpan<GpuGeometryResultRange> m_geometryResults;
        containers::ArraySpan<GpuGeometryShellRange> m_shellDraws;
        containers::ArraySpan<GeometryPhaseDrawRange> m_phaseDrawRanges;
        GpuVisibilityPlan m_visibilityPlan;
        GeometryFrameGraphResources m_graphResources;
        ImportedResourceId m_shellPlanImport;
        rhi::Buffer m_shellPlanBuffer;
        // Import identities only; native ownership belongs to the allocator's
        // retained import records. Prepared once before resource declarations.
        containers::DynamicArray<GeometryExternalBuffer> m_externalBuffers{memory::pools::Rendering::GetInstance()};
        containers::DynamicArray<GeometryTransparentDraw> m_transparentDraws{memory::pools::Rendering::GetInstance()};
        containers::DynamicArray<MeshDrawInstance> m_transparentInstances{memory::pools::Rendering::GetInstance()};
        GeometryTransparentDrawRange m_transparentDrawRanges[MaximumRenderViewsPerFamily]{};
        GeometryFrameWorkCapacity m_capacity;
        u32 m_geometryResultCount = 0;
        u32 m_shellDrawCount = 0;
        u32 m_indirectArgumentCapacity = 0;
        u64 m_catalogRevision = 0;
        u64 m_shellPlanCacheSerial = 0;
        bool m_shellDrawsPlanned = false;
        u64 m_frameSerial = 0;
        GeometryOutputRegion m_outputRegions[MaximumRenderViewsPerFamily]{};
        u32 m_cameraInputMasks[MaximumRenderViewsPerFamily]{};
        u32 m_outputRegionCount = 0;
    };
} // namespace vanguard::rendering
