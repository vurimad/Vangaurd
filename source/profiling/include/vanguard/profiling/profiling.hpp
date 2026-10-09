#pragma once

#ifndef VG_ENABLE_PROFILING
#define VG_ENABLE_PROFILING 0
#endif

#include <cstddef>
#include <cstdint>

namespace vanguard::profiling
{
    inline constexpr bool Enabled = VG_ENABLE_PROFILING != 0;

#if VG_ENABLE_PROFILING
    class SourceLocation final
    {
    public:
        SourceLocation(const char* name, const char* function, const char* file, unsigned int line) noexcept;

        SourceLocation(const SourceLocation&) = delete;
        SourceLocation& operator=(const SourceLocation&) = delete;

    private:
        alignas(16) std::byte m_storage[64]{};

        friend class Scope;
    };

    class Scope final
    {
    public:
        explicit Scope(const SourceLocation& sourceLocation) noexcept;
        ~Scope();

        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;

        void SetName(const char* name) noexcept;
        void SetValue(std::uint64_t value) noexcept;

    private:
        alignas(16) std::byte m_storage[32]{};
    };

    void MarkFrame() noexcept;
    void SetThreadName(const char* name) noexcept;
#endif
}

#if VG_ENABLE_PROFILING
#define VG_PROFILE_DETAIL_CONCAT_INNER(left, right) left##right
#define VG_PROFILE_DETAIL_CONCAT(left, right) VG_PROFILE_DETAIL_CONCAT_INNER(left, right)
#define VG_PROFILE_ZONE() static ::vanguard::profiling::SourceLocation VG_PROFILE_DETAIL_CONCAT(vgProfileLocation_, __LINE__)(nullptr, __FUNCTION__, __FILE__, __LINE__); ::vanguard::profiling::Scope VG_PROFILE_DETAIL_CONCAT(vgProfileZone_, __LINE__)(VG_PROFILE_DETAIL_CONCAT(vgProfileLocation_, __LINE__))
#define VG_PROFILE_ZONE_NAMED(name) static ::vanguard::profiling::SourceLocation VG_PROFILE_DETAIL_CONCAT(vgProfileLocation_, __LINE__)((name), __FUNCTION__, __FILE__, __LINE__); ::vanguard::profiling::Scope VG_PROFILE_DETAIL_CONCAT(vgProfileZone_, __LINE__)(VG_PROFILE_DETAIL_CONCAT(vgProfileLocation_, __LINE__))
#define VG_PROFILE_ZONE_DYNAMIC(name) static ::vanguard::profiling::SourceLocation VG_PROFILE_DETAIL_CONCAT(vgProfileLocation_, __LINE__)(nullptr, __FUNCTION__, __FILE__, __LINE__); ::vanguard::profiling::Scope VG_PROFILE_DETAIL_CONCAT(vgProfileZone_, __LINE__)(VG_PROFILE_DETAIL_CONCAT(vgProfileLocation_, __LINE__)); VG_PROFILE_DETAIL_CONCAT(vgProfileZone_, __LINE__).SetName(name)
#define VG_PROFILE_ZONE_DYNAMIC_VALUE(name, value) static ::vanguard::profiling::SourceLocation VG_PROFILE_DETAIL_CONCAT(vgProfileLocation_, __LINE__)(nullptr, __FUNCTION__, __FILE__, __LINE__); ::vanguard::profiling::Scope VG_PROFILE_DETAIL_CONCAT(vgProfileZone_, __LINE__)(VG_PROFILE_DETAIL_CONCAT(vgProfileLocation_, __LINE__)); VG_PROFILE_DETAIL_CONCAT(vgProfileZone_, __LINE__).SetName(name); VG_PROFILE_DETAIL_CONCAT(vgProfileZone_, __LINE__).SetValue(static_cast<std::uint64_t>(value))
#define VG_PROFILE_FRAME_MARK() ::vanguard::profiling::MarkFrame()
#define VG_PROFILE_THREAD_NAME(name) ::vanguard::profiling::SetThreadName(name)
#else
#define VG_PROFILE_ZONE() ((void)0)
#define VG_PROFILE_ZONE_NAMED(name) ((void)0)
#define VG_PROFILE_ZONE_DYNAMIC(name) ((void)0)
#define VG_PROFILE_ZONE_DYNAMIC_VALUE(name, value) ((void)0)
#define VG_PROFILE_FRAME_MARK() ((void)0)
#define VG_PROFILE_THREAD_NAME(name) ((void)0)
#endif
