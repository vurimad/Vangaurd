#include <vanguard/editor/editor_scene_view_service.hpp>

#include <vanguard/editor/editor_ui_imgui.hpp>

#include <vanguard/concurrency/thread.hpp>
#include <vanguard/engine/engine_services.hpp>
#include <vanguard/engine/game_world_service.hpp>
#include <vanguard/engine/rendering_service.hpp>
#include <vanguard/engine/world_session_service.hpp>
#include <vanguard/entities/rendering_runtime.hpp>
#include <vanguard/memory/memory.hpp>

#include <imgui.h>

#include <new>

namespace
{
    namespace app = vanguard::application;
    namespace editor = vanguard::editor;
    namespace engine = vanguard::engine;
    namespace rendering = vanguard::rendering;

    class ManagedEditorSceneViewService final : public editor::EditorSceneViewService
    {
    public:
        [[nodiscard]] bool PrepareForSessionStop() noexcept override
        {
            if (!vanguard::concurrency::IsMainThread())
                return false;
            if (m_sceneOutput != nullptr && !m_sceneOutput->ClearOutputDemand())
                return false;
            m_requestedExtent = {};
            m_panelTouchFrame = 0;

            if (m_selection.IsValid())
            {
                if (m_session == nullptr)
                    return false;
                const engine::WorldSessionCameraBinding binding = m_session->GetCameraBinding();
                if (binding.GetSelectionToken() == m_selection)
                {
                    if (m_session->GetStatus() != engine::WorldSessionStatus::Running)
                        return false;
                    engine::WorldSessionFailure failure;
                    if (!m_session->ClearCameraSelection(m_selection, &failure))
                        return false;
                }
                m_selection = {};
            }
            if (m_camera.IsValid())
            {
                rendering::RenderCameraFailure failure;
                if (m_rendering == nullptr || !m_rendering->GetCommands().UnregisterCamera(m_camera, &failure))
                    return false;
                m_camera = {};
            }
            m_scene = {};
            return true;
        }

    protected:
        app::LifecycleStatus OnInitialize(app::ServiceContext& context) noexcept override
        {
            m_frames = engine::FindFramePipelineService(context);
            m_rendering = engine::FindRenderingService(context);
            m_gameWorld = engine::FindGameWorldService(context);
            m_session = engine::FindWorldSessionService(context);
            m_ui = editor::FindEditorUiService(context);
            m_sceneOutput = editor::FindEditorSceneOutputService(context);
            if (m_frames == nullptr || m_rendering == nullptr || m_gameWorld == nullptr || m_session == nullptr ||
                m_ui == nullptr || m_sceneOutput == nullptr)
                return app::LifecycleStatus::Failure("editor Scene view dependencies are unavailable");

            editor::EditorPanelTypeDesc panelType;
            panelType.id = editor::EditorScenePanelTypeId;
            panelType.name = "Scene";
            panelType.create = &CreateScenePanel;
            panelType.destroy = &DestroyScenePanel;
            panelType.draw = &DrawScenePanel;
            panelType.factoryData = this;
            panelType.policy = editor::EditorPanelTypePolicy::Singleton;
            panelType.defaultInstance = editor::EditorScenePanelInstanceId;
            panelType.initialPlacement = editor::EditorPanelInitialPlacement::Center;
            editor::EditorUiFailure uiFailure;
            if (!m_ui->GetPanels().RegisterType(panelType, m_panelType, &uiFailure))
                return app::LifecycleStatus::Failure(uiFailure.message != nullptr ? uiFailure.message
                                                                                  : "editor Scene panel type registration failed");
            if (!m_ui->GetPanels().CreatePanel(
                    {editor::EditorScenePanelInstanceId, editor::EditorScenePanelTypeId, "Scene", true},
                    m_panel, &uiFailure))
            {
                static_cast<void>(m_ui->GetPanels().UnregisterType(m_panelType));
                m_panelType = {};
                return app::LifecycleStatus::Failure(uiFailure.message != nullptr ? uiFailure.message
                                                                                  : "editor Scene panel creation failed");
            }

            constexpr engine::FrameParticipantId dependencies[]{editor::EditorUiFrameParticipantId};
            engine::FrameParticipantDescriptor participant;
            participant.id = editor::EditorSceneViewFrameParticipantId;
            participant.name = "editor.sceneView";
            participant.phase = engine::FramePhase::PreSimulation;
            participant.profiles = app::ApplicationProfile::Editor | app::ApplicationProfile::Tool;
            participant.affinity = app::ThreadAffinity::MainThread;
            participant.after = {dependencies, 1};
            participant.execute = &ExecuteFrame;
            participant.userData = this;
            engine::FrameFailure frameFailure;
            if (!m_frames->RegisterParticipant(participant, &frameFailure))
            {
                static_cast<void>(m_ui->GetPanels().DestroyPanel(m_panel));
                m_panel = {};
                static_cast<void>(m_ui->GetPanels().UnregisterType(m_panelType));
                m_panelType = {};
                return app::LifecycleStatus::Failure(frameFailure.message != nullptr ? frameFailure.message
                                                                                    : "editor Scene view frame registration failed");
            }
            return app::LifecycleStatus::Success();
        }

