#include "../src/containers_backend.hpp"

#include "../../imported/common/redContainers/include/redContainersPublic.h"

namespace
{
    bool g_vanguardContainersInitialized = false;
}

namespace vanguard::containers::backend
{
    bool Initialize() noexcept
    {
        if (g_vanguardContainersInitialized)
        {
            return true;
        }

        red::InitializeContainerMemoryPools();
        g_vanguardContainersInitialized = true;
        return true;
    }

    bool IsInitialized() noexcept
    {
        return g_vanguardContainersInitialized;
    }
}
