#include <vanguard/rhi/backend/common_backend.hpp>

#include <vanguard/diagnostics/diagnostics.hpp>
#include <vanguard/memory/memory.hpp>

#include <new>

namespace vanguard::rhi::backend
{
    static_assert(sizeof(IndirectDrawArguments) == sizeof(nvrhi::DrawIndirectArguments));
    static_assert(sizeof(IndirectDrawIndexedArguments) == sizeof(nvrhi::DrawIndexedIndirectArguments));
    static_assert(sizeof(IndirectDispatchArguments) == sizeof(nvrhi::DispatchIndirectArguments));

    namespace
    {
        template <typename Enum> [[nodiscard]] constexpr bool HasFlag(const Enum value, const Enum flag) noexcept
        {
            return (static_cast<u32>(value) & static_cast<u32>(flag)) != 0;
        }

        [[nodiscard]] nvrhi::Format ToNativeFormat(const Format format) noexcept
        {
            switch (format)
            {
            case Format::R8UNorm:
                return nvrhi::Format::R8_UNORM;
            case Format::R8SNorm:
                return nvrhi::Format::R8_SNORM;
            case Format::R8UInt:
                return nvrhi::Format::R8_UINT;
            case Format::R8G8UNorm:
                return nvrhi::Format::RG8_UNORM;
            case Format::R8G8SNorm:
                return nvrhi::Format::RG8_SNORM;
            case Format::R8G8UInt:
                return nvrhi::Format::RG8_UINT;
            case Format::R8G8B8A8UNorm:
                return nvrhi::Format::RGBA8_UNORM;
            case Format::R8G8B8A8UNormSrgb:
                return nvrhi::Format::SRGBA8_UNORM;
            case Format::R8G8B8A8SNorm:
                return nvrhi::Format::RGBA8_SNORM;
            case Format::R8G8B8A8UInt:
                return nvrhi::Format::RGBA8_UINT;
            case Format::B8G8R8A8UNorm:
                return nvrhi::Format::BGRA8_UNORM;
            case Format::B8G8R8A8UNormSrgb:
                return nvrhi::Format::SBGRA8_UNORM;
            case Format::R16UNorm:
                return nvrhi::Format::R16_UNORM;
            case Format::R16SNorm:
                return nvrhi::Format::R16_SNORM;
            case Format::R16UInt:
                return nvrhi::Format::R16_UINT;
            case Format::R16Float:
                return nvrhi::Format::R16_FLOAT;
            case Format::R16G16UNorm:
                return nvrhi::Format::RG16_UNORM;
            case Format::R16G16SNorm:
                return nvrhi::Format::RG16_SNORM;
            case Format::R16G16UInt:
                return nvrhi::Format::RG16_UINT;
            case Format::R16G16Float:
                return nvrhi::Format::RG16_FLOAT;
            case Format::R16G16B16A16UNorm:
                return nvrhi::Format::RGBA16_UNORM;
            case Format::R16G16B16A16SNorm:
                return nvrhi::Format::RGBA16_SNORM;
            case Format::R16G16B16A16UInt:
                return nvrhi::Format::RGBA16_UINT;
            case Format::R16G16B16A16Float:
                return nvrhi::Format::RGBA16_FLOAT;
            case Format::R32UInt:
                return nvrhi::Format::R32_UINT;
            case Format::R32Float:
                return nvrhi::Format::R32_FLOAT;
            case Format::R32G32UInt:
                return nvrhi::Format::RG32_UINT;
            case Format::R32G32Float:
                return nvrhi::Format::RG32_FLOAT;
            case Format::R32G32B32Float:
                return nvrhi::Format::RGB32_FLOAT;
            case Format::R32G32B32A32Float:
                return nvrhi::Format::RGBA32_FLOAT;
            case Format::R10G10B10A2UNorm:
                return nvrhi::Format::R10G10B10A2_UNORM;
            case Format::R11G11B10Float:
                return nvrhi::Format::R11G11B10_FLOAT;
            case Format::D16UNorm:
                return nvrhi::Format::D16;
            case Format::D24UNormS8UInt:
                return nvrhi::Format::D24S8;
            case Format::D32Float:
                return nvrhi::Format::D32;
            case Format::D32FloatS8UInt:
                return nvrhi::Format::D32S8;
            case Format::BC1UNorm:
                return nvrhi::Format::BC1_UNORM;
            case Format::BC1UNormSrgb:
                return nvrhi::Format::BC1_UNORM_SRGB;
            case Format::BC2UNorm:
                return nvrhi::Format::BC2_UNORM;
            case Format::BC2UNormSrgb:
                return nvrhi::Format::BC2_UNORM_SRGB;
            case Format::BC3UNorm:
                return nvrhi::Format::BC3_UNORM;
            case Format::BC3UNormSrgb:
                return nvrhi::Format::BC3_UNORM_SRGB;
            case Format::BC4UNorm:
                return nvrhi::Format::BC4_UNORM;
            case Format::BC4SNorm:
                return nvrhi::Format::BC4_SNORM;
            case Format::BC5UNorm:
                return nvrhi::Format::BC5_UNORM;
            case Format::BC5SNorm:
                return nvrhi::Format::BC5_SNORM;
            case Format::BC6HUFloat:
                return nvrhi::Format::BC6H_UFLOAT;
            case Format::BC6HSFloat:
                return nvrhi::Format::BC6H_SFLOAT;
            case Format::BC7UNorm:
                return nvrhi::Format::BC7_UNORM;
            case Format::BC7UNormSrgb:
                return nvrhi::Format::BC7_UNORM_SRGB;
            default:
                return nvrhi::Format::UNKNOWN;
            }
        }

        [[nodiscard]] nvrhi::ResourceStates ToNativeState(const ResourceState state) noexcept
        {
            u32 result = 0;
            const auto include = [&result](const nvrhi::ResourceStates nativeState) noexcept { result |= static_cast<u32>(nativeState); };
            if (HasFlag(state, ResourceState::Common))
                include(nvrhi::ResourceStates::Common);
            if (HasFlag(state, ResourceState::CopySource))
                include(nvrhi::ResourceStates::CopySource);
            if (HasFlag(state, ResourceState::CopyDestination))
                include(nvrhi::ResourceStates::CopyDest);
            if (HasFlag(state, ResourceState::ShaderResourceGraphics))
                include(nvrhi::ResourceStates::PixelShaderResource);
            if (HasFlag(state, ResourceState::ShaderResourceCompute))
                include(nvrhi::ResourceStates::NonPixelShaderResource);
            if (HasFlag(state, ResourceState::UnorderedAccess))
                include(nvrhi::ResourceStates::UnorderedAccess);
            if (HasFlag(state, ResourceState::RenderTarget))
                include(nvrhi::ResourceStates::RenderTarget);
            if (HasFlag(state, ResourceState::DepthWrite))
                include(nvrhi::ResourceStates::DepthWrite);
            if (HasFlag(state, ResourceState::DepthRead))
                include(nvrhi::ResourceStates::DepthRead);
            if (HasFlag(state, ResourceState::VertexBuffer))
                include(nvrhi::ResourceStates::VertexBuffer);
            if (HasFlag(state, ResourceState::IndexBuffer))
                include(nvrhi::ResourceStates::IndexBuffer);
            if (HasFlag(state, ResourceState::ConstantBuffer))
                include(nvrhi::ResourceStates::ConstantBuffer);
            if (HasFlag(state, ResourceState::IndirectArgument))
                include(nvrhi::ResourceStates::IndirectArgument);
            if (HasFlag(state, ResourceState::AccelerationStructureRead))
                include(nvrhi::ResourceStates::AccelStructRead);
            if (HasFlag(state, ResourceState::AccelerationStructureWrite))
                include(nvrhi::ResourceStates::AccelStructWrite);
            if (HasFlag(state, ResourceState::Present))
                include(nvrhi::ResourceStates::Present);
            return static_cast<nvrhi::ResourceStates>(result);
        }

        [[nodiscard]] nvrhi::CommandQueue ToNativeQueue(const CommandListType type) noexcept
        {
            if (type == CommandListType::Compute)
                return nvrhi::CommandQueue::Compute;
            if (type == CommandListType::CopyAsync)
                return nvrhi::CommandQueue::Copy;
            return nvrhi::CommandQueue::Graphics;
        }

        [[nodiscard]] constexpr u32 QueueIndex(const QueueType queue) noexcept
        {
            return static_cast<u32>(queue);
        }

        [[nodiscard]] nvrhi::ShaderType ToNativeVisibility(const ShaderStageMask stages) noexcept
        {
            nvrhi::ShaderType result = nvrhi::ShaderType::None;
            const auto include = [&result, stages](const ShaderStage stage, const nvrhi::ShaderType native) noexcept
            {
                if ((stages & ShaderStageBit(stage)) != 0)
                    result = static_cast<nvrhi::ShaderType>(static_cast<u16>(result) | static_cast<u16>(native));
            };
            include(ShaderStage::Vertex, nvrhi::ShaderType::Vertex);
            include(ShaderStage::Hull, nvrhi::ShaderType::Hull);
            include(ShaderStage::Domain, nvrhi::ShaderType::Domain);
            include(ShaderStage::Geometry, nvrhi::ShaderType::Geometry);
            include(ShaderStage::Pixel, nvrhi::ShaderType::Pixel);
            include(ShaderStage::Compute, nvrhi::ShaderType::Compute);
            include(ShaderStage::Mesh, nvrhi::ShaderType::Mesh);
            include(ShaderStage::Amplification, nvrhi::ShaderType::Amplification);
            include(ShaderStage::RayGeneration, nvrhi::ShaderType::RayGeneration);
            include(ShaderStage::Miss, nvrhi::ShaderType::Miss);
            include(ShaderStage::ClosestHit, nvrhi::ShaderType::ClosestHit);
            include(ShaderStage::AnyHit, nvrhi::ShaderType::AnyHit);
            include(ShaderStage::Intersection, nvrhi::ShaderType::Intersection);
            include(ShaderStage::Callable, nvrhi::ShaderType::Callable);
            return result;
        }

        [[nodiscard]] nvrhi::ResourceType ToNativeBindingType(const BindingType type) noexcept
        {
            switch (type)
            {
            case BindingType::ConstantBuffer:
                return nvrhi::ResourceType::ConstantBuffer;
            case BindingType::TextureShaderResource:
                return nvrhi::ResourceType::Texture_SRV;
            case BindingType::TextureUnorderedAccess:
                return nvrhi::ResourceType::Texture_UAV;
            case BindingType::TypedBufferShaderResource:
                return nvrhi::ResourceType::TypedBuffer_SRV;
            case BindingType::TypedBufferUnorderedAccess:
                return nvrhi::ResourceType::TypedBuffer_UAV;
            case BindingType::StructuredBufferShaderResource:
                return nvrhi::ResourceType::StructuredBuffer_SRV;
            case BindingType::StructuredBufferUnorderedAccess:
                return nvrhi::ResourceType::StructuredBuffer_UAV;
            case BindingType::ByteAddressBufferShaderResource:
                return nvrhi::ResourceType::RawBuffer_SRV;
            case BindingType::ByteAddressBufferUnorderedAccess:
                return nvrhi::ResourceType::RawBuffer_UAV;
            case BindingType::Sampler:
                return nvrhi::ResourceType::Sampler;
            case BindingType::AccelerationStructure:
                return nvrhi::ResourceType::RayTracingAccelStruct;
            case BindingType::PushConstants:
                return nvrhi::ResourceType::PushConstants;
            default:
                return nvrhi::ResourceType::None;
            }
        }

        [[nodiscard]] nvrhi::PrimitiveType ToNativeTopology(const PrimitiveTopology value) noexcept
        {
            switch (value)
            {
            case PrimitiveTopology::PointList:
                return nvrhi::PrimitiveType::PointList;
            case PrimitiveTopology::LineList:
                return nvrhi::PrimitiveType::LineList;
            case PrimitiveTopology::LineStrip:
                return nvrhi::PrimitiveType::LineStrip;
            case PrimitiveTopology::TriangleStrip:
                return nvrhi::PrimitiveType::TriangleStrip;
            case PrimitiveTopology::PatchList:
                return nvrhi::PrimitiveType::PatchList;
            default:
                return nvrhi::PrimitiveType::TriangleList;
            }
        }

        [[nodiscard]] nvrhi::ComparisonFunc ToNativeComparison(const ComparisonFunction value) noexcept
        {
            switch (value)
            {
            case ComparisonFunction::Never:
                return nvrhi::ComparisonFunc::Never;
            case ComparisonFunction::Less:
                return nvrhi::ComparisonFunc::Less;
            case ComparisonFunction::Equal:
                return nvrhi::ComparisonFunc::Equal;
            case ComparisonFunction::Greater:
                return nvrhi::ComparisonFunc::Greater;
            case ComparisonFunction::NotEqual:
                return nvrhi::ComparisonFunc::NotEqual;
            case ComparisonFunction::GreaterEqual:
                return nvrhi::ComparisonFunc::GreaterOrEqual;
            case ComparisonFunction::Always:
                return nvrhi::ComparisonFunc::Always;
            default:
                return nvrhi::ComparisonFunc::LessOrEqual;
            }
        }

        [[nodiscard]] nvrhi::StencilOp ToNativeStencil(const StencilOperation value) noexcept
        {
            switch (value)
            {
            case StencilOperation::Zero:
                return nvrhi::StencilOp::Zero;
            case StencilOperation::Replace:
                return nvrhi::StencilOp::Replace;
            case StencilOperation::IncrementClamp:
                return nvrhi::StencilOp::IncrementAndClamp;
            case StencilOperation::DecrementClamp:
                return nvrhi::StencilOp::DecrementAndClamp;
            case StencilOperation::Invert:
                return nvrhi::StencilOp::Invert;
            case StencilOperation::IncrementWrap:
                return nvrhi::StencilOp::IncrementAndWrap;
            case StencilOperation::DecrementWrap:
                return nvrhi::StencilOp::DecrementAndWrap;
            default:
                return nvrhi::StencilOp::Keep;
            }
        }

        [[nodiscard]] nvrhi::BlendFactor ToNativeBlendFactor(const BlendFactor value) noexcept
        {
            switch (value)
            {
            case BlendFactor::Zero:
                return nvrhi::BlendFactor::Zero;
            case BlendFactor::SourceColor:
                return nvrhi::BlendFactor::SrcColor;
            case BlendFactor::OneMinusSourceColor:
                return nvrhi::BlendFactor::InvSrcColor;
            case BlendFactor::DestinationColor:
                return nvrhi::BlendFactor::DstColor;
            case BlendFactor::OneMinusDestinationColor:
                return nvrhi::BlendFactor::InvDstColor;
            case BlendFactor::SourceAlpha:
                return nvrhi::BlendFactor::SrcAlpha;
            case BlendFactor::OneMinusSourceAlpha:
                return nvrhi::BlendFactor::InvSrcAlpha;
            case BlendFactor::DestinationAlpha:
                return nvrhi::BlendFactor::DstAlpha;
            case BlendFactor::OneMinusDestinationAlpha:
                return nvrhi::BlendFactor::InvDstAlpha;
            case BlendFactor::ConstantColor:
                return nvrhi::BlendFactor::ConstantColor;
            case BlendFactor::OneMinusConstantColor:
                return nvrhi::BlendFactor::InvConstantColor;
            case BlendFactor::SourceAlphaSaturate:
                return nvrhi::BlendFactor::SrcAlphaSaturate;
            case BlendFactor::SourceOneColor:
                return nvrhi::BlendFactor::Src1Color;
            case BlendFactor::OneMinusSourceOneColor:
                return nvrhi::BlendFactor::InvSrc1Color;
            case BlendFactor::SourceOneAlpha:
                return nvrhi::BlendFactor::Src1Alpha;
            case BlendFactor::OneMinusSourceOneAlpha:
                return nvrhi::BlendFactor::InvSrc1Alpha;
            default:
                return nvrhi::BlendFactor::One;
            }
        }

        [[nodiscard]] nvrhi::BlendOp ToNativeBlendOperation(const BlendOperation value) noexcept
        {
            switch (value)
            {
            case BlendOperation::Subtract:
                return nvrhi::BlendOp::Subtract;
            case BlendOperation::ReverseSubtract:
                return nvrhi::BlendOp::ReverseSubtract;
            case BlendOperation::Minimum:
                return nvrhi::BlendOp::Min;
            case BlendOperation::Maximum:
                return nvrhi::BlendOp::Max;
            default:
                return nvrhi::BlendOp::Add;
            }
        }

        [[nodiscard]] u8 BindingTypeClass(const BindingType type) noexcept
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

        [[nodiscard]] u64 HashBindingLayout(const BindingLayoutDesc& desc) noexcept
        {
            u64 hash = 1469598103934665603ull;
            const auto append = [&hash](const u64 value) noexcept
            {
                hash ^= value;
                hash *= 1099511628211ull;
            };
            append(desc.registerSpace);
            append(desc.visibility);
            append(desc.entryCount);
            for (u32 index = 0; index < desc.entryCount; ++index)
            {
                append(desc.entries[index].slot);
                append(desc.entries[index].arrayCount);
                append(static_cast<u8>(desc.entries[index].type));
            }
            return hash;
        }

        [[nodiscard]] bool StringsEqual(const char* left, const char* right) noexcept
        {
            if (left == nullptr || right == nullptr)
                return left == right;
            while (*left != '\0' && *right != '\0')
            {
                if (*left != *right)
                    return false;
                ++left;
                ++right;
            }
            return *left == *right;
        }

        void CopyString(char* destination, const u32 capacity, const char* source) noexcept
        {
            if (destination == nullptr || capacity == 0)
                return;
            u32 index = 0;
            if (source != nullptr)
                while (index + 1u < capacity && source[index] != '\0')
                {
                    destination[index] = source[index];
                    ++index;
                }
            destination[index] = '\0';
        }

