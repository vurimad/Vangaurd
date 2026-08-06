#pragma once

#include <vanguard/application/framework.hpp>

namespace vanguard::platform::windows
{
    // Enters the portable framework from the native Windows process boundary. The complete process command line is
    // parsed according to Windows quoting rules and normalized to UTF-8 before application composition begins.
    [[nodiscard]] i32 RunFramework(Application& appInstance) noexcept;
} // namespace vanguard::platform::windows
