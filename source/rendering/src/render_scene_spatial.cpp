#include <vanguard/rendering/render_scene_spatial.hpp>

#include <vanguard/memory/pool.hpp>

#include <limits>

namespace vanguard::rendering::spatial
{
    namespace
    {
        [[nodiscard]] i32 FloorToI32(const f64 value) noexcept
        {
            const i32 truncated = static_cast<i32>(value);
            return static_cast<f64>(truncated) > value ? truncated - 1 : truncated;
        }

        [[nodiscard]] u32 HashCellKey(const i32 x, const i32 y, const i32 z) noexcept
        {
            const u32 ux = static_cast<u32>(x);
            const u32 uy = static_cast<u32>(y);
            const u32 uz = static_cast<u32>(z);
            return (ux * 73856093u) ^ (uy * 19349663u) ^ (uz * 83492791u);
        }

        void AddBounds(RenderProxyBounds& aggregate, const RenderProxyBounds& bounds, const bool first) noexcept
        {
            if (first)
            {
                aggregate = bounds;
                return;
            }
            for (u32 axis = 0; axis < 3; ++axis)
            {
                if (bounds.minimum[axis] < aggregate.minimum[axis])
                    aggregate.minimum[axis] = bounds.minimum[axis];
                if (bounds.maximum[axis] > aggregate.maximum[axis])
                    aggregate.maximum[axis] = bounds.maximum[axis];
            }
        }

        [[nodiscard]] bool BoundsOverlap(const RenderProxyBounds& left, const RenderProxyBounds& right) noexcept
        {
            return left.minimum[0] <= right.maximum[0] && left.maximum[0] >= right.minimum[0] && left.minimum[1] <= right.maximum[1] &&
                   left.maximum[1] >= right.minimum[1] && left.minimum[2] <= right.maximum[2] && left.maximum[2] >= right.minimum[2];
        }

        [[nodiscard]] bool BoundsContains(const RenderProxyBounds& outer, const RenderProxyBounds& inner) noexcept
        {
            return outer.minimum[0] <= inner.minimum[0] && outer.maximum[0] >= inner.maximum[0] && outer.minimum[1] <= inner.minimum[1] &&
                   outer.maximum[1] >= inner.maximum[1] && outer.minimum[2] <= inner.minimum[2] && outer.maximum[2] >= inner.maximum[2];
        }

        [[nodiscard]] bool BoundsIntersectsFrustum(const RenderProxyBounds& bounds, const VisibilityFrustum& frustum) noexcept
        {
            for (u32 planeIndex = 0; planeIndex < frustum.planeCount; ++planeIndex)
            {
                const VisibilityPlane& plane = frustum.planes[planeIndex];
                const f32 x = plane.normal[0] >= 0.0f ? bounds.maximum[0] : bounds.minimum[0];
                const f32 y = plane.normal[1] >= 0.0f ? bounds.maximum[1] : bounds.minimum[1];
                const f32 z = plane.normal[2] >= 0.0f ? bounds.maximum[2] : bounds.minimum[2];
                if (plane.normal[0] * x + plane.normal[1] * y + plane.normal[2] * z + plane.distance < 0.0f)
                    return false;
            }
            return true;
        }

        [[nodiscard]] bool HasAllFlags(const RenderProxyVisibilityFlags value, const RenderProxyVisibilityFlags flags) noexcept
        {
            return (static_cast<u32>(value) & static_cast<u32>(flags)) == static_cast<u32>(flags);
        }

        [[nodiscard]] bool HasAnyFlags(const RenderProxyVisibilityFlags value, const RenderProxyVisibilityFlags flags) noexcept
        {
            return (static_cast<u32>(value) & static_cast<u32>(flags)) != 0;
        }

        [[nodiscard]] bool PayloadMatches(const RenderProxyPayloadKind payload, const VisibilityQueryPayloadFilter filter) noexcept
        {
            switch (filter)
            {
            case VisibilityQueryPayloadFilter::Any:
                return true;
            case VisibilityQueryPayloadFilter::Mesh:
                return payload == RenderProxyPayloadKind::Mesh;
            case VisibilityQueryPayloadFilter::Light:
                return payload == RenderProxyPayloadKind::Light;
            case VisibilityQueryPayloadFilter::Decal:
                return payload == RenderProxyPayloadKind::Decal;
            case VisibilityQueryPayloadFilter::None:
                return payload == RenderProxyPayloadKind::None;
            }
            return false;
        }

        void AddDelta(CounterDelta* const delta, const CounterDelta& value) noexcept
        {
            if (delta == nullptr)
                return;
            delta->activeEntries += value.activeEntries;
            delta->dirtyCells += value.dirtyCells;
            delta->fastMoves += value.fastMoves;
            delta->structuralMoves += value.structuralMoves;
            delta->repairedCells += value.repairedCells;
            delta->outOfRangeProxies += value.outOfRangeProxies;
        }

