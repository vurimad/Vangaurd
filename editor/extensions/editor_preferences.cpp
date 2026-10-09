#include <vanguard/editor/editor_static_extensions.hpp>

#include <vanguard/editor/editor_shell_service.hpp>
#include <vanguard/editor/editor_surfaces.hpp>
#include <vanguard/editor/editor_visual_style.hpp>
#include <vanguard/memory/memory.hpp>

#include <imgui.h>

#include <cctype>
#include <cstdio>
#include <cstring>
#include <new>

namespace
{
    namespace editor = vanguard::editor;

    inline constexpr editor::EditorExtensionId PreferencesExtensionId{0x6564707265667301ull};
    inline constexpr editor::EditorPanelTypeId PreferencesPanelTypeId = 0x6564707265667001ull;
    inline constexpr editor::EditorPanelInstanceId PreferencesPanelInstanceId = 0x6564707265667002ull;
    inline constexpr editor::EditorCommandId OpenPreferencesCommandId{0x6564707265666301ull};
    inline constexpr editor::EditorUiContributionId PreferencesMenuContributionId{0x6564707265666d01ull};

    inline constexpr editor::EditorPreferenceProviderId EditorInterfacePreferenceProviderId = 0x6564707265666901ull;
    inline constexpr editor::EditorPreferenceFieldId EditorUiScalePreferenceFieldId = 0x6564707265666902ull;
    inline constexpr editor::EditorPreferenceCategoryId AppearancePreferenceCategoryId = 0x6564707265666101ull;
    inline constexpr editor::EditorPreferenceSectionId InterfacePreferenceSectionId = 0x6564707265666903ull;

    inline constexpr vanguard::u32 PreferencesSearchBytes = 96;
    inline constexpr vanguard::u32 PreferencesStatusBytes = 256;

    struct PreferencesState final
    {
        editor::EditorOwnedPanelAccess panels;
        editor::EditorPreferenceAccess preferences;
        editor::EditorUiService* ui = nullptr;
        editor::EditorPanelHandle panel;
        vanguard::u64 preferenceRevision = 1;
        vanguard::i64 uiScalePercent = 100;
        editor::EditorPreferenceCategoryId selectedCategory = AppearancePreferenceCategoryId;
        char search[PreferencesSearchBytes]{};
        char status[PreferencesStatusBytes]{};
        bool statusError = false;
    };

    void AdvancePreferenceRevision(PreferencesState& state) noexcept
    {
        if (state.preferenceRevision == 0)
            return;
        state.preferenceRevision = state.preferenceRevision == ~vanguard::u64{0}
                                       ? 0
                                       : state.preferenceRevision + 1u;
    }

    [[nodiscard]] bool IsSupportedUiScale(const vanguard::i64 percent) noexcept
    {
        switch (percent)
        {
        case 75:
        case 100:
        case 125:
        case 150:
        case 175:
        case 200:
            return true;
        default:
            return false;
        }
    }

    [[nodiscard]] vanguard::u64 QueryPreferenceRevision(void* const privateState) noexcept
    {
        const auto* const state = static_cast<const PreferencesState*>(privateState);
        return state != nullptr ? state->preferenceRevision : 0;
    }

    [[nodiscard]] bool CapturePreference(void* const privateState,
                                         const editor::EditorPreferenceFieldId fieldId,
                                         editor::EditorPreferenceFieldValue& value) noexcept
    {
        const auto* const state = static_cast<const PreferencesState*>(privateState);
        if (state == nullptr || fieldId != EditorUiScalePreferenceFieldId)
            return false;
        value = {};
        value.type = editor::EditorPreferenceValueType::SignedInteger;
        value.value.signedInteger = state->uiScalePercent;
        return true;
    }

    [[nodiscard]] bool ValidatePreference(void*, const editor::EditorPreferenceFieldId fieldId,
                                          const editor::EditorPreferenceFieldValue& value) noexcept
    {
        return fieldId == EditorUiScalePreferenceFieldId &&
               value.type == editor::EditorPreferenceValueType::SignedInteger &&
               IsSupportedUiScale(value.value.signedInteger);
    }

