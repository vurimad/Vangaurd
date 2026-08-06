#include <vanguard/system/debug.hpp>

#include <cstdlib>

#include <vanguard/system/platform.hpp>

#if VG_PLATFORM_WINDOWS
#include <Windows.h>
#else
#include <cstdio>
#include <csignal>
#endif

namespace vanguard::system
{
    bool IsDebuggerAttached() noexcept
    {
#if VG_PLATFORM_WINDOWS
        return ::IsDebuggerPresent() != FALSE;
#else
        return false;
#endif
    }

    void WriteDebugMessage(const std::string_view message) noexcept
    {
#if VG_PLATFORM_WINDOWS
        constexpr std::size_t bufferCapacity = 2048;
        char buffer[bufferCapacity];
        const std::size_t length = message.size() < (bufferCapacity - 1) ? message.size() : (bufferCapacity - 1);

        for (std::size_t index = 0; index < length; ++index)
        {
            buffer[index] = message[index];
        }
        buffer[length] = '\0';
        ::OutputDebugStringA(buffer);

        const HANDLE stderrHandle = ::GetStdHandle(STD_ERROR_HANDLE);
        if (stderrHandle != nullptr && stderrHandle != INVALID_HANDLE_VALUE)
        {
            DWORD bytesWritten = 0;
            ::WriteFile(stderrHandle, message.data(), static_cast<DWORD>(message.size()), &bytesWritten, nullptr);
        }
#else
        std::fwrite(message.data(), sizeof(char), message.size(), stderr);
#endif
    }

    void BreakIntoDebugger() noexcept
    {
        if (!IsDebuggerAttached())
        {
            return;
        }

#if VG_PLATFORM_WINDOWS
        __debugbreak();
#else
        std::raise(SIGTRAP);
#endif
    }

    [[noreturn]] void FailFast() noexcept
    {
#if VG_PLATFORM_WINDOWS
        ::RaiseFailFastException(nullptr, nullptr, 0);
#endif
        std::abort();
    }
} // namespace vanguard::system
