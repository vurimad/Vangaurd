#include <vanguard/editor/editor_static_extensions.hpp>

#include <vanguard/editor/editor_activity.hpp>
#include <vanguard/editor/editor_activity_preferences.hpp>
#include <vanguard/editor/editor_console_persistence.hpp>
#include <vanguard/editor/editor_shell_service.hpp>
#include <vanguard/editor/editor_surfaces.hpp>
#include <vanguard/editor/editor_visual_style.hpp>
#include <vanguard/memory/memory.hpp>

#include <imgui.h>

#include <cstdio>
#include <cstring>
#include <new>

namespace
{
    namespace diagnostics = vanguard::diagnostics;
    namespace editor = vanguard::editor;

    inline constexpr editor::EditorExtensionId ActivityPresentationExtensionId =
        editor::detail::ActivityPresentationExtensionId;
    inline constexpr editor::EditorPanelTypeId ConsolePanelTypeId{0x65643335636f6e01ull};
    inline constexpr editor::EditorPanelInstanceId ConsolePanelInstanceId{0x65643335636f6e02ull};
    inline constexpr editor::EditorCommandId OpenConsoleCommandId{0x65643335636f6e03ull};
    inline constexpr editor::EditorUiContributionId ConsoleViewContributionId{0x65643335636f6e04ull};
    inline constexpr editor::EditorPanelTypeId BackgroundActivityPanelTypeId{0x6564333562616301ull};
    inline constexpr editor::EditorPanelInstanceId BackgroundActivityPanelInstanceId{0x6564333562616302ull};
    inline constexpr editor::EditorCommandId OpenBackgroundActivityCommandId{0x6564333562616303ull};
    inline constexpr editor::EditorUiContributionId BackgroundActivityViewContributionId{0x6564333562616304ull};
    inline constexpr editor::EditorPreferenceProviderId ActivityPreferenceProviderId =
        editor::detail::ActivityPreferenceProviderId;
    inline constexpr editor::EditorPreferenceFieldId ActivityLevelsPreferenceFieldId =
        editor::detail::ActivityLevelsPreferenceFieldId;
    inline constexpr editor::EditorPreferenceFieldId ActivityCategoryPreferenceFieldId =
        editor::detail::ActivityCategoryPreferenceFieldId;
    inline constexpr editor::EditorPreferenceFieldId ActivityTextPreferenceFieldId =
        editor::detail::ActivityTextPreferenceFieldId;

    inline constexpr vanguard::u32 DiagnosticPageSize = 16;
    inline constexpr vanguard::u32 DiagnosticLevelCount = editor::detail::ActivityPreferenceLevelCount;
    inline constexpr vanguard::u32 DiagnosticTextFilterBytes = editor::detail::ActivityPreferenceTextBytes;

    constexpr const char* DiagnosticLevelNames[]{
        "Fatal", "Error", "Warning", "Info", "Debug", "Trace"};

    constexpr const char* DiagnosticCategoryNames[]{
        "Default", "AI", "Animation", "Audio", "Engine", "Gameplay", "Gameplay Profile",
        "Game State Machine", "Physics", "Rendering", "Scripts", "Script Runtime Errors",
        "Tools", "Core", "Interop", "Input", "TweakDB", "Math", "Item Factory",
        "Localization", "User Interface", "Object Pool", "Scenes", "Data Processing",
        "Entity Validation", "Jobs", "Navigation", "Traffic", "Population",
        "Player Locomotion", "Services", "Resources", "Multiplayer", "Effects", "Workspots",
        "Distributed Process", "Data Build", "Entity", "Smart Objects", "Scanning",
        "Interactions", "Muppet", "Ping System", "Chatter", "Screenshot Tool",
        "AI Director", "Resource Lookup Table", "Player Breadcrumbs", "Functional Tests",
        "Characters", "GPS", "Player Manager", "Mounting", "Projectiles", "Telemetry",
        "Garment"};

    static_assert(static_cast<vanguard::u32>(diagnostics::Category::Count) ==
                  sizeof(DiagnosticCategoryNames) / sizeof(DiagnosticCategoryNames[0]));

