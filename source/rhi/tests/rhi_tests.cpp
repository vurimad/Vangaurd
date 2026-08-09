#include <vanguard/rhi/rhi.hpp>

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
        gpu::BackendStatus RetireDescriptor(gpu::DescriptorDomainRef, gpu::DescriptorHandle,
                                            const gpu::DescriptorRetirement&) noexcept override
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
        gpu::QueryPoolRef CreateQueryPool(const gpu::QueryPoolDesc&) noexcept override
        {
            queryPoolAlive = true;
            return {40, 1};
        }
        void DestroyQueryPool(const gpu::QueryPoolRef queryPool) noexcept override
        {
            if (queryPool == gpu::QueryPoolRef{40, 1} && queryPoolAlive)
            {
                queryPoolAlive = false;
                ++queryPoolDestroyCount;
            }
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
            const vanguard::u32 kind = static_cast<vanguard::u32>(resource.Kind());
            return resource.IsValid() && kind < ResourceKindCount && resource.Index() == kind && resource.Generation() == 1 &&
                   (resource.Kind() == gpu::ResourceKind::VertexLayout || refCounts[kind] != 0);
        }
        void AddRef(const gpu::ResourceRef resource) noexcept override
        {
            if (!IsResourceReferenceValid(resource))
                return;
            ++refCounts[static_cast<vanguard::u32>(resource.Kind())];
            ++lifetimeStats.totalReferences;
        }
        vanguard::i32 Release(const gpu::ResourceRef resource) noexcept override
        {
            if (!IsResourceReferenceValid(resource))
                return 0;
            const vanguard::u32 kind = static_cast<vanguard::u32>(resource.Kind());
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
        gpu::BackendStatus CloseAndSubmitCommandLists(const char*, vanguard::containers::ArraySpan<const gpu::CommandListRef>,
                                                      gpu::CommandListSyncType, gpu::GpuFence& completion) noexcept override
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
        gpu::BackendStatus SetPipeline(gpu::CommandListRef, gpu::PipelineRef) noexcept override
        {
            ++setPipelineCount;
            return {};
        }
        gpu::BackendStatus SetupRenderTargets(gpu::CommandListRef, const gpu::RenderTargetSetup&) noexcept override
        {
            return {};
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
        gpu::BackendStatus DrawIndexedPrimitiveIndirectCount(gpu::CommandListRef, vanguard::u64, vanguard::u64,
                                                             vanguard::u32) noexcept override
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
        gpu::BackendStatus WriteBuffer(gpu::CommandListRef, gpu::BufferRef, const void*, vanguard::u64, vanguard::u64) noexcept override
        {
            return {};
        }
        gpu::BackendStatus WriteTexture(gpu::CommandListRef, gpu::TextureRef, const gpu::TextureSubresourceData&) noexcept override
        {
            return {};
        }
        gpu::BackendStatus CopyBuffer(gpu::CommandListRef, gpu::BufferRef, vanguard::u64, gpu::BufferRef, vanguard::u64,
                                      vanguard::u64) noexcept override
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
        gpu::TextureRef GetBackBufferTexture(gpu::SwapChainRef) noexcept override
        {
            return {1, 1};
        }
        gpu::BackendStatus TransitionSwapChainPresent(gpu::CommandListRef, gpu::SwapChainRef) noexcept override
        {
            ++barrierCount;
            return {};
        }
        gpu::BackendStatus Present(gpu::SwapChainRef) noexcept override
        {
            ++presentCount;
            return {};
        }
        void SetResourceDebugName(gpu::TextureRef, const char*) noexcept override
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
        vanguard::u32 setPipelineCount = 0;
        vanguard::u32 drawCount = 0;
        vanguard::u32 dispatchCount = 0;
        vanguard::u32 barrierCount = 0;
        vanguard::u32 flushBarrierCount = 0;
        vanguard::u32 retirementTransitionCount = 0;
        vanguard::u32 presentCount = 0;
        vanguard::u32 debugNameCount = 0;
        vanguard::u32 queryPoolDestroyCount = 0;

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
    };
} // namespace