        [[nodiscard]] u64 HashVertexLayout(const VertexLayoutDesc& desc) noexcept
        {
            u64 hash = 1469598103934665603ull;
            const auto append = [&hash](const u64 value) noexcept
            {
                hash ^= value;
                hash *= 1099511628211ull;
            };
            append(desc.bindingCount);
            append(desc.attributeCount);
            for (u32 index = 0; index < desc.bindingCount; ++index)
            {
                const VertexBindingDesc& binding = desc.bindings[index];
                append(binding.binding);
                append(binding.stride);
                append(static_cast<u8>(binding.inputRate));
                append(binding.instanceStepRate);
            }
            for (u32 index = 0; index < desc.attributeCount; ++index)
            {
                const VertexAttributeDesc& attribute = desc.attributes[index];
                append(attribute.location);
                append(attribute.binding);
                append(attribute.offset);
                append(static_cast<u16>(attribute.format));
                append(attribute.semanticIndex);
                if (attribute.semanticName != nullptr)
                    for (const char* character = attribute.semanticName; *character != '\0'; ++character)
                        append(static_cast<u8>(*character));
            }
            return hash;
        }

        template <typename Payload, typename... Args> [[nodiscard]] Payload* AllocatePayload(Args&&... args) noexcept
        {
            memory::MemoryBlock block = memory::Allocate(memory::PoolId::Rendering, sizeof(Payload), alignof(Payload));
            return block ? new (block.address) Payload(static_cast<Args&&>(args)...) : nullptr;
        }

        template <typename Payload> void DestroyPayload(void*, ResourceRef, void* const address) noexcept
        {
            if (address == nullptr)
                return;
            static_cast<Payload*>(address)->~Payload();
            memory::MemoryBlock block{address, sizeof(Payload), memory::PoolId::Rendering};
            memory::Free(block);
        }

        struct TexturePayload
        {
            nvrhi::TextureHandle native;
            TextureDesc desc;
        };
        struct BufferPayload
        {
            nvrhi::BufferHandle native;
            BufferDesc desc;
            bool mapped = false;
        };
        struct HeapPayload
        {
            nvrhi::HeapHandle native;
            HeapDesc desc;
        };
        struct SamplerPayload
        {
            nvrhi::SamplerHandle native;
            SamplerStateDesc desc;
        };
        struct ShaderPayload
        {
            nvrhi::ShaderHandle native;
            ShaderStage stage = ShaderStage::Vertex;
        };
        struct StoredVertexAttribute
        {
            VertexAttributeDesc desc;
            char semanticName[MaximumVertexSemanticNameLength]{};
        };
        struct VertexLayoutPayload
        {
            VertexBindingDesc bindings[MaximumVertexBindings]{};
            StoredVertexAttribute attributes[MaximumVertexAttributes]{};
            u64 hash = 0;
            u32 bindingCount = 0;
            u32 attributeCount = 0;
        };
        struct PipelinePayload
        {
            nvrhi::GraphicsPipelineHandle graphics;
            nvrhi::ComputePipelineHandle compute;
            nvrhi::rt::PipelineHandle rayTracing;
            nvrhi::InputLayoutHandle inputLayout;
            nvrhi::BindingSetHandle fixedBindings[MaximumBindingLayoutsPerPipeline]{};
            nvrhi::DescriptorTableHandle descriptorTables[MaximumDescriptorDomainsPerPipeline]{};
            DescriptorDomainRef descriptorDomains[MaximumDescriptorDomainsPerPipeline]{};
            PipelineKind kind = PipelineKind::Graphics;
            u32 fixedBindingCount = 0;
            u32 descriptorDomainCount = 0;
            u32 pushConstantSize = 0;
        };
        struct BindingLayoutPayload
        {
            nvrhi::BindingLayoutHandle native;
            BindingLayoutEntry entries[MaximumBindingLayoutEntries]{};
            u64 hash = 0;
            u32 entryCount = 0;
            u32 registerSpace = 0;
            ShaderStageMask visibility = 0;
        };
        enum class DescriptorSlotState : u8
        {
            Free,
            Allocated,
            Populated,
            Retiring,
            Exhausted
        };
        struct DescriptorSlot
        {
            ResourceRef resource;
            FenceSet retirement;
            u32 generation = 1;
            u32 nextFree = InvalidReferenceIndex;
            DescriptorSlotState state = DescriptorSlotState::Free;
        };
        struct DescriptorDomainPayload
        {
            explicit DescriptorDomainPayload(const DescriptorDomainDesc& source) noexcept
                : slots(memory::pools::Rendering::GetInstance()), desc(source)
            {
            }

            nvrhi::BindingLayoutHandle nativeLayout;
            nvrhi::DescriptorTableHandle nativeTable;
            containers::DynamicArray<DescriptorSlot> slots;
            mutable concurrency::SpinLock lock;
            DescriptorDomainDesc desc;
            DescriptorDomainStats stats;
            u32 freeHead = InvalidReferenceIndex;
            u32 gpuBaseIndex = 0;
        };

        [[nodiscard]] bool ResolveDescriptorSlot(const DescriptorDomainPayload& domain, const DescriptorHandle descriptor,
                                                 u32& slot) noexcept
        {
            if (descriptor.index < domain.gpuBaseIndex)
                return false;
            slot = descriptor.index - domain.gpuBaseIndex;
            return slot < domain.slots.Size() && domain.slots[slot].generation == descriptor.generation;
        }

        [[nodiscard]] bool DescriptorRetirementComplete(const CommonBackend& common, const FenceSet& fences) noexcept
        {
            return (fences.graphics == 0 || common.IsGpuFenceComplete({QueueType::Graphics, fences.graphics})) &&
                   (fences.compute == 0 || common.IsGpuFenceComplete({QueueType::Compute, fences.compute})) &&
                   (fences.copy == 0 || common.IsGpuFenceComplete({QueueType::Copy, fences.copy}));
        }

        void ReclaimDescriptorSlots(CommonBackend& common, DescriptorDomainPayload& domain) noexcept
        {
            for (u32 index = 0; index < domain.slots.Size(); ++index)
            {
                DescriptorSlot& slot = domain.slots[index];
                if (slot.state != DescriptorSlotState::Retiring || !DescriptorRetirementComplete(common, slot.retirement))
                    continue;

                if (domain.desc.kind == DescriptorDomainKind::Resources)
                    static_cast<void>(common.Device()->writeDescriptorTable(domain.nativeTable, nvrhi::BindingSetItem::None(index)));
                if (slot.resource.IsValid())
                    static_cast<void>(common.Release(slot.resource));
                slot.resource = {};
                slot.retirement = {};
                --domain.stats.pendingRetirement;
                ++domain.stats.completedRetirements;
                if (slot.generation == 0xffffffffu)
                {
                    slot.state = DescriptorSlotState::Exhausted;
                    slot.nextFree = InvalidReferenceIndex;
                    ++domain.stats.exhaustedSlots;
                    continue;
                }
                ++slot.generation;
                slot.state = DescriptorSlotState::Free;
                slot.nextFree = domain.freeHead;
                domain.freeHead = index;
            }
            domain.stats.free =
                domain.desc.capacity - domain.stats.allocated - domain.stats.pendingRetirement - domain.stats.exhaustedSlots;
        }

        void DestroyDescriptorDomain(void* const context, ResourceRef, void* const address) noexcept
        {
            if (address == nullptr)
                return;
            auto& common = *static_cast<CommonBackend*>(context);
            auto* const domain = static_cast<DescriptorDomainPayload*>(address);
            for (DescriptorSlot& slot : domain->slots)
                if (slot.resource.IsValid())
                    static_cast<void>(common.Release(slot.resource));
            domain->~DescriptorDomainPayload();
            memory::MemoryBlock block{domain, sizeof(DescriptorDomainPayload), memory::PoolId::Rendering};
            memory::Free(block);
        }
        struct CommandListPayload
        {
            explicit CommandListPayload(nvrhi::CommandListHandle&& commandList, const CommandListType commandListType) noexcept
                : native(static_cast<nvrhi::CommandListHandle&&>(commandList)), resources(memory::pools::Rendering::GetInstance()),
                  type(commandListType)
            {
            }
            nvrhi::CommandListHandle native;
            containers::DynamicArray<ResourceRef> resources;
            nvrhi::FramebufferHandle framebuffer;
            PipelineRef pipeline;
            ViewportDesc viewport;
            Rect scissors;
            VertexBufferBinding vertexBuffers[MaximumVertexBindings]{};
            IndexBufferBinding indexBuffer;
            BufferRef indirectArguments;
            BufferRef indirectCount;
            u8 pushConstants[nvrhi::c_MaxPushConstantSize]{};
            u32 vertexBufferCount = 0;
            u32 pushConstantSize = 0;
            CommandListType type = CommandListType::None;
            bool renderTargetsSet = false;
            bool viewportSet = false;
            bool scissorsSet = false;
            bool indexBufferSet = false;
            bool vertexBufferSet[MaximumVertexBindings]{};
            bool open = true;
        };

        void DestroyPipeline(void* const context, ResourceRef, void* const address) noexcept
        {
            if (address == nullptr)
                return;
            auto& common = *static_cast<CommonBackend*>(context);
            auto* const pipeline = static_cast<PipelinePayload*>(address);
            for (u32 index = 0; index < pipeline->descriptorDomainCount; ++index)
                static_cast<void>(common.Release(ResourceRef(pipeline->descriptorDomains[index])));
            DestroyPayload<PipelinePayload>(nullptr, {}, pipeline);
        }

        [[nodiscard]] bool TrackCommandResource(CommonBackend& common, CommandListPayload& command, const ResourceRef resource) noexcept
        {
            for (const ResourceRef tracked : command.resources)
                if (tracked == resource)
                    return true;
            if (!common.IsResourceReferenceValid(resource))
                return false;
            common.AddRef(resource);
            const u32 expected = command.resources.Size() + 1u;
            command.resources.PushBack(resource);
            if (command.resources.Size() == expected)
                return true;
            static_cast<void>(common.Release(resource));
            return false;
        }

        [[nodiscard]] bool PopulatePipelineBindings(CommonBackend& common, PipelinePayload& pipeline,
                                                    const BindingLayoutRef* const bindingLayouts, const u32 bindingLayoutCount,
                                                    const DescriptorDomainRef* const descriptorDomains,
                                                    const u32 descriptorDomainCount) noexcept
        {
            pipeline.fixedBindingCount = bindingLayoutCount;
            for (u32 index = 0; index < bindingLayoutCount; ++index)
            {
                const auto* const layout =
                    static_cast<const BindingLayoutPayload*>(common.GetResourcePayload(ResourceRef(bindingLayouts[index])));
                if (layout == nullptr)
                    return false;
                nvrhi::BindingSetDesc bindingSetDesc;
                bool pushConstantOnly = layout->entryCount != 0;
                for (u32 entryIndex = 0; entryIndex < layout->entryCount; ++entryIndex)
                {
                    const BindingLayoutEntry& entry = layout->entries[entryIndex];
                    if (entry.type != BindingType::PushConstants)
                    {
                        pushConstantOnly = false;
                        break;
                    }
                    if (pipeline.pushConstantSize != 0)
                        return false;
                    pipeline.pushConstantSize = entry.arrayCount;
                    bindingSetDesc.bindings.push_back(nvrhi::BindingSetItem::PushConstants(entry.slot, entry.arrayCount));
                }
                if (pushConstantOnly)
                {
                    pipeline.fixedBindings[index] = common.Device()->createBindingSet(bindingSetDesc, layout->native);
                    if (!pipeline.fixedBindings[index])
                        return false;
                }
            }
            pipeline.descriptorDomainCount = 0;
            for (u32 index = 0; index < descriptorDomainCount; ++index)
            {
                const auto* const domain =
                    static_cast<const DescriptorDomainPayload*>(common.GetResourcePayload(ResourceRef(descriptorDomains[index])));
                if (domain == nullptr)
                    return false;
                pipeline.descriptorDomains[index] = descriptorDomains[index];
                pipeline.descriptorTables[index] = domain->nativeTable;
                common.AddRef(ResourceRef(descriptorDomains[index]));
                ++pipeline.descriptorDomainCount;
            }
            return true;
        }

        [[nodiscard]] BackendStatus PopulateBoundResources(const PipelinePayload& pipeline, nvrhi::BindingSetVector& bindings) noexcept
        {
            for (u32 index = 0; index < pipeline.fixedBindingCount; ++index)
            {
                if (!pipeline.fixedBindings[index])
                    return BackendStatus::Failure(FailureCode::MissingBinding, 0, "the fixed-binding execution path is not materialized");
                bindings.push_back(pipeline.fixedBindings[index]);
            }
            for (u32 index = 0; index < pipeline.descriptorDomainCount; ++index)
                bindings.push_back(pipeline.descriptorTables[index]);
            return BackendStatus::Success();
        }

        [[nodiscard]] BackendStatus ValidatePushConstants(const CommandListPayload& command, const PipelinePayload& pipeline) noexcept
        {
            if (command.pushConstantSize == pipeline.pushConstantSize)
                return BackendStatus::Success();
            return pipeline.pushConstantSize == 0
                       ? BackendStatus::Failure(FailureCode::IncompatibleBinding, 0,
                                                "push constants were supplied to a pipeline that does not declare them")
                       : BackendStatus::Failure(FailureCode::MissingBinding, 0,
                                                "the pipeline's complete push-constant block was not supplied");
        }

        [[nodiscard]] BackendStatus SetupForGraphics(CommonBackend& common, CommandListPayload& command) noexcept
        {
            const auto* const pipeline = static_cast<const PipelinePayload*>(common.GetResourcePayload(ResourceRef(command.pipeline)));
            if (pipeline == nullptr || pipeline->kind != PipelineKind::Graphics || !pipeline->graphics)
                return BackendStatus::Failure(FailureCode::MissingBinding, 0, "a graphics pipeline must be bound before drawing");
            if (!command.renderTargetsSet || !command.framebuffer || !command.viewportSet)
                return BackendStatus::Failure(FailureCode::MissingBinding, 0, "render targets and a viewport must be set before drawing");
            BackendStatus status = ValidatePushConstants(command, *pipeline);
            if (!status)
                return status;

            nvrhi::GraphicsState state;
            state.pipeline = pipeline->graphics;
            state.framebuffer = command.framebuffer;
            state.viewport.addViewport(nvrhi::Viewport(command.viewport.x, command.viewport.x + command.viewport.width, command.viewport.y,
                                                       command.viewport.y + command.viewport.height, command.viewport.minimumDepth,
                                                       command.viewport.maximumDepth));
            if (command.scissorsSet)
                state.viewport.addScissorRect(nvrhi::Rect(command.scissors.x, command.scissors.x + command.scissors.width,
                                                          command.scissors.y, command.scissors.y + command.scissors.height));
            else
                state.viewport.addScissorRect(
                    nvrhi::Rect(static_cast<i32>(command.viewport.x), static_cast<i32>(command.viewport.x + command.viewport.width),
                                static_cast<i32>(command.viewport.y), static_cast<i32>(command.viewport.y + command.viewport.height)));
            status = PopulateBoundResources(*pipeline, state.bindings);
            if (!status)
                return status;
            for (u32 index = 0; index < command.vertexBufferCount; ++index)
            {
                if (!command.vertexBufferSet[index])
                    continue;
                const auto* const buffer =
                    static_cast<const BufferPayload*>(common.GetResourcePayload(ResourceRef(command.vertexBuffers[index].buffer)));
                if (buffer == nullptr)
                    return BackendStatus::Failure(FailureCode::InvalidReference, 0, "a bound vertex buffer became invalid");
                state.vertexBuffers.push_back(
                    nvrhi::VertexBufferBinding().setBuffer(buffer->native).setSlot(index).setOffset(command.vertexBuffers[index].offset));
            }
            if (command.indexBufferSet)
            {
                const auto* const buffer =
                    static_cast<const BufferPayload*>(common.GetResourcePayload(ResourceRef(command.indexBuffer.buffer)));
                if (buffer == nullptr)
                    return BackendStatus::Failure(FailureCode::InvalidReference, 0, "the bound index buffer became invalid");
                state.indexBuffer =
                    nvrhi::IndexBufferBinding()
                        .setBuffer(buffer->native)
                        .setFormat(command.indexBuffer.format == IndexFormat::UInt32 ? nvrhi::Format::R32_UINT : nvrhi::Format::R16_UINT)
                        .setOffset(static_cast<u32>(command.indexBuffer.offset));
            }
            if (command.indirectArguments)
            {
                const auto* const arguments =
                    static_cast<const BufferPayload*>(common.GetResourcePayload(ResourceRef(command.indirectArguments)));
                const auto* const count =
                    command.indirectCount ? static_cast<const BufferPayload*>(common.GetResourcePayload(ResourceRef(command.indirectCount)))
                                          : nullptr;
                if (arguments == nullptr || (command.indirectCount && count == nullptr))
                    return BackendStatus::Failure(FailureCode::InvalidReference, 0, "an indirect buffer became invalid");
                state.indirectParams = arguments->native;
                state.indirectCountBuffer = count != nullptr ? count->native : nullptr;
            }
            command.native->commitBarriers();
            command.native->setGraphicsState(state);
            if (command.pushConstantSize != 0)
                command.native->setPushConstants(command.pushConstants, command.pushConstantSize);
            return BackendStatus::Success();
        }

        [[nodiscard]] BackendStatus SetupForCompute(CommonBackend& common, CommandListPayload& command) noexcept
        {
            const auto* const pipeline = static_cast<const PipelinePayload*>(common.GetResourcePayload(ResourceRef(command.pipeline)));
            if (pipeline == nullptr || pipeline->kind != PipelineKind::Compute || !pipeline->compute)
                return BackendStatus::Failure(FailureCode::MissingBinding, 0, "a compute pipeline must be bound before dispatching");
            BackendStatus status = ValidatePushConstants(command, *pipeline);
            if (!status)
                return status;
            nvrhi::ComputeState state;
            state.pipeline = pipeline->compute;
            status = PopulateBoundResources(*pipeline, state.bindings);
            if (!status)
                return status;
            if (command.indirectArguments)
            {
                const auto* const arguments =
                    static_cast<const BufferPayload*>(common.GetResourcePayload(ResourceRef(command.indirectArguments)));
                if (arguments == nullptr)
                    return BackendStatus::Failure(FailureCode::InvalidReference, 0, "the indirect buffer became invalid");
                state.indirectParams = arguments->native;
            }
            command.native->commitBarriers();
            command.native->setComputeState(state);
            if (command.pushConstantSize != 0)
                command.native->setPushConstants(command.pushConstants, command.pushConstantSize);
            return BackendStatus::Success();
        }
    } // namespace

