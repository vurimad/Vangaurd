#include <vanguard/editor/editor_static_extensions.hpp>

#include <vanguard/editor/editor_activity_service.hpp>
#include <vanguard/editor/editor_project_service.hpp>
#include <vanguard/asset_streaming/project_asset_pipeline.hpp>
#include <vanguard/memory/memory.hpp>

#include <cstdio>
#include <cstring>
#include <new>

namespace
{
    namespace assetStreaming = vanguard::asset_streaming;
    namespace editor = vanguard::editor;

    inline constexpr editor::EditorExtensionId ProjectAssetActivityExtensionId{0x6564333570616501ull};
    inline constexpr editor::EditorActivitySourceId ProjectAssetActivitySourceId = 0x6564333570617301ull;
    inline constexpr editor::EditorActivityItemId AutomaticRecookItemId = 1u;
    inline constexpr editor::EditorActivityItemId VddcMaintenanceItemId = 2u;
    inline constexpr editor::EditorActivityItemId LooseMaintenanceItemId = 3u;
    inline constexpr editor::EditorActivityItemId ObservationItemId = 4u;
    inline constexpr vanguard::u64 DisplayRowGeneration = 1u;

    struct ProjectAssetActivityState final
    {
        const assetStreaming::ProjectAssetPipeline* pipeline = nullptr;
    };

    [[nodiscard]] const char* AutomaticRecookStateName(
        const assetStreaming::ProjectAssetAutomaticRecookState state) noexcept
    {
        switch (state)
        {
        case assetStreaming::ProjectAssetAutomaticRecookState::Disabled: return "disabled";
        case assetStreaming::ProjectAssetAutomaticRecookState::Idle: return "idle";
        case assetStreaming::ProjectAssetAutomaticRecookState::Pending: return "pending";
        case assetStreaming::ProjectAssetAutomaticRecookState::Planning: return "planning";
        case assetStreaming::ProjectAssetAutomaticRecookState::Building: return "building";
        case assetStreaming::ProjectAssetAutomaticRecookState::Validating: return "validating";
        case assetStreaming::ProjectAssetAutomaticRecookState::Cancelling: return "cancelling";
        case assetStreaming::ProjectAssetAutomaticRecookState::Failed: return "failed";
        case assetStreaming::ProjectAssetAutomaticRecookState::RecoveryRequired: return "recovery required";
        }
        return "unknown";
    }

    [[nodiscard]] editor::EditorActivityState AutomaticRecookActivityState(
        const assetStreaming::ProjectAssetAutomaticRecookState state) noexcept
    {
        switch (state)
        {
        case assetStreaming::ProjectAssetAutomaticRecookState::Disabled:
        case assetStreaming::ProjectAssetAutomaticRecookState::Idle:
            return editor::EditorActivityState::Idle;
        case assetStreaming::ProjectAssetAutomaticRecookState::Pending:
            return editor::EditorActivityState::Pending;
        case assetStreaming::ProjectAssetAutomaticRecookState::Planning:
        case assetStreaming::ProjectAssetAutomaticRecookState::Building:
        case assetStreaming::ProjectAssetAutomaticRecookState::Validating:
            return editor::EditorActivityState::Running;
        case assetStreaming::ProjectAssetAutomaticRecookState::Cancelling:
            return editor::EditorActivityState::Cancelling;
        case assetStreaming::ProjectAssetAutomaticRecookState::Failed:
            return editor::EditorActivityState::Failed;
        case assetStreaming::ProjectAssetAutomaticRecookState::RecoveryRequired:
            return editor::EditorActivityState::Blocked;
        }
        return editor::EditorActivityState::Blocked;
    }

    [[nodiscard]] editor::EditorActivityAttention AutomaticRecookAttention(
        const assetStreaming::ProjectAssetAutomaticRecookStatus& status) noexcept
    {
        if (status.state == assetStreaming::ProjectAssetAutomaticRecookState::Failed)
            return editor::EditorActivityAttention::Error;
        if (status.state == assetStreaming::ProjectAssetAutomaticRecookState::RecoveryRequired ||
            status.recoveryRequired)
            return editor::EditorActivityAttention::Warning;
        return editor::EditorActivityAttention::None;
    }

    [[nodiscard]] const char* VddcMaintenanceStateName(
        const assetStreaming::ProjectAssetVddcMaintenanceState state) noexcept
    {
        switch (state)
        {
        case assetStreaming::ProjectAssetVddcMaintenanceState::Idle: return "idle";
        case assetStreaming::ProjectAssetVddcMaintenanceState::Requested: return "requested";
        case assetStreaming::ProjectAssetVddcMaintenanceState::Running: return "running";
        case assetStreaming::ProjectAssetVddcMaintenanceState::Complete: return "complete";
        case assetStreaming::ProjectAssetVddcMaintenanceState::Deferred: return "deferred";
        case assetStreaming::ProjectAssetVddcMaintenanceState::Failed: return "failed";
        case assetStreaming::ProjectAssetVddcMaintenanceState::Cancelled: return "cancelled";
        }
        return "unknown";
    }

