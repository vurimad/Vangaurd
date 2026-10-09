#include <vanguard/editor/editor_state_artifact_store.hpp>
#include <vanguard/editor/editor_user_state_service.hpp>
#include <vanguard/editor/editor_user_state_text_codec.hpp>
#include <vanguard/editor/editor_panels.hpp>

#include <vanguard/concurrency/concurrency.hpp>
#include <vanguard/containers/containers.hpp>
#include <vanguard/crypto/crypto.hpp>
#include <vanguard/diagnostics/diagnostics.hpp>
#include <vanguard/engine/window_service.hpp>
#include <vanguard/input/input.hpp>
#include <vanguard/io/io.hpp>
#include <vanguard/memory/memory.hpp>
#include <vanguard/serialization/serialization.hpp>

#include <atomic>
#include <cstdio>
#include <cstring>

// Focused test binary compiles EditorUserStateService directly instead of linking the entire
// engine library. Mirror the production capability lookup seam so the service lifecycle path
// remains faithful without dragging unrelated engine-service dependencies into this target.
namespace vanguard::engine
{
    WindowService* FindWindowService(application::ServiceContext& context) noexcept
    {
        return static_cast<WindowService*>(context.FindCapability(WindowCapabilityId));
    }
} // namespace vanguard::engine

namespace
{
    namespace editor = vanguard::editor;
    namespace filesystem = vanguard::filesystem;
    namespace input = vanguard::input;
    namespace projects = vanguard::projects;
    namespace containers = vanguard::containers;
    namespace serialization = vanguard::serialization;
    using namespace vanguard;

    alignas(16) u8 g_image[editor::MaximumEditorUserStateFileBytes]{};
    alignas(16) u8 g_secondImage[editor::MaximumEditorUserStateFileBytes]{};
    alignas(16) u8 g_validation[editor::MaximumEditorUserStateFileBytes]{};
    editor::EditorUserStatePanelRecord g_panelOverflow[editor::MaximumEditorUserStatePanels + 1u]{};
    editor::EditorUserStateSavedDisplay g_displayOverflow[editor::MaximumEditorUserStateDisplays + 1u]{};
    editor::EditorUserStateShortcutRecord g_shortcutOverflow[editor::MaximumEditorUserStateShortcuts + 1u]{};
    editor::EditorUserStatePreferenceRecord g_preferenceOverflow[editor::MaximumEditorUserStatePreferences + 1u]{};
    u8 g_imguiOverflow[editor::MaximumEditorUserStateImguiBytes + 1u]{};
    editor::detail::EditorPanelUserStateRecord g_restorePanels[editor::MaximumEditorUserStatePanels]{};
    editor::detail::EditorUiSavedDisplay g_restoreDisplays[editor::MaximumEditorUserStateDisplays]{};
    u8 g_restoreImgui[editor::MaximumEditorUserStateImguiBytes]{};
    editor::EditorShortcutPreferenceRecord g_restoreShortcuts[editor::MaximumEditorUserStateShortcuts]{};
    editor::EditorTypedPreferenceRecord g_restorePreferences[editor::MaximumEditorUserStatePreferences]{};
    int g_failures = 0;

    void Check(const bool condition, const char* const message) noexcept
    {
        if (!condition)
        {
            std::fprintf(stderr, "[editorUserStateTests] FAILED: %s\n", message);
            ++g_failures;
        }
    }

    void StoreU32(u8* const destination, const u32 value) noexcept
    {
        destination[0] = static_cast<u8>(value);
        destination[1] = static_cast<u8>(value >> 8u);
        destination[2] = static_cast<u8>(value >> 16u);
        destination[3] = static_cast<u8>(value >> 24u);
    }

    void StoreU16(u8* const destination, const u16 value) noexcept
    {
        destination[0] = static_cast<u8>(value);
        destination[1] = static_cast<u8>(value >> 8u);
    }

    [[nodiscard]] u16 LoadU16(const u8* const source) noexcept
    {
        return static_cast<u16>(source[0]) | static_cast<u16>(static_cast<u16>(source[1]) << 8u);
    }

    [[nodiscard]] u32 LoadU32(const u8* const source) noexcept
    {
        return static_cast<u32>(source[0]) | (static_cast<u32>(source[1]) << 8u) |
               (static_cast<u32>(source[2]) << 16u) | (static_cast<u32>(source[3]) << 24u);
    }

    [[nodiscard]] u64 LoadU64(const u8* const source) noexcept
    {
        return static_cast<u64>(LoadU32(source)) | (static_cast<u64>(LoadU32(source + 4u)) << 32u);
    }

    void StoreU64(u8* const destination, const u64 value) noexcept
    {
        StoreU32(destination, static_cast<u32>(value));
        StoreU32(destination + 4u, static_cast<u32>(value >> 32u));
    }

    editor::detail::EditorUiSavedDisplay SavedDisplay(const i32 x, const i32 y, const u32 width, const u32 height,
                                                      const i32 workX, const i32 workY, const u32 workWidth,
                                                      const u32 workHeight, const u32 pixelWidth, const u32 pixelHeight,
                                                      const u32 scaleQ16_16, const bool primary) noexcept
    {
        editor::detail::EditorUiSavedDisplay display;
        display.boundsX = x;
        display.boundsY = y;
        display.boundsWidth = width;
        display.boundsHeight = height;
        display.workX = workX;
        display.workY = workY;
        display.workWidth = workWidth;
        display.workHeight = workHeight;
        display.desktopPixelWidth = pixelWidth;
        display.desktopPixelHeight = pixelHeight;
        display.scaleQ16_16 = scaleQ16_16;
        display.primary = primary;
        return display;
    }

    window::DisplaySnapshot CurrentDisplay(const u32 index, const i32 x, const i32 y, const u32 width,
                                           const u32 height, const i32 workX, const i32 workY,
                                           const u32 workWidth, const u32 workHeight, const u32 pixelWidth,
                                           const u32 pixelHeight, const f32 scale, const bool primary,
                                           const u64 fingerprint) noexcept
    {
        window::DisplaySnapshot display;
        display.handle = {index, 1};
        display.fingerprint = fingerprint;
        display.bounds = {{x, y}, {width, height}};
        display.workArea = {{workX, workY}, {workWidth, workHeight}};
        display.desktopPixelExtent = {pixelWidth, pixelHeight};
        display.desktopRefreshRate = {60, 1};
        display.contentScale = scale;
        display.topologyRevision = 77;
        display.primary = primary;
        return display;
    }

    editor::detail::EditorPanelUserStateRecord PanelStateRecord(
        const editor::EditorPanelTypeId type, const editor::EditorPanelInstanceId instance,
        const char* const title, const bool open) noexcept
    {
        editor::detail::EditorPanelUserStateRecord record;
        record.type = type;
        record.instance = instance;
        record.open = open;
        while (title[record.fallbackTitleByteCount] != '\0' &&
               record.fallbackTitleByteCount + 1u < editor::MaximumEditorPanelNameBytes)
        {
            record.fallbackTitle[record.fallbackTitleByteCount] = title[record.fallbackTitleByteCount];
            ++record.fallbackTitleByteCount;
        }
        record.fallbackTitle[record.fallbackTitleByteCount] = '\0';
        return record;
    }

    void DrawFixturePanel(editor::EditorPanelContext&, void*, void*) noexcept {}

    editor::EditorPanelTypeDesc MultiplePanelType(const editor::EditorPanelTypeId id, const char* const name) noexcept
    {
        editor::EditorPanelTypeDesc desc;
        desc.id = id;
        desc.name = name;
        desc.draw = DrawFixturePanel;
        desc.policy = editor::EditorPanelTypePolicy::Multiple;
        return desc;
    }

    editor::EditorPanelTypeDesc SingletonPanelType(const editor::EditorPanelTypeId id, const char* const name,
                                                  const editor::EditorPanelInstanceId instance) noexcept
    {
        editor::EditorPanelTypeDesc desc = MultiplePanelType(id, name);
        desc.policy = editor::EditorPanelTypePolicy::Singleton;
        desc.defaultInstance = instance;
        return desc;
    }

    struct LocalPanelGenerations final
    {
        std::atomic<u32> types{0};
        std::atomic<u32> instances{0};

        [[nodiscard]] editor::detail::EditorPanelGenerationSource Get() noexcept
        {
            return {&types, &instances};
        }
    };

    void RefreshHeaderAndDigest(u8* const image, const usize byteCount) noexcept
    {
        StoreU32(image + 36u, serialization::Crc32(image, 36u));
        const usize digestOffset = byteCount - crypto::Digest256::ByteCount;
        const crypto::Digest256 digest = crypto::Sha256(image, digestOffset);
        std::memcpy(image + digestOffset, digest.bytes, sizeof(digest.bytes));
    }

    void RefreshSectionCrcAndDigest(u8* const image, const usize byteCount, const u32 sectionIndex) noexcept
    {
        const u64 tableOffset = LoadU64(image + 24u);
        u8* const descriptor = image + tableOffset + static_cast<u64>(sectionIndex) * serialization::SectionDescriptor::WireSize;
        const u64 sectionOffset = LoadU64(descriptor + 16u);
        const u64 sectionBytes = LoadU64(descriptor + 24u);
        StoreU64(descriptor + 40u, serialization::Crc64(image + sectionOffset, static_cast<usize>(sectionBytes)));
        RefreshHeaderAndDigest(image, byteCount);
    }

    editor::EditorUserStateLoadResult DecodeKnownOldFixture(const void* const bytes, const usize byteCount,
                                                            const projects::ProjectId& expectedProject,
                                                            editor::EditorUserStateEnvelopeInfo& info,
                                                            editor::EditorUserStateFailure* const failure,
                                                            void*) noexcept
    {
        if (bytes == nullptr || byteCount > sizeof(g_validation))
            return editor::EditorUserStateLoadResult::RejectedBounds;
        std::memcpy(g_validation, bytes, byteCount);
        StoreU16(g_validation + 8u, 1u);
        StoreU16(g_validation + 10u, 0u);
        RefreshHeaderAndDigest(g_validation, byteCount);
        if (editor::ValidateEditorUserStateImage(g_validation, byteCount, expectedProject, info, failure) !=
            editor::EditorUserStateLoadResult::Restored)
            return editor::EditorUserStateLoadResult::RejectedCorrupt;
        return editor::EditorUserStateLoadResult::Migrated;
    }

    struct DocumentFixture final
    {
        projects::ProjectId project{{0x11111111u, 0x22222222u, 0x33333333u, 0x44444444u}};
        editor::EditorUserStatePanelRecord panels[2]{};
        editor::EditorUserStateSavedDisplay displays[1]{};
        editor::EditorUserStateShortcutRecord shortcuts[2]{};
        editor::EditorUserStatePreferenceRecord preferences[2]{};
        u8 imgui[5]{'I', 'M', 'G', 'U', 'I'};

        DocumentFixture() noexcept
        {
            panels[0].typeId = 20;
            panels[0].instanceId = 200;
            panels[0].fallbackTitleByteCount = 4;
            panels[0].open = true;
            std::memcpy(panels[0].fallbackTitle, "Beta", 4);
            panels[1].typeId = 10;
            panels[1].instanceId = 100;
            panels[1].fallbackTitleByteCount = 5;
            std::memcpy(panels[1].fallbackTitle, "Alpha", 5);

            displays[0].boundsX = -1920;
            displays[0].boundsY = 0;
            displays[0].boundsWidth = 1920;
            displays[0].boundsHeight = 1080;
            displays[0].workX = -1920;
            displays[0].workY = 0;
            displays[0].workWidth = 1920;
            displays[0].workHeight = 1040;
            displays[0].desktopPixelWidth = 1920;
            displays[0].desktopPixelHeight = 1080;
            displays[0].scaleQ16_16 = 0x00010000u;
            displays[0].primary = true;

            shortcuts[0].contextTypeId = 8;
            shortcuts[0].commandId = 7;
            shortcuts[0].mode = editor::EditorUserStateShortcutMode::Unbound;
            shortcuts[1].contextTypeId = 3;
            shortcuts[1].commandId = 9;
            shortcuts[1].keyCode = 4;
            shortcuts[1].modifiers = 1;
            shortcuts[1].mode = editor::EditorUserStateShortcutMode::Custom;

            preferences[0].extensionId = 30;
            preferences[0].providerId = 40;
            preferences[0].fieldId = 50;
            preferences[0].type = editor::EditorUserStatePreferenceValueType::Utf8String;
            preferences[0].stringByteCount = 4;
            std::memcpy(preferences[0].stringValue, "text", 4);
            preferences[1].extensionId = 10;
            preferences[1].providerId = 20;
            preferences[1].fieldId = 30;
            preferences[1].type = editor::EditorUserStatePreferenceValueType::Bool;
            preferences[1].boolValue = true;
        }

        [[nodiscard]] editor::EditorUserStateDocumentView View() const noexcept
        {
            editor::EditorUserStateDocumentView view;
            view.project = project;
            view.panels = {panels, 2};
            view.displays = {displays, 1};
            view.imguiBytes = {imgui, 5};
            view.shortcuts = {shortcuts, 2};
            view.preferences = {preferences, 2};
            return view;
        }
    };

    void TestTextCodec() noexcept
    {
        DocumentFixture fixture;
        editor::detail::EditorUserStateTextDocumentView source;
        source.version = {1, 7};
        source.checkpointSequence = 42;
        source.document = fixture.View();
        source.document.primaryWindow.normalRect = {120, 80, 1440, 900};
        source.document.primaryWindow.referenceDisplay = {
            0, 0, 1920, 1080, 0, 0, 1920, 1040, 1920, 1080, 0x00018000u, true};
        source.document.primaryWindow.mode = window::WindowMode::Windowed;
        source.document.primaryWindow.maximized = true;
        source.document.primaryWindow.referenceDisplayValid = true;
        source.document.primaryWindow.valid = true;

        u8 panelStateBytes[526]{};
        panelStateBytes[0] = 0x00u;
        panelStateBytes[1] = 0xffu;
        panelStateBytes[2] = 0x42u;
        for (u32 index = 3; index < static_cast<u32>(sizeof(panelStateBytes)); ++index)
            panelStateBytes[index] = static_cast<u8>((index * 37u) & 0xffu);
        editor::detail::EditorUserStateTextPanelStateRecord panelStates[2]{};
        // Deliberately provide reverse identity order; canonical text must sort by instance/scope.
        panelStates[0] = {200, editor::EditorPanelStateScope::Content, 3, 0, 3};
        panelStates[1] = {100, editor::EditorPanelStateScope::Presentation, 9, 3, 523};
        source.panelStates = {panelStates, 2};
        source.panelStateBytes = {panelStateBytes, static_cast<u32>(sizeof(panelStateBytes))};

        containers::String encoded;
        editor::detail::EditorUserStateTextFailure failure;
        Check(editor::detail::EncodeEditorUserStateText(source, encoded, &failure) ==
                  editor::detail::EditorUserStateTextResult::Success,
              "vuserstate text encoder accepts same-major future-minor documents with checkpoint sequence");
        constexpr char FutureAssignment[] = "future.example = \"a\\\\tb\"\n";
        encoded.Append(FutureAssignment, sizeof(FutureAssignment) - 1u);

        memory::MemoryBlock storageBlock =
            memory::Allocate(memory::PoolId::Tools,
                             sizeof(editor::detail::EditorUserStateTextDecodedStorage),
                             alignof(editor::detail::EditorUserStateTextDecodedStorage));
        Check(static_cast<bool>(storageBlock), "vuserstate text decode scratch allocation succeeds");
        if (!storageBlock)
            return;
        auto* const decoded = ::new (storageBlock.address) editor::detail::EditorUserStateTextDecodedStorage();
        Check(editor::detail::DecodeEditorUserStateText(
                  {encoded.AsChar(), encoded.Length()}, *decoded, &failure) ==
                  editor::detail::EditorUserStateTextResult::Success &&
                  decoded->version == serialization::Version{1, 7} &&
                  decoded->checkpointSequence == 42 && decoded->unknownAssignmentCount == 1 &&
                  decoded->panelStateCount == 2 && decoded->panelStateByteCount == sizeof(panelStateBytes),
              "vuserstate decode preserves future minor, checkpoint sequence, unknown assignment, and opaque panel payloads");
        Check(decoded->primaryWindow == source.document.primaryWindow,
              "vuserstate primary window round-trip preserves normal rect, mode, maximize state, and reference display");
        Check(decoded->panelStates[0].instanceId == 100 &&
                  decoded->panelStates[0].scope == editor::EditorPanelStateScope::Presentation &&
                  decoded->panelStates[0].version == 9 && decoded->panelStates[0].byteCount == 523 &&
                  std::memcmp(decoded->panelStateBytes + decoded->panelStates[0].byteOffset,
                              panelStateBytes + 3, 523) == 0 &&
                  decoded->panelStates[1].instanceId == 200 &&
                  decoded->panelStates[1].scope == editor::EditorPanelStateScope::Content &&
                  decoded->panelStates[1].version == 3 && decoded->panelStates[1].byteCount == 3 &&
                  std::memcmp(decoded->panelStateBytes + decoded->panelStates[1].byteOffset,
                              panelStateBytes, 3) == 0,
              "vuserstate opaque panel payloads round-trip exact binary bytes, versions, scopes, and deterministic identity order");
        Check(std::strstr(encoded.AsChar(), "panel.0000000000000064.state.presentation.version = \"9\"") != nullptr &&
                  std::strstr(encoded.AsChar(), "panel.00000000000000c8.state.content.version = \"3\"") != nullptr,
              "vuserstate panel payload text uses deterministic hierarchical presentation/content keys");

        containers::String legacyPrimaryText;
        const char* cursor = encoded.AsChar();
        const char* const end = cursor + encoded.Length();
        while (cursor < end)
        {
            const char* lineEnd = static_cast<const char*>(std::memchr(cursor, '\n', static_cast<usize>(end - cursor)));
            const char* const contentEnd = lineEnd != nullptr ? lineEnd : end;
            const containers::StringView line(cursor, static_cast<u32>(contentEnd - cursor));
            if (!line.StartsWith("window.primary.display."))
            {
                legacyPrimaryText.Append(cursor, static_cast<u32>(contentEnd - cursor));
                if (lineEnd != nullptr)
                    legacyPrimaryText.Append('\n');
            }
            cursor = lineEnd != nullptr ? lineEnd + 1 : end;
        }
        memory::MemoryBlock legacyStorageBlock =
            memory::Allocate(memory::PoolId::Tools,
                             sizeof(editor::detail::EditorUserStateTextDecodedStorage),
                             alignof(editor::detail::EditorUserStateTextDecodedStorage));
        Check(static_cast<bool>(legacyStorageBlock), "vuserstate legacy text decode scratch allocation succeeds");
        if (legacyStorageBlock)
        {
            auto* const legacyDecoded =
                ::new (legacyStorageBlock.address) editor::detail::EditorUserStateTextDecodedStorage();
            Check(editor::detail::DecodeEditorUserStateText(
                      {legacyPrimaryText.AsChar(), legacyPrimaryText.Length()}, *legacyDecoded, &failure) ==
                      editor::detail::EditorUserStateTextResult::Success &&
                      legacyDecoded->primaryWindow.valid && legacyDecoded->primaryWindow.referenceDisplayValid &&
                      legacyDecoded->primaryWindow.referenceDisplay == fixture.displays[0],
                  "vuserstate 1.0 legacy six-field primary migrates its reference display from saved topology");
            legacyDecoded->~EditorUserStateTextDecodedStorage();
            memory::Free(legacyStorageBlock);
        }

        containers::String roundTrip;
        Check(editor::detail::EncodeEditorUserStateText(decoded->View(), roundTrip, &failure) ==
                  editor::detail::EditorUserStateTextResult::Success &&
                  std::strstr(roundTrip.AsChar(), "future.example = \"a\\\\tb\"") != nullptr &&
                  std::strstr(roundTrip.AsChar(), "panel.0000000000000064.state.presentation.version = \"9\"") != nullptr,
              "vuserstate unknown assignments and opaque panel payloads survive decode/re-encode together");

        decoded->~EditorUserStateTextDecodedStorage();
        memory::Free(storageBlock);
    }

