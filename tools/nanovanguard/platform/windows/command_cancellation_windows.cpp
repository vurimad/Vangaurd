#include <vanguard/nanovanguard/command_cancellation.hpp>

#include <Windows.h>

namespace
{
    volatile LONG g_cancelRequested = 0;

    BOOL WINAPI NanovanguardConsoleHandler(const DWORD controlType) noexcept
    {
        if (controlType != CTRL_C_EVENT && controlType != CTRL_BREAK_EVENT)
            return FALSE;
        ::InterlockedExchange(&g_cancelRequested, 1);
        return TRUE;
    }
} // namespace

namespace vanguard::nanovanguard
{
    void ResetCommandCancellation() noexcept
    {
        ::InterlockedExchange(&g_cancelRequested, 0);
    }

    void RequestCommandCancellation() noexcept
    {
        ::InterlockedExchange(&g_cancelRequested, 1);
    }

    bool IsCommandCancellationRequested() noexcept
    {
        return ::InterlockedCompareExchange(&g_cancelRequested, 0, 0) != 0;
    }

    bool InstallCommandCancellationHandler() noexcept
    {
        return ::SetConsoleCtrlHandler(&NanovanguardConsoleHandler, TRUE) != FALSE;
    }

    void RemoveCommandCancellationHandler() noexcept
    {
        static_cast<void>(::SetConsoleCtrlHandler(&NanovanguardConsoleHandler, FALSE));
    }
} // namespace vanguard::nanovanguard
