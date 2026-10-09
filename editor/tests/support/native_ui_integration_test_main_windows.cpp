// TEST SUPPORT ONLY: Windows process entry point for the dedicated native UI integration-test executable.

#include <native_ui_integration_test_application.hpp>
#include <vanguard/platform/windows/windows_framework.hpp>

#include <Windows.h>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    vanguard::editor::testing::NativeUiIntegrationTestApplication application;
    return vanguard::platform::windows::RunFramework(application);
}
