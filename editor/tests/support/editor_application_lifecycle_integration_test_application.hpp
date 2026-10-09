// TEST SUPPORT ONLY: non-interactive lifecycle integration-test application wrapper.
#pragma once

#include <vanguard/editor/editor_application.hpp>

namespace vanguard::editor::testing
{
    struct EditorApplicationLifecycleIntegrationTestServiceConfig final
    {
        application::EngineHost* services = nullptr;
        application::ApplicationStateMachine* states = nullptr;
        bool sceneOutputPath = false;
        bool demonstrationExtensionPath = false;
        bool userStateMissingPath = false;
        bool userStateUnwritablePath = false;
    };

    class EditorApplicationLifecycleIntegrationTestApplication final : public vanguard::Application
    {
    public:
        [[nodiscard]] ApplicationTraits GetTraits() const noexcept override;
        [[nodiscard]] application::CompositionStatus Compose(const application::ApplicationStartupContext& startup,
                                                             application::EngineHost& services,
                                                             application::ApplicationStateMachine& states) noexcept override;

    private:
        EditorApplication m_editor;
        EditorApplicationLifecycleIntegrationTestServiceConfig m_serviceConfig;
    };
} // namespace vanguard::editor::testing
