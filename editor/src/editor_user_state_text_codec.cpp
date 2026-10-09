#include <vanguard/editor/editor_user_state_text_codec.hpp>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>

namespace
{
    using namespace vanguard;
    namespace ctr = vanguard::containers;
    namespace editor = vanguard::editor;
    namespace detail = vanguard::editor::detail;
    namespace serialization = vanguard::serialization;

    constexpr u32 PanelFieldType = 1u << 0u;
    constexpr u32 PanelFieldOpen = 1u << 1u;
    constexpr u32 PanelFieldTitle = 1u << 2u;
    constexpr u32 PanelFieldMask = PanelFieldType | PanelFieldOpen | PanelFieldTitle;
    constexpr u32 PanelStateFieldVersion = 1u << 0u;
    constexpr u32 PanelStateFieldBytes = 1u << 1u;
    constexpr u32 PanelStateFieldMask = PanelStateFieldVersion | PanelStateFieldBytes;

    constexpr u32 DisplayFieldPrimary = 1u << 0u;
    constexpr u32 DisplayFieldBoundsX = 1u << 1u;
    constexpr u32 DisplayFieldBoundsY = 1u << 2u;
    constexpr u32 DisplayFieldBoundsWidth = 1u << 3u;
    constexpr u32 DisplayFieldBoundsHeight = 1u << 4u;
    constexpr u32 DisplayFieldWorkX = 1u << 5u;
    constexpr u32 DisplayFieldWorkY = 1u << 6u;
    constexpr u32 DisplayFieldWorkWidth = 1u << 7u;
    constexpr u32 DisplayFieldWorkHeight = 1u << 8u;
    constexpr u32 DisplayFieldDesktopWidth = 1u << 9u;
    constexpr u32 DisplayFieldDesktopHeight = 1u << 10u;
    constexpr u32 DisplayFieldScale = 1u << 11u;
    constexpr u32 DisplayFieldMask = (1u << 12u) - 1u;

    constexpr u32 ShortcutFieldMode = 1u << 0u;
    constexpr u32 ShortcutFieldKeyCode = 1u << 1u;
    constexpr u32 ShortcutFieldModifiers = 1u << 2u;

    constexpr u32 PreferenceFieldType = 1u << 0u;
    constexpr u32 PreferenceFieldValue = 1u << 1u;
    constexpr u32 PreferenceFieldMask = PreferenceFieldType | PreferenceFieldValue;

    constexpr u32 PrimaryFieldNormalX = 1u << 0u;
    constexpr u32 PrimaryFieldNormalY = 1u << 1u;
    constexpr u32 PrimaryFieldNormalWidth = 1u << 2u;
    constexpr u32 PrimaryFieldNormalHeight = 1u << 3u;
    constexpr u32 PrimaryFieldMode = 1u << 4u;
    constexpr u32 PrimaryFieldMaximized = 1u << 5u;
    constexpr u32 PrimaryFieldDisplayBoundsX = 1u << 6u;
    constexpr u32 PrimaryFieldDisplayBoundsY = 1u << 7u;
    constexpr u32 PrimaryFieldDisplayBoundsWidth = 1u << 8u;
    constexpr u32 PrimaryFieldDisplayBoundsHeight = 1u << 9u;
    constexpr u32 PrimaryFieldDisplayWorkX = 1u << 10u;
    constexpr u32 PrimaryFieldDisplayWorkY = 1u << 11u;
    constexpr u32 PrimaryFieldDisplayWorkWidth = 1u << 12u;
    constexpr u32 PrimaryFieldDisplayWorkHeight = 1u << 13u;
    constexpr u32 PrimaryFieldDisplayDesktopWidth = 1u << 14u;
    constexpr u32 PrimaryFieldDisplayDesktopHeight = 1u << 15u;
    constexpr u32 PrimaryFieldDisplayScale = 1u << 16u;
    constexpr u32 PrimaryLegacyFieldMask = (1u << 6u) - 1u;
    constexpr u32 PrimaryFieldMask = (1u << 17u) - 1u;

    void SetFailure(detail::EditorUserStateTextFailure* const failure,
                    const detail::EditorUserStateTextResult result, const u32 line = 0,
                    const u32 column = 0) noexcept
    {
        if (failure != nullptr)
            *failure = {result, line, column};
    }

    [[nodiscard]] bool IsTextValueRepresentable(const ctr::StringView value) noexcept
    {
        return serialization::IsValidUtf8(value);
    }

    [[nodiscard]] bool ParseBool(const ctr::StringView text, bool& value) noexcept
    {
        if (text == "true")
        {
            value = true;
            return true;
        }
        if (text == "false")
        {
            value = false;
            return true;
        }
        return false;
    }

    [[nodiscard]] bool ParseU64(const ctr::StringView text, u64& value) noexcept
    {
        if (text.Empty())
            return false;
        u64 parsed = 0;
        for (const char digit : text)
        {
            if (digit < '0' || digit > '9')
                return false;
            const u64 next = static_cast<u64>(digit - '0');
            if (parsed > (std::numeric_limits<u64>::max() - next) / 10u)
                return false;
            parsed = parsed * 10u + next;
        }
        value = parsed;
        return true;
    }

    [[nodiscard]] bool ParseI64(const ctr::StringView text, i64& value) noexcept
    {
        if (text.Empty())
            return false;
        const bool negative = text.Front() == '-';
        const ctr::StringView magnitudeText = negative ? text.SubView(1u) : text;
        if (magnitudeText.Empty())
            return false;
        u64 magnitude = 0;
        if (!ParseU64(magnitudeText, magnitude))
            return false;
        constexpr u64 MaximumPositive = static_cast<u64>(std::numeric_limits<i64>::max());
        constexpr u64 MaximumNegativeMagnitude = MaximumPositive + 1u;
        if ((!negative && magnitude > MaximumPositive) || (negative && magnitude > MaximumNegativeMagnitude))
            return false;
        if (!negative)
            value = static_cast<i64>(magnitude);
        else if (magnitude == MaximumNegativeMagnitude)
            value = std::numeric_limits<i64>::min();
        else
            value = -static_cast<i64>(magnitude);
        return true;
    }

    [[nodiscard]] bool ParseU32(const ctr::StringView text, u32& value) noexcept
    {
        u64 parsed = 0;
        if (!ParseU64(text, parsed) || parsed > std::numeric_limits<u32>::max())
            return false;
        value = static_cast<u32>(parsed);
        return true;
    }

    [[nodiscard]] bool ParseI32(const ctr::StringView text, i32& value) noexcept
    {
        i64 parsed = 0;
        if (!ParseI64(text, parsed) || parsed < std::numeric_limits<i32>::min() ||
            parsed > std::numeric_limits<i32>::max())
            return false;
        value = static_cast<i32>(parsed);
        return true;
    }

    [[nodiscard]] bool ParseU16(const ctr::StringView text, u16& value) noexcept
    {
        u64 parsed = 0;
        if (!ParseU64(text, parsed) || parsed > std::numeric_limits<u16>::max())
            return false;
        value = static_cast<u16>(parsed);
        return true;
    }

    [[nodiscard]] bool ParseU8(const ctr::StringView text, u8& value) noexcept
    {
        u64 parsed = 0;
        if (!ParseU64(text, parsed) || parsed > std::numeric_limits<u8>::max())
            return false;
        value = static_cast<u8>(parsed);
        return true;
    }

    [[nodiscard]] bool ParseFixedHex16(const ctr::StringView text, u64& value) noexcept
    {
        if (text.Length() != 16u)
            return false;
        u64 parsed = 0;
        for (const char digit : text)
        {
            u64 nibble = 0;
            if (digit >= '0' && digit <= '9')
                nibble = static_cast<u64>(digit - '0');
            else if (digit >= 'a' && digit <= 'f')
                nibble = static_cast<u64>(digit - 'a') + 10u;
            else
                return false;
            parsed = (parsed << 4u) | nibble;
        }
        value = parsed;
        return true;
    }

    void FormatFixedHex16(const u64 value, char (&text)[17]) noexcept
    {
        constexpr char Hex[] = "0123456789abcdef";
        for (u32 index = 0; index < 16u; ++index)
        {
            const u32 shift = (15u - index) * 4u;
            text[index] = Hex[(value >> shift) & 0x0fu];
        }
        text[16] = '\0';
    }

    [[nodiscard]] ctr::StringView FormatU64(const u64 value, char (&buffer)[21]) noexcept
    {
        char reverse[20]{};
        u32 count = 0;
        u64 remaining = value;
        do
        {
            reverse[count++] = static_cast<char>('0' + (remaining % 10u));
            remaining /= 10u;
        } while (remaining != 0);
        for (u32 index = 0; index < count; ++index)
            buffer[index] = reverse[count - index - 1u];
        buffer[count] = '\0';
        return {buffer, count};
    }

    [[nodiscard]] ctr::StringView FormatI64(const i64 value, char (&buffer)[22]) noexcept
    {
        if (value >= 0)
        {
            char unsignedBuffer[21]{};
            const ctr::StringView formatted = FormatU64(static_cast<u64>(value), unsignedBuffer);
            std::memcpy(buffer, formatted.Data(), formatted.Length());
            buffer[formatted.Length()] = '\0';
            return {buffer, formatted.Length()};
        }
        const u64 magnitude = value == std::numeric_limits<i64>::min()
                                ? static_cast<u64>(std::numeric_limits<i64>::max()) + 1u
                                : static_cast<u64>(-value);
        char unsignedBuffer[21]{};
        const ctr::StringView formatted = FormatU64(magnitude, unsignedBuffer);
        buffer[0] = '-';
        std::memcpy(buffer + 1u, formatted.Data(), formatted.Length());
        buffer[formatted.Length() + 1u] = '\0';
        return {buffer, formatted.Length() + 1u};
    }

    [[nodiscard]] ctr::StringView FormatU32(const u32 value, char (&buffer)[21]) noexcept
    {
        return FormatU64(value, buffer);
    }

    [[nodiscard]] ctr::StringView FormatI32(const i32 value, char (&buffer)[22]) noexcept
    {
        return FormatI64(value, buffer);
    }

    [[nodiscard]] bool ParsePanelParentKey(const ctr::StringView key, u64& instanceId,
                                           ctr::StringView& suffix) noexcept
    {
        constexpr ctr::StringView Prefix = "panel.";
        if (!key.StartsWith(Prefix) || key.Length() <= Prefix.Length() + 17u)
            return false;
        const ctr::StringView id = key.SubView(Prefix.Length(), 16u);
        if (!ParseFixedHex16(id, instanceId) || key[Prefix.Length() + 16u] != '.')
            return false;
        suffix = key.SubView(Prefix.Length() + 17u);
        return !suffix.Empty();
    }

    [[nodiscard]] bool ParsePanelKey(const ctr::StringView key, u64& instanceId,
                                     ctr::StringView& field) noexcept
    {
        if (!ParsePanelParentKey(key, instanceId, field))
            return false;
        return field == "type" || field == "open" || field == "title";
    }

    [[nodiscard]] bool ParsePanelStateKey(const ctr::StringView key, u64& instanceId,
                                          editor::EditorPanelStateScope& scope,
                                          ctr::StringView& field) noexcept
    {
        ctr::StringView suffix;
        if (!ParsePanelParentKey(key, instanceId, suffix))
            return false;
        constexpr ctr::StringView Presentation = "state.presentation.";
        constexpr ctr::StringView Content = "state.content.";
        if (suffix.StartsWith(Presentation))
        {
            scope = editor::EditorPanelStateScope::Presentation;
            field = suffix.SubView(Presentation.Length());
        }
        else if (suffix.StartsWith(Content))
        {
            scope = editor::EditorPanelStateScope::Content;
            field = suffix.SubView(Content.Length());
        }
        else
            return false;
        return field == "version" || field == "bytes" || field == "chunk";
    }

    [[nodiscard]] bool DecodeHexChunk(const ctr::StringView text, u8* const destination,
                                      const u32 capacity, u32& byteCount) noexcept
    {
        byteCount = 0;
        if (text.Empty() || (text.Length() & 1u) != 0 ||
            text.Length() > detail::EditorUserStatePanelStateChunkBytes * 2u)
            return false;
        const u32 required = text.Length() / 2u;
        if (required > capacity || destination == nullptr)
            return false;
        const auto nibble = [](const char value, u8& output) noexcept {
            if (value >= '0' && value <= '9')
                output = static_cast<u8>(value - '0');
            else if (value >= 'a' && value <= 'f')
                output = static_cast<u8>(value - 'a' + 10);
            else if (value >= 'A' && value <= 'F')
                output = static_cast<u8>(value - 'A' + 10);
            else
                return false;
            return true;
        };
        for (u32 index = 0; index < required; ++index)
        {
            u8 high = 0;
            u8 low = 0;
            if (!nibble(text[index * 2u], high) || !nibble(text[index * 2u + 1u], low))
                return false;
            destination[index] = static_cast<u8>((high << 4u) | low);
        }
        byteCount = required;
        return true;
    }

    [[nodiscard]] ctr::StringView FormatHexChunk(const u8* const bytes, const u32 byteCount,
                                                 char (&buffer)[detail::EditorUserStatePanelStateChunkBytes * 2u + 1u]) noexcept
    {
        constexpr char Hex[] = "0123456789abcdef";
        for (u32 index = 0; index < byteCount; ++index)
        {
            buffer[index * 2u] = Hex[bytes[index] >> 4u];
            buffer[index * 2u + 1u] = Hex[bytes[index] & 0x0fu];
        }
        buffer[byteCount * 2u] = '\0';
        return {buffer, byteCount * 2u};
    }

    [[nodiscard]] bool ParseDisplayKey(const ctr::StringView key, u32& index,
                                       ctr::StringView& field) noexcept
    {
        constexpr ctr::StringView Prefix = "display.";
        if (!key.StartsWith(Prefix) || key.Length() <= Prefix.Length() + 3u)
            return false;
        const char tens = key[Prefix.Length()];
        const char ones = key[Prefix.Length() + 1u];
        if (tens < '0' || tens > '9' || ones < '0' || ones > '9' || key[Prefix.Length() + 2u] != '.')
            return false;
        index = static_cast<u32>(tens - '0') * 10u + static_cast<u32>(ones - '0');
        if (index >= editor::MaximumEditorUserStateDisplays)
            return false;
        field = key.SubView(Prefix.Length() + 3u);
        return field == "primary" || field == "bounds.x" || field == "bounds.y" ||
               field == "bounds.width" || field == "bounds.height" || field == "work.x" ||
               field == "work.y" || field == "work.width" || field == "work.height" ||
               field == "desktop.width" || field == "desktop.height" || field == "scale-q16-16";
    }

    [[nodiscard]] bool ParseShortcutKey(const ctr::StringView key, u64& contextId, u64& commandId,
                                        ctr::StringView& field) noexcept
    {
        constexpr ctr::StringView Prefix = "shortcut.";
        if (!key.StartsWith(Prefix) || key.Length() <= Prefix.Length() + 34u)
            return false;
        if (!ParseFixedHex16(key.SubView(Prefix.Length(), 16u), contextId) ||
            key[Prefix.Length() + 16u] != '.' ||
            !ParseFixedHex16(key.SubView(Prefix.Length() + 17u, 16u), commandId) ||
            key[Prefix.Length() + 33u] != '.')
            return false;
        field = key.SubView(Prefix.Length() + 34u);
        return field == "mode" || field == "key-code" || field == "modifiers";
    }

