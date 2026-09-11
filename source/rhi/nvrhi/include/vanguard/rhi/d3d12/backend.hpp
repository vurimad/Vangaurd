#pragma once

#include <vanguard/rhi/rhi_backend.hpp>

namespace vanguard::rhi::d3d12
{
    [[nodiscard]] BackendFactory GetBackendFactory() noexcept;

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
        [[nodiscard]] BackendStatus AbandonDevice() noexcept override;
        [[nodiscard]] DeviceState TestDeviceState() noexcept override;
        [[nodiscard]] BackendStatus WaitIdle() noexcept override;
        [[nodiscard]] BackendStatus RetireResources() noexcept override;
        [[nodiscard]] BackendStatus FlushRetiredResources() noexcept override;
        [[nodiscard]] BackendStatus QueryMemoryBudget(MemorySegment segment, MemoryBudgetSnapshot& budget) noexcept override;
        [[nodiscard]] BackendStatus SetResidencyPriority(ResourceRef resource, ResidencyPriority priority) noexcept override;
        [[nodiscard]] BackendStatus SetResidencyPinned(ResourceRef resource, bool pinned) noexcept override;
        [[nodiscard]] BackendStatus MakeResident(containers::ArraySpan<const ResourceRef> resources) noexcept override;
        [[nodiscard]] BackendStatus Evict(containers::ArraySpan<const ResourceRef> resources, const ResidencyFenceSet& safeAfter) noexcept override;
        [[nodiscard]] ResidencyStats GetResidencyStats() const noexcept override;

        [[nodiscard]] BackendStatus CreateTexture(const TextureDesc&, const TextureInitData&, TextureRef&) noexcept override;
        [[nodiscard]] BackendStatus CreateBuffer(const BufferDesc&, const BufferInitData&, BufferRef&) noexcept override;
        [[nodiscard]] BackendStatus CreateHeap(const HeapDesc&, HeapRef&) noexcept override;
        [[nodiscard]] BindingLayoutRef RequestBindingLayout(const BindingLayoutDesc&) noexcept override;
        [[nodiscard]] DescriptorDomainRef CreateDescriptorDomain(const DescriptorDomainDesc&) noexcept override;
        [[nodiscard]] DescriptorHandle AllocateDescriptor(DescriptorDomainRef) noexcept override;
        [[nodiscard]] BackendStatus WriteDescriptor(DescriptorDomainRef, DescriptorHandle, TextureRef, BindingType, const TextureViewDesc&) noexcept override;
        [[nodiscard]] BackendStatus WriteDescriptor(DescriptorDomainRef, DescriptorHandle, BufferRef, BindingType, const BufferViewDesc&) noexcept override;
        [[nodiscard]] BackendStatus WriteDescriptor(DescriptorDomainRef, DescriptorHandle, SamplerStateRef) noexcept override;
        [[nodiscard]] BackendStatus WriteDescriptor(DescriptorDomainRef, DescriptorHandle, AccelerationStructureRef) noexcept override;
        [[nodiscard]] BackendStatus RetireDescriptor(DescriptorDomainRef, DescriptorHandle, const DescriptorRetirement&) noexcept override;
        [[nodiscard]] DescriptorDomainStats GetDescriptorDomainStats(DescriptorDomainRef) const noexcept override;
        [[nodiscard]] SamplerStateRef RequestSamplerState(const SamplerStateDesc&) noexcept override;
        [[nodiscard]] ShaderRef CreateShader(const ShaderDesc&) noexcept override;
        [[nodiscard]] VertexLayoutRef GetVertexLayout(const VertexLayoutDesc&) noexcept override;
        [[nodiscard]] PipelineRef CreateGraphicsPipeline(const GraphicsPipelineDesc&) noexcept override;
        [[nodiscard]] PipelineRef CreateComputePipeline(const ComputePipelineDesc&) noexcept override;
        [[nodiscard]] PipelineRef CreateRayTracingPipeline(const RayTracingPipelineDesc&) noexcept override;
        [[nodiscard]] AccelerationStructureRef CreateAccelerationStructure(const AccelerationStructureDesc&) noexcept override;
        [[nodiscard]] ShaderTableRef CreateShaderTable(const ShaderTableDesc&) noexcept override;
        [[nodiscard]] BackendStatus GetAccelerationStructureDeviceAddress(AccelerationStructureRef, u64&) noexcept override;
        [[nodiscard]] QueryPoolRef CreateQueryPool(const QueryPoolDesc&) noexcept override;
        void DestroyQueryPool(QueryPoolRef) noexcept override;
        [[nodiscard]] BackendStatus BeginQuery(CommandListRef, QueryPoolRef, u32) noexcept override;
        [[nodiscard]] BackendStatus EndQuery(CommandListRef, QueryPoolRef, u32) noexcept override;
        [[nodiscard]] BackendStatus IssueQuery(CommandListRef, QueryPoolRef, u32) noexcept override;
        [[nodiscard]] BackendStatus ResolveQueries(CommandListRef, QueryPoolRef, u32, u32) noexcept override;
        [[nodiscard]] BackendStatus AcquireQueries(QueryPoolRef, u32, u32) noexcept override;
        void ReleaseQueries(QueryPoolRef) noexcept override;
        [[nodiscard]] BackendStatus GetQueryResult(QueryPoolRef, u32, u64&) noexcept override;
        [[nodiscard]] BackendStatus GetQueryResult(QueryPoolRef, u32, PipelineStatistics&) noexcept override;
        [[nodiscard]] BackendStatus GetTimestampFrequency(QueueType, u64&) const noexcept override;
        [[nodiscard]] BackendStatus CalibrateTimestamps(QueueType, TimestampCalibration&) const noexcept override;
        [[nodiscard]] BackendStatus BindMemory(TextureRef, HeapRef, u64) noexcept override;
        [[nodiscard]] BackendStatus BindMemory(BufferRef, HeapRef, u64) noexcept override;
        [[nodiscard]] BackendStatus GetHeapDesc(HeapRef, HeapDesc&) const noexcept override;
        [[nodiscard]] BackendStatus GetPlacement(TextureRef, PlacementRecord&) const noexcept override;
        [[nodiscard]] BackendStatus GetPlacement(BufferRef, PlacementRecord&) const noexcept override;
        [[nodiscard]] BackendStatus GetTextureDesc(TextureRef, TextureDesc&) const noexcept override;
        [[nodiscard]] BackendStatus GetBufferDesc(BufferRef, BufferDesc&) const noexcept override;
        [[nodiscard]] BackendStatus GetMemoryRequirements(const TextureDesc&, MemoryRequirements&) const noexcept override;
        [[nodiscard]] BackendStatus GetMemoryRequirements(const BufferDesc&, MemoryRequirements&) const noexcept override;
        [[nodiscard]] MemoryRequirements GetMemoryRequirements(TextureRef) const noexcept override;
        [[nodiscard]] MemoryRequirements GetMemoryRequirements(BufferRef) const noexcept override;
        [[nodiscard]] BackendStatus ObserveNativeRelease(ResourceRef, NativeReleaseObservation&) const noexcept override;
        [[nodiscard]] bool IsNativeReleaseComplete(NativeReleaseObservation) const noexcept override;
        [[nodiscard]] bool IsResourceReferenceValid(ResourceRef) const noexcept override;
        void AddRef(ResourceRef) noexcept override;
        [[nodiscard]] i32 Release(ResourceRef) noexcept override;
        [[nodiscard]] ResourceLifetimeStats GetResourceLifetimeStats() const noexcept override;

