#pragma once

#include <vanguard/filesystem/filesystem.hpp>

#include <imgui.h>

namespace vanguard::editor::detail
{
    inline constexpr int EditorSvgIconRasterExtent = 128;

    struct EditorSvgIcon final
    {
        ImTextureData texture;

        [[nodiscard]] bool IsValid() const noexcept
        {
            return texture.Width == EditorSvgIconRasterExtent &&
                   texture.Height == EditorSvgIconRasterExtent && texture.Pixels != nullptr;
        }
    };

    // Rasterizes immutable editor-owned SVG artwork through NanoSVG into a high-resolution
    // white-alpha texture. Canonical SVG files remain untouched; toolbar tint is applied at draw time.
    [[nodiscard]] bool LoadEditorSvgIcon(const filesystem::AbsolutePath& path,
                                         EditorSvgIcon& output) noexcept;
    void DrawEditorSvgIcon(ImDrawList& drawList, EditorSvgIcon& icon,
                           ImVec2 minimum, float extent, ImU32 color) noexcept;
} // namespace vanguard::editor::detail
