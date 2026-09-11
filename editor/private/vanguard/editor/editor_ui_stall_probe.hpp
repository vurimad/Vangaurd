#pragma once

#include <vanguard/system/build_config.hpp>
#if VG_BUILD_DEBUG
#include <vanguard/system/time.hpp>
#include <vanguard/diagnostics/diagnostics.hpp>
#endif

namespace vanguard::editor::detail
{
    // Temporary E0F diagnostic. No allocation; only slow operations write to the log.
    class UiStallProbe final
    {
    public:
#if VG_BUILD_DEBUG
        explicit UiStallProbe(const char* name) noexcept : m_name(name), m_start(system::GetMonotonicTicks()) {}
        ~UiStallProbe()
        {
            const double milliseconds = 1000.0 * double(system::GetMonotonicTicks() - m_start) / double(system::GetMonotonicFrequency());
            if (milliseconds >= 100.0)
                VG_LOG_WARNING(diagnostics::Category::Engine, "UI stall: %s %.2f ms", m_name, milliseconds);
        }
    private:
        const char* m_name;
        u64 m_start;
#else
        explicit UiStallProbe(const char*) noexcept {}
#endif
    };
}
