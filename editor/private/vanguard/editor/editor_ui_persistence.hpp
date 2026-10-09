#pragma once

#include <vanguard/editor/editor_ui.hpp>

#include <vanguard/containers/containers.hpp>
#include <vanguard/window/window_types.hpp>

namespace vanguard::editor::detail
{
    struct EditorPanelUserStateRecord;
    struct EditorPanelUserStateRestoreStatus;
    struct EditorPanelPersistentStateCapture;

    inline constexpr u32 EditorPrimaryDockspaceId = 0x5647444Bu;
    inline constexpr u32 MaximumEditorUiPersistenceBytes = 512u * 1024u;
    inline constexpr i32 MinimumRestoredDetachedWidth = 320;
    inline constexpr i32 MinimumRestoredDetachedHeight = 200;
    inline constexpr i32 DefaultRestoredDetachedWidth = 440;
    inline constexpr i32 DefaultRestoredDetachedHeight = 340;
    inline constexpr u32 MaximumEditorUiNamedLayouts = 64;
    inline constexpr u32 MaximumEditorUiNamedLayoutNameBytes = 96;

    enum class EditorUiNamedLayoutDialogKind : u8
    {
        None,
        SaveAs,
        SaveOverwrite,
        Switch,
        Rename,
        Duplicate,
        Delete
    };

    struct EditorUiNamedLayoutChoice final
    {
        u64 id = 0;
        char name[MaximumEditorUiNamedLayoutNameBytes + 1u]{};
        u32 nameByteCount = 0;
        bool modified = false;
    };

    struct EditorUiNamedLayoutDialogResult final
    {
        EditorUiNamedLayoutDialogKind kind = EditorUiNamedLayoutDialogKind::None;
        u64 id = 0;
        char name[MaximumEditorUiNamedLayoutNameBytes + 1u]{};
        u32 nameByteCount = 0;
        bool accepted = false;
    };

    struct EditorUiSavedDisplay final
    {
        i32 boundsX = 0;
        i32 boundsY = 0;
        u32 boundsWidth = 0;
        u32 boundsHeight = 0;
        i32 workX = 0;
        i32 workY = 0;
        u32 workWidth = 0;
        u32 workHeight = 0;
        u32 desktopPixelWidth = 0;
        u32 desktopPixelHeight = 0;
        u32 scaleQ16_16 = 0;
        bool primary = false;

        [[nodiscard]] friend constexpr bool operator==(const EditorUiSavedDisplay&,
                                                       const EditorUiSavedDisplay&) noexcept = default;
    };

    struct EditorUiSavedRect final
    {
        i32 x = 0;
        i32 y = 0;
        i32 width = 0;
        i32 height = 0;

        [[nodiscard]] friend constexpr bool operator==(const EditorUiSavedRect&,
                                                       const EditorUiSavedRect&) noexcept = default;
    };

    struct EditorUiRepairedPlacement final
    {
        EditorUiSavedRect rect;
        u32 savedDisplayIndex = ~u32{0};
        u32 currentDisplayIndex = ~u32{0};
        bool changed = false;
    };

    struct EditorUiPrivateWindowPlacementFields final
    {
        i16 viewportX = 0;
        i16 viewportY = 0;
        i16 relativeX = 0;
        i16 relativeY = 0;
        i16 width = 0;
        i16 height = 0;

        [[nodiscard]] friend constexpr bool operator==(const EditorUiPrivateWindowPlacementFields&,
                                                       const EditorUiPrivateWindowPlacementFields&) noexcept = default;
    };

    enum class EditorUiPlacementResolveResult : u8
    {
        Resolved,
        EnvironmentUnavailable,
        InvalidArgument
    };

    enum class EditorUiUserStateLayoutLoadResult : u8
    {
        AppliedUsable,
        DiscardedUnusable,
        EnvironmentUnavailable,
        InvalidArgument
    };

    [[nodiscard]] EditorUiPlacementResolveResult ResolveEditorUiSavedPlacement(
        containers::ArraySpan<const EditorUiSavedDisplay> savedDisplays,
        containers::ArraySpan<const window::DisplaySnapshot> currentDisplays,
        const EditorUiSavedRect& savedRect, EditorUiRepairedPlacement& repaired,
        bool requirePrivateWindowEncoding = true) noexcept;
    [[nodiscard]] bool CaptureEditorUiSavedDisplay(const window::DisplaySnapshot& source,
                                                   EditorUiSavedDisplay& destination) noexcept;
    [[nodiscard]] bool QuantizeEditorUiDisplayScale(f32 scale, u32& scaleQ16_16) noexcept;
    [[nodiscard]] EditorUiSavedRect DecodeEditorUiPrivateWindowPlacement(
        const EditorUiPrivateWindowPlacementFields& fields) noexcept;
    [[nodiscard]] bool EncodeEditorUiPrivateWindowPlacement(
        const EditorUiSavedRect& repaired, EditorUiPrivateWindowPlacementFields& fields) noexcept;
    [[nodiscard]] constexpr bool ShouldResetEditorDockLayoutAfterUserStateLoad(
        const EditorUiUserStateLayoutLoadResult result, const EditorUiFailureCode failureCode) noexcept
    {
        return result != EditorUiUserStateLayoutLoadResult::AppliedUsable &&
               failureCode != EditorUiFailureCode::WrongThread &&
               failureCode != EditorUiFailureCode::WrongPhase &&
               failureCode != EditorUiFailureCode::NotReady;
    }
    [[nodiscard]] constexpr bool IsPersistedEditorDockTreeUsable(
        const bool rootExists, const bool rootIsDockSpace, const bool rootIsRootNode,
        const bool persistedCentralExists, const bool persistedCentralBelongsToRoot) noexcept
    {
        // Deliberately does not depend on ImGuiDockNode::CentralNode. That root-derived pointer is
        // populated only when DockSpace() first updates a tree reconstructed from saved settings.
        return rootExists && rootIsDockSpace && rootIsRootNode && persistedCentralExists &&
               persistedCentralBelongsToRoot;
    }

