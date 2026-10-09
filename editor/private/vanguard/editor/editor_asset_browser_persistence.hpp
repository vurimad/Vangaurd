#pragma once

#include <vanguard/editor/editor_asset_browser_policy.hpp>
#include <vanguard/editor/editor_asset_browser_tree.hpp>
#include <vanguard/editor/editor_ui.hpp>

#include <algorithm>
#include <cstring>

namespace vanguard::editor::detail
{
    inline constexpr u32 AssetBrowserPanelStateVersion = 2u;
    inline constexpr u32 AssetBrowserPanelStateVersionV1 = 1u;
    inline constexpr u32 AssetBrowserPresentationStateMagicV1 = 0x31504241u; // ABP1
    inline constexpr u32 AssetBrowserContentStateMagicV1 = 0x31434241u; // ABC1
    inline constexpr u32 AssetBrowserPresentationStateMagic = 0x32504241u; // ABP2
    inline constexpr u32 AssetBrowserContentStateMagic = 0x32434241u; // ABC2
    inline constexpr u16 MaximumPersistedAssetBrowserTreeExpansions = 60u;
    inline constexpr float AssetBrowserGridMinimumThumbnailExtent = 48.0f;
    inline constexpr float AssetBrowserGridMaximumThumbnailExtent = 160.0f;
    inline constexpr float AssetBrowserCompactGridMinimumThumbnailExtent = 32.0f;
    inline constexpr float AssetBrowserCompactGridMaximumThumbnailExtent = 96.0f;
    inline constexpr float AssetBrowserMinimumNavigationSplitRatio = 0.15f;
    inline constexpr float AssetBrowserMaximumNavigationSplitRatio = 0.65f;
    inline constexpr u32 AssetBrowserContentViewModeCount = 3u;

    enum class AssetBrowserSelectionIntent : u8
    {
        None,
        Folder,
        Asset
    };

    enum class AssetBrowserPersistedImportFilter : u8
    {
        All,
        Imported,
        Candidate,
        Unsupported,
        InvalidMetadata,
        Ambiguous,
        ImporterUnavailable
    };

    [[nodiscard]] inline AssetBrowserPersistedImportFilter EncodeAssetBrowserImportFilter(const i32 filter) noexcept
    {
        if (filter < 0 || static_cast<u32>(filter) >= GetAssetBrowserImportFilterCount())
            return AssetBrowserPersistedImportFilter::All;
        switch (AssetBrowserImportFilters[filter].state)
        {
        case assets::SourceImportState::MetadataSelected:
            return AssetBrowserPersistedImportFilter::Imported;
        case assets::SourceImportState::Candidate:
            return AssetBrowserPersistedImportFilter::Candidate;
        case assets::SourceImportState::Unsupported:
            return AssetBrowserPersistedImportFilter::Unsupported;
        case assets::SourceImportState::InvalidMetadata:
            return AssetBrowserPersistedImportFilter::InvalidMetadata;
        case assets::SourceImportState::Ambiguous:
            return AssetBrowserPersistedImportFilter::Ambiguous;
        case assets::SourceImportState::ImporterUnavailable:
            return AssetBrowserPersistedImportFilter::ImporterUnavailable;
        default:
            return AssetBrowserPersistedImportFilter::All;
        }
    }

    [[nodiscard]] inline bool DecodeAssetBrowserImportFilter(const u8 encoded, i32& filter) noexcept
    {
        if (encoded > static_cast<u8>(AssetBrowserPersistedImportFilter::ImporterUnavailable))
            return false;
        const auto tag = static_cast<AssetBrowserPersistedImportFilter>(encoded);
        if (tag == AssetBrowserPersistedImportFilter::All)
        {
            filter = -1;
            return true;
        }
        assets::SourceImportState wanted = assets::SourceImportState::NotClassified;
        switch (tag)
        {
        case AssetBrowserPersistedImportFilter::Imported:
            wanted = assets::SourceImportState::MetadataSelected;
            break;
        case AssetBrowserPersistedImportFilter::Candidate:
            wanted = assets::SourceImportState::Candidate;
            break;
        case AssetBrowserPersistedImportFilter::Unsupported:
            wanted = assets::SourceImportState::Unsupported;
            break;
        case AssetBrowserPersistedImportFilter::InvalidMetadata:
            wanted = assets::SourceImportState::InvalidMetadata;
            break;
        case AssetBrowserPersistedImportFilter::Ambiguous:
            wanted = assets::SourceImportState::Ambiguous;
            break;
        case AssetBrowserPersistedImportFilter::ImporterUnavailable:
            wanted = assets::SourceImportState::ImporterUnavailable;
            break;
        case AssetBrowserPersistedImportFilter::All:
            break;
        }
        for (u32 index = 0u; index < GetAssetBrowserImportFilterCount(); ++index)
            if (AssetBrowserImportFilters[index].state == wanted)
            {
                filter = static_cast<i32>(index);
                return true;
            }
        return false;
    }

