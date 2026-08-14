#pragma once

#include <vanguard/nanovanguard/command_line.hpp>

namespace vanguard::nanovanguard
{
    [[nodiscard]] bool RegisterBuiltinCommands(CommandRegistry& registry) noexcept;
}