    void TestNamedLayoutTextCodec() noexcept
    {
        DocumentFixture fixture;
        fixture.panels[0].open = true;
        fixture.panels[1].open = false;

        const u8 stateBytes[]{0x11u, 0x22u, 0x33u, 0x44u, 0xdeu, 0xadu, 0xbeu, 0xefu};
        const editor::detail::EditorUserStateTextPanelStateRecord states[]{
            {fixture.panels[0].instanceId, editor::EditorPanelStateScope::Presentation, 7, 0, 4}};
        editor::detail::EditorNamedLayoutDocumentView source;
        source.version = editor::detail::EditorNamedLayoutTextVersion;
        source.id = 0x1122334455667788ull;
        source.primaryWindow.normalRect = {40, 50, 1280, 720};
        source.primaryWindow.referenceDisplay = fixture.displays[0];
        source.primaryWindow.referenceDisplayValid = true;
        source.primaryWindow.valid = true;
        source.panels = {fixture.panels, 2};
        source.panelStates = states;
        source.panelStateBytes = stateBytes;
        source.displays = {fixture.displays, 1};
        source.imguiBytes = {fixture.imgui, 5};

        containers::String encoded;
        editor::detail::EditorUserStateTextFailure failure;
        Check(editor::detail::EncodeEditorNamedLayoutText(source, encoded, &failure) ==
                  editor::detail::EditorUserStateTextResult::Success,
              "named-layout text encoder accepts presentation-only state");
        Check(std::strstr(encoded.AsChar(), "project.id") == nullptr &&
                  std::strstr(encoded.AsChar(), "session.checkpoint.sequence") == nullptr &&
                  std::strstr(encoded.AsChar(), ".state.content") == nullptr &&
                  std::strstr(encoded.AsChar(), "shortcut.") == nullptr &&
                  std::strstr(encoded.AsChar(), "preference.") == nullptr &&
                  std::strstr(encoded.AsChar(), "deadbeef") == nullptr,
              "named-layout wire excludes project/session/content/preference data and unreferenced content bytes");

        memory::MemoryBlock decodedBlock = memory::Allocate(
            memory::PoolId::Tools, sizeof(editor::detail::EditorNamedLayoutTextDecodedStorage),
            alignof(editor::detail::EditorNamedLayoutTextDecodedStorage));
        Check(static_cast<bool>(decodedBlock), "named-layout decode scratch allocation succeeds");
        if (decodedBlock)
        {
            auto* const decoded =
                ::new (decodedBlock.address) editor::detail::EditorNamedLayoutTextDecodedStorage();
            Check(editor::detail::DecodeEditorNamedLayoutText(
                      {encoded.AsChar(), encoded.Length()}, *decoded, &failure) ==
                      editor::detail::EditorUserStateTextResult::Success &&
                      decoded->id == source.id && decoded->projectedSession.panelStateCount == 1 &&
                      decoded->projectedSession.panelStates[0].scope ==
                          editor::EditorPanelStateScope::Presentation &&
                      decoded->projectedSession.panelStates[0].byteCount == 4 &&
                      std::memcmp(decoded->projectedSession.panelStateBytes, stateBytes, 4) == 0,
                  "named-layout decode round-trips stable ID and exact presentation payload only");
            decoded->~EditorNamedLayoutTextDecodedStorage();
            memory::Free(decodedBlock);
        }

        editor::detail::EditorNamedLayoutDocumentView futureSource = source;
        futureSource.version = {editor::detail::EditorNamedLayoutTextVersion.major, 7};
        containers::String futureText;
        Check(editor::detail::EncodeEditorNamedLayoutText(futureSource, futureText, &failure) ==
                  editor::detail::EditorUserStateTextResult::Success,
              "named-layout encoder accepts same-major future-minor projection");
        futureText.Append("future.layout.mode = \"wide\"\n", 28u);
        memory::MemoryBlock futureBlock = memory::Allocate(
            memory::PoolId::Tools, sizeof(editor::detail::EditorNamedLayoutTextDecodedStorage),
            alignof(editor::detail::EditorNamedLayoutTextDecodedStorage));
        Check(static_cast<bool>(futureBlock), "named-layout future-minor decode scratch allocation succeeds");
        if (futureBlock)
        {
            auto* const futureDecoded =
                ::new (futureBlock.address) editor::detail::EditorNamedLayoutTextDecodedStorage();
            containers::String futureRoundTrip;
            Check(editor::detail::DecodeEditorNamedLayoutText(
                      {futureText.AsChar(), futureText.Length()}, *futureDecoded, &failure) ==
                      editor::detail::EditorUserStateTextResult::Success &&
                      futureDecoded->version.minor == 7 &&
                      editor::detail::EncodeEditorNamedLayoutText(
                          futureDecoded->View(), futureRoundTrip, &failure) ==
                          editor::detail::EditorUserStateTextResult::Success &&
                      std::strstr(futureRoundTrip.AsChar(), "future.layout.mode = \"wide\"") != nullptr,
                  "named-layout same-major future assignments survive decode/re-encode for downgrade-safe explicit save");
            futureDecoded->~EditorNamedLayoutTextDecodedStorage();
            memory::Free(futureBlock);
        }

        editor::detail::EditorUserStateTextPanelStateRecord contentState = states[0];
        contentState.scope = editor::EditorPanelStateScope::Content;
        source.panelStates = {&contentState, 1};
        Check(editor::detail::EncodeEditorNamedLayoutText(source, encoded, &failure) ==
                  editor::detail::EditorUserStateTextResult::InvalidArgument,
              "named-layout encoder rejects Content scope even when the payload would otherwise be valid");

        editor::detail::EditorNamedLayoutIndexRecord layouts[2]{};
        layouts[0].id = 0x22;
        std::memcpy(layouts[0].name, "Work", 5);
        layouts[0].nameByteCount = 4;
        layouts[1].id = 0x11;
        std::memcpy(layouts[1].name, "Debug", 6);
        layouts[1].nameByteCount = 5;
        containers::String indexText;
        const editor::detail::EditorNamedLayoutIndexView indexSource{
            editor::detail::EditorNamedLayoutIndexTextVersion, 0x22, layouts};
        Check(editor::detail::EncodeEditorNamedLayoutIndexText(indexSource, indexText, &failure) ==
                  editor::detail::EditorUserStateTextResult::Success,
              "named-layout index encodes stable IDs independently from display names");
        editor::detail::EditorNamedLayoutIndexDecodedStorage decodedIndex;
        Check(editor::detail::DecodeEditorNamedLayoutIndexText(
                  {indexText.AsChar(), indexText.Length()}, decodedIndex, &failure) ==
                  editor::detail::EditorUserStateTextResult::Success &&
                  decodedIndex.activeId == 0x22 && decodedIndex.layoutCount == 2 &&
                  decodedIndex.layouts[0].id == 0x11 && decodedIndex.layouts[1].id == 0x22,
              "named-layout index canonicalizes order while preserving active stable ID");

        decodedIndex.version = {editor::detail::EditorNamedLayoutIndexTextVersion.major, 7};
        containers::String futureIndexText;
        Check(editor::detail::EncodeEditorNamedLayoutIndexText(decodedIndex.View(), futureIndexText, &failure) ==
                  editor::detail::EditorUserStateTextResult::Success,
              "named-layout index encoder accepts same-major future-minor metadata");
        constexpr char FutureIndexAssignment[] = "future.layout-index-mode = \"opaque\"\n";
        futureIndexText.Append(FutureIndexAssignment, sizeof(FutureIndexAssignment) - 1u);
        constexpr char FutureLayoutAssignment[] = "layout.01.future-mode = \"work-meta\"\n";
        futureIndexText.Append(FutureLayoutAssignment, sizeof(FutureLayoutAssignment) - 1u);
        editor::detail::EditorNamedLayoutIndexDecodedStorage futureIndexDecoded;
        containers::String futureIndexRoundTrip;
        Check(editor::detail::DecodeEditorNamedLayoutIndexText(
                  {futureIndexText.AsChar(), futureIndexText.Length()}, futureIndexDecoded, &failure) ==
                  editor::detail::EditorUserStateTextResult::Success &&
                  futureIndexDecoded.version.minor == 7 &&
                  editor::detail::EncodeEditorNamedLayoutIndexText(
                      futureIndexDecoded.View(), futureIndexRoundTrip, &failure) ==
                  editor::detail::EditorUserStateTextResult::Success &&
                  std::strstr(futureIndexRoundTrip.AsChar(),
                              "future.layout-index-mode = \"opaque\"") != nullptr &&
                  std::strstr(futureIndexRoundTrip.AsChar(),
                              "layout.01.future-mode = \"work-meta\"") != nullptr,
              "named-layout future-minor index assignments survive canonical decode/re-encode");

        futureIndexDecoded.layouts[0] = futureIndexDecoded.layouts[1];
        futureIndexDecoded.layouts[1] = {};
        futureIndexDecoded.layoutCount = 1;
        futureIndexDecoded.activeId = 0x22;
        containers::String rebasedFutureIndex;
        Check(editor::detail::EncodeEditorNamedLayoutIndexText(
                  futureIndexDecoded.View(), rebasedFutureIndex, &failure) ==
                  editor::detail::EditorUserStateTextResult::Success &&
                  std::strstr(rebasedFutureIndex.AsChar(),
                              "layout.00.future-mode = \"work-meta\"") != nullptr &&
                  std::strstr(rebasedFutureIndex.AsChar(), "layout.01.future-mode") == nullptr,
              "named-layout future metadata follows its stable layout ID when canonical ordinals shift");
        futureIndexDecoded.layouts[0] = {};
        futureIndexDecoded.layoutCount = 0;
        futureIndexDecoded.activeId = 0;
        containers::String retiredFutureIndex;
        Check(editor::detail::EncodeEditorNamedLayoutIndexText(
                  futureIndexDecoded.View(), retiredFutureIndex, &failure) ==
                  editor::detail::EditorUserStateTextResult::Success &&
                  std::strstr(retiredFutureIndex.AsChar(), "future-mode") == nullptr &&
                  std::strstr(retiredFutureIndex.AsChar(),
                              "future.layout-index-mode = \"opaque\"") != nullptr,
              "named-layout future metadata is retired with its deleted stable layout while top-level metadata survives");

        editor::detail::EditorUserStateTextUnknownAssignment tooManyUnknowns[
            editor::detail::MaximumEditorNamedLayoutIndexUnknownRecords + 1u]{};
        editor::detail::EditorNamedLayoutIndexView invalidIndexView = indexSource;
        invalidIndexView.unknownAssignments = {
            tooManyUnknowns, editor::detail::MaximumEditorNamedLayoutIndexUnknownRecords + 1u};
        Check(editor::detail::EncodeEditorNamedLayoutIndexText(
                  invalidIndexView, indexText, &failure) ==
                  editor::detail::EditorUserStateTextResult::InvalidArgument,
              "named-layout index encoder rejects over-bound unknown assignment views before sorting");
        invalidIndexView = indexSource;
        invalidIndexView.unknownAssignments = {
            static_cast<const editor::detail::EditorUserStateTextUnknownAssignment*>(nullptr), 1u};
        Check(editor::detail::EncodeEditorNamedLayoutIndexText(
                  invalidIndexView, indexText, &failure) ==
                  editor::detail::EditorUserStateTextResult::InvalidArgument,
              "named-layout index encoder rejects non-empty null unknown assignment views");

        layouts[1].id = 0x33;
        std::memcpy(layouts[1].name, "Work", 5);
        layouts[1].nameByteCount = 4;
        Check(editor::detail::EncodeEditorNamedLayoutIndexText(
                  {editor::detail::EditorNamedLayoutIndexTextVersion, 0x22, layouts}, indexText,
                  &failure) == editor::detail::EditorUserStateTextResult::InvalidArgument,
              "named-layout index rejects duplicate display names");
    }

    void BuildPanelStateTextFixture(containers::String& text, const containers::StringView stateAssignments,
                                    const bool secondPanel = false,
                                    const serialization::Version version = {1, 0}) noexcept
    {
        text.Clear();
        Check(serialization::AppendTextDocumentHeader(text, "vuserstate", version) == serialization::Result::Success,
              "panel-state text fixture header encodes");
        char projectId[projects::ProjectIdTextBufferSize]{};
        const projects::ProjectId project{{0x11111111u, 0x22222222u, 0x33333333u, 0x44444444u}};
        Check(projects::FormatProjectId(project, projectId, sizeof(projectId)),
              "panel-state text fixture project id formats");
        Check(serialization::AppendTextAssignment(text, "project.id", projectId) == serialization::Result::Success &&
                  serialization::AppendTextAssignment(text, "session.checkpoint.sequence", "1") == serialization::Result::Success &&
                  serialization::AppendTextAssignment(text, "panel.0000000000000064.type", "000000000000000a") == serialization::Result::Success &&
                  serialization::AppendTextAssignment(text, "panel.0000000000000064.open", "true") == serialization::Result::Success &&
                  serialization::AppendTextAssignment(text, "panel.0000000000000064.title", "A") == serialization::Result::Success,
              "panel-state text fixture primary panel inventory encodes");
        if (secondPanel)
            Check(serialization::AppendTextAssignment(text, "panel.00000000000000c8.type", "0000000000000014") == serialization::Result::Success &&
                      serialization::AppendTextAssignment(text, "panel.00000000000000c8.open", "true") == serialization::Result::Success &&
                      serialization::AppendTextAssignment(text, "panel.00000000000000c8.title", "B") == serialization::Result::Success,
                  "panel-state text fixture secondary panel inventory encodes");
        text.Append(stateAssignments);
    }

    void TestPanelStateTextValidation() noexcept
    {
        memory::MemoryBlock storageBlock =
            memory::Allocate(memory::PoolId::Tools,
                             sizeof(editor::detail::EditorUserStateTextDecodedStorage),
                             alignof(editor::detail::EditorUserStateTextDecodedStorage));
        Check(static_cast<bool>(storageBlock), "panel-state validation decode scratch allocation succeeds");
        if (!storageBlock)
            return;
        auto* const decoded = ::new (storageBlock.address) editor::detail::EditorUserStateTextDecodedStorage();
        editor::detail::EditorUserStateTextFailure failure;
        containers::String text;

        BuildPanelStateTextFixture(
            text,
            "panel.0000000000000064.state.presentation.version = \"1\"\n"
            "panel.0000000000000064.state.presentation.bytes = \"0\"\n");
        Check(editor::detail::DecodeEditorUserStateText({text.AsChar(), text.Length()}, *decoded, &failure) ==
                  editor::detail::EditorUserStateTextResult::Success &&
                  decoded->panelStateCount == 1 && decoded->panelStates[0].byteCount == 0 &&
                  decoded->panelStateByteCount == 0,
              "zero-length provider success remains a present versioned panel-state record without chunks");
        containers::String zeroRoundTrip;
        Check(editor::detail::EncodeEditorUserStateText(decoded->View(), zeroRoundTrip, &failure) ==
                  editor::detail::EditorUserStateTextResult::Success &&
                  std::strstr(zeroRoundTrip.AsChar(), ".state.presentation.bytes = \"0\"") != nullptr &&
                  std::strstr(zeroRoundTrip.AsChar(), ".state.presentation.chunk") == nullptr,
              "zero-length panel-state record round-trips canonically without inventing a chunk");

        BuildPanelStateTextFixture(
            text,
            "panel.0000000000000064.state.presentation.version = \"1\"\n"
            "panel.0000000000000064.state.presentation.bytes = \"1\"\n"
            "panel.0000000000000064.state.presentation.chunk = \"0g\"\n");
        Check(editor::detail::DecodeEditorUserStateText({text.AsChar(), text.Length()}, *decoded, &failure) ==
                  editor::detail::EditorUserStateTextResult::Success && decoded->panelStateCount == 0,
              "panel-state malformed hex chunk quarantines only that opaque panel scope");

        BuildPanelStateTextFixture(
            text,
            "panel.0000000000000064.state.presentation.version = \"1\"\n"
            "panel.0000000000000064.state.presentation.bytes = \"2\"\n"
            "panel.0000000000000064.state.presentation.chunk = \"aa\"\n");
        Check(editor::detail::DecodeEditorUserStateText({text.AsChar(), text.Length()}, *decoded, &failure) ==
                  editor::detail::EditorUserStateTextResult::Success && decoded->panelStateCount == 0,
              "panel-state declared-byte mismatch quarantines only that opaque panel scope");

        BuildPanelStateTextFixture(
            text,
            "panel.0000000000000064.state.presentation.version = \"1\"\n"
            "panel.0000000000000064.state.presentation.version = \"2\"\n"
            "panel.0000000000000064.state.presentation.bytes = \"0\"\n");
        Check(editor::detail::DecodeEditorUserStateText({text.AsChar(), text.Length()}, *decoded, &failure) ==
                  editor::detail::EditorUserStateTextResult::Success && decoded->panelStateCount == 0,
              "panel-state duplicate metadata quarantines only that opaque panel scope");

        BuildPanelStateTextFixture(
            text,
            "panel.0000000000000064.state.presentation.version = \"1\"\n"
            "panel.0000000000000064.state.presentation.bytes = \"2\"\n"
            "panel.0000000000000064.state.presentation.chunk = \"aa\"\n"
            "panel.00000000000000c8.state.presentation.version = \"1\"\n"
            "panel.00000000000000c8.state.presentation.bytes = \"0\"\n"
            "panel.0000000000000064.state.presentation.chunk = \"cc\"\n",
            true);
        Check(editor::detail::DecodeEditorUserStateText({text.AsChar(), text.Length()}, *decoded, &failure) ==
                  editor::detail::EditorUserStateTextResult::Success && decoded->panelStateCount == 1 &&
                  decoded->panelStates[0].instanceId == 200 &&
                  decoded->panelStates[0].scope == editor::EditorPanelStateScope::Presentation &&
                  decoded->panelStates[0].byteCount == 0,
              "broken panel-state chunk run quarantines only that scope while a separate panel state survives");

        BuildPanelStateTextFixture(
            text, "panel.0000000000000064.future-field = \"opaque\"\n", false, {1, 9});
        Check(editor::detail::DecodeEditorUserStateText({text.AsChar(), text.Length()}, *decoded, &failure) ==
                  editor::detail::EditorUserStateTextResult::Success &&
                  decoded->version == serialization::Version{1, 9} && decoded->unknownAssignmentCount == 1,
              "future same-major unknown panel field is preserved instead of being misparsed as title");
        containers::String futureRoundTrip;
        Check(editor::detail::EncodeEditorUserStateText(decoded->View(), futureRoundTrip, &failure) ==
                  editor::detail::EditorUserStateTextResult::Success &&
                  std::strstr(futureRoundTrip.AsChar(),
                              "panel.0000000000000064.future-field = \"opaque\"") != nullptr,
              "future same-major unknown panel field survives canonical decode/re-encode");
        decoded->panels[0].typeId = 0x0bull;
        containers::String reboundRoundTrip;
        Check(editor::detail::EncodeEditorUserStateText(decoded->View(), reboundRoundTrip, &failure) ==
                  editor::detail::EditorUserStateTextResult::Success &&
                  std::strstr(reboundRoundTrip.AsChar(),
                              "panel.0000000000000064.future-field = \"opaque\"") == nullptr,
              "unknown panel-scoped future field is not rebound when the same instance is reused by another type");

        decoded->~EditorUserStateTextDecodedStorage();
        memory::Free(storageBlock);
    }

    struct ArtifactValidationObservation final
    {
        char bytes[64]{};
        usize byteCount = 0;
    };

    [[nodiscard]] bool ValidateArtifactFixture(const void* const bytes, const usize byteCount,
                                               void* const userData) noexcept
    {
        constexpr char Prefix[] = "valid:";
        const bool valid = bytes != nullptr && byteCount >= sizeof(Prefix) - 1u &&
                           std::memcmp(bytes, Prefix, sizeof(Prefix) - 1u) == 0;
        if (valid && userData != nullptr)
        {
            auto& observation = *static_cast<ArtifactValidationObservation*>(userData);
            observation = {};
            observation.byteCount = byteCount < sizeof(observation.bytes) ? byteCount : sizeof(observation.bytes);
            std::memcpy(observation.bytes, bytes, observation.byteCount);
        }
        return valid;
    }

    void TestArtifactStore() noexcept
    {
        const filesystem::AbsolutePath directory =
            filesystem::paths::GetCurrentWorkingDirectory().AddDirPath("editor_state_artifact_store_tests");
        const filesystem::AbsolutePath canonical = directory.AddFilePath("fixture.vstate");
        const filesystem::AbsolutePath lkg = directory.AddFilePath("fixture.vstate.lkg");
        const filesystem::AbsolutePath corruptStage = directory.AddFilePath("corrupt.stage");
        filesystem::Manager& files = filesystem::GetManager();
        static_cast<void>(files.DeleteFile(canonical));
        static_cast<void>(files.DeleteFile(lkg));
        static_cast<void>(files.DeleteFile(corruptStage));
        static_cast<void>(files.DeletePath(directory));

        editor::detail::EditorStateArtifactStageIdentity identity;
        for (u32 index = 0; index < sizeof(identity.nonce); ++index)
            identity.nonce[index] = static_cast<u8>(0x80u + index);
        char validationScratch[64]{};
        ArtifactValidationObservation validationObservation;
        const editor::detail::EditorStateArtifactValidator validator{
            ValidateArtifactFixture, &validationObservation};
        const editor::detail::EditorStateArtifactPaths paths{directory, canonical, lkg};
        constexpr char First[] = "valid:first";
        constexpr char Second[] = "valid:second";

        Check(editor::detail::PublishEditorStateArtifact(
                  paths, First, sizeof(First) - 1u, validationScratch, sizeof(validationScratch),
                  identity, validator) == editor::detail::EditorStateArtifactPublishResult::Published,
              "artifact store publishes the first validated canonical image");
        Check(validationObservation.byteCount == sizeof(First) - 1u &&
                  std::memcmp(validationObservation.bytes, First, validationObservation.byteCount) == 0,
              "successful first publication leaves stateful validation on the durable canonical generation");
        Check(editor::detail::PublishEditorStateArtifact(
                  paths, Second, sizeof(Second) - 1u, validationScratch, sizeof(validationScratch),
                  identity, validator) == editor::detail::EditorStateArtifactPublishResult::Published,
              "artifact store rotates the previous canonical to one LKG before replacement");
        Check(validationObservation.byteCount == sizeof(Second) - 1u &&
                  std::memcmp(validationObservation.bytes, Second, validationObservation.byteCount) == 0,
              "LKG rotation cannot leave stateful validation observing the previous canonical generation");

        char loaded[64]{};
        usize loadedBytes = 0;
        Check(editor::detail::LoadEditorStateArtifact(
                  paths, loaded, sizeof(loaded), loadedBytes, validator) ==
                  editor::detail::EditorStateArtifactLoadResult::LoadedCanonical &&
                  loadedBytes == sizeof(Second) - 1u &&
                  std::memcmp(loaded, Second, loadedBytes) == 0,
              "artifact store reloads the current canonical generation");

        constexpr char Corrupt[] = "broken";
        Check(files.CreatePath(directory) &&
                  filesystem::WriteExclusiveFile(corruptStage, Corrupt, sizeof(Corrupt) - 1u) ==
                      filesystem::ExclusiveFileWriteResult::Success &&
                  filesystem::ReplaceFile(corruptStage, canonical),
              "artifact-store fixture can atomically replace canonical with corrupt bytes");
        std::memset(loaded, 0, sizeof(loaded));
        loadedBytes = 0;
        Check(editor::detail::LoadEditorStateArtifact(
                  paths, loaded, sizeof(loaded), loadedBytes, validator) ==
                  editor::detail::EditorStateArtifactLoadResult::RecoveredLkg &&
                  loadedBytes == sizeof(First) - 1u &&
                  std::memcmp(loaded, First, loadedBytes) == 0,
              "artifact store rejects corrupt canonical and recovers the prior validated LKG");

        static_cast<void>(files.DeleteFile(canonical));
        static_cast<void>(files.DeleteFile(lkg));
        static_cast<void>(files.DeleteFile(corruptStage));
        Check(files.DeletePath(directory), "artifact-store fixture directory cleanup");
    }

