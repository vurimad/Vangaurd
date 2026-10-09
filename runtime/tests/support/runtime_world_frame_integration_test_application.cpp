// TEST SUPPORT ONLY: bounded native validation of the production runtime world-frame/presentation path.

#include <runtime_world_frame_integration_test_application.hpp>

#include <vanguard/engine/frame_pipeline_service.hpp>
#include <vanguard/engine/game_world_service.hpp>
#include <vanguard/engine/rendering_service.hpp>
#include <vanguard/engine/resource_streaming_service.hpp>
#include <vanguard/engine/world_session_service.hpp>
#include <vanguard/memory/memory.hpp>
#include <vanguard/runtime/runtime_viewport_service.hpp>

#include <cstring>
#include <new>

namespace
{
    namespace app = vanguard::application;
    namespace engine = vanguard::engine;
    namespace entities = vanguard::entities;
    namespace rendering = vanguard::rendering;
    namespace runtime = vanguard::runtime;

    inline constexpr app::ServiceId RuntimeWorldFrameIntegrationTestServiceId = 0x7274776674657301ull;
    inline constexpr engine::FrameParticipantId RuntimeWorldFrameIntegrationTestParticipantId = 0x7274776674667201ull;
    inline constexpr vanguard::u32 MaximumValidationFrames = 600;

    struct RuntimeViewportObservation final
    {
        rendering::RenderViewportHandle presentationOutput;
        vanguard::u32 presentationOutputs = 0;
        vanguard::u32 runtimeWorldViewports = 0;
        bool presentationValid = false;
        bool presentationRendered = false;
        bool presentationPresented = false;
        bool runtimeWorldValid = false;
        bool runtimeWorldSubmitted = false;
        bool runtimeWorldPresents = false;
        bool runtimeWorldUsesPresentation = false;
        vanguard::u64 presentationRenderedFrames = 0;
        vanguard::u64 presentationPresentedFrames = 0;
        vanguard::u64 runtimeWorldSubmittedFrames = 0;
    };

    void ObserveRenderViewport(const rendering::RenderViewport& viewport, void* const userData) noexcept
    {
        auto& observation = *static_cast<RuntimeViewportObservation*>(userData);
        if (viewport.GetOutputKind() != rendering::RenderViewportOutputKind::Presentation)
            return;
        ++observation.presentationOutputs;
        if (observation.presentationOutputs != 1)
            return;
        observation.presentationOutput = viewport.GetHandle();
        observation.presentationValid = viewport.IsValid();
        observation.presentationRenderedFrames = viewport.GetRenderedFrameCount();
        observation.presentationPresentedFrames = viewport.GetPresentedFrameCount();
        observation.presentationRendered = observation.presentationRenderedFrames != 0;
        observation.presentationPresented = observation.presentationPresentedFrames != 0;
    }

    void ObserveEngineViewport(const rendering::EngineViewport& viewport, void* const userData) noexcept
    {
        auto& observation = *static_cast<RuntimeViewportObservation*>(userData);
        const char* const contextName = viewport.GetContextName();
        if (contextName == nullptr || std::strcmp(contextName, "runtime.world") != 0)
            return;
        ++observation.runtimeWorldViewports;
        if (observation.runtimeWorldViewports != 1)
            return;
        observation.runtimeWorldValid = viewport.IsValid();
        observation.runtimeWorldSubmittedFrames = viewport.GetSubmittedFrameCount();
        observation.runtimeWorldSubmitted = observation.runtimeWorldSubmittedFrames != 0;
        observation.runtimeWorldPresents = viewport.PresentsByDefault();
        observation.runtimeWorldUsesPresentation = viewport.GetOutput() == observation.presentationOutput;
    }

    class RuntimeWorldFrameIntegrationTestService final : public app::Service
    {
    public:
        explicit RuntimeWorldFrameIntegrationTestService(
            runtime::testing::RuntimeWorldFrameIntegrationTestServiceConfig& config) noexcept
            : m_config(&config)
        {
        }

