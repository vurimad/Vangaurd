#include <vanguard/nanovanguard/project_asset_commands.hpp>

#include <vanguard/nanovanguard/command_cancellation.hpp>
#include <vanguard/nanovanguard/project_asset_context.hpp>
#include <vanguard/nanovanguard/project_platform.hpp>

#include <vanguard/assets/asset_operations.hpp>
#include <vanguard/concurrency/concurrency.hpp>
#include <vanguard/filesystem/filesystem.hpp>
#include <vanguard/projects/project.hpp>
#include <vanguard/projects/project_workspace.hpp>
#include <vanguard/resources/resources.hpp>

namespace
{
    using namespace vanguard;
    namespace asset_streaming = vanguard::asset_streaming;
    namespace assets = vanguard::assets;
    namespace containers = vanguard::containers;
    namespace filesystem = vanguard::filesystem;
    namespace nano = vanguard::nanovanguard;
    namespace platform = vanguard::nanovanguard::platform;
    namespace projects = vanguard::projects;
    namespace resources = vanguard::resources;
    namespace streaming = vanguard::streaming;

    inline constexpr u32 MaximumStatusDiagnostics = 64u;

    struct ResolvedAssetProject final
    {
        containers::String root;
        containers::String file;
        projects::ProjectDescriptor descriptor;
        projects::ProjectWorkspace workspace;
        containers::String target;
    };

    struct AssetCommandEnvironment final
    {
        ResolvedAssetProject project;
        nano::ProjectAssetContext context;
        asset_streaming::ProjectAssetPipeline* pipeline = nullptr;
    };

    struct SelectedAsset final
    {
        const asset_streaming::ProjectAssetState* state = nullptr;
        containers::String canonicalSource;
        const asset_streaming::ProjectAssetOutputState* output = nullptr;
    };

    [[nodiscard]] bool Equal(const char* left, const char* right) noexcept
    {
        if (left == nullptr || right == nullptr)
            return left == right;
        while (*left != '\0' && *left == *right)
        {
            ++left;
            ++right;
        }
        return *left == '\0' && *right == '\0';
    }

    void Assign(containers::String& destination, const containers::StringView source) noexcept
    {
        destination.Clear();
        destination.Append(source);
    }

    [[nodiscard]] bool Parent(const containers::StringView path, containers::String& output) noexcept
    {
        u32 separator = containers::StringView::npos;
        for (u32 index = 0; index < path.Length(); ++index)
            if (path[index] == '/' || path[index] == '\\')
                separator = index;
        if (separator == containers::StringView::npos || separator == 0u)
            return false;
        Assign(output, path.Slice(0u, separator));
        return true;
    }

    [[nodiscard]] bool Emit(nano::Output& output, const nano::OutputFormat format,
                            const nano::SemanticEventKind kind, const char* const operation,
                            const containers::ArraySpan<const nano::SemanticField> fields = {}) noexcept
    {
        if (fields.Count() > nano::MaximumSemanticEventFields)
            return false;
        nano::SemanticEvent event;
        event.event = kind;
        event.operation = operation;
        for (const nano::SemanticField& field : fields)
        {
            bool added = false;
            if (field.kind == nano::SemanticFieldKind::String)
                added = event.AddString(field.name, field.stringValue);
            else if (field.kind == nano::SemanticFieldKind::Unsigned)
                added = event.AddUnsigned(field.name, field.unsignedValue);
            else
                added = event.AddBoolean(field.name, field.booleanValue);
            if (!added)
                return false;
        }
        return nano::WriteSemanticEvent(output, format, event);
    }

    [[nodiscard]] nano::SemanticField StringField(const char* const name, const char* const value) noexcept
    {
        return {name, nano::SemanticFieldKind::String, value, 0u, false};
    }

    [[nodiscard]] nano::SemanticField UnsignedField(const char* const name, const u64 value) noexcept
    {
        return {name, nano::SemanticFieldKind::Unsigned, nullptr, value, false};
    }

    [[nodiscard]] nano::SemanticField BooleanField(const char* const name, const bool value) noexcept
    {
        return {name, nano::SemanticFieldKind::Boolean, nullptr, 0u, value};
    }

    [[nodiscard]] nano::ExitCode Finish(nano::Output& output, const nano::OutputFormat format,
                                        const char* const operation, const nano::SemanticEventKind terminal,
                                        const nano::ExitCode code, const char* const reason,
                                        const char* const message = nullptr) noexcept
    {
        nano::SemanticField fields[2]{};
        u32 count = 0u;
        if (reason != nullptr)
            fields[count++] = StringField("reason", reason);
        if (message != nullptr)
            fields[count++] = StringField("message", message);
        if (!Emit(output, format, terminal, operation, {fields, count}))
            return nano::ExitCode::InternalFailure;
        return code;
    }

    [[nodiscard]] const char* ProjectResultToken(const projects::Result result) noexcept
    {
        switch (result)
        {
        case projects::Result::Success: return "success";
        case projects::Result::InvalidArgument: return "invalid-argument";
        case projects::Result::IoFailure: return "io-failure";
        case projects::Result::FileTooLarge: return "file-too-large";
        case projects::Result::InvalidUtf8: return "invalid-utf8";
        case projects::Result::InvalidSyntax: return "invalid-syntax";
        case projects::Result::UnsupportedVersion: return "unsupported-version";
        case projects::Result::UnknownField: return "unknown-field";
        case projects::Result::DuplicateField: return "duplicate-field";
        case projects::Result::MissingField: return "missing-field";
        case projects::Result::InvalidValue: return "invalid-value";
        case projects::Result::LimitExceeded: return "limit-exceeded";
        }
        return "unknown";
    }

    [[nodiscard]] const char* PipelineResultToken(const asset_streaming::ProjectAssetPipelineResult result) noexcept
    {
        using Result = asset_streaming::ProjectAssetPipelineResult;
        switch (result)
        {
        case Result::Success: return "success";
        case Result::Pending: return "pending";
        case Result::InvalidArgument: return "invalid-argument";
        case Result::InvalidState: return "invalid-state";
        case Result::Busy: return "busy";
        case Result::BuildInitializationFailed: return "build-initialization-failed";
        case Result::BuildRequestFailed: return "build-request-failed";
        case Result::IndexSaveFailed: return "index-save-failed";
        case Result::PublicationFailed: return "publication-failed";
        case Result::ReconciliationFailed: return "reconciliation-failed";
        case Result::SnapshotLoadFailed: return "snapshot-load-failed";
        case Result::SnapshotSaveFailed: return "snapshot-save-failed";
        case Result::OperationInitializationFailed: return "operation-initialization-failed";
        case Result::OperationFailed: return "operation-failed";
        case Result::IdentityGenerationFailed: return "identity-generation-failed";
        case Result::HistoryLimitExceeded: return "history-limit-exceeded";
        case Result::UndoUnavailable: return "undo-unavailable";
        case Result::RedoUnavailable: return "redo-unavailable";
        case Result::CommittedCleanupPending: return "committed-cleanup-pending";
        case Result::CommittedReconciliationPending: return "committed-reconciliation-pending";
        case Result::ShutdownFailed: return "shutdown-failed";
        case Result::ObservationUnavailable: return "observation-unavailable";
        case Result::ObservationAlreadyActive: return "observation-already-active";
        case Result::ObservationNotActive: return "observation-not-active";
        case Result::CacheMaintenanceFailed: return "cache-maintenance-failed";
        case Result::LooseMaintenanceFailed: return "loose-maintenance-failed";
        }
        return "unknown";
    }

    [[nodiscard]] const char* ContextResultToken(const nano::ProjectAssetContextResult result) noexcept
    {
        using Result = nano::ProjectAssetContextResult;
        switch (result)
        {
        case Result::Success: return "success";
        case Result::InvalidArgument: return "invalid-argument";
        case Result::InvalidState: return "invalid-state";
        case Result::InitializationFailed: return "initialization-failed";
        case Result::SourceScanFailed: return "source-scan-failed";
        case Result::PipelineInitializationFailed: return "pipeline-initialization-failed";
        case Result::ShutdownFailed: return "shutdown-failed";
        }
        return "unknown";
    }

    [[nodiscard]] const char* DirtyReasonToken(const assets::DirtyReason reason) noexcept
    {
        switch (reason)
        {
        case assets::DirtyReason::UpToDate: return "up-to-date";
        case assets::DirtyReason::Missing: return "missing";
        case assets::DirtyReason::SourceChanged: return "source-changed";
        case assets::DirtyReason::CompilerChanged: return "compiler-changed";
        case assets::DirtyReason::DependenciesChanged: return "dependencies-changed";
        }
        return "unknown";
    }

    [[nodiscard]] const char* AutomaticStateToken(const asset_streaming::ProjectAssetAutomaticRecookState state) noexcept
    {
        using State = asset_streaming::ProjectAssetAutomaticRecookState;
        switch (state)
        {
        case State::Disabled: return "disabled";
        case State::Idle: return "idle";
        case State::Pending: return "pending";
        case State::Planning: return "planning";
        case State::Building: return "building";
        case State::Validating: return "validating";
        case State::Cancelling: return "cancelling";
        case State::Failed: return "failed";
        case State::RecoveryRequired: return "recovery-required";
        }
        return "unknown";
    }

    [[nodiscard]] const char* BuildStateToken(const assets::BuildState state) noexcept
    {
        switch (state)
        {
        case assets::BuildState::Resolving: return "resolving";
        case assets::BuildState::Queued: return "queued";
        case assets::BuildState::Building: return "building";
        case assets::BuildState::Succeeded: return "succeeded";
        case assets::BuildState::Failed: return "failed";
        case assets::BuildState::Cancelled: return "cancelled";
        }
        return "unknown";
    }

    [[nodiscard]] const char* DeliveryStageToken(const asset_streaming::ProjectAssetDeliveryStageState state) noexcept
    {
        using State = asset_streaming::ProjectAssetDeliveryStageState;
        switch (state)
        {
        case State::Untracked: return "untracked";
        case State::Pending: return "pending";
        case State::Committed: return "committed";
        case State::Installed: return "installed";
        case State::Current: return "current";
        case State::Stale: return "stale";
        case State::Failed: return "failed";
        }
        return "unknown";
    }

    [[nodiscard]] const char* DeliveryReasonToken(const asset_streaming::ProjectAssetDeliveryReason reason) noexcept
    {
        using Reason = asset_streaming::ProjectAssetDeliveryReason;
        switch (reason)
        {
        case Reason::None: return "none";
        case Reason::AwaitingPublication: return "awaiting-publication";
        case Reason::BuildAdvancedPastPublication: return "build-advanced-past-publication";
        case Reason::AwaitingResidentReplacement: return "awaiting-resident-replacement";
        case Reason::ResidentNotTracked: return "resident-not-tracked";
        case Reason::DeliveryCapacityExceeded: return "delivery-capacity-exceeded";
        case Reason::PublicationFailed: return "publication-failed";
        case Reason::PublicationFreshnessRejected: return "publication-freshness-rejected";
        case Reason::CacheRepairRequired: return "cache-repair-required";
        }
        return "unknown";
    }

    [[nodiscard]] const char* PresenceToken(const asset_streaming::ProjectAssetPresence presence) noexcept
    {
        using Presence = asset_streaming::ProjectAssetPresence;
        switch (presence)
        {
        case Presence::Present: return "present";
        case Presence::Missing: return "missing";
        case Presence::Conflicting: return "conflicting";
        }
        return "unknown";
    }

    [[nodiscard]] const char* ImportStateToken(const assets::SourceImportState state) noexcept
    {
        switch (state)
        {
        case assets::SourceImportState::NotClassified: return "not-classified";
        case assets::SourceImportState::MetadataSelected: return "metadata-selected";
        case assets::SourceImportState::Candidate: return "candidate";
        case assets::SourceImportState::Unsupported: return "unsupported";
        case assets::SourceImportState::DiscoveryUnavailable: return "discovery-unavailable";
        case assets::SourceImportState::Ambiguous: return "ambiguous";
        case assets::SourceImportState::ImporterUnavailable: return "importer-unavailable";
        case assets::SourceImportState::VersionMismatch: return "version-mismatch";
        case assets::SourceImportState::SourceMismatch: return "source-mismatch";
        case assets::SourceImportState::InvalidMetadata: return "invalid-metadata";
        }
        return "unknown";
    }

    [[nodiscard]] const char* VddcMaintenanceStateToken(const asset_streaming::ProjectAssetVddcMaintenanceState state) noexcept
    {
        using State = asset_streaming::ProjectAssetVddcMaintenanceState;
        switch (state)
        {
        case State::Idle: return "idle";
        case State::Requested: return "requested";
        case State::Running: return "running";
        case State::Complete: return "complete";
        case State::Deferred: return "deferred";
        case State::Failed: return "failed";
        case State::Cancelled: return "cancelled";
        }
        return "unknown";
    }

    [[nodiscard]] const char* VddcMaintenanceResultToken(const assets::PersistentCacheMaintenanceResult result) noexcept
    {
        using Result = assets::PersistentCacheMaintenanceResult;
        switch (result)
        {
        case Result::Success: return "success";
        case Result::Deferred: return "deferred";
        case Result::InvalidArgument: return "invalid-argument";
        case Result::InvalidState: return "invalid-state";
        case Result::ScanFailed: return "scan-failed";
        case Result::LimitExceeded: return "limit-exceeded";
        case Result::OutOfMemory: return "out-of-memory";
        case Result::ProtectionChanged: return "protection-changed";
        case Result::Cancelled: return "cancelled";
        }
        return "unknown";
    }

