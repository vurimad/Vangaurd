#pragma once

#include <vanguard/editor/editor_activity.hpp>
#include <vanguard/editor/editor_commands.hpp>
#include <vanguard/editor/editor_extension.hpp>
#include <vanguard/editor/editor_ui.hpp>

namespace vanguard::editor::testing
{
    inline constexpr EditorExtensionId NativeUiIntegrationTestExtensionId{0x6564746573740101ull};
    inline constexpr EditorPanelTypeId NativeUiCommandPanelTypeId = 0x6564746573741101ull;
    inline constexpr EditorPanelInstanceId NativeUiCommandPanelInstanceId = 3;
    inline constexpr EditorCommandId NativeUiCommandId{0x6564746573742101ull};
    inline constexpr EditorCommandContextTypeId NativeUiLocalContextId{0x6564746573743101ull};
    inline constexpr EditorUiSurfaceId NativeUiContextMenuSurfaceId{0x6564746573744101ull};
    inline constexpr EditorUiContributionId NativeUiMainMenuContributionId{0x6564746573745101ull};
    inline constexpr EditorUiContributionId NativeUiToolbarContributionId{0x6564746573745102ull};
    inline constexpr EditorUiContributionId NativeUiContextLeadingSeparatorId{0x6564746573745103ull};
    inline constexpr EditorUiContributionId NativeUiContextCommandContributionId{0x6564746573745104ull};
    inline constexpr EditorUiContributionId NativeUiContextTrailingSeparatorId{0x6564746573745105ull};
    inline constexpr EditorActivitySourceId NativeUiActivitySourceId = 0x6564746573746101ull;
    inline constexpr EditorActivityItemId NativeUiActivityItemId = 0x6564746573746102ull;

    enum class NativeUiPersistenceScenario : u8
    {
        None,
        Seed,
        Restore,
        Topology,
        RejectedCorrupt,
        RejectedTruncated,
        Future,
        ProjectMismatch
    };

    struct NativeUiPersistencePanelTelemetry final
    {
        bool active = false;
        bool open = false;
        u32 creates = 0;
        u32 destroys = 0;
    };

    enum class NativeUiCommandPresentationMode : u8
    {
        Enabled,
        Disabled,
        Unsupported,
        Checked,
        RejectInvoke
    };

    struct NativeUiContextRequestTelemetry final
    {
        u32 sequence = 0;
        u32 frame = 0;
        u32 viewport = 0;
        f32 anchorX = 0.0f;
        f32 anchorY = 0.0f;
        NativeUiCommandPresentationMode mode = NativeUiCommandPresentationMode::Enabled;
        bool accepted = false;
        bool cancelAfterCommit = false;
    };

    struct NativeUiActivityTelemetry final
    {
        u64 sourceGeneration = 0;
        u64 operationGeneration = 0;
        u64 lastCancelGeneration = 0;
        u32 samples = 0;
        u32 producerTicks = 0;
        u32 advances = 0;
        u32 cancelRequests = 0;
        u32 acceptedCancels = 0;
        u32 uiCancelInteractions = 0;
        u32 queuedCancels = 0;
        EditorActivityCancelAdmission lastAdmission = EditorActivityCancelAdmission::Unsupported;
        EditorActivityState state = EditorActivityState::Idle;
    };

    [[nodiscard]] containers::ArraySpan<const EditorExtensionDescriptor> GetNativeUiIntegrationTestExtensions() noexcept;
    [[nodiscard]] bool GetNativeUiContextRequestTelemetry(NativeUiContextRequestTelemetry& telemetry) noexcept;
    [[nodiscard]] bool GetNativeUiActivityTelemetry(NativeUiActivityTelemetry& telemetry) noexcept;
    [[nodiscard]] bool TickNativeUiActivityProducer() noexcept;
    [[nodiscard]] bool AdvanceNativeUiActivityOperation() noexcept;
    [[nodiscard]] bool ConsumeNativeUiContextCancellationRequest(u32& sequence) noexcept;
    [[nodiscard]] bool ConsumeNativeUiShortcutOverrideRequest(bool& custom) noexcept;
    void ConfigureNativeUiPersistenceRuntime(NativeUiPersistenceScenario scenario) noexcept;
    void RecordNativeUiPersistenceRestoreObserved() noexcept;
    void RecordNativeUiPersistenceFirstFrame() noexcept;
    void RecordNativeUiPersistenceAfterFinalCapture() noexcept;
    [[nodiscard]] bool GetNativeUiPersistencePanelTelemetry(
        NativeUiPersistencePanelTelemetry& telemetry) noexcept;
    [[nodiscard]] bool SetNativeUiPersistencePanelOpen(bool open) noexcept;
} // namespace vanguard::editor::testing
