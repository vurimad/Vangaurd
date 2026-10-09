#include <vanguard/runtime/runtime_application.hpp>
#include <vanguard/runtime/runtime_viewport_service.hpp>

#include <vanguard/diagnostics/diagnostics.hpp>
#include <vanguard/engine/engine_services.hpp>
#include <vanguard/engine/frame_pipeline_service.hpp>
#include <vanguard/engine/rendering_service.hpp>
#include <vanguard/engine/world_session_service.hpp>
#include <vanguard/filesystem/filesystem.hpp>
#include <vanguard/rhi/d3d12/backend.hpp>

#include <cstring>

namespace vanguard::runtime
{
    namespace
    {
        [[nodiscard]] bool ParseWindowDimension(const char* const text, const u32 maximum, u32& value) noexcept
        {
            value = 0;
            if (text == nullptr || text[0] == '\0')
                return false;
            u32 parsed = 0;
            for (const char* cursor = text; *cursor != '\0'; ++cursor)
            {
                if (*cursor < '0' || *cursor > '9')
                    return false;
                const u32 digit = static_cast<u32>(*cursor - '0');
                if (parsed > (maximum - digit) / 10u)
                    return false;
                parsed = parsed * 10u + digit;
            }
            if (parsed == 0 || parsed > maximum)
                return false;
            value = parsed;
            return true;
        }

        [[nodiscard]] const char* ParseRuntimeWindowExtent(const application::CommandLineView commandLine,
                                                           window::WindowExtent& extent) noexcept
        {
            extent = window::WindowPlacement{}.logicalExtent;
            bool widthSeen = false;
            bool heightSeen = false;
            const window::WindowConstraints constraints;
            for (i32 index = 1; index < commandLine.argumentCount; ++index)
            {
                const char* const argument = commandLine[index];
                const bool widthArgument = argument != nullptr && std::strcmp(argument, "--window-width") == 0;
                const bool heightArgument = argument != nullptr && std::strcmp(argument, "--window-height") == 0;
                if (!widthArgument && !heightArgument)
                    continue;
                if (index + 1 >= commandLine.argumentCount)
                    return widthArgument ? "--window-width requires one explicit positive integer"
                                         : "--window-height requires one explicit positive integer";
                const char* const valueText = commandLine[++index];
                u32 value = 0;
                if (widthArgument)
                {
                    if (widthSeen)
                        return "--window-width may be specified only once";
                    if (!ParseWindowDimension(valueText, constraints.maximum.width, value))
                        return "--window-width must be between 1 and the window-system maximum";
                    extent.width = value;
                    widthSeen = true;
                }
                else
                {
                    if (heightSeen)
                        return "--window-height may be specified only once";
                    if (!ParseWindowDimension(valueText, constraints.maximum.height, value))
                        return "--window-height must be between 1 and the window-system maximum";
                    extent.height = value;
                    heightSeen = true;
                }
            }
            if (widthSeen != heightSeen)
                return "--window-width and --window-height must be specified together";
            return nullptr;
        }

        application::StateOperationStatus StopSession(application::StateContext& context) noexcept
        {
            engine::WorldSessionService* const session = engine::FindWorldSessionService(context.GetServices());
            if (session == nullptr)
                return application::StateOperationStatus::Failure("World Session service is unavailable");
            if (session->GetStatus() == engine::WorldSessionStatus::Idle)
                return application::StateOperationStatus::Complete();
            if (session->GetStatus() == engine::WorldSessionStatus::Failed)
            {
                const engine::WorldSessionFailure& reported = session->GetLastFailure();
                return application::StateOperationStatus::Failure(reported.message != nullptr ? reported.message : "world session stop failed");
            }

            engine::WorldSessionFailure failure;
            const bool stopRequested = session->RequestStop(&failure);
            if (!stopRequested)
                return application::StateOperationStatus::Failure(failure.message != nullptr ? failure.message : "world session stop request failed");
            const engine::WorldSessionStatus status = session->Poll(&failure);
            if (status == engine::WorldSessionStatus::Idle)
                return application::StateOperationStatus::Complete();
            if (status == engine::WorldSessionStatus::Failed)
            {
                const engine::WorldSessionFailure& reported = session->GetLastFailure();
                VG_LOG_ERROR(diagnostics::Category::Engine, "world session stop failed: code=%u resourceFailure=%u message=%s",
                             static_cast<u32>(reported.code), static_cast<u32>(reported.resourceFailure),
                             reported.message != nullptr ? reported.message : "<none>");
                return application::StateOperationStatus::Failure(reported.message != nullptr ? reported.message : "world session stop failed");
            }
            return application::StateOperationStatus::Pending();
        }
    } // namespace

