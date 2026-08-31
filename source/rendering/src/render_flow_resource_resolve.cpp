#include <vanguard/rendering/render_flow_resource_internal.hpp>

#include <algorithm>
#include <new>

namespace vanguard::rendering::detail
{
    namespace
    {
        [[nodiscard]] bool ExtentEqual(const rhi::Extent3D& left, const rhi::Extent3D& right) noexcept
        {
            return left.width == right.width && left.height == right.height && left.depth == right.depth;
        }

        [[nodiscard]] bool SubresourcesEqual(const rhi::SubresourceRange& left, const rhi::SubresourceRange& right) noexcept
        {
            return left.firstMip == right.firstMip && left.mipCount == right.mipCount && left.firstSlice == right.firstSlice && left.sliceCount == right.sliceCount;
        }

        [[nodiscard]] bool ResolveSubresources(const rhi::TextureDesc& texture, const rhi::SubresourceRange& requested, rhi::SubresourceRange& resolved) noexcept
        {
            if (requested.firstMip >= texture.mipCount || requested.firstSlice >= texture.arraySize)
                return false;
            const u32 mipCount = requested.mipCount == 0xffffu ? static_cast<u32>(texture.mipCount) - requested.firstMip : requested.mipCount;
            const u32 sliceCount = requested.sliceCount == 0xffffu ? static_cast<u32>(texture.arraySize) - requested.firstSlice : requested.sliceCount;
            if (mipCount == 0 || sliceCount == 0 || mipCount > static_cast<u32>(texture.mipCount) - requested.firstMip ||
                sliceCount > static_cast<u32>(texture.arraySize) - requested.firstSlice)
                return false;
            resolved = {requested.firstMip, static_cast<u16>(mipCount), requested.firstSlice, static_cast<u16>(sliceCount)};
            return true;
        }

        [[nodiscard]] bool ContainsSubresources(const rhi::SubresourceRange& outer, const rhi::SubresourceRange& inner) noexcept
        {
            const u32 outerMipEnd = static_cast<u32>(outer.firstMip) + outer.mipCount;
            const u32 innerMipEnd = static_cast<u32>(inner.firstMip) + inner.mipCount;
            const u32 outerSliceEnd = static_cast<u32>(outer.firstSlice) + outer.sliceCount;
            const u32 innerSliceEnd = static_cast<u32>(inner.firstSlice) + inner.sliceCount;
            return outer.firstMip <= inner.firstMip && innerMipEnd <= outerMipEnd && outer.firstSlice <= inner.firstSlice && innerSliceEnd <= outerSliceEnd;
        }

        [[nodiscard]] bool SubresourcesOverlap(const rhi::SubresourceRange& left, const rhi::SubresourceRange& right) noexcept
        {
            const u32 leftMipEnd = static_cast<u32>(left.firstMip) + left.mipCount;
            const u32 rightMipEnd = static_cast<u32>(right.firstMip) + right.mipCount;
            const u32 leftSliceEnd = static_cast<u32>(left.firstSlice) + left.sliceCount;
            const u32 rightSliceEnd = static_cast<u32>(right.firstSlice) + right.sliceCount;
            return left.firstMip < rightMipEnd && right.firstMip < leftMipEnd && left.firstSlice < rightSliceEnd && right.firstSlice < leftSliceEnd;
        }

        [[nodiscard]] bool NormalizeTextureView(const FrameTextureDesc& texture, const rhi::TextureViewDesc& requested, rhi::TextureViewDesc& normalized) noexcept
        {
            normalized.format = requested.format == rhi::Format::Unknown ? texture.active.format : requested.format;
            return normalized.format == texture.active.format && ResolveSubresources(texture.active, requested.subresources, normalized.subresources);
        }

        [[nodiscard]] bool NormalizeBufferView(const FrameBufferDesc& buffer, const rhi::BufferViewDesc& requested, rhi::BufferViewDesc& normalized) noexcept
        {
            if (requested.offset >= buffer.active.size)
                return false;
            const u64 size = requested.size == 0 ? buffer.active.size - requested.offset : requested.size;
            if (size == 0 || size > buffer.active.size - requested.offset)
                return false;
            normalized = requested;
            normalized.size = size;
            if (buffer.active.structureStride != 0)
            {
                if ((requested.structureStride != 0 && requested.structureStride != buffer.active.structureStride) || requested.offset % buffer.active.structureStride != 0 ||
                    size % buffer.active.structureStride != 0)
                    return false;
                normalized.structureStride = buffer.active.structureStride;
            }
            else if (requested.structureStride != 0)
            {
                return false;
            }
            if (requested.format != rhi::Format::Unknown && requested.format != buffer.active.format)
                return false;
            normalized.format = requested.format == rhi::Format::Unknown ? buffer.active.format : requested.format;
            return true;
        }

        [[nodiscard]] bool TextureCreationEqual(const rhi::TextureDesc& left, const rhi::TextureDesc& right) noexcept
        {
            return ExtentEqual(left.extent, right.extent) && left.dimension == right.dimension && left.format == right.format && left.mipCount == right.mipCount &&
                   left.arraySize == right.arraySize && left.sampleCount == right.sampleCount && left.usage == right.usage && left.initialState == right.initialState &&
                   left.virtualResource == right.virtualResource;
        }

        [[nodiscard]] bool BufferCreationEqual(const rhi::BufferDesc& left, const rhi::BufferDesc& right) noexcept
        {
            return left.size == right.size && left.structureStride == right.structureStride && left.format == right.format && left.usage == right.usage &&
                   left.initialState == right.initialState && left.memoryType == right.memoryType && left.virtualResource == right.virtualResource;
        }

        template <typename Enum> [[nodiscard]] constexpr u32 Bits(const Enum value) noexcept
        {
            return static_cast<u32>(value);
        }

        template <typename Enum> [[nodiscard]] constexpr bool HasAny(const Enum value, const Enum flags) noexcept
        {
            return (Bits(value) & Bits(flags)) != 0;
        }

        [[nodiscard]] constexpr bool ValidResourceStateBits(const rhi::ResourceState state) noexcept
        {
            constexpr u32 known = (1u << 19u) - 1u;
            return Bits(state) != 0 && (Bits(state) & ~known) == 0;
        }

        [[nodiscard]] constexpr bool HasSingleBit(const u32 value) noexcept
        {
            return value != 0 && (value & (value - 1u)) == 0;
        }

        [[nodiscard]] constexpr bool CanonicalTextureState(const rhi::ResourceState state) noexcept
        {
            if (!ValidResourceStateBits(state))
                return false;
            const u32 bits = Bits(state);
            constexpr u32 common = Bits(rhi::ResourceState::Common);
            if ((bits & common) != 0)
                return bits == common;
            constexpr u32 standalone = Bits(rhi::ResourceState::CopySource) | Bits(rhi::ResourceState::CopyDestination) | Bits(rhi::ResourceState::UnorderedAccess) |
                                       Bits(rhi::ResourceState::RenderTarget) | Bits(rhi::ResourceState::DepthWrite) | Bits(rhi::ResourceState::Present) |
                                       Bits(rhi::ResourceState::ResolveSource) | Bits(rhi::ResourceState::ResolveDestination) | Bits(rhi::ResourceState::ShadingRate);
            if ((bits & standalone) != 0)
                return HasSingleBit(bits);
            constexpr u32 compatibleReads = Bits(rhi::ResourceState::ShaderResourceGraphics) | Bits(rhi::ResourceState::ShaderResourceCompute) | Bits(rhi::ResourceState::DepthRead);
            return (bits & compatibleReads) != 0 && (bits & ~compatibleReads) == 0;
        }

