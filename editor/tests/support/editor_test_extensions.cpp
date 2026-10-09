// TEST SUPPORT ONLY: focused extension-catalog fixtures.
// Production editor behavior must not depend on any descriptor or callback here.

#include <editor_test_extensions.hpp>

#include <vanguard/editor/editor_shell_service.hpp>

namespace
{
    namespace editor = vanguard::editor;

    bool ActivateTestExtension(editor::EditorExtensionActivationContext&, const void*, void*& privateState) noexcept
    {
        privateState = nullptr;
        return true;
    }

    void DeactivateTestExtension(editor::EditorExtensionActivationContext&, const void*, void*) noexcept
    {
    }

    constexpr editor::EditorExtensionId ProjectTestDependencies[]{editor::testing::StaticTestDependencyExtensionId};

    const editor::EditorExtensionDescriptor TestExtensions[]{
        {editor::testing::StaticTestProjectExtensionId, "editor-test-project", "Editor Project Test Extension", ProjectTestDependencies,
         editor::EditorExtensionSelectionPolicy::ProjectSelectable, ActivateTestExtension, DeactivateTestExtension, nullptr},
        {editor::testing::StaticTestBaseExtensionId, "editor-test-base", "Editor Built-In Test Extension", {},
         editor::EditorExtensionSelectionPolicy::BuiltIn, ActivateTestExtension, DeactivateTestExtension, nullptr},
        {editor::testing::StaticTestDependencyExtensionId, "editor-test-dependency", "Editor Test Dependency", {},
         editor::EditorExtensionSelectionPolicy::DependencyOnly, ActivateTestExtension, DeactivateTestExtension, nullptr}};
} // namespace

namespace vanguard::editor::testing
{
    containers::ArraySpan<const EditorExtensionDescriptor> GetEditorTestExtensions() noexcept
    {
        return TestExtensions;
    }
} // namespace vanguard::editor::testing
