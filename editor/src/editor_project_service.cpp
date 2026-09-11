#include <vanguard/editor/editor_project_service.hpp>
#include <vanguard/projects/project_workspace.hpp>

#include <vanguard/diagnostics/diagnostics.hpp>
#include <vanguard/engine/engine_services.hpp>
#include <vanguard/memory/memory.hpp>

#include <new>

namespace
{
    namespace app = vanguard::application;
    namespace editor = vanguard::editor;

    class ManagedProjectWorkspaceService final : public editor::ProjectWorkspaceService
    {
    public:
        explicit ManagedProjectWorkspaceService(const editor::ProjectWorkspaceConfig* const config) noexcept
        {
            if (config != nullptr)
            {
                m_projectFile = config->projectFile;
                m_project = config->project;
            }
        }

        [[nodiscard]] const vanguard::projects::ProjectDescriptor& GetProject() const noexcept override
        {
            return m_project;
        }
        [[nodiscard]] const vanguard::filesystem::AbsolutePath& GetProjectFile() const noexcept override
        {
            return m_projectFile;
        }
        [[nodiscard]] const vanguard::filesystem::AbsolutePath& GetProjectRoot() const noexcept override
        {
            return m_workspace.root;
        }
        [[nodiscard]] const vanguard::filesystem::AbsolutePath& GetAssetsRoot() const noexcept override
        {
            return m_workspace.assets;
        }
        [[nodiscard]] const vanguard::filesystem::AbsolutePath& GetDerivedDataRoot() const noexcept override
        {
            return m_workspace.derivedData;
        }
        [[nodiscard]] const vanguard::filesystem::AbsolutePath& GetIntermediateRoot() const noexcept override
        {
            return m_workspace.intermediate;
        }
        [[nodiscard]] const vanguard::filesystem::AbsolutePath& GetSavedRoot() const noexcept override
        {
            return m_workspace.saved;
        }
        [[nodiscard]] const vanguard::filesystem::AbsolutePath& GetBuildsRoot() const noexcept override
        {
            return m_workspace.builds;
        }
        [[nodiscard]] const vanguard::filesystem::AbsolutePath& GetConfigRoot() const noexcept override
        {
            return m_workspace.config;
        }
        [[nodiscard]] const vanguard::filesystem::AbsolutePath& GetPluginsRoot() const noexcept override
        {
            return m_workspace.plugins;
        }

        const vanguard::assets::SourceDatabase& GetSources() const noexcept override { return m_sources; }
        vanguard::assets::SourceDatabaseResult RescanSources(vanguard::filesystem::ScanResult* const failure = nullptr) noexcept override
        {
            const vanguard::assets::SourceRoot root{m_workspace.assets, false};
            return m_sources.Rescan(vanguard::filesystem::GetManager(), {&root, 1}, failure);
        }
        bool ClassifySources(vanguard::containers::ArraySpan<const vanguard::assets::CompilerDescriptor> compilers) noexcept override
        {
            return m_sources.ClassifySources(compilers);
        }

    protected:
        app::LifecycleStatus OnInitialize(app::ServiceContext&) noexcept override
        {
            if (!m_projectFile.IsFilePath() || !vanguard::filesystem::IsInitialized())
                return app::LifecycleStatus::Failure("Project Workspace received an invalid project file or unavailable filesystem");

            vanguard::projects::Diagnostic diagnostic;
            if (vanguard::projects::ResolveWorkspace(m_projectFile, m_project, m_workspace, &diagnostic) != vanguard::projects::Result::Success)
                return app::LifecycleStatus::Failure(diagnostic.message != nullptr ? diagnostic.message : "Project Workspace resolution failed");

            const vanguard::filesystem::Manager& files = vanguard::filesystem::GetManager();
            if (files.GetGameRoot() != m_workspace.root || files.GetCacheDirectory() != m_workspace.derivedData)
                return app::LifecycleStatus::Failure("Project Workspace roots disagree with the managed filesystem configuration");
            if (RescanSources() != vanguard::assets::SourceDatabaseResult::Success)
                return app::LifecycleStatus::Failure("Project Workspace source inventory failed");
            return app::LifecycleStatus::Success();
        }

    private:
        vanguard::projects::ProjectDescriptor m_project;
        vanguard::assets::SourceDatabase m_sources;
        vanguard::projects::ProjectWorkspace m_workspace;
        vanguard::filesystem::AbsolutePath m_projectFile;
    };

    app::Service* CreateProjectWorkspaceService(void* const userData) noexcept
    {
        vanguard::memory::MemoryBlock block =
            vanguard::memory::Allocate(vanguard::memory::PoolId::Tools, sizeof(ManagedProjectWorkspaceService), alignof(ManagedProjectWorkspaceService));
        return block ? ::new (block.address) ManagedProjectWorkspaceService(static_cast<const editor::ProjectWorkspaceConfig*>(userData)) : nullptr;
    }

    void DestroyProjectWorkspaceService(app::Service* const service, void*) noexcept
    {
        if (service == nullptr)
            return;
        static_cast<ManagedProjectWorkspaceService*>(service)->~ManagedProjectWorkspaceService();
        vanguard::memory::MemoryBlock block{service, sizeof(ManagedProjectWorkspaceService), vanguard::memory::PoolId::Tools};
        vanguard::memory::Free(block);
    }
} // namespace

namespace vanguard::editor
{
    bool RegisterProjectWorkspaceService(application::EngineHost& host, const ProjectWorkspaceConfig& config, application::HostFailure* const failure) noexcept
    {
        constexpr application::ServiceDependency dependencies[]{{engine::FilesystemServiceId, application::DependencyKind::Required}};
        constexpr application::CapabilityId capabilities[]{ProjectWorkspaceCapabilityId};
        application::ServiceDescriptor descriptor;
        descriptor.id = ProjectWorkspaceServiceId;
        descriptor.name = "projectWorkspace";
        descriptor.profiles = application::ApplicationProfile::Editor | application::ApplicationProfile::Tool;
        descriptor.scope = application::ServiceScope::EditorWorkspace;
        descriptor.affinity = application::ThreadAffinity::MainThread;
        descriptor.dependencies = {dependencies, 1};
        descriptor.provides = {capabilities, 1};
        descriptor.create = CreateProjectWorkspaceService;
        descriptor.destroy = DestroyProjectWorkspaceService;
        descriptor.userData = const_cast<ProjectWorkspaceConfig*>(&config);
        return host.RegisterService(EditorModuleId, descriptor, failure);
    }

    ProjectWorkspaceService* FindProjectWorkspaceService(application::EngineHost& host) noexcept
    {
        return static_cast<ProjectWorkspaceService*>(host.FindCapability(ProjectWorkspaceCapabilityId));
    }

    ProjectWorkspaceService* FindProjectWorkspaceService(application::ServiceContext& context) noexcept
    {
        return static_cast<ProjectWorkspaceService*>(context.FindCapability(ProjectWorkspaceCapabilityId));
    }
} // namespace vanguard::editor
