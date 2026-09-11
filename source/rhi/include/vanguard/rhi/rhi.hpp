#pragma once

#include <vanguard/rhi/rhi_backend.hpp>

namespace vanguard::rhi
{
    [[nodiscard]] bool Initialize(IBackend& backend, const DeviceParams& params = {}, Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool Shutdown(Failure* failure = nullptr) noexcept;
    /// Terminal device-loss teardown. Always detaches global RHI state and never waits for GPU completion.
    [[nodiscard]] bool AbandonDevice(Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool IsInitialized() noexcept;
    [[nodiscard]] const Capabilities& GetCapabilities() noexcept;
    [[nodiscard]] DeviceState TestDeviceState() noexcept;
    [[nodiscard]] bool WaitIdle(Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool RetireResources(Failure* failure = nullptr) noexcept;
    // Rare maintenance barrier for native ownership changes and shutdown; never part of normal frame pacing.
    [[nodiscard]] bool FlushRetiredResources(Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool QueryMemoryBudget(MemorySegment segment, MemoryBudgetSnapshot& budget, Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool SetResidencyPriority(ResourceRef resource, ResidencyPriority priority, Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool SetResidencyPinned(ResourceRef resource, bool pinned, Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool MakeResident(containers::ArraySpan<const ResourceRef> resources, Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool Evict(containers::ArraySpan<const ResourceRef> resources, const ResidencyFenceSet& safeAfter, Failure* failure = nullptr) noexcept;
    [[nodiscard]] ResidencyStats GetResidencyStats() noexcept;

    [[nodiscard]] TextureRef CreateTexture(const TextureDesc& desc, const TextureInitData& initialData = {}, Failure* failure = nullptr) noexcept;
    [[nodiscard]] BufferRef CreateBuffer(const BufferDesc& desc, const BufferInitData& initialData = {}, Failure* failure = nullptr) noexcept;
    [[nodiscard]] HeapRef CreateHeap(const HeapDesc& desc, Failure* failure = nullptr) noexcept;
    [[nodiscard]] BindingLayoutRef RequestBindingLayout(const BindingLayoutDesc& desc, Failure* failure = nullptr) noexcept;
    [[nodiscard]] DescriptorDomainRef CreateDescriptorDomain(const DescriptorDomainDesc& desc, Failure* failure = nullptr) noexcept;
    [[nodiscard]] DescriptorHandle AllocateDescriptor(DescriptorDomainRef domain, Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool WriteDescriptor(DescriptorDomainRef domain, DescriptorHandle descriptor, TextureRef texture, BindingType type, const TextureViewDesc& view = {}, Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool WriteDescriptor(DescriptorDomainRef domain, DescriptorHandle descriptor, BufferRef buffer, BindingType type, const BufferViewDesc& view = {}, Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool WriteDescriptor(DescriptorDomainRef domain, DescriptorHandle descriptor, SamplerStateRef sampler, Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool WriteDescriptor(DescriptorDomainRef domain, DescriptorHandle descriptor, AccelerationStructureRef accelerationStructure, Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool RetireDescriptor(DescriptorDomainRef domain, DescriptorHandle descriptor, const DescriptorRetirement& retirement, Failure* failure = nullptr) noexcept;
    [[nodiscard]] DescriptorDomainStats GetDescriptorDomainStats(DescriptorDomainRef domain) noexcept;
    [[nodiscard]] SamplerStateRef RequestSamplerState(const SamplerStateDesc& desc, Failure* failure = nullptr) noexcept;
    [[nodiscard]] ShaderRef CreateShader(const ShaderDesc& desc, Failure* failure = nullptr) noexcept;
    [[nodiscard]] VertexLayoutRef GetVertexLayout(const VertexLayoutDesc& desc, Failure* failure = nullptr) noexcept;
    [[nodiscard]] PipelineRef CreateGraphicsPipeline(const GraphicsPipelineDesc& desc, Failure* failure = nullptr) noexcept;
    [[nodiscard]] PipelineRef CreateComputePipeline(const ComputePipelineDesc& desc, Failure* failure = nullptr) noexcept;
    [[nodiscard]] PipelineRef CreateRayTracingPipeline(const RayTracingPipelineDesc& desc, Failure* failure = nullptr) noexcept;
    [[nodiscard]] AccelerationStructureRef CreateAccelerationStructure(const AccelerationStructureDesc& desc, Failure* failure = nullptr) noexcept;
    [[nodiscard]] ShaderTableRef CreateShaderTable(const ShaderTableDesc& desc, Failure* failure = nullptr) noexcept;
    [[nodiscard]] u64 GetAccelerationStructureDeviceAddress(AccelerationStructureRef accelerationStructure, Failure* failure = nullptr) noexcept;
    [[nodiscard]] QueryPoolRef CreateQueryPool(const QueryPoolDesc& desc, Failure* failure = nullptr) noexcept;
    void DestroyQueryPool(QueryPoolRef& queryPool) noexcept;
    [[nodiscard]] bool BeginQuery(QueryPoolRef queryPool, u32 index, Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool EndQuery(QueryPoolRef queryPool, u32 index, Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool IssueQuery(QueryPoolRef queryPool, u32 index, Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool ResolveQueries(QueryPoolRef queryPool, u32 start, u32 count, Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool AcquireQueries(QueryPoolRef queryPool, u32 start, u32 count, Failure* failure = nullptr) noexcept;
    void ReleaseQueries(QueryPoolRef queryPool) noexcept;
    [[nodiscard]] bool GetQueryResult(QueryPoolRef queryPool, u32 index, u64& result, Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool GetQueryResult(QueryPoolRef queryPool, u32 index, PipelineStatistics& result, Failure* failure = nullptr) noexcept;
    [[nodiscard]] u64 GetTimestampFrequency(QueueType queue, Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool CalibrateTimestamps(QueueType queue, TimestampCalibration& calibration, Failure* failure = nullptr) noexcept;
    // Binding is a one-time publication operation. The caller exclusively owns the unbound resource until this call returns; a successful placement is immutable for the resource's lifetime.
    [[nodiscard]] bool BindMemory(TextureRef texture, HeapRef heap, u64 offset, Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool BindMemory(BufferRef buffer, HeapRef heap, u64 offset, Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool GetHeapDesc(HeapRef heap, HeapDesc& desc, Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool GetPlacement(TextureRef texture, PlacementRecord& placement, Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool GetPlacement(BufferRef buffer, PlacementRecord& placement, Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool GetTextureDesc(TextureRef texture, TextureDesc& desc, Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool GetBufferDesc(BufferRef buffer, BufferDesc& desc, Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool GetMemoryRequirements(const TextureDesc& desc, MemoryRequirements& requirements, Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool GetMemoryRequirements(const BufferDesc& desc, MemoryRequirements& requirements, Failure* failure = nullptr) noexcept;
    [[nodiscard]] MemoryRequirements GetMemoryRequirements(TextureRef texture) noexcept;
    [[nodiscard]] MemoryRequirements GetMemoryRequirements(BufferRef buffer) noexcept;
    [[nodiscard]] NativeReleaseObservation ObserveNativeRelease(ResourceRef resource, Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool IsNativeReleaseComplete(NativeReleaseObservation observation) noexcept;
    void ReleaseNativeReleaseObservation(NativeReleaseObservation& observation) noexcept;
    [[nodiscard]] bool IsResourceReferenceValid(ResourceRef resource) noexcept;
    [[nodiscard]] ResourceLifetimeStats GetResourceLifetimeStats() noexcept;

    void AddRef(TextureRef resource) noexcept;
    void AddRef(TextureReadbackRef resource) noexcept;
    void AddRef(BufferRef resource) noexcept;
    void AddRef(HeapRef resource) noexcept;
    void AddRef(SamplerStateRef resource) noexcept;
    void AddRef(ShaderRef resource) noexcept;
    void AddRef(PipelineRef resource) noexcept;
    void AddRef(BindingLayoutRef resource) noexcept;
    void AddRef(DescriptorDomainRef resource) noexcept;
    void AddRef(AccelerationStructureRef resource) noexcept;
    void AddRef(ShaderTableRef resource) noexcept;
    void AddRef(SwapChainRef resource) noexcept;

    [[nodiscard]] i32 Release(TextureRef resource) noexcept;
    [[nodiscard]] i32 Release(TextureReadbackRef resource) noexcept;
    [[nodiscard]] i32 Release(BufferRef resource) noexcept;
    [[nodiscard]] i32 Release(HeapRef resource) noexcept;
    [[nodiscard]] i32 Release(SamplerStateRef resource) noexcept;
    [[nodiscard]] i32 Release(ShaderRef resource) noexcept;
    [[nodiscard]] i32 Release(PipelineRef resource) noexcept;
    [[nodiscard]] i32 Release(BindingLayoutRef resource) noexcept;
    [[nodiscard]] i32 Release(DescriptorDomainRef resource) noexcept;
    [[nodiscard]] i32 Release(AccelerationStructureRef resource) noexcept;
    [[nodiscard]] i32 Release(ShaderTableRef resource) noexcept;
    [[nodiscard]] i32 Release(SwapChainRef resource) noexcept;

    template <typename Tag>
        requires IsReferenceCountedResource<Reference<Tag>>
    [[nodiscard]] i32 SafeRelease(Reference<Tag>& resource) noexcept
    {
        const i32 remainingReferences = resource.IsValid() ? Release(resource) : 0;
        resource = {};
        return remainingReferences;
    }
    template <typename Tag>
        requires IsReferenceCountedResource<Reference<Tag>>
    void AddRefIfValid(const Reference<Tag> resource) noexcept
    {
        if (resource.IsValid())
            AddRef(resource);
    }
    template <typename Tag>
        requires IsReferenceCountedResource<Reference<Tag>>
    [[nodiscard]] i32 GetRefCount(const Reference<Tag> resource) noexcept
    {
        if (!resource.IsValid())
            return 0;
        AddRef(resource);
        return Release(resource);
    }
    template <typename Tag>
        requires IsReferenceCountedResource<Reference<Tag>>
    void SafeRefCountAssign(Reference<Tag>& destination, const Reference<Tag> source) noexcept
    {
        if (destination == source)
            return;
        AddRefIfValid(source);
        Reference<Tag> old = destination;
        destination = source;
        static_cast<void>(SafeRelease(old));
    }

    [[nodiscard]] CommandListRef CreateCommandList(CommandListType type, u64 debugHash = 0, Failure* failure = nullptr) noexcept;
    void DiscardCommandList(CommandListRef& commandList) noexcept;
    [[nodiscard]] bool BindCommandList(CommandListRef commandList, Failure* failure = nullptr) noexcept;
    void UnbindCommandList() noexcept;
    [[nodiscard]] CommandListRef GetBoundCommandList() noexcept;
    [[nodiscard]] CommandListType GetBoundCommandListType() noexcept;
    [[nodiscard]] bool CloseCommandList(CommandListRef commandList, Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool SubmitCommandLists(const char* scopeName, containers::ArraySpan<const CommandListRef> commandLists, CommandListSyncType sync, SubmissionReceipt& receipt,
                                          Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool CloseAndSubmitCommandLists(const char* scopeName, containers::ArraySpan<const CommandListRef> commandLists, CommandListSyncType sync, SubmissionReceipt& receipt,
                                                  Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool CloseAndSubmitCommandLists(const char* scopeName, containers::ArraySpan<const CommandListRef> commandLists, CommandListSyncType sync, GpuFence& completion,
                                                  Failure* failure = nullptr) noexcept;
    [[nodiscard]] GpuFence GetGpuFence() noexcept;
    // Snapshot of actual receipts since device initialization. Read after the
    // renderer submission tail joins. Explicit coverage identifies never-used
    // queues; zero-valued fences without that evidence remain incomplete.
    // False means submitted work lost its completion proof; abandonment is needed.
    [[nodiscard]] bool GetSubmittedResidencyFences(ResidencyFenceSet& fences) noexcept;
    [[nodiscard]] bool IsGpuFenceComplete(GpuFence fence) noexcept;
    [[nodiscard]] bool WaitForGpuFence(GpuFence fence, u64 timeoutNanoseconds, Failure* failure = nullptr) noexcept;
    // Declares a resource reached indirectly by GPU-visible data, such as a bindless descriptor index.
    // Direct command operands are tracked automatically; graph and resource-table infrastructure uses this
    // entry point for indirect references before submission.
    [[nodiscard]] bool AddToResidencyWorkingSet(ResourceRef resource, Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool SetPipeline(PipelineRef pipeline, Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool SetupRenderTargets(const RenderTargetSetup& setup, Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool SetVariableRateShading(const VariableRateShadingState& state, Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool SetViewport(const ViewportDesc& viewport, Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool SetScissors(const Rect& rect, Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool BindVertexBuffers(u32 startIndex, containers::ArraySpan<const VertexBufferBinding> bindings, Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool BindIndexBuffer(const IndexBufferBinding& binding, Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool BindIndirectArguments(BufferRef arguments, BufferRef count = {}, Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool SetPushConstants(const void* data, u32 size, Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool ClearColorTarget(TextureRef target, const ColorValue& value, const SubresourceRange& range = {}, const Rect* rectangle = nullptr, Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool ClearDepthTarget(TextureRef target, f32 depth, const SubresourceRange& range = {}, const Rect* rectangle = nullptr, Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool ClearStencilTarget(TextureRef target, u8 stencil, const SubresourceRange& range = {}, const Rect* rectangle = nullptr, Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool ClearDepthStencilTarget(TextureRef target, f32 depth, u8 stencil, const SubresourceRange& range = {}, const Rect* rectangle = nullptr, Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool ClearTextureUav(TextureRef texture, const ColorValue& value, const SubresourceRange& range = {}, Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool ClearTextureUav(TextureRef texture, u32 value, const SubresourceRange& range = {}, Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool ClearBufferUav(BufferRef buffer, u32 value = 0, Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool DiscardTexture(TextureRef texture, const SubresourceRange& range = {}, Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool SetStencilRefValue(u8 value, Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool SetBlendFactor(const ColorValue& value, Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool BeginGpuEvent(const char* name, Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool EndGpuEvent(Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool SetGpuMarker(const char* name, Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool DrawPrimitive(const DrawArguments& arguments, Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool DrawIndexedPrimitive(const DrawIndexedArguments& arguments, Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool DrawPrimitiveIndirect(u64 argumentsOffset, u32 commandCount = 1, Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool DrawIndexedPrimitiveIndirect(u64 argumentsOffset, u32 commandCount = 1, Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool DrawIndexedPrimitiveIndirectCount(u64 argumentsOffset, u64 countOffset, u32 maximumCommandCount, Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool DispatchCompute(u32 groupCountX, u32 groupCountY = 1, u32 groupCountZ = 1, Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool DispatchIndirectCompute(u64 argumentsOffset, Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool BuildBottomLevelAccelerationStructure(AccelerationStructureRef destination, containers::ArraySpan<const RayTracingGeometryDesc> geometries,
                                                             AccelerationStructureBuildMode mode = AccelerationStructureBuildMode::Build, Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool BuildTopLevelAccelerationStructure(AccelerationStructureRef destination, containers::ArraySpan<const RayTracingInstanceDesc> instances,
                                                          AccelerationStructureBuildMode mode = AccelerationStructureBuildMode::Build, Failure* failure = nullptr) noexcept;
    /// The source buffer contains tightly packed RayTracingGpuInstanceDesc records starting at offset.
    [[nodiscard]] bool BuildTopLevelAccelerationStructureIndirect(AccelerationStructureRef destination, BufferRef instanceBuffer, u64 offset, u32 instanceCount,
                                                                  AccelerationStructureBuildMode mode = AccelerationStructureBuildMode::Build, Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool CopyAccelerationStructure(AccelerationStructureRef destination, AccelerationStructureRef source, AccelerationStructureCopyMode mode, Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool WriteAccelerationStructureCompactedSize(AccelerationStructureRef accelerationStructure, QueryPoolRef queryPool, u32 queryIndex, Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool DispatchRays(ShaderTableRef shaderTable, const DispatchRaysArguments& arguments, Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool WriteBuffer(BufferRef buffer, const void* data, u64 size, u64 destinationOffset = 0, Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool WriteTexture(TextureRef texture, const TextureSubresourceData& data, Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool CopyBuffer(BufferRef destination, u64 destinationOffset, BufferRef source, u64 sourceOffset, u64 size, Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool CopyTexture(TextureRef destination, TextureRef source, const TextureCopyRegion& region = {}, Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool ResolveTexture(TextureRef destination, TextureRef source, const TextureResolveRegion& region = {}, Failure* failure = nullptr) noexcept;
    [[nodiscard]] TextureReadbackRef RequestTextureReadback(TextureRef source, const TextureReadbackRegion& region = {}, Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool GetTextureReadbackInfo(TextureReadbackRef readback, TextureReadbackInfo& info, Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool MapTextureReadback(TextureReadbackRef readback, TextureReadbackMapping& mapping, Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool UnmapTextureReadback(TextureReadbackRef readback, Failure* failure = nullptr) noexcept;
    [[nodiscard]] void* LockBuffer(BufferRef buffer, u64 offset, u64 size, Failure* failure = nullptr) noexcept;
    void UnlockBuffer(BufferRef buffer) noexcept;

    // A concrete before state is an assertion against command-list-local tracking. Unknown deliberately skips that assertion.
    // Seed once, before resource recording. Entries are ordered by ResourceRef::value, then slice/mip and cover every subresource of each listed texture.
    // This records no barriers or queue waits. The caller proves entry state and GPU ordering; keepInitialState still controls close-time restoration.
    // Unknown, missing cells, duplicates, stale identities and late/repeated seeding fail. Discard the command list after a seeding failure.
    // Record an already submitted fence dependency. Submission merges requirements
    // by producer queue and inserts GPU waits; this never waits on the CPU.
    [[nodiscard]] bool AddCommandListWait(GpuFence fence, Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool SeedCommandListStates(containers::ArraySpan<const CommandListEntryState> entries, Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool TransitionTexture(TextureRef texture, ResourceState before, ResourceState after, const SubresourceRange& range = {}, Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool TransitionBuffer(BufferRef buffer, ResourceState before, ResourceState after, Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool BarrierTextureUav(TextureRef texture, Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool BarrierBufferUav(BufferRef buffer, Failure* failure = nullptr) noexcept;
    // Activates one placed destination whose complete heap range is covered by
    // the clipped, non-overlapping ranges of the immediate predecessors. The
    // predecessors must be in ascending clipped heap-offset order.
    // Every resource must have completed its one-time BindMemory operation before its reference is published to recording workers.
    // Successful activation makes the destination contents undefined.
    [[nodiscard]] bool ActivateAliasedResource(ResourceRef destination, containers::ArraySpan<const ResourceRef> predecessors, Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool FlushPendingBarriers(Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool MakeStateSafeToRetire(TextureRef texture, Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool MakeStateSafeToRetire(BufferRef buffer, Failure* failure = nullptr) noexcept;

    [[nodiscard]] SwapChainRef CreateSwapChainWithBackBuffer(const SwapChainDesc& desc, Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool ResizeBackbuffer(u32 width, u32 height, SwapChainRef swapChain, Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool SetSwapChainPresentParameters(SwapChainRef swapChain, const PresentParameters& parameters, Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool AcquireBackBuffer(SwapChainRef swapChain, AcquiredBackBuffer& acquisition, Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool AbandonBackBuffer(const AcquiredBackBuffer& acquisition, Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool TransitionSwapChainPresent(const AcquiredBackBuffer& acquisition, Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool Present(const AcquiredBackBuffer& acquisition, Failure* failure = nullptr) noexcept;
    [[nodiscard]] SwapChainStats GetSwapChainStats(SwapChainRef swapChain) noexcept;
    void SetResourceDebugName(TextureRef texture, const char* name) noexcept;
    void SetResourceDebugName(TextureReadbackRef readback, const char* name) noexcept;
    void SetResourceDebugName(BufferRef buffer, const char* name) noexcept;
    void SetResourceDebugName(HeapRef heap, const char* name) noexcept;
    void SetResourceDebugName(SamplerStateRef samplerState, const char* name) noexcept;
    void SetResourceDebugName(ShaderRef shader, const char* name) noexcept;
    void SetResourceDebugName(VertexLayoutRef vertexLayout, const char* name) noexcept;
    void SetResourceDebugName(PipelineRef pipeline, const char* name) noexcept;
    void SetResourceDebugName(AccelerationStructureRef accelerationStructure, const char* name) noexcept;
    void SetResourceDebugName(ShaderTableRef shaderTable, const char* name) noexcept;
    void SetResourceDebugName(QueryPoolRef queryPool, const char* name) noexcept;
    void SetResourceDebugName(CommandListRef commandList, const char* name) noexcept;
    void SetResourceDebugName(SwapChainRef swapChain, const char* name) noexcept;

    struct AdoptReferenceTag
    {
        explicit constexpr AdoptReferenceTag() noexcept = default;
    };
    inline constexpr AdoptReferenceTag AdoptReference{};

    template <typename ReferenceType> struct ResourceFactory;
    template <> struct ResourceFactory<TextureRef>
    {
        template <typename... Args> [[nodiscard]] static TextureRef Create(const Args&... args) noexcept
        {
            return rhi::CreateTexture(args...);
        }
    };
    template <> struct ResourceFactory<BufferRef>
    {
        template <typename... Args> [[nodiscard]] static BufferRef Create(const Args&... args) noexcept
        {
            return rhi::CreateBuffer(args...);
        }
    };

    // Owning intrusive GPU reference. Copying adds a reference, moving transfers it, and final release delegates
    // destruction to the backend's fence-safe retirement queue.
    template <typename ReferenceType>
        requires IsReferenceCountedResource<ReferenceType>
    class Ref final
    {
    public:
        constexpr Ref() noexcept = default;
        constexpr Ref(decltype(nullptr)) noexcept {}
        explicit Ref(const ReferenceType resource) noexcept : m_resource(resource)
        {
            AddRefIfValid(m_resource);
        }
        constexpr Ref(const AdoptReferenceTag, const ReferenceType resource) noexcept : m_resource(resource) {}
        Ref(const Ref& other) noexcept : m_resource(other.m_resource)
        {
            AddRefIfValid(m_resource);
        }
        Ref(Ref&& other) noexcept : m_resource(other.m_resource)
        {
            other.m_resource = {};
        }
        ~Ref()
        {
            static_cast<void>(SafeRelease(m_resource));
        }

        Ref& operator=(const Ref& other) noexcept
        {
            SafeRefCountAssign(m_resource, other.m_resource);
            return *this;
        }
        Ref& operator=(Ref&& other) noexcept
        {
            if (this == &other)
                return *this;
            static_cast<void>(SafeRelease(m_resource));
            m_resource = other.m_resource;
            other.m_resource = {};
            return *this;
        }
        Ref& operator=(decltype(nullptr)) noexcept
        {
            Reset();
            return *this;
        }

        [[nodiscard]] constexpr ReferenceType GetRef() const noexcept
        {
            return m_resource;
        }
        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return m_resource.IsValid();
        }
        [[nodiscard]] constexpr explicit operator bool() const noexcept
        {
            return IsValid();
        }
        [[nodiscard]] constexpr operator ReferenceType() const noexcept
        {
            return m_resource;
        }

        template <typename... Args> [[nodiscard]] static Ref Create(const Args&... args) noexcept
        {
            return Ref(AdoptReference, ResourceFactory<ReferenceType>::Create(args...));
        }

        void Reset() noexcept
        {
            static_cast<void>(SafeRelease(m_resource));
        }
        void Reset(const ReferenceType resource) noexcept
        {
            SafeRefCountAssign(m_resource, resource);
        }
        [[nodiscard]] ReferenceType Detach() noexcept
        {
            const ReferenceType resource = m_resource;
            m_resource = {};
            return resource;
        }
        void Swap(Ref& other) noexcept
        {
            const ReferenceType temporary = m_resource;
            m_resource = other.m_resource;
            other.m_resource = temporary;
        }

    private:
        ReferenceType m_resource{};
    };

    using Texture = Ref<TextureRef>;
    using TextureReadback = Ref<TextureReadbackRef>;
    using Buffer = Ref<BufferRef>;
    using Heap = Ref<HeapRef>;
    using SamplerState = Ref<SamplerStateRef>;
    using Shader = Ref<ShaderRef>;
    using Pipeline = Ref<PipelineRef>;
    using BindingLayout = Ref<BindingLayoutRef>;
    using DescriptorDomain = Ref<DescriptorDomainRef>;
    using AccelerationStructure = Ref<AccelerationStructureRef>;
    using ShaderTable = Ref<ShaderTableRef>;
    using SwapChain = Ref<SwapChainRef>;

    // Query pools have unique ownership and are intentionally separate from shared GPU-resource references.
    class QueryPool final
    {
    public:
        constexpr QueryPool() noexcept = default;
        constexpr QueryPool(const AdoptReferenceTag, const QueryPoolRef queryPool) noexcept : m_queryPool(queryPool) {}
        QueryPool(QueryPool&& other) noexcept : m_queryPool(other.m_queryPool)
        {
            other.m_queryPool = {};
        }
        QueryPool& operator=(QueryPool&& other) noexcept
        {
            if (this == &other)
                return *this;
            Reset();
            m_queryPool = other.m_queryPool;
            other.m_queryPool = {};
            return *this;
        }
        ~QueryPool()
        {
            Reset();
        }

        QueryPool(const QueryPool&) = delete;
        QueryPool& operator=(const QueryPool&) = delete;

        [[nodiscard]] constexpr QueryPoolRef GetRef() const noexcept
        {
            return m_queryPool;
        }
        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return m_queryPool.IsValid();
        }
        [[nodiscard]] constexpr explicit operator bool() const noexcept
        {
            return IsValid();
        }
        void Reset() noexcept
        {
            DestroyQueryPool(m_queryPool);
        }
        [[nodiscard]] QueryPoolRef Detach() noexcept
        {
            const QueryPoolRef result = m_queryPool;
            m_queryPool = {};
            return result;
        }

    private:
        QueryPoolRef m_queryPool{};
    };
} // namespace vanguard::rhi
