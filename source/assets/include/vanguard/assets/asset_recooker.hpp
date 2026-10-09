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
        u64 completedBatches = 0;
        u64 failedBatches = 0;
        u64 cancelledBatches = 0;
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

    struct RecookPreCommitValidator
    {
        ValidateRecookPreCommitFunction validate = nullptr;
        void* userData = nullptr;
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
        // Moves the exact bounded transaction-record snapshot that passed the
        // pre-commit validator and then committed successfully. Records are
        // available only for a successful transactional batch that used a
        // validator, are returned at most once, and are never produced for a
        // no-work completion or any failed/cancelled/rejected transaction.
        // `records` must be empty so the handoff is O(1) and does not retire an
        // unrelated payload on the caller thread.
        [[nodiscard]] bool TakeCommittedRecords(containers::DynamicArray<DependencyRecord>& records) noexcept;
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
        [[nodiscard]] RecookStats GetStats() const noexcept;

    private:
        [[nodiscard]] RecookBatch RequestInternal(containers::ArraySpan<const AssetChange> changes,
                                                  containers::ArraySpan<const resources::ResourceReference> explicitOutputs,
                                                  containers::ArraySpan<const resources::ResourceReference> forcedOutputs,
                                                  bool rejectUntrackedChanges, BuildPriority priority,
                                                  const RecookPreCommitValidator& validator) noexcept;
        [[nodiscard]] bool Poll(RecookOperation& operation, bool wait) noexcept;
        [[nodiscard]] bool Cancel(RecookOperation& operation) noexcept;
        void Release(RecookOperation& operation) noexcept;

        Impl* m_impl = nullptr;

        friend class RecookBatch;
    };
} // namespace vanguard::assets