    struct AssetBrowserStateWriter final
    {
        u8* destination = nullptr;
        u32 capacity = 0u;
        u32 position = 0u;

        [[nodiscard]] bool WriteU8(const u8 value) noexcept { return WriteBytes(&value, sizeof(value)); }

        [[nodiscard]] bool WriteU16(const u16 value) noexcept
        {
            const u8 bytes[]{static_cast<u8>(value), static_cast<u8>(value >> 8u)};
            return WriteBytes(bytes, sizeof(bytes));
        }

        [[nodiscard]] bool WriteU32(const u32 value) noexcept
        {
            const u8 bytes[]{static_cast<u8>(value), static_cast<u8>(value >> 8u),
                             static_cast<u8>(value >> 16u), static_cast<u8>(value >> 24u)};
            return WriteBytes(bytes, sizeof(bytes));
        }

        [[nodiscard]] bool WriteF32(const float value) noexcept
        {
            u32 bits = 0u;
            static_assert(sizeof(bits) == sizeof(value));
            std::memcpy(&bits, &value, sizeof(bits));
            return WriteU32(bits);
        }

        [[nodiscard]] bool WriteBytes(const void* const source, const u32 byteCount) noexcept
        {
            if (position > capacity || byteCount > capacity - position ||
                (byteCount != 0u && (source == nullptr || destination == nullptr)))
                return false;
            if (byteCount != 0u)
                std::memcpy(destination + position, source, byteCount);
            position += byteCount;
            return true;
        }

        [[nodiscard]] bool WriteString(const containers::StringView text) noexcept
        {
            if (text.Length() > 0xffffu ||
                (text.Length() != 0u && std::memchr(text.Data(), '\0', text.Length()) != nullptr))
                return false;
            return WriteU16(static_cast<u16>(text.Length())) && WriteBytes(text.Data(), text.Length());
        }
    };

    struct AssetBrowserStateReader final
    {
        containers::ArraySpan<const u8> source;
        u32 position = 0u;

        [[nodiscard]] bool ReadU8(u8& value) noexcept { return ReadBytes(&value, sizeof(value)); }

        [[nodiscard]] bool ReadU16(u16& value) noexcept
        {
            u8 bytes[2]{};
            if (!ReadBytes(bytes, sizeof(bytes)))
                return false;
            value = static_cast<u16>(bytes[0]) | static_cast<u16>(static_cast<u16>(bytes[1]) << 8u);
            return true;
        }

        [[nodiscard]] bool ReadU32(u32& value) noexcept
        {
            u8 bytes[4]{};
            if (!ReadBytes(bytes, sizeof(bytes)))
                return false;
            value = static_cast<u32>(bytes[0]) | (static_cast<u32>(bytes[1]) << 8u) |
                    (static_cast<u32>(bytes[2]) << 16u) | (static_cast<u32>(bytes[3]) << 24u);
            return true;
        }

        [[nodiscard]] bool ReadF32(float& value) noexcept
        {
            u32 bits = 0u;
            if (!ReadU32(bits))
                return false;
            static_assert(sizeof(bits) == sizeof(value));
            std::memcpy(&value, &bits, sizeof(value));
            return true;
        }

        [[nodiscard]] bool ReadBytes(void* const destination, const u32 byteCount) noexcept
        {
            if (position > source.Count() || byteCount > source.Count() - position ||
                (byteCount != 0u && destination == nullptr))
                return false;
            if (byteCount != 0u)
                std::memcpy(destination, source.Data() + position, byteCount);
            position += byteCount;
            return true;
        }

