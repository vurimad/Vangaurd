#include <vanguard/rhi/d3d12/backend.hpp>

#include <vanguard/diagnostics/diagnostics.hpp>
#include <vanguard/memory/memory.hpp>
#include <vanguard/rhi/backend/common_backend.hpp>
#include <nvrhi/d3d12.h>

#include <dxgi1_6.h>
#include <new>

namespace vanguard::rhi::d3d12
{
    namespace
    {
        constexpr BackendStatus NativeOperationUnavailable() noexcept
        {
            return BackendStatus::Failure(FailureCode::Unsupported, 0, "D3D12 native operation is not implemented in this increment");
        }

        template <typename Type> void ReleaseNative(Type*& object) noexcept
        {
            if (object == nullptr)
                return;
            object->Release();
            object = nullptr;
        }

        DeviceVendor GetVendor(const u32 vendorId) noexcept
        {
            if (vendorId == 0x10deu)
                return DeviceVendor::Nvidia;
            if (vendorId == 0x1002u || vendorId == 0x1022u)
                return DeviceVendor::Amd;
            if (vendorId == 0x8086u)
                return DeviceVendor::Intel;
            return DeviceVendor::Unknown;
        }

        void CopyAdapterName(char* const destination, const u32 capacity, const wchar_t* const source) noexcept
        {
            if (destination == nullptr || capacity == 0)
                return;
            destination[0] = '\0';
            if (source == nullptr)
                return;
            const int result = WideCharToMultiByte(CP_UTF8, 0, source, -1, destination, static_cast<int>(capacity), nullptr, nullptr);
            if (result == 0)
                destination[0] = '\0';
            destination[capacity - 1u] = '\0';
        }
    } // namespace

    struct Backend::Impl final
    {
        IDXGIFactory6* factory = nullptr;
        IDXGIAdapter1* adapter = nullptr;
        ID3D12Device* nativeDevice = nullptr;
        ID3D12CommandQueue* graphicsQueue = nullptr;
        ID3D12CommandQueue* computeQueue = nullptr;
        ID3D12CommandQueue* copyQueue = nullptr;
        ID3D12Fence* queueFences[3]{};
        backend::CommonBackend common;
        Capabilities capabilities{};
        bool initialized = false;

        void ReleaseDeviceObjects() noexcept
        {
            ReleaseNative(queueFences[2]);
            ReleaseNative(queueFences[1]);
            ReleaseNative(queueFences[0]);
            ReleaseNative(copyQueue);
            ReleaseNative(computeQueue);
            ReleaseNative(graphicsQueue);
            ReleaseNative(nativeDevice);
            ReleaseNative(adapter);
            ReleaseNative(factory);
            capabilities = {};
            initialized = false;
        }

        [[nodiscard]] static bool IsFenceComplete(void* const context, const QueueType queue, const u64 value) noexcept
        {
            const Impl& impl = *static_cast<const Impl*>(context);
            ID3D12Fence* const fence = impl.queueFences[static_cast<u32>(queue)];
            return value == 0 || (fence != nullptr && fence->GetCompletedValue() >= value);
        }

        [[nodiscard]] static bool SignalFence(void* const context, const QueueType queue, const u64 value) noexcept
        {
            Impl& impl = *static_cast<Impl*>(context);
            ID3D12CommandQueue* const nativeQueue = queue == QueueType::Graphics  ? impl.graphicsQueue
                                                    : queue == QueueType::Compute ? impl.computeQueue
                                                                                  : impl.copyQueue;
            ID3D12Fence* const fence = impl.queueFences[static_cast<u32>(queue)];
            return nativeQueue != nullptr && fence != nullptr && SUCCEEDED(nativeQueue->Signal(fence, value));
        }

