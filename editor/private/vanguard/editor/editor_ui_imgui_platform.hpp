#pragma once

#include <vanguard/editor/editor_ui.hpp>

namespace vanguard::engine
{
    class InputService;
    class WindowService;
}

namespace vanguard::editor::detail
{
    class EditorUiImGuiPlatform final
    {
    public:
        EditorUiImGuiPlatform() noexcept = default;
        ~EditorUiImGuiPlatform();

        EditorUiImGuiPlatform(const EditorUiImGuiPlatform&) = delete;
        EditorUiImGuiPlatform& operator=(const EditorUiImGuiPlatform&) = delete;

        [[nodiscard]] bool Initialize(EditorUiService& ui, engine::WindowService& windows, engine::InputService& input,
                                      EditorUiFailure* failure = nullptr) noexcept;
        void Shutdown() noexcept;
        [[nodiscard]] bool BeginFrame(const engine::FrameContext& frame, EditorUiFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool EndFrame(EditorUiFrameData* frames, u32 capacity, u32& count, EditorUiTextureFrameData& textureFrame,
                                    EditorUiFailure* failure = nullptr) noexcept;
        void AbortFrame() noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;

    private:
        struct Impl;
        Impl* m_impl = nullptr;
    };
} // namespace vanguard::editor::detail
