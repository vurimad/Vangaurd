// TEST SUPPORT ONLY: native UI integration-test extension.
// This is opt-in test instrumentation; production editor behavior must not depend on it.

#include <native_ui_integration_test_extension.hpp>

#include <vanguard/diagnostics/diagnostics.hpp>
#include <vanguard/editor/editor_activity_service.hpp>
#include <vanguard/editor/editor_project_service.hpp>
#include <vanguard/editor/editor_shell_service.hpp>
#include <vanguard/editor/editor_user_state_service.hpp>
#include <vanguard/filesystem/filesystem.hpp>
#include <vanguard/memory/memory.hpp>

#include <imgui.h>
#include <imgui_internal.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

namespace
{
    namespace editor = vanguard::editor;

    struct NativeUiIntegrationTestState final
    {
        editor::EditorActivityService* activity = nullptr;
        editor::EditorOwnedPanelAccess panels;
        editor::EditorPanelTypeToken panelType;
        editor::EditorPanelHandle panel;
        editor::EditorCommandToken command;
        editor::EditorCommandContextTypeToken contextType;
        editor::EditorCommandHandlerToken handler;
        editor::detail::EditorUiSurfaceToken contextSurface;
        editor::detail::EditorUiContributionToken mainMenuContribution;
        editor::detail::EditorUiContributionToken toolbarContribution;
        editor::detail::EditorUiContributionToken contextLeadingSeparator;
        editor::detail::EditorUiContributionToken contextCommandContribution;
        editor::detail::EditorUiContributionToken contextTrailingSeparator;
        vanguard::u32 creates = 0;
        vanguard::u32 destroys = 0;
        vanguard::u32 invokes = 0;
        vanguard::u32 invokeAttempts = 0;
        vanguard::u32 rejectedInvokes = 0;
        vanguard::u32 queries = 0;
        vanguard::u32 sentinel = 0x4e554954u;
        editor::testing::NativeUiCommandPresentationMode mode = editor::testing::NativeUiCommandPresentationMode::Enabled;
        editor::testing::NativeUiCommandPresentationMode lastLoggedMode = editor::testing::NativeUiCommandPresentationMode::Enabled;
        editor::testing::NativeUiContextRequestTelemetry contextRequest;
        vanguard::u32 contextSequence = 0;
        vanguard::u32 contextCancellationSequence = 0;
        bool contextCancellationPending = false;
        bool shortcutOverridePending = false;
        bool requestedCustomShortcut = false;
        vanguard::u64 activityOperationGeneration = 1u;
        vanguard::u64 activitySourceGeneration = 0u;
        vanguard::u64 activityLastCancelGeneration = 0u;
        vanguard::u32 activitySamples = 0u;
        vanguard::u32 activityProducerTicks = 0u;
        vanguard::u32 activityAdvances = 0u;
        vanguard::u32 activityCancelRequests = 0u;
        vanguard::u32 activityAcceptedCancels = 0u;
        vanguard::u32 activityUiCancelInteractions = 0u;
        vanguard::u32 activityQueuedCancels = 0u;
        editor::EditorActivityCancelAdmission activityLastAdmission =
            editor::EditorActivityCancelAdmission::Unsupported;
        vanguard::u32 activityCancellingTicks = 0u;
        editor::EditorActivityState activityState = editor::EditorActivityState::Running;
        bool automation = false;
        bool lastDocked = false;
        bool lastFocused = false;
        bool seenDocked = false;
        bool detachedAfterDock = false;
        bool redockedAfterDetach = false;
        bool dpiTransition = false;
        vanguard::u32 lastViewport = 0;
        vanguard::f32 lastDpi = 0.0f;
        vanguard::f32 menuX = 0.0f;
        vanguard::f32 menuY = 0.0f;
        vanguard::f32 menuWidth = 0.0f;
        vanguard::f32 toolbarX = 0.0f;
        vanguard::f32 toolbarY = 0.0f;
        vanguard::u32 toolbarProbeFrame = 0;
        vanguard::u32 toolbarProbesThisFrame = 0;
        vanguard::u32 maximumToolbarProbesPerFrame = 0;
        bool toolbarObservedOffPrimary = false;
        vanguard::f32 modeControlX = 0.0f;
        vanguard::f32 modeControlY = 0.0f;
        vanguard::f32 shortcutControlX = 0.0f;
        vanguard::f32 shortcutControlY = 0.0f;
        vanguard::f32 contextControlX = 0.0f;
        vanguard::f32 contextControlY = 0.0f;
        vanguard::f32 cancelControlX = 0.0f;
        vanguard::f32 cancelControlY = 0.0f;
        vanguard::f32 activityCancelControlX = 0.0f;
        vanguard::f32 activityCancelControlY = 0.0f;
    };

    NativeUiIntegrationTestState* g_activeState = nullptr;