        void ComputeCellKey(const WriteIndex& index, const RenderProxyBounds& bounds, i32& x, i32& y, i32& z) noexcept
        {
            const f64 centerX = (static_cast<f64>(bounds.minimum[0]) + bounds.maximum[0]) * 0.5;
            const f64 centerY = (static_cast<f64>(bounds.minimum[1]) + bounds.maximum[1]) * 0.5;
            const f64 centerZ = (static_cast<f64>(bounds.minimum[2]) + bounds.maximum[2]) * 0.5;
            x = FloorToI32((centerX - index.config.origin[0]) / index.config.cellSize);
            y = FloorToI32((centerY - index.config.origin[1]) / index.config.cellSize);
            z = FloorToI32((centerZ - index.config.origin[2]) / index.config.cellSize);
        }

        [[nodiscard]] bool CellKeyRepresentable(const WriteIndex& index, const RenderProxyBounds& bounds) noexcept
        {
            for (u32 axis = 0; axis < 3; ++axis)
            {
                const f64 center = (static_cast<f64>(bounds.minimum[axis]) + bounds.maximum[axis]) * 0.5;
                const f64 coordinate = (center - index.config.origin[axis]) / index.config.cellSize;
                if (coordinate < static_cast<f64>(std::numeric_limits<i32>::min()) || coordinate >= static_cast<f64>(std::numeric_limits<i32>::max()))
                    return false;
            }
            return true;
        }

        void RebuildBuckets(WriteIndex& index) noexcept
        {
            u32 bucketCount = 64;
            while (bucketCount < index.activeCellIndices.Size() * 2u)
                bucketCount *= 2u;
            index.buckets.Resize(bucketCount);
            for (u32 bucket = 0; bucket < index.buckets.Size(); ++bucket)
                index.buckets[bucket] = -1;
            for (const u32 cellIndex : index.activeCellIndices)
            {
                Cell& cell = index.cells[cellIndex];
                const u32 bucket = HashCellKey(cell.x, cell.y, cell.z) & (index.buckets.Size() - 1u);
                cell.nextInBucket = index.buckets[bucket];
                index.buckets[bucket] = static_cast<i32>(cellIndex);
            }
        }

        [[nodiscard]] u32 FindCell(const WriteIndex& index, const i32 x, const i32 y, const i32 z) noexcept
        {
            if (index.buckets.Size() == 0)
                return ~u32{0};
            const u32 bucket = HashCellKey(x, y, z) & (index.buckets.Size() - 1u);
            i32 cellIndex = index.buckets[bucket];
            while (cellIndex >= 0)
            {
                const Cell& cell = index.cells[static_cast<u32>(cellIndex)];
                if (cell.x == x && cell.y == y && cell.z == z)
                    return static_cast<u32>(cellIndex);
                cellIndex = cell.nextInBucket;
            }
            return ~u32{0};
        }

        [[nodiscard]] u32 FindOrCreateCell(WriteIndex& index, const i32 x, const i32 y, const i32 z) noexcept
        {
            u32 cellIndex = FindCell(index, x, y, z);
            if (cellIndex != ~u32{0})
                return cellIndex;
            if (index.buckets.Size() == 0)
                RebuildBuckets(index);
            if (index.freeCells.Size() != 0)
            {
                cellIndex = index.freeCells[index.freeCells.Size() - 1u];
                index.freeCells.PopBack();
            }
            else
            {
                index.cells.PushBack({});
                cellIndex = index.cells.Size() - 1u;
            }
            Cell& cell = index.cells[cellIndex];
            cell.x = x;
            cell.y = y;
            cell.z = z;
            cell.allocated = true;
            cell.occupied = false;
            cell.dirty = false;
            cell.activeIndex = index.activeCellIndices.Size();
            cell.aggregate = {};
            cell.proxies.Clear();
            index.activeCellIndices.PushBack(cellIndex);
            ++index.stats.cells;
            if (index.stats.cells * 2u > index.buckets.Size())
            {
                RebuildBuckets(index);
            }
            else
            {
                const u32 bucket = HashCellKey(x, y, z) & (index.buckets.Size() - 1u);
                cell.nextInBucket = index.buckets[bucket];
                index.buckets[bucket] = static_cast<i32>(cellIndex);
            }
            return cellIndex;
        }

        void ReleaseCell(WriteIndex& index, const u32 cellIndex, CounterDelta* const delta) noexcept
        {
            Cell& cell = index.cells[cellIndex];
            const u32 bucket = HashCellKey(cell.x, cell.y, cell.z) & (index.buckets.Size() - 1u);
            i32* link = &index.buckets[bucket];
            while (*link >= 0)
            {
                if (static_cast<u32>(*link) == cellIndex)
                {
                    *link = cell.nextInBucket;
                    break;
                }
                link = &index.cells[static_cast<u32>(*link)].nextInBucket;
            }
            if (cell.dirty)
            {
                cell.dirty = false;
                --index.stats.dirtyCells;
                if (delta != nullptr)
                    --delta->dirtyCells;
            }
            const u32 lastActiveIndex = index.activeCellIndices.Size() - 1u;
            const u32 movedCellIndex = index.activeCellIndices[lastActiveIndex];
            index.activeCellIndices[cell.activeIndex] = movedCellIndex;
            index.cells[movedCellIndex].activeIndex = cell.activeIndex;
            index.activeCellIndices.PopBack();
            cell.activeIndex = ~u32{0};
            cell.allocated = false;
            cell.occupied = false;
            cell.nextInBucket = -1;
            cell.aggregate = {};
            cell.proxies.Clear();
            index.freeCells.PushBack(cellIndex);
            --index.stats.cells;
        }