    [[nodiscard]] bool ParsePreferenceKey(const ctr::StringView key, u64& extensionId, u64& providerId,
                                          u64& fieldId, ctr::StringView& field) noexcept
    {
        constexpr ctr::StringView Prefix = "preference.";
        if (!key.StartsWith(Prefix) || key.Length() <= Prefix.Length() + 51u)
            return false;
        if (!ParseFixedHex16(key.SubView(Prefix.Length(), 16u), extensionId) ||
            key[Prefix.Length() + 16u] != '.' ||
            !ParseFixedHex16(key.SubView(Prefix.Length() + 17u, 16u), providerId) ||
            key[Prefix.Length() + 33u] != '.' ||
            !ParseFixedHex16(key.SubView(Prefix.Length() + 34u, 16u), fieldId) ||
            key[Prefix.Length() + 50u] != '.')
            return false;
        field = key.SubView(Prefix.Length() + 51u);
        return field == "type" || field == "value";
    }

    [[nodiscard]] int CompareDisplay(const editor::EditorUserStateSavedDisplay& left,
                                     const editor::EditorUserStateSavedDisplay& right) noexcept
    {
        if (left.primary != right.primary)
            return left.primary ? -1 : 1;
#define VG_TEXT_COMPARE_DISPLAY(field) \
        if (left.field != right.field)  \
            return left.field < right.field ? -1 : 1
        VG_TEXT_COMPARE_DISPLAY(boundsX);
        VG_TEXT_COMPARE_DISPLAY(boundsY);
        VG_TEXT_COMPARE_DISPLAY(boundsWidth);
        VG_TEXT_COMPARE_DISPLAY(boundsHeight);
        VG_TEXT_COMPARE_DISPLAY(workX);
        VG_TEXT_COMPARE_DISPLAY(workY);
        VG_TEXT_COMPARE_DISPLAY(workWidth);
        VG_TEXT_COMPARE_DISPLAY(workHeight);
        VG_TEXT_COMPARE_DISPLAY(desktopPixelWidth);
        VG_TEXT_COMPARE_DISPLAY(desktopPixelHeight);
        VG_TEXT_COMPARE_DISPLAY(scaleQ16_16);
#undef VG_TEXT_COMPARE_DISPLAY
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

    [[nodiscard]] bool AppendKnownAssignment(ctr::String& destination, const ctr::StringView key,
                                             const ctr::StringView value) noexcept
    {
        if (!serialization::IsValidTextKey(key) || !serialization::IsValidUtf8(value))
            return false;

        bool extendedControlEscape = false;
        for (const char byte : value)
            if (static_cast<u8>(byte) < 0x20u && byte != '\n' && byte != '\r' && byte != '\t')
            {
                extendedControlEscape = true;
                break;
            }
        if (!extendedControlEscape)
            return serialization::AppendTextAssignment(destination, key, value) == serialization::Result::Success &&
                   destination.Length() <= detail::MaximumEditorUserStateTextBytes;

        u64 additional = static_cast<u64>(key.Length()) + 6u;
        for (const char byte : value)
        {
            const u8 valueByte = static_cast<u8>(byte);
            if (byte == '"' || byte == '\\' || byte == '\n' || byte == '\r' || byte == '\t')
                additional += 2u;
            else if (valueByte < 0x20u)
                additional += 6u;
            else
                ++additional;
        }
        if (destination.Length() > detail::MaximumEditorUserStateTextBytes ||
            additional > detail::MaximumEditorUserStateTextBytes - destination.Length())
            return false;
        if (!destination.Reserve(destination.Length() + static_cast<u32>(additional)))
            return false;

        constexpr char Hex[] = "0123456789abcdef";
        destination.Append(key);
        destination.Append(" = \"", 4u);
        for (const char byte : value)
        {
            const u8 valueByte = static_cast<u8>(byte);
            if (byte == '"' || byte == '\\')
            {
                destination.Append('\\');
                destination.Append(byte);
            }
            else if (byte == '\n')
                destination.Append("\\n", 2u);
            else if (byte == '\r')
                destination.Append("\\r", 2u);
            else if (byte == '\t')
                destination.Append("\\t", 2u);
            else if (valueByte < 0x20u)
            {
                char escaped[]{'\\', 'u', '0', '0', Hex[(valueByte >> 4u) & 0x0fu], Hex[valueByte & 0x0fu]};
                destination.Append(escaped, static_cast<u32>(sizeof(escaped)));
            }
            else
                destination.Append(byte);
        }
        destination.Append("\"\n", 2u);
        return destination.Length() <= detail::MaximumEditorUserStateTextBytes;
    }

    [[nodiscard]] bool AppendBlankLine(ctr::String& destination) noexcept
    {
        if (destination.Length() >= detail::MaximumEditorUserStateTextBytes ||
            !destination.Reserve(destination.Length() + 1u))
            return false;
        destination.Append('\n');
        return true;
    }

    [[nodiscard]] bool ValidUnknownAssignment(const detail::EditorUserStateTextUnknownAssignment& unknown,
                                              const ctr::StringView storage) noexcept
    {
        if (unknown.keyOffset > storage.Length() || unknown.keyByteCount > storage.Length() - unknown.keyOffset ||
            unknown.encodedValueOffset > storage.Length() ||
            unknown.encodedValueByteCount > storage.Length() - unknown.encodedValueOffset)
            return false;
        const ctr::StringView key = storage.SubView(unknown.keyOffset, unknown.keyByteCount);
        const ctr::StringView encoded = storage.SubView(unknown.encodedValueOffset, unknown.encodedValueByteCount);
        if (!serialization::IsValidTextKey(key))
            return false;
        for (u32 index = 0; index < encoded.Length(); ++index)
        {
            const char byte = encoded[index];
            if (byte == '"' || static_cast<u8>(byte) < 0x20u)
                return false;
            if (byte == '\\')
            {
                if (++index >= encoded.Length())
                    return false;
            }
        }
        ctr::String decoded;
        if (serialization::DecodeTextString(encoded, decoded) != serialization::Result::Success)
            return false;
        return true;
    }

    [[nodiscard]] bool UnknownAssignmentMatchesPanelIdentity(
        const detail::EditorUserStateTextUnknownAssignment& unknown,
        const editor::EditorUserStateDocumentView& document) noexcept
    {
        if (!unknown.panelScoped)
            return true;
        if (unknown.panelInstanceId == editor::InvalidEditorPanelInstanceId ||
            unknown.panelTypeId == editor::InvalidEditorPanelTypeId)
            return false;
        for (const editor::EditorUserStatePanelRecord& panel : document.panels)
            if (panel.instanceId == unknown.panelInstanceId)
                return panel.typeId == unknown.panelTypeId;
        return false;
    }

    [[nodiscard]] bool AppendUnknownAssignment(const detail::EditorUserStateTextUnknownAssignment& unknown,
                                               const ctr::StringView storage, ctr::String& destination) noexcept
    {
        if (!ValidUnknownAssignment(unknown, storage))
            return false;
        const ctr::StringView key = storage.SubView(unknown.keyOffset, unknown.keyByteCount);
        const ctr::StringView encoded = storage.SubView(unknown.encodedValueOffset, unknown.encodedValueByteCount);
        const u64 additional = static_cast<u64>(key.Length()) + 4u + encoded.Length() + 2u;
        if (additional > detail::MaximumEditorUserStateTextBytes - destination.Length() ||
            !destination.Reserve(destination.Length() + static_cast<u32>(additional)))
            return false;
        destination.Append(key);
        destination.Append(" = \"", 4u);
        destination.Append(encoded);
        destination.Append("\"\n", 2u);
        return true;
    }

    [[nodiscard]] bool AppendRebasedNamedLayoutIndexUnknown(
        const detail::EditorUserStateTextUnknownAssignment& unknown, const ctr::StringView storage,
        const u32 layoutOrdinal, ctr::String& destination) noexcept
    {
        if (!ValidUnknownAssignment(unknown, storage) || !unknown.namedLayoutScoped ||
            unknown.namedLayoutId == 0 || layoutOrdinal >= detail::MaximumEditorNamedLayouts)
            return false;
        const ctr::StringView key = storage.SubView(unknown.keyOffset, unknown.keyByteCount);
        if (key.Length() < 12u || !key.StartsWith("layout.") || key[7] < '0' || key[7] > '9' ||
            key[8] < '0' || key[8] > '9' || key[9] != '.')
            return false;
        const ctr::StringView encoded = storage.SubView(unknown.encodedValueOffset, unknown.encodedValueByteCount);
        const u64 additional = static_cast<u64>(key.Length()) + 4u + encoded.Length() + 2u;
        if (additional > detail::MaximumEditorUserStateTextBytes - destination.Length() ||
            !destination.Reserve(destination.Length() + static_cast<u32>(additional)))
            return false;
        destination.Append(key.SubView(0u, 7u));
        destination.Append(static_cast<char>('0' + (layoutOrdinal / 10u)));
        destination.Append(static_cast<char>('0' + (layoutOrdinal % 10u)));
        destination.Append(key.SubView(9u));
        destination.Append(" = \"", 4u);
        destination.Append(encoded);
        destination.Append("\"\n", 2u);
        return true;
    }

    [[nodiscard]] bool StoreUnknown(const serialization::TextAssignment& assignment, const u32 ordinal,
                                    detail::EditorUserStateTextDecodedStorage& storage) noexcept
    {
        if (!serialization::IsValidTextKey(assignment.key) ||
            storage.unknownAssignmentCount == detail::MaximumEditorUserStateTextUnknownRecords)
            return false;
        const u64 required = static_cast<u64>(assignment.key.Length()) + assignment.encodedValue.Length();
        if (required > detail::MaximumEditorUserStateTextUnknownBytes - storage.unknownByteCount)
            return false;

        detail::EditorUserStateTextUnknownAssignment& record =
            storage.unknownAssignments[storage.unknownAssignmentCount++];
        record = {};
        record.keyOffset = storage.unknownByteCount;
        record.keyByteCount = assignment.key.Length();
        std::memcpy(storage.unknownBytes + storage.unknownByteCount, assignment.key.Data(), assignment.key.Length());
        storage.unknownByteCount += assignment.key.Length();
        record.encodedValueOffset = storage.unknownByteCount;
        record.encodedValueByteCount = assignment.encodedValue.Length();
        std::memcpy(storage.unknownBytes + storage.unknownByteCount, assignment.encodedValue.Data(),
                    assignment.encodedValue.Length());
        storage.unknownByteCount += assignment.encodedValue.Length();
        record.ordinal = ordinal;
        return true;
    }

    [[nodiscard]] bool StoreNamedLayoutIndexUnknown(
        const serialization::TextAssignment& assignment, const u32 ordinal,
        detail::EditorNamedLayoutIndexDecodedStorage& storage) noexcept
    {
        if (!serialization::IsValidTextKey(assignment.key) ||
            storage.unknownAssignmentCount == detail::MaximumEditorNamedLayoutIndexUnknownRecords)
            return false;
        const u64 required = static_cast<u64>(assignment.key.Length()) + assignment.encodedValue.Length();
        if (required > detail::MaximumEditorNamedLayoutIndexUnknownBytes - storage.unknownByteCount)
            return false;
        detail::EditorUserStateTextUnknownAssignment& record =
            storage.unknownAssignments[storage.unknownAssignmentCount++];
        record = {};
        record.keyOffset = storage.unknownByteCount;
        record.keyByteCount = assignment.key.Length();
        std::memcpy(storage.unknownBytes + storage.unknownByteCount, assignment.key.Data(), assignment.key.Length());
        storage.unknownByteCount += assignment.key.Length();
        record.encodedValueOffset = storage.unknownByteCount;
        record.encodedValueByteCount = assignment.encodedValue.Length();
        std::memcpy(storage.unknownBytes + storage.unknownByteCount, assignment.encodedValue.Data(),
                    assignment.encodedValue.Length());
        storage.unknownByteCount += assignment.encodedValue.Length();
        record.ordinal = ordinal;
        return true;
    }

    [[nodiscard]] editor::EditorUserStatePanelRecord* FindOrCreatePanel(
        detail::EditorUserStateTextDecodedStorage& storage, u32* const masks, const u64 instanceId,
        u32& recordIndex) noexcept
    {
        for (u32 index = 0; index < storage.panelCount; ++index)
            if (storage.panels[index].instanceId == instanceId)
            {
                recordIndex = index;
                return &storage.panels[index];
            }
        if (storage.panelCount == editor::MaximumEditorUserStatePanels)
            return nullptr;
        recordIndex = storage.panelCount++;
        masks[recordIndex] = 0;
        storage.panels[recordIndex] = {};
        storage.panels[recordIndex].instanceId = instanceId;
        return &storage.panels[recordIndex];
    }

    [[nodiscard]] detail::EditorUserStateTextPanelStateRecord* FindOrCreatePanelState(
        detail::EditorUserStateTextDecodedStorage& storage, u32* const masks,
        const u64 instanceId, const editor::EditorPanelStateScope scope, u32& recordIndex) noexcept
    {
        for (u32 index = 0; index < storage.panelStateCount; ++index)
            if (storage.panelStates[index].instanceId == instanceId && storage.panelStates[index].scope == scope)
            {
                recordIndex = index;
                return &storage.panelStates[index];
            }
        if (storage.panelStateCount == detail::MaximumEditorUserStatePanelStateRecords)
            return nullptr;
        recordIndex = storage.panelStateCount++;
        masks[recordIndex] = 0;
        storage.panelStates[recordIndex] = {};
        storage.panelStates[recordIndex].instanceId = instanceId;
        storage.panelStates[recordIndex].scope = scope;
        return &storage.panelStates[recordIndex];
    }

    [[nodiscard]] editor::EditorUserStateShortcutRecord* FindOrCreateShortcut(
        detail::EditorUserStateTextDecodedStorage& storage, u32* const masks, const u64 contextId,
        const u64 commandId, u32& recordIndex) noexcept
    {
        for (u32 index = 0; index < storage.shortcutCount; ++index)
            if (storage.shortcuts[index].contextTypeId == contextId && storage.shortcuts[index].commandId == commandId)
            {
                recordIndex = index;
                return &storage.shortcuts[index];
            }
        if (storage.shortcutCount == editor::MaximumEditorUserStateShortcuts)
            return nullptr;
        recordIndex = storage.shortcutCount++;
        masks[recordIndex] = 0;
        storage.shortcuts[recordIndex] = {};
        storage.shortcuts[recordIndex].contextTypeId = contextId;
        storage.shortcuts[recordIndex].commandId = commandId;
        return &storage.shortcuts[recordIndex];
    }

    [[nodiscard]] editor::EditorUserStatePreferenceRecord* FindOrCreatePreference(
        detail::EditorUserStateTextDecodedStorage& storage, u32* const masks, const u64 extensionId,
        const u64 providerId, const u64 fieldId, u32& recordIndex) noexcept
    {
        for (u32 index = 0; index < storage.preferenceCount; ++index)
            if (storage.preferences[index].extensionId == extensionId &&
                storage.preferences[index].providerId == providerId && storage.preferences[index].fieldId == fieldId)
            {
                recordIndex = index;
                return &storage.preferences[index];
            }
        if (storage.preferenceCount == editor::MaximumEditorUserStatePreferences)
            return nullptr;
        recordIndex = storage.preferenceCount++;
        masks[recordIndex] = 0;
        editor::EditorUserStatePreferenceRecord& preference = storage.preferences[recordIndex];
        preference = {};
        preference.extensionId = extensionId;
        preference.providerId = providerId;
        preference.fieldId = fieldId;
        return &preference;
    }

    [[nodiscard]] bool SetOnce(u32& mask, const u32 bit) noexcept
    {
        if ((mask & bit) != 0)
            return false;
        mask |= bit;
        return true;
    }

    [[nodiscard]] bool CopyBoundedString(const ctr::StringView value, char* const destination,
                                         const u32 capacity, u16& byteCount) noexcept
    {
        if (value.Length() >= capacity || value.Length() > std::numeric_limits<u16>::max())
            return false;
        if (value.Length() != 0)
            std::memcpy(destination, value.Data(), value.Length());
        destination[value.Length()] = '\0';
        byteCount = static_cast<u16>(value.Length());
        return true;
    }

    [[nodiscard]] bool ParsePrimaryMode(const ctr::StringView value, window::WindowMode& mode) noexcept
    {
        if (value == "windowed")
            mode = window::WindowMode::Windowed;
        else if (value == "borderless-fullscreen")
            mode = window::WindowMode::BorderlessFullscreen;
        else if (value == "exclusive-fullscreen")
            mode = window::WindowMode::ExclusiveFullscreen;
        else
            return false;
        return true;
    }

    [[nodiscard]] ctr::StringView PrimaryModeText(const window::WindowMode mode) noexcept
    {
        switch (mode)
        {
        case window::WindowMode::Windowed:
            return "windowed";
        case window::WindowMode::BorderlessFullscreen:
            return "borderless-fullscreen";
        case window::WindowMode::ExclusiveFullscreen:
            return "exclusive-fullscreen";
        }
        return {};
    }

    [[nodiscard]] bool ValidSavedDisplay(const editor::EditorUserStateSavedDisplay& display) noexcept
    {
        return display.boundsWidth != 0 && display.boundsHeight != 0 &&
               display.workWidth != 0 && display.workHeight != 0 &&
               display.desktopPixelWidth != 0 && display.desktopPixelHeight != 0 &&
               display.scaleQ16_16 != 0 && display.scaleQ16_16 <= 0x00100000u;
    }

    [[nodiscard]] bool ValidPrimaryWindow(const editor::EditorUserStatePrimaryWindowRecord& primary) noexcept
    {
        return !primary.valid ||
               (primary.normalRect.width > 0 && primary.normalRect.height > 0 &&
                primary.referenceDisplayValid && ValidSavedDisplay(primary.referenceDisplay) &&
                (!primary.maximized || primary.mode == window::WindowMode::Windowed));
    }

    [[nodiscard]] u64 SavedDisplayOverlap(const editor::EditorUserStateSavedDisplay& display,
                                          const editor::detail::EditorUiSavedRect& rect) noexcept
    {
        const i64 left = std::max<i64>(display.workX, rect.x);
        const i64 top = std::max<i64>(display.workY, rect.y);
        const i64 right = std::min<i64>(static_cast<i64>(display.workX) + display.workWidth,
                                        static_cast<i64>(rect.x) + rect.width);
        const i64 bottom = std::min<i64>(static_cast<i64>(display.workY) + display.workHeight,
                                         static_cast<i64>(rect.y) + rect.height);
        if (right <= left || bottom <= top)
            return 0;
        return static_cast<u64>(right - left) * static_cast<u64>(bottom - top);
    }

    [[nodiscard]] bool MigrateLegacyPrimaryReferenceDisplay(
        detail::EditorUserStateTextDecodedStorage& storage) noexcept
    {
        if (storage.displayCount == 0)
            return false;
        u32 primaryIndex = ~u32{0};
        u32 bestIndex = ~u32{0};
        u64 bestOverlap = 0;
        for (u32 index = 0; index < storage.displayCount; ++index)
        {
            const editor::EditorUserStateSavedDisplay& display = storage.displays[index];
            if (!ValidSavedDisplay(display))
                continue;
            if (display.primary && primaryIndex == ~u32{0})
                primaryIndex = index;
            const u64 overlap = SavedDisplayOverlap(display, storage.primaryWindow.normalRect);
            if (bestIndex == ~u32{0} || overlap > bestOverlap)
            {
                bestIndex = index;
                bestOverlap = overlap;
            }
        }
        if (bestOverlap == 0 && primaryIndex != ~u32{0})
            bestIndex = primaryIndex;
        if (bestIndex == ~u32{0})
            return false;
        storage.primaryWindow.referenceDisplay = storage.displays[bestIndex];
        storage.primaryWindow.referenceDisplay.primary = true;
        storage.primaryWindow.referenceDisplayValid = true;
        return true;
    }

    [[nodiscard]] bool ParseShortcutMode(const ctr::StringView value,
                                         editor::EditorUserStateShortcutMode& mode) noexcept
    {
        if (value == "unbound")
            mode = editor::EditorUserStateShortcutMode::Unbound;
        else if (value == "custom")
            mode = editor::EditorUserStateShortcutMode::Custom;
        else
            return false;
        return true;
    }

    [[nodiscard]] bool ParsePreferenceType(const ctr::StringView value,
                                           editor::EditorUserStatePreferenceValueType& type) noexcept
    {
        if (value == "bool")
            type = editor::EditorUserStatePreferenceValueType::Bool;
        else if (value == "signed-integer")
            type = editor::EditorUserStatePreferenceValueType::SignedInteger;
        else if (value == "unsigned-bit-mask")
            type = editor::EditorUserStatePreferenceValueType::UnsignedBitMask;
        else if (value == "utf8-string")
            type = editor::EditorUserStatePreferenceValueType::Utf8String;
        else
            return false;
        return true;
    }
} // namespace

namespace vanguard::editor::detail
{
    EditorUserStateTextDocumentView EditorUserStateTextDecodedStorage::View() const noexcept
    {
        EditorUserStateDocumentView document;
        document.project = project;
        document.panels = {panels, panelCount};
        document.displays = {displays, displayCount};
        document.imguiBytes = {imguiBytes, imguiByteCount};
        document.shortcuts = {shortcuts, shortcutCount};
        document.preferences = {preferences, preferenceCount};
        document.primaryWindow = primaryWindow;
        return {version, checkpointSequence, document,
                {panelStates, panelStateCount}, {panelStateBytes, panelStateByteCount},
                {unknownAssignments, unknownAssignmentCount}, {unknownBytes, unknownByteCount}};
    }