    struct FakeStore final
    {
        filesystem::AbsolutePath canonical;
        filesystem::AbsolutePath stage;
        usize canonicalBytes = 0;
        usize stageBytes = 0;
        bool canonicalPresent = false;
        bool stagePresent = false;
        bool ensureDirectory = true;
        bool replace = true;
        bool cleanup = true;
        bool corruptStageOnRead = false;
        editor::detail::EditorUserStateFileReadResult canonicalRead = editor::detail::EditorUserStateFileReadResult::Success;
        editor::detail::EditorUserStateFileReadResult stageRead = editor::detail::EditorUserStateFileReadResult::Success;
        filesystem::ExclusiveFileWriteResult write = filesystem::ExclusiveFileWriteResult::Success;
        u8 canonicalImage[editor::MaximumEditorUserStateFileBytes]{};
        u8 stageImage[editor::MaximumEditorUserStateFileBytes]{};
    };

    struct PanelRestoreObservation final
    {
        bool liveClosed = false;
        bool createdUsesCurrentTitle = false;
        bool placeholderUsesFallbackTitle = false;
        bool rejectedInstanceAbsent = true;
        bool resolvedRestartUsesCurrentTitle = false;
    };

    void ObserveRestoredPanel(const editor::EditorPanelInfo& panel, void* const userData) noexcept
    {
        auto& observation = *static_cast<PanelRestoreObservation*>(userData);
        if (panel.id == 100)
            observation.liveClosed = panel.type == 10 && !panel.open && !panel.placeholder &&
                                     std::strcmp(panel.title, "Live current") == 0;
        else if (panel.id == 200)
            observation.createdUsesCurrentTitle = panel.type == 10 && panel.open && !panel.placeholder &&
                                                  std::strcmp(panel.title, "Current Multi") == 0;
        else if (panel.id == 300)
            observation.rejectedInstanceAbsent = false;
        else if (panel.id == 400)
        {
            if (panel.placeholder)
                observation.placeholderUsesFallbackTitle = panel.type == 30 && !panel.open &&
                                                            std::strcmp(panel.title, "Missing Fallback") == 0;
            else
                observation.resolvedRestartUsesCurrentTitle = panel.type == 30 && !panel.open &&
                                                              std::strcmp(panel.title, "Returned Type") == 0;
        }
    }

    bool FakeEnsureDirectory(const filesystem::AbsolutePath&, void* const userData) noexcept
    {
        return static_cast<FakeStore*>(userData)->ensureDirectory;
    }

    editor::detail::EditorUserStateFileReadResult FakeRead(const filesystem::AbsolutePath& path, void* const destination,
                                                           const usize capacity, usize& byteCount, void* const userData) noexcept
    {
        FakeStore& store = *static_cast<FakeStore*>(userData);
        byteCount = 0;
        if (path == store.canonical)
        {
            if (store.canonicalRead != editor::detail::EditorUserStateFileReadResult::Success)
                return store.canonicalRead;
            if (!store.canonicalPresent)
                return editor::detail::EditorUserStateFileReadResult::Missing;
            if (store.canonicalBytes > capacity)
                return editor::detail::EditorUserStateFileReadResult::LimitExceeded;
            std::memcpy(destination, store.canonicalImage, store.canonicalBytes);
            byteCount = store.canonicalBytes;
            return editor::detail::EditorUserStateFileReadResult::Success;
        }
        if (path == store.stage)
        {
            if (store.stageRead != editor::detail::EditorUserStateFileReadResult::Success)
                return store.stageRead;
            if (!store.stagePresent)
                return editor::detail::EditorUserStateFileReadResult::Missing;
            if (store.stageBytes > capacity)
                return editor::detail::EditorUserStateFileReadResult::LimitExceeded;
            std::memcpy(destination, store.stageImage, store.stageBytes);
            if (store.corruptStageOnRead && store.stageBytes != 0)
                static_cast<u8*>(destination)[0] ^= 0x1u;
            byteCount = store.stageBytes;
            return editor::detail::EditorUserStateFileReadResult::Success;
        }
        return editor::detail::EditorUserStateFileReadResult::Missing;
    }

    filesystem::ExclusiveFileWriteResult FakeWrite(const filesystem::AbsolutePath& path, const void* const bytes,
                                                    const usize byteCount, void* const userData) noexcept
    {
        FakeStore& store = *static_cast<FakeStore*>(userData);
        if (!(path == store.stage))
            return filesystem::ExclusiveFileWriteResult::OpenFailed;
        if (store.write == filesystem::ExclusiveFileWriteResult::AlreadyExists ||
            store.write == filesystem::ExclusiveFileWriteResult::OpenFailed ||
            store.write == filesystem::ExclusiveFileWriteResult::UnsupportedPlatform)
            return store.write;
        if (store.stagePresent)
            return filesystem::ExclusiveFileWriteResult::AlreadyExists;
        if (byteCount > sizeof(store.stageImage))
            return filesystem::ExclusiveFileWriteResult::WriteFailed;
        store.stagePresent = true;
        store.stageBytes = byteCount;
        std::memcpy(store.stageImage, bytes, byteCount);
        return store.write;
    }

    bool FakeReplace(const filesystem::AbsolutePath& staged, const filesystem::AbsolutePath& target,
                     void* const userData) noexcept
    {
        FakeStore& store = *static_cast<FakeStore*>(userData);
        if (!(staged == store.stage) || !(target == store.canonical) || !store.stagePresent || !store.replace)
            return false;
        store.canonicalPresent = true;
        store.canonicalBytes = store.stageBytes;
        std::memcpy(store.canonicalImage, store.stageImage, store.stageBytes);
        store.stagePresent = false;
        store.stageBytes = 0;
        return true;
    }

    bool FakeRemove(const filesystem::AbsolutePath& path, void* const userData) noexcept
    {
        FakeStore& store = *static_cast<FakeStore*>(userData);
        if (!(path == store.stage) || !store.stagePresent || !store.cleanup)
            return false;
        store.stagePresent = false;
        store.stageBytes = 0;
        return true;
    }

    [[nodiscard]] editor::detail::EditorUserStateFileOps MakeFakeOps(FakeStore& store) noexcept
    {
        return {FakeEnsureDirectory, FakeRead, FakeWrite, FakeReplace, FakeRemove, &store};
    }

