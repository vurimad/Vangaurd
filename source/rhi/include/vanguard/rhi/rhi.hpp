#pragma once

#include <vanguard/rhi/rhi_backend.hpp>

namespace vanguard::rhi
{
    [[nodiscard]] bool Initialize(IBackend& backend, const DeviceParams& params = {}, Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool Shutdown(Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool IsInitialized() noexcept;
    [[nodiscard]] const Capabilities& GetCapabilities() noexcept;
    [[nodiscard]] DeviceState TestDeviceState() noexcept;
    [[nodiscard]] bool WaitIdle(Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool RetireResources(Failure* failure = nullptr) noexcept;

    [[nodiscard]] TextureRef CreateTexture(const TextureDesc& desc, const TextureInitData& initialData = {},
                                           Failure* failure = nullptr) noexcept;
    [[nodiscard]] BufferRef CreateBuffer(const BufferDesc& desc, const BufferInitData& initialData = {},
                                         Failure* failure = nullptr) noexcept;
    [[nodiscard]] HeapRef CreateHeap(const HeapDesc& desc, Failure* failure = nullptr) noexcept;
    [[nodiscard]] BindingLayoutRef RequestBindingLayout(const BindingLayoutDesc& desc, Failure* failure = nullptr) noexcept;
    [[nodiscard]] DescriptorDomainRef CreateDescriptorDomain(const DescriptorDomainDesc& desc, Failure* failure = nullptr) noexcept;
    [[nodiscard]] DescriptorHandle AllocateDescriptor(DescriptorDomainRef domain, Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool WriteDescriptor(DescriptorDomainRef domain, DescriptorHandle descriptor, TextureRef texture, BindingType type,
                                       const TextureViewDesc& view = {}, Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool WriteDescriptor(DescriptorDomainRef domain, DescriptorHandle descriptor, BufferRef buffer, BindingType type,
                                       const BufferViewDesc& view = {}, Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool WriteDescriptor(DescriptorDomainRef domain, DescriptorHandle descriptor, SamplerStateRef sampler,
                                       Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool RetireDescriptor(DescriptorDomainRef domain, DescriptorHandle descriptor, const DescriptorRetirement& retirement,
                                        Failure* failure = nullptr) noexcept;
    [[nodiscard]] DescriptorDomainStats GetDescriptorDomainStats(DescriptorDomainRef domain) noexcept;
    [[nodiscard]] SamplerStateRef RequestSamplerState(const SamplerStateDesc& desc, Failure* failure = nullptr) noexcept;
    [[nodiscard]] ShaderRef CreateShader(const ShaderDesc& desc, Failure* failure = nullptr) noexcept;
    [[nodiscard]] VertexLayoutRef GetVertexLayout(const VertexLayoutDesc& desc, Failure* failure = nullptr) noexcept;
    [[nodiscard]] PipelineRef CreateGraphicsPipeline(const GraphicsPipelineDesc& desc, Failure* failure = nullptr) noexcept;
    [[nodiscard]] PipelineRef CreateComputePipeline(const ComputePipelineDesc& desc, Failure* failure = nullptr) noexcept;
    [[nodiscard]] PipelineRef CreateRayTracingPipeline(const RayTracingPipelineDesc& desc, Failure* failure = nullptr) noexcept;
    [[nodiscard]] QueryPoolRef CreateQueryPool(const QueryPoolDesc& desc, Failure* failure = nullptr) noexcept;
    void DestroyQueryPool(QueryPoolRef& queryPool) noexcept;
    [[nodiscard]] bool BindMemory(TextureRef texture, HeapRef heap, u64 offset, Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool BindMemory(BufferRef buffer, HeapRef heap, u64 offset, Failure* failure = nullptr) noexcept;
    [[nodiscard]] MemoryRequirements GetMemoryRequirements(TextureRef texture) noexcept;
    [[nodiscard]] MemoryRequirements GetMemoryRequirements(BufferRef buffer) noexcept;
    [[nodiscard]] bool IsResourceReferenceValid(ResourceRef resource) noexcept;
    [[nodiscard]] ResourceLifetimeStats GetResourceLifetimeStats() noexcept;

    void AddRef(TextureRef resource) noexcept;
    void AddRef(BufferRef resource) noexcept;
    void AddRef(HeapRef resource) noexcept;
    void AddRef(SamplerStateRef resource) noexcept;
    void AddRef(ShaderRef resource) noexcept;
    void AddRef(PipelineRef resource) noexcept;
    void AddRef(BindingLayoutRef resource) noexcept;
    void AddRef(DescriptorDomainRef resource) noexcept;
    void AddRef(AccelerationStructureRef resource) noexcept;
    void AddRef(SwapChainRef resource) noexcept;

    [[nodiscard]] i32 Release(TextureRef resource) noexcept;
    [[nodiscard]] i32 Release(BufferRef resource) noexcept;
    [[nodiscard]] i32 Release(HeapRef resource) noexcept;
    [[nodiscard]] i32 Release(SamplerStateRef resource) noexcept;
    [[nodiscard]] i32 Release(ShaderRef resource) noexcept;
    [[nodiscard]] i32 Release(PipelineRef resource) noexcept;
    [[nodiscard]] i32 Release(BindingLayoutRef resource) noexcept;
    [[nodiscard]] i32 Release(DescriptorDomainRef resource) noexcept;
    [[nodiscard]] i32 Release(AccelerationStructureRef resource) noexcept;
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
    [[nodiscard]] bool CloseAndSubmitCommandLists(const char* scopeName, containers::ArraySpan<const CommandListRef> commandLists,
                                                  CommandListSyncType sync, GpuFence& completion, Failure* failure = nullptr) noexcept;
    [[nodiscard]] GpuFence GetGpuFence() noexcept;
    [[nodiscard]] bool IsGpuFenceComplete(GpuFence fence) noexcept;
    [[nodiscard]] bool WaitForGpuFence(GpuFence fence, u64 timeoutNanoseconds, Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool SetPipeline(PipelineRef pipeline, Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool SetupRenderTargets(const RenderTargetSetup& setup, Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool SetViewport(const ViewportDesc& viewport, Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool SetScissors(const Rect& rect, Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool BindVertexBuffers(u32 startIndex, containers::ArraySpan<const VertexBufferBinding> bindings,
                                         Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool BindIndexBuffer(const IndexBufferBinding& binding, Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool BindIndirectArguments(BufferRef arguments, BufferRef count = {}, Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool SetPushConstants(const void* data, u32 size, Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool DrawPrimitive(const DrawArguments& arguments, Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool DrawIndexedPrimitive(const DrawIndexedArguments& arguments, Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool DrawPrimitiveIndirect(u64 argumentsOffset, u32 commandCount = 1, Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool DrawIndexedPrimitiveIndirect(u64 argumentsOffset, u32 commandCount = 1, Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool DrawIndexedPrimitiveIndirectCount(u64 argumentsOffset, u64 countOffset, u32 maximumCommandCount,
                                                         Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool DispatchCompute(u32 groupCountX, u32 groupCountY = 1, u32 groupCountZ = 1, Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool DispatchIndirectCompute(u64 argumentsOffset, Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool WriteBuffer(BufferRef buffer, const void* data, u64 size, u64 destinationOffset = 0,
                                   Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool WriteTexture(TextureRef texture, const TextureSubresourceData& data, Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool CopyBuffer(BufferRef destination, u64 destinationOffset, BufferRef source, u64 sourceOffset, u64 size,
                                  Failure* failure = nullptr) noexcept;
    [[nodiscard]] void* LockBuffer(BufferRef buffer, u64 offset, u64 size, Failure* failure = nullptr) noexcept;
    void UnlockBuffer(BufferRef buffer) noexcept;

    [[nodiscard]] bool TransitionTexture(TextureRef texture, ResourceState before, ResourceState after, const SubresourceRange& range = {},
                                         Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool TransitionBuffer(BufferRef buffer, ResourceState before, ResourceState after, Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool BarrierTextureUav(TextureRef texture, Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool BarrierBufferUav(BufferRef buffer, Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool BarrierTextureAliasing(bool discardAfter, TextureRef textureAfter, TextureRef textureBefore = {},
                                              Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool BarrierBufferAliasing(bool discardAfter, BufferRef bufferAfter, BufferRef bufferBefore = {},
                                             Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool FlushPendingBarriers(Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool MakeStateSafeToRetire(TextureRef texture, Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool MakeStateSafeToRetire(BufferRef buffer, Failure* failure = nullptr) noexcept;

    [[nodiscard]] SwapChainRef CreateSwapChainWithBackBuffer(const SwapChainDesc& desc, Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool ResizeBackbuffer(u32 width, u32 height, SwapChainRef swapChain, Failure* failure = nullptr) noexcept;
    [[nodiscard]] TextureRef GetBackBufferTexture(SwapChainRef swapChain) noexcept;
    [[nodiscard]] bool TransitionSwapChainPresent(SwapChainRef swapChain, Failure* failure = nullptr) noexcept;
    [[nodiscard]] bool Present(SwapChainRef swapChain, Failure* failure = nullptr) noexcept;
    void SetResourceDebugName(TextureRef texture, const char* name) noexcept;
    void SetResourceDebugName(BufferRef buffer, const char* name) noexcept;
    void SetResourceDebugName(HeapRef heap, const char* name) noexcept;
    void SetResourceDebugName(SamplerStateRef samplerState, const char* name) noexcept;
    void SetResourceDebugName(ShaderRef shader, const char* name) noexcept;
    void SetResourceDebugName(VertexLayoutRef vertexLayout, const char* name) noexcept;
    void SetResourceDebugName(PipelineRef pipeline, const char* name) noexcept;
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
    using Buffer = Ref<BufferRef>;
    using Heap = Ref<HeapRef>;
    using SamplerState = Ref<SamplerStateRef>;
    using Shader = Ref<ShaderRef>;
    using Pipeline = Ref<PipelineRef>;
    using BindingLayout = Ref<BindingLayoutRef>;
    using DescriptorDomain = Ref<DescriptorDomainRef>;
    using AccelerationStructure = Ref<AccelerationStructureRef>;
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
