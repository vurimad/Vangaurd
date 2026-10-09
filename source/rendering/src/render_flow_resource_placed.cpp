#include <vanguard/rendering/render_flow_resource_placed.hpp>

#include <vanguard/memory/memory.hpp>
#include <vanguard/profiling/profiling.hpp>
#include <vanguard/rendering/render_flow_resource_internal.hpp>
#include <vanguard/system/assert.hpp>

#include <algorithm>
#include <new>

namespace vanguard::rendering::detail
{
    namespace
    {
        using HeapState = PlacedPlannerHeapState;

        template <typename Enum> [[nodiscard]] constexpr bool HasAny(const Enum value, const Enum flags) noexcept
        {
            return (static_cast<u32>(value) & static_cast<u32>(flags)) != 0;
        }

        struct ProvenanceConsumeWindow
        {
            u32 first = InvalidRenderFlowResourceIndex;
            u32 last = InvalidRenderFlowResourceIndex;
        };

        [[nodiscard]] constexpr bool PositionBefore(const PlanPosition left, const PlanPosition right) noexcept
        {
            return left.flowGroup.value < right.flowGroup.value || (left.flowGroup == right.flowGroup && left.ordinal < right.ordinal);
        }

        [[nodiscard]] constexpr bool IsPowerOfTwo(const u64 value) noexcept
        {
            return value != 0 && (value & (value - 1u)) == 0;
        }

        [[nodiscard]] bool CheckedEnd(const u64 offset, const u64 size, u64& end) noexcept
        {
            if (size == 0 || offset > ~u64{0} - size)
                return false;
            end = offset + size;
            return true;
        }

        [[nodiscard]] bool CheckedAlignUp(const u64 value, const u64 alignment, u64& result) noexcept
        {
            if (!IsPowerOfTwo(alignment) || value > ~u64{0} - (alignment - 1u))
                return false;
            result = (value + alignment - 1u) & ~(alignment - 1u);
            return true;
        }

        [[nodiscard]] bool PlannerFail(RenderFlowResourceFailure* const failure, const RenderFlowResourceFailureCode code, const char* const message) noexcept
        {
            if (failure != nullptr)
            {
                *failure = {};
                failure->code = code;
                failure->phase = RenderFlowResourceSessionState::Resolving;
                failure->message = message;
            }
            return false;
        }

        [[nodiscard]] bool HeapCompatible(const PlacedHeapPlan& heap, const PlacedRangeRequest& request) noexcept
        {
            return heap.kind == request.kind && heap.queue == request.queue && heap.memoryType == request.requirements.memoryType && heap.heapCategory == request.requirements.heapCategory &&
                   heap.compatibilityClass == request.requirements.compatibilityClass && request.requirements.alignment <= heap.alignment;
        }

        [[nodiscard]] u64 PlannerCompatibilityKey(const PlacedRangeRequest& request) noexcept
        {
            u64 key = request.requirements.compatibilityClass;
            key ^= static_cast<u64>(request.kind) * 0x9e3779b97f4a7c15ull;
            key ^= static_cast<u64>(request.queue) * 0xbf58476d1ce4e5b9ull;
            key ^= static_cast<u64>(request.requirements.memoryType) * 0x94d049bb133111ebull;
            key ^= static_cast<u64>(request.requirements.heapCategory) * 0x369dea0f31a53f85ull;
            key ^= key >> 29u;
            key *= 0x9fb21c651e98df25ull;
            key ^= key >> 32u;
            return key;
        }

        [[nodiscard]] u32 CountAlignmentLevels(u64 mask) noexcept
        {
            u32 count = 0;
            while (mask != 0)
            {
                mask &= mask - 1u;
                ++count;
            }
            return count;
        }

        [[nodiscard]] u32 AlignmentSlot(const u64 mask, const u64 alignment) noexcept
        {
            VG_ASSERT(IsPowerOfTwo(alignment) && (mask & alignment) != 0);
            return CountAlignmentLevels(mask & (alignment - 1u));
        }

        [[nodiscard]] u16 FreeRunHeight(const HeapState& heap, const u32 index) noexcept
        {
            return index == InvalidRenderFlowResourceIndex ? 0u : heap.freeRuns[index].height;
        }

        [[nodiscard]] u64 FreeRunOwnFit(const PlacedPlannerFreeRunNode& run, const u64 alignment) noexcept
        {
            u64 aligned = 0;
            u64 end = 0;
            if (!CheckedAlignUp(run.offset, alignment, aligned) || !CheckedEnd(run.offset, run.size, end) || aligned >= end)
                return 0;
            return end - aligned;
        }

        [[nodiscard]] u64 FreeRunFit(const HeapState& heap, const u32 index, const u32 slot) noexcept
        {
            VG_ASSERT(index < heap.freeRuns.Size() && slot < heap.alignmentLevelCount);
            return heap.freeRunFits[static_cast<u64>(index) * heap.alignmentLevelCount + slot];
        }

        u64& FreeRunFit(HeapState& heap, const u32 index, const u32 slot) noexcept
        {
            VG_ASSERT(index < heap.freeRuns.Size() && slot < heap.alignmentLevelCount);
            return heap.freeRunFits[static_cast<u64>(index) * heap.alignmentLevelCount + slot];
        }

        void UpdateFreeRunSummary(HeapState& heap, const u32 index) noexcept
        {
            if (index == InvalidRenderFlowResourceIndex)
                return;
            PlacedPlannerFreeRunNode& node = heap.freeRuns[index];
            const u16 leftHeight = FreeRunHeight(heap, node.left);
            const u16 rightHeight = FreeRunHeight(heap, node.right);
            node.height = static_cast<u16>((leftHeight > rightHeight ? leftHeight : rightHeight) + 1u);
            u64 remainingAlignments = heap.alignmentMask;
            for (u32 slot = 0; slot < heap.alignmentLevelCount; ++slot)
            {
                VG_ASSERT(remainingAlignments != 0);
                const u64 alignment = remainingAlignments & (~remainingAlignments + 1u);
                remainingAlignments &= remainingAlignments - 1u;
                u64 fit = FreeRunOwnFit(node, alignment);
                if (node.left != InvalidRenderFlowResourceIndex && FreeRunFit(heap, node.left, slot) > fit)
                    fit = FreeRunFit(heap, node.left, slot);
                if (node.right != InvalidRenderFlowResourceIndex && FreeRunFit(heap, node.right, slot) > fit)
                    fit = FreeRunFit(heap, node.right, slot);
                FreeRunFit(heap, index, slot) = fit;
            }
            VG_ASSERT(remainingAlignments == 0);
        }

        void ReplaceFreeRunParentLink(HeapState& heap, const u32 parent, const u32 oldChild, const u32 newChild) noexcept
        {
            if (parent == InvalidRenderFlowResourceIndex)
                heap.freeRunRoot = newChild;
            else if (heap.freeRuns[parent].left == oldChild)
                heap.freeRuns[parent].left = newChild;
            else
            {
                VG_ASSERT(heap.freeRuns[parent].right == oldChild);
                heap.freeRuns[parent].right = newChild;
            }
            if (newChild != InvalidRenderFlowResourceIndex)
                heap.freeRuns[newChild].parent = parent;
        }

        u32 RotateFreeRunLeft(HeapState& heap, const u32 root) noexcept
        {
            PlacedPlannerFreeRunNode& rootNode = heap.freeRuns[root];
            const u32 pivot = rootNode.right;
            VG_ASSERT(pivot != InvalidRenderFlowResourceIndex);
            PlacedPlannerFreeRunNode& pivotNode = heap.freeRuns[pivot];
            const u32 parent = rootNode.parent;
            const u32 middle = pivotNode.left;
            ReplaceFreeRunParentLink(heap, parent, root, pivot);
            pivotNode.left = root;
            rootNode.parent = pivot;
            rootNode.right = middle;
            if (middle != InvalidRenderFlowResourceIndex)
                heap.freeRuns[middle].parent = root;
            UpdateFreeRunSummary(heap, root);
            UpdateFreeRunSummary(heap, pivot);
            return pivot;
        }

        u32 RotateFreeRunRight(HeapState& heap, const u32 root) noexcept
        {
            PlacedPlannerFreeRunNode& rootNode = heap.freeRuns[root];
            const u32 pivot = rootNode.left;
            VG_ASSERT(pivot != InvalidRenderFlowResourceIndex);
            PlacedPlannerFreeRunNode& pivotNode = heap.freeRuns[pivot];
            const u32 parent = rootNode.parent;
            const u32 middle = pivotNode.right;
            ReplaceFreeRunParentLink(heap, parent, root, pivot);
            pivotNode.right = root;
            rootNode.parent = pivot;
            rootNode.left = middle;
            if (middle != InvalidRenderFlowResourceIndex)
                heap.freeRuns[middle].parent = root;
            UpdateFreeRunSummary(heap, root);
            UpdateFreeRunSummary(heap, pivot);
            return pivot;
        }

        void RebalanceFreeRunsFrom(HeapState& heap, u32 index) noexcept
        {
            while (index != InvalidRenderFlowResourceIndex)
            {
                UpdateFreeRunSummary(heap, index);
                const i32 balance = static_cast<i32>(FreeRunHeight(heap, heap.freeRuns[index].left)) - static_cast<i32>(FreeRunHeight(heap, heap.freeRuns[index].right));
                u32 balancedRoot = index;
                if (balance > 1)
                {
                    const u32 left = heap.freeRuns[index].left;
                    if (FreeRunHeight(heap, heap.freeRuns[left].left) < FreeRunHeight(heap, heap.freeRuns[left].right))
                        static_cast<void>(RotateFreeRunLeft(heap, left));
                    balancedRoot = RotateFreeRunRight(heap, index);
                }
                else if (balance < -1)
                {
                    const u32 right = heap.freeRuns[index].right;
                    if (FreeRunHeight(heap, heap.freeRuns[right].right) < FreeRunHeight(heap, heap.freeRuns[right].left))
                        static_cast<void>(RotateFreeRunRight(heap, right));
                    balancedRoot = RotateFreeRunLeft(heap, index);
                }
                index = heap.freeRuns[balancedRoot].parent;
            }
        }

        u32 AllocateFreeRun(HeapState& heap) noexcept
        {
            u32 index = InvalidRenderFlowResourceIndex;
            if (heap.freeRunSlots.Empty())
            {
                index = heap.freeRuns.Size();
                heap.freeRuns.EmplaceBack();
                const u64 requiredFitValues = static_cast<u64>(index + 1u) * heap.alignmentLevelCount;
                VG_ASSERT(requiredFitValues <= 0xffffffffu);
                heap.freeRunFits.Resize(static_cast<u32>(requiredFitValues), 0u);
            }
            else
            {
                index = heap.freeRunSlots.Back();
                heap.freeRunSlots.PopBack();
                heap.freeRuns[index] = {};
            }
            heap.freeRuns[index].alive = true;
            return index;
        }

        void ReleaseFreeRunSlot(HeapState& heap, const u32 index) noexcept
        {
            heap.freeRuns[index] = {};
            heap.freeRunSlots.PushBack(index);
        }

        void InsertFreeRun(HeapState& heap, const u32 index) noexcept
        {
            PlacedPlannerFreeRunNode& inserted = heap.freeRuns[index];
            inserted.left = inserted.right = inserted.parent = InvalidRenderFlowResourceIndex;
            inserted.height = 1;
            UpdateFreeRunSummary(heap, index);
            if (heap.freeRunRoot == InvalidRenderFlowResourceIndex)
            {
                heap.freeRunRoot = index;
                return;
            }
            u32 current = heap.freeRunRoot;
            for (;;)
            {
                VG_ASSERT(inserted.offset != heap.freeRuns[current].offset);
                u32& child = inserted.offset < heap.freeRuns[current].offset ? heap.freeRuns[current].left : heap.freeRuns[current].right;
                if (child == InvalidRenderFlowResourceIndex)
                {
                    child = index;
                    inserted.parent = current;
                    RebalanceFreeRunsFrom(heap, current);
                    return;
                }
                current = child;
            }
        }

        void RemoveFreeRun(HeapState& heap, const u32 index) noexcept
        {
            PlacedPlannerFreeRunNode& removed = heap.freeRuns[index];
            while (removed.left != InvalidRenderFlowResourceIndex && removed.right != InvalidRenderFlowResourceIndex)
            {
                if (FreeRunHeight(heap, removed.left) > FreeRunHeight(heap, removed.right))
                    static_cast<void>(RotateFreeRunRight(heap, index));
                else
                    static_cast<void>(RotateFreeRunLeft(heap, index));
            }
            const u32 parent = removed.parent;
            const u32 child = removed.left != InvalidRenderFlowResourceIndex ? removed.left : removed.right;
            ReplaceFreeRunParentLink(heap, parent, index, child);
            removed.left = removed.right = removed.parent = InvalidRenderFlowResourceIndex;
            removed.height = 1;
            RebalanceFreeRunsFrom(heap, parent);
        }

        [[nodiscard]] u32 FreeRunPredecessor(const HeapState& heap, const u64 offset) noexcept
        {
            u32 current = heap.freeRunRoot;
            u32 best = InvalidRenderFlowResourceIndex;
            while (current != InvalidRenderFlowResourceIndex)
            {
                if (heap.freeRuns[current].offset < offset)
                {
                    best = current;
                    current = heap.freeRuns[current].right;
                }
                else
                    current = heap.freeRuns[current].left;
            }
            return best;
        }

        [[nodiscard]] u32 FreeRunSuccessor(const HeapState& heap, const u64 offset) noexcept
        {
            u32 current = heap.freeRunRoot;
            u32 best = InvalidRenderFlowResourceIndex;
            while (current != InvalidRenderFlowResourceIndex)
            {
                if (heap.freeRuns[current].offset > offset)
                {
                    best = current;
                    current = heap.freeRuns[current].left;
                }
                else
                    current = heap.freeRuns[current].right;
            }
            return best;
        }

        u32 AllocateProvenancePiece(HeapState& heap, const u64 offset, const u64 size, const u32 predecessor) noexcept
        {
            u32 index = InvalidRenderFlowResourceIndex;
            if (heap.provenanceSlots.Empty())
            {
                index = heap.provenance.Size();
                heap.provenance.EmplaceBack();
            }
            else
            {
                index = heap.provenanceSlots.Back();
                heap.provenanceSlots.PopBack();
                heap.provenance[index] = {};
            }
            heap.provenance[index] = {offset, size, predecessor, InvalidRenderFlowResourceIndex, InvalidRenderFlowResourceIndex};
            return index;
        }

        void ReleaseProvenancePiece(HeapState& heap, const u32 index) noexcept
        {
            heap.provenance[index] = {};
            heap.provenanceSlots.PushBack(index);
        }

        void AppendPiece(HeapState& heap, PlacedPlannerFreeRunNode& run, const u32 pieceIndex) noexcept
        {
            PlacedPlannerProvenancePiece& piece = heap.provenance[pieceIndex];
            piece.previous = run.lastPiece;
            piece.next = InvalidRenderFlowResourceIndex;
            if (run.lastPiece != InvalidRenderFlowResourceIndex)
                heap.provenance[run.lastPiece].next = pieceIndex;
            else
                run.firstPiece = pieceIndex;
            run.lastPiece = pieceIndex;
        }

        void MergeFreeRuns(HeapState& heap, const u32 leftIndex, const u32 rightIndex) noexcept
        {
            PlacedPlannerFreeRunNode& left = heap.freeRuns[leftIndex];
            PlacedPlannerFreeRunNode& right = heap.freeRuns[rightIndex];
            VG_ASSERT(left.virgin == right.virgin && left.offset + left.size == right.offset);
            if (!left.virgin && right.firstPiece != InvalidRenderFlowResourceIndex)
            {
                if (left.lastPiece != InvalidRenderFlowResourceIndex)
                {
                    heap.provenance[left.lastPiece].next = right.firstPiece;
                    heap.provenance[right.firstPiece].previous = left.lastPiece;
                }
                else
                    left.firstPiece = right.firstPiece;
                left.lastPiece = right.lastPiece;
            }
            RemoveFreeRun(heap, rightIndex);
            left.size += right.size;
            RebalanceFreeRunsFrom(heap, leftIndex);
            ReleaseFreeRunSlot(heap, rightIndex);
        }