    [[nodiscard]] const char* LooseMaintenanceStateToken(const asset_streaming::ProjectAssetLooseMaintenanceState state) noexcept
    {
        using State = asset_streaming::ProjectAssetLooseMaintenanceState;
        switch (state)
        {
        case State::Idle: return "idle";
        case State::Requested: return "requested";
        case State::Running: return "running";
        case State::Complete: return "complete";
        case State::Deferred: return "deferred";
        case State::Failed: return "failed";
        case State::Cancelled: return "cancelled";
        }
        return "unknown";
    }

    [[nodiscard]] const char* LooseMaintenanceResultToken(const asset_streaming::ProjectAssetLooseMaintenanceResult result) noexcept
    {
        using Result = asset_streaming::ProjectAssetLooseMaintenanceResult;
        switch (result)
        {
        case Result::Success: return "success";
        case Result::Deferred: return "deferred";
        case Result::InvalidState: return "invalid-state";
        case Result::LimitExceeded: return "limit-exceeded";
        case Result::StreamingBusy: return "streaming-busy";
        case Result::IoFailure: return "io-failure";
        case Result::Cancelled: return "cancelled";
        }
        return "unknown";
    }

    [[nodiscard]] const char* AssetOperationResultToken(const assets::AssetOperationResult result) noexcept
    {
        using Result = assets::AssetOperationResult;
        switch (result)
        {
        case Result::Success: return "success";
        case Result::InvalidArgument: return "invalid-argument";
        case Result::ReplacementUnavailable: return "replacement-unavailable";
        case Result::ReferenceStateUnavailable: return "reference-state-unavailable";
        case Result::ReadOnlySource: return "read-only-source";
        case Result::ImporterUnavailable: return "importer-unavailable";
        case Result::RewriteUnavailable: return "rewrite-unavailable";
        case Result::RelocationUnavailable: return "relocation-unavailable";
        case Result::SourceUnavailable: return "source-unavailable";
        case Result::CandidateUnavailable: return "candidate-unavailable";
        case Result::SettingsInvalid: return "settings-invalid";
        case Result::TargetConflict: return "target-conflict";
        case Result::IdentityConflict: return "identity-conflict";
        case Result::ReferencesRemain: return "references-remain";
        case Result::RewriteFailed: return "rewrite-failed";
        case Result::TransactionFailed: return "transaction-failed";
        case Result::LimitExceeded: return "limit-exceeded";
        case Result::UnsupportedSource: return "unsupported-source";
        case Result::AmbiguousSource: return "ambiguous-source";
        case Result::ImportDescriptionUnavailable: return "import-description-unavailable";
        }
        return "unknown";
    }

    [[nodiscard]] const char* AssetTransactionResultToken(const assets::AssetFileTransactionResult result) noexcept
    {
        using Result = assets::AssetFileTransactionResult;
        switch (result)
        {
        case Result::Success: return "success";
        case Result::InvalidState: return "invalid-state";
        case Result::InvalidArgument: return "invalid-argument";
        case Result::LimitExceeded: return "limit-exceeded";
        case Result::Conflict: return "conflict";
        case Result::IoFailure: return "io-failure";
        case Result::InvalidJournal: return "invalid-journal";
        case Result::RecoveryRequired: return "recovery-required";
        case Result::CommittedCleanupPending: return "committed-cleanup-pending";
        }
        return "unknown";
    }

    [[nodiscard]] const char* VddcMaintenanceDiagnosticReasonToken(
        const assets::PersistentCacheMaintenanceDiagnosticReason reason) noexcept
    {
        using Reason = assets::PersistentCacheMaintenanceDiagnosticReason;
        switch (reason)
        {
        case Reason::LeaseBusy: return "lease-busy";
        case Reason::InvalidCandidate: return "invalid-candidate";
        case Reason::IoFailure: return "io-failure";
        }
        return "unknown";
    }

    [[nodiscard]] const char* LooseRetireResultToken(const streaming::LooseGenerationRetireResult result) noexcept
    {
        using Result = streaming::LooseGenerationRetireResult;
        switch (result)
        {
        case Result::Removed: return "removed";
        case Result::NotManaged: return "not-managed";
        case Result::Current: return "current";
        case Result::Busy: return "busy";
        case Result::InvalidPath: return "invalid-path";
        case Result::LimitExceeded: return "limit-exceeded";
        case Result::IoFailure: return "io-failure";
        }
        return "unknown";
    }

    [[nodiscard]] bool BuildFingerprintHex(const assets::BuildFingerprint& fingerprint, char (&text)[65]) noexcept
    {
        constexpr char hexadecimal[] = "0123456789abcdef";
        for (u32 index = 0u; index < assets::BuildFingerprint::ByteCount; ++index)
        {
            text[index * 2u] = hexadecimal[fingerprint.bytes[index] >> 4u];
            text[index * 2u + 1u] = hexadecimal[fingerprint.bytes[index] & 0x0fu];
        }
        text[64] = '\0';
        return true;
    }

    void AppendSourceIssue(containers::String& output, const u32 value, const u32 issue, const char* const token) noexcept
    {
        if ((value & issue) == 0u)
            return;
        if (!output.Empty())
            output.Append(',');
        output.Append(containers::StringView(token));
    }

    [[nodiscard]] containers::String SourceIssuesToken(const u32 issues) noexcept
    {
        containers::String result;
        AppendSourceIssue(result, issues, assets::SourceMissing, "source-missing");
        AppendSourceIssue(result, issues, assets::MetadataMissing, "metadata-missing");
        AppendSourceIssue(result, issues, assets::MetadataUnreadable, "metadata-unreadable");
        AppendSourceIssue(result, issues, assets::MetadataInvalid, "metadata-invalid");
        AppendSourceIssue(result, issues, assets::AssetIdentityConflict, "asset-identity-conflict");
        AppendSourceIssue(result, issues, assets::OutputIdentityConflict, "output-identity-conflict");
        AppendSourceIssue(result, issues, assets::SourceIdentityConflict, "source-identity-conflict");
        if (result.Empty())
            result.Append(containers::StringView("none"));
        return result;
    }

    [[nodiscard]] const char* SourceDiagnosticReasonToken(const asset_streaming::ProjectAssetSourceDiagnosticReason reasons) noexcept
    {
        using Reason = asset_streaming::ProjectAssetSourceDiagnosticReason;
        const bool ambiguousMove = asset_streaming::HasSourceDiagnosticReason(reasons, Reason::AmbiguousMove);
        const bool contentUnavailable = asset_streaming::HasSourceDiagnosticReason(reasons, Reason::SourceContentUnavailable);
        if (ambiguousMove && contentUnavailable)
            return "ambiguous-move,source-content-unavailable";
        if (ambiguousMove)
            return "ambiguous-move";
        if (contentUnavailable)
            return "source-content-unavailable";
        return "none";
    }

    [[nodiscard]] const char* BuildResultToken(const assets::ProjectAssetBuildResult result) noexcept
    {
        using Result = assets::ProjectAssetBuildResult;
        switch (result)
        {
        case Result::Success: return "success";
        case Result::InvalidArgument: return "invalid-argument";
        case Result::BuildSystemInitializationFailed: return "build-system-initialization-failed";
        case Result::CompilerRegistrationFailed: return "compiler-registration-failed";
        case Result::DependencyIndexInitializationFailed: return "dependency-index-initialization-failed";
        case Result::BuildGraphInitializationFailed: return "build-graph-initialization-failed";
        case Result::SourceNotFound: return "source-not-found";
        case Result::ImporterUnavailable: return "importer-unavailable";
        case Result::SourceDataUnavailable: return "source-data-unavailable";
        case Result::BuildRequestCreationFailed: return "build-request-creation-failed";
        case Result::BuildRequestRejected: return "build-request-rejected";
        case Result::Busy: return "busy";
        case Result::IndexSaveFailed: return "index-save-failed";
        }
        return "unknown";
    }

    [[nodiscard]] const char* BuildFailureToken(const assets::BuildFailure result) noexcept
    {
        using Result = assets::BuildFailure;
        switch (result)
        {
        case Result::None: return "none";
        case Result::InvalidRequest: return "invalid-request";
        case Result::ResolutionFailed: return "resolution-failed";
        case Result::DependencyCycle: return "dependency-cycle";
        case Result::DependencyFailed: return "dependency-failed";
        case Result::Cancelled: return "cancelled";
        case Result::BuildFailed: return "build-failed";
        case Result::OutOfMemory: return "out-of-memory";
        case Result::LimitExceeded: return "limit-exceeded";
        case Result::SchedulingFailed: return "scheduling-failed";
        case Result::IndexPublicationFailed: return "index-publication-failed";
        }
        return "unknown";
    }

    [[nodiscard]] const char* IndexResultToken(const assets::IndexResult result) noexcept
    {
        using Result = assets::IndexResult;
        switch (result)
        {
        case Result::Success: return "success";
        case Result::Recovered: return "recovered";
        case Result::NotFound: return "not-found";
        case Result::InvalidArgument: return "invalid-argument";
        case Result::InvalidState: return "invalid-state";
        case Result::Incompatible: return "incompatible";
        case Result::Corrupt: return "corrupt";
        case Result::IoFailure: return "io-failure";
        case Result::OutOfMemory: return "out-of-memory";
        case Result::LimitExceeded: return "limit-exceeded";
        }
        return "unknown";
    }

    void AppendAutomaticDirtyReason(containers::String& output,
                                    const asset_streaming::ProjectAssetAutomaticRecookDirtyReason value,
                                    const asset_streaming::ProjectAssetAutomaticRecookDirtyReason reason,
                                    const char* const token) noexcept
    {
        if (!asset_streaming::HasAutomaticRecookDirtyReason(value, reason))
            return;
        if (!output.Empty())
            output.Append(',');
        output.Append(containers::StringView(token));
    }

    [[nodiscard]] containers::String AutomaticDirtyReasonsToken(
        const asset_streaming::ProjectAssetAutomaticRecookDirtyReason value) noexcept
    {
        using Reason = asset_streaming::ProjectAssetAutomaticRecookDirtyReason;
        containers::String result;
        AppendAutomaticDirtyReason(result, value, Reason::AcceptedChange, "accepted-change");
        AppendAutomaticDirtyReason(result, value, Reason::StartupRecovery, "startup-recovery");
        AppendAutomaticDirtyReason(result, value, Reason::ExplicitRetry, "explicit-retry");
        AppendAutomaticDirtyReason(result, value, Reason::StaleCompletion, "stale-completion");
        AppendAutomaticDirtyReason(result, value, Reason::FailedBatch, "failed-batch");
        AppendAutomaticDirtyReason(result, value, Reason::CancelledBatch, "cancelled-batch");
        AppendAutomaticDirtyReason(result, value, Reason::OverflowRecovery, "overflow-recovery");
        AppendAutomaticDirtyReason(result, value, Reason::DeliveryOverflow, "delivery-overflow");
        AppendAutomaticDirtyReason(result, value, Reason::CacheRepairRequired, "cache-repair-required");
        AppendAutomaticDirtyReason(result, value, Reason::ExplicitReimport, "explicit-reimport");
        if (result.Empty())
            result.Append(containers::StringView("none"));
        return result;
    }

    [[nodiscard]] bool AssetIdHex(const assets::AssetId& id, char (&text)[33]) noexcept
    {
        if (!id.IsValid())
            return false;
        constexpr char hexadecimal[] = "0123456789abcdef";
        for (u32 index = 0; index < 16u; ++index)
        {
            text[index * 2u] = hexadecimal[id.bytes[index] >> 4u];
            text[index * 2u + 1u] = hexadecimal[id.bytes[index] & 0x0fu];
        }
        text[32] = '\0';
        return true;
    }

    [[nodiscard]] bool ResolveProjectPath(const char* const argument, ResolvedAssetProject& result) noexcept
    {
        containers::String absolute;
        if (argument == nullptr || !platform::MakeAbsolutePath(argument, absolute))
            return false;
        const platform::PathKind kind = platform::GetPathKind(absolute);
        if (kind == platform::PathKind::Directory)
        {
            result.root = static_cast<containers::String&&>(absolute);
            return platform::FindProjectFile(result.root, result.file);
        }
        if (kind != platform::PathKind::File || !Parent(absolute, result.root))
            return false;
        result.file = static_cast<containers::String&&>(absolute);
        return true;
    }

