#include <vanguard/vellum/instance_storage.hpp>

#include <vanguard/memory/memory.hpp>

#include <new>

namespace vanguard::vellum::detail
{
    namespace
    {
        constexpr u32 ExhaustedGeneration = 0xffffffffu;
        constexpr u64 ExhaustedSequence = ~u64{0};

        [[nodiscard]] bool Fail(Failure* const failure, const FailureCode code, const char* const message) noexcept
        {
            if (failure != nullptr)
            {
                failure->code = code;
                failure->message = message;
            }
            return false;
        }

        [[nodiscard]] bool RejectAdmission(Runtime::Impl& runtime, Failure* const failure, const FailureCode code,
                                           const char* const message) noexcept
        {
            ++runtime.stats.rejectedAdmissions;
            return Fail(failure, code, message);
        }

        void ClearFailure(Failure* const failure) noexcept
        {
            if (failure != nullptr)
                *failure = {};
        }

        [[nodiscard]] bool NextSequence(Runtime::Impl& runtime, u64& sequence) noexcept
        {
            if (runtime.nextMutationSequence == ExhaustedSequence)
                return false;
            sequence = runtime.nextMutationSequence++;
            return true;
        }

        [[nodiscard]] u64 NextReadySequence(const InstanceState& instance, const u64 fence) noexcept
        {
            const StructuralCommand* const ordinary = instance.ordinaryCommands.Front();
            const TerminalCommand* const terminal = instance.terminalCommands.Front();
            const bool ordinaryReady = ordinary != nullptr && ordinary->sequence <= fence;
            const bool terminalReady = terminal != nullptr && terminal->sequence <= fence;
            if (!ordinaryReady)
                return terminalReady ? terminal->sequence : ExhaustedSequence;
            if (!terminalReady)
                return ordinary->sequence;
            return ordinary->sequence < terminal->sequence ? ordinary->sequence : terminal->sequence;
        }

        void MergeSwap(CommitMergeEntry& left, CommitMergeEntry& right) noexcept
        {
            const CommitMergeEntry temporary = left;
            left = right;
            right = temporary;
        }

        [[nodiscard]] bool PushMergeEntry(Runtime::Impl& runtime, const CommitMergeEntry entry) noexcept
        {
            if (runtime.structuralMergeCount >= runtime.config.maximumInstances)
                return false;
            u32 index = runtime.structuralMergeCount++;
            runtime.structuralMergeScratch[index] = entry;
            while (index != 0)
            {
                const u32 parent = (index - 1u) / 2u;
                if (runtime.structuralMergeScratch[parent].sequence <= runtime.structuralMergeScratch[index].sequence)
                    break;
                MergeSwap(runtime.structuralMergeScratch[parent], runtime.structuralMergeScratch[index]);
                index = parent;
            }
            return true;
        }

        [[nodiscard]] CommitMergeEntry PopMergeEntry(Runtime::Impl& runtime) noexcept
        {
            const CommitMergeEntry result = runtime.structuralMergeScratch[0];
            --runtime.structuralMergeCount;
            if (runtime.structuralMergeCount == 0)
                return result;
            runtime.structuralMergeScratch[0] = runtime.structuralMergeScratch[runtime.structuralMergeCount];
            u32 index = 0;
            for (;;)
            {
                const u32 left = index * 2u + 1u;
                if (left >= runtime.structuralMergeCount)
                    break;
                const u32 right = left + 1u;
                u32 smallest = left;
                if (right < runtime.structuralMergeCount &&
                    runtime.structuralMergeScratch[right].sequence < runtime.structuralMergeScratch[left].sequence)
                    smallest = right;
                if (runtime.structuralMergeScratch[index].sequence <= runtime.structuralMergeScratch[smallest].sequence)
                    break;
                MergeSwap(runtime.structuralMergeScratch[index], runtime.structuralMergeScratch[smallest]);
                index = smallest;
            }
            return result;
        }

        [[nodiscard]] InstanceState* AllocateInstanceState(Runtime::Impl& runtime, const VellumInstanceHandle handle,
                                                           const u32 nodeCapacity) noexcept
        {
            memory::MemoryBlock objectBlock = memory::Allocate(memory::PoolId::Runtime, sizeof(InstanceState), alignof(InstanceState));
            if (!objectBlock)
                return nullptr;
            if (runtime.semanticUpdateActive)
                ++runtime.stats.semanticUpdateHeapAllocations;

            auto* const instance = ::new (objectBlock.address) InstanceState;
            instance->handle = handle;
            instance->nodeCapacity = nodeCapacity;

            usize nodeSlotsOffset = 0;
            usize rowsOffset = 0;
            usize scratchOffset = 0;
            usize terminalOffset = 0;
            usize detachedRootsOffset = 0;
            usize corePropertiesOffset = 0;
            usize measureCachesOffset = 0;
            usize geometriesOffset = 0;
            usize stackPanelsOffset = 0;
            usize spacersOffset = 0;
            usize layoutScratchOffset = 0;
            usize dirtyOffsets[DirtyDomainCount] = {};
            BlockLayout arenaLayout;
            bool arenaReady = arenaLayout.AddArray<NodeSlot>(nodeCapacity, nodeSlotsOffset) &&
                              arenaLayout.AddArray<NodeRow>(nodeCapacity, rowsOffset) &&
                              arenaLayout.AddArray<TraversalEntry>(nodeCapacity, scratchOffset) &&
                              arenaLayout.AddArray<TerminalCommand>(nodeCapacity, terminalOffset) &&
                              arenaLayout.AddArray<u32>(nodeCapacity, detachedRootsOffset) &&
                              arenaLayout.AddArray<CoreProperties>(nodeCapacity, corePropertiesOffset) &&
                              arenaLayout.AddArray<MeasureCache>(nodeCapacity, measureCachesOffset) &&
                              arenaLayout.AddArray<Geometry>(nodeCapacity, geometriesOffset) &&
                              arenaLayout.AddArray<StackPanelPayload>(nodeCapacity, stackPanelsOffset) &&
                              arenaLayout.AddArray<SpacerPayload>(nodeCapacity, spacersOffset) &&
                              arenaLayout.AddArray<LayoutFrame>(runtime.config.maximumHierarchyDepth, layoutScratchOffset);
            for (u32 domain = 0; arenaReady && domain < DirtyDomainCount; ++domain)
            {
                if (IsActiveLayoutDirtyDomain(domain))
                    arenaReady = arenaLayout.AddArray<WorkRef>(nodeCapacity, dirtyOffsets[domain]);
            }
            if (!arenaReady || !arenaLayout.Finish() || !AllocateRuntimeBlock(arenaLayout, instance->arenaBlock))
            {
                instance->~InstanceState();
                memory::Free(objectBlock);
                return nullptr;
            }
            if (runtime.semanticUpdateActive)
                ++runtime.stats.semanticUpdateHeapAllocations;

            usize commandsOffset = 0;
            BlockLayout commandLayout;
            if (!commandLayout.AddArray<StructuralCommand>(runtime.config.maximumStructuralCommandsPerInstance, commandsOffset) ||
                !commandLayout.Finish() || !AllocateRuntimeBlock(commandLayout, instance->commandBlock))
            {
                memory::Free(instance->arenaBlock);
                instance->~InstanceState();
                memory::Free(objectBlock);
                return nullptr;
            }
            if (runtime.semanticUpdateActive)
                ++runtime.stats.semanticUpdateHeapAllocations;

            instance->nodeSlots = BindArray<NodeSlot>(instance->arenaBlock, nodeSlotsOffset);
            instance->rows = BindArray<NodeRow>(instance->arenaBlock, rowsOffset);
            instance->traversalScratch = BindArray<TraversalEntry>(instance->arenaBlock, scratchOffset);
            instance->terminalStorage = BindArray<TerminalCommand>(instance->arenaBlock, terminalOffset);
            instance->detachedRoots = BindArray<u32>(instance->arenaBlock, detachedRootsOffset);
            instance->coreProperties = BindArray<CoreProperties>(instance->arenaBlock, corePropertiesOffset);
            instance->measureCaches = BindArray<MeasureCache>(instance->arenaBlock, measureCachesOffset);
            instance->geometries = BindArray<Geometry>(instance->arenaBlock, geometriesOffset);
            instance->stackPanels = BindArray<StackPanelPayload>(instance->arenaBlock, stackPanelsOffset);
            instance->spacers = BindArray<SpacerPayload>(instance->arenaBlock, spacersOffset);
            instance->layoutScratch = BindArray<LayoutFrame>(instance->arenaBlock, layoutScratchOffset);
            instance->ordinaryStorage = BindArray<StructuralCommand>(instance->commandBlock, commandsOffset);
            instance->terminalCommands = {instance->terminalStorage, nodeCapacity, 0, 0};
            instance->ordinaryCommands = {instance->ordinaryStorage, runtime.config.maximumStructuralCommandsPerInstance, 0, 0};
            for (u32 domain = 0; domain < DirtyDomainCount; ++domain)
            {
                if (IsActiveLayoutDirtyDomain(domain))
                    instance->dirtyFrontiers[domain] = {BindArray<WorkRef>(instance->arenaBlock, dirtyOffsets[domain]), nodeCapacity, 0, 0};
            }
            instance->firstFreeNode = nodeCapacity != 0 ? 0u : InvalidIndex;
            instance->freeNodeCount = nodeCapacity;

            for (u32 index = 0; index < nodeCapacity; ++index)
            {
                ::new (&instance->nodeSlots[index]) NodeSlot;
                ::new (&instance->rows[index]) NodeRow;
                ::new (&instance->traversalScratch[index]) TraversalEntry;
                ::new (&instance->terminalStorage[index]) TerminalCommand;
                ::new (&instance->coreProperties[index]) CoreProperties;
                ::new (&instance->measureCaches[index]) MeasureCache;
                ::new (&instance->geometries[index]) Geometry;
                ::new (&instance->stackPanels[index]) StackPanelPayload;
                ::new (&instance->spacers[index]) SpacerPayload;
                instance->detachedRoots[index] = InvalidIndex;
                for (u32 domain = 0; domain < DirtyDomainCount; ++domain)
                {
                    if (IsActiveLayoutDirtyDomain(domain))
                        ::new (&instance->dirtyFrontiers[domain].values[index]) WorkRef;
                }
                instance->nodeSlots[index].nextFree = index + 1u < nodeCapacity ? index + 1u : InvalidIndex;
            }
            for (u32 index = 0; index < runtime.config.maximumHierarchyDepth; ++index)
                ::new (&instance->layoutScratch[index]) LayoutFrame;
            for (u32 index = 0; index < runtime.config.maximumStructuralCommandsPerInstance; ++index)
                ::new (&instance->ordinaryStorage[index]) StructuralCommand;
            return instance;
        }

