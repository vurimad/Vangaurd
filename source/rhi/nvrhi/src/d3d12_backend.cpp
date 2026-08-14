#include <vanguard/rhi/d3d12/backend.hpp>
#include <d3d12/d3d12-backend.h>

#include <vanguard/diagnostics/diagnostics.hpp>
#include <vanguard/memory/memory.hpp>
#include <vanguard/rhi/backend/common_backend.hpp>
#include <vanguard/system/time.hpp>
#include <nvrhi/d3d12.h>

#include <dxgi1_6.h>
#include <new>

namespace vanguard::rhi::d3d12
{
    namespace
    {
        constexpr u32 MaximumQueryPools = 256;

        enum class QueryPoolSlotState : u8
        {
            Free,
            Active,
            Retiring,
            Exhausted
        };

        struct QueryPoolPayload;

        struct QueryPoolSlot
        {
            QueryPoolPayload* payload = nullptr;
            u32 generation = 1;
            QueryPoolSlotState state = QueryPoolSlotState::Free;
        };

        struct QueryPoolPayload
        {
            void* owner = nullptr;
            ID3D12QueryHeap* heap = nullptr;
            ID3D12Resource* readback = nullptr;
            u8* openQueries = nullptr;
            void* mapped = nullptr;
            backend::FenceSet lastUseFences{};
            GpuFence lastResolveFence{};
            QueryPoolRef reference{};
            QueryType type = QueryType::Timestamp;
            u32 capacity = 0;
            u32 resultStride = 0;
            u32 mappedStart = 0;
            u32 mappedEnd = 0;
            u32 pendingSubmissions = 0;
        };

        struct SwapChainPayload final
        {
            concurrency::SpinLock lock;
            backend::CommonBackend* common = nullptr;
            ID3D12CommandQueue* presentationQueue = nullptr;
            IDXGISwapChain4* native = nullptr;
            ID3D12Fence* presentFence = nullptr;
            HANDLE presentFenceEvent = nullptr;
            HANDLE frameLatencyWaitableObject = nullptr;
            TextureRef backBuffers[MaximumSwapChainBuffers]{};
            u64 presentFenceValues[MaximumSwapChainBuffers]{};
            SwapChainDesc desc{};
            AcquiredBackBuffer activeAcquisition{};
            SwapChainStats stats{};
            u64 nextPresentFenceValue = 1;
            u64 nextAcquisitionSerial = 1;
            concurrency::Atomic<u64> submittedAcquisitionSerial;
            u32 bufferCount = 0;
            bool tearingEnabled = false;
            bool presentTransitionRecorded = false;
            char debugName[96]{};
        };

        [[nodiscard]] bool IsPresentParametersValid(const PresentParameters& parameters) noexcept
        {
            if (parameters.mode == PresentMode::Mailbox) return false;
            if (parameters.mode == PresentMode::Immediate)
                return parameters.synchronizationInterval == 0;
            return parameters.synchronizationInterval >= 1 && parameters.synchronizationInterval <= 4 &&
                   !parameters.allowTearing;
        }

        [[nodiscard]] bool MatchesAcquisition(const SwapChainPayload& payload,
                                              const AcquiredBackBuffer& acquisition) noexcept
        {
            return acquisition.IsValid() && payload.activeAcquisition.IsValid() &&
                   acquisition.swapChain == payload.activeAcquisition.swapChain &&
                   acquisition.texture == payload.activeAcquisition.texture &&
                   acquisition.serial == payload.activeAcquisition.serial &&
                   acquisition.bufferIndex == payload.activeAcquisition.bufferIndex;
        }

        [[nodiscard]] u64 TicksToNanoseconds(const u64 ticks) noexcept
        {
            const u64 frequency = system::MonotonicFrequency();
            if (frequency == 0) return 0;
            constexpr u64 NanosecondsPerSecond = 1'000'000'000ull;
            return (ticks / frequency) * NanosecondsPerSecond +
                   ((ticks % frequency) * NanosecondsPerSecond) / frequency;
        }

        [[nodiscard]] DXGI_COLOR_SPACE_TYPE ToNativeColorSpace(ColorSpace colorSpace) noexcept;

