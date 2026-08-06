#pragma once

#include <vanguard/reflection/reflection.hpp>

namespace vanguard::reflection::backend
{
    [[nodiscard]] bool Initialize() noexcept;
    [[nodiscard]] bool IsInitialized() noexcept;
    [[nodiscard]] TypeDescriptor FindType(const char* name) noexcept;
    [[nodiscard]] TypeDescriptor FindTypeByHash(u64 nameHash) noexcept;
} // namespace vanguard::reflection::backend