        [[nodiscard]] bool ReadString(char* const destination, const u32 capacity, u32& byteCount) noexcept
        {
            u16 encodedBytes = 0u;
            if (!ReadU16(encodedBytes) || capacity == 0u || encodedBytes >= capacity ||
                !ReadBytes(destination, encodedBytes) ||
                (encodedBytes != 0u && std::memchr(destination, '\0', encodedBytes) != nullptr))
                return false;
            byteCount = encodedBytes;
            destination[byteCount] = '\0';
            return true;
        }

        [[nodiscard]] bool AtEnd() const noexcept { return position == source.Count(); }
    };

    [[nodiscard]] inline bool ValidAssetBrowserContentViewMode(const AssetBrowserContentViewMode mode) noexcept
    {
        return mode == AssetBrowserContentViewMode::Grid || mode == AssetBrowserContentViewMode::CompactGrid ||
               mode == AssetBrowserContentViewMode::Details;
    }

    [[nodiscard]] inline u32 AssetBrowserContentViewModeIndex(const AssetBrowserContentViewMode mode) noexcept
    {
        return ValidAssetBrowserContentViewMode(mode) ? static_cast<u32>(mode) : 0u;
    }

    [[nodiscard]] inline bool ValidAssetBrowserPersistenceString(const containers::StringView text,
                                                                const u32 maximumBytes) noexcept
    {
        return text.Length() <= maximumBytes &&
               (text.Empty() || std::memchr(text.Data(), '\0', text.Length()) == nullptr);
    }

    [[nodiscard]] inline EditorPanelStateResult CaptureAssetBrowserPresentationPersistence(
        const AssetBrowserContentViewState& state, void* const destination, const u32 capacity,
        u32& byteCount) noexcept
    {
        byteCount = 0u;
        if (!ValidAssetBrowserContentViewMode(state.mode) ||
            !(state.thumbnailExtent >= AssetBrowserGridMinimumThumbnailExtent &&
              state.thumbnailExtent <= AssetBrowserGridMaximumThumbnailExtent) ||
            !(state.compactThumbnailExtent >= AssetBrowserCompactGridMinimumThumbnailExtent &&
              state.compactThumbnailExtent <= AssetBrowserCompactGridMaximumThumbnailExtent) ||
            !(state.navigationSplitRatio >= AssetBrowserMinimumNavigationSplitRatio &&
              state.navigationSplitRatio <= AssetBrowserMaximumNavigationSplitRatio))
            return EditorPanelStateResult::InvalidData;
        AssetBrowserStateWriter writer{static_cast<u8*>(destination), capacity};
        if (!writer.WriteU32(AssetBrowserPresentationStateMagic) ||
            !writer.WriteU8(static_cast<u8>(state.mode)) || !writer.WriteF32(state.thumbnailExtent) ||
            !writer.WriteF32(state.compactThumbnailExtent) || !writer.WriteF32(state.navigationSplitRatio))
            return EditorPanelStateResult::CapacityExceeded;
        byteCount = writer.position;
        return EditorPanelStateResult::Success;
    }

    [[nodiscard]] inline bool DecodeAssetBrowserPresentationPersistenceVersion(
        const containers::ArraySpan<const u8> bytes, const u32 expectedMagic, const bool includesSplitRatio,
        AssetBrowserContentViewState& state) noexcept
    {
        AssetBrowserStateReader reader{bytes};
        u32 magic = 0u;
        u8 modeRaw = 0u;
        AssetBrowserContentViewState decoded;
        if (!reader.ReadU32(magic) || magic != expectedMagic || !reader.ReadU8(modeRaw) ||
            !reader.ReadF32(decoded.thumbnailExtent) || !reader.ReadF32(decoded.compactThumbnailExtent))
            return false;
        decoded.navigationSplitRatio = AssetBrowserDefaultNavigationSplitRatio;
        if (includesSplitRatio && !reader.ReadF32(decoded.navigationSplitRatio))
            return false;
        if (!reader.AtEnd())
            return false;
        decoded.mode = static_cast<AssetBrowserContentViewMode>(modeRaw);
        if (!ValidAssetBrowserContentViewMode(decoded.mode) ||
            !(decoded.thumbnailExtent >= AssetBrowserGridMinimumThumbnailExtent &&
              decoded.thumbnailExtent <= AssetBrowserGridMaximumThumbnailExtent) ||
            !(decoded.compactThumbnailExtent >= AssetBrowserCompactGridMinimumThumbnailExtent &&
              decoded.compactThumbnailExtent <= AssetBrowserCompactGridMaximumThumbnailExtent) ||
            !(decoded.navigationSplitRatio >= AssetBrowserMinimumNavigationSplitRatio &&
              decoded.navigationSplitRatio <= AssetBrowserMaximumNavigationSplitRatio))
            return false;
        state = decoded;
        return true;
    }

