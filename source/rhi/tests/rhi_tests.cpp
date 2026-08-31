#include <vanguard/rhi/rhi.hpp>
#include <vanguard/rhi/gpu_counters.hpp>
#include <vanguard/memory/memory.hpp>

#include <cstdio>

namespace
{
    namespace gpu = vanguard::rhi;
    vanguard::u32 g_failures = 0;

    void Check(const bool condition, const char* const message) noexcept
    {
        if (condition)
            return;
        std::printf("[rhiTests] FAILED: %s\n", message);
        ++g_failures;
    }

    class FakeBackend final : public gpu::IBackend
    {
    public:
        gpu::BackendStatus Initialize(const gpu::DeviceParams&, gpu::Capabilities& capabilities) noexcept override
        {
            initialized = true;
            capabilities.backend = gpu::BackendKind::D3D12;
            capabilities.uploadBufferAlignment = 256;
            capabilities.constantBufferAlignment = 256;
            capabilities.maximumTextureDimension2D = 16384;
            capabilities.maximumTextureDimension3D = 2048;
            capabilities.maximumTextureArrayLayers = 2048;
            capabilities.asyncCompute = true;
            capabilities.copyQueue = true;
            capabilities.transientHeaps = true;
            capabilities.resourceAliasing = true;
            capabilities.bindlessResources = true;
            capabilities.bindlessSamplers = true;
            capabilities.descriptorIndexing = true;
            capabilities.maximumBindlessResources = 4096;
            capabilities.maximumBindlessSamplers = 256;
            capabilities.maximumPushConstantBytes = 128;
            capabilities.occlusionQueries = true;
            capabilities.pipelineStatisticsQueries = true;
            capabilities.timestampQueries = true;
            capabilities.timestampCalibration = true;
            capabilities.gpuMarkers = true;
            capabilities.memoryBudgetQueries = true;
            capabilities.explicitResidency = true;
            return {};
        }
        gpu::BackendStatus Shutdown() noexcept override
        {
            if (lifetimeStats.liveResources != 0 || queryPoolAlive)
                return gpu::BackendStatus::Failure(gpu::FailureCode::Busy, 0, "fake backend still owns live resources");
            initialized = false;
            return {};
        }
        gpu::DeviceState TestDeviceState() noexcept override
        {
            return initialized ? gpu::DeviceState::Operational : gpu::DeviceState::Unknown;
        }
        gpu::BackendStatus WaitIdle() noexcept override
        {
            ++waitIdleCount;
            lifetimeStats.completedRetirements += lifetimeStats.pendingRetirements;
            lifetimeStats.pendingRetirements = 0;
            return {};
        }
        gpu::BackendStatus RetireResources() noexcept override
        {
            ++retireResourcesCount;
            lifetimeStats.completedRetirements += lifetimeStats.pendingRetirements;
            lifetimeStats.pendingRetirements = 0;
            return {};
        }
        gpu::BackendStatus FlushRetiredResources() noexcept override
        {
            ++waitIdleCount;
            lifetimeStats.completedRetirements += lifetimeStats.pendingRetirements;
            lifetimeStats.pendingRetirements = 0;
            return {};
        }
        gpu::BackendStatus QueryMemoryBudget(const gpu::MemorySegment segment, gpu::MemoryBudgetSnapshot& budget) noexcept override
        {
            budget = {8ull * 1024 * 1024 * 1024, 2ull * 1024 * 1024 * 1024, 1024, 512, segment};
            ++residencyStats.budgetQueries;
            return {};
        }
        gpu::BackendStatus SetResidencyPriority(const gpu::ResourceRef resource, gpu::ResidencyPriority) noexcept override
        {
            if (!resource.IsValid())
                return gpu::BackendStatus::Failure(gpu::FailureCode::InvalidReference, 0, "invalid resource");
            ++residencyStats.priorityChanges;
            return {};
        }
        gpu::BackendStatus SetResidencyPinned(const gpu::ResourceRef resource, const bool pinned) noexcept override
        {
            if (!resource.IsValid())
                return gpu::BackendStatus::Failure(gpu::FailureCode::InvalidReference, 0, "invalid resource");
            ++residencyPinChanges;
            residencyPinned = pinned;
            return gpu::BackendStatus::Success();
        }
        gpu::BackendStatus MakeResident(const vanguard::containers::ArraySpan<const gpu::ResourceRef> resources) noexcept override
        {
            ++residencyStats.makeResidentCalls;
            residencyStats.objectsMadeResident += resources.Size();
            return {};
        }
        gpu::BackendStatus Evict(const vanguard::containers::ArraySpan<const gpu::ResourceRef> resources,
                                 const gpu::ResidencyFenceSet& safeAfter) noexcept override
        {
            ++residencyStats.evictCalls;
            if (safeAfter.graphics > completedGraphicsFence)
            {
                ++residencyStats.rejectedEvictions;
                return gpu::BackendStatus::Failure(gpu::FailureCode::Busy, 0, "resource is still in flight");
            }
            residencyStats.objectsEvicted += resources.Size();
            return {};
        }
        gpu::ResidencyStats GetResidencyStats() const noexcept override
        {
            return residencyStats;
        }

