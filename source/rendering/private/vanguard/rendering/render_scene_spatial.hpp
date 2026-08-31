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

    struct Cell
    {
        Cell() noexcept;

        i32 x = 0;
        i32 y = 0;
        i32 z = 0;
        i32 nextInBucket = -1;
        u32 activeIndex = ~u32{0};
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
        containers::DynamicArray<u32> activeCellIndices;
        containers::DynamicArray<u32> dirtyCellIndices;
        containers::DynamicArray<RenderProxyHandle> unindexedProxies;
    };

    enum class InsertResult : u8
    {
        Inserted,
        KeptUnindexed,
        Rejected
    };

    using ResolveProxyBoundsFunction = bool (*)(void* userData, RenderProxyHandle proxy, RenderProxyBounds& bounds) noexcept;
    using ValidateProxyEntryFunction = bool (*)(void* userData, RenderProxyHandle proxy, EntryHandle& entry, RenderProxyBounds& bounds) noexcept;
    struct VisibilityProxyReadView
    {
        const RenderProxyBounds* bounds = nullptr;
        const RenderProxyVisibilityFlags* visibility = nullptr;
        const u64* layerMask = nullptr;
        const u32* visibilityMask = nullptr;
        const RenderProxyPayloadKind* payloadKind = nullptr;
        const GpuInstanceIndex* gpuInstanceIndex = nullptr;

        [[nodiscard]] bool IsValid() const noexcept
        {
            return bounds != nullptr && visibility != nullptr && layerMask != nullptr && visibilityMask != nullptr && payloadKind != nullptr;
        }
    };

    using ResolveVisibilityProxyFunction = bool (*)(void* userData, RenderProxyHandle proxy, VisibilityProxyReadView& view) noexcept;

    void Reset(WriteIndex& index, const SpatialWriteIndexConfig& config, CounterDelta* delta = nullptr) noexcept;
    [[nodiscard]] bool BoundsInFiniteExtent(const WriteIndex& index, const RenderProxyBounds& bounds) noexcept;
    [[nodiscard]] bool BoundsAccepted(const WriteIndex& index, const RenderProxyBounds& bounds) noexcept;
    [[nodiscard]] InsertResult Insert(WriteIndex& index, RenderProxyHandle proxy, const RenderProxyBounds& bounds, RenderProxySpatialMode mode,
                                      EntryHandle& entry, CounterDelta* delta = nullptr) noexcept;
    void Remove(WriteIndex& index, EntryHandle& entry, RemoveResult* result = nullptr, CounterDelta* delta = nullptr) noexcept;
    void Move(WriteIndex& index, RenderProxyHandle proxy, const RenderProxyBounds& oldBounds, const RenderProxyBounds& newBounds, RenderProxySpatialMode mode,
              EntryHandle& entry, RemoveResult* result = nullptr, CounterDelta* delta = nullptr) noexcept;
    /// Returns true only when the object must enter the serialized structural-move lane.
    /// A false result means the live proxy bounds remain covered by the current cell aggregate.
    [[nodiscard]] bool QuickConditionalMove(const WriteIndex& index, const EntryHandle& entry, const RenderProxyBounds& newBounds) noexcept;
    void Repair(WriteIndex& index, void* userData, ResolveProxyBoundsFunction resolveBounds, CounterDelta* delta = nullptr) noexcept;
    [[nodiscard]] bool Validate(const WriteIndex& index, void* userData, ValidateProxyEntryFunction validateProxy,
                                SpatialWriteIndexStats* stats = nullptr) noexcept;
    void BuildBatches(u32 cellCount, u32 targetCellsPerBatch, containers::DynamicArray<VisibilityQueryBatch>& batches, VisibilityQueryPlan& plan) noexcept;
    void BuildLiveBatches(const WriteIndex& index, RenderSceneHandle scene, u64 mutationEpoch, u32 targetCellsPerBatch,
                          containers::DynamicArray<VisibilityQueryBatch>& batches, VisibilityQueryPlan& plan) noexcept;
    [[nodiscard]] bool BuildGpuCandidateBatches(const WriteIndex& index, RenderSceneHandle scene, u64 mutationEpoch, u64 planSerial,
                                                u32 targetCandidatesPerBatch, containers::ArraySpan<RenderSceneGpuCandidateBatch> batchStorage,
                                                RenderSceneGpuCandidatePlan& plan) noexcept;
    void CollectLive(const WriteIndex& index, const VisibilityQueryRequest& request, void* userData, ResolveVisibilityProxyFunction resolveProxy,
                     containers::DynamicArray<RenderProxyHandle>& proxies, VisibilityQueryResult& result) noexcept;
    void CollectLiveRange(const WriteIndex& index, const VisibilityQueryRequest& request, const VisibilityQueryBatch& batch, void* userData,
                          ResolveVisibilityProxyFunction resolveProxy, containers::DynamicArray<RenderProxyHandle>& proxies,
                          VisibilityQueryResult& result) noexcept;
    void WriteGpuCandidateRange(const WriteIndex& index, const VisibilityQueryRequest& request, const RenderSceneGpuCandidateBatch& batch, void* userData,
                                ResolveVisibilityProxyFunction resolveProxy, GpuInstanceIndex* destination, GpuVisibilityCandidateRange& range,
                                RenderSceneGpuCandidateBatchResult& result) noexcept;
} // namespace vanguard::rendering::spatial