    [[nodiscard]] inline bool DecodeAssetBrowserPresentationPersistence(
        const containers::ArraySpan<const u8> bytes, AssetBrowserContentViewState& state) noexcept
    {
        return DecodeAssetBrowserPresentationPersistenceVersion(
            bytes, AssetBrowserPresentationStateMagic, true, state);
    }

    struct AssetBrowserContentPersistenceView final
    {
        u32 root = 0u;
        containers::StringView folderPath;
        containers::StringView search;
        i32 importFilter = -1;
        bool recursive = false;
        AssetBrowserSelectionIntent selection = AssetBrowserSelectionIntent::None;
        u32 selectedContentFolderRoot = 0u;
        containers::StringView selectedContentFolderPath;
        assets::AssetId selectedAsset;
        float treeScrollFraction = 0.0f;
        float contentScrollFractions[AssetBrowserContentViewModeCount]{};
        bool treePersistenceTruncated = false;
        containers::ArraySpan<const AssetBrowserTreeExpandedLocation> expansions;
    };

    struct AssetBrowserDecodedContentPersistenceState final
    {
        u32 root = 0u;
        char folderPath[resources::MaximumResourcePathBytes + 1u]{};
        u32 folderPathBytes = 0u;
        char search[asset_streaming::MaximumProjectAssetCatalogSearchBytes]{};
        u32 searchBytes = 0u;
        i32 importFilter = -1;
        bool recursive = false;
        AssetBrowserSelectionIntent selection = AssetBrowserSelectionIntent::None;
        u32 selectedContentFolderRoot = 0u;
        char selectedContentFolderPath[resources::MaximumResourcePathBytes + 1u]{};
        u32 selectedContentFolderPathBytes = 0u;
        assets::AssetId selectedAsset;
        float treeScrollFraction = 0.0f;
        float contentScrollFractions[AssetBrowserContentViewModeCount]{};
        bool treePersistenceTruncated = false;
        u16 expansionCount = 0u;
        AssetBrowserTreeExpandedLocation expansions[MaximumPersistedAssetBrowserTreeExpansions]{};
    };

