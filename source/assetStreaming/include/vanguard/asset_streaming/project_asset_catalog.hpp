#pragma once

#include <vanguard/assets/source_database.hpp>
#include <vanguard/memory/memory.hpp>

namespace vanguard::asset_streaming
{
    inline constexpr u64 ProjectAssetCatalogHardPeakBytes = 128ull * 1024ull * 1024ull;
    inline constexpr u64 ProjectAssetCatalogSteadyStateTargetBytes = 64ull * 1024ull * 1024ull;
    inline constexpr u64 ProjectAssetCatalogQueryBudgetBytes = 16ull * 1024ull * 1024ull;
    inline constexpr u32 MaximumProjectAssetCatalogSearchBytes = 256u;

    struct ProjectAssetCatalogRowId final
    {
        u32 value = ~u32{0};

        [[nodiscard]] constexpr bool IsValid() const noexcept { return value < assets::SourceDatabase::MaximumSources; }
        [[nodiscard]] friend constexpr bool operator==(ProjectAssetCatalogRowId, ProjectAssetCatalogRowId) noexcept = default;
    };

    struct ProjectAssetCatalogFolderId final
    {
        u32 value = ~u32{0};

        [[nodiscard]] constexpr bool IsValid() const noexcept { return value != ~u32{0}; }
        [[nodiscard]] friend constexpr bool operator==(ProjectAssetCatalogFolderId, ProjectAssetCatalogFolderId) noexcept = default;
    };

    inline constexpr ProjectAssetCatalogRowId InvalidProjectAssetCatalogRowId{};
    inline constexpr ProjectAssetCatalogFolderId InvalidProjectAssetCatalogFolderId{};

    enum class ProjectAssetCatalogResult : u8
    {
        Success,
        Pending,
        InvalidArgument,
        InvalidSource,
        StaleSource,
        LimitExceeded,
        OutOfMemory
    };

    enum class ProjectAssetCatalogQueryState : u8
    {
        Idle,
        Running,
        Complete,
        Stale,
        Failed
    };

    enum class ProjectAssetCatalogRefreshState : u8
    {
        Idle,
        Counting,
        Capturing,
        Building
    };

    struct ProjectAssetCatalogRow final
    {
        assets::AssetId asset;
        resources::ResourcePath sourceIdentity;
        assets::SourceImporterId importer = assets::InvalidSourceImporterId;
        u32 nameOffset = 0;
        u32 nameLength = 0;
        ProjectAssetCatalogFolderId folder;
        u32 root = 0;
        u32 issues = assets::SourceIssueNone;
        u32 availableImporterVersion = 0;
        resources::ResourceTypeId sourceType = resources::InvalidResourceTypeId;
        resources::ResourceTypeId primaryOutputType = resources::InvalidResourceTypeId;
        assets::MetadataResult metadataResult = assets::MetadataResult::InvalidData;
        assets::SourceImportState importState = assets::SourceImportState::NotClassified;
        assets::SourceReferenceScanState referenceScanState = assets::SourceReferenceScanState::NotScanned;
        bool rootReadOnly = true;
        bool hasAssetIdentity = false;
    };

    struct ProjectAssetCatalogFolderInfo final
    {
        ProjectAssetCatalogFolderId id;
        ProjectAssetCatalogFolderId parent;
        u32 root = 0;
        u32 depth = 0;
        u32 childFolderCount = 0;
        u32 directRowCount = 0;
        u32 subtreeRowCount = 0;
    };

    struct ProjectAssetCatalogStatus final
    {
        ProjectAssetCatalogResult lastResult = ProjectAssetCatalogResult::InvalidSource;
        ProjectAssetCatalogRefreshState refreshState = ProjectAssetCatalogRefreshState::Idle;
        u64 sourceGeneration = 0;
        u64 pendingSourceGeneration = 0;
        u64 allocatedBytes = 0;
        u64 lastRebuildPeakBytes = 0;
        u64 pendingAllocatedBytes = 0;
        u64 queryAllocatedBytes = 0;
        u64 lastBuildNanoseconds = 0;
        u32 rowCount = 0;
        u32 folderCount = 0;
        u32 rootCount = 0;
        u32 pendingRows = 0;
        u32 pendingRowsProcessed = 0;
        u32 pendingFolders = 0;
        u32 pendingFoldersProcessed = 0;
        u32 activeQueryCount = 0;
        bool steadyStateTargetExceeded = false;
    };

