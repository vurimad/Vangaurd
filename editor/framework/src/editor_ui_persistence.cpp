#include <vanguard/editor/editor_ui_persistence.hpp>

#include <vanguard/editor/editor_panels.hpp>

namespace vanguard::editor::detail
{
    bool CaptureEditorUiSavedDisplay(const window::DisplaySnapshot& source,
                                     EditorUiSavedDisplay& destination) noexcept
    {
        u32 scale = 0;
        if (!source.handle.IsValid() || source.fingerprint == 0 || !source.bounds.extent.IsValid() ||
            !source.workArea.extent.IsValid() || !source.desktopPixelExtent.IsValid() ||
            source.desktopRefreshRate.denominator == 0 ||
            !QuantizeEditorUiDisplayScale(source.contentScale, scale))
            return false;
        destination = {};
        destination.boundsX = source.bounds.origin.x;
        destination.boundsY = source.bounds.origin.y;
        destination.boundsWidth = source.bounds.extent.width;
        destination.boundsHeight = source.bounds.extent.height;
        destination.workX = source.workArea.origin.x;
        destination.workY = source.workArea.origin.y;
        destination.workWidth = source.workArea.extent.width;
        destination.workHeight = source.workArea.extent.height;
        destination.desktopPixelWidth = source.desktopPixelExtent.width;
        destination.desktopPixelHeight = source.desktopPixelExtent.height;
        destination.scaleQ16_16 = scale;
        destination.primary = source.primary;
        return true;
    }

    bool CaptureEditorPanelUserState(EditorUiPersistencePrivateService& ui, EditorPanelUserStateRecord* const records,
                                     const u32 capacity, u32& count, EditorUiFailure* const failure) noexcept
    {
        auto& panels = static_cast<EditorPanelRegistryCore&>(ui.GetPanels());
        return panels.CaptureUserState(records, capacity, count, failure);
    }

    bool RestoreEditorPanelUserState(EditorUiPersistencePrivateService& ui,
                                     const containers::ArraySpan<const EditorPanelUserStateRecord> records,
                                     EditorPanelUserStateRestoreStatus& status, EditorUiFailure* const failure) noexcept
    {
        auto& panels = static_cast<EditorPanelRegistryCore&>(ui.GetPanels());
        return panels.RestoreUserState(records, status, failure);
    }

    EditorPanelPersistentStateCapture CaptureEditorPanelPersistentState(
        EditorUiPersistencePrivateService& ui, const EditorPanelTypeId type,
        const EditorPanelInstanceId instance, const EditorPanelStateScope scope,
        void* const destination, const u32 capacity, EditorUiFailure* const failure) noexcept
    {
        auto& panels = static_cast<EditorPanelRegistryCore&>(ui.GetPanels());
        return panels.CapturePersistentState(type, instance, scope, destination, capacity, failure);
    }

    EditorPanelStateResult RestoreEditorPanelPersistentState(
        EditorUiPersistencePrivateService& ui, const EditorPanelTypeId type,
        const EditorPanelInstanceId instance, const EditorPanelStateScope scope,
        const u32 version, const containers::ArraySpan<const u8> bytes,
        void* const migrationDestination, const u32 migrationCapacity,
        EditorUiFailure* const failure) noexcept
    {
        auto& panels = static_cast<EditorPanelRegistryCore&>(ui.GetPanels());
        return panels.RestorePersistentState(type, instance, scope, version, bytes,
                                             migrationDestination, migrationCapacity, failure);
    }
} // namespace vanguard::editor::detail