    EditorUserStateTextResult DecodeEditorUserStateText(const containers::StringView source,
                                                        EditorUserStateTextDecodedStorage& storage,
                                                        EditorUserStateTextFailure* const failure) noexcept
    {
        storage.version = {};
        storage.checkpointSequence = 0;
        storage.project = {};
        storage.primaryWindow = {};
        storage.panelCount = 0;
        storage.panelStateCount = 0;
        storage.panelStateByteCount = 0;
        storage.displayCount = 0;
        storage.imguiByteCount = 0;
        storage.shortcutCount = 0;
        storage.preferenceCount = 0;
        storage.unknownAssignmentCount = 0;
        storage.unknownByteCount = 0;
        SetFailure(failure, EditorUserStateTextResult::Success);
        if (source.Empty() || source.Length() > MaximumEditorUserStateTextBytes)
        {
            const EditorUserStateTextResult result = source.Empty() ? EditorUserStateTextResult::InvalidSyntax
                                                                    : EditorUserStateTextResult::LimitExceeded;
            SetFailure(failure, result, source.Empty() ? 1u : 0u, source.Empty() ? 1u : 0u);
            return result;
        }

        serialization::TextDocumentReader reader(source);
        if (!reader.IsUtf8Valid())
        {
            SetFailure(failure, EditorUserStateTextResult::InvalidUtf8);
            return EditorUserStateTextResult::InvalidUtf8;
        }
        serialization::TextDocumentHeader header;
        serialization::TextFailure textFailure;
        const serialization::Result headerResult = reader.ReadHeader(header, &textFailure);
        if (headerResult != serialization::Result::Success || header.kind != "vuserstate")
        {
            SetFailure(failure, EditorUserStateTextResult::InvalidSyntax,
                       textFailure.line != 0 ? textFailure.line : 1u,
                       textFailure.column != 0 ? textFailure.column : 1u);
            return EditorUserStateTextResult::InvalidSyntax;
        }
        if (header.version.major != EditorUserStateTextVersion.major)
        {
            SetFailure(failure, EditorUserStateTextResult::UnsupportedVersion, header.line, 1u);
            return EditorUserStateTextResult::UnsupportedVersion;
        }
        storage.version = header.version;

        u32 panelMasks[MaximumEditorUserStatePanels]{};
        u32 panelStateMasks[MaximumEditorUserStatePanelStateRecords]{};
        u32 panelStateDecodedBytes[MaximumEditorUserStatePanelStateRecords]{};
        bool panelStateChunkStarted[MaximumEditorUserStatePanelStateRecords]{};
        bool panelStateChunkSealed[MaximumEditorUserStatePanelStateRecords]{};
        bool panelStateInvalid[MaximumEditorUserStatePanelStateRecords]{};
        u32 activePanelStateChunkRecord = ~u32{0};
        u32 displayMasks[MaximumEditorUserStateDisplays]{};
        u32 shortcutMasks[MaximumEditorUserStateShortcuts]{};
        u32 preferenceMasks[MaximumEditorUserStatePreferences]{};
        u32 primaryMask = 0;
        bool hasProjectId = false;
        bool hasCheckpointSequence = false;
        bool hasImguiTail = false;
        u32 imguiTailOffset = 0;
        u32 imguiTailByteCount = 0;
        u32 assignmentOrdinal = 0;
        containers::String decoded;
        const auto invalidatePanelState = [&](const u32 recordIndex) noexcept {
            if (recordIndex >= storage.panelStateCount)
                return;
            panelStateInvalid[recordIndex] = true;
            panelStateChunkSealed[recordIndex] = true;
            const EditorUserStateTextPanelStateRecord& state = storage.panelStates[recordIndex];
            if (panelStateChunkStarted[recordIndex] &&
                state.byteOffset + panelStateDecodedBytes[recordIndex] == storage.panelStateByteCount)
                storage.panelStateByteCount = state.byteOffset;
            if (activePanelStateChunkRecord == recordIndex)
            {
                activePanelStateChunkRecord = ~u32{0};
            }
        };

        for (;; ++assignmentOrdinal)
        {
            serialization::TextAssignment assignment;
            const serialization::Result readResult = reader.ReadAssignment(assignment, decoded, &textFailure);
            if (readResult == serialization::Result::EndOfStream)
                break;
            if (readResult != serialization::Result::Success)
            {
                const EditorUserStateTextResult result = readResult == serialization::Result::LimitExceeded
                                                            ? EditorUserStateTextResult::LimitExceeded
                                                            : EditorUserStateTextResult::InvalidSyntax;
                SetFailure(failure, result, textFailure.line, textFailure.column);
                return result;
            }

            if (activePanelStateChunkRecord != ~u32{0})
            {
                u64 chunkInstance = 0;
                editor::EditorPanelStateScope chunkScope = editor::EditorPanelStateScope::Presentation;
                containers::StringView chunkField;
                const EditorUserStateTextPanelStateRecord& activeState =
                    storage.panelStates[activePanelStateChunkRecord];
                const bool continuesActiveChunkRun =
                    ParsePanelStateKey(assignment.key, chunkInstance, chunkScope, chunkField) &&
                    chunkField == "chunk" && chunkInstance == activeState.instanceId &&
                    chunkScope == activeState.scope;
                if (!continuesActiveChunkRun)
                {
                    if (panelStateDecodedBytes[activePanelStateChunkRecord] != activeState.byteCount)
                        invalidatePanelState(activePanelStateChunkRecord);
                    else
                        panelStateChunkSealed[activePanelStateChunkRecord] = true;
                    activePanelStateChunkRecord = ~u32{0};
                }
            }

            bool known = false;
            if (assignment.key == "project.id")
            {
                known = true;
                if (hasProjectId || !projects::ParseProjectId(decoded, storage.project))
                {
                    SetFailure(failure, EditorUserStateTextResult::InvalidSyntax, assignment.line,
                               assignment.valueColumn);
                    return EditorUserStateTextResult::InvalidSyntax;
                }
                hasProjectId = true;
            }
            else if (assignment.key == "session.checkpoint.sequence")
            {
                known = true;
                if (hasCheckpointSequence || !ParseU64(decoded, storage.checkpointSequence) ||
                    storage.checkpointSequence == 0)
                {
                    SetFailure(failure, EditorUserStateTextResult::InvalidSyntax, assignment.line,
                               assignment.valueColumn);
                    return EditorUserStateTextResult::InvalidSyntax;
                }
                hasCheckpointSequence = true;
            }
            else if (assignment.key.StartsWith("window.primary."))
            {
                const containers::StringView field = assignment.key.SubView(15u);
                u32 bit = 0;
                bool parsed = false;
                known = true;
                if (field == "normal.x")
                    bit = PrimaryFieldNormalX, parsed = ParseI32(decoded, storage.primaryWindow.normalRect.x);
                else if (field == "normal.y")
                    bit = PrimaryFieldNormalY, parsed = ParseI32(decoded, storage.primaryWindow.normalRect.y);
                else if (field == "normal.width")
                    bit = PrimaryFieldNormalWidth, parsed = ParseI32(decoded, storage.primaryWindow.normalRect.width);
                else if (field == "normal.height")
                    bit = PrimaryFieldNormalHeight, parsed = ParseI32(decoded, storage.primaryWindow.normalRect.height);
                else if (field == "mode")
                    bit = PrimaryFieldMode, parsed = ParsePrimaryMode(decoded, storage.primaryWindow.mode);
                else if (field == "maximized")
                    bit = PrimaryFieldMaximized, parsed = ParseBool(decoded, storage.primaryWindow.maximized);
                else if (field == "display.bounds.x")
                    bit = PrimaryFieldDisplayBoundsX, parsed = ParseI32(decoded, storage.primaryWindow.referenceDisplay.boundsX);
                else if (field == "display.bounds.y")
                    bit = PrimaryFieldDisplayBoundsY, parsed = ParseI32(decoded, storage.primaryWindow.referenceDisplay.boundsY);
                else if (field == "display.bounds.width")
                    bit = PrimaryFieldDisplayBoundsWidth, parsed = ParseU32(decoded, storage.primaryWindow.referenceDisplay.boundsWidth);
                else if (field == "display.bounds.height")
                    bit = PrimaryFieldDisplayBoundsHeight, parsed = ParseU32(decoded, storage.primaryWindow.referenceDisplay.boundsHeight);
                else if (field == "display.work.x")
                    bit = PrimaryFieldDisplayWorkX, parsed = ParseI32(decoded, storage.primaryWindow.referenceDisplay.workX);
                else if (field == "display.work.y")
                    bit = PrimaryFieldDisplayWorkY, parsed = ParseI32(decoded, storage.primaryWindow.referenceDisplay.workY);
                else if (field == "display.work.width")
                    bit = PrimaryFieldDisplayWorkWidth, parsed = ParseU32(decoded, storage.primaryWindow.referenceDisplay.workWidth);
                else if (field == "display.work.height")
                    bit = PrimaryFieldDisplayWorkHeight, parsed = ParseU32(decoded, storage.primaryWindow.referenceDisplay.workHeight);
                else if (field == "display.desktop.width")
                    bit = PrimaryFieldDisplayDesktopWidth, parsed = ParseU32(decoded, storage.primaryWindow.referenceDisplay.desktopPixelWidth);
                else if (field == "display.desktop.height")
                    bit = PrimaryFieldDisplayDesktopHeight, parsed = ParseU32(decoded, storage.primaryWindow.referenceDisplay.desktopPixelHeight);
                else if (field == "display.scale-q16-16")
                    bit = PrimaryFieldDisplayScale, parsed = ParseU32(decoded, storage.primaryWindow.referenceDisplay.scaleQ16_16);
                else
                    known = false;
                if (known && (!SetOnce(primaryMask, bit) || !parsed))
                {
                    SetFailure(failure, EditorUserStateTextResult::InvalidSyntax, assignment.line,
                               assignment.valueColumn);
                    return EditorUserStateTextResult::InvalidSyntax;
                }
            }
            else
            {
                u64 firstId = 0;
                u64 secondId = 0;
                u64 thirdId = 0;
                u32 displayIndex = 0;
                containers::StringView field;
                editor::EditorPanelStateScope panelStateScope = editor::EditorPanelStateScope::Presentation;
                if (ParsePanelStateKey(assignment.key, firstId, panelStateScope, field))
                {
                    known = true;
                    if (firstId == 0)
                        continue;
                    u32 recordIndex = 0;
                    EditorUserStateTextPanelStateRecord* const state =
                        FindOrCreatePanelState(storage, panelStateMasks, firstId, panelStateScope, recordIndex);
                    if (state == nullptr)
                    {
                        SetFailure(failure, EditorUserStateTextResult::LimitExceeded, assignment.line,
                                   assignment.valueColumn);
                        return EditorUserStateTextResult::LimitExceeded;
                    }
                    if (panelStateInvalid[recordIndex])
                        continue;
                    if (field == "version")
                    {
                        if (!SetOnce(panelStateMasks[recordIndex], PanelStateFieldVersion) ||
                            !ParseU32(decoded, state->version) || state->version == 0)
                        {
                            invalidatePanelState(recordIndex);
                            continue;
                        }
                    }
                    else if (field == "bytes")
                    {
                        if (!SetOnce(panelStateMasks[recordIndex], PanelStateFieldBytes) ||
                            !ParseU32(decoded, state->byteCount) ||
                            state->byteCount > MaximumEditorPanelStateBytes)
                        {
                            invalidatePanelState(recordIndex);
                            continue;
                        }
                    }
                    else
                    {
                        if (panelStateMasks[recordIndex] != PanelStateFieldMask || state->byteCount == 0 ||
                            panelStateChunkSealed[recordIndex])
                        {
                            invalidatePanelState(recordIndex);
                            continue;
                        }
                        if (activePanelStateChunkRecord != recordIndex)
                        {
                            if (activePanelStateChunkRecord != ~u32{0})
                            {
                                if (panelStateDecodedBytes[activePanelStateChunkRecord] !=
                                    storage.panelStates[activePanelStateChunkRecord].byteCount)
                                    invalidatePanelState(activePanelStateChunkRecord);
                                else
                                    panelStateChunkSealed[activePanelStateChunkRecord] = true;
                            }
                            activePanelStateChunkRecord = recordIndex;
                            if (!panelStateChunkStarted[recordIndex])
                            {
                                state->byteOffset = storage.panelStateByteCount;
                                panelStateChunkStarted[recordIndex] = true;
                            }
                        }
                        if (panelStateDecodedBytes[recordIndex] >= state->byteCount)
                        {
                            invalidatePanelState(recordIndex);
                            continue;
                        }
                        if (storage.panelStateByteCount >= MaximumEditorUserStatePanelStateBytes)
                        {
                            SetFailure(failure, EditorUserStateTextResult::LimitExceeded, assignment.line,
                                       assignment.valueColumn);
                            return EditorUserStateTextResult::LimitExceeded;
                        }
                        const u32 remainingRecord = state->byteCount - panelStateDecodedBytes[recordIndex];
                        const u32 remainingArena = MaximumEditorUserStatePanelStateBytes - storage.panelStateByteCount;
                        const u32 requestedChunkBytes = decoded.Length() / 2u;
                        if (requestedChunkBytes > remainingArena)
                        {
                            SetFailure(failure, EditorUserStateTextResult::LimitExceeded, assignment.line,
                                       assignment.valueColumn);
                            return EditorUserStateTextResult::LimitExceeded;
                        }
                        u32 decodedChunkBytes = 0;
                        if (!DecodeHexChunk(decoded, storage.panelStateBytes + storage.panelStateByteCount,
                                            std::min(remainingRecord, remainingArena), decodedChunkBytes))
                        {
                            invalidatePanelState(recordIndex);
                            continue;
                        }
                        panelStateDecodedBytes[recordIndex] += decodedChunkBytes;
                        storage.panelStateByteCount += decodedChunkBytes;
                    }
                }
                else if (ParsePanelKey(assignment.key, firstId, field))
                {
                    known = true;
                    if (firstId == 0)
                    {
                        SetFailure(failure, EditorUserStateTextResult::InvalidSyntax, assignment.line, 1u);
                        return EditorUserStateTextResult::InvalidSyntax;
                    }
                    u32 recordIndex = 0;
                    EditorUserStatePanelRecord* const panel =
                        FindOrCreatePanel(storage, panelMasks, firstId, recordIndex);
                    if (panel == nullptr)
                    {
                        SetFailure(failure, EditorUserStateTextResult::LimitExceeded, assignment.line,
                                   assignment.valueColumn);
                        return EditorUserStateTextResult::LimitExceeded;
                    }
                    u32 bit = 0;
                    bool parsed = false;
                    if (field == "type")
                        bit = PanelFieldType, parsed = ParseFixedHex16(decoded, panel->typeId);
                    else if (field == "open")
                        bit = PanelFieldOpen, parsed = ParseBool(decoded, panel->open);
                    else if (field == "title")
                    {
                        bit = PanelFieldTitle;
                        if (decoded.Length() > MaximumEditorUserStatePanelTitleBytes)
                        {
                            SetFailure(failure, EditorUserStateTextResult::LimitExceeded, assignment.line,
                                       assignment.valueColumn);
                            return EditorUserStateTextResult::LimitExceeded;
                        }
                        parsed = CopyBoundedString(decoded, panel->fallbackTitle,
                                                   MaximumEditorUserStatePanelTitleBytes + 1u,
                                                   panel->fallbackTitleByteCount);
                    }
                    else
                        known = false;
                    if (!SetOnce(panelMasks[recordIndex], bit) || !parsed ||
                        (field == "type" && panel->typeId == 0))
                    {
                        SetFailure(failure, EditorUserStateTextResult::InvalidSyntax, assignment.line,
                                   assignment.valueColumn);
                        return EditorUserStateTextResult::InvalidSyntax;
                    }
                }
                else if (ParseDisplayKey(assignment.key, displayIndex, field))
                {
                    known = true;
                    EditorUserStateSavedDisplay& display = storage.displays[displayIndex];
                    if (displayMasks[displayIndex] == 0)
                        display = {};
                    u32 bit = 0;
                    bool parsed = false;
                    if (field == "primary")
                        bit = DisplayFieldPrimary, parsed = ParseBool(decoded, display.primary);
                    else if (field == "bounds.x")
                        bit = DisplayFieldBoundsX, parsed = ParseI32(decoded, display.boundsX);
                    else if (field == "bounds.y")
                        bit = DisplayFieldBoundsY, parsed = ParseI32(decoded, display.boundsY);
                    else if (field == "bounds.width")
                        bit = DisplayFieldBoundsWidth, parsed = ParseU32(decoded, display.boundsWidth);
                    else if (field == "bounds.height")
                        bit = DisplayFieldBoundsHeight, parsed = ParseU32(decoded, display.boundsHeight);
                    else if (field == "work.x")
                        bit = DisplayFieldWorkX, parsed = ParseI32(decoded, display.workX);
                    else if (field == "work.y")
                        bit = DisplayFieldWorkY, parsed = ParseI32(decoded, display.workY);
                    else if (field == "work.width")
                        bit = DisplayFieldWorkWidth, parsed = ParseU32(decoded, display.workWidth);
                    else if (field == "work.height")
                        bit = DisplayFieldWorkHeight, parsed = ParseU32(decoded, display.workHeight);
                    else if (field == "desktop.width")
                        bit = DisplayFieldDesktopWidth, parsed = ParseU32(decoded, display.desktopPixelWidth);
                    else if (field == "desktop.height")
                        bit = DisplayFieldDesktopHeight, parsed = ParseU32(decoded, display.desktopPixelHeight);
                    else
                        bit = DisplayFieldScale, parsed = ParseU32(decoded, display.scaleQ16_16);
                    if (!SetOnce(displayMasks[displayIndex], bit) || !parsed)
                    {
                        SetFailure(failure, EditorUserStateTextResult::InvalidSyntax, assignment.line,
                                   assignment.valueColumn);
                        return EditorUserStateTextResult::InvalidSyntax;
                    }
                    if (displayIndex + 1u > storage.displayCount)
                        storage.displayCount = displayIndex + 1u;
                }
                else if (assignment.key == "imgui.line")
                {
                    known = true;
                    const u64 required = static_cast<u64>(storage.imguiByteCount) + decoded.Length() + 1u +
                                         imguiTailByteCount;
                    if (required > MaximumEditorUserStateImguiBytes)
                    {
                        SetFailure(failure, EditorUserStateTextResult::LimitExceeded, assignment.line,
                                   assignment.valueColumn);
                        return EditorUserStateTextResult::LimitExceeded;
                    }
                    if (decoded.Length() != 0)
                        std::memcpy(storage.imguiBytes + storage.imguiByteCount, decoded.Data(), decoded.Length());
                    storage.imguiByteCount += decoded.Length();
                    storage.imguiBytes[storage.imguiByteCount++] = '\n';
                }
                else if (assignment.key == "imgui.tail")
                {
                    known = true;
                    if (hasImguiTail || static_cast<u64>(storage.imguiByteCount) + decoded.Length() >
                                            MaximumEditorUserStateImguiBytes)
                    {
                        const EditorUserStateTextResult result = hasImguiTail ? EditorUserStateTextResult::InvalidSyntax
                                                                            : EditorUserStateTextResult::LimitExceeded;
                        SetFailure(failure, result, assignment.line, assignment.valueColumn);
                        return result;
                    }
                    imguiTailByteCount = decoded.Length();
                    imguiTailOffset = MaximumEditorUserStateImguiBytes - imguiTailByteCount;
                    if (imguiTailByteCount != 0)
                        std::memcpy(storage.imguiBytes + imguiTailOffset, decoded.Data(), imguiTailByteCount);
                    hasImguiTail = true;
                }
                else if (ParseShortcutKey(assignment.key, firstId, secondId, field))
                {
                    known = true;
                    if (firstId == 0 || secondId == 0)
                    {
                        SetFailure(failure, EditorUserStateTextResult::InvalidSyntax, assignment.line, 1u);
                        return EditorUserStateTextResult::InvalidSyntax;
                    }
                    u32 recordIndex = 0;
                    EditorUserStateShortcutRecord* const shortcut =
                        FindOrCreateShortcut(storage, shortcutMasks, firstId, secondId, recordIndex);
                    if (shortcut == nullptr)
                    {
                        SetFailure(failure, EditorUserStateTextResult::LimitExceeded, assignment.line,
                                   assignment.valueColumn);
                        return EditorUserStateTextResult::LimitExceeded;
                    }
                    u32 bit = 0;
                    bool parsed = false;
                    if (field == "mode")
                        bit = ShortcutFieldMode, parsed = ParseShortcutMode(decoded, shortcut->mode);
                    else if (field == "key-code")
                        bit = ShortcutFieldKeyCode, parsed = ParseU16(decoded, shortcut->keyCode);
                    else
                        bit = ShortcutFieldModifiers, parsed = ParseU8(decoded, shortcut->modifiers);
                    if (!SetOnce(shortcutMasks[recordIndex], bit) || !parsed)
                    {
                        SetFailure(failure, EditorUserStateTextResult::InvalidSyntax, assignment.line,
                                   assignment.valueColumn);
                        return EditorUserStateTextResult::InvalidSyntax;
                    }
                }
                else if (ParsePreferenceKey(assignment.key, firstId, secondId, thirdId, field))
                {
                    known = true;
                    if (firstId == 0 || secondId == 0 || thirdId == 0)
                    {
                        SetFailure(failure, EditorUserStateTextResult::InvalidSyntax, assignment.line, 1u);
                        return EditorUserStateTextResult::InvalidSyntax;
                    }
                    u32 recordIndex = 0;
                    EditorUserStatePreferenceRecord* const preference =
                        FindOrCreatePreference(storage, preferenceMasks, firstId, secondId, thirdId, recordIndex);
                    if (preference == nullptr)
                    {
                        SetFailure(failure, EditorUserStateTextResult::LimitExceeded, assignment.line,
                                   assignment.valueColumn);
                        return EditorUserStateTextResult::LimitExceeded;
                    }
                    const u32 bit = field == "type" ? PreferenceFieldType : PreferenceFieldValue;
                    bool parsed = false;
                    if (field == "type")
                        parsed = ParsePreferenceType(decoded, preference->type);
                    else
                    {
                        if (decoded.Length() > MaximumEditorUserStatePreferenceStringBytes)
                        {
                            SetFailure(failure, EditorUserStateTextResult::LimitExceeded, assignment.line,
                                       assignment.valueColumn);
                            return EditorUserStateTextResult::LimitExceeded;
                        }
                        parsed = CopyBoundedString(decoded, preference->stringValue,
                                                   MaximumEditorUserStatePreferenceStringBytes + 1u,
                                                   preference->stringByteCount);
                    }
                    if (!SetOnce(preferenceMasks[recordIndex], bit) || !parsed)
                    {
                        SetFailure(failure, EditorUserStateTextResult::InvalidSyntax, assignment.line,
                                   assignment.valueColumn);
                        return EditorUserStateTextResult::InvalidSyntax;
                    }
                }
            }

            if (!known && !StoreUnknown(assignment, assignmentOrdinal, storage))
            {
                const EditorUserStateTextResult result = serialization::IsValidTextKey(assignment.key)
                                                            ? EditorUserStateTextResult::LimitExceeded
                                                            : EditorUserStateTextResult::InvalidSyntax;
                SetFailure(failure, result, assignment.line, 1u);
                return result;
            }
        }

        if (!hasProjectId || !hasCheckpointSequence ||
            (primaryMask != 0 && primaryMask != PrimaryLegacyFieldMask &&
             primaryMask != PrimaryFieldMask))
        {
            SetFailure(failure, EditorUserStateTextResult::InvalidSyntax);
            return EditorUserStateTextResult::InvalidSyntax;
        }
        const bool hasLegacyPrimary = primaryMask == PrimaryLegacyFieldMask;
        storage.primaryWindow.valid = primaryMask != 0;
        storage.primaryWindow.referenceDisplayValid = primaryMask == PrimaryFieldMask;
        if (storage.primaryWindow.referenceDisplayValid)
            storage.primaryWindow.referenceDisplay.primary = true;

        for (u32 index = 0; index < storage.panelCount; ++index)
            if (panelMasks[index] != PanelFieldMask)
            {
                SetFailure(failure, EditorUserStateTextResult::InvalidSyntax);
                return EditorUserStateTextResult::InvalidSyntax;
            }

        for (u32 index = 0; index < storage.unknownAssignmentCount; ++index)
        {
            EditorUserStateTextUnknownAssignment& unknown = storage.unknownAssignments[index];
            const ctr::StringView key(storage.unknownBytes + unknown.keyOffset, unknown.keyByteCount);
            u64 instance = 0;
            ctr::StringView suffix;
            if (!ParsePanelParentKey(key, instance, suffix))
                continue;
            unknown.panelScoped = true;
            unknown.panelInstanceId = instance;
            for (u32 panelIndex = 0; panelIndex < storage.panelCount; ++panelIndex)
                if (storage.panels[panelIndex].instanceId == instance)
                {
                    unknown.panelTypeId = storage.panels[panelIndex].typeId;
                    break;
                }
        }
        if (activePanelStateChunkRecord != ~u32{0})
        {
            if (panelStateDecodedBytes[activePanelStateChunkRecord] !=
                storage.panelStates[activePanelStateChunkRecord].byteCount)
                invalidatePanelState(activePanelStateChunkRecord);
            else
                panelStateChunkSealed[activePanelStateChunkRecord] = true;
            activePanelStateChunkRecord = ~u32{0};
        }
        for (u32 index = 0; index < storage.panelStateCount; ++index)
        {
            const EditorUserStateTextPanelStateRecord& state = storage.panelStates[index];
            if (panelStateMasks[index] != PanelStateFieldMask || state.version == 0 ||
                state.byteCount > MaximumEditorPanelStateBytes ||
                panelStateDecodedBytes[index] != state.byteCount ||
                (state.byteCount != 0 && !panelStateChunkStarted[index]))
                panelStateInvalid[index] = true;
            bool panelFound = false;
            for (u32 panelIndex = 0; panelIndex < storage.panelCount; ++panelIndex)
                if (storage.panels[panelIndex].instanceId == state.instanceId)
                {
                    panelFound = true;
                    break;
                }
            if (!panelFound)
                panelStateInvalid[index] = true;
        }

        // A provider blob is a separately versioned opaque payload. Once the surrounding text
        // grammar and panel inventory are readable, semantic corruption is isolated to that one
        // (instance, scope) instead of invalidating the editor's unrelated session/window/layout.
        u32 panelStateByteOrder[MaximumEditorUserStatePanelStateRecords]{};
        SortIndices(panelStateByteOrder, storage.panelStateCount, [&](const u32 left, const u32 right) noexcept {
            return storage.panelStates[left].byteOffset < storage.panelStates[right].byteOffset;
        });
        u32 compactPanelStateBytes = 0;
        for (u32 orderIndex = 0; orderIndex < storage.panelStateCount; ++orderIndex)
        {
            const u32 recordIndex = panelStateByteOrder[orderIndex];
            EditorUserStateTextPanelStateRecord& state = storage.panelStates[recordIndex];
            if (panelStateInvalid[recordIndex])
                continue;
            if (state.byteCount != 0)
                std::memmove(storage.panelStateBytes + compactPanelStateBytes,
                             storage.panelStateBytes + state.byteOffset, state.byteCount);
            state.byteOffset = compactPanelStateBytes;
            compactPanelStateBytes += state.byteCount;
        }
        u32 compactPanelStateCount = 0;
        for (u32 index = 0; index < storage.panelStateCount; ++index)
            if (!panelStateInvalid[index])
                storage.panelStates[compactPanelStateCount++] = storage.panelStates[index];
        storage.panelStateCount = compactPanelStateCount;
        storage.panelStateByteCount = compactPanelStateBytes;
        for (u32 index = 0; index < storage.displayCount; ++index)
            if (displayMasks[index] != DisplayFieldMask)
            {
                SetFailure(failure, EditorUserStateTextResult::InvalidSyntax);
                return EditorUserStateTextResult::InvalidSyntax;
            }
        if (hasLegacyPrimary && !MigrateLegacyPrimaryReferenceDisplay(storage))
        {
            SetFailure(failure, EditorUserStateTextResult::InvalidSyntax);
            return EditorUserStateTextResult::InvalidSyntax;
        }
        if (!ValidPrimaryWindow(storage.primaryWindow))
        {
            SetFailure(failure, EditorUserStateTextResult::InvalidSyntax);
            return EditorUserStateTextResult::InvalidSyntax;
        }
        for (u32 index = 0; index < storage.shortcutCount; ++index)
        {
            const u32 mask = shortcutMasks[index];
            const bool unbound = storage.shortcuts[index].mode == EditorUserStateShortcutMode::Unbound;
            if ((unbound && mask != ShortcutFieldMode) ||
                (!unbound && mask != (ShortcutFieldMode | ShortcutFieldKeyCode | ShortcutFieldModifiers)))
            {
                SetFailure(failure, EditorUserStateTextResult::InvalidSyntax);
                return EditorUserStateTextResult::InvalidSyntax;
            }
        }
        for (u32 index = 0; index < storage.preferenceCount; ++index)
        {
            EditorUserStatePreferenceRecord& preference = storage.preferences[index];
            if (preferenceMasks[index] != PreferenceFieldMask)
            {
                SetFailure(failure, EditorUserStateTextResult::InvalidSyntax);
                return EditorUserStateTextResult::InvalidSyntax;
            }
            const containers::StringView value(preference.stringValue, preference.stringByteCount);
            switch (preference.type)
            {
            case EditorUserStatePreferenceValueType::Bool:
                if (!ParseBool(value, preference.boolValue))
                {
                    SetFailure(failure, EditorUserStateTextResult::InvalidSyntax);
                    return EditorUserStateTextResult::InvalidSyntax;
                }
                preference.stringByteCount = 0;
                preference.stringValue[0] = '\0';
                break;
            case EditorUserStatePreferenceValueType::SignedInteger:
                if (!ParseI64(value, preference.signedValue))
                {
                    SetFailure(failure, EditorUserStateTextResult::InvalidSyntax);
                    return EditorUserStateTextResult::InvalidSyntax;
                }
                preference.stringByteCount = 0;
                preference.stringValue[0] = '\0';
                break;
            case EditorUserStatePreferenceValueType::UnsignedBitMask:
                if (!ParseU64(value, preference.unsignedValue))
                {
                    SetFailure(failure, EditorUserStateTextResult::InvalidSyntax);
                    return EditorUserStateTextResult::InvalidSyntax;
                }
                preference.stringByteCount = 0;
                preference.stringValue[0] = '\0';
                break;
            case EditorUserStatePreferenceValueType::Utf8String:
                break;
            }
        }

        if (hasImguiTail)
        {
            if (static_cast<u64>(storage.imguiByteCount) + imguiTailByteCount > MaximumEditorUserStateImguiBytes)
            {
                SetFailure(failure, EditorUserStateTextResult::LimitExceeded);
                return EditorUserStateTextResult::LimitExceeded;
            }
            if (imguiTailByteCount != 0)
                std::memmove(storage.imguiBytes + storage.imguiByteCount,
                             storage.imguiBytes + imguiTailOffset, imguiTailByteCount);
            storage.imguiByteCount += imguiTailByteCount;
        }

        u32 panelOrder[MaximumEditorUserStatePanels]{};
        SortIndices(panelOrder, storage.panelCount, [&](const u32 left, const u32 right) noexcept {
            return storage.panels[left].instanceId < storage.panels[right].instanceId;
        });
        EditorUserStatePanelRecord sortedPanels[MaximumEditorUserStatePanels]{};
        for (u32 index = 0; index < storage.panelCount; ++index)
            sortedPanels[index] = storage.panels[panelOrder[index]];
        std::memcpy(storage.panels, sortedPanels, sizeof(EditorUserStatePanelRecord) * storage.panelCount);

        u32 panelStateOrder[MaximumEditorUserStatePanelStateRecords]{};
        SortIndices(panelStateOrder, storage.panelStateCount, [&](const u32 left, const u32 right) noexcept {
            const EditorUserStateTextPanelStateRecord& a = storage.panelStates[left];
            const EditorUserStateTextPanelStateRecord& b = storage.panelStates[right];
            return a.instanceId != b.instanceId
                       ? a.instanceId < b.instanceId
                       : static_cast<u8>(a.scope) < static_cast<u8>(b.scope);
        });
        EditorUserStateTextPanelStateRecord sortedPanelStates[MaximumEditorUserStatePanelStateRecords]{};
        for (u32 index = 0; index < storage.panelStateCount; ++index)
            sortedPanelStates[index] = storage.panelStates[panelStateOrder[index]];
        std::memcpy(storage.panelStates, sortedPanelStates,
                    sizeof(EditorUserStateTextPanelStateRecord) * storage.panelStateCount);

        u32 shortcutOrder[MaximumEditorUserStateShortcuts]{};
        SortIndices(shortcutOrder, storage.shortcutCount, [&](const u32 left, const u32 right) noexcept {
            const EditorUserStateShortcutRecord& a = storage.shortcuts[left];
            const EditorUserStateShortcutRecord& b = storage.shortcuts[right];
            return a.contextTypeId != b.contextTypeId ? a.contextTypeId < b.contextTypeId : a.commandId < b.commandId;
        });
        EditorUserStateShortcutRecord sortedShortcuts[MaximumEditorUserStateShortcuts]{};
        for (u32 index = 0; index < storage.shortcutCount; ++index)
            sortedShortcuts[index] = storage.shortcuts[shortcutOrder[index]];
        std::memcpy(storage.shortcuts, sortedShortcuts,
                    sizeof(EditorUserStateShortcutRecord) * storage.shortcutCount);

        u32 preferenceOrder[MaximumEditorUserStatePreferences]{};
        SortIndices(preferenceOrder, storage.preferenceCount, [&](const u32 left, const u32 right) noexcept {
            const EditorUserStatePreferenceRecord& a = storage.preferences[left];
            const EditorUserStatePreferenceRecord& b = storage.preferences[right];
            if (a.extensionId != b.extensionId)
                return a.extensionId < b.extensionId;
            if (a.providerId != b.providerId)
                return a.providerId < b.providerId;
            return a.fieldId < b.fieldId;
        });
        EditorUserStatePreferenceRecord sortedPreferences[MaximumEditorUserStatePreferences]{};
        for (u32 index = 0; index < storage.preferenceCount; ++index)
            sortedPreferences[index] = storage.preferences[preferenceOrder[index]];
        std::memcpy(storage.preferences, sortedPreferences,
                    sizeof(EditorUserStatePreferenceRecord) * storage.preferenceCount);

        SetFailure(failure, EditorUserStateTextResult::Success);
        return EditorUserStateTextResult::Success;
    }

