#include <vanguard/editor/editor_user_state_service.hpp>

#include <vanguard/editor/editor_commands.hpp>
#include <vanguard/editor/editor_panels.hpp>
#include <vanguard/editor/editor_project_service.hpp>
#include <vanguard/editor/editor_state_artifact_store.hpp>
#include <vanguard/editor/editor_ui.hpp>
#include <vanguard/editor/editor_ui_persistence.hpp>
#include <vanguard/editor/editor_user_state_text_codec.hpp>

#include <vanguard/concurrency/thread.hpp>
#include <vanguard/crypto/crypto.hpp>
#include <vanguard/engine/engine_services.hpp>
#include <vanguard/engine/window_service.hpp>
#include <vanguard/input/input.hpp>
#include <vanguard/memory/memory.hpp>
#include <vanguard/memory/pool.hpp>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <new>
#include <type_traits>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace
{
    namespace app = vanguard::application;
    namespace editor = vanguard::editor;
    namespace engine = vanguard::engine;
    namespace filesystem = vanguard::filesystem;
    namespace serialization = vanguard::serialization;
    using namespace vanguard;

    constexpr serialization::Version UserStateVersion{1, 0};
    constexpr serialization::VersionRange UserStateVersionRange{1, 0, 0};
    constexpr u64 UserStateDomain = 0x6564757374617465ull;
    constexpr u32 UserStateMagic = serialization::MakeFourCC('V', 'U', 'S', 'R');
    constexpr u32 MetaSection = serialization::MakeFourCC('M', 'E', 'T', 'A');
    constexpr u32 PanelsSection = serialization::MakeFourCC('P', 'N', 'L', 'S');
    constexpr u32 UiStateSection = serialization::MakeFourCC('U', 'I', 'S', 'T');
    constexpr u32 ShortcutsSection = serialization::MakeFourCC('S', 'H', 'C', 'T');
    constexpr u32 PreferencesSection = serialization::MakeFourCC('P', 'R', 'E', 'F');
    constexpr u32 SectionCount = 5;
    constexpr u32 SectionAlignment = 8;
    constexpr u32 DigestBytes = crypto::Digest256::ByteCount;

    class NamedLayoutIndexLease final
    {
    public:
        NamedLayoutIndexLease() noexcept
        {
#if defined(_WIN32)
            wchar_t mutexName[96]{};
            HANDLE token = nullptr;
            if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &token))
                return;
            BYTE tokenBytes[256]{};
            DWORD required = 0;
            const bool haveUser = ::GetTokenInformation(
                token, TokenUser, tokenBytes, sizeof(tokenBytes), &required) != FALSE;
            ::CloseHandle(token);
            if (!haveUser)
                return;
            const auto* const user = reinterpret_cast<const TOKEN_USER*>(tokenBytes);
            if (!::IsValidSid(user->User.Sid))
                return;
            const DWORD sidBytes = ::GetLengthSid(user->User.Sid);
            const auto* const sid = static_cast<const u8*>(user->User.Sid);
            u64 sidHash = 14695981039346656037ull;
            for (DWORD index = 0; index < sidBytes; ++index)
            {
                sidHash ^= sid[index];
                sidHash *= 1099511628211ull;
            }
            if (_snwprintf_s(mutexName, _countof(mutexName), _TRUNCATE,
                             L"Global\\VanguardEditorNamedLayoutIndex-%016llx",
                             static_cast<unsigned long long>(sidHash)) <= 0)
                return;
            m_mutex = ::CreateMutexW(nullptr, FALSE, mutexName);
            if (m_mutex == nullptr)
                return;
            const DWORD wait = ::WaitForSingleObject(m_mutex, 0);
            m_acquired = wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED;
            m_busy = wait == WAIT_TIMEOUT;
#endif
        }

        NamedLayoutIndexLease(const NamedLayoutIndexLease&) = delete;
        NamedLayoutIndexLease& operator=(const NamedLayoutIndexLease&) = delete;

        ~NamedLayoutIndexLease() noexcept
        {
#if defined(_WIN32)
            if (m_acquired)
                ::ReleaseMutex(m_mutex);
            if (m_mutex != nullptr)
                ::CloseHandle(m_mutex);
#endif
        }

        [[nodiscard]] bool Acquired() const noexcept { return m_acquired; }
        [[nodiscard]] bool Busy() const noexcept { return m_busy; }

    private:
#if defined(_WIN32)
        HANDLE m_mutex = nullptr;
#endif
        bool m_acquired = false;
        bool m_busy = false;
    };
    constexpr serialization::DocumentFlags UserStateDocumentFlags =
        serialization::DocumentFlags::HasEditorData | serialization::DocumentFlags::Deterministic;
    constexpr serialization::SectionFlags UserStateSectionFlags = serialization::SectionFlags::EditorOnly;
    constexpr u32 SectionIds[SectionCount]{MetaSection, PanelsSection, UiStateSection, ShortcutsSection, PreferencesSection};

    [[nodiscard]] constexpr u64 AbsDiff(const u64 left, const u64 right) noexcept
    {
        return left >= right ? left - right : right - left;
    }

    [[nodiscard]] constexpr u64 RectIntersectionArea(const i64 ax, const i64 ay, const u64 aw, const u64 ah,
                                                     const i64 bx, const i64 by, const u64 bw, const u64 bh) noexcept
    {
        const i64 aRight = ax + static_cast<i64>(aw);
        const i64 aBottom = ay + static_cast<i64>(ah);
        const i64 bRight = bx + static_cast<i64>(bw);
        const i64 bBottom = by + static_cast<i64>(bh);
        const i64 left = ax > bx ? ax : bx;
        const i64 top = ay > by ? ay : by;
        const i64 right = aRight < bRight ? aRight : bRight;
        const i64 bottom = aBottom < bBottom ? aBottom : bBottom;
        if (right <= left || bottom <= top)
            return 0;
        return static_cast<u64>(right - left) * static_cast<u64>(bottom - top);
    }

    [[nodiscard]] constexpr bool ContainsRectCenter(const editor::detail::EditorUiSavedDisplay& display,
                                                    const editor::detail::EditorUiSavedRect& rect) noexcept
    {
        const i64 centerX2 = static_cast<i64>(rect.x) * 2 + rect.width;
        const i64 centerY2 = static_cast<i64>(rect.y) * 2 + rect.height;
        const i64 left2 = static_cast<i64>(display.workX) * 2;
        const i64 top2 = static_cast<i64>(display.workY) * 2;
        const i64 right2 = (static_cast<i64>(display.workX) + display.workWidth) * 2;
        const i64 bottom2 = (static_cast<i64>(display.workY) + display.workHeight) * 2;
        return centerX2 >= left2 && centerX2 <= right2 && centerY2 >= top2 && centerY2 <= bottom2;
    }

    [[nodiscard]] bool ValidSavedTopology(const containers::ArraySpan<const editor::detail::EditorUiSavedDisplay> displays,
                                          u32& primaryIndex) noexcept
    {
        primaryIndex = ~u32{0};
        if (displays.Empty() || displays.Size() > editor::MaximumEditorUserStateDisplays)
            return false;
        u32 primaryCount = 0;
        for (u32 index = 0; index < displays.Size(); ++index)
        {
            const auto& display = displays[index];
            if (display.boundsWidth == 0 || display.boundsHeight == 0 || display.workWidth == 0 ||
                display.workHeight == 0 || display.desktopPixelWidth == 0 || display.desktopPixelHeight == 0 ||
                display.scaleQ16_16 == 0 || display.scaleQ16_16 > 0x00100000u)
                return false;
            if (display.primary)
            {
                primaryIndex = index;
                ++primaryCount;
            }
        }
        return primaryCount == 1;
    }

    [[nodiscard]] bool ValidCurrentTopology(const containers::ArraySpan<const window::DisplaySnapshot> displays,
                                            u32& primaryIndex, u32 (&scales)[window::MaximumDisplays]) noexcept
    {
        primaryIndex = ~u32{0};
        if (displays.Empty() || displays.Size() > window::MaximumDisplays)
            return false;
        u32 primaryCount = 0;
        for (u32 index = 0; index < displays.Size(); ++index)
        {
            const window::DisplaySnapshot& display = displays[index];
            if (!display.handle.IsValid() || display.fingerprint == 0 || !display.bounds.extent.IsValid() ||
                !display.workArea.extent.IsValid() || !display.desktopPixelExtent.IsValid() ||
                display.desktopRefreshRate.denominator == 0 ||
                !editor::detail::QuantizeEditorUiDisplayScale(display.contentScale, scales[index]))
                return false;
            for (u32 previous = 0; previous < index; ++previous)
                if (displays[previous].handle == display.handle)
                    return false;
            if (display.primary)
            {
                primaryIndex = index;
                ++primaryCount;
            }
        }
        return primaryCount == 1;
    }

    [[nodiscard]] bool SimilarityLess(const u64 (&left)[8], const u64 (&right)[8]) noexcept
    {
        for (u32 index = 0; index < 8; ++index)
        {
            if (left[index] < right[index])
                return true;
            if (left[index] > right[index])
                return false;
        }
        return false;
    }

    [[nodiscard]] bool SimilarityEqual(const u64 (&left)[8], const u64 (&right)[8]) noexcept
    {
        for (u32 index = 0; index < 8; ++index)
            if (left[index] != right[index])
                return false;
        return true;
    }

    void BuildSimilarity(const editor::detail::EditorUiSavedDisplay& saved, const window::DisplaySnapshot& current,
                         const u32 currentScale, u64 (&tuple)[8]) noexcept
    {
        tuple[0] = saved.primary == current.primary ? 0u : 1u;
        tuple[1] = AbsDiff(saved.workWidth, current.workArea.extent.width);
        tuple[2] = AbsDiff(saved.workHeight, current.workArea.extent.height);
        tuple[3] = AbsDiff(saved.desktopPixelWidth, current.desktopPixelExtent.width);
        tuple[4] = AbsDiff(saved.desktopPixelHeight, current.desktopPixelExtent.height);
        tuple[5] = AbsDiff(saved.scaleQ16_16, currentScale);
        tuple[6] = AbsDiff(saved.boundsWidth, current.bounds.extent.width);
        tuple[7] = AbsDiff(saved.boundsHeight, current.bounds.extent.height);
    }

    [[nodiscard]] bool SavedDisplayLess(const editor::detail::EditorUiSavedDisplay& left,
                                        const editor::detail::EditorUiSavedDisplay& right) noexcept
    {
        if (left.primary != right.primary)
            return left.primary && !right.primary;
        if (left.boundsX != right.boundsX) return left.boundsX < right.boundsX;
        if (left.boundsY != right.boundsY) return left.boundsY < right.boundsY;
        if (left.boundsWidth != right.boundsWidth) return left.boundsWidth < right.boundsWidth;
        if (left.boundsHeight != right.boundsHeight) return left.boundsHeight < right.boundsHeight;
        if (left.workX != right.workX) return left.workX < right.workX;
        if (left.workY != right.workY) return left.workY < right.workY;
        if (left.workWidth != right.workWidth) return left.workWidth < right.workWidth;
        if (left.workHeight != right.workHeight) return left.workHeight < right.workHeight;
        if (left.desktopPixelWidth != right.desktopPixelWidth) return left.desktopPixelWidth < right.desktopPixelWidth;
        if (left.desktopPixelHeight != right.desktopPixelHeight) return left.desktopPixelHeight < right.desktopPixelHeight;
        return left.scaleQ16_16 < right.scaleQ16_16;
    }

    [[nodiscard]] constexpr u16 LoadU16Le(const u8* const bytes) noexcept
    {
        return static_cast<u16>(bytes[0]) | static_cast<u16>(static_cast<u16>(bytes[1]) << 8u);
    }

    [[nodiscard]] constexpr u32 LoadU32Le(const u8* const bytes) noexcept
    {
        return static_cast<u32>(bytes[0]) | (static_cast<u32>(bytes[1]) << 8u) |
               (static_cast<u32>(bytes[2]) << 16u) | (static_cast<u32>(bytes[3]) << 24u);
    }

    [[nodiscard]] constexpr u64 LoadU64Le(const u8* const bytes) noexcept
    {
        return static_cast<u64>(LoadU32Le(bytes)) | (static_cast<u64>(LoadU32Le(bytes + 4u)) << 32u);
    }

    [[nodiscard]] constexpr bool AddWouldOverflow(const u64 left, const u64 right) noexcept
    {
        return right > ~u64{0} - left;
    }

    [[nodiscard]] constexpr u64 Align8(const u64 value) noexcept
    {
        return (value + 7u) & ~u64{7};
    }

    void ClearFailure(editor::EditorUserStateFailure* const failure) noexcept
    {
        if (failure != nullptr)
            *failure = {};
    }

    void SetFailure(editor::EditorUserStateFailure* const failure, const editor::EditorUserStateFailureCode code,
                    const u32 section = 0, const u32 record = 0, const u32 detail = 0) noexcept
    {
        if (failure != nullptr)
            *failure = {code, section, record, detail};
    }

    [[nodiscard]] editor::EditorUserStateLoadResult MapSerializationFailure(const serialization::Result result,
                                                                            editor::EditorUserStateFailure* const failure) noexcept
    {
        switch (result)
        {
        case serialization::Result::LimitExceeded:
            SetFailure(failure, editor::EditorUserStateFailureCode::BoundExceeded, 0, 0, static_cast<u32>(result));
            return editor::EditorUserStateLoadResult::RejectedBounds;
        case serialization::Result::UnsupportedByteOrder:
        case serialization::Result::UnsupportedHeader:
        case serialization::Result::UnsupportedVersion:
        case serialization::Result::InvalidEncoding:
            SetFailure(failure, editor::EditorUserStateFailureCode::FutureDocument, 0, 0, static_cast<u32>(result));
            return editor::EditorUserStateLoadResult::RejectedFuture;
        default:
            SetFailure(failure, editor::EditorUserStateFailureCode::CorruptDocument, 0, 0, static_cast<u32>(result));
            return editor::EditorUserStateLoadResult::RejectedCorrupt;
        }
    }

    [[nodiscard]] bool IsZeroPadding(const u8* const bytes, const u64 begin, const u64 end) noexcept
    {
        for (u64 index = begin; index < end; ++index)
            if (bytes[index] != 0)
                return false;
        return true;
    }

    [[nodiscard]] bool ReadZeroPadding(serialization::BinaryReader& reader, const u32 alignment) noexcept
    {
        if (alignment == 0 || (alignment & (alignment - 1u)) != 0)
            return false;
        const u64 position = reader.Position();
        const u32 padding = static_cast<u32>((alignment - (position & (alignment - 1u))) & (alignment - 1u));
        for (u32 index = 0; index < padding; ++index)
        {
            u8 value = 0;
            if (!reader.ReadU8(value) || value != 0)
                return false;
        }
        return true;
    }

    [[nodiscard]] bool IsValidUtf8(const char* const bytes, const u32 count) noexcept
    {
        u32 index = 0;
        while (index < count)
        {
            const u8 first = static_cast<u8>(bytes[index]);
            if (first == 0)
                return false;
            if (first < 0x80u)
            {
                ++index;
                continue;
            }

            u32 continuationCount = 0;
            u32 codePoint = 0;
            if ((first & 0xe0u) == 0xc0u)
            {
                continuationCount = 1;
                codePoint = first & 0x1fu;
                if (codePoint < 2u)
                    return false;
            }
            else if ((first & 0xf0u) == 0xe0u)
            {
                continuationCount = 2;
                codePoint = first & 0x0fu;
            }
            else if ((first & 0xf8u) == 0xf0u)
            {
                continuationCount = 3;
                codePoint = first & 0x07u;
                if (codePoint > 4u)
                    return false;
            }
            else
                return false;

            if (index + continuationCount >= count)
                return false;
            for (u32 continuation = 0; continuation < continuationCount; ++continuation)
            {
                const u8 next = static_cast<u8>(bytes[index + continuation + 1u]);
                if ((next & 0xc0u) != 0x80u)
                    return false;
                codePoint = (codePoint << 6u) | (next & 0x3fu);
            }
            if ((continuationCount == 2u && codePoint < 0x800u) ||
                (continuationCount == 3u && codePoint < 0x10000u) ||
                codePoint > 0x10ffffu || (codePoint >= 0xd800u && codePoint <= 0xdfffu))
                return false;
            index += continuationCount + 1u;
        }
        return true;
    }

    [[nodiscard]] bool IsShortcutModifierKey(const input::Key key) noexcept
    {
        switch (key)
        {
        case input::Key::LeftControl:
        case input::Key::LeftShift:
        case input::Key::LeftAlt:
        case input::Key::LeftGui:
        case input::Key::RightControl:
        case input::Key::RightShift:
        case input::Key::RightAlt:
        case input::Key::RightGui:
            return true;
        default:
            return false;
        }
    }

    [[nodiscard]] bool IsKnownShortcutKey(const input::Key key) noexcept
    {
        const u32 value = static_cast<u32>(key);
        return (value >= static_cast<u32>(input::Key::A) && value <= static_cast<u32>(input::Key::VolumeDown)) ||
               (value >= static_cast<u32>(input::Key::KeypadComma) && value <= static_cast<u32>(input::Key::ExSel)) ||
               (value >= static_cast<u32>(input::Key::Keypad00) && value <= static_cast<u32>(input::Key::KeypadHexadecimal)) ||
               (value >= static_cast<u32>(input::Key::LeftControl) && value <= static_cast<u32>(input::Key::RightGui)) ||
               (value >= static_cast<u32>(input::Key::Mode) && value <= static_cast<u32>(input::Key::EndCall));
    }

    [[nodiscard]] bool ValidCustomShortcut(const u16 keyCode, const u8 modifiers) noexcept
    {
        constexpr u8 knownModifiers = 0x0fu;
        const input::Key key = static_cast<input::Key>(keyCode);
        return (modifiers & static_cast<u8>(~knownModifiers)) == 0 && IsKnownShortcutKey(key) && !IsShortcutModifierKey(key);
    }

    [[nodiscard]] int CompareDisplay(const editor::EditorUserStateSavedDisplay& left,
                                     const editor::EditorUserStateSavedDisplay& right) noexcept
    {
        if (left.primary != right.primary)
            return left.primary ? -1 : 1;
#define VG_COMPARE_DISPLAY_FIELD(field) \
        if (left.field != right.field)     \
            return left.field < right.field ? -1 : 1
        VG_COMPARE_DISPLAY_FIELD(boundsX);
        VG_COMPARE_DISPLAY_FIELD(boundsY);
        VG_COMPARE_DISPLAY_FIELD(boundsWidth);
        VG_COMPARE_DISPLAY_FIELD(boundsHeight);
        VG_COMPARE_DISPLAY_FIELD(workX);
        VG_COMPARE_DISPLAY_FIELD(workY);
        VG_COMPARE_DISPLAY_FIELD(workWidth);
        VG_COMPARE_DISPLAY_FIELD(workHeight);
        VG_COMPARE_DISPLAY_FIELD(desktopPixelWidth);
        VG_COMPARE_DISPLAY_FIELD(desktopPixelHeight);
        VG_COMPARE_DISPLAY_FIELD(scaleQ16_16);
#undef VG_COMPARE_DISPLAY_FIELD
        return 0;
    }

    template <typename Compare>
    void SortIndices(u32* const indices, const u32 count, Compare compare) noexcept
    {
        for (u32 index = 0; index < count; ++index)
            indices[index] = index;
        for (u32 index = 1; index < count; ++index)
        {
            const u32 value = indices[index];
            u32 cursor = index;
            while (cursor != 0 && compare(value, indices[cursor - 1u]))
            {
                indices[cursor] = indices[cursor - 1u];
                --cursor;
            }
            indices[cursor] = value;
        }
    }

    struct EncodedLayout final
    {
        u64 sectionOffsets[SectionCount]{};
        u64 sectionSizes[SectionCount]{};
        u64 tableOffset = 0;
        u64 digestOffset = 0;
        u64 fileSize = 0;
    };

    [[nodiscard]] bool ComputeEncodedLayout(const editor::EditorUserStateDocumentView& document,
                                            EncodedLayout& layout,
                                            editor::EditorUserStateFailure* const failure) noexcept
    {
        if (!document.project.IsValid() || document.displays.Size() == 0)
        {
            SetFailure(failure, editor::EditorUserStateFailureCode::InvalidArgument);
            return false;
        }
        if (document.panels.Size() > editor::MaximumEditorUserStatePanels ||
            document.displays.Size() > editor::MaximumEditorUserStateDisplays ||
            document.imguiBytes.Size() > editor::MaximumEditorUserStateImguiBytes ||
            document.shortcuts.Size() > editor::MaximumEditorUserStateShortcuts ||
            document.preferences.Size() > editor::MaximumEditorUserStatePreferences)
        {
            SetFailure(failure, editor::EditorUserStateFailureCode::BoundExceeded);
            return false;
        }

        u64 panelSize = 8;
        for (u32 index = 0; index < document.panels.Size(); ++index)
        {
            const editor::EditorUserStatePanelRecord& panel = document.panels[index];
            if (panel.typeId == 0 || panel.instanceId == 0 || panel.fallbackTitleByteCount == 0 ||
                panel.fallbackTitleByteCount > editor::MaximumEditorUserStatePanelTitleBytes ||
                !IsValidUtf8(panel.fallbackTitle, panel.fallbackTitleByteCount))
            {
                SetFailure(failure, editor::EditorUserStateFailureCode::InvalidArgument, PanelsSection, index);
                return false;
            }
            const u64 recordSize = Align8(24u + panel.fallbackTitleByteCount);
            if (AddWouldOverflow(panelSize, recordSize))
            {
                SetFailure(failure, editor::EditorUserStateFailureCode::BoundExceeded, PanelsSection, index);
                return false;
            }
            panelSize += recordSize;
        }

        for (u32 index = 0; index < document.displays.Size(); ++index)
        {
            const editor::EditorUserStateSavedDisplay& display = document.displays[index];
            if (display.boundsWidth == 0 || display.boundsHeight == 0 || display.workWidth == 0 || display.workHeight == 0 ||
                display.desktopPixelWidth == 0 || display.desktopPixelHeight == 0 || display.scaleQ16_16 == 0 ||
                display.scaleQ16_16 > 0x00100000u)
            {
                SetFailure(failure, editor::EditorUserStateFailureCode::InvalidArgument, UiStateSection, index);
                return false;
            }
        }

        u64 preferenceSize = 8;
        for (u32 index = 0; index < document.preferences.Size(); ++index)
        {
            const editor::EditorUserStatePreferenceRecord& preference = document.preferences[index];
            if (preference.extensionId == 0 || preference.providerId == 0 || preference.fieldId == 0)
            {
                SetFailure(failure, editor::EditorUserStateFailureCode::InvalidArgument, PreferencesSection, index);
                return false;
            }
            u32 payloadBytes = 0;
            switch (preference.type)
            {
            case editor::EditorUserStatePreferenceValueType::Bool:
                payloadBytes = 1;
                break;
            case editor::EditorUserStatePreferenceValueType::SignedInteger:
            case editor::EditorUserStatePreferenceValueType::UnsignedBitMask:
                payloadBytes = 8;
                break;
            case editor::EditorUserStatePreferenceValueType::Utf8String:
                payloadBytes = preference.stringByteCount;
                if (payloadBytes > editor::MaximumEditorUserStatePreferenceStringBytes ||
                    !IsValidUtf8(preference.stringValue, payloadBytes))
                {
                    SetFailure(failure, editor::EditorUserStateFailureCode::InvalidArgument, PreferencesSection, index);
                    return false;
                }
                break;
            default:
                SetFailure(failure, editor::EditorUserStateFailureCode::InvalidArgument, PreferencesSection, index);
                return false;
            }
            const u64 recordSize = Align8(40u + payloadBytes);
            if (AddWouldOverflow(preferenceSize, recordSize))
            {
                SetFailure(failure, editor::EditorUserStateFailureCode::BoundExceeded, PreferencesSection, index);
                return false;
            }
            preferenceSize += recordSize;
        }

        for (u32 index = 0; index < document.shortcuts.Size(); ++index)
        {
            const editor::EditorUserStateShortcutRecord& shortcut = document.shortcuts[index];
            if (shortcut.contextTypeId == 0 || shortcut.commandId == 0 ||
                (shortcut.mode != editor::EditorUserStateShortcutMode::Unbound && shortcut.mode != editor::EditorUserStateShortcutMode::Custom) ||
                (shortcut.mode == editor::EditorUserStateShortcutMode::Unbound && (shortcut.keyCode != 0 || shortcut.modifiers != 0)) ||
                (shortcut.mode == editor::EditorUserStateShortcutMode::Custom && !ValidCustomShortcut(shortcut.keyCode, shortcut.modifiers)))
            {
                SetFailure(failure, editor::EditorUserStateFailureCode::InvalidArgument, ShortcutsSection, index);
                return false;
            }
        }

        layout.sectionSizes[0] = 32;
        layout.sectionSizes[1] = panelSize;
        layout.sectionSizes[2] = 16u + static_cast<u64>(document.displays.Size()) * 48u + document.imguiBytes.Size();
        layout.sectionSizes[3] = 8u + static_cast<u64>(document.shortcuts.Size()) * 32u;
        layout.sectionSizes[4] = preferenceSize;

        u64 cursor = serialization::DocumentHeader::WireSize;
        for (u32 index = 0; index < SectionCount; ++index)
        {
            cursor = Align8(cursor);
            layout.sectionOffsets[index] = cursor;
            if (AddWouldOverflow(cursor, layout.sectionSizes[index]))
            {
                SetFailure(failure, editor::EditorUserStateFailureCode::BoundExceeded, SectionIds[index]);
                return false;
            }
            cursor += layout.sectionSizes[index];
        }
        layout.tableOffset = Align8(cursor);
        constexpr u64 tableSize = static_cast<u64>(SectionCount) * serialization::SectionDescriptor::WireSize;
        if (AddWouldOverflow(layout.tableOffset, tableSize) || AddWouldOverflow(layout.tableOffset + tableSize, DigestBytes))
        {
            SetFailure(failure, editor::EditorUserStateFailureCode::BoundExceeded);
            return false;
        }
        layout.digestOffset = layout.tableOffset + tableSize;
        layout.fileSize = layout.digestOffset + DigestBytes;
        if (layout.fileSize > editor::MaximumEditorUserStateFileBytes)
        {
            SetFailure(failure, editor::EditorUserStateFailureCode::BoundExceeded);
            return false;
        }
        return true;
    }

    [[nodiscard]] bool WritePanelSection(serialization::BinaryWriter& writer,
                                         const editor::EditorUserStateDocumentView& document,
                                         editor::EditorUserStateFailure* const failure) noexcept
    {
        u32 order[editor::MaximumEditorUserStatePanels]{};
        SortIndices(order, document.panels.Size(), [&](const u32 leftIndex, const u32 rightIndex) noexcept {
            const auto& left = document.panels[leftIndex];
            const auto& right = document.panels[rightIndex];
            if (left.instanceId != right.instanceId)
                return left.instanceId < right.instanceId;
            return left.typeId < right.typeId;
        });
        for (u32 index = 1; index < document.panels.Size(); ++index)
            if (document.panels[order[index - 1u]].instanceId == document.panels[order[index]].instanceId)
            {
                SetFailure(failure, editor::EditorUserStateFailureCode::InvalidArgument, PanelsSection, index);
                return false;
            }

        if (!writer.WriteU32(document.panels.Size()) || !writer.WriteU32(0))
            return false;
        for (u32 index = 0; index < document.panels.Size(); ++index)
        {
            const auto& panel = document.panels[order[index]];
            if (!writer.WriteU64(panel.typeId) || !writer.WriteU64(panel.instanceId) || !writer.WriteU16(panel.fallbackTitleByteCount) ||
                !writer.WriteU8(panel.open ? 1u : 0u) || !writer.WriteU8(0) || !writer.WriteU32(0) ||
                !writer.WriteBytes(panel.fallbackTitle, panel.fallbackTitleByteCount) || !writer.Align(SectionAlignment))
                return false;
        }
        return true;
    }

    [[nodiscard]] bool WriteUiSection(serialization::BinaryWriter& writer,
                                      const editor::EditorUserStateDocumentView& document,
                                      editor::EditorUserStateFailure* const failure) noexcept
    {
        u32 order[editor::MaximumEditorUserStateDisplays]{};
        SortIndices(order, document.displays.Size(), [&](const u32 left, const u32 right) noexcept {
            return CompareDisplay(document.displays[left], document.displays[right]) < 0;
        });
        u32 primaryCount = 0;
        for (u32 index = 0; index < document.displays.Size(); ++index)
            primaryCount += document.displays[index].primary ? 1u : 0u;
        if (primaryCount != 1)
        {
            SetFailure(failure, editor::EditorUserStateFailureCode::InvalidArgument, UiStateSection);
            return false;
        }

        if (!writer.WriteU32(document.displays.Size()) || !writer.WriteU32(document.imguiBytes.Size()) ||
            !writer.WriteU32(0) || !writer.WriteU32(0))
            return false;
        for (u32 index = 0; index < document.displays.Size(); ++index)
        {
            const auto& display = document.displays[order[index]];
            if (!writer.WriteI32(display.boundsX) || !writer.WriteI32(display.boundsY) ||
                !writer.WriteU32(display.boundsWidth) || !writer.WriteU32(display.boundsHeight) ||
                !writer.WriteI32(display.workX) || !writer.WriteI32(display.workY) ||
                !writer.WriteU32(display.workWidth) || !writer.WriteU32(display.workHeight) ||
                !writer.WriteU32(display.desktopPixelWidth) || !writer.WriteU32(display.desktopPixelHeight) ||
                !writer.WriteU32(display.scaleQ16_16) || !writer.WriteU8(display.primary ? 1u : 0u) ||
                !writer.WriteU8(0) || !writer.WriteU8(0) || !writer.WriteU8(0))
                return false;
        }
        return writer.WriteBytes(document.imguiBytes.Data(), document.imguiBytes.Size());
    }

    [[nodiscard]] bool WriteShortcutSection(serialization::BinaryWriter& writer,
                                            const editor::EditorUserStateDocumentView& document,
                                            editor::EditorUserStateFailure* const failure) noexcept
    {
        u32 order[editor::MaximumEditorUserStateShortcuts]{};
        SortIndices(order, document.shortcuts.Size(), [&](const u32 leftIndex, const u32 rightIndex) noexcept {
            const auto& left = document.shortcuts[leftIndex];
            const auto& right = document.shortcuts[rightIndex];
            if (left.contextTypeId != right.contextTypeId)
                return left.contextTypeId < right.contextTypeId;
            return left.commandId < right.commandId;
        });
        for (u32 index = 1; index < document.shortcuts.Size(); ++index)
        {
            const auto& previous = document.shortcuts[order[index - 1u]];
            const auto& current = document.shortcuts[order[index]];
            if (previous.contextTypeId == current.contextTypeId && previous.commandId == current.commandId)
            {
                SetFailure(failure, editor::EditorUserStateFailureCode::InvalidArgument, ShortcutsSection, index);
                return false;
            }
        }

        if (!writer.WriteU32(document.shortcuts.Size()) || !writer.WriteU32(0))
            return false;
        for (u32 index = 0; index < document.shortcuts.Size(); ++index)
        {
            const auto& shortcut = document.shortcuts[order[index]];
            if (!writer.WriteU64(shortcut.contextTypeId) || !writer.WriteU64(shortcut.commandId) ||
                !writer.WriteU16(shortcut.keyCode) || !writer.WriteU8(shortcut.modifiers) ||
                !writer.WriteU8(static_cast<u8>(shortcut.mode)) || !writer.WriteU32(0) || !writer.WriteU64(0))
                return false;
        }
        return true;
    }

    [[nodiscard]] bool WritePreferenceSection(serialization::BinaryWriter& writer,
                                              const editor::EditorUserStateDocumentView& document,
                                              editor::EditorUserStateFailure* const failure) noexcept
    {
        u32 order[editor::MaximumEditorUserStatePreferences]{};
        SortIndices(order, document.preferences.Size(), [&](const u32 leftIndex, const u32 rightIndex) noexcept {
            const auto& left = document.preferences[leftIndex];
            const auto& right = document.preferences[rightIndex];
            if (left.extensionId != right.extensionId)
                return left.extensionId < right.extensionId;
            if (left.providerId != right.providerId)
                return left.providerId < right.providerId;
            return left.fieldId < right.fieldId;
        });
        for (u32 index = 1; index < document.preferences.Size(); ++index)
        {
            const auto& previous = document.preferences[order[index - 1u]];
            const auto& current = document.preferences[order[index]];
            if (previous.extensionId == current.extensionId && previous.providerId == current.providerId && previous.fieldId == current.fieldId)
            {
                SetFailure(failure, editor::EditorUserStateFailureCode::InvalidArgument, PreferencesSection, index);
                return false;
            }
        }

        if (!writer.WriteU32(document.preferences.Size()) || !writer.WriteU32(0))
            return false;
        for (u32 index = 0; index < document.preferences.Size(); ++index)
        {
            const auto& preference = document.preferences[order[index]];
            u32 payloadBytes = 0;
            switch (preference.type)
            {
            case editor::EditorUserStatePreferenceValueType::Bool:
                payloadBytes = 1;
                break;
            case editor::EditorUserStatePreferenceValueType::SignedInteger:
            case editor::EditorUserStatePreferenceValueType::UnsignedBitMask:
                payloadBytes = 8;
                break;
            case editor::EditorUserStatePreferenceValueType::Utf8String:
                payloadBytes = preference.stringByteCount;
                break;
            default:
                SetFailure(failure, editor::EditorUserStateFailureCode::InvalidArgument, PreferencesSection, index);
                return false;
            }
            if (!writer.WriteU64(preference.extensionId) || !writer.WriteU64(preference.providerId) || !writer.WriteU64(preference.fieldId) ||
                !writer.WriteU8(static_cast<u8>(preference.type)) || !writer.WriteU8(0) || !writer.WriteU8(0) || !writer.WriteU8(0) ||
                !writer.WriteU32(payloadBytes) || !writer.WriteU64(0))
                return false;
            switch (preference.type)
            {
            case editor::EditorUserStatePreferenceValueType::Bool:
                if (!writer.WriteU8(preference.boolValue ? 1u : 0u))
                    return false;
                break;
            case editor::EditorUserStatePreferenceValueType::SignedInteger:
                if (!writer.WriteI64(preference.signedValue))
                    return false;
                break;
            case editor::EditorUserStatePreferenceValueType::UnsignedBitMask:
                if (!writer.WriteU64(preference.unsignedValue))
                    return false;
                break;
            case editor::EditorUserStatePreferenceValueType::Utf8String:
                if (!writer.WriteBytes(preference.stringValue, preference.stringByteCount))
                    return false;
                break;
            default:
                return false;
            }
            if (!writer.Align(SectionAlignment))
                return false;
        }
        return true;
    }

    [[nodiscard]] editor::EditorUserStateLoadResult RejectCorrupt(editor::EditorUserStateFailure* const failure,
                                                                  const u32 section = 0, const u32 record = 0,
                                                                  const u32 detail = 0) noexcept
    {
        SetFailure(failure, editor::EditorUserStateFailureCode::CorruptDocument, section, record, detail);
        return editor::EditorUserStateLoadResult::RejectedCorrupt;
    }

    [[nodiscard]] editor::EditorUserStateLoadResult RejectBounds(editor::EditorUserStateFailure* const failure,
                                                                 const u32 section = 0, const u32 record = 0,
                                                                 const u32 detail = 0) noexcept
    {
        SetFailure(failure, editor::EditorUserStateFailureCode::BoundExceeded, section, record, detail);
        return editor::EditorUserStateLoadResult::RejectedBounds;
    }

    [[nodiscard]] editor::EditorUserStateLoadResult RejectFuture(editor::EditorUserStateFailure* const failure,
                                                                 const u32 section = 0, const u32 record = 0,
                                                                 const u32 detail = 0) noexcept
    {
        SetFailure(failure, editor::EditorUserStateFailureCode::FutureDocument, section, record, detail);
        return editor::EditorUserStateLoadResult::RejectedFuture;
    }

    [[nodiscard]] bool AtSectionEnd(const serialization::BinaryReader& reader,
                                    const serialization::SectionDescriptor& section) noexcept
    {
        return reader.Position() == section.offset + section.storedSize;
    }

    [[nodiscard]] editor::EditorUserStateLoadResult ParseMetaSection(
        serialization::BinaryReader& reader, const serialization::SectionDescriptor& section,
        const projects::ProjectId& expectedProject, editor::EditorUserStateEnvelopeInfo& info,
        editor::EditorUserStateFailure* const failure) noexcept
    {
        if (section.storedSize != 32 || !reader.Seek(section.offset))
            return RejectCorrupt(failure, MetaSection);
        u64 domain = 0;
        projects::ProjectId project;
        u32 reserved0 = 0, reserved1 = 0;
        if (!reader.ReadU64(domain) || !reader.ReadU32(project.parts[0]) || !reader.ReadU32(project.parts[1]) ||
            !reader.ReadU32(project.parts[2]) || !reader.ReadU32(project.parts[3]) ||
            !reader.ReadU32(reserved0) || !reader.ReadU32(reserved1) || !AtSectionEnd(reader, section))
            return RejectCorrupt(failure, MetaSection);
        if (reserved0 != 0 || reserved1 != 0)
            return RejectFuture(failure, MetaSection);
        if (domain != UserStateDomain || !project.IsValid())
            return RejectCorrupt(failure, MetaSection);
        if (!(project == expectedProject))
        {
            SetFailure(failure, editor::EditorUserStateFailureCode::ProjectMismatch, MetaSection);
            return editor::EditorUserStateLoadResult::RejectedProjectMismatch;
        }
        info.project = project;
        return editor::EditorUserStateLoadResult::Restored;
    }

    [[nodiscard]] editor::EditorUserStateLoadResult ParsePanelsSection(
        serialization::BinaryReader& reader, const serialization::SectionDescriptor& section,
        editor::EditorUserStateEnvelopeInfo& info, editor::EditorUserStateFailure* const failure) noexcept
    {
        if (!reader.Seek(section.offset))
            return RejectCorrupt(failure, PanelsSection);
        u32 count = 0, reserved = 0;
        if (!reader.ReadU32(count) || !reader.ReadU32(reserved))
            return RejectCorrupt(failure, PanelsSection);
        if (reserved != 0)
            return RejectFuture(failure, PanelsSection);
        if (count > editor::MaximumEditorUserStatePanels)
            return RejectBounds(failure, PanelsSection, count);

        u64 previousInstance = 0;
        for (u32 index = 0; index < count; ++index)
        {
            u64 typeId = 0, instanceId = 0;
            u16 titleBytes = 0;
            u8 open = 0, reserved8 = 0;
            u32 reserved32 = 0;
            if (!reader.ReadU64(typeId) || !reader.ReadU64(instanceId) || !reader.ReadU16(titleBytes) ||
                !reader.ReadU8(open) || !reader.ReadU8(reserved8) || !reader.ReadU32(reserved32))
                return RejectCorrupt(failure, PanelsSection, index);
            if (reserved8 != 0 || reserved32 != 0)
                return RejectFuture(failure, PanelsSection, index);
            if (titleBytes > editor::MaximumEditorUserStatePanelTitleBytes)
                return RejectBounds(failure, PanelsSection, index, titleBytes);
            if (typeId == 0 || instanceId == 0 || titleBytes == 0 || open > 1 ||
                (index != 0 && instanceId <= previousInstance))
                return RejectCorrupt(failure, PanelsSection, index);
            char title[editor::MaximumEditorUserStatePanelTitleBytes + 1u]{};
            if (!reader.ReadBytes(title, titleBytes) || !IsValidUtf8(title, titleBytes) || !ReadZeroPadding(reader, SectionAlignment))
                return RejectCorrupt(failure, PanelsSection, index);
            previousInstance = instanceId;
        }
        if (!AtSectionEnd(reader, section))
            return RejectCorrupt(failure, PanelsSection);
        info.panelRecords = count;
        return editor::EditorUserStateLoadResult::Restored;
    }

    [[nodiscard]] editor::EditorUserStateLoadResult ParseUiSection(
        serialization::BinaryReader& reader, const serialization::SectionDescriptor& section,
        editor::EditorUserStateEnvelopeInfo& info, editor::EditorUserStateFailure* const failure) noexcept
    {
        if (!reader.Seek(section.offset))
            return RejectCorrupt(failure, UiStateSection);
        u32 displayCount = 0, imguiByteCount = 0, reserved0 = 0, reserved1 = 0;
        if (!reader.ReadU32(displayCount) || !reader.ReadU32(imguiByteCount) || !reader.ReadU32(reserved0) || !reader.ReadU32(reserved1))
            return RejectCorrupt(failure, UiStateSection);
        if (reserved0 != 0 || reserved1 != 0)
            return RejectFuture(failure, UiStateSection);
        if (displayCount > editor::MaximumEditorUserStateDisplays || imguiByteCount > editor::MaximumEditorUserStateImguiBytes)
            return RejectBounds(failure, UiStateSection);
        if (displayCount == 0)
            return RejectCorrupt(failure, UiStateSection);
        const u64 expectedSize = 16u + static_cast<u64>(displayCount) * 48u + imguiByteCount;
        if (section.storedSize != expectedSize)
            return RejectCorrupt(failure, UiStateSection);

        editor::EditorUserStateSavedDisplay previous{};
        bool hasPrevious = false;
        u32 primaryCount = 0;
        for (u32 index = 0; index < displayCount; ++index)
        {
            editor::EditorUserStateSavedDisplay display;
            u8 primary = 0, pad0 = 0, pad1 = 0, pad2 = 0;
            if (!reader.ReadI32(display.boundsX) || !reader.ReadI32(display.boundsY) ||
                !reader.ReadU32(display.boundsWidth) || !reader.ReadU32(display.boundsHeight) ||
                !reader.ReadI32(display.workX) || !reader.ReadI32(display.workY) ||
                !reader.ReadU32(display.workWidth) || !reader.ReadU32(display.workHeight) ||
                !reader.ReadU32(display.desktopPixelWidth) || !reader.ReadU32(display.desktopPixelHeight) ||
                !reader.ReadU32(display.scaleQ16_16) || !reader.ReadU8(primary) ||
                !reader.ReadU8(pad0) || !reader.ReadU8(pad1) || !reader.ReadU8(pad2))
                return RejectCorrupt(failure, UiStateSection, index);
            if (pad0 != 0 || pad1 != 0 || pad2 != 0)
                return RejectFuture(failure, UiStateSection, index);
            display.primary = primary != 0;
            if (primary > 1 || display.boundsWidth == 0 || display.boundsHeight == 0 ||
                display.workWidth == 0 || display.workHeight == 0 || display.desktopPixelWidth == 0 ||
                display.desktopPixelHeight == 0 || display.scaleQ16_16 == 0 || display.scaleQ16_16 > 0x00100000u ||
                (hasPrevious && CompareDisplay(previous, display) > 0))
                return RejectCorrupt(failure, UiStateSection, index);
            primaryCount += display.primary ? 1u : 0u;
            previous = display;
            hasPrevious = true;
        }
        if (primaryCount != 1 || !reader.Skip(imguiByteCount) || !AtSectionEnd(reader, section))
            return RejectCorrupt(failure, UiStateSection);
        info.displayRecords = displayCount;
        info.imguiByteCount = imguiByteCount;
        return editor::EditorUserStateLoadResult::Restored;
    }

    [[nodiscard]] editor::EditorUserStateLoadResult ParseShortcutsSection(
        serialization::BinaryReader& reader, const serialization::SectionDescriptor& section,
        editor::EditorUserStateEnvelopeInfo& info, editor::EditorUserStateFailure* const failure) noexcept
    {
        if (!reader.Seek(section.offset))
            return RejectCorrupt(failure, ShortcutsSection);
        u32 count = 0, reserved = 0;
        if (!reader.ReadU32(count) || !reader.ReadU32(reserved))
            return RejectCorrupt(failure, ShortcutsSection);
        if (reserved != 0)
            return RejectFuture(failure, ShortcutsSection);
        if (count > editor::MaximumEditorUserStateShortcuts)
            return RejectBounds(failure, ShortcutsSection, count);
        if (section.storedSize != 8u + static_cast<u64>(count) * 32u)
            return RejectCorrupt(failure, ShortcutsSection);

        u64 previousContext = 0, previousCommand = 0;
        bool hasPrevious = false;
        for (u32 index = 0; index < count; ++index)
        {
            u64 contextId = 0, commandId = 0, reserved64 = 0;
            u16 keyCode = 0;
            u8 modifiers = 0, mode = 0;
            u32 reserved32 = 0;
            if (!reader.ReadU64(contextId) || !reader.ReadU64(commandId) || !reader.ReadU16(keyCode) ||
                !reader.ReadU8(modifiers) || !reader.ReadU8(mode) || !reader.ReadU32(reserved32) || !reader.ReadU64(reserved64))
                return RejectCorrupt(failure, ShortcutsSection, index);
            if (reserved32 != 0 || reserved64 != 0)
                return RejectFuture(failure, ShortcutsSection, index);
            const bool unbound = mode == static_cast<u8>(editor::EditorUserStateShortcutMode::Unbound);
            const bool custom = mode == static_cast<u8>(editor::EditorUserStateShortcutMode::Custom);
            const bool ordered = !hasPrevious || contextId > previousContext ||
                                 (contextId == previousContext && commandId > previousCommand);
            if (contextId == 0 || commandId == 0 || !ordered || (!unbound && !custom) ||
                (unbound && (keyCode != 0 || modifiers != 0)) || (custom && !ValidCustomShortcut(keyCode, modifiers)))
                return RejectCorrupt(failure, ShortcutsSection, index);
            previousContext = contextId;
            previousCommand = commandId;
            hasPrevious = true;
        }
        if (!AtSectionEnd(reader, section))
            return RejectCorrupt(failure, ShortcutsSection);
        info.shortcutRecords = count;
        return editor::EditorUserStateLoadResult::Restored;
    }

    [[nodiscard]] editor::EditorUserStateLoadResult ParsePreferencesSection(
        serialization::BinaryReader& reader, const serialization::SectionDescriptor& section,
        editor::EditorUserStateEnvelopeInfo& info, editor::EditorUserStateFailure* const failure) noexcept
    {
        if (!reader.Seek(section.offset))
            return RejectCorrupt(failure, PreferencesSection);
        u32 count = 0, reserved = 0;
        if (!reader.ReadU32(count) || !reader.ReadU32(reserved))
            return RejectCorrupt(failure, PreferencesSection);
        if (reserved != 0)
            return RejectFuture(failure, PreferencesSection);
        if (count > editor::MaximumEditorUserStatePreferences)
            return RejectBounds(failure, PreferencesSection, count);

        u64 previousExtension = 0, previousProvider = 0, previousField = 0;
        bool hasPrevious = false;
        for (u32 index = 0; index < count; ++index)
        {
            u64 extensionId = 0, providerId = 0, fieldId = 0, reserved64 = 0;
            u8 valueType = 0, reserved0 = 0, reserved1 = 0, reserved2 = 0;
            u32 payloadBytes = 0;
            if (!reader.ReadU64(extensionId) || !reader.ReadU64(providerId) || !reader.ReadU64(fieldId) ||
                !reader.ReadU8(valueType) || !reader.ReadU8(reserved0) || !reader.ReadU8(reserved1) || !reader.ReadU8(reserved2) ||
                !reader.ReadU32(payloadBytes) || !reader.ReadU64(reserved64))
                return RejectCorrupt(failure, PreferencesSection, index);
            if (reserved0 != 0 || reserved1 != 0 || reserved2 != 0 || reserved64 != 0)
                return RejectFuture(failure, PreferencesSection, index);
            const bool ordered = !hasPrevious || extensionId > previousExtension ||
                                 (extensionId == previousExtension && (providerId > previousProvider ||
                                  (providerId == previousProvider && fieldId > previousField)));
            if (extensionId == 0 || providerId == 0 || fieldId == 0 || !ordered)
                return RejectCorrupt(failure, PreferencesSection, index);

            switch (static_cast<editor::EditorUserStatePreferenceValueType>(valueType))
            {
            case editor::EditorUserStatePreferenceValueType::Bool:
            {
                u8 value = 0;
                if (payloadBytes != 1 || !reader.ReadU8(value) || value > 1)
                    return RejectCorrupt(failure, PreferencesSection, index);
                break;
            }
            case editor::EditorUserStatePreferenceValueType::SignedInteger:
            {
                i64 value = 0;
                if (payloadBytes != 8 || !reader.ReadI64(value))
                    return RejectCorrupt(failure, PreferencesSection, index);
                break;
            }
            case editor::EditorUserStatePreferenceValueType::UnsignedBitMask:
            {
                u64 value = 0;
                if (payloadBytes != 8 || !reader.ReadU64(value))
                    return RejectCorrupt(failure, PreferencesSection, index);
                break;
            }
            case editor::EditorUserStatePreferenceValueType::Utf8String:
            {
                if (payloadBytes > editor::MaximumEditorUserStatePreferenceStringBytes)
                    return RejectBounds(failure, PreferencesSection, index, payloadBytes);
                char value[editor::MaximumEditorUserStatePreferenceStringBytes + 1u]{};
                if (!reader.ReadBytes(value, payloadBytes) || !IsValidUtf8(value, payloadBytes))
                    return RejectCorrupt(failure, PreferencesSection, index);
                break;
            }
            default:
                return RejectCorrupt(failure, PreferencesSection, index);
            }
            if (!ReadZeroPadding(reader, SectionAlignment))
                return RejectCorrupt(failure, PreferencesSection, index);
            previousExtension = extensionId;
            previousProvider = providerId;
            previousField = fieldId;
            hasPrevious = true;
        }
        if (!AtSectionEnd(reader, section))
            return RejectCorrupt(failure, PreferencesSection);
        info.preferenceRecords = count;
        return editor::EditorUserStateLoadResult::Restored;
    }
} // namespace

