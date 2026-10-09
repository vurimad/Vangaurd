#include <vanguard/asset_streaming/project_asset_catalog.hpp>
#include <vanguard/jobs/jobs.hpp>
#include <vanguard/system/time.hpp>

#include <algorithm>
#include <atomic>
#include <bit>
#include <cstring>
#include <limits>
#include <new>
#include <utility>

namespace
{
    namespace ctr = vanguard::containers;

    constexpr vanguard::u32 MaximumFolderDepth =
        static_cast<vanguard::u32>(vanguard::resources::MaximumResourcePathBytes / 2u) + 1u;

    [[nodiscard]] constexpr vanguard::usize AlignUp(const vanguard::usize value,
                                                    const vanguard::usize alignment) noexcept
    {
        return (value + alignment - 1u) & ~(alignment - 1u);
    }

    [[nodiscard]] bool AddSize(vanguard::usize& value, const vanguard::usize amount) noexcept
    {
        if (amount > std::numeric_limits<vanguard::usize>::max() - value)
            return false;
        value += amount;
        return true;
    }

    template <typename T>
    [[nodiscard]] bool AddArray(vanguard::usize& value, const vanguard::u32 count) noexcept
    {
        value = AlignUp(value, alignof(T));
        return count == 0u || AddSize(value, static_cast<vanguard::usize>(count) * sizeof(T));
    }

    [[nodiscard]] bool IsSeparator(const char value) noexcept
    {
        return value == '/' || value == '\\';
    }

    [[nodiscard]] char FoldAscii(const char value) noexcept
    {
        const unsigned char byte = static_cast<unsigned char>(value);
        return static_cast<char>(byte >= 'A' && byte <= 'Z' ? byte + ('a' - 'A') : byte);
    }

    [[nodiscard]] char CanonicalSearchByte(const char value) noexcept
    {
        return IsSeparator(value) ? '/' : FoldAscii(value);
    }

    [[nodiscard]] int CompareIgnoreCase(const ctr::StringView left, const ctr::StringView right) noexcept
    {
        const vanguard::u32 common = std::min(left.Length(), right.Length());
        for (vanguard::u32 index = 0u; index < common; ++index)
        {
            const char a = CanonicalSearchByte(left[index]);
            const char b = CanonicalSearchByte(right[index]);
            if (a < b)
                return -1;
            if (a > b)
                return 1;
        }
        if (left.Length() < right.Length())
            return -1;
        if (left.Length() > right.Length())
            return 1;
        return 0;
    }

    [[nodiscard]] bool EqualIgnoreCase(const ctr::StringView left, const ctr::StringView right) noexcept
    {
        return left.Length() == right.Length() && CompareIgnoreCase(left, right) == 0;
    }

    [[nodiscard]] bool ContainsCompiled(const ctr::StringView value, const char* const needle,
                                        const vanguard::u16* const prefix,
                                        const vanguard::u32 needleBytes) noexcept
    {
        if (needleBytes == 0u)
            return true;
        if (needle == nullptr || prefix == nullptr || value.Length() < needleBytes)
            return false;
        vanguard::u32 matched = 0u;
        for (vanguard::u32 index = 0u; index < value.Length(); ++index)
        {
            const char valueByte = CanonicalSearchByte(value[index]);
            while (matched != 0u && valueByte != needle[matched])
                matched = prefix[matched - 1u];
            if (valueByte == needle[matched])
                ++matched;
            if (matched == needleBytes)
                return true;
        }
        return false;
    }

    [[nodiscard]] vanguard::u32 FileNameStart(const ctr::StringView path) noexcept
    {
        vanguard::u32 start = 0u;
        for (vanguard::u32 index = 0u; index < path.Length(); ++index)
            if (IsSeparator(path[index]))
                start = index + 1u;
        return start;
    }

    struct SegmentList final
    {
        vanguard::u16 starts[MaximumFolderDepth]{};
        vanguard::u16 lengths[MaximumFolderDepth]{};
        vanguard::u32 count = 0u;
    };

    [[nodiscard]] bool ParseFolderSegments(const ctr::StringView path, const vanguard::u32 parentBytes,
                                           SegmentList& segments) noexcept
    {
        segments = {};
        vanguard::u32 start = 0u;
        for (vanguard::u32 index = 0u; index < parentBytes; ++index)
        {
            if (!IsSeparator(path[index]))
                continue;
            if (index > start)
            {
                const vanguard::u32 length = index - start;
                if (segments.count >= MaximumFolderDepth || start > std::numeric_limits<vanguard::u16>::max() ||
                    length > std::numeric_limits<vanguard::u16>::max())
                    return false;
                segments.starts[segments.count] = static_cast<vanguard::u16>(start);
                segments.lengths[segments.count] = static_cast<vanguard::u16>(length);
                ++segments.count;
            }
            start = index + 1u;
        }
        return true;
    }

    [[nodiscard]] vanguard::u32 CommonSegments(const ctr::StringView left, const SegmentList& leftSegments,
                                               const ctr::StringView right, const SegmentList& rightSegments) noexcept
    {
        const vanguard::u32 count = std::min(leftSegments.count, rightSegments.count);
        vanguard::u32 common = 0u;
        for (; common < count; ++common)
        {
            const vanguard::u16 leftLength = leftSegments.lengths[common];
            if (leftLength != rightSegments.lengths[common])
                break;
            if (!EqualIgnoreCase(left.Slice(leftSegments.starts[common], leftSegments.starts[common] + leftLength),
                                 right.Slice(rightSegments.starts[common], rightSegments.starts[common] + leftLength)))
                break;
        }
        return common;
    }
} // namespace

namespace vanguard::asset_streaming
{
    namespace
    {
        struct CatalogFolder final
        {
            ProjectAssetCatalogFolderId parent;
            u32 nameOffset = 0u;
            u32 nameLength = 0u;
            u32 root = 0u;
            u32 depth = 0u;
            u32 childStart = 0u;
            u32 childCount = 0u;
            u32 directStart = 0u;
            u32 directCount = 0u;
            u32 subtreeStart = 0u;
            u32 subtreeCount = 0u;
        };

        struct CatalogSnapshot final
        {
            memory::MemoryBlock block;
            ProjectAssetCatalogRow* rows = nullptr;
            CatalogFolder* folders = nullptr;
            u8* strings = nullptr;
            u32* sortedRows = nullptr;
            u32* directRows = nullptr;
            ProjectAssetCatalogFolderId* childFolders = nullptr;
            ProjectAssetCatalogFolderId* rootFolders = nullptr;
            u32 rowCount = 0u;
            u32 folderCount = 0u;
            u32 rootCount = 0u;
            u32 stringBytes = 0u;
            u64 generation = 0u;

            ~CatalogSnapshot() { Release(); }
            CatalogSnapshot() noexcept = default;
            CatalogSnapshot(const CatalogSnapshot&) = delete;
            CatalogSnapshot& operator=(const CatalogSnapshot&) = delete;

            void Release() noexcept
            {
                if (block)
                    memory::Free(block);
                rows = nullptr;
                folders = nullptr;
                strings = nullptr;
                sortedRows = nullptr;
                directRows = nullptr;
                childFolders = nullptr;
                rootFolders = nullptr;
                rowCount = 0u;
                folderCount = 0u;
                rootCount = 0u;
                stringBytes = 0u;
                generation = 0u;
            }

            void Swap(CatalogSnapshot& other) noexcept
            {
                std::swap(block, other.block);
                std::swap(rows, other.rows);
                std::swap(folders, other.folders);
                std::swap(strings, other.strings);
                std::swap(sortedRows, other.sortedRows);
                std::swap(directRows, other.directRows);
                std::swap(childFolders, other.childFolders);
                std::swap(rootFolders, other.rootFolders);
                std::swap(rowCount, other.rowCount);
                std::swap(folderCount, other.folderCount);
                std::swap(rootCount, other.rootCount);
                std::swap(stringBytes, other.stringBytes);
                std::swap(generation, other.generation);
            }

            [[nodiscard]] containers::StringView Text(const u32 offset, const u32 length) const noexcept
            {
                if (strings == nullptr || offset > stringBytes || length > stringBytes - offset)
                    return {};
                return containers::StringView(reinterpret_cast<const char*>(strings + offset), length);
            }
        };

        struct CatalogCaptureRow final
        {
            ProjectAssetCatalogRow row;
            u32 pathOffset = 0u;
            u32 pathLength = 0u;
            u32 nameStart = 0u;
        };

        struct CatalogCaptureFolder final
        {
            u32 root = 0u;
            u32 pathOffset = 0u;
            u32 pathLength = 0u;
        };

        struct CatalogCapture final
        {
            memory::MemoryBlock block;
            CatalogCaptureRow* rows = nullptr;
            CatalogCaptureFolder* folders = nullptr;
            u8* strings = nullptr;
            u32* order = nullptr;
            u32 rowCount = 0u;
            u32 folderCount = 0u;
            u32 rootCount = 0u;
            u32 stringBytes = 0u;
            u64 generation = 0u;

            ~CatalogCapture() { Release(); }
            CatalogCapture() noexcept = default;
            CatalogCapture(const CatalogCapture&) = delete;
            CatalogCapture& operator=(const CatalogCapture&) = delete;

            void Release() noexcept
            {
                if (block)
                    memory::Free(block);
                rows = nullptr;
                folders = nullptr;
                strings = nullptr;
                order = nullptr;
                rowCount = 0u;
                folderCount = 0u;
                rootCount = 0u;
                stringBytes = 0u;
                generation = 0u;
            }

            [[nodiscard]] containers::StringView Text(const u32 offset, const u32 length) const noexcept
            {
                if (strings == nullptr || offset > stringBytes || length > stringBytes - offset)
                    return {};
                return containers::StringView(reinterpret_cast<const char*>(strings + offset), length);
            }
        };

        struct SnapshotLayout final
        {
            usize rows = 0u;
            usize folders = 0u;
            usize strings = 0u;
            usize sortedRows = 0u;
            usize directRows = 0u;
            usize childFolders = 0u;
            usize rootFolders = 0u;
            usize total = 0u;
        };

        struct CaptureLayout final
        {
            usize rows = 0u;
            usize folders = 0u;
            usize strings = 0u;
            usize order = 0u;
            usize total = 0u;
        };

        [[nodiscard]] bool MakeCaptureLayout(const u32 rowCount, const u32 folderCount, const u32 stringBytes,
                                             CaptureLayout& layout) noexcept
        {
            if (folderCount > std::numeric_limits<u32>::max() - rowCount)
                return false;
            usize size = 0u;
            layout.rows = AlignUp(size, alignof(CatalogCaptureRow));
            if (!AddArray<CatalogCaptureRow>(size, rowCount))
                return false;
            layout.folders = AlignUp(size, alignof(CatalogCaptureFolder));
            if (!AddArray<CatalogCaptureFolder>(size, folderCount))
                return false;
            layout.strings = size;
            if (!AddSize(size, stringBytes))
                return false;
            layout.order = AlignUp(size, alignof(u32));
            if (!AddArray<u32>(size, rowCount + folderCount))
                return false;
            layout.total = AlignUp(size, 16u);
            return true;
        }

