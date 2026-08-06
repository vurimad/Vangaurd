#include <vanguard/platform/windows/windows_framework.hpp>

#include "editor_application.hpp"

#include <Windows.h>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    vanguard::editor::EditorApplication application;
    return vanguard::platform::windows::RunFramework(application);
}
