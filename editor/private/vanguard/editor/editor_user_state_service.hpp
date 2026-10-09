#pragma once

#include <vanguard/editor/editor_service_ids.hpp>
#include <vanguard/editor/editor_commands.hpp>
#include <vanguard/editor/editor_shell_service.hpp>
#include <vanguard/editor/editor_ui_persistence.hpp>

#include <vanguard/application/engine_host.hpp>
#include <vanguard/containers/containers.hpp>
#include <vanguard/engine/frame_pipeline_service.hpp>
#include <vanguard/filesystem/filesystem.hpp>
#include <vanguard/projects/project.hpp>
#include <vanguard/serialization/serialization.hpp>

namespace vanguard::editor
{
    inline constexpr engine::FrameParticipantId EditorUserStateFrameParticipantId = 0x6564757374667201ull;

    inline constexpr u32 MaximumEditorUserStateFileBytes = 1024u * 1024u;
    inline constexpr u32 MaximumEditorUserStateImguiBytes = 512u * 1024u;
    inline constexpr u32 MaximumEditorUserStatePanels = 512;
    inline constexpr u32 MaximumEditorUserStateDisplays = 32;
    inline constexpr u32 MaximumEditorUserStateShortcuts = 2048;
    inline constexpr u32 MaximumEditorUserStatePreferences = 512;
    inline constexpr u32 MaximumEditorUserStatePanelTitleBytes = 127;
    inline constexpr u32 MaximumEditorUserStatePreferenceStringBytes = 256;

    enum class EditorUserStateServiceState : u8
    {
        Uninitialized,
        Ready,
        Running,
        Quiesced,
        Stopped,
        Failed
    };

    enum class EditorUserStateLoadResult : u8
    {
        NotAttempted,
        Missing,
        Restored,
        Migrated,
        RejectedCorrupt,
        RejectedBounds,
        RejectedProjectMismatch,
        RejectedFuture,
        IoFailure
    };

    enum class EditorUserStateSaveResult : u8
    {
        NotAttempted,
        Published,
        WriteInhibited,
        StageOpenFailed,
        StageWriteFailed,
        StageFlushFailed,
        StageValidationFailed,
        ReplaceFailed,
        CleanupFailed
    };

    enum class EditorUserStateFailureCode : u8
    {
        None,
        WrongThread,
        WrongPhase,
        AllocationFailed,
        RandomUnavailable,
        InvalidArgument,
        CorruptDocument,
        BoundExceeded,
        FutureDocument,
        ProjectMismatch,
        CanonicalIo,
        DirectoryCreateFailed,
        StageOpenFailed,
        StageWriteFailed,
        StageFlushFailed,
        StageValidationFailed,
        ReplaceFailed,
        CleanupFailed,
        GenerationExhausted
    };

    struct EditorUserStateFailure final
    {
        EditorUserStateFailureCode code = EditorUserStateFailureCode::None;
        u32 section = 0;
        u32 record = 0;
        u32 detail = 0;
    };

    struct EditorUserStateStatus final
    {
        EditorUserStateServiceState state = EditorUserStateServiceState::Uninitialized;
        EditorUserStateLoadResult load = EditorUserStateLoadResult::NotAttempted;
        EditorUserStateSaveResult save = EditorUserStateSaveResult::NotAttempted;
        serialization::Version loadedVersion{};
        u32 panelRecords = 0;
        u32 displayRecords = 0;
        u32 shortcutRecords = 0;
        u32 preferenceRecords = 0;
        u32 panelsRestored = 0;
        u32 panelsCreated = 0;
        u32 panelPlaceholders = 0;
        u32 panelsSkipped = 0;
        u32 shortcutsApplied = 0;
        u32 shortcutsOrphaned = 0;
        u32 shortcutsConflicted = 0;
        u32 shortcutsInvalid = 0;
        u32 preferencesApplied = 0;
        u32 preferencesOrphaned = 0;
        u32 preferencesInvalid = 0;
        u32 repairedPlacements = 0;
        bool writesInhibited = false;
        u64 dirtyGeneration = 0;
        u64 publishedGeneration = 0;
        u64 autosaveFailures = 0;
        u64 finalSaveFailures = 0;
        bool recoveredFromLastKnownGood = false;
        EditorUserStateFailure failure;
    };

