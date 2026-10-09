#include <vanguard/editor/editor_static_extensions.hpp>

#include <vanguard/editor/editor_activity_service.hpp>
#include <vanguard/editor/editor_shell_service.hpp>
#include <vanguard/editor/editor_surfaces.hpp>
#include <vanguard/memory/memory.hpp>

#include <imgui.h>

#include <cstdio>
#include <new>

namespace
{
    namespace editor = vanguard::editor;

    inline constexpr editor::EditorExtensionId DemonstrationExtensionId{0x6564333764656d01ull};
    inline constexpr editor::EditorPanelTypeId DemonstrationPanelTypeId = 0x65643337646d7001ull;
    inline constexpr editor::EditorPanelInstanceId DemonstrationPanelInstanceId = 1u;
    inline constexpr editor::EditorCommandId DemonstrationCommandId{0x65643337636d6401ull};
    inline constexpr editor::EditorUiSurfaceId DemonstrationContextMenuSurfaceId{0x6564333763746d01ull};
    inline constexpr editor::EditorUiContributionId DemonstrationMainMenuContributionId{0x656433376d6e7501ull};
    inline constexpr editor::EditorUiContributionId DemonstrationToolbarContributionId{0x6564333774627201ull};
    inline constexpr editor::EditorUiContributionId DemonstrationContextContributionId{0x6564333763746301ull};
    inline constexpr editor::EditorActivitySourceId DemonstrationActivitySourceId = 0x6564333761637401ull;
    inline constexpr editor::EditorActivityItemId DemonstrationActivityItemId = 1u;
    inline constexpr editor::EditorPreferenceProviderId DemonstrationPreferenceProviderId = 0x6564333770726601ull;
    inline constexpr editor::EditorPreferenceFieldId DemonstrationDetailsPreferenceFieldId = 0x6564333770726602ull;

    struct DemonstrationState final
    {
        editor::EditorOwnedPanelAccess panels;
        editor::EditorPanelHandle panel;
        vanguard::u64 commandInvocations = 0;
        vanguard::u64 preferenceRevision = 1;
        bool showDetails = true;
    };

    void AdvancePreferenceRevision(DemonstrationState& state) noexcept
    {
        if (state.preferenceRevision == 0)
            return;
        state.preferenceRevision = state.preferenceRevision == ~vanguard::u64{0}
            ? 0
            : state.preferenceRevision + 1u;
    }

    [[nodiscard]] vanguard::u64 QueryPreferenceRevision(void* const privateState) noexcept
    {
        const auto* const state = static_cast<const DemonstrationState*>(privateState);
        return state != nullptr ? state->preferenceRevision : 0;
    }

    [[nodiscard]] bool CapturePreference(void* const privateState,
                                         const editor::EditorPreferenceFieldId fieldId,
                                         editor::EditorPreferenceFieldValue& value) noexcept
    {
        const auto* const state = static_cast<const DemonstrationState*>(privateState);
        if (state == nullptr || fieldId != DemonstrationDetailsPreferenceFieldId)
            return false;
        value = {};
        value.type = editor::EditorPreferenceValueType::Bool;
        value.value.boolean = state->showDetails;
        return true;
    }

    [[nodiscard]] bool ValidatePreference(void*, const editor::EditorPreferenceFieldId fieldId,
                                          const editor::EditorPreferenceFieldValue& value) noexcept
    {
        return fieldId == DemonstrationDetailsPreferenceFieldId &&
               value.type == editor::EditorPreferenceValueType::Bool;
    }

    void ApplyPreference(void* const privateState, const editor::EditorPreferenceFieldId fieldId,
                         const editor::EditorPreferenceFieldValue& value) noexcept
    {
        auto* const state = static_cast<DemonstrationState*>(privateState);
        if (state == nullptr || fieldId != DemonstrationDetailsPreferenceFieldId ||
            value.type != editor::EditorPreferenceValueType::Bool)
            return;
        if (state->showDetails == value.value.boolean)
            return;
        state->showDetails = value.value.boolean;
        AdvancePreferenceRevision(*state);
    }

    [[nodiscard]] bool QueryActivity(editor::detail::EditorActivitySourceWriter& writer,
                                     void* const userData,
                                     editor::EditorActivityFailure* const failure) noexcept
    {
        const auto* const state = static_cast<const DemonstrationState*>(userData);
        if (state == nullptr)
            return false;

        char detail[editor::MaximumEditorActivityDetailBytes]{};
        if (state->showDetails)
        {
            std::snprintf(detail, sizeof(detail), "Command invocations: %llu",
                          static_cast<unsigned long long>(state->commandInvocations));
        }

        editor::detail::EditorActivityItemDescriptor item;
        item.id = DemonstrationActivityItemId;
        item.generation = 1u;
        item.kind = editor::EditorActivityKind::Status;
        item.state = state->commandInvocations == 0
            ? editor::EditorActivityState::Idle
            : editor::EditorActivityState::Succeeded;
        item.attention = editor::EditorActivityAttention::None;
        item.cancellable = false;
        item.label = "Editor demo tool";
        item.detail = detail;
        return writer.Add(item, failure);
    }