    struct NativeUiPersistenceLifecycleTelemetry final
    {
        editor::testing::NativeUiPersistenceScenario scenario =
            editor::testing::NativeUiPersistenceScenario::None;
        vanguard::u32 sequence = 0;
        vanguard::u32 shellActivation = 0;
        vanguard::u32 restoreObserved = 0;
        vanguard::u32 firstFrame = 0;
        vanguard::u32 afterFinalCapture = 0;
        vanguard::u32 shellDeactivate = 0;
    };

    NativeUiPersistenceLifecycleTelemetry g_persistenceLifecycle;

    [[nodiscard]] vanguard::u32 NextPersistenceSequence() noexcept
    {
        if (g_persistenceLifecycle.sequence == ~vanguard::u32{0})
            return 0;
        return ++g_persistenceLifecycle.sequence;
    }

    [[nodiscard]] bool QueryNativeUiActivity(
        editor::detail::EditorActivitySourceWriter& writer, void* const userData,
        editor::EditorActivityFailure* const failure) noexcept
    {
        auto* const state = static_cast<NativeUiIntegrationTestState*>(userData);
        if (state == nullptr || state->activityOperationGeneration == 0u)
            return false;

        ++state->activitySamples;

        char detail[editor::MaximumEditorActivityDetailBytes]{};
        std::snprintf(detail, sizeof(detail), "operation=%llu samples=%u cancels=%u",
                      static_cast<unsigned long long>(state->activityOperationGeneration),
                      state->activitySamples, state->activityAcceptedCancels);

        editor::detail::EditorActivityItemDescriptor item;
        item.id = editor::testing::NativeUiActivityItemId;
        item.generation = state->activityOperationGeneration;
        item.kind = editor::EditorActivityKind::Status;
        item.state = state->activityState;
        item.attention = editor::EditorActivityAttention::None;
        item.cancellable = state->activityState == editor::EditorActivityState::Running;
        item.label = "Native UI background operation";
        item.detail = detail;
        return writer.Add(item, failure);
    }

    editor::EditorActivityCancelResult RequestNativeUiActivityCancel(
        const editor::EditorActivityKey key, void* const userData) noexcept
    {
        auto* const state = static_cast<NativeUiIntegrationTestState*>(userData);
        if (state == nullptr)
            return editor::EditorActivityCancelResult::Failed;

        ++state->activityCancelRequests;
        if (key.source != editor::testing::NativeUiActivitySourceId ||
            key.sourceGeneration != state->activitySourceGeneration ||
            key.item != editor::testing::NativeUiActivityItemId ||
            key.itemGeneration != state->activityOperationGeneration)
            return editor::EditorActivityCancelResult::Stale;
        if (state->activityState == editor::EditorActivityState::Cancelling ||
            state->activityState == editor::EditorActivityState::Cancelled)
            return editor::EditorActivityCancelResult::AlreadyRequested;
        if (state->activityState != editor::EditorActivityState::Running)
            return editor::EditorActivityCancelResult::Unsupported;

        state->activityState = editor::EditorActivityState::Cancelling;
        state->activityCancellingTicks = 0u;
        state->activityLastCancelGeneration = key.itemGeneration;
        ++state->activityAcceptedCancels;
        return editor::EditorActivityCancelResult::Accepted;
    }

    void CaptureLastItemCenter(vanguard::f32& x, vanguard::f32& y) noexcept
    {
        const ImVec2 minimum = ImGui::GetItemRectMin();
        const ImVec2 maximum = ImGui::GetItemRectMax();
        x = (minimum.x + maximum.x) * 0.5f;
        y = (minimum.y + maximum.y) * 0.5f;
    }

    void DrawNativeUiActivityCancelControl(NativeUiIntegrationTestState& state) noexcept
    {
        const bool canQueue = state.activity != nullptr && state.activitySourceGeneration != 0u &&
                              state.activityState == editor::EditorActivityState::Running;
        if (!canQueue)
            ImGui::BeginDisabled();
        const bool clicked = ImGui::Button("Queue Activity cancel");
        CaptureLastItemCenter(state.activityCancelControlX, state.activityCancelControlY);
        if (!canQueue)
            ImGui::EndDisabled();
        if (!clicked || !canQueue)
            return;

        ++state.activityUiCancelInteractions;
        const editor::EditorActivityKey key{
            editor::testing::NativeUiActivitySourceId,
            state.activitySourceGeneration,
            editor::testing::NativeUiActivityItemId,
            state.activityOperationGeneration};
        editor::EditorActivityFailure failure;
        state.activityLastAdmission = state.activity->RequestCancel(key, &failure);
        if (state.activityLastAdmission == editor::EditorActivityCancelAdmission::Queued)
            ++state.activityQueuedCancels;
        VG_LOG_INFO(
            vanguard::diagnostics::Category::Engine,
            "Editor native UI Activity cancel interaction: operation=%llu admission=%u interactions=%u queued=%u",
            static_cast<unsigned long long>(state.activityOperationGeneration),
            static_cast<vanguard::u32>(state.activityLastAdmission),
            state.activityUiCancelInteractions, state.activityQueuedCancels);
        vanguard::diagnostics::Flush(vanguard::diagnostics::FlushMode::Asynchronous);
    }

