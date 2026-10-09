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
        CommittedCleanupPending
    };

    enum class AssetFileTransactionState : u8
    {
        Closed,
        Ready,
        Collecting,
        Prepared,
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
        [[nodiscard]] AssetFileTransactionResult StageMove(const filesystem::AbsolutePath& source,
                                                           const filesystem::AbsolutePath& target) noexcept;
        [[nodiscard]] AssetFileTransactionResult StageDelete(const filesystem::AbsolutePath& target) noexcept;
        [[nodiscard]] AssetFileTransactionResult StageDeleteIfDigestMatches(
            const filesystem::AbsolutePath& target, const crypto::Digest256& expectedDigest) noexcept;
        [[nodiscard]] AssetFileTransactionResult Prepare() noexcept;
        [[nodiscard]] AssetFileTransactionResult Commit(
            containers::ArraySpan<const filesystem::AbsolutePath> trackedPaths = {},
            containers::ArraySpan<AssetFileTransactionPostCondition> postConditions = {}) noexcept;
        [[nodiscard]] AssetFileTransactionResult Rollback() noexcept;

    private:
        Impl* m_impl = nullptr;
    };
} // namespace vanguard::assets