    [[nodiscard]] editor::EditorActivityState VddcMaintenanceActivityState(
        const assetStreaming::ProjectAssetVddcMaintenanceState state) noexcept
    {
        switch (state)
        {
        case assetStreaming::ProjectAssetVddcMaintenanceState::Idle:
            return editor::EditorActivityState::Idle;
        case assetStreaming::ProjectAssetVddcMaintenanceState::Requested:
            return editor::EditorActivityState::Pending;
        case assetStreaming::ProjectAssetVddcMaintenanceState::Running:
            return editor::EditorActivityState::Running;
        case assetStreaming::ProjectAssetVddcMaintenanceState::Complete:
            return editor::EditorActivityState::Succeeded;
        case assetStreaming::ProjectAssetVddcMaintenanceState::Deferred:
            return editor::EditorActivityState::Blocked;
        case assetStreaming::ProjectAssetVddcMaintenanceState::Failed:
            return editor::EditorActivityState::Failed;
        case assetStreaming::ProjectAssetVddcMaintenanceState::Cancelled:
            return editor::EditorActivityState::Cancelled;
        }
        return editor::EditorActivityState::Blocked;
    }

    [[nodiscard]] const char* LooseMaintenanceStateName(
        const assetStreaming::ProjectAssetLooseMaintenanceState state) noexcept
    {
        switch (state)
        {
        case assetStreaming::ProjectAssetLooseMaintenanceState::Idle: return "idle";
        case assetStreaming::ProjectAssetLooseMaintenanceState::Requested: return "requested";
        case assetStreaming::ProjectAssetLooseMaintenanceState::Running: return "running";
        case assetStreaming::ProjectAssetLooseMaintenanceState::Complete: return "complete";
        case assetStreaming::ProjectAssetLooseMaintenanceState::Deferred: return "deferred";
        case assetStreaming::ProjectAssetLooseMaintenanceState::Failed: return "failed";
        case assetStreaming::ProjectAssetLooseMaintenanceState::Cancelled: return "cancelled";
        }
        return "unknown";
    }

    [[nodiscard]] editor::EditorActivityState LooseMaintenanceActivityState(
        const assetStreaming::ProjectAssetLooseMaintenanceState state) noexcept
    {
        switch (state)
        {
        case assetStreaming::ProjectAssetLooseMaintenanceState::Idle:
            return editor::EditorActivityState::Idle;
        case assetStreaming::ProjectAssetLooseMaintenanceState::Requested:
            return editor::EditorActivityState::Pending;
        case assetStreaming::ProjectAssetLooseMaintenanceState::Running:
            return editor::EditorActivityState::Running;
        case assetStreaming::ProjectAssetLooseMaintenanceState::Complete:
            return editor::EditorActivityState::Succeeded;
        case assetStreaming::ProjectAssetLooseMaintenanceState::Deferred:
            return editor::EditorActivityState::Blocked;
        case assetStreaming::ProjectAssetLooseMaintenanceState::Failed:
            return editor::EditorActivityState::Failed;
        case assetStreaming::ProjectAssetLooseMaintenanceState::Cancelled:
            return editor::EditorActivityState::Cancelled;
        }
        return editor::EditorActivityState::Blocked;
    }

    [[nodiscard]] editor::EditorActivityAttention MaintenanceAttention(
        const bool failed, const bool warning) noexcept
    {
        if (failed)
            return editor::EditorActivityAttention::Error;
        return warning ? editor::EditorActivityAttention::Warning
                       : editor::EditorActivityAttention::None;
    }

    [[nodiscard]] bool AddStatusRow(
        editor::detail::EditorActivitySourceWriter& writer,
        const editor::EditorActivityItemId id,
        const editor::EditorActivityState state,
        const editor::EditorActivityAttention attention,
        const char* const label,
        const char* const detail,
        editor::EditorActivityFailure* const failure) noexcept
    {
        editor::detail::EditorActivityItemDescriptor row;
        row.id = id;
        row.generation = DisplayRowGeneration;
        row.kind = editor::EditorActivityKind::Status;
        row.state = state;
        row.attention = attention;
        row.cancellable = false;
        row.label = label;
        row.detail = detail;
        return writer.Add(row, failure);
    }