    [[nodiscard]] const char* ResolveAssetProject(const nano::Invocation& invocation,
                                                  ResolvedAssetProject& result,
                                                  projects::Diagnostic& diagnostic) noexcept
    {
        if (!ResolveProjectPath(invocation.Positional(0), result))
            return "project-not-found";
        containers::String contents;
        if (!platform::ReadFile(result.file, contents))
            return "project-read-failed";
        if (projects::Parse(contents, result.descriptor, &diagnostic) != projects::Result::Success ||
            projects::Validate(result.descriptor, &diagnostic) != projects::Result::Success)
            return ProjectResultToken(diagnostic.result);
        if (projects::ResolveWorkspace(filesystem::AbsolutePath::CreateFilePath(result.file), result.descriptor,
                                       result.workspace, &diagnostic) != projects::Result::Success)
            return ProjectResultToken(diagnostic.result);

        const filesystem::AbsolutePath* const requiredRoots[]{
            &result.workspace.root, &result.workspace.assets, &result.workspace.derivedData,
            &result.workspace.intermediate, &result.workspace.saved, &result.workspace.builds,
            &result.workspace.config, &result.workspace.plugins};
        for (const filesystem::AbsolutePath* const required : requiredRoots)
            if (platform::GetPathKind(required->AsStringView()) != platform::PathKind::Directory)
                return "invalid-workspace-layout";

        const char* const explicitTarget = invocation.Option("target");
        if (explicitTarget == nullptr)
        {
            if (result.descriptor.targets.Size() != 1u)
                return result.descriptor.targets.Empty() ? "missing-project-target" : "ambiguous-project-target";
            result.target = result.descriptor.targets[0];
        }
        else
        {
            u32 matches = 0u;
            for (const containers::String& declared : result.descriptor.targets)
                if (containers::StringView(declared.AsChar(), declared.Length()) == containers::StringView(explicitTarget))
                    ++matches;
            if (matches != 1u)
                return "undeclared-project-target";
            result.target = explicitTarget;
        }
        if (containers::StringView(result.target.AsChar(), result.target.Length()) != containers::StringView("windows-x64"))
            return "unsupported-project-target";
        return nullptr;
    }

    [[nodiscard]] nano::ExitCode CloseAndFinish(AssetCommandEnvironment& environment, nano::Output& output,
                                                nano::OutputFormat format, const char* operation,
                                                nano::SemanticEventKind terminal, nano::ExitCode code,
                                                const char* reason, const char* message = nullptr) noexcept;

    [[nodiscard]] nano::ExitCode OpenEnvironment(const nano::Invocation& invocation, nano::Output& output,
                                                 const char* const operation, AssetCommandEnvironment& environment,
                                                 bool& opened, const bool allowCommittedStartupPending = false) noexcept
    {
        opened = false;
        projects::Diagnostic diagnostic;
        const char* const projectFailure = ResolveAssetProject(invocation, environment.project, diagnostic);
        if (projectFailure != nullptr)
            return Finish(output, invocation.Format(), operation, nano::SemanticEventKind::Failed,
                          nano::ExitCode::InvalidProject, projectFailure,
                          diagnostic.message != nullptr ? diagnostic.message : nullptr);

        nano::ProjectAssetContextFailure contextFailure;
        const nano::ProjectAssetContextResult contextResult = environment.context.Open(
            environment.project.workspace, environment.project.target, &contextFailure);
        if (contextResult != nano::ProjectAssetContextResult::Success)
        {
            const nano::ExitCode code = contextResult == nano::ProjectAssetContextResult::SourceScanFailed ||
                                                contextResult == nano::ProjectAssetContextResult::PipelineInitializationFailed
                                            ? nano::ExitCode::OperationFailed
                                            : nano::ExitCode::InternalFailure;
            const char* reason = ContextResultToken(contextResult);
            if (contextFailure.pipeline != asset_streaming::ProjectAssetPipelineResult::Success)
                reason = PipelineResultToken(contextFailure.pipeline);
            return Finish(output, invocation.Format(), operation, nano::SemanticEventKind::Failed, code, reason);
        }
        environment.pipeline = environment.context.GetPipeline();
        if (environment.pipeline == nullptr)
            return Finish(output, invocation.Format(), operation, nano::SemanticEventKind::Failed,
                          nano::ExitCode::InternalFailure, "missing-project-asset-pipeline");
        const asset_streaming::ProjectAssetPipelineResult startupResult = environment.context.GetStartupPipelineResult();
        const bool committedStartupPending = startupResult == asset_streaming::ProjectAssetPipelineResult::CommittedCleanupPending ||
                                             startupResult == asset_streaming::ProjectAssetPipelineResult::CommittedReconciliationPending;
        if (committedStartupPending && !allowCommittedStartupPending)
            return CloseAndFinish(environment, output, invocation.Format(), operation, nano::SemanticEventKind::Failed,
                                  nano::ExitCode::OperationFailed, PipelineResultToken(startupResult));
        opened = true;
        return nano::ExitCode::Success;
    }

    [[nodiscard]] nano::ExitCode CloseAndFinish(AssetCommandEnvironment& environment, nano::Output& output,
                                                const nano::OutputFormat format, const char* const operation,
                                                const nano::SemanticEventKind terminal, const nano::ExitCode code,
                                                const char* const reason, const char* const message) noexcept
    {
        const nano::ProjectAssetContextResult closeResult = environment.context.Close();
        environment.pipeline = nullptr;
        if (closeResult != nano::ProjectAssetContextResult::Success)
            return Finish(output, format, operation, nano::SemanticEventKind::Failed,
                          nano::ExitCode::InternalFailure, "unsafe-shutdown-refused");
        return Finish(output, format, operation, terminal, code, reason, message);
    }

    [[nodiscard]] const char* CanonicalSource(const char* const source, containers::String& canonical) noexcept
    {
        if (source == nullptr || source[0] == '\0')
            return "missing-source";
        char buffer[resources::MaximumResourcePathBytes + 1u]{};
        usize written = 0u;
        const resources::Result result = resources::CanonicalizePath(containers::StringView(source), buffer,
                                                                     resources::MaximumResourcePathBytes, written);
        if (result != resources::Result::Success || written == 0u || written > resources::MaximumResourcePathBytes)
            return "invalid-source-path";
        buffer[written] = '\0';
        Assign(canonical, containers::StringView(buffer, static_cast<u32>(written)));
        return nullptr;
    }

    [[nodiscard]] const char* SelectAsset(asset_streaming::ProjectAssetPipeline& pipeline,
                                          const projects::ProjectWorkspace& workspace, const char* const source,
                                          const char* const outputKey, const bool requireOneOutput,
                                          SelectedAsset& selected) noexcept
    {
        const char* const canonicalFailure = CanonicalSource(source, selected.canonicalSource);
        if (canonicalFailure != nullptr)
            return canonicalFailure;
        const resources::ResourcePath identity = resources::ResourcePath::FromString(selected.canonicalSource);
        const asset_streaming::ProjectAssetState* match = nullptr;
        u32 matches = 0u;
        for (const asset_streaming::ProjectAssetState& state : pipeline.GetAcceptedAssets())
        {
            if (state.sourceRoot != workspace.assets || state.sourceIdentity != identity)
                continue;
            match = &state;
            ++matches;
        }
        if (matches == 0u || match == nullptr)
            return "source-not-accepted";
        if (matches != 1u)
            return "source-ambiguous";
        if (match->presence != asset_streaming::ProjectAssetPresence::Present)
            return PresenceToken(match->presence);
        if (match->sourceIssues != assets::SourceIssueNone)
            return "source-has-issues";
        if (match->importState != assets::SourceImportState::MetadataSelected ||
            match->sourceType == resources::InvalidResourceTypeId)
            return ImportStateToken(match->importState);
        if (match->outputs.Empty())
            return "source-has-no-current-outputs";
        for (const asset_streaming::ProjectAssetOutputState& output : match->outputs)
            if (!output.resource.IsValid() || !output.resource.IsTyped() || output.key.Empty())
                return "invalid-current-output";

        selected.state = match;
        if (outputKey != nullptr)
        {
            u32 outputMatches = 0u;
            for (const asset_streaming::ProjectAssetOutputState& output : match->outputs)
                if (containers::StringView(output.key.AsChar(), output.key.Length()) == containers::StringView(outputKey))
                {
                    selected.output = &output;
                    ++outputMatches;
                }
            if (outputMatches == 0u)
                return "output-key-not-found";
            if (outputMatches != 1u)
                return "output-key-ambiguous";
        }
        else if (requireOneOutput)
        {
            if (match->outputs.Count() != 1u)
                return "output-key-required";
            selected.output = &match->outputs[0];
        }
        return nullptr;
    }

    [[nodiscard]] const char* ResolveMutationDestination(const projects::ProjectWorkspace& workspace,
                                                         const char* const destination,
                                                         containers::String& canonical,
                                                         filesystem::AbsolutePath& target) noexcept
    {
        const char* const canonicalFailure = CanonicalSource(destination, canonical);
        if (canonicalFailure != nullptr)
            return "invalid-destination-path";
        target = workspace.assets.AddFilePath(canonical);
        return target.IsFilePath() ? nullptr : "invalid-destination-path";
    }

    [[nodiscard]] const char* ResolveReplacementMappings(
        const nano::Invocation& invocation, asset_streaming::ProjectAssetPipeline& pipeline,
        const projects::ProjectWorkspace& workspace, const char* const previousSource,
        assets::AssetReferenceReplacement* const mappings, const u32 capacity,
        u32& mappingCount) noexcept
    {
        mappingCount = 0u;
        const u32 previousCount = invocation.OptionCount("output");
        const u32 sourceCount = invocation.OptionCount("replacement-source");
        const u32 outputCount = invocation.OptionCount("replacement-output");
        if (previousCount != sourceCount || previousCount != outputCount)
            return "replacement-mapping-count-mismatch";
        if (previousCount == 0u)
            return "replacement-mapping-required";
        if (mappings == nullptr || previousCount > capacity)
            return "replacement-mapping-limit-exceeded";

        for (u32 index = 0u; index < previousCount; ++index)
        {
            SelectedAsset previous;
            const char* const previousFailure =
                SelectAsset(pipeline, workspace, previousSource, invocation.Option("output", index), false, previous);
            if (previousFailure != nullptr || previous.output == nullptr)
                return previousFailure != nullptr ? previousFailure : "output-key-not-found";

            SelectedAsset replacement;
            const char* const replacementFailure =
                SelectAsset(pipeline, workspace, invocation.Option("replacement-source", index),
                            invocation.Option("replacement-output", index), false, replacement);
            if (replacementFailure != nullptr || replacement.output == nullptr)
                return replacementFailure != nullptr ? replacementFailure : "replacement-output-not-found";
            if (previous.output->resource.ExpectedType() != replacement.output->resource.ExpectedType())
                return "replacement-type-mismatch";
            if (previous.output->resource == replacement.output->resource)
                return "replacement-matches-source-output";
            mappings[mappingCount++] = {previous.output->resource, replacement.output->resource};
        }
        return nullptr;
    }