    void ApplyPreference(void* const privateState, const editor::EditorPreferenceFieldId fieldId,
                         const editor::EditorPreferenceFieldValue& value) noexcept
    {
        auto* const state = static_cast<PreferencesState*>(privateState);
        if (state == nullptr || state->ui == nullptr || fieldId != EditorUiScalePreferenceFieldId ||
            value.type != editor::EditorPreferenceValueType::SignedInteger ||
            !IsSupportedUiScale(value.value.signedInteger) ||
            state->uiScalePercent == value.value.signedInteger)
            return;

        editor::EditorUiFailure failure;
        const vanguard::f32 scale = static_cast<vanguard::f32>(value.value.signedInteger) / 100.0f;
        if (!state->ui->SetUserScale(scale, &failure))
        {
            std::snprintf(state->status, sizeof(state->status), "%s",
                          failure.message != nullptr ? failure.message : "Failed to apply editor UI scale");
            state->statusError = true;
            return;
        }
        state->uiScalePercent = value.value.signedInteger;
        state->status[0] = '\0';
        state->statusError = false;
        AdvancePreferenceRevision(*state);
    }

    struct PreferenceProviderCollection final
    {
        vanguard::containers::StaticArray<editor::EditorPreferenceProviderView,
                                          editor::MaximumEditorPreferenceProviders> providers;
    };

    void CollectProvider(const editor::EditorPreferenceProviderView& provider, void* const userData) noexcept
    {
        auto* const collection = static_cast<PreferenceProviderCollection*>(userData);
        if (collection == nullptr || provider.presentation == nullptr ||
            collection->providers.Size() >= editor::MaximumEditorPreferenceProviders)
            return;
        collection->providers.PushBack(provider);
    }

    [[nodiscard]] int CompareText(const char* const left, const char* const right) noexcept
    {
        if (left == right)
            return 0;
        if (left == nullptr)
            return -1;
        if (right == nullptr)
            return 1;
        return std::strcmp(left, right);
    }

    [[nodiscard]] bool ProviderLess(const editor::EditorPreferenceProviderView& left,
                                    const editor::EditorPreferenceProviderView& right) noexcept
    {
        const editor::EditorPreferenceProviderPresentation& a = *left.presentation;
        const editor::EditorPreferenceProviderPresentation& b = *right.presentation;
        if (a.categoryOrder != b.categoryOrder)
            return a.categoryOrder < b.categoryOrder;
        if (a.categoryId != b.categoryId)
        {
            const int labelOrder = CompareText(a.categoryLabel, b.categoryLabel);
            if (labelOrder != 0)
                return labelOrder < 0;
            return a.categoryId < b.categoryId;
        }
        if (a.sectionOrder != b.sectionOrder)
            return a.sectionOrder < b.sectionOrder;
        if (a.sectionId != b.sectionId)
        {
            const int labelOrder = CompareText(a.sectionLabel, b.sectionLabel);
            if (labelOrder != 0)
                return labelOrder < 0;
            return a.sectionId < b.sectionId;
        }
        if (left.owner.extension.value != right.owner.extension.value)
            return left.owner.extension.value < right.owner.extension.value;
        return left.providerId < right.providerId;
    }

    void SortProviders(PreferenceProviderCollection& collection) noexcept
    {
        for (vanguard::u32 index = 1; index < collection.providers.Size(); ++index)
        {
            const editor::EditorPreferenceProviderView value = collection.providers[index];
            vanguard::u32 insertion = index;
            while (insertion != 0 && ProviderLess(value, collection.providers[insertion - 1u]))
            {
                collection.providers[insertion] = collection.providers[insertion - 1u];
                --insertion;
            }
            collection.providers[insertion] = value;
        }
    }

