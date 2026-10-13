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

    // Durable write-ahead ownership for a planned-import generation cutover.
    // This is deliberately not part of `changes`: it survives same-generation
    // snapshot rewrites and process restart until the exact successor cohort has
    // become visible and its predecessor retirement has settled.
    struct ProjectAssetCohortIntent final
    {
        ProjectAssetCohortIntent() noexcept
            : predecessorOutputs(memory::pools::Assets::GetInstance()), successorOutputs(memory::pools::Assets::GetInstance()) {}

        [[nodiscard]] bool IsActive() const noexcept
        {
            return asset.IsValid() && sourceIdentity.IsValid() && reviewedAcceptedGeneration != 0u &&
                   !targetMetadataFingerprint.IsEmpty() && !successorOutputs.Empty();
        }

        void Reset() noexcept
        {
            asset = {};
            sourceIdentity = {};
            reviewedAcceptedGeneration = 0u;
            targetMetadataFingerprint = {};
            predecessorOutputs.Clear();
            successorOutputs.Clear();
        }

        void Swap(ProjectAssetCohortIntent& other) noexcept
        {
            const assets::AssetId swappedAsset = asset;
            asset = other.asset;
            other.asset = swappedAsset;
            const resources::ResourcePath swappedSourceIdentity = sourceIdentity;
            sourceIdentity = other.sourceIdentity;
            other.sourceIdentity = swappedSourceIdentity;
            const u64 swappedGeneration = reviewedAcceptedGeneration;
            reviewedAcceptedGeneration = other.reviewedAcceptedGeneration;
            other.reviewedAcceptedGeneration = swappedGeneration;
            const crypto::Digest256 swappedFingerprint = targetMetadataFingerprint;
            targetMetadataFingerprint = other.targetMetadataFingerprint;
            other.targetMetadataFingerprint = swappedFingerprint;
            predecessorOutputs.Swap(other.predecessorOutputs);
            successorOutputs.Swap(other.successorOutputs);
        }

        [[nodiscard]] bool CopyFrom(const ProjectAssetCohortIntent& other) noexcept
        {
            ProjectAssetCohortIntent copied;
            if (!other.IsActive())
            {
                Swap(copied);
                return true;
            }
            copied.predecessorOutputs.Reserve(other.predecessorOutputs.Size());
            copied.successorOutputs.Reserve(other.successorOutputs.Size());
            if (copied.predecessorOutputs.Capacity() < other.predecessorOutputs.Size() ||
                copied.successorOutputs.Capacity() < other.successorOutputs.Size())
                return false;
            copied.asset = other.asset;
            copied.sourceIdentity = other.sourceIdentity;
            copied.reviewedAcceptedGeneration = other.reviewedAcceptedGeneration;
            copied.targetMetadataFingerprint = other.targetMetadataFingerprint;
            for (const resources::ResourceReference output : other.predecessorOutputs)
                copied.predecessorOutputs.PushBack(output);
            for (const resources::ResourceReference output : other.successorOutputs)
                copied.successorOutputs.PushBack(output);
            if (copied.predecessorOutputs.Size() != other.predecessorOutputs.Size() ||
                copied.successorOutputs.Size() != other.successorOutputs.Size())
                return false;
            Swap(copied);
            return true;
        }

        assets::AssetId asset;
        resources::ResourcePath sourceIdentity;
        u64 reviewedAcceptedGeneration = 0u;
        crypto::Digest256 targetMetadataFingerprint;
        containers::DynamicArray<resources::ResourceReference> predecessorOutputs;
        containers::DynamicArray<resources::ResourceReference> successorOutputs;
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

        ProjectAssetCohortIntent cohortIntent;

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
