#pragma once

#include <vanguard/application/application.hpp>

namespace vanguard::runtime
{
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
        void ExitAfterFirstTick(const bool enabled) noexcept { m_exitAfterFirstTick = enabled; }

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
        [[nodiscard]] application::CompositionStatus Compose(
            const application::ApplicationStartupContext& startup,
            application::EngineHost& services,
            application::ApplicationStateMachine& states) noexcept override;

    private:
        StartupSessionState m_startupSessionState;
        RunningState m_runningState;
    };
} // namespace vanguard::runtime
