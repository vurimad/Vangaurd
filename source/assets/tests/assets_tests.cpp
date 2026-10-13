#include "../../../tests/support/scratch_directory.hpp"
#include <vanguard/assets/assets.hpp>
#include <vanguard/assets/asset_file_transaction.hpp>

#include <vanguard/containers/containers.hpp>
#include <vanguard/diagnostics/diagnostics.hpp>
#include <vanguard/filesystem/filesystem.hpp>
#include <vanguard/io/io.hpp>
#include <vanguard/memory/memory.hpp>

#include <cstring>

namespace
{
    using namespace vanguard;

    constexpr resources::ResourceTypeId SourceType = 0x534f5552u;
    constexpr resources::ResourceTypeId OutputType = 0x434f4f4bu;
    constexpr resources::ResourceTypeId IncludeType = 0x494e434cu;
    constexpr resources::ResourceTypeId AuxiliaryType = 0x41555849u;

    enum class CompilerMode : u8
    {
        Normal,
        DuplicateDependency,
        MissingPrimary,
        WriterLimitFailure
    };

    struct CompilerState
    {
        resources::ResourceReference firstDependency;
        resources::ResourceReference secondDependency;
        resources::ResourceReference auxiliaryOutput;
        assets::BuildFingerprint firstContent;
        assets::BuildFingerprint secondContent;
        u32 discoverCalls = 0;
        u32 compileCalls = 0;
        bool reverseDependencies = false;
        CompilerMode mode = CompilerMode::Normal;
    };

    int g_failures = 0;

    void Check(const bool condition, const char* const message) noexcept
    {
        if (!condition)
        {
            VG_LOG_ERROR(diagnostics::Category::FunctionalTests, "[assetsTests] FAILED: %s", message);
            ++g_failures;
        }
    }

    [[nodiscard]] filesystem::AbsolutePath SuffixedPath(const filesystem::AbsolutePath& path, const char* const suffix) noexcept
    {
        const containers::StringView view = path.AsStringView();
        containers::String value(view.Data(), view.Length());
        value.Append(suffix, static_cast<u32>(std::strlen(suffix)));
        return filesystem::AbsolutePath::CreateFilePath(value);
    }

    [[nodiscard]] bool WriteBytes(filesystem::Manager& files, const filesystem::AbsolutePath& path,
                                  const containers::ArraySpan<const u8> bytes) noexcept
    {
        auto writer = files.CreateFileWriter(path);
        if (!writer)
            return false;
        if (!bytes.Empty())
            writer->Serialize(const_cast<u8*>(bytes.Data()), bytes.Count());
        writer->Flush();
        return !writer->HasErrors();
    }

    [[nodiscard]] bool FileMatches(filesystem::Manager& files, const filesystem::AbsolutePath& path,
                                   const containers::ArraySpan<const u8> expected) noexcept
    {
        auto reader = files.CreateFileReader(path, filesystem::FOF_Buffered);
        if (!reader || reader->GetSize() != expected.Count())
            return false;
        containers::DynamicArray<u8> bytes(memory::pools::Assets::GetInstance());
        bytes.Resize(expected.Count());
        if (bytes.Size() != expected.Count())
            return false;
        if (!expected.Empty())
            reader->Serialize(bytes.TypedData(), bytes.Size());
        return !reader->HasErrors() &&
               (expected.Empty() || std::memcmp(bytes.TypedData(), expected.Data(), expected.Count()) == 0);
    }

    [[nodiscard]] bool DeleteDirectoryTree(filesystem::Manager& files, const filesystem::AbsolutePath& directory) noexcept
    {
        bool removed = true;
        containers::DynamicArray<filesystem::AbsolutePath> children(memory::pools::Assets::GetInstance());
        files.FindDirectories(directory, children);
        for (const filesystem::AbsolutePath& child : children)
            removed = DeleteDirectoryTree(files, child) && removed;
        containers::DynamicArray<filesystem::AbsolutePath> storedFiles(memory::pools::Assets::GetInstance());
        files.FindFiles(directory, containers::String("*"), storedFiles, false);
        for (const filesystem::AbsolutePath& storedFile : storedFiles)
            removed = files.DeleteFile(storedFile) && removed;
        return files.DeletePath(directory) && removed;
    }

    [[nodiscard]] assets::AssetFileTransactionResult InitializeTransaction(
        assets::AssetFileTransaction& transaction, filesystem::Manager& files, const filesystem::AbsolutePath& root,
        const filesystem::AbsolutePath& journal, assets::AssetFileRecoveryReport* const recovery = nullptr) noexcept
    {
        const filesystem::AbsolutePath roots[] = {root};
        assets::AssetFileTransactionConfig config;
        config.journalPath = journal;
        config.writableRoots = {roots, 1u};
        config.assertionRoots = {roots, 1u};
        return transaction.Initialize(files, config, recovery);
    }

    struct PrepareObserverState final
    {
        u64 lastCompletedBytes = 0u;
        u64 totalBytes = 0u;
        u32 progressCalls = 0u;
        bool cancelRequested = false;
        bool cancelOnWriteProgress = false;
        bool cancelOnVerifyProgress = false;
        bool monotonic = true;
        bool sawWriting = false;
        bool sawVerifying = false;

        void ResetProgress() noexcept
        {
            lastCompletedBytes = 0u;
            totalBytes = 0u;
            progressCalls = 0u;
            cancelRequested = false;
            cancelOnWriteProgress = false;
            cancelOnVerifyProgress = false;
            monotonic = true;
            sawWriting = false;
            sawVerifying = false;
        }
    };

    [[nodiscard]] bool IsPrepareCancelled(void* const userData) noexcept
    {
        return static_cast<const PrepareObserverState*>(userData)->cancelRequested;
    }

