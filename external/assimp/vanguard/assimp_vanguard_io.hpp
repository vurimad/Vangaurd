#pragma once

namespace Assimp
{
    class IOSystem;
}

namespace vanguard_assimp
{
    using OpenedFileCallback = void (*)(const char* path, void* userData) noexcept;

    [[nodiscard]] Assimp::IOSystem* CreateTrackingIOSystem(const char* sourceDirectory, OpenedFileCallback callback,
                                                           void* userData);
} // namespace vanguard_assimp
