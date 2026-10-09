#include <vanguard/editor/editor_state_artifact_store.hpp>

#include <ctime>
#include <cstring>

namespace vanguard::editor::detail
{
    namespace
    {
        constexpr char StagePrefix[] = "editor-state.stage-";
        constexpr usize StagePrefixBytes = sizeof(StagePrefix) - 1u;
        constexpr usize StageNonceHexBytes = 32u;
        constexpr usize StageSerialHexBytes = 16u;
        constexpr usize StageFileNameBytes =
            StagePrefixBytes + StageNonceHexBytes + 1u + StageSerialHexBytes;
        constexpr u64 FileTimeTicksPerSecond = 10'000'000ull;
        constexpr u64 FileTimeUnixEpochOffsetSeconds = 11'644'473'600ull;

        enum class ValidatedReadResult : u8
        {
            Valid,
            Missing,
            Corrupt,
            IoFailure
        };

        [[nodiscard]] bool ValidDirectoryAndFile(const filesystem::AbsolutePath& directory,
                                                 const filesystem::AbsolutePath& file) noexcept
        {
            return !directory.Empty() && directory.IsDirectoryPath() && !file.Empty() && file.IsFilePath() &&
                   filesystem::paths::ParentAbsolutePath(file) == directory;
        }

        [[nodiscard]] bool ValidArtifactPaths(const EditorStateArtifactPaths& paths) noexcept
        {
            return ValidDirectoryAndFile(paths.directory, paths.canonical) &&
                   ValidDirectoryAndFile(paths.directory, paths.lastKnownGood) &&
                   paths.canonical != paths.lastKnownGood;
        }

        [[nodiscard]] filesystem::AbsolutePath MakeStagePath(const filesystem::AbsolutePath& directory,
                                                             const EditorStateArtifactStageIdentity& identity,
                                                             const u64 serial) noexcept
        {
            if (directory.Empty() || !directory.IsDirectoryPath() || serial == 0)
                return {};

            char name[96]{};
            char* cursor = name;
            for (usize index = 0; index < StagePrefixBytes; ++index)
                *cursor++ = StagePrefix[index];

            constexpr char hex[] = "0123456789abcdef";
            for (const u8 byte : identity.nonce)
            {
                *cursor++ = hex[byte >> 4u];
                *cursor++ = hex[byte & 0x0fu];
            }
            *cursor++ = '-';
            for (i32 shift = 60; shift >= 0; shift -= 4)
                *cursor++ = hex[(serial >> static_cast<u32>(shift)) & 0x0full];
            *cursor = '\0';
            return directory.AddFilePath(name);
        }

        [[nodiscard]] bool IsLowerHex(const char value) noexcept
        {
            return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f');
        }

        [[nodiscard]] bool IsKnownStageFileName(const char* const name) noexcept
        {
            if (name == nullptr || std::strlen(name) != StageFileNameBytes ||
                std::memcmp(name, StagePrefix, StagePrefixBytes) != 0 ||
                name[StagePrefixBytes + StageNonceHexBytes] != '-')
                return false;
            for (usize index = 0; index < StageNonceHexBytes; ++index)
                if (!IsLowerHex(name[StagePrefixBytes + index]))
                    return false;
            const usize serialOffset = StagePrefixBytes + StageNonceHexBytes + 1u;
            for (usize index = 0; index < StageSerialHexBytes; ++index)
                if (!IsLowerHex(name[serialOffset + index]))
                    return false;
            return true;
        }

        [[nodiscard]] bool MatchesStageNonce(const char* const name,
                                             const EditorStateArtifactStageIdentity& identity) noexcept
        {
            constexpr char hex[] = "0123456789abcdef";
            const char* cursor = name + StagePrefixBytes;
            for (const u8 byte : identity.nonce)
            {
                if (*cursor++ != hex[byte >> 4u] || *cursor++ != hex[byte & 0x0fu])
                    return false;
            }
            return true;
        }

        [[nodiscard]] bool CurrentFileTimeTicks(u64& ticks) noexcept
        {
            ticks = 0;
            const std::time_t now = std::time(nullptr);
            if (now <= 0)
                return false;
            const u64 unixSeconds = static_cast<u64>(now);
            constexpr u64 maximumSeconds = ~u64{0} / FileTimeTicksPerSecond;
            if (unixSeconds > maximumSeconds - FileTimeUnixEpochOffsetSeconds)
                return false;
            ticks = (unixSeconds + FileTimeUnixEpochOffsetSeconds) * FileTimeTicksPerSecond;
            return true;
        }

