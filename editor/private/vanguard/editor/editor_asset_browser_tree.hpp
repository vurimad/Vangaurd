#pragma once

#include <vanguard/asset_streaming/project_asset_catalog.hpp>
#include <vanguard/containers/containers.hpp>

#include <algorithm>
#include <cstring>

namespace vanguard::editor::detail
{
    inline constexpr u32 MaximumAssetBrowserTreeExpandedFolders = 8192u;
    inline constexpr u32 MaximumAssetBrowserTreeVisibleFolders = 65536u;

    struct AssetBrowserTreeExpandedLocation final
    {
        u32 root = 0u;
        u32 pathBytes = 0u;
        char path[resources::MaximumResourcePathBytes + 1u]{};
    };

    struct AssetBrowserTreeVisibleFolder final
    {
        asset_streaming::ProjectAssetCatalogFolderId folder;
        u32 root = 0u;
        u32 depth = 0u;
        u32 childFolderCount = 0u;
        bool expanded = false;
    };

    struct AssetBrowserTreeState final
    {
        explicit AssetBrowserTreeState(const red::memory::Pool& pool) noexcept : expanded(pool), visible(pool) {}

        containers::DynamicArray<AssetBrowserTreeExpandedLocation> expanded;
        containers::DynamicArray<AssetBrowserTreeVisibleFolder> visible;
        u64 visibleGeneration = 0u;
        bool dirty = true;
        bool initialized = false;
        bool truncated = false;
        bool expansionLimitReached = false;
        bool scrollToSelection = false;
        bool focusSelection = false;
    };

    [[nodiscard]] inline char FoldAssetBrowserTreeAscii(const char value) noexcept
    {
        return value >= 'A' && value <= 'Z' ? static_cast<char>(value + ('a' - 'A')) : value;
    }

    [[nodiscard]] inline i32 CompareAssetBrowserTreePath(const containers::StringView left,
                                                         const containers::StringView right) noexcept
    {
        const u32 common = std::min(left.Length(), right.Length());
        for (u32 index = 0u; index < common; ++index)
        {
            const char l = FoldAssetBrowserTreeAscii(left[index]);
            const char r = FoldAssetBrowserTreeAscii(right[index]);
            if (l < r)
                return -1;
            if (l > r)
                return 1;
        }
        if (left.Length() < right.Length())
            return -1;
        if (left.Length() > right.Length())
            return 1;
        return 0;
    }

    [[nodiscard]] inline i32 CompareAssetBrowserTreeLocation(const AssetBrowserTreeExpandedLocation& location,
                                                             const u32 root,
                                                             const containers::StringView path) noexcept
    {
        if (location.root < root)
            return -1;
        if (location.root > root)
            return 1;
        return CompareAssetBrowserTreePath(containers::StringView(location.path, location.pathBytes), path);
    }

    [[nodiscard]] inline u32 LowerBoundAssetBrowserTreeExpansion(const AssetBrowserTreeState& tree,
                                                                 const u32 root,
                                                                 const containers::StringView path,
                                                                 bool& found) noexcept
    {
        u32 lower = 0u;
        u32 upper = tree.expanded.Size();
        while (lower < upper)
        {
            const u32 middle = lower + (upper - lower) / 2u;
            const i32 order = CompareAssetBrowserTreeLocation(tree.expanded[middle], root, path);
            if (order < 0)
                lower = middle + 1u;
            else
                upper = middle;
        }
        found = lower < tree.expanded.Size() &&
                CompareAssetBrowserTreeLocation(tree.expanded[lower], root, path) == 0;
        return lower;
    }

    [[nodiscard]] inline bool IsAssetBrowserTreeFolderExpanded(const AssetBrowserTreeState& tree,
                                                               const u32 root,
                                                               const containers::StringView path) noexcept
    {
        bool found = false;
        static_cast<void>(LowerBoundAssetBrowserTreeExpansion(tree, root, path, found));
        return found;
    }

    [[nodiscard]] inline bool SetAssetBrowserTreeFolderExpanded(AssetBrowserTreeState& tree,
                                                                const u32 root,
                                                                const containers::StringView path,
                                                                const bool expanded) noexcept
    {
        if (path.Length() > resources::MaximumResourcePathBytes)
            return false;
        bool found = false;
        const u32 index = LowerBoundAssetBrowserTreeExpansion(tree, root, path, found);
        if (expanded == found)
            return true;
        if (!expanded)
        {
            static_cast<void>(tree.expanded.RemoveAt(index));
            tree.expansionLimitReached = false;
            tree.dirty = true;
            return true;
        }
        if (tree.expanded.Size() >= MaximumAssetBrowserTreeExpandedFolders)
        {
            tree.expansionLimitReached = true;
            return false;
        }

        AssetBrowserTreeExpandedLocation location;
        location.root = root;
        location.pathBytes = path.Length();
        if (!path.Empty())
            std::memcpy(location.path, path.Data(), path.Length());
        location.path[path.Length()] = '\0';
        if (!tree.expanded.InsertAt(index, location).IsSuccessful())
            return false;
        tree.expansionLimitReached = false;
        tree.dirty = true;
        return true;
    }

