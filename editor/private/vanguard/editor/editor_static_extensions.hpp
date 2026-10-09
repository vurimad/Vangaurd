#pragma once

#include <vanguard/editor/editor_shell_service.hpp>

namespace vanguard::editor
{
    // Authoritative product extension descriptor providers and aggregate.
    [[nodiscard]] containers::ArraySpan<const EditorExtensionDescriptor> GetEditorActivityPresentationExtensions() noexcept;
    [[nodiscard]] containers::ArraySpan<const EditorExtensionDescriptor> GetEditorPreferencesExtensions() noexcept;
    [[nodiscard]] containers::ArraySpan<const EditorExtensionDescriptor> GetEditorToolbarExtensions() noexcept;
    [[nodiscard]] containers::ArraySpan<const EditorExtensionDescriptor> GetEditorProjectAssetActivityExtensions() noexcept;
    [[nodiscard]] containers::ArraySpan<const EditorExtensionDescriptor> GetEditorAssetBrowserExtensions() noexcept;
    [[nodiscard]] containers::ArraySpan<const EditorExtensionDescriptor> GetEditorDemonstrationExtensions() noexcept;
    [[nodiscard]] containers::ArraySpan<const EditorExtensionDescriptor> GetEditorProductionExtensions() noexcept;

    // Compiled extension catalog for the production editor application.
    [[nodiscard]] containers::ArraySpan<const EditorExtensionDescriptor> GetCompiledEditorExtensions() noexcept;
} // namespace vanguard::editor
