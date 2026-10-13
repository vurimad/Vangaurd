#pragma once

#include <vanguard/asset_streaming/project_asset_change_state_internal.hpp>
#include <vanguard/asset_streaming/project_asset_pipeline.hpp>
#include <vanguard/assets/project_asset_build_system.hpp>

namespace vanguard::asset_streaming
{
    // Owned subset of one accepted change side needed by E2.3. Authored
    // references deliberately stay out of this owner: VADI is the build
    // dependency authority. Current BuildRequests below own the exact source,
    // metadata and settings bytes used for request translation/freshness work.
    struct ProjectAssetAutomaticOwnedEvidence final
    {
        ProjectAssetAutomaticOwnedEvidence() noexcept;

        ProjectAssetAutomaticOwnedEvidence(const ProjectAssetAutomaticOwnedEvidence&) = delete;
        ProjectAssetAutomaticOwnedEvidence& operator=(const ProjectAssetAutomaticOwnedEvidence&) = delete;
        ProjectAssetAutomaticOwnedEvidence(ProjectAssetAutomaticOwnedEvidence&&) noexcept = default;
        ProjectAssetAutomaticOwnedEvidence& operator=(ProjectAssetAutomaticOwnedEvidence&&) noexcept = default;

        [[nodiscard]] bool CopyFrom(const ProjectAssetChangeEvidence& source) noexcept;
        void Reset() noexcept;
        [[nodiscard]] u64 ByteCount() const noexcept;

        filesystem::AbsolutePath sourceRoot;
        resources::ResourcePath sourceIdentity;
        filesystem::AbsolutePath physicalPath;
        crypto::Digest256 sourceContentFingerprint;
        crypto::Digest256 settingsFingerprint;
        assets::SourceImporterId importer = assets::InvalidSourceImporterId;
        u32 authoredImporterVersion = 0;
        u32 availableImporterVersion = 0;
        u32 settingsVersion = 0;
        resources::ResourceTypeId sourceType = resources::InvalidResourceTypeId;
        u32 sourceIssues = assets::SourceIssueNone;
        assets::SourceImportState importState = assets::SourceImportState::NotClassified;
        assets::SourceReferenceScanState referenceScanState = assets::SourceReferenceScanState::NotScanned;
        ProjectAssetPresence presence = ProjectAssetPresence::Present;
        bool available = false;
        bool hasSourceContentFingerprint = false;
        containers::DynamicArray<resources::ResourceReference> outputs;
    };

    struct ProjectAssetAutomaticPendingAsset final
    {
        ProjectAssetAutomaticPendingAsset() noexcept = default;

        ProjectAssetAutomaticPendingAsset(const ProjectAssetAutomaticPendingAsset&) = delete;
        ProjectAssetAutomaticPendingAsset& operator=(const ProjectAssetAutomaticPendingAsset&) = delete;
        ProjectAssetAutomaticPendingAsset(ProjectAssetAutomaticPendingAsset&&) noexcept = default;
        ProjectAssetAutomaticPendingAsset& operator=(ProjectAssetAutomaticPendingAsset&&) noexcept = default;

        assets::AssetId asset;
        ProjectAssetChangeReason reasons = ProjectAssetChangeReason::None;
        ProjectAssetAutomaticOwnedEvidence previous;
        ProjectAssetAutomaticOwnedEvidence current;
        u64 firstAcceptedGeneration = 0;
        u64 latestAcceptedGeneration = 0;
        assets::ProjectAssetBuildResult requestResolution = assets::ProjectAssetBuildResult::Success;
        bool diagnostic = false;
    };

    enum class ProjectAssetAutomaticForceReason : u8
    {
        None = 0u,
        CacheRepair = 1u << 0u,
        ExplicitReimport = 1u << 1u
    };

    [[nodiscard]] constexpr ProjectAssetAutomaticForceReason operator|(const ProjectAssetAutomaticForceReason left,
                                                                      const ProjectAssetAutomaticForceReason right) noexcept
    {
        return static_cast<ProjectAssetAutomaticForceReason>(static_cast<u8>(left) | static_cast<u8>(right));
    }

    [[nodiscard]] constexpr bool HasProjectAssetAutomaticForceReason(const ProjectAssetAutomaticForceReason value,
                                                                     const ProjectAssetAutomaticForceReason reason) noexcept
    {
        return (static_cast<u8>(value) & static_cast<u8>(reason)) != 0u;
    }

    struct ProjectAssetAutomaticForcedOutput
    {
        resources::ResourceReference output;
        ProjectAssetAutomaticForceReason reasons = ProjectAssetAutomaticForceReason::None;
    };

    // Canonical bounded pending set. It is a coalesced owner state, never an
    // accepted-generation replay log. `inputs` are typed build/source identities
    // suitable for VADI reverse lookup. `outputs` are cooked-output seeds and
    // indexed direct dependants. `currentRequests` are the latest ownership-safe
    // requests for currently resolvable explicit outputs. `forcedOutputs` is a
    // bounded/deduplicated subset of those current outputs with reason flags for
    // exact cache repair and/or explicit reimport; it is not a second work queue.
    struct ProjectAssetAutomaticPendingState final
    {
        ProjectAssetAutomaticPendingState() noexcept;

        ProjectAssetAutomaticPendingState(const ProjectAssetAutomaticPendingState&) = delete;
        ProjectAssetAutomaticPendingState& operator=(const ProjectAssetAutomaticPendingState&) = delete;

        void Reset() noexcept;
        void Swap(ProjectAssetAutomaticPendingState& other) noexcept;