    inline void RevealAssetBrowserTreeLocation(AssetBrowserTreeState& tree, const u32 root,
                                               const containers::StringView path) noexcept
    {
        // Root and every ancestor must be expanded so navigation originating in
        // breadcrumbs/content/history can reveal the selected folder. The selected
        // folder itself is not expanded automatically.
        static_cast<void>(SetAssetBrowserTreeFolderExpanded(tree, root, {}, true));
        for (u32 index = 0u; index < path.Length(); ++index)
        {
            if (path[index] != '/')
                continue;
            static_cast<void>(SetAssetBrowserTreeFolderExpanded(
                tree, root, path.Slice(0u, index), true));
        }
        tree.scrollToSelection = true;
    }

    inline void PruneAssetBrowserTreeExpansions(AssetBrowserTreeState& tree,
                                                const asset_streaming::ProjectAssetCatalog& catalog) noexcept
    {
        for (u32 index = tree.expanded.Size(); index != 0u; --index)
        {
            const AssetBrowserTreeExpandedLocation& location = tree.expanded[index - 1u];
            if (location.root >= catalog.GetRootCount() ||
                !catalog.ResolveFolder(location.root,
                                       containers::StringView(location.path, location.pathBytes)).IsValid())
                static_cast<void>(tree.expanded.RemoveAt(index - 1u));
        }
        tree.dirty = true;
    }

    struct AssetBrowserTreeBuildContext final
    {
        AssetBrowserTreeState* tree = nullptr;
        const asset_streaming::ProjectAssetCatalog* catalog = nullptr;
        u32 depth = 0u;
        bool stopped = false;
    };

    [[nodiscard]] inline bool AppendAssetBrowserTreeFolder(AssetBrowserTreeState& tree,
                                                           const asset_streaming::ProjectAssetCatalog& catalog,
                                                           asset_streaming::ProjectAssetCatalogFolderId folder,
                                                           u32 depth) noexcept;

    inline bool VisitAssetBrowserTreeChild(const asset_streaming::ProjectAssetCatalogFolderInfo& folder,
                                           containers::StringView, void* userData) noexcept
    {
        auto& context = *static_cast<AssetBrowserTreeBuildContext*>(userData);
        if (context.tree == nullptr || context.catalog == nullptr || context.stopped)
            return false;
        if (!AppendAssetBrowserTreeFolder(*context.tree, *context.catalog, folder.id, context.depth))
        {
            context.stopped = true;
            return false;
        }
        return true;
    }

    [[nodiscard]] inline bool AppendAssetBrowserTreeFolder(AssetBrowserTreeState& tree,
                                                           const asset_streaming::ProjectAssetCatalog& catalog,
                                                           const asset_streaming::ProjectAssetCatalogFolderId folder,
                                                           const u32 depth) noexcept
    {
        if (tree.visible.Size() >= MaximumAssetBrowserTreeVisibleFolders)
        {
            tree.truncated = true;
            return false;
        }
        asset_streaming::ProjectAssetCatalogFolderInfo info;
        if (!catalog.GetFolderInfo(folder, info))
            return true;

        char path[resources::MaximumResourcePathBytes + 1u]{};
        u32 pathBytes = 0u;
        if (!catalog.CopyFolderRelativePath(folder, path, pathBytes))
            return true;
        const bool expanded = info.childFolderCount != 0u &&
                              IsAssetBrowserTreeFolderExpanded(
                                  tree, info.root, containers::StringView(path, pathBytes));
        tree.visible.PushBack({folder, info.root, depth, info.childFolderCount, expanded});
        if (!expanded || info.childFolderCount == 0u)
            return true;

        AssetBrowserTreeBuildContext children{&tree, &catalog, depth + 1u, false};
        static_cast<void>(catalog.VisitChildFolders(
            folder, 0u, info.childFolderCount, VisitAssetBrowserTreeChild, &children));
        return !children.stopped;
    }

    [[nodiscard]] inline bool RebuildAssetBrowserTreeVisible(AssetBrowserTreeState& tree,
                                                             const asset_streaming::ProjectAssetCatalog& catalog) noexcept
    {
        const asset_streaming::ProjectAssetCatalogStatus status = catalog.GetStatus();
        tree.visible.Clear();
        tree.truncated = false;
        for (u32 root = 0u; root < catalog.GetRootCount(); ++root)
        {
            const asset_streaming::ProjectAssetCatalogFolderId folder = catalog.GetRootFolder(root);
            if (!folder.IsValid())
                continue;
            if (!AppendAssetBrowserTreeFolder(tree, catalog, folder, 0u))
                break;
        }
        tree.visibleGeneration = status.sourceGeneration;
        tree.dirty = false;
        return !tree.truncated;
    }
} // namespace vanguard::editor::detail
