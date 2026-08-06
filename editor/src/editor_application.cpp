#include "editor_application.hpp"

#include <vanguard/diagnostics/diagnostics.hpp>
#include <vanguard/engine/engine_services.hpp>
#include <vanguard/engine/frame_pipeline_service.hpp>

namespace vanguard::editor
{
    namespace
    {
        inline constexpr application::ModuleId EditorModule = 0x656469746f720001ull;
        inline constexpr application::StateId RunningStateId = 0x72756e6e696e6701ull;
    }

    application::StateOperationStatus RunningState::OnEnter(application::StateContext& context) noexcept
    {
        engine::FramePipelineService* const framePipeline = engine::FindFramePipelineService(context.Services());
        if (framePipeline == nullptr)
            return application::StateOperationStatus::Failure("Frame Pipeline service is unavailable");
        engine::FrameFailure failure;
        if (!framePipeline->Compile(&failure))
        {
            VG_LOG_ERROR(diagnostics::Category::Engine,
                         "frame pipeline compilation failed: code=%u phase=%u participant=%llu related=%llu name=%s message=%s",
                         static_cast<u32>(failure.code), static_cast<u32>(failure.phase), failure.participant,
                         failure.relatedParticipant, failure.participantName != nullptr ? failure.participantName : "<none>",
                         failure.message != nullptr ? failure.message : "<none>");
            return application::StateOperationStatus::Failure(
                failure.message != nullptr ? failure.message : "frame pipeline compilation failed");
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
        engine::FramePipelineService* const framePipeline = engine::FindFramePipelineService(context.Services());
        if (framePipeline == nullptr) return application::StateTickStatus::Failure("Frame Pipeline service is unavailable");
        engine::FrameFailure failure;
        if (!framePipeline->RunFrame(&failure))
        {
            VG_LOG_ERROR(diagnostics::Category::Engine,
                         "editor frame failed: frame=%llu code=%u phase=%u participant=%llu related=%llu name=%s message=%s",
                         failure.frame, static_cast<u32>(failure.code), static_cast<u32>(failure.phase), failure.participant,
                         failure.relatedParticipant, failure.participantName != nullptr ? failure.participantName : "<none>",
                         failure.message != nullptr ? failure.message : "<none>");
            return application::StateTickStatus::Failure(
                failure.message != nullptr ? failure.message : "editor frame failed");
        }
        return application::StateTickStatus::Success();
    }

    ApplicationTraits EditorApplication::GetTraits() const noexcept
    {
        return {"editor", application::ApplicationProfile::Runtime | application::ApplicationProfile::Editor |
                              application::ApplicationProfile::Tool, 1200};
    }

    application::CompositionStatus EditorApplication::Compose(
        const application::ApplicationStartupContext& startup,
        application::EngineHost& services,
        application::ApplicationStateMachine& states) noexcept
    {
        m_runningState.ExitAfterFirstTick(startup.commandLine.HasArgument("--validate-bootstrap"));
        application::HostFailure failure;
        if (!engine::RegisterEngineModule(services, &failure) || !engine::RegisterIoService(services, &failure) ||
            !engine::RegisterFilesystemService(services, &failure) ||
            !engine::RegisterJobsService(services, &failure) ||
            !engine::RegisterFramePipelineService(services, &failure) ||
            !engine::RegisterWindowService(services, startup.platform, &failure) ||
            !engine::RegisterInputService(services, startup.platform->InputBackend(), &failure) ||
            !engine::RegisterGameInputService(services, &failure) ||
            !engine::RegisterResourcesService(services, &failure) ||
            !engine::RegisterResourceStreamingService(services, &failure) ||
            !engine::RegisterWorldService(services, &failure) ||
            !engine::RegisterGameWorldService(services, &failure) ||
            !engine::RegisterStreamingObserverService(services, &failure) ||
            !engine::RegisterWorldSessionService(services, &failure) ||
            !services.RegisterModule({EditorModule, "editor", 1}, &failure))
            return application::CompositionStatus::Failure(
                failure.message != nullptr ? failure.message : "editor engine service registration failed");
        if (!states.RegisterState({RunningStateId, "running", &m_runningState}) ||
            !states.SetInitialState(RunningStateId))
            return application::CompositionStatus::Failure("editor application state registration failed");
        return application::CompositionStatus::Success();
    }
} // namespace vanguard::editor
