#pragma once

#include <vanguard/application/engine_host.hpp>
#include <vanguard/filesystem/filesystem.hpp>
#include <vanguard/projects/project.hpp>
#include <vanguard/assets/source_database.hpp>

namespace vanguard::editor
{
    inline constexpr application::ModuleId EditorModuleId = 0x656469746f720001ull;
    inline constexpr application::ServiceId ProjectWorkspaceServiceId = 0x656470726f6a7301ull;
    inline constexpr application::CapabilityId ProjectWorkspaceCapabilityId = 0x656470726f6a6301ull;

    struct ProjectWorkspaceConfig
    {
        filesystem::AbsolutePath projectFile;
        // Authored descriptor already read by composition. The service retains
        // its value; no second disk read can change roots during startup.
        projects::ProjectDescriptor project;
    };

    /// Owns the validated project document and every root derived from it for the lifetime of an editor workspace.
    /// Engine services remain project-agnostic and consume only the roots or resource identities they actually need.
    class ProjectWorkspaceService : public application::Service
    {
    public:
        ~ProjectWorkspaceService() override = default;

        [[nodiscard]] virtual const projects::ProjectDescriptor& GetProject() const noexcept = 0;
        [[nodiscard]] virtual const filesystem::AbsolutePath& GetProjectFile() const noexcept = 0;
        [[nodiscard]] virtual const filesystem::AbsolutePath& GetProjectRoot() const noexcept = 0;
        [[nodiscard]] virtual const filesystem::AbsolutePath& GetAssetsRoot() const noexcept = 0;
        [[nodiscard]] virtual const filesystem::AbsolutePath& GetDerivedDataRoot() const noexcept = 0;
        [[nodiscard]] virtual const filesystem::AbsolutePath& GetIntermediateRoot() const noexcept = 0;
        [[nodiscard]] virtual const filesystem::AbsolutePath& GetSavedRoot() const noexcept = 0;
        [[nodiscard]] virtual const filesystem::AbsolutePath& GetBuildsRoot() const noexcept = 0;
        [[nodiscard]] virtual const filesystem::AbsolutePath& GetConfigRoot() const noexcept = 0;
        [[nodiscard]] virtual const filesystem::AbsolutePath& GetPluginsRoot() const noexcept = 0;
        [[nodiscard]] virtual const assets::SourceDatabase& GetSources() const noexcept = 0;
        // Owner-thread calls only, after all borrowed catalog readers finish.
        [[nodiscard]] virtual assets::SourceDatabaseResult RescanSources(filesystem::ScanResult* failure = nullptr) noexcept = 0;
        [[nodiscard]] virtual bool ClassifySources(containers::ArraySpan<const assets::CompilerDescriptor> compilers) noexcept = 0;

    protected:
        ProjectWorkspaceService() noexcept = default;
    };

    [[nodiscard]] bool RegisterProjectWorkspaceService(application::EngineHost& host, const ProjectWorkspaceConfig& config,
                                                       application::HostFailure* failure = nullptr) noexcept;
    [[nodiscard]] ProjectWorkspaceService* FindProjectWorkspaceService(application::EngineHost& host) noexcept;
    [[nodiscard]] ProjectWorkspaceService* FindProjectWorkspaceService(application::ServiceContext& context) noexcept;
} // namespace vanguard::editor