    void TestCodec() noexcept
    {
        DocumentFixture fixture;
        const editor::EditorUserStateDocumentView document = fixture.View();
        usize imageBytes = 0;
        editor::EditorUserStateFailure failure;
        Check(editor::EncodeEditorUserStateImage(document, g_image, sizeof(g_image), imageBytes, &failure),
              "encode valid VUSR image");
        Check(imageBytes != 0 && imageBytes <= editor::MaximumEditorUserStateFileBytes,
              "encoded image respects file bound");

        editor::EditorUserStateEnvelopeInfo info;
        Check(editor::ValidateEditorUserStateImage(g_image, imageBytes, fixture.project, info, &failure) ==
                  editor::EditorUserStateLoadResult::Restored,
              "validate current VUSR image");
        Check(info.version == serialization::Version{1, 0} && info.project == fixture.project &&
                  info.panelRecords == 2 && info.displayRecords == 1 && info.imguiByteCount == 5 &&
                  info.shortcutRecords == 2 && info.preferenceRecords == 2,
              "validated envelope reports exact counts");

        u32 restorePanelCount = 0;
        u32 restoreDisplayCount = 0;
        u32 restoreImguiBytes = 0;
        Check(editor::detail::DecodeEditorUserStatePanelLayoutRestore(
                  g_image, imageBytes, fixture.project,
                  g_restorePanels, editor::MaximumEditorUserStatePanels, restorePanelCount,
                  g_restoreDisplays, editor::MaximumEditorUserStateDisplays, restoreDisplayCount,
                  g_restoreImgui, editor::MaximumEditorUserStateImguiBytes, restoreImguiBytes, &failure) &&
                  restorePanelCount == 2 && restoreDisplayCount == 1 && restoreImguiBytes == 5 &&
                  g_restorePanels[0].type == 10 && g_restorePanels[0].instance == 100 &&
                  !g_restorePanels[0].open && std::strcmp(g_restorePanels[0].fallbackTitle, "Alpha") == 0 &&
                  g_restorePanels[1].type == 20 && g_restorePanels[1].instance == 200 &&
                  g_restorePanels[1].open && std::strcmp(g_restorePanels[1].fallbackTitle, "Beta") == 0 &&
                  g_restoreDisplays[0] == fixture.displays[0] &&
                  std::memcmp(g_restoreImgui, fixture.imgui, sizeof(fixture.imgui)) == 0,
              "validated restore staging decodes canonical PNLS records plus exact UIST topology/layout bytes without owner mutation");

        u32 restoreShortcutCount = 0;
        u32 restorePreferenceCount = 0;
        Check(editor::detail::DecodeEditorUserStateRestoreState(
                  g_image, imageBytes, fixture.project,
                  g_restorePanels, editor::MaximumEditorUserStatePanels, restorePanelCount,
                  g_restoreDisplays, editor::MaximumEditorUserStateDisplays, restoreDisplayCount,
                  g_restoreImgui, editor::MaximumEditorUserStateImguiBytes, restoreImguiBytes,
                  g_restoreShortcuts, editor::MaximumEditorUserStateShortcuts, restoreShortcutCount,
                  g_restorePreferences, editor::MaximumEditorUserStatePreferences, restorePreferenceCount,
                  &failure) &&
                  restoreShortcutCount == 2 && restorePreferenceCount == 2 &&
                  g_restoreShortcuts[0].contextType.value == 3 && g_restoreShortcuts[0].command.value == 9 &&
                  g_restoreShortcuts[0].shortcutOverride.mode == editor::EditorShortcutOverrideMode::Custom &&
                  static_cast<u16>(g_restoreShortcuts[0].shortcutOverride.chord.key) == 4 &&
                  static_cast<u8>(g_restoreShortcuts[0].shortcutOverride.chord.modifiers) == 1 &&
                  g_restoreShortcuts[1].contextType.value == 8 && g_restoreShortcuts[1].command.value == 7 &&
                  g_restoreShortcuts[1].shortcutOverride.mode == editor::EditorShortcutOverrideMode::Unbound &&
                  g_restorePreferences[0].extension.value == 10 && g_restorePreferences[0].providerId == 20 &&
                  g_restorePreferences[0].fieldId == 30 &&
                  g_restorePreferences[0].value.type == editor::EditorPreferenceValueType::Bool &&
                  g_restorePreferences[0].value.value.boolean &&
                  g_restorePreferences[1].extension.value == 30 && g_restorePreferences[1].providerId == 40 &&
                  g_restorePreferences[1].fieldId == 50 &&
                  g_restorePreferences[1].value.type == editor::EditorPreferenceValueType::Utf8String &&
                  g_restorePreferences[1].value.value.string.byteCount == 4 &&
                  std::memcmp(g_restorePreferences[1].value.value.string.bytes, "text", 4) == 0 &&
                  g_restorePreferences[1].value.value.string.bytes[4] == '\0',
              "whole-envelope restore staging materializes canonical SHCT/PREF records into bounded pointer-free owner scratch before any owner mutation");

        g_restorePanels[0].instance = 0xfeedfaceull;
        Check(!editor::detail::DecodeEditorUserStatePanelLayoutRestore(
                  g_image, imageBytes, fixture.project,
                  g_restorePanels, 1, restorePanelCount,
                  g_restoreDisplays, editor::MaximumEditorUserStateDisplays, restoreDisplayCount,
                  g_restoreImgui, editor::MaximumEditorUserStateImguiBytes, restoreImguiBytes, &failure) &&
                  failure.code == editor::EditorUserStateFailureCode::BoundExceeded &&
                  restorePanelCount == 2 && restoreDisplayCount == 1 && restoreImguiBytes == 5 &&
                  g_restorePanels[0].instance == 0xfeedfaceull,
              "restore staging reports exact required counts and copies no owner record prefix when capacity is insufficient");

        const u64 tableOffset = LoadU64(g_image + 24u);
        constexpr u32 expectedSections[]{serialization::MakeFourCC('M', 'E', 'T', 'A'),
                                         serialization::MakeFourCC('P', 'N', 'L', 'S'),
                                         serialization::MakeFourCC('U', 'I', 'S', 'T'),
                                         serialization::MakeFourCC('S', 'H', 'C', 'T'),
                                         serialization::MakeFourCC('P', 'R', 'E', 'F')};
        Check(LoadU32(g_image) == serialization::MakeFourCC('V', 'U', 'S', 'R') && g_image[4] == 1 && g_image[5] == 1 &&
                  LoadU16(g_image + 6u) == serialization::DocumentHeader::WireSize && LoadU16(g_image + 8u) == 1 &&
                  LoadU16(g_image + 10u) == 0 && LoadU16(g_image + 12u) == 0x0003u && LoadU16(g_image + 14u) == 0 &&
                  LoadU64(g_image + 16u) == imageBytes && LoadU32(g_image + 32u) == 5,
              "VUSR header uses the frozen V1 framing");
        bool sectionTableExact = true;
        u64 expectedSectionOffset = serialization::DocumentHeader::WireSize;
        for (u32 index = 0; index < 5; ++index)
        {
            const u8* const descriptor = g_image + tableOffset + static_cast<u64>(index) * serialization::SectionDescriptor::WireSize;
            const u64 sectionOffset = LoadU64(descriptor + 16u);
            const u64 storedSize = LoadU64(descriptor + 24u);
            expectedSectionOffset = (expectedSectionOffset + 7u) & ~u64{7};
            sectionTableExact = sectionTableExact && LoadU32(descriptor) == expectedSections[index] &&
                                LoadU16(descriptor + 4u) == 1 && LoadU16(descriptor + 6u) == 0 &&
                                LoadU32(descriptor + 8u) == static_cast<u32>(serialization::SectionFlags::EditorOnly) &&
                                LoadU16(descriptor + 12u) == static_cast<u16>(serialization::Codec::None) &&
                                descriptor[14] == 3 && descriptor[15] == 0 &&
                                sectionOffset == expectedSectionOffset &&
                                LoadU64(descriptor + 32u) == storedSize &&
                                LoadU64(descriptor + 40u) == serialization::Crc64(g_image + sectionOffset, static_cast<usize>(storedSize));
            expectedSectionOffset += storedSize;
        }
        Check(sectionTableExact && tableOffset == ((expectedSectionOffset + 7u) & ~u64{7}) &&
                  tableOffset + 5u * serialization::SectionDescriptor::WireSize + crypto::Digest256::ByteCount == imageBytes,
              "VUSR section table uses exact order, flags, codec, alignment, sizes and CRC64 values");
        crypto::Digest256 storedDigest;
        std::memcpy(storedDigest.bytes, g_image + imageBytes - crypto::Digest256::ByteCount, crypto::Digest256::ByteCount);
        Check(storedDigest == crypto::Sha256(g_image, imageBytes - crypto::Digest256::ByteCount),
              "VUSR final SHA-256 covers the complete pre-trailer image");

        const u8* const shortcutDescriptor = g_image + tableOffset + 3u * serialization::SectionDescriptor::WireSize;
        const u64 shortcutOffset = LoadU64(shortcutDescriptor + 16u);
        Check(g_image[shortcutOffset + 8u + 19u] == static_cast<u8>(editor::EditorUserStateShortcutMode::Custom) &&
                  g_image[shortcutOffset + 8u + 32u + 19u] == static_cast<u8>(editor::EditorUserStateShortcutMode::Unbound),
              "shortcut wire modes use the frozen Custom and Unbound codes");
        const u8* const preferenceDescriptor = g_image + tableOffset + 4u * serialization::SectionDescriptor::WireSize;
        const u64 preferenceOffset = LoadU64(preferenceDescriptor + 16u);
        Check(LoadU32(g_image + preferenceOffset + 8u + 28u) == 1u &&
                  LoadU32(g_image + preferenceOffset + 8u + 48u + 28u) == 4u,
              "typed preference wire payload sizes match their frozen value types");

        DocumentFixture reordered;
        const editor::EditorUserStatePanelRecord panelSwap = reordered.panels[0];
        reordered.panels[0] = reordered.panels[1];
        reordered.panels[1] = panelSwap;
        const editor::EditorUserStateShortcutRecord shortcutSwap = reordered.shortcuts[0];
        reordered.shortcuts[0] = reordered.shortcuts[1];
        reordered.shortcuts[1] = shortcutSwap;
        const editor::EditorUserStatePreferenceRecord preferenceSwap = reordered.preferences[0];
        reordered.preferences[0] = reordered.preferences[1];
        reordered.preferences[1] = preferenceSwap;
        usize secondBytes = 0;
        Check(editor::EncodeEditorUserStateImage(reordered.View(), g_secondImage, sizeof(g_secondImage), secondBytes, &failure) &&
                  secondBytes == imageBytes && std::memcmp(g_image, g_secondImage, imageBytes) == 0,
              "canonical encoding is independent of input record order");

        std::memcpy(g_secondImage, g_image, imageBytes);
        g_secondImage[40] ^= 0x1u;
        Check(editor::ValidateEditorUserStateImage(g_secondImage, imageBytes, fixture.project, info, &failure) ==
                  editor::EditorUserStateLoadResult::RejectedCorrupt,
              "body corruption is rejected by integrity validation");

        projects::ProjectId foreign{{1, 2, 3, 4}};
        Check(editor::ValidateEditorUserStateImage(g_image, imageBytes, foreign, info, &failure) ==
                  editor::EditorUserStateLoadResult::RejectedProjectMismatch,
              "foreign project identity is rejected");

        std::memcpy(g_secondImage, g_image, imageBytes);
        g_secondImage[10] = 1;
        g_secondImage[11] = 0;
        RefreshHeaderAndDigest(g_secondImage, imageBytes);
        Check(editor::ValidateEditorUserStateImage(g_secondImage, imageBytes, fixture.project, info, &failure) ==
                  editor::EditorUserStateLoadResult::RejectedFuture,
              "higher minor schema is rejected as future");

        std::memcpy(g_secondImage, g_image, imageBytes);
        StoreU16(g_secondImage + 8u, 0u);
        StoreU16(g_secondImage + 10u, 9u);
        RefreshHeaderAndDigest(g_secondImage, imageBytes);
        Check(editor::detail::ValidateEditorUserStateImageWithMigrations(g_secondImage, imageBytes, fixture.project, {}, info,
                                                                        &failure) ==
                  editor::EditorUserStateLoadResult::RejectedFuture,
              "empty production migration table rejects an otherwise well-framed known-old fixture");
        const editor::detail::EditorUserStateMigrationDecoder knownOld{{0, 9}, DecodeKnownOldFixture, nullptr};
        Check(editor::detail::ValidateEditorUserStateImageWithMigrations(g_secondImage, imageBytes, fixture.project,
                                                                        {&knownOld, 1}, info, &failure) ==
                  editor::EditorUserStateLoadResult::Migrated,
              "codec-local injected known-old decoder exercises migration dispatch without product acceptance");

        std::memcpy(g_secondImage, g_image, imageBytes);
        StoreU16(g_secondImage + 8u, 1u);
        StoreU16(g_secondImage + 10u, 1u);
        RefreshHeaderAndDigest(g_secondImage, imageBytes);
        const editor::detail::EditorUserStateMigrationDecoder newerDecoder{{1, 1}, DecodeKnownOldFixture, nullptr};
        Check(editor::detail::ValidateEditorUserStateImageWithMigrations(g_secondImage, imageBytes, fixture.project,
                                                                        {&newerDecoder, 1}, info, &failure) ==
                  editor::EditorUserStateLoadResult::RejectedFuture,
              "migration dispatch never consults an injected newer-schema decoder");

        constexpr usize compactMissingSectionBytes = serialization::DocumentHeader::WireSize +
                                                     4u * serialization::SectionDescriptor::WireSize +
                                                     crypto::Digest256::ByteCount;
        std::memset(g_secondImage, 0, compactMissingSectionBytes);
        StoreU32(g_secondImage, serialization::MakeFourCC('V', 'U', 'S', 'R'));
        g_secondImage[4] = serialization::DocumentHeader::LittleEndian;
        g_secondImage[5] = serialization::DocumentHeader::EncodingVersion;
        StoreU16(g_secondImage + 6u, serialization::DocumentHeader::WireSize);
        StoreU16(g_secondImage + 8u, 1u);
        StoreU16(g_secondImage + 10u, 0u);
        StoreU16(g_secondImage + 12u, 0x0003u);
        StoreU16(g_secondImage + 14u, 0u);
        StoreU64(g_secondImage + 16u, compactMissingSectionBytes);
        StoreU64(g_secondImage + 24u, serialization::DocumentHeader::WireSize);
        StoreU32(g_secondImage + 32u, 4u);
        for (u32 index = 0; index < 4; ++index)
        {
            u8* const descriptor = g_secondImage + serialization::DocumentHeader::WireSize +
                                   static_cast<usize>(index) * serialization::SectionDescriptor::WireSize;
            StoreU32(descriptor, expectedSections[index]);
            StoreU16(descriptor + 4u, 1u);
            StoreU16(descriptor + 6u, 0u);
            StoreU32(descriptor + 8u, static_cast<u32>(serialization::SectionFlags::EditorOnly));
            StoreU16(descriptor + 12u, static_cast<u16>(serialization::Codec::None));
            descriptor[14] = 3;
            StoreU64(descriptor + 16u, serialization::DocumentHeader::WireSize);
        }
        RefreshHeaderAndDigest(g_secondImage, compactMissingSectionBytes);
        Check(editor::ValidateEditorUserStateImage(g_secondImage, compactMissingSectionBytes, fixture.project, info, &failure) ==
                  editor::EditorUserStateLoadResult::RejectedFuture,
              "compact well-framed missing-section schema is rejected as future before V1 minimum-size assumptions");

        std::memcpy(g_secondImage, g_image, imageBytes);
        StoreU32(g_secondImage + 32u, 6u);
        RefreshHeaderAndDigest(g_secondImage, imageBytes);
        Check(editor::ValidateEditorUserStateImage(g_secondImage, imageBytes, fixture.project, info, &failure) ==
                  editor::EditorUserStateLoadResult::RejectedFuture,
              "extra physical section count is unsupported future schema rather than a runtime bound");

        std::memcpy(g_secondImage, g_image, imageBytes);
        g_secondImage[14] = 1;
        RefreshHeaderAndDigest(g_secondImage, imageBytes);
        Check(editor::ValidateEditorUserStateImage(g_secondImage, imageBytes, fixture.project, info, &failure) ==
                  editor::EditorUserStateLoadResult::RejectedFuture,
              "nonzero header reserved field is rejected as future");

        std::memcpy(g_secondImage, g_image, imageBytes);
        const u64 secondTableOffset = LoadU64(g_secondImage + 24u);
        g_secondImage[secondTableOffset + 14u] = 4;
        RefreshHeaderAndDigest(g_secondImage, imageBytes);
        Check(editor::ValidateEditorUserStateImage(g_secondImage, imageBytes, fixture.project, info, &failure) ==
                  editor::EditorUserStateLoadResult::RejectedFuture,
              "section alignment beyond the frozen V1 limit is rejected as future");

        std::memcpy(g_secondImage, g_image, imageBytes);
        const u64 reorderedTableOffset = LoadU64(g_secondImage + 24u);
        u8 descriptorSwap[serialization::SectionDescriptor::WireSize]{};
        std::memcpy(descriptorSwap, g_secondImage + reorderedTableOffset, sizeof(descriptorSwap));
        std::memcpy(g_secondImage + reorderedTableOffset,
                    g_secondImage + reorderedTableOffset + serialization::SectionDescriptor::WireSize,
                    serialization::SectionDescriptor::WireSize);
        std::memcpy(g_secondImage + reorderedTableOffset + serialization::SectionDescriptor::WireSize,
                    descriptorSwap, sizeof(descriptorSwap));
        RefreshHeaderAndDigest(g_secondImage, imageBytes);
        Check(editor::ValidateEditorUserStateImage(g_secondImage, imageBytes, fixture.project, info, &failure) ==
                  editor::EditorUserStateLoadResult::RejectedFuture,
              "physically reordered section descriptors are rejected as unsupported V1 before generic offset validation");

        std::memcpy(g_secondImage, g_image, imageBytes);
        u8* const panelDescriptor = g_secondImage + secondTableOffset + serialization::SectionDescriptor::WireSize;
        const u64 panelOffset = LoadU64(panelDescriptor + 16u);
        g_secondImage[panelOffset + 37u] = 0x7fu;
        RefreshSectionCrcAndDigest(g_secondImage, imageBytes, 1);
        Check(editor::ValidateEditorUserStateImage(g_secondImage, imageBytes, fixture.project, info, &failure) ==
                  editor::EditorUserStateLoadResult::RejectedCorrupt,
              "nonzero variable-record padding is rejected after valid CRC and digest refresh");

        std::memcpy(g_secondImage, g_image, imageBytes);
        g_secondImage[imageBytes - 1u] ^= 0x1u;
        Check(editor::ValidateEditorUserStateImage(g_secondImage, imageBytes, fixture.project, info, &failure) ==
                  editor::EditorUserStateLoadResult::RejectedCorrupt,
              "bad final SHA-256 is rejected while section CRCs remain valid");

        std::memcpy(g_secondImage, g_image, imageBytes);
        const u64 reservedPanelOffset = LoadU64(g_secondImage + secondTableOffset +
                                                serialization::SectionDescriptor::WireSize + 16u);
        StoreU32(g_secondImage + reservedPanelOffset + 4u, 1u);
        RefreshSectionCrcAndDigest(g_secondImage, imageBytes, 1);
        Check(editor::ValidateEditorUserStateImage(g_secondImage, imageBytes, fixture.project, info, &failure) ==
                  editor::EditorUserStateLoadResult::RejectedFuture,
              "nonzero section-body reserved field is rejected as unsupported V1");

        std::memcpy(g_secondImage, g_image, imageBytes);
        const u64 utf8PanelOffset = LoadU64(g_secondImage + secondTableOffset +
                                            serialization::SectionDescriptor::WireSize + 16u);
        g_secondImage[utf8PanelOffset + 32u] = 0xc0u;
        RefreshSectionCrcAndDigest(g_secondImage, imageBytes, 1);
        Check(editor::ValidateEditorUserStateImage(g_secondImage, imageBytes, fixture.project, info, &failure) ==
                  editor::EditorUserStateLoadResult::RejectedCorrupt,
              "malformed panel UTF-8 is rejected after integrity fields are refreshed");

        const u64 canonicalShortcutOffset = LoadU64(g_image + tableOffset +
                                                     3u * serialization::SectionDescriptor::WireSize + 16u);
        std::memcpy(g_secondImage, g_image, imageBytes);
        g_secondImage[canonicalShortcutOffset + 8u + 19u] = 0u;
        RefreshSectionCrcAndDigest(g_secondImage, imageBytes, 3);
        Check(editor::ValidateEditorUserStateImage(g_secondImage, imageBytes, fixture.project, info, &failure) ==
                  editor::EditorUserStateLoadResult::RejectedCorrupt,
              "invalid decoded shortcut mode is rejected after integrity fields are refreshed");

        std::memcpy(g_secondImage, g_image, imageBytes);
        StoreU64(g_secondImage + canonicalShortcutOffset + 8u + 32u,
                 LoadU64(g_secondImage + canonicalShortcutOffset + 8u));
        StoreU64(g_secondImage + canonicalShortcutOffset + 8u + 32u + 8u,
                 LoadU64(g_secondImage + canonicalShortcutOffset + 8u + 8u));
        RefreshSectionCrcAndDigest(g_secondImage, imageBytes, 3);
        Check(editor::ValidateEditorUserStateImage(g_secondImage, imageBytes, fixture.project, info, &failure) ==
                  editor::EditorUserStateLoadResult::RejectedCorrupt,
              "duplicate decoded shortcut stable key is rejected rather than resolved by file order");

        const u64 canonicalPreferenceOffset = LoadU64(g_image + tableOffset +
                                                       4u * serialization::SectionDescriptor::WireSize + 16u);
        std::memcpy(g_secondImage, g_image, imageBytes);
        StoreU64(g_secondImage + canonicalPreferenceOffset + 8u, 0u);
        RefreshSectionCrcAndDigest(g_secondImage, imageBytes, 4);
        Check(editor::ValidateEditorUserStateImage(g_secondImage, imageBytes, fixture.project, info, &failure) ==
                  editor::EditorUserStateLoadResult::RejectedCorrupt,
              "zero decoded preference stable ID is rejected");

        std::memcpy(g_secondImage, g_image, imageBytes);
        g_secondImage[canonicalPreferenceOffset + 8u + 24u] = 0u;
        RefreshSectionCrcAndDigest(g_secondImage, imageBytes, 4);
        Check(editor::ValidateEditorUserStateImage(g_secondImage, imageBytes, fixture.project, info, &failure) ==
                  editor::EditorUserStateLoadResult::RejectedCorrupt,
              "invalid decoded preference value type is rejected");

        std::memcpy(g_secondImage, g_image, imageBytes);
        StoreU32(g_secondImage + canonicalPreferenceOffset + 8u + 28u, 2u);
        RefreshSectionCrcAndDigest(g_secondImage, imageBytes, 4);
        Check(editor::ValidateEditorUserStateImage(g_secondImage, imageBytes, fixture.project, info, &failure) ==
                  editor::EditorUserStateLoadResult::RejectedCorrupt,
              "invalid decoded Bool payload size is rejected");

        std::memcpy(g_secondImage, g_image, imageBytes);
        g_secondImage[canonicalPreferenceOffset + 8u + 40u] = 2u;
        RefreshSectionCrcAndDigest(g_secondImage, imageBytes, 4);
        Check(editor::ValidateEditorUserStateImage(g_secondImage, imageBytes, fixture.project, info, &failure) ==
                  editor::EditorUserStateLoadResult::RejectedCorrupt,
              "invalid decoded Bool value is rejected");

        std::memcpy(g_secondImage, g_image, imageBytes);
        constexpr u64 firstPreferenceEncodedBytes = 48u;
        g_secondImage[canonicalPreferenceOffset + 8u + firstPreferenceEncodedBytes + 40u] = 0xc0u;
        RefreshSectionCrcAndDigest(g_secondImage, imageBytes, 4);
        Check(editor::ValidateEditorUserStateImage(g_secondImage, imageBytes, fixture.project, info, &failure) ==
                  editor::EditorUserStateLoadResult::RejectedCorrupt,
              "malformed typed-string UTF-8 is rejected after integrity fields are refreshed");

        const u64 canonicalUiOffset = LoadU64(g_image + tableOffset +
                                              2u * serialization::SectionDescriptor::WireSize + 16u);
        std::memcpy(g_secondImage, g_image, imageBytes);
        StoreU32(g_secondImage + canonicalUiOffset, 0u);
        RefreshSectionCrcAndDigest(g_secondImage, imageBytes, 2);
        Check(editor::ValidateEditorUserStateImage(g_secondImage, imageBytes, fixture.project, info, &failure) ==
                  editor::EditorUserStateLoadResult::RejectedCorrupt,
              "decoded zero-display topology is malformed rather than a hard-bound violation");

        std::memcpy(g_secondImage, g_image, imageBytes);
        StoreU32(g_secondImage + panelOffset, editor::MaximumEditorUserStatePanels + 1u);
        RefreshSectionCrcAndDigest(g_secondImage, imageBytes, 1);
        Check(editor::ValidateEditorUserStateImage(g_secondImage, imageBytes, fixture.project, info, &failure) ==
                  editor::EditorUserStateLoadResult::RejectedBounds,
              "decoded panel count bound plus one is rejected as a hard bound");

        std::memcpy(g_secondImage, g_image, imageBytes);
        StoreU32(g_secondImage + canonicalUiOffset, editor::MaximumEditorUserStateDisplays + 1u);
        RefreshSectionCrcAndDigest(g_secondImage, imageBytes, 2);
        Check(editor::ValidateEditorUserStateImage(g_secondImage, imageBytes, fixture.project, info, &failure) ==
                  editor::EditorUserStateLoadResult::RejectedBounds,
              "decoded display count bound plus one is rejected as a hard bound");

        std::memcpy(g_secondImage, g_image, imageBytes);
        StoreU32(g_secondImage + canonicalUiOffset + 4u, editor::MaximumEditorUserStateImguiBytes + 1u);
        RefreshSectionCrcAndDigest(g_secondImage, imageBytes, 2);
        Check(editor::ValidateEditorUserStateImage(g_secondImage, imageBytes, fixture.project, info, &failure) ==
                  editor::EditorUserStateLoadResult::RejectedBounds,
              "decoded ImGui byte bound plus one is rejected as a hard bound");

        std::memcpy(g_secondImage, g_image, imageBytes);
        StoreU32(g_secondImage + canonicalShortcutOffset, editor::MaximumEditorUserStateShortcuts + 1u);
        RefreshSectionCrcAndDigest(g_secondImage, imageBytes, 3);
        Check(editor::ValidateEditorUserStateImage(g_secondImage, imageBytes, fixture.project, info, &failure) ==
                  editor::EditorUserStateLoadResult::RejectedBounds,
              "decoded shortcut count bound plus one is rejected as a hard bound");

        std::memcpy(g_secondImage, g_image, imageBytes);
        StoreU32(g_secondImage + canonicalPreferenceOffset, editor::MaximumEditorUserStatePreferences + 1u);
        RefreshSectionCrcAndDigest(g_secondImage, imageBytes, 4);
        Check(editor::ValidateEditorUserStateImage(g_secondImage, imageBytes, fixture.project, info, &failure) ==
                  editor::EditorUserStateLoadResult::RejectedBounds,
              "decoded preference count bound plus one is rejected as a hard bound");

        std::memcpy(g_secondImage, g_image, imageBytes);
        StoreU16(g_secondImage + panelOffset + 8u + 16u, editor::MaximumEditorUserStatePanelTitleBytes + 1u);
        RefreshSectionCrcAndDigest(g_secondImage, imageBytes, 1);
        Check(editor::ValidateEditorUserStateImage(g_secondImage, imageBytes, fixture.project, info, &failure) ==
                  editor::EditorUserStateLoadResult::RejectedBounds,
              "decoded panel title bound plus one is rejected as a hard bound");

        std::memcpy(g_secondImage, g_image, imageBytes);
        StoreU32(g_secondImage + canonicalPreferenceOffset + 8u + firstPreferenceEncodedBytes + 28u,
                 editor::MaximumEditorUserStatePreferenceStringBytes + 1u);
        RefreshSectionCrcAndDigest(g_secondImage, imageBytes, 4);
        Check(editor::ValidateEditorUserStateImage(g_secondImage, imageBytes, fixture.project, info, &failure) ==
                  editor::EditorUserStateLoadResult::RejectedBounds,
              "decoded typed-string bound plus one is rejected as a hard bound");

        std::memcpy(g_secondImage, g_image, imageBytes);
        StoreU64(g_secondImage + panelOffset + 8u + 32u + 8u,
                 LoadU64(g_secondImage + panelOffset + 8u + 8u));
        RefreshSectionCrcAndDigest(g_secondImage, imageBytes, 1);
        Check(editor::ValidateEditorUserStateImage(g_secondImage, imageBytes, fixture.project, info, &failure) ==
                  editor::EditorUserStateLoadResult::RejectedCorrupt,
              "duplicate decoded panel instance identity is rejected");

        std::memcpy(g_secondImage, g_image, imageBytes);
        const u64 secondPreferenceOffset = canonicalPreferenceOffset + 8u + firstPreferenceEncodedBytes;
        StoreU64(g_secondImage + secondPreferenceOffset,
                 LoadU64(g_secondImage + canonicalPreferenceOffset + 8u));
        StoreU64(g_secondImage + secondPreferenceOffset + 8u,
                 LoadU64(g_secondImage + canonicalPreferenceOffset + 16u));
        StoreU64(g_secondImage + secondPreferenceOffset + 16u,
                 LoadU64(g_secondImage + canonicalPreferenceOffset + 24u));
        RefreshSectionCrcAndDigest(g_secondImage, imageBytes, 4);
        Check(editor::ValidateEditorUserStateImage(g_secondImage, imageBytes, fixture.project, info, &failure) ==
                  editor::EditorUserStateLoadResult::RejectedCorrupt,
              "duplicate decoded typed-preference stable key is rejected");

        const u8* const uiDescriptor = g_image + tableOffset + 2u * serialization::SectionDescriptor::WireSize;
        const u64 uiStoredSize = LoadU64(uiDescriptor + 24u);
        const u64 shortcutSectionOffset = LoadU64(g_image + tableOffset +
                                                  3u * serialization::SectionDescriptor::WireSize + 16u);
        Check(canonicalUiOffset + uiStoredSize < shortcutSectionOffset,
              "canonical fixture contains inter-section alignment padding for zero-padding proof");
        std::memcpy(g_secondImage, g_image, imageBytes);
        g_secondImage[canonicalUiOffset + uiStoredSize] = 0x5au;
        RefreshHeaderAndDigest(g_secondImage, imageBytes);
        Check(editor::ValidateEditorUserStateImage(g_secondImage, imageBytes, fixture.project, info, &failure) ==
                  editor::EditorUserStateLoadResult::RejectedCorrupt,
              "nonzero inter-section alignment padding is rejected");

        Check(editor::ValidateEditorUserStateImage(g_image, editor::MaximumEditorUserStateFileBytes + 1u,
                                                   fixture.project, info, &failure) ==
                  editor::EditorUserStateLoadResult::RejectedBounds,
              "file bound plus one is rejected before reading");
        std::memset(g_secondImage, 0, sizeof(g_secondImage));
        Check(editor::ValidateEditorUserStateImage(g_secondImage, editor::MaximumEditorUserStateFileBytes,
                                                   fixture.project, info, &failure) !=
                  editor::EditorUserStateLoadResult::RejectedBounds,
              "exact 1 MiB file extent passes the hard-bound gate before ordinary framing validation");

        editor::EditorUserStateDocumentView invalid = document;
        invalid.displays = {};
        Check(!editor::EncodeEditorUserStateImage(invalid, g_secondImage, sizeof(g_secondImage), secondBytes, &failure) &&
                  failure.code == editor::EditorUserStateFailureCode::InvalidArgument,
              "zero-display encode is rejected as an invalid document argument");
        invalid = document;
        invalid.displays = {g_displayOverflow, editor::MaximumEditorUserStateDisplays + 1u};
        Check(!editor::EncodeEditorUserStateImage(invalid, g_secondImage, sizeof(g_secondImage), secondBytes, &failure),
              "display count bound plus one is rejected");
        invalid = document;
        invalid.imguiBytes = {g_imguiOverflow, editor::MaximumEditorUserStateImguiBytes + 1u};
        Check(!editor::EncodeEditorUserStateImage(invalid, g_secondImage, sizeof(g_secondImage), secondBytes, &failure),
              "ImGui byte bound plus one is rejected");
        invalid = document;
        invalid.panels = {g_panelOverflow, editor::MaximumEditorUserStatePanels + 1u};
        Check(!editor::EncodeEditorUserStateImage(invalid, g_secondImage, sizeof(g_secondImage), secondBytes, &failure),
              "panel count bound plus one is rejected");
        invalid = document;
        invalid.shortcuts = {g_shortcutOverflow, editor::MaximumEditorUserStateShortcuts + 1u};
        Check(!editor::EncodeEditorUserStateImage(invalid, g_secondImage, sizeof(g_secondImage), secondBytes, &failure),
              "shortcut count bound plus one is rejected");
        invalid = document;
        invalid.preferences = {g_preferenceOverflow, editor::MaximumEditorUserStatePreferences + 1u};
        Check(!editor::EncodeEditorUserStateImage(invalid, g_secondImage, sizeof(g_secondImage), secondBytes, &failure),
              "preference count bound plus one is rejected");

        for (u32 index = 0; index < editor::MaximumEditorUserStatePanels; ++index)
        {
            g_panelOverflow[index].typeId = 1;
            g_panelOverflow[index].instanceId = static_cast<u64>(index) + 1u;
            g_panelOverflow[index].fallbackTitleByteCount = 1;
            g_panelOverflow[index].fallbackTitle[0] = 'p';
        }
        invalid = document;
        invalid.panels = {g_panelOverflow, editor::MaximumEditorUserStatePanels};
        Check(editor::EncodeEditorUserStateImage(invalid, g_secondImage, sizeof(g_secondImage), secondBytes, &failure),
              "panel count exact bound is accepted");

        for (u32 index = 0; index < editor::MaximumEditorUserStateDisplays; ++index)
        {
            g_displayOverflow[index] = fixture.displays[0];
            g_displayOverflow[index].boundsX = static_cast<i32>(index) * 1920;
            g_displayOverflow[index].workX = static_cast<i32>(index) * 1920;
            g_displayOverflow[index].primary = index == 0;
        }
        invalid = document;
        invalid.displays = {g_displayOverflow, editor::MaximumEditorUserStateDisplays};
        Check(editor::EncodeEditorUserStateImage(invalid, g_secondImage, sizeof(g_secondImage), secondBytes, &failure),
              "display count exact bound is accepted");

        for (u32 index = 0; index < editor::MaximumEditorUserStateShortcuts; ++index)
        {
            g_shortcutOverflow[index].contextTypeId = 1;
            g_shortcutOverflow[index].commandId = static_cast<u64>(index) + 1u;
            g_shortcutOverflow[index].mode = editor::EditorUserStateShortcutMode::Unbound;
        }
        invalid = document;
        invalid.shortcuts = {g_shortcutOverflow, editor::MaximumEditorUserStateShortcuts};
        Check(editor::EncodeEditorUserStateImage(invalid, g_secondImage, sizeof(g_secondImage), secondBytes, &failure),
              "shortcut count exact bound is accepted");

        for (u32 index = 0; index < editor::MaximumEditorUserStatePreferences; ++index)
        {
            g_preferenceOverflow[index].extensionId = 1;
            g_preferenceOverflow[index].providerId = 1;
            g_preferenceOverflow[index].fieldId = static_cast<u64>(index) + 1u;
            g_preferenceOverflow[index].type = editor::EditorUserStatePreferenceValueType::Bool;
        }
        invalid = document;
        invalid.preferences = {g_preferenceOverflow, editor::MaximumEditorUserStatePreferences};
        Check(editor::EncodeEditorUserStateImage(invalid, g_secondImage, sizeof(g_secondImage), secondBytes, &failure),
              "preference count exact bound is accepted");

        invalid = document;
        invalid.imguiBytes = {g_imguiOverflow, editor::MaximumEditorUserStateImguiBytes};
        Check(editor::EncodeEditorUserStateImage(invalid, g_secondImage, sizeof(g_secondImage), secondBytes, &failure),
              "ImGui byte exact bound is accepted");

        editor::EditorUserStatePanelRecord boundedPanel = fixture.panels[0];
        boundedPanel.fallbackTitleByteCount = editor::MaximumEditorUserStatePanelTitleBytes;
        for (u32 index = 0; index < boundedPanel.fallbackTitleByteCount; ++index)
            boundedPanel.fallbackTitle[index] = 'a';
        invalid = document;
        invalid.panels = {&boundedPanel, 1};
        Check(editor::EncodeEditorUserStateImage(invalid, g_secondImage, sizeof(g_secondImage), secondBytes, &failure),
              "panel fallback title exact bound is accepted");
        boundedPanel.fallbackTitleByteCount = editor::MaximumEditorUserStatePanelTitleBytes + 1u;
        Check(!editor::EncodeEditorUserStateImage(invalid, g_secondImage, sizeof(g_secondImage), secondBytes, &failure),
              "panel fallback title bound plus one is rejected");

        editor::EditorUserStatePreferenceRecord boundedPreference = fixture.preferences[0];
        boundedPreference.stringByteCount = editor::MaximumEditorUserStatePreferenceStringBytes;
        for (u32 index = 0; index < boundedPreference.stringByteCount; ++index)
            boundedPreference.stringValue[index] = 'b';
        invalid = document;
        invalid.preferences = {&boundedPreference, 1};
        Check(editor::EncodeEditorUserStateImage(invalid, g_secondImage, sizeof(g_secondImage), secondBytes, &failure),
              "typed string exact bound is accepted");
        boundedPreference.stringByteCount = editor::MaximumEditorUserStatePreferenceStringBytes + 1u;
        Check(!editor::EncodeEditorUserStateImage(invalid, g_secondImage, sizeof(g_secondImage), secondBytes, &failure),
              "typed string bound plus one is rejected");

        editor::EditorUserStateSavedDisplay boundedDisplay = fixture.displays[0];
        boundedDisplay.scaleQ16_16 = 0x00100000u;
        invalid = document;
        invalid.displays = {&boundedDisplay, 1};
        Check(editor::EncodeEditorUserStateImage(invalid, g_secondImage, sizeof(g_secondImage), secondBytes, &failure),
              "display Q16.16 exact maximum is accepted");
        boundedDisplay.scaleQ16_16 = 0x00100001u;
        Check(!editor::EncodeEditorUserStateImage(invalid, g_secondImage, sizeof(g_secondImage), secondBytes, &failure),
              "display Q16.16 maximum plus one is rejected");
        boundedDisplay.scaleQ16_16 = 0;
        Check(!editor::EncodeEditorUserStateImage(invalid, g_secondImage, sizeof(g_secondImage), secondBytes, &failure),
              "display zero Q16.16 scale is rejected");
        boundedDisplay.scaleQ16_16 = 1;
        Check(editor::EncodeEditorUserStateImage(invalid, g_secondImage, sizeof(g_secondImage), secondBytes, &failure),
              "display Q16.16 exact minimum is accepted");

        editor::EditorUserStatePreferenceRecord invalidPreference = fixture.preferences[1];
        invalid = document;
        invalid.preferences = {&invalidPreference, 1};
        invalidPreference.extensionId = 0;
        Check(!editor::EncodeEditorUserStateImage(invalid, g_secondImage, sizeof(g_secondImage), secondBytes, &failure),
              "zero preference extension ID is rejected");
        invalidPreference = fixture.preferences[1];
        invalidPreference.providerId = 0;
        Check(!editor::EncodeEditorUserStateImage(invalid, g_secondImage, sizeof(g_secondImage), secondBytes, &failure),
              "zero preference provider ID is rejected");
        invalidPreference = fixture.preferences[1];
        invalidPreference.fieldId = 0;
        Check(!editor::EncodeEditorUserStateImage(invalid, g_secondImage, sizeof(g_secondImage), secondBytes, &failure),
              "zero preference field ID is rejected");

        editor::EditorUserStateShortcutRecord shortcut = fixture.shortcuts[1];
        shortcut.keyCode = static_cast<u16>(input::Key::KeypadComma);
        invalid = document;
        invalid.shortcuts = {&shortcut, 1};
        Check(editor::EncodeEditorUserStateImage(invalid, g_secondImage, sizeof(g_secondImage), secondBytes, &failure),
              "shortcut accepts the complete E3.2 known HID-key ranges");
        shortcut.keyCode = static_cast<u16>(input::Key::LeftControl);
        Check(!editor::EncodeEditorUserStateImage(invalid, g_secondImage, sizeof(g_secondImage), secondBytes, &failure),
              "modifier-only shortcut key is rejected");
        shortcut = fixture.shortcuts[1];
        shortcut.modifiers = 0x10u;
        Check(!editor::EncodeEditorUserStateImage(invalid, g_secondImage, sizeof(g_secondImage), secondBytes, &failure),
              "unknown shortcut modifier bit is rejected");

        editor::EditorUserStatePanelRecord duplicatePanels[2]{fixture.panels[0], fixture.panels[1]};
        duplicatePanels[1].instanceId = duplicatePanels[0].instanceId;
        invalid = document;
        invalid.panels = {duplicatePanels, 2};
        Check(!editor::EncodeEditorUserStateImage(invalid, g_secondImage, sizeof(g_secondImage), secondBytes, &failure),
              "duplicate panel instance identity is rejected");
        editor::EditorUserStateShortcutRecord duplicateShortcuts[2]{fixture.shortcuts[0], fixture.shortcuts[1]};
        duplicateShortcuts[1].contextTypeId = duplicateShortcuts[0].contextTypeId;
        duplicateShortcuts[1].commandId = duplicateShortcuts[0].commandId;
        invalid = document;
        invalid.shortcuts = {duplicateShortcuts, 2};
        Check(!editor::EncodeEditorUserStateImage(invalid, g_secondImage, sizeof(g_secondImage), secondBytes, &failure),
              "duplicate shortcut stable key is rejected");
        editor::EditorUserStatePreferenceRecord duplicatePreferences[2]{fixture.preferences[0], fixture.preferences[1]};
        duplicatePreferences[1].extensionId = duplicatePreferences[0].extensionId;
        duplicatePreferences[1].providerId = duplicatePreferences[0].providerId;
        duplicatePreferences[1].fieldId = duplicatePreferences[0].fieldId;
        invalid = document;
        invalid.preferences = {duplicatePreferences, 2};
        Check(!editor::EncodeEditorUserStateImage(invalid, g_secondImage, sizeof(g_secondImage), secondBytes, &failure),
              "duplicate typed-preference stable key is rejected");

        editor::EditorUserStatePreferenceRecord integerPreferences[2]{};
        integerPreferences[0].extensionId = 11;
        integerPreferences[0].providerId = 21;
        integerPreferences[0].fieldId = 31;
        integerPreferences[0].type = editor::EditorUserStatePreferenceValueType::SignedInteger;
        integerPreferences[0].signedValue = -0x123456789ll;
        integerPreferences[1].extensionId = 12;
        integerPreferences[1].providerId = 22;
        integerPreferences[1].fieldId = 32;
        integerPreferences[1].type = editor::EditorUserStatePreferenceValueType::UnsignedBitMask;
        integerPreferences[1].unsignedValue = 0xf0e1d2c3b4a59687ull;
        invalid = document;
        invalid.preferences = {integerPreferences, 2};
        usize integerImageBytes = 0;
        Check(editor::EncodeEditorUserStateImage(invalid, g_secondImage, sizeof(g_secondImage), integerImageBytes, &failure) &&
                  editor::ValidateEditorUserStateImage(g_secondImage, integerImageBytes, fixture.project, info, &failure) ==
                      editor::EditorUserStateLoadResult::Restored,
              "SignedInteger and UnsignedBitMask preferences encode and validate as current V1");
        const u64 integerTableOffset = LoadU64(g_secondImage + 24u);
        const u64 integerPreferenceOffset = LoadU64(g_secondImage + integerTableOffset +
                                                    4u * serialization::SectionDescriptor::WireSize + 16u);
        Check(g_secondImage[integerPreferenceOffset + 8u + 24u] ==
                  static_cast<u8>(editor::EditorUserStatePreferenceValueType::SignedInteger) &&
                  LoadU32(g_secondImage + integerPreferenceOffset + 8u + 28u) == 8u &&
                  g_secondImage[integerPreferenceOffset + 8u + 48u + 24u] ==
                  static_cast<u8>(editor::EditorUserStatePreferenceValueType::UnsignedBitMask) &&
                  LoadU32(g_secondImage + integerPreferenceOffset + 8u + 48u + 28u) == 8u,
              "SignedInteger and UnsignedBitMask use frozen type codes with exact 8-byte payloads");
        std::memcpy(g_validation, g_secondImage, integerImageBytes);
        StoreU32(g_secondImage + integerPreferenceOffset + 8u + 28u, 7u);
        RefreshSectionCrcAndDigest(g_secondImage, integerImageBytes, 4);
        Check(editor::ValidateEditorUserStateImage(g_secondImage, integerImageBytes, fixture.project, info, &failure) ==
                  editor::EditorUserStateLoadResult::RejectedCorrupt,
              "SignedInteger payload size other than eight is rejected");
        std::memcpy(g_secondImage, g_validation, integerImageBytes);
        StoreU32(g_secondImage + integerPreferenceOffset + 8u + 48u + 28u, 9u);
        RefreshSectionCrcAndDigest(g_secondImage, integerImageBytes, 4);
        Check(editor::ValidateEditorUserStateImage(g_secondImage, integerImageBytes, fixture.project, info, &failure) ==
                  editor::EditorUserStateLoadResult::RejectedCorrupt,
              "UnsignedBitMask payload size other than eight is rejected");
    }

