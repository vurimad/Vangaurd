#include <vanguard/engine/resources_service.hpp>

#include <vanguard/jobs/jobs.hpp>
#include <vanguard/memory/memory.hpp>

#include <new>

namespace
{
    namespace app = vanguard::application;

    class ManagedResourcesService final : public vanguard::engine::ResourcesService
    {
    public:
        [[nodiscard]] vanguard::resources::ResourceRegistry& Registry() noexcept override { return m_registry; }
        [[nodiscard]] const vanguard::resources::ResourceRegistry& Registry() const noexcept override { return m_registry; }
        [[nodiscard]] vanguard::resources::ResourcePipeline& Pipeline() noexcept override { return m_pipeline; }
        [[nodiscard]] const vanguard::resources::ResourcePipeline& Pipeline() const noexcept override { return m_pipeline; }

    protected:
        app::LifecycleStatus OnInitialize(app::ServiceContext&) noexcept override
        {
            if (!vanguard::jobs::IsInitialized())
                return app::LifecycleStatus::Failure("Resources requires the managed Jobs scheduler");
            if (m_registry.IsInitialized() || m_pipeline.IsInitialized())
                return app::LifecycleStatus::Failure("Resources was initialized outside its managed lifecycle");
            if (!m_registry.Initialize())
                return app::LifecycleStatus::Failure("Resource registry initialization failed");
            if (!m_pipeline.Initialize(m_registry))
            {
                static_cast<void>(m_registry.Shutdown());
                return app::LifecycleStatus::Failure("Resource pipeline initialization failed");
            }
            return app::LifecycleStatus::Success();
        }

        app::LifecycleStatus OnStart(app::ServiceContext&) noexcept override
        {
            return m_registry.IsInitialized() && m_pipeline.IsInitialized()
                ? app::LifecycleStatus::Success()
                : app::LifecycleStatus::Failure("Resources did not enter a running state");
        }

        app::LifecycleStatus OnQuiesce(app::ServiceContext&) noexcept override
        {
            const vanguard::resources::PipelineStats pipeline = m_pipeline.GetStats();
            const vanguard::resources::RegistryStats registry = m_registry.GetStats();
            if (pipeline.externalRequests != 0)
                return app::LifecycleStatus::Failure("Resources still has external pipeline requests");
            if (registry.strongHandles != 0 || registry.weakHandles != 0)
                return app::LifecycleStatus::Failure("Resources still has external strong or weak handles");
            return app::LifecycleStatus::Success();
        }

        app::LifecycleStatus OnDrain(app::ServiceContext&) noexcept override
        {
            const vanguard::resources::PipelineStats pipeline = m_pipeline.GetStats();
            const vanguard::resources::RegistryStats registry = m_registry.GetStats();
            if (pipeline.activeOperations != 0 || pipeline.activeJobs != 0 || pipeline.activePreparations != 0)
                return app::LifecycleStatus::Failure("Resource pipeline did not drain all operations and stages");
            if (registry.queuedResources != 0 || registry.loadingResources != 0 || registry.loadedResources != 0)
                return app::LifecycleStatus::Failure("Resource registry still has resident or in-flight resources");
            return app::LifecycleStatus::Success();
        }

        app::LifecycleStatus OnShutdown(app::ServiceContext&) noexcept override
        {
            if (m_pipeline.IsInitialized() && !m_pipeline.Shutdown())
                return app::LifecycleStatus::Failure("Resource pipeline shutdown was blocked by live state");
            if (m_registry.IsInitialized() && !m_registry.Shutdown())
                return app::LifecycleStatus::Failure("Resource registry shutdown was blocked by live state");
            return app::LifecycleStatus::Success();
        }

    private:
        vanguard::resources::ResourceRegistry m_registry;
        vanguard::resources::ResourcePipeline m_pipeline;
    };

    app::Service* CreateResourcesService(void*) noexcept
    {
        vanguard::memory::MemoryBlock block = vanguard::memory::Allocate(
            vanguard::memory::PoolId::Resources, sizeof(ManagedResourcesService), alignof(ManagedResourcesService));
        return block ? ::new (block.address) ManagedResourcesService() : nullptr;
    }

    void DestroyResourcesService(app::Service* const service, void*) noexcept
    {
        if (service == nullptr) return;
        static_cast<ManagedResourcesService*>(service)->~ManagedResourcesService();
        vanguard::memory::MemoryBlock block{
            service, sizeof(ManagedResourcesService), vanguard::memory::PoolId::Resources};
        vanguard::memory::Free(block);
    }
}

namespace vanguard::engine
{
    bool RegisterResourcesService(application::EngineHost& host,
                                  application::HostFailure* const failure) noexcept
    {
        constexpr application::ServiceDependency dependencies[]{
            {FilesystemServiceId, application::DependencyKind::Required},
            {JobsServiceId, application::DependencyKind::Required}};
        constexpr application::CapabilityId providedCapabilities[]{
            ResourceRegistryCapabilityId, ResourcePipelineCapabilityId};
        application::ServiceDescriptor descriptor;
        descriptor.id = ResourcesServiceId;
        descriptor.name = "resources";
        descriptor.profiles = application::ApplicationProfile::All;
        descriptor.scope = application::ServiceScope::Process;
        descriptor.affinity = application::ThreadAffinity::MainThread;
        descriptor.dependencies = {dependencies, 2};
        descriptor.provides = {providedCapabilities, 2};
        descriptor.create = CreateResourcesService;
        descriptor.destroy = DestroyResourcesService;
        return host.RegisterService(EngineModuleId, descriptor, failure);
    }

    ResourcesService* FindResourcesService(application::EngineHost& host) noexcept
    {
        return static_cast<ResourcesService*>(host.FindCapability(ResourcePipelineCapabilityId));
    }

    ResourcesService* FindResourcesService(application::ServiceContext& context) noexcept
    {
        return static_cast<ResourcesService*>(context.FindCapability(ResourcePipelineCapabilityId));
    }
} // namespace vanguard::engine