    protected:
        app::LifecycleStatus OnInitialize(app::ServiceContext& context) noexcept override
        {
            m_rendering = engine::FindRenderingService(context);
            m_session = engine::FindWorldSessionService(context);
            m_gameWorld = engine::FindGameWorldService(context);
            m_sources = engine::FindResourceStreamingService(context);
            engine::FramePipelineService* const frames = engine::FindFramePipelineService(context);
            if (m_config == nullptr || m_config->states == nullptr || m_rendering == nullptr || m_session == nullptr ||
                m_gameWorld == nullptr || m_sources == nullptr || frames == nullptr)
                return app::LifecycleStatus::Failure("runtime world-frame integration-test dependencies are unavailable");

            constexpr engine::FrameParticipantId dependencies[]{runtime::RuntimeWorldFrameParticipantId};
            engine::FrameParticipantDescriptor descriptor;
            descriptor.id = RuntimeWorldFrameIntegrationTestParticipantId;
            descriptor.name = "runtimeWorldFrameIntegrationTest";
            descriptor.phase = engine::FramePhase::EndFrame;
            descriptor.profiles = app::ApplicationProfile::Runtime;
            descriptor.affinity = app::ThreadAffinity::MainThread;
            descriptor.after = {dependencies, 1};
            descriptor.execute = ExecuteFrame;
            descriptor.userData = this;
            engine::FrameFailure failure;
            if (!frames->RegisterParticipant(descriptor, &failure))
                return app::LifecycleStatus::Failure(failure.message != nullptr
                                                         ? failure.message
                                                         : "runtime world-frame integration-test frame registration failed");
            return app::LifecycleStatus::Success();
        }

        app::LifecycleStatus OnShutdown(app::ServiceContext&) noexcept override
        {
            m_rendering = nullptr;
            m_session = nullptr;
            m_gameWorld = nullptr;
            m_sources = nullptr;
            return app::LifecycleStatus::Success();
        }

    private:
        [[nodiscard]] engine::FrameParticipantStatus AwaitNextFrame() noexcept
        {
            ++m_frames;
            return m_frames < MaximumValidationFrames
                       ? engine::FrameParticipantStatus::Success()
                       : engine::FrameParticipantStatus::Failure("runtime world-frame integration test timed out before one presented world frame");
        }

