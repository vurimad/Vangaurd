#pragma once

#include <vanguard/concurrency/atomic.hpp>
#include <vanguard/concurrency/synchronization.hpp>
#include <vanguard/rhi/backend/resource_lifetime.hpp>
#include <vanguard/rhi/rhi_backend.hpp>
#include <nvrhi/nvrhi.h>

namespace vanguard::rhi::backend
{
    using SignalFenceCallback = bool (*)(void* context, QueueType queue, u64 value) noexcept;
    using WaitFenceCallback = bool (*)(void* context, QueueType queue, u64 value, u64 timeoutNanoseconds) noexcept;
    using AliasingBarrierCallback = bool (*)(void* context, nvrhi::ICommandList* commandList, nvrhi::IResource* resourceAfter,
                                             nvrhi::IResource* resourceBefore, bool discardAfter) noexcept;

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
                                      WaitFenceCallback waitFence, AliasingBarrierCallback aliasingBarrier, void* fenceContext) noexcept;
        [[nodiscard]] bool ShutdownAfterGpuIdle() noexcept;
        void ForceShutdownAfterGpuIdle() noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;
        [[nodiscard]] nvrhi::IDevice* Device() const noexcept;
        void PopulateCapabilities(Capabilities& capabilities) const noexcept;

        [[nodiscard]] bool WaitIdle() noexcept;
        void RetireResources() noexcept;

        [[nodiscard]] TextureRef CreateTexture(const TextureDesc&, const TextureInitData&) noexcept;
        [[nodiscard]] BufferRef CreateBuffer(const BufferDesc&, const BufferInitData&) noexcept;
        [[nodiscard]] HeapRef CreateHeap(const HeapDesc&) noexcept;
        [[nodiscard]] BindingLayoutRef RequestBindingLayout(const BindingLayoutDesc&) noexcept;
        [[nodiscard]] DescriptorDomainRef CreateDescriptorDomain(const DescriptorDomainDesc&) noexcept;
        [[nodiscard]] DescriptorHandle AllocateDescriptor(DescriptorDomainRef) noexcept;
        [[nodiscard]] BackendStatus WriteDescriptor(DescriptorDomainRef, DescriptorHandle, TextureRef, BindingType,
                                                    const TextureViewDesc&) noexcept;
        [[nodiscard]] BackendStatus WriteDescriptor(DescriptorDomainRef, DescriptorHandle, BufferRef, BindingType,
                                                    const BufferViewDesc&) noexcept;
        [[nodiscard]] BackendStatus WriteDescriptor(DescriptorDomainRef, DescriptorHandle, SamplerStateRef) noexcept;
        [[nodiscard]] BackendStatus RetireDescriptor(DescriptorDomainRef, DescriptorHandle, const DescriptorRetirement&) noexcept;
        [[nodiscard]] DescriptorDomainStats GetDescriptorDomainStats(DescriptorDomainRef) const noexcept;
        [[nodiscard]] SamplerStateRef RequestSamplerState(const SamplerStateDesc&) noexcept;
        [[nodiscard]] ShaderRef CreateShader(const ShaderDesc&) noexcept;
        [[nodiscard]] VertexLayoutRef GetVertexLayout(const VertexLayoutDesc&) noexcept;
        [[nodiscard]] PipelineRef CreateGraphicsPipeline(const GraphicsPipelineDesc&) noexcept;
        [[nodiscard]] PipelineRef CreateComputePipeline(const ComputePipelineDesc&) noexcept;
        [[nodiscard]] PipelineRef CreateRayTracingPipeline(const RayTracingPipelineDesc&) noexcept;
        [[nodiscard]] QueryPoolRef CreateQueryPool(const QueryPoolDesc&) noexcept;
        void DestroyQueryPool(QueryPoolRef) noexcept;
        [[nodiscard]] BackendStatus BindMemory(TextureRef, HeapRef, u64) noexcept;
        [[nodiscard]] BackendStatus BindMemory(BufferRef, HeapRef, u64) noexcept;
        [[nodiscard]] MemoryRequirements GetMemoryRequirements(TextureRef) const noexcept;
        [[nodiscard]] MemoryRequirements GetMemoryRequirements(BufferRef) const noexcept;
        [[nodiscard]] bool IsResourceReferenceValid(ResourceRef) const noexcept;
        [[nodiscard]] const void* GetResourcePayload(ResourceRef) const noexcept;
        void AddRef(ResourceRef) noexcept;
        [[nodiscard]] i32 Release(ResourceRef) noexcept;
        [[nodiscard]] ResourceLifetimeStats GetResourceLifetimeStats() const noexcept;

        [[nodiscard]] CommandListRef CreateCommandList(CommandListType, u64) noexcept;
        void DiscardCommandList(CommandListRef) noexcept;
        [[nodiscard]] CommandListType GetCommandListType(CommandListRef) const noexcept;
        [[nodiscard]] BackendStatus CloseAndSubmitCommandLists(const char*, containers::ArraySpan<const CommandListRef>,
                                                               CommandListSyncType, GpuFence&) noexcept;
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
        [[nodiscard]] BackendStatus LockBuffer(BufferRef, u64, u64, void*&) noexcept;
        void UnlockBuffer(BufferRef) noexcept;

        [[nodiscard]] BackendStatus TransitionTexture(CommandListRef, TextureRef, ResourceState, ResourceState,
                                                      const SubresourceRange&) noexcept;
        [[nodiscard]] BackendStatus TransitionBuffer(CommandListRef, BufferRef, ResourceState, ResourceState) noexcept;
        [[nodiscard]] BackendStatus BarrierTextureUav(CommandListRef, TextureRef) noexcept;
        [[nodiscard]] BackendStatus BarrierBufferUav(CommandListRef, BufferRef) noexcept;
        [[nodiscard]] BackendStatus BarrierTextureAliasing(CommandListRef, bool, TextureRef, TextureRef) noexcept;
        [[nodiscard]] BackendStatus BarrierBufferAliasing(CommandListRef, bool, BufferRef, BufferRef) noexcept;
        [[nodiscard]] BackendStatus FlushPendingBarriers(CommandListRef) noexcept;
        [[nodiscard]] BackendStatus MakeStateSafeToRetire(CommandListRef, TextureRef) noexcept;
        [[nodiscard]] BackendStatus MakeStateSafeToRetire(CommandListRef, BufferRef) noexcept;

        void SetResourceDebugName(TextureRef, const char*) noexcept;
        void SetResourceDebugName(BufferRef, const char*) noexcept;
        void SetResourceDebugName(HeapRef, const char*) noexcept;
        void SetResourceDebugName(SamplerStateRef, const char*) noexcept;
        void SetResourceDebugName(ShaderRef, const char*) noexcept;
        void SetResourceDebugName(VertexLayoutRef, const char*) noexcept;
        void SetResourceDebugName(PipelineRef, const char*) noexcept;
        void SetResourceDebugName(QueryPoolRef, const char*) noexcept;
        void SetResourceDebugName(CommandListRef, const char*) noexcept;

    private:
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

        void message(nvrhi::MessageSeverity severity, const char* text) override;

        nvrhi::DeviceHandle m_device;
        ResourceLifetimeManager m_lifetime;
        concurrency::Atomic<u64> m_submittedFences[3];
        u64 m_submittedInstances[3]{};
        FenceCompleteCallback m_fenceComplete = nullptr;
        SignalFenceCallback m_signalFence = nullptr;
        WaitFenceCallback m_waitFence = nullptr;
        AliasingBarrierCallback m_aliasingBarrier = nullptr;
        void* m_fenceContext = nullptr;
        concurrency::SpinLock m_submissionLock;
        concurrency::SpinLock m_bindingLayoutLock;
        containers::DynamicArray<BindingLayoutCacheEntry> m_bindingLayouts;
        u32 m_bindingLayoutBuckets[4096]{};
        concurrency::SpinLock m_vertexLayoutLock;
        containers::DynamicArray<VertexLayoutCacheEntry> m_vertexLayouts;
        u32 m_vertexLayoutBuckets[4096]{};
        concurrency::SpinLock m_descriptorDomainListLock;
        containers::DynamicArray<DescriptorDomainRef> m_descriptorDomains;
    };
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
} // namespace vanguard::rhi::backend