namespace vanguard::editor::detail
{
    bool QuantizeEditorUiDisplayScale(const f32 scale, u32& scaleQ16_16) noexcept
    {
        scaleQ16_16 = 0;
        if (!(scale > 0.0f) || scale > 16.0f || !std::isfinite(scale))
            return false;
        const f64 scaled = static_cast<f64>(scale) * 65536.0;
        if (scaled < 0.5 || scaled > static_cast<f64>(0x00100000u) + 0.5)
            return false;
        const long long rounded = std::llround(scaled);
        if (rounded < 1 || rounded > 0x00100000ll)
            return false;
        scaleQ16_16 = static_cast<u32>(rounded);
        return true;
    }

    EditorUiSavedRect DecodeEditorUiPrivateWindowPlacement(
        const EditorUiPrivateWindowPlacementFields& fields) noexcept
    {
        return {static_cast<i32>(fields.viewportX) + static_cast<i32>(fields.relativeX),
                static_cast<i32>(fields.viewportY) + static_cast<i32>(fields.relativeY),
                fields.width, fields.height};
    }

    bool EncodeEditorUiPrivateWindowPlacement(
        const EditorUiSavedRect& repaired, EditorUiPrivateWindowPlacementFields& fields) noexcept
    {
        fields = {};
        if (repaired.x < -32768 || repaired.x > 32767 || repaired.y < -32768 || repaired.y > 32767 ||
            repaired.width < 1 || repaired.width > 32767 || repaired.height < 1 || repaired.height > 32767)
            return false;
        fields.viewportX = static_cast<i16>(repaired.x);
        fields.viewportY = static_cast<i16>(repaired.y);
        fields.width = static_cast<i16>(repaired.width);
        fields.height = static_cast<i16>(repaired.height);
        return true;
    }

    EditorUiPlacementResolveResult ResolveEditorUiSavedPlacement(
        const containers::ArraySpan<const EditorUiSavedDisplay> savedDisplays,
        const containers::ArraySpan<const window::DisplaySnapshot> currentDisplays,
        const EditorUiSavedRect& savedRect, EditorUiRepairedPlacement& repaired,
        const bool requirePrivateWindowEncoding) noexcept
    {
        repaired = {};
        u32 savedPrimary = ~u32{0};
        u32 currentPrimary = ~u32{0};
        u32 currentScales[window::MaximumDisplays]{};
        if (!ValidSavedTopology(savedDisplays, savedPrimary))
            return EditorUiPlacementResolveResult::InvalidArgument;
        if (!ValidCurrentTopology(currentDisplays, currentPrimary, currentScales))
            return EditorUiPlacementResolveResult::EnvironmentUnavailable;

        const bool degenerate = savedRect.width <= 0 || savedRect.height <= 0;
        u32 savedIndex = savedPrimary;
        u32 currentIndex = currentPrimary;
        if (!degenerate)
        {
            u64 bestSavedArea = 0;
            bool bestContainsCenter = false;
            bool bestPrimary = false;
            bool haveSaved = false;
            for (u32 index = 0; index < savedDisplays.Size(); ++index)
            {
                const EditorUiSavedDisplay& display = savedDisplays[index];
                const u64 area = RectIntersectionArea(savedRect.x, savedRect.y,
                                                      static_cast<u64>(savedRect.width), static_cast<u64>(savedRect.height),
                                                      display.workX, display.workY, display.workWidth, display.workHeight);
                const bool containsCenter = ContainsRectCenter(display, savedRect);
                const bool equalRank = haveSaved && area == bestSavedArea &&
                                       containsCenter == bestContainsCenter && display.primary == bestPrimary;
                const bool better = !haveSaved || area > bestSavedArea ||
                                    (area == bestSavedArea && containsCenter && !bestContainsCenter) ||
                                    (area == bestSavedArea && containsCenter == bestContainsCenter && display.primary && !bestPrimary) ||
                                    (equalRank && SavedDisplayLess(display, savedDisplays[savedIndex]));
                if (better)
                {
                    savedIndex = index;
                    bestSavedArea = area;
                    bestContainsCenter = containsCenter;
                    bestPrimary = display.primary;
                    haveSaved = true;
                }
            }
            if (bestSavedArea == 0)
                savedIndex = savedPrimary;

            const EditorUiSavedDisplay& saved = savedDisplays[savedIndex];
            u64 bestTuple[8]{};
            u32 tied[window::MaximumDisplays]{};
            u32 tiedCount = 0;
            for (u32 index = 0; index < currentDisplays.Size(); ++index)
            {
                u64 tuple[8]{};
                BuildSimilarity(saved, currentDisplays[index], currentScales[index], tuple);
                if (tiedCount == 0 || SimilarityLess(tuple, bestTuple))
                {
                    for (u32 component = 0; component < 8; ++component)
                        bestTuple[component] = tuple[component];
                    tied[0] = index;
                    tiedCount = 1;
                }
                else if (SimilarityEqual(tuple, bestTuple))
                {
                    tied[tiedCount++] = index;
                }
            }

            if (tiedCount == 1)
            {
                currentIndex = tied[0];
            }
            else
            {
                u64 greatestOverlap = 0;
                u32 greatestIndex = ~u32{0};
                bool uniqueGreatest = false;
                for (u32 tie = 0; tie < tiedCount; ++tie)
                {
                    const u32 index = tied[tie];
                    const window::WindowRect& work = currentDisplays[index].workArea;
                    const u64 overlap = RectIntersectionArea(saved.workX, saved.workY, saved.workWidth, saved.workHeight,
                                                             work.origin.x, work.origin.y, work.extent.width, work.extent.height);
                    if (overlap > greatestOverlap)
                    {
                        greatestOverlap = overlap;
                        greatestIndex = index;
                        uniqueGreatest = true;
                    }
                    else if (overlap == greatestOverlap && overlap != 0)
                    {
                        uniqueGreatest = false;
                    }
                }
                currentIndex = greatestOverlap != 0 && uniqueGreatest ? greatestIndex : currentPrimary;
            }
        }

        const window::WindowRect& currentWork = currentDisplays[currentIndex].workArea;
        i64 width = 0;
        i64 height = 0;
        i64 x = 0;
        i64 y = 0;
        if (degenerate)
        {
            width = DefaultRestoredDetachedWidth;
            height = DefaultRestoredDetachedHeight;
            if (width > static_cast<i64>(currentWork.extent.width))
                width = currentWork.extent.width;
            if (height > static_cast<i64>(currentWork.extent.height))
                height = currentWork.extent.height;
            x = static_cast<i64>(currentWork.origin.x) +
                (static_cast<i64>(currentWork.extent.width) - width) / 2;
            y = static_cast<i64>(currentWork.origin.y) +
                (static_cast<i64>(currentWork.extent.height) - height) / 2;
        }
        else
        {
            const EditorUiSavedDisplay& saved = savedDisplays[savedIndex];
            const f64 relativeX = (static_cast<f64>(savedRect.x) - static_cast<f64>(saved.workX)) /
                                  static_cast<f64>(saved.workWidth);
            const f64 relativeY = (static_cast<f64>(savedRect.y) - static_cast<f64>(saved.workY)) /
                                  static_cast<f64>(saved.workHeight);
            const f64 relativeWidth = static_cast<f64>(savedRect.width) / static_cast<f64>(saved.workWidth);
            const f64 relativeHeight = static_cast<f64>(savedRect.height) / static_cast<f64>(saved.workHeight);
            const f64 mappedX = static_cast<f64>(currentWork.origin.x) +
                                relativeX * static_cast<f64>(currentWork.extent.width);
            const f64 mappedY = static_cast<f64>(currentWork.origin.y) +
                                relativeY * static_cast<f64>(currentWork.extent.height);
            const f64 mappedWidth = relativeWidth * static_cast<f64>(currentWork.extent.width);
            const f64 mappedHeight = relativeHeight * static_cast<f64>(currentWork.extent.height);
            if (!std::isfinite(mappedX) || !std::isfinite(mappedY) || !std::isfinite(mappedWidth) || !std::isfinite(mappedHeight))
                return EditorUiPlacementResolveResult::EnvironmentUnavailable;

            const auto roundSaturated = [](const f64 value) noexcept -> i64 {
                constexpr f64 minimum = static_cast<f64>(std::numeric_limits<i64>::min());
                constexpr f64 maximum = static_cast<f64>(std::numeric_limits<i64>::max());
                if (value <= minimum)
                    return std::numeric_limits<i64>::min();
                if (value >= maximum)
                    return std::numeric_limits<i64>::max();
                return static_cast<i64>(std::llround(value));
            };
            x = roundSaturated(mappedX);
            y = roundSaturated(mappedY);
            width = roundSaturated(mappedWidth);
            height = roundSaturated(mappedHeight);

            const i64 minimumWidth = currentWork.extent.width < static_cast<u32>(MinimumRestoredDetachedWidth)
                                       ? currentWork.extent.width
                                       : MinimumRestoredDetachedWidth;
            const i64 minimumHeight = currentWork.extent.height < static_cast<u32>(MinimumRestoredDetachedHeight)
                                        ? currentWork.extent.height
                                        : MinimumRestoredDetachedHeight;
            if (width < minimumWidth)
                width = minimumWidth;
            if (height < minimumHeight)
                height = minimumHeight;
            if (width > static_cast<i64>(currentWork.extent.width))
                width = currentWork.extent.width;
            if (height > static_cast<i64>(currentWork.extent.height))
                height = currentWork.extent.height;

            const i64 minimumX = currentWork.origin.x;
            const i64 minimumY = currentWork.origin.y;
            const i64 maximumX = minimumX + static_cast<i64>(currentWork.extent.width) - width;
            const i64 maximumY = minimumY + static_cast<i64>(currentWork.extent.height) - height;
            if (x < minimumX)
                x = minimumX;
            if (x > maximumX)
                x = maximumX;
            if (y < minimumY)
                y = minimumY;
            if (y > maximumY)
                y = maximumY;
        }

        if (requirePrivateWindowEncoding)
        {
            if (x < -32768 || x > 32767 || y < -32768 || y > 32767 ||
                width < 1 || width > 32767 || height < 1 || height > 32767)
                return EditorUiPlacementResolveResult::EnvironmentUnavailable;
        }
        else if (x < std::numeric_limits<i32>::min() || x > std::numeric_limits<i32>::max() ||
                 y < std::numeric_limits<i32>::min() || y > std::numeric_limits<i32>::max() ||
                 width < 1 || width > std::numeric_limits<i32>::max() ||
                 height < 1 || height > std::numeric_limits<i32>::max())
            return EditorUiPlacementResolveResult::EnvironmentUnavailable;

        repaired.rect = {static_cast<i32>(x), static_cast<i32>(y), static_cast<i32>(width), static_cast<i32>(height)};
        repaired.savedDisplayIndex = savedIndex;
        repaired.currentDisplayIndex = currentIndex;
        repaired.changed = !(repaired.rect == savedRect);
        return EditorUiPlacementResolveResult::Resolved;
    }
} // namespace vanguard::editor::detail

namespace vanguard::editor
{
    bool EncodeEditorUserStateImage(const EditorUserStateDocumentView& document, void* const destination,
                                    const usize capacity, usize& byteCount, EditorUserStateFailure* const failure) noexcept
    {
        byteCount = 0;
        ClearFailure(failure);
        if (destination == nullptr || capacity == 0)
        {
            SetFailure(failure, EditorUserStateFailureCode::InvalidArgument);
            return false;
        }

        EncodedLayout layout;
        if (!ComputeEncodedLayout(document, layout, failure) || layout.fileSize > capacity || layout.fileSize > 0xffffffffu)
        {
            if (failure == nullptr || failure->code == EditorUserStateFailureCode::None)
                SetFailure(failure, EditorUserStateFailureCode::BoundExceeded);
            return false;
        }

        std::memset(destination, 0, static_cast<usize>(layout.fileSize));
        filesystem::MemoryFileWriterExternalBuffer memoryWriter(destination, static_cast<u32>(layout.fileSize));
        serialization::BinaryWriter writer(memoryWriter);
        serialization::DocumentHeader header;
        header.magic = UserStateMagic;
        header.version = UserStateVersion;
        header.flags = UserStateDocumentFlags;
        header.fileSize = layout.fileSize;
        header.sectionTableOffset = layout.tableOffset;
        header.sectionCount = SectionCount;
        if (serialization::WriteDocumentHeader(writer, header) != serialization::Result::Success)
        {
            SetFailure(failure, EditorUserStateFailureCode::CorruptDocument);
            return false;
        }

        serialization::SectionDescriptor sections[SectionCount]{};
        for (u32 index = 0; index < SectionCount; ++index)
        {
            if (!writer.Align(SectionAlignment) || writer.Position() != layout.sectionOffsets[index])
            {
                SetFailure(failure, EditorUserStateFailureCode::CorruptDocument, SectionIds[index]);
                return false;
            }
            sections[index].id = SectionIds[index];
            sections[index].version = UserStateVersion;
            sections[index].flags = UserStateSectionFlags;
            sections[index].codec = serialization::Codec::None;
            sections[index].alignmentLog2 = 3;
            sections[index].offset = layout.sectionOffsets[index];
            sections[index].storedSize = layout.sectionSizes[index];
            sections[index].logicalSize = layout.sectionSizes[index];

            const u64 sectionStart = writer.Position();
            bool written = false;
            switch (index)
            {
            case 0:
                written = writer.WriteU64(UserStateDomain) && writer.WriteU32(document.project.parts[0]) &&
                          writer.WriteU32(document.project.parts[1]) && writer.WriteU32(document.project.parts[2]) &&
                          writer.WriteU32(document.project.parts[3]) && writer.WriteU32(0) && writer.WriteU32(0);
                break;
            case 1:
                written = WritePanelSection(writer, document, failure);
                break;
            case 2:
                written = WriteUiSection(writer, document, failure);
                break;
            case 3:
                written = WriteShortcutSection(writer, document, failure);
                break;
            case 4:
                written = WritePreferenceSection(writer, document, failure);
                break;
            default:
                break;
            }
            if (!written || writer.Position() - sectionStart != layout.sectionSizes[index])
            {
                if (failure == nullptr || failure->code == EditorUserStateFailureCode::None)
                    SetFailure(failure, EditorUserStateFailureCode::CorruptDocument, SectionIds[index]);
                return false;
            }
            sections[index].storedCrc64 = serialization::Crc64(static_cast<const u8*>(destination) + sectionStart,
                                                                static_cast<usize>(layout.sectionSizes[index]));
        }

        if (!writer.Align(SectionAlignment) || writer.Position() != layout.tableOffset)
        {
            SetFailure(failure, EditorUserStateFailureCode::CorruptDocument);
            return false;
        }
        for (const serialization::SectionDescriptor& section : sections)
            if (serialization::WriteSectionDescriptor(writer, section) != serialization::Result::Success)
            {
                SetFailure(failure, EditorUserStateFailureCode::CorruptDocument, section.id);
                return false;
            }
        if (writer.Position() != layout.digestOffset)
        {
            SetFailure(failure, EditorUserStateFailureCode::CorruptDocument);
            return false;
        }
        const crypto::Digest256 digest = crypto::Sha256(destination, static_cast<usize>(layout.digestOffset));
        if (!writer.WriteBytes(digest.bytes, sizeof(digest.bytes)) || writer.Position() != layout.fileSize ||
            memoryWriter.GetRealSize() != layout.fileSize)
        {
            SetFailure(failure, EditorUserStateFailureCode::BoundExceeded);
            return false;
        }
        byteCount = static_cast<usize>(layout.fileSize);
        return true;
    }