        void DeleteInstanceState(InstanceState* const instance) noexcept
        {
            if (instance == nullptr)
                return;
            memory::MemoryBlock objectBlock{instance, sizeof(InstanceState), memory::PoolId::Runtime};
            memory::Free(instance->commandBlock);
            memory::Free(instance->arenaBlock);
            instance->~InstanceState();
            memory::Free(objectBlock);
        }

        [[nodiscard]] NodeRow* ResolveLiveRow(InstanceState& instance, const u32 slotIndex) noexcept
        {
            if (slotIndex >= instance.nodeCapacity)
                return nullptr;
            NodeSlot& slot = instance.nodeSlots[slotIndex];
            return slot.state == SlotState::Live && slot.row < instance.denseCount ? &instance.rows[slot.row] : nullptr;
        }

        [[nodiscard]] const NodeRow* ResolveLiveRow(const InstanceState& instance, const u32 slotIndex) noexcept
        {
            if (slotIndex >= instance.nodeCapacity)
                return nullptr;
            const NodeSlot& slot = instance.nodeSlots[slotIndex];
            return slot.state == SlotState::Live && slot.row < instance.denseCount ? &instance.rows[slot.row] : nullptr;
        }

        [[nodiscard]] LocalNodeHandle MakeLocalHandle(const InstanceState& instance, const u32 slotIndex) noexcept
        {
            if (slotIndex == InvalidIndex || slotIndex >= instance.nodeCapacity)
                return {};
            return {slotIndex, instance.nodeSlots[slotIndex].generation};
        }

        [[nodiscard]] VellumNodeHandle MakeNodeHandle(const InstanceState& instance, const u32 slotIndex) noexcept
        {
            const LocalNodeHandle local = MakeLocalHandle(instance, slotIndex);
            return local.IsValid() ? VellumNodeHandle{instance.handle, local} : VellumNodeHandle{};
        }

        void UpdateDetachedPeaks(Runtime::Impl& runtime) noexcept
        {
            if (runtime.stats.liveDetachedRoots > runtime.stats.peakDetachedRoots)
                runtime.stats.peakDetachedRoots = runtime.stats.liveDetachedRoots;
            if (runtime.stats.liveDetachedNodes > runtime.stats.peakDetachedNodes)
                runtime.stats.peakDetachedNodes = runtime.stats.liveDetachedNodes;
        }

        [[nodiscard]] bool AddDetachedRoot(Runtime::Impl& runtime, InstanceState& instance, const u32 slotIndex) noexcept
        {
            if (slotIndex >= instance.nodeCapacity || instance.detachedRootCount >= instance.nodeCapacity)
                return false;
            NodeSlot& slot = instance.nodeSlots[slotIndex];
            if (slot.detachedRootIndex != InvalidIndex)
                return true;
            const u32 index = instance.detachedRootCount++;
            instance.detachedRoots[index] = slotIndex;
            slot.detachedRootIndex = index;
            ++runtime.stats.liveDetachedRoots;
            UpdateDetachedPeaks(runtime);
            return true;
        }

        void RemoveDetachedRoot(Runtime::Impl& runtime, InstanceState& instance, const u32 slotIndex) noexcept
        {
            if (slotIndex >= instance.nodeCapacity)
                return;
            NodeSlot& slot = instance.nodeSlots[slotIndex];
            const u32 index = slot.detachedRootIndex;
            if (index == InvalidIndex || index >= instance.detachedRootCount)
                return;
            const u32 lastIndex = instance.detachedRootCount - 1u;
            const u32 movedSlot = instance.detachedRoots[lastIndex];
            if (index != lastIndex)
            {
                instance.detachedRoots[index] = movedSlot;
                if (movedSlot < instance.nodeCapacity)
                    instance.nodeSlots[movedSlot].detachedRootIndex = index;
            }
            instance.detachedRoots[lastIndex] = InvalidIndex;
            --instance.detachedRootCount;
            slot.detachedRootIndex = InvalidIndex;
            if (runtime.stats.liveDetachedRoots != 0)
                --runtime.stats.liveDetachedRoots;
        }

