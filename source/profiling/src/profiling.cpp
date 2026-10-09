#include <vanguard/profiling/profiling.hpp>

#if VG_ENABLE_PROFILING
#ifndef TRACY_ENABLE
#define TRACY_ENABLE
#endif
#include <tracy/Tracy.hpp>

#include <cstring>
#include <new>
#endif

namespace vanguard::profiling
{
    static_assert(Enabled == (VG_ENABLE_PROFILING != 0));

#if VG_ENABLE_PROFILING
    namespace
    {
        using TracySourceLocation = tracy::SourceLocationData;
        using TracyScope = tracy::ScopedZone;
        static_assert(sizeof(TracySourceLocation) <= 64);
        static_assert(alignof(TracySourceLocation) <= 16);
        static_assert(sizeof(TracyScope) <= 32);
        static_assert(alignof(TracyScope) <= 16);
    }

    SourceLocation::SourceLocation(const char* const name, const char* const function, const char* const file, const unsigned int line) noexcept
    {
        static_cast<void>(new (m_storage) TracySourceLocation{name, function != nullptr ? function : "", file != nullptr ? file : "", line, 0});
    }

    Scope::Scope(const SourceLocation& sourceLocation) noexcept
    {
        const auto* const tracyLocation = std::launder(reinterpret_cast<const TracySourceLocation*>(sourceLocation.m_storage));
        static_cast<void>(new (m_storage) TracyScope(tracyLocation, -1, true));
    }

    Scope::~Scope()
    {
        std::launder(reinterpret_cast<TracyScope*>(m_storage))->~TracyScope();
    }

    void Scope::SetName(const char* const name) noexcept
    {
        if (name == nullptr)
            return;
        std::launder(reinterpret_cast<TracyScope*>(m_storage))->Name(name, std::strlen(name));
    }

    void Scope::SetValue(const std::uint64_t value) noexcept
    {
        std::launder(reinterpret_cast<TracyScope*>(m_storage))->Value(value);
    }

    void MarkFrame() noexcept
    {
        tracy::Profiler::SendFrameMark(nullptr);
    }

    void SetThreadName(const char* const name) noexcept
    {
        tracy::SetThreadName(name != nullptr ? name : "");
    }
#endif
}