    EditorUserStateLoadResult ValidateEditorUserStateImage(const void* const bytes, const usize byteCount,
                                                           const projects::ProjectId& expectedProject,
                                                           EditorUserStateEnvelopeInfo& info,
                                                           EditorUserStateFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (bytes == nullptr || !expectedProject.IsValid())
        {
            SetFailure(failure, EditorUserStateFailureCode::InvalidArgument);
            return EditorUserStateLoadResult::RejectedCorrupt;
        }
        if (byteCount > MaximumEditorUserStateFileBytes)
            return RejectBounds(failure);
        if (byteCount < serialization::DocumentHeader::WireSize)
            return RejectCorrupt(failure);

        filesystem::MemoryFileReaderExternalBuffer memoryReader(bytes, static_cast<u32>(byteCount), "EditorUserState");
        serialization::BinaryReader reader(memoryReader);
        serialization::DocumentHeader header;
        serialization::ReadLimits limits;
        limits.maximumFileSize = MaximumEditorUserStateFileBytes;
        limits.maximumSections = SectionCount;
        limits.maximumAlignmentLog2 = 3;
        const serialization::Result headerResult =
            serialization::ReadDocumentHeader(reader, UserStateMagic, UserStateVersionRange, limits, header);
        if (headerResult == serialization::Result::LimitExceeded &&
            header.fileSize <= MaximumEditorUserStateFileBytes && header.sectionCount > SectionCount)
            return RejectFuture(failure);
        if (headerResult != serialization::Result::Success)
            return MapSerializationFailure(headerResult, failure);
        if (header.headerSize != serialization::DocumentHeader::WireSize || header.flags != UserStateDocumentFlags || header.sectionCount != SectionCount)
            return RejectFuture(failure);
        constexpr u64 tableSize = static_cast<u64>(SectionCount) * serialization::SectionDescriptor::WireSize;
        if (AddWouldOverflow(header.sectionTableOffset, tableSize) ||
            AddWouldOverflow(header.sectionTableOffset + tableSize, DigestBytes) ||
            header.sectionTableOffset + tableSize + DigestBytes != header.fileSize)
            return RejectCorrupt(failure);

        // Schema identity is checked directly from the bounded table before generic layout
        // validation. This preserves V1's distinction between unsupported schema shape
        // (unknown/duplicate/reordered descriptors or descriptor encoding changes) and
        // genuinely malformed offsets/overlap, which remain corruption.
        const auto* raw = static_cast<const u8*>(bytes);
        for (u32 index = 0; index < SectionCount; ++index)
        {
            const u8* const descriptor = raw + header.sectionTableOffset +
                                         static_cast<u64>(index) * serialization::SectionDescriptor::WireSize;
            if (LoadU32Le(descriptor) != SectionIds[index] ||
                LoadU16Le(descriptor + 4u) != UserStateVersion.major ||
                LoadU16Le(descriptor + 6u) != UserStateVersion.minor ||
                LoadU32Le(descriptor + 8u) != static_cast<u32>(UserStateSectionFlags) ||
                LoadU16Le(descriptor + 12u) != static_cast<u16>(serialization::Codec::None) ||
                descriptor[14] != 3 || descriptor[15] != 0)
                return RejectFuture(failure, LoadU32Le(descriptor), index);
        }

        containers::DynamicArray<serialization::SectionDescriptor> sections(memory::pools::Tools::GetInstance());
        const serialization::Result tableResult = serialization::ReadSectionTable(reader, header, limits, sections);
        // The header already proved the file-size and section-count bounds. With the exact
        // VUSR limits above, a table-level LimitExceeded can therefore only mean a section
        // requested an unsupported alignment greater than V1's frozen eight-byte alignment.
        if (tableResult == serialization::Result::LimitExceeded)
            return RejectFuture(failure);
        if (tableResult != serialization::Result::Success)
            return MapSerializationFailure(tableResult, failure);
        if (sections.Size() != SectionCount)
            return RejectCorrupt(failure);

        u64 previousEnd = header.headerSize;
        for (u32 index = 0; index < SectionCount; ++index)
        {
            const serialization::SectionDescriptor& section = sections[index];
            if (section.id != SectionIds[index] || section.version != UserStateVersion ||
                section.flags != UserStateSectionFlags || section.codec != serialization::Codec::None ||
                section.alignmentLog2 != 3 || section.logicalSize != section.storedSize)
                return RejectFuture(failure, section.id, index);
            const u64 expectedOffset = Align8(previousEnd);
            if (section.offset != expectedOffset || section.offset + section.storedSize > header.sectionTableOffset ||
                !IsZeroPadding(raw, previousEnd, expectedOffset))
                return RejectCorrupt(failure, section.id, index);
            if (serialization::Crc64(raw + section.offset, static_cast<usize>(section.storedSize)) != section.storedCrc64)
                return RejectCorrupt(failure, section.id, index);
            previousEnd = section.offset + section.storedSize;
        }
        const u64 expectedTableOffset = Align8(previousEnd);
        if (header.sectionTableOffset != expectedTableOffset || !IsZeroPadding(raw, previousEnd, expectedTableOffset))
            return RejectCorrupt(failure);

        const u64 digestOffset = header.sectionTableOffset + tableSize;
        crypto::Digest256 storedDigest;
        std::memcpy(storedDigest.bytes, raw + digestOffset, DigestBytes);
        if (!(storedDigest == crypto::Sha256(bytes, static_cast<usize>(digestOffset))))
            return RejectCorrupt(failure);

        EditorUserStateEnvelopeInfo parsed;
        parsed.version = header.version;
        EditorUserStateLoadResult result = ParseMetaSection(reader, sections[0], expectedProject, parsed, failure);
        if (result != EditorUserStateLoadResult::Restored)
            return result;
        result = ParsePanelsSection(reader, sections[1], parsed, failure);
        if (result != EditorUserStateLoadResult::Restored)
            return result;
        result = ParseUiSection(reader, sections[2], parsed, failure);
        if (result != EditorUserStateLoadResult::Restored)
            return result;
        result = ParseShortcutsSection(reader, sections[3], parsed, failure);
        if (result != EditorUserStateLoadResult::Restored)
            return result;
        result = ParsePreferencesSection(reader, sections[4], parsed, failure);
        if (result != EditorUserStateLoadResult::Restored)
            return result;
        info = parsed;
        return EditorUserStateLoadResult::Restored;
    }

    namespace detail
    {
        EditorUserStateLoadResult ValidateEditorUserStateImageWithMigrations(
            const void* const bytes, const usize byteCount, const projects::ProjectId& expectedProject,
            const containers::ArraySpan<const EditorUserStateMigrationDecoder> migrations,
            EditorUserStateEnvelopeInfo& info, EditorUserStateFailure* const failure) noexcept
        {
            const EditorUserStateLoadResult current =
                ValidateEditorUserStateImage(bytes, byteCount, expectedProject, info, failure);
            if (current != EditorUserStateLoadResult::RejectedFuture || migrations.Empty() || bytes == nullptr ||
                byteCount < serialization::DocumentHeader::WireSize || byteCount > MaximumEditorUserStateFileBytes)
                return current;

            const auto* const raw = static_cast<const u8*>(bytes);
            const serialization::Version sourceVersion{LoadU16Le(raw + 8u), LoadU16Le(raw + 10u)};
            const bool isStrictlyOlder = sourceVersion.major < UserStateVersion.major ||
                                         (sourceVersion.major == UserStateVersion.major &&
                                          sourceVersion.minor < UserStateVersion.minor);
            if (!isStrictlyOlder)
                return current;

            for (u32 index = 0; index < migrations.Size(); ++index)
            {
                const EditorUserStateMigrationDecoder& migration = migrations[index];
                if (!(migration.sourceVersion == sourceVersion) || migration.decode == nullptr)
                    continue;

                filesystem::MemoryFileReaderExternalBuffer memoryReader(bytes, static_cast<u32>(byteCount), "EditorUserStateMigration");
                serialization::BinaryReader reader(memoryReader);
                serialization::ReadLimits limits;
                limits.maximumFileSize = MaximumEditorUserStateFileBytes;
                limits.maximumSections = SectionCount;
                limits.maximumAlignmentLog2 = 3;
                serialization::DocumentHeader sourceHeader;
                const serialization::VersionRange exactSource{sourceVersion.major, sourceVersion.minor, sourceVersion.minor};
                if (serialization::ReadDocumentHeader(reader, UserStateMagic, exactSource, limits, sourceHeader) !=
                    serialization::Result::Success)
                    return current;

                EditorUserStateEnvelopeInfo migratedInfo;
                EditorUserStateFailure migrationFailure;
                const EditorUserStateLoadResult migrated = migration.decode(
                    bytes, byteCount, expectedProject, migratedInfo, &migrationFailure, migration.userData);
                if (migrated == EditorUserStateLoadResult::Migrated)
                {
                    info = migratedInfo;
                    ClearFailure(failure);
                    return migrated;
                }
                if (failure != nullptr)
                    *failure = migrationFailure;
                return migrated;
            }
            return current;
        }

        bool DecodeEditorUserStatePanelLayoutRestore(
            const void* const bytes, const usize byteCount, const projects::ProjectId& expectedProject,
            EditorPanelUserStateRecord* const panels, const u32 panelCapacity, u32& panelCount,
            EditorUiSavedDisplay* const displays, const u32 displayCapacity, u32& displayCount,
            u8* const imguiBytes, const u32 imguiCapacity, u32& imguiByteCount,
            EditorUserStateFailure* const failure) noexcept
        {
            panelCount = 0;
            displayCount = 0;
            imguiByteCount = 0;
            EditorUserStateEnvelopeInfo info;
            const EditorUserStateLoadResult validation =
                ValidateEditorUserStateImage(bytes, byteCount, expectedProject, info, failure);
            if (validation != EditorUserStateLoadResult::Restored)
                return false;

            panelCount = info.panelRecords;
            displayCount = info.displayRecords;
            imguiByteCount = info.imguiByteCount;
            if (panelCount > panelCapacity || displayCount > displayCapacity || imguiByteCount > imguiCapacity)
            {
                SetFailure(failure, EditorUserStateFailureCode::BoundExceeded);
                return false;
            }
            if ((panelCount != 0 && panels == nullptr) || (displayCount != 0 && displays == nullptr) ||
                (imguiByteCount != 0 && imguiBytes == nullptr))
            {
                SetFailure(failure, EditorUserStateFailureCode::InvalidArgument);
                return false;
            }

            const auto* const raw = static_cast<const u8*>(bytes);
            const u64 tableOffset = LoadU64Le(raw + 24u);
            const u8* const panelDescriptor =
                raw + tableOffset + static_cast<u64>(1u) * serialization::SectionDescriptor::WireSize;
            const u8* const uiDescriptor =
                raw + tableOffset + static_cast<u64>(2u) * serialization::SectionDescriptor::WireSize;
            const u64 panelOffset = LoadU64Le(panelDescriptor + 16u);
            const u64 uiOffset = LoadU64Le(uiDescriptor + 16u);

            filesystem::MemoryFileReaderExternalBuffer memoryReader(bytes, static_cast<u32>(byteCount),
                                                                    "EditorUserStatePanelLayoutRestore");
            serialization::BinaryReader reader(memoryReader);
            u32 wirePanelCount = 0, panelReserved = 0;
            if (!reader.Seek(panelOffset) || !reader.ReadU32(wirePanelCount) || !reader.ReadU32(panelReserved) ||
                wirePanelCount != panelCount || panelReserved != 0)
            {
                SetFailure(failure, EditorUserStateFailureCode::CorruptDocument, PanelsSection);
                return false;
            }
            for (u32 index = 0; index < panelCount; ++index)
            {
                EditorPanelUserStateRecord record;
                u8 open = 0, reserved8 = 0;
                u32 reserved32 = 0;
                if (!reader.ReadU64(record.type) || !reader.ReadU64(record.instance) ||
                    !reader.ReadU16(record.fallbackTitleByteCount) || !reader.ReadU8(open) ||
                    !reader.ReadU8(reserved8) || !reader.ReadU32(reserved32) ||
                    !reader.ReadBytes(record.fallbackTitle, record.fallbackTitleByteCount) ||
                    !ReadZeroPadding(reader, SectionAlignment))
                {
                    SetFailure(failure, EditorUserStateFailureCode::CorruptDocument, PanelsSection, index);
                    return false;
                }
                record.open = open != 0;
                record.fallbackTitle[record.fallbackTitleByteCount] = '\0';
                panels[index] = record;
            }

            u32 wireDisplayCount = 0, wireImguiByteCount = 0, reserved0 = 0, reserved1 = 0;
            if (!reader.Seek(uiOffset) || !reader.ReadU32(wireDisplayCount) || !reader.ReadU32(wireImguiByteCount) ||
                !reader.ReadU32(reserved0) || !reader.ReadU32(reserved1) ||
                wireDisplayCount != displayCount || wireImguiByteCount != imguiByteCount ||
                reserved0 != 0 || reserved1 != 0)
            {
                SetFailure(failure, EditorUserStateFailureCode::CorruptDocument, UiStateSection);
                return false;
            }
            for (u32 index = 0; index < displayCount; ++index)
            {
                EditorUiSavedDisplay display;
                u8 primary = 0, pad0 = 0, pad1 = 0, pad2 = 0;
                if (!reader.ReadI32(display.boundsX) || !reader.ReadI32(display.boundsY) ||
                    !reader.ReadU32(display.boundsWidth) || !reader.ReadU32(display.boundsHeight) ||
                    !reader.ReadI32(display.workX) || !reader.ReadI32(display.workY) ||
                    !reader.ReadU32(display.workWidth) || !reader.ReadU32(display.workHeight) ||
                    !reader.ReadU32(display.desktopPixelWidth) || !reader.ReadU32(display.desktopPixelHeight) ||
                    !reader.ReadU32(display.scaleQ16_16) || !reader.ReadU8(primary) ||
                    !reader.ReadU8(pad0) || !reader.ReadU8(pad1) || !reader.ReadU8(pad2))
                {
                    SetFailure(failure, EditorUserStateFailureCode::CorruptDocument, UiStateSection, index);
                    return false;
                }
                display.primary = primary != 0;
                displays[index] = display;
            }
            if (imguiByteCount != 0 && !reader.ReadBytes(imguiBytes, imguiByteCount))
            {
                SetFailure(failure, EditorUserStateFailureCode::CorruptDocument, UiStateSection);
                return false;
            }
            ClearFailure(failure);
            return true;
        }

        bool DecodeEditorUserStateRestoreState(
            const void* const bytes, const usize byteCount, const projects::ProjectId& expectedProject,
            EditorPanelUserStateRecord* const panels, const u32 panelCapacity, u32& panelCount,
            EditorUiSavedDisplay* const displays, const u32 displayCapacity, u32& displayCount,
            u8* const imguiBytes, const u32 imguiCapacity, u32& imguiByteCount,
            EditorShortcutPreferenceRecord* const shortcuts, const u32 shortcutCapacity, u32& shortcutCount,
            EditorTypedPreferenceRecord* const preferences, const u32 preferenceCapacity, u32& preferenceCount,
            EditorUserStateFailure* const failure) noexcept
        {
            shortcutCount = 0;
            preferenceCount = 0;
            if (!DecodeEditorUserStatePanelLayoutRestore(
                    bytes, byteCount, expectedProject, panels, panelCapacity, panelCount,
                    displays, displayCapacity, displayCount, imguiBytes, imguiCapacity,
                    imguiByteCount, failure))
                return false;

            EditorUserStateEnvelopeInfo info;
            if (ValidateEditorUserStateImage(bytes, byteCount, expectedProject, info, failure) !=
                EditorUserStateLoadResult::Restored)
                return false;
            shortcutCount = info.shortcutRecords;
            preferenceCount = info.preferenceRecords;
            if (shortcutCount > shortcutCapacity || preferenceCount > preferenceCapacity)
            {
                SetFailure(failure, EditorUserStateFailureCode::BoundExceeded);
                return false;
            }
            if ((shortcutCount != 0 && shortcuts == nullptr) ||
                (preferenceCount != 0 && preferences == nullptr))
            {
                SetFailure(failure, EditorUserStateFailureCode::InvalidArgument);
                return false;
            }

            const auto* const raw = static_cast<const u8*>(bytes);
            const u64 tableOffset = LoadU64Le(raw + 24u);
            const u8* const shortcutDescriptor =
                raw + tableOffset + static_cast<u64>(3u) * serialization::SectionDescriptor::WireSize;
            const u8* const preferenceDescriptor =
                raw + tableOffset + static_cast<u64>(4u) * serialization::SectionDescriptor::WireSize;
            const u64 shortcutOffset = LoadU64Le(shortcutDescriptor + 16u);
            const u64 preferenceOffset = LoadU64Le(preferenceDescriptor + 16u);

            filesystem::MemoryFileReaderExternalBuffer memoryReader(
                bytes, static_cast<u32>(byteCount), "EditorUserStateRestore");
            serialization::BinaryReader reader(memoryReader);

            u32 wireShortcutCount = 0, shortcutReserved = 0;
            if (!reader.Seek(shortcutOffset) || !reader.ReadU32(wireShortcutCount) ||
                !reader.ReadU32(shortcutReserved) || wireShortcutCount != shortcutCount ||
                shortcutReserved != 0)
            {
                SetFailure(failure, EditorUserStateFailureCode::CorruptDocument, ShortcutsSection);
                return false;
            }
            for (u32 index = 0; index < shortcutCount; ++index)
            {
                u64 contextId = 0, commandId = 0, reserved64 = 0;
                u16 keyCode = 0;
                u8 modifiers = 0, mode = 0;
                u32 reserved32 = 0;
                if (!reader.ReadU64(contextId) || !reader.ReadU64(commandId) ||
                    !reader.ReadU16(keyCode) || !reader.ReadU8(modifiers) ||
                    !reader.ReadU8(mode) || !reader.ReadU32(reserved32) ||
                    !reader.ReadU64(reserved64))
                {
                    SetFailure(failure, EditorUserStateFailureCode::CorruptDocument,
                               ShortcutsSection, index);
                    return false;
                }
                EditorShortcutPreferenceRecord record;
                record.contextType = {contextId};
                record.command = {commandId};
                if (mode == static_cast<u8>(EditorUserStateShortcutMode::Unbound))
                {
                    record.shortcutOverride.mode = EditorShortcutOverrideMode::Unbound;
                }
                else
                {
                    record.shortcutOverride.mode = EditorShortcutOverrideMode::Custom;
                    record.shortcutOverride.chord.key = static_cast<input::Key>(keyCode);
                    record.shortcutOverride.chord.modifiers =
                        static_cast<EditorShortcutModifiers>(modifiers);
                }
                shortcuts[index] = record;
            }

            u32 wirePreferenceCount = 0, preferenceReserved = 0;
            if (!reader.Seek(preferenceOffset) || !reader.ReadU32(wirePreferenceCount) ||
                !reader.ReadU32(preferenceReserved) || wirePreferenceCount != preferenceCount ||
                preferenceReserved != 0)
            {
                SetFailure(failure, EditorUserStateFailureCode::CorruptDocument, PreferencesSection);
                return false;
            }
            for (u32 index = 0; index < preferenceCount; ++index)
            {
                EditorTypedPreferenceRecord record;
                u8 valueType = 0, reserved0 = 0, reserved1 = 0, reserved2 = 0;
                u32 payloadBytes = 0;
                u64 reserved64 = 0;
                if (!reader.ReadU64(record.extension.value) ||
                    !reader.ReadU64(record.providerId) || !reader.ReadU64(record.fieldId) ||
                    !reader.ReadU8(valueType) || !reader.ReadU8(reserved0) ||
                    !reader.ReadU8(reserved1) || !reader.ReadU8(reserved2) ||
                    !reader.ReadU32(payloadBytes) || !reader.ReadU64(reserved64))
                {
                    SetFailure(failure, EditorUserStateFailureCode::CorruptDocument,
                               PreferencesSection, index);
                    return false;
                }
                record.value.type = static_cast<EditorPreferenceValueType>(valueType);
                switch (record.value.type)
                {
                case EditorPreferenceValueType::Bool:
                {
                    u8 value = 0;
                    if (!reader.ReadU8(value))
                    {
                        SetFailure(failure, EditorUserStateFailureCode::CorruptDocument,
                                   PreferencesSection, index);
                        return false;
                    }
                    record.value.value.boolean = value != 0;
                    break;
                }
                case EditorPreferenceValueType::SignedInteger:
                    if (!reader.ReadI64(record.value.value.signedInteger))
                    {
                        SetFailure(failure, EditorUserStateFailureCode::CorruptDocument,
                                   PreferencesSection, index);
                        return false;
                    }
                    break;
                case EditorPreferenceValueType::UnsignedBitMask:
                    if (!reader.ReadU64(record.value.value.unsignedBitMask))
                    {
                        SetFailure(failure, EditorUserStateFailureCode::CorruptDocument,
                                   PreferencesSection, index);
                        return false;
                    }
                    break;
                case EditorPreferenceValueType::Utf8String:
                    record.value.value.string.byteCount = static_cast<u16>(payloadBytes);
                    if (!reader.ReadBytes(record.value.value.string.bytes, payloadBytes))
                    {
                        SetFailure(failure, EditorUserStateFailureCode::CorruptDocument,
                                   PreferencesSection, index);
                        return false;
                    }
                    record.value.value.string.bytes[payloadBytes] = '\0';
                    break;
                default:
                    SetFailure(failure, EditorUserStateFailureCode::CorruptDocument,
                               PreferencesSection, index);
                    return false;
                }
                if (!ReadZeroPadding(reader, SectionAlignment))
                {
                    SetFailure(failure, EditorUserStateFailureCode::CorruptDocument,
                               PreferencesSection, index);
                    return false;
                }
                preferences[index] = record;
            }
            ClearFailure(failure);
            return true;
        }
    } // namespace detail

    EditorUserStateLoadResult LoadEditorUserStateImage(const detail::EditorUserStateFileOps& fileOps,
                                                       const filesystem::AbsolutePath& canonicalPath,
                                                       const projects::ProjectId& expectedProject,
                                                       void* const destination, const usize capacity, usize& byteCount,
                                                       EditorUserStateEnvelopeInfo& info,
                                                       EditorUserStateFailure* const failure) noexcept
    {
        byteCount = 0;
        ClearFailure(failure);
        if (!fileOps.IsValid() || canonicalPath.Empty() || !canonicalPath.IsFilePath() || !expectedProject.IsValid() ||
            destination == nullptr || capacity == 0)
        {
            SetFailure(failure, EditorUserStateFailureCode::InvalidArgument);
            return EditorUserStateLoadResult::RejectedCorrupt;
        }

        const detail::EditorUserStateFileReadResult read =
            fileOps.readComplete(canonicalPath, destination, capacity, byteCount, fileOps.userData);
        switch (read)
        {
        case detail::EditorUserStateFileReadResult::Success:
            break;
        case detail::EditorUserStateFileReadResult::Missing:
            byteCount = 0;
            return EditorUserStateLoadResult::Missing;
        case detail::EditorUserStateFileReadResult::LimitExceeded:
            byteCount = 0;
            SetFailure(failure, EditorUserStateFailureCode::BoundExceeded, 0, 0, static_cast<u32>(read));
            return EditorUserStateLoadResult::RejectedBounds;
        case detail::EditorUserStateFileReadResult::OpenFailed:
        case detail::EditorUserStateFileReadResult::ReadFailed:
        case detail::EditorUserStateFileReadResult::UnsupportedPlatform:
            byteCount = 0;
            SetFailure(failure, EditorUserStateFailureCode::CanonicalIo, 0, 0, static_cast<u32>(read));
            return EditorUserStateLoadResult::IoFailure;
        default:
            byteCount = 0;
            SetFailure(failure, EditorUserStateFailureCode::CanonicalIo);
            return EditorUserStateLoadResult::IoFailure;
        }

        EditorUserStateEnvelopeInfo parsed;
        const EditorUserStateLoadResult result = detail::ValidateEditorUserStateImageWithMigrations(
            destination, byteCount, expectedProject, {}, parsed, failure);
        if (result == EditorUserStateLoadResult::Restored || result == EditorUserStateLoadResult::Migrated)
            info = parsed;
        return result;
    }

    filesystem::AbsolutePath MakeEditorUserStateStagePath(const filesystem::AbsolutePath& directory,
                                                          const detail::EditorUserStateStageIdentity& identity) noexcept
    {
        if (directory.Empty() || !directory.IsDirectoryPath() || identity.saveSerial == 0)
            return {};
        char name[96]{};
        char* cursor = name;
        constexpr char prefix[] = "user-state.vstate.stage-";
        std::memcpy(cursor, prefix, sizeof(prefix) - 1u);
        cursor += sizeof(prefix) - 1u;
        constexpr char hex[] = "0123456789abcdef";
        for (const u8 byte : identity.nonce)
        {
            *cursor++ = hex[byte >> 4u];
            *cursor++ = hex[byte & 0x0fu];
        }
        *cursor++ = '-';
        for (i32 shift = 60; shift >= 0; shift -= 4)
            *cursor++ = hex[(identity.saveSerial >> static_cast<u32>(shift)) & 0x0full];
        *cursor = '\0';
        return directory.AddFilePath(name);
    }

    EditorUserStateSaveResult PublishEditorUserStateImage(const detail::EditorUserStateFileOps& fileOps,
                                                           const filesystem::AbsolutePath& directory,
                                                           const filesystem::AbsolutePath& canonicalPath,
                                                           const filesystem::AbsolutePath& stagedPath,
                                                           const EditorUserStateDocumentView& document,
                                                           void* const imageScratch, void* const validationScratch,
                                                           const usize scratchCapacity,
                                                           EditorUserStateFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!fileOps.IsValid() || directory.Empty() || !directory.IsDirectoryPath() || canonicalPath.Empty() ||
            !canonicalPath.IsFilePath() || stagedPath.Empty() || !stagedPath.IsFilePath() ||
            filesystem::paths::ParentAbsolutePath(canonicalPath) != directory ||
            filesystem::paths::ParentAbsolutePath(stagedPath) != directory || canonicalPath == stagedPath ||
            imageScratch == nullptr || validationScratch == nullptr || scratchCapacity < MaximumEditorUserStateFileBytes)
        {
            SetFailure(failure, EditorUserStateFailureCode::InvalidArgument);
            return EditorUserStateSaveResult::StageOpenFailed;
        }

        usize imageBytes = 0;
        if (!EncodeEditorUserStateImage(document, imageScratch, scratchCapacity, imageBytes, failure))
            return EditorUserStateSaveResult::StageValidationFailed;
        if (!fileOps.ensureDirectory(directory, fileOps.userData))
        {
            SetFailure(failure, EditorUserStateFailureCode::DirectoryCreateFailed);
            return EditorUserStateSaveResult::StageOpenFailed;
        }

        bool ownsStage = false;
        EditorUserStateSaveResult primaryFailure = EditorUserStateSaveResult::NotAttempted;
        const auto cleanupOwnedStage = [&]() noexcept -> EditorUserStateSaveResult {
            if (!ownsStage)
                return primaryFailure;
            if (!fileOps.removeOwnedStage(stagedPath, fileOps.userData))
            {
                SetFailure(failure, EditorUserStateFailureCode::CleanupFailed, 0, 0, static_cast<u32>(primaryFailure));
                return EditorUserStateSaveResult::CleanupFailed;
            }
            ownsStage = false;
            return primaryFailure;
        };

        const filesystem::ExclusiveFileWriteResult write =
            fileOps.writeExclusive(stagedPath, imageScratch, imageBytes, fileOps.userData);
        switch (write)
        {
        case filesystem::ExclusiveFileWriteResult::Success:
            ownsStage = true;
            break;
        case filesystem::ExclusiveFileWriteResult::WriteFailed:
            ownsStage = true;
            primaryFailure = EditorUserStateSaveResult::StageWriteFailed;
            SetFailure(failure, EditorUserStateFailureCode::StageWriteFailed, 0, 0, static_cast<u32>(write));
            return cleanupOwnedStage();
        case filesystem::ExclusiveFileWriteResult::FlushFailed:
            ownsStage = true;
            primaryFailure = EditorUserStateSaveResult::StageFlushFailed;
            SetFailure(failure, EditorUserStateFailureCode::StageFlushFailed, 0, 0, static_cast<u32>(write));
            return cleanupOwnedStage();
        case filesystem::ExclusiveFileWriteResult::AlreadyExists:
        case filesystem::ExclusiveFileWriteResult::OpenFailed:
        case filesystem::ExclusiveFileWriteResult::UnsupportedPlatform:
            SetFailure(failure, EditorUserStateFailureCode::StageOpenFailed, 0, 0, static_cast<u32>(write));
            return EditorUserStateSaveResult::StageOpenFailed;
        default:
            SetFailure(failure, EditorUserStateFailureCode::StageOpenFailed);
            return EditorUserStateSaveResult::StageOpenFailed;
        }

        usize validationBytes = 0;
        const detail::EditorUserStateFileReadResult reopened =
            fileOps.readComplete(stagedPath, validationScratch, scratchCapacity, validationBytes, fileOps.userData);
        if (reopened != detail::EditorUserStateFileReadResult::Success)
        {
            primaryFailure = EditorUserStateSaveResult::StageValidationFailed;
            SetFailure(failure, EditorUserStateFailureCode::StageValidationFailed, 0, 0, static_cast<u32>(reopened));
            return cleanupOwnedStage();
        }

        EditorUserStateEnvelopeInfo stagedInfo;
        EditorUserStateFailure validationFailure;
        if (detail::ValidateEditorUserStateImageWithMigrations(validationScratch, validationBytes, document.project, {}, stagedInfo,
                                                              &validationFailure) !=
            EditorUserStateLoadResult::Restored)
        {
            primaryFailure = EditorUserStateSaveResult::StageValidationFailed;
            if (failure != nullptr)
                *failure = validationFailure;
            return cleanupOwnedStage();
        }

        if (!fileOps.replace(stagedPath, canonicalPath, fileOps.userData))
        {
            primaryFailure = EditorUserStateSaveResult::ReplaceFailed;
            SetFailure(failure, EditorUserStateFailureCode::ReplaceFailed);
            return cleanupOwnedStage();
        }
        ownsStage = false;
        return EditorUserStateSaveResult::Published;
    }
} // namespace vanguard::editor

namespace
{
    [[nodiscard]] bool EnsureUserStateDirectory(const filesystem::AbsolutePath& directory, void*) noexcept
    {
        return filesystem::IsInitialized() && filesystem::GetManager().CreatePath(directory);
    }

    [[nodiscard]] editor::detail::EditorUserStateFileReadResult ReadUserStateFile(const filesystem::AbsolutePath& path,
                                                                                  void* const destination, const usize capacity,
                                                                                  usize& byteCount, void*) noexcept
    {
        switch (filesystem::ReadCompleteFile(path, destination, capacity, byteCount))
        {
        case filesystem::CompleteFileReadResult::Success:
            return editor::detail::EditorUserStateFileReadResult::Success;
        case filesystem::CompleteFileReadResult::Missing:
            return editor::detail::EditorUserStateFileReadResult::Missing;
        case filesystem::CompleteFileReadResult::OpenFailed:
            return editor::detail::EditorUserStateFileReadResult::OpenFailed;
        case filesystem::CompleteFileReadResult::ReadFailed:
            return editor::detail::EditorUserStateFileReadResult::ReadFailed;
        case filesystem::CompleteFileReadResult::LimitExceeded:
            return editor::detail::EditorUserStateFileReadResult::LimitExceeded;
        case filesystem::CompleteFileReadResult::UnsupportedPlatform:
        default:
            return editor::detail::EditorUserStateFileReadResult::UnsupportedPlatform;
        }
    }

    [[nodiscard]] filesystem::ExclusiveFileWriteResult WriteUserStateFile(const filesystem::AbsolutePath& path,
                                                                           const void* const bytes, const usize byteCount,
                                                                           void*) noexcept
    {
        return filesystem::WriteExclusiveFile(path, bytes, byteCount);
    }

    [[nodiscard]] bool ReplaceUserStateFile(const filesystem::AbsolutePath& staged,
                                            const filesystem::AbsolutePath& target, void*) noexcept
    {
        return filesystem::ReplaceFile(staged, target);
    }

    [[nodiscard]] bool RemoveUserStateStage(const filesystem::AbsolutePath& staged, void*) noexcept
    {
        return filesystem::IsInitialized() && filesystem::GetManager().DeleteFile(staged);
    }

    constexpr editor::detail::EditorUserStateFileOps ProductionUserStateFileOps{
        EnsureUserStateDirectory, ReadUserStateFile, WriteUserStateFile, ReplaceUserStateFile, RemoveUserStateStage, nullptr};

    constexpr serialization::Version SessionReferenceVersion{1, 0};
    constexpr u32 SessionInstanceNameBytes = 39; // 32 lowercase hex nonce bytes + ".vstate".

    struct SessionReference final
    {
        char instance[SessionInstanceNameBytes + 1u]{};
        u64 checkpointSequence = 0;
        char supersededLatestInstance[SessionInstanceNameBytes + 1u]{};
        u64 supersededLatestCheckpointSequence = 0;
        bool hasSupersededLatest = false;
    };

    [[nodiscard]] bool CleanReferenceSupersedesLatest(const SessionReference& clean,
                                                      const SessionReference& latest) noexcept
    {
        return clean.hasSupersededLatest &&
               clean.supersededLatestCheckpointSequence == latest.checkpointSequence &&
               std::memcmp(clean.supersededLatestInstance, latest.instance,
                           SessionInstanceNameBytes + 1u) == 0;
    }

    enum class SessionCandidateLoadResult : u8
    {
        Loaded,
        Missing,
        Corrupt,
        IoFailure
    };

    enum class SessionRestorePrepareResult : u8
    {
        Prepared,
        DocumentRejected,
        OwnerInvariantFailed,
        AllocationFailed
    };

    struct EditorUserStateTextValidationContext final
    {
        projects::ProjectId expectedProject;
        u64 expectedCheckpointSequence = 0;
        editor::detail::EditorUserStateTextDecodedStorage* decoded = nullptr;
        void* semanticScratch = nullptr;
        usize semanticCapacity = 0;
        usize semanticByteCount = 0;
        editor::detail::EditorUserStateTextFailure textFailure;
        editor::EditorUserStateFailure semanticFailure;
    };

    [[nodiscard]] bool ValidateEditorUserStateTextArtifact(const void* const bytes, const usize byteCount,
                                                           void* const userData) noexcept
    {
        auto* const context = static_cast<EditorUserStateTextValidationContext*>(userData);
        if (context == nullptr || context->decoded == nullptr || context->semanticScratch == nullptr ||
            byteCount == 0 || byteCount > editor::detail::MaximumEditorUserStateTextBytes ||
            bytes == nullptr)
            return false;
        context->semanticByteCount = 0;
        context->textFailure = {};
        context->semanticFailure = {};
        if (editor::detail::DecodeEditorUserStateText(
                {static_cast<const char*>(bytes), static_cast<u32>(byteCount)}, *context->decoded,
                &context->textFailure) != editor::detail::EditorUserStateTextResult::Success)
            return false;
        const editor::detail::EditorUserStateTextDocumentView decoded = context->decoded->View();
        if (!(decoded.document.project == context->expectedProject))
        {
            context->semanticFailure.code = editor::EditorUserStateFailureCode::ProjectMismatch;
            return false;
        }
        if (context->expectedCheckpointSequence != 0 &&
            decoded.checkpointSequence != context->expectedCheckpointSequence)
        {
            context->semanticFailure.code = editor::EditorUserStateFailureCode::CorruptDocument;
            return false;
        }
        return editor::EncodeEditorUserStateImage(
            decoded.document, context->semanticScratch, context->semanticCapacity,
            context->semanticByteCount, &context->semanticFailure);
    }

    [[nodiscard]] bool IsSessionInstanceName(const containers::StringView name) noexcept
    {
        if (name.Length() != SessionInstanceNameBytes || name.SubView(32u) != ".vstate")
            return false;
        for (u32 index = 0; index < 32u; ++index)
        {
            const char value = name[index];
            if (!((value >= '0' && value <= '9') || (value >= 'a' && value <= 'f')))
                return false;
        }
        return true;
    }

    void FormatSessionInstanceName(const u8 (&nonce)[16], char (&name)[SessionInstanceNameBytes + 1u]) noexcept
    {
        constexpr char Hex[] = "0123456789abcdef";
        u32 cursor = 0;
        for (const u8 byte : nonce)
        {
            name[cursor++] = Hex[byte >> 4u];
            name[cursor++] = Hex[byte & 0x0fu];
        }
        constexpr char Extension[] = ".vstate";
        for (u32 index = 0; index + 1u < sizeof(Extension); ++index)
            name[cursor++] = Extension[index];
        name[cursor] = '\0';
    }

