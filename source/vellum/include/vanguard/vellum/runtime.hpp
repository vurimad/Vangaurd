#pragma once

#include <vanguard/resources/resource_pipeline.hpp>
#include <vanguard/vellum/types.hpp>

namespace vanguard::streaming
{
    class ResourceStreamer;
}

namespace vanguard::vellum
{
    struct RuntimeConfig
    {
        u32 maximumInstances = 64;
        u32 maximumNodesPerDocument = 131'072;
        u32 maximumTotalNodes = 1'048'576;
        u32 maximumHierarchyDepth = 1'024;
        u32 maximumCookedDocumentBytes = 64u * 1024u * 1024u;
        u32 maximumCookedPropertiesPerDocument = 524'288;
        u32 maximumStructuralCommandsPerInstance = 65'536;
        u32 maximumConcurrentSpawns = 256;
        u32 maximumMaterializedNodesPerUpdate = 4'096;
    };

    struct NodeCreateDesc
    {
        NodeKind kind = NodeKind::Invalid;
        VellumNodeHandle parent;
        VellumNodeHandle beforeSibling;
    };

    struct SpawnDesc
    {
        resources::ResourceReference document;
        resources::LoadPriority priority = resources::LoadPriority::Normal;
        u32 additionalNodeCapacity = 0;
    };

    struct InstanceInfo
    {
        VellumInstanceHandle instance;
        VellumNodeHandle root;
        u32 nodeCount = 0;
        u32 nodeCapacity = 0;
        u32 detachedRootCount = 0;
        u32 detachedNodeCount = 0;
    };

    struct NodeInfo
    {
        VellumNodeHandle node;
        NodeKind kind = NodeKind::Invalid;
        VellumNodeHandle parent;
        VellumNodeHandle firstChild;
        VellumNodeHandle lastChild;
        VellumNodeHandle previousSibling;
        VellumNodeHandle nextSibling;
        u32 childCount = 0;
    };

    enum class FailureCode : u8
    {
        None,
        NotInitialized,
        AlreadyInitialized,
        InvalidState,
        WrongThread,
        InvalidDependency,
        InvalidConfiguration,
        InvalidArgument,
        OutOfMemory,
        CapacityExceeded,
        StaleHandle,
        WrongNodeKind,
        PropertyNotFound,
        PropertyTypeMismatch,
        StructuralConflict,
        HierarchyCycle,
        CrossInstance,
        ResourceFailure,
        DecodeFailure,
        Cancelled,
        LiveWorkRemains,
        InternalError
    };

    struct Failure
    {
        FailureCode code = FailureCode::None;
        const char* message = nullptr;
    };

    struct SpawnInfo
    {
        SpawnHandle spawn;
        SpawnState state = SpawnState::Invalid;
        resources::ResourceReference document;
        VellumInstanceHandle instance;
        FailureCode failure = FailureCode::None;
        resources::Failure resourceFailure = resources::Failure::None;
        u32 documentGeneration = 0;
        u32 materializedNodes = 0;
        u32 totalNodes = 0;
    };

    class SpawnRequest final
    {
    public:
        SpawnRequest() noexcept = default;
        SpawnRequest(SpawnRequest&& other) noexcept;
        ~SpawnRequest();

        SpawnRequest(const SpawnRequest&) = delete;
        SpawnRequest& operator=(const SpawnRequest&) = delete;
        SpawnRequest& operator=(SpawnRequest&& other) noexcept;

        [[nodiscard]] bool IsValid() const noexcept;
        // Reset/destruction may occur away from the semantic owner. They publish
        // bounded cancel+release intent only; staging/instance teardown remains
        // owner-thread work. A succeeded instance transfers to the caller only
        // through Runtime::TakeSpawnResult().
        void Reset() noexcept;

    private:
        SpawnRequest(void* control, SpawnHandle handle) noexcept;

        void* m_control = nullptr;
        SpawnHandle m_handle;

        friend class Runtime;
    };

    enum class RuntimeState : u8
    {
        Uninitialized,
        Active,
        Quiescing,
        Drained
    };

    struct DirtyFrontierStats
    {
        u32 current = 0;
        u32 peak = 0;
    };