        [[nodiscard]] constexpr bool CanonicalBufferState(const rhi::ResourceState state) noexcept
        {
            if (!ValidResourceStateBits(state))
                return false;
            const u32 bits = Bits(state);
            constexpr u32 common = Bits(rhi::ResourceState::Common);
            if ((bits & common) != 0)
                return bits == common;
            constexpr u32 standalone = Bits(rhi::ResourceState::CopyDestination) | Bits(rhi::ResourceState::UnorderedAccess) | Bits(rhi::ResourceState::AccelerationStructureRead) |
                                       Bits(rhi::ResourceState::AccelerationStructureWrite);
            if ((bits & standalone) != 0)
                return HasSingleBit(bits);
            constexpr u32 compatibleReads = Bits(rhi::ResourceState::CopySource) | Bits(rhi::ResourceState::ShaderResourceGraphics) | Bits(rhi::ResourceState::ShaderResourceCompute) |
                                            Bits(rhi::ResourceState::VertexBuffer) | Bits(rhi::ResourceState::IndexBuffer) | Bits(rhi::ResourceState::ConstantBuffer) |
                                            Bits(rhi::ResourceState::IndirectArgument);
            return (bits & compatibleReads) != 0 && (bits & ~compatibleReads) == 0;
        }

        [[nodiscard]] constexpr bool ValidTextureUsageBits(const rhi::TextureUsage usage) noexcept
        {
            constexpr u32 known = (1u << 11u) - 1u;
            return (Bits(usage) & ~known) == 0;
        }

        [[nodiscard]] constexpr bool ValidBufferUsageBits(const rhi::BufferUsage usage) noexcept
        {
            constexpr u32 known = (1u << 12u) - 1u;
            return (Bits(usage) & ~known) == 0;
        }

        [[nodiscard]] bool TextureStateCompatible(const rhi::TextureDesc& texture, const rhi::ResourceState state) noexcept
        {
            if (!CanonicalTextureState(state))
                return false;
            const u32 bits = Bits(state);
            constexpr u32 bufferOnly = Bits(rhi::ResourceState::VertexBuffer) | Bits(rhi::ResourceState::IndexBuffer) | Bits(rhi::ResourceState::ConstantBuffer) |
                                       Bits(rhi::ResourceState::IndirectArgument) | Bits(rhi::ResourceState::AccelerationStructureRead) |
                                       Bits(rhi::ResourceState::AccelerationStructureWrite);
            if ((bits & bufferOnly) != 0)
                return false;
            if ((bits & Bits(rhi::ResourceState::CopySource)) != 0 && !HasAny(texture.usage, rhi::TextureUsage::CopySource))
                return false;
            if ((bits & Bits(rhi::ResourceState::CopyDestination)) != 0 && !HasAny(texture.usage, rhi::TextureUsage::CopyDestination))
                return false;
            if ((bits & (Bits(rhi::ResourceState::ShaderResourceGraphics) | Bits(rhi::ResourceState::ShaderResourceCompute))) != 0 &&
                !HasAny(texture.usage, rhi::TextureUsage::ShaderResource))
                return false;
            if ((bits & Bits(rhi::ResourceState::UnorderedAccess)) != 0 && !HasAny(texture.usage, rhi::TextureUsage::UnorderedAccess))
                return false;
            if ((bits & Bits(rhi::ResourceState::RenderTarget)) != 0 && !HasAny(texture.usage, rhi::TextureUsage::RenderTarget))
                return false;
            if ((bits & (Bits(rhi::ResourceState::DepthWrite) | Bits(rhi::ResourceState::DepthRead))) != 0 && !HasAny(texture.usage, rhi::TextureUsage::DepthStencil))
                return false;
            if ((bits & Bits(rhi::ResourceState::Present)) != 0 && !HasAny(texture.usage, rhi::TextureUsage::Present))
                return false;
            if ((bits & Bits(rhi::ResourceState::ResolveSource)) != 0 && !HasAny(texture.usage, rhi::TextureUsage::ResolveSource))
                return false;
            if ((bits & Bits(rhi::ResourceState::ResolveDestination)) != 0 && !HasAny(texture.usage, rhi::TextureUsage::ResolveDestination))
                return false;
            return (bits & Bits(rhi::ResourceState::ShadingRate)) == 0 || HasAny(texture.usage, rhi::TextureUsage::ShadingRate);
        }

        [[nodiscard]] bool BufferStateCompatible(const rhi::BufferDesc& buffer, const rhi::ResourceState state) noexcept
        {
            if (!CanonicalBufferState(state))
                return false;
            const u32 bits = Bits(state);
            constexpr u32 textureOnly = Bits(rhi::ResourceState::RenderTarget) | Bits(rhi::ResourceState::DepthWrite) | Bits(rhi::ResourceState::DepthRead) |
                                        Bits(rhi::ResourceState::Present) | Bits(rhi::ResourceState::ResolveSource) | Bits(rhi::ResourceState::ResolveDestination) |
                                        Bits(rhi::ResourceState::ShadingRate);
            if ((bits & textureOnly) != 0)
                return false;
            if ((bits & Bits(rhi::ResourceState::CopySource)) != 0 && !HasAny(buffer.usage, rhi::BufferUsage::CopySource))
                return false;
            if ((bits & Bits(rhi::ResourceState::CopyDestination)) != 0 && !HasAny(buffer.usage, rhi::BufferUsage::CopyDestination))
                return false;
            if ((bits & (Bits(rhi::ResourceState::ShaderResourceGraphics) | Bits(rhi::ResourceState::ShaderResourceCompute))) != 0 && !HasAny(buffer.usage, rhi::BufferUsage::ShaderResource))
                return false;
            if ((bits & Bits(rhi::ResourceState::UnorderedAccess)) != 0 && !HasAny(buffer.usage, rhi::BufferUsage::UnorderedAccess))
                return false;
            if ((bits & Bits(rhi::ResourceState::VertexBuffer)) != 0 && !HasAny(buffer.usage, rhi::BufferUsage::Vertex))
                return false;
            if ((bits & Bits(rhi::ResourceState::IndexBuffer)) != 0 && !HasAny(buffer.usage, rhi::BufferUsage::Index))
                return false;
            if ((bits & Bits(rhi::ResourceState::ConstantBuffer)) != 0 && !HasAny(buffer.usage, rhi::BufferUsage::Constant))
                return false;
            if ((bits & Bits(rhi::ResourceState::IndirectArgument)) != 0 && !HasAny(buffer.usage, rhi::BufferUsage::IndirectArguments))
                return false;
            if ((bits & (Bits(rhi::ResourceState::AccelerationStructureRead) | Bits(rhi::ResourceState::AccelerationStructureWrite))) != 0 &&
                !HasAny(buffer.usage, rhi::BufferUsage::AccelerationStructure))
                return false;
            return true;
        }

