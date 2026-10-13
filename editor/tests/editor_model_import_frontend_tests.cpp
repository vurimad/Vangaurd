#include <vanguard/editor/editor_model_import_frontend.hpp>

#include <cstdio>

namespace
{
    vanguard::u32 g_failures = 0u;

    void Check(const bool condition, const char* const message) noexcept
    {
        if (condition)
            return;
        ++g_failures;
        std::fprintf(stderr, "[editorModelImportFrontendTests] FAILED: %s\n", message);
    }
}

int main()
{
    using vanguard::editor::IsEditorModelImportSourcePath;

    Check(IsEditorModelImportSourcePath("model.gltf"), "recognize canonical glTF extension");
    Check(IsEditorModelImportSourcePath("MODEL.GLTF"), "recognize uppercase glTF before durable browser import");
    Check(IsEditorModelImportSourcePath("mixed.GlB"), "recognize mixed-case GLB before durable browser import");
    Check(!IsEditorModelImportSourcePath("model.gltf.backup"), "reject a trailing extension after glTF");
    Check(!IsEditorModelImportSourcePath("model.png"), "reject a non-model project source");
    Check(!IsEditorModelImportSourcePath("gltf"), "reject a path without a model extension");

    if (g_failures != 0u)
        std::fprintf(stderr, "[editorModelImportFrontendTests] finished with %u failed checks\n", g_failures);
    return g_failures == 0u ? 0 : 1;
}
