#include <vanguard/editor/editor_application.hpp>
#include <vanguard/editor/editor_activity.hpp>
#include <vanguard/editor/editor_commands.hpp>
#include <vanguard/editor/editor_file_dialog.hpp>
#include <vanguard/editor/editor_scene_output_service.hpp>
#include <vanguard/editor/editor_scene_view_service.hpp>
#include <vanguard/editor/editor_static_extensions.hpp>
#include <vanguard/editor/editor_user_state_service.hpp>
#include <vanguard/editor/editor_ui.hpp>

#include <vanguard/diagnostics/diagnostics.hpp>
#include <vanguard/system/assert.hpp>
#include <vanguard/engine/engine_services.hpp>
#include <vanguard/engine/frame_pipeline_service.hpp>
#include <vanguard/engine/window_service.hpp>
#include <vanguard/engine/world_session_service.hpp>
#include <vanguard/projects/project_workspace.hpp>
#include <vanguard/resources/resources.hpp>
#include <vanguard/rhi/d3d12/backend.hpp>
#include <vanguard/world/worlds.hpp>

#include <cstring>

namespace vanguard::editor
{
    namespace
    {
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
            engine::WorldSessionService* const session = engine::FindWorldSessionService(context.GetServices());
            EditorSceneViewService* const sceneView = FindEditorSceneViewService(context.GetServices());
            if (session == nullptr || sceneView == nullptr)
                return application::StateOperationStatus::Failure("editor session-stop dependencies are unavailable");
            if (!sceneView->PrepareForSessionStop())
                return application::StateOperationStatus::Failure("editor Scene view could not release camera ownership before session stop");
            if (session->GetStatus() == engine::WorldSessionStatus::Idle)
                return application::StateOperationStatus::Complete();
            if (session->GetStatus() == engine::WorldSessionStatus::Failed)
            {
                const engine::WorldSessionFailure& reported = session->GetLastFailure();
                return application::StateOperationStatus::Failure(reported.message != nullptr ? reported.message : "editor world session stop failed");
            }