    void TestStore() noexcept
    {
        DocumentFixture fixture;
        const filesystem::AbsolutePath directory = filesystem::paths::GetCurrentWorkingDirectory().AddDirPath("editor_user_state_fake");
        static FakeStore store;
        store.canonical = directory.AddFilePath("user-state.vstate");
        editor::detail::EditorUserStateStageIdentity stageIdentity;
        for (u32 index = 0; index < sizeof(stageIdentity.nonce); ++index)
            stageIdentity.nonce[index] = static_cast<u8>(index);
        stageIdentity.saveSerial = 1;
        store.stage = editor::MakeEditorUserStateStagePath(directory, stageIdentity);
        Check(filesystem::paths::GetFileName(store.stage) ==
                  containers::StringView("user-state.vstate.stage-000102030405060708090a0b0c0d0e0f-0000000000000001"),
              "stage name uses fixed lowercase nonce and serial encoding");

        editor::EditorUserStateFailure failure;
        const editor::detail::EditorUserStateFileOps ops = MakeFakeOps(store);
        Check(editor::PublishEditorUserStateImage(ops, directory, store.canonical, store.stage, fixture.View(),
                                                 g_image, g_validation, sizeof(g_image), &failure) ==
                  editor::EditorUserStateSaveResult::Published && store.canonicalPresent && !store.stagePresent,
              "store publishes only a validated staged image");

        usize loadedBytes = 0;
        editor::EditorUserStateEnvelopeInfo info;
        Check(editor::LoadEditorUserStateImage(ops, store.canonical, fixture.project, g_validation, sizeof(g_validation),
                                              loadedBytes, info, &failure) == editor::EditorUserStateLoadResult::Restored &&
                  loadedBytes == store.canonicalBytes,
              "store reloads a published canonical image");

        const usize preservedBytes = store.canonicalBytes;
        std::memcpy(g_secondImage, store.canonicalImage, preservedBytes);

        projects::ProjectId foreignProject{{9, 8, 7, 6}};
        const editor::EditorUserStateLoadResult foreignLoad =
            editor::LoadEditorUserStateImage(ops, store.canonical, foreignProject, g_validation,
                                             sizeof(g_validation), loadedBytes, info, &failure);
        bool recoveryWritesInhibited =
            editor::detail::ShouldInhibitEditorUserStateWritesAfterLoad(foreignLoad);
        bool recoveryObserveCalled = false;
        bool recoveryPublishCalled = false;
        const auto foreignFinal = editor::detail::ExecuteEditorUserStateFinalPersistence(
            recoveryWritesInhibited,
            [&]() noexcept {
                recoveryObserveCalled = true;
                return true;
            },
            [&]() noexcept {
                recoveryPublishCalled = true;
                return true;
            });
        Check(foreignLoad == editor::EditorUserStateLoadResult::RejectedProjectMismatch &&
                  foreignFinal == editor::detail::EditorUserStateFinalPersistenceResult::WriteInhibited &&
                  !recoveryObserveCalled && !recoveryPublishCalled &&
                  store.canonicalBytes == preservedBytes &&
                  std::memcmp(store.canonicalImage, g_image, preservedBytes) == 0,
              "foreign-project canonical state is rejected and remains byte-identical through write-inhibited quiesce");
        Check(editor::LoadEditorUserStateImage(ops, store.canonical, fixture.project, g_validation,
                                              sizeof(g_validation), loadedBytes, info, &failure) ==
                  editor::EditorUserStateLoadResult::Restored,
              "same-project restart still restores the exact canonical state after a foreign-project rejection preserved it");

        StoreU16(store.canonicalImage + 8u, 2u);
        StoreU16(store.canonicalImage + 10u, 0u);
        RefreshHeaderAndDigest(store.canonicalImage, preservedBytes);
        std::memcpy(g_secondImage, store.canonicalImage, preservedBytes);
        const editor::EditorUserStateLoadResult futureLoad =
            editor::LoadEditorUserStateImage(ops, store.canonical, fixture.project, g_validation,
                                             sizeof(g_validation), loadedBytes, info, &failure);
        recoveryWritesInhibited =
            editor::detail::ShouldInhibitEditorUserStateWritesAfterLoad(futureLoad);
        recoveryPublishCalled = false;
        const auto futureFinal = editor::detail::ExecuteEditorUserStateFinalPersistence(
            recoveryWritesInhibited, []() noexcept { return true; },
            [&]() noexcept {
                recoveryPublishCalled = true;
                return true;
            });
        Check(futureLoad == editor::EditorUserStateLoadResult::RejectedFuture &&
                  futureFinal == editor::detail::EditorUserStateFinalPersistenceResult::WriteInhibited &&
                  !recoveryPublishCalled && store.canonicalBytes == preservedBytes &&
                  std::memcmp(store.canonicalImage, g_secondImage, preservedBytes) == 0,
              "future canonical state is preserved byte-identically and cannot be overwritten by final persistence");
        std::memcpy(store.canonicalImage, g_image, preservedBytes);

        store.canonicalImage[0] ^= 0x1u;
        std::memcpy(g_secondImage, store.canonicalImage, preservedBytes);
        const editor::EditorUserStateLoadResult corruptLoad =
            editor::LoadEditorUserStateImage(ops, store.canonical, fixture.project, g_validation,
                                             sizeof(g_validation), loadedBytes, info, &failure);
        recoveryWritesInhibited =
            editor::detail::ShouldInhibitEditorUserStateWritesAfterLoad(corruptLoad);
        recoveryPublishCalled = false;
        const auto corruptFinal = editor::detail::ExecuteEditorUserStateFinalPersistence(
            recoveryWritesInhibited, []() noexcept { return true; },
            [&]() noexcept {
                recoveryPublishCalled = true;
                return true;
            });
        Check(corruptLoad == editor::EditorUserStateLoadResult::RejectedCorrupt &&
                  corruptFinal == editor::detail::EditorUserStateFinalPersistenceResult::WriteInhibited &&
                  !recoveryPublishCalled &&
                  std::memcmp(store.canonicalImage, g_secondImage, preservedBytes) == 0,
              "corrupt canonical state is preserved byte-identically instead of being silently repaired on shutdown");
        std::memcpy(store.canonicalImage, g_image, preservedBytes);

        store.replace = false;
        stageIdentity.saveSerial = 2;
        store.stage = editor::MakeEditorUserStateStagePath(directory, stageIdentity);
        Check(editor::PublishEditorUserStateImage(ops, directory, store.canonical, store.stage, fixture.View(),
                                                 g_image, g_validation, sizeof(g_image), &failure) ==
                  editor::EditorUserStateSaveResult::ReplaceFailed && !store.stagePresent &&
                  store.canonicalBytes == preservedBytes && std::memcmp(store.canonicalImage, g_image, preservedBytes) == 0,
              "replace failure preserves canonical bytes and cleans only owned stage");

        store.replace = true;
        store.stagePresent = true;
        store.stageBytes = 1;
        store.stageImage[0] = 0x5a;
        store.write = filesystem::ExclusiveFileWriteResult::AlreadyExists;
        stageIdentity.saveSerial = 3;
        store.stage = editor::MakeEditorUserStateStagePath(directory, stageIdentity);
        Check(editor::PublishEditorUserStateImage(ops, directory, store.canonical, store.stage, fixture.View(),
                                                 g_image, g_validation, sizeof(g_image), &failure) ==
                  editor::EditorUserStateSaveResult::StageOpenFailed && store.stagePresent &&
                  store.stageBytes == 1 && store.stageImage[0] == 0x5a &&
                  store.canonicalBytes == preservedBytes &&
                  std::memcmp(store.canonicalImage, g_image, preservedBytes) == 0,
              "pre-existing stage collision is never treated as owned cleanup");

        store.stagePresent = false;
        store.stageBytes = 0;
        store.write = filesystem::ExclusiveFileWriteResult::OpenFailed;
        stageIdentity.saveSerial = 4;
        store.stage = editor::MakeEditorUserStateStagePath(directory, stageIdentity);
        Check(editor::PublishEditorUserStateImage(ops, directory, store.canonical, store.stage, fixture.View(),
                                                 g_image, g_validation, sizeof(g_image), &failure) ==
                  editor::EditorUserStateSaveResult::StageOpenFailed && !store.stagePresent &&
                  store.canonicalBytes == preservedBytes &&
                  std::memcmp(store.canonicalImage, g_image, preservedBytes) == 0,
              "stage open failure acquires no cleanup ownership");
        store.write = filesystem::ExclusiveFileWriteResult::UnsupportedPlatform;
        stageIdentity.saveSerial = 5;
        store.stage = editor::MakeEditorUserStateStagePath(directory, stageIdentity);
        Check(editor::PublishEditorUserStateImage(ops, directory, store.canonical, store.stage, fixture.View(),
                                                 g_image, g_validation, sizeof(g_image), &failure) ==
                  editor::EditorUserStateSaveResult::StageOpenFailed && !store.stagePresent &&
                  store.canonicalBytes == preservedBytes &&
                  std::memcmp(store.canonicalImage, g_image, preservedBytes) == 0,
              "unsupported exclusive write maps to StageOpenFailed without cleanup ownership");

        store.write = filesystem::ExclusiveFileWriteResult::Success;
        store.canonicalPresent = false;
        store.stagePresent = true;
        store.stageBytes = preservedBytes;
        std::memcpy(store.stageImage, g_image, preservedBytes);
        loadedBytes = 99;
        Check(editor::LoadEditorUserStateImage(ops, store.canonical, fixture.project, g_validation, sizeof(g_validation),
                                              loadedBytes, info, &failure) == editor::EditorUserStateLoadResult::Missing &&
                  loadedBytes == 0 && !store.canonicalPresent && store.stagePresent &&
                  store.stageBytes == preservedBytes,
              "positive missing canonical remains writable Missing state and never promotes or consumes an abandoned sibling stage");
        store.stagePresent = false;
        store.stageBytes = 0;
        store.canonicalPresent = true;
        store.canonicalRead = editor::detail::EditorUserStateFileReadResult::OpenFailed;
        const editor::EditorUserStateLoadResult openFailure =
            editor::LoadEditorUserStateImage(ops, store.canonical, fixture.project, g_validation,
                                             sizeof(g_validation), loadedBytes, info, &failure);
        recoveryWritesInhibited =
            editor::detail::ShouldInhibitEditorUserStateWritesAfterLoad(openFailure);
        recoveryPublishCalled = false;
        const auto openFailureFinal = editor::detail::ExecuteEditorUserStateFinalPersistence(
            recoveryWritesInhibited, []() noexcept { return true; },
            [&]() noexcept {
                recoveryPublishCalled = true;
                return true;
            });
        Check(openFailure == editor::EditorUserStateLoadResult::IoFailure &&
                  openFailureFinal == editor::detail::EditorUserStateFinalPersistenceResult::WriteInhibited &&
                  !recoveryPublishCalled && store.canonicalBytes == preservedBytes &&
                  std::memcmp(store.canonicalImage, g_image, preservedBytes) == 0,
              "canonical open failure is not guessed to be missing and cannot overwrite the unreadable target during quiesce");

        store.canonicalRead = editor::detail::EditorUserStateFileReadResult::ReadFailed;
        Check(editor::LoadEditorUserStateImage(ops, store.canonical, fixture.project, g_validation, sizeof(g_validation),
                                              loadedBytes, info, &failure) == editor::EditorUserStateLoadResult::IoFailure,
              "canonical read failure maps to IoFailure");
        store.canonicalRead = editor::detail::EditorUserStateFileReadResult::UnsupportedPlatform;
        Check(editor::LoadEditorUserStateImage(ops, store.canonical, fixture.project, g_validation, sizeof(g_validation),
                                              loadedBytes, info, &failure) == editor::EditorUserStateLoadResult::IoFailure,
              "unsupported canonical read maps to IoFailure");

        store.canonicalRead = editor::detail::EditorUserStateFileReadResult::LimitExceeded;
        Check(editor::LoadEditorUserStateImage(ops, store.canonical, fixture.project, g_validation, sizeof(g_validation),
                                              loadedBytes, info, &failure) == editor::EditorUserStateLoadResult::RejectedBounds,
              "canonical read bound failure maps to RejectedBounds");

        store.canonicalRead = editor::detail::EditorUserStateFileReadResult::Success;
        store.replace = true;
        store.cleanup = true;
        store.ensureDirectory = false;
        stageIdentity.saveSerial = 6;
        store.stage = editor::MakeEditorUserStateStagePath(directory, stageIdentity);
        Check(editor::PublishEditorUserStateImage(ops, directory, store.canonical, store.stage, fixture.View(),
                                                 g_image, g_validation, sizeof(g_image), &failure) ==
                  editor::EditorUserStateSaveResult::StageOpenFailed && !store.stagePresent &&
                  store.canonicalBytes == preservedBytes &&
                  std::memcmp(store.canonicalImage, g_image, preservedBytes) == 0,
              "directory creation failure maps to StageOpenFailed without stage ownership");

        store.ensureDirectory = true;
        store.write = filesystem::ExclusiveFileWriteResult::WriteFailed;
        stageIdentity.saveSerial = 7;
        store.stage = editor::MakeEditorUserStateStagePath(directory, stageIdentity);
        Check(editor::PublishEditorUserStateImage(ops, directory, store.canonical, store.stage, fixture.View(),
                                                 g_image, g_validation, sizeof(g_image), &failure) ==
                  editor::EditorUserStateSaveResult::StageWriteFailed && !store.stagePresent &&
                  store.canonicalBytes == preservedBytes &&
                  std::memcmp(store.canonicalImage, g_image, preservedBytes) == 0,
              "owned partial write failure is cleaned and maps to StageWriteFailed");

        store.write = filesystem::ExclusiveFileWriteResult::FlushFailed;
        stageIdentity.saveSerial = 8;
        store.stage = editor::MakeEditorUserStateStagePath(directory, stageIdentity);
        Check(editor::PublishEditorUserStateImage(ops, directory, store.canonical, store.stage, fixture.View(),
                                                 g_image, g_validation, sizeof(g_image), &failure) ==
                  editor::EditorUserStateSaveResult::StageFlushFailed && !store.stagePresent &&
                  store.canonicalBytes == preservedBytes &&
                  std::memcmp(store.canonicalImage, g_image, preservedBytes) == 0,
              "owned flush failure is cleaned and maps to StageFlushFailed");

        store.write = filesystem::ExclusiveFileWriteResult::Success;
        store.stageRead = editor::detail::EditorUserStateFileReadResult::ReadFailed;
        stageIdentity.saveSerial = 9;
        store.stage = editor::MakeEditorUserStateStagePath(directory, stageIdentity);
        Check(editor::PublishEditorUserStateImage(ops, directory, store.canonical, store.stage, fixture.View(),
                                                 g_image, g_validation, sizeof(g_image), &failure) ==
                  editor::EditorUserStateSaveResult::StageValidationFailed && !store.stagePresent &&
                  store.canonicalBytes == preservedBytes &&
                  std::memcmp(store.canonicalImage, g_image, preservedBytes) == 0,
              "staged reopen failure maps to StageValidationFailed and cleans owned stage");

        store.stageRead = editor::detail::EditorUserStateFileReadResult::Success;
        store.corruptStageOnRead = true;
        stageIdentity.saveSerial = 10;
        store.stage = editor::MakeEditorUserStateStagePath(directory, stageIdentity);
        Check(editor::PublishEditorUserStateImage(ops, directory, store.canonical, store.stage, fixture.View(),
                                                 g_image, g_validation, sizeof(g_image), &failure) ==
                  editor::EditorUserStateSaveResult::StageValidationFailed && !store.stagePresent &&
                  store.canonicalBytes == preservedBytes &&
                  std::memcmp(store.canonicalImage, g_image, preservedBytes) == 0,
              "staged image corruption is rejected by the same full validator and cleans owned stage");
        store.corruptStageOnRead = false;

        store.cleanup = false;
        store.write = filesystem::ExclusiveFileWriteResult::WriteFailed;
        stageIdentity.saveSerial = 11;
        store.stage = editor::MakeEditorUserStateStagePath(directory, stageIdentity);
        Check(editor::PublishEditorUserStateImage(ops, directory, store.canonical, store.stage, fixture.View(),
                                                 g_image, g_validation, sizeof(g_image), &failure) ==
                  editor::EditorUserStateSaveResult::CleanupFailed && store.stagePresent &&
                  store.canonicalBytes == preservedBytes &&
                  std::memcmp(store.canonicalImage, g_image, preservedBytes) == 0,
              "owned-stage cleanup failure overrides primary save result with CleanupFailed");
        store.cleanup = true;
        store.stagePresent = false;
        store.stageBytes = 0;
        store.write = filesystem::ExclusiveFileWriteResult::Success;
    }