        void MarkCellDirty(WriteIndex& index, const u32 cellIndex, CounterDelta* const delta) noexcept
        {
            Cell& cell = index.cells[cellIndex];
            if (cell.dirty)
                return;
            cell.dirty = true;
            index.dirtyCellIndices.PushBack(cellIndex);
            ++index.stats.dirtyCells;
            if (delta != nullptr)
                ++delta->dirtyCells;
        }

    } // namespace

    Cell::Cell() noexcept : proxies(memory::pools::Rendering::GetInstance()) {}

    WriteIndex::WriteIndex() noexcept
        : cells(memory::pools::Rendering::GetInstance()), buckets(memory::pools::Rendering::GetInstance()), freeCells(memory::pools::Rendering::GetInstance()),
          activeCellIndices(memory::pools::Rendering::GetInstance()), dirtyCellIndices(memory::pools::Rendering::GetInstance()),
          unindexedProxies(memory::pools::Rendering::GetInstance()), globalProxies(memory::pools::Rendering::GetInstance())
    {
    }

    void Reset(WriteIndex& index, const SpatialWriteIndexConfig& config, CounterDelta* const delta) noexcept
    {
        if (delta != nullptr)
        {
            delta->activeEntries -= static_cast<i32>(index.stats.activeEntries);
            delta->dirtyCells -= static_cast<i32>(index.stats.dirtyCells);
        }
        index.config = config;
        index.stats = {};
        index.stats.valid = config.cellSize > 0.0f;
        index.cells.Clear();
        index.buckets.Clear();
        index.freeCells.Clear();
        index.activeCellIndices.Clear();
        index.dirtyCellIndices.Clear();
        index.unindexedProxies.Clear();
        index.globalProxies.Clear();
        index.buckets.Resize(64);
        for (u32 bucket = 0; bucket < index.buckets.Size(); ++bucket)
            index.buckets[bucket] = -1;
    }

    bool BoundsInFiniteExtent(const WriteIndex& index, const RenderProxyBounds& bounds) noexcept
    {
        if (!index.config.HasFiniteExtent())
            return true;
        for (u32 axis = 0; axis < 3; ++axis)
        {
            const f32 min = index.config.origin[axis];
            const f32 max = min + static_cast<f32>(index.config.cellsPerAxis[axis]) * index.config.cellSize;
            if (bounds.minimum[axis] < min || bounds.maximum[axis] > max)
                return false;
        }
        return true;
    }

    bool BoundsAccepted(const WriteIndex& index, const RenderProxyBounds& bounds) noexcept
    {
        if (!index.stats.valid)
            return false;
        if (BoundsInFiniteExtent(index, bounds) && CellKeyRepresentable(index, bounds))
            return true;
        return index.config.outOfRangePolicy == SpatialOutOfRangePolicy::KeepUnindexed;
    }

    InsertResult Insert(WriteIndex& index, const RenderProxyHandle proxy, const RenderProxyBounds& bounds, const RenderProxySpatialMode mode,
                        EntryHandle& entry, CounterDelta* const delta) noexcept
    {
        if (mode == RenderProxySpatialMode::None)
            return InsertResult::KeptUnindexed;
        if (mode == RenderProxySpatialMode::Global)
        {
            entry = {};
            entry.objectIndex = index.globalProxies.Size();
            entry.unindexed = true;
            entry.global = true;
            index.globalProxies.PushBack(proxy);
            ++index.stats.activeEntries;
            ++index.stats.insertedEntries;
            if (delta != nullptr)
                ++delta->activeEntries;
            return InsertResult::KeptUnindexed;
        }
        if (!BoundsInFiniteExtent(index, bounds) || !CellKeyRepresentable(index, bounds))
        {
            ++index.stats.outOfRangeProxies;
            if (delta != nullptr)
                ++delta->outOfRangeProxies;
            if (index.config.outOfRangePolicy == SpatialOutOfRangePolicy::RejectProxy)
            {
                entry = {};
                return InsertResult::Rejected;
            }
            entry.cellIndex = ~u32{0};
            entry.objectIndex = index.unindexedProxies.Size();
            entry.unindexed = true;
            index.unindexedProxies.PushBack(proxy);
            ++index.stats.activeEntries;
            ++index.stats.insertedEntries;
            if (delta != nullptr)
                ++delta->activeEntries;
            return InsertResult::KeptUnindexed;
        }
        i32 x = 0;
        i32 y = 0;
        i32 z = 0;
        ComputeCellKey(index, bounds, x, y, z);
        const u32 cellIndex = FindOrCreateCell(index, x, y, z);
        Cell& cell = index.cells[cellIndex];
        entry.cellIndex = cellIndex;
        entry.objectIndex = cell.proxies.Size();
        entry.unindexed = false;
        cell.proxies.PushBack(proxy);
        AddBounds(cell.aggregate, bounds, cell.proxies.Size() == 1u);
        if (!cell.occupied)
        {
            cell.occupied = true;
            ++index.stats.occupiedCells;
        }
        ++index.stats.activeEntries;
        ++index.stats.insertedEntries;
        if (delta != nullptr)
            ++delta->activeEntries;
        return InsertResult::Inserted;
    }

