#include <vanguard/filesystem/filesystem_backend.hpp>

#include "../../imported/common/redCompression/include/compression.h"

namespace
{
    ::red::UniquePtr<::CFileManager, vanguard::memory::pools::Filesystem> g_manager;
    bool g_initialized = false;
} // namespace

namespace vanguard::filesystem::backend
{
    bool Initialize(const Config& config) noexcept
    {
        if (g_initialized)
        {
            return true;
        }

        ::compression::InitializeMemoryPools();
        g_manager = ::red::CreateUniquePtr<::CFileManager, vanguard::memory::pools::Filesystem>(config.engineRoot, config.gameRoot, config.cacheRoot);
        if (!g_manager)
        {
            return false;
        }

        ::GFileManager = g_manager.Get();
        g_initialized = true;
        return true;
    }

    void Shutdown() noexcept
    {
        if (!g_initialized)
        {
            return;
        }

        ::GFileManager = nullptr;
        g_manager.Reset();
        g_initialized = false;
    }

    bool IsInitialized() noexcept
    {
        return g_initialized;
    }

    Manager& GetManager() noexcept
    {
        RED_FATAL_ASSERT(g_manager);
        return *g_manager;
    }
} // namespace vanguard::filesystem::backend
