#pragma once

#include <vanguard/application/application.hpp>

namespace vanguard::editor
{
    class RunningState final : public application::ApplicationState
    {
    public:
        void ExitAfterFirstTick(const bool enabled) noexcept { m_exitAfterFirstTick = enabled; }

    protected:
        application::StateOperationStatus OnEnter(application::StateContext& context) noexcept override;
        application::StateTickStatus OnTick(application::StateContext& context) noexcept override;

    private:
        bool m_exitAfterFirstTick = false;
    };

    class EditorApplication final : public vanguard::Application
    {
    public:
        [[nodiscard]] ApplicationTraits GetTraits() const noexcept override;
        [[nodiscard]] application::CompositionStatus Compose(
            const application::ApplicationStartupContext& startup,
            application::EngineHost& services,
            application::ApplicationStateMachine& states) noexcept override;

    private:
        RunningState m_runningState;
    };
} // namespace vanguard::editor
