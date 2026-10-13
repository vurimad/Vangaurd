#pragma once

#include <vanguard/rendering/render_flow_resource_execution.hpp>
#include <vanguard/rendering/render_node_graph.hpp>

namespace vanguard::rendering
{
    namespace detail
    {
        struct ResourceContractNodeTemplate;
    }

    class FrameRenderer;
    class GeometryFrameWork;
    class PreparedRenderViewFamily;
    class RenderCameraStorage;
    class RenderFrameCommandLists;
    class RenderFrameInfo;
    class RenderFrameOutputTransaction;
    class RenderViewport;
    class RenderNodeCommandListGroup;
    class RenderNodeSynchronize;
    struct RenderView;
    struct RenderNodeImplContext;

    struct RenderFlowNameTag
    {
        LogicalResourceKey key;
        LogicalResourceId resource;
        FrameResourceKind kind = FrameResourceKind::Invalid;

        [[nodiscard]] bool IsValid() const noexcept { return key.GetFlowSpace().IsValid() && !key.GetName().Empty(); }
    };

    struct RenderNodeResourceUseBinding
    {
        RenderNodeResourceUseBinding() noexcept : name(memory::pools::Rendering::GetInstance()) {}

        FlowSpaceId flowSpace;
        containers::String name;
        u32 lookupHash = 0;
        ResourceUseId use;
        FrameResourceKind kind = FrameResourceKind::Invalid;
        u32 nextDuplicate = InvalidRenderFlowResourceIndex;
        u32 resolvedIndex = InvalidRenderFlowResourceIndex;
    };

    struct RenderNodeResourceLookupGroup
    {
        u32 lookupHash = 0;
        u32 firstUse = InvalidRenderFlowResourceIndex;
        u32 lastUse = InvalidRenderFlowResourceIndex;
        // Per-occurrence execution cursor. Lookup groups are built while the
        // declaration is already walking this child and execute exactly once.
        u32 runtimeNextUse = InvalidRenderFlowResourceIndex;
        u32 nextBucketGroup = InvalidRenderFlowResourceIndex;
    };

    struct RenderNodeResourceDecisionBinding
    {
        DecisionId decision;
    };

    struct RenderNodeResourceRange
    {
        u32 firstUse = 0;
        u32 useCount = 0;
        u32 firstDecision = 0;
        u32 decisionCount = 0;
        u32 lookupBucketOffset = 0;
        u32 lookupBucketCount = 0;
        u32 lookupGroupOffset = 0;
        u32 lookupGroupCount = 0;
        CompiledExecutionRange execution;
    };

    struct RenderNodeResourceUseView
    {
        FlowSpaceId flowSpace;
        containers::StringView name;
        u32 lookupHash = 0;
        ResourceUseId use;
        FrameResourceKind kind = FrameResourceKind::Invalid;
        u32 nextDuplicate = InvalidRenderFlowResourceIndex;
        u32 resolvedIndex = InvalidRenderFlowResourceIndex;
    };

    // One frame-owned record per composed graph occurrence. A command-list
    // group appends one range for each ordered child Process() call.
    class RenderNodeResources final
    {
    public:
        RenderNodeResources() noexcept;
        void Reset() noexcept;

    private:
        [[nodiscard]] bool IsWarmContract() const noexcept { return m_warmTemplate != nullptr; }
        void BindWarmContract(const detail::ResourceContractNodeTemplate& source, u32 generation) noexcept;
        void ReleaseWarmContractView() noexcept;
        [[nodiscard]] u32 UseCount() const noexcept;
        [[nodiscard]] u32 DecisionCount() const noexcept;
        [[nodiscard]] u32 RangeCount() const noexcept;
        [[nodiscard]] u32 LookupBucketCount() const noexcept;
        [[nodiscard]] u32 LookupGroupCount() const noexcept;
        [[nodiscard]] u32 TextureBindingCount() const noexcept;
        [[nodiscard]] u32 BufferBindingCount() const noexcept;
        [[nodiscard]] RenderNodeResourceUseView GetUse(u32 index) const noexcept;
        [[nodiscard]] DecisionId GetDecision(u32 index) const noexcept;
        [[nodiscard]] RenderNodeResourceRange GetRange(u32 index) const noexcept;
        [[nodiscard]] u32 GetLookupBucket(u32 index) const noexcept;
        [[nodiscard]] RenderNodeResourceLookupGroup GetLookupGroup(u32 index) const noexcept;
        void SetLookupGroupRuntimeNextUse(u32 index, u32 nextUse) noexcept;
        void ClearRangeExecutions() noexcept;
        void SetRangeExecution(u32 index, const CompiledExecutionRange& execution) noexcept;

