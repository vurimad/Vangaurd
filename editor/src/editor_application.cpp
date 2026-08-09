#include "editor_application.hpp"

#include <vanguard/diagnostics/diagnostics.hpp>
#include <vanguard/engine/engine_services.hpp>
#include <vanguard/engine/frame_pipeline_service.hpp>
#include <vanguard/engine/window_service.hpp>
#include <vanguard/engine/world_session_service.hpp>
#include <vanguard/projects/project.hpp>
#include <vanguard/resources/resources.hpp>
#include <vanguard/world/worlds.hpp>

namespace vanguard::editor
{
    namespace
    {
        inline constexpr application::StateId StartupSessionStateId = 0x6564737461727401ull;
        inline constexpr application::StateId RunningStateId = 0x72756e6e696e6701ull;

        [[nodiscard]] bool HasProjectExtension(const char* const path) noexcept
        {
            if (path == nullptr)
                return false;
            const char suffix[] = ".vproject";
            u32 length = 0;
            while (path[length] != '\0')
                ++length;
            constexpr u32 suffixLength = sizeof(suffix) - 1;
            if (length < suffixLength)
                return false;
            for (u32 index = 0; index < suffixLength; ++index)
                if (path[length - suffixLength + index] != suffix[index])
                    return false;
            return true;
        }

        [[nodiscard]] const char* FindProjectArgument(const application::CommandLineView commandLine) noexcept
        {
            for (i32 index = 1; index < commandLine.argumentCount; ++index)
                if (HasProjectExtension(commandLine[index]))
                    return commandLine[index];
            return nullptr;
        }

        [[nodiscard]] filesystem::AbsolutePath ResolveProjectFile(const char* const argument) noexcept
        {
            const containers::StringView path(argument);
            return filesystem::AbsolutePath::IsValidPath(path) ? filesystem::AbsolutePath::CreateFilePath(path)
                                                               : filesystem::paths::GetCurrentWorkingDirectory().AddFilePath(path);
        }

        [[nodiscard]] application::StateOperationStatus StopSession(application::StateContext& context) noexcept
        {
            engine::WorldSessionService* const session = engine::FindWorldSessionService(context.Services());
            if (session == nullptr)
                return application::StateOperationStatus::Failure("World Session service is unavailable");
            if (session->Status() == engine::WorldSessionStatus::Idle)
                return application::StateOperationStatus::Complete();

            engine::WorldSessionFailure failure;
            if (!session->RequestStop(engine::WorldSessionStopMode::ReleaseEverything, &failure))
                return application::StateOperationStatus::Failure(
                    failure.message != nullptr ? failure.message : "editor world session stop request failed");
            const engine::WorldSessionStatus status = session->Poll(&failure);
            if (status == engine::WorldSessionStatus::Idle) return application::StateOperationStatus::Complete();
            if (status == engine::WorldSessionStatus::Failed)
                return application::StateOperationStatus::Failure(
                    failure.message != nullptr ? failure.message : "editor world session stop failed");
            return application::StateOperationStatus::Pending();
        }
    } // namespace

    application::StateOperationStatus StartupSessionState::OnEnter(application::StateContext& context) noexcept
    {
        ProjectWorkspaceService* const workspace = FindProjectWorkspaceService(context.Services());
        engine::WorldSessionService* const session = engine::FindWorldSessionService(context.Services());
        if (workspace == nullptr || session == nullptr)
            return application::StateOperationStatus::Failure("editor startup dependencies are unavailable");

        m_worldConfigured = !workspace->Project().editorWorld.Empty();
        m_transitionRequested = false;
        if (!m_worldConfigured) return application::StateOperationStatus::Complete();

        // The project stores the logical resource identity used by both loose cooked artifacts and VPAK indexes.
        const resources::ResourcePath worldPath = resources::ResourcePath::FromString(workspace->Project().editorWorld);
        if (!worldPath.IsValid())
            return application::StateOperationStatus::Failure("startup.editorWorld is not a valid resource identity");

        engine::WorldSessionStartRequest request;
        request.gameDirectory = workspace->BuildsRoot().AddDirPath("Windows").AddDirPath("Development");
        request.world = resources::ResourceReference(worldPath, world::WorldResourceType);
        engine::WorldSessionFailure failure;
        if (!session->Begin(request, &failure))
        {
            VG_LOG_ERROR(diagnostics::Category::Resources,
                         "editor world session start failed: packages=%s world=%s code=%u packageResult=%u resourceFailure=%u message=%s",
                         request.gameDirectory.ToDebugString(), workspace->Project().editorWorld.AsChar(),
                         static_cast<u32>(failure.code), static_cast<u32>(failure.packageResult),
                         static_cast<u32>(failure.resourceFailure), failure.message != nullptr ? failure.message : "<none>");
            return application::StateOperationStatus::Failure(
                failure.message != nullptr ? failure.message : "editor world session start failed");
        }
        return application::StateOperationStatus::Complete();
    }