        gpu::TextureRef CreateTexture(const gpu::TextureDesc&, const gpu::TextureInitData&) noexcept override
        {
            Create(gpu::ResourceKind::Texture);
            return {1, 1};
        }
        gpu::BufferRef CreateBuffer(const gpu::BufferDesc&, const gpu::BufferInitData&) noexcept override
        {
            Create(gpu::ResourceKind::Buffer);
            return {2, 1};
        }
        gpu::HeapRef CreateHeap(const gpu::HeapDesc&) noexcept override
        {
            Create(gpu::ResourceKind::Heap);
            return {3, 1};
        }
        gpu::BindingLayoutRef RequestBindingLayout(const gpu::BindingLayoutDesc&) noexcept override
        {
            Create(gpu::ResourceKind::BindingLayout);
            return {static_cast<vanguard::u32>(gpu::ResourceKind::BindingLayout), 1};
        }
        gpu::DescriptorDomainRef CreateDescriptorDomain(const gpu::DescriptorDomainDesc& desc) noexcept override
        {
            Create(gpu::ResourceKind::DescriptorDomain);
            descriptorStats.capacity = desc.capacity;
            descriptorStats.free = desc.capacity;
            return {static_cast<vanguard::u32>(gpu::ResourceKind::DescriptorDomain), 1};
        }
        gpu::DescriptorHandle AllocateDescriptor(gpu::DescriptorDomainRef) noexcept override
        {
            ++descriptorStats.allocated;
            --descriptorStats.free;
            descriptorStats.peakAllocated = descriptorStats.allocated;
            return {0, 1};
        }
        gpu::BackendStatus WriteDescriptor(gpu::DescriptorDomainRef, gpu::DescriptorHandle, gpu::TextureRef, gpu::BindingType,
                                           const gpu::TextureViewDesc&) noexcept override
        {
            ++descriptorStats.populated;
            return {};
        }
        gpu::BackendStatus WriteDescriptor(gpu::DescriptorDomainRef, gpu::DescriptorHandle, gpu::BufferRef, gpu::BindingType,
                                           const gpu::BufferViewDesc&) noexcept override
        {
            ++descriptorStats.populated;
            return {};
        }
        gpu::BackendStatus WriteDescriptor(gpu::DescriptorDomainRef, gpu::DescriptorHandle, gpu::SamplerStateRef) noexcept override
        {
            ++descriptorStats.populated;
            return {};
        }
        gpu::BackendStatus WriteDescriptor(gpu::DescriptorDomainRef, gpu::DescriptorHandle, gpu::AccelerationStructureRef) noexcept override
        {
            return gpu::BackendStatus::Failure(gpu::FailureCode::Unsupported, 0, "ray tracing is unavailable");
        }
        gpu::BackendStatus RetireDescriptor(gpu::DescriptorDomainRef, gpu::DescriptorHandle, const gpu::DescriptorRetirement&) noexcept override
        {
            --descriptorStats.allocated;
            --descriptorStats.populated;
            ++descriptorStats.free;
            ++descriptorStats.completedRetirements;
            return {};
        }
        gpu::DescriptorDomainStats GetDescriptorDomainStats(gpu::DescriptorDomainRef) const noexcept override
        {
            return descriptorStats;
        }
        gpu::SamplerStateRef RequestSamplerState(const gpu::SamplerStateDesc&) noexcept override
        {
            Create(gpu::ResourceKind::SamplerState);
            return {static_cast<vanguard::u32>(gpu::ResourceKind::SamplerState), 1};
        }
        gpu::ShaderRef CreateShader(const gpu::ShaderDesc&) noexcept override
        {
            Create(gpu::ResourceKind::Shader);
            return {static_cast<vanguard::u32>(gpu::ResourceKind::Shader), 1};
        }
        gpu::VertexLayoutRef GetVertexLayout(const gpu::VertexLayoutDesc&) noexcept override
        {
            return {static_cast<vanguard::u32>(gpu::ResourceKind::VertexLayout), 1};
        }
        gpu::PipelineRef CreateGraphicsPipeline(const gpu::GraphicsPipelineDesc&) noexcept override
        {
            Create(gpu::ResourceKind::Pipeline);
            return {static_cast<vanguard::u32>(gpu::ResourceKind::Pipeline), 1};
        }
        gpu::PipelineRef CreateComputePipeline(const gpu::ComputePipelineDesc&) noexcept override
        {
            Create(gpu::ResourceKind::Pipeline);
            return {static_cast<vanguard::u32>(gpu::ResourceKind::Pipeline), 1};
        }
        gpu::PipelineRef CreateRayTracingPipeline(const gpu::RayTracingPipelineDesc&) noexcept override
        {
            Create(gpu::ResourceKind::Pipeline);
            return {static_cast<vanguard::u32>(gpu::ResourceKind::Pipeline), 1};
        }
        gpu::AccelerationStructureRef CreateAccelerationStructure(const gpu::AccelerationStructureDesc&) noexcept override
        {
            return {};
        }
        gpu::ShaderTableRef CreateShaderTable(const gpu::ShaderTableDesc&) noexcept override
        {
            return {};
        }
        gpu::BackendStatus GetAccelerationStructureDeviceAddress(gpu::AccelerationStructureRef, vanguard::u64&) noexcept override
        {
            return gpu::BackendStatus::Failure(gpu::FailureCode::Unsupported, 0, "ray tracing is unavailable");
        }
        gpu::QueryPoolRef CreateQueryPool(const gpu::QueryPoolDesc&) noexcept override
        {
            queryPoolAlive = true;
            ++activeQueryPools;
            return {nextQueryPoolIndex++, 1};
        }
        void DestroyQueryPool(const gpu::QueryPoolRef queryPool) noexcept override
        {
            if (queryPool.index >= 40 && queryPool.index < nextQueryPoolIndex && activeQueryPools != 0)
            {
                --activeQueryPools;
                queryPoolAlive = activeQueryPools != 0;
                ++queryPoolDestroyCount;
            }
        }
        gpu::BackendStatus BeginQuery(gpu::CommandListRef, gpu::QueryPoolRef, vanguard::u32) noexcept override
        {
            return {};
        }
        gpu::BackendStatus EndQuery(gpu::CommandListRef, gpu::QueryPoolRef, vanguard::u32) noexcept override
        {
            return {};
        }
        gpu::BackendStatus IssueQuery(gpu::CommandListRef, gpu::QueryPoolRef, vanguard::u32) noexcept override
        {
            return {};
        }
        gpu::BackendStatus ResolveQueries(gpu::CommandListRef, gpu::QueryPoolRef, vanguard::u32, vanguard::u32) noexcept override
        {
            return {};
        }
        gpu::BackendStatus AcquireQueries(gpu::QueryPoolRef, vanguard::u32, vanguard::u32) noexcept override
        {
            return {};
        }
        void ReleaseQueries(gpu::QueryPoolRef) noexcept override {}
        gpu::BackendStatus GetQueryResult(gpu::QueryPoolRef, vanguard::u32 index, vanguard::u64& result) noexcept override
        {
            result = 100 + index;
            return {};
        }
        gpu::BackendStatus GetQueryResult(gpu::QueryPoolRef, vanguard::u32, gpu::PipelineStatistics& result) noexcept override
        {
            result.inputAssemblerVertices = 3;
            return {};
        }
        gpu::BackendStatus GetTimestampFrequency(gpu::QueueType, vanguard::u64& frequency) const noexcept override
        {
            frequency = 1'000'000;
            return {};
        }
        gpu::BackendStatus CalibrateTimestamps(gpu::QueueType, gpu::TimestampCalibration& calibration) const noexcept override
        {
            calibration = {10, 20, 1'000'000, 10'000'000};
            return {};
        }
        gpu::BackendStatus BindMemory(gpu::TextureRef, gpu::HeapRef, vanguard::u64) noexcept override
        {
            ++bindCount;
            return {};
        }
        gpu::BackendStatus BindMemory(gpu::BufferRef, gpu::HeapRef, vanguard::u64) noexcept override
        {
            ++bindCount;
            return {};
        }
        gpu::MemoryRequirements GetMemoryRequirements(gpu::TextureRef) const noexcept override
        {
            return {65536, 65536, 7};
        }
        gpu::MemoryRequirements GetMemoryRequirements(gpu::BufferRef) const noexcept override
        {
            return {4096, 256, 3};
        }
        bool IsResourceReferenceValid(const gpu::ResourceRef resource) const noexcept override
        {
            const vanguard::u32 kind = static_cast<vanguard::u32>(resource.GetKind());
            return resource.IsValid() && kind < ResourceKindCount && resource.Index() == kind && resource.GetGeneration() == 1 &&
                   (resource.GetKind() == gpu::ResourceKind::VertexLayout || refCounts[kind] != 0);
        }
        void AddRef(const gpu::ResourceRef resource) noexcept override
        {
            if (!IsResourceReferenceValid(resource))
                return;
            ++refCounts[static_cast<vanguard::u32>(resource.GetKind())];
            ++lifetimeStats.totalReferences;
        }
        vanguard::i32 Release(const gpu::ResourceRef resource) noexcept override
        {
            if (!IsResourceReferenceValid(resource))
                return 0;
            const vanguard::u32 kind = static_cast<vanguard::u32>(resource.GetKind());
            --refCounts[kind];
            --lifetimeStats.totalReferences;
            ++releaseCount;
            if (refCounts[kind] == 0)
            {
                --lifetimeStats.liveResources;
                ++lifetimeStats.pendingRetirements;
            }
            return static_cast<vanguard::i32>(refCounts[kind]);
        }
        gpu::ResourceLifetimeStats GetResourceLifetimeStats() const noexcept override
        {
            return lifetimeStats;
        }

