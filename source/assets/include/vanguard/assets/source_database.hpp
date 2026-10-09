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

    // Physical source-root directory inventory, excluding the configured root
    // itself. Kept separate from SourceRecord because a directory is not an
    // authored source and has no importer/identity/metadata semantics.
    struct SourceFolder
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
        OutputIdentityConflict = 1u << 5u,
        SourceIdentityConflict = 1u << 6u
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

    enum class SourceReferenceScanState : u8
    {
        NotScanned,
        Scanned,
        ImporterUnavailable,
        InputUnavailable,
        ScannerUnavailable,
        ScanFailed
    };

    // An authored link from an existing source asset to a typed runtime output.
    // The target is allowed to be missing from the current source catalog.
    struct AssetReference
    {
        AssetId from;
        resources::ResourceReference to;
        resources::DependencyKind kind = resources::DependencyKind::Required;
        AssetReferenceLocation location = AssetReferenceLocation::Primary;
        bool inPlace = false;
    };

    struct SourceRecord
    {
        filesystem::AbsolutePath path;
        resources::ResourcePath sourceIdentity;
        u32 root = 0;
        u32 issues = SourceIssueNone;
        MetadataResult metadataResult = MetadataResult::InvalidData;
        AssetMetadata metadata;
        SourceImportState importState = SourceImportState::NotClassified;
        SourceImporterId importer = InvalidSourceImporterId;
        u32 availableImporterVersion = 0;
        resources::ResourceTypeId sourceType = resources::InvalidResourceTypeId;
        resources::ResourceTypeId primaryOutputType = resources::InvalidResourceTypeId;
        SourceReferenceScanState referenceScanState = SourceReferenceScanState::NotScanned;
    };

    struct SourceDataView
    {
        containers::ArraySpan<const u8> content;
        containers::ArraySpan<const u8> metadata;
        TargetPlatform target = TargetPlatform::WindowsD3D12;

        [[nodiscard]] bool IsValid() const noexcept
        {
            return (content.Empty() || content.Data() != nullptr) && (metadata.Empty() || metadata.Data() != nullptr) && target < TargetPlatform::Count;
        }
    };

    // Data is borrowed only for the immediate scanner call. Spans must remain
    // valid until the provider's next invocation, or until ScanReferences
    // returns after the final invocation.
    using LoadSourceDataFunction = bool (*)(const SourceRecord& source, SourceDataView& data, void* userData) noexcept;

    // Project-source importers classify authored files and translate one
    // declared output into the low-level BuildRequest consumed by BuildSystem.
    // They are deliberately separate from artifact compilers: one authored
    // document may create requests for several output types and compilers.
    using RecognizeSourceFunction = bool (*)(containers::StringView sourcePath, void* userData) noexcept;
    // Returned request spans are borrowed for immediate validation/copy by the
    // caller; custom importers retain any generated intermediate bytes.
    using CreateSourceBuildRequestFunction = bool (*)(const SourceRecord& source, const SourceDataView& data,
                                                      resources::ResourceReference output, BuildRequest& request, void* userData) noexcept;
    using ScanBuildRequestReferencesFunction = bool (*)(const BuildRequest& request, AssetReferenceCollector& references, void* userData) noexcept;
    using ScanSourceReferencesFunction = bool (*)(const SourceRecord& source, const SourceDataView& data,
                                                   AssetReferenceCollector& references, void* userData) noexcept;
    struct AssetReferenceReplacement
    {
        resources::ResourceReference previous;
        resources::ResourceReference replacement;
    };
    // Format-owned rewrite. The callback returns complete replacement source
    // bytes and metadata; the transaction owner validates and stages them. It
    // must not write files, mutate the catalog, or retain borrowed input spans.
    using RewriteSourceReferencesFunction = bool (*)(const SourceRecord& source, const SourceDataView& data,
                                                      containers::ArraySpan<const AssetReferenceReplacement> replacements,
                                                      containers::DynamicArray<u8>& rewrittenSource, AssetMetadata& rewrittenMetadata,
                                                      void* userData) noexcept;
    // Format-owned relocation for metadata/settings that embed the source
    // identity. The new identity is the canonical root-relative source path.
    // Like reference rewriting, this callback is pure and returns complete
    // replacement bytes for the transaction owner.
    using RelocateSourceFunction = bool (*)(const SourceRecord& source, const SourceDataView& data,
                                            containers::StringView newSourceIdentity,
                                            containers::DynamicArray<u8>& relocatedSource, AssetMetadata& relocatedMetadata,
                                            void* userData) noexcept;
    // Explicit relocation policy for formats whose source bytes and importer
    // settings do not encode their root-relative path.
    [[nodiscard]] bool RelocateSourceUnchanged(const SourceRecord& source, const SourceDataView& data,
                                               containers::StringView newSourceIdentity,
                                               containers::DynamicArray<u8>& relocatedSource, AssetMetadata& relocatedMetadata,
                                               void* userData) noexcept;

    struct SourceImportDescription
    {
        SourceImportDescription() noexcept
            : settings(memory::pools::Assets::GetInstance()), outputs(memory::pools::Assets::GetInstance()) {}

        u32 settingsVersion = 0;
        containers::DynamicArray<u8> settings;
        containers::DynamicArray<AssetOutput> outputs;
    };

    using CreateSourceImportDescriptionFunction =
        bool (*)(const SourceRecord& candidate, SourceImportDescription& output, void* userData) noexcept;
    using ValidateSourceSettingsFunction =
        bool (*)(u32 settingsVersion, containers::ArraySpan<const u8> settings, void* userData) noexcept;

    struct SourceImporterDescriptor
    {
        SourceImporterId id = InvalidSourceImporterId;
        const char* name = nullptr;
        u32 version = 0;
        resources::ResourceTypeId sourceType = resources::InvalidResourceTypeId;
        resources::ResourceTypeId primaryOutputType = resources::InvalidResourceTypeId;
        RecognizeSourceFunction recognizeSource = nullptr;
        CreateSourceBuildRequestFunction createBuildRequest = nullptr;
        ScanBuildRequestReferencesFunction scanBuildReferences = nullptr;
        ScanSourceReferencesFunction scanSourceReferences = nullptr;
        RewriteSourceReferencesFunction rewriteSourceReferences = nullptr;
        RelocateSourceFunction relocateSource = nullptr;
        void* userData = nullptr;
        CreateSourceImportDescriptionFunction createImportDescription = nullptr;
        ValidateSourceSettingsFunction validateSettings = nullptr;

        [[nodiscard]] bool IsValid() const noexcept
        {
            const bool direct = sourceType != resources::InvalidResourceTypeId;
            return id != InvalidSourceImporterId && name != nullptr && name[0] != '\0' && version != 0 &&
                   primaryOutputType != resources::InvalidResourceTypeId && (direct || createBuildRequest != nullptr);
        }
    };

    [[nodiscard]] SourceImporterDescriptor MakeDirectSourceImporterDescriptor(const CompilerDescriptor& compiler,
                                                                               RecognizeSourceFunction recognizeSource,
                                                                               ScanBuildRequestReferencesFunction scanReferences = nullptr,
                                                                               RewriteSourceReferencesFunction rewriteReferences = nullptr,
                                                                               RelocateSourceFunction relocateSource = nullptr,
                                                                               CreateSourceImportDescriptionFunction createImportDescription = nullptr,
                                                                               ValidateSourceSettingsFunction validateSettings = nullptr) noexcept;
    [[nodiscard]] bool CreateSourceBuildRequest(const SourceImporterDescriptor& importer, const SourceRecord& source,
                                                const SourceDataView& data, resources::ResourceReference output,
                                                BuildRequest& request) noexcept;
    // Resolves a declared output only when exactly one output has the requested
    // type. Multiple same-type outputs are intentionally ambiguous; declaration
    // order is never used as primary-output authority.
    [[nodiscard]] bool ResolveUniqueAssetOutputOfType(const AssetMetadata& metadata,
                                                      resources::ResourceTypeId type,
                                                      resources::ResourceReference& output,
                                                      bool& ambiguous) noexcept;

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
        bool filterReferenceScanState = false;
        SourceReferenceScanState referenceScanState = SourceReferenceScanState::NotScanned;
    };

    // Returning false stops traversal. Records are borrowed; do not mutate the
    // database, start a rescan, or retain pointers past its next update.
    using VisitSourceFunction = bool (*)(const SourceRecord& record, void* userData) noexcept;

    enum class SourceDatabaseResult : u8
    {
        Success,
        InvalidInventory,
        InvalidReferences,
        LimitExceeded,
        ScanFailed
    };

    class SourceDatabase final
    {
    public:
        static constexpr u32 MaximumSources = 262144;
        static constexpr u32 MaximumFolders = MaximumSources * 2u;
        static constexpr u32 MaximumRoots = 128;
        static constexpr u32 MaximumAssetReferences = 4194304;

        SourceDatabase() noexcept;
        SourceDatabase(const SourceDatabase&) = delete;
        SourceDatabase& operator=(const SourceDatabase&) = delete;

        // Exclusive-owner constant-time handoff used when a complete catalog is
        // prepared away from the owner thread. Borrowed rows/spans are invalidated
        // exactly like a successful Rebuild/Rescan.
        void Swap(SourceDatabase& other) noexcept;

        // Exclusive owner operation: callers must finish readers/build-request
        // resolution before rebuilding. Does no cooking, identity assignment or writes.
        // Inventory failure leaves the previous database unchanged; per-file
        // metadata errors become visible records instead of dropping the source.
        [[nodiscard]] SourceDatabaseResult Rebuild(
            filesystem::Manager& files, containers::ArraySpan<const SourceRoot> roots,
            containers::ArraySpan<const SourceFile> sources,
            containers::ArraySpan<const SourceFolder> folders = {}) noexcept;
        // Same exclusive-owner contract as Rebuild. All roots must scan
        // successfully before replacing the catalog. No source writes.
        [[nodiscard]] SourceDatabaseResult Rescan(
            filesystem::Manager& files, containers::ArraySpan<const SourceRoot> roots,
            filesystem::ScanResult* scanFailure = nullptr,
            containers::ArraySpan<const filesystem::AbsolutePath> excludedFiles = {}) noexcept;
        [[nodiscard]] containers::ArraySpan<const SourceRecord> GetResources() const noexcept;
        [[nodiscard]] containers::ArraySpan<const SourceFolder> GetFolders() const noexcept;
        [[nodiscard]] containers::ArraySpan<const SourceRoot> GetRoots() const noexcept;
        [[nodiscard]] const SourceRecord* LookupResource(const filesystem::AbsolutePath& path) const noexcept;
        [[nodiscard]] const SourceRecord* LookupSource(resources::ResourcePath identity) const noexcept;
        // Ambiguous asset IDs and outputs never select an arbitrary winner.
        [[nodiscard]] const SourceRecord* LookupAsset(const AssetId& id) const noexcept;
        [[nodiscard]] const SourceRecord* LookupOutput(resources::ResourceReference output) const noexcept;
        [[nodiscard]] bool ContainsAssetIdentity(const AssetId& id) const noexcept;
        [[nodiscard]] bool ContainsOutputPath(resources::ResourcePath path) const noexcept;
        [[nodiscard]] u64 GetGeneration() const noexcept;
        // Replaces the complete authored-reference index. Sources must exist in
        // this catalog; targets may be absent. Failure preserves the old index.
        // Duplicate links are removed. Returned spans remain borrowed until the
        // next successful catalog rebuild or reference replacement.
        [[nodiscard]] SourceDatabaseResult SetReferences(containers::ArraySpan<const AssetReference> references) noexcept;
        [[nodiscard]] containers::ArraySpan<const AssetReference> GetForwardReferences(const AssetId& source) const noexcept;
        [[nodiscard]] containers::ArraySpan<const AssetReference> GetBackwardReferences(resources::ResourceReference target) const noexcept;
        [[nodiscard]] containers::ArraySpan<const AssetReference> GetAllForwardReferences() const noexcept;
        [[nodiscard]] containers::ArraySpan<const AssetReference> GetAllBackwardReferences() const noexcept;
        // Full synchronous scan. Existing links are retained for a source when
        // its importer, input or scanner fails; referenceScanState exposes that
        // failure. Catastrophic validation/allocation failure changes nothing.
        [[nodiscard]] SourceDatabaseResult ScanReferences(containers::ArraySpan<const SourceImporterDescriptor> importers,
                                                          LoadSourceDataFunction load, void* userData = nullptr) noexcept;
        // Caller keeps importer tools alive and registration stable through
        // this exclusive-owner operation.
        // No descriptor/callback pointers are retained after return.
        [[nodiscard]] bool ClassifySources(containers::ArraySpan<const SourceImporterDescriptor> importers) noexcept;
        // Explicit filtered traversal, like resource database filtering. No I/O,
        // allocations, metadata copies or query locks. Returns visited count.
        [[nodiscard]] u32 VisitResources(const SourceQuery& query, VisitSourceFunction visitor, void* userData = nullptr) const noexcept;

    private:
        [[nodiscard]] SourceDatabaseResult SetReferencesInternal(containers::ArraySpan<const AssetReference> references) noexcept;
        void AdvanceGeneration() noexcept;

        containers::DynamicArray<SourceRoot> m_roots;
        containers::DynamicArray<SourceFolder> m_folders;
        containers::DynamicArray<SourceRecord> m_resources;
        containers::HashMap<containers::String, u32> m_paths;
        containers::HashMap<resources::ResourceId, u32> m_sourcePaths;
        containers::HashMap<AssetId, u32> m_assets;
        containers::HashMap<resources::ResourceId, u32> m_outputs;
        containers::DynamicArray<AssetReference> m_forwardReferences;
        containers::DynamicArray<AssetReference> m_backwardReferences;
        u64 m_generation = 1;
    };
} // namespace vanguard::assets
