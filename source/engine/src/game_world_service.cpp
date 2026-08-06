#include <vanguard/engine/game_world_service.hpp>

#include <vanguard/engine/resource_streaming_service.hpp>
#include <vanguard/engine/world_service.hpp>
#include <vanguard/memory/memory.hpp>
#include <vanguard/prefabs/prefabs.hpp>

#include <new>

namespace
{
    namespace app = vanguard::application;

    [[nodiscard]] vanguard::resources::Failure ConvertPrefabFailure(const vanguard::prefabs::Result result) noexcept
    {
        using Failure = vanguard::resources::Failure;
        using Result = vanguard::prefabs::Result;
        switch (result)
        {
        case Result::Success: return Failure::None;
        case Result::InvalidMagic:
        case Result::IntegrityFailure: return Failure::IntegrityFailure;
        case Result::UnsupportedVersion: return Failure::UnsupportedVersion;
        case Result::IoFailure: return Failure::IoFailure;
        case Result::LimitExceeded: return Failure::OutOfMemory;
        default: return Failure::DeserializationFailure;
        }
    }

    [[nodiscard]] vanguard::resources::ResourceObject* DecodePrefab(
        const vanguard::resources::ResourceReference reference, const void* const data, const vanguard::usize size,
        const vanguard::resources::LoadContext&, vanguard::resources::Failure& failure, void*) noexcept
    {
        if (reference.ExpectedType() != vanguard::prefabs::PrefabResourceType)
        {
            failure = vanguard::resources::Failure::UnknownType;
            return nullptr;
        }
        vanguard::memory::MemoryBlock block = vanguard::memory::Allocate(
            vanguard::memory::PoolId::World, sizeof(vanguard::prefabs::PrefabResource), alignof(vanguard::prefabs::PrefabResource));
        if (!block)
        {
            failure = vanguard::resources::Failure::OutOfMemory;
            return nullptr;
        }
        auto* const resource = ::new (block.address) vanguard::prefabs::PrefabResource();
        const vanguard::prefabs::Result result = resource->Open(data, size);
        if (result == vanguard::prefabs::Result::Success) return resource;
        resource->~PrefabResource();
        vanguard::memory::Free(block);
        failure = ConvertPrefabFailure(result);
        return nullptr;
    }

    void DestroyPrefab(vanguard::resources::ResourceObject* const object, void*) noexcept
    {
        if (object == nullptr) return;
        static_cast<vanguard::prefabs::PrefabResource*>(object)->~PrefabResource();
        vanguard::memory::MemoryBlock block{
            object, sizeof(vanguard::prefabs::PrefabResource), vanguard::memory::PoolId::World};
        vanguard::memory::Free(block);
    }

    class ManagedGameWorldService final : public vanguard::engine::GameWorldService
    {
    public:
        [[nodiscard]] bool Configure(const vanguard::engine::GameWorldServiceConfig& config) noexcept override
        {
            if (m_status != vanguard::engine::GameWorldStatus::Idle) return false;
            m_config = config;
            return true;
        }

        [[nodiscard]] bool BeginWorld() noexcept override
        {
            if (m_status != vanguard::engine::GameWorldStatus::Idle || m_worldService == nullptr ||
                m_worldService->Status() != vanguard::engine::WorldResourceStatus::Ready ||
                m_worldService->Executor() == nullptr || m_worldService->Resource() == nullptr)
                return false;

            vanguard::entities::CellStreamingSystemConfig systemConfig;
            systemConfig.executor = m_worldService->Executor();
            systemConfig.registerComponents = &RegisterComponents;
            systemConfig.forwardNonCellEvent = &ForwardNonCellEvent;
            systemConfig.userData = this;
            systemConfig.materialization = m_config.materialization;
            m_cellStreaming = AllocateCellStreamingSystem(systemConfig);
            if (m_cellStreaming == nullptr || !m_world.RegisterSystem(*m_cellStreaming) || !m_world.Initialize(m_config.world))
            {
                if (m_world.IsInitialized()) static_cast<void>(m_world.Shutdown());
                DeleteCellStreamingSystem();
                m_status = vanguard::engine::GameWorldStatus::Failed;
                return false;
            }

            vanguard::world::StreamingObserver observer;
            const vanguard::f64* const origin = m_worldService->Resource()->File().Origin();
            for (vanguard::u32 axis = 0; axis < 3; ++axis)
            {
                observer.predictedPosition[axis] = origin[axis];
                m_cameraPosition[axis] = origin[axis];
            }
            m_defaultObserver = observer;
            vanguard::world::StreamingProcessInput input;
            input.observers = {&m_defaultObserver, 1};
            for (vanguard::u32 axis = 0; axis < 3; ++axis) input.cameraPosition[axis] = m_cameraPosition[axis];
            if (!m_cellStreaming->SetProcessInput(input))
            {
                m_status = vanguard::engine::GameWorldStatus::Failed;
                return false;
            }
            m_status = vanguard::engine::GameWorldStatus::Running;
            return true;
        }