    struct EditorUserStatePanelRecord final
    {
        u64 typeId = 0;
        u64 instanceId = 0;
        u16 fallbackTitleByteCount = 0;
        bool open = false;
        char fallbackTitle[MaximumEditorUserStatePanelTitleBytes + 1u]{};
    };

    using EditorUserStateSavedDisplay = detail::EditorUiSavedDisplay;

    struct EditorUserStatePrimaryWindowRecord final
    {
        detail::EditorUiSavedRect normalRect;
        // A dedicated reference-display snapshot gives the primary normal rectangle stable monitor
        // meaning independent of the ImGui/detached-window topology captured elsewhere in the document.
        EditorUserStateSavedDisplay referenceDisplay;
        window::WindowMode mode = window::WindowMode::Windowed;
        bool maximized = false;
        bool referenceDisplayValid = false;
        bool valid = false;

        [[nodiscard]] friend constexpr bool operator==(const EditorUserStatePrimaryWindowRecord&,
                                                       const EditorUserStatePrimaryWindowRecord&) noexcept = default;
    };

    enum class EditorUserStateShortcutMode : u8
    {
        Unbound = 1,
        Custom = 2
    };

    struct EditorUserStateShortcutRecord final
    {
        u64 contextTypeId = 0;
        u64 commandId = 0;
        u16 keyCode = 0;
        u8 modifiers = 0;
        EditorUserStateShortcutMode mode = EditorUserStateShortcutMode::Unbound;
    };

    enum class EditorUserStatePreferenceValueType : u8
    {
        Bool = 1,
        SignedInteger = 2,
        UnsignedBitMask = 3,
        Utf8String = 4
    };

    struct EditorUserStatePreferenceRecord final
    {
        u64 extensionId = 0;
        u64 providerId = 0;
        u64 fieldId = 0;
        EditorUserStatePreferenceValueType type = EditorUserStatePreferenceValueType::Bool;
        bool boolValue = false;
        i64 signedValue = 0;
        u64 unsignedValue = 0;
        u16 stringByteCount = 0;
        char stringValue[MaximumEditorUserStatePreferenceStringBytes + 1u]{};
    };

    struct EditorUserStateDocumentView final
    {
        projects::ProjectId project;
        containers::ArraySpan<const EditorUserStatePanelRecord> panels;
        containers::ArraySpan<const EditorUserStateSavedDisplay> displays;
        containers::ArraySpan<const u8> imguiBytes;
        containers::ArraySpan<const EditorUserStateShortcutRecord> shortcuts;
        containers::ArraySpan<const EditorUserStatePreferenceRecord> preferences;
        EditorUserStatePrimaryWindowRecord primaryWindow;
    };

    struct EditorUserStateEnvelopeInfo final
    {
        serialization::Version version{};
        projects::ProjectId project;
        u32 panelRecords = 0;
        u32 displayRecords = 0;
        u32 imguiByteCount = 0;
        u32 shortcutRecords = 0;
        u32 preferenceRecords = 0;
    };

    namespace detail
    {
        enum class EditorUserStateFileReadResult : u8
        {
            Success,
            Missing,
            OpenFailed,
            ReadFailed,
            LimitExceeded,
            UnsupportedPlatform
        };

        struct EditorUserStateFileOps final
        {
            using EnsureDirectory = bool (*)(const filesystem::AbsolutePath& directory, void* userData) noexcept;
            using ReadComplete = EditorUserStateFileReadResult (*)(const filesystem::AbsolutePath& path, void* destination,
                                                                   usize capacity, usize& byteCount, void* userData) noexcept;
            using WriteExclusive = filesystem::ExclusiveFileWriteResult (*)(const filesystem::AbsolutePath& path, const void* bytes,
                                                                             usize byteCount, void* userData) noexcept;
            using Replace = bool (*)(const filesystem::AbsolutePath& staged, const filesystem::AbsolutePath& target, void* userData) noexcept;
            using RemoveOwnedStage = bool (*)(const filesystem::AbsolutePath& path, void* userData) noexcept;

            EnsureDirectory ensureDirectory = nullptr;
            ReadComplete readComplete = nullptr;
            WriteExclusive writeExclusive = nullptr;
            Replace replace = nullptr;
            RemoveOwnedStage removeOwnedStage = nullptr;
            void* userData = nullptr;

