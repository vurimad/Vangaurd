#include <vanguard/engine/world_session_service.hpp>

#include <vanguard/engine/resource_streaming_service.hpp>
#include <vanguard/engine/game_input_service.hpp>
#include <vanguard/engine/rendering_service.hpp>
#include <vanguard/engine/streaming_observer_service.hpp>
#include <vanguard/engine/world_service.hpp>
#include <vanguard/memory/memory.hpp>
#include <vanguard/world/worlds.hpp>

#include <cmath>
#include <new>

namespace
{
    namespace app = vanguard::application;
    namespace engine = vanguard::engine;
    namespace resources = vanguard::resources;

    /// One observer owns the selected session camera's streaming authority. The
    /// name is stable diagnostic text, not an identity; the generational handle is.
    inline constexpr const char* WorldSessionObserverName = "worldSessionCamera";
    /// The selected camera is the active gameplay/editor viewpoint, so it uses the
    /// existing on-foot observer velocity class with a one-second predictive look-ahead.
    /// Gameplay-specific vehicle/air classes
    /// belong to the future camera/director policy owner, not this controller.
    inline constexpr engine::StreamingObserverVelocityClass WorldSessionObserverVelocityClass = engine::StreamingObserverVelocityClass::OnFoot;
    inline constexpr vanguard::f32 WorldSessionObserverPredictionSeconds = 1.0f;
    /// A completed-pose displacement implying more than this speed is a camera cut
    /// or teleport, not continuous motion, and publishes zero velocity. Normal
    /// derived velocity is bounded by the same limit.
    inline constexpr vanguard::f64 WorldSessionObserverMaximumSpeed = 100.0;

    class ManagedWorldSessionService final : public engine::WorldSessionService
    {
    public:
        [[nodiscard]] bool Begin(const engine::WorldSessionStartRequest& request, engine::WorldSessionFailure* const failure) noexcept override
        {
            ClearOutput(failure);
            if (m_stopRequested || m_status != engine::WorldSessionStatus::Idle)
            {
                // Rejected commands must not corrupt an already running session.
                if (failure != nullptr)
                    *failure = {engine::WorldSessionFailureCode::InvalidState, resources::Failure::None,
                                "world session cannot begin in its current state"};
                return false;
            }
            // A completed stop already released observer authority; this keeps the
            // idle-to-loading transition self-healing if a caller leaked one.
            ReleaseCameraObserver();
            if (!request.world.IsValid() || request.world.ExpectedType() != vanguard::world::WorldResourceType)
                return Reject(failure, engine::WorldSessionFailureCode::InvalidRequest, "world session requires an explicit typed world resource");
            if (request.inputMode != engine::WorldSessionInputMode::None && request.inputMode != engine::WorldSessionInputMode::Mapping)
                return Reject(failure, engine::WorldSessionFailureCode::InvalidRequest, "world session requires an explicit input policy");
            if (request.cameraSource != engine::WorldSessionCameraSource::None && request.cameraSource != engine::WorldSessionCameraSource::WorldDefault)
                return Reject(failure, engine::WorldSessionFailureCode::InvalidRequest, "world session requires an explicit camera-source policy");
            if (request.inputMode == engine::WorldSessionInputMode::Mapping &&
                (!request.input.IsValid() || request.input.ExpectedType() != vanguard::game_input::MappingResourceType))
                return Reject(failure, engine::WorldSessionFailureCode::InvalidRequest, "world session requires a typed input mapping");
            if (request.inputMode == engine::WorldSessionInputMode::None && request.input.IsValid())
                return Reject(failure, engine::WorldSessionFailureCode::InvalidRequest, "no-input session cannot specify an input resource");
            if (m_worldGeneration == ~vanguard::u64{0} || m_selectionGeneration == ~vanguard::u64{0})
                return Reject(failure, engine::WorldSessionFailureCode::InvalidState, "world session camera generation space is exhausted");

            // The prior session is fully detached before replacing its map. This
            // also prevents edit-only worlds from inheriting gameplay contexts.
            const bool inputCleared = m_gameInput->Clear();
            if (!inputCleared)
                return Reject(failure, engine::WorldSessionFailureCode::InputClearFailure, "world session input clearing failed");
            m_terminalFailure = {};
            m_lastFailure = {};
            m_ownsInput = true;
            m_inputInstalled = request.inputMode == engine::WorldSessionInputMode::None;
            if (request.inputMode == engine::WorldSessionInputMode::Mapping)
            {
                m_inputRequest = m_streaming->GetStreamer().Request(request.input, resources::LoadPriority::Critical);
                if (!m_inputRequest.IsValid())
                    return Fail(failure, engine::WorldSessionFailureCode::InputRequestFailure, "world session default input request was rejected");
            }

            const bool worldRequested = m_world->BeginWorld(request.world);
            if (!worldRequested)
            {
                return Fail(failure, engine::WorldSessionFailureCode::WorldRequestFailure,
                            "world session resource request was rejected");
            }

            ++m_worldGeneration;
            ++m_selectionGeneration;
            m_cameraSource = request.cameraSource;
            m_bindingSource = request.cameraSource == engine::WorldSessionCameraSource::WorldDefault
                                  ? engine::WorldSessionCameraBindingSource::WorldDefault
                                  : engine::WorldSessionCameraBindingSource::None;
            m_worldCameraLocator = {};
            m_liveCamera = {};
            m_livePose = {};
            m_status = engine::WorldSessionStatus::LoadingWorld;
            return true;
        }