        [[nodiscard]] constexpr bool StateMatchesAccess(const FrameResourceKind kind, const rhi::ResourceState state, const LogicalAccessIntent access) noexcept
        {
            const u32 bits = Bits(state);
            const u32 readBits = kind == FrameResourceKind::Texture
                                     ? Bits(rhi::ResourceState::CopySource) | Bits(rhi::ResourceState::ShaderResourceGraphics) | Bits(rhi::ResourceState::ShaderResourceCompute) |
                                           Bits(rhi::ResourceState::UnorderedAccess) | Bits(rhi::ResourceState::DepthRead) | Bits(rhi::ResourceState::Present) |
                                           Bits(rhi::ResourceState::ResolveSource) | Bits(rhi::ResourceState::ShadingRate)
                                     : Bits(rhi::ResourceState::CopySource) | Bits(rhi::ResourceState::ShaderResourceGraphics) | Bits(rhi::ResourceState::ShaderResourceCompute) |
                                           Bits(rhi::ResourceState::UnorderedAccess) | Bits(rhi::ResourceState::VertexBuffer) | Bits(rhi::ResourceState::IndexBuffer) |
                                           Bits(rhi::ResourceState::ConstantBuffer) | Bits(rhi::ResourceState::IndirectArgument) | Bits(rhi::ResourceState::AccelerationStructureRead);
            const u32 writeBits = kind == FrameResourceKind::Texture
                                      ? Bits(rhi::ResourceState::CopyDestination) | Bits(rhi::ResourceState::UnorderedAccess) | Bits(rhi::ResourceState::RenderTarget) |
                                            Bits(rhi::ResourceState::DepthWrite) | Bits(rhi::ResourceState::ResolveDestination)
                                      : Bits(rhi::ResourceState::CopyDestination) | Bits(rhi::ResourceState::UnorderedAccess) | Bits(rhi::ResourceState::AccelerationStructureWrite);
            const bool reads = (bits & readBits) != 0;
            const bool writes = (bits & writeBits) != 0;
            if (access == LogicalAccessIntent::Read)
                return reads;
            if (access == LogicalAccessIntent::Write)
                return writes;
            return access == LogicalAccessIntent::ReadWrite && reads && writes;
        }

        [[nodiscard]] constexpr bool QueueSupportsState(const rhi::QueueType queue, const FrameResourceKind kind, const rhi::ResourceState state) noexcept
        {
            const u32 bits = Bits(state);
            if (queue == rhi::QueueType::Graphics)
                return true;
            if (queue == rhi::QueueType::Copy)
                return bits == Bits(rhi::ResourceState::Common) || bits == Bits(rhi::ResourceState::CopySource) || bits == Bits(rhi::ResourceState::CopyDestination);
            if (queue != rhi::QueueType::Compute)
                return false;
            if (kind == FrameResourceKind::Texture)
            {
                constexpr u32 unsupported = Bits(rhi::ResourceState::ShaderResourceGraphics) | Bits(rhi::ResourceState::RenderTarget) | Bits(rhi::ResourceState::DepthWrite) |
                                            Bits(rhi::ResourceState::DepthRead) | Bits(rhi::ResourceState::Present) | Bits(rhi::ResourceState::ResolveSource) |
                                            Bits(rhi::ResourceState::ResolveDestination) | Bits(rhi::ResourceState::ShadingRate);
                return (bits & unsupported) == 0;
            }
            constexpr u32 unsupported = Bits(rhi::ResourceState::ShaderResourceGraphics) | Bits(rhi::ResourceState::VertexBuffer) | Bits(rhi::ResourceState::IndexBuffer);
            return (bits & unsupported) == 0;
        }

        [[nodiscard]] constexpr bool ValidInitialization(const FrameResourceInitialization initialization) noexcept
        {
            return initialization == FrameResourceInitialization::Undefined || initialization == FrameResourceInitialization::Clear;
        }

        [[nodiscard]] constexpr bool ValidAccessAndContent(const LogicalAccessIntent access, const ResourceContentIntent content) noexcept
        {
            const bool validAccess = access == LogicalAccessIntent::Read || access == LogicalAccessIntent::Write || access == LogicalAccessIntent::ReadWrite;
            const bool validContent = content == ResourceContentIntent::Discard || content == ResourceContentIntent::Preserve || content == ResourceContentIntent::Clear;
            return validAccess && validContent && (content == ResourceContentIntent::Preserve || access == LogicalAccessIntent::Write || access == LogicalAccessIntent::ReadWrite);
        }

        [[nodiscard]] u16 MaximumMipCount(const rhi::Extent3D extent) noexcept
        {
            u32 largest = std::max(extent.width, std::max(extent.height, extent.depth));
            u16 count = 0;
            do
            {
                ++count;
                largest >>= 1u;
            } while (largest != 0);
            return count;
        }

        struct OperationVisit
        {
            u32 batch = 0;
            u32 operation = 0;
            PlanPosition position;
        };

        struct TextureSubresourceLogicalState
        {
            rhi::ResourceState state = rhi::ResourceState::Common;
            bool contentsDefined = false;
        };

        struct AllocationLogicalState
        {
            u32 textureOffset = InvalidRenderFlowResourceIndex;
            u32 textureCount = 0;
            u32 firstActiveUse = InvalidRenderFlowResourceIndex;
            rhi::ResourceState bufferState = rhi::ResourceState::Common;
            bool bufferContentsDefined = false;
        };

        struct LogicalNameKey
        {
            FlowSpaceId flowSpace;
            containers::StringView name;

            [[nodiscard]] u32 CalcHash() const noexcept
            {
                u32 hash = 2166136261u;
                const auto mix = [&hash](const u8 byte) noexcept
                {
                    hash ^= byte;
                    hash *= 16777619u;
                };
                for (u32 shift = 0; shift < 32; shift += 8)
                    mix(static_cast<u8>(flowSpace.value >> shift));
                for (u32 index = 0; index < name.Length(); ++index)
                    mix(static_cast<u8>(name[index]));
                return hash;
            }

            [[nodiscard]] friend bool operator==(const LogicalNameKey& left, const LogicalNameKey& right) noexcept
            {
                return left.flowSpace == right.flowSpace && left.name == right.name;
            }
        };

        [[nodiscard]] bool AccessWrites(const LogicalAccessIntent access) noexcept
        {
            return access == LogicalAccessIntent::Write || access == LogicalAccessIntent::ReadWrite;
        }

        [[nodiscard]] constexpr u8 QueueMask(const rhi::QueueType queue) noexcept
        {
            return queue == rhi::QueueType::Graphics ? u8{1} : queue == rhi::QueueType::Compute ? u8{2} : queue == rhi::QueueType::Copy ? u8{4} : u8{0};
        }

        void TouchLifetime(LogicalAllocationRecord& allocation, const PlanPosition position, const CompiledPacket& packet) noexcept
        {
            if (!allocation.firstUsePosition.IsValid())
            {
                allocation.firstUsePosition = position;
                allocation.firstUseCommandScope = packet.commandScope;
            }
            allocation.lastUsePosition = position;
            allocation.lastUseCommandScope = packet.commandScope;
            allocation.queueMask |= QueueMask(packet.queue);
        }

        [[nodiscard]] constexpr bool ValidQueue(const rhi::QueueType queue) noexcept
        {
            return queue == rhi::QueueType::Graphics || queue == rhi::QueueType::Compute || queue == rhi::QueueType::Copy;
        }

        [[nodiscard]] bool FailResolve(RenderFlowResourceAllocator::Impl& allocator, ExecutionGenerationRef::Impl* const scratch, RenderFlowResourceFailure* const failure,
                                       const RenderFlowResourceFailureCode code, const char* const message, const RenderFlowNodeId node = {}, const PlanPosition position = {},
                                       const ResourceUseId use = {}) noexcept
        {
            const bool result = Fail(failure, code, allocator.state, message, node, position, use);
            if (scratch != nullptr)
                ReleaseGeneration(scratch);
            CancelSession(allocator, allocator.sessionGeneration);
            return result;
        }

        [[nodiscard]] ExecutionGenerationRef::Impl* AllocateGeneration() noexcept
        {
            memory::MemoryBlock block = memory::Allocate(memory::PoolId::Rendering, sizeof(ExecutionGenerationRef::Impl), alignof(ExecutionGenerationRef::Impl));
            return block ? new (block.address) ExecutionGenerationRef::Impl() : nullptr;
        }
    } // namespace