            [[nodiscard]] bool IsValid() const noexcept
            {
                return ensureDirectory != nullptr && readComplete != nullptr && writeExclusive != nullptr &&
                       replace != nullptr && removeOwnedStage != nullptr;
            }
        };

        struct EditorUserStateStageIdentity final
        {
            u8 nonce[16]{};
            u64 saveSerial = 0;
        };

        using EditorUserStateMigrationDecode = EditorUserStateLoadResult (*)(const void* bytes, usize byteCount,
                                                                             const projects::ProjectId& expectedProject,
                                                                             EditorUserStateEnvelopeInfo& info,
                                                                             EditorUserStateFailure* failure,
                                                                             void* userData) noexcept;

        struct EditorUserStateMigrationDecoder final
        {
            serialization::Version sourceVersion{};
            EditorUserStateMigrationDecode decode = nullptr;
            void* userData = nullptr;
        };

        [[nodiscard]] EditorUserStateLoadResult ValidateEditorUserStateImageWithMigrations(
            const void* bytes, usize byteCount, const projects::ProjectId& expectedProject,
            containers::ArraySpan<const EditorUserStateMigrationDecoder> migrations,
            EditorUserStateEnvelopeInfo& info, EditorUserStateFailure* failure = nullptr) noexcept;

        [[nodiscard]] bool DecodeEditorUserStatePanelLayoutRestore(
            const void* bytes, usize byteCount, const projects::ProjectId& expectedProject,
            EditorPanelUserStateRecord* panels, u32 panelCapacity, u32& panelCount,
            EditorUiSavedDisplay* displays, u32 displayCapacity, u32& displayCount,
            u8* imguiBytes, u32 imguiCapacity, u32& imguiByteCount,
            EditorUserStateFailure* failure = nullptr) noexcept;

        [[nodiscard]] bool DecodeEditorUserStateRestoreState(
            const void* bytes, usize byteCount, const projects::ProjectId& expectedProject,
            EditorPanelUserStateRecord* panels, u32 panelCapacity, u32& panelCount,
            EditorUiSavedDisplay* displays, u32 displayCapacity, u32& displayCount,
            u8* imguiBytes, u32 imguiCapacity, u32& imguiByteCount,
            EditorShortcutPreferenceRecord* shortcuts, u32 shortcutCapacity, u32& shortcutCount,
            EditorTypedPreferenceRecord* preferences, u32 preferenceCapacity, u32& preferenceCount,
            EditorUserStateFailure* failure = nullptr) noexcept;