    [[nodiscard]] bool EmitProjectStatus(nano::Output& output, const nano::OutputFormat format,
                                         const char* const operation, const AssetCommandEnvironment& environment) noexcept
    {
        const containers::ArraySpan<const asset_streaming::ProjectAssetState> assetsView = environment.pipeline->GetAcceptedAssets();
        const containers::ArraySpan<const asset_streaming::ProjectAssetSourceDiagnostic> diagnostics = environment.pipeline->GetAssetSourceDiagnostics();
        const asset_streaming::ProjectAssetPipelineResult startupResult = environment.context.GetStartupPipelineResult();
        const asset_streaming::ProjectAssetRestoreReport* const startupRestore = environment.context.GetStartupRestoreReport();
        const nano::SemanticField projectFields[]{
            StringField("scope", "project"),
            StringField("target", environment.project.target.AsChar()),
            StringField("startup-result", PipelineResultToken(startupResult)),
            UnsignedField("accepted-generation", environment.pipeline->GetAcceptedAssetGeneration()),
            UnsignedField("accepted-assets", assetsView.Count()),
            UnsignedField("source-diagnostics", diagnostics.Count()),
            BooleanField("diagnostics-truncated", diagnostics.Count() > MaximumStatusDiagnostics),
            BooleanField("operation-cleanup-pending", startupRestore != nullptr && startupRestore->operationCleanupPending),
            UnsignedField("startup-restored", startupRestore != nullptr ? startupRestore->restored : 0u),
            UnsignedField("startup-removed-publications", startupRestore != nullptr ? startupRestore->removedPublications : 0u),
            UnsignedField("startup-stale-files-deferred", startupRestore != nullptr ? startupRestore->staleFilesDeferred : 0u)};
        if (!Emit(output, format, nano::SemanticEventKind::Status, operation, projectFields))
            return false;

        const u32 diagnosticCount = diagnostics.Count() < MaximumStatusDiagnostics ? diagnostics.Count() : MaximumStatusDiagnostics;
        for (u32 index = 0u; index < diagnosticCount; ++index)
        {
            const asset_streaming::ProjectAssetSourceDiagnostic& diagnostic = diagnostics[index];
            containers::String sourceIssues = SourceIssuesToken(diagnostic.sourceIssues);
            char assetId[33]{};
            const bool hasAssetId = diagnostic.hasParsedAssetIdentity && AssetIdHex(diagnostic.asset, assetId);
            const nano::SemanticField diagnosticFields[]{
                StringField("scope", "source-diagnostic"),
                StringField("physical-path", diagnostic.physicalPath.AsChar()),
                StringField("issues", sourceIssues.AsChar()),
                StringField("import-state", ImportStateToken(diagnostic.importState)),
                StringField("reason", SourceDiagnosticReasonToken(diagnostic.reasons)),
                BooleanField("has-parsed-asset-id", hasAssetId),
                StringField("asset-id", hasAssetId ? assetId : "")};
            if (!Emit(output, format, nano::SemanticEventKind::Diagnostic, operation, diagnosticFields))
                return false;
        }

        const asset_streaming::ProjectAssetAutomaticRecookStatus automatic = environment.pipeline->GetAutomaticRecookStatus();
        containers::String dirtyReasons = AutomaticDirtyReasonsToken(automatic.dirtyReasons);
        const nano::SemanticField automaticFields[]{
            StringField("scope", "automatic-recook"), StringField("state", AutomaticStateToken(automatic.state)),
            StringField("dirty-reasons", dirtyReasons.AsChar()), UnsignedField("pending-assets", automatic.pendingAssets),
            UnsignedField("pending-inputs", automatic.pendingInputs), UnsignedField("pending-outputs", automatic.pendingOutputs),
            UnsignedField("pending-reimport-outputs", automatic.pendingReimportOutputs),
            UnsignedField("pending-delivery-outputs", automatic.pendingDeliveryOutputs),
            BooleanField("retry-requested", automatic.retryRequested), BooleanField("cancellation-requested", automatic.cancellationRequested),
            BooleanField("recovery-required", automatic.recoveryRequired)};
        if (!Emit(output, format, nano::SemanticEventKind::Status, operation, automaticFields))
            return false;

        const asset_streaming::ProjectAssetObservationStatus observation = environment.pipeline->GetObservationStatus();
        const nano::SemanticField observationFields[]{
            StringField("scope", "observation"), BooleanField("running", observation.running), BooleanField("overflow", observation.overflow),
            BooleanField("rescan-required", observation.rescanRequired), BooleanField("reconcile-pending", observation.reconcilePending),
            UnsignedField("watched-roots", observation.watchedRoots), UnsignedField("pending-paths", observation.pendingPaths),
            UnsignedField("deferred-paths", observation.deferredPaths), UnsignedField("ready-paths", observation.readyPaths),
            UnsignedField("ticks", observation.ticks), UnsignedField("hints-received", observation.hintsReceived),
            UnsignedField("hints-coalesced", observation.hintsCoalesced), UnsignedField("hints-suppressed", observation.hintsSuppressed),
            UnsignedField("hints-rejected", observation.hintsRejected), UnsignedField("rescans-requested", observation.rescansRequested),
            UnsignedField("rescans-completed", observation.rescansCompleted)};
        if (!Emit(output, format, nano::SemanticEventKind::Status, operation, observationFields))
            return false;

        const asset_streaming::ProjectAssetVddcMaintenanceStatus vddc = environment.pipeline->GetVddcMaintenanceStatus();
        const nano::SemanticField vddcFields[]{
            StringField("scope", "vddc-maintenance"), StringField("state", VddcMaintenanceStateToken(vddc.state)),
            StringField("result", VddcMaintenanceResultToken(vddc.lastResult)), UnsignedField("scanned-records", vddc.scannedRecords),
            UnsignedField("protected-records", vddc.protectedRecords), UnsignedField("eligible-records", vddc.eligibleRecords),
            UnsignedField("removed-records", vddc.removedRecords), UnsignedField("deferred-deletes", vddc.deferredDeletes),
            UnsignedField("failed-deletes", vddc.failedDeletes), BooleanField("retry-required", vddc.retryRequired),
            BooleanField("cancellation-requested", vddc.cancellationRequested), BooleanField("limit-exceeded", vddc.limitExceeded),
            BooleanField("status-rows-truncated", vddc.statusRowsTruncated)};
        if (!Emit(output, format, nano::SemanticEventKind::Status, operation, vddcFields))
            return false;

        const asset_streaming::ProjectAssetLooseMaintenanceStatus loose = environment.pipeline->GetLooseMaintenanceStatus();
        const nano::SemanticField looseFields[]{
            StringField("scope", "loose-maintenance"), StringField("state", LooseMaintenanceStateToken(loose.state)),
            StringField("result", LooseMaintenanceResultToken(loose.lastResult)), UnsignedField("tracked-candidates", loose.trackedCandidates),
            UnsignedField("scanned-records", loose.scannedRecords), UnsignedField("removed-records", loose.removedRecords),
            UnsignedField("deferred-deletes", loose.deferredDeletes), UnsignedField("failed-deletes", loose.failedDeletes),
            BooleanField("retry-required", loose.retryRequired), BooleanField("cancellation-requested", loose.cancellationRequested),
            BooleanField("limit-exceeded", loose.limitExceeded), BooleanField("status-rows-truncated", loose.statusRowsTruncated)};
        return Emit(output, format, nano::SemanticEventKind::Status, operation, looseFields);
    }

    [[nodiscard]] bool EmitSelectedAssetStatus(nano::Output& output, const nano::OutputFormat format,
                                               const char* const operation, asset_streaming::ProjectAssetPipeline& pipeline,
                                               const SelectedAsset& selected, const char*& queryFailureReason) noexcept
    {
        queryFailureReason = nullptr;
        char assetId[33]{};
        if (!AssetIdHex(selected.state->asset, assetId))
            return false;
        const nano::SemanticField sourceFields[]{
            StringField("scope", "source"), StringField("source", selected.canonicalSource.AsChar()),
            StringField("asset-id", assetId), StringField("presence", PresenceToken(selected.state->presence)),
            StringField("import-state", ImportStateToken(selected.state->importState)),
            UnsignedField("accepted-generation", selected.state->acceptedGeneration),
            UnsignedField("output-count", selected.state->outputs.Count())};
        if (!Emit(output, format, nano::SemanticEventKind::Status, operation, sourceFields))
            return false;

        for (const asset_streaming::ProjectAssetOutputState& outputState : selected.state->outputs)
        {
            if (selected.output != nullptr && selected.output != &outputState)
                continue;
            asset_streaming::ProjectAssetBuildFreshness freshness;
            asset_streaming::ProjectAssetPipelineFailure freshnessFailure;
            const asset_streaming::ProjectAssetPipelineResult freshnessResult =
                pipeline.QueryBuildFreshness(outputState.resource, freshness, &freshnessFailure);
            if (freshnessResult != asset_streaming::ProjectAssetPipelineResult::Success)
            {
                queryFailureReason = PipelineResultToken(freshnessResult);
                const nano::SemanticField fields[]{
                    StringField("scope", "output"), StringField("key", outputState.key.AsChar()), StringField("query", "freshness"),
                    StringField("reason", queryFailureReason), StringField("build-result", BuildResultToken(freshnessFailure.build)),
                    StringField("build-failure", BuildFailureToken(freshnessFailure.buildFailure)),
                    StringField("index-result", IndexResultToken(freshnessFailure.index))};
                if (!Emit(output, format, nano::SemanticEventKind::Diagnostic, operation, fields))
                    return false;
                return true;
            }

            asset_streaming::ProjectAssetOutputDeliveryStatus delivery;
            const bool deliveryTracked = pipeline.GetOutputDeliveryStatus(outputState.resource, delivery);
            if (!deliveryTracked)
            {
                const nano::SemanticField fields[]{StringField("scope", "output"), StringField("key", outputState.key.AsChar()),
                                                   StringField("freshness", DirtyReasonToken(freshness.reason)),
                                                   BooleanField("delivery-tracked", false)};
                if (!Emit(output, format, nano::SemanticEventKind::Status, operation, fields))
                    return false;
                continue;
            }
            const nano::SemanticField fields[]{
                StringField("scope", "output"), StringField("key", outputState.key.AsChar()),
                StringField("freshness", DirtyReasonToken(freshness.reason)), BooleanField("delivery-tracked", true),
                StringField("build-state", DeliveryStageToken(delivery.buildState)),
                StringField("publication-state", DeliveryStageToken(delivery.publicationState)),
                StringField("publication-reason", DeliveryReasonToken(delivery.publicationReason)),
                StringField("resident-state", DeliveryStageToken(delivery.residentState)),
                StringField("resident-reason", DeliveryReasonToken(delivery.residentReason)),
                BooleanField("publication-retry-required", delivery.publicationRetryRequired),
                BooleanField("resident-retry-required", delivery.residentRetryRequired)};
            if (!Emit(output, format, nano::SemanticEventKind::Status, operation, fields))
                return false;
        }
        return true;
    }

    nano::ExitCode AssetStatus(const nano::Invocation& invocation, nano::Output& output) noexcept
    {
        constexpr char operation[] = "asset-status";
        AssetCommandEnvironment environment;
        bool opened = false;
        const nano::ExitCode openResult = OpenEnvironment(invocation, output, operation, environment, opened, true);
        if (!opened)
            return openResult;

        if (!EmitProjectStatus(output, invocation.Format(), operation, environment))
            return CloseAndFinish(environment, output, invocation.Format(), operation, nano::SemanticEventKind::Failed,
                                  nano::ExitCode::InternalFailure, "protocol-output-failed");

        const asset_streaming::ProjectAssetPipelineResult startupResult = environment.context.GetStartupPipelineResult();
        if (startupResult == asset_streaming::ProjectAssetPipelineResult::CommittedCleanupPending ||
            startupResult == asset_streaming::ProjectAssetPipelineResult::CommittedReconciliationPending)
            return CloseAndFinish(environment, output, invocation.Format(), operation, nano::SemanticEventKind::Failed,
                                  nano::ExitCode::OperationFailed, PipelineResultToken(startupResult));

        const char* const source = invocation.PositionalCount() > 1u ? invocation.Positional(1u) : nullptr;
        const char* const outputKey = invocation.Option("output");
        if (source == nullptr && outputKey != nullptr)
            return CloseAndFinish(environment, output, invocation.Format(), operation, nano::SemanticEventKind::Failed,
                                  nano::ExitCode::OperationFailed, "output-requires-source");
        if (source != nullptr)
        {
            SelectedAsset selected;
            const char* const selectionFailure = SelectAsset(*environment.pipeline, environment.project.workspace,
                                                             source, outputKey, false, selected);
            if (selectionFailure != nullptr)
                return CloseAndFinish(environment, output, invocation.Format(), operation, nano::SemanticEventKind::Failed,
                                      nano::ExitCode::OperationFailed, selectionFailure);
            const char* queryFailureReason = nullptr;
            if (!EmitSelectedAssetStatus(output, invocation.Format(), operation, *environment.pipeline, selected, queryFailureReason))
                return CloseAndFinish(environment, output, invocation.Format(), operation, nano::SemanticEventKind::Failed,
                                      nano::ExitCode::InternalFailure, "protocol-output-failed");
            if (queryFailureReason != nullptr)
                return CloseAndFinish(environment, output, invocation.Format(), operation, nano::SemanticEventKind::Failed,
                                      nano::ExitCode::OperationFailed, queryFailureReason);
        }
        return CloseAndFinish(environment, output, invocation.Format(), operation, nano::SemanticEventKind::Completed,
                              nano::ExitCode::Success, "success");
    }

    nano::ExitCode AssetRescan(const nano::Invocation& invocation, nano::Output& output) noexcept
    {
        constexpr char operation[] = "asset-rescan";
        AssetCommandEnvironment environment;
        bool opened = false;
        const nano::ExitCode openResult = OpenEnvironment(invocation, output, operation, environment, opened);
        if (!opened)
            return openResult;
        const assets::SourceRoot root{environment.project.workspace.assets, false};
        asset_streaming::ProjectAssetRestoreReport report;
        filesystem::ScanResult scanFailure = filesystem::ScanResult::Success;
        asset_streaming::ProjectAssetPipelineFailure failure;
        const asset_streaming::ProjectAssetPipelineResult result =
            environment.pipeline->Rescan({&root, 1u}, &report, &scanFailure, &failure);
        if (result != asset_streaming::ProjectAssetPipelineResult::Success)
            return CloseAndFinish(environment, output, invocation.Format(), operation, nano::SemanticEventKind::Failed,
                                  nano::ExitCode::OperationFailed, PipelineResultToken(result));
        const nano::SemanticField fields[]{StringField("scope", "rescan"),
                                           UnsignedField("accepted-generation", environment.pipeline->GetAcceptedAssetGeneration()),
                                           UnsignedField("accepted-assets", environment.pipeline->GetAcceptedAssets().Count()),
                                           UnsignedField("catalog-outputs", report.catalogOutputs),
                                           UnsignedField("restored", report.restored),
                                           UnsignedField("removed-publications", report.removedPublications)};
        if (!Emit(output, invocation.Format(), nano::SemanticEventKind::Status, operation, fields))
            return CloseAndFinish(environment, output, invocation.Format(), operation, nano::SemanticEventKind::Failed,
                                  nano::ExitCode::InternalFailure, "protocol-output-failed");
        return CloseAndFinish(environment, output, invocation.Format(), operation, nano::SemanticEventKind::Completed,
                              nano::ExitCode::Success, "success");
    }