    void* CreatePanel(const editor::EditorPanelInstanceId, void* const factoryData) noexcept
    {
        return factoryData;
    }

    void DestroyPanel(void*, void*) noexcept
    {
    }

    void DrawPanel(editor::EditorPanelContext& context, void* const panel,
                   void* const factoryData) noexcept
    {
        auto* const state = static_cast<DemonstrationState*>(panel != nullptr ? panel : factoryData);
        if (state == nullptr)
            return;

        ImGui::TextUnformatted("Editor extension demonstration");
        ImGui::TextDisabled("The same command is exposed through Tools, the toolbar, this panel's context menu, and Ctrl+Shift+D.");
        ImGui::Separator();
        ImGui::Text("Command invocations: %llu", static_cast<unsigned long long>(state->commandInvocations));

        if (ImGui::Checkbox("Show detailed status", &state->showDetails))
            AdvancePreferenceRevision(*state);

        if (state->showDetails)
            ImGui::TextDisabled("This setting is persisted through the Shell-owned typed preference path.");

        if (ImGui::Button("Open demo context menu"))
        {
            editor::EditorUiFailure failure;
            static_cast<void>(context.RequestContextMenu(DemonstrationContextMenuSurfaceId, &failure));
        }
    }

    editor::EditorCommandState QueryCommand(void*, void* const handlerData) noexcept
    {
        const auto* const state = static_cast<const DemonstrationState*>(handlerData);
        return state != nullptr && state->panel.IsValid()
            ? editor::EditorCommandState::Enabled()
            : editor::EditorCommandState::Disabled();
    }

    bool InvokeCommand(void*, void* const handlerData) noexcept
    {
        auto* const state = static_cast<DemonstrationState*>(handlerData);
        if (state == nullptr || !state->panel.IsValid())
            return false;
        editor::EditorUiFailure failure;
        if (!state->panels.SetOpen(state->panel, true, &failure))
            return false;
        if (state->commandInvocations != ~vanguard::u64{0})
            ++state->commandInvocations;
        return true;
    }

