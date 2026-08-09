#pragma once

#include <vanguard/containers/containers.hpp>
#include <vanguard/rhi/rhi_types.hpp>

namespace vanguard::rhi
{
    class IBackend
    {
    public:
        virtual ~IBackend() = default;
        IBackend(const IBackend&) = delete;
        IBackend& operator=(const IBackend&) = delete;

        [[nodiscard]] virtual BackendStatus Initialize(const DeviceParams& params, Capabilities& capabilities) noexcept = 0;
        [[nodiscard]] virtual BackendStatus Shutdown() noexcept = 0;
        [[nodiscard]] virtual DeviceState TestDeviceState() noexcept = 0;
        [[nodiscard]] virtual BackendStatus WaitIdle() noexcept = 0;
        // Seals the current retirement epoch and schedules fence-safe native destruction. The frame pipeline calls
        // this once per frame; backends may also call it after submissions that produce independently progressing work.
        [[nodiscard]] virtual BackendStatus RetireResources() noexcept = 0;

        [[nodiscard]] virtual TextureRef CreateTexture(const TextureDesc& desc, const TextureInitData& initialData) noexcept = 0;
        [[nodiscard]] virtual BufferRef CreateBuffer(const BufferDesc& desc, const BufferInitData& initialData) noexcept = 0;
        [[nodiscard]] virtual HeapRef CreateHeap(const HeapDesc& desc) noexcept = 0;
        [[nodiscard]] virtual BindingLayoutRef RequestBindingLayout(const BindingLayoutDesc& desc) noexcept = 0;
        [[nodiscard]] virtual DescriptorDomainRef CreateDescriptorDomain(const DescriptorDomainDesc& desc) noexcept = 0;
        [[nodiscard]] virtual DescriptorHandle AllocateDescriptor(DescriptorDomainRef domain) noexcept = 0;
        [[nodiscard]] virtual BackendStatus WriteDescriptor(DescriptorDomainRef domain, DescriptorHandle descriptor, TextureRef texture,
                                                            BindingType type, const TextureViewDesc& view) noexcept = 0;
        [[nodiscard]] virtual BackendStatus WriteDescriptor(DescriptorDomainRef domain, DescriptorHandle descriptor, BufferRef buffer,
                                                            BindingType type, const BufferViewDesc& view) noexcept = 0;
        [[nodiscard]] virtual BackendStatus WriteDescriptor(DescriptorDomainRef domain, DescriptorHandle descriptor,
                                                            SamplerStateRef sampler) noexcept = 0;
        [[nodiscard]] virtual BackendStatus RetireDescriptor(DescriptorDomainRef domain, DescriptorHandle descriptor,
                                                             const DescriptorRetirement& retirement) noexcept = 0;
        [[nodiscard]] virtual DescriptorDomainStats GetDescriptorDomainStats(DescriptorDomainRef domain) const noexcept = 0;
        [[nodiscard]] virtual SamplerStateRef RequestSamplerState(const SamplerStateDesc& desc) noexcept = 0;
        [[nodiscard]] virtual ShaderRef CreateShader(const ShaderDesc& desc) noexcept = 0;
        [[nodiscard]] virtual VertexLayoutRef GetVertexLayout(const VertexLayoutDesc& desc) noexcept = 0;
        [[nodiscard]] virtual PipelineRef CreateGraphicsPipeline(const GraphicsPipelineDesc& desc) noexcept = 0;
        [[nodiscard]] virtual PipelineRef CreateComputePipeline(const ComputePipelineDesc& desc) noexcept = 0;
        [[nodiscard]] virtual PipelineRef CreateRayTracingPipeline(const RayTracingPipelineDesc& desc) noexcept = 0;
        [[nodiscard]] virtual QueryPoolRef CreateQueryPool(const QueryPoolDesc& desc) noexcept = 0;
        virtual void DestroyQueryPool(QueryPoolRef queryPool) noexcept = 0;
        [[nodiscard]] virtual BackendStatus BindMemory(TextureRef texture, HeapRef heap, u64 offset) noexcept = 0;
        [[nodiscard]] virtual BackendStatus BindMemory(BufferRef buffer, HeapRef heap, u64 offset) noexcept = 0;
        [[nodiscard]] virtual MemoryRequirements GetMemoryRequirements(TextureRef texture) const noexcept = 0;
        [[nodiscard]] virtual MemoryRequirements GetMemoryRequirements(BufferRef buffer) const noexcept = 0;
        [[nodiscard]] virtual bool IsResourceReferenceValid(ResourceRef resource) const noexcept = 0;
        // Reference counts are atomic. When Release reaches zero, the slot becomes stale immediately but native
        // destruction is deferred until every queue fence recorded as using the object has completed.
        virtual void AddRef(ResourceRef resource) noexcept = 0;
        [[nodiscard]] virtual i32 Release(ResourceRef resource) noexcept = 0;
        [[nodiscard]] virtual ResourceLifetimeStats GetResourceLifetimeStats() const noexcept = 0;

