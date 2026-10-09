// TEST SUPPORT ONLY: lifecycle driver layered on the real editor composition.

#include <editor_application_lifecycle_integration_test_application.hpp>
#include <native_ui_integration_test_extension.hpp>

#include <vanguard/diagnostics/diagnostics.hpp>
#include <vanguard/editor/editor_activity.hpp>
#include <vanguard/editor/editor_commands.hpp>
#include <vanguard/editor/editor_project_service.hpp>
#include <vanguard/editor/editor_service_ids.hpp>
#include <vanguard/editor/editor_scene_output_service.hpp>
#include <vanguard/editor/editor_shell_service.hpp>
#include <vanguard/editor/editor_user_state_service.hpp>
#include <vanguard/editor/editor_ui.hpp>
#include <vanguard/editor/editor_ui_imgui.hpp>
#include <vanguard/editor/editor_ui_render_data.hpp>
#include <vanguard/engine/frame_pipeline_service.hpp>
#include <vanguard/engine/rendering_service.hpp>
#include <vanguard/engine/world_session_service.hpp>
#include <vanguard/filesystem/filesystem.hpp>
#include <vanguard/memory/memory.hpp>

#include <cstring>
#include <new>

namespace
{
    namespace app = vanguard::application;
    namespace diagnostics = vanguard::diagnostics;
    namespace editor = vanguard::editor;
    namespace engine = vanguard::engine;
    namespace filesystem = vanguard::filesystem;
    namespace rendering = vanguard::rendering;
    namespace rhi = vanguard::rhi;

    inline constexpr app::ServiceId EditorApplicationLifecycleIntegrationTestServiceId = 0x65646c6966650101ull;
    inline constexpr engine::FrameParticipantId EditorApplicationLifecycleIntegrationTestPreUiParticipantId = 0x65646c6966650181ull;
    inline constexpr engine::FrameParticipantId EditorApplicationLifecycleIntegrationTestFrameParticipantId = 0x65646c6966650201ull;
    inline constexpr editor::EditorPanelTypeId SceneOutputPanelTypeId = 0x73636e6f75747001ull;
    inline constexpr editor::EditorPanelInstanceId SceneOutputPanelInstanceId = 0x73636e6f75746901ull;
    inline constexpr vanguard::u32 MaximumSceneOutputFrames = 600;
    inline constexpr editor::EditorExtensionId DemonstrationExtensionId{0x6564333764656d01ull};
    inline constexpr editor::EditorPanelTypeId DemonstrationPanelTypeId = 0x65643337646d7001ull;
    inline constexpr editor::EditorPanelInstanceId DemonstrationPanelInstanceId = 1u;
    inline constexpr editor::EditorCommandId DemonstrationCommandId{0x65643337636d6401ull};
    inline constexpr editor::EditorActivitySourceId DemonstrationActivitySourceId = 0x6564333761637401ull;
    inline constexpr editor::EditorActivityItemId DemonstrationActivityItemId = 1u;
    inline constexpr editor::EditorPreferenceProviderId DemonstrationPreferenceProviderId = 0x6564333770726601ull;
    inline constexpr editor::EditorPreferenceFieldId DemonstrationDetailsPreferenceFieldId = 0x6564333770726602ull;

    void LogUserStateLifecycleEvent(const app::LifecycleEvent& event, void*) noexcept
    {
        if (event.service != editor::EditorUserStateServiceId &&
            event.service != editor::EditorShellServiceId &&
            event.service != editor::EditorUiServiceId &&
            event.service != EditorApplicationLifecycleIntegrationTestServiceId)
            return;
        if (event.stage != app::LifecycleStage::Start &&
            event.stage != app::LifecycleStage::Quiesce &&
            event.stage != app::LifecycleStage::Shutdown)
            return;
        VG_LOG_INFO(
            diagnostics::Category::Engine,
            "Editor persistence lifecycle event: sequence=%llu service=%llu stage=%u state=%u result=%u",
            static_cast<unsigned long long>(event.sequence),
            static_cast<unsigned long long>(event.service), static_cast<vanguard::u32>(event.stage),
            static_cast<vanguard::u32>(event.state), static_cast<vanguard::u32>(event.result));
        diagnostics::Flush(diagnostics::FlushMode::Asynchronous);
    }

    class EditorApplicationLifecycleIntegrationTestService final : public app::Service
    {
    public:
        explicit EditorApplicationLifecycleIntegrationTestService(
            editor::testing::EditorApplicationLifecycleIntegrationTestServiceConfig& config) noexcept
            : m_config(&config)
        {
        }

