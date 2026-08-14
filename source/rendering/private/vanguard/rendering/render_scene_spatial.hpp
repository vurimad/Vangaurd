#pragma once

#include <vanguard/rendering/render_scene.hpp>

namespace vanguard::rendering::spatial
{
    struct EntryHandle
    {
        u32 cellIndex = ~u32{0};
        u32 objectIndex = ~u32{0};
        bool unindexed = false;

        [[nodiscard]] bool IsValid() const noexcept
        {
            return objectIndex != ~u32{0} && (unindexed || cellIndex != ~u32{0});
        }
    };

    struct CounterDelta
    {
        i32 activeEntries = 0;
        i32 dirtyCells = 0;
        u64 fastMoves = 0;
        u64 structuralMoves = 0;
        u64 repairedCells = 0;
        u64 outOfRangeProxies = 0;
    };

    struct RemoveResult
    {
        RenderProxyHandle movedProxy;
        u32 movedObjectIndex = ~u32{0};
        bool movedProxyRelocated = false;
    };

    struct CellSnapshot
    {
        i32 x = 0;
        i32 y = 0;
        i32 z = 0;
        RenderProxyBounds aggregate;
        u32 firstProxy = 0;
        u32 proxyCount = 0;
        bool alwaysTraverse = false;
    };

    struct Cell
    {
        Cell() noexcept;

        i32 x = 0;
        i32 y = 0;
        i32 z = 0;
        i32 nextInBucket = -1;
        bool allocated = false;
        bool occupied = false;
        bool dirty = false;
        RenderProxyBounds aggregate;
        containers::DynamicArray<RenderProxyHandle> proxies;
    };

    struct WriteIndex
    {
        WriteIndex() noexcept;

        SpatialWriteIndexConfig config;
        SpatialWriteIndexStats stats;
        containers::DynamicArray<Cell> cells;
        containers::DynamicArray<i32> buckets;
        containers::DynamicArray<u32> freeCells;
        containers::DynamicArray<RenderProxyHandle> unindexedProxies;
    };

    enum class InsertResult : u8
    {
        Inserted,
        KeptUnindexed,
        Rejected
    };

    using ResolveProxyBoundsFunction = bool (*)(void* userData, RenderProxyHandle proxy,
                                                RenderProxyBounds& bounds) noexcept;
    using ValidateProxyEntryFunction = bool (*)(void* userData, RenderProxyHandle proxy,
                                                EntryHandle& entry, RenderProxyBounds& bounds) noexcept;

    void Reset(WriteIndex& index, const SpatialWriteIndexConfig& config,
               CounterDelta* delta = nullptr) noexcept;
    [[nodiscard]] bool BoundsInFiniteExtent(const WriteIndex& index,
                                            const RenderProxyBounds& bounds) noexcept;
    [[nodiscard]] bool BoundsAccepted(const WriteIndex& index,
                                      const RenderProxyBounds& bounds) noexcept;
    [[nodiscard]] InsertResult Insert(WriteIndex& index, RenderProxyHandle proxy,
                                      const RenderProxyBounds& bounds,
                                      RenderProxySpatialMode mode,
                                      EntryHandle& entry,
                                      CounterDelta* delta = nullptr) noexcept;
    void Remove(WriteIndex& index, EntryHandle& entry, RemoveResult* result = nullptr,
                CounterDelta* delta = nullptr) noexcept;
    void Move(WriteIndex& index, RenderProxyHandle proxy, const RenderProxyBounds& oldBounds,
              const RenderProxyBounds& newBounds, RenderProxySpatialMode mode,
              EntryHandle& entry, RemoveResult* result = nullptr,
              CounterDelta* delta = nullptr) noexcept;
    void Repair(WriteIndex& index, void* userData, ResolveProxyBoundsFunction resolveBounds,
                CounterDelta* delta = nullptr) noexcept;
    [[nodiscard]] bool Validate(const WriteIndex& index, void* userData,
                                ValidateProxyEntryFunction validateProxy,
                                SpatialWriteIndexStats* stats = nullptr) noexcept;
    void PublishCells(const WriteIndex& index, const containers::DynamicArray<u32>& proxyLookup,
                      const containers::DynamicArray<RenderProxySnapshot>& publishedProxies,
                      containers::DynamicArray<CellSnapshot>& cells,
                      containers::DynamicArray<RenderProxyHandle>& cellProxies) noexcept;
    void BuildBatches(u32 cellCount, u32 targetCellsPerBatch,
                      containers::DynamicArray<VisibilityQueryBatch>& batches,
                      VisibilityQueryPlan& plan) noexcept;
    void Collect(const VisibilityQueryRequest& request,
                 const containers::DynamicArray<CellSnapshot>& cells,
                 const containers::DynamicArray<RenderProxyHandle>& cellProxies,
                 const containers::DynamicArray<u32>& proxyLookup,
                 const containers::DynamicArray<RenderProxySnapshot>& publishedProxies,
                 containers::DynamicArray<RenderProxyHandle>& proxies,
                 VisibilityQueryResult& result) noexcept;
    void CollectRange(const VisibilityQueryRequest& request,
                      const containers::DynamicArray<CellSnapshot>& cells,
                      const containers::DynamicArray<RenderProxyHandle>& cellProxies,
                      const containers::DynamicArray<u32>& proxyLookup,
                      const containers::DynamicArray<RenderProxySnapshot>& publishedProxies,
                      VisibilityQueryBatch batch,
                      containers::DynamicArray<RenderProxyHandle>& proxies,
                      VisibilityQueryResult& result) noexcept;
} // namespace vanguard::rendering::spatial