    void Remove(WriteIndex& index, EntryHandle& entry, RemoveResult* const result, CounterDelta* const delta) noexcept
    {
        if (result != nullptr)
            *result = {};
        if (!entry.IsValid())
            return;
        if (entry.unindexed)
        {
            auto& proxies = entry.global ? index.globalProxies : index.unindexedProxies;
            if (entry.objectIndex >= proxies.Size())
                return;
            const u32 removedIndex = entry.objectIndex;
            const u32 lastIndex = proxies.Size() - 1u;
            const RenderProxyHandle moved = proxies[lastIndex];
            proxies[removedIndex] = moved;
            proxies.PopBack();
            if (removedIndex != lastIndex && result != nullptr)
            {
                result->movedProxy = moved;
                result->movedObjectIndex = removedIndex;
                result->movedProxyRelocated = true;
            }
            entry = {};
            --index.stats.activeEntries;
            ++index.stats.removedEntries;
            if (delta != nullptr)
                --delta->activeEntries;
            return;
        }
        if (entry.cellIndex >= index.cells.Size())
            return;
        Cell& cell = index.cells[entry.cellIndex];
        if (entry.objectIndex < cell.proxies.Size())
        {
            const u32 removedIndex = entry.objectIndex;
            const u32 lastIndex = cell.proxies.Size() - 1u;
            const RenderProxyHandle moved = cell.proxies[lastIndex];
            cell.proxies[removedIndex] = moved;
            cell.proxies.PopBack();
            if (removedIndex != lastIndex && result != nullptr)
            {
                result->movedProxy = moved;
                result->movedObjectIndex = removedIndex;
                result->movedProxyRelocated = true;
            }
        }
        if (cell.occupied && cell.proxies.Size() == 0)
        {
            cell.occupied = false;
            --index.stats.occupiedCells;
            ReleaseCell(index, entry.cellIndex, delta);
        }
        else
        {
            MarkCellDirty(index, entry.cellIndex, delta);
        }
        entry = {};
        --index.stats.activeEntries;
        ++index.stats.removedEntries;
        if (delta != nullptr)
            --delta->activeEntries;
    }

    void Move(WriteIndex& index, const RenderProxyHandle proxy, const RenderProxyBounds& oldBounds, const RenderProxyBounds& newBounds,
              const RenderProxySpatialMode mode, EntryHandle& entry, RemoveResult* const result, CounterDelta* const delta) noexcept
    {
        if (result != nullptr)
            *result = {};
        if (mode == RenderProxySpatialMode::None)
        {
            Remove(index, entry, result, delta);
            return;
        }
        if (mode == RenderProxySpatialMode::Global)
        {
            if (entry.IsValid() && entry.global)
            {
                entry.global = true;
                return;
            }
            Remove(index, entry, result, delta);
            static_cast<void>(Insert(index, proxy, newBounds, mode, entry, delta));
            return;
        }
        if (entry.global)
        {
            Remove(index, entry, result, delta);
            static_cast<void>(Insert(index, proxy, newBounds, mode, entry, delta));
            return;
        }
        if (!BoundsInFiniteExtent(index, newBounds) || !CellKeyRepresentable(index, newBounds))
        {
            if (index.config.outOfRangePolicy == SpatialOutOfRangePolicy::KeepUnindexed)
            {
                if (entry.unindexed)
                    return;
                CounterDelta structuralDelta;
                Remove(index, entry, result, &structuralDelta);
                static_cast<void>(Insert(index, proxy, newBounds, mode, entry, &structuralDelta));
                ++index.stats.structuralMoves;
                ++structuralDelta.structuralMoves;
                AddDelta(delta, structuralDelta);
                return;
            }
            ++index.stats.outOfRangeProxies;
            if (delta != nullptr)
                ++delta->outOfRangeProxies;
            Remove(index, entry, result, delta);
            return;
        }
        if (!entry.IsValid())
        {
            static_cast<void>(Insert(index, proxy, newBounds, mode, entry, delta));
            return;
        }
        if (entry.unindexed)
        {
            CounterDelta structuralDelta;
            Remove(index, entry, result, &structuralDelta);
            static_cast<void>(Insert(index, proxy, newBounds, mode, entry, &structuralDelta));
            ++index.stats.structuralMoves;
            ++structuralDelta.structuralMoves;
            AddDelta(delta, structuralDelta);
            return;
        }
        i32 oldX = 0;
        i32 oldY = 0;
        i32 oldZ = 0;
        i32 newX = 0;
        i32 newY = 0;
        i32 newZ = 0;
        ComputeCellKey(index, oldBounds, oldX, oldY, oldZ);
        ComputeCellKey(index, newBounds, newX, newY, newZ);
        if (oldX == newX && oldY == newY && oldZ == newZ)
        {
            if (entry.cellIndex < index.cells.Size())
                MarkCellDirty(index, entry.cellIndex, delta);
            ++index.stats.fastMoves;
            if (delta != nullptr)
                ++delta->fastMoves;
            return;
        }
        CounterDelta structuralDelta;
        Remove(index, entry, result, &structuralDelta);
        static_cast<void>(Insert(index, proxy, newBounds, mode, entry, &structuralDelta));
        ++index.stats.structuralMoves;
        ++structuralDelta.structuralMoves;
        AddDelta(delta, structuralDelta);
    }

