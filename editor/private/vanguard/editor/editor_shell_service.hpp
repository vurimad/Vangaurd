#pragma once

#include <vanguard/editor/editor_activity_service.hpp>
#include <vanguard/editor/editor_commands.hpp>
#include <vanguard/editor/editor_extension.hpp>
#include <vanguard/editor/editor_file_dialog.hpp>
#include <vanguard/editor/editor_project_service.hpp>
#include <vanguard/editor/editor_service_ids.hpp>
#include <vanguard/editor/editor_surfaces.hpp>
#include <vanguard/editor/editor_ui.hpp>

#include <vanguard/application/engine_host.hpp>

namespace vanguard::projects
{
    struct ProjectDescriptor;
}

namespace vanguard::editor
{
    class EditorOwnedPanelAccess;

    // The shell-owned typed registration ledger is deliberately bounded. Domain
    // registries retain authority for their own entries and capacities.
    inline constexpr u32 MaximumEditorExtensionOwnedRegistrations = 1024;
    inline constexpr u32 MaximumEditorPreferenceProviders = 256;
    inline constexpr u32 MaximumEditorPreferenceFieldsPerProvider = 32;
    inline constexpr u32 MaximumEditorLivePreferenceFields = 512;
    inline constexpr u32 MaximumEditorPreferenceStringBytes = 256;

    using EditorPreferenceProviderId = u64;
    using EditorPreferenceFieldId = u64;
    using EditorPreferenceCategoryId = u64;
    using EditorPreferenceSectionId = u64;