    CommonBackend::CommonBackend() noexcept
        : m_bindingLayouts(memory::pools::Rendering::GetInstance()), m_vertexLayouts(memory::pools::Rendering::GetInstance()),
          m_descriptorDomains(memory::pools::Rendering::GetInstance())
    {
        for (u32& bucket : m_bindingLayoutBuckets)
            bucket = InvalidReferenceIndex;
        for (u32& bucket : m_vertexLayoutBuckets)
            bucket = InvalidReferenceIndex;
    }

    CommonBackend::~CommonBackend()
    {
        if (!IsInitialized())
            return;
        VG_LOG_ERROR(diagnostics::Category::Rendering, "common RHI backend was destroyed before explicit shutdown");
        static_cast<void>(WaitIdle());
        ForceShutdownAfterGpuIdle();
    }

    bool CommonBackend::Initialize(nvrhi::DeviceHandle&& device, const FenceCompleteCallback fenceComplete,
                                   const SignalFenceCallback signalFence, const WaitFenceCallback waitFence,
                                   const AliasingBarrierCallback aliasingBarrier, void* const fenceContext) noexcept
    {
        if (IsInitialized() || !device || fenceComplete == nullptr || signalFence == nullptr || waitFence == nullptr ||
            aliasingBarrier == nullptr)
            return false;
        m_device = static_cast<nvrhi::DeviceHandle&&>(device);
        if (!m_lifetime.Initialize({}, fenceComplete, fenceContext))
        {
            m_device = nullptr;
            return false;
        }
        m_submittedFences[0].SetValue(0);
        m_submittedFences[1].SetValue(0);
        m_submittedFences[2].SetValue(0);
        m_submittedInstances[0] = 0;
        m_submittedInstances[1] = 0;
        m_submittedInstances[2] = 0;
        m_fenceComplete = fenceComplete;
        m_signalFence = signalFence;
        m_waitFence = waitFence;
        m_aliasingBarrier = aliasingBarrier;
        m_fenceContext = fenceContext;
        return true;
    }

    bool CommonBackend::ShutdownAfterGpuIdle() noexcept
    {
        if (!IsInitialized())
            return false;
        {
            concurrency::ScopedLock layoutGuard(m_bindingLayoutLock);
            for (const BindingLayoutCacheEntry& entry : m_bindingLayouts)
                static_cast<void>(m_lifetime.Release(ResourceRef(entry.layout)));
            m_bindingLayouts.Clear();
            for (u32& bucket : m_bindingLayoutBuckets)
                bucket = InvalidReferenceIndex;
        }
        {
            concurrency::ScopedLock layoutGuard(m_vertexLayoutLock);
            for (const VertexLayoutCacheEntry& entry : m_vertexLayouts)
                static_cast<void>(m_lifetime.Release(ResourceRef(entry.layout)));
            m_vertexLayouts.Clear();
            for (u32& bucket : m_vertexLayoutBuckets)
                bucket = InvalidReferenceIndex;
        }
        {
            concurrency::ScopedLock domainGuard(m_descriptorDomainListLock);
            m_descriptorDomains.Clear();
        }
        RetireResources();
        m_lifetime.WaitForReclamation();
        if (!m_lifetime.ShutdownAfterGpuIdle())
            return false;
        m_device = nullptr;
        m_fenceComplete = nullptr;
        m_signalFence = nullptr;
        m_waitFence = nullptr;
        m_aliasingBarrier = nullptr;
        m_fenceContext = nullptr;
        return true;
    }

    void CommonBackend::ForceShutdownAfterGpuIdle() noexcept
    {
        if (!IsInitialized())
            return;
        m_bindingLayouts.Clear();
        for (u32& bucket : m_bindingLayoutBuckets)
            bucket = InvalidReferenceIndex;
        m_vertexLayouts.Clear();
        for (u32& bucket : m_vertexLayoutBuckets)
            bucket = InvalidReferenceIndex;
        m_descriptorDomains.Clear();
        m_lifetime.ForceShutdownAfterGpuIdle();
        m_device = nullptr;
        m_fenceComplete = nullptr;
        m_signalFence = nullptr;
        m_waitFence = nullptr;
        m_aliasingBarrier = nullptr;
        m_fenceContext = nullptr;
    }

    bool CommonBackend::IsInitialized() const noexcept
    {
        return m_device != nullptr;
    }
    nvrhi::IDevice* CommonBackend::Device() const noexcept
    {
        return m_device.Get();
    }

    void CommonBackend::PopulateCapabilities(Capabilities& capabilities) const noexcept
    {
        if (!IsInitialized())
            return;
        capabilities.asyncCompute = m_device->queryFeatureSupport(nvrhi::Feature::ComputeQueue);
        capabilities.copyQueue = m_device->queryFeatureSupport(nvrhi::Feature::CopyQueue);
        capabilities.bindlessResources = m_device->queryFeatureSupport(nvrhi::Feature::HeapDirectlyIndexed);
        capabilities.bindlessSamplers = capabilities.bindlessResources;
        capabilities.descriptorIndexing = capabilities.bindlessResources;
        capabilities.maximumBindlessResources = capabilities.bindlessResources ? 1000000u : 0u;
        capabilities.maximumBindlessSamplers = capabilities.bindlessSamplers ? 2048u : 0u;
        capabilities.maximumPushConstantBytes = nvrhi::c_MaxPushConstantSize;
        capabilities.transientHeaps = m_device->queryFeatureSupport(nvrhi::Feature::VirtualResources);
        capabilities.resourceAliasing = capabilities.transientHeaps;
        capabilities.rayTracing = m_device->queryFeatureSupport(nvrhi::Feature::RayTracingAccelStruct);
        capabilities.rayTracingPipeline = m_device->queryFeatureSupport(nvrhi::Feature::RayTracingPipeline);
        capabilities.meshShaders = m_device->queryFeatureSupport(nvrhi::Feature::Meshlets);
        capabilities.variableRateShading = m_device->queryFeatureSupport(nvrhi::Feature::VariableRateShading);
    }

    bool CommonBackend::WaitIdle() noexcept
    {
        if (!IsInitialized())
            return false;
        concurrency::ScopedLock submissionGuard(m_submissionLock);
        return m_device->waitForIdle();
    }

    void CommonBackend::RetireResources() noexcept
    {
        if (!IsInitialized())
            return;
        {
            concurrency::ScopedLock domainListGuard(m_descriptorDomainListLock);
            for (u32 index = m_descriptorDomains.Size(); index != 0; --index)
            {
                const ResourceRef domainResource(m_descriptorDomains[index - 1u]);
                if (!m_lifetime.AddRef(domainResource))
                {
                    static_cast<void>(m_descriptorDomains.RemoveAt(index - 1u));
                    continue;
                }
                auto* const domain = static_cast<DescriptorDomainPayload*>(m_lifetime.GetPayload(domainResource));
                if (domain == nullptr)
                {
                    static_cast<void>(m_lifetime.Release(domainResource));
                    static_cast<void>(m_descriptorDomains.RemoveAt(index - 1u));
                    continue;
                }
                {
                    concurrency::ScopedLock domainGuard(domain->lock);
                    ReclaimDescriptorSlots(*this, *domain);
                }
                static_cast<void>(m_lifetime.Release(domainResource));
            }
        }
        {
            concurrency::ScopedLock submissionGuard(m_submissionLock);
            m_device->runGarbageCollection();
        }
        m_lifetime.SealRetirementEpoch({m_submittedFences[0].GetValue(), m_submittedFences[1].GetValue(), m_submittedFences[2].GetValue()});
        m_lifetime.CollectGarbage();
    }

    TextureRef CommonBackend::CreateTexture(const TextureDesc& desc, const TextureInitData& initialData) noexcept
    {
        nvrhi::TextureDesc nativeDesc{};
        nativeDesc.width = desc.extent.width;
        nativeDesc.height = desc.extent.height;
        nativeDesc.depth = desc.extent.depth;
        nativeDesc.arraySize = desc.arraySize;
        nativeDesc.mipLevels = desc.mipCount;
        nativeDesc.sampleCount = desc.sampleCount;
        nativeDesc.format = ToNativeFormat(desc.format);
        nativeDesc.dimension = desc.dimension == TextureDimension::Texture1D
                                   ? (desc.arraySize > 1 ? nvrhi::TextureDimension::Texture1DArray : nvrhi::TextureDimension::Texture1D)
                               : desc.dimension == TextureDimension::Texture3D ? nvrhi::TextureDimension::Texture3D
                               : desc.dimension == TextureDimension::TextureCube
                                   ? (desc.arraySize > 6 ? nvrhi::TextureDimension::TextureCubeArray : nvrhi::TextureDimension::TextureCube)
                               : desc.sampleCount > 1
                                   ? (desc.arraySize > 1 ? nvrhi::TextureDimension::Texture2DMSArray : nvrhi::TextureDimension::Texture2DMS)
                                   : (desc.arraySize > 1 ? nvrhi::TextureDimension::Texture2DArray : nvrhi::TextureDimension::Texture2D);
        nativeDesc.isShaderResource = HasFlag(desc.usage, TextureUsage::ShaderResource);
        nativeDesc.isRenderTarget = HasFlag(desc.usage, TextureUsage::RenderTarget) || HasFlag(desc.usage, TextureUsage::DepthStencil);
        nativeDesc.isUAV = HasFlag(desc.usage, TextureUsage::UnorderedAccess);
        nativeDesc.isShadingRateSurface = HasFlag(desc.usage, TextureUsage::ShadingRate);
        nativeDesc.isVirtual = desc.virtualResource;
        nativeDesc.initialState = ToNativeState(desc.initialState);
        nativeDesc.keepInitialState = true;

        nvrhi::TextureHandle native = m_device->createTexture(nativeDesc);
        if (!native)
            return {};
        TexturePayload* const payload = AllocatePayload<TexturePayload>(TexturePayload{static_cast<nvrhi::TextureHandle&&>(native), desc});
        if (payload == nullptr)
            return {};
        const ResourceRef resource = m_lifetime.Create(ResourceKind::Texture, payload, &DestroyPayload<TexturePayload>);
        if (!resource)
        {
            DestroyPayload<TexturePayload>(nullptr, {}, payload);
            return {};
        }

        if (initialData.subresourceCount != 0)
        {
            nvrhi::CommandListParameters parameters{};
            parameters.enableImmediateExecution = false;
            parameters.queueType = nvrhi::CommandQueue::Graphics;
            nvrhi::CommandListHandle upload = m_device->createCommandList(parameters);
            if (!upload)
            {
                static_cast<void>(m_lifetime.Release(resource));
                return {};
            }
            upload->open();
            for (u32 index = 0; index < initialData.subresourceCount; ++index)
            {
                const TextureSubresourceData& subresource = initialData.subresources[index];
                upload->writeTexture(payload->native, subresource.arraySlice, subresource.mipLevel, subresource.data,
                                     static_cast<usize>(subresource.rowPitch), static_cast<usize>(subresource.depthPitch));
            }
            upload->close();
            concurrency::ScopedLock submissionGuard(m_submissionLock);
            static_cast<void>(m_device->executeCommandList(upload, nvrhi::CommandQueue::Graphics));
            const u64 fenceValue = m_submittedFences[0].Increment();
            if (!m_signalFence(m_fenceContext, QueueType::Graphics, fenceValue))
            {
                static_cast<void>(m_device->waitForIdle());
                static_cast<void>(m_lifetime.Release(resource));
                return {};
            }
            static_cast<void>(m_lifetime.RecordUse(resource, QueueType::Graphics, fenceValue));
        }
        return CastResourceRef<TextureRef>(resource);
    }

    BufferRef CommonBackend::CreateBuffer(const BufferDesc& desc, const BufferInitData& initialData) noexcept
    {
        nvrhi::BufferDesc nativeDesc{};
        nativeDesc.byteSize = desc.size;
        nativeDesc.structStride = desc.structureStride;
        nativeDesc.format = ToNativeFormat(desc.format);
        nativeDesc.canHaveUAVs = HasFlag(desc.usage, BufferUsage::UnorderedAccess);
        nativeDesc.canHaveTypedViews = desc.format != Format::Unknown;
        nativeDesc.canHaveRawViews = HasFlag(desc.usage, BufferUsage::Raw);
        nativeDesc.isVertexBuffer = HasFlag(desc.usage, BufferUsage::Vertex);
        nativeDesc.isIndexBuffer = HasFlag(desc.usage, BufferUsage::Index);
        nativeDesc.isConstantBuffer = HasFlag(desc.usage, BufferUsage::Constant);
        nativeDesc.isDrawIndirectArgs = HasFlag(desc.usage, BufferUsage::IndirectArguments);
        nativeDesc.isAccelStructBuildInput = HasFlag(desc.usage, BufferUsage::AccelerationStructure);
        nativeDesc.isShaderBindingTable = HasFlag(desc.usage, BufferUsage::ShaderBindingTable);
        nativeDesc.isVirtual = desc.virtualResource;
        nativeDesc.initialState = ToNativeState(desc.initialState);
        nativeDesc.keepInitialState = true;
        nativeDesc.cpuAccess = desc.memoryType == MemoryType::Upload     ? nvrhi::CpuAccessMode::Write
                               : desc.memoryType == MemoryType::Readback ? nvrhi::CpuAccessMode::Read
                                                                         : nvrhi::CpuAccessMode::None;

        nvrhi::BufferHandle native = m_device->createBuffer(nativeDesc);
        if (!native)
            return {};
        BufferPayload* const payload = AllocatePayload<BufferPayload>(BufferPayload{static_cast<nvrhi::BufferHandle&&>(native), desc});
        if (payload == nullptr)
            return {};
        const ResourceRef resource = m_lifetime.Create(
            ResourceKind::Buffer, payload,
            [](void* context, ResourceRef, void* address) noexcept
            {
                auto& common = *static_cast<CommonBackend*>(context);
                auto* const buffer = static_cast<BufferPayload*>(address);
                if (buffer->mapped)
                    common.Device()->unmapBuffer(buffer->native);
                DestroyPayload<BufferPayload>(nullptr, {}, buffer);
            },
            this);
        if (!resource)
        {
            DestroyPayload<BufferPayload>(nullptr, {}, payload);
            return {};
        }
        if (initialData.data != nullptr && initialData.size != 0)
        {
            nvrhi::CommandListParameters parameters{};
            parameters.enableImmediateExecution = false;
            parameters.queueType = nvrhi::CommandQueue::Graphics;
            nvrhi::CommandListHandle upload = m_device->createCommandList(parameters);
            if (!upload)
            {
                static_cast<void>(m_lifetime.Release(resource));
                return {};
            }
            upload->open();
            upload->writeBuffer(payload->native, initialData.data, static_cast<usize>(initialData.size));
            upload->close();
            concurrency::ScopedLock submissionGuard(m_submissionLock);
            static_cast<void>(m_device->executeCommandList(upload, nvrhi::CommandQueue::Graphics));
            const u64 fenceValue = m_submittedFences[0].Increment();
            if (!m_signalFence(m_fenceContext, QueueType::Graphics, fenceValue))
            {
                static_cast<void>(m_device->waitForIdle());
                static_cast<void>(m_lifetime.Release(resource));
                return {};
            }
            static_cast<void>(m_lifetime.RecordUse(resource, QueueType::Graphics, fenceValue));
        }
        return CastResourceRef<BufferRef>(resource);
    }

    HeapRef CommonBackend::CreateHeap(const HeapDesc& desc) noexcept
    {
        nvrhi::HeapDesc nativeDesc{};
        nativeDesc.capacity = desc.size;
        nativeDesc.type = desc.memoryType == MemoryType::Upload     ? nvrhi::HeapType::Upload
                          : desc.memoryType == MemoryType::Readback ? nvrhi::HeapType::Readback
                                                                    : nvrhi::HeapType::DeviceLocal;
        nvrhi::HeapHandle native = m_device->createHeap(nativeDesc);
        if (!native)
            return {};
        HeapPayload* const payload = AllocatePayload<HeapPayload>(HeapPayload{static_cast<nvrhi::HeapHandle&&>(native), desc});
        if (payload == nullptr)
            return {};
        const ResourceRef resource = m_lifetime.Create(ResourceKind::Heap, payload, &DestroyPayload<HeapPayload>);
        if (!resource)
            DestroyPayload<HeapPayload>(nullptr, {}, payload);
        return CastResourceRef<HeapRef>(resource);
    }
    BindingLayoutRef CommonBackend::RequestBindingLayout(const BindingLayoutDesc& source) noexcept
    {
        BindingLayoutEntry canonicalEntries[MaximumBindingLayoutEntries]{};
        for (u32 index = 0; index < source.entryCount; ++index)
            canonicalEntries[index] = source.entries[index];
        for (u32 index = 1; index < source.entryCount; ++index)
        {
            const BindingLayoutEntry value = canonicalEntries[index];
            u32 destination = index;
            while (destination != 0)
            {
                const BindingLayoutEntry& previous = canonicalEntries[destination - 1u];
                const u8 previousClass = BindingTypeClass(previous.type);
                const u8 valueClass = BindingTypeClass(value.type);
                if (previousClass < valueClass || (previousClass == valueClass && previous.slot < value.slot) ||
                    (previousClass == valueClass && previous.slot == value.slot && previous.type <= value.type))
                    break;
                canonicalEntries[destination] = previous;
                --destination;
            }
            canonicalEntries[destination] = value;
        }
        BindingLayoutDesc desc = source;
        desc.entries = canonicalEntries;
        const u64 hash = HashBindingLayout(desc);
        concurrency::ScopedLock layoutGuard(m_bindingLayoutLock);
        constexpr u32 bucketCount = static_cast<u32>(sizeof(m_bindingLayoutBuckets) / sizeof(m_bindingLayoutBuckets[0]));
        const u32 bucket = static_cast<u32>(hash) & (bucketCount - 1u);
        for (u32 index = m_bindingLayoutBuckets[bucket]; index != InvalidReferenceIndex; index = m_bindingLayouts[index].next)
        {
            const BindingLayoutCacheEntry& cached = m_bindingLayouts[index];
            if (cached.hash != hash)
                continue;
            const auto* const payload = static_cast<const BindingLayoutPayload*>(m_lifetime.GetPayload(ResourceRef(cached.layout)));
            if (payload == nullptr || payload->entryCount != desc.entryCount || payload->registerSpace != desc.registerSpace ||
                payload->visibility != desc.visibility)
                continue;
            bool equal = true;
            for (u32 entry = 0; entry < desc.entryCount; ++entry)
                if (payload->entries[entry].slot != desc.entries[entry].slot ||
                    payload->entries[entry].arrayCount != desc.entries[entry].arrayCount ||
                    payload->entries[entry].type != desc.entries[entry].type)
                    equal = false;
            if (!equal)
                continue;
            if (!m_lifetime.AddRef(ResourceRef(cached.layout)))
                return {};
            return cached.layout;
        }

        nvrhi::BindingLayoutDesc nativeDesc{};
        nativeDesc.visibility = ToNativeVisibility(desc.visibility);
        nativeDesc.registerSpace = desc.registerSpace;
        nativeDesc.registerSpaceIsDescriptorSet = true;
        for (u32 index = 0; index < desc.entryCount; ++index)
        {
            nvrhi::BindingLayoutItem item{};
            item.slot = desc.entries[index].slot;
            item.type = ToNativeBindingType(desc.entries[index].type);
            item.size = static_cast<u16>(desc.entries[index].arrayCount);
            nativeDesc.bindings.push_back(item);
        }
        nvrhi::BindingLayoutHandle native = m_device->createBindingLayout(nativeDesc);
        if (!native)
            return {};

        BindingLayoutPayload* const payload = AllocatePayload<BindingLayoutPayload>();
        if (payload == nullptr)
            return {};
        payload->native = static_cast<nvrhi::BindingLayoutHandle&&>(native);
        payload->hash = hash;
        payload->entryCount = desc.entryCount;
        payload->registerSpace = desc.registerSpace;
        payload->visibility = desc.visibility;
        for (u32 index = 0; index < desc.entryCount; ++index)
            payload->entries[index] = desc.entries[index];
        const ResourceRef resource =
            m_lifetime.Create(ResourceKind::BindingLayout, payload, &DestroyPayload<BindingLayoutPayload>, nullptr, 2);
        if (!resource)
        {
            DestroyPayload<BindingLayoutPayload>(nullptr, {}, payload);
            return {};
        }
        const BindingLayoutRef layout = CastResourceRef<BindingLayoutRef>(resource);
        m_bindingLayouts.PushBack({hash, layout, m_bindingLayoutBuckets[bucket]});
        m_bindingLayoutBuckets[bucket] = m_bindingLayouts.Size() - 1u;
        return layout;
    }

