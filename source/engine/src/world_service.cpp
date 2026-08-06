#include <vanguard/engine/world_service.hpp>

#include <vanguard/engine/resource_streaming_service.hpp>
#include <vanguard/engine/resources_service.hpp>
#include <vanguard/memory/memory.hpp>

#include <new>

namespace
{
    namespace app = vanguard::application;

    [[nodiscard]] vanguard::resources::Failure ConvertWorldFailure(const vanguard::world::Result result) noexcept
    {
        using Failure = vanguard::resources::Failure;
        using Result = vanguard::world::Result;
        switch (result)
        {
        case Result::Success: return Failure::None;
        case Result::InvalidMagic:
        case Result::IntegrityFailure: return Failure::IntegrityFailure;
        case Result::UnsupportedVersion: return Failure::UnsupportedVersion;
        case Result::IoFailure: return Failure::IoFailure;
        case Result::InvalidReference: return Failure::DependencyFailure;
        case Result::LimitExceeded: return Failure::OutOfMemory;
        default: return Failure::DeserializationFailure;
        }
    }

    [[nodiscard]] vanguard::resources::ResourceObject* DecodeWorld(
        const vanguard::resources::ResourceReference, const void* const data, const vanguard::usize size,
        const vanguard::resources::LoadContext&, vanguard::resources::Failure& failure, void*) noexcept
    {
        vanguard::memory::MemoryBlock block = vanguard::memory::Allocate(
            vanguard::memory::PoolId::World, sizeof(vanguard::world::WorldResource), alignof(vanguard::world::WorldResource));
        if (!block)
        {
            failure = vanguard::resources::Failure::OutOfMemory;
            return nullptr;
        }

        auto* const resource = ::new (block.address) vanguard::world::WorldResource();
        const vanguard::world::Result result = resource->Open(data, size);
        if (result == vanguard::world::Result::Success) return resource;

        resource->~WorldResource();
        vanguard::memory::Free(block);
        failure = ConvertWorldFailure(result);
        return nullptr;
    }

    void DestroyWorld(vanguard::resources::ResourceObject* const object, void*) noexcept
    {
        if (object == nullptr) return;
        static_cast<vanguard::world::WorldResource*>(object)->~WorldResource();
        vanguard::memory::MemoryBlock block{
            object, sizeof(vanguard::world::WorldResource), vanguard::memory::PoolId::World};
        vanguard::memory::Free(block);
    }

    class ManagedWorldService final : public vanguard::engine::WorldService
    {
    public:
        [[nodiscard]] bool BeginWorld(const vanguard::resources::ResourceReference reference) noexcept override
        {
            if (m_status != vanguard::engine::WorldResourceStatus::Idle || m_streaming == nullptr ||
                !reference.IsValid() || reference.ExpectedType() != vanguard::world::WorldResourceType)
                return false;

            m_request = m_streaming->Streamer().Request(reference, vanguard::resources::LoadPriority::Critical);
            if (!m_request.IsValid()) return false;
            m_status = vanguard::engine::WorldResourceStatus::Loading;
            m_failure = vanguard::resources::Failure::None;
            return true;
        }

        [[nodiscard]] vanguard::engine::WorldResourceStatus PollWorld() noexcept override
        {
            if (m_status != vanguard::engine::WorldResourceStatus::Loading || !m_request.HasFinished()) return m_status;
            if (!m_request.HasLoaded())
            {
                m_failure = m_request.Error();
                m_request.Reset();
                m_status = vanguard::engine::WorldResourceStatus::Failed;
                return m_status;
            }

            m_resource = m_request.Acquire();
            m_request.Reset();
            if (!m_resource.IsValid() || m_resource.Get()->Type() != vanguard::world::WorldResourceType)
            {
                m_resource = {};
                m_failure = vanguard::resources::Failure::InternalError;
                m_status = vanguard::engine::WorldResourceStatus::Failed;
                return m_status;
            }

            const auto* const resource = static_cast<const vanguard::world::WorldResource*>(m_resource.Get());
            if (!m_grid.Initialize(resource->File()) || !m_executor.Initialize(m_grid, m_resources->Pipeline()))
            {
                if (m_executor.IsInitialized()) static_cast<void>(m_executor.Shutdown());
                if (m_grid.IsInitialized()) static_cast<void>(m_grid.Shutdown());
                m_resource = {};
                m_failure = vanguard::resources::Failure::OutOfMemory;
                m_status = vanguard::engine::WorldResourceStatus::Failed;
                return m_status;
            }

            m_status = vanguard::engine::WorldResourceStatus::Ready;
            return m_status;
        }

        [[nodiscard]] bool CancelWorld() noexcept override
        {
            if (m_status != vanguard::engine::WorldResourceStatus::Loading) return false;
            static_cast<void>(m_request.Cancel());
            m_request.Reset();
            m_status = vanguard::engine::WorldResourceStatus::Idle;
            m_failure = vanguard::resources::Failure::None;
            return true;
        }

        [[nodiscard]] bool ReleaseWorld() noexcept override
        {
            if (m_status == vanguard::engine::WorldResourceStatus::Loading) return CancelWorld();
            if (m_status == vanguard::engine::WorldResourceStatus::Idle) return true;
            if (m_executor.IsInitialized() && !m_executor.Shutdown()) return false;
            if (m_grid.IsInitialized() && !m_grid.Shutdown()) return false;
            m_resource = {};
            m_failure = vanguard::resources::Failure::None;
            m_status = vanguard::engine::WorldResourceStatus::Idle;
            return true;
        }

