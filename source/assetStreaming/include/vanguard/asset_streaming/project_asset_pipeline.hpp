#pragma once

#include <vanguard/asset_streaming/project_asset_change_state.hpp>
#include <vanguard/asset_streaming/project_asset_observation.hpp>
#include <vanguard/asset_streaming/project_loose_publisher.hpp>
#include <vanguard/assets/asset_operations.hpp>
#include <vanguard/assets/asset_recooker.hpp>
#include <vanguard/assets/project_asset_build_system.hpp>

namespace vanguard::filesystem
{
    class DirectoryObservationSource;
}

namespace vanguard::asset_streaming
{
    enum class ProjectAssetSnapshotResult : u8
    {
        NotChecked,
        Success,
        NotFound,
        InvalidState,
        Corrupt,
        Incompatible,
        IoFailure,
        OutOfMemory,
        LimitExceeded
    };

    [[nodiscard]] const char* ToString(ProjectAssetSnapshotResult result) noexcept;

    enum class ProjectAssetPipelineResult : u8
    {
        Success,
        Pending,
        InvalidArgument,
        InvalidState,
        Busy,
        BuildInitializationFailed,
        BuildRequestFailed,
        IndexSaveFailed,
        PublicationFailed,
        ReconciliationFailed,
        SnapshotLoadFailed,
        SnapshotSaveFailed,
        OperationInitializationFailed,
        OperationFailed,
        StalePlan,
        IdentityGenerationFailed,
        HistoryLimitExceeded,
        UndoUnavailable,
        RedoUnavailable,
        CommittedCleanupPending,
        CommittedReconciliationPending,
        ShutdownFailed,
        ObservationUnavailable,
        ObservationAlreadyActive,
        ObservationNotActive,
        CacheMaintenanceFailed,
        LooseMaintenanceFailed
    };

    [[nodiscard]] const char* ToString(ProjectAssetPipelineResult result) noexcept;

    // E2.3 automatic recook coordination is opt-in. P1 defines the bounded
    // owner-state contract, P2 populates owned pending evidence, and P3 drives
    // that evidence through the existing IncrementalRecooker/BuildGraph owners.
    struct ProjectAssetAutomaticRecookConfig
    {
        bool enabled = false;
        u32 maximumPendingAssets = 4096;
        u32 maximumPendingInputs = 16384;
        u32 maximumPendingOutputs = 16384;
        u32 maximumRetryEntries = 4096;
        u64 maximumCapturedEvidenceBytes = 256ull * 1024ull * 1024ull;

        [[nodiscard]] bool IsValid() const noexcept
        {
            return maximumPendingAssets != 0u && maximumPendingInputs != 0u && maximumPendingOutputs != 0u &&
                   maximumRetryEntries >= maximumPendingAssets && maximumCapturedEvidenceBytes != 0u;
        }
    };

    enum class ProjectAssetAutomaticRecookState : u8
    {
        Disabled,
        Idle,
        Pending,
        Planning,
        Building,
        Validating,
        Cancelling,
        Failed,
        RecoveryRequired
    };

    enum class ProjectAssetAutomaticRecookDirtyReason : u32
    {
        None = 0,
        AcceptedChange = 1u << 0u,
        StartupRecovery = 1u << 1u,
        ExplicitRetry = 1u << 2u,
        StaleCompletion = 1u << 3u,
        FailedBatch = 1u << 4u,
        CancelledBatch = 1u << 5u,
        OverflowRecovery = 1u << 6u,
        DeliveryOverflow = 1u << 7u,
        CacheRepairRequired = 1u << 8u,
        ExplicitReimport = 1u << 9u
    };

    [[nodiscard]] constexpr ProjectAssetAutomaticRecookDirtyReason operator|(const ProjectAssetAutomaticRecookDirtyReason left,
                                                                             const ProjectAssetAutomaticRecookDirtyReason right) noexcept
    {
        return static_cast<ProjectAssetAutomaticRecookDirtyReason>(static_cast<u32>(left) | static_cast<u32>(right));
    }

    constexpr ProjectAssetAutomaticRecookDirtyReason& operator|=(ProjectAssetAutomaticRecookDirtyReason& left,
                                                                 const ProjectAssetAutomaticRecookDirtyReason right) noexcept
    {
        left = left | right;
        return left;
    }

    [[nodiscard]] constexpr bool HasAutomaticRecookDirtyReason(const ProjectAssetAutomaticRecookDirtyReason value,
                                                               const ProjectAssetAutomaticRecookDirtyReason reason) noexcept
    {
        return (static_cast<u32>(value) & static_cast<u32>(reason)) != 0u;
    }

    struct ProjectAssetAutomaticRecookStatus
    {
        ProjectAssetAutomaticRecookState state = ProjectAssetAutomaticRecookState::Disabled;
        ProjectAssetAutomaticRecookDirtyReason dirtyReasons = ProjectAssetAutomaticRecookDirtyReason::None;
        // Terminal worker diagnostic, retained across idle UI frames until a
        // new plan begins. A failed recook need not have failed output records.
        assets::RecookFailure lastRecookFailure = assets::RecookFailure::None;
        assets::RecookRootFailureDetail firstFailedRoot;
        u32 failedRootCount = 0u;
        u32 pendingAssets = 0;
        u32 pendingInputs = 0;
        u32 pendingOutputs = 0;
        u32 pendingRepairOutputs = 0;
        u32 pendingReimportOutputs = 0;
        u32 pendingDiagnostics = 0;
        // Number of retained failed-batch asset entries eligible for an explicit
        // retry/newer-change recovery pass. P5 requires maximumRetryEntries to be
        // large enough to retain any admitted pending asset set without truncation.
        u32 retryEntries = 0;
        u32 activeRoots = 0;
        u32 completedRoots = 0;
        u32 buildOperations = 0;
        u32 completedBuildOperations = 0;
        u32 pendingDeliveryOutputs = 0;
        u64 activeAcceptedGeneration = 0;
        u64 capturedEvidenceBytes = 0;
        u64 pendingDeliveryEvidenceBytes = 0;
        bool retryRequested = false;
        bool cancellationRequested = false;
        bool recoveryRequired = false;
    };