    application::StateOperationStatus StartupSessionState::OnEnter(application::StateContext& context) noexcept
    {
        engine::WorldSessionService* const session = engine::FindWorldSessionService(context.GetServices());
        engine::ResourceStreamingService* const sources = engine::FindResourceStreamingService(context.GetServices());
        if (session == nullptr || sources == nullptr || !sources->GetPackageSet().IsMounted())
            return application::StateOperationStatus::Failure("runtime session dependencies are unavailable");

        engine::WorldSessionStartRequest request;
        request.world = sources->GetPackageSet().StartupWorld();
        request.input = sources->GetPackageSet().GetDefaultInput();
        request.inputMode = engine::WorldSessionInputMode::Mapping;
        request.cameraSource = engine::WorldSessionCameraSource::WorldDefault;
        engine::WorldSessionFailure failure;
        const bool sessionStarted = session->Begin(request, &failure);
        if (!sessionStarted)
        {
            VG_LOG_ERROR(diagnostics::Category::Resources, "runtime session start failed: code=%u resourceFailure=%u message=%s",
                         static_cast<u32>(failure.code),
                         static_cast<u32>(failure.resourceFailure), failure.message != nullptr ? failure.message : "<none>");
            return application::StateOperationStatus::Failure(failure.message != nullptr ? failure.message : "runtime session start failed");
        }
        m_transitionRequested = false;
        return application::StateOperationStatus::Complete();
    }

    application::StateTickStatus StartupSessionState::OnTick(application::StateContext& context) noexcept
    {
        engine::WorldSessionService* const session = engine::FindWorldSessionService(context.GetServices());
        if (session == nullptr)
            return application::StateTickStatus::Failure("World Session service became unavailable");
        engine::WorldSessionFailure failure;
        const engine::WorldSessionStatus status = session->Poll(&failure);
        if (status == engine::WorldSessionStatus::LoadingWorld)
            return application::StateTickStatus::Success();
        if (status == engine::WorldSessionStatus::Stopping && failure.code != engine::WorldSessionFailureCode::None)
        {
            const engine::WorldSessionFailure& reported = session->GetLastFailure();
            VG_LOG_ERROR(diagnostics::Category::Resources, "runtime session loading failed: code=%u resourceFailure=%u message=%s",
                         static_cast<u32>(reported.code), static_cast<u32>(reported.resourceFailure),
                         reported.message != nullptr ? reported.message : "<none>");
            return application::StateTickStatus::Failure(reported.message != nullptr ? reported.message : "runtime session loading failed");
        }
        if (status == engine::WorldSessionStatus::Failed)
        {
            const engine::WorldSessionFailure& reported = session->GetLastFailure();
            VG_LOG_ERROR(diagnostics::Category::Resources, "runtime session loading failed: code=%u resourceFailure=%u message=%s",
                         static_cast<u32>(reported.code), static_cast<u32>(reported.resourceFailure),
                         reported.message != nullptr ? reported.message : "<none>");
            return application::StateTickStatus::Failure(reported.message != nullptr ? reported.message : "runtime session loading failed");
        }
        if (status != engine::WorldSessionStatus::Running)
            return application::StateTickStatus::Failure("runtime session entered an invalid startup state");
        if (!context.RequestTransition(RuntimeRunningStateId))
            return application::StateTickStatus::Failure("running state transition failed");
        m_transitionRequested = true;
        return application::StateTickStatus::Success();
    }

    application::StateOperationStatus StartupSessionState::OnExit(application::StateContext& context) noexcept
    {
        if (m_transitionRequested)
            return application::StateOperationStatus::Complete();
        return StopSession(context);
    }

    application::StateOperationStatus RunningState::OnEnter(application::StateContext& context) noexcept
    {
        engine::FramePipelineService* const framePipeline = engine::FindFramePipelineService(context.GetServices());
        if (framePipeline == nullptr)
            return application::StateOperationStatus::Failure("Frame Pipeline service is unavailable");
        engine::FrameFailure failure;
        if (framePipeline->GetState() == engine::FramePipelineState::Building)
        {
            engine::FramePipelineConfig frameConfig;
            frameConfig.pacing = engine::FramePacingMode::Disabled;
            if (!framePipeline->Configure(frameConfig, &failure))
            {
                VG_LOG_ERROR(diagnostics::Category::Engine, "runtime frame pipeline configuration failed: code=%u message=%s",
                             static_cast<u32>(failure.code), failure.message != nullptr ? failure.message : "<none>");
                return application::StateOperationStatus::Failure(failure.message != nullptr ? failure.message : "runtime frame pipeline configuration failed");
            }
        }
        if (!framePipeline->Compile(&failure))
        {
            VG_LOG_ERROR(diagnostics::Category::Engine, "frame pipeline compilation failed: code=%u phase=%u participant=%llu related=%llu name=%s message=%s",
                         static_cast<u32>(failure.code), static_cast<u32>(failure.phase), failure.participant, failure.relatedParticipant,
                         failure.participantName != nullptr ? failure.participantName : "<none>", failure.message != nullptr ? failure.message : "<none>");
            return application::StateOperationStatus::Failure(failure.message != nullptr ? failure.message : "frame pipeline compilation failed");
        }
        return application::StateOperationStatus::Complete();
    }

