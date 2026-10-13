#include <vanguard/assets/asset_file_transaction.hpp>

#include <vanguard/crypto/crypto.hpp>
#include <vanguard/memory/memory.hpp>
#include <vanguard/system/platform.hpp>

#include <algorithm>
#include <cstring>

#if VG_PLATFORM_WINDOWS
#include <Windows.h>
#include <bcrypt.h>
#endif

namespace vanguard::assets
{
    namespace
    {
        constexpr u32 JournalMagic = 0x4e585456u; // VTXN
        constexpr u32 JournalVersion = 5u;
        constexpr u32 AssertionJournalVersion = 4u;
        constexpr u32 DigestConditionalJournalVersion = 3u;
        constexpr u32 ConditionalWriteJournalVersion = 2u;
        constexpr u32 LegacyJournalVersion = 1u;
        constexpr u32 JournalHeaderBytes = 48u;
        constexpr u32 PrepareWriteChunkBytes = 256u * 1024u;
        constexpr u32 PrepareVerifyChunkBytes = 64u * 1024u;

        enum class MutationKind : u8 { Write, Move, Delete, AssertDigest };
        enum class WriteCondition : u8 { Any, MustNotExist, ExpectedBytes, ExpectedDigest };
        enum class FileMatchResult : u8 { Match, Mismatch, IoFailure };
        enum class JournalState : u32 { Staging = 1u, Prepared = 2u, Committed = 3u, Applying = 4u };

        constexpr u8 JournalEvidencePreTarget = 1u << 0u;
        constexpr u8 JournalEvidencePreSource = 1u << 1u;
        constexpr u8 JournalEvidencePostTarget = 1u << 2u;

        struct Mutation
        {
            Mutation() noexcept
                : bytes(memory::pools::Assets::GetInstance()), expectedBytes(memory::pools::Assets::GetInstance()) {}
            Mutation(const Mutation&) = delete;
            Mutation& operator=(const Mutation&) = delete;
            Mutation(Mutation&&) noexcept = default;
            Mutation& operator=(Mutation&&) noexcept = default;

            MutationKind kind = MutationKind::Write;
            filesystem::AbsolutePath source;
            filesystem::AbsolutePath target;
            filesystem::AbsolutePath staged;
            filesystem::AbsolutePath backup;
            containers::DynamicArray<u8> bytes;
            containers::DynamicArray<u8> expectedBytes;
            crypto::Digest256 preTargetDigest;
            crypto::Digest256 preSourceDigest;
            crypto::Digest256 postDigest;
            crypto::Digest256 expectedDigest;
            bool targetExisted = false;
            WriteCondition writeCondition = WriteCondition::Any;
            bool preTargetDigestValid = false;
            bool preSourceDigestValid = false;
            bool postDigestValid = false;
            bool touched = false;
            bool backupMatchesExpected = false;
            bool conflictingTargetWon = false;
        };

        [[nodiscard]] filesystem::AbsolutePath SuffixedPath(const filesystem::AbsolutePath& path, const char* const suffix) noexcept
        {
            const containers::StringView view = path.AsStringView();
            containers::String value(view.Data(), view.Length());
            value.Append(suffix, static_cast<u32>(std::strlen(suffix)));
            return filesystem::AbsolutePath::CreateFilePath(value);
        }

        [[nodiscard]] bool AppendByte(containers::DynamicArray<u8>& bytes, const u8 value) noexcept
        {
            const u32 old = bytes.Size();
            bytes.PushBack(value);
            return bytes.Size() == old + 1u;
        }

        [[nodiscard]] bool AppendU32(containers::DynamicArray<u8>& bytes, const u32 value) noexcept
        {
            for (u32 shift = 0; shift < 32u; shift += 8u)
                if (!AppendByte(bytes, static_cast<u8>(value >> shift)))
                    return false;
            return true;
        }

        [[nodiscard]] bool AppendDigest(containers::DynamicArray<u8>& bytes, const crypto::Digest256& digest) noexcept
        {
            for (u32 index = 0; index < crypto::Digest256::ByteCount; ++index)
                if (!AppendByte(bytes, digest.bytes[index]))
                    return false;
            return true;
        }

        [[nodiscard]] bool AppendPath(containers::DynamicArray<u8>& bytes, const filesystem::AbsolutePath& path) noexcept
        {
            const containers::StringView view = path.AsStringView();
            if (!AppendU32(bytes, view.Length()))
                return false;
            for (const char value : view)
                if (!AppendByte(bytes, static_cast<u8>(value)))
                    return false;
            return true;
        }

        [[nodiscard]] bool ReadU32(const containers::ArraySpan<const u8> bytes, u32& cursor, u32& value) noexcept
        {
            if (cursor > bytes.Count() || bytes.Count() - cursor < 4u)
                return false;
            value = static_cast<u32>(bytes[cursor]) | (static_cast<u32>(bytes[cursor + 1u]) << 8u) |
                    (static_cast<u32>(bytes[cursor + 2u]) << 16u) | (static_cast<u32>(bytes[cursor + 3u]) << 24u);
            cursor += 4u;
            return true;
        }

        [[nodiscard]] bool ReadDigest(const containers::ArraySpan<const u8> bytes, u32& cursor, crypto::Digest256& digest) noexcept
        {
            if (cursor > bytes.Count() || crypto::Digest256::ByteCount > bytes.Count() - cursor)
                return false;
            std::memcpy(digest.bytes, bytes.Data() + cursor, crypto::Digest256::ByteCount);
            cursor += crypto::Digest256::ByteCount;
            return true;
        }

        [[nodiscard]] bool ReadPath(const containers::ArraySpan<const u8> bytes, u32& cursor, filesystem::AbsolutePath& path,
                                    const bool allowEmpty = false) noexcept
        {
            u32 length = 0;
            if (!ReadU32(bytes, cursor, length) || cursor > bytes.Count() || length > bytes.Count() - cursor)
                return false;
            if (length == 0u)
            {
                path = {};
                return allowEmpty;
            }
            for (u32 index = 0; index < length; ++index)
                if (bytes[cursor + index] == 0u)
                    return false;
            containers::String value(reinterpret_cast<const char*>(bytes.Data() + cursor), length);
            if (value.Length() != length)
                return false;
            cursor += length;
            path = filesystem::AbsolutePath::CreateFilePath(value);
            return path.IsFilePath();
        }

        [[nodiscard]] bool ReadFile(filesystem::Manager& files, const filesystem::AbsolutePath& path, const u32 maximumBytes,
                                    containers::DynamicArray<u8>& bytes) noexcept
        {
            auto reader = files.CreateFileReader(path, filesystem::FOF_Buffered);
            if (!reader)
                return false;
            const u64 size = reader->GetSize();
            if (size > maximumBytes || size > 0xffffffffull)
                return false;
            bytes.Resize(static_cast<u32>(size));
            if (bytes.Size() != size)
                return false;
            if (size != 0u)
                reader->Serialize(bytes.TypedData(), static_cast<usize>(size));
            return !reader->HasErrors();
        }

        [[nodiscard]] FileMatchResult MatchFile(filesystem::Manager& files, const filesystem::AbsolutePath& path,
                                                const containers::ArraySpan<const u8> expected) noexcept
        {
            auto reader = files.CreateFileReader(path, filesystem::FOF_Buffered);
            if (!reader)
                return files.FileExist(path) ? FileMatchResult::IoFailure : FileMatchResult::Mismatch;
            if (reader->GetSize() != expected.Count())
                return FileMatchResult::Mismatch;
            u8 buffer[4096];
            u32 offset = 0;
            while (offset < expected.Count())
            {
                const u32 count = std::min<u32>(static_cast<u32>(sizeof(buffer)), expected.Count() - offset);
                reader->Serialize(buffer, count);
                if (reader->HasErrors())
                    return FileMatchResult::IoFailure;
                if (std::memcmp(buffer, expected.Data() + offset, count) != 0)
                    return FileMatchResult::Mismatch;
                offset += count;
            }
            return reader->HasErrors() ? FileMatchResult::IoFailure : FileMatchResult::Match;
        }

        [[nodiscard]] bool DigestBytes(const containers::ArraySpan<const u8> bytes, crypto::Digest256& digest) noexcept
        {
            if (!bytes.Empty() && bytes.Data() == nullptr)
                return false;
            digest = crypto::Sha256(bytes.Data(), bytes.Count());
            return true;
        }

#if VG_PLATFORM_WINDOWS
        // A separate streaming CNG path is deliberately local to file evidence.
        // The portable Sha256Builder remains allocation-free and is used when
        // the OS provider is unavailable or the file is small. Incremental file
        // verification never needs to retain a second giant source buffer.
        struct WindowsFileSha256 final
        {
            explicit WindowsFileSha256(const bool enable) noexcept
            {
                if (enable && ::BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0u) >= 0)
                    if (::BCryptCreateHash(algorithm, &hash, nullptr, 0u, nullptr, 0u, 0u) < 0)
                        hash = nullptr;
            }
            ~WindowsFileSha256()
            {
                if (hash != nullptr)
                    ::BCryptDestroyHash(hash);
                if (algorithm != nullptr)
                    ::BCryptCloseAlgorithmProvider(algorithm, 0u);
            }
            WindowsFileSha256(const WindowsFileSha256&) = delete;
            WindowsFileSha256& operator=(const WindowsFileSha256&) = delete;
            [[nodiscard]] bool IsReady() const noexcept { return hash != nullptr; }
            [[nodiscard]] bool Update(const u8* const bytes, const u32 count) noexcept
            {
                return hash != nullptr && ::BCryptHashData(hash, const_cast<PUCHAR>(bytes), count, 0u) >= 0;
            }
            [[nodiscard]] bool Finalize(crypto::Digest256& digest) noexcept
            {
                return hash != nullptr &&
                       ::BCryptFinishHash(hash, digest.bytes, crypto::Digest256::ByteCount, 0u) >= 0;
            }
            BCRYPT_ALG_HANDLE algorithm = nullptr;
            BCRYPT_HASH_HANDLE hash = nullptr;
        };
#endif

        [[nodiscard]] FileMatchResult DigestFile(filesystem::Manager& files, const filesystem::AbsolutePath& path,
                                                crypto::Digest256& digest) noexcept
        {
            auto reader = files.CreateFileReader(path, filesystem::FOF_Buffered);
            if (!reader)
                return files.FileExist(path) ? FileMatchResult::IoFailure : FileMatchResult::Mismatch;
            crypto::Sha256Builder hash;
            u8 buffer[64u * 1024u];
            u64 remaining = reader->GetSize();
#if VG_PLATFORM_WINDOWS
            WindowsFileSha256 accelerated(remaining >= (1u << 20u));
            const bool useAccelerated = accelerated.IsReady();
#endif
            while (remaining != 0u)
            {
                const u32 count = static_cast<u32>(std::min<u64>(sizeof(buffer), remaining));
                reader->Serialize(buffer, count);
#if VG_PLATFORM_WINDOWS
                const bool updated = useAccelerated ? accelerated.Update(buffer, count) : hash.Update(buffer, count);
#else
                const bool updated = hash.Update(buffer, count);
#endif
                if (reader->HasErrors() || !updated)
                    return FileMatchResult::IoFailure;
                remaining -= count;
            }
#if VG_PLATFORM_WINDOWS
            const bool finalized = useAccelerated ? accelerated.Finalize(digest) : hash.Finalize(digest);
#else
            const bool finalized = hash.Finalize(digest);
#endif
            return !reader->HasErrors() && finalized ? FileMatchResult::Match : FileMatchResult::IoFailure;
        }

