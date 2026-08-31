#pragma once

#include <vanguard/application/engine_host.hpp>
#include <vanguard/window/window_manager.hpp>

namespace vanguard::engine
{
    class WindowService : public application::Service
    {
    public:
        ~WindowService() override = default;

        [[nodiscard]] virtual window::WindowManager& GetManager() noexcept = 0;
        [[nodiscard]] virtual const window::WindowManager& GetManager() const noexcept = 0;
        [[nodiscard]] virtual window::WindowHandle GetPrimaryWindow() const noexcept = 0;

    protected:
        WindowService() noexcept = default;
    };

    [[nodiscard]] WindowService* FindWindowService(application::EngineHost& host) noexcept;
    [[nodiscard]] WindowService* FindWindowService(application::ServiceContext& context) noexcept;
} // namespace vanguard::engine
