#pragma once

#include <vanguard/rendering/render_scene.hpp>

#include <vanguard/jobs/jobs.hpp>

namespace vanguard::rendering
{
    class VisibilityFeedbackService;
    class RenderSceneFrameLifecycle;

    inline constexpr u32 MaximumRenderSceneViews = 1024;
    inline constexpr u32 MaximumRenderSceneCollections = 4096;
    inline constexpr u32 RenderViewFeatureStateSlots = 4;

    struct RenderSceneViewHandle
    {
        u32 index = ~u32{0};
        u32 generation = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return index < MaximumRenderSceneViews && generation != 0;
        }

        [[nodiscard]] friend constexpr bool operator==(const RenderSceneViewHandle&,
                                                       const RenderSceneViewHandle&) noexcept = default;
    };

    struct RenderSceneCollectionHandle
    {
        u32 index = ~u32{0};
        u32 generation = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return index < MaximumRenderSceneCollections && generation != 0;
        }

        [[nodiscard]] friend constexpr bool operator==(const RenderSceneCollectionHandle&,
                                                       const RenderSceneCollectionHandle&) noexcept = default;
    };

    struct ViewProxyState
    {
        RenderProxyHandle proxy;
        u64 previousVisibleFrame = 0;
        u64 lastVisibleFrame = 0;
        f32 distance = 0.0f;
        u16 selectedLod = 0;
        f32 dissolve = 0.0f;
        u32 featureState[RenderViewFeatureStateSlots]{};
    };

    struct MeshCollectorPacket
    {
        RenderProxySnapshot proxy;
        MeshProxySnapshot mesh;
        f32 distance = 0.0f;
        u16 selectedLod = 0;
        f32 dissolve = 0.0f;
    };

    struct LightCollectorPacket
    {
        RenderProxySnapshot proxy;
        LightProxySnapshot light;
        f32 distance = 0.0f;
    };

    struct DecalCollectorPacket
    {
        RenderProxySnapshot proxy;
        DecalProxySnapshot decal;
        f32 distance = 0.0f;
    };

    struct ViewCollectionRequest
    {
        RenderSceneViewHandle view;
        VisibilityQueryRequest visibility;
        f32 cameraPosition[3]{};
        u64 frameSerial = 0;
        u32 targetCellsPerJob = 64;
        u32 maximumMeshPackets = ~u32{0};
        u32 maximumLightPackets = ~u32{0};
        u32 maximumDecalPackets = ~u32{0};
    };

    struct ViewCollectionResult
    {
        RenderSceneCollectionHandle collection;
        RenderSceneViewHandle view;
        RenderSceneHandle scene;
        RenderSceneVersion version;
        u64 frameSerial = 0;
        u32 batchCount = 0;
        u32 candidateProxies = 0;
        u32 meshPackets = 0;
        u32 lightPackets = 0;
        u32 decalPackets = 0;
        u32 overflowedMeshPackets = 0;
        u32 overflowedLightPackets = 0;
        u32 overflowedDecalPackets = 0;
        bool succeeded = false;
        bool completed = false;
    };

    struct ViewCollectionOutput
    {
        ViewCollectionOutput() noexcept;

        ViewCollectionResult result;
        containers::DynamicArray<MeshCollectorPacket> meshes;
        containers::DynamicArray<LightCollectorPacket> lights;
        containers::DynamicArray<DecalCollectorPacket> decals;
    };

    struct RenderSceneCollectorConfig
    {
        u32 maximumViews = 64;
        u32 maximumCollections = 256;
    };

    class RenderSceneCollector final
    {
    public:
        struct Impl;

        RenderSceneCollector() noexcept = default;
        ~RenderSceneCollector();

        RenderSceneCollector(const RenderSceneCollector&) = delete;
        RenderSceneCollector& operator=(const RenderSceneCollector&) = delete;

        [[nodiscard]] bool Initialize(RenderSceneManager& scenes,
                                      const RenderSceneCollectorConfig& config = {},
                                      RenderSceneFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool Shutdown(RenderSceneFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;

        [[nodiscard]] bool CreateView(RenderSceneHandle scene, RenderSceneViewHandle& view,
                                      RenderSceneFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool DestroyView(RenderSceneViewHandle view,
                                       RenderSceneFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool ReadViewProxyState(RenderSceneViewHandle view, RenderProxyHandle proxy,
                                              ViewProxyState& state,
                                              RenderSceneFailure* failure = nullptr) const noexcept;

        [[nodiscard]] bool Dispatch(const ViewCollectionRequest& request,
                                    RenderSceneCollectionHandle& collection,
                                    RenderSceneFailure* failure = nullptr) noexcept;
        [[nodiscard]] const jobs::Counter* Completion(RenderSceneCollectionHandle collection) const noexcept;
        [[nodiscard]] bool IsReady(RenderSceneCollectionHandle collection) const noexcept;
        [[nodiscard]] bool Wait(RenderSceneCollectionHandle collection, i32 timeoutMilliseconds = -1) const noexcept;
        [[nodiscard]] bool CopyOutput(RenderSceneCollectionHandle collection, ViewCollectionOutput& output,
                                      RenderSceneFailure* failure = nullptr) const noexcept;
        [[nodiscard]] bool Release(RenderSceneCollectionHandle& collection,
                                   RenderSceneFailure* failure = nullptr) noexcept;

    private:
        friend class RenderSceneFrameLifecycle;

        [[nodiscard]] bool InspectRetirement(u64 frameSerial, u32& eligibleCollections,
                                             u32& pendingCollections,
                                             RenderSceneFailure* failure) const noexcept;
        [[nodiscard]] bool RetireThroughFrame(u64 frameSerial, VisibilityFeedbackService& feedback,
                                              u32& retiredCollections,
                                              RenderSceneFailure* failure) noexcept;
        void AttachFeedback(VisibilityFeedbackService* feedback) noexcept;

        Impl* m_impl = nullptr;
    };
} // namespace vanguard::rendering
