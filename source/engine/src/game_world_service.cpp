#include <vanguard/engine/game_world_service.hpp>

#include <vanguard/engine/resource_streaming_service.hpp>
#include <vanguard/engine/resources_service.hpp>
#include <vanguard/engine/rendering_service.hpp>
#include <vanguard/engine/world_service.hpp>
#include <vanguard/jobs/jobs.hpp>
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
        case Result::Success:
            return Failure::None;
        case Result::InvalidMagic:
        case Result::IntegrityFailure:
            return Failure::IntegrityFailure;
        case Result::UnsupportedVersion:
            return Failure::UnsupportedVersion;
        case Result::IoFailure:
            return Failure::IoFailure;
        case Result::LimitExceeded:
            return Failure::OutOfMemory;
        default:
            return Failure::DeserializationFailure;
        }
    }

    [[nodiscard]] vanguard::resources::ResourceObject* DecodePrefab(const vanguard::resources::ResourceReference reference, const void* const data,
                                                                    const vanguard::usize size, const vanguard::resources::LoadContext&,
                                                                    vanguard::resources::Failure& failure, void*) noexcept
    {
        if (reference.ExpectedType() != vanguard::prefabs::PrefabResourceType)
        {
            failure = vanguard::resources::Failure::UnknownType;
            return nullptr;
        }
        vanguard::memory::MemoryBlock block =
            vanguard::memory::Allocate(vanguard::memory::PoolId::World, sizeof(vanguard::prefabs::PrefabResource), alignof(vanguard::prefabs::PrefabResource));
        if (!block)
        {
            failure = vanguard::resources::Failure::OutOfMemory;
            return nullptr;
        }
        auto* const resource = ::new (block.address) vanguard::prefabs::PrefabResource();
        const vanguard::prefabs::Result result = resource->Open(data, size);
        if (result == vanguard::prefabs::Result::Success)
            return resource;
        resource->~PrefabResource();
        vanguard::memory::Free(block);
        failure = ConvertPrefabFailure(result);
        return nullptr;
    }

    void DestroyPrefab(vanguard::resources::ResourceObject* const object, void*) noexcept
    {
        if (object == nullptr)
            return;
        static_cast<vanguard::prefabs::PrefabResource*>(object)->~PrefabResource();
        vanguard::memory::MemoryBlock block{object, sizeof(vanguard::prefabs::PrefabResource), vanguard::memory::PoolId::World};
        vanguard::memory::Free(block);
    }

    class ManagedGameWorldService final : public vanguard::engine::GameWorldService
    {
    public:
        [[nodiscard]] bool Configure(const vanguard::engine::GameWorldServiceConfig& config) noexcept override
        {
            if (m_status != vanguard::engine::GameWorldStatus::Idle)
                return false;
            m_config = config;
            return true;
        }

        [[nodiscard]] bool BeginWorld() noexcept override
        {
            if (m_status != vanguard::engine::GameWorldStatus::Idle || m_transformTail.IsValid() || m_worldService == nullptr ||
                m_worldService->GetStatus() != vanguard::engine::WorldResourceStatus::Ready || m_worldService->GetExecutor() == nullptr ||
                m_worldService->GetResource() == nullptr)
                return false;

            vanguard::entities::CellStreamingSystemConfig systemConfig;
            systemConfig.executor = m_worldService->GetExecutor();
            systemConfig.resources = &m_resourcesService->GetRegistry();
            systemConfig.resourcePipeline = &m_resourcesService->GetPipeline();
            systemConfig.registerComponents = &RegisterComponents;
            systemConfig.forwardNonCellEvent = &ForwardNonCellEvent;
            systemConfig.userData = this;
            systemConfig.materialization = m_config.materialization;
            m_components = AllocateComponentRuntime(m_config.components);
            if (m_components != nullptr)
                systemConfig.componentDirectory = &m_components->GetDirectory();
            m_transforms = AllocateTransformRuntime(m_config.transforms);
            m_rendering = AllocateRenderingRuntime(m_renderingService->GetScenes(), m_config.rendering);
            m_cellStreaming = AllocateCellStreamingSystem(systemConfig);
            if (m_components == nullptr || m_transforms == nullptr || m_rendering == nullptr || m_cellStreaming == nullptr ||
                !m_rendering->BindDistantProxyStreaming(m_worldService->GetResource()->GetFile(), *m_worldService->GetExecutor()) ||
                !m_world.RegisterSystem(*m_components) || !m_world.RegisterSystem(*m_transforms) ||
                !m_world.RegisterSystem(*m_rendering) || !m_world.RegisterSystem(*m_cellStreaming) || !m_world.Initialize(m_config.world))
            {
                // RegisterSystem materializes GameWorld state before Initialize. Always tear
                // that state down before deleting the registered runtime-system objects.
                static_cast<void>(m_world.Shutdown());
                DeleteCellStreamingSystem();
                DeleteRenderingRuntime();
                DeleteTransformRuntime();
                DeleteComponentRuntime();
                m_status = vanguard::engine::GameWorldStatus::Failed;
                return false;
            }

            vanguard::world::StreamingObserver observer;
            const vanguard::f64* const origin = m_worldService->GetResource()->GetFile().GetOrigin();
            for (vanguard::u32 axis = 0; axis < 3; ++axis)
            {
                observer.predictedPosition[axis] = origin[axis];
                m_cameraPosition[axis] = origin[axis];
            }
            m_defaultObserver = observer;
            vanguard::world::StreamingProcessInput input;
            input.observers = {&m_defaultObserver, 1};
            for (vanguard::u32 axis = 0; axis < 3; ++axis)
                input.cameraPosition[axis] = m_cameraPosition[axis];
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
            return m_status == vanguard::engine::GameWorldStatus::Running && m_cellStreaming != nullptr && m_cellStreaming->SetProcessInput(input);
        }

        [[nodiscard]] bool Tick(const vanguard::f32 deltaSeconds) noexcept override
        {
            if (m_status != vanguard::engine::GameWorldStatus::Running || !CompleteTransforms() || !m_world.Tick(deltaSeconds) ||
                m_status != vanguard::engine::GameWorldStatus::Running || !DispatchTransforms())
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
            if (!CompleteTransforms())
                return StopResult::Failure;
            if (m_status == vanguard::engine::GameWorldStatus::Idle)
                return StopResult::Complete;
            if (!m_world.IsInitialized())
            {
                static_cast<void>(m_world.Shutdown());
                DeleteCellStreamingSystem();
                DeleteRenderingRuntime();
                DeleteTransformRuntime();
                DeleteComponentRuntime();
                m_status = vanguard::engine::GameWorldStatus::Idle;
                return StopResult::Complete;
            }
            if (m_status == vanguard::engine::GameWorldStatus::Running || m_status == vanguard::engine::GameWorldStatus::Failed)
            {
                vanguard::world::WorldStreamingGrid* const grid = m_worldService->GetGrid();
                if (grid == nullptr || (!grid->IsShutdownRequested() && !grid->RequestShutdown()))
                    return StopResult::Failure;
                m_status = vanguard::engine::GameWorldStatus::Draining;
            }
            if (!m_world.Tick(0.0f) || !DispatchTransforms() || !CompleteTransforms())
                return StopResult::Failure;

            const vanguard::world::StreamingExecutorStats executor = m_worldService->GetExecutor()->GetStats();
            const vanguard::entities::CellStreamingSystemStats cells = m_cellStreaming->GetStats();
            if (executor.requestingNodes != 0 || executor.residentNodes != 0 || executor.releasePendingNodes != 0 || executor.failedNodes != 0 ||
                cells.trackedCells != 0)
                return StopResult::Pending;
            if (!m_world.Shutdown())
                return StopResult::Failure;
            if (m_rendering != nullptr && !m_rendering->ReleaseScene())
                return StopResult::Failure;
            DeleteCellStreamingSystem();
            DeleteRenderingRuntime();
            DeleteTransformRuntime();
            DeleteComponentRuntime();
            m_status = vanguard::engine::GameWorldStatus::Idle;
            return StopResult::Complete;
        }

        [[nodiscard]] vanguard::engine::GameWorldStatus GetStatus() const noexcept override
        {
            return m_status;
        }
        [[nodiscard]] vanguard::game::GameWorld* GetWorld() noexcept override
        {
            return m_world.IsInitialized() ? &m_world : nullptr;
        }
        [[nodiscard]] vanguard::entities::CellStreamingSystem* GetCellStreaming() noexcept override
        {
            return m_cellStreaming;
        }
        [[nodiscard]] vanguard::entities::ComponentDirectory* GetComponents() noexcept override
        {
            return m_components != nullptr && m_components->GetDirectory().IsInitialized() ? &m_components->GetDirectory() : nullptr;
        }
        [[nodiscard]] vanguard::entities::TransformRuntime* GetTransforms() noexcept override
        {
            return m_transforms;
        }
        [[nodiscard]] vanguard::entities::RenderingRuntime* GetRendering() noexcept override
        {
            return m_rendering;
        }

    protected:
        app::LifecycleStatus OnInitialize(app::ServiceContext& context) noexcept override
        {
            m_framePipeline = vanguard::engine::FindFramePipelineService(context);
            m_worldService = vanguard::engine::FindWorldService(context);
            m_streamingService = vanguard::engine::FindResourceStreamingService(context);
            m_resourcesService = vanguard::engine::FindResourcesService(context);
            m_renderingService = vanguard::engine::FindRenderingService(context);
            if (m_framePipeline == nullptr || m_worldService == nullptr || m_streamingService == nullptr || m_resourcesService == nullptr ||
                m_renderingService == nullptr)
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
                return app::LifecycleStatus::Failure(frameFailure.message != nullptr ? frameFailure.message : "Game World frame registration failed");

            constexpr vanguard::engine::FrameParticipantId transformDependencies[]{vanguard::engine::GameWorldFrameParticipantId};
            vanguard::engine::FrameParticipantDescriptor transformCompletion;
            transformCompletion.id = vanguard::engine::GameWorldTransformCompletionParticipantId;
            transformCompletion.name = "gameWorld.transformCompletion";
            transformCompletion.phase = vanguard::engine::FramePhase::PostSimulation;
            transformCompletion.profiles = app::ApplicationProfile::Runtime | app::ApplicationProfile::Editor;
            transformCompletion.affinity = app::ThreadAffinity::MainThread;
            transformCompletion.after = {transformDependencies, 1};
            transformCompletion.execute = &ExecuteTransformCompletion;
            transformCompletion.userData = this;
            if (!m_framePipeline->RegisterParticipant(transformCompletion, &frameFailure))
                return app::LifecycleStatus::Failure(frameFailure.message != nullptr ? frameFailure.message
                                                                                     : "Game World transform completion registration failed");
            if (!m_streamingService->GetStreamer().RegisterDecoder(
                    {vanguard::prefabs::PrefabResourceType, "Vanguard prefab", &DecodePrefab, &DestroyPrefab, nullptr}))
                return app::LifecycleStatus::Failure("Prefab decoder registration failed");
            m_prefabDecoderRegistered = true;
            m_cellDecoder = vanguard::entities::MakeCellDecoder(m_cellDecoderContext);
            if (!m_streamingService->GetStreamer().RegisterDecoder(m_cellDecoder))
            {
                static_cast<void>(m_streamingService->GetStreamer().UnregisterDecoder(vanguard::prefabs::PrefabResourceType));
                m_prefabDecoderRegistered = false;
                return app::LifecycleStatus::Failure("Cell decoder registration failed");
            }
            m_cellDecoderRegistered = true;
            return app::LifecycleStatus::Success();
        }

        app::LifecycleStatus OnQuiesce(app::ServiceContext&) noexcept override
        {
            return m_status == vanguard::engine::GameWorldStatus::Idle && !m_transformTail.IsValid()
                       ? app::LifecycleStatus::Success()
                       : app::LifecycleStatus::Failure("Game World and its transform CPU tail must be explicitly drained before shutdown");
        }

        app::LifecycleStatus OnShutdown(app::ServiceContext&) noexcept override
        {
            if (m_status != vanguard::engine::GameWorldStatus::Idle || m_transformTail.IsValid() || m_world.IsInitialized() ||
                m_cellStreaming != nullptr || m_rendering != nullptr || m_transforms != nullptr || m_components != nullptr)
                return app::LifecycleStatus::Failure("Game World runtime state remains live during shutdown");
            if (m_cellDecoderRegistered && !m_streamingService->GetStreamer().UnregisterDecoder(vanguard::world::CellResourceType))
                return app::LifecycleStatus::Failure("Cell decoder unregistration failed");
            m_cellDecoderRegistered = false;
            if (m_prefabDecoderRegistered && !m_streamingService->GetStreamer().UnregisterDecoder(vanguard::prefabs::PrefabResourceType))
                return app::LifecycleStatus::Failure("Prefab decoder unregistration failed");
            m_prefabDecoderRegistered = false;
            m_framePipeline = nullptr;
            m_worldService = nullptr;
            m_streamingService = nullptr;
            m_resourcesService = nullptr;
            m_renderingService = nullptr;
            return app::LifecycleStatus::Success();
        }

    private:
        static vanguard::engine::FrameParticipantStatus ExecuteFrame(const vanguard::engine::FrameContext& context, void* const userData) noexcept
        {
            auto* const service = static_cast<ManagedGameWorldService*>(userData);
            if (service == nullptr)
                return vanguard::engine::FrameParticipantStatus::Failure("Game World frame participant lost its service");
            if (service->m_status == vanguard::engine::GameWorldStatus::Idle)
                return vanguard::engine::FrameParticipantStatus::Success();
            if (service->m_status != vanguard::engine::GameWorldStatus::Running || !service->Tick(context.simulationDeltaSeconds))
                return vanguard::engine::FrameParticipantStatus::Failure("Flecs game world tick failed");
            return vanguard::engine::FrameParticipantStatus::Success();
        }

        static vanguard::engine::FrameParticipantStatus ExecuteTransformCompletion(const vanguard::engine::FrameContext&, void* const userData) noexcept
        {
            auto* const service = static_cast<ManagedGameWorldService*>(userData);
            if (service == nullptr)
                return vanguard::engine::FrameParticipantStatus::Failure("Game World transform completion lost its service");
            return service->CompleteTransforms()
                       ? vanguard::engine::FrameParticipantStatus::Success()
                       : vanguard::engine::FrameParticipantStatus::Failure("Game World transform CPU processing failed");
        }

        [[nodiscard]] bool DispatchTransforms() noexcept
        {
            if (m_transforms == nullptr || m_transformTail.IsValid())
                return m_transforms != nullptr && !m_transformTail.IsValid();
            vanguard::jobs::Builder builder({vanguard::jobs::Priority::RenderPath, vanguard::jobs::Affinity::AnyWorker}, this);
            if (!builder.IsValid())
                return false;
            vanguard::entities::TransformUpdateDispatch dispatch;
            vanguard::entities::TransformRuntimeFailure failure;
            if (!m_transforms->DispatchUpdates(builder, dispatch, &failure))
                return false;
            if (!dispatch.dispatched)
                return true;
            m_transformTail = builder.ExtractCounter();
            return m_transformTail.IsValid();
        }

        [[nodiscard]] bool CompleteTransforms() noexcept
        {
            if (m_transformTail.IsValid())
            {
                if (!m_transformTail.WaitOnProcessFrame())
                    return false;
                m_transformTail = {};
            }
            vanguard::entities::VisualRelinkFailure failure;
            if (m_rendering != nullptr && m_rendering->ConsumeRelinkFailure(failure))
            {
                m_status = vanguard::engine::GameWorldStatus::Failed;
                return false;
            }
            vanguard::rendering::RenderSceneFailure proxyFailure;
            if (m_rendering != nullptr && m_rendering->ConsumeProxyLifecycleFailure(proxyFailure))
            {
                m_status = vanguard::engine::GameWorldStatus::Failed;
                return false;
            }
            return true;
        }

        static bool RegisterComponents(vanguard::entities::ComponentRegistry& registry, void* const userData) noexcept
        {
            auto* const service = static_cast<ManagedGameWorldService*>(userData);
            return service != nullptr &&
                   (service->m_config.registerComponents == nullptr || service->m_config.registerComponents(registry, service->m_config.userData));
        }

        static void ForwardNonCellEvent(const vanguard::world::StreamingResourceEvent& event, void* const userData) noexcept
        {
            auto* const service = static_cast<ManagedGameWorldService*>(userData);
            if (service == nullptr)
                return;
            if (service->m_rendering == nullptr || !service->m_rendering->HandleStreamingEvent(event))
            {
                service->m_status = vanguard::engine::GameWorldStatus::Failed;
                return;
            }
            if (service->m_config.forwardNonCellEvent != nullptr)
            {
                service->m_config.forwardNonCellEvent(event, service->m_config.userData);
            }
        }

        [[nodiscard]] static vanguard::entities::CellStreamingSystem* AllocateCellStreamingSystem(
            const vanguard::entities::CellStreamingSystemConfig& config) noexcept
        {
            vanguard::memory::MemoryBlock block = vanguard::memory::Allocate(
                vanguard::memory::PoolId::Gameplay, sizeof(vanguard::entities::CellStreamingSystem), alignof(vanguard::entities::CellStreamingSystem));
            return block ? ::new (block.address) vanguard::entities::CellStreamingSystem(config) : nullptr;
        }

        void DeleteCellStreamingSystem() noexcept
        {
            if (m_cellStreaming == nullptr)
                return;
            m_cellStreaming->~CellStreamingSystem();
            vanguard::memory::MemoryBlock block{m_cellStreaming, sizeof(vanguard::entities::CellStreamingSystem), vanguard::memory::PoolId::Gameplay};
            vanguard::memory::Free(block);
            m_cellStreaming = nullptr;
        }

        [[nodiscard]] static vanguard::entities::ComponentRuntime* AllocateComponentRuntime(
            const vanguard::entities::ComponentDirectoryConfig& config) noexcept
        {
            vanguard::memory::MemoryBlock block = vanguard::memory::Allocate(
                vanguard::memory::PoolId::Gameplay, sizeof(vanguard::entities::ComponentRuntime), alignof(vanguard::entities::ComponentRuntime));
            return block ? ::new (block.address) vanguard::entities::ComponentRuntime(config) : nullptr;
        }

        void DeleteComponentRuntime() noexcept
        {
            if (m_components == nullptr)
                return;
            m_components->~ComponentRuntime();
            vanguard::memory::MemoryBlock block{m_components, sizeof(vanguard::entities::ComponentRuntime), vanguard::memory::PoolId::Gameplay};
            vanguard::memory::Free(block);
            m_components = nullptr;
        }

        [[nodiscard]] static vanguard::entities::TransformRuntime* AllocateTransformRuntime(
            const vanguard::entities::TransformRuntimeConfig& config) noexcept
        {
            vanguard::memory::MemoryBlock block = vanguard::memory::Allocate(
                vanguard::memory::PoolId::Gameplay, sizeof(vanguard::entities::TransformRuntime), alignof(vanguard::entities::TransformRuntime));
            return block ? ::new (block.address) vanguard::entities::TransformRuntime(config) : nullptr;
        }

        void DeleteTransformRuntime() noexcept
        {
            if (m_transforms == nullptr)
                return;
            m_transforms->~TransformRuntime();
            vanguard::memory::MemoryBlock block{m_transforms, sizeof(vanguard::entities::TransformRuntime), vanguard::memory::PoolId::Gameplay};
            vanguard::memory::Free(block);
            m_transforms = nullptr;
        }

        [[nodiscard]] static vanguard::entities::RenderingRuntime* AllocateRenderingRuntime(
            vanguard::rendering::RenderSceneManager& scenes, const vanguard::entities::RenderingRuntimeConfig& config) noexcept
        {
            vanguard::memory::MemoryBlock block = vanguard::memory::Allocate(
                vanguard::memory::PoolId::Gameplay, sizeof(vanguard::entities::RenderingRuntime), alignof(vanguard::entities::RenderingRuntime));
            return block ? ::new (block.address) vanguard::entities::RenderingRuntime(scenes, config) : nullptr;
        }

        void DeleteRenderingRuntime() noexcept
        {
            if (m_rendering == nullptr)
                return;
            m_rendering->~RenderingRuntime();
            vanguard::memory::MemoryBlock block{m_rendering, sizeof(vanguard::entities::RenderingRuntime), vanguard::memory::PoolId::Gameplay};
            vanguard::memory::Free(block);
            m_rendering = nullptr;
        }

        vanguard::engine::FramePipelineService* m_framePipeline = nullptr;
        vanguard::engine::WorldService* m_worldService = nullptr;
        vanguard::engine::ResourceStreamingService* m_streamingService = nullptr;
        vanguard::engine::ResourcesService* m_resourcesService = nullptr;
        vanguard::engine::RenderingService* m_renderingService = nullptr;
        vanguard::engine::GameWorldServiceConfig m_config;
        vanguard::entities::CellDecoderContext m_cellDecoderContext;
        vanguard::streaming::DecoderDescriptor m_cellDecoder;
        vanguard::game::GameWorld m_world;
        vanguard::entities::ComponentRuntime* m_components = nullptr;
        vanguard::entities::TransformRuntime* m_transforms = nullptr;
        vanguard::entities::RenderingRuntime* m_rendering = nullptr;
        vanguard::entities::CellStreamingSystem* m_cellStreaming = nullptr;
        vanguard::jobs::Counter m_transformTail;
        vanguard::world::StreamingObserver m_defaultObserver;
        vanguard::f64 m_cameraPosition[3]{};
        vanguard::engine::GameWorldStatus m_status = vanguard::engine::GameWorldStatus::Idle;
        bool m_prefabDecoderRegistered = false;
        bool m_cellDecoderRegistered = false;
    };

    app::Service* CreateGameWorldService(void*) noexcept
    {
        vanguard::memory::MemoryBlock block =
            vanguard::memory::Allocate(vanguard::memory::PoolId::Gameplay, sizeof(ManagedGameWorldService), alignof(ManagedGameWorldService));
        return block ? ::new (block.address) ManagedGameWorldService() : nullptr;
    }

    void DestroyGameWorldService(app::Service* const service, void*) noexcept
    {
        if (service == nullptr)
            return;
        static_cast<ManagedGameWorldService*>(service)->~ManagedGameWorldService();
        vanguard::memory::MemoryBlock block{service, sizeof(ManagedGameWorldService), vanguard::memory::PoolId::Gameplay};
        vanguard::memory::Free(block);
    }
} // namespace

namespace vanguard::engine
{
    bool RegisterGameWorldService(application::EngineHost& host, application::HostFailure* const failure) noexcept
    {
        constexpr application::ServiceDependency dependencies[]{{FramePipelineServiceId, application::DependencyKind::Required},
                                                                {ReflectionServiceId, application::DependencyKind::Required},
                                                                {WorldServiceId, application::DependencyKind::Required},
                                                                {ResourcesServiceId, application::DependencyKind::Required},
                                                                {ResourceStreamingServiceId, application::DependencyKind::Required},
                                                                {RenderingServiceId, application::DependencyKind::Required}};
        constexpr application::CapabilityId providedCapabilities[]{GameWorldCapabilityId};
        application::ServiceDescriptor descriptor;
        descriptor.id = GameWorldServiceId;
        descriptor.name = "gameWorld";
        descriptor.profiles = application::ApplicationProfile::Runtime | application::ApplicationProfile::Editor;
        descriptor.scope = application::ServiceScope::Engine;
        descriptor.affinity = application::ThreadAffinity::MainThread;
        descriptor.dependencies = {dependencies, 6};
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
