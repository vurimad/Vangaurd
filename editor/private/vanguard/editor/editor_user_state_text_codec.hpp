#pragma once

#include <vanguard/editor/editor_user_state_service.hpp>

namespace vanguard::editor::detail
{
    inline constexpr serialization::Version EditorUserStateTextVersion{1, 0};

    // A 512 KiB ImGui payload consisting only of newlines expands to roughly 8 MiB
    // because every newline becomes a separate `imgui.line = ""` assignment. Keep
    // one explicit text bound with enough headroom for the other bounded records,
    // escaping, and forward-compatible passthrough fields.
    inline constexpr u32 MaximumEditorUserStateTextBytes = 16u * 1024u * 1024u;
    inline constexpr u32 MaximumEditorUserStateTextUnknownRecords = 4096;
    inline constexpr u32 MaximumEditorUserStateTextUnknownBytes = 1024u * 1024u;
    inline constexpr u32 MaximumEditorUserStatePanelStateRecords = MaximumEditorUserStatePanels * 2u;
    inline constexpr u32 MaximumEditorUserStatePanelStateBytes = 2u * 1024u * 1024u;
    inline constexpr u32 EditorUserStatePanelStateChunkBytes = 512u;
    inline constexpr serialization::Version EditorNamedLayoutTextVersion{1, 0};
    inline constexpr serialization::Version EditorNamedLayoutIndexTextVersion{1, 0};
    inline constexpr u32 MaximumEditorNamedLayouts = 64;
    inline constexpr u32 MaximumEditorNamedLayoutNameBytes = 96;
    inline constexpr u32 MaximumEditorNamedLayoutIndexUnknownRecords = 256;
    inline constexpr u32 MaximumEditorNamedLayoutIndexUnknownBytes = 64u * 1024u;

    enum class EditorUserStateTextResult : u8
    {
        Success,
        InvalidArgument,
        InvalidUtf8,
        InvalidSyntax,
        UnsupportedVersion,
        LimitExceeded
    };

    struct EditorUserStateTextFailure final
    {
        EditorUserStateTextResult result = EditorUserStateTextResult::Success;
        u32 line = 0;
        u32 column = 0;
    };

    struct EditorUserStateTextUnknownAssignment final
    {
        // Offsets address EditorUserStateTextDecodedStorage::unknownBytes. Values are
        // retained in their validated encoded form (without quotes) so a future-minor
        // field survives a current-reader round trip without losing escape spelling.
        u32 keyOffset = 0;
        u32 keyByteCount = 0;
        u32 encodedValueOffset = 0;
        u32 encodedValueByteCount = 0;
        u32 ordinal = 0;
        // Panel-scoped future-minor fields carry the parent identity observed at decode time.
        // This prevents opaque type-A fields from being rebound to type B if an instance ID is
        // later reused before the document is written again.
        bool panelScoped = false;
        EditorPanelInstanceId panelInstanceId = InvalidEditorPanelInstanceId;
        EditorPanelTypeId panelTypeId = InvalidEditorPanelTypeId;
        // Named-layout-index future fields are keyed by ordinal on disk, but the ordinal is
        // only presentation syntax. Bind them to the stable layout identity at decode time so
        // CRUD/sorting cannot silently transfer opaque metadata to another saved layout.
        bool namedLayoutScoped = false;
        u64 namedLayoutId = 0;
    };

    struct EditorUserStateTextPanelStateRecord final
    {
        EditorPanelInstanceId instanceId = InvalidEditorPanelInstanceId;
        EditorPanelStateScope scope = EditorPanelStateScope::Presentation;
        u32 version = 0;
        u32 byteOffset = 0;
        u32 byteCount = 0;
    };

    struct EditorUserStateTextDocumentView final
    {
        serialization::Version version = EditorUserStateTextVersion;
        u64 checkpointSequence = 0;
        EditorUserStateDocumentView document;
        containers::ArraySpan<const EditorUserStateTextPanelStateRecord> panelStates;
        containers::ArraySpan<const u8> panelStateBytes;
        containers::ArraySpan<const EditorUserStateTextUnknownAssignment> unknownAssignments;
        containers::StringView unknownBytes;
    };

    // Large fixed decode image. Intended for Tools-pool/owner scratch storage rather
    // than routine stack allocation. Every source-backed StringView returned by
    // TextDocumentReader is copied before DecodeEditorUserStateText returns.
    struct EditorUserStateTextDecodedStorage final
    {
        serialization::Version version{};
        u64 checkpointSequence = 0;
        projects::ProjectId project;
        EditorUserStatePrimaryWindowRecord primaryWindow;