    [[nodiscard]] bool AddUnavailableRows(
        editor::detail::EditorActivitySourceWriter& writer,
        editor::EditorActivityFailure* const failure) noexcept
    {
        constexpr const char* unavailable = "Project asset pipeline is unavailable.";
        return AddStatusRow(writer, AutomaticRecookItemId, editor::EditorActivityState::Blocked,
                            editor::EditorActivityAttention::Warning, "Asset recook / reimport",
                            unavailable, failure) &&
               AddStatusRow(writer, VddcMaintenanceItemId, editor::EditorActivityState::Blocked,
                            editor::EditorActivityAttention::Warning, "Derived-data maintenance",
                            unavailable, failure) &&
               AddStatusRow(writer, LooseMaintenanceItemId, editor::EditorActivityState::Blocked,
                            editor::EditorActivityAttention::Warning, "Loose-output maintenance",
                            unavailable, failure) &&
               AddStatusRow(writer, ObservationItemId, editor::EditorActivityState::Blocked,
                            editor::EditorActivityAttention::Warning, "Source observation",
                            unavailable, failure);
    }

    [[nodiscard]] bool QueryProjectAssetActivity(
        editor::detail::EditorActivitySourceWriter& writer, void* const userData,
        editor::EditorActivityFailure* const failure) noexcept
    {
        const auto* const state = static_cast<const ProjectAssetActivityState*>(userData);
        if (state == nullptr || state->pipeline == nullptr || !state->pipeline->IsInitialized())
            return AddUnavailableRows(writer, failure);

        const assetStreaming::ProjectAssetAutomaticRecookStatus automatic =
            state->pipeline->GetAutomaticRecookStatus();
        char automaticDetail[editor::MaximumEditorActivityDetailBytes]{};
        std::snprintf(
            automaticDetail, sizeof(automaticDetail),
            "%s; build operations=%u/%u; pending assets=%u inputs=%u outputs=%u; reimport=%u repair=%u delivery=%u; retries=%u%s%s",
            AutomaticRecookStateName(automatic.state), automatic.completedBuildOperations, automatic.buildOperations,
            automatic.pendingAssets,
            automatic.pendingInputs, automatic.pendingOutputs, automatic.pendingReimportOutputs,
            automatic.pendingRepairOutputs, automatic.pendingDeliveryOutputs, automatic.retryEntries,
            automatic.retryRequested ? "; retry requested" : "",
            automatic.recoveryRequired ? "; recovery required" : "");
        if (automatic.state == assetStreaming::ProjectAssetAutomaticRecookState::Failed ||
            automatic.state == assetStreaming::ProjectAssetAutomaticRecookState::RecoveryRequired)
        {
            const std::size_t used = std::strlen(automaticDetail);
            std::snprintf(automaticDetail + used, sizeof(automaticDetail) - used,
                          "; reason=%s; failedRoots=%u; graph=%s; compile=%s; %s",
                          vanguard::assets::ToString(automatic.lastRecookFailure),
                          automatic.failedRootCount,
                          vanguard::assets::ToString(automatic.firstFailedRoot.graphFailure),
                          vanguard::assets::ToString(automatic.firstFailedRoot.buildResult),
                          automatic.firstFailedRoot.message);
        }
        if (!AddStatusRow(writer, AutomaticRecookItemId, AutomaticRecookActivityState(automatic.state),
                          AutomaticRecookAttention(automatic), "Asset recook / reimport",
                          automaticDetail, failure))
            return false;

        const assetStreaming::ProjectAssetVddcMaintenanceStatus vddc =
            state->pipeline->GetVddcMaintenanceStatus();
        char vddcDetail[editor::MaximumEditorActivityDetailBytes]{};
        std::snprintf(
            vddcDetail, sizeof(vddcDetail),
            "%s; scanned=%u eligible=%u removed=%u deferred=%u failed=%u%s%s%s",
            VddcMaintenanceStateName(vddc.state), vddc.scannedRecords, vddc.eligibleRecords,
            vddc.removedRecords, vddc.deferredDeletes, vddc.failedDeletes,
            vddc.retryRequired ? "; retry required" : "",
            vddc.limitExceeded ? "; limit exceeded" : "",
            vddc.statusRowsTruncated ? "; diagnostics truncated" : "");
        if (!AddStatusRow(
                writer, VddcMaintenanceItemId, VddcMaintenanceActivityState(vddc.state),
                MaintenanceAttention(vddc.state == assetStreaming::ProjectAssetVddcMaintenanceState::Failed,
                                     vddc.state == assetStreaming::ProjectAssetVddcMaintenanceState::Deferred ||
                                         vddc.retryRequired || vddc.limitExceeded),
                "Derived-data maintenance", vddcDetail, failure))
            return false;

        const assetStreaming::ProjectAssetLooseMaintenanceStatus loose =
            state->pipeline->GetLooseMaintenanceStatus();
        char looseDetail[editor::MaximumEditorActivityDetailBytes]{};
        std::snprintf(
            looseDetail, sizeof(looseDetail),
            "%s; tracked=%u scanned=%u removed=%u deferred=%u failed=%u%s%s%s",
            LooseMaintenanceStateName(loose.state), loose.trackedCandidates, loose.scannedRecords,
            loose.removedRecords, loose.deferredDeletes, loose.failedDeletes,
            loose.retryRequired ? "; retry required" : "",
            loose.limitExceeded ? "; limit exceeded" : "",
            loose.statusRowsTruncated ? "; diagnostics truncated" : "");
        if (!AddStatusRow(
                writer, LooseMaintenanceItemId, LooseMaintenanceActivityState(loose.state),
                MaintenanceAttention(loose.state == assetStreaming::ProjectAssetLooseMaintenanceState::Failed,
                                     loose.state == assetStreaming::ProjectAssetLooseMaintenanceState::Deferred ||
                                         loose.retryRequired || loose.limitExceeded),
                "Loose-output maintenance", looseDetail, failure))
            return false;

        const assetStreaming::ProjectAssetObservationStatus observation =
            state->pipeline->GetObservationStatus();
        const bool observationWarning = observation.overflow || observation.rescanRequired;
        const editor::EditorActivityState observationState =
            !observation.running
                ? editor::EditorActivityState::Idle
                : (observation.reconcilePending || observation.rescanRequired
                       ? editor::EditorActivityState::Pending
                       : editor::EditorActivityState::Running);
        char observationDetail[editor::MaximumEditorActivityDetailBytes]{};
        std::snprintf(
            observationDetail, sizeof(observationDetail),
            "%s; roots=%u pending=%u deferred=%u ready=%u; reconciled=%u%s%s",
            observation.running ? "running" : "idle", observation.watchedRoots,
            observation.pendingPaths, observation.deferredPaths, observation.readyPaths,
            observation.reconcileOperationsCompleted,
            observation.rescanRequired ? "; rescan required" : "",
            observation.overflow ? "; overflow" : "");
        return AddStatusRow(writer, ObservationItemId, observationState,
                            observationWarning ? editor::EditorActivityAttention::Warning
                                               : editor::EditorActivityAttention::None,
                            "Source observation", observationDetail, failure);
    }

