#pragma once

#include <vanguard/system/types.hpp>

namespace vanguard::system
{
    /// Monotonic high-resolution clock used for engine intervals. Values have no wall-clock meaning.
    [[nodiscard]] u64 MonotonicTicks() noexcept;
    [[nodiscard]] u64 MonotonicFrequency() noexcept;
}