    struct EditorPreferenceProviderToken final
    {
        u32 index = ~u32{0};
        u32 generation = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return index < MaximumEditorPreferenceProviders && generation != 0;
        }
        [[nodiscard]] friend constexpr bool operator==(EditorPreferenceProviderToken,
                                                       EditorPreferenceProviderToken) noexcept = default;
    };

    enum class EditorPreferenceValueType : u8
    {
        Bool = 1,
        SignedInteger = 2,
        UnsignedBitMask = 3,
        Utf8String = 4
    };

    struct EditorPreferenceFieldDescriptor final
    {
        EditorPreferenceFieldId fieldId = 0;
        EditorPreferenceValueType type = EditorPreferenceValueType::Bool;
        u16 maximumStringBytes = MaximumEditorPreferenceStringBytes;
    };

    enum class EditorPreferenceControlKind : u8
    {
        Automatic,
        Toggle,
        Choice,
        SignedInteger,
        Text
    };

    struct EditorPreferenceChoiceDescriptor final
    {
        i64 value = 0;
        const char* label = nullptr;
    };

    struct EditorPreferenceFieldPresentation final
    {
        EditorPreferenceFieldId fieldId = 0;
        const char* label = nullptr;
        const char* description = nullptr;
        EditorPreferenceControlKind control = EditorPreferenceControlKind::Automatic;
        containers::ArraySpan<const EditorPreferenceChoiceDescriptor> choices;
        i64 minimum = 0;
        i64 maximum = 0;
        i64 step = 1;
        i32 order = 0;
    };

    struct EditorPreferenceProviderPresentation final
    {
        EditorPreferenceCategoryId categoryId = 0;
        const char* categoryLabel = nullptr;
        i32 categoryOrder = 0;
        EditorPreferenceSectionId sectionId = 0;
        const char* sectionLabel = nullptr;
        const char* description = nullptr;
        i32 sectionOrder = 0;
        containers::ArraySpan<const EditorPreferenceFieldPresentation> fields;
    };

    struct EditorPreferenceFieldValue final
    {
        struct StringValue final
        {
            u16 byteCount = 0;
            char bytes[MaximumEditorPreferenceStringBytes + 1u]{};
        };

        union Value
        {
            bool boolean;
            i64 signedInteger;
            u64 unsignedBitMask;
            StringValue string;

            constexpr Value() noexcept : unsignedBitMask(0) {}
        };

        EditorPreferenceValueType type = EditorPreferenceValueType::Bool;
        Value value;
    };

    using EditorPreferenceQueryRevision = u64 (*)(void* privateState) noexcept;
    using EditorPreferenceCapture = bool (*)(void* privateState, EditorPreferenceFieldId fieldId,
                                             EditorPreferenceFieldValue& value) noexcept;
    using EditorPreferenceValidate = bool (*)(void* privateState, EditorPreferenceFieldId fieldId,
                                              const EditorPreferenceFieldValue& value) noexcept;
    using EditorPreferenceApply = void (*)(void* privateState, EditorPreferenceFieldId fieldId,
                                           const EditorPreferenceFieldValue& value) noexcept;

    struct EditorPreferenceProviderDescriptor final
    {
        EditorPreferenceProviderId providerId = 0;
        containers::ArraySpan<const EditorPreferenceFieldDescriptor> fields;
        EditorPreferenceQueryRevision queryRevision = nullptr;
        EditorPreferenceCapture capture = nullptr;
        EditorPreferenceValidate validate = nullptr;
        EditorPreferenceApply apply = nullptr;
        // Optional presentation metadata. Providers without this remain persistable but are not
        // exposed by the production Preferences panel. The descriptor, its field/choice spans and
        // all referenced strings are borrowed immutable data and must remain valid until this
        // provider is unregistered or its extension owner is revoked.
        const EditorPreferenceProviderPresentation* presentation = nullptr;
    };

    struct EditorTypedPreferenceRecord final
    {
        EditorExtensionId extension;
        EditorPreferenceProviderId providerId = 0;
        EditorPreferenceFieldId fieldId = 0;
        EditorPreferenceFieldValue value;
    };

    enum class EditorTypedPreferenceItemResult : u8
    {
        Valid,
        MissingProvider,
        MissingField,
        Invalid
    };

    enum class EditorPreferenceFailureCode : u8
    {
        None,
        WrongThread,
        WrongPhase,
        InvalidDescriptor,
        DuplicateProvider,
        CapacityExceeded,
        InvalidToken,
        OwnerRevoked,
        CallbackFailure,
        ValidationFailed,
        TransactionMismatch,
        GenerationExhausted
    };

    struct EditorPreferenceFailure final
    {
        EditorPreferenceFailureCode code = EditorPreferenceFailureCode::None;
        EditorExtensionId extension;
        EditorPreferenceProviderId providerId = 0;
        EditorPreferenceFieldId fieldId = 0;
        const char* message = nullptr;
    };

    struct EditorPreferenceProviderView final
    {
        EditorExtensionOwner owner;
        EditorPreferenceProviderId providerId = 0;
        const EditorPreferenceProviderPresentation* presentation = nullptr;
    };

    using EditorPreferenceProviderVisitor = void (*)(const EditorPreferenceProviderView& provider,
                                                      void* userData) noexcept;
    struct EditorPanelLocalContextBinding final
    {
        EditorCommandContextTypeId contextType;
        EditorCommandContextMode mode = EditorCommandContextMode::Inherit;
    };

    class EditorExtensionRegistrationAccess
    {
    protected:
        EditorExtensionRegistrationAccess() noexcept = default;
        ~EditorExtensionRegistrationAccess() = default;

        [[nodiscard]] static bool RegisterOwnedCommand(EditorCommandService& service, const EditorExtensionOwner& owner,
                                                       const EditorCommandDescriptor& descriptor, EditorCommandToken& token,
                                                       EditorCommandFailure* failure = nullptr) noexcept
        {
            return service.RegisterCommandForOwner(owner, descriptor, token, failure);
        }

        [[nodiscard]] static bool UnregisterOwnedCommand(EditorCommandService& service, const EditorExtensionOwner& owner,
                                                         const EditorCommandToken token,
                                                         EditorCommandFailure* failure = nullptr) noexcept
        {
            return service.UnregisterCommandForOwner(owner, token, failure);
        }

        [[nodiscard]] static bool RegisterOwnedContextType(EditorCommandService& service, const EditorExtensionOwner& owner,
                                                           const EditorCommandContextTypeDescriptor& descriptor,
                                                           EditorCommandContextTypeToken& token,
                                                           EditorCommandFailure* failure = nullptr) noexcept
        {
            return service.RegisterContextTypeForOwner(owner, descriptor, token, failure);
        }

        [[nodiscard]] static bool UnregisterOwnedContextType(EditorCommandService& service, const EditorExtensionOwner& owner,
                                                             const EditorCommandContextTypeToken token,
                                                             EditorCommandFailure* failure = nullptr) noexcept
        {
            return service.UnregisterContextTypeForOwner(owner, token, failure);
        }

        [[nodiscard]] static bool RegisterOwnedHandler(EditorCommandService& service, const EditorExtensionOwner& owner,
                                                       const EditorCommandHandlerDescriptor& descriptor,
                                                       EditorCommandHandlerToken& token,
                                                       EditorCommandFailure* failure = nullptr) noexcept
        {
            return service.RegisterHandlerForOwner(owner, descriptor, token, failure);
        }

        [[nodiscard]] static bool UnregisterOwnedHandler(EditorCommandService& service, const EditorExtensionOwner& owner,
                                                         const EditorCommandHandlerToken token,
                                                         EditorCommandFailure* failure = nullptr) noexcept
        {
            return service.UnregisterHandlerForOwner(owner, token, failure);
        }

        static void RevokeCommandOwner(EditorCommandService& service, const EditorExtensionOwner& owner) noexcept
        {
            service.RevokeOwner(owner);
        }

        [[nodiscard]] static bool ValidateOwnedLocalContext(EditorCommandService& service, const EditorExtensionOwner& owner,
                                                           const EditorCommandContextTypeId contextType,
                                                           EditorCommandFailure* failure = nullptr) noexcept
        {
            return service.ValidateLocalContextForOwner(owner, contextType, failure);
        }

        [[nodiscard]] static bool ValidateOwnedCommand(EditorCommandService& service, const EditorExtensionOwner& owner,
                                                      const EditorCommandId command,
                                                      EditorCommandFailure* failure = nullptr) noexcept
        {
            return service.ValidateCommandForOwner(owner, command, failure);
        }

        [[nodiscard]] static bool QueryCommandPresentation(EditorCommandService& service, const EditorCommandId command,
                                                          EditorCommandPresentation& presentation,
                                                          EditorCommandFailure* failure = nullptr) noexcept
        {
            return service.QueryPresentation(command, presentation, failure);
        }

        [[nodiscard]] static bool RegisterOwnedPanelType(EditorPanelRegistry& registry, const EditorExtensionOwner& owner,
                                                        const EditorPanelTypeDesc& descriptor, EditorPanelTypeToken& token,
                                                        EditorUiFailure* failure = nullptr) noexcept
        {
            return registry.RegisterTypeForOwner(&owner, descriptor, token, failure);
        }

        [[nodiscard]] static bool UnregisterOwnedPanelType(EditorPanelRegistry& registry, const EditorExtensionOwner& owner,
                                                          const EditorPanelTypeToken token,
                                                          EditorUiFailure* failure = nullptr) noexcept
        {
            return registry.UnregisterTypeForOwner(&owner, token, failure);
        }

        [[nodiscard]] static bool CanRevokePanelOwner(const EditorPanelRegistry& registry,
                                                      const EditorExtensionOwner& owner) noexcept
        {
            return registry.CanRevokeOwner(owner);
        }

        static void RevokePanelOwner(EditorPanelRegistry& registry, const EditorExtensionOwner& owner) noexcept
        {
            registry.RevokeOwner(owner);
        }

        [[nodiscard]] static bool RegisterOwnedSurface(EditorUiService& ui, const EditorExtensionOwner& owner,
                                                      const detail::EditorUiSurfaceDesc& descriptor,
                                                      detail::EditorUiSurfaceToken& token,
                                                      EditorUiFailure* failure = nullptr) noexcept
        {
            return ui.RegisterSurfaceForOwner(owner, descriptor, token, failure);
        }

        [[nodiscard]] static bool UnregisterOwnedSurface(EditorUiService& ui, const EditorExtensionOwner& owner,
                                                        const detail::EditorUiSurfaceToken token,
                                                        EditorUiFailure* failure = nullptr) noexcept
        {
            return ui.UnregisterSurfaceForOwner(owner, token, failure);
        }

        [[nodiscard]] static bool RegisterOwnedContribution(EditorUiService& ui, const EditorExtensionOwner& owner,
                                                           const detail::EditorUiContributionDesc& descriptor,
                                                           detail::EditorUiContributionToken& token,
                                                           EditorUiFailure* failure = nullptr) noexcept
        {
            return ui.RegisterContributionForOwner(owner, descriptor, token, failure);
        }

        [[nodiscard]] static bool UnregisterOwnedContribution(EditorUiService& ui, const EditorExtensionOwner& owner,
                                                             const detail::EditorUiContributionToken token,
                                                             EditorUiFailure* failure = nullptr) noexcept
        {
            return ui.UnregisterContributionForOwner(owner, token, failure);
        }

        static void RevokeSurfaceOwner(EditorUiService& ui, const EditorExtensionOwner& owner) noexcept
        {
            ui.RevokeSurfaceOwner(owner);
        }

        [[nodiscard]] static bool RegisterOwnedActivitySource(detail::EditorActivityPrivateService& service,
                                                             const EditorExtensionOwner& owner,
                                                             const detail::EditorActivitySourceDescriptor& descriptor,
                                                             detail::EditorActivitySourceToken& token,
                                                             EditorActivityFailure* failure = nullptr) noexcept
        {
            return service.RegisterSourceForOwner(owner, descriptor, token, failure);
        }

        [[nodiscard]] static bool UnregisterOwnedActivitySource(detail::EditorActivityPrivateService& service,
                                                               const EditorExtensionOwner& owner,
                                                               const detail::EditorActivitySourceToken token,
                                                               EditorActivityFailure* failure = nullptr) noexcept
        {
            return service.UnregisterSourceForOwner(owner, token, failure);
        }

        [[nodiscard]] static bool RevokeActivityOwner(detail::EditorActivityPrivateService& service,
                                                      const EditorExtensionOwner& owner,
                                                      EditorActivityFailure* failure = nullptr) noexcept
        {
            return service.RevokeSourceOwner(owner, failure);
        }

        [[nodiscard]] static bool RegisterSurfaceCommandBridge(EditorUiService& ui,
                                                              const detail::EditorUiCommandBridge& bridge,
                                                              detail::EditorUiCommandBridgeToken& token,
                                                              EditorUiFailure* failure = nullptr) noexcept
        {
            return ui.RegisterSurfaceCommandBridge(bridge, token, failure);
        }

        [[nodiscard]] static bool UnregisterSurfaceCommandBridge(EditorUiService& ui,
                                                                const detail::EditorUiCommandBridgeToken& token,
                                                                EditorUiFailure* failure = nullptr) noexcept
        {
            return ui.UnregisterSurfaceCommandBridge(token, failure);
        }

        [[nodiscard]] static bool RegisterPanelFocusSink(EditorUiService& ui, detail::EditorPanelFocusSink sink,
                                                        void* userData, detail::EditorPanelFocusSinkToken& token,
                                                        EditorUiFailure* failure = nullptr) noexcept
        {
            return ui.RegisterPanelFocusSink(sink, userData, token, failure);
        }

        [[nodiscard]] static bool UnregisterPanelFocusSink(EditorUiService& ui,
                                                          const detail::EditorPanelFocusSinkToken token,
                                                          EditorUiFailure* failure = nullptr) noexcept
        {
            return ui.UnregisterPanelFocusSink(token, failure);
        }

    private:
        [[nodiscard]] virtual bool RegisterCommand(EditorExtensionOwner owner, const EditorCommandDescriptor& descriptor,
                                                   EditorCommandToken& token, EditorCommandFailure* failure) noexcept = 0;
        [[nodiscard]] virtual bool RegisterContextType(EditorExtensionOwner owner,
                                                       const EditorCommandContextTypeDescriptor& descriptor,
                                                       EditorCommandContextTypeToken& token,
                                                       EditorCommandFailure* failure) noexcept = 0;
        [[nodiscard]] virtual bool RegisterHandler(EditorExtensionOwner owner, const EditorCommandHandlerDescriptor& descriptor,
                                                   EditorCommandHandlerToken& token, EditorCommandFailure* failure) noexcept = 0;
        [[nodiscard]] virtual bool RegisterPanelType(EditorExtensionOwner owner, const EditorPanelTypeDesc& descriptor,
                                                    const EditorPanelLocalContextBinding* localContext,
                                                    EditorPanelTypeToken& token, EditorUiFailure* failure) noexcept = 0;
        [[nodiscard]] virtual bool CreateOwnedPanel(EditorExtensionOwner owner, const EditorPanelDesc& descriptor,
                                                   EditorPanelHandle& panel, EditorUiFailure* failure) noexcept = 0;
        [[nodiscard]] virtual bool SetOwnedPanelOpen(EditorExtensionOwner owner, EditorPanelHandle panel,
                                                    bool open, EditorUiFailure* failure) noexcept = 0;
        [[nodiscard]] virtual bool IsOwnedPanelOpen(EditorExtensionOwner owner,
                                                   EditorPanelHandle panel) const noexcept = 0;
        [[nodiscard]] virtual bool RegisterContextMenuSurface(EditorExtensionOwner owner, EditorUiSurfaceId surface,
                                                             detail::EditorUiSurfaceToken& token,
                                                             EditorUiFailure* failure) noexcept = 0;
        [[nodiscard]] virtual bool RegisterSurfaceContribution(EditorExtensionOwner owner,
                                                              const detail::EditorUiContributionDesc& descriptor,
                                                              detail::EditorUiContributionToken& token,
                                                              EditorUiFailure* failure) noexcept = 0;
        [[nodiscard]] virtual bool RegisterActivitySource(EditorExtensionOwner owner,
                                                         const detail::EditorActivitySourceDescriptor& descriptor,
                                                         detail::EditorActivitySourceToken& token,
                                                         EditorActivityFailure* failure) noexcept = 0;
        [[nodiscard]] virtual bool RegisterPreferenceProvider(
            EditorExtensionOwner owner, const EditorPreferenceProviderDescriptor& descriptor,
            EditorPreferenceProviderToken& token, EditorPreferenceFailure* failure) noexcept = 0;
        [[nodiscard]] virtual bool UnregisterPreferenceProvider(
            EditorExtensionOwner owner, EditorPreferenceProviderToken token,
            EditorPreferenceFailure* failure) noexcept = 0;
        virtual void VisitPreferenceProviders(EditorPreferenceProviderVisitor visitor,
                                              void* userData) const noexcept = 0;
        [[nodiscard]] virtual bool CapturePreferenceValue(EditorExtensionId extension,
                                                         EditorPreferenceProviderId providerId,
                                                         EditorPreferenceFieldId fieldId,
                                                         EditorPreferenceFieldValue& value,
                                                         EditorPreferenceFailure* failure) const noexcept = 0;
        [[nodiscard]] virtual bool ApplyPreferenceValue(EditorExtensionId extension,
                                                       EditorPreferenceProviderId providerId,
                                                       EditorPreferenceFieldId fieldId,
                                                       const EditorPreferenceFieldValue& value,
                                                       EditorPreferenceFailure* failure) noexcept = 0;

        friend class EditorOwnedPanelAccess;
        friend class EditorPreferenceAccess;
        friend struct EditorExtensionActivationContext;
    };

    class EditorPreferenceAccess final
    {
    public:
        EditorPreferenceAccess() noexcept = default;

        void VisitProviders(EditorPreferenceProviderVisitor visitor, void* userData = nullptr) const noexcept
        {
            if (m_registrations != nullptr && visitor != nullptr)
                m_registrations->VisitPreferenceProviders(visitor, userData);
        }

        [[nodiscard]] bool Capture(EditorExtensionId extension, EditorPreferenceProviderId providerId,
                                   EditorPreferenceFieldId fieldId, EditorPreferenceFieldValue& value,
                                   EditorPreferenceFailure* failure = nullptr) const noexcept
        {
            value = {};
            return m_registrations != nullptr &&
                   m_registrations->CapturePreferenceValue(extension, providerId, fieldId, value, failure);
        }

        [[nodiscard]] bool Apply(EditorExtensionId extension, EditorPreferenceProviderId providerId,
                                 EditorPreferenceFieldId fieldId, const EditorPreferenceFieldValue& value,
                                 EditorPreferenceFailure* failure = nullptr) const noexcept
        {
            return m_registrations != nullptr &&
                   m_registrations->ApplyPreferenceValue(extension, providerId, fieldId, value, failure);
        }

    private:
        EditorPreferenceAccess(EditorExtensionRegistrationAccess& registrations) noexcept
            : m_registrations(&registrations)
        {
        }

        EditorExtensionRegistrationAccess* m_registrations = nullptr;
        friend struct EditorExtensionActivationContext;
    };

    class EditorOwnedPanelAccess final
    {
    public:
        EditorOwnedPanelAccess() noexcept = default;

        [[nodiscard]] bool CreatePanel(const EditorPanelDesc& descriptor, EditorPanelHandle& panel,
                                       EditorUiFailure* failure = nullptr) const noexcept
        {
            panel = {};
            return m_registrations != nullptr &&
                   m_registrations->CreateOwnedPanel(m_owner, descriptor, panel, failure);
        }

        [[nodiscard]] bool SetOpen(const EditorPanelHandle panel, const bool open,
                                   EditorUiFailure* failure = nullptr) const noexcept
        {
            return m_registrations != nullptr &&
                   m_registrations->SetOwnedPanelOpen(m_owner, panel, open, failure);
        }

        [[nodiscard]] bool IsOpen(const EditorPanelHandle panel) const noexcept
        {
            return m_registrations != nullptr &&
                   m_registrations->IsOwnedPanelOpen(m_owner, panel);
        }

    private:
        EditorOwnedPanelAccess(const EditorExtensionOwner owner,
                               EditorExtensionRegistrationAccess& registrations) noexcept
            : m_owner(owner), m_registrations(&registrations)
        {
        }

        EditorExtensionOwner m_owner;
        EditorExtensionRegistrationAccess* m_registrations = nullptr;

        friend struct EditorExtensionActivationContext;
    };

    enum class EditorShellState : u8
    {
        Offline,
        Ready,
        Activating,
        Active,
        Quiesced,
        Failed
    };

    struct EditorShellStatus
    {
        EditorShellState state = EditorShellState::Offline;
        u32 catalogCount = 0;
        u32 selectedCount = 0;
        u32 activeCount = 0;
        u64 activationGeneration = 0;
        EditorExtensionId currentExtension;
        // Borrowed from the immutable compiled catalog while this shell service lives.
        const char* currentTechnicalName = nullptr;
        EditorExtensionFailure failure;
    };

    struct EditorExtensionActivationContext final
    {
    public:
        [[nodiscard]] EditorExtensionId GetExtension() const noexcept { return m_extension; }
        [[nodiscard]] u64 GetActivationGeneration() const noexcept { return m_activationGeneration; }
        [[nodiscard]] EditorExtensionOwner GetOwner() const noexcept { return {m_extension, m_activationGeneration}; }
        [[nodiscard]] const ProjectWorkspaceService& GetWorkspace() const noexcept { return *m_workspace; }
        [[nodiscard]] EditorProjectAssetAccess GetProjectAssets() const noexcept
        {
            return m_workspace != nullptr ? EditorProjectAssetAccess(*m_workspace) : EditorProjectAssetAccess{};
        }
        [[nodiscard]] EditorProjectTexturePreviewPresentationAccess GetProjectTexturePreviewPresentation() const noexcept
        {
            return m_workspace != nullptr ? EditorProjectTexturePreviewPresentationAccess(*m_workspace)
                                          : EditorProjectTexturePreviewPresentationAccess{};
        }
        [[nodiscard]] EditorActivityService& GetActivity() const noexcept { return *m_activity; }
        [[nodiscard]] EditorUiService& GetUi() const noexcept { return *m_ui; }
        [[nodiscard]] EditorFileDialogService* GetFileDialogs() const noexcept { return m_fileDialogs; }
        [[nodiscard]] EditorPreferenceAccess GetPreferences() const noexcept
        {
            return m_registrations != nullptr ? EditorPreferenceAccess(*m_registrations)
                                              : EditorPreferenceAccess{};
        }
        [[nodiscard]] EditorOwnedPanelAccess GetOwnedPanels() const noexcept
        {
            return m_registrations != nullptr ? EditorOwnedPanelAccess(GetOwner(), *m_registrations)
                                              : EditorOwnedPanelAccess{};
        }
        [[nodiscard]] bool RegisterCommand(const EditorCommandDescriptor& descriptor, EditorCommandToken& token,
                                           EditorCommandFailure* failure = nullptr) noexcept
        {
            token = {};
            return m_registrations != nullptr && m_registrations->RegisterCommand(GetOwner(), descriptor, token, failure);
        }
        [[nodiscard]] bool RegisterContextType(const EditorCommandContextTypeDescriptor& descriptor,
                                               EditorCommandContextTypeToken& token,
                                               EditorCommandFailure* failure = nullptr) noexcept
        {
            token = {};
            return m_registrations != nullptr && m_registrations->RegisterContextType(GetOwner(), descriptor, token, failure);
        }
        [[nodiscard]] bool RegisterHandler(const EditorCommandHandlerDescriptor& descriptor, EditorCommandHandlerToken& token,
                                           EditorCommandFailure* failure = nullptr) noexcept
        {
            token = {};
            return m_registrations != nullptr && m_registrations->RegisterHandler(GetOwner(), descriptor, token, failure);
        }
        [[nodiscard]] bool RegisterPanelType(const EditorPanelTypeDesc& descriptor, EditorPanelTypeToken& token,
                                             EditorUiFailure* failure = nullptr) noexcept
        {
            token = {};
            return m_registrations != nullptr && m_registrations->RegisterPanelType(GetOwner(), descriptor, nullptr, token, failure);
        }
        [[nodiscard]] bool RegisterPanelType(const EditorPanelTypeDesc& descriptor,
                                             const EditorPanelLocalContextBinding& localContext,
                                             EditorPanelTypeToken& token,
                                             EditorUiFailure* failure = nullptr) noexcept
        {
            token = {};
            return m_registrations != nullptr &&
                   m_registrations->RegisterPanelType(GetOwner(), descriptor, &localContext, token, failure);
        }
        [[nodiscard]] bool RegisterContextMenuSurface(const EditorUiSurfaceId surface,
                                                      detail::EditorUiSurfaceToken& token,
                                                      EditorUiFailure* failure = nullptr) noexcept
        {
            token = {};
            return m_registrations != nullptr &&
                   m_registrations->RegisterContextMenuSurface(GetOwner(), surface, token, failure);
        }
        [[nodiscard]] bool RegisterSurfaceContribution(const detail::EditorUiContributionDesc& descriptor,
                                                       detail::EditorUiContributionToken& token,
                                                       EditorUiFailure* failure = nullptr) noexcept
        {
            token = {};
            return m_registrations != nullptr &&
                   m_registrations->RegisterSurfaceContribution(GetOwner(), descriptor, token, failure);
        }
        [[nodiscard]] bool RegisterActivitySource(const detail::EditorActivitySourceDescriptor& descriptor,
                                                  detail::EditorActivitySourceToken& token,
                                                  EditorActivityFailure* failure = nullptr) noexcept
        {
            token = {};
            return m_registrations != nullptr &&
                   m_registrations->RegisterActivitySource(GetOwner(), descriptor, token, failure);
        }
        [[nodiscard]] bool RegisterPreferenceProvider(
            const EditorPreferenceProviderDescriptor& descriptor, EditorPreferenceProviderToken& token,
            EditorPreferenceFailure* failure = nullptr) noexcept
        {
            token = {};
            return m_registrations != nullptr &&
                   m_registrations->RegisterPreferenceProvider(GetOwner(), descriptor, token, failure);
        }
        [[nodiscard]] bool UnregisterPreferenceProvider(
            const EditorPreferenceProviderToken token, EditorPreferenceFailure* failure = nullptr) noexcept
        {
            return m_registrations != nullptr &&
                   m_registrations->UnregisterPreferenceProvider(GetOwner(), token, failure);
        }

    private:
        EditorExtensionActivationContext(EditorExtensionId extension, u64 activationGeneration,
                                         ProjectWorkspaceService& workspace, EditorActivityService& activity,
                                         EditorUiService& ui, EditorFileDialogService* fileDialogs,
                                         EditorExtensionRegistrationAccess& registrations) noexcept
            : m_extension(extension), m_activationGeneration(activationGeneration), m_workspace(&workspace),
              m_activity(&activity), m_ui(&ui), m_fileDialogs(fileDialogs), m_registrations(&registrations)
        {
        }

        EditorExtensionId m_extension;
        u64 m_activationGeneration = 0;
        ProjectWorkspaceService* m_workspace = nullptr;
        EditorActivityService* m_activity = nullptr;
        EditorUiService* m_ui = nullptr;
        EditorFileDialogService* m_fileDialogs = nullptr;
        EditorExtensionRegistrationAccess* m_registrations = nullptr;

        friend class EditorShellServiceAccess;
    };

    struct EditorShellServiceConfig
    {
        // Borrowed production backing is immutable/process-static and must outlive
        // EngineHost construction plus the resulting shell service lifetime.
        containers::ArraySpan<const EditorExtensionDescriptor> extensions;
    };

    class EditorShellService : public application::Service
    {
    public:
        ~EditorShellService() override = default;

        [[nodiscard]] virtual EditorShellStatus GetStatus() const noexcept = 0;

    protected:
        EditorShellService() noexcept = default;
    };

    namespace detail
    {
        class EditorShellPersistencePrivateService : public EditorShellService
        {
        public:
            [[nodiscard]] virtual bool CaptureTypedPreferences(
                EditorTypedPreferenceRecord* records, u32 capacity, u32& count,
                EditorPreferenceFailure* failure = nullptr) const noexcept = 0;
            [[nodiscard]] virtual bool ValidateTypedPreferences(
                containers::ArraySpan<const EditorTypedPreferenceRecord> records,
                containers::ArraySpan<EditorTypedPreferenceItemResult> results,
                EditorPreferenceFailure* failure = nullptr) noexcept = 0;
            [[nodiscard]] virtual bool ApplyTypedPreferences(
                containers::ArraySpan<const EditorTypedPreferenceRecord> records,
                containers::ArraySpan<EditorTypedPreferenceItemResult> results,
                EditorPreferenceFailure* failure = nullptr) noexcept = 0;
            [[nodiscard]] virtual bool RefreshTypedPreferenceRevision(
                EditorPreferenceFailure* failure = nullptr) noexcept = 0;
            [[nodiscard]] virtual u64 GetTypedPreferenceRevision() const noexcept = 0;

        protected:
            EditorShellPersistencePrivateService() noexcept = default;
        };

        [[nodiscard]] inline EditorShellPersistencePrivateService* FindEditorShellPersistencePrivateService(
            application::ServiceContext& context) noexcept
        {
            return static_cast<EditorShellPersistencePrivateService*>(
                context.FindCapability(EditorShellCapabilityId));
        }
    } // namespace detail

    [[nodiscard]] bool RegisterEditorShellService(application::EngineHost& host, const EditorShellServiceConfig& config,
                                                  application::HostFailure* failure = nullptr) noexcept;
    [[nodiscard]] EditorShellService* FindEditorShellService(application::EngineHost& host) noexcept;
    [[nodiscard]] EditorShellService* FindEditorShellService(application::ServiceContext& context) noexcept;
} // namespace vanguard::editor