        [[nodiscard]] engine::WorldSessionStatus Poll(engine::WorldSessionFailure* const failure) noexcept override
        {
            ClearOutput(failure);
            if (m_status == engine::WorldSessionStatus::Failed)
            {
                // A terminal failure recorded outside Poll (for example a required
                // camera observer failure raised by the BeginFrame participant) must
                // remain visible to the session owner on every subsequent poll.
                if (failure != nullptr)
                    *failure = m_lastFailure;
                return m_status;
            }
            if (m_stopRequested)
                return PollStop(failure);
            if (m_status == engine::WorldSessionStatus::Running)
            {
                if (m_gameWorld->GetStatus() != engine::GameWorldStatus::Failed)
                    return m_status;
                const engine::WorldCameraResolution& camera = m_gameWorld->GetWorldCameraResolution();
                if (m_bindingSource == engine::WorldSessionCameraBindingSource::WorldDefault &&
                    camera.status == engine::WorldCameraResolutionStatus::Failed)
                {
                    static_cast<void>(Fail(failure, engine::WorldSessionFailureCode::RequiredCameraFailure,
                                           RequiredCameraFailureMessage(camera.failure)));
                    return m_status;
                }
                static_cast<void>(Fail(failure, engine::WorldSessionFailureCode::GameWorldStartFailure,
                                       "world session game world entered a failed state"));
                return m_status;
            }
            if (m_status != engine::WorldSessionStatus::LoadingWorld)
                return m_status;

            // Input loading is independently asynchronous. Observe a terminal input
            // failure before polling the still-Loading world owner so a failure that
            // already happened cannot be overwritten by a later world fault.
            if (!m_inputInstalled && m_inputRequest.IsValid() && m_inputRequest.HasFinished() && !m_inputRequest.HasLoaded())
            {
                const resources::Failure inputFailure = m_inputRequest.GetError();
                m_inputRequest.Reset();
                static_cast<void>(Fail(failure, engine::WorldSessionFailureCode::InputLoadFailure,
                                       "world session default input loading failed", inputFailure));
                return m_status;
            }

            const engine::WorldResourceStatus worldStatus = m_world->PollWorld();
            if (worldStatus == engine::WorldResourceStatus::Loading)
                return m_status;
            if (worldStatus == engine::WorldResourceStatus::Failed)
            {
                static_cast<void>(Fail(failure, engine::WorldSessionFailureCode::WorldLoadFailure, "world session resource loading failed",
                                       m_world->GetLastFailure()));
                return m_status;
            }
            if (worldStatus != engine::WorldResourceStatus::Ready)
            {
                static_cast<void>(Fail(failure, engine::WorldSessionFailureCode::WorldLoadFailure, "world session resource entered an invalid state"));
                return m_status;
            }
            if (!m_inputInstalled)
            {
                if (!m_inputRequest.HasFinished())
                    return m_status;
                if (!m_inputRequest.HasLoaded())
                {
                    const resources::Failure inputFailure = m_inputRequest.GetError();
                    m_inputRequest.Reset();
                    static_cast<void>(Fail(failure, engine::WorldSessionFailureCode::InputLoadFailure, "world session default input loading failed",
                                           inputFailure));
                    return m_status;
                }
                resources::ResourceHandle input = m_inputRequest.Acquire();
                m_inputRequest.Reset();
                if (!input.IsValid() || input.Get()->GetType() != vanguard::game_input::MappingResourceType)
                {
                    static_cast<void>(Fail(failure, engine::WorldSessionFailureCode::InputLoadFailure,
                                           "world session input resource has an invalid type",
                                           resources::Failure::InternalError));
                    return m_status;
                }
                const auto* const mapping = static_cast<const vanguard::game_input::MappingResource*>(input.Get());
                const vanguard::game_input::MappingResult installed = m_gameInput->Install(mapping->GetFile());
                if (installed != vanguard::game_input::MappingResult::Success)
                {
                    static_cast<void>(Fail(failure, engine::WorldSessionFailureCode::InputInstallFailure, "world session default input installation failed"));
                    return m_status;
                }
                m_inputInstalled = true;
            }
            m_worldCameraLocator = {};
            if (m_cameraSource == engine::WorldSessionCameraSource::WorldDefault)
            {
                const vanguard::world::WorldResource* const worldResource = m_world->GetResource();
                if (worldResource == nullptr || !worldResource->GetFile().HasStartupCamera())
                {
                    static_cast<void>(Fail(failure, engine::WorldSessionFailureCode::RequiredCameraFailure,
                                           "world session requires an authored default startup camera"));
                    return m_status;
                }
                m_worldCameraLocator = worldResource->GetFile().GetStartupCamera();
            }
            const bool worldStarted = m_gameWorld->BeginWorld(m_worldCameraLocator);
            if (!worldStarted)
            {
                static_cast<void>(Fail(failure, engine::WorldSessionFailureCode::GameWorldStartFailure, "world session game-world initialization failed"));
                return m_status;
            }

            m_status = engine::WorldSessionStatus::Running;
            m_lastFailure = {};
            return m_status;
        }

        [[nodiscard]] bool RequestStop(engine::WorldSessionFailure* const failure) noexcept override
        {
            ClearOutput(failure);
            if (m_status == engine::WorldSessionStatus::Failed)
            {
                if (failure != nullptr)
                    *failure = m_lastFailure;
                return true;
            }
            if (m_stopRequested)
            {
                WriteTerminalFailureOutput(failure);
                return true;
            }
            if (m_status == engine::WorldSessionStatus::Idle)
                return true;

            CaptureGameWorldFailureIfNeeded();
            BeginStopTransaction();
            WriteTerminalFailureOutput(failure);
            return true;
        }