        [[nodiscard]] static bool WaitFence(void* const context, const QueueType queue, const u64 value,
                                            const u64 timeoutNanoseconds) noexcept
        {
            Impl& impl = *static_cast<Impl*>(context);
            ID3D12Fence* const fence = impl.queueFences[static_cast<u32>(queue)];
            if (fence == nullptr)
                return false;
            if (fence->GetCompletedValue() >= value)
                return true;
            HANDLE eventHandle = CreateEventW(nullptr, FALSE, FALSE, nullptr);
            if (eventHandle == nullptr)
                return false;
            const HRESULT eventResult = fence->SetEventOnCompletion(value, eventHandle);
            if (FAILED(eventResult))
            {
                CloseHandle(eventHandle);
                return false;
            }
            constexpr u64 NanosecondsPerMillisecond = 1000000u;
            const u64 roundedMilliseconds =
                timeoutNanoseconds / NanosecondsPerMillisecond + (timeoutNanoseconds % NanosecondsPerMillisecond != 0 ? 1u : 0u);
            const DWORD timeoutMilliseconds =
                timeoutNanoseconds == ~u64{0}
                    ? INFINITE
                    : static_cast<DWORD>(roundedMilliseconds > INFINITE - 1u ? INFINITE - 1u : roundedMilliseconds);
            const DWORD waitResult = WaitForSingleObject(eventHandle, timeoutMilliseconds);
            CloseHandle(eventHandle);
            return waitResult == WAIT_OBJECT_0;
        }

        [[nodiscard]] static bool AliasingBarrier(void*, nvrhi::ICommandList* const commandList, nvrhi::IResource* const resourceAfter,
                                                  nvrhi::IResource* const resourceBefore, const bool discardAfter) noexcept
        {
            if (commandList == nullptr || resourceAfter == nullptr)
                return false;
            ID3D12GraphicsCommandList* const nativeCommandList =
                commandList->getNativeObject(nvrhi::ObjectTypes::D3D12_GraphicsCommandList);
            ID3D12Resource* const nativeAfter = resourceAfter->getNativeObject(nvrhi::ObjectTypes::D3D12_Resource);
            ID3D12Resource* const nativeBefore =
                resourceBefore != nullptr ? resourceBefore->getNativeObject(nvrhi::ObjectTypes::D3D12_Resource) : nullptr;
            if (nativeCommandList == nullptr || nativeAfter == nullptr || (resourceBefore != nullptr && nativeBefore == nullptr))
                return false;
            D3D12_RESOURCE_BARRIER barrier{};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_ALIASING;
            barrier.Aliasing.pResourceBefore = nativeBefore;
            barrier.Aliasing.pResourceAfter = nativeAfter;
            nativeCommandList->ResourceBarrier(1, &barrier);
            if (discardAfter)
                nativeCommandList->DiscardResource(nativeAfter, nullptr);
            return true;
        }
    };

    Backend::~Backend()
    {
        if (m_impl == nullptr)
            return;
        if (m_impl->initialized)
        {
            const BackendStatus shutdown = Shutdown();
            if (!shutdown)
            {
                VG_LOG_ERROR(diagnostics::Category::Rendering,
                             "D3D12 backend required emergency teardown because live GPU resource owners remained");
                static_cast<void>(WaitIdle());
                m_impl->common.ForceShutdownAfterGpuIdle();
                m_impl->ReleaseDeviceObjects();
            }
        }
        m_impl->~Impl();
        memory::MemoryBlock block{m_impl, sizeof(Impl), memory::PoolId::Rendering};
        memory::Free(block);
        m_impl = nullptr;
    }

    BackendStatus Backend::Initialize(const DeviceParams& params, Capabilities& capabilities) noexcept
    {
        capabilities = {};
        if (m_impl != nullptr && m_impl->initialized)
            return BackendStatus::Failure(FailureCode::AlreadyInitialized, 0, "D3D12 backend is already initialized");
        if (!memory::IsInitialized())
            return BackendStatus::Failure(FailureCode::NotInitialized, 0, "Vanguard memory must be initialized before the D3D12 backend");
        if (m_impl == nullptr)
        {
            memory::MemoryBlock block = memory::Allocate(memory::PoolId::Rendering, sizeof(Impl), alignof(Impl));
            if (!block)
                return BackendStatus::Failure(FailureCode::OutOfMemory, 0, "could not allocate D3D12 backend state");
            m_impl = new (block.address) Impl();
        }

        UINT factoryFlags = 0;
#if VG_ENABLE_ASSERTS
        if (params.enableValidation)
        {
            ID3D12Debug* debug = nullptr;
            if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))
            {
                debug->EnableDebugLayer();
                factoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
                debug->Release();
            }
            else
            {
                VG_LOG_WARNING(diagnostics::Category::Rendering, "D3D12 validation was requested but the debug layer is unavailable");
            }
        }