        gpu::CommandListRef CreateCommandList(const gpu::CommandListType type, vanguard::u64) noexcept override
        {
            commandListType = type;
            return {20, 1};
        }
        void DiscardCommandList(gpu::CommandListRef) noexcept override
        {
            commandListType = gpu::CommandListType::None;
        }
        gpu::CommandListType GetCommandListType(const gpu::CommandListRef commandList) const noexcept override
        {
            return commandList == gpu::CommandListRef{20, 1} ? commandListType : gpu::CommandListType::None;
        }
        gpu::BackendStatus CloseAndSubmitCommandLists(const char*, vanguard::containers::ArraySpan<const gpu::CommandListRef>, gpu::CommandListSyncType,
                                                      gpu::GpuFence& completion) noexcept override
        {
            ++submitCount;
            completion = {gpu::QueueType::Graphics, 9};
            return {};
        }
        gpu::GpuFence GetGpuFence(gpu::CommandListRef) const noexcept override
        {
            return {gpu::QueueType::Graphics, 8};
        }
        bool IsGpuFenceComplete(const gpu::GpuFence fence) const noexcept override
        {
            return fence.value <= 9;
        }
        gpu::BackendStatus WaitForGpuFence(gpu::GpuFence, vanguard::u64) noexcept override
        {
            ++waitFenceCount;
            return {};
        }
        gpu::BackendStatus AddToResidencyWorkingSet(gpu::CommandListRef, gpu::ResourceRef) noexcept override
        {
            ++workingSetAddCount;
            return {};
        }
        gpu::BackendStatus SetPipeline(gpu::CommandListRef, gpu::PipelineRef) noexcept override
        {
            ++setPipelineCount;
            return {};
        }
        gpu::BackendStatus SetupRenderTargets(gpu::CommandListRef, const gpu::RenderTargetSetup&) noexcept override
        {
            return {};
        }
        gpu::BackendStatus SetVariableRateShading(gpu::CommandListRef, const gpu::VariableRateShadingState&) noexcept override
        {
            return gpu::BackendStatus::Failure(gpu::FailureCode::Unsupported, 0, "variable-rate shading is unavailable");
        }
        gpu::BackendStatus SetViewport(gpu::CommandListRef, const gpu::ViewportDesc&) noexcept override
        {
            return {};
        }
        gpu::BackendStatus SetScissors(gpu::CommandListRef, const gpu::Rect&) noexcept override
        {
            return {};
        }
        gpu::BackendStatus BindVertexBuffers(gpu::CommandListRef, vanguard::u32,
                                             vanguard::containers::ArraySpan<const gpu::VertexBufferBinding>) noexcept override
        {
            return {};
        }
        gpu::BackendStatus BindIndexBuffer(gpu::CommandListRef, const gpu::IndexBufferBinding&) noexcept override
        {
            return {};
        }
        gpu::BackendStatus BindIndirectArguments(gpu::CommandListRef, gpu::BufferRef, gpu::BufferRef) noexcept override
        {
            return {};
        }
        gpu::BackendStatus SetPushConstants(gpu::CommandListRef, const void*, vanguard::u32) noexcept override
        {
            return {};
        }
        gpu::BackendStatus ClearColorTarget(gpu::CommandListRef, gpu::TextureRef, const gpu::ColorValue&, const gpu::SubresourceRange&,
                                            const gpu::Rect*) noexcept override
        {
            ++clearCount;
            return {};
        }
        gpu::BackendStatus ClearDepthStencilTarget(gpu::CommandListRef, gpu::TextureRef, bool, vanguard::f32, bool, vanguard::u8, const gpu::SubresourceRange&,
                                                   const gpu::Rect*) noexcept override
        {
            ++clearCount;
            return {};
        }
        gpu::BackendStatus ClearTextureUav(gpu::CommandListRef, gpu::TextureRef, const gpu::ColorValue&, const gpu::SubresourceRange&) noexcept override
        {
            ++clearCount;
            return {};
        }
        gpu::BackendStatus ClearTextureUav(gpu::CommandListRef, gpu::TextureRef, vanguard::u32, const gpu::SubresourceRange&) noexcept override
        {
            ++clearCount;
            return {};
        }
        gpu::BackendStatus ClearBufferUav(gpu::CommandListRef, gpu::BufferRef, vanguard::u32) noexcept override
        {
            ++clearCount;
            return {};
        }
        gpu::BackendStatus DiscardTexture(gpu::CommandListRef, gpu::TextureRef, const gpu::SubresourceRange&) noexcept override
        {
            ++discardCount;
            return {};
        }
        gpu::BackendStatus DiscardBuffer(gpu::CommandListRef, gpu::BufferRef) noexcept override
        {
            ++discardCount;
            return {};
        }
        gpu::BackendStatus SetStencilRefValue(gpu::CommandListRef, vanguard::u8) noexcept override
        {
            ++dynamicOutputStateCount;
            return {};
        }
        gpu::BackendStatus SetBlendFactor(gpu::CommandListRef, const gpu::ColorValue&) noexcept override
        {
            ++dynamicOutputStateCount;
            return {};
        }
        gpu::BackendStatus BeginGpuEvent(gpu::CommandListRef, const char*) noexcept override
        {
            ++gpuEventDepth;
            return {};
        }
        gpu::BackendStatus EndGpuEvent(gpu::CommandListRef) noexcept override
        {
            if (gpuEventDepth != 0)
                --gpuEventDepth;
            return {};
        }
        gpu::BackendStatus SetGpuMarker(gpu::CommandListRef, const char*) noexcept override
        {
            ++gpuMarkerCount;
            return {};
        }
        gpu::BackendStatus DrawPrimitive(gpu::CommandListRef, const gpu::DrawArguments&) noexcept override
        {
            ++drawCount;
            return {};
        }
        gpu::BackendStatus DrawIndexedPrimitive(gpu::CommandListRef, const gpu::DrawIndexedArguments&) noexcept override
        {
            ++drawCount;
            return {};
        }
        gpu::BackendStatus DrawPrimitiveIndirect(gpu::CommandListRef, vanguard::u64, vanguard::u32) noexcept override
        {
            ++drawCount;
            return {};
        }
        gpu::BackendStatus DrawIndexedPrimitiveIndirect(gpu::CommandListRef, vanguard::u64, vanguard::u32) noexcept override
        {
            ++drawCount;
            return {};
        }
        gpu::BackendStatus DrawIndexedPrimitiveIndirectCount(gpu::CommandListRef, vanguard::u64, vanguard::u64, vanguard::u32) noexcept override
        {
            ++drawCount;
            return {};
        }
        gpu::BackendStatus DispatchCompute(gpu::CommandListRef, vanguard::u32, vanguard::u32, vanguard::u32) noexcept override
        {
            ++dispatchCount;
            return {};
        }
        gpu::BackendStatus DispatchIndirectCompute(gpu::CommandListRef, vanguard::u64) noexcept override
        {
            ++dispatchCount;
            return {};
        }
        gpu::BackendStatus BuildBottomLevelAccelerationStructure(gpu::CommandListRef, gpu::AccelerationStructureRef,
                                                                 vanguard::containers::ArraySpan<const gpu::RayTracingGeometryDesc>,
                                                                 gpu::AccelerationStructureBuildMode) noexcept override
        {
            return gpu::BackendStatus::Failure(gpu::FailureCode::Unsupported, 0, "ray tracing is unavailable");
        }
        gpu::BackendStatus BuildTopLevelAccelerationStructure(gpu::CommandListRef, gpu::AccelerationStructureRef,
                                                              vanguard::containers::ArraySpan<const gpu::RayTracingInstanceDesc>,
                                                              gpu::AccelerationStructureBuildMode) noexcept override
        {
            return gpu::BackendStatus::Failure(gpu::FailureCode::Unsupported, 0, "ray tracing is unavailable");
        }
        gpu::BackendStatus BuildTopLevelAccelerationStructureIndirect(gpu::CommandListRef, gpu::AccelerationStructureRef, gpu::BufferRef, vanguard::u64,
                                                                      vanguard::u32, gpu::AccelerationStructureBuildMode) noexcept override
        {
            return gpu::BackendStatus::Failure(gpu::FailureCode::Unsupported, 0, "ray tracing is unavailable");
        }
        gpu::BackendStatus CopyAccelerationStructure(gpu::CommandListRef, gpu::AccelerationStructureRef, gpu::AccelerationStructureRef,
                                                     gpu::AccelerationStructureCopyMode) noexcept override
        {
            return gpu::BackendStatus::Failure(gpu::FailureCode::Unsupported, 0, "ray tracing is unavailable");
        }
        gpu::BackendStatus WriteAccelerationStructureCompactedSize(gpu::CommandListRef, gpu::AccelerationStructureRef, gpu::QueryPoolRef,
                                                                   vanguard::u32) noexcept override
        {
            return gpu::BackendStatus::Failure(gpu::FailureCode::Unsupported, 0, "ray tracing is unavailable");
        }
        gpu::BackendStatus DispatchRays(gpu::CommandListRef, gpu::ShaderTableRef, const gpu::DispatchRaysArguments&) noexcept override
        {
            return gpu::BackendStatus::Failure(gpu::FailureCode::Unsupported, 0, "ray tracing is unavailable");
        }
        gpu::BackendStatus WriteBuffer(gpu::CommandListRef, gpu::BufferRef, const void*, vanguard::u64, vanguard::u64) noexcept override
        {
            return {};
        }
        gpu::BackendStatus WriteTexture(gpu::CommandListRef, gpu::TextureRef, const gpu::TextureSubresourceData&) noexcept override
        {
            return {};
        }
        gpu::BackendStatus CopyBuffer(gpu::CommandListRef, gpu::BufferRef, vanguard::u64, gpu::BufferRef, vanguard::u64, vanguard::u64) noexcept override
        {
            return {};
        }
        gpu::BackendStatus CopyTexture(gpu::CommandListRef, gpu::TextureRef, gpu::TextureRef, const gpu::TextureCopyRegion&) noexcept override
        {
            ++textureCopyCount;
            return {};
        }
        gpu::BackendStatus ResolveTexture(gpu::CommandListRef, gpu::TextureRef, gpu::TextureRef, const gpu::TextureResolveRegion&) noexcept override
        {
            ++textureResolveCount;
            return {};
        }
        gpu::BackendStatus RequestTextureReadback(gpu::CommandListRef, gpu::TextureRef, const gpu::TextureReadbackRegion&,
                                                  gpu::TextureReadbackRef& readback) noexcept override
        {
            Create(gpu::ResourceKind::TextureReadback);
            readback = {static_cast<vanguard::u32>(gpu::ResourceKind::TextureReadback), 1};
            ++textureReadbackCount;
            return {};
        }
        gpu::BackendStatus GetTextureReadbackInfo(gpu::TextureReadbackRef, gpu::TextureReadbackInfo& info) noexcept override
        {
            info = {gpu::Format::R8G8B8A8UNorm, {2, 2, 1}, gpu::TextureReadbackState::Ready, {gpu::QueueType::Graphics, 4}};
            return {};
        }
        gpu::BackendStatus MapTextureReadback(gpu::TextureReadbackRef, gpu::TextureReadbackMapping& mapping) noexcept override
        {
            mapping = {mappedBytes, 8, 16, 16, gpu::Format::R8G8B8A8UNorm, {2, 2, 1}};
            return {};
        }
        gpu::BackendStatus UnmapTextureReadback(gpu::TextureReadbackRef) noexcept override
        {
            return {};
        }
        gpu::BackendStatus LockBuffer(gpu::BufferRef, vanguard::u64, vanguard::u64, void*& data) noexcept override
        {
            data = mappedBytes;
            return {};
        }
        void UnlockBuffer(gpu::BufferRef) noexcept override {}