        [[nodiscard]] FileMatchResult MatchFileDigest(filesystem::Manager& files, const filesystem::AbsolutePath& path,
                                                      const crypto::Digest256& expected) noexcept
        {
            crypto::Digest256 actual;
            const FileMatchResult result = DigestFile(files, path, actual);
            return result == FileMatchResult::Match ? (actual == expected ? FileMatchResult::Match : FileMatchResult::Mismatch) : result;
        }

        [[nodiscard]] bool WriteAndVerify(filesystem::Manager& files, const filesystem::AbsolutePath& path,
                                          const containers::ArraySpan<const u8> bytes) noexcept
        {
            {
                // RawFileWriter::Flush reaches NativeFileHandle::Flush; the
                // buffered wrapper only drains its in-memory buffer.
                auto writer = files.CreateFileWriter(path);
                if (!writer)
                    return false;
                if (!bytes.Empty())
                    writer->Serialize(const_cast<u8*>(bytes.Data()), bytes.Count());
                writer->Flush();
                if (writer->HasErrors())
                    return false;
            }
            return MatchFile(files, path, bytes) == FileMatchResult::Match;
        }

        enum class PrepareStageResult : u8
        {
            Success,
            Cancelled,
            IoFailure
        };

        [[nodiscard]] bool IsPrepareCancellationRequested(const AssetFileTransactionPrepareObserver& observer) noexcept
        {
            return observer.isCancellationRequested != nullptr && observer.isCancellationRequested(observer.userData);
        }

        void ReportPrepareProgress(const AssetFileTransactionPrepareObserver& observer,
                                   const AssetFileTransactionPreparePhase phase, const u64 completedBytes,
                                   const u64 totalBytes) noexcept
        {
            if (observer.reportProgress != nullptr)
                observer.reportProgress({phase, completedBytes, totalBytes}, observer.userData);
        }

        [[nodiscard]] u64 AddProgressBytes(const u64 current, const u64 increment) noexcept
        {
            return increment > ~u64{0} - current ? ~u64{0} : current + increment;
        }

        [[nodiscard]] PrepareStageResult WriteAndVerifyPreparedStage(
            filesystem::Manager& files, const filesystem::AbsolutePath& path, const containers::ArraySpan<const u8> bytes,
            const AssetFileTransactionPrepareObserver& observer, u64& completedBytes, const u64 totalBytes) noexcept
        {
            if (IsPrepareCancellationRequested(observer))
                return PrepareStageResult::Cancelled;

            {
                // Keep the writer scoped so cancellation/failure closes the file
                // before transaction-owned cleanup attempts to remove the stage.
                auto writer = files.CreateFileWriter(path);
                if (!writer)
                    return PrepareStageResult::IoFailure;
                u32 offset = 0u;
                while (offset < bytes.Count())
                {
                    if (IsPrepareCancellationRequested(observer))
                        return PrepareStageResult::Cancelled;
                    const u32 count = std::min<u32>(PrepareWriteChunkBytes, bytes.Count() - offset);
                    writer->Serialize(const_cast<u8*>(bytes.Data() + offset), count);
                    if (writer->HasErrors())
                        return PrepareStageResult::IoFailure;
                    offset += count;
                    completedBytes = AddProgressBytes(completedBytes, count);
                    ReportPrepareProgress(observer, AssetFileTransactionPreparePhase::Writing, completedBytes, totalBytes);
                }
                if (IsPrepareCancellationRequested(observer))
                    return PrepareStageResult::Cancelled;
                // RawFileWriter::Flush reaches NativeFileHandle::Flush. This call
                // may block in the OS, so cancellation is checked again once it
                // returns; the bounded write chunks keep the cooperative portions
                // of Prepare responsive without weakening durability.
                writer->Flush();
                if (writer->HasErrors())
                    return PrepareStageResult::IoFailure;
            }

            if (IsPrepareCancellationRequested(observer))
                return PrepareStageResult::Cancelled;
            auto reader = files.CreateFileReader(path, filesystem::FOF_Buffered);
            if (!reader)
                return PrepareStageResult::IoFailure;
            if (reader->GetSize() != bytes.Count())
                return PrepareStageResult::IoFailure;
            u8 buffer[PrepareVerifyChunkBytes];
            u32 offset = 0u;
            while (offset < bytes.Count())
            {
                if (IsPrepareCancellationRequested(observer))
                    return PrepareStageResult::Cancelled;
                const u32 count = std::min<u32>(PrepareVerifyChunkBytes, bytes.Count() - offset);
                reader->Serialize(buffer, count);
                if (reader->HasErrors() || std::memcmp(buffer, bytes.Data() + offset, count) != 0)
                    return PrepareStageResult::IoFailure;
                offset += count;
                completedBytes = AddProgressBytes(completedBytes, count);
                ReportPrepareProgress(observer, AssetFileTransactionPreparePhase::Verifying, completedBytes, totalBytes);
            }
            if (reader->HasErrors())
                return PrepareStageResult::IoFailure;
            return IsPrepareCancellationRequested(observer) ? PrepareStageResult::Cancelled : PrepareStageResult::Success;
        }

        [[nodiscard]] bool PathInRoots(const containers::ArraySpan<const filesystem::AbsolutePath> roots,
                                       const filesystem::AbsolutePath& path) noexcept
        {
            for (const filesystem::AbsolutePath& root : roots)
                if (filesystem::ValidateMutationPath(root, path) == filesystem::MutationPathResult::Success)
                    return true;
            return false;
        }

        [[nodiscard]] i32 RootIndex(const containers::ArraySpan<const filesystem::AbsolutePath> roots,
                                    const filesystem::AbsolutePath& path) noexcept
        {
            for (u32 index = 0; index < roots.Count(); ++index)
                if (filesystem::ValidateMutationPath(roots[index], path) == filesystem::MutationPathResult::Success)
                    return static_cast<i32>(index);
            return -1;
        }