    [[nodiscard]] editor::testing::NativeUiCommandPresentationMode NextPresentationMode(
        const editor::testing::NativeUiCommandPresentationMode mode) noexcept
    {
        using Mode = editor::testing::NativeUiCommandPresentationMode;
        switch (mode)
        {
        case Mode::Enabled: return Mode::Disabled;
        case Mode::Disabled: return Mode::Unsupported;
        case Mode::Unsupported: return Mode::Checked;
        case Mode::Checked: return Mode::RejectInvoke;
        case Mode::RejectInvoke: return Mode::Enabled;
        }
        return Mode::Enabled;
    }

    void CapturePersistentSurfaceProbe(NativeUiIntegrationTestState& state) noexcept
    {
        if (!state.automation || ImGui::GetCurrentContext() == nullptr)
            return;
        ImGuiWindow* const window = ImGui::GetCurrentWindowRead();
        if (window == nullptr || window->SkipItems)
            return;

        const ImVec2 cursor = window->DC.CursorPos;
        const ImVec2 label = ImGui::CalcTextSize("Native UI command");
        const ImGuiStyle& style = ImGui::GetStyle();
        if (std::strcmp(window->Name, "##VanguardMainToolbar") == 0)
        {
            const vanguard::u32 frame = static_cast<vanguard::u32>(ImGui::GetFrameCount());
            if (state.toolbarProbeFrame != frame)
            {
                state.toolbarProbeFrame = frame;
                state.toolbarProbesThisFrame = 0;
            }
            ++state.toolbarProbesThisFrame;
            if (state.toolbarProbesThisFrame > state.maximumToolbarProbesPerFrame)
                state.maximumToolbarProbesPerFrame = state.toolbarProbesThisFrame;
            ImGuiViewport* const mainViewport = ImGui::GetMainViewport();
            if (window->Viewport == nullptr || mainViewport == nullptr || window->Viewport->ID != mainViewport->ID)
                state.toolbarObservedOffPrimary = true;
            state.toolbarX = cursor.x + static_cast<float>(static_cast<int>(style.ItemSpacing.x * 0.5f)) + label.x * 0.5f;
            state.toolbarY = cursor.y + ImGui::GetFrameHeight() * 0.5f;
            return;
        }
        if ((window->Flags & ImGuiWindowFlags_ChildMenu) != 0)
        {
            state.menuX = (window->InnerRect.Min.x + window->InnerRect.Max.x) * 0.5f;
            state.menuY = cursor.y + ImGui::GetFrameHeight() * 0.5f;
            state.menuWidth = window->SizeFull.x;
        }
    }

    editor::EditorCommandState QueryNativeUiIntegrationCommand(void* const receiverData, void* const handlerData) noexcept
    {
        auto* const state = static_cast<NativeUiIntegrationTestState*>(handlerData);
        if (state == nullptr || receiverData != state || state->sentinel != 0x4e554954u)
            return editor::EditorCommandState::Unsupported();
        ++state->queries;
        CapturePersistentSurfaceProbe(*state);
        using Mode = editor::testing::NativeUiCommandPresentationMode;
        switch (state->mode)
        {
        case Mode::Enabled: return editor::EditorCommandState::Enabled();
        case Mode::Disabled: return editor::EditorCommandState::Disabled();
        case Mode::Unsupported: return editor::EditorCommandState::Unsupported();
        case Mode::Checked: return editor::EditorCommandState::Enabled(true);
        case Mode::RejectInvoke: return editor::EditorCommandState::Enabled();
        }
        return editor::EditorCommandState::Unsupported();
    }

    bool InvokeNativeUiIntegrationCommand(void* const receiverData, void* const handlerData) noexcept
    {
        auto* const state = static_cast<NativeUiIntegrationTestState*>(handlerData);
        if (state == nullptr || receiverData != state || state->sentinel != 0x4e554954u)
            return false;
        ++state->invokeAttempts;
        if (state->mode == editor::testing::NativeUiCommandPresentationMode::RejectInvoke)
        {
            ++state->rejectedInvokes;
            VG_LOG_INFO(vanguard::diagnostics::Category::Engine,
                        "Editor native UI test command rejected: attempts=%u rejected=%u invokes=%u queries=%u receiver=%p",
                        state->invokeAttempts, state->rejectedInvokes, state->invokes, state->queries, receiverData);
            vanguard::diagnostics::Flush(vanguard::diagnostics::FlushMode::Asynchronous);
            return false;
        }
        ++state->invokes;
        VG_LOG_INFO(vanguard::diagnostics::Category::Engine,
                    "Editor native UI test command: invokes=%u attempts=%u rejected=%u queries=%u receiver=%p",
                    state->invokes, state->invokeAttempts, state->rejectedInvokes, state->queries, receiverData);
        vanguard::diagnostics::Flush(vanguard::diagnostics::FlushMode::Asynchronous);
        return true;
    }