    [[nodiscard]] bool ContainsInsensitive(const char* const text, const char* const needle) noexcept
    {
        if (needle == nullptr || needle[0] == '\0')
            return true;
        if (text == nullptr)
            return false;
        for (const char* start = text; *start != '\0'; ++start)
        {
            const char* a = start;
            const char* b = needle;
            while (*a != '\0' && *b != '\0' &&
                   std::tolower(static_cast<unsigned char>(*a)) ==
                       std::tolower(static_cast<unsigned char>(*b)))
            {
                ++a;
                ++b;
            }
            if (*b == '\0')
                return true;
        }
        return false;
    }

    [[nodiscard]] bool FieldMatchesSearch(const editor::EditorPreferenceProviderPresentation& provider,
                                          const editor::EditorPreferenceFieldPresentation& field,
                                          const char* const search) noexcept
    {
        return ContainsInsensitive(field.label, search) || ContainsInsensitive(field.description, search) ||
               ContainsInsensitive(provider.categoryLabel, search) ||
               ContainsInsensitive(provider.sectionLabel, search);
    }

    void RecordPreferenceFailure(PreferencesState& state,
                                 const editor::EditorPreferenceFailure& failure) noexcept
    {
        std::snprintf(state.status, sizeof(state.status), "%s",
                      failure.message != nullptr ? failure.message : "Preference update failed");
        state.statusError = true;
    }

    [[nodiscard]] bool ApplyFieldValue(PreferencesState& state,
                                       const editor::EditorPreferenceProviderView& provider,
                                       const editor::EditorPreferenceFieldPresentation& field,
                                       const editor::EditorPreferenceFieldValue& value) noexcept
    {
        editor::EditorPreferenceFailure failure;
        if (!state.preferences.Apply(provider.owner.extension, provider.providerId, field.fieldId,
                                     value, &failure))
        {
            RecordPreferenceFailure(state, failure);
            return false;
        }
        state.status[0] = '\0';
        state.statusError = false;
        return true;
    }

    void DrawPreferenceField(PreferencesState& state,
                             const editor::EditorPreferenceProviderView& provider,
                             const editor::EditorPreferenceFieldPresentation& field) noexcept
    {
        editor::EditorPreferenceFieldValue value;
        editor::EditorPreferenceFailure failure;
        if (!state.preferences.Capture(provider.owner.extension, provider.providerId, field.fieldId,
                                       value, &failure))
        {
            RecordPreferenceFailure(state, failure);
            return;
        }

        char stableId[80]{};
        std::snprintf(stableId, sizeof(stableId), "%016llx_%016llx_%016llx",
                      static_cast<unsigned long long>(provider.owner.extension.value),
                      static_cast<unsigned long long>(provider.providerId),
                      static_cast<unsigned long long>(field.fieldId));
        ImGui::PushID(stableId);

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(field.label);
        if (field.description != nullptr && field.description[0] != '\0')
        {
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + editor::detail::EditorVisualPx(260.0f));
            editor::detail::DrawEditorSecondaryText(field.description);
            ImGui::PopTextWrapPos();
        }

        ImGui::TableSetColumnIndex(1);
        ImGui::SetNextItemWidth(-1.0f);
        editor::EditorPreferenceControlKind control = field.control;
        if (control == editor::EditorPreferenceControlKind::Automatic)
        {
            switch (value.type)
            {
            case editor::EditorPreferenceValueType::Bool:
                control = editor::EditorPreferenceControlKind::Toggle;
                break;
            case editor::EditorPreferenceValueType::SignedInteger:
                control = editor::EditorPreferenceControlKind::SignedInteger;
                break;
            case editor::EditorPreferenceValueType::Utf8String:
                control = editor::EditorPreferenceControlKind::Text;
                break;
            case editor::EditorPreferenceValueType::UnsignedBitMask:
                break;
            }
        }

