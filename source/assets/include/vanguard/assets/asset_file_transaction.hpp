#pragma once

#include <vanguard/crypto/crypto.hpp>
#include <vanguard/filesystem/filesystem.hpp>

namespace vanguard::assets
{
    enum class AssetFileTransactionResult : u8
    {
        Success,
        InvalidState,
        InvalidArgument,
        LimitExceeded,
        Conflict,
        IoFailure,
        InvalidJournal,
        RecoveryRequired,
        CommittedCleanupPending,
        Cancelled
    };

    enum class AssetFileTransactionState : u8
    {
        Closed,
        Ready,
        Collecting,
        Prepared,
        Committed,
        RecoveryRequired
    };

    // Durable-transaction-owned sibling suffixes. External observation must
    // never treat these intermediate commit files as authored source assets.
    inline constexpr const char* AssetTransactionStageSuffix = ".vtxn-stage";
    inline constexpr const char* AssetTransactionBackupSuffix = ".vtxn-backup";

    struct AssetFileTransactionConfig
    {
        filesystem::AbsolutePath journalPath;
        containers::ArraySpan<const filesystem::AbsolutePath> writableRoots;
        // Paths authorized only for read-only transaction assertions. This may
        // include writableRoots, but never expands write/move/delete authority.
        containers::ArraySpan<const filesystem::AbsolutePath> assertionRoots;
        u32 maximumEntries = 4096;
        u64 maximumFileBytes = 256ull * 1024ull * 1024ull;
        u64 maximumStagedBytes = 512ull * 1024ull * 1024ull;
        u32 maximumJournalBytes = 4u * 1024u * 1024u;

        [[nodiscard]] bool IsValid() const noexcept;
    };

    struct AssetFileRecoveryReport
    {
        bool journalFound = false;
        bool rolledBack = false;
        bool completedCleanup = false;
    };

    // Optional commit receipt used by higher-level undo/redo owners. The
    // condition describes the exact state authored by the transaction, not a
    // later filesystem observation, so an external writer racing after commit
    // can be detected before history replay mutates the path again.
    struct AssetFileTransactionPostCondition
    {
        crypto::Digest256 contentDigest;
        bool exists = false;
        bool known = false;
    };

    enum class AssetFileTransactionPreparePhase : u8
    {
        Writing,
        Verifying
    };

    struct AssetFileTransactionPrepareProgress
    {
        AssetFileTransactionPreparePhase phase = AssetFileTransactionPreparePhase::Writing;
        u64 completedBytes = 0u;
        u64 totalBytes = 0u;
    };

    using AssetFileTransactionPrepareCancellationFunction = bool (*)(void* userData) noexcept;
    using AssetFileTransactionPrepareProgressFunction = void (*)(const AssetFileTransactionPrepareProgress& progress,
                                                                 void* userData) noexcept;

    struct AssetFileTransactionPrepareObserver
    {
        AssetFileTransactionPrepareCancellationFunction isCancellationRequested = nullptr;
        AssetFileTransactionPrepareProgressFunction reportProgress = nullptr;
        void* userData = nullptr;
    };

    // Single-owner durable transaction for explicitly authorized source roots.
    // Initialize performs journal recovery. A committed cleanup result means
    // the logical commit succeeded and recovery must finish owned-file cleanup.
    // No catalog, build graph, streamer or editor state is retained here.
    class AssetFileTransaction final
    {
    public:
        struct Impl;

        AssetFileTransaction() noexcept = default;
        ~AssetFileTransaction();
        AssetFileTransaction(const AssetFileTransaction&) = delete;
        AssetFileTransaction& operator=(const AssetFileTransaction&) = delete;

        [[nodiscard]] AssetFileTransactionResult Initialize(filesystem::Manager& files, const AssetFileTransactionConfig& config,
                                                            AssetFileRecoveryReport* recovery = nullptr) noexcept;
        [[nodiscard]] AssetFileTransactionResult Shutdown() noexcept;
        [[nodiscard]] AssetFileTransactionState GetState() const noexcept;

        [[nodiscard]] AssetFileTransactionResult Begin() noexcept;
        [[nodiscard]] AssetFileTransactionResult StageWrite(const filesystem::AbsolutePath& target,
                                                            containers::ArraySpan<const u8> bytes) noexcept;
        [[nodiscard]] AssetFileTransactionResult StageCreate(const filesystem::AbsolutePath& target,
                                                             containers::ArraySpan<const u8> bytes) noexcept;
        [[nodiscard]] AssetFileTransactionResult StageWriteIfUnchanged(
            const filesystem::AbsolutePath& target, containers::ArraySpan<const u8> expectedBytes,
            containers::ArraySpan<const u8> replacementBytes) noexcept;
        [[nodiscard]] AssetFileTransactionResult StageWriteIfDigestMatches(
            const filesystem::AbsolutePath& target, const crypto::Digest256& expectedDigest,
            containers::ArraySpan<const u8> replacementBytes) noexcept;
        // Read-only commit precondition. The path is never rewritten; Prepare/Commit
        // only succeeds if the file still exists with the exact expected digest.
        // This participates in the same atomic transaction boundary as mutations.
        [[nodiscard]] AssetFileTransactionResult StageAssertDigestMatches(
            const filesystem::AbsolutePath& target, const crypto::Digest256& expectedDigest) noexcept;
        [[nodiscard]] AssetFileTransactionResult StageMove(const filesystem::AbsolutePath& source,
                                                           const filesystem::AbsolutePath& target) noexcept;
        [[nodiscard]] AssetFileTransactionResult StageDelete(const filesystem::AbsolutePath& target) noexcept;
        [[nodiscard]] AssetFileTransactionResult StageDeleteIfDigestMatches(
            const filesystem::AbsolutePath& target, const crypto::Digest256& expectedDigest) noexcept;
        [[nodiscard]] AssetFileTransactionResult Prepare() noexcept;
        // The transaction remains single-owner while Prepare runs. The observer
        // is invoked synchronously on that calling thread and must not re-enter
        // this transaction. Cancellation before the durable Prepared record
        // cleans transaction-owned staging and leaves Collecting retryable.
        [[nodiscard]] AssetFileTransactionResult Prepare(const AssetFileTransactionPrepareObserver& observer) noexcept;
        // Publishes a durable Applying recovery record before the first authored
        // filesystem mutation, applies the prepared mutations, and performs the
        // final read-only assertions. Applying remains rollback-capable: only
        // PublishCommit() is the durable commit linearization point.
        [[nodiscard]] AssetFileTransactionResult ApplyPrepared() noexcept;
        // Short durability linearization step. Call only after ApplyPrepared().
        // Publication failure leaves the transaction Prepared/applied so the
        // caller can Rollback() after leaving any higher-level admission lock.
        [[nodiscard]] AssetFileTransactionResult PublishCommit() noexcept;
        // Computes postconditions and cleans transaction-owned files after the
        // committed journal is already durable.
        [[nodiscard]] AssetFileTransactionResult CompleteCommit(
            containers::ArraySpan<const filesystem::AbsolutePath> trackedPaths = {},
            containers::ArraySpan<AssetFileTransactionPostCondition> postConditions = {}) noexcept;
        [[nodiscard]] AssetFileTransactionResult Commit(
            containers::ArraySpan<const filesystem::AbsolutePath> trackedPaths = {},
            containers::ArraySpan<AssetFileTransactionPostCondition> postConditions = {}) noexcept;
        [[nodiscard]] AssetFileTransactionResult Rollback() noexcept;

    private:
        Impl* m_impl = nullptr;
    };
} // namespace vanguard::assets