        [[nodiscard]] engine::FrameParticipantStatus ValidateFrame() noexcept
        {
            if (m_config == nullptr || m_config->states == nullptr || m_rendering == nullptr || m_session == nullptr)
                return engine::FrameParticipantStatus::Failure("runtime world-frame integration-test service is unavailable");

            rendering::RenderCommandFailure commandFailure;
            if (!m_rendering->GetCommands().FlushPreviousFrameProcessing(&commandFailure))
                return engine::FrameParticipantStatus::Failure(commandFailure.message != nullptr
                                                                   ? commandFailure.message
                                                                   : "runtime world-frame integration-test render flush failed");
            if (m_rendering->GetCommands().ConsumeExecutionFailure(commandFailure))
                return engine::FrameParticipantStatus::Failure(commandFailure.message != nullptr
                                                                   ? commandFailure.message
                                                                   : "runtime world-frame integration-test render execution failed");

            const engine::WorldSessionStatus sessionStatus = m_session->GetStatus();
            if (sessionStatus == engine::WorldSessionStatus::Failed)
                return engine::FrameParticipantStatus::Failure("runtime world-frame integration-test world session failed");
            if (sessionStatus != engine::WorldSessionStatus::Running)
                return AwaitNextFrame();

            const engine::WorldSessionCameraBinding binding = m_session->GetCameraBinding();
            if (binding.status == engine::WorldSessionCameraBindingStatus::Failed)
                return engine::FrameParticipantStatus::Failure("runtime world-frame integration-test camera binding failed");
            if (binding.status != engine::WorldSessionCameraBindingStatus::Ready ||
                binding.source != engine::WorldSessionCameraBindingSource::WorldDefault || !binding.camera.IsValid() ||
                !m_rendering->GetCameras().IsAlive(binding.camera))
                return AwaitNextFrame();

            RuntimeViewportObservation observation;
            rendering::ViewportManager& viewports = m_rendering->GetViewports();
            viewports.VisitRenderViewports(ObserveRenderViewport, &observation);
            viewports.VisitEngineViewports(ObserveEngineViewport, &observation);
            const rendering::ViewportManagerStats stats = viewports.GetStats();
            if (observation.presentationOutputs != 1 || observation.runtimeWorldViewports != 1 ||
                stats.renderViewports != 1 || stats.engineViewports != 1)
                return engine::FrameParticipantStatus::Failure("runtime world-frame integration-test viewport topology is not the production single-output path");

            const bool presentedWorldFrame = observation.presentationValid && observation.presentationRendered && observation.presentationPresented &&
                                             observation.runtimeWorldValid && observation.runtimeWorldSubmitted && observation.runtimeWorldPresents &&
                                             observation.runtimeWorldUsesPresentation;
            if (!presentedWorldFrame)
                return AwaitNextFrame();

            if (m_config->replacementPath)
            {
                if (!m_replacementRequested)
                {
                    const entities::RenderingRuntime* const worldRendering = m_gameWorld->GetRendering();
                    if (binding.worldGeneration == 0 || worldRendering == nullptr || !worldRendering->GetScene().IsValid())
                        return engine::FrameParticipantStatus::Failure("runtime world-replacement integration test initial world identity is unavailable");
                    m_initialWorldGeneration = binding.worldGeneration;
                    m_initialScene = worldRendering->GetScene();
                    m_initialPresentationRenderedFrames = observation.presentationRenderedFrames;
                    m_initialPresentationPresentedFrames = observation.presentationPresentedFrames;
                    m_initialRuntimeSubmittedFrames = observation.runtimeWorldSubmittedFrames;
                    app::StateMachineFailure stateFailure;
                    if (!m_config->states->RequestTransition(runtime::testing::RuntimeWorldReplacementValidationStateId, &stateFailure))
                        return engine::FrameParticipantStatus::Failure(stateFailure.message != nullptr
                                                                           ? stateFailure.message
                                                                           : "runtime world-replacement integration test could not request replacement state");
                    m_replacementRequested = true;
                    return engine::FrameParticipantStatus::Success();
                }

                const entities::RenderingRuntime* const worldRendering = m_gameWorld->GetRendering();
                if (binding.worldGeneration == m_initialWorldGeneration || worldRendering == nullptr ||
                    !worldRendering->GetScene().IsValid() || worldRendering->GetScene() == m_initialScene ||
                    m_rendering->GetScenes().IsAlive(m_initialScene) ||
                    observation.presentationRenderedFrames <= m_initialPresentationRenderedFrames ||
                    observation.presentationPresentedFrames <= m_initialPresentationPresentedFrames ||
                    observation.runtimeWorldSubmittedFrames <= m_initialRuntimeSubmittedFrames)
                    return AwaitNextFrame();
            }

            app::StateMachineFailure stateFailure;
            if (!m_config->states->RequestExit(0, &stateFailure))
                return engine::FrameParticipantStatus::Failure(stateFailure.message != nullptr
                                                                   ? stateFailure.message
                                                                   : "runtime world-frame integration test could not request application exit");
            return engine::FrameParticipantStatus::Success();
        }

        static engine::FrameParticipantStatus ExecuteFrame(const engine::FrameContext&, void* const userData) noexcept
        {
            auto* const service = static_cast<RuntimeWorldFrameIntegrationTestService*>(userData);
            return service != nullptr ? service->ValidateFrame()
                                      : engine::FrameParticipantStatus::Failure("runtime world-frame integration-test service is unavailable");
        }

        runtime::testing::RuntimeWorldFrameIntegrationTestServiceConfig* m_config = nullptr;
        engine::RenderingService* m_rendering = nullptr;
        engine::WorldSessionService* m_session = nullptr;
        engine::GameWorldService* m_gameWorld = nullptr;
        engine::ResourceStreamingService* m_sources = nullptr;
        vanguard::u32 m_frames = 0;
        vanguard::u64 m_initialWorldGeneration = 0;
        vanguard::u64 m_initialPresentationRenderedFrames = 0;
        vanguard::u64 m_initialPresentationPresentedFrames = 0;
        vanguard::u64 m_initialRuntimeSubmittedFrames = 0;
        rendering::RenderSceneHandle m_initialScene;
        bool m_replacementRequested = false;
    };