        void InsertReleasedRange(HeapState& heap, const u64 offset, const u64 size, const u32 allocation) noexcept
        {
            const u32 runIndex = AllocateFreeRun(heap);
            PlacedPlannerFreeRunNode& run = heap.freeRuns[runIndex];
            run.offset = offset;
            run.size = size;
            run.virgin = false;
            const u32 piece = AllocateProvenancePiece(heap, offset, size, allocation);
            run.firstPiece = run.lastPiece = piece;
            InsertFreeRun(heap, runIndex);

            u32 merged = runIndex;
            const u32 predecessor = FreeRunPredecessor(heap, heap.freeRuns[merged].offset);
            if (predecessor != InvalidRenderFlowResourceIndex)
            {
                u64 predecessorEnd = 0;
                if (!heap.freeRuns[predecessor].virgin && CheckedEnd(heap.freeRuns[predecessor].offset, heap.freeRuns[predecessor].size, predecessorEnd) &&
                    predecessorEnd == heap.freeRuns[merged].offset)
                {
                    MergeFreeRuns(heap, predecessor, merged);
                    merged = predecessor;
                }
            }
            const u32 successor = FreeRunSuccessor(heap, heap.freeRuns[merged].offset);
            if (successor != InvalidRenderFlowResourceIndex)
            {
                u64 mergedEnd = 0;
                if (!heap.freeRuns[successor].virgin && CheckedEnd(heap.freeRuns[merged].offset, heap.freeRuns[merged].size, mergedEnd) && mergedEnd == heap.freeRuns[successor].offset)
                    MergeFreeRuns(heap, merged, successor);
            }
        }

        [[nodiscard]] bool FindFirstFit(const HeapState& heap, const PlacedRangeRequest& request, u32& runIndex, u64& offset,
                                        containers::DynamicArray<PlacedPredecessorFragment>& fragments, ProvenanceConsumeWindow& provenanceWindow) noexcept
        {
            fragments.Clear();
            provenanceWindow = {};
            const u32 slot = AlignmentSlot(heap.alignmentMask, request.requirements.alignment);
            u32 current = heap.freeRunRoot;
            while (current != InvalidRenderFlowResourceIndex)
            {
                const PlacedPlannerFreeRunNode& run = heap.freeRuns[current];
                if (run.left != InvalidRenderFlowResourceIndex && FreeRunFit(heap, run.left, slot) >= request.requirements.size)
                {
                    current = run.left;
                    continue;
                }
                if (FreeRunOwnFit(run, request.requirements.alignment) >= request.requirements.size)
                {
                    runIndex = current;
                    if (!CheckedAlignUp(run.offset, request.requirements.alignment, offset))
                        return false;
                    if (!run.virgin)
                    {
                        const u64 end = offset + request.requirements.size;
                        u64 covered = offset;
                        for (u32 pieceIndex = run.firstPiece; pieceIndex != InvalidRenderFlowResourceIndex; pieceIndex = heap.provenance[pieceIndex].next)
                        {
                            const PlacedPlannerProvenancePiece& piece = heap.provenance[pieceIndex];
                            const u64 pieceEnd = piece.offset + piece.size;
                            if (pieceEnd <= offset)
                                continue;
                            if (piece.offset >= end)
                                break;
                            const u64 overlapStart = piece.offset > offset ? piece.offset : offset;
                            const u64 overlapEnd = pieceEnd < end ? pieceEnd : end;
                            if (overlapStart != covered || overlapEnd <= overlapStart)
                            {
                                fragments.Clear();
                                provenanceWindow = {};
                                return false;
                            }
                            if (provenanceWindow.first == InvalidRenderFlowResourceIndex)
                                provenanceWindow.first = pieceIndex;
                            provenanceWindow.last = pieceIndex;
                            fragments.PushBack({piece.predecessor, overlapStart, overlapEnd - overlapStart});
                            covered = overlapEnd;
                        }
                        if (covered != end || provenanceWindow.first == InvalidRenderFlowResourceIndex || provenanceWindow.last == InvalidRenderFlowResourceIndex)
                        {
                            fragments.Clear();
                            provenanceWindow = {};
                            return false;
                        }
                    }
                    return true;
                }
                if (run.right != InvalidRenderFlowResourceIndex && FreeRunFit(heap, run.right, slot) >= request.requirements.size)
                {
                    current = run.right;
                    continue;
                }
                break;
            }
            runIndex = InvalidRenderFlowResourceIndex;
            fragments.Clear();
            provenanceWindow = {};
            return false;
        }

        void ConsumeFreeRange(HeapState& heap, const u32 runIndex, const u64 offset, const u64 size, const ProvenanceConsumeWindow provenanceWindow) noexcept
        {
            PlacedPlannerFreeRunNode source = heap.freeRuns[runIndex];
            const u64 end = offset + size;
            const u64 sourceEnd = source.offset + source.size;
            RemoveFreeRun(heap, runIndex);
            ReleaseFreeRunSlot(heap, runIndex);

            u32 leftFirst = InvalidRenderFlowResourceIndex;
            u32 leftLast = InvalidRenderFlowResourceIndex;
            u32 rightFirst = InvalidRenderFlowResourceIndex;
            u32 rightLast = InvalidRenderFlowResourceIndex;

            if (!source.virgin)
            {
                VG_ASSERT(provenanceWindow.first != InvalidRenderFlowResourceIndex && provenanceWindow.last != InvalidRenderFlowResourceIndex);
                const u32 firstConsumed = provenanceWindow.first;
                const u32 lastConsumed = provenanceWindow.last;
                const u32 before = heap.provenance[firstConsumed].previous;
                const u32 after = heap.provenance[lastConsumed].next;

                if (source.offset < offset)
                {
                    leftFirst = source.firstPiece;
                    leftLast = before;
                    if (before != InvalidRenderFlowResourceIndex)
                        heap.provenance[before].next = InvalidRenderFlowResourceIndex;
                }
                if (end < sourceEnd)
                {
                    rightFirst = after;
                    rightLast = source.lastPiece;
                    if (after != InvalidRenderFlowResourceIndex)
                        heap.provenance[after].previous = InvalidRenderFlowResourceIndex;
                }

                for (u32 pieceIndex = firstConsumed;;)
                {
                    const u32 next = heap.provenance[pieceIndex].next;
                    PlacedPlannerProvenancePiece& piece = heap.provenance[pieceIndex];
                    const u64 pieceStart = piece.offset;
                    const u64 pieceEnd = piece.offset + piece.size;
                    const bool isLast = pieceIndex == lastConsumed;
                    if (pieceStart < offset)
                    {
                        const u32 predecessor = piece.predecessor;
                        piece.offset = pieceStart;
                        piece.size = offset - pieceStart;
                        piece.previous = before;
                        piece.next = InvalidRenderFlowResourceIndex;
                        if (before != InvalidRenderFlowResourceIndex)
                            heap.provenance[before].next = pieceIndex;
                        else
                            leftFirst = pieceIndex;
                        leftLast = pieceIndex;
                        if (pieceEnd > end)
                        {
                            const u32 rightPiece = AllocateProvenancePiece(heap, end, pieceEnd - end, predecessor);
                            PlacedPlannerProvenancePiece& right = heap.provenance[rightPiece];
                            right.previous = InvalidRenderFlowResourceIndex;
                            right.next = after;
                            if (after != InvalidRenderFlowResourceIndex)
                                heap.provenance[after].previous = rightPiece;
                            else
                                rightLast = rightPiece;
                            rightFirst = rightPiece;
                        }
                    }
                    else if (pieceEnd > end)
                    {
                        piece.offset = end;
                        piece.size = pieceEnd - end;
                        piece.previous = InvalidRenderFlowResourceIndex;
                        piece.next = after;
                        if (after != InvalidRenderFlowResourceIndex)
                            heap.provenance[after].previous = pieceIndex;
                        else
                            rightLast = pieceIndex;
                        rightFirst = pieceIndex;
                    }
                    else
                        ReleaseProvenancePiece(heap, pieceIndex);
                    if (isLast)
                        break;
                    pieceIndex = next;
                }
            }

            if (source.offset < offset)
            {
                const u32 leftRun = AllocateFreeRun(heap);
                PlacedPlannerFreeRunNode& run = heap.freeRuns[leftRun];
                run.offset = source.offset;
                run.size = offset - source.offset;
                run.virgin = source.virgin;
                run.firstPiece = leftFirst;
                run.lastPiece = leftLast;
                InsertFreeRun(heap, leftRun);
            }
            if (end < sourceEnd)
            {
                const u32 rightRun = AllocateFreeRun(heap);
                PlacedPlannerFreeRunNode& run = heap.freeRuns[rightRun];
                run.offset = end;
                run.size = sourceEnd - end;
                run.virgin = source.virgin;
                run.firstPiece = rightFirst;
                run.lastPiece = rightLast;
                InsertFreeRun(heap, rightRun);
            }
        }

        [[nodiscard]] bool CompatibilityGroupMatches(const PlacedHeapPlan& exemplar, const PlacedRangeRequest& request) noexcept
        {
            return exemplar.kind == request.kind && exemplar.queue == request.queue && exemplar.memoryType == request.requirements.memoryType &&
                   exemplar.heapCategory == request.requirements.heapCategory && exemplar.compatibilityClass == request.requirements.compatibilityClass;
        }

        [[nodiscard]] u64 HeapFitAtSlot(const HeapState& heap, const u32 slot) noexcept
        {
            return heap.freeRunRoot == InvalidRenderFlowResourceIndex ? 0 : FreeRunFit(heap, heap.freeRunRoot, slot);
        }

        void RebuildCompatibilityGroupTree(PlacedPlannerCompatibilityGroup& group, const containers::DynamicArray<PlacedPlannerHeapState>& heaps) noexcept
        {
            u32 capacity = 1;
            while (capacity < group.heapIndices.Size())
                capacity <<= 1u;
            VG_ASSERT(group.alignmentLevelCount != 0 && capacity <= 0xffffffffu / (2u * group.alignmentLevelCount));
            group.treeCapacity = capacity;
            group.fitTree.Resize(capacity * 2u * group.alignmentLevelCount, 0u);
            for (u32 position = 0; position < group.heapIndices.Size(); ++position)
            {
                const HeapState& heap = heaps[group.heapIndices[position]];
                const u32 base = (capacity + position) * group.alignmentLevelCount;
                for (u32 slot = 0; slot < group.alignmentLevelCount; ++slot)
                    group.fitTree[base + slot] = HeapFitAtSlot(heap, slot);
            }
            for (u32 node = capacity; node-- > 1u;)
            {
                const u32 base = node * group.alignmentLevelCount;
                const u32 left = node * 2u * group.alignmentLevelCount;
                const u32 right = (node * 2u + 1u) * group.alignmentLevelCount;
                for (u32 slot = 0; slot < group.alignmentLevelCount; ++slot)
                    group.fitTree[base + slot] = group.fitTree[left + slot] > group.fitTree[right + slot] ? group.fitTree[left + slot] : group.fitTree[right + slot];
            }
        }

        void UpdateCompatibilityGroupHeap(PlacedPlannerCompatibilityGroup& group, const u32 position, const HeapState& heap) noexcept
        {
            VG_ASSERT(position < group.heapIndices.Size() && group.treeCapacity != 0);
            u32 node = group.treeCapacity + position;
            u32 base = node * group.alignmentLevelCount;
            for (u32 slot = 0; slot < group.alignmentLevelCount; ++slot)
                group.fitTree[base + slot] = HeapFitAtSlot(heap, slot);
            while ((node >>= 1u) != 0)
            {
                base = node * group.alignmentLevelCount;
                const u32 left = node * 2u * group.alignmentLevelCount;
                const u32 right = (node * 2u + 1u) * group.alignmentLevelCount;
                for (u32 slot = 0; slot < group.alignmentLevelCount; ++slot)
                    group.fitTree[base + slot] = group.fitTree[left + slot] > group.fitTree[right + slot] ? group.fitTree[left + slot] : group.fitTree[right + slot];
            }
        }

        [[nodiscard]] u32 FindCompatibilityGroupHeap(const PlacedPlannerCompatibilityGroup& group, const u32 slot, const u64 size) noexcept
        {
            if (group.treeCapacity == 0 || group.fitTree[group.alignmentLevelCount + slot] < size)
                return InvalidRenderFlowResourceIndex;
            u32 node = 1;
            while (node < group.treeCapacity)
            {
                const u32 left = node * 2u;
                if (group.fitTree[static_cast<u64>(left) * group.alignmentLevelCount + slot] >= size)
                    node = left;
                else
                    node = left + 1u;
            }
            const u32 position = node - group.treeCapacity;
            return position < group.heapIndices.Size() ? group.heapIndices[position] : InvalidRenderFlowResourceIndex;
        }

        [[nodiscard]] u32 FindOrCreateCompatibilityGroup(PlacedRangePlannerScratch& scratch, const PlacedRangeRequest& request, const u64 key,
                                                          const u64 heapAlignment) noexcept
        {
            u32 head = InvalidRenderFlowResourceIndex;
            static_cast<void>(scratch.compatibleHeaps.Find(key, head));
            for (u32 groupIndex = head; groupIndex != InvalidRenderFlowResourceIndex; groupIndex = scratch.groups[groupIndex].nextCollision)
                if (CompatibilityGroupMatches(scratch.groups[groupIndex].exemplar, request))
                    return groupIndex;

            PlacedPlannerCompatibilityGroup& group = scratch.AcquireGroup();
            const u32 groupIndex = scratch.activeGroupCount - 1u;
            group.exemplar.kind = request.kind;
            group.exemplar.queue = request.queue;
            group.exemplar.memoryType = request.requirements.memoryType;
            group.exemplar.heapCategory = request.requirements.heapCategory;
            group.exemplar.compatibilityClass = request.requirements.compatibilityClass;
            group.exemplar.alignment = heapAlignment;
            group.nextCollision = head;
            if (!scratch.compatibleHeaps.Set(key, groupIndex))
                return InvalidRenderFlowResourceIndex;
            return groupIndex;
        }

        void AddHeapToCompatibilityGroup(PlacedRangePlannerScratch& scratch, const u32 groupIndex, const u32 heapIndex) noexcept
        {
            PlacedPlannerCompatibilityGroup& group = scratch.groups[groupIndex];
            HeapState& heap = scratch.heaps[heapIndex];
            heap.compatibilityGroup = groupIndex;
            heap.compatibilityPosition = group.heapIndices.Size();
            group.heapIndices.PushBack(heapIndex);
            if (group.treeCapacity < group.heapIndices.Size())
                RebuildCompatibilityGroupTree(group, scratch.heaps);
            else
                UpdateCompatibilityGroupHeap(group, heap.compatibilityPosition, heap);
        }

        void UpdateHeapCompatibilityFit(PlacedRangePlannerScratch& scratch, const u32 heapIndex) noexcept
        {
            HeapState& heap = scratch.heaps[heapIndex];
            VG_ASSERT(heap.compatibilityGroup < scratch.activeGroupCount);
            UpdateCompatibilityGroupHeap(scratch.groups[heap.compatibilityGroup], heap.compatibilityPosition, heap);
        }

        [[nodiscard]] bool ReleaseEventAfter(const PlacedPlannerReleaseEvent& left, const PlacedPlannerReleaseEvent& right) noexcept
        {
            if (left.release != right.release)
                return PositionBefore(right.release, left.release);
            if (left.heap != right.heap)
                return left.heap > right.heap;
            if (left.offset != right.offset)
                return left.offset > right.offset;
            return left.allocation > right.allocation;
        }

        void ReleaseBefore(PlacedRangePlannerScratch& scratch, const PlanPosition acquire) noexcept
        {
            while (!scratch.releaseEvents.Empty() && PositionBefore(scratch.releaseEvents[0].release, acquire))
            {
                std::pop_heap(scratch.releaseEvents.Begin(), scratch.releaseEvents.End(), ReleaseEventAfter);
                const PlacedPlannerReleaseEvent event = scratch.releaseEvents.Back();
                scratch.releaseEvents.PopBack();
                InsertReleasedRange(scratch.heaps[event.heap], event.offset, event.size, event.allocation);
                UpdateHeapCompatibilityFit(scratch, event.heap);
            }
        }
    } // namespace

    void PlacedRangePlan::Clear() noexcept
    {
        heaps.Clear();
        assignments.Clear();
        predecessors.Clear();
    }

