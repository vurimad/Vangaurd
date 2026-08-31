#pragma once

#include <vanguard/system/types.hpp>

namespace vanguard::system
{
    /// Monotonic high-resolution clock used for engine intervals. Values have no wall-clock meaning.
    [[nodiscard]] u64 GetMonotonicTicks() noexcept;
    [[nodiscard]] u64 GetMonotonicFrequency() noexcept;
} // namespace vanguard::system