    bool TextureDescEqual(const FrameTextureDesc& left, const FrameTextureDesc& right) noexcept
    {
        return TextureCreationEqual(left.active, right.active) && ExtentEqual(left.maximumExtent, right.maximumExtent) && left.maximumMipCount == right.maximumMipCount &&
               left.initialization == right.initialization;
    }

    bool BufferDescEqual(const FrameBufferDesc& left, const FrameBufferDesc& right) noexcept
    {
        return BufferCreationEqual(left.active, right.active) && left.maximumSize == right.maximumSize && left.initialization == right.initialization;
    }

    bool ResourceDescEqual(const FrameResourceDesc& left, const FrameResourceDesc& right) noexcept
    {
        if (left.kind != right.kind)
            return false;
        if (left.kind == FrameResourceKind::Texture)
            return TextureDescEqual(left.texture, right.texture);
        if (left.kind == FrameResourceKind::Buffer)
            return BufferDescEqual(left.buffer, right.buffer);
        return false;
    }

    bool TextureViewDescEqual(const rhi::TextureViewDesc& left, const rhi::TextureViewDesc& right) noexcept
    {
        return left.format == right.format && SubresourcesEqual(left.subresources, right.subresources);
    }

    bool BufferViewDescEqual(const rhi::BufferViewDesc& left, const rhi::BufferViewDesc& right) noexcept
    {
        return left.format == right.format && left.offset == right.offset && left.size == right.size && left.structureStride == right.structureStride;
    }

    bool ValidTextureDesc(const FrameTextureDesc& desc) noexcept
    {
        const rhi::Extent3D& active = desc.active.extent;
        const rhi::Extent3D& maximum = desc.maximumExtent;
        const bool validDimension = desc.active.dimension == rhi::TextureDimension::Texture1D || desc.active.dimension == rhi::TextureDimension::Texture2D ||
                                    desc.active.dimension == rhi::TextureDimension::Texture3D || desc.active.dimension == rhi::TextureDimension::TextureCube;
        const bool validShape = (desc.active.dimension == rhi::TextureDimension::Texture1D && active.height == 1 && active.depth == 1) ||
                                (desc.active.dimension == rhi::TextureDimension::Texture2D && active.depth == 1) ||
                                (desc.active.dimension == rhi::TextureDimension::Texture3D && desc.active.arraySize == 1) ||
                                (desc.active.dimension == rhi::TextureDimension::TextureCube && active.width == active.height && active.depth == 1 && desc.active.arraySize >= 6 &&
                                 desc.active.arraySize % 6 == 0);
        const bool validSamples = desc.active.sampleCount == 1 || desc.active.sampleCount == 2 || desc.active.sampleCount == 4 || desc.active.sampleCount == 8;
        const bool validMultisampling = desc.active.sampleCount == 1 || (desc.active.dimension == rhi::TextureDimension::Texture2D && desc.active.mipCount == 1 &&
                                                                         !HasAny(desc.active.usage, rhi::TextureUsage::UnorderedAccess));
        return active.width != 0 && active.height != 0 && active.depth != 0 && maximum.width != 0 && maximum.height != 0 && maximum.depth != 0 && validDimension && validShape &&
               desc.active.format != rhi::Format::Unknown && static_cast<u16>(desc.active.format) <= static_cast<u16>(rhi::Format::BC7UNormSrgb) && desc.active.mipCount != 0 &&
               desc.active.mipCount <= MaximumMipCount(active) && desc.active.arraySize != 0 && validSamples && validMultisampling && ValidTextureUsageBits(desc.active.usage) &&
               TextureStateCompatible(desc.active, desc.active.initialState) && !desc.active.virtualResource && maximum.width >= active.width && maximum.height >= active.height &&
               maximum.depth >= active.depth && desc.maximumMipCount >= desc.active.mipCount && desc.maximumMipCount <= MaximumMipCount(maximum) && ValidInitialization(desc.initialization);
    }

    bool ValidBufferDesc(const FrameBufferDesc& desc) noexcept
    {
        const bool validMemory =
            desc.active.memoryType == rhi::MemoryType::DeviceLocal || desc.active.memoryType == rhi::MemoryType::Upload || desc.active.memoryType == rhi::MemoryType::Readback;
        const bool structured = HasAny(desc.active.usage, rhi::BufferUsage::Structured);
        return desc.active.size != 0 && desc.maximumSize >= desc.active.size && ValidBufferUsageBits(desc.active.usage) && validMemory &&
               BufferStateCompatible(desc.active, desc.active.initialState) && !desc.active.virtualResource &&
               (!structured || (desc.active.structureStride != 0 && desc.active.size % desc.active.structureStride == 0 && desc.maximumSize % desc.active.structureStride == 0)) &&
               (desc.active.format == rhi::Format::Unknown || static_cast<u16>(desc.active.format) <= static_cast<u16>(rhi::Format::BC7UNormSrgb)) &&
               ValidInitialization(desc.initialization);
    }

    bool ResolveFrame(RenderFlowResourceAllocator::Impl& allocator, const SurvivingGraphOverlay& surviving, const CompiledQueueSchedule& schedule, ExecutionGenerationRef::Impl*& output,
                      RenderFlowResourceFailure* const failure) noexcept
    {
        ClearFailure(failure);
        output = nullptr;
        if (allocator.state != RenderFlowResourceSessionState::CandidatesSealed)
            return Fail(failure, RenderFlowResourceFailureCode::InvalidPhase, allocator.state, "candidate plan is not sealed");

        allocator.state = RenderFlowResourceSessionState::Resolving;
        allocator.stats.state = allocator.state;
        ExecutionGenerationRef::Impl* const scratch = AllocateGeneration();
        if (scratch == nullptr)
            return FailResolve(allocator, nullptr, failure, RenderFlowResourceFailureCode::CapacityExceeded, "execution generation metadata allocation failed");
        scratch->id = {0, NextGeneration(allocator.executionGeneration)};

        containers::DynamicArray<u32> survivingBatches{memory::pools::Rendering::GetInstance()};
        containers::DynamicArray<u32> batchPackets{memory::pools::Rendering::GetInstance()};
        containers::DynamicArray<BatchResolveMap> batchMaps{memory::pools::Rendering::GetInstance()};
        containers::DynamicArray<LogicalSlot> slots{memory::pools::Rendering::GetInstance()};
        containers::DynamicArray<ActiveUseRecord> uses{memory::pools::Rendering::GetInstance()};
        containers::DynamicArray<OpenScopeRecord> scopes{memory::pools::Rendering::GetInstance()};
        containers::DynamicArray<AllocationLogicalState> allocationStates{memory::pools::Rendering::GetInstance()};
        containers::DynamicArray<TextureSubresourceLogicalState> textureSubresourceStates{memory::pools::Rendering::GetInstance()};
        containers::HashMap<LogicalNameKey, u32> namedSlots{memory::pools::Rendering::GetInstance()};
        containers::HashMap<u32, u32> scheduleScopeIndices{memory::pools::Rendering::GetInstance()};
        containers::HashSet<u32> scheduleStableOrders{memory::pools::Rendering::GetInstance()};
        containers::HashMap<u64, u32> openScopeIndices{memory::pools::Rendering::GetInstance()};
        containers::DynamicArray<u8> scheduleScopeUsed{memory::pools::Rendering::GetInstance()};

        batchPackets.Resize(allocator.writerBatches.Size(), InvalidRenderFlowResourceIndex);
        batchMaps.Resize(allocator.writerBatches.Size());
        scheduleScopeUsed.Resize(schedule.scopes.Size(), u8{0});
        for (u32 batchIndex = 0; batchIndex < allocator.writerBatches.Size(); ++batchIndex)
        {
            const CandidateWriterBatch& batch = allocator.writerBatches[batchIndex];
            if (!surviving.Contains(batch.node))
                continue;
            survivingBatches.PushBack(batchIndex);
            BatchResolveMap& map = batchMaps[batchIndex];
            map.resourceSlots.Resize(batch.resources.Size(), InvalidRenderFlowResourceIndex);
            map.textureViews.Resize(batch.textureViews.Size());
            map.bufferViews.Resize(batch.bufferViews.Size());
            map.useRecords.Resize(batch.operations.Size(), InvalidRenderFlowResourceIndex);
        }
        std::sort(survivingBatches.Begin(), survivingBatches.End(),
                  [&allocator](const u32 left, const u32 right) noexcept { return allocator.writerBatches[left].flowGroup.value < allocator.writerBatches[right].flowGroup.value; });

        if (survivingBatches.Size() > allocator.config.maximumExecutionPackets)
            return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::CapacityExceeded, "surviving packet count exceeds allocator capacity");

