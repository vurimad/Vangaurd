#include <vanguard/editor/editor_static_extensions.hpp>

namespace vanguard::editor
{
    containers::ArraySpan<const EditorExtensionDescriptor> GetEditorProductionExtensions() noexcept
    {
        struct ProductionExtensionCatalog final
        {
            containers::StaticArray<EditorExtensionDescriptor, MaximumEditorExtensions> descriptors;
            bool valid = true;

            void AppendRequired(const containers::ArraySpan<const EditorExtensionDescriptor> provider) noexcept
            {
                if (!valid || provider.Empty() ||
                    descriptors.Size() + provider.Size() > MaximumEditorExtensions)
                {
                    valid = false;
                    descriptors.Clear();
                    return;
                }
                for (const EditorExtensionDescriptor& descriptor : provider)
                    descriptors.PushBack(descriptor);
            }

            ProductionExtensionCatalog() noexcept
            {
                AppendRequired(GetEditorPreferencesExtensions());
                AppendRequired(GetEditorToolbarExtensions());
                AppendRequired(GetEditorActivityPresentationExtensions());
                AppendRequired(GetEditorProjectAssetActivityExtensions());
                AppendRequired(GetEditorAssetBrowserExtensions());
                AppendRequired(GetEditorDemonstrationExtensions());
            }
        };

        static const ProductionExtensionCatalog catalog;
        return catalog.valid
            ? containers::ArraySpan<const EditorExtensionDescriptor>{
                  catalog.descriptors.TypedData(), catalog.descriptors.Size()}
            : containers::ArraySpan<const EditorExtensionDescriptor>{};
    }
} // namespace vanguard::editor