    nano::ExitCode AssetBuild(const nano::Invocation& invocation, nano::Output& output) noexcept
    {
        constexpr char operation[] = "asset-build";
        AssetCommandEnvironment environment;
        bool opened = false;
        const nano::ExitCode openResult = OpenEnvironment(invocation, output, operation, environment, opened);
        if (!opened)
            return openResult;
        SelectedAsset selected;
        const char* const selectionFailure = SelectAsset(*environment.pipeline, environment.project.workspace,
                                                         invocation.Positional(1u), invocation.Option("output"), true, selected);
        if (selectionFailure != nullptr)
            return CloseAndFinish(environment, output, invocation.Format(), operation, nano::SemanticEventKind::Failed,
                                  nano::ExitCode::OperationFailed, selectionFailure);

        asset_streaming::ProjectAssetPipelineFailure failure;
        asset_streaming::ProjectAssetRequest request = environment.pipeline->Request(selected.output->resource, assets::BuildPriority::Normal, &failure);
        if (!request.IsValid())
            return CloseAndFinish(environment, output, invocation.Format(), operation, nano::SemanticEventKind::Failed,
                                  nano::ExitCode::OperationFailed, PipelineResultToken(failure.result));

        bool cancelIssued = false;
        assets::BuildState reportedState = request.GetBuildState();
        const nano::SemanticField initial[]{StringField("state", BuildStateToken(reportedState)),
                                            StringField("output", selected.output->key.AsChar())};
        if (!Emit(output, invocation.Format(), nano::SemanticEventKind::Progress, operation, initial))
        {
            request.Reset();
            return CloseAndFinish(environment, output, invocation.Format(), operation, nano::SemanticEventKind::Failed,
                                  nano::ExitCode::InternalFailure, "protocol-output-failed");
        }

        asset_streaming::ProjectAssetPipelineResult pollResult = asset_streaming::ProjectAssetPipelineResult::Pending;
        while (pollResult == asset_streaming::ProjectAssetPipelineResult::Pending)
        {
            if (!cancelIssued && nano::IsCommandCancellationRequested())
            {
                cancelIssued = request.Cancel();
                if (!cancelIssued && request.HasFinished())
                    cancelIssued = false;
            }
            pollResult = request.Poll(&failure);
            const assets::BuildState state = request.GetBuildState();
            if (state != reportedState)
            {
                reportedState = state;
                const nano::SemanticField fields[]{StringField("state", BuildStateToken(state)),
                                                   StringField("output", selected.output->key.AsChar())};
                if (!Emit(output, invocation.Format(), nano::SemanticEventKind::Progress, operation, fields))
                {
                    request.Reset();
                    return CloseAndFinish(environment, output, invocation.Format(), operation, nano::SemanticEventKind::Failed,
                                          nano::ExitCode::InternalFailure, "protocol-output-failed");
                }
            }
            if (pollResult == asset_streaming::ProjectAssetPipelineResult::Pending)
                concurrency::YieldCurrentThread();
        }
        const assets::BuildState finalState = request.GetBuildState();
        const bool cancelled = finalState == assets::BuildState::Cancelled || failure.buildFailure == assets::BuildFailure::Cancelled;
        request.Reset();
        if (cancelled)
            return CloseAndFinish(environment, output, invocation.Format(), operation, nano::SemanticEventKind::Cancelled,
                                  nano::ExitCode::Cancelled, "cancelled");
        if (pollResult != asset_streaming::ProjectAssetPipelineResult::Success)
            return CloseAndFinish(environment, output, invocation.Format(), operation, nano::SemanticEventKind::Failed,
                                  nano::ExitCode::OperationFailed, PipelineResultToken(pollResult));
        return CloseAndFinish(environment, output, invocation.Format(), operation, nano::SemanticEventKind::Completed,
                              nano::ExitCode::Success, "success");
    }

    nano::ExitCode AssetReimport(const nano::Invocation& invocation, nano::Output& output) noexcept
    {
        constexpr char operation[] = "asset-reimport";
        AssetCommandEnvironment environment;
        bool opened = false;
        const nano::ExitCode openResult = OpenEnvironment(invocation, output, operation, environment, opened);
        if (!opened)
            return openResult;
        SelectedAsset selected;
        const char* const selectionFailure = SelectAsset(*environment.pipeline, environment.project.workspace,
                                                         invocation.Positional(1u), nullptr, false, selected);
        if (selectionFailure != nullptr)
            return CloseAndFinish(environment, output, invocation.Format(), operation, nano::SemanticEventKind::Failed,
                                  nano::ExitCode::OperationFailed, selectionFailure);

        asset_streaming::ProjectAssetPipelineFailure failure;
        const asset_streaming::ProjectAssetPipelineResult requestResult =
            environment.pipeline->RequestAssetReimport(selected.state->asset, &failure);
        if (requestResult != asset_streaming::ProjectAssetPipelineResult::Success)
            return CloseAndFinish(environment, output, invocation.Format(), operation, nano::SemanticEventKind::Failed,
                                  nano::ExitCode::OperationFailed, PipelineResultToken(requestResult));

        using AutomaticState = asset_streaming::ProjectAssetAutomaticRecookState;
        bool cancelIssued = false;
        AutomaticState reported = environment.pipeline->GetAutomaticRecookStatus().state;
        for (;;)
        {
            asset_streaming::ProjectAssetAutomaticRecookStatus status = environment.pipeline->GetAutomaticRecookStatus();
            if (status.state != reported)
            {
                reported = status.state;
                const nano::SemanticField fields[]{StringField("state", AutomaticStateToken(status.state)),
                                                   UnsignedField("pending-outputs", status.pendingOutputs),
                                                   UnsignedField("pending-reimport-outputs", status.pendingReimportOutputs)};
                if (!Emit(output, invocation.Format(), nano::SemanticEventKind::Progress, operation, fields))
                    return CloseAndFinish(environment, output, invocation.Format(), operation, nano::SemanticEventKind::Failed,
                                          nano::ExitCode::InternalFailure, "protocol-output-failed");
            }

            if (status.state == AutomaticState::Disabled)
                return CloseAndFinish(environment, output, invocation.Format(), operation, nano::SemanticEventKind::Completed,
                                      nano::ExitCode::Success, "success");
            if (status.state == AutomaticState::Failed || status.state == AutomaticState::RecoveryRequired)
            {
                const bool cancelled = cancelIssued && asset_streaming::HasAutomaticRecookDirtyReason(
                    status.dirtyReasons, asset_streaming::ProjectAssetAutomaticRecookDirtyReason::CancelledBatch);
                return CloseAndFinish(environment, output, invocation.Format(), operation,
                                      cancelled ? nano::SemanticEventKind::Cancelled : nano::SemanticEventKind::Failed,
                                      cancelled ? nano::ExitCode::Cancelled : nano::ExitCode::OperationFailed,
                                      cancelled ? "cancelled" : AutomaticStateToken(status.state));
            }

            if (!cancelIssued && nano::IsCommandCancellationRequested())
            {
                const asset_streaming::ProjectAssetPipelineResult cancelResult = environment.pipeline->CancelAutomaticRecook(&failure);
                if (cancelResult == asset_streaming::ProjectAssetPipelineResult::Success)
                    cancelIssued = true;
                else if (cancelResult != asset_streaming::ProjectAssetPipelineResult::InvalidState)
                    return CloseAndFinish(environment, output, invocation.Format(), operation, nano::SemanticEventKind::Failed,
                                          nano::ExitCode::OperationFailed, PipelineResultToken(cancelResult));
            }

            const asset_streaming::ProjectAssetPipelineResult tick = environment.pipeline->TickAutomaticRecook(&failure);
            if (tick != asset_streaming::ProjectAssetPipelineResult::Success && tick != asset_streaming::ProjectAssetPipelineResult::Pending &&
                tick != asset_streaming::ProjectAssetPipelineResult::BuildRequestFailed)
                return CloseAndFinish(environment, output, invocation.Format(), operation, nano::SemanticEventKind::Failed,
                                      nano::ExitCode::OperationFailed, PipelineResultToken(tick));
            concurrency::YieldCurrentThread();
        }
    }

    struct MaintenanceRunResult final
    {
        nano::ExitCode code = nano::ExitCode::Success;
        const char* reason = "success";
    };

    [[nodiscard]] bool EmitVddcMaintenanceState(nano::Output& output, const nano::OutputFormat format,
                                                const char* const operation,
                                                const asset_streaming::ProjectAssetVddcMaintenanceStatus& status) noexcept
    {
        const nano::SemanticField fields[]{
            StringField("scope", "vddc-maintenance"),
            StringField("state", VddcMaintenanceStateToken(status.state)),
            StringField("result", VddcMaintenanceResultToken(status.lastResult)),
            UnsignedField("scanned-records", status.scannedRecords),
            UnsignedField("protected-records", status.protectedRecords),
            UnsignedField("eligible-records", status.eligibleRecords),
            UnsignedField("removed-records", status.removedRecords),
            UnsignedField("deferred-deletes", status.deferredDeletes),
            UnsignedField("failed-deletes", status.failedDeletes),
            BooleanField("budget-satisfied", status.budgetSatisfied),
            BooleanField("retry-required", status.retryRequired),
            BooleanField("cancellation-requested", status.cancellationRequested),
            BooleanField("limit-exceeded", status.limitExceeded),
            BooleanField("status-rows-truncated", status.statusRowsTruncated)};
        return Emit(output, format, nano::SemanticEventKind::Progress, operation, fields);
    }

    [[nodiscard]] bool EmitVddcMaintenanceDiagnostics(nano::Output& output, const nano::OutputFormat format,
                                                      const char* const operation,
                                                      asset_streaming::ProjectAssetPipeline& pipeline) noexcept
    {
        const auto diagnostics = pipeline.GetVddcMaintenanceDiagnostics();
        const u32 count = diagnostics.Count() < MaximumStatusDiagnostics ? diagnostics.Count() : MaximumStatusDiagnostics;
        for (u32 index = 0u; index < count; ++index)
        {
            char build[65]{};
            static_cast<void>(BuildFingerprintHex(diagnostics[index].build, build));
            const nano::SemanticField fields[]{StringField("scope", "vddc-maintenance"),
                                               StringField("reason", VddcMaintenanceDiagnosticReasonToken(diagnostics[index].reason)),
                                               StringField("build-fingerprint", build)};
            if (!Emit(output, format, nano::SemanticEventKind::Diagnostic, operation, fields))
                return false;
        }
        return true;
    }

    [[nodiscard]] bool EmitLooseMaintenanceState(nano::Output& output, const nano::OutputFormat format,
                                                 const char* const operation,
                                                 const asset_streaming::ProjectAssetLooseMaintenanceStatus& status) noexcept
    {
        const nano::SemanticField fields[]{
            StringField("scope", "loose-maintenance"),
            StringField("state", LooseMaintenanceStateToken(status.state)),
            StringField("result", LooseMaintenanceResultToken(status.lastResult)),
            UnsignedField("tracked-candidates", status.trackedCandidates),
            UnsignedField("scanned-records", status.scannedRecords),
            UnsignedField("rediscovered-records", status.rediscoveredRecords),
            UnsignedField("streamer-protected-records", status.streamerProtectedRecords),
            UnsignedField("resident-protected-records", status.residentProtectedRecords),
            UnsignedField("settlement-protected-records", status.settlementProtectedRecords),
            UnsignedField("removed-records", status.removedRecords),
            UnsignedField("deferred-deletes", status.deferredDeletes),
            UnsignedField("failed-deletes", status.failedDeletes),
            BooleanField("retry-required", status.retryRequired),
            BooleanField("cancellation-requested", status.cancellationRequested),
            BooleanField("limit-exceeded", status.limitExceeded),
            BooleanField("status-rows-truncated", status.statusRowsTruncated)};
        return Emit(output, format, nano::SemanticEventKind::Progress, operation, fields);
    }

    [[nodiscard]] bool EmitLooseMaintenanceDiagnostics(nano::Output& output, const nano::OutputFormat format,
                                                       const char* const operation,
                                                       asset_streaming::ProjectAssetPipeline& pipeline) noexcept
    {
        const auto diagnostics = pipeline.GetLooseMaintenanceDiagnostics();
        const u32 count = diagnostics.Count() < MaximumStatusDiagnostics ? diagnostics.Count() : MaximumStatusDiagnostics;
        for (u32 index = 0u; index < count; ++index)
        {
            const nano::SemanticField fields[]{
                StringField("scope", "loose-maintenance"),
                StringField("reason", LooseRetireResultToken(diagnostics[index].retireResult)),
                StringField("physical-path", diagnostics[index].physicalPath.AsChar()),
                BooleanField("rediscovered", diagnostics[index].rediscovered)};
            if (!Emit(output, format, nano::SemanticEventKind::Diagnostic, operation, fields))
                return false;
        }
        return true;
    }

    [[nodiscard]] MaintenanceRunResult ClassifyVddcMaintenance(
        const asset_streaming::ProjectAssetVddcMaintenanceStatus& status,
        const asset_streaming::ProjectAssetPipelineResult ownerResult) noexcept
    {
        using State = asset_streaming::ProjectAssetVddcMaintenanceState;
        if (status.state == State::Cancelled)
            return {nano::ExitCode::Cancelled, "cancelled"};
        if (status.state == State::Complete && !status.retryRequired)
            return {nano::ExitCode::Success, "success"};
        if (status.state == State::Deferred || status.state == State::Failed || status.retryRequired)
            return {nano::ExitCode::OperationFailed, VddcMaintenanceResultToken(status.lastResult)};
        if (ownerResult != asset_streaming::ProjectAssetPipelineResult::Success &&
            ownerResult != asset_streaming::ProjectAssetPipelineResult::Pending)
            return {nano::ExitCode::OperationFailed, PipelineResultToken(ownerResult)};
        return {nano::ExitCode::OperationFailed, VddcMaintenanceStateToken(status.state)};
    }