    DescriptorDomainRef CommonBackend::CreateDescriptorDomain(const DescriptorDomainDesc& desc) noexcept
    {
        nvrhi::BindlessLayoutDesc nativeDesc{};
        nativeDesc.visibility = ToNativeVisibility(desc.visibility);
        nativeDesc.firstSlot = desc.firstShaderSlot;
        nativeDesc.maxCapacity = desc.capacity;
        nativeDesc.layoutType = desc.kind == DescriptorDomainKind::Samplers ? nvrhi::BindlessLayoutDesc::LayoutType::MutableSampler
                                                                            : nvrhi::BindlessLayoutDesc::LayoutType::MutableSrvUavCbv;
        nvrhi::BindingLayoutHandle nativeLayout = m_device->createBindlessLayout(nativeDesc);
        if (!nativeLayout)
            return {};
        nvrhi::DescriptorTableHandle nativeTable = m_device->createDescriptorTable(nativeLayout);
        if (!nativeTable)
            return {};
        m_device->resizeDescriptorTable(nativeTable, desc.capacity, false);
        if (nativeTable->getCapacity() != desc.capacity)
            return {};

        DescriptorDomainPayload* const payload = AllocatePayload<DescriptorDomainPayload>(desc);
        if (payload == nullptr)
            return {};
        payload->nativeLayout = static_cast<nvrhi::BindingLayoutHandle&&>(nativeLayout);
        payload->nativeTable = static_cast<nvrhi::DescriptorTableHandle&&>(nativeTable);
        payload->gpuBaseIndex = payload->nativeTable->getFirstDescriptorIndexInHeap();
        payload->stats.capacity = desc.capacity;
        payload->stats.free = desc.capacity;
        payload->slots.Reserve(desc.capacity);
        for (u32 index = 0; index < desc.capacity; ++index)
        {
            DescriptorSlot slot;
            slot.nextFree = index + 1u < desc.capacity ? index + 1u : InvalidReferenceIndex;
            payload->slots.PushBack(slot);
        }
        if (payload->slots.Size() != desc.capacity)
        {
            DestroyDescriptorDomain(this, {}, payload);
            return {};
        }
        payload->freeHead = 0;

        const ResourceRef resource = m_lifetime.Create(ResourceKind::DescriptorDomain, payload, &DestroyDescriptorDomain, this);
        if (!resource)
        {
            DestroyDescriptorDomain(this, {}, payload);
            return {};
        }
        const DescriptorDomainRef domain = CastResourceRef<DescriptorDomainRef>(resource);
        {
            concurrency::ScopedLock domainListGuard(m_descriptorDomainListLock);
            const u32 expectedSize = m_descriptorDomains.Size() + 1u;
            m_descriptorDomains.PushBack(domain);
            if (m_descriptorDomains.Size() != expectedSize)
            {
                static_cast<void>(m_lifetime.Release(resource));
                return {};
            }
        }
        return domain;
    }

    DescriptorHandle CommonBackend::AllocateDescriptor(const DescriptorDomainRef domainRef) noexcept
    {
        auto* const domain = static_cast<DescriptorDomainPayload*>(m_lifetime.GetPayload(ResourceRef(domainRef)));
        if (domain == nullptr)
            return {};
        concurrency::ScopedLock domainGuard(domain->lock);
        ReclaimDescriptorSlots(*this, *domain);
        if (domain->freeHead == InvalidReferenceIndex)
        {
            ++domain->stats.allocationFailures;
            return {};
        }
        const u32 index = domain->freeHead;
        DescriptorSlot& slot = domain->slots[index];
        domain->freeHead = slot.nextFree;
        slot.nextFree = InvalidReferenceIndex;
        slot.state = DescriptorSlotState::Allocated;
        ++domain->stats.allocated;
        if (domain->stats.peakAllocated < domain->stats.allocated)
            domain->stats.peakAllocated = domain->stats.allocated;
        domain->stats.free =
            domain->desc.capacity - domain->stats.allocated - domain->stats.pendingRetirement - domain->stats.exhaustedSlots;
        return {domain->gpuBaseIndex + index, slot.generation};
    }

    BackendStatus CommonBackend::WriteDescriptor(const DescriptorDomainRef domainRef, const DescriptorHandle descriptor,
                                                 const TextureRef textureRef, const BindingType type, const TextureViewDesc& view) noexcept
    {
        auto* const domain = static_cast<DescriptorDomainPayload*>(m_lifetime.GetPayload(ResourceRef(domainRef)));
        auto* const texture = static_cast<TexturePayload*>(m_lifetime.GetPayload(ResourceRef(textureRef)));
        if (domain == nullptr || texture == nullptr)
            return BackendStatus::Failure(FailureCode::InvalidReference, 0, "stale descriptor domain or texture reference");
        const bool shaderResource = type == BindingType::TextureShaderResource;
        const bool unorderedAccess = type == BindingType::TextureUnorderedAccess;
        if ((!shaderResource && !unorderedAccess) || (shaderResource && !HasFlag(texture->desc.usage, TextureUsage::ShaderResource)) ||
            (unorderedAccess && !HasFlag(texture->desc.usage, TextureUsage::UnorderedAccess)) ||
            view.subresources.firstMip >= texture->desc.mipCount || view.subresources.firstSlice >= texture->desc.arraySize)
            return BackendStatus::Failure(FailureCode::InvalidArgument, 0, "texture descriptor view is incompatible with the texture");
        const u32 mipCount = view.subresources.mipCount == 0xffffu ? static_cast<u32>(texture->desc.mipCount) - view.subresources.firstMip
                                                                   : view.subresources.mipCount;
        const u32 sliceCount = view.subresources.sliceCount == 0xffffu
                                   ? static_cast<u32>(texture->desc.arraySize) - view.subresources.firstSlice
                                   : view.subresources.sliceCount;
        if (mipCount == 0 || mipCount > static_cast<u32>(texture->desc.mipCount) - view.subresources.firstMip || sliceCount == 0 ||
            sliceCount > static_cast<u32>(texture->desc.arraySize) - view.subresources.firstSlice)
            return BackendStatus::Failure(FailureCode::InvalidArgument, 0, "texture descriptor view exceeds the texture");
        concurrency::ScopedLock domainGuard(domain->lock);
        u32 descriptorSlot = 0;
        if (!ResolveDescriptorSlot(*domain, descriptor, descriptorSlot))
        {
            ++domain->stats.staleHandleOperations;
            return BackendStatus::Failure(FailureCode::InvalidReference, 0, "stale descriptor handle");
        }
        DescriptorSlot& slot = domain->slots[descriptorSlot];
        if (domain->desc.kind != DescriptorDomainKind::Resources || slot.state != DescriptorSlotState::Allocated)
        {
            ++domain->stats.rejectedWrites;
            return BackendStatus::Failure(FailureCode::IncompatibleBinding, 0, "descriptor slot is not an unwritten resource slot");
        }
        if (!m_lifetime.AddRef(ResourceRef(textureRef)))
            return BackendStatus::Failure(FailureCode::InvalidReference, 0, "texture became stale during descriptor publication");

        nvrhi::BindingSetItem item{};
        item.slot = descriptorSlot;
        item.arrayElement = 0;
        item.type = ToNativeBindingType(type);
        item.resourceHandle = texture->native.Get();
        item.format = ToNativeFormat(view.format == Format::Unknown ? texture->desc.format : view.format);
        item.dimension = nvrhi::TextureDimension::Unknown;
        item.subresources = {
            view.subresources.firstMip,
            view.subresources.mipCount == 0xffffu ? nvrhi::TextureSubresourceSet::AllMipLevels : view.subresources.mipCount,
            view.subresources.firstSlice,
            view.subresources.sliceCount == 0xffffu ? nvrhi::TextureSubresourceSet::AllArraySlices : view.subresources.sliceCount};
        if (!m_device->writeDescriptorTable(domain->nativeTable, item))
        {
            static_cast<void>(m_lifetime.Release(ResourceRef(textureRef)));
            ++domain->stats.rejectedWrites;
            return BackendStatus::Failure(FailureCode::BackendFailure, 0, "NVRHI rejected the texture descriptor write");
        }
        slot.resource = ResourceRef(textureRef);
        slot.state = DescriptorSlotState::Populated;
        ++domain->stats.populated;
        return BackendStatus::Success();
    }

    BackendStatus CommonBackend::WriteDescriptor(const DescriptorDomainRef domainRef, const DescriptorHandle descriptor,
                                                 const BufferRef bufferRef, const BindingType type, const BufferViewDesc& view) noexcept
    {
        auto* const domain = static_cast<DescriptorDomainPayload*>(m_lifetime.GetPayload(ResourceRef(domainRef)));
        auto* const buffer = static_cast<BufferPayload*>(m_lifetime.GetPayload(ResourceRef(bufferRef)));
        if (domain == nullptr || buffer == nullptr)
            return BackendStatus::Failure(FailureCode::InvalidReference, 0, "stale descriptor domain or buffer reference");
        const bool constant = type == BindingType::ConstantBuffer;
        const bool typed = type == BindingType::TypedBufferShaderResource || type == BindingType::TypedBufferUnorderedAccess;
        const bool structured = type == BindingType::StructuredBufferShaderResource || type == BindingType::StructuredBufferUnorderedAccess;
        const bool raw = type == BindingType::ByteAddressBufferShaderResource || type == BindingType::ByteAddressBufferUnorderedAccess;
        const bool shaderResource = type == BindingType::TypedBufferShaderResource || type == BindingType::StructuredBufferShaderResource ||
                                    type == BindingType::ByteAddressBufferShaderResource;
        const bool unorderedAccess = type == BindingType::TypedBufferUnorderedAccess ||
                                     type == BindingType::StructuredBufferUnorderedAccess ||
                                     type == BindingType::ByteAddressBufferUnorderedAccess;
        if ((!constant && !shaderResource && !unorderedAccess) || (constant && !HasFlag(buffer->desc.usage, BufferUsage::Constant)) ||
            (shaderResource && !HasFlag(buffer->desc.usage, BufferUsage::ShaderResource)) ||
            (unorderedAccess && !HasFlag(buffer->desc.usage, BufferUsage::UnorderedAccess)) ||
            (typed && view.format == Format::Unknown && buffer->desc.format == Format::Unknown) ||
            (structured && (!HasFlag(buffer->desc.usage, BufferUsage::Structured) || buffer->desc.structureStride == 0 ||
                            (view.structureStride != 0 && view.structureStride != buffer->desc.structureStride))) ||
            (raw && !HasFlag(buffer->desc.usage, BufferUsage::Raw)) || view.offset > buffer->desc.size ||
            (view.size != 0 && view.size > buffer->desc.size - view.offset))
            return BackendStatus::Failure(FailureCode::InvalidArgument, 0, "buffer descriptor view is incompatible with the buffer");
        concurrency::ScopedLock domainGuard(domain->lock);
        u32 descriptorSlot = 0;
        if (!ResolveDescriptorSlot(*domain, descriptor, descriptorSlot))
        {
            ++domain->stats.staleHandleOperations;
            return BackendStatus::Failure(FailureCode::InvalidReference, 0, "stale descriptor handle");
        }
        DescriptorSlot& slot = domain->slots[descriptorSlot];
        if (domain->desc.kind != DescriptorDomainKind::Resources || slot.state != DescriptorSlotState::Allocated)
        {
            ++domain->stats.rejectedWrites;
            return BackendStatus::Failure(FailureCode::IncompatibleBinding, 0, "descriptor slot is not an unwritten resource slot");
        }
        if (!m_lifetime.AddRef(ResourceRef(bufferRef)))
            return BackendStatus::Failure(FailureCode::InvalidReference, 0, "buffer became stale during descriptor publication");

        nvrhi::BindingSetItem item{};
        item.slot = descriptorSlot;
        item.arrayElement = 0;
        item.type = ToNativeBindingType(type);
        item.resourceHandle = buffer->native.Get();
        item.format = ToNativeFormat(view.format == Format::Unknown ? buffer->desc.format : view.format);
        item.dimension = nvrhi::TextureDimension::Unknown;
        item.range = {view.offset, view.size == 0 ? ~0ull : view.size};
        if (!m_device->writeDescriptorTable(domain->nativeTable, item))
        {
            static_cast<void>(m_lifetime.Release(ResourceRef(bufferRef)));
            ++domain->stats.rejectedWrites;
            return BackendStatus::Failure(FailureCode::BackendFailure, 0, "NVRHI rejected the buffer descriptor write");
        }
        slot.resource = ResourceRef(bufferRef);
        slot.state = DescriptorSlotState::Populated;
        ++domain->stats.populated;
        return BackendStatus::Success();
    }

    BackendStatus CommonBackend::WriteDescriptor(const DescriptorDomainRef domainRef, const DescriptorHandle descriptor,
                                                 const SamplerStateRef samplerRef) noexcept
    {
        auto* const domain = static_cast<DescriptorDomainPayload*>(m_lifetime.GetPayload(ResourceRef(domainRef)));
        auto* const sampler = static_cast<SamplerPayload*>(m_lifetime.GetPayload(ResourceRef(samplerRef)));
        if (domain == nullptr || sampler == nullptr)
            return BackendStatus::Failure(FailureCode::InvalidReference, 0, "stale descriptor domain or sampler reference");
        concurrency::ScopedLock domainGuard(domain->lock);
        u32 descriptorSlot = 0;
        if (!ResolveDescriptorSlot(*domain, descriptor, descriptorSlot))
        {
            ++domain->stats.staleHandleOperations;
            return BackendStatus::Failure(FailureCode::InvalidReference, 0, "stale descriptor handle");
        }
        DescriptorSlot& slot = domain->slots[descriptorSlot];
        if (domain->desc.kind != DescriptorDomainKind::Samplers || slot.state != DescriptorSlotState::Allocated)
        {
            ++domain->stats.rejectedWrites;
            return BackendStatus::Failure(FailureCode::IncompatibleBinding, 0, "descriptor slot is not an unwritten sampler slot");
        }
        if (!m_lifetime.AddRef(ResourceRef(samplerRef)))
            return BackendStatus::Failure(FailureCode::InvalidReference, 0, "sampler became stale during descriptor publication");
        if (!m_device->writeDescriptorTable(domain->nativeTable, nvrhi::BindingSetItem::Sampler(descriptorSlot, sampler->native)))
        {
            static_cast<void>(m_lifetime.Release(ResourceRef(samplerRef)));
            ++domain->stats.rejectedWrites;
            return BackendStatus::Failure(FailureCode::BackendFailure, 0, "NVRHI rejected the sampler descriptor write");
        }
        slot.resource = ResourceRef(samplerRef);
        slot.state = DescriptorSlotState::Populated;
        ++domain->stats.populated;
        return BackendStatus::Success();
    }