        [[nodiscard]] bool TakeStageSerial(EditorStateArtifactStageIdentity& identity, u64& serial) noexcept
        {
            serial = identity.serial;
            if (serial == 0)
                return false;
            identity.serial = serial == ~u64{0} ? 0 : serial + 1u;
            return true;
        }

        [[nodiscard]] bool CleanupOwnedStage(const filesystem::AbsolutePath& stage) noexcept
        {
            if (!filesystem::IsInitialized())
                return false;
            filesystem::Manager& files = filesystem::GetManager();
            return !files.FileExist(stage) || files.DeleteFile(stage);
        }

        [[nodiscard]] ValidatedReadResult ReadValidated(const filesystem::AbsolutePath& path, void* const destination,
                                                        const usize capacity, usize& byteCount,
                                                        const EditorStateArtifactValidator& validator) noexcept
        {
            byteCount = 0;
            switch (filesystem::ReadCompleteFile(path, destination, capacity, byteCount))
            {
            case filesystem::CompleteFileReadResult::Success:
                if (validator(destination, byteCount))
                    return ValidatedReadResult::Valid;
                byteCount = 0;
                return ValidatedReadResult::Corrupt;
            case filesystem::CompleteFileReadResult::Missing:
                return ValidatedReadResult::Missing;
            case filesystem::CompleteFileReadResult::LimitExceeded:
                return ValidatedReadResult::Corrupt;
            case filesystem::CompleteFileReadResult::OpenFailed:
            case filesystem::CompleteFileReadResult::ReadFailed:
            case filesystem::CompleteFileReadResult::UnsupportedPlatform:
            default:
                return ValidatedReadResult::IoFailure;
            }
        }

        [[nodiscard]] EditorStateArtifactPublishResult CleanupAfterFailure(
            const filesystem::AbsolutePath& stage, const EditorStateArtifactPublishResult primary) noexcept
        {
            return CleanupOwnedStage(stage) ? primary : EditorStateArtifactPublishResult::CleanupFailed;
        }

        [[nodiscard]] EditorStateArtifactPublishResult WriteValidatedStage(
            const filesystem::AbsolutePath& directory, const void* const bytes, const usize byteCount,
            void* const validationScratch, const usize validationCapacity, EditorStateArtifactStageIdentity& identity,
            const EditorStateArtifactValidator& validator, filesystem::AbsolutePath& stage) noexcept
        {
            stage = {};
            for (u32 attempt = 0; attempt < MaximumEditorStateArtifactStageAttempts; ++attempt)
            {
                u64 serial = 0;
                if (!TakeStageSerial(identity, serial))
                    return EditorStateArtifactPublishResult::StageCollisionExhausted;
                stage = MakeStagePath(directory, identity, serial);
                if (stage.Empty())
                    return EditorStateArtifactPublishResult::InvalidArgument;

                const filesystem::ExclusiveFileWriteResult write = filesystem::WriteExclusiveFile(stage, bytes, byteCount);
                if (write == filesystem::ExclusiveFileWriteResult::AlreadyExists)
                {
                    stage = {};
                    continue;
                }
                switch (write)
                {
                case filesystem::ExclusiveFileWriteResult::Success:
                    break;
                case filesystem::ExclusiveFileWriteResult::OpenFailed:
                    stage = {};
                    return EditorStateArtifactPublishResult::StageOpenFailed;
                case filesystem::ExclusiveFileWriteResult::WriteFailed:
                    return CleanupAfterFailure(stage, EditorStateArtifactPublishResult::StageWriteFailed);
                case filesystem::ExclusiveFileWriteResult::FlushFailed:
                    return CleanupAfterFailure(stage, EditorStateArtifactPublishResult::StageFlushFailed);
                case filesystem::ExclusiveFileWriteResult::UnsupportedPlatform:
                    stage = {};
                    return EditorStateArtifactPublishResult::UnsupportedPlatform;
                case filesystem::ExclusiveFileWriteResult::AlreadyExists:
                default:
                    stage = {};
                    return EditorStateArtifactPublishResult::StageOpenFailed;
                }

                usize rereadBytes = 0;
                if (filesystem::ReadCompleteFile(stage, validationScratch, validationCapacity, rereadBytes) !=
                        filesystem::CompleteFileReadResult::Success ||
                    rereadBytes != byteCount || !validator(validationScratch, rereadBytes))
                    return CleanupAfterFailure(stage, EditorStateArtifactPublishResult::StageValidationFailed);
                return EditorStateArtifactPublishResult::Published;
            }
            return EditorStateArtifactPublishResult::StageCollisionExhausted;
        }

