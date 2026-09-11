#include <vanguard/rhi/rhi.hpp>
#include <vanguard/system/assert.hpp>
#include <vanguard/concurrency/atomic.hpp>

namespace vanguard::rhi
{
    void DescriptorRetirement::Include(const GpuFence fence) noexcept
    {
        if (!fence.IsValid())
            return;
        if (fence.queue == QueueType::Graphics && graphicsFence < fence.value)
            graphicsFence = fence.value;
        else if (fence.queue == QueueType::Compute && computeFence < fence.value)
            computeFence = fence.value;
        else if (fence.queue == QueueType::Copy && copyFence < fence.value)
            copyFence = fence.value;
    }

    void ResidencyFenceSet::Include(const GpuFence fence) noexcept
    {
        if (!fence.IsValid())
            return;
        neverSubmittedQueues &= static_cast<u8>(~(1u << static_cast<u32>(fence.queue)));
        if (fence.queue == QueueType::Graphics && graphics < fence.value)
            graphics = fence.value;
        else if (fence.queue == QueueType::Compute && compute < fence.value)
            compute = fence.value;
        else if (fence.queue == QueueType::Copy && copy < fence.value)
            copy = fence.value;
    }

    namespace
    {
        IBackend* g_backend = nullptr;
        Capabilities g_capabilities{};
        thread_local CommandListRef g_boundCommandList{};
        concurrency::Atomic<u64> g_submittedGraphics{0};
        concurrency::Atomic<u64> g_submittedCompute{0};
        concurrency::Atomic<u64> g_submittedCopy{0};
        concurrency::Atomic<u32> g_submissionCoverageLost{0};

        void IncludeSubmittedFence(concurrency::Atomic<u64>& target, const u64 value) noexcept
        {
            u64 previous = target.GetValue();
            while (previous < value)
            {
                const u64 observed = target.CompareExchange(value, previous);
                if (observed == previous)
                    break;
                previous = observed;
            }
        }

        void ClearFailure(Failure* const failure) noexcept
        {
            if (failure != nullptr)
                *failure = {};
        }

        void CopyMessage(char* const destination, const u32 capacity, const char* const source) noexcept
        {
            if (destination == nullptr || capacity == 0)
                return;
            u32 index = 0;
            if (source != nullptr)
            {
                while (index + 1 < capacity && source[index] != '\0')
                {
                    destination[index] = source[index];
                    ++index;
                }
            }
            destination[index] = '\0';
        }

        bool Reject(Failure* const failure, const FailureCode code, const char* const message) noexcept
        {
            if (failure != nullptr)
            {
                failure->code = code;
                failure->backendCode = 0;
                CopyMessage(failure->message, static_cast<u32>(sizeof(failure->message)), message);
            }
            return false;
        }

        bool Accept(const BackendStatus status, Failure* const failure) noexcept
        {
            if (status)
            {
                ClearFailure(failure);
                return true;
            }
            if (failure != nullptr)
            {
                failure->code = status.code == FailureCode::None ? FailureCode::BackendFailure : status.code;
                failure->backendCode = status.backendCode;
                CopyMessage(failure->message, static_cast<u32>(sizeof(failure->message)), status.message);
            }
            return false;
        }

        bool RequireBackend(Failure* const failure) noexcept
        {
            return g_backend != nullptr || Reject(failure, FailureCode::NotInitialized, "RHI is not initialized");
        }

        bool RequireBoundCommandList(Failure* const failure) noexcept
        {
            if (!RequireBackend(failure))
                return false;
            return g_boundCommandList.IsValid() || Reject(failure, FailureCode::NoBoundCommandList, "RHI command recording requires a bound command list");
        }

        bool ValidateCommandListSubmission(const char* const scopeName, const containers::ArraySpan<const CommandListRef> commandLists, const CommandListSyncType sync,
                                           Failure* const failure) noexcept
        {
            if (scopeName == nullptr || scopeName[0] == '\0' || commandLists.Data() == nullptr || commandLists.Size() == 0 || sync > CommandListSyncType::JoinAsyncCompute ||
                commandLists.Size() > MaximumCommandListsPerSubmission)
                return Reject(failure, FailureCode::InvalidArgument, "invalid command-list submission span");
            for (u32 index = 0; index < commandLists.Size(); ++index)
            {
                if (!commandLists[index].IsValid() || commandLists[index] == g_boundCommandList)
                    return Reject(failure, FailureCode::InvalidCommandList, "submission contains an invalid or currently bound command list");
                for (u32 previous = 0; previous < index; ++previous)
                    if (commandLists[previous] == commandLists[index])
                        return Reject(failure, FailureCode::InvalidCommandList, "submission contains a duplicate command list");
            }
            return true;
        }

        bool IsPowerOfTwo(const u64 value) noexcept
        {
            return value != 0 && (value & (value - 1)) == 0;
        }

        template <typename Enum> [[nodiscard]] constexpr bool HasFlag(const Enum value, const Enum flag) noexcept
        {
            return (static_cast<u32>(value) & static_cast<u32>(flag)) != 0;
        }

        template <typename Enum> [[nodiscard]] constexpr bool HasOnlyFlags(const Enum value, const Enum knownFlags) noexcept
        {
            return (static_cast<u32>(value) & ~static_cast<u32>(knownFlags)) == 0;
        }

        inline constexpr TextureUsage AllTextureUsage = TextureUsage::ShaderResource | TextureUsage::UnorderedAccess | TextureUsage::RenderTarget | TextureUsage::DepthStencil | TextureUsage::CopySource |
                                                        TextureUsage::CopyDestination | TextureUsage::ResolveSource | TextureUsage::ResolveDestination | TextureUsage::Present | TextureUsage::ShadingRate |
                                                        TextureUsage::RayTracing;
        inline constexpr BufferUsage AllBufferUsage = BufferUsage::Vertex | BufferUsage::Index | BufferUsage::Constant | BufferUsage::Structured | BufferUsage::Raw | BufferUsage::IndirectArguments |
                                                      BufferUsage::ShaderResource | BufferUsage::UnorderedAccess | BufferUsage::CopySource | BufferUsage::CopyDestination | BufferUsage::AccelerationStructure |
                                                      BufferUsage::ShaderBindingTable;
        inline constexpr ResourceState AllResourceStates = ResourceState::Common | ResourceState::CopySource | ResourceState::CopyDestination | ResourceState::ShaderResourceGraphics |
                                                           ResourceState::ShaderResourceCompute | ResourceState::UnorderedAccess | ResourceState::RenderTarget | ResourceState::DepthWrite |
                                                           ResourceState::DepthRead | ResourceState::VertexBuffer | ResourceState::IndexBuffer | ResourceState::ConstantBuffer | ResourceState::IndirectArgument |
                                                           ResourceState::AccelerationStructureRead | ResourceState::AccelerationStructureWrite | ResourceState::Present | ResourceState::ResolveSource |
                                                           ResourceState::ResolveDestination | ResourceState::ShadingRate;

        [[nodiscard]] bool IsValidBindingType(const BindingType type) noexcept
        {
            return type <= BindingType::PushConstants;
        }

        [[nodiscard]] u8 BindingNamespace(const BindingType type) noexcept
        {
            if (type == BindingType::ConstantBuffer)
                return 0;
            if (type == BindingType::Sampler)
                return 3;
            if (type == BindingType::PushConstants)
                return 4;
            if (type == BindingType::TextureUnorderedAccess || type == BindingType::TypedBufferUnorderedAccess || type == BindingType::StructuredBufferUnorderedAccess ||
                type == BindingType::ByteAddressBufferUnorderedAccess)
                return 2;
            return 1;
        }

        struct FormatBlockInfo
        {
            u32 bytes = 0;
            u32 extent = 1;
        };

        [[nodiscard]] constexpr FormatBlockInfo GetFormatBlockInfo(const Format format) noexcept
        {
            switch (format)
            {
            case Format::R8UNorm:
            case Format::R8SNorm:
            case Format::R8UInt:
                return {1, 1};
            case Format::R8G8UNorm:
            case Format::R8G8SNorm:
            case Format::R8G8UInt:
            case Format::R16UNorm:
            case Format::R16SNorm:
            case Format::R16UInt:
            case Format::R16Float:
            case Format::D16UNorm:
                return {2, 1};
            case Format::R8G8B8A8UNorm:
            case Format::R8G8B8A8UNormSrgb:
            case Format::R8G8B8A8SNorm:
            case Format::R8G8B8A8UInt:
            case Format::B8G8R8A8UNorm:
            case Format::B8G8R8A8UNormSrgb:
            case Format::R16G16UNorm:
            case Format::R16G16SNorm:
            case Format::R16G16UInt:
            case Format::R16G16Float:
            case Format::R32UInt:
            case Format::R32Float:
            case Format::R10G10B10A2UNorm:
            case Format::R11G11B10Float:
            case Format::D24UNormS8UInt:
            case Format::D32Float:
                return {4, 1};
            case Format::R16G16B16A16UNorm:
            case Format::R16G16B16A16SNorm:
            case Format::R16G16B16A16UInt:
            case Format::R16G16B16A16Float:
            case Format::R32G32UInt:
            case Format::R32G32Float:
            case Format::D32FloatS8UInt:
                return {8, 1};
            case Format::R32G32B32Float:
                return {12, 1};
            case Format::R32G32B32A32Float:
                return {16, 1};
            case Format::BC1UNorm:
            case Format::BC1UNormSrgb:
            case Format::BC4UNorm:
            case Format::BC4SNorm:
                return {8, 4};
            case Format::BC2UNorm:
            case Format::BC2UNormSrgb:
            case Format::BC3UNorm:
            case Format::BC3UNormSrgb:
            case Format::BC5UNorm:
            case Format::BC5SNorm:
            case Format::BC6HUFloat:
            case Format::BC6HSFloat:
            case Format::BC7UNorm:
            case Format::BC7UNormSrgb:
                return {16, 4};
            default:
                return {};
            }
        }

        [[nodiscard]] constexpr bool IsKnownFormat(const Format format) noexcept
        {
            return GetFormatBlockInfo(format).bytes != 0;
        }

        [[nodiscard]] constexpr bool IsKnownResourceState(const ResourceState state, const bool allowUnknown = false) noexcept
        {
            return (allowUnknown || state != ResourceState::Unknown) && HasOnlyFlags(state, AllResourceStates);
        }

        [[nodiscard]] constexpr u32 MaximumMipCount(const Extent3D extent) noexcept
        {
            u32 largest = extent.width > extent.height ? extent.width : extent.height;
            if (extent.depth > largest)
                largest = extent.depth;
            u32 count = 0;
            while (largest != 0)
            {
                ++count;
                largest >>= 1u;
            }
            return count;
        }

        [[nodiscard]] bool IsTextureDescriptorValid(const TextureDesc& desc) noexcept
        {
            const bool dimensionValid = (desc.dimension == TextureDimension::Texture1D && desc.extent.height == 1 && desc.extent.depth == 1) ||
                                        (desc.dimension == TextureDimension::Texture2D && desc.extent.depth == 1) || (desc.dimension == TextureDimension::Texture3D && desc.arraySize == 1) ||
                                        (desc.dimension == TextureDimension::TextureCube && desc.extent.width == desc.extent.height && desc.extent.depth == 1 && desc.arraySize >= 6 && desc.arraySize % 6 == 0);
            const bool samplesValid = desc.sampleCount == 1 || desc.sampleCount == 2 || desc.sampleCount == 4 || desc.sampleCount == 8;
            const bool limitsValid =
                (desc.dimension != TextureDimension::Texture3D || (desc.extent.width <= g_capabilities.maximumTextureDimension3D && desc.extent.height <= g_capabilities.maximumTextureDimension3D &&
                                                                   desc.extent.depth <= g_capabilities.maximumTextureDimension3D)) &&
                (desc.dimension == TextureDimension::Texture3D || (desc.extent.width <= g_capabilities.maximumTextureDimension2D && desc.extent.height <= g_capabilities.maximumTextureDimension2D)) &&
                (desc.dimension == TextureDimension::Texture3D || desc.arraySize <= g_capabilities.maximumTextureArrayLayers);
            const bool shadingRateValid = !HasFlag(desc.usage, TextureUsage::ShadingRate) || (desc.dimension == TextureDimension::Texture2D && desc.format == Format::R8UInt && desc.extent.depth == 1 &&
                                                                                              desc.mipCount == 1 && desc.arraySize == 1 && desc.sampleCount == 1);
            return desc.extent.width != 0 && desc.extent.height != 0 && desc.extent.depth != 0 && IsKnownFormat(desc.format) && desc.dimension <= TextureDimension::TextureCube &&
                   HasOnlyFlags(desc.usage, AllTextureUsage) && IsKnownResourceState(desc.initialState) && desc.mipCount != 0 && desc.mipCount <= MaximumMipCount(desc.extent) && desc.arraySize != 0 &&
                   dimensionValid && samplesValid && limitsValid && shadingRateValid && (desc.sampleCount == 1 || (desc.dimension == TextureDimension::Texture2D && desc.mipCount == 1));
        }

        [[nodiscard]] bool IsBufferDescriptorValid(const BufferDesc& desc) noexcept
        {
            const bool structured = HasFlag(desc.usage, BufferUsage::Structured);
            return desc.size != 0 && HasOnlyFlags(desc.usage, AllBufferUsage) && IsKnownResourceState(desc.initialState) && desc.memoryType <= MemoryType::Readback &&
                   (desc.format == Format::Unknown || IsKnownFormat(desc.format)) && (!structured || (desc.structureStride != 0 && desc.size % desc.structureStride == 0)) &&
                   (!desc.virtualResource || desc.memoryType == MemoryType::DeviceLocal) &&
                   (desc.keepInitialState || (desc.memoryType == MemoryType::DeviceLocal && desc.initialState == ResourceState::Common));
        }