    BackendStatus CommonBackend::RetireDescriptor(const DescriptorDomainRef domainRef, const DescriptorHandle descriptor,
                                                  const DescriptorRetirement& retirement) noexcept
    {
        auto* const domain = static_cast<DescriptorDomainPayload*>(m_lifetime.GetPayload(ResourceRef(domainRef)));
        if (domain == nullptr)
            return BackendStatus::Failure(FailureCode::InvalidReference, 0, "stale descriptor domain reference");
        concurrency::ScopedLock domainGuard(domain->lock);
        u32 descriptorSlot = 0;
        if (!ResolveDescriptorSlot(*domain, descriptor, descriptorSlot))
        {
            ++domain->stats.staleHandleOperations;
            return BackendStatus::Failure(FailureCode::InvalidReference, 0, "stale descriptor handle");
        }
        DescriptorSlot& slot = domain->slots[descriptorSlot];
        if (slot.state != DescriptorSlotState::Allocated && slot.state != DescriptorSlotState::Populated)
        {
            ++domain->stats.staleHandleOperations;
            return BackendStatus::Failure(FailureCode::InvalidReference, 0, "descriptor slot is not live");
        }
        const FenceSet fences{retirement.graphicsFence, retirement.computeFence, retirement.copyFence};
        if ((fences.graphics != 0 && !m_lifetime.RecordUse(ResourceRef(domainRef), QueueType::Graphics, fences.graphics)) ||
            (fences.compute != 0 && !m_lifetime.RecordUse(ResourceRef(domainRef), QueueType::Compute, fences.compute)) ||
            (fences.copy != 0 && !m_lifetime.RecordUse(ResourceRef(domainRef), QueueType::Copy, fences.copy)))
            return BackendStatus::Failure(FailureCode::InvalidReference, 0, "descriptor domain became stale during retirement");
        if (slot.state == DescriptorSlotState::Populated)
            --domain->stats.populated;
        --domain->stats.allocated;
        ++domain->stats.pendingRetirement;
        slot.retirement = fences;
        slot.state = DescriptorSlotState::Retiring;
        ReclaimDescriptorSlots(*this, *domain);
        return BackendStatus::Success();
    }

    DescriptorDomainStats CommonBackend::GetDescriptorDomainStats(const DescriptorDomainRef domainRef) const noexcept
    {
        const auto* const domain = static_cast<const DescriptorDomainPayload*>(m_lifetime.GetPayload(ResourceRef(domainRef)));
        if (domain == nullptr)
            return {};
        concurrency::ScopedLock domainGuard(domain->lock);
        return domain->stats;
    }

    SamplerStateRef CommonBackend::RequestSamplerState(const SamplerStateDesc& desc) noexcept
    {
        const auto address = [](const SamplerAddressMode mode) noexcept
        {
            if (mode == SamplerAddressMode::Wrap)
                return nvrhi::SamplerAddressMode::Wrap;
            if (mode == SamplerAddressMode::Mirror)
                return nvrhi::SamplerAddressMode::Mirror;
            if (mode == SamplerAddressMode::Border)
                return nvrhi::SamplerAddressMode::Border;
            return nvrhi::SamplerAddressMode::Clamp;
        };
        nvrhi::SamplerDesc nativeDesc{};
        nativeDesc.minFilter = desc.minification == FilterMode::Linear;
        nativeDesc.magFilter = desc.magnification == FilterMode::Linear;
        nativeDesc.mipFilter = desc.mip == FilterMode::Linear;
        nativeDesc.addressU = address(desc.addressU);
        nativeDesc.addressV = address(desc.addressV);
        nativeDesc.addressW = address(desc.addressW);
        nativeDesc.reductionType =
            desc.comparison == ComparisonFunction::Never ? nvrhi::SamplerReductionType::Standard : nvrhi::SamplerReductionType::Comparison;
        nativeDesc.mipBias = desc.mipLodBias;
        nativeDesc.maxAnisotropy = desc.maximumAnisotropy;
        nativeDesc.borderColor = nvrhi::Color(desc.borderColor[0], desc.borderColor[1], desc.borderColor[2], desc.borderColor[3]);
        nvrhi::SamplerHandle native = m_device->createSampler(nativeDesc);
        if (!native)
            return {};
        SamplerPayload* const payload = AllocatePayload<SamplerPayload>(SamplerPayload{static_cast<nvrhi::SamplerHandle&&>(native), desc});
        if (payload == nullptr)
            return {};
        const ResourceRef resource = m_lifetime.Create(ResourceKind::SamplerState, payload, &DestroyPayload<SamplerPayload>);
        if (!resource)
            DestroyPayload<SamplerPayload>(nullptr, {}, payload);
        return CastResourceRef<SamplerStateRef>(resource);
    }

    ShaderRef CommonBackend::CreateShader(const ShaderDesc& desc) noexcept
    {
        nvrhi::ShaderDesc nativeDesc{};
        switch (desc.stage)
        {
        case ShaderStage::Vertex:
            nativeDesc.shaderType = nvrhi::ShaderType::Vertex;
            break;
        case ShaderStage::Hull:
            nativeDesc.shaderType = nvrhi::ShaderType::Hull;
            break;
        case ShaderStage::Domain:
            nativeDesc.shaderType = nvrhi::ShaderType::Domain;
            break;
        case ShaderStage::Geometry:
            nativeDesc.shaderType = nvrhi::ShaderType::Geometry;
            break;
        case ShaderStage::Pixel:
            nativeDesc.shaderType = nvrhi::ShaderType::Pixel;
            break;
        case ShaderStage::Compute:
            nativeDesc.shaderType = nvrhi::ShaderType::Compute;
            break;
        case ShaderStage::Mesh:
            nativeDesc.shaderType = nvrhi::ShaderType::Mesh;
            break;
        case ShaderStage::Amplification:
            nativeDesc.shaderType = nvrhi::ShaderType::Amplification;
            break;
        case ShaderStage::RayGeneration:
            nativeDesc.shaderType = nvrhi::ShaderType::RayGeneration;
            break;
        case ShaderStage::Miss:
            nativeDesc.shaderType = nvrhi::ShaderType::Miss;
            break;
        case ShaderStage::ClosestHit:
            nativeDesc.shaderType = nvrhi::ShaderType::ClosestHit;
            break;
        case ShaderStage::AnyHit:
            nativeDesc.shaderType = nvrhi::ShaderType::AnyHit;
            break;
        case ShaderStage::Intersection:
            nativeDesc.shaderType = nvrhi::ShaderType::Intersection;
            break;
        case ShaderStage::Callable:
            nativeDesc.shaderType = nvrhi::ShaderType::Callable;
            break;
        }
        nativeDesc.entryName = desc.entryPoint;
        nvrhi::ShaderHandle native = m_device->createShader(nativeDesc, desc.bytecode, static_cast<usize>(desc.bytecodeSize));
        if (!native)
            return {};
        ShaderPayload* const payload =
            AllocatePayload<ShaderPayload>(ShaderPayload{static_cast<nvrhi::ShaderHandle&&>(native), desc.stage});
        if (payload == nullptr)
            return {};
        const ResourceRef resource = m_lifetime.Create(ResourceKind::Shader, payload, &DestroyPayload<ShaderPayload>);
        if (!resource)
            DestroyPayload<ShaderPayload>(nullptr, {}, payload);
        return CastResourceRef<ShaderRef>(resource);
    }

    VertexLayoutRef CommonBackend::GetVertexLayout(const VertexLayoutDesc& desc) noexcept
    {
        VertexBindingDesc canonicalBindings[MaximumVertexBindings]{};
        VertexAttributeDesc canonicalAttributes[MaximumVertexAttributes]{};
        for (u32 index = 0; index < desc.bindingCount; ++index)
        {
            canonicalBindings[index] = desc.bindings[index];
            u32 position = index;
            while (position != 0 && canonicalBindings[position - 1u].binding > canonicalBindings[position].binding)
            {
                const VertexBindingDesc temporary = canonicalBindings[position - 1u];
                canonicalBindings[position - 1u] = canonicalBindings[position];
                canonicalBindings[position] = temporary;
                --position;
            }
        }
        for (u32 index = 0; index < desc.attributeCount; ++index)
        {
            canonicalAttributes[index] = desc.attributes[index];
            u32 position = index;
            while (position != 0 && canonicalAttributes[position - 1u].location > canonicalAttributes[position].location)
            {
                const VertexAttributeDesc temporary = canonicalAttributes[position - 1u];
                canonicalAttributes[position - 1u] = canonicalAttributes[position];
                canonicalAttributes[position] = temporary;
                --position;
            }
        }
        const VertexLayoutDesc canonical{canonicalBindings, desc.bindingCount, canonicalAttributes, desc.attributeCount};
        const u64 hash = HashVertexLayout(canonical);
        concurrency::ScopedLock guard(m_vertexLayoutLock);
        const u32 bucket = static_cast<u32>(hash) & 4095u;
        for (u32 index = m_vertexLayoutBuckets[bucket]; index != InvalidReferenceIndex; index = m_vertexLayouts[index].next)
        {
            const VertexLayoutCacheEntry& cached = m_vertexLayouts[index];
            if (cached.hash != hash)
                continue;
            const auto* const payload = static_cast<const VertexLayoutPayload*>(m_lifetime.GetPayload(ResourceRef(cached.layout)));
            if (payload == nullptr || payload->bindingCount != canonical.bindingCount ||
                payload->attributeCount != canonical.attributeCount)
                continue;
            bool equal = true;
            for (u32 binding = 0; binding < canonical.bindingCount && equal; ++binding)
            {
                const VertexBindingDesc& left = payload->bindings[binding];
                const VertexBindingDesc& right = canonical.bindings[binding];
                equal = left.binding == right.binding && left.stride == right.stride && left.inputRate == right.inputRate &&
                        left.instanceStepRate == right.instanceStepRate;
            }
            for (u32 attribute = 0; attribute < canonical.attributeCount && equal; ++attribute)
            {
                const VertexAttributeDesc& left = payload->attributes[attribute].desc;
                const VertexAttributeDesc& right = canonical.attributes[attribute];
                equal = left.location == right.location && left.binding == right.binding && left.offset == right.offset &&
                        left.format == right.format && left.semanticIndex == right.semanticIndex &&
                        StringsEqual(payload->attributes[attribute].semanticName, right.semanticName);
            }
            if (equal)
                return cached.layout;
        }

        VertexLayoutPayload* const payload = AllocatePayload<VertexLayoutPayload>();
        if (payload == nullptr)
            return {};
        payload->hash = hash;
        payload->bindingCount = canonical.bindingCount;
        payload->attributeCount = canonical.attributeCount;
        for (u32 index = 0; index < canonical.bindingCount; ++index)
            payload->bindings[index] = canonical.bindings[index];
        for (u32 index = 0; index < canonical.attributeCount; ++index)
        {
            payload->attributes[index].desc = canonical.attributes[index];
            CopyString(payload->attributes[index].semanticName, MaximumVertexSemanticNameLength, canonical.attributes[index].semanticName);
            payload->attributes[index].desc.semanticName = payload->attributes[index].semanticName;
        }
        const ResourceRef resource = m_lifetime.Create(ResourceKind::VertexLayout, payload, &DestroyPayload<VertexLayoutPayload>);
        if (!resource)
        {
            DestroyPayload<VertexLayoutPayload>(nullptr, {}, payload);
            return {};
        }
        const VertexLayoutRef layout = CastResourceRef<VertexLayoutRef>(resource);
        VertexLayoutCacheEntry entry{hash, layout, m_vertexLayoutBuckets[bucket]};
        m_vertexLayouts.PushBack(entry);
        m_vertexLayoutBuckets[bucket] = m_vertexLayouts.Size() - 1u;
        return layout;
    }

    PipelineRef CommonBackend::CreateGraphicsPipeline(const GraphicsPipelineDesc& desc) noexcept
    {
        const auto shader = [this](const ShaderRef reference, const ShaderStage expected) noexcept -> nvrhi::ShaderHandle
        {
            if (!reference)
                return {};
            const auto* const payload = static_cast<const ShaderPayload*>(m_lifetime.GetPayload(ResourceRef(reference)));
            return payload != nullptr && payload->stage == expected ? payload->native : nvrhi::ShaderHandle{};
        };
        const auto layout = [this](const BindingLayoutRef reference) noexcept -> nvrhi::BindingLayoutHandle
        {
            const auto* const payload = static_cast<const BindingLayoutPayload*>(m_lifetime.GetPayload(ResourceRef(reference)));
            return payload != nullptr ? payload->native : nvrhi::BindingLayoutHandle{};
        };

        nvrhi::GraphicsPipelineDesc nativeDesc{};
        nativeDesc.VS = shader(desc.vertexShader, ShaderStage::Vertex);
        nativeDesc.HS = shader(desc.hullShader, ShaderStage::Hull);
        nativeDesc.DS = shader(desc.domainShader, ShaderStage::Domain);
        nativeDesc.GS = shader(desc.geometryShader, ShaderStage::Geometry);
        nativeDesc.PS = shader(desc.pixelShader, ShaderStage::Pixel);
        if (!nativeDesc.VS || (desc.hullShader && !nativeDesc.HS) || (desc.domainShader && !nativeDesc.DS) ||
            (desc.geometryShader && !nativeDesc.GS) || (desc.pixelShader && !nativeDesc.PS) ||
            static_cast<bool>(desc.hullShader) != static_cast<bool>(desc.domainShader))
            return {};

        nvrhi::InputLayoutHandle nativeInputLayout;
        if (desc.vertexLayout)
        {
            const auto* const source = static_cast<const VertexLayoutPayload*>(m_lifetime.GetPayload(ResourceRef(desc.vertexLayout)));
            if (source == nullptr)
                return {};
            u32 order[MaximumVertexAttributes]{};
            for (u32 index = 0; index < source->attributeCount; ++index)
            {
                order[index] = index;
                u32 position = index;
                while (position != 0 &&
                       source->attributes[order[position - 1u]].desc.location > source->attributes[order[position]].desc.location)
                {
                    const u32 temporary = order[position - 1u];
                    order[position - 1u] = order[position];
                    order[position] = temporary;
                    --position;
                }
            }
            nvrhi::VertexAttributeDesc nativeAttributes[MaximumVertexAttributes]{};
            u32 nativeAttributeCount = 0;
            for (u32 cursor = 0; cursor < source->attributeCount;)
            {
                const StoredVertexAttribute& first = source->attributes[order[cursor]];
                if (first.desc.location != cursor || first.desc.semanticIndex != 0)
                    return {};
                const VertexBindingDesc* binding = nullptr;
                for (u32 index = 0; index < source->bindingCount; ++index)
                    if (source->bindings[index].binding == first.desc.binding)
                        binding = &source->bindings[index];
                if (binding == nullptr || binding->instanceStepRate != 1)
                    return {};
                const nvrhi::Format format = ToNativeFormat(first.desc.format);
                if (format == nvrhi::Format::UNKNOWN)
                    return {};
                const u32 elementBytes = nvrhi::getFormatInfo(format).bytesPerBlock;
                u32 arraySize = 1;
                while (cursor + arraySize < source->attributeCount)
                {
                    const StoredVertexAttribute& next = source->attributes[order[cursor + arraySize]];
                    if (!StringsEqual(first.semanticName, next.semanticName))
                        break;
                    if (next.desc.semanticIndex != arraySize || next.desc.binding != first.desc.binding ||
                        next.desc.format != first.desc.format || next.desc.offset != first.desc.offset + arraySize * elementBytes)
                        return {};
                    ++arraySize;
                }
                nvrhi::VertexAttributeDesc& native = nativeAttributes[nativeAttributeCount++];
                native.name = first.semanticName;
                native.format = format;
                native.arraySize = arraySize;
                native.bufferIndex = first.desc.binding;
                native.offset = first.desc.offset;
                native.elementStride = binding->stride;
                native.isInstanced = binding->inputRate == VertexInputRate::PerInstance;
                cursor += arraySize;
            }
            nativeInputLayout = m_device->createInputLayout(nativeAttributes, nativeAttributeCount, nativeDesc.VS);
            if (!nativeInputLayout)
                return {};
            nativeDesc.inputLayout = nativeInputLayout;
        }

        nativeDesc.primType = ToNativeTopology(desc.topology);
        nativeDesc.patchControlPoints = desc.patchControlPoints;
        for (u32 index = 0; index < desc.bindingLayoutCount; ++index)
        {
            nvrhi::BindingLayoutHandle native = layout(desc.bindingLayouts[index]);
            if (!native)
                return {};
            nativeDesc.bindingLayouts.push_back(native);
        }
        for (u32 index = 0; index < desc.descriptorDomainCount; ++index)
        {
            const auto* const domain =
                static_cast<const DescriptorDomainPayload*>(m_lifetime.GetPayload(ResourceRef(desc.descriptorDomains[index])));
            if (domain == nullptr)
                return {};
            nativeDesc.bindingLayouts.push_back(domain->nativeLayout);
        }

        nvrhi::RasterState& raster = nativeDesc.renderState.rasterState;
        raster.fillMode =
            desc.rasterizer.fill == RasterFillMode::Wireframe ? nvrhi::RasterFillMode::Wireframe : nvrhi::RasterFillMode::Solid;
        raster.cullMode = desc.rasterizer.cull == RasterCullMode::None    ? nvrhi::RasterCullMode::None
                          : desc.rasterizer.cull == RasterCullMode::Front ? nvrhi::RasterCullMode::Front
                                                                          : nvrhi::RasterCullMode::Back;
        raster.frontCounterClockwise = desc.rasterizer.frontCounterClockwise;
        raster.depthClipEnable = desc.rasterizer.depthClipEnable;
        raster.scissorEnable = desc.rasterizer.scissorEnable;
        raster.multisampleEnable = desc.rasterizer.multisampleEnable;
        raster.antialiasedLineEnable = desc.rasterizer.antialiasedLineEnable;
        raster.conservativeRasterEnable = desc.rasterizer.conservativeRasterization;
        raster.depthBias = desc.rasterizer.depthBias;
        raster.depthBiasClamp = desc.rasterizer.depthBiasClamp;
        raster.slopeScaledDepthBias = desc.rasterizer.slopeScaledDepthBias;

        nvrhi::DepthStencilState& depth = nativeDesc.renderState.depthStencilState;
        depth.depthTestEnable = desc.depthStencil.depthTestEnable;
        depth.depthWriteEnable = desc.depthStencil.depthWriteEnable;
        depth.depthFunc = ToNativeComparison(desc.depthStencil.depthComparison);
        depth.stencilEnable = desc.depthStencil.stencilEnable;
        depth.stencilReadMask = desc.depthStencil.stencilReadMask;
        depth.stencilWriteMask = desc.depthStencil.stencilWriteMask;
        depth.dynamicStencilRef = desc.depthStencil.dynamicStencilReference;
        depth.stencilRefValue = desc.depthStencil.stencilReference;
        const auto stencil = [](nvrhi::DepthStencilState::StencilOpDesc& output, const StencilFaceStateDesc& input) noexcept
        {
            output.failOp = ToNativeStencil(input.fail);
            output.depthFailOp = ToNativeStencil(input.depthFail);
            output.passOp = ToNativeStencil(input.pass);
            output.stencilFunc = ToNativeComparison(input.comparison);
        };
        stencil(depth.frontFaceStencil, desc.depthStencil.front);
        stencil(depth.backFaceStencil, desc.depthStencil.back);

        nativeDesc.renderState.blendState.alphaToCoverageEnable = desc.blend.alphaToCoverageEnable;
        for (u32 index = 0; index < MaximumColorAttachments; ++index)
        {
            const BlendAttachmentStateDesc& source = desc.blend.attachments[index];
            nvrhi::BlendState::RenderTarget& target = nativeDesc.renderState.blendState.targets[index];
            target.blendEnable = source.blendEnable;
            target.srcBlend = ToNativeBlendFactor(source.sourceColor);
            target.destBlend = ToNativeBlendFactor(source.destinationColor);
            target.blendOp = ToNativeBlendOperation(source.colorOperation);
            target.srcBlendAlpha = ToNativeBlendFactor(source.sourceAlpha);
            target.destBlendAlpha = ToNativeBlendFactor(source.destinationAlpha);
            target.blendOpAlpha = ToNativeBlendOperation(source.alphaOperation);
            target.colorWriteMask = static_cast<nvrhi::ColorMask>(source.colorWriteMask & 0x0fu);
        }

        nvrhi::FramebufferInfo framebuffer;
        for (u32 index = 0; index < desc.attachments.colorCount; ++index)
        {
            const nvrhi::Format format = ToNativeFormat(desc.attachments.colorFormats[index]);
            if (format == nvrhi::Format::UNKNOWN)
                return {};
            framebuffer.colorFormats.push_back(format);
        }
        framebuffer.depthFormat = ToNativeFormat(desc.attachments.depthStencilFormat);
        framebuffer.sampleCount = desc.attachments.sampleCount;
        framebuffer.sampleQuality = desc.attachments.sampleQuality;

        nvrhi::GraphicsPipelineHandle native = m_device->createGraphicsPipeline(nativeDesc, framebuffer);
        if (!native)
            return {};
        PipelinePayload* const payload = AllocatePayload<PipelinePayload>();
        if (payload == nullptr)
            return {};
        payload->graphics = static_cast<nvrhi::GraphicsPipelineHandle&&>(native);
        payload->inputLayout = static_cast<nvrhi::InputLayoutHandle&&>(nativeInputLayout);
        payload->kind = PipelineKind::Graphics;
        if (!PopulatePipelineBindings(*this, *payload, desc.bindingLayouts, desc.bindingLayoutCount, desc.descriptorDomains,
                                      desc.descriptorDomainCount))
        {
            DestroyPipeline(this, {}, payload);
            return {};
        }
        const ResourceRef resource = m_lifetime.Create(ResourceKind::Pipeline, payload, &DestroyPipeline, this);
        if (!resource)
            DestroyPipeline(this, {}, payload);
        return CastResourceRef<PipelineRef>(resource);
    }