        switch (control)
        {
        case editor::EditorPreferenceControlKind::Toggle:
        {
            bool next = value.value.boolean;
            if (ImGui::Checkbox("##value", &next))
            {
                editor::EditorPreferenceFieldValue changed;
                changed.type = editor::EditorPreferenceValueType::Bool;
                changed.value.boolean = next;
                static_cast<void>(ApplyFieldValue(state, provider, field, changed));
            }
            break;
        }
        case editor::EditorPreferenceControlKind::Choice:
        {
            const char* selectedLabel = "Custom";
            for (const editor::EditorPreferenceChoiceDescriptor& choice : field.choices)
                if (choice.value == value.value.signedInteger)
                {
                    selectedLabel = choice.label;
                    break;
                }
            if (ImGui::BeginCombo("##value", selectedLabel))
            {
                for (const editor::EditorPreferenceChoiceDescriptor& choice : field.choices)
                {
                    const bool selected = choice.value == value.value.signedInteger;
                    if (ImGui::Selectable(choice.label, selected) && !selected)
                    {
                        editor::EditorPreferenceFieldValue changed;
                        changed.type = editor::EditorPreferenceValueType::SignedInteger;
                        changed.value.signedInteger = choice.value;
                        static_cast<void>(ApplyFieldValue(state, provider, field, changed));
                    }
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            break;
        }
        case editor::EditorPreferenceControlKind::SignedInteger:
        {
            vanguard::i64 next = value.value.signedInteger;
            const vanguard::i64 step = field.step;
            if (ImGui::InputScalar("##value", ImGuiDataType_S64, &next, &step))
            {
                if (next < field.minimum) next = field.minimum;
                if (next > field.maximum) next = field.maximum;
                editor::EditorPreferenceFieldValue changed;
                changed.type = editor::EditorPreferenceValueType::SignedInteger;
                changed.value.signedInteger = next;
                static_cast<void>(ApplyFieldValue(state, provider, field, changed));
            }
            break;
        }
        case editor::EditorPreferenceControlKind::Text:
        {
            char text[editor::MaximumEditorPreferenceStringBytes + 1u]{};
            const vanguard::u16 count = value.value.string.byteCount <= editor::MaximumEditorPreferenceStringBytes
                                            ? value.value.string.byteCount
                                            : static_cast<vanguard::u16>(editor::MaximumEditorPreferenceStringBytes);
            if (count != 0)
                std::memcpy(text, value.value.string.bytes, count);
            text[count] = '\0';
            if (ImGui::InputText("##value", text, sizeof(text), ImGuiInputTextFlags_EnterReturnsTrue))
            {
                editor::EditorPreferenceFieldValue changed;
                changed.type = editor::EditorPreferenceValueType::Utf8String;
                const size_t length = std::strlen(text);
                changed.value.string.byteCount = static_cast<vanguard::u16>(
                    length <= editor::MaximumEditorPreferenceStringBytes
                        ? length
                        : editor::MaximumEditorPreferenceStringBytes);
                if (changed.value.string.byteCount != 0)
                    std::memcpy(changed.value.string.bytes, text, changed.value.string.byteCount);
                changed.value.string.bytes[changed.value.string.byteCount] = '\0';
                static_cast<void>(ApplyFieldValue(state, provider, field, changed));
            }
            break;
        }
        case editor::EditorPreferenceControlKind::Automatic:
            if (value.type == editor::EditorPreferenceValueType::UnsignedBitMask)
                ImGui::TextDisabled("0x%016llX", static_cast<unsigned long long>(value.value.unsignedBitMask));
            break;
        }
        ImGui::PopID();
    }

    [[nodiscard]] bool ProviderHasVisibleField(const editor::EditorPreferenceProviderView& provider,
                                               const char* const search) noexcept
    {
        for (const editor::EditorPreferenceFieldPresentation& field : provider.presentation->fields)
            if (FieldMatchesSearch(*provider.presentation, field, search))
                return true;
        return false;
    }

    void DrawProviderSection(PreferencesState& state,
                             const editor::EditorPreferenceProviderView& provider) noexcept
    {
        const editor::EditorPreferenceProviderPresentation& presentation = *provider.presentation;
        editor::detail::DrawEditorSectionHeader(presentation.sectionLabel);
        if (presentation.description != nullptr && presentation.description[0] != '\0')
            editor::detail::DrawEditorSecondaryText(presentation.description);

        const ImGuiTableFlags flags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_PadOuterX;
        char tableId[64]{};
        std::snprintf(tableId, sizeof(tableId), "##preferences_%016llx_%016llx",
                      static_cast<unsigned long long>(provider.owner.extension.value),
                      static_cast<unsigned long long>(provider.providerId));
        if (!ImGui::BeginTable(tableId, 2, flags))
            return;
        ImGui::TableSetupColumn("Setting", ImGuiTableColumnFlags_WidthFixed,
                                editor::detail::EditorVisualPx(300.0f));
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 1.0f);

        const editor::EditorPreferenceFieldPresentation* ordered[editor::MaximumEditorPreferenceFieldsPerProvider]{};
        vanguard::u32 orderedCount = 0;
        for (const editor::EditorPreferenceFieldPresentation& field : presentation.fields)
            if (FieldMatchesSearch(presentation, field, state.search))
            {
                vanguard::u32 insertion = orderedCount;
                while (insertion != 0 && ordered[insertion - 1u]->order > field.order)
                {
                    ordered[insertion] = ordered[insertion - 1u];
                    --insertion;
                }
                ordered[insertion] = &field;
                ++orderedCount;
            }
        for (vanguard::u32 index = 0; index < orderedCount; ++index)
            DrawPreferenceField(state, provider, *ordered[index]);
        ImGui::EndTable();
    }

