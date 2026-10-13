#pragma once

#include <vanguard/rendering/render_node_graph_factory.hpp>
#include <vanguard/rendering/viewport.hpp>

namespace vanguard::rendering
{
    struct RenderViewGraphKey
    {
        RenderingMode mode = RenderingMode::Shaded;
        RenderViewPurpose purpose = RenderViewPurpose::Main;
        RenderViewFlags flags = RenderViewFlags::None;
        RenderPhaseSet phases;
        u64 featureMask = 0;
        u32 width = 0;
        u32 height = 0;
        u64 hash = 0;

        void RebuildHash() noexcept;
        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] bool operator==(const RenderViewGraphKey& other) const noexcept;
    };

    struct RenderGraphDependencyKey
    {
        u32 parentViewIndex = 0;
        u32 childViewIndex = 0;
        RenderCameraDependencyOutputs outputs = RenderCameraDependencyOutputs::None;

        [[nodiscard]] friend constexpr bool operator==(const RenderGraphDependencyKey&, const RenderGraphDependencyKey&) noexcept = default;
    };

    struct RenderGraphKey
    {
        RenderingMode mode = RenderingMode::Shaded;
        RenderFramePurpose purpose = RenderFramePurpose::Normal;
        RenderViewportOutputKind outputKind = RenderViewportOutputKind::Headless;
        ViewportExtent renderExtent;
        ViewportExtent outputExtent;
        u64 featureMask = 0;
        u64 rendererRevision = 0;
        containers::FixedArray<RenderViewGraphKey, MaximumRenderViewsPerFamily> views;
        containers::FixedArray<RenderGraphDependencyKey, MaximumRenderViewsPerFamily * MaximumRenderCameraDependencies> dependencies;
        u32 viewCount = 0;
        u32 dependencyCount = 0;
        bool present = false;
        u64 hash = 0;

        void RebuildHash() noexcept;
        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] bool operator==(const RenderGraphKey& other) const noexcept;
    };

    // Exact Layer-2 declaration-shape witness owned beside the compiled
    // resource contract. `hash` is only a fast reject; equality always checks
    // every semantic field so warm admission never relies on digest collision
    // improbability.
    struct RenderFlowResourceContractRevisionKey
    {
        u64 hash = 0;
        u32 views = 0;
        u32 visibleInstances = 0;
        u32 geometryWorkItems = 0;
        u32 geometryBins = 0;
        u32 shellDraws = 0;
        u32 shellsPerView = 0;
        u32 indirectArgumentCapacity = 0;
        u32 shellDrawCount = 0;
        u32 outputRegionCount = 0;
        containers::FixedArray<u32, MaximumRenderViewsPerFamily> outputRegionViews;
        RenderViewportOutputKind outputKind = RenderViewportOutputKind::Headless;
        RenderOutputColorEncoding outputEncoding = RenderOutputColorEncoding::SceneLinear;
        rhi::ResourceState textureRestingState = rhi::ResourceState::Unknown;
        rhi::QueueType textureRestingQueue = rhi::QueueType::Graphics;
        RenderOutputPublicationReadinessKind texturePublicationReadiness = RenderOutputPublicationReadinessKind::SameQueueContinuation;
        bool diagnosticReadback = false;
        bool hasFrameOutput = false;
        bool hasTextureOutput = false;

        [[nodiscard]] bool operator==(const RenderFlowResourceContractRevisionKey& other) const noexcept
        {
            if (outputRegionCount > MaximumRenderViewsPerFamily || other.outputRegionCount > MaximumRenderViewsPerFamily)
                return false;
            if (hash != other.hash || views != other.views || visibleInstances != other.visibleInstances ||
                geometryWorkItems != other.geometryWorkItems || geometryBins != other.geometryBins || shellDraws != other.shellDraws ||
                shellsPerView != other.shellsPerView || indirectArgumentCapacity != other.indirectArgumentCapacity ||
                shellDrawCount != other.shellDrawCount || outputRegionCount != other.outputRegionCount ||
                outputKind != other.outputKind || outputEncoding != other.outputEncoding || textureRestingState != other.textureRestingState ||
                textureRestingQueue != other.textureRestingQueue || texturePublicationReadiness != other.texturePublicationReadiness ||
                diagnosticReadback != other.diagnosticReadback || hasFrameOutput != other.hasFrameOutput || hasTextureOutput != other.hasTextureOutput)
                return false;
            for (u32 index = 0; index < outputRegionCount; ++index)
                if (outputRegionViews[index] != other.outputRegionViews[index])
                    return false;
            return true;
        }
    };

    class RenderGraphCache final
    {
    public:
        static constexpr u32 MaximumCacheEntries = 4;

        struct CameraSetupData
        {
            VANGUARD_USE_MEMORY_POOL(memory::pools::Rendering);

            RenderNodeGraph graph;
            NodesContainer nodes;
            RenderViewGraphKey cameraSetupKey;

            [[nodiscard]] bool DoesNeedRebuild(const RenderViewGraphKey& other) noexcept;
        };

        struct CacheEntry
        {
            CacheEntry();
            ~CacheEntry();
            CacheEntry(const CacheEntry&) = delete;
            CacheEntry& operator=(const CacheEntry&) = delete;

            void PostBuildClear() noexcept;
            [[nodiscard]] CameraSetupData* FindCameraSetup(const RenderViewGraphKey& cameraKey) const noexcept;
            void Reset() noexcept;

            u64 lastUsedFrame = 0;
            RenderGraphKey graphKey;
            RenderNodeGraph graph;
            RenderFlowResourceContract resourceContract;
            RenderFlowResourceContractRevisionKey resourceContractRevision;
            u64 resourceContractPublicationSerial = 0;
            containers::DynamicArray<CameraSetupData*> cameraBuildData;
            bool valid = false;
        };

        RenderGraphCache() noexcept = default;
        ~RenderGraphCache() = default;
        RenderGraphCache(const RenderGraphCache&) = delete;
        RenderGraphCache& operator=(const RenderGraphCache&) = delete;

        [[nodiscard]] CacheEntry* GetGraph(u64 frameSerial, const RenderGraphKey& graphKey, u32 cameraSetupCount, bool& needsRebuild,
                                           bool forceClear = false) noexcept;
        void Clear() noexcept;

    private:
        containers::FixedArray<CacheEntry, MaximumCacheEntries> m_cacheEntries;
    };
} // namespace vanguard::rendering
