#pragma once

#include <vanguard/rendering/render_scene_collector.hpp>

namespace vanguard::rendering
{
    inline constexpr u32 MaximumVisibilityProbes = 1u << 20u;
    inline constexpr u32 MaximumVisibilityFeedbackViews = MaximumRenderSceneViews;
    inline constexpr u32 MaximumVisibilityProbeNameBytes = 96;

    struct VisibilityProbeHandle
    {
        u32 index = ~u32{0};
        u32 generation = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return index < MaximumVisibilityProbes && generation != 0;
        }

        [[nodiscard]] friend constexpr bool operator==(const VisibilityProbeHandle&,
                                                       const VisibilityProbeHandle&) noexcept = default;
    };

    enum class VisibilityFeedbackState : u8
    {
        Unknown,
        Visible,
        NotVisible
    };

    enum class VisibilityViewPolicyKind : u8
    {
        SpecificView,
        ViewFamily,
        StreamingAuthority
    };

    struct VisibilityViewPolicy
    {
        VisibilityViewPolicyKind kind = VisibilityViewPolicyKind::StreamingAuthority;
        RenderSceneViewHandle view;
        u64 family = 0;
    };

    struct VisibilityFeedbackViewDesc
    {
        RenderSceneViewHandle view;
        RenderSceneHandle scene;
        u64 family = 0;
        bool streamingAuthority = false;
    };

    struct VisibilityProbeDesc
    {
        RenderSceneHandle scene;
        RenderProxyBounds bounds;
        VisibilityViewPolicy viewPolicy;
        u32 queryMask = ~u32{0};
        const char* debugName = nullptr;
    };

    struct VisibilityProbeSnapshot
    {
        VisibilityProbeHandle handle;
        RenderSceneHandle scene;
        RenderProxyBounds bounds;
        VisibilityViewPolicy viewPolicy;
        u32 queryMask = 0;
        u64 descriptorRevision = 0;
        char debugName[MaximumVisibilityProbeNameBytes]{};
    };

    struct VisibilityFeedback
    {
        VisibilityProbeHandle probe;
        VisibilityFeedbackState state = VisibilityFeedbackState::Unknown;
        RenderSceneVersion sceneVersion;
        VisibilityViewPolicy viewPolicy;
        u64 viewSetRevision = 0;
        u64 evaluatedFrame = 0;
        u64 ageInFrames = 0;
        u32 contributingViews = 0;
    };

    struct VisibilityProbeEvaluationInput
    {
        VisibilityProbeHandle probe;
        RenderProxyBounds bounds;
        u64 descriptorRevision = 0;
        u64 viewSetRevision = 0;
    };

    struct VisibilityProbeObservation
    {
        VisibilityProbeHandle probe;
        VisibilityFeedbackState state = VisibilityFeedbackState::Unknown;
        RenderSceneVersion sceneVersion;
        u64 descriptorRevision = 0;
        u64 viewSetRevision = 0;
        u64 frameSerial = 0;
    };

    struct VisibilityFeedbackServiceConfig
    {
        u32 maximumProbes = 16u * 1024u;
        u32 maximumViews = 64;
    };

    struct VisibilityFeedbackStats
    {
        u32 activeProbes = 0;
        u32 registeredViews = 0;
        u64 viewSetRevision = 0;
        u64 publishedFrame = 0;
        u64 createdProbes = 0;
        u64 destroyedProbes = 0;
        u64 evaluatedProbes = 0;
        u64 rejectedOperations = 0;
        bool initialized = false;
    };

    class VisibilityFeedbackService final
    {
    public:
        struct Impl;

        VisibilityFeedbackService() noexcept = default;
        ~VisibilityFeedbackService();

        VisibilityFeedbackService(const VisibilityFeedbackService&) = delete;
        VisibilityFeedbackService& operator=(const VisibilityFeedbackService&) = delete;

        [[nodiscard]] bool Initialize(RenderSceneManager& scenes,
                                      const VisibilityFeedbackServiceConfig& config = {},
                                      RenderSceneFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool Shutdown(RenderSceneFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;

        [[nodiscard]] bool RegisterView(const VisibilityFeedbackViewDesc& desc,
                                        RenderSceneFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool UnregisterView(RenderSceneViewHandle view,
                                          RenderSceneFailure* failure = nullptr) noexcept;

        [[nodiscard]] bool CreateProbe(const VisibilityProbeDesc& desc, VisibilityProbeHandle& probe,
                                       RenderSceneFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool UpdateProbe(VisibilityProbeHandle probe, const VisibilityProbeDesc& desc,
                                       RenderSceneFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool DestroyProbe(VisibilityProbeHandle& probe,
                                        RenderSceneFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool ReadProbe(VisibilityProbeHandle probe, VisibilityProbeSnapshot& snapshot,
                                     RenderSceneFailure* failure = nullptr) const noexcept;
        [[nodiscard]] bool ReadFeedback(VisibilityProbeHandle probe, u64 currentFrame,
                                        VisibilityFeedback& feedback,
                                        RenderSceneFailure* failure = nullptr) const noexcept;

        [[nodiscard]] VisibilityFeedbackStats GetStats() const noexcept;

    private:
        friend class RenderSceneFrameLifecycle;
        friend class RenderSceneCollector;
        friend struct RenderSceneCollector::Impl;

        [[nodiscard]] bool BeginFrame(u64 frameSerial, RenderSceneFailure* failure) noexcept;
        [[nodiscard]] bool Capture(const ViewCollectionRequest& request,
                                   containers::DynamicArray<VisibilityProbeEvaluationInput>& inputs,
                                   RenderSceneFailure* failure) const noexcept;
        static void Evaluate(const ViewCollectionRequest& request,
                             const ViewCollectionResult& result,
                             const containers::DynamicArray<VisibilityProbeEvaluationInput>& inputs,
                             containers::DynamicArray<VisibilityProbeObservation>& observations) noexcept;
        [[nodiscard]] bool Accumulate(const containers::DynamicArray<VisibilityProbeObservation>& observations,
                                      const ViewCollectionResult& result,
                                      RenderSceneFailure* failure) noexcept;
        [[nodiscard]] bool Publish(RenderSceneFailure* failure) noexcept;

        Impl* m_impl = nullptr;
    };

    enum class RenderSceneEndFrameStatus : u8
    {
        Complete,
        Pending,
        Failure
    };

    struct RenderSceneEndFrameResult
    {
        u64 frameSerial = 0;
        u32 retiredCollections = 0;
        u32 pendingCollections = 0;
        u32 reclaimedVersions = 0;
        u32 retainedVersions = 0;
        u32 versionsBlockedByReaders = 0;
        u32 liveReadLeases = 0;
        bool feedbackPublished = false;
        bool fullyRetired = false;
    };

    class RenderSceneFrameLifecycle final
    {
    public:
        [[nodiscard]] bool Initialize(RenderSceneManager& scenes, RenderSceneCollector& collector,
                                      VisibilityFeedbackService& feedback,
                                      RenderSceneFailure* failure = nullptr) noexcept;
        void Shutdown() noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;

        [[nodiscard]] RenderSceneEndFrameStatus EndFrame(u64 frameSerial,
                                                         RenderSceneEndFrameResult& result,
                                                         RenderSceneFailure* failure = nullptr) noexcept;

    private:
        RenderSceneManager* m_scenes = nullptr;
        RenderSceneCollector* m_collector = nullptr;
        VisibilityFeedbackService* m_feedback = nullptr;
        u64 m_lastEndedFrame = 0;
    };
} // namespace vanguard::rendering