    void RecordPrepareProgress(const assets::AssetFileTransactionPrepareProgress& progress, void* const userData) noexcept
    {
        auto& state = *static_cast<PrepareObserverState*>(userData);
        state.monotonic = state.monotonic && progress.completedBytes >= state.lastCompletedBytes &&
                          progress.completedBytes <= progress.totalBytes &&
                          (state.progressCalls == 0u || progress.totalBytes == state.totalBytes);
        state.lastCompletedBytes = progress.completedBytes;
        state.totalBytes = progress.totalBytes;
        ++state.progressCalls;
        if (progress.phase == assets::AssetFileTransactionPreparePhase::Writing)
        {
            state.sawWriting = true;
            if (state.cancelOnWriteProgress)
                state.cancelRequested = true;
        }
        else
        {
            state.sawVerifying = true;
            if (state.cancelOnVerifyProgress)
                state.cancelRequested = true;
        }
    }

    void RunAssetFileTransactionRecoveryTests(filesystem::Manager& files, const filesystem::AbsolutePath& root) noexcept
    {
        static_cast<void>(DeleteDirectoryTree(files, root));
        Check(files.CreatePath(root), "transaction recovery root creation");

        const u8 oldBytes[] = {0x10u, 0x20u, 0x30u};
        const u8 newBytes[] = {0x40u, 0x50u, 0x60u, 0x70u};
        const u8 foreignBytes[] = {0xa1u, 0xb2u};
        const crypto::Digest256 oldDigest = crypto::Sha256(oldBytes, sizeof(oldBytes));

        // Prepare can run on an exclusive worker owner: payload IO is bounded,
        // progress is monotonic, and cooperative cancellation removes only
        // transaction-owned staging while retaining Collecting for a safe retry.
        {
            const filesystem::AbsolutePath target = root.AddFilePath("prepare-cancel-write.bin");
            const filesystem::AbsolutePath journal = root.AddFilePath("prepare-cancel-write.vtxn");
            const filesystem::AbsolutePath stage = SuffixedPath(target, assets::AssetTransactionStageSuffix);
            containers::DynamicArray<u8> payload(memory::pools::Assets::GetInstance());
            payload.Resize(1024u * 1024u);
            for (u32 index = 0u; index < payload.Size(); ++index)
                payload[index] = static_cast<u8>((index * 17u + 3u) & 0xffu);
            PrepareObserverState observerState;
            observerState.cancelOnWriteProgress = true;
            const assets::AssetFileTransactionPrepareObserver observer{
                &IsPrepareCancelled, &RecordPrepareProgress, &observerState};
            assets::AssetFileTransaction transaction;
            Check(payload.Size() == 1024u * 1024u &&
                      InitializeTransaction(transaction, files, root, journal) == assets::AssetFileTransactionResult::Success &&
                      transaction.Begin() == assets::AssetFileTransactionResult::Success &&
                      transaction.StageCreate(target, {payload.TypedData(), payload.Size()}) == assets::AssetFileTransactionResult::Success &&
                      transaction.Prepare(observer) == assets::AssetFileTransactionResult::Cancelled &&
                      transaction.GetState() == assets::AssetFileTransactionState::Collecting && observerState.sawWriting &&
                      !observerState.sawVerifying && observerState.monotonic && observerState.progressCalls != 0u &&
                      !files.FileExist(target) && !files.FileExist(stage) && !files.FileExist(journal),
                  "prepare write cancellation cleans owned staging and remains retryable");

            observerState.ResetProgress();
            Check(transaction.Prepare(observer) == assets::AssetFileTransactionResult::Success &&
                      transaction.GetState() == assets::AssetFileTransactionState::Prepared && observerState.sawWriting &&
                      observerState.sawVerifying && observerState.monotonic &&
                      observerState.lastCompletedBytes == observerState.totalBytes &&
                      observerState.totalBytes == static_cast<u64>(payload.Size()) * 2u,
                  "prepare retry reports complete bounded write and verification progress");
            Check(transaction.Rollback() == assets::AssetFileTransactionResult::Success &&
                      transaction.Shutdown() == assets::AssetFileTransactionResult::Success && !files.FileExist(stage) &&
                      !files.FileExist(journal) && !files.FileExist(target),
                  "prepare retry rollback removes only owned staging");
        }

        {
            const filesystem::AbsolutePath target = root.AddFilePath("prepare-cancel-verify.bin");
            const filesystem::AbsolutePath journal = root.AddFilePath("prepare-cancel-verify.vtxn");
            const filesystem::AbsolutePath stage = SuffixedPath(target, assets::AssetTransactionStageSuffix);
            containers::DynamicArray<u8> payload(memory::pools::Assets::GetInstance());
            payload.Resize(1024u * 1024u);
            for (u32 index = 0u; index < payload.Size(); ++index)
                payload[index] = static_cast<u8>((index * 29u + 11u) & 0xffu);
            PrepareObserverState observerState;
            observerState.cancelOnVerifyProgress = true;
            const assets::AssetFileTransactionPrepareObserver observer{
                &IsPrepareCancelled, &RecordPrepareProgress, &observerState};
            assets::AssetFileTransaction transaction;
            Check(payload.Size() == 1024u * 1024u &&
                      InitializeTransaction(transaction, files, root, journal) == assets::AssetFileTransactionResult::Success &&
                      transaction.Begin() == assets::AssetFileTransactionResult::Success &&
                      transaction.StageCreate(target, {payload.TypedData(), payload.Size()}) == assets::AssetFileTransactionResult::Success &&
                      transaction.Prepare(observer) == assets::AssetFileTransactionResult::Cancelled &&
                      transaction.GetState() == assets::AssetFileTransactionState::Collecting && observerState.sawWriting &&
                      observerState.sawVerifying && observerState.monotonic && !files.FileExist(target) &&
                      !files.FileExist(stage) && !files.FileExist(journal),
                  "prepare verification cancellation cleans owned staging and remains retryable");
            Check(transaction.Rollback() == assets::AssetFileTransactionResult::Success &&
                      transaction.Shutdown() == assets::AssetFileTransactionResult::Success,
                  "prepare verification cancellation rolls back Collecting state cleanly");
        }

        // Prepared is still a no-authored-mutation state in v5.
        {
            const filesystem::AbsolutePath target = root.AddFilePath("prepared-create.bin");
            const filesystem::AbsolutePath journal = root.AddFilePath("prepared-create.vtxn");
            {
                assets::AssetFileTransaction transaction;
                Check(InitializeTransaction(transaction, files, root, journal) == assets::AssetFileTransactionResult::Success,
                      "prepared create initialize");
                Check(transaction.Begin() == assets::AssetFileTransactionResult::Success &&
                          transaction.StageCreate(target, {newBytes, sizeof(newBytes)}) == assets::AssetFileTransactionResult::Success &&
                          transaction.Prepare() == assets::AssetFileTransactionResult::Success,
                      "prepared create stages without authored mutation");
            }
            assets::AssetFileRecoveryReport recovery;
            assets::AssetFileTransaction recovered;
            Check(InitializeTransaction(recovered, files, root, journal, &recovery) == assets::AssetFileTransactionResult::Success &&
                      recovery.journalFound && recovery.rolledBack && !files.FileExist(target),
                  "prepared restart removes only transaction-owned staging");
            Check(recovered.Shutdown() == assets::AssetFileTransactionResult::Success, "prepared recovery shutdown");
        }

        // v5 Prepared means the transaction has not touched authored paths.
        // A concurrent target appearing after Prepare must survive restart.
        {
            const filesystem::AbsolutePath target = root.AddFilePath("prepared-external.bin");
            const filesystem::AbsolutePath journal = root.AddFilePath("prepared-external.vtxn");
            {
                assets::AssetFileTransaction transaction;
                Check(InitializeTransaction(transaction, files, root, journal) == assets::AssetFileTransactionResult::Success &&
                          transaction.Begin() == assets::AssetFileTransactionResult::Success &&
                          transaction.StageWrite(target, {newBytes, sizeof(newBytes)}) == assets::AssetFileTransactionResult::Success &&
                          transaction.Prepare() == assets::AssetFileTransactionResult::Success,
                      "prepared Any write stages without authored mutation");
            }
            Check(WriteBytes(files, target, {foreignBytes, sizeof(foreignBytes)}),
                  "prepared external writer creates target after crash");
            assets::AssetFileRecoveryReport recovery;
            assets::AssetFileTransaction recovered;
            Check(InitializeTransaction(recovered, files, root, journal, &recovery) == assets::AssetFileTransactionResult::Success &&
                      recovery.journalFound && recovery.rolledBack &&
                      FileMatches(files, target, {foreignBytes, sizeof(foreignBytes)}),
                  "v5 Prepared recovery preserves concurrent authored target");
            Check(recovered.Shutdown() == assets::AssetFileTransactionResult::Success, "prepared external recovery shutdown");
            Check(files.DeleteFile(target), "prepared external fixture cleanup");
        }

        // The same v5 Prepared guarantee applies to an explicit live rollback:
        // it may clean its own stage, but it must not delete a target created by
        // another writer after Prepare.
        {
            const filesystem::AbsolutePath target = root.AddFilePath("prepared-live-rollback.bin");
            const filesystem::AbsolutePath journal = root.AddFilePath("prepared-live-rollback.vtxn");
            assets::AssetFileTransaction transaction;
            Check(InitializeTransaction(transaction, files, root, journal) == assets::AssetFileTransactionResult::Success &&
                      transaction.Begin() == assets::AssetFileTransactionResult::Success &&
                      transaction.StageWrite(target, {newBytes, sizeof(newBytes)}) == assets::AssetFileTransactionResult::Success &&
                      transaction.Prepare() == assets::AssetFileTransactionResult::Success,
                  "prepared live rollback fixture");
            Check(WriteBytes(files, target, {foreignBytes, sizeof(foreignBytes)}),
                  "prepared live rollback external target");
            Check(transaction.Rollback() == assets::AssetFileTransactionResult::Success &&
                      FileMatches(files, target, {foreignBytes, sizeof(foreignBytes)}),
                  "v5 live Prepared rollback preserves concurrent target");
            Check(transaction.Shutdown() == assets::AssetFileTransactionResult::Success, "prepared live rollback shutdown");
            Check(files.DeleteFile(target), "prepared live rollback fixture cleanup");
        }

        // A fully applied create is rolled back after a crash before PublishCommit.
        {
            const filesystem::AbsolutePath target = root.AddFilePath("applied-create.bin");
            const filesystem::AbsolutePath journal = root.AddFilePath("applied-create.vtxn");
            {
                assets::AssetFileTransaction transaction;
                Check(InitializeTransaction(transaction, files, root, journal) == assets::AssetFileTransactionResult::Success &&
                          transaction.Begin() == assets::AssetFileTransactionResult::Success &&
                          transaction.StageCreate(target, {newBytes, sizeof(newBytes)}) == assets::AssetFileTransactionResult::Success &&
                          transaction.Prepare() == assets::AssetFileTransactionResult::Success &&
                          transaction.ApplyPrepared() == assets::AssetFileTransactionResult::Success &&
                          FileMatches(files, target, {newBytes, sizeof(newBytes)}),
                      "applied create reaches crash window");
            }
            assets::AssetFileRecoveryReport recovery;
            assets::AssetFileTransaction recovered;
            Check(InitializeTransaction(recovered, files, root, journal, &recovery) == assets::AssetFileTransactionResult::Success &&
                      recovery.journalFound && recovery.rolledBack && !files.FileExist(target),
                  "applying create restart restores absent pre-state");
            Check(recovered.Shutdown() == assets::AssetFileTransactionResult::Success, "applied create recovery shutdown");
        }

        // Existing conditional writes carry both exact pre-image and post-image evidence.
        {
            const filesystem::AbsolutePath target = root.AddFilePath("applied-replace.bin");
            const filesystem::AbsolutePath journal = root.AddFilePath("applied-replace.vtxn");
            Check(WriteBytes(files, target, {oldBytes, sizeof(oldBytes)}), "replace fixture old bytes");
            {
                assets::AssetFileTransaction transaction;
                Check(InitializeTransaction(transaction, files, root, journal) == assets::AssetFileTransactionResult::Success &&
                          transaction.Begin() == assets::AssetFileTransactionResult::Success &&
                          transaction.StageWriteIfDigestMatches(target, oldDigest, {newBytes, sizeof(newBytes)}) ==
                              assets::AssetFileTransactionResult::Success &&
                          transaction.Prepare() == assets::AssetFileTransactionResult::Success &&
                          transaction.ApplyPrepared() == assets::AssetFileTransactionResult::Success &&
                          FileMatches(files, target, {newBytes, sizeof(newBytes)}),
                      "applied conditional replace reaches crash window");
            }
            assets::AssetFileRecoveryReport recovery;
            assets::AssetFileTransaction recovered;
            Check(InitializeTransaction(recovered, files, root, journal, &recovery) == assets::AssetFileTransactionResult::Success &&
                      recovery.rolledBack && FileMatches(files, target, {oldBytes, sizeof(oldBytes)}),
                  "applying conditional replace restart restores exact pre-image");
            Check(recovered.Shutdown() == assets::AssetFileTransactionResult::Success, "replace recovery shutdown");
            Check(files.DeleteFile(target), "replace recovery fixture cleanup");
        }

        // Unconditional replacement uses the atomic replace-with-backup path so
        // restart can restore the exact old image without a target-absent window.
        {
            const filesystem::AbsolutePath target = root.AddFilePath("applied-atomic-replace.bin");
            const filesystem::AbsolutePath journal = root.AddFilePath("applied-atomic-replace.vtxn");
            Check(WriteBytes(files, target, {oldBytes, sizeof(oldBytes)}), "atomic replace fixture old bytes");
            {
                assets::AssetFileTransaction transaction;
                Check(InitializeTransaction(transaction, files, root, journal) == assets::AssetFileTransactionResult::Success &&
                          transaction.Begin() == assets::AssetFileTransactionResult::Success &&
                          transaction.StageWrite(target, {newBytes, sizeof(newBytes)}) == assets::AssetFileTransactionResult::Success &&
                          transaction.Prepare() == assets::AssetFileTransactionResult::Success &&
                          transaction.ApplyPrepared() == assets::AssetFileTransactionResult::Success &&
                          FileMatches(files, target, {newBytes, sizeof(newBytes)}),
                      "atomic unconditional replace reaches Applying crash window");
            }
            assets::AssetFileRecoveryReport recovery;
            assets::AssetFileTransaction recovered;
            Check(InitializeTransaction(recovered, files, root, journal, &recovery) == assets::AssetFileTransactionResult::Success &&
                      recovery.rolledBack && FileMatches(files, target, {oldBytes, sizeof(oldBytes)}),
                  "atomic unconditional replace restart restores exact pre-image");
            Check(recovered.Shutdown() == assets::AssetFileTransactionResult::Success, "atomic replace recovery shutdown");
            Check(files.DeleteFile(target), "atomic replace fixture cleanup");
        }

        // A multi-file Applying journal must roll an interrupted cohort back as
        // one generation. Reconstruct the exact topology after member A has
        // published but before member B has published, then restart.
        {
            const filesystem::AbsolutePath targetA = root.AddFilePath("cohort-a.bin");
            const filesystem::AbsolutePath targetB = root.AddFilePath("cohort-b.bin");
            const filesystem::AbsolutePath journal = root.AddFilePath("cohort-partial.vtxn");
            const filesystem::AbsolutePath stageA = SuffixedPath(targetA, assets::AssetTransactionStageSuffix);
            const filesystem::AbsolutePath stageB = SuffixedPath(targetB, assets::AssetTransactionStageSuffix);
            const filesystem::AbsolutePath backupA = SuffixedPath(targetA, assets::AssetTransactionBackupSuffix);
            const filesystem::AbsolutePath backupB = SuffixedPath(targetB, assets::AssetTransactionBackupSuffix);
            const u8 oldA[] = {11u, 12u, 13u};
            const u8 oldB[] = {21u, 22u, 23u};
            const u8 newA[] = {31u, 32u, 33u, 34u};
            const u8 newB[] = {41u, 42u, 43u, 44u};
            Check(WriteBytes(files, targetA, {oldA, sizeof(oldA)}) && WriteBytes(files, targetB, {oldB, sizeof(oldB)}),
                  "cohort partial fixtures");
            {
                assets::AssetFileTransaction transaction;
                Check(InitializeTransaction(transaction, files, root, journal) == assets::AssetFileTransactionResult::Success &&
                          transaction.Begin() == assets::AssetFileTransactionResult::Success &&
                          transaction.StageWrite(targetA, {newA, sizeof(newA)}) == assets::AssetFileTransactionResult::Success &&
                          transaction.StageWrite(targetB, {newB, sizeof(newB)}) == assets::AssetFileTransactionResult::Success &&
                          transaction.Prepare() == assets::AssetFileTransactionResult::Success &&
                          transaction.ApplyPrepared() == assets::AssetFileTransactionResult::Success &&
                          FileMatches(files, targetA, {newA, sizeof(newA)}) && FileMatches(files, targetB, {newB, sizeof(newB)}) &&
                          files.FileExist(backupA) && files.FileExist(backupB),
                      "cohort full Applying fixture");

                Check(filesystem::SystemIO::MoveFile(targetB.AsChar(), stageB.AsChar()) &&
                          filesystem::SystemIO::MoveFile(backupB.AsChar(), targetB.AsChar()) &&
                          FileMatches(files, targetA, {newA, sizeof(newA)}) && FileMatches(files, targetB, {oldB, sizeof(oldB)}) &&
                          files.FileExist(backupA) && !files.FileExist(backupB) && files.FileExist(stageB),
                      "cohort emulates crash between member publications");
            }
            assets::AssetFileRecoveryReport recovery;
            assets::AssetFileTransaction recovered;
            Check(InitializeTransaction(recovered, files, root, journal, &recovery) == assets::AssetFileTransactionResult::Success &&
                      recovery.journalFound && recovery.rolledBack &&
                      FileMatches(files, targetA, {oldA, sizeof(oldA)}) && FileMatches(files, targetB, {oldB, sizeof(oldB)}) &&
                      !files.FileExist(stageA) && !files.FileExist(stageB) && !files.FileExist(backupA) && !files.FileExist(backupB),
                  "cohort Applying restart restores one complete old generation");
            Check(recovered.Shutdown() == assets::AssetFileTransactionResult::Success, "cohort partial recovery shutdown");
            Check(files.DeleteFile(targetA) && files.DeleteFile(targetB), "cohort partial fixture cleanup");
        }

        // Once the Committed journal is durable, restart must preserve the
        // complete new cohort and finish only transaction-owned cleanup.
        {
            const filesystem::AbsolutePath targetA = root.AddFilePath("cohort-committed-a.bin");
            const filesystem::AbsolutePath targetB = root.AddFilePath("cohort-committed-b.bin");
            const filesystem::AbsolutePath journal = root.AddFilePath("cohort-committed.vtxn");
            const u8 oldA[] = {51u, 52u};
            const u8 oldB[] = {61u, 62u};
            const u8 newA[] = {71u, 72u, 73u};
            const u8 newB[] = {81u, 82u, 83u};
            Check(WriteBytes(files, targetA, {oldA, sizeof(oldA)}) && WriteBytes(files, targetB, {oldB, sizeof(oldB)}),
                  "cohort committed fixtures");
            {
                assets::AssetFileTransaction transaction;
                Check(InitializeTransaction(transaction, files, root, journal) == assets::AssetFileTransactionResult::Success &&
                          transaction.Begin() == assets::AssetFileTransactionResult::Success &&
                          transaction.StageWrite(targetA, {newA, sizeof(newA)}) == assets::AssetFileTransactionResult::Success &&
                          transaction.StageWrite(targetB, {newB, sizeof(newB)}) == assets::AssetFileTransactionResult::Success &&
                          transaction.Prepare() == assets::AssetFileTransactionResult::Success &&
                          transaction.ApplyPrepared() == assets::AssetFileTransactionResult::Success &&
                          transaction.PublishCommit() == assets::AssetFileTransactionResult::Success,
                      "cohort committed cleanup crash window");
            }
            assets::AssetFileRecoveryReport recovery;
            assets::AssetFileTransaction recovered;
            Check(InitializeTransaction(recovered, files, root, journal, &recovery) == assets::AssetFileTransactionResult::Success &&
                      recovery.journalFound && recovery.completedCleanup &&
                      FileMatches(files, targetA, {newA, sizeof(newA)}) && FileMatches(files, targetB, {newB, sizeof(newB)}),
                  "cohort committed restart preserves one complete new generation");
            Check(recovered.Shutdown() == assets::AssetFileTransactionResult::Success, "cohort committed recovery shutdown");
            Check(files.DeleteFile(targetA) && files.DeleteFile(targetB), "cohort committed fixture cleanup");
        }

        // The same Applying record also recovers the mid-apply topology after
        // target -> backup but before stage -> target.
        {
            const filesystem::AbsolutePath target = root.AddFilePath("partial-replace.bin");
            const filesystem::AbsolutePath journal = root.AddFilePath("partial-replace.vtxn");
            const filesystem::AbsolutePath stage = SuffixedPath(target, assets::AssetTransactionStageSuffix);
            Check(WriteBytes(files, target, {oldBytes, sizeof(oldBytes)}), "partial replace fixture old bytes");
            {
                assets::AssetFileTransaction transaction;
                Check(InitializeTransaction(transaction, files, root, journal) == assets::AssetFileTransactionResult::Success &&
                          transaction.Begin() == assets::AssetFileTransactionResult::Success &&
                          transaction.StageWriteIfDigestMatches(target, oldDigest, {newBytes, sizeof(newBytes)}) ==
                              assets::AssetFileTransactionResult::Success &&
                          transaction.Prepare() == assets::AssetFileTransactionResult::Success &&
                          transaction.ApplyPrepared() == assets::AssetFileTransactionResult::Success,
                      "partial replace creates Applying evidence");
            }
            Check(filesystem::SystemIO::MoveFile(target.AsChar(), stage.AsChar()),
                  "partial replace emulates crash before stage publication");
            assets::AssetFileTransaction recovered;
            assets::AssetFileRecoveryReport recovery;
            Check(InitializeTransaction(recovered, files, root, journal, &recovery) == assets::AssetFileTransactionResult::Success &&
                      recovery.rolledBack && FileMatches(files, target, {oldBytes, sizeof(oldBytes)}) && !files.FileExist(stage),
                  "partial replace restart restores backup and removes stage");
            Check(recovered.Shutdown() == assets::AssetFileTransactionResult::Success, "partial replace recovery shutdown");
            Check(files.DeleteFile(target), "partial replace fixture cleanup");
        }

        // Deletes are represented by the exact backup pre-image.
        {
            const filesystem::AbsolutePath target = root.AddFilePath("applied-delete.bin");
            const filesystem::AbsolutePath journal = root.AddFilePath("applied-delete.vtxn");
            Check(WriteBytes(files, target, {oldBytes, sizeof(oldBytes)}), "delete fixture old bytes");
            {
                assets::AssetFileTransaction transaction;
                Check(InitializeTransaction(transaction, files, root, journal) == assets::AssetFileTransactionResult::Success &&
                          transaction.Begin() == assets::AssetFileTransactionResult::Success &&
                          transaction.StageDeleteIfDigestMatches(target, oldDigest) == assets::AssetFileTransactionResult::Success &&
                          transaction.Prepare() == assets::AssetFileTransactionResult::Success &&
                          transaction.ApplyPrepared() == assets::AssetFileTransactionResult::Success && !files.FileExist(target),
                      "applied delete reaches crash window");
            }
            assets::AssetFileTransaction recovered;
            assets::AssetFileRecoveryReport recovery;
            Check(InitializeTransaction(recovered, files, root, journal, &recovery) == assets::AssetFileTransactionResult::Success &&
                      recovery.rolledBack && FileMatches(files, target, {oldBytes, sizeof(oldBytes)}),
                  "applying delete restart restores exact pre-image");
            Check(recovered.Shutdown() == assets::AssetFileTransactionResult::Success, "delete recovery shutdown");
            Check(files.DeleteFile(target), "delete fixture cleanup");
        }

        // Move recovery needs exact source evidence and, when occupied, target evidence.
        {
            const filesystem::AbsolutePath source = root.AddFilePath("move-source.bin");
            const filesystem::AbsolutePath target = root.AddFilePath("move-target.bin");
            const filesystem::AbsolutePath journal = root.AddFilePath("applied-move.vtxn");
            Check(WriteBytes(files, source, {newBytes, sizeof(newBytes)}) && WriteBytes(files, target, {oldBytes, sizeof(oldBytes)}),
                  "move fixture bytes");
            {
                assets::AssetFileTransaction transaction;
                Check(InitializeTransaction(transaction, files, root, journal) == assets::AssetFileTransactionResult::Success &&
                          transaction.Begin() == assets::AssetFileTransactionResult::Success &&
                          transaction.StageMove(source, target) == assets::AssetFileTransactionResult::Success &&
                          transaction.Prepare() == assets::AssetFileTransactionResult::Success &&
                          transaction.ApplyPrepared() == assets::AssetFileTransactionResult::Success && !files.FileExist(source) &&
                          FileMatches(files, target, {newBytes, sizeof(newBytes)}),
                      "applied move reaches crash window");
            }
            assets::AssetFileTransaction recovered;
            assets::AssetFileRecoveryReport recovery;
            Check(InitializeTransaction(recovered, files, root, journal, &recovery) == assets::AssetFileTransactionResult::Success &&
                      recovery.rolledBack && FileMatches(files, source, {newBytes, sizeof(newBytes)}) &&
                      FileMatches(files, target, {oldBytes, sizeof(oldBytes)}),
                  "applying move restart restores source and occupied target");
            Check(recovered.Shutdown() == assets::AssetFileTransactionResult::Success, "move recovery shutdown");
            Check(files.DeleteFile(source) && files.DeleteFile(target), "move fixture cleanup");
        }

        // A committed journal is never rolled back; restart only finishes owned cleanup.
        {
            const filesystem::AbsolutePath target = root.AddFilePath("committed-create.bin");
            const filesystem::AbsolutePath journal = root.AddFilePath("committed-create.vtxn");
            {
                assets::AssetFileTransaction transaction;
                Check(InitializeTransaction(transaction, files, root, journal) == assets::AssetFileTransactionResult::Success &&
                          transaction.Begin() == assets::AssetFileTransactionResult::Success &&
                          transaction.StageCreate(target, {newBytes, sizeof(newBytes)}) == assets::AssetFileTransactionResult::Success &&
                          transaction.Prepare() == assets::AssetFileTransactionResult::Success &&
                          transaction.ApplyPrepared() == assets::AssetFileTransactionResult::Success &&
                          transaction.PublishCommit() == assets::AssetFileTransactionResult::Success,
                      "committed create reaches cleanup crash window");
            }
            assets::AssetFileTransaction recovered;
            assets::AssetFileRecoveryReport recovery;
            Check(InitializeTransaction(recovered, files, root, journal, &recovery) == assets::AssetFileTransactionResult::Success &&
                      recovery.journalFound && recovery.completedCleanup && FileMatches(files, target, {newBytes, sizeof(newBytes)}),
                  "committed restart preserves authored result and completes cleanup");
            Check(recovered.Shutdown() == assets::AssetFileTransactionResult::Success, "committed recovery shutdown");
            Check(files.DeleteFile(target), "committed fixture cleanup");
        }

        // Exact post-image evidence must not authorize rollback across a later
        // external edit. Preserve the foreign target and all recovery evidence.
        {
            const filesystem::AbsolutePath target = root.AddFilePath("ambiguous-replace.bin");
            const filesystem::AbsolutePath journal = root.AddFilePath("ambiguous-replace.vtxn");
            const filesystem::AbsolutePath backup = SuffixedPath(target, assets::AssetTransactionBackupSuffix);
            Check(WriteBytes(files, target, {oldBytes, sizeof(oldBytes)}), "ambiguous replace old bytes");
            {
                assets::AssetFileTransaction transaction;
                Check(InitializeTransaction(transaction, files, root, journal) == assets::AssetFileTransactionResult::Success &&
                          transaction.Begin() == assets::AssetFileTransactionResult::Success &&
                          transaction.StageWriteIfDigestMatches(target, oldDigest, {newBytes, sizeof(newBytes)}) ==
                              assets::AssetFileTransactionResult::Success &&
                          transaction.Prepare() == assets::AssetFileTransactionResult::Success &&
                          transaction.ApplyPrepared() == assets::AssetFileTransactionResult::Success,
                      "ambiguous replace creates Applying evidence");
            }
            Check(WriteBytes(files, target, {foreignBytes, sizeof(foreignBytes)}), "ambiguous replace external edit");
            {
                assets::AssetFileTransaction recovered;
                assets::AssetFileRecoveryReport recovery;
                Check(InitializeTransaction(recovered, files, root, journal, &recovery) ==
                              assets::AssetFileTransactionResult::RecoveryRequired &&
                          recovery.journalFound && FileMatches(files, target, {foreignBytes, sizeof(foreignBytes)}) &&
                          files.FileExist(backup) && files.FileExist(journal),
                      "ambiguous Applying recovery fails closed and preserves foreign state/evidence");
            }
            Check(files.DeleteFile(target) && files.DeleteFile(backup) && files.DeleteFile(journal),
                  "ambiguous recovery fixture manual cleanup");
        }

        Check(DeleteDirectoryTree(files, root), "transaction recovery root cleanup");
    }

