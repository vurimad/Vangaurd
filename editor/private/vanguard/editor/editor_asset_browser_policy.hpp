#pragma once

#include <vanguard/asset_streaming/project_asset_catalog.hpp>

#include <algorithm>
#include <cstring>

namespace vanguard::editor::detail
{
    inline constexpr float AssetBrowserDefaultNavigationSplitRatio = 0.28f;

    enum class AssetBrowserContentViewMode : u8
    {
        Grid,
        CompactGrid,
        Details
    };

    struct AssetBrowserContentViewState final
    {
        AssetBrowserContentViewMode mode = AssetBrowserContentViewMode::Grid;
        float thumbnailExtent = 112.0f;
        float compactThumbnailExtent = 64.0f;
        float navigationSplitRatio = AssetBrowserDefaultNavigationSplitRatio;
    };

    struct AssetBrowserContentRange final
    {
        u32 firstFolder = 0u;
        u32 folderCount = 0u;
        u32 firstAsset = 0u;
        u32 assetCount = 0u;
    };

    [[nodiscard]] inline AssetBrowserContentRange SplitAssetBrowserContentRange(
        const u32 firstEntry, const u32 endEntry, const u32 folderCount) noexcept
    {
        AssetBrowserContentRange result;
        if (endEntry <= firstEntry)
            return result;
        if (firstEntry < folderCount)
        {
            result.firstFolder = firstEntry;
            result.folderCount = std::min(endEntry, folderCount) - firstEntry;
        }
        if (endEntry > folderCount)
        {
            result.firstAsset = firstEntry > folderCount ? firstEntry - folderCount : 0u;
            result.assetCount = endEntry - folderCount - result.firstAsset;
        }
        return result;
    }

    struct AssetBrowserImportFilterDescriptor final
    {
        const char* label = nullptr;
        assets::SourceImportState state = assets::SourceImportState::NotClassified;
    };

    inline constexpr AssetBrowserImportFilterDescriptor AssetBrowserImportFilters[]{
        {"Imported", assets::SourceImportState::MetadataSelected},
        {"Candidate", assets::SourceImportState::Candidate},
        {"Unsupported", assets::SourceImportState::Unsupported},
        {"Invalid metadata", assets::SourceImportState::InvalidMetadata},
        {"Ambiguous", assets::SourceImportState::Ambiguous},
        {"Importer unavailable", assets::SourceImportState::ImporterUnavailable}};

    [[nodiscard]] inline constexpr u32 GetAssetBrowserImportFilterCount() noexcept
    {
        return static_cast<u32>(sizeof(AssetBrowserImportFilters) / sizeof(AssetBrowserImportFilters[0]));
    }

    [[nodiscard]] inline const char* GetAssetBrowserImportFilterLabel(const i32 filter) noexcept
    {
        return filter >= 0 && static_cast<u32>(filter) < GetAssetBrowserImportFilterCount()
                   ? AssetBrowserImportFilters[filter].label
                   : "All";
    }

    struct AssetBrowserBrowsePolicyState
    {
        // Durable navigation identity is root + folderPath. `folder` is only a
        // transient handle into the currently published catalog generation and
        // must be rebound after publication. UI code must navigate through the
        // helpers below rather than mutating these three fields independently.
        asset_streaming::ProjectAssetCatalogQueryResult query;
        asset_streaming::ProjectAssetCatalogFolderId folder;
        asset_streaming::ProjectAssetCatalogRowId selectedRow;
        assets::AssetId selectedAsset;
        u64 catalogGeneration = 0u;
        u32 root = 0u;
        char folderPath[resources::MaximumResourcePathBytes + 1u]{};
        char search[asset_streaming::MaximumProjectAssetCatalogSearchBytes]{};
        i32 importFilter = -1;
        bool selectedHasAsset = false;
        bool recursive = false;
        bool queryDirty = true;
        AssetBrowserContentViewState contentView;
    };

    enum class AssetBrowserNavigationResult : u8
    {
        Unchanged,
        Changed,
        InvalidTarget,
        LimitExceeded
    };