int main()
{
    gpu::Failure failure{};
    gpu::BufferDesc invalidBuffer{};
    Check(!gpu::CreateBuffer(invalidBuffer, {}, &failure).IsValid() && failure.code == gpu::FailureCode::NotInitialized,
          "resource creation reports use before initialization");

    FakeBackend backend;
    Check(gpu::Initialize(backend, {}, &failure) && gpu::IsInitialized(), "RHI initializes through its backend boundary");
    Check(gpu::GetCapabilities().resourceAliasing && gpu::TestDeviceState() == gpu::DeviceState::Operational,
          "validated capabilities and device state are exposed");

    gpu::TextureDesc textureDesc{};
    textureDesc.extent = {1920, 1080, 1};
    textureDesc.format = gpu::Format::R16G16B16A16Float;
    textureDesc.usage = gpu::TextureUsage::ShaderResource | gpu::TextureUsage::UnorderedAccess;
    textureDesc.virtualResource = true;
    gpu::Texture ownedTexture = gpu::Texture::Create(textureDesc, gpu::TextureInitData{}, &failure);
    gpu::TextureRef texture = ownedTexture.GetRef();
    gpu::BufferDesc bufferDesc{};
    bufferDesc.size = 1024 * 1024;
    bufferDesc.usage = gpu::BufferUsage::Structured | gpu::BufferUsage::UnorderedAccess;
    bufferDesc.virtualResource = true;
    gpu::BufferRef buffer = gpu::CreateBuffer(bufferDesc, {}, &failure);
    gpu::HeapRef heap = gpu::CreateHeap({2 * 1024 * 1024, 65536, 7, gpu::MemoryType::DeviceLocal}, &failure);
    Check(texture.IsValid() && buffer.IsValid() && heap.IsValid(), "typed resources and transient heaps are created");
    Check(gpu::BindMemory(texture, heap, 0, &failure) && gpu::BindMemory(buffer, heap, 65536, &failure) && backend.bindCount == 2,
          "virtual resources bind to explicit heap placements");

    const gpu::ResourceRef genericTexture = texture;
    Check(sizeof(gpu::ResourceRef) == sizeof(gpu::TextureRef) && genericTexture.Kind() == gpu::ResourceKind::Texture &&
              gpu::CastResourceRef<gpu::TextureRef>(genericTexture) == texture &&
              !gpu::CastResourceRef<gpu::BufferRef>(genericTexture).IsValid(),
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
    Check(
        !gpu::CreateDescriptorDomain({gpu::DescriptorDomainKind::Samplers, 257, 0, gpu::ShaderStageBit(gpu::ShaderStage::Pixel)}, &failure)
                .IsValid() &&
            failure.code == gpu::FailureCode::InvalidArgument,
        "descriptor domains enforce class-specific hardware capacity limits");
    gpu::DescriptorDomainRef descriptorDomain =
        gpu::CreateDescriptorDomain({gpu::DescriptorDomainKind::Resources, 1024, 0,
                                     gpu::ShaderStageBit(gpu::ShaderStage::Vertex) | gpu::ShaderStageBit(gpu::ShaderStage::Pixel)},
                                    &failure);
    const gpu::DescriptorHandle textureDescriptor = gpu::AllocateDescriptor(descriptorDomain, &failure);
    Check(sampler.IsValid() && shader.IsValid() && vertexLayout.IsValid() && queryPool.IsValid() && bindingLayout.IsValid() &&
              pipeline.IsValid() && descriptorDomain.IsValid() && textureDescriptor.IsValid() &&
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
    gpu::RenderTargetSetup renderTargets{};
    renderTargets.colorTargets[0].texture = texture;
    renderTargets.colorTargetCount = 1;
    const gpu::VertexBufferBinding vertexBufferBinding{buffer, 0, 0};
    const gpu::IndexBufferBinding indexBufferBinding{buffer, 0, gpu::IndexFormat::UInt16};
    const vanguard::u32 pushConstants[] = {1, 2, 3, 4};
    Check(gpu::SetPipeline(pipeline, &failure) && gpu::SetupRenderTargets(renderTargets, &failure) &&
              gpu::SetViewport({0.0f, 0.0f, 1280.0f, 720.0f, 0.0f, 1.0f}, &failure) && gpu::SetScissors({0, 0, 1280, 720}, &failure) &&
              gpu::BindVertexBuffers(0, {&vertexBufferBinding, 1}, &failure) && gpu::BindIndexBuffer(indexBufferBinding, &failure) &&
              gpu::BindIndirectArguments(buffer, buffer, &failure) &&
              gpu::SetPushConstants(pushConstants, sizeof(pushConstants), &failure) && gpu::DrawPrimitive({3, 1, 0, 0}, &failure) &&
              gpu::DrawIndexedPrimitive({3, 1, 0, 0, 0}, &failure) && gpu::DrawPrimitiveIndirect(0, 1, &failure) &&
              gpu::DrawIndexedPrimitiveIndirect(0, 1, &failure) && gpu::DrawIndexedPrimitiveIndirectCount(0, 0, 1, &failure) &&
              gpu::DispatchCompute(1, 1, 1, &failure) && gpu::DispatchIndirectCompute(0, &failure) && backend.setPipelineCount == 1 &&
              backend.drawCount == 5 && backend.dispatchCount == 2,
          "draw, indirect, push-constant and compute commands route through command-list-local state");
    Check(gpu::TransitionTexture(texture, gpu::ResourceState::Common, gpu::ResourceState::UnorderedAccess, {}, &failure) &&
              gpu::BarrierTextureUav(texture, &failure) &&
              gpu::TransitionBuffer(buffer, gpu::ResourceState::Common, gpu::ResourceState::UnorderedAccess, &failure) &&
              gpu::BarrierBufferAliasing(true, buffer, {}, &failure) && gpu::MakeStateSafeToRetire(texture, &failure) &&
              gpu::MakeStateSafeToRetire(buffer, &failure) && gpu::FlushPendingBarriers(&failure),
          "explicit transitions, UAV ordering, aliasing and barrier flush route through the bound command list");
    gpu::CommandListRef commandLists[] = {commandList};
    gpu::GpuFence completion{};
    Check(!gpu::CloseAndSubmitCommandLists("bound rejection", commandLists, gpu::CommandListSyncType::None, completion, &failure) &&
              failure.code == gpu::FailureCode::InvalidCommandList,
          "a bound command list cannot be submitted implicitly");
    gpu::UnbindCommandList();
    Check(gpu::CloseAndSubmitCommandLists("frame", commandLists, gpu::CommandListSyncType::None, completion, &failure) &&
              completion.IsValid() && gpu::IsGpuFenceComplete(completion) && gpu::WaitForGpuFence(completion, 1000, &failure),
          "submission produces an explicit queue timeline fence");

    int nativeWindow = 0;
    gpu::SwapChainDesc swapChainDesc{};
    swapChainDesc.surface = {gpu::PresentationSurfaceKind::Win32, &nativeWindow, nullptr};
    swapChainDesc.width = 1280;
    swapChainDesc.height = 720;
    gpu::SwapChainRef swapChain = gpu::CreateSwapChainWithBackBuffer(swapChainDesc, &failure);
    Check(swapChain.IsValid() && gpu::GetBackBufferTexture(swapChain).IsValid() && gpu::Present(swapChain, &failure),
          "presentation remains an explicit native-surface contract");

    gpu::SetResourceDebugName(texture, "Transient.HdrColor");
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
    const gpu::ResourceLifetimeStats beforeIdle = gpu::GetResourceLifetimeStats();
    Check(beforeIdle.liveResources == 0 && beforeIdle.pendingRetirements == 9 && backend.debugNameCount == 2 &&
              backend.retirementTransitionCount == 2 && backend.queryPoolDestroyCount == 1,
          "final release enters fence-safe retirement instead of immediately destroying native resources");
    Check(gpu::RetireResources(&failure) && backend.retireResourcesCount == 1 && gpu::GetResourceLifetimeStats().pendingRetirements == 0,
          "explicit retirement advances backend reclamation independently of a device-idle wait");
    Check(gpu::WaitIdle(&failure) && gpu::GetResourceLifetimeStats().completedRetirements == 9 && gpu::Shutdown(&failure) &&
              !gpu::IsInitialized(),
          "device idle and explicit shutdown complete after retirement without leaking backend state");

    if (g_failures != 0)
        return 1;
    std::puts("[rhiTests] all tests passed");
    return 0;
}