    void* CreateNativeUiIntegrationPanel(const editor::EditorPanelInstanceId, void* const factoryData) noexcept
    {
        auto* const state = static_cast<NativeUiIntegrationTestState*>(factoryData);
        if (state == nullptr || state->creates != 0u)
            return nullptr;
        ++state->creates;
        return state;
    }

    void DestroyNativeUiIntegrationPanel(void* const panel, void* const factoryData) noexcept
    {
        auto* const state = static_cast<NativeUiIntegrationTestState*>(factoryData);
        if (state != nullptr && panel == state)
            ++state->destroys;
    }

    void DrawNativeUiIntegrationPanel(editor::EditorPanelContext& context, void* const panel, void* const factoryData) noexcept
    {
        auto* const state = static_cast<NativeUiIntegrationTestState*>(factoryData);
        if (state == nullptr || panel != state)
            return;
        ImGui::TextUnformatted("Native UI integration command focus test");
        ImGui::TextUnformatted("Press F8 while this panel is focused.");
        ImGui::Text("Invokes: %u; attempts: %u; rejected: %u; queries: %u", state->invokes, state->invokeAttempts,
                    state->rejectedInvokes, state->queries);
        ImGui::Text("State: %08x; creates: %u; destroys: %u", state->sentinel, state->creates, state->destroys);

        if (ImGui::Button("Cycle command presentation"))
            state->mode = NextPresentationMode(state->mode);
        CaptureLastItemCenter(state->modeControlX, state->modeControlY);

        if (ImGui::Button("Toggle F8/F12 shortcut"))
        {
            state->requestedCustomShortcut = !state->requestedCustomShortcut;
            state->shortcutOverridePending = true;
        }
        CaptureLastItemCenter(state->shortcutControlX, state->shortcutControlY);

        const auto requestContextMenu = [&](const bool cancelAfterCommit) noexcept
        {
            editor::EditorUiFailure failure;
            editor::testing::NativeUiContextRequestTelemetry telemetry;
            telemetry.sequence = ++state->contextSequence;
            telemetry.frame = static_cast<vanguard::u32>(ImGui::GetFrameCount());
            if (ImGuiViewport* const viewport = ImGui::GetWindowViewport(); viewport != nullptr)
                telemetry.viewport = viewport->ID;
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            telemetry.anchorX = mouse.x;
            telemetry.anchorY = mouse.y;
            telemetry.mode = state->mode;
            telemetry.cancelAfterCommit = cancelAfterCommit;
            telemetry.accepted = context.RequestContextMenu(editor::testing::NativeUiContextMenuSurfaceId, &failure);
            state->contextRequest = telemetry;
            if (telemetry.accepted && cancelAfterCommit)
            {
                state->contextCancellationSequence = telemetry.sequence;
                state->contextCancellationPending = true;
            }
            VG_LOG_INFO(vanguard::diagnostics::Category::Engine,
                        "Editor native UI test context request: sequence=%u frame=%u mode=%u accepted=%u cancel=%u viewport=%u anchor=%.1f,%.1f failure=%u",
                        telemetry.sequence, telemetry.frame, static_cast<vanguard::u32>(telemetry.mode),
                        vanguard::u32(telemetry.accepted), vanguard::u32(telemetry.cancelAfterCommit), telemetry.viewport,
                        static_cast<double>(telemetry.anchorX), static_cast<double>(telemetry.anchorY),
                        static_cast<vanguard::u32>(failure.code));
            vanguard::diagnostics::Flush(vanguard::diagnostics::FlushMode::Asynchronous);
        };

        if (ImGui::Button("Request context menu"))
            requestContextMenu(false);
        CaptureLastItemCenter(state->contextControlX, state->contextControlY);

        if (ImGui::Button("Request context menu then close"))
            requestContextMenu(true);
        CaptureLastItemCenter(state->cancelControlX, state->cancelControlY);
        DrawNativeUiActivityCancelControl(*state);
        if (!state->automation)
            return;

        ImGuiWindow* const window = ImGui::GetCurrentWindow();
        ImGuiViewport* const viewport = ImGui::GetWindowViewport();
        const bool docked = ImGui::IsWindowDocked();
        const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        const vanguard::u32 viewportId = viewport != nullptr ? viewport->ID : 0u;
        if (window == nullptr || viewport == nullptr)
            return;
        ImVec2 drag = {(window->TitleBarRect().Min.x + window->TitleBarRect().Max.x) * 0.5f,
                       (window->TitleBarRect().Min.y + window->TitleBarRect().Max.y) * 0.5f};
        if (window->DockNode != nullptr && window->DockNode->TabBar != nullptr)
        {
            ImGuiTabBar* const tabBar = window->DockNode->TabBar;
            for (int index = 0; index < tabBar->Tabs.Size; ++index)
            {
                const ImGuiTabItem& tab = tabBar->Tabs[index];
                if (tab.Window != window)
                    continue;
                drag = {tabBar->BarRect.Min.x + tab.Offset - tabBar->ScrollingAnim + tab.Width * 0.5f,
                        (tabBar->BarRect.Min.y + tabBar->BarRect.Max.y) * 0.5f};
                break;
            }
        }
        if (docked)
        {
            state->seenDocked = true;
            if (state->detachedAfterDock)
                state->redockedAfterDetach = true;
        }
        else if (state->seenDocked)
        {
            state->detachedAfterDock = true;
        }
        if (state->lastDpi > 0.0f)
        {
            const vanguard::f32 delta = viewport->DpiScale - state->lastDpi;
            state->dpiTransition = state->dpiTransition || delta < -0.01f || delta > 0.01f;
        }
        if (state->lastDocked != docked || state->lastFocused != focused || state->lastViewport != viewportId ||
            state->lastLoggedMode != state->mode ||
            (ImGui::GetFrameCount() % 30) == 0)
        {
            VG_LOG_INFO(vanguard::diagnostics::Category::Engine,
                        "Editor native UI test command telemetry: docked=%u focused=%u viewport=%u dpi=%.3f drag=%.1f,%.1f invokes=%u attempts=%u rejected=%u queries=%u mode=%u state=%08x create=%u destroy=%u menu=%.1f,%.1f menuWidth=%.1f toolbar=%.1f,%.1f toolbarMax=%u toolbarOffPrimary=%u modeControl=%.1f,%.1f shortcutControl=%.1f,%.1f request=%.1f,%.1f cancel=%.1f,%.1f activityCancel=%.1f,%.1f",
                        vanguard::u32(docked), vanguard::u32(focused), viewportId,
                        static_cast<double>(viewport->DpiScale), static_cast<double>(drag.x),
                        static_cast<double>(drag.y), state->invokes, state->invokeAttempts, state->rejectedInvokes,
                        state->queries, static_cast<vanguard::u32>(state->mode), state->sentinel,
                        state->creates, state->destroys,
                        static_cast<double>(state->menuX), static_cast<double>(state->menuY),
                        static_cast<double>(state->menuWidth),
                        static_cast<double>(state->toolbarX), static_cast<double>(state->toolbarY),
                        state->maximumToolbarProbesPerFrame, vanguard::u32(state->toolbarObservedOffPrimary),
                        static_cast<double>(state->modeControlX), static_cast<double>(state->modeControlY),
                        static_cast<double>(state->shortcutControlX), static_cast<double>(state->shortcutControlY),
                        static_cast<double>(state->contextControlX), static_cast<double>(state->contextControlY),
                        static_cast<double>(state->cancelControlX), static_cast<double>(state->cancelControlY),
                        static_cast<double>(state->activityCancelControlX),
                        static_cast<double>(state->activityCancelControlY));
            vanguard::diagnostics::Flush(vanguard::diagnostics::FlushMode::Asynchronous);
        }
        state->lastDocked = docked;
        state->lastFocused = focused;
        state->lastViewport = viewportId;
        state->lastDpi = viewport->DpiScale;
        state->lastLoggedMode = state->mode;
    }