    [[nodiscard]] bool DecodeSessionReference(const containers::StringView source,
                                              SessionReference& reference) noexcept
    {
        reference = {};
        serialization::TextDocumentReader reader(source);
        if (!reader.IsUtf8Valid())
            return false;
        serialization::TextDocumentHeader header;
        if (reader.ReadHeader(header) != serialization::Result::Success || header.kind != "vsessionref" ||
            header.version.major != SessionReferenceVersion.major)
            return false;

        bool hasInstance = false;
        bool hasCheckpointSequence = false;
        bool hasSupersededLatestInstance = false;
        bool hasSupersededLatestCheckpointSequence = false;
        containers::String decoded;
        for (;;)
        {
            serialization::TextAssignment assignment;
            const serialization::Result result = reader.ReadAssignment(assignment, decoded);
            if (result == serialization::Result::EndOfStream)
                break;
            if (result != serialization::Result::Success)
                return false;
            if (assignment.key == "instance")
            {
                if (hasInstance || !IsSessionInstanceName(decoded))
                    return false;
                std::memcpy(reference.instance, decoded.AsChar(), decoded.Length());
                reference.instance[decoded.Length()] = '\0';
                hasInstance = true;
            }
            else if (assignment.key == "checkpoint.sequence")
            {
                if (hasCheckpointSequence)
                    return false;
                u64 sequence = 0;
                for (const char digit : decoded)
                {
                    if (digit < '0' || digit > '9')
                        return false;
                    const u64 next = static_cast<u64>(digit - '0');
                    if (sequence > (std::numeric_limits<u64>::max() - next) / 10u)
                        return false;
                    sequence = sequence * 10u + next;
                }
                if (decoded.Empty() || sequence == 0)
                    return false;
                reference.checkpointSequence = sequence;
                hasCheckpointSequence = true;
            }
            else if (assignment.key == "supersedes.latest.instance")
            {
                if (hasSupersededLatestInstance || !IsSessionInstanceName(decoded))
                    return false;
                std::memcpy(reference.supersededLatestInstance, decoded.AsChar(), decoded.Length());
                reference.supersededLatestInstance[decoded.Length()] = '\0';
                hasSupersededLatestInstance = true;
            }
            else if (assignment.key == "supersedes.latest.checkpoint.sequence")
            {
                if (hasSupersededLatestCheckpointSequence)
                    return false;
                u64 sequence = 0;
                for (const char digit : decoded)
                {
                    if (digit < '0' || digit > '9')
                        return false;
                    const u64 next = static_cast<u64>(digit - '0');
                    if (sequence > (std::numeric_limits<u64>::max() - next) / 10u)
                        return false;
                    sequence = sequence * 10u + next;
                }
                if (decoded.Empty() || sequence == 0)
                    return false;
                reference.supersededLatestCheckpointSequence = sequence;
                hasSupersededLatestCheckpointSequence = true;
            }
        }
        if (hasSupersededLatestInstance != hasSupersededLatestCheckpointSequence)
            return false;
        reference.hasSupersededLatest = hasSupersededLatestInstance;
        return hasInstance && hasCheckpointSequence;
    }

    [[nodiscard]] bool ValidateSessionReferenceArtifact(const void* const bytes, const usize byteCount,
                                                        void*) noexcept
    {
        if (bytes == nullptr || byteCount == 0 ||
            byteCount > editor::detail::MaximumEditorStateReferenceBytes)
            return false;
        SessionReference reference;
        return DecodeSessionReference(
            {static_cast<const char*>(bytes), static_cast<u32>(byteCount)}, reference);
    }

    [[nodiscard]] bool EncodeSessionReference(const containers::StringView instance,
                                              const u64 checkpointSequence,
                                              const SessionReference* const supersededLatest,
                                              containers::String& destination) noexcept
    {
        destination.Clear();
        if (!IsSessionInstanceName(instance) || checkpointSequence == 0 ||
            serialization::AppendTextDocumentHeader(destination, "vsessionref", SessionReferenceVersion) !=
                serialization::Result::Success)
            return false;
        destination.Append('\n');
        char sequence[21]{};
        const int sequenceLength = std::snprintf(sequence, sizeof(sequence), "%llu",
                                                 static_cast<unsigned long long>(checkpointSequence));
        if (sequenceLength <= 0 || static_cast<usize>(sequenceLength) >= sizeof(sequence) ||
            serialization::AppendTextAssignment(destination, "instance", instance) !=
                serialization::Result::Success ||
            serialization::AppendTextAssignment(
                destination, "checkpoint.sequence",
                {sequence, static_cast<u32>(sequenceLength)}) != serialization::Result::Success)
            return false;
        if (supersededLatest != nullptr)
        {
            if (!IsSessionInstanceName(supersededLatest->instance) ||
                supersededLatest->checkpointSequence == 0)
                return false;
            char supersededSequence[21]{};
            const int supersededSequenceLength =
                std::snprintf(supersededSequence, sizeof(supersededSequence), "%llu",
                              static_cast<unsigned long long>(supersededLatest->checkpointSequence));
            if (supersededSequenceLength <= 0 ||
                static_cast<usize>(supersededSequenceLength) >= sizeof(supersededSequence) ||
                serialization::AppendTextAssignment(destination, "supersedes.latest.instance",
                                                    supersededLatest->instance) !=
                    serialization::Result::Success ||
                serialization::AppendTextAssignment(
                    destination, "supersedes.latest.checkpoint.sequence",
                    {supersededSequence, static_cast<u32>(supersededSequenceLength)}) !=
                    serialization::Result::Success)
                return false;
        }
        return destination.Length() <= editor::detail::MaximumEditorStateReferenceBytes;
    }

    struct EditorUserStateRestoreScratch final
    {
        editor::detail::EditorPanelUserStateRecord panels[editor::MaximumEditorUserStatePanels]{};
        editor::detail::EditorUiSavedDisplay displays[editor::MaximumEditorUserStateDisplays]{};
        u8 imguiBytes[editor::MaximumEditorUserStateImguiBytes]{};
        editor::EditorShortcutPreferenceRecord shortcuts[editor::MaximumEditorUserStateShortcuts]{};
        editor::EditorShortcutPreferenceItemResult shortcutResults[editor::MaximumEditorUserStateShortcuts]{};
        editor::EditorTypedPreferenceRecord preferences[editor::MaximumEditorUserStatePreferences]{};
        editor::EditorTypedPreferenceItemResult preferenceResults[editor::MaximumEditorUserStatePreferences]{};
        editor::EditorPanelStateResult panelStateResults[editor::detail::MaximumEditorUserStatePanelStateRecords]{};
        u8 panelStateMigrationBytes[editor::MaximumEditorPanelStateBytes]{};
        editor::EditorUserStatePrimaryWindowRecord primaryWindow;
        u32 panelCount = 0;
        u32 displayCount = 0;
        u32 imguiByteCount = 0;
        u32 shortcutCount = 0;
        u32 preferenceCount = 0;
        bool textState = false;
    };

    struct EditorUserStateCaptureScratch final
    {
        editor::detail::EditorPanelUserStateRecord privatePanels[editor::MaximumEditorUserStatePanels]{};
        editor::EditorUserStatePanelRecord panels[editor::MaximumEditorUserStatePanels]{};
        editor::detail::EditorUiSavedDisplay displays[editor::MaximumEditorUserStateDisplays]{};
        u8 imguiBytes[editor::MaximumEditorUserStateImguiBytes]{};
        editor::EditorShortcutPreferenceRecord liveShortcuts[editor::MaximumEditorUserStateShortcuts]{};
        editor::EditorUserStateShortcutRecord shortcuts[editor::MaximumEditorUserStateShortcuts]{};
        editor::EditorTypedPreferenceRecord livePreferences[editor::MaximumEditorUserStatePreferences]{};
        editor::EditorUserStatePreferenceRecord preferences[editor::MaximumEditorUserStatePreferences]{};
        editor::detail::EditorUserStateTextPanelStateRecord panelStates[editor::detail::MaximumEditorUserStatePanelStateRecords]{};
        bool panelStateUnsupportedProtected[editor::detail::MaximumEditorUserStatePanelStateRecords]{};
        u8 panelStateBytes[editor::detail::MaximumEditorUserStatePanelStateBytes]{};
        u8 panelStateCallbackBytes[editor::MaximumEditorPanelStateBytes]{};
        u32 panelCount = 0;
        u32 panelStateCount = 0;
        u32 panelStateByteCount = 0;
        u32 displayCount = 0;
        u32 imguiByteCount = 0;
        u32 liveShortcutCount = 0;
        u32 shortcutCount = 0;
        u32 livePreferenceCount = 0;
        u32 preferenceCount = 0;
    };

    enum class NamedLayoutOperation : u8
    {
        None,
        PromptSaveAs,
        SaveAs,
        PromptSave,
        Save,
        PromptSwitch,
        Switch,
        Revert,
        PromptRename,
        Rename,
        PromptDuplicate,
        Duplicate,
        PromptDelete,
        Delete,
        ResetDefault
    };

    enum class LayoutPresentationApplyMode : u8
    {
        SavedSnapshot,
        CanonicalDefault
    };

    enum class LayoutPresentationFailurePolicy : u8
    {
        StopOnFailure,
        BestEffort
    };

    struct NamedLayoutRequest final
    {
        NamedLayoutOperation operation = NamedLayoutOperation::None;
        u64 id = 0;
        char name[editor::detail::MaximumEditorNamedLayoutNameBytes + 1u]{};
        u32 nameByteCount = 0;
    };

    struct NamedLayoutValidationContext final
    {
        u64 expectedId = 0;
        editor::detail::EditorNamedLayoutTextDecodedStorage* decoded = nullptr;
    };

    [[nodiscard]] bool ValidateNamedLayoutArtifact(const void* const bytes, const usize byteCount,
                                                   void* const userData) noexcept
    {
        auto* const context = static_cast<NamedLayoutValidationContext*>(userData);
        if (context == nullptr || context->decoded == nullptr || context->expectedId == 0 ||
            (byteCount != 0 && bytes == nullptr) || byteCount > editor::detail::MaximumEditorUserStateTextBytes)
            return false;
        editor::detail::EditorUserStateTextFailure failure;
        return editor::detail::DecodeEditorNamedLayoutText(
                   {static_cast<const char*>(bytes), static_cast<u32>(byteCount)}, *context->decoded, &failure) ==
                   editor::detail::EditorUserStateTextResult::Success &&
               context->decoded->id == context->expectedId;
    }

    [[nodiscard]] bool ValidateNamedLayoutIndexArtifact(const void* const bytes, const usize byteCount,
                                                        void* const userData) noexcept
    {
        auto* const decoded = static_cast<editor::detail::EditorNamedLayoutIndexDecodedStorage*>(userData);
        if (decoded == nullptr || (byteCount != 0 && bytes == nullptr) ||
            byteCount > editor::detail::MaximumEditorUserStateTextBytes)
            return false;
        editor::detail::EditorUserStateTextFailure failure;
        return editor::detail::DecodeEditorNamedLayoutIndexText(
                   {static_cast<const char*>(bytes), static_cast<u32>(byteCount)}, *decoded, &failure) ==
               editor::detail::EditorUserStateTextResult::Success;
    }

    void FormatNamedLayoutFileName(const u64 id, char (&name)[25]) noexcept
    {
        constexpr char hex[] = "0123456789abcdef";
        for (u32 nibble = 0; nibble < 16u; ++nibble)
        {
            const u32 shift = (15u - nibble) * 4u;
            name[nibble] = hex[(id >> shift) & 0x0full];
        }
        std::memcpy(name + 16u, ".vlayout", 9u);
    }

    [[nodiscard]] bool ParseNamedLayoutArtifactFileName(const char* const name, u64& id) noexcept
    {
        id = 0;
        if (name == nullptr)
            return false;
        const usize nameLength = std::strlen(name);
        if (nameLength != 24u && nameLength != 28u)
            return false;
        for (u32 index = 0; index < 16u; ++index)
        {
            const char byte = name[index];
            u8 nibble = 0;
            if (byte >= '0' && byte <= '9')
                nibble = static_cast<u8>(byte - '0');
            else if (byte >= 'a' && byte <= 'f')
                nibble = static_cast<u8>(10 + byte - 'a');
            else
                return false;
            id = (id << 4u) | nibble;
        }
        if (id == 0 || std::memcmp(name + 16u, ".vlayout", 8u) != 0)
            return false;
        return nameLength == 24u || std::memcmp(name + 24u, ".lkg", 5u) == 0;
    }

    class ManagedEditorUserStateService final : public editor::EditorUserStateService
    {
    public:
        explicit ManagedEditorUserStateService(
            const editor::EditorUserStateServiceConfig* const config) noexcept
            : m_forceDefaultLayout(config != nullptr && config->forceDefaultLayout)
        {
        }

        [[nodiscard]] editor::EditorUserStateStatus GetStatus() const noexcept override
        {
            return m_status;
        }

    protected:
        app::LifecycleStatus OnInitialize(app::ServiceContext& context) noexcept override
        {
            Reset();
            if (!concurrency::IsMainThread())
                return LifecycleFailure(editor::EditorUserStateFailureCode::WrongThread, "editor user state initialization requires the main thread");

            m_framePipeline = static_cast<engine::FramePipelineService*>(context.FindCapability(engine::FramePipelineCapabilityId));
            m_window = engine::FindWindowService(context);
            m_workspace = static_cast<editor::ProjectWorkspaceService*>(context.FindCapability(editor::ProjectWorkspaceCapabilityId));
            m_uiPersistence = editor::detail::FindEditorUiPersistencePrivateService(context);
            m_commandPersistence = editor::detail::FindEditorCommandPersistencePrivateService(context);
            m_shellPersistence = editor::detail::FindEditorShellPersistencePrivateService(context);
            if (m_framePipeline == nullptr || m_window == nullptr || m_workspace == nullptr ||
                m_uiPersistence == nullptr || m_commandPersistence == nullptr ||
                m_shellPersistence == nullptr || context.Find(engine::FilesystemServiceId) == nullptr)
                return LifecycleFailure(editor::EditorUserStateFailureCode::WrongPhase, "editor user state dependencies are unavailable");

            m_image = memory::Allocate(memory::PoolId::Tools, editor::MaximumEditorUserStateFileBytes, 16);
            if (!m_image)
                return LifecycleFailure(editor::EditorUserStateFailureCode::AllocationFailed, "editor user state image allocation failed");
            m_validationImage =
                memory::Allocate(memory::PoolId::Tools, editor::MaximumEditorUserStateFileBytes, 16);
            if (!m_validationImage)
                return LifecycleFailure(editor::EditorUserStateFailureCode::AllocationFailed,
                                        "editor user state validation image allocation failed");
            m_textImage = memory::Allocate(memory::PoolId::Tools,
                                           editor::detail::MaximumEditorUserStateTextBytes, 16);
            if (!m_textImage)
                return LifecycleFailure(editor::EditorUserStateFailureCode::AllocationFailed,
                                        "editor user state text image allocation failed");
            m_textDecodedScratch =
                memory::Allocate(memory::PoolId::Tools,
                                 sizeof(editor::detail::EditorUserStateTextDecodedStorage),
                                 alignof(editor::detail::EditorUserStateTextDecodedStorage));
            if (!m_textDecodedScratch)
                return LifecycleFailure(editor::EditorUserStateFailureCode::AllocationFailed,
                                        "editor user state text decode scratch allocation failed");
            ::new (m_textDecodedScratch.address) editor::detail::EditorUserStateTextDecodedStorage();
            m_textValidationDecodedScratch =
                memory::Allocate(memory::PoolId::Tools,
                                 sizeof(editor::detail::EditorUserStateTextDecodedStorage),
                                 alignof(editor::detail::EditorUserStateTextDecodedStorage));
            if (!m_textValidationDecodedScratch)
                return LifecycleFailure(editor::EditorUserStateFailureCode::AllocationFailed,
                                        "editor user state text validation decode scratch allocation failed");
            ::new (m_textValidationDecodedScratch.address)
                editor::detail::EditorUserStateTextDecodedStorage();
            m_captureScratch =
                memory::Allocate(memory::PoolId::Tools, sizeof(EditorUserStateCaptureScratch),
                                 alignof(EditorUserStateCaptureScratch));
            if (!m_captureScratch)
                return LifecycleFailure(editor::EditorUserStateFailureCode::AllocationFailed,
                                        "editor user state capture scratch allocation failed");
            ::new (m_captureScratch.address) EditorUserStateCaptureScratch();
            m_defaultLayoutCaptureScratch =
                memory::Allocate(memory::PoolId::Tools, sizeof(EditorUserStateCaptureScratch),
                                 alignof(EditorUserStateCaptureScratch));
            if (!m_defaultLayoutCaptureScratch)
                return LifecycleFailure(editor::EditorUserStateFailureCode::AllocationFailed,
                                        "editor default layout capture scratch allocation failed");
            ::new (m_defaultLayoutCaptureScratch.address) EditorUserStateCaptureScratch();
            m_namedLayoutDecodedScratch =
                memory::Allocate(memory::PoolId::Tools,
                                 sizeof(editor::detail::EditorNamedLayoutTextDecodedStorage),
                                 alignof(editor::detail::EditorNamedLayoutTextDecodedStorage));
            if (!m_namedLayoutDecodedScratch)
                return LifecycleFailure(editor::EditorUserStateFailureCode::AllocationFailed,
                                        "editor named layout decode scratch allocation failed");
            ::new (m_namedLayoutDecodedScratch.address) editor::detail::EditorNamedLayoutTextDecodedStorage();
            m_namedLayoutValidationDecodedScratch =
                memory::Allocate(memory::PoolId::Tools,
                                 sizeof(editor::detail::EditorNamedLayoutTextDecodedStorage),
                                 alignof(editor::detail::EditorNamedLayoutTextDecodedStorage));
            if (!m_namedLayoutValidationDecodedScratch)
                return LifecycleFailure(editor::EditorUserStateFailureCode::AllocationFailed,
                                        "editor named layout validation scratch allocation failed");
            ::new (m_namedLayoutValidationDecodedScratch.address) editor::detail::EditorNamedLayoutTextDecodedStorage();

            static_assert(editor::detail::MaximumEditorNamedLayouts ==
                          editor::detail::MaximumEditorUiNamedLayouts);
            static_assert(editor::detail::MaximumEditorNamedLayoutNameBytes ==
                          editor::detail::MaximumEditorUiNamedLayoutNameBytes);

            constexpr engine::FrameParticipantId dependencies[]{editor::EditorUiRenderParticipantId,
                                                                 editor::EditorCommandShortcutFrameParticipantId};
            engine::FrameParticipantDescriptor descriptor;
            descriptor.id = editor::EditorUserStateFrameParticipantId;
            descriptor.name = "editorUserState.persist";
            descriptor.phase = engine::FramePhase::EndFrame;
            descriptor.profiles = app::ApplicationProfile::Editor | app::ApplicationProfile::Tool;
            descriptor.affinity = app::ThreadAffinity::MainThread;
            descriptor.after = {dependencies, 2};
            descriptor.execute = ExecuteFrame;
            descriptor.userData = this;
            engine::FrameFailure frameFailure;
            if (!m_framePipeline->RegisterParticipant(descriptor, &frameFailure))
                return LifecycleFailure(editor::EditorUserStateFailureCode::WrongPhase,
                                        frameFailure.message != nullptr ? frameFailure.message : "editor user state frame registration failed");

            m_status.state = editor::EditorUserStateServiceState::Ready;
            return app::LifecycleStatus::Success();
        }

        app::LifecycleStatus OnStart(app::ServiceContext&) noexcept override
        {
            const editor::EditorUserStateFailureCode lifecycleGuard =
                editor::detail::ValidateEditorUserStateLifecycleEntry(
                    concurrency::IsMainThread(), m_status.state,
                    editor::EditorUserStateServiceState::Ready,
                    m_workspace != nullptr && m_image && m_validationImage && m_textImage &&
                        m_textDecodedScratch && m_textValidationDecodedScratch && m_captureScratch &&
                        m_defaultLayoutCaptureScratch && m_namedLayoutDecodedScratch &&
                        m_namedLayoutValidationDecodedScratch);
            if (lifecycleGuard != editor::EditorUserStateFailureCode::None)
                return LifecycleFailure(
                    lifecycleGuard,
                    lifecycleGuard == editor::EditorUserStateFailureCode::WrongThread
                        ? "editor user state start requires the main thread"
                        : "editor user state start requires initialized state");

            if (!crypto::SecureRandom(m_artifactStageIdentity.nonce,
                                      sizeof(m_artifactStageIdentity.nonce)))
                m_publicationAvailable = false;
            else
            {
                m_publicationAvailable = true;
                m_artifactStageIdentity.serial = 1;
            }
            if (!InitializeSessionStoragePaths())
                return LifecycleFailure(editor::EditorUserStateFailureCode::InvalidArgument,
                                        "editor user-state session paths are invalid");

            ResetDecodedTextState();
            {
                NamedLayoutIndexLease layoutLease;
                const editor::detail::EditorStateArtifactLoadResult layoutIndexLoad =
                    LoadNamedLayoutIndex();
                if (layoutLease.Acquired() && m_publicationAvailable && filesystem::IsInitialized())
                {
                    filesystem::Manager& files = filesystem::GetManager();
                    if (files.CreatePath(m_layoutDirectory))
                    {
                        editor::detail::CleanupStaleEditorStateStages(
                            m_layoutDirectory, m_artifactStageIdentity);
                        // A recovered LKG is not proof that the unreadable canonical generation
                        // references no additional payloads. Only destructively collect when the
                        // canonical index itself validated, or when both index generations are
                        // proven absent.
                        if (layoutIndexLoad == editor::detail::EditorStateArtifactLoadResult::LoadedCanonical ||
                            layoutIndexLoad == editor::detail::EditorStateArtifactLoadResult::Missing)
                            GarbageCollectNamedLayoutArtifacts();
                    }
                }
            }
            m_activeLayoutId = FindNamedLayoutIndexRecord(m_namedLayoutIndex.activeId) != nullptr
                ? m_namedLayoutIndex.activeId
                : 0;
            editor::EditorUserStateDocumentView defaultDocument;
            editor::EditorUserStateFailure defaultCaptureFailure;
            if (!CaptureCurrentDocument(
                    *static_cast<EditorUserStateCaptureScratch*>(m_defaultLayoutCaptureScratch.address),
                    defaultDocument, defaultCaptureFailure))
                return LifecycleFailure(editor::EditorUserStateFailureCode::InvalidArgument,
                                        "editor default presentation capture failed");
            m_defaultLayoutCaptured = true;
            m_defaultPrimaryWindow = defaultDocument.primaryWindow;
            if (!RegisterNamedLayoutCommands())
                return LifecycleFailure(editor::EditorUserStateFailureCode::WrongPhase,
                                        "editor named-layout command registration failed");

            if (m_publicationAvailable && filesystem::IsInitialized())
            {
                // Maintenance is deliberately best-effort: abandoned stages from a crash must
                // never turn a usable prior session into a startup or save failure.
                filesystem::Manager& files = filesystem::GetManager();
                if (files.CreatePath(m_projectSessionDirectory))
                    editor::detail::CleanupStaleEditorStateStages(
                        m_projectSessionDirectory, m_artifactStageIdentity);
                if (files.CreatePath(m_directory))
                    editor::detail::CleanupStaleEditorStateStages(
                        m_directory, m_artifactStageIdentity);
            }

            ResetDecodedTextState();
            m_loadedByteCount = 0;
            m_status.writesInhibited = !m_publicationAvailable;
            if (!m_publicationAvailable)
                m_status.failure.code = editor::EditorUserStateFailureCode::RandomUnavailable;

            SessionReference latestReference;
            SessionReference lastReference;
            SessionCandidateLoadResult latest = ReadSessionReference(
                m_latestCheckpointRefPath, latestReference);
            SessionCandidateLoadResult last = ReadSessionReference(
                m_lastSessionRefPath, lastReference);
            bool restoreReady = false;
            bool selectionStoppedByOwnerInvariant = false;
            bool restoreAllocationFailed = false;
            const auto tryReference = [&](const SessionReference& reference,
                                          SessionCandidateLoadResult& result) noexcept {
                if (result != SessionCandidateLoadResult::Loaded)
                    return;
                result = TryLoadTextSessionReference(reference);
                if (result != SessionCandidateLoadResult::Loaded)
                    return;
                const SessionRestorePrepareResult prepared = PrepareLoadedRestore(true);
                if (prepared == SessionRestorePrepareResult::Prepared)
                    restoreReady = true;
                else if (prepared == SessionRestorePrepareResult::AllocationFailed)
                    restoreAllocationFailed = true;
                else if (prepared == SessionRestorePrepareResult::OwnerInvariantFailed)
                    selectionStoppedByOwnerInvariant = true;
                else
                {
                    result = SessionCandidateLoadResult::Corrupt;
                    m_checkpointSequence = 0;
                    ResetDecodedTextState();
                }
            };

            // A clean-close ref wins only when it explicitly names the exact latest ref target it
            // observed and causally superseded. If another process checkpoints afterward, current
            // latest no longer matches and wins. No wall clock or process-global sequence is used.
            const bool lastFirst =
                last == SessionCandidateLoadResult::Loaded &&
                (latest != SessionCandidateLoadResult::Loaded ||
                 CleanReferenceSupersedesLatest(lastReference, latestReference));
            if (lastFirst)
                tryReference(lastReference, last);
            else
                tryReference(latestReference, latest);
            if (!restoreReady && !selectionStoppedByOwnerInvariant && !restoreAllocationFailed)
            {
                if (lastFirst)
                    tryReference(latestReference, latest);
                else
                    tryReference(lastReference, last);
            }
            if (restoreAllocationFailed)
                return LifecycleFailure(editor::EditorUserStateFailureCode::AllocationFailed,
                                        "editor user state restore scratch allocation failed");

            if (!restoreReady && !selectionStoppedByOwnerInvariant)
            {
                // The old project-local VUSR is a read-only migration input. A valid legacy
                // image is restored once and the next checkpoint publishes the user-local text
                // session without deleting or rewriting the legacy file.
                const filesystem::AbsolutePath legacyDirectory =
                    m_workspace->GetSavedRoot().AddDirPath("Editor");
                const filesystem::AbsolutePath legacyCanonical =
                    legacyDirectory.AddFilePath("user-state.vstate");
                editor::EditorUserStateEnvelopeInfo legacyInfo;
                editor::EditorUserStateFailure legacyFailure;
                const editor::EditorUserStateLoadResult legacyLoad =
                    editor::LoadEditorUserStateImage(
                        ProductionUserStateFileOps, legacyCanonical,
                        m_workspace->GetProject().id, m_image.address, m_image.size,
                        m_loadedByteCount, legacyInfo, &legacyFailure);
                if (legacyLoad == editor::EditorUserStateLoadResult::Restored ||
                    legacyLoad == editor::EditorUserStateLoadResult::Migrated)
                {
                    m_status.load = editor::EditorUserStateLoadResult::Migrated;
                    m_status.loadedVersion = legacyInfo.version;
                    m_status.panelRecords = legacyInfo.panelRecords;
                    m_status.displayRecords = legacyInfo.displayRecords;
                    m_status.shortcutRecords = legacyInfo.shortcutRecords;
                    m_status.preferenceRecords = legacyInfo.preferenceRecords;
                    m_checkpointSequence = 0;
                    ResetDecodedTextState();
                    const SessionRestorePrepareResult prepared = PrepareLoadedRestore(false);
                    if (prepared == SessionRestorePrepareResult::Prepared)
                        restoreReady = true;
                    else if (prepared == SessionRestorePrepareResult::AllocationFailed)
                        return LifecycleFailure(editor::EditorUserStateFailureCode::AllocationFailed,
                                                "editor user state restore scratch allocation failed");
                    else if (prepared == SessionRestorePrepareResult::OwnerInvariantFailed)
                        selectionStoppedByOwnerInvariant = true;
                    else
                    {
                        m_loadedByteCount = 0;
                        m_status.load = editor::EditorUserStateLoadResult::RejectedCorrupt;
                        m_status.failure.code = editor::EditorUserStateFailureCode::CorruptDocument;
                    }
                }
                else if (legacyLoad == editor::EditorUserStateLoadResult::Missing)
                {
                    m_loadedByteCount = 0;
                    if (latest == SessionCandidateLoadResult::IoFailure ||
                        last == SessionCandidateLoadResult::IoFailure)
                    {
                        m_status.load = editor::EditorUserStateLoadResult::IoFailure;
                        m_status.failure.code = editor::EditorUserStateFailureCode::CanonicalIo;
                    }
                    else if (latest == SessionCandidateLoadResult::Corrupt ||
                             last == SessionCandidateLoadResult::Corrupt)
                    {
                        m_status.load = editor::EditorUserStateLoadResult::RejectedCorrupt;
                        m_status.failure.code = editor::EditorUserStateFailureCode::CorruptDocument;
                    }
                    else
                    {
                        m_status.load = editor::EditorUserStateLoadResult::Missing;
                        m_status.failure = {};
                    }
                    ResetDecodedTextState();
                }
                else
                {
                    m_loadedByteCount = 0;
                    m_status.load = legacyLoad;
                    m_status.failure = legacyFailure;
                    ResetDecodedTextState();
                }
            }

            if (restoreReady)
            {
                auto& restore = *static_cast<EditorUserStateRestoreScratch*>(m_restoreScratch.address);
                editor::detail::EditorUserStateRestoreOwnerStep failedOwnerStep;
                const editor::detail::EditorUserStateRestoreTransactionResult transaction =
                    editor::detail::ExecuteEditorUserStateRestoreTransaction(
                        []() noexcept { return true; },
                        [&](const editor::detail::EditorUserStateRestoreOwnerStep step) noexcept {
                            return ApplyPreparedRestoreStep(restore, step);
                        },
                        failedOwnerStep);
                if (transaction == editor::detail::EditorUserStateRestoreTransactionResult::OwnerApplyFailed)
                {
                    // State application is recovery input, not an editor-start dependency. Keep
                    // the editor usable and visible; conservative write inhibition prevents a live
                    // owner failure from overwriting the source until the owner is healthy again.
                    m_status.failure.detail = static_cast<u32>(failedOwnerStep);
                    m_status.failure.code = editor::EditorUserStateFailureCode::InvalidArgument;
                    m_status.writesInhibited = true;
                }
            }
            if (!RevealPrimaryWindow())
                return LifecycleFailure(editor::EditorUserStateFailureCode::WrongPhase,
                                        "editor user state could not reveal the primary window");
            // Visibility is also a synchronous backend state query. Baseline only after reveal so
            // maximize/fullscreen actions requested while hidden are reflected before dirty tracking.
            static_cast<void>(InitializeSourceBaselines());
            if (m_forceDefaultLayout)
            {
                // Force-default is a process-local recovery choice. Preserve the durable last-active
                // named-layout hint for the next ordinary launch, but do not claim that named layout
                // is active while this process is showing the canonical default presentation.
                m_activeLayoutId = 0;
                m_activeLayoutModified = false;
            }
            static_cast<void>(RefreshActiveLayoutModified());
            m_admissionOpen = true;
            m_status.state = editor::EditorUserStateServiceState::Running;
            return app::LifecycleStatus::Success();
        }

        app::LifecycleStatus OnQuiesce(app::ServiceContext&) noexcept override
        {
            const editor::EditorUserStateFailureCode lifecycleGuard =
                editor::detail::ValidateEditorUserStateLifecycleEntry(
                    concurrency::IsMainThread(), m_status.state,
                    editor::EditorUserStateServiceState::Running);
            if (lifecycleGuard != editor::EditorUserStateFailureCode::None)
                return LifecycleFailure(
                    lifecycleGuard,
                    lifecycleGuard == editor::EditorUserStateFailureCode::WrongThread
                        ? "editor user state quiesce requires the main thread"
                        : "editor user state quiesce requires running state");
            m_admissionOpen = false;
            m_lastPayloadPublishSucceeded = false;
            if (m_defaultLayoutRebuildPending)
            {
                // Reset Default clears the live ImGui dock tree at EndFrame and relies on
                // the following UI frame to rebuild/re-place the default presentation. A primary
                // close can transition the application to Exiting before that frame is authored.
                // Never replace the last durable checkpoint with that deliberately transient,
                // cleared layout; abandoning this pending reset on teardown is safer than making a
                // structurally incomplete presentation the next startup state.
                m_defaultLayoutRebuildPending = false;
                // Command mutation is only admitted while the command service
                // is Running. All services drain before OnStop, so release
                // our named-layout handlers during Quiesce, even on this
                // intentionally skipped final-layout-persistence branch.
                if (!UnregisterNamedLayoutCommands())
                    return LifecycleFailure(editor::EditorUserStateFailureCode::CleanupFailed,
                                            "editor user state named-layout command cleanup failed");
                m_status.state = editor::EditorUserStateServiceState::Quiesced;
                return app::LifecycleStatus::Success();
            }
            const editor::detail::EditorUserStateFinalPersistenceResult finalPersistence =
                editor::detail::ExecuteEditorUserStateFinalPersistence(
                    m_status.writesInhibited,
                    [&]() noexcept {
                        return ObserveDirtySources(m_dirtyTracker.lastDirtyRealTimeSeconds);
                    },
                    [&]() noexcept { return PublishCurrentState(); });
            if (finalPersistence == editor::detail::EditorUserStateFinalPersistenceResult::WriteInhibited)
                m_status.save = editor::EditorUserStateSaveResult::WriteInhibited;
            else if (finalPersistence == editor::detail::EditorUserStateFinalPersistenceResult::PublishFailed)
                ++m_status.finalSaveFailures;
            if (m_lastPayloadPublishSucceeded)
            {
                SessionReference observedLatest;
                const SessionCandidateLoadResult observedLatestResult =
                    ReadSessionReference(m_latestCheckpointRefPath, observedLatest);
                // Without a stable structural read we cannot safely claim a causal clean-close
                // relationship to the shared latest ref; leave the older last-session ref intact.
                if (observedLatestResult == SessionCandidateLoadResult::IoFailure ||
                    !PublishSessionReference(
                        m_lastSessionRefPath,
                        observedLatestResult == SessionCandidateLoadResult::Loaded ? &observedLatest : nullptr))
                {
                    ++m_status.finalSaveFailures;
                    m_status.save = editor::EditorUserStateSaveResult::ReplaceFailed;
                    m_status.failure.code = editor::EditorUserStateFailureCode::ReplaceFailed;
                }
            }
            // Final persistence has captured command shortcut/layout state.
            // Release command callbacks while Command is still Running;
            // deferring this to Stop violates its earlier Drain invariant.
            if (!UnregisterNamedLayoutCommands())
                return LifecycleFailure(editor::EditorUserStateFailureCode::CleanupFailed,
                                        "editor user state named-layout command cleanup failed");
            m_status.state = editor::EditorUserStateServiceState::Quiesced;
            return app::LifecycleStatus::Success();
        }

