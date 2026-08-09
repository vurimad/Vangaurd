#include <vanguard/rhi/rhi.hpp>
#include <vanguard/system/assert.hpp>

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

    namespace
    {
        IBackend* g_backend = nullptr;
        Capabilities g_capabilities{};
        thread_local CommandListRef g_boundCommandList{};

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
            return g_boundCommandList.IsValid() ||
                   Reject(failure, FailureCode::NoBoundCommandList, "RHI command recording requires a bound command list");
        }

        bool IsPowerOfTwo(const u64 value) noexcept
        {
            return value != 0 && (value & (value - 1)) == 0;
        }

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
            if (type == BindingType::TextureUnorderedAccess || type == BindingType::TypedBufferUnorderedAccess ||
                type == BindingType::StructuredBufferUnorderedAccess || type == BindingType::ByteAddressBufferUnorderedAccess)
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
            const u64 sliceBytes = blockRows == 0 ? 0 : upload.rowPitch * (blockRows - 1u) + minimumRowBytes;
            if (sliceBytes < minimumRowBytes)
                return false;
            if (mipDepth == 1)
                return upload.size >= sliceBytes;
            if (upload.depthPitch < sliceBytes)
                return false;
            const u64 requiredBytes = upload.depthPitch * (mipDepth - 1u) + sliceBytes;
            return requiredBytes >= sliceBytes && upload.size >= requiredBytes;
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

        Capabilities capabilities{};
        const BackendStatus status = backend.Initialize(params, capabilities);
        if (!Accept(status, failure))
            return false;
        if (capabilities.backend == BackendKind::Unknown || !IsPowerOfTwo(capabilities.uploadBufferAlignment) ||
            !IsPowerOfTwo(capabilities.constantBufferAlignment) ||
            capabilities.bindlessResources != (capabilities.descriptorIndexing && capabilities.maximumBindlessResources != 0) ||
            capabilities.bindlessSamplers != (capabilities.bindlessResources && capabilities.maximumBindlessSamplers != 0))
        {
            static_cast<void>(backend.Shutdown());
            return Reject(failure, FailureCode::BackendFailure, "RHI backend returned an invalid capability contract");
        }

        g_capabilities = capabilities;
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

    TextureRef CreateTexture(const TextureDesc& desc, const TextureInitData& initialData, Failure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!RequireBackend(failure))
            return {};
        if (desc.extent.width == 0 || desc.extent.height == 0 || desc.extent.depth == 0 || desc.format == Format::Unknown ||
            desc.mipCount == 0 || desc.mipCount > 32 || desc.arraySize == 0 || desc.sampleCount == 0 ||
            (initialData.subresources == nullptr) != (initialData.subresourceCount == 0) ||
            initialData.subresourceCount > MaximumTextureSubresourcesPerUpload ||
            (desc.virtualResource && initialData.subresourceCount != 0) || (desc.sampleCount > 1 && initialData.subresourceCount != 0))
        {
            Reject(failure, FailureCode::InvalidArgument, "invalid texture descriptor or initial data");
            return {};
        }
        const bool dimensionValid = (desc.dimension == TextureDimension::Texture1D && desc.extent.height == 1 && desc.extent.depth == 1) ||
                                    (desc.dimension == TextureDimension::Texture2D && desc.extent.depth == 1) ||
                                    (desc.dimension == TextureDimension::Texture3D && desc.arraySize == 1) ||
                                    (desc.dimension == TextureDimension::TextureCube && desc.extent.width == desc.extent.height &&
                                     desc.extent.depth == 1 && desc.arraySize >= 6 && desc.arraySize % 6 == 0);
        const bool samplesValid = desc.sampleCount == 1 || desc.sampleCount == 2 || desc.sampleCount == 4 || desc.sampleCount == 8;
        if (!dimensionValid || !samplesValid ||
            (desc.sampleCount > 1 && (desc.dimension != TextureDimension::Texture2D || desc.mipCount != 1)))
        {
            Reject(failure, FailureCode::InvalidArgument, "texture dimensions, array slices or sample count are inconsistent");
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
                if (initialData.subresources[previous].mipLevel == subresource.mipLevel &&
                    initialData.subresources[previous].arraySlice == subresource.arraySlice)
                {
                    Reject(failure, FailureCode::InvalidArgument, "duplicate texture subresource upload");
                    return {};
                }
            }
        }
        const TextureRef texture = g_backend->CreateTexture(desc, initialData);
        if (!texture.IsValid())
            Reject(failure, FailureCode::BackendFailure, "RHI backend failed to create texture");
        return texture;
    }

    BufferRef CreateBuffer(const BufferDesc& desc, const BufferInitData& initialData, Failure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!RequireBackend(failure))
            return {};
        if (desc.size == 0 || (initialData.data == nullptr) != (initialData.size == 0) || initialData.size > desc.size ||
            (desc.virtualResource && initialData.data != nullptr))
        {
            Reject(failure, FailureCode::InvalidArgument, "invalid buffer descriptor or initial data");
            return {};
        }
        const BufferRef buffer = g_backend->CreateBuffer(desc, initialData);
        if (!buffer.IsValid())
            Reject(failure, FailureCode::BackendFailure, "RHI backend failed to create buffer");
        return buffer;
    }

    HeapRef CreateHeap(const HeapDesc& desc, Failure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!RequireBackend(failure))
            return {};
        if (desc.size == 0 || !IsPowerOfTwo(desc.alignment))
        {
            Reject(failure, FailureCode::InvalidArgument, "invalid GPU heap descriptor");
            return {};
        }
        const HeapRef heap = g_backend->CreateHeap(desc);
        if (!heap.IsValid())
            Reject(failure, FailureCode::BackendFailure, "RHI backend failed to create heap");
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
                (entry.type == BindingType::PushConstants ? entry.arrayCount > g_capabilities.maximumPushConstantBytes
                                                          : entry.arrayCount > MaximumFixedBindingArraySize) ||
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
                const u64 previousEnd =
                    static_cast<u64>(previous.slot) + (previous.type == BindingType::PushConstants ? 1u : previous.arrayCount);
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
        if (desc.capacity == 0 || desc.capacity > maximumCapacity || desc.visibility == 0 ||
            (desc.visibility & ~((1u << static_cast<u32>(ShaderStage::Count)) - 1u)) != 0 || desc.kind > DescriptorDomainKind::Samplers)
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

    bool WriteDescriptor(const DescriptorDomainRef domain, const DescriptorHandle descriptor, const TextureRef texture,
                         const BindingType type, const TextureViewDesc& view, Failure* const failure) noexcept
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

    bool WriteDescriptor(const DescriptorDomainRef domain, const DescriptorHandle descriptor, const BufferRef buffer,
                         const BindingType type, const BufferViewDesc& view, Failure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!RequireBackend(failure))
            return false;
        if (!domain.IsValid() || !descriptor.IsValid() || !buffer.IsValid())
            return Reject(failure, FailureCode::InvalidReference, "buffer descriptor write contains an invalid reference");
        const bool compatible = type == BindingType::ConstantBuffer || type == BindingType::TypedBufferShaderResource ||
                                type == BindingType::TypedBufferUnorderedAccess || type == BindingType::StructuredBufferShaderResource ||
                                type == BindingType::StructuredBufferUnorderedAccess ||
                                type == BindingType::ByteAddressBufferShaderResource ||
                                type == BindingType::ByteAddressBufferUnorderedAccess;
        if (!compatible)
            return Reject(failure, FailureCode::InvalidArgument, "buffer descriptor write has an incompatible descriptor type");
        return Accept(g_backend->WriteDescriptor(domain, descriptor, buffer, type, view), failure);
    }

    bool WriteDescriptor(const DescriptorDomainRef domain, const DescriptorHandle descriptor, const SamplerStateRef sampler,
                         Failure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!RequireBackend(failure))
            return false;
        if (!domain.IsValid() || !descriptor.IsValid() || !sampler.IsValid())
            return Reject(failure, FailureCode::InvalidReference, "sampler descriptor write contains an invalid reference");
        return Accept(g_backend->WriteDescriptor(domain, descriptor, sampler), failure);
    }

    bool RetireDescriptor(const DescriptorDomainRef domain, const DescriptorHandle descriptor, const DescriptorRetirement& retirement,
                          Failure* const failure) noexcept
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
        if (desc.bytecode == nullptr || desc.bytecodeSize == 0 || desc.entryPoint == nullptr || desc.entryPoint[0] == '\0')
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
        if (desc.bindings == nullptr || desc.bindingCount == 0 || desc.bindingCount > MaximumVertexBindings || desc.attributes == nullptr ||
            desc.attributeCount == 0 || desc.attributeCount > MaximumVertexAttributes)
        {
            Reject(failure, FailureCode::InvalidArgument, "invalid vertex-layout descriptor");
            return {};
        }
        for (u32 bindingIndex = 0; bindingIndex < desc.bindingCount; ++bindingIndex)
        {
            const VertexBindingDesc& binding = desc.bindings[bindingIndex];
            if (binding.stride == 0 || binding.binding >= MaximumVertexBindings ||
                (binding.inputRate == VertexInputRate::PerInstance && binding.instanceStepRate == 0))
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
            if (!knownBinding || attribute.format == Format::Unknown || semanticLength == 0 ||
                semanticLength == MaximumVertexSemanticNameLength)
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
        if (!desc.vertexShader || desc.bindingLayoutCount > MaximumBindingLayoutsPerPipeline ||
            desc.bindingLayoutCount + desc.descriptorDomainCount > MaximumBindingLayoutsPerPipeline ||
            (desc.bindingLayouts == nullptr) != (desc.bindingLayoutCount == 0) || desc.attachments.colorCount > MaximumColorAttachments ||
            desc.descriptorDomainCount > MaximumDescriptorDomainsPerPipeline ||
            (desc.descriptorDomains == nullptr) != (desc.descriptorDomainCount == 0) || desc.attachments.sampleCount == 0 ||
            (desc.topology == PrimitiveTopology::PatchList && desc.patchControlPoints == 0))
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
        if (!desc.computeShader || !IsResourceReferenceValid(ResourceRef(desc.computeShader)) ||
            desc.bindingLayoutCount > MaximumBindingLayoutsPerPipeline ||
            desc.bindingLayoutCount + desc.descriptorDomainCount > MaximumBindingLayoutsPerPipeline ||
            (desc.bindingLayouts == nullptr) != (desc.bindingLayoutCount == 0) ||
            desc.descriptorDomainCount > MaximumDescriptorDomainsPerPipeline ||
            (desc.descriptorDomains == nullptr) != (desc.descriptorDomainCount == 0))
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
        if (desc.shaders == nullptr || desc.shaderCount == 0 || desc.shaderCount > MaximumRayTracingShaders ||
            (desc.hitGroups == nullptr) != (desc.hitGroupCount == 0) || desc.hitGroupCount > MaximumRayTracingHitGroups ||
            (desc.globalBindingLayouts == nullptr) != (desc.globalBindingLayoutCount == 0) ||
            desc.globalBindingLayoutCount > MaximumBindingLayoutsPerPipeline ||
            desc.globalBindingLayoutCount + desc.descriptorDomainCount > MaximumBindingLayoutsPerPipeline ||
            desc.descriptorDomainCount > MaximumDescriptorDomainsPerPipeline ||
            (desc.descriptorDomains == nullptr) != (desc.descriptorDomainCount == 0) || desc.maximumRecursionDepth == 0)
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
            if (group.exportName == nullptr || group.exportName[0] == '\0' ||
                (!group.closestHitShader && !group.anyHitShader && !group.intersectionShader) ||
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

    QueryPoolRef CreateQueryPool(const QueryPoolDesc& desc, Failure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!RequireBackend(failure))
            return {};
        if (desc.capacity == 0)
        {
            Reject(failure, FailureCode::InvalidArgument, "query-pool capacity must be non-zero");
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

    MemoryRequirements GetMemoryRequirements(const TextureRef texture) noexcept
    {
        return g_backend != nullptr && texture.IsValid() ? g_backend->GetMemoryRequirements(texture) : MemoryRequirements{};
    }

    MemoryRequirements GetMemoryRequirements(const BufferRef buffer) noexcept
    {
        return g_backend != nullptr && buffer.IsValid() ? g_backend->GetMemoryRequirements(buffer) : MemoryRequirements{};
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
    void AddRef(const SwapChainRef resource) noexcept
    {
        AddResourceReference(resource);
    }

    i32 Release(const TextureRef resource) noexcept
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
    i32 Release(const SwapChainRef resource) noexcept
    {
        return ReleaseResourceReference(resource);
    }

    CommandListRef CreateCommandList(const CommandListType type, const u64 debugHash, Failure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!RequireBackend(failure))
            return {};
        if (type == CommandListType::None)
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
        return g_backend != nullptr && g_boundCommandList.IsValid() ? g_backend->GetCommandListType(g_boundCommandList)
                                                                    : CommandListType::None;
    }

    bool CloseAndSubmitCommandLists(const char* const scopeName, const containers::ArraySpan<const CommandListRef> commandLists,
                                    const CommandListSyncType sync, GpuFence& completion, Failure* const failure) noexcept
    {
        completion = {};
        if (!RequireBackend(failure))
            return false;
        if (scopeName == nullptr || scopeName[0] == '\0' || commandLists.Data() == nullptr || commandLists.Size() == 0 ||
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
        return Accept(g_backend->CloseAndSubmitCommandLists(scopeName, commandLists, sync, completion), failure);
    }

    GpuFence GetGpuFence() noexcept
    {
        return g_backend != nullptr && g_boundCommandList.IsValid() ? g_backend->GetGpuFence(g_boundCommandList) : GpuFence{};
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
        if (GetBoundCommandListType() != CommandListType::Default || setup.colorTargetCount > MaximumColorAttachments ||
            (setup.colorTargetCount == 0 && !setup.depthStencilTarget.texture))
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

    bool SetViewport(const ViewportDesc& viewport, Failure* const failure) noexcept
    {
        if (!RequireBoundCommandList(failure))
            return false;
        if (GetBoundCommandListType() != CommandListType::Default || !(viewport.width > 0.0f) || !(viewport.height > 0.0f) ||
            !(viewport.minimumDepth >= 0.0f) || !(viewport.maximumDepth <= 1.0f) || viewport.minimumDepth > viewport.maximumDepth)
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

    bool BindVertexBuffers(const u32 startIndex, const containers::ArraySpan<const VertexBufferBinding> bindings,
                           Failure* const failure) noexcept
    {
        if (!RequireBoundCommandList(failure))
            return false;
        if (GetBoundCommandListType() != CommandListType::Default || bindings.Data() == nullptr || bindings.Size() == 0 ||
            startIndex >= MaximumVertexBindings || bindings.Size() > MaximumVertexBindings - startIndex)
            return Reject(failure, FailureCode::InvalidArgument, "invalid vertex-buffer binding span");
        for (u32 index = 0; index < bindings.Size(); ++index)
            if (!bindings[index].buffer || bindings[index].binding != startIndex + index ||
                !IsResourceReferenceValid(ResourceRef(bindings[index].buffer)))
                return Reject(failure, FailureCode::InvalidReference, "vertex-buffer span contains an invalid binding");
        return Accept(g_backend->BindVertexBuffers(g_boundCommandList, startIndex, bindings), failure);
    }

    bool BindIndexBuffer(const IndexBufferBinding& binding, Failure* const failure) noexcept
    {
        if (!RequireBoundCommandList(failure))
            return false;
        if (GetBoundCommandListType() != CommandListType::Default || !binding.buffer ||
            !IsResourceReferenceValid(ResourceRef(binding.buffer)))
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
        if (GetBoundCommandListType() != CommandListType::Default || commandCount == 0 || argumentsOffset > 0xffffffffu)
            return Reject(failure, FailureCode::InvalidArgument, "invalid indirect draw arguments");
        return Accept(g_backend->DrawPrimitiveIndirect(g_boundCommandList, argumentsOffset, commandCount), failure);
    }

    bool DrawIndexedPrimitiveIndirect(const u64 argumentsOffset, const u32 commandCount, Failure* const failure) noexcept
    {
        if (!RequireBoundCommandList(failure))
            return false;
        if (GetBoundCommandListType() != CommandListType::Default || commandCount == 0 || argumentsOffset > 0xffffffffu)
            return Reject(failure, FailureCode::InvalidArgument, "invalid indexed indirect draw arguments");
        return Accept(g_backend->DrawIndexedPrimitiveIndirect(g_boundCommandList, argumentsOffset, commandCount), failure);
    }

    bool DrawIndexedPrimitiveIndirectCount(const u64 argumentsOffset, const u64 countOffset, const u32 maximumCommandCount,
                                           Failure* const failure) noexcept
    {
        if (!RequireBoundCommandList(failure))
            return false;
        if (GetBoundCommandListType() != CommandListType::Default || maximumCommandCount == 0 || argumentsOffset > 0xffffffffu ||
            countOffset > 0xffffffffu)
            return Reject(failure, FailureCode::InvalidArgument, "invalid counted indirect draw arguments");
        return Accept(g_backend->DrawIndexedPrimitiveIndirectCount(g_boundCommandList, argumentsOffset, countOffset, maximumCommandCount),
                      failure);
    }

    bool DispatchCompute(const u32 groupCountX, const u32 groupCountY, const u32 groupCountZ, Failure* const failure) noexcept
    {
        if (!RequireBoundCommandList(failure))
            return false;
        const CommandListType type = GetBoundCommandListType();
        if ((type != CommandListType::Default && type != CommandListType::Compute) || groupCountX == 0 || groupCountY == 0 ||
            groupCountZ == 0)
            return Reject(failure, FailureCode::InvalidArgument, "invalid compute dispatch arguments");
        return Accept(g_backend->DispatchCompute(g_boundCommandList, groupCountX, groupCountY, groupCountZ), failure);
    }

    bool DispatchIndirectCompute(const u64 argumentsOffset, Failure* const failure) noexcept
    {
        if (!RequireBoundCommandList(failure))
            return false;
        const CommandListType type = GetBoundCommandListType();
        if ((type != CommandListType::Default && type != CommandListType::Compute) || argumentsOffset > 0xffffffffu)
            return Reject(failure, FailureCode::InvalidArgument, "invalid indirect dispatch arguments");
        return Accept(g_backend->DispatchIndirectCompute(g_boundCommandList, argumentsOffset), failure);
    }

    bool WriteBuffer(const BufferRef buffer, const void* const data, const u64 size, const u64 destinationOffset,
                     Failure* const failure) noexcept
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

    bool CopyBuffer(const BufferRef destination, const u64 destinationOffset, const BufferRef source, const u64 sourceOffset,
                    const u64 size, Failure* const failure) noexcept
    {
        if (!RequireBoundCommandList(failure))
            return false;
        if (!destination.IsValid() || !source.IsValid() || size == 0 || destinationOffset + size < destinationOffset ||
            sourceOffset + size < sourceOffset)
            return Reject(failure, FailureCode::InvalidArgument, "invalid buffer copy");
        return Accept(g_backend->CopyBuffer(g_boundCommandList, destination, destinationOffset, source, sourceOffset, size), failure);
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

    bool TransitionTexture(const TextureRef texture, const ResourceState before, const ResourceState after, const SubresourceRange& range,
                           Failure* const failure) noexcept
    {
        if (!RequireBoundCommandList(failure))
            return false;
        if (!texture.IsValid() || before == ResourceState::Unknown || after == ResourceState::Unknown)
            return Reject(failure, FailureCode::InvalidArgument, "invalid texture transition");
        return Accept(g_backend->TransitionTexture(g_boundCommandList, texture, before, after, range), failure);
    }

    bool TransitionBuffer(const BufferRef buffer, const ResourceState before, const ResourceState after, Failure* const failure) noexcept
    {
        if (!RequireBoundCommandList(failure))
            return false;
        if (!buffer.IsValid() || before == ResourceState::Unknown || after == ResourceState::Unknown)
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
    bool BarrierTextureAliasing(const bool discardAfter, const TextureRef textureAfter, const TextureRef textureBefore,
                                Failure* const failure) noexcept
    {
        if (!RequireBoundCommandList(failure))
            return false;
        if (!textureAfter.IsValid())
            return Reject(failure, FailureCode::InvalidReference, "aliasing barrier requires the texture after aliasing");
        return Accept(g_backend->BarrierTextureAliasing(g_boundCommandList, discardAfter, textureAfter, textureBefore), failure);
    }
    bool BarrierBufferAliasing(const bool discardAfter, const BufferRef bufferAfter, const BufferRef bufferBefore,
                               Failure* const failure) noexcept
    {
        if (!RequireBoundCommandList(failure))
            return false;
        if (!bufferAfter.IsValid())
            return Reject(failure, FailureCode::InvalidReference, "aliasing barrier requires the buffer after aliasing");
        return Accept(g_backend->BarrierBufferAliasing(g_boundCommandList, discardAfter, bufferAfter, bufferBefore), failure);
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
        if (desc.surface.kind == PresentationSurfaceKind::None || desc.surface.nativeWindow == nullptr || desc.width == 0 ||
            desc.height == 0 || desc.bufferCount < 2 || desc.format == Format::Unknown)
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
    TextureRef GetBackBufferTexture(const SwapChainRef swapChain) noexcept
    {
        return g_backend != nullptr && swapChain.IsValid() ? g_backend->GetBackBufferTexture(swapChain) : TextureRef{};
    }
    bool TransitionSwapChainPresent(const SwapChainRef swapChain, Failure* const failure) noexcept
    {
        if (!RequireBoundCommandList(failure))
            return false;
        if (!swapChain.IsValid())
            return Reject(failure, FailureCode::InvalidReference, "invalid swap chain");
        return Accept(g_backend->TransitionSwapChainPresent(g_boundCommandList, swapChain), failure);
    }
    bool Present(const SwapChainRef swapChain, Failure* const failure) noexcept
    {
        if (!RequireBackend(failure))
            return false;
        if (!swapChain.IsValid())
            return Reject(failure, FailureCode::InvalidReference, "invalid swap chain");
        return Accept(g_backend->Present(swapChain), failure);
    }
    void SetResourceDebugName(const TextureRef texture, const char* const name) noexcept
    {
        if (g_backend != nullptr && texture.IsValid())
            g_backend->SetResourceDebugName(texture, name);
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