    bool ActivateNativeUiIntegrationTest(editor::EditorExtensionActivationContext& context, const void*, void*& privateState) noexcept
    {
        if (g_persistenceLifecycle.scenario != editor::testing::NativeUiPersistenceScenario::None)
            g_persistenceLifecycle.shellActivation = NextPersistenceSequence();
        const vanguard::memory::MemoryBlock block =
            vanguard::memory::Allocate(vanguard::memory::PoolId::Tools, sizeof(NativeUiIntegrationTestState), alignof(NativeUiIntegrationTestState));
        if (!block)
            return false;
        auto* const state = ::new (block.address) NativeUiIntegrationTestState();
        state->activity = &context.GetActivity();
        state->panels = context.GetOwnedPanels();
        privateState = state;
#if defined(_WIN32)
        size_t automationEnvironmentBytes = 0;
        state->automation =
            getenv_s(&automationEnvironmentBytes, nullptr, 0, "VG_EDITOR_NATIVE_UI_TEST_AUTOMATION") == 0 &&
            automationEnvironmentBytes > 1u;
#endif

        editor::EditorCommandFailure commandFailure;
        editor::EditorUiFailure uiFailure;
        editor::EditorActivityFailure activityFailure;
        editor::detail::EditorActivitySourceToken activitySourceToken;
        const editor::EditorCommandDescriptor command{editor::testing::NativeUiCommandId, "Native UI integration test command",
                                                      editor::EditorCommandFlags::None};
        const editor::EditorCommandContextTypeDescriptor contextType{
            editor::testing::NativeUiLocalContextId, "Native UI integration test Local context", editor::EditorCommandContextTier::Local};
        editor::EditorCommandHandlerDescriptor handler;
        handler.contextType = editor::testing::NativeUiLocalContextId;
        handler.command = editor::testing::NativeUiCommandId;
        handler.query = QueryNativeUiIntegrationCommand;
        handler.invoke = InvokeNativeUiIntegrationCommand;
        handler.handlerData = state;
        handler.defaultShortcut = {vanguard::input::Key::F8, editor::EditorShortcutModifiers::None};
        editor::EditorPanelTypeDesc panel;
        panel.id = editor::testing::NativeUiCommandPanelTypeId;
        panel.name = "Native UI command focus test";
        panel.create = CreateNativeUiIntegrationPanel;
        panel.destroy = DestroyNativeUiIntegrationPanel;
        panel.draw = DrawNativeUiIntegrationPanel;
        panel.factoryData = state;
        panel.initialPlacement =
            g_persistenceLifecycle.scenario == editor::testing::NativeUiPersistenceScenario::None ||
                    state->automation
                ? editor::EditorPanelInitialPlacement::Left
                : editor::EditorPanelInitialPlacement::Floating;
        const editor::EditorPanelLocalContextBinding binding{
            editor::testing::NativeUiLocalContextId, editor::EditorCommandContextMode::Inherit};

        editor::detail::EditorActivitySourceDescriptor activitySource;
        activitySource.id = editor::testing::NativeUiActivitySourceId;
        activitySource.displayName = "Native UI Activity";
        activitySource.query = QueryNativeUiActivity;
        activitySource.requestCancel = RequestNativeUiActivityCancel;
        activitySource.userData = state;

        editor::detail::EditorUiContributionDesc mainMenu;
        mainMenu.id = editor::testing::NativeUiMainMenuContributionId;
        mainMenu.surface = editor::detail::EditorMainMenuSurfaceId;
        mainMenu.kind = editor::detail::EditorUiContributionKind::Command;
        mainMenu.parent = editor::detail::EditorMainMenuFileAnchorId;
        mainMenu.command = editor::testing::NativeUiCommandId;
        mainMenu.label = "Native UI command";

        editor::detail::EditorUiContributionDesc toolbar;
        toolbar.id = editor::testing::NativeUiToolbarContributionId;
        toolbar.surface = editor::detail::EditorMainToolbarSurfaceId;
        toolbar.kind = editor::detail::EditorUiContributionKind::Command;
        toolbar.sibling = editor::detail::EditorMainToolbarStartAnchorId;
        toolbar.placement = editor::detail::EditorUiPlacementRelation::After;
        toolbar.command = editor::testing::NativeUiCommandId;
        toolbar.label = "Native UI command";

        editor::detail::EditorUiContributionDesc contextLeadingSeparator;
        contextLeadingSeparator.id = editor::testing::NativeUiContextLeadingSeparatorId;
        contextLeadingSeparator.surface = editor::testing::NativeUiContextMenuSurfaceId;
        contextLeadingSeparator.kind = editor::detail::EditorUiContributionKind::Separator;
        contextLeadingSeparator.order = 10;

        editor::detail::EditorUiContributionDesc contextCommand;
        contextCommand.id = editor::testing::NativeUiContextCommandContributionId;
        contextCommand.surface = editor::testing::NativeUiContextMenuSurfaceId;
        contextCommand.kind = editor::detail::EditorUiContributionKind::Command;
        contextCommand.order = 20;
        contextCommand.command = editor::testing::NativeUiCommandId;
        contextCommand.label = "Native UI command";

        editor::detail::EditorUiContributionDesc contextTrailingSeparator = contextLeadingSeparator;
        contextTrailingSeparator.id = editor::testing::NativeUiContextTrailingSeparatorId;
        contextTrailingSeparator.order = 30;

        const bool registered =
            context.RegisterActivitySource(activitySource, activitySourceToken, &activityFailure) &&
            context.RegisterCommand(command, state->command, &commandFailure) &&
            context.RegisterContextType(contextType, state->contextType, &commandFailure) &&
            context.RegisterHandler(handler, state->handler, &commandFailure) &&
            context.RegisterPanelType(panel, binding, state->panelType, &uiFailure) &&
            context.RegisterContextMenuSurface(editor::testing::NativeUiContextMenuSurfaceId, state->contextSurface, &uiFailure) &&
            context.RegisterSurfaceContribution(mainMenu, state->mainMenuContribution, &uiFailure) &&
            context.RegisterSurfaceContribution(toolbar, state->toolbarContribution, &uiFailure) &&
            context.RegisterSurfaceContribution(contextLeadingSeparator, state->contextLeadingSeparator, &uiFailure) &&
            context.RegisterSurfaceContribution(contextCommand, state->contextCommandContribution, &uiFailure) &&
            context.RegisterSurfaceContribution(contextTrailingSeparator, state->contextTrailingSeparator, &uiFailure) &&
            state->panels.CreatePanel(
                {editor::testing::NativeUiCommandPanelInstanceId,
                 editor::testing::NativeUiCommandPanelTypeId,
                 "Native UI command focus test", true},
                state->panel, &uiFailure);
        if (registered)
        {
            state->activitySourceGeneration = activitySourceToken.generation;
            g_activeState = state;
        }
        return registered;
    }

