#pragma once

#include <vanguard/rhi/rhi_backend.hpp>

namespace vanguard::rhi::d3d12
{
    // D3D12 implementation of Vanguard's GpuApi-shaped contract. Its implementation
    // dependency is intentionally opaque; no NVRHI or native API type crosses this header.
    class Backend final : public IBackend
    {
    public:
        Backend() noexcept = default;
        ~Backend() override;
        Backend(const Backend&) = delete;
        Backend& operator=(const Backend&) = delete;

        [[nodiscard]] BackendStatus Initialize(const DeviceParams& params, Capabilities& capabilities) noexcept override;
        [[nodiscard]] BackendStatus Shutdown() noexcept override;
        [[nodiscard]] DeviceState TestDeviceState() noexcept override;
        [[nodiscard]] BackendStatus WaitIdle() noexcept override;
        [[nodiscard]] BackendStatus RetireResources() noexcept override;

        [[nodiscard]] TextureRef CreateTexture(const TextureDesc&, const TextureInitData&) noexcept override;
        [[nodiscard]] BufferRef CreateBuffer(const BufferDesc&, const BufferInitData&) noexcept override;
        [[nodiscard]] HeapRef CreateHeap(const HeapDesc&) noexcept override;
        [[nodiscard]] BindingLayoutRef RequestBindingLayout(const BindingLayoutDesc&) noexcept override;
        [[nodiscard]] DescriptorDomainRef CreateDescriptorDomain(const DescriptorDomainDesc&) noexcept override;
        [[nodiscard]] DescriptorHandle AllocateDescriptor(DescriptorDomainRef) noexcept override;
        [[nodiscard]] BackendStatus WriteDescriptor(DescriptorDomainRef, DescriptorHandle, TextureRef, BindingType,
                                                    const TextureViewDesc&) noexcept override;
        [[nodiscard]] BackendStatus WriteDescriptor(DescriptorDomainRef, DescriptorHandle, BufferRef, BindingType,
                                                    const BufferViewDesc&) noexcept override;
        [[nodiscard]] BackendStatus WriteDescriptor(DescriptorDomainRef, DescriptorHandle, SamplerStateRef) noexcept override;
        [[nodiscard]] BackendStatus RetireDescriptor(DescriptorDomainRef, DescriptorHandle, const DescriptorRetirement&) noexcept override;
        [[nodiscard]] DescriptorDomainStats GetDescriptorDomainStats(DescriptorDomainRef) const noexcept override;
        [[nodiscard]] SamplerStateRef RequestSamplerState(const SamplerStateDesc&) noexcept override;
        [[nodiscard]] ShaderRef CreateShader(const ShaderDesc&) noexcept override;
        [[nodiscard]] VertexLayoutRef GetVertexLayout(const VertexLayoutDesc&) noexcept override;
        [[nodiscard]] PipelineRef CreateGraphicsPipeline(const GraphicsPipelineDesc&) noexcept override;
        [[nodiscard]] PipelineRef CreateComputePipeline(const ComputePipelineDesc&) noexcept override;
        [[nodiscard]] PipelineRef CreateRayTracingPipeline(const RayTracingPipelineDesc&) noexcept override;
        [[nodiscard]] QueryPoolRef CreateQueryPool(const QueryPoolDesc&) noexcept override;
        void DestroyQueryPool(QueryPoolRef) noexcept override;
        [[nodiscard]] BackendStatus BindMemory(TextureRef, HeapRef, u64) noexcept override;
        [[nodiscard]] BackendStatus BindMemory(BufferRef, HeapRef, u64) noexcept override;
        [[nodiscard]] MemoryRequirements GetMemoryRequirements(TextureRef) const noexcept override;
        [[nodiscard]] MemoryRequirements GetMemoryRequirements(BufferRef) const noexcept override;
        [[nodiscard]] bool IsResourceReferenceValid(ResourceRef) const noexcept override;
        void AddRef(ResourceRef) noexcept override;
        [[nodiscard]] i32 Release(ResourceRef) noexcept override;
        [[nodiscard]] ResourceLifetimeStats GetResourceLifetimeStats() const noexcept override;