    void* CreatePreferencesPanel(const editor::EditorPanelInstanceId, void* const factoryData) noexcept
    {
        return factoryData;
    }

    void DestroyPreferencesPanel(void*, void*) noexcept
    {
    }

    void DrawPreferencesPanel(editor::EditorPanelContext&, void* const panel,
                              void* const factoryData) noexcept
    {
        auto* const state = static_cast<PreferencesState*>(panel != nullptr ? panel : factoryData);
        if (state == nullptr)
            return;

        PreferenceProviderCollection collection;
        state->preferences.VisitProviders(CollectProvider, &collection);
        SortProviders(collection);

        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##PreferencesSearch", "Search preferences", state->search,
                                 sizeof(state->search));
        ImGui::Separator();

        if (!collection.providers.Empty() && state->selectedCategory == 0)
            state->selectedCategory = collection.providers[0].presentation->categoryId;

        const float navigationWidth = editor::detail::EditorVisualPx(170.0f);
        ImGui::BeginChild("##PreferencesCategories", {navigationWidth, 0.0f}, false,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        editor::EditorPreferenceCategoryId previousCategory = 0;
        for (const editor::EditorPreferenceProviderView& provider : collection.providers)
        {
            const editor::EditorPreferenceProviderPresentation& presentation = *provider.presentation;
            if (presentation.categoryId == previousCategory)
                continue;
            previousCategory = presentation.categoryId;
            const bool selected = state->selectedCategory == presentation.categoryId;
            if (ImGui::Selectable(presentation.categoryLabel, selected))
                state->selectedCategory = presentation.categoryId;
        }
        ImGui::EndChild();

        ImGui::SameLine();
        ImGui::BeginChild("##PreferencesContent", {0.0f, 0.0f}, false);
        const bool searching = state->search[0] != '\0';
        editor::EditorPreferenceCategoryId lastRenderedCategory = 0;
        bool renderedAny = false;
        for (const editor::EditorPreferenceProviderView& provider : collection.providers)
        {
            const editor::EditorPreferenceProviderPresentation& presentation = *provider.presentation;
            if ((!searching && presentation.categoryId != state->selectedCategory) ||
                !ProviderHasVisibleField(provider, state->search))
                continue;
            if (searching && presentation.categoryId != lastRenderedCategory)
            {
                if (renderedAny)
                    ImGui::Spacing();
                ImGui::TextDisabled("%s", presentation.categoryLabel);
                lastRenderedCategory = presentation.categoryId;
            }
            DrawProviderSection(*state, provider);
            ImGui::Spacing();
            renderedAny = true;
        }
        if (!renderedAny)
            ImGui::TextDisabled(searching ? "No preferences match your search." : "No preferences are registered in this category.");
        if (state->status[0] != '\0')
        {
            ImGui::Separator();
            editor::detail::DrawEditorToneText(state->status,
                                               state->statusError ? editor::detail::EditorVisualTone::Error
                                                                  : editor::detail::EditorVisualTone::Neutral);
        }
        ImGui::EndChild();
    }