    void DeactivateNativeUiIntegrationTest(editor::EditorExtensionActivationContext& context, const void*, void* const privateState) noexcept
    {
        if (g_persistenceLifecycle.scenario != editor::testing::NativeUiPersistenceScenario::None)
        {
            g_persistenceLifecycle.shellDeactivate = NextPersistenceSequence();
            bool canonicalValid = false;
            const vanguard::filesystem::AbsolutePath canonical =
                context.GetWorkspace().GetSavedRoot().AddDirPath("Editor").AddFilePath(
                    "user-state.vstate");
            vanguard::memory::MemoryBlock image =
                vanguard::memory::Allocate(vanguard::memory::PoolId::Tools,
                                           editor::MaximumEditorUserStateFileBytes, 16);
            if (image)
            {
                vanguard::usize bytes = 0;
                if (vanguard::filesystem::ReadCompleteFile(canonical, image.address, image.size,
                                                           bytes) ==
                    vanguard::filesystem::CompleteFileReadResult::Success)
                {
                    editor::EditorUserStateEnvelopeInfo info;
                    editor::EditorUserStateFailure failure;
                    canonicalValid =
                        editor::ValidateEditorUserStateImage(
                            image.address, bytes, context.GetWorkspace().GetProject().id, info,
                            &failure) == editor::EditorUserStateLoadResult::Restored;
                }
                vanguard::memory::Free(image);
            }
            VG_LOG_INFO(
                vanguard::diagnostics::Category::Engine,
                "Editor native UI persistence lifecycle: scenario=%u shellActivate=%u restore=%u firstFrame=%u afterFinal=%u shellDeactivate=%u canonical=%u",
                static_cast<vanguard::u32>(g_persistenceLifecycle.scenario),
                g_persistenceLifecycle.shellActivation,
                g_persistenceLifecycle.restoreObserved,
                g_persistenceLifecycle.firstFrame,
                g_persistenceLifecycle.afterFinalCapture,
                g_persistenceLifecycle.shellDeactivate, vanguard::u32(canonicalValid));
            vanguard::diagnostics::Flush(vanguard::diagnostics::FlushMode::Synchronous);
        }
        if (privateState == nullptr)
            return;
        auto* const state = static_cast<NativeUiIntegrationTestState*>(privateState);
        if (g_activeState == state)
            g_activeState = nullptr;
        if (state->automation)
        {
            VG_LOG_INFO(vanguard::diagnostics::Category::Engine,
                        "Editor native UI test command summary: dockCycle=%u dpiTransition=%u invokes=%u attempts=%u rejected=%u queries=%u mode=%u state=%08x create=%u destroy=%u toolbarMax=%u toolbarOffPrimary=%u",
                        vanguard::u32(state->seenDocked && state->detachedAfterDock && state->redockedAfterDetach),
                        vanguard::u32(state->dpiTransition), state->invokes, state->invokeAttempts,
                        state->rejectedInvokes, state->queries, static_cast<vanguard::u32>(state->mode),
                        state->sentinel, state->creates, state->destroys,
                        state->maximumToolbarProbesPerFrame, vanguard::u32(state->toolbarObservedOffPrimary));
            VG_LOG_INFO(vanguard::diagnostics::Category::Engine,
                        "Editor native UI Activity summary: sourceGeneration=%llu operation=%llu samples=%u producerTicks=%u advances=%u uiInteractions=%u queued=%u callbacks=%u accepted=%u admission=%u state=%u",
                        static_cast<unsigned long long>(state->activitySourceGeneration),
                        static_cast<unsigned long long>(state->activityOperationGeneration),
                        state->activitySamples, state->activityProducerTicks, state->activityAdvances,
                        state->activityUiCancelInteractions, state->activityQueuedCancels,
                        state->activityCancelRequests, state->activityAcceptedCancels,
                        static_cast<vanguard::u32>(state->activityLastAdmission),
                        static_cast<vanguard::u32>(state->activityState));
            vanguard::diagnostics::Flush(vanguard::diagnostics::FlushMode::Synchronous);
        }
        state->~NativeUiIntegrationTestState();
        vanguard::memory::MemoryBlock block{state, sizeof(NativeUiIntegrationTestState), vanguard::memory::PoolId::Tools};
        vanguard::memory::Free(block);
    }