        [[nodiscard]] EditorStateArtifactPublishResult ValidatePublishArguments(
            const filesystem::AbsolutePath& directory, const filesystem::AbsolutePath& canonical,
            const void* const bytes, const usize byteCount, void* const validationScratch,
            const usize validationCapacity, const EditorStateArtifactStageIdentity& identity,
            const EditorStateArtifactValidator& validator) noexcept
        {
            if (!ValidDirectoryAndFile(directory, canonical) || !validator.IsValid() || identity.serial == 0 ||
                (byteCount != 0 && bytes == nullptr) || validationCapacity < byteCount ||
                (validationCapacity != 0 && validationScratch == nullptr))
                return EditorStateArtifactPublishResult::InvalidArgument;
            if (!validator(bytes, byteCount))
                return EditorStateArtifactPublishResult::InvalidArgument;
            return EditorStateArtifactPublishResult::Published;
        }

        [[nodiscard]] bool EnsureDirectory(const filesystem::AbsolutePath& directory) noexcept
        {
            return filesystem::IsInitialized() && filesystem::GetManager().CreatePath(directory);
        }
    } // namespace

    EditorStateArtifactLoadResult LoadEditorStateArtifact(
        const EditorStateArtifactPaths& paths, void* const destination, const usize capacity, usize& byteCount,
        const EditorStateArtifactValidator& validator) noexcept
    {
        byteCount = 0;
        if (!ValidArtifactPaths(paths) || !validator.IsValid() || (capacity != 0 && destination == nullptr))
            return EditorStateArtifactLoadResult::InvalidArgument;

        usize candidateBytes = 0;
        const ValidatedReadResult canonical =
            ReadValidated(paths.canonical, destination, capacity, candidateBytes, validator);
        if (canonical == ValidatedReadResult::Valid)
        {
            byteCount = candidateBytes;
            return EditorStateArtifactLoadResult::LoadedCanonical;
        }

        const ValidatedReadResult lkg =
            ReadValidated(paths.lastKnownGood, destination, capacity, candidateBytes, validator);
        if (lkg == ValidatedReadResult::Valid)
        {
            byteCount = candidateBytes;
            return EditorStateArtifactLoadResult::RecoveredLkg;
        }

        byteCount = 0;
        if (canonical == ValidatedReadResult::Missing && lkg == ValidatedReadResult::Missing)
            return EditorStateArtifactLoadResult::Missing;
        if (canonical == ValidatedReadResult::IoFailure || lkg == ValidatedReadResult::IoFailure)
            return EditorStateArtifactLoadResult::IoFailure;
        return EditorStateArtifactLoadResult::Corrupt;
    }

