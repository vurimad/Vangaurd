#pragma once

#include <vanguard/filesystem/filesystem.hpp>

namespace vanguard::filesystem::backend
{
    [[nodiscard]] bool Initialize(const Config& config) noexcept;
    void Shutdown() noexcept;
    [[nodiscard]] bool IsInitialized() noexcept;
    [[nodiscard]] Manager& GetManager() noexcept;
} // namespace vanguard::filesystem::backend
