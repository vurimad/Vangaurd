#pragma once

#include <vanguard/assets/asset_metadata.hpp>
#include <vanguard/filesystem/filesystem.hpp>

namespace vanguard::assets
{
    struct SourceRoot
    {
        filesystem::AbsolutePath directory;
        bool readOnly = true;
    };

    // A complete, caller-verified inventory of source paths, including sources
    // inferred from orphan .vmeta files. Never include sidecars as source rows.
    struct SourceFile
    {
        filesystem::AbsolutePath path;
        u32 root = 0;
    };

    enum SourceIssue : u32
    {
        SourceIssueNone = 0,
        SourceMissing = 1u << 0u,
        MetadataMissing = 1u << 1u,
        MetadataUnreadable = 1u << 2u,
        MetadataInvalid = 1u << 3u,
        AssetIdentityConflict = 1u << 4u,
        OutputIdentityConflict = 1u << 5u
    };

    enum class SourceImportState : u8
    {
        NotClassified,
        MetadataSelected,
        Candidate,
        Unsupported,
        DiscoveryUnavailable,
        Ambiguous,
        ImporterUnavailable,
        VersionMismatch,
        SourceMismatch,
        InvalidMetadata
    };

    struct SourceRecord
    {
        filesystem::AbsolutePath path;
        u32 root = 0;
        u32 issues = SourceIssueNone;
        MetadataResult metadataResult = MetadataResult::InvalidData;
        AssetMetadata metadata;
        SourceImportState importState = SourceImportState::NotClassified;
        CompilerId importer = InvalidCompilerId;
        u32 availableImporterVersion = 0;
        resources::ResourceTypeId sourceType = resources::InvalidResourceTypeId;
        resources::ResourceTypeId primaryOutputType = resources::InvalidResourceTypeId;
    };

    struct SourceQuery
    {
        // Empty folder means all roots. Non-empty folder must be a directory.
        filesystem::AbsolutePath folder;
        bool recursive = true;
        resources::ResourceTypeId sourceType = resources::InvalidResourceTypeId;
        resources::ResourceTypeId outputType = resources::InvalidResourceTypeId;
        u32 requiredIssues = 0;
        u32 excludedIssues = 0;
        bool filterImportState = false;
        SourceImportState importState = SourceImportState::NotClassified;
    };

    // Returning false stops traversal. Records are borrowed; do not mutate the
    // database, start a rescan, or retain pointers past its next update.
    using VisitSourceFunction = bool (*)(const SourceRecord& record, void* userData) noexcept;

    enum class SourceDatabaseResult : u8
    {
        Success,
        InvalidInventory,
        LimitExceeded,
        ScanFailed
    };

    class SourceDatabase final
    {
    public:
        SourceDatabase() noexcept;
        SourceDatabase(const SourceDatabase&) = delete;
        SourceDatabase& operator=(const SourceDatabase&) = delete;

        // Exclusive owner operation: callers must finish readers/build-request
        // resolution before rebuilding. Does no cooking, identity assignment or writes.
        // Inventory failure leaves the previous database unchanged; per-file
        // metadata errors become visible records instead of dropping the source.
        [[nodiscard]] SourceDatabaseResult Rebuild(filesystem::Manager& files, containers::ArraySpan<const SourceRoot> roots, containers::ArraySpan<const SourceFile> sources) noexcept;
        // Same exclusive-owner contract as Rebuild. All roots must scan
        // successfully before replacing the catalog. No source writes.
        [[nodiscard]] SourceDatabaseResult Rescan(filesystem::Manager& files, containers::ArraySpan<const SourceRoot> roots, filesystem::ScanResult* scanFailure = nullptr) noexcept;
        [[nodiscard]] containers::ArraySpan<const SourceRecord> GetResources() const noexcept;
        [[nodiscard]] containers::ArraySpan<const SourceRoot> GetRoots() const noexcept;
        [[nodiscard]] const SourceRecord* LookupResource(const filesystem::AbsolutePath& path) const noexcept;
        // Ambiguous asset IDs and outputs never select an arbitrary winner.
        [[nodiscard]] const SourceRecord* LookupAsset(const AssetId& id) const noexcept;
        [[nodiscard]] const SourceRecord* LookupOutput(resources::ResourceReference output) const noexcept;
        // Existing tool descriptors, not a second registry. Caller keeps tools
        // alive and registration stable through this exclusive-owner operation.
        // No descriptor/callback pointers are retained after return.
        [[nodiscard]] bool ClassifySources(containers::ArraySpan<const CompilerDescriptor> compilers) noexcept;
        // Explicit filtered traversal, like resource database filtering. No I/O,
        // allocations, metadata copies or query locks. Returns visited count.
        [[nodiscard]] u32 VisitResources(const SourceQuery& query, VisitSourceFunction visitor, void* userData = nullptr) const noexcept;

    private:
        containers::DynamicArray<SourceRoot> m_roots;
        containers::DynamicArray<SourceRecord> m_resources;
        containers::HashMap<containers::String, u32> m_paths;
        containers::HashMap<containers::String, u32> m_assets;
        containers::HashMap<resources::ResourceId, u32> m_outputs;
    };
} // namespace vanguard::assets
