#pragma once

#include <vanguard/resources/resources.hpp>
#include <vanguard/vellum/fixed_storage.hpp>
#include <vanguard/vellum/layout_storage.hpp>
#include <vanguard/vellum/runtime_internal.hpp>

namespace vanguard::vellum::detail
{
    enum class SlotState : u8
    {
        Free,
        ReservedCreate,
        Live,
        DeferredFree,
        Exhausted
    };

    struct NodeSlot
    {
        u64 destroySequence = 0;
        u32 generation = 1;
        u32 row = InvalidIndex;
        u32 nextFree = InvalidIndex;
        u32 detachedRootIndex = InvalidIndex;
        u16 dirtyMask = 0;
        u16 queuedMask = 0;
        SlotState state = SlotState::Free;
        bool destroyPending = false;
        bool detached = false;
        u8 layoutForceMask = 0;
    };

    struct NodeRow
    {
        NodeKind kind = NodeKind::Invalid;
        u32 slot = InvalidIndex;
        u32 payloadIndex = InvalidIndex;
        u32 parent = InvalidIndex;
        u32 firstChild = InvalidIndex;
        u32 lastChild = InvalidIndex;
        u32 previousSibling = InvalidIndex;
        u32 nextSibling = InvalidIndex;
        u32 childCount = 0;
    };

    enum class StructuralCommandKind : u8
    {
        Create,
        Reparent
    };

    struct StructuralCommand
    {
        u64 sequence = 0;
        StructuralCommandKind kind = StructuralCommandKind::Create;
        LocalNodeHandle node;
        LocalNodeHandle parent;
        LocalNodeHandle beforeSibling;
        NodeKind nodeKind = NodeKind::Invalid;
    };

    struct TerminalCommand
    {
        u64 sequence = 0;
        u32 slot = InvalidIndex;
        u32 generation = 0;
    };

    struct CommitMergeEntry
    {
        u64 sequence = 0;
        u32 instanceIndex = InvalidIndex;
    };

    struct TraversalEntry
    {
        u32 slot = InvalidIndex;
        u32 depth = 0;
    };

    struct InstanceState
    {
        VellumInstanceHandle handle;
        resources::ResourceHandle document;
        memory::MemoryBlock arenaBlock;
        memory::MemoryBlock commandBlock;
        NodeSlot* nodeSlots = nullptr;
        NodeRow* rows = nullptr;
        TraversalEntry* traversalScratch = nullptr;
        TerminalCommand* terminalStorage = nullptr;
        StructuralCommand* ordinaryStorage = nullptr;
        u32* detachedRoots = nullptr;
        CoreProperties* coreProperties = nullptr;
        MeasureCache* measureCaches = nullptr;
        Geometry* geometries = nullptr;
        StackPanelPayload* stackPanels = nullptr;
        SpacerPayload* spacers = nullptr;
        LayoutFrame* layoutScratch = nullptr;
        FixedRing<TerminalCommand> terminalCommands;
        FixedRing<StructuralCommand> ordinaryCommands;
        FixedRing<WorkRef> dirtyFrontiers[DirtyDomainCount];
        u32 nodeCapacity = 0;
        u32 denseCount = 0;
        u32 stackPanelCount = 0;
        u32 spacerCount = 0;
        u32 detachedRootCount = 0;
        u32 detachedNodeCount = 0;
        u32 freeNodeCount = 0;
        u32 deferredNodeCount = 0;
        u32 exhaustedNodeCount = 0;
        u32 firstFreeNode = InvalidIndex;
        u32 firstDeferredNode = InvalidIndex;
        u32 rootSlot = InvalidIndex;
        u32 traversalCount = 0;
        u64 nextGeometryRevision = 1;
    };

    struct InstanceSlot
    {
        InstanceState* instance = nullptr;
        u32 generation = 1;
        u32 nextFree = InvalidIndex;
        SlotState state = SlotState::Free;
    };

    [[nodiscard]] bool InitializeInstanceRegistry(Runtime::Impl& runtime, Failure* failure) noexcept;
    void DestroyInstanceRegistry(Runtime::Impl& runtime) noexcept;
    void PromoteDeferredInstances(Runtime::Impl& runtime) noexcept;

    [[nodiscard]] InstanceState* ResolveInstance(Runtime::Impl& runtime, VellumInstanceHandle handle, bool allowReserved = false) noexcept;
    [[nodiscard]] const InstanceState* ResolveInstance(const Runtime::Impl& runtime, VellumInstanceHandle handle, bool allowReserved = false) noexcept;
    [[nodiscard]] NodeSlot* ResolveNodeSlot(InstanceState& instance, LocalNodeHandle handle, bool allowReserved = false) noexcept;
    [[nodiscard]] const NodeSlot* ResolveNodeSlot(const InstanceState& instance, LocalNodeHandle handle, bool allowReserved = false) noexcept;

    [[nodiscard]] bool AllocateInstance(Runtime::Impl& runtime, u32 nodeCapacity, VellumInstanceHandle& handle, InstanceState*& instance,
                                        Failure* failure) noexcept;
    [[nodiscard]] bool MaterializeDocumentNode(Runtime::Impl& runtime, InstanceState& instance, const DocumentResource& document,
                                               u32 documentNodeIndex, Failure* failure) noexcept;
    [[nodiscard]] bool PublishInstance(Runtime::Impl& runtime, VellumInstanceHandle handle, u32 rootSlot, Failure* failure) noexcept;
    [[nodiscard]] bool RetireInstance(Runtime::Impl& runtime, VellumInstanceHandle handle, Failure* failure) noexcept;

    void PromoteDeferredNodes(Runtime::Impl& runtime, InstanceState& instance) noexcept;
    [[nodiscard]] bool QueueCreate(Runtime::Impl& runtime, InstanceState& instance, const NodeCreateDesc& desc, VellumNodeHandle& node,
                                   Failure* failure) noexcept;
    [[nodiscard]] bool QueueDestroy(Runtime::Impl& runtime, InstanceState& instance, LocalNodeHandle node, Failure* failure) noexcept;
    [[nodiscard]] bool QueueReparent(Runtime::Impl& runtime, InstanceState& instance, LocalNodeHandle node, LocalNodeHandle parent,
                                     LocalNodeHandle beforeSibling, Failure* failure) noexcept;
    [[nodiscard]] bool CommitStructural(Runtime::Impl& runtime, u64 fence, Failure* failure) noexcept;

    [[nodiscard]] bool GetInstanceInfo(const Runtime::Impl& runtime, VellumInstanceHandle handle, InstanceInfo& info) noexcept;
    [[nodiscard]] bool GetNodeInfo(const Runtime::Impl& runtime, VellumNodeHandle handle, NodeInfo& info) noexcept;
} // namespace vanguard::vellum::detail