    enum class ProjectAssetDeliveryStageState : u8
    {
        Untracked,
        Pending,
        Committed,
        Installed,
        Current,
        Stale,
        Failed
    };

    enum class ProjectAssetDeliveryReason : u8
    {
        None,
        AwaitingPublication,
        BuildAdvancedPastPublication,
        AwaitingResidentReplacement,
        ResidentNotTracked,
        DeliveryCapacityExceeded,
        PublicationFailed,
        PublicationFreshnessRejected,
        CacheRepairRequired
    };

    // Copyable owner snapshot for one cooked output. `authoredGeneration` is the
    // accepted source generation represented by the latest automatic committed
    // record; it is not a claim that no newer authored change exists. P1 records
    // committed and manually installed generations, but does not make P2's
    // descriptor-current or P3's resident-current claims.
    struct ProjectAssetOutputDeliveryStatus
    {
        resources::ResourceReference resource;
        u64 authoredGeneration = 0;
        assets::BuildFingerprint committedBuildFingerprint;
        assets::BuildFingerprint committedContentFingerprint;
        assets::BuildFingerprint publishedBuildFingerprint;
        assets::BuildFingerprint publishedContentFingerprint;
        filesystem::AbsolutePath publishedPhysicalPath;
        u32 residentResourceGeneration = 0;
        assets::BuildFingerprint residentBuildFingerprint;
        assets::BuildFingerprint residentContentFingerprint;
        ProjectAssetDeliveryStageState buildState = ProjectAssetDeliveryStageState::Untracked;
        ProjectAssetDeliveryStageState publicationState = ProjectAssetDeliveryStageState::Untracked;
        ProjectAssetDeliveryStageState residentState = ProjectAssetDeliveryStageState::Untracked;
        ProjectAssetDeliveryReason publicationReason = ProjectAssetDeliveryReason::None;
        ProjectAssetDeliveryReason residentReason = ProjectAssetDeliveryReason::ResidentNotTracked;
        bool publicationRetryRequired = false;
        bool residentRetryRequired = false;
    };

    // Allocation-free owner snapshot for UI/diagnostic consumers that only need
    // publication identity/truth. Unlike ProjectAssetOutputDeliveryStatus this
    // deliberately excludes the owning physical-path string and resident fields,
    // so clipped browser rows can observe publication changes without copying
    // path storage or creating a second delivery authority.
    struct ProjectAssetOutputPublicationStatus
    {
        resources::ResourceReference resource;
        u64 authoredGeneration = 0;
        assets::BuildFingerprint committedBuildFingerprint;
        assets::BuildFingerprint committedContentFingerprint;
        assets::BuildFingerprint publishedBuildFingerprint;
        assets::BuildFingerprint publishedContentFingerprint;
        ProjectAssetDeliveryStageState buildState = ProjectAssetDeliveryStageState::Untracked;
        ProjectAssetDeliveryStageState publicationState = ProjectAssetDeliveryStageState::Untracked;
        ProjectAssetDeliveryReason publicationReason = ProjectAssetDeliveryReason::None;
        bool publicationRetryRequired = false;
    };

    // Neutral P3 resident evidence reported by the engine/editor composition
    // owner after consulting the actual runtime resident. No rendering types
    // cross this boundary. The pipeline derives Current/Stale truth against the
    // currently published exact build/content fingerprints.
    struct ProjectAssetResidentDeliveryEvidence
    {
        resources::ResourceReference resource;
        u32 resourceGeneration = 0;
        assets::BuildFingerprint buildFingerprint;
        assets::BuildFingerprint contentFingerprint;
        bool retryRequired = false;

        [[nodiscard]] bool IsValid() const noexcept
        {
            return resource.IsValid() && resource.IsTyped() && resourceGeneration != 0 && !buildFingerprint.IsEmpty() && !contentFingerprint.IsEmpty();
        }
    };

    // Neutral P4 retirement evidence reported by the composition owner only
    // after an external resident owner has completed retirement of this exact
    // CPU resource generation. A newer current resident is not equivalent to
    // this signal.
    struct ProjectAssetResidentRetirementEvidence
    {
        resources::ResourcePath resource;
        u32 resourceGeneration = 0;

        [[nodiscard]] bool IsValid() const noexcept
        {
            return resource.IsValid() && resourceGeneration != 0u;
        }
    };

    enum class ProjectAssetResidentRetirementResult : u8
    {
        Accepted,
        NotTracked,
        Busy,
        InvalidArgument,
        InvalidState
    };

