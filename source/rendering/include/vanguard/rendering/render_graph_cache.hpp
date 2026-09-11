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