    protected:
        app::LifecycleStatus OnInitialize(app::ServiceContext& context) noexcept override
        {
            engine::FramePipelineService* const frames = engine::FindFramePipelineService(context);
            if (m_config == nullptr || m_config->states == nullptr || frames == nullptr)
                return app::LifecycleStatus::Failure("editor lifecycle integration-test dependencies are unavailable");

            if (m_config->userStateMissingPath || m_config->userStateUnwritablePath)
            {
                m_ui = editor::FindEditorUiService(context);
                m_workspace = editor::FindProjectWorkspaceService(context);
                if (m_config->services == nullptr || m_ui == nullptr || m_workspace == nullptr)
                    return app::LifecycleStatus::Failure(
                        "editor user-state lifecycle integration-test dependencies are unavailable");
            }

            if (m_config->demonstrationExtensionPath)
            {
                m_ui = editor::FindEditorUiService(context);
                m_commands = editor::FindEditorCommandService(context);
                m_activity = editor::FindEditorActivityService(context);
                m_shell = editor::FindEditorShellService(context);
                if (m_ui == nullptr || m_commands == nullptr || m_activity == nullptr || m_shell == nullptr)
                    return app::LifecycleStatus::Failure(
                        "editor demonstration-extension integration-test dependencies are unavailable");
            }

            if (m_config->sceneOutputPath)
            {
                m_ui = editor::FindEditorUiService(context);
                m_sceneOutput = editor::FindEditorSceneOutputService(context);
                m_rendering = engine::FindRenderingService(context);
                m_session = engine::FindWorldSessionService(context);
                if (m_ui == nullptr || m_sceneOutput == nullptr || m_rendering == nullptr || m_session == nullptr)
                    return app::LifecycleStatus::Failure("editor scene-output integration-test dependencies are unavailable");

                constexpr engine::FrameParticipantId preUiDependencies[]{editor::EditorSceneFrameParticipantId};
                engine::FrameParticipantDescriptor preUiDescriptor;
                preUiDescriptor.id = EditorApplicationLifecycleIntegrationTestPreUiParticipantId;
                preUiDescriptor.name = "editorApplicationLifecycleIntegrationTest.preUi";
                preUiDescriptor.phase = engine::FramePhase::Render;
                preUiDescriptor.profiles = app::ApplicationProfile::Editor | app::ApplicationProfile::Tool;
                preUiDescriptor.affinity = app::ThreadAffinity::MainThread;
                preUiDescriptor.after = {preUiDependencies, 1};
                preUiDescriptor.execute = ExecutePreUiFrame;
                preUiDescriptor.userData = this;
                engine::FrameFailure preUiFailure;
                if (!frames->RegisterParticipant(preUiDescriptor, &preUiFailure))
                    return app::LifecycleStatus::Failure(preUiFailure.message != nullptr
                                                             ? preUiFailure.message
                                                             : "editor scene-output integration-test pre-UI registration failed");
            }

            constexpr engine::FrameParticipantId dependencies[]{
                editor::EditorUiRenderParticipantId, editor::EditorUserStateFrameParticipantId};
            engine::FrameParticipantDescriptor descriptor;
            descriptor.id = EditorApplicationLifecycleIntegrationTestFrameParticipantId;
            descriptor.name = "editorApplicationLifecycleIntegrationTest";
            descriptor.phase = engine::FramePhase::EndFrame;
            descriptor.profiles = app::ApplicationProfile::Editor | app::ApplicationProfile::Tool;
            descriptor.affinity = app::ThreadAffinity::MainThread;
            descriptor.after = {dependencies,
                                m_config->userStateMissingPath || m_config->userStateUnwritablePath ||
                                        m_config->demonstrationExtensionPath
                                    ? 2u
                                    : 1u};
            descriptor.execute = ExecuteFrame;
            descriptor.userData = this;
            engine::FrameFailure failure;
            if (!frames->RegisterParticipant(descriptor, &failure))
                return app::LifecycleStatus::Failure(failure.message != nullptr
                                                         ? failure.message
                                                         : "editor lifecycle integration-test frame registration failed");
            return app::LifecycleStatus::Success();
        }

        app::LifecycleStatus OnStart(app::ServiceContext&) noexcept override
        {
            if (m_config == nullptr || !m_config->sceneOutputPath)
                return app::LifecycleStatus::Success();
            editor::EditorPanelTypeDesc type;
            type.id = SceneOutputPanelTypeId;
            type.name = "Scene output integration test";
            type.create = CreatePanelState;
            type.destroy = DestroyPanelState;
            type.draw = DrawPanel;
            type.factoryData = this;
            type.policy = editor::EditorPanelTypePolicy::Singleton;
            type.defaultInstance = SceneOutputPanelInstanceId;
            editor::EditorUiFailure failure;
            if (!m_ui->GetPanels().RegisterType(type, m_panelType, &failure) ||
                !m_ui->GetPanels().CreatePanel({SceneOutputPanelInstanceId, SceneOutputPanelTypeId,
                                                "Scene output integration test", true},
                                               m_panel, &failure))
                return app::LifecycleStatus::Failure(failure.message != nullptr ? failure.message
                                                                                : "editor scene-output test panel startup failed");
            return app::LifecycleStatus::Success();
        }

