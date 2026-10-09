#pragma once

#include <vanguard/asset_streaming/project_asset_change_state.hpp>
#include <vanguard/containers/containers.hpp>
#include <vanguard/memory/memory.hpp>

namespace vanguard::asset_streaming
{
    // P2 consumes source-content evidence prepared coherently with the candidate
    // reference scan. The array passed to CompareProjectAssetCandidate is aligned
    // one-for-one with SourceDatabase::GetResources(). P3 owns producing it.
    struct ProjectAssetPreparedSourceEvidence
    {
        crypto::Digest256 sourceContentFingerprint;
        bool hasSourceContentFingerprint = false;
    };

    enum class ProjectAssetComparisonResult : u8
    {
        Success,
        InvalidArgument,
        LimitExceeded
    };

    // Immutable-after-publication owner state for E2.2. SourceDatabase remains
    // current authority; this accepted asset baseline owns only historical evidence,
    // the current canonical change set and query indexes. P2 builds a complete
    // instance away from the owner; P3 swaps it at the acceptance boundary.
    struct ProjectAssetAcceptedBaseline final
    {
        explicit ProjectAssetAcceptedBaseline(const ProjectAssetChangeStateLimits& stateLimits = {}) noexcept;

        ProjectAssetAcceptedBaseline(const ProjectAssetAcceptedBaseline&) = delete;
        ProjectAssetAcceptedBaseline& operator=(const ProjectAssetAcceptedBaseline&) = delete;

        void Reset() noexcept;
        void Swap(ProjectAssetAcceptedBaseline& other) noexcept;

        [[nodiscard]] const ProjectAssetState* LookupAsset(assets::AssetId asset) const noexcept;
        [[nodiscard]] containers::ArraySpan<const assets::AssetReference> GetMissingOutputReferencers(
            resources::ResourceReference output) const noexcept;

        ProjectAssetChangeStateLimits limits;
        u64 generation = 0;
        u32 tombstones = 0;

        containers::DynamicArray<ProjectAssetState> assets;
        containers::HashMap<containers::String, u32> assetLookup;
        containers::DynamicArray<ProjectAssetOutputState> outputs;
        containers::DynamicArray<assets::AssetReference> references;
        // Current effective authored links sorted by target for missing-output
        // referencer queries. SourceDatabase remains current reference authority.
        containers::DynamicArray<assets::AssetReference> backwardReferences;

        containers::DynamicArray<ProjectAssetChangeRecord> changes;
        containers::DynamicArray<ProjectAssetOutputState> changePreviousOutputs;
        containers::DynamicArray<ProjectAssetOutputState> changeCurrentOutputs;
        containers::DynamicArray<assets::AssetReference> changePreviousReferences;
        containers::DynamicArray<assets::AssetReference> changeCurrentReferences;

        containers::DynamicArray<ProjectAssetSourceDiagnostic> diagnostics;
        // Typed source/build inputs for E2.3 translation when the importer exposes
        // a descriptor-level source type. Callback-importer dirty evidence remains
        // in `changes` for E2.3 to resolve through importer build-request creation.
        // These are distinct from cooked outputs and do not themselves schedule work.
        containers::DynamicArray<resources::ResourceReference> invalidationSeeds;
        containers::DynamicArray<resources::ResourceReference> invalidationOutputs;
    };

    // Deterministic, allocation-bounded P2 comparison. This function never
    // mutates `previous` or `candidate`, performs no filesystem I/O/persistence,
    // and publishes nothing. `proposed` is replaced only on Success.
    [[nodiscard]] ProjectAssetComparisonResult CompareProjectAssetCandidate(
        const ProjectAssetAcceptedBaseline& previous, const assets::SourceDatabase& candidate,
        containers::ArraySpan<const ProjectAssetPreparedSourceEvidence> sourceEvidence,
        ProjectAssetAcceptedBaseline& proposed) noexcept;
} // namespace vanguard::asset_streaming