        [[nodiscard]] bool BindCapture(CatalogCapture& capture, const CaptureLayout& layout,
                                       const u32 rowCount, const u32 folderCount, const u32 rootCount,
                                       const u32 stringBytes, const u64 generation) noexcept
        {
            if (layout.total != 0u)
            {
                capture.block = memory::Allocate(memory::PoolId::Assets, layout.total, 16u);
                if (!capture.block)
                    return false;
                auto* const bytes = static_cast<u8*>(capture.block.address);
                capture.rows = reinterpret_cast<CatalogCaptureRow*>(bytes + layout.rows);
                capture.folders = reinterpret_cast<CatalogCaptureFolder*>(bytes + layout.folders);
                capture.strings = bytes + layout.strings;
                capture.order = reinterpret_cast<u32*>(bytes + layout.order);
                std::memset(capture.block.address, 0, layout.total);
            }
            capture.rowCount = rowCount;
            capture.folderCount = folderCount;
            capture.rootCount = rootCount;
            capture.stringBytes = stringBytes;
            capture.generation = generation;
            return true;
        }

        [[nodiscard]] bool MakeLayout(const u32 rowCount, const u32 folderCount, const u32 stringBytes,
                                      const u32 rootCount, SnapshotLayout& layout) noexcept
        {
            usize size = 0u;
            layout.rows = AlignUp(size, alignof(ProjectAssetCatalogRow));
            if (!AddArray<ProjectAssetCatalogRow>(size, rowCount))
                return false;
            layout.folders = AlignUp(size, alignof(CatalogFolder));
            if (!AddArray<CatalogFolder>(size, folderCount))
                return false;
            layout.strings = size;
            if (!AddSize(size, stringBytes))
                return false;
            layout.sortedRows = AlignUp(size, alignof(u32));
            if (!AddArray<u32>(size, rowCount))
                return false;
            layout.directRows = AlignUp(size, alignof(u32));
            if (!AddArray<u32>(size, rowCount))
                return false;
            layout.childFolders = AlignUp(size, alignof(ProjectAssetCatalogFolderId));
            if (!AddArray<ProjectAssetCatalogFolderId>(size, folderCount))
                return false;
            layout.rootFolders = AlignUp(size, alignof(ProjectAssetCatalogFolderId));
            if (!AddArray<ProjectAssetCatalogFolderId>(size, rootCount))
                return false;
            layout.total = AlignUp(size, 16u);
            return true;
        }

        [[nodiscard]] bool BindSnapshot(CatalogSnapshot& snapshot, const SnapshotLayout& layout,
                                        const u32 rowCount, const u32 folderCount, const u32 stringBytes,
                                        const u32 rootCount) noexcept
        {
            snapshot.block = memory::Allocate(memory::PoolId::Assets, layout.total, 16u);
            if (!snapshot.block)
                return false;
            auto* const bytes = static_cast<u8*>(snapshot.block.address);
            snapshot.rows = reinterpret_cast<ProjectAssetCatalogRow*>(bytes + layout.rows);
            snapshot.folders = reinterpret_cast<CatalogFolder*>(bytes + layout.folders);
            snapshot.strings = bytes + layout.strings;
            snapshot.sortedRows = reinterpret_cast<u32*>(bytes + layout.sortedRows);
            snapshot.directRows = reinterpret_cast<u32*>(bytes + layout.directRows);
            snapshot.childFolders = reinterpret_cast<ProjectAssetCatalogFolderId*>(bytes + layout.childFolders);
            snapshot.rootFolders = reinterpret_cast<ProjectAssetCatalogFolderId*>(bytes + layout.rootFolders);
            snapshot.rowCount = rowCount;
            snapshot.folderCount = folderCount;
            snapshot.rootCount = rootCount;
            snapshot.stringBytes = stringBytes;
            if (layout.total != 0u)
                std::memset(snapshot.block.address, 0, layout.total);
            return true;
        }

        [[nodiscard]] bool PeakFits(const CatalogSnapshot& current, const usize stagedBytes,
                                    const usize transientBytes) noexcept
        {
            const u64 currentBytes = current.block ? current.block.size : 0u;
            if (currentBytes > ProjectAssetCatalogHardPeakBytes || stagedBytes > ProjectAssetCatalogHardPeakBytes ||
                transientBytes > ProjectAssetCatalogHardPeakBytes)
                return false;
            const u64 available = ProjectAssetCatalogHardPeakBytes - ProjectAssetCatalogQueryBudgetBytes;
            return currentBytes + static_cast<u64>(stagedBytes) + static_cast<u64>(transientBytes) <= available;
        }

        std::atomic<u64> g_projectAssetCatalogQueryBytes{0u};
        std::atomic<u32> g_projectAssetCatalogActiveQueries{0u};
        std::atomic<u64> g_projectAssetCatalogNextIdentity{1u};

        [[nodiscard]] u64 AcquireCatalogIdentity() noexcept
        {
            u64 identity = g_projectAssetCatalogNextIdentity.fetch_add(1u, std::memory_order_relaxed);
            if (identity != 0u)
                return identity;
            identity = g_projectAssetCatalogNextIdentity.fetch_add(1u, std::memory_order_relaxed);
            return identity != 0u ? identity : 1u;
        }

        [[nodiscard]] u64 CatalogRetryDeadline() noexcept
        {
            const u64 frequency = system::GetMonotonicFrequency();
            const u64 now = frequency != 0u ? system::GetMonotonicTicks() : 0u;
            if (frequency == 0u || now == 0u)
                return 0u;
            return now > std::numeric_limits<u64>::max() - frequency
                       ? std::numeric_limits<u64>::max()
                       : now + frequency;
        }

        [[nodiscard]] u64 ElapsedNanoseconds(const u64 begin, const u64 end) noexcept
        {
            if (begin == 0u || end < begin)
                return 0u;
            const u64 frequency = system::GetMonotonicFrequency();
            if (frequency == 0u)
                return 0u;
            const u64 ticks = end - begin;
            constexpr u64 NanosecondsPerSecond = 1'000'000'000ull;
            return (ticks / frequency) * NanosecondsPerSecond +
                   ((ticks % frequency) * NanosecondsPerSecond) / frequency;
        }

        [[nodiscard]] bool ReserveQueryBytes(const u64 bytes) noexcept
        {
            if (bytes == 0u)
                return true;
            u64 current = g_projectAssetCatalogQueryBytes.load(std::memory_order_relaxed);
            for (;;)
            {
                if (current > ProjectAssetCatalogQueryBudgetBytes ||
                    bytes > ProjectAssetCatalogQueryBudgetBytes - current)
                    return false;
                if (g_projectAssetCatalogQueryBytes.compare_exchange_weak(
                        current, current + bytes, std::memory_order_acq_rel, std::memory_order_relaxed))
                    return true;
            }
        }

        void ReleaseQueryBytes(const u64 bytes) noexcept
        {
            if (bytes != 0u)
                static_cast<void>(g_projectAssetCatalogQueryBytes.fetch_sub(bytes, std::memory_order_acq_rel));
        }

        [[nodiscard]] containers::StringView RelativePath(const assets::SourceRecord& source,
                                                          const containers::ArraySpan<const assets::SourceRoot> roots) noexcept
        {
            if (source.root >= roots.Size())
                return {};
            const containers::StringView root = roots[source.root].directory.AsStringView();
            const containers::StringView path = source.path.AsStringView();
            if (path.Length() <= root.Length() || !path.StartsWithIgnoreCase(root))
                return {};
            return path.Slice(root.Length(), path.Length());
        }

        [[nodiscard]] containers::StringView RelativePath(const assets::SourceFolder& folder,
                                                          const containers::ArraySpan<const assets::SourceRoot> roots) noexcept
        {
            if (folder.root >= roots.Size())
                return {};
            const containers::StringView root = roots[folder.root].directory.AsStringView();
            const containers::StringView path = folder.path.AsStringView();
            if (path.Length() <= root.Length() || !path.StartsWithIgnoreCase(root))
                return {};
            return path.Slice(root.Length(), path.Length());
        }

        [[nodiscard]] bool SameFolderName(const CatalogSnapshot& snapshot, const CatalogFolder& folder,
                                          const containers::StringView value) noexcept
        {
            return folder.nameLength == value.Length() &&
                   EqualIgnoreCase(snapshot.Text(folder.nameOffset, folder.nameLength), value);
        }

        [[nodiscard]] u32 CopyCatalogRelativePath(const CatalogSnapshot& snapshot,
                                                  const ProjectAssetCatalogRow& row,
                                                  const containers::ArraySpan<char> output) noexcept
        {
            if (!row.folder.IsValid() || row.folder.value >= snapshot.folderCount ||
                row.nameLength == 0u)
                return 0u;

            containers::StaticArray<ProjectAssetCatalogFolderId, MaximumFolderDepth> chain;
            ProjectAssetCatalogFolderId current = row.folder;
            u32 bytes = row.nameLength;
            while (current.IsValid())
            {
                if (current.value >= snapshot.folderCount)
                    return 0u;
                const CatalogFolder& folder = snapshot.folders[current.value];
                if (folder.depth == 0u)
                    break;
                if (chain.Size() == MaximumFolderDepth)
                    return 0u;
                chain.PushBack(current);
                if (bytes >= resources::MaximumResourcePathBytes ||
                    folder.nameLength > resources::MaximumResourcePathBytes - bytes - 1u)
                    return 0u;
                bytes += folder.nameLength + 1u;
                current = folder.parent;
            }
            if (bytes > resources::MaximumResourcePathBytes || output.Size() < bytes)
                return 0u;

            u32 write = 0u;
            for (u32 index = chain.Size(); index != 0u; --index)
            {
                const CatalogFolder& folder = snapshot.folders[chain[index - 1u].value];
                const containers::StringView name = snapshot.Text(folder.nameOffset, folder.nameLength);
                if (name.Length() != folder.nameLength)
                    return 0u;
                if (!name.Empty())
                    std::memcpy(output.Data() + write, name.Data(), name.Length());
                write += name.Length();
                output[write++] = '/';
            }
            const containers::StringView name = snapshot.Text(row.nameOffset, row.nameLength);
            if (name.Length() != row.nameLength)
                return 0u;
            std::memcpy(output.Data() + write, name.Data(), name.Length());
            write += name.Length();
            if (write < output.Size())
                output[write] = '\0';
            return write;
        }