        [[nodiscard]] bool SelectWorldDefaultCamera(engine::WorldSessionCameraSelectionToken* const selection,
                                                    engine::WorldSessionFailure* const failure) noexcept override
        {
            ClearOutput(failure);
            ClearSelectionOutput(selection);
            if (!CanChangeCameraSelection(failure))
                return false;
            const vanguard::world::WorldResource* const worldResource = m_world->GetResource();
            if (worldResource == nullptr || !worldResource->GetFile().HasStartupCamera())
                return Reject(failure, engine::WorldSessionFailureCode::InvalidRequest,
                              "world session has no authored default camera to select");
            const vanguard::world::WorldCameraLocator locator = worldResource->GetFile().GetStartupCamera();
            ReleaseCameraObserver();
            if (!m_gameWorld->SetWorldCameraLocator(locator))
                return Reject(failure, engine::WorldSessionFailureCode::InvalidState,
                              "world session could not replace the world-camera resolution transaction");

            ++m_selectionGeneration;
            m_bindingSource = engine::WorldSessionCameraBindingSource::WorldDefault;
            m_worldCameraLocator = locator;
            m_liveCamera = {};
            m_livePose = {};
            WriteSelectionOutput(selection);
            return true;
        }

        [[nodiscard]] bool SelectLiveCamera(const vanguard::rendering::RenderCameraHandle camera, const engine::WorldCameraPose& pose,
                                            engine::WorldSessionCameraSelectionToken* const selection,
                                            engine::WorldSessionFailure* const failure) noexcept override
        {
            ClearOutput(failure);
            ClearSelectionOutput(selection);
            if (!CanChangeCameraSelection(failure))
                return false;
            if (!camera.IsValid() || !pose.IsValid())
                return Reject(failure, engine::WorldSessionFailureCode::InvalidRequest,
                              "live camera selection requires a valid camera and precise pose");
            const vanguard::entities::RenderingRuntime* const worldRendering = m_gameWorld->GetRendering();
            if (worldRendering == nullptr || !worldRendering->GetScene().IsValid() || camera.scene != worldRendering->GetScene())
                return Reject(failure, engine::WorldSessionFailureCode::InvalidRequest,
                              "live camera selection must belong to the current world scene");
            if (!m_rendering->GetCameras().IsAlive(camera))
                return Reject(failure, engine::WorldSessionFailureCode::InvalidRequest,
                              "live camera selection references a stale renderer camera");
            ReleaseCameraObserver();
            if (!m_gameWorld->SetWorldCameraLocator({}))
                return Reject(failure, engine::WorldSessionFailureCode::InvalidState,
                              "world session could not revoke the prior world-camera resolution transaction");

            ++m_selectionGeneration;
            m_bindingSource = engine::WorldSessionCameraBindingSource::Live;
            m_worldCameraLocator = {};
            m_liveCamera = camera;
            m_livePose = pose;
            WriteSelectionOutput(selection);
            return true;
        }

        [[nodiscard]] bool UpdateLiveCameraPose(const engine::WorldSessionCameraSelectionToken selection, const engine::WorldCameraPose& pose,
                                                engine::WorldSessionFailure* const failure) noexcept override
        {
            ClearOutput(failure);
            if (m_status != engine::WorldSessionStatus::Running || m_stopRequested ||
                m_bindingSource != engine::WorldSessionCameraBindingSource::Live || !SelectionMatches(selection))
                return Reject(failure, engine::WorldSessionFailureCode::InvalidState,
                              "live camera pose update does not own the current selection");
            if (!pose.IsValid())
                return Reject(failure, engine::WorldSessionFailureCode::InvalidRequest, "live camera pose is invalid");
            const vanguard::entities::RenderingRuntime* const worldRendering = m_gameWorld->GetRendering();
            if (worldRendering == nullptr || !worldRendering->GetScene().IsValid() || m_liveCamera.scene != worldRendering->GetScene() ||
                !m_rendering->GetCameras().IsAlive(m_liveCamera))
                return Reject(failure, engine::WorldSessionFailureCode::InvalidState,
                              "live camera pose update references a stale camera selection");
            m_livePose = pose;
            return true;
        }

        [[nodiscard]] bool ClearCameraSelection(const engine::WorldSessionCameraSelectionToken selection,
                                                engine::WorldSessionFailure* const failure) noexcept override
        {
            ClearOutput(failure);
            if (m_status != engine::WorldSessionStatus::Running || m_stopRequested || !SelectionMatches(selection))
                return Reject(failure, engine::WorldSessionFailureCode::InvalidState,
                              "camera selection revocation does not own the current selection");
            if (m_selectionGeneration == ~vanguard::u64{0})
                return Reject(failure, engine::WorldSessionFailureCode::InvalidState, "camera selection generation space is exhausted");
            ReleaseCameraObserver();
            if (!m_gameWorld->SetWorldCameraLocator({}))
                return Reject(failure, engine::WorldSessionFailureCode::InvalidState,
                              "world session could not revoke the world-camera resolution transaction");
            ++m_selectionGeneration;
            m_bindingSource = engine::WorldSessionCameraBindingSource::None;
            m_worldCameraLocator = {};
            m_liveCamera = {};
            m_livePose = {};
            return true;
        }