        [[nodiscard]] u16 EncodeChromaticity(const f32 value) noexcept
        {
            const f32 clamped = value < 0.0f ? 0.0f : value > 1.0f ? 1.0f : value;
            return static_cast<u16>(clamped * 50'000.0f + 0.5f);
        }

        [[nodiscard]] u32 EncodeLuminance(const f32 value) noexcept
        {
            if (value <= 0.0f) return 0;
            constexpr f32 Maximum = 429'496.7295f;
            return static_cast<u32>((value > Maximum ? Maximum : value) * 10'000.0f + 0.5f);
        }

        [[nodiscard]] bool ApplyColorConfiguration(IDXGISwapChain4& swapChain, const SwapChainDesc& desc) noexcept
        {
            const DXGI_COLOR_SPACE_TYPE nativeColorSpace = ToNativeColorSpace(desc.colorSpace);
            UINT support = 0;
            if (FAILED(swapChain.CheckColorSpaceSupport(nativeColorSpace, &support)) ||
                (support & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT) == 0 ||
                FAILED(swapChain.SetColorSpace1(nativeColorSpace))) return false;

            if (desc.colorSpace != ColorSpace::Hdr10)
                return SUCCEEDED(swapChain.SetHDRMetaData(DXGI_HDR_METADATA_TYPE_NONE, 0, nullptr));

            const Hdr10Metadata& source = desc.hdr10Metadata;
            if (source.maximumMasteringLuminanceNits <= 0.0f || source.minimumMasteringLuminanceNits < 0.0f ||
                source.minimumMasteringLuminanceNits > source.maximumMasteringLuminanceNits ||
                source.maximumFrameAverageLightLevelNits > source.maximumContentLightLevelNits) return false;
            DXGI_HDR_METADATA_HDR10 metadata{};
            metadata.RedPrimary[0] = EncodeChromaticity(source.redPrimary.x);
            metadata.RedPrimary[1] = EncodeChromaticity(source.redPrimary.y);
            metadata.GreenPrimary[0] = EncodeChromaticity(source.greenPrimary.x);
            metadata.GreenPrimary[1] = EncodeChromaticity(source.greenPrimary.y);
            metadata.BluePrimary[0] = EncodeChromaticity(source.bluePrimary.x);
            metadata.BluePrimary[1] = EncodeChromaticity(source.bluePrimary.y);
            metadata.WhitePoint[0] = EncodeChromaticity(source.whitePoint.x);
            metadata.WhitePoint[1] = EncodeChromaticity(source.whitePoint.y);
            metadata.MaxMasteringLuminance = EncodeLuminance(source.maximumMasteringLuminanceNits);
            metadata.MinMasteringLuminance = EncodeLuminance(source.minimumMasteringLuminanceNits);
            metadata.MaxContentLightLevel = source.maximumContentLightLevelNits;
            metadata.MaxFrameAverageLightLevel = source.maximumFrameAverageLightLevelNits;
            return SUCCEEDED(swapChain.SetHDRMetaData(DXGI_HDR_METADATA_TYPE_HDR10, sizeof(metadata), &metadata));
        }

        [[nodiscard]] DisplayColorCapabilities QueryDisplayColorCapabilities(IDXGISwapChain4& swapChain) noexcept
        {
            DisplayColorCapabilities capabilities{};
            IDXGIOutput* output = nullptr;
            if (FAILED(swapChain.GetContainingOutput(&output)) || output == nullptr) return capabilities;
            IDXGIOutput6* output6 = nullptr;
            const HRESULT query = output->QueryInterface(IID_PPV_ARGS(&output6));
            output->Release();
            if (FAILED(query) || output6 == nullptr) return capabilities;
            DXGI_OUTPUT_DESC1 desc{};
            const HRESULT result = output6->GetDesc1(&desc);
            output6->Release();
            if (FAILED(result)) return capabilities;
            capabilities.redPrimary = {desc.RedPrimary[0], desc.RedPrimary[1]};
            capabilities.greenPrimary = {desc.GreenPrimary[0], desc.GreenPrimary[1]};
            capabilities.bluePrimary = {desc.BluePrimary[0], desc.BluePrimary[1]};
            capabilities.whitePoint = {desc.WhitePoint[0], desc.WhitePoint[1]};
            capabilities.minimumLuminanceNits = desc.MinLuminance;
            capabilities.maximumLuminanceNits = desc.MaxLuminance;
            capabilities.maximumFullFrameLuminanceNits = desc.MaxFullFrameLuminance;
            capabilities.bitsPerColor = static_cast<u8>(desc.BitsPerColor > 255u ? 255u : desc.BitsPerColor);
            capabilities.hdr10Output = desc.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
            capabilities.hdrActive = capabilities.hdr10Output ||
                                     desc.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;
            return capabilities;
        }

        [[nodiscard]] DXGI_FORMAT ToSwapChainFormat(const Format format) noexcept
        {
            switch (format)
            {
            case Format::R8G8B8A8UNorm:
            case Format::R8G8B8A8UNormSrgb: return DXGI_FORMAT_R8G8B8A8_UNORM;
            case Format::B8G8R8A8UNorm:
            case Format::B8G8R8A8UNormSrgb: return DXGI_FORMAT_B8G8R8A8_UNORM;
            case Format::R10G10B10A2UNorm: return DXGI_FORMAT_R10G10B10A2_UNORM;
            case Format::R16G16B16A16Float: return DXGI_FORMAT_R16G16B16A16_FLOAT;
            default: return DXGI_FORMAT_UNKNOWN;
            }
        }

        [[nodiscard]] nvrhi::Format ToNativeSwapChainFormat(const Format format) noexcept
        {
            switch (format)
            {
            case Format::R8G8B8A8UNorm: return nvrhi::Format::RGBA8_UNORM;
            case Format::R8G8B8A8UNormSrgb: return nvrhi::Format::SRGBA8_UNORM;
            case Format::B8G8R8A8UNorm: return nvrhi::Format::BGRA8_UNORM;
            case Format::B8G8R8A8UNormSrgb: return nvrhi::Format::SBGRA8_UNORM;
            case Format::R10G10B10A2UNorm: return nvrhi::Format::R10G10B10A2_UNORM;
            case Format::R16G16B16A16Float: return nvrhi::Format::RGBA16_FLOAT;
            default: return nvrhi::Format::UNKNOWN;
            }
        }

        [[nodiscard]] DXGI_COLOR_SPACE_TYPE ToNativeColorSpace(const ColorSpace colorSpace) noexcept
        {
            switch (colorSpace)
            {
            case ColorSpace::Hdr10: return DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
            case ColorSpace::ScRgb: return DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;
            default: return DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
            }
        }

        [[nodiscard]] bool IsSwapChainDescriptorSupported(const SwapChainDesc& desc) noexcept
        {
            if (desc.surface.kind != PresentationSurfaceKind::Win32 || desc.surface.nativeWindow == nullptr ||
                desc.width == 0 || desc.height == 0 || desc.bufferCount < 2 ||
                desc.bufferCount > MaximumSwapChainBuffers || ToSwapChainFormat(desc.format) == DXGI_FORMAT_UNKNOWN ||
                ToNativeSwapChainFormat(desc.format) == nvrhi::Format::UNKNOWN || desc.presentMode == PresentMode::Mailbox)
                return false;
            if (desc.frameLatency.enabled && (desc.frameLatency.maximumFramesInFlight == 0 ||
                desc.frameLatency.maximumFramesInFlight > desc.bufferCount || desc.frameLatency.waitTimeoutMilliseconds == 0))
                return false;
            if (desc.colorSpace == ColorSpace::Hdr10 && desc.format != Format::R10G10B10A2UNorm) return false;
            if (desc.colorSpace == ColorSpace::ScRgb && desc.format != Format::R16G16B16A16Float) return false;
            return desc.colorSpace != ColorSpace::Srgb || desc.format == Format::R8G8B8A8UNorm ||
                   desc.format == Format::R8G8B8A8UNormSrgb || desc.format == Format::B8G8R8A8UNorm ||
                   desc.format == Format::B8G8R8A8UNormSrgb;
        }

        void CopyDebugName(char* const destination, const u32 capacity, const char* const source) noexcept
        {
            if (destination == nullptr || capacity == 0) return;
            destination[0] = '\0';
            if (source == nullptr) return;
            u32 index = 0;
            while (index + 1u < capacity && source[index] != '\0')
            {
                destination[index] = source[index];
                ++index;
            }
            destination[index] = '\0';
        }

        void NameBackBuffers(SwapChainPayload& payload) noexcept
        {
            if (payload.native == nullptr || payload.debugName[0] == '\0') return;
            wchar_t wideName[128]{};
            const int prefixLength = MultiByteToWideChar(CP_UTF8, 0, payload.debugName, -1, wideName, 112);
            if (prefixLength <= 0) return;
            const u32 prefixEnd = static_cast<u32>(prefixLength - 1);
            for (u32 index = 0; index < payload.bufferCount; ++index)
            {
                ID3D12Resource* resource = nullptr;
                if (FAILED(payload.native->GetBuffer(index, IID_PPV_ARGS(&resource)))) continue;
                wchar_t name[128]{};
                u32 write = 0;
                while (write < prefixEnd && write + 1u < static_cast<u32>(_countof(name)))
                {
                    name[write] = wideName[write];
                    ++write;
                }
                const wchar_t suffix[] = L"/BackBuffer[0]";
                for (u32 suffixIndex = 0; suffixIndex + 1u < static_cast<u32>(_countof(suffix)) &&
                                          write + 1u < static_cast<u32>(_countof(name)); ++suffixIndex)
                    name[write++] = suffix[suffixIndex];
                if (write >= 2u) name[write - 2u] = static_cast<wchar_t>(L'0' + index);
                name[write] = L'\0';
                static_cast<void>(resource->SetName(name));
                resource->Release();
            }
        }

        void ReleaseBackBufferHandles(SwapChainPayload& payload) noexcept;

        [[nodiscard]] bool WaitForPresentFence(SwapChainPayload& payload, const u64 value,
                                               bool* const waited = nullptr) noexcept
        {
            if (waited != nullptr) *waited = false;
            if (value == 0) return true;
            if (payload.presentFence == nullptr || payload.presentFenceEvent == nullptr) return false;
            if (payload.presentFence->GetCompletedValue() >= value) return true;
            if (waited != nullptr) *waited = true;
            if (FAILED(payload.presentFence->SetEventOnCompletion(value, payload.presentFenceEvent))) return false;
            return WaitForSingleObjectEx(payload.presentFenceEvent, INFINITE, FALSE) == WAIT_OBJECT_0;
        }

        [[nodiscard]] bool WaitForAllPresents(SwapChainPayload& payload) noexcept
        {
            u64 latestFence = 0;
            for (u32 index = 0; index < payload.bufferCount; ++index)
                if (latestFence < payload.presentFenceValues[index]) latestFence = payload.presentFenceValues[index];
            return WaitForPresentFence(payload, latestFence);
        }

        struct PresentOperationContext final
        {
            SwapChainPayload* payload = nullptr;
            ID3D12Device* device = nullptr;
        };

        BackendStatus ExecutePresent(void* const address) noexcept
        {
            auto& context = *static_cast<PresentOperationContext*>(address);
            SwapChainPayload& payload = *context.payload;
            const u32 presentedIndex = payload.activeAcquisition.bufferIndex;
            const PresentParameters& parameters = payload.stats.presentParameters;
            const UINT interval = parameters.mode == PresentMode::Fifo ? parameters.synchronizationInterval : 0u;
            const UINT flags = interval == 0 && parameters.allowTearing ? DXGI_PRESENT_ALLOW_TEARING : 0u;
            const HRESULT result = payload.native->Present(interval, flags);
            if (result == DXGI_ERROR_DEVICE_REMOVED || result == DXGI_ERROR_DEVICE_RESET)
            {
                ++payload.stats.presentationFailures;
                payload.stats.state = SwapChainState::Failed;
                const HRESULT reason = context.device != nullptr ? context.device->GetDeviceRemovedReason() : result;
                return BackendStatus::Failure(FailureCode::DeviceLost, reason, "DXGI present reported device loss");
            }
            if (FAILED(result))
            {
                ++payload.stats.presentationFailures;
                payload.stats.state = SwapChainState::Failed;
                return BackendStatus::Failure(FailureCode::BackendFailure, result, "DXGI present failed");
            }
            const u64 fenceValue = payload.nextPresentFenceValue++;
            if (payload.presentationQueue == nullptr || payload.presentFence == nullptr ||
                FAILED(payload.presentationQueue->Signal(payload.presentFence, fenceValue)))
            {
                ++payload.stats.presentationFailures;
                payload.stats.state = SwapChainState::Failed;
                return BackendStatus::Failure(FailureCode::DeviceLost, 0,
                                              "failed to signal the swap-chain presentation fence");
            }
            payload.presentFenceValues[presentedIndex] = fenceValue;
            payload.stats.lastPresentationFence = fenceValue;
            ++payload.stats.presentedFrames;
            payload.activeAcquisition = {};
            payload.presentTransitionRecorded = false;
            payload.stats.state = SwapChainState::Available;
            return BackendStatus::Success();
        }

        void MarkPresentTransitionSubmitted(void* const context, const GpuFence completion,
                                            const u64 acquisitionSerial) noexcept
        {
            auto& payload = *static_cast<SwapChainPayload*>(context);
            payload.submittedAcquisitionSerial.SetValue(completion.queue == QueueType::Graphics
                                                            ? acquisitionSerial
                                                            : 0);
        }

        [[nodiscard]] bool CreateBackBufferHandles(SwapChainPayload& payload) noexcept
        {
            const nvrhi::Format nativeFormat = ToNativeSwapChainFormat(payload.desc.format);
            TextureDesc textureDesc{};
            textureDesc.extent = {payload.desc.width, payload.desc.height, 1};
            textureDesc.dimension = TextureDimension::Texture2D;
            textureDesc.format = payload.desc.format;
            textureDesc.mipCount = 1;
            textureDesc.arraySize = 1;
            textureDesc.sampleCount = 1;
            textureDesc.usage = TextureUsage::RenderTarget | TextureUsage::Present;
            textureDesc.initialState = ResourceState::Present;

            for (u32 index = 0; index < payload.bufferCount; ++index)
            {
                ID3D12Resource* resource = nullptr;
                if (FAILED(payload.native->GetBuffer(index, IID_PPV_ARGS(&resource))))
                {
                    ReleaseBackBufferHandles(payload);
                    return false;
                }
                nvrhi::TextureDesc nativeDesc{};
                nativeDesc.width = payload.desc.width;
                nativeDesc.height = payload.desc.height;
                nativeDesc.depth = 1;
                nativeDesc.arraySize = 1;
                nativeDesc.mipLevels = 1;
                nativeDesc.sampleCount = 1;
                nativeDesc.format = nativeFormat;
                nativeDesc.dimension = nvrhi::TextureDimension::Texture2D;
                nativeDesc.isShaderResource = false;
                nativeDesc.isRenderTarget = true;
                nativeDesc.initialState = nvrhi::ResourceStates::Present;
                nativeDesc.keepInitialState = true;
                nvrhi::TextureHandle native = payload.common->Device()->createHandleForNativeTexture(
                    nvrhi::ObjectTypes::D3D12_Resource, nvrhi::Object(resource), nativeDesc);
                resource->Release();
                if (!native)
                {
                    ReleaseBackBufferHandles(payload);
                    return false;
                }
                payload.backBuffers[index] =
                    payload.common->AdoptNativeTexture(static_cast<nvrhi::TextureHandle&&>(native), textureDesc);
                if (!payload.backBuffers[index].IsValid())
                {
                    ReleaseBackBufferHandles(payload);
                    return false;
                }
            }
            NameBackBuffers(payload);
            return true;
        }

        void ReleaseBackBufferHandles(SwapChainPayload& payload) noexcept
        {
            for (u32 index = 0; index < payload.bufferCount; ++index)
            {
                if (payload.backBuffers[index].IsValid())
                    static_cast<void>(payload.common->Release(ResourceRef(payload.backBuffers[index])));
                payload.backBuffers[index] = {};
            }
        }

        void DestroySwapChainPayload(void*, ResourceRef, void* const address) noexcept
        {
            if (address == nullptr) return;
            auto* const payload = static_cast<SwapChainPayload*>(address);
            static_cast<void>(WaitForAllPresents(*payload));
            ReleaseBackBufferHandles(*payload);
            if (payload->presentFenceEvent != nullptr) CloseHandle(payload->presentFenceEvent);
            payload->presentFenceEvent = nullptr;
            if (payload->frameLatencyWaitableObject != nullptr) CloseHandle(payload->frameLatencyWaitableObject);
            payload->frameLatencyWaitableObject = nullptr;
            if (payload->presentFence != nullptr) payload->presentFence->Release();
            payload->presentFence = nullptr;
            if (payload->native != nullptr) payload->native->Release();
            payload->native = nullptr;
            payload->~SwapChainPayload();
            memory::MemoryBlock block{payload, sizeof(SwapChainPayload), memory::PoolId::Rendering};
            memory::Free(block);
        }

        template <typename Type> void ReleaseNative(Type*& object) noexcept
        {
            if (object == nullptr)
                return;
            object->Release();
            object = nullptr;
        }

        [[nodiscard]] D3D12_QUERY_HEAP_TYPE ToNativeQueryHeapType(const QueryType type) noexcept
        {
            if (type == QueryType::Occlusion) return D3D12_QUERY_HEAP_TYPE_OCCLUSION;
            if (type == QueryType::PipelineStatistics) return D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS;
            return D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
        }

        [[nodiscard]] D3D12_QUERY_TYPE ToNativeQueryType(const QueryType type) noexcept
        {
            if (type == QueryType::Occlusion) return D3D12_QUERY_TYPE_OCCLUSION;
            if (type == QueryType::PipelineStatistics) return D3D12_QUERY_TYPE_PIPELINE_STATISTICS;
            return D3D12_QUERY_TYPE_TIMESTAMP;
        }

        [[nodiscard]] u32 QueryResultStride(const QueryType type) noexcept
        {
            return type == QueryType::PipelineStatistics ? sizeof(D3D12_QUERY_DATA_PIPELINE_STATISTICS) : sizeof(u64);
        }

        [[nodiscard]] bool IsRangedQuery(const QueryType type) noexcept
        {
            return type == QueryType::Occlusion || type == QueryType::PipelineStatistics;
        }

        void SetNativeName(ID3D12Object* const object, const char* const name) noexcept
        {
            if (object == nullptr || name == nullptr || name[0] == '\0') return;
            wchar_t wideName[128]{};
            if (MultiByteToWideChar(CP_UTF8, 0, name, -1, wideName, static_cast<int>(sizeof(wideName) / sizeof(wideName[0]))) > 0)
                static_cast<void>(object->SetName(wideName));
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

        [[nodiscard]] D3D12_RESIDENCY_PRIORITY ToNativeResidencyPriority(const ResidencyPriority priority) noexcept
        {
            switch (priority)
            {
            case ResidencyPriority::Minimum: return D3D12_RESIDENCY_PRIORITY_MINIMUM;
            case ResidencyPriority::Low: return D3D12_RESIDENCY_PRIORITY_LOW;
            case ResidencyPriority::Normal: return D3D12_RESIDENCY_PRIORITY_NORMAL;
            case ResidencyPriority::High: return D3D12_RESIDENCY_PRIORITY_HIGH;
            case ResidencyPriority::Maximum: return D3D12_RESIDENCY_PRIORITY_MAXIMUM;
            }
            return D3D12_RESIDENCY_PRIORITY_NORMAL;
        }

        [[nodiscard]] constexpr u64 PercentageOf(const u64 value, const u8 percent) noexcept
        {
            return (value / 100u) * percent + ((value % 100u) * percent) / 100u;
        }

        [[nodiscard]] BackendStatus ResolveResidencyBatch(backend::CommonBackend& common,
                                                           const containers::ArraySpan<const ResourceRef> resources,
                                                           ID3D12Pageable** const pageables, u32& count) noexcept
        {
            count = 0;
            if (resources.Size() == 0 || resources.Size() > MaximumResidencyBatchSize)
                return BackendStatus::Failure(FailureCode::CapacityExceeded, 0, "GPU residency batch capacity is invalid");
            for (const ResourceRef resource : resources)
            {
                const ResourceRef allocation = common.GetResidencyAllocation(resource);
                if (!allocation.IsValid())
                    return BackendStatus::Failure(FailureCode::IncompatibleBinding, 0,
                                                  "only textures, buffers and heaps have explicit residency");
                ID3D12Pageable* pageable = nullptr;
                if (allocation.Kind() == ResourceKind::Heap)
                {
                    auto* const heap =
                        static_cast<nvrhi::d3d12::Heap*>(common.GetNativeHeap(CastResourceRef<HeapRef>(allocation)));
                    pageable = heap != nullptr ? heap->heap.Get() : nullptr;
                }
                else
                    pageable = common.GetNativeObject(allocation, nvrhi::ObjectTypes::D3D12_Resource);
                if (pageable == nullptr)
                    return BackendStatus::Failure(FailureCode::InvalidReference, 0,
                                                  "GPU residency batch contains a stale or unsupported resource");
                bool duplicate = false;
                for (u32 index = 0; index < count; ++index) duplicate = duplicate || pageables[index] == pageable;
                if (!duplicate) pageables[count++] = pageable;
            }
            return BackendStatus::Success();
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

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4324)
#endif
    struct Backend::Impl final
    {
        enum class AllocationResidency : u8 { Resident, Evicted };
        struct ResidencyRecord
        {
            ResourceRef resource;
            ResidencyFenceSet lastUse;
            u64 size = 0;
            u64 lastUseOrdinal = 0;
            ResidencyPriority priority = ResidencyPriority::Normal;
            AllocationResidency state = AllocationResidency::Resident;
            bool pinned = false;
        };
        struct EvictionCandidate
        {
            ResourceRef resource;
            u64 size = 0;
            u64 lastUseOrdinal = 0;
            ResidencyPriority priority = ResidencyPriority::Normal;
        };

        static constexpr u32 TextureResidencyCapacity = 65'535;
        static constexpr u32 BufferResidencyCapacity = 65'535;
        static constexpr u32 HeapResidencyCapacity = 8'192;
        static constexpr u32 ResidencyRecordCapacity = TextureResidencyCapacity + BufferResidencyCapacity + HeapResidencyCapacity;

        IDXGIFactory6* factory = nullptr;
        IDXGIAdapter1* adapter = nullptr;
        IDXGIAdapter3* budgetAdapter = nullptr;
        ID3D12Device* nativeDevice = nullptr;
        ID3D12Device1* residencyDevice = nullptr;
        ID3D12CommandQueue* graphicsQueue = nullptr;
        ID3D12CommandQueue* computeQueue = nullptr;
        ID3D12CommandQueue* copyQueue = nullptr;
        ID3D12Fence* queueFences[3]{};
        backend::CommonBackend common;
        concurrency::SpinLock queryPoolLock;
        mutable concurrency::SpinLock residencyLock;
        ResidencyRecord* residencyRecords = nullptr;
        memory::MemoryBlock residencyRecordBlock;
        QueryPoolSlot queryPools[MaximumQueryPools]{};
        ResidencyStats residencyStats{};
        DeviceParams::ResidencyPolicy residencyPolicy{};
        Capabilities capabilities{};
        ID3D12Pageable* submissionResidencyPageables[MaximumResidencyBatchSize]{};
        ID3D12Pageable* submissionEvictedPageables[MaximumResidencyBatchSize]{};
        EvictionCandidate policyCandidates[MaximumResidencyBatchSize]{};
        ResourceRef policyResources[MaximumResidencyBatchSize]{};
        ID3D12Pageable* policyPageables[MaximumResidencyBatchSize]{};
        u64 residencyUseOrdinal = 0;
        bool initialized = false;

        [[nodiscard]] static u32 ResidencyRecordIndex(const ResourceRef allocation) noexcept
        {
            if (allocation.Kind() == ResourceKind::Texture && allocation.Index() < TextureResidencyCapacity)
                return allocation.Index();
            if (allocation.Kind() == ResourceKind::Buffer && allocation.Index() < BufferResidencyCapacity)
                return TextureResidencyCapacity + allocation.Index();
            if (allocation.Kind() == ResourceKind::Heap && allocation.Index() < HeapResidencyCapacity)
                return TextureResidencyCapacity + BufferResidencyCapacity + allocation.Index();
            return InvalidReferenceIndex;
        }

        [[nodiscard]] ResidencyRecord* FindResidencyRecord(const ResourceRef allocation) noexcept
        {
            const u32 index = ResidencyRecordIndex(allocation);
            if (residencyRecords == nullptr || index == InvalidReferenceIndex) return nullptr;
            ResidencyRecord& record = residencyRecords[index];
            return record.resource == allocation ? &record : nullptr;
        }

        [[nodiscard]] bool InitializeResidencyPolicy(const DeviceParams::ResidencyPolicy& policy) noexcept
        {
            if (residencyRecords != nullptr) return false;
            residencyRecordBlock = memory::Allocate(memory::PoolId::Rendering,
                                                     sizeof(ResidencyRecord) * ResidencyRecordCapacity,
                                                     alignof(ResidencyRecord));
            if (!residencyRecordBlock) return false;
            residencyRecords = static_cast<ResidencyRecord*>(residencyRecordBlock.address);
            for (u32 index = 0; index < ResidencyRecordCapacity; ++index)
                new (&residencyRecords[index]) ResidencyRecord();
            residencyPolicy = policy;
            return true;
        }

        void ReleaseResidencyPolicy() noexcept
        {
            if (residencyRecords == nullptr) return;
            for (u32 index = 0; index < ResidencyRecordCapacity; ++index)
                residencyRecords[index].~ResidencyRecord();
            memory::Free(residencyRecordBlock);
            residencyRecordBlock = {};
            residencyRecords = nullptr;
            residencyStats = {};
            residencyUseOrdinal = 0;
        }

        void RemoveRecordAccounting(const ResidencyRecord& record) noexcept
        {
            if (!record.resource.IsValid()) return;
            --residencyStats.trackedAllocations;
            if (record.state == AllocationResidency::Resident)
                residencyStats.trackedResidentBytes -= record.size;
            else
                residencyStats.trackedEvictedBytes -= record.size;
            if (record.pinned) --residencyStats.policyPinnedObjects;
        }

        [[nodiscard]] bool RegisterAllocation(const ResourceRef allocation, const u64 size,
                                              const ResidencyFenceSet& initialUse = {}) noexcept
        {
            const u32 index = ResidencyRecordIndex(allocation);
            if (residencyRecords == nullptr || index == InvalidReferenceIndex || size == 0) return false;
            concurrency::ScopedLock guard(residencyLock);
            ResidencyRecord& record = residencyRecords[index];
            RemoveRecordAccounting(record);
            record = {};
            record.resource = allocation;
            record.size = size;
            record.lastUse = initialUse;
            record.lastUseOrdinal = ++residencyUseOrdinal;
            ++residencyStats.trackedAllocations;
            residencyStats.trackedResidentBytes += size;
            return true;
        }

        void MarkResident(const ResourceRef allocation) noexcept
        {
            ResidencyRecord* const record = FindResidencyRecord(allocation);
            if (record == nullptr || record->state == AllocationResidency::Resident) return;
            record->state = AllocationResidency::Resident;
            residencyStats.trackedEvictedBytes -= record->size;
            residencyStats.trackedResidentBytes += record->size;
        }

        void MarkEvicted(const ResourceRef allocation) noexcept
        {
            ResidencyRecord* const record = FindResidencyRecord(allocation);
            if (record == nullptr || record->state == AllocationResidency::Evicted) return;
            record->state = AllocationResidency::Evicted;
            residencyStats.trackedResidentBytes -= record->size;
            residencyStats.trackedEvictedBytes += record->size;
        }

        static BackendStatus PrepareWorkingSet(void* const context,
                                               const containers::ArraySpan<const ResourceRef> resources) noexcept
        {
            auto& impl = *static_cast<Impl*>(context);
            ID3D12Pageable** const resolved = impl.submissionResidencyPageables;
            u32 resolvedCount = 0;
            const BackendStatus resolution = ResolveResidencyBatch(impl.common, resources, resolved, resolvedCount);
            if (!resolution) return resolution;

            ID3D12Pageable** const evicted = impl.submissionEvictedPageables;
            u32 evictedCount = 0;
            {
                concurrency::ScopedLock guard(impl.residencyLock);
                ++impl.residencyStats.automaticWorkingSetChecks;
                for (u32 index = 0; index < resolvedCount; ++index)
                {
                    const ResidencyRecord* const record = impl.FindResidencyRecord(resources[index]);
                    if (record != nullptr && record->state == AllocationResidency::Evicted)
                        evicted[evictedCount++] = resolved[index];
                }
            }
            if (evictedCount == 0) return BackendStatus::Success();

            const HRESULT result = impl.residencyDevice->MakeResident(evictedCount, evicted);
            concurrency::ScopedLock guard(impl.residencyLock);
            if (FAILED(result))
            {
                ++impl.residencyStats.automaticWorkingSetFailures;
                ++impl.residencyStats.residencyFailures;
                return BackendStatus::Failure(result == E_OUTOFMEMORY ? FailureCode::OutOfMemory
                                                                      : FailureCode::BackendFailure,
                                              result, "D3D12 failed to prepare the command-list residency working set");
            }
            ++impl.residencyStats.automaticMakeResidentCalls;
            impl.residencyStats.automaticObjectsMadeResident += evictedCount;
            for (const ResourceRef allocation : resources) impl.MarkResident(allocation);
            return BackendStatus::Success();
        }

        static void CommitWorkingSet(void* const context,
                                     const containers::ArraySpan<const ResourceRef> resources,
                                     const GpuFence completion) noexcept
        {
            if (!completion.IsValid()) return;
            auto& impl = *static_cast<Impl*>(context);
            concurrency::ScopedLock guard(impl.residencyLock);
            const u64 ordinal = ++impl.residencyUseOrdinal;
            for (const ResourceRef allocation : resources)
            {
                ResidencyRecord* const record = impl.FindResidencyRecord(allocation);
                if (record == nullptr) continue;
                record->lastUse.Include(completion);
                record->lastUseOrdinal = ordinal;
            }
        }

        static void ReleaseResidencyRecord(void* const context, const ResourceRef resource) noexcept
        {
            auto& impl = *static_cast<Impl*>(context);
            const u32 index = ResidencyRecordIndex(resource);
            if (impl.residencyRecords == nullptr || index == InvalidReferenceIndex) return;
            concurrency::ScopedLock guard(impl.residencyLock);
            ResidencyRecord& record = impl.residencyRecords[index];
            if (record.resource != resource) return;
            impl.RemoveRecordAccounting(record);
            record = {};
        }

        [[nodiscard]] static bool BetterEvictionCandidate(const EvictionCandidate& left,
                                                          const EvictionCandidate& right) noexcept
        {
            if (left.priority != right.priority)
                return left.priority < right.priority;
            if (left.lastUseOrdinal != right.lastUseOrdinal)
                return left.lastUseOrdinal < right.lastUseOrdinal;
            return left.resource.value < right.resource.value;
        }

        static BackendStatus MaintainResidencyPolicy(void* const context) noexcept
        {
            auto& impl = *static_cast<Impl*>(context);
            if (!impl.residencyPolicy.enabled || impl.budgetAdapter == nullptr || impl.residencyDevice == nullptr)
                return BackendStatus::Success();

            DXGI_QUERY_VIDEO_MEMORY_INFO info{};
            const HRESULT budgetResult = impl.budgetAdapter->QueryVideoMemoryInfo(
                0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info);
            if (FAILED(budgetResult))
            {
                concurrency::ScopedLock guard(impl.residencyLock);
                ++impl.residencyStats.policyFailures;
                ++impl.residencyStats.residencyFailures;
                return BackendStatus::Failure(FailureCode::BackendFailure, budgetResult,
                                              "DXGI memory-budget query failed during residency maintenance");
            }

            const u64 pressureUsage = PercentageOf(info.Budget, impl.residencyPolicy.pressureThresholdPercent);
            const u64 recoveryUsage = PercentageOf(info.Budget, impl.residencyPolicy.recoveryThresholdPercent);
            {
                concurrency::ScopedLock guard(impl.residencyLock);
                ++impl.residencyStats.policyMaintenanceCalls;
                ++impl.residencyStats.budgetQueries;
                impl.residencyStats.lastObservedBudget = info.Budget;
                impl.residencyStats.lastObservedUsage = info.CurrentUsage;
                if (info.Budget == 0 || info.CurrentUsage <= pressureUsage)
                    return BackendStatus::Success();
                ++impl.residencyStats.policyPressureEvents;
            }

            const u32 limit = impl.residencyPolicy.maximumEvictionsPerMaintenance;
            EvictionCandidate* const candidates = impl.policyCandidates;
            u32 candidateCount = 0;

            const auto worse = [](const EvictionCandidate& left, const EvictionCandidate& right) noexcept
            { return BetterEvictionCandidate(right, left); };
            const auto siftDown = [&](const u32 start) noexcept
            {
                u32 parent = start;
                while (true)
                {
                    const u32 left = parent * 2u + 1u;
                    if (left >= candidateCount) break;
                    const u32 right = left + 1u;
                    u32 worst = left;
                    if (right < candidateCount && worse(candidates[right], candidates[left])) worst = right;
                    if (!worse(candidates[worst], candidates[parent])) break;
                    const EvictionCandidate temporary = candidates[parent];
                    candidates[parent] = candidates[worst];
                    candidates[worst] = temporary;
                    parent = worst;
                }
            };

            {
                concurrency::ScopedLock guard(impl.residencyLock);
                for (u32 index = 0; index < ResidencyRecordCapacity; ++index)
                {
                    ResidencyRecord& record = impl.residencyRecords[index];
                    if (!record.resource.IsValid()) continue;
                    if (!impl.common.IsResourceReferenceValid(record.resource))
                    {
                        impl.RemoveRecordAccounting(record);
                        record = {};
                        continue;
                    }
                    if (record.state != AllocationResidency::Resident || record.pinned) continue;
                    if ((record.lastUse.graphics != 0 &&
                         !impl.common.IsGpuFenceComplete({QueueType::Graphics, record.lastUse.graphics})) ||
                        (record.lastUse.compute != 0 &&
                         !impl.common.IsGpuFenceComplete({QueueType::Compute, record.lastUse.compute})) ||
                        (record.lastUse.copy != 0 &&
                         !impl.common.IsGpuFenceComplete({QueueType::Copy, record.lastUse.copy})))
                    {
                        ++impl.residencyStats.policyInFlightSkips;
                        continue;
                    }

                    const EvictionCandidate candidate{record.resource, record.size, record.lastUseOrdinal, record.priority};
                    if (candidateCount < limit)
                    {
                        if (!impl.common.TryAddRef(candidate.resource)) continue;
                        u32 child = candidateCount++;
                        candidates[child] = candidate;
                        while (child != 0)
                        {
                            const u32 parent = (child - 1u) / 2u;
                            if (!worse(candidates[child], candidates[parent])) break;
                            const EvictionCandidate temporary = candidates[parent];
                            candidates[parent] = candidates[child];
                            candidates[child] = temporary;
                            child = parent;
                        }
                    }
                    else if (BetterEvictionCandidate(candidate, candidates[0]) &&
                             impl.common.TryAddRef(candidate.resource))
                    {
                        static_cast<void>(impl.common.Release(candidates[0].resource));
                        candidates[0] = candidate;
                        siftDown(0);
                    }
                }
            }

            if (candidateCount == 0)
            {
                concurrency::ScopedLock guard(impl.residencyLock);
                ++impl.residencyStats.policyNoCandidateEvents;
                return BackendStatus::Success();
            }

            for (u32 index = 1; index < candidateCount; ++index)
            {
                const EvictionCandidate candidate = candidates[index];
                u32 insertion = index;
                while (insertion != 0 && BetterEvictionCandidate(candidate, candidates[insertion - 1u]))
                {
                    candidates[insertion] = candidates[insertion - 1u];
                    --insertion;
                }
                candidates[insertion] = candidate;
            }

            const u64 requiredBytes = info.CurrentUsage > recoveryUsage ? info.CurrentUsage - recoveryUsage : 0;
            ResourceRef* const resources = impl.policyResources;
            u64 selectedBytes = 0;
            u32 selectedCount = 0;
            while (selectedCount < candidateCount && selectedBytes < requiredBytes)
            {
                resources[selectedCount] = candidates[selectedCount].resource;
                selectedBytes += candidates[selectedCount].size;
                ++selectedCount;
            }

            ID3D12Pageable** const pageables = impl.policyPageables;
            u32 pageableCount = 0;
            const BackendStatus resolution = ResolveResidencyBatch(
                impl.common, {resources, selectedCount}, pageables, pageableCount);
            BackendStatus status = resolution;
            if (status)
            {
                const HRESULT evictResult = impl.residencyDevice->Evict(pageableCount, pageables);
                status = SUCCEEDED(evictResult)
                             ? BackendStatus::Success()
                             : BackendStatus::Failure(FailureCode::BackendFailure, evictResult,
                                                      "D3D12 residency-policy eviction failed");
            }

            {
                concurrency::ScopedLock guard(impl.residencyLock);
                if (status)
                {
                    for (u32 index = 0; index < selectedCount; ++index) impl.MarkEvicted(resources[index]);
                    impl.residencyStats.policyObjectsEvicted += pageableCount;
                    impl.residencyStats.policyBytesEvicted += selectedBytes;
                    impl.residencyStats.objectsEvicted += pageableCount;
                }
                else
                {
                    ++impl.residencyStats.policyFailures;
                    ++impl.residencyStats.residencyFailures;
                }
            }
            for (u32 index = 0; index < candidateCount; ++index)
                static_cast<void>(impl.common.Release(candidates[index].resource));
            return status;
        }

        [[nodiscard]] QueryPoolPayload* FindQueryPool(const QueryPoolRef reference) noexcept
        {
            if (!reference.IsValid() || reference.index >= MaximumQueryPools) return nullptr;
            QueryPoolSlot& slot = queryPools[reference.index];
            return slot.state == QueryPoolSlotState::Active && slot.generation == reference.generation ? slot.payload : nullptr;
        }

        static void DestroyQueryPoolPayload(QueryPoolPayload* const payload) noexcept
        {
            if (payload == nullptr) return;
            if (payload->mapped != nullptr && payload->readback != nullptr)
                payload->readback->Unmap(0, nullptr);
            ReleaseNative(payload->readback);
            ReleaseNative(payload->heap);
            if (payload->openQueries != nullptr)
            {
                memory::MemoryBlock states{payload->openQueries, payload->capacity, memory::PoolId::Rendering};
                memory::Free(states);
            }
            payload->~QueryPoolPayload();
            memory::MemoryBlock block{payload, sizeof(QueryPoolPayload), memory::PoolId::Rendering};
            memory::Free(block);
        }

        void CollectQueryPools(const bool force = false) noexcept
        {
            concurrency::ScopedLock guard(queryPoolLock);
            for (u32 index = 0; index < MaximumQueryPools; ++index)
            {
                QueryPoolSlot& slot = queryPools[index];
                QueryPoolPayload* const payload = slot.payload;
                if (slot.state != QueryPoolSlotState::Retiring || payload == nullptr) continue;
                if (!force)
                {
                    const backend::FenceSet& fences = payload->lastUseFences;
                    if (payload->pendingSubmissions != 0 ||
                        (fences.graphics != 0 && !common.IsGpuFenceComplete({QueueType::Graphics, fences.graphics})) ||
                        (fences.compute != 0 && !common.IsGpuFenceComplete({QueueType::Compute, fences.compute})) ||
                        (fences.copy != 0 && !common.IsGpuFenceComplete({QueueType::Copy, fences.copy})))
                        continue;
                }
                DestroyQueryPoolPayload(payload);
                slot.payload = nullptr;
                if (slot.generation == 0xffffffffu)
                    slot.state = QueryPoolSlotState::Exhausted;
                else
                {
                    ++slot.generation;
                    slot.state = QueryPoolSlotState::Free;
                }
            }
        }

        [[nodiscard]] bool HasActiveQueryPools() noexcept
        {
            concurrency::ScopedLock guard(queryPoolLock);
            for (const QueryPoolSlot& slot : queryPools)
                if (slot.state == QueryPoolSlotState::Active || slot.state == QueryPoolSlotState::Retiring)
                    return true;
            return false;
        }

        static void QueryPoolSubmitted(void* const context, const GpuFence completion, const u64 resolved) noexcept
        {
            auto* const payload = static_cast<QueryPoolPayload*>(context);
            if (payload == nullptr || payload->owner == nullptr) return;
            auto& impl = *static_cast<Impl*>(payload->owner);
            concurrency::ScopedLock guard(impl.queryPoolLock);
            payload->lastUseFences.Include(completion.queue, completion.value);
            if (resolved != 0) payload->lastResolveFence = completion;
            if (payload->pendingSubmissions != 0) --payload->pendingSubmissions;
        }

        [[nodiscard]] bool TrackQueryPoolUse(const CommandListRef commandList, QueryPoolPayload& payload,
                                             const bool resolvesResults = false) noexcept
        {
            bool added = false;
            if (!common.RegisterCommandSubmissionCallback(commandList, &QueryPoolSubmitted, &payload,
                                                          resolvesResults ? 1u : 0u, &added)) return false;
            if (added) ++payload.pendingSubmissions;
            return true;
        }

        void ReleaseDeviceObjects() noexcept
        {
            CollectQueryPools(true);
            ReleaseNative(queueFences[2]);
            ReleaseNative(queueFences[1]);
            ReleaseNative(queueFences[0]);
            ReleaseNative(copyQueue);
            ReleaseNative(computeQueue);
            ReleaseNative(graphicsQueue);
            ReleaseNative(residencyDevice);
            ReleaseNative(nativeDevice);
            ReleaseNative(budgetAdapter);
            ReleaseNative(adapter);
            ReleaseNative(factory);
            ReleaseResidencyPolicy();
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

        [[nodiscard]] static bool RectColorClear(void*, nvrhi::ICommandList* const commandList,
                                                 nvrhi::ITexture* const texture, const ColorValue& value,
                                                 const SubresourceRange& range, const Rect& rectangle) noexcept
        {
            if (commandList == nullptr || texture == nullptr || range.mipCount != 1)
                return false;
            ID3D12GraphicsCommandList* const nativeCommandList =
                commandList->getNativeObject(nvrhi::ObjectTypes::D3D12_GraphicsCommandList);
            const nvrhi::TextureSubresourceSet subresources(range.firstMip, 1, range.firstSlice, range.sliceCount);
            const nvrhi::Object view = texture->getNativeView(nvrhi::ObjectTypes::D3D12_RenderTargetViewDescriptor,
                                                              nvrhi::Format::UNKNOWN, subresources);
            if (nativeCommandList == nullptr || view.integer == 0)
                return false;
            const D3D12_CPU_DESCRIPTOR_HANDLE descriptor{view.integer};
            const D3D12_RECT nativeRectangle{rectangle.x, rectangle.y, rectangle.x + rectangle.width,
                                             rectangle.y + rectangle.height};
            const f32 clearValue[4] = {value.red, value.green, value.blue, value.alpha};
            nativeCommandList->ClearRenderTargetView(descriptor, clearValue, 1, &nativeRectangle);
            return true;
        }

        [[nodiscard]] static bool RectDepthStencilClear(void*, nvrhi::ICommandList* const commandList,
                                                        nvrhi::ITexture* const texture, const bool clearDepth,
                                                        const f32 depth, const bool clearStencil, const u8 stencil,
                                                        const SubresourceRange& range, const Rect& rectangle) noexcept
        {
            if (commandList == nullptr || texture == nullptr || range.mipCount != 1 || (!clearDepth && !clearStencil))
                return false;
            ID3D12GraphicsCommandList* const nativeCommandList =
                commandList->getNativeObject(nvrhi::ObjectTypes::D3D12_GraphicsCommandList);
            const nvrhi::TextureSubresourceSet subresources(range.firstMip, 1, range.firstSlice, range.sliceCount);
            const nvrhi::Object view = texture->getNativeView(nvrhi::ObjectTypes::D3D12_DepthStencilViewDescriptor,
                                                              nvrhi::Format::UNKNOWN, subresources);
            if (nativeCommandList == nullptr || view.integer == 0)
                return false;
            D3D12_CLEAR_FLAGS flags = clearDepth ? D3D12_CLEAR_FLAG_DEPTH : static_cast<D3D12_CLEAR_FLAGS>(0);
            if (clearStencil) flags = static_cast<D3D12_CLEAR_FLAGS>(flags | D3D12_CLEAR_FLAG_STENCIL);
            const D3D12_CPU_DESCRIPTOR_HANDLE descriptor{view.integer};
            const D3D12_RECT nativeRectangle{rectangle.x, rectangle.y, rectangle.x + rectangle.width,
                                             rectangle.y + rectangle.height};
            nativeCommandList->ClearDepthStencilView(descriptor, flags, depth, stencil, 1, &nativeRectangle);
            return true;
        }

        [[nodiscard]] static bool DiscardResource(void*, nvrhi::ICommandList* const commandList,
                                                  nvrhi::IResource* const resource,
                                                  const SubresourceRange* const textureRange) noexcept
        {
            if (commandList == nullptr || resource == nullptr)
                return false;
            ID3D12GraphicsCommandList* const nativeCommandList =
                commandList->getNativeObject(nvrhi::ObjectTypes::D3D12_GraphicsCommandList);
            ID3D12Resource* const nativeResource = resource->getNativeObject(nvrhi::ObjectTypes::D3D12_Resource);
            if (nativeCommandList == nullptr || nativeResource == nullptr)
                return false;
            if (textureRange == nullptr)
            {
                D3D12_DISCARD_REGION region{};
                region.FirstSubresource = 0;
                region.NumSubresources = 1;
                nativeCommandList->DiscardResource(nativeResource, &region);
                return true;
            }
            auto* const texture = static_cast<nvrhi::ITexture*>(resource);
            const u32 mipLevels = texture->getDesc().mipLevels;
            for (u32 slice = 0; slice < textureRange->sliceCount; ++slice)
            {
                D3D12_DISCARD_REGION region{};
                region.FirstSubresource = textureRange->firstMip +
                                          (textureRange->firstSlice + slice) * mipLevels;
                region.NumSubresources = textureRange->mipCount;
                nativeCommandList->DiscardResource(nativeResource, &region);
            }
            return true;
        }
    };

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

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
        if (!m_impl->InitializeResidencyPolicy(params.residencyPolicy))
            return BackendStatus::Failure(FailureCode::OutOfMemory, 0,
                                          "could not allocate the GPU residency-policy registry");

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
        static_cast<void>(m_impl->adapter->QueryInterface(IID_PPV_ARGS(&m_impl->budgetAdapter)));
        static_cast<void>(m_impl->nativeDevice->QueryInterface(IID_PPV_ARGS(&m_impl->residencyDevice)));

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
                                       &Impl::WaitFence, &Impl::AliasingBarrier, &Impl::RectColorClear,
                                       &Impl::RectDepthStencilClear, &Impl::DiscardResource,
                                       &Impl::PrepareWorkingSet, &Impl::CommitWorkingSet,
                                       &Impl::ReleaseResidencyRecord, m_impl))
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
        caps.rayTracing = false;
        caps.rayTracingPipeline = false;
        caps.meshShaders = false;
        caps.variableRateShading = false;
        caps.occlusionQueries = true;
        caps.pipelineStatisticsQueries = true;
        caps.timestampQueries = true;
        caps.timestampCalibration = true;
        caps.gpuMarkers = true;
        caps.memoryBudgetQueries = m_impl->budgetAdapter != nullptr;
        caps.explicitResidency = m_impl->budgetAdapter != nullptr && m_impl->residencyDevice != nullptr;
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
        m_impl->CollectQueryPools();
        if (m_impl->HasActiveQueryPools())
            return BackendStatus::Failure(FailureCode::Busy, 0, "RHI backend still owns live query pools");
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
        m_impl->CollectQueryPools();
        return m_impl->common.ExecuteSerializedQueueOperation(&Impl::MaintainResidencyPolicy, m_impl);
    }

    BackendStatus Backend::FlushRetiredResources() noexcept
    {
        if (m_impl == nullptr || !m_impl->initialized || !m_impl->common.IsInitialized())
            return BackendStatus::Failure(FailureCode::NotInitialized, 0, "D3D12 backend is not initialized");
        if (!m_impl->common.WaitIdle())
            return BackendStatus::Failure(FailureCode::DeviceLost, m_impl->nativeDevice->GetDeviceRemovedReason(),
                                          "D3D12 device failed while draining retired resources");
        m_impl->common.DrainRetiredResourcesAfterGpuIdle();
        m_impl->CollectQueryPools();
        return BackendStatus::Success();
    }

    BackendStatus Backend::QueryMemoryBudget(const MemorySegment segment, MemoryBudgetSnapshot& budget) noexcept
    {
        budget = {};
        budget.segment = segment;
        if (m_impl == nullptr || !m_impl->initialized || m_impl->budgetAdapter == nullptr)
            return BackendStatus::Failure(FailureCode::Unsupported, 0, "DXGI memory-budget queries are unavailable");
        DXGI_QUERY_VIDEO_MEMORY_INFO info{};
        const HRESULT result = m_impl->budgetAdapter->QueryVideoMemoryInfo(
            0, segment == MemorySegment::Local ? DXGI_MEMORY_SEGMENT_GROUP_LOCAL : DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &info);
        if (FAILED(result))
            return BackendStatus::Failure(FailureCode::BackendFailure, result, "DXGI video-memory budget query failed");
        budget.budget = info.Budget;
        budget.currentUsage = info.CurrentUsage;
        budget.availableForReservation = info.AvailableForReservation;
        budget.currentReservation = info.CurrentReservation;
        concurrency::ScopedLock guard(m_impl->residencyLock);
        ++m_impl->residencyStats.budgetQueries;
        return BackendStatus::Success();
    }

    BackendStatus Backend::SetResidencyPriority(const ResourceRef resource, const ResidencyPriority priority) noexcept
    {
        if (m_impl == nullptr || !m_impl->initialized || m_impl->residencyDevice == nullptr)
            return BackendStatus::Failure(FailureCode::Unsupported, 0, "explicit D3D12 residency is unavailable");
        const ResourceRef resources[] = {resource};
        ID3D12Pageable* pageables[1]{};
        u32 count = 0;
        const BackendStatus resolved = ResolveResidencyBatch(m_impl->common, {resources, 1}, pageables, count);
        if (!resolved) return resolved;
        struct Operation
        {
            ID3D12Device1* device;
            ID3D12Pageable* pageable;
            D3D12_RESIDENCY_PRIORITY priority;
        } operation{m_impl->residencyDevice, pageables[0], ToNativeResidencyPriority(priority)};
        const BackendStatus status = m_impl->common.ExecuteSerializedQueueOperation(
            [](void* const context) noexcept
            {
                auto& value = *static_cast<Operation*>(context);
                const HRESULT result = value.device->SetResidencyPriority(1, &value.pageable, &value.priority);
                return SUCCEEDED(result) ? BackendStatus::Success()
                                         : BackendStatus::Failure(FailureCode::BackendFailure, result,
                                                                  "D3D12 residency-priority update failed");
            },
            &operation);
        concurrency::ScopedLock guard(m_impl->residencyLock);
        if (status)
        {
            ++m_impl->residencyStats.priorityChanges;
            const ResourceRef allocation = m_impl->common.GetResidencyAllocation(resource);
            Impl::ResidencyRecord* const record = m_impl->FindResidencyRecord(allocation);
            if (record != nullptr) record->priority = priority;
        }
        else
            ++m_impl->residencyStats.residencyFailures;
        return status;
    }

    BackendStatus Backend::SetResidencyPinned(const ResourceRef resource, const bool pinned) noexcept
    {
        if (m_impl == nullptr || !m_impl->initialized)
            return BackendStatus::Failure(FailureCode::NotInitialized, 0, "D3D12 backend is not initialized");
        const ResourceRef allocation = m_impl->common.GetResidencyAllocation(resource);
        concurrency::ScopedLock guard(m_impl->residencyLock);
        Impl::ResidencyRecord* const record = m_impl->FindResidencyRecord(allocation);
        if (record == nullptr)
            return BackendStatus::Failure(FailureCode::IncompatibleBinding, 0,
                                          "GPU allocation is not managed by the residency policy");
        if (record->pinned == pinned) return BackendStatus::Success();
        record->pinned = pinned;
        if (pinned)
            ++m_impl->residencyStats.policyPinnedObjects;
        else
            --m_impl->residencyStats.policyPinnedObjects;
        return BackendStatus::Success();
    }

    BackendStatus Backend::MakeResident(const containers::ArraySpan<const ResourceRef> resources) noexcept
    {
        if (m_impl == nullptr || !m_impl->initialized || m_impl->residencyDevice == nullptr)
            return BackendStatus::Failure(FailureCode::Unsupported, 0, "explicit D3D12 residency is unavailable");
        memory::MemoryBlock pageableBlock = memory::Allocate(
            memory::PoolId::Rendering, sizeof(ID3D12Pageable*) * resources.Size(), alignof(ID3D12Pageable*));
        if (!pageableBlock)
            return BackendStatus::Failure(FailureCode::OutOfMemory, 0, "could not allocate GPU residency batch storage");
        auto** const pageables = static_cast<ID3D12Pageable**>(pageableBlock.address);
        u32 count = 0;
        const BackendStatus resolved = ResolveResidencyBatch(m_impl->common, resources, pageables, count);
        if (!resolved)
        {
            memory::Free(pageableBlock);
            return resolved;
        }
        struct Operation
        {
            Impl* impl;
            ID3D12Pageable** pageables;
            u32 count;
            containers::ArraySpan<const ResourceRef> resources;
        } operation{m_impl, pageables, count, resources};
        const BackendStatus status = m_impl->common.ExecuteSerializedQueueOperation(
            [](void* const context) noexcept
            {
                auto& value = *static_cast<Operation*>(context);
                const HRESULT result = value.impl->residencyDevice->MakeResident(value.count, value.pageables);
                if (FAILED(result))
                    return BackendStatus::Failure(result == E_OUTOFMEMORY ? FailureCode::OutOfMemory
                                                                          : FailureCode::BackendFailure,
                                                  result, "D3D12 failed to make the GPU working set resident");
                concurrency::ScopedLock guard(value.impl->residencyLock);
                for (const ResourceRef resource : value.resources)
                    value.impl->MarkResident(value.impl->common.GetResidencyAllocation(resource));
                return BackendStatus::Success();
            },
            &operation);
        {
            concurrency::ScopedLock guard(m_impl->residencyLock);
            ++m_impl->residencyStats.makeResidentCalls;
            if (status)
                m_impl->residencyStats.objectsMadeResident += count;
            else
                ++m_impl->residencyStats.residencyFailures;
        }
        memory::Free(pageableBlock);
        return status;
    }

    BackendStatus Backend::Evict(const containers::ArraySpan<const ResourceRef> resources,
                                 const ResidencyFenceSet& safeAfter) noexcept
    {
        if (m_impl == nullptr || !m_impl->initialized || m_impl->residencyDevice == nullptr)
            return BackendStatus::Failure(FailureCode::Unsupported, 0, "explicit D3D12 residency is unavailable");
        memory::MemoryBlock pageableBlock = memory::Allocate(
            memory::PoolId::Rendering, sizeof(ID3D12Pageable*) * resources.Size(), alignof(ID3D12Pageable*));
        if (!pageableBlock)
            return BackendStatus::Failure(FailureCode::OutOfMemory, 0, "could not allocate GPU eviction batch storage");
        auto** const pageables = static_cast<ID3D12Pageable**>(pageableBlock.address);
        u32 count = 0;
        const BackendStatus resolved = ResolveResidencyBatch(m_impl->common, resources, pageables, count);
        if (!resolved)
        {
            memory::Free(pageableBlock);
            return resolved;
        }
        struct Operation
        {
            Impl* impl;
            ID3D12Pageable** pageables;
            u32 count;
            ResidencyFenceSet safeAfter;
            containers::ArraySpan<const ResourceRef> resources;
        } operation{m_impl, pageables, count, safeAfter, resources};
        const BackendStatus status = m_impl->common.ExecuteSerializedQueueOperation(
            [](void* const context) noexcept
            {
                auto& value = *static_cast<Operation*>(context);
                if ((value.safeAfter.graphics != 0 &&
                     !value.impl->common.IsGpuFenceComplete({QueueType::Graphics, value.safeAfter.graphics})) ||
                    (value.safeAfter.compute != 0 &&
                     !value.impl->common.IsGpuFenceComplete({QueueType::Compute, value.safeAfter.compute})) ||
                    (value.safeAfter.copy != 0 &&
                     !value.impl->common.IsGpuFenceComplete({QueueType::Copy, value.safeAfter.copy})))
                    return BackendStatus::Failure(FailureCode::Busy, 0, "GPU resources are still in flight and cannot be evicted");
                {
                    concurrency::ScopedLock guard(value.impl->residencyLock);
                    for (const ResourceRef resource : value.resources)
                    {
                        const ResourceRef allocation = value.impl->common.GetResidencyAllocation(resource);
                        const Impl::ResidencyRecord* const record = value.impl->FindResidencyRecord(allocation);
                        if (record != nullptr && record->pinned)
                            return BackendStatus::Failure(FailureCode::Busy, 0,
                                                          "pinned GPU allocations cannot be evicted");
                    }
                }
                const HRESULT result = value.impl->residencyDevice->Evict(value.count, value.pageables);
                if (FAILED(result))
                    return BackendStatus::Failure(FailureCode::BackendFailure, result,
                                                  "D3D12 GPU-memory eviction failed");
                concurrency::ScopedLock guard(value.impl->residencyLock);
                for (const ResourceRef resource : value.resources)
                    value.impl->MarkEvicted(value.impl->common.GetResidencyAllocation(resource));
                return BackendStatus::Success();
            },
            &operation);
        {
            concurrency::ScopedLock guard(m_impl->residencyLock);
            ++m_impl->residencyStats.evictCalls;
            if (status)
                m_impl->residencyStats.objectsEvicted += count;
            else if (status.code == FailureCode::Busy)
                ++m_impl->residencyStats.rejectedEvictions;
            else
                ++m_impl->residencyStats.residencyFailures;
        }
        memory::Free(pageableBlock);
        return status;
    }

    ResidencyStats Backend::GetResidencyStats() const noexcept
    {
        if (m_impl == nullptr) return {};
        concurrency::ScopedLock guard(m_impl->residencyLock);
        return m_impl->residencyStats;
    }

    TextureRef Backend::CreateTexture(const TextureDesc& desc, const TextureInitData& data) noexcept
    {
        const TextureRef texture = m_impl->common.CreateTexture(desc, data);
        if (!texture || desc.virtualResource) return texture;
        const MemoryRequirements requirements = m_impl->common.GetMemoryRequirements(texture);
        const backend::FenceSet use = m_impl->common.GetResourceLastUse(ResourceRef(texture));
        if (!m_impl->RegisterAllocation(ResourceRef(texture), requirements.size,
                                        {use.graphics, use.compute, use.copy}))
        {
            static_cast<void>(m_impl->common.Release(ResourceRef(texture)));
            return {};
        }
        return texture;
    }
    BufferRef Backend::CreateBuffer(const BufferDesc& desc, const BufferInitData& data) noexcept
    {
        const BufferRef buffer = m_impl->common.CreateBuffer(desc, data);
        if (!buffer || desc.virtualResource || desc.memoryType != MemoryType::DeviceLocal) return buffer;
        const MemoryRequirements requirements = m_impl->common.GetMemoryRequirements(buffer);
        const backend::FenceSet use = m_impl->common.GetResourceLastUse(ResourceRef(buffer));
        if (!m_impl->RegisterAllocation(ResourceRef(buffer), requirements.size,
                                        {use.graphics, use.compute, use.copy}))
        {
            static_cast<void>(m_impl->common.Release(ResourceRef(buffer)));
            return {};
        }
        return buffer;
    }
    HeapRef Backend::CreateHeap(const HeapDesc& desc) noexcept
    {
        const HeapRef heap = m_impl->common.CreateHeap(desc);
        if (!heap || desc.memoryType != MemoryType::DeviceLocal) return heap;
        if (!m_impl->RegisterAllocation(ResourceRef(heap), desc.size))
        {
            static_cast<void>(m_impl->common.Release(ResourceRef(heap)));
            return {};
        }
        return heap;
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
    BackendStatus Backend::WriteDescriptor(DescriptorDomainRef, DescriptorHandle, AccelerationStructureRef) noexcept
    {
        return BackendStatus::Failure(FailureCode::Unsupported, 0,
                                      "ray-tracing acceleration-structure descriptors are not implemented");
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
    AccelerationStructureRef Backend::CreateAccelerationStructure(const AccelerationStructureDesc&) noexcept { return {}; }
    ShaderTableRef Backend::CreateShaderTable(const ShaderTableDesc&) noexcept { return {}; }
    BackendStatus Backend::GetAccelerationStructureDeviceAddress(AccelerationStructureRef, u64&) noexcept
    {
        return BackendStatus::Failure(FailureCode::Unsupported, 0,
                                      "acceleration-structure device addresses are not implemented");
    }
    QueryPoolRef Backend::CreateQueryPool(const QueryPoolDesc& desc) noexcept
    {
        if (m_impl == nullptr || !m_impl->initialized || desc.capacity == 0 || desc.capacity > MaximumQueryPoolEntries ||
            desc.type == QueryType::AccelerationStructureCompactedSize)
            return {};
        concurrency::ScopedLock guard(m_impl->queryPoolLock);
        u32 slotIndex = MaximumQueryPools;
        for (u32 index = 0; index < MaximumQueryPools; ++index)
            if (m_impl->queryPools[index].state == QueryPoolSlotState::Free)
            {
                slotIndex = index;
                break;
            }
        if (slotIndex == MaximumQueryPools) return {};

        memory::MemoryBlock payloadBlock = memory::Allocate(memory::PoolId::Rendering, sizeof(QueryPoolPayload), alignof(QueryPoolPayload));
        if (!payloadBlock) return {};
        auto* const payload = new (payloadBlock.address) QueryPoolPayload{};
        memory::MemoryBlock stateBlock = memory::Allocate(memory::PoolId::Rendering, desc.capacity, alignof(u8));
        if (!stateBlock)
        {
            Impl::DestroyQueryPoolPayload(payload);
            return {};
        }
        payload->openQueries = static_cast<u8*>(stateBlock.address);
        for (u32 index = 0; index < desc.capacity; ++index) payload->openQueries[index] = 0;
        payload->owner = m_impl;
        payload->type = desc.type;
        payload->capacity = desc.capacity;
        payload->resultStride = QueryResultStride(desc.type);

        D3D12_QUERY_HEAP_DESC heapDesc{};
        heapDesc.Type = ToNativeQueryHeapType(desc.type);
        heapDesc.Count = desc.capacity;
        HRESULT result = m_impl->nativeDevice->CreateQueryHeap(&heapDesc, IID_PPV_ARGS(&payload->heap));
        if (FAILED(result))
        {
            Impl::DestroyQueryPoolPayload(payload);
            return {};
        }
        D3D12_HEAP_PROPERTIES heapProperties{};
        heapProperties.Type = D3D12_HEAP_TYPE_READBACK;
        heapProperties.CreationNodeMask = 1;
        heapProperties.VisibleNodeMask = 1;
        D3D12_RESOURCE_DESC bufferDesc{};
        bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bufferDesc.Width = static_cast<u64>(desc.capacity) * payload->resultStride;
        bufferDesc.Height = 1;
        bufferDesc.DepthOrArraySize = 1;
        bufferDesc.MipLevels = 1;
        bufferDesc.SampleDesc.Count = 1;
        bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        result = m_impl->nativeDevice->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE, &bufferDesc,
                                                                D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                                IID_PPV_ARGS(&payload->readback));
        if (FAILED(result))
        {
            Impl::DestroyQueryPoolPayload(payload);
            return {};
        }
        QueryPoolSlot& slot = m_impl->queryPools[slotIndex];
        payload->reference = {slotIndex, slot.generation};
        slot.payload = payload;
        slot.state = QueryPoolSlotState::Active;
        SetNativeName(payload->heap, "Vanguard Query Heap");
        SetNativeName(payload->readback, "Vanguard Query Readback");
        return payload->reference;
    }
    void Backend::DestroyQueryPool(const QueryPoolRef queryPool) noexcept
    {
        if (m_impl == nullptr) return;
        {
            concurrency::ScopedLock guard(m_impl->queryPoolLock);
            QueryPoolPayload* const payload = m_impl->FindQueryPool(queryPool);
            if (payload == nullptr) return;
            if (payload->mapped != nullptr)
            {
                payload->readback->Unmap(0, nullptr);
                payload->mapped = nullptr;
            }
            m_impl->queryPools[queryPool.index].state = QueryPoolSlotState::Retiring;
        }
        m_impl->CollectQueryPools();
    }

    BackendStatus Backend::BeginQuery(const CommandListRef commandList, const QueryPoolRef queryPool, const u32 index) noexcept
    {
        concurrency::ScopedLock guard(m_impl->queryPoolLock);
        QueryPoolPayload* const payload = m_impl->FindQueryPool(queryPool);
        nvrhi::ICommandList* const command = m_impl->common.GetNativeCommandList(commandList);
        ID3D12GraphicsCommandList* const native = command != nullptr
            ? command->getNativeObject(nvrhi::ObjectTypes::D3D12_GraphicsCommandList) : nullptr;
        if (payload == nullptr || native == nullptr) return BackendStatus::Failure(FailureCode::InvalidReference, 0, "invalid query pool or command list");
        if (!IsRangedQuery(payload->type)) return BackendStatus::Failure(FailureCode::InvalidArgument, 0, "timestamp queries must be issued, not begun");
        if (index >= payload->capacity || payload->openQueries[index] != 0 ||
            (payload->mapped != nullptr && index >= payload->mappedStart && index < payload->mappedEnd))
            return BackendStatus::Failure(FailureCode::InvalidArgument, 0, "query index is unavailable for begin");
        if (payload->type == QueryType::PipelineStatistics && m_impl->common.GetCommandListType(commandList) != CommandListType::Default)
            return BackendStatus::Failure(FailureCode::Unsupported, 0, "pipeline statistics require a graphics command list");
        if (!m_impl->TrackQueryPoolUse(commandList, *payload)) return BackendStatus::Failure(FailureCode::OutOfMemory, 0, "failed to retain query pool use");
        native->BeginQuery(payload->heap, ToNativeQueryType(payload->type), index);
        payload->openQueries[index] = 1;
        return BackendStatus::Success();
    }

    BackendStatus Backend::EndQuery(const CommandListRef commandList, const QueryPoolRef queryPool, const u32 index) noexcept
    {
        concurrency::ScopedLock guard(m_impl->queryPoolLock);
        QueryPoolPayload* const payload = m_impl->FindQueryPool(queryPool);
        nvrhi::ICommandList* const command = m_impl->common.GetNativeCommandList(commandList);
        ID3D12GraphicsCommandList* const native = command != nullptr
            ? command->getNativeObject(nvrhi::ObjectTypes::D3D12_GraphicsCommandList) : nullptr;
        if (payload == nullptr || native == nullptr) return BackendStatus::Failure(FailureCode::InvalidReference, 0, "invalid query pool or command list");
        if (!IsRangedQuery(payload->type) || index >= payload->capacity || payload->openQueries[index] == 0)
            return BackendStatus::Failure(FailureCode::InvalidArgument, 0, "query was not begun");
        if (!m_impl->TrackQueryPoolUse(commandList, *payload)) return BackendStatus::Failure(FailureCode::OutOfMemory, 0, "failed to retain query pool use");
        native->EndQuery(payload->heap, ToNativeQueryType(payload->type), index);
        payload->openQueries[index] = 0;
        return BackendStatus::Success();
    }

    BackendStatus Backend::IssueQuery(const CommandListRef commandList, const QueryPoolRef queryPool, const u32 index) noexcept
    {
        concurrency::ScopedLock guard(m_impl->queryPoolLock);
        QueryPoolPayload* const payload = m_impl->FindQueryPool(queryPool);
        nvrhi::ICommandList* const command = m_impl->common.GetNativeCommandList(commandList);
        ID3D12GraphicsCommandList* const native = command != nullptr
            ? command->getNativeObject(nvrhi::ObjectTypes::D3D12_GraphicsCommandList) : nullptr;
        if (payload == nullptr || native == nullptr) return BackendStatus::Failure(FailureCode::InvalidReference, 0, "invalid query pool or command list");
        if (payload->type != QueryType::Timestamp || index >= payload->capacity ||
            (payload->mapped != nullptr && index >= payload->mappedStart && index < payload->mappedEnd))
            return BackendStatus::Failure(FailureCode::InvalidArgument, 0, "query cannot be issued at this index");
        if (!m_impl->TrackQueryPoolUse(commandList, *payload)) return BackendStatus::Failure(FailureCode::OutOfMemory, 0, "failed to retain query pool use");
        native->EndQuery(payload->heap, D3D12_QUERY_TYPE_TIMESTAMP, index);
        return BackendStatus::Success();
    }

    BackendStatus Backend::ResolveQueries(const CommandListRef commandList, const QueryPoolRef queryPool, const u32 start,
                                          const u32 count) noexcept
    {
        concurrency::ScopedLock guard(m_impl->queryPoolLock);
        QueryPoolPayload* const payload = m_impl->FindQueryPool(queryPool);
        nvrhi::ICommandList* const command = m_impl->common.GetNativeCommandList(commandList);
        ID3D12GraphicsCommandList* const native = command != nullptr
            ? command->getNativeObject(nvrhi::ObjectTypes::D3D12_GraphicsCommandList) : nullptr;
        if (payload == nullptr || native == nullptr) return BackendStatus::Failure(FailureCode::InvalidReference, 0, "invalid query pool or command list");
        if (m_impl->common.GetCommandListType(commandList) != CommandListType::Default)
            return BackendStatus::Failure(FailureCode::Unsupported, 0, "query resolution requires a graphics command list");
        if (count == 0 || start >= payload->capacity || count > payload->capacity - start || payload->mapped != nullptr)
            return BackendStatus::Failure(FailureCode::InvalidArgument, 0, "invalid query resolve range");
        for (u32 index = start; index < start + count; ++index)
            if (payload->openQueries[index] != 0)
                return BackendStatus::Failure(FailureCode::Busy, 0, "query resolve range contains an open query");
        if (!m_impl->TrackQueryPoolUse(commandList, *payload, true)) return BackendStatus::Failure(FailureCode::OutOfMemory, 0, "failed to retain query pool use");
        native->ResolveQueryData(payload->heap, ToNativeQueryType(payload->type), start, count, payload->readback,
                                 static_cast<u64>(start) * payload->resultStride);
        return BackendStatus::Success();
    }

    BackendStatus Backend::AcquireQueries(const QueryPoolRef queryPool, const u32 start, const u32 count) noexcept
    {
        concurrency::ScopedLock guard(m_impl->queryPoolLock);
        QueryPoolPayload* const payload = m_impl->FindQueryPool(queryPool);
        if (payload == nullptr) return BackendStatus::Failure(FailureCode::InvalidReference, 0, "invalid query pool");
        if (count == 0 || start >= payload->capacity || count > payload->capacity - start || payload->mapped != nullptr)
            return BackendStatus::Failure(FailureCode::InvalidArgument, 0, "invalid query acquisition range");
        if (payload->pendingSubmissions != 0 || !payload->lastResolveFence.IsValid() ||
            !m_impl->common.IsGpuFenceComplete(payload->lastResolveFence))
            return BackendStatus::Failure(FailureCode::Busy, 0, "query results are not ready");
        D3D12_RANGE range{static_cast<SIZE_T>(start) * payload->resultStride,
                          static_cast<SIZE_T>(start + count) * payload->resultStride};
        const HRESULT result = payload->readback->Map(0, &range, &payload->mapped);
        if (FAILED(result) || payload->mapped == nullptr)
        {
            payload->mapped = nullptr;
            return BackendStatus::Failure(FailureCode::BackendFailure, result, "failed to map query results");
        }
        payload->mappedStart = start;
        payload->mappedEnd = start + count;
        return BackendStatus::Success();
    }

    void Backend::ReleaseQueries(const QueryPoolRef queryPool) noexcept
    {
        concurrency::ScopedLock guard(m_impl->queryPoolLock);
        QueryPoolPayload* const payload = m_impl->FindQueryPool(queryPool);
        if (payload == nullptr || payload->mapped == nullptr) return;
        const D3D12_RANGE written{};
        payload->readback->Unmap(0, &written);
        payload->mapped = nullptr;
        payload->mappedStart = 0;
        payload->mappedEnd = 0;
    }

    BackendStatus Backend::GetQueryResult(const QueryPoolRef queryPool, const u32 index, u64& result) noexcept
    {
        result = 0;
        concurrency::ScopedLock guard(m_impl->queryPoolLock);
        QueryPoolPayload* const payload = m_impl->FindQueryPool(queryPool);
        if (payload == nullptr || payload->type == QueryType::PipelineStatistics || payload->mapped == nullptr ||
            index < payload->mappedStart || index >= payload->mappedEnd)
            return BackendStatus::Failure(FailureCode::InvalidArgument, 0, "query result is not in the acquired range");
        result = static_cast<const u64*>(payload->mapped)[index];
        return BackendStatus::Success();
    }

    BackendStatus Backend::GetQueryResult(const QueryPoolRef queryPool, const u32 index, PipelineStatistics& result) noexcept
    {
        result = {};
        concurrency::ScopedLock guard(m_impl->queryPoolLock);
        QueryPoolPayload* const payload = m_impl->FindQueryPool(queryPool);
        if (payload == nullptr || payload->type != QueryType::PipelineStatistics || payload->mapped == nullptr ||
            index < payload->mappedStart || index >= payload->mappedEnd)
            return BackendStatus::Failure(FailureCode::InvalidArgument, 0, "pipeline statistics are not in the acquired range");
        const auto& native = static_cast<const D3D12_QUERY_DATA_PIPELINE_STATISTICS*>(payload->mapped)[index];
        result = {native.IAVertices, native.IAPrimitives, native.VSInvocations, native.GSInvocations, native.GSPrimitives,
                  native.CInvocations, native.CPrimitives, native.PSInvocations, native.HSInvocations,
                  native.DSInvocations, native.CSInvocations};
        return BackendStatus::Success();
    }

    BackendStatus Backend::GetTimestampFrequency(const QueueType queue, u64& frequency) const noexcept
    {
        frequency = 0;
        ID3D12CommandQueue* const native = queue == QueueType::Graphics ? m_impl->graphicsQueue
            : queue == QueueType::Compute ? m_impl->computeQueue : m_impl->copyQueue;
        if (native == nullptr) return BackendStatus::Failure(FailureCode::Unsupported, 0, "command queue is unavailable");
        const HRESULT result = native->GetTimestampFrequency(&frequency);
        return SUCCEEDED(result) && frequency != 0 ? BackendStatus::Success()
            : BackendStatus::Failure(FailureCode::BackendFailure, result, "failed to query GPU timestamp frequency");
    }

    BackendStatus Backend::CalibrateTimestamps(const QueueType queue, TimestampCalibration& calibration) const noexcept
    {
        calibration = {};
        ID3D12CommandQueue* const native = queue == QueueType::Graphics ? m_impl->graphicsQueue
            : queue == QueueType::Compute ? m_impl->computeQueue : m_impl->copyQueue;
        LARGE_INTEGER cpuFrequency{};
        if (native == nullptr || QueryPerformanceFrequency(&cpuFrequency) == FALSE)
            return BackendStatus::Failure(FailureCode::Unsupported, 0, "timestamp calibration is unavailable");
        HRESULT result = native->GetTimestampFrequency(&calibration.gpuFrequency);
        if (SUCCEEDED(result)) result = native->GetClockCalibration(&calibration.gpuTimestamp, &calibration.cpuTimestamp);
        calibration.cpuFrequency = static_cast<u64>(cpuFrequency.QuadPart);
        return SUCCEEDED(result) && calibration.IsValid() ? BackendStatus::Success()
            : BackendStatus::Failure(FailureCode::BackendFailure, result, "failed to calibrate CPU and GPU timestamps");
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
    BackendStatus Backend::AddToResidencyWorkingSet(const CommandListRef commandList,
                                                    const ResourceRef resource) noexcept
    {
        return m_impl != nullptr && m_impl->common.RetainCommandResource(commandList, resource)
                   ? BackendStatus::Success()
                   : BackendStatus::Failure(FailureCode::InvalidReference, 0,
                                            "could not retain a command-list residency resource");
    }
    BackendStatus Backend::SetPipeline(const CommandListRef commandList, const PipelineRef pipeline) noexcept
    {
        return m_impl->common.SetPipeline(commandList, pipeline);
    }
    BackendStatus Backend::SetupRenderTargets(const CommandListRef commandList, const RenderTargetSetup& setup) noexcept
    {
        return m_impl->common.SetupRenderTargets(commandList, setup);
    }
    BackendStatus Backend::SetVariableRateShading(CommandListRef, const VariableRateShadingState&) noexcept
    {
        return BackendStatus::Failure(FailureCode::Unsupported, 0,
                                      "variable-rate shading backend support is not implemented");
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
    BackendStatus Backend::ClearColorTarget(const CommandListRef commandList, const TextureRef target,
                                            const ColorValue& value, const SubresourceRange& range,
                                            const Rect* const rectangle) noexcept
    {
        return m_impl->common.ClearColorTarget(commandList, target, value, range, rectangle);
    }
    BackendStatus Backend::ClearDepthStencilTarget(const CommandListRef commandList, const TextureRef target,
                                                   const bool clearDepth, const f32 depth, const bool clearStencil,
                                                   const u8 stencil, const SubresourceRange& range,
                                                   const Rect* const rectangle) noexcept
    {
        return m_impl->common.ClearDepthStencilTarget(commandList, target, clearDepth, depth, clearStencil,
                                                      stencil, range, rectangle);
    }
    BackendStatus Backend::ClearTextureUav(const CommandListRef commandList, const TextureRef texture,
                                           const ColorValue& value, const SubresourceRange& range) noexcept
    {
        return m_impl->common.ClearTextureUav(commandList, texture, value, range);
    }
    BackendStatus Backend::ClearTextureUav(const CommandListRef commandList, const TextureRef texture,
                                           const u32 value, const SubresourceRange& range) noexcept
    {
        return m_impl->common.ClearTextureUav(commandList, texture, value, range);
    }
    BackendStatus Backend::ClearBufferUav(const CommandListRef commandList, const BufferRef buffer, const u32 value) noexcept
    {
        return m_impl->common.ClearBufferUav(commandList, buffer, value);
    }
    BackendStatus Backend::DiscardTexture(const CommandListRef commandList, const TextureRef texture,
                                          const SubresourceRange& range) noexcept
    {
        return m_impl->common.DiscardTexture(commandList, texture, range);
    }
    BackendStatus Backend::DiscardBuffer(const CommandListRef commandList, const BufferRef buffer) noexcept
    {
        return m_impl->common.DiscardBuffer(commandList, buffer);
    }
    BackendStatus Backend::SetStencilRefValue(const CommandListRef commandList, const u8 value) noexcept
    {
        return m_impl->common.SetStencilRefValue(commandList, value);
    }
    BackendStatus Backend::SetBlendFactor(const CommandListRef commandList, const ColorValue& value) noexcept
    {
        return m_impl->common.SetBlendFactor(commandList, value);
    }
    BackendStatus Backend::BeginGpuEvent(const CommandListRef commandList, const char* const name) noexcept
    {
        return m_impl->common.BeginGpuEvent(commandList, name);
    }
    BackendStatus Backend::EndGpuEvent(const CommandListRef commandList) noexcept
    {
        return m_impl->common.EndGpuEvent(commandList);
    }
    BackendStatus Backend::SetGpuMarker(const CommandListRef commandList, const char* const name) noexcept
    {
        return m_impl->common.SetGpuMarker(commandList, name);
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
    BackendStatus Backend::BuildBottomLevelAccelerationStructure(
        CommandListRef, AccelerationStructureRef, containers::ArraySpan<const RayTracingGeometryDesc>,
        AccelerationStructureBuildMode) noexcept
    {
        return BackendStatus::Failure(FailureCode::Unsupported, 0,
                                      "bottom-level acceleration-structure builds are not implemented");
    }
    BackendStatus Backend::BuildTopLevelAccelerationStructure(
        CommandListRef, AccelerationStructureRef, containers::ArraySpan<const RayTracingInstanceDesc>,
        AccelerationStructureBuildMode) noexcept
    {
        return BackendStatus::Failure(FailureCode::Unsupported, 0,
                                      "top-level acceleration-structure builds are not implemented");
    }
    BackendStatus Backend::BuildTopLevelAccelerationStructureIndirect(
        CommandListRef, AccelerationStructureRef, BufferRef, u64, u32, AccelerationStructureBuildMode) noexcept
    {
        return BackendStatus::Failure(FailureCode::Unsupported, 0,
                                      "GPU-driven top-level acceleration-structure builds are not implemented");
    }
    BackendStatus Backend::CopyAccelerationStructure(
        CommandListRef, AccelerationStructureRef, AccelerationStructureRef, AccelerationStructureCopyMode) noexcept
    {
        return BackendStatus::Failure(FailureCode::Unsupported, 0,
                                      "acceleration-structure copy and compaction are not implemented");
    }
    BackendStatus Backend::WriteAccelerationStructureCompactedSize(
        CommandListRef, AccelerationStructureRef, QueryPoolRef, u32) noexcept
    {
        return BackendStatus::Failure(FailureCode::Unsupported, 0,
                                      "acceleration-structure compaction queries are not implemented");
    }
    BackendStatus Backend::DispatchRays(CommandListRef, ShaderTableRef, const DispatchRaysArguments&) noexcept
    {
        return BackendStatus::Failure(FailureCode::Unsupported, 0, "ray dispatch is not implemented");
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
    BackendStatus Backend::CopyTexture(const CommandListRef commandList, const TextureRef destination,
                                       const TextureRef source, const TextureCopyRegion& region) noexcept
    {
        return m_impl->common.CopyTexture(commandList, destination, source, region);
    }
    BackendStatus Backend::ResolveTexture(const CommandListRef commandList, const TextureRef destination,
                                          const TextureRef source, const TextureResolveRegion& region) noexcept
    {
        return m_impl->common.ResolveTexture(commandList, destination, source, region);
    }
    BackendStatus Backend::RequestTextureReadback(const CommandListRef commandList, const TextureRef source,
                                                  const TextureReadbackRegion& region,
                                                  TextureReadbackRef& readback) noexcept
    {
        return m_impl->common.RequestTextureReadback(commandList, source, region, readback);
    }
    BackendStatus Backend::GetTextureReadbackInfo(const TextureReadbackRef readback, TextureReadbackInfo& info) noexcept
    {
        return m_impl->common.GetTextureReadbackInfo(readback, info);
    }
    BackendStatus Backend::MapTextureReadback(const TextureReadbackRef readback, TextureReadbackMapping& mapping) noexcept
    {
        return m_impl->common.MapTextureReadback(readback, mapping);
    }
    BackendStatus Backend::UnmapTextureReadback(const TextureReadbackRef readback) noexcept
    {
        return m_impl->common.UnmapTextureReadback(readback);
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
    SwapChainRef Backend::CreateSwapChainWithBackBuffer(const SwapChainDesc& desc) noexcept
    {
        if (m_impl == nullptr || !m_impl->initialized || !IsSwapChainDescriptorSupported(desc) ||
            !IsWindow(static_cast<HWND>(desc.surface.nativeWindow)))
            return {};

        BOOL tearingSupported = FALSE;
        const bool tearingAvailable = SUCCEEDED(m_impl->factory->CheckFeatureSupport(
            DXGI_FEATURE_PRESENT_ALLOW_TEARING, &tearingSupported, sizeof(tearingSupported))) && tearingSupported == TRUE;
        const bool tearingEnabled = desc.allowTearing && tearingAvailable;

        DXGI_SWAP_CHAIN_DESC1 nativeDesc{};
        nativeDesc.Width = desc.width;
        nativeDesc.Height = desc.height;
        nativeDesc.Format = ToSwapChainFormat(desc.format);
        nativeDesc.Stereo = FALSE;
        nativeDesc.SampleDesc = {1, 0};
        nativeDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        nativeDesc.BufferCount = desc.bufferCount;
        nativeDesc.Scaling = DXGI_SCALING_STRETCH;
        nativeDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        nativeDesc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
        nativeDesc.Flags = (tearingEnabled ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0) |
                           (desc.frameLatency.enabled ? DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT : 0);

        IDXGISwapChain1* baseSwapChain = nullptr;
        HRESULT result = m_impl->factory->CreateSwapChainForHwnd(
            m_impl->graphicsQueue, static_cast<HWND>(desc.surface.nativeWindow), &nativeDesc, nullptr, nullptr,
            &baseSwapChain);
        if (FAILED(result) || baseSwapChain == nullptr) return {};

        IDXGISwapChain4* nativeSwapChain = nullptr;
        result = baseSwapChain->QueryInterface(IID_PPV_ARGS(&nativeSwapChain));
        baseSwapChain->Release();
        if (FAILED(result) || nativeSwapChain == nullptr) return {};

        if (!ApplyColorConfiguration(*nativeSwapChain, desc))
        {
            nativeSwapChain->Release();
            return {};
        }
        HANDLE frameLatencyWaitableObject = nullptr;
        if (desc.frameLatency.enabled)
        {
            result = nativeSwapChain->SetMaximumFrameLatency(desc.frameLatency.maximumFramesInFlight);
            if (SUCCEEDED(result)) frameLatencyWaitableObject = nativeSwapChain->GetFrameLatencyWaitableObject();
            if (FAILED(result) || frameLatencyWaitableObject == nullptr)
            {
                nativeSwapChain->Release();
                return {};
            }
        }
        static_cast<void>(m_impl->factory->MakeWindowAssociation(
            static_cast<HWND>(desc.surface.nativeWindow), DXGI_MWA_NO_ALT_ENTER));

        memory::MemoryBlock block = memory::Allocate(memory::PoolId::Rendering, sizeof(SwapChainPayload), alignof(SwapChainPayload));
        if (!block)
        {
            nativeSwapChain->Release();
            return {};
        }
        auto* const payload = new (block.address) SwapChainPayload();
        payload->common = &m_impl->common;
        payload->presentationQueue = m_impl->graphicsQueue;
        payload->native = nativeSwapChain;
        payload->frameLatencyWaitableObject = frameLatencyWaitableObject;
        payload->desc = desc;
        payload->bufferCount = desc.bufferCount;
        payload->tearingEnabled = tearingEnabled;
        payload->stats.state = SwapChainState::Available;
        payload->stats.presentParameters = {
            desc.presentMode,
            static_cast<u8>(desc.presentMode == PresentMode::Fifo ? 1u : 0u),
            desc.presentMode == PresentMode::Immediate && tearingEnabled};
        payload->stats.bufferCount = desc.bufferCount;
        payload->stats.width = desc.width;
        payload->stats.height = desc.height;
        payload->stats.colorSpace = desc.colorSpace;
        payload->stats.frameLatency = desc.frameLatency;
        payload->stats.displayColor = QueryDisplayColorCapabilities(*nativeSwapChain);
        payload->stats.tearingSupported = tearingAvailable;
        payload->stats.tearingEnabled = tearingEnabled;
        result = m_impl->nativeDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&payload->presentFence));
        if (SUCCEEDED(result)) payload->presentFenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (FAILED(result) || payload->presentFenceEvent == nullptr || !CreateBackBufferHandles(*payload))
        {
            DestroySwapChainPayload(nullptr, {}, payload);
            m_impl->common.DrainRetiredResourcesAfterGpuIdle();
            return {};
        }

        const ResourceRef resource = m_impl->common.CreateBackendResource(
            ResourceKind::SwapChain, payload, &DestroySwapChainPayload);
        if (!resource)
        {
            DestroySwapChainPayload(nullptr, {}, payload);
            m_impl->common.DrainRetiredResourcesAfterGpuIdle();
            return {};
        }
        return CastResourceRef<SwapChainRef>(resource);
    }
    BackendStatus Backend::ResizeBackbuffer(const SwapChainRef swapChain, const u32 width, const u32 height) noexcept
    {
        if (m_impl == nullptr || !m_impl->initialized)
            return BackendStatus::Failure(FailureCode::NotInitialized, 0, "D3D12 backend is not initialized");
        auto* const payload = static_cast<SwapChainPayload*>(m_impl->common.GetResourcePayload(ResourceRef(swapChain)));
        if (payload == nullptr || width == 0 || height == 0)
            return BackendStatus::Failure(FailureCode::InvalidReference, 0, "invalid D3D12 swap-chain resize request");
        concurrency::ScopedLock guard(payload->lock);
        if (payload->stats.state == SwapChainState::Acquired)
        {
            ++payload->stats.rejectedOperations;
            return BackendStatus::Failure(FailureCode::Busy, 0,
                                          "swap-chain resize requires the active back-buffer acquisition to be presented or abandoned");
        }
        if (payload->stats.state == SwapChainState::Failed)
        {
            ++payload->stats.rejectedOperations;
            return BackendStatus::Failure(FailureCode::DeviceLost, 0, "failed swap chain cannot be resized");
        }
        if (payload->desc.width == width && payload->desc.height == height)
        {
            const u32 current = payload->native->GetCurrentBackBufferIndex();
            if (current < payload->bufferCount && payload->backBuffers[current].IsValid())
                return BackendStatus::Success();
            return CreateBackBufferHandles(*payload)
                       ? BackendStatus::Success()
                       : BackendStatus::Failure(FailureCode::BackendFailure, 0,
                                                "failed to recover DXGI back-buffer wrappers");
        }
        for (u32 index = 0; index < payload->bufferCount; ++index)
            if (m_impl->common.GetResourceReferenceCount(ResourceRef(payload->backBuffers[index])) != 1)
            {
                ++payload->stats.rejectedOperations;
                return BackendStatus::Failure(FailureCode::Busy, 0,
                                              "swap-chain back buffers are still retained by recorded or external work");
            }
        payload->stats.state = SwapChainState::Reconfiguring;
        if (!m_impl->common.WaitIdle())
        {
            payload->stats.state = SwapChainState::Failed;
            return BackendStatus::Failure(FailureCode::DeviceLost, 0, "GPU idle wait failed before swap-chain resize");
        }
        if (!WaitForAllPresents(*payload))
        {
            payload->stats.state = SwapChainState::Failed;
            return BackendStatus::Failure(FailureCode::DeviceLost, 0,
                                          "presentation fence wait failed before swap-chain resize");
        }

        ReleaseBackBufferHandles(*payload);
        m_impl->common.DrainRetiredResourcesAfterGpuIdle();
        const HRESULT result = payload->native->ResizeBuffers(
            payload->bufferCount, width, height, ToSwapChainFormat(payload->desc.format),
            (payload->tearingEnabled ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0) |
            (payload->desc.frameLatency.enabled ? DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT : 0));
        if (FAILED(result))
        {
            const bool recovered = CreateBackBufferHandles(*payload);
            const FailureCode code = result == DXGI_ERROR_DEVICE_REMOVED || result == DXGI_ERROR_DEVICE_RESET
                                         ? FailureCode::DeviceLost
                                         : FailureCode::BackendFailure;
            payload->stats.state = recovered && code != FailureCode::DeviceLost
                                       ? SwapChainState::Available
                                       : SwapChainState::Failed;
            return BackendStatus::Failure(code, result, "DXGI ResizeBuffers failed");
        }
        payload->desc.width = width;
        payload->desc.height = height;
        for (u32 index = 0; index < payload->bufferCount; ++index) payload->presentFenceValues[index] = 0;
        if (!CreateBackBufferHandles(*payload))
        {
            payload->stats.state = SwapChainState::Failed;
            return BackendStatus::Failure(FailureCode::BackendFailure, 0,
                                          "failed to wrap resized DXGI back buffers with NVRHI");
        }
        payload->stats.width = width;
        payload->stats.height = height;
        payload->stats.currentBufferIndex = payload->native->GetCurrentBackBufferIndex();
        payload->stats.state = SwapChainState::Available;
        ++payload->stats.resizeCount;
        return BackendStatus::Success();
    }

    BackendStatus Backend::SetSwapChainPresentParameters(const SwapChainRef swapChain,
                                                         const PresentParameters& parameters) noexcept
    {
        if (m_impl == nullptr || !m_impl->initialized)
            return BackendStatus::Failure(FailureCode::NotInitialized, 0, "D3D12 backend is not initialized");
        auto* const payload = static_cast<SwapChainPayload*>(m_impl->common.GetResourcePayload(ResourceRef(swapChain)));
        if (payload == nullptr)
            return BackendStatus::Failure(FailureCode::InvalidReference, 0, "invalid D3D12 swap chain");
        concurrency::ScopedLock guard(payload->lock);
        if (!IsPresentParametersValid(parameters))
        {
            ++payload->stats.rejectedOperations;
            return BackendStatus::Failure(parameters.mode == PresentMode::Mailbox ? FailureCode::Unsupported
                                                                                  : FailureCode::InvalidArgument,
                                          0, "invalid swap-chain presentation parameters");
        }
        if (payload->stats.state != SwapChainState::Available)
        {
            ++payload->stats.rejectedOperations;
            return BackendStatus::Failure(payload->stats.state == SwapChainState::Acquired ? FailureCode::Busy
                                                                                           : FailureCode::BackendFailure,
                                          0, "swap-chain presentation parameters require an available swap chain");
        }
        if (parameters.allowTearing && !payload->tearingEnabled)
        {
            ++payload->stats.rejectedOperations;
            return BackendStatus::Failure(FailureCode::Unsupported, 0,
                                          "tearing was not enabled for this swap chain");
        }
        payload->stats.presentParameters = parameters;
        return BackendStatus::Success();
    }

    BackendStatus Backend::AcquireBackBuffer(const SwapChainRef swapChain,
                                             AcquiredBackBuffer& acquisition) noexcept
    {
        acquisition = {};
        if (m_impl == nullptr || !m_impl->initialized)
            return BackendStatus::Failure(FailureCode::NotInitialized, 0, "D3D12 backend is not initialized");
        auto* const payload = static_cast<SwapChainPayload*>(m_impl->common.GetResourcePayload(ResourceRef(swapChain)));
        if (payload == nullptr)
            return BackendStatus::Failure(FailureCode::InvalidReference, 0, "invalid D3D12 swap chain");
        concurrency::ScopedLock guard(payload->lock);
        if (payload->stats.state != SwapChainState::Available)
        {
            ++payload->stats.rejectedOperations;
            return BackendStatus::Failure(payload->stats.state == SwapChainState::Acquired ? FailureCode::Busy
                                                                                           : FailureCode::BackendFailure,
                                          0, "swap chain is not available for back-buffer acquisition");
        }
        if (payload->desc.frameLatency.enabled)
        {
            const u64 waitStart = system::MonotonicTicks();
            const DWORD waitResult = WaitForSingleObjectEx(payload->frameLatencyWaitableObject,
                                                           payload->desc.frameLatency.waitTimeoutMilliseconds, FALSE);
            const u64 elapsed = TicksToNanoseconds(system::MonotonicTicks() - waitStart);
            ++payload->stats.frameLatencyWaits;
            payload->stats.frameLatencyWaitNanoseconds += elapsed;
            if (payload->stats.longestFrameLatencyWaitNanoseconds < elapsed)
                payload->stats.longestFrameLatencyWaitNanoseconds = elapsed;
            if (waitResult == WAIT_TIMEOUT)
            {
                ++payload->stats.frameLatencyTimeouts;
                ++payload->stats.rejectedOperations;
                return BackendStatus::Failure(FailureCode::Timeout, WAIT_TIMEOUT,
                                              "DXGI frame-latency pacing timed out");
            }
            if (waitResult != WAIT_OBJECT_0)
            {
                payload->stats.state = SwapChainState::Failed;
                return BackendStatus::Failure(FailureCode::BackendFailure, GetLastError(),
                                              "DXGI frame-latency wait failed");
            }
        }
        const u32 index = payload->native->GetCurrentBackBufferIndex();
        if (index >= payload->bufferCount || !payload->backBuffers[index].IsValid())
        {
            payload->stats.state = SwapChainState::Failed;
            return BackendStatus::Failure(FailureCode::BackendFailure, 0, "DXGI current back buffer is invalid");
        }
        bool waited = false;
        const u64 waitStart = system::MonotonicTicks();
        if (!WaitForPresentFence(*payload, payload->presentFenceValues[index], &waited))
        {
            payload->stats.state = SwapChainState::Failed;
            return BackendStatus::Failure(FailureCode::DeviceLost, 0, "failed waiting for the current back buffer");
        }
        if (waited)
        {
            const u64 elapsed = TicksToNanoseconds(system::MonotonicTicks() - waitStart);
            ++payload->stats.acquireWaits;
            payload->stats.acquireWaitNanoseconds += elapsed;
            if (payload->stats.longestAcquireWaitNanoseconds < elapsed)
                payload->stats.longestAcquireWaitNanoseconds = elapsed;
        }
        u64 serial = payload->nextAcquisitionSerial++;
        if (serial == 0)
        {
            serial = payload->nextAcquisitionSerial++;
            if (serial == 0)
            {
                payload->stats.state = SwapChainState::Failed;
                return BackendStatus::Failure(FailureCode::CapacityExceeded, 0,
                                              "swap-chain acquisition serial space was exhausted");
            }
        }
        acquisition = {swapChain, payload->backBuffers[index], serial, index, payload->desc.width, payload->desc.height};
        payload->activeAcquisition = acquisition;
        payload->submittedAcquisitionSerial.SetValue(0);
        payload->presentTransitionRecorded = false;
        payload->stats.state = SwapChainState::Acquired;
        payload->stats.currentBufferIndex = index;
        payload->stats.lastAcquisitionSerial = serial;
        ++payload->stats.acquisitions;
        return BackendStatus::Success();
    }

    BackendStatus Backend::AbandonBackBuffer(const AcquiredBackBuffer& acquisition) noexcept
    {
        if (m_impl == nullptr || !m_impl->initialized)
            return BackendStatus::Failure(FailureCode::NotInitialized, 0, "D3D12 backend is not initialized");
        auto* const payload = static_cast<SwapChainPayload*>(
            m_impl->common.GetResourcePayload(ResourceRef(acquisition.swapChain)));
        if (payload == nullptr)
            return BackendStatus::Failure(FailureCode::InvalidReference, 0, "invalid D3D12 swap chain acquisition");
        concurrency::ScopedLock guard(payload->lock);
        if (payload->stats.state != SwapChainState::Acquired || !MatchesAcquisition(*payload, acquisition))
        {
            ++payload->stats.rejectedOperations;
            return BackendStatus::Failure(FailureCode::InvalidReference, 0,
                                          "stale or foreign back-buffer acquisition");
        }
        payload->activeAcquisition = {};
        payload->submittedAcquisitionSerial.SetValue(0);
        payload->presentTransitionRecorded = false;
        payload->stats.state = SwapChainState::Available;
        ++payload->stats.abandonedAcquisitions;
        return BackendStatus::Success();
    }

    BackendStatus Backend::TransitionSwapChainPresent(const CommandListRef commandList,
                                                      const AcquiredBackBuffer& acquisition) noexcept
    {
        if (m_impl == nullptr || !m_impl->initialized)
            return BackendStatus::Failure(FailureCode::NotInitialized, 0, "D3D12 backend is not initialized");
        auto* const payload = static_cast<SwapChainPayload*>(
            m_impl->common.GetResourcePayload(ResourceRef(acquisition.swapChain)));
        if (payload == nullptr)
            return BackendStatus::Failure(FailureCode::InvalidReference, 0, "invalid D3D12 swap chain");
        concurrency::ScopedLock guard(payload->lock);
        if (payload->stats.state != SwapChainState::Acquired || !MatchesAcquisition(*payload, acquisition))
        {
            ++payload->stats.rejectedOperations;
            return BackendStatus::Failure(FailureCode::InvalidReference, 0,
                                          "stale or foreign back-buffer acquisition");
        }
        if (payload->presentTransitionRecorded)
        {
            ++payload->stats.rejectedOperations;
            return BackendStatus::Failure(FailureCode::InvalidArgument, 0,
                                          "back-buffer present transition was already recorded");
        }
        if (m_impl->common.GetCommandListType(commandList) != CommandListType::Default)
        {
            ++payload->stats.rejectedOperations;
            return BackendStatus::Failure(FailureCode::InvalidCommandList, 0,
                                          "back-buffer presentation requires a graphics command list");
        }
        const BackendStatus transition = m_impl->common.TransitionTexture(
            commandList, acquisition.texture, ResourceState::Unknown, ResourceState::Present, {});
        if (!transition) return transition;
        if (!m_impl->common.RetainCommandResource(commandList, ResourceRef(acquisition.swapChain)))
            return BackendStatus::Failure(FailureCode::OutOfMemory, 0,
                                          "failed to retain the swap chain for command submission");
        if (!m_impl->common.RegisterCommandSubmissionCallback(commandList, &MarkPresentTransitionSubmitted,
                                                              payload, acquisition.serial))
            return BackendStatus::Failure(FailureCode::OutOfMemory, 0,
                                          "failed to register the swap-chain submission acknowledgement");
        payload->presentTransitionRecorded = true;
        return BackendStatus::Success();
    }

    BackendStatus Backend::Present(const AcquiredBackBuffer& acquisition) noexcept
    {
        if (m_impl == nullptr || !m_impl->initialized)
            return BackendStatus::Failure(FailureCode::NotInitialized, 0, "D3D12 backend is not initialized");
        auto* const payload = static_cast<SwapChainPayload*>(
            m_impl->common.GetResourcePayload(ResourceRef(acquisition.swapChain)));
        if (payload == nullptr)
            return BackendStatus::Failure(FailureCode::InvalidReference, 0, "invalid D3D12 swap chain");
        concurrency::ScopedLock guard(payload->lock);
        if (payload->stats.state != SwapChainState::Acquired || !MatchesAcquisition(*payload, acquisition))
        {
            ++payload->stats.rejectedOperations;
            return BackendStatus::Failure(FailureCode::InvalidReference, 0,
                                          "stale or foreign back-buffer acquisition");
        }
        if (!payload->presentTransitionRecorded)
        {
            ++payload->stats.rejectedOperations;
            return BackendStatus::Failure(FailureCode::InvalidArgument, 0,
                                          "presentation requires an explicit back-buffer present transition");
        }
        if (payload->submittedAcquisitionSerial.GetValue() != acquisition.serial)
        {
            ++payload->stats.rejectedOperations;
            return BackendStatus::Failure(FailureCode::Busy, 0,
                                          "the back-buffer present transition has not been submitted");
        }
        const u32 currentIndex = payload->native->GetCurrentBackBufferIndex();
        if (currentIndex != acquisition.bufferIndex)
        {
            ++payload->stats.presentationFailures;
            payload->stats.state = SwapChainState::Failed;
            return BackendStatus::Failure(FailureCode::BackendFailure, 0,
                                          "DXGI current back buffer changed before presentation");
        }
        PresentOperationContext context{payload, m_impl->nativeDevice};
        return m_impl->common.ExecuteSerializedQueueOperation(&ExecutePresent, &context);
    }

    SwapChainStats Backend::GetSwapChainStats(const SwapChainRef swapChain) noexcept
    {
        if (m_impl == nullptr || !m_impl->initialized) return {};
        auto* const payload = static_cast<SwapChainPayload*>(m_impl->common.GetResourcePayload(ResourceRef(swapChain)));
        if (payload == nullptr) return {};
        concurrency::ScopedLock guard(payload->lock);
        return payload->stats;
    }
    void Backend::SetResourceDebugName(const TextureRef resource, const char* const name) noexcept
    {
        m_impl->common.SetResourceDebugName(resource, name);
        SetNativeName(m_impl->common.GetNativeObject(ResourceRef(resource), nvrhi::ObjectTypes::D3D12_Resource), name);
    }
    void Backend::SetResourceDebugName(const TextureReadbackRef resource, const char* const name) noexcept
    {
        m_impl->common.SetResourceDebugName(resource, name);
    }
    void Backend::SetResourceDebugName(const BufferRef resource, const char* const name) noexcept
    {
        m_impl->common.SetResourceDebugName(resource, name);
        SetNativeName(m_impl->common.GetNativeObject(ResourceRef(resource), nvrhi::ObjectTypes::D3D12_Resource), name);
    }
    void Backend::SetResourceDebugName(const HeapRef resource, const char* const name) noexcept
    {
        m_impl->common.SetResourceDebugName(resource, name);
        SetNativeName(m_impl->common.GetNativeObject(ResourceRef(resource), nvrhi::ObjectTypes::D3D12_Resource), name);
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
        SetNativeName(m_impl->common.GetNativeObject(ResourceRef(resource), nvrhi::ObjectTypes::D3D12_PipelineState), name);
    }
    void Backend::SetResourceDebugName(AccelerationStructureRef, const char*) noexcept {}
    void Backend::SetResourceDebugName(ShaderTableRef, const char*) noexcept {}
    void Backend::SetResourceDebugName(const QueryPoolRef resource, const char* const name) noexcept
    {
        concurrency::ScopedLock guard(m_impl->queryPoolLock);
        QueryPoolPayload* const payload = m_impl->FindQueryPool(resource);
        if (payload == nullptr) return;
        SetNativeName(payload->heap, name);
        SetNativeName(payload->readback, name);
    }
    void Backend::SetResourceDebugName(const CommandListRef resource, const char* const name) noexcept
    {
        m_impl->common.SetResourceDebugName(resource, name);
        SetNativeName(m_impl->common.GetNativeObject(ResourceRef(resource), nvrhi::ObjectTypes::D3D12_GraphicsCommandList), name);
    }
    void Backend::SetResourceDebugName(const SwapChainRef swapChain, const char* const name) noexcept
    {
        if (m_impl == nullptr || !m_impl->initialized || name == nullptr) return;
        auto* const payload = static_cast<SwapChainPayload*>(m_impl->common.GetResourcePayload(ResourceRef(swapChain)));
        if (payload == nullptr) return;
        concurrency::ScopedLock guard(payload->lock);
        CopyDebugName(payload->debugName, static_cast<u32>(sizeof(payload->debugName)), name);
        NameBackBuffers(*payload);
    }
} // namespace vanguard::rhi::d3d12