    struct ActivityPresentationState final
    {
        editor::EditorActivityService* activity = nullptr;
        editor::EditorOwnedPanelAccess panels;
        editor::EditorPanelHandle consolePanel;
        editor::EditorPanelHandle backgroundActivityPanel;
        editor::EditorActivitySourceSnapshot activitySources[editor::MaximumEditorActivitySources]{};
        editor::EditorActivityItem activityItems[editor::MaximumEditorActivityRows]{};
        bool levels[DiagnosticLevelCount]{true, true, true, true, true, true};
        vanguard::i32 category = -1;
        char text[DiagnosticTextFilterBytes]{};
        vanguard::u64 preferenceRevision = 1;
        bool consoleScrollLock = true;
        bool consolePanelContentRestored = false;
    };

    void AdvancePreferenceRevision(ActivityPresentationState& state) noexcept
    {
        editor::detail::AdvanceActivityPreferenceRevision(state.preferenceRevision);
    }

    [[nodiscard]] vanguard::u64 QueryActivityPreferenceRevision(void* const privateState) noexcept
    {
        const auto* const state = static_cast<const ActivityPresentationState*>(privateState);
        return state != nullptr ? state->preferenceRevision : 0;
    }

    [[nodiscard]] bool CaptureActivityPreference(
        void* const privateState, const editor::EditorPreferenceFieldId fieldId,
        editor::EditorPreferenceFieldValue& value) noexcept
    {
        const auto* const state = static_cast<const ActivityPresentationState*>(privateState);
        if (state == nullptr)
            return false;
        return editor::detail::CaptureActivityPreferenceValue(
            state->levels, state->category, state->text, fieldId, value);
    }

    [[nodiscard]] bool ValidateActivityPreference(
        void*, const editor::EditorPreferenceFieldId fieldId,
        const editor::EditorPreferenceFieldValue& value) noexcept
    {
        return editor::detail::ValidateLegacyConsolePreferenceForPreflight(fieldId, value);
    }

    void ApplyActivityPreference(
        void* const privateState, const editor::EditorPreferenceFieldId fieldId,
        const editor::EditorPreferenceFieldValue& value) noexcept
    {
        auto* const state = static_cast<ActivityPresentationState*>(privateState);
        if (state == nullptr)
            return;
        editor::detail::ApplyLegacyConsolePreferenceIfNeeded(
            state->consolePanelContentRestored, state->levels, state->category, state->text,
            state->preferenceRevision, static_cast<vanguard::u32>(diagnostics::Category::Count),
            fieldId, value);
    }

    [[nodiscard]] editor::detail::ConsoleContentPersistenceState CaptureConsoleContentState(
        const ActivityPresentationState& state) noexcept
    {
        editor::detail::ConsoleContentPersistenceState captured;
        for (vanguard::u32 index = 0u; index < DiagnosticLevelCount; ++index)
            captured.levels[index] = state.levels[index];
        captured.category = state.category;
        std::memcpy(captured.text, state.text, sizeof(captured.text));
        captured.scrollLock = state.consoleScrollLock;
        return captured;
    }

    [[nodiscard]] editor::EditorPanelStateResult CaptureConsolePanelState(
        const editor::EditorPanelInstanceId instance, void* const panel,
        const editor::EditorPanelStateScope scope, void* const destination,
        const vanguard::u32 capacity, vanguard::u32& byteCount, void* const factoryData) noexcept
    {
        byteCount = 0u;
        const auto* const state = static_cast<const ActivityPresentationState*>(panel);
        if (state == nullptr || state != factoryData || instance != ConsolePanelInstanceId)
            return editor::EditorPanelStateResult::CallbackFailure;
        if (scope == editor::EditorPanelStateScope::Presentation)
            return editor::EditorPanelStateResult::NoState;
        if (scope != editor::EditorPanelStateScope::Content)
            return editor::EditorPanelStateResult::InvalidData;
        const editor::detail::ConsoleContentPersistenceState captured = CaptureConsoleContentState(*state);
        return editor::detail::CaptureConsoleContentPersistence(
            captured, static_cast<vanguard::u32>(diagnostics::Category::Count),
            destination, capacity, byteCount);
    }