        [[nodiscard]] engine::WorldSessionStatus GetStatus() const noexcept override
        {
            return m_status;
        }
        [[nodiscard]] const engine::WorldSessionFailure& GetLastFailure() const noexcept override
        {
            return m_lastFailure;
        }
        [[nodiscard]] engine::WorldSessionCameraBinding GetCameraBinding() const noexcept override
        {
            engine::WorldSessionCameraBinding binding;
            binding.source = m_bindingSource;
            binding.worldGeneration = m_worldGeneration;
            binding.selectionGeneration = m_selectionGeneration;
            if (m_bindingSource == engine::WorldSessionCameraBindingSource::None)
                return binding;

            if (m_status == engine::WorldSessionStatus::LoadingWorld)
            {
                binding.status = engine::WorldSessionCameraBindingStatus::Pending;
                return binding;
            }
            if (m_status != engine::WorldSessionStatus::Running)
            {
                binding.status = engine::WorldSessionCameraBindingStatus::Failed;
                binding.failure = m_bindingSource == engine::WorldSessionCameraBindingSource::WorldDefault
                                      ? engine::WorldSessionCameraBindingFailure::RequiredWorldCameraUnavailable
                                      : engine::WorldSessionCameraBindingFailure::LiveCameraUnavailable;
                return binding;
            }

            if (m_bindingSource == engine::WorldSessionCameraBindingSource::WorldDefault)
            {
                const engine::WorldCameraResolution& resolved = m_gameWorld->GetWorldCameraResolution();
                binding.worldFailure = resolved.failure;
                if (resolved.status == engine::WorldCameraResolutionStatus::Failed)
                {
                    binding.status = engine::WorldSessionCameraBindingStatus::Failed;
                    binding.failure = engine::WorldSessionCameraBindingFailure::RequiredWorldCameraUnavailable;
                    return binding;
                }
                if (resolved.status != engine::WorldCameraResolutionStatus::Ready)
                {
                    binding.status = engine::WorldSessionCameraBindingStatus::Pending;
                    return binding;
                }
                engine::WorldCameraPose pose;
                if (!m_gameWorld->GetWorldCameraPose(pose))
                {
                    // A detach/replacement may precede the post-tick resolver update.
                    // Do not expose the previously resolved handle at this use boundary.
                    binding.status = engine::WorldSessionCameraBindingStatus::Pending;
                    return binding;
                }
                binding.status = engine::WorldSessionCameraBindingStatus::Ready;
                binding.camera = resolved.camera;
                binding.pose = pose;
                binding.hasPrecisePose = true;
                return binding;
            }

            const vanguard::entities::RenderingRuntime* const worldRendering = m_gameWorld->GetRendering();
            if (worldRendering == nullptr || !worldRendering->GetScene().IsValid() || m_liveCamera.scene != worldRendering->GetScene())
            {
                binding.status = engine::WorldSessionCameraBindingStatus::Failed;
                binding.failure = engine::WorldSessionCameraBindingFailure::WrongScene;
                return binding;
            }
            if (!m_liveCamera.IsValid() || !m_rendering->GetCameras().IsAlive(m_liveCamera))
            {
                binding.status = engine::WorldSessionCameraBindingStatus::Failed;
                binding.failure = engine::WorldSessionCameraBindingFailure::LiveCameraUnavailable;
                return binding;
            }
            binding.status = engine::WorldSessionCameraBindingStatus::Ready;
            binding.camera = m_liveCamera;
            binding.pose = m_livePose;
            binding.hasPrecisePose = true;
            return binding;
        }

    protected:
        app::LifecycleStatus OnInitialize(app::ServiceContext& context) noexcept override
        {
            m_streaming = engine::FindResourceStreamingService(context);
            m_world = engine::FindWorldService(context);
            m_gameWorld = engine::FindGameWorldService(context);
            m_gameInput = engine::FindGameInputService(context);
            m_rendering = engine::FindRenderingService(context);
            m_streamingObservers = engine::FindStreamingObserverService(context);
            m_framePipeline = engine::FindFramePipelineService(context);
            if (m_streaming == nullptr || m_world == nullptr || m_gameWorld == nullptr || m_gameInput == nullptr || m_rendering == nullptr ||
                m_streamingObservers == nullptr || m_framePipeline == nullptr)
                return app::LifecycleStatus::Failure("World Session dependencies are not running");

            engine::FrameParticipantDescriptor observerParticipant;
            observerParticipant.id = engine::WorldSessionCameraObserverParticipantId;
            observerParticipant.name = "worldSession.cameraObserver";
            observerParticipant.phase = engine::FramePhase::BeginFrame;
            observerParticipant.profiles = app::ApplicationProfile::Runtime | app::ApplicationProfile::Editor;
            observerParticipant.affinity = app::ThreadAffinity::MainThread;
            observerParticipant.execute = &ExecuteCameraObserverFrame;
            observerParticipant.userData = this;
            engine::FrameFailure frameFailure;
            if (!m_framePipeline->RegisterParticipant(observerParticipant, &frameFailure))
                return app::LifecycleStatus::Failure(frameFailure.message != nullptr ? frameFailure.message
                                                                                    : "World Session camera observer frame registration failed");
            return app::LifecycleStatus::Success();
        }

        app::LifecycleStatus OnQuiesce(app::ServiceContext&) noexcept override
        {
            const bool terminalState = m_status == engine::WorldSessionStatus::Idle || m_status == engine::WorldSessionStatus::Failed;
            return terminalState && IsSessionOwnershipEmpty()
                       ? app::LifecycleStatus::Success()
                       : app::LifecycleStatus::Failure("World Session must release all session ownership before engine shutdown");
        }

        app::LifecycleStatus OnShutdown(app::ServiceContext&) noexcept override
        {
            const bool terminalState = m_status == engine::WorldSessionStatus::Idle || m_status == engine::WorldSessionStatus::Failed;
            if (!terminalState || !IsSessionOwnershipEmpty())
                return app::LifecycleStatus::Failure("World Session state remains live during shutdown");
            m_streaming = nullptr;
            m_world = nullptr;
            m_gameWorld = nullptr;
            m_gameInput = nullptr;
            m_rendering = nullptr;
            m_streamingObservers = nullptr;
            m_framePipeline = nullptr;
            m_lastFailure = {};
            m_terminalFailure = {};
            return app::LifecycleStatus::Success();
        }