    bool BuildPlacedRangePlan(const containers::ArraySpan<const PlacedRangeRequest> requests, const PlacedRangePlannerConfig& config, PlacedRangePlan& plan,
                              PlacedRangePlannerScratch& scratch, RenderFlowResourceFailure* const failure) noexcept
    {
        plan.Clear();
        scratch.Reset();
        if (failure != nullptr)
            *failure = {};
        const auto reject = [&](const RenderFlowResourceFailureCode code, const char* const message) noexcept
        {
            plan.Clear();
            scratch.Finish();
            return PlannerFail(failure, code, message);
        };
        if (config.minimumHeapBytes == 0 || !IsPowerOfTwo(config.heapAlignment))
            return reject(RenderFlowResourceFailureCode::InvalidConfiguration, "placed-range planner requires a nonzero heap size and power-of-two heap alignment");

        containers::DynamicArray<u32>& order = scratch.order;
        containers::DynamicArray<PlacedPredecessorFragment>& candidateFragments = scratch.candidateFragments;
        ProvenanceConsumeWindow provenanceWindow;
        order.Reserve(requests.Size());
        scratch.releaseEvents.Reserve(requests.Size());
        scratch.seenAllocations.Reserve(requests.Size());
        scratch.compatibleHeaps.Reserve(requests.Size());
        plan.assignments.Reserve(requests.Size());
        for (u32 index = 0; index < requests.Size(); ++index)
        {
            const PlacedRangeRequest& request = requests[index];
            const bool validKind = request.kind == FrameResourceKind::Texture || request.kind == FrameResourceKind::Buffer;
            const bool validQueue = request.queue == rhi::QueueType::Graphics || request.queue == rhi::QueueType::Compute;
            const bool validCategory = (request.kind == FrameResourceKind::Texture && request.requirements.heapCategory == rhi::PlacedHeapCategory::Texture) ||
                                       (request.kind == FrameResourceKind::Buffer && request.requirements.heapCategory == rhi::PlacedHeapCategory::Buffer);
            if (request.allocation == InvalidRenderFlowResourceIndex || !validKind || !validQueue || !request.firstAcquire.IsValid() || !request.lastRelease.IsValid() ||
                PositionBefore(request.lastRelease, request.firstAcquire) || request.requirements.size == 0 || !IsPowerOfTwo(request.requirements.alignment) ||
                request.requirements.alignment > config.heapAlignment || request.requirements.compatibilityClass == 0 || request.requirements.memoryType != rhi::MemoryType::DeviceLocal || !validCategory)
                return reject(validQueue ? RenderFlowResourceFailureCode::BackendContractViolation : RenderFlowResourceFailureCode::UnsupportedCapability,
                              validQueue ? "placed-range request violates the Stage 3 placement profile" : "placed-range planning supports only graphics or compute queues");
            if (!scratch.seenAllocations.Insert(request.allocation))
                return reject(RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "placed-range request duplicates a logical allocation");
            scratch.alignmentMask |= request.requirements.alignment;
            order.PushBack(index);
        }
        scratch.alignmentLevelCount = CountAlignmentLevels(scratch.alignmentMask);

        std::sort(order.Begin(), order.End(),
                  [&requests](const u32 leftIndex, const u32 rightIndex) noexcept
                  {
                      const PlacedRangeRequest& left = requests[leftIndex];
                      const PlacedRangeRequest& right = requests[rightIndex];
                      if (left.firstAcquire != right.firstAcquire)
                          return PositionBefore(left.firstAcquire, right.firstAcquire);
                      if (left.requirements.size != right.requirements.size)
                          return left.requirements.size > right.requirements.size;
                      if (left.requirements.alignment != right.requirements.alignment)
                          return left.requirements.alignment > right.requirements.alignment;
                      return left.allocation < right.allocation;
                  });

        for (const u32 requestIndex : order)
        {
            const PlacedRangeRequest& request = requests[requestIndex];
            ReleaseBefore(scratch, request.firstAcquire);
            u32 selectedHeap = InvalidRenderFlowResourceIndex;
            u32 selectedRun = InvalidRenderFlowResourceIndex;
            u64 selectedOffset = 0;
            const u64 compatibilityKey = PlannerCompatibilityKey(request);
            const u32 groupIndex = FindOrCreateCompatibilityGroup(scratch, request, compatibilityKey, config.heapAlignment);
            if (groupIndex == InvalidRenderFlowResourceIndex)
                return reject(RenderFlowResourceFailureCode::CapacityExceeded, "placed-range compatibility index allocation failed");
            PlacedPlannerCompatibilityGroup& group = scratch.groups[groupIndex];
            const u32 alignmentSlot = AlignmentSlot(scratch.alignmentMask, request.requirements.alignment);
            selectedHeap = FindCompatibilityGroupHeap(group, alignmentSlot, request.requirements.size);

            if (selectedHeap == InvalidRenderFlowResourceIndex)
            {
                const u64 requiredCapacity = request.requirements.size > config.minimumHeapBytes ? request.requirements.size : config.minimumHeapBytes;
                u64 capacity = 0;
                if (!CheckedAlignUp(requiredCapacity, config.heapAlignment, capacity))
                    return reject(RenderFlowResourceFailureCode::ArithmeticOverflow, "placed heap capacity alignment overflows u64");
                HeapState& heap = scratch.AcquireHeap();
                heap.plan.kind = request.kind;
                heap.plan.queue = request.queue;
                heap.plan.memoryType = request.requirements.memoryType;
                heap.plan.heapCategory = request.requirements.heapCategory;
                heap.plan.compatibilityClass = request.requirements.compatibilityClass;
                heap.plan.capacity = capacity;
                heap.plan.alignment = config.heapAlignment;
                selectedHeap = scratch.activeHeapCount - 1u;
                const u32 freeRun = AllocateFreeRun(heap);
                heap.freeRuns[freeRun].offset = 0;
                heap.freeRuns[freeRun].size = capacity;
                heap.freeRuns[freeRun].virgin = true;
                InsertFreeRun(heap, freeRun);
                AddHeapToCompatibilityGroup(scratch, groupIndex, selectedHeap);
                if (!FindFirstFit(heap, request, selectedRun, selectedOffset, candidateFragments, provenanceWindow))
                    return reject(RenderFlowResourceFailureCode::BackendContractViolation, "fresh placed heap cannot satisfy the request used to size it");
            }
            else
            {
                HeapState& heap = scratch.heaps[selectedHeap];
                if (!HeapCompatible(heap.plan, request) || !FindFirstFit(heap, request, selectedRun, selectedOffset, candidateFragments, provenanceWindow))
                    return reject(RenderFlowResourceFailureCode::BackendContractViolation, "placed-range compatibility index selected an incompatible or undersized heap");
            }

            HeapState& heap = scratch.heaps[selectedHeap];
            PlacedRangeAssignment assignment;
            assignment.allocation = request.allocation;
            assignment.heap = selectedHeap;
            assignment.offset = selectedOffset;
            assignment.size = request.requirements.size;
            assignment.alignment = request.requirements.alignment;
            assignment.predecessorOffset = plan.predecessors.Size();
            assignment.predecessorCount = candidateFragments.Size();
            for (const PlacedPredecessorFragment fragment : candidateFragments)
                plan.predecessors.PushBack(fragment);
            ConsumeFreeRange(heap, selectedRun, selectedOffset, request.requirements.size, provenanceWindow);
            UpdateHeapCompatibilityFit(scratch, selectedHeap);
            scratch.releaseEvents.PushBack({request.lastRelease, selectedOffset, request.requirements.size, selectedHeap, request.allocation});
            std::push_heap(scratch.releaseEvents.Begin(), scratch.releaseEvents.End(), ReleaseEventAfter);
            plan.assignments.PushBack(assignment);
        }

        plan.heaps.Reserve(scratch.activeHeapCount);
        for (u32 heapIndex = 0; heapIndex < scratch.activeHeapCount; ++heapIndex)
            plan.heaps.PushBack(scratch.heaps[heapIndex].plan);
        scratch.Finish();
        return true;
    }

    bool BuildPlacedRangePlan(const containers::ArraySpan<const PlacedRangeRequest> requests, const PlacedRangePlannerConfig& config, PlacedRangePlan& plan, RenderFlowResourceFailure* const failure) noexcept
    {
        PlacedRangePlannerScratch scratch;
        return BuildPlacedRangePlan(requests, config, plan, scratch, failure);
    }

    namespace
    {
        enum class PlacedCacheState : u8
        {
            Vacant,
            Reusable,
            Assigned,
            PendingRetirement,
            PendingNativeDestruction,
            ProviderFailureQuarantine,
            NativeReleased
        };

        [[nodiscard]] bool TextureDescriptorEqual(const rhi::TextureDesc& left, const rhi::TextureDesc& right) noexcept
        {
            return PhysicalTextureDescEqual(left, right);
        }

        [[nodiscard]] bool BufferDescriptorEqual(const rhi::BufferDesc& left, const rhi::BufferDesc& right) noexcept
        {
            return PhysicalBufferDescEqual(left, right);
        }

        [[nodiscard]] bool ObjectDescriptorEqual(const FrameResourceDesc& left, const FrameResourceDesc& right) noexcept
        {
            if (left.kind != right.kind)
                return false;
            return left.kind == FrameResourceKind::Texture ? TextureDescriptorEqual(left.texture.active, right.texture.active)
                                                           : left.kind == FrameResourceKind::Buffer && BufferDescriptorEqual(left.buffer.active, right.buffer.active);
        }

        [[nodiscard]] bool HeapPlanEqual(const PlacedHeapPlan& left, const PlacedHeapPlan& right) noexcept
        {
            return left.kind == right.kind && left.queue == right.queue && left.memoryType == right.memoryType && left.heapCategory == right.heapCategory &&
                   left.compatibilityClass == right.compatibilityClass && left.capacity == right.capacity && left.alignment == right.alignment;
        }

        [[nodiscard]] u64 Mix64(u64 value) noexcept
        {
            value ^= value >> 30;
            value *= 0xbf58476d1ce4e5b9ull;
            value ^= value >> 27;
            value *= 0x94d049bb133111ebull;
            return value ^ (value >> 31);
        }

        [[nodiscard]] u64 HashHeapPlan(const PlacedHeapPlan& plan) noexcept
        {
            u64 hash = Mix64(static_cast<u64>(plan.kind) | (static_cast<u64>(plan.queue) << 8u) | (static_cast<u64>(plan.memoryType) << 16u) |
                             (static_cast<u64>(plan.heapCategory) << 24u));
            hash ^= Mix64(plan.compatibilityClass + 0x9e3779b97f4a7c15ull);
            hash ^= Mix64(plan.capacity + (plan.alignment << 1u));
            return hash;
        }

        [[nodiscard]] u64 HashObjectDescriptor(const FrameResourceDesc& desc) noexcept
        {
            if (desc.kind == FrameResourceKind::Texture)
            {
                const rhi::TextureDesc& value = desc.texture.active;
                u64 hash = Mix64(static_cast<u64>(value.extent.width) | (static_cast<u64>(value.extent.height) << 21u) | (static_cast<u64>(value.extent.depth) << 42u));
                hash ^= Mix64(static_cast<u64>(value.dimension) | (static_cast<u64>(value.format) << 8u) | (static_cast<u64>(value.mipCount) << 24u) |
                              (static_cast<u64>(value.arraySize) << 40u) | (static_cast<u64>(value.sampleCount) << 56u));
                hash ^= Mix64(static_cast<u64>(value.usage) ^ (static_cast<u64>(value.initialState) << 32u));
                hash ^= Mix64(static_cast<u64>(value.virtualResource) | (static_cast<u64>(value.keepInitialState) << 1u));
                return hash;
            }
            const rhi::BufferDesc& value = desc.buffer.active;
            u64 hash = Mix64(value.size);
            hash ^= Mix64(static_cast<u64>(value.structureStride) | (static_cast<u64>(value.format) << 32u));
            hash ^= Mix64(static_cast<u64>(value.usage) ^ (static_cast<u64>(value.initialState) << 32u));
            hash ^= Mix64(static_cast<u64>(value.memoryType) | (static_cast<u64>(value.virtualResource) << 8u) | (static_cast<u64>(value.keepInitialState) << 9u));
            return hash;
        }

        [[nodiscard]] u64 HashPlacedObject(const u32 heapEntry, const u64 offset, const FrameResourceDesc& desc) noexcept
        {
            return Mix64(static_cast<u64>(heapEntry) ^ (offset * 0x9e3779b97f4a7c15ull) ^ HashObjectDescriptor(desc));
        }

        [[nodiscard]] bool FencesComplete(const rhi::ResidencyFenceSet& fences) noexcept
        {
            return (fences.graphics == 0 || rhi::IsGpuFenceComplete({rhi::QueueType::Graphics, fences.graphics})) &&
                   (fences.compute == 0 || rhi::IsGpuFenceComplete({rhi::QueueType::Compute, fences.compute})) && (fences.copy == 0 || rhi::IsGpuFenceComplete({rhi::QueueType::Copy, fences.copy}));
        }

        [[nodiscard]] bool PlacedFail(RenderFlowResourceFailure* const failure, const RenderFlowResourceFailureCode code, const char* const message,
                                      const RenderFlowResourceSessionState phase = RenderFlowResourceSessionState::Resolving) noexcept
        {
            if (failure != nullptr)
                *failure = {code, phase, {}, {}, {}, message};
            return false;
        }

        [[nodiscard]] rhi::DescriptorRetirement DescriptorRetirementFor(const rhi::ResidencyFenceSet& fences) noexcept
        {
            return {fences.graphics, fences.compute, fences.copy};
        }
    } // namespace

    struct PlacedResourcePool::Impl
    {
        struct HeapEntry
        {
            PlacedHeapPlan plan;
            rhi::Heap heap;
            rhi::ResidencyFenceSet safeAfter;
            rhi::NativeReleaseObservation releaseObservation;
            u64 lastUsedFrame = 0;
            u64 reusableKey = 0;
            u32 previousReusable = InvalidPlacedResourceEntry;
            u32 nextReusable = InvalidPlacedResourceEntry;
            u32 objectHead = InvalidPlacedResourceEntry;
            u32 objectTail = InvalidPlacedResourceEntry;
            u32 objectCount = 0;
            u32 pendingPosition = InvalidPlacedResourceEntry;
            u32 trimPosition = InvalidPlacedResourceEntry;
            u32 heldOwnerWitness = InvalidPlacedResourceEntry;
            // While a reusable heap is held for external object owners, entries
            // before this cursor have already been proven pool-owned during the
            // current held episode. The witness itself is checked separately.
            u32 heldOwnerScanCursor = InvalidPlacedResourceEntry;
            u64 transactionSerial = 0;
            PlacedCacheState state = PlacedCacheState::Vacant;
            bool reusableAfterRetirement = true;
            bool reusableIndexed = false;
        };

        struct ObjectEntry
        {
            FrameResourceDesc desc;
            rhi::Texture texture;
            rhi::Buffer buffer;
            CanonicalResourceDescriptors descriptors;
            rhi::PlacementRecord placement;
            u32 heapEntry = InvalidPlacedResourceEntry;
            u64 lastUsedFrame = 0;
            u64 reusableKey = 0;
            u32 previousReusable = InvalidPlacedResourceEntry;
            u32 nextReusable = InvalidPlacedResourceEntry;
            u32 previousInHeap = InvalidPlacedResourceEntry;
            u32 nextInHeap = InvalidPlacedResourceEntry;
            u32 descriptorReusablePosition = InvalidPlacedResourceEntry;
            u64 transactionSerial = 0;
            PlacedCacheState state = PlacedCacheState::Vacant;
            bool reusableIndexed = false;
        };

        Impl(const RenderFlowResourceAllocatorConfig& allocatorConfig, AllocatorNativeByteLedger& nativeByteLedger) noexcept
            : heaps(memory::pools::Rendering::GetInstance()), objects(memory::pools::Rendering::GetInstance()), freeHeaps(memory::pools::Rendering::GetInstance()),
              freeObjects(memory::pools::Rendering::GetInstance()), reusableTextureTrimHeap(memory::pools::Rendering::GetInstance()), reusableBufferTrimHeap(memory::pools::Rendering::GetInstance()),
              descriptorReusableTrim(memory::pools::Rendering::GetInstance()),
              heldReusable(memory::pools::Rendering::GetInstance()), pendingRetirement(memory::pools::Rendering::GetInstance()), pendingQuarantine(memory::pools::Rendering::GetInstance()),
              pendingDestruction(memory::pools::Rendering::GetInstance()), localDescriptorLedger(0, 0), ledger(&nativeByteLedger), descriptorLedger(&localDescriptorLedger),
              textureSoftTargetBytes(allocatorConfig.textureSoftTargetBytes), bufferSoftTargetBytes(allocatorConfig.bufferSoftTargetBytes)
        {
        }

        Impl(const RenderFlowResourceAllocatorConfig& allocatorConfig, AllocatorNativeByteLedger& nativeByteLedger, AllocatorDescriptorLedger& canonicalDescriptorLedger,
             const rhi::DescriptorDomainRef canonicalDescriptorDomain) noexcept
            : heaps(memory::pools::Rendering::GetInstance()), objects(memory::pools::Rendering::GetInstance()), freeHeaps(memory::pools::Rendering::GetInstance()),
              freeObjects(memory::pools::Rendering::GetInstance()), reusableTextureTrimHeap(memory::pools::Rendering::GetInstance()), reusableBufferTrimHeap(memory::pools::Rendering::GetInstance()),
              descriptorReusableTrim(memory::pools::Rendering::GetInstance()), heldReusable(memory::pools::Rendering::GetInstance()),
              pendingRetirement(memory::pools::Rendering::GetInstance()), pendingQuarantine(memory::pools::Rendering::GetInstance()), pendingDestruction(memory::pools::Rendering::GetInstance()),
              localDescriptorLedger(0, 0), ledger(&nativeByteLedger), descriptorLedger(&canonicalDescriptorLedger), descriptorDomain(canonicalDescriptorDomain),
              textureSoftTargetBytes(allocatorConfig.textureSoftTargetBytes), bufferSoftTargetBytes(allocatorConfig.bufferSoftTargetBytes)
        {
        }

