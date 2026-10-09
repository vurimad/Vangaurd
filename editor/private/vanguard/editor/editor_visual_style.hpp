#pragma once

#include <imgui.h>

namespace vanguard::editor::detail
{
    struct EditorVisualFonts final
    {
        ImFont* regular = nullptr;
        ImFont* semibold = nullptr;
    };

    struct EditorVisualScale final
    {
        float user = 1.0f;
        float dpi = 1.0f;
        float effective = 1.0f;
    };

    inline EditorVisualFonts g_editorVisualFonts;
    inline EditorVisualScale g_editorVisualScale;

    inline void SetEditorVisualFonts(ImFont* const regular, ImFont* const semibold) noexcept
    {
        g_editorVisualFonts = {regular, semibold};
    }

    inline void ResetEditorVisualFonts() noexcept
    {
        g_editorVisualFonts = {};
    }

    [[nodiscard]] inline float NormalizeEditorVisualScale(const float scale) noexcept
    {
        return scale > 0.0f && scale <= 16.0f ? scale : 1.0f;
    }

    [[nodiscard]] inline float EditorVisualPx(const float logicalPixels) noexcept
    {
        return logicalPixels * g_editorVisualScale.effective;
    }

    enum class EditorVisualTone : unsigned char
    {
        Neutral,
        Accent,
        Success,
        Warning,
        Error
    };

    [[nodiscard]] inline ImVec4 EditorVisualColor(const unsigned int rgb, const float alpha = 1.0f) noexcept
    {
        constexpr float scale = 1.0f / 255.0f;
        return {
            static_cast<float>((rgb >> 16u) & 0xffu) * scale,
            static_cast<float>((rgb >> 8u) & 0xffu) * scale,
            static_cast<float>(rgb & 0xffu) * scale,
            alpha};
    }

