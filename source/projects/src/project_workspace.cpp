#include <vanguard/projects/project_workspace.hpp>

namespace vanguard::projects
{
    Result ResolveWorkspace(const filesystem::AbsolutePath& projectFile, const ProjectDescriptor& project, ProjectWorkspace& workspace, Diagnostic* const diagnostic) noexcept
    {
        if (diagnostic != nullptr)
            *diagnostic = {};
        const Result validated = Validate(project, diagnostic);
        if (validated != Result::Success)
            return validated;

        ProjectWorkspace resolved;
        resolved.projectFile = projectFile;
        resolved.root = filesystem::paths::ParentAbsolutePath(projectFile);
        if (!projectFile.IsFilePath() || resolved.root.Empty())
        {
            if (diagnostic != nullptr)
                *diagnostic = {Result::InvalidArgument, 0, 0, "projectFile", "project document requires an absolute file path with a parent directory"};
            return Result::InvalidArgument;
        }

        containers::String filename(project.technicalName);
        filename.Append(".vproject", 9);
        if (projectFile != resolved.root.AddFilePath(filename))
        {
            if (diagnostic != nullptr)
                *diagnostic = {Result::InvalidValue, 0, 0, "projectFile", "project document filename must match project.technicalName"};
            return Result::InvalidValue;
        }

        resolved.assets = resolved.root.AddDirPath(project.assets);
        resolved.derivedData = resolved.root.AddDirPath(project.derivedData);
        resolved.intermediate = resolved.root.AddDirPath(project.intermediate);
        resolved.saved = resolved.root.AddDirPath(project.saved);
        resolved.builds = resolved.root.AddDirPath(project.builds);
        resolved.config = resolved.root.AddDirPath(project.config);
        resolved.plugins = resolved.root.AddDirPath(project.pluginsRoot);
        const filesystem::AbsolutePath* roots[]{&resolved.assets, &resolved.derivedData, &resolved.intermediate, &resolved.saved, &resolved.builds, &resolved.config, &resolved.plugins};
        for (u32 i = 0; i < 7; ++i)
        {
            if (!filesystem::paths::IsSubpath(resolved.root, *roots[i]) || *roots[i] == resolved.root)
            {
                if (diagnostic != nullptr)
                    *diagnostic = {Result::InvalidValue, 0, 0, "paths", "project roots must remain beneath the project directory"};
                return Result::InvalidValue;
            }
            for (u32 j = 0; j < i; ++j)
            {
                // Match AbsolutePath's case-insensitive equality for root aliases.
                if (roots[i]->AsStringView().StartsWithIgnoreCase(roots[j]->AsStringView()) || roots[j]->AsStringView().StartsWithIgnoreCase(roots[i]->AsStringView()))
                {
                    if (diagnostic != nullptr)
                        *diagnostic = {Result::InvalidValue, 0, 0, "paths", "project roots must not overlap"};
                    return Result::InvalidValue;
                }
            }
        }
        workspace = static_cast<ProjectWorkspace&&>(resolved);
        return Result::Success;
    }
} // namespace vanguard::projects
