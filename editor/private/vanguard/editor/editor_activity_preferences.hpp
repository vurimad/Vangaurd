#pragma once

#include <vanguard/editor/editor_shell_service.hpp>

#include <cstring>

namespace vanguard::editor::detail
{
    inline constexpr EditorExtensionId ActivityPresentationExtensionId{0x6564333561637401ull};
    inline constexpr EditorPreferenceProviderId ActivityPreferenceProviderId = 0x6564333570726601ull;
    inline constexpr EditorPreferenceFieldId ActivityLevelsPreferenceFieldId = 0x656433356c766c01ull;
    inline constexpr EditorPreferenceFieldId ActivityCategoryPreferenceFieldId = 0x6564333563617401ull;
    inline constexpr EditorPreferenceFieldId ActivityTextPreferenceFieldId = 0x6564333574787401ull;
    inline constexpr u32 ActivityPreferenceLevelCount = 6;
    inline constexpr u32 ActivityPreferenceTextBytes = 128;

    inline void AdvanceActivityPreferenceRevision(u64& revision) noexcept
    {
        if (revision == 0)
            return;
        revision = revision == ~u64{0} ? 0 : revision + 1u;
    }

    [[nodiscard]] inline bool CaptureActivityPreferenceValue(
        const bool (&levels)[ActivityPreferenceLevelCount], const i32 category,
        const char (&text)[ActivityPreferenceTextBytes], const EditorPreferenceFieldId fieldId,
        EditorPreferenceFieldValue& value) noexcept
    {
        if (fieldId == ActivityLevelsPreferenceFieldId)
        {
            value = {};
            value.type = EditorPreferenceValueType::UnsignedBitMask;
            for (u32 index = 0; index < ActivityPreferenceLevelCount; ++index)
                if (levels[index])
                    value.value.unsignedBitMask |= u64{1} << index;
            return true;
        }
        if (fieldId == ActivityCategoryPreferenceFieldId)
        {
            value = {};
            value.type = EditorPreferenceValueType::SignedInteger;
            value.value.signedInteger = category;
            return true;
        }
        if (fieldId == ActivityTextPreferenceFieldId)
        {
            value = {};
            value.type = EditorPreferenceValueType::Utf8String;
            const u32 count = static_cast<u32>(std::strlen(text));
            if (count >= ActivityPreferenceTextBytes)
                return false;
            value.value.string.byteCount = static_cast<u16>(count);
            std::memcpy(value.value.string.bytes, text, count);
            value.value.string.bytes[count] = '\0';
            return true;
        }
        return false;
    }

    [[nodiscard]] inline bool ValidateActivityPreferenceValue(
        const EditorPreferenceFieldId fieldId, const EditorPreferenceFieldValue& value,
        const u32 categoryCount) noexcept
    {
        if (fieldId == ActivityLevelsPreferenceFieldId)
            return value.type == EditorPreferenceValueType::UnsignedBitMask &&
                   (value.value.unsignedBitMask & ~u64{0x3fu}) == 0;
        if (fieldId == ActivityCategoryPreferenceFieldId)
            return value.type == EditorPreferenceValueType::SignedInteger &&
                   (value.value.signedInteger == -1 ||
                    (value.value.signedInteger >= 0 &&
                     static_cast<u64>(value.value.signedInteger) < static_cast<u64>(categoryCount)));
        if (fieldId == ActivityTextPreferenceFieldId)
            return value.type == EditorPreferenceValueType::Utf8String &&
                   value.value.string.byteCount < ActivityPreferenceTextBytes;
        return false;
    }

    inline void ApplyActivityPreferenceValue(
        bool (&levels)[ActivityPreferenceLevelCount], i32& category,
        char (&text)[ActivityPreferenceTextBytes], u64& revision,
        const EditorPreferenceFieldId fieldId, const EditorPreferenceFieldValue& value) noexcept
    {
        bool changed = false;
        if (fieldId == ActivityLevelsPreferenceFieldId)
        {
            for (u32 index = 0; index < ActivityPreferenceLevelCount; ++index)
            {
                const bool enabled = (value.value.unsignedBitMask & (u64{1} << index)) != 0;
                changed = changed || levels[index] != enabled;
                levels[index] = enabled;
            }
        }
        else if (fieldId == ActivityCategoryPreferenceFieldId)
        {
            const i32 restoredCategory = static_cast<i32>(value.value.signedInteger);
            changed = category != restoredCategory;
            category = restoredCategory;
        }
        else if (fieldId == ActivityTextPreferenceFieldId)
        {
            const u32 count = value.value.string.byteCount;
            changed = std::strlen(text) != count || std::memcmp(text, value.value.string.bytes, count) != 0;
            std::memcpy(text, value.value.string.bytes, count);
            text[count] = '\0';
        }
        if (changed)
            AdvanceActivityPreferenceRevision(revision);
    }
} // namespace vanguard::editor::detail
