#include <vanguard/editor/editor_static_extensions.hpp>

namespace vanguard::editor
{
    containers::ArraySpan<const EditorExtensionDescriptor> GetCompiledEditorExtensions() noexcept
    {
        return GetEditorProductionExtensions();
    }
} // namespace vanguard::editor