        [[nodiscard]] bool CollectSubtree(InstanceState& instance, const u32 rootSlot) noexcept
        {
            if (ResolveLiveRow(instance, rootSlot) == nullptr)
                return false;
            instance.traversalCount = 0;
            instance.traversalScratch[instance.traversalCount++] = {rootSlot, 1u};
            for (u32 cursor = 0; cursor < instance.traversalCount; ++cursor)
            {
                const TraversalEntry entry = instance.traversalScratch[cursor];
                const NodeRow* const row = ResolveLiveRow(instance, entry.slot);
                if (row == nullptr)
                    return false;
                for (u32 child = row->firstChild; child != InvalidIndex;)
                {
                    const NodeRow* const childRow = ResolveLiveRow(instance, child);
                    if (childRow == nullptr || instance.traversalCount >= instance.nodeCapacity)
                        return false;
                    instance.traversalScratch[instance.traversalCount++] = {child, entry.depth + 1u};
                    child = childRow->nextSibling;
                }
            }
            return true;
        }

        void SetCollectedSubtreeDetached(Runtime::Impl& runtime, InstanceState& instance, const bool detached) noexcept
        {
            if (instance.traversalCount == 0)
                return;
            const u32 rootSlot = instance.traversalScratch[0].slot;
            if (rootSlot >= instance.nodeCapacity || instance.nodeSlots[rootSlot].detached == detached)
                return;
            for (u32 index = 0; index < instance.traversalCount; ++index)
            {
                const u32 slotIndex = instance.traversalScratch[index].slot;
                if (slotIndex < instance.nodeCapacity)
                    instance.nodeSlots[slotIndex].detached = detached;
            }
            if (detached)
            {
                instance.detachedNodeCount += instance.traversalCount;
                runtime.stats.liveDetachedNodes += instance.traversalCount;
                UpdateDetachedPeaks(runtime);
            }
            else
            {
                if (instance.detachedNodeCount >= instance.traversalCount)
                    instance.detachedNodeCount -= instance.traversalCount;
                if (runtime.stats.liveDetachedNodes >= instance.traversalCount)
                    runtime.stats.liveDetachedNodes -= instance.traversalCount;
            }
        }

        [[nodiscard]] bool AcquireNodeSlot(Runtime::Impl& runtime, InstanceState& instance, LocalNodeHandle& handle) noexcept
        {
            if (instance.firstFreeNode == InvalidIndex)
                return false;
            const u32 index = instance.firstFreeNode;
            NodeSlot& slot = instance.nodeSlots[index];
            if (slot.state != SlotState::Free || slot.generation == 0)
                return false;
            instance.firstFreeNode = slot.nextFree;
            slot.nextFree = InvalidIndex;
            slot.row = InvalidIndex;
            slot.detachedRootIndex = InvalidIndex;
            slot.destroySequence = 0;
            slot.destroyPending = false;
            slot.detached = false;
            slot.state = SlotState::ReservedCreate;
            ResetNodeDirtyState(instance, index);
            --instance.freeNodeCount;
            --runtime.stats.freeNodeSlots;
            handle = {index, slot.generation};
            return true;
        }

        void RetireNodeSlot(Runtime::Impl& runtime, InstanceState& instance, const u32 slotIndex) noexcept
        {
            NodeSlot& slot = instance.nodeSlots[slotIndex];
            if (slot.state == SlotState::DeferredFree || slot.state == SlotState::Exhausted || slot.state == SlotState::Free)
                return;
            slot.row = InvalidIndex;
            slot.detachedRootIndex = InvalidIndex;
            slot.detached = false;
            slot.state = SlotState::DeferredFree;
            slot.nextFree = instance.firstDeferredNode;
            instance.firstDeferredNode = slotIndex;
            ++instance.deferredNodeCount;
            ++runtime.stats.deferredNodeSlots;
        }

        void RemoveDenseRow(Runtime::Impl& runtime, InstanceState& instance, const u32 rowIndex) noexcept
        {
            if (rowIndex >= instance.denseCount)
                return;
            const u32 lastRow = instance.denseCount - 1u;
            RemoveDenseLayoutState(runtime, instance, rowIndex);
            if (rowIndex != lastRow)
            {
                MoveDenseLayoutState(instance, rowIndex, lastRow);
                instance.rows[rowIndex] = instance.rows[lastRow];
                const u32 movedSlot = instance.rows[rowIndex].slot;
                if (movedSlot < instance.nodeCapacity)
                    instance.nodeSlots[movedSlot].row = rowIndex;
                ++runtime.stats.denseRowMoves;
            }
            instance.rows[lastRow] = {};
            --instance.denseCount;
        }

        void Unlink(Runtime::Impl& runtime, InstanceState& instance, const u32 slotIndex) noexcept
        {
            NodeRow* const row = ResolveLiveRow(instance, slotIndex);
            if (row == nullptr)
                return;
            const u32 parentIndex = row->parent;
            const u32 previousIndex = row->previousSibling;
            const u32 nextIndex = row->nextSibling;
            if (parentIndex == InvalidIndex && slotIndex != instance.rootSlot)
                RemoveDetachedRoot(runtime, instance, slotIndex);
            if (previousIndex != InvalidIndex)
            {
                if (NodeRow* const previous = ResolveLiveRow(instance, previousIndex))
                    previous->nextSibling = nextIndex;
            }
            if (nextIndex != InvalidIndex)
            {
                if (NodeRow* const next = ResolveLiveRow(instance, nextIndex))
                    next->previousSibling = previousIndex;
            }
            if (NodeRow* const parent = ResolveLiveRow(instance, parentIndex))
            {
                if (parent->firstChild == slotIndex)
                    parent->firstChild = nextIndex;
                if (parent->lastChild == slotIndex)
                    parent->lastChild = previousIndex;
                if (parent->childCount != 0)
                    --parent->childCount;
            }
            row->parent = InvalidIndex;
            row->previousSibling = InvalidIndex;
            row->nextSibling = InvalidIndex;
        }

        void AppendChild(InstanceState& instance, const u32 parentIndex, const u32 slotIndex) noexcept
        {
            NodeRow* const parent = ResolveLiveRow(instance, parentIndex);
            NodeRow* const row = ResolveLiveRow(instance, slotIndex);
            if (parent == nullptr || row == nullptr)
                return;
            const u32 previous = parent->lastChild;
            row->parent = parentIndex;
            row->previousSibling = previous;
            row->nextSibling = InvalidIndex;
            if (previous != InvalidIndex)
            {
                if (NodeRow* const previousRow = ResolveLiveRow(instance, previous))
                    previousRow->nextSibling = slotIndex;
            }
            else
                parent->firstChild = slotIndex;
            parent->lastChild = slotIndex;
            ++parent->childCount;
        }

        void LinkBefore(InstanceState& instance, const u32 parentIndex, const u32 beforeIndex, const u32 slotIndex) noexcept
        {
            NodeRow* const parent = ResolveLiveRow(instance, parentIndex);
            NodeRow* const before = ResolveLiveRow(instance, beforeIndex);
            NodeRow* const row = ResolveLiveRow(instance, slotIndex);
            if (parent == nullptr || before == nullptr || row == nullptr)
                return;
            const u32 previous = before->previousSibling;
            row->parent = parentIndex;
            row->previousSibling = previous;
            row->nextSibling = beforeIndex;
            before->previousSibling = slotIndex;
            if (previous != InvalidIndex)
            {
                if (NodeRow* const previousRow = ResolveLiveRow(instance, previous))
                    previousRow->nextSibling = slotIndex;
            }
            else
                parent->firstChild = slotIndex;
            ++parent->childCount;
        }

