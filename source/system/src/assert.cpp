#include <vanguard/system/assert.hpp>

#include <cstdio>

namespace
{
    constexpr std::size_t diagnosticCapacity = 4096;

    void WriteFailure(const char* category, const std::string_view expression, const std::string_view message, const std::source_location location) noexcept
    {
        char diagnostic[diagnosticCapacity];
        const int written = std::snprintf(diagnostic, diagnosticCapacity,
                                          "[Vanguard][%s]\n  Expression: %.*s\n  Message: %.*s\n"
                                          "  Source: %s(%u)\n  Function: %s\n",
                                          category, static_cast<int>(expression.size()), expression.data(), static_cast<int>(message.size()), message.data(),
                                          location.file_name(), location.line(), location.function_name());

        if (written <= 0)
        {
            vanguard::system::WriteDebugMessage("[Vanguard][Fatal] Failed to format diagnostic message.\n");
            return;
        }

        const std::size_t length = static_cast<std::size_t>(written) < diagnosticCapacity ? static_cast<std::size_t>(written) : diagnosticCapacity - 1;
        vanguard::system::WriteDebugMessage({diagnostic, length});
    }
} // namespace

namespace vanguard::system
{
    [[noreturn]] void ReportAssertionFailure(const std::string_view expression, const std::string_view message, const std::source_location location) noexcept
    {
        WriteFailure("Assertion", expression, message, location);
        BreakIntoDebugger();
        FailFast();
    }

    void ReportEnsureFailure(const std::string_view expression, const std::string_view message, const std::source_location location) noexcept
    {
        WriteFailure("Ensure", expression, message, location);
    }

    [[noreturn]] void ReportFatalFailure(const std::string_view message, const std::source_location location) noexcept
    {
        WriteFailure("Fatal", {}, message, location);
        BreakIntoDebugger();
        FailFast();
    }
} // namespace vanguard::system