        containers::DynamicArray<ProjectAssetAutomaticPendingAsset> assets;
        containers::DynamicArray<resources::ResourceReference> inputs;
        containers::DynamicArray<resources::ResourceReference> outputs;
        containers::DynamicArray<assets::OwnedBuildRequest> currentRequests;
        containers::DynamicArray<ProjectAssetAutomaticForcedOutput> forcedOutputs;
        // Physical bytes held by immutable source backings shared across currentRequests.
        // Request-local metadata/settings remain charged by ComputeBytes per request.
        u64 currentRequestSharedSourceBytes = 0;
        u64 capturedEvidenceBytes = 0;
        u64 lastAcceptedGeneration = 0;
        u32 diagnostics = 0;
    };

    enum class ProjectAssetAutomaticPendingPrepareResult : u8
    {
        Success,
        NoChange,
        RecoveryRequired
    };

    // Builds the complete next coalesced pending state away from the owner. On
    // Success `staged` is ready for an O(1) swap. On RecoveryRequired `staged`
    // must be ignored and the caller keeps its previous complete pending set,
    // marks authoritative recovery, and still accepts the E2.2 generation.
    [[nodiscard]] ProjectAssetAutomaticPendingPrepareResult PrepareProjectAssetAutomaticPending(
        const ProjectAssetAutomaticPendingState& existing, const ProjectAssetAcceptedBaseline& proposed,
        const assets::SourceDatabase& candidate, const assets::ProjectAssetBuildSystem& builds,
        assets::DependencyIndex& index, const ProjectAssetAutomaticRecookConfig& config,
        ProjectAssetAutomaticPendingState& staged) noexcept;

    // One bounded current-source + committed-VADI recovery pass used when
    // automatic recook starts without a transient queue (startup/re-enable).
    // Supported current outputs are translated exactly like accepted changes.
    // Durable Missing accepted rows are historical tombstones and are skipped;
    // unsupported current Present state remains diagnostic and is never given a
    // fabricated request. Live Missing transitions remain diagnostic in P2.
    [[nodiscard]] ProjectAssetAutomaticPendingPrepareResult PrepareProjectAssetAutomaticRecovery(
        const ProjectAssetAcceptedBaseline& accepted, const assets::SourceDatabase& sources,
        const assets::ProjectAssetBuildSystem& builds, assets::DependencyIndex& index,
        const ProjectAssetAutomaticRecookConfig& config, ProjectAssetAutomaticPendingState& staged) noexcept;

    // Merges one exact cache-repair requirement into the same bounded automatic
    // pending owner used by E2.3. The output is freshly resolved through the
    // canonical importer path; no compiler or graph work is started here.
    [[nodiscard]] ProjectAssetAutomaticPendingPrepareResult PrepareProjectAssetAutomaticRepair(
        const ProjectAssetAutomaticPendingState& existing, resources::ResourceReference output,
        u64 acceptedGeneration, const assets::SourceDatabase& sources,
        const assets::ProjectAssetBuildSystem& builds, assets::DependencyIndex& index,
        const ProjectAssetAutomaticRecookConfig& config, ProjectAssetAutomaticPendingState& staged) noexcept;

    // Transactionally merges the complete current accepted output set for one
    // asset into the existing pending owner and marks every output as an explicit
    // reimport force. No graph/compiler work is started here.
    [[nodiscard]] ProjectAssetAutomaticPendingPrepareResult PrepareProjectAssetAutomaticReimport(
        const ProjectAssetAutomaticPendingState& existing, const ProjectAssetState& asset,
        u64 acceptedGeneration, const assets::SourceDatabase& sources,
        const assets::ProjectAssetBuildSystem& builds, assets::DependencyIndex& index,
        const ProjectAssetAutomaticRecookConfig& config, ProjectAssetAutomaticPendingState& staged,
        containers::ArraySpan<const assets::OwnedBuildRequest> alreadyResolved = {}) noexcept;

    // Rebuilds only current-session ExplicitReimport intent after an explicit-only
    // failed/cancelled/stale batch. Current accepted outputs are re-resolved, while
    // unrelated accepted changes and dormant cache-repair work remain outside the
    // transient coordinator when the automatic policy is disabled.
    [[nodiscard]] ProjectAssetAutomaticPendingPrepareResult PrepareProjectAssetAutomaticExplicitRecovery(
        const ProjectAssetAutomaticPendingState& retainedActive,
        const ProjectAssetAutomaticPendingState& retainedPending,
        const ProjectAssetAcceptedBaseline& accepted, const assets::SourceDatabase& sources,
        const assets::ProjectAssetBuildSystem& builds, assets::DependencyIndex& index,
        const ProjectAssetAutomaticRecookConfig& config, ProjectAssetAutomaticPendingState& staged) noexcept;

    // Any retained failed/cancelled/stale automatic batch can contain execution-
    // critical previous-side VADI identities while newer accepted work is pending.
    // A current-source recovery snapshot cannot reconstruct deleted or moved-away
    // identities from source truth alone. Preserve the union of those already-
    // owned invalidation identities before replacing active with recovery.
    [[nodiscard]] bool PreserveProjectAssetAutomaticRecoveryInvalidations(
        const ProjectAssetAutomaticPendingState& retainedActive,
        const ProjectAssetAutomaticPendingState& retainedPending,
        const ProjectAssetAutomaticRecookConfig& config,
        ProjectAssetAutomaticPendingState& recovered) noexcept;
} // namespace vanguard::asset_streaming
