#pragma once

#include <vanguard/editor/editor_ui.hpp>

#include <imgui.h>

namespace vanguard::editor::detail
{
    [[nodiscard]] inline ImTextureID ToImGuiTextureId(const EditorTextureHandle texture) noexcept
    {
        return texture.IsValid() ? (static_cast<ImTextureID>(texture.generation) << 32u) | static_cast<ImTextureID>(texture.index) : ImTextureID_Invalid;
    }

    [[nodiscard]] inline ImTextureRef ToImGuiTexture(const EditorTextureHandle texture) noexcept
    {
        return ImTextureRef(ToImGuiTextureId(texture));
    }

    [[nodiscard]] inline EditorTextureHandle FromImGuiTextureId(const ImTextureID texture) noexcept
    {
        const u64 value = static_cast<u64>(texture);
        return {static_cast<u32>(value), static_cast<u32>(value >> 32u)};
    }
} // namespace vanguard::editor::detail