        [[nodiscard]] bool CopyCatalogFolderRelativePath(
            const CatalogSnapshot& snapshot, const ProjectAssetCatalogFolderId id,
            const containers::ArraySpan<char> output, u32& writtenBytes) noexcept
        {
            writtenBytes = 0u;
            if (!id.IsValid() || id.value >= snapshot.folderCount || output.Empty())
                return false;

            containers::StaticArray<ProjectAssetCatalogFolderId, MaximumFolderDepth> chain;
            ProjectAssetCatalogFolderId current = id;
            u32 bytes = 0u;
            while (current.IsValid())
            {
                if (current.value >= snapshot.folderCount)
                    return false;
                const CatalogFolder& folder = snapshot.folders[current.value];
                if (folder.depth == 0u)
                    break;
                if (chain.Size() == MaximumFolderDepth)
                    return false;
                chain.PushBack(current);
                const u32 separator = chain.Size() > 1u ? 1u : 0u;
                if (bytes > resources::MaximumResourcePathBytes ||
                    separator > resources::MaximumResourcePathBytes - bytes ||
                    folder.nameLength > resources::MaximumResourcePathBytes - bytes - separator)
                    return false;
                bytes += separator + folder.nameLength;
                current = folder.parent;
            }
            if (bytes > resources::MaximumResourcePathBytes || output.Size() <= bytes)
                return false;

            u32 write = 0u;
            for (u32 index = chain.Size(); index != 0u; --index)
            {
                const CatalogFolder& folder = snapshot.folders[chain[index - 1u].value];
                const containers::StringView name = snapshot.Text(folder.nameOffset, folder.nameLength);
                if (name.Length() != folder.nameLength)
                    return false;
                if (write != 0u)
                    output[write++] = '/';
                if (!name.Empty())
                    std::memcpy(output.Data() + write, name.Data(), name.Length());
                write += name.Length();
            }
            output[write] = '\0';
            writtenBytes = write;
            return true;
        }

        [[nodiscard]] u32 RelativePathBytes(const CatalogSnapshot& snapshot,
                                            const ProjectAssetCatalogRow& row) noexcept
        {
            if (!row.folder.IsValid() || row.folder.value >= snapshot.folderCount ||
                row.nameLength == 0u)
                return 0u;
            u32 bytes = row.nameLength;
            ProjectAssetCatalogFolderId current = row.folder;
            u32 depth = 0u;
            while (current.IsValid())
            {
                if (current.value >= snapshot.folderCount || ++depth > MaximumFolderDepth)
                    return 0u;
                const CatalogFolder& folder = snapshot.folders[current.value];
                if (folder.depth == 0u)
                    break;
                if (bytes >= resources::MaximumResourcePathBytes ||
                    folder.nameLength > resources::MaximumResourcePathBytes - bytes - 1u)
                    return 0u;
                bytes += folder.nameLength + 1u;
                current = folder.parent;
            }
            return bytes;
        }

    } // namespace

    struct ProjectAssetCatalog::Impl final
    {
        CatalogSnapshot snapshot;
        CatalogSnapshot staged;
        CatalogCapture capture;
        jobs::Counter build;
        jobs::JobName buildJobName{"ProjectAssets/CatalogBuild"};
        ProjectAssetCatalogStatus status;
        u64 identityCookie = AcquireCatalogIdentity();
        u64 refreshGeneration = 0u;
        u64 lastAttemptGeneration = 0u;
        u64 retryNotBeforeTick = 0u;
        u64 countedPathBytes = 0u;
        u64 buildPeakBytes = 0u;
        u64 buildNanoseconds = 0u;
        u32 refreshCursor = 0u;
        u32 refreshFolderCursor = 0u;
        u32 captureStringWrite = 0u;
        ProjectAssetCatalogResult buildResult = ProjectAssetCatalogResult::Pending;
        bool lastAttemptRetryable = false;

        void RecordAttemptResult(const ProjectAssetCatalogResult result, const u64 generation,
                                 const bool retryable) noexcept
        {
            status.lastResult = result;
            lastAttemptGeneration = generation;
            lastAttemptRetryable = retryable;
            retryNotBeforeTick = retryable ? CatalogRetryDeadline() : 0u;
        }

        void ClearRefreshStorage(const bool waitForBuild) noexcept
        {
            if (build.IsValid())
            {
                if (waitForBuild && !build.IsReady())
                    static_cast<void>(build.Wait(true));
                if (waitForBuild || build.IsReady())
                    build = {};
            }
            if (!build.IsValid())
            {
                staged.Release();
                capture.Release();
            }
            refreshGeneration = 0u;
            countedPathBytes = 0u;
            buildPeakBytes = 0u;
            buildNanoseconds = 0u;
            refreshCursor = 0u;
            refreshFolderCursor = 0u;
            captureStringWrite = 0u;
            buildResult = ProjectAssetCatalogResult::Pending;
            status.refreshState = ProjectAssetCatalogRefreshState::Idle;
            status.pendingSourceGeneration = 0u;
            status.pendingAllocatedBytes = 0u;
            status.pendingRows = 0u;
            status.pendingRowsProcessed = 0u;
            status.pendingFolders = 0u;
            status.pendingFoldersProcessed = 0u;
        }

        void BuildCapturedSnapshot() noexcept;
    };