        [[nodiscard]] inline bool MergeEditorUserStateShortcutPreferences(
            const containers::ArraySpan<const EditorShortcutPreferenceRecord> live,
            const containers::ArraySpan<const EditorShortcutPreferenceRecord> restored,
            const containers::ArraySpan<const EditorShortcutPreferenceItemResult> restoredResults,
            const containers::ArraySpan<EditorUserStateShortcutRecord> destination,
            u32& count, EditorUserStateFailure* const failure = nullptr) noexcept
        {
            constexpr u32 section = serialization::MakeFourCC('S', 'H', 'C', 'T');
            count = 0;
            if (failure != nullptr)
                *failure = {};
            if (restored.Size() != restoredResults.Size() ||
                (!live.Empty() && live.Data() == nullptr) ||
                (!restored.Empty() && (restored.Data() == nullptr || restoredResults.Data() == nullptr)) ||
                (!destination.Empty() && destination.Data() == nullptr))
            {
                if (failure != nullptr)
                    *failure = {EditorUserStateFailureCode::InvalidArgument, section};
                return false;
            }

            const auto keyLess = [](const EditorShortcutPreferenceRecord& left,
                                    const EditorShortcutPreferenceRecord& right) noexcept {
                return left.contextType.value < right.contextType.value ||
                       (left.contextType == right.contextType && left.command.value < right.command.value);
            };
            const auto sameKey = [](const EditorShortcutPreferenceRecord& left,
                                    const EditorShortcutPreferenceRecord& right) noexcept {
                return left.contextType == right.contextType && left.command == right.command;
            };
            const auto validRecord = [](const EditorShortcutPreferenceRecord& record) noexcept {
                return record.contextType.IsValid() && record.command.IsValid() &&
                       (record.shortcutOverride.mode == EditorShortcutOverrideMode::Unbound ||
                        record.shortcutOverride.mode == EditorShortcutOverrideMode::Custom);
            };

            for (u32 index = 0; index < live.Size(); ++index)
            {
                if (!validRecord(live[index]) ||
                    (index != 0 && !keyLess(live[index - 1u], live[index])))
                {
                    if (failure != nullptr)
                        *failure = {EditorUserStateFailureCode::InvalidArgument, section, index};
                    return false;
                }
            }
            for (u32 index = 0; index < restored.Size(); ++index)
            {
                if (!validRecord(restored[index]) ||
                    (index != 0 && !keyLess(restored[index - 1u], restored[index])))
                {
                    if (failure != nullptr)
                        *failure = {EditorUserStateFailureCode::InvalidArgument, section, index};
                    return false;
                }
            }

            u32 orphanCount = 0;
            for (u32 index = 0; index < restoredResults.Size(); ++index)
                if (restoredResults[index] == EditorShortcutPreferenceItemResult::MissingCommand)
                    ++orphanCount;
            const u32 required = live.Size() + orphanCount;
            count = required;
            if (required > MaximumEditorUserStateShortcuts || required > destination.Size())
            {
                if (failure != nullptr)
                    *failure = {EditorUserStateFailureCode::BoundExceeded, section, required};
                return false;
            }

            u32 liveIndex = 0;
            u32 orphanIndex = 0;
            const auto skipNonOrphans = [&]() noexcept {
                while (orphanIndex < restored.Size() &&
                       restoredResults[orphanIndex] != EditorShortcutPreferenceItemResult::MissingCommand)
                    ++orphanIndex;
            };
            skipNonOrphans();
            while (liveIndex < live.Size() && orphanIndex < restored.Size())
            {
                if (sameKey(live[liveIndex], restored[orphanIndex]))
                {
                    if (failure != nullptr)
                        *failure = {EditorUserStateFailureCode::InvalidArgument, section, orphanIndex};
                    return false;
                }
                if (keyLess(live[liveIndex], restored[orphanIndex]))
                    ++liveIndex;
                else
                {
                    ++orphanIndex;
                    skipNonOrphans();
                }
            }

            const auto convert = [](const EditorShortcutPreferenceRecord& source,
                                    EditorUserStateShortcutRecord& target) noexcept {
                target = {};
                target.contextTypeId = source.contextType.value;
                target.commandId = source.command.value;
                if (source.shortcutOverride.mode == EditorShortcutOverrideMode::Unbound)
                {
                    target.mode = EditorUserStateShortcutMode::Unbound;
                    return;
                }
                target.mode = EditorUserStateShortcutMode::Custom;
                target.keyCode = static_cast<u16>(source.shortcutOverride.chord.key);
                target.modifiers = static_cast<u8>(source.shortcutOverride.chord.modifiers);
            };

            liveIndex = 0;
            orphanIndex = 0;
            u32 write = 0;
            skipNonOrphans();
            while (liveIndex < live.Size() || orphanIndex < restored.Size())
            {
                if (liveIndex >= live.Size())
                {
                    convert(restored[orphanIndex++], destination[write++]);
                    skipNonOrphans();
                }
                else if (orphanIndex >= restored.Size())
                    convert(live[liveIndex++], destination[write++]);
                else if (keyLess(live[liveIndex], restored[orphanIndex]))
                    convert(live[liveIndex++], destination[write++]);
                else
                {
                    convert(restored[orphanIndex++], destination[write++]);
                    skipNonOrphans();
                }
            }
            return true;
        }

