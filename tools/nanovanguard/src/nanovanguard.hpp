#pragma once

#include "command_line.hpp"

namespace vanguard::nanovanguard
{
    [[nodiscard]] bool RegisterBuiltinCommands(CommandRegistry& registry) noexcept;
}