        [[nodiscard]] CommandListRef CreateCommandList(CommandListType, u64) noexcept override;
        void DiscardCommandList(CommandListRef) noexcept override;
        [[nodiscard]] CommandListType GetCommandListType(CommandListRef) const noexcept override;
        [[nodiscard]] BackendStatus CloseCommandList(CommandListRef) noexcept override;
        [[nodiscard]] BackendStatus SubmitCommandLists(const char*, containers::ArraySpan<const CommandListRef>, CommandListSyncType, SubmissionReceipt&) noexcept override;
        [[nodiscard]] GpuFence GetGpuFence(CommandListRef) const noexcept override;
        [[nodiscard]] bool IsGpuFenceComplete(GpuFence) const noexcept override;
        [[nodiscard]] BackendStatus WaitForGpuFence(GpuFence, u64) noexcept override;
        [[nodiscard]] BackendStatus AddToResidencyWorkingSet(CommandListRef, ResourceRef) noexcept override;
        [[nodiscard]] BackendStatus SetPipeline(CommandListRef, PipelineRef) noexcept override;
        [[nodiscard]] BackendStatus SetupRenderTargets(CommandListRef, const RenderTargetSetup&) noexcept override;
        [[nodiscard]] BackendStatus SetVariableRateShading(CommandListRef, const VariableRateShadingState&) noexcept override;
        [[nodiscard]] BackendStatus SetViewport(CommandListRef, const ViewportDesc&) noexcept override;
        [[nodiscard]] BackendStatus SetScissors(CommandListRef, const Rect&) noexcept override;
        [[nodiscard]] BackendStatus BindVertexBuffers(CommandListRef, u32, containers::ArraySpan<const VertexBufferBinding>) noexcept override;
        [[nodiscard]] BackendStatus BindIndexBuffer(CommandListRef, const IndexBufferBinding&) noexcept override;
        [[nodiscard]] BackendStatus BindIndirectArguments(CommandListRef, BufferRef, BufferRef) noexcept override;
        [[nodiscard]] BackendStatus SetPushConstants(CommandListRef, const void*, u32) noexcept override;
        [[nodiscard]] BackendStatus ClearColorTarget(CommandListRef, TextureRef, const ColorValue&, const SubresourceRange&, const Rect*) noexcept override;
        [[nodiscard]] BackendStatus ClearDepthStencilTarget(CommandListRef, TextureRef, bool, f32, bool, u8, const SubresourceRange&, const Rect*) noexcept override;
        [[nodiscard]] BackendStatus ClearTextureUav(CommandListRef, TextureRef, const ColorValue&, const SubresourceRange&) noexcept override;
        [[nodiscard]] BackendStatus ClearTextureUav(CommandListRef, TextureRef, u32, const SubresourceRange&) noexcept override;
        [[nodiscard]] BackendStatus ClearBufferUav(CommandListRef, BufferRef, u32) noexcept override;
        [[nodiscard]] BackendStatus DiscardTexture(CommandListRef, TextureRef, const SubresourceRange&) noexcept override;
        [[nodiscard]] BackendStatus SetStencilRefValue(CommandListRef, u8) noexcept override;
        [[nodiscard]] BackendStatus SetBlendFactor(CommandListRef, const ColorValue&) noexcept override;
        [[nodiscard]] BackendStatus BeginGpuEvent(CommandListRef, const char*) noexcept override;
        [[nodiscard]] BackendStatus EndGpuEvent(CommandListRef) noexcept override;
        [[nodiscard]] BackendStatus SetGpuMarker(CommandListRef, const char*) noexcept override;
        [[nodiscard]] BackendStatus DrawPrimitive(CommandListRef, const DrawArguments&) noexcept override;
        [[nodiscard]] BackendStatus DrawIndexedPrimitive(CommandListRef, const DrawIndexedArguments&) noexcept override;
        [[nodiscard]] BackendStatus DrawPrimitiveIndirect(CommandListRef, u64, u32) noexcept override;
        [[nodiscard]] BackendStatus DrawIndexedPrimitiveIndirect(CommandListRef, u64, u32) noexcept override;
        [[nodiscard]] BackendStatus DrawIndexedPrimitiveIndirectCount(CommandListRef, u64, u64, u32) noexcept override;
        [[nodiscard]] BackendStatus DispatchCompute(CommandListRef, u32, u32, u32) noexcept override;
        [[nodiscard]] BackendStatus DispatchIndirectCompute(CommandListRef, u64) noexcept override;
        [[nodiscard]] BackendStatus BuildBottomLevelAccelerationStructure(CommandListRef, AccelerationStructureRef, containers::ArraySpan<const RayTracingGeometryDesc>,
                                                                          AccelerationStructureBuildMode) noexcept override;
        [[nodiscard]] BackendStatus BuildTopLevelAccelerationStructure(CommandListRef, AccelerationStructureRef, containers::ArraySpan<const RayTracingInstanceDesc>,
                                                                       AccelerationStructureBuildMode) noexcept override;
        [[nodiscard]] BackendStatus BuildTopLevelAccelerationStructureIndirect(CommandListRef, AccelerationStructureRef, BufferRef, u64, u32, AccelerationStructureBuildMode) noexcept override;
        [[nodiscard]] BackendStatus CopyAccelerationStructure(CommandListRef, AccelerationStructureRef, AccelerationStructureRef, AccelerationStructureCopyMode) noexcept override;
        [[nodiscard]] BackendStatus WriteAccelerationStructureCompactedSize(CommandListRef, AccelerationStructureRef, QueryPoolRef, u32) noexcept override;
        [[nodiscard]] BackendStatus DispatchRays(CommandListRef, ShaderTableRef, const DispatchRaysArguments&) noexcept override;
        [[nodiscard]] BackendStatus WriteBuffer(CommandListRef, BufferRef, const void*, u64, u64) noexcept override;
        [[nodiscard]] BackendStatus WriteTexture(CommandListRef, TextureRef, const TextureSubresourceData&) noexcept override;
        [[nodiscard]] BackendStatus CopyBuffer(CommandListRef, BufferRef, u64, BufferRef, u64, u64) noexcept override;
        [[nodiscard]] BackendStatus CopyTexture(CommandListRef, TextureRef, TextureRef, const TextureCopyRegion&) noexcept override;
        [[nodiscard]] BackendStatus ResolveTexture(CommandListRef, TextureRef, TextureRef, const TextureResolveRegion&) noexcept override;
        [[nodiscard]] BackendStatus RequestTextureReadback(CommandListRef, TextureRef, const TextureReadbackRegion&, TextureReadbackRef&) noexcept override;
        [[nodiscard]] BackendStatus GetTextureReadbackInfo(TextureReadbackRef, TextureReadbackInfo&) noexcept override;
        [[nodiscard]] BackendStatus MapTextureReadback(TextureReadbackRef, TextureReadbackMapping&) noexcept override;
        [[nodiscard]] BackendStatus UnmapTextureReadback(TextureReadbackRef) noexcept override;
        [[nodiscard]] BackendStatus LockBuffer(BufferRef, u64, u64, void*&) noexcept override;
        void UnlockBuffer(BufferRef) noexcept override;

