// TEST SUPPORT ONLY: Windows entry point for the runtime world-frame integration-test executable.

#include <runtime_world_frame_integration_test_application.hpp>
#include <vanguard/platform/windows/windows_framework.hpp>

#include <Windows.h>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    vanguard::runtime::testing::RuntimeWorldFrameIntegrationTestApplication application;
    return vanguard::platform::windows::RunFramework(application);
}
