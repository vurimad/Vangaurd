#include <vanguard/platform/windows/windows_framework.hpp>

#include "runtime_application.hpp"

#include <Windows.h>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    vanguard::runtime::RuntimeApplication application;
    return vanguard::platform::windows::RunFramework(application);
}