        [[nodiscard]] CommandListRef CreateCommandList(CommandListType, u64) noexcept override;
        void DiscardCommandList(CommandListRef) noexcept override;
        [[nodiscard]] CommandListType GetCommandListType(CommandListRef) const noexcept override;
        [[nodiscard]] BackendStatus CloseAndSubmitCommandLists(const char*, containers::ArraySpan<const CommandListRef>,
                                                               CommandListSyncType, GpuFence&) noexcept override;
        [[nodiscard]] GpuFence GetGpuFence(CommandListRef) const noexcept override;
        [[nodiscard]] bool IsGpuFenceComplete(GpuFence) const noexcept override;
        [[nodiscard]] BackendStatus WaitForGpuFence(GpuFence, u64) noexcept override;
        [[nodiscard]] BackendStatus SetPipeline(CommandListRef, PipelineRef) noexcept override;
        [[nodiscard]] BackendStatus SetupRenderTargets(CommandListRef, const RenderTargetSetup&) noexcept override;
        [[nodiscard]] BackendStatus SetViewport(CommandListRef, const ViewportDesc&) noexcept override;
        [[nodiscard]] BackendStatus SetScissors(CommandListRef, const Rect&) noexcept override;
        [[nodiscard]] BackendStatus BindVertexBuffers(CommandListRef, u32,
                                                      containers::ArraySpan<const VertexBufferBinding>) noexcept override;
        [[nodiscard]] BackendStatus BindIndexBuffer(CommandListRef, const IndexBufferBinding&) noexcept override;
        [[nodiscard]] BackendStatus BindIndirectArguments(CommandListRef, BufferRef, BufferRef) noexcept override;
        [[nodiscard]] BackendStatus SetPushConstants(CommandListRef, const void*, u32) noexcept override;
        [[nodiscard]] BackendStatus DrawPrimitive(CommandListRef, const DrawArguments&) noexcept override;
        [[nodiscard]] BackendStatus DrawIndexedPrimitive(CommandListRef, const DrawIndexedArguments&) noexcept override;
        [[nodiscard]] BackendStatus DrawPrimitiveIndirect(CommandListRef, u64, u32) noexcept override;
        [[nodiscard]] BackendStatus DrawIndexedPrimitiveIndirect(CommandListRef, u64, u32) noexcept override;
        [[nodiscard]] BackendStatus DrawIndexedPrimitiveIndirectCount(CommandListRef, u64, u64, u32) noexcept override;
        [[nodiscard]] BackendStatus DispatchCompute(CommandListRef, u32, u32, u32) noexcept override;
        [[nodiscard]] BackendStatus DispatchIndirectCompute(CommandListRef, u64) noexcept override;
        [[nodiscard]] BackendStatus WriteBuffer(CommandListRef, BufferRef, const void*, u64, u64) noexcept override;
        [[nodiscard]] BackendStatus WriteTexture(CommandListRef, TextureRef, const TextureSubresourceData&) noexcept override;
        [[nodiscard]] BackendStatus CopyBuffer(CommandListRef, BufferRef, u64, BufferRef, u64, u64) noexcept override;
        [[nodiscard]] BackendStatus LockBuffer(BufferRef, u64, u64, void*&) noexcept override;
        void UnlockBuffer(BufferRef) noexcept override;

        [[nodiscard]] BackendStatus TransitionTexture(CommandListRef, TextureRef, ResourceState, ResourceState,
                                                      const SubresourceRange&) noexcept override;
        [[nodiscard]] BackendStatus TransitionBuffer(CommandListRef, BufferRef, ResourceState, ResourceState) noexcept override;
        [[nodiscard]] BackendStatus BarrierTextureUav(CommandListRef, TextureRef) noexcept override;
        [[nodiscard]] BackendStatus BarrierBufferUav(CommandListRef, BufferRef) noexcept override;
        [[nodiscard]] BackendStatus BarrierTextureAliasing(CommandListRef, bool, TextureRef, TextureRef) noexcept override;
        [[nodiscard]] BackendStatus BarrierBufferAliasing(CommandListRef, bool, BufferRef, BufferRef) noexcept override;
        [[nodiscard]] BackendStatus FlushPendingBarriers(CommandListRef) noexcept override;
        [[nodiscard]] BackendStatus MakeStateSafeToRetire(CommandListRef, TextureRef) noexcept override;
        [[nodiscard]] BackendStatus MakeStateSafeToRetire(CommandListRef, BufferRef) noexcept override;

        [[nodiscard]] SwapChainRef CreateSwapChainWithBackBuffer(const SwapChainDesc&) noexcept override;
        [[nodiscard]] BackendStatus ResizeBackbuffer(SwapChainRef, u32, u32) noexcept override;
        [[nodiscard]] TextureRef GetBackBufferTexture(SwapChainRef) noexcept override;
        [[nodiscard]] BackendStatus TransitionSwapChainPresent(CommandListRef, SwapChainRef) noexcept override;
        [[nodiscard]] BackendStatus Present(SwapChainRef) noexcept override;

        void SetResourceDebugName(TextureRef, const char*) noexcept override;
        void SetResourceDebugName(BufferRef, const char*) noexcept override;
        void SetResourceDebugName(HeapRef, const char*) noexcept override;
        void SetResourceDebugName(SamplerStateRef, const char*) noexcept override;
        void SetResourceDebugName(ShaderRef, const char*) noexcept override;
        void SetResourceDebugName(VertexLayoutRef, const char*) noexcept override;
        void SetResourceDebugName(PipelineRef, const char*) noexcept override;
        void SetResourceDebugName(QueryPoolRef, const char*) noexcept override;
        void SetResourceDebugName(CommandListRef, const char*) noexcept override;
        void SetResourceDebugName(SwapChainRef, const char*) noexcept override;

    private:
        struct Impl;
        Impl* m_impl = nullptr;
    };
} // namespace vanguard::rhi::d3d12
