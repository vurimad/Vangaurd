#include <vanguard/engine/resource_streaming_service.hpp>

#include <vanguard/engine/resources_service.hpp>
#include <vanguard/filesystem/filesystem.hpp>
#include <vanguard/io/io.hpp>
#include <vanguard/jobs/jobs.hpp>
#include <vanguard/memory/memory.hpp>

#include <new>

namespace
{
    namespace app = vanguard::application;

    class ManagedResourceStreamingService final : public vanguard::engine::ResourceStreamingService
    {
    public:
        [[nodiscard]] vanguard::streaming::ResourceStreamer& Streamer() noexcept override { return m_streamer; }
        [[nodiscard]] const vanguard::streaming::ResourceStreamer& Streamer() const noexcept override { return m_streamer; }
        [[nodiscard]] vanguard::streaming::PackageSetMount& PackageSet() noexcept override { return m_packageSet; }
        [[nodiscard]] const vanguard::streaming::PackageSetMount& PackageSet() const noexcept override { return m_packageSet; }

    protected:
        app::LifecycleStatus OnInitialize(app::ServiceContext& context) noexcept override
        {
            vanguard::engine::ResourcesService* const resources = vanguard::engine::FindResourcesService(context);
            if (resources == nullptr || !resources->Pipeline().IsInitialized() ||
                !vanguard::filesystem::IsInitialized() || !vanguard::io::IsInitialized() ||
                !vanguard::jobs::IsInitialized())
                return app::LifecycleStatus::Failure("Resource Streaming dependencies are not running");
            if (m_streamer.IsInitialized())
                return app::LifecycleStatus::Failure("Resource Streaming was initialized outside its managed lifecycle");
            if (!m_streamer.Initialize(resources->Pipeline()))
                return app::LifecycleStatus::Failure("Resource Streaming initialization failed");
            return app::LifecycleStatus::Success();
        }

        app::LifecycleStatus OnStart(app::ServiceContext&) noexcept override
        {
            return m_streamer.IsInitialized()
                ? app::LifecycleStatus::Success()
                : app::LifecycleStatus::Failure("Resource Streaming did not enter a running state");
        }

        app::LifecycleStatus OnQuiesce(app::ServiceContext&) noexcept override
        {
            const vanguard::streaming::Stats stats = m_streamer.GetStats();
            return stats.activeLoads == 0 && stats.activeReads == 0
                ? app::LifecycleStatus::Success()
                : app::LifecycleStatus::Failure("Resource Streaming still has active loads or I/O reads");
        }

        app::LifecycleStatus OnDrain(app::ServiceContext&) noexcept override
        {
            const vanguard::streaming::Stats stats = m_streamer.GetStats();
            return stats.activeLoads == 0 && stats.activeReads == 0 && stats.stagingBytesInUse == 0
                ? app::LifecycleStatus::Success()
                : app::LifecycleStatus::Failure("Resource Streaming did not drain all loads, reads, and staging memory");
        }

        app::LifecycleStatus OnShutdown(app::ServiceContext&) noexcept override
        {
            if (m_packageSet.IsMounted())
                return app::LifecycleStatus::Failure("Runtime package set must be explicitly unmounted before Resource Streaming shutdown");
            return m_streamer.Shutdown()
                ? app::LifecycleStatus::Success()
                : app::LifecycleStatus::Failure("Resource Streaming shutdown was blocked by live state");
        }

    private:
        vanguard::streaming::PackageSetMount m_packageSet;
        vanguard::streaming::ResourceStreamer m_streamer;
    };

    app::Service* CreateResourceStreamingService(void*) noexcept
    {
        vanguard::memory::MemoryBlock block = vanguard::memory::Allocate(
            vanguard::memory::PoolId::Streaming, sizeof(ManagedResourceStreamingService),
            alignof(ManagedResourceStreamingService));
        return block ? ::new (block.address) ManagedResourceStreamingService() : nullptr;
    }

    void DestroyResourceStreamingService(app::Service* const service, void*) noexcept
    {
        if (service == nullptr) return;
        static_cast<ManagedResourceStreamingService*>(service)->~ManagedResourceStreamingService();
        vanguard::memory::MemoryBlock block{
            service, sizeof(ManagedResourceStreamingService), vanguard::memory::PoolId::Streaming};
        vanguard::memory::Free(block);
    }
}

namespace vanguard::engine
{
    bool RegisterResourceStreamingService(application::EngineHost& host,
                                          application::HostFailure* const failure) noexcept
    {
        constexpr application::ServiceDependency dependencies[]{
            {ResourcesServiceId, application::DependencyKind::Required},
            {FilesystemServiceId, application::DependencyKind::Required},
            {IoServiceId, application::DependencyKind::Required},
            {JobsServiceId, application::DependencyKind::Required}};
        constexpr application::CapabilityId providedCapabilities[]{ResourceStreamingCapabilityId};
        application::ServiceDescriptor descriptor;
        descriptor.id = ResourceStreamingServiceId;
        descriptor.name = "resourceStreaming";
        descriptor.profiles = application::ApplicationProfile::All;
        descriptor.scope = application::ServiceScope::Process;
        descriptor.affinity = application::ThreadAffinity::MainThread;
        descriptor.dependencies = {dependencies, 4};
        descriptor.provides = {providedCapabilities, 1};
        descriptor.create = CreateResourceStreamingService;
        descriptor.destroy = DestroyResourceStreamingService;
        return host.RegisterService(EngineModuleId, descriptor, failure);
    }

    ResourceStreamingService* FindResourceStreamingService(application::EngineHost& host) noexcept
    {
        return static_cast<ResourceStreamingService*>(host.FindCapability(ResourceStreamingCapabilityId));
    }

    ResourceStreamingService* FindResourceStreamingService(application::ServiceContext& context) noexcept
    {
        return static_cast<ResourceStreamingService*>(context.FindCapability(ResourceStreamingCapabilityId));
    }
} // namespace vanguard::engine