    const editor::EditorExtensionDescriptor NativeUiIntegrationTestExtensions[]{
        {editor::testing::NativeUiIntegrationTestExtensionId, "editor-native-ui-integration-test", "Editor Native UI Integration Test", {},
         editor::EditorExtensionSelectionPolicy::ProjectSelectable, ActivateNativeUiIntegrationTest, DeactivateNativeUiIntegrationTest, nullptr}};
} // namespace

namespace vanguard::editor::testing
{
    containers::ArraySpan<const EditorExtensionDescriptor> GetNativeUiIntegrationTestExtensions() noexcept
    {
        return NativeUiIntegrationTestExtensions;
    }

    bool GetNativeUiContextRequestTelemetry(NativeUiContextRequestTelemetry& telemetry) noexcept
    {
        telemetry = {};
        if (g_activeState == nullptr || !g_activeState->automation || g_activeState->contextRequest.sequence == 0u)
            return false;
        telemetry = g_activeState->contextRequest;
        return true;
    }

    bool GetNativeUiActivityTelemetry(NativeUiActivityTelemetry& telemetry) noexcept
    {
        telemetry = {};
        if (g_activeState == nullptr)
            return false;
        telemetry.sourceGeneration = g_activeState->activitySourceGeneration;
        telemetry.operationGeneration = g_activeState->activityOperationGeneration;
        telemetry.lastCancelGeneration = g_activeState->activityLastCancelGeneration;
        telemetry.samples = g_activeState->activitySamples;
        telemetry.producerTicks = g_activeState->activityProducerTicks;
        telemetry.advances = g_activeState->activityAdvances;
        telemetry.cancelRequests = g_activeState->activityCancelRequests;
        telemetry.acceptedCancels = g_activeState->activityAcceptedCancels;
        telemetry.uiCancelInteractions = g_activeState->activityUiCancelInteractions;
        telemetry.queuedCancels = g_activeState->activityQueuedCancels;
        telemetry.lastAdmission = g_activeState->activityLastAdmission;
        telemetry.state = g_activeState->activityState;
        return true;
    }