    EditorUserStateTextResult EncodeEditorUserStateText(const EditorUserStateTextDocumentView& source,
                                                        containers::String& destination,
                                                        EditorUserStateTextFailure* const failure) noexcept
    {
        destination.Clear();
        SetFailure(failure, EditorUserStateTextResult::Success);
        if (source.version.major != EditorUserStateTextVersion.major)
        {
            SetFailure(failure, EditorUserStateTextResult::UnsupportedVersion);
            return EditorUserStateTextResult::UnsupportedVersion;
        }
        if (!source.document.project.IsValid())
        {
            SetFailure(failure, EditorUserStateTextResult::InvalidArgument);
            return EditorUserStateTextResult::InvalidArgument;
        }
        if (source.checkpointSequence == 0)
        {
            SetFailure(failure, EditorUserStateTextResult::InvalidArgument);
            return EditorUserStateTextResult::InvalidArgument;
        }
        if (source.document.panels.Size() > MaximumEditorUserStatePanels ||
            source.panelStates.Size() > MaximumEditorUserStatePanelStateRecords ||
            source.panelStateBytes.Size() > MaximumEditorUserStatePanelStateBytes ||
            source.document.displays.Size() > MaximumEditorUserStateDisplays ||
            source.document.imguiBytes.Size() > MaximumEditorUserStateImguiBytes ||
            source.document.shortcuts.Size() > MaximumEditorUserStateShortcuts ||
            source.document.preferences.Size() > MaximumEditorUserStatePreferences ||
            source.unknownAssignments.Size() > MaximumEditorUserStateTextUnknownRecords ||
            source.unknownBytes.Length() > MaximumEditorUserStateTextUnknownBytes)
        {
            SetFailure(failure, EditorUserStateTextResult::LimitExceeded);
            return EditorUserStateTextResult::LimitExceeded;
        }

        const auto failLimit = [&]() noexcept {
            SetFailure(failure, EditorUserStateTextResult::LimitExceeded);
            return EditorUserStateTextResult::LimitExceeded;
        };
        if (serialization::AppendTextDocumentHeader(destination, "vuserstate", source.version) !=
                serialization::Result::Success ||
            !AppendBlankLine(destination))
            return failLimit();

        char projectId[projects::ProjectIdTextBufferSize]{};
        if (!projects::FormatProjectId(source.document.project, projectId, projects::ProjectIdTextBufferSize) ||
            !AppendKnownAssignment(destination, "project.id", projectId))
            return failLimit();
        char checkpointSequence[21]{};
        if (!AppendKnownAssignment(destination, "session.checkpoint.sequence",
                                   FormatU64(source.checkpointSequence, checkpointSequence)))
            return failLimit();

        if (!ValidPrimaryWindow(source.document.primaryWindow))
        {
            SetFailure(failure, EditorUserStateTextResult::InvalidArgument);
            return EditorUserStateTextResult::InvalidArgument;
        }
        if (source.document.primaryWindow.valid)
        {
            char signedNumber[22]{};
            char unsignedNumber[21]{};
            const ctr::StringView mode = PrimaryModeText(source.document.primaryWindow.mode);
            if (mode.Empty())
            {
                SetFailure(failure, EditorUserStateTextResult::InvalidArgument);
                return EditorUserStateTextResult::InvalidArgument;
            }
            if (!AppendKnownAssignment(destination, "window.primary.normal.x",
                                       FormatI32(source.document.primaryWindow.normalRect.x, signedNumber)) ||
                !AppendKnownAssignment(destination, "window.primary.normal.y",
                                       FormatI32(source.document.primaryWindow.normalRect.y, signedNumber)) ||
                !AppendKnownAssignment(destination, "window.primary.normal.width",
                                       FormatI32(source.document.primaryWindow.normalRect.width, signedNumber)) ||
                !AppendKnownAssignment(destination, "window.primary.normal.height",
                                       FormatI32(source.document.primaryWindow.normalRect.height, signedNumber)) ||
                !AppendKnownAssignment(destination, "window.primary.mode", mode) ||
                !AppendKnownAssignment(destination, "window.primary.maximized",
                                       source.document.primaryWindow.maximized ? "true" : "false") ||
                !AppendKnownAssignment(destination, "window.primary.display.bounds.x",
                                       FormatI32(source.document.primaryWindow.referenceDisplay.boundsX, signedNumber)) ||
                !AppendKnownAssignment(destination, "window.primary.display.bounds.y",
                                       FormatI32(source.document.primaryWindow.referenceDisplay.boundsY, signedNumber)) ||
                !AppendKnownAssignment(destination, "window.primary.display.bounds.width",
                                       FormatU32(source.document.primaryWindow.referenceDisplay.boundsWidth, unsignedNumber)) ||
                !AppendKnownAssignment(destination, "window.primary.display.bounds.height",
                                       FormatU32(source.document.primaryWindow.referenceDisplay.boundsHeight, unsignedNumber)) ||
                !AppendKnownAssignment(destination, "window.primary.display.work.x",
                                       FormatI32(source.document.primaryWindow.referenceDisplay.workX, signedNumber)) ||
                !AppendKnownAssignment(destination, "window.primary.display.work.y",
                                       FormatI32(source.document.primaryWindow.referenceDisplay.workY, signedNumber)) ||
                !AppendKnownAssignment(destination, "window.primary.display.work.width",
                                       FormatU32(source.document.primaryWindow.referenceDisplay.workWidth, unsignedNumber)) ||
                !AppendKnownAssignment(destination, "window.primary.display.work.height",
                                       FormatU32(source.document.primaryWindow.referenceDisplay.workHeight, unsignedNumber)) ||
                !AppendKnownAssignment(destination, "window.primary.display.desktop.width",
                                       FormatU32(source.document.primaryWindow.referenceDisplay.desktopPixelWidth, unsignedNumber)) ||
                !AppendKnownAssignment(destination, "window.primary.display.desktop.height",
                                       FormatU32(source.document.primaryWindow.referenceDisplay.desktopPixelHeight, unsignedNumber)) ||
                !AppendKnownAssignment(destination, "window.primary.display.scale-q16-16",
                                       FormatU32(source.document.primaryWindow.referenceDisplay.scaleQ16_16, unsignedNumber)))
                return failLimit();
        }
        if (!AppendBlankLine(destination))
            return failLimit();

        u32 panelOrder[MaximumEditorUserStatePanels]{};
        SortIndices(panelOrder, source.document.panels.Size(), [&](const u32 left, const u32 right) noexcept {
            return source.document.panels[left].instanceId < source.document.panels[right].instanceId;
        });
        for (u32 index = 0; index < source.document.panels.Size(); ++index)
        {
            const EditorUserStatePanelRecord& panel = source.document.panels[panelOrder[index]];
            if (index != 0 && source.document.panels[panelOrder[index - 1u]].instanceId == panel.instanceId)
            {
                SetFailure(failure, EditorUserStateTextResult::InvalidArgument);
                return EditorUserStateTextResult::InvalidArgument;
            }
            if (panel.fallbackTitleByteCount > MaximumEditorUserStatePanelTitleBytes ||
                panel.instanceId == 0 || panel.typeId == 0 ||
                !IsTextValueRepresentable({panel.fallbackTitle, panel.fallbackTitleByteCount}))
            {
                SetFailure(failure, EditorUserStateTextResult::InvalidArgument);
                return EditorUserStateTextResult::InvalidArgument;
            }
            char instance[17]{};
            char type[17]{};
            FormatFixedHex16(panel.instanceId, instance);
            FormatFixedHex16(panel.typeId, type);
            containers::String key;
            key.Append("panel.", 6u).Append(instance, 16u);
            const u32 baseLength = key.Length();
            key.Append(".type", 5u);
            if (!AppendKnownAssignment(destination, key, type))
                return failLimit();
            key.Resize(baseLength);
            key.Append(".open", 5u);
            if (!AppendKnownAssignment(destination, key, panel.open ? "true" : "false"))
                return failLimit();
            key.Resize(baseLength);
            key.Append(".title", 6u);
            if (!AppendKnownAssignment(destination, key,
                                       {panel.fallbackTitle, panel.fallbackTitleByteCount}))
                return failLimit();
        }

        u32 panelStateOrder[MaximumEditorUserStatePanelStateRecords]{};
        SortIndices(panelStateOrder, source.panelStates.Size(), [&](const u32 left, const u32 right) noexcept {
            const EditorUserStateTextPanelStateRecord& a = source.panelStates[left];
            const EditorUserStateTextPanelStateRecord& b = source.panelStates[right];
            return a.instanceId != b.instanceId
                       ? a.instanceId < b.instanceId
                       : static_cast<u8>(a.scope) < static_cast<u8>(b.scope);
        });
        for (u32 index = 0; index < source.panelStates.Size(); ++index)
        {
            const EditorUserStateTextPanelStateRecord& state = source.panelStates[panelStateOrder[index]];
            if (state.instanceId == InvalidEditorPanelInstanceId || state.version == 0 ||
                static_cast<u8>(state.scope) > static_cast<u8>(EditorPanelStateScope::Content) ||
                state.byteCount > MaximumEditorPanelStateBytes ||
                state.byteOffset > source.panelStateBytes.Size() ||
                state.byteCount > source.panelStateBytes.Size() - state.byteOffset)
            {
                SetFailure(failure, EditorUserStateTextResult::InvalidArgument);
                return EditorUserStateTextResult::InvalidArgument;
            }
            if (index != 0)
            {
                const EditorUserStateTextPanelStateRecord& previous = source.panelStates[panelStateOrder[index - 1u]];
                if (previous.instanceId == state.instanceId && previous.scope == state.scope)
                {
                    SetFailure(failure, EditorUserStateTextResult::InvalidArgument);
                    return EditorUserStateTextResult::InvalidArgument;
                }
            }
            bool panelFound = false;
            for (u32 panelIndex = 0; panelIndex < source.document.panels.Size(); ++panelIndex)
                if (source.document.panels[panelIndex].instanceId == state.instanceId)
                {
                    panelFound = true;
                    break;
                }
            if (!panelFound)
            {
                SetFailure(failure, EditorUserStateTextResult::InvalidArgument);
                return EditorUserStateTextResult::InvalidArgument;
            }

            char instance[17]{};
            FormatFixedHex16(state.instanceId, instance);
            containers::String key;
            key.Append("panel.", 6u).Append(instance, 16u);
            key.Append(state.scope == EditorPanelStateScope::Presentation
                           ? containers::StringView(".state.presentation")
                           : containers::StringView(".state.content"));
            const u32 baseLength = key.Length();
            char number[21]{};
            key.Append(".version", 8u);
            if (!AppendKnownAssignment(destination, key, FormatU32(state.version, number)))
                return failLimit();
            key.Resize(baseLength);
            key.Append(".bytes", 6u);
            if (!AppendKnownAssignment(destination, key, FormatU32(state.byteCount, number)))
                return failLimit();

            const u8* const bytes = source.panelStateBytes.Data() + state.byteOffset;
            for (u32 offset = 0; offset < state.byteCount;)
            {
                const u32 chunkBytes = std::min(EditorUserStatePanelStateChunkBytes, state.byteCount - offset);
                char chunk[EditorUserStatePanelStateChunkBytes * 2u + 1u]{};
                key.Resize(baseLength);
                key.Append(".chunk", 6u);
                if (!AppendKnownAssignment(destination, key, FormatHexChunk(bytes + offset, chunkBytes, chunk)))
                    return failLimit();
                offset += chunkBytes;
            }
        }
        if (!AppendBlankLine(destination))
            return failLimit();

        u32 displayOrder[MaximumEditorUserStateDisplays]{};
        SortIndices(displayOrder, source.document.displays.Size(), [&](const u32 left, const u32 right) noexcept {
            return CompareDisplay(source.document.displays[left], source.document.displays[right]) < 0;
        });
        constexpr ctr::StringView DisplayFields[]{
            "primary", "bounds.x", "bounds.y", "bounds.width", "bounds.height", "work.x", "work.y",
            "work.width", "work.height", "desktop.width", "desktop.height", "scale-q16-16"};
        for (u32 index = 0; index < source.document.displays.Size(); ++index)
        {
            const EditorUserStateSavedDisplay& display = source.document.displays[displayOrder[index]];
            char prefix[] = "display.00.";
            prefix[8] = static_cast<char>('0' + (index / 10u));
            prefix[9] = static_cast<char>('0' + (index % 10u));
            containers::String key(prefix);
            char signedNumber[22]{};
            char unsignedNumber[21]{};
            const auto appendDisplay = [&](const ctr::StringView field, const ctr::StringView value) noexcept {
                key.Resize(11u);
                key.Append(field);
                return AppendKnownAssignment(destination, key, value);
            };
            if (!appendDisplay(DisplayFields[0], display.primary ? "true" : "false") ||
                !appendDisplay(DisplayFields[1], FormatI32(display.boundsX, signedNumber)) ||
                !appendDisplay(DisplayFields[2], FormatI32(display.boundsY, signedNumber)) ||
                !appendDisplay(DisplayFields[3], FormatU32(display.boundsWidth, unsignedNumber)) ||
                !appendDisplay(DisplayFields[4], FormatU32(display.boundsHeight, unsignedNumber)) ||
                !appendDisplay(DisplayFields[5], FormatI32(display.workX, signedNumber)) ||
                !appendDisplay(DisplayFields[6], FormatI32(display.workY, signedNumber)) ||
                !appendDisplay(DisplayFields[7], FormatU32(display.workWidth, unsignedNumber)) ||
                !appendDisplay(DisplayFields[8], FormatU32(display.workHeight, unsignedNumber)) ||
                !appendDisplay(DisplayFields[9], FormatU32(display.desktopPixelWidth, unsignedNumber)) ||
                !appendDisplay(DisplayFields[10], FormatU32(display.desktopPixelHeight, unsignedNumber)) ||
                !appendDisplay(DisplayFields[11], FormatU32(display.scaleQ16_16, unsignedNumber)))
                return failLimit();
        }
        if (!AppendBlankLine(destination))
            return failLimit();

        const u8* const imgui = source.document.imguiBytes.Data();
        const u32 imguiSize = source.document.imguiBytes.Size();
        u32 begin = 0;
        for (u32 index = 0; index < imguiSize; ++index)
        {
            if (imgui[index] != '\n')
                continue;
            const ctr::StringView line(reinterpret_cast<const char*>(imgui + begin), index - begin);
            if (!IsTextValueRepresentable(line) || !AppendKnownAssignment(destination, "imgui.line", line))
            {
                const EditorUserStateTextResult result = IsTextValueRepresentable(line)
                                                            ? EditorUserStateTextResult::LimitExceeded
                                                            : EditorUserStateTextResult::InvalidArgument;
                SetFailure(failure, result);
                return result;
            }
            begin = index + 1u;
        }
        if (begin < imguiSize)
        {
            const ctr::StringView tail(reinterpret_cast<const char*>(imgui + begin), imguiSize - begin);
            if (!IsTextValueRepresentable(tail) || !AppendKnownAssignment(destination, "imgui.tail", tail))
            {
                const EditorUserStateTextResult result = IsTextValueRepresentable(tail)
                                                            ? EditorUserStateTextResult::LimitExceeded
                                                            : EditorUserStateTextResult::InvalidArgument;
                SetFailure(failure, result);
                return result;
            }
        }
        if (!AppendBlankLine(destination))
            return failLimit();

        u32 shortcutOrder[MaximumEditorUserStateShortcuts]{};
        SortIndices(shortcutOrder, source.document.shortcuts.Size(), [&](const u32 left, const u32 right) noexcept {
            const EditorUserStateShortcutRecord& a = source.document.shortcuts[left];
            const EditorUserStateShortcutRecord& b = source.document.shortcuts[right];
            return a.contextTypeId != b.contextTypeId ? a.contextTypeId < b.contextTypeId : a.commandId < b.commandId;
        });
        for (u32 index = 0; index < source.document.shortcuts.Size(); ++index)
        {
            const EditorUserStateShortcutRecord& shortcut = source.document.shortcuts[shortcutOrder[index]];
            if (shortcut.contextTypeId == 0 || shortcut.commandId == 0)
            {
                SetFailure(failure, EditorUserStateTextResult::InvalidArgument);
                return EditorUserStateTextResult::InvalidArgument;
            }
            if (index != 0)
            {
                const EditorUserStateShortcutRecord& previous = source.document.shortcuts[shortcutOrder[index - 1u]];
                if (previous.contextTypeId == shortcut.contextTypeId && previous.commandId == shortcut.commandId)
                {
                    SetFailure(failure, EditorUserStateTextResult::InvalidArgument);
                    return EditorUserStateTextResult::InvalidArgument;
                }
            }
            char context[17]{};
            char command[17]{};
            FormatFixedHex16(shortcut.contextTypeId, context);
            FormatFixedHex16(shortcut.commandId, command);
            containers::String key;
            key.Append("shortcut.", 9u).Append(context, 16u).Append('.').Append(command, 16u);
            const u32 baseLength = key.Length();
            key.Append(".mode", 5u);
            const bool unbound = shortcut.mode == EditorUserStateShortcutMode::Unbound;
            const bool custom = shortcut.mode == EditorUserStateShortcutMode::Custom;
            if ((!unbound && !custom) || !AppendKnownAssignment(destination, key, unbound ? "unbound" : "custom"))
            {
                const EditorUserStateTextResult result = (!unbound && !custom)
                                                            ? EditorUserStateTextResult::InvalidArgument
                                                            : EditorUserStateTextResult::LimitExceeded;
                SetFailure(failure, result);
                return result;
            }
            if (custom)
            {
                char number[21]{};
                key.Resize(baseLength);
                key.Append(".key-code", 9u);
                if (!AppendKnownAssignment(destination, key, FormatU32(shortcut.keyCode, number)))
                    return failLimit();
                key.Resize(baseLength);
                key.Append(".modifiers", 10u);
                if (!AppendKnownAssignment(destination, key, FormatU32(shortcut.modifiers, number)))
                    return failLimit();
            }
        }
        if (!AppendBlankLine(destination))
            return failLimit();

        u32 preferenceOrder[MaximumEditorUserStatePreferences]{};
        SortIndices(preferenceOrder, source.document.preferences.Size(), [&](const u32 left, const u32 right) noexcept {
            const EditorUserStatePreferenceRecord& a = source.document.preferences[left];
            const EditorUserStatePreferenceRecord& b = source.document.preferences[right];
            if (a.extensionId != b.extensionId)
                return a.extensionId < b.extensionId;
            if (a.providerId != b.providerId)
                return a.providerId < b.providerId;
            return a.fieldId < b.fieldId;
        });
        for (u32 index = 0; index < source.document.preferences.Size(); ++index)
        {
            const EditorUserStatePreferenceRecord& preference = source.document.preferences[preferenceOrder[index]];
            if (preference.extensionId == 0 || preference.providerId == 0 || preference.fieldId == 0)
            {
                SetFailure(failure, EditorUserStateTextResult::InvalidArgument);
                return EditorUserStateTextResult::InvalidArgument;
            }
            if (index != 0)
            {
                const EditorUserStatePreferenceRecord& previous = source.document.preferences[preferenceOrder[index - 1u]];
                if (previous.extensionId == preference.extensionId && previous.providerId == preference.providerId &&
                    previous.fieldId == preference.fieldId)
                {
                    SetFailure(failure, EditorUserStateTextResult::InvalidArgument);
                    return EditorUserStateTextResult::InvalidArgument;
                }
            }
            char extension[17]{};
            char provider[17]{};
            char field[17]{};
            FormatFixedHex16(preference.extensionId, extension);
            FormatFixedHex16(preference.providerId, provider);
            FormatFixedHex16(preference.fieldId, field);
            containers::String key;
            key.Append("preference.", 11u).Append(extension, 16u).Append('.').Append(provider, 16u)
                .Append('.').Append(field, 16u);
            const u32 baseLength = key.Length();
            key.Append(".type", 5u);
            ctr::StringView type;
            ctr::StringView value;
            char signedNumber[22]{};
            char unsignedNumber[21]{};
            switch (preference.type)
            {
            case EditorUserStatePreferenceValueType::Bool:
                type = "bool";
                value = preference.boolValue ? "true" : "false";
                break;
            case EditorUserStatePreferenceValueType::SignedInteger:
                type = "signed-integer";
                value = FormatI64(preference.signedValue, signedNumber);
                break;
            case EditorUserStatePreferenceValueType::UnsignedBitMask:
                type = "unsigned-bit-mask";
                value = FormatU64(preference.unsignedValue, unsignedNumber);
                break;
            case EditorUserStatePreferenceValueType::Utf8String:
                type = "utf8-string";
                if (preference.stringByteCount > MaximumEditorUserStatePreferenceStringBytes ||
                    !IsTextValueRepresentable({preference.stringValue, preference.stringByteCount}))
                {
                    SetFailure(failure, EditorUserStateTextResult::InvalidArgument);
                    return EditorUserStateTextResult::InvalidArgument;
                }
                value = {preference.stringValue, preference.stringByteCount};
                break;
            default:
                SetFailure(failure, EditorUserStateTextResult::InvalidArgument);
                return EditorUserStateTextResult::InvalidArgument;
            }
            if (!AppendKnownAssignment(destination, key, type))
                return failLimit();
            key.Resize(baseLength);
            key.Append(".value", 6u);
            if (!AppendKnownAssignment(destination, key, value))
                return failLimit();
        }

        if (source.unknownAssignments.Size() != 0 && !AppendBlankLine(destination))
            return failLimit();
        for (u32 index = 0; index < source.unknownAssignments.Size(); ++index)
            if (!ValidUnknownAssignment(source.unknownAssignments[index], source.unknownBytes))
            {
                SetFailure(failure, EditorUserStateTextResult::InvalidArgument);
                return EditorUserStateTextResult::InvalidArgument;
            }
        u32 unknownOrder[MaximumEditorUserStateTextUnknownRecords]{};
        SortIndices(unknownOrder, source.unknownAssignments.Size(), [&](const u32 left, const u32 right) noexcept {
            const EditorUserStateTextUnknownAssignment& a = source.unknownAssignments[left];
            const EditorUserStateTextUnknownAssignment& b = source.unknownAssignments[right];
            const ctr::StringView aKey = source.unknownBytes.SubView(a.keyOffset, a.keyByteCount);
            const ctr::StringView bKey = source.unknownBytes.SubView(b.keyOffset, b.keyByteCount);
            if (aKey != bKey)
                return aKey < bKey;
            return a.ordinal < b.ordinal;
        });
        for (u32 index = 0; index < source.unknownAssignments.Size(); ++index)
        {
            const EditorUserStateTextUnknownAssignment& unknown = source.unknownAssignments[unknownOrder[index]];
            if (!UnknownAssignmentMatchesPanelIdentity(unknown, source.document))
                continue;
            if (!AppendUnknownAssignment(unknown, source.unknownBytes, destination))
            {
                SetFailure(failure, EditorUserStateTextResult::InvalidArgument);
                return EditorUserStateTextResult::InvalidArgument;
            }
        }

        if (destination.Length() > MaximumEditorUserStateTextBytes)
            return failLimit();
        SetFailure(failure, EditorUserStateTextResult::Success);
        return EditorUserStateTextResult::Success;
    }