        containers::DynamicArray<HeapEntry> heaps;
        containers::DynamicArray<ObjectEntry> objects;
        containers::DynamicArray<u32> freeHeaps;
        containers::DynamicArray<u32> freeObjects;
        containers::DynamicArray<u32> reusableTextureTrimHeap;
        containers::DynamicArray<u32> reusableBufferTrimHeap;
        containers::DynamicArray<u32> descriptorReusableTrim;
        containers::DynamicArray<u32> heldReusable;
        containers::DynamicArray<u32> pendingRetirement;
        containers::DynamicArray<u32> pendingQuarantine;
        containers::DynamicArray<u32> pendingDestruction;
        ResolveScratchHashMap<u64, u32> reusableHeapHeads;
        ResolveScratchHashMap<u64, u32> reusableObjectHeads;
        ResolveScratchHashMap<u64, u32> reusableObjectTails;
        ResolveScratchHashMap<u32, u32> requestByAllocation;
        mutable concurrency::SpinLock lock;
        AllocatorDescriptorLedger localDescriptorLedger;
        AllocatorNativeByteLedger* ledger = nullptr;
        AllocatorDescriptorLedger* descriptorLedger = nullptr;
        rhi::DescriptorDomainRef descriptorDomain;
        u64 textureSoftTargetBytes = ~u64{0};
        u64 bufferSoftTargetBytes = ~u64{0};

        [[nodiscard]] static bool FreeSlotAfter(const u32 left, const u32 right) noexcept
        {
            return left > right;
        }
        PlacedResourcePoolStats stats;
        u64 nextTransactionSerial = 1;

        [[nodiscard]] bool PoolOwnsOnlyObjectReference(const ObjectEntry& object) const noexcept
        {
            const i32 expected = 1 + static_cast<i32>(CanonicalDescriptorReferenceCount(object.descriptors));
            if (object.texture.IsValid())
                return rhi::GetRefCount(object.texture.GetRef()) == expected;
            return object.buffer.IsValid() && rhi::GetRefCount(object.buffer.GetRef()) == expected;
        }

        [[nodiscard]] bool RetireCanonicalDescriptors(ObjectEntry& object, const rhi::DescriptorRetirement& retirement,
                                                      RenderFlowResourceFailure* const failure,
                                                      const RenderFlowResourceSessionState phase = RenderFlowResourceSessionState::Resolving,
                                                      bool* const deviceLostEvidence = nullptr) noexcept
        {
            if (!descriptorDomain.IsValid())
            {
                VG_ASSERT(CanonicalDescriptorReferenceCount(object.descriptors) == 0);
                return true;
            }
            for (rhi::DescriptorHandle* const descriptor : {&object.descriptors.shaderResource, &object.descriptors.unorderedAccess})
            {
                if (!descriptor->IsValid())
                    continue;
                rhi::Failure rhiFailure;
                bool retired = false;
                {
                    VG_PROFILE_ZONE_NAMED("RenderFlow/CanonicalDescriptor/Retire");
                    retired = rhi::RetireDescriptor(descriptorDomain, *descriptor, retirement, &rhiFailure);
                }
                if (!retired)
                {
                    if (deviceLostEvidence != nullptr && rhiFailure.code == rhi::FailureCode::DeviceLost)
                        *deviceLostEvidence = true;
                    return PlacedFail(failure, MapRhiFailure(rhiFailure, RhiFailureContext::BackendContract),
                                      rhiFailure.message[0] != '\0' ? rhiFailure.message : "placed canonical descriptor retirement failed", phase);
                }
                *descriptor = {};
                if (object.descriptorReusablePosition != InvalidPlacedResourceEntry)
                    descriptorLedger->MarkNotReusable(1);
                descriptorLedger->Release(1);
            }
            if (CanonicalDescriptorReferenceCount(object.descriptors) == 0)
            {
                if (object.descriptorReusablePosition != InvalidPlacedResourceEntry)
                    RemoveDescriptorReusable(object, false);
                object.descriptors.lastDemandFrame = 0;
            }
            return true;
        }

        [[nodiscard]] bool EnsureCanonicalDescriptors(ObjectEntry& object, const u64 frameSerial, RenderFlowResourceFailure* const failure,
                                                      CanonicalDescriptorPressure* const descriptorPressure = nullptr,
                                                      const bool recordPublicationStats = true) noexcept
        {
            if (descriptorPressure != nullptr)
                *descriptorPressure = {};
            if (!descriptorDomain.IsValid())
                return true;
            const bool buffer = object.buffer.IsValid();
            if (buffer == object.texture.IsValid())
                return PlacedFail(failure, RenderFlowResourceFailureCode::BackendContractViolation, "placed canonical descriptor source kind is invalid");
            const bool needShaderResource = buffer ? HasAny(object.desc.buffer.active.usage, rhi::BufferUsage::ShaderResource)
                                                   : HasAny(object.desc.texture.active.usage, rhi::TextureUsage::ShaderResource);
            const bool needUnorderedAccess = buffer ? HasAny(object.desc.buffer.active.usage, rhi::BufferUsage::UnorderedAccess)
                                                    : HasAny(object.desc.texture.active.usage, rhi::TextureUsage::UnorderedAccess);
            const u32 missing = static_cast<u32>(needShaderResource && !object.descriptors.shaderResource.IsValid()) +
                                static_cast<u32>(needUnorderedAccess && !object.descriptors.unorderedAccess.IsValid());
            const u32 needed = static_cast<u32>(needShaderResource) + static_cast<u32>(needUnorderedAccess);
            if (missing == 0)
            {
                if (needed != 0)
                {
                    VG_PROFILE_ZONE_NAMED("RenderFlow/CanonicalDescriptor/Hit");
                    if (recordPublicationStats)
                        descriptorLedger->RecordPublicationHit();
                }
                object.descriptors.lastDemandFrame = frameSerial;
                return true;
            }
            {
                VG_PROFILE_ZONE_NAMED("RenderFlow/CanonicalDescriptor/Miss");
                if (recordPublicationStats)
                    descriptorLedger->RecordPublicationMiss();
            }

            const u32 maximumAttempts = descriptorPressure != nullptr ? 1u : 2u;
            for (u32 attempt = 0; attempt < maximumAttempts; ++attempt)
            {
                if (!descriptorLedger->TryReserve(missing))
                {
                    if (descriptorPressure != nullptr)
                    {
                        *descriptorPressure = {CanonicalDescriptorPressureKind::LocalLimit, missing};
                        return false;
                    }
                    while (!descriptorLedger->CanReserve(missing) || descriptorLedger->GetStats().reusableSlots > descriptorLedger->GetTarget())
                    {
                        const u32 before = descriptorLedger->GetStats().liveSlots;
                        if (!EvictOneColdDescriptorBundle(failure, RenderFlowResourceSessionState::Resolving, true) || descriptorLedger->GetStats().liveSlots >= before)
                            return PlacedFail(failure, RenderFlowResourceFailureCode::CapacityExceeded, "canonical descriptor cache hard limit is exhausted");
                    }
                    if (!descriptorLedger->TryReserve(missing))
                        return PlacedFail(failure, RenderFlowResourceFailureCode::CapacityExceeded, "canonical descriptor cache hard limit is exhausted");
                }

                rhi::DescriptorHandle createdShaderResource;
                rhi::DescriptorHandle createdUnorderedAccess;
                rhi::Failure rhiFailure;
                bool allocationCapacityFailure = false;
                bool published = true;
                const auto create = [&](const bool writable, rhi::DescriptorHandle& created) noexcept
                {
                    {
                        VG_PROFILE_ZONE_NAMED("RenderFlow/CanonicalDescriptor/Allocate");
                        created = rhi::AllocateDescriptor(descriptorDomain, &rhiFailure);
                    }
                    if (!created.IsValid())
                    {
                        allocationCapacityFailure = rhiFailure.code == rhi::FailureCode::CapacityExceeded;
                        return false;
                    }
                    bool written = false;
                    {
                        VG_PROFILE_ZONE_NAMED("RenderFlow/CanonicalDescriptor/Write");
                        if (buffer)
                        {
                            const rhi::BufferUsage usage = object.desc.buffer.active.usage;
                            const rhi::BindingType type = HasAny(usage, rhi::BufferUsage::Structured)
                                                              ? (writable ? rhi::BindingType::StructuredBufferUnorderedAccess : rhi::BindingType::StructuredBufferShaderResource)
                                                          : HasAny(usage, rhi::BufferUsage::Raw)
                                                              ? (writable ? rhi::BindingType::ByteAddressBufferUnorderedAccess : rhi::BindingType::ByteAddressBufferShaderResource)
                                                              : (writable ? rhi::BindingType::TypedBufferUnorderedAccess : rhi::BindingType::TypedBufferShaderResource);
                            written = rhi::WriteDescriptor(descriptorDomain, created, object.buffer.GetRef(), type, {}, &rhiFailure);
                        }
                        else
                        {
                            written = rhi::WriteDescriptor(descriptorDomain, created, object.texture.GetRef(),
                                                           writable ? rhi::BindingType::TextureUnorderedAccess : rhi::BindingType::TextureShaderResource, {}, &rhiFailure);
                        }
                    }
                    return written;
                };

                if (needShaderResource && !object.descriptors.shaderResource.IsValid())
                    published = create(false, createdShaderResource);
                if (published && needUnorderedAccess && !object.descriptors.unorderedAccess.IsValid())
                    published = create(true, createdUnorderedAccess);
                if (published)
                {
                    if (createdShaderResource.IsValid())
                        object.descriptors.shaderResource = createdShaderResource;
                    if (createdUnorderedAccess.IsValid())
                        object.descriptors.unorderedAccess = createdUnorderedAccess;
                    object.descriptors.lastDemandFrame = frameSerial;
                    return true;
                }

                if (createdShaderResource.IsValid())
                {
                    VG_PROFILE_ZONE_NAMED("RenderFlow/CanonicalDescriptor/Retire");
                    static_cast<void>(rhi::RetireDescriptor(descriptorDomain, createdShaderResource, {}));
                }
                if (createdUnorderedAccess.IsValid())
                {
                    VG_PROFILE_ZONE_NAMED("RenderFlow/CanonicalDescriptor/Retire");
                    static_cast<void>(rhi::RetireDescriptor(descriptorDomain, createdUnorderedAccess, {}));
                }
                descriptorLedger->Release(missing);
                if (allocationCapacityFailure && descriptorPressure != nullptr)
                {
                    *descriptorPressure = {CanonicalDescriptorPressureKind::SharedDomain, missing};
                    return false;
                }
                if (!allocationCapacityFailure || attempt != 0)
                    return PlacedFail(failure, allocationCapacityFailure ? RenderFlowResourceFailureCode::CapacityExceeded : MapRhiFailure(rhiFailure, RhiFailureContext::BackendContract),
                                      rhiFailure.message[0] != '\0' ? rhiFailure.message : "placed canonical descriptor publication failed");
                while (rhi::GetDescriptorDomainStats(descriptorDomain).free < missing)
                {
                    const u32 before = descriptorLedger->GetStats().liveSlots;
                    if (!EvictOneColdDescriptorBundle(failure, RenderFlowResourceSessionState::Resolving, true) || descriptorLedger->GetStats().liveSlots >= before)
                        return PlacedFail(failure, RenderFlowResourceFailureCode::CapacityExceeded, "placed canonical descriptor shared domain is exhausted");
                }
            }
            return false;
        }

        void AddReusableHeap(const u32 index) noexcept
        {
            HeapEntry& entry = heaps[index];
            if (entry.reusableIndexed)
                return;
            entry.reusableKey = HashHeapPlan(entry.plan);
            u32 head = InvalidPlacedResourceEntry;
            static_cast<void>(reusableHeapHeads.Find(entry.reusableKey, head));
            entry.previousReusable = InvalidPlacedResourceEntry;
            entry.nextReusable = head;
            if (head != InvalidPlacedResourceEntry)
                heaps[head].previousReusable = index;
            if (head == InvalidPlacedResourceEntry)
            {
                const bool inserted = reusableHeapHeads.Insert(entry.reusableKey, index);
                VG_ASSERT(inserted);
            }
            else
            {
                const bool erased = reusableHeapHeads.Erase(entry.reusableKey);
                const bool inserted = reusableHeapHeads.Insert(entry.reusableKey, index);
                VG_ASSERT(erased && inserted);
            }
            entry.reusableIndexed = true;
            InsertTrimHeap(index);
        }

        void RemoveReusableHeap(const u32 index) noexcept
        {
            HeapEntry& entry = heaps[index];
            if (!entry.reusableIndexed)
                return;
            RemoveTrimHeap(index);
            if (entry.previousReusable == InvalidPlacedResourceEntry)
            {
                const bool erased = reusableHeapHeads.Erase(entry.reusableKey);
                VG_ASSERT(erased);
                if (entry.nextReusable != InvalidPlacedResourceEntry)
                {
                    const bool inserted = reusableHeapHeads.Insert(entry.reusableKey, entry.nextReusable);
                    VG_ASSERT(inserted);
                }
            }
            else
                heaps[entry.previousReusable].nextReusable = entry.nextReusable;
            if (entry.nextReusable != InvalidPlacedResourceEntry)
                heaps[entry.nextReusable].previousReusable = entry.previousReusable;
            entry.previousReusable = entry.nextReusable = InvalidPlacedResourceEntry;
            entry.reusableIndexed = false;
        }

        [[nodiscard]] bool DescriptorReusableLess(const u32 left, const u32 right) const noexcept
        {
            const ObjectEntry& leftEntry = objects[left];
            const ObjectEntry& rightEntry = objects[right];
            return leftEntry.descriptors.lastDemandFrame != rightEntry.descriptors.lastDemandFrame
                       ? leftEntry.descriptors.lastDemandFrame < rightEntry.descriptors.lastDemandFrame
                       : left < right;
        }

        void SwapDescriptorReusable(const u32 leftPosition, const u32 rightPosition) noexcept
        {
            const u32 left = descriptorReusableTrim[leftPosition];
            const u32 right = descriptorReusableTrim[rightPosition];
            descriptorReusableTrim[leftPosition] = right;
            descriptorReusableTrim[rightPosition] = left;
            objects[left].descriptorReusablePosition = rightPosition;
            objects[right].descriptorReusablePosition = leftPosition;
        }

        void SiftDescriptorReusableUp(u32 position) noexcept
        {
            while (position != 0)
            {
                const u32 parent = (position - 1u) / 2u;
                if (!DescriptorReusableLess(descriptorReusableTrim[position], descriptorReusableTrim[parent]))
                    break;
                SwapDescriptorReusable(position, parent);
                position = parent;
            }
        }

        void SiftDescriptorReusableDown(u32 position) noexcept
        {
            for (;;)
            {
                const u32 left = position * 2u + 1u;
                if (left >= descriptorReusableTrim.Size())
                    break;
                const u32 right = left + 1u;
                u32 best = left;
                if (right < descriptorReusableTrim.Size() && DescriptorReusableLess(descriptorReusableTrim[right], descriptorReusableTrim[left]))
                    best = right;
                if (!DescriptorReusableLess(descriptorReusableTrim[best], descriptorReusableTrim[position]))
                    break;
                SwapDescriptorReusable(position, best);
                position = best;
            }
        }

        void InsertDescriptorReusable(const u32 index) noexcept
        {
            ObjectEntry& entry = objects[index];
            const u32 descriptorCount = CanonicalDescriptorReferenceCount(entry.descriptors);
            if (descriptorCount == 0 || entry.descriptorReusablePosition != InvalidPlacedResourceEntry || !PoolOwnsOnlyObjectReference(entry))
                return;
            entry.descriptorReusablePosition = descriptorReusableTrim.Size();
            descriptorReusableTrim.PushBack(index);
            descriptorLedger->MarkReusable(descriptorCount);
            SiftDescriptorReusableUp(entry.descriptorReusablePosition);
        }

        void RemoveDescriptorReusable(ObjectEntry& entry, const bool updateLedger = true) noexcept
        {
            const u32 position = entry.descriptorReusablePosition;
            if (position == InvalidPlacedResourceEntry)
                return;
            VG_ASSERT(position < descriptorReusableTrim.Size());
            const u32 index = descriptorReusableTrim[position];
            VG_ASSERT(&objects[index] == &entry);
            if (updateLedger)
                descriptorLedger->MarkNotReusable(CanonicalDescriptorReferenceCount(entry.descriptors));
            const u32 last = descriptorReusableTrim.Size() - 1u;
            if (position != last)
                SwapDescriptorReusable(position, last);
            descriptorReusableTrim.PopBack();
            entry.descriptorReusablePosition = InvalidPlacedResourceEntry;
            if (position < descriptorReusableTrim.Size())
            {
                const u32 moved = descriptorReusableTrim[position];
                SiftDescriptorReusableUp(position);
                SiftDescriptorReusableDown(objects[moved].descriptorReusablePosition);
            }
        }