    private:
        [[nodiscard]] static engine::FrameParticipantStatus ExecuteCameraObserverFrame(const engine::FrameContext& context, void* const userData) noexcept
        {
            auto* const service = static_cast<ManagedWorldSessionService*>(userData);
            if (service == nullptr)
                return engine::FrameParticipantStatus::Failure("World Session camera observer lost its service");
            return service->UpdateCameraObserver(context);
        }

        /// Owns the selected camera's streaming authority for one frame. It runs in
        /// BeginFrame before the existing Streaming Observer PreSimulation snapshot,
        /// so frame N streaming intentionally consumes the precise pose completed at
        /// the end of frame N-1. At most one observer is registered and updated.
        [[nodiscard]] engine::FrameParticipantStatus UpdateCameraObserver(const engine::FrameContext& context) noexcept
        {
            if (m_streamingObservers == nullptr || m_status != engine::WorldSessionStatus::Running || m_stopRequested)
            {
                ReleaseCameraObserver();
                return engine::FrameParticipantStatus::Success();
            }

            const engine::WorldSessionCameraBinding binding = GetCameraBinding();
            if (binding.status != engine::WorldSessionCameraBindingStatus::Ready || !binding.hasPrecisePose || !binding.camera.IsValid())
            {
                // None, Pending and Failed bindings never hold observer authority.
                ReleaseCameraObserver();
                return engine::FrameParticipantStatus::Success();
            }

            if (!EnsureCameraObserver())
                return HandleCameraObserverFailure("world session camera streaming observer registration failed");

            const vanguard::f64 position[3] = {binding.pose.position.x.AsDouble(), binding.pose.position.y.AsDouble(),
                                               binding.pose.position.z.AsDouble()};
            vanguard::f64 velocity[3]{};
            // The binding pose was completed by the PostSimulation of the previous
            // frame, so its timestamp is the accumulated simulation time before this
            // frame's delta. Using the current context time would divide the previous
            // pose displacement by the next frame's delta.
            const vanguard::f64 sampleTime = context.simulationTimeSeconds - static_cast<vanguard::f64>(context.simulationDeltaSeconds);
            const engine::WorldSessionCameraSelectionToken selection = binding.GetSelectionToken();
            const bool sameSample = m_observerHasSample && selection.IsValid() && selection == m_observerSelection &&
                                    binding.camera == m_observerCamera;
            const vanguard::f64 elapsed = sampleTime - m_observerPreviousTime;
            const bool validDelta = sameSample && std::isfinite(elapsed) && elapsed > 0.0;
            if (validDelta)
            {
                vanguard::f64 delta[3]{};
                vanguard::f64 distanceSquared = 0.0;
                bool finiteDelta = true;
                for (vanguard::u32 axis = 0; axis < 3; ++axis)
                {
                    delta[axis] = position[axis] - m_observerPreviousPosition[axis];
                    finiteDelta = finiteDelta && std::isfinite(delta[axis]);
                    distanceSquared += delta[axis] * delta[axis];
                }
                const vanguard::f64 speed = std::sqrt(distanceSquared) / elapsed;
                // A displacement implying an impossible continuous speed is a camera
                // cut/teleport and publishes zero velocity rather than a bogus vector.
                if (finiteDelta && std::isfinite(speed) && speed <= WorldSessionObserverMaximumSpeed)
                {
                    for (vanguard::u32 axis = 0; axis < 3; ++axis)
                        velocity[axis] = delta[axis] / elapsed;
                }
            }

            engine::StreamingObserverUpdate update;
            for (vanguard::u32 axis = 0; axis < 3; ++axis)
            {
                update.position[axis] = position[axis];
                update.velocity[axis] = velocity[axis];
            }
            update.positionValid = true;
            if (!m_streamingObservers->UpdateObserver(m_cameraObserver, update))
            {
                ReleaseCameraObserver();
                return HandleCameraObserverFailure("world session camera streaming observer update was rejected");
            }

            if (std::isfinite(sampleTime))
            {
                m_observerHasSample = true;
                m_observerCamera = binding.camera;
                m_observerSelection = selection;
                m_observerPreviousTime = sampleTime;
                for (vanguard::u32 axis = 0; axis < 3; ++axis)
                    m_observerPreviousPosition[axis] = position[axis];
            }
            else
            {
                ResetObserverSample();
            }
            if (m_lastFailure.code == engine::WorldSessionFailureCode::CameraObserverFailure)
                m_lastFailure = {};
            return engine::FrameParticipantStatus::Success();
        }

        /// Registers and promotes the single session observer. Registration applies
        /// `descriptor.enabled` directly, so no separate enable call is needed. A
        /// partial setup failure unregisters the observer it created.
        [[nodiscard]] bool EnsureCameraObserver() noexcept
        {
            if (m_cameraObserver.IsValid())
                return true;
            if (m_streamingObservers == nullptr)
                return false;

            engine::StreamingObserverDescriptor descriptor;
            descriptor.name = WorldSessionObserverName;
            descriptor.velocityClass = WorldSessionObserverVelocityClass;
            descriptor.predictionSeconds = WorldSessionObserverPredictionSeconds;
            descriptor.enabled = true;
            engine::StreamingObserverHandle observer;
            if (!m_streamingObservers->RegisterObserver(descriptor, observer))
                return false;
            m_cameraObserver = observer;
            if (!m_streamingObservers->SetPrimaryObserver(observer))
            {
                ReleaseCameraObserver();
                return false;
            }
            ResetObserverSample();
            return true;
        }