        [[nodiscard]] inline bool MergeEditorUserStateTypedPreferences(
            const containers::ArraySpan<const EditorTypedPreferenceRecord> live,
            const containers::ArraySpan<const EditorTypedPreferenceRecord> restored,
            const containers::ArraySpan<const EditorTypedPreferenceItemResult> restoredResults,
            const containers::ArraySpan<EditorUserStatePreferenceRecord> destination,
            u32& count, EditorUserStateFailure* const failure = nullptr) noexcept
        {
            constexpr u32 section = serialization::MakeFourCC('P', 'R', 'E', 'F');
            count = 0;
            if (failure != nullptr)
                *failure = {};
            if (restored.Size() != restoredResults.Size() ||
                (!live.Empty() && live.Data() == nullptr) ||
                (!restored.Empty() && (restored.Data() == nullptr || restoredResults.Data() == nullptr)) ||
                (!destination.Empty() && destination.Data() == nullptr))
            {
                if (failure != nullptr)
                    *failure = {EditorUserStateFailureCode::InvalidArgument, section};
                return false;
            }

            const auto keyLess = [](const EditorTypedPreferenceRecord& left,
                                    const EditorTypedPreferenceRecord& right) noexcept {
                if (left.extension.value != right.extension.value)
                    return left.extension.value < right.extension.value;
                if (left.providerId != right.providerId)
                    return left.providerId < right.providerId;
                return left.fieldId < right.fieldId;
            };
            const auto sameKey = [](const EditorTypedPreferenceRecord& left,
                                    const EditorTypedPreferenceRecord& right) noexcept {
                return left.extension == right.extension && left.providerId == right.providerId &&
                       left.fieldId == right.fieldId;
            };
            const auto validRecord = [](const EditorTypedPreferenceRecord& record) noexcept {
                if (!record.extension.IsValid() || record.providerId == 0 || record.fieldId == 0)
                    return false;
                switch (record.value.type)
                {
                case EditorPreferenceValueType::Bool:
                case EditorPreferenceValueType::SignedInteger:
                case EditorPreferenceValueType::UnsignedBitMask:
                    return true;
                case EditorPreferenceValueType::Utf8String:
                    return record.value.value.string.byteCount <= MaximumEditorUserStatePreferenceStringBytes &&
                           record.value.value.string.bytes[record.value.value.string.byteCount] == '\0';
                default:
                    return false;
                }
            };

            for (u32 index = 0; index < live.Size(); ++index)
            {
                if (!validRecord(live[index]) ||
                    (index != 0 && !keyLess(live[index - 1u], live[index])))
                {
                    if (failure != nullptr)
                        *failure = {EditorUserStateFailureCode::InvalidArgument, section, index};
                    return false;
                }
            }
            for (u32 index = 0; index < restored.Size(); ++index)
            {
                if (!validRecord(restored[index]) ||
                    (index != 0 && !keyLess(restored[index - 1u], restored[index])))
                {
                    if (failure != nullptr)
                        *failure = {EditorUserStateFailureCode::InvalidArgument, section, index};
                    return false;
                }
            }

            u32 orphanCount = 0;
            for (u32 index = 0; index < restoredResults.Size(); ++index)
                if (restoredResults[index] == EditorTypedPreferenceItemResult::MissingProvider ||
                    restoredResults[index] == EditorTypedPreferenceItemResult::MissingField)
                    ++orphanCount;
            const u32 required = live.Size() + orphanCount;
            count = required;
            if (required > MaximumEditorUserStatePreferences || required > destination.Size())
            {
                if (failure != nullptr)
                    *failure = {EditorUserStateFailureCode::BoundExceeded, section, required};
                return false;
            }

            u32 liveIndex = 0;
            u32 orphanIndex = 0;
            const auto skipNonOrphans = [&]() noexcept {
                while (orphanIndex < restored.Size() &&
                       restoredResults[orphanIndex] != EditorTypedPreferenceItemResult::MissingProvider &&
                       restoredResults[orphanIndex] != EditorTypedPreferenceItemResult::MissingField)
                    ++orphanIndex;
            };
            skipNonOrphans();
            while (liveIndex < live.Size() && orphanIndex < restored.Size())
            {
                if (sameKey(live[liveIndex], restored[orphanIndex]))
                {
                    if (failure != nullptr)
                        *failure = {EditorUserStateFailureCode::InvalidArgument, section, orphanIndex};
                    return false;
                }
                if (keyLess(live[liveIndex], restored[orphanIndex]))
                    ++liveIndex;
                else
                {
                    ++orphanIndex;
                    skipNonOrphans();
                }
            }

            const auto convert = [](const EditorTypedPreferenceRecord& source,
                                    EditorUserStatePreferenceRecord& target) noexcept {
                target = {};
                target.extensionId = source.extension.value;
                target.providerId = source.providerId;
                target.fieldId = source.fieldId;
                target.type = static_cast<EditorUserStatePreferenceValueType>(source.value.type);
                switch (source.value.type)
                {
                case EditorPreferenceValueType::Bool:
                    target.boolValue = source.value.value.boolean;
                    break;
                case EditorPreferenceValueType::SignedInteger:
                    target.signedValue = source.value.value.signedInteger;
                    break;
                case EditorPreferenceValueType::UnsignedBitMask:
                    target.unsignedValue = source.value.value.unsignedBitMask;
                    break;
                case EditorPreferenceValueType::Utf8String:
                    target.stringByteCount = source.value.value.string.byteCount;
                    for (u32 index = 0; index < target.stringByteCount; ++index)
                        target.stringValue[index] = source.value.value.string.bytes[index];
                    target.stringValue[target.stringByteCount] = '\0';
                    break;
                }
            };

            liveIndex = 0;
            orphanIndex = 0;
            u32 write = 0;
            skipNonOrphans();
            while (liveIndex < live.Size() || orphanIndex < restored.Size())
            {
                if (liveIndex >= live.Size())
                {
                    convert(restored[orphanIndex++], destination[write++]);
                    skipNonOrphans();
                }
                else if (orphanIndex >= restored.Size())
                    convert(live[liveIndex++], destination[write++]);
                else if (keyLess(live[liveIndex], restored[orphanIndex]))
                    convert(live[liveIndex++], destination[write++]);
                else
                {
                    convert(restored[orphanIndex++], destination[write++]);
                    skipNonOrphans();
                }
            }
            return true;
        }

