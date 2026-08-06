#include <vanguard/diagnostics/diagnostics.hpp>
#include <vanguard/io/io.hpp>

#include "io_backend.hpp"

namespace vanguard::io
{
    bool Initialize(const InitSetup& setup) noexcept
    {
        if (!vanguard::memory::IsInitialized() || !vanguard::containers::IsInitialized() || !vanguard::diagnostics::IsInitialized())
        {
            return false;
        }

        return backend::Initialize(setup);
    }

    void Shutdown() noexcept
    {
        backend::Shutdown();
    }

    bool IsInitialized() noexcept
    {
        return backend::IsInitialized();
    }

    AsyncIO& System() noexcept
    {
        return backend::System();
    }

    const char* GetRequestSourceDebugText(const RequestSource source) noexcept
    {
        return ::io::GetRequestSourceDebugText(source);
    }
} // namespace vanguard::io