        [[nodiscard]] bool ValidatePlacement(const Runtime::Impl& runtime, InstanceState& instance, const u32 nodeIndex,
                                             const LocalNodeHandle parentHandle, const LocalNodeHandle beforeHandle,
                                             u32& resultingDepth) noexcept
        {
            resultingDepth = 1;
            if (!parentHandle.IsValid())
                return !beforeHandle.IsValid();

            const NodeSlot* const parentSlot = ResolveNodeSlot(instance, parentHandle);
            if (parentSlot == nullptr)
                return false;
            const NodeRow* const parentRow = ResolveLiveRow(instance, parentHandle.slot);
            if (parentRow == nullptr || parentRow->kind == NodeKind::Spacer)
                return false;

            u32 parentDepth = 0;
            u32 ancestor = parentHandle.slot;
            while (ancestor != InvalidIndex)
            {
                if (ancestor == nodeIndex)
                    return false;
                const NodeRow* const row = ResolveLiveRow(instance, ancestor);
                if (row == nullptr || ++parentDepth >= runtime.config.maximumHierarchyDepth)
                    return false;
                ancestor = row->parent;
            }

            if (beforeHandle.IsValid())
            {
                if (beforeHandle.slot == nodeIndex || ResolveNodeSlot(instance, beforeHandle) == nullptr)
                    return false;
                const NodeRow* const before = ResolveLiveRow(instance, beforeHandle.slot);
                if (before == nullptr || before->parent != parentHandle.slot)
                    return false;
            }
            resultingDepth = parentDepth + 1u;
            return resultingDepth <= runtime.config.maximumHierarchyDepth;
        }

        [[nodiscard]] bool ValidateReparent(const Runtime::Impl& runtime, InstanceState& instance, const u32 nodeIndex,
                                            const LocalNodeHandle parentHandle, const LocalNodeHandle beforeHandle,
                                            u32& maximumResultingDepth) noexcept
        {
            u32 baseDepth = 0;
            if (!ValidatePlacement(runtime, instance, nodeIndex, parentHandle, beforeHandle, baseDepth))
                return false;
            const u32 parentDepth = parentHandle.IsValid() ? baseDepth - 1u : 0u;
            if (!CollectSubtree(instance, nodeIndex))
                return false;
            maximumResultingDepth = parentDepth + 1u;
            for (u32 cursor = 0; cursor < instance.traversalCount; ++cursor)
            {
                const TraversalEntry entry = instance.traversalScratch[cursor];
                const u32 absoluteDepth = parentDepth + entry.depth;
                if (absoluteDepth > runtime.config.maximumHierarchyDepth)
                    return false;
                if (absoluteDepth > maximumResultingDepth)
                    maximumResultingDepth = absoluteDepth;
            }
            return true;
        }

        void UpdateSiblingPeak(Runtime::Impl& runtime, const InstanceState& instance, const u32 parentIndex) noexcept
        {
            const NodeRow* const parent = ResolveLiveRow(instance, parentIndex);
            if (parent != nullptr && parent->childCount > runtime.stats.maximumSiblingCountObserved)
                runtime.stats.maximumSiblingCountObserved = parent->childCount;
        }

        void RejectCommand(Runtime::Impl& runtime, const bool stale) noexcept
        {
            ++runtime.stats.structuralCommandsRejected;
            if (stale)
                ++runtime.stats.staleWorkRejected;
        }

        void ApplyCreate(Runtime::Impl& runtime, InstanceState& instance, const StructuralCommand& command) noexcept
        {
            NodeSlot* const slot = ResolveNodeSlot(instance, command.node, true);
            if (slot == nullptr || slot->state != SlotState::ReservedCreate || instance.denseCount >= instance.nodeCapacity ||
                command.nodeKind == NodeKind::Invalid)
            {
                if (slot != nullptr && slot->state == SlotState::ReservedCreate)
                    RetireNodeSlot(runtime, instance, command.node.slot);
                RejectCommand(runtime, slot == nullptr);
                return;
            }

            if ((command.parent.IsValid() && ResolveNodeSlot(instance, command.parent) == nullptr) ||
                (command.beforeSibling.IsValid() && ResolveNodeSlot(instance, command.beforeSibling) == nullptr))
            {
                RetireNodeSlot(runtime, instance, command.node.slot);
                RejectCommand(runtime, true);
                return;
            }

            u32 depth = 1;
            if (!ValidatePlacement(runtime, instance, command.node.slot, command.parent, command.beforeSibling, depth))
            {
                RetireNodeSlot(runtime, instance, command.node.slot);
                RejectCommand(runtime, false);
                return;
            }

            const u32 rowIndex = instance.denseCount++;
            instance.rows[rowIndex] = {};
            NodeRow& row = instance.rows[rowIndex];
            row.kind = command.nodeKind;
            row.slot = command.node.slot;
            slot->row = rowIndex;
            if (!InitializeDenseLayoutState(runtime, instance, rowIndex, command.node.slot, command.nodeKind))
            {
                instance.rows[rowIndex] = {};
                --instance.denseCount;
                RetireNodeSlot(runtime, instance, command.node.slot);
                RejectCommand(runtime, false);
                return;
            }
            slot->state = SlotState::Live;
            slot->detached = !command.parent.IsValid() || instance.nodeSlots[command.parent.slot].detached;

            if (command.parent.IsValid())
            {
                if (command.beforeSibling.IsValid())
                    LinkBefore(instance, command.parent.slot, command.beforeSibling.slot, command.node.slot);
                else
                    AppendChild(instance, command.parent.slot, command.node.slot);
                UpdateSiblingPeak(runtime, instance, command.parent.slot);
            }
            else if (!AddDetachedRoot(runtime, instance, command.node.slot))
            {
                RemoveDenseRow(runtime, instance, rowIndex);
                RetireNodeSlot(runtime, instance, command.node.slot);
                RejectCommand(runtime, false);
                return;
            }

            if (slot->detached)
            {
                ++instance.detachedNodeCount;
                ++runtime.stats.liveDetachedNodes;
                UpdateDetachedPeaks(runtime);
            }

            ++runtime.stats.liveNodes;
            if (runtime.stats.liveNodes > runtime.stats.peakLiveNodes)
                runtime.stats.peakLiveNodes = runtime.stats.liveNodes;
            if (depth > runtime.stats.maximumHierarchyDepthObserved)
                runtime.stats.maximumHierarchyDepthObserved = depth;
            MarkStructuralCreate(runtime, instance, command.node.slot,
                                 command.parent.IsValid() ? command.parent.slot : InvalidIndex);
            ++runtime.stats.structuralCommandsApplied;
        }

        [[nodiscard]] bool DestroySubtreeIterative(Runtime::Impl& runtime, InstanceState& instance, const u32 rootSlot) noexcept
        {
            if (!CollectSubtree(instance, rootSlot))
                return false;
            const NodeRow* const rootRow = ResolveLiveRow(instance, rootSlot);
            if (rootRow == nullptr)
                return false;
            const u32 oldParent = rootRow->parent;
            const bool detached = instance.nodeSlots[rootSlot].detached;
            Unlink(runtime, instance, rootSlot);
            if (detached)
                SetCollectedSubtreeDetached(runtime, instance, false);
            for (u32 cursor = instance.traversalCount; cursor != 0; --cursor)
            {
                const u32 slotIndex = instance.traversalScratch[cursor - 1u].slot;
                NodeSlot& slot = instance.nodeSlots[slotIndex];
                if (slot.state != SlotState::Live || slot.row >= instance.denseCount)
                    continue;
                RemoveDenseRow(runtime, instance, slot.row);
                RetireNodeSlot(runtime, instance, slotIndex);
                if (runtime.stats.liveNodes != 0)
                    --runtime.stats.liveNodes;
            }
            MarkStructuralDestroy(runtime, instance, oldParent);
            return true;
        }