    application::StateTickStatus StartupSessionState::OnTick(application::StateContext& context) noexcept
    {
        if (!m_worldConfigured)
        {
            if (!context.RequestTransition(RunningStateId))
                return application::StateTickStatus::Failure("editor running-state transition failed");
            m_transitionRequested = true;
            return application::StateTickStatus::Success();
        }

        engine::WorldSessionService* const session = engine::FindWorldSessionService(context.Services());
        if (session == nullptr) return application::StateTickStatus::Failure("World Session service became unavailable");
        engine::WorldSessionFailure failure;
        const engine::WorldSessionStatus status = session->Poll(&failure);
        if (status == engine::WorldSessionStatus::LoadingWorld) return application::StateTickStatus::Success();
        if (status == engine::WorldSessionStatus::Failed)
            return application::StateTickStatus::Failure(
                failure.message != nullptr ? failure.message : "editor world session loading failed");
        if (status != engine::WorldSessionStatus::Running)
            return application::StateTickStatus::Failure("editor world session entered an invalid startup state");
        if (!context.RequestTransition(RunningStateId))
            return application::StateTickStatus::Failure("editor running-state transition failed");
        m_transitionRequested = true;
        return application::StateTickStatus::Success();
    }

    application::StateOperationStatus StartupSessionState::OnExit(application::StateContext& context) noexcept
    {
        if (m_transitionRequested) return application::StateOperationStatus::Complete();
        return StopSession(context);
    }

    void RunningState::SetProjectWindowTitle(const containers::StringView displayName, const containers::StringView technicalName) noexcept
    {
        constexpr containers::StringView suffix(" - Vanguard Editor");
        const containers::StringView selectedName =
            displayName.Length() + suffix.Length() < window::MaximumWindowTitleBytes ? displayName : technicalName;
        m_windowTitle.Clear();
        m_windowTitle.Append(selectedName).Append(suffix);
    }