        enum class EditorUserStateRestoreOwnerStep : u8
        {
            PrimaryWindow,
            PanelInventory,
            PrivateLayout,
            Shortcuts,
            TypedPreferences
        };

        enum class EditorUserStateRestoreTransactionResult : u8
        {
            Applied,
            SemanticPreflightRejected,
            OwnerApplyFailed
        };

        template <typename SemanticPreflight, typename OwnerApply>
        [[nodiscard]] inline EditorUserStateRestoreTransactionResult ExecuteEditorUserStateRestoreTransaction(
            SemanticPreflight&& semanticPreflight, OwnerApply&& ownerApply,
            EditorUserStateRestoreOwnerStep& failedStep) noexcept
        {
            failedStep = EditorUserStateRestoreOwnerStep::PrimaryWindow;
            if (!semanticPreflight())
                return EditorUserStateRestoreTransactionResult::SemanticPreflightRejected;

            constexpr EditorUserStateRestoreOwnerStep steps[]{
                EditorUserStateRestoreOwnerStep::PrimaryWindow,
                EditorUserStateRestoreOwnerStep::PanelInventory,
                EditorUserStateRestoreOwnerStep::PrivateLayout,
                EditorUserStateRestoreOwnerStep::Shortcuts,
                EditorUserStateRestoreOwnerStep::TypedPreferences};
            for (const EditorUserStateRestoreOwnerStep step : steps)
            {
                if (!ownerApply(step))
                {
                    failedStep = step;
                    return EditorUserStateRestoreTransactionResult::OwnerApplyFailed;
                }
            }
            return EditorUserStateRestoreTransactionResult::Applied;
        }

        struct EditorUserStateDirtyTracker final
        {
            u64 panelRevision = 0;
            u64 shortcutRevision = 0;
            u64 preferenceRevision = 0;
            f64 firstDirtyRealTimeSeconds = 0.0;
            f64 lastDirtyRealTimeSeconds = 0.0;
        };

        struct EditorUserStateDirtyObservation final
        {
            u64 panelRevision = 0;
            u64 shortcutRevision = 0;
            u64 preferenceRevision = 0;
            bool layoutDirty = false;
        };

        [[nodiscard]] inline bool InstallEditorUserStateDirtyBaselines(
            EditorUserStateDirtyTracker& tracker, const EditorUserStateDirtyObservation& observation) noexcept
        {
            if (observation.panelRevision == 0 || observation.shortcutRevision == 0 ||
                observation.preferenceRevision == 0)
                return false;
            tracker.panelRevision = observation.panelRevision;
            tracker.shortcutRevision = observation.shortcutRevision;
            tracker.preferenceRevision = observation.preferenceRevision;
            return true;
        }