        gpu::BackendStatus TransitionTexture(gpu::CommandListRef, gpu::TextureRef, gpu::ResourceState, gpu::ResourceState,
                                             const gpu::SubresourceRange&) noexcept override
        {
            ++barrierCount;
            return {};
        }
        gpu::BackendStatus TransitionBuffer(gpu::CommandListRef, gpu::BufferRef, gpu::ResourceState, gpu::ResourceState) noexcept override
        {
            ++barrierCount;
            return {};
        }
        gpu::BackendStatus BarrierTextureUav(gpu::CommandListRef, gpu::TextureRef) noexcept override
        {
            ++barrierCount;
            return {};
        }
        gpu::BackendStatus BarrierBufferUav(gpu::CommandListRef, gpu::BufferRef) noexcept override
        {
            ++barrierCount;
            return {};
        }
        gpu::BackendStatus BarrierTextureAliasing(gpu::CommandListRef, bool, gpu::TextureRef, gpu::TextureRef) noexcept override
        {
            ++barrierCount;
            return {};
        }
        gpu::BackendStatus BarrierBufferAliasing(gpu::CommandListRef, bool, gpu::BufferRef, gpu::BufferRef) noexcept override
        {
            ++barrierCount;
            return {};
        }
        gpu::BackendStatus FlushPendingBarriers(gpu::CommandListRef) noexcept override
        {
            ++flushBarrierCount;
            return {};
        }
        gpu::BackendStatus MakeStateSafeToRetire(gpu::CommandListRef, gpu::TextureRef) noexcept override
        {
            ++retirementTransitionCount;
            return {};
        }
        gpu::BackendStatus MakeStateSafeToRetire(gpu::CommandListRef, gpu::BufferRef) noexcept override
        {
            ++retirementTransitionCount;
            return {};
        }

        gpu::SwapChainRef CreateSwapChainWithBackBuffer(const gpu::SwapChainDesc&) noexcept override
        {
            Create(gpu::ResourceKind::SwapChain);
            return {static_cast<vanguard::u32>(gpu::ResourceKind::SwapChain), 1};
        }
        gpu::BackendStatus ResizeBackbuffer(gpu::SwapChainRef, vanguard::u32, vanguard::u32) noexcept override
        {
            return {};
        }
        gpu::BackendStatus SetSwapChainPresentParameters(gpu::SwapChainRef, const gpu::PresentParameters& parameters) noexcept override
        {
            swapChainStats.presentParameters = parameters;
            return {};
        }
        gpu::BackendStatus AcquireBackBuffer(const gpu::SwapChainRef swapChain, gpu::AcquiredBackBuffer& acquisition) noexcept override
        {
            acquisition = {swapChain, {1, 1}, ++acquisitionSerial, 0, 1280, 720};
            swapChainStats.state = gpu::SwapChainState::Acquired;
            swapChainStats.lastAcquisitionSerial = acquisition.serial;
            ++swapChainStats.acquisitions;
            return {};
        }
        gpu::BackendStatus AbandonBackBuffer(const gpu::AcquiredBackBuffer&) noexcept override
        {
            swapChainStats.state = gpu::SwapChainState::Available;
            ++swapChainStats.abandonedAcquisitions;
            return {};
        }
        gpu::BackendStatus TransitionSwapChainPresent(gpu::CommandListRef, const gpu::AcquiredBackBuffer&) noexcept override
        {
            ++barrierCount;
            return {};
        }
        gpu::BackendStatus Present(const gpu::AcquiredBackBuffer&) noexcept override
        {
            ++presentCount;
            swapChainStats.state = gpu::SwapChainState::Available;
            ++swapChainStats.presentedFrames;
            return {};
        }
        gpu::SwapChainStats GetSwapChainStats(gpu::SwapChainRef) noexcept override
        {
            return swapChainStats;
        }
        void SetResourceDebugName(gpu::TextureRef, const char*) noexcept override
        {
            ++debugNameCount;
        }
        void SetResourceDebugName(gpu::TextureReadbackRef, const char*) noexcept override
        {
            ++debugNameCount;
        }
        void SetResourceDebugName(gpu::BufferRef, const char*) noexcept override
        {
            ++debugNameCount;
        }
        void SetResourceDebugName(gpu::HeapRef, const char*) noexcept override
        {
            ++debugNameCount;
        }
        void SetResourceDebugName(gpu::SamplerStateRef, const char*) noexcept override
        {
            ++debugNameCount;
        }
        void SetResourceDebugName(gpu::ShaderRef, const char*) noexcept override
        {
            ++debugNameCount;
        }
        void SetResourceDebugName(gpu::VertexLayoutRef, const char*) noexcept override
        {
            ++debugNameCount;
        }
        void SetResourceDebugName(gpu::PipelineRef, const char*) noexcept override
        {
            ++debugNameCount;
        }
        void SetResourceDebugName(gpu::AccelerationStructureRef, const char*) noexcept override {}
        void SetResourceDebugName(gpu::ShaderTableRef, const char*) noexcept override {}
        void SetResourceDebugName(gpu::QueryPoolRef, const char*) noexcept override
        {
            ++debugNameCount;
        }
        void SetResourceDebugName(gpu::CommandListRef, const char*) noexcept override
        {
            ++debugNameCount;
        }
        void SetResourceDebugName(gpu::SwapChainRef, const char*) noexcept override
        {
            ++debugNameCount;
        }