    EditorStateArtifactPublishResult PublishEditorStateArtifact(
        const EditorStateArtifactPaths& paths, const void* const bytes, const usize byteCount,
        void* const validationScratch, const usize validationCapacity, EditorStateArtifactStageIdentity& identity,
        const EditorStateArtifactValidator& validator) noexcept
    {
        if (!ValidArtifactPaths(paths))
            return EditorStateArtifactPublishResult::InvalidArgument;
        const EditorStateArtifactPublishResult argument = ValidatePublishArguments(
            paths.directory, paths.canonical, bytes, byteCount, validationScratch, validationCapacity, identity, validator);
        if (argument != EditorStateArtifactPublishResult::Published)
            return argument;
        if (!EnsureDirectory(paths.directory))
            return EditorStateArtifactPublishResult::DirectoryCreateFailed;

        filesystem::AbsolutePath stage;
        const EditorStateArtifactPublishResult staged =
            WriteValidatedStage(paths.directory, bytes, byteCount, validationScratch, validationCapacity,
                                identity, validator, stage);
        if (staged != EditorStateArtifactPublishResult::Published)
            return staged;

        usize canonicalBytes = 0;
        const ValidatedReadResult canonical =
            ReadValidated(paths.canonical, validationScratch, validationCapacity, canonicalBytes, validator);
        if (canonical == ValidatedReadResult::IoFailure)
            return CleanupAfterFailure(stage, EditorStateArtifactPublishResult::CanonicalReadFailed);

        if (canonical == ValidatedReadResult::Valid && !filesystem::ReplaceFile(paths.canonical, paths.lastKnownGood))
            return CleanupAfterFailure(stage, EditorStateArtifactPublishResult::LkgRotateFailed);

        if (!filesystem::ReplaceFile(stage, paths.canonical))
            return CleanupAfterFailure(stage, EditorStateArtifactPublishResult::ReplaceFailed);

        // ReadValidated() above intentionally validates the previous canonical before rotating it
        // to LKG. Validators used by editor persistence also decode into caller-owned userData, so
        // that old-generation validation can otherwise leave the caller observing N after N+1 was
        // durably published. Re-validate the immutable source image on success so a stateful
        // validator finishes on the exact generation that now owns canonical.
        static_cast<void>(validator(bytes, byteCount));
        return EditorStateArtifactPublishResult::Published;
    }

    EditorStateArtifactPublishResult PublishEditorStateReference(
        const filesystem::AbsolutePath& directory, const filesystem::AbsolutePath& canonical,
        const void* const bytes, const usize byteCount, void* const validationScratch,
        const usize validationCapacity, EditorStateArtifactStageIdentity& identity,
        const EditorStateArtifactValidator& validator) noexcept
    {
        if (byteCount > MaximumEditorStateReferenceBytes)
            return EditorStateArtifactPublishResult::InvalidArgument;
        const EditorStateArtifactPublishResult argument = ValidatePublishArguments(
            directory, canonical, bytes, byteCount, validationScratch, validationCapacity, identity, validator);
        if (argument != EditorStateArtifactPublishResult::Published)
            return argument;
        if (!EnsureDirectory(directory))
            return EditorStateArtifactPublishResult::DirectoryCreateFailed;

        filesystem::AbsolutePath stage;
        const EditorStateArtifactPublishResult staged =
            WriteValidatedStage(directory, bytes, byteCount, validationScratch, validationCapacity,
                                identity, validator, stage);
        if (staged != EditorStateArtifactPublishResult::Published)
            return staged;
        if (!filesystem::ReplaceFile(stage, canonical))
            return CleanupAfterFailure(stage, EditorStateArtifactPublishResult::ReplaceFailed);
        return EditorStateArtifactPublishResult::Published;
    }

    void CleanupStaleEditorStateStages(const filesystem::AbsolutePath& directory,
                                       const EditorStateArtifactStageIdentity& currentIdentity) noexcept
    {
        if (!filesystem::IsInitialized() || directory.Empty() || !directory.IsDirectoryPath() ||
            currentIdentity.serial == 0)
            return;

        u64 nowTicks = 0;
        if (!CurrentFileTimeTicks(nowTicks))
            return;
        constexpr u64 staleAgeTicks =
            EditorStateArtifactStaleStageMinimumAgeSeconds * FileTimeTicksPerSecond;

        filesystem::Manager& files = filesystem::GetManager();
        const auto candidates = files.FindFiles(directory.AddFilePath("editor-state.stage-*"));
        u32 deleteAttempts = 0;
        for (u32 index = 0;
             index < candidates.Size() && deleteAttempts < MaximumEditorStateArtifactStaleStageDeletes;
             ++index)
        {
            const char* const name = candidates[index].AsChar();
            if (!IsKnownStageFileName(name) || MatchesStageNonce(name, currentIdentity))
                continue;

            const filesystem::AbsolutePath path = directory.AddFilePath(name);
            const u64 modifiedTicks = files.GetFileTimeStamp(path);
            if (modifiedTicks == 0 || modifiedTicks > nowTicks ||
                nowTicks - modifiedTicks < staleAgeTicks)
                continue;

            ++deleteAttempts;
            static_cast<void>(files.DeleteFile(path));
        }
    }
} // namespace vanguard::editor::detail
