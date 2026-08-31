#pragma once

#include <vanguard/rendering/render_scene.hpp>

namespace vanguard::rendering
{
    inline constexpr u32 MaximumVisibilityProbes = 1u << 20u;
    inline constexpr u32 MaximumVisibilityFeedbackViews = MaximumRenderViews;
    inline constexpr u32 MaximumVisibilityProbeNameBytes = 96;

    struct VisibilityProbeHandle
    {
        u32 index = ~u32{0};
        u32 generation = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return index < MaximumVisibilityProbes && generation != 0;
        }
        [[nodiscard]] friend constexpr bool operator==(const VisibilityProbeHandle&, const VisibilityProbeHandle&) noexcept = default;
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
        RenderViewId view;
        RenderViewFamilyId family;
    };

    /// Explicit, frame-scoped view input. This service does not own cameras or renderer view storage.
    struct VisibilityFeedbackView
    {
        RenderViewId view;
        RenderViewFamilyId family;
        VisibilityFrustum frustum;
        u32 queryMask = ~u32{0};
        bool streamingAuthority = false;
    };

    struct VisibilityProbeDesc
    {
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
        RenderSceneHandle scene;
        u64 mutationEpoch = 0;
        VisibilityViewPolicy viewPolicy;
        u64 viewSetRevision = 0;
        u64 publishedFrame = 0;
        u64 evaluatedFrame = 0;
        u64 ageInFrames = 0;
        u32 contributingViews = 0;
    };

    struct VisibilityFeedbackEvaluationRequest
    {
        containers::ArraySpan<const VisibilityFeedbackView> views;
        u64 mutationEpoch = 0;
        u64 frameSerial = 0;
        u64 viewSetRevision = 0;
        u32 targetProbesPerBatch = 1024;
    };

    struct VisibilityFeedbackEvaluationBatch
    {
        u64 serial = 0;
        u32 batchIndex = 0;
        u32 firstActiveProbe = 0;
        u32 probeCount = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return serial != 0 && probeCount != 0;
        }
    };

    struct VisibilityFeedbackEvaluationPlan
    {
        RenderSceneHandle scene;
        u64 mutationEpoch = 0;
        u64 frameSerial = 0;
        u64 viewSetRevision = 0;
        u64 serial = 0;
        u32 probeCount = 0;
        u32 batchCount = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return scene.IsValid() && frameSerial != 0 && viewSetRevision != 0 && serial != 0;
        }
    };

    struct VisibilityFeedbackBatchResult
    {
        u32 evaluatedProbes = 0;
        u32 visibleProbes = 0;
        u32 notVisibleProbes = 0;
        u32 unknownProbes = 0;
        bool completed = false;
    };

    struct VisibilityFeedbackServiceConfig
    {
        u32 maximumProbes = 16u * 1024u;
        u32 maximumViews = 64;
    };

    struct VisibilityFeedbackStats
    {
        u32 activeProbes = 0;
        u64 publishedFrame = 0;
        u64 createdProbes = 0;
        u64 destroyedProbes = 0;
        u64 evaluatedProbes = 0;
        u64 cancelledEvaluations = 0;
        u64 rejectedOperations = 0;
        bool initialized = false;
        bool evaluationOpen = false;
    };

    /// Scene-relative, non-renderable visibility probes. Planning is main-thread-only. After PrepareEvaluation,
    /// different batches may execute in parallel with distinct result/failure objects. CompleteEvaluation or
    /// CancelEvaluation is called only after their dependency joins. The attachment blocks scene destruction.
    class VisibilityFeedbackService final
    {
    public:
        struct Impl;

        VisibilityFeedbackService() noexcept = default;
        ~VisibilityFeedbackService();

        VisibilityFeedbackService(const VisibilityFeedbackService&) = delete;
        VisibilityFeedbackService& operator=(const VisibilityFeedbackService&) = delete;

        [[nodiscard]] bool Initialize(RenderSceneManager& scenes, RenderSceneHandle scene, const VisibilityFeedbackServiceConfig& config = {},
                                      RenderSceneFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool Shutdown(RenderSceneFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;

        [[nodiscard]] bool CreateProbe(const VisibilityProbeDesc& desc, VisibilityProbeHandle& probe, RenderSceneFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool UpdateProbe(VisibilityProbeHandle probe, const VisibilityProbeDesc& desc, RenderSceneFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool DestroyProbe(VisibilityProbeHandle& probe, RenderSceneFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool ReadProbe(VisibilityProbeHandle probe, VisibilityProbeSnapshot& snapshot, RenderSceneFailure* failure = nullptr) const noexcept;
        [[nodiscard]] bool ReadFeedback(VisibilityProbeHandle probe, u64 currentFrame, VisibilityFeedback& feedback,
                                        RenderSceneFailure* failure = nullptr) const noexcept;

        [[nodiscard]] bool PrepareEvaluation(const VisibilityFeedbackEvaluationRequest& request,
                                             containers::ArraySpan<VisibilityFeedbackEvaluationBatch> batchStorage, VisibilityFeedbackEvaluationPlan& plan,
                                             RenderSceneFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool EvaluateBatch(const VisibilityFeedbackEvaluationPlan& plan, const VisibilityFeedbackEvaluationBatch& batch,
                                         VisibilityFeedbackBatchResult& result, RenderSceneFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool CompleteEvaluation(const VisibilityFeedbackEvaluationPlan& plan, RenderSceneFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool CancelEvaluation(const VisibilityFeedbackEvaluationPlan& plan, RenderSceneFailure* failure = nullptr) noexcept;

        [[nodiscard]] VisibilityFeedbackStats GetStats() const noexcept;

    private:
        Impl* m_impl = nullptr;
    };
} // namespace vanguard::rendering