        /// Teardown order: disable, unregister, then drop the handle and velocity
        /// history. Unregister only clears primary status when this exact handle is
        /// still primary, so a later primary owner is never disturbed. Idempotent and
        /// safe after partial failure.
        void ReleaseCameraObserver() noexcept
        {
            if (m_cameraObserver.IsValid() && m_streamingObservers != nullptr)
            {
                static_cast<void>(m_streamingObservers->SetObserverEnabled(m_cameraObserver, false));
                static_cast<void>(m_streamingObservers->UnregisterObserver(m_cameraObserver));
            }
            m_cameraObserver = {};
            ResetObserverSample();
        }

        void ResetObserverSample() noexcept
        {
            m_observerHasSample = false;
            m_observerCamera = {};
            m_observerSelection = {};
            m_observerPreviousTime = 0.0;
            for (vanguard::u32 axis = 0; axis < 3; ++axis)
                m_observerPreviousPosition[axis] = 0.0;
        }

        /// A required WorldDefault observer failure is a session failure and fails
        /// the owning frame. An optional Live observer failure stays binding-local:
        /// the world session remains valid and the reason stays visible as the last
        /// failure for the owning controller.
        [[nodiscard]] engine::FrameParticipantStatus HandleCameraObserverFailure(const char* const message) noexcept
        {
            if (m_bindingSource == engine::WorldSessionCameraBindingSource::WorldDefault)
            {
                static_cast<void>(Fail(nullptr, engine::WorldSessionFailureCode::CameraObserverFailure, message));
                return engine::FrameParticipantStatus::Failure(message);
            }
            m_lastFailure = {engine::WorldSessionFailureCode::CameraObserverFailure, resources::Failure::None, message};
            return engine::FrameParticipantStatus::Success();
        }

        [[nodiscard]] engine::WorldSessionStatus PollStop(engine::WorldSessionFailure* const failure) noexcept
        {
            CancelInputRequest();
            engine::GameWorldStopFailure gameWorldFailure;
            const engine::GameWorldStopResult gameWorldResult = m_gameWorld->StopWorld(&gameWorldFailure);
            if (gameWorldFailure.code != engine::GameWorldStopFailureCode::None)
                LatchTerminalFailure(engine::WorldSessionFailureCode::GameWorldStopFailure,
                                     gameWorldFailure.message != nullptr ? gameWorldFailure.message : "world session game-world drain failed",
                                     resources::Failure::None, &gameWorldFailure);
            if (gameWorldResult == engine::GameWorldStopResult::Pending)
            {
                WriteTerminalFailureOutput(failure);
                return m_status;
            }
            if (gameWorldResult == engine::GameWorldStopResult::Failure)
            {
                if (gameWorldFailure.code == engine::GameWorldStopFailureCode::None)
                    LatchTerminalFailure(engine::WorldSessionFailureCode::GameWorldStopFailure, "world session game-world drain failed");
                if (HasGameWorldOwnership())
                {
                    WriteTerminalFailureOutput(failure);
                    return m_status;
                }
            }

            const engine::WorldResourceStatus worldStatusBeforeRelease = m_world->GetStatus();
            const bool worldReleased = m_world->ReleaseWorld();
            if (!worldReleased)
            {
                const resources::Failure worldFailure = m_world->GetLastFailure();
                if (worldStatusBeforeRelease == engine::WorldResourceStatus::Loading &&
                    m_world->GetStatus() == engine::WorldResourceStatus::Idle &&
                    worldFailure != resources::Failure::None)
                    LatchTerminalFailure(engine::WorldSessionFailureCode::WorldLoadFailure,
                                         "world session resource loading failed during cancellation", worldFailure);
                else
                    LatchTerminalFailure(engine::WorldSessionFailureCode::WorldReleaseFailure,
                                         "world session world-resource release failed", worldFailure);
            }

            if (m_ownsInput)
            {
                const bool inputCleared = m_gameInput->Clear();
                if (!inputCleared)
                    LatchTerminalFailure(engine::WorldSessionFailureCode::InputClearFailure, "world session input clearing failed");
                // GameInput is host-owned. A terminal clear failure blocks replacement,
                // but the session must relinquish its logical claim so host teardown can
                // diagnose and own the remaining input state.
                m_ownsInput = false;
            }

            return FinalizeStop(failure);
        }

        [[nodiscard]] engine::WorldSessionStatus FinalizeStop(engine::WorldSessionFailure* const failure) noexcept
        {
            m_cameraSource = engine::WorldSessionCameraSource::Unspecified;
            m_worldCameraLocator = {};
            m_bindingSource = engine::WorldSessionCameraBindingSource::None;
            m_liveCamera = {};
            m_livePose = {};
            ReleaseCameraObserver();
            m_stopRequested = false;
            m_inputInstalled = false;
            if (HasTerminalFailure())
            {
                m_status = engine::WorldSessionStatus::Failed;
                m_lastFailure = m_terminalFailure;
                WriteTerminalFailureOutput(failure);
                return m_status;
            }

            m_status = engine::WorldSessionStatus::Idle;
            m_lastFailure = {};
            m_terminalFailure = {};
            return m_status;
        }

        [[nodiscard]] bool CanChangeCameraSelection(engine::WorldSessionFailure* const failure) const noexcept
        {
            if (m_status != engine::WorldSessionStatus::Running || m_stopRequested || m_gameWorld == nullptr ||
                m_gameWorld->GetStatus() != engine::GameWorldStatus::Running)
                return Reject(failure, engine::WorldSessionFailureCode::InvalidState,
                              "camera selection can change only while the world session is running");
            if (m_selectionGeneration == ~vanguard::u64{0})
                return Reject(failure, engine::WorldSessionFailureCode::InvalidState, "camera selection generation space is exhausted");
            return true;
        }