    class EditorUiPersistencePrivateService : public EditorUiService
    {
    public:
        ~EditorUiPersistencePrivateService() override = default;

        [[nodiscard]] virtual EditorUiUserStateLayoutLoadResult LoadUserStateLayout(
            containers::ArraySpan<const u8> bytes,
            containers::ArraySpan<const EditorUiSavedDisplay> savedDisplays,
            u32& repairedPlacementCount, EditorUiFailure* failure = nullptr) noexcept = 0;

        [[nodiscard]] virtual EditorUiUserStateLayoutLoadResult ApplyUserStateLayoutLive(
            containers::ArraySpan<const u8> bytes,
            containers::ArraySpan<const EditorUiSavedDisplay> savedDisplays,
            u32& repairedPlacementCount, EditorUiFailure* failure = nullptr) noexcept = 0;
        [[nodiscard]] virtual bool ResetUserStateLayoutToDefault(EditorUiFailure* failure = nullptr) noexcept = 0;

        [[nodiscard]] virtual bool CaptureUserStateLayout(
            void* destination, u32 capacity, u32& byteCount,
            EditorUiSavedDisplay* savedDisplays, u32 displayCapacity, u32& displayCount,
            EditorUiFailure* failure = nullptr) noexcept = 0;

        [[nodiscard]] virtual bool ConsumeUserStateLayoutDirty(EditorUiFailure* failure = nullptr) noexcept = 0;
        [[nodiscard]] virtual bool IsUserStateLayoutSettling(EditorUiFailure* failure = nullptr) const noexcept = 0;
        [[nodiscard]] virtual bool RefreshPanelUserStateRevision(EditorUiFailure* failure = nullptr) noexcept = 0;
        [[nodiscard]] virtual u64 GetPanelUserStateRevision() const noexcept = 0;
        [[nodiscard]] virtual bool ReconcilePanelPresentation(
            containers::ArraySpan<const EditorPanelUserStateRecord> records,
            EditorPanelUserStateRestoreStatus& status, EditorUiFailure* failure = nullptr) noexcept = 0;
        [[nodiscard]] virtual bool RequestNamedLayoutDialog(
            EditorUiNamedLayoutDialogKind kind, containers::ArraySpan<const EditorUiNamedLayoutChoice> choices,
            u64 activeId, containers::StringView initialName,
            EditorUiFailure* failure = nullptr) noexcept = 0;
        [[nodiscard]] virtual bool ConsumeNamedLayoutDialogResult(
            EditorUiNamedLayoutDialogResult& result, EditorUiFailure* failure = nullptr) noexcept = 0;

    protected:
        EditorUiPersistencePrivateService() noexcept = default;
    };

    [[nodiscard]] inline EditorUiPersistencePrivateService* FindEditorUiPersistencePrivateService(
        application::ServiceContext& context) noexcept
    {
        return static_cast<EditorUiPersistencePrivateService*>(context.FindCapability(EditorUiCapabilityId));
    }

    [[nodiscard]] bool CaptureEditorPanelUserState(
        EditorUiPersistencePrivateService& ui, EditorPanelUserStateRecord* records,
        u32 capacity, u32& count, EditorUiFailure* failure = nullptr) noexcept;
    [[nodiscard]] bool RestoreEditorPanelUserState(
        EditorUiPersistencePrivateService& ui, containers::ArraySpan<const EditorPanelUserStateRecord> records,
        EditorPanelUserStateRestoreStatus& status, EditorUiFailure* failure = nullptr) noexcept;
    [[nodiscard]] EditorPanelPersistentStateCapture CaptureEditorPanelPersistentState(
        EditorUiPersistencePrivateService& ui, EditorPanelTypeId type, EditorPanelInstanceId instance,
        EditorPanelStateScope scope, void* destination, u32 capacity, EditorUiFailure* failure = nullptr) noexcept;
    [[nodiscard]] EditorPanelStateResult RestoreEditorPanelPersistentState(
        EditorUiPersistencePrivateService& ui, EditorPanelTypeId type, EditorPanelInstanceId instance,
        EditorPanelStateScope scope, u32 version, containers::ArraySpan<const u8> bytes,
        void* migrationDestination, u32 migrationCapacity, EditorUiFailure* failure = nullptr) noexcept;
} // namespace vanguard::editor::detail