        [[nodiscard]] BackendStatus AddCommandListWait(CommandListRef, GpuFence) noexcept override;
        [[nodiscard]] BackendStatus SeedCommandListStates(CommandListRef, containers::ArraySpan<const CommandListEntryState>) noexcept override;
        [[nodiscard]] BackendStatus TransitionTexture(CommandListRef, TextureRef, ResourceState, ResourceState, const SubresourceRange&) noexcept override;
        [[nodiscard]] BackendStatus TransitionBuffer(CommandListRef, BufferRef, ResourceState, ResourceState) noexcept override;
        [[nodiscard]] BackendStatus BarrierTextureUav(CommandListRef, TextureRef) noexcept override;
        [[nodiscard]] BackendStatus BarrierBufferUav(CommandListRef, BufferRef) noexcept override;
        [[nodiscard]] BackendStatus ActivateAliasedResource(CommandListRef, ResourceRef, containers::ArraySpan<const ResourceRef>) noexcept override;
        [[nodiscard]] BackendStatus FlushPendingBarriers(CommandListRef) noexcept override;
        [[nodiscard]] BackendStatus MakeStateSafeToRetire(CommandListRef, TextureRef) noexcept override;
        [[nodiscard]] BackendStatus MakeStateSafeToRetire(CommandListRef, BufferRef) noexcept override;