    struct ProjectAssetCatalogQuery final
    {
        ProjectAssetCatalogFolderId folder;
        containers::StringView text;
        u32 requiredIssues = 0;
        u32 excludedIssues = 0;
        resources::ResourceTypeId sourceType = resources::InvalidResourceTypeId;
        resources::ResourceTypeId primaryOutputType = resources::InvalidResourceTypeId;
        assets::SourceImportState importState = assets::SourceImportState::NotClassified;
        assets::SourceReferenceScanState referenceScanState = assets::SourceReferenceScanState::NotScanned;
        bool recursive = false;
        bool filterImportState = false;
        bool filterReferenceScanState = false;
    };

    struct ProjectAssetCatalogQueryStatus final
    {
        ProjectAssetCatalogQueryState state = ProjectAssetCatalogQueryState::Idle;
        u64 sourceGeneration = 0;
        u64 scannedPathBytes = 0;
        u64 totalProcessingNanoseconds = 0;
        u64 lastStepNanoseconds = 0;
        u32 candidates = 0;
        u32 scanned = 0;
        u32 matches = 0;
        u32 allocatedBytes = 0;
    };

    class ProjectAssetCatalogQueryResult final
    {
    public:
        ProjectAssetCatalogQueryResult() noexcept;
        ~ProjectAssetCatalogQueryResult();
        ProjectAssetCatalogQueryResult(const ProjectAssetCatalogQueryResult&) = delete;
        ProjectAssetCatalogQueryResult& operator=(const ProjectAssetCatalogQueryResult&) = delete;

        void Reset() noexcept;
        [[nodiscard]] ProjectAssetCatalogQueryStatus GetStatus() const noexcept;

    private:
        friend class ProjectAssetCatalog;

        memory::MemoryBlock m_matches;
        u64 m_accountedMatchBytes = 0;
        const void* m_catalogIdentity = nullptr;
        u64 m_catalogIdentityCookie = 0;
        ProjectAssetCatalogQueryStatus m_status;
        ProjectAssetCatalogFolderId m_folder;
        u32 m_candidateOffset = 0;
        u32 m_cursor = 0;
        u32 m_matchBitCapacity = 0;
        u32 m_matchWordCount = 0;
        u32 m_matchBlockCount = 0;
        u32 m_requiredIssues = 0;
        u32 m_excludedIssues = 0;
        resources::ResourceTypeId m_sourceType = resources::InvalidResourceTypeId;
        resources::ResourceTypeId m_primaryOutputType = resources::InvalidResourceTypeId;
        assets::SourceImportState m_importState = assets::SourceImportState::NotClassified;
        assets::SourceReferenceScanState m_referenceScanState = assets::SourceReferenceScanState::NotScanned;
        u16 m_textBytes = 0;
        bool m_passthrough = false;
        bool m_recursive = false;
        bool m_filterImportState = false;
        bool m_filterReferenceScanState = false;
        bool m_countedActive = false;
        char m_text[MaximumProjectAssetCatalogSearchBytes]{};
        u16 m_textPrefix[MaximumProjectAssetCatalogSearchBytes]{};
    };

    using VisitProjectAssetCatalogRow = bool (*)(ProjectAssetCatalogRowId id, const ProjectAssetCatalogRow& row,
                                                void* userData) noexcept;
    using VisitProjectAssetCatalogFolder = bool (*)(const ProjectAssetCatalogFolderInfo& folder,
                                                   containers::StringView name, void* userData) noexcept;