    EditorNamedLayoutDocumentView EditorNamedLayoutTextDecodedStorage::View() const noexcept
    {
        const EditorUserStateTextDocumentView projected = projectedSession.View();
        return {version, id, projected.document.primaryWindow, projected.document.panels,
                projected.panelStates, projected.panelStateBytes, projected.document.displays,
                projected.document.imguiBytes, projected.unknownAssignments, projected.unknownBytes};
    }

    namespace
    {
        constexpr containers::StringView NamedLayoutProjectionProject =
            "00000000-0000-0000-0000-000000000001";

        [[nodiscard]] bool ValidNamedLayoutName(const containers::StringView name) noexcept
        {
            if (name.Empty() || name.Length() > MaximumEditorNamedLayoutNameBytes ||
                !serialization::IsValidUtf8(name))
                return false;
            bool hasVisible = false;
            for (const char value : name)
            {
                const u8 byte = static_cast<u8>(value);
                if (byte < 0x20u || byte == 0x7fu)
                    return false;
                if (value == '<' || value == '>' || value == ':' || value == '"' || value == '/' ||
                    value == '\\' || value == '|' || value == '?' || value == '*')
                    return false;
                hasVisible = hasVisible || (value != ' ' && value != '\t');
            }
            return hasVisible;
        }