    [[nodiscard]] resources::ResourceReference Reference(const char* const path, const resources::ResourceTypeId type) noexcept
    {
        return resources::ResourceReference(resources::ResourcePath::FromString(path), type);
    }

    [[nodiscard]] bool AddDependency(assets::DependencyCollector& collector, const resources::ResourceReference identity,
                                     const assets::BuildFingerprint& content, const assets::DependencyRole role) noexcept
    {
        return collector.Add({identity, content, role, assets::DependencyRequirement::Required}) == assets::Result::Success;
    }

    bool DiscoverDependencies(const assets::BuildRequest&, assets::DependencyCollector& collector, void* const userData) noexcept
    {
        auto& state = *static_cast<CompilerState*>(userData);
        ++state.discoverCalls;
        if (state.mode == CompilerMode::DuplicateDependency)
        {
            static_cast<void>(AddDependency(collector, state.firstDependency, state.firstContent, assets::DependencyRole::Source));
            static_cast<void>(
                collector.Add({state.firstDependency, state.firstContent, assets::DependencyRole::Source, assets::DependencyRequirement::Required}));
            return true;
        }

        if (state.reverseDependencies)
        {
            return AddDependency(collector, state.secondDependency, state.secondContent, assets::DependencyRole::Generated) &&
                   AddDependency(collector, state.firstDependency, state.firstContent, assets::DependencyRole::Source);
        }
        return AddDependency(collector, state.firstDependency, state.firstContent, assets::DependencyRole::Source) &&
               AddDependency(collector, state.secondDependency, state.secondContent, assets::DependencyRole::Generated);
    }