    [[nodiscard]] MaintenanceRunResult RunVddcMaintenance(AssetCommandEnvironment& environment,
                                                          nano::Output& output,
                                                          const nano::OutputFormat format,
                                                          const char* const operation) noexcept
    {
        asset_streaming::ProjectAssetPipelineFailure failure;
        asset_streaming::ProjectAssetPipelineResult ownerResult = environment.pipeline->RequestVddcMaintenance(&failure);
        asset_streaming::ProjectAssetVddcMaintenanceStatus status = environment.pipeline->GetVddcMaintenanceStatus();
        auto reported = status.state;
        if (!EmitVddcMaintenanceState(output, format, operation, status))
            return {nano::ExitCode::InternalFailure, "protocol-output-failed"};

        bool cancelIssued = false;
        while (ownerResult == asset_streaming::ProjectAssetPipelineResult::Pending)
        {
            if (!cancelIssued && nano::IsCommandCancellationRequested())
            {
                const asset_streaming::ProjectAssetPipelineResult cancelled =
                    environment.pipeline->CancelVddcMaintenance(&failure);
                if (cancelled == asset_streaming::ProjectAssetPipelineResult::Success)
                    cancelIssued = true;
                else if (cancelled != asset_streaming::ProjectAssetPipelineResult::InvalidState)
                    return {nano::ExitCode::OperationFailed, PipelineResultToken(cancelled)};
            }

            ownerResult = environment.pipeline->TickVddcMaintenance(&failure);
            status = environment.pipeline->GetVddcMaintenanceStatus();
            if (status.state != reported)
            {
                reported = status.state;
                if (!EmitVddcMaintenanceState(output, format, operation, status))
                    return {nano::ExitCode::InternalFailure, "protocol-output-failed"};
            }
            if (ownerResult == asset_streaming::ProjectAssetPipelineResult::Pending)
                concurrency::YieldCurrentThread();
        }

        status = environment.pipeline->GetVddcMaintenanceStatus();
        if (status.state != reported && !EmitVddcMaintenanceState(output, format, operation, status))
            return {nano::ExitCode::InternalFailure, "protocol-output-failed"};
        if (!EmitVddcMaintenanceDiagnostics(output, format, operation, *environment.pipeline))
            return {nano::ExitCode::InternalFailure, "protocol-output-failed"};
        return ClassifyVddcMaintenance(status, ownerResult);
    }

    [[nodiscard]] MaintenanceRunResult ClassifyLooseMaintenance(
        const asset_streaming::ProjectAssetLooseMaintenanceStatus& status,
        const asset_streaming::ProjectAssetPipelineResult ownerResult) noexcept
    {
        using State = asset_streaming::ProjectAssetLooseMaintenanceState;
        if (status.state == State::Cancelled)
            return {nano::ExitCode::Cancelled, "cancelled"};
        if (status.state == State::Complete && !status.retryRequired)
            return {nano::ExitCode::Success, "success"};
        if (status.state == State::Deferred || status.state == State::Failed || status.retryRequired)
            return {nano::ExitCode::OperationFailed, LooseMaintenanceResultToken(status.lastResult)};
        if (ownerResult != asset_streaming::ProjectAssetPipelineResult::Success &&
            ownerResult != asset_streaming::ProjectAssetPipelineResult::Pending)
            return {nano::ExitCode::OperationFailed, PipelineResultToken(ownerResult)};
        return {nano::ExitCode::OperationFailed, LooseMaintenanceStateToken(status.state)};
    }

    [[nodiscard]] MaintenanceRunResult RunLooseMaintenance(AssetCommandEnvironment& environment,
                                                           nano::Output& output,
                                                           const nano::OutputFormat format,
                                                           const char* const operation) noexcept
    {
        asset_streaming::ProjectAssetPipelineFailure failure;
        asset_streaming::ProjectAssetPipelineResult ownerResult = environment.pipeline->RequestLooseMaintenance(&failure);
        asset_streaming::ProjectAssetLooseMaintenanceStatus status = environment.pipeline->GetLooseMaintenanceStatus();
        auto reported = status.state;
        if (!EmitLooseMaintenanceState(output, format, operation, status))
            return {nano::ExitCode::InternalFailure, "protocol-output-failed"};

        bool cancelIssued = false;
        while (ownerResult == asset_streaming::ProjectAssetPipelineResult::Pending)
        {
            if (!cancelIssued && nano::IsCommandCancellationRequested())
            {
                const asset_streaming::ProjectAssetPipelineResult cancelled =
                    environment.pipeline->CancelLooseMaintenance(&failure);
                if (cancelled == asset_streaming::ProjectAssetPipelineResult::Success)
                    cancelIssued = true;
                else if (cancelled != asset_streaming::ProjectAssetPipelineResult::InvalidState)
                    return {nano::ExitCode::OperationFailed, PipelineResultToken(cancelled)};
            }

            ownerResult = environment.pipeline->TickLooseMaintenance(&failure);
            status = environment.pipeline->GetLooseMaintenanceStatus();
            if (status.state != reported)
            {
                reported = status.state;
                if (!EmitLooseMaintenanceState(output, format, operation, status))
                    return {nano::ExitCode::InternalFailure, "protocol-output-failed"};
            }
            if (ownerResult == asset_streaming::ProjectAssetPipelineResult::Pending)
                concurrency::YieldCurrentThread();
        }

        status = environment.pipeline->GetLooseMaintenanceStatus();
        if (status.state != reported && !EmitLooseMaintenanceState(output, format, operation, status))
            return {nano::ExitCode::InternalFailure, "protocol-output-failed"};
        if (!EmitLooseMaintenanceDiagnostics(output, format, operation, *environment.pipeline))
            return {nano::ExitCode::InternalFailure, "protocol-output-failed"};
        return ClassifyLooseMaintenance(status, ownerResult);
    }

    [[nodiscard]] nano::SemanticEventKind MaintenanceTerminalKind(const nano::ExitCode code) noexcept
    {
        if (code == nano::ExitCode::Success)
            return nano::SemanticEventKind::Completed;
        if (code == nano::ExitCode::Cancelled)
            return nano::SemanticEventKind::Cancelled;
        return nano::SemanticEventKind::Failed;
    }

    nano::ExitCode AssetMaintain(const nano::Invocation& invocation, nano::Output& output) noexcept
    {
        const char* const domain = invocation.Option("domain");
        constexpr char operation[] = "asset-maintain";
        enum class Domain : u8
        {
            Invalid,
            Vddc,
            Loose,
            All
        };
        Domain selected = Domain::Invalid;
        if (Equal(domain, "vddc"))
        {
            selected = Domain::Vddc;
        }
        else if (Equal(domain, "loose"))
        {
            selected = Domain::Loose;
        }
        else if (Equal(domain, "all"))
        {
            selected = Domain::All;
        }
        if (selected == Domain::Invalid)
            return Finish(output, invocation.Format(), operation, nano::SemanticEventKind::Failed,
                          nano::ExitCode::UsageError, domain == nullptr ? "maintenance-domain-required" : "invalid-maintenance-domain");

        AssetCommandEnvironment environment;
        bool opened = false;
        const nano::ExitCode openResult = OpenEnvironment(invocation, output, operation, environment, opened);
        if (!opened)
            return openResult;
        if (nano::IsCommandCancellationRequested())
            return CloseAndFinish(environment, output, invocation.Format(), operation,
                                  nano::SemanticEventKind::Cancelled, nano::ExitCode::Cancelled, "cancelled");

        MaintenanceRunResult result;
        if (selected == Domain::Vddc || selected == Domain::All)
        {
            result = RunVddcMaintenance(environment, output, invocation.Format(), operation);
            if (result.code != nano::ExitCode::Success)
                return CloseAndFinish(environment, output, invocation.Format(), operation,
                                      MaintenanceTerminalKind(result.code), result.code, result.reason);
        }
        if (selected == Domain::All && nano::IsCommandCancellationRequested())
            return CloseAndFinish(environment, output, invocation.Format(), operation,
                                  nano::SemanticEventKind::Cancelled, nano::ExitCode::Cancelled, "cancelled");
        if (selected == Domain::Loose || selected == Domain::All)
        {
            result = RunLooseMaintenance(environment, output, invocation.Format(), operation);
            if (result.code != nano::ExitCode::Success)
                return CloseAndFinish(environment, output, invocation.Format(), operation,
                                      MaintenanceTerminalKind(result.code), result.code, result.reason);
        }
        return CloseAndFinish(environment, output, invocation.Format(), operation,
                              nano::SemanticEventKind::Completed, nano::ExitCode::Success, "success");
    }

    [[nodiscard]] const char* MutationFailureReason(
        const asset_streaming::ProjectAssetPipelineResult result,
        const asset_streaming::ProjectAssetPipelineFailure& failure) noexcept
    {
        if (result == asset_streaming::ProjectAssetPipelineResult::OperationFailed &&
            failure.operation.result != assets::AssetOperationResult::Success)
            return AssetOperationResultToken(failure.operation.result);
        return PipelineResultToken(result);
    }

    [[nodiscard]] bool EmitMutationStatus(nano::Output& output, const nano::OutputFormat format,
                                          const char* const operation,
                                          const asset_streaming::ProjectAssetPipelineResult result,
                                          const asset_streaming::ProjectAssetRestoreReport& report,
                                          const asset_streaming::ProjectAssetPipelineFailure& failure,
                                          const u64 acceptedGeneration) noexcept
    {
        const bool cleanupPending = result == asset_streaming::ProjectAssetPipelineResult::CommittedCleanupPending ||
                                    report.operationCleanupPending;
        const bool reconciliationPending =
            result == asset_streaming::ProjectAssetPipelineResult::CommittedReconciliationPending;
        const bool committed = result == asset_streaming::ProjectAssetPipelineResult::Success ||
                               cleanupPending || reconciliationPending;
        const nano::SemanticField fields[]{
            StringField("scope", "mutation"),
            StringField("result", PipelineResultToken(result)),
            StringField("operation-result", AssetOperationResultToken(failure.operation.result)),
            StringField("transaction-result", AssetTransactionResultToken(failure.operation.transaction)),
            BooleanField("committed", committed),
            BooleanField("cleanup-pending", cleanupPending),
            BooleanField("reconciliation-pending", reconciliationPending),
            UnsignedField("accepted-generation", acceptedGeneration)};
        return Emit(output, format, nano::SemanticEventKind::Status, operation, fields);
    }

    [[nodiscard]] nano::ExitCode FinishMutation(AssetCommandEnvironment& environment,
                                                nano::Output& output,
                                                const nano::OutputFormat format,
                                                const char* const operation,
                                                const asset_streaming::ProjectAssetPipelineResult result,
                                                const asset_streaming::ProjectAssetRestoreReport& report,
                                                const asset_streaming::ProjectAssetPipelineFailure& failure) noexcept
    {
        if (!EmitMutationStatus(output, format, operation, result, report, failure,
                                environment.pipeline->GetAcceptedAssetGeneration()))
            return CloseAndFinish(environment, output, format, operation, nano::SemanticEventKind::Failed,
                                  nano::ExitCode::InternalFailure, "protocol-output-failed");
        if (result == asset_streaming::ProjectAssetPipelineResult::Success)
            return CloseAndFinish(environment, output, format, operation, nano::SemanticEventKind::Completed,
                                  nano::ExitCode::Success, "success");
        return CloseAndFinish(environment, output, format, operation, nano::SemanticEventKind::Failed,
                              nano::ExitCode::OperationFailed, MutationFailureReason(result, failure));
    }

    nano::ExitCode AssetMove(const nano::Invocation& invocation, nano::Output& output) noexcept
    {
        constexpr char operation[] = "asset-move";
        AssetCommandEnvironment environment;
        bool opened = false;
        const nano::ExitCode openResult = OpenEnvironment(invocation, output, operation, environment, opened);
        if (!opened)
            return openResult;
        if (nano::IsCommandCancellationRequested())
            return CloseAndFinish(environment, output, invocation.Format(), operation, nano::SemanticEventKind::Cancelled,
                                  nano::ExitCode::Cancelled, "cancelled");

        SelectedAsset selected;
        const char* const selectionFailure =
            SelectAsset(*environment.pipeline, environment.project.workspace, invocation.Positional(1u), nullptr, false, selected);
        if (selectionFailure != nullptr)
            return CloseAndFinish(environment, output, invocation.Format(), operation, nano::SemanticEventKind::Failed,
                                  nano::ExitCode::OperationFailed, selectionFailure);
        containers::String canonicalDestination;
        filesystem::AbsolutePath destination;
        const char* const destinationFailure = ResolveMutationDestination(
            environment.project.workspace, invocation.Positional(2u), canonicalDestination, destination);
        if (destinationFailure != nullptr)
            return CloseAndFinish(environment, output, invocation.Format(), operation, nano::SemanticEventKind::Failed,
                                  nano::ExitCode::OperationFailed, destinationFailure);

        asset_streaming::ProjectAssetRestoreReport report;
        asset_streaming::ProjectAssetPipelineFailure failure;
        const asset_streaming::ProjectAssetPipelineResult result =
            environment.pipeline->MoveAsset(selected.state->asset, destination, &report, &failure);
        return FinishMutation(environment, output, invocation.Format(), operation, result, report, failure);
    }

