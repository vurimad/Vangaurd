#include "../src/io_backend.hpp"

namespace
{
    bool g_vanguardIOInitialized = false;
    bool g_vanguardIOTerminated = false;
}

namespace vanguard::io::backend
{
    bool Initialize(const InitSetup& setup) noexcept
    {
        if (g_vanguardIOInitialized)
        {
            return true;
        }

        // RED's global AsyncIO instance supports one Init/Shutdown lifetime.
        // Reject reinitialization explicitly instead of entering invalid state.
        if (g_vanguardIOTerminated || !::io::Initialize(setup))
        {
            return false;
        }

        g_vanguardIOInitialized = true;
        return true;
    }

    void Shutdown() noexcept
    {
        if (!g_vanguardIOInitialized)
        {
            return;
        }

        ::io::Shutdown();
        g_vanguardIOInitialized = false;
        g_vanguardIOTerminated = true;
    }

    bool IsInitialized() noexcept
    {
        return g_vanguardIOInitialized;
    }

    AsyncIO& System() noexcept
    {
        return ::io::GAsyncIO;
    }
}
