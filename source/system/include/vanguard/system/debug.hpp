#pragma once

#include <string_view>

namespace vanguard::system
{
    [[nodiscard]] bool IsDebuggerAttached() noexcept;
    void WriteDebugMessage(std::string_view message) noexcept;
    void BreakIntoDebugger() noexcept;
    [[noreturn]] void FailFast() noexcept;
} // namespace vanguard::system