    nano::ExitCode AssetDuplicate(const nano::Invocation& invocation, nano::Output& output) noexcept
    {
        constexpr char operation[] = "asset-duplicate";
        AssetCommandEnvironment environment;
        bool opened = false;
        const nano::ExitCode openResult = OpenEnvironment(invocation, output, operation, environment, opened);
        if (!opened)
            return openResult;
        if (nano::IsCommandCancellationRequested())
            return CloseAndFinish(environment, output, invocation.Format(), operation, nano::SemanticEventKind::Cancelled,
                                  nano::ExitCode::Cancelled, "cancelled");

        SelectedAsset selected;
        const char* const selectionFailure =
            SelectAsset(*environment.pipeline, environment.project.workspace, invocation.Positional(1u), nullptr, false, selected);
        if (selectionFailure != nullptr)
            return CloseAndFinish(environment, output, invocation.Format(), operation, nano::SemanticEventKind::Failed,
                                  nano::ExitCode::OperationFailed, selectionFailure);
        containers::String canonicalDestination;
        filesystem::AbsolutePath destination;
        const char* const destinationFailure = ResolveMutationDestination(
            environment.project.workspace, invocation.Positional(2u), canonicalDestination, destination);
        if (destinationFailure != nullptr)
            return CloseAndFinish(environment, output, invocation.Format(), operation, nano::SemanticEventKind::Failed,
                                  nano::ExitCode::OperationFailed, destinationFailure);

        assets::AssetId newIdentity;
        asset_streaming::ProjectAssetRestoreReport report;
        asset_streaming::ProjectAssetPipelineFailure failure;
        const asset_streaming::ProjectAssetPipelineResult result =
            environment.pipeline->DuplicateAssetWithGeneratedIdentity(
                selected.state->asset, destination, newIdentity, &report, &failure);
        if (newIdentity.IsValid())
        {
            char assetId[33]{};
            if (!AssetIdHex(newIdentity, assetId))
                return CloseAndFinish(environment, output, invocation.Format(), operation, nano::SemanticEventKind::Failed,
                                      nano::ExitCode::InternalFailure, "asset-id-format-failed");
            const nano::SemanticField fields[]{StringField("scope", "duplicate"),
                                               StringField("new-asset-id", assetId),
                                               StringField("destination", canonicalDestination.AsChar())};
            if (!Emit(output, invocation.Format(), nano::SemanticEventKind::Status, operation, fields))
                return CloseAndFinish(environment, output, invocation.Format(), operation, nano::SemanticEventKind::Failed,
                                      nano::ExitCode::InternalFailure, "protocol-output-failed");
        }
        return FinishMutation(environment, output, invocation.Format(), operation, result, report, failure);
    }

    nano::ExitCode AssetRetarget(const nano::Invocation& invocation, nano::Output& output) noexcept
    {
        constexpr char operation[] = "asset-retarget";
        const u32 previousCount = invocation.OptionCount("output");
        const u32 sourceCount = invocation.OptionCount("replacement-source");
        const u32 replacementCount = invocation.OptionCount("replacement-output");
        if (previousCount == 0u || previousCount != sourceCount || previousCount != replacementCount)
            return Finish(output, invocation.Format(), operation, nano::SemanticEventKind::Failed,
                          nano::ExitCode::UsageError,
                          previousCount == 0u ? "replacement-mapping-required" : "replacement-mapping-count-mismatch");

        AssetCommandEnvironment environment;
        bool opened = false;
        const nano::ExitCode openResult = OpenEnvironment(invocation, output, operation, environment, opened);
        if (!opened)
            return openResult;
        if (nano::IsCommandCancellationRequested())
            return CloseAndFinish(environment, output, invocation.Format(), operation, nano::SemanticEventKind::Cancelled,
                                  nano::ExitCode::Cancelled, "cancelled");

        assets::AssetReferenceReplacement mappings[nano::MaximumParsedOptions]{};
        u32 mappingCount = 0u;
        const char* const mappingFailure = ResolveReplacementMappings(
            invocation, *environment.pipeline, environment.project.workspace, invocation.Positional(1u),
            mappings, nano::MaximumParsedOptions, mappingCount);
        if (mappingFailure != nullptr)
            return CloseAndFinish(environment, output, invocation.Format(), operation, nano::SemanticEventKind::Failed,
                                  nano::ExitCode::OperationFailed, mappingFailure);

        asset_streaming::ProjectAssetRestoreReport report;
        asset_streaming::ProjectAssetPipelineFailure failure;
        const asset_streaming::ProjectAssetPipelineResult result =
            environment.pipeline->RetargetAssetReferences({mappings, mappingCount}, &report, &failure);
        return FinishMutation(environment, output, invocation.Format(), operation, result, report, failure);
    }

    nano::ExitCode AssetDelete(const nano::Invocation& invocation, nano::Output& output) noexcept
    {
        constexpr char operation[] = "asset-delete";
        const char* const dependents = invocation.Option("dependents");
        const bool reject = Equal(dependents, "reject");
        const bool replace = Equal(dependents, "replace");
        if (!reject && !replace)
            return Finish(output, invocation.Format(), operation, nano::SemanticEventKind::Failed,
                          nano::ExitCode::UsageError,
                          dependents == nullptr ? "dependents-policy-required" : "invalid-dependents-policy");
        const u32 previousCount = invocation.OptionCount("output");
        const u32 sourceCount = invocation.OptionCount("replacement-source");
        const u32 replacementCount = invocation.OptionCount("replacement-output");
        if (reject && (previousCount != 0u || sourceCount != 0u || replacementCount != 0u))
            return Finish(output, invocation.Format(), operation, nano::SemanticEventKind::Failed,
                          nano::ExitCode::UsageError, "replacement-mapping-forbidden");
        if (replace && (previousCount == 0u || previousCount != sourceCount || previousCount != replacementCount))
            return Finish(output, invocation.Format(), operation, nano::SemanticEventKind::Failed,
                          nano::ExitCode::UsageError,
                          previousCount == 0u ? "replacement-mapping-required" : "replacement-mapping-count-mismatch");

        AssetCommandEnvironment environment;
        bool opened = false;
        const nano::ExitCode openResult = OpenEnvironment(invocation, output, operation, environment, opened);
        if (!opened)
            return openResult;
        if (nano::IsCommandCancellationRequested())
            return CloseAndFinish(environment, output, invocation.Format(), operation, nano::SemanticEventKind::Cancelled,
                                  nano::ExitCode::Cancelled, "cancelled");

        SelectedAsset selected;
        const char* const selectionFailure =
            SelectAsset(*environment.pipeline, environment.project.workspace, invocation.Positional(1u), nullptr, false, selected);
        if (selectionFailure != nullptr)
            return CloseAndFinish(environment, output, invocation.Format(), operation, nano::SemanticEventKind::Failed,
                                  nano::ExitCode::OperationFailed, selectionFailure);

        assets::AssetReferenceReplacement mappings[nano::MaximumParsedOptions]{};
        u32 mappingCount = 0u;
        if (replace)
        {
            const char* const mappingFailure = ResolveReplacementMappings(
                invocation, *environment.pipeline, environment.project.workspace, invocation.Positional(1u),
                mappings, nano::MaximumParsedOptions, mappingCount);
            if (mappingFailure != nullptr)
                return CloseAndFinish(environment, output, invocation.Format(), operation, nano::SemanticEventKind::Failed,
                                      nano::ExitCode::OperationFailed, mappingFailure);
        }

        asset_streaming::ProjectAssetRestoreReport report;
        asset_streaming::ProjectAssetPipelineFailure failure;
        const asset_streaming::ProjectAssetPipelineResult result =
            environment.pipeline->DeleteAsset(selected.state->asset, {mappings, mappingCount}, &report, &failure);
        return FinishMutation(environment, output, invocation.Format(), operation, result, report, failure);
    }

    [[nodiscard]] bool AutomaticOwnsForegroundWork(const asset_streaming::ProjectAssetAutomaticRecookStatus& status) noexcept
    {
        using State = asset_streaming::ProjectAssetAutomaticRecookState;
        return status.state == State::Planning || status.state == State::Building || status.state == State::Validating ||
               status.state == State::Cancelling;
    }

    [[nodiscard]] bool AutomaticReachedCancelledTerminal(const asset_streaming::ProjectAssetAutomaticRecookStatus& status,
                                                         const bool cancelAutomaticIssued) noexcept
    {
        return cancelAutomaticIssued && status.state == asset_streaming::ProjectAssetAutomaticRecookState::Failed &&
               asset_streaming::HasAutomaticRecookDirtyReason(
                   status.dirtyReasons, asset_streaming::ProjectAssetAutomaticRecookDirtyReason::CancelledBatch);
    }