    editor::EditorCommandState QueryOpenPreferences(void*, void* const handlerData) noexcept
    {
        auto* const state = static_cast<PreferencesState*>(handlerData);
        if (state == nullptr || !state->panel.IsValid())
            return editor::EditorCommandState::Disabled();
        return editor::EditorCommandState::Enabled(state->panels.IsOpen(state->panel));
    }

    bool InvokeOpenPreferences(void*, void* const handlerData) noexcept
    {
        auto* const state = static_cast<PreferencesState*>(handlerData);
        if (state == nullptr || !state->panel.IsValid())
            return false;
        editor::EditorUiFailure failure;
        return state->panels.SetOpen(state->panel, true, &failure);
    }

    [[nodiscard]] bool EnsurePreferencesPanel(PreferencesState& state) noexcept
    {
        editor::EditorUiFailure failure;
        editor::EditorPanelHandle panel;
        if (!state.panels.CreatePanel(
                {PreferencesPanelInstanceId, PreferencesPanelTypeId, "Preferences", false}, panel, &failure))
            return false;
        state.panel = panel;
        return true;
    }

    bool ActivatePreferences(editor::EditorExtensionActivationContext& context, const void*, void*& privateState) noexcept
    {
        const vanguard::memory::MemoryBlock block =
            vanguard::memory::Allocate(vanguard::memory::PoolId::Tools,
                                       sizeof(PreferencesState), alignof(PreferencesState));
        if (!block)
            return false;

        auto* const state = ::new (block.address) PreferencesState();
        state->panels = context.GetOwnedPanels();
        state->preferences = context.GetPreferences();
        state->ui = &context.GetUi();
        const vanguard::f32 currentScale = state->ui->GetUserScale();
        const vanguard::i64 currentPercent = static_cast<vanguard::i64>(currentScale * 100.0f + 0.5f);
        state->uiScalePercent = IsSupportedUiScale(currentPercent) ? currentPercent : 100;
        privateState = state;

        editor::EditorCommandFailure commandFailure;
        editor::EditorUiFailure uiFailure;
        editor::EditorPreferenceFailure preferenceFailure;

        editor::EditorCommandToken commandToken;
        const editor::EditorCommandDescriptor command{
            OpenPreferencesCommandId, "Open Preferences", editor::EditorCommandFlags::None};

        editor::EditorCommandHandlerToken handlerToken;
        editor::EditorCommandHandlerDescriptor handler;
        handler.contextType = editor::EditorWorkspaceCommandContextTypeId;
        handler.command = OpenPreferencesCommandId;
        handler.query = QueryOpenPreferences;
        handler.invoke = InvokeOpenPreferences;
        handler.handlerData = state;

        editor::EditorPanelTypeToken panelTypeToken;
        editor::EditorPanelTypeDesc panelType;
        panelType.id = PreferencesPanelTypeId;
        panelType.name = "Preferences";
        panelType.create = CreatePreferencesPanel;
        panelType.destroy = DestroyPreferencesPanel;
        panelType.draw = DrawPreferencesPanel;
        panelType.factoryData = state;
        panelType.policy = editor::EditorPanelTypePolicy::Singleton;
        panelType.defaultInstance = PreferencesPanelInstanceId;
        panelType.initialPlacement = editor::EditorPanelInitialPlacement::Center;

        editor::detail::EditorUiContributionToken menuContributionToken;
        editor::detail::EditorUiContributionDesc menuContribution;
        menuContribution.id = PreferencesMenuContributionId;
        menuContribution.surface = editor::detail::EditorMainMenuSurfaceId;
        menuContribution.kind = editor::detail::EditorUiContributionKind::Command;
        menuContribution.parent = editor::detail::EditorMainMenuEditAnchorId;
        menuContribution.order = 900;
        menuContribution.command = OpenPreferencesCommandId;
        menuContribution.label = "Preferences...";

        static constexpr editor::EditorPreferenceChoiceDescriptor UiScaleChoices[]{
            {75, "75%"}, {100, "100%"}, {125, "125%"},
            {150, "150%"}, {175, "175%"}, {200, "200%"}};
        static const editor::EditorPreferenceFieldPresentation UiPreferenceFields[]{
            {EditorUiScalePreferenceFieldId,
             "UI scale",
             "Scales editor text, controls, chrome, and panel geometry on top of the operating-system display scale.",
             editor::EditorPreferenceControlKind::Choice,
             UiScaleChoices,
             75,
             200,
             25,
             10}};
        static const editor::EditorPreferenceProviderPresentation UiPreferencePresentation{
            AppearancePreferenceCategoryId,
            "Appearance",
            10,
            InterfacePreferenceSectionId,
            "Interface",
            "Editor-wide interface presentation. Changes apply live and are persisted in user state.",
            10,
            UiPreferenceFields};
        static constexpr editor::EditorPreferenceFieldDescriptor PreferenceFields[]{
            {EditorUiScalePreferenceFieldId, editor::EditorPreferenceValueType::SignedInteger, 0}};

        editor::EditorPreferenceProviderDescriptor preferenceProvider;
        preferenceProvider.providerId = EditorInterfacePreferenceProviderId;
        preferenceProvider.fields = PreferenceFields;
        preferenceProvider.queryRevision = QueryPreferenceRevision;
        preferenceProvider.capture = CapturePreference;
        preferenceProvider.validate = ValidatePreference;
        preferenceProvider.apply = ApplyPreference;
        preferenceProvider.presentation = &UiPreferencePresentation;
        editor::EditorPreferenceProviderToken preferenceToken;

        return context.RegisterCommand(command, commandToken, &commandFailure) &&
               context.RegisterHandler(handler, handlerToken, &commandFailure) &&
               context.RegisterPanelType(panelType, panelTypeToken, &uiFailure) &&
               context.RegisterSurfaceContribution(menuContribution, menuContributionToken, &uiFailure) &&
               context.RegisterPreferenceProvider(preferenceProvider, preferenceToken, &preferenceFailure) &&
               EnsurePreferencesPanel(*state);
    }

    void DeactivatePreferences(editor::EditorExtensionActivationContext&,
                               const void*, void* const privateState) noexcept
    {
        if (privateState == nullptr)
            return;
        auto* const state = static_cast<PreferencesState*>(privateState);
        state->~PreferencesState();
        vanguard::memory::MemoryBlock block{
            state, sizeof(PreferencesState), vanguard::memory::PoolId::Tools};
        vanguard::memory::Free(block);
    }

    const editor::EditorExtensionDescriptor PreferencesExtensions[]{
        {PreferencesExtensionId,
         "editor-preferences",
         "Editor Preferences",
         {},
         editor::EditorExtensionSelectionPolicy::BuiltIn,
         ActivatePreferences,
         DeactivatePreferences,
         nullptr}};
} // namespace

namespace vanguard::editor
{
    containers::ArraySpan<const EditorExtensionDescriptor> GetEditorPreferencesExtensions() noexcept
    {
        return PreferencesExtensions;
    }
} // namespace vanguard::editor