    PipelineRef CommonBackend::CreateComputePipeline(const ComputePipelineDesc& desc) noexcept
    {
        const auto* const shader = static_cast<const ShaderPayload*>(m_lifetime.GetPayload(ResourceRef(desc.computeShader)));
        if (shader == nullptr || shader->stage != ShaderStage::Compute)
            return {};
        nvrhi::ComputePipelineDesc nativeDesc{};
        nativeDesc.CS = shader->native;
        for (u32 index = 0; index < desc.bindingLayoutCount; ++index)
        {
            const auto* const layout =
                static_cast<const BindingLayoutPayload*>(m_lifetime.GetPayload(ResourceRef(desc.bindingLayouts[index])));
            if (layout == nullptr)
                return {};
            nativeDesc.bindingLayouts.push_back(layout->native);
        }
        for (u32 index = 0; index < desc.descriptorDomainCount; ++index)
        {
            const auto* const domain =
                static_cast<const DescriptorDomainPayload*>(m_lifetime.GetPayload(ResourceRef(desc.descriptorDomains[index])));
            if (domain == nullptr)
                return {};
            nativeDesc.bindingLayouts.push_back(domain->nativeLayout);
        }
        nvrhi::ComputePipelineHandle native = m_device->createComputePipeline(nativeDesc);
        if (!native)
            return {};
        PipelinePayload* const payload = AllocatePayload<PipelinePayload>();
        if (payload == nullptr)
            return {};
        payload->compute = static_cast<nvrhi::ComputePipelineHandle&&>(native);
        payload->kind = PipelineKind::Compute;
        if (!PopulatePipelineBindings(*this, *payload, desc.bindingLayouts, desc.bindingLayoutCount, desc.descriptorDomains,
                                      desc.descriptorDomainCount))
        {
            DestroyPipeline(this, {}, payload);
            return {};
        }
        const ResourceRef resource = m_lifetime.Create(ResourceKind::Pipeline, payload, &DestroyPipeline, this);
        if (!resource)
            DestroyPipeline(this, {}, payload);
        return CastResourceRef<PipelineRef>(resource);
    }

    PipelineRef CommonBackend::CreateRayTracingPipeline(const RayTracingPipelineDesc& desc) noexcept
    {
        const auto shader = [this](const ShaderRef reference) noexcept -> const ShaderPayload*
        { return reference ? static_cast<const ShaderPayload*>(m_lifetime.GetPayload(ResourceRef(reference))) : nullptr; };
        const auto layout = [this](const BindingLayoutRef reference) noexcept -> nvrhi::BindingLayoutHandle
        {
            if (!reference)
                return {};
            const auto* const payload = static_cast<const BindingLayoutPayload*>(m_lifetime.GetPayload(ResourceRef(reference)));
            return payload != nullptr ? payload->native : nvrhi::BindingLayoutHandle{};
        };
        nvrhi::rt::PipelineDesc nativeDesc{};
        nativeDesc.maxPayloadSize = desc.maximumPayloadBytes;
        nativeDesc.maxAttributeSize = desc.maximumAttributeBytes;
        nativeDesc.maxRecursionDepth = desc.maximumRecursionDepth;
        for (u32 index = 0; index < desc.globalBindingLayoutCount; ++index)
        {
            nvrhi::BindingLayoutHandle native = layout(desc.globalBindingLayouts[index]);
            if (!native)
                return {};
            nativeDesc.globalBindingLayouts.push_back(native);
        }
        for (u32 index = 0; index < desc.descriptorDomainCount; ++index)
        {
            const auto* const domain =
                static_cast<const DescriptorDomainPayload*>(m_lifetime.GetPayload(ResourceRef(desc.descriptorDomains[index])));
            if (domain == nullptr)
                return {};
            nativeDesc.globalBindingLayouts.push_back(domain->nativeLayout);
        }
        for (u32 index = 0; index < desc.shaderCount; ++index)
        {
            const RayTracingShaderDesc& source = desc.shaders[index];
            const ShaderPayload* const nativeShader = shader(source.shader);
            if (nativeShader == nullptr || nativeShader->stage < ShaderStage::RayGeneration || nativeShader->stage > ShaderStage::Callable)
                return {};
            nvrhi::rt::PipelineShaderDesc native;
            native.exportName = source.exportName;
            native.shader = nativeShader->native;
            if (source.localBindingLayout)
            {
                native.bindingLayout = layout(source.localBindingLayout);
                if (!native.bindingLayout)
                    return {};
            }
            nativeDesc.shaders.push_back(native);
        }
        for (u32 index = 0; index < desc.hitGroupCount; ++index)
        {
            const RayTracingHitGroupDesc& source = desc.hitGroups[index];
            nvrhi::rt::PipelineHitGroupDesc native;
            native.exportName = source.exportName;
            const ShaderPayload* closest = shader(source.closestHitShader);
            const ShaderPayload* any = shader(source.anyHitShader);
            const ShaderPayload* intersection = shader(source.intersectionShader);
            if ((closest != nullptr && closest->stage != ShaderStage::ClosestHit) ||
                (any != nullptr && any->stage != ShaderStage::AnyHit) ||
                (intersection != nullptr && intersection->stage != ShaderStage::Intersection))
                return {};
            if (closest != nullptr)
                native.closestHitShader = closest->native;
            if (any != nullptr)
                native.anyHitShader = any->native;
            if (intersection != nullptr)
                native.intersectionShader = intersection->native;
            if (source.localBindingLayout)
            {
                native.bindingLayout = layout(source.localBindingLayout);
                if (!native.bindingLayout)
                    return {};
            }
            native.isProceduralPrimitive = source.proceduralPrimitive;
            nativeDesc.hitGroups.push_back(native);
        }
        nvrhi::rt::PipelineHandle native = m_device->createRayTracingPipeline(nativeDesc);
        if (!native)
            return {};
        PipelinePayload* const payload = AllocatePayload<PipelinePayload>();
        if (payload == nullptr)
            return {};
        payload->rayTracing = static_cast<nvrhi::rt::PipelineHandle&&>(native);
        payload->kind = PipelineKind::RayTracing;
        if (!PopulatePipelineBindings(*this, *payload, desc.globalBindingLayouts, desc.globalBindingLayoutCount, desc.descriptorDomains,
                                      desc.descriptorDomainCount))
        {
            DestroyPipeline(this, {}, payload);
            return {};
        }
        const ResourceRef resource = m_lifetime.Create(ResourceKind::Pipeline, payload, &DestroyPipeline, this);
        if (!resource)
            DestroyPipeline(this, {}, payload);
        return CastResourceRef<PipelineRef>(resource);
    }
    QueryPoolRef CommonBackend::CreateQueryPool(const QueryPoolDesc&) noexcept
    {
        return {};
    }
    void CommonBackend::DestroyQueryPool(QueryPoolRef) noexcept {}
    BackendStatus CommonBackend::BindMemory(const TextureRef texture, const HeapRef heap, const u64 offset) noexcept
    {
        auto* const texturePayload = static_cast<TexturePayload*>(m_lifetime.GetPayload(ResourceRef(texture)));
        auto* const heapPayload = static_cast<HeapPayload*>(m_lifetime.GetPayload(ResourceRef(heap)));
        if (texturePayload == nullptr || heapPayload == nullptr)
            return BackendStatus::Failure(FailureCode::InvalidReference, 0, "invalid texture or heap reference");
        return m_device->bindTextureMemory(texturePayload->native, heapPayload->native, offset)
                   ? BackendStatus::Success()
                   : BackendStatus::Failure(FailureCode::BackendFailure, 0, "NVRHI failed to bind texture memory");
    }
    BackendStatus CommonBackend::BindMemory(const BufferRef buffer, const HeapRef heap, const u64 offset) noexcept
    {
        auto* const bufferPayload = static_cast<BufferPayload*>(m_lifetime.GetPayload(ResourceRef(buffer)));
        auto* const heapPayload = static_cast<HeapPayload*>(m_lifetime.GetPayload(ResourceRef(heap)));
        if (bufferPayload == nullptr || heapPayload == nullptr)
            return BackendStatus::Failure(FailureCode::InvalidReference, 0, "invalid buffer or heap reference");
        return m_device->bindBufferMemory(bufferPayload->native, heapPayload->native, offset)
                   ? BackendStatus::Success()
                   : BackendStatus::Failure(FailureCode::BackendFailure, 0, "NVRHI failed to bind buffer memory");
    }
    MemoryRequirements CommonBackend::GetMemoryRequirements(const TextureRef texture) const noexcept
    {
        const auto* const payload = static_cast<const TexturePayload*>(m_lifetime.GetPayload(ResourceRef(texture)));
        if (payload == nullptr)
            return {};
        const nvrhi::MemoryRequirements requirements = m_device->getTextureMemoryRequirements(payload->native);
        return {requirements.size, requirements.alignment, 0};
    }
    MemoryRequirements CommonBackend::GetMemoryRequirements(const BufferRef buffer) const noexcept
    {
        const auto* const payload = static_cast<const BufferPayload*>(m_lifetime.GetPayload(ResourceRef(buffer)));
        if (payload == nullptr)
            return {};
        const nvrhi::MemoryRequirements requirements = m_device->getBufferMemoryRequirements(payload->native);
        return {requirements.size, requirements.alignment, 0};
    }
    bool CommonBackend::IsResourceReferenceValid(const ResourceRef resource) const noexcept
    {
        return m_lifetime.IsValid(resource);
    }
    const void* CommonBackend::GetResourcePayload(const ResourceRef resource) const noexcept
    {
        return m_lifetime.GetPayload(resource);
    }
    void CommonBackend::AddRef(const ResourceRef resource) noexcept
    {
        static_cast<void>(m_lifetime.AddRef(resource));
    }
    i32 CommonBackend::Release(const ResourceRef resource) noexcept
    {
        return m_lifetime.Release(resource);
    }
    ResourceLifetimeStats CommonBackend::GetResourceLifetimeStats() const noexcept
    {
        return m_lifetime.GetStats();
    }
    CommandListRef CommonBackend::CreateCommandList(const CommandListType type, const u64) noexcept
    {
        nvrhi::CommandListParameters parameters{};
        parameters.enableImmediateExecution = false;
        parameters.queueType = ToNativeQueue(type);
        nvrhi::CommandListHandle native = m_device->createCommandList(parameters);
        if (!native)
            return {};
        native->open();
        native->setEnableAutomaticBarriers(false);
        CommandListPayload* const payload = AllocatePayload<CommandListPayload>(static_cast<nvrhi::CommandListHandle&&>(native), type);
        if (payload == nullptr)
            return {};
        const ResourceRef resource = m_lifetime.Create(
            ResourceKind::CommandList, payload,
            [](void* context, ResourceRef, void* address) noexcept
            {
                auto& common = *static_cast<CommonBackend*>(context);
                auto* const command = static_cast<CommandListPayload*>(address);
                for (const ResourceRef used : command->resources)
                    static_cast<void>(common.Release(used));
                DestroyPayload<CommandListPayload>(nullptr, {}, command);
            },
            this);
        if (!resource)
            DestroyPayload<CommandListPayload>(nullptr, {}, payload);
        return CastResourceRef<CommandListRef>(resource);
    }
    CommandListType CommonBackend::GetCommandListType(const CommandListRef commandList) const noexcept
    {
        const auto* const payload = static_cast<const CommandListPayload*>(m_lifetime.GetPayload(ResourceRef(commandList)));
        return payload != nullptr && payload->open ? payload->type : CommandListType::None;
    }
    void CommonBackend::DiscardCommandList(const CommandListRef commandList) noexcept
    {
        const ResourceRef resource(commandList);
        auto* const payload = static_cast<CommandListPayload*>(m_lifetime.GetPayload(resource));
        if (payload == nullptr)
            return;
        if (payload->open)
        {
            payload->native->close();
            payload->open = false;
        }
        static_cast<void>(m_lifetime.Release(resource));
    }
    BackendStatus CommonBackend::CloseAndSubmitCommandLists(const char*, const containers::ArraySpan<const CommandListRef> commandLists,
                                                            const CommandListSyncType sync, GpuFence& completion) noexcept
    {
        concurrency::ScopedLock submissionGuard(m_submissionLock);
        completion = {};
        nvrhi::ICommandList* nativeLists[3][MaximumCommandListsPerSubmission]{};
        CommandListPayload* payloads[MaximumCommandListsPerSubmission]{};
        ResourceRef references[MaximumCommandListsPerSubmission]{};
        u32 queueCounts[3]{};

        for (u32 index = 0; index < commandLists.Size(); ++index)
        {
            const ResourceRef resource(commandLists[index]);
            auto* const payload = static_cast<CommandListPayload*>(m_lifetime.GetPayload(resource));
            if (payload == nullptr || !payload->open)
                return BackendStatus::Failure(FailureCode::InvalidCommandList, 0, "submission contains a stale or closed command list");
            const u32 queue = QueueIndex(GetQueueType(payload->type));
            nativeLists[queue][queueCounts[queue]++] = payload->native;
            payloads[index] = payload;
            references[index] = resource;
        }
        if (sync == CommandListSyncType::None)
        {
            u32 activeQueues = 0;
            for (u32 queue = 0; queue < 3; ++queue)
                activeQueues += queueCounts[queue] != 0 ? 1u : 0u;
            if (activeQueues > 1)
                return BackendStatus::Failure(
                    FailureCode::InvalidArgument, 0,
                    "a submission spanning multiple queues requires an explicit fork or join synchronization mode");
        }

        for (u32 index = 0; index < commandLists.Size(); ++index)
        {
            payloads[index]->native->commitBarriers();
            payloads[index]->native->close();
            payloads[index]->open = false;
        }

        const auto executeQueue = [&](const QueueType queue) noexcept
        {
            const u32 index = QueueIndex(queue);
            if (queueCounts[index] == 0)
                return;
            m_submittedInstances[index] =
                m_device->executeCommandLists(nativeLists[index], queueCounts[index], static_cast<nvrhi::CommandQueue>(index));
        };
        if (sync == CommandListSyncType::ForkAsyncCompute)
        {
            executeQueue(QueueType::Graphics);
            if (m_submittedInstances[0] != 0)
                m_device->queueWaitForCommandList(nvrhi::CommandQueue::Compute, nvrhi::CommandQueue::Graphics, m_submittedInstances[0]);
            executeQueue(QueueType::Compute);
            executeQueue(QueueType::Copy);
        }
        else if (sync == CommandListSyncType::JoinAsyncCompute)
        {
            executeQueue(QueueType::Compute);
            if (m_submittedInstances[1] != 0)
                m_device->queueWaitForCommandList(nvrhi::CommandQueue::Graphics, nvrhi::CommandQueue::Compute, m_submittedInstances[1]);
            executeQueue(QueueType::Graphics);
            executeQueue(QueueType::Copy);
        }
        else
        {
            executeQueue(QueueType::Graphics);
            executeQueue(QueueType::Compute);
            executeQueue(QueueType::Copy);
        }

        GpuFence queueFences[3]{};
        for (u32 queueIndex = 0; queueIndex < 3; ++queueIndex)
        {
            bool signalQueue = queueCounts[queueIndex] != 0;
            if (sync == CommandListSyncType::ForkAsyncCompute && queueIndex == QueueIndex(QueueType::Compute))
                signalQueue = true;
            if (sync == CommandListSyncType::JoinAsyncCompute && queueIndex == QueueIndex(QueueType::Graphics))
                signalQueue = true;
            if (!signalQueue)
                continue;
            const QueueType queue = static_cast<QueueType>(queueIndex);
            const u64 value = m_submittedFences[queueIndex].Increment();
            if (!m_signalFence(m_fenceContext, queue, value))
                return BackendStatus::Failure(FailureCode::DeviceLost, 0, "failed to signal a submission fence");
            queueFences[queueIndex] = {queue, value};
        }

        for (u32 index = 0; index < commandLists.Size(); ++index)
        {
            CommandListPayload& payload = *payloads[index];
            const QueueType queue = GetQueueType(payload.type);
            const GpuFence fence = queueFences[QueueIndex(queue)];
            for (const ResourceRef resource : payload.resources)
            {
                static_cast<void>(m_lifetime.RecordUse(resource, queue, fence.value));
                static_cast<void>(m_lifetime.Release(resource));
            }
            payload.resources.Clear();
            static_cast<void>(m_lifetime.RecordUse(references[index], queue, fence.value));
            static_cast<void>(m_lifetime.Release(references[index]));
        }

        completion = sync == CommandListSyncType::ForkAsyncCompute   ? queueFences[QueueIndex(QueueType::Compute)]
                     : sync == CommandListSyncType::JoinAsyncCompute ? queueFences[QueueIndex(QueueType::Graphics)]
                                                                     : queueFences[queueCounts[0] != 0   ? 0u
                                                                                   : queueCounts[1] != 0 ? 1u
                                                                                                         : 2u];
        return BackendStatus::Success();
    }
    GpuFence CommonBackend::GetGpuFence(CommandListRef) const noexcept
    {
        return {};
    }
    bool CommonBackend::IsGpuFenceComplete(const GpuFence fence) const noexcept
    {
        return !fence.IsValid() || m_fenceComplete(m_fenceContext, fence.queue, fence.value);
    }
    BackendStatus CommonBackend::WaitForGpuFence(const GpuFence fence, const u64 timeout) noexcept
    {
        return m_waitFence(m_fenceContext, fence.queue, fence.value, timeout)
                   ? BackendStatus::Success()
                   : BackendStatus::Failure(FailureCode::DeviceLost, 0, "GPU fence wait failed or timed out");
    }

