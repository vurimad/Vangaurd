#include <vanguard/editor/editor_project_service.hpp>

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
            if (config != nullptr) m_projectFile = config->projectFile;
        }

        [[nodiscard]] const vanguard::projects::ProjectDescriptor& Project() const noexcept override { return m_project; }
        [[nodiscard]] const vanguard::filesystem::AbsolutePath& ProjectFile() const noexcept override { return m_projectFile; }
        [[nodiscard]] const vanguard::filesystem::AbsolutePath& ProjectRoot() const noexcept override { return m_projectRoot; }
        [[nodiscard]] const vanguard::filesystem::AbsolutePath& AssetsRoot() const noexcept override { return m_assetsRoot; }
        [[nodiscard]] const vanguard::filesystem::AbsolutePath& DerivedDataRoot() const noexcept override { return m_derivedDataRoot; }
        [[nodiscard]] const vanguard::filesystem::AbsolutePath& IntermediateRoot() const noexcept override { return m_intermediateRoot; }
        [[nodiscard]] const vanguard::filesystem::AbsolutePath& SavedRoot() const noexcept override { return m_savedRoot; }
        [[nodiscard]] const vanguard::filesystem::AbsolutePath& BuildsRoot() const noexcept override { return m_buildsRoot; }
        [[nodiscard]] const vanguard::filesystem::AbsolutePath& ConfigRoot() const noexcept override { return m_configRoot; }
        [[nodiscard]] const vanguard::filesystem::AbsolutePath& PluginsRoot() const noexcept override { return m_pluginsRoot; }

    protected:
        app::LifecycleStatus OnInitialize(app::ServiceContext&) noexcept override
        {
            if (!m_projectFile.IsFilePath() || !vanguard::filesystem::IsInitialized())
                return app::LifecycleStatus::Failure("Project Workspace received an invalid project file or unavailable filesystem");

            auto reader = vanguard::filesystem::GetManager().CreateFileReader(m_projectFile, vanguard::filesystem::FOF_Buffered);
            if (!reader) return app::LifecycleStatus::Failure("Project Workspace could not open the project document");

            vanguard::projects::Diagnostic diagnostic;
            if (vanguard::projects::Read(*reader, m_project, &diagnostic) != vanguard::projects::Result::Success)
            {
                VG_LOG_ERROR(vanguard::diagnostics::Category::Engine,
                             "project workspace validation failed: result=%u line=%u column=%u field=%s message=%s",
                             static_cast<vanguard::u32>(diagnostic.result), diagnostic.line, diagnostic.column,
                             diagnostic.field != nullptr ? diagnostic.field : "<none>",
                             diagnostic.message != nullptr ? diagnostic.message : "<none>");
                return app::LifecycleStatus::Failure(diagnostic.message != nullptr ? diagnostic.message
                                                                                  : "Project Workspace validation failed");
            }

            m_projectRoot = vanguard::filesystem::paths::ParentAbsolutePath(m_projectFile);
            if (m_projectRoot.Empty()) return app::LifecycleStatus::Failure("Project Workspace has no project root");
            m_assetsRoot = m_projectRoot.AddDirPath(m_project.assets);
            m_derivedDataRoot = m_projectRoot.AddDirPath(m_project.derivedData);
            m_intermediateRoot = m_projectRoot.AddDirPath(m_project.intermediate);
            m_savedRoot = m_projectRoot.AddDirPath(m_project.saved);
            m_buildsRoot = m_projectRoot.AddDirPath(m_project.builds);
            m_configRoot = m_projectRoot.AddDirPath(m_project.config);
            m_pluginsRoot = m_projectRoot.AddDirPath(m_project.pluginsRoot);

            const vanguard::filesystem::Manager& files = vanguard::filesystem::GetManager();
            if (files.GetGameRoot() != m_projectRoot || files.GetCacheDirectory() != m_derivedDataRoot)
                return app::LifecycleStatus::Failure("Project Workspace roots disagree with the managed filesystem configuration");
            return app::LifecycleStatus::Success();
        }

    private:
        vanguard::projects::ProjectDescriptor m_project;
        vanguard::filesystem::AbsolutePath m_projectFile;
        vanguard::filesystem::AbsolutePath m_projectRoot;
        vanguard::filesystem::AbsolutePath m_assetsRoot;
        vanguard::filesystem::AbsolutePath m_derivedDataRoot;
        vanguard::filesystem::AbsolutePath m_intermediateRoot;
        vanguard::filesystem::AbsolutePath m_savedRoot;
        vanguard::filesystem::AbsolutePath m_buildsRoot;
        vanguard::filesystem::AbsolutePath m_configRoot;
        vanguard::filesystem::AbsolutePath m_pluginsRoot;
    };

    app::Service* CreateProjectWorkspaceService(void* const userData) noexcept
    {
        vanguard::memory::MemoryBlock block = vanguard::memory::Allocate(
            vanguard::memory::PoolId::Tools, sizeof(ManagedProjectWorkspaceService), alignof(ManagedProjectWorkspaceService));
        return block ? ::new (block.address) ManagedProjectWorkspaceService(
                           static_cast<const editor::ProjectWorkspaceConfig*>(userData))
                     : nullptr;
    }

    void DestroyProjectWorkspaceService(app::Service* const service, void*) noexcept
    {
        if (service == nullptr) return;
        static_cast<ManagedProjectWorkspaceService*>(service)->~ManagedProjectWorkspaceService();
        vanguard::memory::MemoryBlock block{
            service, sizeof(ManagedProjectWorkspaceService), vanguard::memory::PoolId::Tools};
        vanguard::memory::Free(block);
    }
} // namespace

namespace vanguard::editor
{
    bool RegisterProjectWorkspaceService(application::EngineHost& host, const ProjectWorkspaceConfig& config,
                                         application::HostFailure* const failure) noexcept
    {
        constexpr application::ServiceDependency dependencies[]{
            {engine::FilesystemServiceId, application::DependencyKind::Required}};
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
