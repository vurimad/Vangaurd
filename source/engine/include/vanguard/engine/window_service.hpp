#pragma once

#include <vanguard/application/engine_host.hpp>
#include <vanguard/window/window_manager.hpp>

namespace vanguard::application
{
    class IPlatformHost;
}

namespace vanguard::engine
{
    struct WindowServiceConfig
    {
        application::IPlatformHost* platform = nullptr;
        window::WindowExtent primaryLogicalExtent = window::WindowPlacement{}.logicalExtent;
    };

    enum class WindowFileDropEventType : u8
    {
        Begin,
        File,
        Complete
    };

    struct WindowFileDropEvent final
    {
        WindowFileDropEventType type = WindowFileDropEventType::Begin;
        window::WindowHandle window;
        window::WindowPoint position;
        u64 timestampNanoseconds = 0u;
        // Borrowed only for the synchronous File callback. The sink must copy it
        // before returning; no window-service journal stores this pointer.
        const char* path = nullptr;
    };

    class IWindowFileDropSink
    {
    public:
        virtual ~IWindowFileDropSink() = default;
        [[nodiscard]] virtual bool ProcessFileDrop(const WindowFileDropEvent& event) noexcept = 0;

    protected:
        IWindowFileDropSink() noexcept = default;
    };

    class WindowService : public application::Service
    {
    public:
        ~WindowService() override = default;

        [[nodiscard]] virtual window::WindowManager& GetManager() noexcept = 0;
        [[nodiscard]] virtual const window::WindowManager& GetManager() const noexcept = 0;
        [[nodiscard]] virtual window::WindowHandle GetPrimaryWindow() const noexcept = 0;
        [[nodiscard]] virtual bool AttachFileDropSink(IWindowFileDropSink* sink) noexcept = 0;
        virtual void DetachFileDropSink(IWindowFileDropSink* sink) noexcept = 0;

    protected:
        WindowService() noexcept = default;
    };

    [[nodiscard]] WindowService* FindWindowService(application::EngineHost& host) noexcept;
    [[nodiscard]] WindowService* FindWindowService(application::ServiceContext& context) noexcept;
} // namespace vanguard::engine