    application::StateTickStatus RunningState::OnTick(application::StateContext& context) noexcept
    {
        if (m_exitAfterFirstTick)
        {
            static_cast<void>(context.RequestExit());
            return application::StateTickStatus::Success();
        }
        engine::FramePipelineService* const framePipeline = engine::FindFramePipelineService(context.GetServices());
        if (framePipeline == nullptr)
            return application::StateTickStatus::Failure("Frame Pipeline service is unavailable");
        engine::FrameFailure failure;
        if (!framePipeline->RunFrame(&failure))
        {
            VG_LOG_ERROR(diagnostics::Category::Engine, "engine frame failed: frame=%llu code=%u phase=%u participant=%llu related=%llu name=%s message=%s",
                         failure.frame, static_cast<u32>(failure.code), static_cast<u32>(failure.phase), failure.participant, failure.relatedParticipant,
                         failure.participantName != nullptr ? failure.participantName : "<none>", failure.message != nullptr ? failure.message : "<none>");
            return application::StateTickStatus::Failure(failure.message != nullptr ? failure.message : "engine frame failed");
        }
        return application::StateTickStatus::Success();
    }

    application::StateOperationStatus RunningState::OnExit(application::StateContext& context) noexcept
    {
        return StopSession(context);
    }

    ApplicationTraits RuntimeApplication::GetTraits() const noexcept
    {
        return {"runtime", application::ApplicationProfile::Runtime, 600, "VanguardRuntime.log"};
    }

    application::CompositionStatus RuntimeApplication::Compose(const application::ApplicationStartupContext& startup, application::EngineHost& services,
                                                               application::ApplicationStateMachine& states) noexcept
    {
        const filesystem::AbsolutePath runtimeRoot = filesystem::paths::GetExecutableDirectory();
        m_filesystemConfig = {runtimeRoot, runtimeRoot, filesystem::paths::GetUserCacheDirectory()};
        m_streamingConfig = {};
        if (!startup.commandLine.HasArgument("--validate-bootstrap"))
            m_streamingConfig.packageDirectory = runtimeRoot;
        m_renderingConfig = {};
        m_renderingConfig.deviceMode = engine::RenderingDeviceMode::Required;
        m_renderingConfig.usePackageRendererBootstrap = !startup.commandLine.HasArgument("--validate-bootstrap");
        m_renderingConfig.rendererOutputAttachments = detail::MakeRuntimeSdrOutputAttachments();
        m_renderingConfig.enableFrameDiagnostics = startup.commandLine.HasArgument("--render-diagnostics");
        m_renderingConfig.backendFactory = rhi::d3d12::GetBackendFactory();
        m_windowConfig = {};
        m_windowConfig.platform = startup.platform;
        if (const char* const windowFailure = ParseRuntimeWindowExtent(startup.commandLine, m_windowConfig.primaryLogicalExtent))
            return application::CompositionStatus::Failure(windowFailure);
        m_runningState.ExitAfterFirstTick(startup.commandLine.HasArgument("--validate-bootstrap"));
        application::HostFailure failure;
        if (!engine::RegisterEngineModule(services, &failure) || !services.RegisterModule({RuntimeModuleId, "runtime", 1}, &failure) ||
            !engine::RegisterIoService(services, &failure) ||
            !engine::RegisterFilesystemService(services, m_filesystemConfig, &failure) || !engine::RegisterJobsService(services, &failure) ||
            !engine::RegisterFramePipelineService(services, &failure) || !engine::RegisterRenderingService(services, m_renderingConfig, &failure) ||
            !engine::RegisterReflectionService(services, &failure) || !engine::RegisterWindowService(services, m_windowConfig, &failure) ||
            !engine::RegisterInputService(services, startup.platform->GetInputBackend(), &failure) || !engine::RegisterGameInputService(services, &failure) ||
            !engine::RegisterResourcesService(services, &failure) || !engine::RegisterResourceStreamingService(services, m_streamingConfig, &failure) ||
            !engine::RegisterWorldService(services, &failure) || !engine::RegisterGameWorldService(services, &failure) ||
            !engine::RegisterStreamingObserverService(services, &failure) || !engine::RegisterWorldSessionService(services, &failure) ||
            !RegisterRuntimeViewportService(services, &failure))
            return application::CompositionStatus::Failure(failure.message != nullptr ? failure.message : "runtime engine service registration failed");
        const bool validateBootstrap = startup.commandLine.HasArgument("--validate-bootstrap");
        if (!states.RegisterState({RuntimeStartupSessionStateId, "startupSession", &m_startupSessionState}) ||
            !states.RegisterState({RuntimeRunningStateId, "running", &m_runningState}) ||
            !states.SetInitialState(validateBootstrap ? RuntimeRunningStateId : RuntimeStartupSessionStateId))
            return application::CompositionStatus::Failure("runtime application state registration failed");
        return application::CompositionStatus::Success();
    }
} // namespace vanguard::runtime