    bool QuickConditionalMove(const WriteIndex& index, const EntryHandle& entry, const RenderProxyBounds& newBounds) noexcept
    {
        if (!entry.IsValid() || entry.unindexed || entry.cellIndex >= index.cells.Size())
            return true;
        const Cell& cell = index.cells[entry.cellIndex];
        if (!cell.allocated || entry.objectIndex >= cell.proxies.Size())
            return true;

        i32 x = 0;
        i32 y = 0;
        i32 z = 0;
        ComputeCellKey(index, newBounds, x, y, z);
        if (cell.x != x || cell.y != y || cell.z != z)
            return true;

        // The aggregate is intentionally conservative between repair boundaries. A proxy-local
        // bounds write is safe while it remains covered; expanding it requires serialized movement.
        return !BoundsContains(cell.aggregate, newBounds);
    }

    void Repair(WriteIndex& index, void* const userData, const ResolveProxyBoundsFunction resolveBounds, CounterDelta* const delta) noexcept
    {
        if (resolveBounds == nullptr)
            return;
        for (u32 dirtyIndex = 0; dirtyIndex < index.dirtyCellIndices.Size(); ++dirtyIndex)
        {
            const u32 cellIndex = index.dirtyCellIndices[dirtyIndex];
            if (cellIndex >= index.cells.Size())
                continue;
            Cell& cell = index.cells[cellIndex];
            if (!cell.allocated || !cell.dirty)
                continue;
            RenderProxyBounds aggregate;
            bool first = true;
            for (u32 proxyIndex = 0; proxyIndex < cell.proxies.Size(); ++proxyIndex)
            {
                RenderProxyBounds bounds;
                if (!resolveBounds(userData, cell.proxies[proxyIndex], bounds))
                    continue;
                AddBounds(aggregate, bounds, first);
                first = false;
            }
            cell.aggregate = aggregate;
            cell.dirty = false;
            --index.stats.dirtyCells;
            ++index.stats.repairedCells;
            if (delta != nullptr)
            {
                --delta->dirtyCells;
                ++delta->repairedCells;
            }
        }
        index.dirtyCellIndices.Clear();
    }

    bool Validate(const WriteIndex& index, void* const userData, const ValidateProxyEntryFunction validateProxy,
                  SpatialWriteIndexStats* const outStats) noexcept
    {
        SpatialWriteIndexStats copy = index.stats;
        copy.valid = index.stats.valid;
        u32 countedEntries = 0;
        u32 allocatedCells = 0;
        u32 occupiedCells = 0;
        u32 dirtyCells = 0;
        if (validateProxy == nullptr)
            return false;
        for (u32 cellIndex = 0; cellIndex < index.cells.Size(); ++cellIndex)
        {
            const Cell& cell = index.cells[cellIndex];
            if (!cell.allocated)
            {
                if (cell.proxies.Size() != 0 || cell.dirty || cell.occupied || cell.activeIndex != ~u32{0})
                    return false;
                continue;
            }
            if (cell.activeIndex >= index.activeCellIndices.Size() || index.activeCellIndices[cell.activeIndex] != cellIndex)
                return false;
            ++allocatedCells;
            if (cell.dirty)
                ++dirtyCells;
            if (cell.proxies.Size() != 0)
                ++occupiedCells;
            for (u32 objectIndex = 0; objectIndex < cell.proxies.Size(); ++objectIndex)
            {
                EntryHandle entry;
                RenderProxyBounds bounds;
                const RenderProxyHandle proxy = cell.proxies[objectIndex];
                if (!validateProxy(userData, proxy, entry, bounds))
                    return false;
                if (entry.cellIndex != cellIndex || entry.objectIndex != objectIndex)
                    return false;
                if (!BoundsInFiniteExtent(index, bounds))
                    return false;
                i32 x = 0;
                i32 y = 0;
                i32 z = 0;
                ComputeCellKey(index, bounds, x, y, z);
                if (cell.x != x || cell.y != y || cell.z != z)
                    return false;
                ++countedEntries;
            }
        }
        for (u32 membership = 0; membership < 2; ++membership)
        {
            const bool global = membership != 0;
            const auto& proxies = global ? index.globalProxies : index.unindexedProxies;
            countedEntries += proxies.Size();
            for (u32 objectIndex = 0; objectIndex < proxies.Size(); ++objectIndex)
            {
                EntryHandle entry;
                RenderProxyBounds bounds;
                const bool resolved = validateProxy(userData, proxies[objectIndex], entry, bounds);
                if (!resolved || !entry.unindexed || entry.global != global || entry.objectIndex != objectIndex)
                    return false;
                if (!global && BoundsInFiniteExtent(index, bounds) && CellKeyRepresentable(index, bounds))
                    return false;
            }
        }
        copy.activeEntries = countedEntries;
        copy.cells = index.stats.cells;
        copy.occupiedCells = occupiedCells;
        copy.dirtyCells = dirtyCells;
        if (outStats != nullptr)
            *outStats = copy;
        return countedEntries == index.stats.activeEntries && allocatedCells == index.stats.cells && allocatedCells == index.activeCellIndices.Size() &&
               occupiedCells == index.stats.occupiedCells && dirtyCells == index.stats.dirtyCells;
    }