    application::StateOperationStatus RunningState::OnEnter(application::StateContext& context) noexcept
    {
        engine::WindowService* const windows = engine::FindWindowService(context.Services());
        if (windows == nullptr || !windows->PrimaryWindow().IsValid())
            return application::StateOperationStatus::Failure("Window service has no primary editor window");
        window::Failure windowFailure;
        if (!windows->Manager().SetTitle(windows->PrimaryWindow(), m_windowTitle.AsChar(), &windowFailure))
            return application::StateOperationStatus::Failure(windowFailure.message != nullptr ? windowFailure.message
                                                                                               : "editor window title update failed");

        engine::FramePipelineService* const framePipeline = engine::FindFramePipelineService(context.Services());
        if (framePipeline == nullptr)
            return application::StateOperationStatus::Failure("Frame Pipeline service is unavailable");
        engine::FrameFailure failure;
        if (!framePipeline->Compile(&failure))
        {
            VG_LOG_ERROR(diagnostics::Category::Engine,
                         "frame pipeline compilation failed: code=%u phase=%u participant=%llu related=%llu name=%s message=%s",
                         static_cast<u32>(failure.code), static_cast<u32>(failure.phase), failure.participant, failure.relatedParticipant,
                         failure.participantName != nullptr ? failure.participantName : "<none>",
                         failure.message != nullptr ? failure.message : "<none>");
            return application::StateOperationStatus::Failure(failure.message != nullptr ? failure.message
                                                                                         : "frame pipeline compilation failed");
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
        if (framePipeline == nullptr)
            return application::StateTickStatus::Failure("Frame Pipeline service is unavailable");
        engine::FrameFailure failure;
        if (!framePipeline->RunFrame(&failure))
        {
            VG_LOG_ERROR(diagnostics::Category::Engine,
                         "editor frame failed: frame=%llu code=%u phase=%u participant=%llu related=%llu name=%s message=%s", failure.frame,
                         static_cast<u32>(failure.code), static_cast<u32>(failure.phase), failure.participant, failure.relatedParticipant,
                         failure.participantName != nullptr ? failure.participantName : "<none>",
                         failure.message != nullptr ? failure.message : "<none>");
            return application::StateTickStatus::Failure(failure.message != nullptr ? failure.message : "editor frame failed");
        }
        return application::StateTickStatus::Success();
    }

    application::StateOperationStatus RunningState::OnExit(application::StateContext& context) noexcept
    {
        return StopSession(context);
    }

    ApplicationTraits EditorApplication::GetTraits() const noexcept
    {
        return {"editor",
                application::ApplicationProfile::Runtime | application::ApplicationProfile::Editor | application::ApplicationProfile::Tool,
                1200, nullptr};
    }

    application::CompositionStatus EditorApplication::Compose(const application::ApplicationStartupContext& startup,
                                                              application::EngineHost& services,
                                                              application::ApplicationStateMachine& states) noexcept
    {
        const char* const projectArgument = FindProjectArgument(startup.commandLine);
        if (projectArgument == nullptr)
            return application::CompositionStatus::Failure("editor launch requires an explicit .vproject path");
        const filesystem::AbsolutePath projectFile = ResolveProjectFile(projectArgument);
        const filesystem::AbsolutePath projectRoot = filesystem::paths::ParentAbsolutePath(projectFile);
        if (projectRoot.Empty())
            return application::CompositionStatus::Failure("editor .vproject path has no valid project directory");

        auto projectReader = filesystem::RawFileReader::Create(projectFile);
        if (!projectReader)
            return application::CompositionStatus::Failure("editor could not open the requested .vproject document");
        projects::ProjectDescriptor project;
        projects::Diagnostic projectDiagnostic;
        if (projects::Read(*projectReader, project, &projectDiagnostic) != projects::Result::Success)
        {
            VG_LOG_ERROR(diagnostics::Category::Engine, "editor project validation failed: result=%u line=%u column=%u field=%s message=%s",
                         static_cast<u32>(projectDiagnostic.result), projectDiagnostic.line, projectDiagnostic.column,
                         projectDiagnostic.field != nullptr ? projectDiagnostic.field : "<none>",
                         projectDiagnostic.message != nullptr ? projectDiagnostic.message : "<none>");
            return application::CompositionStatus::Failure(projectDiagnostic.message != nullptr ? projectDiagnostic.message
                                                                                                : "editor project validation failed");
        }

        m_filesystemConfig = {projectRoot, projectRoot, projectRoot.AddDirPath(project.derivedData)};
        m_workspaceConfig.projectFile = projectFile;
        m_runningState.SetProjectWindowTitle(project.name, project.technicalName);
        const bool validateBootstrap = startup.commandLine.HasArgument("--validate-bootstrap");
        const bool validateWorkspace = startup.commandLine.HasArgument("--validate-workspace");
        m_runningState.ExitAfterFirstTick(validateBootstrap || validateWorkspace);
        application::HostFailure failure;
        if (!engine::RegisterEngineModule(services, &failure) ||
            !services.RegisterModule({EditorModuleId, "editor", 1}, &failure) ||
            !engine::RegisterIoService(services, &failure) ||
            !engine::RegisterFilesystemService(services, m_filesystemConfig, &failure) ||
            !RegisterProjectWorkspaceService(services, m_workspaceConfig, &failure) ||
            !engine::RegisterJobsService(services, &failure) || !engine::RegisterFramePipelineService(services, &failure) ||
            !engine::RegisterReflectionService(services, &failure) ||
            !engine::RegisterWindowService(services, startup.platform, &failure) ||
            !engine::RegisterInputService(services, startup.platform->InputBackend(), &failure) ||
            !engine::RegisterGameInputService(services, &failure) || !engine::RegisterResourcesService(services, &failure) ||
            !engine::RegisterResourceStreamingService(services, &failure) || !engine::RegisterWorldService(services, &failure) ||
            !engine::RegisterGameWorldService(services, &failure) || !engine::RegisterStreamingObserverService(services, &failure) ||
            !engine::RegisterWorldSessionService(services, &failure))
            return application::CompositionStatus::Failure(failure.message != nullptr ? failure.message
                                                                                      : "editor engine service registration failed");
        if (!states.RegisterState({StartupSessionStateId, "startupSession", &m_startupSessionState}) ||
            !states.RegisterState({RunningStateId, "running", &m_runningState}) ||
            !states.SetInitialState(validateBootstrap ? RunningStateId : StartupSessionStateId))
            return application::CompositionStatus::Failure("editor application state registration failed");
        return application::CompositionStatus::Success();
    }
} // namespace vanguard::editor
