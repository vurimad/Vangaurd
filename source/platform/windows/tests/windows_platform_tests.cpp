#include <vanguard/application/framework.hpp>
#include <vanguard/input/input.hpp>
#include <vanguard/platform/windows/windows_framework.hpp>
#include <vanguard/platform/windows/windows_platform_host.hpp>
#include <vanguard/window/window_backend.hpp>

#include <SDL3/SDL.h>

#include <cstdio>

namespace
{
    namespace app = vanguard::application;

    class ExitState final : public app::ApplicationState
    {
    public:
        vanguard::u32 enterCalls = 0;
        vanguard::u32 tickCalls = 0;
        vanguard::u32 exitCalls = 0;

    protected:
        app::StateOperationStatus OnEnter(app::StateContext&) noexcept override
        {
            ++enterCalls;
            return app::StateOperationStatus::Complete();
        }
        app::StateTickStatus OnTick(app::StateContext& context) noexcept override
        {
            ++tickCalls;
            static_cast<void>(context.RequestExit());
            return app::StateTickStatus::Success();
        }
        app::StateOperationStatus OnExit(app::StateContext&) noexcept override
        {
            ++exitCalls;
            return app::StateOperationStatus::Complete();
        }
    };

    class TestApplication final : public vanguard::Application
    {
    public:
        ExitState state;

        vanguard::ApplicationTraits GetTraits() const noexcept override
        {
            return {"platformWindowsTests", app::ApplicationProfile::Test, 32};
        }
        app::CompositionStatus Compose(const app::ApplicationStartupContext&, app::EngineHost& services,
                                       app::ApplicationStateMachine& states) noexcept override
        {
            if (!services.RegisterModule({1, "platform-test", 1}) || !states.RegisterState({1, "exit", &state}) ||
                !states.SetInitialState(1))
                return app::CompositionStatus::Failure("Windows platform test composition failed");
            return app::CompositionStatus::Success();
        }
    };

    class TestWindowSink final : public vanguard::window::IWindowEventSink
    {
    public:
        vanguard::window::BackendWindowId backendWindow;
        vanguard::window::WindowHandle window{3, 7};

        vanguard::window::BackendStatus RefreshDisplayTopology() noexcept override
        {
            return vanguard::window::BackendStatus::Success();
        }
        vanguard::window::WindowEventSinkResult ProcessWindowEvent(
            const vanguard::window::BackendWindowEvent&) noexcept override
        {
            return {vanguard::window::BackendStatus::Success(), vanguard::window::WindowEventSinkAction::Continue};
        }
        vanguard::window::WindowHandle ResolveWindow(
            const vanguard::window::BackendWindowId candidate) const noexcept override
        {
            return candidate == backendWindow ? window : vanguard::window::WindowHandle{};
        }
    };
}

int main()
{
    TestApplication application;
    const vanguard::i32 result = vanguard::platform::windows::RunFramework(application);
    bool passed = result == 0 && application.state.enterCalls == 1 && application.state.tickCalls == 1 &&
                  application.state.exitCalls == 1;

    vanguard::platform::windows::WindowsPlatformHost platform;
    const vanguard::application::PlatformStartupInfo startup{
        "platformInputTranslationTests", vanguard::application::ApplicationProfile::Test, {}};
    passed = passed && static_cast<bool>(platform.Initialize(startup)) && platform.InputBackend() != nullptr &&
             platform.WindowBackend() != nullptr;
    vanguard::window::BackendDisplaySnapshot displays[vanguard::window::MaximumDisplays]{};
    vanguard::u32 displayCount = 0;
    passed = passed && static_cast<bool>(platform.WindowBackend()->EnumerateDisplays(
        displays, vanguard::window::MaximumDisplays, displayCount)) && displayCount != 0;
    vanguard::window::BackendWindowDescriptor windowDescriptor;
    windowDescriptor.title = "platform input target";
    windowDescriptor.placement.display = displays[0].id;
    windowDescriptor.placement.visible = false;
    vanguard::window::BackendWindowState windowState;
    TestWindowSink sink;
    passed = passed && static_cast<bool>(platform.WindowBackend()->Create(
        windowDescriptor, sink.backendWindow, windowState)) && platform.AttachWindowEventSink(&sink);
    SDL_Event source{};
    source.type = SDL_EVENT_KEY_DOWN;
    source.key.timestamp = 1234;
    source.key.which = 7;
    source.key.windowID = static_cast<SDL_WindowID>(sink.backendWindow.value);
    source.key.scancode = SDL_SCANCODE_W;
    source.key.down = true;
    passed = passed && SDL_PushEvent(&source);
    const vanguard::application::PlatformPumpResult pump = platform.PumpEvents();
    vanguard::input::RawEvent translated[4]{};
    const vanguard::input::BackendDrainResult drain = platform.InputBackend()->Drain(translated, 4);
    passed = passed && pump.action == vanguard::application::PlatformPumpAction::Continue && drain.count == 1 &&
             translated[0].type == vanguard::input::EventType::KeyChanged &&
             translated[0].data.key.key == vanguard::input::Key::W && translated[0].data.key.pressed &&
             translated[0].timestampNanoseconds == 1234 && translated[0].window == sink.window;
    platform.DetachWindowEventSink(&sink);
    passed = passed && static_cast<bool>(platform.WindowBackend()->DestroyWindow(sink.backendWindow));
    platform.Shutdown();
    if (passed) std::printf("[platformWindowsTests] all tests passed\n");
    return passed ? 0 : 1;
}