        [[nodiscard]] bool SamePhysicalPath(const filesystem::AbsolutePath& left, const filesystem::AbsolutePath& right) noexcept
        {
            return left.AsStringView().Length() == right.AsStringView().Length() &&
                   left.AsStringView().StartsWithIgnoreCase(right.AsStringView());
        }
    }

    struct AssetFileTransaction::Impl
    {
        VANGUARD_USE_MEMORY_POOL(memory::pools::Assets);

        Impl() noexcept
            : roots(memory::pools::Assets::GetInstance()), assertionRoots(memory::pools::Assets::GetInstance()),
              mutations(memory::pools::Assets::GetInstance()) {}

        filesystem::Manager* files = nullptr;
        filesystem::AbsolutePath journal;
        filesystem::AbsolutePath journalStage;
        containers::DynamicArray<filesystem::AbsolutePath> roots;
        containers::DynamicArray<filesystem::AbsolutePath> assertionRoots;
        containers::DynamicArray<Mutation> mutations;
        AssetFileTransactionState state = AssetFileTransactionState::Closed;
        bool appliedPrepared = false;
        u32 maximumEntries = 0;
        u64 maximumFileBytes = 0;
        u64 maximumStagedBytes = 0;
        u32 maximumJournalBytes = 0;
        u64 stagedBytes = 0;
    };

    namespace
    {
        [[nodiscard]] containers::ArraySpan<const filesystem::AbsolutePath> Roots(const AssetFileTransaction::Impl& impl) noexcept
        {
            return {impl.roots.TypedData(), impl.roots.Size()};
        }

        [[nodiscard]] containers::ArraySpan<const filesystem::AbsolutePath> AssertionRoots(const AssetFileTransaction::Impl& impl) noexcept
        {
            return {impl.assertionRoots.TypedData(), impl.assertionRoots.Size()};
        }

        [[nodiscard]] bool ValidateMutationPaths(const AssetFileTransaction::Impl& impl, const Mutation& mutation) noexcept
        {
            if (mutation.kind == MutationKind::AssertDigest)
                return PathInRoots(AssertionRoots(impl), mutation.target) && mutation.source.Empty() && mutation.staged.Empty() &&
                       mutation.backup.Empty();
            if (!PathInRoots(Roots(impl), mutation.target))
                return false;
            return PathInRoots(Roots(impl), mutation.staged) && PathInRoots(Roots(impl), mutation.backup) &&
                   (mutation.kind != MutationKind::Move || PathInRoots(Roots(impl), mutation.source));
        }

        [[nodiscard]] bool DeriveOwnedPaths(Mutation& mutation) noexcept
        {
            mutation.staged = SuffixedPath(mutation.target, ".vtxn-stage");
            mutation.backup = SuffixedPath(mutation.target, ".vtxn-backup");
            return mutation.staged.IsFilePath() && mutation.backup.IsFilePath() &&
                   filesystem::paths::ParentAbsolutePath(mutation.staged) == filesystem::paths::ParentAbsolutePath(mutation.target) &&
                   filesystem::paths::ParentAbsolutePath(mutation.backup) == filesystem::paths::ParentAbsolutePath(mutation.target);
        }

        [[nodiscard]] bool HasPathConflict(const AssetFileTransaction::Impl& impl, const filesystem::AbsolutePath& source,
                                           const filesystem::AbsolutePath& target) noexcept
        {
            if ((!source.Empty() && SamePhysicalPath(source, target)) || SamePhysicalPath(target, impl.journal) ||
                SamePhysicalPath(target, impl.journalStage) ||
                (!source.Empty() &&
                 (SamePhysicalPath(source, impl.journal) || SamePhysicalPath(source, impl.journalStage))))
                return true;
            for (const Mutation& mutation : impl.mutations)
            {
                if (SamePhysicalPath(target, mutation.target) || SamePhysicalPath(target, mutation.staged) || SamePhysicalPath(target, mutation.backup) ||
                    (!mutation.source.Empty() && SamePhysicalPath(target, mutation.source)))
                    return true;
                if (!source.Empty() && (SamePhysicalPath(source, mutation.target) || SamePhysicalPath(source, mutation.staged) ||
                                        SamePhysicalPath(source, mutation.backup) || (!mutation.source.Empty() && SamePhysicalPath(source, mutation.source))))
                    return true;
            }
            return false;
        }

        [[nodiscard]] bool HasOwnedPathConflict(const AssetFileTransaction::Impl& impl, const Mutation& mutation) noexcept
        {
            return HasPathConflict(impl, {}, mutation.staged) || HasPathConflict(impl, {}, mutation.backup) ||
                   SamePhysicalPath(mutation.staged, mutation.backup) || SamePhysicalPath(mutation.staged, mutation.target) ||
                   SamePhysicalPath(mutation.backup, mutation.target) ||
                   (!mutation.source.Empty() && (SamePhysicalPath(mutation.staged, mutation.source) || SamePhysicalPath(mutation.backup, mutation.source)));
        }

        [[nodiscard]] AssetFileTransactionResult StageWriteInternal(
            AssetFileTransaction::Impl& impl, const filesystem::AbsolutePath& target,
            const containers::ArraySpan<const u8> bytes, const WriteCondition condition,
            const containers::ArraySpan<const u8> expectedBytes = {},
            const crypto::Digest256* const expectedDigest = nullptr) noexcept
        {
            if (!target.IsFilePath() || (!bytes.Empty() && bytes.Data() == nullptr) ||
                (!expectedBytes.Empty() && expectedBytes.Data() == nullptr) || bytes.Count() > impl.maximumFileBytes ||
                expectedBytes.Count() > impl.maximumFileBytes || bytes.Count() > impl.maximumStagedBytes - impl.stagedBytes ||
                expectedBytes.Count() > impl.maximumStagedBytes - impl.stagedBytes - bytes.Count() ||
                !PathInRoots(Roots(impl), target) || HasPathConflict(impl, {}, target))
                return AssetFileTransactionResult::InvalidArgument;
            if (impl.mutations.Size() == impl.maximumEntries)
                return AssetFileTransactionResult::LimitExceeded;

            Mutation mutation;
            mutation.kind = MutationKind::Write;
            mutation.target = target;
            mutation.writeCondition = condition;
            if (!DigestBytes(bytes, mutation.postDigest))
                return AssetFileTransactionResult::IoFailure;
            mutation.postDigestValid = true;
            if (expectedDigest != nullptr)
                mutation.expectedDigest = *expectedDigest;
            if (!DeriveOwnedPaths(mutation) || HasOwnedPathConflict(impl, mutation) || !PathInRoots(Roots(impl), mutation.staged) ||
                !PathInRoots(Roots(impl), mutation.backup))
                return AssetFileTransactionResult::LimitExceeded;
            mutation.bytes.Resize(bytes.Count());
            mutation.expectedBytes.Resize(expectedBytes.Count());
            if (mutation.bytes.Size() != bytes.Count() || mutation.expectedBytes.Size() != expectedBytes.Count())
                return AssetFileTransactionResult::LimitExceeded;
            if (!bytes.Empty())
                std::memcpy(mutation.bytes.TypedData(), bytes.Data(), bytes.Count());
            if (!expectedBytes.Empty())
                std::memcpy(mutation.expectedBytes.TypedData(), expectedBytes.Data(), expectedBytes.Count());

            const u32 oldEntryCount = impl.mutations.Size();
            impl.mutations.PushBack(static_cast<Mutation&&>(mutation));
            if (impl.mutations.Size() != oldEntryCount + 1u || impl.mutations[oldEntryCount].target != target ||
                impl.mutations[oldEntryCount].bytes.Size() != bytes.Count() ||
                impl.mutations[oldEntryCount].expectedBytes.Size() != expectedBytes.Count())
                return AssetFileTransactionResult::LimitExceeded;
            impl.stagedBytes += static_cast<u64>(bytes.Count()) + expectedBytes.Count();
            return AssetFileTransactionResult::Success;
        }

        [[nodiscard]] AssetFileTransactionResult StageDeleteInternal(
            AssetFileTransaction::Impl& impl, const filesystem::AbsolutePath& target,
            const WriteCondition condition, const crypto::Digest256* const expectedDigest = nullptr) noexcept
        {
            if (!target.IsFilePath() || !PathInRoots(Roots(impl), target) || HasPathConflict(impl, {}, target))
                return AssetFileTransactionResult::InvalidArgument;
            if (impl.mutations.Size() == impl.maximumEntries)
                return AssetFileTransactionResult::LimitExceeded;
            Mutation mutation;
            mutation.kind = MutationKind::Delete;
            mutation.target = target;
            mutation.writeCondition = condition;
            if (expectedDigest != nullptr)
                mutation.expectedDigest = *expectedDigest;
            if (!DeriveOwnedPaths(mutation) || HasOwnedPathConflict(impl, mutation) || !PathInRoots(Roots(impl), mutation.staged) ||
                !PathInRoots(Roots(impl), mutation.backup))
                return AssetFileTransactionResult::LimitExceeded;
            const u32 old = impl.mutations.Size();
            impl.mutations.PushBack(static_cast<Mutation&&>(mutation));
            return impl.mutations.Size() == old + 1u ? AssetFileTransactionResult::Success
                                                     : AssetFileTransactionResult::LimitExceeded;
        }

        [[nodiscard]] AssetFileTransactionResult StageAssertDigestInternal(
            AssetFileTransaction::Impl& impl, const filesystem::AbsolutePath& target,
            const crypto::Digest256& expectedDigest) noexcept
        {
            if (!target.IsFilePath() || expectedDigest.IsEmpty() || !PathInRoots(AssertionRoots(impl), target) ||
                HasPathConflict(impl, {}, target))
                return AssetFileTransactionResult::InvalidArgument;
            if (impl.mutations.Size() == impl.maximumEntries)
                return AssetFileTransactionResult::LimitExceeded;
            Mutation mutation;
            mutation.kind = MutationKind::AssertDigest;
            mutation.target = target;
            mutation.writeCondition = WriteCondition::ExpectedDigest;
            mutation.expectedDigest = expectedDigest;
            const u32 old = impl.mutations.Size();
            impl.mutations.PushBack(static_cast<Mutation&&>(mutation));
            return impl.mutations.Size() == old + 1u ? AssetFileTransactionResult::Success
                                                     : AssetFileTransactionResult::LimitExceeded;
        }

        [[nodiscard]] bool EncodeJournal(const AssetFileTransaction::Impl& impl, const JournalState state,
                                         containers::DynamicArray<u8>& bytes) noexcept
        {
            if (!AppendU32(bytes, JournalMagic) || !AppendU32(bytes, JournalVersion) || !AppendU32(bytes, static_cast<u32>(state)) ||
                !AppendU32(bytes, impl.mutations.Size()))
                return false;
            for (u32 index = 0; index < crypto::Digest256::ByteCount; ++index)
                if (!AppendByte(bytes, 0u))
                    return false;
            for (const Mutation& mutation : impl.mutations)
            {
                if (!AppendByte(bytes, static_cast<u8>(mutation.kind)) || !AppendByte(bytes, mutation.targetExisted ? 1u : 0u) ||
                    !AppendByte(bytes, static_cast<u8>(mutation.writeCondition)) || !AppendByte(bytes, 0u) ||
                    !AppendPath(bytes, mutation.source) || !AppendPath(bytes, mutation.target) ||
                    bytes.Size() > impl.maximumJournalBytes)
                    return false;
                u8 evidence = 0u;
                if (mutation.preTargetDigestValid)
                    evidence |= JournalEvidencePreTarget;
                if (mutation.preSourceDigestValid)
                    evidence |= JournalEvidencePreSource;
                if (mutation.postDigestValid)
                    evidence |= JournalEvidencePostTarget;
                if (!AppendByte(bytes, evidence) || !AppendByte(bytes, 0u) || !AppendByte(bytes, 0u) || !AppendByte(bytes, 0u) ||
                    !AppendDigest(bytes, mutation.preTargetDigest) || !AppendDigest(bytes, mutation.preSourceDigest) ||
                    !AppendDigest(bytes, mutation.postDigest) || bytes.Size() > impl.maximumJournalBytes)
                    return false;
            }
            crypto::Sha256Builder hash;
            crypto::Digest256 digest;
            if (!hash.Update(bytes.TypedData(), 16u) ||
                !hash.Update(bytes.TypedData() + JournalHeaderBytes, bytes.Size() - JournalHeaderBytes) || !hash.Finalize(digest))
                return false;
            std::memcpy(bytes.TypedData() + 16u, digest.bytes, crypto::Digest256::ByteCount);
            return bytes.Size() <= impl.maximumJournalBytes;
        }

        [[nodiscard]] bool PublishJournal(AssetFileTransaction::Impl& impl, const JournalState state) noexcept
        {
            containers::DynamicArray<u8> bytes(memory::pools::Assets::GetInstance());
            if (!EncodeJournal(impl, state, bytes))
                return false;
            if (impl.files->FileExist(impl.journalStage) && !impl.files->DeleteFile(impl.journalStage))
                return false;
            if (!WriteAndVerify(*impl.files, impl.journalStage, {bytes.TypedData(), bytes.Size()}))
                return false;
            return filesystem::ReplaceFile(*impl.files, impl.journalStage, impl.journal);
        }

        [[nodiscard]] bool CleanupOwnedFiles(AssetFileTransaction::Impl& impl) noexcept;

        [[nodiscard]] AssetFileTransactionResult CaptureApplyingEvidence(AssetFileTransaction::Impl& impl) noexcept
        {
            for (Mutation& mutation : impl.mutations)
            {
                mutation.preTargetDigest = {};
                mutation.preSourceDigest = {};
                mutation.preTargetDigestValid = false;
                mutation.preSourceDigestValid = false;

                if (mutation.kind == MutationKind::AssertDigest)
                {
                    if (!impl.files->FileExist(mutation.target))
                        return AssetFileTransactionResult::Conflict;
                    const FileMatchResult match = MatchFileDigest(*impl.files, mutation.target, mutation.expectedDigest);
                    if (match != FileMatchResult::Match)
                        return match == FileMatchResult::Mismatch ? AssetFileTransactionResult::Conflict
                                                                 : AssetFileTransactionResult::IoFailure;
                    continue;
                }

                if (impl.files->FileExist(mutation.backup) ||
                    (mutation.kind != MutationKind::Write && impl.files->FileExist(mutation.staged)))
                    return AssetFileTransactionResult::RecoveryRequired;

                const bool targetExists = impl.files->FileExist(mutation.target);
                if ((mutation.writeCondition == WriteCondition::MustNotExist && targetExists) ||
                    ((mutation.writeCondition == WriteCondition::ExpectedBytes ||
                      mutation.writeCondition == WriteCondition::ExpectedDigest) &&
                     !targetExists))
                    return AssetFileTransactionResult::Conflict;
                if (targetExists != mutation.targetExisted)
                    return AssetFileTransactionResult::IoFailure;

                if (mutation.kind == MutationKind::Write)
                {
                    if (!mutation.postDigestValid || !impl.files->FileExist(mutation.staged))
                        return AssetFileTransactionResult::IoFailure;
                    const FileMatchResult stagedMatch = MatchFileDigest(*impl.files, mutation.staged, mutation.postDigest);
                    if (stagedMatch != FileMatchResult::Match)
                        return AssetFileTransactionResult::IoFailure;
                }

                if (mutation.targetExisted)
                {
                    if (mutation.writeCondition == WriteCondition::ExpectedBytes)
                    {
                        const FileMatchResult match =
                            MatchFile(*impl.files, mutation.target, {mutation.expectedBytes.TypedData(), mutation.expectedBytes.Size()});
                        if (match != FileMatchResult::Match)
                            return match == FileMatchResult::Mismatch ? AssetFileTransactionResult::Conflict
                                                                     : AssetFileTransactionResult::IoFailure;
                        if (!DigestBytes({mutation.expectedBytes.TypedData(), mutation.expectedBytes.Size()}, mutation.preTargetDigest))
                            return AssetFileTransactionResult::IoFailure;
                    }
                    else if (mutation.writeCondition == WriteCondition::ExpectedDigest)
                    {
                        const FileMatchResult match = MatchFileDigest(*impl.files, mutation.target, mutation.expectedDigest);
                        if (match != FileMatchResult::Match)
                            return match == FileMatchResult::Mismatch ? AssetFileTransactionResult::Conflict
                                                                     : AssetFileTransactionResult::IoFailure;
                        mutation.preTargetDigest = mutation.expectedDigest;
                    }
                    else if (DigestFile(*impl.files, mutation.target, mutation.preTargetDigest) != FileMatchResult::Match)
                        return AssetFileTransactionResult::IoFailure;
                    mutation.preTargetDigestValid = true;
                }

                if (mutation.kind == MutationKind::Move)
                {
                    if (!impl.files->FileExist(mutation.source) ||
                        DigestFile(*impl.files, mutation.source, mutation.preSourceDigest) != FileMatchResult::Match)
                        return AssetFileTransactionResult::IoFailure;
                    mutation.preSourceDigestValid = true;
                }
            }
            return AssetFileTransactionResult::Success;
        }

        [[nodiscard]] bool CanRecoverApplyingMutation(AssetFileTransaction::Impl& impl, const Mutation& mutation) noexcept
        {
            if (!ValidateMutationPaths(impl, mutation))
                return false;
            if (mutation.kind == MutationKind::AssertDigest)
                return !mutation.preTargetDigestValid && !mutation.preSourceDigestValid && !mutation.postDigestValid;
            if (mutation.kind != MutationKind::Write && impl.files->FileExist(mutation.staged))
                return false;

            const bool targetExists = impl.files->FileExist(mutation.target);
            const bool backupExists = impl.files->FileExist(mutation.backup);
            if (mutation.kind == MutationKind::Write)
            {
                if (!mutation.postDigestValid || mutation.preSourceDigestValid)
                    return false;
                const bool stageExists = impl.files->FileExist(mutation.staged);
                if (mutation.targetExisted)
                {
                    if (!mutation.preTargetDigestValid)
                        return false;
                    if (backupExists)
                    {
                        if (MatchFileDigest(*impl.files, mutation.backup, mutation.preTargetDigest) != FileMatchResult::Match)
                            return false;
                        if (targetExists)
                            return !stageExists &&
                                   MatchFileDigest(*impl.files, mutation.target, mutation.postDigest) == FileMatchResult::Match;
                        return stageExists;
                    }
                    return targetExists &&
                           MatchFileDigest(*impl.files, mutation.target, mutation.preTargetDigest) == FileMatchResult::Match;
                }
                if (mutation.preTargetDigestValid || backupExists)
                    return false;
                if (stageExists || !targetExists)
                    return true;
                return MatchFileDigest(*impl.files, mutation.target, mutation.postDigest) == FileMatchResult::Match;
            }

            if (mutation.kind == MutationKind::Delete)
            {
                if (!mutation.targetExisted || !mutation.preTargetDigestValid || mutation.preSourceDigestValid ||
                    mutation.postDigestValid)
                    return false;
                if (backupExists)
                    return !targetExists &&
                           MatchFileDigest(*impl.files, mutation.backup, mutation.preTargetDigest) == FileMatchResult::Match;
                return targetExists &&
                       MatchFileDigest(*impl.files, mutation.target, mutation.preTargetDigest) == FileMatchResult::Match;
            }

            if (mutation.kind != MutationKind::Move || !mutation.preSourceDigestValid || mutation.postDigestValid)
                return false;
            const bool sourceExists = impl.files->FileExist(mutation.source);
            if (mutation.targetExisted != mutation.preTargetDigestValid)
                return false;
            if (sourceExists)
            {
                if (MatchFileDigest(*impl.files, mutation.source, mutation.preSourceDigest) != FileMatchResult::Match)
                    return false;
                if (!mutation.targetExisted)
                    return !backupExists && !targetExists;
                if (backupExists)
                {
                    if (MatchFileDigest(*impl.files, mutation.backup, mutation.preTargetDigest) != FileMatchResult::Match)
                        return false;
                    return !targetExists ||
                           MatchFileDigest(*impl.files, mutation.target, mutation.preTargetDigest) == FileMatchResult::Match;
                }
                return targetExists &&
                       MatchFileDigest(*impl.files, mutation.target, mutation.preTargetDigest) == FileMatchResult::Match;
            }

            if (!targetExists || MatchFileDigest(*impl.files, mutation.target, mutation.preSourceDigest) != FileMatchResult::Match)
                return false;
            if (mutation.targetExisted)
                return backupExists &&
                       MatchFileDigest(*impl.files, mutation.backup, mutation.preTargetDigest) == FileMatchResult::Match;
            return !backupExists;
        }

        [[nodiscard]] bool RecoverApplyingMutation(AssetFileTransaction::Impl& impl, Mutation& mutation) noexcept
        {
            if (!CanRecoverApplyingMutation(impl, mutation))
                return false;
            if (mutation.kind == MutationKind::AssertDigest)
                return true;

            const bool targetExists = impl.files->FileExist(mutation.target);
            const bool backupExists = impl.files->FileExist(mutation.backup);
            if (mutation.kind == MutationKind::Write)
            {
                const bool stageExists = impl.files->FileExist(mutation.staged);
                if (mutation.targetExisted)
                {
                    if (!backupExists)
                        return true;
                    // Restore in one filesystem operation. Delete-then-restore
                    // would create a crash tuple (backup present, target absent,
                    // stage consumed) that is ambiguous on the next restart.
                    return filesystem::ReplaceFile(*impl.files, mutation.backup, mutation.target);
                }
                if (stageExists || !targetExists)
                    return true;
                return impl.files->DeleteFile(mutation.target);
            }

            if (mutation.kind == MutationKind::Delete)
                return !backupExists || filesystem::ReplaceFile(*impl.files, mutation.backup, mutation.target);

            const bool sourceExists = impl.files->FileExist(mutation.source);
            if (sourceExists)
            {
                if (mutation.targetExisted && backupExists && !targetExists)
                    return filesystem::ReplaceFile(*impl.files, mutation.backup, mutation.target);
                return true;
            }
            if (!filesystem::SystemIO::MoveFile(mutation.target.AsChar(), mutation.source.AsChar()))
                return false;
            return !mutation.targetExisted || filesystem::ReplaceFile(*impl.files, mutation.backup, mutation.target);
        }

        [[nodiscard]] bool RollbackApplying(AssetFileTransaction::Impl& impl) noexcept
        {
            for (const Mutation& mutation : impl.mutations)
                if (!CanRecoverApplyingMutation(impl, mutation))
                    return false;
            for (u32 ordinal = impl.mutations.Size(); ordinal > 0u; --ordinal)
                if (!RecoverApplyingMutation(impl, impl.mutations[ordinal - 1u]))
                    return false;
            return CleanupOwnedFiles(impl);
        }

        [[nodiscard]] bool CleanupUnappliedPrepared(AssetFileTransaction::Impl& impl) noexcept
        {
            for (const Mutation& mutation : impl.mutations)
            {
                if (!ValidateMutationPaths(impl, mutation))
                    return false;
                if (mutation.kind == MutationKind::AssertDigest)
                    continue;
                // v5 Prepared proves no authored mutation has begun. A backup
                // therefore means either a newer Applying transaction or
                // external interference; preserve it rather than guessing.
                if (impl.files->FileExist(mutation.backup) ||
                    (mutation.kind != MutationKind::Write && impl.files->FileExist(mutation.staged)))
                    return false;
            }
            // Never restore or delete authored targets/sources here. Any changes
            // to those paths happened outside this v5 transaction.
            return CleanupOwnedFiles(impl);
        }

        [[nodiscard]] bool CleanupOwnedFiles(AssetFileTransaction::Impl& impl) noexcept
        {
            bool success = true;
            for (Mutation& mutation : impl.mutations)
            {
                if (mutation.kind == MutationKind::AssertDigest)
                    continue;
                if (impl.files->FileExist(mutation.staged) && !impl.files->DeleteFile(mutation.staged))
                    success = false;
                if (impl.files->FileExist(mutation.backup) && !impl.files->DeleteFile(mutation.backup))
                    success = false;
            }
            if (impl.files->FileExist(impl.journalStage) && !impl.files->DeleteFile(impl.journalStage))
                success = false;
            return success;
        }

        [[nodiscard]] bool RestoreTarget(AssetFileTransaction::Impl& impl, Mutation& mutation) noexcept
        {
            if (impl.files->FileExist(mutation.backup))
            {
                if (impl.files->FileExist(mutation.target) && !impl.files->DeleteFile(mutation.target))
                    return false;
                return filesystem::ReplaceFile(*impl.files, mutation.backup, mutation.target);
            }
            if (mutation.targetExisted)
                return impl.files->FileExist(mutation.target);
            return !impl.files->FileExist(mutation.target) || impl.files->DeleteFile(mutation.target);
        }

        [[nodiscard]] bool RollbackPrepared(AssetFileTransaction::Impl& impl) noexcept
        {
            for (const Mutation& mutation : impl.mutations)
                if (!ValidateMutationPaths(impl, mutation))
                    return false;
            for (u32 ordinal = impl.mutations.Size(); ordinal > 0u; --ordinal)
            {
                Mutation& mutation = impl.mutations[ordinal - 1u];
                if (mutation.kind == MutationKind::AssertDigest)
                    continue;
                if (mutation.kind == MutationKind::Move && !impl.files->FileExist(mutation.source))
                {
                    // A completed move into an originally occupied target must
                    // still have that target's backup. Without it, recovery
                    // cannot prove which bytes belong at either path.
                    if (mutation.targetExisted && !impl.files->FileExist(mutation.backup))
                        return false;
                    if (!impl.files->FileExist(mutation.target) || !filesystem::SystemIO::MoveFile(mutation.target.AsChar(), mutation.source.AsChar()))
                        return false;
                }
                if (mutation.writeCondition == WriteCondition::MustNotExist && !mutation.targetExisted &&
                    impl.files->FileExist(mutation.target))
                {
                    // If the stage still exists, the transaction never won the
                    // no-replace publish and the visible target belongs to the
                    // competing writer. If stage was consumed, publication won
                    // but a crash occurred before the Committed journal; preserve
                    // the ambiguous target and require explicit recovery.
                    if (!impl.files->FileExist(mutation.staged))
                        return false;
                    continue;
                }
                if ((mutation.writeCondition == WriteCondition::ExpectedBytes || mutation.writeCondition == WriteCondition::ExpectedDigest) &&
                    impl.files->FileExist(mutation.backup) &&
                    impl.files->FileExist(mutation.target))
                    return false;
                if (!RestoreTarget(impl, mutation))
                    return false;
            }
            return CleanupOwnedFiles(impl);
        }

        [[nodiscard]] bool RollbackTouched(AssetFileTransaction::Impl& impl) noexcept
        {
            for (u32 ordinal = impl.mutations.Size(); ordinal > 0u; --ordinal)
            {
                Mutation& mutation = impl.mutations[ordinal - 1u];
                if (!mutation.touched)
                    continue;
                if (mutation.conflictingTargetWon)
                    // The transaction moved the verified previous target aside,
                    // but a concurrent writer then won the original path before
                    // the conditional stage could publish. Neither file may be
                    // discarded or overwritten automatically: preserve both and
                    // require explicit recovery.
                    return false;
                if (mutation.writeCondition == WriteCondition::MustNotExist && impl.files->FileExist(mutation.target))
                {
                    const FileMatchResult match = MatchFile(*impl.files, mutation.target,
                                                            {mutation.bytes.TypedData(), mutation.bytes.Size()});
                    if (match != FileMatchResult::Match)
                        return false;
                }
                if ((mutation.writeCondition == WriteCondition::ExpectedBytes || mutation.writeCondition == WriteCondition::ExpectedDigest) &&
                    impl.files->FileExist(mutation.backup))
                {
                    if (!mutation.backupMatchesExpected && impl.files->FileExist(mutation.target))
                        return false;
                    if (mutation.backupMatchesExpected && impl.files->FileExist(mutation.target))
                    {
                        if (mutation.kind == MutationKind::Delete)
                            return false;
                        const FileMatchResult match = mutation.writeCondition == WriteCondition::ExpectedDigest
                                                          ? MatchFileDigest(*impl.files, mutation.target, mutation.postDigest)
                                                          : MatchFile(*impl.files, mutation.target,
                                                                      {mutation.bytes.TypedData(), mutation.bytes.Size()});
                        if (match != FileMatchResult::Match)
                            return false;
                    }
                }
                if (mutation.kind == MutationKind::Move && !impl.files->FileExist(mutation.source))
                {
                    if (mutation.targetExisted && !impl.files->FileExist(mutation.backup))
                        return false;
                    if (!impl.files->FileExist(mutation.target) ||
                        !filesystem::SystemIO::MoveFile(mutation.target.AsChar(), mutation.source.AsChar()))
                        return false;
                }
                if (!RestoreTarget(impl, mutation))
                    return false;
            }
            return CleanupOwnedFiles(impl);
        }

        [[nodiscard]] bool ValidateCommitted(const AssetFileTransaction::Impl& impl) noexcept
        {
            for (const Mutation& mutation : impl.mutations)
            {
                if (mutation.kind == MutationKind::AssertDigest)
                    continue;
                if (mutation.kind == MutationKind::Delete)
                {
                    if (impl.files->FileExist(mutation.target))
                        return false;
                    continue;
                }
                if (!impl.files->FileExist(mutation.target) ||
                    (mutation.kind == MutationKind::Move && impl.files->FileExist(mutation.source)))
                    return false;
            }
            return true;
        }

        void FillPostConditions(const AssetFileTransaction::Impl& impl,
                                const containers::ArraySpan<const filesystem::AbsolutePath> trackedPaths,
                                const containers::ArraySpan<AssetFileTransactionPostCondition> postConditions) noexcept
        {
            for (u32 index = 0; index < postConditions.Count(); ++index)
                postConditions[index] = {};
            for (u32 index = 0; index < trackedPaths.Count(); ++index)
            {
                AssetFileTransactionPostCondition& condition = postConditions[index];
                for (const Mutation& mutation : impl.mutations)
                {
                    if (SamePhysicalPath(trackedPaths[index], mutation.target))
                    {
                        if (mutation.kind == MutationKind::Write && mutation.postDigestValid)
                        {
                            condition.contentDigest = mutation.postDigest;
                            condition.exists = true;
                            condition.known = true;
                        }
                        else if (mutation.kind == MutationKind::Delete)
                        {
                            condition.exists = false;
                            condition.known = true;
                        }
                        break;
                    }
                    if (mutation.kind == MutationKind::Move && SamePhysicalPath(trackedPaths[index], mutation.source))
                    {
                        condition.exists = false;
                        condition.known = true;
                        break;
                    }
                }
            }
        }

        [[nodiscard]] bool DecodeJournal(AssetFileTransaction::Impl& impl, const containers::ArraySpan<const u8> bytes,
                                         JournalState& state, u32& decodedVersion) noexcept
        {
            if (bytes.Count() < JournalHeaderBytes)
                return false;
            u32 cursor = 0;
            u32 magic = 0, version = 0, encodedState = 0, count = 0;
            if (!ReadU32(bytes, cursor, magic) || !ReadU32(bytes, cursor, version) || !ReadU32(bytes, cursor, encodedState) ||
                !ReadU32(bytes, cursor, count) || magic != JournalMagic || version < LegacyJournalVersion || version > JournalVersion ||
                count == 0u || count > impl.maximumEntries || encodedState < static_cast<u32>(JournalState::Staging) ||
                encodedState > (version >= JournalVersion ? static_cast<u32>(JournalState::Applying)
                                                          : static_cast<u32>(JournalState::Committed)) ||
                (version < JournalVersion && encodedState == static_cast<u32>(JournalState::Applying)))
                return false;
            crypto::Digest256 expected;
            std::memcpy(expected.bytes, bytes.Data() + 16u, crypto::Digest256::ByteCount);
            crypto::Sha256Builder hash;
            crypto::Digest256 actual;
            if (!hash.Update(bytes.Data(), 16u) || !hash.Update(bytes.Data() + JournalHeaderBytes, bytes.Count() - JournalHeaderBytes) ||
                !hash.Finalize(actual) || !(actual == expected))
                return false;
            cursor = JournalHeaderBytes;
            for (u32 index = 0; index < count; ++index)
            {
                if (cursor > bytes.Count() || bytes.Count() - cursor < 4u)
                    return false;
                Mutation mutation;
                mutation.kind = static_cast<MutationKind>(bytes[cursor++]);
                const u8 targetExisted = bytes[cursor++];
                const u8 encodedWriteCondition = bytes[cursor++];
                const u8 reserved1 = bytes[cursor++];
                mutation.targetExisted = targetExisted != 0u;
                mutation.writeCondition = version == LegacyJournalVersion ? WriteCondition::Any
                                                                          : static_cast<WriteCondition>(encodedWriteCondition);
                const u8 maximumCondition = version >= DigestConditionalJournalVersion ? static_cast<u8>(WriteCondition::ExpectedDigest)
                                                                                        : static_cast<u8>(WriteCondition::ExpectedBytes);
                const bool validConditionForKind =
                    mutation.kind == MutationKind::Write ||
                    (mutation.kind == MutationKind::Delete &&
                     (mutation.writeCondition == WriteCondition::Any || mutation.writeCondition == WriteCondition::ExpectedDigest)) ||
                    (mutation.kind == MutationKind::Move && mutation.writeCondition == WriteCondition::Any) ||
                    (version >= AssertionJournalVersion && mutation.kind == MutationKind::AssertDigest &&
                     mutation.writeCondition == WriteCondition::ExpectedDigest);
                if (mutation.kind > (version >= AssertionJournalVersion ? MutationKind::AssertDigest : MutationKind::Delete) ||
                    !ReadPath(bytes, cursor, mutation.source, mutation.kind != MutationKind::Move) ||
                    !ReadPath(bytes, cursor, mutation.target) ||
                    targetExisted > 1u || (version == LegacyJournalVersion && encodedWriteCondition != 0u) ||
                    (version != LegacyJournalVersion && encodedWriteCondition > maximumCondition) ||
                    reserved1 != 0u || !validConditionForKind ||
                    (mutation.kind != MutationKind::AssertDigest &&
                     (!DeriveOwnedPaths(mutation) || HasOwnedPathConflict(impl, mutation))) ||
                    !ValidateMutationPaths(impl, mutation) ||
                    (mutation.kind == MutationKind::Move && RootIndex(Roots(impl), mutation.source) != RootIndex(Roots(impl), mutation.target)) ||
                    (mutation.kind != MutationKind::Move && !mutation.source.Empty()) || HasPathConflict(impl, mutation.source, mutation.target))
                    return false;
                if (version >= JournalVersion)
                {
                    if (cursor > bytes.Count() || bytes.Count() - cursor < 4u)
                        return false;
                    const u8 evidence = bytes[cursor++];
                    const u8 reserved2 = bytes[cursor++];
                    const u8 reserved3 = bytes[cursor++];
                    const u8 reserved4 = bytes[cursor++];
                    if ((evidence & ~(JournalEvidencePreTarget | JournalEvidencePreSource | JournalEvidencePostTarget)) != 0u ||
                        reserved2 != 0u || reserved3 != 0u || reserved4 != 0u ||
                        !ReadDigest(bytes, cursor, mutation.preTargetDigest) ||
                        !ReadDigest(bytes, cursor, mutation.preSourceDigest) || !ReadDigest(bytes, cursor, mutation.postDigest))
                        return false;
                    mutation.preTargetDigestValid = (evidence & JournalEvidencePreTarget) != 0u;
                    mutation.preSourceDigestValid = (evidence & JournalEvidencePreSource) != 0u;
                    mutation.postDigestValid = (evidence & JournalEvidencePostTarget) != 0u;

                    const bool applyingOrCommitted =
                        encodedState == static_cast<u32>(JournalState::Applying) ||
                        encodedState == static_cast<u32>(JournalState::Committed);
                    if (mutation.kind == MutationKind::Write)
                    {
                        if (!mutation.postDigestValid || mutation.preSourceDigestValid ||
                            (applyingOrCommitted && mutation.preTargetDigestValid != mutation.targetExisted) ||
                            (!applyingOrCommitted && mutation.preTargetDigestValid))
                            return false;
                    }
                    else if (mutation.kind == MutationKind::Move)
                    {
                        if (mutation.postDigestValid ||
                            (applyingOrCommitted &&
                             (!mutation.preSourceDigestValid || mutation.preTargetDigestValid != mutation.targetExisted)) ||
                            (!applyingOrCommitted && (mutation.preTargetDigestValid || mutation.preSourceDigestValid)))
                            return false;
                    }
                    else if (mutation.kind == MutationKind::Delete)
                    {
                        if (mutation.postDigestValid || mutation.preSourceDigestValid ||
                            (applyingOrCommitted && (!mutation.targetExisted || !mutation.preTargetDigestValid)) ||
                            (!applyingOrCommitted && mutation.preTargetDigestValid))
                            return false;
                    }
                    else if (mutation.preTargetDigestValid || mutation.preSourceDigestValid || mutation.postDigestValid)
                        return false;
                }
                impl.mutations.PushBack(static_cast<Mutation&&>(mutation));
                if (impl.mutations.Size() != index + 1u)
                    return false;
            }
            if (cursor != bytes.Count())
                return false;
            state = static_cast<JournalState>(encodedState);
            decodedVersion = version;
            return true;
        }

        void ClearTransaction(AssetFileTransaction::Impl& impl) noexcept
        {
            impl.mutations.Clear();
            impl.stagedBytes = 0u;
            impl.appliedPrepared = false;
            impl.state = AssetFileTransactionState::Ready;
        }
    }

    bool AssetFileTransactionConfig::IsValid() const noexcept
    {
        return journalPath.IsFilePath() && !writableRoots.Empty() && writableRoots.Data() != nullptr &&
               !assertionRoots.Empty() && assertionRoots.Data() != nullptr && maximumEntries != 0u &&
               maximumFileBytes != 0u && maximumStagedBytes >= maximumFileBytes && maximumJournalBytes >= JournalHeaderBytes;
    }

    AssetFileTransaction::~AssetFileTransaction()
    {
        if (m_impl != nullptr)
            VANGUARD_DELETE(m_impl);
    }

    AssetFileTransactionResult AssetFileTransaction::Initialize(filesystem::Manager& files, const AssetFileTransactionConfig& config,
                                                                AssetFileRecoveryReport* const recovery) noexcept
    {
        if (recovery != nullptr)
            *recovery = {};
        if (m_impl != nullptr || !config.IsValid())
            return AssetFileTransactionResult::InvalidArgument;
        Impl* const impl = VANGUARD_NEW(Impl)();
        if (impl == nullptr)
            return AssetFileTransactionResult::LimitExceeded;
        impl->files = &files;
        impl->journal = config.journalPath;
        impl->journalStage = SuffixedPath(config.journalPath, ".stage");
        impl->maximumEntries = config.maximumEntries;
        impl->maximumFileBytes = config.maximumFileBytes;
        impl->maximumStagedBytes = config.maximumStagedBytes;
        impl->maximumJournalBytes = config.maximumJournalBytes;
        for (const filesystem::AbsolutePath& root : config.writableRoots)
        {
            // The probe is lexical only and is never created. It must not start
            // with '.', because AbsolutePath::AddFilePath resolves '.' as a
            // current-directory component and drops the preceding separator.
            const filesystem::AbsolutePath probe = root.AddFilePath("vtxn-root-check");
            if (!root.IsDirectoryPath() || root.IsOnlyRootPath() || !probe.IsFilePath() ||
                filesystem::ValidateMutationPath(root, probe) != filesystem::MutationPathResult::Success)
            {
                VANGUARD_DELETE(impl);
                return AssetFileTransactionResult::InvalidArgument;
            }
            for (const filesystem::AbsolutePath& previous : impl->roots)
                if (root.AsStringView().StartsWithIgnoreCase(previous.AsStringView()) ||
                    previous.AsStringView().StartsWithIgnoreCase(root.AsStringView()))
                {
                    VANGUARD_DELETE(impl);
                    return AssetFileTransactionResult::InvalidArgument;
                }
            const u32 oldRootCount = impl->roots.Size();
            impl->roots.PushBack(root);
            if (impl->roots.Size() != oldRootCount + 1u || impl->roots[oldRootCount] != root)
            {
                VANGUARD_DELETE(impl);
                return AssetFileTransactionResult::LimitExceeded;
            }
        }
        for (const filesystem::AbsolutePath& root : config.assertionRoots)
        {
            const filesystem::AbsolutePath probe = root.AddFilePath("vtxn-assert-root-check");
            if (!root.IsDirectoryPath() || root.IsOnlyRootPath() || !probe.IsFilePath() ||
                filesystem::ValidateMutationPath(root, probe) != filesystem::MutationPathResult::Success)
            {
                VANGUARD_DELETE(impl);
                return AssetFileTransactionResult::InvalidArgument;
            }
            for (const filesystem::AbsolutePath& previous : impl->assertionRoots)
                if (root.AsStringView().StartsWithIgnoreCase(previous.AsStringView()) ||
                    previous.AsStringView().StartsWithIgnoreCase(root.AsStringView()))
                {
                    VANGUARD_DELETE(impl);
                    return AssetFileTransactionResult::InvalidArgument;
                }
            const u32 oldRootCount = impl->assertionRoots.Size();
            impl->assertionRoots.PushBack(root);
            if (impl->assertionRoots.Size() != oldRootCount + 1u || impl->assertionRoots[oldRootCount] != root)
            {
                VANGUARD_DELETE(impl);
                return AssetFileTransactionResult::LimitExceeded;
            }
        }
        if (impl->roots.Size() != config.writableRoots.Count() || impl->assertionRoots.Size() != config.assertionRoots.Count() ||
            !impl->journalStage.IsFilePath() ||
            filesystem::paths::ParentAbsolutePath(impl->journalStage) != filesystem::paths::ParentAbsolutePath(impl->journal) ||
            filesystem::ValidateMutationPath(filesystem::paths::ParentAbsolutePath(impl->journal), impl->journal) !=
                filesystem::MutationPathResult::Success ||
            filesystem::ValidateMutationPath(filesystem::paths::ParentAbsolutePath(impl->journal), impl->journalStage) !=
                filesystem::MutationPathResult::Success)
        {
            VANGUARD_DELETE(impl);
            return AssetFileTransactionResult::LimitExceeded;
        }
        impl->state = AssetFileTransactionState::Ready;
        m_impl = impl;

        if (!files.FileExist(impl->journal))
        {
            if (files.FileExist(impl->journalStage) && !files.DeleteFile(impl->journalStage))
            {
                impl->state = AssetFileTransactionState::RecoveryRequired;
                return AssetFileTransactionResult::RecoveryRequired;
            }
            return AssetFileTransactionResult::Success;
        }
        if (recovery != nullptr)
            recovery->journalFound = true;
        containers::DynamicArray<u8> journalBytes(memory::pools::Assets::GetInstance());
        JournalState journalState = JournalState::Staging;
        u32 journalVersion = 0u;
        if (!ReadFile(files, impl->journal, impl->maximumJournalBytes, journalBytes) ||
            !DecodeJournal(*impl, {journalBytes.TypedData(), journalBytes.Size()}, journalState, journalVersion))
        {
            impl->state = AssetFileTransactionState::RecoveryRequired;
            return AssetFileTransactionResult::InvalidJournal;
        }
        bool recovered = false;
        if (journalState == JournalState::Staging)
        {
            recovered = true;
            for (const Mutation& mutation : impl->mutations)
                if (files.FileExist(mutation.backup))
                    recovered = false;
            if (recovered)
                recovered = CleanupOwnedFiles(*impl);
        }
        else if (journalState == JournalState::Prepared)
        {
            recovered = journalVersion >= JournalVersion ? CleanupUnappliedPrepared(*impl) : RollbackPrepared(*impl);
            if (recovery != nullptr)
                recovery->rolledBack = recovered;
        }
        else if (journalState == JournalState::Applying)
        {
            recovered = RollbackApplying(*impl);
            if (recovery != nullptr)
                recovery->rolledBack = recovered;
        }
        else
        {
            recovered = ValidateCommitted(*impl) && CleanupOwnedFiles(*impl);
            if (recovery != nullptr)
                recovery->completedCleanup = recovered;
        }
        if (!recovered || !files.DeleteFile(impl->journal))
        {
            impl->state = AssetFileTransactionState::RecoveryRequired;
            return AssetFileTransactionResult::RecoveryRequired;
        }
        ClearTransaction(*impl);
        return AssetFileTransactionResult::Success;
    }

    AssetFileTransactionResult AssetFileTransaction::Shutdown() noexcept
    {
        if (m_impl == nullptr)
            return AssetFileTransactionResult::Success;
        if (m_impl->state == AssetFileTransactionState::Prepared || m_impl->state == AssetFileTransactionState::Committed ||
            m_impl->state == AssetFileTransactionState::RecoveryRequired)
            return AssetFileTransactionResult::InvalidState;
        VANGUARD_DELETE(m_impl);
        m_impl = nullptr;
        return AssetFileTransactionResult::Success;
    }

    AssetFileTransactionState AssetFileTransaction::GetState() const noexcept
    {
        return m_impl != nullptr ? m_impl->state : AssetFileTransactionState::Closed;
    }

    AssetFileTransactionResult AssetFileTransaction::Begin() noexcept
    {
        if (m_impl == nullptr || m_impl->state != AssetFileTransactionState::Ready)
            return AssetFileTransactionResult::InvalidState;
        m_impl->mutations.Clear();
        m_impl->stagedBytes = 0u;
        m_impl->state = AssetFileTransactionState::Collecting;
        return AssetFileTransactionResult::Success;
    }

    AssetFileTransactionResult AssetFileTransaction::StageWrite(const filesystem::AbsolutePath& target,
                                                                const containers::ArraySpan<const u8> bytes) noexcept
    {
        if (m_impl == nullptr || m_impl->state != AssetFileTransactionState::Collecting)
            return AssetFileTransactionResult::InvalidState;
        return StageWriteInternal(*m_impl, target, bytes, WriteCondition::Any);
    }

    AssetFileTransactionResult AssetFileTransaction::StageCreate(const filesystem::AbsolutePath& target,
                                                                 const containers::ArraySpan<const u8> bytes) noexcept
    {
        if (m_impl == nullptr || m_impl->state != AssetFileTransactionState::Collecting)
            return AssetFileTransactionResult::InvalidState;
        return StageWriteInternal(*m_impl, target, bytes, WriteCondition::MustNotExist);
    }

    AssetFileTransactionResult AssetFileTransaction::StageWriteIfUnchanged(
        const filesystem::AbsolutePath& target, const containers::ArraySpan<const u8> expectedBytes,
        const containers::ArraySpan<const u8> replacementBytes) noexcept
    {
        if (m_impl == nullptr || m_impl->state != AssetFileTransactionState::Collecting)
            return AssetFileTransactionResult::InvalidState;
        return StageWriteInternal(*m_impl, target, replacementBytes, WriteCondition::ExpectedBytes, expectedBytes);
    }

    AssetFileTransactionResult AssetFileTransaction::StageWriteIfDigestMatches(
        const filesystem::AbsolutePath& target, const crypto::Digest256& expectedDigest,
        const containers::ArraySpan<const u8> replacementBytes) noexcept
    {
        if (m_impl == nullptr || m_impl->state != AssetFileTransactionState::Collecting)
            return AssetFileTransactionResult::InvalidState;
        return StageWriteInternal(*m_impl, target, replacementBytes, WriteCondition::ExpectedDigest, {}, &expectedDigest);
    }

    AssetFileTransactionResult AssetFileTransaction::StageAssertDigestMatches(
        const filesystem::AbsolutePath& target, const crypto::Digest256& expectedDigest) noexcept
    {
        if (m_impl == nullptr || m_impl->state != AssetFileTransactionState::Collecting)
            return AssetFileTransactionResult::InvalidState;
        return StageAssertDigestInternal(*m_impl, target, expectedDigest);
    }

    AssetFileTransactionResult AssetFileTransaction::StageMove(const filesystem::AbsolutePath& source,
                                                               const filesystem::AbsolutePath& target) noexcept
    {
        if (m_impl == nullptr || m_impl->state != AssetFileTransactionState::Collecting)
            return AssetFileTransactionResult::InvalidState;
        if (!source.IsFilePath() || !target.IsFilePath() || RootIndex(Roots(*m_impl), source) < 0 ||
            RootIndex(Roots(*m_impl), source) != RootIndex(Roots(*m_impl), target) ||
            HasPathConflict(*m_impl, source, target))
            return AssetFileTransactionResult::InvalidArgument;
        if (m_impl->mutations.Size() == m_impl->maximumEntries)
            return AssetFileTransactionResult::LimitExceeded;
        Mutation mutation;
        mutation.kind = MutationKind::Move;
        mutation.source = source;
        mutation.target = target;
        if (!DeriveOwnedPaths(mutation) || HasOwnedPathConflict(*m_impl, mutation) || !PathInRoots(Roots(*m_impl), mutation.staged) ||
            !PathInRoots(Roots(*m_impl), mutation.backup))
            return AssetFileTransactionResult::LimitExceeded;
        const u32 old = m_impl->mutations.Size();
        m_impl->mutations.PushBack(static_cast<Mutation&&>(mutation));
        return m_impl->mutations.Size() == old + 1u ? AssetFileTransactionResult::Success : AssetFileTransactionResult::LimitExceeded;
    }

    AssetFileTransactionResult AssetFileTransaction::StageDelete(const filesystem::AbsolutePath& target) noexcept
    {
        if (m_impl == nullptr || m_impl->state != AssetFileTransactionState::Collecting)
            return AssetFileTransactionResult::InvalidState;
        return StageDeleteInternal(*m_impl, target, WriteCondition::Any);
    }

    AssetFileTransactionResult AssetFileTransaction::StageDeleteIfDigestMatches(
        const filesystem::AbsolutePath& target, const crypto::Digest256& expectedDigest) noexcept
    {
        if (m_impl == nullptr || m_impl->state != AssetFileTransactionState::Collecting)
            return AssetFileTransactionResult::InvalidState;
        return StageDeleteInternal(*m_impl, target, WriteCondition::ExpectedDigest, &expectedDigest);
    }

    AssetFileTransactionResult AssetFileTransaction::Prepare() noexcept
    {
        return Prepare({});
    }

    AssetFileTransactionResult AssetFileTransaction::Prepare(const AssetFileTransactionPrepareObserver& observer) noexcept
    {
        if (m_impl == nullptr || m_impl->state != AssetFileTransactionState::Collecting || m_impl->mutations.Empty())
            return AssetFileTransactionResult::InvalidState;
        if (IsPrepareCancellationRequested(observer))
            return AssetFileTransactionResult::Cancelled;
        for (Mutation& mutation : m_impl->mutations)
        {
            if (!ValidateMutationPaths(*m_impl, mutation) ||
                (mutation.kind != MutationKind::AssertDigest &&
                 (m_impl->files->FileExist(mutation.staged) || m_impl->files->FileExist(mutation.backup))))
                return AssetFileTransactionResult::Conflict;
            mutation.targetExisted = m_impl->files->FileExist(mutation.target);
            if ((mutation.writeCondition == WriteCondition::MustNotExist && mutation.targetExisted) ||
                ((mutation.writeCondition == WriteCondition::ExpectedBytes || mutation.writeCondition == WriteCondition::ExpectedDigest) &&
                 !mutation.targetExisted))
                return AssetFileTransactionResult::Conflict;
            if ((mutation.kind == MutationKind::Move && !m_impl->files->FileExist(mutation.source)) ||
                (mutation.kind == MutationKind::Delete && !mutation.targetExisted))
                return AssetFileTransactionResult::Conflict;
            if ((mutation.kind != MutationKind::AssertDigest && mutation.targetExisted &&
                 m_impl->files->GetFileSize(mutation.target) > m_impl->maximumFileBytes) ||
                (mutation.kind == MutationKind::Move && m_impl->files->GetFileSize(mutation.source) > m_impl->maximumFileBytes))
                return AssetFileTransactionResult::LimitExceeded;
        }
        if (IsPrepareCancellationRequested(observer))
            return AssetFileTransactionResult::Cancelled;
        if (!PublishJournal(*m_impl, JournalState::Staging))
        {
            if (!CleanupOwnedFiles(*m_impl) || (m_impl->files->FileExist(m_impl->journal) && !m_impl->files->DeleteFile(m_impl->journal)))
            {
                m_impl->state = AssetFileTransactionState::RecoveryRequired;
                return AssetFileTransactionResult::RecoveryRequired;
            }
            return AssetFileTransactionResult::IoFailure;
        }
        u64 totalProgressBytes = 0u;
        for (const Mutation& mutation : m_impl->mutations)
            if (mutation.kind == MutationKind::Write)
            {
                totalProgressBytes = AddProgressBytes(totalProgressBytes, mutation.bytes.Size());
                totalProgressBytes = AddProgressBytes(totalProgressBytes, mutation.bytes.Size());
            }
        u64 completedProgressBytes = 0u;
        for (Mutation& mutation : m_impl->mutations)
            if (mutation.kind == MutationKind::Write)
            {
                const PrepareStageResult staged = WriteAndVerifyPreparedStage(
                    *m_impl->files, mutation.staged, {mutation.bytes.TypedData(), mutation.bytes.Size()}, observer,
                    completedProgressBytes, totalProgressBytes);
                if (staged == PrepareStageResult::Success)
                    continue;
                if (!CleanupOwnedFiles(*m_impl) || !m_impl->files->DeleteFile(m_impl->journal))
                {
                    m_impl->state = AssetFileTransactionState::RecoveryRequired;
                    return AssetFileTransactionResult::RecoveryRequired;
                }
                return staged == PrepareStageResult::Cancelled ? AssetFileTransactionResult::Cancelled
                                                               : AssetFileTransactionResult::IoFailure;
            }
        // Prepared is the cut after which restart treats every stage as a complete
        // unapplied transaction. Honor cancellation immediately before that
        // durable record; once publication starts, its result owns the outcome.
        if (IsPrepareCancellationRequested(observer))
        {
            if (!CleanupOwnedFiles(*m_impl) || !m_impl->files->DeleteFile(m_impl->journal))
            {
                m_impl->state = AssetFileTransactionState::RecoveryRequired;
                return AssetFileTransactionResult::RecoveryRequired;
            }
            return AssetFileTransactionResult::Cancelled;
        }
        if (!PublishJournal(*m_impl, JournalState::Prepared))
        {
            if (!CleanupOwnedFiles(*m_impl) || !m_impl->files->DeleteFile(m_impl->journal))
            {
                m_impl->state = AssetFileTransactionState::RecoveryRequired;
                return AssetFileTransactionResult::RecoveryRequired;
            }
            return AssetFileTransactionResult::IoFailure;
        }
        for (Mutation& mutation : m_impl->mutations)
            if (mutation.writeCondition == WriteCondition::Any || mutation.writeCondition == WriteCondition::ExpectedDigest)
                mutation.bytes.Clear();
        m_impl->appliedPrepared = false;
        m_impl->state = AssetFileTransactionState::Prepared;
        return AssetFileTransactionResult::Success;
    }

    AssetFileTransactionResult AssetFileTransaction::Commit(
        const containers::ArraySpan<const filesystem::AbsolutePath> trackedPaths,
        const containers::ArraySpan<AssetFileTransactionPostCondition> postConditions) noexcept
    {
        if (m_impl == nullptr || m_impl->state != AssetFileTransactionState::Prepared ||
            trackedPaths.Count() != postConditions.Count())
            return AssetFileTransactionResult::InvalidState;
        AssetFileTransactionResult result = ApplyPrepared();
        if (result != AssetFileTransactionResult::Success)
            return result;
        result = PublishCommit();
        if (result != AssetFileTransactionResult::Success)
        {
            const AssetFileTransactionResult rollback = Rollback();
            return rollback == AssetFileTransactionResult::Success ? result : rollback;
        }
        return CompleteCommit(trackedPaths, postConditions);
    }

    AssetFileTransactionResult AssetFileTransaction::ApplyPrepared() noexcept
    {
        if (m_impl == nullptr || m_impl->state != AssetFileTransactionState::Prepared || m_impl->appliedPrepared)
            return AssetFileTransactionResult::InvalidState;
        for (const Mutation& mutation : m_impl->mutations)
            if (!ValidateMutationPaths(*m_impl, mutation))
            {
                m_impl->state = AssetFileTransactionState::RecoveryRequired;
                return AssetFileTransactionResult::RecoveryRequired;
            }

        const AssetFileTransactionResult evidence = CaptureApplyingEvidence(*m_impl);
        if (evidence != AssetFileTransactionResult::Success)
        {
            if (evidence == AssetFileTransactionResult::RecoveryRequired)
            {
                m_impl->state = AssetFileTransactionState::RecoveryRequired;
                return evidence;
            }
            // No authored mutation has begun. Only transaction-owned staging
            // files may be removed; a target/source that changed after Prepare
            // belongs to the concurrent writer and must be left untouched.
            if (CleanupOwnedFiles(*m_impl) && m_impl->files->DeleteFile(m_impl->journal))
            {
                ClearTransaction(*m_impl);
                return evidence;
            }
            m_impl->state = AssetFileTransactionState::RecoveryRequired;
            return AssetFileTransactionResult::RecoveryRequired;
        }
        // Applying is a write-ahead recovery boundary. Every before/after digest
        // needed to undo an interrupted application is durable before the first
        // authored path is moved.
        if (!PublishJournal(*m_impl, JournalState::Applying))
        {
            if (CleanupOwnedFiles(*m_impl) && m_impl->files->DeleteFile(m_impl->journal))
            {
                ClearTransaction(*m_impl);
                return AssetFileTransactionResult::IoFailure;
            }
            m_impl->state = AssetFileTransactionState::RecoveryRequired;
            return AssetFileTransactionResult::RecoveryRequired;
        }

        bool applied = true;
        bool conflict = false;
        for (Mutation& mutation : m_impl->mutations)
        {
            if (mutation.kind == MutationKind::AssertDigest)
            {
                if (!m_impl->files->FileExist(mutation.target))
                {
                    conflict = true;
                    applied = false;
                    break;
                }
                const FileMatchResult match = MatchFileDigest(*m_impl->files, mutation.target, mutation.expectedDigest);
                if (match != FileMatchResult::Match)
                {
                    conflict = match == FileMatchResult::Mismatch;
                    applied = false;
                    break;
                }
                continue;
            }
            if (m_impl->files->FileExist(mutation.backup) ||
                (mutation.kind != MutationKind::Write && m_impl->files->FileExist(mutation.staged)))
            {
                // Applying was published with these transaction-owned siblings
                // absent. A new unexpected sibling is external interference;
                // preserve it and the journal rather than guessing ownership.
                m_impl->state = AssetFileTransactionState::RecoveryRequired;
                return AssetFileTransactionResult::RecoveryRequired;
            }
            const bool targetExists = m_impl->files->FileExist(mutation.target);
            if ((mutation.writeCondition == WriteCondition::MustNotExist && targetExists) ||
                ((mutation.writeCondition == WriteCondition::ExpectedBytes || mutation.writeCondition == WriteCondition::ExpectedDigest) && !targetExists))
            {
                conflict = true;
                applied = false;
                break;
            }
            if (targetExists != mutation.targetExisted)
            {
                applied = false;
                break;
            }
            if (mutation.targetExisted)
            {
                const FileMatchResult currentMatch =
                    mutation.writeCondition == WriteCondition::ExpectedDigest
                        ? MatchFileDigest(*m_impl->files, mutation.target, mutation.expectedDigest)
                        : (mutation.writeCondition == WriteCondition::ExpectedBytes
                               ? MatchFile(*m_impl->files, mutation.target,
                                           {mutation.expectedBytes.TypedData(), mutation.expectedBytes.Size()})
                               : MatchFileDigest(*m_impl->files, mutation.target, mutation.preTargetDigest));
                if (currentMatch != FileMatchResult::Match)
                {
                    conflict = (mutation.writeCondition == WriteCondition::ExpectedBytes ||
                                mutation.writeCondition == WriteCondition::ExpectedDigest) &&
                               currentMatch == FileMatchResult::Mismatch;
                    applied = false;
                    break;
                }
            }

            if (mutation.kind == MutationKind::Write)
            {
                if (!m_impl->files->FileExist(mutation.staged) ||
                    MatchFileDigest(*m_impl->files, mutation.staged, mutation.postDigest) != FileMatchResult::Match)
                {
                    applied = false;
                    break;
                }
            }
            else if (mutation.kind == MutationKind::Move)
            {
                if (!m_impl->files->FileExist(mutation.source) ||
                    MatchFileDigest(*m_impl->files, mutation.source, mutation.preSourceDigest) != FileMatchResult::Match)
                {
                    applied = false;
                    break;
                }
            }

            const bool atomicExistingWrite = mutation.targetExisted && mutation.kind == MutationKind::Write &&
                                             mutation.writeCondition == WriteCondition::Any;
            if (mutation.targetExisted && !atomicExistingWrite &&
                !filesystem::SystemIO::MoveFile(mutation.target.AsChar(), mutation.backup.AsChar()))
            {
                applied = false;
                break;
            }
            if (mutation.targetExisted && !atomicExistingWrite)
            {
                mutation.touched = true;
                const FileMatchResult movedMatch =
                    mutation.writeCondition == WriteCondition::ExpectedDigest
                        ? MatchFileDigest(*m_impl->files, mutation.backup, mutation.expectedDigest)
                        : (mutation.writeCondition == WriteCondition::ExpectedBytes
                               ? MatchFile(*m_impl->files, mutation.backup,
                                           {mutation.expectedBytes.TypedData(), mutation.expectedBytes.Size()})
                               : MatchFileDigest(*m_impl->files, mutation.backup, mutation.preTargetDigest));
                mutation.backupMatchesExpected =
                    (mutation.writeCondition == WriteCondition::ExpectedBytes ||
                     mutation.writeCondition == WriteCondition::ExpectedDigest) &&
                    movedMatch == FileMatchResult::Match;
                if (movedMatch != FileMatchResult::Match)
                {
                    conflict = (mutation.writeCondition == WriteCondition::ExpectedBytes ||
                                mutation.writeCondition == WriteCondition::ExpectedDigest) &&
                               movedMatch == FileMatchResult::Mismatch;
                    applied = false;
                    break;
                }
            }
            if (mutation.kind == MutationKind::Write)
            {
                if (atomicExistingWrite)
                {
                    applied = filesystem::ReplaceFileWithBackup(*m_impl->files, mutation.staged, mutation.target, mutation.backup);
                    if (applied)
                    {
                        mutation.touched = true;
                        applied = MatchFileDigest(*m_impl->files, mutation.backup, mutation.preTargetDigest) == FileMatchResult::Match;
                    }
                }
                else if (mutation.writeCondition == WriteCondition::Any)
                    applied = filesystem::ReplaceFile(*m_impl->files, mutation.staged, mutation.target);
                else
                {
                    const filesystem::PublishNewFileResult published =
                        filesystem::PublishNewFile(*m_impl->files, mutation.staged, mutation.target);
                    if (published == filesystem::PublishNewFileResult::Success)
                        applied = true;
                    else if (published == filesystem::PublishNewFileResult::AlreadyExists ||
                             m_impl->files->FileExist(mutation.target))
                    {
                        conflict = true;
                        applied = false;
                        mutation.conflictingTargetWon =
                            (mutation.writeCondition == WriteCondition::ExpectedBytes ||
                             mutation.writeCondition == WriteCondition::ExpectedDigest) &&
                                                        mutation.backupMatchesExpected;
                    }
                    else
                        applied = false;
                }
                if (applied)
                {
                    mutation.touched = true;
                    applied = MatchFileDigest(*m_impl->files, mutation.target, mutation.postDigest) == FileMatchResult::Match;
                }
            }
            else if (mutation.kind == MutationKind::Move)
            {
                applied = filesystem::SystemIO::MoveFile(mutation.source.AsChar(), mutation.target.AsChar());
                if (applied)
                {
                    mutation.touched = true;
                    applied = MatchFileDigest(*m_impl->files, mutation.target, mutation.preSourceDigest) == FileMatchResult::Match;
                }
            }
            if (!applied)
                break;
        }
        // Assertions are checked before mutation for the common stale case and
        // again after every authored mutation has been applied. The latter is
        // the asserted-file snapshot boundary: if a dependency/source changed
        // while the composite write was in flight, roll every touched authored
        // file back instead of committing metadata for a stale plan.
        if (applied)
        {
            for (const Mutation& mutation : m_impl->mutations)
            {
                if (mutation.kind != MutationKind::AssertDigest)
                    continue;
                if (!m_impl->files->FileExist(mutation.target))
                {
                    conflict = true;
                    applied = false;
                    break;
                }
                const FileMatchResult match = MatchFileDigest(*m_impl->files, mutation.target, mutation.expectedDigest);
                if (match != FileMatchResult::Match)
                {
                    conflict = match == FileMatchResult::Mismatch;
                    applied = false;
                    break;
                }
            }
        }
        if (!applied)
        {
            if (RollbackApplying(*m_impl) && m_impl->files->DeleteFile(m_impl->journal))
            {
                ClearTransaction(*m_impl);
                return conflict ? AssetFileTransactionResult::Conflict : AssetFileTransactionResult::IoFailure;
            }
            m_impl->state = AssetFileTransactionState::RecoveryRequired;
            return AssetFileTransactionResult::RecoveryRequired;
        }
        m_impl->appliedPrepared = true;
        return AssetFileTransactionResult::Success;
    }

    AssetFileTransactionResult AssetFileTransaction::PublishCommit() noexcept
    {
        if (m_impl == nullptr || m_impl->state != AssetFileTransactionState::Prepared || !m_impl->appliedPrepared)
            return AssetFileTransactionResult::InvalidState;
        if (!PublishJournal(*m_impl, JournalState::Committed))
            // Keep the already-applied Prepared transaction rollback-capable.
            // The caller performs Rollback() after leaving any higher-level
            // admission mutex; publication itself stays a short journal-only
            // linearization step.
            return AssetFileTransactionResult::IoFailure;
        m_impl->state = AssetFileTransactionState::Committed;
        return AssetFileTransactionResult::Success;
    }

    AssetFileTransactionResult AssetFileTransaction::CompleteCommit(
        const containers::ArraySpan<const filesystem::AbsolutePath> trackedPaths,
        const containers::ArraySpan<AssetFileTransactionPostCondition> postConditions) noexcept
    {
        if (m_impl == nullptr || m_impl->state != AssetFileTransactionState::Committed || trackedPaths.Count() != postConditions.Count())
            return AssetFileTransactionResult::InvalidState;
        for (u32 index = 0; index < postConditions.Count(); ++index)
            postConditions[index] = {};
        FillPostConditions(*m_impl, trackedPaths, postConditions);
        if (!CleanupOwnedFiles(*m_impl) || !m_impl->files->DeleteFile(m_impl->journal))
        {
            m_impl->state = AssetFileTransactionState::RecoveryRequired;
            return AssetFileTransactionResult::CommittedCleanupPending;
        }
        ClearTransaction(*m_impl);
        return AssetFileTransactionResult::Success;
    }

    AssetFileTransactionResult AssetFileTransaction::Rollback() noexcept
    {
        if (m_impl == nullptr)
            return AssetFileTransactionResult::InvalidState;
        if (m_impl->state == AssetFileTransactionState::Collecting)
        {
            ClearTransaction(*m_impl);
            return AssetFileTransactionResult::Success;
        }
        if (m_impl->state != AssetFileTransactionState::Prepared)
            return AssetFileTransactionResult::InvalidState;
        // A current-process Prepared transaction that has not entered Applying
        // has not authored any target/source path. Use the same cleanup-only
        // semantics as v5 restart recovery; legacy RollbackPrepared is retained
        // only for decoding v1-v4 journals from older binaries.
        const bool rolledBack = m_impl->appliedPrepared ? RollbackApplying(*m_impl) : CleanupUnappliedPrepared(*m_impl);
        if (!rolledBack || !m_impl->files->DeleteFile(m_impl->journal))
        {
            m_impl->state = AssetFileTransactionState::RecoveryRequired;
            return AssetFileTransactionResult::RecoveryRequired;
        }
        ClearTransaction(*m_impl);
        return AssetFileTransactionResult::Success;
    }
} // namespace vanguard::assets