        containers::DynamicArray<RenderNodeResourceUseBinding> m_uses;
        containers::DynamicArray<RenderNodeResourceDecisionBinding> m_decisions;
        containers::DynamicArray<RenderNodeResourceRange> m_ranges;
        containers::DynamicArray<u32> m_lookupBuckets;
        containers::DynamicArray<RenderNodeResourceLookupGroup> m_lookupGroups;
        containers::DynamicArray<ResolvedTextureUse> m_resolvedTextures;
        containers::DynamicArray<ResolvedBufferUse> m_resolvedBuffers;
        containers::DynamicArray<CompiledExecutionRange> m_warmRangeExecutions;
        containers::DynamicArray<u32> m_warmLookupRuntimeNextUses;
        const detail::ResourceContractNodeTemplate* m_warmTemplate = nullptr;
        u32 m_warmGeneration = 0;

        friend struct RenderNodeImplContext;
        friend class RenderNodeGraph;
        friend class RenderNodeResourceBindings;
    };

    class RenderNodeResourceBindings final
    {
    public:
        RenderNodeResourceBindings() noexcept;
        ~RenderNodeResourceBindings();

        RenderNodeResourceBindings(const RenderNodeResourceBindings&) = delete;
        RenderNodeResourceBindings& operator=(const RenderNodeResourceBindings&) = delete;

        void Reset() noexcept;

    private:
        struct Entry
        {
            RenderFlowNodeId node;
            RenderNodeResources resources;
            RenderNodeImplContext* executionContext = nullptr;
            RenderNodeImpl* executionImpl = nullptr;
            CompiledExecutionPacketView packet;
            ExecutionPacketCursor cursor;
            RenderFlowResourceFailure executionFailure;
        };

        containers::DynamicArray<Entry> m_entries;
        void* m_executionContextStorage = nullptr;
        u32 m_executionContextCount = 0;

        void ReleaseWarmContractViews() noexcept;
        void BlockExecution(RenderFlowResourceFailureCode code, const char* message) noexcept;
        [[nodiscard]] bool GetFirstExecutionFailure(RenderFlowResourceFailure& failure) const noexcept;

        friend class FrameRenderer;
        friend class RenderNodeGraph;
        friend class RenderNodeJob;
    };

    // Per-node context. It borrows frame-owned data; the retained
    // render frame must outlive every context and child job made from it.
    struct RenderNodeImplContext
    {
        struct InitData
        {
            explicit InitData(const u32 dispatcherThreadIndexValue) noexcept : dispatcherThreadIndex(dispatcherThreadIndexValue) {}

            const RenderFrameInfo* frame = nullptr;
            const PreparedRenderViewFamily* viewFamily = nullptr;
            RenderCameraStorage* cameraStorage = nullptr;
            GeometryFrameWork* geometryFrameWork = nullptr;
            RenderFrameCommandLists* frameCommandLists = nullptr;
            RenderFrameOutputTransaction* outputTransaction = nullptr;
            u32 dispatcherThreadIndex = ~u32{0};
            bool skipCameraData = false;
        };

        RenderNodeImplContext() noexcept;
        RenderNodeImplContext(const RenderNodeImplContext& other) noexcept;
        RenderNodeImplContext(const RenderNodeImplContext& other, u32 dispatcherThreadIndex) noexcept;
        RenderNodeImplContext& operator=(const RenderNodeImplContext&) = delete;

        void Init(const InitData& initData) noexcept;
        void SetupNodeData(const RenderNodeContext& nodeContext, const RenderNodeParameters& nodeParameters) noexcept;
        void ResetNodeData() noexcept;
        void BeginNewNode() const noexcept;

