#pragma once

#include <vanguard/assets/source_database.hpp>
#include <vanguard/crypto/crypto.hpp>

namespace vanguard::asset_streaming
{
    enum class ProjectAssetPresence : u8
    {
        Present,
        Missing,
        Conflicting
    };

    // Reasons compose. A move may also carry content/settings/importer/output or
    // reference changes; lifecycle reasons never erase the input-change reasons.
    enum class ProjectAssetChangeReason : u32
    {
        None = 0,
        New = 1u << 0u,
        ContentChanged = 1u << 1u,
        SettingsChanged = 1u << 2u,
        ImporterChanged = 1u << 3u,
        ImporterVersionChanged = 1u << 4u,
        Moved = 1u << 5u,
        Missing = 1u << 6u,
        Restored = 1u << 7u,
        Unsupported = 1u << 8u,
        Supported = 1u << 9u,
        Conflicting = 1u << 10u,
        OutputsChanged = 1u << 11u,
        ReferencesChanged = 1u << 12u
    };

    [[nodiscard]] constexpr ProjectAssetChangeReason operator|(const ProjectAssetChangeReason left,
                                                               const ProjectAssetChangeReason right) noexcept
    {
        return static_cast<ProjectAssetChangeReason>(static_cast<u32>(left) | static_cast<u32>(right));
    }

    [[nodiscard]] constexpr ProjectAssetChangeReason operator&(const ProjectAssetChangeReason left,
                                                               const ProjectAssetChangeReason right) noexcept
    {
        return static_cast<ProjectAssetChangeReason>(static_cast<u32>(left) & static_cast<u32>(right));
    }

    constexpr ProjectAssetChangeReason& operator|=(ProjectAssetChangeReason& left, const ProjectAssetChangeReason right) noexcept
    {
        left = left | right;
        return left;
    }

    [[nodiscard]] constexpr bool HasChangeReason(const ProjectAssetChangeReason value,
                                                 const ProjectAssetChangeReason reason) noexcept
    {
        return (static_cast<u32>(value) & static_cast<u32>(reason)) != 0u;
    }

    // E2.2-only source-row diagnostic evidence. Keep this separate from
    // SourceIssue/SourceImportState: those remain SourceDatabase classification
    // authority, while these reasons describe comparison evidence only.
    enum class ProjectAssetSourceDiagnosticReason : u32
    {
        None = 0,
        AmbiguousMove = 1u << 0u,
        SourceContentUnavailable = 1u << 1u
    };

    [[nodiscard]] constexpr ProjectAssetSourceDiagnosticReason operator|(const ProjectAssetSourceDiagnosticReason left,
                                                                         const ProjectAssetSourceDiagnosticReason right) noexcept
    {
        return static_cast<ProjectAssetSourceDiagnosticReason>(static_cast<u32>(left) | static_cast<u32>(right));
    }

    [[nodiscard]] constexpr bool HasSourceDiagnosticReason(const ProjectAssetSourceDiagnosticReason value,
                                                           const ProjectAssetSourceDiagnosticReason reason) noexcept
    {
        return (static_cast<u32>(value) & static_cast<u32>(reason)) != 0u;
    }

    // Hard bounds for the accepted historical snapshot and the current accepted
    // change set. Limit failures are transactional in E2.2: callers must keep the
    // previous accepted state rather than truncating any collection.
    struct ProjectAssetChangeStateLimits
    {
        u32 maximumAssets = 262144;
        u32 maximumTombstones = 262144;
        u32 maximumOutputs = 1048576;
        u32 maximumReferences = 4194304;
        u32 maximumDiagnostics = 262144;
        u32 maximumChanges = 262144;
        u32 maximumChangeOutputs = 2097152;
        u32 maximumChangeReferences = 8388608;
        u32 maximumInvalidationSeeds = 262144;
        u32 maximumInvalidationOutputs = 2097152;
        u64 maximumSerializedBytes = 512ull * 1024ull * 1024ull;

        [[nodiscard]] bool IsValid() const noexcept
        {
            return maximumAssets != 0u && maximumTombstones != 0u && maximumTombstones <= maximumAssets &&
                   maximumOutputs != 0u && maximumReferences != 0u && maximumDiagnostics != 0u && maximumChanges != 0u &&
                   maximumChangeOutputs != 0u && maximumChangeReferences != 0u && maximumInvalidationSeeds != 0u &&
                   maximumInvalidationOutputs != 0u && maximumSerializedBytes >= 128u && maximumSerializedBytes <= 0xffffffffull;
        }
    };

    // Canonical metadata-declared output. The key owns its bytes inside the
    // accepted asset baseline; query spans borrow these values until the next accepted
    // state publication or shutdown.
    struct ProjectAssetOutputState
    {
        containers::String key;
        resources::ResourceReference resource;
    };

    // Public borrowed view of one accepted uniquely identified asset. The
    // baseline owns every path/string/output/reference represented here.
    struct ProjectAssetState
    {
        assets::AssetId asset;
        // Durable location evidence uses the root directory itself, never the
        // positional SourceRecord::root index, plus the root-relative identity.
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
        bool hasSourceContentFingerprint = false;
        u64 acceptedGeneration = 0;
        containers::ArraySpan<const ProjectAssetOutputState> outputs;
        containers::ArraySpan<const assets::AssetReference> references;
    };

    // Previous/current comparison evidence retained with a canonical change.
    // `available` is false for a side that does not exist (for example the
    // previous side of New). Output/reference spans are borrowed from immutable
    // accepted change storage.
    struct ProjectAssetChangeEvidence
    {
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
        containers::ArraySpan<const ProjectAssetOutputState> outputs;
        containers::ArraySpan<const assets::AssetReference> references;
    };

    struct ProjectAssetChangeRecord
    {
        assets::AssetId asset;
        ProjectAssetChangeReason reasons = ProjectAssetChangeReason::None;
        ProjectAssetChangeEvidence previous;
        ProjectAssetChangeEvidence current;
    };

    // Rows that cannot safely own an AssetId remain separately queryable. A
    // valid parsed identity may be present for diagnostics, but it never grants
    // ownership when the row is conflicting/invalid.
    struct ProjectAssetSourceDiagnostic
    {
        filesystem::AbsolutePath sourceRoot;
        resources::ResourcePath sourceIdentity;
        filesystem::AbsolutePath physicalPath;
        assets::AssetId asset;
        u32 sourceIssues = assets::SourceIssueNone;
        assets::MetadataResult metadataResult = assets::MetadataResult::InvalidData;
        assets::SourceImportState importState = assets::SourceImportState::NotClassified;
        assets::SourceReferenceScanState referenceScanState = assets::SourceReferenceScanState::NotScanned;
        ProjectAssetSourceDiagnosticReason reasons = ProjectAssetSourceDiagnosticReason::None;
        bool hasParsedAssetIdentity = false;
    };
} // namespace vanguard::asset_streaming