    void ProjectAssetCatalog::Impl::BuildCapturedSnapshot() noexcept
    {
        buildResult = ProjectAssetCatalogResult::Pending;
        buildPeakBytes = 0u;
        staged.Release();

        if (capture.generation == 0u || capture.rowCount > assets::SourceDatabase::MaximumSources ||
            capture.folderCount > assets::SourceDatabase::MaximumFolders ||
            capture.rootCount > assets::SourceDatabase::MaximumRoots ||
            capture.folderCount > std::numeric_limits<u32>::max() - capture.rowCount)
        {
            buildResult = ProjectAssetCatalogResult::InvalidSource;
            return;
        }

        const u32 entryCount = capture.rowCount + capture.folderCount;
        const auto entryRoot = [this](const u32 entry) noexcept
        {
            return entry < capture.rowCount ? capture.rows[entry].row.root
                                            : capture.folders[entry - capture.rowCount].root;
        };
        const auto entryPath = [this](const u32 entry) noexcept
        {
            if (entry < capture.rowCount)
            {
                const CatalogCaptureRow& row = capture.rows[entry];
                return capture.Text(row.pathOffset, row.pathLength);
            }
            const CatalogCaptureFolder& folder = capture.folders[entry - capture.rowCount];
            return capture.Text(folder.pathOffset, folder.pathLength);
        };
        const auto entryParentBytes = [this](const u32 entry) noexcept
        {
            return entry < capture.rowCount ? capture.rows[entry].nameStart
                                            : capture.folders[entry - capture.rowCount].pathLength;
        };

        if (entryCount > 1u)
        {
            std::sort(capture.order, capture.order + entryCount,
                      [this, &entryRoot, &entryPath](const u32 left, const u32 right) noexcept
            {
                const u32 leftRoot = entryRoot(left);
                const u32 rightRoot = entryRoot(right);
                if (leftRoot != rightRoot)
                    return leftRoot < rightRoot;
                const containers::StringView ap = entryPath(left);
                const containers::StringView bp = entryPath(right);
                const int folded = CompareIgnoreCase(ap, bp);
                if (folded != 0)
                    return folded < 0;
                if (ap != bp)
                    return ap < bp;
                return left < right;
            });
        }

        u64 folderNameBytes64 = 0u;
        u64 rowNameBytes64 = 0u;
        u64 folderCount64 = capture.rootCount;
        u32 previousRoot = ~u32{0};
        containers::StringView previousPath;
        SegmentList previousSegments;
        for (u32 sorted = 0u; sorted < entryCount; ++sorted)
        {
            const u32 entry = capture.order[sorted];
            const bool sourceRow = entry < capture.rowCount;
            const containers::StringView relative = entryPath(entry);
            if (relative.Empty())
            {
                buildResult = ProjectAssetCatalogResult::InvalidSource;
                return;
            }
            const u32 parentBytes = entryParentBytes(entry);
            if (parentBytes > relative.Length())
            {
                buildResult = ProjectAssetCatalogResult::InvalidSource;
                return;
            }
            if (sourceRow)
                rowNameBytes64 += relative.Length() - parentBytes;
            SegmentList currentSegments;
            if (!ParseFolderSegments(relative, parentBytes, currentSegments) ||
                (!sourceRow && currentSegments.count == 0u))
            {
                buildResult = ProjectAssetCatalogResult::LimitExceeded;
                return;
            }
            const u32 root = entryRoot(entry);
            const u32 common = root == previousRoot
                                   ? CommonSegments(previousPath, previousSegments, relative, currentSegments)
                                   : 0u;
            folderCount64 += currentSegments.count - common;
            for (u32 segment = common; segment < currentSegments.count; ++segment)
                folderNameBytes64 += currentSegments.lengths[segment];
            previousRoot = root;
            previousPath = relative;
            previousSegments = currentSegments;
            if (rowNameBytes64 > std::numeric_limits<u32>::max() ||
                folderNameBytes64 > std::numeric_limits<u32>::max() ||
                folderCount64 > std::numeric_limits<u32>::max())
            {
                buildResult = ProjectAssetCatalogResult::LimitExceeded;
                return;
            }
        }

        if (rowNameBytes64 > std::numeric_limits<u32>::max() - folderNameBytes64)
        {
            buildResult = ProjectAssetCatalogResult::LimitExceeded;
            return;
        }
        const u32 folderCount = static_cast<u32>(folderCount64);
        const u32 stringBytes = static_cast<u32>(rowNameBytes64 + folderNameBytes64);
        SnapshotLayout layout;
        if (!MakeLayout(capture.rowCount, folderCount, stringBytes, capture.rootCount, layout) ||
            !PeakFits(snapshot, layout.total, capture.block ? capture.block.size : 0u))
        {
            buildResult = ProjectAssetCatalogResult::LimitExceeded;
            return;
        }
        if (!BindSnapshot(staged, layout, capture.rowCount, folderCount, stringBytes, capture.rootCount))
        {
            buildResult = ProjectAssetCatalogResult::OutOfMemory;
            return;
        }
        staged.generation = capture.generation;

        u32 stringWrite = 0u;
        for (u32 sourceIndex = 0u; sourceIndex < capture.rowCount; ++sourceIndex)
        {
            const CatalogCaptureRow& captured = capture.rows[sourceIndex];
            ProjectAssetCatalogRow& row =
                *::new (&staged.rows[sourceIndex]) ProjectAssetCatalogRow(captured.row);
            const containers::StringView relative = capture.Text(captured.pathOffset, captured.pathLength);
            const containers::StringView name =
                relative.Slice(captured.nameStart, captured.pathLength);
            row.nameOffset = stringWrite;
            row.nameLength = name.Length();
            if (!name.Empty())
                std::memcpy(staged.strings + stringWrite, name.Data(), name.Length());
            stringWrite += name.Length();
        }
        u32 sortedRowCount = 0u;
        for (u32 index = 0u; index < entryCount; ++index)
        {
            const u32 entry = capture.order[index];
            if (entry < capture.rowCount)
                staged.sortedRows[sortedRowCount++] = entry;
        }
        if (sortedRowCount != capture.rowCount)
        {
            buildResult = ProjectAssetCatalogResult::InvalidSource;
            return;
        }

        for (u32 root = 0u; root < capture.rootCount; ++root)
        {
            ::new (&staged.folders[root]) CatalogFolder();
            staged.rootFolders[root] = ProjectAssetCatalogFolderId{root};
            staged.folders[root].root = root;
        }

        containers::StaticArray<ProjectAssetCatalogFolderId, MaximumFolderDepth> activeFolders;
        u32 activeRoot = ~u32{0};
        u32 nextFolder = capture.rootCount;
        const auto closeTo = [&activeFolders, this](const u32 keep, const u32 sortedIndex) noexcept
        {
            while (activeFolders.Size() > keep)
            {
                CatalogFolder& folder = staged.folders[activeFolders[activeFolders.Size() - 1u].value];
                folder.subtreeCount = sortedIndex - folder.subtreeStart;
                activeFolders.Resize(activeFolders.Size() - 1u);
            }
        };

        u32 sortedRowIndex = 0u;
        for (u32 sortedEntry = 0u; sortedEntry < entryCount; ++sortedEntry)
        {
            const u32 entry = capture.order[sortedEntry];
            const bool sourceRow = entry < capture.rowCount;
            const u32 root = entryRoot(entry);
            if (root >= capture.rootCount)
            {
                buildResult = ProjectAssetCatalogResult::InvalidSource;
                return;
            }
            const containers::StringView path = entryPath(entry);
            const u32 parentBytes = entryParentBytes(entry);
            SegmentList segments;
            if (!ParseFolderSegments(path, parentBytes, segments) || (!sourceRow && segments.count == 0u))
            {
                buildResult = ProjectAssetCatalogResult::LimitExceeded;
                return;
            }
            if (root != activeRoot)
            {
                closeTo(0u, sortedRowIndex);
                activeRoot = root;
                const ProjectAssetCatalogFolderId rootFolder = staged.rootFolders[root];
                staged.folders[rootFolder.value].subtreeStart = sortedRowIndex;
                activeFolders.PushBack(rootFolder);
            }

            u32 common = 0u;
            while (common < segments.count && common + 1u < activeFolders.Size())
            {
                const CatalogFolder& folder = staged.folders[activeFolders[common + 1u].value];
                const containers::StringView segment =
                    path.Slice(segments.starts[common], segments.starts[common] + segments.lengths[common]);
                if (!SameFolderName(staged, folder, segment))
                    break;
                ++common;
            }
            closeTo(common + 1u, sortedRowIndex);

            for (u32 segment = common; segment < segments.count; ++segment)
            {
                if (nextFolder >= staged.folderCount)
                {
                    buildResult = ProjectAssetCatalogResult::InvalidSource;
                    return;
                }
                const containers::StringView name =
                    path.Slice(segments.starts[segment], segments.starts[segment] + segments.lengths[segment]);
                const ProjectAssetCatalogFolderId parent = activeFolders[activeFolders.Size() - 1u];
                const ProjectAssetCatalogFolderId id{nextFolder++};
                CatalogFolder& created = *::new (&staged.folders[id.value]) CatalogFolder();
                created.parent = parent;
                created.nameOffset = stringWrite;
                created.nameLength = name.Length();
                created.root = root;
                created.depth = segment + 1u;
                created.subtreeStart = sortedRowIndex;
                std::memcpy(staged.strings + stringWrite, name.Data(), name.Length());
                stringWrite += name.Length();
                CatalogFolder& parentFolder = staged.folders[parent.value];
                ++parentFolder.childCount;
                activeFolders.PushBack(id);
            }
            if (sourceRow)
            {
                ProjectAssetCatalogRow& row = staged.rows[entry];
                row.folder = activeFolders[activeFolders.Size() - 1u];
                ++staged.folders[row.folder.value].directCount;
                ++sortedRowIndex;
            }
        }
        closeTo(0u, sortedRowIndex);
        if (sortedRowIndex != capture.rowCount || nextFolder != staged.folderCount ||
            stringWrite != staged.stringBytes)
        {
            buildResult = ProjectAssetCatalogResult::InvalidSource;
            return;
        }

        const usize cursorBytes = static_cast<usize>(staged.folderCount) * sizeof(u32);
        if (!PeakFits(snapshot, staged.block.size,
                      (capture.block ? capture.block.size : 0u) + cursorBytes))
        {
            buildResult = ProjectAssetCatalogResult::LimitExceeded;
            return;
        }
        memory::MemoryBlock cursorBlock =
            cursorBytes != 0u ? memory::Allocate(memory::PoolId::Assets, cursorBytes, alignof(u32))
                              : memory::MemoryBlock{};
        if (cursorBytes != 0u && !cursorBlock)
        {
            buildResult = ProjectAssetCatalogResult::OutOfMemory;
            return;
        }
        u32* const cursors = static_cast<u32*>(cursorBlock.address);
        if (cursorBytes != 0u)
            std::memset(cursors, 0, cursorBytes);

        u32 childOffset = 0u;
        for (u32 folderIndex = 0u; folderIndex < staged.folderCount; ++folderIndex)
        {
            CatalogFolder& folder = staged.folders[folderIndex];
            folder.childStart = childOffset;
            if (folder.childCount > std::numeric_limits<u32>::max() - childOffset)
            {
                if (cursorBlock)
                    memory::Free(cursorBlock);
                buildResult = ProjectAssetCatalogResult::LimitExceeded;
                return;
            }
            childOffset += folder.childCount;
        }
        if (childOffset != staged.folderCount - staged.rootCount)
        {
            if (cursorBlock)
                memory::Free(cursorBlock);
            buildResult = ProjectAssetCatalogResult::InvalidSource;
            return;
        }
        for (u32 folderIndex = staged.rootCount; folderIndex < staged.folderCount; ++folderIndex)
        {
            const ProjectAssetCatalogFolderId id{folderIndex};
            const ProjectAssetCatalogFolderId parent = staged.folders[folderIndex].parent;
            if (!parent.IsValid() || parent.value >= staged.folderCount)
            {
                if (cursorBlock)
                    memory::Free(cursorBlock);
                buildResult = ProjectAssetCatalogResult::InvalidSource;
                return;
            }
            const CatalogFolder& parentFolder = staged.folders[parent.value];
            staged.childFolders[parentFolder.childStart + cursors[parent.value]++] = id;
        }
        if (cursorBytes != 0u)
            std::memset(cursors, 0, cursorBytes);

        u32 directOffset = 0u;
        for (u32 folderIndex = 0u; folderIndex < staged.folderCount; ++folderIndex)
        {
            CatalogFolder& folder = staged.folders[folderIndex];
            folder.directStart = directOffset;
            if (folder.directCount > std::numeric_limits<u32>::max() - directOffset)
            {
                if (cursorBlock)
                    memory::Free(cursorBlock);
                buildResult = ProjectAssetCatalogResult::LimitExceeded;
                return;
            }
            directOffset += folder.directCount;
        }
        if (directOffset != staged.rowCount)
        {
            if (cursorBlock)
                memory::Free(cursorBlock);
            buildResult = ProjectAssetCatalogResult::InvalidSource;
            return;
        }
        for (u32 sortedIndex = 0u; sortedIndex < staged.rowCount; ++sortedIndex)
        {
            const u32 rowIndex = staged.sortedRows[sortedIndex];
            const ProjectAssetCatalogFolderId folderId = staged.rows[rowIndex].folder;
            CatalogFolder& folder = staged.folders[folderId.value];
            staged.directRows[folder.directStart + cursors[folderId.value]++] = rowIndex;
        }

        buildPeakBytes = (snapshot.block ? snapshot.block.size : 0u) +
                         (capture.block ? capture.block.size : 0u) + staged.block.size +
                         (cursorBlock ? cursorBlock.size : 0u) + ProjectAssetCatalogQueryBudgetBytes;
        if (cursorBlock)
            memory::Free(cursorBlock);
        buildResult = ProjectAssetCatalogResult::Success;
    }

    ProjectAssetCatalogQueryResult::ProjectAssetCatalogQueryResult() noexcept = default;

    ProjectAssetCatalogQueryResult::~ProjectAssetCatalogQueryResult()
    {
        Reset();
        ReleaseQueryBytes(m_accountedMatchBytes);
        m_accountedMatchBytes = 0u;
        if (m_matches)
            memory::Free(m_matches);
    }

    void ProjectAssetCatalogQueryResult::Reset() noexcept
    {
        if (m_countedActive)
            static_cast<void>(g_projectAssetCatalogActiveQueries.fetch_sub(1u, std::memory_order_acq_rel));
        m_status = {};
        m_catalogIdentity = nullptr;
        m_catalogIdentityCookie = 0u;
        m_folder = {};
        m_candidateOffset = 0u;
        m_cursor = 0u;
        m_matchBitCapacity = 0u;
        m_matchWordCount = 0u;
        m_matchBlockCount = 0u;
        m_requiredIssues = 0u;
        m_excludedIssues = 0u;
        m_sourceType = resources::InvalidResourceTypeId;
        m_primaryOutputType = resources::InvalidResourceTypeId;
        m_importState = assets::SourceImportState::NotClassified;
        m_referenceScanState = assets::SourceReferenceScanState::NotScanned;
        m_textBytes = 0u;
        m_passthrough = false;
        m_recursive = false;
        m_filterImportState = false;
        m_filterReferenceScanState = false;
        m_countedActive = false;
        m_text[0] = '\0';
    }

    ProjectAssetCatalogQueryStatus ProjectAssetCatalogQueryResult::GetStatus() const noexcept
    {
        ProjectAssetCatalogQueryStatus status = m_status;
        status.allocatedBytes = m_matches ? static_cast<u32>(m_matches.size) : 0u;
        return status;
    }

    ProjectAssetCatalog::ProjectAssetCatalog() noexcept
    {
        const memory::MemoryBlock block = memory::Allocate(memory::PoolId::Assets, sizeof(Impl), alignof(Impl));
        if (block)
            m_impl = ::new (block.address) Impl();
    }

    ProjectAssetCatalog::~ProjectAssetCatalog()
    {
        if (m_impl == nullptr)
            return;
        m_impl->ClearRefreshStorage(true);
        m_impl->~Impl();
        memory::MemoryBlock block{m_impl, sizeof(Impl), memory::PoolId::Assets};
        memory::Free(block);
        m_impl = nullptr;
    }