    void BuildBatches(const u32 cellCount, const u32 targetCellsPerBatch, containers::DynamicArray<VisibilityQueryBatch>& batches,
                      VisibilityQueryPlan& plan) noexcept
    {
        batches.Clear();
        plan.cellCount = cellCount;
        plan.batchSize = targetCellsPerBatch != 0 ? targetCellsPerBatch : cellCount;
        if (plan.batchSize == 0)
            plan.batchSize = 1;
        for (u32 firstCell = 0; firstCell < cellCount;)
        {
            const u32 remaining = cellCount - firstCell;
            const u32 count = remaining < plan.batchSize ? remaining : plan.batchSize;
            VisibilityQueryBatch batch;
            batch.scene = plan.scene;
            batch.mutationEpoch = plan.mutationEpoch;
            batch.firstCell = firstCell;
            batch.cellCount = count;
            batches.PushBack(batch);
            firstCell += count;
        }
        plan.batchCount = batches.Size();
    }

    u32 TraversalCount(const WriteIndex& index) noexcept
    {
        return index.activeCellIndices.Size() + (index.unindexedProxies.Empty() ? 0u : 1u) + (index.globalProxies.Empty() ? 0u : 1u);
    }

    static const containers::DynamicArray<RenderProxyHandle>& TraversalProxies(const WriteIndex& index, const u32 traversal) noexcept
    {
        if (traversal < index.activeCellIndices.Size())
            return index.cells[index.activeCellIndices[traversal]].proxies;
        if (traversal == index.activeCellIndices.Size() && !index.unindexedProxies.Empty())
            return index.unindexedProxies;
        return index.globalProxies;
    }

    void BuildLiveBatches(const WriteIndex& index, const RenderSceneHandle scene, const u64 mutationEpoch, const u32 targetCellsPerBatch,
                          containers::DynamicArray<VisibilityQueryBatch>& batches, VisibilityQueryPlan& plan) noexcept
    {
        plan = {};
        plan.scene = scene;
        plan.mutationEpoch = mutationEpoch;
        const u32 traversalSlots = TraversalCount(index);
        BuildBatches(traversalSlots, targetCellsPerBatch, batches, plan);
    }

    bool PrepareGpuCandidatePlan(const WriteIndex& index, const RenderSceneHandle scene, const u64 mutationEpoch, const u64 planSerial,
                                 const u32 targetCandidatesPerBatch, RenderSceneGpuCandidatePlan& plan) noexcept
    {
        plan = {};
        plan.scene = scene;
        plan.mutationEpoch = mutationEpoch;
        plan.serial = planSerial;
        plan.targetCandidatesPerBatch = targetCandidatesPerBatch;
        if (targetCandidatesPerBatch == 0)
            return false;

        if (index.globalProxies.Size() > ~u32{0} - index.unindexedProxies.Size())
            return false;
        u32 traversalCandidateCount = index.unindexedProxies.Size() + index.globalProxies.Size();
        for (const u32 cellIndex : index.activeCellIndices)
        {
            const u32 cellCandidates = index.cells[cellIndex].proxies.Size();
            if (cellCandidates > ~u32{0} - traversalCandidateCount)
                return false;
            traversalCandidateCount += cellCandidates;
        }
        const u32 requiredBatches = traversalCandidateCount / targetCandidatesPerBatch + (traversalCandidateCount % targetCandidatesPerBatch != 0 ? 1u : 0u);
        plan.traversalCandidateCount = traversalCandidateCount;
        plan.requiredCandidateCapacity = traversalCandidateCount;
        plan.batchCount = requiredBatches;
        const u32 fullBatches = traversalCandidateCount / targetCandidatesPerBatch;
        const u32 tailCandidates = traversalCandidateCount % targetCandidatesPerBatch;
        const u32 fullBatchRanges = targetCandidatesPerBatch / GpuVisibilityThreadsPerGroup +
                                   (targetCandidatesPerBatch % GpuVisibilityThreadsPerGroup != 0 ? 1u : 0u);
        const u64 workRanges = static_cast<u64>(fullBatches) * fullBatchRanges + tailCandidates / GpuVisibilityThreadsPerGroup +
                               (tailCandidates % GpuVisibilityThreadsPerGroup != 0 ? 1u : 0u);
        if (workRanges > ~u32{0})
            return false;
        plan.requiredWorkRangeCapacity = static_cast<u32>(workRanges);
        return true;
    }

