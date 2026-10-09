#pragma once

#include <vanguard/editor/editor_activity_preferences.hpp>
#include <vanguard/editor/editor_ui.hpp>

#include <cstring>

namespace vanguard::editor::detail
{
    inline constexpr u32 ConsolePanelStateVersion = 1u;
    inline constexpr u32 ConsoleContentStateMagic = 0x31534e43u; // CNS1

    struct ConsoleContentPersistenceState final
    {
        bool levels[ActivityPreferenceLevelCount]{true, true, true, true, true, true};
        i32 category = -1;
        char text[ActivityPreferenceTextBytes]{};
        bool scrollLock = true;
    };

    [[nodiscard]] inline u8 EncodeConsoleLevelMask(
        const bool (&levels)[ActivityPreferenceLevelCount]) noexcept
    {
        u8 mask = 0u;
        for (u32 index = 0u; index < ActivityPreferenceLevelCount; ++index)
            if (levels[index])
                mask |= static_cast<u8>(u8{1u} << index);
        return mask;
    }

    inline void DecodeConsoleLevelMask(
        const u8 mask, bool (&levels)[ActivityPreferenceLevelCount]) noexcept
    {
        for (u32 index = 0u; index < ActivityPreferenceLevelCount; ++index)
            levels[index] = (mask & static_cast<u8>(u8{1u} << index)) != 0u;
    }

    [[nodiscard]] inline EditorPanelStateResult CaptureConsoleContentPersistence(
        const ConsoleContentPersistenceState& state, const u32 categoryCount,
        void* const destination, const u32 capacity, u32& byteCount) noexcept
    {
        byteCount = 0u;
        if (destination == nullptr)
            return EditorPanelStateResult::CallbackFailure;
        if (state.category < -1 ||
            (state.category >= 0 && static_cast<u32>(state.category) >= categoryCount))
            return EditorPanelStateResult::InvalidData;

        const u32 textBytes = static_cast<u32>(std::strlen(state.text));
        if (textBytes >= ActivityPreferenceTextBytes || textBytes > 0xffu)
            return EditorPanelStateResult::InvalidData;
        constexpr u32 headerBytes = 4u + 1u + 4u + 1u + 1u;
        if (headerBytes + textBytes > capacity)
            return EditorPanelStateResult::CapacityExceeded;

        auto* const output = static_cast<u8*>(destination);
        u32 write = 0u;
        const auto writeU8 = [&](const u8 value) noexcept { output[write++] = value; };
        const auto writeU32 = [&](const u32 value) noexcept {
            output[write++] = static_cast<u8>(value);
            output[write++] = static_cast<u8>(value >> 8u);
            output[write++] = static_cast<u8>(value >> 16u);
            output[write++] = static_cast<u8>(value >> 24u);
        };
        writeU32(ConsoleContentStateMagic);
        writeU8(EncodeConsoleLevelMask(state.levels));
        writeU32(static_cast<u32>(state.category));
        writeU8(state.scrollLock ? 1u : 0u);
        writeU8(static_cast<u8>(textBytes));
        if (textBytes != 0u)
        {
            std::memcpy(output + write, state.text, textBytes);
            write += textBytes;
        }
        byteCount = write;
        return EditorPanelStateResult::Success;
    }

    [[nodiscard]] inline bool DecodeConsoleContentPersistence(
        const containers::ArraySpan<const u8> bytes, const u32 categoryCount,
        ConsoleContentPersistenceState& state) noexcept
    {
        constexpr u32 headerBytes = 4u + 1u + 4u + 1u + 1u;
        if (bytes.Count() < headerBytes || bytes.Data() == nullptr)
            return false;
        const u8* const input = bytes.Data();
        u32 read = 0u;
        const auto readU8 = [&]() noexcept { return input[read++]; };
        const auto readU32 = [&]() noexcept {
            const u32 value = static_cast<u32>(input[read]) |
                              (static_cast<u32>(input[read + 1u]) << 8u) |
                              (static_cast<u32>(input[read + 2u]) << 16u) |
                              (static_cast<u32>(input[read + 3u]) << 24u);
            read += 4u;
            return value;
        };

        if (readU32() != ConsoleContentStateMagic)
            return false;
        const u8 levelMask = readU8();
        if ((levelMask & ~u8{0x3fu}) != 0u)
            return false;
        const i32 category = static_cast<i32>(readU32());
        if (category < -1 || (category >= 0 && static_cast<u32>(category) >= categoryCount))
            return false;
        const u8 scrollLock = readU8();
        if (scrollLock > 1u)
            return false;
        const u32 textBytes = readU8();
        if (textBytes >= ActivityPreferenceTextBytes || read + textBytes != bytes.Count())
            return false;
        if (textBytes != 0u && std::memchr(input + read, '\0', textBytes) != nullptr)
            return false;

        ConsoleContentPersistenceState decoded;
        DecodeConsoleLevelMask(levelMask, decoded.levels);
        decoded.category = category;
        decoded.scrollLock = scrollLock != 0u;
        if (textBytes != 0u)
            std::memcpy(decoded.text, input + read, textBytes);
        decoded.text[textBytes] = '\0';
        state = decoded;
        return true;
    }

    inline void ApplyConsoleContentPersistenceState(
        const ConsoleContentPersistenceState& persisted,
        bool (&levels)[ActivityPreferenceLevelCount], i32& category,
        char (&text)[ActivityPreferenceTextBytes], bool& scrollLock,
        bool& panelContentRestored) noexcept
    {
        for (u32 index = 0u; index < ActivityPreferenceLevelCount; ++index)
            levels[index] = persisted.levels[index];
        category = persisted.category;
        std::memcpy(text, persisted.text, sizeof(text));
        scrollLock = persisted.scrollLock;
        panelContentRestored = true;
    }

    [[nodiscard]] inline bool ValidateLegacyConsolePreferenceForPreflight(
        const EditorPreferenceFieldId fieldId,
        const EditorPreferenceFieldValue& value) noexcept
    {
        if (fieldId == ActivityLevelsPreferenceFieldId)
            return value.type == EditorPreferenceValueType::UnsignedBitMask;
        if (fieldId == ActivityCategoryPreferenceFieldId)
            return value.type == EditorPreferenceValueType::SignedInteger;
        if (fieldId == ActivityTextPreferenceFieldId)
            return value.type == EditorPreferenceValueType::Utf8String &&
                   value.value.string.byteCount < ActivityPreferenceTextBytes;
        return false;
    }

    inline void ApplyLegacyConsolePreferenceIfNeeded(
        const bool panelContentRestored,
        bool (&levels)[ActivityPreferenceLevelCount], i32& category,
        char (&text)[ActivityPreferenceTextBytes], u64& revision,
        const u32 categoryCount,
        const EditorPreferenceFieldId fieldId,
        const EditorPreferenceFieldValue& value) noexcept
    {
        if (panelContentRestored ||
            !ValidateActivityPreferenceValue(fieldId, value, categoryCount))
            return;
        ApplyActivityPreferenceValue(levels, category, text, revision, fieldId, value);
    }
}