    inline void ApplyEditorVisualStyle(const float userScale = 1.0f, const float dpiScale = 1.0f) noexcept
    {
        const float user = NormalizeEditorVisualScale(userScale);
        const float dpi = NormalizeEditorVisualScale(dpiScale);
        const float effective = user * dpi;
        ImGuiStyle base;
        ImGuiStyle& style = ImGui::GetStyle();
        style = base;

        style.FontSizeBase = 13.0f;
        style.DisabledAlpha = 0.55f;
        style.WindowPadding = {5.0f, 5.0f};
        style.WindowRounding = 0.0f;
        style.WindowBorderSize = 1.0f;
        style.WindowBorderHoverPadding = 8.0f;
        style.WindowTitleAlign = {0.0f, 0.5f};
        style.WindowMenuButtonPosition = ImGuiDir_None;
        style.ChildRounding = 0.0f;
        style.ChildBorderSize = 0.0f;
        style.PopupRounding = 0.0f;
        style.PopupBorderSize = 1.0f;
        style.FramePadding = {4.0f, 2.0f};
        style.FrameRounding = 0.0f;
        style.FrameBorderSize = 1.0f;
        style.ItemSpacing = {6.0f, 3.0f};
        style.ItemInnerSpacing = {4.0f, 2.0f};
        style.CellPadding = {4.0f, 2.0f};
        style.TouchExtraPadding = {0.0f, 0.0f};
        style.IndentSpacing = 16.0f;
        style.ColumnsMinSpacing = 5.0f;
        style.ScrollbarSize = 11.0f;
        style.ScrollbarRounding = 0.0f;
        style.ScrollbarPadding = 1.0f;
        style.GrabMinSize = 8.0f;
        style.GrabRounding = 0.0f;
        style.ImageRounding = 0.0f;
        style.ImageBorderSize = 0.0f;
        style.TabRounding = 0.0f;
        style.TabBorderSize = 0.0f;
        style.TabMinWidthShrink = 48.0f;
        style.TabCloseButtonMinWidthSelected = 0.0f;
        style.TabCloseButtonMinWidthUnselected = 0.0f;
        style.TabBarBorderSize = 1.0f;
        style.TabBarOverlineSize = 1.0f;
        style.TreeLinesFlags = ImGuiTreeNodeFlags_DrawLinesToNodes;
        style.TreeLinesSize = 1.0f;
        style.TreeLinesRounding = 0.0f;
        style.MenuItemRounding = 0.0f;
        style.SelectableRounding = 0.0f;
        style.DragDropTargetRounding = 0.0f;
        style.DragDropTargetBorderSize = 1.0f;
        style.ButtonTextAlign = {0.5f, 0.5f};
        style.SelectableTextAlign = {0.0f, 0.5f};
        style.InputTextCursorSize = 1.0f;
        style.SeparatorSize = 1.0f;
        style.SeparatorTextBorderSize = 1.0f;
        style.SeparatorTextAlign = {0.0f, 0.5f};
        style.SeparatorTextPadding = {5.0f, 2.0f};
        style.DockingSeparatorSize = 2.0f;

        ImVec4* const colors = style.Colors;
        colors[ImGuiCol_Text] = EditorVisualColor(0xd8d8d8u);
        colors[ImGuiCol_TextDisabled] = EditorVisualColor(0x969696u);
        colors[ImGuiCol_WindowBg] = EditorVisualColor(0x2d2d2du);
        colors[ImGuiCol_ChildBg] = EditorVisualColor(0x2d2d2du);
        colors[ImGuiCol_PopupBg] = EditorVisualColor(0x292929u, 0.99f);
        colors[ImGuiCol_Border] = EditorVisualColor(0x171717u);
        colors[ImGuiCol_BorderShadow] = EditorVisualColor(0x000000u, 0.0f);
        colors[ImGuiCol_FrameBg] = EditorVisualColor(0x3a3a3au);
        colors[ImGuiCol_FrameBgHovered] = EditorVisualColor(0x454545u);
        colors[ImGuiCol_FrameBgActive] = EditorVisualColor(0x4c4c4cu);
        colors[ImGuiCol_TitleBg] = EditorVisualColor(0x292929u);
        colors[ImGuiCol_TitleBgActive] = EditorVisualColor(0x333333u);
        colors[ImGuiCol_TitleBgCollapsed] = EditorVisualColor(0x292929u);
        colors[ImGuiCol_MenuBarBg] = EditorVisualColor(0x242424u);
        colors[ImGuiCol_ScrollbarBg] = EditorVisualColor(0x252525u);
        colors[ImGuiCol_ScrollbarGrab] = EditorVisualColor(0x555555u);
        colors[ImGuiCol_ScrollbarGrabHovered] = EditorVisualColor(0x676767u);
        colors[ImGuiCol_ScrollbarGrabActive] = EditorVisualColor(0x777777u);
        colors[ImGuiCol_CheckMark] = EditorVisualColor(0x67aee8u);
        colors[ImGuiCol_CheckboxSelectedBg] = EditorVisualColor(0x335f80u);
        colors[ImGuiCol_SliderGrab] = EditorVisualColor(0x6c6c6cu);
        colors[ImGuiCol_SliderGrabActive] = EditorVisualColor(0x67aee8u);
        colors[ImGuiCol_Button] = EditorVisualColor(0x3a3a3au);
        colors[ImGuiCol_ButtonHovered] = EditorVisualColor(0x4a4a4au);
        colors[ImGuiCol_ButtonActive] = EditorVisualColor(0x315d7du);
        colors[ImGuiCol_Header] = EditorVisualColor(0x365f7du);
        colors[ImGuiCol_HeaderHovered] = EditorVisualColor(0x47799du);
        colors[ImGuiCol_HeaderActive] = EditorVisualColor(0x5088b0u);
        colors[ImGuiCol_Separator] = EditorVisualColor(0x181818u);
        colors[ImGuiCol_SeparatorHovered] = EditorVisualColor(0x4e83a9u);
        colors[ImGuiCol_SeparatorActive] = EditorVisualColor(0x67aee8u);
        colors[ImGuiCol_ResizeGrip] = EditorVisualColor(0x000000u, 0.0f);
        colors[ImGuiCol_ResizeGripHovered] = EditorVisualColor(0x67aee8u, 0.45f);
        colors[ImGuiCol_ResizeGripActive] = EditorVisualColor(0x67aee8u, 0.75f);
        colors[ImGuiCol_InputTextCursor] = EditorVisualColor(0xe2e2e2u);
        colors[ImGuiCol_Tab] = EditorVisualColor(0x2a2a2au);
        colors[ImGuiCol_TabHovered] = EditorVisualColor(0x414141u);
        colors[ImGuiCol_TabSelected] = EditorVisualColor(0x3a3a3au);
        colors[ImGuiCol_TabSelectedOverline] = EditorVisualColor(0x67aee8u);
        colors[ImGuiCol_TabDimmed] = EditorVisualColor(0x252525u);
        colors[ImGuiCol_TabDimmedSelected] = EditorVisualColor(0x323232u);
        colors[ImGuiCol_TabDimmedSelectedOverline] = EditorVisualColor(0x526f85u);
        colors[ImGuiCol_DockingPreview] = EditorVisualColor(0x4f93c4u, 0.42f);
        colors[ImGuiCol_DockingEmptyBg] = EditorVisualColor(0x202020u);
        colors[ImGuiCol_PlotLines] = EditorVisualColor(0xa8a8a8u);
        colors[ImGuiCol_PlotLinesHovered] = EditorVisualColor(0x67aee8u);
        colors[ImGuiCol_PlotHistogram] = EditorVisualColor(0x6f9d63u);
        colors[ImGuiCol_PlotHistogramHovered] = EditorVisualColor(0x86b87au);
        colors[ImGuiCol_TableHeaderBg] = EditorVisualColor(0x333333u);
        colors[ImGuiCol_TableBorderStrong] = EditorVisualColor(0x181818u);
        colors[ImGuiCol_TableBorderLight] = EditorVisualColor(0x242424u);
        colors[ImGuiCol_TableRowBg] = EditorVisualColor(0x2d2d2du);
        colors[ImGuiCol_TableRowBgAlt] = EditorVisualColor(0x303030u);
        colors[ImGuiCol_TextLink] = EditorVisualColor(0x78b9e9u);
        colors[ImGuiCol_TextSelectedBg] = EditorVisualColor(0x4d91c3u, 0.45f);
        colors[ImGuiCol_TreeLines] = EditorVisualColor(0x505050u);
        colors[ImGuiCol_DragDropTarget] = EditorVisualColor(0x67aee8u);
        colors[ImGuiCol_DragDropTargetBg] = EditorVisualColor(0x335f80u, 0.30f);
        colors[ImGuiCol_UnsavedMarker] = EditorVisualColor(0xe6b55cu);
        colors[ImGuiCol_NavCursor] = EditorVisualColor(0x79bdf0u);
        colors[ImGuiCol_NavWindowingHighlight] = EditorVisualColor(0xcfcfcfu, 0.70f);
        colors[ImGuiCol_NavWindowingDimBg] = EditorVisualColor(0x101010u, 0.30f);
        colors[ImGuiCol_ModalWindowDimBg] = EditorVisualColor(0x0d0d0du, 0.55f);

        // Always scale from the immutable 100% authoring values above. ImGui rounds many style values,
        // so scaling an already-scaled style would accumulate drift when a window crosses monitors.
        style.ScaleAllSizes(effective);
        style.FontScaleMain = user;
        style.FontScaleDpi = dpi;
        g_editorVisualScale = {user, dpi, effective};
    }