#else
        static_cast<void>(params.enableValidation);
#endif

        HRESULT result = CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&m_impl->factory));
        if (FAILED(result))
        {
            m_impl->ReleaseDeviceObjects();
            return BackendStatus::Failure(FailureCode::BackendFailure, result, "CreateDXGIFactory2 failed");
        }

        const DXGI_GPU_PREFERENCE preference =
            params.preferHighPerformanceAdapter ? DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE : DXGI_GPU_PREFERENCE_UNSPECIFIED;
        u32 compatibleIndex = 0;
        for (u32 enumerationIndex = 0;; ++enumerationIndex)
        {
            IDXGIAdapter1* candidate = nullptr;
            result = m_impl->factory->EnumAdapterByGpuPreference(enumerationIndex, preference, IID_PPV_ARGS(&candidate));
            if (result == DXGI_ERROR_NOT_FOUND)
                break;
            if (FAILED(result))
            {
                m_impl->ReleaseDeviceObjects();
                return BackendStatus::Failure(FailureCode::BackendFailure, result, "DXGI adapter enumeration failed");
            }

            DXGI_ADAPTER_DESC1 candidateDesc{};
            candidate->GetDesc1(&candidateDesc);
            const bool hardware = (candidateDesc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0;
            const bool supported =
                hardware && SUCCEEDED(D3D12CreateDevice(candidate, D3D_FEATURE_LEVEL_12_0, __uuidof(ID3D12Device), nullptr));
            if (supported && compatibleIndex++ == params.adapterIndex)
            {
                m_impl->adapter = candidate;
                break;
            }
            candidate->Release();
        }
        if (m_impl->adapter == nullptr)
        {
            m_impl->ReleaseDeviceObjects();
            return BackendStatus::Failure(FailureCode::Unsupported, 0, "requested D3D12 hardware adapter was not found");
        }

        result = D3D12CreateDevice(m_impl->adapter, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&m_impl->nativeDevice));
        if (FAILED(result))
        {
            m_impl->ReleaseDeviceObjects();
            return BackendStatus::Failure(FailureCode::Unsupported, result, "D3D12 feature level 12.0 is unavailable");
        }

        const D3D12_COMMAND_QUEUE_DESC graphicsDesc{D3D12_COMMAND_LIST_TYPE_DIRECT, D3D12_COMMAND_QUEUE_PRIORITY_NORMAL,
                                                    D3D12_COMMAND_QUEUE_FLAG_NONE, 0};
        const D3D12_COMMAND_QUEUE_DESC computeDesc{D3D12_COMMAND_LIST_TYPE_COMPUTE, D3D12_COMMAND_QUEUE_PRIORITY_NORMAL,
                                                   D3D12_COMMAND_QUEUE_FLAG_NONE, 0};
        const D3D12_COMMAND_QUEUE_DESC copyDesc{D3D12_COMMAND_LIST_TYPE_COPY, D3D12_COMMAND_QUEUE_PRIORITY_NORMAL,
                                                D3D12_COMMAND_QUEUE_FLAG_NONE, 0};
        result = m_impl->nativeDevice->CreateCommandQueue(&graphicsDesc, IID_PPV_ARGS(&m_impl->graphicsQueue));
        if (SUCCEEDED(result))
            result = m_impl->nativeDevice->CreateCommandQueue(&computeDesc, IID_PPV_ARGS(&m_impl->computeQueue));
        if (SUCCEEDED(result))
            result = m_impl->nativeDevice->CreateCommandQueue(&copyDesc, IID_PPV_ARGS(&m_impl->copyQueue));
        if (SUCCEEDED(result))
            result = m_impl->nativeDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_impl->queueFences[0]));
        if (SUCCEEDED(result))
            result = m_impl->nativeDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_impl->queueFences[1]));
        if (SUCCEEDED(result))
            result = m_impl->nativeDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_impl->queueFences[2]));
        if (FAILED(result))
        {
            m_impl->ReleaseDeviceObjects();
            return BackendStatus::Failure(FailureCode::BackendFailure, result, "D3D12 command queue creation failed");
        }

        nvrhi::d3d12::DeviceDesc deviceDesc{};
        deviceDesc.errorCB = &m_impl->common;
        deviceDesc.pDevice = m_impl->nativeDevice;
        deviceDesc.pGraphicsCommandQueue = m_impl->graphicsQueue;
        deviceDesc.pComputeCommandQueue = m_impl->computeQueue;
        deviceDesc.pCopyCommandQueue = m_impl->copyQueue;
        deviceDesc.shaderResourceViewHeapSize = 1000000;
        deviceDesc.samplerHeapSize = 2048;
        deviceDesc.enableHeapDirectlyIndexed = true;
        deviceDesc.enableEnhancedBarriers = true;
        deviceDesc.logBufferLifetime = params.enableValidation;
        nvrhi::d3d12::DeviceHandle device = nvrhi::d3d12::createDevice(deviceDesc);
        if (!device)
        {
            m_impl->ReleaseDeviceObjects();
            return BackendStatus::Failure(FailureCode::BackendFailure, 0, "NVRHI failed to create its D3D12 device");
        }
        if (!m_impl->common.Initialize(static_cast<nvrhi::DeviceHandle&&>(device), &Impl::IsFenceComplete, &Impl::SignalFence,
                                       &Impl::WaitFence, &Impl::AliasingBarrier, m_impl))
        {
            m_impl->ReleaseDeviceObjects();
            return BackendStatus::Failure(FailureCode::OutOfMemory, 0, "could not initialize D3D12 resource lifetime tables");
        }

        DXGI_ADAPTER_DESC1 adapterDesc{};
        m_impl->adapter->GetDesc1(&adapterDesc);
        Capabilities& caps = m_impl->capabilities;
        caps.backend = BackendKind::D3D12;
        caps.vendorId = adapterDesc.VendorId;
        caps.deviceId = adapterDesc.DeviceId;
        caps.vendor = GetVendor(adapterDesc.VendorId);
        caps.dedicatedVideoMemory = adapterDesc.DedicatedVideoMemory;
        caps.uploadBufferAlignment = D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT;
        caps.constantBufferAlignment = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;
        caps.maximumTextureDimension2D = D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION;
        caps.maximumTextureArrayLayers = D3D12_REQ_TEXTURE2D_ARRAY_AXIS_DIMENSION;
        m_impl->common.PopulateCapabilities(caps);
        CopyAdapterName(caps.adapterName, static_cast<u32>(sizeof(caps.adapterName)), adapterDesc.Description);
        m_impl->initialized = true;
        capabilities = caps;

        VG_LOG_INFO(diagnostics::Category::Rendering, "D3D12 RHI initialized on %s (%llu MiB dedicated VRAM)", caps.adapterName,
                    static_cast<unsigned long long>(caps.dedicatedVideoMemory / (1024u * 1024u)));
        return BackendStatus::Success();
    }

    BackendStatus Backend::Shutdown() noexcept
    {
        if (m_impl == nullptr || !m_impl->initialized)
            return BackendStatus::Failure(FailureCode::NotInitialized, 0, "D3D12 backend is not initialized");
        const BackendStatus idle = WaitIdle();
        if (!idle)
            return idle;
        if (!m_impl->common.ShutdownAfterGpuIdle())
            return BackendStatus::Failure(FailureCode::Busy, 0, "RHI backend still owns live resources");
        m_impl->ReleaseDeviceObjects();
        return BackendStatus::Success();
    }

    DeviceState Backend::TestDeviceState() noexcept
    {
        if (m_impl == nullptr || !m_impl->initialized || m_impl->nativeDevice == nullptr)
            return DeviceState::Unknown;
        const HRESULT result = m_impl->nativeDevice->GetDeviceRemovedReason();
        if (result == S_OK)
            return DeviceState::Operational;
        if (result == DXGI_ERROR_DEVICE_RESET)
            return DeviceState::ResetRequired;
        if (result == DXGI_ERROR_DEVICE_REMOVED || result == DXGI_ERROR_DEVICE_HUNG || result == DXGI_ERROR_DRIVER_INTERNAL_ERROR)
            return DeviceState::Removed;
        return DeviceState::Unknown;
    }

    BackendStatus Backend::WaitIdle() noexcept
    {
        if (m_impl == nullptr || !m_impl->initialized || !m_impl->common.IsInitialized())
            return BackendStatus::Failure(FailureCode::NotInitialized, 0, "D3D12 backend is not initialized");
        return m_impl->common.WaitIdle() ? BackendStatus::Success()
                                         : BackendStatus::Failure(FailureCode::DeviceLost, m_impl->nativeDevice->GetDeviceRemovedReason(),
                                                                  "D3D12 device failed while waiting for idle");
    }

    BackendStatus Backend::RetireResources() noexcept
    {
        if (m_impl == nullptr || !m_impl->initialized || !m_impl->common.IsInitialized())
            return BackendStatus::Failure(FailureCode::NotInitialized, 0, "D3D12 backend is not initialized");
        m_impl->common.RetireResources();
        return BackendStatus::Success();
    }

    TextureRef Backend::CreateTexture(const TextureDesc& desc, const TextureInitData& data) noexcept
    {
        return m_impl->common.CreateTexture(desc, data);
    }
    BufferRef Backend::CreateBuffer(const BufferDesc& desc, const BufferInitData& data) noexcept
    {
        return m_impl->common.CreateBuffer(desc, data);
    }
    HeapRef Backend::CreateHeap(const HeapDesc& desc) noexcept
    {
        return m_impl->common.CreateHeap(desc);
    }
    BindingLayoutRef Backend::RequestBindingLayout(const BindingLayoutDesc& desc) noexcept
    {
        return m_impl->common.RequestBindingLayout(desc);
    }
    DescriptorDomainRef Backend::CreateDescriptorDomain(const DescriptorDomainDesc& desc) noexcept
    {
        return m_impl->common.CreateDescriptorDomain(desc);
    }
    DescriptorHandle Backend::AllocateDescriptor(const DescriptorDomainRef domain) noexcept
    {
        return m_impl->common.AllocateDescriptor(domain);
    }
    BackendStatus Backend::WriteDescriptor(const DescriptorDomainRef domain, const DescriptorHandle descriptor, const TextureRef texture,
                                           const BindingType type, const TextureViewDesc& view) noexcept
    {
        return m_impl->common.WriteDescriptor(domain, descriptor, texture, type, view);
    }
    BackendStatus Backend::WriteDescriptor(const DescriptorDomainRef domain, const DescriptorHandle descriptor, const BufferRef buffer,
                                           const BindingType type, const BufferViewDesc& view) noexcept
    {
        return m_impl->common.WriteDescriptor(domain, descriptor, buffer, type, view);
    }
    BackendStatus Backend::WriteDescriptor(const DescriptorDomainRef domain, const DescriptorHandle descriptor,
                                           const SamplerStateRef sampler) noexcept
    {
        return m_impl->common.WriteDescriptor(domain, descriptor, sampler);
    }
    BackendStatus Backend::RetireDescriptor(const DescriptorDomainRef domain, const DescriptorHandle descriptor,
                                            const DescriptorRetirement& retirement) noexcept
    {
        return m_impl->common.RetireDescriptor(domain, descriptor, retirement);
    }
    DescriptorDomainStats Backend::GetDescriptorDomainStats(const DescriptorDomainRef domain) const noexcept
    {
        return m_impl->common.GetDescriptorDomainStats(domain);
    }
    SamplerStateRef Backend::RequestSamplerState(const SamplerStateDesc& desc) noexcept
    {
        return m_impl->common.RequestSamplerState(desc);
    }
    ShaderRef Backend::CreateShader(const ShaderDesc& desc) noexcept
    {
        return m_impl->common.CreateShader(desc);
    }
    VertexLayoutRef Backend::GetVertexLayout(const VertexLayoutDesc& desc) noexcept
    {
        return m_impl->common.GetVertexLayout(desc);
    }
    PipelineRef Backend::CreateGraphicsPipeline(const GraphicsPipelineDesc& desc) noexcept
    {
        return m_impl->common.CreateGraphicsPipeline(desc);
    }
    PipelineRef Backend::CreateComputePipeline(const ComputePipelineDesc& desc) noexcept
    {
        return m_impl->common.CreateComputePipeline(desc);
    }
    PipelineRef Backend::CreateRayTracingPipeline(const RayTracingPipelineDesc& desc) noexcept
    {
        return m_impl->common.CreateRayTracingPipeline(desc);
    }
    QueryPoolRef Backend::CreateQueryPool(const QueryPoolDesc& desc) noexcept
    {
        return m_impl->common.CreateQueryPool(desc);
    }
    void Backend::DestroyQueryPool(const QueryPoolRef queryPool) noexcept
    {
        m_impl->common.DestroyQueryPool(queryPool);
    }
    BackendStatus Backend::BindMemory(const TextureRef texture, const HeapRef heap, const u64 offset) noexcept
    {
        return m_impl->common.BindMemory(texture, heap, offset);
    }
    BackendStatus Backend::BindMemory(const BufferRef buffer, const HeapRef heap, const u64 offset) noexcept
    {
        return m_impl->common.BindMemory(buffer, heap, offset);
    }
    MemoryRequirements Backend::GetMemoryRequirements(const TextureRef texture) const noexcept
    {
        return m_impl->common.GetMemoryRequirements(texture);
    }
    MemoryRequirements Backend::GetMemoryRequirements(const BufferRef buffer) const noexcept
    {
        return m_impl->common.GetMemoryRequirements(buffer);
    }
    bool Backend::IsResourceReferenceValid(const ResourceRef resource) const noexcept
    {
        return m_impl != nullptr && m_impl->common.IsResourceReferenceValid(resource);
    }
    void Backend::AddRef(const ResourceRef resource) noexcept
    {
        if (m_impl != nullptr)
            m_impl->common.AddRef(resource);
    }
    i32 Backend::Release(const ResourceRef resource) noexcept
    {
        return m_impl != nullptr ? m_impl->common.Release(resource) : -1;
    }
    ResourceLifetimeStats Backend::GetResourceLifetimeStats() const noexcept
    {
        return m_impl != nullptr ? m_impl->common.GetResourceLifetimeStats() : ResourceLifetimeStats{};
    }
    CommandListRef Backend::CreateCommandList(const CommandListType type, const u64 debugHash) noexcept
    {
        return m_impl->common.CreateCommandList(type, debugHash);
    }
    void Backend::DiscardCommandList(const CommandListRef commandList) noexcept
    {
        m_impl->common.DiscardCommandList(commandList);
    }
    CommandListType Backend::GetCommandListType(const CommandListRef commandList) const noexcept
    {
        return m_impl->common.GetCommandListType(commandList);
    }
    BackendStatus Backend::CloseAndSubmitCommandLists(const char* const name, const containers::ArraySpan<const CommandListRef> lists,
                                                      const CommandListSyncType sync, GpuFence& completion) noexcept
    {
        return m_impl->common.CloseAndSubmitCommandLists(name, lists, sync, completion);
    }
    GpuFence Backend::GetGpuFence(const CommandListRef commandList) const noexcept
    {
        return m_impl->common.GetGpuFence(commandList);
    }
    bool Backend::IsGpuFenceComplete(const GpuFence fence) const noexcept
    {
        return m_impl->common.IsGpuFenceComplete(fence);
    }
    BackendStatus Backend::WaitForGpuFence(const GpuFence fence, const u64 timeout) noexcept
    {
        return m_impl->common.WaitForGpuFence(fence, timeout);
    }
    BackendStatus Backend::SetPipeline(const CommandListRef commandList, const PipelineRef pipeline) noexcept
    {
        return m_impl->common.SetPipeline(commandList, pipeline);
    }
    BackendStatus Backend::SetupRenderTargets(const CommandListRef commandList, const RenderTargetSetup& setup) noexcept
    {
        return m_impl->common.SetupRenderTargets(commandList, setup);
    }
    BackendStatus Backend::SetViewport(const CommandListRef commandList, const ViewportDesc& viewport) noexcept
    {
        return m_impl->common.SetViewport(commandList, viewport);
    }
    BackendStatus Backend::SetScissors(const CommandListRef commandList, const Rect& rect) noexcept
    {
        return m_impl->common.SetScissors(commandList, rect);
    }
    BackendStatus Backend::BindVertexBuffers(const CommandListRef commandList, const u32 startIndex,
                                             const containers::ArraySpan<const VertexBufferBinding> bindings) noexcept
    {
        return m_impl->common.BindVertexBuffers(commandList, startIndex, bindings);
    }
    BackendStatus Backend::BindIndexBuffer(const CommandListRef commandList, const IndexBufferBinding& binding) noexcept
    {
        return m_impl->common.BindIndexBuffer(commandList, binding);
    }
    BackendStatus Backend::BindIndirectArguments(const CommandListRef commandList, const BufferRef arguments,
                                                 const BufferRef count) noexcept
    {
        return m_impl->common.BindIndirectArguments(commandList, arguments, count);
    }
    BackendStatus Backend::SetPushConstants(const CommandListRef commandList, const void* const data, const u32 size) noexcept
    {
        return m_impl->common.SetPushConstants(commandList, data, size);
    }
    BackendStatus Backend::DrawPrimitive(const CommandListRef commandList, const DrawArguments& arguments) noexcept
    {
        return m_impl->common.DrawPrimitive(commandList, arguments);
    }
    BackendStatus Backend::DrawIndexedPrimitive(const CommandListRef commandList, const DrawIndexedArguments& arguments) noexcept
    {
        return m_impl->common.DrawIndexedPrimitive(commandList, arguments);
    }
    BackendStatus Backend::DrawPrimitiveIndirect(const CommandListRef commandList, const u64 offset, const u32 count) noexcept
    {
        return m_impl->common.DrawPrimitiveIndirect(commandList, offset, count);
    }
    BackendStatus Backend::DrawIndexedPrimitiveIndirect(const CommandListRef commandList, const u64 offset, const u32 count) noexcept
    {
        return m_impl->common.DrawIndexedPrimitiveIndirect(commandList, offset, count);
    }
    BackendStatus Backend::DrawIndexedPrimitiveIndirectCount(const CommandListRef commandList, const u64 argumentsOffset,
                                                             const u64 countOffset, const u32 maximumCount) noexcept
    {
        return m_impl->common.DrawIndexedPrimitiveIndirectCount(commandList, argumentsOffset, countOffset, maximumCount);
    }
    BackendStatus Backend::DispatchCompute(const CommandListRef commandList, const u32 x, const u32 y, const u32 z) noexcept
    {
        return m_impl->common.DispatchCompute(commandList, x, y, z);
    }
    BackendStatus Backend::DispatchIndirectCompute(const CommandListRef commandList, const u64 offset) noexcept
    {
        return m_impl->common.DispatchIndirectCompute(commandList, offset);
    }
    BackendStatus Backend::WriteBuffer(const CommandListRef commandList, const BufferRef buffer, const void* const data, const u64 size,
                                       const u64 offset) noexcept
    {
        return m_impl->common.WriteBuffer(commandList, buffer, data, size, offset);
    }
    BackendStatus Backend::WriteTexture(const CommandListRef commandList, const TextureRef texture,
                                        const TextureSubresourceData& data) noexcept
    {
        return m_impl->common.WriteTexture(commandList, texture, data);
    }
    BackendStatus Backend::CopyBuffer(const CommandListRef commandList, const BufferRef destination, const u64 destinationOffset,
                                      const BufferRef source, const u64 sourceOffset, const u64 size) noexcept
    {
        return m_impl->common.CopyBuffer(commandList, destination, destinationOffset, source, sourceOffset, size);
    }
    BackendStatus Backend::LockBuffer(const BufferRef buffer, const u64 offset, const u64 size, void*& data) noexcept
    {
        return m_impl->common.LockBuffer(buffer, offset, size, data);
    }
    void Backend::UnlockBuffer(const BufferRef buffer) noexcept
    {
        m_impl->common.UnlockBuffer(buffer);
    }
    BackendStatus Backend::TransitionTexture(const CommandListRef commandList, const TextureRef texture, const ResourceState before,
                                             const ResourceState after, const SubresourceRange& range) noexcept
    {
        return m_impl->common.TransitionTexture(commandList, texture, before, after, range);
    }
    BackendStatus Backend::TransitionBuffer(const CommandListRef commandList, const BufferRef buffer, const ResourceState before,
                                            const ResourceState after) noexcept
    {
        return m_impl->common.TransitionBuffer(commandList, buffer, before, after);
    }
    BackendStatus Backend::BarrierTextureUav(const CommandListRef commandList, const TextureRef texture) noexcept
    {
        return m_impl->common.BarrierTextureUav(commandList, texture);
    }
    BackendStatus Backend::BarrierBufferUav(const CommandListRef commandList, const BufferRef buffer) noexcept
    {
        return m_impl->common.BarrierBufferUav(commandList, buffer);
    }
    BackendStatus Backend::BarrierTextureAliasing(const CommandListRef commandList, const bool discard, const TextureRef after,
                                                  const TextureRef before) noexcept
    {
        return m_impl->common.BarrierTextureAliasing(commandList, discard, after, before);
    }
    BackendStatus Backend::BarrierBufferAliasing(const CommandListRef commandList, const bool discard, const BufferRef after,
                                                 const BufferRef before) noexcept
    {
        return m_impl->common.BarrierBufferAliasing(commandList, discard, after, before);
    }
    BackendStatus Backend::FlushPendingBarriers(const CommandListRef commandList) noexcept
    {
        return m_impl->common.FlushPendingBarriers(commandList);
    }
    BackendStatus Backend::MakeStateSafeToRetire(const CommandListRef commandList, const TextureRef texture) noexcept
    {
        return m_impl->common.MakeStateSafeToRetire(commandList, texture);
    }
    BackendStatus Backend::MakeStateSafeToRetire(const CommandListRef commandList, const BufferRef buffer) noexcept
    {
        return m_impl->common.MakeStateSafeToRetire(commandList, buffer);
    }
    SwapChainRef Backend::CreateSwapChainWithBackBuffer(const SwapChainDesc&) noexcept
    {
        return {};
    }
    BackendStatus Backend::ResizeBackbuffer(SwapChainRef, u32, u32) noexcept
    {
        return NativeOperationUnavailable();
    }
    TextureRef Backend::GetBackBufferTexture(SwapChainRef) noexcept
    {
        return {};
    }
    BackendStatus Backend::TransitionSwapChainPresent(CommandListRef, SwapChainRef) noexcept
    {
        return NativeOperationUnavailable();
    }
    BackendStatus Backend::Present(SwapChainRef) noexcept
    {
        return NativeOperationUnavailable();
    }
    void Backend::SetResourceDebugName(const TextureRef resource, const char* const name) noexcept
    {
        m_impl->common.SetResourceDebugName(resource, name);
    }
    void Backend::SetResourceDebugName(const BufferRef resource, const char* const name) noexcept
    {
        m_impl->common.SetResourceDebugName(resource, name);
    }
    void Backend::SetResourceDebugName(const HeapRef resource, const char* const name) noexcept
    {
        m_impl->common.SetResourceDebugName(resource, name);
    }
    void Backend::SetResourceDebugName(const SamplerStateRef resource, const char* const name) noexcept
    {
        m_impl->common.SetResourceDebugName(resource, name);
    }
    void Backend::SetResourceDebugName(const ShaderRef resource, const char* const name) noexcept
    {
        m_impl->common.SetResourceDebugName(resource, name);
    }
    void Backend::SetResourceDebugName(const VertexLayoutRef resource, const char* const name) noexcept
    {
        m_impl->common.SetResourceDebugName(resource, name);
    }
    void Backend::SetResourceDebugName(const PipelineRef resource, const char* const name) noexcept
    {
        m_impl->common.SetResourceDebugName(resource, name);
    }
    void Backend::SetResourceDebugName(const QueryPoolRef resource, const char* const name) noexcept
    {
        m_impl->common.SetResourceDebugName(resource, name);
    }
    void Backend::SetResourceDebugName(const CommandListRef resource, const char* const name) noexcept
    {
        m_impl->common.SetResourceDebugName(resource, name);
    }
    void Backend::SetResourceDebugName(SwapChainRef, const char*) noexcept {}
} // namespace vanguard::rhi::d3d12