    ProjectAssetCatalogResult ProjectAssetCatalog::Refresh(const assets::SourceDatabase& sources,
                                                           const u32 maximumSourceRows,
                                                           const u32 maximumPathBytes) noexcept
    {
        if (m_impl == nullptr)
            return ProjectAssetCatalogResult::OutOfMemory;
        if (maximumSourceRows == 0u || maximumPathBytes < resources::MaximumResourcePathBytes)
            return ProjectAssetCatalogResult::InvalidArgument;

        const u64 generation = sources.GetGeneration();

        if (m_impl->status.refreshState == ProjectAssetCatalogRefreshState::Building)
        {
            if (!m_impl->build.IsValid() || !m_impl->build.IsReady())
                return ProjectAssetCatalogResult::Pending;

            m_impl->build = {};
            const ProjectAssetCatalogResult built = m_impl->buildResult;
            const u64 builtGeneration = m_impl->refreshGeneration;
            const u64 buildPeak = m_impl->buildPeakBytes;
            if (built != ProjectAssetCatalogResult::Success)
            {
                m_impl->RecordAttemptResult(built, builtGeneration,
                                            built == ProjectAssetCatalogResult::OutOfMemory);
                m_impl->ClearRefreshStorage(false);
                return built;
            }
            if (generation != builtGeneration)
            {
                m_impl->RecordAttemptResult(ProjectAssetCatalogResult::StaleSource, builtGeneration, false);
                m_impl->ClearRefreshStorage(false);
                return ProjectAssetCatalogResult::StaleSource;
            }

            m_impl->snapshot.Swap(m_impl->staged);
            m_impl->status.lastResult = ProjectAssetCatalogResult::Success;
            m_impl->status.sourceGeneration = builtGeneration;
            m_impl->status.allocatedBytes = m_impl->snapshot.block ? m_impl->snapshot.block.size : 0u;
            m_impl->status.lastRebuildPeakBytes = buildPeak;
            m_impl->status.lastBuildNanoseconds = m_impl->buildNanoseconds;
            m_impl->status.rowCount = m_impl->snapshot.rowCount;
            m_impl->status.folderCount = m_impl->snapshot.folderCount;
            m_impl->status.rootCount = m_impl->snapshot.rootCount;
            m_impl->status.steadyStateTargetExceeded =
                m_impl->status.allocatedBytes + g_projectAssetCatalogQueryBytes.load(std::memory_order_relaxed) >
                ProjectAssetCatalogSteadyStateTargetBytes;
            m_impl->lastAttemptGeneration = builtGeneration;
            m_impl->lastAttemptRetryable = false;
            m_impl->retryNotBeforeTick = 0u;
            m_impl->ClearRefreshStorage(false);
            return ProjectAssetCatalogResult::Success;
        }

        if (m_impl->status.refreshState != ProjectAssetCatalogRefreshState::Idle &&
            generation != m_impl->refreshGeneration)
        {
            m_impl->RecordAttemptResult(ProjectAssetCatalogResult::StaleSource,
                                        m_impl->refreshGeneration, false);
            m_impl->ClearRefreshStorage(false);
            return ProjectAssetCatalogResult::StaleSource;
        }

        if (m_impl->status.refreshState == ProjectAssetCatalogRefreshState::Idle)
        {
            if (generation != 0u && generation == m_impl->snapshot.generation)
                return ProjectAssetCatalogResult::Success;
            if (generation != 0u && generation == m_impl->lastAttemptGeneration &&
                m_impl->status.lastResult != ProjectAssetCatalogResult::Success &&
                m_impl->status.lastResult != ProjectAssetCatalogResult::StaleSource)
            {
                if (!m_impl->lastAttemptRetryable)
                    return m_impl->status.lastResult;
                if (m_impl->retryNotBeforeTick != 0u &&
                    system::GetMonotonicTicks() < m_impl->retryNotBeforeTick)
                    return m_impl->status.lastResult;
                m_impl->lastAttemptRetryable = false;
                m_impl->retryNotBeforeTick = 0u;
            }

            const containers::ArraySpan<const assets::SourceRecord> rows = sources.GetResources();
            const containers::ArraySpan<const assets::SourceFolder> folders = sources.GetFolders();
            const containers::ArraySpan<const assets::SourceRoot> roots = sources.GetRoots();
            if (generation == 0u || rows.Size() > assets::SourceDatabase::MaximumSources ||
                folders.Size() > assets::SourceDatabase::MaximumFolders ||
                roots.Empty() || roots.Size() > assets::SourceDatabase::MaximumRoots)
            {
                m_impl->RecordAttemptResult(ProjectAssetCatalogResult::InvalidSource, generation, false);
                return m_impl->status.lastResult;
            }
            m_impl->status.lastResult = ProjectAssetCatalogResult::Pending;
            m_impl->refreshGeneration = generation;
            m_impl->countedPathBytes = 0u;
            m_impl->refreshCursor = 0u;
            m_impl->refreshFolderCursor = 0u;
            m_impl->captureStringWrite = 0u;
            m_impl->status.refreshState = ProjectAssetCatalogRefreshState::Counting;
            m_impl->status.pendingSourceGeneration = generation;
            m_impl->status.pendingRows = rows.Size();
            m_impl->status.pendingRowsProcessed = 0u;
            m_impl->status.pendingFolders = folders.Size();
            m_impl->status.pendingFoldersProcessed = 0u;
            m_impl->status.pendingAllocatedBytes = 0u;
        }

        const containers::ArraySpan<const assets::SourceRecord> sourceRows = sources.GetResources();
        const containers::ArraySpan<const assets::SourceFolder> sourceFolders = sources.GetFolders();
        const containers::ArraySpan<const assets::SourceRoot> sourceRoots = sources.GetRoots();
        if (sourceRows.Size() != m_impl->status.pendingRows ||
            sourceFolders.Size() != m_impl->status.pendingFolders ||
            sourceRoots.Size() > assets::SourceDatabase::MaximumRoots)
        {
            m_impl->RecordAttemptResult(ProjectAssetCatalogResult::StaleSource,
                                        m_impl->refreshGeneration, false);
            m_impl->ClearRefreshStorage(false);
            return ProjectAssetCatalogResult::StaleSource;
        }

        if (m_impl->status.refreshState == ProjectAssetCatalogRefreshState::Counting)
        {
            u32 processedEntries = 0u;
            u32 processedPathBytes = 0u;
            while (m_impl->refreshCursor < sourceRows.Size() && processedEntries < maximumSourceRows)
            {
                const assets::SourceRecord& source = sourceRows[m_impl->refreshCursor];
                if (source.root >= sourceRoots.Size() || !source.path.IsFilePath())
                {
                    m_impl->RecordAttemptResult(ProjectAssetCatalogResult::InvalidSource,
                                                m_impl->refreshGeneration, false);
                    m_impl->ClearRefreshStorage(false);
                    return m_impl->status.lastResult;
                }
                const containers::StringView relative = RelativePath(source, sourceRoots);
                if (relative.Empty() || relative.Length() > resources::MaximumResourcePathBytes ||
                    m_impl->countedPathBytes > std::numeric_limits<u32>::max() - relative.Length())
                {
                    m_impl->RecordAttemptResult(ProjectAssetCatalogResult::LimitExceeded,
                                                m_impl->refreshGeneration, false);
                    m_impl->ClearRefreshStorage(false);
                    return m_impl->status.lastResult;
                }
                if (processedPathBytes > maximumPathBytes - relative.Length())
                    break;
                processedPathBytes += relative.Length();
                m_impl->countedPathBytes += relative.Length();
                ++m_impl->refreshCursor;
                ++processedEntries;
            }
            m_impl->status.pendingRowsProcessed = m_impl->refreshCursor;
            if (m_impl->refreshCursor != sourceRows.Size())
                return ProjectAssetCatalogResult::Pending;
            while (m_impl->refreshFolderCursor < sourceFolders.Size() && processedEntries < maximumSourceRows)
            {
                const assets::SourceFolder& folder = sourceFolders[m_impl->refreshFolderCursor];
                if (folder.root >= sourceRoots.Size() || !folder.path.IsDirectoryPath())
                {
                    m_impl->RecordAttemptResult(ProjectAssetCatalogResult::InvalidSource,
                                                m_impl->refreshGeneration, false);
                    m_impl->ClearRefreshStorage(false);
                    return m_impl->status.lastResult;
                }
                const containers::StringView relative = RelativePath(folder, sourceRoots);
                if (relative.Empty() || relative.Length() > resources::MaximumResourcePathBytes ||
                    m_impl->countedPathBytes > std::numeric_limits<u32>::max() - relative.Length())
                {
                    m_impl->RecordAttemptResult(ProjectAssetCatalogResult::LimitExceeded,
                                                m_impl->refreshGeneration, false);
                    m_impl->ClearRefreshStorage(false);
                    return m_impl->status.lastResult;
                }
                if (processedPathBytes > maximumPathBytes - relative.Length())
                    break;
                processedPathBytes += relative.Length();
                m_impl->countedPathBytes += relative.Length();
                ++m_impl->refreshFolderCursor;
                ++processedEntries;
            }
            m_impl->status.pendingFoldersProcessed = m_impl->refreshFolderCursor;
            if (m_impl->refreshFolderCursor != sourceFolders.Size())
                return ProjectAssetCatalogResult::Pending;
            if (sources.GetGeneration() != m_impl->refreshGeneration)
            {
                m_impl->RecordAttemptResult(ProjectAssetCatalogResult::StaleSource,
                                            m_impl->refreshGeneration, false);
                m_impl->ClearRefreshStorage(false);
                return ProjectAssetCatalogResult::StaleSource;
            }

            CaptureLayout captureLayout;
            if (!MakeCaptureLayout(sourceRows.Size(), sourceFolders.Size(),
                                   static_cast<u32>(m_impl->countedPathBytes), captureLayout) ||
                !PeakFits(m_impl->snapshot, 0u, captureLayout.total))
            {
                m_impl->RecordAttemptResult(ProjectAssetCatalogResult::LimitExceeded,
                                            m_impl->refreshGeneration, false);
                m_impl->ClearRefreshStorage(false);
                return m_impl->status.lastResult;
            }
            if (!BindCapture(m_impl->capture, captureLayout, sourceRows.Size(), sourceFolders.Size(), sourceRoots.Size(),
                             static_cast<u32>(m_impl->countedPathBytes), m_impl->refreshGeneration))
            {
                m_impl->RecordAttemptResult(ProjectAssetCatalogResult::OutOfMemory,
                                            m_impl->refreshGeneration, true);
                m_impl->ClearRefreshStorage(false);
                return m_impl->status.lastResult;
            }
            m_impl->refreshCursor = 0u;
            m_impl->refreshFolderCursor = 0u;
            m_impl->captureStringWrite = 0u;
            m_impl->status.refreshState = ProjectAssetCatalogRefreshState::Capturing;
            m_impl->status.pendingRowsProcessed = 0u;
            m_impl->status.pendingFoldersProcessed = 0u;
            m_impl->status.pendingAllocatedBytes =
                m_impl->capture.block ? m_impl->capture.block.size : 0u;
            return ProjectAssetCatalogResult::Pending;
        }

        u32 processedEntries = 0u;
        u32 processedPathBytes = 0u;
        while (m_impl->refreshCursor < sourceRows.Size() && processedEntries < maximumSourceRows)
        {
            const u32 sourceIndex = m_impl->refreshCursor;
            const assets::SourceRecord& source = sourceRows[sourceIndex];
            if (source.root >= sourceRoots.Size() || !source.path.IsFilePath())
            {
                m_impl->RecordAttemptResult(ProjectAssetCatalogResult::InvalidSource,
                                            m_impl->refreshGeneration, false);
                m_impl->ClearRefreshStorage(false);
                return m_impl->status.lastResult;
            }
            const containers::StringView relative = RelativePath(source, sourceRoots);
            if (relative.Empty() || relative.Length() > resources::MaximumResourcePathBytes ||
                m_impl->captureStringWrite > m_impl->capture.stringBytes ||
                relative.Length() > m_impl->capture.stringBytes - m_impl->captureStringWrite)
            {
                m_impl->RecordAttemptResult(ProjectAssetCatalogResult::InvalidSource,
                                            m_impl->refreshGeneration, false);
                m_impl->ClearRefreshStorage(false);
                return m_impl->status.lastResult;
            }
            if (processedPathBytes > maximumPathBytes - relative.Length())
                break;

            CatalogCaptureRow& captured =
                *::new (&m_impl->capture.rows[sourceIndex]) CatalogCaptureRow();
            ProjectAssetCatalogRow& row = captured.row;
            row.asset = source.metadata.id;
            row.sourceIdentity = source.sourceIdentity;
            row.importer = source.importer;
            row.root = source.root;
            row.issues = source.issues;
            row.availableImporterVersion = source.availableImporterVersion;
            row.sourceType = source.sourceType;
            row.primaryOutputType = source.primaryOutputType;
            row.metadataResult = source.metadataResult;
            row.importState = source.importState;
            row.referenceScanState = source.referenceScanState;
            row.rootReadOnly = sourceRoots[source.root].readOnly;
            row.hasAssetIdentity = source.metadata.id.IsValid();
            captured.pathOffset = m_impl->captureStringWrite;
            captured.pathLength = relative.Length();
            if (relative.Length() != 0u)
                std::memcpy(m_impl->capture.strings + m_impl->captureStringWrite,
                            relative.Data(), relative.Length());
            const u32 nameStart = FileNameStart(relative);
            captured.nameStart = nameStart;
            row.nameOffset = 0u;
            row.nameLength = relative.Length() - nameStart;
            m_impl->captureStringWrite += relative.Length();
            m_impl->capture.order[sourceIndex] = sourceIndex;
            processedPathBytes += relative.Length();
            ++m_impl->refreshCursor;
            ++processedEntries;
        }
        m_impl->status.pendingRowsProcessed = m_impl->refreshCursor;
        if (m_impl->refreshCursor != sourceRows.Size())
            return ProjectAssetCatalogResult::Pending;
        while (m_impl->refreshFolderCursor < sourceFolders.Size() && processedEntries < maximumSourceRows)
        {
            const u32 folderIndex = m_impl->refreshFolderCursor;
            const assets::SourceFolder& folder = sourceFolders[folderIndex];
            if (folder.root >= sourceRoots.Size() || !folder.path.IsDirectoryPath())
            {
                m_impl->RecordAttemptResult(ProjectAssetCatalogResult::InvalidSource,
                                            m_impl->refreshGeneration, false);
                m_impl->ClearRefreshStorage(false);
                return m_impl->status.lastResult;
            }
            const containers::StringView relative = RelativePath(folder, sourceRoots);
            if (relative.Empty() || relative.Length() > resources::MaximumResourcePathBytes ||
                m_impl->captureStringWrite > m_impl->capture.stringBytes ||
                relative.Length() > m_impl->capture.stringBytes - m_impl->captureStringWrite)
            {
                m_impl->RecordAttemptResult(ProjectAssetCatalogResult::InvalidSource,
                                            m_impl->refreshGeneration, false);
                m_impl->ClearRefreshStorage(false);
                return m_impl->status.lastResult;
            }
            if (processedPathBytes > maximumPathBytes - relative.Length())
                break;

            CatalogCaptureFolder& captured =
                *::new (&m_impl->capture.folders[folderIndex]) CatalogCaptureFolder();
            captured.root = folder.root;
            captured.pathOffset = m_impl->captureStringWrite;
            captured.pathLength = relative.Length();
            std::memcpy(m_impl->capture.strings + m_impl->captureStringWrite,
                        relative.Data(), relative.Length());
            m_impl->captureStringWrite += relative.Length();
            m_impl->capture.order[m_impl->capture.rowCount + folderIndex] =
                m_impl->capture.rowCount + folderIndex;
            processedPathBytes += relative.Length();
            ++m_impl->refreshFolderCursor;
            ++processedEntries;
        }
        m_impl->status.pendingFoldersProcessed = m_impl->refreshFolderCursor;
        if (m_impl->refreshFolderCursor != sourceFolders.Size())
            return ProjectAssetCatalogResult::Pending;
        if (m_impl->captureStringWrite != m_impl->capture.stringBytes ||
            sources.GetGeneration() != m_impl->refreshGeneration)
        {
            m_impl->RecordAttemptResult(ProjectAssetCatalogResult::StaleSource,
                                        m_impl->refreshGeneration, false);
            m_impl->ClearRefreshStorage(false);
            return ProjectAssetCatalogResult::StaleSource;
        }

        // Publish the in-flight marker before a worker can observe the task. A
        // fast worker may run to completion before Dispatch/ExtractCounter
        // returns, so never overwrite its terminal result afterwards.
        m_impl->buildResult = ProjectAssetCatalogResult::Pending;
        jobs::Builder builder({jobs::Priority::Latent, jobs::Affinity::AnyWorker}, m_impl);
        jobs::Task task = jobs::Task::Create([impl = m_impl](const jobs::JobContext&) noexcept
        {
            const u64 beginTicks = system::GetMonotonicTicks();
            impl->BuildCapturedSnapshot();
            impl->buildNanoseconds =
                ElapsedNanoseconds(beginTicks, system::GetMonotonicTicks());
        });
        if (!builder.IsValid())
        {
            m_impl->RecordAttemptResult(ProjectAssetCatalogResult::LimitExceeded,
                                        m_impl->refreshGeneration, true);
            m_impl->ClearRefreshStorage(false);
            return m_impl->status.lastResult;
        }
        if (!task)
        {
            m_impl->RecordAttemptResult(ProjectAssetCatalogResult::OutOfMemory,
                                        m_impl->refreshGeneration, true);
            m_impl->ClearRefreshStorage(false);
            return m_impl->status.lastResult;
        }
        if (!builder.Dispatch(m_impl->buildJobName, static_cast<jobs::Task&&>(task)))
        {
            m_impl->RecordAttemptResult(ProjectAssetCatalogResult::LimitExceeded,
                                        m_impl->refreshGeneration, true);
            m_impl->ClearRefreshStorage(false);
            return m_impl->status.lastResult;
        }
        m_impl->build = builder.ExtractCounter();
        if (!m_impl->build.IsValid())
        {
            // ExtractCounter allocates its public wrapper before detaching the
            // builder's internal chain. On wrapper OOM the dispatched task is
            // still builder-owned; synchronously drain it before releasing the
            // capture/staged storage the task touches.
            const bool drained = builder.WaitForCompletion();
            m_impl->RecordAttemptResult(drained ? ProjectAssetCatalogResult::OutOfMemory
                                                : ProjectAssetCatalogResult::InvalidSource,
                                        m_impl->refreshGeneration, drained);
            m_impl->ClearRefreshStorage(false);
            return m_impl->status.lastResult;
        }
        m_impl->status.refreshState = ProjectAssetCatalogRefreshState::Building;
        return ProjectAssetCatalogResult::Pending;
    }

