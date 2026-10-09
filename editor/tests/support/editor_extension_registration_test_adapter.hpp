// TEST SUPPORT ONLY: exposes the real extension-registration authority to focused tests.
#pragma once

#include <vanguard/editor/editor_shell_service.hpp>

namespace vanguard::editor::testing
{
    class EditorExtensionRegistrationTestAdapter final : public EditorExtensionRegistrationAccess
    {
    public:
        [[nodiscard]] static constexpr EditorExtensionOwner DefaultOwner() noexcept
        {
            return {{0x746573746f776e01ull}, 1u};
        }

        [[nodiscard]] static bool RegisterCommand(EditorCommandService& service, const EditorCommandDescriptor& descriptor,
                                                  EditorCommandToken& token, EditorCommandFailure* failure = nullptr) noexcept
        {
            return RegisterOwnedCommand(service, DefaultOwner(), descriptor, token, failure);
        }

        [[nodiscard]] static bool UnregisterCommand(EditorCommandService& service, EditorCommandToken token,
                                                    EditorCommandFailure* failure = nullptr) noexcept
        {
            return UnregisterOwnedCommand(service, DefaultOwner(), token, failure);
        }

        [[nodiscard]] static bool RegisterContextType(EditorCommandService& service,
                                                      const EditorCommandContextTypeDescriptor& descriptor,
                                                      EditorCommandContextTypeToken& token,
                                                      EditorCommandFailure* failure = nullptr) noexcept
        {
            return RegisterOwnedContextType(service, DefaultOwner(), descriptor, token, failure);
        }

        [[nodiscard]] static bool UnregisterContextType(EditorCommandService& service, EditorCommandContextTypeToken token,
                                                        EditorCommandFailure* failure = nullptr) noexcept
        {
            return UnregisterOwnedContextType(service, DefaultOwner(), token, failure);
        }

        [[nodiscard]] static bool RegisterHandler(EditorCommandService& service, const EditorCommandHandlerDescriptor& descriptor,
                                                  EditorCommandHandlerToken& token,
                                                  EditorCommandFailure* failure = nullptr) noexcept
        {
            return RegisterOwnedHandler(service, DefaultOwner(), descriptor, token, failure);
        }

        [[nodiscard]] static bool UnregisterHandler(EditorCommandService& service, EditorCommandHandlerToken token,
                                                    EditorCommandFailure* failure = nullptr) noexcept
        {
            return UnregisterOwnedHandler(service, DefaultOwner(), token, failure);
        }

        using EditorExtensionRegistrationAccess::RegisterOwnedCommand;
        using EditorExtensionRegistrationAccess::RegisterOwnedContextType;
        using EditorExtensionRegistrationAccess::RegisterOwnedHandler;
        using EditorExtensionRegistrationAccess::RegisterOwnedPanelType;
        using EditorExtensionRegistrationAccess::CanRevokePanelOwner;
        using EditorExtensionRegistrationAccess::RevokeCommandOwner;
        using EditorExtensionRegistrationAccess::RevokePanelOwner;
        using EditorExtensionRegistrationAccess::UnregisterOwnedCommand;
        using EditorExtensionRegistrationAccess::UnregisterOwnedContextType;
        using EditorExtensionRegistrationAccess::UnregisterOwnedHandler;
        using EditorExtensionRegistrationAccess::UnregisterOwnedPanelType;
        using EditorExtensionRegistrationAccess::QueryCommandPresentation;
        using EditorExtensionRegistrationAccess::ValidateOwnedCommand;
        using EditorExtensionRegistrationAccess::ValidateOwnedLocalContext;
    };
} // namespace vanguard::editor::testing
