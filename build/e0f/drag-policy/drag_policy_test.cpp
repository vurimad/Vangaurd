#include "imgui.h"
#include "imgui_internal.h"
#include <cstdio>
#include <cstdlib>

static ImVec2 nativePosition;
static int nativeMoves = 0;

static void Check(bool condition, const char* message)
{
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

int main()
{
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(1000, 700);
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_ViewportsEnable;
    io.BackendFlags |= ImGuiBackendFlags_PlatformHasViewports | ImGuiBackendFlags_RendererHasViewports;
    unsigned char* pixels; int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    io.Fonts->SetTexID(1);
    ImGuiPlatformIO& platform = ImGui::GetPlatformIO();
    platform.Platform_CreateWindow = [](ImGuiViewport*) {};
    platform.Platform_DestroyWindow = [](ImGuiViewport* v) { v->PlatformHandle = nullptr; };
    platform.Platform_ShowWindow = [](ImGuiViewport*) {};
    platform.Platform_SetWindowTitle = [](ImGuiViewport*, const char*) {};
    platform.Platform_SetWindowFocus = [](ImGuiViewport*) {};
    platform.Platform_GetWindowFocus = [](ImGuiViewport*) { return false; };
    platform.Platform_GetWindowMinimized = [](ImGuiViewport*) { return false; };
    platform.Platform_SetWindowPos = [](ImGuiViewport*, ImVec2 p) { nativePosition = p; ++nativeMoves; };
    platform.Platform_GetWindowPos = [](ImGuiViewport*) { return nativePosition; };
    platform.Platform_SetWindowSize = [](ImGuiViewport* v, ImVec2 s) { v->Size = s; };
    platform.Platform_GetWindowSize = [](ImGuiViewport* v) { return v->Size; };
    ImGuiPlatformMonitor monitor;
    monitor.MainSize = monitor.WorkSize = ImVec2(3000, 2000);
    platform.Monitors.push_back(monitor);
    ImGuiViewport* main = ImGui::GetMainViewport();
    main->PlatformHandle = reinterpret_cast<void*>(1);

    auto begin = [&](ImVec2 mouse) { io.AddMousePosEvent(mouse.x, mouse.y); ImGui::NewFrame(); ImGui::DockSpaceOverViewport(0, main); };
    auto end = [&]() { ImGui::End(); ImGui::Render(); ImGui::UpdatePlatformWindows(); };
    begin(ImVec2(100, 100));
    ImGui::SetNextWindowPos(ImVec2(50, 50));
    ImGui::SetNextWindowSize(ImVec2(900, 600));
    ImGui::Begin("Panel");
    end();
    io.AddMouseButtonEvent(0, true);
    begin(ImVec2(300, 100));
    ImGuiWindow* panel = ImGui::FindWindowByName("Panel");
    ImGui::StartMouseMovingWindow(panel);
    ImGui::SetWindowPos(panel, ImVec2(200, 50), ImGuiCond_Always);
    ImGui::Begin("Panel");
    Check(panel->Viewport == main && !panel->ViewportOwned, "internal drag must not detach when panel edges protrude");
    end();

    begin(ImVec2(1100, 100));
    ImGui::SetWindowPos(panel, ImVec2(1000, 50), ImGuiCond_Always);
    ImGui::Begin("Panel");
    Check(panel->Viewport != main && panel->ViewportOwned, "leaving host must still detach");
    end();
    int previousMoves = nativeMoves;
    panel->Viewport->PlatformRequestMove = true;
    begin(ImVec2(1150, 100));
    ImGui::Begin("Panel");
    end();
    Check(nativeMoves == previousMoves, "echoed move request reproduces suppression of the next native drag move");
    previousMoves = nativeMoves;
    // Matching acknowledgement is not an external move request.
    panel->Viewport->PlatformRequestMove = false;
    begin(ImVec2(1200, 100));
    ImGui::Begin("Panel");
    end();
    Check(nativeMoves > previousMoves, "ignoring matching acknowledgement lets native dragging progress");
    ImGui::DestroyPlatformWindows();
    main->PlatformHandle = nullptr;
    ImGui::DestroyContext();
    std::puts("PASS: internal drag stays hosted; external drag detaches; move echo suppresses native update; acknowledgement allows it");
}
