#pragma once

#include <vanguard/projects/project.hpp>

namespace vanguard::projects
{
    struct ProjectWorkspace
    {
        filesystem::AbsolutePath projectFile;
        filesystem::AbsolutePath root;
        filesystem::AbsolutePath assets;
        filesystem::AbsolutePath derivedData;
        filesystem::AbsolutePath intermediate;
        filesystem::AbsolutePath saved;
        filesystem::AbsolutePath builds;
        filesystem::AbsolutePath config;
        filesystem::AbsolutePath plugins;
    };

    // Resolves a validated document without filesystem initialization or I/O.
    // Output is unchanged on failure. Paths are lexical, not a symlink security boundary.
    [[nodiscard]] Result ResolveWorkspace(const filesystem::AbsolutePath& projectFile, const ProjectDescriptor& project, ProjectWorkspace& workspace, Diagnostic* diagnostic = nullptr) noexcept;
} // namespace vanguard::projects