        // Vertex layouts are backend-interned non-owning references. Query pools are uniquely owned. Command lists
        // are single-owner consumable references. None participate in the intrusive ResourceRef count.
        [[nodiscard]] virtual CommandListRef CreateCommandList(CommandListType type, u64 debugHash) noexcept = 0;
        virtual void DiscardCommandList(CommandListRef commandList) noexcept = 0;
        [[nodiscard]] virtual CommandListType GetCommandListType(CommandListRef commandList) const noexcept = 0;
        [[nodiscard]] virtual BackendStatus CloseAndSubmitCommandLists(const char* scopeName,
                                                                       containers::ArraySpan<const CommandListRef> commandLists,
                                                                       CommandListSyncType sync, GpuFence& completion) noexcept = 0;
        [[nodiscard]] virtual GpuFence GetGpuFence(CommandListRef commandList) const noexcept = 0;
        [[nodiscard]] virtual bool IsGpuFenceComplete(GpuFence fence) const noexcept = 0;
        [[nodiscard]] virtual BackendStatus WaitForGpuFence(GpuFence fence, u64 timeoutNanoseconds) noexcept = 0;

        [[nodiscard]] virtual BackendStatus SetPipeline(CommandListRef commandList, PipelineRef pipeline) noexcept = 0;
        [[nodiscard]] virtual BackendStatus SetupRenderTargets(CommandListRef commandList, const RenderTargetSetup& setup) noexcept = 0;
        [[nodiscard]] virtual BackendStatus SetViewport(CommandListRef commandList, const ViewportDesc& viewport) noexcept = 0;
        [[nodiscard]] virtual BackendStatus SetScissors(CommandListRef commandList, const Rect& rect) noexcept = 0;
        [[nodiscard]] virtual BackendStatus BindVertexBuffers(CommandListRef commandList, u32 startIndex,
                                                              containers::ArraySpan<const VertexBufferBinding> bindings) noexcept = 0;
        [[nodiscard]] virtual BackendStatus BindIndexBuffer(CommandListRef commandList, const IndexBufferBinding& binding) noexcept = 0;
        [[nodiscard]] virtual BackendStatus BindIndirectArguments(CommandListRef commandList, BufferRef arguments,
                                                                  BufferRef count) noexcept = 0;
        [[nodiscard]] virtual BackendStatus SetPushConstants(CommandListRef commandList, const void* data, u32 size) noexcept = 0;
        [[nodiscard]] virtual BackendStatus DrawPrimitive(CommandListRef commandList, const DrawArguments& arguments) noexcept = 0;
        [[nodiscard]] virtual BackendStatus DrawIndexedPrimitive(CommandListRef commandList,
                                                                 const DrawIndexedArguments& arguments) noexcept = 0;
        [[nodiscard]] virtual BackendStatus DrawPrimitiveIndirect(CommandListRef commandList, u64 argumentsOffset,
                                                                  u32 commandCount) noexcept = 0;
        [[nodiscard]] virtual BackendStatus DrawIndexedPrimitiveIndirect(CommandListRef commandList, u64 argumentsOffset,
                                                                         u32 commandCount) noexcept = 0;
        [[nodiscard]] virtual BackendStatus DrawIndexedPrimitiveIndirectCount(CommandListRef commandList, u64 argumentsOffset,
                                                                              u64 countOffset, u32 maximumCommandCount) noexcept = 0;
        [[nodiscard]] virtual BackendStatus DispatchCompute(CommandListRef commandList, u32 groupCountX, u32 groupCountY,
                                                            u32 groupCountZ) noexcept = 0;
        [[nodiscard]] virtual BackendStatus DispatchIndirectCompute(CommandListRef commandList, u64 argumentsOffset) noexcept = 0;

