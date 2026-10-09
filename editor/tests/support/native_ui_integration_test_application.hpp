#pragma once

#include <native_ui_integration_test_extension.hpp>
#include <vanguard/editor/editor_application.hpp>

namespace vanguard::editor::testing
{
    struct NativeUiIntegrationTestServiceConfig final
    {
        application::EngineHost* services = nullptr;
        application::ApplicationStateMachine* states = nullptr;
        bool interactive = false;
        bool visualPreview = false;
        NativeUiPersistenceScenario persistenceScenario = NativeUiPersistenceScenario::None;
    };

    class NativeUiIntegrationTestApplication final : public vanguard::Application
    {
    public:
        [[nodiscard]] ApplicationTraits GetTraits() const noexcept override;
        [[nodiscard]] application::CompositionStatus Compose(const application::ApplicationStartupContext& startup,
                                                             application::EngineHost& services,
                                                             application::ApplicationStateMachine& states) noexcept override;

    private:
        EditorApplication m_editor;
        NativeUiIntegrationTestServiceConfig m_serviceConfig;
    };
} // namespace vanguard::editor::testing