        [[nodiscard]] SwapChainRef CreateSwapChainWithBackBuffer(const SwapChainDesc&) noexcept override;
        [[nodiscard]] BackendStatus ResizeBackbuffer(SwapChainRef, u32, u32) noexcept override;
        [[nodiscard]] BackendStatus SetSwapChainPresentParameters(SwapChainRef, const PresentParameters&) noexcept override;
        [[nodiscard]] BackendStatus AcquireBackBuffer(SwapChainRef, AcquiredBackBuffer&) noexcept override;
        [[nodiscard]] BackendStatus AbandonBackBuffer(const AcquiredBackBuffer&) noexcept override;
        [[nodiscard]] BackendStatus TransitionSwapChainPresent(CommandListRef, const AcquiredBackBuffer&) noexcept override;
        [[nodiscard]] BackendStatus Present(const AcquiredBackBuffer&) noexcept override;
        [[nodiscard]] SwapChainStats GetSwapChainStats(SwapChainRef) noexcept override;

        void SetResourceDebugName(TextureRef, const char*) noexcept override;
        void SetResourceDebugName(TextureReadbackRef, const char*) noexcept override;
        void SetResourceDebugName(BufferRef, const char*) noexcept override;
        void SetResourceDebugName(HeapRef, const char*) noexcept override;
        void SetResourceDebugName(SamplerStateRef, const char*) noexcept override;
        void SetResourceDebugName(ShaderRef, const char*) noexcept override;
        void SetResourceDebugName(VertexLayoutRef, const char*) noexcept override;
        void SetResourceDebugName(PipelineRef, const char*) noexcept override;
        void SetResourceDebugName(AccelerationStructureRef, const char*) noexcept override;
        void SetResourceDebugName(ShaderTableRef, const char*) noexcept override;
        void SetResourceDebugName(QueryPoolRef, const char*) noexcept override;
        void SetResourceDebugName(CommandListRef, const char*) noexcept override;
        void SetResourceDebugName(SwapChainRef, const char*) noexcept override;

    private:
        struct Impl;
        Impl* m_impl = nullptr;
    };
} // namespace vanguard::rhi::d3d12
