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
        u32 maximumPredecessorFragments = 1'048'576;
        // Resolve can provide its already-monotonic first-touch stream plus dense
        // semantic ordinals. Standalone planner callers leave these disabled and
        // retain the deterministic sort/release-heap path.
        u32 semanticEventCount = 0;
        bool requestsPreordered = false;
    };

    struct PlacedRangeRequest
    {
        u32 allocation = InvalidRenderFlowResourceIndex;
        FrameResourceKind kind = FrameResourceKind::Invalid;
        rhi::QueueType queue = rhi::QueueType::Graphics;
        PlanPosition firstAcquire;
        PlanPosition lastRelease;
        u32 firstAcquireSemanticOrdinal = InvalidRenderFlowResourceIndex;
        u32 lastReleaseSemanticOrdinal = InvalidRenderFlowResourceIndex;
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

    struct PlacedPlannerProvenancePiece
    {
        u64 offset = 0;
        u64 size = 0;
        u32 predecessor = InvalidRenderFlowResourceIndex;
        u32 previous = InvalidRenderFlowResourceIndex;
        u32 next = InvalidRenderFlowResourceIndex;
    };

    struct PlacedPlannerFreeRunNode
    {
        u64 offset = 0;
        u64 size = 0;
        u32 firstPiece = InvalidRenderFlowResourceIndex;
        u32 lastPiece = InvalidRenderFlowResourceIndex;
        u32 left = InvalidRenderFlowResourceIndex;
        u32 right = InvalidRenderFlowResourceIndex;
        u32 parent = InvalidRenderFlowResourceIndex;
        u16 height = 1;
        bool virgin = true;
        bool alive = false;
    };

    struct PlacedPlannerReleaseEvent
    {
        PlanPosition release;
        u64 offset = 0;
        u64 size = 0;
        u32 heap = InvalidRenderFlowResourceIndex;
        u32 allocation = InvalidRenderFlowResourceIndex;
        u32 next = InvalidRenderFlowResourceIndex;
    };

    struct PlacedPlannerSeenSet
    {
        PlacedPlannerSeenSet() noexcept : keys(memory::pools::Rendering::GetInstance()), stamps(memory::pools::Rendering::GetInstance()) {}

        void Reset() noexcept
        {
            size = 0;
            ++generation;
            if (generation != 0)
                return;
            for (u32& stamp : stamps)
                stamp = 0;
            generation = 1;
        }

        void Reserve(const u32 expectedSize) noexcept
        {
            if (expectedSize == 0)
                return;
            u32 capacity = 8;
            const u64 required = static_cast<u64>(expectedSize) * 2u;
            while (capacity < required && capacity <= 0x40000000u)
                capacity <<= 1u;
            if (capacity <= keys.Size())
                return;
            containers::DynamicArray<u32> oldKeys{static_cast<containers::DynamicArray<u32>&&>(keys)};
            containers::DynamicArray<u32> oldStamps{static_cast<containers::DynamicArray<u32>&&>(stamps)};
            keys = containers::DynamicArray<u32>{memory::pools::Rendering::GetInstance()};
            stamps = containers::DynamicArray<u32>{memory::pools::Rendering::GetInstance()};
            keys.Resize(capacity);
            stamps.Resize(capacity, 0u);
            const u32 oldGeneration = generation;
            size = 0;
            for (u32 index = 0; index < oldKeys.Size(); ++index)
                if (oldStamps[index] == oldGeneration)
                    static_cast<void>(Insert(oldKeys[index]));
        }

        [[nodiscard]] bool Insert(const u32 value) noexcept
        {
            if (keys.Empty() || static_cast<u64>(size + 1u) * 2u > keys.Size())
                Reserve(size + 1u);
            const u32 mask = keys.Size() - 1u;
            u32 slot = (value * 0x9e3779b9u) & mask;
            for (u32 probe = 0; probe < keys.Size(); ++probe)
            {
                if (stamps[slot] != generation)
                {
                    keys[slot] = value;
                    stamps[slot] = generation;
                    ++size;
                    return true;
                }
                if (keys[slot] == value)
                    return false;
                slot = (slot + 1u) & mask;
            }
            return false;
        }

        void TrimAfterUse(const u32 expectedSize) noexcept
        {
            constexpr u32 minimumCapacity = 8;
            u32 retainedCapacity = minimumCapacity;
            const u64 required = static_cast<u64>(expectedSize) * 2u;
            while (retainedCapacity < required && retainedCapacity <= 0x40000000u)
                retainedCapacity <<= 1u;
            if (keys.Size() <= retainedCapacity || static_cast<u64>(keys.Size()) <= static_cast<u64>(retainedCapacity) * 4u)
                return;
            keys = containers::DynamicArray<u32>{memory::pools::Rendering::GetInstance()};
            stamps = containers::DynamicArray<u32>{memory::pools::Rendering::GetInstance()};
            keys.Resize(retainedCapacity);
            stamps.Resize(retainedCapacity, 0u);
            generation = 1;
            size = 0;
        }

        containers::DynamicArray<u32> keys;
        containers::DynamicArray<u32> stamps;
        u32 generation = 1;
        u32 size = 0;
    };

    struct PlacedPlannerCompatibilityIndex
    {
        struct Entry
        {
            u64 key = 0;
            u32 head = InvalidRenderFlowResourceIndex;
        };

        PlacedPlannerCompatibilityIndex() noexcept : entries(memory::pools::Rendering::GetInstance()), stamps(memory::pools::Rendering::GetInstance()) {}

        void Reset() noexcept
        {
            size = 0;
            ++generation;
            if (generation != 0)
                return;
            for (u32& stamp : stamps)
                stamp = 0;
            generation = 1;
        }

        void Reserve(const u32 expectedSize) noexcept
        {
            if (expectedSize == 0)
                return;
            u64 required = static_cast<u64>(expectedSize) * 2u;
            u32 capacity = 8;
            while (capacity < required && capacity <= 0x40000000u)
                capacity <<= 1u;
            if (capacity <= entries.Size())
                return;
            containers::DynamicArray<Entry> oldEntries{static_cast<containers::DynamicArray<Entry>&&>(entries)};
            containers::DynamicArray<u32> oldStamps{static_cast<containers::DynamicArray<u32>&&>(stamps)};
            entries = containers::DynamicArray<Entry>{memory::pools::Rendering::GetInstance()};
            stamps = containers::DynamicArray<u32>{memory::pools::Rendering::GetInstance()};
            entries.Resize(capacity);
            stamps.Resize(capacity, 0u);
            const u32 oldGeneration = generation;
            size = 0;
            for (u32 index = 0; index < oldEntries.Size(); ++index)
                if (oldStamps[index] == oldGeneration)
                    static_cast<void>(Set(oldEntries[index].key, oldEntries[index].head));
        }

        [[nodiscard]] bool Find(const u64 key, u32& head) const noexcept
        {
            if (entries.Empty())
                return false;
            const u32 mask = entries.Size() - 1u;
            u32 slot = Hash(key) & mask;
            for (u32 probe = 0; probe < entries.Size(); ++probe)
            {
                if (stamps[slot] != generation)
                    return false;
                const Entry& entry = entries[slot];
                if (entry.key == key)
                {
                    head = entry.head;
                    return true;
                }
                slot = (slot + 1u) & mask;
            }
            return false;
        }

        [[nodiscard]] bool Set(const u64 key, const u32 head) noexcept
        {
            if (entries.Empty() || static_cast<u64>(size + 1u) * 2u > entries.Size())
                Reserve(size + 1u);
            const u32 mask = entries.Size() - 1u;
            u32 slot = Hash(key) & mask;
            for (u32 probe = 0; probe < entries.Size(); ++probe)
            {
                if (stamps[slot] != generation)
                {
                    entries[slot] = {key, head};
                    stamps[slot] = generation;
                    ++size;
                    return true;
                }
                Entry& entry = entries[slot];
                if (entry.key == key)
                {
                    entry.head = head;
                    return true;
                }
                slot = (slot + 1u) & mask;
            }
            return false;
        }

        void TrimAfterUse(const u32 expectedSize) noexcept
        {
            constexpr u32 minimumCapacity = 8;
            u32 retainedCapacity = minimumCapacity;
            const u64 required = static_cast<u64>(expectedSize) * 2u;
            while (retainedCapacity < required && retainedCapacity <= 0x40000000u)
                retainedCapacity <<= 1u;
            if (entries.Size() <= retainedCapacity || static_cast<u64>(entries.Size()) <= static_cast<u64>(retainedCapacity) * 4u)
                return;
            entries = containers::DynamicArray<Entry>{memory::pools::Rendering::GetInstance()};
            stamps = containers::DynamicArray<u32>{memory::pools::Rendering::GetInstance()};
            entries.Resize(retainedCapacity);
            stamps.Resize(retainedCapacity, 0u);
            generation = 1;
            size = 0;
        }

        containers::DynamicArray<Entry> entries;
        containers::DynamicArray<u32> stamps;
        u32 generation = 1;
        u32 size = 0;

    private:
        [[nodiscard]] static u32 Hash(const u64 value) noexcept
        {
            u64 mixed = value + 0x9e3779b97f4a7c15ull;
            mixed = (mixed ^ (mixed >> 30u)) * 0xbf58476d1ce4e5b9ull;
            mixed = (mixed ^ (mixed >> 27u)) * 0x94d049bb133111ebull;
            mixed ^= mixed >> 31u;
            return static_cast<u32>(mixed) ^ static_cast<u32>(mixed >> 32u);
        }
    };

    struct PlacedPlannerCompatibilityGroup
    {
        PlacedPlannerCompatibilityGroup() noexcept
            : heapIndices(memory::pools::Rendering::GetInstance()), fitTree(memory::pools::Rendering::GetInstance())
        {
        }

        void Reset() noexcept
        {
            exemplar = {};
            nextCollision = InvalidRenderFlowResourceIndex;
            heapIndices.Clear();
            fitTree.Clear();
            treeCapacity = 0;
            alignmentLevelCount = 0;
        }

        void TrimAfterUse() noexcept
        {
            constexpr u32 minimumRetainedCapacity = 32;
            if (heapIndices.Capacity() > minimumRetainedCapacity && heapIndices.Capacity() > heapIndices.Size() * 4u)
                heapIndices.Shrink();
            const u64 liveSummaryValues = static_cast<u64>(treeCapacity) * 2u * alignmentLevelCount;
            const u64 minimumSummaryCapacity = static_cast<u64>(minimumRetainedCapacity) * (alignmentLevelCount != 0 ? alignmentLevelCount : 1u);
            if (fitTree.Capacity() > minimumSummaryCapacity && fitTree.Capacity() > liveSummaryValues * 4u)
                fitTree.Shrink();
        }

        void ReleaseDormantBacking() noexcept
        {
            Reset();
            heapIndices.Shrink();
            fitTree.Shrink();
        }

        PlacedHeapPlan exemplar;
        u32 nextCollision = InvalidRenderFlowResourceIndex;
        containers::DynamicArray<u32> heapIndices;
        containers::DynamicArray<u64> fitTree;
        u32 treeCapacity = 0;
        u32 alignmentLevelCount = 0;
    };

    struct PlacedPlannerHeapState
    {
        PlacedPlannerHeapState() noexcept
            : freeRuns(memory::pools::Rendering::GetInstance()), freeRunFits(memory::pools::Rendering::GetInstance()), freeRunSlots(memory::pools::Rendering::GetInstance()),
              provenance(memory::pools::Rendering::GetInstance()), provenanceSlots(memory::pools::Rendering::GetInstance())
        {
        }

        void Reset() noexcept
        {
            plan = {};
            compatibilityGroup = InvalidRenderFlowResourceIndex;
            compatibilityPosition = InvalidRenderFlowResourceIndex;
            freeRunRoot = InvalidRenderFlowResourceIndex;
            freeRuns.Clear();
            freeRunFits.Clear();
            freeRunSlots.Clear();
            provenance.Clear();
            provenanceSlots.Clear();
            alignmentMask = 0;
            alignmentLevelCount = 0;
        }

        void TrimAfterUse() noexcept
        {
            constexpr u32 minimumRetainedCapacity = 32;
            if (freeRuns.Capacity() > minimumRetainedCapacity && freeRuns.Capacity() > freeRuns.Size() * 4u)
                freeRuns.Shrink();
            const u64 liveFitValues = static_cast<u64>(freeRuns.Size()) * alignmentLevelCount;
            const u64 minimumFitCapacity = static_cast<u64>(minimumRetainedCapacity) * (alignmentLevelCount != 0 ? alignmentLevelCount : 1u);
            if (freeRunFits.Capacity() > minimumFitCapacity && freeRunFits.Capacity() > liveFitValues * 4u)
                freeRunFits.Shrink();
            if (freeRunSlots.Capacity() > minimumRetainedCapacity && freeRunSlots.Capacity() > freeRunSlots.Size() * 4u)
                freeRunSlots.Shrink();
            if (provenance.Capacity() > minimumRetainedCapacity && provenance.Capacity() > provenance.Size() * 4u)
                provenance.Shrink();
            if (provenanceSlots.Capacity() > minimumRetainedCapacity && provenanceSlots.Capacity() > provenanceSlots.Size() * 4u)
                provenanceSlots.Shrink();
        }

        void ReleaseDormantBacking() noexcept
        {
            Reset();
            freeRuns.Shrink();
            freeRunFits.Shrink();
            freeRunSlots.Shrink();
            provenance.Shrink();
            provenanceSlots.Shrink();
        }

        PlacedHeapPlan plan;
        u32 compatibilityGroup = InvalidRenderFlowResourceIndex;
        u32 compatibilityPosition = InvalidRenderFlowResourceIndex;
        u32 freeRunRoot = InvalidRenderFlowResourceIndex;
        containers::DynamicArray<PlacedPlannerFreeRunNode> freeRuns;
        containers::DynamicArray<u64> freeRunFits;
        containers::DynamicArray<u32> freeRunSlots;
        containers::DynamicArray<PlacedPlannerProvenancePiece> provenance;
        containers::DynamicArray<u32> provenanceSlots;
        u64 alignmentMask = 0;
        u32 alignmentLevelCount = 0;
    };

    struct PlacedRangePlannerScratch
    {
        PlacedRangePlannerScratch() noexcept
            : order(memory::pools::Rendering::GetInstance()), heaps(memory::pools::Rendering::GetInstance()), groups(memory::pools::Rendering::GetInstance()),
              releaseEvents(memory::pools::Rendering::GetInstance()), releaseHeads(memory::pools::Rendering::GetInstance()), candidateFragments(memory::pools::Rendering::GetInstance())
        {
        }

        void Reset() noexcept
        {
            order.Clear();
            releaseEvents.Clear();
            releaseHeads.Clear();
            candidateFragments.Clear();
            seenAllocations.Reset();
            compatibleHeaps.Reset();
            activeHeapCount = 0;
            activeGroupCount = 0;
            alignmentMask = 0;
            alignmentLevelCount = 0;
        }

        [[nodiscard]] PlacedPlannerHeapState& AcquireHeap() noexcept
        {
            const u32 index = activeHeapCount++;
            if (index == heaps.Size())
                heaps.EmplaceBack();
            PlacedPlannerHeapState& heap = heaps[index];
            heap.Reset();
            heap.alignmentMask = alignmentMask;
            heap.alignmentLevelCount = alignmentLevelCount;
            return heap;
        }

        [[nodiscard]] PlacedPlannerCompatibilityGroup& AcquireGroup() noexcept
        {
            const u32 index = activeGroupCount++;
            if (index == groups.Size())
                groups.EmplaceBack();
            PlacedPlannerCompatibilityGroup& group = groups[index];
            group.Reset();
            group.alignmentLevelCount = alignmentLevelCount;
            return group;
        }

        void Finish(const u32 requestCount) noexcept
        {
            for (u32 index = 0; index < activeHeapCount; ++index)
                heaps[index].TrimAfterUse();
            for (u32 index = 0; index < activeGroupCount; ++index)
                groups[index].TrimAfterUse();
            constexpr u32 dormantTrimBudget = 2;
            for (u32 pass = 0; pass < dormantTrimBudget && activeHeapCount < heaps.Size(); ++pass)
            {
                if (dormantHeapTrimCursor < activeHeapCount || dormantHeapTrimCursor >= heaps.Size())
                    dormantHeapTrimCursor = activeHeapCount;
                heaps[dormantHeapTrimCursor].ReleaseDormantBacking();
                if (++dormantHeapTrimCursor >= heaps.Size())
                    dormantHeapTrimCursor = activeHeapCount;
            }
            for (u32 pass = 0; pass < dormantTrimBudget && activeGroupCount < groups.Size(); ++pass)
            {
                if (dormantGroupTrimCursor < activeGroupCount || dormantGroupTrimCursor >= groups.Size())
                    dormantGroupTrimCursor = activeGroupCount;
                groups[dormantGroupTrimCursor].ReleaseDormantBacking();
                if (++dormantGroupTrimCursor >= groups.Size())
                    dormantGroupTrimCursor = activeGroupCount;
            }
            constexpr u32 minimumRetainedTransientCapacity = 32;
            const u32 retainedTransientFloor = requestCount > minimumRetainedTransientCapacity ? requestCount : minimumRetainedTransientCapacity;
            const u32 releaseHeadCount = releaseHeads.Size();
            const u32 retainedReleaseHeadFloor = releaseHeadCount > minimumRetainedTransientCapacity ? releaseHeadCount : minimumRetainedTransientCapacity;
            if (static_cast<u64>(order.Capacity()) > static_cast<u64>(retainedTransientFloor) * 4u)
                order.Shrink();
            if (static_cast<u64>(releaseEvents.Capacity()) > static_cast<u64>(retainedTransientFloor) * 4u)
                releaseEvents.Shrink();
            if (static_cast<u64>(releaseHeads.Capacity()) > static_cast<u64>(retainedReleaseHeadFloor) * 4u)
                releaseHeads.Shrink();
            if (static_cast<u64>(candidateFragments.Capacity()) > static_cast<u64>(retainedTransientFloor) * 4u)
                candidateFragments.Shrink();
            seenAllocations.TrimAfterUse(requestCount);
            compatibleHeaps.TrimAfterUse(activeGroupCount);
            constexpr u32 minimumRetainedCount = 16;
            const u32 retainedHeapFloor = activeHeapCount > minimumRetainedCount ? activeHeapCount : minimumRetainedCount;
            if (static_cast<u64>(heaps.Size()) > static_cast<u64>(retainedHeapFloor) * 4u)
            {
                heaps.Resize(activeHeapCount);
                heaps.Shrink();
                dormantHeapTrimCursor = activeHeapCount;
            }
            const u32 retainedGroupFloor = activeGroupCount > minimumRetainedCount ? activeGroupCount : minimumRetainedCount;
            if (static_cast<u64>(groups.Size()) > static_cast<u64>(retainedGroupFloor) * 4u)
            {
                groups.Resize(activeGroupCount);
                groups.Shrink();
                dormantGroupTrimCursor = activeGroupCount;
            }
        }

        void DecayWithoutUse() noexcept
        {
            // ResolveScratch::ResetFrame has already cleared logical contents.
            // If placement is skipped entirely, still age a bounded amount of
            // retained planner backing so a past spike cannot persist forever.
            Finish(0);
        }

        containers::DynamicArray<u32> order;
        containers::DynamicArray<PlacedPlannerHeapState> heaps;
        containers::DynamicArray<PlacedPlannerCompatibilityGroup> groups;
        containers::DynamicArray<PlacedPlannerReleaseEvent> releaseEvents;
        containers::DynamicArray<u32> releaseHeads;
        containers::DynamicArray<PlacedPredecessorFragment> candidateFragments;
        PlacedPlannerSeenSet seenAllocations;
        PlacedPlannerCompatibilityIndex compatibleHeaps;
        u32 activeHeapCount = 0;
        u32 activeGroupCount = 0;
        u64 alignmentMask = 0;
        u32 alignmentLevelCount = 0;
        u32 dormantHeapTrimCursor = 0;
        u32 dormantGroupTrimCursor = 0;
    };

    // Builds only the deterministic logical heap/range plan. Native heap and
    // placed-object ownership are added by the provider after this plan has
    // succeeded, so a planner failure cannot partially mutate RHI state.
    [[nodiscard]] bool BuildPlacedRangePlan(containers::ArraySpan<const PlacedRangeRequest> requests, const PlacedRangePlannerConfig& config, PlacedRangePlan& plan,
                                            RenderFlowResourceFailure* failure = nullptr) noexcept;
    [[nodiscard]] bool BuildPlacedRangePlan(containers::ArraySpan<const PlacedRangeRequest> requests, const PlacedRangePlannerConfig& config, PlacedRangePlan& plan,
                                            PlacedRangePlannerScratch& scratch, RenderFlowResourceFailure* failure = nullptr) noexcept;

    inline constexpr u32 InvalidPlacedResourceEntry = 0xffffffffu;

    struct PlacedObjectRequest
    {
        u32 allocation = InvalidRenderFlowResourceIndex;
        FrameResourceDesc desc;
        bool requiresCanonicalDescriptors = false;
    };

    struct PlacedResourceAssignment
    {
        u32 allocation = InvalidRenderFlowResourceIndex;
        u32 objectEntry = InvalidPlacedResourceEntry;
        rhi::TextureRef texture;
        rhi::BufferRef buffer;
        rhi::PlacementRecord placement;
        rhi::DescriptorHandle shaderResource;
        rhi::DescriptorHandle unorderedAccess;

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
        PlacedResourcePool(const RenderFlowResourceAllocatorConfig& config, AllocatorNativeByteLedger& ledger,
                           AllocatorDescriptorLedger& descriptorLedger, rhi::DescriptorDomainRef descriptorDomain) noexcept;
        ~PlacedResourcePool();
        PlacedResourcePool(const PlacedResourcePool&) = delete;
        PlacedResourcePool& operator=(const PlacedResourcePool&) = delete;

        [[nodiscard]] bool Acquire(const PlacedRangePlan& plan, containers::ArraySpan<const PlacedObjectRequest> objects, u64 frameSerial, PlacedResourceBatch& batch,
                                   RenderFlowResourceFailure* failure = nullptr, CanonicalDescriptorPressure* descriptorPressure = nullptr,
                                   bool recordPublicationStats = true) noexcept;
        void Rollback(PlacedResourceBatch& batch) noexcept;
        // False keeps the heap-granular batch out of the cache and releases it through native observation after the fences complete.
        void Retire(PlacedResourceBatch& batch, const rhi::ResidencyFenceSet& safeAfter, bool reusable = true) noexcept;
        void Poll() noexcept;
        [[nodiscard]] bool TrimToSoftTargets(ResourcePoolTrimState& trim, RenderFlowResourceFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool GetColdestCanonicalDescriptorCandidate(CanonicalDescriptorEvictionCandidate& candidate) noexcept;
        [[nodiscard]] CanonicalDescriptorEvictionResult EvictCanonicalDescriptorCandidate(const CanonicalDescriptorEvictionCandidate& candidate, bool pressure,
                                                                                           RenderFlowResourceFailure* failure = nullptr) noexcept;
        void RefreshCanonicalDescriptorCandidates() noexcept;
        [[nodiscard]] bool ClearPersistentCache(RenderFlowResourceFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool PrepareHealthyShutdown(bool& deviceLostEvidence, RenderFlowResourceFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool ValidateHealthyShutdownCommit(RenderFlowResourceFailure* failure = nullptr) const noexcept;
        void CommitHealthyShutdown() noexcept;
        void DeviceLost(const rhi::ResidencyFenceSet& submittedFences = {}) noexcept;
        [[nodiscard]] PlacedResourcePoolStats GetStats() const noexcept;

    private:
        struct Impl;
        void PollUnlocked() noexcept;
        Impl* m_impl = nullptr;
    };
} // namespace vanguard::rendering::detail