    app::Service* CreateRuntimeWorldFrameIntegrationTestService(void* const userData) noexcept
    {
        auto* const config = static_cast<runtime::testing::RuntimeWorldFrameIntegrationTestServiceConfig*>(userData);
        if (config == nullptr)
            return nullptr;
        const vanguard::memory::MemoryBlock block = vanguard::memory::Allocate(
            vanguard::memory::PoolId::Tools, sizeof(RuntimeWorldFrameIntegrationTestService),
            alignof(RuntimeWorldFrameIntegrationTestService));
        return block ? ::new (block.address) RuntimeWorldFrameIntegrationTestService(*config) : nullptr;
    }

    void DestroyRuntimeWorldFrameIntegrationTestService(app::Service* const service, void*) noexcept
    {
        if (service == nullptr)
            return;
        static_cast<RuntimeWorldFrameIntegrationTestService*>(service)->~RuntimeWorldFrameIntegrationTestService();
        vanguard::memory::MemoryBlock block{service, sizeof(RuntimeWorldFrameIntegrationTestService), vanguard::memory::PoolId::Tools};
        vanguard::memory::Free(block);
    }

    [[nodiscard]] bool RegisterRuntimeWorldFrameIntegrationTestService(
        app::EngineHost& host, runtime::testing::RuntimeWorldFrameIntegrationTestServiceConfig& config,
        app::HostFailure* const failure) noexcept
    {
        constexpr app::ServiceDependency dependencies[]{
            {engine::FramePipelineServiceId, app::DependencyKind::Required},
            {engine::RenderingServiceId, app::DependencyKind::Required},
            {engine::ResourceStreamingServiceId, app::DependencyKind::Required},
            {engine::GameWorldServiceId, app::DependencyKind::Required},
            {engine::WorldSessionServiceId, app::DependencyKind::Required},
            {runtime::RuntimeViewportServiceId, app::DependencyKind::Required}};
        app::ServiceDescriptor descriptor;
        descriptor.id = RuntimeWorldFrameIntegrationTestServiceId;
        descriptor.name = "runtimeWorldFrameIntegrationTest";
        descriptor.profiles = app::ApplicationProfile::Runtime;
        descriptor.scope = app::ServiceScope::TestSandbox;
        descriptor.affinity = app::ThreadAffinity::MainThread;
        descriptor.dependencies = dependencies;
        descriptor.create = CreateRuntimeWorldFrameIntegrationTestService;
        descriptor.destroy = DestroyRuntimeWorldFrameIntegrationTestService;
        descriptor.userData = &config;
        return host.RegisterService(runtime::RuntimeModuleId, descriptor, failure);
    }
} // namespace

namespace vanguard::runtime::testing
{
    application::StateOperationStatus RuntimeWorldReplacementValidationState::OnEnter(application::StateContext& context) noexcept
    {
        engine::WorldSessionService* const session = engine::FindWorldSessionService(context.GetServices());
        engine::ResourceStreamingService* const sources = engine::FindResourceStreamingService(context.GetServices());
        if (session == nullptr || sources == nullptr || !sources->GetPackageSet().IsMounted() ||
            session->GetStatus() != engine::WorldSessionStatus::Idle)
            return application::StateOperationStatus::Failure("runtime world-replacement state requires one fully stopped mounted session");

        engine::WorldSessionStartRequest request;
        request.world = sources->GetPackageSet().StartupWorld();
        request.input = sources->GetPackageSet().GetDefaultInput();
        request.inputMode = engine::WorldSessionInputMode::Mapping;
        request.cameraSource = engine::WorldSessionCameraSource::WorldDefault;
        engine::WorldSessionFailure failure;
        if (!session->Begin(request, &failure))
            return application::StateOperationStatus::Failure(failure.message != nullptr ? failure.message
                                                                                        : "runtime replacement session start failed");
        m_transitionRequested = false;
        return application::StateOperationStatus::Complete();
    }

