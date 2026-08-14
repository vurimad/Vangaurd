#pragma once

#include <vanguard/containers/containers.hpp>

namespace vanguard::nanovanguard::platform
{
    enum class PathKind : u8
    {
        Missing,
        File,
        Directory,
        Other,
        Failure
    };

    [[nodiscard]] bool MakeAbsolutePath(containers::StringView path, containers::String& absolutePath) noexcept;
    [[nodiscard]] PathKind GetPathKind(containers::StringView absolutePath) noexcept;
    [[nodiscard]] bool CreateDirectory(containers::StringView absolutePath) noexcept;
    [[nodiscard]] bool RemoveEmptyDirectory(containers::StringView absolutePath) noexcept;
    [[nodiscard]] bool DeleteFile(containers::StringView absolutePath) noexcept;
    [[nodiscard]] bool PublishDirectory(containers::StringView stagingPath, containers::StringView destinationPath) noexcept;
    [[nodiscard]] bool ReadFile(containers::StringView absolutePath, containers::String& contents) noexcept;
    [[nodiscard]] bool WriteFileDurable(containers::StringView absolutePath, containers::ArraySpan<const u8> contents) noexcept;
    [[nodiscard]] bool FindProjectFile(containers::StringView directory, containers::String& projectFile) noexcept;
    [[nodiscard]] bool GenerateRandomBytes(containers::ArraySpan<u8> bytes) noexcept;
    [[nodiscard]] u32 GetLastErrorCode() noexcept;
} // namespace vanguard::nanovanguard::platform