        bool initialized = false;
        gpu::CommandListType commandListType = gpu::CommandListType::None;
        vanguard::u32 waitIdleCount = 0;
        vanguard::u32 retireResourcesCount = 0;
        vanguard::u32 bindCount = 0;
        vanguard::u32 releaseCount = 0;
        vanguard::u32 submitCount = 0;
        vanguard::u32 waitFenceCount = 0;
        vanguard::u32 workingSetAddCount = 0;
        vanguard::u32 setPipelineCount = 0;
        vanguard::u32 drawCount = 0;
        vanguard::u32 dispatchCount = 0;
        vanguard::u32 barrierCount = 0;
        vanguard::u32 flushBarrierCount = 0;
        vanguard::u32 retirementTransitionCount = 0;
        vanguard::u32 gpuEventDepth = 0;
        vanguard::u32 gpuMarkerCount = 0;
        vanguard::u32 clearCount = 0;
        vanguard::u32 discardCount = 0;
        vanguard::u32 textureCopyCount = 0;
        vanguard::u32 textureResolveCount = 0;
        vanguard::u32 textureReadbackCount = 0;
        vanguard::u32 dynamicOutputStateCount = 0;
        vanguard::u32 presentCount = 0;
        vanguard::u64 acquisitionSerial = 0;
        gpu::SwapChainStats swapChainStats{gpu::SwapChainState::Available};
        vanguard::u32 debugNameCount = 0;
        vanguard::u32 queryPoolDestroyCount = 0;
        vanguard::u64 completedGraphicsFence = 0;
        gpu::ResidencyStats residencyStats{};
        vanguard::u32 residencyPinChanges = 0;
        bool residencyPinned = false;

    private:
        static constexpr vanguard::u32 ResourceKindCount = static_cast<vanguard::u32>(gpu::ResourceKind::Count);

        void Create(const gpu::ResourceKind kind) noexcept
        {
            const vanguard::u32 index = static_cast<vanguard::u32>(kind);
            refCounts[index] = 1;
            ++lifetimeStats.liveResources;
            ++lifetimeStats.totalReferences;
        }

        vanguard::u32 refCounts[ResourceKindCount]{};
        vanguard::u8 mappedBytes[16]{};
        gpu::ResourceLifetimeStats lifetimeStats{};
        gpu::DescriptorDomainStats descriptorStats{};
        bool queryPoolAlive = false;
        vanguard::u32 activeQueryPools = 0;
        vanguard::u32 nextQueryPoolIndex = 40;
    };
} // namespace