    BackendStatus CommonBackend::SetPipeline(const CommandListRef commandList, const PipelineRef pipeline) noexcept
    {
        auto* const command = static_cast<CommandListPayload*>(m_lifetime.GetPayload(ResourceRef(commandList)));
        const auto* const nativePipeline = static_cast<const PipelinePayload*>(m_lifetime.GetPayload(ResourceRef(pipeline)));
        if (command == nullptr || !command->open || nativePipeline == nullptr)
            return BackendStatus::Failure(FailureCode::InvalidReference, 0, "invalid command list or pipeline");
        if ((command->type == CommandListType::Compute && nativePipeline->kind != PipelineKind::Compute) ||
            (command->type != CommandListType::Default && command->type != CommandListType::Compute))
            return BackendStatus::Failure(FailureCode::IncompatibleBinding, 0, "pipeline kind is incompatible with the command list");
        if (!TrackCommandResource(*this, *command, ResourceRef(pipeline)))
            return BackendStatus::Failure(FailureCode::OutOfMemory, 0, "failed to retain a recorded pipeline");
        if (command->pipeline != pipeline)
            command->pushConstantSize = 0;
        command->pipeline = pipeline;
        return BackendStatus::Success();
    }

    BackendStatus CommonBackend::SetupRenderTargets(const CommandListRef commandList, const RenderTargetSetup& setup) noexcept
    {
        auto* const command = static_cast<CommandListPayload*>(m_lifetime.GetPayload(ResourceRef(commandList)));
        if (command == nullptr || !command->open || command->type != CommandListType::Default)
            return BackendStatus::Failure(FailureCode::InvalidCommandList, 0, "render targets require an open graphics command list");
        nvrhi::FramebufferDesc nativeDesc;
        for (u32 index = 0; index < setup.colorTargetCount; ++index)
        {
            const RenderTargetAttachment& attachment = setup.colorTargets[index];
            const auto* const texture = static_cast<const TexturePayload*>(m_lifetime.GetPayload(ResourceRef(attachment.texture)));
            if (texture == nullptr || !HasFlag(texture->desc.usage, TextureUsage::RenderTarget) ||
                attachment.mipLevel >= texture->desc.mipCount || attachment.arraySlice >= texture->desc.arraySize)
                return BackendStatus::Failure(FailureCode::IncompatibleBinding, 0, "invalid color render-target attachment");
            nvrhi::FramebufferAttachment native;
            native.texture = texture->native;
            native.subresources = nvrhi::TextureSubresourceSet(attachment.mipLevel, 1, attachment.arraySlice, 1);
            native.format = attachment.format == Format::Unknown ? nvrhi::Format::UNKNOWN : ToNativeFormat(attachment.format);
            if (attachment.format != Format::Unknown && native.format == nvrhi::Format::UNKNOWN)
                return BackendStatus::Failure(FailureCode::InvalidArgument, 0, "invalid color attachment view format");
            nativeDesc.colorAttachments.push_back(native);
            if (!TrackCommandResource(*this, *command, ResourceRef(attachment.texture)))
                return BackendStatus::Failure(FailureCode::OutOfMemory, 0, "failed to retain a color render target");
        }
        if (setup.depthStencilTarget.texture)
        {
            const RenderTargetAttachment& attachment = setup.depthStencilTarget;
            const auto* const texture = static_cast<const TexturePayload*>(m_lifetime.GetPayload(ResourceRef(attachment.texture)));
            if (texture == nullptr || !HasFlag(texture->desc.usage, TextureUsage::DepthStencil) ||
                attachment.mipLevel >= texture->desc.mipCount || attachment.arraySlice >= texture->desc.arraySize)
                return BackendStatus::Failure(FailureCode::IncompatibleBinding, 0, "invalid depth-stencil attachment");
            nvrhi::FramebufferAttachment native;
            native.texture = texture->native;
            native.subresources = nvrhi::TextureSubresourceSet(attachment.mipLevel, 1, attachment.arraySlice, 1);
            native.format = attachment.format == Format::Unknown ? nvrhi::Format::UNKNOWN : ToNativeFormat(attachment.format);
            native.isReadOnly = attachment.readOnly;
            if (attachment.format != Format::Unknown && native.format == nvrhi::Format::UNKNOWN)
                return BackendStatus::Failure(FailureCode::InvalidArgument, 0, "invalid depth attachment view format");
            nativeDesc.depthAttachment = native;
            if (!TrackCommandResource(*this, *command, ResourceRef(attachment.texture)))
                return BackendStatus::Failure(FailureCode::OutOfMemory, 0, "failed to retain a depth target");
        }
        nvrhi::FramebufferHandle framebuffer = m_device->createFramebuffer(nativeDesc);
        if (!framebuffer)
            return BackendStatus::Failure(FailureCode::BackendFailure, 0, "failed to create the render-target framebuffer");
        command->framebuffer = static_cast<nvrhi::FramebufferHandle&&>(framebuffer);
        command->renderTargetsSet = true;
        return BackendStatus::Success();
    }

    BackendStatus CommonBackend::SetViewport(const CommandListRef commandList, const ViewportDesc& viewport) noexcept
    {
        auto* const command = static_cast<CommandListPayload*>(m_lifetime.GetPayload(ResourceRef(commandList)));
        if (command == nullptr || !command->open || command->type != CommandListType::Default)
            return BackendStatus::Failure(FailureCode::InvalidCommandList, 0, "viewport requires an open graphics command list");
        command->viewport = viewport;
        command->viewportSet = true;
        return BackendStatus::Success();
    }

    BackendStatus CommonBackend::SetScissors(const CommandListRef commandList, const Rect& rect) noexcept
    {
        auto* const command = static_cast<CommandListPayload*>(m_lifetime.GetPayload(ResourceRef(commandList)));
        if (command == nullptr || !command->open || command->type != CommandListType::Default)
            return BackendStatus::Failure(FailureCode::InvalidCommandList, 0, "scissors require an open graphics command list");
        command->scissors = rect;
        command->scissorsSet = true;
        return BackendStatus::Success();
    }

    BackendStatus CommonBackend::BindVertexBuffers(const CommandListRef commandList, const u32 startIndex,
                                                   const containers::ArraySpan<const VertexBufferBinding> bindings) noexcept
    {
        auto* const command = static_cast<CommandListPayload*>(m_lifetime.GetPayload(ResourceRef(commandList)));
        if (command == nullptr || !command->open || command->type != CommandListType::Default)
            return BackendStatus::Failure(FailureCode::InvalidCommandList, 0, "vertex buffers require an open graphics command list");
        for (u32 index = 0; index < bindings.Size(); ++index)
        {
            const VertexBufferBinding& binding = bindings[index];
            const auto* const buffer = static_cast<const BufferPayload*>(m_lifetime.GetPayload(ResourceRef(binding.buffer)));
            if (buffer == nullptr || !HasFlag(buffer->desc.usage, BufferUsage::Vertex) || binding.offset >= buffer->desc.size)
                return BackendStatus::Failure(FailureCode::IncompatibleBinding, 0, "invalid vertex-buffer binding");
            if (!TrackCommandResource(*this, *command, ResourceRef(binding.buffer)))
                return BackendStatus::Failure(FailureCode::OutOfMemory, 0, "failed to retain a vertex buffer");
            const u32 destination = startIndex + index;
            command->vertexBuffers[destination] = binding;
            command->vertexBufferSet[destination] = true;
            if (command->vertexBufferCount <= destination)
                command->vertexBufferCount = destination + 1u;
        }
        return BackendStatus::Success();
    }

    BackendStatus CommonBackend::BindIndexBuffer(const CommandListRef commandList, const IndexBufferBinding& binding) noexcept
    {
        auto* const command = static_cast<CommandListPayload*>(m_lifetime.GetPayload(ResourceRef(commandList)));
        const auto* const buffer = static_cast<const BufferPayload*>(m_lifetime.GetPayload(ResourceRef(binding.buffer)));
        const u64 alignment = binding.format == IndexFormat::UInt32 ? 4u : 2u;
        if (command == nullptr || !command->open || command->type != CommandListType::Default || buffer == nullptr ||
            !HasFlag(buffer->desc.usage, BufferUsage::Index) || binding.offset >= buffer->desc.size ||
            (binding.offset & (alignment - 1u)) != 0 || binding.offset > 0xffffffffu)
            return BackendStatus::Failure(FailureCode::IncompatibleBinding, 0, "invalid index-buffer binding");
        if (!TrackCommandResource(*this, *command, ResourceRef(binding.buffer)))
            return BackendStatus::Failure(FailureCode::OutOfMemory, 0, "failed to retain an index buffer");
        command->indexBuffer = binding;
        command->indexBufferSet = true;
        return BackendStatus::Success();
    }

    BackendStatus CommonBackend::BindIndirectArguments(const CommandListRef commandList, const BufferRef arguments,
                                                       const BufferRef count) noexcept
    {
        auto* const command = static_cast<CommandListPayload*>(m_lifetime.GetPayload(ResourceRef(commandList)));
        const auto* const argumentBuffer = static_cast<const BufferPayload*>(m_lifetime.GetPayload(ResourceRef(arguments)));
        const auto* const countBuffer = count ? static_cast<const BufferPayload*>(m_lifetime.GetPayload(ResourceRef(count))) : nullptr;
        if (command == nullptr || !command->open || argumentBuffer == nullptr ||
            !HasFlag(argumentBuffer->desc.usage, BufferUsage::IndirectArguments) ||
            (count && (countBuffer == nullptr || !HasFlag(countBuffer->desc.usage, BufferUsage::IndirectArguments))))
            return BackendStatus::Failure(FailureCode::IncompatibleBinding, 0, "invalid indirect-argument buffers");
        if (!TrackCommandResource(*this, *command, ResourceRef(arguments)) ||
            (count && !TrackCommandResource(*this, *command, ResourceRef(count))))
            return BackendStatus::Failure(FailureCode::OutOfMemory, 0, "failed to retain indirect-argument buffers");
        command->indirectArguments = arguments;
        command->indirectCount = count;
        return BackendStatus::Success();
    }

    BackendStatus CommonBackend::SetPushConstants(const CommandListRef commandList, const void* const data, const u32 size) noexcept
    {
        auto* const command = static_cast<CommandListPayload*>(m_lifetime.GetPayload(ResourceRef(commandList)));
        if (command == nullptr || !command->open || data == nullptr || size > sizeof(command->pushConstants))
            return BackendStatus::Failure(FailureCode::InvalidArgument, 0, "invalid push-constant recording request");
        const u8* const source = static_cast<const u8*>(data);
        for (u32 index = 0; index < size; ++index)
            command->pushConstants[index] = source[index];
        command->pushConstantSize = size;
        return BackendStatus::Success();
    }

    BackendStatus CommonBackend::DrawPrimitive(const CommandListRef commandList, const DrawArguments& arguments) noexcept
    {
        auto* const command = static_cast<CommandListPayload*>(m_lifetime.GetPayload(ResourceRef(commandList)));
        if (command == nullptr || !command->open || command->type != CommandListType::Default)
            return BackendStatus::Failure(FailureCode::InvalidCommandList, 0, "draw requires an open graphics command list");
        const BackendStatus status = SetupForGraphics(*this, *command);
        if (!status)
            return status;
        nvrhi::DrawArguments native;
        native.vertexCount = arguments.vertexCount;
        native.instanceCount = arguments.instanceCount;
        native.startVertexLocation = arguments.firstVertex;
        native.startInstanceLocation = arguments.firstInstance;
        command->native->draw(native);
        return BackendStatus::Success();
    }

    BackendStatus CommonBackend::DrawIndexedPrimitive(const CommandListRef commandList, const DrawIndexedArguments& arguments) noexcept
    {
        auto* const command = static_cast<CommandListPayload*>(m_lifetime.GetPayload(ResourceRef(commandList)));
        if (command == nullptr || !command->open || command->type != CommandListType::Default || !command->indexBufferSet)
            return BackendStatus::Failure(FailureCode::MissingBinding, 0, "indexed draw requires an index buffer");
        const BackendStatus status = SetupForGraphics(*this, *command);
        if (!status)
            return status;
        nvrhi::DrawArguments native;
        native.vertexCount = arguments.indexCount;
        native.instanceCount = arguments.instanceCount;
        native.startIndexLocation = arguments.firstIndex;
        native.startVertexLocation = static_cast<u32>(arguments.baseVertex);
        native.startInstanceLocation = arguments.firstInstance;
        command->native->drawIndexed(native);
        return BackendStatus::Success();
    }

    BackendStatus CommonBackend::DrawPrimitiveIndirect(const CommandListRef commandList, const u64 argumentsOffset,
                                                       const u32 commandCount) noexcept
    {
        auto* const command = static_cast<CommandListPayload*>(m_lifetime.GetPayload(ResourceRef(commandList)));
        const auto* const arguments =
            command != nullptr ? static_cast<const BufferPayload*>(m_lifetime.GetPayload(ResourceRef(command->indirectArguments)))
                               : nullptr;
        const u64 bytes = static_cast<u64>(sizeof(IndirectDrawArguments)) * commandCount;
        if (command == nullptr || arguments == nullptr || argumentsOffset + bytes < argumentsOffset ||
            argumentsOffset + bytes > arguments->desc.size)
            return BackendStatus::Failure(FailureCode::MissingBinding, 0, "indirect draw arguments are not fully bound");
        const BackendStatus status = SetupForGraphics(*this, *command);
        if (!status)
            return status;
        command->native->drawIndirect(static_cast<u32>(argumentsOffset), commandCount);
        return BackendStatus::Success();
    }

    BackendStatus CommonBackend::DrawIndexedPrimitiveIndirect(const CommandListRef commandList, const u64 argumentsOffset,
                                                              const u32 commandCount) noexcept
    {
        auto* const command = static_cast<CommandListPayload*>(m_lifetime.GetPayload(ResourceRef(commandList)));
        const auto* const arguments =
            command != nullptr ? static_cast<const BufferPayload*>(m_lifetime.GetPayload(ResourceRef(command->indirectArguments)))
                               : nullptr;
        const u64 bytes = static_cast<u64>(sizeof(IndirectDrawIndexedArguments)) * commandCount;
        if (command == nullptr || !command->indexBufferSet || arguments == nullptr || argumentsOffset + bytes < argumentsOffset ||
            argumentsOffset + bytes > arguments->desc.size)
            return BackendStatus::Failure(FailureCode::MissingBinding, 0, "indexed indirect draw state is incomplete");
        const BackendStatus status = SetupForGraphics(*this, *command);
        if (!status)
            return status;
        command->native->drawIndexedIndirect(static_cast<u32>(argumentsOffset), commandCount);
        return BackendStatus::Success();
    }

    BackendStatus CommonBackend::DrawIndexedPrimitiveIndirectCount(const CommandListRef commandList, const u64 argumentsOffset,
                                                                   const u64 countOffset, const u32 maximumCommandCount) noexcept
    {
        auto* const command = static_cast<CommandListPayload*>(m_lifetime.GetPayload(ResourceRef(commandList)));
        const auto* const arguments =
            command != nullptr ? static_cast<const BufferPayload*>(m_lifetime.GetPayload(ResourceRef(command->indirectArguments)))
                               : nullptr;
        const auto* const count =
            command != nullptr ? static_cast<const BufferPayload*>(m_lifetime.GetPayload(ResourceRef(command->indirectCount))) : nullptr;
        const u64 bytes = static_cast<u64>(sizeof(IndirectDrawIndexedArguments)) * maximumCommandCount;
        if (command == nullptr || !command->indexBufferSet || arguments == nullptr || count == nullptr ||
            argumentsOffset + bytes < argumentsOffset || argumentsOffset + bytes > arguments->desc.size ||
            countOffset + sizeof(u32) < countOffset || countOffset + sizeof(u32) > count->desc.size)
            return BackendStatus::Failure(FailureCode::MissingBinding, 0, "counted indirect draw state is incomplete");
        const BackendStatus status = SetupForGraphics(*this, *command);
        if (!status)
            return status;
        command->native->drawIndexedIndirectCount(static_cast<u32>(argumentsOffset), static_cast<u32>(countOffset), maximumCommandCount);
        return BackendStatus::Success();
    }