    [[nodiscard]] editor::EditorPanelStateResult RestoreConsolePanelState(
        const editor::EditorPanelInstanceId instance, void* const panel,
        const editor::EditorPanelStateScope scope, const vanguard::u32 version,
        const vanguard::containers::ArraySpan<const vanguard::u8> bytes,
        void* const factoryData) noexcept
    {
        auto* const state = static_cast<ActivityPresentationState*>(panel);
        if (state == nullptr || state != factoryData || instance != ConsolePanelInstanceId)
            return editor::EditorPanelStateResult::CallbackFailure;
        if (version != editor::detail::ConsolePanelStateVersion)
            return editor::EditorPanelStateResult::UnsupportedVersion;
        if (scope != editor::EditorPanelStateScope::Content)
            return editor::EditorPanelStateResult::InvalidData;

        editor::detail::ConsoleContentPersistenceState restored;
        if (!editor::detail::DecodeConsoleContentPersistence(
                bytes, static_cast<vanguard::u32>(diagnostics::Category::Count), restored))
            return editor::EditorPanelStateResult::InvalidData;
        editor::detail::ApplyConsoleContentPersistenceState(
            restored, state->levels, state->category, state->text, state->consoleScrollLock,
            state->consolePanelContentRestored);
        return editor::EditorPanelStateResult::Success;
    }

    [[nodiscard]] vanguard::u64 GetConsolePanelStateRevision(
        const editor::EditorPanelInstanceId instance, void* const panel,
        void* const factoryData) noexcept
    {
        const auto* const state = static_cast<const ActivityPresentationState*>(panel);
        return state != nullptr && state == factoryData && instance == ConsolePanelInstanceId
                   ? state->preferenceRevision
                   : 0u;
    }

    [[nodiscard]] bool IsDiagnosticVisible(
        const ActivityPresentationState& state,
        const editor::EditorActivityDiagnosticRecord& record) noexcept
    {
        const vanguard::u32 level = static_cast<vanguard::u32>(record.level);
        if (level >= DiagnosticLevelCount || !state.levels[level])
            return false;
        if (state.category >= 0 &&
            static_cast<vanguard::u32>(state.category) !=
                static_cast<vanguard::u32>(record.category))
            return false;
        return state.text[0] == '\0' || std::strstr(record.message, state.text) != nullptr;
    }

    [[nodiscard]] const char* LevelName(const diagnostics::Level level) noexcept
    {
        const vanguard::u32 index = static_cast<vanguard::u32>(level);
        return index < DiagnosticLevelCount ? DiagnosticLevelNames[index] : "Unknown";
    }

    [[nodiscard]] const char* CategoryName(const diagnostics::Category category) noexcept
    {
        const vanguard::u32 index = static_cast<vanguard::u32>(category);
        return index < static_cast<vanguard::u32>(diagnostics::Category::Count)
            ? DiagnosticCategoryNames[index]
            : "Unknown";
    }

    [[nodiscard]] bool EnsureConsolePanel(ActivityPresentationState& state) noexcept
    {
        editor::EditorUiFailure failure;
        editor::EditorPanelHandle panel;
        if (!state.panels.CreatePanel(
                {ConsolePanelInstanceId, ConsolePanelTypeId, "Console", true}, panel, &failure))
            return false;
        state.consolePanel = panel;
        return true;
    }

    [[nodiscard]] bool EnsureBackgroundActivityPanel(ActivityPresentationState& state) noexcept
    {
        editor::EditorUiFailure failure;
        editor::EditorPanelHandle panel;
        if (!state.panels.CreatePanel(
                {BackgroundActivityPanelInstanceId, BackgroundActivityPanelTypeId,
                 "Background Activity", true}, panel, &failure))
            return false;
        state.backgroundActivityPanel = panel;
        return true;
    }

    void* CreateConsolePanel(const editor::EditorPanelInstanceId, void* const factoryData) noexcept
    {
        return factoryData;
    }

    void DestroyConsolePanel(void*, void*) noexcept
    {
    }

    void* CreateBackgroundActivityPanel(const editor::EditorPanelInstanceId,
                                        void* const factoryData) noexcept
    {
        return factoryData;
    }

    void DestroyBackgroundActivityPanel(void*, void*) noexcept
    {
    }

