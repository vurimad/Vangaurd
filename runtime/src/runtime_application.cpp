#include <vanguard/runtime/runtime_application.hpp>

#include <vanguard/diagnostics/diagnostics.hpp>
#include <vanguard/engine/engine_services.hpp>
#include <vanguard/engine/frame_pipeline_service.hpp>
#include <vanguard/engine/world_session_service.hpp>
#include <vanguard/filesystem/filesystem.hpp>
#include <vanguard/rhi/d3d12/backend.hpp>

namespace vanguard::runtime
{
    namespace
    {
        inline constexpr application::ModuleId RuntimeModule = 0x72756e74696d6501ull;
        inline constexpr application::StateId StartupSessionStateId = 0x7374617274736501ull;
        inline constexpr application::StateId RunningStateId = 0x72756e6e696e6701ull;

        application::StateOperationStatus StopSession(application::StateContext& context) noexcept
        {
            engine::WorldSessionService* const session = engine::FindWorldSessionService(context.GetServices());
            if (session == nullptr)
                return application::StateOperationStatus::Failure("World Session service is unavailable");

            engine::WorldSessionFailure failure;
            if (!session->RequestStop(engine::WorldSessionStopMode::ReleaseEverything, &failure))
                return application::StateOperationStatus::Failure(failure.message != nullptr ? failure.message : "world session stop request failed");
            const engine::WorldSessionStatus status = session->Poll(&failure);
            if (status == engine::WorldSessionStatus::Idle)
                return application::StateOperationStatus::Complete();
            if (status == engine::WorldSessionStatus::Failed)
            {
                const engine::WorldSessionFailure& reported = session->GetLastFailure();
                VG_LOG_ERROR(diagnostics::Category::Engine, "world session stop failed: code=%u packageResult=%u resourceFailure=%u message=%s",
                             static_cast<u32>(reported.code), static_cast<u32>(reported.packageResult), static_cast<u32>(reported.resourceFailure),
                             reported.message != nullptr ? reported.message : "<none>");
                return application::StateOperationStatus::Failure(reported.message != nullptr ? reported.message : "world session stop failed");
            }
            return application::StateOperationStatus::Pending();
        }
    } // namespace

    application::StateOperationStatus StartupSessionState::OnEnter(application::StateContext& context) noexcept
    {
        engine::WorldSessionService* const session = engine::FindWorldSessionService(context.GetServices());
        if (session == nullptr || !filesystem::IsInitialized())
            return application::StateOperationStatus::Failure("runtime session dependencies are unavailable");

        engine::WorldSessionStartRequest request;
        request.gameDirectory = filesystem::GetManager().GetGameRoot();
        engine::WorldSessionFailure failure;
        if (!session->Begin(request, &failure))
        {
            VG_LOG_ERROR(diagnostics::Category::Resources, "runtime session start failed: root=%s code=%u packageResult=%u resourceFailure=%u message=%s",
                         request.gameDirectory.ToDebugString(), static_cast<u32>(failure.code), static_cast<u32>(failure.packageResult),
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
        if (status == engine::WorldSessionStatus::Failed)
        {
            const engine::WorldSessionFailure& reported = session->GetLastFailure();
            VG_LOG_ERROR(diagnostics::Category::Resources, "runtime session loading failed: code=%u packageResult=%u resourceFailure=%u message=%s",
                         static_cast<u32>(reported.code), static_cast<u32>(reported.packageResult), static_cast<u32>(reported.resourceFailure),
                         reported.message != nullptr ? reported.message : "<none>");
            return application::StateTickStatus::Failure(reported.message != nullptr ? reported.message : "runtime session loading failed");
        }
        if (status != engine::WorldSessionStatus::Running)
            return application::StateTickStatus::Failure("runtime session entered an invalid startup state");
        if (!context.RequestTransition(RunningStateId))
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
        return {"runtime", application::ApplicationProfile::Runtime, 600, nullptr};
    }

    application::CompositionStatus RuntimeApplication::Compose(const application::ApplicationStartupContext& startup, application::EngineHost& services,
                                                               application::ApplicationStateMachine& states) noexcept
    {
        const filesystem::AbsolutePath runtimeRoot = filesystem::paths::GetExecutableDirectory();
        m_filesystemConfig = {runtimeRoot, runtimeRoot, filesystem::paths::GetUserCacheDirectory()};
        m_renderingConfig = {};
        m_renderingConfig.deviceMode = engine::RenderingDeviceMode::Required;
        m_renderingConfig.backendFactory = rhi::d3d12::GetBackendFactory();
        m_runningState.ExitAfterFirstTick(startup.commandLine.HasArgument("--validate-bootstrap"));
        application::HostFailure failure;
        if (!engine::RegisterEngineModule(services, &failure) || !engine::RegisterIoService(services, &failure) ||
            !engine::RegisterFilesystemService(services, m_filesystemConfig, &failure) || !engine::RegisterJobsService(services, &failure) ||
            !engine::RegisterFramePipelineService(services, &failure) || !engine::RegisterRenderingService(services, m_renderingConfig, &failure) ||
            !engine::RegisterReflectionService(services, &failure) || !engine::RegisterWindowService(services, startup.platform, &failure) ||
            !engine::RegisterInputService(services, startup.platform->GetInputBackend(), &failure) || !engine::RegisterGameInputService(services, &failure) ||
            !engine::RegisterResourcesService(services, &failure) || !engine::RegisterResourceStreamingService(services, &failure) ||
            !engine::RegisterWorldService(services, &failure) || !engine::RegisterGameWorldService(services, &failure) ||
            !engine::RegisterStreamingObserverService(services, &failure) || !engine::RegisterWorldSessionService(services, &failure) ||
            !services.RegisterModule({RuntimeModule, "runtime", 1}, &failure))
            return application::CompositionStatus::Failure(failure.message != nullptr ? failure.message : "runtime engine service registration failed");
        const bool validateBootstrap = startup.commandLine.HasArgument("--validate-bootstrap");
        if (!states.RegisterState({StartupSessionStateId, "startupSession", &m_startupSessionState}) ||
            !states.RegisterState({RunningStateId, "running", &m_runningState}) ||
            !states.SetInitialState(validateBootstrap ? RunningStateId : StartupSessionStateId))
            return application::CompositionStatus::Failure("runtime application state registration failed");
        return application::CompositionStatus::Success();
    }
} // namespace vanguard::runtime