        EditorUserStatePanelRecord panels[MaximumEditorUserStatePanels]{};
        u32 panelCount = 0;
        EditorUserStateTextPanelStateRecord panelStates[MaximumEditorUserStatePanelStateRecords]{};
        u32 panelStateCount = 0;
        u8 panelStateBytes[MaximumEditorUserStatePanelStateBytes]{};
        u32 panelStateByteCount = 0;
        EditorUserStateSavedDisplay displays[MaximumEditorUserStateDisplays]{};
        u32 displayCount = 0;
        u8 imguiBytes[MaximumEditorUserStateImguiBytes]{};
        u32 imguiByteCount = 0;
        EditorUserStateShortcutRecord shortcuts[MaximumEditorUserStateShortcuts]{};
        u32 shortcutCount = 0;
        EditorUserStatePreferenceRecord preferences[MaximumEditorUserStatePreferences]{};
        u32 preferenceCount = 0;

        EditorUserStateTextUnknownAssignment unknownAssignments[MaximumEditorUserStateTextUnknownRecords]{};
        u32 unknownAssignmentCount = 0;
        char unknownBytes[MaximumEditorUserStateTextUnknownBytes]{};
        u32 unknownByteCount = 0;

        [[nodiscard]] EditorUserStateTextDocumentView View() const noexcept;
    };

    struct EditorNamedLayoutDocumentView final
    {
        serialization::Version version = EditorNamedLayoutTextVersion;
        u64 id = 0;
        EditorUserStatePrimaryWindowRecord primaryWindow;
        containers::ArraySpan<const EditorUserStatePanelRecord> panels;
        containers::ArraySpan<const EditorUserStateTextPanelStateRecord> panelStates;
        containers::ArraySpan<const u8> panelStateBytes;
        containers::ArraySpan<const EditorUserStateSavedDisplay> displays;
        containers::ArraySpan<const u8> imguiBytes;
        containers::ArraySpan<const EditorUserStateTextUnknownAssignment> unknownAssignments;
        containers::StringView unknownBytes;
    };

    struct EditorNamedLayoutTextDecodedStorage final
    {
        serialization::Version version{};
        u64 id = 0;
        EditorUserStateTextDecodedStorage projectedSession;

        [[nodiscard]] EditorNamedLayoutDocumentView View() const noexcept;
    };

    struct EditorNamedLayoutIndexRecord final
    {
        u64 id = 0;
        char name[MaximumEditorNamedLayoutNameBytes + 1u]{};
        u32 nameByteCount = 0;
    };

    struct EditorNamedLayoutIndexView final
    {
        serialization::Version version = EditorNamedLayoutIndexTextVersion;
        u64 activeId = 0;
        containers::ArraySpan<const EditorNamedLayoutIndexRecord> layouts;
        containers::ArraySpan<const EditorUserStateTextUnknownAssignment> unknownAssignments;
        containers::StringView unknownBytes;
    };

    struct EditorNamedLayoutIndexDecodedStorage final
    {
        serialization::Version version{};
        u64 activeId = 0;
        EditorNamedLayoutIndexRecord layouts[MaximumEditorNamedLayouts]{};
        u32 layoutCount = 0;
        EditorUserStateTextUnknownAssignment unknownAssignments[MaximumEditorNamedLayoutIndexUnknownRecords]{};
        u32 unknownAssignmentCount = 0;
        char unknownBytes[MaximumEditorNamedLayoutIndexUnknownBytes]{};
        u32 unknownByteCount = 0;

        [[nodiscard]] EditorNamedLayoutIndexView View() const noexcept
        {
            return {version, activeId, {layouts, layoutCount},
                    {unknownAssignments, unknownAssignmentCount}, {unknownBytes, unknownByteCount}};
        }
    };

    [[nodiscard]] EditorUserStateTextResult DecodeEditorUserStateText(
        containers::StringView source, EditorUserStateTextDecodedStorage& storage,
        EditorUserStateTextFailure* failure = nullptr) noexcept;

    [[nodiscard]] EditorUserStateTextResult EncodeEditorUserStateText(
        const EditorUserStateTextDocumentView& source, containers::String& destination,
        EditorUserStateTextFailure* failure = nullptr) noexcept;

    [[nodiscard]] EditorUserStateTextResult DecodeEditorNamedLayoutText(
        containers::StringView source, EditorNamedLayoutTextDecodedStorage& storage,
        EditorUserStateTextFailure* failure = nullptr) noexcept;
    [[nodiscard]] EditorUserStateTextResult EncodeEditorNamedLayoutText(
        const EditorNamedLayoutDocumentView& source, containers::String& destination,
        EditorUserStateTextFailure* failure = nullptr) noexcept;
    [[nodiscard]] EditorUserStateTextResult DecodeEditorNamedLayoutIndexText(
        containers::StringView source, EditorNamedLayoutIndexDecodedStorage& storage,
        EditorUserStateTextFailure* failure = nullptr) noexcept;
    [[nodiscard]] EditorUserStateTextResult EncodeEditorNamedLayoutIndexText(
        const EditorNamedLayoutIndexView& source, containers::String& destination,
        EditorUserStateTextFailure* failure = nullptr) noexcept;
} // namespace vanguard::editor::detail