    BackendStatus CommonBackend::DispatchCompute(const CommandListRef commandList, const u32 groupCountX, const u32 groupCountY,
                                                 const u32 groupCountZ) noexcept
    {
        auto* const command = static_cast<CommandListPayload*>(m_lifetime.GetPayload(ResourceRef(commandList)));
        if (command == nullptr || !command->open ||
            (command->type != CommandListType::Default && command->type != CommandListType::Compute))
            return BackendStatus::Failure(FailureCode::InvalidCommandList, 0, "dispatch requires an open graphics or compute command list");
        const BackendStatus status = SetupForCompute(*this, *command);
        if (!status)
            return status;
        command->native->dispatch(groupCountX, groupCountY, groupCountZ);
        return BackendStatus::Success();
    }

    BackendStatus CommonBackend::DispatchIndirectCompute(const CommandListRef commandList, const u64 argumentsOffset) noexcept
    {
        auto* const command = static_cast<CommandListPayload*>(m_lifetime.GetPayload(ResourceRef(commandList)));
        const auto* const arguments =
            command != nullptr ? static_cast<const BufferPayload*>(m_lifetime.GetPayload(ResourceRef(command->indirectArguments)))
                               : nullptr;
        if (command == nullptr || arguments == nullptr || argumentsOffset + sizeof(IndirectDispatchArguments) < argumentsOffset ||
            argumentsOffset + sizeof(IndirectDispatchArguments) > arguments->desc.size)
            return BackendStatus::Failure(FailureCode::MissingBinding, 0, "indirect dispatch arguments are not fully bound");
        const BackendStatus status = SetupForCompute(*this, *command);
        if (!status)
            return status;
        command->native->dispatchIndirect(static_cast<u32>(argumentsOffset));
        return BackendStatus::Success();
    }

    BackendStatus CommonBackend::WriteBuffer(const CommandListRef commandList, const BufferRef buffer, const void* const data,
                                             const u64 size, const u64 destinationOffset) noexcept
    {
        auto* const command = static_cast<CommandListPayload*>(m_lifetime.GetPayload(ResourceRef(commandList)));
        auto* const resource = static_cast<BufferPayload*>(m_lifetime.GetPayload(ResourceRef(buffer)));
        if (command == nullptr || !command->open)
            return BackendStatus::Failure(FailureCode::InvalidCommandList, 0, "buffer upload requires an open command list");
        if (resource == nullptr)
            return BackendStatus::Failure(FailureCode::InvalidReference, 0, "buffer upload references a stale buffer");
        if (destinationOffset > resource->desc.size || size > resource->desc.size - destinationOffset)
            return BackendStatus::Failure(FailureCode::InvalidArgument, 0, "buffer upload exceeds the destination buffer");
        if (!TrackCommandResource(*this, *command, ResourceRef(buffer)))
            return BackendStatus::Failure(FailureCode::OutOfMemory, 0, "failed to retain a buffer upload destination");
        command->native->commitBarriers();
        command->native->writeBuffer(resource->native, data, static_cast<usize>(size), destinationOffset);
        return BackendStatus::Success();
    }

    BackendStatus CommonBackend::WriteTexture(const CommandListRef commandList, const TextureRef texture,
                                              const TextureSubresourceData& data) noexcept
    {
        auto* const command = static_cast<CommandListPayload*>(m_lifetime.GetPayload(ResourceRef(commandList)));
        auto* const resource = static_cast<TexturePayload*>(m_lifetime.GetPayload(ResourceRef(texture)));
        if (command == nullptr || !command->open)
            return BackendStatus::Failure(FailureCode::InvalidCommandList, 0, "texture upload requires an open command list");
        if (resource == nullptr)
            return BackendStatus::Failure(FailureCode::InvalidReference, 0, "texture upload references a stale texture");
        if (data.mipLevel >= resource->desc.mipCount || data.arraySlice >= resource->desc.arraySize)
            return BackendStatus::Failure(FailureCode::InvalidArgument, 0, "texture upload selects an invalid subresource");
        if (!TrackCommandResource(*this, *command, ResourceRef(texture)))
            return BackendStatus::Failure(FailureCode::OutOfMemory, 0, "failed to retain a texture upload destination");
        command->native->commitBarriers();
        command->native->writeTexture(resource->native, data.arraySlice, data.mipLevel, data.data, static_cast<usize>(data.rowPitch),
                                      static_cast<usize>(data.depthPitch));
        return BackendStatus::Success();
    }

    BackendStatus CommonBackend::CopyBuffer(const CommandListRef commandList, const BufferRef destination, const u64 destinationOffset,
                                            const BufferRef source, const u64 sourceOffset, const u64 size) noexcept
    {
        auto* const command = static_cast<CommandListPayload*>(m_lifetime.GetPayload(ResourceRef(commandList)));
        auto* const destinationPayload = static_cast<BufferPayload*>(m_lifetime.GetPayload(ResourceRef(destination)));
        auto* const sourcePayload = static_cast<BufferPayload*>(m_lifetime.GetPayload(ResourceRef(source)));
        if (command == nullptr || !command->open)
            return BackendStatus::Failure(FailureCode::InvalidCommandList, 0, "buffer copy requires an open command list");
        if (destinationPayload == nullptr || sourcePayload == nullptr)
            return BackendStatus::Failure(FailureCode::InvalidReference, 0, "buffer copy references a stale buffer");
        if (destinationOffset > destinationPayload->desc.size || size > destinationPayload->desc.size - destinationOffset ||
            sourceOffset > sourcePayload->desc.size || size > sourcePayload->desc.size - sourceOffset)
            return BackendStatus::Failure(FailureCode::InvalidArgument, 0, "buffer copy exceeds a source or destination buffer");
        const ResourceRef references[] = {ResourceRef(destination), ResourceRef(source)};
        for (const ResourceRef reference : references)
            if (!TrackCommandResource(*this, *command, reference))
                return BackendStatus::Failure(FailureCode::OutOfMemory, 0, "failed to retain a buffer copy resource");
        command->native->commitBarriers();
        command->native->copyBuffer(destinationPayload->native, destinationOffset, sourcePayload->native, sourceOffset, size);
        return BackendStatus::Success();
    }

    BackendStatus CommonBackend::LockBuffer(const BufferRef buffer, const u64 offset, const u64 size, void*& data) noexcept
    {
        data = nullptr;
        auto* const payload = static_cast<BufferPayload*>(m_lifetime.GetPayload(ResourceRef(buffer)));
        if (payload == nullptr)
            return BackendStatus::Failure(FailureCode::InvalidReference, 0, "buffer lock references a stale buffer");
        if (payload->desc.memoryType == MemoryType::DeviceLocal)
            return BackendStatus::Failure(FailureCode::InvalidArgument, 0, "device-local buffers cannot be CPU locked");
        if (payload->mapped)
            return BackendStatus::Failure(FailureCode::Busy, 0, "buffer is already CPU locked");
        if (offset > payload->desc.size || size > payload->desc.size - offset)
            return BackendStatus::Failure(FailureCode::InvalidArgument, 0, "buffer lock exceeds the resource");
        const nvrhi::CpuAccessMode access =
            payload->desc.memoryType == MemoryType::Readback ? nvrhi::CpuAccessMode::Read : nvrhi::CpuAccessMode::Write;
        void* const base = m_device->mapBuffer(payload->native, access);
        if (base == nullptr)
            return BackendStatus::Failure(FailureCode::BackendFailure, 0, "NVRHI failed to map a buffer");
        payload->mapped = true;
        data = static_cast<u8*>(base) + offset;
        return BackendStatus::Success();
    }

    void CommonBackend::UnlockBuffer(const BufferRef buffer) noexcept
    {
        auto* const payload = static_cast<BufferPayload*>(m_lifetime.GetPayload(ResourceRef(buffer)));
        if (payload == nullptr || !payload->mapped)
            return;
        m_device->unmapBuffer(payload->native);
        payload->mapped = false;
    }

    BackendStatus CommonBackend::TransitionTexture(const CommandListRef commandList, const TextureRef texture, const ResourceState,
                                                   const ResourceState after, const SubresourceRange& range) noexcept
    {
        auto* const command = static_cast<CommandListPayload*>(m_lifetime.GetPayload(ResourceRef(commandList)));
        auto* const resource = static_cast<TexturePayload*>(m_lifetime.GetPayload(ResourceRef(texture)));
        if (command == nullptr || !command->open)
            return BackendStatus::Failure(FailureCode::InvalidCommandList, 0, "texture transition requires an open command list");
        if (resource == nullptr)
            return BackendStatus::Failure(FailureCode::InvalidReference, 0, "texture transition references a stale texture");
        if (!TrackCommandResource(*this, *command, ResourceRef(texture)))
            return BackendStatus::Failure(FailureCode::OutOfMemory, 0, "failed to retain a texture transition resource");
        const nvrhi::TextureSubresourceSet subresources(range.firstMip, range.mipCount, range.firstSlice, range.sliceCount);
        command->native->setTextureState(resource->native, subresources, ToNativeState(after));
        return BackendStatus::Success();
    }
    BackendStatus CommonBackend::TransitionBuffer(const CommandListRef commandList, const BufferRef buffer, const ResourceState,
                                                  const ResourceState after) noexcept
    {
        auto* const command = static_cast<CommandListPayload*>(m_lifetime.GetPayload(ResourceRef(commandList)));
        auto* const resource = static_cast<BufferPayload*>(m_lifetime.GetPayload(ResourceRef(buffer)));
        if (command == nullptr || !command->open)
            return BackendStatus::Failure(FailureCode::InvalidCommandList, 0, "buffer transition requires an open command list");
        if (resource == nullptr)
            return BackendStatus::Failure(FailureCode::InvalidReference, 0, "buffer transition references a stale buffer");
        if (!TrackCommandResource(*this, *command, ResourceRef(buffer)))
            return BackendStatus::Failure(FailureCode::OutOfMemory, 0, "failed to retain a buffer transition resource");
        command->native->setBufferState(resource->native, ToNativeState(after));
        return BackendStatus::Success();
    }
    BackendStatus CommonBackend::BarrierTextureUav(const CommandListRef commandList, const TextureRef texture) noexcept
    {
        auto* const command = static_cast<CommandListPayload*>(m_lifetime.GetPayload(ResourceRef(commandList)));
        const auto* const resource = static_cast<const TexturePayload*>(m_lifetime.GetPayload(ResourceRef(texture)));
        if (command == nullptr || !command->open)
            return BackendStatus::Failure(FailureCode::InvalidCommandList, 0, "texture UAV barrier requires an open command list");
        if (resource == nullptr || !HasFlag(resource->desc.usage, TextureUsage::UnorderedAccess))
            return BackendStatus::Failure(FailureCode::IncompatibleBinding, 0, "texture does not support unordered access");
        if (!TrackCommandResource(*this, *command, ResourceRef(texture)))
            return BackendStatus::Failure(FailureCode::OutOfMemory, 0, "failed to retain a texture UAV barrier resource");
        command->native->setTextureState(resource->native, nvrhi::AllSubresources, nvrhi::ResourceStates::UnorderedAccess);
        return BackendStatus::Success();
    }
    BackendStatus CommonBackend::BarrierBufferUav(const CommandListRef commandList, const BufferRef buffer) noexcept
    {
        auto* const command = static_cast<CommandListPayload*>(m_lifetime.GetPayload(ResourceRef(commandList)));
        const auto* const resource = static_cast<const BufferPayload*>(m_lifetime.GetPayload(ResourceRef(buffer)));
        if (command == nullptr || !command->open)
            return BackendStatus::Failure(FailureCode::InvalidCommandList, 0, "buffer UAV barrier requires an open command list");
        if (resource == nullptr || !HasFlag(resource->desc.usage, BufferUsage::UnorderedAccess))
            return BackendStatus::Failure(FailureCode::IncompatibleBinding, 0, "buffer does not support unordered access");
        if (!TrackCommandResource(*this, *command, ResourceRef(buffer)))
            return BackendStatus::Failure(FailureCode::OutOfMemory, 0, "failed to retain a buffer UAV barrier resource");
        command->native->setBufferState(resource->native, nvrhi::ResourceStates::UnorderedAccess);
        return BackendStatus::Success();
    }
    BackendStatus CommonBackend::BarrierTextureAliasing(const CommandListRef commandList, const bool discardAfter,
                                                        const TextureRef textureAfter, const TextureRef textureBefore) noexcept
    {
        auto* const command = static_cast<CommandListPayload*>(m_lifetime.GetPayload(ResourceRef(commandList)));
        const auto* const after = static_cast<const TexturePayload*>(m_lifetime.GetPayload(ResourceRef(textureAfter)));
        const auto* const before =
            textureBefore ? static_cast<const TexturePayload*>(m_lifetime.GetPayload(ResourceRef(textureBefore))) : nullptr;
        if (command == nullptr || !command->open)
            return BackendStatus::Failure(FailureCode::InvalidCommandList, 0, "texture aliasing barrier requires an open command list");
        if (after == nullptr || !after->desc.virtualResource || (textureBefore && (before == nullptr || !before->desc.virtualResource)))
            return BackendStatus::Failure(FailureCode::IncompatibleBinding, 0, "texture aliasing barriers require placed resources");
        if (!TrackCommandResource(*this, *command, ResourceRef(textureAfter)) ||
            (textureBefore && !TrackCommandResource(*this, *command, ResourceRef(textureBefore))))
            return BackendStatus::Failure(FailureCode::OutOfMemory, 0, "failed to retain texture aliasing resources");
        command->native->commitBarriers();
        return m_aliasingBarrier(m_fenceContext, command->native, after->native.Get(), before != nullptr ? before->native.Get() : nullptr,
                                 discardAfter)
                   ? BackendStatus::Success()
                   : BackendStatus::Failure(FailureCode::BackendFailure, 0, "native texture aliasing barrier failed");
    }
    BackendStatus CommonBackend::BarrierBufferAliasing(const CommandListRef commandList, const bool discardAfter,
                                                       const BufferRef bufferAfter, const BufferRef bufferBefore) noexcept
    {
        auto* const command = static_cast<CommandListPayload*>(m_lifetime.GetPayload(ResourceRef(commandList)));
        const auto* const after = static_cast<const BufferPayload*>(m_lifetime.GetPayload(ResourceRef(bufferAfter)));
        const auto* const before =
            bufferBefore ? static_cast<const BufferPayload*>(m_lifetime.GetPayload(ResourceRef(bufferBefore))) : nullptr;
        if (command == nullptr || !command->open)
            return BackendStatus::Failure(FailureCode::InvalidCommandList, 0, "buffer aliasing barrier requires an open command list");
        if (after == nullptr || !after->desc.virtualResource || (bufferBefore && (before == nullptr || !before->desc.virtualResource)))
            return BackendStatus::Failure(FailureCode::IncompatibleBinding, 0, "buffer aliasing barriers require placed resources");
        if (!TrackCommandResource(*this, *command, ResourceRef(bufferAfter)) ||
            (bufferBefore && !TrackCommandResource(*this, *command, ResourceRef(bufferBefore))))
            return BackendStatus::Failure(FailureCode::OutOfMemory, 0, "failed to retain buffer aliasing resources");
        command->native->commitBarriers();
        return m_aliasingBarrier(m_fenceContext, command->native, after->native.Get(), before != nullptr ? before->native.Get() : nullptr,
                                 discardAfter)
                   ? BackendStatus::Success()
                   : BackendStatus::Failure(FailureCode::BackendFailure, 0, "native buffer aliasing barrier failed");
    }
    BackendStatus CommonBackend::FlushPendingBarriers(const CommandListRef commandList) noexcept
    {
        auto* const command = static_cast<CommandListPayload*>(m_lifetime.GetPayload(ResourceRef(commandList)));
        if (command == nullptr || !command->open)
            return BackendStatus::Failure(FailureCode::InvalidCommandList, 0, "barrier flush requires an open command list");
        command->native->commitBarriers();
        return BackendStatus::Success();
    }
    BackendStatus CommonBackend::MakeStateSafeToRetire(const CommandListRef commandList, const TextureRef texture) noexcept
    {
        return TransitionTexture(commandList, texture, ResourceState::Unknown, ResourceState::Common, {});
    }
    BackendStatus CommonBackend::MakeStateSafeToRetire(const CommandListRef commandList, const BufferRef buffer) noexcept
    {
        return TransitionBuffer(commandList, buffer, ResourceState::Unknown, ResourceState::Common);
    }
    void CommonBackend::SetResourceDebugName(TextureRef, const char*) noexcept {}
    void CommonBackend::SetResourceDebugName(BufferRef, const char*) noexcept {}
    void CommonBackend::SetResourceDebugName(HeapRef, const char*) noexcept {}
    void CommonBackend::SetResourceDebugName(SamplerStateRef, const char*) noexcept {}
    void CommonBackend::SetResourceDebugName(ShaderRef, const char*) noexcept {}
    void CommonBackend::SetResourceDebugName(VertexLayoutRef, const char*) noexcept {}
    void CommonBackend::SetResourceDebugName(PipelineRef, const char*) noexcept {}
    void CommonBackend::SetResourceDebugName(QueryPoolRef, const char*) noexcept {}
    void CommonBackend::SetResourceDebugName(CommandListRef, const char*) noexcept {}

    void CommonBackend::message(const nvrhi::MessageSeverity severity, const char* const text)
    {
        diagnostics::Level level = diagnostics::Level::Info;
        if (severity == nvrhi::MessageSeverity::Warning)
            level = diagnostics::Level::Warning;
        else if (severity == nvrhi::MessageSeverity::Error || severity == nvrhi::MessageSeverity::Fatal)
            level = diagnostics::Level::Error;
        diagnostics::Log(level, diagnostics::Category::Rendering, text != nullptr ? text : "NVRHI emitted an empty diagnostic");
    }

} // namespace vanguard::rhi::backend