        app::LifecycleStatus OnStart(app::ServiceContext&) noexcept override
        {
            m_acceptFrames = true;
            return app::LifecycleStatus::Success();
        }

        app::LifecycleStatus OnQuiesce(app::ServiceContext&) noexcept override
        {
            m_acceptFrames = false;
            if (!PrepareForSessionStop())
                return app::LifecycleStatus::Failure("editor Scene view ownership could not quiesce before world-session teardown");
            editor::EditorUiFailure failure;
            if (m_panel.IsValid() && !m_ui->GetPanels().DestroyPanel(m_panel, &failure))
                return app::LifecycleStatus::Failure(failure.message != nullptr ? failure.message : "editor Scene panel destruction failed");
            m_panel = {};
            if (m_panelType.IsValid() && !m_ui->GetPanels().UnregisterType(m_panelType, &failure))
                return app::LifecycleStatus::Failure(failure.message != nullptr ? failure.message : "editor Scene panel type unregistration failed");
            m_panelType = {};
            return app::LifecycleStatus::Success();
        }

        app::LifecycleStatus OnDrain(app::ServiceContext&) noexcept override
        {
            return !m_camera.IsValid() && !m_selection.IsValid() && !m_panel.IsValid() && !m_panelType.IsValid()
                       ? app::LifecycleStatus::Success()
                       : app::LifecycleStatus::Failure("editor Scene view ownership survived drain");
        }

        app::LifecycleStatus OnShutdown(app::ServiceContext&) noexcept override
        {
            m_acceptFrames = false;
            m_frames = nullptr;
            m_rendering = nullptr;
            m_gameWorld = nullptr;
            m_session = nullptr;
            m_ui = nullptr;
            m_sceneOutput = nullptr;
            return app::LifecycleStatus::Success();
        }

    private:
        static void* CreateScenePanel(editor::EditorPanelInstanceId, void* const factoryData) noexcept
        {
            return factoryData;
        }

        static void DestroyScenePanel(void*, void*) noexcept {}

        static void DrawScenePanel(editor::EditorPanelContext&, void* const panel, void* const factoryData) noexcept
        {
            auto* const service = static_cast<ManagedEditorSceneViewService*>(panel != nullptr ? panel : factoryData);
            if (service == nullptr || service->m_ui == nullptr || service->m_sceneOutput == nullptr)
                return;

            const ImVec2 available = ImGui::GetContentRegionAvail();
            service->m_panelTouchFrame = service->m_ui->GetFrame();
            service->m_requestedExtent = available.x >= 1.0f && available.y >= 1.0f
                                             ? rendering::ViewportExtent{static_cast<vanguard::u32>(available.x),
                                                                         static_cast<vanguard::u32>(available.y)}
                                             : rendering::ViewportExtent{};
            if (!service->m_requestedExtent.IsValid())
                return;

            const editor::EditorSceneOutputInfo output = service->m_sceneOutput->GetPublishedOutput();
            if (!output.IsValid())
            {
                ImGui::TextDisabled("Scene output is preparing...");
                return;
            }
            ImGui::Image(editor::detail::ToImGuiTexture(output.texture), available);
        }