        app::LifecycleStatus OnDrain(app::ServiceContext&) noexcept override
        {
            if (!concurrency::IsMainThread())
                return LifecycleFailure(editor::EditorUserStateFailureCode::WrongThread, "editor user state drain requires the main thread");
            return m_status.state == editor::EditorUserStateServiceState::Quiesced
                ? app::LifecycleStatus::Success()
                : LifecycleFailure(editor::EditorUserStateFailureCode::WrongPhase, "editor user state drain requires quiesced state");
        }

        app::LifecycleStatus OnStop(app::ServiceContext&) noexcept override
        {
            if (!concurrency::IsMainThread())
                return LifecycleFailure(editor::EditorUserStateFailureCode::WrongThread, "editor user state stop requires the main thread");
            if (!UnregisterNamedLayoutCommands())
                return LifecycleFailure(editor::EditorUserStateFailureCode::CleanupFailed,
                                        "editor user state named-layout command cleanup survived quiesce");
            m_status.state = editor::EditorUserStateServiceState::Stopped;
            return app::LifecycleStatus::Success();
        }

        app::LifecycleStatus OnShutdown(app::ServiceContext&) noexcept override
        {
            m_admissionOpen = false;
            m_loadedByteCount = 0;
            m_projectSessionDirectory = {};
            m_directory = {};
            m_canonicalPath = {};
            m_lkgPath = {};
            m_latestCheckpointRefPath = {};
            m_lastSessionRefPath = {};
            m_layoutDirectory = {};
            m_layoutIndexPath = {};
            m_layoutIndexLkgPath = {};
            m_workspace = nullptr;
            m_framePipeline = nullptr;
            m_uiPersistence = nullptr;
            m_commandPersistence = nullptr;
            m_shellPersistence = nullptr;
            if (m_restoreScratch)
            {
                static_cast<EditorUserStateRestoreScratch*>(m_restoreScratch.address)->~EditorUserStateRestoreScratch();
                memory::Free(m_restoreScratch);
                m_restoreScratch = {};
            }
            m_restorePrepared = false;
            if (m_image)
                memory::Free(m_image);
            m_image = {};
            if (m_validationImage)
                memory::Free(m_validationImage);
            m_validationImage = {};
            if (m_textImage)
                memory::Free(m_textImage);
            m_textImage = {};
            if (m_textDecodedScratch)
            {
                static_cast<editor::detail::EditorUserStateTextDecodedStorage*>(
                    m_textDecodedScratch.address)->~EditorUserStateTextDecodedStorage();
                memory::Free(m_textDecodedScratch);
            }
            m_textDecodedScratch = {};
            if (m_textValidationDecodedScratch)
            {
                static_cast<editor::detail::EditorUserStateTextDecodedStorage*>(
                    m_textValidationDecodedScratch.address)->~EditorUserStateTextDecodedStorage();
                memory::Free(m_textValidationDecodedScratch);
            }
            m_textValidationDecodedScratch = {};
            if (m_captureScratch)
            {
                static_cast<EditorUserStateCaptureScratch*>(m_captureScratch.address)->
                    ~EditorUserStateCaptureScratch();
                memory::Free(m_captureScratch);
            }
            m_captureScratch = {};
            if (m_defaultLayoutCaptureScratch)
            {
                static_cast<EditorUserStateCaptureScratch*>(m_defaultLayoutCaptureScratch.address)->
                    ~EditorUserStateCaptureScratch();
                memory::Free(m_defaultLayoutCaptureScratch);
            }
            m_defaultLayoutCaptureScratch = {};
            if (m_namedLayoutDecodedScratch)
            {
                static_cast<editor::detail::EditorNamedLayoutTextDecodedStorage*>(
                    m_namedLayoutDecodedScratch.address)->~EditorNamedLayoutTextDecodedStorage();
                memory::Free(m_namedLayoutDecodedScratch);
            }
            m_namedLayoutDecodedScratch = {};
            if (m_namedLayoutValidationDecodedScratch)
            {
                static_cast<editor::detail::EditorNamedLayoutTextDecodedStorage*>(
                    m_namedLayoutValidationDecodedScratch.address)->~EditorNamedLayoutTextDecodedStorage();
                memory::Free(m_namedLayoutValidationDecodedScratch);
            }
            m_namedLayoutValidationDecodedScratch = {};
            m_textOutput.Clear();
            m_publicationAvailable = false;
            m_artifactStageIdentity = {};
            m_checkpointSequence = 0;
            m_lastPayloadPublishSucceeded = false;
            m_latestReferencePending = false;
            m_pendingPayloadGeneration = 0;
            std::memset(m_instanceFileName, 0, sizeof(m_instanceFileName));
            m_recoveredFromLkg = false;
            m_namedLayoutIndex = {};
            m_namedLayoutIndexValidation = {};
            m_namedLayoutPending = {};
            m_namedLayoutDialogOutstanding = false;
            m_activeLayoutModified = false;
            m_defaultLayoutCaptured = false;
            m_layoutBackupPrimaryWindow = {};
            m_status.state = editor::EditorUserStateServiceState::Uninitialized;
            return app::LifecycleStatus::Success();
        }

    private:
        void ResetDecodedTextState() noexcept
        {
            if (!m_textDecodedScratch || m_workspace == nullptr)
                return;
            auto* const decoded =
                static_cast<editor::detail::EditorUserStateTextDecodedStorage*>(
                    m_textDecodedScratch.address);
            std::memset(decoded, 0, sizeof(*decoded));
            decoded->version = editor::detail::EditorUserStateTextVersion;
            decoded->project = m_workspace->GetProject().id;
        }

        [[nodiscard]] bool InitializeSessionStoragePaths() noexcept
        {
            if (m_workspace == nullptr)
                return false;
            char projectId[projects::ProjectIdTextBufferSize]{};
            if (!projects::FormatProjectId(m_workspace->GetProject().id, projectId,
                                           projects::ProjectIdTextBufferSize))
                return false;

            const filesystem::AbsolutePath& userRoot =
                filesystem::paths::GetUserEditorDirectory();
            if (userRoot.Empty())
                return false;
            m_projectSessionDirectory =
                userRoot.AddDirPath("Vanguard")
                    .AddDirPath("Sessions")
                    .AddDirPath("Projects")
                    .AddDirPath(projectId);
            m_layoutDirectory = userRoot.AddDirPath("Vanguard").AddDirPath("Layouts");
            m_layoutIndexPath = m_layoutDirectory.AddFilePath("index.vlayout-index");
            m_layoutIndexLkgPath = m_layoutDirectory.AddFilePath("index.vlayout-index.lkg");
            m_directory = m_projectSessionDirectory.AddDirPath("Instances");
            m_latestCheckpointRefPath =
                m_projectSessionDirectory.AddFilePath("latest-checkpoint.ref");
            m_lastSessionRefPath =
                m_projectSessionDirectory.AddFilePath("last-session.ref");

            FormatSessionInstanceName(m_artifactStageIdentity.nonce, m_instanceFileName);
            m_canonicalPath = m_directory.AddFilePath(m_instanceFileName);
            char lkgName[SessionInstanceNameBytes + 5u]{};
            std::memcpy(lkgName, m_instanceFileName, SessionInstanceNameBytes);
            std::memcpy(lkgName + SessionInstanceNameBytes, ".lkg", 5u);
            m_lkgPath = m_directory.AddFilePath(lkgName);
            return !m_projectSessionDirectory.Empty() && !m_directory.Empty() &&
                   !m_layoutDirectory.Empty() && !m_layoutIndexPath.Empty() && !m_layoutIndexLkgPath.Empty() &&
                   !m_latestCheckpointRefPath.Empty() && !m_lastSessionRefPath.Empty() &&
                   !m_canonicalPath.Empty() && !m_lkgPath.Empty();
        }

        [[nodiscard]] bool BuildNamedLayoutArtifactPaths(
            const u64 id, editor::detail::EditorStateArtifactPaths& paths) const noexcept
        {
            paths = {};
            if (id == 0 || m_layoutDirectory.Empty())
                return false;
            char name[25]{};
            FormatNamedLayoutFileName(id, name);
            char lkgName[29]{};
            std::memcpy(lkgName, name, 24u);
            std::memcpy(lkgName + 24u, ".lkg", 5u);
            paths.directory = m_layoutDirectory;
            paths.canonical = m_layoutDirectory.AddFilePath(name);
            paths.lastKnownGood = m_layoutDirectory.AddFilePath(lkgName);
            return !paths.canonical.Empty() && !paths.lastKnownGood.Empty();
        }

        [[nodiscard]] u32 PruneMissingNamedLayoutPayloadRecords() noexcept
        {
            if (!filesystem::IsInitialized() || m_layoutDirectory.Empty())
                return 0;

            filesystem::Manager& files = filesystem::GetManager();
            u32 writeIndex = 0;
            u32 removed = 0;
            for (u32 readIndex = 0; readIndex < m_namedLayoutIndex.layoutCount; ++readIndex)
            {
                const auto& record = m_namedLayoutIndex.layouts[readIndex];
                editor::detail::EditorStateArtifactPaths paths;
                const bool havePaths = BuildNamedLayoutArtifactPaths(record.id, paths);
                const bool payloadPresent = havePaths &&
                    (files.FileExist(paths.canonical) || files.FileExist(paths.lastKnownGood));
                if (!payloadPresent)
                {
                    if (m_namedLayoutIndex.activeId == record.id)
                        m_namedLayoutIndex.activeId = 0;
                    ++removed;
                    continue;
                }
                if (writeIndex != readIndex)
                    m_namedLayoutIndex.layouts[writeIndex] = record;
                ++writeIndex;
            }
            for (u32 index = writeIndex; index < m_namedLayoutIndex.layoutCount; ++index)
                m_namedLayoutIndex.layouts[index] = {};
            m_namedLayoutIndex.layoutCount = writeIndex;
            return removed;
        }

        [[nodiscard]] editor::detail::EditorStateArtifactLoadResult LoadNamedLayoutIndex() noexcept
        {
            m_namedLayoutIndex = {};
            m_namedLayoutIndex.version = editor::detail::EditorNamedLayoutIndexTextVersion;
            m_namedLayoutIndexValidation = {};
            if (!m_textImage || m_layoutDirectory.Empty() || m_layoutIndexPath.Empty() ||
                m_layoutIndexLkgPath.Empty())
                return editor::detail::EditorStateArtifactLoadResult::InvalidArgument;
            const editor::detail::EditorStateArtifactValidator validator{
                ValidateNamedLayoutIndexArtifact, &m_namedLayoutIndexValidation};
            usize byteCount = 0;
            const editor::detail::EditorStateArtifactLoadResult loaded =
                editor::detail::LoadEditorStateArtifact(
                    {m_layoutDirectory, m_layoutIndexPath, m_layoutIndexLkgPath},
                    m_textImage.address, m_textImage.size, byteCount, validator);
            if (loaded == editor::detail::EditorStateArtifactLoadResult::LoadedCanonical ||
                loaded == editor::detail::EditorStateArtifactLoadResult::RecoveredLkg)
            {
                m_namedLayoutIndex = m_namedLayoutIndexValidation;
                const u32 missingPayloads = PruneMissingNamedLayoutPayloadRecords();
                if (missingPayloads != 0)
                    VG_LOG_WARNING(diagnostics::Category::Engine,
                                   "ignored %u named-layout index record(s) whose payload and LKG are both missing",
                                   missingPayloads);
                return loaded;
            }
            if (loaded != editor::detail::EditorStateArtifactLoadResult::Missing)
                VG_LOG_WARNING(diagnostics::Category::Engine,
                               "named-layout index is unavailable or corrupt; session persistence remains enabled");
            return loaded;
        }

        [[nodiscard]] bool PublishNamedLayoutIndex(
            const editor::detail::EditorNamedLayoutIndexDecodedStorage& candidate) noexcept
        {
            if (!m_publicationAvailable || !m_textImage || m_artifactStageIdentity.serial == 0)
                return false;
            m_textOutput.Clear();
            editor::detail::EditorUserStateTextFailure textFailure;
            if (editor::detail::EncodeEditorNamedLayoutIndexText(
                    candidate.View(), m_textOutput, &textFailure) !=
                editor::detail::EditorUserStateTextResult::Success)
                return false;
            m_namedLayoutIndexValidation = {};
            const editor::detail::EditorStateArtifactValidator validator{
                ValidateNamedLayoutIndexArtifact, &m_namedLayoutIndexValidation};
            const editor::detail::EditorStateArtifactPublishResult published =
                editor::detail::PublishEditorStateArtifact(
                    {m_layoutDirectory, m_layoutIndexPath, m_layoutIndexLkgPath},
                    m_textOutput.AsChar(), m_textOutput.Length(), m_textImage.address,
                    m_textImage.size, m_artifactStageIdentity, validator);
            if (published != editor::detail::EditorStateArtifactPublishResult::Published)
            {
                VG_LOG_WARNING(diagnostics::Category::Engine,
                               "named-layout index publication failed (%u)",
                               static_cast<u32>(published));
                return false;
            }
            // The durable canonical is the encoded candidate above. Keep runtime command/query and
            // GC reachability on that same generation rather than depending on validator scratch,
            // which is also used while the artifact store inspects the previous canonical for LKG
            // rotation.
            m_namedLayoutIndex = candidate;
            GarbageCollectNamedLayoutArtifacts();
            return true;
        }

        [[nodiscard]] bool NamedLayoutIndexContains(
            const editor::detail::EditorNamedLayoutIndexDecodedStorage& index, const u64 id) const noexcept
        {
            for (u32 record = 0; record < index.layoutCount; ++record)
                if (index.layouts[record].id == id)
                    return true;
            return false;
        }

        void GarbageCollectNamedLayoutArtifacts() noexcept
        {
            if (!filesystem::IsInitialized() || !m_textImage || m_layoutDirectory.Empty() ||
                m_layoutIndexLkgPath.Empty())
                return;

            editor::detail::EditorNamedLayoutIndexDecodedStorage lkgIndex;
            bool lkgUsable = false;
            usize lkgByteCount = 0;
            switch (filesystem::ReadCompleteFile(
                m_layoutIndexLkgPath, m_textImage.address, m_textImage.size, lkgByteCount))
            {
            case filesystem::CompleteFileReadResult::Missing:
                lkgUsable = true;
                break;
            case filesystem::CompleteFileReadResult::Success:
            {
                editor::detail::EditorUserStateTextFailure failure;
                lkgUsable = editor::detail::DecodeEditorNamedLayoutIndexText(
                                {static_cast<const char*>(m_textImage.address),
                                 static_cast<u32>(lkgByteCount)},
                                lkgIndex, &failure) ==
                            editor::detail::EditorUserStateTextResult::Success;
                break;
            }
            default:
                break;
            }
            if (!lkgUsable)
                return;

            filesystem::Manager& files = filesystem::GetManager();
            constexpr u32 MaximumDeletesPerPass = 16u;
            u64 retiredIds[MaximumDeletesPerPass]{};
            u32 deleted = 0;
            const auto processCandidates = [&](const auto& candidates) noexcept {
                for (u32 index = 0; index < candidates.Size() && deleted < MaximumDeletesPerPass; ++index)
                {
                    u64 id = 0;
                    if (!ParseNamedLayoutArtifactFileName(candidates[index].AsChar(), id) ||
                        NamedLayoutIndexContains(m_namedLayoutIndex, id) ||
                        NamedLayoutIndexContains(lkgIndex, id))
                        continue;
                    bool alreadyRetired = false;
                    for (u32 previous = 0; previous < deleted; ++previous)
                        alreadyRetired = alreadyRetired || retiredIds[previous] == id;
                    if (alreadyRetired)
                        continue;
                    DeleteNamedLayoutArtifacts(id);
                    retiredIds[deleted++] = id;
                }
            };
            const auto canonicalCandidates = files.FindFiles(m_layoutDirectory.AddFilePath("*.vlayout"));
            processCandidates(canonicalCandidates);
            if (deleted < MaximumDeletesPerPass)
            {
                const auto lkgCandidates = files.FindFiles(m_layoutDirectory.AddFilePath("*.vlayout.lkg"));
                processCandidates(lkgCandidates);
            }
        }

        [[nodiscard]] editor::detail::EditorNamedLayoutIndexRecord* FindNamedLayoutIndexRecord(
            const u64 id) noexcept
        {
            for (u32 index = 0; index < m_namedLayoutIndex.layoutCount; ++index)
                if (m_namedLayoutIndex.layouts[index].id == id)
                    return &m_namedLayoutIndex.layouts[index];
            return nullptr;
        }

        [[nodiscard]] const editor::detail::EditorNamedLayoutIndexRecord* FindNamedLayoutIndexRecord(
            const u64 id) const noexcept
        {
            for (u32 index = 0; index < m_namedLayoutIndex.layoutCount; ++index)
                if (m_namedLayoutIndex.layouts[index].id == id)
                    return &m_namedLayoutIndex.layouts[index];
            return nullptr;
        }

        [[nodiscard]] bool NamedLayoutNameAvailable(const containers::StringView name,
                                                    const u64 exceptId = 0) const noexcept
        {
            if (name.Empty() || name.Length() > editor::detail::MaximumEditorNamedLayoutNameBytes ||
                !serialization::IsValidUtf8(name))
                return false;
            bool visible = false;
            for (const char value : name)
            {
                const u8 byte = static_cast<u8>(value);
                if (byte < 0x20u || byte == 0x7fu || value == '<' || value == '>' || value == ':' ||
                    value == '"' || value == '/' || value == '\\' || value == '|' || value == '?' || value == '*')
                    return false;
                visible = visible || value != ' ';
            }
            if (!visible)
                return false;
            for (u32 index = 0; index < m_namedLayoutIndex.layoutCount; ++index)
            {
                const auto& record = m_namedLayoutIndex.layouts[index];
                if (record.id == exceptId)
                    continue;
                if (containers::StringView(record.name, record.nameByteCount) == name)
                    return false;
            }
            return true;
        }

        [[nodiscard]] u64 GenerateNamedLayoutId() const noexcept
        {
            for (u32 attempt = 0; attempt < 16u; ++attempt)
            {
                u64 id = 0;
                if (!crypto::SecureRandom(&id, sizeof(id)))
                    return 0;
                if (id != 0 && FindNamedLayoutIndexRecord(id) == nullptr)
                    return id;
            }
            return 0;
        }

        [[nodiscard]] bool LoadNamedLayout(
            const u64 id, editor::detail::EditorNamedLayoutDocumentView& view,
            editor::detail::EditorStateArtifactLoadResult* const loadResult = nullptr) noexcept
        {
            view = {};
            if (loadResult != nullptr)
                *loadResult = editor::detail::EditorStateArtifactLoadResult::InvalidArgument;
            if (id == 0 || !m_textImage || !m_namedLayoutDecodedScratch)
                return false;
            editor::detail::EditorStateArtifactPaths paths;
            if (!BuildNamedLayoutArtifactPaths(id, paths))
                return false;
            auto* const decoded = static_cast<editor::detail::EditorNamedLayoutTextDecodedStorage*>(
                m_namedLayoutDecodedScratch.address);
            NamedLayoutValidationContext validation{id, decoded};
            const editor::detail::EditorStateArtifactValidator validator{
                ValidateNamedLayoutArtifact, &validation};
            usize byteCount = 0;
            const editor::detail::EditorStateArtifactLoadResult loaded =
                editor::detail::LoadEditorStateArtifact(
                    paths, m_textImage.address, m_textImage.size, byteCount, validator);
            if (loadResult != nullptr)
                *loadResult = loaded;
            if (loaded != editor::detail::EditorStateArtifactLoadResult::LoadedCanonical &&
                loaded != editor::detail::EditorStateArtifactLoadResult::RecoveredLkg)
                return false;
            view = decoded->View();
            return view.id == id;
        }

        [[nodiscard]] bool PublishNamedLayout(const editor::detail::EditorNamedLayoutDocumentView& view) noexcept
        {
            if (view.id == 0 || !m_publicationAvailable || !m_textImage ||
                !m_namedLayoutValidationDecodedScratch || m_artifactStageIdentity.serial == 0)
                return false;
            editor::detail::EditorStateArtifactPaths paths;
            if (!BuildNamedLayoutArtifactPaths(view.id, paths))
                return false;
            m_textOutput.Clear();
            editor::detail::EditorUserStateTextFailure textFailure;
            if (editor::detail::EncodeEditorNamedLayoutText(view, m_textOutput, &textFailure) !=
                editor::detail::EditorUserStateTextResult::Success)
                return false;
            auto* const decoded = static_cast<editor::detail::EditorNamedLayoutTextDecodedStorage*>(
                m_namedLayoutValidationDecodedScratch.address);
            NamedLayoutValidationContext validation{view.id, decoded};
            const editor::detail::EditorStateArtifactValidator validator{
                ValidateNamedLayoutArtifact, &validation};
            const editor::detail::EditorStateArtifactPublishResult published =
                editor::detail::PublishEditorStateArtifact(
                    paths, m_textOutput.AsChar(), m_textOutput.Length(), m_textImage.address,
                    m_textImage.size, m_artifactStageIdentity, validator);
            if (published != editor::detail::EditorStateArtifactPublishResult::Published)
            {
                VG_LOG_WARNING(diagnostics::Category::Engine,
                               "named-layout payload publication failed (%u)",
                               static_cast<u32>(published));
                return false;
            }
            return true;
        }

        [[nodiscard]] bool CaptureNamedLayoutProjection(
            const u64 id, EditorUserStateCaptureScratch& capture,
            editor::detail::EditorNamedLayoutDocumentView& layout,
            const editor::detail::EditorNamedLayoutDocumentView* const preserved = nullptr) noexcept
        {
            layout = {};
            if (preserved != nullptr && preserved->id != id)
                return false;
            editor::EditorUserStateDocumentView document;
            editor::EditorUserStateFailure failure;
            if (id == 0 || !CaptureCurrentDocument(capture, document, failure))
                return false;
            u32 presentationCount = 0;
            u32 presentationByteCount = 0;
            for (u32 index = 0; index < capture.panelStateCount; ++index)
            {
                editor::detail::EditorUserStateTextPanelStateRecord state = capture.panelStates[index];
                if (state.scope != editor::EditorPanelStateScope::Presentation)
                    continue;
                if (state.byteOffset > capture.panelStateByteCount ||
                    state.byteCount > capture.panelStateByteCount - state.byteOffset)
                    return false;
                if (state.byteCount != 0 && state.byteOffset != presentationByteCount)
                    std::memmove(capture.panelStateBytes + presentationByteCount,
                                 capture.panelStateBytes + state.byteOffset, state.byteCount);
                state.byteOffset = presentationByteCount;
                presentationByteCount += state.byteCount;
                capture.panelStates[presentationCount++] = state;
            }
            capture.panelStateCount = presentationCount;
            capture.panelStateByteCount = presentationByteCount;
            layout.version = preserved != nullptr ? preserved->version : editor::detail::EditorNamedLayoutTextVersion;
            layout.id = id;
            layout.primaryWindow = document.primaryWindow;
            layout.panels = document.panels;
            layout.panelStates = {capture.panelStates, capture.panelStateCount};
            layout.panelStateBytes = {capture.panelStateBytes, capture.panelStateByteCount};
            layout.displays = document.displays;
            layout.imguiBytes = document.imguiBytes;
            if (preserved != nullptr)
            {
                layout.unknownAssignments = preserved->unknownAssignments;
                layout.unknownBytes = preserved->unknownBytes;
            }
            return true;
        }

        struct NamedLayoutCommandBinding final
        {
            ManagedEditorUserStateService* service = nullptr;
            NamedLayoutOperation operation = NamedLayoutOperation::None;
        };

        static editor::EditorCommandState QueryNamedLayoutCommand(void*, void* const handlerData) noexcept
        {
            const auto* const binding = static_cast<const NamedLayoutCommandBinding*>(handlerData);
            if (binding == nullptr || binding->service == nullptr)
                return editor::EditorCommandState::Unsupported();
            const ManagedEditorUserStateService& service = *binding->service;
            if (service.m_status.state != editor::EditorUserStateServiceState::Running ||
                !service.m_admissionOpen || service.m_namedLayoutPending.operation != NamedLayoutOperation::None ||
                service.m_namedLayoutDialogOutstanding)
                return editor::EditorCommandState::Disabled();
            const bool hasActive = service.m_activeLayoutId != 0 &&
                                   service.FindNamedLayoutIndexRecord(service.m_activeLayoutId) != nullptr;
            const bool hasAny = service.m_namedLayoutIndex.layoutCount != 0;
            switch (binding->operation)
            {
            case NamedLayoutOperation::PromptSave:
            case NamedLayoutOperation::Revert:
                return hasActive && service.m_activeLayoutModified
                    ? editor::EditorCommandState::Enabled()
                    : editor::EditorCommandState::Disabled();
            case NamedLayoutOperation::PromptRename:
            case NamedLayoutOperation::PromptDuplicate:
                return hasActive ? editor::EditorCommandState::Enabled()
                                 : editor::EditorCommandState::Disabled();
            case NamedLayoutOperation::PromptSwitch:
            case NamedLayoutOperation::PromptDelete:
                return hasAny ? editor::EditorCommandState::Enabled()
                              : editor::EditorCommandState::Disabled();
            case NamedLayoutOperation::PromptSaveAs:
            case NamedLayoutOperation::ResetDefault:
                return editor::EditorCommandState::Enabled();
            default:
                return editor::EditorCommandState::Disabled();
            }
        }

        static bool InvokeNamedLayoutCommand(void*, void* const handlerData) noexcept
        {
            auto* const binding = static_cast<NamedLayoutCommandBinding*>(handlerData);
            if (binding == nullptr || binding->service == nullptr)
                return false;
            ManagedEditorUserStateService& service = *binding->service;
            if (QueryNamedLayoutCommand(nullptr, handlerData).kind != editor::EditorCommandStateKind::Enabled)
                return false;
            service.m_namedLayoutPending = {};
            service.m_namedLayoutPending.operation = binding->operation;
            return true;
        }

        [[nodiscard]] bool RegisterNamedLayoutCommands() noexcept
        {
            if (m_namedLayoutCommandsRegistered || m_commandPersistence == nullptr)
                return m_namedLayoutCommandsRegistered;
            struct CommandSpec final
            {
                editor::EditorCommandId id;
                const char* name;
                NamedLayoutOperation operation;
            };
            constexpr CommandSpec specs[]{
                {editor::detail::SaveCurrentLayoutAsCommandId, "editor.layout.save-as", NamedLayoutOperation::PromptSaveAs},
                {editor::detail::SaveLayoutCommandId, "editor.layout.save", NamedLayoutOperation::PromptSave},
                {editor::detail::SwitchLayoutCommandId, "editor.layout.switch", NamedLayoutOperation::PromptSwitch},
                {editor::detail::RevertLayoutCommandId, "editor.layout.revert", NamedLayoutOperation::Revert},
                {editor::detail::RenameLayoutCommandId, "editor.layout.rename", NamedLayoutOperation::PromptRename},
                {editor::detail::DuplicateLayoutCommandId, "editor.layout.duplicate", NamedLayoutOperation::PromptDuplicate},
                {editor::detail::DeleteLayoutCommandId, "editor.layout.delete", NamedLayoutOperation::PromptDelete},
                {editor::detail::ResetDefaultLayoutCommandId, "editor.layout.reset-default", NamedLayoutOperation::ResetDefault}};
            static_assert(std::size(specs) == std::extent_v<decltype(m_namedLayoutCommandBindings)>);
            for (u32 index = 0; index < std::size(specs); ++index)
            {
                m_namedLayoutCommandBindings[index] = {this, specs[index].operation};
                editor::EditorCommandFailure failure;
                if (!m_commandPersistence->RegisterCoreWorkspaceCommand(
                        {specs[index].id, specs[index].name, editor::EditorCommandFlags::None},
                        QueryNamedLayoutCommand, InvokeNamedLayoutCommand,
                        &m_namedLayoutCommandBindings[index], m_namedLayoutCommandTokens[index],
                        m_namedLayoutHandlerTokens[index], &failure))
                {
                    for (u32 rollback = index; rollback-- > 0;)
                    {
                        editor::EditorCommandFailure cleanupFailure;
                        static_cast<void>(m_commandPersistence->UnregisterCoreWorkspaceCommand(
                            m_namedLayoutCommandTokens[rollback], m_namedLayoutHandlerTokens[rollback],
                            &cleanupFailure));
                        m_namedLayoutCommandTokens[rollback] = {};
                        m_namedLayoutHandlerTokens[rollback] = {};
                    }
                    return false;
                }
            }
            m_namedLayoutCommandsRegistered = true;
            return true;
        }

        [[nodiscard]] bool UnregisterNamedLayoutCommands() noexcept
        {
            if (!m_namedLayoutCommandsRegistered)
                return true;
            if (m_commandPersistence == nullptr)
                return false;
            bool cleaned = true;
            for (u32 index = static_cast<u32>(std::size(m_namedLayoutCommandTokens)); index-- > 0;)
            {
                if (!m_namedLayoutCommandTokens[index].IsValid() || !m_namedLayoutHandlerTokens[index].IsValid())
                    continue;
                editor::EditorCommandFailure failure;
                if (!m_commandPersistence->UnregisterCoreWorkspaceCommand(
                        m_namedLayoutCommandTokens[index], m_namedLayoutHandlerTokens[index], &failure))
                {
                    cleaned = false;
                    continue;
                }
                m_namedLayoutCommandTokens[index] = {};
                m_namedLayoutHandlerTokens[index] = {};
            }
            if (cleaned)
                m_namedLayoutCommandsRegistered = false;
            return cleaned;
        }

        [[nodiscard]] bool RequestNamedLayoutPrompt(const NamedLayoutOperation operation) noexcept
        {
            if (m_uiPersistence == nullptr)
                return false;
            using DialogKind = editor::detail::EditorUiNamedLayoutDialogKind;
            DialogKind kind = DialogKind::None;
            switch (operation)
            {
            case NamedLayoutOperation::PromptSaveAs: kind = DialogKind::SaveAs; break;
            case NamedLayoutOperation::PromptSave: kind = DialogKind::SaveOverwrite; break;
            case NamedLayoutOperation::PromptSwitch: kind = DialogKind::Switch; break;
            case NamedLayoutOperation::PromptRename: kind = DialogKind::Rename; break;
            case NamedLayoutOperation::PromptDuplicate: kind = DialogKind::Duplicate; break;
            case NamedLayoutOperation::PromptDelete: kind = DialogKind::Delete; break;
            default: return false;
            }

            editor::detail::EditorUiNamedLayoutChoice choices[editor::detail::MaximumEditorUiNamedLayouts]{};
            for (u32 index = 0; index < m_namedLayoutIndex.layoutCount; ++index)
            {
                choices[index].id = m_namedLayoutIndex.layouts[index].id;
                choices[index].nameByteCount = m_namedLayoutIndex.layouts[index].nameByteCount;
                choices[index].modified = choices[index].id == m_activeLayoutId &&
                                          m_activeLayoutModified;
                std::memcpy(choices[index].name, m_namedLayoutIndex.layouts[index].name,
                            choices[index].nameByteCount + 1u);
            }
            char initial[editor::detail::MaximumEditorUiNamedLayoutNameBytes + 1u]{};
            u32 initialBytes = 0;
            if (kind == DialogKind::SaveAs)
            {
                constexpr char DefaultName[] = "Layout";
                initialBytes = sizeof(DefaultName) - 1u;
                std::memcpy(initial, DefaultName, sizeof(DefaultName));
            }
            else if (const auto* const active = FindNamedLayoutIndexRecord(m_activeLayoutId);
                     active != nullptr && (kind == DialogKind::Rename || kind == DialogKind::Duplicate))
            {
                initialBytes = active->nameByteCount;
                std::memcpy(initial, active->name, initialBytes);
                if (kind == DialogKind::Duplicate)
                {
                    constexpr char Suffix[] = " Copy";
                    const u32 suffixBytes = sizeof(Suffix) - 1u;
                    if (initialBytes + suffixBytes <= editor::detail::MaximumEditorUiNamedLayoutNameBytes)
                    {
                        std::memcpy(initial + initialBytes, Suffix, suffixBytes);
                        initialBytes += suffixBytes;
                    }
                }
                initial[initialBytes] = '\0';
            }

            editor::EditorUiFailure failure;
            if (!m_uiPersistence->RequestNamedLayoutDialog(
                    kind, {choices, m_namedLayoutIndex.layoutCount}, m_activeLayoutId,
                    {initial, initialBytes}, &failure))
                return false;
            m_namedLayoutDialogOutstanding = true;
            return true;
        }

        [[nodiscard]] bool ConsumeNamedLayoutPromptResult() noexcept
        {
            if (!m_namedLayoutDialogOutstanding || m_uiPersistence == nullptr)
                return true;
            editor::detail::EditorUiNamedLayoutDialogResult result;
            editor::EditorUiFailure failure;
            if (!m_uiPersistence->ConsumeNamedLayoutDialogResult(result, &failure))
                return false;
            if (result.kind == editor::detail::EditorUiNamedLayoutDialogKind::None)
                return true;
            m_namedLayoutDialogOutstanding = false;
            if (!result.accepted)
                return true;
            m_namedLayoutPending = {};
            m_namedLayoutPending.id = result.id;
            m_namedLayoutPending.nameByteCount = result.nameByteCount;
            if (result.nameByteCount != 0)
                std::memcpy(m_namedLayoutPending.name, result.name, result.nameByteCount + 1u);
            switch (result.kind)
            {
            case editor::detail::EditorUiNamedLayoutDialogKind::SaveAs:
                m_namedLayoutPending.operation = NamedLayoutOperation::SaveAs;
                break;
            case editor::detail::EditorUiNamedLayoutDialogKind::SaveOverwrite:
                m_namedLayoutPending.operation = NamedLayoutOperation::Save;
                m_namedLayoutPending.id = m_activeLayoutId;
                break;
            case editor::detail::EditorUiNamedLayoutDialogKind::Switch:
                m_namedLayoutPending.operation = NamedLayoutOperation::Switch;
                break;
            case editor::detail::EditorUiNamedLayoutDialogKind::Rename:
                m_namedLayoutPending.operation = NamedLayoutOperation::Rename;
                m_namedLayoutPending.id = m_activeLayoutId;
                break;
            case editor::detail::EditorUiNamedLayoutDialogKind::Duplicate:
                m_namedLayoutPending.operation = NamedLayoutOperation::Duplicate;
                m_namedLayoutPending.id = m_activeLayoutId;
                break;
            case editor::detail::EditorUiNamedLayoutDialogKind::Delete:
                m_namedLayoutPending.operation = NamedLayoutOperation::Delete;
                break;
            default:
                m_namedLayoutPending = {};
                break;
            }
            return true;
        }

