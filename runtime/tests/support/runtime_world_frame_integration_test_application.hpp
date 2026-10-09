// TEST SUPPORT ONLY: native runtime world-frame validation layered on the real runtime composition.
#pragma once

#include <vanguard/runtime/runtime_application.hpp>

namespace vanguard::runtime::testing
{
    inline constexpr application::StateId RuntimeWorldReplacementValidationStateId = 0x7274776672737401ull;

    struct RuntimeWorldFrameIntegrationTestServiceConfig final
    {
        application::ApplicationStateMachine* states = nullptr;
        bool replacementPath = false;
    };

    class RuntimeWorldReplacementValidationState final : public application::ApplicationState
    {
    protected:
        application::StateOperationStatus OnEnter(application::StateContext& context) noexcept override;
        application::StateTickStatus OnTick(application::StateContext& context) noexcept override;
        application::StateOperationStatus OnExit(application::StateContext& context) noexcept override;

    private:
        bool m_transitionRequested = false;
    };

    class RuntimeWorldFrameIntegrationTestApplication final : public vanguard::Application
    {
    public:
        [[nodiscard]] ApplicationTraits GetTraits() const noexcept override;
        [[nodiscard]] application::CompositionStatus Compose(const application::ApplicationStartupContext& startup,
                                                             application::EngineHost& services,
                                                             application::ApplicationStateMachine& states) noexcept override;

    private:
        RuntimeApplication m_runtime;
        RuntimeWorldFrameIntegrationTestServiceConfig m_serviceConfig;
        RuntimeWorldReplacementValidationState m_replacementState;
    };
} // namespace vanguard::runtime::testing
