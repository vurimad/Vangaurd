#pragma once

#include <vanguard/nanovanguard/command_line.hpp>

namespace vanguard::nanovanguard
{
    // Registers the GPU-free, command-scoped E2.6 P3 asset frontend. This is
    // deliberately separate from RegisterBuiltinCommands so document-only tests
    // do not acquire ProjectAssetContext or the heavy project-asset link closure.
    [[nodiscard]] bool RegisterProjectAssetCommands(CommandRegistry& registry) noexcept;
} // namespace vanguard::nanovanguard
