#pragma once

#include <vanguard/application/platform_host.hpp>

namespace vanguard::window
{
    class IWindowEventSink;
}
namespace vanguard::window::sdl
{
    class SdlWindowBackend;
}

namespace vanguard::platform::windows
{
    class WindowsPlatformHost final : public application::IPlatformHost
    {
    public:
        WindowsPlatformHost() noexcept = default;
        ~WindowsPlatformHost() override;

        [[nodiscard]] const char* GetName() const noexcept override;
        [[nodiscard]] const char* GetExecutablePath() const noexcept override;
        [[nodiscard]] application::PlatformStatus Initialize(const application::PlatformStartupInfo& startup) noexcept override;
        [[nodiscard]] application::PlatformPumpResult PumpEvents() noexcept override;
        [[nodiscard]] input::IInputBackend* GetInputBackend() noexcept override;
        [[nodiscard]] window::IWindowBackend* GetWindowBackend() noexcept override;
        [[nodiscard]] application::PlatformStatus ShowOpenFileDialog(const application::PlatformOpenFileDialogRequest& request,
                                                                      application::PlatformFileDialogCallback callback,
                                                                      void* userData) noexcept override;
        [[nodiscard]] application::PlatformStatus RevealFileLocation(const char* utf8FilePath) noexcept override;
        [[nodiscard]] bool AttachWindowEventSink(window::IWindowEventSink* sink) noexcept override;
        void DetachWindowEventSink(window::IWindowEventSink* sink) noexcept override;
        void Shutdown() noexcept override;

    private:
        input::IInputBackend* m_inputBackend = nullptr;
        window::sdl::SdlWindowBackend* m_windowBackend = nullptr;
        window::IWindowEventSink* m_windowEventSink = nullptr;
        char m_executablePath[1024]{};
        bool m_initialized = false;
    };
} // namespace vanguard::platform::windows