        app::LifecycleStatus OnQuiesce(app::ServiceContext&) noexcept override
        {
            if (m_config != nullptr &&
                (m_config->userStateMissingPath || m_config->userStateUnwritablePath))
            {
                if (m_userState == nullptr && m_config->services != nullptr)
                    m_userState = editor::FindEditorUserStateService(*m_config->services);
                if (m_userState == nullptr)
                    return app::LifecycleStatus::Failure(
                        "editor user-state lifecycle fixture never observed UserState after host startup");
                const editor::EditorUserStateStatus status = m_userState->GetStatus();
                if (status.state != editor::EditorUserStateServiceState::Quiesced)
                    return app::LifecycleStatus::Failure(
                        "editor user-state did not quiesce before lifecycle fixture");
                if (m_config->userStateMissingPath &&
                    status.save != editor::EditorUserStateSaveResult::Published)
                    return app::LifecycleStatus::Failure(
                        "editor user-state missing-owner restart did not publish final state");
                if (m_config->userStateUnwritablePath &&
                    (status.writesInhibited ||
                     status.load != editor::EditorUserStateLoadResult::Missing ||
                     status.save != editor::EditorUserStateSaveResult::StageOpenFailed))
                    return app::LifecycleStatus::Failure(
                        "editor user-state unwritable fixture did not preserve a soft final publication failure");
                VG_LOG_INFO(
                    diagnostics::Category::Engine,
                    "Editor user-state lifecycle summary: missing=%u unwritable=%u state=%u load=%u save=%u writesInhibited=%u placeholders=%u shortcutsOrphaned=%u preferencesApplied=%u dirty=%llu published=%llu",
                    vanguard::u32(m_config->userStateMissingPath),
                    vanguard::u32(m_config->userStateUnwritablePath), static_cast<vanguard::u32>(status.state),
                    static_cast<vanguard::u32>(status.load), static_cast<vanguard::u32>(status.save),
                    vanguard::u32(status.writesInhibited), status.panelPlaceholders,
                    status.shortcutsOrphaned, status.preferencesApplied,
                    static_cast<unsigned long long>(status.dirtyGeneration),
                    static_cast<unsigned long long>(status.publishedGeneration));
                diagnostics::Flush(diagnostics::FlushMode::Synchronous);
            }
            if (m_config != nullptr && m_config->sceneOutputPath)
            {
                if (m_sceneOutput != nullptr)
                    static_cast<void>(m_sceneOutput->ClearOutputDemand());
                editor::EditorUiFailure failure;
                if (m_ui != nullptr && m_panel.IsValid() && !m_ui->GetPanels().DestroyPanel(m_panel, &failure))
                    return app::LifecycleStatus::Failure(failure.message != nullptr ? failure.message
                                                                                    : "editor scene-output test panel cleanup failed");
                m_panel = {};
                if (m_ui != nullptr && m_panelType.IsValid() && !m_ui->GetPanels().UnregisterType(m_panelType, &failure))
                    return app::LifecycleStatus::Failure(failure.message != nullptr ? failure.message
                                                                                    : "editor scene-output test panel type cleanup failed");
                m_panelType = {};
            }
            return app::LifecycleStatus::Success();
        }

        app::LifecycleStatus OnShutdown(app::ServiceContext&) noexcept override
        {
            m_ui = nullptr;
            m_commands = nullptr;
            m_activity = nullptr;
            m_shell = nullptr;
            m_sceneOutput = nullptr;
            m_rendering = nullptr;
            m_session = nullptr;
            m_userState = nullptr;
            m_workspace = nullptr;
            return app::LifecycleStatus::Success();
        }

    private:
        static void* CreatePanelState(editor::EditorPanelInstanceId, void* const factoryData) noexcept
        {
            return factoryData;
        }

        static void DestroyPanelState(void*, void*) noexcept {}

        static void DrawPanel(editor::EditorPanelContext&, void* const panelState, void*) noexcept
        {
            auto* const service = static_cast<EditorApplicationLifecycleIntegrationTestService*>(panelState);
            if (service == nullptr || service->m_sceneOutput == nullptr)
                return;
            const editor::EditorSceneOutputInfo output = service->m_sceneOutput->GetPublishedOutput();
            if (!output.IsValid())
            {
                ImGui::TextUnformatted("Waiting for production scene output...");
                return;
            }
            service->m_lastDrawnTexture = output.texture;
            service->m_lastDrawnGeneration = output.generation;
            ImGui::Image(editor::detail::ToImGuiTexture(output.texture),
                         {static_cast<float>(output.extent.width), static_cast<float>(output.extent.height)});
        }

        struct PrimaryHost final
        {
            editor::EditorHostWindowInfo info;
            bool found = false;
        };

        static void CapturePrimaryHost(const editor::EditorHostWindowInfo& host, void* const userData) noexcept
        {
            auto* const primary = static_cast<PrimaryHost*>(userData);
            if (primary != nullptr && !primary->found && host.primary)
            {
                primary->info = host;
                primary->found = true;
            }
        }

        static engine::FrameParticipantStatus ExecutePreUiFrame(const engine::FrameContext& context, void* const userData) noexcept
        {
            auto* const service = static_cast<EditorApplicationLifecycleIntegrationTestService*>(userData);
            if (service == nullptr || service->m_config == nullptr || service->m_config->states == nullptr)
                return engine::FrameParticipantStatus::Failure("editor scene-output integration-test pre-UI state is unavailable");
            return service->CapturePreUiFrame(context);
        }

        static engine::FrameParticipantStatus ExecuteFrame(const engine::FrameContext& context, void* const userData) noexcept
        {
            auto* const service = static_cast<EditorApplicationLifecycleIntegrationTestService*>(userData);
            if (service == nullptr || service->m_config == nullptr || service->m_config->states == nullptr)
                return engine::FrameParticipantStatus::Failure("editor lifecycle integration-test state machine is unavailable");
            if (service->m_config->sceneOutputPath)
                return service->ExecuteSceneOutputFrame(context);
            if (service->m_config->demonstrationExtensionPath)
                return service->ExecuteDemonstrationExtensionFrame();
            if (service->m_config->userStateMissingPath)
                return service->ExecuteUserStateMissingFrame();
            if (service->m_config->userStateUnwritablePath)
                return service->ExecuteUserStateUnwritableFrame();
            if (service->m_config->states->GetCurrentState() != editor::EditorRunningStateId)
                return engine::FrameParticipantStatus::Failure("editor lifecycle integration test reached a frame outside the running state");
            app::StateMachineFailure failure;
            if (!service->m_config->states->RequestExit(0, &failure))
                return engine::FrameParticipantStatus::Failure(failure.message != nullptr
                                                                   ? failure.message
                                                                   : "editor lifecycle integration test could not request exit");
            return engine::FrameParticipantStatus::Success();
        }