        [[nodiscard]] bool SetStreamingInput(const vanguard::world::StreamingProcessInput& input) noexcept override
        {
            return m_status == vanguard::engine::GameWorldStatus::Running && m_cellStreaming != nullptr &&
                   m_cellStreaming->SetProcessInput(input);
        }

        [[nodiscard]] bool Tick(const vanguard::f32 deltaSeconds) noexcept override
        {
            if (m_status != vanguard::engine::GameWorldStatus::Running || !m_world.Tick(deltaSeconds))
            {
                if (m_status == vanguard::engine::GameWorldStatus::Running)
                    m_status = vanguard::engine::GameWorldStatus::Failed;
                return false;
            }
            return true;
        }

        [[nodiscard]] vanguard::engine::GameWorldStopResult StopWorld() noexcept override
        {
            using StopResult = vanguard::engine::GameWorldStopResult;
            if (m_status == vanguard::engine::GameWorldStatus::Idle) return StopResult::Complete;
            if (!m_world.IsInitialized())
            {
                static_cast<void>(m_world.Shutdown());
                DeleteCellStreamingSystem();
                m_status = vanguard::engine::GameWorldStatus::Idle;
                return StopResult::Complete;
            }
            if (m_status == vanguard::engine::GameWorldStatus::Running ||
                m_status == vanguard::engine::GameWorldStatus::Failed)
            {
                vanguard::world::WorldStreamingGrid* const grid = m_worldService->Grid();
                if (grid == nullptr || (!grid->IsShutdownRequested() && !grid->RequestShutdown())) return StopResult::Failure;
                m_status = vanguard::engine::GameWorldStatus::Draining;
            }
            if (!m_world.Tick(0.0f)) return StopResult::Failure;

            const vanguard::world::StreamingExecutorStats executor = m_worldService->Executor()->GetStats();
            const vanguard::entities::CellStreamingSystemStats cells = m_cellStreaming->GetStats();
            if (executor.requestingNodes != 0 || executor.residentNodes != 0 || executor.releasePendingNodes != 0 ||
                executor.failedNodes != 0 || cells.trackedCells != 0)
                return StopResult::Pending;
            if (!m_world.Shutdown()) return StopResult::Failure;
            DeleteCellStreamingSystem();
            m_status = vanguard::engine::GameWorldStatus::Idle;
            return StopResult::Complete;
        }

        [[nodiscard]] vanguard::engine::GameWorldStatus Status() const noexcept override { return m_status; }
        [[nodiscard]] vanguard::game::GameWorld* World() noexcept override
        {
            return m_world.IsInitialized() ? &m_world : nullptr;
        }
        [[nodiscard]] vanguard::entities::CellStreamingSystem* CellStreaming() noexcept override { return m_cellStreaming; }

