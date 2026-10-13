#pragma once

namespace Assimp
{
    class Importer;
    class IOSystem;
}

namespace vanguard_assimp
{
    using OpenedFileCallback = void (*)(const char* path, void* userData) noexcept;
    using ProgressCallback = bool (*)(float progress, void* userData) noexcept;

    [[nodiscard]] Assimp::IOSystem* CreateTrackingIOSystem(const char* sourceDirectory, OpenedFileCallback callback,
                                                           void* userData);
    void InstallProgressHandler(Assimp::Importer& importer, ProgressCallback callback, void* userData);
} // namespace vanguard_assimp
