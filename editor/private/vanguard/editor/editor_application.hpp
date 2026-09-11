#pragma once
#include <vanguard/editor/editor_ui_proof.hpp>

#include <vanguard/application/application.hpp>
#include <vanguard/containers/containers.hpp>
#include <vanguard/engine/rendering_service.hpp>
#include <vanguard/engine/resource_streaming_service.hpp>
#include <vanguard/filesystem/filesystem.hpp>

#include <vanguard/editor/editor_project_service.hpp>

namespace vanguard::editor
{
    class StartupSessionState final : public application::ApplicationState
    {
    protected:
        application::StateOperationStatus OnEnter(application::StateContext& context) noexcept override;
        application::StateTickStatus OnTick(application::StateContext& context) noexcept override;
        application::StateOperationStatus OnExit(application::StateContext& context) noexcept override;

    private:
        bool m_worldConfigured = false;
        bool m_transitionRequested = false;
    };

    class RunningState final : public application::ApplicationState
    {
    public:
        void ExitAfterFirstTick(const bool enabled) noexcept
        {
            m_exitAfterFirstTick = enabled;
        }
        void SetProjectWindowTitle(containers::StringView displayName, containers::StringView technicalName) noexcept;
        void EnableUiProof(bool enabled, bool interactive = false) noexcept { m_uiProofEnabled = enabled; m_uiProofInteractive = interactive; }

    protected:
        application::StateOperationStatus OnEnter(application::StateContext& context) noexcept override;
        application::StateTickStatus OnTick(application::StateContext& context) noexcept override;
        application::StateOperationStatus OnExit(application::StateContext& context) noexcept override;

    private:
        containers::String m_windowTitle;
        bool m_exitAfterFirstTick = false;
        bool m_uiProofEnabled = false;
        bool m_uiProofInteractive = false;
        EditorUiProof m_uiProof;
    };

    class EditorApplication final : public vanguard::Application
    {
    public:
        [[nodiscard]] ApplicationTraits GetTraits() const noexcept override;
        [[nodiscard]] application::CompositionStatus Compose(const application::ApplicationStartupContext& startup, application::EngineHost& services,
                                                             application::ApplicationStateMachine& states) noexcept override;

    private:
        filesystem::Config m_filesystemConfig;
        engine::RenderingServiceConfig m_renderingConfig;
        engine::ResourceStreamingServiceConfig m_streamingConfig;
        ProjectWorkspaceConfig m_workspaceConfig;
        StartupSessionState m_startupSessionState;
        RunningState m_runningState;
    };
} // namespace vanguard::editor