    bool BuildGpuCandidateBatches(const WriteIndex& index, const RenderSceneGpuCandidatePlan& plan,
                                  const containers::ArraySpan<RenderSceneGpuCandidateBatch> batchStorage) noexcept
    {
        if (!plan.IsValid() || plan.targetCandidatesPerBatch == 0 || plan.batchCount > batchStorage.Size())
            return false;

        const u32 traversalSourceCount = TraversalCount(index);
        u32 traversal = 0;
        u32 firstProxy = 0;
        u32 destinationOffset = 0;
        for (u32 batchIndex = 0; batchIndex < plan.batchCount; ++batchIndex)
        {
            const u32 remainingCandidates = plan.traversalCandidateCount - destinationOffset;
            const u32 batchCandidateCount = remainingCandidates < plan.targetCandidatesPerBatch ? remainingCandidates : plan.targetCandidatesPerBatch;
            batchStorage[batchIndex] = {plan.scene, plan.mutationEpoch, plan.serial, traversal, firstProxy, batchCandidateCount, destinationOffset};

            u32 advance = batchCandidateCount;
            while (advance != 0 && traversal < traversalSourceCount)
            {
                const auto& source = TraversalProxies(index, traversal);
                const u32 available = source.Size() - firstProxy;
                const u32 count = advance < available ? advance : available;
                firstProxy += count;
                advance -= count;
                if (firstProxy == source.Size())
                {
                    ++traversal;
                    firstProxy = 0;
                }
            }
            destinationOffset += batchCandidateCount;
        }
        return destinationOffset == plan.traversalCandidateCount;
    }

    void WriteGpuCandidateRange(const WriteIndex& index, const VisibilityQueryRequest& request, const RenderSceneGpuCandidateBatch& batch, void* const userData,
                                const ResolveVisibilityProxyFunction resolveProxy, GpuInstanceIndex* const destination, GpuVisibilityCandidateRange& range,
                                RenderSceneGpuCandidateBatchResult& result) noexcept
    {
        result = {};
        range = {};
        result.visibility.scene = request.scene;
        result.visibility.mutationEpoch = request.mutationEpoch;
        range.offset = batch.destinationOffset;
        if (!batch.IsValid() || resolveProxy == nullptr || destination == nullptr)
            return;

        const u32 traversalSourceCount = TraversalCount(index);
        u32 traversal = batch.firstTraversal;
        u32 firstProxy = batch.firstProxy;
        u32 remaining = batch.traversalCandidateCount;
        while (remaining != 0 && traversal < traversalSourceCount)
        {
            const Cell* cell = nullptr;
            const containers::DynamicArray<RenderProxyHandle>* source = nullptr;
            if (traversal < index.activeCellIndices.Size())
            {
                cell = &index.cells[index.activeCellIndices[traversal]];
                source = &cell->proxies;
            }
            else
            {
                source = &TraversalProxies(index, traversal);
            }

            const u32 available = source->Size() - firstProxy;
            const u32 sourceCount = remaining < available ? remaining : available;
            bool rejectedCell = false;
            if (cell != nullptr && request.useBounds && !BoundsOverlap(cell->aggregate, request.bounds))
            {
                if (firstProxy == 0)
                    ++result.visibility.rejectedCellsByBounds;
                rejectedCell = true;
            }
            else if (cell != nullptr && request.useFrustum && !BoundsIntersectsFrustum(cell->aggregate, request.frustum))
            {
                if (firstProxy == 0)
                    ++result.visibility.rejectedCellsByFrustum;
                rejectedCell = true;
            }

            if (!rejectedCell)
            {
                if (firstProxy == 0)
                    ++result.visibility.visitedCells;
                const u32 endProxy = firstProxy + sourceCount;
                for (u32 proxyIndex = firstProxy; proxyIndex < endProxy; ++proxyIndex)
                {
                    ++result.visibility.candidateProxies;
                    VisibilityProxyReadView view;
                    if (!resolveProxy(userData, (*source)[proxyIndex], view) || !view.IsValid())
                        continue;
                    if (!view.global && request.useBounds && !BoundsOverlap(*view.bounds, request.bounds))
                    {
                        ++result.visibility.rejectedByBounds;
                        continue;
                    }
                    if (!view.global && request.useFrustum && !BoundsIntersectsFrustum(*view.bounds, request.frustum))
                    {
                        ++result.visibility.rejectedByFrustum;
                        continue;
                    }
                    if ((*view.layerMask & request.layerMask) == 0)
                    {
                        ++result.visibility.rejectedByLayer;
                        continue;
                    }
                    if ((*view.visibilityMask & request.visibilityMask) == 0 || !HasAllFlags(*view.visibility, request.requiredFlags) ||
                        HasAnyFlags(*view.visibility, request.excludedFlags))
                    {
                        ++result.visibility.rejectedByVisibility;
                        continue;
                    }
                    if (*view.payloadKind != RenderProxyPayloadKind::Mesh)
                    {
                        ++result.visibility.rejectedByPayload;
                        continue;
                    }
                    if (view.gpuInstanceIndex == nullptr || *view.gpuInstanceIndex == InvalidGpuSceneIndex)
                    {
                        ++result.unresolvedGpuIdentities;
                        continue;
                    }
                    destination[result.visibility.acceptedProxies++] = *view.gpuInstanceIndex;
                }
            }

            remaining -= sourceCount;
            ++traversal;
            firstProxy = 0;
        }
        range.count = result.visibility.acceptedProxies;
        result.visibility.completed = remaining == 0;
        result.completed = remaining == 0 && result.unresolvedGpuIdentities == 0;
    }