        engine::FrameParticipantStatus ExecuteDemonstrationExtensionFrame() noexcept
        {
            if (m_config == nullptr || m_config->states == nullptr || m_shell == nullptr ||
                m_commands == nullptr || m_ui == nullptr || m_activity == nullptr)
                return engine::FrameParticipantStatus::Failure(
                    "editor demonstration-extension integration-test state is unavailable");
            if (m_config->states->GetCurrentState() != editor::EditorRunningStateId)
                return engine::FrameParticipantStatus::Failure(
                    "editor demonstration-extension integration test reached a frame outside the running state");

            const editor::EditorShellStatus shellStatus = m_shell->GetStatus();
            if (shellStatus.state != editor::EditorShellState::Active ||
                shellStatus.failure.code != editor::EditorExtensionFailureCode::None ||
                shellStatus.activeCount == 0)
                return engine::FrameParticipantStatus::Failure(
                    "editor demonstration extension did not activate through the production shell catalog");

            struct PanelObservation final
            {
                editor::EditorPanelHandle handle;
                bool found = false;
                bool open = false;
            } panel;
            m_ui->GetPanels().VisitPanels(
                [](const editor::EditorPanelInfo& info, void* const userData) noexcept
                {
                    auto* const observation = static_cast<PanelObservation*>(userData);
                    if (observation == nullptr || info.type != DemonstrationPanelTypeId ||
                        info.id != DemonstrationPanelInstanceId)
                        return;
                    observation->handle = info.handle;
                    observation->found = true;
                    observation->open = info.open;
                },
                &panel);
            if (!panel.found || !panel.handle.IsValid())
                return engine::FrameParticipantStatus::Failure(
                    "editor demonstration extension did not create its stable panel");

            editor::EditorCommandFailure commandFailure;
            editor::EditorShortcutBinding binding;
            if (!m_commands->GetShortcutBinding(editor::EditorWorkspaceCommandContextTypeId,
                                                DemonstrationCommandId, binding, &commandFailure) ||
                !binding.bound || binding.mode != editor::EditorShortcutOverrideMode::Default ||
                binding.chord.key != vanguard::input::Key::D ||
                binding.chord.modifiers !=
                    (editor::EditorShortcutModifiers::Control | editor::EditorShortcutModifiers::Shift))
                return engine::FrameParticipantStatus::Failure(
                    "editor demonstration extension shortcut binding is incorrect");

            if (!m_demonstrationInvoked)
            {
                editor::EditorUiFailure uiFailure;
                if (!m_ui->GetPanels().SetOpen(panel.handle, false, &uiFailure) ||
                    m_ui->GetPanels().IsOpen(panel.handle) ||
                    !m_commands->Invoke(DemonstrationCommandId, &commandFailure) ||
                    !m_ui->GetPanels().IsOpen(panel.handle))
                    return engine::FrameParticipantStatus::Failure(
                        "editor demonstration extension command did not reopen its owned panel");
                m_demonstrationInvoked = true;
                return engine::FrameParticipantStatus::Success();
            }

            editor::EditorActivitySourceSnapshot sources[16]{};
            editor::EditorActivityItem items[32]{};
            editor::EditorActivityCapture activityCapture;
            editor::EditorActivityFailure activityFailure;
            if (!m_activity->CaptureActivities({}, {sources, 16}, {items, 32}, activityCapture,
                                              &activityFailure))
                return engine::FrameParticipantStatus::Failure(
                    "editor demonstration extension Activity capture failed");
            bool foundStatus = false;
            for (vanguard::u32 index = 0; index < activityCapture.itemCount; ++index)
            {
                const editor::EditorActivityItem& item = items[index];
                if (item.key.source != DemonstrationActivitySourceId ||
                    item.key.item != DemonstrationActivityItemId)
                    continue;
                foundStatus = item.kind == editor::EditorActivityKind::Status &&
                              item.state == editor::EditorActivityState::Succeeded &&
                              std::strcmp(item.label, "Editor demo tool") == 0 &&
                              std::strstr(item.detail, "Command invocations: 1") != nullptr;
            }
            if (!foundStatus)
                return engine::FrameParticipantStatus::Failure(
                    "editor demonstration extension did not publish the expected Activity status row");

            auto& persistence =
                static_cast<editor::detail::EditorShellPersistencePrivateService&>(*m_shell);
            editor::EditorTypedPreferenceRecord preferences[16]{};
            vanguard::u32 preferenceCount = 0;
            editor::EditorPreferenceFailure preferenceFailure;
            if (!persistence.CaptureTypedPreferences(preferences, 16, preferenceCount,
                                                    &preferenceFailure))
                return engine::FrameParticipantStatus::Failure(
                    "editor demonstration extension typed-preference capture failed");
            editor::EditorTypedPreferenceRecord* demonstrationPreference = nullptr;
            for (vanguard::u32 index = 0; index < preferenceCount; ++index)
            {
                editor::EditorTypedPreferenceRecord& record = preferences[index];
                if (record.extension == DemonstrationExtensionId &&
                    record.providerId == DemonstrationPreferenceProviderId &&
                    record.fieldId == DemonstrationDetailsPreferenceFieldId)
                    demonstrationPreference = &record;
            }
            if (demonstrationPreference == nullptr ||
                demonstrationPreference->value.type != editor::EditorPreferenceValueType::Bool ||
                !demonstrationPreference->value.value.boolean)
                return engine::FrameParticipantStatus::Failure(
                    "editor demonstration extension typed preference was not captured at its default value");

            demonstrationPreference->value.value.boolean = false;
            editor::EditorTypedPreferenceItemResult preferenceResult{};
            if (!persistence.ValidateTypedPreferences({demonstrationPreference, 1},
                                                     {&preferenceResult, 1}, &preferenceFailure) ||
                preferenceResult != editor::EditorTypedPreferenceItemResult::Valid ||
                !persistence.ApplyTypedPreferences({demonstrationPreference, 1},
                                                  {&preferenceResult, 1}, &preferenceFailure))
                return engine::FrameParticipantStatus::Failure(
                    "editor demonstration extension typed preference did not validate/apply");

            preferenceCount = 0;
            if (!persistence.CaptureTypedPreferences(preferences, 16, preferenceCount,
                                                    &preferenceFailure))
                return engine::FrameParticipantStatus::Failure(
                    "editor demonstration extension typed-preference recapture failed");
            bool preferenceApplied = false;
            for (vanguard::u32 index = 0; index < preferenceCount; ++index)
            {
                const editor::EditorTypedPreferenceRecord& record = preferences[index];
                if (record.extension == DemonstrationExtensionId &&
                    record.providerId == DemonstrationPreferenceProviderId &&
                    record.fieldId == DemonstrationDetailsPreferenceFieldId &&
                    record.value.type == editor::EditorPreferenceValueType::Bool &&
                    !record.value.value.boolean)
                    preferenceApplied = true;
            }
            if (!preferenceApplied)
                return engine::FrameParticipantStatus::Failure(
                    "editor demonstration extension typed preference did not retain the applied value");

            app::StateMachineFailure failure;
            if (!m_config->states->RequestExit(0, &failure))
                return engine::FrameParticipantStatus::Failure(
                    failure.message != nullptr ? failure.message
                                               : "editor demonstration-extension integration test could not request exit");
            return engine::FrameParticipantStatus::Success();
        }

