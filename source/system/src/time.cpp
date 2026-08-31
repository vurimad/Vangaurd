#include <vanguard/system/time.hpp>

#include <vanguard/system/platform.hpp>

#if VG_PLATFORM_WINDOWS
#include <Windows.h>
#elif VG_PLATFORM_LINUX
#include <time.h>
#endif

namespace vanguard::system
{
    u64 GetMonotonicTicks() noexcept
    {
#if VG_PLATFORM_WINDOWS
        LARGE_INTEGER value;
        return ::QueryPerformanceCounter(&value) != 0 ? static_cast<u64>(value.QuadPart) : 0;
#elif VG_PLATFORM_LINUX
        timespec value{};
        if (::clock_gettime(CLOCK_MONOTONIC_RAW, &value) != 0)
            return 0;
        return static_cast<u64>(value.tv_sec) * 1'000'000'000ull + static_cast<u64>(value.tv_nsec);
#endif
    }

    u64 GetMonotonicFrequency() noexcept
    {
#if VG_PLATFORM_WINDOWS
        LARGE_INTEGER value;
        return ::QueryPerformanceFrequency(&value) != 0 ? static_cast<u64>(value.QuadPart) : 0;
#elif VG_PLATFORM_LINUX
        return 1'000'000'000ull;
#endif
    }
} // namespace vanguard::system