        [[nodiscard]] bool BuildPrivatePanelPresentation(
            const containers::ArraySpan<const editor::EditorUserStatePanelRecord> source,
            editor::detail::EditorPanelUserStateRecord (&destination)[editor::MaximumEditorUserStatePanels],
            u32& count) const noexcept
        {
            count = 0;
            if (source.Size() > editor::MaximumEditorUserStatePanels)
                return false;
            for (u32 index = 0; index < source.Size(); ++index)
            {
                const editor::EditorUserStatePanelRecord& input = source[index];
                if (input.typeId == 0 || input.instanceId == 0 ||
                    input.fallbackTitleByteCount > editor::MaximumEditorUserStatePanelTitleBytes ||
                    input.fallbackTitleByteCount >= editor::MaximumEditorPanelNameBytes)
                    return false;
                editor::detail::EditorPanelUserStateRecord& output = destination[index];
                output = {};
                output.type = input.typeId;
                output.instance = input.instanceId;
                output.fallbackTitleByteCount = input.fallbackTitleByteCount;
                output.open = input.open;
                if (input.fallbackTitleByteCount != 0)
                    std::memcpy(output.fallbackTitle, input.fallbackTitle,
                                input.fallbackTitleByteCount);
                output.fallbackTitle[input.fallbackTitleByteCount] = '\0';
            }
            count = source.Size();
            return true;
        }

        [[nodiscard]] bool FindPresentationPanelType(
            const containers::ArraySpan<const editor::EditorUserStatePanelRecord> panels,
            const editor::EditorPanelInstanceId instance, editor::EditorPanelTypeId& type) const noexcept
        {
            type = editor::InvalidEditorPanelTypeId;
            for (const editor::EditorUserStatePanelRecord& panel : panels)
            {
                if (panel.instanceId != instance)
                    continue;
                type = panel.typeId;
                return type != editor::InvalidEditorPanelTypeId;
            }
            return false;
        }

        [[nodiscard]] bool RestorePresentationScope(
            const containers::ArraySpan<const editor::EditorUserStatePanelRecord> panels,
            const containers::ArraySpan<const editor::detail::EditorUserStateTextPanelStateRecord> states,
            const containers::ArraySpan<const u8> stateBytes, const editor::EditorPanelStateScope scope,
            const bool requireSuccessfulRestore) noexcept
        {
            if (!m_captureScratch)
                return false;
            auto& scratch = *static_cast<EditorUserStateCaptureScratch*>(m_captureScratch.address);
            for (const editor::detail::EditorUserStateTextPanelStateRecord& state : states)
            {
                if (state.scope != scope)
                    continue;
                if (state.version == 0 || state.byteOffset > stateBytes.Size() ||
                    state.byteCount > stateBytes.Size() - state.byteOffset)
                    return false;
                editor::EditorPanelTypeId type = editor::InvalidEditorPanelTypeId;
                if (!FindPresentationPanelType(panels, state.instanceId, type))
                    return false;
                const u8* const payload = state.byteCount != 0
                    ? stateBytes.Data() + state.byteOffset
                    : nullptr;
                editor::EditorUiFailure uiFailure;
                const editor::EditorPanelStateResult result = editor::detail::RestoreEditorPanelPersistentState(
                    *m_uiPersistence, type, state.instanceId, scope, state.version,
                    {payload, state.byteCount},
                    scratch.panelStateCallbackBytes,
                    static_cast<u32>(sizeof(scratch.panelStateCallbackBytes)), &uiFailure);
                if (result == editor::EditorPanelStateResult::Success ||
                    result == editor::EditorPanelStateResult::NoState ||
                    result == editor::EditorPanelStateResult::ProviderUnavailable)
                    continue;
                VG_LOG_WARNING(diagnostics::Category::Engine,
                               "named-layout panel-state restore skipped instance=%llu scope=%u result=%u",
                               static_cast<unsigned long long>(state.instanceId),
                               static_cast<u32>(scope), static_cast<u32>(result));
                if (requireSuccessfulRestore)
                    return false;
            }
            return true;
        }

        [[nodiscard]] bool RestoreCapturedScope(EditorUserStateCaptureScratch& capture,
                                                const editor::EditorPanelStateScope scope,
                                                const bool requireSuccessfulRestore) noexcept
        {
            return RestorePresentationScope(
                {capture.panels, capture.panelCount},
                {capture.panelStates, capture.panelStateCount},
                {capture.panelStateBytes, capture.panelStateByteCount}, scope,
                requireSuccessfulRestore);
        }

        [[nodiscard]] bool BuildDefaultLayoutPresentationTarget(
            editor::detail::EditorNamedLayoutDocumentView& target) noexcept
        {
            target = {};
            if (!m_defaultLayoutCaptured || !m_defaultLayoutCaptureScratch)
                return false;
            const auto& defaults = *static_cast<const EditorUserStateCaptureScratch*>(
                m_defaultLayoutCaptureScratch.address);
            target.primaryWindow = m_defaultPrimaryWindow;
            target.panels = {defaults.panels, defaults.panelCount};
            target.panelStates = {defaults.panelStates, defaults.panelStateCount};
            target.panelStateBytes = {defaults.panelStateBytes, defaults.panelStateByteCount};
            target.displays = {defaults.displays, defaults.displayCount};
            target.imguiBytes = {defaults.imguiBytes, defaults.imguiByteCount};
            return true;
        }

        [[nodiscard]] bool ApplyLayoutPresentationTarget(
            const editor::detail::EditorNamedLayoutDocumentView& target,
            const LayoutPresentationApplyMode mode, u32& repairedPlacements,
            editor::EditorUiFailure& uiFailure,
            const LayoutPresentationFailurePolicy failurePolicy =
                LayoutPresentationFailurePolicy::StopOnFailure) noexcept
        {
            repairedPlacements = 0;
            const bool bestEffort = failurePolicy == LayoutPresentationFailurePolicy::BestEffort;
            bool ok = true;
            editor::detail::EditorPanelUserStateRecord targetPanels[editor::MaximumEditorUserStatePanels]{};
            u32 targetPanelCount = 0;
            if (!BuildPrivatePanelPresentation(target.panels, targetPanels, targetPanelCount))
            {
                if (!bestEffort)
                    return false;
                ok = false;
            }

            editor::detail::EditorPanelUserStateRestoreStatus panelStatus;
            if (targetPanelCount == target.panels.Size() &&
                !m_uiPersistence->ReconcilePanelPresentation(
                    {targetPanels, targetPanelCount}, panelStatus, &uiFailure))
            {
                if (!bestEffort)
                    return false;
                ok = false;
            }
            if (!ApplyPrimaryWindowState(target.primaryWindow))
            {
                if (!bestEffort)
                    return false;
                ok = false;
            }

            const bool layoutApplied = mode == LayoutPresentationApplyMode::CanonicalDefault
                ? m_uiPersistence->ResetUserStateLayoutToDefault(&uiFailure)
                : m_uiPersistence->ApplyUserStateLayoutLive(
                      target.imguiBytes, target.displays, repairedPlacements, &uiFailure) ==
                      editor::detail::EditorUiUserStateLayoutLoadResult::AppliedUsable;
            if (!layoutApplied)
            {
                if (!bestEffort)
                    return false;
                ok = false;
            }

            if (!RestorePresentationScope(
                    target.panels, target.panelStates, target.panelStateBytes,
                    editor::EditorPanelStateScope::Presentation, false))
            {
                if (!bestEffort)
                    return false;
                ok = false;
            }
            return ok;
        }

        [[nodiscard]] bool RollbackLayoutPresentation(
            EditorUserStateCaptureScratch& backup,
            const editor::EditorUserStateDocumentView& backupDocument) noexcept
        {
            editor::detail::EditorNamedLayoutDocumentView target;
            target.primaryWindow = backupDocument.primaryWindow;
            target.panels = {backup.panels, backup.panelCount};
            target.panelStates = {backup.panelStates, backup.panelStateCount};
            target.panelStateBytes = {backup.panelStateBytes, backup.panelStateByteCount};
            target.displays = {backup.displays, backup.displayCount};
            target.imguiBytes = {backup.imguiBytes, backup.imguiByteCount};

            editor::EditorUiFailure uiFailure;
            u32 repairedPlacements = 0;
            const bool presentationRestored = ApplyLayoutPresentationTarget(
                target, LayoutPresentationApplyMode::SavedSnapshot, repairedPlacements, uiFailure,
                LayoutPresentationFailurePolicy::BestEffort);
            const bool contentRestored = RestoreCapturedScope(
                backup, editor::EditorPanelStateScope::Content, true);
            return presentationRestored && contentRestored;
        }

        [[nodiscard]] bool ApplyLayoutPresentation(
            const editor::detail::EditorNamedLayoutDocumentView& target,
            const LayoutPresentationApplyMode mode = LayoutPresentationApplyMode::SavedSnapshot) noexcept
        {
            if (m_uiPersistence == nullptr || !m_captureScratch)
                return false;
            editor::EditorUiFailure uiFailure;
            const bool settling = m_uiPersistence->IsUserStateLayoutSettling(&uiFailure);
            if (uiFailure.code != editor::EditorUiFailureCode::None || settling)
                return false;

            auto& backup = *static_cast<EditorUserStateCaptureScratch*>(m_captureScratch.address);
            editor::EditorUserStateDocumentView backupDocument;
            editor::EditorUserStateFailure captureFailure;
            if (!CaptureCurrentDocument(backup, backupDocument, captureFailure))
                return false;
            m_layoutBackupPrimaryWindow = backupDocument.primaryWindow;

            u32 repairedPlacements = 0;
            if (!ApplyLayoutPresentationTarget(target, mode, repairedPlacements, uiFailure))
            {
                static_cast<void>(RollbackLayoutPresentation(backup, backupDocument));
                return false;
            }
            if (!RestoreCapturedScope(backup, editor::EditorPanelStateScope::Content, true))
            {
                static_cast<void>(RollbackLayoutPresentation(backup, backupDocument));
                return false;
            }
            m_status.repairedPlacements = repairedPlacements;
            if (mode == LayoutPresentationApplyMode::CanonicalDefault)
            {
                // The low-level default dock operation clears the live ImGui tree immediately and
                // rebuilds it during the next authored UI frame. The surrounding presentation
                // transaction is shared with saved layouts; only this source-specific completion
                // timing differs.
                m_defaultLayoutRebuildPending = true;
            }
            else
                m_forceSessionLayoutDirty = true;
            return true;
        }

        [[nodiscard]] bool RollbackLastLayoutPresentation() noexcept
        {
            if (!m_captureScratch)
                return false;
            auto& backup = *static_cast<EditorUserStateCaptureScratch*>(m_captureScratch.address);
            editor::EditorUserStateDocumentView backupDocument;
            backupDocument.primaryWindow = m_layoutBackupPrimaryWindow;
            backupDocument.panels = {backup.panels, backup.panelCount};
            backupDocument.displays = {backup.displays, backup.displayCount};
            backupDocument.imguiBytes = {backup.imguiBytes, backup.imguiByteCount};
            return RollbackLayoutPresentation(backup, backupDocument);
        }

        [[nodiscard]] static u64 HashCanonicalNamedLayout(const containers::String& text) noexcept
        {
            u64 hash = 14695981039346656037ull;
            const char* const bytes = text.AsChar();
            for (u32 index = 0; index < text.Length(); ++index)
            {
                hash ^= static_cast<u8>(bytes[index]);
                hash *= 1099511628211ull;
            }
            return hash;
        }

        [[nodiscard]] bool RefreshActiveLayoutModified() noexcept
        {
            if (m_activeLayoutId == 0 || FindNamedLayoutIndexRecord(m_activeLayoutId) == nullptr)
            {
                m_activeLayoutModified = false;
                return true;
            }
            if (!m_captureScratch)
                return false;
            editor::detail::EditorNamedLayoutDocumentView saved;
            if (!LoadNamedLayout(m_activeLayoutId, saved))
            {
                m_activeLayoutModified = true;
                return true;
            }
            auto& capture = *static_cast<EditorUserStateCaptureScratch*>(m_captureScratch.address);
            editor::detail::EditorNamedLayoutDocumentView current;
            if (!CaptureNamedLayoutProjection(m_activeLayoutId, capture, current, &saved))
                return false;
            containers::String currentText;
            containers::String savedText;
            editor::detail::EditorUserStateTextFailure textFailure;
            if (editor::detail::EncodeEditorNamedLayoutText(current, currentText, &textFailure) !=
                    editor::detail::EditorUserStateTextResult::Success ||
                editor::detail::EncodeEditorNamedLayoutText(saved, savedText, &textFailure) !=
                    editor::detail::EditorUserStateTextResult::Success)
                return false;
            m_activeLayoutModified = currentText.Length() != savedText.Length() ||
                                     HashCanonicalNamedLayout(currentText) != HashCanonicalNamedLayout(savedText);
            return true;
        }

        void DeleteNamedLayoutArtifacts(const u64 id) noexcept
        {
            if (!filesystem::IsInitialized())
                return;
            editor::detail::EditorStateArtifactPaths paths;
            if (!BuildNamedLayoutArtifactPaths(id, paths))
                return;
            filesystem::Manager& files = filesystem::GetManager();
            if (files.FileExist(paths.canonical))
                static_cast<void>(files.DeleteFile(paths.canonical));
            if (files.FileExist(paths.lastKnownGood))
                static_cast<void>(files.DeleteFile(paths.lastKnownGood));
        }

        [[nodiscard]] bool ExecuteNamedLayoutOperation(const NamedLayoutRequest request) noexcept
        {
            m_namedLayoutLeaseBusy = false;
            if (request.operation == NamedLayoutOperation::None)
                return true;

            switch (request.operation)
            {
            case NamedLayoutOperation::PromptSaveAs:
            case NamedLayoutOperation::PromptSave:
            case NamedLayoutOperation::PromptSwitch:
            case NamedLayoutOperation::PromptRename:
            case NamedLayoutOperation::PromptDuplicate:
            case NamedLayoutOperation::PromptDelete:
                static_cast<void>(LoadNamedLayoutIndex());
                return RequestNamedLayoutPrompt(request.operation);
            default:
                break;
            }

            // Named layouts are personal and may be edited by multiple editor processes. Serialize
            // the full read/mutate/publish transaction so independent CRUD cannot lose updates or
            // resurrect a record after another process deleted its payload. The runtime-active ID
            // remains process-local below; the index active.id is only the last-active startup hint.
            NamedLayoutIndexLease indexLease;
            if (!indexLease.Acquired())
            {
                m_namedLayoutLeaseBusy = indexLease.Busy();
                return false;
            }
            const editor::detail::EditorStateArtifactLoadResult indexLoad = LoadNamedLayoutIndex();
            if (m_activeLayoutId != 0 && FindNamedLayoutIndexRecord(m_activeLayoutId) == nullptr)
            {
                m_activeLayoutId = 0;
                m_activeLayoutModified = false;
            }

            switch (request.operation)
            {
            case NamedLayoutOperation::SaveAs:
            {
                if (indexLoad != editor::detail::EditorStateArtifactLoadResult::LoadedCanonical &&
                    indexLoad != editor::detail::EditorStateArtifactLoadResult::Missing)
                    return false;
                const containers::StringView name{request.name, request.nameByteCount};
                if (m_namedLayoutIndex.layoutCount >= editor::detail::MaximumEditorNamedLayouts ||
                    !NamedLayoutNameAvailable(name))
                    return false;
                const u64 id = GenerateNamedLayoutId();
                if (id == 0)
                    return false;
                auto& capture = *static_cast<EditorUserStateCaptureScratch*>(m_captureScratch.address);
                editor::detail::EditorNamedLayoutDocumentView layout;
                if (!CaptureNamedLayoutProjection(id, capture, layout) || !PublishNamedLayout(layout))
                    return false;
                auto candidate = m_namedLayoutIndex;
                editor::detail::EditorNamedLayoutIndexRecord& record =
                    candidate.layouts[candidate.layoutCount++];
                record.id = id;
                record.nameByteCount = request.nameByteCount;
                std::memcpy(record.name, request.name, request.nameByteCount + 1u);
                candidate.activeId = id;
                if (!PublishNamedLayoutIndex(candidate))
                {
                    DeleteNamedLayoutArtifacts(id);
                    return false;
                }
                m_activeLayoutId = id;
                m_activeLayoutModified = false;
                return true;
            }
            case NamedLayoutOperation::Save:
            {
                if (indexLoad != editor::detail::EditorStateArtifactLoadResult::LoadedCanonical)
                    return false;
                const u64 id = m_activeLayoutId;
                if (id == 0 || FindNamedLayoutIndexRecord(id) == nullptr)
                    return false;
                editor::detail::EditorNamedLayoutDocumentView saved;
                editor::detail::EditorStateArtifactLoadResult layoutLoad;
                if (!LoadNamedLayout(id, saved, &layoutLoad) ||
                    layoutLoad != editor::detail::EditorStateArtifactLoadResult::LoadedCanonical)
                    return false;
                auto& capture = *static_cast<EditorUserStateCaptureScratch*>(m_captureScratch.address);
                editor::detail::EditorNamedLayoutDocumentView layout;
                if (!CaptureNamedLayoutProjection(id, capture, layout, &saved) || !PublishNamedLayout(layout))
                    return false;
                m_activeLayoutModified = false;
                return true;
            }
            case NamedLayoutOperation::Switch:
            {
                if (indexLoad != editor::detail::EditorStateArtifactLoadResult::LoadedCanonical)
                    return false;
                if (FindNamedLayoutIndexRecord(request.id) == nullptr)
                    return false;
                editor::detail::EditorNamedLayoutDocumentView layout;
                if (!LoadNamedLayout(request.id, layout))
                    return false;
                if (!ApplyLayoutPresentation(layout))
                    return false;
                auto candidate = m_namedLayoutIndex;
                candidate.activeId = request.id;
                if (!PublishNamedLayoutIndex(candidate))
                {
                    static_cast<void>(RollbackLastLayoutPresentation());
                    return false;
                }
                m_activeLayoutId = request.id;
                m_activeLayoutModified = false;
                return true;
            }
            case NamedLayoutOperation::Revert:
            {
                const u64 id = m_activeLayoutId;
                editor::detail::EditorNamedLayoutDocumentView layout;
                if (id == 0 || FindNamedLayoutIndexRecord(id) == nullptr ||
                    !LoadNamedLayout(id, layout) || !ApplyLayoutPresentation(layout))
                    return false;
                m_activeLayoutModified = false;
                return true;
            }
            case NamedLayoutOperation::Rename:
            {
                if (indexLoad != editor::detail::EditorStateArtifactLoadResult::LoadedCanonical)
                    return false;
                const containers::StringView name{request.name, request.nameByteCount};
                editor::detail::EditorNamedLayoutIndexRecord* const live = FindNamedLayoutIndexRecord(request.id);
                if (live == nullptr || !NamedLayoutNameAvailable(name, request.id))
                    return false;
                auto candidate = m_namedLayoutIndex;
                for (u32 index = 0; index < candidate.layoutCount; ++index)
                {
                    if (candidate.layouts[index].id != request.id)
                        continue;
                    candidate.layouts[index].nameByteCount = request.nameByteCount;
                    std::memcpy(candidate.layouts[index].name, request.name, request.nameByteCount + 1u);
                    break;
                }
                return PublishNamedLayoutIndex(candidate);
            }
            case NamedLayoutOperation::Duplicate:
            {
                if (indexLoad != editor::detail::EditorStateArtifactLoadResult::LoadedCanonical)
                    return false;
                const containers::StringView name{request.name, request.nameByteCount};
                if (FindNamedLayoutIndexRecord(request.id) == nullptr ||
                    m_namedLayoutIndex.layoutCount >= editor::detail::MaximumEditorNamedLayouts ||
                    !NamedLayoutNameAvailable(name))
                    return false;
                editor::detail::EditorNamedLayoutDocumentView source;
                if (!LoadNamedLayout(request.id, source))
                    return false;
                const u64 id = GenerateNamedLayoutId();
                if (id == 0)
                    return false;
                source.id = id;
                if (!PublishNamedLayout(source))
                    return false;
                auto candidate = m_namedLayoutIndex;
                editor::detail::EditorNamedLayoutIndexRecord& record =
                    candidate.layouts[candidate.layoutCount++];
                record.id = id;
                record.nameByteCount = request.nameByteCount;
                std::memcpy(record.name, request.name, request.nameByteCount + 1u);
                candidate.activeId = id;
                if (!PublishNamedLayoutIndex(candidate))
                {
                    DeleteNamedLayoutArtifacts(id);
                    return false;
                }
                m_activeLayoutId = id;
                // Duplication selects the new saved layout without changing the live presentation.
                // It is clean only if the current presentation still matches the duplicated source.
                return RefreshActiveLayoutModified();
            }
            case NamedLayoutOperation::Delete:
            {
                if (indexLoad != editor::detail::EditorStateArtifactLoadResult::LoadedCanonical)
                    return false;
                u32 removeIndex = ~u32{0};
                for (u32 index = 0; index < m_namedLayoutIndex.layoutCount; ++index)
                    if (m_namedLayoutIndex.layouts[index].id == request.id)
                    {
                        removeIndex = index;
                        break;
                    }
                if (removeIndex == ~u32{0})
                    return false;
                const bool deletingActive = m_activeLayoutId == request.id;
                auto candidate = m_namedLayoutIndex;
                for (u32 index = removeIndex + 1u; index < candidate.layoutCount; ++index)
                    candidate.layouts[index - 1u] = candidate.layouts[index];
                --candidate.layoutCount;
                candidate.layouts[candidate.layoutCount] = {};
                if (candidate.activeId == request.id)
                    candidate.activeId = 0;
                if (!PublishNamedLayoutIndex(candidate))
                    return false;
                // Do not remove the payload while the just-rotated index LKG can still reference
                // it. A deleted layout is made unreachable by the canonical index immediately;
                // retaining the orphaned payload keeps canonical->LKG recovery internally
                // consistent instead of recovering an index record whose artifact was destroyed.
                if (deletingActive)
                {
                    m_activeLayoutId = 0;
                    m_activeLayoutModified = false;
                }
                return true;
            }
            case NamedLayoutOperation::ResetDefault:
            {
                if (m_namedLayoutIndex.activeId != 0 &&
                    indexLoad != editor::detail::EditorStateArtifactLoadResult::LoadedCanonical)
                    return false;
                editor::detail::EditorNamedLayoutDocumentView defaults;
                if (!BuildDefaultLayoutPresentationTarget(defaults) ||
                    !ApplyLayoutPresentation(defaults, LayoutPresentationApplyMode::CanonicalDefault))
                    return false;
                if (m_namedLayoutIndex.activeId != 0)
                {
                    auto candidate = m_namedLayoutIndex;
                    candidate.activeId = 0;
                    if (!PublishNamedLayoutIndex(candidate))
                    {
                        static_cast<void>(RollbackLastLayoutPresentation());
                        m_defaultLayoutRebuildPending = false;
                        return false;
                    }
                }
                m_activeLayoutId = 0;
                m_activeLayoutModified = false;
                return true;
            }
            default:
                return false;
            }
        }

        void ProcessNamedLayoutWork(const f64 nowSeconds) noexcept
        {
            if (!ConsumeNamedLayoutPromptResult())
                return;
            if (m_namedLayoutPending.operation == NamedLayoutOperation::None)
                return;
            const bool appliesLivePresentation =
                m_namedLayoutPending.operation == NamedLayoutOperation::Switch ||
                m_namedLayoutPending.operation == NamedLayoutOperation::Revert ||
                m_namedLayoutPending.operation == NamedLayoutOperation::ResetDefault;
            if (appliesLivePresentation)
            {
                editor::EditorUiFailure uiFailure;
                const bool settling = m_uiPersistence->IsUserStateLayoutSettling(&uiFailure);
                if (uiFailure.code != editor::EditorUiFailureCode::None)
                {
                    VG_LOG_WARNING(diagnostics::Category::Engine,
                                   "editor named-layout operation could not observe layout settling (%u)",
                                   static_cast<u32>(uiFailure.code));
                    m_namedLayoutPending = {};
                    return;
                }
                editor::EditorUserStatePrimaryWindowRecord primaryWindow;
                if (!SamplePrimaryWindow(primaryWindow))
                {
                    VG_LOG_WARNING(diagnostics::Category::Engine,
                                   "editor named-layout operation could not observe primary-window settling");
                    return;
                }
                const bool primaryWindowChangedSinceObservation =
                    !m_primaryWindowBaselineValid || !(primaryWindow == m_lastObservedPrimaryWindow);
                const bool primaryWindowSettling =
                    primaryWindowChangedSinceObservation ||
                    (m_lastPrimaryWindowChangeRealTimeSeconds > 0.0 &&
                     nowSeconds - m_lastPrimaryWindowChangeRealTimeSeconds < 2.0);
                if (settling || primaryWindowSettling)
                    return;
            }
            const NamedLayoutRequest request = m_namedLayoutPending;
            m_namedLayoutPending = {};
            if (!ExecuteNamedLayoutOperation(request))
            {
                if (m_namedLayoutLeaseBusy)
                {
                    m_namedLayoutPending = request;
                    return;
                }
                VG_LOG_WARNING(diagnostics::Category::Engine,
                               "editor named-layout operation failed (%u)",
                               static_cast<u32>(request.operation));
            }
        }

        [[nodiscard]] SessionCandidateLoadResult ReadSessionReference(
            const filesystem::AbsolutePath& referencePath, SessionReference& reference) noexcept
        {
            reference = {};
            if (referencePath.Empty())
                return SessionCandidateLoadResult::IoFailure;
            char referenceBytes[editor::detail::MaximumEditorStateReferenceBytes]{};
            usize referenceByteCount = 0;
            switch (filesystem::ReadCompleteFile(
                referencePath, referenceBytes, sizeof(referenceBytes), referenceByteCount))
            {
            case filesystem::CompleteFileReadResult::Success:
                break;
            case filesystem::CompleteFileReadResult::Missing:
                return SessionCandidateLoadResult::Missing;
            case filesystem::CompleteFileReadResult::LimitExceeded:
                return SessionCandidateLoadResult::Corrupt;
            case filesystem::CompleteFileReadResult::OpenFailed:
            case filesystem::CompleteFileReadResult::ReadFailed:
            case filesystem::CompleteFileReadResult::UnsupportedPlatform:
            default:
                return SessionCandidateLoadResult::IoFailure;
            }
            if (referenceByteCount > std::numeric_limits<u32>::max() ||
                !DecodeSessionReference(
                    {referenceBytes, static_cast<u32>(referenceByteCount)}, reference))
                return SessionCandidateLoadResult::Corrupt;
            return SessionCandidateLoadResult::Loaded;
        }

        [[nodiscard]] SessionCandidateLoadResult TryLoadTextSessionReference(
            const SessionReference& reference) noexcept
        {
            if (!IsSessionInstanceName(reference.instance) || reference.checkpointSequence == 0 ||
                !m_textImage || !m_textDecodedScratch ||
                !m_validationImage || !m_image || m_workspace == nullptr)
                return SessionCandidateLoadResult::IoFailure;

            const filesystem::AbsolutePath canonical = m_directory.AddFilePath(reference.instance);
            char lkgName[SessionInstanceNameBytes + 5u]{};
            std::memcpy(lkgName, reference.instance, SessionInstanceNameBytes);
            std::memcpy(lkgName + SessionInstanceNameBytes, ".lkg", 5u);
            const filesystem::AbsolutePath lkg = m_directory.AddFilePath(lkgName);
            if (canonical.Empty() || lkg.Empty())
                return SessionCandidateLoadResult::Corrupt;

            auto* const decoded =
                static_cast<editor::detail::EditorUserStateTextDecodedStorage*>(
                    m_textDecodedScratch.address);
            EditorUserStateTextValidationContext validation;
            validation.expectedProject = m_workspace->GetProject().id;
            validation.expectedCheckpointSequence = reference.checkpointSequence;
            validation.decoded = decoded;
            validation.semanticScratch = m_validationImage.address;
            validation.semanticCapacity = m_validationImage.size;
            const editor::detail::EditorStateArtifactValidator validator{
                ValidateEditorUserStateTextArtifact, &validation};
            usize textByteCount = 0;
            const editor::detail::EditorStateArtifactLoadResult load =
                editor::detail::LoadEditorStateArtifact(
                    {m_directory, canonical, lkg}, m_textImage.address, m_textImage.size,
                    textByteCount, validator);
            switch (load)
            {
            case editor::detail::EditorStateArtifactLoadResult::LoadedCanonical:
            case editor::detail::EditorStateArtifactLoadResult::RecoveredLkg:
                if (validation.semanticByteCount == 0 ||
                    validation.semanticByteCount > m_image.size)
                    return SessionCandidateLoadResult::Corrupt;
                std::memcpy(m_image.address, m_validationImage.address,
                            validation.semanticByteCount);
                m_loadedByteCount = validation.semanticByteCount;
                m_status.load = editor::EditorUserStateLoadResult::Restored;
                m_status.loadedVersion = decoded->version;
                m_status.panelRecords = decoded->panelCount;
                m_status.displayRecords = decoded->displayCount;
                m_status.shortcutRecords = decoded->shortcutCount;
                m_status.preferenceRecords = decoded->preferenceCount;
                m_status.failure = {};
                m_checkpointSequence = decoded->checkpointSequence;
                m_recoveredFromLkg =
                    load == editor::detail::EditorStateArtifactLoadResult::RecoveredLkg;
                m_status.recoveredFromLastKnownGood = m_recoveredFromLkg;
                return SessionCandidateLoadResult::Loaded;
            case editor::detail::EditorStateArtifactLoadResult::Missing:
                return SessionCandidateLoadResult::Missing;
            case editor::detail::EditorStateArtifactLoadResult::Corrupt:
            case editor::detail::EditorStateArtifactLoadResult::InvalidArgument:
                return SessionCandidateLoadResult::Corrupt;
            case editor::detail::EditorStateArtifactLoadResult::IoFailure:
                return SessionCandidateLoadResult::IoFailure;
            }
            return SessionCandidateLoadResult::Corrupt;
        }

        [[nodiscard]] bool PublishSessionReference(
            const filesystem::AbsolutePath& path,
            const SessionReference* const supersededLatest = nullptr) noexcept
        {
            containers::String reference;
            if (!EncodeSessionReference(
                    m_instanceFileName, m_checkpointSequence, supersededLatest, reference))
                return false;
            char validationScratch[editor::detail::MaximumEditorStateReferenceBytes]{};
            const editor::detail::EditorStateArtifactValidator validator{
                ValidateSessionReferenceArtifact, nullptr};
            return editor::detail::PublishEditorStateReference(
                       m_projectSessionDirectory, path, reference.AsChar(), reference.Length(),
                       validationScratch, sizeof(validationScratch), m_artifactStageIdentity,
                       validator) == editor::detail::EditorStateArtifactPublishResult::Published;
        }

        void SetSaveFailureFromArtifact(
            const editor::detail::EditorStateArtifactPublishResult result) noexcept
        {
            using ArtifactResult = editor::detail::EditorStateArtifactPublishResult;
            switch (result)
            {
            case ArtifactResult::Published:
                m_status.save = editor::EditorUserStateSaveResult::Published;
                m_status.failure = {};
                return;
            case ArtifactResult::DirectoryCreateFailed:
                m_status.save = editor::EditorUserStateSaveResult::StageOpenFailed;
                m_status.failure.code = editor::EditorUserStateFailureCode::DirectoryCreateFailed;
                return;
            case ArtifactResult::StageCollisionExhausted:
            case ArtifactResult::StageOpenFailed:
                m_status.save = editor::EditorUserStateSaveResult::StageOpenFailed;
                m_status.failure.code = editor::EditorUserStateFailureCode::StageOpenFailed;
                return;
            case ArtifactResult::StageWriteFailed:
                m_status.save = editor::EditorUserStateSaveResult::StageWriteFailed;
                m_status.failure.code = editor::EditorUserStateFailureCode::StageWriteFailed;
                return;
            case ArtifactResult::StageFlushFailed:
                m_status.save = editor::EditorUserStateSaveResult::StageFlushFailed;
                m_status.failure.code = editor::EditorUserStateFailureCode::StageFlushFailed;
                return;
            case ArtifactResult::StageValidationFailed:
            case ArtifactResult::InvalidArgument:
                m_status.save = editor::EditorUserStateSaveResult::StageValidationFailed;
                m_status.failure.code = editor::EditorUserStateFailureCode::StageValidationFailed;
                return;
            case ArtifactResult::CanonicalReadFailed:
                m_status.save = editor::EditorUserStateSaveResult::ReplaceFailed;
                m_status.failure.code = editor::EditorUserStateFailureCode::CanonicalIo;
                return;
            case ArtifactResult::LkgRotateFailed:
            case ArtifactResult::ReplaceFailed:
                m_status.save = editor::EditorUserStateSaveResult::ReplaceFailed;
                m_status.failure.code = editor::EditorUserStateFailureCode::ReplaceFailed;
                return;
            case ArtifactResult::CleanupFailed:
                m_status.save = editor::EditorUserStateSaveResult::CleanupFailed;
                m_status.failure.code = editor::EditorUserStateFailureCode::CleanupFailed;
                return;
            case ArtifactResult::UnsupportedPlatform:
                m_status.save = editor::EditorUserStateSaveResult::StageOpenFailed;
                m_status.failure.code = editor::EditorUserStateFailureCode::StageOpenFailed;
                return;
            }
        }