    [[nodiscard]] inline bool ComposeAssetBrowserChildFolderPath(
        containers::StringView parent, containers::StringView child,
        containers::ArraySpan<char> output) noexcept
    {
        if (output.Empty() || child.Empty())
            return false;
        const u32 separator = parent.Empty() ? 0u : 1u;
        if (parent.Length() + separator + child.Length() >= output.Count())
            return false;
        u32 write = 0u;
        if (!parent.Empty())
        {
            std::memcpy(output.Data(), parent.Data(), parent.Length());
            write = parent.Length();
            output[write++] = '/';
        }
        std::memcpy(output.Data() + write, child.Data(), child.Length());
        output[write + child.Length()] = '\0';
        return true;
    }

    [[nodiscard]] inline u32 CopyAssetBrowserParentFolderPath(
        containers::StringView path, containers::ArraySpan<char> output) noexcept
    {
        if (output.Empty())
            return 0u;
        u32 length = path.Length();
        while (length != 0u && path[length - 1u] == '/')
            --length;
        while (length != 0u && path[length - 1u] != '/')
            --length;
        if (length != 0u)
            --length;
        if (length >= output.Count())
            return 0u;
        if (length != 0u)
            std::memcpy(output.Data(), path.Data(), length);
        output[length] = '\0';
        return length;
    }

    [[nodiscard]] inline AssetBrowserNavigationResult NavigateAssetBrowserToFolder(
        AssetBrowserBrowsePolicyState& state, const asset_streaming::ProjectAssetCatalog& catalog,
        const u32 root, const containers::StringView relativePath) noexcept
    {
        if (root >= catalog.GetRootCount())
            return AssetBrowserNavigationResult::InvalidTarget;
        if (relativePath.Length() >= sizeof(state.folderPath))
            return AssetBrowserNavigationResult::LimitExceeded;
        const asset_streaming::ProjectAssetCatalogFolderId resolved = catalog.ResolveFolder(root, relativePath);
        if (!resolved.IsValid())
            return AssetBrowserNavigationResult::InvalidTarget;

        const bool unchanged = state.root == root &&
                               containers::StringView(state.folderPath) == relativePath;
        state.root = root;
        if (relativePath.Length() != 0u)
            std::memcpy(state.folderPath, relativePath.Data(), relativePath.Length());
        state.folderPath[relativePath.Length()] = '\0';
        state.folder = resolved;
        if (unchanged)
            return AssetBrowserNavigationResult::Unchanged;
        state.queryDirty = true;
        return AssetBrowserNavigationResult::Changed;
    }

    [[nodiscard]] inline AssetBrowserNavigationResult NavigateAssetBrowserToChildFolder(
        AssetBrowserBrowsePolicyState& state, const asset_streaming::ProjectAssetCatalog& catalog,
        const containers::StringView child) noexcept
    {
        char path[resources::MaximumResourcePathBytes + 1u]{};
        if (!ComposeAssetBrowserChildFolderPath(containers::StringView(state.folderPath), child,
                                                {path, static_cast<u32>(sizeof(path))}))
            return AssetBrowserNavigationResult::LimitExceeded;
        return NavigateAssetBrowserToFolder(state, catalog, state.root, containers::StringView(path));
    }

    [[nodiscard]] inline AssetBrowserNavigationResult NavigateAssetBrowserToParentFolder(
        AssetBrowserBrowsePolicyState& state, const asset_streaming::ProjectAssetCatalog& catalog) noexcept
    {
        if (state.folderPath[0] == '\0')
            return AssetBrowserNavigationResult::Unchanged;
        char parent[resources::MaximumResourcePathBytes + 1u]{};
        static_cast<void>(CopyAssetBrowserParentFolderPath(
            containers::StringView(state.folderPath), {parent, static_cast<u32>(sizeof(parent))}));
        return NavigateAssetBrowserToFolder(state, catalog, state.root, containers::StringView(parent));
    }

    enum class AssetBrowserSelectionRefreshPolicy : u8
    {
        None,
        Clear,
        Rebind
    };

    struct AssetBrowserCatalogSyncPolicyResult final
    {
        asset_streaming::ProjectAssetCatalogRowId rebindRow;
        AssetBrowserSelectionRefreshPolicy selection = AssetBrowserSelectionRefreshPolicy::None;
        bool ready = false;
    };