    struct RuntimeStats
    {
        u32 instanceCapacity = 0;
        u32 freeInstanceSlots = 0;
        u32 deferredInstanceSlots = 0;
        u32 totalNodeCapacity = 0;
        u32 reservedNodeCapacity = 0;
        u32 liveInstances = 0;
        u32 peakLiveInstances = 0;
        u32 liveNodes = 0;
        u32 peakLiveNodes = 0;
        u32 liveDetachedRoots = 0;
        u32 peakDetachedRoots = 0;
        u32 liveDetachedNodes = 0;
        u32 peakDetachedNodes = 0;
        u32 freeNodeSlots = 0;
        u32 deferredNodeSlots = 0;
        u32 exhaustedInstanceSlots = 0;
        u32 exhaustedNodeSlots = 0;
        u64 pendingStructuralCommands = 0;
        u64 peakPendingStructuralCommands = 0;
        u64 terminalStructuralCommands = 0;
        u64 peakTerminalStructuralCommands = 0;
        u32 liveSpawns = 0;
        u32 peakLiveSpawns = 0;
        u32 spawnCapacity = 0;
        u32 freeSpawnSlots = 0;
        u32 deferredSpawnSlots = 0;
        u32 exhaustedSpawnSlots = 0;
        u32 queuedSpawns = 0;
        u32 loadingSpawns = 0;
        u32 materializingSpawns = 0;
        u32 readyToCommitSpawns = 0;
        u32 succeededSpawns = 0;
        u32 failedSpawns = 0;
        u32 cancelledSpawns = 0;
        u64 totalSpawnRequests = 0;
        u64 successfulSpawnOperations = 0;
        u64 failedSpawnOperations = 0;
        u64 cancelledSpawnOperations = 0;
        u32 stagedNodes = 0;
        u32 peakStagedNodes = 0;
        u32 liveDecodedDocuments = 0;
        u32 peakDecodedDocuments = 0;
        u64 materializedNodes = 0;
        u64 rejectedAdmissions = 0;
        u64 staleWorkRejected = 0;
        u64 denseRowMoves = 0;
        u64 structuralCommandsApplied = 0;
        u64 structuralCommandsRejected = 0;
        u64 semanticEpoch = 0;
        u64 semanticUpdateHeapAllocations = 0;
        u64 cleanNodeDiscoveries = 0;
        u64 measuredNodes = 0;
        u64 arrangedNodes = 0;
        u64 collapsedDescendantsSkipped = 0;
        u32 maximumHierarchyDepthObserved = 0;
        u32 maximumSiblingCountObserved = 0;
        DirtyFrontierStats textShapeDirty;
        DirtyFrontierStats measureDirty;
        DirtyFrontierStats arrangeDirty;
        DirtyFrontierStats paintDirty;
        DirtyFrontierStats hitTestDirty;
        DirtyFrontierStats navigationDirty;
        RuntimeState state = RuntimeState::Uninitialized;
        bool acceptingWork = false;
    };

    class Runtime final
    {
    public:
        struct Impl;

        Runtime() noexcept = default;
        ~Runtime();

        Runtime(const Runtime&) = delete;
        Runtime& operator=(const Runtime&) = delete;

        // The injected streamer is non-owning and must remain alive and initialized until
        // this runtime has completed a successful Shutdown(), including decoder unregister.
        [[nodiscard]] bool Initialize(streaming::ResourceStreamer& streamer, const RuntimeConfig& config = {}, Failure* failure = nullptr) noexcept;
        [[nodiscard]] bool BeginSemanticUpdate(Failure* failure = nullptr) noexcept;
        [[nodiscard]] bool RequestSpawn(const SpawnDesc& desc, SpawnRequest& request, Failure* failure = nullptr) noexcept;
        [[nodiscard]] bool CancelSpawn(SpawnRequest& request, Failure* failure = nullptr) noexcept;
        [[nodiscard]] bool GetSpawnInfo(const SpawnRequest& request, SpawnInfo& info, Failure* failure = nullptr) const noexcept;
        [[nodiscard]] bool TakeSpawnResult(SpawnRequest& request, VellumInstanceHandle& instance, Failure* failure = nullptr) noexcept;
        [[nodiscard]] bool ReleaseInstance(VellumInstanceHandle instance, Failure* failure = nullptr) noexcept;
        [[nodiscard]] bool QueueCreate(VellumInstanceHandle instance, const NodeCreateDesc& desc, VellumNodeHandle& node,
                                       Failure* failure = nullptr) noexcept;
        [[nodiscard]] bool QueueDestroy(VellumNodeHandle node, Failure* failure = nullptr) noexcept;
        [[nodiscard]] bool QueueReparent(VellumNodeHandle node, VellumNodeHandle parent, VellumNodeHandle beforeSibling = {},
                                         Failure* failure = nullptr) noexcept;
        [[nodiscard]] bool CommitStructuralMutations(Failure* failure = nullptr) noexcept;
        [[nodiscard]] bool ResolveProperty(VellumNodeHandle node, PropertyId id, PropertyHandle& property,
                                           Failure* failure = nullptr) const noexcept;
        [[nodiscard]] bool WriteProperty(PropertyHandle property, f32 value, Failure* failure = nullptr) noexcept;
        [[nodiscard]] bool WriteProperty(PropertyHandle property, Visibility value, Failure* failure = nullptr) noexcept;
        [[nodiscard]] bool WriteProperty(PropertyHandle property, Orientation value, Failure* failure = nullptr) noexcept;
        [[nodiscard]] bool MeasureInstance(VellumInstanceHandle instance, const LayoutConstraints& constraints, Size& desired,
                                           Failure* failure = nullptr) noexcept;
        [[nodiscard]] bool ArrangeInstance(VellumInstanceHandle instance, const Rect& rect, Failure* failure = nullptr) noexcept;
        [[nodiscard]] bool GetGeometry(VellumNodeHandle node, Geometry& geometry, Failure* failure = nullptr) const noexcept;
        [[nodiscard]] bool GetInstanceInfo(VellumInstanceHandle instance, InstanceInfo& info, Failure* failure = nullptr) const noexcept;
        [[nodiscard]] bool GetNodeInfo(VellumNodeHandle node, NodeInfo& info, Failure* failure = nullptr) const noexcept;
        [[nodiscard]] bool Quiesce(Failure* failure = nullptr) noexcept;
        [[nodiscard]] bool Drain(Failure* failure = nullptr) noexcept;
        [[nodiscard]] bool Shutdown(Failure* failure = nullptr) noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;
        [[nodiscard]] RuntimeStats GetStats() const noexcept;

    private:
        Impl* m_impl = nullptr;
    };
} // namespace vanguard::vellum
