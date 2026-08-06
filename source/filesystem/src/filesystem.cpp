#include <vanguard/diagnostics/diagnostics.hpp>
#include <vanguard/filesystem/filesystem.hpp>

#include "filesystem_backend.hpp"

namespace vanguard::filesystem
{
    bool Initialize(const Config& config) noexcept
    {
        if (!vanguard::memory::IsInitialized() || !vanguard::diagnostics::IsInitialized() || !vanguard::containers::IsInitialized() ||
            !vanguard::io::IsInitialized())
        {
            return false;
        }

        return backend::Initialize(config);
    }

    void Shutdown() noexcept
    {
        backend::Shutdown();
    }

    bool IsInitialized() noexcept
    {
        return backend::IsInitialized();
    }

    Manager& GetManager() noexcept
    {
        return backend::GetManager();
    }
} // namespace vanguard::filesystem