    bool Compile(const assets::CompileContext& context, assets::ArtifactWriter& writer, void* const userData) noexcept
    {
        auto& state = *static_cast<CompilerState*>(userData);
        ++state.compileCalls;
        const u8 primary[] = {static_cast<u8>(context.request.target),
                              context.request.source.content.Empty() ? static_cast<u8>(0) : context.request.source.content[0], state.firstContent.bytes[0],
                              state.secondContent.bytes[0]};
        const assets::ArtifactFlags primaryFlags = state.mode == CompilerMode::MissingPrimary
                                                       ? assets::ArtifactFlags::MemoryResident
                                                       : assets::ArtifactFlags::Primary | assets::ArtifactFlags::MemoryResident;
        if (writer.Add(context.request.output, 1, primaryFlags, 8, primary, sizeof(primary)) != assets::Result::Success)
        {
            return false;
        }

        const u8 auxiliary[] = {0x41, 0x55, 0x58};
        if (writer.Add(state.auxiliaryOutput, 7, assets::ArtifactFlags::Streamable, 4, auxiliary, sizeof(auxiliary)) != assets::Result::Success)
        {
            return false;
        }
        if (state.mode == CompilerMode::WriterLimitFailure)
        {
            const u8 overflow[] = {0x4f, 0x56};
            return writer.Add(state.auxiliaryOutput, 8, assets::ArtifactFlags::Streamable, 4, overflow, sizeof(overflow)) ==
                   assets::Result::Success;
        }
        return true;
    }
} // namespace

