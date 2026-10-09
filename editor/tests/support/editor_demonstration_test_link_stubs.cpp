// TEST SUPPORT ONLY: narrow link shims for the production demonstration extension.
// The focused Shell test never draws the panel or samples the Activity callback; the real
// lifecycle integration executable compiles and executes the production implementations.

#include <vanguard/editor/editor_activity_service.hpp>
#include <vanguard/editor/editor_ui.hpp>

namespace vanguard::editor::detail
{
    bool EditorActivitySourceWriter::Add(const EditorActivityItemDescriptor& descriptor,
                                         EditorActivityFailure* const failure) noexcept
    {
        if (failure != nullptr)
            *failure = {};
        return descriptor.id != InvalidEditorActivityItemId && descriptor.generation != 0 &&
               descriptor.label != nullptr;
    }
} // namespace vanguard::editor::detail

namespace vanguard::editor
{
    bool EditorPanelContext::RequestContextMenu(const EditorUiSurfaceId surface,
                                                EditorUiFailure* const failure) const noexcept
    {
        if (failure != nullptr)
            *failure = {};
        return surface.IsValid();
    }
} // namespace vanguard::editor