    void TestPreferenceMerge() noexcept
    {
        editor::EditorUserStateFailure failure;

        editor::EditorShortcutPreferenceRecord liveShortcuts[2]{};
        liveShortcuts[0].contextType = {1};
        liveShortcuts[0].command = {10};
        liveShortcuts[0].shortcutOverride.mode = editor::EditorShortcutOverrideMode::Unbound;
        liveShortcuts[1].contextType = {2};
        liveShortcuts[1].command = {20};
        liveShortcuts[1].shortcutOverride.mode = editor::EditorShortcutOverrideMode::Unbound;
        editor::EditorShortcutPreferenceRecord restoredShortcuts[3]{};
        restoredShortcuts[0].contextType = {1};
        restoredShortcuts[0].command = {5};
        restoredShortcuts[0].shortcutOverride.mode = editor::EditorShortcutOverrideMode::Unbound;
        restoredShortcuts[1] = liveShortcuts[0];
        restoredShortcuts[2].contextType = {3};
        restoredShortcuts[2].command = {30};
        restoredShortcuts[2].shortcutOverride.mode = editor::EditorShortcutOverrideMode::Unbound;
        const editor::EditorShortcutPreferenceItemResult restoredShortcutResults[]{
            editor::EditorShortcutPreferenceItemResult::MissingCommand,
            editor::EditorShortcutPreferenceItemResult::Applied,
            editor::EditorShortcutPreferenceItemResult::MissingCommand};
        editor::EditorUserStateShortcutRecord mergedShortcuts[4]{};
        u32 mergedShortcutCount = 0;
        Check(editor::detail::MergeEditorUserStateShortcutPreferences(
                  {liveShortcuts, 2}, {restoredShortcuts, 3}, {restoredShortcutResults, 3},
                  {mergedShortcuts, 4}, mergedShortcutCount, &failure) &&
                  mergedShortcutCount == 4 &&
                  mergedShortcuts[0].contextTypeId == 1 && mergedShortcuts[0].commandId == 5 &&
                  mergedShortcuts[1].contextTypeId == 1 && mergedShortcuts[1].commandId == 10 &&
                  mergedShortcuts[2].contextTypeId == 2 && mergedShortcuts[2].commandId == 20 &&
                  mergedShortcuts[3].contextTypeId == 3 && mergedShortcuts[3].commandId == 30,
              "shortcut publication merges canonical live overrides with exact-key missing-command orphans while ignoring already-applied restore rows");

        editor::EditorShortcutPreferenceRecord collidingShortcut = liveShortcuts[0];
        const editor::EditorShortcutPreferenceItemResult collidingShortcutResult =
            editor::EditorShortcutPreferenceItemResult::MissingCommand;
        mergedShortcuts[0].commandId = 0xfeedu;
        Check(!editor::detail::MergeEditorUserStateShortcutPreferences(
                  {liveShortcuts, 2}, {&collidingShortcut, 1}, {&collidingShortcutResult, 1},
                  {mergedShortcuts, 4}, mergedShortcutCount, &failure) &&
                  failure.code == editor::EditorUserStateFailureCode::InvalidArgument &&
                  failure.section == serialization::MakeFourCC('S', 'H', 'C', 'T') &&
                  mergedShortcuts[0].commandId == 0xfeedu,
              "live shortcut colliding with a retained exact-key orphan fails publication before copying any merged prefix");

        for (u32 index = 0; index < editor::MaximumEditorUserStateShortcuts; ++index)
        {
            g_restoreShortcuts[index] = {};
            g_restoreShortcuts[index].contextType = {1};
            g_restoreShortcuts[index].command = {static_cast<u64>(index) + 1u};
            g_restoreShortcuts[index].shortcutOverride.mode = editor::EditorShortcutOverrideMode::Unbound;
        }
        editor::EditorShortcutPreferenceRecord overflowShortcut;
        overflowShortcut.contextType = {2};
        overflowShortcut.command = {1};
        overflowShortcut.shortcutOverride.mode = editor::EditorShortcutOverrideMode::Unbound;
        Check(!editor::detail::MergeEditorUserStateShortcutPreferences(
                  {g_restoreShortcuts, editor::MaximumEditorUserStateShortcuts}, {&overflowShortcut, 1},
                  {&collidingShortcutResult, 1}, {g_shortcutOverflow, editor::MaximumEditorUserStateShortcuts + 1u},
                  mergedShortcutCount, &failure) &&
                  failure.code == editor::EditorUserStateFailureCode::BoundExceeded &&
                  mergedShortcutCount == editor::MaximumEditorUserStateShortcuts + 1u,
              "live shortcut plus exact-key orphan merge refuses 2049 records instead of dropping retained intent");

        editor::EditorTypedPreferenceRecord livePreferences[2]{};
        livePreferences[0].extension = {1};
        livePreferences[0].providerId = 10;
        livePreferences[0].fieldId = 20;
        livePreferences[0].value.type = editor::EditorPreferenceValueType::Bool;
        livePreferences[0].value.value.boolean = true;
        livePreferences[1].extension = {2};
        livePreferences[1].providerId = 10;
        livePreferences[1].fieldId = 20;
        livePreferences[1].value.type = editor::EditorPreferenceValueType::SignedInteger;
        livePreferences[1].value.value.signedInteger = 7;
        editor::EditorTypedPreferenceRecord restoredPreferences[3]{};
        restoredPreferences[0].extension = {1};
        restoredPreferences[0].providerId = 9;
        restoredPreferences[0].fieldId = 30;
        restoredPreferences[0].value.type = editor::EditorPreferenceValueType::Bool;
        restoredPreferences[1] = livePreferences[0];
        restoredPreferences[2].extension = {3};
        restoredPreferences[2].providerId = 1;
        restoredPreferences[2].fieldId = 1;
        restoredPreferences[2].value.type = editor::EditorPreferenceValueType::UnsignedBitMask;
        restoredPreferences[2].value.value.unsignedBitMask = 3;
        const editor::EditorTypedPreferenceItemResult restoredPreferenceResults[]{
            editor::EditorTypedPreferenceItemResult::MissingProvider,
            editor::EditorTypedPreferenceItemResult::Valid,
            editor::EditorTypedPreferenceItemResult::MissingField};
        editor::EditorUserStatePreferenceRecord mergedPreferences[4]{};
        u32 mergedPreferenceCount = 0;
        Check(editor::detail::MergeEditorUserStateTypedPreferences(
                  {livePreferences, 2}, {restoredPreferences, 3}, {restoredPreferenceResults, 3},
                  {mergedPreferences, 4}, mergedPreferenceCount, &failure) &&
                  mergedPreferenceCount == 4 &&
                  mergedPreferences[0].extensionId == 1 && mergedPreferences[0].providerId == 9 &&
                  mergedPreferences[1].extensionId == 1 && mergedPreferences[1].providerId == 10 &&
                  mergedPreferences[2].extensionId == 2 && mergedPreferences[3].extensionId == 3,
              "typed-preference publication canonically merges live values with exact missing-provider/field orphans and ignores live-applied restore rows");

        editor::EditorTypedPreferenceRecord collidingPreference = livePreferences[0];
        const editor::EditorTypedPreferenceItemResult collidingPreferenceResult =
            editor::EditorTypedPreferenceItemResult::MissingField;
        mergedPreferences[0].fieldId = 0xbeefu;
        Check(!editor::detail::MergeEditorUserStateTypedPreferences(
                  {livePreferences, 2}, {&collidingPreference, 1}, {&collidingPreferenceResult, 1},
                  {mergedPreferences, 4}, mergedPreferenceCount, &failure) &&
                  failure.code == editor::EditorUserStateFailureCode::InvalidArgument &&
                  failure.section == serialization::MakeFourCC('P', 'R', 'E', 'F') &&
                  mergedPreferences[0].fieldId == 0xbeefu,
              "live typed preference colliding with a retained exact-key orphan fails publication before copying a merged prefix");

        for (u32 index = 0; index < editor::MaximumEditorUserStatePreferences; ++index)
        {
            g_restorePreferences[index] = {};
            g_restorePreferences[index].extension = {1};
            g_restorePreferences[index].providerId = 1;
            g_restorePreferences[index].fieldId = static_cast<u64>(index) + 1u;
            g_restorePreferences[index].value.type = editor::EditorPreferenceValueType::Bool;
        }
        editor::EditorTypedPreferenceRecord overflowPreference;
        overflowPreference.extension = {2};
        overflowPreference.providerId = 1;
        overflowPreference.fieldId = 1;
        overflowPreference.value.type = editor::EditorPreferenceValueType::Bool;
        Check(!editor::detail::MergeEditorUserStateTypedPreferences(
                  {g_restorePreferences, editor::MaximumEditorUserStatePreferences}, {&overflowPreference, 1},
                  {&collidingPreferenceResult, 1}, {g_preferenceOverflow, editor::MaximumEditorUserStatePreferences + 1u},
                  mergedPreferenceCount, &failure) &&
                  failure.code == editor::EditorUserStateFailureCode::BoundExceeded &&
                  mergedPreferenceCount == editor::MaximumEditorUserStatePreferences + 1u,
              "live typed-preference plus orphan merge refuses 513 records instead of dropping retained intent");

        editor::EditorUserStateShortcutRecord restartShortcuts[4]{};
        editor::EditorUserStatePreferenceRecord restartPreferences[4]{};
        u32 restartShortcutCount = 0;
        u32 restartPreferenceCount = 0;
        Check(editor::detail::MergeEditorUserStateShortcutPreferences(
                  {liveShortcuts, 2}, {restoredShortcuts, 3}, {restoredShortcutResults, 3},
                  {restartShortcuts, 4}, restartShortcutCount, &failure) &&
                  restartShortcutCount == 4 &&
                  editor::detail::MergeEditorUserStateTypedPreferences(
                      {livePreferences, 2}, {restoredPreferences, 3}, {restoredPreferenceResults, 3},
                      {restartPreferences, 4}, restartPreferenceCount, &failure) &&
                  restartPreferenceCount == 4,
              "restart fixture rebuilds the canonical live-plus-orphan state after failure-path sentinels");
        DocumentFixture restartFixture;
        editor::EditorUserStateDocumentView persisted = restartFixture.View();
        persisted.shortcuts = {restartShortcuts, restartShortcutCount};
        persisted.preferences = {restartPreferences, restartPreferenceCount};
        usize persistedBytes = 0;
        Check(editor::EncodeEditorUserStateImage(
                  persisted, g_secondImage, sizeof(g_secondImage), persistedBytes, &failure),
              "merged live plus orphan state can be serialized as one complete restart document");
        u32 panelCount = 0;
        u32 displayCount = 0;
        u32 imguiByteCount = 0;
        u32 shortcutCount = 0;
        u32 preferenceCount = 0;
        Check(editor::detail::DecodeEditorUserStateRestoreState(
                  g_secondImage, persistedBytes, restartFixture.project,
                  g_restorePanels, editor::MaximumEditorUserStatePanels, panelCount,
                  g_restoreDisplays, editor::MaximumEditorUserStateDisplays, displayCount,
                  g_restoreImgui, editor::MaximumEditorUserStateImguiBytes, imguiByteCount,
                  g_restoreShortcuts, editor::MaximumEditorUserStateShortcuts, shortcutCount,
                  g_restorePreferences, editor::MaximumEditorUserStatePreferences, preferenceCount,
                  &failure) &&
                  shortcutCount == 4 && preferenceCount == 4 &&
                  g_restoreShortcuts[0].contextType.value == 1 &&
                  g_restoreShortcuts[0].command.value == 5 &&
                  g_restoreShortcuts[0].shortcutOverride.mode ==
                      editor::EditorShortcutOverrideMode::Unbound &&
                  g_restorePreferences[0].extension.value == 1 &&
                  g_restorePreferences[0].providerId == 9 &&
                  g_restorePreferences[0].fieldId == 30,
              "retained shortcut/provider stable-key orphans survive canonical publication and fresh-cycle materialization without retargeting");
    }

    void TestRestoreTransactionOrdering() noexcept
    {
        using editor::detail::EditorUserStateRestoreOwnerStep;
        using editor::detail::EditorUserStateRestoreTransactionResult;

        u32 preflightCalls = 0;
        u32 ownerCalls = 0;
        EditorUserStateRestoreOwnerStep observed[5]{};
        EditorUserStateRestoreOwnerStep failedStep;
        const EditorUserStateRestoreTransactionResult rejected =
            editor::detail::ExecuteEditorUserStateRestoreTransaction(
                [&]() noexcept {
                    ++preflightCalls;
                    return false;
                },
                [&](const EditorUserStateRestoreOwnerStep step) noexcept {
                    observed[ownerCalls++] = step;
                    return true;
                },
                failedStep);
        Check(rejected == EditorUserStateRestoreTransactionResult::SemanticPreflightRejected &&
                  preflightCalls == 1 && ownerCalls == 0,
              "whole-envelope semantic preflight rejection executes zero panel/layout/shortcut/provider owner mutation");

        ownerCalls = 0;
        const EditorUserStateRestoreTransactionResult applied =
            editor::detail::ExecuteEditorUserStateRestoreTransaction(
                [&]() noexcept {
                    ++preflightCalls;
                    return true;
                },
                [&](const EditorUserStateRestoreOwnerStep step) noexcept {
                    observed[ownerCalls++] = step;
                    return true;
                },
                failedStep);
        Check(applied == EditorUserStateRestoreTransactionResult::Applied && ownerCalls == 5 &&
                  observed[0] == EditorUserStateRestoreOwnerStep::PrimaryWindow &&
                  observed[1] == EditorUserStateRestoreOwnerStep::PanelInventory &&
                  observed[2] == EditorUserStateRestoreOwnerStep::PrivateLayout &&
                  observed[3] == EditorUserStateRestoreOwnerStep::Shortcuts &&
                  observed[4] == EditorUserStateRestoreOwnerStep::TypedPreferences,
              "successful global preflight applies owners only in frozen primary-window-panel-layout-shortcut-typed-preference order");

        ownerCalls = 0;
        const EditorUserStateRestoreTransactionResult applyFailure =
            editor::detail::ExecuteEditorUserStateRestoreTransaction(
                []() noexcept { return true; },
                [&](const EditorUserStateRestoreOwnerStep step) noexcept {
                    observed[ownerCalls++] = step;
                    return step != EditorUserStateRestoreOwnerStep::Shortcuts;
                },
                failedStep);
        Check(applyFailure == EditorUserStateRestoreTransactionResult::OwnerApplyFailed &&
                  failedStep == EditorUserStateRestoreOwnerStep::Shortcuts && ownerCalls == 4 &&
                  observed[0] == EditorUserStateRestoreOwnerStep::PrimaryWindow &&
                  observed[1] == EditorUserStateRestoreOwnerStep::PanelInventory &&
                  observed[2] == EditorUserStateRestoreOwnerStep::PrivateLayout &&
                  observed[3] == EditorUserStateRestoreOwnerStep::Shortcuts,
              "post-preflight invariant failure stops at the exact owner stage and never advances to typed preference apply");
    }