        void ApplyDestroy(Runtime::Impl& runtime, InstanceState& instance, const TerminalCommand& command) noexcept
        {
            if (command.slot >= instance.nodeCapacity)
            {
                RejectCommand(runtime, true);
                return;
            }
            NodeSlot& slot = instance.nodeSlots[command.slot];
            const bool exactTerminal = slot.generation == command.generation && slot.destroyPending && slot.destroySequence == command.sequence;
            if (slot.generation != command.generation)
            {
                RejectCommand(runtime, true);
                return;
            }

            if (slot.state == SlotState::Live)
            {
                if (command.slot == instance.rootSlot)
                    RejectCommand(runtime, false);
                else
                {
                    if (DestroySubtreeIterative(runtime, instance, command.slot))
                        ++runtime.stats.structuralCommandsApplied;
                    else
                        RejectCommand(runtime, false);
                }
            }
            else if (slot.state == SlotState::ReservedCreate)
            {
                RetireNodeSlot(runtime, instance, command.slot);
                ++runtime.stats.structuralCommandsApplied;
            }
            else
                RejectCommand(runtime, true);

            if (exactTerminal && slot.generation == command.generation)
            {
                slot.destroyPending = false;
                slot.destroySequence = 0;
            }
        }

        void ApplyReparent(Runtime::Impl& runtime, InstanceState& instance, const StructuralCommand& command) noexcept
        {
            NodeSlot* const slot = ResolveNodeSlot(instance, command.node);
            if (slot == nullptr || command.node.slot == instance.rootSlot)
            {
                RejectCommand(runtime, slot == nullptr);
                return;
            }

            if ((command.parent.IsValid() && ResolveNodeSlot(instance, command.parent) == nullptr) ||
                (command.beforeSibling.IsValid() && ResolveNodeSlot(instance, command.beforeSibling) == nullptr))
            {
                RejectCommand(runtime, true);
                return;
            }

            u32 depth = 0;
            if (!ValidateReparent(runtime, instance, command.node.slot, command.parent, command.beforeSibling, depth))
            {
                RejectCommand(runtime, false);
                return;
            }

            NodeRow* const current = ResolveLiveRow(instance, command.node.slot);
            if (current == nullptr)
            {
                RejectCommand(runtime, true);
                return;
            }
            if (current->parent == InvalidIndex && !command.parent.IsValid())
            {
                ++runtime.stats.structuralCommandsApplied;
                return;
            }

            const bool targetDetached = !command.parent.IsValid() || instance.nodeSlots[command.parent.slot].detached;
            const bool oldDetached = slot->detached;
            const bool oldLayoutGated = IsNodeLayoutGated(instance, command.node.slot);
            const u32 oldParent = current->parent;
            if (!command.parent.IsValid() && slot->detachedRootIndex == InvalidIndex &&
                !AddDetachedRoot(runtime, instance, command.node.slot))
            {
                RejectCommand(runtime, false);
                return;
            }

            Unlink(runtime, instance, command.node.slot);
            if (oldDetached != targetDetached)
                SetCollectedSubtreeDetached(runtime, instance, targetDetached);
            if (command.parent.IsValid())
            {
                if (command.beforeSibling.IsValid())
                    LinkBefore(instance, command.parent.slot, command.beforeSibling.slot, command.node.slot);
                else
                    AppendChild(instance, command.parent.slot, command.node.slot);
                UpdateSiblingPeak(runtime, instance, command.parent.slot);
            }
            if (depth > runtime.stats.maximumHierarchyDepthObserved)
                runtime.stats.maximumHierarchyDepthObserved = depth;
            MarkStructuralReparent(runtime, instance, command.node.slot, oldParent,
                                   command.parent.IsValid() ? command.parent.slot : InvalidIndex, oldDetached, targetDetached, oldLayoutGated);
            ++runtime.stats.structuralCommandsApplied;
        }
    } // namespace

    bool InitializeInstanceRegistry(Runtime::Impl& runtime, Failure* const failure) noexcept
    {
        ClearFailure(failure);
        usize slotsOffset = 0;
        usize mergeOffset = 0;
        BlockLayout layout;
        if (!layout.AddArray<InstanceSlot>(runtime.config.maximumInstances, slotsOffset) ||
            !layout.AddArray<CommitMergeEntry>(runtime.config.maximumInstances, mergeOffset) || !layout.Finish() ||
            !AllocateRuntimeBlock(layout, runtime.instanceBlock))
            return Fail(failure, FailureCode::OutOfMemory, "Vellum instance registry allocation failed");
        runtime.instanceSlots = BindArray<InstanceSlot>(runtime.instanceBlock, slotsOffset);
        runtime.structuralMergeScratch = BindArray<CommitMergeEntry>(runtime.instanceBlock, mergeOffset);
        runtime.firstFreeInstance = 0;
        for (u32 index = 0; index < runtime.config.maximumInstances; ++index)
        {
            ::new (&runtime.instanceSlots[index]) InstanceSlot;
            ::new (&runtime.structuralMergeScratch[index]) CommitMergeEntry;
            runtime.instanceSlots[index].nextFree = index + 1u < runtime.config.maximumInstances ? index + 1u : InvalidIndex;
        }
        runtime.stats.freeInstanceSlots = runtime.config.maximumInstances;
        return true;
    }

    void DestroyInstanceRegistry(Runtime::Impl& runtime) noexcept
    {
        if (runtime.instanceSlots != nullptr)
        {
            for (u32 index = 0; index < runtime.config.maximumInstances; ++index)
            {
                if (runtime.instanceSlots[index].instance != nullptr)
                    DeleteInstanceState(runtime.instanceSlots[index].instance);
                runtime.instanceSlots[index].instance = nullptr;
                runtime.instanceSlots[index].~InstanceSlot();
            }
        }
        runtime.instanceSlots = nullptr;
        runtime.structuralMergeScratch = nullptr;
        runtime.firstFreeInstance = InvalidIndex;
        runtime.firstDeferredInstance = InvalidIndex;
        runtime.structuralMergeCount = 0;
        memory::Free(runtime.instanceBlock);
    }

    void PromoteDeferredInstances(Runtime::Impl& runtime) noexcept
    {
        u32 current = runtime.firstDeferredInstance;
        runtime.firstDeferredInstance = InvalidIndex;
        while (current != InvalidIndex)
        {
            InstanceSlot& slot = runtime.instanceSlots[current];
            const u32 next = slot.nextFree;
            slot.nextFree = InvalidIndex;
            if (runtime.stats.deferredInstanceSlots != 0)
                --runtime.stats.deferredInstanceSlots;
            if (slot.generation == ExhaustedGeneration)
            {
                slot.state = SlotState::Exhausted;
                ++runtime.stats.exhaustedInstanceSlots;
            }
            else
            {
                ++slot.generation;
                slot.state = SlotState::Free;
                slot.nextFree = runtime.firstFreeInstance;
                runtime.firstFreeInstance = current;
                ++runtime.stats.freeInstanceSlots;
            }
            current = next;
        }
    }

    InstanceState* ResolveInstance(Runtime::Impl& runtime, const VellumInstanceHandle handle, const bool allowReserved) noexcept
    {
        if (!handle.IsValid() || handle.index >= runtime.config.maximumInstances || runtime.instanceSlots == nullptr)
            return nullptr;
        InstanceSlot& slot = runtime.instanceSlots[handle.index];
        if (slot.generation != handle.generation)
            return nullptr;
        if (slot.state != SlotState::Live && !(allowReserved && slot.state == SlotState::ReservedCreate))
            return nullptr;
        return slot.instance;
    }

    const InstanceState* ResolveInstance(const Runtime::Impl& runtime, const VellumInstanceHandle handle, const bool allowReserved) noexcept
    {
        return ResolveInstance(const_cast<Runtime::Impl&>(runtime), handle, allowReserved);
    }