        [[nodiscard]] inline bool ObserveEditorUserStateDirtyState(
            EditorUserStateStatus& status, EditorUserStateDirtyTracker& tracker,
            const EditorUserStateDirtyObservation& observation, const f64 nowSeconds) noexcept
        {
            if (observation.panelRevision == 0 || observation.shortcutRevision == 0 ||
                observation.preferenceRevision == 0)
            {
                status.writesInhibited = true;
                status.failure.code = EditorUserStateFailureCode::GenerationExhausted;
                return false;
            }

            const bool changed = observation.layoutDirty ||
                                 observation.panelRevision != tracker.panelRevision ||
                                 observation.shortcutRevision != tracker.shortcutRevision ||
                                 observation.preferenceRevision != tracker.preferenceRevision;
            tracker.panelRevision = observation.panelRevision;
            tracker.shortcutRevision = observation.shortcutRevision;
            tracker.preferenceRevision = observation.preferenceRevision;
            if (!changed)
                return true;

            if (status.dirtyGeneration == ~u64{0})
            {
                status.writesInhibited = true;
                status.failure.code = EditorUserStateFailureCode::GenerationExhausted;
                return false;
            }
            if (status.dirtyGeneration == status.publishedGeneration)
                tracker.firstDirtyRealTimeSeconds = nowSeconds;
            ++status.dirtyGeneration;
            tracker.lastDirtyRealTimeSeconds = nowSeconds;
            return true;
        }

        [[nodiscard]] inline bool EditorUserStateAutosaveWindowReady(
            const EditorUserStateStatus& status, const EditorUserStateDirtyTracker& tracker,
            const f64 nowSeconds, const bool hasAutosaveFailureTime,
            const f64 lastAutosaveFailureRealTimeSeconds) noexcept
        {
            constexpr f64 quietWindowSeconds = 2.0;
            constexpr f64 periodicCheckpointSeconds = 45.0;
            const bool dirty = status.dirtyGeneration != status.publishedGeneration;
            const bool quiet = nowSeconds - tracker.lastDirtyRealTimeSeconds >= quietWindowSeconds;
            const bool checkpointDue = dirty && nowSeconds - tracker.firstDirtyRealTimeSeconds >= periodicCheckpointSeconds;
            return !status.writesInhibited && dirty && (quiet || checkpointDue) &&
                   (!hasAutosaveFailureTime || nowSeconds - lastAutosaveFailureRealTimeSeconds >= quietWindowSeconds);
        }

        [[nodiscard]] inline bool EditorUserStateAutosaveReadyAfterSettling(
            const bool windowReady, const bool layoutSettling) noexcept
        {
            return windowReady && !layoutSettling;
        }

        [[nodiscard]] inline bool ShouldInhibitEditorUserStateWritesAfterLoad(
            const EditorUserStateLoadResult result) noexcept
        {
            switch (result)
            {
            case EditorUserStateLoadResult::Missing:
            case EditorUserStateLoadResult::Restored:
            case EditorUserStateLoadResult::Migrated:
                return false;
            case EditorUserStateLoadResult::NotAttempted:
            case EditorUserStateLoadResult::RejectedCorrupt:
            case EditorUserStateLoadResult::RejectedBounds:
            case EditorUserStateLoadResult::RejectedProjectMismatch:
            case EditorUserStateLoadResult::RejectedFuture:
            case EditorUserStateLoadResult::IoFailure:
                return true;
            }
            return true;
        }

        [[nodiscard]] inline bool ShouldObserveEditorUserStateDirtySources(
            const EditorUserStateStatus& status) noexcept
        {
            return !status.writesInhibited;
        }

        [[nodiscard]] inline EditorUserStateFailureCode ValidateEditorUserStateLifecycleEntry(
            const bool mainThread, const EditorUserStateServiceState state,
            const EditorUserStateServiceState requiredState, const bool dependenciesReady = true) noexcept
        {
            if (!mainThread)
                return EditorUserStateFailureCode::WrongThread;
            if (state != requiredState || !dependenciesReady)
                return EditorUserStateFailureCode::WrongPhase;
            return EditorUserStateFailureCode::None;
        }

        [[nodiscard]] inline bool IsEditorUserStatePreferenceDocumentRejection(
            const EditorPreferenceFailureCode code) noexcept
        {
            return code == EditorPreferenceFailureCode::ValidationFailed;
        }