    struct ProjectAssetCacheMaintenanceConfig
    {
        u64 maximumVddcBytes = 4ull * 1024ull * 1024ull * 1024ull;
        u32 maximumVddcProtectionRecords = 262144;
        u64 maximumVddcProtectionBytes = 64ull * 1024ull * 1024ull;
        u32 maximumVddcScanEntries = 262144;
        u64 maximumVddcScanBytes = 64ull * 1024ull * 1024ull * 1024ull;
        u32 maximumVddcDeletesPerBatch = 1024;
        u32 maximumRetiredLooseCandidates = 262144;
        u64 maximumRetiredLooseBytes = 64ull * 1024ull * 1024ull * 1024ull;
        u32 maximumLooseScanEntries = 262144;
        u64 maximumLooseScanBytes = 64ull * 1024ull * 1024ull * 1024ull;
        u32 maximumLooseDeletesPerBatch = 1024;
        u32 maximumStatusRows = 64;

        [[nodiscard]] bool IsValid() const noexcept
        {
            return maximumVddcBytes != 0u && maximumVddcProtectionRecords != 0u && maximumVddcProtectionBytes != 0u &&
                   maximumVddcScanEntries != 0u && maximumVddcScanBytes >= maximumVddcBytes &&
                   maximumVddcDeletesPerBatch != 0u && maximumRetiredLooseCandidates != 0u &&
                   maximumRetiredLooseBytes != 0u && maximumLooseScanEntries != 0u &&
                   maximumLooseScanBytes >= maximumRetiredLooseBytes && maximumLooseDeletesPerBatch != 0u &&
                   maximumStatusRows != 0u;
        }

        [[nodiscard]] assets::PersistentCacheMaintenanceLimits VddcLimits() const noexcept
        {
            assets::PersistentCacheMaintenanceLimits limits;
            limits.maximumCacheBytes = maximumVddcBytes;
            limits.maximumProtectionRecords = maximumVddcProtectionRecords;
            limits.maximumProtectionBytes = maximumVddcProtectionBytes;
            limits.maximumScanEntries = maximumVddcScanEntries;
            limits.maximumScanBytes = maximumVddcScanBytes;
            limits.maximumDeleteRecords = maximumVddcDeletesPerBatch;
            limits.maximumDiagnosticRows = maximumStatusRows;
            return limits;
        }
    };

    enum class ProjectAssetVddcMaintenanceState : u8
    {
        Idle,
        Requested,
        Running,
        Complete,
        Deferred,
        Failed,
        Cancelled
    };

    struct ProjectAssetVddcMaintenanceStatus
    {
        ProjectAssetVddcMaintenanceState state = ProjectAssetVddcMaintenanceState::Idle;
        assets::PersistentCacheMaintenanceResult lastResult = assets::PersistentCacheMaintenanceResult::Success;
        u64 protectionEpoch = 0;
        u32 scannedRecords = 0;
        u64 scannedBytes = 0;
        u32 protectedRecords = 0;
        u64 protectedBytes = 0;
        u32 eligibleRecords = 0;
        u64 eligibleBytes = 0;
        u32 removedRecords = 0;
        u64 removedBytes = 0;
        u32 deferredDeletes = 0;
        u32 failedDeletes = 0;
        u32 foreignFiles = 0;
        u32 stageFiles = 0;
        bool protectedOverBudget = false;
        bool deleteBatchLimitReached = false;
        bool budgetSatisfied = false;
        bool retryRequired = false;
        bool cancellationRequested = false;
        bool limitExceeded = false;
        bool statusRowsTruncated = false;
    };

    enum class ProjectAssetLooseMaintenanceState : u8
    {
        Idle,
        Requested,
        Running,
        Complete,
        Deferred,
        Failed,
        Cancelled
    };

    enum class ProjectAssetLooseMaintenanceResult : u8
    {
        Success,
        Deferred,
        InvalidState,
        LimitExceeded,
        StreamingBusy,
        IoFailure,
        Cancelled
    };

    struct ProjectAssetLooseMaintenanceStatus
    {
        ProjectAssetLooseMaintenanceState state = ProjectAssetLooseMaintenanceState::Idle;
        ProjectAssetLooseMaintenanceResult lastResult = ProjectAssetLooseMaintenanceResult::Success;
        u32 trackedCandidates = 0;
        u64 trackedCandidateBytes = 0;
        u32 scannedRecords = 0;
        u64 scannedBytes = 0;
        u32 rediscoveredRecords = 0;
        u32 streamerProtectedRecords = 0;
        u32 residentProtectedRecords = 0;
        u32 settlementProtectedRecords = 0;
        u32 removedRecords = 0;
        u64 removedBytes = 0;
        u32 deferredDeletes = 0;
        u32 failedDeletes = 0;
        u32 foreignFiles = 0;
        u32 stageFiles = 0;
        bool retryRequired = false;
        bool cancellationRequested = false;
        bool limitExceeded = false;
        bool deleteBatchLimitReached = false;
        bool statusRowsTruncated = false;
    };

    struct ProjectAssetLooseMaintenanceDiagnostic
    {
        filesystem::AbsolutePath physicalPath;
        streaming::LooseGenerationRetireResult retireResult = streaming::LooseGenerationRetireResult::NotManaged;
        bool rediscovered = false;
    };

    struct ProjectAssetPipelineConfig
    {
        assets::ProjectAssetBuildConfig build;
        ProjectLoosePublisherConfig publication;
        u32 maximumPublishedResources = 262144;
        u32 maximumLooseFiles = 262144;
        filesystem::AbsolutePath operationJournalPath;
        u32 maximumOperationEntries = 4096;
        u64 maximumOperationFileBytes = 256ull * 1024ull * 1024ull;
        u64 maximumOperationStagedBytes = 512ull * 1024ull * 1024ull;
        u32 maximumOperationJournalBytes = 4u * 1024u * 1024u;
        u32 maximumOperationHistoryRecords = 64;
        u64 maximumOperationHistoryBytes = 128ull * 1024ull * 1024ull;
        u32 maximumIdentityGenerationAttempts = 32;
        ProjectAssetChangeStateLimits changeState;
        ProjectAssetAutomaticRecookConfig automaticRecook;
        ProjectAssetCacheMaintenanceConfig cacheMaintenance;