    void ProjectAssetCatalog::Reset() noexcept
    {
        if (m_impl == nullptr)
            return;
        m_impl->ClearRefreshStorage(true);
        m_impl->snapshot.Release();
        m_impl->status = {};
        m_impl->lastAttemptGeneration = 0u;
        m_impl->lastAttemptRetryable = false;
        m_impl->retryNotBeforeTick = 0u;
    }

    ProjectAssetCatalogStatus ProjectAssetCatalog::GetStatus() const noexcept
    {
        if (m_impl == nullptr)
            return {};
        ProjectAssetCatalogStatus status = m_impl->status;
        status.queryAllocatedBytes = g_projectAssetCatalogQueryBytes.load(std::memory_order_relaxed);
        status.activeQueryCount = g_projectAssetCatalogActiveQueries.load(std::memory_order_relaxed);
        status.steadyStateTargetExceeded =
            status.allocatedBytes + status.queryAllocatedBytes > ProjectAssetCatalogSteadyStateTargetBytes;
        return status;
    }

    const ProjectAssetCatalogRow* ProjectAssetCatalog::GetRow(const ProjectAssetCatalogRowId id) const noexcept
    {
        return m_impl != nullptr && id.value < m_impl->snapshot.rowCount ? &m_impl->snapshot.rows[id.value] : nullptr;
    }

    containers::StringView ProjectAssetCatalog::GetName(const ProjectAssetCatalogRowId id) const noexcept
    {
        const ProjectAssetCatalogRow* const row = GetRow(id);
        return row != nullptr ? m_impl->snapshot.Text(row->nameOffset, row->nameLength) : containers::StringView{};
    }

    u32 ProjectAssetCatalog::CopyRelativePath(const ProjectAssetCatalogRowId id,
                                              const containers::ArraySpan<char> output) const noexcept
    {
        const ProjectAssetCatalogRow* const row = GetRow(id);
        return row != nullptr ? CopyCatalogRelativePath(m_impl->snapshot, *row, output) : 0u;
    }

    ProjectAssetCatalogRowId ProjectAssetCatalog::ResolveAsset(const assets::AssetId asset) const noexcept
    {
        if (m_impl == nullptr || !asset.IsValid())
            return {};
        for (u32 rowIndex = 0u; rowIndex < m_impl->snapshot.rowCount; ++rowIndex)
            if (m_impl->snapshot.rows[rowIndex].hasAssetIdentity &&
                m_impl->snapshot.rows[rowIndex].asset == asset &&
                (m_impl->snapshot.rows[rowIndex].issues & assets::AssetIdentityConflict) == 0u)
                return ProjectAssetCatalogRowId{rowIndex};
        return {};
    }

