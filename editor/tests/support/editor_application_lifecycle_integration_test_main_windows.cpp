// TEST SUPPORT ONLY: Windows entry point for the editor lifecycle integration-test executable.

#include <editor_application_lifecycle_integration_test_application.hpp>
#include <vanguard/platform/windows/windows_framework.hpp>

#include <Windows.h>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    vanguard::editor::testing::EditorApplicationLifecycleIntegrationTestApplication application;
    return vanguard::platform::windows::RunFramework(application);
}
