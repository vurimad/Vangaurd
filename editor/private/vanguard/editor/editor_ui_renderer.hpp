#pragma once

#include <vanguard/editor/editor_ui.hpp>

namespace vanguard::engine { class RenderingService; }
namespace vanguard::window { class WindowManager; }

namespace vanguard::editor::detail
{
    // Main-thread host ownership; recording uses only retained draw payloads.
    class EditorUiRenderer final
    {
    public:
        struct Impl;
        EditorUiRenderer() noexcept = default;
        EditorUiRenderer(const EditorUiRenderer&) = delete;
        EditorUiRenderer& operator=(const EditorUiRenderer&) = delete;
        ~EditorUiRenderer();
        [[nodiscard]] bool Initialize(engine::RenderingService& rendering, window::WindowManager& windows, EditorUiFailure* failure) noexcept;
        [[nodiscard]] bool BeginFrame(EditorUiFailure* failure) noexcept;
        [[nodiscard]] bool CreateHost(EditorHostWindowHandle handle, EditorHostWindowDesc& desc, EditorUiFailure* failure) noexcept;
        [[nodiscard]] bool DestroyHost(EditorHostWindowHandle handle, EditorUiFailure* failure) noexcept;
        [[nodiscard]] bool SubmitTextures(const EditorUiTextureFrameData& data, EditorUiFailure* failure) noexcept;
        [[nodiscard]] bool SubmitHost(const EditorUiFrameData& data, EditorUiFailure* failure) noexcept;
        void Shutdown() noexcept;

    private:
        Impl* m_impl = nullptr;
    };
}
