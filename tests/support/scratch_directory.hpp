#pragma once

#include <vanguard/filesystem/filesystem.hpp>
#include <vanguard/system/assert.hpp>

namespace vanguard::tests
{
    // Fixtures are disposable and independent of the directory used to launch
    // the executable. Authored test inputs must keep their own source root.
    inline filesystem::AbsolutePath ScratchDirectory(const char* suite)
    {
#if VG_BUILD_DEBUG
        constexpr const char* configuration = "Debug";
#elif VG_BUILD_DEVELOPMENT
        constexpr const char* configuration = "Development";
#elif VG_BUILD_PROFILE
        constexpr const char* configuration = "Profile";
#else
        constexpr const char* configuration = "Shipping";
#endif
        const auto directory = filesystem::paths::GetTempDirectory().AddDirPath("VanguardTests").AddDirPath(configuration).AddDirPath(suite);
        const bool created = filesystem::SystemIO::CreatePath(directory.AsChar());
        if (!created)
            VG_FATAL("Could not create test scratch directory");
        return directory;
    }
}