        [[nodiscard]] const RenderFrameInfo& GetFrameInfo() const noexcept;
        [[nodiscard]] RenderViewport* GetViewport() const noexcept;
        [[nodiscard]] const PreparedRenderViewFamily* GetViewFamily() const noexcept { return m_viewFamily; }
        [[nodiscard]] RenderCameraStorage* GetCameraStorage() const noexcept { return m_cameraStorage; }
        [[nodiscard]] GeometryFrameWork* GetGeometryFrameWork() const noexcept { return m_geometryFrameWork; }
        [[nodiscard]] const RenderView* GetView() const noexcept { return m_view; }
        [[nodiscard]] bool HasView() const noexcept { return m_view != nullptr; }
        [[nodiscard]] bool HasAnyCameraStorage() const noexcept { return m_cameraStorage != nullptr; }
        [[nodiscard]] bool HasPresentationOutput() const noexcept;
        [[nodiscard]] bool HasFrameOutput() const noexcept;
        [[nodiscard]] rhi::TextureRef GetFrameOutputTexture() const noexcept;
        void RTImportFrameOutput() const noexcept;
        void RTExportFrameOutput() const noexcept;
        [[nodiscard]] i32 GetCameraIndex() const noexcept { return m_cameraIndex; }
        [[nodiscard]] RenderFlowGroup GetRenderFlowGroup() const noexcept { return m_renderFlowGroup; }
        [[nodiscard]] RenderFlowSpace GetRenderFlowSpace() const noexcept { return m_renderFlowSpace; }
        [[nodiscard]] u32 GetDispatcherThreadIndex() const noexcept { return m_dispatcherThreadIndex; }
        void MarkPresentNodeReached() const noexcept;
        void SetCommandList(rhi::CommandListRef commandList) const noexcept;
        [[nodiscard]] rhi::CommandListRef GetCommandList() const noexcept;
        [[nodiscard]] CommandScopeId GetCommandScope() const noexcept;
        [[nodiscard]] bool HasCommandList() const noexcept;
        [[nodiscard]] bool SubmitCommandLists(const char* scopeName, rhi::CommandListSyncType sync, jobs::Builder& builder) const noexcept;
        [[nodiscard]] RenderFlowNameTag RTNameTag(LogicalResourceName name) const noexcept;
        [[nodiscard]] static RenderFlowNameTag RTSharedNameTag(LogicalResourceName name) noexcept;
        /// Compact prepared-family view index, never a persistent camera slot.
        [[nodiscard]] static RenderFlowNameTag RTCameraNameTag(u32 viewIndex, LogicalResourceName name) noexcept;
        [[nodiscard]] RenderFlowNameTag RTAlloc(LogicalResourceName name, const FrameTextureDesc& desc) const noexcept;
        [[nodiscard]] RenderFlowNameTag RTAlloc(LogicalResourceName name, const FrameBufferDesc& desc) const noexcept;
        [[nodiscard]] RenderFlowNameTag RTAlloc(LogicalResourceName name, const RenderFlowNameTag& desc) const noexcept;
        [[nodiscard]] RenderFlowNameTag RTSharedAlloc(LogicalResourceName name, const FrameTextureDesc& desc) const noexcept;
        [[nodiscard]] RenderFlowNameTag RTSharedAlloc(LogicalResourceName name, const FrameBufferDesc& desc) const noexcept;
        [[nodiscard]] RenderFlowNameTag RTTempAlloc(containers::StringView name, const FrameTextureDesc& desc) const noexcept;
        [[nodiscard]] RenderFlowNameTag RTTempAlloc(containers::StringView name, const FrameBufferDesc& desc) const noexcept;
        [[nodiscard]] RenderFlowNameTag RTTempAlloc(containers::StringView name, const RenderFlowNameTag& desc) const noexcept;
        void RTImportPresentationOutput() const noexcept;
        // The frame coordinator registers the retained texture before declarations run.
        [[nodiscard]] RenderFlowNameTag RTInject(LogicalResourceName name, ImportedResourceId imported) const noexcept;
        [[nodiscard]] RenderFlowNameTag RTSharedInjectBuffer(LogicalResourceName name, ImportedResourceId imported) const noexcept;
        void RTUsePresentationOutput() const noexcept;
        [[nodiscard]] const ResolvedTextureUse& RTPresentationOutput() const noexcept;
        void RTUseBegin(const RenderFlowNameTag& tag, const TextureUseDesc& desc) const noexcept;
        void RTUseBegin(LogicalResourceName name, const TextureUseDesc& desc) const noexcept { RTUseBegin(RTNameTag(name), desc); }
        void RTUseBegin(const RenderFlowNameTag& tag, const BufferUseDesc& desc) const noexcept;
        void RTUseBegin(LogicalResourceName name, const BufferUseDesc& desc) const noexcept { RTUseBegin(RTNameTag(name), desc); }
        void RTUseEnd(const RenderFlowNameTag& tag) const noexcept;
        void RTUseEnd(LogicalResourceName name) const noexcept { RTUseEnd(RTNameTag(name)); }
        void RTSwap(const RenderFlowNameTag& left, const RenderFlowNameTag& right) const noexcept;
        [[nodiscard]] bool RTDecision(bool value) const noexcept;
        // Returned witnesses remain allocated for this occurrence, but become
        // invalid as soon as the concrete node's Process() call ends.
        [[nodiscard]] const ResolvedTextureUse& RTTexture(const RenderFlowNameTag& tag) const noexcept;
        [[nodiscard]] const ResolvedTextureUse& RTTexture(LogicalResourceName name) const noexcept { return RTTexture(RTNameTag(name)); }
        [[nodiscard]] const ResolvedBufferUse& RTBuffer(const RenderFlowNameTag& tag) const noexcept;
        [[nodiscard]] const ResolvedBufferUse& RTBuffer(LogicalResourceName name) const noexcept { return RTBuffer(RTNameTag(name)); }
    private:
        enum class Operation : u8
        {
            None,
            DeclareResources,
            PatchResourceContract,
            Execute
        };

