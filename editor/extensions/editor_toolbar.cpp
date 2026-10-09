#include <vanguard/editor/editor_shell_service.hpp>
#include <vanguard/editor/editor_static_extensions.hpp>
#include <vanguard/editor/editor_surfaces.hpp>
#include <vanguard/editor/editor_toolbar_icons.hpp>

namespace
{
    namespace editor = vanguard::editor;

    constexpr editor::EditorExtensionId ToolbarExtensionId{0x6564746f6f6c6201ull};
    constexpr vanguard::u64 ToolbarToolContributionBase = 0x65647462746f0000ull;
    constexpr vanguard::u64 ToolbarSeparatorContributionBase = 0x6564746273650000ull;

    bool ActivateToolbar(editor::EditorExtensionActivationContext& context,
                         const void*, void*& privateState) noexcept
    {
        privateState = nullptr;
        editor::EditorUiFailure failure;
        const vanguard::containers::ArraySpan<const editor::detail::EditorToolbarIconDescriptor> icons =
            editor::detail::GetEditorToolbarIconDescriptors();
        vanguard::u8 previousGroup = 0;
        bool first = true;
        for (vanguard::u32 index = 0; index < icons.Size(); ++index)
        {
            const editor::detail::EditorToolbarIconDescriptor& icon = icons[index];
            if (!first && icon.group != previousGroup)
            {
                editor::detail::EditorUiContributionDesc separator;
                separator.id = {ToolbarSeparatorContributionBase + static_cast<vanguard::u64>(icon.group)};
                separator.surface = editor::detail::EditorMainToolbarSurfaceId;
                separator.kind = editor::detail::EditorUiContributionKind::Separator;
                separator.order = static_cast<vanguard::i32>(index * 10u + 95u);
                editor::detail::EditorUiContributionToken separatorToken;
                if (!context.RegisterSurfaceContribution(separator, separatorToken, &failure))
                    return false;
            }

            editor::detail::EditorUiContributionDesc tool;
            tool.id = {ToolbarToolContributionBase + static_cast<vanguard::u64>(index + 1u)};
            tool.surface = editor::detail::EditorMainToolbarSurfaceId;
            tool.kind = editor::detail::EditorUiContributionKind::Tool;
            tool.order = static_cast<vanguard::i32>(index * 10u + 100u);
            tool.icon = icon.resource.id;
            tool.label = icon.label;
            editor::detail::EditorUiContributionToken toolToken;
            if (!context.RegisterSurfaceContribution(tool, toolToken, &failure))
                return false;

            previousGroup = icon.group;
            first = false;
        }
        return true;
    }

    void DeactivateToolbar(editor::EditorExtensionActivationContext&,
                           const void*, void*) noexcept
    {
    }

    const editor::EditorExtensionDescriptor ToolbarExtensions[]{
        {ToolbarExtensionId,
         "editor-toolbar",
         "Editor Toolbar",
         {},
         editor::EditorExtensionSelectionPolicy::BuiltIn,
         ActivateToolbar,
         DeactivateToolbar,
         nullptr}};
} // namespace

namespace vanguard::editor
{
    containers::ArraySpan<const EditorExtensionDescriptor> GetEditorToolbarExtensions() noexcept
    {
        return ToolbarExtensions;
    }
} // namespace vanguard::editor
