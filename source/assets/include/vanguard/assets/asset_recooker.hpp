#pragma once

#include <vanguard/assets/asset_graph.hpp>
#include <vanguard/assets/asset_index.hpp>

namespace vanguard::assets
{
    class IncrementalRecooker;
    struct RecookOperation;

    enum class RecookState : u8
    {
        Building,
        Succeeded,
        Failed,
        Cancelled
    };

    enum class RecookFailure : u8
    {
        None,
        InvalidChange,
        UntrackedChange,
        RequestResolutionFailed,
        DependencyPreparationFailed,
        IndexQueryFailed,
        TransactionFailed,
        PreCommitRejected,
        PreCommitValidationFailed,
        BuildFailed,
        Cancelled,
        OutOfMemory,
        LimitExceeded
    };

    [[nodiscard]] const char* ToString(RecookFailure failure) noexcept;
    [[nodiscard]] const char* ToString(BuildFailure failure) noexcept;

    struct AssetChange
    {
        resources::ResourceReference identity;
    };

    struct RecookConfig
    {
        u32 maximumChangesPerBatch = 4096;
        u32 maximumAffectedOutputsPerBatch = 65536;
        u32 maximumRootRequestsPerBatch = 4096;
        u32 maximumValidationRecordsPerBatch = 65536;
        u64 maximumValidationEvidenceBytes = 256ull * 1024ull * 1024ull;
        // Opt-in authority witness for historical indexed outputs invalidated
        // by a removed source. Called only after current root resolution fails;
        // never applies to an explicitly requested current/forced output.
        bool (*isConfirmedHistoricalOutput)(resources::ResourceReference output, void* userData) noexcept = nullptr;
        void* historicalOutputUserData = nullptr;
    };

    struct RecookStats
    {
        u32 inputChanges = 0;
        u32 unchangedChanges = 0;
        u32 explicitOutputs = 0;
        u32 unchangedOutputs = 0;
        u32 firstCookOutputs = 0;
        u32 dirtySeeds = 0;
        u32 affectedOutputs = 0;
        u32 rootRequests = 0;
        u32 succeededRoots = 0;
        u32 failedRoots = 0;
        // Complete BuildGraph operation accounting for the active batch,
        // including generated dependencies below the externally requested
        // roots. This is presentation/telemetry evidence only; it does not
        // participate in scheduling or correctness decisions.
        u32 buildOperations = 0;
        u32 completedBuildOperations = 0;
        u64 completedBatches = 0;
        u64 failedBatches = 0;
        u64 cancelledBatches = 0;
    };

    // Captured only for a terminal failed graph root; no borrowed graph/report
    // memory crosses the worker/owner boundary. The bounded message is optional.
    struct RecookRootFailureDetail
    {
        resources::ResourceReference output;
        BuildFailure graphFailure = BuildFailure::None;
        Result buildResult = Result::Success;
        u32 rootIndex = ~u32{0};
        char message[192]{};
    };

    // Resolves a cooked output identity to ownership-safe current source bytes,
    // metadata, settings, and target.
    using ResolveIndexedBuildRequestFunction = bool (*)(resources::ResourceReference output, OwnedBuildRequest& request, void* userData) noexcept;

    enum class RecookPreCommitValidationResult : u8
    {
        Accept,
        Reject,
        Failed
    };

    // For a transactional batch, runs after every owned graph root succeeds and
    // after the recooker has captured an owned snapshot of the staged VADI
    // records, but before DependencyIndex::CommitTransaction. A no-work batch
    // also invokes the validator before reporting success, with an empty record
    // span and no active transaction, so callers can fence freshness decisions
    // that produced no dirty roots. Reject/Failed rolls back an active
    // transaction when present and otherwise simply rejects that no-work
    // completion. Omitting the callback preserves the legacy commit path.
    using ValidateRecookPreCommitFunction = RecookPreCommitValidationResult (*)(
        containers::ArraySpan<const DependencyRecord> records, void* userData) noexcept;
    // Model/schema refresh also validates exact old output retirements in the
    // same no-mutation pre-commit window as replacement build records.
    using ValidateRecookRetirementFunction = RecookPreCommitValidationResult (*)(
        containers::ArraySpan<const DependencyRecord> records,
        containers::ArraySpan<const resources::ResourceReference> retirements, void* userData) noexcept;

    struct RecookPreCommitValidator
    {
        ValidateRecookPreCommitFunction validate = nullptr;
        void* userData = nullptr;
        ValidateRecookRetirementFunction validateRetirements = nullptr;
    };

    class RecookBatch final
    {
    public:
        RecookBatch() noexcept = default;
        RecookBatch(RecookBatch&& other) noexcept;
        RecookBatch& operator=(RecookBatch&& other) noexcept;
        ~RecookBatch();