            engine::WorldSessionFailure failure;
            const bool stopRequested = session->RequestStop(&failure);
            if (!stopRequested)
                return application::StateOperationStatus::Failure(failure.message != nullptr ? failure.message : "editor world session stop request failed");
            const engine::WorldSessionStatus status = session->Poll(&failure);
            if (status == engine::WorldSessionStatus::Idle)
                return application::StateOperationStatus::Complete();
            if (status == engine::WorldSessionStatus::Failed)
                return application::StateOperationStatus::Failure(failure.message != nullptr ? failure.message : "editor world session stop failed");
            return application::StateOperationStatus::Pending();
        }
    } // namespace

    application::StateOperationStatus StartupSessionState::OnEnter(application::StateContext& context) noexcept
    {
        ProjectWorkspaceService* const workspace = FindProjectWorkspaceService(context.GetServices());
        engine::WorldSessionService* const session = engine::FindWorldSessionService(context.GetServices());
        if (workspace == nullptr || session == nullptr)
            return application::StateOperationStatus::Failure("editor startup dependencies are unavailable");

        m_worldConfigured = !workspace->GetProject().editorWorld.Empty();
        m_transitionRequested = false;
        if (!m_worldConfigured)
            return application::StateOperationStatus::Complete();

        // The project stores the logical resource identity used by both loose cooked artifacts and VPAK indexes.
        const resources::ResourcePath worldPath = resources::ResourcePath::FromString(workspace->GetProject().editorWorld);
        if (!worldPath.IsValid())
            return application::StateOperationStatus::Failure("startup.editorWorld is not a valid resource identity");

        engine::WorldSessionStartRequest request;
        request.world = resources::ResourceReference(worldPath, world::WorldResourceType);
        request.inputMode = engine::WorldSessionInputMode::None;
        // Editor navigation remains an explicit live-camera owner; do not silently
        // turn the authored gameplay camera into editor streaming authority.
        request.cameraSource = engine::WorldSessionCameraSource::None;
        engine::WorldSessionFailure failure;
        const bool sessionStarted = session->Begin(request, &failure);
        if (!sessionStarted)
        {
            VG_LOG_ERROR(diagnostics::Category::Resources,
                         "editor world session start failed: world=%s code=%u resourceFailure=%u message=%s",
                         workspace->GetProject().editorWorld.AsChar(), static_cast<u32>(failure.code), static_cast<u32>(failure.resourceFailure),
                         failure.message != nullptr ? failure.message : "<none>");
            return application::StateOperationStatus::Failure(failure.message != nullptr ? failure.message : "editor world session start failed");
        }
        return application::StateOperationStatus::Complete();
    }

    application::StateTickStatus StartupSessionState::OnTick(application::StateContext& context) noexcept
    {
        if (!m_worldConfigured)
        {
            if (!context.RequestTransition(EditorRunningStateId))
                return application::StateTickStatus::Failure("editor running-state transition failed");
            m_transitionRequested = true;
            return application::StateTickStatus::Success();
        }

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
            return application::StateTickStatus::Failure(reported.message != nullptr ? reported.message : "editor world session loading failed");
        }
        if (status == engine::WorldSessionStatus::Failed)
        {
            const engine::WorldSessionFailure& reported = session->GetLastFailure();
            return application::StateTickStatus::Failure(reported.message != nullptr ? reported.message : "editor world session loading failed");
        }
        if (status != engine::WorldSessionStatus::Running)
            return application::StateTickStatus::Failure("editor world session entered an invalid startup state");
        if (!context.RequestTransition(EditorRunningStateId))
            return application::StateTickStatus::Failure("editor running-state transition failed");
        m_transitionRequested = true;
        return application::StateTickStatus::Success();
    }

    application::StateOperationStatus StartupSessionState::OnExit(application::StateContext& context) noexcept
    {
        if (m_transitionRequested)
            return application::StateOperationStatus::Complete();
        return StopSession(context);
    }

    void RunningState::SetProjectWindowTitle(const containers::StringView displayName, const containers::StringView technicalName) noexcept
    {
        constexpr containers::StringView suffix(" - Vanguard Editor");
        const containers::StringView selectedName = displayName.Length() + suffix.Length() < window::MaximumWindowTitleBytes ? displayName : technicalName;
        m_windowTitle.Clear();
        m_windowTitle.Append(selectedName).Append(suffix);
    }

    application::StateOperationStatus RunningState::OnEnter(application::StateContext& context) noexcept
    {
        engine::WindowService* const windows = engine::FindWindowService(context.GetServices());
        if (windows == nullptr || !windows->GetPrimaryWindow().IsValid())
            return application::StateOperationStatus::Failure("Window service has no primary editor window");
        window::Failure windowFailure;
        if (!windows->GetManager().SetTitle(windows->GetPrimaryWindow(), m_windowTitle.AsChar(), &windowFailure))
            return application::StateOperationStatus::Failure(windowFailure.message != nullptr ? windowFailure.message : "editor window title update failed");

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
        engine::FramePipelineService* const framePipeline = engine::FindFramePipelineService(context.GetServices());
        if (framePipeline == nullptr)
            return application::StateTickStatus::Failure("Frame Pipeline service is unavailable");
        engine::FrameFailure failure;
        if (!framePipeline->RunFrame(&failure))
        {
            VG_LOG_ERROR(diagnostics::Category::Engine, "editor frame failed: frame=%llu code=%u phase=%u participant=%llu related=%llu name=%s message=%s",
                         failure.frame, static_cast<u32>(failure.code), static_cast<u32>(failure.phase), failure.participant, failure.relatedParticipant,
                         failure.participantName != nullptr ? failure.participantName : "<none>", failure.message != nullptr ? failure.message : "<none>");
            return application::StateTickStatus::Failure(failure.message != nullptr ? failure.message : "editor frame failed");
        }
        if (m_frameDiagnosticsEnabled)
        {
            const engine::FramePipelineStats stats = framePipeline->GetStats();
            if (stats.lastRawDeltaSeconds >= 0.040f || stats.lastExecutionSeconds >= 0.040f)
            {
                struct SlowestParticipant final
                {
                    engine::FrameParticipantId id = engine::InvalidFrameParticipantId;
                    const char* name = nullptr;
                    engine::FramePhase phase = engine::FramePhase::PlatformEvents;
                    f32 seconds = 0.0f;
                    u32 invocations = 0;
                } slowest;
                framePipeline->VisitParticipantStats(
                    [](const engine::FrameParticipantStats& participant, void* const userData) noexcept
                    {
                        auto* const result = static_cast<SlowestParticipant*>(userData);
                        if (result != nullptr && participant.lastFrameSeconds > result->seconds)
                        {
                            result->id = participant.id;
                            result->name = participant.name;
                            result->phase = participant.phase;
                            result->seconds = participant.lastFrameSeconds;
                            result->invocations = participant.lastFrameInvocations;
                        }
                    },
                    &slowest);
                VG_LOG_WARNING(
                    diagnostics::Category::Engine,
                    "editor main-thread stall: frame=%llu rawDeltaMs=%.3f executionMs=%.3f phasesMs=[%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f] slowestId=%llu slowestName=%s slowestPhase=%u slowestMs=%.3f slowestInvocations=%u",
                    static_cast<unsigned long long>(stats.frames), static_cast<double>(stats.lastRawDeltaSeconds) * 1000.0,
                    static_cast<double>(stats.lastExecutionSeconds) * 1000.0,
                    static_cast<double>(stats.lastPhaseSeconds[0]) * 1000.0, static_cast<double>(stats.lastPhaseSeconds[1]) * 1000.0,
                    static_cast<double>(stats.lastPhaseSeconds[2]) * 1000.0, static_cast<double>(stats.lastPhaseSeconds[3]) * 1000.0,
                    static_cast<double>(stats.lastPhaseSeconds[4]) * 1000.0, static_cast<double>(stats.lastPhaseSeconds[5]) * 1000.0,
                    static_cast<double>(stats.lastPhaseSeconds[6]) * 1000.0, static_cast<double>(stats.lastPhaseSeconds[7]) * 1000.0,
                    static_cast<double>(stats.lastPhaseSeconds[8]) * 1000.0, static_cast<double>(stats.lastPhaseSeconds[9]) * 1000.0,
                    static_cast<double>(stats.lastPhaseSeconds[10]) * 1000.0, static_cast<double>(stats.lastPhaseSeconds[11]) * 1000.0,
                    static_cast<unsigned long long>(slowest.id), slowest.name != nullptr ? slowest.name : "<none>",
                    static_cast<u32>(slowest.phase), static_cast<double>(slowest.seconds) * 1000.0, slowest.invocations);
            }
        }
        return application::StateTickStatus::Success();
    }

    application::StateOperationStatus RunningState::OnExit(application::StateContext& context) noexcept
    {
        return StopSession(context);
    }

    ApplicationTraits EditorApplication::GetTraits() const noexcept
    {
        return {"editor", application::ApplicationProfile::Runtime | application::ApplicationProfile::Editor | application::ApplicationProfile::Tool, 1200,
                nullptr};
    }

    application::CompositionStatus EditorApplication::Compose(const application::ApplicationStartupContext& startup, application::EngineHost& services,
                                                              application::ApplicationStateMachine& states) noexcept
    {
        const char* const projectArgument = FindProjectArgument(startup.commandLine);
        if (projectArgument == nullptr)
            return application::CompositionStatus::Failure("editor launch requires an explicit .vproject path");
        const filesystem::AbsolutePath projectFile = ResolveProjectFile(projectArgument);
        const filesystem::AbsolutePath projectRoot = filesystem::paths::ParentAbsolutePath(projectFile);
        if (projectRoot.Empty())
            return application::CompositionStatus::Failure("editor .vproject path has no valid project directory");

        const char* const executablePath = startup.platform != nullptr ? startup.platform->GetExecutablePath() : nullptr;
        if (executablePath == nullptr || executablePath[0] == '\0' ||
            !filesystem::AbsolutePath::IsValidPath(containers::StringView(executablePath)))
            return application::CompositionStatus::Failure("editor could not resolve its process image path");
        const filesystem::AbsolutePath executableFile =
            filesystem::AbsolutePath::CreateFilePath(containers::StringView(executablePath));
        const filesystem::AbsolutePath executableDirectory = filesystem::paths::ParentAbsolutePath(executableFile);
        if (executableDirectory.Empty())
            return application::CompositionStatus::Failure("editor process image has no valid directory");
        m_editorResourceConfig.root = executableDirectory.AddDirPath("Editor");

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

        projects::ProjectWorkspace workspace;
        if (projects::ResolveWorkspace(projectFile, project, workspace, &projectDiagnostic) != projects::Result::Success)
            return application::CompositionStatus::Failure(projectDiagnostic.message != nullptr ? projectDiagnostic.message : "editor workspace resolution failed");
        m_filesystemConfig = {workspace.root, workspace.root, workspace.derivedData};
        const auto logDirectory = workspace.saved.AddDirPath("Logs");
        const bool logDirectoryCreated = filesystem::SystemIO::CreatePath(logDirectory.AsChar());
        if (!logDirectoryCreated)
            return application::CompositionStatus::Failure("editor could not create project Saved log directory");
        const auto logPath = logDirectory.AddFilePath("VanguardEditor.log");
        const bool logOpened = diagnostics::OpenFileSink(logPath.AsChar());
        if (!logOpened)
            return application::CompositionStatus::Failure("editor could not open project log file");
        m_streamingConfig = {};
        // Packaged fallback is explicit. Project-derived providers publish their
        // committed locations through ResourceStreamer; never infer DDC filenames.
        for (i32 index = 1; index < startup.commandLine.argumentCount; ++index)
        {
            const char* const argument = startup.commandLine[index];
            if (argument == nullptr || std::strcmp(argument, "--package-root") != 0)
                continue;
            const char* const root = startup.commandLine[++index];
            if (root == nullptr || root[0] == '\0' || root[0] == '-' || !m_streamingConfig.packageDirectory.Empty())
                return application::CompositionStatus::Failure("--package-root requires one explicit package directory");
            const containers::StringView path(root);
            m_streamingConfig.packageDirectory = filesystem::AbsolutePath::IsValidPath(path)
                ? filesystem::AbsolutePath::CreateDirPath(path)
                : filesystem::paths::GetCurrentWorkingDirectory().AddDirPath(path);
            if (m_streamingConfig.packageDirectory.Empty())
                return application::CompositionStatus::Failure("--package-root is not a valid directory");
        }
        m_renderingConfig = {};
        m_renderingConfig.deviceMode = engine::RenderingDeviceMode::Required;
        // A supplied package root owns renderer-catalog discovery through its DATA boot record.
        // Project/loose rendering resources never gain a guessed catalog path here.
        m_renderingConfig.usePackageRendererBootstrap = !m_streamingConfig.packageDirectory.Empty();
        m_renderingConfig.rendererOutputAttachments = detail::MakeEditorSceneOutputAttachments();
        m_renderingConfig.enableFrameDiagnostics = startup.commandLine.HasArgument("--render-diagnostics");
        m_runningState.SetFrameDiagnosticsEnabled(m_renderingConfig.enableFrameDiagnostics);
        m_renderingConfig.backendFactory = rhi::d3d12::GetBackendFactory();
        m_renderingConfig.device.editor = true;
        m_windowConfig = {};
        m_windowConfig.platform = startup.platform;
        m_windowConfig.primaryLogicalExtent = {1920, 1080};
        m_workspaceConfig.projectFile = projectFile;
        m_workspaceConfig.project = project;
        m_shellConfig.extensions = GetCompiledEditorExtensions();
        m_userStateConfig.forceDefaultLayout = startup.commandLine.HasArgument("--force-default-layout");
        if (m_shellConfig.extensions.Empty())
            return application::CompositionStatus::Failure("editor compiled extension catalog is empty");
        m_runningState.SetProjectWindowTitle(project.name, project.technicalName);
        application::HostFailure failure;
        if (!engine::RegisterEngineModule(services, &failure) || !services.RegisterModule({EditorModuleId, "editor", 1}, &failure) ||
            !engine::RegisterIoService(services, &failure) || !engine::RegisterFilesystemService(services, m_filesystemConfig, &failure) ||
            !RegisterProjectWorkspaceService(services, m_workspaceConfig, &failure) || !engine::RegisterJobsService(services, &failure) ||
            !engine::RegisterFramePipelineService(services, &failure) || !engine::RegisterReflectionService(services, &failure) ||
            !engine::RegisterRenderingService(services, m_renderingConfig, &failure) || !engine::RegisterWindowService(services, m_windowConfig, &failure) ||
            !engine::RegisterInputService(services, startup.platform->GetInputBackend(), &failure) || !engine::RegisterGameInputService(services, &failure) ||
            !engine::RegisterResourcesService(services, &failure) || !engine::RegisterResourceStreamingService(services, m_streamingConfig, &failure) ||
            !engine::RegisterWorldService(services, &failure) || !engine::RegisterGameWorldService(services, &failure) ||
            !engine::RegisterStreamingObserverService(services, &failure) || !engine::RegisterWorldSessionService(services, &failure) ||
            !RegisterEditorResourceService(services, m_editorResourceConfig, &failure) ||
            !RegisterEditorFileDialogService(services, startup.platform, &failure) ||
            !RegisterEditorActivityService(services, &failure) || !RegisterEditorUiService(services, &failure) ||
            !RegisterEditorSceneOutputService(services, &failure) || !RegisterEditorSceneViewService(services, &failure) ||
            !RegisterEditorCommandService(services, &failure) ||
            !RegisterEditorShellService(services, m_shellConfig, &failure) ||
            !RegisterEditorUserStateService(services, m_userStateConfig, &failure))
            return application::CompositionStatus::Failure(failure.message != nullptr ? failure.message : "editor engine service registration failed");
        if (!states.RegisterState({EditorStartupSessionStateId, "startupSession", &m_startupSessionState}) ||
            !states.RegisterState({EditorRunningStateId, "running", &m_runningState}) ||
            !states.SetInitialState(EditorStartupSessionStateId))
            return application::CompositionStatus::Failure("editor application state registration failed");
        return application::CompositionStatus::Success();
    }
} // namespace vanguard::editor