        [[nodiscard]] virtual BackendStatus WriteBuffer(CommandListRef commandList, BufferRef buffer, const void* data, u64 size,
                                                        u64 destinationOffset) noexcept = 0;
        [[nodiscard]] virtual BackendStatus WriteTexture(CommandListRef commandList, TextureRef texture,
                                                         const TextureSubresourceData& data) noexcept = 0;
        [[nodiscard]] virtual BackendStatus CopyBuffer(CommandListRef commandList, BufferRef destination, u64 destinationOffset,
                                                       BufferRef source, u64 sourceOffset, u64 size) noexcept = 0;
        [[nodiscard]] virtual BackendStatus LockBuffer(BufferRef buffer, u64 offset, u64 size, void*& data) noexcept = 0;
        virtual void UnlockBuffer(BufferRef buffer) noexcept = 0;

        [[nodiscard]] virtual BackendStatus TransitionTexture(CommandListRef commandList, TextureRef texture, ResourceState before,
                                                              ResourceState after, const SubresourceRange& range) noexcept = 0;
        [[nodiscard]] virtual BackendStatus TransitionBuffer(CommandListRef commandList, BufferRef buffer, ResourceState before,
                                                             ResourceState after) noexcept = 0;
        [[nodiscard]] virtual BackendStatus BarrierTextureUav(CommandListRef commandList, TextureRef texture) noexcept = 0;
        [[nodiscard]] virtual BackendStatus BarrierBufferUav(CommandListRef commandList, BufferRef buffer) noexcept = 0;
        [[nodiscard]] virtual BackendStatus BarrierTextureAliasing(CommandListRef commandList, bool discardAfter, TextureRef textureAfter,
                                                                   TextureRef textureBefore) noexcept = 0;
        [[nodiscard]] virtual BackendStatus BarrierBufferAliasing(CommandListRef commandList, bool discardAfter, BufferRef bufferAfter,
                                                                  BufferRef bufferBefore) noexcept = 0;
        [[nodiscard]] virtual BackendStatus FlushPendingBarriers(CommandListRef commandList) noexcept = 0;
        [[nodiscard]] virtual BackendStatus MakeStateSafeToRetire(CommandListRef commandList, TextureRef texture) noexcept = 0;
        [[nodiscard]] virtual BackendStatus MakeStateSafeToRetire(CommandListRef commandList, BufferRef buffer) noexcept = 0;

        [[nodiscard]] virtual SwapChainRef CreateSwapChainWithBackBuffer(const SwapChainDesc& desc) noexcept = 0;
        [[nodiscard]] virtual BackendStatus ResizeBackbuffer(SwapChainRef swapChain, u32 width, u32 height) noexcept = 0;
        [[nodiscard]] virtual TextureRef GetBackBufferTexture(SwapChainRef swapChain) noexcept = 0;
        [[nodiscard]] virtual BackendStatus TransitionSwapChainPresent(CommandListRef commandList, SwapChainRef swapChain) noexcept = 0;
        [[nodiscard]] virtual BackendStatus Present(SwapChainRef swapChain) noexcept = 0;

        virtual void SetResourceDebugName(TextureRef texture, const char* name) noexcept = 0;
        virtual void SetResourceDebugName(BufferRef buffer, const char* name) noexcept = 0;
        virtual void SetResourceDebugName(HeapRef heap, const char* name) noexcept = 0;
        virtual void SetResourceDebugName(SamplerStateRef samplerState, const char* name) noexcept = 0;
        virtual void SetResourceDebugName(ShaderRef shader, const char* name) noexcept = 0;
        virtual void SetResourceDebugName(VertexLayoutRef vertexLayout, const char* name) noexcept = 0;
        virtual void SetResourceDebugName(PipelineRef pipeline, const char* name) noexcept = 0;
        virtual void SetResourceDebugName(QueryPoolRef queryPool, const char* name) noexcept = 0;
        virtual void SetResourceDebugName(CommandListRef commandList, const char* name) noexcept = 0;
        virtual void SetResourceDebugName(SwapChainRef swapChain, const char* name) noexcept = 0;

    protected:
        IBackend() noexcept = default;
    };
} // namespace vanguard::rhi