        engine::FrameParticipantStatus ExecuteUserStateMissingFrame() noexcept
        {
            if (m_userState == nullptr && m_config->services != nullptr)
                m_userState = editor::FindEditorUserStateService(*m_config->services);
            if (m_userState == nullptr)
                return engine::FrameParticipantStatus::Failure(
                    "editor user-state missing-owner restart could not resolve UserState after host startup");
            const editor::EditorUserStateStatus status = m_userState->GetStatus();
            if (status.state != editor::EditorUserStateServiceState::Running ||
                status.load != editor::EditorUserStateLoadResult::Restored ||
                status.panelPlaceholders == 0u || status.shortcutsOrphaned == 0u ||
                status.preferencesApplied < 3u)
                return engine::FrameParticipantStatus::Failure(
                    "editor user-state missing-owner restart did not restore the retained state");

            struct PanelObservation final
            {
                bool found = false;
                bool placeholder = false;
                bool open = false;
                bool titleMatches = false;
            } observation;
            m_ui->GetPanels().VisitPanels(
                [](const editor::EditorPanelInfo& panel, void* const userData) noexcept
                {
                    auto* const result = static_cast<PanelObservation*>(userData);
                    if (result == nullptr ||
                        panel.type != editor::testing::NativeUiCommandPanelTypeId ||
                        panel.id != editor::testing::NativeUiCommandPanelInstanceId)
                        return;
                    result->found = true;
                    result->placeholder = panel.placeholder;
                    result->open = panel.open;
                    result->titleMatches =
                        panel.title != nullptr &&
                        std::strcmp(panel.title, "Native UI command focus test") == 0;
                },
                &observation);
            if (!observation.found || !observation.placeholder || observation.open ||
                !observation.titleMatches)
                return engine::FrameParticipantStatus::Failure(
                    "editor user-state missing extension did not create the exact closed placeholder");

            app::StateMachineFailure failure;
            if (!m_config->states->RequestExit(0, &failure))
                return engine::FrameParticipantStatus::Failure(
                    failure.message != nullptr ? failure.message
                                               : "editor user-state missing-owner test could not request exit");
            return engine::FrameParticipantStatus::Success();
        }

        engine::FrameParticipantStatus ExecuteUserStateUnwritableFrame() noexcept
        {
            if (m_userState == nullptr && m_config->services != nullptr)
                m_userState = editor::FindEditorUserStateService(*m_config->services);
            if (m_userState == nullptr)
                return engine::FrameParticipantStatus::Failure(
                    "editor user-state unwritable fixture could not resolve UserState after host startup");
            const editor::EditorUserStateStatus status = m_userState->GetStatus();
            if (status.state != editor::EditorUserStateServiceState::Running ||
                status.load != editor::EditorUserStateLoadResult::Missing ||
                status.writesInhibited)
                return engine::FrameParticipantStatus::Failure(
                    "editor user-state unwritable fixture did not start from writable defaults");
            if (!m_unwritableBlockerCreated)
            {
                const filesystem::AbsolutePath blocker =
                    m_workspace->GetSavedRoot().AddFilePath("Editor");
                static constexpr vanguard::u8 blockerByte = 0x5au;
                if (filesystem::WriteExclusiveFile(blocker, &blockerByte, 1u) !=
                    filesystem::ExclusiveFileWriteResult::Success)
                    return engine::FrameParticipantStatus::Failure(
                        "editor user-state unwritable fixture could not create the isolated Saved/Editor blocker");
                m_unwritableBlockerCreated = true;
            }
            app::StateMachineFailure failure;
            if (!m_config->states->RequestExit(0, &failure))
                return engine::FrameParticipantStatus::Failure(
                    failure.message != nullptr ? failure.message
                                               : "editor user-state unwritable test could not request exit");
            return engine::FrameParticipantStatus::Success();
        }