        RecookBatch(const RecookBatch&) = delete;
        RecookBatch& operator=(const RecookBatch&) = delete;

        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] explicit operator bool() const noexcept;
        [[nodiscard]] RecookState GetStatus() const noexcept;
        [[nodiscard]] RecookFailure GetError() const noexcept;
        [[nodiscard]] bool HasFinished() const noexcept;
        [[nodiscard]] bool Poll() noexcept;
        void Wait() noexcept;
        [[nodiscard]] bool Cancel() noexcept;
        [[nodiscard]] RecookStats GetStats() const noexcept;
        [[nodiscard]] RecookRootFailureDetail GetRootFailureDetail() const noexcept;
        // Moves the exact bounded transaction-record snapshot that passed the
        // pre-commit validator and then committed successfully. Records are
        // available only for a successful transactional batch that used a
        // validator, are returned at most once, and are never produced for a
        // no-work completion or any failed/cancelled/rejected transaction.
        // `records` must be empty so the handoff is O(1) and does not retire an
        // unrelated payload on the caller thread.
        [[nodiscard]] bool TakeCommittedRecords(containers::DynamicArray<DependencyRecord>& records) noexcept;
        // Exact retired identities become observable only on successful
        // durable transaction commit; returned at most once by O(1) handoff.
        [[nodiscard]] bool TakeCommittedRetirements(
            containers::DynamicArray<resources::ResourceReference>& retirements) noexcept;
        // Carries the exact pre-retirement indexed build/content/artifact
        // witnesses for a future durable last-known-good handoff. These are
        // captured before removal and only exposed after successful commit.
        [[nodiscard]] bool TakeCommittedRetirementRecords(
            containers::DynamicArray<DependencyRecord>& records) noexcept;
        void Reset() noexcept;

    private:
        RecookBatch(IncrementalRecooker* owner, RecookOperation* operation) noexcept;

        IncrementalRecooker* m_owner = nullptr;
        RecookOperation* m_operation = nullptr;

        friend class IncrementalRecooker;
    };

    class IncrementalRecooker final
    {
    public:
        struct Impl;

        IncrementalRecooker() noexcept = default;
        ~IncrementalRecooker();

        IncrementalRecooker(const IncrementalRecooker&) = delete;
        IncrementalRecooker& operator=(const IncrementalRecooker&) = delete;

        [[nodiscard]] bool Initialize(BuildSystem& buildSystem, BuildGraph& graph, DependencyIndex& index, ResolveIndexedBuildRequestFunction resolver,
                                      void* resolverUserData = nullptr, const RecookConfig& config = {}) noexcept;
        [[nodiscard]] bool Shutdown() noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;

        [[nodiscard]] RecookBatch Request(containers::ArraySpan<const AssetChange> changes, BuildPriority priority = BuildPriority::Normal) noexcept;
        // Combined incremental/first-cook entry point. Explicit cooked outputs
        // are evaluated in the same planning pass and VADI transaction as
        // indexed input changes. Untracked inputs are tolerated here because a
        // corresponding explicit output may be a legitimate first cook; the
        // legacy changes-only overload keeps its explicit UntrackedChange
        // contract.
        [[nodiscard]] RecookBatch Request(containers::ArraySpan<const AssetChange> changes,
                                          containers::ArraySpan<const resources::ResourceReference> explicitOutputs,
                                          BuildPriority priority = BuildPriority::Normal,
                                          const RecookPreCommitValidator& validator = {}) noexcept;
        // `forcedOutputs` must be a subset of `explicitOutputs`. Those current
        // outputs are submitted through the normal graph/VADI transaction even
        // when VADI build evidence itself is UpToDate. This is used for exact
        // durable-record repair without introducing another scheduler.
        [[nodiscard]] RecookBatch Request(containers::ArraySpan<const AssetChange> changes,
                                          containers::ArraySpan<const resources::ResourceReference> explicitOutputs,
                                          containers::ArraySpan<const resources::ResourceReference> forcedOutputs,
                                          BuildPriority priority = BuildPriority::Normal,
                                          const RecookPreCommitValidator& validator = {}) noexcept;
        // Schema-changing refresh: stage retired old identities alongside
        // current build records. A retirement-aware precommit validator is
        // required. This is not a publication/cohort visibility operation.
        [[nodiscard]] RecookBatch Request(containers::ArraySpan<const AssetChange> changes,
                                          containers::ArraySpan<const resources::ResourceReference> explicitOutputs,
                                          containers::ArraySpan<const resources::ResourceReference> forcedOutputs,
                                          containers::ArraySpan<const resources::ResourceReference> retiredOutputs,
                                          containers::ArraySpan<const resources::ResourceReference> retirementSuccessors,
                                          BuildPriority priority,
                                          const RecookPreCommitValidator& validator) noexcept;
        [[nodiscard]] RecookStats GetStats() const noexcept;

    private:
        [[nodiscard]] RecookBatch RequestInternal(containers::ArraySpan<const AssetChange> changes,
                                                  containers::ArraySpan<const resources::ResourceReference> explicitOutputs,
                                                  containers::ArraySpan<const resources::ResourceReference> forcedOutputs,
                                                  containers::ArraySpan<const resources::ResourceReference> retiredOutputs,
                                                  containers::ArraySpan<const resources::ResourceReference> retirementSuccessors,
                                                  bool rejectUntrackedChanges, BuildPriority priority,
                                                  const RecookPreCommitValidator& validator) noexcept;
        [[nodiscard]] bool Poll(RecookOperation& operation, bool wait) noexcept;
        [[nodiscard]] bool Cancel(RecookOperation& operation) noexcept;
        void Release(RecookOperation& operation) noexcept;

        Impl* m_impl = nullptr;

        friend class RecookBatch;
    };
} // namespace vanguard::assets