        [[nodiscard]] bool IsValid() const noexcept
        {
            return build.IsValid() && publication.IsValid() && build.artifactCacheRoot == publication.artifactCacheRoot &&
                   build.target == publication.target && build.buildSystem.persistentCacheCoordination == publication.cacheCoordination &&
                   maximumPublishedResources != 0 && maximumLooseFiles != 0 &&
                   (operationJournalPath.Empty() || operationJournalPath.IsFilePath()) && maximumOperationEntries != 0 && maximumOperationFileBytes != 0 &&
                   maximumOperationStagedBytes >= maximumOperationFileBytes && maximumOperationJournalBytes != 0 &&
                   maximumOperationHistoryRecords != 0 && maximumOperationHistoryBytes >= maximumOperationFileBytes &&
                   maximumIdentityGenerationAttempts != 0 && changeState.IsValid() && automaticRecook.IsValid() &&
                   cacheMaintenance.IsValid();
        }
    };

    struct ProjectAssetRestoreReport
    {
        ProjectAssetSnapshotResult acceptedSnapshot = ProjectAssetSnapshotResult::NotChecked;
        bool operationCleanupPending = false;
        u32 catalogOutputs = 0;
        u32 restored = 0;
        u32 alreadyPublished = 0;
        u32 missingBuildRecords = 0;
        u32 unavailableBuildRecords = 0;
        u32 repairRequiredRecords = 0;
        u32 removedPublications = 0;
        u32 staleFilesRemoved = 0;
        u32 staleFilesDeferred = 0;
    };

    struct ProjectAssetCandidateImportRequest
    {
        filesystem::AbsolutePath sourcePath;
        resources::ResourcePath sourceIdentity;
        assets::SourceImporterId importer = assets::InvalidSourceImporterId;
        u32 importerVersion = 0;
        u64 sourceGeneration = 0;
    };

    // Project-owned import boundary for source content that does not yet exist
    // under a managed source root. The original external picker path is not
    // project identity; callers provide one complete stable byte snapshot and
    // a destination path inside a current writable source root.
    struct ProjectAssetExternalImportRequest
    {
        filesystem::AbsolutePath destinationPath;
        containers::ArraySpan<const u8> sourceBytes;
    };

    struct ProjectAssetSettingsUpdateRequest
    {
        assets::AssetId asset;
        u64 acceptedGeneration = 0;
        u32 settingsVersion = 0;
        containers::ArraySpan<const u8> settings;
    };

    // Opt-in atomic delivery cohort. `outputs` must be the complete current
    // accepted output set for `asset` at `acceptedGeneration`.
    struct ProjectAssetCohortDeliveryRequest
    {
        assets::AssetId asset;
        u64 acceptedGeneration = 0;
        containers::ArraySpan<const resources::ResourceReference> outputs;
    };

    enum class ProjectAssetDeferredPlannedImportState : u8
    {
        None,
        // The reviewed transaction is staged but not durable. The pipeline owns
        // all continuation evidence while a Jobs worker executes only
        // AssetFileTransaction::Prepare(). `CommitPlannedImport` returns Pending
        // and leaves importedAsset invalid in this state.
        PreparingTransaction,
        AwaitingReconciliation,
        CohortQueued,
        ContinuationFailed
    };

    enum class ProjectAssetDeferredPlannedImportPreparePhase : u8
    {
        None,
        Writing,
        Verifying
    };

    // Process-local observation of the one owner-driven continuation attached to
    // an explicitly deferred planned import. The durable cohort intent remains the
    // restart authority; this status only reports the current process attempt.
    struct ProjectAssetDeferredPlannedImportStatus
    {
        ProjectAssetDeferredPlannedImportState state = ProjectAssetDeferredPlannedImportState::None;
        assets::AssetId asset;
        u64 acceptedGeneration = 0u;
        ProjectAssetPipelineResult result = ProjectAssetPipelineResult::Success;
        assets::ProjectAssetBuildResult build = assets::ProjectAssetBuildResult::Success;
        resources::ResourceReference buildRequestOutput;
        u32 buildRequestOutputIndex = ~u32{0};
        ProjectAssetDeferredPlannedImportPreparePhase preparePhase = ProjectAssetDeferredPlannedImportPreparePhase::None;
        u64 prepareCompletedBytes = 0u;
        u64 prepareTotalBytes = 0u;
    };

    struct ProjectAssetBuildFreshness
    {
        resources::ResourceReference output;
        assets::DirtyReason reason = assets::DirtyReason::Missing;
    };