        [[nodiscard]] u32 ColdestReusableDescriptorObject() const noexcept
        {
            return descriptorReusableTrim.Empty() ? InvalidPlacedResourceEntry : descriptorReusableTrim[0];
        }

        [[nodiscard]] bool EvictOneColdDescriptorBundle(RenderFlowResourceFailure* const failure,
                                                        const RenderFlowResourceSessionState phase = RenderFlowResourceSessionState::Resolving,
                                                        const bool pressure = true) noexcept
        {
            for (;;)
            {
                const u32 candidate = ColdestReusableDescriptorObject();
                if (candidate == InvalidPlacedResourceEntry)
                    return false;
                ObjectEntry& entry = objects[candidate];
                if (entry.state != PlacedCacheState::Reusable || CanonicalDescriptorReferenceCount(entry.descriptors) == 0)
                    VG_FATAL("placed descriptor cold index contains an ineligible object");
                if (!PoolOwnsOnlyObjectReference(entry))
                {
                    RemoveDescriptorReusable(entry);
                    continue;
                }
                if (!RetireCanonicalDescriptors(entry, {}, failure, phase))
                    return false;
                if (pressure)
                    descriptorLedger->RecordPressureEviction();
                else
                    descriptorLedger->RecordMaintenanceEviction();
                return true;
            }
        }

        void RefreshHeapDescriptorReusable(const u32 heapIndex) noexcept
        {
            const HeapEntry& heap = heaps[heapIndex];
            for (u32 objectIndex = heap.objectHead; objectIndex != InvalidPlacedResourceEntry; objectIndex = objects[objectIndex].nextInHeap)
            {
                ObjectEntry& object = objects[objectIndex];
                if (object.state == PlacedCacheState::Reusable)
                    InsertDescriptorReusable(objectIndex);
            }
        }

        void AddReusableObject(const u32 index) noexcept
        {
            ObjectEntry& entry = objects[index];
            if (entry.reusableIndexed)
                return;
            entry.reusableKey = HashPlacedObject(entry.heapEntry, entry.placement.offset, entry.desc);
            u32 tail = InvalidPlacedResourceEntry;
            static_cast<void>(reusableObjectTails.Find(entry.reusableKey, tail));
            entry.previousReusable = tail;
            entry.nextReusable = InvalidPlacedResourceEntry;
            if (tail != InvalidPlacedResourceEntry)
            {
                objects[tail].nextReusable = index;
                const bool erased = reusableObjectTails.Erase(entry.reusableKey);
                const bool inserted = reusableObjectTails.Insert(entry.reusableKey, index);
                VG_ASSERT(erased && inserted);
            }
            else
            {
                const bool headInserted = reusableObjectHeads.Insert(entry.reusableKey, index);
                const bool tailInserted = reusableObjectTails.Insert(entry.reusableKey, index);
                VG_ASSERT(headInserted && tailInserted);
            }
            entry.reusableIndexed = true;
            InsertDescriptorReusable(index);
        }

        void RemoveReusableObject(const u32 index) noexcept
        {
            ObjectEntry& entry = objects[index];
            if (!entry.reusableIndexed)
                return;
            RemoveDescriptorReusable(entry);
            if (entry.previousReusable == InvalidPlacedResourceEntry)
            {
                const bool erased = reusableObjectHeads.Erase(entry.reusableKey);
                VG_ASSERT(erased);
                if (entry.nextReusable != InvalidPlacedResourceEntry)
                {
                    const bool inserted = reusableObjectHeads.Insert(entry.reusableKey, entry.nextReusable);
                    VG_ASSERT(inserted);
                }
            }
            else
                objects[entry.previousReusable].nextReusable = entry.nextReusable;
            if (entry.nextReusable != InvalidPlacedResourceEntry)
                objects[entry.nextReusable].previousReusable = entry.previousReusable;
            else
            {
                const bool erased = reusableObjectTails.Erase(entry.reusableKey);
                VG_ASSERT(erased);
                if (entry.previousReusable != InvalidPlacedResourceEntry)
                {
                    const bool inserted = reusableObjectTails.Insert(entry.reusableKey, entry.previousReusable);
                    VG_ASSERT(inserted);
                }
            }
            entry.previousReusable = entry.nextReusable = InvalidPlacedResourceEntry;
            entry.reusableIndexed = false;
        }

        [[nodiscard]] bool TrimLess(const u32 left, const u32 right) const noexcept
        {
            const HeapEntry& leftEntry = heaps[left];
            const HeapEntry& rightEntry = heaps[right];
            return leftEntry.lastUsedFrame != rightEntry.lastUsedFrame ? leftEntry.lastUsedFrame < rightEntry.lastUsedFrame : left < right;
        }

        [[nodiscard]] containers::DynamicArray<u32>& TrimHeap(const FrameResourceKind kind) noexcept
        {
            VG_ASSERT(kind == FrameResourceKind::Texture || kind == FrameResourceKind::Buffer);
            return kind == FrameResourceKind::Texture ? reusableTextureTrimHeap : reusableBufferTrimHeap;
        }

        void SwapTrim(containers::DynamicArray<u32>& trimHeap, const u32 leftPosition, const u32 rightPosition) noexcept
        {
            const u32 left = trimHeap[leftPosition];
            const u32 right = trimHeap[rightPosition];
            trimHeap[leftPosition] = right;
            trimHeap[rightPosition] = left;
            heaps[left].trimPosition = rightPosition;
            heaps[right].trimPosition = leftPosition;
        }

        void SiftTrimUp(containers::DynamicArray<u32>& trimHeap, u32 position) noexcept
        {
            while (position != 0)
            {
                const u32 parent = (position - 1u) / 2u;
                if (!TrimLess(trimHeap[position], trimHeap[parent]))
                    break;
                SwapTrim(trimHeap, position, parent);
                position = parent;
            }
        }

        void SiftTrimDown(containers::DynamicArray<u32>& trimHeap, u32 position) noexcept
        {
            for (;;)
            {
                const u32 left = position * 2u + 1u;
                if (left >= trimHeap.Size())
                    break;
                const u32 right = left + 1u;
                u32 best = left;
                if (right < trimHeap.Size() && TrimLess(trimHeap[right], trimHeap[left]))
                    best = right;
                if (!TrimLess(trimHeap[best], trimHeap[position]))
                    break;
                SwapTrim(trimHeap, position, best);
                position = best;
            }
        }

        void InsertTrimHeap(const u32 index) noexcept
        {
            HeapEntry& entry = heaps[index];
            if (entry.trimPosition != InvalidPlacedResourceEntry)
                return;
            containers::DynamicArray<u32>& trimHeap = TrimHeap(entry.plan.kind);
            entry.trimPosition = trimHeap.Size();
            trimHeap.PushBack(index);
            SiftTrimUp(trimHeap, entry.trimPosition);
        }

        void RemoveTrimHeap(const u32 index) noexcept
        {
            HeapEntry& entry = heaps[index];
            const u32 position = entry.trimPosition;
            if (position == InvalidPlacedResourceEntry)
                return;
            containers::DynamicArray<u32>& trimHeap = TrimHeap(entry.plan.kind);
            const u32 last = trimHeap.Size() - 1u;
            if (position != last)
                SwapTrim(trimHeap, position, last);
            trimHeap.PopBack();
            entry.trimPosition = InvalidPlacedResourceEntry;
            if (position < trimHeap.Size())
            {
                const u32 moved = trimHeap[position];
                SiftTrimUp(trimHeap, position);
                SiftTrimDown(trimHeap, heaps[moved].trimPosition);
            }
        }

        void AddPending(containers::DynamicArray<u32>& list, const u32 index) noexcept
        {
            HeapEntry& heap = heaps[index];
            VG_ASSERT(heap.pendingPosition == InvalidPlacedResourceEntry);
            heap.pendingPosition = list.Size();
            list.PushBack(index);
        }

        void RemovePending(containers::DynamicArray<u32>& list, const u32 index) noexcept
        {
            HeapEntry& heap = heaps[index];
            const u32 position = heap.pendingPosition;
            VG_ASSERT(position < list.Size() && list[position] == index);
            const u32 moved = list.Back();
            list[position] = moved;
            list.PopBack();
            if (moved != index)
                heaps[moved].pendingPosition = position;
            heap.pendingPosition = InvalidPlacedResourceEntry;
        }

        u32 StoreHeap(HeapEntry&& entry) noexcept
        {
            if (freeHeaps.Empty())
            {
                const u32 index = heaps.Size();
                heaps.PushBack(static_cast<HeapEntry&&>(entry));
                return index;
            }
            std::pop_heap(freeHeaps.Begin(), freeHeaps.End(), FreeSlotAfter);
            const u32 index = freeHeaps.Back();
            freeHeaps.PopBack();
            heaps[index] = static_cast<HeapEntry&&>(entry);
            return index;
        }

        void LinkObjectToHeap(const u32 objectIndex, const u32 heapIndex) noexcept
        {
            HeapEntry& heap = heaps[heapIndex];
            ObjectEntry& object = objects[objectIndex];
            object.heapEntry = heapIndex;
            object.previousInHeap = heap.objectTail;
            object.nextInHeap = InvalidPlacedResourceEntry;
            if (heap.objectTail != InvalidPlacedResourceEntry)
                objects[heap.objectTail].nextInHeap = objectIndex;
            else
                heap.objectHead = objectIndex;
            heap.objectTail = objectIndex;
            ++heap.objectCount;
        }

        void UnlinkObjectFromHeap(const u32 objectIndex) noexcept
        {
            ObjectEntry& object = objects[objectIndex];
            if (object.heapEntry == InvalidPlacedResourceEntry)
                return;
            HeapEntry& heap = heaps[object.heapEntry];
            if (object.previousInHeap != InvalidPlacedResourceEntry)
                objects[object.previousInHeap].nextInHeap = object.nextInHeap;
            else
                heap.objectHead = object.nextInHeap;
            if (object.nextInHeap != InvalidPlacedResourceEntry)
                objects[object.nextInHeap].previousInHeap = object.previousInHeap;
            else
                heap.objectTail = object.previousInHeap;
            VG_ASSERT(heap.objectCount != 0);
            --heap.objectCount;
            object.previousInHeap = object.nextInHeap = InvalidPlacedResourceEntry;
            object.heapEntry = InvalidPlacedResourceEntry;
        }

        u32 StoreObject(ObjectEntry&& entry, const u32 heapIndex) noexcept
        {
            u32 index = InvalidPlacedResourceEntry;
            if (freeObjects.Empty())
            {
                index = objects.Size();
                objects.PushBack(static_cast<ObjectEntry&&>(entry));
            }
            else
            {
                std::pop_heap(freeObjects.Begin(), freeObjects.End(), FreeSlotAfter);
                index = freeObjects.Back();
                freeObjects.PopBack();
                objects[index] = static_cast<ObjectEntry&&>(entry);
            }
            LinkObjectToHeap(index, heapIndex);
            return index;
        }

        void ReleaseObjectSlot(const u32 index) noexcept
        {
            ObjectEntry& object = objects[index];
            VG_ASSERT(CanonicalDescriptorReferenceCount(object.descriptors) == 0);
            RemoveReusableObject(index);
            object.texture.Reset();
            object.buffer.Reset();
            object.placement = {};
            UnlinkObjectFromHeap(index);
            object = {};
            object.state = PlacedCacheState::Vacant;
            freeObjects.PushBack(index);
            std::push_heap(freeObjects.Begin(), freeObjects.End(), FreeSlotAfter);
        }

        void ReleaseAllHeapObjects(const u32 heapIndex) noexcept
        {
            u32 objectIndex = heaps[heapIndex].objectHead;
            while (objectIndex != InvalidPlacedResourceEntry)
            {
                const u32 next = objects[objectIndex].nextInHeap;
                ReleaseObjectSlot(objectIndex);
                objectIndex = next;
            }
            VG_ASSERT(heaps[heapIndex].objectCount == 0 && heaps[heapIndex].objectHead == InvalidPlacedResourceEntry && heaps[heapIndex].objectTail == InvalidPlacedResourceEntry);
        }

        [[nodiscard]] bool RetireAllHeapObjectDescriptors(const u32 heapIndex, const rhi::DescriptorRetirement& retirement,
                                                          RenderFlowResourceFailure* const failure,
                                                          const RenderFlowResourceSessionState phase = RenderFlowResourceSessionState::Resolving,
                                                          bool* const deviceLostEvidence = nullptr) noexcept
        {
            for (u32 objectIndex = heaps[heapIndex].objectHead; objectIndex != InvalidPlacedResourceEntry; objectIndex = objects[objectIndex].nextInHeap)
                if (!RetireCanonicalDescriptors(objects[objectIndex], retirement, failure, phase, deviceLostEvidence))
                    return false;
            return true;
        }

        void ReleaseHeapSlot(const u32 index) noexcept
        {
            HeapEntry& heap = heaps[index];
            VG_ASSERT(heap.objectCount == 0 && heap.objectHead == InvalidPlacedResourceEntry && heap.objectTail == InvalidPlacedResourceEntry);
            RemoveReusableHeap(index);
            VG_ASSERT(heap.pendingPosition == InvalidPlacedResourceEntry);
            heap = {};
            heap.state = PlacedCacheState::Vacant;
            freeHeaps.PushBack(index);
            std::push_heap(freeHeaps.Begin(), freeHeaps.End(), FreeSlotAfter);
        }

        enum class HeapOwnershipProof : u8
        {
            ReusableOwned,
            PendingOwned,
            Blocked
        };

        [[nodiscard]] bool ObjectBlocksPoolOwnership(const u32 heapIndex, const u32 objectIndex) const noexcept
        {
            if (objectIndex >= objects.Size())
                return true;
            const ObjectEntry& object = objects[objectIndex];
            if (object.heapEntry != heapIndex || object.state != PlacedCacheState::Reusable)
                return true;
            return !PoolOwnsOnlyObjectReference(object);
        }

        void BeginHeldOwnershipProof(const u32 heapIndex, const u32 blocker) noexcept
        {
            HeapEntry& heap = heaps[heapIndex];
            heap.heldOwnerWitness = blocker;
            // InspectHeapOwnership scanned every object before blocker already.
            // Resume after that blocker once its external owners disappear. An
            // unexpected blocker-less result restarts from the head defensively.
            heap.heldOwnerScanCursor = blocker < objects.Size() && objects[blocker].heapEntry == heapIndex ? objects[blocker].nextInHeap : heap.objectHead;
        }

        [[nodiscard]] bool ContinueHeldOwnershipProof(const u32 heapIndex) noexcept
        {
            HeapEntry& heap = heaps[heapIndex];
            if (heap.heldOwnerWitness != InvalidPlacedResourceEntry)
            {
                if (ObjectBlocksPoolOwnership(heapIndex, heap.heldOwnerWitness))
                    return false;
                heap.heldOwnerWitness = InvalidPlacedResourceEntry;
            }

            u32 objectIndex = heap.heldOwnerScanCursor;
            while (objectIndex != InvalidPlacedResourceEntry)
            {
                if (objectIndex >= objects.Size())
                {
                    heap.heldOwnerWitness = objectIndex;
                    heap.heldOwnerScanCursor = InvalidPlacedResourceEntry;
                    return false;
                }
                const u32 next = objects[objectIndex].nextInHeap;
                if (ObjectBlocksPoolOwnership(heapIndex, objectIndex))
                {
                    heap.heldOwnerWitness = objectIndex;
                    heap.heldOwnerScanCursor = next;
                    return false;
                }
                objectIndex = next;
                heap.heldOwnerScanCursor = objectIndex;
            }
            return true;
        }

        [[nodiscard]] HeapOwnershipProof InspectHeapOwnership(const u32 heapIndex, u32* const blocker = nullptr) const noexcept
        {
            const HeapEntry& heap = heaps[heapIndex];
            bool allReusable = true;
            if (blocker != nullptr)
                *blocker = InvalidPlacedResourceEntry;
            for (u32 objectIndex = heap.objectHead; objectIndex != InvalidPlacedResourceEntry; objectIndex = objects[objectIndex].nextInHeap)
            {
                const ObjectEntry& object = objects[objectIndex];
                if (object.heapEntry != heapIndex || (object.state != PlacedCacheState::Reusable && object.state != PlacedCacheState::PendingRetirement))
                {
                    if (blocker != nullptr)
                        *blocker = objectIndex;
                    return HeapOwnershipProof::Blocked;
                }
                if (object.state != PlacedCacheState::Reusable)
                    allReusable = false;
                if (!PoolOwnsOnlyObjectReference(object))
                {
                    if (blocker != nullptr)
                        *blocker = allReusable ? objectIndex : InvalidPlacedResourceEntry;
                    return HeapOwnershipProof::Blocked;
                }
            }
            return allReusable ? HeapOwnershipProof::ReusableOwned : HeapOwnershipProof::PendingOwned;
        }