    void TestDirtyPersistencePolicy() noexcept
    {
        Check(!editor::detail::ShouldInhibitEditorUserStateWritesAfterLoad(
                  editor::EditorUserStateLoadResult::Missing) &&
                  !editor::detail::ShouldInhibitEditorUserStateWritesAfterLoad(
                      editor::EditorUserStateLoadResult::Restored) &&
                  !editor::detail::ShouldInhibitEditorUserStateWritesAfterLoad(
                      editor::EditorUserStateLoadResult::Migrated) &&
                  editor::detail::ShouldInhibitEditorUserStateWritesAfterLoad(
                      editor::EditorUserStateLoadResult::NotAttempted) &&
                  editor::detail::ShouldInhibitEditorUserStateWritesAfterLoad(
                      editor::EditorUserStateLoadResult::RejectedCorrupt) &&
                  editor::detail::ShouldInhibitEditorUserStateWritesAfterLoad(
                      editor::EditorUserStateLoadResult::RejectedBounds) &&
                  editor::detail::ShouldInhibitEditorUserStateWritesAfterLoad(
                      editor::EditorUserStateLoadResult::RejectedProjectMismatch) &&
                  editor::detail::ShouldInhibitEditorUserStateWritesAfterLoad(
                      editor::EditorUserStateLoadResult::RejectedFuture) &&
                  editor::detail::ShouldInhibitEditorUserStateWritesAfterLoad(
                      editor::EditorUserStateLoadResult::IoFailure),
              "startup recovery policy leaves only positive Missing and accepted current/migrated state writable");

        editor::EditorUserStateStatus rejectedStatus;
        rejectedStatus.writesInhibited = true;
        Check(!editor::detail::ShouldObserveEditorUserStateDirtySources(rejectedStatus),
              "write-inhibited rejected/unreadable state stops automatic source observation before it can consume dirtiness or overwrite rejection evidence");
        Check(editor::detail::ValidateEditorUserStateLifecycleEntry(
                  false, editor::EditorUserStateServiceState::Ready,
                  editor::EditorUserStateServiceState::Ready) ==
                  editor::EditorUserStateFailureCode::WrongThread &&
                  editor::detail::ValidateEditorUserStateLifecycleEntry(
                      true, editor::EditorUserStateServiceState::Running,
                      editor::EditorUserStateServiceState::Ready) ==
                      editor::EditorUserStateFailureCode::WrongPhase &&
                  editor::detail::ValidateEditorUserStateLifecycleEntry(
                      true, editor::EditorUserStateServiceState::Ready,
                      editor::EditorUserStateServiceState::Ready, false) ==
                      editor::EditorUserStateFailureCode::WrongPhase &&
                  editor::detail::ValidateEditorUserStateLifecycleEntry(
                      true, editor::EditorUserStateServiceState::Ready,
                      editor::EditorUserStateServiceState::Ready) ==
                      editor::EditorUserStateFailureCode::None,
              "production lifecycle guard gives wrong-thread precedence and rejects wrong state or missing initialized dependencies");
        Check(editor::detail::IsEditorUserStatePreferenceDocumentRejection(
                  editor::EditorPreferenceFailureCode::ValidationFailed) &&
                  !editor::detail::IsEditorUserStatePreferenceDocumentRejection(
                      editor::EditorPreferenceFailureCode::CallbackFailure) &&
                  !editor::detail::IsEditorUserStatePreferenceDocumentRejection(
                      editor::EditorPreferenceFailureCode::WrongPhase) &&
                  editor::detail::MapEditorUserStatePreferenceInvariantFailure(
                      editor::EditorPreferenceFailureCode::WrongThread) ==
                      editor::EditorUserStateFailureCode::WrongThread &&
                  editor::detail::MapEditorUserStatePreferenceInvariantFailure(
                      editor::EditorPreferenceFailureCode::WrongPhase) ==
                      editor::EditorUserStateFailureCode::WrongPhase &&
                  editor::detail::MapEditorUserStatePreferenceInvariantFailure(
                      editor::EditorPreferenceFailureCode::GenerationExhausted) ==
                      editor::EditorUserStateFailureCode::GenerationExhausted &&
                  editor::detail::MapEditorUserStatePreferenceInvariantFailure(
                      editor::EditorPreferenceFailureCode::CallbackFailure) ==
                      editor::EditorUserStateFailureCode::InvalidArgument,
              "only persisted-value validation rejects the document; live provider/lifecycle failures remain internal invariant failures");

        editor::EditorUserStateStatus status;
        editor::detail::EditorUserStateDirtyTracker tracker;
        const editor::detail::EditorUserStateDirtyObservation startup{10, 20, 30, false};
        Check(editor::detail::InstallEditorUserStateDirtyBaselines(tracker, startup) &&
                  status.dirtyGeneration == 0 && tracker.panelRevision == 10 &&
                  tracker.shortcutRevision == 20 && tracker.preferenceRevision == 30,
              "startup baseline installation samples owner revisions without treating restore/default reconstruction as a user edit");
        Check(editor::detail::ObserveEditorUserStateDirtyState(status, tracker, startup, 1.0) &&
                  status.dirtyGeneration == 0 && tracker.lastDirtyRealTimeSeconds == 0.0,
              "unchanged panel/shortcut/provider/layout sources leave the shared dirty generation stable");

        const editor::detail::EditorUserStateDirtyObservation coalesced{11, 21, 31, true};
        Check(editor::detail::ObserveEditorUserStateDirtyState(status, tracker, coalesced, 5.0) &&
                  status.dirtyGeneration == 1 && tracker.lastDirtyRealTimeSeconds == 5.0,
              "one persistence observation coalesces simultaneous panel/shortcut/provider/layout changes into one dirty-generation increment");
        Check(!editor::detail::EditorUserStateAutosaveWindowReady(status, tracker, 6.9, false, 0.0) &&
                  editor::detail::EditorUserStateAutosaveWindowReady(status, tracker, 7.0, false, 0.0),
              "dirty observation starts the two-second autosave quiet window at the exact change time");
        Check(!editor::detail::EditorUserStateAutosaveReadyAfterSettling(true, true) &&
                  editor::detail::EditorUserStateAutosaveReadyAfterSettling(true, false),
              "layout settling blocks an otherwise-ready autosave until private layout state is stable");

        editor::EditorUserStateStatus checkpointStatus;
        editor::detail::EditorUserStateDirtyTracker checkpointTracker;
        Check(editor::detail::InstallEditorUserStateDirtyBaselines(checkpointTracker, startup),
              "periodic-checkpoint fixture installs startup baselines");
        for (u32 second = 1; second <= 45; ++second)
        {
            const editor::detail::EditorUserStateDirtyObservation continuous{
                10u + second, 20, 30, false};
            Check(editor::detail::ObserveEditorUserStateDirtyState(
                      checkpointStatus, checkpointTracker, continuous,
                      static_cast<f64>(second)),
                  "continuous periodic-checkpoint fixture observes each durable change");
        }
        Check(checkpointTracker.firstDirtyRealTimeSeconds == 1.0 &&
                  checkpointTracker.lastDirtyRealTimeSeconds == 45.0 &&
                  !editor::detail::EditorUserStateAutosaveWindowReady(
                      checkpointStatus, checkpointTracker, 45.9, false, 0.0) &&
                  editor::detail::EditorUserStateAutosaveWindowReady(
                      checkpointStatus, checkpointTracker, 46.0, false, 0.0),
              "continuous durable changes retain their first-dirty timestamp and force a 45-second checkpoint without requiring the two-second quiet window");

        const editor::detail::EditorUserStateDirtyObservation secondChange{11, 21, 31, true};
        Check(editor::detail::ObserveEditorUserStateDirtyState(status, tracker, secondChange, 6.0) &&
                  status.dirtyGeneration == 2 && tracker.lastDirtyRealTimeSeconds == 6.0 &&
                  !editor::detail::EditorUserStateAutosaveWindowReady(status, tracker, 7.9, false, 0.0) &&
                  editor::detail::EditorUserStateAutosaveWindowReady(status, tracker, 8.0, false, 0.0),
              "a later durable/layout change restarts debounce while still coalescing only once for that persistence tick");
        status.publishedGeneration = status.dirtyGeneration;
        Check(!editor::detail::EditorUserStateAutosaveWindowReady(status, tracker, 20.0, false, 0.0),
              "published dirty generation suppresses redundant per-frame disk publication");

        status.publishedGeneration = status.dirtyGeneration - 1u;
        Check(!editor::detail::EditorUserStateAutosaveWindowReady(status, tracker, 7.9, true, 6.0) &&
                  editor::detail::EditorUserStateAutosaveWindowReady(status, tracker, 8.0, true, 6.0),
              "failed autosave adds an independent exact two-second retry backoff while retaining the dirty generation");
        const editor::detail::EditorUserStateDirtyObservation postFailureChange{12, 21, 31, false};
        Check(editor::detail::ObserveEditorUserStateDirtyState(status, tracker, postFailureChange, 7.0) &&
                  !editor::detail::EditorUserStateAutosaveWindowReady(status, tracker, 8.9, true, 6.0) &&
                  editor::detail::EditorUserStateAutosaveWindowReady(status, tracker, 9.0, true, 6.0),
              "a newer dirty generation after save failure restarts the quiet deadline instead of retrying from the older failure deadline");

        editor::EditorUserStateStatus invalidRevisionStatus;
        editor::detail::EditorUserStateDirtyTracker invalidRevisionTracker;
        Check(editor::detail::InstallEditorUserStateDirtyBaselines(invalidRevisionTracker, startup),
              "revision-zero fixture installs a valid initial baseline");
        const editor::detail::EditorUserStateDirtyObservation exhaustedRevision{10, 20, 0, false};
        Check(!editor::detail::ObserveEditorUserStateDirtyState(
                  invalidRevisionStatus, invalidRevisionTracker, exhaustedRevision, 3.0) &&
                  invalidRevisionStatus.writesInhibited &&
                  invalidRevisionStatus.failure.code == editor::EditorUserStateFailureCode::GenerationExhausted,
              "zero panel/shortcut/provider source revision write-inhibits persistence instead of wrapping or publishing");

        editor::EditorUserStateStatus generationStatus;
        generationStatus.dirtyGeneration = ~u64{0};
        editor::detail::EditorUserStateDirtyTracker generationTracker;
        Check(editor::detail::InstallEditorUserStateDirtyBaselines(generationTracker, startup),
              "dirty-generation exhaustion fixture installs valid baselines");
        const editor::detail::EditorUserStateDirtyObservation changedPanel{11, 20, 30, false};
        Check(!editor::detail::ObserveEditorUserStateDirtyState(
                  generationStatus, generationTracker, changedPanel, 4.0) &&
                  generationStatus.writesInhibited &&
                  generationStatus.dirtyGeneration == ~u64{0},
              "dirty generation exhaustion write-inhibits without wrapping to zero");

        bool writesInhibited = false;
        bool observedLastChange = false;
        bool publishedAfterObservation = false;
        editor::EditorUserStateStatus finalStatus;
        editor::detail::EditorUserStateDirtyTracker finalTracker;
        Check(editor::detail::InstallEditorUserStateDirtyBaselines(finalTracker, startup),
              "final-persistence fixture installs startup baselines");
        const auto finalResult = editor::detail::ExecuteEditorUserStateFinalPersistence(
            writesInhibited,
            [&]() noexcept {
                observedLastChange = editor::detail::ObserveEditorUserStateDirtyState(
                    finalStatus, finalTracker, changedPanel, 9.0);
                return observedLastChange;
            },
            [&]() noexcept {
                publishedAfterObservation = observedLastChange && finalStatus.dirtyGeneration == 1;
                return publishedAfterObservation;
            });
        Check(finalResult == editor::detail::EditorUserStateFinalPersistenceResult::Published &&
                  observedLastChange && publishedAfterObservation,
              "quiesce final persistence observes the last owner change before final capture/publication");

        bool cleanObserveCalled = false;
        u32 cleanPublishCalls = 0;
        writesInhibited = false;
        const auto cleanFinal = editor::detail::ExecuteEditorUserStateFinalPersistence(
            writesInhibited,
            [&]() noexcept {
                cleanObserveCalled = true;
                return true;
            },
            [&]() noexcept {
                ++cleanPublishCalls;
                return true;
            });
        Check(cleanFinal == editor::detail::EditorUserStateFinalPersistenceResult::Published &&
                  cleanObserveCalled && cleanPublishCalls == 1,
              "writable quiesce performs exactly one final publication attempt even when no debounce/dirty-generation gate is consulted");

        cleanObserveCalled = false;
        cleanPublishCalls = 0;
        writesInhibited = true;
        const auto rejectedFinal = editor::detail::ExecuteEditorUserStateFinalPersistence(
            writesInhibited,
            [&]() noexcept {
                cleanObserveCalled = true;
                return true;
            },
            [&]() noexcept {
                ++cleanPublishCalls;
                return true;
            });
        Check(rejectedFinal == editor::detail::EditorUserStateFinalPersistenceResult::WriteInhibited &&
                  !cleanObserveCalled && cleanPublishCalls == 0,
              "canonical rejection/write-inhibit performs neither final source observation nor final publication");

        writesInhibited = false;
        cleanPublishCalls = 0;
        const auto failedFinal = editor::detail::ExecuteEditorUserStateFinalPersistence(
            writesInhibited,
            []() noexcept { return true; },
            [&]() noexcept {
                ++cleanPublishCalls;
                return false;
            });
        Check(failedFinal == editor::detail::EditorUserStateFinalPersistenceResult::PublishFailed &&
                  !writesInhibited && cleanPublishCalls == 1,
              "final publication failure is one soft attempt and does not itself strand teardown by converting the session to write-inhibited");

        writesInhibited = false;
        bool forbiddenPublishCalled = false;
        const auto inhibitedFinal = editor::detail::ExecuteEditorUserStateFinalPersistence(
            writesInhibited,
            [&]() noexcept {
                writesInhibited = true;
                return false;
            },
            [&]() noexcept {
                forbiddenPublishCalled = true;
                return true;
            });
        Check(inhibitedFinal == editor::detail::EditorUserStateFinalPersistenceResult::WriteInhibited &&
                  !forbiddenPublishCalled,
              "final source-refresh failure/write-inhibit prevents quiesce publication from bypassing the failure");
    }

    void TestPanelRestoreDecisions() noexcept
    {
        LocalPanelGenerations generations;
        editor::EditorUiFailure failure;
        const editor::detail::EditorPanelUserStateRecord records[]{
            PanelStateRecord(10, 100, "Stale Live", false),
            PanelStateRecord(10, 200, "Stale Created", true),
            PanelStateRecord(20, 300, "Rejected Singleton", true),
            PanelStateRecord(30, 400, "Missing Fallback", false)};
        editor::detail::EditorPanelUserStateRecord capturedPlaceholder{};
        bool capturedPlaceholderFound = false;

        {
            editor::EditorUiState state = editor::EditorUiState::Ready;
            editor::detail::EditorPanelRegistryCore registry(state, generations.Get());
            editor::EditorPanelTypeToken multi;
            editor::EditorPanelTypeToken singleton;
            editor::EditorPanelHandle live;
            Check(registry.RegisterType(MultiplePanelType(10, "Current Multi"), multi, &failure) &&
                      registry.RegisterType(SingletonPanelType(20, "Current Singleton", 250), singleton, &failure) &&
                      registry.CreatePanel({100, 10, "Live current", true}, live, &failure),
                  "panel restore fixture starts from current registered metadata and a live exact instance");

            editor::detail::EditorPanelUserStateRestoreStatus status;
            Check(registry.RestoreUserState({records, 4}, status, &failure) &&
                      status.restored == 1 && status.created == 1 && status.placeholders == 1 &&
                      status.skipped == 1 &&
                      status.firstSkippedFailure.code == editor::EditorUiFailureCode::SingletonViolation,
                  "panel restore applies live state, creates exact registered IDs, placeholders only missing types, and skips policy rejection");

            PanelRestoreObservation observation;
            registry.VisitPanels(ObserveRestoredPanel, &observation);
            Check(observation.liveClosed && observation.createdUsesCurrentTitle &&
                      observation.placeholderUsesFallbackTitle && observation.rejectedInstanceAbsent,
                  "panel restore keeps current metadata authoritative and fallback title exclusive to unresolved placeholders");

            editor::detail::EditorPanelUserStateRecord captured[3]{};
            u32 capturedCount = 0;
            Check(registry.CaptureUserState(captured, 3, capturedCount, &failure) && capturedCount == 3,
                  "missing-type restart fixture captures the actual live placeholder into persistent panel state");
            for (u32 index = 0; index < capturedCount; ++index)
                if (captured[index].instance == 400)
                {
                    capturedPlaceholder = captured[index];
                    capturedPlaceholderFound = true;
                    break;
                }
            Check(capturedPlaceholderFound && capturedPlaceholder.type == 30 &&
                      capturedPlaceholder.instance == 400 && !capturedPlaceholder.open &&
                      std::strcmp(capturedPlaceholder.fallbackTitle, "Missing Fallback") == 0,
                  "captured placeholder retains the exact missing type/instance identity and fallback presentation");

            editor::EditorPanelTypeToken hotReturned;
            Check(!registry.RegisterType(MultiplePanelType(30, "Returned Type"), hotReturned, &failure) &&
                      failure.code == editor::EditorUiFailureCode::DuplicateIdentity && !hotReturned.IsValid(),
                  "unresolved placeholder is not hot-replaced by a type returning in the same process");
        }

        {
            editor::EditorUiState state = editor::EditorUiState::Ready;
            editor::detail::EditorPanelRegistryCore registry(state, generations.Get());
            editor::EditorPanelTypeToken returned;
            Check(registry.RegisterType(MultiplePanelType(30, "Returned Type"), returned, &failure),
                  "later simulated restart registers the formerly missing type normally");
            editor::detail::EditorPanelUserStateRestoreStatus status;
            Check(capturedPlaceholderFound &&
                      registry.RestoreUserState({&capturedPlaceholder, 1}, status, &failure) &&
                      status.created == 1 && status.placeholders == 0 && status.skipped == 0,
                  "later simulated restart resolves the exact previously captured placeholder through ordinary registered-type creation");
            PanelRestoreObservation observation;
            registry.VisitPanels(ObserveRestoredPanel, &observation);
            Check(observation.resolvedRestartUsesCurrentTitle,
                  "later exact-ID resolution uses current returned-type metadata rather than persisted fallback text");
            editor::detail::EditorPanelUserStateRecord recaptured[1]{};
            u32 recapturedCount = 0;
            Check(registry.CaptureUserState(recaptured, 1, recapturedCount, &failure) &&
                      recapturedCount == 1 && recaptured[0].type == 30 &&
                      recaptured[0].instance == 400 &&
                      std::strcmp(recaptured[0].fallbackTitle, "Returned Type") == 0,
                  "returned owner recapture keeps the stable identity once and refreshes only current presentation metadata");
        }

        {
            editor::EditorUiState state = editor::EditorUiState::Ready;
            editor::detail::EditorPanelRegistryCore registry(state, generations.Get());
            editor::EditorPanelTypeToken type;
            editor::EditorPanelHandle live;
            Check(registry.RegisterType(MultiplePanelType(40, "Identity Owner"), type, &failure) &&
                      registry.CreatePanel({500, 40, "Identity Owner", true}, live, &failure),
                  "identity mismatch fixture creates a live instance");
            const editor::detail::EditorPanelUserStateRecord mismatch = PanelStateRecord(41, 500, "Other", false);
            editor::detail::EditorPanelUserStateRestoreStatus status;
            Check(registry.RestoreUserState({&mismatch, 1}, status, &failure) &&
                      status.skipped == 1 && status.placeholders == 0 &&
                      status.firstSkippedFailure.code == editor::EditorUiFailureCode::DuplicateIdentity &&
                      registry.IsOpen(live),
                  "live exact instance owned by another type is diagnosed and skipped without placeholder or open-state mutation");
        }

        {
            editor::EditorUiState state = editor::EditorUiState::Ready;
            editor::detail::EditorPanelRegistryCore registry(state, generations.Get());
            editor::EditorPanelTypeToken type;
            editor::EditorPanelHandle kept;
            editor::EditorPanelHandle hidden;
            Check(registry.RegisterType(MultiplePanelType(50, "Layout Panels"), type, &failure) &&
                      registry.CreatePanel({600, 50, "Kept", true}, kept, &failure) &&
                      registry.CreatePanel({601, 50, "Hidden", true}, hidden, &failure),
                  "named-layout presentation fixture creates two live panel instances");
            const editor::detail::EditorPanelUserStateRecord target =
                PanelStateRecord(50, 600, "Kept", true);
            editor::detail::EditorPanelUserStateRestoreStatus status;
            Check(registry.ReconcilePresentationUserState({&target, 1}, status, &failure) &&
                      registry.IsOpen(kept) && !registry.IsOpen(hidden),
                  "named-layout presentation reconcile hides a live instance absent from the target");
            editor::detail::EditorPanelUserStateRecord captured[2]{};
            u32 capturedCount = 0;
            Check(registry.CaptureUserState(captured, 2, capturedCount, &failure) &&
                      capturedCount == 2 && captured[0].instance == 600 && captured[0].open &&
                      captured[1].instance == 601 && !captured[1].open,
                  "named-layout presentation reconcile keeps absent instances resident instead of destroying them");
        }

        {
            editor::EditorUiState state = editor::EditorUiState::Ready;
            editor::detail::EditorPanelRegistryCore registry(state, generations.Get());
            editor::EditorPanelTypeToken type;
            editor::EditorPanelHandle live;
            Check(registry.RegisterType(MultiplePanelType(60, "Layout Identity Owner"), type, &failure) &&
                      registry.CreatePanel({700, 60, "Live", true}, live, &failure),
                  "named-layout reconcile rejection fixture creates a live instance");
            const editor::detail::EditorPanelUserStateRecord target[]{
                PanelStateRecord(60, 650, "Created Only For Failed Switch", true),
                PanelStateRecord(61, 700, "Other Type", false)};
            editor::detail::EditorPanelUserStateRestoreStatus status;
            Check(!registry.ReconcilePresentationUserState({target, 2}, status, &failure) &&
                      status.created == 1 && status.skipped == 1 &&
                      failure.code == editor::EditorUiFailureCode::DuplicateIdentity &&
                      registry.IsOpen(live),
                  "named-layout reconcile rejects a skipped target instead of reporting a partial live transaction as success");
            editor::detail::EditorPanelUserStateRecord captured[2]{};
            u32 capturedCount = 0;
            Check(registry.CaptureUserState(captured, 2, capturedCount, &failure) &&
                      capturedCount == 1 && captured[0].instance == 700 && captured[0].open,
                  "failed named-layout reconcile destroys transaction-created topology and restores the pre-switch live inventory");
        }
    }