    struct ProjectAssetPipelineFailure
    {
        ProjectAssetPipelineResult result = ProjectAssetPipelineResult::Success;
        // One-shot diagnostic for reviewed import rejection. Zero means this
        // failure did not originate from CommitPlannedImport's stale witness.
        // This is evidence only; it never changes admission policy.
        u32 stalePlanSite = 0u;
        ProjectAssetSnapshotResult acceptedSnapshot = ProjectAssetSnapshotResult::NotChecked;
        assets::ProjectAssetBuildResult build = assets::ProjectAssetBuildResult::Success;
        // First failing member of a reviewed explicit cohort. Only populated
        // for a pre-admission resolver failure; never implies durable rollback.
        resources::ResourceReference buildRequestOutput;
        u32 buildRequestOutputIndex = ~u32{0};
        assets::BuildFailure buildFailure = assets::BuildFailure::None;
        assets::Result buildError = assets::Result::Success;
        assets::IndexResult index = assets::IndexResult::Success;
        ProjectLoosePublicationFailure publication;
        assets::SourceDatabaseResult sources = assets::SourceDatabaseResult::Success;
        assets::AssetOperationFailure operation;
        assets::PersistentCacheMaintenanceResult maintenance = assets::PersistentCacheMaintenanceResult::Success;
        ProjectAssetLooseMaintenanceResult looseMaintenance = ProjectAssetLooseMaintenanceResult::Success;
    };

    class ProjectAssetPipeline;

    // One caller-owned interest in the existing BuildGraph. A request must be
    // released before source-catalog mutation or pipeline shutdown.
    class ProjectAssetRequest final
    {
    public:
        ProjectAssetRequest() noexcept = default;
        ProjectAssetRequest(ProjectAssetRequest&& other) noexcept;
        ProjectAssetRequest& operator=(ProjectAssetRequest&& other) noexcept;
        ~ProjectAssetRequest();

        ProjectAssetRequest(const ProjectAssetRequest&) = delete;
        ProjectAssetRequest& operator=(const ProjectAssetRequest&) = delete;

        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] bool HasFinished() const noexcept;
        [[nodiscard]] assets::BuildState GetBuildState() const noexcept;
        [[nodiscard]] resources::ResourceReference GetResource() const noexcept;
        void Wait() const noexcept;
        [[nodiscard]] ProjectAssetPipelineResult Poll(ProjectAssetPipelineFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool Cancel() noexcept;
        void Reset() noexcept;

    private:
        ProjectAssetRequest(ProjectAssetPipeline& owner, resources::ResourceReference resource, assets::GraphRequest&& request) noexcept;

        ProjectAssetPipeline* m_owner = nullptr;
        resources::ResourceReference m_resource;
        assets::GraphRequest m_request;
        bool m_published = false;

        friend class ProjectAssetPipeline;
    };

    // Shared owner-thread project cooker/publication lifecycle. Hosts provide their tool
    // descriptors; editor and headless frontends consume the same owner.
    // It adds no scheduler, DDC, dependency table, or runtime resource registry.
    class ProjectAssetPipeline final
    {
    public:
        struct Impl;

        ProjectAssetPipeline() noexcept = default;
        ~ProjectAssetPipeline();

        ProjectAssetPipeline(const ProjectAssetPipeline&) = delete;
        ProjectAssetPipeline& operator=(const ProjectAssetPipeline&) = delete;