    application::StateTickStatus RuntimeWorldReplacementValidationState::OnTick(application::StateContext& context) noexcept
    {
        engine::WorldSessionService* const session = engine::FindWorldSessionService(context.GetServices());
        if (session == nullptr)
            return application::StateTickStatus::Failure("runtime replacement World Session service became unavailable");
        engine::WorldSessionFailure failure;
        const engine::WorldSessionStatus status = session->Poll(&failure);
        if (status == engine::WorldSessionStatus::LoadingWorld)
            return application::StateTickStatus::Success();
        if (status == engine::WorldSessionStatus::Stopping || status == engine::WorldSessionStatus::Failed)
        {
            const engine::WorldSessionFailure& reported = session->GetLastFailure();
            return application::StateTickStatus::Failure(reported.message != nullptr ? reported.message
                                                                                    : "runtime replacement session failed");
        }
        if (status != engine::WorldSessionStatus::Running)
            return application::StateTickStatus::Failure("runtime replacement session entered an invalid state");
        application::StateMachineFailure stateFailure;
        if (!context.RequestTransition(RuntimeRunningStateId, &stateFailure))
            return application::StateTickStatus::Failure(stateFailure.message != nullptr ? stateFailure.message
                                                                                        : "runtime replacement could not return to running");
        m_transitionRequested = true;
        return application::StateTickStatus::Success();
    }

    application::StateOperationStatus RuntimeWorldReplacementValidationState::OnExit(application::StateContext& context) noexcept
    {
        if (m_transitionRequested)
            return application::StateOperationStatus::Complete();

        engine::WorldSessionService* const session = engine::FindWorldSessionService(context.GetServices());
        if (session == nullptr)
            return application::StateOperationStatus::Failure("runtime replacement World Session service became unavailable during exit");
        if (session->GetStatus() == engine::WorldSessionStatus::Idle)
            return application::StateOperationStatus::Complete();
        engine::WorldSessionFailure failure;
        if (!session->RequestStop(&failure))
            return application::StateOperationStatus::Failure(failure.message != nullptr ? failure.message
                                                                                        : "runtime replacement cleanup stop request failed");
        const engine::WorldSessionStatus status = session->Poll(&failure);
        if (status == engine::WorldSessionStatus::Idle)
            return application::StateOperationStatus::Complete();
        if (status == engine::WorldSessionStatus::Failed)
            return application::StateOperationStatus::Failure(session->GetLastFailure().message != nullptr
                                                                  ? session->GetLastFailure().message
                                                                  : "runtime replacement cleanup failed");
        return application::StateOperationStatus::Pending();
    }

    ApplicationTraits RuntimeWorldFrameIntegrationTestApplication::GetTraits() const noexcept
    {
        ApplicationTraits traits = m_runtime.GetTraits();
        traits.processName = "runtimeWorldFrameIntegrationTest";
        traits.diagnosticsFilePath = "VanguardRuntimeWorldFrameIntegrationTest.log";
        return traits;
    }

    application::CompositionStatus RuntimeWorldFrameIntegrationTestApplication::Compose(
        const application::ApplicationStartupContext& startup, application::EngineHost& services,
        application::ApplicationStateMachine& states) noexcept
    {
        const bool validateWorldFrame = startup.commandLine.HasArgument("--validate-world-frame");
        const bool validateWorldReplacement = startup.commandLine.HasArgument("--validate-world-replacement");
        if (!validateWorldFrame && !validateWorldReplacement)
            return application::CompositionStatus::Failure(
                "runtime world-frame integration-test executable requires --validate-world-frame or --validate-world-replacement");

        const application::CompositionStatus productComposition = m_runtime.Compose(startup, services, states);
        if (!productComposition)
            return productComposition;

        m_serviceConfig.states = &states;
        m_serviceConfig.replacementPath = validateWorldReplacement;
        if (validateWorldReplacement &&
            !states.RegisterState({RuntimeWorldReplacementValidationStateId, "worldReplacementValidation", &m_replacementState}))
            return application::CompositionStatus::Failure("runtime world-replacement integration-test state registration failed");
        application::HostFailure failure;
        if (!RegisterRuntimeWorldFrameIntegrationTestService(services, m_serviceConfig, &failure))
            return application::CompositionStatus::Failure(failure.message != nullptr
                                                               ? failure.message
                                                               : "runtime world-frame integration-test service registration failed");
        return application::CompositionStatus::Success();
    }
} // namespace vanguard::runtime::testing