        void BeginResourceDeclaration(RenderFlowResourceAllocator& allocator, ResourcePlanningWriter* writer, RenderNodeResources& resources,
                                      RenderFlowResourceFailure& failure) noexcept;
        void EndResourceDeclaration() noexcept;
        void BeginResourceContractPatch(RenderFlowResourceAllocator& allocator, RenderNodeResources& resources,
                                        RenderFlowResourceFailure& failure) noexcept;
        void EndResourceContractPatch() noexcept;
        void BeginResourceExecution(CompiledExecutionPacketView* packet, ExecutionPacketCursor& cursor, RenderNodeResources& resources, RenderFlowResourceFailure& failure) noexcept;
        [[nodiscard]] bool OpenResourceExecutionPacket() noexcept;
        void EndResourceExecution() noexcept;
        void EndNodeResourceUses() const noexcept;
        [[nodiscard]] bool CanRecordResourceOperation() const noexcept;
        [[nodiscard]] bool CanResolveResourceOperation() const noexcept;
        void FailResourceOperation(RenderFlowResourceFailureCode code, const char* message) const noexcept;
        [[nodiscard]] RenderFlowNameTag ResolveNamedTag(const RenderFlowNameTag& tag) const noexcept;
        [[nodiscard]] RenderFlowNameTag MakeTemporaryTag(containers::StringView name, LogicalResourceId resource, FrameResourceKind kind) const noexcept;
        [[nodiscard]] bool IsDeclaringResources() const noexcept { return m_operation == Operation::DeclareResources; }
        [[nodiscard]] bool IsPatchingResourceContract() const noexcept { return m_operation == Operation::PatchResourceContract; }
        [[nodiscard]] bool BeginResourceQueue(rhi::QueueType queue) const noexcept;
        [[nodiscard]] bool EndResourceQueue() const noexcept;
        [[nodiscard]] bool SyncResourceQueue(rhi::CommandListSyncType sync) const noexcept;
        [[nodiscard]] ResourcePlanningWriter& GetResourceWriter() const noexcept;
        [[nodiscard]] RenderFlowResourceFailure& GetResourceFailure() const noexcept;
        void SetCustomDataView(const RenderView* view, i32 cameraIndex) noexcept;

        const RenderFrameInfo* m_frame = nullptr;
        const PreparedRenderViewFamily* m_viewFamily = nullptr;
        RenderCameraStorage* m_cameraStorage = nullptr;
        GeometryFrameWork* m_geometryFrameWork = nullptr;
        RenderFrameCommandLists* m_frameCommandLists = nullptr;
        RenderFrameOutputTransaction* m_outputTransaction = nullptr;
        const RenderView* m_view = nullptr;
        i32 m_cameraIndex = -1;
        u32 m_dispatcherThreadIndex = ~u32{0};
        RenderFlowSpace m_renderFlowSpace = 0xffu;
        RenderFlowGroup m_renderFlowGroup = ~RenderFlowGroup{0};
        ResourcePlanningWriter* m_resourceWriter = nullptr;
        RenderFlowResourceAllocator* m_resourceAllocator = nullptr;
        RenderFlowResourceFailure* m_resourceFailure = nullptr;
        RenderNodeResources* m_nodeResources = nullptr;
        mutable u32 m_resourceRange = InvalidRenderFlowResourceIndex;
        mutable u32 m_nextResourceRange = 0;
        mutable u32 m_nextResourceDecision = 0;
        CompiledExecutionPacketView* m_executionPacket = nullptr;
        ExecutionPacketCursor* m_resourceCursor = nullptr;
        Operation m_operation = Operation::None;
        friend class RenderNodeGraph;
        friend class RenderCameraStorage;
        friend class RenderNodeCommandListGroup;
        friend class RenderNodeSynchronize;
        friend class RenderNodeJob;
        friend class RenderNodeImpl;
        friend class RenderNodeGraph;
    };
} // namespace vanguard::rendering
