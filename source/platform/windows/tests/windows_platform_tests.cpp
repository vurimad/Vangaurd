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
        app::CompositionStatus Compose(const app::ApplicationStartupContext&, app::EngineHost& services, app::ApplicationStateMachine& states) noexcept override
        {
            if (!services.RegisterModule({1, "platform-test", 1}) || !states.RegisterState({1, "exit", &state}) || !states.SetInitialState(1))
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
        vanguard::window::WindowEventSinkResult ProcessWindowEvent(const vanguard::window::BackendWindowEvent&) noexcept override
        {
            return {vanguard::window::BackendStatus::Success(), vanguard::window::WindowEventSinkAction::Continue};
        }
        vanguard::window::WindowHandle ResolveWindow(const vanguard::window::BackendWindowId candidate) const noexcept override
        {
            return candidate == backendWindow ? window : vanguard::window::WindowHandle{};
        }
    };
} // namespace

int main()
{
    TestApplication application;
    const vanguard::i32 result = vanguard::platform::windows::RunFramework(application);
    bool passed = result == 0 && application.state.enterCalls == 1 && application.state.tickCalls == 1 && application.state.exitCalls == 1;

    vanguard::platform::windows::WindowsPlatformHost platform;
    const vanguard::application::PlatformStartupInfo startup{"platformInputTranslationTests", vanguard::application::ApplicationProfile::Test | vanguard::application::ApplicationProfile::Editor, {}};
    passed = passed && static_cast<bool>(platform.Initialize(startup)) && platform.GetInputBackend() != nullptr && platform.GetWindowBackend() != nullptr;
    passed = passed && SDL_GetHintBoolean(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, false);
    vanguard::window::BackendDisplaySnapshot displays[vanguard::window::MaximumDisplays]{};
    vanguard::u32 displayCount = 0;
    passed = passed && static_cast<bool>(platform.GetWindowBackend()->EnumerateDisplays(displays, vanguard::window::MaximumDisplays, displayCount)) &&
             displayCount != 0;
    vanguard::window::BackendWindowDescriptor windowDescriptor;
    windowDescriptor.title = "platform input target";
    windowDescriptor.placement.display = displays[0].id;
    windowDescriptor.placement.visible = false;
    vanguard::window::BackendWindowState windowState;
    TestWindowSink sink;
    passed = passed && static_cast<bool>(platform.GetWindowBackend()->Create(windowDescriptor, sink.backendWindow, windowState)) &&
             platform.AttachWindowEventSink(&sink);
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
    const vanguard::input::BackendDrainResult drain = platform.GetInputBackend()->Drain(translated, 4);
    passed = passed && pump.action == vanguard::application::PlatformPumpAction::Continue && drain.count == 1 &&
             translated[0].type == vanguard::input::EventType::KeyChanged && translated[0].data.key.key == vanguard::input::Key::W &&
             translated[0].data.key.pressed && translated[0].timestampNanoseconds == 1234 && translated[0].window == sink.window;

    // A deferred consumer must retain the origin used during translation, not rebase onto a subsequently moved window.
    SDL_Window* const nativeWindow = SDL_GetWindowFromID(static_cast<SDL_WindowID>(sink.backendWindow.value));
    int originX = 0;
    int originY = 0;
    passed = passed && nativeWindow != nullptr && SDL_GetWindowPosition(nativeWindow, &originX, &originY);
    passed = passed && static_cast<bool>(platform.GetWindowBackend()->SetWindowOpacity(sink.backendWindow, 0.5f)) && SDL_GetWindowOpacity(nativeWindow) == 0.5f;
    passed = passed && static_cast<bool>(platform.GetWindowBackend()->SetWindowOpacity(sink.backendWindow, 1.0f)) && SDL_GetWindowOpacity(nativeWindow) == 1.0f;
    SDL_Window* const focusBeforeShow = SDL_GetKeyboardFocus();
    const bool activationHintBeforeShow = SDL_GetHintBoolean(SDL_HINT_WINDOW_ACTIVATE_WHEN_SHOWN, true);
    vanguard::window::BackendWindowRequest showRequest;
    showRequest.fields = vanguard::window::WindowStateField::Visibility;
    showRequest.placement = windowState.placement;
    showRequest.placement.visible = true;
    showRequest.activateWhenShown = false;
    passed = passed && static_cast<bool>(platform.GetWindowBackend()->ApplyWindowState(sink.backendWindow, showRequest, windowState)) &&
             SDL_GetKeyboardFocus() == focusBeforeShow && SDL_GetHintBoolean(SDL_HINT_WINDOW_ACTIVATE_WHEN_SHOWN, true) == activationHintBeforeShow;
    showRequest.placement.visible = false;
    passed = passed && static_cast<bool>(platform.GetWindowBackend()->ApplyWindowState(sink.backendWindow, showRequest, windowState));
    source = {};
    source.type = SDL_EVENT_MOUSE_MOTION;
    source.motion.windowID = static_cast<SDL_WindowID>(sink.backendWindow.value);
    source.motion.timestamp = 5678;
    source.motion.x = 17.0f;
    source.motion.y = 23.0f;
    source.motion.xrel = 3.0f;
    source.motion.yrel = -2.0f;
    passed = passed && SDL_PushEvent(&source);
    passed = passed && platform.PumpEvents().action == app::PlatformPumpAction::Continue;
    passed = passed && SDL_SetWindowPosition(nativeWindow, originX + 120, originY + 80);
    vanguard::input::RawEvent motionEvents[64]{};
    const auto motionDrain = platform.GetInputBackend()->Drain(motionEvents, 64);
    bool foundMotion = false;
    for (vanguard::u32 index = 0; index < motionDrain.count; ++index)
    {
        const auto& event = motionEvents[index];
        if (event.type != vanguard::input::EventType::MouseMoved || event.timestampNanoseconds != 5678)
            continue;
        const auto& motion = event.data.mouseMotion;
        foundMotion = event.window == sink.window && motion.x == 17.0f && motion.y == 23.0f && motion.deltaX == 3.0f && motion.deltaY == -2.0f &&
                      motion.hasDesktopPosition && motion.desktopX == static_cast<vanguard::f32>(originX) + 17.0f && motion.desktopY == static_cast<vanguard::f32>(originY) + 23.0f;
    }
    passed = passed && foundMotion;
    if (!foundMotion)
        std::fprintf(stderr, "[platformWindowsTests] deferred desktop mouse coordinates were not preserved\n");
    platform.DetachWindowEventSink(&sink);
    passed = passed && static_cast<bool>(platform.GetWindowBackend()->DestroyWindow(sink.backendWindow));
    platform.Shutdown();
    if (passed)
        std::printf("[platformWindowsTests] all tests passed\n");
    return passed ? 0 : 1;
}
