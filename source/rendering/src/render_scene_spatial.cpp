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

        void AddBounds(RenderProxyBounds& aggregate, const RenderProxyBounds& bounds,
                       const bool first) noexcept
        {
            if (first)
            {
                aggregate = bounds;
                return;
            }
            for (u32 axis = 0; axis < 3; ++axis)
            {
                if (bounds.minimum[axis] < aggregate.minimum[axis]) aggregate.minimum[axis] = bounds.minimum[axis];
                if (bounds.maximum[axis] > aggregate.maximum[axis]) aggregate.maximum[axis] = bounds.maximum[axis];
            }
        }

        [[nodiscard]] bool BoundsOverlap(const RenderProxyBounds& left,
                                         const RenderProxyBounds& right) noexcept
        {
            return left.minimum[0] <= right.maximum[0] && left.maximum[0] >= right.minimum[0] &&
                   left.minimum[1] <= right.maximum[1] && left.maximum[1] >= right.minimum[1] &&
                   left.minimum[2] <= right.maximum[2] && left.maximum[2] >= right.minimum[2];
        }

        [[nodiscard]] bool BoundsIntersectsFrustum(const RenderProxyBounds& bounds,
                                                   const VisibilityFrustum& frustum) noexcept
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

        [[nodiscard]] bool HasAllFlags(const RenderProxyVisibilityFlags value,
                                       const RenderProxyVisibilityFlags flags) noexcept
        {
            return (static_cast<u32>(value) & static_cast<u32>(flags)) == static_cast<u32>(flags);
        }

        [[nodiscard]] bool HasAnyFlags(const RenderProxyVisibilityFlags value,
                                       const RenderProxyVisibilityFlags flags) noexcept
        {
            return (static_cast<u32>(value) & static_cast<u32>(flags)) != 0;
        }

        [[nodiscard]] bool PayloadMatches(const RenderProxyPayloadKind payload,
                                          const VisibilityQueryPayloadFilter filter) noexcept
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
            if (delta == nullptr) return;
            delta->activeEntries += value.activeEntries;
            delta->dirtyCells += value.dirtyCells;
            delta->fastMoves += value.fastMoves;
            delta->structuralMoves += value.structuralMoves;
            delta->repairedCells += value.repairedCells;
            delta->outOfRangeProxies += value.outOfRangeProxies;
        }

        void ComputeCellKey(const WriteIndex& index, const RenderProxyBounds& bounds,
                             i32& x, i32& y, i32& z) noexcept
        {
            const f64 centerX = (static_cast<f64>(bounds.minimum[0]) + bounds.maximum[0]) * 0.5;
            const f64 centerY = (static_cast<f64>(bounds.minimum[1]) + bounds.maximum[1]) * 0.5;
            const f64 centerZ = (static_cast<f64>(bounds.minimum[2]) + bounds.maximum[2]) * 0.5;
            x = FloorToI32((centerX - index.config.origin[0]) / index.config.cellSize);
            y = FloorToI32((centerY - index.config.origin[1]) / index.config.cellSize);
            z = FloorToI32((centerZ - index.config.origin[2]) / index.config.cellSize);
        }

        [[nodiscard]] bool CellKeyRepresentable(const WriteIndex& index,
                                                const RenderProxyBounds& bounds) noexcept
        {
            for (u32 axis = 0; axis < 3; ++axis)
            {
                const f64 center = (static_cast<f64>(bounds.minimum[axis]) + bounds.maximum[axis]) * 0.5;
                const f64 coordinate = (center - index.config.origin[axis]) / index.config.cellSize;
                if (coordinate < static_cast<f64>(std::numeric_limits<i32>::min()) ||
                    coordinate >= static_cast<f64>(std::numeric_limits<i32>::max()))
                    return false;
            }
            return true;
        }

        void RebuildBuckets(WriteIndex& index) noexcept
        {
            u32 bucketCount = 64;
            while (bucketCount < index.cells.Size() * 2u) bucketCount *= 2u;
            index.buckets.Resize(bucketCount);
            for (u32 bucket = 0; bucket < index.buckets.Size(); ++bucket) index.buckets[bucket] = -1;
            for (u32 cellIndex = 0; cellIndex < index.cells.Size(); ++cellIndex)
            {
                Cell& cell = index.cells[cellIndex];
                if (!cell.allocated)
                {
                    cell.nextInBucket = -1;
                    continue;
                }
                const u32 bucket = HashCellKey(cell.x, cell.y, cell.z) & (index.buckets.Size() - 1u);
                cell.nextInBucket = index.buckets[bucket];
                index.buckets[bucket] = static_cast<i32>(cellIndex);
            }
        }

        [[nodiscard]] u32 FindCell(const WriteIndex& index,
                                   const i32 x, const i32 y, const i32 z) noexcept
        {
            if (index.buckets.Size() == 0) return ~u32{0};
            const u32 bucket = HashCellKey(x, y, z) & (index.buckets.Size() - 1u);
            i32 cellIndex = index.buckets[bucket];
            while (cellIndex >= 0)
            {
                const Cell& cell = index.cells[static_cast<u32>(cellIndex)];
                if (cell.x == x && cell.y == y && cell.z == z) return static_cast<u32>(cellIndex);
                cellIndex = cell.nextInBucket;
            }
            return ~u32{0};
        }

        [[nodiscard]] u32 FindOrCreateCell(WriteIndex& index,
                                            const i32 x, const i32 y, const i32 z) noexcept
        {
            u32 cellIndex = FindCell(index, x, y, z);
            if (cellIndex != ~u32{0}) return cellIndex;
            if (index.buckets.Size() == 0) RebuildBuckets(index);
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
            cell.aggregate = {};
            cell.proxies.Clear();
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
                if (delta != nullptr) --delta->dirtyCells;
            }
            cell.allocated = false;
            cell.occupied = false;
            cell.nextInBucket = -1;
            cell.aggregate = {};
            cell.proxies.Clear();
            index.freeCells.PushBack(cellIndex);
            --index.stats.cells;
        }

        void MarkCellDirty(WriteIndex& index, Cell& cell, CounterDelta* const delta) noexcept
        {
            if (cell.dirty) return;
            cell.dirty = true;
            ++index.stats.dirtyCells;
            if (delta != nullptr) ++delta->dirtyCells;
        }

        [[nodiscard]] const RenderProxySnapshot* ResolvePublishedProxy(
            const containers::DynamicArray<u32>& proxyLookup,
            const containers::DynamicArray<RenderProxySnapshot>& publishedProxies,
            const RenderProxyHandle proxy) noexcept
        {
            if (proxy.index >= proxyLookup.Size()) return nullptr;
            const u32 publishedIndex = proxyLookup[proxy.index];
            if (publishedIndex >= publishedProxies.Size()) return nullptr;
            const RenderProxySnapshot& snapshot = publishedProxies[publishedIndex];
            return snapshot.handle == proxy ? &snapshot : nullptr;
        }
    } // namespace

    Cell::Cell() noexcept
        : proxies(memory::pools::Rendering::GetInstance())
    {
    }

    WriteIndex::WriteIndex() noexcept
        : cells(memory::pools::Rendering::GetInstance()),
          buckets(memory::pools::Rendering::GetInstance()),
          freeCells(memory::pools::Rendering::GetInstance()),
          unindexedProxies(memory::pools::Rendering::GetInstance())
    {
    }

    void Reset(WriteIndex& index, const SpatialWriteIndexConfig& config,
               CounterDelta* const delta) noexcept
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
        index.unindexedProxies.Clear();
        index.buckets.Resize(64);
        for (u32 bucket = 0; bucket < index.buckets.Size(); ++bucket) index.buckets[bucket] = -1;
    }

    bool BoundsInFiniteExtent(const WriteIndex& index, const RenderProxyBounds& bounds) noexcept
    {
        if (!index.config.HasFiniteExtent()) return true;
        for (u32 axis = 0; axis < 3; ++axis)
        {
            const f32 min = index.config.origin[axis];
            const f32 max = min + static_cast<f32>(index.config.cellsPerAxis[axis]) * index.config.cellSize;
            if (bounds.minimum[axis] < min || bounds.maximum[axis] > max) return false;
        }
        return true;
    }

    bool BoundsAccepted(const WriteIndex& index, const RenderProxyBounds& bounds) noexcept
    {
        if (!index.stats.valid) return false;
        if (BoundsInFiniteExtent(index, bounds) && CellKeyRepresentable(index, bounds)) return true;
        return index.config.outOfRangePolicy == SpatialOutOfRangePolicy::KeepUnindexed;
    }

    InsertResult Insert(WriteIndex& index, const RenderProxyHandle proxy,
                        const RenderProxyBounds& bounds, const RenderProxySpatialMode mode,
                        EntryHandle& entry, CounterDelta* const delta) noexcept
    {
        if (mode != RenderProxySpatialMode::Bounds) return InsertResult::KeptUnindexed;
        if (!BoundsInFiniteExtent(index, bounds) || !CellKeyRepresentable(index, bounds))
        {
            ++index.stats.outOfRangeProxies;
            if (delta != nullptr) ++delta->outOfRangeProxies;
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
            if (delta != nullptr) ++delta->activeEntries;
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
        if (!cell.occupied)
        {
            cell.occupied = true;
            ++index.stats.occupiedCells;
        }
        MarkCellDirty(index, cell, delta);
        ++index.stats.activeEntries;
        ++index.stats.insertedEntries;
        if (delta != nullptr) ++delta->activeEntries;
        return InsertResult::Inserted;
    }

    void Remove(WriteIndex& index, EntryHandle& entry, RemoveResult* const result,
                CounterDelta* const delta) noexcept
    {
        if (result != nullptr) *result = {};
        if (!entry.IsValid()) return;
        if (entry.unindexed)
        {
            if (entry.objectIndex >= index.unindexedProxies.Size()) return;
            const u32 removedIndex = entry.objectIndex;
            const u32 lastIndex = index.unindexedProxies.Size() - 1u;
            const RenderProxyHandle moved = index.unindexedProxies[lastIndex];
            index.unindexedProxies[removedIndex] = moved;
            index.unindexedProxies.PopBack();
            if (removedIndex != lastIndex && result != nullptr)
            {
                result->movedProxy = moved;
                result->movedObjectIndex = removedIndex;
                result->movedProxyRelocated = true;
            }
            entry = {};
            --index.stats.activeEntries;
            ++index.stats.removedEntries;
            if (delta != nullptr) --delta->activeEntries;
            return;
        }
        if (entry.cellIndex >= index.cells.Size()) return;
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
            MarkCellDirty(index, cell, delta);
        }
        entry = {};
        --index.stats.activeEntries;
        ++index.stats.removedEntries;
        if (delta != nullptr) --delta->activeEntries;
    }

    void Move(WriteIndex& index, const RenderProxyHandle proxy,
              const RenderProxyBounds& oldBounds, const RenderProxyBounds& newBounds,
              const RenderProxySpatialMode mode, EntryHandle& entry,
              RemoveResult* const result, CounterDelta* const delta) noexcept
    {
        if (result != nullptr) *result = {};
        if (mode != RenderProxySpatialMode::Bounds)
        {
            Remove(index, entry, result, delta);
            return;
        }
        if (!BoundsInFiniteExtent(index, newBounds) || !CellKeyRepresentable(index, newBounds))
        {
            if (index.config.outOfRangePolicy == SpatialOutOfRangePolicy::KeepUnindexed)
            {
                if (entry.unindexed) return;
                CounterDelta structuralDelta;
                Remove(index, entry, result, &structuralDelta);
                static_cast<void>(Insert(index, proxy, newBounds, mode, entry, &structuralDelta));
                ++index.stats.structuralMoves;
                ++structuralDelta.structuralMoves;
                AddDelta(delta, structuralDelta);
                return;
            }
            ++index.stats.outOfRangeProxies;
            if (delta != nullptr) ++delta->outOfRangeProxies;
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
            if (entry.cellIndex < index.cells.Size()) MarkCellDirty(index, index.cells[entry.cellIndex], delta);
            ++index.stats.fastMoves;
            if (delta != nullptr) ++delta->fastMoves;
            return;
        }
        CounterDelta structuralDelta;
        Remove(index, entry, result, &structuralDelta);
        static_cast<void>(Insert(index, proxy, newBounds, mode, entry, &structuralDelta));
        ++index.stats.structuralMoves;
        ++structuralDelta.structuralMoves;
        AddDelta(delta, structuralDelta);
    }

    void Repair(WriteIndex& index, void* const userData,
                const ResolveProxyBoundsFunction resolveBounds,
                CounterDelta* const delta) noexcept
    {
        if (resolveBounds == nullptr) return;
        for (u32 cellIndex = 0; cellIndex < index.cells.Size(); ++cellIndex)
        {
            Cell& cell = index.cells[cellIndex];
            if (!cell.allocated) continue;
            if (!cell.dirty) continue;
            RenderProxyBounds aggregate;
            bool first = true;
            for (u32 proxyIndex = 0; proxyIndex < cell.proxies.Size(); ++proxyIndex)
            {
                RenderProxyBounds bounds;
                if (!resolveBounds(userData, cell.proxies[proxyIndex], bounds)) continue;
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
    }

    bool Validate(const WriteIndex& index, void* const userData,
                  const ValidateProxyEntryFunction validateProxy,
                  SpatialWriteIndexStats* const outStats) noexcept
    {
        SpatialWriteIndexStats copy = index.stats;
        copy.valid = index.stats.valid;
        u32 countedEntries = 0;
        u32 allocatedCells = 0;
        u32 occupiedCells = 0;
        u32 dirtyCells = 0;
        if (validateProxy == nullptr) return false;
        for (u32 cellIndex = 0; cellIndex < index.cells.Size(); ++cellIndex)
        {
            const Cell& cell = index.cells[cellIndex];
            if (!cell.allocated)
            {
                if (cell.proxies.Size() != 0 || cell.dirty || cell.occupied) return false;
                continue;
            }
            ++allocatedCells;
            if (cell.dirty) ++dirtyCells;
            if (cell.proxies.Size() != 0) ++occupiedCells;
            for (u32 objectIndex = 0; objectIndex < cell.proxies.Size(); ++objectIndex)
            {
                EntryHandle entry;
                RenderProxyBounds bounds;
                const RenderProxyHandle proxy = cell.proxies[objectIndex];
                if (!validateProxy(userData, proxy, entry, bounds)) return false;
                if (entry.cellIndex != cellIndex || entry.objectIndex != objectIndex) return false;
                if (!BoundsInFiniteExtent(index, bounds)) return false;
                i32 x = 0;
                i32 y = 0;
                i32 z = 0;
                ComputeCellKey(index, bounds, x, y, z);
                if (cell.x != x || cell.y != y || cell.z != z) return false;
                ++countedEntries;
            }
        }
        countedEntries += index.unindexedProxies.Size();
        for (u32 objectIndex = 0; objectIndex < index.unindexedProxies.Size(); ++objectIndex)
        {
            EntryHandle entry;
            RenderProxyBounds bounds;
            if (!validateProxy(userData, index.unindexedProxies[objectIndex], entry, bounds)) return false;
            if (!entry.unindexed || entry.objectIndex != objectIndex) return false;
            if (BoundsInFiniteExtent(index, bounds) && CellKeyRepresentable(index, bounds)) return false;
        }
        copy.activeEntries = countedEntries;
        copy.cells = index.stats.cells;
        copy.occupiedCells = occupiedCells;
        copy.dirtyCells = dirtyCells;
        if (outStats != nullptr) *outStats = copy;
        return countedEntries == index.stats.activeEntries &&
               allocatedCells == index.stats.cells &&
               occupiedCells == index.stats.occupiedCells &&
               dirtyCells == index.stats.dirtyCells;
    }

    void PublishCells(const WriteIndex& index, const containers::DynamicArray<u32>& proxyLookup,
                       const containers::DynamicArray<RenderProxySnapshot>& publishedProxies,
                       containers::DynamicArray<CellSnapshot>& cells,
                       containers::DynamicArray<RenderProxyHandle>& cellProxies) noexcept
    {
        cells.Clear();
        cellProxies.Clear();
        for (u32 cellIndex = 0; cellIndex < index.cells.Size(); ++cellIndex)
        {
            const Cell& cell = index.cells[cellIndex];
            if (!cell.allocated || cell.proxies.Size() == 0) continue;
            cells.PushBack({});
            CellSnapshot& snapshot = cells[cells.Size() - 1u];
            snapshot.x = cell.x;
            snapshot.y = cell.y;
            snapshot.z = cell.z;
            snapshot.aggregate = cell.aggregate;
            snapshot.firstProxy = cellProxies.Size();
            for (u32 proxyIndex = 0; proxyIndex < cell.proxies.Size(); ++proxyIndex)
            {
                const RenderProxyHandle proxy = cell.proxies[proxyIndex];
                if (ResolvePublishedProxy(proxyLookup, publishedProxies, proxy) != nullptr) cellProxies.PushBack(proxy);
            }
            snapshot.proxyCount = cellProxies.Size() - snapshot.firstProxy;
        }
        if (index.unindexedProxies.Size() != 0)
        {
            cells.PushBack({});
            CellSnapshot& snapshot = cells[cells.Size() - 1u];
            snapshot.firstProxy = cellProxies.Size();
            snapshot.alwaysTraverse = true;
            for (u32 proxyIndex = 0; proxyIndex < index.unindexedProxies.Size(); ++proxyIndex)
            {
                const RenderProxyHandle proxy = index.unindexedProxies[proxyIndex];
                if (ResolvePublishedProxy(proxyLookup, publishedProxies, proxy) != nullptr) cellProxies.PushBack(proxy);
            }
            snapshot.proxyCount = cellProxies.Size() - snapshot.firstProxy;
        }
    }

    void BuildBatches(const u32 cellCount, const u32 targetCellsPerBatch,
                      containers::DynamicArray<VisibilityQueryBatch>& batches,
                      VisibilityQueryPlan& plan) noexcept
    {
        batches.Clear();
        plan.cellCount = cellCount;
        plan.batchSize = targetCellsPerBatch != 0 ? targetCellsPerBatch : cellCount;
        if (plan.batchSize == 0) plan.batchSize = 1;
        for (u32 firstCell = 0; firstCell < cellCount;)
        {
            const u32 remaining = cellCount - firstCell;
            const u32 count = remaining < plan.batchSize ? remaining : plan.batchSize;
            VisibilityQueryBatch batch;
            batch.scene = plan.scene;
            batch.version = plan.version;
            batch.firstCell = firstCell;
            batch.cellCount = count;
            batches.PushBack(batch);
            firstCell += count;
        }
        plan.batchCount = batches.Size();
    }

    void CollectRange(const VisibilityQueryRequest& request,
                       const containers::DynamicArray<CellSnapshot>& cells,
                       const containers::DynamicArray<RenderProxyHandle>& cellProxies,
                       const containers::DynamicArray<u32>& proxyLookup,
                      const containers::DynamicArray<RenderProxySnapshot>& publishedProxies,
                      const VisibilityQueryBatch batch,
                      containers::DynamicArray<RenderProxyHandle>& proxies,
                      VisibilityQueryResult& result) noexcept
    {
        proxies.Clear();
        result = {};
        result.scene = request.lease.scene;
        result.version = request.lease.version;
        if (!batch.IsValid() || batch.firstCell >= cells.Size())
        {
            result.completed = true;
            return;
        }
        const u32 availableCells = cells.Size() - batch.firstCell;
        const u32 endCell = batch.firstCell + (batch.cellCount < availableCells ? batch.cellCount : availableCells);
        for (u32 cellIndex = batch.firstCell; cellIndex < endCell; ++cellIndex)
        {
            const CellSnapshot& cell = cells[cellIndex];
            if (!cell.alwaysTraverse && request.useBounds && !BoundsOverlap(cell.aggregate, request.bounds))
            {
                ++result.rejectedCellsByBounds;
                continue;
            }
            if (!cell.alwaysTraverse && request.useFrustum && !BoundsIntersectsFrustum(cell.aggregate, request.frustum))
            {
                ++result.rejectedCellsByFrustum;
                continue;
            }
            ++result.visitedCells;
            if (cell.firstProxy > cellProxies.Size() || cell.proxyCount > cellProxies.Size() - cell.firstProxy)
                continue;
            const u32 proxyEnd = cell.firstProxy + cell.proxyCount;
            for (u32 proxyIndex = cell.firstProxy; proxyIndex < proxyEnd; ++proxyIndex)
            {
                ++result.candidateProxies;
                const RenderProxySnapshot* const proxy =
                    ResolvePublishedProxy(proxyLookup, publishedProxies, cellProxies[proxyIndex]);
                if (proxy == nullptr) continue;
                if (request.useBounds && !BoundsOverlap(proxy->bounds, request.bounds))
                {
                    ++result.rejectedByBounds;
                    continue;
                }
                if (request.useFrustum && !BoundsIntersectsFrustum(proxy->bounds, request.frustum))
                {
                    ++result.rejectedByFrustum;
                    continue;
                }
                if ((proxy->layerMask & request.layerMask) == 0)
                {
                    ++result.rejectedByLayer;
                    continue;
                }
                if ((proxy->visibilityMask & request.visibilityMask) == 0 ||
                    !HasAllFlags(proxy->visibility, request.requiredFlags) ||
                    HasAnyFlags(proxy->visibility, request.excludedFlags))
                {
                    ++result.rejectedByVisibility;
                    continue;
                }
                if (!PayloadMatches(proxy->payloadKind, request.payloadFilter))
                {
                    ++result.rejectedByPayload;
                    continue;
                }
                if (result.acceptedProxies >= request.maximumResults)
                {
                    ++result.overflowedProxies;
                    continue;
                }
                proxies.PushBack(proxy->handle);
                ++result.acceptedProxies;
            }
        }
        result.completed = result.overflowedProxies == 0;
    }

    void Collect(const VisibilityQueryRequest& request,
                  const containers::DynamicArray<CellSnapshot>& cells,
                  const containers::DynamicArray<RenderProxyHandle>& cellProxies,
                  const containers::DynamicArray<u32>& proxyLookup,
                 const containers::DynamicArray<RenderProxySnapshot>& publishedProxies,
                 containers::DynamicArray<RenderProxyHandle>& proxies,
                 VisibilityQueryResult& result) noexcept
    {
        VisibilityQueryBatch batch;
        batch.scene = request.lease.scene;
        batch.version = request.lease.version;
        batch.firstCell = 0;
        batch.cellCount = cells.Size();
        CollectRange(request, cells, cellProxies, proxyLookup, publishedProxies, batch, proxies, result);
    }
} // namespace vanguard::rendering::spatial