    void DrawConsoleFilters(ActivityPresentationState& state) noexcept
    {
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("Levels");
        ImGui::SameLine();
        for (vanguard::u32 index = 0; index < DiagnosticLevelCount; ++index)
        {
            if (index != 0)
                ImGui::SameLine();
            if (ImGui::Checkbox(DiagnosticLevelNames[index], &state.levels[index]))
                AdvancePreferenceRevision(state);
        }

        ImGui::SameLine();
        ImGui::TextDisabled("Category");
        ImGui::SameLine();
        const char* selectedCategory = state.category < 0
            ? "All categories"
            : DiagnosticCategoryNames[static_cast<vanguard::u32>(state.category)];
        ImGui::SetNextItemWidth(editor::detail::EditorVisualPx(145.0f));
        if (ImGui::BeginCombo("##ConsoleCategory", selectedCategory))
        {
            const bool allSelected = state.category < 0;
            if (ImGui::Selectable("All categories", allSelected) && state.category != -1)
            {
                state.category = -1;
                AdvancePreferenceRevision(state);
            }
            if (allSelected)
                ImGui::SetItemDefaultFocus();
            for (vanguard::u32 index = 0;
                 index < static_cast<vanguard::u32>(diagnostics::Category::Count); ++index)
            {
                const bool selected = state.category == static_cast<vanguard::i32>(index);
                if (ImGui::Selectable(DiagnosticCategoryNames[index], selected) &&
                    state.category != static_cast<vanguard::i32>(index))
                {
                    state.category = static_cast<vanguard::i32>(index);
                    AdvancePreferenceRevision(state);
                }
                if (selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        ImGui::SameLine();
        ImGui::SetNextItemWidth(editor::detail::EditorVisualPx(260.0f));
        if (ImGui::InputTextWithHint("##ConsoleTextFilter", "Filter message text", state.text,
                                     sizeof(state.text)))
            AdvancePreferenceRevision(state);
    }

    void DrawDiagnosticRecord(const editor::EditorActivityDiagnosticRecord& record) noexcept
    {
        ImGui::Text("[%s][%s] #%llu", LevelName(record.level), CategoryName(record.category),
                    static_cast<unsigned long long>(record.lastSequence));
        if (record.repeatCount > 1u)
        {
            ImGui::SameLine();
            ImGui::TextDisabled("x%u", record.repeatCount);
        }
        if (record.truncated)
        {
            ImGui::SameLine();
            ImGui::TextDisabled("[truncated]");
        }
        ImGui::SameLine();
        ImGui::TextUnformatted(record.message);
    }

    void DrawConsolePanel(editor::EditorPanelContext&, void* const panel,
                          void* const factoryData) noexcept
    {
        auto* const state = static_cast<ActivityPresentationState*>(
            panel != nullptr ? panel : factoryData);
        if (state == nullptr || state->activity == nullptr)
            return;

        DrawConsoleFilters(*state);

        editor::EditorActivityFailure clearFailure;
        if (ImGui::Button("Clear"))
            static_cast<void>(state->activity->ClearDiagnostics(&clearFailure));
        ImGui::SameLine();
        if (ImGui::Checkbox("Scroll Lock", &state->consoleScrollLock))
            AdvancePreferenceRevision(*state);

        const editor::EditorActivityServiceStatus status = state->activity->GetStatus();
        ImGui::SameLine();
        ImGui::TextDisabled(
            "retained=%u ingress=%u dropped=%llu evicted=%llu coalesced=%llu truncated=%llu",
            status.diagnosticsRetained, status.diagnosticsIngress,
            static_cast<unsigned long long>(status.diagnosticsDropped),
            static_cast<unsigned long long>(status.diagnosticsEvicted),
            static_cast<unsigned long long>(status.diagnosticsCoalesced),
            static_cast<unsigned long long>(status.diagnosticsTruncated));

        editor::detail::DrawEditorSectionHeader("Diagnostics");
        const bool childVisible =
            ImGui::BeginChild("##ConsoleDiagnostics", ImVec2(0.0f, 0.0f), false,
                              ImGuiWindowFlags_HorizontalScrollbar);
        if (childVisible)
        {
            editor::EditorActivityCursor cursor;
            bool sawGap = false;
            for (;;)
            {
                editor::EditorActivityDiagnosticRecord page[DiagnosticPageSize]{};
                editor::EditorActivityDiagnosticCapture capture;
                editor::EditorActivityFailure failure;
                if (!state->activity->CaptureDiagnostics(
                        cursor, {page, DiagnosticPageSize}, capture, &failure))
                {
                    ImGui::TextDisabled("Console capture unavailable.");
                    break;
                }

                sawGap = sawGap || capture.gap;
                for (vanguard::u32 index = 0; index < capture.copiedCount; ++index)
                    if (IsDiagnosticVisible(*state, page[index]))
                        DrawDiagnosticRecord(page[index]);

                if (capture.copiedCount == 0u ||
                    capture.cursor.diagnosticSequence == cursor.diagnosticSequence)
                    break;
                cursor = capture.cursor;
            }

            if (sawGap)
                ImGui::TextDisabled("Earlier retained diagnostics were evicted or dropped.");
            if (state->consoleScrollLock)
                ImGui::SetScrollY(ImGui::GetScrollMaxY());
        }

        ImGui::EndChild();
    }

    [[nodiscard]] const char* ActivityStateName(const editor::EditorActivityState state) noexcept
    {
        switch (state)
        {
        case editor::EditorActivityState::Idle: return "Idle";
        case editor::EditorActivityState::Pending: return "Pending";
        case editor::EditorActivityState::Running: return "Running";
        case editor::EditorActivityState::Cancelling: return "Cancelling";
        case editor::EditorActivityState::Succeeded: return "Succeeded";
        case editor::EditorActivityState::Failed: return "Failed";
        case editor::EditorActivityState::Cancelled: return "Cancelled";
        case editor::EditorActivityState::Blocked: return "Blocked";
        }
        return "Unknown";
    }

    [[nodiscard]] const char* ActivityAttentionName(
        const editor::EditorActivityAttention attention) noexcept
    {
        switch (attention)
        {
        case editor::EditorActivityAttention::None: return nullptr;
        case editor::EditorActivityAttention::Info: return "info";
        case editor::EditorActivityAttention::Warning: return "warning";
        case editor::EditorActivityAttention::Error: return "error";
        }
        return "unknown";
    }

    [[nodiscard]] editor::detail::EditorVisualTone ActivityStateTone(
        const editor::EditorActivityState state) noexcept
    {
        switch (state)
        {
        case editor::EditorActivityState::Running:
        case editor::EditorActivityState::Pending:
        case editor::EditorActivityState::Cancelling:
            return editor::detail::EditorVisualTone::Accent;
        case editor::EditorActivityState::Succeeded:
            return editor::detail::EditorVisualTone::Success;
        case editor::EditorActivityState::Failed:
        case editor::EditorActivityState::Blocked:
            return editor::detail::EditorVisualTone::Error;
        case editor::EditorActivityState::Cancelled:
        case editor::EditorActivityState::Idle:
            return editor::detail::EditorVisualTone::Neutral;
        }
        return editor::detail::EditorVisualTone::Neutral;
    }

    [[nodiscard]] editor::detail::EditorVisualTone ActivityAttentionTone(
        const editor::EditorActivityAttention attention) noexcept
    {
        switch (attention)
        {
        case editor::EditorActivityAttention::Info: return editor::detail::EditorVisualTone::Accent;
        case editor::EditorActivityAttention::Warning: return editor::detail::EditorVisualTone::Warning;
        case editor::EditorActivityAttention::Error: return editor::detail::EditorVisualTone::Error;
        case editor::EditorActivityAttention::None: return editor::detail::EditorVisualTone::Neutral;
        }
        return editor::detail::EditorVisualTone::Neutral;
    }

    void DrawActivityItem(ActivityPresentationState& state,
                          const editor::EditorActivityItem& item) noexcept
    {
        ImGui::TextUnformatted(item.label);
        ImGui::SameLine();
        char stateLabel[48]{};
        std::snprintf(stateLabel, sizeof(stateLabel), "[%s]", ActivityStateName(item.state));
        editor::detail::DrawEditorToneText(stateLabel, ActivityStateTone(item.state));
        if (const char* const attention = ActivityAttentionName(item.attention);
            attention != nullptr)
        {
            ImGui::SameLine();
            char attentionLabel[32]{};
            std::snprintf(attentionLabel, sizeof(attentionLabel), "[%s]", attention);
            editor::detail::DrawEditorToneText(attentionLabel, ActivityAttentionTone(item.attention));
        }

        if (item.detail[0] != '\0')
            editor::detail::DrawEditorSecondaryText(item.detail);

        if (item.kind == editor::EditorActivityKind::Progress && item.determinate &&
            item.total != 0u && item.completed <= item.total)
        {
            const float fraction = static_cast<float>(item.completed) /
                                   static_cast<float>(item.total);
            ImGui::ProgressBar(fraction, ImVec2(-1.0f, 0.0f));
        }

        if (item.cancellable)
        {
            if (item.cancelQueued)
            {
                ImGui::TextDisabled("Cancel queued");
            }
            else if (ImGui::Button("Cancel"))
            {
                editor::EditorActivityFailure failure;
                static_cast<void>(state.activity->RequestCancel(item.key, &failure));
            }
        }
    }

    void DrawBackgroundActivityPanel(editor::EditorPanelContext&, void* const panel,
                                     void* const factoryData) noexcept
    {
        auto* const state = static_cast<ActivityPresentationState*>(
            panel != nullptr ? panel : factoryData);
        if (state == nullptr || state->activity == nullptr)
            return;

        editor::EditorActivityCapture capture;
        editor::EditorActivityFailure failure;
        if (!state->activity->CaptureActivities(
                {}, {state->activitySources, editor::MaximumEditorActivitySources},
                {state->activityItems, editor::MaximumEditorActivityRows}, capture, &failure))
        {
            ImGui::TextDisabled("Background activity capture unavailable.");
            return;
        }

        ImGui::TextDisabled("generation %llu  |  sources %u  |  rows %u",
                            static_cast<unsigned long long>(capture.cursor.activityGeneration),
                            capture.sourceCount, capture.itemCount);

        for (vanguard::u32 sourceIndex = 0; sourceIndex < capture.sourceCount; ++sourceIndex)
        {
            const editor::EditorActivitySourceSnapshot& source =
                state->activitySources[sourceIndex];
            ImGui::PushID(static_cast<int>(sourceIndex));
            char sourceLabel[editor::MaximumEditorActivityLabelBytes + 16u]{};
            std::snprintf(sourceLabel, sizeof(sourceLabel), "%s%s", source.displayName,
                          source.stale ? " [stale]" : "");
            editor::detail::DrawEditorSectionHeader(sourceLabel);
            if (source.stale)
            {
                ImGui::TextDisabled("Last successful activity generation: %llu",
                                    static_cast<unsigned long long>(source.lastSuccessGeneration));
            }

            if (source.firstItem + source.itemCount <= capture.itemCount)
            {
                for (vanguard::u32 itemIndex = 0; itemIndex < source.itemCount; ++itemIndex)
                {
                    ImGui::PushID(static_cast<int>(itemIndex));
                    DrawActivityItem(*state,
                                     state->activityItems[source.firstItem + itemIndex]);
                    ImGui::PopID();
                }
            }
            else
            {
                ImGui::TextDisabled("Activity source snapshot is inconsistent.");
            }
            ImGui::PopID();
        }
    }

    editor::EditorCommandState QueryOpenConsole(void*, void* const handlerData) noexcept
    {
        auto* const state = static_cast<ActivityPresentationState*>(handlerData);
        if (state == nullptr)
            return editor::EditorCommandState::Disabled();
        return editor::EditorCommandState::Enabled(
            state->consolePanel.IsValid() && state->panels.IsOpen(state->consolePanel));
    }

    bool InvokeOpenConsole(void*, void* const handlerData) noexcept
    {
        auto* const state = static_cast<ActivityPresentationState*>(handlerData);
        if (state == nullptr || !state->consolePanel.IsValid())
            return false;
        editor::EditorUiFailure failure;
        return state->panels.SetOpen(state->consolePanel, true, &failure);
    }

    editor::EditorCommandState QueryOpenBackgroundActivity(
        void*, void* const handlerData) noexcept
    {
        auto* const state = static_cast<ActivityPresentationState*>(handlerData);
        if (state == nullptr)
            return editor::EditorCommandState::Disabled();
        return editor::EditorCommandState::Enabled(
            state->backgroundActivityPanel.IsValid() &&
            state->panels.IsOpen(state->backgroundActivityPanel));
    }

    bool InvokeOpenBackgroundActivity(void*, void* const handlerData) noexcept
    {
        auto* const state = static_cast<ActivityPresentationState*>(handlerData);
        if (state == nullptr || !state->backgroundActivityPanel.IsValid())
            return false;
        editor::EditorUiFailure failure;
        return state->panels.SetOpen(state->backgroundActivityPanel, true, &failure);
    }

    bool ActivateActivityPresentation(editor::EditorExtensionActivationContext& context,
                                      const void*, void*& privateState) noexcept
    {
        const vanguard::memory::MemoryBlock block =
            vanguard::memory::Allocate(vanguard::memory::PoolId::Tools,
                                      sizeof(ActivityPresentationState),
                                      alignof(ActivityPresentationState));
        if (!block)
            return false;

        auto* const state = ::new (block.address) ActivityPresentationState();
        state->activity = &context.GetActivity();
        state->panels = context.GetOwnedPanels();
        privateState = state;

        editor::EditorCommandFailure commandFailure;
        editor::EditorUiFailure uiFailure;
        editor::EditorPreferenceFailure preferenceFailure;

        editor::EditorCommandToken commandToken;
        const editor::EditorCommandDescriptor command{
            OpenConsoleCommandId, "Open Console", editor::EditorCommandFlags::None};

        editor::EditorCommandHandlerToken handlerToken;
        editor::EditorCommandHandlerDescriptor handler;
        handler.contextType = editor::EditorWorkspaceCommandContextTypeId;
        handler.command = OpenConsoleCommandId;
        handler.query = QueryOpenConsole;
        handler.invoke = InvokeOpenConsole;
        handler.handlerData = state;

        editor::EditorPanelTypeToken panelTypeToken;
        editor::EditorPanelTypeDesc panelType;
        panelType.id = ConsolePanelTypeId;
        panelType.name = "Console";
        panelType.create = CreateConsolePanel;
        panelType.destroy = DestroyConsolePanel;
        panelType.draw = DrawConsolePanel;
        panelType.factoryData = state;
        panelType.persistence.stateVersion = editor::detail::ConsolePanelStateVersion;
        panelType.persistence.capture = CaptureConsolePanelState;
        panelType.persistence.restore = RestoreConsolePanelState;
        panelType.persistence.getRevision = GetConsolePanelStateRevision;
        panelType.policy = editor::EditorPanelTypePolicy::Singleton;
        panelType.defaultInstance = ConsolePanelInstanceId;
        panelType.initialPlacement = editor::EditorPanelInitialPlacement::Bottom;

        editor::detail::EditorUiContributionToken viewContributionToken;
        editor::detail::EditorUiContributionDesc viewContribution;
        viewContribution.id = ConsoleViewContributionId;
        viewContribution.surface = editor::detail::EditorMainMenuSurfaceId;
        viewContribution.kind = editor::detail::EditorUiContributionKind::Command;
        viewContribution.parent = editor::detail::EditorMainMenuViewAnchorId;
        viewContribution.order = 100;
        viewContribution.command = OpenConsoleCommandId;
        viewContribution.label = "Console";

        editor::EditorCommandToken backgroundCommandToken;
        const editor::EditorCommandDescriptor backgroundCommand{
            OpenBackgroundActivityCommandId, "Open Background Activity",
            editor::EditorCommandFlags::None};

        editor::EditorCommandHandlerToken backgroundHandlerToken;
        editor::EditorCommandHandlerDescriptor backgroundHandler;
        backgroundHandler.contextType = editor::EditorWorkspaceCommandContextTypeId;
        backgroundHandler.command = OpenBackgroundActivityCommandId;
        backgroundHandler.query = QueryOpenBackgroundActivity;
        backgroundHandler.invoke = InvokeOpenBackgroundActivity;
        backgroundHandler.handlerData = state;

        editor::EditorPanelTypeToken backgroundPanelTypeToken;
        editor::EditorPanelTypeDesc backgroundPanelType;
        backgroundPanelType.id = BackgroundActivityPanelTypeId;
        backgroundPanelType.name = "Background Activity";
        backgroundPanelType.create = CreateBackgroundActivityPanel;
        backgroundPanelType.destroy = DestroyBackgroundActivityPanel;
        backgroundPanelType.draw = DrawBackgroundActivityPanel;
        backgroundPanelType.factoryData = state;
        backgroundPanelType.policy = editor::EditorPanelTypePolicy::Singleton;
        backgroundPanelType.defaultInstance = BackgroundActivityPanelInstanceId;
        backgroundPanelType.initialPlacement = editor::EditorPanelInitialPlacement::Bottom;

        editor::detail::EditorUiContributionToken backgroundViewContributionToken;
        editor::detail::EditorUiContributionDesc backgroundViewContribution;
        backgroundViewContribution.id = BackgroundActivityViewContributionId;
        backgroundViewContribution.surface = editor::detail::EditorMainMenuSurfaceId;
        backgroundViewContribution.kind = editor::detail::EditorUiContributionKind::Command;
        backgroundViewContribution.parent = editor::detail::EditorMainMenuViewAnchorId;
        backgroundViewContribution.order = 110;
        backgroundViewContribution.command = OpenBackgroundActivityCommandId;
        backgroundViewContribution.label = "Background Activity";

        constexpr editor::EditorPreferenceFieldDescriptor preferenceFields[]{
            {ActivityLevelsPreferenceFieldId, editor::EditorPreferenceValueType::UnsignedBitMask, 0},
            {ActivityCategoryPreferenceFieldId, editor::EditorPreferenceValueType::SignedInteger, 0},
            {ActivityTextPreferenceFieldId, editor::EditorPreferenceValueType::Utf8String,
             static_cast<vanguard::u16>(DiagnosticTextFilterBytes - 1u)}};
        editor::EditorPreferenceProviderDescriptor preferenceProvider;
        preferenceProvider.providerId = ActivityPreferenceProviderId;
        preferenceProvider.fields = preferenceFields;
        preferenceProvider.queryRevision = QueryActivityPreferenceRevision;
        preferenceProvider.capture = CaptureActivityPreference;
        preferenceProvider.validate = ValidateActivityPreference;
        preferenceProvider.apply = ApplyActivityPreference;
        editor::EditorPreferenceProviderToken preferenceToken;

        return context.RegisterCommand(command, commandToken, &commandFailure) &&
               context.RegisterHandler(handler, handlerToken, &commandFailure) &&
               context.RegisterPanelType(panelType, panelTypeToken, &uiFailure) &&
               context.RegisterSurfaceContribution(viewContribution, viewContributionToken,
                                                   &uiFailure) &&
               context.RegisterCommand(backgroundCommand, backgroundCommandToken,
                                       &commandFailure) &&
               context.RegisterHandler(backgroundHandler, backgroundHandlerToken,
                                       &commandFailure) &&
               context.RegisterPanelType(backgroundPanelType, backgroundPanelTypeToken,
                                         &uiFailure) &&
               context.RegisterSurfaceContribution(backgroundViewContribution,
                                                   backgroundViewContributionToken,
                                                   &uiFailure) &&
               context.RegisterPreferenceProvider(preferenceProvider, preferenceToken,
                                                  &preferenceFailure) &&
               EnsureConsolePanel(*state) &&
               EnsureBackgroundActivityPanel(*state);
    }

    void DeactivateActivityPresentation(editor::EditorExtensionActivationContext&,
                                        const void*, void* const privateState) noexcept
    {
        if (privateState == nullptr)
            return;
        auto* const state = static_cast<ActivityPresentationState*>(privateState);
        state->~ActivityPresentationState();
        vanguard::memory::MemoryBlock block{
            state, sizeof(ActivityPresentationState), vanguard::memory::PoolId::Tools};
        vanguard::memory::Free(block);
    }

    const editor::EditorExtensionDescriptor ActivityPresentationExtensions[]{
        {ActivityPresentationExtensionId,
         "editor-activity-presentation",
         "Editor Activity Presentation",
         {},
         editor::EditorExtensionSelectionPolicy::BuiltIn,
         ActivateActivityPresentation,
         DeactivateActivityPresentation,
         nullptr}};
} // namespace

namespace vanguard::editor
{
    containers::ArraySpan<const EditorExtensionDescriptor>
    GetEditorActivityPresentationExtensions() noexcept
    {
        return ActivityPresentationExtensions;
    }
} // namespace vanguard::editor