        for (u32 index = 0; index < schedule.scopes.Size(); ++index)
        {
            const CompiledCommandScope& scope = schedule.scopes[index];
            if (!scope.scope.IsValid() || !ValidQueue(scope.queue))
                return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::QueueOrCommandScopeMismatch, "queue schedule contains an invalid command scope or queue");
            u32 existing = 0;
            if (scheduleScopeIndices.Find(scope.scope.value, existing))
                return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::QueueOrCommandScopeMismatch, "queue schedule contains duplicate command scope identities");
            if (scheduleStableOrders.Exist(scope.stableOrder))
                return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::QueueOrCommandScopeMismatch, "queue schedule contains duplicate stable-order positions");
            scheduleScopeIndices.Insert(scope.scope.value, index);
            scheduleStableOrders.Insert(scope.stableOrder);
        }

        for (const u32 batchIndex : survivingBatches)
        {
            const CandidateWriterBatch& batch = allocator.writerBatches[batchIndex];
            u32 scopeIndex = InvalidRenderFlowResourceIndex;
            if (!scheduleScopeIndices.Find(batch.commandScope.value, scopeIndex))
                return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::QueueOrCommandScopeMismatch, "surviving node command scope is missing from the queue schedule",
                                   batch.node);

            const u32 packetIndex = scratch->packets.Size();
            if (!scratch->packetByNode.Insert(batch.node.value, packetIndex).IsSuccessful())
                return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "execution generation contains duplicate node packets", batch.node);
            CompiledPacket& packet = scratch->packets.EmplaceBack();
            packet.node = batch.node;
            packet.flowGroup = batch.flowGroup;
            packet.commandScope = batch.commandScope;
            packet.queue = schedule.scopes[scopeIndex].queue;
            batchPackets[batchIndex] = packetIndex;
            if (scheduleScopeUsed[scopeIndex] == 0)
            {
                scratch->commandScopes.PushBack(schedule.scopes[scopeIndex]);
                scheduleScopeUsed[scopeIndex] = 1;
            }
        }
        std::sort(scratch->commandScopes.Begin(), scratch->commandScopes.End(),
                  [](const CompiledCommandScope& left, const CompiledCommandScope& right) noexcept { return left.stableOrder < right.stableOrder; });

        const auto getSlot = [&](const u32 batchIndex, const LogicalResourceId resource, u32& result) noexcept -> bool
        {
            result = InvalidRenderFlowResourceIndex;
            const CandidateWriterBatch& batch = allocator.writerBatches[batchIndex];
            if (!resource.IsValid() || resource.flowGroup != batch.flowGroup || resource.generation != batch.sessionGeneration || resource.index >= batch.resources.Size())
                return false;
            BatchResolveMap& map = batchMaps[batchIndex];
            if (map.resourceSlots[resource.index] != InvalidRenderFlowResourceIndex)
            {
                result = map.resourceSlots[resource.index];
                return true;
            }
            const CandidateResource& candidate = batch.resources[resource.index];
            if (candidate.identity == CandidateIdentityKind::Named)
            {
                // The key views candidate-owned name bytes. Sealed candidate
                // batches are immutable until this resolve-local map is gone.
                const LogicalNameKey key{candidate.flowSpace, containers::StringView(candidate.name)};
                u32 index = InvalidRenderFlowResourceIndex;
                if (namedSlots.Find(key, index))
                {
                    map.resourceSlots[resource.index] = index;
                    result = index;
                    return true;
                }
            }
            LogicalSlot& slot = slots.EmplaceBack();
            slot.identity = candidate.identity;
            slot.flowSpace = candidate.flowSpace;
            slot.name = candidate.name;
            slot.temporaryPosition = candidate.declarationPosition;
            map.resourceSlots[resource.index] = slots.Size() - 1u;
            result = slots.Size() - 1u;
            if (candidate.identity == CandidateIdentityKind::Named && !namedSlots.Insert({candidate.flowSpace, containers::StringView(candidate.name)}, result).IsSuccessful())
                return false;
            return true;
        };

        const auto failOperation = [&](const OperationVisit& mergedOperation, const RenderFlowResourceFailureCode code, const char* const message,
                                       const ResourceUseId use = {}) noexcept -> bool
        {
            const CandidateWriterBatch& batch = allocator.writerBatches[mergedOperation.batch];
            return FailResolve(allocator, scratch, failure, code, message, batch.node, mergedOperation.position, use);
        };

        u64 compiledOperationCount = 0;
        for (const u32 batchIndex : survivingBatches)
        {
            const CandidateWriterBatch& batch = allocator.writerBatches[batchIndex];
            for (u32 operationIndex = 0; operationIndex < batch.operations.Size(); ++operationIndex)
            {
                const CandidateOperation& operation = batch.operations[operationIndex];
                const OperationVisit mergedOperation{batchIndex, operationIndex, {batch.flowGroup, operationIndex}};
                if (operation.ordinal != operationIndex)
                    return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "candidate operation ordinal does not match its sealed tape position");
                const u32 packetIndex = batchPackets[batchIndex];
                CompiledPacket& packet = scratch->packets[packetIndex];
                ++compiledOperationCount;

                u32 slotIndex = InvalidRenderFlowResourceIndex;
                switch (operation.kind)
                {
                case CandidateOperationKind::DeclareTexture:
                case CandidateOperationKind::DeclareBuffer:
                case CandidateOperationKind::DeclareLike:
                {
                    if (!getSlot(mergedOperation.batch, operation.resource, slotIndex))
                        return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "declaration references an invalid candidate resource");
                    LogicalSlot& slot = slots[slotIndex];
                    if (slot.currentAllocation != InvalidRenderFlowResourceIndex)
                        return failOperation(mergedOperation, RenderFlowResourceFailureCode::DescriptorConflict, "declaration cannot replace an already-mapped logical resource");
                    FrameResourceDesc resolvedDesc = operation.resourceDesc;
                    if (operation.kind == CandidateOperationKind::DeclareLike)
                    {
                        u32 sourceSlot = InvalidRenderFlowResourceIndex;
                        if (!getSlot(mergedOperation.batch, operation.otherResource, sourceSlot) || slots[sourceSlot].currentAllocation == InvalidRenderFlowResourceIndex)
                            return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "declare-like source is not mapped at this plan position");
                        resolvedDesc = scratch->allocations[slots[sourceSlot].currentAllocation].desc;
                    }
                    if ((operation.kind == CandidateOperationKind::DeclareTexture && resolvedDesc.kind != FrameResourceKind::Texture) ||
                        (operation.kind == CandidateOperationKind::DeclareBuffer && resolvedDesc.kind != FrameResourceKind::Buffer))
                        return failOperation(mergedOperation, RenderFlowResourceFailureCode::DescriptorConflict, "declaration kind and descriptor kind disagree");
                    LogicalAllocationRecord allocation;
                    allocation.desc = resolvedDesc;
                    allocation.declarationPosition = mergedOperation.position;
                    scratch->allocations.PushBack(allocation);
                    AllocationLogicalState logicalState;
                    if (resolvedDesc.kind == FrameResourceKind::Texture)
                    {
                        const u64 count = static_cast<u64>(resolvedDesc.texture.active.mipCount) * resolvedDesc.texture.active.arraySize;
                        if (count > allocator.config.maximumTrackedTextureSubresources ||
                            static_cast<u64>(textureSubresourceStates.Size()) + count > allocator.config.maximumTrackedTextureSubresources)
                            return failOperation(mergedOperation, RenderFlowResourceFailureCode::CapacityExceeded,
                                                 "texture subresource state metadata exceeds the allocator's aggregate capacity");
                        logicalState.textureOffset = textureSubresourceStates.Size();
                        logicalState.textureCount = static_cast<u32>(count);
                        const TextureSubresourceLogicalState initial{resolvedDesc.texture.active.initialState, resolvedDesc.texture.initialization != FrameResourceInitialization::Undefined};
                        for (u32 index = 0; index < logicalState.textureCount; ++index)
                            textureSubresourceStates.PushBack(initial);
                    }
                    else
                    {
                        logicalState.bufferState = resolvedDesc.buffer.active.initialState;
                        logicalState.bufferContentsDefined = resolvedDesc.buffer.initialization != FrameResourceInitialization::Undefined;
                    }
                    allocationStates.PushBack(logicalState);
                    slot.currentAllocation = scratch->allocations.Size() - 1u;
                    break;
                }
                case CandidateOperationKind::CreateTextureView:
                {
                    if (!getSlot(mergedOperation.batch, operation.resource, slotIndex) || slotIndex >= slots.Size() || slots[slotIndex].currentAllocation == InvalidRenderFlowResourceIndex)
                        return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "texture view was created before its resource was mapped");
                    const u32 allocation = slots[slotIndex].currentAllocation;
                    if (scratch->allocations[allocation].desc.kind != FrameResourceKind::Texture || operation.textureView.index >= batchMaps[mergedOperation.batch].textureViews.Size())
                        return failOperation(mergedOperation, RenderFlowResourceFailureCode::DescriptorConflict, "texture view is incompatible with its logical resource");
                    ResolvedTextureViewRecord& resolvedView = batchMaps[mergedOperation.batch].textureViews[operation.textureView.index];
                    if (!NormalizeTextureView(scratch->allocations[allocation].desc.texture, batch.textureViews[operation.textureView.index].desc, resolvedView.desc))
                        return failOperation(mergedOperation, RenderFlowResourceFailureCode::DescriptorConflict, "texture view range or format is incompatible with its logical resource");
                    resolvedView.slot = slotIndex;
                    break;
                }
                case CandidateOperationKind::CreateBufferView:
                {
                    if (!getSlot(mergedOperation.batch, operation.resource, slotIndex) || slotIndex >= slots.Size() || slots[slotIndex].currentAllocation == InvalidRenderFlowResourceIndex)
                        return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "buffer view was created before its resource was mapped");
                    const u32 allocation = slots[slotIndex].currentAllocation;
                    if (scratch->allocations[allocation].desc.kind != FrameResourceKind::Buffer || operation.bufferView.index >= batchMaps[mergedOperation.batch].bufferViews.Size())
                        return failOperation(mergedOperation, RenderFlowResourceFailureCode::DescriptorConflict, "buffer view is incompatible with its logical resource");
                    ResolvedBufferViewRecord& resolvedView = batchMaps[mergedOperation.batch].bufferViews[operation.bufferView.index];
                    if (!NormalizeBufferView(scratch->allocations[allocation].desc.buffer, batch.bufferViews[operation.bufferView.index].desc, resolvedView.desc))
                        return failOperation(mergedOperation, RenderFlowResourceFailureCode::DescriptorConflict,
                                             "buffer view range, format, or stride is incompatible with its logical resource");
                    resolvedView.slot = slotIndex;
                    break;
                }
                case CandidateOperationKind::TextureUseBegin:
                case CandidateOperationKind::BufferUseBegin:
                {
                    const bool texture = operation.kind == CandidateOperationKind::TextureUseBegin;
                    if (texture && operation.textureView.IsValid())
                    {
                        if (operation.textureView.index >= batchMaps[mergedOperation.batch].textureViews.Size())
                            return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "texture use references an invalid view", operation.use);
                        slotIndex = batchMaps[mergedOperation.batch].textureViews[operation.textureView.index].slot;
                    }
                    else if (!texture && operation.bufferView.IsValid())
                    {
                        if (operation.bufferView.index >= batchMaps[mergedOperation.batch].bufferViews.Size())
                            return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "buffer use references an invalid view", operation.use);
                        slotIndex = batchMaps[mergedOperation.batch].bufferViews[operation.bufferView.index].slot;
                    }
                    else if (!getSlot(mergedOperation.batch, operation.resource, slotIndex))
                    {
                        return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "resource use references an invalid logical resource", operation.use);
                    }
                    if (slotIndex == InvalidRenderFlowResourceIndex || slots[slotIndex].currentAllocation == InvalidRenderFlowResourceIndex)
                        return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidUseOrScope, "resource use begins before the logical resource is mapped", operation.use);
                    const u32 allocation = slots[slotIndex].currentAllocation;
                    const FrameResourceKind expected = texture ? FrameResourceKind::Texture : FrameResourceKind::Buffer;
                    if (scratch->allocations[allocation].desc.kind != expected)
                        return failOperation(mergedOperation, RenderFlowResourceFailureCode::DescriptorConflict, "resource use kind does not match the active allocation", operation.use);
                    const LogicalAccessIntent access = texture ? operation.textureUse.access : operation.bufferUse.access;
                    const ResourceContentIntent content = texture ? operation.textureUse.content : operation.bufferUse.content;
                    const rhi::ResourceState requiredState = texture ? operation.textureUse.requiredState : operation.bufferUse.requiredState;
                    if (!ValidAccessAndContent(access, content))
                        return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidUseOrScope, "resource use contains an invalid access/content combination", operation.use);
                    if ((texture && !TextureStateCompatible(scratch->allocations[allocation].desc.texture.active, requiredState)) ||
                        (!texture && !BufferStateCompatible(scratch->allocations[allocation].desc.buffer.active, requiredState)) || !StateMatchesAccess(expected, requiredState, access) ||
                        !QueueSupportsState(packet.queue, expected, requiredState))
                        return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidUseOrScope,
                                             "resource use state is incompatible with its kind, creation usage, access intent, or command queue", operation.use);
                    BatchResolveMap& map = batchMaps[mergedOperation.batch];
                    if (!operation.use.IsValid() || operation.use.flowGroup != batch.flowGroup || operation.use.generation != batch.sessionGeneration ||
                        operation.use.ordinal != operationIndex || operation.use.ordinal >= map.useRecords.Size() || map.useRecords[operation.use.ordinal] != InvalidRenderFlowResourceIndex)
                        return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidUseOrScope, "resource use identity is invalid or duplicated", operation.use);
                    CompiledStep step;
                    step.kind = texture ? CompiledResourceStepKind::TextureUseBegin : CompiledResourceStepKind::BufferUseBegin;
                    step.use = operation.use;
                    step.logicalAllocation = allocation;
                    if (packet.runtime.liveness == nullptr)
                    {
                        packet.runtime.liveness = AllocatePacketUseLiveness();
                        if (packet.runtime.liveness == nullptr)
                            return failOperation(mergedOperation, RenderFlowResourceFailureCode::CapacityExceeded, "packet liveness metadata allocation failed", operation.use);
                    }
                    step.runtimeUseSlot = packet.runtime.liveness->useActive.Size();
                    step.texture = operation.textureUse;
                    step.buffer = operation.bufferUse;
                    step.hasExplicitView = texture ? operation.textureView.IsValid() : operation.bufferView.IsValid();
                    if (texture)
                    {
                        if (step.texture.requiredState == rhi::ResourceState::Unknown ||
                            !ResolveSubresources(scratch->allocations[allocation].desc.texture.active, operation.textureUse.subresources, step.texture.subresources))
                            return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidUseOrScope, "texture use contains an invalid required state or subresource range",
                                                 operation.use);
                        if (operation.textureView.IsValid())
                        {
                            step.textureView = batchMaps[mergedOperation.batch].textureViews[operation.textureView.index].desc;
                            if (!ContainsSubresources(step.texture.subresources, step.textureView.subresources))
                                return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidUseOrScope,
                                                     "texture use range does not cover every subresource exposed by its view", operation.use);
                        }
                        AllocationLogicalState& logicalState = allocationStates[allocation];
                        for (u32 activeIndex = logicalState.firstActiveUse; activeIndex != InvalidRenderFlowResourceIndex; activeIndex = uses[activeIndex].nextActive)
                        {
                            if (activeIndex >= uses.Size())
                                return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "active resource-use chain contains an invalid index",
                                                     operation.use);
                            const ActiveUseRecord& active = uses[activeIndex];
                            if (!active.texture || active.ended)
                                return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "active texture-use chain contains an invalid record",
                                                     operation.use);
                            if (SubresourcesOverlap(active.textureSubresources, step.texture.subresources) &&
                                (active.access != LogicalAccessIntent::Read || access != LogicalAccessIntent::Read || active.requiredState != requiredState))
                                return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidUseOrScope, "overlapping active texture uses require the same read-only state",
                                                     operation.use);
                        }
                        const u32 resourceMipCount = scratch->allocations[allocation].desc.texture.active.mipCount;
                        for (u32 slice = step.texture.subresources.firstSlice; slice < static_cast<u32>(step.texture.subresources.firstSlice) + step.texture.subresources.sliceCount; ++slice)
                        {
                            for (u32 mip = step.texture.subresources.firstMip; mip < static_cast<u32>(step.texture.subresources.firstMip) + step.texture.subresources.mipCount; ++mip)
                            {
                                TextureSubresourceLogicalState& subresource = textureSubresourceStates[logicalState.textureOffset + slice * resourceMipCount + mip];
                                if (content == ResourceContentIntent::Preserve && !subresource.contentsDefined)
                                    return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidUseOrScope, "texture use attempts to preserve undefined subresource contents",
                                                         operation.use);
                                subresource.state = step.texture.requiredState;
                            }
                        }
                    }
                    else
                    {
                        if (step.buffer.requiredState == rhi::ResourceState::Unknown)
                            return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidUseOrScope, "buffer use contains an invalid required state", operation.use);
                        if (operation.bufferView.IsValid())
                            step.bufferView = batchMaps[mergedOperation.batch].bufferViews[operation.bufferView.index].desc;
                        AllocationLogicalState& logicalState = allocationStates[allocation];
                        for (u32 activeIndex = logicalState.firstActiveUse; activeIndex != InvalidRenderFlowResourceIndex; activeIndex = uses[activeIndex].nextActive)
                        {
                            if (activeIndex >= uses.Size())
                                return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "active resource-use chain contains an invalid index",
                                                     operation.use);
                            const ActiveUseRecord& active = uses[activeIndex];
                            if (active.texture || active.ended)
                                return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "active buffer-use chain contains an invalid record",
                                                     operation.use);
                            if (active.access != LogicalAccessIntent::Read || access != LogicalAccessIntent::Read || active.requiredState != requiredState)
                                return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidUseOrScope, "overlapping active buffer uses require the same read-only state",
                                                     operation.use);
                        }
                        if (content == ResourceContentIntent::Preserve && !logicalState.bufferContentsDefined)
                            return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidUseOrScope, "buffer use attempts to preserve undefined contents", operation.use);
                        logicalState.bufferState = step.buffer.requiredState;
                    }
                    packet.runtime.liveness->useActive.PushBack(0);
                    const u32 useIndex = uses.Size();
                    ActiveUseRecord activeUse;
                    activeUse.id = operation.use;
                    activeUse.allocation = allocation;
                    activeUse.packet = packetIndex;
                    activeUse.access = access;
                    activeUse.content = content;
                    activeUse.requiredState = requiredState;
                    activeUse.textureSubresources = step.texture.subresources;
                    activeUse.runtimeUseSlot = step.runtimeUseSlot;
                    activeUse.nextActive = allocationStates[allocation].firstActiveUse;
                    activeUse.texture = texture;
                    uses.PushBack(activeUse);
                    if (activeUse.nextActive != InvalidRenderFlowResourceIndex)
                        uses[activeUse.nextActive].previousActive = useIndex;
                    allocationStates[allocation].firstActiveUse = useIndex;
                    map.useRecords[operation.use.ordinal] = useIndex;
                    packet.steps.PushBack(step);
                    scratch->allocations[allocation].used = true;
                    TouchLifetime(scratch->allocations[allocation], mergedOperation.position, packet);
                    break;
                }
                case CandidateOperationKind::UseEnd:
                {
                    BatchResolveMap& map = batchMaps[mergedOperation.batch];
                    if (!operation.use.IsValid() || operation.use.flowGroup != batch.flowGroup || operation.use.generation != batch.sessionGeneration ||
                        operation.use.ordinal >= map.useRecords.Size())
                        return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidUseOrScope, "resource use end has an invalid identity", operation.use);
                    const u32 useIndex = map.useRecords[operation.use.ordinal];
                    ActiveUseRecord* const active = useIndex < uses.Size() ? &uses[useIndex] : nullptr;
                    if (active == nullptr || active->id != operation.use || active->ended || active->packet != packetIndex)
                        return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidUseOrScope, "resource use end is missing, duplicated, or owned by another packet",
                                             operation.use);
                    AllocationLogicalState& logicalState = allocationStates[active->allocation];
                    if ((active->previousActive == InvalidRenderFlowResourceIndex && logicalState.firstActiveUse != useIndex) ||
                        (active->previousActive != InvalidRenderFlowResourceIndex && (active->previousActive >= uses.Size() || uses[active->previousActive].nextActive != useIndex)) ||
                        (active->nextActive != InvalidRenderFlowResourceIndex && (active->nextActive >= uses.Size() || uses[active->nextActive].previousActive != useIndex)))
                        return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "active resource-use chain is internally inconsistent", operation.use);
                    if (active->previousActive == InvalidRenderFlowResourceIndex)
                        logicalState.firstActiveUse = active->nextActive;
                    else
                        uses[active->previousActive].nextActive = active->nextActive;
                    if (active->nextActive != InvalidRenderFlowResourceIndex)
                        uses[active->nextActive].previousActive = active->previousActive;
                    active->previousActive = InvalidRenderFlowResourceIndex;
                    active->nextActive = InvalidRenderFlowResourceIndex;
                    active->ended = true;
                    if (AccessWrites(active->access))
                    {
                        if (active->texture)
                        {
                            const u32 resourceMipCount = scratch->allocations[active->allocation].desc.texture.active.mipCount;
                            for (u32 slice = active->textureSubresources.firstSlice;
                                 slice < static_cast<u32>(active->textureSubresources.firstSlice) + active->textureSubresources.sliceCount; ++slice)
                                for (u32 mip = active->textureSubresources.firstMip; mip < static_cast<u32>(active->textureSubresources.firstMip) + active->textureSubresources.mipCount;
                                     ++mip)
                                    textureSubresourceStates[logicalState.textureOffset + slice * resourceMipCount + mip].contentsDefined = true;
                        }
                        else
                        {
                            logicalState.bufferContentsDefined = true;
                        }
                    }
                    CompiledStep step;
                    step.kind = CompiledResourceStepKind::UseEnd;
                    step.use = operation.use;
                    step.logicalAllocation = active->allocation;
                    step.runtimeUseSlot = active->runtimeUseSlot;
                    packet.steps.PushBack(step);
                    TouchLifetime(scratch->allocations[active->allocation], mergedOperation.position, packet);
                    break;
                }
                case CandidateOperationKind::ScopeOpen:
                {
                    if (!getSlot(mergedOperation.batch, operation.resource, slotIndex) || slots[slotIndex].currentAllocation == InvalidRenderFlowResourceIndex)
                        return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidUseOrScope, "resource scope begins before the logical resource is mapped");
                    u32 existingScope = InvalidRenderFlowResourceIndex;
                    if (!operation.scope.IsValid() || openScopeIndices.Find(operation.scope.value, existingScope))
                        return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidUseOrScope, "resource scope identity is invalid or duplicated");
                    const u32 allocation = slots[slotIndex].currentAllocation;
                    if (!openScopeIndices.Insert(operation.scope.value, scopes.Size()).IsSuccessful())
                        return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidUseOrScope, "resource scope identity became duplicated during Resolve");
                    scopes.PushBack({operation.scope, allocation, mergedOperation.position, false});
                    scratch->allocations[allocation].used = true;
                    TouchLifetime(scratch->allocations[allocation], mergedOperation.position, packet);
                    break;
                }
                case CandidateOperationKind::ScopeClose:
                {
                    u32 scopeIndex = InvalidRenderFlowResourceIndex;
                    OpenScopeRecord* const found =
                        operation.scope.IsValid() && openScopeIndices.Find(operation.scope.value, scopeIndex) && scopeIndex < scopes.Size() ? &scopes[scopeIndex] : nullptr;
                    if (found == nullptr || found->closed)
                        return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidUseOrScope, "resource scope end has no surviving open endpoint");
                    found->closed = true;
                    TouchLifetime(scratch->allocations[found->allocation], mergedOperation.position, packet);
                    break;
                }
                case CandidateOperationKind::SwapMappings:
                {
                    u32 leftSlot = InvalidRenderFlowResourceIndex;
                    u32 rightSlot = InvalidRenderFlowResourceIndex;
                    if (!getSlot(mergedOperation.batch, operation.resource, leftSlot) || !getSlot(mergedOperation.batch, operation.otherResource, rightSlot) || leftSlot == rightSlot ||
                        slots[leftSlot].currentAllocation == InvalidRenderFlowResourceIndex || slots[rightSlot].currentAllocation == InvalidRenderFlowResourceIndex)
                        return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "logical swap requires two distinct mapped resources");
                    const u32 leftAllocation = slots[leftSlot].currentAllocation;
                    const u32 rightAllocation = slots[rightSlot].currentAllocation;
                    if (!ResourceDescEqual(scratch->allocations[leftAllocation].desc, scratch->allocations[rightAllocation].desc))
                        return failOperation(mergedOperation, RenderFlowResourceFailureCode::DescriptorConflict, "logical swap descriptors are incompatible");
                    slots[leftSlot].currentAllocation = rightAllocation;
                    slots[rightSlot].currentAllocation = leftAllocation;
                    break;
                }
                case CandidateOperationKind::Decision:
                    if (!operation.decision.IsValid() || operation.decision.flowGroup != batch.flowGroup || operation.decision.generation != batch.sessionGeneration ||
                        operation.decision.ordinal != operationIndex)
                        return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "decision identity is invalid or duplicated");
                    packet.decisions.PushBack({operation.decision, operation.decisionValue});
                    break;
                case CandidateOperationKind::Export:
                {
                    if (!getSlot(mergedOperation.batch, operation.resource, slotIndex))
                        return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "export references an invalid logical resource");
                    for (const PendingExportRecord& exportRecord : scratch->pendingExports)
                        if (exportRecord.slot == operation.exportSlot)
                            return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "export slot is requested more than once");
                    scratch->pendingExports.PushBack({operation.exportSlot, slotIndex, InvalidRenderFlowResourceIndex});
                    break;
                }
                }
            }
        }

        for (const ActiveUseRecord& use : uses)
            if (!use.ended)
                return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::InvalidUseOrScope, "a surviving resource use has no end endpoint",
                                   scratch->packets[use.packet].node, {}, use.id);
        for (const OpenScopeRecord& scope : scopes)
            if (!scope.closed)
                return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::InvalidUseOrScope, "a surviving cross-node resource scope has no close endpoint", {},
                                   scope.begin);

        for (PendingExportRecord& exportRecord : scratch->pendingExports)
        {
            if (exportRecord.logicalSlot >= slots.Size() || slots[exportRecord.logicalSlot].currentAllocation == InvalidRenderFlowResourceIndex)
                return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "terminal export has no mapped logical allocation");
            exportRecord.allocation = slots[exportRecord.logicalSlot].currentAllocation;
            scratch->allocations[exportRecord.allocation].exported = true;
            scratch->allocations[exportRecord.allocation].used = true;
        }

        const u32 physicalGeneration = scratch->id.generation;
        for (u32 allocation = 0; allocation < scratch->allocations.Size(); ++allocation)
            if (scratch->allocations[allocation].used)
                scratch->allocations[allocation].physical = {allocation, physicalGeneration};
        for (CompiledPacket& packet : scratch->packets)
        {
            for (CompiledStep& step : packet.steps)
            {
                if (step.kind == CompiledResourceStepKind::UseEnd)
                    continue;
                if (step.logicalAllocation >= scratch->allocations.Size())
                    return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "compiled resource use has an invalid logical allocation",
                                       packet.node, {}, step.use);
                step.physical = scratch->allocations[step.logicalAllocation].physical;
                if (!step.physical.IsValid())
                    return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "compiled resource use has no physical assignment", packet.node,
                                       {}, step.use);
            }
        }

        allocator.executionGeneration = scratch->id.generation;
        allocator.publishedGeneration = scratch;
        allocator.state = RenderFlowResourceSessionState::Ready;
        allocator.stats.state = allocator.state;
        ++allocator.stats.resolvedFrames;
        allocator.stats.compiledOperations += compiledOperationCount;
        allocator.stats.logicalAllocations = scratch->allocations.Size();
        allocator.stats.compiledPackets = scratch->packets.Size();
        // The immutable execution generation now owns every datum required by
        // Consume. Candidate tapes are no longer replayed and must not overlap
        // the execution generation's lifetime.
        allocator.writerBatches.Clear();
        allocator.writerReservations.Clear();
        allocator.operationCount = 0;
        output = scratch;
        return true;
    }
} // namespace vanguard::rendering::detail