        [[nodiscard]] SessionRestorePrepareResult PrepareLoadedRestore(const bool textState) noexcept
        {
            DiscardRestoreScratch();
            m_restoreScratch = memory::Allocate(memory::PoolId::Tools,
                                                sizeof(EditorUserStateRestoreScratch),
                                                alignof(EditorUserStateRestoreScratch));
            if (!m_restoreScratch)
                return SessionRestorePrepareResult::AllocationFailed;

            auto* const restore = ::new (m_restoreScratch.address) EditorUserStateRestoreScratch();
            restore->textState = textState;
            editor::EditorUserStateFailure decodeFailure;
            if (!editor::detail::DecodeEditorUserStateRestoreState(
                    m_image.address, m_loadedByteCount, m_workspace->GetProject().id,
                    restore->panels, editor::MaximumEditorUserStatePanels, restore->panelCount,
                    restore->displays, editor::MaximumEditorUserStateDisplays, restore->displayCount,
                    restore->imguiBytes, editor::MaximumEditorUserStateImguiBytes, restore->imguiByteCount,
                    restore->shortcuts, editor::MaximumEditorUserStateShortcuts, restore->shortcutCount,
                    restore->preferences, editor::MaximumEditorUserStatePreferences, restore->preferenceCount,
                    &decodeFailure))
            {
                m_status.load = editor::EditorUserStateLoadResult::RejectedCorrupt;
                m_status.failure = decodeFailure;
                DiscardRestoreScratch();
                return SessionRestorePrepareResult::DocumentRejected;
            }

            if (textState)
                restore->primaryWindow =
                    static_cast<editor::detail::EditorUserStateTextDecodedStorage*>(
                        m_textDecodedScratch.address)->primaryWindow;

            editor::EditorPreferenceFailure preferenceFailure;
            if (!m_shellPersistence->ValidateTypedPreferences(
                    {restore->preferences, restore->preferenceCount},
                    {restore->preferenceResults, restore->preferenceCount},
                    &preferenceFailure))
            {
                if (editor::detail::IsEditorUserStatePreferenceDocumentRejection(preferenceFailure.code))
                {
                    u32 firstInvalidPreference = 0;
                    for (u32 index = 0; index < restore->preferenceCount; ++index)
                        if (restore->preferenceResults[index] == editor::EditorTypedPreferenceItemResult::Invalid)
                        {
                            firstInvalidPreference = index;
                            break;
                        }
                    m_status.load = editor::EditorUserStateLoadResult::RejectedCorrupt;
                    m_status.failure = {editor::EditorUserStateFailureCode::CorruptDocument,
                                        PreferencesSection, firstInvalidPreference,
                                        static_cast<u32>(preferenceFailure.code)};
                    DiscardRestoreScratch();
                    return SessionRestorePrepareResult::DocumentRejected;
                }

                m_status.failure.code =
                    editor::detail::MapEditorUserStatePreferenceInvariantFailure(preferenceFailure.code);
                m_status.failure.detail = static_cast<u32>(preferenceFailure.code);
                m_status.writesInhibited = true;
                DiscardRestoreScratch();
                return SessionRestorePrepareResult::OwnerInvariantFailed;
            }

            m_restorePrepared = true;
            return SessionRestorePrepareResult::Prepared;
        }

        [[nodiscard]] bool ApplyPreparedRestoreStep(
            EditorUserStateRestoreScratch& restore,
            const editor::detail::EditorUserStateRestoreOwnerStep step) noexcept
        {
            switch (step)
            {
            case editor::detail::EditorUserStateRestoreOwnerStep::PrimaryWindow:
                if (m_forceDefaultLayout)
                    return true;
                // Monitor/backend restore is recovery input. If it cannot be repaired/applied, keep
                // the hidden default primary and continue restoring the rest of the session. Keep
                // the saved intent protected until the live primary actually changes so an
                // unrelated dirty panel cannot overwrite a good placement after a transient failure.
                m_primaryRestoreIntent = restore.primaryWindow;
                m_primaryRestoreIntentProtected =
                    restore.primaryWindow.valid &&
                    !ApplyPrimaryWindowState(restore.primaryWindow);
                if (!m_primaryRestoreIntentProtected)
                    m_primaryRestoreIntent = {};
                return true;
            case editor::detail::EditorUserStateRestoreOwnerStep::PanelInventory:
            {
                editor::detail::EditorPanelUserStateRestoreStatus panelStatus;
                editor::EditorUiFailure uiFailure;
                if (!editor::detail::RestoreEditorPanelUserState(
                        *m_uiPersistence, {restore.panels, restore.panelCount}, panelStatus, &uiFailure))
                    return false;
                m_status.panelsRestored = panelStatus.restored;
                m_status.panelsCreated = panelStatus.created;
                m_status.panelPlaceholders = panelStatus.placeholders;
                m_status.panelsSkipped = panelStatus.skipped;

                // Panel payload failures are deliberately isolated per instance/scope. Inventory
                // restore has already established the stable (type, instance) identity; now give
                // each available provider its opaque state without making one bad/missing plugin
                // capable of aborting startup. The result is retained for the next checkpoint so
                // missing/future payloads can be re-emitted byte-for-byte.
                if (restore.textState)
                {
                    auto* const decoded =
                        static_cast<editor::detail::EditorUserStateTextDecodedStorage*>(
                            m_textDecodedScratch.address);
                    if (decoded == nullptr)
                        return false;
                    for (u32 stateIndex = 0; stateIndex < decoded->panelStateCount; ++stateIndex)
                    {
                        const editor::detail::EditorUserStateTextPanelStateRecord& state =
                            decoded->panelStates[stateIndex];
                        if (m_forceDefaultLayout &&
                            state.scope == editor::EditorPanelStateScope::Presentation)
                        {
                            restore.panelStateResults[stateIndex] =
                                editor::EditorPanelStateResult::Success;
                            continue;
                        }
                        editor::EditorPanelTypeId type = editor::InvalidEditorPanelTypeId;
                        for (u32 panelIndex = 0; panelIndex < decoded->panelCount; ++panelIndex)
                            if (decoded->panels[panelIndex].instanceId == state.instanceId)
                            {
                                type = decoded->panels[panelIndex].typeId;
                                break;
                            }
                        if (type == editor::InvalidEditorPanelTypeId)
                        {
                            restore.panelStateResults[stateIndex] = editor::EditorPanelStateResult::InvalidData;
                            continue;
                        }

                        editor::EditorUiFailure panelStateFailure;
                        restore.panelStateResults[stateIndex] = editor::detail::RestoreEditorPanelPersistentState(
                            *m_uiPersistence, type, state.instanceId, state.scope, state.version,
                            {decoded->panelStateBytes + state.byteOffset, state.byteCount},
                            restore.panelStateMigrationBytes,
                            static_cast<u32>(sizeof(restore.panelStateMigrationBytes)), &panelStateFailure);
                    }
                }
                return true;
            }
            case editor::detail::EditorUserStateRestoreOwnerStep::PrivateLayout:
            {
                editor::EditorUiFailure uiFailure;
                if (m_forceDefaultLayout)
                {
                    editor::detail::EditorNamedLayoutDocumentView defaults;
                    u32 repairedPlacements = 0;
                    if (!BuildDefaultLayoutPresentationTarget(defaults) ||
                        !ApplyLayoutPresentationTarget(
                            defaults, LayoutPresentationApplyMode::CanonicalDefault,
                            repairedPlacements, uiFailure))
                        return false;
                    m_status.repairedPlacements = repairedPlacements;
                    m_defaultLayoutRebuildPending = true;
                    return true;
                }
                u32 repairedPlacements = 0;
                const editor::detail::EditorUiUserStateLayoutLoadResult layoutResult =
                    m_uiPersistence->LoadUserStateLayout(
                        {restore.imguiBytes, restore.imguiByteCount},
                        {restore.displays, restore.displayCount}, repairedPlacements, &uiFailure);
                m_status.repairedPlacements = repairedPlacements;
                if (layoutResult == editor::detail::EditorUiUserStateLayoutLoadResult::AppliedUsable)
                    return true;
                // The UI owner already resets to the default dock tree for persisted-data and
                // environment failures. Treat that as successful recovery; only live owner-phase
                // failures should escape as OwnerApplyFailed and inhibit publication.
                return editor::detail::ShouldResetEditorDockLayoutAfterUserStateLoad(
                    layoutResult, uiFailure.code);
            }
            case editor::detail::EditorUserStateRestoreOwnerStep::Shortcuts:
            {
                for (u32 begin = 0; begin < restore.shortcutCount;)
                {
                    u32 end = begin + 1u;
                    const editor::EditorCommandContextTypeId contextType =
                        restore.shortcuts[begin].contextType;
                    while (end < restore.shortcutCount &&
                           restore.shortcuts[end].contextType == contextType)
                        ++end;

                    editor::EditorCommandFailure commandFailure;
                    const editor::EditorShortcutPreferenceBatchResult batch =
                        m_commandPersistence->ApplyShortcutPreferenceBatch(
                            contextType, {restore.shortcuts + begin, end - begin},
                            {restore.shortcutResults + begin, end - begin}, &commandFailure);
                    if (batch == editor::EditorShortcutPreferenceBatchResult::Invalid)
                    {
                        m_status.shortcutsInvalid += end - begin;
                        return false;
                    }
                    if (batch == editor::EditorShortcutPreferenceBatchResult::Conflict)
                    {
                        for (u32 index = begin; index < end; ++index)
                        {
                            if (restore.shortcutResults[index] ==
                                editor::EditorShortcutPreferenceItemResult::MissingCommand)
                                ++m_status.shortcutsOrphaned;
                            else
                                ++m_status.shortcutsConflicted;
                        }
                    }
                    else
                    {
                        for (u32 index = begin; index < end; ++index)
                        {
                            switch (restore.shortcutResults[index])
                            {
                            case editor::EditorShortcutPreferenceItemResult::Applied:
                                ++m_status.shortcutsApplied;
                                break;
                            case editor::EditorShortcutPreferenceItemResult::MissingCommand:
                                ++m_status.shortcutsOrphaned;
                                break;
                            case editor::EditorShortcutPreferenceItemResult::Invalid:
                                ++m_status.shortcutsInvalid;
                                break;
                            }
                        }
                    }
                    begin = end;
                }
                return true;
            }
            case editor::detail::EditorUserStateRestoreOwnerStep::TypedPreferences:
            {
                editor::EditorPreferenceFailure preferenceFailure;
                if (!m_shellPersistence->ApplyTypedPreferences(
                        {restore.preferences, restore.preferenceCount},
                        {restore.preferenceResults, restore.preferenceCount}, &preferenceFailure))
                    return false;
                for (u32 index = 0; index < restore.preferenceCount; ++index)
                {
                    switch (restore.preferenceResults[index])
                    {
                    case editor::EditorTypedPreferenceItemResult::Valid:
                        ++m_status.preferencesApplied;
                        break;
                    case editor::EditorTypedPreferenceItemResult::MissingProvider:
                    case editor::EditorTypedPreferenceItemResult::MissingField:
                        ++m_status.preferencesOrphaned;
                        break;
                    case editor::EditorTypedPreferenceItemResult::Invalid:
                        ++m_status.preferencesInvalid;
                        break;
                    }
                }
                return true;
            }
            }
            return false;
        }

        [[nodiscard]] bool ApplyPrimaryWindowState(
            const editor::EditorUserStatePrimaryWindowRecord& saved) noexcept
        {
            if (!saved.valid)
                return true;
            if (m_window == nullptr || !saved.referenceDisplayValid ||
                (saved.maximized && saved.mode != window::WindowMode::Windowed))
                return false;

            window::DisplaySnapshot currentDisplays[window::MaximumDisplays]{};
            const u32 currentCount = m_window->GetManager().VisitDisplays(
                currentDisplays, window::MaximumDisplays);
            if (currentCount == 0)
                return false;

            editor::detail::EditorUiRepairedPlacement repaired;
            editor::EditorUserStateSavedDisplay savedReference = saved.referenceDisplay;
            savedReference.primary = true;
            const editor::detail::EditorUiPlacementResolveResult resolved =
                editor::detail::ResolveEditorUiSavedPlacement(
                    {&savedReference, 1u}, {currentDisplays, currentCount}, saved.normalRect, repaired, false);
            if (resolved != editor::detail::EditorUiPlacementResolveResult::Resolved ||
                repaired.currentDisplayIndex >= currentCount)
                return false;

            const window::WindowExtent normalExtent{
                static_cast<u32>(repaired.rect.width),
                static_cast<u32>(repaired.rect.height)};
            const window::DisplayHandle repairedDisplay =
                currentDisplays[repaired.currentDisplayIndex].handle;
            editor::EditorUserStateSavedDisplay repairedReferenceDisplay;
            if (!editor::detail::CaptureEditorUiSavedDisplay(
                    currentDisplays[repaired.currentDisplayIndex], repairedReferenceDisplay))
                return false;
            // The primary normal rectangle is now expressed in the repaired/current topology. Keep
            // its reference display in that same topology so a later checkpoint cannot pair a
            // repaired rectangle with the stale pre-repair monitor facts.
            repairedReferenceDisplay.primary = true;
            window::Failure windowFailure;
            const window::WindowHandle primary = m_window->GetPrimaryWindow();

            // Always establish the repaired normal rectangle in native windowed state first.
            // SDL ignores position/size changes after entering fullscreen, and the normal rectangle
            // is what a later Restore must return to.
            window::WindowStateRequest normalRequest;
            normalRequest.fields = window::WindowStateField::Position |
                                   window::WindowStateField::LogicalExtent |
                                   window::WindowStateField::Display |
                                   window::WindowStateField::Mode;
            normalRequest.placement.position = {repaired.rect.x, repaired.rect.y};
            normalRequest.placement.logicalExtent = normalExtent;
            normalRequest.placement.display = repairedDisplay;
            normalRequest.placement.mode = window::WindowMode::Windowed;
            if (!m_window->GetManager().RequestState(primary, normalRequest, &windowFailure))
            {
                m_status.failure.detail = static_cast<u32>(windowFailure.code);
                return false;
            }

            if (saved.mode != window::WindowMode::Windowed)
            {
                window::WindowStateRequest modeRequest;
                modeRequest.fields = window::WindowStateField::Mode;
                modeRequest.placement.mode = saved.mode;
                if (saved.mode == window::WindowMode::ExclusiveFullscreen)
                {
                    // Exclusive mode selection needs a display-mode extent, not the saved windowed
                    // rectangle. Use the current display's desktop pixels for mode selection.
                    modeRequest.fields = modeRequest.fields | window::WindowStateField::LogicalExtent;
                    modeRequest.placement.logicalExtent =
                        currentDisplays[repaired.currentDisplayIndex].desktopPixelExtent;
                }
                if (!m_window->GetManager().RequestState(primary, modeRequest, &windowFailure))
                {
                    m_status.failure.detail = static_cast<u32>(windowFailure.code);
                    return false;
                }
                if (saved.mode == window::WindowMode::ExclusiveFullscreen)
                {
                    // Preserve the requested normal extent while fullscreen. The SDL backend does
                    // not resize non-windowed windows for an extent-only request, but WindowManager
                    // retains this normal extent for the eventual return to windowed mode.
                    window::WindowStateRequest preserveNormalExtent;
                    preserveNormalExtent.fields = window::WindowStateField::LogicalExtent;
                    preserveNormalExtent.placement.logicalExtent = normalExtent;
                    if (!m_window->GetManager().RequestState(primary, preserveNormalExtent,
                                                            &windowFailure))
                    {
                        m_status.failure.detail = static_cast<u32>(windowFailure.code);
                        return false;
                    }
                }
            }
            if (saved.maximized &&
                !m_window->GetManager().RequestAction(
                    primary, window::WindowAction::Maximize, &windowFailure))
            {
                m_status.failure.detail = static_cast<u32>(windowFailure.code);
                return false;
            }

            m_primaryWindowCapture = saved;
            m_primaryWindowCapture.normalRect = repaired.rect;
            m_primaryWindowCapture.referenceDisplay = repairedReferenceDisplay;
            m_primaryWindowCapture.referenceDisplayValid = true;
            m_primaryWindowCapture.valid = true;
            return true;
        }

        [[nodiscard]] bool InitializeSourceBaselines() noexcept
        {
            if (m_uiPersistence == nullptr || m_commandPersistence == nullptr ||
                m_shellPersistence == nullptr)
            {
                m_status.writesInhibited = true;
                m_status.failure.code = editor::EditorUserStateFailureCode::InvalidArgument;
                return false;
            }
            editor::EditorPreferenceFailure preferenceFailure;
            if (!m_shellPersistence->RefreshTypedPreferenceRevision(&preferenceFailure))
            {
                m_status.writesInhibited = true;
                m_status.failure.code =
                    preferenceFailure.code == editor::EditorPreferenceFailureCode::GenerationExhausted
                        ? editor::EditorUserStateFailureCode::GenerationExhausted
                        : editor::EditorUserStateFailureCode::InvalidArgument;
                m_status.failure.detail = static_cast<u32>(preferenceFailure.code);
                return false;
            }

            editor::EditorUiFailure uiFailure;
            if (!m_uiPersistence->RefreshPanelUserStateRevision(&uiFailure))
            {
                m_status.writesInhibited = true;
                m_status.failure.code =
                    uiFailure.code == editor::EditorUiFailureCode::GenerationExhausted
                        ? editor::EditorUserStateFailureCode::GenerationExhausted
                        : editor::EditorUserStateFailureCode::InvalidArgument;
                m_status.failure.detail = static_cast<u32>(uiFailure.code);
                return false;
            }
            static_cast<void>(m_uiPersistence->ConsumeUserStateLayoutDirty(&uiFailure));
            if (uiFailure.code != editor::EditorUiFailureCode::None)
            {
                m_status.writesInhibited = true;
                m_status.failure.code = editor::EditorUserStateFailureCode::InvalidArgument;
                m_status.failure.detail = static_cast<u32>(uiFailure.code);
                return false;
            }
            const editor::detail::EditorUserStateDirtyObservation observation{
                m_uiPersistence->GetPanelUserStateRevision(),
                m_commandPersistence->GetShortcutPreferenceRevision(),
                m_shellPersistence->GetTypedPreferenceRevision(), false};
            if (!editor::detail::InstallEditorUserStateDirtyBaselines(m_dirtyTracker, observation))
            {
                m_status.writesInhibited = true;
                m_status.failure.code = editor::EditorUserStateFailureCode::GenerationExhausted;
                return false;
            }
            if (!SamplePrimaryWindow(m_lastObservedPrimaryWindow))
            {
                // Baseline sampling is observational and can transiently miss native state during
                // startup. Keep the established revision baselines and retry the primary sample on
                // the next persistence tick instead of permanently poisoning all future writes.
                m_lastObservedPrimaryWindow = m_primaryWindowCapture;
                m_primaryWindowBaselineValid = false;
            }
            else
                m_primaryWindowBaselineValid = true;
            return true;
        }

        void DiscardRestoreScratch() noexcept
        {
            if (m_restoreScratch)
            {
                static_cast<EditorUserStateRestoreScratch*>(m_restoreScratch.address)->
                    ~EditorUserStateRestoreScratch();
                memory::Free(m_restoreScratch);
                m_restoreScratch = {};
            }
            m_restorePrepared = false;
        }

        [[nodiscard]] bool CaptureCurrentDocument(
            EditorUserStateCaptureScratch& capture, editor::EditorUserStateDocumentView& document,
            editor::EditorUserStateFailure& failure) noexcept
        {
            failure = {};
            capture.panelCount = 0;
            capture.panelStateCount = 0;
            capture.panelStateByteCount = 0;
            capture.displayCount = 0;
            capture.imguiByteCount = 0;
            capture.liveShortcutCount = 0;
            capture.shortcutCount = 0;
            capture.livePreferenceCount = 0;
            capture.preferenceCount = 0;

            const EditorUserStateRestoreScratch* const restore =
                m_restorePrepared && m_restoreScratch
                    ? static_cast<const EditorUserStateRestoreScratch*>(m_restoreScratch.address)
                    : nullptr;
            const auto* const decoded = m_textDecodedScratch
                ? static_cast<const editor::detail::EditorUserStateTextDecodedStorage*>(m_textDecodedScratch.address)
                : nullptr;

            editor::EditorUiFailure uiFailure;
            if (!editor::detail::CaptureEditorPanelUserState(
                    *m_uiPersistence, capture.privatePanels, editor::MaximumEditorUserStatePanels,
                    capture.panelCount, &uiFailure))
            {
                failure.code = uiFailure.code == editor::EditorUiFailureCode::CapacityExceeded
                    ? editor::EditorUserStateFailureCode::BoundExceeded
                    : editor::EditorUserStateFailureCode::InvalidArgument;
                failure.section = PanelsSection;
                return false;
            }
            for (u32 index = 0; index < capture.panelCount; ++index)
            {
                const editor::detail::EditorPanelUserStateRecord& source =
                    capture.privatePanels[index];
                editor::EditorUserStatePanelRecord& destination = capture.panels[index];
                destination = {};
                destination.typeId = source.type;
                destination.instanceId = source.instance;
                destination.fallbackTitleByteCount = source.fallbackTitleByteCount;
                destination.open = source.open;
                std::memcpy(destination.fallbackTitle, source.fallbackTitle,
                            source.fallbackTitleByteCount);
                destination.fallbackTitle[source.fallbackTitleByteCount] = '\0';
            }

            const auto findLoadedPanelState = [&](const editor::EditorPanelTypeId type,
                                                  const editor::EditorPanelInstanceId instance,
                                                  const editor::EditorPanelStateScope scope,
                                                  u32& loadedIndex) noexcept
                -> const editor::detail::EditorUserStateTextPanelStateRecord* {
                loadedIndex = 0;
                if (decoded == nullptr)
                    return nullptr;
                u32 panelBegin = 0;
                u32 panelEnd = decoded->panelCount;
                while (panelBegin < panelEnd)
                {
                    const u32 middle = panelBegin + (panelEnd - panelBegin) / 2u;
                    if (decoded->panels[middle].instanceId < instance)
                        panelBegin = middle + 1u;
                    else
                        panelEnd = middle;
                }
                if (panelBegin >= decoded->panelCount ||
                    decoded->panels[panelBegin].instanceId != instance ||
                    decoded->panels[panelBegin].typeId != type)
                    return nullptr;
                u32 begin = 0;
                u32 end = decoded->panelStateCount;
                while (begin < end)
                {
                    const u32 middle = begin + (end - begin) / 2u;
                    const editor::detail::EditorUserStateTextPanelStateRecord& candidate =
                        decoded->panelStates[middle];
                    if (candidate.instanceId < instance ||
                        (candidate.instanceId == instance &&
                         static_cast<u8>(candidate.scope) < static_cast<u8>(scope)))
                        begin = middle + 1u;
                    else
                        end = middle;
                }
                if (begin >= decoded->panelStateCount)
                    return nullptr;
                const editor::detail::EditorUserStateTextPanelStateRecord& candidate =
                    decoded->panelStates[begin];
                if (candidate.instanceId != instance || candidate.scope != scope)
                    return nullptr;
                loadedIndex = begin;
                return &candidate;
            };
            const auto appendPanelState = [&](const editor::EditorPanelInstanceId instance,
                                              const editor::EditorPanelStateScope scope,
                                              const u32 version, const u8* const bytes,
                                              const u32 byteCount, const bool unsupportedProtected,
                                              const u32 reservedLoadedBytes) noexcept {
                if (version == 0 || byteCount > editor::MaximumEditorPanelStateBytes ||
                    capture.panelStateCount == editor::detail::MaximumEditorUserStatePanelStateRecords ||
                    reservedLoadedBytes > editor::detail::MaximumEditorUserStatePanelStateBytes - capture.panelStateByteCount ||
                    byteCount > editor::detail::MaximumEditorUserStatePanelStateBytes -
                                    capture.panelStateByteCount - reservedLoadedBytes ||
                    (byteCount != 0 && bytes == nullptr))
                    return false;
                editor::detail::EditorUserStateTextPanelStateRecord& destination =
                    capture.panelStates[capture.panelStateCount];
                destination = {instance, scope, version, capture.panelStateByteCount, byteCount};
                capture.panelStateUnsupportedProtected[capture.panelStateCount] = unsupportedProtected;
                if (byteCount != 0)
                    std::memcpy(capture.panelStateBytes + capture.panelStateByteCount, bytes, byteCount);
                capture.panelStateByteCount += byteCount;
                ++capture.panelStateCount;
                return true;
            };

            constexpr editor::EditorPanelStateScope PanelStateScopes[]{
                editor::EditorPanelStateScope::Presentation,
                editor::EditorPanelStateScope::Content};

            // Reserve aggregate arena space for every durable loaded blob that is still eligible
            // for retry/passthrough. Live provider growth may consume only the unreserved slack;
            // this prevents an earlier panel from evicting a later missing/unsupported provider's
            // opaque state solely because instance ordering reached the 2 MiB session bound first.
            u32 reservedLoadedPanelStateBytes = 0;
            for (u32 panelIndex = 0; panelIndex < capture.panelCount; ++panelIndex)
            {
                const editor::EditorUserStatePanelRecord& panel = capture.panels[panelIndex];
                for (const editor::EditorPanelStateScope scope : PanelStateScopes)
                {
                    u32 loadedIndex = 0;
                    const editor::detail::EditorUserStateTextPanelStateRecord* const loaded =
                        findLoadedPanelState(panel.typeId, panel.instanceId, scope, loadedIndex);
                    if (loaded == nullptr)
                        continue;
                    editor::EditorPanelStateResult loadedResult = editor::EditorPanelStateResult::Success;
                    if (restore != nullptr && restore->textState &&
                        loadedIndex < editor::detail::MaximumEditorUserStatePanelStateRecords)
                        loadedResult = restore->panelStateResults[loadedIndex];
                    if (loadedResult == editor::EditorPanelStateResult::InvalidData)
                        continue;
                    reservedLoadedPanelStateBytes += loaded->byteCount;
                }
            }
            for (u32 panelIndex = 0; panelIndex < capture.panelCount; ++panelIndex)
            {
                const editor::EditorUserStatePanelRecord& panel = capture.panels[panelIndex];
                for (const editor::EditorPanelStateScope scope : PanelStateScopes)
                {
                    u32 loadedIndex = 0;
                    const editor::detail::EditorUserStateTextPanelStateRecord* const loaded =
                        findLoadedPanelState(panel.typeId, panel.instanceId, scope, loadedIndex);
                    editor::EditorPanelStateResult loadedResult = editor::EditorPanelStateResult::Success;
                    if (loaded != nullptr && restore != nullptr && restore->textState &&
                        loadedIndex < editor::detail::MaximumEditorUserStatePanelStateRecords)
                        loadedResult = restore->panelStateResults[loadedIndex];

                    const editor::detail::EditorPanelPersistentStateCapture live =
                        editor::detail::CaptureEditorPanelPersistentState(
                            *m_uiPersistence, panel.typeId, panel.instanceId, scope,
                            capture.panelStateCallbackBytes,
                            static_cast<u32>(sizeof(capture.panelStateCallbackBytes)), &uiFailure);

                    const bool loadedSemanticallyUsable = loaded != nullptr &&
                        loadedResult != editor::EditorPanelStateResult::InvalidData;
                    if (loadedSemanticallyUsable)
                        reservedLoadedPanelStateBytes -= loaded->byteCount;
                    const bool unsupportedProtected = loadedSemanticallyUsable &&
                        loadedResult == editor::EditorPanelStateResult::UnsupportedVersion;

                    const u8* selectedBytes = nullptr;
                    u32 selectedByteCount = 0;
                    u32 selectedVersion = 0;
                    bool selectedUnsupportedProtected = false;
                    if (unsupportedProtected)
                    {
                        selectedBytes = decoded->panelStateBytes + loaded->byteOffset;
                        selectedByteCount = loaded->byteCount;
                        selectedVersion = loaded->version;
                        selectedUnsupportedProtected = true;
                    }
                    else if (live.result == editor::EditorPanelStateResult::Success)
                    {
                        selectedBytes = capture.panelStateCallbackBytes;
                        selectedByteCount = live.byteCount;
                        selectedVersion = live.version;
                    }
                    else if (live.result != editor::EditorPanelStateResult::NoState && loadedSemanticallyUsable)
                    {
                        selectedBytes = decoded->panelStateBytes + loaded->byteOffset;
                        selectedByteCount = loaded->byteCount;
                        selectedVersion = loaded->version;
                        selectedUnsupportedProtected = loadedResult == editor::EditorPanelStateResult::UnsupportedVersion;
                    }

                    if (selectedVersion != 0 &&
                        !appendPanelState(panel.instanceId, scope, selectedVersion, selectedBytes,
                                          selectedByteCount, selectedUnsupportedProtected,
                                          reservedLoadedPanelStateBytes))
                    {
                        // Aggregate panel-state storage is intentionally bounded independently of
                        // the per-provider 64 KiB contract. Capacity exhaustion must stay isolated
                        // to this panel scope rather than aborting the entire session checkpoint.
                        // Prefer the previously durable blob when it is smaller and still usable;
                        // otherwise omit this scope for this checkpoint and preserve all remaining
                        // session/window/layout state.
                        if (loadedSemanticallyUsable && loaded != nullptr &&
                            selectedBytes != decoded->panelStateBytes + loaded->byteOffset)
                            static_cast<void>(appendPanelState(
                                panel.instanceId, scope, loaded->version,
                                decoded->panelStateBytes + loaded->byteOffset, loaded->byteCount,
                                loadedResult == editor::EditorPanelStateResult::UnsupportedVersion,
                                reservedLoadedPanelStateBytes));
                    }
                }
            }

            if (!m_uiPersistence->CaptureUserStateLayout(
                    capture.imguiBytes, editor::MaximumEditorUserStateImguiBytes,
                    capture.imguiByteCount, capture.displays,
                    editor::MaximumEditorUserStateDisplays, capture.displayCount, &uiFailure))
            {
                failure.code = uiFailure.code == editor::EditorUiFailureCode::CapacityExceeded
                    ? editor::EditorUserStateFailureCode::BoundExceeded
                    : editor::EditorUserStateFailureCode::InvalidArgument;
                failure.section = UiStateSection;
                return false;
            }

            editor::EditorCommandFailure commandFailure;
            if (!m_commandPersistence->CaptureShortcutPreferences(
                    capture.liveShortcuts, editor::MaximumEditorUserStateShortcuts,
                    capture.liveShortcutCount, &commandFailure))
            {
                failure.code = commandFailure.code == editor::EditorCommandFailureCode::CapacityExceeded
                    ? editor::EditorUserStateFailureCode::BoundExceeded
                    : editor::EditorUserStateFailureCode::InvalidArgument;
                failure.section = ShortcutsSection;
                return false;
            }

            editor::EditorPreferenceFailure preferenceFailure;
            if (!m_shellPersistence->CaptureTypedPreferences(
                    capture.livePreferences, editor::MaximumEditorUserStatePreferences,
                    capture.livePreferenceCount, &preferenceFailure))
            {
                failure.code = preferenceFailure.code == editor::EditorPreferenceFailureCode::CapacityExceeded
                    ? editor::EditorUserStateFailureCode::BoundExceeded
                    : editor::EditorUserStateFailureCode::InvalidArgument;
                failure.section = PreferencesSection;
                failure.detail = static_cast<u32>(preferenceFailure.code);
                return false;
            }

            const containers::ArraySpan<const editor::EditorShortcutPreferenceRecord> restoredShortcuts =
                restore != nullptr
                    ? containers::ArraySpan<const editor::EditorShortcutPreferenceRecord>{restore->shortcuts,
                                                                                          restore->shortcutCount}
                    : containers::ArraySpan<const editor::EditorShortcutPreferenceRecord>{};
            const containers::ArraySpan<const editor::EditorShortcutPreferenceItemResult> restoredShortcutResults =
                restore != nullptr
                    ? containers::ArraySpan<const editor::EditorShortcutPreferenceItemResult>{restore->shortcutResults,
                                                                                              restore->shortcutCount}
                    : containers::ArraySpan<const editor::EditorShortcutPreferenceItemResult>{};
            if (!editor::detail::MergeEditorUserStateShortcutPreferences(
                    {capture.liveShortcuts, capture.liveShortcutCount}, restoredShortcuts,
                    restoredShortcutResults,
                    {capture.shortcuts, editor::MaximumEditorUserStateShortcuts}, capture.shortcutCount,
                    &failure))
                return false;

            const containers::ArraySpan<const editor::EditorTypedPreferenceRecord> restoredPreferences =
                restore != nullptr
                    ? containers::ArraySpan<const editor::EditorTypedPreferenceRecord>{restore->preferences,
                                                                                       restore->preferenceCount}
                    : containers::ArraySpan<const editor::EditorTypedPreferenceRecord>{};
            const containers::ArraySpan<const editor::EditorTypedPreferenceItemResult> restoredPreferenceResults =
                restore != nullptr
                    ? containers::ArraySpan<const editor::EditorTypedPreferenceItemResult>{restore->preferenceResults,
                                                                                           restore->preferenceCount}
                    : containers::ArraySpan<const editor::EditorTypedPreferenceItemResult>{};
            if (!editor::detail::MergeEditorUserStateTypedPreferences(
                    {capture.livePreferences, capture.livePreferenceCount}, restoredPreferences,
                    restoredPreferenceResults,
                    {capture.preferences, editor::MaximumEditorUserStatePreferences}, capture.preferenceCount,
                    &failure))
                return false;

            document = {};
            document.project = m_workspace->GetProject().id;
            document.panels = {capture.panels, capture.panelCount};
            document.displays = {capture.displays, capture.displayCount};
            document.imguiBytes = {capture.imguiBytes, capture.imguiByteCount};
            document.shortcuts = {capture.shortcuts, capture.shortcutCount};
            document.preferences = {capture.preferences, capture.preferenceCount};
            editor::EditorUserStatePrimaryWindowRecord sampledPrimary;
            if (!SamplePrimaryWindow(sampledPrimary) && !m_primaryRestoreIntentProtected)
            {
                failure.code = editor::EditorUserStateFailureCode::InvalidArgument;
                return false;
            }
            document.primaryWindow = m_primaryRestoreIntentProtected
                                         ? m_primaryRestoreIntent
                                         : sampledPrimary;
            return true;
        }