    [[nodiscard]] inline EditorPanelStateResult CaptureAssetBrowserContentPersistence(
        const AssetBrowserContentPersistenceView& state, void* const destination, const u32 capacity,
        u32& byteCount) noexcept
    {
        byteCount = 0u;
        if (!ValidAssetBrowserPersistenceString(state.folderPath, resources::MaximumResourcePathBytes) ||
            !ValidAssetBrowserPersistenceString(
                state.search, asset_streaming::MaximumProjectAssetCatalogSearchBytes - 1u) ||
            !ValidAssetBrowserPersistenceString(
                state.selectedContentFolderPath, resources::MaximumResourcePathBytes) ||
            static_cast<u8>(state.selection) > static_cast<u8>(AssetBrowserSelectionIntent::Asset))
            return EditorPanelStateResult::InvalidData;

        AssetBrowserSelectionIntent selection = state.selection;
        if (selection == AssetBrowserSelectionIntent::Asset && !state.selectedAsset.IsValid())
            selection = AssetBrowserSelectionIntent::None;

        AssetBrowserStateWriter writer{static_cast<u8*>(destination), capacity};
        if (!writer.WriteU32(AssetBrowserContentStateMagic) || !writer.WriteU32(state.root) ||
            !writer.WriteString(state.folderPath) || !writer.WriteString(state.search) ||
            !writer.WriteU8(static_cast<u8>(EncodeAssetBrowserImportFilter(state.importFilter))) ||
            !writer.WriteU8(state.recursive ? 1u : 0u) || !writer.WriteU8(static_cast<u8>(selection)))
            return EditorPanelStateResult::CapacityExceeded;

        if (selection == AssetBrowserSelectionIntent::Folder)
        {
            if (!writer.WriteU32(state.selectedContentFolderRoot) ||
                !writer.WriteString(state.selectedContentFolderPath))
                return EditorPanelStateResult::CapacityExceeded;
        }
        else if (selection == AssetBrowserSelectionIntent::Asset &&
                 !writer.WriteBytes(state.selectedAsset.bytes, sizeof(state.selectedAsset.bytes)))
            return EditorPanelStateResult::CapacityExceeded;

        if (!(state.treeScrollFraction >= 0.0f && state.treeScrollFraction <= 1.0f))
            return EditorPanelStateResult::InvalidData;
        for (u32 index = 0u; index < AssetBrowserContentViewModeCount; ++index)
            if (!(state.contentScrollFractions[index] >= 0.0f && state.contentScrollFractions[index] <= 1.0f))
                return EditorPanelStateResult::InvalidData;
        if (!writer.WriteF32(state.treeScrollFraction))
            return EditorPanelStateResult::CapacityExceeded;
        for (u32 index = 0u; index < AssetBrowserContentViewModeCount; ++index)
            if (!writer.WriteF32(state.contentScrollFractions[index]))
                return EditorPanelStateResult::CapacityExceeded;

        const u16 persistedExpansionCount = static_cast<u16>(
            std::min<u32>(state.expansions.Count(), MaximumPersistedAssetBrowserTreeExpansions));
        const bool expansionsTruncated =
            state.treePersistenceTruncated || persistedExpansionCount < state.expansions.Count();
        if (!writer.WriteU8(expansionsTruncated ? 1u : 0u) || !writer.WriteU16(persistedExpansionCount))
            return EditorPanelStateResult::CapacityExceeded;

        AssetBrowserTreeExpandedLocation previous;
        bool hasPrevious = false;
        for (u32 index = 0u; index < persistedExpansionCount; ++index)
        {
            const AssetBrowserTreeExpandedLocation& expanded = state.expansions[index];
            if (expanded.pathBytes > resources::MaximumResourcePathBytes ||
                std::memchr(expanded.path, '\0', expanded.pathBytes) != nullptr ||
                (hasPrevious && CompareAssetBrowserTreeLocation(
                                    previous, expanded.root,
                                    containers::StringView(expanded.path, expanded.pathBytes)) >= 0))
                return EditorPanelStateResult::InvalidData;
            if (!writer.WriteU32(expanded.root) ||
                !writer.WriteString(containers::StringView(expanded.path, expanded.pathBytes)))
                return EditorPanelStateResult::CapacityExceeded;
            previous = expanded;
            hasPrevious = true;
        }

        byteCount = writer.position;
        return EditorPanelStateResult::Success;
    }