    bool ActivateProjectAssetActivity(editor::EditorExtensionActivationContext& context,
                                      const void*, void*& privateState) noexcept
    {
        const vanguard::memory::MemoryBlock block =
            vanguard::memory::Allocate(vanguard::memory::PoolId::Tools,
                                       sizeof(ProjectAssetActivityState),
                                       alignof(ProjectAssetActivityState));
        if (!block)
            return false;

        auto* const state = ::new (block.address) ProjectAssetActivityState();
        state->pipeline = context.GetWorkspace().GetAssetPipeline();
        privateState = state;

        editor::detail::EditorActivitySourceDescriptor source;
        source.id = ProjectAssetActivitySourceId;
        source.displayName = "Project Assets";
        source.query = QueryProjectAssetActivity;
        source.requestCancel = nullptr;
        source.userData = state;

        editor::detail::EditorActivitySourceToken sourceToken;
        editor::EditorActivityFailure failure;
        return context.RegisterActivitySource(source, sourceToken, &failure);
    }

    void DeactivateProjectAssetActivity(editor::EditorExtensionActivationContext&,
                                        const void*, void* const privateState) noexcept
    {
        if (privateState == nullptr)
            return;
        auto* const state = static_cast<ProjectAssetActivityState*>(privateState);
        state->~ProjectAssetActivityState();
        vanguard::memory::MemoryBlock block{
            state, sizeof(ProjectAssetActivityState), vanguard::memory::PoolId::Tools};
        vanguard::memory::Free(block);
    }

    const editor::EditorExtensionDescriptor ProjectAssetActivityExtensions[]{
        {ProjectAssetActivityExtensionId,
         "editor-project-asset-activity",
         "Project Asset Activity",
         {},
         editor::EditorExtensionSelectionPolicy::BuiltIn,
         ActivateProjectAssetActivity,
         DeactivateProjectAssetActivity,
         nullptr}};
} // namespace

namespace vanguard::editor
{
    containers::ArraySpan<const EditorExtensionDescriptor>
    GetEditorProjectAssetActivityExtensions() noexcept
    {
        return ProjectAssetActivityExtensions;
    }
} // namespace vanguard::editor
