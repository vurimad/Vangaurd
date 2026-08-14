#pragma once

#include <vanguard/packages/packages.hpp>

namespace vanguard::packages::backend
{
    [[nodiscard]] Result CompressLz4(const void* source, usize sourceSize, containers::DynamicArray<u8>& destination) noexcept;

    [[nodiscard]] Result DecompressLz4(const void* source, usize sourceSize, void* destination, usize destinationSize) noexcept;
} // namespace vanguard::packages::backend