    protected:
        app::LifecycleStatus OnInitialize(app::ServiceContext& context) noexcept override
        {
            m_framePipeline = vanguard::engine::FindFramePipelineService(context);
            m_worldService = vanguard::engine::FindWorldService(context);
            m_streamingService = vanguard::engine::FindResourceStreamingService(context);
            if (m_framePipeline == nullptr || m_worldService == nullptr || m_streamingService == nullptr)
                return app::LifecycleStatus::Failure("Game World dependencies are not running");
            vanguard::engine::FrameParticipantDescriptor frameParticipant;
            frameParticipant.id = vanguard::engine::GameWorldFrameParticipantId;
            frameParticipant.name = "gameWorld";
            frameParticipant.phase = vanguard::engine::FramePhase::Simulation;
            frameParticipant.profiles = app::ApplicationProfile::Runtime | app::ApplicationProfile::Editor;
            frameParticipant.affinity = app::ThreadAffinity::MainThread;
            frameParticipant.execute = &ExecuteFrame;
            frameParticipant.userData = this;
            vanguard::engine::FrameFailure frameFailure;
            if (!m_framePipeline->RegisterParticipant(frameParticipant, &frameFailure))
                return app::LifecycleStatus::Failure(frameFailure.message != nullptr ? frameFailure.message
                                                                                    : "Game World frame registration failed");
            if (!m_streamingService->Streamer().RegisterDecoder(
                    {vanguard::prefabs::PrefabResourceType, "Vanguard prefab", &DecodePrefab, &DestroyPrefab, nullptr}))
                return app::LifecycleStatus::Failure("Prefab decoder registration failed");
            m_prefabDecoderRegistered = true;
            m_cellDecoder = vanguard::entities::MakeCellDecoder(m_cellDecoderContext);
            if (!m_streamingService->Streamer().RegisterDecoder(m_cellDecoder))
            {
                static_cast<void>(m_streamingService->Streamer().UnregisterDecoder(vanguard::prefabs::PrefabResourceType));
                m_prefabDecoderRegistered = false;
                return app::LifecycleStatus::Failure("Cell decoder registration failed");
            }
            m_cellDecoderRegistered = true;
            return app::LifecycleStatus::Success();
        }

        app::LifecycleStatus OnQuiesce(app::ServiceContext&) noexcept override
        {
            return m_status == vanguard::engine::GameWorldStatus::Idle
                ? app::LifecycleStatus::Success()
                : app::LifecycleStatus::Failure("Game World must be explicitly drained before shutdown");
        }

        app::LifecycleStatus OnShutdown(app::ServiceContext&) noexcept override
        {
            if (m_status != vanguard::engine::GameWorldStatus::Idle || m_world.IsInitialized() || m_cellStreaming != nullptr)
                return app::LifecycleStatus::Failure("Game World runtime state remains live during shutdown");
            if (m_cellDecoderRegistered &&
                !m_streamingService->Streamer().UnregisterDecoder(vanguard::world::CellResourceType))
                return app::LifecycleStatus::Failure("Cell decoder unregistration failed");
            m_cellDecoderRegistered = false;
            if (m_prefabDecoderRegistered &&
                !m_streamingService->Streamer().UnregisterDecoder(vanguard::prefabs::PrefabResourceType))
                return app::LifecycleStatus::Failure("Prefab decoder unregistration failed");
            m_prefabDecoderRegistered = false;
            m_framePipeline = nullptr;
            m_worldService = nullptr;
            m_streamingService = nullptr;
            return app::LifecycleStatus::Success();
        }

    private:
        static vanguard::engine::FrameParticipantStatus ExecuteFrame(
            const vanguard::engine::FrameContext& context, void* const userData) noexcept
        {
            auto* const service = static_cast<ManagedGameWorldService*>(userData);
            if (service == nullptr)
                return vanguard::engine::FrameParticipantStatus::Failure("Game World frame participant lost its service");
            if (service->m_status == vanguard::engine::GameWorldStatus::Idle)
                return vanguard::engine::FrameParticipantStatus::Success();
            if (service->m_status != vanguard::engine::GameWorldStatus::Running ||
                !service->Tick(context.simulationDeltaSeconds))
                return vanguard::engine::FrameParticipantStatus::Failure("Flecs game world tick failed");
            return vanguard::engine::FrameParticipantStatus::Success();
        }

        static bool RegisterComponents(vanguard::entities::ComponentRegistry& registry, void* const userData) noexcept
        {
            auto* const service = static_cast<ManagedGameWorldService*>(userData);
            return service != nullptr && (service->m_config.registerComponents == nullptr ||
                   service->m_config.registerComponents(registry, service->m_config.userData));
        }

        static void ForwardNonCellEvent(const vanguard::world::StreamingResourceEvent& event, void* const userData) noexcept
        {
            auto* const service = static_cast<ManagedGameWorldService*>(userData);
            if (service == nullptr) return;
            if (service->m_config.forwardNonCellEvent != nullptr)
            {
                service->m_config.forwardNonCellEvent(event, service->m_config.userData);
                return;
            }
            if (event.type == vanguard::world::StreamingResourceEventType::ReleaseRequested)
                static_cast<void>(service->m_worldService->Executor()->CompleteRelease(event.key));
        }