int main()
{
    using namespace vanguard;

    Check(memory::Initialize(), "memory initialization");
    Check(diagnostics::Initialize(diagnostics::Mode::Synchronous, "assetsTests"), "diagnostics initialization");
    Check(containers::Initialize(), "containers initialization");
    Check(vanguard::io::Initialize(), "I/O initialization");

    const filesystem::AbsolutePath working = tests::ScratchDirectory("assets_tests");
    Check(filesystem::Initialize({working, working, working}), "filesystem initialization");
    filesystem::Manager& files = filesystem::GetManager();
    RunAssetFileTransactionRecoveryTests(files, working.AddDirPath("vtxn_recovery"));

    CompilerState state;
    state.firstDependency = Reference("shaders/includes/common.inc", IncludeType);
    state.secondDependency = Reference("generated/layouts/common.layout", IncludeType);
    state.auxiliaryOutput = Reference("cooked/test.asset.meta", AuxiliaryType);
    const char firstDependencyBytes[] = "first dependency";
    const char secondDependencyBytes[] = "second dependency";
    state.firstContent = crypto::Sha256(firstDependencyBytes, sizeof(firstDependencyBytes) - 1u);
    state.secondContent = crypto::Sha256(secondDependencyBytes, sizeof(secondDependencyBytes) - 1u);

    const assets::CompilerId compilerId = assets::HashCompilerName("assets.test_compiler");
    assets::CompilerDescriptor compiler{compilerId, "assets.test_compiler", 1, SourceType, OutputType, &DiscoverDependencies, &Compile, &state};

    assets::Config config;
    config.maximumCacheEntries = 4;
    config.maximumCacheBytes = 1024 * 1024;
    config.maximumArtifactBytesPerBuild = 8;
    assets::BuildSystem system;
    Check(system.Initialize(config), "build system initialization");
    Check(system.RegisterCompiler(compiler) == assets::Result::Success, "compiler registration");
    Check(system.RegisterCompiler(compiler) == assets::Result::CompilerAlreadyRegistered, "ambiguous compiler registration rejected");

    const u8 sourceBytes[] = {1, 2, 3, 4};
    const u8 changedSourceBytes[] = {9, 2, 3, 4};
    const u8 metadataBytes[] = {7, 8};
    const u8 settingsA[] = {0x10};
    const u8 settingsB[] = {0x20};
    const resources::ResourceReference source = Reference("source/test.asset", SourceType);
    const resources::ResourceReference output = Reference("cooked/test.asset", OutputType);

    const u8 capacityPayload[] = {1, 2, 3, 4};
    assets::ArtifactWriter capacityWriter(output, 2, 6);
    Check(capacityWriter.GetMaximumArtifactCount() == 2 && capacityWriter.GetRemainingArtifactCount() == 2 &&
              capacityWriter.GetMaximumByteCount() == 6 && capacityWriter.GetRemainingByteCount() == 6,
          "artifact writer exposes its initial capacity");
    Check(capacityWriter.Add(output, 0, assets::ArtifactFlags::Primary, 4, capacityPayload, sizeof(capacityPayload)) == assets::Result::Success &&
              capacityWriter.GetRemainingArtifactCount() == 1 && capacityWriter.GetRemainingByteCount() == 2,
          "artifact writer reports remaining capacity after a write");
    Check(capacityWriter.Add(state.auxiliaryOutput, 0, assets::ArtifactFlags::Streamable, 4, capacityPayload, 3) ==
                  assets::Result::LimitExceeded &&
              capacityWriter.GetStatus() == assets::Result::LimitExceeded &&
              capacityWriter.Add(state.auxiliaryOutput, 1, assets::ArtifactFlags::Streamable, 4, capacityPayload, 1) ==
                  assets::Result::LimitExceeded,
          "artifact writer preserves a sticky capacity failure");

    assets::BuildRequest request{{source, {sourceBytes, 4}, {metadataBytes, 2}}, output, assets::TargetPlatform::WindowsD3D12, {settingsA, 1}};

    assets::BuildOutput first;
    Check(system.Build(request, first) == assets::Result::Success && first.disposition == assets::BuildDisposition::Built && first.artifacts.Size() == 2 &&
              !first.buildFingerprint.IsEmpty() && !first.contentFingerprint.IsEmpty() && state.compileCalls == 1,
          "first request builds a multi-artifact output");

    state.reverseDependencies = true;
    assets::BuildOutput reordered;
    Check(system.Build(request, reordered) == assets::Result::Success && reordered.disposition == assets::BuildDisposition::CacheHit &&
              reordered.buildFingerprint == first.buildFingerprint && reordered.contentFingerprint == first.contentFingerprint && state.compileCalls == 1,
          "dependency order does not change cache identity");

    assets::BuildRequest changedSource = request;
    changedSource.source.content = {changedSourceBytes, 4};
    assets::BuildOutput sourceInvalidated;
    Check(system.Build(changedSource, sourceInvalidated) == assets::Result::Success && sourceInvalidated.disposition == assets::BuildDisposition::Built &&
              sourceInvalidated.buildFingerprint != first.buildFingerprint && state.compileCalls == 2,
          "source content invalidates derived data");

    const char changedDependencyBytes[] = "changed dependency";
    state.firstContent = crypto::Sha256(changedDependencyBytes, sizeof(changedDependencyBytes) - 1u);
    assets::BuildOutput dependencyInvalidated;
    Check(system.Build(request, dependencyInvalidated) == assets::Result::Success && dependencyInvalidated.disposition == assets::BuildDisposition::Built &&
              dependencyInvalidated.buildFingerprint != first.buildFingerprint && state.compileCalls == 3,
          "dependency content invalidates derived data");

    assets::BuildRequest platformChanged = request;
    platformChanged.target = assets::TargetPlatform::WindowsVulkan;
    assets::BuildOutput platformInvalidated;
    Check(system.Build(platformChanged, platformInvalidated) == assets::Result::Success && platformInvalidated.disposition == assets::BuildDisposition::Built &&
              state.compileCalls == 4,
          "target platform participates in build fingerprint");

    assets::BuildRequest settingsChanged = request;
    settingsChanged.settings = {settingsB, 1};
    assets::BuildOutput settingsInvalidated;
    Check(system.Build(settingsChanged, settingsInvalidated) == assets::Result::Success && settingsInvalidated.disposition == assets::BuildDisposition::Built &&
              state.compileCalls == 5,
          "build settings participate in build fingerprint");

    state.mode = CompilerMode::DuplicateDependency;
    assets::BuildRequest duplicateRequest = request;
    const u8 duplicateSettings[] = {0x31};
    duplicateRequest.settings = {duplicateSettings, 1};
    assets::BuildOutput duplicateOutput;
    Check(system.Build(duplicateRequest, duplicateOutput) == assets::Result::DuplicateDependency, "duplicate dependencies fail deterministically");

    state.mode = CompilerMode::MissingPrimary;
    assets::BuildRequest invalidArtifactRequest = request;
    const u8 invalidSettings[] = {0x32};
    invalidArtifactRequest.settings = {invalidSettings, 1};
    assets::BuildOutput invalidArtifactOutput;
    Check(system.Build(invalidArtifactRequest, invalidArtifactOutput) == assets::Result::InvalidArtifact, "compiler must emit an explicit primary artifact");

    state.mode = CompilerMode::WriterLimitFailure;
    assets::BuildRequest writerLimitRequest = request;
    const u8 writerLimitSettings[] = {0x33};
    writerLimitRequest.settings = {writerLimitSettings, 1};
    assets::BuildOutput writerLimitOutput;
    Check(system.Build(writerLimitRequest, writerLimitOutput) == assets::Result::LimitExceeded,
          "artifact writer failure is returned instead of generic compiler failure");
    state.mode = CompilerMode::Normal;

    Check(system.UnregisterCompiler(compilerId) == assets::Result::Success, "compiler unregistration");
    compiler.version = 2;
    Check(system.RegisterCompiler(compiler) == assets::Result::Success, "new compiler version registration");
    assets::BuildOutput versionInvalidated;
    Check(system.Build(request, versionInvalidated) == assets::Result::Success && versionInvalidated.disposition == assets::BuildDisposition::Built &&
              versionInvalidated.buildFingerprint != dependencyInvalidated.buildFingerprint && state.compileCalls == 8,
          "compiler version invalidates derived data");

    const assets::Stats stats = system.GetStats();
    Check(stats.registeredCompilers == 1 && stats.activeBuilds == 0 && stats.buildRequests == 10 && stats.localBuilds == 6 && stats.cacheHits == 1 &&
              stats.cacheMisses == 6 && stats.failedBuilds == 3 && stats.cacheEntries == 4 && stats.cacheStores == 6 && stats.cacheEvictions == 2,
          "build and cache telemetry");

    Check(system.UnregisterCompiler(compilerId) == assets::Result::Success, "final compiler unregistration");
    Check(system.Shutdown(), "build system shutdown");

    filesystem::Shutdown();
    vanguard::io::Shutdown();
    if (g_failures == 0)
    {
        VG_LOG_INFO(diagnostics::Category::FunctionalTests, "[assetsTests] Vanguard cooking core checks passed");
    }
    diagnostics::Shutdown();
    return g_failures == 0 ? 0 : 1;
}