        [[nodiscard]] bool PublishCurrentState() noexcept
        {
            m_lastPayloadPublishSucceeded = false;
            if (m_status.writesInhibited)
            {
                m_status.save = editor::EditorUserStateSaveResult::WriteInhibited;
                return false;
            }
            if (!m_publicationAvailable)
            {
                m_status.save = editor::EditorUserStateSaveResult::StageOpenFailed;
                m_status.failure.code = editor::EditorUserStateFailureCode::RandomUnavailable;
                m_status.writesInhibited = true;
                return false;
            }
            if (m_artifactStageIdentity.serial == 0)
            {
                m_status.save = editor::EditorUserStateSaveResult::WriteInhibited;
                m_status.failure.code = editor::EditorUserStateFailureCode::GenerationExhausted;
                m_status.writesInhibited = true;
                return false;
            }

            if (m_latestReferencePending)
            {
                // The current canonical payload is already durable. Repair the tiny reference
                // before advancing the instance again so its one-generation LKG can never rotate
                // away while latest-checkpoint.ref still names that generation.
                m_lastPayloadPublishSucceeded = true;
                if (!PublishSessionReference(m_latestCheckpointRefPath))
                {
                    m_status.save = editor::EditorUserStateSaveResult::ReplaceFailed;
                    m_status.failure.code = editor::EditorUserStateFailureCode::ReplaceFailed;
                    return false;
                }
                m_latestReferencePending = false;
                m_status.publishedGeneration = m_pendingPayloadGeneration;
                m_status.save = editor::EditorUserStateSaveResult::Published;
                m_status.failure = {};
                if (m_status.dirtyGeneration <= m_pendingPayloadGeneration)
                    return true;
            }
            if (!m_captureScratch || !m_image || !m_validationImage || !m_textImage ||
                !m_textDecodedScratch || !m_textValidationDecodedScratch || m_workspace == nullptr)
            {
                m_status.save = editor::EditorUserStateSaveResult::StageValidationFailed;
                m_status.failure.code = editor::EditorUserStateFailureCode::InvalidArgument;
                return false;
            }

            auto& capture = *static_cast<EditorUserStateCaptureScratch*>(m_captureScratch.address);
            editor::EditorUserStateDocumentView document;
            editor::EditorUserStateFailure captureFailure;
            if (!CaptureCurrentDocument(capture, document, captureFailure))
            {
                m_status.save = editor::EditorUserStateSaveResult::StageValidationFailed;
                m_status.failure = captureFailure;
                return false;
            }

            // Keep the established binary VUSR encoder as the semantic oracle while the
            // production persistence envelope migrates to canonical text. This prevents the
            // text schema from drifting away from the existing record invariants.
            usize semanticByteCount = 0;
            editor::EditorUserStateFailure semanticFailure;
            if (!editor::EncodeEditorUserStateImage(
                    document, m_validationImage.address, m_validationImage.size,
                    semanticByteCount, &semanticFailure))
            {
                m_status.save = editor::EditorUserStateSaveResult::StageValidationFailed;
                m_status.failure = semanticFailure;
                return false;
            }

            auto* const decoded =
                static_cast<editor::detail::EditorUserStateTextDecodedStorage*>(
                    m_textDecodedScratch.address);
            editor::detail::EditorUserStateTextDocumentView textDocument = decoded->View();
            textDocument.document = document;
            textDocument.panelStates = {capture.panelStates, capture.panelStateCount};
            textDocument.panelStateBytes = {capture.panelStateBytes, capture.panelStateByteCount};
            if (m_checkpointSequence == ~u64{0})
            {
                m_status.save = editor::EditorUserStateSaveResult::WriteInhibited;
                m_status.failure.code = editor::EditorUserStateFailureCode::GenerationExhausted;
                m_status.writesInhibited = true;
                return false;
            }
            textDocument.checkpointSequence = m_checkpointSequence + 1u;
            if (textDocument.version.major != editor::detail::EditorUserStateTextVersion.major)
                textDocument.version = editor::detail::EditorUserStateTextVersion;
            editor::detail::EditorUserStateTextFailure textFailure;
            if (editor::detail::EncodeEditorUserStateText(
                    textDocument, m_textOutput, &textFailure) !=
                editor::detail::EditorUserStateTextResult::Success)
            {
                m_status.save = editor::EditorUserStateSaveResult::StageValidationFailed;
                m_status.failure.code =
                    textFailure.result == editor::detail::EditorUserStateTextResult::LimitExceeded
                        ? editor::EditorUserStateFailureCode::BoundExceeded
                        : editor::EditorUserStateFailureCode::CorruptDocument;
                m_status.failure.record = textFailure.line;
                m_status.failure.detail = textFailure.column;
                return false;
            }

            EditorUserStateTextValidationContext validation;
            validation.expectedProject = m_workspace->GetProject().id;
            validation.decoded =
                static_cast<editor::detail::EditorUserStateTextDecodedStorage*>(
                    m_textValidationDecodedScratch.address);
            validation.semanticScratch = m_validationImage.address;
            validation.semanticCapacity = m_validationImage.size;
            const editor::detail::EditorStateArtifactValidator validator{
                ValidateEditorUserStateTextArtifact, &validation};
            const editor::detail::EditorStateArtifactPublishResult published =
                editor::detail::PublishEditorStateArtifact(
                    {m_directory, m_canonicalPath, m_lkgPath},
                    m_textOutput.AsChar(), m_textOutput.Length(),
                    m_textImage.address, m_textImage.size, m_artifactStageIdentity, validator);
            if (published != editor::detail::EditorStateArtifactPublishResult::Published)
            {
                SetSaveFailureFromArtifact(published);
                return false;
            }

            // The validation decoder now contains the exact durable canonical generation. Promote
            // it to the retained source image without a multi-megabyte copy so later checkpoints
            // preserve the most recently published opaque panel bytes (not merely startup bytes)
            // if a provider disappears or becomes temporarily unavailable in this process.
            const memory::MemoryBlock previousDecoded = m_textDecodedScratch;
            m_textDecodedScratch = m_textValidationDecodedScratch;
            m_textValidationDecodedScratch = previousDecoded;

            if (m_restorePrepared && m_restoreScratch)
            {
                auto* const restore = static_cast<EditorUserStateRestoreScratch*>(m_restoreScratch.address);
                auto* const canonical =
                    static_cast<editor::detail::EditorUserStateTextDecodedStorage*>(m_textDecodedScratch.address);
                restore->textState = true;
                for (u32 index = 0; index < editor::detail::MaximumEditorUserStatePanelStateRecords; ++index)
                    restore->panelStateResults[index] = editor::EditorPanelStateResult::Success;
                for (u32 canonicalIndex = 0; canonicalIndex < canonical->panelStateCount; ++canonicalIndex)
                {
                    const editor::detail::EditorUserStateTextPanelStateRecord& state =
                        canonical->panelStates[canonicalIndex];
                    for (u32 captureIndex = 0; captureIndex < capture.panelStateCount; ++captureIndex)
                    {
                        const editor::detail::EditorUserStateTextPanelStateRecord& captured =
                            capture.panelStates[captureIndex];
                        if (captured.instanceId != state.instanceId || captured.scope != state.scope)
                            continue;
                        if (capture.panelStateUnsupportedProtected[captureIndex])
                            restore->panelStateResults[canonicalIndex] =
                                editor::EditorPanelStateResult::UnsupportedVersion;
                        break;
                    }
                }
            }
            m_checkpointSequence = textDocument.checkpointSequence;
            m_lastPayloadPublishSucceeded = true;
            m_pendingPayloadGeneration = m_status.dirtyGeneration;
            m_latestReferencePending = true;
            if (!PublishSessionReference(m_latestCheckpointRefPath))
            {
                m_status.save = editor::EditorUserStateSaveResult::ReplaceFailed;
                m_status.failure.code = editor::EditorUserStateFailureCode::ReplaceFailed;
                return false;
            }
            m_latestReferencePending = false;
            m_status.save = editor::EditorUserStateSaveResult::Published;
            m_status.publishedGeneration = m_pendingPayloadGeneration;
            m_status.failure = {};
            return true;
        }

        [[nodiscard]] bool ObserveDirtySources(const f64 nowSeconds) noexcept
        {
            editor::EditorPreferenceFailure preferenceFailure;
            if (!m_shellPersistence->RefreshTypedPreferenceRevision(&preferenceFailure))
            {
                m_status.writesInhibited = true;
                m_status.failure.code =
                    preferenceFailure.code == editor::EditorPreferenceFailureCode::GenerationExhausted
                        ? editor::EditorUserStateFailureCode::GenerationExhausted
                        : editor::EditorUserStateFailureCode::InvalidArgument;
                m_status.failure.detail = static_cast<u32>(preferenceFailure.code);
                return false;
            }

            editor::EditorUiFailure panelFailure;
            if (!m_uiPersistence->RefreshPanelUserStateRevision(&panelFailure))
            {
                m_status.writesInhibited = true;
                m_status.failure.code =
                    panelFailure.code == editor::EditorUiFailureCode::GenerationExhausted
                        ? editor::EditorUserStateFailureCode::GenerationExhausted
                        : editor::EditorUserStateFailureCode::InvalidArgument;
                m_status.failure.detail = static_cast<u32>(panelFailure.code);
                return false;
            }
            const u64 panelRevision = m_uiPersistence->GetPanelUserStateRevision();
            const u64 shortcutRevision = m_commandPersistence->GetShortcutPreferenceRevision();
            const u64 preferenceRevision = m_shellPersistence->GetTypedPreferenceRevision();
            const bool panelStateChanged = panelRevision != m_dirtyTracker.panelRevision;
            editor::EditorUserStatePrimaryWindowRecord primaryWindow;
            if (!SamplePrimaryWindow(primaryWindow))
            {
                m_status.failure.code = editor::EditorUserStateFailureCode::InvalidArgument;
                return false;
            }
            bool primaryWindowDirty = false;
            if (!m_primaryWindowBaselineValid)
            {
                // First successful observation after a transient startup miss establishes the
                // baseline; it is not evidence that the user changed the window.
                m_lastObservedPrimaryWindow = primaryWindow;
                m_primaryWindowBaselineValid = true;
            }
            else
            {
                primaryWindowDirty = !(primaryWindow == m_lastObservedPrimaryWindow);
                m_lastObservedPrimaryWindow = primaryWindow;
                if (primaryWindowDirty)
                {
                    m_primaryRestoreIntentProtected = false;
                    m_primaryRestoreIntent = {};
                    m_lastPrimaryWindowChangeRealTimeSeconds = nowSeconds;
                }
            }
            editor::EditorUiFailure uiFailure;
            const bool layoutDirty = m_uiPersistence->ConsumeUserStateLayoutDirty(&uiFailure);
            if (uiFailure.code != editor::EditorUiFailureCode::None)
            {
                m_status.writesInhibited = true;
                m_status.failure.code = editor::EditorUserStateFailureCode::InvalidArgument;
                return false;
            }
            const bool presentationMayHaveChanged =
                panelStateChanged || layoutDirty || primaryWindowDirty || m_forceSessionLayoutDirty;
            const bool observed = editor::detail::ObserveEditorUserStateDirtyState(
                m_status, m_dirtyTracker,
                {panelRevision, shortcutRevision, preferenceRevision,
                 layoutDirty || primaryWindowDirty || m_forceSessionLayoutDirty}, nowSeconds);
            if (!observed)
                return false;
            m_forceSessionLayoutDirty = false;
            if (presentationMayHaveChanged && m_activeLayoutId != 0 &&
                !RefreshActiveLayoutModified())
                VG_LOG_WARNING(diagnostics::Category::Engine,
                               "editor named-layout modified-state refresh failed");
            return true;
        }

        [[nodiscard]] engine::FrameParticipantStatus TickPersistence(const f64 nowSeconds) noexcept
        {
            if (m_defaultLayoutRebuildPending)
            {
                // One complete UI frame has elapsed since the reset cleared the live dock tree.
                // The default tree/panel placement has now been authored, so its presentation can
                // re-enter the normal dirty + canonical modified-state observation path.
                m_defaultLayoutRebuildPending = false;
                m_forceSessionLayoutDirty = true;
            }
            ProcessNamedLayoutWork(nowSeconds);
            if (m_defaultLayoutRebuildPending)
            {
                // A reset was admitted after this frame's UI authoring had already completed. The
                // live ImGui tree is intentionally empty until the next UI frame rebuilds defaults;
                // never observe or publish that transient presentation as durable user state.
                return engine::FrameParticipantStatus::Success();
            }
            if (!editor::detail::ShouldObserveEditorUserStateDirtySources(m_status) ||
                !ObserveDirtySources(nowSeconds))
                return engine::FrameParticipantStatus::Success();
            if (!editor::detail::EditorUserStateAutosaveWindowReady(
                    m_status, m_dirtyTracker, nowSeconds, m_hasAutosaveFailureTime,
                    m_lastAutosaveFailureRealTimeSeconds))
                return engine::FrameParticipantStatus::Success();

            editor::EditorUiFailure uiFailure;
            const bool settling = m_uiPersistence->IsUserStateLayoutSettling(&uiFailure);
            if (uiFailure.code != editor::EditorUiFailureCode::None)
                return engine::FrameParticipantStatus::Failure(
                    "editor user state could not observe private layout settling");
            const bool primaryWindowSettling =
                m_lastPrimaryWindowChangeRealTimeSeconds > 0.0 &&
                nowSeconds - m_lastPrimaryWindowChangeRealTimeSeconds < 2.0;
            if (!editor::detail::EditorUserStateAutosaveReadyAfterSettling(
                    true, settling || primaryWindowSettling))
                return engine::FrameParticipantStatus::Success();

            if (PublishCurrentState())
            {
                m_hasAutosaveFailureTime = false;
                return engine::FrameParticipantStatus::Success();
            }
            ++m_status.autosaveFailures;
            m_lastAutosaveFailureRealTimeSeconds = nowSeconds;
            m_hasAutosaveFailureTime = true;
            return engine::FrameParticipantStatus::Success();
        }

        static engine::FrameParticipantStatus ExecuteFrame(const engine::FrameContext& context,
                                                           void* const userData) noexcept
        {
            auto* const service = static_cast<ManagedEditorUserStateService*>(userData);
            if (service == nullptr || !service->m_admissionOpen ||
                service->m_status.state != editor::EditorUserStateServiceState::Running)
                return engine::FrameParticipantStatus::Success();
            return service->TickPersistence(context.realTimeSeconds);
        }

        [[nodiscard]] bool RevealPrimaryWindow() noexcept
        {
            if (m_window == nullptr)
                return false;
            const window::WindowHandle primary = m_window->GetPrimaryWindow();
            if (!primary.IsValid())
                return false;
            window::WindowStateRequest request;
            request.fields = window::WindowStateField::Visibility;
            request.placement.visible = true;
            request.activateWhenShown = true;
            window::Failure failure;
            if (!m_window->GetManager().RequestState(primary, request, &failure))
            {
                m_status.failure.detail = static_cast<u32>(failure.code);
                return false;
            }
            return true;
        }

        [[nodiscard]] bool SamplePrimaryWindow(editor::EditorUserStatePrimaryWindowRecord& state) noexcept
        {
            if (m_window == nullptr)
                return false;
            window::WindowSnapshot snapshot;
            if (!m_window->GetManager().GetSnapshot(m_window->GetPrimaryWindow(), snapshot))
                return false;

            const window::WindowPlacement& native = snapshot.nativeState.placement;
            const bool normalPlacementLive =
                !snapshot.nativeState.minimized && !snapshot.nativeState.maximized &&
                native.mode == window::WindowMode::Windowed;
            if (!DrainPrimaryWindowEvents(normalPlacementLive))
                return false;
            // Event loss or a mode transition can make the journal's inferred state conservative.
            // The manager snapshot is authoritative for the state after every drained event.
            m_primaryWindowJournalNormalPlacement = normalPlacementLive;
            if (!snapshot.nativeState.minimized)
            {
                m_primaryWindowCapture.mode = native.mode;
                m_primaryWindowCapture.maximized = snapshot.nativeState.maximized;
                if (normalPlacementLive)
                {
                    if (native.logicalExtent.width == 0 || native.logicalExtent.height == 0 ||
                        native.logicalExtent.width > static_cast<u32>(std::numeric_limits<i32>::max()) ||
                        native.logicalExtent.height > static_cast<u32>(std::numeric_limits<i32>::max()))
                        return false;
                    m_primaryWindowCapture.normalRect = {
                        native.position.x, native.position.y,
                        static_cast<i32>(native.logicalExtent.width),
                        static_cast<i32>(native.logicalExtent.height)};
                    window::DisplaySnapshot referenceDisplay;
                    editor::EditorUserStateSavedDisplay savedReference;
                    if (!m_window->GetManager().GetSnapshot(native.display, referenceDisplay) ||
                        !editor::detail::CaptureEditorUiSavedDisplay(referenceDisplay, savedReference))
                        return false;
                    // The dedicated reference topology contains exactly one display, so mark it
                    // primary for the shared placement resolver independent of OS primary status.
                    savedReference.primary = true;
                    m_primaryWindowCapture.referenceDisplay = savedReference;
                    m_primaryWindowCapture.referenceDisplayValid = true;
                    m_primaryWindowCapture.valid = true;
                }
            }

            // Never synthesize a normal rectangle/reference display from maximized/fullscreen
            // geometry. Those states deliberately preserve the last known windowed/restored pair.
            if (!m_primaryWindowCapture.valid || !m_primaryWindowCapture.referenceDisplayValid)
                return false;
            state = m_primaryWindowCapture;
            return true;
        }

        [[nodiscard]] bool DrainPrimaryWindowEvents(const bool currentNormalPlacement) noexcept
        {
            if (m_window == nullptr)
                return false;
            window::WindowManager& manager = m_window->GetManager();
            if (!m_primaryWindowEventCursorInitialized)
            {
                if (!manager.CreateEventCursor(window::EventCursorOrigin::NextEvent, m_primaryWindowEventCursor))
                    return false;
                m_primaryWindowEventCursorInitialized = true;
                m_primaryWindowJournalNormalPlacement = currentNormalPlacement;
                return true;
            }

            const window::WindowHandle primary = m_window->GetPrimaryWindow();
            window::WindowEvent events[64]{};
            u64 activeRevision = 0;
            editor::detail::EditorUiSavedRect candidate{};
            window::DisplayHandle candidateDisplay;
            bool haveCandidate = false;
            bool disqualified = false;
            bool journalReliable = true;
            const auto flushCandidate = [&]() noexcept {
                if (journalReliable && m_primaryWindowJournalNormalPlacement && haveCandidate &&
                    !disqualified && candidate.width > 0 && candidate.height > 0)
                {
                    window::DisplaySnapshot referenceDisplay;
                    editor::EditorUserStateSavedDisplay savedReference;
                    if (candidateDisplay.IsValid() && manager.GetSnapshot(candidateDisplay, referenceDisplay) &&
                        editor::detail::CaptureEditorUiSavedDisplay(referenceDisplay, savedReference))
                    {
                        savedReference.primary = true;
                        m_primaryWindowCapture.normalRect = candidate;
                        m_primaryWindowCapture.referenceDisplay = savedReference;
                        m_primaryWindowCapture.referenceDisplayValid = true;
                        m_primaryWindowCapture.valid = true;
                    }
                }
                haveCandidate = false;
                disqualified = false;
                candidateDisplay = {};
            };

            for (;;)
            {
                const window::WindowEventReadResult read = manager.ReadEvents(
                    m_primaryWindowEventCursor, {events, 64u});
                if (read.invalidCursor)
                    return false;
                if (read.lostEvents != 0)
                {
                    // The current snapshot below is still authoritative for the final state,
                    // but a journal gap means no event-derived normal rectangle is trustworthy.
                    haveCandidate = false;
                    disqualified = true;
                    journalReliable = false;
                }
                for (u32 index = 0; index < read.count; ++index)
                {
                    const window::WindowEvent& event = events[index];
                    if (!(event.window == primary))
                        continue;
                    if (activeRevision != 0 && event.stateRevision != activeRevision)
                        flushCandidate();
                    if (activeRevision == 0 || event.stateRevision != activeRevision)
                        activeRevision = event.stateRevision;

                    switch (event.type)
                    {
                    case window::WindowEventType::Moved:
                    case window::WindowEventType::Resized:
                        if (event.logicalExtent.width != 0 && event.logicalExtent.height != 0 &&
                            event.logicalExtent.width <= static_cast<u32>(std::numeric_limits<i32>::max()) &&
                            event.logicalExtent.height <= static_cast<u32>(std::numeric_limits<i32>::max()))
                        {
                            candidate = {event.position.x, event.position.y,
                                         static_cast<i32>(event.logicalExtent.width),
                                         static_cast<i32>(event.logicalExtent.height)};
                            candidateDisplay = event.display;
                            haveCandidate = true;
                        }
                        break;
                    case window::WindowEventType::Maximized:
                    case window::WindowEventType::Minimized:
                    case window::WindowEventType::ModeChanged:
                        disqualified = true;
                        m_primaryWindowJournalNormalPlacement = false;
                        break;
                    case window::WindowEventType::Restored:
                        // Restored is published after Moved/Resized for the same state revision, so
                        // a candidate from this revision is the new normal placement and may commit.
                        m_primaryWindowJournalNormalPlacement = true;
                        break;
                    default:
                        break;
                    }
                }
                if (read.count < 64u)
                    break;
            }
            flushCandidate();
            return true;
        }

        app::LifecycleStatus LifecycleFailure(const editor::EditorUserStateFailureCode code, const char* const message) noexcept
        {
            m_status.failure.code = code;
            m_status.state = editor::EditorUserStateServiceState::Failed;
            return app::LifecycleStatus::Failure(message);
        }

        void Reset() noexcept
        {
            if (m_image)
                memory::Free(m_image);
            m_image = {};
            if (m_validationImage)
                memory::Free(m_validationImage);
            m_validationImage = {};
            if (m_textImage)
                memory::Free(m_textImage);
            m_textImage = {};
            if (m_textDecodedScratch)
            {
                static_cast<editor::detail::EditorUserStateTextDecodedStorage*>(
                    m_textDecodedScratch.address)->~EditorUserStateTextDecodedStorage();
                memory::Free(m_textDecodedScratch);
            }
            m_textDecodedScratch = {};
            if (m_textValidationDecodedScratch)
            {
                static_cast<editor::detail::EditorUserStateTextDecodedStorage*>(
                    m_textValidationDecodedScratch.address)->~EditorUserStateTextDecodedStorage();
                memory::Free(m_textValidationDecodedScratch);
            }
            m_textValidationDecodedScratch = {};
            m_textOutput.Clear();
            if (m_captureScratch)
            {
                static_cast<EditorUserStateCaptureScratch*>(m_captureScratch.address)->
                    ~EditorUserStateCaptureScratch();
                memory::Free(m_captureScratch);
            }
            m_captureScratch = {};
            if (m_defaultLayoutCaptureScratch)
            {
                static_cast<EditorUserStateCaptureScratch*>(m_defaultLayoutCaptureScratch.address)->
                    ~EditorUserStateCaptureScratch();
                memory::Free(m_defaultLayoutCaptureScratch);
            }
            m_defaultLayoutCaptureScratch = {};
            if (m_namedLayoutDecodedScratch)
            {
                static_cast<editor::detail::EditorNamedLayoutTextDecodedStorage*>(
                    m_namedLayoutDecodedScratch.address)->~EditorNamedLayoutTextDecodedStorage();
                memory::Free(m_namedLayoutDecodedScratch);
            }
            m_namedLayoutDecodedScratch = {};
            if (m_namedLayoutValidationDecodedScratch)
            {
                static_cast<editor::detail::EditorNamedLayoutTextDecodedStorage*>(
                    m_namedLayoutValidationDecodedScratch.address)->~EditorNamedLayoutTextDecodedStorage();
                memory::Free(m_namedLayoutValidationDecodedScratch);
            }
            m_namedLayoutValidationDecodedScratch = {};
            if (m_restoreScratch)
            {
                static_cast<EditorUserStateRestoreScratch*>(m_restoreScratch.address)->~EditorUserStateRestoreScratch();
                memory::Free(m_restoreScratch);
            }
            m_restoreScratch = {};
            m_framePipeline = nullptr;
            m_window = nullptr;
            m_workspace = nullptr;
            m_uiPersistence = nullptr;
            m_commandPersistence = nullptr;
            m_shellPersistence = nullptr;
            m_projectSessionDirectory = {};
            m_directory = {};
            m_canonicalPath = {};
            m_lkgPath = {};
            m_latestCheckpointRefPath = {};
            m_lastSessionRefPath = {};
            m_layoutDirectory = {};
            m_layoutIndexPath = {};
            m_layoutIndexLkgPath = {};
            m_loadedByteCount = 0;
            m_artifactStageIdentity = {};
            m_checkpointSequence = 0;
            m_lastPayloadPublishSucceeded = false;
            m_latestReferencePending = false;
            m_pendingPayloadGeneration = 0;
            std::memset(m_instanceFileName, 0, sizeof(m_instanceFileName));
            m_publicationAvailable = false;
            m_admissionOpen = false;
            m_restorePrepared = false;
            m_dirtyTracker = {};
            m_lastAutosaveFailureRealTimeSeconds = 0.0;
            m_hasAutosaveFailureTime = false;
            m_lastPrimaryWindowChangeRealTimeSeconds = 0.0;
            m_primaryWindowEventCursor = {};
            m_primaryWindowEventCursorInitialized = false;
            m_primaryWindowJournalNormalPlacement = false;
            m_primaryWindowBaselineValid = false;
            m_primaryWindowCapture = {};
            m_lastObservedPrimaryWindow = {};
            m_primaryRestoreIntent = {};
            m_primaryRestoreIntentProtected = false;
            m_recoveredFromLkg = false;
            m_namedLayoutIndex = {};
            m_namedLayoutIndex.version = editor::detail::EditorNamedLayoutIndexTextVersion;
            m_namedLayoutIndexValidation = {};
            m_namedLayoutPending = {};
            m_namedLayoutLeaseBusy = false;
            m_namedLayoutDialogOutstanding = false;
            m_namedLayoutCommandsRegistered = false;
            for (auto& token : m_namedLayoutCommandTokens)
                token = {};
            for (auto& token : m_namedLayoutHandlerTokens)
                token = {};
            for (auto& binding : m_namedLayoutCommandBindings)
                binding = {};
            m_activeLayoutModified = false;
            m_activeLayoutId = 0;
            m_forceSessionLayoutDirty = false;
            m_defaultLayoutRebuildPending = false;
            m_defaultLayoutCaptured = false;
            m_defaultPrimaryWindow = {};
            m_layoutBackupPrimaryWindow = {};
            m_status = {};
        }

        engine::FramePipelineService* m_framePipeline = nullptr;
        engine::WindowService* m_window = nullptr;
        editor::ProjectWorkspaceService* m_workspace = nullptr;
        editor::detail::EditorUiPersistencePrivateService* m_uiPersistence = nullptr;
        editor::detail::EditorCommandPersistencePrivateService* m_commandPersistence = nullptr;
        editor::detail::EditorShellPersistencePrivateService* m_shellPersistence = nullptr;
        memory::MemoryBlock m_image;
        memory::MemoryBlock m_validationImage;
        memory::MemoryBlock m_textImage;
        memory::MemoryBlock m_textDecodedScratch;
        memory::MemoryBlock m_textValidationDecodedScratch;
        memory::MemoryBlock m_captureScratch;
        memory::MemoryBlock m_defaultLayoutCaptureScratch;
        memory::MemoryBlock m_namedLayoutDecodedScratch;
        memory::MemoryBlock m_namedLayoutValidationDecodedScratch;
        memory::MemoryBlock m_restoreScratch;
        containers::String m_textOutput;
        filesystem::AbsolutePath m_projectSessionDirectory;
        filesystem::AbsolutePath m_directory;
        filesystem::AbsolutePath m_canonicalPath;
        filesystem::AbsolutePath m_lkgPath;
        filesystem::AbsolutePath m_latestCheckpointRefPath;
        filesystem::AbsolutePath m_lastSessionRefPath;
        filesystem::AbsolutePath m_layoutDirectory;
        filesystem::AbsolutePath m_layoutIndexPath;
        filesystem::AbsolutePath m_layoutIndexLkgPath;
        usize m_loadedByteCount = 0;
        editor::detail::EditorStateArtifactStageIdentity m_artifactStageIdentity;
        u64 m_checkpointSequence = 0;
        bool m_lastPayloadPublishSucceeded = false;
        bool m_latestReferencePending = false;
        u64 m_pendingPayloadGeneration = 0;
        char m_instanceFileName[SessionInstanceNameBytes + 1u]{};
        bool m_publicationAvailable = false;
        bool m_admissionOpen = false;
        bool m_restorePrepared = false;
        bool m_recoveredFromLkg = false;
        editor::detail::EditorUserStateDirtyTracker m_dirtyTracker;
        f64 m_lastAutosaveFailureRealTimeSeconds = 0.0;
        bool m_hasAutosaveFailureTime = false;
        f64 m_lastPrimaryWindowChangeRealTimeSeconds = 0.0;
        window::WindowEventCursor m_primaryWindowEventCursor;
        bool m_primaryWindowEventCursorInitialized = false;
        bool m_primaryWindowJournalNormalPlacement = false;
        bool m_primaryWindowBaselineValid = false;
        editor::EditorUserStatePrimaryWindowRecord m_primaryWindowCapture;
        editor::EditorUserStatePrimaryWindowRecord m_lastObservedPrimaryWindow;
        editor::EditorUserStatePrimaryWindowRecord m_primaryRestoreIntent;
        bool m_primaryRestoreIntentProtected = false;
        editor::detail::EditorNamedLayoutIndexDecodedStorage m_namedLayoutIndex;
        editor::detail::EditorNamedLayoutIndexDecodedStorage m_namedLayoutIndexValidation;
        NamedLayoutRequest m_namedLayoutPending;
        bool m_namedLayoutLeaseBusy = false;
        NamedLayoutCommandBinding m_namedLayoutCommandBindings[8]{};
        editor::EditorCommandToken m_namedLayoutCommandTokens[8]{};
        editor::EditorCommandHandlerToken m_namedLayoutHandlerTokens[8]{};
        editor::EditorUserStatePrimaryWindowRecord m_defaultPrimaryWindow;
        editor::EditorUserStatePrimaryWindowRecord m_layoutBackupPrimaryWindow;
        u64 m_activeLayoutId = 0;
        bool m_namedLayoutDialogOutstanding = false;
        bool m_namedLayoutCommandsRegistered = false;
        bool m_activeLayoutModified = false;
        bool m_forceSessionLayoutDirty = false;
        bool m_defaultLayoutRebuildPending = false;
        bool m_defaultLayoutCaptured = false;
        const bool m_forceDefaultLayout = false;
        editor::EditorUserStateStatus m_status;
    };

    app::Service* CreateEditorUserStateService(void* const userData) noexcept
    {
        memory::MemoryBlock block = memory::Allocate(memory::PoolId::Tools, sizeof(ManagedEditorUserStateService), alignof(ManagedEditorUserStateService));
        return block ? ::new (block.address) ManagedEditorUserStateService(
                           static_cast<const editor::EditorUserStateServiceConfig*>(userData))
                     : nullptr;
    }

    void DestroyEditorUserStateService(app::Service* const service, void*) noexcept
    {
        if (service == nullptr)
            return;
        static_cast<ManagedEditorUserStateService*>(service)->~ManagedEditorUserStateService();
        memory::MemoryBlock block{service, sizeof(ManagedEditorUserStateService), memory::PoolId::Tools};
        memory::Free(block);
    }
} // namespace

namespace vanguard::editor
{
    bool RegisterEditorUserStateService(application::EngineHost& host,
                                        const EditorUserStateServiceConfig& config,
                                        application::HostFailure* const failure) noexcept
    {
        constexpr application::ServiceDependency dependencies[]{
            {engine::FramePipelineServiceId, application::DependencyKind::Required},
            {engine::FilesystemServiceId, application::DependencyKind::Required},
            {engine::WindowServiceId, application::DependencyKind::Required},
            {ProjectWorkspaceServiceId, application::DependencyKind::Required},
            {EditorUiServiceId, application::DependencyKind::Required},
            {EditorCommandServiceId, application::DependencyKind::Required},
            {EditorShellServiceId, application::DependencyKind::Required},
            // SceneView owns the built-in Scene panel type. It must be live before persisted panel restore so a
            // valid Scene record resolves to the production type instead of becoming an unresolved placeholder.
            {EditorSceneViewServiceId, application::DependencyKind::Required}};
        constexpr application::CapabilityId capabilities[]{EditorUserStateCapabilityId};
        application::ServiceDescriptor descriptor;
        descriptor.id = EditorUserStateServiceId;
        descriptor.name = "editorUserState";
        descriptor.profiles = application::ApplicationProfile::Editor | application::ApplicationProfile::Tool;
        descriptor.scope = application::ServiceScope::EditorWorkspace;
        descriptor.affinity = application::ThreadAffinity::MainThread;
        descriptor.dependencies = dependencies;
        descriptor.provides = capabilities;
        descriptor.create = CreateEditorUserStateService;
        descriptor.destroy = DestroyEditorUserStateService;
        descriptor.userData = const_cast<EditorUserStateServiceConfig*>(&config);
        return host.RegisterService(EditorModuleId, descriptor, failure);
    }

    EditorUserStateService* FindEditorUserStateService(application::EngineHost& host) noexcept
    {
        return static_cast<EditorUserStateService*>(host.FindCapability(EditorUserStateCapabilityId));
    }

    EditorUserStateService* FindEditorUserStateService(application::ServiceContext& context) noexcept
    {
        return static_cast<EditorUserStateService*>(context.FindCapability(EditorUserStateCapabilityId));
    }
} // namespace vanguard::editor