    void CollectLiveRange(const WriteIndex& index, const VisibilityQueryRequest& request, const VisibilityQueryBatch& batch, void* const userData,
                          const ResolveVisibilityProxyFunction resolveProxy, containers::DynamicArray<RenderProxyHandle>& proxies,
                          VisibilityQueryResult& result) noexcept
    {
        proxies.Clear();
        result = {};
        result.scene = request.scene;
        result.mutationEpoch = request.mutationEpoch;
        if (!batch.IsValid() || resolveProxy == nullptr)
        {
            result.completed = true;
            return;
        }

        const u32 traversalSlots = TraversalCount(index);
        if (batch.firstCell >= traversalSlots)
        {
            result.completed = true;
            return;
        }
        const u32 available = traversalSlots - batch.firstCell;
        const u32 end = batch.firstCell + (batch.cellCount < available ? batch.cellCount : available);
        for (u32 traversalIndex = batch.firstCell; traversalIndex < end; ++traversalIndex)
        {
            const containers::DynamicArray<RenderProxyHandle>* cellProxies = nullptr;
            const Cell* cell = nullptr;
            if (traversalIndex < index.activeCellIndices.Size())
            {
                cell = &index.cells[index.activeCellIndices[traversalIndex]];
                if (request.useBounds && !BoundsOverlap(cell->aggregate, request.bounds))
                {
                    ++result.rejectedCellsByBounds;
                    continue;
                }
                if (request.useFrustum && !BoundsIntersectsFrustum(cell->aggregate, request.frustum))
                {
                    ++result.rejectedCellsByFrustum;
                    continue;
                }
                cellProxies = &cell->proxies;
            }
            else
            {
                cellProxies = &TraversalProxies(index, traversalIndex);
            }

            ++result.visitedCells;
            for (u32 proxyIndex = 0; proxyIndex < cellProxies->Size(); ++proxyIndex)
            {
                ++result.candidateProxies;
                const RenderProxyHandle proxy = (*cellProxies)[proxyIndex];
                VisibilityProxyReadView view;
                if (!resolveProxy(userData, proxy, view) || !view.IsValid())
                    continue;
                if (!view.global && request.useBounds && !BoundsOverlap(*view.bounds, request.bounds))
                {
                    ++result.rejectedByBounds;
                    continue;
                }
                if (!view.global && request.useFrustum && !BoundsIntersectsFrustum(*view.bounds, request.frustum))
                {
                    ++result.rejectedByFrustum;
                    continue;
                }
                if ((*view.layerMask & request.layerMask) == 0)
                {
                    ++result.rejectedByLayer;
                    continue;
                }
                if ((*view.visibilityMask & request.visibilityMask) == 0 || !HasAllFlags(*view.visibility, request.requiredFlags) ||
                    HasAnyFlags(*view.visibility, request.excludedFlags))
                {
                    ++result.rejectedByVisibility;
                    continue;
                }
                if (!PayloadMatches(*view.payloadKind, request.payloadFilter))
                {
                    ++result.rejectedByPayload;
                    continue;
                }
                if (result.acceptedProxies >= request.maximumResults)
                {
                    ++result.overflowedProxies;
                    continue;
                }
                proxies.PushBack(proxy);
                ++result.acceptedProxies;
            }
        }
        result.completed = result.overflowedProxies == 0;
    }

    void CollectLive(const WriteIndex& index, const VisibilityQueryRequest& request, void* const userData, const ResolveVisibilityProxyFunction resolveProxy,
                     containers::DynamicArray<RenderProxyHandle>& proxies, VisibilityQueryResult& result) noexcept
    {
        VisibilityQueryBatch batch;
        batch.scene = request.scene;
        batch.mutationEpoch = request.mutationEpoch;
        batch.firstCell = 0;
        batch.cellCount = TraversalCount(index);
        CollectLiveRange(index, request, batch, userData, resolveProxy, proxies, result);
    }
} // namespace vanguard::rendering::spatial
