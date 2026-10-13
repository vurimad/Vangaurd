#include <vanguard/engine/game_world_service.hpp>

#include <vanguard/engine/resource_streaming_service.hpp>
#include <vanguard/engine/resources_service.hpp>
#include <vanguard/engine/rendering_service.hpp>
#include <vanguard/engine/world_service.hpp>
#include <vanguard/engine/game_world_service_detail.hpp>
#include <vanguard/diagnostics/diagnostics.hpp>
#include <vanguard/entities/static_mesh_component.hpp>
#include <vanguard/entities/scene_components.hpp>
#include <vanguard/entities/transform_component.hpp>
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

        [[nodiscard]] bool BeginWorld(const vanguard::world::WorldCameraLocator startupCamera) noexcept override
        {
            if (m_status != vanguard::engine::GameWorldStatus::Idle || m_transformTail.IsValid() || m_worldService == nullptr ||
                m_worldService->GetStatus() != vanguard::engine::WorldResourceStatus::Ready || m_worldService->GetExecutor() == nullptr ||
                m_worldService->GetResource() == nullptr)
                return false;
            m_stopFailure = {};
            m_stopPhase = StopPhase::None;
            m_stopStreamingCallbackFailed = false;
            m_rendererOwnersAbandoned = false;
            if (!m_worldCameraResolver.Begin(startupCamera))
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
                !m_rendering->BindMeshResidency(m_renderingService->GetMeshResidency()) ||
                !m_rendering->BindCommands(m_renderingService->GetCommands(), m_renderingService->GetRenderPhases(),
                                           m_renderingService->GetDefaultCameraPhases()) ||
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
                m_worldCameraResolver.Reset();
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
                m_worldCameraResolver.Reset();
                m_status = vanguard::engine::GameWorldStatus::Failed;
                return false;
            }
            m_status = vanguard::engine::GameWorldStatus::Running;
            return true;
        }

        [[nodiscard]] bool SetWorldCameraLocator(const vanguard::world::WorldCameraLocator camera) noexcept override
        {
            if (m_status != vanguard::engine::GameWorldStatus::Running)
                return false;
            return m_worldCameraResolver.Begin(camera);
        }

        [[nodiscard]] bool SetStreamingInput(const vanguard::world::StreamingProcessInput& input) noexcept override
        {
            return m_status == vanguard::engine::GameWorldStatus::Running && m_cellStreaming != nullptr && m_cellStreaming->SetProcessInput(input);
        }

        [[nodiscard]] bool Tick(const vanguard::f32 deltaSeconds) noexcept override
        {
            if (m_status != vanguard::engine::GameWorldStatus::Running || !CompleteTransforms() || !m_world.Tick(deltaSeconds) ||
                m_status != vanguard::engine::GameWorldStatus::Running || !ResolveWorldCamera() || !DispatchTransforms())
            {
                if (m_status == vanguard::engine::GameWorldStatus::Running)
                    m_status = vanguard::engine::GameWorldStatus::Failed;
                return false;
            }
            return true;
        }

        [[nodiscard]] vanguard::engine::GameWorldStopResult StopWorld(vanguard::engine::GameWorldStopFailure* const failure) noexcept override
        {
            using StopResult = vanguard::engine::GameWorldStopResult;
            if (failure != nullptr)
                *failure = {};
            if (m_status == vanguard::engine::GameWorldStatus::Idle)
                return StopResult::Complete;
            if (m_stopPhase == StopPhase::FailedComplete)
            {
                CopyStopFailure(failure);
                return StopResult::Failure;
            }
            if (!m_world.IsInitialized())
            {
                static_cast<void>(m_world.Shutdown());
                DeleteCellStreamingSystem();
                DeleteRenderingRuntime();
                DeleteTransformRuntime();
                DeleteComponentRuntime();
                m_worldCameraResolver.Reset();
                m_status = vanguard::engine::GameWorldStatus::Idle;
                m_stopPhase = StopPhase::None;
                m_stopStreamingCallbackFailed = false;
                m_rendererOwnersAbandoned = false;
                return StopResult::Complete;
            }

            if (m_stopPhase == StopPhase::None)
            {
                static_cast<void>(JoinTransformTailForStop());
                m_worldCameraResolver.Revoke();
                vanguard::world::WorldStreamingGrid* const grid = m_worldService->GetGrid();
                if (grid == nullptr || (!grid->IsShutdownRequested() && !grid->RequestShutdown()))
                {
                    LatchStopFailure(vanguard::engine::GameWorldStopFailureCode::Streaming, "world streaming shutdown request failed");
                    CopyStopFailure(failure);
                    return StopResult::Failure;
                }
                vanguard::entities::RenderingReleaseFailure releaseFailure;
                if (m_rendering == nullptr || !m_rendering->BeginReleaseOnly(&releaseFailure))
                {
                    if (releaseFailure.code == vanguard::entities::RenderingReleaseFailureCode::DistantProxy)
                        LatchStopFailure(vanguard::engine::GameWorldStopFailureCode::Streaming,
                                         releaseFailure.message != nullptr ? releaseFailure.message : "distant proxy release cutover failed",
                                         nullptr, nullptr, nullptr, nullptr, &releaseFailure.distantProxyFailure);
                    else
                        LatchStopFailure(vanguard::engine::GameWorldStopFailureCode::RendererProgress,
                                         releaseFailure.message != nullptr ? releaseFailure.message : "renderer release cutover failed",
                                         nullptr, nullptr, &releaseFailure.sceneFailure);
                    CopyStopFailure(failure);
                    return StopResult::Failure;
                }
                if (m_cellStreaming == nullptr || !m_cellStreaming->BeginReleaseOnly())
                {
                    LatchStopFailure(vanguard::engine::GameWorldStopFailureCode::Streaming, "cell streaming release cutover failed");
                    CopyStopFailure(failure);
                    return StopResult::Failure;
                }
                m_status = vanguard::engine::GameWorldStatus::Draining;
                m_stopPhase = StopPhase::AcceptedWork;
            }

            if (m_stopPhase == StopPhase::AcceptedWork)
            {
                const TerminalCommitStatus commit = CommitAcceptedWorkSlice();
                if (commit == TerminalCommitStatus::Failed)
                {
                    CopyStopFailure(failure);
                    return StopResult::Failure;
                }
                if (commit == TerminalCommitStatus::Pending)
                {
                    // A renderer-backed component can be the owner preventing this
                    // accepted ECS slice from completing. Progress only the existing
                    // renderer owner here; no GameWorld/frame callback is re-entered.
                    vanguard::engine::RenderingProgressFailure renderingFailure;
                    const vanguard::engine::RenderingSessionDrainResult rendering =
                        m_renderingService->ProgressSessionDrain(m_rendering->GetScene(), &renderingFailure,
                                                                 m_rendering->RequiresReleaseFrameTick());
                    if (rendering.status == vanguard::engine::RenderingSessionDrainStatus::Failed)
                    {
                        vanguard::engine::detail::ApplyRendererProgressFailure(renderingFailure,
                                                                               "renderer progression failed while draining accepted component work",
                                                                               m_rendererOwnersAbandoned, m_stopFailure);
                        CopyStopFailure(failure);
                        return StopResult::Failure;
                    }
                    CopyStopFailure(failure);
                    return StopResult::Pending;
                }
                m_stopPhase = StopPhase::ReleaseStreaming;
            }

            if (m_stopPhase == StopPhase::ReleaseStreaming)
            {
                vanguard::entities::CellStreamingReleaseFailure streamingFailure;
                m_stopStreamingCallbackFailed = false;
                const vanguard::entities::CellStreamingReleaseStatus streaming = m_cellStreaming->ProgressReleaseOnly(&streamingFailure);
                if (streaming == vanguard::entities::CellStreamingReleaseStatus::Failed || m_stopStreamingCallbackFailed)
                {
                    vanguard::rendering::RenderSceneFailure proxyFailure;
                    const bool hasProxyFailure = m_rendering != nullptr && m_rendering->ConsumeProxyLifecycleFailure(proxyFailure);
                    LatchStopFailure(vanguard::engine::GameWorldStopFailureCode::Streaming,
                                     streamingFailure.message != nullptr ? streamingFailure.message
                                                                         : "release-only streaming progression failed",
                                     nullptr, nullptr, hasProxyFailure ? &proxyFailure : nullptr, &streamingFailure);
                    CopyStopFailure(failure);
                    return StopResult::Failure;
                }

                const TerminalCommitStatus commit = CommitAcceptedWorkSlice();
                if (commit == TerminalCommitStatus::Failed)
                {
                    CopyStopFailure(failure);
                    return StopResult::Failure;
                }
                if (m_rendererOwnersAbandoned && commit == TerminalCommitStatus::Pending)
                {
                    // Component-owned admissions/mesh/camera state must finish its
                    // exact ECS synchronization before device-abandonment may bypass
                    // renderer policy/GPU waits.
                    CopyStopFailure(failure);
                    return StopResult::Failure;
                }

                vanguard::entities::RenderingReleaseFailure releaseFailure;
                vanguard::entities::RenderingReleaseStatus preRenderer = vanguard::entities::RenderingReleaseStatus::Complete;
                if (!m_rendererOwnersAbandoned)
                    preRenderer = m_rendering->ProgressReleaseOnly(0, false, &releaseFailure);
                if (preRenderer == vanguard::entities::RenderingReleaseStatus::Failed)
                {
                    if (releaseFailure.code == vanguard::entities::RenderingReleaseFailureCode::DistantProxy)
                        LatchStopFailure(vanguard::engine::GameWorldStopFailureCode::Streaming,
                                         releaseFailure.message != nullptr ? releaseFailure.message : "distant proxy ownership release failed",
                                         nullptr, nullptr, nullptr, nullptr, &releaseFailure.distantProxyFailure);
                    else
                        LatchStopFailure(vanguard::engine::GameWorldStopFailureCode::RendererProgress,
                                         releaseFailure.message != nullptr ? releaseFailure.message : "renderer ownership release failed",
                                         nullptr, nullptr, &releaseFailure.sceneFailure);
                    CopyStopFailure(failure);
                    return StopResult::Failure;
                }

                vanguard::engine::RenderingProgressFailure renderingFailure;
                vanguard::engine::RenderingSessionDrainResult rendering;
                rendering.status = vanguard::engine::RenderingSessionDrainStatus::Complete;
                vanguard::entities::RenderingReleaseStatus postRenderer = vanguard::entities::RenderingReleaseStatus::Complete;
                if (!m_rendererOwnersAbandoned)
                    rendering = m_renderingService->ProgressSessionDrain(m_rendering->GetScene(), &renderingFailure,
                                                                        m_rendering->RequiresReleaseFrameTick());
                if (rendering.status == vanguard::engine::RenderingSessionDrainStatus::Failed)
                {
                    vanguard::engine::detail::ApplyRendererProgressFailure(renderingFailure, "renderer session drain failed",
                                                                           m_rendererOwnersAbandoned, m_stopFailure);
                    if (!m_rendererOwnersAbandoned)
                    {
                        CopyStopFailure(failure);
                        return StopResult::Failure;
                    }
                }

                if (m_rendererOwnersAbandoned)
                {
                    // RenderingService has already terminalized GPU
                    // publication/residency ownership. Settle only world-scoped CPU
                    // scene/executor ownership; no renderer epoch or fence is created.
                    postRenderer = m_rendering->AbandonDeviceReleaseOnly(&releaseFailure);
                }
                else
                {
                    postRenderer =
                        m_rendering->ProgressReleaseOnly(rendering.rendererProgressEpoch, rendering.frameTickDispatched, &releaseFailure);
                }
                if (postRenderer != vanguard::entities::RenderingReleaseStatus::Complete)
                {
                    if (postRenderer == vanguard::entities::RenderingReleaseStatus::Failed)
                    {
                        if (releaseFailure.code == vanguard::entities::RenderingReleaseFailureCode::DistantProxy)
                            LatchStopFailure(vanguard::engine::GameWorldStopFailureCode::Streaming,
                                             releaseFailure.message != nullptr ? releaseFailure.message
                                                                               : "distant proxy retirement progression failed",
                                             nullptr, nullptr, nullptr, nullptr, &releaseFailure.distantProxyFailure);
                        else
                            LatchStopFailure(vanguard::engine::GameWorldStopFailureCode::RendererProgress,
                                             releaseFailure.message != nullptr ? releaseFailure.message : "renderer retirement progression failed",
                                             nullptr, nullptr, &releaseFailure.sceneFailure);
                    }
                    CopyStopFailure(failure);
                    return m_rendererOwnersAbandoned ? StopResult::Failure : StopResult::Pending;
                }

                const vanguard::world::StreamingExecutorStats executor = m_worldService->GetExecutor()->GetStats();
                const vanguard::world::WorldStreamingGrid* const grid = m_worldService->GetGrid();
                if (grid == nullptr)
                {
                    LatchStopFailure(vanguard::engine::GameWorldStopFailureCode::Streaming,
                                     "world streaming grid disappeared during terminal drain");
                    CopyStopFailure(failure);
                    return StopResult::Failure;
                }
                const vanguard::world::StreamingGridStats gridStats = grid->GetStats();
                const vanguard::entities::CellStreamingSystemStats cells = m_cellStreaming->GetStats();
                const vanguard::ecs::WorldStats entities = m_world.GetEntities().GetStats();
                if (streaming != vanguard::entities::CellStreamingReleaseStatus::Complete ||
                    (!m_rendererOwnersAbandoned && preRenderer != vanguard::entities::RenderingReleaseStatus::Complete) ||
                    postRenderer != vanguard::entities::RenderingReleaseStatus::Complete ||
                    (!m_rendererOwnersAbandoned && rendering.status != vanguard::engine::RenderingSessionDrainStatus::Complete) ||
                    commit != TerminalCommitStatus::Complete || entities.pendingActions != 0 || entities.pendingComponentActions != 0 ||
                    executor.requestingNodes != 0 || executor.residentNodes != 0 || executor.releasePendingNodes != 0 || executor.failedNodes != 0 ||
                    cells.trackedCells != 0 || gridStats.inRangeNodes != 0 || gridStats.streamingNodes != 0 || gridStats.streamedNodes != 0 ||
                    gridStats.failedNodes != 0)
                {
                    CopyStopFailure(failure);
                    return StopResult::Pending;
                }

                vanguard::rendering::RenderSceneFailure sceneFailure;
                if (!m_rendering->ReleaseScene(&sceneFailure))
                {
                    if (sceneFailure.code == vanguard::rendering::RenderSceneFailureCode::Busy ||
                        sceneFailure.code == vanguard::rendering::RenderSceneFailureCode::ProxiesRemainAlive ||
                        sceneFailure.code == vanguard::rendering::RenderSceneFailureCode::PendingDestroy)
                    {
                        CopyStopFailure(failure);
                        return StopResult::Pending;
                    }
                    LatchStopFailure(vanguard::engine::GameWorldStopFailureCode::SceneRelease,
                                     sceneFailure.message != nullptr ? sceneFailure.message : "RenderScene release failed",
                                     nullptr, nullptr, &sceneFailure);
                    CopyStopFailure(failure);
                    return StopResult::Failure;
                }
                m_stopPhase = StopPhase::ShutdownWorld;
            }

            if (m_stopPhase != StopPhase::ShutdownWorld)
            {
                CopyStopFailure(failure);
                return StopResult::Pending;
            }

            if (!m_world.Shutdown())
            {
                LatchStopFailure(vanguard::engine::GameWorldStopFailureCode::WorldShutdown, "GameWorld shutdown rejected drained ownership state");
                CopyStopFailure(failure);
                return StopResult::Failure;
            }
            DeleteCellStreamingSystem();
            DeleteRenderingRuntime();
            DeleteTransformRuntime();
            DeleteComponentRuntime();
            m_worldCameraResolver.Reset();
            if (m_stopFailure.code != vanguard::engine::GameWorldStopFailureCode::None)
            {
                m_status = vanguard::engine::GameWorldStatus::Failed;
                m_stopPhase = StopPhase::FailedComplete;
                CopyStopFailure(failure);
                return StopResult::Failure;
            }
            m_status = vanguard::engine::GameWorldStatus::Idle;
            m_stopPhase = StopPhase::None;
            m_stopStreamingCallbackFailed = false;
            m_rendererOwnersAbandoned = false;
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
        [[nodiscard]] const vanguard::engine::WorldCameraResolution& GetWorldCameraResolution() const noexcept override
        {
            return m_worldCameraResolver.GetResolution();
        }
        [[nodiscard]] bool GetWorldCameraPose(vanguard::engine::WorldCameraPose& pose) const noexcept override
        {
            pose = {};
            if (m_components == nullptr || m_renderingService == nullptr)
                return false;
            const vanguard::engine::WorldCameraResolution& resolution = m_worldCameraResolver.GetResolution();
            if (resolution.status != vanguard::engine::WorldCameraResolutionStatus::Ready || !resolution.component.IsValid() ||
                !resolution.camera.IsValid())
                return false;
            const vanguard::entities::Component* const stored = m_components->GetDirectory().Get(resolution.component);
            if (stored == nullptr || stored->GetEntity() != resolution.locator.entityId ||
                stored->GetStableId() != resolution.locator.componentStableId || stored->GetType() != vanguard::entities::CameraComponentType ||
                !stored->IsInitialized() || !stored->IsAttached() || !stored->IsPostAttached())
                return false;
            const auto* const camera = static_cast<const vanguard::entities::CameraComponent*>(stored);
            if (camera->GetCamera() != resolution.camera || !m_renderingService->GetCameras().IsAlive(resolution.camera))
                return false;
            pose.position = camera->GetWorldPosition();
            pose.orientation = camera->GetWorldOrientation();
            if (!pose.IsValid())
            {
                pose = {};
                return false;
            }
            return true;
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
            const bool terminalState = m_status == vanguard::engine::GameWorldStatus::Idle ||
                                       (m_status == vanguard::engine::GameWorldStatus::Failed && m_stopPhase == StopPhase::FailedComplete);
            const bool ownershipEmpty = !m_transformTail.IsValid() && !m_world.IsInitialized() && m_cellStreaming == nullptr &&
                                        m_rendering == nullptr && m_transforms == nullptr && m_components == nullptr;
            return terminalState && ownershipEmpty
                       ? app::LifecycleStatus::Success()
                       : app::LifecycleStatus::Failure("Game World and its transform CPU tail must be explicitly drained before shutdown");
        }

        app::LifecycleStatus OnShutdown(app::ServiceContext&) noexcept override
        {
            const bool terminalState = m_status == vanguard::engine::GameWorldStatus::Idle ||
                                       (m_status == vanguard::engine::GameWorldStatus::Failed && m_stopPhase == StopPhase::FailedComplete);
            const bool ownershipEmpty = !m_transformTail.IsValid() && !m_world.IsInitialized() && m_cellStreaming == nullptr &&
                                        m_rendering == nullptr && m_transforms == nullptr && m_components == nullptr;
            if (!terminalState || !ownershipEmpty)
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
        [[nodiscard]] bool ResolveWorldCamera() noexcept
        {
            if (m_cellStreaming == nullptr || m_renderingService == nullptr || m_components == nullptr || m_worldService == nullptr)
            {
                m_status = vanguard::engine::GameWorldStatus::Failed;
                return false;
            }
            const vanguard::world::WorldStreamingGrid* const grid = m_worldService->GetGrid();
            if (grid == nullptr)
            {
                m_status = vanguard::engine::GameWorldStatus::Failed;
                return false;
            }
            vanguard::entities::CellMaterializer* const materializer = m_cellStreaming->GetMaterializer();
            vanguard::entities::EntityReferenceRegistry* const references = m_cellStreaming->GetReferences();
            if (materializer == nullptr || references == nullptr)
            {
                m_status = vanguard::engine::GameWorldStatus::Failed;
                return false;
            }
            if (m_worldCameraResolver.Update(*grid, *materializer, *references, m_components->GetDirectory(),
                                             m_renderingService->GetCameras()))
                return true;
            m_status = vanguard::engine::GameWorldStatus::Failed;
            return false;
        }

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

        enum class StopPhase : vanguard::u8
        {
            None,
            AcceptedWork,
            ReleaseStreaming,
            ShutdownWorld,
            FailedComplete
        };

        enum class TerminalCommitStatus : vanguard::u8
        {
            Complete,
            Pending,
            Failed
        };

        void LatchStopFailure(const vanguard::engine::GameWorldStopFailureCode code, const char* const message,
                              const vanguard::entities::TransformRuntimeFailure* const transformFailure = nullptr,
                              const vanguard::engine::RenderingProgressFailure* const renderingFailure = nullptr,
                              const vanguard::rendering::RenderSceneFailure* const sceneFailure = nullptr,
                              const vanguard::entities::CellStreamingReleaseFailure* const streamingFailure = nullptr,
                              const vanguard::entities::DistantProxyStreamingFailure* const distantProxyFailure = nullptr) noexcept
        {
            if (m_stopFailure.code != vanguard::engine::GameWorldStopFailureCode::None)
                return;
            m_stopFailure.code = code;
            m_stopFailure.message = message;
            if (transformFailure != nullptr)
                m_stopFailure.transformFailure = *transformFailure;
            if (renderingFailure != nullptr)
                m_stopFailure.renderingFailure = *renderingFailure;
            if (sceneFailure != nullptr)
                m_stopFailure.sceneFailure = *sceneFailure;
            if (streamingFailure != nullptr)
                m_stopFailure.streamingFailure = *streamingFailure;
            if (distantProxyFailure != nullptr)
                m_stopFailure.distantProxyFailure = *distantProxyFailure;
        }

        void CopyStopFailure(vanguard::engine::GameWorldStopFailure* const failure) const noexcept
        {
            if (failure != nullptr)
                *failure = m_stopFailure;
        }

        [[nodiscard]] bool JoinTransformTailForStop() noexcept
        {
            bool succeeded = true;
            if (m_transformTail.IsValid())
            {
                if (!m_transformTail.WaitOnProcessFrame())
                {
                    LatchStopFailure(vanguard::engine::GameWorldStopFailureCode::Transform,
                                     "already-dispatched transform CPU work failed during stop");
                    succeeded = false;
                }
                m_transformTail = {};
            }
            vanguard::entities::VisualRelinkFailure relinkFailure;
            if (m_rendering != nullptr && m_rendering->ConsumeRelinkFailure(relinkFailure))
            {
                LatchStopFailure(vanguard::engine::GameWorldStopFailureCode::Transform,
                                 relinkFailure.sceneFailure.message != nullptr ? relinkFailure.sceneFailure.message
                                                                               : "transform relink failed before stop");
                succeeded = false;
            }
            vanguard::rendering::RenderSceneFailure proxyFailure;
            if (m_rendering != nullptr && m_rendering->ConsumeProxyLifecycleFailure(proxyFailure))
            {
                LatchStopFailure(vanguard::engine::GameWorldStopFailureCode::RendererProgress,
                                 proxyFailure.message != nullptr ? proxyFailure.message : "proxy lifecycle failed before stop",
                                 nullptr, nullptr, &proxyFailure);
                succeeded = false;
            }
            return succeeded;
        }

        [[nodiscard]] bool SynchronizeTerminalConsumers() noexcept
        {
            if (m_components == nullptr || m_transforms == nullptr)
            {
                LatchStopFailure(vanguard::engine::GameWorldStopFailureCode::Component,
                                 "terminal ECS synchronization lost its runtime consumers");
                return false;
            }
            const bool componentsSynchronized = m_components->SynchronizeCommittedChanges(m_world);
            vanguard::rendering::RenderSceneFailure proxyFailure;
            if (m_rendering != nullptr && m_rendering->ConsumeProxyLifecycleFailure(proxyFailure))
            {
                LatchStopFailure(vanguard::engine::GameWorldStopFailureCode::Component,
                                 proxyFailure.message != nullptr ? proxyFailure.message
                                                                 : "component renderer lifecycle failed during terminal synchronization",
                                 nullptr, nullptr, &proxyFailure);
                return false;
            }
            if (!componentsSynchronized)
            {
                LatchStopFailure(vanguard::engine::GameWorldStopFailureCode::Component,
                                 m_components->GetSynchronizationFailure() != nullptr ? m_components->GetSynchronizationFailure()
                                                                                     : "component committed-change synchronization failed");
                return false;
            }
            vanguard::entities::TransformRuntimeFailure transformFailure;
            if (!m_transforms->Synchronize(&transformFailure))
            {
                LatchStopFailure(vanguard::engine::GameWorldStopFailureCode::Transform,
                                 transformFailure.message != nullptr ? transformFailure.message : "transform committed-change synchronization failed",
                                 &transformFailure);
                return false;
            }
            // Transform synchronization may capture owner-component changes directly into
            // the same journal. Consume those before the next producer slice can overwrite them.
            const bool componentsResynchronized = m_components->SynchronizeCommittedChanges(m_world);
            proxyFailure = {};
            if (m_rendering != nullptr && m_rendering->ConsumeProxyLifecycleFailure(proxyFailure))
            {
                LatchStopFailure(vanguard::engine::GameWorldStopFailureCode::Component,
                                 proxyFailure.message != nullptr ? proxyFailure.message
                                                                 : "component renderer lifecycle failed after transform ownership updates",
                                 nullptr, nullptr, &proxyFailure);
                return false;
            }
            if (!componentsResynchronized)
            {
                LatchStopFailure(vanguard::engine::GameWorldStopFailureCode::Component,
                                 m_components->GetSynchronizationFailure() != nullptr ? m_components->GetSynchronizationFailure()
                                                                                     : "component synchronization failed after transform ownership updates");
                return false;
            }
            transformFailure = {};
            if (!m_transforms->Synchronize(&transformFailure))
            {
                LatchStopFailure(vanguard::engine::GameWorldStopFailureCode::Transform,
                                 transformFailure.message != nullptr ? transformFailure.message
                                                                     : "transform synchronization failed after ownership updates",
                                 &transformFailure);
                return false;
            }
            return true;
        }

        [[nodiscard]] TerminalCommitStatus CommitAcceptedWorkSlice() noexcept
        {
            constexpr vanguard::u32 MaximumCommandsPerSlice = 4096u;
            vanguard::u32 maximumCommands = MaximumCommandsPerSlice;
            if (m_config.world.entities.committedChangeCapacity < maximumCommands)
                maximumCommands = m_config.world.entities.committedChangeCapacity;
            if (m_config.transforms.maximumChangesPerSynchronize < maximumCommands)
                maximumCommands = m_config.transforms.maximumChangesPerSynchronize;
            if (maximumCommands == 0)
            {
                LatchStopFailure(vanguard::engine::GameWorldStopFailureCode::Component,
                                 "terminal ECS synchronization has no committed-change capacity");
                return TerminalCommitStatus::Failed;
            }

            vanguard::ecs::World& entities = m_world.GetEntities();
            vanguard::ecs::WorldStats stats = entities.GetStats();
            if (stats.pendingActions != 0)
            {
                vanguard::ecs::ActionReport report;
                if (!entities.FlushActions(maximumCommands, &report) || report.rejected != 0)
                {
                    LatchStopFailure(vanguard::engine::GameWorldStopFailureCode::Component,
                                     "accepted structural ECS work failed during terminal commit");
                    return TerminalCommitStatus::Failed;
                }
                if (!SynchronizeTerminalConsumers())
                    return TerminalCommitStatus::Failed;
                if (entities.GetStats().pendingActions != 0)
                    return TerminalCommitStatus::Pending;
            }

            stats = entities.GetStats();
            if (stats.pendingComponentActions != 0)
            {
                vanguard::ecs::ComponentActionReport report;
                if (!entities.FlushComponentActions(maximumCommands, &report) || report.rejected != 0)
                {
                    LatchStopFailure(vanguard::engine::GameWorldStopFailureCode::Component,
                                     "accepted component ECS work failed during terminal commit");
                    return TerminalCommitStatus::Failed;
                }
                if (!SynchronizeTerminalConsumers())
                    return TerminalCommitStatus::Failed;
            }

            stats = entities.GetStats();
            return stats.pendingActions != 0 || stats.pendingComponentActions != 0 ? TerminalCommitStatus::Pending
                                                                                   : TerminalCommitStatus::Complete;
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
                {
                    VG_LOG_ERROR(vanguard::diagnostics::Category::Engine, "Game World transform completion failed while joining the dispatched transform CPU tail");
                    return false;
                }
                m_transformTail = {};
            }
            vanguard::entities::VisualRelinkFailure failure;
            if (m_rendering != nullptr && !m_rendering->FlushCameraTransforms())
            {
                VG_LOG_ERROR(vanguard::diagnostics::Category::Engine, "Game World transform completion failed while publishing camera transforms");
                m_status = vanguard::engine::GameWorldStatus::Failed;
                return false;
            }
            if (m_rendering != nullptr && m_rendering->ConsumeRelinkFailure(failure))
            {
                VG_LOG_ERROR(vanguard::diagnostics::Category::Engine, "Game World transform completion consumed a visual relink failure: %s",
                             failure.sceneFailure.message != nullptr ? failure.sceneFailure.message : "<none>");
                m_status = vanguard::engine::GameWorldStatus::Failed;
                return false;
            }
            vanguard::rendering::RenderSceneFailure proxyFailure;
            if (m_rendering != nullptr && m_rendering->ConsumeProxyLifecycleFailure(proxyFailure))
            {
                VG_LOG_ERROR(vanguard::diagnostics::Category::Engine, "Game World transform completion consumed a proxy lifecycle failure: %s",
                             proxyFailure.message != nullptr ? proxyFailure.message : "<none>");
                m_status = vanguard::engine::GameWorldStatus::Failed;
                return false;
            }
            return true;
        }

        static bool RegisterComponents(vanguard::entities::ComponentRegistry& registry, void* const userData) noexcept
        {
            auto* const service = static_cast<ManagedGameWorldService*>(userData);
            return service != nullptr &&
                   vanguard::entities::RegisterTransformComponent(registry) &&
                   vanguard::entities::RegisterStaticMeshComponent(registry) &&
                   vanguard::entities::RegisterLightComponent(registry) &&
                   vanguard::entities::RegisterCameraComponent(registry) &&
                   (service->m_config.registerComponents == nullptr || service->m_config.registerComponents(registry, service->m_config.userData));
        }

        static void ForwardNonCellEvent(const vanguard::world::StreamingResourceEvent& event, void* const userData) noexcept
        {
            auto* const service = static_cast<ManagedGameWorldService*>(userData);
            if (service == nullptr)
                return;
            vanguard::entities::DistantProxyStreamingFailure distantFailure;
            if (service->m_rendering == nullptr)
            {
                distantFailure = {vanguard::entities::DistantProxyStreamingFailureCode::InvalidState, event.key,
                                  vanguard::resources::Failure::None, {}, "distant proxy streaming lost its rendering runtime owner"};
            }
            if (service->m_rendering == nullptr || !service->m_rendering->HandleStreamingEvent(event, &distantFailure))
            {
                service->LatchStopFailure(vanguard::engine::GameWorldStopFailureCode::Streaming,
                                          distantFailure.message != nullptr ? distantFailure.message
                                                                            : "distant proxy streaming progression failed",
                                          nullptr, nullptr, nullptr, nullptr, &distantFailure);
                if (service->m_stopPhase == StopPhase::None)
                    service->m_status = vanguard::engine::GameWorldStatus::Failed;
                else
                    service->m_stopStreamingCallbackFailed = true;
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
        vanguard::engine::WorldCameraResolver m_worldCameraResolver;
        vanguard::engine::GameWorldStatus m_status = vanguard::engine::GameWorldStatus::Idle;
        StopPhase m_stopPhase = StopPhase::None;
        vanguard::engine::GameWorldStopFailure m_stopFailure;
        bool m_stopStreamingCallbackFailed = false;
        bool m_rendererOwnersAbandoned = false;
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
    bool WorldCameraResolver::Begin(const world::WorldCameraLocator locator) noexcept
    {
        const bool hasAnyField = locator.cellId != world::InvalidCellId || locator.entityId != world::InvalidEntityId ||
                                 locator.componentStableId != prefabs::InvalidStableId;
        const bool hasAllFields = locator.cellId != world::InvalidCellId && locator.entityId != world::InvalidEntityId &&
                                 locator.componentStableId != prefabs::InvalidStableId;
        if (hasAnyField != hasAllFields)
            return false;
        Reset();
        if (hasAllFields)
        {
            m_resolution.locator = locator;
            m_resolution.status = WorldCameraResolutionStatus::Pending;
        }
        return true;
    }

    void WorldCameraResolver::Revoke() noexcept
    {
        m_resolution.component = {};
        m_resolution.camera = {};
        m_resolution.failure = WorldCameraResolutionFailure::None;
        if (m_resolution.status == WorldCameraResolutionStatus::Ready)
            m_resolution.status = WorldCameraResolutionStatus::Pending;
    }

    void WorldCameraResolver::Reset() noexcept
    {
        m_resolution = {};
    }

    bool WorldCameraResolver::Fail(const WorldCameraResolutionFailure failure) noexcept
    {
        m_resolution.component = {};
        m_resolution.camera = {};
        m_resolution.failure = failure;
        m_resolution.status = WorldCameraResolutionStatus::Failed;
        return false;
    }

    bool WorldCameraResolver::Update(const world::WorldStreamingGrid& grid, entities::CellMaterializer& materializer,
                                     entities::EntityReferenceRegistry& references, entities::ComponentDirectory& components,
                                     const rendering::RenderCameraStorage& cameras) noexcept
    {
        if (m_resolution.status == WorldCameraResolutionStatus::None)
            return true;
        if (m_resolution.status == WorldCameraResolutionStatus::Failed)
            return false;
        if (m_resolution.status == WorldCameraResolutionStatus::Ready)
        {
            const entities::Component* const stored = components.Get(m_resolution.component);
            if (stored != nullptr && stored->GetEntity() == m_resolution.locator.entityId &&
                stored->GetStableId() == m_resolution.locator.componentStableId &&
                stored->GetType() == entities::CameraComponentType && stored->IsInitialized() &&
                stored->IsAttached() && stored->IsPostAttached())
            {
                const auto* const camera = static_cast<const entities::CameraComponent*>(stored);
                if (camera->GetCamera() == m_resolution.camera && cameras.IsAlive(m_resolution.camera))
                    return true;
            }
            Revoke();
        }

        const world::StreamingNodeKey cellKey{m_resolution.locator.cellId, world::StreamingNodeKind::Cell};
        if (grid.GetState(cellKey) == world::StreamingNodeState::Failed)
            return Fail(WorldCameraResolutionFailure::CellFailed);

        const entities::CellState cellState = materializer.GetState(m_resolution.locator.cellId);
        if (cellState == entities::CellState::Unknown || cellState == entities::CellState::PendingActivation ||
            cellState == entities::CellState::PendingReferences)
            return true;
        if (cellState == entities::CellState::Failed)
            return Fail(WorldCameraResolutionFailure::CellFailed);
        if (cellState != entities::CellState::Active)
            return Fail(WorldCameraResolutionFailure::CellUnavailable);

        const u32 cellGeneration = materializer.GetGeneration(m_resolution.locator.cellId);
        if (cellGeneration == 0 ||
            !references.HasEntityInPartition(m_resolution.locator.entityId, m_resolution.locator.cellId, cellGeneration,
                                             entities::EntityReferenceRegistry::BasePartition))
            return Fail(WorldCameraResolutionFailure::MissingEntity);
        entities::Component* const component =
            components.Find(m_resolution.locator.entityId, m_resolution.locator.componentStableId);
        if (component == nullptr)
            return Fail(WorldCameraResolutionFailure::MissingComponent);
        if (component->GetType() != entities::CameraComponentType)
            return Fail(WorldCameraResolutionFailure::WrongComponentType);
        if (!component->IsInitialized() || !component->IsAttached() || !component->IsPostAttached())
            return true;

        auto* const camera = static_cast<entities::CameraComponent*>(component);
        const rendering::RenderCameraHandle renderCamera = camera->GetCamera();
        if (!renderCamera.IsValid() || !cameras.IsAlive(renderCamera))
            return Fail(WorldCameraResolutionFailure::CameraUnavailable);

        m_resolution.component = component->GetHandle();
        m_resolution.camera = renderCamera;
        m_resolution.failure = WorldCameraResolutionFailure::None;
        m_resolution.status = WorldCameraResolutionStatus::Ready;
        return true;
    }

    const WorldCameraResolution& WorldCameraResolver::GetResolution() const noexcept
    {
        return m_resolution;
    }

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
