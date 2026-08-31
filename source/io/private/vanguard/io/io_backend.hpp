#pragma once

#include <vanguard/io/io.hpp>

namespace vanguard::io::backend
{
    [[nodiscard]] bool Initialize(const InitSetup& setup) noexcept;
    void Shutdown() noexcept;
    [[nodiscard]] bool IsInitialized() noexcept;
    [[nodiscard]] AsyncIO& GetSystem() noexcept;
} // namespace vanguard::io::backend