        [[nodiscard]] vanguard::engine::WorldResourceStatus Status() const noexcept override { return m_status; }
        [[nodiscard]] vanguard::resources::Failure LastFailure() const noexcept override { return m_failure; }
        [[nodiscard]] const vanguard::world::WorldResource* Resource() const noexcept override
        {
            return m_status == vanguard::engine::WorldResourceStatus::Ready
                ? static_cast<const vanguard::world::WorldResource*>(m_resource.Get()) : nullptr;
        }
        [[nodiscard]] vanguard::world::WorldStreamingGrid* Grid() noexcept override
        {
            return m_grid.IsInitialized() ? &m_grid : nullptr;
        }
        [[nodiscard]] vanguard::world::WorldStreamingExecutor* Executor() noexcept override
        {
            return m_executor.IsInitialized() ? &m_executor : nullptr;
        }

    protected:
        app::LifecycleStatus OnInitialize(app::ServiceContext& context) noexcept override
        {
            m_streaming = vanguard::engine::FindResourceStreamingService(context);
            m_resources = vanguard::engine::FindResourcesService(context);
            if (m_streaming == nullptr || m_resources == nullptr)
                return app::LifecycleStatus::Failure("World dependencies are not running");
            if (!m_streaming->Streamer().RegisterDecoder(
                    {vanguard::world::WorldResourceType, "Vanguard world", &DecodeWorld, &DestroyWorld, nullptr}))
                return app::LifecycleStatus::Failure("World decoder registration failed");
            m_decoderRegistered = true;
            return app::LifecycleStatus::Success();
        }

        app::LifecycleStatus OnQuiesce(app::ServiceContext&) noexcept override
        {
            return m_status == vanguard::engine::WorldResourceStatus::Idle
                ? app::LifecycleStatus::Success()
                : app::LifecycleStatus::Failure("Startup world must be explicitly released before World shutdown");
        }

        app::LifecycleStatus OnShutdown(app::ServiceContext&) noexcept override
        {
            if (m_status != vanguard::engine::WorldResourceStatus::Idle || m_grid.IsInitialized() || m_executor.IsInitialized())
                return app::LifecycleStatus::Failure("World runtime state remains live during shutdown");
            if (m_decoderRegistered && !m_streaming->Streamer().UnregisterDecoder(vanguard::world::WorldResourceType))
                return app::LifecycleStatus::Failure("World decoder unregistration failed");
            m_decoderRegistered = false;
            m_streaming = nullptr;
            m_resources = nullptr;
            return app::LifecycleStatus::Success();
        }

    private:
        vanguard::engine::ResourceStreamingService* m_streaming = nullptr;
        vanguard::engine::ResourcesService* m_resources = nullptr;
        vanguard::resources::PipelineRequest m_request;
        vanguard::resources::ResourceHandle m_resource;
        vanguard::world::WorldStreamingGrid m_grid;
        vanguard::world::WorldStreamingExecutor m_executor;
        vanguard::engine::WorldResourceStatus m_status = vanguard::engine::WorldResourceStatus::Idle;
        vanguard::resources::Failure m_failure = vanguard::resources::Failure::None;
        bool m_decoderRegistered = false;
    };

    app::Service* CreateWorldService(void*) noexcept
    {
        vanguard::memory::MemoryBlock block = vanguard::memory::Allocate(
            vanguard::memory::PoolId::World, sizeof(ManagedWorldService), alignof(ManagedWorldService));
        return block ? ::new (block.address) ManagedWorldService() : nullptr;
    }

    void DestroyWorldService(app::Service* const service, void*) noexcept
    {
        if (service == nullptr) return;
        static_cast<ManagedWorldService*>(service)->~ManagedWorldService();
        vanguard::memory::MemoryBlock block{
            service, sizeof(ManagedWorldService), vanguard::memory::PoolId::World};
        vanguard::memory::Free(block);
    }
}

namespace vanguard::engine
{
    bool RegisterWorldService(application::EngineHost& host, application::HostFailure* const failure) noexcept
    {
        constexpr application::ServiceDependency dependencies[]{
            {ResourceStreamingServiceId, application::DependencyKind::Required},
            {ResourcesServiceId, application::DependencyKind::Required}};
        constexpr application::CapabilityId providedCapabilities[]{WorldCapabilityId};
        application::ServiceDescriptor descriptor;
        descriptor.id = WorldServiceId;
        descriptor.name = "world";
        descriptor.profiles = application::ApplicationProfile::Runtime | application::ApplicationProfile::Editor;
        descriptor.scope = application::ServiceScope::Engine;
        descriptor.affinity = application::ThreadAffinity::MainThread;
        descriptor.dependencies = {dependencies, 2};
        descriptor.provides = {providedCapabilities, 1};
        descriptor.create = CreateWorldService;
        descriptor.destroy = DestroyWorldService;
        return host.RegisterService(EngineModuleId, descriptor, failure);
    }

    WorldService* FindWorldService(application::EngineHost& host) noexcept
    {
        return static_cast<WorldService*>(host.FindCapability(WorldCapabilityId));
    }

    WorldService* FindWorldService(application::ServiceContext& context) noexcept
    {
        return static_cast<WorldService*>(context.FindCapability(WorldCapabilityId));
    }
} // namespace vanguard::engine