        [[nodiscard]] EditorUserStateTextResult MapTextProjectionFailure(
            const serialization::Result result, const serialization::TextFailure& textFailure,
            EditorUserStateTextFailure* const failure) noexcept
        {
            EditorUserStateTextResult mapped = EditorUserStateTextResult::InvalidSyntax;
            if (result == serialization::Result::LimitExceeded)
                mapped = EditorUserStateTextResult::LimitExceeded;
            else if (result == serialization::Result::UnsupportedVersion)
                mapped = EditorUserStateTextResult::UnsupportedVersion;
            else if (result == serialization::Result::InvalidEncoding)
                mapped = EditorUserStateTextResult::InvalidSyntax;
            SetFailure(failure, mapped, textFailure.line, textFailure.column);
            return mapped;
        }
    } // namespace

    EditorUserStateTextResult EncodeEditorNamedLayoutText(const EditorNamedLayoutDocumentView& source,
                                                          containers::String& destination,
                                                          EditorUserStateTextFailure* const failure) noexcept
    {
        destination.Clear();
        SetFailure(failure, EditorUserStateTextResult::Success);
        if (source.id == 0 || source.version.major != EditorNamedLayoutTextVersion.major ||
            source.panels.Size() > MaximumEditorUserStatePanels ||
            source.panelStates.Size() > MaximumEditorUserStatePanels ||
            source.panelStateBytes.Size() > MaximumEditorUserStatePanelStateBytes ||
            source.displays.Size() > MaximumEditorUserStateDisplays ||
            source.imguiBytes.Size() > MaximumEditorUserStateImguiBytes)
        {
            SetFailure(failure, EditorUserStateTextResult::InvalidArgument);
            return EditorUserStateTextResult::InvalidArgument;
        }
        for (const EditorUserStateTextPanelStateRecord& state : source.panelStates)
            if (state.scope != EditorPanelStateScope::Presentation)
            {
                SetFailure(failure, EditorUserStateTextResult::InvalidArgument);
                return EditorUserStateTextResult::InvalidArgument;
            }

        projects::ProjectId projectionProject;
        if (!projects::ParseProjectId(NamedLayoutProjectionProject, projectionProject))
        {
            SetFailure(failure, EditorUserStateTextResult::InvalidArgument);
            return EditorUserStateTextResult::InvalidArgument;
        }

        EditorUserStateTextDocumentView projected;
        projected.version = source.version;
        projected.checkpointSequence = 1;
        projected.document.project = projectionProject;
        projected.document.primaryWindow = source.primaryWindow;
        projected.document.panels = source.panels;
        projected.document.displays = source.displays;
        projected.document.imguiBytes = source.imguiBytes;
        projected.document.shortcuts = {};
        projected.document.preferences = {};
        projected.panelStates = source.panelStates;
        projected.panelStateBytes = source.panelStateBytes;
        projected.unknownAssignments = source.unknownAssignments;
        projected.unknownBytes = source.unknownBytes;

        containers::String session;
        EditorUserStateTextFailure projectedFailure;
        const EditorUserStateTextResult encoded = EncodeEditorUserStateText(projected, session, &projectedFailure);
        if (encoded != EditorUserStateTextResult::Success)
        {
            if (failure != nullptr)
                *failure = projectedFailure;
            return encoded;
        }

        if (serialization::AppendTextDocumentHeader(destination, "vlayout", source.version) != serialization::Result::Success)
        {
            SetFailure(failure, EditorUserStateTextResult::LimitExceeded);
            return EditorUserStateTextResult::LimitExceeded;
        }
        char id[17]{};
        FormatFixedHex16(source.id, id);
        if (!AppendKnownAssignment(destination, "layout.id", {id, 16u}))
        {
            SetFailure(failure, EditorUserStateTextResult::LimitExceeded);
            return EditorUserStateTextResult::LimitExceeded;
        }

        serialization::TextDocumentReader reader(session);
        serialization::TextDocumentHeader header;
        serialization::TextFailure textFailure;
        if (reader.ReadHeader(header, &textFailure) != serialization::Result::Success)
            return MapTextProjectionFailure(serialization::Result::InvalidEncoding, textFailure, failure);
        containers::String decoded;
        for (;;)
        {
            serialization::TextAssignment assignment;
            const serialization::Result result = reader.ReadAssignment(assignment, decoded, &textFailure);
            if (result == serialization::Result::EndOfStream)
                break;
            if (result != serialization::Result::Success)
                return MapTextProjectionFailure(result, textFailure, failure);
            if (assignment.key == "project.id" || assignment.key == "session.checkpoint.sequence" ||
                assignment.key == "layout.id")
                continue;
            if (!AppendKnownAssignment(destination, assignment.key, decoded))
            {
                SetFailure(failure, EditorUserStateTextResult::LimitExceeded);
                return EditorUserStateTextResult::LimitExceeded;
            }
        }
        if (destination.Length() > MaximumEditorUserStateTextBytes)
        {
            SetFailure(failure, EditorUserStateTextResult::LimitExceeded);
            return EditorUserStateTextResult::LimitExceeded;
        }
        return EditorUserStateTextResult::Success;
    }