        engine::FrameParticipantStatus CapturePreUiFrame(const engine::FrameContext& context) noexcept
        {
            m_preUiCaptured = false;
            if (m_config->states->GetCurrentState() != editor::EditorRunningStateId || !m_lastDrawnTexture.IsValid())
                return engine::FrameParticipantStatus::Success();
            if (m_ui == nullptr || m_rendering == nullptr)
                return engine::FrameParticipantStatus::Failure("editor scene-output pre-UI services became unavailable");

            PrimaryHost primary;
            m_ui->VisitHosts(CapturePrimaryHost, &primary);
            if (!primary.found || !primary.info.handle.IsValid() || !primary.info.renderViewport.IsValid())
                return engine::FrameParticipantStatus::Failure("editor scene-output test primary UI host is unavailable");

            editor::EditorUiFrameData frameData;
            if (!m_ui->AcquireFrameData(primary.info.handle, frameData) || !frameData.IsValid() || frameData.GetFrame() != context.frame)
                return engine::FrameParticipantStatus::Failure("editor scene-output current retained UI frame is unavailable");
            const auto* const drawData = static_cast<const editor::detail::EditorUiRenderData*>(frameData.GetPayload().data);
            if (drawData == nullptr)
                return engine::FrameParticipantStatus::Failure("editor scene-output retained UI draw payload is unavailable");

            vanguard::u32 textureIndex = ~vanguard::u32{0};
            for (vanguard::u32 index = 0; index < drawData->textures.Size(); ++index)
            {
                const editor::detail::EditorUiRenderTexture& texture = drawData->textures[index];
                if (texture.identity == m_lastDrawnTexture)
                {
                    if (!texture.publication.IsValid())
                        return engine::FrameParticipantStatus::Failure("editor scene-output retained UI texture lost its producer publication");
                    textureIndex = index;
                    break;
                }
            }
            if (textureIndex == ~vanguard::u32{0})
                return engine::FrameParticipantStatus::Failure("editor scene-output texture was not captured into retained UI frame data");

            bool referencedByDraw = false;
            for (const editor::detail::EditorUiRenderCommand& command : drawData->commands)
                if (command.kind == editor::detail::EditorUiRenderCommandKind::Draw && command.elementCount != 0 && command.texture == textureIndex)
                {
                    referencedByDraw = true;
                    break;
                }
            if (!referencedByDraw)
                return engine::FrameParticipantStatus::Failure("editor scene-output retained UI texture is not referenced by a draw command");

            const rendering::RenderViewport* const hostViewport = m_rendering->GetViewports().Resolve(primary.info.renderViewport);
            if (hostViewport == nullptr)
                return engine::FrameParticipantStatus::Failure("editor scene-output primary UI RenderViewport is unavailable");
            m_preUiHostViewport = primary.info.renderViewport;
            m_preUiRenderedFrames = hostViewport->GetRenderedFrameCount();
            m_preUiFrame = context.frame;
            m_preUiCaptured = true;
            return engine::FrameParticipantStatus::Success();
        }