        [[nodiscard]] bool IsTextureUploadValid(const TextureDesc& desc, const TextureSubresourceData& upload) noexcept
        {
            if (upload.data == nullptr || upload.size == 0 || upload.mipLevel >= desc.mipCount || upload.arraySlice >= desc.arraySize)
                return false;
            const FormatBlockInfo block = GetFormatBlockInfo(desc.format);
            if (block.bytes == 0)
                return false;
            const u32 mipWidth = desc.extent.width >> upload.mipLevel != 0 ? desc.extent.width >> upload.mipLevel : 1u;
            const u32 mipHeight = desc.extent.height >> upload.mipLevel != 0 ? desc.extent.height >> upload.mipLevel : 1u;
            const u32 mipDepth = desc.extent.depth >> upload.mipLevel != 0 ? desc.extent.depth >> upload.mipLevel : 1u;
            const u64 blocksWide = (static_cast<u64>(mipWidth) + block.extent - 1u) / block.extent;
            const u64 blockRows = (static_cast<u64>(mipHeight) + block.extent - 1u) / block.extent;
            const u64 minimumRowBytes = blocksWide * block.bytes;
            if (upload.rowPitch < minimumRowBytes)
                return false;
            if (blockRows > 1u && upload.rowPitch > (static_cast<u64>(-1) - minimumRowBytes) / (blockRows - 1u))
                return false;
            const u64 sliceBytes = blockRows == 0 ? 0 : upload.rowPitch * (blockRows - 1u) + minimumRowBytes;
            if (mipDepth == 1)
                return upload.size >= sliceBytes;
            if (upload.depthPitch < sliceBytes)
                return false;
            if (upload.depthPitch > (static_cast<u64>(-1) - sliceBytes) / (mipDepth - 1u))
                return false;
            const u64 requiredBytes = upload.depthPitch * (mipDepth - 1u) + sliceBytes;
            return upload.size >= requiredBytes;
        }

        void AddResourceReference(const ResourceRef resource) noexcept
        {
            if (!resource.IsValid())
                return;
            VG_ASSERT_MSG(g_backend != nullptr, "cannot retain a GPU resource after RHI shutdown");
            if (g_backend == nullptr)
                return;
            VG_ASSERT_MSG(g_backend->IsResourceReferenceValid(resource), "cannot retain an invalid or stale GPU resource reference");
            if (g_backend->IsResourceReferenceValid(resource))
                g_backend->AddRef(resource);
        }

        i32 ReleaseResourceReference(const ResourceRef resource) noexcept
        {
            if (!resource.IsValid())
                return 0;
            VG_ASSERT_MSG(g_backend != nullptr, "cannot release a GPU resource after RHI shutdown");
            if (g_backend == nullptr)
                return 0;
            VG_ASSERT_MSG(g_backend->IsResourceReferenceValid(resource), "cannot release an invalid or stale GPU resource reference");
            return g_backend->IsResourceReferenceValid(resource) ? g_backend->Release(resource) : 0;
        }
    } // namespace

    bool Initialize(IBackend& backend, const DeviceParams& params, Failure* const failure) noexcept
    {
        ClearFailure(failure);
        if (g_backend != nullptr)
            return Reject(failure, FailureCode::AlreadyInitialized, "RHI is already initialized");

        if (params.residencyPolicy.pressureThresholdPercent > 100 || params.residencyPolicy.recoveryThresholdPercent >= params.residencyPolicy.pressureThresholdPercent ||
            params.residencyPolicy.maximumEvictionsPerMaintenance == 0 || params.residencyPolicy.maximumEvictionsPerMaintenance > MaximumResidencyBatchSize)
            return Reject(failure, FailureCode::InvalidArgument, "invalid GPU residency-policy configuration");

        Capabilities capabilities{};
        const BackendStatus status = backend.Initialize(params, capabilities);
        if (!Accept(status, failure))
            return false;
        if (capabilities.backend == BackendKind::Unknown || !IsPowerOfTwo(capabilities.uploadBufferAlignment) || !IsPowerOfTwo(capabilities.constantBufferAlignment) ||
            capabilities.maximumTextureDimension2D == 0 || capabilities.maximumTextureDimension3D == 0 || capabilities.maximumTextureArrayLayers == 0 ||
            capabilities.bindlessResources != (capabilities.descriptorIndexing && capabilities.maximumBindlessResources != 0) ||
            capabilities.bindlessSamplers != (capabilities.bindlessResources && capabilities.maximumBindlessSamplers != 0) ||
            (capabilities.placedResources.IsSupported() && capabilities.placedResources.aliasDiscard == PlacedAliasDiscardLowering::Unsupported) ||
            (capabilities.placedResources.buffers.IsSupported() &&
             (capabilities.placedResources.buffers.heapCategory != PlacedHeapCategory::Buffer || capabilities.placedResources.buffers.compatibilityClass != DeviceLocalBufferCompatibilityClass)) ||
            (capabilities.placedResources.textures.IsSupported() &&
             (capabilities.placedResources.textures.heapCategory != PlacedHeapCategory::Texture || capabilities.placedResources.textures.compatibilityClass != DeviceLocalTextureCompatibilityClass)) ||
            ((!capabilities.placedResources.IsSupported()) &&
             (capabilities.placedResources.aliasDiscard != PlacedAliasDiscardLowering::Unsupported || capabilities.placedResources.sameQueueGraphics || capabilities.placedResources.sameQueueCompute ||
              capabilities.placedResources.sameQueueCopy || capabilities.placedResources.graphicsComputeHandoff || capabilities.placedResources.copyQueueHandoff)) ||
            capabilities.variableRateShading != (capabilities.variableRateShadingDetails.tier != VariableRateShadingTier::None) ||
            (capabilities.variableRateShading &&
             (capabilities.variableRateShadingDetails.tier > VariableRateShadingTier::ShadingRateImage ||
              (capabilities.variableRateShadingDetails.supportedRates & ~((1u << static_cast<u32>(ShadingRate::Count)) - 1u)) != 0 ||
              (capabilities.variableRateShadingDetails.supportedCombiners & ~((1u << static_cast<u32>(ShadingRateCombiner::Count)) - 1u)) != 0 ||
              !capabilities.variableRateShadingDetails.Supports(ShadingRate::Rate1x1) || !capabilities.variableRateShadingDetails.Supports(ShadingRateCombiner::Passthrough) ||
              (capabilities.variableRateShadingDetails.shadingRateImageTileWidth == 0) != (capabilities.variableRateShadingDetails.shadingRateImageTileHeight == 0) ||
              (capabilities.variableRateShadingDetails.tier == VariableRateShadingTier::ShadingRateImage) != (capabilities.variableRateShadingDetails.shadingRateImageTileWidth != 0))))
        {
            static_cast<void>(backend.Shutdown());
            return Reject(failure, FailureCode::BackendFailure, "RHI backend returned an invalid capability contract");
        }

        g_capabilities = capabilities;
        g_submittedGraphics.SetValue(0);
        g_submittedCompute.SetValue(0);
        g_submittedCopy.SetValue(0);
        g_submissionCoverageLost.SetValue(0);
        g_backend = &backend;
        g_boundCommandList = {};
        return true;
    }