        [[nodiscard]] bool PoolOwnsAllHeapObjects(const u32 heapIndex) const noexcept
        {
            return InspectHeapOwnership(heapIndex) != HeapOwnershipProof::Blocked;
        }

        [[nodiscard]] u32 CompactReusableHeapSlot(const u32 heapIndex) noexcept
        {
            HeapEntry& source = heaps[heapIndex];
            VG_ASSERT(source.state == PlacedCacheState::Reusable && source.pendingPosition == InvalidPlacedResourceEntry && !source.reusableIndexed && source.trimPosition == InvalidPlacedResourceEntry);
            if (freeHeaps.Empty() || freeHeaps[0] >= heapIndex)
                return heapIndex;

            std::pop_heap(freeHeaps.Begin(), freeHeaps.End(), FreeSlotAfter);
            const u32 targetIndex = freeHeaps.Back();
            freeHeaps.PopBack();
            VG_ASSERT(targetIndex < heapIndex && heaps[targetIndex].state == PlacedCacheState::Vacant);

            for (u32 objectIndex = source.objectHead; objectIndex != InvalidPlacedResourceEntry; objectIndex = objects[objectIndex].nextInHeap)
            {
                ObjectEntry& object = objects[objectIndex];
                const bool reindex = object.reusableIndexed;
                if (reindex)
                    RemoveReusableObject(objectIndex);
                object.heapEntry = targetIndex;
                if (reindex)
                    AddReusableObject(objectIndex);
            }

            heaps[targetIndex] = static_cast<HeapEntry&&>(source);
            source = {};
            source.state = PlacedCacheState::Vacant;
            freeHeaps.PushBack(heapIndex);
            std::push_heap(freeHeaps.Begin(), freeHeaps.End(), FreeSlotAfter);
            return targetIndex;
        }

        u32 PublishReusableHeapAfterOwnershipProof(const u32 heapIndex) noexcept
        {
            const u32 publishedIndex = CompactReusableHeapSlot(heapIndex);
            HeapEntry& heap = heaps[publishedIndex];
            VG_ASSERT(heap.state == PlacedCacheState::Reusable && heap.pendingPosition == InvalidPlacedResourceEntry && !heap.reusableIndexed);
            heap.heldOwnerWitness = InvalidPlacedResourceEntry;
            heap.heldOwnerScanCursor = InvalidPlacedResourceEntry;
            RefreshHeapDescriptorReusable(publishedIndex);
            AddReusableHeap(publishedIndex);
            return publishedIndex;
        }

        u32 PublishReusableHeap(const u32 heapIndex) noexcept
        {
            const u32 publishedIndex = CompactReusableHeapSlot(heapIndex);
            HeapEntry& heap = heaps[publishedIndex];
            VG_ASSERT(heap.state == PlacedCacheState::Reusable && heap.pendingPosition == InvalidPlacedResourceEntry && !heap.reusableIndexed);
            u32 blocker = InvalidPlacedResourceEntry;
            const HeapOwnershipProof ownership = InspectHeapOwnership(publishedIndex, &blocker);
            if (ownership == HeapOwnershipProof::ReusableOwned)
            {
                heap.heldOwnerWitness = InvalidPlacedResourceEntry;
                heap.heldOwnerScanCursor = InvalidPlacedResourceEntry;
                RefreshHeapDescriptorReusable(publishedIndex);
                AddReusableHeap(publishedIndex);
            }
            else
            {
                VG_ASSERT(ownership == HeapOwnershipProof::Blocked && "reusable placed heap contains a pending-retirement object");
                BeginHeldOwnershipProof(publishedIndex, blocker);
                AddPending(heldReusable, publishedIndex);
            }
            return publishedIndex;
        }
    };

    PlacedResourcePool::PlacedResourcePool(const RenderFlowResourceAllocatorConfig& config, AllocatorNativeByteLedger& ledger) noexcept
    {
        memory::MemoryBlock block = memory::Allocate(memory::PoolId::Rendering, sizeof(Impl), alignof(Impl));
        if (block)
            m_impl = new (block.address) Impl(config, ledger);
    }

    PlacedResourcePool::PlacedResourcePool(const RenderFlowResourceAllocatorConfig& config, AllocatorNativeByteLedger& ledger,
                                           AllocatorDescriptorLedger& descriptorLedger, const rhi::DescriptorDomainRef descriptorDomain) noexcept
    {
        memory::MemoryBlock block = memory::Allocate(memory::PoolId::Rendering, sizeof(Impl), alignof(Impl));
        if (block)
            m_impl = new (block.address) Impl(config, ledger, descriptorLedger, descriptorDomain);
    }

    PlacedResourcePool::~PlacedResourcePool()
    {
        if (m_impl == nullptr)
            return;
        for (Impl::ObjectEntry& object : m_impl->objects)
        {
            VG_ASSERT(CanonicalDescriptorReferenceCount(object.descriptors) == 0);
            object.texture.Reset();
            object.buffer.Reset();
        }
        for (Impl::HeapEntry& heap : m_impl->heaps)
        {
            rhi::ReleaseNativeReleaseObservation(heap.releaseObservation);
            heap.heap.Reset();
            if (heap.plan.capacity != 0)
                m_impl->ledger->Release(heap.plan.kind, heap.plan.capacity);
        }
        Impl* const impl = m_impl;
        m_impl = nullptr;
        impl->~Impl();
        memory::MemoryBlock block{impl, sizeof(Impl), memory::PoolId::Rendering};
        memory::Free(block);
    }

    void PlacedResourcePool::PollUnlocked() noexcept
    {
        if (m_impl == nullptr || !rhi::IsInitialized())
            return;
        for (u32 position = 0; position < m_impl->heldReusable.Size();)
        {
            const u32 heapIndex = m_impl->heldReusable[position];
            Impl::HeapEntry& heap = m_impl->heaps[heapIndex];
            VG_ASSERT(heap.state == PlacedCacheState::Reusable && !heap.reusableIndexed);
            if (!m_impl->ContinueHeldOwnershipProof(heapIndex))
            {
                ++position;
                continue;
            }
            m_impl->RemovePending(m_impl->heldReusable, heapIndex);
            heap.heldOwnerWitness = InvalidPlacedResourceEntry;
            heap.heldOwnerScanCursor = InvalidPlacedResourceEntry;
            static_cast<void>(m_impl->PublishReusableHeapAfterOwnershipProof(heapIndex));
        }
        for (u32 position = 0; position < m_impl->pendingRetirement.Size();)
        {
            const u32 heapIndex = m_impl->pendingRetirement[position];
            Impl::HeapEntry& heap = m_impl->heaps[heapIndex];
            VG_ASSERT(heap.state == PlacedCacheState::PendingRetirement);
            if (!FencesComplete(heap.safeAfter))
            {
                ++position;
                continue;
            }
            m_impl->RemovePending(m_impl->pendingRetirement, heapIndex);
            heap.safeAfter = {};
            if (heap.reusableAfterRetirement)
            {
                heap.state = PlacedCacheState::Reusable;
                for (u32 objectIndex = heap.objectHead; objectIndex != InvalidPlacedResourceEntry; objectIndex = m_impl->objects[objectIndex].nextInHeap)
                {
                    Impl::ObjectEntry& object = m_impl->objects[objectIndex];
                    if (object.state == PlacedCacheState::PendingRetirement)
                    {
                        object.state = PlacedCacheState::Reusable;
                        m_impl->AddReusableObject(objectIndex);
                    }
                }
                static_cast<void>(m_impl->PublishReusableHeap(heapIndex));
                continue;
            }
            heap.state = PlacedCacheState::ProviderFailureQuarantine;
            m_impl->AddPending(m_impl->pendingQuarantine, heapIndex);
        }

        for (u32 position = 0; position < m_impl->pendingQuarantine.Size();)
        {
            const u32 heapIndex = m_impl->pendingQuarantine[position];
            Impl::HeapEntry& heap = m_impl->heaps[heapIndex];
            VG_ASSERT(heap.state == PlacedCacheState::ProviderFailureQuarantine);
            if (!m_impl->RetireAllHeapObjectDescriptors(heapIndex, {}, nullptr))
            {
                ++position;
                continue;
            }
            rhi::Failure ignored;
            heap.releaseObservation = rhi::ObserveNativeRelease(rhi::ResourceRef(heap.heap.GetRef()), &ignored);
            if (!heap.releaseObservation.IsValid())
            {
                ++position;
                continue;
            }
            m_impl->RemovePending(m_impl->pendingQuarantine, heapIndex);
            m_impl->ReleaseAllHeapObjects(heapIndex);
            heap.heap.Reset();
            heap.state = PlacedCacheState::PendingNativeDestruction;
            m_impl->AddPending(m_impl->pendingDestruction, heapIndex);
        }

        for (u32 position = 0; position < m_impl->pendingDestruction.Size();)
        {
            const u32 heapIndex = m_impl->pendingDestruction[position];
            Impl::HeapEntry& heap = m_impl->heaps[heapIndex];
            VG_ASSERT(heap.state == PlacedCacheState::PendingNativeDestruction);
            if (!rhi::IsNativeReleaseComplete(heap.releaseObservation))
            {
                ++position;
                continue;
            }
            m_impl->RemovePending(m_impl->pendingDestruction, heapIndex);
            rhi::ReleaseNativeReleaseObservation(heap.releaseObservation);
            m_impl->ledger->Release(heap.plan.kind, heap.plan.capacity);
            m_impl->ReleaseHeapSlot(heapIndex);
        }
        m_impl->stats.pendingRetirementHeaps = m_impl->pendingRetirement.Size();
        m_impl->stats.pendingNativeDestructionHeaps = m_impl->pendingQuarantine.Size() + m_impl->pendingDestruction.Size();
    }

    bool PlacedResourcePool::Acquire(const PlacedRangePlan& plan, const containers::ArraySpan<const PlacedObjectRequest> objects, const u64 frameSerial, PlacedResourceBatch& batch,
                                     RenderFlowResourceFailure* const failure, CanonicalDescriptorPressure* const descriptorPressure,
                                     const bool recordPublicationStats) noexcept
    {
        batch.Clear();
        if (failure != nullptr)
            *failure = {};
        if (descriptorPressure != nullptr)
            *descriptorPressure = {};
        if (m_impl == nullptr)
            return PlacedFail(failure, RenderFlowResourceFailureCode::CapacityExceeded, "placed-resource pool metadata allocation failed");
        if (plan.heaps.Empty() || plan.assignments.Empty() || objects.Size() != plan.assignments.Size())
            return PlacedFail(failure, RenderFlowResourceFailureCode::BackendContractViolation, "placed-resource batch disagrees with its range plan");
        concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);

        m_impl->requestByAllocation.Reset();
        m_impl->requestByAllocation.Reserve(objects.Size());
        for (u32 objectIndex = 0; objectIndex < objects.Size(); ++objectIndex)
            if (objects[objectIndex].allocation == InvalidRenderFlowResourceIndex || !m_impl->requestByAllocation.Insert(objects[objectIndex].allocation, objectIndex))
                return PlacedFail(failure, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "placed-object request duplicates or omits a logical allocation");

        const u64 initialHeapHits = m_impl->stats.heapHits;
        const u64 initialHeapMisses = m_impl->stats.heapMisses;
        const u64 initialObjectHits = m_impl->stats.objectHits;
        const u64 initialObjectMisses = m_impl->stats.objectMisses;
        u64 transactionSerial = m_impl->nextTransactionSerial++;
        if (transactionSerial == 0)
            transactionSerial = m_impl->nextTransactionSerial++;
        const auto rollbackUnlocked = [this, &batch, transactionSerial, initialHeapHits, initialHeapMisses, initialObjectHits, initialObjectMisses, failure]() noexcept
        {
            bool clean = true;
            for (const u32 objectIndex : batch.objectEntries)
                if (objectIndex < m_impl->objects.Size() && m_impl->objects[objectIndex].state == PlacedCacheState::Assigned)
                {
                    if (m_impl->objects[objectIndex].transactionSerial == transactionSerial)
                    {
                        Impl::ObjectEntry& object = m_impl->objects[objectIndex];
                        if (!m_impl->RetireCanonicalDescriptors(object, {}, failure))
                        {
                            object.state = PlacedCacheState::Reusable;
                            m_impl->AddReusableObject(objectIndex);
                            clean = false;
                        }
                        else
                            m_impl->ReleaseObjectSlot(objectIndex);
                    }
                    else
                    {
                        m_impl->objects[objectIndex].state = PlacedCacheState::Reusable;
                        m_impl->AddReusableObject(objectIndex);
                    }
                }
            for (const u32 heapIndex : batch.heapEntries)
            {
                if (heapIndex >= m_impl->heaps.Size())
                    continue;
                Impl::HeapEntry& heap = m_impl->heaps[heapIndex];
                if (heap.state != PlacedCacheState::Assigned)
                    continue;
                if (heap.transactionSerial != transactionSerial)
                {
                    heap.state = PlacedCacheState::Reusable;
                    static_cast<void>(m_impl->PublishReusableHeap(heapIndex));
                    continue;
                }
                if (heap.objectCount != 0)
                {
                    heap.state = PlacedCacheState::Reusable;
                    static_cast<void>(m_impl->PublishReusableHeap(heapIndex));
                    clean = false;
                    continue;
                }
                VG_ASSERT(heap.objectCount == 0);
                rhi::Failure observationFailure;
                heap.releaseObservation = rhi::ObserveNativeRelease(rhi::ResourceRef(heap.heap.GetRef()), &observationFailure);
                if (heap.releaseObservation.IsValid())
                {
                    heap.heap.Reset();
                    heap.state = PlacedCacheState::PendingNativeDestruction;
                    m_impl->AddPending(m_impl->pendingDestruction, heapIndex);
                }
                else
                {
                    heap.state = PlacedCacheState::ProviderFailureQuarantine;
                    m_impl->AddPending(m_impl->pendingQuarantine, heapIndex);
                }
            }
            m_impl->stats.heapHits = initialHeapHits;
            m_impl->stats.heapMisses = initialHeapMisses;
            m_impl->stats.objectHits = initialObjectHits;
            m_impl->stats.objectMisses = initialObjectMisses;
            batch.Clear();
            return clean;
        };
        const auto fail = [&rollbackUnlocked, failure](const RenderFlowResourceFailureCode code, const char* const message) noexcept
        {
            if (!rollbackUnlocked())
                return false;
            return PlacedFail(failure, code, message);
        };