    [[nodiscard]] inline float EditorMainToolbarHeight() noexcept
    {
        return EditorVisualPx(28.0f);
    }

    [[nodiscard]] inline float EditorMainToolbarButtonExtent() noexcept
    {
        return EditorVisualPx(24.0f);
    }

    [[nodiscard]] inline float EditorMainToolbarIconExtent() noexcept
    {
        return EditorVisualPx(16.0f);
    }

    [[nodiscard]] inline float EditorMainToolbarItemSpacing() noexcept
    {
        return EditorVisualPx(2.0f);
    }

    [[nodiscard]] inline float EditorMainToolbarSeparatorWidth() noexcept
    {
        return EditorVisualPx(7.0f);
    }

    [[nodiscard]] inline float EditorMainStatusBarHeight() noexcept
    {
        return EditorVisualPx(24.0f);
    }

    [[nodiscard]] inline float EditorMainStatusBarHorizontalPadding() noexcept
    {
        return EditorVisualPx(8.0f);
    }

    [[nodiscard]] inline float EditorMainStatusBarItemSpacing() noexcept
    {
        return EditorVisualPx(10.0f);
    }

    [[nodiscard]] inline float EditorMainMenuBrandHeight() noexcept
    {
        const float height = ImGui::GetFontSize();
        return height > 1.0f ? height : 1.0f;
    }

