#include <vanguard/assets/asset_file_transaction.hpp>

#include <vanguard/crypto/crypto.hpp>
#include <vanguard/memory/memory.hpp>

#include <algorithm>
#include <cstring>

namespace vanguard::assets
{
    namespace
    {
        constexpr u32 JournalMagic = 0x4e585456u; // VTXN
        constexpr u32 JournalVersion = 3u;
        constexpr u32 ConditionalWriteJournalVersion = 2u;
        constexpr u32 LegacyJournalVersion = 1u;
        constexpr u32 JournalHeaderBytes = 48u;

        enum class MutationKind : u8 { Write, Move, Delete };
        enum class WriteCondition : u8 { Any, MustNotExist, ExpectedBytes, ExpectedDigest };
        enum class FileMatchResult : u8 { Match, Mismatch, IoFailure };
        enum class JournalState : u32 { Staging = 1u, Prepared = 2u, Committed = 3u };

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
            crypto::Digest256 postDigest;
            crypto::Digest256 expectedDigest;
            bool targetExisted = false;
            WriteCondition writeCondition = WriteCondition::Any;
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
            crypto::Sha256Builder hash;
            return (bytes.Empty() || hash.Update(bytes.Data(), bytes.Count())) && hash.Finalize(digest);
        }

        [[nodiscard]] FileMatchResult MatchFileDigest(filesystem::Manager& files, const filesystem::AbsolutePath& path,
                                                      const crypto::Digest256& expected) noexcept
        {
            auto reader = files.CreateFileReader(path, filesystem::FOF_Buffered);
            if (!reader)
                return files.FileExist(path) ? FileMatchResult::IoFailure : FileMatchResult::Mismatch;
            crypto::Sha256Builder hash;
            u8 buffer[4096];
            u64 remaining = reader->GetSize();
            while (remaining != 0u)
            {
                const u32 count = static_cast<u32>(std::min<u64>(sizeof(buffer), remaining));
                reader->Serialize(buffer, count);
                if (reader->HasErrors() || !hash.Update(buffer, count))
                    return FileMatchResult::IoFailure;
                remaining -= count;
            }
            crypto::Digest256 actual;
            return !reader->HasErrors() && hash.Finalize(actual)
                       ? (actual == expected ? FileMatchResult::Match : FileMatchResult::Mismatch)
                       : FileMatchResult::IoFailure;
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

        Impl() noexcept : roots(memory::pools::Assets::GetInstance()), mutations(memory::pools::Assets::GetInstance()) {}

        filesystem::Manager* files = nullptr;
        filesystem::AbsolutePath journal;
        filesystem::AbsolutePath journalStage;
        containers::DynamicArray<filesystem::AbsolutePath> roots;
        containers::DynamicArray<Mutation> mutations;
        AssetFileTransactionState state = AssetFileTransactionState::Closed;
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

        [[nodiscard]] bool ValidateMutationPaths(const AssetFileTransaction::Impl& impl, const Mutation& mutation) noexcept
        {
            return PathInRoots(Roots(impl), mutation.target) && PathInRoots(Roots(impl), mutation.staged) &&
                   PathInRoots(Roots(impl), mutation.backup) &&
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

        [[nodiscard]] bool CleanupOwnedFiles(AssetFileTransaction::Impl& impl) noexcept
        {
            bool success = true;
            for (Mutation& mutation : impl.mutations)
            {
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
                                         JournalState& state) noexcept
        {
            if (bytes.Count() < JournalHeaderBytes)
                return false;
            u32 cursor = 0;
            u32 magic = 0, version = 0, encodedState = 0, count = 0;
            if (!ReadU32(bytes, cursor, magic) || !ReadU32(bytes, cursor, version) || !ReadU32(bytes, cursor, encodedState) ||
                !ReadU32(bytes, cursor, count) || magic != JournalMagic ||
                (version != LegacyJournalVersion && version != ConditionalWriteJournalVersion && version != JournalVersion) || count == 0u ||
                count > impl.maximumEntries ||
                encodedState < static_cast<u32>(JournalState::Staging) || encodedState > static_cast<u32>(JournalState::Committed))
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
                const u8 maximumCondition = version == JournalVersion ? static_cast<u8>(WriteCondition::ExpectedDigest)
                                                                       : static_cast<u8>(WriteCondition::ExpectedBytes);
                const bool validConditionForKind =
                    mutation.kind == MutationKind::Write ||
                    (mutation.kind == MutationKind::Delete &&
                     (mutation.writeCondition == WriteCondition::Any || mutation.writeCondition == WriteCondition::ExpectedDigest)) ||
                    (mutation.kind == MutationKind::Move && mutation.writeCondition == WriteCondition::Any);
                if (mutation.kind > MutationKind::Delete || !ReadPath(bytes, cursor, mutation.source, mutation.kind != MutationKind::Move) ||
                    !ReadPath(bytes, cursor, mutation.target) ||
                    targetExisted > 1u || (version == LegacyJournalVersion && encodedWriteCondition != 0u) ||
                    (version != LegacyJournalVersion && encodedWriteCondition > maximumCondition) ||
                    reserved1 != 0u || !validConditionForKind ||
                    !DeriveOwnedPaths(mutation) || HasOwnedPathConflict(impl, mutation) ||
                    !ValidateMutationPaths(impl, mutation) ||
                    (mutation.kind == MutationKind::Move && RootIndex(Roots(impl), mutation.source) != RootIndex(Roots(impl), mutation.target)) ||
                    (mutation.kind != MutationKind::Move && !mutation.source.Empty()) || HasPathConflict(impl, mutation.source, mutation.target))
                    return false;
                impl.mutations.PushBack(static_cast<Mutation&&>(mutation));
                if (impl.mutations.Size() != index + 1u)
                    return false;
            }
            if (cursor != bytes.Count())
                return false;
            state = static_cast<JournalState>(encodedState);
            return true;
        }

        void ClearTransaction(AssetFileTransaction::Impl& impl) noexcept
        {
            impl.mutations.Clear();
            impl.stagedBytes = 0u;
            impl.state = AssetFileTransactionState::Ready;
        }
    }

    bool AssetFileTransactionConfig::IsValid() const noexcept
    {
        return journalPath.IsFilePath() && !writableRoots.Empty() && writableRoots.Data() != nullptr && maximumEntries != 0u &&
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
        if (impl->roots.Size() != config.writableRoots.Count() || !impl->journalStage.IsFilePath() ||
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
        if (!ReadFile(files, impl->journal, impl->maximumJournalBytes, journalBytes) ||
            !DecodeJournal(*impl, {journalBytes.TypedData(), journalBytes.Size()}, journalState))
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
            recovered = RollbackPrepared(*impl);
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
        if (m_impl->state == AssetFileTransactionState::Prepared || m_impl->state == AssetFileTransactionState::RecoveryRequired)
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
        if (m_impl == nullptr || m_impl->state != AssetFileTransactionState::Collecting || m_impl->mutations.Empty())
            return AssetFileTransactionResult::InvalidState;
        for (Mutation& mutation : m_impl->mutations)
        {
            if (!ValidateMutationPaths(*m_impl, mutation) || m_impl->files->FileExist(mutation.staged) ||
                m_impl->files->FileExist(mutation.backup))
                return AssetFileTransactionResult::Conflict;
            mutation.targetExisted = m_impl->files->FileExist(mutation.target);
            if ((mutation.writeCondition == WriteCondition::MustNotExist && mutation.targetExisted) ||
                ((mutation.writeCondition == WriteCondition::ExpectedBytes || mutation.writeCondition == WriteCondition::ExpectedDigest) &&
                 !mutation.targetExisted))
                return AssetFileTransactionResult::Conflict;
            if ((mutation.kind == MutationKind::Move && !m_impl->files->FileExist(mutation.source)) ||
                (mutation.kind == MutationKind::Delete && !mutation.targetExisted))
                return AssetFileTransactionResult::Conflict;
            if ((mutation.targetExisted && m_impl->files->GetFileSize(mutation.target) > m_impl->maximumFileBytes) ||
                (mutation.kind == MutationKind::Move && m_impl->files->GetFileSize(mutation.source) > m_impl->maximumFileBytes))
                return AssetFileTransactionResult::LimitExceeded;
        }
        if (!PublishJournal(*m_impl, JournalState::Staging))
        {
            if (!CleanupOwnedFiles(*m_impl) || (m_impl->files->FileExist(m_impl->journal) && !m_impl->files->DeleteFile(m_impl->journal)))
            {
                m_impl->state = AssetFileTransactionState::RecoveryRequired;
                return AssetFileTransactionResult::RecoveryRequired;
            }
            return AssetFileTransactionResult::IoFailure;
        }
        for (Mutation& mutation : m_impl->mutations)
            if (mutation.kind == MutationKind::Write &&
                !WriteAndVerify(*m_impl->files, mutation.staged, {mutation.bytes.TypedData(), mutation.bytes.Size()}))
            {
                if (!CleanupOwnedFiles(*m_impl) || !m_impl->files->DeleteFile(m_impl->journal))
                {
                    m_impl->state = AssetFileTransactionState::RecoveryRequired;
                    return AssetFileTransactionResult::RecoveryRequired;
                }
                return AssetFileTransactionResult::IoFailure;
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
        m_impl->state = AssetFileTransactionState::Prepared;
        return AssetFileTransactionResult::Success;
    }

    AssetFileTransactionResult AssetFileTransaction::Commit(
        const containers::ArraySpan<const filesystem::AbsolutePath> trackedPaths,
        const containers::ArraySpan<AssetFileTransactionPostCondition> postConditions) noexcept
    {
        if (m_impl == nullptr || m_impl->state != AssetFileTransactionState::Prepared || trackedPaths.Count() != postConditions.Count())
            return AssetFileTransactionResult::InvalidState;
        for (u32 index = 0; index < postConditions.Count(); ++index)
            postConditions[index] = {};
        for (const Mutation& mutation : m_impl->mutations)
            if (!ValidateMutationPaths(*m_impl, mutation))
            {
                m_impl->state = AssetFileTransactionState::RecoveryRequired;
                return AssetFileTransactionResult::RecoveryRequired;
            }
        bool applied = true;
        bool conflict = false;
        for (Mutation& mutation : m_impl->mutations)
        {
            if (m_impl->files->FileExist(mutation.backup))
            {
                // Prepare proved this transaction-owned sibling absent. If it
                // appears before commit, ownership is ambiguous; never let the
                // replacing move overwrite it or cleanup guess which bytes win.
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
            if (targetExists != mutation.targetExisted ||
                (mutation.kind == MutationKind::Write && !m_impl->files->FileExist(mutation.staged)) ||
                (mutation.kind == MutationKind::Move && !m_impl->files->FileExist(mutation.source)))
            {
                applied = false;
                break;
            }
            if (mutation.targetExisted && !filesystem::SystemIO::MoveFile(mutation.target.AsChar(), mutation.backup.AsChar()))
            {
                applied = false;
                break;
            }
            if (mutation.targetExisted)
                mutation.touched = true;
            if ((mutation.writeCondition == WriteCondition::ExpectedBytes || mutation.writeCondition == WriteCondition::ExpectedDigest) &&
                mutation.targetExisted)
            {
                const FileMatchResult movedMatch = mutation.writeCondition == WriteCondition::ExpectedDigest
                                                       ? MatchFileDigest(*m_impl->files, mutation.backup, mutation.expectedDigest)
                                                       : MatchFile(*m_impl->files, mutation.backup,
                                                                   {mutation.expectedBytes.TypedData(), mutation.expectedBytes.Size()});
                mutation.backupMatchesExpected = movedMatch == FileMatchResult::Match;
                if (!mutation.backupMatchesExpected)
                {
                    conflict = movedMatch == FileMatchResult::Mismatch;
                    applied = false;
                    break;
                }
            }
            if (mutation.kind == MutationKind::Write)
            {
                if (mutation.writeCondition == WriteCondition::Any)
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
                    mutation.touched = true;
            }
            else if (mutation.kind == MutationKind::Move)
            {
                applied = filesystem::SystemIO::MoveFile(mutation.source.AsChar(), mutation.target.AsChar());
                if (applied)
                    mutation.touched = true;
            }
            if (!applied)
                break;
        }
        if (!applied || !PublishJournal(*m_impl, JournalState::Committed))
        {
            if (RollbackTouched(*m_impl) && m_impl->files->DeleteFile(m_impl->journal))
            {
                ClearTransaction(*m_impl);
                return conflict ? AssetFileTransactionResult::Conflict : AssetFileTransactionResult::IoFailure;
            }
            m_impl->state = AssetFileTransactionState::RecoveryRequired;
            return AssetFileTransactionResult::RecoveryRequired;
        }
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
        if (!RollbackPrepared(*m_impl) || !m_impl->files->DeleteFile(m_impl->journal))
        {
            m_impl->state = AssetFileTransactionState::RecoveryRequired;
            return AssetFileTransactionResult::RecoveryRequired;
        }
        ClearTransaction(*m_impl);
        return AssetFileTransactionResult::Success;
    }
} // namespace vanguard::assets
