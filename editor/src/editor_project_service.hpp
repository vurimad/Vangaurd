#pragma once

#include <vanguard/application/engine_host.hpp>
#include <vanguard/filesystem/filesystem.hpp>
#include <vanguard/projects/project.hpp>

namespace vanguard::editor
{
    inline constexpr application::ModuleId EditorModuleId = 0x656469746f720001ull;
    inline constexpr application::ServiceId ProjectWorkspaceServiceId = 0x656470726f6a7301ull;
    inline constexpr application::CapabilityId ProjectWorkspaceCapabilityId = 0x656470726f6a6301ull;

    struct ProjectWorkspaceConfig
    {
        filesystem::AbsolutePath projectFile;
    };

    /// Owns the validated project document and every root derived from it for the lifetime of an editor workspace.
    /// Engine services remain project-agnostic and consume only the roots or resource identities they actually need.
    class ProjectWorkspaceService : public application::Service
    {
    public:
        ~ProjectWorkspaceService() override = default;

        [[nodiscard]] virtual const projects::ProjectDescriptor& Project() const noexcept = 0;
        [[nodiscard]] virtual const filesystem::AbsolutePath& ProjectFile() const noexcept = 0;
        [[nodiscard]] virtual const filesystem::AbsolutePath& ProjectRoot() const noexcept = 0;
        [[nodiscard]] virtual const filesystem::AbsolutePath& AssetsRoot() const noexcept = 0;
        [[nodiscard]] virtual const filesystem::AbsolutePath& DerivedDataRoot() const noexcept = 0;
        [[nodiscard]] virtual const filesystem::AbsolutePath& IntermediateRoot() const noexcept = 0;
        [[nodiscard]] virtual const filesystem::AbsolutePath& SavedRoot() const noexcept = 0;
        [[nodiscard]] virtual const filesystem::AbsolutePath& BuildsRoot() const noexcept = 0;
        [[nodiscard]] virtual const filesystem::AbsolutePath& ConfigRoot() const noexcept = 0;
        [[nodiscard]] virtual const filesystem::AbsolutePath& PluginsRoot() const noexcept = 0;

    protected:
        ProjectWorkspaceService() noexcept = default;
    };

    [[nodiscard]] bool RegisterProjectWorkspaceService(application::EngineHost& host,
                                                       const ProjectWorkspaceConfig& config,
                                                       application::HostFailure* failure = nullptr) noexcept;
    [[nodiscard]] ProjectWorkspaceService* FindProjectWorkspaceService(application::EngineHost& host) noexcept;
    [[nodiscard]] ProjectWorkspaceService* FindProjectWorkspaceService(application::ServiceContext& context) noexcept;
} // namespace vanguard::editor
