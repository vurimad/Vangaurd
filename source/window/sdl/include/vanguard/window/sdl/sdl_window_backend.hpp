#pragma once

#include <vanguard/window/window_backend.hpp>

union SDL_Event;

namespace vanguard::window::sdl
{
    struct EventTranslation
    {
        bool windowEvent = false;
        bool displayTopologyChanged = false;
    };

    class SdlWindowBackend final : public IWindowBackend
    {
    public:
        SdlWindowBackend() noexcept = default;
        ~SdlWindowBackend() override;

        [[nodiscard]] BackendStatus Initialize() noexcept;
        [[nodiscard]] BackendStatus Shutdown() noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;

        // The process platform host remains the sole SDL event pump. It passes every event here so window, input,
        // and editor integrations observe one ordered native event stream.
        [[nodiscard]] EventTranslation ProcessEvent(const SDL_Event& source,
                                                     BackendWindowEvent& destination) noexcept;
        [[nodiscard]] BackendWindowId ResolveNativeWindow(u32 nativeWindowId) const noexcept;

        [[nodiscard]] BackendStatus EnumerateDisplays(BackendDisplaySnapshot* displays, u32 capacity,
                                                       u32& count) noexcept override;
        [[nodiscard]] BackendStatus Create(const BackendWindowDescriptor& descriptor, BackendWindowId& window,
                                           BackendWindowState& state) noexcept override;
        [[nodiscard]] BackendStatus ApplyWindowState(BackendWindowId window, const BackendWindowRequest& request,
                                                     BackendWindowState& state) noexcept override;
        [[nodiscard]] BackendStatus SetWindowTitle(BackendWindowId window, const char* title) noexcept override;
        [[nodiscard]] BackendStatus DestroyWindow(BackendWindowId window) noexcept override;

    private:
        struct Impl;
        Impl* m_impl = nullptr;
    };
} // namespace vanguard::window::sdl
