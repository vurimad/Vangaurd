#pragma once

#include <vanguard/rendering/render_flow_resource_execution.hpp>
#include <vanguard/rendering/render_node_graph.hpp>

namespace vanguard::rendering
{
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

        [[nodiscard]] bool IsValid() const noexcept { return key.flowSpace.IsValid() && !key.name.Empty(); }
    };

    struct RenderNodeResourceUseBinding
    {
        RenderNodeResourceUseBinding() noexcept : name(memory::pools::Rendering::GetInstance()) {}

        FlowSpaceId flowSpace;
        containers::String name;
        ResourceUseId use;
        FrameResourceKind kind = FrameResourceKind::Invalid;
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
    };

    // One frame-owned record per composed graph occurrence. A command-list
    // group appends one range for each ordered child Process() call.
    class RenderNodeResources final
    {
    public:
        RenderNodeResources() noexcept;
        void Reset() noexcept;

    private:
        containers::DynamicArray<RenderNodeResourceUseBinding> m_uses;
        containers::DynamicArray<RenderNodeResourceDecisionBinding> m_decisions;
        containers::DynamicArray<RenderNodeResourceRange> m_ranges;

        friend struct RenderNodeImplContext;
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
            CompiledExecutionPacketView packet;
            ExecutionPacketCursor cursor;
            RenderFlowResourceFailure executionFailure;
        };

        containers::DynamicArray<Entry> m_entries;
        void* m_executionContextStorage = nullptr;
        u32 m_executionContextCount = 0;

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
        [[nodiscard]] RenderFlowNameTag RTNameTag(containers::StringView name) const noexcept;
        [[nodiscard]] static RenderFlowNameTag RTSharedNameTag(containers::StringView name) noexcept;
        /// Compact prepared-family view index, never a persistent camera slot.
        [[nodiscard]] static RenderFlowNameTag RTCameraNameTag(u32 viewIndex, containers::StringView name) noexcept;
        [[nodiscard]] RenderFlowNameTag RTAlloc(containers::StringView name, const FrameTextureDesc& desc) const noexcept;
        [[nodiscard]] RenderFlowNameTag RTAlloc(containers::StringView name, const FrameBufferDesc& desc) const noexcept;
        [[nodiscard]] RenderFlowNameTag RTAlloc(containers::StringView name, const RenderFlowNameTag& desc) const noexcept;
        [[nodiscard]] RenderFlowNameTag RTSharedAlloc(containers::StringView name, const FrameTextureDesc& desc) const noexcept;
        [[nodiscard]] RenderFlowNameTag RTSharedAlloc(containers::StringView name, const FrameBufferDesc& desc) const noexcept;
        [[nodiscard]] RenderFlowNameTag RTTempAlloc(containers::StringView name, const FrameTextureDesc& desc) const noexcept;
        [[nodiscard]] RenderFlowNameTag RTTempAlloc(containers::StringView name, const FrameBufferDesc& desc) const noexcept;
        [[nodiscard]] RenderFlowNameTag RTTempAlloc(containers::StringView name, const RenderFlowNameTag& desc) const noexcept;
        void RTImportPresentationOutput() const noexcept;
        // The frame coordinator registers the retained texture before declarations run.
        [[nodiscard]] RenderFlowNameTag RTInject(containers::StringView name, ImportedResourceId imported) const noexcept;
        [[nodiscard]] RenderFlowNameTag RTSharedInjectBuffer(containers::StringView name, ImportedResourceId imported) const noexcept;
        void RTUsePresentationOutput() const noexcept;
        [[nodiscard]] const ResolvedTextureUse& RTPresentationOutput() const noexcept;
        void RTUseBegin(const RenderFlowNameTag& tag, const TextureUseDesc& desc) const noexcept;
        void RTUseBegin(containers::StringView name, const TextureUseDesc& desc) const noexcept { RTUseBegin(RTNameTag(name), desc); }
        void RTUseBegin(const RenderFlowNameTag& tag, const BufferUseDesc& desc) const noexcept;
        void RTUseBegin(containers::StringView name, const BufferUseDesc& desc) const noexcept { RTUseBegin(RTNameTag(name), desc); }
        void RTUseEnd(const RenderFlowNameTag& tag) const noexcept;
        void RTUseEnd(containers::StringView name) const noexcept { RTUseEnd(RTNameTag(name)); }
        void RTSwap(const RenderFlowNameTag& left, const RenderFlowNameTag& right) const noexcept;
        [[nodiscard]] bool RTDecision(bool value) const noexcept;
        // Returned witnesses remain allocated for this occurrence, but become
        // invalid as soon as the concrete node's Process() call ends.
        [[nodiscard]] const ResolvedTextureUse& RTTexture(const RenderFlowNameTag& tag) const noexcept;
        [[nodiscard]] const ResolvedTextureUse& RTTexture(containers::StringView name) const noexcept { return RTTexture(RTNameTag(name)); }
        [[nodiscard]] const ResolvedBufferUse& RTBuffer(const RenderFlowNameTag& tag) const noexcept;
        [[nodiscard]] const ResolvedBufferUse& RTBuffer(containers::StringView name) const noexcept { return RTBuffer(RTNameTag(name)); }
    private:
        enum class Operation : u8
        {
            None,
            DeclareResources,
            Execute
        };

        void BeginResourceDeclaration(RenderFlowResourceAllocator& allocator, ResourcePlanningWriter* writer, RenderNodeResources& resources,
                                      RenderFlowResourceFailure& failure) noexcept;
        void EndResourceDeclaration() noexcept;
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
        mutable u32 m_nextResourceUse = 0;
        mutable u32 m_nextResourceDecision = 0;
        CompiledExecutionPacketView* m_executionPacket = nullptr;
        ExecutionPacketCursor* m_resourceCursor = nullptr;
        mutable containers::DynamicArray<ResolvedTextureUse> m_resolvedTextures{memory::pools::Rendering::GetInstance()};
        mutable containers::DynamicArray<ResolvedBufferUse> m_resolvedBuffers{memory::pools::Rendering::GetInstance()};
        mutable containers::DynamicArray<ResourceUseId> m_resolvedUseOrder{memory::pools::Rendering::GetInstance()};
        mutable containers::DynamicArray<u32> m_resolvedResourceIndices{memory::pools::Rendering::GetInstance()};
        mutable containers::DynamicArray<u8> m_resourceLookups{memory::pools::Rendering::GetInstance()};
        Operation m_operation = Operation::None;
        friend class RenderNodeGraph;
        friend class RenderCameraStorage;
        friend class RenderNodeCommandListGroup;
        friend class RenderNodeSynchronize;
        friend class RenderNodeJob;
        friend class RenderNodeImpl;
    };
} // namespace vanguard::rendering