    NodeSlot* ResolveNodeSlot(InstanceState& instance, const LocalNodeHandle handle, const bool allowReserved) noexcept
    {
        if (!handle.IsValid() || handle.slot >= instance.nodeCapacity)
            return nullptr;
        NodeSlot& slot = instance.nodeSlots[handle.slot];
        if (slot.generation != handle.generation)
            return nullptr;
        if (slot.state != SlotState::Live && !(allowReserved && slot.state == SlotState::ReservedCreate))
            return nullptr;
        return &slot;
    }

    const NodeSlot* ResolveNodeSlot(const InstanceState& instance, const LocalNodeHandle handle, const bool allowReserved) noexcept
    {
        return ResolveNodeSlot(const_cast<InstanceState&>(instance), handle, allowReserved);
    }

    bool AllocateInstance(Runtime::Impl& runtime, const u32 nodeCapacity, VellumInstanceHandle& handle, InstanceState*& instance,
                          Failure* const failure) noexcept
    {
        ClearFailure(failure);
        handle = {};
        instance = nullptr;
        if (nodeCapacity == 0 || nodeCapacity > runtime.config.maximumNodesPerDocument)
            return RejectAdmission(runtime, failure, FailureCode::InvalidArgument, "Vellum instance node capacity is invalid");
        if (runtime.stats.reservedNodeCapacity > runtime.config.maximumTotalNodes - nodeCapacity)
            return RejectAdmission(runtime, failure, FailureCode::CapacityExceeded, "Vellum aggregate node capacity is exhausted");
        if (runtime.firstFreeInstance == InvalidIndex)
            return RejectAdmission(runtime, failure, FailureCode::CapacityExceeded, "Vellum instance capacity is exhausted");

        const u32 slotIndex = runtime.firstFreeInstance;
        InstanceSlot& slot = runtime.instanceSlots[slotIndex];
        runtime.firstFreeInstance = slot.nextFree;
        if (runtime.stats.freeInstanceSlots != 0)
            --runtime.stats.freeInstanceSlots;
        slot.nextFree = InvalidIndex;
        slot.state = SlotState::ReservedCreate;
        handle = {slotIndex, slot.generation};
        instance = AllocateInstanceState(runtime, handle, nodeCapacity);
        if (instance == nullptr)
        {
            slot.state = SlotState::Free;
            slot.nextFree = runtime.firstFreeInstance;
            runtime.firstFreeInstance = slotIndex;
            ++runtime.stats.freeInstanceSlots;
            handle = {};
            return RejectAdmission(runtime, failure, FailureCode::OutOfMemory, "Vellum instance storage allocation failed");
        }
        slot.instance = instance;
        runtime.stats.reservedNodeCapacity += nodeCapacity;
        runtime.stats.freeNodeSlots += nodeCapacity;
        return true;
    }

    bool MaterializeDocumentNode(Runtime::Impl& runtime, InstanceState& instance, const DocumentResource& document,
                                 const u32 documentNodeIndex, Failure* const failure) noexcept
    {
        ClearFailure(failure);
        if (documentNodeIndex >= document.GetNodeCount() || documentNodeIndex >= instance.nodeCapacity ||
            documentNodeIndex != instance.denseCount)
            return Fail(failure, FailureCode::InternalError, "Vellum staged document node order is invalid");

        const DocumentNode& authored = document.GetNodes()[documentNodeIndex];
        if ((documentNodeIndex == document.GetRootIndex() && authored.parentIndex != InvalidDocumentNodeIndex) ||
            (documentNodeIndex != document.GetRootIndex() && authored.parentIndex >= documentNodeIndex))
            return Fail(failure, FailureCode::DecodeFailure, "Vellum staged document topology is invalid");
        if (authored.parentIndex != InvalidDocumentNodeIndex)
        {
            const NodeRow* const parent = ResolveLiveRow(instance, authored.parentIndex);
            if (parent == nullptr || parent->kind == NodeKind::Spacer)
                return Fail(failure, FailureCode::DecodeFailure, "Vellum staged document parent is invalid");
        }

        LocalNodeHandle local;
        if (!AcquireNodeSlot(runtime, instance, local))
            return Fail(failure, FailureCode::InternalError, "Vellum staged instance unexpectedly exhausted node slots");
        if (local.slot != documentNodeIndex)
        {
            RetireNodeSlot(runtime, instance, local.slot);
            return Fail(failure, FailureCode::InternalError, "Vellum staged document slot mapping lost canonical order");
        }

        NodeSlot& slot = instance.nodeSlots[local.slot];
        const u32 rowIndex = instance.denseCount++;
        instance.rows[rowIndex] = {};
        NodeRow& row = instance.rows[rowIndex];
        row.kind = authored.kind;
        row.slot = local.slot;
        slot.row = rowIndex;
        if (!InitializeDenseLayoutState(runtime, instance, rowIndex, local.slot, authored.kind))
        {
            instance.rows[rowIndex] = {};
            --instance.denseCount;
            RetireNodeSlot(runtime, instance, local.slot);
            return Fail(failure, FailureCode::InternalError, "Vellum staged document node kind storage is invalid");
        }
        slot.state = SlotState::Live;
        // The entire instance slot is still ReservedCreate and therefore off-tree.
        // Staged document nodes do not participate in the runtime detached domain;
        // this keeps publication O(1) instead of requiring a subtree detach->attach walk.
        slot.detached = false;

        if (authored.parentIndex != InvalidDocumentNodeIndex)
        {
            AppendChild(instance, authored.parentIndex, local.slot);
            UpdateSiblingPeak(runtime, instance, authored.parentIndex);
        }

        ++runtime.stats.liveNodes;
        if (runtime.stats.liveNodes > runtime.stats.peakLiveNodes)
            runtime.stats.peakLiveNodes = runtime.stats.liveNodes;
        if (authored.depth > runtime.stats.maximumHierarchyDepthObserved)
            runtime.stats.maximumHierarchyDepthObserved = authored.depth;

        for (u32 propertyIndex = 0; propertyIndex < authored.propertyCount; ++propertyIndex)
        {
            const DocumentProperty& property = document.GetProperties()[authored.firstProperty + propertyIndex];
            if (!ApplyInitialProperty(instance, rowIndex, property.id, property.type, property.canonicalBits))
                return Fail(failure, FailureCode::DecodeFailure, "Vellum staged document property is invalid");
        }
        return true;
    }

    bool PublishInstance(Runtime::Impl& runtime, const VellumInstanceHandle handle, const u32 rootSlot, Failure* const failure) noexcept
    {
        ClearFailure(failure);
        InstanceState* const instance = ResolveInstance(runtime, handle, true);
        if (instance == nullptr || handle.index >= runtime.config.maximumInstances)
            return Fail(failure, FailureCode::StaleHandle, "Vellum instance publication references a stale reservation");
        InstanceSlot& slot = runtime.instanceSlots[handle.index];
        if (slot.state != SlotState::ReservedCreate || rootSlot >= instance->nodeCapacity || instance->nodeSlots[rootSlot].state != SlotState::Live)
            return Fail(failure, FailureCode::InvalidState, "Vellum instance publication requires one live root");
        const NodeRow* const root = ResolveLiveRow(*instance, rootSlot);
        if (root == nullptr || root->parent != InvalidIndex)
            return Fail(failure, FailureCode::StructuralConflict, "Vellum instance root must be detached from any parent");
        if (instance->detachedRootCount != 0 || instance->detachedNodeCount != 0 || instance->nodeSlots[rootSlot].detached)
            return Fail(failure, FailureCode::InternalError, "Vellum staged instance leaked detached-domain ownership");
        instance->rootSlot = rootSlot;
        slot.state = SlotState::Live;
        MarkStructuralReparent(runtime, *instance, rootSlot, InvalidIndex, InvalidIndex, true, false, false);
        ++runtime.stats.liveInstances;
        if (runtime.stats.liveInstances > runtime.stats.peakLiveInstances)
            runtime.stats.peakLiveInstances = runtime.stats.liveInstances;
        return true;
    }