    bool ActivateDemonstration(editor::EditorExtensionActivationContext& context,
                               const void*, void*& privateState) noexcept
    {
        const vanguard::memory::MemoryBlock block =
            vanguard::memory::Allocate(vanguard::memory::PoolId::Tools,
                                       sizeof(DemonstrationState), alignof(DemonstrationState));
        if (!block)
            return false;

        auto* const state = ::new (block.address) DemonstrationState();
        state->panels = context.GetOwnedPanels();
        privateState = state;

        editor::EditorCommandFailure commandFailure;
        editor::EditorUiFailure uiFailure;
        editor::EditorActivityFailure activityFailure;
        editor::EditorPreferenceFailure preferenceFailure;

        editor::EditorCommandToken commandToken;
        const editor::EditorCommandDescriptor command{
            DemonstrationCommandId, "Run Editor Demo Action", editor::EditorCommandFlags::None};

        editor::EditorCommandHandlerToken handlerToken;
        editor::EditorCommandHandlerDescriptor handler;
        handler.contextType = editor::EditorWorkspaceCommandContextTypeId;
        handler.command = DemonstrationCommandId;
        handler.query = QueryCommand;
        handler.invoke = InvokeCommand;
        handler.handlerData = state;
        handler.defaultShortcut = {
            vanguard::input::Key::D,
            editor::EditorShortcutModifiers::Control | editor::EditorShortcutModifiers::Shift};

        editor::EditorPanelTypeToken panelTypeToken;
        editor::EditorPanelTypeDesc panelType;
        panelType.id = DemonstrationPanelTypeId;
        panelType.name = "Extension Demo";
        panelType.create = CreatePanel;
        panelType.destroy = DestroyPanel;
        panelType.draw = DrawPanel;
        panelType.factoryData = state;
        panelType.policy = editor::EditorPanelTypePolicy::Singleton;
        panelType.defaultInstance = DemonstrationPanelInstanceId;
        panelType.initialPlacement = editor::EditorPanelInitialPlacement::Right;

        editor::detail::EditorUiSurfaceToken contextSurfaceToken;

        editor::detail::EditorUiContributionToken mainMenuContributionToken;
        editor::detail::EditorUiContributionDesc mainMenuContribution;
        mainMenuContribution.id = DemonstrationMainMenuContributionId;
        mainMenuContribution.surface = editor::detail::EditorMainMenuSurfaceId;
        mainMenuContribution.kind = editor::detail::EditorUiContributionKind::Command;
        mainMenuContribution.parent = editor::detail::EditorMainMenuToolsAnchorId;
        mainMenuContribution.order = 100;
        mainMenuContribution.command = DemonstrationCommandId;
        mainMenuContribution.label = "Run Editor Demo Action";

        editor::detail::EditorUiContributionToken toolbarContributionToken;
        editor::detail::EditorUiContributionDesc toolbarContribution;
        toolbarContribution.id = DemonstrationToolbarContributionId;
        toolbarContribution.surface = editor::detail::EditorMainToolbarSurfaceId;
        toolbarContribution.kind = editor::detail::EditorUiContributionKind::Command;
        toolbarContribution.sibling = editor::detail::EditorMainToolbarStartAnchorId;
        toolbarContribution.placement = editor::detail::EditorUiPlacementRelation::After;
        toolbarContribution.order = 100;
        toolbarContribution.command = DemonstrationCommandId;
        toolbarContribution.label = "Demo";

        editor::detail::EditorUiContributionToken contextContributionToken;
        editor::detail::EditorUiContributionDesc contextContribution;
        contextContribution.id = DemonstrationContextContributionId;
        contextContribution.surface = DemonstrationContextMenuSurfaceId;
        contextContribution.kind = editor::detail::EditorUiContributionKind::Command;
        contextContribution.order = 10;
        contextContribution.command = DemonstrationCommandId;
        contextContribution.label = "Run Editor Demo Action";

        editor::detail::EditorActivitySourceToken activitySourceToken;
        editor::detail::EditorActivitySourceDescriptor activitySource;
        activitySource.id = DemonstrationActivitySourceId;
        activitySource.displayName = "Editor Demo Tool";
        activitySource.query = QueryActivity;
        activitySource.userData = state;

        constexpr editor::EditorPreferenceFieldDescriptor preferenceFields[]{
            {DemonstrationDetailsPreferenceFieldId, editor::EditorPreferenceValueType::Bool, 0}};
        editor::EditorPreferenceProviderDescriptor preferenceProvider;
        preferenceProvider.providerId = DemonstrationPreferenceProviderId;
        preferenceProvider.fields = preferenceFields;
        preferenceProvider.queryRevision = QueryPreferenceRevision;
        preferenceProvider.capture = CapturePreference;
        preferenceProvider.validate = ValidatePreference;
        preferenceProvider.apply = ApplyPreference;
        editor::EditorPreferenceProviderToken preferenceProviderToken;

        return context.RegisterCommand(command, commandToken, &commandFailure) &&
               context.RegisterHandler(handler, handlerToken, &commandFailure) &&
               context.RegisterPanelType(panelType, panelTypeToken, &uiFailure) &&
               context.RegisterContextMenuSurface(DemonstrationContextMenuSurfaceId,
                                                  contextSurfaceToken, &uiFailure) &&
               context.RegisterSurfaceContribution(mainMenuContribution,
                                                   mainMenuContributionToken, &uiFailure) &&
               context.RegisterSurfaceContribution(toolbarContribution,
                                                   toolbarContributionToken, &uiFailure) &&
               context.RegisterSurfaceContribution(contextContribution,
                                                   contextContributionToken, &uiFailure) &&
               context.RegisterActivitySource(activitySource, activitySourceToken,
                                              &activityFailure) &&
               context.RegisterPreferenceProvider(preferenceProvider,
                                                  preferenceProviderToken, &preferenceFailure) &&
               state->panels.CreatePanel(
                   {DemonstrationPanelInstanceId, DemonstrationPanelTypeId, "Extension Demo", true},
                   state->panel, &uiFailure);
    }

    void DeactivateDemonstration(editor::EditorExtensionActivationContext&,
                                 const void*, void* const privateState) noexcept
    {
        if (privateState == nullptr)
            return;
        auto* const state = static_cast<DemonstrationState*>(privateState);
        state->~DemonstrationState();
        vanguard::memory::MemoryBlock block{
            state, sizeof(DemonstrationState), vanguard::memory::PoolId::Tools};
        vanguard::memory::Free(block);
    }

    const editor::EditorExtensionDescriptor DemonstrationExtensions[]{
        {DemonstrationExtensionId,
         "editor-demo-tool",
         "Editor Demo Tool",
         {},
         editor::EditorExtensionSelectionPolicy::ProjectSelectable,
         ActivateDemonstration,
         DeactivateDemonstration,
         nullptr}};
} // namespace

namespace vanguard::editor
{
    containers::ArraySpan<const EditorExtensionDescriptor> GetEditorDemonstrationExtensions() noexcept
    {
        return DemonstrationExtensions;
    }
} // namespace vanguard::editor