    ProjectAssetCatalogRowId ProjectAssetCatalog::ResolveRow(
        const u32 root, const containers::StringView relativePath) const noexcept
    {
        if (m_impl == nullptr || relativePath.Empty() || root >= m_impl->snapshot.rootCount ||
            relativePath.Length() > resources::MaximumResourcePathBytes)
            return {};
        const ProjectAssetCatalogFolderId rootFolder = m_impl->snapshot.rootFolders[root];
        if (!rootFolder.IsValid() || rootFolder.value >= m_impl->snapshot.folderCount)
            return {};
        const CatalogFolder& folder = m_impl->snapshot.folders[rootFolder.value];
        u32 lower = folder.subtreeStart;
        u32 upper = folder.subtreeStart + folder.subtreeCount;
        char candidatePath[resources::MaximumResourcePathBytes + 1u]{};
        while (lower < upper)
        {
            const u32 middle = lower + (upper - lower) / 2u;
            const u32 rowIndex = m_impl->snapshot.sortedRows[middle];
            const ProjectAssetCatalogRow& row = m_impl->snapshot.rows[rowIndex];
            const u32 bytes = CopyCatalogRelativePath(
                m_impl->snapshot, row, {candidatePath, static_cast<u32>(sizeof(candidatePath))});
            if (bytes == 0u)
                return {};
            const int order = CompareIgnoreCase(containers::StringView(candidatePath, bytes), relativePath);
            if (order < 0)
                lower = middle + 1u;
            else
                upper = middle;
        }
        if (lower >= folder.subtreeStart + folder.subtreeCount)
            return {};
        const u32 rowIndex = m_impl->snapshot.sortedRows[lower];
        const ProjectAssetCatalogRow& row = m_impl->snapshot.rows[rowIndex];
        const u32 bytes = CopyCatalogRelativePath(
            m_impl->snapshot, row, {candidatePath, static_cast<u32>(sizeof(candidatePath))});
        return bytes != 0u &&
                       EqualIgnoreCase(containers::StringView(candidatePath, bytes), relativePath)
                   ? ProjectAssetCatalogRowId{rowIndex}
                   : ProjectAssetCatalogRowId{};
    }

    bool ProjectAssetCatalog::GetFolderInfo(const ProjectAssetCatalogFolderId id, ProjectAssetCatalogFolderInfo& info) const noexcept
    {
        info = {};
        if (m_impl == nullptr || !id.IsValid() || id.value >= m_impl->snapshot.folderCount)
            return false;
        const CatalogFolder& folder = m_impl->snapshot.folders[id.value];
        info = {id, folder.parent, folder.root, folder.depth, folder.childCount,
                folder.directCount, folder.subtreeCount};
        return true;
    }

    containers::StringView ProjectAssetCatalog::GetFolderName(const ProjectAssetCatalogFolderId id) const noexcept
    {
        if (m_impl == nullptr || !id.IsValid() || id.value >= m_impl->snapshot.folderCount)
            return {};
        const CatalogFolder& folder = m_impl->snapshot.folders[id.value];
        return m_impl->snapshot.Text(folder.nameOffset, folder.nameLength);
    }

    bool ProjectAssetCatalog::CopyFolderRelativePath(const ProjectAssetCatalogFolderId id,
                                                     const containers::ArraySpan<char> output,
                                                     u32& writtenBytes) const noexcept
    {
        writtenBytes = 0u;
        return m_impl != nullptr &&
               CopyCatalogFolderRelativePath(m_impl->snapshot, id, output, writtenBytes);
    }

    ProjectAssetCatalogFolderId ProjectAssetCatalog::GetRootFolder(const u32 root) const noexcept
    {
        return m_impl != nullptr && root < m_impl->snapshot.rootCount ? m_impl->snapshot.rootFolders[root]
                                                                      : InvalidProjectAssetCatalogFolderId;
    }

    ProjectAssetCatalogFolderId ProjectAssetCatalog::ResolveFolder(const u32 root,
                                                                    const containers::StringView relativeFolder) const noexcept
    {
        ProjectAssetCatalogFolderId current = GetRootFolder(root);
        if (!current.IsValid() || relativeFolder.Empty())
            return current;
        u32 start = 0u;
        while (start < relativeFolder.Length())
        {
            while (start < relativeFolder.Length() && IsSeparator(relativeFolder[start]))
                ++start;
            if (start == relativeFolder.Length())
                break;
            u32 end = start;
            while (end < relativeFolder.Length() && !IsSeparator(relativeFolder[end]))
                ++end;
            const containers::StringView segment = relativeFolder.Slice(start, end);
            const CatalogFolder& parent = m_impl->snapshot.folders[current.value];
            u32 lower = 0u;
            u32 upper = parent.childCount;
            while (lower < upper)
            {
                const u32 middle = lower + (upper - lower) / 2u;
                const ProjectAssetCatalogFolderId child =
                    m_impl->snapshot.childFolders[parent.childStart + middle];
                const CatalogFolder& folder = m_impl->snapshot.folders[child.value];
                const int order =
                    CompareIgnoreCase(m_impl->snapshot.Text(folder.nameOffset, folder.nameLength), segment);
                if (order < 0)
                    lower = middle + 1u;
                else
                    upper = middle;
            }
            if (lower >= parent.childCount)
                return InvalidProjectAssetCatalogFolderId;
            const ProjectAssetCatalogFolderId found =
                m_impl->snapshot.childFolders[parent.childStart + lower];
            if (!SameFolderName(m_impl->snapshot, m_impl->snapshot.folders[found.value], segment))
                return InvalidProjectAssetCatalogFolderId;
            current = found;
            start = end + 1u;
        }
        return current;
    }

    u32 ProjectAssetCatalog::GetRootCount() const noexcept
    {
        return m_impl != nullptr ? m_impl->snapshot.rootCount : 0u;
    }

    u32 ProjectAssetCatalog::VisitChildFolders(const ProjectAssetCatalogFolderId parent, const u32 firstChild,
                                               const u32 maximumFolders,
                                               const VisitProjectAssetCatalogFolder visitor,
                                               void* const userData) const noexcept
    {
        if (m_impl == nullptr || visitor == nullptr || maximumFolders == 0u ||
            !parent.IsValid() || parent.value >= m_impl->snapshot.folderCount)
            return 0u;
        const CatalogFolder& folder = m_impl->snapshot.folders[parent.value];
        if (firstChild >= folder.childCount)
            return 0u;
        const u32 count = std::min(maximumFolders, folder.childCount - firstChild);
        u32 visited = 0u;
        for (; visited < count; ++visited)
        {
            const ProjectAssetCatalogFolderId child =
                m_impl->snapshot.childFolders[folder.childStart + firstChild + visited];
            ProjectAssetCatalogFolderInfo info;
            if (!GetFolderInfo(child, info))
                break;
            if (!visitor(info, GetFolderName(child), userData))
                return visited + 1u;
        }
        return visited;
    }

    u32 ProjectAssetCatalog::VisitFolderRows(const ProjectAssetCatalogFolderId folder,
                                             const VisitProjectAssetCatalogRow visitor, void* const userData) const noexcept
    {
        if (m_impl == nullptr || visitor == nullptr || !folder.IsValid() || folder.value >= m_impl->snapshot.folderCount)
            return 0u;
        const CatalogFolder& selected = m_impl->snapshot.folders[folder.value];
        u32 visited = 0u;
        for (u32 index = 0u; index < selected.directCount; ++index)
        {
            const u32 rowIndex = m_impl->snapshot.directRows[selected.directStart + index];
            ++visited;
            if (!visitor(ProjectAssetCatalogRowId{rowIndex}, m_impl->snapshot.rows[rowIndex], userData))
                break;
        }
        return visited;
    }

    ProjectAssetCatalogResult ProjectAssetCatalog::BeginQuery(const ProjectAssetCatalogQuery& query,
                                                               ProjectAssetCatalogQueryResult& result) const noexcept
    {
        result.Reset();
        if (m_impl == nullptr || m_impl->snapshot.generation == 0u ||
            query.text.Length() >= MaximumProjectAssetCatalogSearchBytes)
        {
            result.m_status.state = ProjectAssetCatalogQueryState::Failed;
            return ProjectAssetCatalogResult::InvalidArgument;
        }
        const CatalogSnapshot& snapshot = m_impl->snapshot;
        const CatalogFolder* selected = nullptr;
        if (query.folder.IsValid())
        {
            if (query.folder.value >= snapshot.folderCount)
            {
                result.m_status.state = ProjectAssetCatalogQueryState::Failed;
                return ProjectAssetCatalogResult::InvalidArgument;
            }
            selected = &snapshot.folders[query.folder.value];
        }

        result.m_folder = query.folder;
        result.m_recursive = query.recursive;
        result.m_requiredIssues = query.requiredIssues;
        result.m_excludedIssues = query.excludedIssues;
        result.m_sourceType = query.sourceType;
        result.m_primaryOutputType = query.primaryOutputType;
        result.m_importState = query.importState;
        result.m_referenceScanState = query.referenceScanState;
        result.m_filterImportState = query.filterImportState;
        result.m_filterReferenceScanState = query.filterReferenceScanState;
        result.m_textBytes = static_cast<u16>(query.text.Length());
        for (u32 index = 0u; index < result.m_textBytes; ++index)
            result.m_text[index] = CanonicalSearchByte(query.text[index]);
        result.m_text[result.m_textBytes] = '\0';
        if (result.m_textBytes != 0u)
        {
            result.m_textPrefix[0] = 0u;
            u16 prefix = 0u;
            for (u16 index = 1u; index < result.m_textBytes; ++index)
            {
                while (prefix != 0u && result.m_text[index] != result.m_text[prefix])
                    prefix = result.m_textPrefix[prefix - 1u];
                if (result.m_text[index] == result.m_text[prefix])
                    ++prefix;
                result.m_textPrefix[index] = prefix;
            }
        }
        result.m_candidateOffset = selected == nullptr ? 0u : query.recursive ? selected->subtreeStart : selected->directStart;
        result.m_status.candidates = selected == nullptr ? snapshot.rowCount
                                                         : query.recursive ? selected->subtreeCount : selected->directCount;
        result.m_status.sourceGeneration = snapshot.generation;
        result.m_passthrough = result.m_textBytes == 0u && result.m_requiredIssues == 0u &&
                               result.m_excludedIssues == 0u && !result.m_filterImportState &&
                               !result.m_filterReferenceScanState &&
                               result.m_sourceType == resources::InvalidResourceTypeId &&
                               result.m_primaryOutputType == resources::InvalidResourceTypeId;
        if (result.m_passthrough)
        {
            result.m_matchBitCapacity = 0u;
            result.m_matchWordCount = 0u;
            result.m_matchBlockCount = 0u;
            result.m_status.scanned = result.m_status.candidates;
            result.m_status.matches = result.m_status.candidates;
            result.m_status.state = ProjectAssetCatalogQueryState::Complete;
            result.m_status.allocatedBytes = result.m_matches ? static_cast<u32>(result.m_matches.size) : 0u;
            result.m_catalogIdentity = m_impl;
            result.m_catalogIdentityCookie = m_impl->identityCookie;
            result.m_countedActive = true;
            static_cast<void>(g_projectAssetCatalogActiveQueries.fetch_add(1u, std::memory_order_acq_rel));
            return ProjectAssetCatalogResult::Success;
        }

        constexpr u32 MatchWordsPerBlock = 64u;
        const u32 words = (result.m_status.candidates + 63u) / 64u;
        const u32 blocks = (words + MatchWordsPerBlock - 1u) / MatchWordsPerBlock;
        const usize wordBytes = static_cast<usize>(words) * sizeof(u64);
        const usize bytes = wordBytes + static_cast<usize>(blocks) * sizeof(u32);
        if (bytes > ProjectAssetCatalogQueryBudgetBytes)
        {
            result.m_status.state = ProjectAssetCatalogQueryState::Failed;
            return ProjectAssetCatalogResult::LimitExceeded;
        }
        if (result.m_matches.size < bytes)
        {
            const u64 previousBytes = result.m_accountedMatchBytes;
            const u64 requestedBytes = static_cast<u64>(bytes);
            const u64 additionalBytes = requestedBytes > previousBytes ? requestedBytes - previousBytes : 0u;
            if (!ReserveQueryBytes(additionalBytes))
            {
                result.m_status.state = ProjectAssetCatalogQueryState::Failed;
                return ProjectAssetCatalogResult::LimitExceeded;
            }
            if (result.m_matches)
            {
                if (!memory::Reallocate(result.m_matches, bytes, alignof(u64)))
                {
                    ReleaseQueryBytes(additionalBytes);
                    result.m_status.state = ProjectAssetCatalogQueryState::Failed;
                    return ProjectAssetCatalogResult::OutOfMemory;
                }
            }
            else if (bytes != 0u)
            {
                result.m_matches = memory::Allocate(memory::PoolId::Assets, bytes, alignof(u64));
                if (!result.m_matches)
                {
                    ReleaseQueryBytes(additionalBytes);
                    result.m_status.state = ProjectAssetCatalogQueryState::Failed;
                    return ProjectAssetCatalogResult::OutOfMemory;
                }
            }
            result.m_accountedMatchBytes += additionalBytes;
        }
        result.m_matchBitCapacity = result.m_status.candidates;
        result.m_matchWordCount = words;
        result.m_matchBlockCount = blocks;
        if (result.m_matches && result.m_matches.size != 0u)
            std::memset(result.m_matches.address, 0, result.m_matches.size);
        result.m_status.allocatedBytes = result.m_matches ? static_cast<u32>(result.m_matches.size) : 0u;
        result.m_status.state = result.m_status.candidates == 0u ? ProjectAssetCatalogQueryState::Complete
                                                                 : ProjectAssetCatalogQueryState::Running;
        result.m_catalogIdentity = m_impl;
        result.m_catalogIdentityCookie = m_impl->identityCookie;
        result.m_countedActive = true;
        static_cast<void>(g_projectAssetCatalogActiveQueries.fetch_add(1u, std::memory_order_acq_rel));
        return ProjectAssetCatalogResult::Success;
    }