        [[nodiscard]] bool SelectionMatches(const engine::WorldSessionCameraSelectionToken selection) const noexcept
        {
            return selection.IsValid() && selection.worldGeneration == m_worldGeneration &&
                   selection.selectionGeneration == m_selectionGeneration;
        }

        void RevokeCameraBinding() noexcept
        {
            ReleaseCameraObserver();
            if (m_status != engine::WorldSessionStatus::Idle && m_selectionGeneration != ~vanguard::u64{0})
                ++m_selectionGeneration;
            m_bindingSource = engine::WorldSessionCameraBindingSource::None;
            m_worldCameraLocator = {};
            m_liveCamera = {};
            m_livePose = {};
        }

        void ClearSelectionOutput(engine::WorldSessionCameraSelectionToken* const selection) const noexcept
        {
            if (selection != nullptr)
                *selection = {};
        }

        void WriteSelectionOutput(engine::WorldSessionCameraSelectionToken* const selection) const noexcept
        {
            if (selection != nullptr)
                *selection = {m_worldGeneration, m_selectionGeneration};
        }

        void ClearOutput(engine::WorldSessionFailure* const failure) const noexcept
        {
            if (failure != nullptr)
                *failure = {};
        }

        [[nodiscard]] bool Reject(engine::WorldSessionFailure* const output, const engine::WorldSessionFailureCode code,
                                  const char* const message) const noexcept
        {
            if (output != nullptr)
                *output = {code, resources::Failure::None, message};
            return false;
        }

        [[nodiscard]] bool HasTerminalFailure() const noexcept
        {
            return m_terminalFailure.code != engine::WorldSessionFailureCode::None;
        }

        void WriteTerminalFailureOutput(engine::WorldSessionFailure* const output) const noexcept
        {
            if (output != nullptr && HasTerminalFailure())
                *output = m_terminalFailure;
        }

        void LatchTerminalFailure(const engine::WorldSessionFailureCode code, const char* const message,
                                  const resources::Failure resourceFailure = resources::Failure::None,
                                  const engine::GameWorldStopFailure* const gameWorldFailure = nullptr) noexcept
        {
            if (!HasTerminalFailure())
            {
                m_terminalFailure = {code, resourceFailure, message};
                if (gameWorldFailure != nullptr)
                    m_terminalFailure.gameWorldStopFailure = *gameWorldFailure;
            }
            m_lastFailure = m_terminalFailure;
        }

        void BeginStopTransaction() noexcept
        {
            if (!m_stopRequested)
            {
                RevokeCameraBinding();
                m_stopRequested = true;
            }
            m_status = engine::WorldSessionStatus::Stopping;
        }

        void CaptureGameWorldFailureIfNeeded() noexcept
        {
            if (HasTerminalFailure() || m_gameWorld == nullptr || m_gameWorld->GetStatus() != engine::GameWorldStatus::Failed)
                return;
            const engine::WorldCameraResolution& camera = m_gameWorld->GetWorldCameraResolution();
            if (m_bindingSource == engine::WorldSessionCameraBindingSource::WorldDefault &&
                camera.status == engine::WorldCameraResolutionStatus::Failed)
            {
                LatchTerminalFailure(engine::WorldSessionFailureCode::RequiredCameraFailure, RequiredCameraFailureMessage(camera.failure));
                return;
            }
            LatchTerminalFailure(engine::WorldSessionFailureCode::GameWorldStartFailure, "world session game world entered a failed state");
        }

        void CancelInputRequest() noexcept
        {
            if (!m_inputRequest.IsValid())
                return;
            resources::Failure terminalFailure = resources::Failure::None;
            const resources::PipelineCancelStatus cancelStatus = m_inputRequest.CancelWithStatus(&terminalFailure);
            if (cancelStatus == resources::PipelineCancelStatus::Failed)
                LatchTerminalFailure(engine::WorldSessionFailureCode::InputLoadFailure,
                                     "world session default input loading failed", terminalFailure);
            else if (cancelStatus == resources::PipelineCancelStatus::Invalid)
                LatchTerminalFailure(engine::WorldSessionFailureCode::InputCancelFailure,
                                     "world session input request cancellation failed");
            // The atomic cancel disposition owns the request-interest cutover.
            // Reset only drops the detached request wrapper.
            m_inputRequest.Reset();
        }

        [[nodiscard]] bool HasGameWorldOwnership() const noexcept
        {
            return m_gameWorld != nullptr &&
                   (m_gameWorld->GetWorld() != nullptr || m_gameWorld->GetCellStreaming() != nullptr || m_gameWorld->GetComponents() != nullptr ||
                    m_gameWorld->GetTransforms() != nullptr || m_gameWorld->GetRendering() != nullptr);
        }

        [[nodiscard]] bool HasWorldOwnership() const noexcept
        {
            return m_world != nullptr &&
                   (m_world->GetStatus() != engine::WorldResourceStatus::Idle || m_world->GetGrid() != nullptr || m_world->GetExecutor() != nullptr);
        }

        [[nodiscard]] bool IsSessionOwnershipEmpty() const noexcept
        {
            return !m_stopRequested && !m_ownsInput && !m_inputRequest.IsValid() &&
                   m_bindingSource == engine::WorldSessionCameraBindingSource::None && !m_liveCamera.IsValid() && !m_cameraObserver.IsValid() &&
                   !HasGameWorldOwnership() && !HasWorldOwnership();
        }