    [[nodiscard]] inline float EditorMainMenuBrandLeftPadding() noexcept
    {
        return EditorVisualPx(6.0f);
    }

    [[nodiscard]] inline float EditorMainMenuBrandRightPadding() noexcept
    {
        return EditorVisualPx(12.0f);
    }

    inline void PushEditorMenuPopupStyle() noexcept
    {
        // Menus sit directly over dense editor chrome, so the normal popup color is too close to
        // the toolbar/menu-bar surfaces and can visually merge with them. Give menu layers a clear
        // elevation boundary without changing ordinary panel/window borders.
        ImGui::PushStyleColor(ImGuiCol_PopupBg, EditorVisualColor(0x202020u));
        ImGui::PushStyleColor(ImGuiCol_Border, EditorVisualColor(0x5a5a5au));
        ImGui::PushStyleColor(ImGuiCol_BorderShadow, EditorVisualColor(0x000000u, 0.65f));
        ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, EditorVisualPx(1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                            ImVec2(EditorVisualPx(6.0f), EditorVisualPx(4.0f)));
    }

    inline void PopEditorMenuPopupStyle() noexcept
    {
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(3);
    }

    enum class EditorChromeWindowButton : unsigned char
    {
        Minimize,
        Maximize,
        Restore,
        Close
    };

    [[nodiscard]] inline float EditorChromeWindowButtonWidth() noexcept
    {
        return EditorVisualPx(32.0f);
    }