        engine::FrameParticipantStatus ExecuteSceneOutputFrame(const engine::FrameContext& context) noexcept
        {
            if (++m_frames > MaximumSceneOutputFrames)
                return engine::FrameParticipantStatus::Failure("editor scene-output integration test exceeded its frame budget");
            if (m_config->states->GetCurrentState() != editor::EditorRunningStateId)
                return engine::FrameParticipantStatus::Success();
            if (m_session == nullptr || m_rendering == nullptr || m_sceneOutput == nullptr || m_ui == nullptr)
                return engine::FrameParticipantStatus::Failure("editor scene-output integration-test services became unavailable");

            if (!m_selectionRequested)
            {
                engine::WorldSessionFailure failure;
                if (!m_session->SelectWorldDefaultCamera(&m_selection, &failure))
                    return engine::FrameParticipantStatus::Failure(failure.message != nullptr
                                                                       ? failure.message
                                                                       : "editor scene-output test could not select the authored world camera");
                m_selectionRequested = true;
                return engine::FrameParticipantStatus::Success();
            }

            const engine::WorldSessionCameraBinding binding = m_session->GetCameraBinding();
            if (binding.status == engine::WorldSessionCameraBindingStatus::Failed)
                return engine::FrameParticipantStatus::Failure("editor scene-output test camera selection failed");
            if (binding.status != engine::WorldSessionCameraBindingStatus::Ready || !binding.camera.IsValid() ||
                !m_rendering->GetCameras().IsAlive(binding.camera))
                return engine::FrameParticipantStatus::Success();

            if (!m_demandRequested)
            {
                if (!m_sceneOutput->RequestOutputExtent({320, 180}))
                    return engine::FrameParticipantStatus::Failure("editor scene-output test demand request failed");
                m_demandRequested = true;
                return engine::FrameParticipantStatus::Success();
            }
            if (!m_lastDrawnTexture.IsValid())
                return engine::FrameParticipantStatus::Success();
            if (!m_preUiCaptured || m_preUiFrame != context.frame || !m_preUiHostViewport.IsValid())
                return engine::FrameParticipantStatus::Success();

            const rendering::RenderViewport* const hostViewport = m_rendering->GetViewports().Resolve(m_preUiHostViewport);
            if (hostViewport == nullptr || hostViewport->GetRenderedFrameCount() <= m_preUiRenderedFrames)
                return engine::FrameParticipantStatus::Failure("editor scene-output retained UI host did not submit the captured draw frame");

            rendering::RenderCommandFailure commandFailure;
            if (!m_rendering->GetCommands().FlushPreviousFrameProcessing(&commandFailure) ||
                m_rendering->GetCommands().ConsumeExecutionFailure(commandFailure))
                return engine::FrameParticipantStatus::Failure(commandFailure.message != nullptr
                                                                   ? commandFailure.message
                                                                   : "editor scene-output retained UI processing failed");

            editor::ResolvedEditorTexture resolved;
            if (!m_ui->ResolveTexture(m_lastDrawnTexture, resolved) || !resolved.publication.IsValid())
                return engine::FrameParticipantStatus::Failure("editor scene-output captured texture no longer resolves");
            const editor::EditorSceneOutputInfo published = m_sceneOutput->GetPublishedOutput();
            if (!published.IsValid() || published.texture != m_lastDrawnTexture || published.generation != m_lastDrawnGeneration)
                return engine::FrameParticipantStatus::Failure("editor scene-output publication changed underneath the captured UI generation");

            rendering::RenderOutputReceipt receipt;
            if (!resolved.publication.ResolveReceipt(receipt) || receipt.state != rendering::RenderOutputReceiptState::Ready ||
                receipt.viewport != resolved.publication.viewport || receipt.outputRevision != resolved.publication.outputRevision ||
                receipt.producerSerial == 0 || receipt.terminalState != rhi::ResourceState::ShaderResourceGraphics ||
                receipt.terminalQueue != rhi::QueueType::Graphics ||
                receipt.readiness != rendering::RenderOutputPublicationReadinessKind::SameQueueContinuation)
                return engine::FrameParticipantStatus::Failure("editor scene-output exact producer receipt is not Ready for retained UI consumption");

            app::StateMachineFailure failure;
            if (!m_config->states->RequestExit(0, &failure))
                return engine::FrameParticipantStatus::Failure(failure.message != nullptr
                                                                   ? failure.message
                                                                   : "editor scene-output integration test could not request exit");
            return engine::FrameParticipantStatus::Success();
        }

        editor::testing::EditorApplicationLifecycleIntegrationTestServiceConfig* m_config = nullptr;
        editor::EditorUiService* m_ui = nullptr;
        editor::EditorCommandService* m_commands = nullptr;
        editor::EditorActivityService* m_activity = nullptr;
        editor::EditorShellService* m_shell = nullptr;
        editor::EditorSceneOutputService* m_sceneOutput = nullptr;
        engine::RenderingService* m_rendering = nullptr;
        engine::WorldSessionService* m_session = nullptr;
        editor::EditorUserStateService* m_userState = nullptr;
        editor::ProjectWorkspaceService* m_workspace = nullptr;
        engine::WorldSessionCameraSelectionToken m_selection;
        editor::EditorPanelTypeToken m_panelType;
        editor::EditorPanelHandle m_panel;
        editor::EditorTextureHandle m_lastDrawnTexture;
        vanguard::u64 m_lastDrawnGeneration = 0;
        rendering::RenderViewportHandle m_preUiHostViewport;
        vanguard::u64 m_preUiRenderedFrames = 0;
        vanguard::u64 m_preUiFrame = 0;
        vanguard::u32 m_frames = 0;
        bool m_preUiCaptured = false;
        bool m_selectionRequested = false;
        bool m_demandRequested = false;
        bool m_unwritableBlockerCreated = false;
        bool m_demonstrationInvoked = false;
    };

    app::Service* CreateEditorApplicationLifecycleIntegrationTestService(void* const userData) noexcept
    {
        auto* const config = static_cast<editor::testing::EditorApplicationLifecycleIntegrationTestServiceConfig*>(userData);
        if (config == nullptr)
            return nullptr;
        const vanguard::memory::MemoryBlock block = vanguard::memory::Allocate(
            vanguard::memory::PoolId::Tools, sizeof(EditorApplicationLifecycleIntegrationTestService),
            alignof(EditorApplicationLifecycleIntegrationTestService));
        return block ? ::new (block.address) EditorApplicationLifecycleIntegrationTestService(*config) : nullptr;
    }

    void DestroyEditorApplicationLifecycleIntegrationTestService(app::Service* const service, void*) noexcept
    {
        if (service == nullptr)
            return;
        static_cast<EditorApplicationLifecycleIntegrationTestService*>(service)->~EditorApplicationLifecycleIntegrationTestService();
        vanguard::memory::MemoryBlock block{service, sizeof(EditorApplicationLifecycleIntegrationTestService),
                                            vanguard::memory::PoolId::Tools};
        vanguard::memory::Free(block);
    }

