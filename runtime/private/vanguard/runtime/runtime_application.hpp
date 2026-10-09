#pragma once

#include <vanguard/application/application.hpp>
#include <vanguard/engine/rendering_service.hpp>
#include <vanguard/engine/resource_streaming_service.hpp>
#include <vanguard/engine/window_service.hpp>
#include <vanguard/filesystem/filesystem.hpp>

namespace vanguard::runtime
{
    inline constexpr application::StateId RuntimeStartupSessionStateId = 0x7374617274736501ull;
    inline constexpr application::StateId RuntimeRunningStateId = 0x72756e6e696e6701ull;

    class StartupSessionState final : public application::ApplicationState
    {
    protected:
        application::StateOperationStatus OnEnter(application::StateContext& context) noexcept override;
        application::StateTickStatus OnTick(application::StateContext& context) noexcept override;
        application::StateOperationStatus OnExit(application::StateContext& context) noexcept override;

    private:
        bool m_transitionRequested = false;
    };

    class RunningState final : public application::ApplicationState
    {
    public:
        void ExitAfterFirstTick(const bool enabled) noexcept
        {
            m_exitAfterFirstTick = enabled;
        }

    protected:
        application::StateOperationStatus OnEnter(application::StateContext& context) noexcept override;
        application::StateTickStatus OnTick(application::StateContext& context) noexcept override;
        application::StateOperationStatus OnExit(application::StateContext& context) noexcept override;

    private:
        bool m_exitAfterFirstTick = false;
    };

    class RuntimeApplication final : public vanguard::Application
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
        StartupSessionState m_startupSessionState;
        RunningState m_runningState;
    };
} // namespace vanguard::runtime