        [[nodiscard]] ProjectAssetPipelineResult Initialize(filesystem::Manager& files, assets::SourceDatabase& sources,
                                                            streaming::ResourceStreamer& streamer,
                                                            containers::ArraySpan<const assets::CompilerDescriptor> compilers,
                                                            containers::ArraySpan<const assets::SourceImporterDescriptor> importers,
                                                            const ProjectAssetPipelineConfig& config,
                                                            ProjectAssetRestoreReport* restore = nullptr,
                                                            ProjectAssetPipelineFailure* failure = nullptr) noexcept;
        [[nodiscard]] ProjectAssetPipelineResult Shutdown(ProjectAssetPipelineFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;

        // Automatic coordination lifecycle. Planning/finalization and publication
        // work run on Jobs; owner-thread ticks perform bounded admission and O(1)
        // completion/settlement handoff.
        [[nodiscard]] ProjectAssetPipelineResult EnableAutomaticRecook(ProjectAssetPipelineFailure* failure = nullptr) noexcept;
        [[nodiscard]] ProjectAssetPipelineResult DisableAutomaticRecook(ProjectAssetPipelineFailure* failure = nullptr) noexcept;
        [[nodiscard]] ProjectAssetPipelineResult RetryAutomaticRecook(ProjectAssetPipelineFailure* failure = nullptr) noexcept;
        [[nodiscard]] ProjectAssetPipelineResult RetryAutomaticPublication(resources::ResourceReference output,
                                                                           ProjectAssetPipelineFailure* failure = nullptr) noexcept;
        // Forces the complete current accepted output set for one asset through
        // the existing automatic coordinator/IncrementalRecooker. No second
        // queue, graph, scheduler or cache-bypass owner is created.
        [[nodiscard]] ProjectAssetPipelineResult RequestAssetReimport(assets::AssetId asset,
                                                                     ProjectAssetPipelineFailure* failure = nullptr) noexcept;
        // Forces one exact accepted asset-generation output set through the same
        // recooker, then publishes every cohort member through one prepared loose
        // visibility batch. Disconnected siblings remain in the same cutover.
        [[nodiscard]] ProjectAssetPipelineResult RequestAssetCohortDelivery(
            const ProjectAssetCohortDeliveryRequest& request,
            ProjectAssetPipelineFailure* failure = nullptr) noexcept;
        [[nodiscard]] ProjectAssetPipelineResult CancelAutomaticRecook(ProjectAssetPipelineFailure* failure = nullptr) noexcept;
        [[nodiscard]] ProjectAssetPipelineResult TickAutomaticRecook(ProjectAssetPipelineFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool IsAutomaticRecookEnabled() const noexcept;
        [[nodiscard]] ProjectAssetAutomaticRecookStatus GetAutomaticRecookStatus() const noexcept;
        [[nodiscard]] bool GetOutputDeliveryStatus(resources::ResourceReference output,
                                                   ProjectAssetOutputDeliveryStatus& status) const noexcept;
        [[nodiscard]] bool GetOutputPublicationStatus(resources::ResourceReference output,
                                                      ProjectAssetOutputPublicationStatus& status) const noexcept;
        // Bounded immutable E2.4 delivery/retry evidence used by later cache
        // maintenance admission. No delivery arrays or mutable indices escape.
        [[nodiscard]] bool CaptureVddcProtectionKeys(containers::DynamicArray<assets::ArtifactSetKey>& keys,
                                                     u32 maximumRecords, u64 maximumBytes) const noexcept;
        [[nodiscard]] bool ReportResidentDelivery(const ProjectAssetResidentDeliveryEvidence& evidence) noexcept;
        [[nodiscard]] bool ClearResidentDelivery(resources::ResourceReference output) noexcept;
        // A composition owner may acknowledge renderer-side retirement evidence only
        // for Accepted or NotTracked. Busy is retryable and must leave the renderer
        // evidence unacknowledged; invalid argument/state results are lifecycle or
        // caller contract failures and are never acknowledgement-safe.
        [[nodiscard]] ProjectAssetResidentRetirementResult ReportResidentRetirement(
            const ProjectAssetResidentRetirementEvidence& evidence) noexcept;
        // E2.5 P3 VDDC maintenance. Request captures bounded immutable project
        // protection, then dispatches project-sized scan/metadata/delete work to
        // Jobs. Ticks only adopt completion; terminal failure/defer is sticky
        // until an explicit retry/new request.
        [[nodiscard]] ProjectAssetPipelineResult RequestVddcMaintenance(ProjectAssetPipelineFailure* failure = nullptr) noexcept;
        [[nodiscard]] ProjectAssetPipelineResult RetryVddcMaintenance(ProjectAssetPipelineFailure* failure = nullptr) noexcept;
        [[nodiscard]] ProjectAssetPipelineResult CancelVddcMaintenance(ProjectAssetPipelineFailure* failure = nullptr) noexcept;
        [[nodiscard]] ProjectAssetPipelineResult TickVddcMaintenance(ProjectAssetPipelineFailure* failure = nullptr) noexcept;
        [[nodiscard]] ProjectAssetVddcMaintenanceStatus GetVddcMaintenanceStatus() const noexcept;
        [[nodiscard]] containers::ArraySpan<const assets::PersistentCacheMaintenanceDiagnostic> GetVddcMaintenanceDiagnostics() const noexcept;
        // E2.5 P4 loose-generation maintenance. Superseded candidates are
        // captured before descriptor cutover; this explicit maintenance pass
        // performs bounded restart rediscovery and exact streamer-authority
        // retirement. Terminal defer/failure remains sticky until retry.
        [[nodiscard]] ProjectAssetPipelineResult RequestLooseMaintenance(ProjectAssetPipelineFailure* failure = nullptr) noexcept;
        [[nodiscard]] ProjectAssetPipelineResult RetryLooseMaintenance(ProjectAssetPipelineFailure* failure = nullptr) noexcept;
        [[nodiscard]] ProjectAssetPipelineResult CancelLooseMaintenance(ProjectAssetPipelineFailure* failure = nullptr) noexcept;
        [[nodiscard]] ProjectAssetPipelineResult TickLooseMaintenance(ProjectAssetPipelineFailure* failure = nullptr) noexcept;
        [[nodiscard]] ProjectAssetLooseMaintenanceStatus GetLooseMaintenanceStatus() const noexcept;
        [[nodiscard]] containers::ArraySpan<const ProjectAssetLooseMaintenanceDiagnostic> GetLooseMaintenanceDiagnostics() const noexcept;
        // P2 planning diagnostics. These borrowed spans are invalidated by any
        // automatic lifecycle mutation that admits/retires the current pending
        // set (including TickAutomaticRecook), by the next successful pending-
        // state capture, disable, or shutdown. Callers must not retain them
        // across an automatic owner-thread command/tick.
        // Inputs are typed source/build identities for committed VADI reverse
        // lookup; outputs are cooked-output seeds/dependants. Neither API starts
        // cooking and neither encodes concrete cooker types.
        [[nodiscard]] containers::ArraySpan<const resources::ResourceReference> GetAutomaticRecookPendingInputs() const noexcept;
        [[nodiscard]] containers::ArraySpan<const resources::ResourceReference> GetAutomaticRecookPendingOutputs() const noexcept;

        // Semantically read-only committed-VADI freshness query. The output
        // value remains unchanged on failure. Busy is returned whenever accepted
        // truth is reconciling or BuildGraph/VADI state can cross a commit while
        // the Resolve -> Prepare -> materialize -> Evaluate sequence is running.
        [[nodiscard]] ProjectAssetPipelineResult QueryBuildFreshness(
            resources::ResourceReference output, ProjectAssetBuildFreshness& freshness,
            ProjectAssetPipelineFailure* failure = nullptr) noexcept;

        [[nodiscard]] ProjectAssetRequest Request(resources::ResourceReference output,
                                                  assets::BuildPriority priority = assets::BuildPriority::Normal,
                                                  ProjectAssetPipelineFailure* failure = nullptr) noexcept;

        // Owner-thread reconciliation after a successful source rescan. It
        // never cooks: committed records are restored, removed outputs are
        // unpublished, and existing last-valid publications are retained.
        [[nodiscard]] ProjectAssetPipelineResult Reconcile(ProjectAssetRestoreReport* report = nullptr,
                                                           ProjectAssetPipelineFailure* failure = nullptr) noexcept;
        [[nodiscard]] ProjectAssetPipelineResult Rescan(containers::ArraySpan<const assets::SourceRoot> roots,
                                                        ProjectAssetRestoreReport* report = nullptr,
                                                        filesystem::ScanResult* scanFailure = nullptr,
                                                        ProjectAssetPipelineFailure* failure = nullptr) noexcept;
        // Read-only exact-current authoring capability check used by frontends to
        // avoid presenting Import for Candidates whose live importer cannot
        // author canonical default metadata/settings. This performs no callback
        // invocation and no mutation.
        [[nodiscard]] bool CanImportCandidate(const ProjectAssetCandidateImportRequest& request) const noexcept;
        [[nodiscard]] ProjectAssetPipelineResult ImportCandidate(
            const ProjectAssetCandidateImportRequest& request, assets::AssetId& importedAsset,
            ProjectAssetRestoreReport* report = nullptr, ProjectAssetPipelineFailure* failure = nullptr) noexcept;
        [[nodiscard]] ProjectAssetPipelineResult ImportExternalSource(
            const ProjectAssetExternalImportRequest& request, assets::AssetId& importedAsset,
            ProjectAssetRestoreReport* report = nullptr, ProjectAssetPipelineFailure* failure = nullptr) noexcept;
        // Produces a collision-free parent identity candidate before a format-owned
        // immutable import plan exists. The plan validates its derived output paths;
        // CommitPlannedImport rechecks all identity/output collisions before mutation.
        [[nodiscard]] ProjectAssetPipelineResult GenerateImportIdentityCandidate(
            assets::AssetId& identity, ProjectAssetPipelineFailure* failure = nullptr) noexcept;
        // Upper bound a frontend may read before canonical VMETA size is known.
        // ImportExternalSource still performs the exact source+VMETA staged/history
        // admission before commit.
        [[nodiscard]] u64 GetMaximumExternalImportSourceBytes() const noexcept;
        [[nodiscard]] ProjectAssetPipelineResult UpdateAssetSettings(
            const ProjectAssetSettingsUpdateRequest& request, ProjectAssetRestoreReport* report = nullptr,
            ProjectAssetPipelineFailure* failure = nullptr) noexcept;
        // Stale-safe composite durable commit for a previously reviewed plan.
        // It preserves the existing AssetId on reimport, atomically commits the
        // source/VMETA/output schema plus explicitly selected auxiliary candidate
        // adoptions, records history, then rescans/reconciles through this owner.
        // `deferPostCommitReconciliation` is a narrow opt-in for an independent,
        // create-only first import. The durable filesystem transaction and cohort
        // intent still commit before return; authoritative catalog reconciliation
        // is then completed by the owner-driven observation catch-up.
        [[nodiscard]] ProjectAssetPipelineResult CommitPlannedImport(
            const assets::PlannedAssetImportRequest& request, assets::AssetId& importedAsset,
            ProjectAssetRestoreReport* report = nullptr, ProjectAssetPipelineFailure* failure = nullptr,
            bool deferPostCommitReconciliation = false) noexcept;
        // Cheap owner-thread readiness probe for a reviewed plan. The commit itself
        // remains authoritative and must repeat every gate and stale witness proof.
        // A plan replacing its own persisted cohort may use the same exception as
        // CommitPlannedImport; unrelated source mutations remain fenced.
        [[nodiscard]] bool CanCommitPlannedImportNow(assets::AssetId parentAsset,
                                                     resources::ResourcePath sourceIdentity) const noexcept;
        // Owner-frame settlement for the pre-durable async Prepare path. This is
        // intentionally separate from TickObservation because observation is
        // stopped while a large independent first import is being prepared.
        // Success means no pre-durable continuation is pending; Pending means the
        // worker still owns Prepare; CommittedReconciliationPending means the
        // owner has durably committed and handed the operation to the existing
        // observation/deferred-cohort continuation.
        [[nodiscard]] ProjectAssetPipelineResult TickDeferredPlannedImport(
            ProjectAssetPipelineFailure* failure = nullptr) noexcept;
        // EngineHost quiesce barrier. Cancel/join any pre-durable Prepare worker
        // and roll back its unapplied transaction BEFORE Jobs service drain.
        // Pipeline Shutdown calls this as a fallback for direct non-host users.
        [[nodiscard]] ProjectAssetPipelineResult QuiesceDeferredPlannedImport(
            ProjectAssetPipelineFailure* failure = nullptr) noexcept;
        [[nodiscard]] ProjectAssetDeferredPlannedImportStatus GetDeferredPlannedImportStatus() const noexcept;
        [[nodiscard]] ProjectAssetPipelineResult MoveAsset(assets::AssetId asset, const filesystem::AbsolutePath& target,
                                                           ProjectAssetRestoreReport* report = nullptr,
                                                           ProjectAssetPipelineFailure* failure = nullptr) noexcept;
        [[nodiscard]] ProjectAssetPipelineResult DuplicateAsset(assets::AssetId asset, const filesystem::AbsolutePath& target,
                                                                assets::AssetId newIdentity,
                                                                ProjectAssetRestoreReport* report = nullptr,
                                                                ProjectAssetPipelineFailure* failure = nullptr) noexcept;
        // Canonical editor/headless duplicate command. The shared owner draws a
        // UUID-v4 identity and rejects catalog collisions before mutation.
        [[nodiscard]] ProjectAssetPipelineResult DuplicateAssetWithGeneratedIdentity(
            assets::AssetId asset, const filesystem::AbsolutePath& target, assets::AssetId& newIdentity,
            ProjectAssetRestoreReport* report = nullptr, ProjectAssetPipelineFailure* failure = nullptr) noexcept;
        [[nodiscard]] ProjectAssetPipelineResult RetargetAssetReferences(
            containers::ArraySpan<const assets::AssetReferenceReplacement> replacements,
            ProjectAssetRestoreReport* report = nullptr, ProjectAssetPipelineFailure* failure = nullptr) noexcept;
        [[nodiscard]] ProjectAssetPipelineResult DeleteAsset(
            assets::AssetId asset, containers::ArraySpan<const assets::AssetReferenceReplacement> replacements = {},
            ProjectAssetRestoreReport* report = nullptr, ProjectAssetPipelineFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool CanMutateSources() const noexcept;
        [[nodiscard]] bool CanStartAssetOperation() const noexcept;
        [[nodiscard]] bool CanUndoAssetOperation() const noexcept;
        [[nodiscard]] bool CanRedoAssetOperation() const noexcept;
        [[nodiscard]] u32 GetUndoAssetOperationCount() const noexcept;
        [[nodiscard]] u32 GetRedoAssetOperationCount() const noexcept;
        [[nodiscard]] ProjectAssetPipelineResult UndoAssetOperation(ProjectAssetRestoreReport* report = nullptr,
                                                                    ProjectAssetPipelineFailure* failure = nullptr) noexcept;
        [[nodiscard]] ProjectAssetPipelineResult RedoAssetOperation(ProjectAssetRestoreReport* report = nullptr,
                                                                    ProjectAssetPipelineFailure* failure = nullptr) noexcept;
        // Frontends call this after an external project transition. History is
        // session-local and deliberately is not replayed across restart.
        void ClearAssetOperationHistory() noexcept;

        // E2.1 external-change observation. Watched roots come from the current
        // source catalog. Callbacks only publish coalesced path/overflow hints;
        // TickObservation advances bounded owner bookkeeping while the persistent
        // observation worker performs authoritative scans, materialization and
        // other project-sized reconciliation work. Enabling starts one
        // authoritative catch-up pass. The platform overload owns the OS observer;
        // the injected overload borrows a caller-owned source for hosts/tests.
        [[nodiscard]] ProjectAssetPipelineResult EnableObservation(const ProjectAssetObservationConfig& config = {},
                                                                   ProjectAssetPipelineFailure* failure = nullptr) noexcept;
        [[nodiscard]] ProjectAssetPipelineResult EnableObservation(filesystem::DirectoryObservationSource& source,
                                                                   const ProjectAssetObservationConfig& config = {},
                                                                   ProjectAssetPipelineFailure* failure = nullptr) noexcept;
        [[nodiscard]] ProjectAssetPipelineResult DisableObservation(ProjectAssetPipelineFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool IsObservingSources() const noexcept;
        [[nodiscard]] ProjectAssetPipelineResult TickObservation(ProjectAssetRestoreReport* report = nullptr,
                                                                 ProjectAssetPipelineFailure* failure = nullptr) noexcept;
        [[nodiscard]] ProjectAssetObservationStatus GetObservationStatus() const noexcept;

        // E2.2 accepted-state queries. Returned pointers/spans borrow immutable
        // pipeline-owned state and are invalidated by the next successful E2.2
        // acceptance or shutdown. P1 defines the contract/storage only; later
        // E2.2 phases populate it transactionally.
        [[nodiscard]] u64 GetAcceptedAssetGeneration() const noexcept;
        [[nodiscard]] containers::ArraySpan<const ProjectAssetState> GetAcceptedAssets() const noexcept;
        [[nodiscard]] const ProjectAssetState* LookupAcceptedAsset(assets::AssetId asset) const noexcept;
        [[nodiscard]] containers::ArraySpan<const ProjectAssetChangeRecord> GetAcceptedAssetChanges() const noexcept;
        [[nodiscard]] containers::ArraySpan<const ProjectAssetSourceDiagnostic> GetAssetSourceDiagnostics() const noexcept;
        // Source/build inputs and cooked outputs are intentionally distinct.
        // GetAssetInvalidationOutputs() is diagnostic/publication evidence and
        // is not directly suitable as IncrementalRecooker::AssetChange input.
        [[nodiscard]] containers::ArraySpan<const resources::ResourceReference> GetAssetInvalidationSeeds() const noexcept;
        [[nodiscard]] containers::ArraySpan<const resources::ResourceReference> GetAssetInvalidationOutputs() const noexcept;
        [[nodiscard]] containers::ArraySpan<const assets::AssetReference> GetMissingOutputReferencers(
            resources::ResourceReference output) const noexcept;

    private:
        [[nodiscard]] ProjectAssetPipelineResult Poll(ProjectAssetRequest& request, ProjectAssetPipelineFailure* failure) noexcept;
        [[nodiscard]] ProjectAssetPipelineResult ReplayAssetOperation(bool undo, ProjectAssetRestoreReport* report,
                                                                      ProjectAssetPipelineFailure* failure) noexcept;
        Impl* m_impl = nullptr;

        friend class ProjectAssetRequest;
    };
} // namespace vanguard::asset_streaming
