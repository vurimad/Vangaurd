#pragma once

#include <source_location>
#include <string_view>

#include <vanguard/system/build_config.hpp>
#include <vanguard/system/debug.hpp>

namespace vanguard::system
{
    [[noreturn]] void ReportAssertionFailure(std::string_view expression, std::string_view message,
                                             std::source_location location = std::source_location::current()) noexcept;

    void ReportEnsureFailure(std::string_view expression, std::string_view message, std::source_location location = std::source_location::current()) noexcept;

    [[noreturn]] void ReportFatalFailure(std::string_view message, std::source_location location = std::source_location::current()) noexcept;
} // namespace vanguard::system

#if VG_ENABLE_ASSERTS
#define VG_ASSERT(expression)                                                                                                                                  \
    [&]() noexcept                                                                                                                                             \
    {                                                                                                                                                          \
        if (!(expression))                                                                                                                                     \
        {                                                                                                                                                      \
            ::vanguard::system::ReportAssertionFailure(#expression, {}, std::source_location::current());                                                      \
        }                                                                                                                                                      \
    }()

#define VG_ASSERT_MSG(expression, message)                                                                                                                     \
    [&]() noexcept                                                                                                                                             \
    {                                                                                                                                                          \
        if (!(expression))                                                                                                                                     \
        {                                                                                                                                                      \
            ::vanguard::system::ReportAssertionFailure(#expression, message, std::source_location::current());                                                 \
        }                                                                                                                                                      \
    }()
#else
#define VG_ASSERT(expression) static_cast<void>(sizeof(expression))
#define VG_ASSERT_MSG(expression, message)                                                                                                                     \
    [&]() noexcept                                                                                                                                             \
    {                                                                                                                                                          \
        static_cast<void>(sizeof(expression));                                                                                                                 \
        static_cast<void>(sizeof(message));                                                                                                                    \
    }()
#endif

#if VG_ENABLE_ASSERTS
#define VG_VERIFY(expression) VG_ASSERT(expression)
#else
#define VG_VERIFY(expression) static_cast<void>(expression)
#endif

#define VG_ENSURE(expression)                                                                                                                                  \
    [&]() noexcept -> bool                                                                                                                                     \
    {                                                                                                                                                          \
        const bool vgEnsureResult = !!(expression);                                                                                                            \
        if (!vgEnsureResult)                                                                                                                                   \
        {                                                                                                                                                      \
            ::vanguard::system::ReportEnsureFailure(#expression, {}, std::source_location::current());                                                         \
        }                                                                                                                                                      \
        return vgEnsureResult;                                                                                                                                 \
    }()

#define VG_ENSURE_MSG(expression, message)                                                                                                                     \
    [&]() noexcept -> bool                                                                                                                                     \
    {                                                                                                                                                          \
        const bool vgEnsureResult = !!(expression);                                                                                                            \
        if (!vgEnsureResult)                                                                                                                                   \
        {                                                                                                                                                      \
            ::vanguard::system::ReportEnsureFailure(#expression, message, std::source_location::current());                                                    \
        }                                                                                                                                                      \
        return vgEnsureResult;                                                                                                                                 \
    }()

#define VG_FATAL(message) ::vanguard::system::ReportFatalFailure(message, std::source_location::current())

#define VG_DEBUG_BREAK() ::vanguard::system::BreakIntoDebugger()
