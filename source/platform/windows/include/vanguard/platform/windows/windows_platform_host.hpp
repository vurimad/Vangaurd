#pragma once

#include <vanguard/application/platform_host.hpp>

namespace vanguard::window { class IWindowEventSink; }
namespace vanguard::window::sdl { class SdlWindowBackend; }

namespace vanguard::platform::windows
{
    class WindowsPlatformHost final : public application::IPlatformHost
    {
    public:
        WindowsPlatformHost() noexcept = default;
        ~WindowsPlatformHost() override;

        [[nodiscard]] const char* Name() const noexcept override;
        [[nodiscard]] application::PlatformStatus Initialize(
            const application::PlatformStartupInfo& startup) noexcept override;
        [[nodiscard]] application::PlatformPumpResult PumpEvents() noexcept override;
        [[nodiscard]] input::IInputBackend* InputBackend() noexcept override;
        [[nodiscard]] window::IWindowBackend* WindowBackend() noexcept override;
        [[nodiscard]] bool AttachWindowEventSink(window::IWindowEventSink* sink) noexcept override;
        void DetachWindowEventSink(window::IWindowEventSink* sink) noexcept override;
        void Shutdown() noexcept override;

    private:
        input::IInputBackend* m_inputBackend = nullptr;
        window::sdl::SdlWindowBackend* m_windowBackend = nullptr;
        window::IWindowEventSink* m_windowEventSink = nullptr;
        bool m_initialized = false;
    };
} // namespace vanguard::platform::windows
