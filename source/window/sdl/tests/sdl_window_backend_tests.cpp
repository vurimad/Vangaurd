#include <vanguard/memory/memory.hpp>
#include <vanguard/window/sdl/sdl_window_backend.hpp>
#include <vanguard/window/window_manager.hpp>

#include <SDL3/SDL.h>

#include <cstdio>

int main()
{
    using namespace vanguard;
    using namespace vanguard::window;
    using namespace vanguard::window::sdl;

    if (!memory::Initialize())
        return 1;
    static_cast<void>(SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy"));
    SdlWindowBackend backend;
    if (!backend.Initialize())
        return 2;

    BackendDisplaySnapshot displays[MaximumDisplays]{};
    u32 displayCount = 0;
    if (!backend.EnumerateDisplays(displays, MaximumDisplays, displayCount) || displayCount == 0)
        return 3;

    BackendWindowDescriptor descriptor;
    descriptor.title = "Vanguard SDL backend test";
    descriptor.placement.display = displays[0].id;
    descriptor.placement.logicalExtent = {640, 360};
    descriptor.initialPlacement = InitialWindowPlacement::CenteredOnDisplay;
    descriptor.placement.visible = false;
    descriptor.constraints = {{320, 180}, {1920, 1080}};
    BackendWindowId window;
    BackendWindowState state;
    if (!backend.Create(descriptor, window, state) || !window.IsValid())
        return 4;
    if (state.placement.logicalExtent.width != 640 || state.placement.logicalExtent.height != 360)
        return 5;

    BackendWindowRequest request;
    request.fields = WindowStateField::LogicalExtent;
    request.placement = state.placement;
    request.placement.logicalExtent = {800, 450};
    if (!backend.ApplyWindowState(window, request, state))
        return 6;
    if (state.placement.logicalExtent.width != 800 || state.placement.logicalExtent.height != 450)
        return 7;

    SDL_Event close{};
    close.type = SDL_EVENT_WINDOW_CLOSE_REQUESTED;
    close.window.windowID = static_cast<SDL_WindowID>(window.value);
    close.window.timestamp = 123;
    BackendWindowEvent translated;
    const EventTranslation translation = backend.ProcessEvent(close, translated);
    if (!translation.windowEvent || translation.displayTopologyChanged || translated.type != BackendEventType::CloseRequested || translated.window != window ||
        translated.timestampNanoseconds != 123)
        return 8;

    SDL_Event display{};
    display.type = SDL_EVENT_DISPLAY_CONTENT_SCALE_CHANGED;
    if (!backend.ProcessEvent(display, translated).displayTopologyChanged)
        return 9;
    if (!backend.DestroyWindow(window))
        return 10;

    WindowManager manager;
    Failure failure;
    if (!manager.Initialize(backend, &failure))
        return 11;
    WindowDescriptor managedDescriptor;
    managedDescriptor.title = "Vanguard managed SDL window";
    managedDescriptor.placement.display = manager.GetPrimaryDisplay();
    managedDescriptor.placement.logicalExtent = {960, 540};
    managedDescriptor.placement.visible = false;
    WindowHandle managedWindow;
    if (!manager.Create(managedDescriptor, managedWindow, &failure))
        return 12;
    WindowSnapshot snapshot;
    if (!manager.GetSnapshot(managedWindow, snapshot) || snapshot.nativeState.pixelExtent.width == 0 || snapshot.nativeState.pixelExtent.height == 0)
        return 13;
    if (!manager.DestroyWindow(managedWindow, &failure) || !manager.Shutdown(&failure))
        return 14;
    if (!backend.Shutdown())
        return 15;
    std::puts("[sdlWindowBackendTests] all tests passed");
    return 0;
}