        [[nodiscard]] bool EnsureLiveCamera() noexcept
        {
            if (m_session->GetStatus() != engine::WorldSessionStatus::Running)
                return true;
            vanguard::entities::RenderingRuntime* const worldRendering = m_gameWorld->GetRendering();
            if (worldRendering == nullptr || !worldRendering->GetScene().IsValid())
                return false;
            const rendering::RenderSceneHandle scene = worldRendering->GetScene();
            if (m_camera.IsValid() && m_scene == scene && m_rendering->GetCameras().IsAlive(m_camera) && m_selection.IsValid())
                return true;
            if ((m_camera.IsValid() || m_selection.IsValid()) && !PrepareForSessionStop())
                return false;

            rendering::RenderCameraDesc desc;
            desc.scene = scene;
            desc.name = "EditorScene";
            desc.renderPolicy = rendering::RenderCameraRenderPolicy::OnDemand;
            desc.state.projection.kind = rendering::RenderCameraProjectionKind::Perspective;
            desc.state.projection.verticalFieldOfViewRadians = 1.0471975512f;
            desc.state.projection.aspectRatio = 0.0f;
            desc.state.projection.nearPlane = 0.1f;
            desc.state.projection.farPlane = 100.0f;
            desc.state.projection.reverseDepth = true;
            desc.state.projection.infiniteFarPlane = false;
            desc.state.resolution.mode = rendering::RenderCameraResolutionMode::InheritFrame;
            desc.state.phases = m_rendering->GetDefaultCameraPhases();
            rendering::RenderCameraFailure cameraFailure;
            if (!m_rendering->GetCommands().RegisterCamera(desc, m_camera, &cameraFailure))
                return false;

            engine::WorldCameraPose pose;
            pose.position = math::WorldPosition(0.0f, 0.0f, 0.0f);
            pose.orientation = math::Quaternion(0.0f, 0.0f, 0.0f, 1.0f);
            engine::WorldSessionFailure sessionFailure;
            if (!m_session->SelectLiveCamera(m_camera, pose, &m_selection, &sessionFailure))
            {
                rendering::RenderCameraFailure unregisterFailure;
                static_cast<void>(m_rendering->GetCommands().UnregisterCamera(m_camera, &unregisterFailure));
                m_camera = {};
                return false;
            }
            m_scene = scene;
            return true;
        }

        [[nodiscard]] engine::FrameParticipantStatus AdvanceFrame(const engine::FrameContext& context) noexcept
        {
            if (!m_acceptFrames)
                return engine::FrameParticipantStatus::Success();

            const bool touched = m_ui->GetFrame() != 0 && m_panelTouchFrame == m_ui->GetFrame() && m_requestedExtent.IsValid();
            if (touched)
            {
                if (m_observedExtent != m_requestedExtent)
                {
                    m_observedExtent = m_requestedExtent;
                    m_observedExtentSinceSeconds = context.realTimeSeconds;
                }
                const editor::EditorSceneOutputInfo published = m_sceneOutput->GetPublishedOutput();
                const rendering::ViewportExtent publishedExtent = published.IsValid() ? published.extent : rendering::ViewportExtent{};
                const rendering::ViewportExtent demand = editor::detail::ShouldCommitEditorSceneResize(
                                                             m_requestedExtent, publishedExtent, m_observedExtent,
                                                             m_observedExtentSinceSeconds, context.realTimeSeconds)
                                                             ? m_requestedExtent
                                                             : publishedExtent;
                if (!m_sceneOutput->RequestOutputExtent(demand))
                    return engine::FrameParticipantStatus::Failure("editor Scene output demand could not be updated");
            }
            else if (!m_sceneOutput->ClearOutputDemand())
                return engine::FrameParticipantStatus::Failure("editor Scene output demand could not be cleared");

            if (m_session->GetStatus() != engine::WorldSessionStatus::Running)
                return engine::FrameParticipantStatus::Success();
            if (!EnsureLiveCamera())
                return engine::FrameParticipantStatus::Failure("editor Scene live camera could not be established");
            return engine::FrameParticipantStatus::Success();
        }