    [[nodiscard]] inline bool DecodeAssetBrowserContentPersistenceVersion(
        const containers::ArraySpan<const u8> bytes, const u32 expectedMagic, const bool includesScrollIntent,
        AssetBrowserDecodedContentPersistenceState& state) noexcept
    {
        AssetBrowserDecodedContentPersistenceState decoded;
        AssetBrowserStateReader reader{bytes};
        u32 magic = 0u;
        u8 importFilterTag = 0u;
        u8 recursive = 0u;
        u8 selectionRaw = 0u;
        if (!reader.ReadU32(magic) || magic != expectedMagic || !reader.ReadU32(decoded.root) ||
            !reader.ReadString(decoded.folderPath, sizeof(decoded.folderPath), decoded.folderPathBytes) ||
            !reader.ReadString(decoded.search, sizeof(decoded.search), decoded.searchBytes) ||
            !reader.ReadU8(importFilterTag) || !DecodeAssetBrowserImportFilter(importFilterTag, decoded.importFilter) ||
            !reader.ReadU8(recursive) || recursive > 1u || !reader.ReadU8(selectionRaw) ||
            selectionRaw > static_cast<u8>(AssetBrowserSelectionIntent::Asset))
            return false;
        decoded.recursive = recursive != 0u;
        decoded.selection = static_cast<AssetBrowserSelectionIntent>(selectionRaw);

        if (decoded.selection == AssetBrowserSelectionIntent::Folder)
        {
            if (!reader.ReadU32(decoded.selectedContentFolderRoot) ||
                !reader.ReadString(decoded.selectedContentFolderPath, sizeof(decoded.selectedContentFolderPath),
                                   decoded.selectedContentFolderPathBytes))
                return false;
        }
        else if (decoded.selection == AssetBrowserSelectionIntent::Asset)
        {
            if (!reader.ReadBytes(decoded.selectedAsset.bytes, sizeof(decoded.selectedAsset.bytes)) ||
                !decoded.selectedAsset.IsValid())
                return false;
        }

        if (includesScrollIntent)
        {
            if (!reader.ReadF32(decoded.treeScrollFraction) ||
                !(decoded.treeScrollFraction >= 0.0f && decoded.treeScrollFraction <= 1.0f))
                return false;
            for (u32 index = 0u; index < AssetBrowserContentViewModeCount; ++index)
                if (!reader.ReadF32(decoded.contentScrollFractions[index]) ||
                    !(decoded.contentScrollFractions[index] >= 0.0f && decoded.contentScrollFractions[index] <= 1.0f))
                    return false;
        }

        u8 expansionFlags = 0u;
        if (!reader.ReadU8(expansionFlags) || (expansionFlags & ~1u) != 0u ||
            !reader.ReadU16(decoded.expansionCount) ||
            decoded.expansionCount > MaximumPersistedAssetBrowserTreeExpansions)
            return false;
        decoded.treePersistenceTruncated = (expansionFlags & 1u) != 0u;
        for (u32 index = 0u; index < decoded.expansionCount; ++index)
        {
            AssetBrowserTreeExpandedLocation& current = decoded.expansions[index];
            if (!reader.ReadU32(current.root) ||
                !reader.ReadString(current.path, sizeof(current.path), current.pathBytes))
                return false;
            if (index != 0u && CompareAssetBrowserTreeLocation(
                                   decoded.expansions[index - 1u], current.root,
                                   containers::StringView(current.path, current.pathBytes)) >= 0)
                return false;
        }
        if (!reader.AtEnd())
            return false;
        state = decoded;
        return true;
    }

    [[nodiscard]] inline bool DecodeAssetBrowserContentPersistence(
        const containers::ArraySpan<const u8> bytes, AssetBrowserDecodedContentPersistenceState& state) noexcept
    {
        return DecodeAssetBrowserContentPersistenceVersion(bytes, AssetBrowserContentStateMagic, true, state);
    }

    [[nodiscard]] inline EditorPanelStateResult MigrateAssetBrowserPersistenceV1(
        const EditorPanelStateScope scope, const containers::ArraySpan<const u8> source,
        void* const destination, const u32 capacity, u32& byteCount) noexcept
    {
        byteCount = 0u;
        if (scope == EditorPanelStateScope::Presentation)
        {
            AssetBrowserContentViewState decoded;
            if (!DecodeAssetBrowserPresentationPersistenceVersion(
                    source, AssetBrowserPresentationStateMagicV1, false, decoded))
                return EditorPanelStateResult::InvalidData;
            return CaptureAssetBrowserPresentationPersistence(decoded, destination, capacity, byteCount);
        }
        if (scope != EditorPanelStateScope::Content)
            return EditorPanelStateResult::InvalidData;

        AssetBrowserDecodedContentPersistenceState decoded;
        if (!DecodeAssetBrowserContentPersistenceVersion(
                source, AssetBrowserContentStateMagicV1, false, decoded))
            return EditorPanelStateResult::InvalidData;

        AssetBrowserContentPersistenceView migrated;
        migrated.root = decoded.root;
        migrated.folderPath = containers::StringView(decoded.folderPath, decoded.folderPathBytes);
        migrated.search = containers::StringView(decoded.search, decoded.searchBytes);
        migrated.importFilter = decoded.importFilter;
        migrated.recursive = decoded.recursive;
        migrated.selection = decoded.selection;
        migrated.selectedContentFolderRoot = decoded.selectedContentFolderRoot;
        migrated.selectedContentFolderPath = containers::StringView(
            decoded.selectedContentFolderPath, decoded.selectedContentFolderPathBytes);
        migrated.selectedAsset = decoded.selectedAsset;
        migrated.treePersistenceTruncated = decoded.treePersistenceTruncated;
        migrated.expansions = containers::ArraySpan<const AssetBrowserTreeExpandedLocation>(
            decoded.expansions, decoded.expansionCount);
        return CaptureAssetBrowserContentPersistence(migrated, destination, capacity, byteCount);
    }
}
