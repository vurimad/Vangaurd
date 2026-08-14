#pragma once

#include <vanguard/nanovanguard/command_line.hpp>

namespace vanguard::nanovanguard
{
    [[nodiscard]] bool RegisterProjectCommands(CommandRegistry& registry) noexcept;
}
