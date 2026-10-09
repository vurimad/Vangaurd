#pragma once

#include <vanguard/filesystem/filesystem.hpp>

namespace vanguard::editor::detail
{
    inline constexpr u32 MaximumEditorStateArtifactStageAttempts = 16;
    inline constexpr usize MaximumEditorStateReferenceBytes = 4096;
    inline constexpr u64 EditorStateArtifactStaleStageMinimumAgeSeconds = 24u * 60u * 60u;
    inline constexpr u32 MaximumEditorStateArtifactStaleStageDeletes = 32;

    struct EditorStateArtifactPaths final
    {
        filesystem::AbsolutePath directory;
        filesystem::AbsolutePath canonical;
        filesystem::AbsolutePath lastKnownGood;
    };

    struct EditorStateArtifactStageIdentity final
    {
        u8 nonce[16]{};
        u64 serial = 1;
    };

    struct EditorStateArtifactValidator final
    {
        using Validate = bool (*)(const void* bytes, usize byteCount, void* userData) noexcept;

        Validate validate = nullptr;
        void* userData = nullptr;

        [[nodiscard]] constexpr bool IsValid() const noexcept { return validate != nullptr; }
        [[nodiscard]] bool operator()(const void* const bytes, const usize byteCount) const noexcept
        {
            return validate != nullptr && validate(bytes, byteCount, userData);
        }
    };

    enum class EditorStateArtifactLoadResult : u8
    {
        LoadedCanonical,
        RecoveredLkg,
        Missing,
        Corrupt,
        IoFailure,
        InvalidArgument
    };

    enum class EditorStateArtifactPublishResult : u8
    {
        Published,
        InvalidArgument,
        DirectoryCreateFailed,
        StageCollisionExhausted,
        StageOpenFailed,
        StageWriteFailed,
        StageFlushFailed,
        StageValidationFailed,
        CanonicalReadFailed,
        LkgRotateFailed,
        ReplaceFailed,
        CleanupFailed,
        UnsupportedPlatform
    };

    // Loads and validates canonical first, then lastKnownGood. The caller-owned destination is
    // bounded by capacity and contains the recovered image only on LoadedCanonical/RecoveredLkg.
    [[nodiscard]] EditorStateArtifactLoadResult LoadEditorStateArtifact(
        const EditorStateArtifactPaths& paths, void* destination, usize capacity, usize& byteCount,
        const EditorStateArtifactValidator& validator) noexcept;

    // Publishes one complete validated image. A unique sibling stage is exclusive-created and
    // flushed, reread and validated, then the currently valid canonical is rotated to one LKG
    // before the stage atomically replaces canonical. Corrupt canonical data is never promoted
    // over an existing LKG. The helper owns no write-inhibition or retry policy beyond bounded
    // stage-name collision handling.
    [[nodiscard]] EditorStateArtifactPublishResult PublishEditorStateArtifact(
        const EditorStateArtifactPaths& paths, const void* bytes, usize byteCount,
        void* validationScratch, usize validationCapacity, EditorStateArtifactStageIdentity& identity,
        const EditorStateArtifactValidator& validator) noexcept;

    // Small reference artifacts use the same exclusive stage/reread/validate/atomic-replace
    // discipline but intentionally have no LKG rotation. They are suitable for indirections such
    // as latest-checkpoint.ref and last-session.ref.
    [[nodiscard]] EditorStateArtifactPublishResult PublishEditorStateReference(
        const filesystem::AbsolutePath& directory, const filesystem::AbsolutePath& canonical,
        const void* bytes, usize byteCount, void* validationScratch, usize validationCapacity,
        EditorStateArtifactStageIdentity& identity, const EditorStateArtifactValidator& validator) noexcept;

    // Best-effort startup maintenance for crash-abandoned stages created by this helper. Only
    // exact editor-state.stage-<nonce>-<serial> names older than the stale threshold are eligible;
    // the current process nonce is always skipped and deletion work is bounded per invocation.
    // Failures are intentionally not surfaced into session load/save policy.
    void CleanupStaleEditorStateStages(const filesystem::AbsolutePath& directory,
                                       const EditorStateArtifactStageIdentity& currentIdentity) noexcept;
} // namespace vanguard::editor::detail