    class ProjectAssetCatalog final
    {
    public:
        // Thread/lifetime contract: public Refresh/read/query/enumeration calls are
        // owner-thread only. Returned row pointers and StringViews are borrowed
        // from the current immutable publication and remain valid only until the
        // next successful Refresh publication or Reset. Query results must be
        // consumed on that same owner thread; future asynchronous consumers need
        // an explicit snapshot lease rather than borrowing these views.
        ProjectAssetCatalog() noexcept;
        ~ProjectAssetCatalog();
        ProjectAssetCatalog(const ProjectAssetCatalog&) = delete;
        ProjectAssetCatalog& operator=(const ProjectAssetCatalog&) = delete;

        // Owner-thread refresh. Borrowed SourceDatabase records are touched only
        // during bounded Count/Capture phases. Expensive sort/folder/index work
        // runs only on the owned capture after that boundary. A previous complete
        // snapshot stays published until the new generation is complete.
        [[nodiscard]] ProjectAssetCatalogResult Refresh(const assets::SourceDatabase& sources,
                                                        u32 maximumSourceRows = 16384u,
                                                        u32 maximumPathBytes = 4u * 1024u * 1024u) noexcept;
        void Reset() noexcept;

        [[nodiscard]] ProjectAssetCatalogStatus GetStatus() const noexcept;
        [[nodiscard]] const ProjectAssetCatalogRow* GetRow(ProjectAssetCatalogRowId id) const noexcept;
        [[nodiscard]] containers::StringView GetName(ProjectAssetCatalogRowId id) const noexcept;
        // Reconstructs the root-relative source path from the shared folder tree
        // and row basename. The output is all-or-nothing and never retained.
        u32 CopyRelativePath(ProjectAssetCatalogRowId id, containers::ArraySpan<char> output) const noexcept;
        // O(N) generation-rebase lookup by durable authored identity. This is
        // intentionally not a steady-state secondary index: browser instances
        // call it only when a published catalog generation changes.
        [[nodiscard]] ProjectAssetCatalogRowId ResolveAsset(assets::AssetId asset) const noexcept;
        [[nodiscard]] ProjectAssetCatalogRowId ResolveRow(u32 root,
                                                          containers::StringView relativePath) const noexcept;
        [[nodiscard]] bool GetFolderInfo(ProjectAssetCatalogFolderId id, ProjectAssetCatalogFolderInfo& info) const noexcept;
        [[nodiscard]] containers::StringView GetFolderName(ProjectAssetCatalogFolderId id) const noexcept;
        // Copies a generation-local folder handle into its durable root-relative
        // path identity. Folder IDs must never be persisted across catalog
        // publications; navigation/tree/history state should persist root + path
        // and resolve the handle again in the current generation.
        [[nodiscard]] bool CopyFolderRelativePath(ProjectAssetCatalogFolderId id,
                                                  containers::ArraySpan<char> output,
                                                  u32& writtenBytes) const noexcept;
        [[nodiscard]] ProjectAssetCatalogFolderId GetRootFolder(u32 root) const noexcept;
        [[nodiscard]] ProjectAssetCatalogFolderId ResolveFolder(u32 root,
                                                               containers::StringView relativeFolder) const noexcept;
        [[nodiscard]] u32 GetRootCount() const noexcept;

        u32 VisitChildFolders(ProjectAssetCatalogFolderId parent, u32 firstChild, u32 maximumFolders,
                              VisitProjectAssetCatalogFolder visitor, void* userData = nullptr) const noexcept;
        u32 VisitFolderRows(ProjectAssetCatalogFolderId folder, VisitProjectAssetCatalogRow visitor,
                            void* userData = nullptr) const noexcept;

        [[nodiscard]] ProjectAssetCatalogResult BeginQuery(const ProjectAssetCatalogQuery& query,
                                                          ProjectAssetCatalogQueryResult& result) const noexcept;
        [[nodiscard]] ProjectAssetCatalogResult StepQuery(ProjectAssetCatalogQueryResult& result,
                                                         u32 maximumRows,
                                                         u32 maximumPathBytes) const noexcept;
        u32 VisitQueryMatches(const ProjectAssetCatalogQueryResult& result, u32 firstMatch, u32 maximumMatches,
                              VisitProjectAssetCatalogRow visitor, void* userData = nullptr) const noexcept;

    private:
        struct Impl;
        Impl* m_impl = nullptr;
    };
} // namespace vanguard::asset_streaming