    void TestPlacementResolver() noexcept
    {
        using editor::detail::EditorUiPlacementResolveResult;
        using editor::detail::EditorUiPrivateWindowPlacementFields;
        using editor::detail::EditorUiRepairedPlacement;
        using editor::detail::EditorUiSavedRect;

        {
            u32 quantized = 99;
            Check(!editor::detail::QuantizeEditorUiDisplayScale(1.0e-6f, quantized) &&
                      quantized == 0,
                  "positive current content scale that rounds below Q16.16 one is rejected rather than persisted as zero");
            Check(editor::detail::QuantizeEditorUiDisplayScale(1.5f, quantized) && quantized == 0x00018000u,
                  "current content scale uses exact Q16.16 rounding shared by capture and restore matching");

            const EditorUiPrivateWindowPlacementFields loaded{100, 200, 7, -9, 640, 480};
            const EditorUiSavedRect absolute = editor::detail::DecodeEditorUiPrivateWindowPlacement(loaded);
            Check(absolute == EditorUiSavedRect{107, 191, 640, 480},
                  "private detached settings decode absolute placement as ViewportPos plus nonzero relative Pos");
            EditorUiPrivateWindowPlacementFields writeback;
            Check(editor::detail::EncodeEditorUiPrivateWindowPlacement({300, 400, 500, 350}, writeback) &&
                      writeback.viewportX == 300 && writeback.viewportY == 400 &&
                      writeback.relativeX == 0 && writeback.relativeY == 0 &&
                      writeback.width == 500 && writeback.height == 350,
                  "successful private placement writeback stores repaired absolute ViewportPos, zero relative Pos, and exact Size");
            Check(!editor::detail::EncodeEditorUiPrivateWindowPlacement({32768, 0, 320, 200}, writeback) &&
                      writeback == EditorUiPrivateWindowPlacementFields{},
                  "private placement writeback refuses one-unit-outside signed-16 coordinates before narrowing");
            Check(!editor::detail::ShouldResetEditorDockLayoutAfterUserStateLoad(
                       editor::detail::EditorUiUserStateLayoutLoadResult::InvalidArgument,
                       editor::EditorUiFailureCode::WrongThread) &&
                      !editor::detail::ShouldResetEditorDockLayoutAfterUserStateLoad(
                          editor::detail::EditorUiUserStateLayoutLoadResult::InvalidArgument,
                          editor::EditorUiFailureCode::WrongPhase) &&
                      editor::detail::ShouldResetEditorDockLayoutAfterUserStateLoad(
                          editor::detail::EditorUiUserStateLayoutLoadResult::DiscardedUnusable,
                      editor::EditorUiFailureCode::None),
                  "wrong-thread/repeated-phase load rejection preserves dock suppression while a legitimate unusable load resets it");
            Check(editor::detail::IsPersistedEditorDockTreeUsable(true, true, true, true, true) &&
                      !editor::detail::IsPersistedEditorDockTreeUsable(true, true, true, false, false) &&
                      !editor::detail::IsPersistedEditorDockTreeUsable(true, false, true, true, true),
                  "pre-first-frame dock validation accepts a persisted central-node flag without requiring ImGui's derived root CentralNode pointer");
        }

        {
            const editor::detail::EditorUiSavedDisplay saved[]{
                SavedDisplay(0, 0, 1920, 1080, 0, 0, 1920, 1040, 1920, 1080, 0x00010000u, true),
                SavedDisplay(1920, 0, 1280, 1024, 1920, 0, 1280, 984, 2560, 2048, 0x00018000u, false)};
            const window::DisplaySnapshot current[]{
                CurrentDisplay(0, 1920, 0, 1280, 1024, 1920, 0, 1280, 984, 2560, 2048, 1.5f, false, 0xaaaau),
                CurrentDisplay(1, 0, 0, 1920, 1080, 0, 0, 1920, 1040, 1920, 1080, 1.0f, true, 0xbbbbu)};
            const EditorUiSavedRect rect{2000, 100, 640, 480};
            EditorUiRepairedPlacement repaired;
            Check(editor::detail::ResolveEditorUiSavedPlacement({saved, 2}, {current, 2}, rect, repaired) ==
                      EditorUiPlacementResolveResult::Resolved &&
                      repaired.savedDisplayIndex == 1 && repaired.currentDisplayIndex == 0 && repaired.rect == rect,
                  "reordered current displays match by stable geometry/pixel/scale evidence rather than enumeration order");

            window::DisplaySnapshot reversed[]{current[1], current[0]};
            reversed[1].fingerprint = 0xccccu;
            reversed[1].topologyRevision = 991;
            Check(editor::detail::ResolveEditorUiSavedPlacement({saved, 2}, {reversed, 2}, rect, repaired) ==
                      EditorUiPlacementResolveResult::Resolved &&
                      repaired.currentDisplayIndex == 1 && repaired.rect == rect,
                  "runtime fingerprint/topology revision changes do not become persistent display identity");
        }

        {
            const editor::detail::EditorUiSavedDisplay saved[]{
                SavedDisplay(0, 0, 1920, 1000, 0, 0, 1920, 1000, 1920, 1000, 0x00010000u, true),
                SavedDisplay(1920, 0, 1280, 1000, 1920, 0, 1280, 1000, 1280, 1000, 0x00018000u, false)};
            const window::DisplaySnapshot current[]{
                CurrentDisplay(0, 0, 0, 1600, 800, 0, 0, 1600, 800, 1600, 800, 1.25f, true, 0x101u)};
            EditorUiRepairedPlacement repaired;
            Check(editor::detail::ResolveEditorUiSavedPlacement(
                      {saved, 2}, {current, 1}, {2240, 250, 640, 500}, repaired) ==
                      EditorUiPlacementResolveResult::Resolved &&
                      repaired.savedDisplayIndex == 1 && repaired.currentDisplayIndex == 0 &&
                      repaired.rect == EditorUiSavedRect{400, 200, 800, 400},
                  "removed saved secondary display remaps its detached rectangle into the current primary work area using relative geometry");
        }

        {
            const editor::detail::EditorUiSavedDisplay saved[]{
                SavedDisplay(0, 0, 1600, 900, 0, 0, 1600, 900, 1600, 900, 0x00010000u, true)};
            const window::DisplaySnapshot current[]{
                CurrentDisplay(0, 100, 50, 1280, 700, 100, 50, 1280, 700, 1920, 1080, 1.5f, true, 0x202u)};
            EditorUiRepairedPlacement repaired;
            Check(editor::detail::ResolveEditorUiSavedPlacement(
                      {saved, 1}, {current, 1}, {400, 225, 800, 450}, repaired) ==
                      EditorUiPlacementResolveResult::Resolved &&
                      repaired.currentDisplayIndex == 0 &&
                      repaired.rect == EditorUiSavedRect{420, 225, 640, 350},
                  "same logical display after DPI/work-area change reconstructs placement from current work facts without a saved-DPI multiplier");
        }

        {
            const editor::detail::EditorUiSavedDisplay saved[]{
                SavedDisplay(-3000, 0, 800, 800, -3000, 0, 800, 800, 800, 800, 0x10000u, true),
                SavedDisplay(1000, 0, 1000, 1000, 1000, 0, 1000, 1000, 1000, 1000, 0x10000u, false),
                SavedDisplay(0, 0, 1000, 1000, 0, 0, 1000, 1000, 1000, 1000, 0x10000u, false)};
            const window::DisplaySnapshot current[]{
                CurrentDisplay(0, 0, 0, 2000, 1000, 0, 0, 2000, 1000, 2000, 1000, 1.0f, true, 10)};
            const EditorUiSavedRect rect{900, 100, 200, 200};
            EditorUiRepairedPlacement repaired;
            Check(editor::detail::ResolveEditorUiSavedPlacement({saved, 3}, {current, 1}, rect, repaired) ==
                      EditorUiPlacementResolveResult::Resolved && repaired.savedDisplayIndex == 2,
                  "saved-display overlap/center/primary tie ends in canonical saved-display ordering");
            const editor::detail::EditorUiSavedDisplay reordered[]{saved[2], saved[0], saved[1]};
            Check(editor::detail::ResolveEditorUiSavedPlacement({reordered, 3}, {current, 1}, rect, repaired) ==
                      EditorUiPlacementResolveResult::Resolved && repaired.savedDisplayIndex == 0,
                  "canonical saved-display tie result is invariant to supplied saved enumeration order");

            const EditorUiSavedRect nowhere{7000, 7000, 400, 300};
            Check(editor::detail::ResolveEditorUiSavedPlacement({saved, 3}, {current, 1}, nowhere, repaired) ==
                      EditorUiPlacementResolveResult::Resolved && repaired.savedDisplayIndex == 0,
                  "all-zero saved-display intersection falls back to the saved primary");
        }

        {
            const editor::detail::EditorUiSavedDisplay centerTie[]{
                SavedDisplay(-3000, 0, 800, 800, -3000, 0, 800, 800, 800, 800, 0x10000u, true),
                SavedDisplay(450, 0, 100, 1000, 450, 0, 100, 1000, 100, 1000, 0x10000u, false),
                SavedDisplay(300, 0, 100, 1000, 300, 0, 100, 1000, 100, 1000, 0x10000u, false)};
            const window::DisplaySnapshot current[]{
                CurrentDisplay(0, 0, 0, 1000, 1000, 0, 0, 1000, 1000, 1000, 1000, 1.0f, true, 11)};
            EditorUiRepairedPlacement repaired;
            Check(editor::detail::ResolveEditorUiSavedPlacement({centerTie, 3}, {current, 1},
                                                                 {300, 100, 400, 200}, repaired) ==
                      EditorUiPlacementResolveResult::Resolved && repaired.savedDisplayIndex == 1,
                  "equal saved-display intersection prefers the work area containing the saved rectangle center");

            const editor::detail::EditorUiSavedDisplay primaryTie[]{
                SavedDisplay(0, 0, 1000, 1000, 0, 0, 1000, 1000, 1000, 1000, 0x10000u, false),
                SavedDisplay(0, 0, 1000, 1000, 0, 0, 1000, 1000, 1000, 1000, 0x10000u, true)};
            Check(editor::detail::ResolveEditorUiSavedPlacement({primaryTie, 2}, {current, 1},
                                                                 {100, 100, 400, 200}, repaired) ==
                      EditorUiPlacementResolveResult::Resolved && repaired.savedDisplayIndex == 1,
                  "saved-display overlap/center tie prefers the saved primary before canonical ordering");
        }

        {
            const editor::detail::EditorUiSavedDisplay saved[]{
                SavedDisplay(-3000, 0, 1600, 900, -3000, 0, 1600, 860, 1600, 900, 0x10000u, true),
                SavedDisplay(0, 0, 1000, 800, 0, 0, 1000, 760, 1500, 1200, 0x18000u, false)};
            const EditorUiSavedRect rect{100, 100, 500, 300};
            const window::DisplaySnapshot unique[]{
                CurrentDisplay(0, -3000, 0, 1600, 900, -3000, 0, 1600, 860, 1600, 900, 1.0f, true, 21),
                CurrentDisplay(1, 0, 0, 1000, 800, 0, 0, 1000, 760, 1500, 1200, 1.0f, false, 22),
                CurrentDisplay(2, 1000, 0, 1000, 800, 1000, 0, 1000, 760, 1500, 1200, 1.5f, false, 23)};
            EditorUiRepairedPlacement repaired;
            Check(editor::detail::ResolveEditorUiSavedPlacement({saved, 2}, {unique, 3}, rect, repaired) ==
                      EditorUiPlacementResolveResult::Resolved && repaired.currentDisplayIndex == 2,
                  "current-display similarity tuple uses exact Q16.16 scale after higher-priority dimensions tie");

            const window::DisplaySnapshot overlapTie[]{
                CurrentDisplay(0, -3000, 0, 1600, 900, -3000, 0, 1600, 860, 1600, 900, 1.0f, true, 31),
                CurrentDisplay(1, 0, 0, 1000, 800, 0, 0, 1000, 760, 1500, 1200, 1.5f, false, 32),
                CurrentDisplay(2, 500, 0, 1000, 800, 500, 0, 1000, 760, 1500, 1200, 1.5f, false, 33)};
            Check(editor::detail::ResolveEditorUiSavedPlacement({saved, 2}, {overlapTie, 3}, rect, repaired) ==
                      EditorUiPlacementResolveResult::Resolved && repaired.currentDisplayIndex == 1,
                  "tied current similarity uses the unique greatest positive current-work/saved-work overlap");

            const window::DisplaySnapshot zeroTie[]{
                CurrentDisplay(0, -3000, 0, 1600, 900, -3000, 0, 1600, 860, 1600, 900, 1.0f, true, 41),
                CurrentDisplay(1, 3000, 0, 1000, 800, 3000, 0, 1000, 760, 1500, 1200, 1.5f, false, 42),
                CurrentDisplay(2, 5000, 0, 1000, 800, 5000, 0, 1000, 760, 1500, 1200, 1.5f, false, 43)};
            Check(editor::detail::ResolveEditorUiSavedPlacement({saved, 2}, {zeroTie, 3}, rect, repaired) ==
                      EditorUiPlacementResolveResult::Resolved && repaired.currentDisplayIndex == 0,
                  "unresolved/all-zero tied current candidates fall back to the unique current primary");
        }

        {
            const editor::detail::EditorUiSavedDisplay saved[]{
                SavedDisplay(0, 0, 1000, 1000, 0, 0, 1000, 1000, 1000, 1000, 0x10000u, true)};
            window::DisplaySnapshot noPrimary[]{
                CurrentDisplay(0, 0, 0, 1000, 1000, 0, 0, 1000, 1000, 1000, 1000, 1.0f, false, 51)};
            EditorUiRepairedPlacement repaired;
            Check(editor::detail::ResolveEditorUiSavedPlacement({saved, 1}, {noPrimary, 1}, {10, 10, 400, 300}, repaired) ==
                      EditorUiPlacementResolveResult::EnvironmentUnavailable,
                  "restore rejects current topology with zero primary displays");
            window::DisplaySnapshot multiplePrimary[]{
                CurrentDisplay(0, 0, 0, 1000, 1000, 0, 0, 1000, 1000, 1000, 1000, 1.0f, true, 52),
                CurrentDisplay(1, 1000, 0, 1000, 1000, 1000, 0, 1000, 1000, 1000, 1000, 1.0f, true, 53)};
            Check(editor::detail::ResolveEditorUiSavedPlacement({saved, 1}, {multiplePrimary, 2}, {10, 10, 400, 300}, repaired) ==
                      EditorUiPlacementResolveResult::EnvironmentUnavailable,
                  "restore rejects synthetic current topology with multiple primary displays");
        }

        {
            const editor::detail::EditorUiSavedDisplay saved[]{
                SavedDisplay(0, 0, 1000, 800, 0, 0, 1000, 800, 1000, 800, 0x10000u, true)};
            const window::DisplaySnapshot current[]{
                CurrentDisplay(0, 100, 50, 1500, 900, 100, 50, 1500, 900, 1500, 900, 1.0f, true, 61)};
            EditorUiRepairedPlacement repaired;
            Check(editor::detail::ResolveEditorUiSavedPlacement({saved, 1}, {current, 1}, {333, 200, 333, 200}, repaired) ==
                      EditorUiPlacementResolveResult::Resolved &&
                      repaired.rect.x == 600 && repaired.rect.y == 275 &&
                      repaired.rect.width == 500 && repaired.rect.height == 225,
                  "placement reconstruction uses f64 relative geometry and std::llround in current logical work units");
            Check(editor::detail::ResolveEditorUiSavedPlacement({saved, 1}, {current, 1}, {100, 100, 10, 10}, repaired) ==
                      EditorUiPlacementResolveResult::Resolved &&
                      repaired.rect.width == 320 && repaired.rect.height == 200,
                  "normal detached placement enforces the 320x200 minimum when the current work area can fit it");
            Check(editor::detail::ResolveEditorUiSavedPlacement({saved, 1}, {current, 1}, {9000, -9000, 5000, 5000}, repaired) ==
                      EditorUiPlacementResolveResult::Resolved &&
                      repaired.rect.x == 100 && repaired.rect.y == 50 &&
                      repaired.rect.width == 1500 && repaired.rect.height == 900,
                  "extreme offscreen/oversized restored rectangle is fit-clamped wholly inside current work area");
        }

        {
            const editor::detail::EditorUiSavedDisplay saved[]{
                SavedDisplay(0, 0, 1000, 800, 0, 0, 1000, 800, 1000, 800, 0x10000u, true)};
            const window::DisplaySnapshot current[]{
                CurrentDisplay(0, 10, 20, 200, 100, 10, 20, 200, 100, 200, 100, 1.0f, true, 71)};
            EditorUiRepairedPlacement repaired;
            Check(editor::detail::ResolveEditorUiSavedPlacement({saved, 1}, {current, 1}, {100, 100, 10, 10}, repaired) ==
                      EditorUiPlacementResolveResult::Resolved &&
                      repaired.rect.width == 200 && repaired.rect.height == 100 &&
                      repaired.rect.x == 10 && repaired.rect.y == 20,
                  "work area smaller than 320x200 minimum clamps restored extent to the full current work area");
            Check(editor::detail::ResolveEditorUiSavedPlacement({saved, 1}, {current, 1}, {0, 0, 0, 0}, repaired) ==
                      EditorUiPlacementResolveResult::Resolved &&
                      repaired.rect == EditorUiSavedRect{10, 20, 200, 100},
                  "degenerate saved rectangle uses centered 440x340 fallback fitted to current primary work area");
        }

        {
            const editor::detail::EditorUiSavedDisplay saved[]{
                SavedDisplay(0, 0, 1000, 1000, 0, 0, 1000, 1000, 1000, 1000, 0x10000u, true)};
            EditorUiRepairedPlacement repaired;
            const window::DisplaySnapshot minOrigin[]{
                CurrentDisplay(0, -32768, 0, 320, 300, -32768, 0, 320, 300, 320, 300, 1.0f, true, 81)};
            Check(editor::detail::ResolveEditorUiSavedPlacement({saved, 1}, {minOrigin, 1}, {0, 0, 1000, 1000}, repaired) ==
                      EditorUiPlacementResolveResult::Resolved && repaired.rect.x == -32768,
                  "signed-16 minimum restored origin is representable");
            const window::DisplaySnapshot belowMin[]{
                CurrentDisplay(0, -32769, 0, 320, 300, -32769, 0, 320, 300, 320, 300, 1.0f, true, 82)};
            Check(editor::detail::ResolveEditorUiSavedPlacement({saved, 1}, {belowMin, 1}, {0, 0, 1000, 1000}, repaired) ==
                      EditorUiPlacementResolveResult::EnvironmentUnavailable,
                  "one unit below signed-16 restored origin rejects the whole private layout");
            const window::DisplaySnapshot maxOrigin[]{
                CurrentDisplay(0, 32767, 0, 320, 300, 32767, 0, 320, 300, 320, 300, 1.0f, true, 83)};
            Check(editor::detail::ResolveEditorUiSavedPlacement({saved, 1}, {maxOrigin, 1}, {0, 0, 1000, 1000}, repaired) ==
                      EditorUiPlacementResolveResult::Resolved && repaired.rect.x == 32767,
                  "signed-16 maximum restored origin is representable");
            const window::DisplaySnapshot aboveMax[]{
                CurrentDisplay(0, 32768, 0, 320, 300, 32768, 0, 320, 300, 320, 300, 1.0f, true, 84)};
            Check(editor::detail::ResolveEditorUiSavedPlacement({saved, 1}, {aboveMax, 1}, {0, 0, 1000, 1000}, repaired) ==
                      EditorUiPlacementResolveResult::EnvironmentUnavailable,
                  "one unit above signed-16 restored origin rejects the whole private layout");
            Check(editor::detail::ResolveEditorUiSavedPlacement(
                      {saved, 1}, {aboveMax, 1}, {0, 0, 1000, 1000}, repaired, false) ==
                      EditorUiPlacementResolveResult::Resolved && repaired.rect.x == 32768,
                  "primary-window placement reuses topology repair without ImGui signed-16 narrowing");

            const window::DisplaySnapshot maxSize[]{
                CurrentDisplay(0, 0, 0, 32767, 1000, 0, 0, 32767, 1000, 32767, 1000, 1.0f, true, 85)};
            Check(editor::detail::ResolveEditorUiSavedPlacement({saved, 1}, {maxSize, 1}, {0, 0, 1000, 1000}, repaired) ==
                      EditorUiPlacementResolveResult::Resolved && repaired.rect.width == 32767,
                  "signed-16 maximum restored size is representable");
            const window::DisplaySnapshot aboveSize[]{
                CurrentDisplay(0, 0, 0, 32768, 1000, 0, 0, 32768, 1000, 32768, 1000, 1.0f, true, 86)};
            Check(editor::detail::ResolveEditorUiSavedPlacement({saved, 1}, {aboveSize, 1}, {0, 0, 1000, 1000}, repaired) ==
                      EditorUiPlacementResolveResult::EnvironmentUnavailable,
                  "restored size 32768 rejects before ImGui signed-16 narrowing");
        }
    }
} // namespace

int main()
{
    vanguard::concurrency::InitializeMainThread();
    Check(vanguard::memory::Initialize(), "memory initialization");
    Check(vanguard::diagnostics::Initialize(vanguard::diagnostics::Mode::Synchronous, "editorUserStateTests"),
          "diagnostics initialization");
    Check(vanguard::containers::Initialize(), "containers initialization");
    Check(vanguard::io::Initialize(), "I/O initialization");
    const vanguard::filesystem::AbsolutePath workingDirectory =
        vanguard::filesystem::paths::GetCurrentWorkingDirectory();
    Check(vanguard::filesystem::Initialize({workingDirectory, workingDirectory, workingDirectory}),
          "filesystem initialization");

    TestCodec();
    TestTextCodec();
    TestNamedLayoutTextCodec();
    TestPanelStateTextValidation();
    TestArtifactStore();
    TestStore();
    TestPreferenceMerge();
    TestRestoreTransactionOrdering();
    TestDirtyPersistencePolicy();
    TestPanelRestoreDecisions();
    TestPlacementResolver();

    vanguard::filesystem::Shutdown();
    vanguard::io::Shutdown();
    vanguard::diagnostics::Shutdown();
    if (g_failures == 0)
        std::printf("[editorUserStateTests] bounded VUSR codec/store source oracle passed\n");
    return g_failures == 0 ? 0 : 1;
}