    [[nodiscard]] inline bool DrawEditorChromeWindowButton(const EditorChromeWindowButton button) noexcept
    {
        const ImVec2 size{EditorChromeWindowButtonWidth(), ImGui::GetFrameHeight()};
        const ImVec2 minimum = ImGui::GetCursorScreenPos();
        ImGui::PushID(static_cast<int>(button));
        const bool activated = ImGui::InvisibleButton("##editorChrome", size);
        const bool hovered = ImGui::IsItemHovered();
        const bool held = ImGui::IsItemActive();
        ImGui::PopID();

        ImDrawList* const draw = ImGui::GetWindowDrawList();
        if (hovered || held)
        {
            const ImVec4 background = button == EditorChromeWindowButton::Close
                                          ? EditorVisualColor(held ? 0xa51d14u : 0xc42b1cu)
                                          : EditorVisualColor(held ? 0x474747u : 0x393939u);
            draw->AddRectFilled(minimum, {minimum.x + size.x, minimum.y + size.y}, ImGui::ColorConvertFloat4ToU32(background));
        }

        const ImU32 icon = ImGui::ColorConvertFloat4ToU32(EditorVisualColor(hovered ? 0xe8e8e8u : 0xb8b8b8u));
        const float centerX = minimum.x + size.x * 0.5f;
        const float centerY = minimum.y + size.y * 0.5f;
        const float half = EditorVisualPx(4.0f);
        const float minimizeOffset = EditorVisualPx(2.5f);
        const float restoreOffset = EditorVisualPx(1.5f);
        const float stroke = EditorVisualPx(1.0f);
        switch (button)
        {
        case EditorChromeWindowButton::Minimize:
            draw->AddLine({centerX - half, centerY + minimizeOffset}, {centerX + half, centerY + minimizeOffset}, icon, stroke);
            break;
        case EditorChromeWindowButton::Maximize:
            draw->AddRect({centerX - half, centerY - half}, {centerX + half, centerY + half}, icon, 0.0f, 0, stroke);
            break;
        case EditorChromeWindowButton::Restore:
            draw->AddRect({centerX - half + restoreOffset, centerY - half - restoreOffset},
                          {centerX + half + restoreOffset, centerY + half - restoreOffset}, icon, 0.0f, 0, stroke);
            draw->AddRectFilled({centerX - half - restoreOffset, centerY - half + restoreOffset},
                                {centerX + half - restoreOffset, centerY + half + restoreOffset},
                                ImGui::ColorConvertFloat4ToU32(EditorVisualColor(0x242424u)));
            draw->AddRect({centerX - half - restoreOffset, centerY - half + restoreOffset},
                          {centerX + half - restoreOffset, centerY + half + restoreOffset}, icon, 0.0f, 0, stroke);
            break;
        case EditorChromeWindowButton::Close:
            draw->AddLine({centerX - half, centerY - half}, {centerX + half, centerY + half}, icon, stroke);
            draw->AddLine({centerX + half, centerY - half}, {centerX - half, centerY + half}, icon, stroke);
            break;
        }
        return activated;
    }

    [[nodiscard]] inline bool DrawEditorToolbarCommand(const char* const label, const bool enabled,
                                                       const bool checked) noexcept
    {
        const ImVec4 transparent = EditorVisualColor(0x000000u, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, checked ? EditorVisualColor(0x365f7du) : transparent);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, EditorVisualColor(0x454545u));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, EditorVisualColor(0x315d7du));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(EditorVisualPx(5.0f), EditorVisualPx(1.0f)));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        ImGui::BeginDisabled(!enabled);
        const bool activated = ImGui::Button(label);
        ImGui::EndDisabled();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(3);
        return enabled && activated;
    }

    inline void DrawEditorSectionHeader(const char* const label) noexcept
    {
        if (g_editorVisualFonts.semibold != nullptr)
            ImGui::PushFont(g_editorVisualFonts.semibold);
        ImGui::PushStyleColor(ImGuiCol_Text, EditorVisualColor(0xc7c7c7u));
        ImGui::SeparatorText(label);
        ImGui::PopStyleColor();
        if (g_editorVisualFonts.semibold != nullptr)
            ImGui::PopFont();
    }

    [[nodiscard]] inline ImVec4 EditorSecondaryTextColor() noexcept
    {
        return EditorVisualColor(0xafafafu);
    }

    inline void DrawEditorSecondaryText(const char* const text) noexcept
    {
        if (text == nullptr || text[0] == '\0')
            return;
        ImGui::TextColored(EditorSecondaryTextColor(), "%s", text);
    }

    inline void DrawEditorToneText(const char* const text, const EditorVisualTone tone) noexcept
    {
        ImVec4 color = EditorVisualColor(0x999999u);
        switch (tone)
        {
        case EditorVisualTone::Accent: color = EditorVisualColor(0x78b9e9u); break;
        case EditorVisualTone::Success: color = EditorVisualColor(0x88b47du); break;
        case EditorVisualTone::Warning: color = EditorVisualColor(0xd3a85cu); break;
        case EditorVisualTone::Error: color = EditorVisualColor(0xd46f6fu); break;
        case EditorVisualTone::Neutral: break;
        }
        ImGui::TextColored(color, "%s", text);
    }
} // namespace vanguard::editor::detail