        [[nodiscard]] bool Fail(engine::WorldSessionFailure* const output, const engine::WorldSessionFailureCode code, const char* const message,
                                const resources::Failure resourceFailure = resources::Failure::None) noexcept
        {
            LatchTerminalFailure(code, message, resourceFailure);
            BeginStopTransaction();
            WriteTerminalFailureOutput(output);
            return false;
        }

        [[nodiscard]] static const char* RequiredCameraFailureMessage(const engine::WorldCameraResolutionFailure failure) noexcept
        {
            switch (failure)
            {
            case engine::WorldCameraResolutionFailure::CellFailed:
                return "required world camera cell failed to materialize";
            case engine::WorldCameraResolutionFailure::CellUnavailable:
                return "required world camera cell became unavailable";
            case engine::WorldCameraResolutionFailure::MissingEntity:
                return "required world camera entity is absent from the active startup cell";
            case engine::WorldCameraResolutionFailure::MissingComponent:
                return "required world camera component is absent from the active entity";
            case engine::WorldCameraResolutionFailure::WrongComponentType:
                return "required world camera locator resolved to a non-camera component";
            case engine::WorldCameraResolutionFailure::CameraUnavailable:
                return "required world camera has no live renderer camera handle";
            default:
                return "required world camera resolution failed";
            }
        }

        engine::ResourceStreamingService* m_streaming = nullptr;
        engine::WorldService* m_world = nullptr;
        engine::GameWorldService* m_gameWorld = nullptr;
        engine::GameInputService* m_gameInput = nullptr;
        engine::RenderingService* m_rendering = nullptr;
        engine::StreamingObserverService* m_streamingObservers = nullptr;
        engine::FramePipelineService* m_framePipeline = nullptr;
        resources::PipelineRequest m_inputRequest;
        vanguard::world::WorldCameraLocator m_worldCameraLocator;
        engine::WorldCameraPose m_livePose;
        engine::WorldSessionFailure m_lastFailure;
        engine::WorldSessionFailure m_terminalFailure;
        engine::WorldSessionCameraSource m_cameraSource = engine::WorldSessionCameraSource::Unspecified;
        engine::WorldSessionCameraBindingSource m_bindingSource = engine::WorldSessionCameraBindingSource::None;
        vanguard::rendering::RenderCameraHandle m_liveCamera;
        engine::StreamingObserverHandle m_cameraObserver;
        engine::WorldSessionCameraSelectionToken m_observerSelection;
        vanguard::rendering::RenderCameraHandle m_observerCamera;
        vanguard::f64 m_observerPreviousPosition[3]{};
        vanguard::f64 m_observerPreviousTime = 0.0;
        vanguard::u64 m_worldGeneration = 0;
        vanguard::u64 m_selectionGeneration = 0;
        engine::WorldSessionStatus m_status = engine::WorldSessionStatus::Idle;
        bool m_observerHasSample = false;
        bool m_stopRequested = false;
        bool m_inputInstalled = false;
        bool m_ownsInput = false;
    };

    app::Service* CreateWorldSessionService(void*) noexcept
    {
        vanguard::memory::MemoryBlock block =
            vanguard::memory::Allocate(vanguard::memory::PoolId::World, sizeof(ManagedWorldSessionService), alignof(ManagedWorldSessionService));
        return block ? ::new (block.address) ManagedWorldSessionService() : nullptr;
    }

    void DestroyWorldSessionService(app::Service* const service, void*) noexcept
    {
        if (service == nullptr)
            return;
        static_cast<ManagedWorldSessionService*>(service)->~ManagedWorldSessionService();
        vanguard::memory::MemoryBlock block{service, sizeof(ManagedWorldSessionService), vanguard::memory::PoolId::World};
        vanguard::memory::Free(block);
    }
} // namespace

namespace vanguard::engine
{
    bool RegisterWorldSessionService(application::EngineHost& host, application::HostFailure* const failure) noexcept
    {
        constexpr application::ServiceDependency dependencies[]{{ResourceStreamingServiceId, application::DependencyKind::Required},
                                                                {WorldServiceId, application::DependencyKind::Required},
                                                                {GameWorldServiceId, application::DependencyKind::Required},
                                                                {GameInputServiceId, application::DependencyKind::Required},
                                                                {RenderingServiceId, application::DependencyKind::Required},
                                                                {StreamingObserverServiceId, application::DependencyKind::Required},
                                                                {FramePipelineServiceId, application::DependencyKind::Required}};
        constexpr application::CapabilityId providedCapabilities[]{WorldSessionCapabilityId};
        application::ServiceDescriptor descriptor;
        descriptor.id = WorldSessionServiceId;
        descriptor.name = "worldSession";
        descriptor.profiles = application::ApplicationProfile::Runtime | application::ApplicationProfile::Editor;
        descriptor.scope = application::ServiceScope::Engine;
        descriptor.affinity = application::ThreadAffinity::MainThread;
        descriptor.dependencies = {dependencies, 7};
        descriptor.provides = {providedCapabilities, 1};
        descriptor.create = CreateWorldSessionService;
        descriptor.destroy = DestroyWorldSessionService;
        return host.RegisterService(EngineModuleId, descriptor, failure);
    }

    WorldSessionService* FindWorldSessionService(application::EngineHost& host) noexcept
    {
        return static_cast<WorldSessionService*>(host.FindCapability(WorldSessionCapabilityId));
    }

    WorldSessionService* FindWorldSessionService(application::ServiceContext& context) noexcept
    {
        return static_cast<WorldSessionService*>(context.FindCapability(WorldSessionCapabilityId));
    }
} // namespace vanguard::engine