    bool Shutdown(Failure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!RequireBackend(failure))
            return false;
        if (g_boundCommandList.IsValid())
            return Reject(failure, FailureCode::Busy, "unbind the current command list before shutting down the RHI");
        if (!Accept(g_backend->Shutdown(), failure))
            return false;
        g_backend = nullptr;
        g_capabilities = {};
        return true;
    }

    bool AbandonDevice(Failure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!RequireBackend(failure))
            return false;
        g_boundCommandList = {};
        const BackendStatus status = g_backend->AbandonDevice();
        g_backend = nullptr;
        g_capabilities = {};
        return Accept(status, failure);
    }

    bool IsInitialized() noexcept
    {
        return g_backend != nullptr;
    }
    const Capabilities& GetCapabilities() noexcept
    {
        return g_capabilities;
    }
    DeviceState TestDeviceState() noexcept
    {
        return g_backend != nullptr ? g_backend->TestDeviceState() : DeviceState::Unknown;
    }

    bool WaitIdle(Failure* const failure) noexcept
    {
        if (!RequireBackend(failure))
            return false;
        return Accept(g_backend->WaitIdle(), failure);
    }

    bool RetireResources(Failure* const failure) noexcept
    {
        if (!RequireBackend(failure))
            return false;
        return Accept(g_backend->RetireResources(), failure);
    }

    bool FlushRetiredResources(Failure* const failure) noexcept
    {
        if (!RequireBackend(failure))
            return false;
        return Accept(g_backend->FlushRetiredResources(), failure);
    }

    bool QueryMemoryBudget(const MemorySegment segment, MemoryBudgetSnapshot& budget, Failure* const failure) noexcept
    {
        budget = {};
        budget.segment = segment;
        if (!RequireBackend(failure))
            return false;
        if (segment > MemorySegment::NonLocal)
            return Reject(failure, FailureCode::InvalidArgument, "invalid GPU memory segment");
        return Accept(g_backend->QueryMemoryBudget(segment, budget), failure);
    }

    bool SetResidencyPriority(const ResourceRef resource, const ResidencyPriority priority, Failure* const failure) noexcept
    {
        if (!RequireBackend(failure))
            return false;
        if (!resource.IsValid() || priority > ResidencyPriority::Maximum)
            return Reject(failure, FailureCode::InvalidArgument, "invalid GPU residency resource or priority");
        return Accept(g_backend->SetResidencyPriority(resource, priority), failure);
    }

    bool SetResidencyPinned(const ResourceRef resource, const bool pinned, Failure* const failure) noexcept
    {
        if (!RequireBackend(failure))
            return false;
        if (!resource.IsValid())
            return Reject(failure, FailureCode::InvalidReference, "cannot pin an invalid GPU allocation");
        return Accept(g_backend->SetResidencyPinned(resource, pinned), failure);
    }

    bool MakeResident(const containers::ArraySpan<const ResourceRef> resources, Failure* const failure) noexcept
    {
        if (!RequireBackend(failure))
            return false;
        if (resources.Data() == nullptr || resources.Size() == 0 || resources.Size() > MaximumResidencyBatchSize)
            return Reject(failure, FailureCode::InvalidArgument, "invalid GPU residency set");
        for (const ResourceRef resource : resources)
            if (!resource.IsValid() || !IsResourceReferenceValid(resource))
                return Reject(failure, FailureCode::InvalidReference, "GPU residency set contains an invalid resource");
        return Accept(g_backend->MakeResident(resources), failure);
    }

    bool Evict(const containers::ArraySpan<const ResourceRef> resources, const ResidencyFenceSet& safeAfter, Failure* const failure) noexcept
    {
        if (!RequireBackend(failure))
            return false;
        if (resources.Data() == nullptr || resources.Size() == 0 || resources.Size() > MaximumResidencyBatchSize)
            return Reject(failure, FailureCode::InvalidArgument, "invalid GPU eviction set");
        for (const ResourceRef resource : resources)
            if (!resource.IsValid() || !IsResourceReferenceValid(resource))
                return Reject(failure, FailureCode::InvalidReference, "GPU eviction set contains an invalid resource");
        return Accept(g_backend->Evict(resources, safeAfter), failure);
    }

    ResidencyStats GetResidencyStats() noexcept
    {
        return g_backend != nullptr ? g_backend->GetResidencyStats() : ResidencyStats{};
    }

    TextureRef CreateTexture(const TextureDesc& desc, const TextureInitData& initialData, Failure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!RequireBackend(failure))
            return {};
        if (desc.virtualResource && !g_capabilities.placedResources.textures.IsSupported())
        {
            Reject(failure, FailureCode::Unsupported, "deferred-binding textures are unsupported by the active placed-resource profile");
            return {};
        }
        if (HasFlag(desc.usage, TextureUsage::ShadingRate) && !g_capabilities.variableRateShading)
        {
            Reject(failure, FailureCode::Unsupported, "shading-rate images are not supported");
            return {};
        }
        if (!IsTextureDescriptorValid(desc) || (initialData.subresources == nullptr) != (initialData.subresourceCount == 0) || initialData.subresourceCount > MaximumTextureSubresourcesPerUpload ||
            (desc.virtualResource && initialData.subresourceCount != 0) || (desc.sampleCount > 1 && initialData.subresourceCount != 0))
        {
            Reject(failure, FailureCode::InvalidArgument, "invalid texture descriptor or initial data");
            return {};
        }
        for (u32 index = 0; index < initialData.subresourceCount; ++index)
        {
            const TextureSubresourceData& subresource = initialData.subresources[index];
            if (!IsTextureUploadValid(desc, subresource))
            {
                Reject(failure, FailureCode::InvalidArgument, "invalid texture subresource upload");
                return {};
            }
            for (u32 previous = 0; previous < index; ++previous)
            {
                if (initialData.subresources[previous].mipLevel == subresource.mipLevel && initialData.subresources[previous].arraySlice == subresource.arraySlice)
                {
                    Reject(failure, FailureCode::InvalidArgument, "duplicate texture subresource upload");
                    return {};
                }
            }
        }
        TextureRef texture{};
        if (!Accept(g_backend->CreateTexture(desc, initialData, texture), failure))
            return {};
        if (!texture.IsValid())
        {
            Reject(failure, FailureCode::BackendFailure, "RHI backend reported texture creation success without a resource");
            return {};
        }
        return texture;
    }

    BufferRef CreateBuffer(const BufferDesc& desc, const BufferInitData& initialData, Failure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!RequireBackend(failure))
            return {};
        if (desc.virtualResource && (!g_capabilities.placedResources.buffers.IsSupported() || desc.memoryType != MemoryType::DeviceLocal))
        {
            Reject(failure, FailureCode::Unsupported, "deferred-binding buffer class is unsupported by the active placed-resource profile");
            return {};
        }
        if (!IsBufferDescriptorValid(desc) || (initialData.data == nullptr) != (initialData.size == 0) || initialData.size > desc.size || (desc.virtualResource && initialData.data != nullptr))
        {
            Reject(failure, FailureCode::InvalidArgument, "invalid buffer descriptor or initial data");
            return {};
        }
        BufferRef buffer{};
        if (!Accept(g_backend->CreateBuffer(desc, initialData, buffer), failure))
            return {};
        if (!buffer.IsValid())
        {
            Reject(failure, FailureCode::BackendFailure, "RHI backend reported buffer creation success without a resource");
            return {};
        }
        return buffer;
    }

    HeapRef CreateHeap(const HeapDesc& desc, Failure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!RequireBackend(failure))
            return {};
        if (desc.size == 0 || !IsPowerOfTwo(desc.alignment) || desc.compatibilityClass == 0 || desc.memoryType > MemoryType::Readback || desc.heapCategory == PlacedHeapCategory::None ||
            desc.heapCategory > PlacedHeapCategory::Texture)
        {
            Reject(failure, FailureCode::InvalidArgument, "invalid GPU heap descriptor");
            return {};
        }
        const Capabilities::PlacedResourceClass& supportedClass = desc.heapCategory == PlacedHeapCategory::Buffer ? g_capabilities.placedResources.buffers : g_capabilities.placedResources.textures;
        if (!supportedClass.IsSupported() || desc.memoryType != MemoryType::DeviceLocal || supportedClass.heapCategory != desc.heapCategory || supportedClass.compatibilityClass != desc.compatibilityClass ||
            desc.alignment > supportedClass.maximumHeapAlignment)
        {
            Reject(failure, FailureCode::Unsupported, "heap descriptor is outside the active placed-resource profile");
            return {};
        }
        HeapRef heap{};
        if (!Accept(g_backend->CreateHeap(desc, heap), failure))
            return {};
        if (!heap.IsValid())
            Reject(failure, FailureCode::BackendFailure, "RHI backend reported heap creation success without a heap");
        return heap;
    }

    BindingLayoutRef RequestBindingLayout(const BindingLayoutDesc& desc, Failure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!RequireBackend(failure))
            return {};
        if (desc.entries == nullptr || desc.entryCount == 0 || desc.entryCount > MaximumBindingLayoutEntries || desc.visibility == 0 ||
            (desc.visibility & ~((1u << static_cast<u32>(ShaderStage::Count)) - 1u)) != 0)
        {
            Reject(failure, FailureCode::InvalidArgument, "invalid binding-layout descriptor");
            return {};
        }
        for (u32 index = 0; index < desc.entryCount; ++index)
        {
            const BindingLayoutEntry& entry = desc.entries[index];
            const u32 range = entry.type == BindingType::PushConstants ? 1u : entry.arrayCount;
            if (!IsValidBindingType(entry.type) || entry.arrayCount == 0 ||
                (entry.type == BindingType::PushConstants ? entry.arrayCount > g_capabilities.maximumPushConstantBytes : entry.arrayCount > MaximumFixedBindingArraySize) ||
                static_cast<u64>(entry.slot) + range > 0x100000000ull)
            {
                Reject(failure, FailureCode::InvalidArgument, "invalid binding-layout entry");
                return {};
            }
            for (u32 previousIndex = 0; previousIndex < index; ++previousIndex)
            {
                const BindingLayoutEntry& previous = desc.entries[previousIndex];
                if (BindingNamespace(previous.type) != BindingNamespace(entry.type))
                    continue;
                const u64 previousEnd = static_cast<u64>(previous.slot) + (previous.type == BindingType::PushConstants ? 1u : previous.arrayCount);
                const u64 entryEnd = static_cast<u64>(entry.slot) + range;
                if (static_cast<u64>(entry.slot) < previousEnd && static_cast<u64>(previous.slot) < entryEnd)
                {
                    Reject(failure, FailureCode::InvalidArgument, "binding-layout entries overlap within one descriptor namespace");
                    return {};
                }
            }
        }
        const BindingLayoutRef layout = g_backend->RequestBindingLayout(desc);
        if (!layout.IsValid())
            Reject(failure, FailureCode::BackendFailure, "RHI backend failed to create binding layout");
        return layout;
    }

    DescriptorDomainRef CreateDescriptorDomain(const DescriptorDomainDesc& desc, Failure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!RequireBackend(failure))
            return {};
        const bool samplerDomain = desc.kind == DescriptorDomainKind::Samplers;
        if (!g_capabilities.bindlessResources || !g_capabilities.descriptorIndexing || (samplerDomain && !g_capabilities.bindlessSamplers))
        {
            Reject(failure, FailureCode::Unsupported, "the active RHI device does not support bindless descriptor domains");
            return {};
        }
        const u32 maximumCapacity = samplerDomain ? g_capabilities.maximumBindlessSamplers : g_capabilities.maximumBindlessResources;
        if (desc.capacity == 0 || desc.capacity > maximumCapacity || desc.visibility == 0 || (desc.visibility & ~((1u << static_cast<u32>(ShaderStage::Count)) - 1u)) != 0 ||
            desc.kind > DescriptorDomainKind::Samplers)
        {
            Reject(failure, FailureCode::InvalidArgument, "invalid descriptor-domain descriptor");
            return {};
        }
        const DescriptorDomainRef domain = g_backend->CreateDescriptorDomain(desc);
        if (!domain.IsValid())
            Reject(failure, FailureCode::BackendFailure, "RHI backend failed to create descriptor domain");
        return domain;
    }

    DescriptorHandle AllocateDescriptor(const DescriptorDomainRef domain, Failure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!RequireBackend(failure))
            return {};
        if (!domain.IsValid())
        {
            Reject(failure, FailureCode::InvalidReference, "descriptor allocation requires a valid domain");
            return {};
        }
        const DescriptorHandle descriptor = g_backend->AllocateDescriptor(domain);
        if (!descriptor.IsValid())
            Reject(failure, FailureCode::CapacityExceeded, "descriptor domain has no recyclable slots");
        return descriptor;
    }

    bool WriteDescriptor(const DescriptorDomainRef domain, const DescriptorHandle descriptor, const TextureRef texture, const BindingType type, const TextureViewDesc& view, Failure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!RequireBackend(failure))
            return false;
        if (!domain.IsValid() || !descriptor.IsValid() || !texture.IsValid())
            return Reject(failure, FailureCode::InvalidReference, "texture descriptor write contains an invalid reference");
        if (type != BindingType::TextureShaderResource && type != BindingType::TextureUnorderedAccess)
            return Reject(failure, FailureCode::InvalidArgument, "texture descriptor write has an incompatible descriptor type");
        return Accept(g_backend->WriteDescriptor(domain, descriptor, texture, type, view), failure);
    }

    bool WriteDescriptor(const DescriptorDomainRef domain, const DescriptorHandle descriptor, const BufferRef buffer, const BindingType type, const BufferViewDesc& view, Failure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!RequireBackend(failure))
            return false;
        if (!domain.IsValid() || !descriptor.IsValid() || !buffer.IsValid())
            return Reject(failure, FailureCode::InvalidReference, "buffer descriptor write contains an invalid reference");
        const bool compatible = type == BindingType::ConstantBuffer || type == BindingType::TypedBufferShaderResource || type == BindingType::TypedBufferUnorderedAccess ||
                                type == BindingType::StructuredBufferShaderResource || type == BindingType::StructuredBufferUnorderedAccess || type == BindingType::ByteAddressBufferShaderResource ||
                                type == BindingType::ByteAddressBufferUnorderedAccess;
        if (!compatible)
            return Reject(failure, FailureCode::InvalidArgument, "buffer descriptor write has an incompatible descriptor type");
        return Accept(g_backend->WriteDescriptor(domain, descriptor, buffer, type, view), failure);
    }

    bool WriteDescriptor(const DescriptorDomainRef domain, const DescriptorHandle descriptor, const SamplerStateRef sampler, Failure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!RequireBackend(failure))
            return false;
        if (!domain.IsValid() || !descriptor.IsValid() || !sampler.IsValid())
            return Reject(failure, FailureCode::InvalidReference, "sampler descriptor write contains an invalid reference");
        return Accept(g_backend->WriteDescriptor(domain, descriptor, sampler), failure);
    }

    bool WriteDescriptor(const DescriptorDomainRef domain, const DescriptorHandle descriptor, const AccelerationStructureRef accelerationStructure, Failure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!RequireBackend(failure))
            return false;
        if (!g_capabilities.rayTracing)
            return Reject(failure, FailureCode::Unsupported, "ray-tracing acceleration structures are not supported");
        if (!domain || !descriptor || !accelerationStructure)
            return Reject(failure, FailureCode::InvalidReference, "acceleration-structure descriptor write contains an invalid reference");
        return Accept(g_backend->WriteDescriptor(domain, descriptor, accelerationStructure), failure);
    }

    bool RetireDescriptor(const DescriptorDomainRef domain, const DescriptorHandle descriptor, const DescriptorRetirement& retirement, Failure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!RequireBackend(failure))
            return false;
        if (!domain.IsValid() || !descriptor.IsValid())
            return Reject(failure, FailureCode::InvalidReference, "descriptor retirement contains an invalid reference");
        return Accept(g_backend->RetireDescriptor(domain, descriptor, retirement), failure);
    }

    DescriptorDomainStats GetDescriptorDomainStats(const DescriptorDomainRef domain) noexcept
    {
        return g_backend != nullptr && domain.IsValid() ? g_backend->GetDescriptorDomainStats(domain) : DescriptorDomainStats{};
    }

    SamplerStateRef RequestSamplerState(const SamplerStateDesc& desc, Failure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!RequireBackend(failure))
            return {};
        if (!(desc.minimumLod <= desc.maximumLod) || !(desc.maximumAnisotropy >= 1.0f && desc.maximumAnisotropy <= 16.0f))
        {
            Reject(failure, FailureCode::InvalidArgument, "invalid sampler-state descriptor");
            return {};
        }
        const SamplerStateRef samplerState = g_backend->RequestSamplerState(desc);
        if (!samplerState.IsValid())
            Reject(failure, FailureCode::BackendFailure, "RHI backend failed to create sampler state");
        return samplerState;
    }

    ShaderRef CreateShader(const ShaderDesc& desc, Failure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!RequireBackend(failure))
            return {};
        if (desc.bytecode == nullptr || desc.bytecodeSize == 0 || desc.entryPoint == nullptr || desc.entryPoint[0] == '\0' || desc.stage >= ShaderStage::Count)
        {
            Reject(failure, FailureCode::InvalidArgument, "invalid shader descriptor");
            return {};
        }
        const ShaderRef shader = g_backend->CreateShader(desc);
        if (!shader.IsValid())
            Reject(failure, FailureCode::BackendFailure, "RHI backend failed to create shader");
        return shader;
    }

    VertexLayoutRef GetVertexLayout(const VertexLayoutDesc& desc, Failure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!RequireBackend(failure))
            return {};
        if (desc.bindings == nullptr || desc.bindingCount == 0 || desc.bindingCount > MaximumVertexBindings || desc.attributes == nullptr || desc.attributeCount == 0 ||
            desc.attributeCount > MaximumVertexAttributes)
        {
            Reject(failure, FailureCode::InvalidArgument, "invalid vertex-layout descriptor");
            return {};
        }
        for (u32 bindingIndex = 0; bindingIndex < desc.bindingCount; ++bindingIndex)
        {
            const VertexBindingDesc& binding = desc.bindings[bindingIndex];
            if (binding.stride == 0 || binding.binding >= MaximumVertexBindings || (binding.inputRate == VertexInputRate::PerInstance && binding.instanceStepRate == 0))
            {
                Reject(failure, FailureCode::InvalidArgument, "invalid vertex binding");
                return {};
            }
            for (u32 previous = 0; previous < bindingIndex; ++previous)
            {
                if (desc.bindings[previous].binding == binding.binding)
                {
                    Reject(failure, FailureCode::InvalidArgument, "vertex binding indices must be unique");
                    return {};
                }
            }
        }
        for (u32 attributeIndex = 0; attributeIndex < desc.attributeCount; ++attributeIndex)
        {
            const VertexAttributeDesc& attribute = desc.attributes[attributeIndex];
            u32 semanticLength = 0;
            if (attribute.semanticName != nullptr)
                while (semanticLength < MaximumVertexSemanticNameLength && attribute.semanticName[semanticLength] != '\0')
                    ++semanticLength;
            bool knownBinding = false;
            for (u32 bindingIndex = 0; bindingIndex < desc.bindingCount; ++bindingIndex)
                knownBinding = knownBinding || desc.bindings[bindingIndex].binding == attribute.binding;
            if (!knownBinding || attribute.format == Format::Unknown || semanticLength == 0 || semanticLength == MaximumVertexSemanticNameLength)
            {
                Reject(failure, FailureCode::InvalidArgument, "vertex attribute references an unknown binding or format");
                return {};
            }
            for (u32 previous = 0; previous < attributeIndex; ++previous)
            {
                if (desc.attributes[previous].location == attribute.location)
                {
                    Reject(failure, FailureCode::InvalidArgument, "vertex attribute locations must be unique");
                    return {};
                }
            }
        }
        const VertexLayoutRef vertexLayout = g_backend->GetVertexLayout(desc);
        if (!vertexLayout.IsValid())
            Reject(failure, FailureCode::BackendFailure, "RHI backend failed to create vertex layout");
        return vertexLayout;
    }

    PipelineRef CreateGraphicsPipeline(const GraphicsPipelineDesc& desc, Failure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!RequireBackend(failure))
            return {};
        if (!desc.vertexShader || desc.bindingLayoutCount > MaximumBindingLayoutsPerPipeline || desc.bindingLayoutCount + desc.descriptorDomainCount > MaximumBindingLayoutsPerPipeline ||
            (desc.bindingLayouts == nullptr) != (desc.bindingLayoutCount == 0) || desc.attachments.colorCount > MaximumColorAttachments || desc.descriptorDomainCount > MaximumDescriptorDomainsPerPipeline ||
            (desc.descriptorDomains == nullptr) != (desc.descriptorDomainCount == 0) || desc.attachments.sampleCount == 0 || (desc.topology == PrimitiveTopology::PatchList && desc.patchControlPoints == 0))
        {
            Reject(failure, FailureCode::InvalidArgument, "invalid graphics-pipeline descriptor");
            return {};
        }
        for (u32 index = 0; index < desc.bindingLayoutCount; ++index)
            if (!IsResourceReferenceValid(ResourceRef(desc.bindingLayouts[index])))
            {
                Reject(failure, FailureCode::InvalidReference, "graphics pipeline references an invalid binding layout");
                return {};
            }
        for (u32 index = 0; index < desc.descriptorDomainCount; ++index)
            if (!IsResourceReferenceValid(ResourceRef(desc.descriptorDomains[index])))
            {
                Reject(failure, FailureCode::InvalidReference, "graphics pipeline references an invalid descriptor domain");
                return {};
            }
        const ShaderRef shaders[] = {desc.vertexShader, desc.hullShader, desc.domainShader, desc.geometryShader, desc.pixelShader};
        for (const ShaderRef shader : shaders)
            if (shader && !IsResourceReferenceValid(ResourceRef(shader)))
            {
                Reject(failure, FailureCode::InvalidReference, "graphics pipeline references an invalid shader");
                return {};
            }
        if (desc.vertexLayout && !IsResourceReferenceValid(ResourceRef(desc.vertexLayout)))
        {
            Reject(failure, FailureCode::InvalidReference, "graphics pipeline references an invalid vertex layout");
            return {};
        }
        for (u32 index = 0; index < desc.attachments.colorCount; ++index)
            if (desc.attachments.colorFormats[index] == Format::Unknown)
            {
                Reject(failure, FailureCode::InvalidArgument, "graphics pipeline contains an unknown color format");
                return {};
            }
        const PipelineRef pipeline = g_backend->CreateGraphicsPipeline(desc);
        if (!pipeline)
            Reject(failure, FailureCode::BackendFailure, "RHI backend failed to create graphics pipeline");
        return pipeline;
    }

    PipelineRef CreateComputePipeline(const ComputePipelineDesc& desc, Failure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!RequireBackend(failure))
            return {};
        if (!desc.computeShader || !IsResourceReferenceValid(ResourceRef(desc.computeShader)) || desc.bindingLayoutCount > MaximumBindingLayoutsPerPipeline ||
            desc.bindingLayoutCount + desc.descriptorDomainCount > MaximumBindingLayoutsPerPipeline || (desc.bindingLayouts == nullptr) != (desc.bindingLayoutCount == 0) ||
            desc.descriptorDomainCount > MaximumDescriptorDomainsPerPipeline || (desc.descriptorDomains == nullptr) != (desc.descriptorDomainCount == 0))
        {
            Reject(failure, FailureCode::InvalidArgument, "invalid compute-pipeline descriptor");
            return {};
        }
        for (u32 index = 0; index < desc.bindingLayoutCount; ++index)
            if (!IsResourceReferenceValid(ResourceRef(desc.bindingLayouts[index])))
            {
                Reject(failure, FailureCode::InvalidReference, "compute pipeline references an invalid binding layout");
                return {};
            }
        for (u32 index = 0; index < desc.descriptorDomainCount; ++index)
            if (!IsResourceReferenceValid(ResourceRef(desc.descriptorDomains[index])))
            {
                Reject(failure, FailureCode::InvalidReference, "compute pipeline references an invalid descriptor domain");
                return {};
            }
        const PipelineRef pipeline = g_backend->CreateComputePipeline(desc);
        if (!pipeline)
            Reject(failure, FailureCode::BackendFailure, "RHI backend failed to create compute pipeline");
        return pipeline;
    }

    PipelineRef CreateRayTracingPipeline(const RayTracingPipelineDesc& desc, Failure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!RequireBackend(failure))
            return {};
        if (!g_capabilities.rayTracingPipeline)
        {
            Reject(failure, FailureCode::Unsupported, "ray-tracing pipelines are not supported by this device");
            return {};
        }
        if (desc.shaders == nullptr || desc.shaderCount == 0 || desc.shaderCount > MaximumRayTracingShaders || (desc.hitGroups == nullptr) != (desc.hitGroupCount == 0) ||
            desc.hitGroupCount > MaximumRayTracingHitGroups || (desc.globalBindingLayouts == nullptr) != (desc.globalBindingLayoutCount == 0) ||
            desc.globalBindingLayoutCount > MaximumBindingLayoutsPerPipeline || desc.globalBindingLayoutCount + desc.descriptorDomainCount > MaximumBindingLayoutsPerPipeline ||
            desc.descriptorDomainCount > MaximumDescriptorDomainsPerPipeline || (desc.descriptorDomains == nullptr) != (desc.descriptorDomainCount == 0) || desc.maximumRecursionDepth == 0)
        {
            Reject(failure, FailureCode::InvalidArgument, "invalid ray-tracing pipeline descriptor");
            return {};
        }
        for (u32 index = 0; index < desc.shaderCount; ++index)
        {
            const RayTracingShaderDesc& shader = desc.shaders[index];
            if (shader.exportName == nullptr || shader.exportName[0] == '\0' || !IsResourceReferenceValid(ResourceRef(shader.shader)) ||
                (shader.localBindingLayout && !IsResourceReferenceValid(ResourceRef(shader.localBindingLayout))))
            {
                Reject(failure, FailureCode::InvalidReference, "ray-tracing pipeline contains an invalid shader export");
                return {};
            }
            for (u32 previous = 0; previous < index; ++previous)
            {
                const char* left = shader.exportName;
                const char* right = desc.shaders[previous].exportName;
                while (*left != '\0' && *right != '\0' && *left == *right)
                {
                    ++left;
                    ++right;
                }
                if (*left == *right)
                {
                    Reject(failure, FailureCode::InvalidArgument, "ray-tracing shader export names must be unique");
                    return {};
                }
            }
        }
        for (u32 index = 0; index < desc.globalBindingLayoutCount; ++index)
            if (!IsResourceReferenceValid(ResourceRef(desc.globalBindingLayouts[index])))
            {
                Reject(failure, FailureCode::InvalidReference, "ray-tracing pipeline references an invalid global binding layout");
                return {};
            }
        for (u32 index = 0; index < desc.descriptorDomainCount; ++index)
            if (!IsResourceReferenceValid(ResourceRef(desc.descriptorDomains[index])))
            {
                Reject(failure, FailureCode::InvalidReference, "ray-tracing pipeline references an invalid descriptor domain");
                return {};
            }
        for (u32 index = 0; index < desc.hitGroupCount; ++index)
        {
            const RayTracingHitGroupDesc& group = desc.hitGroups[index];
            if (group.exportName == nullptr || group.exportName[0] == '\0' || (!group.closestHitShader && !group.anyHitShader && !group.intersectionShader) ||
                (group.proceduralPrimitive != static_cast<bool>(group.intersectionShader)))
            {
                Reject(failure, FailureCode::InvalidArgument, "ray-tracing pipeline contains an invalid hit group");
                return {};
            }
            const ShaderRef shaders[] = {group.closestHitShader, group.anyHitShader, group.intersectionShader};
            for (const ShaderRef shader : shaders)
                if (shader && !IsResourceReferenceValid(ResourceRef(shader)))
                {
                    Reject(failure, FailureCode::InvalidReference, "ray-tracing hit group references an invalid shader");
                    return {};
                }
            if (group.localBindingLayout && !IsResourceReferenceValid(ResourceRef(group.localBindingLayout)))
            {
                Reject(failure, FailureCode::InvalidReference, "ray-tracing hit group references an invalid local binding layout");
                return {};
            }
        }
        const PipelineRef pipeline = g_backend->CreateRayTracingPipeline(desc);
        if (!pipeline)
            Reject(failure, FailureCode::BackendFailure, "RHI backend failed to create ray-tracing pipeline");
        return pipeline;
    }

    AccelerationStructureRef CreateAccelerationStructure(const AccelerationStructureDesc& desc, Failure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!RequireBackend(failure))
            return {};
        if (!g_capabilities.rayTracing)
        {
            Reject(failure, FailureCode::Unsupported, "ray-tracing acceleration structures are not supported");
            return {};
        }
        if ((desc.kind == AccelerationStructureKind::BottomLevel && (desc.geometries == nullptr || desc.geometryCount == 0)) ||
            (desc.kind == AccelerationStructureKind::TopLevel && desc.maximumInstanceCount == 0))
        {
            Reject(failure, FailureCode::InvalidArgument, "invalid acceleration-structure descriptor");
            return {};
        }
        const AccelerationStructureRef result = g_backend->CreateAccelerationStructure(desc);
        if (!result)
            Reject(failure, FailureCode::BackendFailure, "RHI backend failed to create acceleration structure");
        return result;
    }

    ShaderTableRef CreateShaderTable(const ShaderTableDesc& desc, Failure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!RequireBackend(failure))
            return {};
        if (!g_capabilities.rayTracingPipeline)
        {
            Reject(failure, FailureCode::Unsupported, "ray-tracing shader tables are not supported");
            return {};
        }
        if (!desc.pipeline || desc.rayGeneration.exportName == nullptr || desc.rayGeneration.exportName[0] == '\0')
        {
            Reject(failure, FailureCode::InvalidArgument, "invalid ray-tracing shader-table descriptor");
            return {};
        }
        const ShaderTableRef result = g_backend->CreateShaderTable(desc);
        if (!result)
            Reject(failure, FailureCode::BackendFailure, "RHI backend failed to create shader table");
        return result;
    }

    u64 GetAccelerationStructureDeviceAddress(const AccelerationStructureRef accelerationStructure, Failure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!RequireBackend(failure))
            return 0;
        if (!g_capabilities.rayTracing)
        {
            Reject(failure, FailureCode::Unsupported, "ray-tracing acceleration structures are not supported");
            return 0;
        }
        if (!accelerationStructure)
        {
            Reject(failure, FailureCode::InvalidReference, "invalid acceleration structure");
            return 0;
        }
        u64 address = 0;
        if (!Accept(g_backend->GetAccelerationStructureDeviceAddress(accelerationStructure, address), failure))
            return 0;
        if (address == 0)
            Reject(failure, FailureCode::BackendFailure, "backend returned an invalid acceleration-structure address");
        return address;
    }

    QueryPoolRef CreateQueryPool(const QueryPoolDesc& desc, Failure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!RequireBackend(failure))
            return {};
        if (desc.capacity == 0 || desc.capacity > MaximumQueryPoolEntries)
        {
            Reject(failure, FailureCode::InvalidArgument, "query-pool capacity is outside the supported range");
            return {};
        }
        const bool supported = (desc.type == QueryType::Occlusion && g_capabilities.occlusionQueries) || (desc.type == QueryType::PipelineStatistics && g_capabilities.pipelineStatisticsQueries) ||
                               (desc.type == QueryType::Timestamp && g_capabilities.timestampQueries);
        if (!supported)
        {
            Reject(failure, FailureCode::Unsupported, "requested query type is not supported by the active RHI backend");
            return {};
        }
        const QueryPoolRef queryPool = g_backend->CreateQueryPool(desc);
        if (!queryPool.IsValid())
            Reject(failure, FailureCode::BackendFailure, "RHI backend failed to create query pool");
        return queryPool;
    }

    void DestroyQueryPool(QueryPoolRef& queryPool) noexcept
    {
        if (queryPool.IsValid())
        {
            VG_ASSERT_MSG(g_backend != nullptr, "cannot destroy a query pool after RHI shutdown");
            if (g_backend != nullptr)
                g_backend->DestroyQueryPool(queryPool);
        }
        queryPool = {};
    }

    bool BeginQuery(const QueryPoolRef queryPool, const u32 index, Failure* const failure) noexcept
    {
        if (!RequireBoundCommandList(failure))
            return false;
        if (!queryPool.IsValid())
            return Reject(failure, FailureCode::InvalidReference, "invalid query pool");
        return Accept(g_backend->BeginQuery(g_boundCommandList, queryPool, index), failure);
    }

    bool EndQuery(const QueryPoolRef queryPool, const u32 index, Failure* const failure) noexcept
    {
        if (!RequireBoundCommandList(failure))
            return false;
        if (!queryPool.IsValid())
            return Reject(failure, FailureCode::InvalidReference, "invalid query pool");
        return Accept(g_backend->EndQuery(g_boundCommandList, queryPool, index), failure);
    }

    bool IssueQuery(const QueryPoolRef queryPool, const u32 index, Failure* const failure) noexcept
    {
        if (!RequireBoundCommandList(failure))
            return false;
        if (!queryPool.IsValid())
            return Reject(failure, FailureCode::InvalidReference, "invalid query pool");
        return Accept(g_backend->IssueQuery(g_boundCommandList, queryPool, index), failure);
    }

    bool ResolveQueries(const QueryPoolRef queryPool, const u32 start, const u32 count, Failure* const failure) noexcept
    {
        if (!RequireBoundCommandList(failure))
            return false;
        if (!queryPool.IsValid() || count == 0)
            return Reject(failure, FailureCode::InvalidArgument, "invalid query resolve range");
        return Accept(g_backend->ResolveQueries(g_boundCommandList, queryPool, start, count), failure);
    }

    bool AcquireQueries(const QueryPoolRef queryPool, const u32 start, const u32 count, Failure* const failure) noexcept
    {
        if (!RequireBackend(failure))
            return false;
        if (!queryPool.IsValid() || count == 0)
            return Reject(failure, FailureCode::InvalidArgument, "invalid query acquisition range");
        return Accept(g_backend->AcquireQueries(queryPool, start, count), failure);
    }

    void ReleaseQueries(const QueryPoolRef queryPool) noexcept
    {
        VG_ASSERT_MSG(g_backend != nullptr, "cannot release query results after RHI shutdown");
        if (g_backend != nullptr && queryPool.IsValid())
            g_backend->ReleaseQueries(queryPool);
    }

    bool GetQueryResult(const QueryPoolRef queryPool, const u32 index, u64& result, Failure* const failure) noexcept
    {
        result = 0;
        if (!RequireBackend(failure))
            return false;
        if (!queryPool.IsValid())
            return Reject(failure, FailureCode::InvalidReference, "invalid query pool");
        return Accept(g_backend->GetQueryResult(queryPool, index, result), failure);
    }

    bool GetQueryResult(const QueryPoolRef queryPool, const u32 index, PipelineStatistics& result, Failure* const failure) noexcept
    {
        result = {};
        if (!RequireBackend(failure))
            return false;
        if (!queryPool.IsValid())
            return Reject(failure, FailureCode::InvalidReference, "invalid query pool");
        return Accept(g_backend->GetQueryResult(queryPool, index, result), failure);
    }

    u64 GetTimestampFrequency(const QueueType queue, Failure* const failure) noexcept
    {
        if (!RequireBackend(failure))
            return 0;
        u64 frequency = 0;
        return Accept(g_backend->GetTimestampFrequency(queue, frequency), failure) ? frequency : 0;
    }

    bool CalibrateTimestamps(const QueueType queue, TimestampCalibration& calibration, Failure* const failure) noexcept
    {
        calibration = {};
        if (!RequireBackend(failure))
            return false;
        return Accept(g_backend->CalibrateTimestamps(queue, calibration), failure);
    }

    bool BindMemory(const TextureRef texture, const HeapRef heap, const u64 offset, Failure* const failure) noexcept
    {
        if (!RequireBackend(failure))
            return false;
        if (!texture.IsValid() || !heap.IsValid())
            return Reject(failure, FailureCode::InvalidReference, "invalid texture or heap reference");
        return Accept(g_backend->BindMemory(texture, heap, offset), failure);
    }

    bool BindMemory(const BufferRef buffer, const HeapRef heap, const u64 offset, Failure* const failure) noexcept
    {
        if (!RequireBackend(failure))
            return false;
        if (!buffer.IsValid() || !heap.IsValid())
            return Reject(failure, FailureCode::InvalidReference, "invalid buffer or heap reference");
        return Accept(g_backend->BindMemory(buffer, heap, offset), failure);
    }

    bool GetHeapDesc(const HeapRef heap, HeapDesc& desc, Failure* const failure) noexcept
    {
        desc = {};
        if (!RequireBackend(failure))
            return false;
        if (!heap.IsValid())
            return Reject(failure, FailureCode::InvalidReference, "cannot query an invalid heap reference");
        if (!Accept(g_backend->GetHeapDesc(heap, desc), failure))
        {
            desc = {};
            return false;
        }
        if (desc.size == 0 || !IsPowerOfTwo(desc.alignment) || desc.compatibilityClass == 0 || desc.heapCategory == PlacedHeapCategory::None)
        {
            desc = {};
            return Reject(failure, FailureCode::BackendFailure, "backend returned an invalid heap descriptor");
        }
        return true;
    }

    bool GetPlacement(const TextureRef texture, PlacementRecord& placement, Failure* const failure) noexcept
    {
        placement = {};
        if (!RequireBackend(failure))
            return false;
        if (!texture.IsValid())
            return Reject(failure, FailureCode::InvalidReference, "cannot query placement for an invalid texture");
        if (!Accept(g_backend->GetPlacement(texture, placement), failure))
        {
            placement = {};
            return false;
        }
        if (!placement.IsValid())
        {
            placement = {};
            return Reject(failure, FailureCode::BackendFailure, "backend returned an invalid texture placement");
        }
        return true;
    }

    bool GetPlacement(const BufferRef buffer, PlacementRecord& placement, Failure* const failure) noexcept
    {
        placement = {};
        if (!RequireBackend(failure))
            return false;
        if (!buffer.IsValid())
            return Reject(failure, FailureCode::InvalidReference, "cannot query placement for an invalid buffer");
        if (!Accept(g_backend->GetPlacement(buffer, placement), failure))
        {
            placement = {};
            return false;
        }
        if (!placement.IsValid())
        {
            placement = {};
            return Reject(failure, FailureCode::BackendFailure, "backend returned an invalid buffer placement");
        }
        return true;
    }

    bool GetTextureDesc(const TextureRef texture, TextureDesc& desc, Failure* const failure) noexcept
    {
        desc = {};
        if (!RequireBackend(failure))
            return false;
        if (!texture.IsValid())
            return Reject(failure, FailureCode::InvalidReference, "cannot query an invalid texture reference");
        if (!Accept(g_backend->GetTextureDesc(texture, desc), failure))
        {
            desc = {};
            return false;
        }
        return true;
    }

    bool GetBufferDesc(const BufferRef buffer, BufferDesc& desc, Failure* const failure) noexcept
    {
        desc = {};
        if (!RequireBackend(failure))
            return false;
        if (!buffer.IsValid())
            return Reject(failure, FailureCode::InvalidReference, "cannot query an invalid buffer reference");
        if (!Accept(g_backend->GetBufferDesc(buffer, desc), failure))
        {
            desc = {};
            return false;
        }
        return true;
    }

    bool GetMemoryRequirements(const TextureDesc& desc, MemoryRequirements& requirements, Failure* const failure) noexcept
    {
        requirements = {};
        if (!RequireBackend(failure))
            return false;
        if (HasFlag(desc.usage, TextureUsage::ShadingRate) && !g_capabilities.variableRateShading)
            return Reject(failure, FailureCode::Unsupported, "shading-rate images are not supported");
        if (!IsTextureDescriptorValid(desc))
            return Reject(failure, FailureCode::InvalidArgument, "invalid texture descriptor for memory-requirement query");
        if (!Accept(g_backend->GetMemoryRequirements(desc, requirements), failure))
        {
            requirements = {};
            return false;
        }
        if (requirements.size == 0 || !IsPowerOfTwo(requirements.alignment) || requirements.compatibilityClass != DeviceLocalTextureCompatibilityClass || requirements.memoryType != MemoryType::DeviceLocal ||
            requirements.heapCategory != PlacedHeapCategory::Texture)
        {
            requirements = {};
            return Reject(failure, FailureCode::BackendFailure, "backend returned invalid texture memory requirements");
        }
        return true;
    }

    bool GetMemoryRequirements(const BufferDesc& desc, MemoryRequirements& requirements, Failure* const failure) noexcept
    {
        requirements = {};
        if (!RequireBackend(failure))
            return false;
        if (!IsBufferDescriptorValid(desc))
            return Reject(failure, FailureCode::InvalidArgument, "invalid buffer descriptor for memory-requirement query");
        if (!Accept(g_backend->GetMemoryRequirements(desc, requirements), failure))
        {
            requirements = {};
            return false;
        }
        const u64 expectedClass = desc.memoryType == MemoryType::Upload     ? UploadBufferCompatibilityClass
                                  : desc.memoryType == MemoryType::Readback ? ReadbackBufferCompatibilityClass
                                                                            : DeviceLocalBufferCompatibilityClass;
        if (requirements.size < desc.size || !IsPowerOfTwo(requirements.alignment) || requirements.compatibilityClass != expectedClass || requirements.memoryType != desc.memoryType ||
            requirements.heapCategory != PlacedHeapCategory::Buffer)
        {
            requirements = {};
            return Reject(failure, FailureCode::BackendFailure, "backend returned invalid buffer memory requirements");
        }
        return true;
    }

    MemoryRequirements GetMemoryRequirements(const TextureRef texture) noexcept
    {
        return g_backend != nullptr && texture.IsValid() ? g_backend->GetMemoryRequirements(texture) : MemoryRequirements{};
    }

    MemoryRequirements GetMemoryRequirements(const BufferRef buffer) noexcept
    {
        return g_backend != nullptr && buffer.IsValid() ? g_backend->GetMemoryRequirements(buffer) : MemoryRequirements{};
    }

    NativeReleaseObservation ObserveNativeRelease(const ResourceRef resource, Failure* const failure) noexcept
    {
        NativeReleaseObservation observation{};
        if (!RequireBackend(failure))
            return {};
        if (!resource.IsValid())
        {
            Reject(failure, FailureCode::InvalidReference, "cannot observe an invalid resource reference");
            return {};
        }
        return Accept(g_backend->ObserveNativeRelease(resource, observation), failure) ? observation : NativeReleaseObservation{};
    }

    bool IsNativeReleaseComplete(const NativeReleaseObservation observation) noexcept
    {
        return g_backend != nullptr && observation.IsValid() && g_backend->IsNativeReleaseComplete(observation);
    }

    void ReleaseNativeReleaseObservation(NativeReleaseObservation& observation) noexcept
    {
        observation.Reset();
    }

    bool IsResourceReferenceValid(const ResourceRef resource) noexcept
    {
        return g_backend != nullptr && resource.IsValid() && g_backend->IsResourceReferenceValid(resource);
    }
    ResourceLifetimeStats GetResourceLifetimeStats() noexcept
    {
        return g_backend != nullptr ? g_backend->GetResourceLifetimeStats() : ResourceLifetimeStats{};
    }

    void AddRef(const TextureRef resource) noexcept
    {
        AddResourceReference(resource);
    }
    void AddRef(const TextureReadbackRef resource) noexcept
    {
        AddResourceReference(resource);
    }
    void AddRef(const BufferRef resource) noexcept
    {
        AddResourceReference(resource);
    }
    void AddRef(const HeapRef resource) noexcept
    {
        AddResourceReference(resource);
    }
    void AddRef(const SamplerStateRef resource) noexcept
    {
        AddResourceReference(resource);
    }
    void AddRef(const ShaderRef resource) noexcept
    {
        AddResourceReference(resource);
    }
    void AddRef(const PipelineRef resource) noexcept
    {
        AddResourceReference(resource);
    }
    void AddRef(const BindingLayoutRef resource) noexcept
    {
        AddResourceReference(resource);
    }
    void AddRef(const DescriptorDomainRef resource) noexcept
    {
        AddResourceReference(resource);
    }
    void AddRef(const AccelerationStructureRef resource) noexcept
    {
        AddResourceReference(resource);
    }
    void AddRef(const ShaderTableRef resource) noexcept
    {
        AddResourceReference(resource);
    }
    void AddRef(const SwapChainRef resource) noexcept
    {
        AddResourceReference(resource);
    }

    i32 Release(const TextureRef resource) noexcept
    {
        return ReleaseResourceReference(resource);
    }
    i32 Release(const TextureReadbackRef resource) noexcept
    {
        return ReleaseResourceReference(resource);
    }
    i32 Release(const BufferRef resource) noexcept
    {
        return ReleaseResourceReference(resource);
    }
    i32 Release(const HeapRef resource) noexcept
    {
        return ReleaseResourceReference(resource);
    }
    i32 Release(const SamplerStateRef resource) noexcept
    {
        return ReleaseResourceReference(resource);
    }
    i32 Release(const ShaderRef resource) noexcept
    {
        return ReleaseResourceReference(resource);
    }
    i32 Release(const PipelineRef resource) noexcept
    {
        return ReleaseResourceReference(resource);
    }
    i32 Release(const BindingLayoutRef resource) noexcept
    {
        return ReleaseResourceReference(resource);
    }
    i32 Release(const DescriptorDomainRef resource) noexcept
    {
        return ReleaseResourceReference(resource);
    }
    i32 Release(const AccelerationStructureRef resource) noexcept
    {
        return ReleaseResourceReference(resource);
    }
    i32 Release(const ShaderTableRef resource) noexcept
    {
        return ReleaseResourceReference(resource);
    }
    i32 Release(const SwapChainRef resource) noexcept
    {
        return ReleaseResourceReference(resource);
    }

    CommandListRef CreateCommandList(const CommandListType type, const u64 debugHash, Failure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!RequireBackend(failure))
            return {};
        if (type <= CommandListType::None || type > CommandListType::Compute || (type == CommandListType::Compute && !g_capabilities.asyncCompute) ||
            (type == CommandListType::CopyAsync && !g_capabilities.copyQueue))
        {
            Reject(failure, FailureCode::InvalidArgument, "invalid command-list type");
            return {};
        }
        const CommandListRef commandList = g_backend->CreateCommandList(type, debugHash);
        if (!commandList.IsValid())
            Reject(failure, FailureCode::BackendFailure, "RHI backend failed to create command list");
        return commandList;
    }

    void DiscardCommandList(CommandListRef& commandList) noexcept
    {
        if (!commandList.IsValid())
            return;
        VG_ASSERT_MSG(g_backend != nullptr, "cannot discard a command list after RHI shutdown");
        VG_ASSERT_MSG(commandList != g_boundCommandList, "unbind a command list before discarding it");
        if (g_backend != nullptr && commandList != g_boundCommandList)
            g_backend->DiscardCommandList(commandList);
        commandList = {};
    }

    bool BindCommandList(const CommandListRef commandList, Failure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!RequireBackend(failure))
            return false;
        if (!commandList.IsValid() || g_backend->GetCommandListType(commandList) == CommandListType::None)
            return Reject(failure, FailureCode::InvalidCommandList, "cannot bind an invalid command list");
        g_boundCommandList = commandList;
        return true;
    }

    void UnbindCommandList() noexcept
    {
        g_boundCommandList = {};
    }
    CommandListRef GetBoundCommandList() noexcept
    {
        return g_boundCommandList;
    }
    CommandListType GetBoundCommandListType() noexcept
    {
        return g_backend != nullptr && g_boundCommandList.IsValid() ? g_backend->GetCommandListType(g_boundCommandList) : CommandListType::None;
    }

    bool CloseCommandList(const CommandListRef commandList, Failure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!RequireBackend(failure))
            return false;
        if (!commandList.IsValid() || commandList == g_boundCommandList)
            return Reject(failure, FailureCode::InvalidCommandList, "cannot close an invalid or currently bound command list");
        return Accept(g_backend->CloseCommandList(commandList), failure);
    }

    bool SubmitCommandLists(const char* const scopeName, const containers::ArraySpan<const CommandListRef> commandLists, const CommandListSyncType sync, SubmissionReceipt& receipt,
                            Failure* const failure) noexcept
    {
        ClearFailure(failure);
        receipt = {};
        if (!RequireBackend(failure) || !ValidateCommandListSubmission(scopeName, commandLists, sync, failure))
            return false;
        const bool succeeded = Accept(g_backend->SubmitCommandLists(scopeName, commandLists, sync, receipt), failure);
        if (receipt.WasSubmitted())
        {
            if (!succeeded || !receipt.IsValid())
                g_submissionCoverageLost.SetValue(1);
            else
            {
                IncludeSubmittedFence(g_submittedGraphics, receipt.residency.graphics);
                IncludeSubmittedFence(g_submittedCompute, receipt.residency.compute);
                IncludeSubmittedFence(g_submittedCopy, receipt.residency.copy);
            }
        }
        if (!succeeded)
        {
            if (!receipt.WasSubmitted())
                receipt = {};
            return false;
        }
        if (!receipt.IsValid())
        {
            g_submissionCoverageLost.SetValue(1);
            return Reject(failure, FailureCode::BackendFailure, "backend returned an invalid submission receipt");
        }
        return true;
    }

    bool CloseAndSubmitCommandLists(const char* const scopeName, const containers::ArraySpan<const CommandListRef> commandLists, const CommandListSyncType sync, SubmissionReceipt& receipt,
                                    Failure* const failure) noexcept
    {
        ClearFailure(failure);
        receipt = {};
        if (!RequireBackend(failure) || !ValidateCommandListSubmission(scopeName, commandLists, sync, failure))
            return false;
        for (const CommandListRef commandList : commandLists)
        {
            if (!Accept(g_backend->CloseCommandList(commandList), failure))
                return false;
        }
        return SubmitCommandLists(scopeName, commandLists, sync, receipt, failure);
    }

    bool CloseAndSubmitCommandLists(const char* const scopeName, const containers::ArraySpan<const CommandListRef> commandLists, const CommandListSyncType sync, GpuFence& completion,
                                    Failure* const failure) noexcept
    {
        SubmissionReceipt receipt{};
        const bool submitted = CloseAndSubmitCommandLists(scopeName, commandLists, sync, receipt, failure);
        completion = submitted ? receipt.completion : GpuFence{};
        return submitted;
    }

    GpuFence GetGpuFence() noexcept
    {
        return g_backend != nullptr && g_boundCommandList.IsValid() ? g_backend->GetGpuFence(g_boundCommandList) : GpuFence{};
    }
    bool GetSubmittedResidencyFences(ResidencyFenceSet& fences) noexcept
    {
        fences = {};
        if (g_backend == nullptr || g_submissionCoverageLost.GetValue() != 0)
            return false;
        fences = {g_submittedGraphics.GetValue(), g_submittedCompute.GetValue(), g_submittedCopy.GetValue()};
        if (fences.graphics == 0)
            fences.neverSubmittedQueues |= static_cast<u8>(1u << static_cast<u32>(QueueType::Graphics));
        if (fences.compute == 0)
            fences.neverSubmittedQueues |= static_cast<u8>(1u << static_cast<u32>(QueueType::Compute));
        if (fences.copy == 0)
            fences.neverSubmittedQueues |= static_cast<u8>(1u << static_cast<u32>(QueueType::Copy));
        if (g_submissionCoverageLost.GetValue() != 0)
        {
            fences = {};
            return false;
        }
        return true;
    }
    bool IsGpuFenceComplete(const GpuFence fence) noexcept
    {
        return !fence.IsValid() || (g_backend != nullptr && g_backend->IsGpuFenceComplete(fence));
    }
    bool WaitForGpuFence(const GpuFence fence, const u64 timeoutNanoseconds, Failure* const failure) noexcept
    {
        if (!fence.IsValid())
        {
            ClearFailure(failure);
            return true;
        }
        if (!RequireBackend(failure))
            return false;
        return Accept(g_backend->WaitForGpuFence(fence, timeoutNanoseconds), failure);
    }

    bool AddToResidencyWorkingSet(const ResourceRef resource, Failure* const failure) noexcept
    {
        if (!RequireBoundCommandList(failure))
            return false;
        if (!resource.IsValid() || !IsResourceReferenceValid(resource))
            return Reject(failure, FailureCode::InvalidReference, "cannot add an invalid resource to the residency working set");
        if (resource.GetKind() != ResourceKind::Texture && resource.GetKind() != ResourceKind::Buffer && resource.GetKind() != ResourceKind::Heap)
            return Reject(failure, FailureCode::IncompatibleBinding, "only textures, buffers and heaps belong to a residency working set");
        return Accept(g_backend->AddToResidencyWorkingSet(g_boundCommandList, resource), failure);
    }

    bool SetPipeline(const PipelineRef pipeline, Failure* const failure) noexcept
    {
        if (!RequireBoundCommandList(failure))
            return false;
        if (!pipeline || !IsResourceReferenceValid(ResourceRef(pipeline)))
            return Reject(failure, FailureCode::InvalidReference, "cannot bind an invalid pipeline");
        return Accept(g_backend->SetPipeline(g_boundCommandList, pipeline), failure);
    }

    bool SetupRenderTargets(const RenderTargetSetup& setup, Failure* const failure) noexcept
    {
        if (!RequireBoundCommandList(failure))
            return false;
        if (GetBoundCommandListType() != CommandListType::Default || setup.colorTargetCount > MaximumColorAttachments || (setup.colorTargetCount == 0 && !setup.depthStencilTarget.texture))
            return Reject(failure, FailureCode::InvalidArgument, "render targets require a graphics command list and a valid target count");
        for (u32 index = 0; index < setup.colorTargetCount; ++index)
        {
            const RenderTargetAttachment& target = setup.colorTargets[index];
            if (!target.texture || target.readOnly || !IsResourceReferenceValid(ResourceRef(target.texture)))
                return Reject(failure, FailureCode::InvalidReference, "render-target setup contains an invalid color target");
        }
        if (setup.depthStencilTarget.texture && !IsResourceReferenceValid(ResourceRef(setup.depthStencilTarget.texture)))
            return Reject(failure, FailureCode::InvalidReference, "render-target setup contains an invalid depth target");
        return Accept(g_backend->SetupRenderTargets(g_boundCommandList, setup), failure);
    }

    bool SetVariableRateShading(const VariableRateShadingState& state, Failure* const failure) noexcept
    {
        if (!RequireBoundCommandList(failure))
            return false;
        if (!g_capabilities.variableRateShading)
            return Reject(failure, FailureCode::Unsupported, "variable-rate shading is not supported");
        if (GetBoundCommandListType() != CommandListType::Default)
            return Reject(failure, FailureCode::InvalidCommandList, "variable-rate shading requires a graphics command list");
        if (!state.enabled)
            return Accept(g_backend->SetVariableRateShading(g_boundCommandList, state), failure);

        const VariableRateShadingCapabilities& capabilities = g_capabilities.variableRateShadingDetails;
        if (!capabilities.Supports(state.rate) || !capabilities.Supports(state.primitiveCombiner) || !capabilities.Supports(state.imageCombiner))
            return Reject(failure, FailureCode::Unsupported, "the requested shading rate or combiner is not supported");
        if (state.primitiveCombiner != ShadingRateCombiner::Passthrough && !capabilities.perPrimitive)
            return Reject(failure, FailureCode::Unsupported, "per-primitive shading rates are not supported");
        if (state.image)
        {
            if (capabilities.tier != VariableRateShadingTier::ShadingRateImage)
                return Reject(failure, FailureCode::Unsupported, "shading-rate images are not supported");
            if (!IsResourceReferenceValid(ResourceRef(state.image)))
                return Reject(failure, FailureCode::InvalidReference, "the shading-rate image is invalid");
        }
        else if (state.imageCombiner != ShadingRateCombiner::Passthrough)
            return Reject(failure, FailureCode::InvalidArgument, "an image combiner requires a shading-rate image");
        return Accept(g_backend->SetVariableRateShading(g_boundCommandList, state), failure);
    }

    bool SetViewport(const ViewportDesc& viewport, Failure* const failure) noexcept
    {
        if (!RequireBoundCommandList(failure))
            return false;
        if (GetBoundCommandListType() != CommandListType::Default || !(viewport.width > 0.0f) || !(viewport.height > 0.0f) || !(viewport.minimumDepth >= 0.0f) || !(viewport.maximumDepth <= 1.0f) ||
            viewport.minimumDepth > viewport.maximumDepth)
            return Reject(failure, FailureCode::InvalidArgument, "invalid graphics viewport");
        return Accept(g_backend->SetViewport(g_boundCommandList, viewport), failure);
    }

    bool SetScissors(const Rect& rect, Failure* const failure) noexcept
    {
        if (!RequireBoundCommandList(failure))
            return false;
        if (GetBoundCommandListType() != CommandListType::Default || rect.width <= 0 || rect.height <= 0)
            return Reject(failure, FailureCode::InvalidArgument, "invalid graphics scissor rectangle");
        return Accept(g_backend->SetScissors(g_boundCommandList, rect), failure);
    }

    bool BindVertexBuffers(const u32 startIndex, const containers::ArraySpan<const VertexBufferBinding> bindings, Failure* const failure) noexcept
    {
        if (!RequireBoundCommandList(failure))
            return false;
        if (GetBoundCommandListType() != CommandListType::Default || bindings.Data() == nullptr || bindings.Size() == 0 || startIndex >= MaximumVertexBindings ||
            bindings.Size() > MaximumVertexBindings - startIndex)
            return Reject(failure, FailureCode::InvalidArgument, "invalid vertex-buffer binding span");
        for (u32 index = 0; index < bindings.Size(); ++index)
            if (!bindings[index].buffer || bindings[index].binding != startIndex + index || !IsResourceReferenceValid(ResourceRef(bindings[index].buffer)))
                return Reject(failure, FailureCode::InvalidReference, "vertex-buffer span contains an invalid binding");
        return Accept(g_backend->BindVertexBuffers(g_boundCommandList, startIndex, bindings), failure);
    }

    bool BindIndexBuffer(const IndexBufferBinding& binding, Failure* const failure) noexcept
    {
        if (!RequireBoundCommandList(failure))
            return false;
        if (GetBoundCommandListType() != CommandListType::Default || !binding.buffer || !IsResourceReferenceValid(ResourceRef(binding.buffer)))
            return Reject(failure, FailureCode::InvalidReference, "invalid index-buffer binding");
        return Accept(g_backend->BindIndexBuffer(g_boundCommandList, binding), failure);
    }

    bool BindIndirectArguments(const BufferRef arguments, const BufferRef count, Failure* const failure) noexcept
    {
        if (!RequireBoundCommandList(failure))
            return false;
        if (!arguments || !IsResourceReferenceValid(ResourceRef(arguments)) || (count && !IsResourceReferenceValid(ResourceRef(count))))
            return Reject(failure, FailureCode::InvalidReference, "invalid indirect-argument buffer binding");
        return Accept(g_backend->BindIndirectArguments(g_boundCommandList, arguments, count), failure);
    }

    bool SetPushConstants(const void* const data, const u32 size, Failure* const failure) noexcept
    {
        if (!RequireBoundCommandList(failure))
            return false;
        if (data == nullptr || size == 0 || size > g_capabilities.maximumPushConstantBytes || (size & 3u) != 0)
            return Reject(failure, FailureCode::InvalidArgument, "invalid push-constant payload");
        return Accept(g_backend->SetPushConstants(g_boundCommandList, data, size), failure);
    }

    bool ClearColorTarget(const TextureRef target, const ColorValue& value, const SubresourceRange& range, const Rect* const rectangle, Failure* const failure) noexcept
    {
        if (!RequireBoundCommandList(failure))
            return false;
        if (!target.IsValid())
            return Reject(failure, FailureCode::InvalidReference, "invalid color target");
        if (rectangle != nullptr && (rectangle->x < 0 || rectangle->y < 0 || rectangle->width <= 0 || rectangle->height <= 0))
            return Reject(failure, FailureCode::InvalidArgument, "invalid color-clear rectangle");
        return Accept(g_backend->ClearColorTarget(g_boundCommandList, target, value, range, rectangle), failure);
    }

    bool ClearDepthTarget(const TextureRef target, const f32 depth, const SubresourceRange& range, const Rect* const rectangle, Failure* const failure) noexcept
    {
        if (!RequireBoundCommandList(failure))
            return false;
        if (!target.IsValid())
            return Reject(failure, FailureCode::InvalidReference, "invalid depth target");
        if (!(depth >= 0.0f && depth <= 1.0f))
            return Reject(failure, FailureCode::InvalidArgument, "depth clear value must be in [0, 1]");
        if (rectangle != nullptr && (rectangle->x < 0 || rectangle->y < 0 || rectangle->width <= 0 || rectangle->height <= 0))
            return Reject(failure, FailureCode::InvalidArgument, "invalid depth-clear rectangle");
        return Accept(g_backend->ClearDepthStencilTarget(g_boundCommandList, target, true, depth, false, 0, range, rectangle), failure);
    }

    bool ClearStencilTarget(const TextureRef target, const u8 stencil, const SubresourceRange& range, const Rect* const rectangle, Failure* const failure) noexcept
    {
        if (!RequireBoundCommandList(failure))
            return false;
        if (!target.IsValid())
            return Reject(failure, FailureCode::InvalidReference, "invalid stencil target");
        if (rectangle != nullptr && (rectangle->x < 0 || rectangle->y < 0 || rectangle->width <= 0 || rectangle->height <= 0))
            return Reject(failure, FailureCode::InvalidArgument, "invalid stencil-clear rectangle");
        return Accept(g_backend->ClearDepthStencilTarget(g_boundCommandList, target, false, 1.0f, true, stencil, range, rectangle), failure);
    }

    bool ClearDepthStencilTarget(const TextureRef target, const f32 depth, const u8 stencil, const SubresourceRange& range, const Rect* const rectangle, Failure* const failure) noexcept
    {
        if (!RequireBoundCommandList(failure))
            return false;
        if (!target.IsValid())
            return Reject(failure, FailureCode::InvalidReference, "invalid depth-stencil target");
        if (!(depth >= 0.0f && depth <= 1.0f))
            return Reject(failure, FailureCode::InvalidArgument, "depth clear value must be in [0, 1]");
        if (rectangle != nullptr && (rectangle->x < 0 || rectangle->y < 0 || rectangle->width <= 0 || rectangle->height <= 0))
            return Reject(failure, FailureCode::InvalidArgument, "invalid depth-stencil-clear rectangle");
        return Accept(g_backend->ClearDepthStencilTarget(g_boundCommandList, target, true, depth, true, stencil, range, rectangle), failure);
    }

    bool ClearTextureUav(const TextureRef texture, const ColorValue& value, const SubresourceRange& range, Failure* const failure) noexcept
    {
        if (!RequireBoundCommandList(failure))
            return false;
        if (!texture.IsValid())
            return Reject(failure, FailureCode::InvalidReference, "invalid texture UAV");
        return Accept(g_backend->ClearTextureUav(g_boundCommandList, texture, value, range), failure);
    }

    bool ClearTextureUav(const TextureRef texture, const u32 value, const SubresourceRange& range, Failure* const failure) noexcept
    {
        if (!RequireBoundCommandList(failure))
            return false;
        if (!texture.IsValid())
            return Reject(failure, FailureCode::InvalidReference, "invalid texture UAV");
        return Accept(g_backend->ClearTextureUav(g_boundCommandList, texture, value, range), failure);
    }

    bool ClearBufferUav(const BufferRef buffer, const u32 value, Failure* const failure) noexcept
    {
        if (!RequireBoundCommandList(failure))
            return false;
        if (!buffer.IsValid())
            return Reject(failure, FailureCode::InvalidReference, "invalid buffer UAV");
        return Accept(g_backend->ClearBufferUav(g_boundCommandList, buffer, value), failure);
    }

    bool DiscardTexture(const TextureRef texture, const SubresourceRange& range, Failure* const failure) noexcept
    {
        if (!RequireBoundCommandList(failure))
            return false;
        if (!texture.IsValid())
            return Reject(failure, FailureCode::InvalidReference, "invalid texture discard target");
        return Accept(g_backend->DiscardTexture(g_boundCommandList, texture, range), failure);
    }

    bool SetStencilRefValue(const u8 value, Failure* const failure) noexcept
    {
        if (!RequireBoundCommandList(failure))
            return false;
        return Accept(g_backend->SetStencilRefValue(g_boundCommandList, value), failure);
    }

    bool SetBlendFactor(const ColorValue& value, Failure* const failure) noexcept
    {
        if (!RequireBoundCommandList(failure))
            return false;
        return Accept(g_backend->SetBlendFactor(g_boundCommandList, value), failure);
    }

    bool BeginGpuEvent(const char* const name, Failure* const failure) noexcept
    {
        if (!RequireBoundCommandList(failure))
            return false;
        if (name == nullptr || name[0] == '\0')
            return Reject(failure, FailureCode::InvalidArgument, "GPU event name is empty");
        return Accept(g_backend->BeginGpuEvent(g_boundCommandList, name), failure);
    }

    bool EndGpuEvent(Failure* const failure) noexcept
    {
        if (!RequireBoundCommandList(failure))
            return false;
        return Accept(g_backend->EndGpuEvent(g_boundCommandList), failure);
    }

    bool SetGpuMarker(const char* const name, Failure* const failure) noexcept
    {
        if (!RequireBoundCommandList(failure))
            return false;
        if (name == nullptr || name[0] == '\0')
            return Reject(failure, FailureCode::InvalidArgument, "GPU marker name is empty");
        return Accept(g_backend->SetGpuMarker(g_boundCommandList, name), failure);
    }

    bool DrawPrimitive(const DrawArguments& arguments, Failure* const failure) noexcept
    {
        if (!RequireBoundCommandList(failure))
            return false;
        if (GetBoundCommandListType() != CommandListType::Default || arguments.vertexCount == 0 || arguments.instanceCount == 0)
            return Reject(failure, FailureCode::InvalidArgument, "invalid direct draw arguments");
        return Accept(g_backend->DrawPrimitive(g_boundCommandList, arguments), failure);
    }

    bool DrawIndexedPrimitive(const DrawIndexedArguments& arguments, Failure* const failure) noexcept
    {
        if (!RequireBoundCommandList(failure))
            return false;
        if (GetBoundCommandListType() != CommandListType::Default || arguments.indexCount == 0 || arguments.instanceCount == 0)
            return Reject(failure, FailureCode::InvalidArgument, "invalid indexed draw arguments");
        return Accept(g_backend->DrawIndexedPrimitive(g_boundCommandList, arguments), failure);
    }

    bool DrawPrimitiveIndirect(const u64 argumentsOffset, const u32 commandCount, Failure* const failure) noexcept
    {
        if (!RequireBoundCommandList(failure))
            return false;
        if (GetBoundCommandListType() != CommandListType::Default || commandCount == 0 || argumentsOffset > 0xffffffffu || (argumentsOffset & 3u) != 0)
            return Reject(failure, FailureCode::InvalidArgument, "invalid indirect draw arguments");
        return Accept(g_backend->DrawPrimitiveIndirect(g_boundCommandList, argumentsOffset, commandCount), failure);
    }

    bool DrawIndexedPrimitiveIndirect(const u64 argumentsOffset, const u32 commandCount, Failure* const failure) noexcept
    {
        if (!RequireBoundCommandList(failure))
            return false;
        if (GetBoundCommandListType() != CommandListType::Default || commandCount == 0 || argumentsOffset > 0xffffffffu || (argumentsOffset & 3u) != 0)
            return Reject(failure, FailureCode::InvalidArgument, "invalid indexed indirect draw arguments");
        return Accept(g_backend->DrawIndexedPrimitiveIndirect(g_boundCommandList, argumentsOffset, commandCount), failure);
    }

    bool DrawIndexedPrimitiveIndirectCount(const u64 argumentsOffset, const u64 countOffset, const u32 maximumCommandCount, Failure* const failure) noexcept
    {
        if (!RequireBoundCommandList(failure))
            return false;
        if (GetBoundCommandListType() != CommandListType::Default || maximumCommandCount == 0 || argumentsOffset > 0xffffffffu || countOffset > 0xffffffffu || (argumentsOffset & 3u) != 0 ||
            (countOffset & 3u) != 0)
            return Reject(failure, FailureCode::InvalidArgument, "invalid counted indirect draw arguments");
        return Accept(g_backend->DrawIndexedPrimitiveIndirectCount(g_boundCommandList, argumentsOffset, countOffset, maximumCommandCount), failure);
    }

    bool DispatchCompute(const u32 groupCountX, const u32 groupCountY, const u32 groupCountZ, Failure* const failure) noexcept
    {
        if (!RequireBoundCommandList(failure))
            return false;
        const CommandListType type = GetBoundCommandListType();
        if ((type != CommandListType::Default && type != CommandListType::Compute) || groupCountX == 0 || groupCountY == 0 || groupCountZ == 0)
            return Reject(failure, FailureCode::InvalidArgument, "invalid compute dispatch arguments");
        return Accept(g_backend->DispatchCompute(g_boundCommandList, groupCountX, groupCountY, groupCountZ), failure);
    }

    bool DispatchIndirectCompute(const u64 argumentsOffset, Failure* const failure) noexcept
    {
        if (!RequireBoundCommandList(failure))
            return false;
        const CommandListType type = GetBoundCommandListType();
        if ((type != CommandListType::Default && type != CommandListType::Compute) || argumentsOffset > 0xffffffffu || (argumentsOffset & 3u) != 0)
            return Reject(failure, FailureCode::InvalidArgument, "invalid indirect dispatch arguments");
        return Accept(g_backend->DispatchIndirectCompute(g_boundCommandList, argumentsOffset), failure);
    }

    bool BuildBottomLevelAccelerationStructure(const AccelerationStructureRef destination, const containers::ArraySpan<const RayTracingGeometryDesc> geometries, const AccelerationStructureBuildMode mode,
                                               Failure* const failure) noexcept
    {
        if (!RequireBoundCommandList(failure))
            return false;
        if (!g_capabilities.rayTracing)
            return Reject(failure, FailureCode::Unsupported, "ray-tracing acceleration structures are not supported");
        if (!destination || geometries.Size() == 0)
            return Reject(failure, FailureCode::InvalidArgument, "invalid bottom-level acceleration-structure build");
        return Accept(g_backend->BuildBottomLevelAccelerationStructure(g_boundCommandList, destination, geometries, mode), failure);
    }

    bool BuildTopLevelAccelerationStructure(const AccelerationStructureRef destination, const containers::ArraySpan<const RayTracingInstanceDesc> instances, const AccelerationStructureBuildMode mode,
                                            Failure* const failure) noexcept
    {
        if (!RequireBoundCommandList(failure))
            return false;
        if (!g_capabilities.rayTracing)
            return Reject(failure, FailureCode::Unsupported, "ray-tracing acceleration structures are not supported");
        if (!destination || instances.Size() == 0)
            return Reject(failure, FailureCode::InvalidArgument, "invalid top-level acceleration-structure build");
        return Accept(g_backend->BuildTopLevelAccelerationStructure(g_boundCommandList, destination, instances, mode), failure);
    }

    bool BuildTopLevelAccelerationStructureIndirect(const AccelerationStructureRef destination, const BufferRef nativeInstanceBuffer, const u64 offset, const u32 instanceCount,
                                                    const AccelerationStructureBuildMode mode, Failure* const failure) noexcept
    {
        if (!RequireBoundCommandList(failure))
            return false;
        if (!g_capabilities.rayTracing)
            return Reject(failure, FailureCode::Unsupported, "ray-tracing acceleration structures are not supported");
        if (!destination || !nativeInstanceBuffer || instanceCount == 0)
            return Reject(failure, FailureCode::InvalidArgument, "invalid GPU-driven top-level acceleration-structure build");
        return Accept(g_backend->BuildTopLevelAccelerationStructureIndirect(g_boundCommandList, destination, nativeInstanceBuffer, offset, instanceCount, mode), failure);
    }

    bool CopyAccelerationStructure(const AccelerationStructureRef destination, const AccelerationStructureRef source, const AccelerationStructureCopyMode mode, Failure* const failure) noexcept
    {
        if (!RequireBoundCommandList(failure))
            return false;
        if (!g_capabilities.rayTracing)
            return Reject(failure, FailureCode::Unsupported, "ray-tracing acceleration structures are not supported");
        if (!destination || !source || destination == source)
            return Reject(failure, FailureCode::InvalidArgument, "invalid acceleration-structure copy");
        return Accept(g_backend->CopyAccelerationStructure(g_boundCommandList, destination, source, mode), failure);
    }

    bool WriteAccelerationStructureCompactedSize(const AccelerationStructureRef accelerationStructure, const QueryPoolRef queryPool, const u32 queryIndex, Failure* const failure) noexcept
    {
        if (!RequireBoundCommandList(failure))
            return false;
        if (!g_capabilities.rayTracing)
            return Reject(failure, FailureCode::Unsupported, "ray-tracing acceleration structures are not supported");
        if (!accelerationStructure || !queryPool)
            return Reject(failure, FailureCode::InvalidReference, "invalid acceleration-structure compaction query");
        return Accept(g_backend->WriteAccelerationStructureCompactedSize(g_boundCommandList, accelerationStructure, queryPool, queryIndex), failure);
    }

    bool DispatchRays(const ShaderTableRef shaderTable, const DispatchRaysArguments& arguments, Failure* const failure) noexcept
    {
        if (!RequireBoundCommandList(failure))
            return false;
        if (!g_capabilities.rayTracingPipeline)
            return Reject(failure, FailureCode::Unsupported, "ray-tracing dispatch is not supported");
        if (!shaderTable || arguments.width == 0 || arguments.height == 0 || arguments.depth == 0)
            return Reject(failure, FailureCode::InvalidArgument, "invalid ray dispatch arguments");
        return Accept(g_backend->DispatchRays(g_boundCommandList, shaderTable, arguments), failure);
    }

    bool WriteBuffer(const BufferRef buffer, const void* const data, const u64 size, const u64 destinationOffset, Failure* const failure) noexcept
    {
        if (!RequireBoundCommandList(failure))
            return false;
        if (!buffer.IsValid() || data == nullptr || size == 0 || destinationOffset + size < destinationOffset)
            return Reject(failure, FailureCode::InvalidArgument, "invalid buffer upload");
        return Accept(g_backend->WriteBuffer(g_boundCommandList, buffer, data, size, destinationOffset), failure);
    }

    bool WriteTexture(const TextureRef texture, const TextureSubresourceData& data, Failure* const failure) noexcept
    {
        if (!RequireBoundCommandList(failure))
            return false;
        if (!texture.IsValid() || data.data == nullptr || data.size == 0 || data.rowPitch == 0)
            return Reject(failure, FailureCode::InvalidArgument, "invalid texture upload");
        return Accept(g_backend->WriteTexture(g_boundCommandList, texture, data), failure);
    }

    bool CopyBuffer(const BufferRef destination, const u64 destinationOffset, const BufferRef source, const u64 sourceOffset, const u64 size, Failure* const failure) noexcept
    {
        if (!RequireBoundCommandList(failure))
            return false;
        if (!destination.IsValid() || !source.IsValid() || size == 0 || destinationOffset + size < destinationOffset || sourceOffset + size < sourceOffset)
            return Reject(failure, FailureCode::InvalidArgument, "invalid buffer copy");
        return Accept(g_backend->CopyBuffer(g_boundCommandList, destination, destinationOffset, source, sourceOffset, size), failure);
    }

    bool CopyTexture(const TextureRef destination, const TextureRef source, const TextureCopyRegion& region, Failure* const failure) noexcept
    {
        if (!RequireBoundCommandList(failure))
            return false;
        if (!destination.IsValid() || !source.IsValid())
            return Reject(failure, FailureCode::InvalidReference, "texture copy contains an invalid resource reference");
        const bool anyZero = region.extent.width == 0 || region.extent.height == 0 || region.extent.depth == 0;
        const bool allZero = region.extent.width == 0 && region.extent.height == 0 && region.extent.depth == 0;
        if (anyZero && !allZero)
            return Reject(failure, FailureCode::InvalidArgument, "texture copy extent must be complete or entirely implicit");
        return Accept(g_backend->CopyTexture(g_boundCommandList, destination, source, region), failure);
    }

    bool ResolveTexture(const TextureRef destination, const TextureRef source, const TextureResolveRegion& region, Failure* const failure) noexcept
    {
        if (!RequireBoundCommandList(failure))
            return false;
        if (GetBoundCommandListType() != CommandListType::Default)
            return Reject(failure, FailureCode::InvalidCommandList, "texture resolve requires a graphics command list");
        if (!destination.IsValid() || !source.IsValid())
            return Reject(failure, FailureCode::InvalidReference, "texture resolve contains an invalid resource reference");
        return Accept(g_backend->ResolveTexture(g_boundCommandList, destination, source, region), failure);
    }

    TextureReadbackRef RequestTextureReadback(const TextureRef source, const TextureReadbackRegion& region, Failure* const failure) noexcept
    {
        if (!RequireBoundCommandList(failure))
            return {};
        if (!source.IsValid())
        {
            Reject(failure, FailureCode::InvalidReference, "texture readback source is invalid");
            return {};
        }
        const bool anyZero = region.extent.width == 0 || region.extent.height == 0 || region.extent.depth == 0;
        const bool allZero = region.extent.width == 0 && region.extent.height == 0 && region.extent.depth == 0;
        if (anyZero && !allZero)
        {
            Reject(failure, FailureCode::InvalidArgument, "texture readback extent must be complete or entirely implicit");
            return {};
        }
        TextureReadbackRef readback;
        return Accept(g_backend->RequestTextureReadback(g_boundCommandList, source, region, readback), failure) ? readback : TextureReadbackRef{};
    }

    bool GetTextureReadbackInfo(const TextureReadbackRef readback, TextureReadbackInfo& info, Failure* const failure) noexcept
    {
        info = {};
        if (g_backend == nullptr)
            return Reject(failure, FailureCode::NotInitialized, "RHI is not initialized");
        if (!readback.IsValid())
            return Reject(failure, FailureCode::InvalidReference, "texture readback reference is invalid");
        return Accept(g_backend->GetTextureReadbackInfo(readback, info), failure);
    }

    bool MapTextureReadback(const TextureReadbackRef readback, TextureReadbackMapping& mapping, Failure* const failure) noexcept
    {
        mapping = {};
        if (g_backend == nullptr)
            return Reject(failure, FailureCode::NotInitialized, "RHI is not initialized");
        if (!readback.IsValid())
            return Reject(failure, FailureCode::InvalidReference, "texture readback reference is invalid");
        return Accept(g_backend->MapTextureReadback(readback, mapping), failure);
    }

    bool UnmapTextureReadback(const TextureReadbackRef readback, Failure* const failure) noexcept
    {
        if (g_backend == nullptr)
            return Reject(failure, FailureCode::NotInitialized, "RHI is not initialized");
        if (!readback.IsValid())
            return Reject(failure, FailureCode::InvalidReference, "texture readback reference is invalid");
        return Accept(g_backend->UnmapTextureReadback(readback), failure);
    }

    void* LockBuffer(const BufferRef buffer, const u64 offset, const u64 size, Failure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!RequireBackend(failure))
            return nullptr;
        if (!buffer.IsValid() || size == 0 || offset + size < offset)
        {
            Reject(failure, FailureCode::InvalidArgument, "invalid buffer lock range");
            return nullptr;
        }
        void* data = nullptr;
        if (!Accept(g_backend->LockBuffer(buffer, offset, size, data), failure))
            return nullptr;
        if (data == nullptr)
        {
            Reject(failure, FailureCode::BackendFailure, "RHI backend returned a null buffer mapping");
            return nullptr;
        }
        return data;
    }

    void UnlockBuffer(const BufferRef buffer) noexcept
    {
        VG_ASSERT_MSG(g_backend != nullptr, "cannot unlock a buffer after RHI shutdown");
        VG_ASSERT_MSG(buffer.IsValid(), "cannot unlock an invalid buffer");
        if (g_backend != nullptr && buffer.IsValid())
            g_backend->UnlockBuffer(buffer);
    }

    bool AddCommandListWait(const GpuFence fence, Failure* const failure) noexcept
    {
        const bool bound = RequireBoundCommandList(failure);
        if (!bound)
            return false;
        if (!fence.IsValid() || (fence.queue != QueueType::Graphics && fence.queue != QueueType::Compute && fence.queue != QueueType::Copy))
            return Reject(failure, FailureCode::InvalidArgument, "incoming queue wait requires a valid submitted fence");
        const BackendStatus status = g_backend->AddCommandListWait(g_boundCommandList, fence);
        return Accept(status, failure);
    }

    bool SeedCommandListStates(const containers::ArraySpan<const CommandListEntryState> entries, Failure* const failure) noexcept
    {
        if (!RequireBoundCommandList(failure))
            return false;
        for (const CommandListEntryState& entry : entries)
            if (!entry.resource.IsValid() || (entry.resource.GetKind() != ResourceKind::Texture && entry.resource.GetKind() != ResourceKind::Buffer) || !IsKnownResourceState(entry.state))
                return Reject(failure, FailureCode::InvalidArgument, "command-list entry state requires a texture or buffer and an explicit known state");
        return Accept(g_backend->SeedCommandListStates(g_boundCommandList, entries), failure);
    }

    bool TransitionTexture(const TextureRef texture, const ResourceState before, const ResourceState after, const SubresourceRange& range, Failure* const failure) noexcept
    {
        if (!RequireBoundCommandList(failure))
            return false;
        if (!texture.IsValid() || !IsKnownResourceState(before, true) || !IsKnownResourceState(after))
            return Reject(failure, FailureCode::InvalidArgument, "invalid texture transition");
        return Accept(g_backend->TransitionTexture(g_boundCommandList, texture, before, after, range), failure);
    }

    bool TransitionBuffer(const BufferRef buffer, const ResourceState before, const ResourceState after, Failure* const failure) noexcept
    {
        if (!RequireBoundCommandList(failure))
            return false;
        if (!buffer.IsValid() || !IsKnownResourceState(before, true) || !IsKnownResourceState(after))
            return Reject(failure, FailureCode::InvalidArgument, "invalid buffer transition");
        return Accept(g_backend->TransitionBuffer(g_boundCommandList, buffer, before, after), failure);
    }

    bool BarrierTextureUav(const TextureRef texture, Failure* const failure) noexcept
    {
        if (!RequireBoundCommandList(failure))
            return false;
        if (!texture.IsValid())
            return Reject(failure, FailureCode::InvalidReference, "invalid texture UAV barrier");
        return Accept(g_backend->BarrierTextureUav(g_boundCommandList, texture), failure);
    }
    bool BarrierBufferUav(const BufferRef buffer, Failure* const failure) noexcept
    {
        if (!RequireBoundCommandList(failure))
            return false;
        if (!buffer.IsValid())
            return Reject(failure, FailureCode::InvalidReference, "invalid buffer UAV barrier");
        return Accept(g_backend->BarrierBufferUav(g_boundCommandList, buffer), failure);
    }
    bool ActivateAliasedResource(const ResourceRef destination, const containers::ArraySpan<const ResourceRef> predecessors, Failure* const failure) noexcept
    {
        if (!RequireBoundCommandList(failure))
            return false;
        if (!destination.IsValid() || (destination.GetKind() != ResourceKind::Texture && destination.GetKind() != ResourceKind::Buffer))
            return Reject(failure, FailureCode::InvalidReference, "alias activation requires a texture or buffer destination");
        if (predecessors.Size() == 0)
            return Reject(failure, FailureCode::InvalidArgument, "alias activation requires the complete predecessor set");
        for (const ResourceRef predecessor : predecessors)
            if (!predecessor.IsValid() || (predecessor.GetKind() != ResourceKind::Texture && predecessor.GetKind() != ResourceKind::Buffer))
                return Reject(failure, FailureCode::InvalidReference, "alias activation contains an invalid predecessor resource");
        return Accept(g_backend->ActivateAliasedResource(g_boundCommandList, destination, predecessors), failure);
    }
    bool FlushPendingBarriers(Failure* const failure) noexcept
    {
        if (!RequireBoundCommandList(failure))
            return false;
        return Accept(g_backend->FlushPendingBarriers(g_boundCommandList), failure);
    }
    bool MakeStateSafeToRetire(const TextureRef texture, Failure* const failure) noexcept
    {
        if (!RequireBoundCommandList(failure))
            return false;
        if (!texture.IsValid())
            return Reject(failure, FailureCode::InvalidReference, "invalid texture retirement transition");
        return Accept(g_backend->MakeStateSafeToRetire(g_boundCommandList, texture), failure);
    }
    bool MakeStateSafeToRetire(const BufferRef buffer, Failure* const failure) noexcept
    {
        if (!RequireBoundCommandList(failure))
            return false;
        if (!buffer.IsValid())
            return Reject(failure, FailureCode::InvalidReference, "invalid buffer retirement transition");
        return Accept(g_backend->MakeStateSafeToRetire(g_boundCommandList, buffer), failure);
    }

    SwapChainRef CreateSwapChainWithBackBuffer(const SwapChainDesc& desc, Failure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!RequireBackend(failure))
            return {};
        if (desc.surface.kind == PresentationSurfaceKind::None || desc.surface.nativeWindow == nullptr || desc.width == 0 || desc.height == 0 || desc.bufferCount < 2 ||
            desc.bufferCount > MaximumSwapChainBuffers || desc.format == Format::Unknown ||
            (desc.frameLatency.enabled && (desc.frameLatency.maximumFramesInFlight == 0 || desc.frameLatency.maximumFramesInFlight > desc.bufferCount || desc.frameLatency.waitTimeoutMilliseconds == 0)))
        {
            Reject(failure, FailureCode::InvalidArgument, "invalid swap-chain descriptor");
            return {};
        }
        const SwapChainRef swapChain = g_backend->CreateSwapChainWithBackBuffer(desc);
        if (!swapChain.IsValid())
            Reject(failure, FailureCode::BackendFailure, "RHI backend failed to create swap chain");
        return swapChain;
    }
    bool ResizeBackbuffer(const u32 width, const u32 height, const SwapChainRef swapChain, Failure* const failure) noexcept
    {
        if (!RequireBackend(failure))
            return false;
        if (!swapChain.IsValid() || width == 0 || height == 0)
            return Reject(failure, FailureCode::InvalidArgument, "invalid swap-chain resize");
        return Accept(g_backend->ResizeBackbuffer(swapChain, width, height), failure);
    }
    bool SetSwapChainPresentParameters(const SwapChainRef swapChain, const PresentParameters& parameters, Failure* const failure) noexcept
    {
        if (!RequireBackend(failure))
            return false;
        if (!swapChain.IsValid())
            return Reject(failure, FailureCode::InvalidReference, "invalid swap chain");
        if (parameters.mode == PresentMode::Mailbox)
            return Reject(failure, FailureCode::Unsupported, "mailbox presentation is not supported by this RHI backend");
        if ((parameters.mode == PresentMode::Immediate && parameters.synchronizationInterval != 0) ||
            (parameters.mode == PresentMode::Fifo && (parameters.synchronizationInterval == 0 || parameters.synchronizationInterval > 4 || parameters.allowTearing)))
            return Reject(failure, FailureCode::InvalidArgument, "invalid swap-chain presentation parameters");
        return Accept(g_backend->SetSwapChainPresentParameters(swapChain, parameters), failure);
    }

    bool AcquireBackBuffer(const SwapChainRef swapChain, AcquiredBackBuffer& acquisition, Failure* const failure) noexcept
    {
        acquisition = {};
        if (!RequireBackend(failure))
            return false;
        if (!swapChain.IsValid())
            return Reject(failure, FailureCode::InvalidReference, "invalid swap chain");
        return Accept(g_backend->AcquireBackBuffer(swapChain, acquisition), failure);
    }

    bool AbandonBackBuffer(const AcquiredBackBuffer& acquisition, Failure* const failure) noexcept
    {
        if (!RequireBackend(failure))
            return false;
        if (!acquisition.IsValid())
            return Reject(failure, FailureCode::InvalidReference, "invalid back-buffer acquisition");
        return Accept(g_backend->AbandonBackBuffer(acquisition), failure);
    }

    bool TransitionSwapChainPresent(const AcquiredBackBuffer& acquisition, Failure* const failure) noexcept
    {
        if (!RequireBoundCommandList(failure))
            return false;
        if (!acquisition.IsValid())
            return Reject(failure, FailureCode::InvalidReference, "invalid back-buffer acquisition");
        return Accept(g_backend->TransitionSwapChainPresent(g_boundCommandList, acquisition), failure);
    }
    bool Present(const AcquiredBackBuffer& acquisition, Failure* const failure) noexcept
    {
        if (!RequireBackend(failure))
            return false;
        if (!acquisition.IsValid())
            return Reject(failure, FailureCode::InvalidReference, "invalid back-buffer acquisition");
        return Accept(g_backend->Present(acquisition), failure);
    }
    SwapChainStats GetSwapChainStats(const SwapChainRef swapChain) noexcept
    {
        return g_backend != nullptr && swapChain.IsValid() ? g_backend->GetSwapChainStats(swapChain) : SwapChainStats{};
    }
    void SetResourceDebugName(const TextureRef texture, const char* const name) noexcept
    {
        if (g_backend != nullptr && texture.IsValid())
            g_backend->SetResourceDebugName(texture, name);
    }
    void SetResourceDebugName(const TextureReadbackRef readback, const char* const name) noexcept
    {
        if (g_backend != nullptr && readback.IsValid())
            g_backend->SetResourceDebugName(readback, name);
    }
    void SetResourceDebugName(const BufferRef buffer, const char* const name) noexcept
    {
        if (g_backend != nullptr && buffer.IsValid())
            g_backend->SetResourceDebugName(buffer, name);
    }
    void SetResourceDebugName(const HeapRef heap, const char* const name) noexcept
    {
        if (g_backend != nullptr && heap.IsValid())
            g_backend->SetResourceDebugName(heap, name);
    }
    void SetResourceDebugName(const SamplerStateRef samplerState, const char* const name) noexcept
    {
        if (g_backend != nullptr && samplerState.IsValid())
            g_backend->SetResourceDebugName(samplerState, name);
    }
    void SetResourceDebugName(const ShaderRef shader, const char* const name) noexcept
    {
        if (g_backend != nullptr && shader.IsValid())
            g_backend->SetResourceDebugName(shader, name);
    }
    void SetResourceDebugName(const VertexLayoutRef vertexLayout, const char* const name) noexcept
    {
        if (g_backend != nullptr && vertexLayout.IsValid())
            g_backend->SetResourceDebugName(vertexLayout, name);
    }
    void SetResourceDebugName(const PipelineRef pipeline, const char* const name) noexcept
    {
        if (g_backend != nullptr && pipeline.IsValid())
            g_backend->SetResourceDebugName(pipeline, name);
    }
    void SetResourceDebugName(const AccelerationStructureRef accelerationStructure, const char* const name) noexcept
    {
        if (g_backend != nullptr && accelerationStructure.IsValid())
            g_backend->SetResourceDebugName(accelerationStructure, name);
    }
    void SetResourceDebugName(const ShaderTableRef shaderTable, const char* const name) noexcept
    {
        if (g_backend != nullptr && shaderTable.IsValid())
            g_backend->SetResourceDebugName(shaderTable, name);
    }
    void SetResourceDebugName(const QueryPoolRef queryPool, const char* const name) noexcept
    {
        if (g_backend != nullptr && queryPool.IsValid())
            g_backend->SetResourceDebugName(queryPool, name);
    }
    void SetResourceDebugName(const CommandListRef commandList, const char* const name) noexcept
    {
        if (g_backend != nullptr && commandList.IsValid())
            g_backend->SetResourceDebugName(commandList, name);
    }
    void SetResourceDebugName(const SwapChainRef swapChain, const char* const name) noexcept
    {
        if (g_backend != nullptr && swapChain.IsValid())
            g_backend->SetResourceDebugName(swapChain, name);
    }
} // namespace vanguard::rhi
