#include <vanguard/containers/containers.hpp>

#include <vanguard/containers/containers_backend.hpp>

namespace vanguard::containers
{
    bool Initialize() noexcept
    {
        if (!vanguard::memory::IsInitialized())
        {
            return false;
        }
        return backend::Initialize();
    }

    bool IsInitialized() noexcept
    {
        return backend::IsInitialized();
    }
} // namespace vanguard::containers