        batch.heapEntries.Reserve(plan.heaps.Size());
        batch.objectEntries.Reserve(plan.assignments.Size());
        batch.assignments.Reserve(plan.assignments.Size());
        bool pressurePolled = false;
        for (const PlacedHeapPlan& requested : plan.heaps)
        {
            const u64 requestedKey = HashHeapPlan(requested);
            const auto findReusableHeap = [&]() noexcept -> u32
            {
                u32 candidateIndex = InvalidPlacedResourceEntry;
                static_cast<void>(m_impl->reusableHeapHeads.Find(requestedKey, candidateIndex));
                for (u32 index = candidateIndex; index != InvalidPlacedResourceEntry;)
                {
                    const u32 next = m_impl->heaps[index].nextReusable;
                    const Impl::HeapEntry& candidate = m_impl->heaps[index];
                    if (candidate.state == PlacedCacheState::Reusable && candidate.heap.IsValid() && HeapPlanEqual(candidate.plan, requested))
                    {
                        u32 blocker = InvalidPlacedResourceEntry;
                        const Impl::HeapOwnershipProof ownership = m_impl->InspectHeapOwnership(index, &blocker);
                        if (ownership == Impl::HeapOwnershipProof::Blocked)
                        {
                            m_impl->RemoveReusableHeap(index);
                            m_impl->BeginHeldOwnershipProof(index, blocker);
                            m_impl->AddPending(m_impl->heldReusable, index);
                            index = next;
                            continue;
                        }
                        if (ownership == Impl::HeapOwnershipProof::ReusableOwned)
                            return index;
                        VG_ASSERT_MSG(ownership != Impl::HeapOwnershipProof::PendingOwned, "reusable placed heap contains a pending-retirement object");
                        m_impl->RemoveReusableHeap(index);
                        m_impl->BeginHeldOwnershipProof(index, InvalidPlacedResourceEntry);
                        m_impl->AddPending(m_impl->heldReusable, index);
                    }
                    index = next;
                }
                return InvalidPlacedResourceEntry;
            };

            u32 selected = findReusableHeap();
            if (selected == InvalidPlacedResourceEntry)
            {
                if (!m_impl->ledger->TryReserve(requested.kind, requested.capacity))
                {
                    if (pressurePolled)
                        return fail(RenderFlowResourceFailureCode::BudgetExceeded, "placed heap exceeds the allocator-wide hard native-byte budget");
                    pressurePolled = true;
                    PollUnlocked();
                    selected = findReusableHeap();
                    if (selected == InvalidPlacedResourceEntry && !m_impl->ledger->TryReserve(requested.kind, requested.capacity))
                        return fail(RenderFlowResourceFailureCode::BudgetExceeded, "placed heap exceeds the allocator-wide hard native-byte budget");
                }
            }
            if (selected == InvalidPlacedResourceEntry)
            {
                rhi::HeapDesc desc;
                desc.size = requested.capacity;
                desc.alignment = requested.alignment;
                desc.compatibilityClass = requested.compatibilityClass;
                desc.memoryType = requested.memoryType;
                desc.heapCategory = requested.heapCategory;
                rhi::Failure rhiFailure;
                rhi::Heap heap(rhi::AdoptReference, rhi::CreateHeap(desc, &rhiFailure));
                if (!heap.IsValid())
                {
                    m_impl->ledger->Release(requested.kind, requested.capacity);
                    return fail(MapRhiFailure(rhiFailure, RhiFailureContext::BackendContract), "placed heap creation failed");
                }
                rhi::HeapDesc authoritative;
                if (!rhi::GetHeapDesc(heap.GetRef(), authoritative, &rhiFailure) || authoritative.size != desc.size || authoritative.alignment != desc.alignment ||
                    authoritative.compatibilityClass != desc.compatibilityClass || authoritative.memoryType != desc.memoryType || authoritative.heapCategory != desc.heapCategory)
                {
                    Impl::HeapEntry quarantined;
                    quarantined.plan = requested;
                    quarantined.heap = static_cast<rhi::Heap&&>(heap);
                    rhi::Failure observationFailure;
                    quarantined.releaseObservation = rhi::ObserveNativeRelease(rhi::ResourceRef(quarantined.heap.GetRef()), &observationFailure);
                    if (quarantined.releaseObservation.IsValid())
                    {
                        quarantined.heap.Reset();
                        quarantined.state = PlacedCacheState::PendingNativeDestruction;
                    }
                    else
                        quarantined.state = PlacedCacheState::ProviderFailureQuarantine;
                    const u32 quarantinedIndex = m_impl->StoreHeap(static_cast<Impl::HeapEntry&&>(quarantined));
                    if (m_impl->heaps[quarantinedIndex].state == PlacedCacheState::PendingNativeDestruction)
                        m_impl->AddPending(m_impl->pendingDestruction, quarantinedIndex);
                    else
                        m_impl->AddPending(m_impl->pendingQuarantine, quarantinedIndex);
                    return fail(RenderFlowResourceFailureCode::BackendContractViolation, "placed heap differs from its canonical descriptor");
                }
                Impl::HeapEntry entry;
                entry.plan = requested;
                entry.heap = static_cast<rhi::Heap&&>(heap);
                entry.lastUsedFrame = frameSerial;
                entry.transactionSerial = transactionSerial;
                entry.state = PlacedCacheState::Assigned;
                selected = m_impl->StoreHeap(static_cast<Impl::HeapEntry&&>(entry));
                ++m_impl->stats.heapMisses;
            }
            else
            {
                ++m_impl->stats.heapHits;
                m_impl->RemoveReusableHeap(selected);
            }
            m_impl->heaps[selected].state = PlacedCacheState::Assigned;
            m_impl->heaps[selected].lastUsedFrame = frameSerial;
            batch.heapEntries.PushBack(selected);
        }