        static engine::FrameParticipantStatus ExecuteFrame(const engine::FrameContext& context, void* const userData) noexcept
        {
            auto* const service = static_cast<ManagedEditorSceneViewService*>(userData);
            return service != nullptr ? service->AdvanceFrame(context)
                                      : engine::FrameParticipantStatus::Failure("editor Scene view service is unavailable");
        }

        engine::FramePipelineService* m_frames = nullptr;
        engine::RenderingService* m_rendering = nullptr;
        engine::GameWorldService* m_gameWorld = nullptr;
        engine::WorldSessionService* m_session = nullptr;
        editor::EditorUiService* m_ui = nullptr;
        editor::EditorSceneOutputService* m_sceneOutput = nullptr;
        editor::EditorPanelTypeToken m_panelType;
        editor::EditorPanelHandle m_panel;
        rendering::RenderCameraHandle m_camera;
        rendering::RenderSceneHandle m_scene;
        engine::WorldSessionCameraSelectionToken m_selection;
        rendering::ViewportExtent m_requestedExtent;
        rendering::ViewportExtent m_observedExtent;
        vanguard::f64 m_observedExtentSinceSeconds = 0.0;
        vanguard::u64 m_panelTouchFrame = 0;
        bool m_acceptFrames = false;
    };

    app::Service* CreateEditorSceneViewService(void*) noexcept
    {
        const vanguard::memory::MemoryBlock block = vanguard::memory::Allocate(
            vanguard::memory::PoolId::Editor, sizeof(ManagedEditorSceneViewService), alignof(ManagedEditorSceneViewService));
        return block ? ::new (block.address) ManagedEditorSceneViewService() : nullptr;
    }

    void DestroyEditorSceneViewService(app::Service* const service, void*) noexcept
    {
        if (service == nullptr)
            return;
        static_cast<ManagedEditorSceneViewService*>(service)->~ManagedEditorSceneViewService();
        vanguard::memory::MemoryBlock block{service, sizeof(ManagedEditorSceneViewService), vanguard::memory::PoolId::Editor};
        vanguard::memory::Free(block);
    }
} // namespace

namespace vanguard::editor
{
    bool RegisterEditorSceneViewService(application::EngineHost& host, application::HostFailure* const failure) noexcept
    {
        constexpr application::ServiceDependency dependencies[]{
            {engine::FramePipelineServiceId, application::DependencyKind::Required},
            {engine::RenderingServiceId, application::DependencyKind::Required},
            {engine::GameWorldServiceId, application::DependencyKind::Required},
            {engine::WorldSessionServiceId, application::DependencyKind::Required},
            {EditorUiServiceId, application::DependencyKind::Required},
            {EditorSceneOutputServiceId, application::DependencyKind::Required}};
        application::ServiceDescriptor descriptor;
        descriptor.id = EditorSceneViewServiceId;
        descriptor.name = "editorSceneView";
        descriptor.profiles = application::ApplicationProfile::Editor | application::ApplicationProfile::Tool;
        descriptor.scope = application::ServiceScope::EditorWorkspace;
        descriptor.affinity = application::ThreadAffinity::MainThread;
        descriptor.dependencies = {dependencies, 6};
        descriptor.create = CreateEditorSceneViewService;
        descriptor.destroy = DestroyEditorSceneViewService;
        return host.RegisterService(EditorModuleId, descriptor, failure);
    }

    EditorSceneViewService* FindEditorSceneViewService(application::EngineHost& host) noexcept
    {
        return static_cast<EditorSceneViewService*>(host.Find(EditorSceneViewServiceId));
    }

    EditorSceneViewService* FindEditorSceneViewService(application::ServiceContext& context) noexcept
    {
        return static_cast<EditorSceneViewService*>(context.Find(EditorSceneViewServiceId));
    }
} // namespace vanguard::editor
