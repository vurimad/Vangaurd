#pragma once

#include <vanguard/asset_streaming/project_asset_pipeline.hpp>
#include <vanguard/editor/editor_project_service.hpp>
#include <vanguard/mesh_tools/model_import.hpp>

namespace vanguard::editor
{
    enum class EditorModelImportSessionPhase : u8
    {
        Empty,
        Inspecting,
        ProjectBusy,
        Review,
        ReviewInvalid,
        StalePlan,
        PreparingTransaction,
        Imported,
        Compiling,
        Cancelling,
        Ready,
        BuildFailed,
        ReconciliationPending,
        Cancelled,
        Failed
    };

    struct EditorModelImportSessionStatus final
    {
        EditorModelImportSessionPhase phase = EditorModelImportSessionPhase::Empty;
        mesh_tools::ModelImportResult inspection = mesh_tools::ModelImportResult::Success;
        asset_streaming::ProjectAssetPipelineResult pipeline = asset_streaming::ProjectAssetPipelineResult::Success;
        assets::AssetId asset;
        u64 acceptedGeneration = 0u;
        u32 inspectionProgressPermille = 0u;
        asset_streaming::ProjectAssetDeferredPlannedImportPreparePhase transactionPreparePhase =
            asset_streaming::ProjectAssetDeferredPlannedImportPreparePhase::None;
        u64 transactionPreparedBytes = 0u;
        u64 transactionTotalBytes = 0u;
        u32 outputCount = 0u;
        u32 builtOutputs = 0u;
        u32 readyOutputs = 0u;
        u32 failedOutputs = 0u;
        u32 retryOutputs = 0u;
        asset_streaming::ProjectAssetAutomaticRecookState automaticState =
            asset_streaming::ProjectAssetAutomaticRecookState::Disabled;
        u32 buildOperations = 0u;
        u32 completedBuildOperations = 0u;
        resources::ResourceReference publicationRetryOutput;
        assets::ProjectAssetBuildResult cohortAdmissionBuild = assets::ProjectAssetBuildResult::Success;
        bool cohortAdmissionBuildFailed = false;
        bool durableCommitted = false;
        bool reimport = false;
        // Busy does not invalidate a reviewed plan. An explicit user click may
        // queue its exact commit until project observation/transactions settle.
        bool commitQueued = false;
        u32 commitAttempts = 0u;
        u32 stalePlanSite = 0u;
    };

    // One transient, per-source authoring session. Inspection and candidate planning
    // are disposable; durable mutation happens only in CommitReviewedPlan(). The
    // session never owns or ticks a ProjectAssetPipeline.
    class EditorModelImportSession final
    {
    public:
        EditorModelImportSession() noexcept;
        ~EditorModelImportSession();

        EditorModelImportSession(const EditorModelImportSession&) = delete;
        EditorModelImportSession& operator=(const EditorModelImportSession&) = delete;

        [[nodiscard]] bool BeginExternalSource(const filesystem::AbsolutePath& source,
                                               u32 destinationRoot,
                                               containers::StringView rootRelativeFolder,
                                               const EditorProjectAssetAccess& assets) noexcept;
        // Preferred Asset Browser handoff: consumes one already-bounded immutable
        // external-source snapshot captured by the browser/file queue. The worker
        // inspects these exact bytes, and the same bytes are later committed only
        // after explicit review.
        [[nodiscard]] bool BeginExternalSnapshot(const filesystem::AbsolutePath& source,
                                                 containers::ArraySpan<const u8> sourceSnapshot,
                                                 u32 destinationRoot,
                                                 containers::StringView rootRelativeFolder,
                                                 const EditorProjectAssetAccess& assets) noexcept;
        // Reimport sessions are fed the exact currently-authored settings and
        // generation witnesses by their caller. No durable state is changed here.
        [[nodiscard]] bool BeginExistingSource(const filesystem::AbsolutePath& source,
                                               assets::AssetId asset,
                                               u64 expectedAcceptedGeneration,
                                               u64 expectedSourceGeneration,
                                               const mesh_tools::ModelImportSettings& settings,
                                               const EditorProjectAssetAccess& assets) noexcept;
        // Restarts inspection after project/source truth advances, preserving only
        // the user's transient candidate settings. Existing-asset callers supply
        // freshly observed accepted/source generations; first-import callers pass
        // zero for both.
        [[nodiscard]] bool Reinspect(u64 expectedAcceptedGeneration,
                                     u64 expectedSourceGeneration,
                                     const EditorProjectAssetAccess& assets) noexcept;

        void Poll(const EditorProjectAssetAccess& assets) noexcept;
        [[nodiscard]] bool Replan(const EditorProjectAssetAccess& assets) noexcept;
        // UI entrypoint: retain the exact reviewed settings and let at least one
        // complete UI frame present a truthful committing notice before the
        // potentially expensive authoritative owner-thread transaction starts.
        [[nodiscard]] bool QueueReviewedCommit() noexcept;
        [[nodiscard]] asset_streaming::ProjectAssetPipelineResult CommitReviewedPlan(
            const EditorProjectAssetAccess& assets,
            asset_streaming::ProjectAssetPipelineFailure* failure = nullptr) noexcept;
        // Stops only a non-durable queued retry, retaining the reviewed plan.
        [[nodiscard]] bool CancelQueuedCommit() noexcept;
        [[nodiscard]] asset_streaming::ProjectAssetPipelineResult Cancel(
            const EditorProjectAssetAccess& assets,
            asset_streaming::ProjectAssetPipelineFailure* failure = nullptr) noexcept;
        [[nodiscard]] asset_streaming::ProjectAssetPipelineResult Retry(
            const EditorProjectAssetAccess& assets,
            asset_streaming::ProjectAssetPipelineFailure* failure = nullptr) noexcept;
        void Reset() noexcept;

        // Candidate-only dependency policy helpers. They modify transient settings;
        // callers explicitly invoke Replan() afterward.
        [[nodiscard]] u32 AdoptResolvableProjectImages(const assets::SourceDatabase& sources) noexcept;
        [[nodiscard]] u32 IgnoreMissingTextures() noexcept;

        [[nodiscard]] EditorModelImportSessionStatus GetStatus() const noexcept;
        [[nodiscard]] const filesystem::AbsolutePath& GetSourcePath() const noexcept;
        [[nodiscard]] const mesh_tools::ModelImportManifest& GetManifest() const noexcept;
        [[nodiscard]] const mesh_tools::ModelImportReport& GetInspectionReport() const noexcept;
        [[nodiscard]] mesh_tools::ModelImportSettings& GetCandidateSettings() noexcept;
        [[nodiscard]] const mesh_tools::ModelImportSettings& GetCandidateSettings() const noexcept;
        [[nodiscard]] const mesh_tools::ModelImportPlan& GetPlan() const noexcept;
        [[nodiscard]] const mesh_tools::ModelImportPlanReport& GetPlanReport() const noexcept;

    private:
        struct Impl;
        Impl* m_impl = nullptr;
    };
} // namespace vanguard::editor