        [[nodiscard]] inline EditorUserStateFailureCode MapEditorUserStatePreferenceInvariantFailure(
            const EditorPreferenceFailureCode code) noexcept
        {
            switch (code)
            {
            case EditorPreferenceFailureCode::WrongThread:
                return EditorUserStateFailureCode::WrongThread;
            case EditorPreferenceFailureCode::WrongPhase:
                return EditorUserStateFailureCode::WrongPhase;
            case EditorPreferenceFailureCode::GenerationExhausted:
                return EditorUserStateFailureCode::GenerationExhausted;
            case EditorPreferenceFailureCode::None:
            case EditorPreferenceFailureCode::InvalidDescriptor:
            case EditorPreferenceFailureCode::DuplicateProvider:
            case EditorPreferenceFailureCode::CapacityExceeded:
            case EditorPreferenceFailureCode::InvalidToken:
            case EditorPreferenceFailureCode::OwnerRevoked:
            case EditorPreferenceFailureCode::CallbackFailure:
            case EditorPreferenceFailureCode::ValidationFailed:
            case EditorPreferenceFailureCode::TransactionMismatch:
                return EditorUserStateFailureCode::InvalidArgument;
            }
            return EditorUserStateFailureCode::InvalidArgument;
        }

        enum class EditorUserStateFinalPersistenceResult : u8
        {
            Published,
            WriteInhibited,
            PublishFailed
        };

        template <typename Observe, typename Publish>
        [[nodiscard]] inline EditorUserStateFinalPersistenceResult ExecuteEditorUserStateFinalPersistence(
            bool& writesInhibited, Observe&& observe, Publish&& publish) noexcept
        {
            if (!writesInhibited)
                static_cast<void>(observe());
            if (writesInhibited)
                return EditorUserStateFinalPersistenceResult::WriteInhibited;
            return publish() ? EditorUserStateFinalPersistenceResult::Published
                             : EditorUserStateFinalPersistenceResult::PublishFailed;
        }
    } // namespace detail

    [[nodiscard]] EditorUserStateLoadResult ValidateEditorUserStateImage(const void* bytes, usize byteCount,
                                                                         const projects::ProjectId& expectedProject,
                                                                         EditorUserStateEnvelopeInfo& info,
                                                                         EditorUserStateFailure* failure = nullptr) noexcept;
    [[nodiscard]] bool EncodeEditorUserStateImage(const EditorUserStateDocumentView& document, void* destination,
                                                  usize capacity, usize& byteCount,
                                                  EditorUserStateFailure* failure = nullptr) noexcept;
    [[nodiscard]] EditorUserStateLoadResult LoadEditorUserStateImage(const detail::EditorUserStateFileOps& fileOps,
                                                                     const filesystem::AbsolutePath& canonicalPath,
                                                                     const projects::ProjectId& expectedProject,
                                                                     void* destination, usize capacity, usize& byteCount,
                                                                     EditorUserStateEnvelopeInfo& info,
                                                                     EditorUserStateFailure* failure = nullptr) noexcept;
    [[nodiscard]] EditorUserStateSaveResult PublishEditorUserStateImage(const detail::EditorUserStateFileOps& fileOps,
                                                                        const filesystem::AbsolutePath& directory,
                                                                        const filesystem::AbsolutePath& canonicalPath,
                                                                        const filesystem::AbsolutePath& stagedPath,
                                                                        const EditorUserStateDocumentView& document,
                                                                        void* imageScratch, void* validationScratch,
                                                                        usize scratchCapacity,
                                                                        EditorUserStateFailure* failure = nullptr) noexcept;
    [[nodiscard]] filesystem::AbsolutePath MakeEditorUserStateStagePath(const filesystem::AbsolutePath& directory,
                                                                       const detail::EditorUserStateStageIdentity& identity) noexcept;

    class EditorUserStateService : public application::Service
    {
    public:
        ~EditorUserStateService() override = default;
        [[nodiscard]] virtual EditorUserStateStatus GetStatus() const noexcept = 0;

    protected:
        EditorUserStateService() noexcept = default;
    };

    struct EditorUserStateServiceConfig final
    {
        bool forceDefaultLayout = false;
    };

    [[nodiscard]] bool RegisterEditorUserStateService(application::EngineHost& host,
                                                      const EditorUserStateServiceConfig& config,
                                                      application::HostFailure* failure = nullptr) noexcept;
    [[nodiscard]] EditorUserStateService* FindEditorUserStateService(application::EngineHost& host) noexcept;
    [[nodiscard]] EditorUserStateService* FindEditorUserStateService(application::ServiceContext& context) noexcept;
} // namespace vanguard::editor