int main()
{
    Check(vanguard::memory::Initialize(), "memory initialization");

    gpu::Failure failure{};
    gpu::BufferDesc invalidBuffer{};
    Check(!gpu::CreateBuffer(invalidBuffer, {}, &failure).IsValid() && failure.code == gpu::FailureCode::NotInitialized,
          "resource creation reports use before initialization");

    FakeBackend backend;
    gpu::DeviceParams invalidResidencyParams{};
    invalidResidencyParams.residencyPolicy.recoveryThresholdPercent = invalidResidencyParams.residencyPolicy.pressureThresholdPercent;
    Check(!gpu::Initialize(backend, invalidResidencyParams, &failure) && failure.code == gpu::FailureCode::InvalidArgument && !backend.initialized,
          "RHI rejects a residency policy without a recovery hysteresis band");
    Check(gpu::Initialize(backend, {}, &failure) && gpu::IsInitialized(), "RHI initializes through its backend boundary");
    Check(gpu::GetCapabilities().resourceAliasing && gpu::TestDeviceState() == gpu::DeviceState::Operational,
          "validated capabilities and device state are exposed");
    gpu::TextureDesc malformedTexture{};
    malformedTexture.extent = {16385, 1, 1};
    malformedTexture.format = gpu::Format::R8UNorm;
    Check(!gpu::CreateTexture(malformedTexture, {}, &failure) && failure.code == gpu::FailureCode::InvalidArgument,
          "texture creation enforces device dimension limits before reaching the backend");
    malformedTexture.dimension = gpu::TextureDimension::Texture3D;
    malformedTexture.extent = {2049, 1, 1};
    malformedTexture.arraySize = 1;
    Check(!gpu::CreateTexture(malformedTexture, {}, &failure) && failure.code == gpu::FailureCode::InvalidArgument,
          "texture creation enforces device 3D dimension limits before reaching the backend");
    malformedTexture.dimension = gpu::TextureDimension::Texture2D;
    malformedTexture.extent = {1, 1, 1};
    malformedTexture.format = static_cast<gpu::Format>(0xffffu);
    Check(!gpu::CreateTexture(malformedTexture, {}, &failure) && failure.code == gpu::FailureCode::InvalidArgument,
          "texture creation rejects unknown format values before backend conversion");
    gpu::BufferDesc malformedStructuredBuffer{};
    malformedStructuredBuffer.size = 256;
    malformedStructuredBuffer.usage = gpu::BufferUsage::Structured;
    Check(!gpu::CreateBuffer(malformedStructuredBuffer, {}, &failure) && failure.code == gpu::FailureCode::InvalidArgument,
          "structured buffers require an explicit element stride");
    gpu::AccelerationStructureDesc dormantAccelerationStructure{};
    Check(!gpu::CreateAccelerationStructure(dormantAccelerationStructure, &failure) && failure.code == gpu::FailureCode::Unsupported &&
              !gpu::CreateShaderTable({}, &failure) && failure.code == gpu::FailureCode::Unsupported,
          "dormant ray-tracing resource APIs report unsupported without reaching the backend");
    gpu::TextureDesc dormantShadingRateImage{};
    dormantShadingRateImage.extent = {80, 45, 1};
    dormantShadingRateImage.format = gpu::Format::R8UInt;
    dormantShadingRateImage.usage = gpu::TextureUsage::ShadingRate;
    Check(!gpu::CreateTexture(dormantShadingRateImage, {}, &failure) && failure.code == gpu::FailureCode::Unsupported,
          "dormant shading-rate images report unsupported without reaching the backend");

    gpu::TextureDesc textureDesc{};
    textureDesc.extent = {1920, 1080, 1};
    textureDesc.format = gpu::Format::R16G16B16A16Float;
    textureDesc.usage = gpu::TextureUsage::ShaderResource | gpu::TextureUsage::UnorderedAccess;
    textureDesc.virtualResource = true;
    gpu::Texture ownedTexture = gpu::Texture::Create(textureDesc, gpu::TextureInitData{}, &failure);
    gpu::TextureRef texture = ownedTexture.GetRef();
    gpu::BufferDesc bufferDesc{};
    bufferDesc.size = 1024 * 1024;
    bufferDesc.structureStride = 16;
    bufferDesc.usage = gpu::BufferUsage::Structured | gpu::BufferUsage::UnorderedAccess;
    bufferDesc.virtualResource = true;
    gpu::BufferRef buffer = gpu::CreateBuffer(bufferDesc, {}, &failure);
    gpu::HeapRef heap = gpu::CreateHeap({2 * 1024 * 1024, 65536, 7, gpu::MemoryType::DeviceLocal}, &failure);
    Check(texture.IsValid() && buffer.IsValid() && heap.IsValid(), "typed resources and transient heaps are created");
    gpu::MemoryBudgetSnapshot localBudget;
    const gpu::ResourceRef residencyResources[] = {gpu::ResourceRef(texture), gpu::ResourceRef(buffer), gpu::ResourceRef(heap)};
    gpu::ResidencyFenceSet unfinishedUse;
    unfinishedUse.Include({gpu::QueueType::Graphics, 9});
    Check(gpu::GetCapabilities().memoryBudgetQueries && gpu::GetCapabilities().explicitResidency &&
              gpu::QueryMemoryBudget(gpu::MemorySegment::Local, localBudget, &failure) && localBudget.budget == 8ull * 1024 * 1024 * 1024 &&
              localBudget.Available() == 6ull * 1024 * 1024 * 1024 &&
              gpu::SetResidencyPriority(gpu::ResourceRef(heap), gpu::ResidencyPriority::High, &failure) &&
              gpu::SetResidencyPinned(gpu::ResourceRef(heap), true, &failure) && backend.residencyPinned &&
              gpu::SetResidencyPinned(gpu::ResourceRef(heap), false, &failure) && !backend.residencyPinned &&
              !gpu::Evict({residencyResources, 3}, unfinishedUse, &failure) && failure.code == gpu::FailureCode::Busy &&
              gpu::Evict({residencyResources, 3}, {}, &failure) && gpu::MakeResident({residencyResources, 3}, &failure),
          "memory budgets and fence-safe batched residency operations route through the backend contract");
    const gpu::ResidencyStats residencyStats = gpu::GetResidencyStats();
    Check(residencyStats.budgetQueries == 1 && residencyStats.priorityChanges == 1 && backend.residencyPinChanges == 2 && residencyStats.evictCalls == 2 &&
              residencyStats.rejectedEvictions == 1 && residencyStats.objectsEvicted == 3 && residencyStats.objectsMadeResident == 3,
          "residency telemetry distinguishes completed operations from rejected in-flight eviction");
    Check(gpu::BindMemory(texture, heap, 0, &failure) && gpu::BindMemory(buffer, heap, 65536, &failure) && backend.bindCount == 2,
          "virtual resources bind to explicit heap placements");

    const gpu::ResourceRef genericTexture = texture;
    Check(sizeof(gpu::ResourceRef) == sizeof(gpu::TextureRef) && genericTexture.GetKind() == gpu::ResourceKind::Texture &&
              gpu::CastResourceRef<gpu::TextureRef>(genericTexture) == texture && !gpu::CastResourceRef<gpu::BufferRef>(genericTexture).IsValid(),
          "type-erased resources require checked conversion back to a typed reference");

    Check(gpu::GetRefCount(texture) == 1, "the owning factory adopts creation without an accidental reference");
    {
        gpu::Texture copiedTexture = ownedTexture;
        gpu::Texture assignedTexture;
        assignedTexture = copiedTexture;
        Check(gpu::GetRefCount(texture) == 3, "owning GPU references retain resources across copy and assignment");
        gpu::Texture movedTexture = static_cast<gpu::Texture&&>(assignedTexture);
        Check(!assignedTexture.IsValid() && movedTexture.IsValid() && gpu::GetRefCount(texture) == 3,
              "moving an owning GPU reference transfers rather than duplicates ownership");
    }
    Check(gpu::GetRefCount(texture) == 1, "temporary owning references release exactly their acquired ownership");

    gpu::SamplerStateRef sampler = gpu::RequestSamplerState({}, &failure);
    const vanguard::u32 shaderBytecode = 0x07230203u;
    gpu::ShaderRef shader = gpu::CreateShader({gpu::ShaderStage::Vertex, &shaderBytecode, sizeof(shaderBytecode), "Main"}, &failure);
    const gpu::VertexBindingDesc vertexBinding{0, 32, gpu::VertexInputRate::PerVertex, 1};
    const gpu::VertexAttributeDesc vertexAttribute{0, 0, 0, gpu::Format::R32G32B32Float, "POSITION", 0};
    gpu::VertexLayoutRef vertexLayout = gpu::GetVertexLayout({&vertexBinding, 1, &vertexAttribute, 1}, &failure);
    gpu::QueryPoolRef queryPool = gpu::CreateQueryPool({gpu::QueryType::Timestamp, 256}, &failure);
    const gpu::BindingLayoutEntry bindingEntries[] = {{0, 1, gpu::BindingType::TextureShaderResource},
                                                      {1, 1, gpu::BindingType::StructuredBufferUnorderedAccess}};
    gpu::BindingLayoutRef bindingLayout = gpu::RequestBindingLayout(
        {bindingEntries, 2, 0, gpu::ShaderStageBit(gpu::ShaderStage::Vertex) | gpu::ShaderStageBit(gpu::ShaderStage::Pixel)}, &failure);
    const gpu::BindingLayoutRef pipelineLayouts[] = {bindingLayout};
    gpu::GraphicsPipelineDesc graphicsPipelineDesc{};
    graphicsPipelineDesc.vertexShader = shader;
    graphicsPipelineDesc.vertexLayout = vertexLayout;
    graphicsPipelineDesc.bindingLayouts = pipelineLayouts;
    graphicsPipelineDesc.bindingLayoutCount = 1;
    graphicsPipelineDesc.attachments.colorFormats[0] = gpu::Format::R8G8B8A8UNorm;
    graphicsPipelineDesc.attachments.colorCount = 1;
    gpu::PipelineRef pipeline = gpu::CreateGraphicsPipeline(graphicsPipelineDesc, &failure);
    Check(!gpu::CreateDescriptorDomain({gpu::DescriptorDomainKind::Samplers, 257, 0, gpu::ShaderStageBit(gpu::ShaderStage::Pixel)}, &failure).IsValid() &&
              failure.code == gpu::FailureCode::InvalidArgument,
          "descriptor domains enforce class-specific hardware capacity limits");
    gpu::DescriptorDomainRef descriptorDomain = gpu::CreateDescriptorDomain(
        {gpu::DescriptorDomainKind::Resources, 1024, 0, gpu::ShaderStageBit(gpu::ShaderStage::Vertex) | gpu::ShaderStageBit(gpu::ShaderStage::Pixel)},
        &failure);
    const gpu::DescriptorHandle textureDescriptor = gpu::AllocateDescriptor(descriptorDomain, &failure);
    Check(sampler.IsValid() && shader.IsValid() && vertexLayout.IsValid() && queryPool.IsValid() && bindingLayout.IsValid() && pipeline.IsValid() &&
              descriptorDomain.IsValid() && textureDescriptor.IsValid() &&
              gpu::WriteDescriptor(descriptorDomain, textureDescriptor, texture, gpu::BindingType::TextureShaderResource, {}, &failure),
          "samplers, shaders, vertex layouts, query pools and descriptor layouts have typed references");
    gpu::DescriptorRetirement descriptorRetirement;
    descriptorRetirement.Include({gpu::QueueType::Graphics, 4});
    Check(gpu::RetireDescriptor(descriptorDomain, textureDescriptor, descriptorRetirement, &failure) &&
              gpu::GetDescriptorDomainStats(descriptorDomain).completedRetirements == 1,
          "descriptor handles expose explicit fence retirement and stable GPU indices");
    gpu::QueryPool ownedQueryPool(gpu::AdoptReference, queryPool);
    gpu::QueryPool movedQueryPool = static_cast<gpu::QueryPool&&>(ownedQueryPool);
    Check(!ownedQueryPool.IsValid() && movedQueryPool.IsValid(), "query pools preserve unique ownership");
    queryPool = movedQueryPool.Detach();
    Check(!gpu::Shutdown(&failure) && failure.code == gpu::FailureCode::Busy && gpu::IsInitialized(),
          "backend shutdown refuses live GPU resources without silently destroying them");

    const gpu::CommandListRef commandList = gpu::CreateCommandList(gpu::CommandListType::Default, 0x1234, &failure);
    Check(gpu::BindCommandList(commandList, &failure) && gpu::GetBoundCommandListType() == gpu::CommandListType::Default,
          "GpuApi-style command-list binding is thread local");
    Check(!gpu::DispatchRays({}, {}, &failure) && failure.code == gpu::FailureCode::Unsupported &&
              !gpu::BuildBottomLevelAccelerationStructure({}, {}, gpu::AccelerationStructureBuildMode::Build, &failure) &&
              failure.code == gpu::FailureCode::Unsupported,
          "dormant ray-tracing recording APIs remain explicit unsupported operations");
    Check(!gpu::SetVariableRateShading({gpu::ShadingRate::Rate2x2, gpu::ShadingRateCombiner::Passthrough, gpu::ShadingRateCombiner::Passthrough, {}, {}, true},
                                       &failure) &&
              failure.code == gpu::FailureCode::Unsupported,
          "dormant variable-rate shading commands remain explicit unsupported operations");
    gpu::TimestampCalibration timestampCalibration{};
    Check(gpu::BeginGpuEvent("RHI façade", &failure) && gpu::SetGpuMarker("query begin", &failure) && gpu::IssueQuery(queryPool, 0, &failure) &&
              gpu::IssueQuery(queryPool, 1, &failure) && gpu::ResolveQueries(queryPool, 0, 2, &failure) && gpu::EndGpuEvent(&failure) &&
              gpu::GetTimestampFrequency(gpu::QueueType::Graphics, &failure) == 1'000'000 &&
              gpu::CalibrateTimestamps(gpu::QueueType::Graphics, timestampCalibration, &failure) && timestampCalibration.IsValid() &&
              backend.gpuEventDepth == 0 && backend.gpuMarkerCount == 1,
          "GPU events, markers, query recording and timestamp calibration route through the backend contract");
    gpu::RenderTargetSetup renderTargets{};
    renderTargets.colorTargets[0].texture = texture;
    renderTargets.colorTargetCount = 1;
    const gpu::VertexBufferBinding vertexBufferBinding{buffer, 0, 0};
    const gpu::IndexBufferBinding indexBufferBinding{buffer, 0, gpu::IndexFormat::UInt16};
    const vanguard::u32 pushConstants[] = {1, 2, 3, 4};
    const gpu::Rect clearRectangle{8, 8, 64, 64};
    Check(gpu::ClearColorTarget(texture, {0.1f, 0.2f, 0.3f, 1.0f}, {}, &clearRectangle, &failure) &&
              gpu::ClearDepthTarget(texture, 0.0f, {}, nullptr, &failure) && gpu::ClearStencilTarget(texture, 7, {}, nullptr, &failure) &&
              gpu::ClearDepthStencilTarget(texture, 1.0f, 0, {}, nullptr, &failure) &&
              gpu::ClearTextureUav(texture, gpu::ColorValue{0.0f, 0.0f, 0.0f, 0.0f}, {}, &failure) && gpu::ClearTextureUav(texture, 0u, {}, &failure) &&
              gpu::ClearBufferUav(buffer, 0u, &failure) && gpu::DiscardTexture(texture, {}, &failure) && gpu::DiscardBuffer(buffer, &failure) &&
              gpu::SetStencilRefValue(19, &failure) && gpu::SetBlendFactor({0.25f, 0.5f, 0.75f, 1.0f}, &failure) && backend.clearCount == 7 &&
              backend.discardCount == 2 && backend.dynamicOutputStateCount == 2,
          "clear, discard and dynamic output state commands route through the bound command list");
    const gpu::Rect invalidClearRectangle{-1, 0, 1, 1};
    Check(!gpu::ClearColorTarget(texture, {}, {}, &invalidClearRectangle, &failure) && failure.code == gpu::FailureCode::InvalidArgument &&
              backend.clearCount == 7,
          "clear rectangles are rejected before reaching the backend when their extent is invalid");
    const gpu::TextureCopyRegion invalidTextureCopy{{}, {}, 0, 0, 0, 0, 0, 0, {16, 0, 1}};
    Check(gpu::CopyTexture(texture, texture, {}, &failure) && gpu::ResolveTexture(texture, texture, {}, &failure) && backend.textureCopyCount == 1 &&
              backend.textureResolveCount == 1 && !gpu::CopyTexture(texture, texture, invalidTextureCopy, &failure) &&
              failure.code == gpu::FailureCode::InvalidArgument && backend.textureCopyCount == 1,
          "texture copy and resolve commands route through the façade with complete extent validation");
    gpu::TextureReadback textureReadback(gpu::AdoptReference, gpu::RequestTextureReadback(texture, {}, &failure));
    gpu::TextureReadbackInfo textureReadbackInfo{};
    gpu::TextureReadbackMapping textureReadbackMapping{};
    Check(textureReadback.IsValid() && backend.textureReadbackCount == 1 && gpu::GetTextureReadbackInfo(textureReadback, textureReadbackInfo, &failure) &&
              textureReadbackInfo.state == gpu::TextureReadbackState::Ready && gpu::MapTextureReadback(textureReadback, textureReadbackMapping, &failure) &&
              textureReadbackMapping.data != nullptr && textureReadbackMapping.rowPitch == 8 && gpu::UnmapTextureReadback(textureReadback, &failure),
          "asynchronous texture readback exposes owned native-format staging and explicit mapping");
    Check(gpu::SetPipeline(pipeline, &failure) && gpu::SetupRenderTargets(renderTargets, &failure) &&
              gpu::SetViewport({0.0f, 0.0f, 1280.0f, 720.0f, 0.0f, 1.0f}, &failure) && gpu::SetScissors({0, 0, 1280, 720}, &failure) &&
              gpu::BindVertexBuffers(0, {&vertexBufferBinding, 1}, &failure) && gpu::BindIndexBuffer(indexBufferBinding, &failure) &&
              gpu::BindIndirectArguments(buffer, buffer, &failure) && gpu::SetPushConstants(pushConstants, sizeof(pushConstants), &failure) &&
              gpu::DrawPrimitive({3, 1, 0, 0}, &failure) && gpu::DrawIndexedPrimitive({3, 1, 0, 0, 0}, &failure) &&
              gpu::DrawPrimitiveIndirect(0, 1, &failure) && gpu::DrawIndexedPrimitiveIndirect(0, 1, &failure) &&
              gpu::DrawIndexedPrimitiveIndirectCount(0, 0, 1, &failure) && gpu::DispatchCompute(1, 1, 1, &failure) &&
              gpu::DispatchIndirectCompute(0, &failure) && backend.setPipelineCount == 1 && backend.drawCount == 5 && backend.dispatchCount == 2,
          "draw, indirect, push-constant and compute commands route through command-list-local state");
    constexpr vanguard::u64 unrepresentableIndirectOffset = static_cast<vanguard::u64>(0xffffffffu) + 1u;
    Check(!gpu::DrawPrimitiveIndirect(2, 1, &failure) && failure.code == gpu::FailureCode::InvalidArgument &&
              !gpu::DrawIndexedPrimitiveIndirect(unrepresentableIndirectOffset, 1, &failure) && failure.code == gpu::FailureCode::InvalidArgument &&
              !gpu::DrawIndexedPrimitiveIndirectCount(0, 2, 1, &failure) && failure.code == gpu::FailureCode::InvalidArgument &&
              !gpu::DispatchIndirectCompute(2, &failure) && failure.code == gpu::FailureCode::InvalidArgument && backend.drawCount == 5 &&
              backend.dispatchCount == 2,
          "indirect offsets reject misalignment and native narrowing before reaching the backend");
    Check(gpu::TransitionTexture(texture, gpu::ResourceState::Common, gpu::ResourceState::UnorderedAccess, {}, &failure) &&
              gpu::BarrierTextureUav(texture, &failure) &&
              gpu::TransitionBuffer(buffer, gpu::ResourceState::Common, gpu::ResourceState::UnorderedAccess, &failure) &&
              gpu::BarrierBufferAliasing(true, buffer, {}, &failure) && gpu::MakeStateSafeToRetire(texture, &failure) &&
              gpu::MakeStateSafeToRetire(buffer, &failure) && gpu::FlushPendingBarriers(&failure),
          "explicit transitions, UAV ordering, aliasing and barrier flush route through the bound command list");
    Check(gpu::AddToResidencyWorkingSet(gpu::ResourceRef(texture), &failure) && backend.workingSetAddCount == 1,
          "indirect bindless resources can be declared in the bound command-list working set");
    Check(gpu::TransitionTexture(texture, gpu::ResourceState::Unknown, gpu::ResourceState::Common, {}, &failure) &&
              gpu::TransitionBuffer(buffer, gpu::ResourceState::Unknown, gpu::ResourceState::Common, &failure),
          "Unknown before-state explicitly bypasses transition-state assertions");
    Check(!gpu::TransitionTexture(texture, gpu::ResourceState::Common, gpu::ResourceState::Unknown, {}, &failure) &&
              failure.code == gpu::FailureCode::InvalidArgument,
          "Unknown remains invalid as a requested destination state");
    gpu::GpuCounterSystem gpuCounters;
    const gpu::GpuCounterScopeId geometryScope = gpu::MakeGpuCounterScopeId("Geometry.Main");
    gpu::GpuCounterFrameHandle counterFrame{};
    gpu::GpuCounterScopeToken counterScope{};
    const bool counterRecordingSucceeded =
        gpuCounters.Initialize({2, 4, 2, gpu::QueueType::Graphics, false}, &failure) && gpuCounters.RegisterScope(geometryScope, "Geometry.Main", &failure) &&
        !gpuCounters.RegisterScope(geometryScope, "Geometry.Collision", &failure) && failure.code == gpu::FailureCode::IncompatibleBinding &&
        gpuCounters.BeginFrame(77, counterFrame, &failure) && gpuCounters.BeginScope(counterFrame, geometryScope, counterScope, &failure) &&
        !gpuCounters.EndFrame(counterFrame, &failure) && failure.code == gpu::FailureCode::Busy && gpuCounters.EndScope(counterScope, &failure) &&
        gpuCounters.EndFrame(counterFrame, &failure) && gpuCounters.GetFrameState(counterFrame) == gpu::GpuCounterFrameState::AwaitingSubmission &&
        gpuCounters.Collect(&failure) == 0;
    Check(counterRecordingSucceeded, "GPU counter recording uses registered stable scopes and refuses incomplete frames");
    gpu::CommandListRef commandLists[] = {commandList};
    gpu::GpuFence completion{};
    Check(!gpu::CloseAndSubmitCommandLists("bound rejection", commandLists, gpu::CommandListSyncType::None, completion, &failure) &&
              failure.code == gpu::FailureCode::InvalidCommandList,
          "a bound command list cannot be submitted implicitly");
    gpu::UnbindCommandList();
    Check(gpu::CloseAndSubmitCommandLists("frame", commandLists, gpu::CommandListSyncType::None, completion, &failure) && completion.IsValid() &&
              gpu::IsGpuFenceComplete(completion) && gpu::WaitForGpuFence(completion, 1000, &failure),
          "submission produces an explicit queue timeline fence");
    gpu::GpuCounterFrameView counterView{};
    const bool counterCollectionSucceeded = gpuCounters.CommitFrame(counterFrame, completion, &failure) && gpuCounters.Collect(&failure) == 1 &&
                                            gpuCounters.GetOldestReadyFrame(counterView, &failure) && counterView.frameNumber == 77 &&
                                            counterView.sampleCount == 1 && counterView.gpuBegin == 100 && counterView.gpuEnd == 101 &&
                                            counterView.samples[0].valid && counterView.samples[0].scope == geometryScope &&
                                            counterView.samples[0].gpuBegin == 102 && counterView.samples[0].gpuEnd == 103 &&
                                            gpuCounters.ConsumeFrame(counterView.handle, &failure) && gpuCounters.Shutdown(&failure);
    Check(counterCollectionSucceeded, "GPU counter frames are committed with the actual submission fence, collected without waiting and explicitly consumed");
    vanguard::u64 timestampResult = 0;
    Check(gpu::AcquireQueries(queryPool, 0, 2, &failure) && gpu::GetQueryResult(queryPool, 1, timestampResult, &failure) && timestampResult == 101,
          "resolved query ranges expose results only through explicit acquisition");
    gpu::ReleaseQueries(queryPool);

    int nativeWindow = 0;
    gpu::SwapChainDesc swapChainDesc{};
    swapChainDesc.surface = {gpu::PresentationSurfaceKind::Win32, &nativeWindow, nullptr};
    swapChainDesc.width = 1280;
    swapChainDesc.height = 720;
    gpu::SwapChainRef swapChain = gpu::CreateSwapChainWithBackBuffer(swapChainDesc, &failure);
    gpu::AcquiredBackBuffer abandonedBackBuffer;
    Check(swapChain.IsValid() && gpu::AcquireBackBuffer(swapChain, abandonedBackBuffer, &failure) && abandonedBackBuffer.IsValid() &&
              gpu::AbandonBackBuffer(abandonedBackBuffer, &failure),
          "presentation acquisition can be explicitly abandoned");
    gpu::AcquiredBackBuffer backBuffer;
    Check(gpu::AcquireBackBuffer(swapChain, backBuffer, &failure) && gpu::BindCommandList(commandList, &failure) &&
              gpu::TransitionSwapChainPresent(backBuffer, &failure),
          "presentation uses the exact explicitly acquired back buffer");
    gpu::UnbindCommandList();
    Check(gpu::CloseAndSubmitCommandLists("present", commandLists, gpu::CommandListSyncType::None, completion, &failure) &&
              gpu::Present(backBuffer, &failure) && gpu::GetSwapChainStats(swapChain).presentedFrames == 1,
          "presentation remains an explicit native-surface contract");

    gpu::SetResourceDebugName(texture, "Transient.HdrColor");
    gpu::SetResourceDebugName(textureReadback.GetRef(), "Capture.NativeColor");
    gpu::SetResourceDebugName(pipeline, "Opaque.Geometry");
    texture = ownedTexture.Detach();
    static_cast<void>(gpu::SafeRelease(swapChain));
    static_cast<void>(gpu::SafeRelease(bindingLayout));
    static_cast<void>(gpu::SafeRelease(pipeline));
    static_cast<void>(gpu::SafeRelease(descriptorDomain));
    gpu::DestroyQueryPool(queryPool);
    vertexLayout = {};
    static_cast<void>(gpu::SafeRelease(shader));
    static_cast<void>(gpu::SafeRelease(sampler));
    static_cast<void>(gpu::SafeRelease(buffer));
    static_cast<void>(gpu::SafeRelease(texture));
    static_cast<void>(gpu::SafeRelease(heap));
    textureReadback.Reset();
    const gpu::ResourceLifetimeStats beforeIdle = gpu::GetResourceLifetimeStats();
    Check(beforeIdle.liveResources == 0 && beforeIdle.pendingRetirements == 10 && backend.debugNameCount == 4 && backend.retirementTransitionCount == 2 &&
              backend.queryPoolDestroyCount == 2,
          "final release enters fence-safe retirement instead of immediately destroying native resources");
    Check(gpu::RetireResources(&failure) && backend.retireResourcesCount == 1 && gpu::GetResourceLifetimeStats().pendingRetirements == 0,
          "explicit retirement advances backend reclamation independently of a device-idle wait");
    Check(gpu::WaitIdle(&failure) && gpu::GetResourceLifetimeStats().completedRetirements == 10 && gpu::Shutdown(&failure) && !gpu::IsInitialized(),
          "device idle and explicit shutdown complete after retirement without leaking backend state");

    if (g_failures != 0)
        return 1;
    std::puts("[rhiTests] all tests passed");
    return 0;
}