        for (const PlacedRangeAssignment& range : plan.assignments)
        {
            u32 requestedIndex = InvalidPlacedResourceEntry;
            if (!m_impl->requestByAllocation.Find(range.allocation, requestedIndex) || requestedIndex >= objects.Size() || range.heap >= batch.heapEntries.Size())
                return fail(RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "placed-object request is missing from its range plan");
            const PlacedObjectRequest* const requested = &objects[requestedIndex];
            const u32 heapEntry = batch.heapEntries[range.heap];
            Impl::HeapEntry& heap = m_impl->heaps[heapEntry];
            FrameResourceDesc desc = requested->desc;
            if (desc.kind == FrameResourceKind::Texture)
                desc.texture.active.virtualResource = true;
            else if (desc.kind == FrameResourceKind::Buffer)
                desc.buffer.active.virtualResource = true;
            else
                return fail(RenderFlowResourceFailureCode::BackendContractViolation, "placed-object request has an invalid resource kind");

            u32 selected = InvalidPlacedResourceEntry;
            const u64 objectKey = HashPlacedObject(heapEntry, range.offset, desc);
            u32 objectCandidate = InvalidPlacedResourceEntry;
            static_cast<void>(m_impl->reusableObjectHeads.Find(objectKey, objectCandidate));
            for (u32 index = objectCandidate; index != InvalidPlacedResourceEntry; index = m_impl->objects[index].nextReusable)
            {
                const Impl::ObjectEntry& candidate = m_impl->objects[index];
                if (candidate.state == PlacedCacheState::Reusable && candidate.heapEntry == heapEntry && candidate.placement.offset == range.offset && ObjectDescriptorEqual(candidate.desc, desc) &&
                    m_impl->PoolOwnsOnlyObjectReference(candidate))
                {
                    selected = index;
                    break;
                }
            }
            if (selected == InvalidPlacedResourceEntry)
            {
                rhi::Failure rhiFailure;
                Impl::ObjectEntry entry;
                entry.desc = desc;
                entry.lastUsedFrame = frameSerial;
                entry.transactionSerial = transactionSerial;
                entry.state = PlacedCacheState::Assigned;
                if (desc.kind == FrameResourceKind::Texture)
                    entry.texture = rhi::Texture(rhi::AdoptReference, rhi::CreateTexture(desc.texture.active, {}, &rhiFailure));
                else
                    entry.buffer = rhi::Buffer(rhi::AdoptReference, rhi::CreateBuffer(desc.buffer.active, {}, &rhiFailure));
                if (!entry.texture.IsValid() && !entry.buffer.IsValid())
                    return fail(MapRhiFailure(rhiFailure, RhiFailureContext::BackendContract), "placed object creation failed");
                if (entry.texture.IsValid())
                {
                    rhi::TextureDesc authoritative;
                    if (!rhi::GetTextureDesc(entry.texture.GetRef(), authoritative, &rhiFailure))
                        return fail(MapRhiFailure(rhiFailure, RhiFailureContext::BackendContract), "placed texture descriptor query failed");
                    if (!TextureDescriptorEqual(authoritative, desc.texture.active))
                        return fail(RenderFlowResourceFailureCode::BackendContractViolation, "placed texture differs from its canonical creation descriptor");
                }
                else
                {
                    rhi::BufferDesc authoritative;
                    if (!rhi::GetBufferDesc(entry.buffer.GetRef(), authoritative, &rhiFailure))
                        return fail(MapRhiFailure(rhiFailure, RhiFailureContext::BackendContract), "placed buffer descriptor query failed");
                    if (!BufferDescriptorEqual(authoritative, desc.buffer.active))
                        return fail(RenderFlowResourceFailureCode::BackendContractViolation, "placed buffer differs from its canonical creation descriptor");
                }
                const rhi::MemoryRequirements requirements = entry.texture.IsValid() ? rhi::GetMemoryRequirements(entry.texture.GetRef()) : rhi::GetMemoryRequirements(entry.buffer.GetRef());
                if (requirements.size != range.size || requirements.alignment != range.alignment || requirements.compatibilityClass != heap.plan.compatibilityClass ||
                    requirements.memoryType != heap.plan.memoryType || requirements.heapCategory != heap.plan.heapCategory)
                    return fail(RenderFlowResourceFailureCode::BackendContractViolation, "placed object requirements disagree with the range plan");
                const bool bound = entry.texture.IsValid() ? rhi::BindMemory(entry.texture.GetRef(), heap.heap.GetRef(), range.offset, &rhiFailure)
                                                           : rhi::BindMemory(entry.buffer.GetRef(), heap.heap.GetRef(), range.offset, &rhiFailure);
                if (!bound)
                    return fail(MapRhiFailure(rhiFailure, RhiFailureContext::BackendContract), "placed object binding failed");
                const bool queried = entry.texture.IsValid() ? rhi::GetPlacement(entry.texture.GetRef(), entry.placement, &rhiFailure) : rhi::GetPlacement(entry.buffer.GetRef(), entry.placement, &rhiFailure);
                if (!queried || entry.placement.heap != heap.heap.GetRef() || entry.placement.offset != range.offset || entry.placement.size != range.size || entry.placement.alignment != range.alignment ||
                    entry.placement.compatibilityClass != heap.plan.compatibilityClass)
                    return fail(RenderFlowResourceFailureCode::BackendContractViolation, "placed object published an incorrect immutable placement");
                selected = m_impl->StoreObject(static_cast<Impl::ObjectEntry&&>(entry), heapEntry);
                ++m_impl->stats.objectMisses;
            }
            else
            {
                ++m_impl->stats.objectHits;
                m_impl->RemoveReusableObject(selected);
            }

            Impl::ObjectEntry& object = m_impl->objects[selected];
            object.state = PlacedCacheState::Assigned;
            object.lastUsedFrame = frameSerial;
            batch.objectEntries.PushBack(selected);
            if (requested->requiresCanonicalDescriptors && !m_impl->EnsureCanonicalDescriptors(object, frameSerial, failure, descriptorPressure, recordPublicationStats))
            {
                const bool pressureRequired = descriptorPressure != nullptr && descriptorPressure->IsRequired();
                if (!rollbackUnlocked())
                {
                    if (descriptorPressure != nullptr)
                        *descriptorPressure = {};
                    return false;
                }
                if (pressureRequired)
                    return false;
                return false;
            }
            batch.assignments.PushBack({range.allocation, selected, object.texture.GetRef(), object.buffer.GetRef(), object.placement,
                                        requested->requiresCanonicalDescriptors ? object.descriptors.shaderResource : rhi::DescriptorHandle{},
                                        requested->requiresCanonicalDescriptors ? object.descriptors.unorderedAccess : rhi::DescriptorHandle{}});
        }
        return true;
    }

    void PlacedResourcePool::Rollback(PlacedResourceBatch& batch) noexcept
    {
        if (m_impl == nullptr)
        {
            batch.Clear();
            return;
        }
        concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
        for (const u32 objectIndex : batch.objectEntries)
            if (objectIndex < m_impl->objects.Size() && m_impl->objects[objectIndex].state == PlacedCacheState::Assigned)
            {
                m_impl->objects[objectIndex].state = PlacedCacheState::Reusable;
                m_impl->AddReusableObject(objectIndex);
            }
        for (const u32 heapIndex : batch.heapEntries)
            if (heapIndex < m_impl->heaps.Size() && m_impl->heaps[heapIndex].state == PlacedCacheState::Assigned)
            {
                m_impl->heaps[heapIndex].state = PlacedCacheState::Reusable;
                static_cast<void>(m_impl->PublishReusableHeap(heapIndex));
            }
        batch.Clear();
    }

    void PlacedResourcePool::Retire(PlacedResourceBatch& batch, const rhi::ResidencyFenceSet& safeAfter, const bool reusable) noexcept
    {
        if (m_impl == nullptr)
        {
            batch.Clear();
            return;
        }
        concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
        for (const u32 objectIndex : batch.objectEntries)
            if (objectIndex < m_impl->objects.Size() && m_impl->objects[objectIndex].state == PlacedCacheState::Assigned)
                m_impl->objects[objectIndex].state = PlacedCacheState::PendingRetirement;
        for (const u32 heapIndex : batch.heapEntries)
            if (heapIndex < m_impl->heaps.Size() && m_impl->heaps[heapIndex].state == PlacedCacheState::Assigned)
            {
                m_impl->heaps[heapIndex].safeAfter = safeAfter;
                m_impl->heaps[heapIndex].reusableAfterRetirement = reusable;
                m_impl->heaps[heapIndex].state = PlacedCacheState::PendingRetirement;
                m_impl->AddPending(m_impl->pendingRetirement, heapIndex);
        }
        batch.Clear();
    }

    void PlacedResourcePool::Poll() noexcept
    {
        if (m_impl == nullptr)
            return;
        concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
        PollUnlocked();
    }

    bool PlacedResourcePool::TrimToSoftTargets(ResourcePoolTrimState& trim, RenderFlowResourceFailure* const failure) noexcept
    {
        if (failure != nullptr)
            *failure = {};
        if (m_impl == nullptr || !rhi::IsInitialized())
            return true;
        concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);

        for (const u32 heapIndex : m_impl->pendingQuarantine)
        {
            const Impl::HeapEntry& heap = m_impl->heaps[heapIndex];
            u64& projected = heap.plan.kind == FrameResourceKind::Texture ? trim.projectedTextureBytes : trim.projectedBufferBytes;
            projected = heap.plan.capacity < projected ? projected - heap.plan.capacity : 0;
        }
        for (const u32 heapIndex : m_impl->pendingDestruction)
        {
            const Impl::HeapEntry& heap = m_impl->heaps[heapIndex];
            u64& projected = heap.plan.kind == FrameResourceKind::Texture ? trim.projectedTextureBytes : trim.projectedBufferBytes;
            projected = heap.plan.capacity < projected ? projected - heap.plan.capacity : 0;
        }
        if (trim.projectedTextureBytes <= m_impl->textureSoftTargetBytes &&
            trim.projectedBufferBytes <= m_impl->bufferSoftTargetBytes)
            return true;

        containers::DynamicArray<u32> skipped{memory::pools::Rendering::GetInstance()};
        while (trim.projectedTextureBytes > m_impl->textureSoftTargetBytes || trim.projectedBufferBytes > m_impl->bufferSoftTargetBytes)
        {
            const bool textureOver = trim.projectedTextureBytes > m_impl->textureSoftTargetBytes;
            const bool bufferOver = trim.projectedBufferBytes > m_impl->bufferSoftTargetBytes;
            const u32 textureCandidate = textureOver && !m_impl->reusableTextureTrimHeap.Empty() ? m_impl->reusableTextureTrimHeap[0] : InvalidPlacedResourceEntry;
            const u32 bufferCandidate = bufferOver && !m_impl->reusableBufferTrimHeap.Empty() ? m_impl->reusableBufferTrimHeap[0] : InvalidPlacedResourceEntry;
            u32 heapIndex = InvalidPlacedResourceEntry;
            if (textureCandidate == InvalidPlacedResourceEntry)
                heapIndex = bufferCandidate;
            else if (bufferCandidate == InvalidPlacedResourceEntry)
                heapIndex = textureCandidate;
            else
                heapIndex = m_impl->TrimLess(textureCandidate, bufferCandidate) ? textureCandidate : bufferCandidate;
            if (heapIndex == InvalidPlacedResourceEntry)
                break;
            Impl::HeapEntry& heap = m_impl->heaps[heapIndex];
            m_impl->RemoveTrimHeap(heapIndex);
            if (heap.state != PlacedCacheState::Reusable || !heap.heap.IsValid())
            {
                skipped.PushBack(heapIndex);
                continue;
            }
            u32 blocker = InvalidPlacedResourceEntry;
            const Impl::HeapOwnershipProof ownership = m_impl->InspectHeapOwnership(heapIndex, &blocker);
            if (ownership == Impl::HeapOwnershipProof::Blocked)
            {
                m_impl->RemoveReusableHeap(heapIndex);
                m_impl->BeginHeldOwnershipProof(heapIndex, blocker);
                m_impl->AddPending(m_impl->heldReusable, heapIndex);
                continue;
            }
            if (ownership != Impl::HeapOwnershipProof::ReusableOwned)
            {
                VG_ASSERT_MSG(ownership != Impl::HeapOwnershipProof::PendingOwned, "reusable placed heap contains a pending-retirement object");
                m_impl->RemoveReusableHeap(heapIndex);
                m_impl->BeginHeldOwnershipProof(heapIndex, InvalidPlacedResourceEntry);
                m_impl->AddPending(m_impl->heldReusable, heapIndex);
                continue;
            }

            if (!m_impl->RetireAllHeapObjectDescriptors(heapIndex, {}, failure, RenderFlowResourceSessionState::Idle))
            {
                m_impl->InsertTrimHeap(heapIndex);
                for (const u32 skippedIndex : skipped)
                    m_impl->InsertTrimHeap(skippedIndex);
                return false;
            }
            rhi::Failure rhiFailure;
            heap.releaseObservation = rhi::ObserveNativeRelease(rhi::ResourceRef(heap.heap.GetRef()), &rhiFailure);
            if (!heap.releaseObservation.IsValid())
            {
                m_impl->InsertTrimHeap(heapIndex);
                for (const u32 skippedIndex : skipped)
                    m_impl->InsertTrimHeap(skippedIndex);
                return PlacedFail(failure, MapRhiFailure(rhiFailure, RhiFailureContext::BackendContract), rhiFailure.message[0] != '\0' ? rhiFailure.message : "placed heap native-release observation failed",
                                  RenderFlowResourceSessionState::Idle);
            }
            m_impl->RemoveReusableHeap(heapIndex);
            m_impl->ReleaseAllHeapObjects(heapIndex);
            heap.heap.Reset();
            heap.safeAfter = {};
            heap.state = PlacedCacheState::PendingNativeDestruction;
            m_impl->AddPending(m_impl->pendingDestruction, heapIndex);
            if (heap.plan.kind == FrameResourceKind::Texture)
                trim.projectedTextureBytes -= heap.plan.capacity;
            else
                trim.projectedBufferBytes -= heap.plan.capacity;
        }
        for (const u32 skippedIndex : skipped)
            m_impl->InsertTrimHeap(skippedIndex);
        PollUnlocked();
        return true;
    }

    bool PlacedResourcePool::GetColdestCanonicalDescriptorCandidate(CanonicalDescriptorEvictionCandidate& candidate) noexcept
    {
        candidate = {};
        if (m_impl == nullptr || !m_impl->descriptorDomain.IsValid())
            return false;
        concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
        const u32 index = m_impl->ColdestReusableDescriptorObject();
        if (index == InvalidPlacedResourceEntry)
            return false;
        const Impl::ObjectEntry& object = m_impl->objects[index];
        const u32 descriptorCount = CanonicalDescriptorReferenceCount(object.descriptors);
        if (object.state != PlacedCacheState::Reusable || descriptorCount == 0 || object.descriptorReusablePosition == InvalidPlacedResourceEntry)
            VG_FATAL("placed descriptor cold index contains an ineligible object");
        candidate.ownerClass = CanonicalDescriptorOwnerClass::Placed;
        candidate.entry = index;
        candidate.lastDemandFrame = object.descriptors.lastDemandFrame;
        candidate.descriptorCount = descriptorCount;
        return true;
    }

    CanonicalDescriptorEvictionResult PlacedResourcePool::EvictCanonicalDescriptorCandidate(const CanonicalDescriptorEvictionCandidate& candidate, const bool pressure,
                                                                                              RenderFlowResourceFailure* const failure) noexcept
    {
        if (m_impl == nullptr || !m_impl->descriptorDomain.IsValid() || candidate.ownerClass != CanonicalDescriptorOwnerClass::Placed || candidate.entry >= m_impl->objects.Size())
            return CanonicalDescriptorEvictionResult::Skipped;
        concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
        Impl::ObjectEntry& object = m_impl->objects[candidate.entry];
        const u32 descriptorCount = CanonicalDescriptorReferenceCount(object.descriptors);
        if (object.state != PlacedCacheState::Reusable || descriptorCount == 0 || object.descriptorReusablePosition == InvalidPlacedResourceEntry ||
            object.descriptors.lastDemandFrame != candidate.lastDemandFrame)
            return CanonicalDescriptorEvictionResult::Skipped;
        if (!m_impl->PoolOwnsOnlyObjectReference(object))
        {
            m_impl->RemoveDescriptorReusable(object);
            if (object.heapEntry < m_impl->heaps.Size())
            {
                Impl::HeapEntry& heap = m_impl->heaps[object.heapEntry];
                if (heap.state == PlacedCacheState::Reusable && heap.reusableIndexed)
                {
                    m_impl->RemoveReusableHeap(object.heapEntry);
                    m_impl->BeginHeldOwnershipProof(object.heapEntry, candidate.entry);
                    m_impl->AddPending(m_impl->heldReusable, object.heapEntry);
                }
            }
            return CanonicalDescriptorEvictionResult::Skipped;
        }
        const RenderFlowResourceSessionState phase = pressure ? RenderFlowResourceSessionState::Resolving : RenderFlowResourceSessionState::Idle;
        if (!m_impl->RetireCanonicalDescriptors(object, {}, failure, phase))
            return CanonicalDescriptorEvictionResult::Failed;
        if (pressure)
            m_impl->descriptorLedger->RecordPressureEviction();
        else
            m_impl->descriptorLedger->RecordMaintenanceEviction();
        return CanonicalDescriptorEvictionResult::Evicted;
    }

    void PlacedResourcePool::RefreshCanonicalDescriptorCandidates() noexcept
    {
        if (m_impl == nullptr)
            return;
        concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
        PollUnlocked();
    }

    bool PlacedResourcePool::ClearPersistentCache(RenderFlowResourceFailure* const failure) noexcept
    {
        if (failure != nullptr)
            *failure = {};
        if (m_impl == nullptr || !rhi::IsInitialized())
            return true;
        concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
        PollUnlocked();
        if (!m_impl->heldReusable.Empty())
            return PlacedFail(failure, RenderFlowResourceFailureCode::BackendContractViolation, "persistent placed cache contains an object owner outside the allocator",
                              RenderFlowResourceSessionState::Idle);
        containers::DynamicArray<u32> candidates{memory::pools::Rendering::GetInstance()};
        containers::DynamicArray<rhi::NativeReleaseObservation> observations{memory::pools::Rendering::GetInstance()};
        candidates.Reserve(m_impl->reusableTextureTrimHeap.Size() + m_impl->reusableBufferTrimHeap.Size() + m_impl->pendingRetirement.Size());
        for (const u32 heapIndex : m_impl->reusableTextureTrimHeap)
        {
            const Impl::HeapEntry& heap = m_impl->heaps[heapIndex];
            if (heap.state != PlacedCacheState::Reusable || !m_impl->PoolOwnsAllHeapObjects(heapIndex))
                return PlacedFail(failure, RenderFlowResourceFailureCode::BackendContractViolation, "persistent placed cache contains an object owner outside the allocator",
                                  RenderFlowResourceSessionState::Idle);
            candidates.PushBack(heapIndex);
        }
        for (const u32 heapIndex : m_impl->reusableBufferTrimHeap)
        {
            const Impl::HeapEntry& heap = m_impl->heaps[heapIndex];
            if (heap.state != PlacedCacheState::Reusable || !m_impl->PoolOwnsAllHeapObjects(heapIndex))
                return PlacedFail(failure, RenderFlowResourceFailureCode::BackendContractViolation, "persistent placed cache contains an object owner outside the allocator",
                                  RenderFlowResourceSessionState::Idle);
            candidates.PushBack(heapIndex);
        }
        for (const u32 heapIndex : m_impl->pendingRetirement)
        {
            if (!m_impl->PoolOwnsAllHeapObjects(heapIndex))
                return PlacedFail(failure, RenderFlowResourceFailureCode::BackendContractViolation, "persistent placed cache contains an object owner outside the allocator",
                                  RenderFlowResourceSessionState::Idle);
            candidates.PushBack(heapIndex);
        }
        observations.Reserve(candidates.Size());
        for (const u32 heapIndex : candidates)
        {
            Impl::HeapEntry& heap = m_impl->heaps[heapIndex];
            const rhi::DescriptorRetirement retirement = heap.state == PlacedCacheState::PendingRetirement ? DescriptorRetirementFor(heap.safeAfter) : rhi::DescriptorRetirement{};
            if (!m_impl->RetireAllHeapObjectDescriptors(heapIndex, retirement, failure, RenderFlowResourceSessionState::Idle))
            {
                for (rhi::NativeReleaseObservation& prepared : observations)
                    rhi::ReleaseNativeReleaseObservation(prepared);
                return false;
            }
            rhi::Failure rhiFailure;
            rhi::NativeReleaseObservation observation = rhi::ObserveNativeRelease(rhi::ResourceRef(heap.heap.GetRef()), &rhiFailure);
            if (!observation.IsValid())
            {
                for (rhi::NativeReleaseObservation& prepared : observations)
                    rhi::ReleaseNativeReleaseObservation(prepared);
                return PlacedFail(failure, MapRhiFailure(rhiFailure, RhiFailureContext::BackendContract), rhiFailure.message[0] != '\0' ? rhiFailure.message : "placed heap native-release observation failed",
                                  RenderFlowResourceSessionState::Idle);
            }
            observations.PushBack(observation);
        }
        for (u32 position = 0; position < candidates.Size(); ++position)
        {
            const u32 heapIndex = candidates[position];
            Impl::HeapEntry& heap = m_impl->heaps[heapIndex];
            if (heap.state == PlacedCacheState::Reusable)
                m_impl->RemoveReusableHeap(heapIndex);
            else
                m_impl->RemovePending(m_impl->pendingRetirement, heapIndex);
            m_impl->ReleaseAllHeapObjects(heapIndex);
            heap.releaseObservation = observations[position];
            observations[position] = {};
            heap.heap.Reset();
            heap.safeAfter = {};
            heap.state = PlacedCacheState::PendingNativeDestruction;
            m_impl->AddPending(m_impl->pendingDestruction, heapIndex);
        }
        PollUnlocked();
        return true;
    }

    bool PlacedResourcePool::PrepareHealthyShutdown(bool& deviceLostEvidence, RenderFlowResourceFailure* const failure) noexcept
    {
        deviceLostEvidence = false;
        if (m_impl == nullptr)
            return true;
        if (!rhi::IsInitialized())
            return PlacedFail(failure, RenderFlowResourceFailureCode::NotInitialized,
                              "healthy placed-resource shutdown requires an initialized RHI", RenderFlowResourceSessionState::Idle);
        concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
        for (u32 heapIndex = 0; heapIndex < m_impl->heaps.Size(); ++heapIndex)
        {
            Impl::HeapEntry& heap = m_impl->heaps[heapIndex];
            if (heap.state == PlacedCacheState::Vacant || heap.state == PlacedCacheState::NativeReleased)
                continue;
            if (heap.state == PlacedCacheState::Assigned)
                return PlacedFail(failure, RenderFlowResourceFailureCode::InvalidPhase,
                                  "healthy placed-resource shutdown still owns an assigned heap", RenderFlowResourceSessionState::Idle);
            for (u32 objectIndex = heap.objectHead; objectIndex != InvalidPlacedResourceEntry; objectIndex = m_impl->objects[objectIndex].nextInHeap)
            {
                const Impl::ObjectEntry& object = m_impl->objects[objectIndex];
                if (object.state == PlacedCacheState::Assigned)
                    return PlacedFail(failure, RenderFlowResourceFailureCode::InvalidPhase,
                                      "healthy placed-resource shutdown still owns an assigned object", RenderFlowResourceSessionState::Idle);
                if (!m_impl->PoolOwnsOnlyObjectReference(object))
                    return PlacedFail(failure, RenderFlowResourceFailureCode::BackendContractViolation,
                                      "healthy placed-resource shutdown found an external object owner", RenderFlowResourceSessionState::Idle);
            }
            const rhi::DescriptorRetirement retirement = heap.state == PlacedCacheState::PendingRetirement ? DescriptorRetirementFor(heap.safeAfter) : rhi::DescriptorRetirement{};
            if (!m_impl->RetireAllHeapObjectDescriptors(heapIndex, retirement, failure, RenderFlowResourceSessionState::Idle, &deviceLostEvidence))
                return false;
        }
        return true;
    }

    bool PlacedResourcePool::ValidateHealthyShutdownCommit(RenderFlowResourceFailure* const failure) const noexcept
    {
        if (m_impl == nullptr)
            return true;
        if (!rhi::IsInitialized())
            return PlacedFail(failure, RenderFlowResourceFailureCode::NotInitialized,
                              "healthy placed-resource shutdown lost the RHI before commit", RenderFlowResourceSessionState::Idle);
        concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
        for (const Impl::ObjectEntry& object : m_impl->objects)
        {
            if (CanonicalDescriptorReferenceCount(object.descriptors) != 0)
                return PlacedFail(failure, RenderFlowResourceFailureCode::BackendContractViolation,
                                  "healthy placed-resource shutdown still owns canonical descriptors", RenderFlowResourceSessionState::Idle);
        }
        return true;
    }

    void PlacedResourcePool::CommitHealthyShutdown() noexcept
    {
        if (m_impl == nullptr)
            return;
        if (!rhi::IsInitialized())
        {
            VG_FATAL("healthy placed-resource shutdown lost the RHI after commit preflight");
            return;
        }
        concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
        for (Impl::ObjectEntry& object : m_impl->objects)
        {
            if (CanonicalDescriptorReferenceCount(object.descriptors) != 0)
            {
                VG_FATAL("healthy placed-resource shutdown commit invariant lost canonical descriptor drain");
                return;
            }
            object.texture.Reset();
            object.buffer.Reset();
            object = {};
        }
        for (Impl::HeapEntry& heap : m_impl->heaps)
        {
            if (heap.releaseObservation.IsValid())
                rhi::ReleaseNativeReleaseObservation(heap.releaseObservation);
            heap.heap.Reset();
            if (heap.plan.capacity != 0)
                m_impl->ledger->Release(heap.plan.kind, heap.plan.capacity);
            heap = {};
        }
        m_impl->objects.Clear();
        m_impl->heaps.Clear();
        m_impl->freeObjects.Clear();
        m_impl->freeHeaps.Clear();
        m_impl->reusableTextureTrimHeap.Clear();
        m_impl->reusableBufferTrimHeap.Clear();
        m_impl->descriptorReusableTrim.Clear();
        m_impl->heldReusable.Clear();
        m_impl->pendingRetirement.Clear();
        m_impl->pendingQuarantine.Clear();
        m_impl->pendingDestruction.Clear();
        m_impl->reusableHeapHeads.Reset();
        m_impl->reusableObjectHeads.Reset();
        m_impl->reusableObjectTails.Reset();
        m_impl->requestByAllocation.Reset();
        m_impl->stats = {};
    }

    void PlacedResourcePool::DeviceLost(const rhi::ResidencyFenceSet& submittedFences) noexcept
    {
        if (m_impl == nullptr)
            return;
        concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
        for (Impl::ObjectEntry& object : m_impl->objects)
        {
            const u32 descriptorCount = CanonicalDescriptorReferenceCount(object.descriptors);
            if (descriptorCount != 0)
            {
                rhi::ResidencyFenceSet safeAfter = submittedFences;
                if (object.heapEntry < m_impl->heaps.Size())
                {
                    const Impl::HeapEntry& heap = m_impl->heaps[object.heapEntry];
                    safeAfter.graphics = std::max(safeAfter.graphics, heap.safeAfter.graphics);
                    safeAfter.compute = std::max(safeAfter.compute, heap.safeAfter.compute);
                    safeAfter.copy = std::max(safeAfter.copy, heap.safeAfter.copy);
                }
                const rhi::DescriptorRetirement retirement = DescriptorRetirementFor(safeAfter);
                if (m_impl->descriptorDomain.IsValid() && rhi::IsInitialized())
                {
                    for (const rhi::DescriptorHandle descriptor : {object.descriptors.shaderResource, object.descriptors.unorderedAccess})
                    {
                        if (!descriptor.IsValid())
                            continue;
                        rhi::Failure ignored;
                        static_cast<void>(rhi::RetireDescriptor(m_impl->descriptorDomain, descriptor, retirement, &ignored));
                    }
                }
                if (object.descriptorReusablePosition != InvalidPlacedResourceEntry)
                    m_impl->descriptorLedger->MarkNotReusable(descriptorCount);
                m_impl->descriptorLedger->Release(descriptorCount);
                object.descriptors = {};
                object.descriptorReusablePosition = InvalidPlacedResourceEntry;
            }
            object.texture.Reset();
            object.buffer.Reset();
        }
        for (Impl::HeapEntry& heap : m_impl->heaps)
        {
            rhi::ReleaseNativeReleaseObservation(heap.releaseObservation);
            heap.heap.Reset();
            if (heap.plan.capacity != 0)
                m_impl->ledger->Release(heap.plan.kind, heap.plan.capacity);
        }
        m_impl->objects.Clear();
        m_impl->heaps.Clear();
        m_impl->freeObjects.Clear();
        m_impl->freeHeaps.Clear();
        m_impl->reusableTextureTrimHeap.Clear();
        m_impl->reusableBufferTrimHeap.Clear();
        m_impl->descriptorReusableTrim.Clear();
        m_impl->heldReusable.Clear();
        m_impl->pendingRetirement.Clear();
        m_impl->pendingQuarantine.Clear();
        m_impl->pendingDestruction.Clear();
        m_impl->reusableHeapHeads.Reset();
        m_impl->reusableObjectHeads.Reset();
        m_impl->reusableObjectTails.Reset();
        m_impl->requestByAllocation.Reset();
        m_impl->stats = {};
    }

    PlacedResourcePoolStats PlacedResourcePool::GetStats() const noexcept
    {
        if (m_impl == nullptr)
            return {};
        concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
        const_cast<PlacedResourcePool*>(this)->PollUnlocked();
        return m_impl->stats;
    }
} // namespace vanguard::rendering::detail
