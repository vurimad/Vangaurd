#pragma once

#include <vanguard/containers/containers.hpp>
#include <vanguard/rhi/rhi_types.hpp>

namespace vanguard::rhi
{
    class IBackend;

    struct BackendFactory
    {
        using Create = IBackend* (*)() noexcept;
        using Destroy = void (*)(IBackend* backend) noexcept;

        Create create = nullptr;
        Destroy destroy = nullptr;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return create != nullptr && destroy != nullptr;
        }
    };

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
        [[nodiscard]] virtual BackendStatus FlushRetiredResources() noexcept = 0;
        [[nodiscard]] virtual BackendStatus QueryMemoryBudget(MemorySegment segment, MemoryBudgetSnapshot& budget) noexcept = 0;
        [[nodiscard]] virtual BackendStatus SetResidencyPriority(ResourceRef resource, ResidencyPriority priority) noexcept = 0;
        [[nodiscard]] virtual BackendStatus SetResidencyPinned(ResourceRef resource, bool pinned) noexcept = 0;
        [[nodiscard]] virtual BackendStatus MakeResident(containers::ArraySpan<const ResourceRef> resources) noexcept = 0;
        [[nodiscard]] virtual BackendStatus Evict(containers::ArraySpan<const ResourceRef> resources, const ResidencyFenceSet& safeAfter) noexcept = 0;
        [[nodiscard]] virtual ResidencyStats GetResidencyStats() const noexcept = 0;

        [[nodiscard]] virtual TextureRef CreateTexture(const TextureDesc& desc, const TextureInitData& initialData) noexcept = 0;
        [[nodiscard]] virtual BufferRef CreateBuffer(const BufferDesc& desc, const BufferInitData& initialData) noexcept = 0;
        [[nodiscard]] virtual HeapRef CreateHeap(const HeapDesc& desc) noexcept = 0;
        [[nodiscard]] virtual BindingLayoutRef RequestBindingLayout(const BindingLayoutDesc& desc) noexcept = 0;
        [[nodiscard]] virtual DescriptorDomainRef CreateDescriptorDomain(const DescriptorDomainDesc& desc) noexcept = 0;
        [[nodiscard]] virtual DescriptorHandle AllocateDescriptor(DescriptorDomainRef domain) noexcept = 0;
        [[nodiscard]] virtual BackendStatus WriteDescriptor(DescriptorDomainRef domain, DescriptorHandle descriptor, TextureRef texture, BindingType type,
                                                            const TextureViewDesc& view) noexcept = 0;
        [[nodiscard]] virtual BackendStatus WriteDescriptor(DescriptorDomainRef domain, DescriptorHandle descriptor, BufferRef buffer, BindingType type,
                                                            const BufferViewDesc& view) noexcept = 0;
        [[nodiscard]] virtual BackendStatus WriteDescriptor(DescriptorDomainRef domain, DescriptorHandle descriptor, SamplerStateRef sampler) noexcept = 0;
        [[nodiscard]] virtual BackendStatus WriteDescriptor(DescriptorDomainRef domain, DescriptorHandle descriptor,
                                                            AccelerationStructureRef accelerationStructure) noexcept = 0;
        [[nodiscard]] virtual BackendStatus RetireDescriptor(DescriptorDomainRef domain, DescriptorHandle descriptor,
                                                             const DescriptorRetirement& retirement) noexcept = 0;
        [[nodiscard]] virtual DescriptorDomainStats GetDescriptorDomainStats(DescriptorDomainRef domain) const noexcept = 0;
        [[nodiscard]] virtual SamplerStateRef RequestSamplerState(const SamplerStateDesc& desc) noexcept = 0;
        [[nodiscard]] virtual ShaderRef CreateShader(const ShaderDesc& desc) noexcept = 0;
        [[nodiscard]] virtual VertexLayoutRef GetVertexLayout(const VertexLayoutDesc& desc) noexcept = 0;
        [[nodiscard]] virtual PipelineRef CreateGraphicsPipeline(const GraphicsPipelineDesc& desc) noexcept = 0;
        [[nodiscard]] virtual PipelineRef CreateComputePipeline(const ComputePipelineDesc& desc) noexcept = 0;
        [[nodiscard]] virtual PipelineRef CreateRayTracingPipeline(const RayTracingPipelineDesc& desc) noexcept = 0;
        [[nodiscard]] virtual AccelerationStructureRef CreateAccelerationStructure(const AccelerationStructureDesc& desc) noexcept = 0;
        [[nodiscard]] virtual ShaderTableRef CreateShaderTable(const ShaderTableDesc& desc) noexcept = 0;
        [[nodiscard]] virtual BackendStatus GetAccelerationStructureDeviceAddress(AccelerationStructureRef accelerationStructure, u64& address) noexcept = 0;
        [[nodiscard]] virtual QueryPoolRef CreateQueryPool(const QueryPoolDesc& desc) noexcept = 0;
        virtual void DestroyQueryPool(QueryPoolRef queryPool) noexcept = 0;
        [[nodiscard]] virtual BackendStatus BeginQuery(CommandListRef commandList, QueryPoolRef queryPool, u32 index) noexcept = 0;
        [[nodiscard]] virtual BackendStatus EndQuery(CommandListRef commandList, QueryPoolRef queryPool, u32 index) noexcept = 0;
        [[nodiscard]] virtual BackendStatus IssueQuery(CommandListRef commandList, QueryPoolRef queryPool, u32 index) noexcept = 0;
        [[nodiscard]] virtual BackendStatus ResolveQueries(CommandListRef commandList, QueryPoolRef queryPool, u32 start, u32 count) noexcept = 0;
        [[nodiscard]] virtual BackendStatus AcquireQueries(QueryPoolRef queryPool, u32 start, u32 count) noexcept = 0;
        virtual void ReleaseQueries(QueryPoolRef queryPool) noexcept = 0;
        [[nodiscard]] virtual BackendStatus GetQueryResult(QueryPoolRef queryPool, u32 index, u64& result) noexcept = 0;
        [[nodiscard]] virtual BackendStatus GetQueryResult(QueryPoolRef queryPool, u32 index, PipelineStatistics& result) noexcept = 0;
        [[nodiscard]] virtual BackendStatus GetTimestampFrequency(QueueType queue, u64& frequency) const noexcept = 0;
        [[nodiscard]] virtual BackendStatus CalibrateTimestamps(QueueType queue, TimestampCalibration& calibration) const noexcept = 0;
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
        [[nodiscard]] virtual BackendStatus CloseAndSubmitCommandLists(const char* scopeName, containers::ArraySpan<const CommandListRef> commandLists,
                                                                       CommandListSyncType sync, GpuFence& completion) noexcept = 0;
        [[nodiscard]] virtual GpuFence GetGpuFence(CommandListRef commandList) const noexcept = 0;
        [[nodiscard]] virtual bool IsGpuFenceComplete(GpuFence fence) const noexcept = 0;
        [[nodiscard]] virtual BackendStatus WaitForGpuFence(GpuFence fence, u64 timeoutNanoseconds) noexcept = 0;
        [[nodiscard]] virtual BackendStatus AddToResidencyWorkingSet(CommandListRef commandList, ResourceRef resource) noexcept = 0;

        [[nodiscard]] virtual BackendStatus SetPipeline(CommandListRef commandList, PipelineRef pipeline) noexcept = 0;
        [[nodiscard]] virtual BackendStatus SetupRenderTargets(CommandListRef commandList, const RenderTargetSetup& setup) noexcept = 0;
        [[nodiscard]] virtual BackendStatus SetVariableRateShading(CommandListRef commandList, const VariableRateShadingState& state) noexcept = 0;
        [[nodiscard]] virtual BackendStatus SetViewport(CommandListRef commandList, const ViewportDesc& viewport) noexcept = 0;
        [[nodiscard]] virtual BackendStatus SetScissors(CommandListRef commandList, const Rect& rect) noexcept = 0;
        [[nodiscard]] virtual BackendStatus BindVertexBuffers(CommandListRef commandList, u32 startIndex,
                                                              containers::ArraySpan<const VertexBufferBinding> bindings) noexcept = 0;
        [[nodiscard]] virtual BackendStatus BindIndexBuffer(CommandListRef commandList, const IndexBufferBinding& binding) noexcept = 0;
        [[nodiscard]] virtual BackendStatus BindIndirectArguments(CommandListRef commandList, BufferRef arguments, BufferRef count) noexcept = 0;
        [[nodiscard]] virtual BackendStatus SetPushConstants(CommandListRef commandList, const void* data, u32 size) noexcept = 0;
        [[nodiscard]] virtual BackendStatus ClearColorTarget(CommandListRef commandList, TextureRef target, const ColorValue& value,
                                                             const SubresourceRange& range, const Rect* rectangle) noexcept = 0;
        [[nodiscard]] virtual BackendStatus ClearDepthStencilTarget(CommandListRef commandList, TextureRef target, bool clearDepth, f32 depth,
                                                                    bool clearStencil, u8 stencil, const SubresourceRange& range,
                                                                    const Rect* rectangle) noexcept = 0;
        [[nodiscard]] virtual BackendStatus ClearTextureUav(CommandListRef commandList, TextureRef texture, const ColorValue& value,
                                                            const SubresourceRange& range) noexcept = 0;
        [[nodiscard]] virtual BackendStatus ClearTextureUav(CommandListRef commandList, TextureRef texture, u32 value,
                                                            const SubresourceRange& range) noexcept = 0;
        [[nodiscard]] virtual BackendStatus ClearBufferUav(CommandListRef commandList, BufferRef buffer, u32 value) noexcept = 0;
        [[nodiscard]] virtual BackendStatus DiscardTexture(CommandListRef commandList, TextureRef texture, const SubresourceRange& range) noexcept = 0;
        [[nodiscard]] virtual BackendStatus DiscardBuffer(CommandListRef commandList, BufferRef buffer) noexcept = 0;
        [[nodiscard]] virtual BackendStatus SetStencilRefValue(CommandListRef commandList, u8 value) noexcept = 0;
        [[nodiscard]] virtual BackendStatus SetBlendFactor(CommandListRef commandList, const ColorValue& value) noexcept = 0;
        [[nodiscard]] virtual BackendStatus BeginGpuEvent(CommandListRef commandList, const char* name) noexcept = 0;
        [[nodiscard]] virtual BackendStatus EndGpuEvent(CommandListRef commandList) noexcept = 0;
        [[nodiscard]] virtual BackendStatus SetGpuMarker(CommandListRef commandList, const char* name) noexcept = 0;
        [[nodiscard]] virtual BackendStatus DrawPrimitive(CommandListRef commandList, const DrawArguments& arguments) noexcept = 0;
        [[nodiscard]] virtual BackendStatus DrawIndexedPrimitive(CommandListRef commandList, const DrawIndexedArguments& arguments) noexcept = 0;
        [[nodiscard]] virtual BackendStatus DrawPrimitiveIndirect(CommandListRef commandList, u64 argumentsOffset, u32 commandCount) noexcept = 0;
        [[nodiscard]] virtual BackendStatus DrawIndexedPrimitiveIndirect(CommandListRef commandList, u64 argumentsOffset, u32 commandCount) noexcept = 0;
        [[nodiscard]] virtual BackendStatus DrawIndexedPrimitiveIndirectCount(CommandListRef commandList, u64 argumentsOffset, u64 countOffset,
                                                                              u32 maximumCommandCount) noexcept = 0;
        [[nodiscard]] virtual BackendStatus DispatchCompute(CommandListRef commandList, u32 groupCountX, u32 groupCountY, u32 groupCountZ) noexcept = 0;
        [[nodiscard]] virtual BackendStatus DispatchIndirectCompute(CommandListRef commandList, u64 argumentsOffset) noexcept = 0;
        [[nodiscard]] virtual BackendStatus BuildBottomLevelAccelerationStructure(CommandListRef commandList, AccelerationStructureRef destination,
                                                                                  containers::ArraySpan<const RayTracingGeometryDesc> geometries,
                                                                                  AccelerationStructureBuildMode mode) noexcept = 0;
        [[nodiscard]] virtual BackendStatus BuildTopLevelAccelerationStructure(CommandListRef commandList, AccelerationStructureRef destination,
                                                                               containers::ArraySpan<const RayTracingInstanceDesc> instances,
                                                                               AccelerationStructureBuildMode mode) noexcept = 0;
        [[nodiscard]] virtual BackendStatus BuildTopLevelAccelerationStructureIndirect(CommandListRef commandList, AccelerationStructureRef destination,
                                                                                       BufferRef instanceBuffer, u64 offset, u32 instanceCount,
                                                                                       AccelerationStructureBuildMode mode) noexcept = 0;
        [[nodiscard]] virtual BackendStatus CopyAccelerationStructure(CommandListRef commandList, AccelerationStructureRef destination,
                                                                      AccelerationStructureRef source, AccelerationStructureCopyMode mode) noexcept = 0;
        [[nodiscard]] virtual BackendStatus WriteAccelerationStructureCompactedSize(CommandListRef commandList, AccelerationStructureRef accelerationStructure,
                                                                                    QueryPoolRef queryPool, u32 queryIndex) noexcept = 0;
        [[nodiscard]] virtual BackendStatus DispatchRays(CommandListRef commandList, ShaderTableRef shaderTable,
                                                         const DispatchRaysArguments& arguments) noexcept = 0;

        [[nodiscard]] virtual BackendStatus WriteBuffer(CommandListRef commandList, BufferRef buffer, const void* data, u64 size,
                                                        u64 destinationOffset) noexcept = 0;
        [[nodiscard]] virtual BackendStatus WriteTexture(CommandListRef commandList, TextureRef texture, const TextureSubresourceData& data) noexcept = 0;
        [[nodiscard]] virtual BackendStatus CopyBuffer(CommandListRef commandList, BufferRef destination, u64 destinationOffset, BufferRef source,
                                                       u64 sourceOffset, u64 size) noexcept = 0;
        [[nodiscard]] virtual BackendStatus CopyTexture(CommandListRef commandList, TextureRef destination, TextureRef source,
                                                        const TextureCopyRegion& region) noexcept = 0;
        [[nodiscard]] virtual BackendStatus ResolveTexture(CommandListRef commandList, TextureRef destination, TextureRef source,
                                                           const TextureResolveRegion& region) noexcept = 0;
        [[nodiscard]] virtual BackendStatus RequestTextureReadback(CommandListRef commandList, TextureRef source, const TextureReadbackRegion& region,
                                                                   TextureReadbackRef& readback) noexcept = 0;
        [[nodiscard]] virtual BackendStatus GetTextureReadbackInfo(TextureReadbackRef readback, TextureReadbackInfo& info) noexcept = 0;
        [[nodiscard]] virtual BackendStatus MapTextureReadback(TextureReadbackRef readback, TextureReadbackMapping& mapping) noexcept = 0;
        [[nodiscard]] virtual BackendStatus UnmapTextureReadback(TextureReadbackRef readback) noexcept = 0;
        [[nodiscard]] virtual BackendStatus LockBuffer(BufferRef buffer, u64 offset, u64 size, void*& data) noexcept = 0;
        virtual void UnlockBuffer(BufferRef buffer) noexcept = 0;

        [[nodiscard]] virtual BackendStatus TransitionTexture(CommandListRef commandList, TextureRef texture, ResourceState before, ResourceState after,
                                                              const SubresourceRange& range) noexcept = 0;
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
        [[nodiscard]] virtual BackendStatus SetSwapChainPresentParameters(SwapChainRef swapChain, const PresentParameters& parameters) noexcept = 0;
        [[nodiscard]] virtual BackendStatus AcquireBackBuffer(SwapChainRef swapChain, AcquiredBackBuffer& acquisition) noexcept = 0;
        [[nodiscard]] virtual BackendStatus AbandonBackBuffer(const AcquiredBackBuffer& acquisition) noexcept = 0;
        [[nodiscard]] virtual BackendStatus TransitionSwapChainPresent(CommandListRef commandList, const AcquiredBackBuffer& acquisition) noexcept = 0;
        [[nodiscard]] virtual BackendStatus Present(const AcquiredBackBuffer& acquisition) noexcept = 0;
        [[nodiscard]] virtual SwapChainStats GetSwapChainStats(SwapChainRef swapChain) noexcept = 0;

        virtual void SetResourceDebugName(TextureRef texture, const char* name) noexcept = 0;
        virtual void SetResourceDebugName(TextureReadbackRef readback, const char* name) noexcept = 0;
        virtual void SetResourceDebugName(BufferRef buffer, const char* name) noexcept = 0;
        virtual void SetResourceDebugName(HeapRef heap, const char* name) noexcept = 0;
        virtual void SetResourceDebugName(SamplerStateRef samplerState, const char* name) noexcept = 0;
        virtual void SetResourceDebugName(ShaderRef shader, const char* name) noexcept = 0;
        virtual void SetResourceDebugName(VertexLayoutRef vertexLayout, const char* name) noexcept = 0;
        virtual void SetResourceDebugName(PipelineRef pipeline, const char* name) noexcept = 0;
        virtual void SetResourceDebugName(AccelerationStructureRef accelerationStructure, const char* name) noexcept = 0;
        virtual void SetResourceDebugName(ShaderTableRef shaderTable, const char* name) noexcept = 0;
        virtual void SetResourceDebugName(QueryPoolRef queryPool, const char* name) noexcept = 0;
        virtual void SetResourceDebugName(CommandListRef commandList, const char* name) noexcept = 0;
        virtual void SetResourceDebugName(SwapChainRef swapChain, const char* name) noexcept = 0;

    protected:
        IBackend() noexcept = default;
    };
} // namespace vanguard::rhi