    [[nodiscard]] bool RegisterEditorApplicationLifecycleIntegrationTestService(
        app::EngineHost& host, editor::testing::EditorApplicationLifecycleIntegrationTestServiceConfig& config,
        app::HostFailure* const failure) noexcept
    {
        constexpr app::ServiceDependency standardDependencies[]{
            {engine::FramePipelineServiceId, app::DependencyKind::Required},
            {engine::RenderingServiceId, app::DependencyKind::Required},
            {engine::WorldSessionServiceId, app::DependencyKind::Required},
            {editor::ProjectWorkspaceServiceId, app::DependencyKind::Required},
            {editor::EditorUiServiceId, app::DependencyKind::Required},
            {editor::EditorSceneOutputServiceId, app::DependencyKind::Required}};
        constexpr app::ServiceDependency userStateDependencies[]{
            {engine::FramePipelineServiceId, app::DependencyKind::Required},
            {editor::ProjectWorkspaceServiceId, app::DependencyKind::Required},
            {editor::EditorUiServiceId, app::DependencyKind::Required},
            {editor::EditorShellServiceId, app::DependencyKind::Required}};
        constexpr app::ServiceDependency demonstrationDependencies[]{
            {engine::FramePipelineServiceId, app::DependencyKind::Required},
            {editor::ProjectWorkspaceServiceId, app::DependencyKind::Required},
            {editor::EditorUiServiceId, app::DependencyKind::Required},
            {editor::EditorShellServiceId, app::DependencyKind::Required},
            {editor::EditorCommandServiceId, app::DependencyKind::Required},
            {editor::EditorActivityServiceId, app::DependencyKind::Required}};
        app::ServiceDescriptor descriptor;
        descriptor.id = EditorApplicationLifecycleIntegrationTestServiceId;
        descriptor.name = "editorApplicationLifecycleIntegrationTest";
        descriptor.profiles = app::ApplicationProfile::Editor | app::ApplicationProfile::Tool;
        descriptor.scope = app::ServiceScope::TestSandbox;
        descriptor.affinity = app::ThreadAffinity::MainThread;
        if (config.userStateMissingPath || config.userStateUnwritablePath)
            descriptor.dependencies = {userStateDependencies, 4};
        else if (config.demonstrationExtensionPath)
            descriptor.dependencies = {demonstrationDependencies, 6};
        else
            descriptor.dependencies = {standardDependencies, 6};
        descriptor.create = CreateEditorApplicationLifecycleIntegrationTestService;
        descriptor.destroy = DestroyEditorApplicationLifecycleIntegrationTestService;
        descriptor.userData = &config;
        return host.RegisterService(editor::EditorModuleId, descriptor, failure);
    }
} // namespace

namespace vanguard::editor::testing
{
    ApplicationTraits EditorApplicationLifecycleIntegrationTestApplication::GetTraits() const noexcept
    {
        return m_editor.GetTraits();
    }

    application::CompositionStatus EditorApplicationLifecycleIntegrationTestApplication::Compose(
        const application::ApplicationStartupContext& startup, application::EngineHost& services,
        application::ApplicationStateMachine& states) noexcept
    {
        const bool bootstrapPath = startup.commandLine.HasArgument("--bootstrap-path");
        const bool workspacePath = startup.commandLine.HasArgument("--workspace-path");
        const bool sceneOutputPath = startup.commandLine.HasArgument("--scene-output-path");
        const bool demonstrationExtensionPath =
            startup.commandLine.HasArgument("--demonstration-extension-path");
        const bool userStateMissingPath = startup.commandLine.HasArgument("--user-state-missing-path");
        const bool userStateUnwritablePath = startup.commandLine.HasArgument("--user-state-unwritable-path");
        if (static_cast<vanguard::u32>(bootstrapPath) + static_cast<vanguard::u32>(workspacePath) +
                static_cast<vanguard::u32>(sceneOutputPath) +
                static_cast<vanguard::u32>(demonstrationExtensionPath) +
                static_cast<vanguard::u32>(userStateMissingPath) +
                static_cast<vanguard::u32>(userStateUnwritablePath) !=
            1u)
            return application::CompositionStatus::Failure(
                "editor lifecycle integration-test executable requires exactly one test path flag");

        const application::CompositionStatus productComposition = m_editor.Compose(startup, services, states);
        if (!productComposition)
            return productComposition;
        if ((bootstrapPath || demonstrationExtensionPath) && !states.SetInitialState(EditorRunningStateId))
            return application::CompositionStatus::Failure("editor lifecycle integration test could not select the running state");

        m_serviceConfig.services = &services;
        m_serviceConfig.states = &states;
        m_serviceConfig.sceneOutputPath = sceneOutputPath;
        m_serviceConfig.demonstrationExtensionPath = demonstrationExtensionPath;
        m_serviceConfig.userStateMissingPath = userStateMissingPath;
        m_serviceConfig.userStateUnwritablePath = userStateUnwritablePath;
        application::HostFailure failure;
        if (!RegisterEditorApplicationLifecycleIntegrationTestService(services, m_serviceConfig, &failure))
            return application::CompositionStatus::Failure(failure.message != nullptr
                                                               ? failure.message
                                                               : "editor lifecycle integration-test service registration failed");
        if (userStateMissingPath || userStateUnwritablePath)
            services.SetLifecycleSink(LogUserStateLifecycleEvent);
        return application::CompositionStatus::Success();
    }
} // namespace vanguard::editor::testing