    bool RetireInstance(Runtime::Impl& runtime, const VellumInstanceHandle handle, Failure* const failure) noexcept
    {
        ClearFailure(failure);
        if (runtime.structuralCommitActive)
            return Fail(failure, FailureCode::InvalidState, "Vellum cannot retire an instance during structural commit");
        if (!handle.IsValid() || handle.index >= runtime.config.maximumInstances)
            return Fail(failure, FailureCode::StaleHandle, "Vellum instance retirement references a stale instance");
        InstanceSlot& slot = runtime.instanceSlots[handle.index];
        if (slot.generation != handle.generation || (slot.state != SlotState::Live && slot.state != SlotState::ReservedCreate))
            return Fail(failure, FailureCode::StaleHandle, "Vellum instance retirement references a stale instance");
        InstanceState* const instance = slot.instance;
        if (instance == nullptr)
            return Fail(failure, FailureCode::InternalError, "Vellum live instance slot has no storage");
        if (runtime.stats.pendingStructuralCommands < instance->ordinaryCommands.count ||
            runtime.stats.terminalStructuralCommands < instance->terminalCommands.count || runtime.stats.liveNodes < instance->denseCount ||
            runtime.stats.liveDetachedRoots < instance->detachedRootCount || runtime.stats.liveDetachedNodes < instance->detachedNodeCount ||
            runtime.stats.freeNodeSlots < instance->freeNodeCount || runtime.stats.deferredNodeSlots < instance->deferredNodeCount ||
            runtime.stats.exhaustedNodeSlots < instance->exhaustedNodeCount || runtime.stats.reservedNodeCapacity < instance->nodeCapacity)
            return Fail(failure, FailureCode::InternalError, "Vellum instance retirement accounting invariant was violated");
        if (slot.state == SlotState::Live && runtime.stats.liveInstances != 0)
            --runtime.stats.liveInstances;
        runtime.stats.pendingStructuralCommands -= instance->ordinaryCommands.count;
        runtime.stats.terminalStructuralCommands -= instance->terminalCommands.count;
        runtime.stats.liveNodes -= instance->denseCount;
        runtime.stats.liveDetachedRoots -= instance->detachedRootCount;
        runtime.stats.liveDetachedNodes -= instance->detachedNodeCount;
        runtime.stats.freeNodeSlots -= instance->freeNodeCount;
        runtime.stats.deferredNodeSlots -= instance->deferredNodeCount;
        runtime.stats.exhaustedNodeSlots -= instance->exhaustedNodeCount;
        runtime.stats.reservedNodeCapacity -= instance->nodeCapacity;
        ReleaseDirtyWork(runtime, *instance);
        DeleteInstanceState(instance);
        slot.instance = nullptr;
        slot.state = SlotState::DeferredFree;
        slot.nextFree = runtime.firstDeferredInstance;
        runtime.firstDeferredInstance = handle.index;
        ++runtime.stats.deferredInstanceSlots;
        return true;
    }

    void PromoteDeferredNodes(Runtime::Impl& runtime, InstanceState& instance) noexcept
    {
        u32 current = instance.firstDeferredNode;
        u32 retained = InvalidIndex;
        instance.firstDeferredNode = InvalidIndex;
        while (current != InvalidIndex)
        {
            NodeSlot& slot = instance.nodeSlots[current];
            const u32 next = slot.nextFree;
            slot.nextFree = InvalidIndex;
            if (slot.destroyPending)
            {
                slot.nextFree = retained;
                retained = current;
                current = next;
                continue;
            }
            if (instance.deferredNodeCount != 0)
                --instance.deferredNodeCount;
            if (runtime.stats.deferredNodeSlots != 0)
                --runtime.stats.deferredNodeSlots;
            if (slot.generation == ExhaustedGeneration)
            {
                slot.state = SlotState::Exhausted;
                ++instance.exhaustedNodeCount;
                ++runtime.stats.exhaustedNodeSlots;
            }
            else
            {
                ++slot.generation;
                slot.state = SlotState::Free;
                slot.row = InvalidIndex;
                slot.nextFree = instance.firstFreeNode;
                instance.firstFreeNode = current;
                ++instance.freeNodeCount;
                ++runtime.stats.freeNodeSlots;
            }
            current = next;
        }
        instance.firstDeferredNode = retained;
    }

    bool QueueCreate(Runtime::Impl& runtime, InstanceState& instance, const NodeCreateDesc& desc, VellumNodeHandle& node,
                     Failure* const failure) noexcept
    {
        ClearFailure(failure);
        node = {};
        if (runtime.stats.state != RuntimeState::Active || !runtime.stats.acceptingWork)
            return Fail(failure, FailureCode::InvalidState, "Vellum runtime is not accepting create work");
        if (desc.kind != NodeKind::StackPanel && desc.kind != NodeKind::Spacer)
            return Fail(failure, FailureCode::InvalidArgument, "Vellum create node kind is invalid");
        if (instance.ordinaryCommands.Full())
            return RejectAdmission(runtime, failure, FailureCode::CapacityExceeded, "Vellum structural command lane is full");
        if ((desc.parent.IsValid() && desc.parent.instance != instance.handle) ||
            (desc.beforeSibling.IsValid() && desc.beforeSibling.instance != instance.handle))
            return Fail(failure, FailureCode::CrossInstance, "Vellum create topology references another instance");
        if (desc.parent.IsValid() && ResolveNodeSlot(instance, desc.parent.local, true) == nullptr)
            return Fail(failure, FailureCode::StaleHandle, "Vellum create parent is stale");
        if (desc.beforeSibling.IsValid() && ResolveNodeSlot(instance, desc.beforeSibling.local, true) == nullptr)
            return Fail(failure, FailureCode::StaleHandle, "Vellum create sibling is stale");

        u64 sequence = 0;
        if (!NextSequence(runtime, sequence))
            return RejectAdmission(runtime, failure, FailureCode::CapacityExceeded, "Vellum mutation sequence space is exhausted");
        LocalNodeHandle local;
        if (!AcquireNodeSlot(runtime, instance, local))
            return RejectAdmission(runtime, failure, FailureCode::CapacityExceeded, "Vellum instance node capacity is exhausted");

        const StructuralCommand command{sequence, StructuralCommandKind::Create, local,
                                        desc.parent.IsValid() ? desc.parent.local : LocalNodeHandle{},
                                        desc.beforeSibling.IsValid() ? desc.beforeSibling.local : LocalNodeHandle{}, desc.kind};
        if (!instance.ordinaryCommands.Push(command))
        {
            RetireNodeSlot(runtime, instance, local.slot);
            return RejectAdmission(runtime, failure, FailureCode::CapacityExceeded, "Vellum structural command lane is full");
        }
        ++runtime.stats.pendingStructuralCommands;
        if (runtime.stats.pendingStructuralCommands > runtime.stats.peakPendingStructuralCommands)
            runtime.stats.peakPendingStructuralCommands = runtime.stats.pendingStructuralCommands;
        node = {instance.handle, local};
        return true;
    }

