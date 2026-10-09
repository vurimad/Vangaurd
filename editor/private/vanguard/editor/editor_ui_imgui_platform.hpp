#pragma once

#include <vanguard/editor/editor_resources.hpp>
#include <vanguard/editor/editor_ui.hpp>
#include <vanguard/editor/editor_ui_persistence.hpp>

struct ImTextureData;

namespace vanguard::engine
{
    class InputService;
    class WindowService;
}
namespace vanguard::editor
{
    class EditorResourceService;
}

namespace vanguard::editor::detail
{
    struct EditorSvgIcon;

    class EditorUiImGuiPlatform final
    {
    public:
        EditorUiImGuiPlatform() noexcept = default;
        ~EditorUiImGuiPlatform();

        EditorUiImGuiPlatform(const EditorUiImGuiPlatform&) = delete;
        EditorUiImGuiPlatform& operator=(const EditorUiImGuiPlatform&) = delete;

        [[nodiscard]] bool Initialize(EditorUiService& ui, EditorResourceService& resources,
                                      engine::WindowService& windows, engine::InputService& input,
                                      bool frameDiagnosticsEnabled,
                                      EditorUiFailure* failure = nullptr) noexcept;
        void Shutdown() noexcept;
        [[nodiscard]] bool BeginFrame(const engine::FrameContext& frame, EditorUiFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool EndFrame(EditorUiFrameData* frames, u32 capacity, u32& count, EditorUiTextureFrameData& textureFrame,
                                    EditorUiInputCapture& inputCapture, EditorUiFailure* failure = nullptr) noexcept;
        void AbortFrame() noexcept;
        [[nodiscard]] bool GetCurrentViewportHost(EditorHostWindowHandle& host) const noexcept;
        [[nodiscard]] EditorUiUserStateLayoutLoadResult LoadUserStateLayout(
            containers::ArraySpan<const u8> bytes, containers::ArraySpan<const EditorUiSavedDisplay> savedDisplays,
            u32& repairedPlacementCount, EditorUiFailure* failure = nullptr) noexcept;
        [[nodiscard]] EditorUiUserStateLayoutLoadResult ApplyUserStateLayoutLive(
            containers::ArraySpan<const u8> bytes, containers::ArraySpan<const EditorUiSavedDisplay> savedDisplays,
            u32& repairedPlacementCount, EditorUiFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool ResetUserStateLayoutLive(EditorUiFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool CaptureUserStateLayout(void* destination, u32 capacity, u32& byteCount,
                                                  EditorUiSavedDisplay* savedDisplays, u32 displayCapacity,
                                                  u32& displayCount, EditorUiFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool ConsumeUserStateLayoutDirty(EditorUiFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool IsUserStateLayoutSettling(EditorUiFailure* failure = nullptr) const noexcept;
        [[nodiscard]] bool GetVanguardWordmarkTexture(ImTextureData*& texture, float& aspectRatio) const noexcept;
        [[nodiscard]] EditorSvgIcon* FindSvgIcon(EditorResourceId resource) const noexcept;
        [[nodiscard]] f32 GetUserScale() const noexcept;
        [[nodiscard]] bool SetUserScale(f32 scale, EditorUiFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;

    private:
        [[nodiscard]] EditorUiUserStateLayoutLoadResult ApplyUserStateLayoutInternal(
            containers::ArraySpan<const u8> bytes, containers::ArraySpan<const EditorUiSavedDisplay> savedDisplays,
            bool startup, u32& repairedPlacementCount, EditorUiFailure* failure) noexcept;
        struct Impl;
        Impl* m_impl = nullptr;
    };
} // namespace vanguard::editor::detail