    bool TickNativeUiActivityProducer() noexcept
    {
        if (g_activeState == nullptr)
            return false;
        ++g_activeState->activityProducerTicks;
        if (g_activeState->activityState == EditorActivityState::Cancelling)
        {
            ++g_activeState->activityCancellingTicks;
            if (g_activeState->activityCancellingTicks >= 2u)
                g_activeState->activityState = EditorActivityState::Cancelled;
        }
        return true;
    }

    bool AdvanceNativeUiActivityOperation() noexcept
    {
        if (g_activeState == nullptr || g_activeState->activityOperationGeneration == ~vanguard::u64{0})
            return false;
        ++g_activeState->activityOperationGeneration;
        ++g_activeState->activityAdvances;
        g_activeState->activityLastCancelGeneration = 0u;
        g_activeState->activityCancellingTicks = 0u;
        g_activeState->activityState = EditorActivityState::Running;
        return true;
    }

    bool ConsumeNativeUiContextCancellationRequest(u32& sequence) noexcept
    {
        sequence = 0;
        if (g_activeState == nullptr || !g_activeState->contextCancellationPending)
            return false;
        sequence = g_activeState->contextCancellationSequence;
        g_activeState->contextCancellationPending = false;
        return sequence != 0u;
    }

    bool ConsumeNativeUiShortcutOverrideRequest(bool& custom) noexcept
    {
        custom = false;
        if (g_activeState == nullptr || !g_activeState->shortcutOverridePending)
            return false;
        custom = g_activeState->requestedCustomShortcut;
        g_activeState->shortcutOverridePending = false;
        return true;
    }

    void ConfigureNativeUiPersistenceRuntime(const NativeUiPersistenceScenario scenario) noexcept
    {
        g_persistenceLifecycle = {};
        g_persistenceLifecycle.scenario = scenario;
    }

    void RecordNativeUiPersistenceRestoreObserved() noexcept
    {
        if (g_persistenceLifecycle.scenario == NativeUiPersistenceScenario::None ||
            g_persistenceLifecycle.restoreObserved != 0u)
            return;
        g_persistenceLifecycle.restoreObserved = NextPersistenceSequence();
    }

    void RecordNativeUiPersistenceFirstFrame() noexcept
    {
        if (g_persistenceLifecycle.scenario == NativeUiPersistenceScenario::None ||
            g_persistenceLifecycle.firstFrame != 0u)
            return;
        g_persistenceLifecycle.firstFrame = NextPersistenceSequence();
    }

    void RecordNativeUiPersistenceAfterFinalCapture() noexcept
    {
        if (g_persistenceLifecycle.scenario == NativeUiPersistenceScenario::None ||
            g_persistenceLifecycle.afterFinalCapture != 0u)
            return;
        g_persistenceLifecycle.afterFinalCapture = NextPersistenceSequence();
    }

    bool GetNativeUiPersistencePanelTelemetry(
        NativeUiPersistencePanelTelemetry& telemetry) noexcept
    {
        telemetry = {};
        if (g_activeState == nullptr || !g_activeState->panel.IsValid())
            return false;
        telemetry.active = true;
        telemetry.open = g_activeState->panels.IsOpen(g_activeState->panel);
        telemetry.creates = g_activeState->creates;
        telemetry.destroys = g_activeState->destroys;
        return true;
    }

    bool SetNativeUiPersistencePanelOpen(const bool open) noexcept
    {
        return g_activeState != nullptr && g_activeState->panel.IsValid() &&
               g_activeState->panels.SetOpen(g_activeState->panel, open);
    }
} // namespace vanguard::editor::testing
