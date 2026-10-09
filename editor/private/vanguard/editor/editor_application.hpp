#pragma once
#include <vanguard/application/application.hpp>
#include <vanguard/containers/containers.hpp>
#include <vanguard/engine/rendering_service.hpp>
#include <vanguard/engine/resource_streaming_service.hpp>
#include <vanguard/engine/window_service.hpp>
#include <vanguard/filesystem/filesystem.hpp>

#include <vanguard/editor/editor_project_service.hpp>
#include <vanguard/editor/editor_resources.hpp>
#include <vanguard/editor/editor_shell_service.hpp>
#include <vanguard/editor/editor_user_state_service.hpp>

namespace vanguard::editor
{
    inline constexpr application::StateId EditorStartupSessionStateId = 0x6564737461727401ull;
    inline constexpr application::StateId EditorRunningStateId = 0x72756e6e696e6701ull;

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
        void SetProjectWindowTitle(containers::StringView displayName, containers::StringView technicalName) noexcept;
        void SetFrameDiagnosticsEnabled(bool enabled) noexcept { m_frameDiagnosticsEnabled = enabled; }
    protected:
        application::StateOperationStatus OnEnter(application::StateContext& context) noexcept override;
        application::StateTickStatus OnTick(application::StateContext& context) noexcept override;
        application::StateOperationStatus OnExit(application::StateContext& context) noexcept override;

    private:
        containers::String m_windowTitle;
        bool m_frameDiagnosticsEnabled = false;
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
        engine::WindowServiceConfig m_windowConfig;
        EditorResourceServiceConfig m_editorResourceConfig;
        ProjectWorkspaceConfig m_workspaceConfig;
        EditorShellServiceConfig m_shellConfig;
        EditorUserStateServiceConfig m_userStateConfig;
        StartupSessionState m_startupSessionState;
        RunningState m_runningState;
    };
} // namespace vanguard::editor
