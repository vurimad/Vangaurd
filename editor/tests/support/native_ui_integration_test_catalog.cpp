// TEST SUPPORT ONLY: compiled extension catalog for the dedicated native UI integration-test executable.

#include <native_ui_integration_test_extension.hpp>
#include <vanguard/editor/editor_static_extensions.hpp>

namespace vanguard::editor
{
    containers::ArraySpan<const EditorExtensionDescriptor> GetCompiledEditorExtensions() noexcept
    {
        struct NativeUiIntegrationCatalog final
        {
            containers::StaticArray<EditorExtensionDescriptor, MaximumEditorExtensions> descriptors;
            bool valid = true;

            void Append(const containers::ArraySpan<const EditorExtensionDescriptor> provider) noexcept
            {
                if (!valid || descriptors.Size() + provider.Size() > MaximumEditorExtensions)
                {
                    valid = false;
                    descriptors.Clear();
                    return;
                }
                for (const EditorExtensionDescriptor& descriptor : provider)
                    descriptors.PushBack(descriptor);
            }

            NativeUiIntegrationCatalog() noexcept
            {
                const containers::ArraySpan<const EditorExtensionDescriptor> production =
                    GetEditorProductionExtensions();
                const containers::ArraySpan<const EditorExtensionDescriptor> test =
                    testing::GetNativeUiIntegrationTestExtensions();
                if (production.Empty() || test.Empty())
                {
                    valid = false;
                    return;
                }
                Append(production);
                Append(test);
            }
        };

        static const NativeUiIntegrationCatalog catalog;
        return catalog.valid
            ? containers::ArraySpan<const EditorExtensionDescriptor>{
                  catalog.descriptors.TypedData(), catalog.descriptors.Size()}
            : containers::ArraySpan<const EditorExtensionDescriptor>{};
    }
} // namespace vanguard::editor