    ProjectAssetCatalogResult ProjectAssetCatalog::StepQuery(ProjectAssetCatalogQueryResult& result,
                                                              const u32 maximumRows,
                                                              const u32 maximumPathBytes) const noexcept
    {
        if (m_impl == nullptr || result.m_catalogIdentity != m_impl ||
            result.m_catalogIdentityCookie != m_impl->identityCookie || maximumRows == 0u ||
            (result.m_textBytes != 0u && maximumPathBytes < resources::MaximumResourcePathBytes) ||
            result.m_status.state == ProjectAssetCatalogQueryState::Idle ||
            result.m_status.state == ProjectAssetCatalogQueryState::Failed)
            return ProjectAssetCatalogResult::InvalidArgument;
        if (result.m_status.sourceGeneration != m_impl->snapshot.generation)
        {
            result.m_status.state = ProjectAssetCatalogQueryState::Stale;
            return ProjectAssetCatalogResult::StaleSource;
        }
        if (result.m_status.state == ProjectAssetCatalogQueryState::Complete)
            return ProjectAssetCatalogResult::Success;

        const u64 beginTicks = system::GetMonotonicTicks();
        const CatalogSnapshot& snapshot = m_impl->snapshot;
        const bool direct = result.m_folder.IsValid() && !result.m_recursive;
        u32 processed = 0u;
        u32 pathBytesProcessed = 0u;
        char relativePath[resources::MaximumResourcePathBytes + 1u]{};
        while (processed < maximumRows && result.m_cursor < result.m_status.candidates)
        {
            const u32 candidate = result.m_candidateOffset + result.m_cursor;
            const u32 rowIndex = direct ? snapshot.directRows[candidate] : snapshot.sortedRows[candidate];
            const ProjectAssetCatalogRow& row = snapshot.rows[rowIndex];
            bool matches =
                (row.issues & result.m_requiredIssues) == result.m_requiredIssues &&
                (row.issues & result.m_excludedIssues) == 0u &&
                (!result.m_filterImportState || row.importState == result.m_importState) &&
                (!result.m_filterReferenceScanState || row.referenceScanState == result.m_referenceScanState) &&
                (result.m_sourceType == resources::InvalidResourceTypeId || row.sourceType == result.m_sourceType) &&
                (result.m_primaryOutputType == resources::InvalidResourceTypeId ||
                 row.primaryOutputType == result.m_primaryOutputType);
            if (matches && result.m_textBytes != 0u)
            {
                const u32 pathBytes = RelativePathBytes(snapshot, row);
                if (pathBytes == 0u)
                    matches = false;
                else
                {
                    if (pathBytesProcessed > maximumPathBytes - pathBytes)
                        break;
                    const u32 copied = CopyCatalogRelativePath(
                        snapshot, row, {relativePath, static_cast<u32>(sizeof(relativePath))});
                    if (copied != pathBytes)
                        matches = false;
                    else
                        matches = ContainsCompiled(containers::StringView(relativePath, copied),
                                                   result.m_text, result.m_textPrefix,
                                                   result.m_textBytes);
                    pathBytesProcessed += pathBytes;
                }
            }
            if (matches)
            {
                auto* const words = static_cast<u64*>(result.m_matches.address);
                auto* const blockCounts = reinterpret_cast<u32*>(
                    static_cast<u8*>(result.m_matches.address) +
                    static_cast<usize>(result.m_matchWordCount) * sizeof(u64));
                const u32 ordinal = result.m_cursor;
                words[ordinal >> 6u] |= u64{1} << (ordinal & 63u);
                ++blockCounts[(ordinal >> 6u) / 64u];
                ++result.m_status.matches;
            }
            ++result.m_cursor;
            ++processed;
        }
        result.m_status.scanned = result.m_cursor;
        result.m_status.scannedPathBytes += pathBytesProcessed;
        result.m_status.lastStepNanoseconds =
            ElapsedNanoseconds(beginTicks, system::GetMonotonicTicks());
        result.m_status.totalProcessingNanoseconds += result.m_status.lastStepNanoseconds;
        if (result.m_cursor == result.m_status.candidates)
            result.m_status.state = ProjectAssetCatalogQueryState::Complete;
        return ProjectAssetCatalogResult::Success;
    }

    u32 ProjectAssetCatalog::VisitQueryMatches(const ProjectAssetCatalogQueryResult& result, const u32 firstMatch,
                                               const u32 maximumMatches, const VisitProjectAssetCatalogRow visitor,
                                               void* const userData) const noexcept
    {
        if (m_impl == nullptr || result.m_catalogIdentity != m_impl ||
            result.m_catalogIdentityCookie != m_impl->identityCookie ||
            visitor == nullptr || maximumMatches == 0u ||
            result.m_status.sourceGeneration != m_impl->snapshot.generation ||
            firstMatch >= result.m_status.matches)
            return 0u;
        if (result.m_passthrough)
        {
            const bool direct = result.m_folder.IsValid() && !result.m_recursive;
            const u32 count = std::min(maximumMatches, result.m_status.matches - firstMatch);
            u32 visited = 0u;
            for (; visited < count; ++visited)
            {
                const u32 candidate = result.m_candidateOffset + firstMatch + visited;
                const u32 rowIndex = direct ? m_impl->snapshot.directRows[candidate]
                                            : m_impl->snapshot.sortedRows[candidate];
                if (!visitor(ProjectAssetCatalogRowId{rowIndex}, m_impl->snapshot.rows[rowIndex], userData))
                    return visited + 1u;
            }
            return visited;
        }
        if (result.m_matchBitCapacity < result.m_status.candidates ||
            result.m_matchWordCount != (result.m_status.candidates + 63u) / 64u)
            return 0u;
        const u64* const words = static_cast<const u64*>(result.m_matches.address);
        if (words == nullptr)
            return 0u;
        const u32 wordCount = result.m_matchWordCount;
        constexpr u32 MatchWordsPerBlock = 64u;
        const u32* const blockCounts = reinterpret_cast<const u32*>(
            static_cast<const u8*>(result.m_matches.address) +
            static_cast<usize>(wordCount) * sizeof(u64));
        u32 ordinalBase = 0u;
        u32 blockIndex = 0u;
        for (; blockIndex < result.m_matchBlockCount; ++blockIndex)
        {
            const u32 count = blockCounts[blockIndex];
            if (firstMatch < ordinalBase + count)
                break;
            ordinalBase += count;
        }
        if (blockIndex >= result.m_matchBlockCount)
            return 0u;
        u32 wordIndex = blockIndex * MatchWordsPerBlock;
        const u32 blockWordEnd = std::min(wordCount, wordIndex + MatchWordsPerBlock);
        u64 word = 0u;
        for (; wordIndex < blockWordEnd; ++wordIndex)
        {
            word = words[wordIndex];
            const u32 count = static_cast<u32>(std::popcount(word));
            if (firstMatch < ordinalBase + count)
                break;
            ordinalBase += count;
        }
        if (wordIndex >= wordCount)
            return 0u;
        u32 skip = firstMatch - ordinalBase;
        while (skip != 0u)
        {
            word &= word - 1u;
            --skip;
        }

        u32 visited = 0u;
        while (wordIndex < wordCount && visited < maximumMatches)
        {
            while (word != 0u && visited < maximumMatches)
            {
                const u32 bit = static_cast<u32>(std::countr_zero(word));
                const u32 ordinal = wordIndex * 64u + bit;
                word &= word - 1u;
                if (ordinal >= result.m_status.candidates)
                    break;
                const u32 candidate = result.m_candidateOffset + ordinal;
                const bool direct = result.m_folder.IsValid() && !result.m_recursive;
                const u32 rowIndex = direct ? m_impl->snapshot.directRows[candidate]
                                            : m_impl->snapshot.sortedRows[candidate];
                ++visited;
                if (!visitor(ProjectAssetCatalogRowId{rowIndex}, m_impl->snapshot.rows[rowIndex], userData))
                    return visited;
            }
            ++wordIndex;
            word = wordIndex < wordCount ? words[wordIndex] : 0u;
        }
        return visited;
    }
} // namespace vanguard::asset_streaming
