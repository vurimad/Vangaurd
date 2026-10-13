#pragma once

#include <vanguard/concurrency/atomic.hpp>
#include <vanguard/concurrency/synchronization.hpp>
#include <vanguard/rhi/backend/command_list_retention.hpp>
#include <vanguard/rhi/backend/resource_lifetime.hpp>
#include <vanguard/rhi/rhi_backend.hpp>
#include <nvrhi/nvrhi.h>

namespace vanguard::rhi::backend
{
    using SerializedQueueOperation = BackendStatus (*)(void* context) noexcept;
    using CommandSubmissionCallback = void (*)(void* context, GpuFence completion, u64 value) noexcept;
    using PrepareResidencyCallback = BackendStatus (*)(void* context, containers::ArraySpan<const ResourceRef> resources) noexcept;
    using CommitResidencyCallback = void (*)(void* context, containers::ArraySpan<const ResourceRef> resources, GpuFence completion) noexcept;
    using ReleaseResidencyCallback = void (*)(void* context, ResourceRef resource) noexcept;

    using SignalFenceCallback = bool (*)(void* context, QueueType queue, u64 value) noexcept;
    using WaitFenceCallback = bool (*)(void* context, QueueType queue, u64 value, u64 timeoutNanoseconds) noexcept;
    using QueueWaitCallback = bool (*)(void* context, QueueType consumer, GpuFence producer) noexcept;
    struct AdmittedCommandResource
    {
        ResourceRef resource;
        ResourceRef residency;
        void* payload = nullptr;
        nvrhi::IResource* native = nullptr;
        u16 textureMipCount = 0;
        u16 textureArraySize = 0;
        u8 texturePlaneCount = 0;
        u32 compiledBarrierEpoch = 0;
    };
    using CompiledBarrierBatchCallback = BackendStatus (*)(void* context, nvrhi::ICommandList* commandList,
                                                            containers::ArraySpan<const CompiledBarrier> barriers,
                                                            containers::ArraySpan<const AdmittedCommandResource> resources) noexcept;
    using AliasingBarrierCallback = bool (*)(void* context, nvrhi::ICommandList* commandList, nvrhi::IResource* resourceAfter) noexcept;
    using RectColorClearCallback = bool (*)(void* context, nvrhi::ICommandList* commandList, nvrhi::ITexture* texture, const ColorValue& value,
                                            const SubresourceRange& range, const Rect& rectangle) noexcept;
    using RectDepthStencilClearCallback = bool (*)(void* context, nvrhi::ICommandList* commandList, nvrhi::ITexture* texture, bool clearDepth, f32 depth,
                                                   bool clearStencil, u8 stencil, const SubresourceRange& range, const Rect& rectangle) noexcept;
    using DiscardTextureCallback = bool (*)(void* context, nvrhi::ICommandList* commandList, nvrhi::ITexture* texture,
                                            const SubresourceRange& range) noexcept;

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4324)
#endif
    class CommonBackend final : public nvrhi::IMessageCallback
    {
    public:
        CommonBackend() noexcept;
        ~CommonBackend();
        CommonBackend(const CommonBackend&) = delete;
        CommonBackend& operator=(const CommonBackend&) = delete;

        [[nodiscard]] bool Initialize(nvrhi::DeviceHandle&& device, FenceCompleteCallback fenceComplete, SignalFenceCallback signalFence,
                                      WaitFenceCallback waitFence, QueueWaitCallback queueWait, CompiledBarrierBatchCallback compiledBarrierBatch,
                                      AliasingBarrierCallback aliasingBarrier, RectColorClearCallback rectColorClear,
                                      RectDepthStencilClearCallback rectDepthStencilClear, DiscardTextureCallback discardTexture,
                                      PrepareResidencyCallback prepareResidency, CommitResidencyCallback commitResidency,
                                      ReleaseResidencyCallback releaseResidency, void* fenceContext) noexcept;
        [[nodiscard]] bool ShutdownAfterGpuIdle() noexcept;
        void ForceShutdownAfterGpuIdle() noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;
        [[nodiscard]] nvrhi::IDevice* GetDevice() const noexcept;
        void PopulateCapabilities(Capabilities& capabilities) const noexcept;

        [[nodiscard]] bool WaitIdle() noexcept;
        void RetireResources() noexcept;

        [[nodiscard]] BackendStatus CreateTexture(const TextureDesc&, const TextureInitData&, TextureRef&) noexcept;
        [[nodiscard]] TextureRef AdoptNativeTexture(nvrhi::TextureHandle&& texture, const TextureDesc& desc) noexcept;
        [[nodiscard]] ResourceRef CreateBackendResource(ResourceKind kind, void* payload, DestroyResourceCallback destroy,
                                                        void* destroyContext = nullptr) noexcept;
        [[nodiscard]] BackendStatus CreateBuffer(const BufferDesc&, const BufferInitData&, BufferRef&) noexcept;
        [[nodiscard]] BackendStatus CreateHeap(const HeapDesc&, HeapRef&) noexcept;
        [[nodiscard]] BindingLayoutRef RequestBindingLayout(const BindingLayoutDesc&) noexcept;
        [[nodiscard]] DescriptorDomainRef CreateDescriptorDomain(const DescriptorDomainDesc&) noexcept;
        [[nodiscard]] DescriptorHandle AllocateDescriptor(DescriptorDomainRef) noexcept;
        [[nodiscard]] BackendStatus WriteDescriptor(DescriptorDomainRef, DescriptorHandle, TextureRef, BindingType, const TextureViewDesc&) noexcept;
        [[nodiscard]] BackendStatus WriteDescriptor(DescriptorDomainRef, DescriptorHandle, BufferRef, BindingType, const BufferViewDesc&) noexcept;
        [[nodiscard]] BackendStatus WriteDescriptor(DescriptorDomainRef, DescriptorHandle, SamplerStateRef) noexcept;
        [[nodiscard]] BackendStatus RetireDescriptor(DescriptorDomainRef, DescriptorHandle, const DescriptorRetirement&) noexcept;
        [[nodiscard]] DescriptorDomainStats GetDescriptorDomainStats(DescriptorDomainRef) const noexcept;
        [[nodiscard]] SamplerStateRef RequestSamplerState(const SamplerStateDesc&) noexcept;
        [[nodiscard]] ShaderRef CreateShader(const ShaderDesc&) noexcept;
        [[nodiscard]] VertexLayoutRef GetVertexLayout(const VertexLayoutDesc&) noexcept;
        [[nodiscard]] PipelineRef CreateGraphicsPipeline(const GraphicsPipelineDesc&) noexcept;
        [[nodiscard]] PipelineRef CreateComputePipeline(const ComputePipelineDesc&) noexcept;
        [[nodiscard]] PipelineRef CreateRayTracingPipeline(const RayTracingPipelineDesc&) noexcept;
        [[nodiscard]] BackendStatus BindMemory(TextureRef, HeapRef, u64) noexcept;
        [[nodiscard]] BackendStatus BindMemory(BufferRef, HeapRef, u64) noexcept;
        [[nodiscard]] BackendStatus GetHeapDesc(HeapRef, HeapDesc&) const noexcept;
        [[nodiscard]] BackendStatus GetPlacement(TextureRef, PlacementRecord&) const noexcept;
        [[nodiscard]] BackendStatus GetPlacement(BufferRef, PlacementRecord&) const noexcept;
        [[nodiscard]] BackendStatus GetTextureDesc(TextureRef, TextureDesc&) const noexcept;
        [[nodiscard]] BackendStatus GetBufferDesc(BufferRef, BufferDesc&) const noexcept;
        [[nodiscard]] BackendStatus GetMemoryRequirements(const TextureDesc&, MemoryRequirements&) const noexcept;
        [[nodiscard]] BackendStatus GetMemoryRequirements(const BufferDesc&, MemoryRequirements&) const noexcept;
        [[nodiscard]] MemoryRequirements GetMemoryRequirements(TextureRef) const noexcept;
        [[nodiscard]] MemoryRequirements GetMemoryRequirements(BufferRef) const noexcept;
        [[nodiscard]] BackendStatus ValidateNativeReleaseObservation(ResourceRef) const noexcept;
        [[nodiscard]] bool IsNativeReleaseComplete(ResourceRef) const noexcept;
        [[nodiscard]] bool IsResourceReferenceValid(ResourceRef) const noexcept;
        [[nodiscard]] void* GetResourcePayload(ResourceRef) noexcept;
        [[nodiscard]] const void* GetResourcePayload(ResourceRef) const noexcept;
        [[nodiscard]] i32 GetResourceReferenceCount(ResourceRef) const noexcept;
        void AddRef(ResourceRef) noexcept;
        [[nodiscard]] bool TryAddRef(ResourceRef) noexcept;
        [[nodiscard]] i32 Release(ResourceRef) noexcept;
        // Swap-chain resize helper. The caller must be the sole owner. Waits only
        // for this texture's recorded queue use before dropping its native handle;
        // the lifetime record itself is still retired through the normal path.
        [[nodiscard]] BackendStatus ReleaseNativeTextureHandleAfterGpuUse(TextureRef) noexcept;
        [[nodiscard]] ResourceLifetimeStats GetResourceLifetimeStats() const noexcept;
        void DrainRetiredResourcesAfterGpuIdle() noexcept;

        [[nodiscard]] CommandListRef CreateCommandList(CommandListType, u64, CommandListRetentionPolicy) noexcept;
        void DiscardCommandList(CommandListRef) noexcept;
        [[nodiscard]] CommandListType GetCommandListType(CommandListRef) const noexcept;
        [[nodiscard]] BackendStatus CloseCommandList(CommandListRef) noexcept;
        [[nodiscard]] BackendStatus SubmitCommandLists(const char*, containers::ArraySpan<const CommandListRef>, CommandListSyncType, SubmissionReceipt&) noexcept;
        [[nodiscard]] BackendStatus ExecuteSerializedQueueOperation(SerializedQueueOperation operation, void* context) noexcept;
        [[nodiscard]] GpuFence GetGpuFence(CommandListRef) const noexcept;
        [[nodiscard]] bool IsGpuFenceComplete(GpuFence) const noexcept;
        [[nodiscard]] BackendStatus WaitForGpuFence(GpuFence, u64) noexcept;
        [[nodiscard]] BackendStatus SetPipeline(CommandListRef, PipelineRef) noexcept;
        [[nodiscard]] BackendStatus SetupRenderTargets(CommandListRef, const RenderTargetSetup&) noexcept;
        [[nodiscard]] BackendStatus SetViewport(CommandListRef, const ViewportDesc&) noexcept;
        [[nodiscard]] BackendStatus SetScissors(CommandListRef, const Rect&) noexcept;
        [[nodiscard]] BackendStatus BindVertexBuffers(CommandListRef, u32, containers::ArraySpan<const VertexBufferBinding>) noexcept;
        [[nodiscard]] BackendStatus BindIndexBuffer(CommandListRef, const IndexBufferBinding&) noexcept;
        [[nodiscard]] BackendStatus BindIndirectArguments(CommandListRef, BufferRef, BufferRef) noexcept;
        [[nodiscard]] BackendStatus SetPushConstants(CommandListRef, const void*, u32) noexcept;
        [[nodiscard]] BackendStatus ClearColorTarget(CommandListRef, TextureRef, const ColorValue&, const SubresourceRange&, const Rect*) noexcept;
        [[nodiscard]] BackendStatus ClearDepthStencilTarget(CommandListRef, TextureRef, bool, f32, bool, u8, const SubresourceRange&, const Rect*) noexcept;
        [[nodiscard]] BackendStatus ClearTextureUav(CommandListRef, TextureRef, const ColorValue&, const SubresourceRange&) noexcept;
        [[nodiscard]] BackendStatus ClearTextureUav(CommandListRef, TextureRef, u32, const SubresourceRange&) noexcept;
        [[nodiscard]] BackendStatus ClearBufferUav(CommandListRef, BufferRef, u32) noexcept;
        [[nodiscard]] BackendStatus DiscardTexture(CommandListRef, TextureRef, const SubresourceRange&) noexcept;
        [[nodiscard]] BackendStatus SetStencilRefValue(CommandListRef, u8) noexcept;
        [[nodiscard]] BackendStatus SetBlendFactor(CommandListRef, const ColorValue&) noexcept;
        [[nodiscard]] BackendStatus BeginGpuEvent(CommandListRef, const char*) noexcept;
        [[nodiscard]] BackendStatus EndGpuEvent(CommandListRef) noexcept;
        [[nodiscard]] BackendStatus SetGpuMarker(CommandListRef, const char*) noexcept;
        [[nodiscard]] BackendStatus DrawPrimitive(CommandListRef, const DrawArguments&) noexcept;
        [[nodiscard]] BackendStatus DrawIndexedPrimitive(CommandListRef, const DrawIndexedArguments&) noexcept;
        [[nodiscard]] BackendStatus DrawPrimitiveIndirect(CommandListRef, u64, u32) noexcept;
        [[nodiscard]] BackendStatus DrawIndexedPrimitiveIndirect(CommandListRef, u64, u32) noexcept;
        [[nodiscard]] BackendStatus DrawIndexedPrimitiveIndirectCount(CommandListRef, u64, u64, u32) noexcept;
        [[nodiscard]] BackendStatus DispatchCompute(CommandListRef, u32, u32, u32) noexcept;
        [[nodiscard]] BackendStatus DispatchIndirectCompute(CommandListRef, u64) noexcept;
        [[nodiscard]] BackendStatus WriteBuffer(CommandListRef, BufferRef, const void*, u64, u64) noexcept;
        [[nodiscard]] BackendStatus WriteTexture(CommandListRef, TextureRef, const TextureSubresourceData&) noexcept;
        [[nodiscard]] BackendStatus CopyBuffer(CommandListRef, BufferRef, u64, BufferRef, u64, u64) noexcept;
        [[nodiscard]] BackendStatus CopyTexture(CommandListRef, TextureRef, TextureRef, const TextureCopyRegion&) noexcept;
        [[nodiscard]] BackendStatus ResolveTexture(CommandListRef, TextureRef, TextureRef, const TextureResolveRegion&) noexcept;
        [[nodiscard]] BackendStatus RequestTextureReadback(CommandListRef, TextureRef, const TextureReadbackRegion&, TextureReadbackRef&) noexcept;
        [[nodiscard]] BackendStatus GetTextureReadbackInfo(TextureReadbackRef, TextureReadbackInfo&) noexcept;
        [[nodiscard]] BackendStatus MapTextureReadback(TextureReadbackRef, TextureReadbackMapping&) noexcept;
        [[nodiscard]] BackendStatus UnmapTextureReadback(TextureReadbackRef) noexcept;
        [[nodiscard]] BackendStatus LockBuffer(BufferRef, u64, u64, void*&) noexcept;
        void UnlockBuffer(BufferRef) noexcept;

        [[nodiscard]] BackendStatus AddCommandListWait(CommandListRef, GpuFence) noexcept;
        [[nodiscard]] BackendStatus AdmitCommandListResources(CommandListRef, containers::ArraySpan<const ResourceRef>) noexcept;
        [[nodiscard]] BackendStatus SeedCommandListStates(CommandListRef, containers::ArraySpan<const CommandListEntryState>) noexcept;
        [[nodiscard]] BackendStatus ExecuteCompiledBarriers(CommandListRef, containers::ArraySpan<const CompiledBarrier>) noexcept;
        [[nodiscard]] BackendStatus TransitionTexture(CommandListRef, TextureRef, ResourceState, ResourceState, const SubresourceRange&) noexcept;
        [[nodiscard]] BackendStatus TransitionBuffer(CommandListRef, BufferRef, ResourceState, ResourceState) noexcept;
        [[nodiscard]] BackendStatus BarrierTextureUav(CommandListRef, TextureRef) noexcept;
        [[nodiscard]] BackendStatus BarrierBufferUav(CommandListRef, BufferRef) noexcept;
        [[nodiscard]] BackendStatus ActivateAliasedResource(CommandListRef, ResourceRef, containers::ArraySpan<const ResourceRef>) noexcept;
        [[nodiscard]] BackendStatus FlushPendingBarriers(CommandListRef) noexcept;
        [[nodiscard]] BackendStatus MakeStateSafeToRetire(CommandListRef, TextureRef) noexcept;
        [[nodiscard]] BackendStatus MakeStateSafeToRetire(CommandListRef, BufferRef) noexcept;
        [[nodiscard]] bool RetainCommandResource(CommandListRef commandList, ResourceRef resource) noexcept;
        [[nodiscard]] bool RegisterCommandSubmissionCallback(CommandListRef commandList, CommandSubmissionCallback callback, void* context, u64 value,
                                                             bool* added = nullptr) noexcept;
        [[nodiscard]] nvrhi::ICommandList* GetNativeCommandList(CommandListRef) noexcept;
        [[nodiscard]] nvrhi::IHeap* GetNativeHeap(HeapRef) noexcept;
        [[nodiscard]] ResourceRef GetResidencyAllocation(ResourceRef) noexcept;
        [[nodiscard]] FenceSet GetResourceLastUse(ResourceRef resource) const noexcept;
        [[nodiscard]] nvrhi::Object GetNativeObject(ResourceRef, nvrhi::ObjectType) noexcept;

        void SetResourceDebugName(TextureRef, const char*) noexcept;
        void SetResourceDebugName(TextureReadbackRef, const char*) noexcept;
        void SetResourceDebugName(BufferRef, const char*) noexcept;
        void SetResourceDebugName(HeapRef, const char*) noexcept;
        void SetResourceDebugName(SamplerStateRef, const char*) noexcept;
        void SetResourceDebugName(ShaderRef, const char*) noexcept;
        void SetResourceDebugName(VertexLayoutRef, const char*) noexcept;
        void SetResourceDebugName(PipelineRef, const char*) noexcept;
        void SetResourceDebugName(CommandListRef, const char*) noexcept;

    private:
        struct CommandListArenaSlot;
        struct CommandListArenaState;

        struct BindingLayoutCacheEntry
        {
            u64 hash = 0;
            BindingLayoutRef layout;
            u32 next = InvalidReferenceIndex;
        };

        struct VertexLayoutCacheEntry
        {
            u64 hash = 0;
            VertexLayoutRef layout;
            u32 next = InvalidReferenceIndex;
        };

        struct RecycledCommandList
        {
            nvrhi::CommandListHandle native;
            GpuFence completion{};
            u64 role = 0;
            u64 lastUsedMaintenanceEpoch = 0;
            u32 previousClass = InvalidReferenceIndex;
            u32 nextClass = InvalidReferenceIndex;
            u32 previousAge = InvalidReferenceIndex;
            u32 nextAge = InvalidReferenceIndex;
            u32 nextFree = InvalidReferenceIndex;
            CommandListType type = CommandListType::None;
            CommandListReuseClass reuseClass = CommandListReuseClass::Clean;
        };

        static constexpr u32 MaximumRetainedCommandListContexts = ResourceLifetimeConfig{}.commandListCapacity;
        static constexpr u32 CommandListPoolHashBucketCount = 4096;
        static constexpr u32 CommandListTypeSlotCount = static_cast<u32>(CommandListType::Compute) + 1u;

        [[nodiscard]] nvrhi::CommandListHandle AcquireNativeCommandList(CommandListType type, u64 role, CommandListRetentionPolicy retentionPolicy,
                                                                       CommandListReuseClass& reuseClass, u64& reuseRole, GpuFence& priorCompletion) noexcept;
        void RecycleNativeCommandList(nvrhi::CommandListHandle&& native, CommandListType type, CommandListReuseClass reuseClass, u64 reuseRole,
                                      GpuFence completion) noexcept;
        void RetireNativeCommandListContext(nvrhi::CommandListHandle&& native, GpuFence completion) noexcept;
        void WaitForGpuFenceCompletionNoAllocation(GpuFence completion) const noexcept;
        void MaintainRecycledCommandLists() noexcept;
        void ClearRecycledCommandLists() noexcept;
        void DrainUnsignaledCommandListRetirementsAfterGpuIdle() noexcept;
        [[nodiscard]] u32 AllocateRecycledCommandListSlotLocked() noexcept;
        void ReleaseRecycledCommandListSlotLocked(u32 index) noexcept;
        void LinkRecycledCommandListLocked(u32 index) noexcept;
        void UnlinkRecycledCommandListLocked(u32 index) noexcept;
        [[nodiscard]] bool InitializeCommandListArena() noexcept;
        void ShutdownCommandListArena() noexcept;
        [[nodiscard]] CommandListRef AcquireCommandListLease(nvrhi::CommandListHandle&& native, CommandListType type, u64 role,
                                                             CommandListRetentionPolicy retentionPolicy, CommandListReuseClass reuseClass,
                                                             u64 reuseRole, GpuFence priorCompletion) noexcept;
        void RetireCommandListLease(CommandListRef commandList, GpuFence completion = {}, bool allowImmediateReuse = false,
                                    bool submittedWithoutCompletion = false) noexcept;
        void CompleteCommandListRetirement(CommandListArenaSlot* slot) noexcept;
        [[nodiscard]] bool HasLiveCommandListLeases() const noexcept;
        [[nodiscard]] bool IsCommandListArenaDrained() const noexcept;
        void ForceDrainCommandListArena() noexcept;
        [[nodiscard]] CommandListArenaSlot* GetCommandListSlot(CommandListRef commandList) noexcept;
        [[nodiscard]] const CommandListArenaSlot* GetCommandListSlot(CommandListRef commandList) const noexcept;
        [[nodiscard]] void* GetCommandListPayload(CommandListRef commandList) noexcept;
        [[nodiscard]] const void* GetCommandListPayload(CommandListRef commandList) const noexcept;

        void message(nvrhi::MessageSeverity severity, const char* text) override;

        nvrhi::DeviceHandle m_device;
        ResourceLifetimeManager m_lifetime;
        CommandListArenaState* m_commandListArena = nullptr;
        concurrency::Atomic<u64> m_submittedFences[3];
        // Guarded by m_submissionLock; excludes failed signal attempts.
        u64 m_signaledFences[3]{};
        u64 m_submittedInstances[3]{};
        FenceCompleteCallback m_fenceComplete = nullptr;
        SignalFenceCallback m_signalFence = nullptr;
        WaitFenceCallback m_waitFence = nullptr;
        QueueWaitCallback m_queueWait = nullptr;
        CompiledBarrierBatchCallback m_compiledBarrierBatch = nullptr;
        AliasingBarrierCallback m_aliasingBarrier = nullptr;
        RectColorClearCallback m_rectColorClear = nullptr;
        RectDepthStencilClearCallback m_rectDepthStencilClear = nullptr;
        DiscardTextureCallback m_discardTexture = nullptr;
        PrepareResidencyCallback m_prepareResidency = nullptr;
        CommitResidencyCallback m_commitResidency = nullptr;
        void* m_fenceContext = nullptr;
        concurrency::SpinLock m_submissionLock;
        concurrency::Atomic<u64> m_nextPlacementGeneration{0};
        concurrency::SpinLock m_bindingLayoutLock;
        containers::DynamicArray<BindingLayoutCacheEntry> m_bindingLayouts;
        u32 m_bindingLayoutBuckets[4096]{};
        concurrency::SpinLock m_vertexLayoutLock;
        containers::DynamicArray<VertexLayoutCacheEntry> m_vertexLayouts;
        u32 m_vertexLayoutBuckets[4096]{};
        concurrency::SpinLock m_descriptorDomainListLock;
        containers::DynamicArray<DescriptorDomainRef> m_descriptorDomains;
        concurrency::SpinLock m_commandListPoolLock;
        RecycledCommandList m_recycledCommandLists[MaximumRetainedCommandListContexts];
        u32 m_cleanCommandListHeads[CommandListTypeSlotCount]{};
        u32 m_boundedCommandListBuckets[CommandListPoolHashBucketCount]{};
        u32 m_commandListPoolFreeHead = InvalidReferenceIndex;
        u32 m_commandListPoolOldest = InvalidReferenceIndex;
        u32 m_commandListPoolNewest = InvalidReferenceIndex;
        u32 m_recycledCommandListCount = 0;
        u64 m_commandListMaintenanceEpoch = 0;
    };
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
} // namespace vanguard::rhi::backend