    bool QueueDestroy(Runtime::Impl& runtime, InstanceState& instance, const LocalNodeHandle node, Failure* const failure) noexcept
    {
        ClearFailure(failure);
        if (runtime.stats.state != RuntimeState::Active && runtime.stats.state != RuntimeState::Quiescing)
            return Fail(failure, FailureCode::InvalidState, "Vellum runtime cannot accept destroy work in its current state");
        NodeSlot* const slot = ResolveNodeSlot(instance, node, true);
        if (slot == nullptr)
            return Fail(failure, FailureCode::StaleHandle, "Vellum destroy target is stale");
        if (node.slot == instance.rootSlot)
            return Fail(failure, FailureCode::StructuralConflict, "Vellum root destruction is owned by instance release");
        if (slot->destroyPending)
            return true;
        u64 sequence = 0;
        if (!NextSequence(runtime, sequence))
            return RejectAdmission(runtime, failure, FailureCode::CapacityExceeded, "Vellum mutation sequence space is exhausted");
        const TerminalCommand command{sequence, node.slot, node.generation};
        if (!instance.terminalCommands.Push(command))
            return Fail(failure, FailureCode::InternalError, "Vellum terminal destroy storage invariant was violated");
        slot->destroyPending = true;
        slot->destroySequence = sequence;
        ++runtime.stats.terminalStructuralCommands;
        if (runtime.stats.terminalStructuralCommands > runtime.stats.peakTerminalStructuralCommands)
            runtime.stats.peakTerminalStructuralCommands = runtime.stats.terminalStructuralCommands;
        return true;
    }

    bool QueueReparent(Runtime::Impl& runtime, InstanceState& instance, const LocalNodeHandle node, const LocalNodeHandle parent,
                       const LocalNodeHandle beforeSibling, Failure* const failure) noexcept
    {
        ClearFailure(failure);
        if (runtime.stats.state != RuntimeState::Active || !runtime.stats.acceptingWork)
            return Fail(failure, FailureCode::InvalidState, "Vellum runtime is not accepting reparent work");
        if (instance.ordinaryCommands.Full())
            return RejectAdmission(runtime, failure, FailureCode::CapacityExceeded, "Vellum structural command lane is full");
        if (ResolveNodeSlot(instance, node, true) == nullptr)
            return Fail(failure, FailureCode::StaleHandle, "Vellum reparent target is stale");
        if (parent.IsValid() && ResolveNodeSlot(instance, parent, true) == nullptr)
            return Fail(failure, FailureCode::StaleHandle, "Vellum reparent parent is stale");
        if (beforeSibling.IsValid() && ResolveNodeSlot(instance, beforeSibling, true) == nullptr)
            return Fail(failure, FailureCode::StaleHandle, "Vellum reparent sibling is stale");
        u64 sequence = 0;
        if (!NextSequence(runtime, sequence))
            return RejectAdmission(runtime, failure, FailureCode::CapacityExceeded, "Vellum mutation sequence space is exhausted");
        const StructuralCommand command{sequence, StructuralCommandKind::Reparent, node, parent, beforeSibling, NodeKind::Invalid};
        if (!instance.ordinaryCommands.Push(command))
            return RejectAdmission(runtime, failure, FailureCode::CapacityExceeded, "Vellum structural command lane is full");
        ++runtime.stats.pendingStructuralCommands;
        if (runtime.stats.pendingStructuralCommands > runtime.stats.peakPendingStructuralCommands)
            runtime.stats.peakPendingStructuralCommands = runtime.stats.pendingStructuralCommands;
        return true;
    }

    bool CommitStructural(Runtime::Impl& runtime, const u64 fence, Failure* const failure) noexcept
    {
        runtime.structuralMergeCount = 0;
        for (u32 index = 0; index < runtime.config.maximumInstances; ++index)
        {
            InstanceSlot& slot = runtime.instanceSlots[index];
            if (slot.instance == nullptr || slot.state != SlotState::Live)
                continue;
            const u64 sequence = NextReadySequence(*slot.instance, fence);
            if (sequence != ExhaustedSequence && !PushMergeEntry(runtime, {sequence, index}))
                return Fail(failure, FailureCode::InternalError, "Vellum structural merge scratch overflowed");
        }

        while (runtime.structuralMergeCount != 0)
        {
            const CommitMergeEntry entry = PopMergeEntry(runtime);
            if (entry.instanceIndex >= runtime.config.maximumInstances)
                return Fail(failure, FailureCode::InternalError, "Vellum structural merge entry is invalid");
            InstanceSlot& slot = runtime.instanceSlots[entry.instanceIndex];
            if (slot.instance == nullptr || slot.state != SlotState::Live)
                return Fail(failure, FailureCode::InternalError, "Vellum structural merge instance changed during commit");
            InstanceState& instance = *slot.instance;
            const StructuralCommand* const ordinary = instance.ordinaryCommands.Front();
            const TerminalCommand* const terminal = instance.terminalCommands.Front();
            const bool ordinaryReady = ordinary != nullptr && ordinary->sequence <= fence;
            const bool terminalReady = terminal != nullptr && terminal->sequence <= fence;
            if ((!ordinaryReady && !terminalReady) || NextReadySequence(instance, fence) != entry.sequence)
                return Fail(failure, FailureCode::InternalError, "Vellum structural merge ordering invariant was violated");
            if (terminalReady && (!ordinaryReady || terminal->sequence < ordinary->sequence))
            {
                if (runtime.stats.terminalStructuralCommands == 0)
                    return Fail(failure, FailureCode::InternalError, "Vellum terminal command accounting underflowed");
                const TerminalCommand command = *terminal;
                ApplyDestroy(runtime, instance, command);
                instance.terminalCommands.Pop();
                --runtime.stats.terminalStructuralCommands;
            }
            else
            {
                if (runtime.stats.pendingStructuralCommands == 0)
                    return Fail(failure, FailureCode::InternalError, "Vellum structural command accounting underflowed");
                const StructuralCommand command = *ordinary;
                if (command.kind == StructuralCommandKind::Create)
                    ApplyCreate(runtime, instance, command);
                else
                    ApplyReparent(runtime, instance, command);
                instance.ordinaryCommands.Pop();
                --runtime.stats.pendingStructuralCommands;
            }
            const u64 next = NextReadySequence(instance, fence);
            if (next != ExhaustedSequence && !PushMergeEntry(runtime, {next, entry.instanceIndex}))
                return Fail(failure, FailureCode::InternalError, "Vellum structural merge scratch overflowed");
        }
        return true;
    }

    bool GetInstanceInfo(const Runtime::Impl& runtime, const VellumInstanceHandle handle, InstanceInfo& info) noexcept
    {
        info = {};
        const InstanceState* const instance = ResolveInstance(runtime, handle);
        if (instance == nullptr)
            return false;
        info.instance = handle;
        info.root = MakeNodeHandle(*instance, instance->rootSlot);
        info.nodeCount = instance->denseCount;
        info.nodeCapacity = instance->nodeCapacity;
        info.detachedRootCount = instance->detachedRootCount;
        info.detachedNodeCount = instance->detachedNodeCount;
        return true;
    }

    bool GetNodeInfo(const Runtime::Impl& runtime, const VellumNodeHandle handle, NodeInfo& info) noexcept
    {
        info = {};
        const InstanceState* const instance = ResolveInstance(runtime, handle.instance);
        if (instance == nullptr || ResolveNodeSlot(*instance, handle.local) == nullptr)
            return false;
        const NodeRow* const row = ResolveLiveRow(*instance, handle.local.slot);
        if (row == nullptr)
            return false;
        info.node = handle;
        info.kind = row->kind;
        info.parent = MakeNodeHandle(*instance, row->parent);
        info.firstChild = MakeNodeHandle(*instance, row->firstChild);
        info.lastChild = MakeNodeHandle(*instance, row->lastChild);
        info.previousSibling = MakeNodeHandle(*instance, row->previousSibling);
        info.nextSibling = MakeNodeHandle(*instance, row->nextSibling);
        info.childCount = row->childCount;
        return true;
    }
} // namespace vanguard::vellum::detail
