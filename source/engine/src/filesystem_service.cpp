#include <vanguard/engine/engine_services.hpp>

#include <vanguard/filesystem/filesystem.hpp>
#include <vanguard/memory/memory.hpp>

#include <new>

namespace
{
    namespace app = vanguard::application;

    [[nodiscard]] vanguard::filesystem::Config MakeFilesystemConfig() noexcept
    {
        const vanguard::filesystem::AbsolutePath root = vanguard::filesystem::paths::GetRootDirectory();
        return {root, root.AddDirPath("data"), root.AddDirPath("cache")};
    }

    class FilesystemService final : public app::Service
    {
    public:
        explicit FilesystemService(const vanguard::filesystem::Config* const config) noexcept : m_config(config != nullptr ? *config : MakeFilesystemConfig())
        {
        }

    protected:
        app::LifecycleStatus OnInitialize(app::ServiceContext&) noexcept override
        {
            if (vanguard::filesystem::IsInitialized())
                return app::LifecycleStatus::Failure("Filesystem was initialized outside the managed engine lifecycle");

            if (!vanguard::filesystem::Initialize(m_config))
                return app::LifecycleStatus::Failure("Filesystem initialization failed");
            m_initialized = true;
            return app::LifecycleStatus::Success();
        }

        app::LifecycleStatus OnStart(app::ServiceContext&) noexcept override
        {
            if (!m_initialized || !vanguard::filesystem::IsInitialized())
                return app::LifecycleStatus::Failure("Filesystem did not enter a running state");

            const vanguard::filesystem::Manager& manager = vanguard::filesystem::GetManager();
            if (manager.GetEngineRoot() != m_config.engineRoot || manager.GetGameRoot() != m_config.gameRoot ||
                manager.GetCacheDirectory() != m_config.cacheRoot)
                return app::LifecycleStatus::Failure("Filesystem roots do not match the Vanguard launch layout");
            return app::LifecycleStatus::Success();
        }

        app::LifecycleStatus OnShutdown(app::ServiceContext&) noexcept override
        {
            if (!m_initialized)
                return app::LifecycleStatus::Success();
            vanguard::filesystem::Shutdown();
            if (vanguard::filesystem::IsInitialized())
                return app::LifecycleStatus::Failure("Filesystem shutdown failed");
            m_initialized = false;
            return app::LifecycleStatus::Success();
        }

    private:
        vanguard::filesystem::Config m_config;
        bool m_initialized = false;
    };

    app::Service* CreateFilesystemService(void* const userData) noexcept
    {
        vanguard::memory::MemoryBlock block =
            vanguard::memory::Allocate(vanguard::memory::PoolId::Filesystem, sizeof(FilesystemService), alignof(FilesystemService));
        return block ? ::new (block.address) FilesystemService(static_cast<const vanguard::filesystem::Config*>(userData)) : nullptr;
    }

    void DestroyFilesystemService(app::Service* const service, void*) noexcept
    {
        if (service == nullptr)
            return;
        static_cast<FilesystemService*>(service)->~FilesystemService();
        vanguard::memory::MemoryBlock block{service, sizeof(FilesystemService), vanguard::memory::PoolId::Filesystem};
        vanguard::memory::Free(block);
    }
} // namespace

namespace vanguard::engine
{
    namespace
    {
        bool RegisterFilesystemServiceInternal(application::EngineHost& host, void* const config, application::HostFailure* const failure) noexcept
        {
            constexpr application::ServiceDependency dependencies[]{{IoServiceId, application::DependencyKind::Required}};
            constexpr application::CapabilityId providedCapabilities[]{FilesystemCapabilityId};
            application::ServiceDescriptor descriptor;
            descriptor.id = FilesystemServiceId;
            descriptor.name = "filesystem";
            descriptor.profiles = application::ApplicationProfile::All;
            descriptor.scope = application::ServiceScope::Process;
            descriptor.affinity = application::ThreadAffinity::MainThread;
            descriptor.dependencies = {dependencies, 1};
            descriptor.provides = {providedCapabilities, 1};
            descriptor.create = CreateFilesystemService;
            descriptor.destroy = DestroyFilesystemService;
            descriptor.userData = config;
            return host.RegisterService(EngineModuleId, descriptor, failure);
        }
    } // namespace

    bool RegisterFilesystemService(application::EngineHost& host, application::HostFailure* const failure) noexcept
    {
        return RegisterFilesystemServiceInternal(host, nullptr, failure);
    }

    bool RegisterFilesystemService(application::EngineHost& host, const filesystem::Config& config, application::HostFailure* const failure) noexcept
    {
        return RegisterFilesystemServiceInternal(host, const_cast<filesystem::Config*>(&config), failure);
    }
} // namespace vanguard::engine
