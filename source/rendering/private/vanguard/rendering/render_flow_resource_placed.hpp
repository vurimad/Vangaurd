#pragma once

#include <vanguard/memory/pool.hpp>
#include <vanguard/rendering/render_flow_resource_allocator.hpp>
#include "render_flow_resource_pool.hpp"
#include <vanguard/rhi/rhi_types.hpp>

namespace vanguard::rendering::detail
{
    struct PlacedRangePlannerConfig
    {
        u64 minimumHeapBytes = 64ull * 1024ull * 1024ull;
        u64 heapAlignment = 64ull * 1024ull;
    };

    struct PlacedRangeRequest
    {
        u32 allocation = InvalidRenderFlowResourceIndex;
        FrameResourceKind kind = FrameResourceKind::Invalid;
        rhi::QueueType queue = rhi::QueueType::Graphics;
        PlanPosition firstAcquire;
        PlanPosition lastRelease;
        rhi::MemoryRequirements requirements;
    };

    struct PlacedHeapPlan
    {
        FrameResourceKind kind = FrameResourceKind::Invalid;
        rhi::QueueType queue = rhi::QueueType::Graphics;
        rhi::MemoryType memoryType = rhi::MemoryType::DeviceLocal;
        rhi::PlacedHeapCategory heapCategory = rhi::PlacedHeapCategory::None;
        u64 compatibilityClass = 0;
        u64 capacity = 0;
        u64 alignment = 0;
    };

    struct PlacedPredecessorFragment
    {
        u32 allocation = InvalidRenderFlowResourceIndex;
        u64 offset = 0;
        u64 size = 0;
    };

    struct PlacedRangeAssignment
    {
        u32 allocation = InvalidRenderFlowResourceIndex;
        u32 heap = InvalidRenderFlowResourceIndex;
        u64 offset = 0;
        u64 size = 0;
        u64 alignment = 0;
        u32 predecessorOffset = 0;
        u32 predecessorCount = 0;
    };

    struct PlacedRangePlan
    {
        PlacedRangePlan() noexcept : heaps(memory::pools::Rendering::GetInstance()), assignments(memory::pools::Rendering::GetInstance()), predecessors(memory::pools::Rendering::GetInstance()) {}

        containers::DynamicArray<PlacedHeapPlan> heaps;
        containers::DynamicArray<PlacedRangeAssignment> assignments;
        containers::DynamicArray<PlacedPredecessorFragment> predecessors;

        void Clear() noexcept;
    };

    // Builds only the deterministic logical heap/range plan. Native heap and
    // placed-object ownership are added by the provider after this plan has
    // succeeded, so a planner failure cannot partially mutate RHI state.
    [[nodiscard]] bool BuildPlacedRangePlan(containers::ArraySpan<const PlacedRangeRequest> requests, const PlacedRangePlannerConfig& config, PlacedRangePlan& plan,
                                            RenderFlowResourceFailure* failure = nullptr) noexcept;

    inline constexpr u32 InvalidPlacedResourceEntry = 0xffffffffu;

    struct PlacedObjectRequest
    {
        u32 allocation = InvalidRenderFlowResourceIndex;
        FrameResourceDesc desc;
    };

    struct PlacedResourceAssignment
    {
        u32 allocation = InvalidRenderFlowResourceIndex;
        u32 objectEntry = InvalidPlacedResourceEntry;
        rhi::TextureRef texture;
        rhi::BufferRef buffer;
        rhi::PlacementRecord placement;

        [[nodiscard]] bool IsValid() const noexcept
        {
            return allocation != InvalidRenderFlowResourceIndex && objectEntry != InvalidPlacedResourceEntry && (texture.IsValid() != buffer.IsValid()) && placement.IsValid();
        }
    };

    struct PlacedResourceBatch
    {
        PlacedResourceBatch() noexcept : heapEntries(memory::pools::Rendering::GetInstance()), objectEntries(memory::pools::Rendering::GetInstance()), assignments(memory::pools::Rendering::GetInstance()) {}

        containers::DynamicArray<u32> heapEntries;
        containers::DynamicArray<u32> objectEntries;
        containers::DynamicArray<PlacedResourceAssignment> assignments;

        [[nodiscard]] bool IsValid() const noexcept
        {
            return !heapEntries.Empty() && !assignments.Empty() && objectEntries.Size() == assignments.Size();
        }

        void Clear() noexcept
        {
            heapEntries.Clear();
            objectEntries.Clear();
            assignments.Clear();
        }
    };

    struct PlacedResourcePoolStats
    {
        u64 heapHits = 0;
        u64 heapMisses = 0;
        u64 objectHits = 0;
        u64 objectMisses = 0;
        u64 pendingRetirementHeaps = 0;
        u64 pendingNativeDestructionHeaps = 0;
    };

    class PlacedResourcePool final
    {
    public:
        PlacedResourcePool(const RenderFlowResourceAllocatorConfig& config, AllocatorNativeByteLedger& ledger) noexcept;
        ~PlacedResourcePool();
        PlacedResourcePool(const PlacedResourcePool&) = delete;
        PlacedResourcePool& operator=(const PlacedResourcePool&) = delete;

        [[nodiscard]] bool Acquire(const PlacedRangePlan& plan, containers::ArraySpan<const PlacedObjectRequest> objects, u64 frameSerial, PlacedResourceBatch& batch,
                                   RenderFlowResourceFailure* failure = nullptr) noexcept;
        void Rollback(PlacedResourceBatch& batch) noexcept;
        // False keeps the heap-granular batch out of the cache and releases it through native observation after the fences complete.
        void Retire(PlacedResourceBatch& batch, const rhi::ResidencyFenceSet& safeAfter, bool reusable = true) noexcept;
        void Poll() noexcept;
        [[nodiscard]] bool TrimToSoftTargets(ResourcePoolTrimState& trim, RenderFlowResourceFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool ClearPersistentCache(RenderFlowResourceFailure* failure = nullptr) noexcept;
        void DeviceLost() noexcept;
        [[nodiscard]] PlacedResourcePoolStats GetStats() const noexcept;

    private:
        struct Impl;
        void PollUnlocked() noexcept;
        Impl* m_impl = nullptr;
    };
} // namespace vanguard::rendering::detail