    EditorUserStateTextResult DecodeEditorNamedLayoutText(const containers::StringView source,
                                                          EditorNamedLayoutTextDecodedStorage& storage,
                                                          EditorUserStateTextFailure* const failure) noexcept
    {
        storage.version = {};
        storage.id = 0;
        SetFailure(failure, EditorUserStateTextResult::Success);
        if (source.Empty() || source.Length() > MaximumEditorUserStateTextBytes)
        {
            const EditorUserStateTextResult result = source.Empty() ? EditorUserStateTextResult::InvalidSyntax
                                                                    : EditorUserStateTextResult::LimitExceeded;
            SetFailure(failure, result, source.Empty() ? 1u : 0u, source.Empty() ? 1u : 0u);
            return result;
        }

        serialization::TextDocumentReader reader(source);
        serialization::TextDocumentHeader header;
        serialization::TextFailure textFailure;
        const serialization::Result headerResult = reader.ReadHeader(header, &textFailure);
        if (headerResult != serialization::Result::Success || header.kind != "vlayout")
            return MapTextProjectionFailure(headerResult == serialization::Result::Success
                                                ? serialization::Result::InvalidEncoding
                                                : headerResult,
                                            textFailure, failure);
        if (header.version.major != EditorNamedLayoutTextVersion.major)
        {
            SetFailure(failure, EditorUserStateTextResult::UnsupportedVersion, header.line, 1u);
            return EditorUserStateTextResult::UnsupportedVersion;
        }

        containers::String projected;
        if (serialization::AppendTextDocumentHeader(projected, "vuserstate", header.version) != serialization::Result::Success ||
            serialization::AppendTextAssignment(projected, "project.id", NamedLayoutProjectionProject) != serialization::Result::Success ||
            serialization::AppendTextAssignment(projected, "session.checkpoint.sequence", "1") != serialization::Result::Success)
        {
            SetFailure(failure, EditorUserStateTextResult::LimitExceeded);
            return EditorUserStateTextResult::LimitExceeded;
        }

        bool hasId = false;
        containers::String decoded;
        for (;;)
        {
            serialization::TextAssignment assignment;
            const serialization::Result result = reader.ReadAssignment(assignment, decoded, &textFailure);
            if (result == serialization::Result::EndOfStream)
                break;
            if (result != serialization::Result::Success)
                return MapTextProjectionFailure(result, textFailure, failure);
            if (assignment.key == "layout.id")
            {
                if (hasId || !ParseFixedHex16(decoded, storage.id) || storage.id == 0)
                {
                    SetFailure(failure, EditorUserStateTextResult::InvalidSyntax, assignment.line, assignment.valueColumn);
                    return EditorUserStateTextResult::InvalidSyntax;
                }
                hasId = true;
                continue;
            }
            if (assignment.key == "project.id" || assignment.key == "session.checkpoint.sequence" ||
                assignment.key.StartsWith("shortcut.") || assignment.key.StartsWith("preference.") ||
                assignment.key.Find(".state.content") != containers::StringView::npos)
            {
                SetFailure(failure, EditorUserStateTextResult::InvalidSyntax, assignment.line, assignment.valueColumn);
                return EditorUserStateTextResult::InvalidSyntax;
            }
            if (serialization::AppendTextAssignment(projected, assignment.key, decoded) != serialization::Result::Success)
            {
                SetFailure(failure, EditorUserStateTextResult::LimitExceeded, assignment.line, assignment.valueColumn);
                return EditorUserStateTextResult::LimitExceeded;
            }
        }
        if (!hasId)
        {
            SetFailure(failure, EditorUserStateTextResult::InvalidSyntax, 1u, 1u);
            return EditorUserStateTextResult::InvalidSyntax;
        }

        EditorUserStateTextFailure projectedFailure;
        const EditorUserStateTextResult decodedResult = DecodeEditorUserStateText(projected, storage.projectedSession,
                                                                                  &projectedFailure);
        if (decodedResult != EditorUserStateTextResult::Success)
        {
            if (failure != nullptr)
                *failure = projectedFailure;
            return decodedResult;
        }
        if (storage.projectedSession.shortcutCount != 0 || storage.projectedSession.preferenceCount != 0 ||
            storage.projectedSession.panelStateCount > MaximumEditorUserStatePanels)
        {
            SetFailure(failure, EditorUserStateTextResult::InvalidSyntax);
            return EditorUserStateTextResult::InvalidSyntax;
        }
        for (u32 index = 0; index < storage.projectedSession.panelStateCount; ++index)
            if (storage.projectedSession.panelStates[index].scope != EditorPanelStateScope::Presentation)
            {
                SetFailure(failure, EditorUserStateTextResult::InvalidSyntax);
                return EditorUserStateTextResult::InvalidSyntax;
            }
        storage.version = header.version;
        return EditorUserStateTextResult::Success;
    }