        [[nodiscard]] static vanguard::entities::CellStreamingSystem* AllocateCellStreamingSystem(
            const vanguard::entities::CellStreamingSystemConfig& config) noexcept
        {
            vanguard::memory::MemoryBlock block = vanguard::memory::Allocate(
                vanguard::memory::PoolId::Gameplay, sizeof(vanguard::entities::CellStreamingSystem),
                alignof(vanguard::entities::CellStreamingSystem));
            return block ? ::new (block.address) vanguard::entities::CellStreamingSystem(config) : nullptr;
        }

        void DeleteCellStreamingSystem() noexcept
        {
            if (m_cellStreaming == nullptr) return;
            m_cellStreaming->~CellStreamingSystem();
            vanguard::memory::MemoryBlock block{
                m_cellStreaming, sizeof(vanguard::entities::CellStreamingSystem), vanguard::memory::PoolId::Gameplay};
            vanguard::memory::Free(block);
            m_cellStreaming = nullptr;
        }

        vanguard::engine::FramePipelineService* m_framePipeline = nullptr;
        vanguard::engine::WorldService* m_worldService = nullptr;
        vanguard::engine::ResourceStreamingService* m_streamingService = nullptr;
        vanguard::engine::GameWorldServiceConfig m_config;
        vanguard::entities::CellDecoderContext m_cellDecoderContext;
        vanguard::streaming::DecoderDescriptor m_cellDecoder;
        vanguard::game::GameWorld m_world;
        vanguard::entities::CellStreamingSystem* m_cellStreaming = nullptr;
        vanguard::world::StreamingObserver m_defaultObserver;
        vanguard::f64 m_cameraPosition[3]{};
        vanguard::engine::GameWorldStatus m_status = vanguard::engine::GameWorldStatus::Idle;
        bool m_prefabDecoderRegistered = false;
        bool m_cellDecoderRegistered = false;
    };

    app::Service* CreateGameWorldService(void*) noexcept
    {
        vanguard::memory::MemoryBlock block = vanguard::memory::Allocate(
            vanguard::memory::PoolId::Gameplay, sizeof(ManagedGameWorldService), alignof(ManagedGameWorldService));
        return block ? ::new (block.address) ManagedGameWorldService() : nullptr;
    }

    void DestroyGameWorldService(app::Service* const service, void*) noexcept
    {
        if (service == nullptr) return;
        static_cast<ManagedGameWorldService*>(service)->~ManagedGameWorldService();
        vanguard::memory::MemoryBlock block{
            service, sizeof(ManagedGameWorldService), vanguard::memory::PoolId::Gameplay};
        vanguard::memory::Free(block);
    }
}

namespace vanguard::engine
{
    bool RegisterGameWorldService(application::EngineHost& host, application::HostFailure* const failure) noexcept
    {
        constexpr application::ServiceDependency dependencies[]{
            {FramePipelineServiceId, application::DependencyKind::Required},
            {WorldServiceId, application::DependencyKind::Required},
            {ResourceStreamingServiceId, application::DependencyKind::Required}};
        constexpr application::CapabilityId providedCapabilities[]{GameWorldCapabilityId};
        application::ServiceDescriptor descriptor;
        descriptor.id = GameWorldServiceId;
        descriptor.name = "gameWorld";
        descriptor.profiles = application::ApplicationProfile::Runtime | application::ApplicationProfile::Editor;
        descriptor.scope = application::ServiceScope::Engine;
        descriptor.affinity = application::ThreadAffinity::MainThread;
        descriptor.dependencies = {dependencies, 3};
        descriptor.provides = {providedCapabilities, 1};
        descriptor.create = CreateGameWorldService;
        descriptor.destroy = DestroyGameWorldService;
        return host.RegisterService(EngineModuleId, descriptor, failure);
    }

    GameWorldService* FindGameWorldService(application::EngineHost& host) noexcept
    {
        return static_cast<GameWorldService*>(host.FindCapability(GameWorldCapabilityId));
    }

    GameWorldService* FindGameWorldService(application::ServiceContext& context) noexcept
    {
        return static_cast<GameWorldService*>(context.FindCapability(GameWorldCapabilityId));
    }
} // namespace vanguard::engine