    nano::ExitCode AssetWatch(const nano::Invocation& invocation, nano::Output& output) noexcept
    {
        constexpr char operation[] = "asset-watch";
        AssetCommandEnvironment environment;
        bool opened = false;
        const nano::ExitCode openResult = OpenEnvironment(invocation, output, operation, environment, opened);
        if (!opened)
            return openResult;
        asset_streaming::ProjectAssetPipelineFailure failure;
        asset_streaming::ProjectAssetPipelineResult result = environment.pipeline->EnableObservation({}, &failure);
        if (result != asset_streaming::ProjectAssetPipelineResult::Success)
            return CloseAndFinish(environment, output, invocation.Format(), operation, nano::SemanticEventKind::Failed,
                                  nano::ExitCode::OperationFailed, PipelineResultToken(result));

        // Observation startup always owns one authoritative generation-zero
        // reconciliation. Automatic recook cannot be admitted until that owner
        // reaches a terminal catch-up state; preserve the established E2.1/E2.3
        // sequencing instead of treating its Busy exclusion as an operation
        // failure. A cancellation arriving here is remembered and handled only
        // after the observation owner is safe to disable.
        bool cancelling = nano::IsCommandCancellationRequested();
        for (;;)
        {
            result = environment.pipeline->TickObservation(nullptr, &failure);
            if (result == asset_streaming::ProjectAssetPipelineResult::Success)
                break;
            if (result != asset_streaming::ProjectAssetPipelineResult::Pending)
                return CloseAndFinish(environment, output, invocation.Format(), operation, nano::SemanticEventKind::Failed,
                                      nano::ExitCode::OperationFailed, PipelineResultToken(result));
            cancelling = cancelling || nano::IsCommandCancellationRequested();
            concurrency::YieldCurrentThread();
        }
        if (cancelling)
        {
            const asset_streaming::ProjectAssetPipelineResult disableObservation = environment.pipeline->DisableObservation(&failure);
            if (disableObservation != asset_streaming::ProjectAssetPipelineResult::Success &&
                disableObservation != asset_streaming::ProjectAssetPipelineResult::ObservationNotActive)
                return CloseAndFinish(environment, output, invocation.Format(), operation, nano::SemanticEventKind::Failed,
                                      nano::ExitCode::OperationFailed, PipelineResultToken(disableObservation));
            return CloseAndFinish(environment, output, invocation.Format(), operation, nano::SemanticEventKind::Cancelled,
                                  nano::ExitCode::Cancelled, "cancelled");
        }
        result = environment.pipeline->EnableAutomaticRecook(&failure);
        if (result != asset_streaming::ProjectAssetPipelineResult::Success)
        {
            while (environment.pipeline->DisableObservation(&failure) == asset_streaming::ProjectAssetPipelineResult::Busy)
            {
                static_cast<void>(environment.pipeline->TickObservation(nullptr, &failure));
                concurrency::YieldCurrentThread();
            }
            return CloseAndFinish(environment, output, invocation.Format(), operation, nano::SemanticEventKind::Failed,
                                  nano::ExitCode::OperationFailed, PipelineResultToken(result));
        }

        bool cancelAutomaticIssued = false;
        bool automaticDisabled = false;
        bool observationDisabled = false;
        asset_streaming::ProjectAssetAutomaticRecookState reportedAutomatic = environment.pipeline->GetAutomaticRecookStatus().state;
        u64 reportedObservationTicks = static_cast<u64>(-1);
        bool reportedObservationState = false;
        bool reportedOverflow = false;
        bool reportedRescanRequired = false;
        bool reportedReconcilePending = false;
        for (;;)
        {
            if (nano::IsCommandCancellationRequested())
                cancelling = true;

            if (!observationDisabled)
            {
                result = environment.pipeline->TickObservation(nullptr, &failure);
                if (result != asset_streaming::ProjectAssetPipelineResult::Success &&
                    result != asset_streaming::ProjectAssetPipelineResult::Pending)
                    return CloseAndFinish(environment, output, invocation.Format(), operation, nano::SemanticEventKind::Failed,
                                          nano::ExitCode::OperationFailed, PipelineResultToken(result));
            }

            if (!automaticDisabled)
            {
                asset_streaming::ProjectAssetAutomaticRecookStatus automatic = environment.pipeline->GetAutomaticRecookStatus();
                if ((automatic.state == asset_streaming::ProjectAssetAutomaticRecookState::Failed ||
                     automatic.state == asset_streaming::ProjectAssetAutomaticRecookState::RecoveryRequired) &&
                    !AutomaticReachedCancelledTerminal(automatic, cancelAutomaticIssued))
                    return CloseAndFinish(environment, output, invocation.Format(), operation, nano::SemanticEventKind::Failed,
                                          nano::ExitCode::OperationFailed, AutomaticStateToken(automatic.state));

                const bool stopping = cancelling;
                if (stopping && !AutomaticOwnsForegroundWork(automatic))
                {
                    // Once a stop is requested, do not call TickAutomaticRecook
                    // on a Pending-but-unowned coordinator: that tick could
                    // admit brand-new foreground work after cancellation/failure.
                    const asset_streaming::ProjectAssetPipelineResult disableAutomatic =
                        environment.pipeline->DisableAutomaticRecook(&failure);
                    if (disableAutomatic == asset_streaming::ProjectAssetPipelineResult::Success)
                        automaticDisabled = true;
                    else if (disableAutomatic != asset_streaming::ProjectAssetPipelineResult::Busy)
                        return CloseAndFinish(environment, output, invocation.Format(), operation, nano::SemanticEventKind::Failed,
                                              nano::ExitCode::OperationFailed, PipelineResultToken(disableAutomatic));
                }
                else if (stopping && !cancelAutomaticIssued && AutomaticOwnsForegroundWork(automatic))
                {
                    const asset_streaming::ProjectAssetPipelineResult cancelResult = environment.pipeline->CancelAutomaticRecook(&failure);
                    if (cancelResult == asset_streaming::ProjectAssetPipelineResult::Success)
                        cancelAutomaticIssued = true;
                    else if (cancelResult != asset_streaming::ProjectAssetPipelineResult::InvalidState)
                        return CloseAndFinish(environment, output, invocation.Format(), operation, nano::SemanticEventKind::Failed,
                                              nano::ExitCode::OperationFailed, PipelineResultToken(cancelResult));
                }
                if (!automaticDisabled)
                {
                    const asset_streaming::ProjectAssetPipelineResult tick = environment.pipeline->TickAutomaticRecook(&failure);
                    if (tick != asset_streaming::ProjectAssetPipelineResult::Success &&
                        tick != asset_streaming::ProjectAssetPipelineResult::Pending &&
                        tick != asset_streaming::ProjectAssetPipelineResult::BuildRequestFailed)
                        return CloseAndFinish(environment, output, invocation.Format(), operation, nano::SemanticEventKind::Failed,
                                              nano::ExitCode::OperationFailed, PipelineResultToken(tick));
                    automatic = environment.pipeline->GetAutomaticRecookStatus();
                    if ((automatic.state == asset_streaming::ProjectAssetAutomaticRecookState::Failed ||
                         automatic.state == asset_streaming::ProjectAssetAutomaticRecookState::RecoveryRequired) &&
                        !AutomaticReachedCancelledTerminal(automatic, cancelAutomaticIssued))
                        return CloseAndFinish(environment, output, invocation.Format(), operation, nano::SemanticEventKind::Failed,
                                              nano::ExitCode::OperationFailed, AutomaticStateToken(automatic.state));
                    if (automatic.state != reportedAutomatic)
                    {
                        reportedAutomatic = automatic.state;
                        const nano::SemanticField fields[]{StringField("scope", "automatic-recook"),
                                                           StringField("state", AutomaticStateToken(automatic.state)),
                                                           UnsignedField("pending-assets", automatic.pendingAssets),
                                                           UnsignedField("pending-outputs", automatic.pendingOutputs)};
                        if (!Emit(output, invocation.Format(), nano::SemanticEventKind::Progress, operation, fields))
                            return CloseAndFinish(environment, output, invocation.Format(), operation, nano::SemanticEventKind::Failed,
                                                  nano::ExitCode::InternalFailure, "protocol-output-failed");
                    }
                }
            }

            const asset_streaming::ProjectAssetObservationStatus observation = environment.pipeline->GetObservationStatus();
            const bool observationEdge = !reportedObservationState || observation.overflow != reportedOverflow ||
                                         observation.rescanRequired != reportedRescanRequired ||
                                         observation.reconcilePending != reportedReconcilePending;
            const bool observationPeriodic = reportedObservationTicks == static_cast<u64>(-1) ||
                                             observation.ticks >= reportedObservationTicks + 64u;
            if (!observationDisabled && (observationEdge || observationPeriodic))
            {
                reportedObservationTicks = observation.ticks;
                reportedObservationState = true;
                reportedOverflow = observation.overflow;
                reportedRescanRequired = observation.rescanRequired;
                reportedReconcilePending = observation.reconcilePending;
                const nano::SemanticField fields[]{
                    StringField("scope", "observation"), UnsignedField("ticks", observation.ticks),
                    BooleanField("overflow", observation.overflow), BooleanField("rescan-required", observation.rescanRequired),
                    BooleanField("reconcile-pending", observation.reconcilePending),
                    UnsignedField("pending-paths", observation.pendingPaths), UnsignedField("hints-received", observation.hintsReceived),
                    UnsignedField("hints-coalesced", observation.hintsCoalesced), UnsignedField("hints-suppressed", observation.hintsSuppressed),
                    UnsignedField("hints-rejected", observation.hintsRejected), UnsignedField("rescans-completed", observation.rescansCompleted)};
                if (!Emit(output, invocation.Format(), nano::SemanticEventKind::Progress, operation, fields))
                    return CloseAndFinish(environment, output, invocation.Format(), operation, nano::SemanticEventKind::Failed,
                                          nano::ExitCode::InternalFailure, "protocol-output-failed");
            }

            if (cancelling)
            {
                if (!automaticDisabled)
                {
                    asset_streaming::ProjectAssetAutomaticRecookStatus automatic = environment.pipeline->GetAutomaticRecookStatus();
                    if (!AutomaticOwnsForegroundWork(automatic))
                    {
                        const asset_streaming::ProjectAssetPipelineResult disableAutomatic = environment.pipeline->DisableAutomaticRecook(&failure);
                        if (disableAutomatic == asset_streaming::ProjectAssetPipelineResult::Success)
                            automaticDisabled = true;
                        else if (disableAutomatic != asset_streaming::ProjectAssetPipelineResult::Busy)
                            return CloseAndFinish(environment, output, invocation.Format(), operation, nano::SemanticEventKind::Failed,
                                                  nano::ExitCode::OperationFailed, PipelineResultToken(disableAutomatic));
                    }
                }
                if (!observationDisabled)
                {
                    const asset_streaming::ProjectAssetPipelineResult disableObservation = environment.pipeline->DisableObservation(&failure);
                    if (disableObservation == asset_streaming::ProjectAssetPipelineResult::Success ||
                        disableObservation == asset_streaming::ProjectAssetPipelineResult::ObservationNotActive)
                        observationDisabled = true;
                    else if (disableObservation != asset_streaming::ProjectAssetPipelineResult::Busy)
                        return CloseAndFinish(environment, output, invocation.Format(), operation, nano::SemanticEventKind::Failed,
                                              nano::ExitCode::OperationFailed, PipelineResultToken(disableObservation));
                }
                if (automaticDisabled && observationDisabled)
                    return CloseAndFinish(environment, output, invocation.Format(), operation, nano::SemanticEventKind::Cancelled,
                                          nano::ExitCode::Cancelled, "cancelled");
            }
            concurrency::YieldCurrentThread();
        }
    }

    inline constexpr nano::OptionDescriptor CommonOptions[]{
        {"target", 't', nano::OptionValue::Required, "token", "Select one target declared by the project.", false},
        {"format", '\0', nano::OptionValue::Required, "human|jsonl", "Select stable human or JSON Lines output.", false}};
    inline constexpr nano::OptionDescriptor StatusOptions[]{
        {"target", 't', nano::OptionValue::Required, "token", "Select one target declared by the project.", false},
        {"output", 'o', nano::OptionValue::Required, "key", "Select an exact current VMETA output key.", false},
        {"format", '\0', nano::OptionValue::Required, "human|jsonl", "Select stable human or JSON Lines output.", false}};
    inline constexpr nano::OptionDescriptor BuildOptions[]{
        {"target", 't', nano::OptionValue::Required, "token", "Select one target declared by the project.", false},
        {"output", 'o', nano::OptionValue::Required, "key", "Select an exact current VMETA output key.", false},
        {"format", '\0', nano::OptionValue::Required, "human|jsonl", "Select stable human or JSON Lines output.", false}};
    inline constexpr nano::OptionDescriptor MaintainOptions[]{
        {"target", 't', nano::OptionValue::Required, "token", "Select one target declared by the project.", false},
        {"domain", 'd', nano::OptionValue::Required, "vddc|loose|all", "Select one maintenance owner; all runs VDDC then loose sequentially.", false},
        {"format", '\0', nano::OptionValue::Required, "human|jsonl", "Select stable human or JSON Lines output.", false}};
    inline constexpr nano::OptionDescriptor ReplacementOptions[]{
        {"target", 't', nano::OptionValue::Required, "token", "Select one target declared by the project.", false},
        {"output", 'o', nano::OptionValue::Required, "key", "Select one output of the primary accepted source.", true},
        {"replacement-source", '\0', nano::OptionValue::Required, "source", "Select the paired accepted replacement source.", true},
        {"replacement-output", '\0', nano::OptionValue::Required, "key", "Select the paired exact replacement output key.", true},
        {"format", '\0', nano::OptionValue::Required, "human|jsonl", "Select stable human or JSON Lines output.", false}};
    inline constexpr nano::OptionDescriptor DeleteOptions[]{
        {"target", 't', nano::OptionValue::Required, "token", "Select one target declared by the project.", false},
        {"dependents", '\0', nano::OptionValue::Required, "reject|replace", "Choose explicit referencer policy for deletion.", false},
        {"output", 'o', nano::OptionValue::Required, "key", "Select one output of the deleted accepted source for replacement.", true},
        {"replacement-source", '\0', nano::OptionValue::Required, "source", "Select the paired accepted replacement source.", true},
        {"replacement-output", '\0', nano::OptionValue::Required, "key", "Select the paired exact replacement output key.", true},
        {"format", '\0', nano::OptionValue::Required, "human|jsonl", "Select stable human or JSON Lines output.", false}};
} // namespace

namespace vanguard::nanovanguard
{
    bool RegisterProjectAssetCommands(CommandRegistry& registry) noexcept
    {
        if (registry.Register({"asset", nullptr, "Inspect and drive the current project's asset pipeline.", "<subcommand>", nullptr, 0u, 0u, 0u, nullptr}) !=
            RegistrationResult::Success)
            return false;
        return registry.Register({"status", "asset", "Report current project-asset lifecycle and optional accepted source/output status.",
                                  "<project> [source]", StatusOptions, static_cast<u32>(sizeof(StatusOptions) / sizeof(StatusOptions[0])),
                                  1u, 2u, &AssetStatus}) == RegistrationResult::Success &&
               registry.Register({"rescan", "asset", "Authoritatively rescan the current project Assets root.", "<project>", CommonOptions,
                                  static_cast<u32>(sizeof(CommonOptions) / sizeof(CommonOptions[0])), 1u, 1u, &AssetRescan}) == RegistrationResult::Success &&
               registry.Register({"build", "asset", "Build and publish exactly one current accepted output.", "<project> <source>", BuildOptions,
                                  static_cast<u32>(sizeof(BuildOptions) / sizeof(BuildOptions[0])), 2u, 2u, &AssetBuild}) == RegistrationResult::Success &&
               registry.Register({"reimport", "asset", "Reimport the complete current accepted output set for one asset.", "<project> <source>", CommonOptions,
                                  static_cast<u32>(sizeof(CommonOptions) / sizeof(CommonOptions[0])), 2u, 2u, &AssetReimport}) == RegistrationResult::Success &&
               registry.Register({"watch", "asset", "Observe source changes and run automatic recook until current-session cancellation.", "<project>", CommonOptions,
                                  static_cast<u32>(sizeof(CommonOptions) / sizeof(CommonOptions[0])), 1u, 1u, &AssetWatch}) == RegistrationResult::Success &&
               registry.Register({"maintain", "asset", "Run bounded project-asset cache maintenance through existing owners.", "<project>", MaintainOptions,
                                  static_cast<u32>(sizeof(MaintainOptions) / sizeof(MaintainOptions[0])), 1u, 1u, &AssetMaintain}) == RegistrationResult::Success &&
               registry.Register({"move", "asset", "Move one current accepted asset inside the project Assets root.", "<project> <source> <destination>", CommonOptions,
                                  static_cast<u32>(sizeof(CommonOptions) / sizeof(CommonOptions[0])), 3u, 3u, &AssetMove}) == RegistrationResult::Success &&
               registry.Register({"duplicate", "asset", "Duplicate one current accepted asset with a pipeline-generated identity.", "<project> <source> <destination>", CommonOptions,
                                  static_cast<u32>(sizeof(CommonOptions) / sizeof(CommonOptions[0])), 3u, 3u, &AssetDuplicate}) == RegistrationResult::Success &&
               registry.Register({"retarget", "asset", "Retarget authored references from selected outputs to explicit accepted replacements.", "<project> <source>", ReplacementOptions,
                                  static_cast<u32>(sizeof(ReplacementOptions) / sizeof(ReplacementOptions[0])), 2u, 2u, &AssetRetarget}) == RegistrationResult::Success &&
               registry.Register({"delete", "asset", "Delete one accepted asset under an explicit dependent/replacement policy.", "<project> <source>", DeleteOptions,
                                  static_cast<u32>(sizeof(DeleteOptions) / sizeof(DeleteOptions[0])), 2u, 2u, &AssetDelete}) == RegistrationResult::Success;
    }
} // namespace vanguard::nanovanguard