    EditorUserStateTextResult EncodeEditorNamedLayoutIndexText(const EditorNamedLayoutIndexView& source,
                                                               containers::String& destination,
                                                               EditorUserStateTextFailure* const failure) noexcept
    {
        destination.Clear();
        SetFailure(failure, EditorUserStateTextResult::Success);
        if (source.version.major != EditorNamedLayoutIndexTextVersion.major ||
            source.layouts.Size() > MaximumEditorNamedLayouts ||
            (!source.layouts.Empty() && source.layouts.Data() == nullptr) ||
            source.unknownAssignments.Size() > MaximumEditorNamedLayoutIndexUnknownRecords ||
            (!source.unknownAssignments.Empty() && source.unknownAssignments.Data() == nullptr) ||
            source.unknownBytes.Length() > MaximumEditorNamedLayoutIndexUnknownBytes ||
            (!source.unknownBytes.Empty() && source.unknownBytes.Data() == nullptr))
        {
            SetFailure(failure, EditorUserStateTextResult::InvalidArgument);
            return EditorUserStateTextResult::InvalidArgument;
        }
        for (u32 index = 0; index < source.layouts.Size(); ++index)
        {
            const EditorNamedLayoutIndexRecord& record = source.layouts[index];
            if (record.id == 0 || record.nameByteCount == 0 ||
                record.nameByteCount > MaximumEditorNamedLayoutNameBytes ||
                !ValidNamedLayoutName({record.name, record.nameByteCount}))
            {
                SetFailure(failure, EditorUserStateTextResult::InvalidArgument, index + 1u);
                return EditorUserStateTextResult::InvalidArgument;
            }
            for (u32 previous = 0; previous < index; ++previous)
                if (source.layouts[previous].id == record.id ||
                    containers::StringView(source.layouts[previous].name, source.layouts[previous].nameByteCount) ==
                        containers::StringView(record.name, record.nameByteCount))
                {
                    SetFailure(failure, EditorUserStateTextResult::InvalidArgument, index + 1u);
                    return EditorUserStateTextResult::InvalidArgument;
                }
        }
        if (source.activeId != 0)
        {
            bool found = false;
            for (const EditorNamedLayoutIndexRecord& record : source.layouts)
                found = found || record.id == source.activeId;
            if (!found)
            {
                SetFailure(failure, EditorUserStateTextResult::InvalidArgument);
                return EditorUserStateTextResult::InvalidArgument;
            }
        }

        if (serialization::AppendTextDocumentHeader(destination, "vlayout-index", source.version) != serialization::Result::Success)
        {
            SetFailure(failure, EditorUserStateTextResult::LimitExceeded);
            return EditorUserStateTextResult::LimitExceeded;
        }
        char hex[17]{};
        FormatFixedHex16(source.activeId, hex);
        if (!AppendKnownAssignment(destination, "active.id", {hex, 16u}))
            return (SetFailure(failure, EditorUserStateTextResult::LimitExceeded), EditorUserStateTextResult::LimitExceeded);
        char count[21]{};
        if (!AppendKnownAssignment(destination, "layout.count", FormatU32(source.layouts.Size(), count)))
            return (SetFailure(failure, EditorUserStateTextResult::LimitExceeded), EditorUserStateTextResult::LimitExceeded);

        u32 order[MaximumEditorNamedLayouts]{};
        SortIndices(order, source.layouts.Size(), [&](const u32 left, const u32 right) noexcept {
            return source.layouts[left].id < source.layouts[right].id;
        });
        for (u32 ordinal = 0; ordinal < source.layouts.Size(); ++ordinal)
        {
            const EditorNamedLayoutIndexRecord& record = source.layouts[order[ordinal]];
            char key[32]{};
            std::snprintf(key, sizeof(key), "layout.%02u.id", ordinal);
            FormatFixedHex16(record.id, hex);
            if (!AppendKnownAssignment(destination, key, {hex, 16u}))
                return (SetFailure(failure, EditorUserStateTextResult::LimitExceeded), EditorUserStateTextResult::LimitExceeded);
            std::snprintf(key, sizeof(key), "layout.%02u.name", ordinal);
            if (!AppendKnownAssignment(destination, key, {record.name, record.nameByteCount}))
                return (SetFailure(failure, EditorUserStateTextResult::LimitExceeded), EditorUserStateTextResult::LimitExceeded);
        }

        u32 unknownOrder[MaximumEditorNamedLayoutIndexUnknownRecords]{};
        u32 unknownLayoutOrdinals[MaximumEditorNamedLayoutIndexUnknownRecords]{};
        u32 retainedUnknownCount = 0;
        for (u32 index = 0; index < source.unknownAssignments.Size(); ++index)
        {
            const EditorUserStateTextUnknownAssignment& unknown = source.unknownAssignments[index];
            if (!ValidUnknownAssignment(unknown, source.unknownBytes) || unknown.panelScoped)
            {
                SetFailure(failure, EditorUserStateTextResult::InvalidArgument, index + 1u);
                return EditorUserStateTextResult::InvalidArgument;
            }
            const containers::StringView key =
                source.unknownBytes.SubView(unknown.keyOffset, unknown.keyByteCount);
            if (key == "active.id" || key == "layout.count")
            {
                SetFailure(failure, EditorUserStateTextResult::InvalidArgument, index + 1u);
                return EditorUserStateTextResult::InvalidArgument;
            }
            constexpr containers::StringView prefix = "layout.";
            const bool layoutScopedKey = key.StartsWith(prefix) && key.Length() >= 12u &&
                key[7] >= '0' && key[7] <= '9' && key[8] >= '0' && key[8] <= '9' && key[9] == '.';
            if (layoutScopedKey)
            {
                const containers::StringView field = key.SubView(10u);
                if (field == "id" || field == "name")
                {
                    SetFailure(failure, EditorUserStateTextResult::InvalidArgument, index + 1u);
                    return EditorUserStateTextResult::InvalidArgument;
                }
            }
            if (layoutScopedKey != unknown.namedLayoutScoped ||
                (unknown.namedLayoutScoped && unknown.namedLayoutId == 0))
            {
                SetFailure(failure, EditorUserStateTextResult::InvalidArgument, index + 1u);
                return EditorUserStateTextResult::InvalidArgument;
            }
            u32 layoutOrdinal = ~u32{0};
            if (unknown.namedLayoutScoped)
            {
                for (u32 ordinal = 0; ordinal < source.layouts.Size(); ++ordinal)
                    if (source.layouts[order[ordinal]].id == unknown.namedLayoutId)
                    {
                        layoutOrdinal = ordinal;
                        break;
                    }
                // Opaque metadata belonging to a deleted stable layout is retired with that
                // layout instead of being rebound to whichever record now occupies its ordinal.
                if (layoutOrdinal == ~u32{0})
                    continue;
            }
            unknownOrder[retainedUnknownCount] = index;
            unknownLayoutOrdinals[index] = layoutOrdinal;
            ++retainedUnknownCount;
        }
        const auto effectiveKeyLess = [&](const u32 left, const u32 right) noexcept {
            const EditorUserStateTextUnknownAssignment& a = source.unknownAssignments[left];
            const EditorUserStateTextUnknownAssignment& b = source.unknownAssignments[right];
            const containers::StringView aKey = source.unknownBytes.SubView(a.keyOffset, a.keyByteCount);
            const containers::StringView bKey = source.unknownBytes.SubView(b.keyOffset, b.keyByteCount);
            const u32 shared = aKey.Length() < bKey.Length() ? aKey.Length() : bKey.Length();
            for (u32 byte = 0; byte < shared; ++byte)
            {
                char aByte = aKey[byte];
                char bByte = bKey[byte];
                if (a.namedLayoutScoped && byte == 7u)
                    aByte = static_cast<char>('0' + unknownLayoutOrdinals[left] / 10u);
                else if (a.namedLayoutScoped && byte == 8u)
                    aByte = static_cast<char>('0' + unknownLayoutOrdinals[left] % 10u);
                if (b.namedLayoutScoped && byte == 7u)
                    bByte = static_cast<char>('0' + unknownLayoutOrdinals[right] / 10u);
                else if (b.namedLayoutScoped && byte == 8u)
                    bByte = static_cast<char>('0' + unknownLayoutOrdinals[right] % 10u);
                if (aByte != bByte)
                    return aByte < bByte;
            }
            if (aKey.Length() != bKey.Length())
                return aKey.Length() < bKey.Length();
            return a.ordinal < b.ordinal;
        };
        for (u32 index = 1; index < retainedUnknownCount; ++index)
        {
            const u32 value = unknownOrder[index];
            u32 insertion = index;
            while (insertion != 0 && effectiveKeyLess(value, unknownOrder[insertion - 1u]))
            {
                unknownOrder[insertion] = unknownOrder[insertion - 1u];
                --insertion;
            }
            unknownOrder[insertion] = value;
        }
        if (retainedUnknownCount != 0 && !AppendBlankLine(destination))
            return (SetFailure(failure, EditorUserStateTextResult::LimitExceeded), EditorUserStateTextResult::LimitExceeded);
        for (u32 index = 0; index < retainedUnknownCount; ++index)
        {
            const u32 unknownIndex = unknownOrder[index];
            const EditorUserStateTextUnknownAssignment& unknown = source.unknownAssignments[unknownIndex];
            const bool appended = unknown.namedLayoutScoped
                ? AppendRebasedNamedLayoutIndexUnknown(
                      unknown, source.unknownBytes, unknownLayoutOrdinals[unknownIndex], destination)
                : AppendUnknownAssignment(unknown, source.unknownBytes, destination);
            if (!appended)
                return (SetFailure(failure, EditorUserStateTextResult::LimitExceeded), EditorUserStateTextResult::LimitExceeded);
        }
        return EditorUserStateTextResult::Success;
    }

    EditorUserStateTextResult DecodeEditorNamedLayoutIndexText(const containers::StringView source,
                                                               EditorNamedLayoutIndexDecodedStorage& storage,
                                                               EditorUserStateTextFailure* const failure) noexcept
    {
        storage.version = {};
        storage.activeId = 0;
        storage.layoutCount = 0;
        storage.unknownAssignmentCount = 0;
        storage.unknownByteCount = 0;
        SetFailure(failure, EditorUserStateTextResult::Success);
        serialization::TextDocumentReader reader(source);
        if (!reader.IsUtf8Valid())
        {
            SetFailure(failure, EditorUserStateTextResult::InvalidUtf8);
            return EditorUserStateTextResult::InvalidUtf8;
        }
        serialization::TextDocumentHeader header;
        serialization::TextFailure textFailure;
        const serialization::Result headerResult = reader.ReadHeader(header, &textFailure);
        if (headerResult != serialization::Result::Success || header.kind != "vlayout-index")
            return MapTextProjectionFailure(headerResult == serialization::Result::Success
                                                ? serialization::Result::InvalidEncoding
                                                : headerResult,
                                            textFailure, failure);
        if (header.version.major != EditorNamedLayoutIndexTextVersion.major)
        {
            SetFailure(failure, EditorUserStateTextResult::UnsupportedVersion, header.line, 1u);
            return EditorUserStateTextResult::UnsupportedVersion;
        }

        bool hasActive = false;
        bool hasCount = false;
        u32 declaredCount = 0;
        u8 masks[MaximumEditorNamedLayouts]{};
        containers::String decoded;
        u32 assignmentOrdinal = 0;
        for (;;)
        {
            serialization::TextAssignment assignment;
            const serialization::Result result = reader.ReadAssignment(assignment, decoded, &textFailure);
            if (result == serialization::Result::EndOfStream)
                break;
            if (result != serialization::Result::Success)
                return MapTextProjectionFailure(result, textFailure, failure);
            const u32 ordinal = assignmentOrdinal++;
            if (assignment.key == "active.id")
            {
                if (hasActive || !ParseFixedHex16(decoded, storage.activeId))
                {
                    SetFailure(failure, EditorUserStateTextResult::InvalidSyntax, assignment.line, assignment.valueColumn);
                    return EditorUserStateTextResult::InvalidSyntax;
                }
                hasActive = true;
                continue;
            }
            if (assignment.key == "layout.count")
            {
                if (hasCount || !ParseU32(decoded, declaredCount) || declaredCount > MaximumEditorNamedLayouts)
                {
                    SetFailure(failure, EditorUserStateTextResult::InvalidSyntax, assignment.line, assignment.valueColumn);
                    return EditorUserStateTextResult::InvalidSyntax;
                }
                hasCount = true;
                continue;
            }
            constexpr containers::StringView prefix = "layout.";
            if (!assignment.key.StartsWith(prefix) || assignment.key.Length() < 12u ||
                assignment.key[7] < '0' || assignment.key[7] > '9' ||
                assignment.key[8] < '0' || assignment.key[8] > '9' || assignment.key[9] != '.')
            {
                if (!StoreNamedLayoutIndexUnknown(assignment, ordinal, storage))
                {
                    SetFailure(failure, EditorUserStateTextResult::LimitExceeded, assignment.line,
                               assignment.valueColumn);
                    return EditorUserStateTextResult::LimitExceeded;
                }
                continue;
            }
            const u32 index = static_cast<u32>(assignment.key[7] - '0') * 10u +
                              static_cast<u32>(assignment.key[8] - '0');
            if (index >= MaximumEditorNamedLayouts)
            {
                SetFailure(failure, EditorUserStateTextResult::InvalidSyntax, assignment.line, assignment.valueColumn);
                return EditorUserStateTextResult::InvalidSyntax;
            }
            const containers::StringView field = assignment.key.SubView(10u);
            EditorNamedLayoutIndexRecord& record = storage.layouts[index];
            if (field == "id")
            {
                if ((masks[index] & 1u) != 0 || !ParseFixedHex16(decoded, record.id) || record.id == 0)
                {
                    SetFailure(failure, EditorUserStateTextResult::InvalidSyntax, assignment.line, assignment.valueColumn);
                    return EditorUserStateTextResult::InvalidSyntax;
                }
                masks[index] |= 1u;
            }
            else if (field == "name")
            {
                if ((masks[index] & 2u) != 0 || !ValidNamedLayoutName(decoded))
                {
                    SetFailure(failure, EditorUserStateTextResult::InvalidSyntax, assignment.line, assignment.valueColumn);
                    return EditorUserStateTextResult::InvalidSyntax;
                }
                record.nameByteCount = decoded.Length();
                std::memcpy(record.name, decoded.Data(), decoded.Length());
                record.name[record.nameByteCount] = '\0';
                masks[index] |= 2u;
            }
            else
            {
                if (!StoreNamedLayoutIndexUnknown(assignment, ordinal, storage))
                {
                    SetFailure(failure, EditorUserStateTextResult::LimitExceeded, assignment.line,
                               assignment.valueColumn);
                    return EditorUserStateTextResult::LimitExceeded;
                }
                continue;
            }
        }
        if (!hasActive || !hasCount)
        {
            SetFailure(failure, EditorUserStateTextResult::InvalidSyntax);
            return EditorUserStateTextResult::InvalidSyntax;
        }
        for (u32 index = 0; index < declaredCount; ++index)
            if (masks[index] != 3u)
            {
                SetFailure(failure, EditorUserStateTextResult::InvalidSyntax, index + 1u);
                return EditorUserStateTextResult::InvalidSyntax;
            }
        for (u32 index = declaredCount; index < MaximumEditorNamedLayouts; ++index)
            if (masks[index] != 0)
            {
                SetFailure(failure, EditorUserStateTextResult::InvalidSyntax, index + 1u);
                return EditorUserStateTextResult::InvalidSyntax;
            }
        storage.layoutCount = declaredCount;
        for (u32 index = 0; index < storage.layoutCount; ++index)
        {
            if (index != 0 && storage.layouts[index - 1u].id >= storage.layouts[index].id)
            {
                SetFailure(failure, EditorUserStateTextResult::InvalidSyntax, index + 1u);
                return EditorUserStateTextResult::InvalidSyntax;
            }
            for (u32 previous = 0; previous < index; ++previous)
                if (storage.layouts[previous].id == storage.layouts[index].id ||
                    containers::StringView(storage.layouts[previous].name, storage.layouts[previous].nameByteCount) ==
                        containers::StringView(storage.layouts[index].name, storage.layouts[index].nameByteCount))
                {
                    SetFailure(failure, EditorUserStateTextResult::InvalidSyntax, index + 1u);
                    return EditorUserStateTextResult::InvalidSyntax;
                }
        }
        if (storage.activeId != 0)
        {
            bool found = false;
            for (u32 index = 0; index < storage.layoutCount; ++index)
                found = found || storage.layouts[index].id == storage.activeId;
            if (!found)
            {
                SetFailure(failure, EditorUserStateTextResult::InvalidSyntax);
                return EditorUserStateTextResult::InvalidSyntax;
            }
        }
        for (u32 unknownIndex = 0; unknownIndex < storage.unknownAssignmentCount; ++unknownIndex)
        {
            EditorUserStateTextUnknownAssignment& unknown = storage.unknownAssignments[unknownIndex];
            const containers::StringView key{
                storage.unknownBytes + unknown.keyOffset, unknown.keyByteCount};
            constexpr containers::StringView prefix = "layout.";
            if (!key.StartsWith(prefix) || key.Length() < 12u || key[7] < '0' || key[7] > '9' ||
                key[8] < '0' || key[8] > '9' || key[9] != '.')
                continue;
            const u32 index = static_cast<u32>(key[7] - '0') * 10u +
                              static_cast<u32>(key[8] - '0');
            if (index >= storage.layoutCount)
            {
                SetFailure(failure, EditorUserStateTextResult::InvalidSyntax, unknownIndex + 1u);
                return EditorUserStateTextResult::InvalidSyntax;
            }
            unknown.namedLayoutScoped = true;
            unknown.namedLayoutId = storage.layouts[index].id;
        }
        storage.version = header.version;
        return EditorUserStateTextResult::Success;
    }
} // namespace vanguard::editor::detail