    [[nodiscard]] inline AssetBrowserCatalogSyncPolicyResult SyncAssetBrowserBrowsePolicy(
        AssetBrowserBrowsePolicyState& state, const asset_streaming::ProjectAssetCatalog& catalog) noexcept
    {
        AssetBrowserCatalogSyncPolicyResult result;
        const asset_streaming::ProjectAssetCatalogStatus status = catalog.GetStatus();
        if (status.sourceGeneration == 0u)
            return result;
        if (state.catalogGeneration == status.sourceGeneration && state.folder.IsValid())
        {
            result.ready = true;
            return result;
        }

        state.catalogGeneration = status.sourceGeneration;
        if (state.root >= catalog.GetRootCount())
        {
            state.root = 0u;
            state.folderPath[0] = '\0';
        }
        state.folder = catalog.ResolveFolder(state.root, state.folderPath);
        while (!state.folder.IsValid() && state.folderPath[0] != '\0')
        {
            char parent[resources::MaximumResourcePathBytes + 1u]{};
            const u32 parentBytes = CopyAssetBrowserParentFolderPath(
                containers::StringView(state.folderPath),
                {parent, static_cast<u32>(sizeof(parent))});
            if (parentBytes != 0u)
                std::memcpy(state.folderPath, parent, parentBytes);
            state.folderPath[parentBytes] = '\0';
            state.folder = catalog.ResolveFolder(state.root, state.folderPath);
        }
        if (!state.folder.IsValid())
        {
            state.folderPath[0] = '\0';
            state.folder = catalog.GetRootFolder(state.root);
        }
        state.queryDirty = true;

        if (state.selectedHasAsset && state.selectedAsset.IsValid())
        {
            const asset_streaming::ProjectAssetCatalogRowId selected = catalog.ResolveAsset(state.selectedAsset);
            const asset_streaming::ProjectAssetCatalogRow* const row = catalog.GetRow(selected);
            if (row != nullptr && row->asset == state.selectedAsset && row->hasAssetIdentity)
            {
                result.selection = AssetBrowserSelectionRefreshPolicy::Rebind;
                result.rebindRow = selected;
            }
            else
            {
                result.selection = AssetBrowserSelectionRefreshPolicy::Clear;
            }
        }
        else if (state.selectedRow.IsValid())
        {
            // Candidate/non-adopted rows intentionally have no stable selection identity.
            result.selection = AssetBrowserSelectionRefreshPolicy::Clear;
        }

        result.ready = state.folder.IsValid();
        return result;
    }

    [[nodiscard]] inline asset_streaming::ProjectAssetCatalogResult BeginAssetBrowserBrowseQuery(
        AssetBrowserBrowsePolicyState& state, const asset_streaming::ProjectAssetCatalog& catalog) noexcept
    {
        asset_streaming::ProjectAssetCatalogQuery query;
        query.folder = state.folder;
        query.text = containers::StringView(state.search);
        query.recursive = state.recursive;
        if (state.importFilter >= 0)
        {
            if (static_cast<u32>(state.importFilter) < GetAssetBrowserImportFilterCount())
            {
                query.filterImportState = true;
                query.importState = AssetBrowserImportFilters[state.importFilter].state;
            }
        }
        const asset_streaming::ProjectAssetCatalogResult result = catalog.BeginQuery(query, state.query);
        state.queryDirty = false;
        return result;
    }

    [[nodiscard]] inline bool FenceStaleAssetBrowserBrowseQuery(
        AssetBrowserBrowsePolicyState& state, const asset_streaming::ProjectAssetCatalogQueryStatus status) noexcept
    {
        if (status.state != asset_streaming::ProjectAssetCatalogQueryState::Stale)
            return false;
        state.queryDirty = true;
        return true;
    }

    [[nodiscard]] inline bool IsAssetBrowserMutationSelectionCurrent(
        const bool sourceGenerationCurrent, const u64 selectedAcceptedGeneration,
        const bool acceptedGenerationResolved, const u64 acceptedGeneration) noexcept
    {
        return sourceGenerationCurrent && selectedAcceptedGeneration != 0u && acceptedGenerationResolved &&
               acceptedGeneration == selectedAcceptedGeneration;
    }

    [[nodiscard]] inline bool CanRunAssetBrowserAdoptedOperation(
        const bool selectionCurrent, const bool selectedReadOnly,
        const bool requiresWritableSource, const bool mutationDomainReady) noexcept
    {
        return selectionCurrent && mutationDomainReady && (!requiresWritableSource || !selectedReadOnly);
    }
} // namespace vanguard::editor::detail
