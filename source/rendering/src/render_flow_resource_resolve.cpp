#include <vanguard/rendering/render_flow_resource_internal.hpp>

#include <algorithm>
#include <cmath>
#include <new>

namespace vanguard::rendering::detail
{
    namespace
    {
        template <typename Enum> [[nodiscard]] constexpr u32 Bits(const Enum value) noexcept
        {
            return static_cast<u32>(value);
        }

        template <typename Enum> [[nodiscard]] constexpr bool HasAny(const Enum value, const Enum flags) noexcept
        {
            return (Bits(value) & Bits(flags)) != 0;
        }

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
            if (mipCount == 0 || sliceCount == 0 || mipCount > static_cast<u32>(texture.mipCount) - requested.firstMip || sliceCount > static_cast<u32>(texture.arraySize) - requested.firstSlice)
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

        [[nodiscard]] constexpr bool IsDepthFormat(const rhi::Format format) noexcept
        {
            return format == rhi::Format::D16UNorm || format == rhi::Format::D24UNormS8UInt || format == rhi::Format::D32Float || format == rhi::Format::D32FloatS8UInt;
        }

        [[nodiscard]] constexpr bool HasStencilPlane(const rhi::Format format) noexcept
        {
            return format == rhi::Format::D24UNormS8UInt || format == rhi::Format::D32FloatS8UInt;
        }

        [[nodiscard]] constexpr bool IsUnsignedIntegerFormat(const rhi::Format format) noexcept
        {
            return format == rhi::Format::R8UInt || format == rhi::Format::R8G8UInt || format == rhi::Format::R8G8B8A8UInt || format == rhi::Format::R16UInt || format == rhi::Format::R16G16UInt ||
                   format == rhi::Format::R16G16B16A16UInt || format == rhi::Format::R32UInt || format == rhi::Format::R32G32UInt;
        }

        [[nodiscard]] bool TextureClearValueEqual(const TextureClearValue& left, const TextureClearValue& right) noexcept
        {
            if (left.kind != right.kind)
                return false;
            if (left.kind == TextureClearValueKind::ColorFloat)
                return left.color.red == right.color.red && left.color.green == right.color.green && left.color.blue == right.color.blue && left.color.alpha == right.color.alpha;
            if (left.kind == TextureClearValueKind::ColorUint)
                return left.uintValue == right.uintValue;
            if (left.kind == TextureClearValueKind::Depth)
                return left.depth == right.depth;
            if (left.kind == TextureClearValueKind::Stencil)
                return left.stencil == right.stencil;
            if (left.kind == TextureClearValueKind::DepthStencil)
                return left.depth == right.depth && left.stencil == right.stencil;
            return left.kind == TextureClearValueKind::None;
        }

        [[nodiscard]] constexpr bool BufferClearValueEqual(const BufferClearValue& left, const BufferClearValue& right) noexcept
        {
            return left.kind == right.kind && (left.kind == BufferClearValueKind::None || left.value == right.value);
        }

        [[nodiscard]] bool ValidTextureClearScalars(const TextureClearValue& value) noexcept
        {
            if (value.kind == TextureClearValueKind::ColorFloat)
                return std::isfinite(value.color.red) && std::isfinite(value.color.green) && std::isfinite(value.color.blue) && std::isfinite(value.color.alpha);
            if (value.kind == TextureClearValueKind::Depth || value.kind == TextureClearValueKind::DepthStencil)
                return std::isfinite(value.depth) && value.depth >= 0.0f && value.depth <= 1.0f;
            return value.kind == TextureClearValueKind::ColorUint || value.kind == TextureClearValueKind::Stencil;
        }

        [[nodiscard]] bool TextureDescriptorSupportsClear(const rhi::TextureDesc& desc, const TextureClearValue& value) noexcept
        {
            if (!ValidTextureClearScalars(value))
                return false;
            const bool depth = IsDepthFormat(desc.format);
            if (value.kind == TextureClearValueKind::ColorFloat)
                return (!depth && HasAny(desc.usage, rhi::TextureUsage::RenderTarget)) ||
                       (!depth && !IsUnsignedIntegerFormat(desc.format) && HasAny(desc.usage, rhi::TextureUsage::UnorderedAccess) && !HasAny(desc.usage, rhi::TextureUsage::RenderTarget));
            if (value.kind == TextureClearValueKind::ColorUint)
                return IsUnsignedIntegerFormat(desc.format) && HasAny(desc.usage, rhi::TextureUsage::UnorderedAccess);
            if (value.kind == TextureClearValueKind::Depth)
                return depth && HasAny(desc.usage, rhi::TextureUsage::DepthStencil);
            if (value.kind == TextureClearValueKind::Stencil || value.kind == TextureClearValueKind::DepthStencil)
                return depth && HasStencilPlane(desc.format) && HasAny(desc.usage, rhi::TextureUsage::DepthStencil);
            return false;
        }

        [[nodiscard]] bool ClassifyTextureClear(const rhi::TextureDesc& desc, const rhi::ResourceState state, const rhi::QueueType queue, const TextureClearValue& value,
                                                CompiledResourceActionKind& kind) noexcept
        {
            if (!ValidTextureClearScalars(value))
                return false;
            const bool depth = IsDepthFormat(desc.format);
            if (value.kind == TextureClearValueKind::ColorFloat && state == rhi::ResourceState::RenderTarget && queue == rhi::QueueType::Graphics && !depth &&
                HasAny(desc.usage, rhi::TextureUsage::RenderTarget))
                kind = CompiledResourceActionKind::TextureColorTargetClear;
            else if (value.kind == TextureClearValueKind::ColorFloat && state == rhi::ResourceState::UnorderedAccess && queue != rhi::QueueType::Copy && !depth && !IsUnsignedIntegerFormat(desc.format) &&
                     HasAny(desc.usage, rhi::TextureUsage::UnorderedAccess) && !HasAny(desc.usage, rhi::TextureUsage::RenderTarget))
                kind = CompiledResourceActionKind::TextureUavFloatClear;
            else if (value.kind == TextureClearValueKind::ColorUint && state == rhi::ResourceState::UnorderedAccess && queue != rhi::QueueType::Copy && IsUnsignedIntegerFormat(desc.format) &&
                     HasAny(desc.usage, rhi::TextureUsage::UnorderedAccess))
                kind = CompiledResourceActionKind::TextureUavUintClear;
            else if (value.kind == TextureClearValueKind::Depth && state == rhi::ResourceState::DepthWrite && queue == rhi::QueueType::Graphics && depth && HasAny(desc.usage, rhi::TextureUsage::DepthStencil))
                kind = CompiledResourceActionKind::TextureDepthClear;
            else if (value.kind == TextureClearValueKind::Stencil && state == rhi::ResourceState::DepthWrite && queue == rhi::QueueType::Graphics && depth && HasStencilPlane(desc.format) &&
                     HasAny(desc.usage, rhi::TextureUsage::DepthStencil))
                kind = CompiledResourceActionKind::TextureStencilClear;
            else if (value.kind == TextureClearValueKind::DepthStencil && state == rhi::ResourceState::DepthWrite && queue == rhi::QueueType::Graphics && depth && HasStencilPlane(desc.format) &&
                     HasAny(desc.usage, rhi::TextureUsage::DepthStencil))
                kind = CompiledResourceActionKind::TextureDepthStencilClear;
            else
                return false;
            return true;
        }

        [[nodiscard]] bool ClassifyBufferClear(const rhi::BufferDesc& desc, const rhi::ResourceState state, const rhi::QueueType queue, const BufferClearValue& value, CompiledResourceActionKind& kind) noexcept
        {
            if (value.kind != BufferClearValueKind::Uint || state != rhi::ResourceState::UnorderedAccess || queue == rhi::QueueType::Copy || !HasAny(desc.usage, rhi::BufferUsage::UnorderedAccess))
                return false;
            kind = CompiledResourceActionKind::BufferUavUintClear;
            return true;
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
            return ExtentEqual(left.extent, right.extent) && left.dimension == right.dimension && left.format == right.format && left.mipCount == right.mipCount && left.arraySize == right.arraySize &&
                   left.sampleCount == right.sampleCount && left.usage == right.usage && left.initialState == right.initialState && left.virtualResource == right.virtualResource && left.keepInitialState == right.keepInitialState;
        }

        [[nodiscard]] bool BufferCreationEqual(const rhi::BufferDesc& left, const rhi::BufferDesc& right) noexcept
        {
            return left.size == right.size && left.structureStride == right.structureStride && left.format == right.format && left.usage == right.usage && left.initialState == right.initialState &&
                   left.memoryType == right.memoryType && left.virtualResource == right.virtualResource && left.keepInitialState == right.keepInitialState;
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
            constexpr u32 standalone = Bits(rhi::ResourceState::CopySource) | Bits(rhi::ResourceState::CopyDestination) | Bits(rhi::ResourceState::UnorderedAccess) | Bits(rhi::ResourceState::RenderTarget) |
                                       Bits(rhi::ResourceState::DepthWrite) | Bits(rhi::ResourceState::Present) | Bits(rhi::ResourceState::ResolveSource) | Bits(rhi::ResourceState::ResolveDestination) |
                                       Bits(rhi::ResourceState::ShadingRate);
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
            constexpr u32 bufferOnly = Bits(rhi::ResourceState::VertexBuffer) | Bits(rhi::ResourceState::IndexBuffer) | Bits(rhi::ResourceState::ConstantBuffer) | Bits(rhi::ResourceState::IndirectArgument) |
                                       Bits(rhi::ResourceState::AccelerationStructureRead) | Bits(rhi::ResourceState::AccelerationStructureWrite);
            if ((bits & bufferOnly) != 0)
                return false;
            if ((bits & Bits(rhi::ResourceState::CopySource)) != 0 && !HasAny(texture.usage, rhi::TextureUsage::CopySource))
                return false;
            if ((bits & Bits(rhi::ResourceState::CopyDestination)) != 0 && !HasAny(texture.usage, rhi::TextureUsage::CopyDestination))
                return false;
            if ((bits & (Bits(rhi::ResourceState::ShaderResourceGraphics) | Bits(rhi::ResourceState::ShaderResourceCompute))) != 0 && !HasAny(texture.usage, rhi::TextureUsage::ShaderResource))
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
            constexpr u32 textureOnly = Bits(rhi::ResourceState::RenderTarget) | Bits(rhi::ResourceState::DepthWrite) | Bits(rhi::ResourceState::DepthRead) | Bits(rhi::ResourceState::Present) |
                                        Bits(rhi::ResourceState::ResolveSource) | Bits(rhi::ResourceState::ResolveDestination) | Bits(rhi::ResourceState::ShadingRate);
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
            if ((bits & (Bits(rhi::ResourceState::AccelerationStructureRead) | Bits(rhi::ResourceState::AccelerationStructureWrite))) != 0 && !HasAny(buffer.usage, rhi::BufferUsage::AccelerationStructure))
                return false;
            return true;
        }

        [[nodiscard]] constexpr bool StateMatchesAccess(const FrameResourceKind kind, const rhi::ResourceState state, const LogicalAccessIntent access) noexcept
        {
            const u32 bits = Bits(state);
            const u32 readBits = kind == FrameResourceKind::Texture
                                     ? Bits(rhi::ResourceState::CopySource) | Bits(rhi::ResourceState::ShaderResourceGraphics) | Bits(rhi::ResourceState::ShaderResourceCompute) |
                                           Bits(rhi::ResourceState::UnorderedAccess) | Bits(rhi::ResourceState::DepthRead) | Bits(rhi::ResourceState::Present) | Bits(rhi::ResourceState::ResolveSource) |
                                           Bits(rhi::ResourceState::ShadingRate)
                                     : Bits(rhi::ResourceState::CopySource) | Bits(rhi::ResourceState::ShaderResourceGraphics) | Bits(rhi::ResourceState::ShaderResourceCompute) |
                                           Bits(rhi::ResourceState::UnorderedAccess) | Bits(rhi::ResourceState::VertexBuffer) | Bits(rhi::ResourceState::IndexBuffer) | Bits(rhi::ResourceState::ConstantBuffer) |
                                           Bits(rhi::ResourceState::IndirectArgument) | Bits(rhi::ResourceState::AccelerationStructureRead);
            const u32 writeBits = kind == FrameResourceKind::Texture
                                      ? Bits(rhi::ResourceState::CopyDestination) | Bits(rhi::ResourceState::UnorderedAccess) | Bits(rhi::ResourceState::RenderTarget) | Bits(rhi::ResourceState::DepthWrite) |
                                            Bits(rhi::ResourceState::ResolveDestination)
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
                                            Bits(rhi::ResourceState::DepthRead) | Bits(rhi::ResourceState::Present) | Bits(rhi::ResourceState::ResolveSource) | Bits(rhi::ResourceState::ResolveDestination) |
                                            Bits(rhi::ResourceState::ShadingRate);
                return (bits & unsupported) == 0;
            }
            constexpr u32 unsupported = Bits(rhi::ResourceState::ShaderResourceGraphics) | Bits(rhi::ResourceState::VertexBuffer) | Bits(rhi::ResourceState::IndexBuffer);
            return (bits & unsupported) == 0;
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
            CommandScopeId lastCommandScope;
            LogicalAccessIntent lastAccess = LogicalAccessIntent::Read;
            bool hasPriorUse = false;
            bool authoritativeBefore = false;
        };

        struct AllocationLogicalState
        {
            u32 textureOffset = InvalidRenderFlowResourceIndex;
            u32 textureCount = 0;
            u32 firstActiveUse = InvalidRenderFlowResourceIndex;
            rhi::ResourceState bufferState = rhi::ResourceState::Common;
            bool bufferContentsDefined = false;
            CommandScopeId bufferLastCommandScope;
            LogicalAccessIntent bufferLastAccess = LogicalAccessIntent::Read;
            bool bufferHasPriorUse = false;
            bool bufferAuthoritativeBefore = false;
            bool initializationPending = false;
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

        [[nodiscard]] constexpr bool PositionBefore(const PlanPosition left, const PlanPosition right) noexcept
        {
            return left.flowGroup.value < right.flowGroup.value || (left.flowGroup == right.flowGroup && left.ordinal < right.ordinal);
        }

        [[nodiscard]] bool QueueOrderProved(const CommandScopeId producerScope, const rhi::QueueType producerQueue, const CommandScopeId consumerScope, const rhi::QueueType consumerQueue,
                                            const containers::DynamicArray<CompiledCommandScope>& scopes,
                                            const containers::DynamicArray<CompiledQueueDependency>& dependencies) noexcept
        {
            if (producerQueue == consumerQueue)
                return true;
            const rhi::CommandListSyncType required = RequiredQueueSync(producerQueue, consumerQueue);
            if (required == rhi::CommandListSyncType::None)
                return false;
            u32 producerOrder = InvalidRenderFlowResourceIndex;
            u32 consumerOrder = InvalidRenderFlowResourceIndex;
            for (const CompiledCommandScope& scope : scopes)
            {
                if (scope.scope == producerScope)
                    producerOrder = scope.stableOrder;
                if (scope.scope == consumerScope)
                    consumerOrder = scope.stableOrder;
            }
            if (producerOrder == InvalidRenderFlowResourceIndex || consumerOrder == InvalidRenderFlowResourceIndex)
                return false;
            for (const CompiledQueueDependency& dependency : dependencies)
            {
                if (dependency.sync != required)
                    continue;
                u32 boundaryProducerOrder = InvalidRenderFlowResourceIndex;
                u32 boundaryConsumerOrder = InvalidRenderFlowResourceIndex;
                for (const CompiledCommandScope& scope : scopes)
                {
                    if (scope.scope == dependency.producerScope)
                        boundaryProducerOrder = scope.stableOrder;
                    if (scope.scope == dependency.consumerScope)
                        boundaryConsumerOrder = scope.stableOrder;
                }
                if (boundaryProducerOrder != InvalidRenderFlowResourceIndex && boundaryConsumerOrder != InvalidRenderFlowResourceIndex && producerOrder <= boundaryProducerOrder &&
                    boundaryConsumerOrder <= consumerOrder)
                    return true;
            }
            return false;
        }

        struct WholeFrameLane
        {
            FrameResourceDesc physicalDesc;
            PlanPosition lastUsePosition;
            CommandScopeId lastUseCommandScope;
            u32 physicalIndex = InvalidRenderFlowResourceIndex;
        };

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

        [[nodiscard]] bool FailResolve(RenderFlowResourceAllocator::Impl& allocator, ExecutionGenerationRef::Impl* const scratch, RenderFlowResourceFailure* const failure,
                                       const RenderFlowResourceFailureCode code, const char* const message, const RenderFlowNodeId node = {}, const PlanPosition position = {},
                                       const ResourceUseId use = {}) noexcept
        {
            const bool result = Fail(failure, code, allocator.state, message, node, position, use);
            if (scratch != nullptr)
            {
                if (scratch->hasNativeResourceBindings)
                {
                    allocator.placedPool.Rollback(scratch->placedBatch);
                    for (const PhysicalBindingRecord& binding : scratch->physicalBindings)
                        if (binding.kind == PhysicalBindingKind::DedicatedPool)
                            allocator.dedicatedPool.Rollback(binding.poolEntry);
                }
                ReleaseGeneration(scratch);
            }
            CancelSession(allocator, allocator.sessionGeneration);
            return result;
        }

        struct PhysicalScopeState
        {
            PhysicalScopeState() noexcept : cells(memory::pools::Rendering::GetInstance()) {}
            containers::DynamicArray<rhi::ResourceState> cells;
            rhi::ResourceState initial = rhi::ResourceState::Unknown;
            rhi::ResourceState terminal = rhi::ResourceState::Unknown;
            u32 lastPacket = InvalidRenderFlowResourceIndex;
        };

        // Replay native object state in deterministic flow order. Automatic-reset objects start independently; explicit objects carry the exact exit of their ordered predecessor.
        [[nodiscard]] bool CompileScopeEntryStates(RenderFlowResourceAllocator::Impl& allocator, ExecutionGenerationRef::Impl& generation, CompiledPacket& packet, const u32 packetIndex,
                                                   containers::HashMap<u64, u32>& nativeIndices, containers::DynamicArray<PhysicalScopeState>& history, u32& totalEntries, u64& totalActions, RenderFlowResourceFailure* const failure) noexcept
        {
            struct TrackedResource
            {
                rhi::ResourceRef resource;
                rhi::TextureDesc texture;
                u32 first = 0;
                u32 count = 1;
                u32 physicalIndex = InvalidRenderFlowResourceIndex;
            };
            containers::DynamicArray<rhi::ResourceRef> references{memory::pools::Rendering::GetInstance()};
            containers::DynamicArray<TrackedResource> resources{memory::pools::Rendering::GetInstance()};
            containers::DynamicArray<rhi::ResourceState> states{memory::pools::Rendering::GetInstance()};
            const auto reject = [&](const RenderFlowResourceFailureCode code, const char* message) noexcept { return Fail(failure, code, allocator.state, message, packet.node); };
            const auto referenceFor = [&](const PhysicalResourceId physical) noexcept -> rhi::ResourceRef
            {
                if (!physical.IsValid() || physical.generation != generation.id.generation || physical.index >= generation.physicalBindings.Size())
                    return {};
                const PhysicalBindingRecord& binding = generation.physicalBindings[physical.index];
                if (binding.texture.IsValid() == binding.buffer.IsValid())
                    return {};
                return binding.texture.IsValid() ? rhi::ResourceRef(binding.texture) : rhi::ResourceRef(binding.buffer);
            };
            const auto append = [&](const rhi::ResourceRef reference) noexcept
            {
                if (!reference.IsValid())
                    return reject(RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "scope entry lost a physical resource assignment");
                const u32 previous = references.Size();
                references.PushBack(reference);
                return references.Size() == previous + 1u || reject(RenderFlowResourceFailureCode::CapacityExceeded, "scope reference metadata allocation failed");
            };
            for (const CompiledStep& step : packet.steps)
                if ((step.kind != CompiledResourceStepKind::UseEnd || step.finalizeForAlias) && !append(referenceFor(step.physical)))
                    return false;
            for (const CompiledResourceAction& action : packet.actions)
                if (!append(referenceFor(action.physical)))
                    return false;
            for (const rhi::ResourceRef predecessor : packet.aliasPredecessors)
                if (!append(predecessor))
                    return false;
            if (references.Size() > 1)
                std::sort(references.Begin(), references.End(), [](const rhi::ResourceRef left, const rhi::ResourceRef right) noexcept { return left.value < right.value; });
            rhi::ResourceRef previous;
            for (const rhi::ResourceRef reference : references)
            {
                if (reference == previous)
                    continue;
                previous = reference;
                TrackedResource tracked;
                tracked.resource = reference;
                tracked.first = states.Size();
                if (!nativeIndices.Find(reference.value, tracked.physicalIndex) || tracked.physicalIndex >= history.Size())
                    return reject(RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "scope resource has no retained physical-state identity");
                rhi::ResourceState initial;
                rhi::Failure rhiFailure;
                if (reference.GetKind() == rhi::ResourceKind::Texture)
                {
                    if (!rhi::GetTextureDesc(rhi::CastResourceRef<rhi::TextureRef>(reference), tracked.texture, &rhiFailure))
                        return reject(MapRhiFailure(rhiFailure, RhiFailureContext::ImportedIdentity), "scope entry texture descriptor query failed");
                    if (!tracked.texture.keepInitialState && generation.physicalBindings[tracked.physicalIndex].kind == PhysicalBindingKind::PlacedPool && tracked.texture.initialState != rhi::ResourceState::Common)
                        return reject(RenderFlowResourceFailureCode::UnsupportedCapability, "explicit placed textures require Common creation and reuse state");
                    tracked.count = static_cast<u32>(tracked.texture.mipCount) * tracked.texture.arraySize;
                    initial = tracked.texture.keepInitialState ? tracked.texture.initialState : history[tracked.physicalIndex].initial;
                    if (generation.physicalBindings[tracked.physicalIndex].explicitState == tracked.texture.keepInitialState)
                        return reject(RenderFlowResourceFailureCode::BackendContractViolation, "texture binding state policy disagrees with its native descriptor");
                }
                else if (reference.GetKind() == rhi::ResourceKind::Buffer)
                {
                    rhi::BufferDesc buffer;
                    if (!rhi::GetBufferDesc(rhi::CastResourceRef<rhi::BufferRef>(reference), buffer, &rhiFailure))
                        return reject(MapRhiFailure(rhiFailure, RhiFailureContext::ImportedIdentity), "scope entry buffer descriptor query failed");
                    if (buffer.memoryType != rhi::MemoryType::DeviceLocal || buffer.initialState != rhi::ResourceState::Common)
                        return reject(RenderFlowResourceFailureCode::UnsupportedCapability, "scope entry requires device-local Common buffers");
                    if (generation.physicalBindings[tracked.physicalIndex].explicitState == buffer.keepInitialState)
                        return reject(RenderFlowResourceFailureCode::BackendContractViolation, "buffer binding state policy disagrees with its native descriptor");
                    initial = buffer.initialState;
                }
                else
                    return reject(RenderFlowResourceFailureCode::BackendContractViolation, "scope entry requires a texture or buffer");
                if (initial == rhi::ResourceState::Unknown || tracked.count == 0)
                    return reject(RenderFlowResourceFailureCode::BackendContractViolation, "scope entry has no explicit native initial state");
                if (tracked.count > allocator.config.maximumCommandScopeEntryStates - totalEntries)
                    return reject(RenderFlowResourceFailureCode::CapacityExceeded, "command-scope entry states exceed the aggregate metadata capacity");
                totalEntries += tracked.count;
                PhysicalScopeState& prior = history[tracked.physicalIndex];
                const bool explicitState = generation.physicalBindings[tracked.physicalIndex].explicitState;
                if (explicitState && prior.cells.Empty())
                    prior.cells.Resize(tracked.count, initial);
                if (explicitState && prior.cells.Size() != tracked.count)
                    return reject(RenderFlowResourceFailureCode::CapacityExceeded, "physical scope-state storage is incomplete");
                if (explicitState && prior.lastPacket != InvalidRenderFlowResourceIndex)
                {
                    if (prior.lastPacket >= generation.packets.Size())
                        return reject(RenderFlowResourceFailureCode::BackendContractViolation, "explicit state predecessor packet is stale");
                    CompiledPacket& predecessor = generation.packets[prior.lastPacket];
                    if (predecessor.queue != packet.queue)
                    {
                        if (!QueueOrderProved(predecessor.commandScope, predecessor.queue, packet.commandScope, packet.queue, generation.commandScopes, generation.queueDependencies))
                            return reject(RenderFlowResourceFailureCode::QueueOrCommandScopeMismatch, "explicit state crosses queues without the matching fork/join boundary");
                        const FrameResourceKind kind = reference.GetKind() == rhi::ResourceKind::Texture ? FrameResourceKind::Texture : FrameResourceKind::Buffer;
                        for (u32 cell = 0; cell < tracked.count; ++cell)
                        {
                            if (QueueStateAllowed(packet.queue, kind, prior.cells[cell]))
                                continue;
                            if (predecessor.queue != rhi::QueueType::Graphics || packet.queue != rhi::QueueType::Compute || !QueueStateAllowed(predecessor.queue, kind, prior.cells[cell]) ||
                                !QueueStateAllowed(predecessor.queue, kind, rhi::ResourceState::Common) || !QueueStateAllowed(packet.queue, kind, rhi::ResourceState::Common))
                                return reject(RenderFlowResourceFailureCode::QueueOrCommandScopeMismatch, "explicit state cannot be handed to the consumer queue");
                            if (totalActions >= allocator.config.maximumCompiledResourceActions)
                                return reject(RenderFlowResourceFailureCode::CapacityExceeded, "cross-queue state handoff transitions exceed the compiled action capacity");
                            CompiledResourceAction action;
                            action.kind = kind == FrameResourceKind::Texture ? CompiledResourceActionKind::TextureTransition : CompiledResourceActionKind::BufferTransition;
                            action.physical = {tracked.physicalIndex, generation.id.generation};
                            action.before = prior.cells[cell];
                            action.after = rhi::ResourceState::Common;
                            if (kind == FrameResourceKind::Texture)
                                action.textureSubresources = {static_cast<u16>(cell % tracked.texture.mipCount), 1, static_cast<u16>(cell / tracked.texture.mipCount), 1};
                            const u32 exitActionCount = predecessor.exitActions.Size();
                            predecessor.exitActions.PushBack(action);
                            if (predecessor.exitActions.Size() != exitActionCount + 1u)
                                return reject(RenderFlowResourceFailureCode::CapacityExceeded, "cross-queue state handoff storage allocation failed");
                            ++totalActions;
                            prior.cells[cell] = rhi::ResourceState::Common;
                        }
                    }
                    const u32 size = packet.statePredecessorPackets.Size();
                    packet.statePredecessorPackets.PushBack(prior.lastPacket);
                    if (packet.statePredecessorPackets.Size() != size + 1u)
                        return reject(RenderFlowResourceFailureCode::CapacityExceeded, "physical state predecessor storage allocation failed");
                }
                for (u32 cell = 0; cell < tracked.count; ++cell)
                {
                    rhi::CommandListEntryState entry;
                    entry.resource = reference;
                    entry.state = explicitState ? prior.cells[cell] : initial;
                    if (reference.GetKind() == rhi::ResourceKind::Texture)
                        entry.subresource = {static_cast<u16>(cell % tracked.texture.mipCount), static_cast<u16>(cell / tracked.texture.mipCount)};
                    packet.entryStates.PushBack(entry);
                    states.PushBack(entry.state);
                    if (!QueueStateAllowed(packet.queue, reference.GetKind() == rhi::ResourceKind::Texture ? FrameResourceKind::Texture : FrameResourceKind::Buffer, entry.state))
                        return reject(RenderFlowResourceFailureCode::QueueOrCommandScopeMismatch, "native entry state is illegal on the scope queue");
                }
                if (states.Size() != tracked.first + tracked.count || packet.entryStates.Size() != states.Size())
                    return reject(RenderFlowResourceFailureCode::CapacityExceeded, "scope entry metadata allocation failed");
                const u32 size = resources.Size();
                resources.PushBack(tracked);
                if (resources.Size() != size + 1u)
                    return reject(RenderFlowResourceFailureCode::CapacityExceeded, "scope resource metadata allocation failed");
            }
            const auto findResource = [&](const rhi::ResourceRef reference) noexcept -> const TrackedResource*
            {
                if (resources.Size() == 0)
                    return nullptr;
                const auto found = std::lower_bound(resources.Begin(), resources.End(), reference.value, [](const TrackedResource& value, const u64 key) noexcept { return value.resource.value < key; });
                return found != resources.End() && found->resource == reference ? &*found : nullptr;
            };
            const auto changeState = [&](const TrackedResource& tracked, const rhi::SubresourceRange range, const rhi::ResourceState after, rhi::ResourceState* const before) noexcept
            {
                const bool texture = tracked.resource.GetKind() == rhi::ResourceKind::Texture;
                const u32 mips = texture ? tracked.texture.mipCount : 1u;
                const u32 slices = texture ? tracked.texture.arraySize : 1u;
                const u32 firstMip = texture ? range.firstMip : 0u;
                const u32 firstSlice = texture ? range.firstSlice : 0u;
                if (firstMip >= mips || firstSlice >= slices)
                    return false;
                const u32 mipCount = !texture ? 1u : range.mipCount == 0xffffu ? mips - firstMip : range.mipCount;
                const u32 sliceCount = !texture ? 1u : range.sliceCount == 0xffffu ? slices - firstSlice : range.sliceCount;
                if (mipCount == 0 || sliceCount == 0 || mipCount > mips - firstMip || sliceCount > slices - firstSlice)
                    return false;
                const rhi::ResourceState entry = states[tracked.first + firstSlice * mips + firstMip];
                for (u32 slice = firstSlice; slice < firstSlice + sliceCount; ++slice)
                    for (u32 mip = firstMip; mip < firstMip + mipCount; ++mip)
                    {
                        rhi::ResourceState& state = states[tracked.first + slice * mips + mip];
                        if (before != nullptr && state != entry)
                            return false; // A single barrier cannot claim one before-state for a heterogeneous range.
                        state = after;
                    }
                if (before != nullptr)
                    *before = entry;
                return true;
            };
            for (const rhi::ResourceRef predecessor : packet.aliasPredecessors)
            {
                const TrackedResource* const tracked = findResource(predecessor);
                if (tracked == nullptr || (!generation.physicalBindings[tracked->physicalIndex].explicitState && packet.entryStates[tracked->first].state != rhi::ResourceState::Common))
                    return reject(RenderFlowResourceFailureCode::UnsupportedCapability, "aliased predecessor close-time restoration must remain Common after activation");
            }
            const auto replayActions = [&](const u32 offset, const u32 count) noexcept
            {
                if (offset > packet.actions.Size() || count > packet.actions.Size() - offset)
                    return false;
                for (u32 index = offset; index < offset + count; ++index)
                {
                    CompiledResourceAction& action = packet.actions[index];
                    const TrackedResource* const tracked = findResource(referenceFor(action.physical));
                    if (tracked == nullptr)
                        return false;
                    if (action.kind == CompiledResourceActionKind::TextureTransition || action.kind == CompiledResourceActionKind::BufferTransition)
                    {
                        if (action.after == rhi::ResourceState::Unknown || (action.kind == CompiledResourceActionKind::TextureTransition) != (tracked->resource.GetKind() == rhi::ResourceKind::Texture))
                            return false;
                        if (!changeState(*tracked, action.textureSubresources, action.after, &action.before))
                            return false;
                    }
                    else if (action.kind == CompiledResourceActionKind::TextureUavBarrier || action.kind == CompiledResourceActionKind::BufferUavBarrier)
                    {
                        // The current RHI UAV primitive touches the entire resource; do not silently transition undeclared subresources.
                        for (u32 cell = tracked->first; cell < tracked->first + tracked->count; ++cell)
                            if (states[cell] != rhi::ResourceState::UnorderedAccess)
                                return false;
                    }
                    else if (action.kind == CompiledResourceActionKind::SwapChainPresentTransition)
                    {
                        if (tracked->resource.GetKind() != rhi::ResourceKind::Texture || action.after != rhi::ResourceState::Present ||
                            !changeState(*tracked, {}, rhi::ResourceState::Present, &action.before))
                            return false;
                    }
                }
                return true;
            };
            for (const CompiledStep& step : packet.steps)
            {
                const TrackedResource* const tracked = findResource(referenceFor(step.physical));
                if (step.kind != CompiledResourceStepKind::UseEnd)
                {
                    if (tracked == nullptr)
                        return reject(RenderFlowResourceFailureCode::BackendContractViolation, "compiled use is missing its scope entry");
                    if (step.aliasPredecessorCount != 0 && tracked->resource.GetKind() == rhi::ResourceKind::Texture && HasAny(tracked->texture.usage, rhi::TextureUsage::RenderTarget | rhi::TextureUsage::DepthStencil))
                    {
                        const rhi::ResourceState discard = packet.queue == rhi::QueueType::Graphics ? (HasAny(tracked->texture.usage, rhi::TextureUsage::RenderTarget) ? rhi::ResourceState::RenderTarget : rhi::ResourceState::DepthWrite) : rhi::ResourceState::UnorderedAccess;
                        if (!changeState(*tracked, {}, discard, nullptr))
                            return reject(RenderFlowResourceFailureCode::BackendContractViolation, "alias discard state is not representable");
                    }
                    if (!replayActions(step.beforeActionOffset, step.beforeActionCount))
                        return reject(RenderFlowResourceFailureCode::UnsupportedCapability, "scope before-actions require unrepresented physical subresource state");
                }
                else
                {
                    if (!replayActions(step.afterActionOffset, step.afterActionCount))
                        return reject(RenderFlowResourceFailureCode::UnsupportedCapability, "scope after-actions require unrepresented physical subresource state");
                    if (step.finalizeForAlias)
                    {
                        if (tracked == nullptr || (!generation.physicalBindings[tracked->physicalIndex].explicitState && packet.entryStates[tracked->first].state != rhi::ResourceState::Common))
                            return reject(RenderFlowResourceFailureCode::UnsupportedCapability, "alias predecessor close would undo Common finalization");
                        if (!changeState(*tracked, {}, rhi::ResourceState::Common, nullptr))
                            return reject(RenderFlowResourceFailureCode::BackendContractViolation, "alias finalization is missing physical state");
                    }
                }
            }
            for (const TrackedResource& tracked : resources)
                if (generation.physicalBindings[tracked.physicalIndex].explicitState)
                {
                    PhysicalScopeState& result = history[tracked.physicalIndex];
                    // Buffers decay after ExecuteCommandLists, not between lists in one submission. Normalize every scope so both arrangements agree.
                    if (tracked.resource.GetKind() == rhi::ResourceKind::Buffer && states[tracked.first] != rhi::ResourceState::Common)
                    {
                        if (totalActions >= allocator.config.maximumCompiledResourceActions)
                            return reject(RenderFlowResourceFailureCode::CapacityExceeded, "buffer scope exit transitions exceed the compiled action capacity");
                        CompiledResourceAction action;
                        action.kind = CompiledResourceActionKind::BufferTransition;
                        action.physical = {tracked.physicalIndex, generation.id.generation};
                        action.before = states[tracked.first];
                        action.after = rhi::ResourceState::Common;
                        const u32 size = packet.exitActions.Size();
                        packet.exitActions.PushBack(action);
                        if (packet.exitActions.Size() != size + 1u)
                            return reject(RenderFlowResourceFailureCode::CapacityExceeded, "buffer scope exit transition storage allocation failed");
                        ++totalActions;
                        states[tracked.first] = rhi::ResourceState::Common;
                    }
                    for (u32 cell = 0; cell < tracked.count; ++cell)
                        result.cells[cell] = states[tracked.first + cell];
                    result.lastPacket = packetIndex;
                }
            if (packet.statePredecessorPackets.Size() > 1)
            {
                std::sort(packet.statePredecessorPackets.Begin(), packet.statePredecessorPackets.End());
                const auto end = std::unique(packet.statePredecessorPackets.Begin(), packet.statePredecessorPackets.End());
                packet.statePredecessorPackets.Resize(static_cast<u32>(end - packet.statePredecessorPackets.Begin()));
            }
            return true;
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
               left.initialization == right.initialization && TextureClearValueEqual(left.clearValue, right.clearValue);
    }

    bool BufferDescEqual(const FrameBufferDesc& left, const FrameBufferDesc& right) noexcept
    {
        return BufferCreationEqual(left.active, right.active) && left.maximumSize == right.maximumSize && left.initialization == right.initialization && BufferClearValueEqual(left.clearValue, right.clearValue);
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
        const bool validShape =
            (desc.active.dimension == rhi::TextureDimension::Texture1D && active.height == 1 && active.depth == 1) || (desc.active.dimension == rhi::TextureDimension::Texture2D && active.depth == 1) ||
            (desc.active.dimension == rhi::TextureDimension::Texture3D && desc.active.arraySize == 1) ||
            (desc.active.dimension == rhi::TextureDimension::TextureCube && active.width == active.height && active.depth == 1 && desc.active.arraySize >= 6 && desc.active.arraySize % 6 == 0);
        const bool validSamples = desc.active.sampleCount == 1 || desc.active.sampleCount == 2 || desc.active.sampleCount == 4 || desc.active.sampleCount == 8;
        const bool validMultisampling =
            desc.active.sampleCount == 1 || (desc.active.dimension == rhi::TextureDimension::Texture2D && desc.active.mipCount == 1 && !HasAny(desc.active.usage, rhi::TextureUsage::UnorderedAccess));
        const bool validInitialization = (desc.initialization == FrameResourceInitialization::Undefined && desc.clearValue.kind == TextureClearValueKind::None) ||
                                         (desc.initialization == FrameResourceInitialization::Clear && TextureDescriptorSupportsClear(desc.active, desc.clearValue));
        return active.width != 0 && active.height != 0 && active.depth != 0 && maximum.width != 0 && maximum.height != 0 && maximum.depth != 0 && validDimension && validShape &&
               desc.active.format != rhi::Format::Unknown && static_cast<u16>(desc.active.format) <= static_cast<u16>(rhi::Format::BC7UNormSrgb) && desc.active.mipCount != 0 &&
               desc.active.mipCount <= MaximumMipCount(active) && desc.active.arraySize != 0 && validSamples && validMultisampling && ValidTextureUsageBits(desc.active.usage) &&
               TextureStateCompatible(desc.active, desc.active.initialState) && !desc.active.virtualResource && maximum.width >= active.width && maximum.height >= active.height &&
               maximum.depth >= active.depth && desc.maximumMipCount >= desc.active.mipCount && desc.maximumMipCount <= MaximumMipCount(maximum) && validInitialization;
    }

    bool ValidBufferDesc(const FrameBufferDesc& desc) noexcept
    {
        const bool validMemory = desc.active.memoryType == rhi::MemoryType::DeviceLocal || desc.active.memoryType == rhi::MemoryType::Upload || desc.active.memoryType == rhi::MemoryType::Readback;
        const bool structured = HasAny(desc.active.usage, rhi::BufferUsage::Structured);
        const bool validInitialization =
            (desc.initialization == FrameResourceInitialization::Undefined && desc.clearValue.kind == BufferClearValueKind::None) ||
            (desc.initialization == FrameResourceInitialization::Clear && desc.clearValue.kind == BufferClearValueKind::Uint && HasAny(desc.active.usage, rhi::BufferUsage::UnorderedAccess));
        return desc.active.size != 0 && desc.maximumSize >= desc.active.size && ValidBufferUsageBits(desc.active.usage) && validMemory && BufferStateCompatible(desc.active, desc.active.initialState) &&
               !desc.active.virtualResource &&
               (!structured || (desc.active.structureStride != 0 && desc.active.size % desc.active.structureStride == 0 && desc.maximumSize % desc.active.structureStride == 0)) &&
               (desc.active.format == rhi::Format::Unknown || static_cast<u16>(desc.active.format) <= static_cast<u16>(rhi::Format::BC7UNormSrgb)) && validInitialization;
    }

    bool PhysicalTextureDescEqual(const rhi::TextureDesc& left, const rhi::TextureDesc& right) noexcept
    {
        return TextureCreationEqual(left, right);
    }

    bool PhysicalBufferDescEqual(const rhi::BufferDesc& left, const rhi::BufferDesc& right) noexcept
    {
        return BufferCreationEqual(left, right);
    }

    bool TextureStateAllowed(const rhi::TextureDesc& desc, const rhi::ResourceState state) noexcept
    {
        return TextureStateCompatible(desc, state);
    }

    bool BufferStateAllowed(const rhi::BufferDesc& desc, const rhi::ResourceState state) noexcept
    {
        return BufferStateCompatible(desc, state);
    }

    bool QueueStateAllowed(const rhi::QueueType queue, const FrameResourceKind kind, const rhi::ResourceState state) noexcept
    {
        return QueueSupportsState(queue, kind, state);
    }

    bool ResolveFrame(RenderFlowResourceAllocator::Impl& allocator, RenderFlowResourceFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (allocator.state != RenderFlowResourceSessionState::CandidatesSealed)
            return Fail(failure, RenderFlowResourceFailureCode::InvalidPhase, allocator.state, "candidate plan is not sealed");

        allocator.state = RenderFlowResourceSessionState::Resolving;
        allocator.stats.state = allocator.state;
        ExecutionGenerationRef::Impl* const scratch = AllocateGeneration();
        if (scratch == nullptr)
            return FailResolve(allocator, nullptr, failure, RenderFlowResourceFailureCode::CapacityExceeded, "execution generation metadata allocation failed");
        scratch->id = {0, NextGeneration(allocator.executionGeneration)};

        containers::DynamicArray<u32> batchPackets{memory::pools::Rendering::GetInstance()};
        containers::DynamicArray<BatchResolveMap> batchMaps{memory::pools::Rendering::GetInstance()};
        containers::DynamicArray<LogicalSlot> slots{memory::pools::Rendering::GetInstance()};
        containers::DynamicArray<ActiveUseRecord> uses{memory::pools::Rendering::GetInstance()};
        containers::DynamicArray<OpenScopeRecord> scopes{memory::pools::Rendering::GetInstance()};
        containers::DynamicArray<u32> openNamedScopes{memory::pools::Rendering::GetInstance()};
        containers::DynamicArray<AllocationLogicalState> allocationStates{memory::pools::Rendering::GetInstance()};
        containers::DynamicArray<TextureSubresourceLogicalState> textureSubresourceStates{memory::pools::Rendering::GetInstance()};
        containers::HashMap<LogicalNameKey, u32> namedSlots{memory::pools::Rendering::GetInstance()};
        containers::HashMap<u32, u32> commandScopeIndices{memory::pools::Rendering::GetInstance()};
        containers::HashSet<u32> queueGroupsUsed{memory::pools::Rendering::GetInstance()};
        containers::DynamicArray<u32> syncRequestGroups{memory::pools::Rendering::GetInstance()};
        containers::HashMap<u64, u32> openScopeIndices{memory::pools::Rendering::GetInstance()};
        containers::DynamicArray<u8> importedInstalled{memory::pools::Rendering::GetInstance()};

        batchPackets.Resize(allocator.writerBatches.Size(), InvalidRenderFlowResourceIndex);
        batchMaps.Resize(allocator.writerBatches.Size());
        importedInstalled.Resize(allocator.retainedImports.Size(), u8{0});
        for (u32 batchIndex = 0; batchIndex < allocator.writerBatches.Size(); ++batchIndex)
        {
            const CandidateWriterBatch& batch = allocator.writerBatches[batchIndex];
            BatchResolveMap& map = batchMaps[batchIndex];
            map.resourceSlots.Resize(batch.resources.Size(), InvalidRenderFlowResourceIndex);
            map.textureViews.Resize(batch.textureViews.Size());
            map.bufferViews.Resize(batch.bufferViews.Size());
            map.useRecords.Resize(batch.operations.Size(), InvalidRenderFlowResourceIndex);
        }
        if (allocator.writerBatches.Size() > allocator.config.maximumExecutionPackets)
            return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::CapacityExceeded, "execution packet count exceeds allocator capacity");

        for (u32 groupIndex = 0; groupIndex < allocator.queueRequestGroups.Size(); ++groupIndex)
        {
            const QueueRequestGroup& group = allocator.queueRequestGroups[groupIndex];
            if (group.count == 0)
                continue;
            const QueueRequestRecord& request = group.requests[0];
            if (!request.flowGroup.IsValid() || request.flowGroup.value != groupIndex)
                return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::QueueOrCommandScopeMismatch, "allocator request stream contains an invalid GPU flow group");
            if (request.kind == QueueRequestKind::Sync)
            {
                if (group.count != 1)
                    return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::QueueOrCommandScopeMismatch, "one GPU flow group cannot mix synchronization and command-queue requests");
                if (request.sync != rhi::CommandListSyncType::None && request.sync != rhi::CommandListSyncType::ForkAsyncCompute && request.sync != rhi::CommandListSyncType::JoinAsyncCompute)
                    return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::UnsupportedCapability, "queue synchronization request uses an unsupported primitive");
                syncRequestGroups.PushBack(groupIndex);
                continue;
            }
            if (group.count != 2 || group.requests[0].kind != QueueRequestKind::Begin || group.requests[1].kind != QueueRequestKind::End)
                return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::QueueOrCommandScopeMismatch, "queue begin and end requests must be unique and balanced");
            const QueueRequestRecord& end = group.requests[1];
            if (!ValidQueue(request.queue) || end.queue != request.queue)
                return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::QueueOrCommandScopeMismatch, "one GPU flow group cannot contain duplicate or mixed queue requests");
        }

        for (u32 batchIndex = 0; batchIndex < allocator.writerBatches.Size(); ++batchIndex)
        {
            const CandidateWriterBatch& batch = allocator.writerBatches[batchIndex];
            const u32 packetIndex = scratch->packets.Size();
            if (!scratch->packetByNode.Insert(batch.node.value, packetIndex).IsSuccessful())
                return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "execution generation contains duplicate node packets", batch.node);
            CompiledPacket& packet = scratch->packets.EmplaceBack();
            packet.node = batch.node;
            packet.flowGroup = batch.flowGroup;
            packet.commandScope = batch.commandScope;
            if (batch.flowGroup.value < allocator.queueRequestGroups.Size() && allocator.queueRequestGroups[batch.flowGroup.value].count == 2)
            {
                packet.queue = allocator.queueRequestGroups[batch.flowGroup.value].requests[0].queue;
                static_cast<void>(queueGroupsUsed.Insert(batch.flowGroup.value));
            }
            else
                packet.queue = rhi::QueueType::Graphics;
            batchPackets[batchIndex] = packetIndex;
            if (!commandScopeIndices.Insert(batch.commandScope.value, scratch->commandScopes.Size()).IsSuccessful())
                return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::QueueOrCommandScopeMismatch, "allocator request stream contains duplicate command-scope identities", batch.node);
            scratch->commandScopes.PushBack({batch.commandScope, packet.queue, batch.flowGroup.value});
        }
        for (const QueueRequestGroup& group : allocator.queueRequestGroups)
            if (group.count == 2 && group.requests[0].kind == QueueRequestKind::Begin && !queueGroupsUsed.Exist(group.requests[0].flowGroup.value))
                return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::QueueOrCommandScopeMismatch, "queue begin/end requests do not belong to a resource-recording packet");

        bool asyncComputeOpen = false;
        u32 packetCursor = 0;
        u32 producerPacket = InvalidRenderFlowResourceIndex;
        for (const u32 syncRequestGroup : syncRequestGroups)
        {
            const QueueRequestRecord& request = allocator.queueRequestGroups[syncRequestGroup].requests[0];
            while (packetCursor < allocator.writerBatches.Size() && allocator.writerBatches[packetCursor].flowGroup.value < request.flowGroup.value)
            {
                producerPacket = batchPackets[packetCursor];
                ++packetCursor;
            }
            if (packetCursor < allocator.writerBatches.Size() && allocator.writerBatches[packetCursor].flowGroup == request.flowGroup)
                return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::QueueOrCommandScopeMismatch, "queue synchronization request cannot own a resource-recording packet",
                                   allocator.writerBatches[packetCursor].node);
            const u32 consumerPacket = packetCursor < allocator.writerBatches.Size() ? batchPackets[packetCursor] : InvalidRenderFlowResourceIndex;
            if (producerPacket == InvalidRenderFlowResourceIndex)
                return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::QueueOrCommandScopeMismatch, "queue synchronization request has no preceding command scope");
            if (request.sync == rhi::CommandListSyncType::None)
                continue;
            if (consumerPacket == InvalidRenderFlowResourceIndex)
                return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::QueueOrCommandScopeMismatch, "cross-queue synchronization request has no following command scope");
            const CompiledPacket& producer = scratch->packets[producerPacket];
            const CompiledPacket& consumer = scratch->packets[consumerPacket];
            if (request.sync == rhi::CommandListSyncType::ForkAsyncCompute)
            {
                if (asyncComputeOpen || producer.queue != rhi::QueueType::Graphics || consumer.queue != rhi::QueueType::Compute)
                    return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::QueueOrCommandScopeMismatch, "async-compute fork must be a non-nested Graphics-to-Compute boundary");
                asyncComputeOpen = true;
            }
            else
            {
                if (!asyncComputeOpen || producer.queue != rhi::QueueType::Compute || consumer.queue != rhi::QueueType::Graphics)
                    return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::QueueOrCommandScopeMismatch, "async-compute join must close an open Compute-to-Graphics boundary");
                asyncComputeOpen = false;
            }
            scratch->queueDependencies.PushBack({producer.commandScope, consumer.commandScope, request.sync});
        }
        if (asyncComputeOpen)
            return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::QueueOrCommandScopeMismatch, "async-compute fork/join requests must be balanced");

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

        const auto failOperation = [&](const OperationVisit& mergedOperation, const RenderFlowResourceFailureCode code, const char* const message, const ResourceUseId use = {}) noexcept -> bool
        {
            const CandidateWriterBatch& batch = allocator.writerBatches[mergedOperation.batch];
            return FailResolve(allocator, scratch, failure, code, message, batch.node, mergedOperation.position, use);
        };

        u64 compiledOperationCount = 0;
        u64 compiledResourceActionCount = 0;
        for (u32 batchIndex = 0; batchIndex < allocator.writerBatches.Size(); ++batchIndex)
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
                case CandidateOperationKind::ImportTexture:
                case CandidateOperationKind::ImportBuffer:
                {
                    if (!getSlot(mergedOperation.batch, operation.resource, slotIndex))
                        return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "declaration references an invalid candidate resource");
                    LogicalSlot& slot = slots[slotIndex];
                    if (slot.currentAllocation != InvalidRenderFlowResourceIndex)
                        return failOperation(mergedOperation, RenderFlowResourceFailureCode::DescriptorConflict, "declaration cannot replace an already-mapped logical resource");
                    FrameResourceDesc resolvedDesc = operation.resourceDesc;
                    const bool imported = operation.kind == CandidateOperationKind::ImportTexture || operation.kind == CandidateOperationKind::ImportBuffer;
                    if (operation.kind == CandidateOperationKind::DeclareLike)
                    {
                        u32 sourceSlot = InvalidRenderFlowResourceIndex;
                        if (!getSlot(mergedOperation.batch, operation.otherResource, sourceSlot) || slots[sourceSlot].currentAllocation == InvalidRenderFlowResourceIndex)
                            return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "declare-like source is not mapped at this plan position");
                        resolvedDesc = scratch->allocations[slots[sourceSlot].currentAllocation].desc;
                    }
                    else if (imported)
                    {
                        if (!operation.importedResource.IsValid() || operation.importedResource.generation != allocator.sessionGeneration || operation.importedResource.index >= allocator.retainedImports.Size())
                            return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "import references an invalid session import");
                        if (importedInstalled[operation.importedResource.index] != 0)
                            return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidUseOrScope, "one retained import cannot be installed into multiple logical destinations");
                        resolvedDesc = allocator.retainedImports[operation.importedResource.index].desc;
                        importedInstalled[operation.importedResource.index] = 1;
                    }
                    if ((operation.kind == CandidateOperationKind::DeclareTexture && resolvedDesc.kind != FrameResourceKind::Texture) ||
                        (operation.kind == CandidateOperationKind::DeclareBuffer && resolvedDesc.kind != FrameResourceKind::Buffer) ||
                        (operation.kind == CandidateOperationKind::ImportTexture && resolvedDesc.kind != FrameResourceKind::Texture) ||
                        (operation.kind == CandidateOperationKind::ImportBuffer && resolvedDesc.kind != FrameResourceKind::Buffer))
                        return failOperation(mergedOperation, RenderFlowResourceFailureCode::DescriptorConflict, "declaration kind and descriptor kind disagree");
                    LogicalAllocationRecord allocation;
                    allocation.desc = resolvedDesc;
                    allocation.declarationPosition = mergedOperation.position;
                    allocation.imported = imported;
                    allocation.importedResource = imported ? operation.importedResource.index : InvalidRenderFlowResourceIndex;
                    scratch->allocations.PushBack(allocation);
                    AllocationLogicalState logicalState;
                    if (resolvedDesc.kind == FrameResourceKind::Texture)
                    {
                        const u64 count = static_cast<u64>(resolvedDesc.texture.active.mipCount) * resolvedDesc.texture.active.arraySize;
                        if (count > allocator.config.maximumTrackedTextureSubresources || static_cast<u64>(textureSubresourceStates.Size()) + count > allocator.config.maximumTrackedTextureSubresources)
                            return failOperation(mergedOperation, RenderFlowResourceFailureCode::CapacityExceeded, "texture subresource state metadata exceeds the allocator's aggregate capacity");
                        logicalState.textureOffset = textureSubresourceStates.Size();
                        logicalState.textureCount = static_cast<u32>(count);
                        TextureSubresourceLogicalState initial;
                        initial.state = imported ? allocator.retainedImports[operation.importedResource.index].initialState : resolvedDesc.texture.active.initialState;
                        initial.contentsDefined = imported;
                        initial.authoritativeBefore = imported;
                        for (u32 index = 0; index < logicalState.textureCount; ++index)
                            textureSubresourceStates.PushBack(initial);
                    }
                    else
                    {
                        logicalState.bufferState = imported ? allocator.retainedImports[operation.importedResource.index].initialState : resolvedDesc.buffer.active.initialState;
                        logicalState.bufferContentsDefined = imported;
                        logicalState.bufferAuthoritativeBefore = imported;
                    }
                    logicalState.initializationPending = !imported && (resolvedDesc.kind == FrameResourceKind::Texture ? resolvedDesc.texture.initialization == FrameResourceInitialization::Clear
                                                                                                                       : resolvedDesc.buffer.initialization == FrameResourceInitialization::Clear);
                    allocationStates.PushBack(logicalState);
                    openNamedScopes.PushBack(InvalidRenderFlowResourceIndex);
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
                        return failOperation(mergedOperation, RenderFlowResourceFailureCode::DescriptorConflict, "buffer view range, format, or stride is incompatible with its logical resource");
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
                    AllocationLogicalState& allocationState = allocationStates[allocation];
                    const bool initializationClear = allocationState.initializationPending;
                    if (initializationClear && !AccessWrites(access))
                        return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidUseOrScope, "declaration-time clear initialization requires a writable first use", operation.use);
                    if ((texture && content != ResourceContentIntent::Clear && operation.textureUse.clearValue.kind != TextureClearValueKind::None) ||
                        (!texture && content != ResourceContentIntent::Clear && operation.bufferUse.clearValue.kind != BufferClearValueKind::None))
                        return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidUseOrScope, "a resource use supplies a clear value without clear content intent", operation.use);
                    BatchResolveMap& map = batchMaps[mergedOperation.batch];
                    if (!operation.use.IsValid() || operation.use.flowGroup != batch.flowGroup || operation.use.generation != batch.sessionGeneration || operation.use.ordinal != operationIndex ||
                        operation.use.ordinal >= map.useRecords.Size() || map.useRecords[operation.use.ordinal] != InvalidRenderFlowResourceIndex)
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
                    step.beforeActionOffset = packet.actions.Size();
                    if (texture)
                    {
                        if (step.texture.requiredState == rhi::ResourceState::Unknown ||
                            !ResolveSubresources(scratch->allocations[allocation].desc.texture.active, operation.textureUse.subresources, step.texture.subresources))
                            return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidUseOrScope, "texture use contains an invalid required state or subresource range", operation.use);
                        if (operation.textureView.IsValid())
                        {
                            step.textureView = batchMaps[mergedOperation.batch].textureViews[operation.textureView.index].desc;
                            if (!ContainsSubresources(step.texture.subresources, step.textureView.subresources))
                                return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidUseOrScope, "texture use range does not cover every subresource exposed by its view", operation.use);
                        }
                        AllocationLogicalState& logicalState = allocationState;
                        const rhi::TextureDesc& textureDesc = scratch->allocations[allocation].desc.texture.active;
                        const bool explicitClear = content == ResourceContentIntent::Clear;
                        TextureClearValue clearValue;
                        if (initializationClear)
                        {
                            clearValue = scratch->allocations[allocation].desc.texture.clearValue;
                            if (step.texture.subresources.firstMip != 0 || step.texture.subresources.mipCount != textureDesc.mipCount || step.texture.subresources.firstSlice != 0 ||
                                step.texture.subresources.sliceCount != textureDesc.arraySize)
                                return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidUseOrScope, "declaration-time texture clear requires a full-resource first use", operation.use);
                            if (explicitClear && !TextureClearValueEqual(clearValue, step.texture.clearValue))
                                return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidUseOrScope, "declaration and first-use texture clear values disagree", operation.use);
                        }
                        else if (explicitClear)
                        {
                            clearValue = step.texture.clearValue;
                        }
                        CompiledResourceActionKind clearKind = CompiledResourceActionKind::TextureTransition;
                        const bool compileClear = initializationClear || explicitClear;
                        if ((explicitClear && step.texture.clearValue.kind == TextureClearValueKind::None) ||
                            (compileClear && !ClassifyTextureClear(textureDesc, step.texture.requiredState, packet.queue, clearValue, clearKind)))
                            return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidUseOrScope, "texture clear value, format, state, usage, or queue is incompatible", operation.use);
                        if (compileClear && (clearKind == CompiledResourceActionKind::TextureUavFloatClear || clearKind == CompiledResourceActionKind::TextureUavUintClear) &&
                            (step.texture.subresources.firstSlice != 0 || step.texture.subresources.sliceCount != textureDesc.arraySize))
                            return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidUseOrScope, "texture UAV clears must cover every array slice", operation.use);
                        bool requiresUavBarrier = false;
                        for (u32 activeIndex = logicalState.firstActiveUse; activeIndex != InvalidRenderFlowResourceIndex; activeIndex = uses[activeIndex].nextActive)
                        {
                            if (activeIndex >= uses.Size())
                                return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "active resource-use chain contains an invalid index", operation.use);
                            const ActiveUseRecord& active = uses[activeIndex];
                            if (!active.texture || active.ended)
                                return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "active texture-use chain contains an invalid record", operation.use);
                            if (active.packet >= scratch->packets.Size() || scratch->packets[active.packet].queue != packet.queue)
                                return failOperation(mergedOperation, RenderFlowResourceFailureCode::QueueOrCommandScopeMismatch, "simultaneous texture uses cannot span command queues", operation.use);
                            if (SubresourcesOverlap(active.textureSubresources, step.texture.subresources) &&
                                (active.access != LogicalAccessIntent::Read || access != LogicalAccessIntent::Read || active.requiredState != requiredState))
                                return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidUseOrScope, "overlapping active texture uses require the same read-only state", operation.use);
                        }
                        const u32 resourceMipCount = scratch->allocations[allocation].desc.texture.active.mipCount;
                        for (u32 slice = step.texture.subresources.firstSlice; slice < static_cast<u32>(step.texture.subresources.firstSlice) + step.texture.subresources.sliceCount; ++slice)
                        {
                            for (u32 mip = step.texture.subresources.firstMip; mip < static_cast<u32>(step.texture.subresources.firstMip) + step.texture.subresources.mipCount; ++mip)
                            {
                                TextureSubresourceLogicalState& subresource = textureSubresourceStates[logicalState.textureOffset + slice * resourceMipCount + mip];
                                if (content == ResourceContentIntent::Preserve && !subresource.contentsDefined && !compileClear)
                                    return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidUseOrScope, "texture use attempts to preserve undefined subresource contents", operation.use);
                                requiresUavBarrier = requiresUavBarrier || (subresource.hasPriorUse && subresource.state == rhi::ResourceState::UnorderedAccess &&
                                                                            step.texture.requiredState == rhi::ResourceState::UnorderedAccess && (AccessWrites(subresource.lastAccess) || AccessWrites(access)));
                                const bool sameCommandScope = subresource.lastCommandScope == packet.commandScope;
                                if (!sameCommandScope || subresource.state != step.texture.requiredState)
                                {
                                    if (compiledResourceActionCount >= allocator.config.maximumCompiledResourceActions)
                                        return failOperation(mergedOperation, RenderFlowResourceFailureCode::CapacityExceeded, "compiled texture transitions exceed the allocator action capacity",
                                                             operation.use);
                                    CompiledResourceAction action;
                                    action.kind = CompiledResourceActionKind::TextureTransition;
                                    action.logicalAllocation = allocation;
                                    action.before = subresource.authoritativeBefore || sameCommandScope ? subresource.state : rhi::ResourceState::Unknown;
                                    action.after = step.texture.requiredState;
                                    action.textureSubresources = {static_cast<u16>(mip), 1, static_cast<u16>(slice), 1};
                                    packet.actions.PushBack(action);
                                    ++compiledResourceActionCount;
                                }
                                subresource.state = step.texture.requiredState;
                                subresource.lastCommandScope = packet.commandScope;
                                subresource.lastAccess = access;
                                subresource.hasPriorUse = true;
                                subresource.authoritativeBefore = false;
                            }
                        }
                        if (requiresUavBarrier)
                        {
                            if (compiledResourceActionCount >= allocator.config.maximumCompiledResourceActions)
                                return failOperation(mergedOperation, RenderFlowResourceFailureCode::CapacityExceeded, "compiled texture UAV barriers exceed the allocator action capacity", operation.use);
                            CompiledResourceAction action;
                            action.kind = CompiledResourceActionKind::TextureUavBarrier;
                            action.logicalAllocation = allocation;
                            packet.actions.PushBack(action);
                            ++compiledResourceActionCount;
                        }
                        if (compileClear)
                        {
                            if (compiledResourceActionCount >= allocator.config.maximumCompiledResourceActions)
                                return failOperation(mergedOperation, RenderFlowResourceFailureCode::CapacityExceeded, "compiled texture clears exceed the allocator action capacity", operation.use);
                            CompiledResourceAction action;
                            action.kind = clearKind;
                            action.logicalAllocation = allocation;
                            action.textureSubresources = step.texture.subresources;
                            action.textureClearValue = clearValue;
                            packet.actions.PushBack(action);
                            ++compiledResourceActionCount;
                            for (u32 slice = step.texture.subresources.firstSlice; slice < static_cast<u32>(step.texture.subresources.firstSlice) + step.texture.subresources.sliceCount; ++slice)
                                for (u32 mip = step.texture.subresources.firstMip; mip < static_cast<u32>(step.texture.subresources.firstMip) + step.texture.subresources.mipCount; ++mip)
                                    textureSubresourceStates[logicalState.textureOffset + slice * resourceMipCount + mip].contentsDefined = true;
                            logicalState.initializationPending = false;
                        }
                    }
                    else
                    {
                        if (step.buffer.requiredState == rhi::ResourceState::Unknown)
                            return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidUseOrScope, "buffer use contains an invalid required state", operation.use);
                        if (operation.bufferView.IsValid())
                            step.bufferView = batchMaps[mergedOperation.batch].bufferViews[operation.bufferView.index].desc;
                        AllocationLogicalState& logicalState = allocationState;
                        const bool explicitClear = content == ResourceContentIntent::Clear;
                        BufferClearValue clearValue;
                        if (initializationClear)
                        {
                            clearValue = scratch->allocations[allocation].desc.buffer.clearValue;
                            if (explicitClear && !BufferClearValueEqual(clearValue, step.buffer.clearValue))
                                return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidUseOrScope, "declaration and first-use buffer clear values disagree", operation.use);
                        }
                        else if (explicitClear)
                        {
                            clearValue = step.buffer.clearValue;
                        }
                        CompiledResourceActionKind clearKind = CompiledResourceActionKind::BufferTransition;
                        const bool compileClear = initializationClear || explicitClear;
                        if ((explicitClear && step.buffer.clearValue.kind == BufferClearValueKind::None) ||
                            (compileClear && !ClassifyBufferClear(scratch->allocations[allocation].desc.buffer.active, step.buffer.requiredState, packet.queue, clearValue, clearKind)))
                            return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidUseOrScope, "buffer clear value, state, usage, or queue is incompatible", operation.use);
                        for (u32 activeIndex = logicalState.firstActiveUse; activeIndex != InvalidRenderFlowResourceIndex; activeIndex = uses[activeIndex].nextActive)
                        {
                            if (activeIndex >= uses.Size())
                                return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "active resource-use chain contains an invalid index", operation.use);
                            const ActiveUseRecord& active = uses[activeIndex];
                            if (active.texture || active.ended)
                                return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "active buffer-use chain contains an invalid record", operation.use);
                            if (active.packet >= scratch->packets.Size() || scratch->packets[active.packet].queue != packet.queue)
                                return failOperation(mergedOperation, RenderFlowResourceFailureCode::QueueOrCommandScopeMismatch, "simultaneous buffer uses cannot span command queues", operation.use);
                            if (active.access != LogicalAccessIntent::Read || access != LogicalAccessIntent::Read || active.requiredState != requiredState)
                                return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidUseOrScope, "overlapping active buffer uses require the same read-only state", operation.use);
                        }
                        if (content == ResourceContentIntent::Preserve && !logicalState.bufferContentsDefined && !compileClear)
                            return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidUseOrScope, "buffer use attempts to preserve undefined contents", operation.use);
                        const bool sameCommandScope = logicalState.bufferLastCommandScope == packet.commandScope;
                        if (!sameCommandScope || logicalState.bufferState != step.buffer.requiredState)
                        {
                            if (compiledResourceActionCount >= allocator.config.maximumCompiledResourceActions)
                                return failOperation(mergedOperation, RenderFlowResourceFailureCode::CapacityExceeded, "compiled buffer transitions exceed the allocator action capacity", operation.use);
                            CompiledResourceAction action;
                            action.kind = CompiledResourceActionKind::BufferTransition;
                            action.logicalAllocation = allocation;
                            action.before = logicalState.bufferAuthoritativeBefore || sameCommandScope ? logicalState.bufferState : rhi::ResourceState::Unknown;
                            action.after = step.buffer.requiredState;
                            packet.actions.PushBack(action);
                            ++compiledResourceActionCount;
                        }
                        if (logicalState.bufferHasPriorUse && logicalState.bufferState == rhi::ResourceState::UnorderedAccess && step.buffer.requiredState == rhi::ResourceState::UnorderedAccess &&
                            (AccessWrites(logicalState.bufferLastAccess) || AccessWrites(access)))
                        {
                            if (compiledResourceActionCount >= allocator.config.maximumCompiledResourceActions)
                                return failOperation(mergedOperation, RenderFlowResourceFailureCode::CapacityExceeded, "compiled buffer UAV barriers exceed the allocator action capacity", operation.use);
                            CompiledResourceAction action;
                            action.kind = CompiledResourceActionKind::BufferUavBarrier;
                            action.logicalAllocation = allocation;
                            packet.actions.PushBack(action);
                            ++compiledResourceActionCount;
                        }
                        if (compileClear)
                        {
                            if (compiledResourceActionCount >= allocator.config.maximumCompiledResourceActions)
                                return failOperation(mergedOperation, RenderFlowResourceFailureCode::CapacityExceeded, "compiled buffer clears exceed the allocator action capacity", operation.use);
                            CompiledResourceAction action;
                            action.kind = clearKind;
                            action.logicalAllocation = allocation;
                            action.bufferClearValue = clearValue;
                            packet.actions.PushBack(action);
                            ++compiledResourceActionCount;
                            logicalState.bufferContentsDefined = true;
                            logicalState.initializationPending = false;
                        }
                        logicalState.bufferState = step.buffer.requiredState;
                        logicalState.bufferLastCommandScope = packet.commandScope;
                        logicalState.bufferLastAccess = access;
                        logicalState.bufferHasPriorUse = true;
                        logicalState.bufferAuthoritativeBefore = false;
                    }
                    step.beforeActionCount = packet.actions.Size() - step.beforeActionOffset;
                    step.afterActionOffset = packet.actions.Size();
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
                    activeUse.beginPosition = mergedOperation.position;
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
                    if (!operation.use.IsValid() || operation.use.flowGroup != batch.flowGroup || operation.use.generation != batch.sessionGeneration || operation.use.ordinal >= map.useRecords.Size())
                        return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidUseOrScope, "resource use end has an invalid identity", operation.use);
                    const u32 useIndex = map.useRecords[operation.use.ordinal];
                    ActiveUseRecord* const active = useIndex < uses.Size() ? &uses[useIndex] : nullptr;
                    if (active == nullptr || active->id != operation.use || active->ended || active->packet != packetIndex)
                        return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidUseOrScope, "resource use end is missing, duplicated, or owned by another packet", operation.use);
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
                            for (u32 slice = active->textureSubresources.firstSlice; slice < static_cast<u32>(active->textureSubresources.firstSlice) + active->textureSubresources.sliceCount; ++slice)
                                for (u32 mip = active->textureSubresources.firstMip; mip < static_cast<u32>(active->textureSubresources.firstMip) + active->textureSubresources.mipCount; ++mip)
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
                    active->endStep = packet.steps.Size();
                    active->endPosition = mergedOperation.position;
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
                    scopes.PushBack({operation.scope, allocation, InvalidRenderFlowResourceIndex, mergedOperation.position, false, false});
                    scratch->allocations[allocation].used = true;
                    TouchLifetime(scratch->allocations[allocation], mergedOperation.position, packet);
                    break;
                }
                case CandidateOperationKind::ScopeClose:
                {
                    u32 scopeIndex = InvalidRenderFlowResourceIndex;
                    OpenScopeRecord* const found = operation.scope.IsValid() && openScopeIndices.Find(operation.scope.value, scopeIndex) && scopeIndex < scopes.Size() ? &scopes[scopeIndex] : nullptr;
                    if (found == nullptr || found->closed)
                        return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidUseOrScope, "resource scope end has no matching open endpoint");
                    found->closed = true;
                    TouchLifetime(scratch->allocations[found->allocation], mergedOperation.position, packet);
                    break;
                }
                case CandidateOperationKind::NamedScopeOpen:
                {
                    if (!getSlot(mergedOperation.batch, operation.resource, slotIndex) || slots[slotIndex].currentAllocation == InvalidRenderFlowResourceIndex)
                        return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidUseOrScope, "named resource scope begins before the logical resource is mapped");
                    const u32 allocation = slots[slotIndex].currentAllocation;
                    if (allocation >= openNamedScopes.Size())
                        return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidUseOrScope, "named resource scope references an invalid allocation");
                    const u32 previous = openNamedScopes[allocation];
                    openNamedScopes[allocation] = scopes.Size();
                    scopes.PushBack({{}, allocation, previous, mergedOperation.position, true, false});
                    scratch->allocations[allocation].used = true;
                    TouchLifetime(scratch->allocations[allocation], mergedOperation.position, packet);
                    break;
                }
                case CandidateOperationKind::NamedScopeClose:
                {
                    if (!getSlot(mergedOperation.batch, operation.resource, slotIndex) || slots[slotIndex].currentAllocation == InvalidRenderFlowResourceIndex)
                        return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidUseOrScope, "named resource scope ends before the logical resource is mapped");
                    const u32 allocation = slots[slotIndex].currentAllocation;
                    const u32 scopeIndex = allocation < openNamedScopes.Size() ? openNamedScopes[allocation] : InvalidRenderFlowResourceIndex;
                    OpenScopeRecord* const found = scopeIndex < scopes.Size() ? &scopes[scopeIndex] : nullptr;
                    if (found == nullptr || !found->named || found->closed || found->allocation != allocation)
                        return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidUseOrScope, "named resource scope end has no matching open endpoint");
                    found->closed = true;
                    openNamedScopes[allocation] = found->previous;
                    TouchLifetime(scratch->allocations[allocation], mergedOperation.position, packet);
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
                    if (!operation.exportSlot.IsValid() || operation.exportSlot.generation != allocator.sessionGeneration || operation.exportSlot.index >= allocator.reservedExportSlots)
                        return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "export references an unreserved session slot");
                    if (operation.exportDesc.readiness != ExportReadinessKind::SameQueueContinuation && operation.exportDesc.readiness != ExportReadinessKind::ExplicitFenceSignal)
                        return failOperation(mergedOperation, RenderFlowResourceFailureCode::UnsupportedCapability, "export readiness kind is unsupported");
                    for (const PendingExportRecord& exportRecord : scratch->pendingExports)
                        if (exportRecord.slot == operation.exportSlot)
                            return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "export slot is requested more than once");
                    PendingExportRecord pending;
                    pending.slot = operation.exportSlot;
                    pending.logicalSlot = slotIndex;
                    pending.terminalState = operation.exportDesc.terminalState;
                    pending.terminalQueue = operation.exportDesc.terminalQueue;
                    pending.readiness = operation.exportDesc.readiness;
                    scratch->pendingExports.PushBack(pending);
                    break;
                }
                }
            }
        }

        for (const ActiveUseRecord& use : uses)
            if (!use.ended)
                return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::InvalidUseOrScope, "a resource use has no end endpoint", scratch->packets[use.packet].node, {}, use.id);
        for (const OpenScopeRecord& scope : scopes)
            if (!scope.closed)
                return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::InvalidUseOrScope, "a cross-node resource scope has no close endpoint", {}, scope.begin);

        for (u32 allocationIndex = 0; allocationIndex < scratch->allocations.Size(); ++allocationIndex)
        {
            const ActiveUseRecord* previous = nullptr;
            for (const ActiveUseRecord& use : uses)
            {
                if (use.allocation != allocationIndex || !use.ended)
                    continue;
                if (previous != nullptr)
                {
                    if (previous->packet >= scratch->packets.Size() || use.packet >= scratch->packets.Size())
                        return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "cross-queue dependency validation encountered an invalid packet");
                    const CompiledPacket& producer = scratch->packets[previous->packet];
                    const CompiledPacket& consumer = scratch->packets[use.packet];
                    if (producer.queue != consumer.queue)
                    {
                        if (!QueueOrderProved(producer.commandScope, producer.queue, consumer.commandScope, consumer.queue, scratch->commandScopes, scratch->queueDependencies))
                            return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::QueueOrCommandScopeMismatch,
                                               "cross-queue resource use is not ordered through an executable Graphics/Compute fork or join boundary", consumer.node, use.beginPosition, use.id);
                    }
                }
                previous = &use;
            }
        }

        if (!scratch->pendingExports.Empty() && allocator.config.allowLogicalOnlyValidation)
            return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::UnsupportedCapability, "terminal exports require real standalone physical resources");
        if (scratch->pendingExports.Size() >
            allocator.config.maximumOutstandingPublishedExports -
                (allocator.publishedExports.Size() <= allocator.config.maximumOutstandingPublishedExports ? allocator.publishedExports.Size() : allocator.config.maximumOutstandingPublishedExports))
            return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::CapacityExceeded, "outstanding terminal export publication capacity is exhausted");
        allocator.publishedExports.Reserve(allocator.publishedExports.Size() + scratch->pendingExports.Size());
        for (u32 exportIndex = 0; exportIndex < scratch->pendingExports.Size(); ++exportIndex)
        {
            PendingExportRecord& exportRecord = scratch->pendingExports[exportIndex];
            if (exportRecord.logicalSlot >= slots.Size() || slots[exportRecord.logicalSlot].currentAllocation == InvalidRenderFlowResourceIndex)
                return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "terminal export has no mapped logical allocation");
            exportRecord.allocation = slots[exportRecord.logicalSlot].currentAllocation;
            for (u32 prior = 0; prior < exportIndex; ++prior)
                if (scratch->pendingExports[prior].allocation == exportRecord.allocation)
                    return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::InvalidUseOrScope, "one terminal logical allocation cannot publish to multiple export slots");
            scratch->allocations[exportRecord.allocation].exported = true;
        }

        for (u32 allocationIndex = 0; allocationIndex < scratch->allocations.Size(); ++allocationIndex)
        {
            LogicalAllocationRecord& allocation = scratch->allocations[allocationIndex];
            if (!allocation.imported)
                continue;
            if (allocation.importedResource >= allocator.retainedImports.Size())
                return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "imported allocation lost its retained session resource");
            const RetainedImportRecord& imported = allocator.retainedImports[allocation.importedResource];
            if (!allocation.used)
            {
                if (imported.initialState != imported.terminalState || imported.initialQueue != imported.terminalQueue)
                    return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::IncompleteExecution, "an imported resource without an executable use cannot change terminal state or queue");
                continue;
            }

            ActiveUseRecord* initialUse = nullptr;
            ActiveUseRecord* terminalUse = nullptr;
            for (ActiveUseRecord& use : uses)
            {
                if (use.allocation != allocationIndex || !use.ended)
                    continue;
                if (initialUse == nullptr)
                    initialUse = &use;
                terminalUse = &use;
                if (imported.readiness == ImportReadinessKind::ExplicitFenceWait)
                {
                    if (use.packet >= scratch->packets.Size())
                        return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "imported wait has an invalid consumer packet");
                    u64& wait = scratch->packets[use.packet].incomingWaits[static_cast<u32>(imported.incomingWait.queue)];
                    if (imported.incomingWait.value > wait)
                        wait = imported.incomingWait.value;
                }
            }
            if (initialUse == nullptr || terminalUse == nullptr)
                return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::IncompleteExecution, "a used imported resource has no complete use endpoints");
            if (initialUse->packet >= scratch->packets.Size() || terminalUse->packet >= scratch->packets.Size() || terminalUse->endStep >= scratch->packets[terminalUse->packet].steps.Size())
                return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "retained import use endpoint has an invalid compiled packet or step");
            const rhi::QueueType firstQueue = scratch->packets[initialUse->packet].queue;
            if ((imported.readiness == ImportReadinessKind::SameQueueContinuation && firstQueue != imported.initialQueue) ||
                scratch->packets[terminalUse->packet].queue != imported.terminalQueue)
                return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::QueueOrCommandScopeMismatch, "retained import first and final uses must match its registered continuation queues");
            if (!QueueStateAllowed(firstQueue, allocation.desc.kind, imported.initialState) ||
                (firstQueue != imported.initialQueue && imported.initialQueue == rhi::QueueType::Copy && imported.initialState != rhi::ResourceState::Common))
                return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::QueueOrCommandScopeMismatch, "incoming import state is not legal on the consumer queue; Copy handoffs require Common");
            CompiledPacket& terminalPacket = scratch->packets[terminalUse->packet];
            CompiledStep& terminalStep = terminalPacket.steps[terminalUse->endStep];
            terminalStep.afterActionOffset = terminalPacket.actions.Size();
            if (allocation.desc.kind == FrameResourceKind::Texture)
            {
                AllocationLogicalState& logicalState = allocationStates[allocationIndex];
                const u32 mipCount = allocation.desc.texture.active.mipCount;
                if (imported.presentationAcquisition.IsValid())
                {
                    if (scratch->presentationAcquisition.IsValid() || imported.initialState != rhi::ResourceState::Present ||
                        imported.terminalState != rhi::ResourceState::Present || imported.initialQueue != rhi::QueueType::Graphics ||
                        imported.terminalQueue != rhi::QueueType::Graphics || terminalPacket.queue != rhi::QueueType::Graphics || mipCount != 1 ||
                        allocation.desc.texture.active.arraySize != 1)
                        return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::BackendContractViolation,
                                           "presentation import does not terminate once on the Graphics queue");
                    if (compiledResourceActionCount >= allocator.config.maximumCompiledResourceActions)
                        return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::CapacityExceeded,
                                           "presentation transition exceeds the allocator action capacity");
                    CompiledResourceAction action;
                    action.kind = CompiledResourceActionKind::SwapChainPresentTransition;
                    action.logicalAllocation = allocationIndex;
                    action.before = textureSubresourceStates[logicalState.textureOffset].state;
                    action.after = rhi::ResourceState::Present;
                    terminalPacket.actions.PushBack(action);
                    ++compiledResourceActionCount;
                    textureSubresourceStates[logicalState.textureOffset].state = rhi::ResourceState::Present;
                    scratch->presentationAcquisition = imported.presentationAcquisition;
                    scratch->presentationCommandScope = terminalPacket.commandScope;
                }
                else
                {
                    for (u32 slice = 0; slice < allocation.desc.texture.active.arraySize; ++slice)
                    {
                        for (u32 mip = 0; mip < mipCount; ++mip)
                        {
                            TextureSubresourceLogicalState& subresource = textureSubresourceStates[logicalState.textureOffset + slice * mipCount + mip];
                            if (subresource.state == imported.terminalState)
                                continue;
                            if (compiledResourceActionCount >= allocator.config.maximumCompiledResourceActions)
                                return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::CapacityExceeded,
                                                   "retained texture terminal transitions exceed the allocator action capacity");
                            CompiledResourceAction action;
                            action.kind = CompiledResourceActionKind::TextureTransition;
                            action.logicalAllocation = allocationIndex;
                            action.before = subresource.state;
                            action.after = imported.terminalState;
                            action.textureSubresources = {static_cast<u16>(mip), 1, static_cast<u16>(slice), 1};
                            terminalPacket.actions.PushBack(action);
                            ++compiledResourceActionCount;
                            subresource.state = imported.terminalState;
                        }
                    }
                }
            }
            else
            {
                AllocationLogicalState& logicalState = allocationStates[allocationIndex];
                if (logicalState.bufferState != imported.terminalState)
                {
                    if (compiledResourceActionCount >= allocator.config.maximumCompiledResourceActions)
                        return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::CapacityExceeded, "retained buffer terminal transition exceeds the allocator action capacity");
                    CompiledResourceAction action;
                    action.kind = CompiledResourceActionKind::BufferTransition;
                    action.logicalAllocation = allocationIndex;
                    action.before = logicalState.bufferState;
                    action.after = imported.terminalState;
                    terminalPacket.actions.PushBack(action);
                    ++compiledResourceActionCount;
                    logicalState.bufferState = imported.terminalState;
                }
            }
            terminalStep.afterActionCount = terminalPacket.actions.Size() - terminalStep.afterActionOffset;
        }

        if (!allocator.config.allowLogicalOnlyValidation)
            for (const LogicalAllocationRecord& allocation : scratch->allocations)
            {
                if (!allocation.used || !allocation.imported)
                    continue;
                const RetainedImportRecord& imported = allocator.retainedImports[allocation.importedResource];
                const rhi::ResourceState nativeInitial = allocation.desc.kind == FrameResourceKind::Texture ? allocation.desc.texture.active.initialState : allocation.desc.buffer.active.initialState;
                const bool automaticReset = allocation.desc.kind == FrameResourceKind::Texture ? allocation.desc.texture.active.keepInitialState : allocation.desc.buffer.active.keepInitialState;
                if (automaticReset && (imported.initialState != nativeInitial || imported.terminalState != nativeInitial))
                    return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::UnsupportedCapability, "retained import entry/terminal states disagree with native close-time restoration");
                if (!automaticReset && allocation.desc.kind == FrameResourceKind::Buffer && (imported.initialState != rhi::ResourceState::Common || imported.terminalState != rhi::ResourceState::Common))
                    return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::UnsupportedCapability, "explicit buffer imports require Common entry and terminal states");
            }

        for (PendingExportRecord& exportRecord : scratch->pendingExports)
        {
            if (exportRecord.allocation >= scratch->allocations.Size())
                return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "terminal export lost its logical allocation");
            LogicalAllocationRecord& allocation = scratch->allocations[exportRecord.allocation];
            if (!allocation.used || !allocation.firstUsePosition.IsValid())
                return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::IncompleteExecution, "terminal export requires at least one executable resource use");
            if ((allocation.desc.kind == FrameResourceKind::Texture && (!TextureStateCompatible(allocation.desc.texture.active, exportRecord.terminalState) ||
                                                                        !QueueSupportsState(exportRecord.terminalQueue, FrameResourceKind::Texture, exportRecord.terminalState))) ||
                (allocation.desc.kind == FrameResourceKind::Buffer &&
                 (!BufferStateCompatible(allocation.desc.buffer.active, exportRecord.terminalState) || !QueueSupportsState(exportRecord.terminalQueue, FrameResourceKind::Buffer, exportRecord.terminalState))))
                return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::InvalidUseOrScope, "terminal export state is incompatible with its resource descriptor or queue");
            if (allocation.imported)
            {
                if (allocation.importedResource >= allocator.retainedImports.Size())
                    return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "imported terminal export lost its retained resource");
                const RetainedImportRecord& imported = allocator.retainedImports[allocation.importedResource];
                if (imported.terminalState != exportRecord.terminalState || imported.terminalQueue != exportRecord.terminalQueue)
                    return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::DescriptorConflict, "imported terminal export must match the import release contract");
            }

            ActiveUseRecord* terminalUse = nullptr;
            for (ActiveUseRecord& use : uses)
            {
                if (use.allocation != exportRecord.allocation || !use.ended)
                    continue;
                if (terminalUse == nullptr || terminalUse->endPosition.flowGroup.value < use.endPosition.flowGroup.value ||
                    (terminalUse->endPosition.flowGroup == use.endPosition.flowGroup && terminalUse->endPosition.ordinal < use.endPosition.ordinal))
                    terminalUse = &use;
            }
            if (terminalUse == nullptr || terminalUse->packet >= scratch->packets.Size() || terminalUse->endStep >= scratch->packets[terminalUse->packet].steps.Size())
                return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::IncompleteExecution, "terminal export has no valid final resource-use endpoint");
            CompiledPacket& terminalPacket = scratch->packets[terminalUse->packet];
            if (terminalPacket.queue != exportRecord.terminalQueue)
                return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::QueueOrCommandScopeMismatch, "terminal export queue must match the resource's final executable use",
                                   terminalPacket.node);
            CompiledStep& terminalStep = terminalPacket.steps[terminalUse->endStep];
            const u32 actionStart = terminalPacket.actions.Size();
            if (allocation.desc.kind == FrameResourceKind::Texture)
            {
                AllocationLogicalState& logicalState = allocationStates[exportRecord.allocation];
                const u32 mipCount = allocation.desc.texture.active.mipCount;
                for (u32 slice = 0; slice < allocation.desc.texture.active.arraySize; ++slice)
                {
                    for (u32 mip = 0; mip < mipCount; ++mip)
                    {
                        TextureSubresourceLogicalState& subresource = textureSubresourceStates[logicalState.textureOffset + slice * mipCount + mip];
                        if (subresource.state == exportRecord.terminalState)
                            continue;
                        if (compiledResourceActionCount >= allocator.config.maximumCompiledResourceActions)
                            return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::CapacityExceeded, "terminal texture export transitions exceed the allocator action capacity");
                        CompiledResourceAction action;
                        action.kind = CompiledResourceActionKind::TextureTransition;
                        action.logicalAllocation = exportRecord.allocation;
                        action.before = subresource.state;
                        action.after = exportRecord.terminalState;
                        action.textureSubresources = {static_cast<u16>(mip), 1, static_cast<u16>(slice), 1};
                        terminalPacket.actions.PushBack(action);
                        ++compiledResourceActionCount;
                        subresource.state = exportRecord.terminalState;
                    }
                }
            }
            else
            {
                AllocationLogicalState& logicalState = allocationStates[exportRecord.allocation];
                if (logicalState.bufferState != exportRecord.terminalState)
                {
                    if (compiledResourceActionCount >= allocator.config.maximumCompiledResourceActions)
                        return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::CapacityExceeded, "terminal buffer export transition exceeds the allocator action capacity");
                    CompiledResourceAction action;
                    action.kind = CompiledResourceActionKind::BufferTransition;
                    action.logicalAllocation = exportRecord.allocation;
                    action.before = logicalState.bufferState;
                    action.after = exportRecord.terminalState;
                    terminalPacket.actions.PushBack(action);
                    ++compiledResourceActionCount;
                    logicalState.bufferState = exportRecord.terminalState;
                }
            }
            const u32 actionCount = terminalPacket.actions.Size() - actionStart;
            if (actionCount != 0)
            {
                if (terminalStep.afterActionCount != 0)
                    return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::BackendContractViolation, "terminal export attempted to append a second non-contiguous after-action slice");
                terminalStep.afterActionOffset = actionStart;
                terminalStep.afterActionCount = actionCount;
            }
            exportRecord.terminalCommandScope = terminalPacket.commandScope;
            exportRecord.kind = allocation.desc.kind;
        }

        const u32 physicalGeneration = scratch->id.generation;
        scratch->hasNativeResourceBindings = !allocator.config.allowLogicalOnlyValidation;
        containers::DynamicArray<u32> physicalAssignmentOrder{memory::pools::Rendering::GetInstance()};
        containers::DynamicArray<WholeFrameLane> wholeFrameLanes{memory::pools::Rendering::GetInstance()};
        physicalAssignmentOrder.Reserve(scratch->allocations.Size());
        wholeFrameLanes.Reserve(scratch->allocations.Size());
        for (u32 allocation = 0; allocation < scratch->allocations.Size(); ++allocation)
            if (scratch->allocations[allocation].used)
            {
                const LogicalAllocationRecord& logical = scratch->allocations[allocation];
                if (!logical.firstUsePosition.IsValid() || !logical.lastUsePosition.IsValid() || !logical.firstUseCommandScope.IsValid() || !logical.lastUseCommandScope.IsValid())
                    return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::BackendContractViolation, "used logical allocation has an incomplete compiled lifetime");
                physicalAssignmentOrder.PushBack(allocation);
            }
        std::sort(physicalAssignmentOrder.Begin(), physicalAssignmentOrder.End(),
                  [&scratch](const u32 left, const u32 right) noexcept
                  {
                      const PlanPosition leftPosition = scratch->allocations[left].firstUsePosition;
                      const PlanPosition rightPosition = scratch->allocations[right].firstUsePosition;
                      if (leftPosition == rightPosition)
                          return left < right;
                      return PositionBefore(leftPosition, rightPosition);
                  });

        PlacedRangePlan placedPlan;
        containers::DynamicArray<PlacedRangeRequest> placedRequests{memory::pools::Rendering::GetInstance()};
        containers::DynamicArray<PlacedObjectRequest> placedObjects{memory::pools::Rendering::GetInstance()};
        if (scratch->hasNativeResourceBindings && allocator.activePolicy.enablePlacedResources)
        {
            const rhi::Capabilities::PlacedResourceProfile& profile = rhi::GetCapabilities().placedResources;
            u64 maximumRequiredAlignment = 1;
            u64 minimumSupportedHeapAlignment = ~u64{0};
            placedRequests.Reserve(physicalAssignmentOrder.Size());
            placedObjects.Reserve(physicalAssignmentOrder.Size());
            for (const u32 allocation : physicalAssignmentOrder)
            {
                const LogicalAllocationRecord& logical = scratch->allocations[allocation];
                if (logical.imported || logical.exported || (logical.queueMask != QueueMask(rhi::QueueType::Graphics) && logical.queueMask != QueueMask(rhi::QueueType::Compute)))
                    continue;
                u32 firstScopeIndex = InvalidRenderFlowResourceIndex;
                if (!commandScopeIndices.Find(logical.firstUseCommandScope.value, firstScopeIndex) || firstScopeIndex >= scratch->commandScopes.Size())
                    return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::BackendContractViolation, "placed candidate first-use scope is absent from the compiled command-scope stream");
                const rhi::QueueType queue = scratch->commandScopes[firstScopeIndex].queue;
                if ((queue == rhi::QueueType::Graphics && !profile.sameQueueGraphics) || (queue == rhi::QueueType::Compute && !profile.sameQueueCompute))
                    continue;
                if (logical.desc.kind == FrameResourceKind::Texture && queue == rhi::QueueType::Compute &&
                    HasAny(logical.desc.texture.active.usage, rhi::TextureUsage::RenderTarget | rhi::TextureUsage::DepthStencil) &&
                    !HasAny(logical.desc.texture.active.usage, rhi::TextureUsage::UnorderedAccess))
                    continue;
                const bool explicitState = logical.desc.kind == FrameResourceKind::Texture ? !logical.desc.texture.active.keepInitialState : !logical.desc.buffer.active.keepInitialState;
                const rhi::ResourceState initialState = logical.desc.kind == FrameResourceKind::Texture ? logical.desc.texture.active.initialState : logical.desc.buffer.active.initialState;
                // Explicit placed reuse has one portable baseline today: Common on one authored Graphics or Compute queue.
                if (explicitState && initialState != rhi::ResourceState::Common)
                    continue;
                const rhi::Capabilities::PlacedResourceClass& placedClass = logical.desc.kind == FrameResourceKind::Texture ? profile.textures : profile.buffers;
                if (!placedClass.IsSupported())
                    continue;

                FrameResourceDesc physicalDesc = DedicatedResourcePhysicalDesc(logical.desc);
                rhi::MemoryRequirements requirements;
                rhi::Failure rhiFailure;
                bool queried = false;
                if (physicalDesc.kind == FrameResourceKind::Texture)
                {
                    physicalDesc.texture.active.virtualResource = true;
                    queried = rhi::GetMemoryRequirements(physicalDesc.texture.active, requirements, &rhiFailure);
                }
                else if (physicalDesc.kind == FrameResourceKind::Buffer)
                {
                    physicalDesc.buffer.active.virtualResource = true;
                    queried = rhi::GetMemoryRequirements(physicalDesc.buffer.active, requirements, &rhiFailure);
                }
                if (!queried)
                    return FailResolve(allocator, scratch, failure, MapRhiFailure(rhiFailure, RhiFailureContext::BackendContract),
                                       rhiFailure.message[0] != '\0' ? rhiFailure.message : "placed candidate requirement query failed");
                if (requirements.size == 0 || requirements.alignment == 0 || requirements.compatibilityClass != placedClass.compatibilityClass || requirements.heapCategory != placedClass.heapCategory ||
                    requirements.memoryType != rhi::MemoryType::DeviceLocal || requirements.alignment > placedClass.maximumHeapAlignment)
                    return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::BackendContractViolation, "placed candidate requirements disagree with the advertised RHI profile");

                PlacedRangeRequest request;
                request.allocation = allocation;
                request.kind = logical.desc.kind;
                request.queue = queue;
                request.firstAcquire = logical.firstUsePosition;
                request.lastRelease = logical.lastUsePosition;
                request.requirements = requirements;
                placedRequests.PushBack(request);
                placedObjects.PushBack({allocation, physicalDesc});
                if (requirements.alignment > maximumRequiredAlignment)
                    maximumRequiredAlignment = requirements.alignment;
                if (placedClass.maximumHeapAlignment < minimumSupportedHeapAlignment)
                    minimumSupportedHeapAlignment = placedClass.maximumHeapAlignment;
            }

            // One plan uses one legal heap-alignment value. If the active RHI
            // advertises incompatible class ceilings, retain the mandatory
            // dedicated path instead of inventing an unsupported native heap.
            if (!placedRequests.Empty() && maximumRequiredAlignment <= minimumSupportedHeapAlignment)
            {
                PlacedRangePlannerConfig plannerConfig;
                plannerConfig.heapAlignment = maximumRequiredAlignment;
                if (!BuildPlacedRangePlan(placedRequests, plannerConfig, placedPlan, failure))
                {
                    const RenderFlowResourceFailureCode code = failure != nullptr ? failure->code : RenderFlowResourceFailureCode::BackendContractViolation;
                    const char* const message = failure != nullptr && failure->message != nullptr ? failure->message : "placed range planning failed";
                    return FailResolve(allocator, scratch, failure, code, message);
                }
                if (!allocator.placedPool.Acquire(placedPlan, placedObjects, allocator.frameSerial, scratch->placedBatch, failure))
                {
                    const RenderFlowResourceFailureCode code = failure != nullptr ? failure->code : RenderFlowResourceFailureCode::DeviceLostOrBackendFailure;
                    const char* const message = failure != nullptr && failure->message != nullptr ? failure->message : "placed-resource physical assignment failed";
                    return FailResolve(allocator, scratch, failure, code, message);
                }
                for (const PlacedResourceAssignment& assignment : scratch->placedBatch.assignments)
                {
                    if (!assignment.IsValid() || assignment.allocation >= scratch->allocations.Size() || scratch->allocations[assignment.allocation].physical.IsValid())
                        return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::BackendContractViolation, "placed provider returned an invalid or duplicate logical assignment");
                    PhysicalBindingRecord binding;
                    binding.kind = PhysicalBindingKind::PlacedPool;
                    binding.poolEntry = assignment.objectEntry;
                    binding.texture = assignment.texture;
                    binding.buffer = assignment.buffer;
                    const FrameResourceDesc& assignedDesc = scratch->allocations[assignment.allocation].desc;
                    binding.explicitState = assignedDesc.kind == FrameResourceKind::Texture ? !assignedDesc.texture.active.keepInitialState : !assignedDesc.buffer.active.keepInitialState;
                    const u32 bindingIndex = scratch->physicalBindings.Size();
                    scratch->physicalBindings.PushBack(binding);
                    scratch->allocations[assignment.allocation].physical = {bindingIndex, physicalGeneration};
                }
            }
            else
            {
                placedRequests.Clear();
                placedObjects.Clear();
            }
        }

        u32 nextVirtualPhysical = 0;
        for (const u32 allocation : physicalAssignmentOrder)
        {
            LogicalAllocationRecord& logical = scratch->allocations[allocation];
            if (logical.imported)
            {
                if (!scratch->hasNativeResourceBindings)
                {
                    logical.physical = {nextVirtualPhysical++, physicalGeneration};
                    continue;
                }
                if (logical.importedResource >= allocator.retainedImports.Size())
                    return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "imported physical assignment references an invalid session import");
                const u32 retainedIndex = scratch->retainedImports.Size();
                scratch->retainedImports.PushBack(allocator.retainedImports[logical.importedResource]);
                const RetainedImportRecord& retained = scratch->retainedImports[retainedIndex];
                PhysicalBindingRecord binding;
                binding.kind = PhysicalBindingKind::RetainedImport;
                binding.texture = retained.texture.GetRef();
                binding.buffer = retained.buffer.GetRef();
                binding.retainedImport = retainedIndex;
                binding.explicitState = logical.desc.kind == FrameResourceKind::Texture ? !logical.desc.texture.active.keepInitialState : !logical.desc.buffer.active.keepInitialState;
                const u32 bindingIndex = scratch->physicalBindings.Size();
                scratch->physicalBindings.PushBack(std::move(binding));
                logical.physical = {bindingIndex, physicalGeneration};
                continue;
            }
            if (logical.physical.IsValid())
                continue;

            u32 firstScopeIndex = InvalidRenderFlowResourceIndex;
            if (!commandScopeIndices.Find(logical.firstUseCommandScope.value, firstScopeIndex) || firstScopeIndex >= scratch->commandScopes.Size())
                return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::BackendContractViolation, "logical allocation first-use scope is absent from the compiled command-scope stream");

            u32 reusableLane = InvalidRenderFlowResourceIndex;
            u64 bestBufferCapacity = ~u64{0};
            if (!logical.exported)
            {
                for (u32 laneIndex = 0; laneIndex < wholeFrameLanes.Size(); ++laneIndex)
                {
                    const WholeFrameLane& lane = wholeFrameLanes[laneIndex];
                    if (!PositionBefore(lane.lastUsePosition, logical.firstUsePosition) || !DedicatedResourceCanSatisfy(lane.physicalDesc, logical.desc))
                        continue;
                    u32 lastScopeIndex = InvalidRenderFlowResourceIndex;
                    if (!commandScopeIndices.Find(lane.lastUseCommandScope.value, lastScopeIndex) || lastScopeIndex >= scratch->commandScopes.Size())
                        return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::BackendContractViolation,
                                           "dedicated-resource reuse predecessor scope is absent from the compiled command-scope stream");
                    if (!QueueOrderProved(lane.lastUseCommandScope, scratch->commandScopes[lastScopeIndex].queue, logical.firstUseCommandScope, scratch->commandScopes[firstScopeIndex].queue,
                                          scratch->commandScopes, scratch->queueDependencies))
                        continue;
                    if (logical.desc.kind == FrameResourceKind::Texture)
                    {
                        reusableLane = laneIndex;
                        break;
                    }
                    const u64 capacity = lane.physicalDesc.buffer.active.size;
                    if (capacity < bestBufferCapacity)
                    {
                        reusableLane = laneIndex;
                        bestBufferCapacity = capacity;
                    }
                }
            }

            if (reusableLane != InvalidRenderFlowResourceIndex)
            {
                WholeFrameLane& lane = wholeFrameLanes[reusableLane];
                logical.physical = {lane.physicalIndex, physicalGeneration};
                lane.lastUsePosition = logical.lastUsePosition;
                lane.lastUseCommandScope = logical.lastUseCommandScope;
                continue;
            }

            FrameResourceDesc physicalDesc = DedicatedResourcePhysicalDesc(logical.desc);
            u32 physicalIndex = InvalidRenderFlowResourceIndex;
            if (!scratch->hasNativeResourceBindings)
            {
                physicalIndex = nextVirtualPhysical++;
                logical.physical = {physicalIndex, physicalGeneration};
            }
            else
            {
                DedicatedResourceAssignment assignment;
                if (!allocator.dedicatedPool.Acquire(logical.desc, allocator.frameSerial, assignment, failure))
                {
                    const RenderFlowResourceFailureCode code = failure != nullptr ? failure->code : RenderFlowResourceFailureCode::DeviceLostOrBackendFailure;
                    const char* const message = failure != nullptr && failure->message != nullptr ? failure->message : "dedicated-resource physical assignment failed";
                    return FailResolve(allocator, scratch, failure, code, message);
                }
                const u32 bindingIndex = scratch->physicalBindings.Size();
                PhysicalBindingRecord binding;
                binding.kind = PhysicalBindingKind::DedicatedPool;
                binding.poolEntry = assignment.entry;
                binding.texture = assignment.texture;
                binding.buffer = assignment.buffer;
                binding.explicitState = logical.desc.kind == FrameResourceKind::Texture ? !logical.desc.texture.active.keepInitialState : !logical.desc.buffer.active.keepInitialState;
                scratch->physicalBindings.PushBack(binding);
                logical.physical = {bindingIndex, physicalGeneration};
                physicalIndex = bindingIndex;
                physicalDesc = assignment.physicalDesc;
            }
            if (!logical.exported)
            {
                WholeFrameLane lane;
                lane.physicalDesc = physicalDesc;
                lane.lastUsePosition = logical.lastUsePosition;
                lane.lastUseCommandScope = logical.lastUseCommandScope;
                lane.physicalIndex = physicalIndex;
                wholeFrameLanes.PushBack(lane);
            }
        }

        for (const PlacedRangeAssignment& range : placedPlan.assignments)
        {
            if (range.predecessorCount == 0)
                continue;
            if (range.allocation >= scratch->allocations.Size() || range.predecessorOffset > placedPlan.predecessors.Size() || range.predecessorCount > placedPlan.predecessors.Size() - range.predecessorOffset)
                return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::BackendContractViolation, "placed alias provenance is outside the compiled range plan");
            const LogicalAllocationRecord& destination = scratch->allocations[range.allocation];
            const ActiveUseRecord* firstUse = nullptr;
            for (const ActiveUseRecord& use : uses)
                if (use.allocation == range.allocation && use.beginPosition == destination.firstUsePosition)
                {
                    if (firstUse != nullptr)
                        return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::BackendContractViolation, "placed destination has duplicate first-use records");
                    firstUse = &use;
                }
            if (firstUse == nullptr || firstUse->packet >= scratch->packets.Size())
                return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::BackendContractViolation, "placed destination has no compiled first-use packet");
            CompiledPacket& packet = scratch->packets[firstUse->packet];
            CompiledStep* beginStep = nullptr;
            for (CompiledStep& step : packet.steps)
                if (step.use == firstUse->id && (step.kind == CompiledResourceStepKind::TextureUseBegin || step.kind == CompiledResourceStepKind::BufferUseBegin))
                {
                    beginStep = &step;
                    break;
                }
            if (beginStep == nullptr || beginStep->aliasPredecessorCount != 0 || !destination.physical.IsValid() || destination.physical.index >= scratch->physicalBindings.Size() ||
                scratch->physicalBindings[destination.physical.index].kind != PhysicalBindingKind::PlacedPool)
                return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::BackendContractViolation, "placed destination cannot receive its compiled alias activation");
            const PhysicalBindingRecord& destinationBinding = scratch->physicalBindings[destination.physical.index];
            const bool destinationIsTexture = destination.desc.kind == FrameResourceKind::Texture;
            if (destinationBinding.texture.IsValid() != destinationIsTexture || destinationBinding.buffer.IsValid() == destinationIsTexture)
                return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::BackendContractViolation, "placed destination kind disagrees with its physical resource");
            if (compiledResourceActionCount >= allocator.config.maximumCompiledResourceActions)
                return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::CapacityExceeded, "placed alias activations exceed the allocator action capacity");
            beginStep->aliasPredecessorOffset = packet.aliasPredecessors.Size();
            beginStep->aliasPredecessorCount = range.predecessorCount;
            for (u32 predecessorIndex = 0; predecessorIndex < range.predecessorCount; ++predecessorIndex)
            {
                const PlacedPredecessorFragment& fragment = placedPlan.predecessors[range.predecessorOffset + predecessorIndex];
                if (fragment.allocation >= scratch->allocations.Size())
                    return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::BackendContractViolation, "placed alias predecessor is outside the logical allocation table");
                const PhysicalResourceId predecessorPhysical = scratch->allocations[fragment.allocation].physical;
                if (!predecessorPhysical.IsValid() || predecessorPhysical.index >= scratch->physicalBindings.Size())
                    return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::BackendContractViolation, "placed alias predecessor has no physical assignment");
                const PhysicalBindingRecord& predecessorBinding = scratch->physicalBindings[predecessorPhysical.index];
                const LogicalAllocationRecord& predecessor = scratch->allocations[fragment.allocation];
                if (predecessorBinding.kind != PhysicalBindingKind::PlacedPool || predecessor.desc.kind != destination.desc.kind || predecessorBinding.texture.IsValid() != destinationIsTexture ||
                    predecessorBinding.buffer.IsValid() == destinationIsTexture)
                    return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::BackendContractViolation, "placed alias predecessor kind disagrees with its destination or physical resource");
                const ActiveUseRecord* terminalUse = nullptr;
                for (const ActiveUseRecord& use : uses)
                    if (use.allocation == fragment.allocation && use.endPosition == predecessor.lastUsePosition)
                    {
                        if (terminalUse != nullptr)
                            return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::BackendContractViolation, "placed predecessor has duplicate terminal-use records");
                        terminalUse = &use;
                    }
                if (terminalUse == nullptr || terminalUse->packet >= scratch->packets.Size() || terminalUse->endStep >= scratch->packets[terminalUse->packet].steps.Size())
                    return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::BackendContractViolation, "placed predecessor has no compiled terminal-use step");
                CompiledStep& terminalStep = scratch->packets[terminalUse->packet].steps[terminalUse->endStep];
                if (terminalStep.kind != CompiledResourceStepKind::UseEnd || terminalStep.use != terminalUse->id)
                    return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::BackendContractViolation, "placed predecessor terminal-use step is inconsistent");
                if (!terminalStep.finalizeForAlias)
                {
                    if (compiledResourceActionCount >= allocator.config.maximumCompiledResourceActions)
                        return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::CapacityExceeded, "placed predecessor finalizations exceed the allocator action capacity");
                    terminalStep.finalizeForAlias = true;
                    terminalStep.physical = predecessorPhysical;
                    ++compiledResourceActionCount;
                }
                packet.aliasPredecessors.PushBack(predecessorBinding.texture.IsValid() ? rhi::ResourceRef(predecessorBinding.texture) : rhi::ResourceRef(predecessorBinding.buffer));
            }
            ++compiledResourceActionCount;
        }

        for (PendingExportRecord& exportRecord : scratch->pendingExports)
        {
            if (exportRecord.allocation >= scratch->allocations.Size())
                return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "terminal export physical assignment lost its logical allocation");
            exportRecord.physical = scratch->allocations[exportRecord.allocation].physical;
            if (!exportRecord.physical.IsValid() || exportRecord.physical.index >= scratch->physicalBindings.Size())
                return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "terminal export has no physical assignment");
            const PhysicalBindingRecord& binding = scratch->physicalBindings[exportRecord.physical.index];
            rhi::Failure rhiFailure;
            const bool described = exportRecord.kind == FrameResourceKind::Texture
                                       ? binding.texture.IsValid() && !binding.buffer.IsValid() && rhi::GetTextureDesc(binding.texture, exportRecord.textureDesc, &rhiFailure)
                                       : binding.buffer.IsValid() && !binding.texture.IsValid() && rhi::GetBufferDesc(binding.buffer, exportRecord.bufferDesc, &rhiFailure);
            if (!described)
                return FailResolve(allocator, scratch, failure, MapRhiFailure(rhiFailure, RhiFailureContext::BackendContract),
                                   rhiFailure.message[0] != '\0' ? rhiFailure.message : "terminal export authoritative descriptor query failed");
            const rhi::ResourceState nativeTerminal = exportRecord.kind == FrameResourceKind::Texture ? exportRecord.textureDesc.initialState : exportRecord.bufferDesc.initialState;
            const bool automaticReset = exportRecord.kind == FrameResourceKind::Texture ? exportRecord.textureDesc.keepInitialState : exportRecord.bufferDesc.keepInitialState;
            if (automaticReset && exportRecord.terminalState != nativeTerminal)
                return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::UnsupportedCapability, "terminal export state would be overwritten by native command-list close");
            if (!automaticReset && exportRecord.kind == FrameResourceKind::Buffer && exportRecord.terminalState != rhi::ResourceState::Common)
                return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::UnsupportedCapability, "explicit buffer exports require Common terminal state");
        }
        for (CompiledPacket& packet : scratch->packets)
        {
            for (CompiledStep& step : packet.steps)
            {
                if (step.kind == CompiledResourceStepKind::UseEnd)
                    continue;
                if (step.logicalAllocation >= scratch->allocations.Size())
                    return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "compiled resource use has an invalid logical allocation", packet.node, {}, step.use);
                step.physical = scratch->allocations[step.logicalAllocation].physical;
                if (!step.physical.IsValid())
                    return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "compiled resource use has no physical assignment", packet.node, {}, step.use);
            }
            for (CompiledResourceAction& action : packet.actions)
            {
                if (action.logicalAllocation >= scratch->allocations.Size())
                    return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "compiled resource action has an invalid logical allocation", packet.node);
                action.physical = scratch->allocations[action.logicalAllocation].physical;
                if (!action.physical.IsValid())
                    return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "compiled resource action has no physical assignment", packet.node);
            }
        }

        if (scratch->hasNativeResourceBindings)
        {
            containers::HashMap<u64, u32> nativeIndices{memory::pools::Rendering::GetInstance()};
            containers::DynamicArray<PhysicalScopeState> history{memory::pools::Rendering::GetInstance()};
            history.Resize(scratch->physicalBindings.Size());
            if (history.Size() != scratch->physicalBindings.Size())
                return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::CapacityExceeded, "physical-state table allocation failed");
            for (u32 index = 0; index < scratch->physicalBindings.Size(); ++index)
            {
                const PhysicalBindingRecord& binding = scratch->physicalBindings[index];
                const rhi::ResourceRef reference = binding.texture.IsValid() ? rhi::ResourceRef(binding.texture) : rhi::ResourceRef(binding.buffer);
                u32 existingIndex = InvalidRenderFlowResourceIndex;
                if (!reference.IsValid() || nativeIndices.Find(reference.value, existingIndex))
                    return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::BackendContractViolation, "physical bindings contain a missing or duplicate native identity");
                if (!nativeIndices.Insert(reference.value, index).IsSuccessful())
                    return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::CapacityExceeded, "physical identity index allocation failed");
            }
            for (const LogicalAllocationRecord& allocation : scratch->allocations)
            {
                if (!allocation.used || allocation.desc.kind != FrameResourceKind::Texture || allocation.desc.texture.active.keepInitialState)
                    continue;
                if (!allocation.physical.IsValid() || allocation.physical.index >= history.Size())
                    return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "explicit texture lost its physical history");
                PhysicalScopeState& state = history[allocation.physical.index];
                state.initial = allocation.desc.texture.active.initialState;
                state.terminal = state.initial; // Pool reuse starts from creation state after successful completion.
                if (allocation.imported)
                {
                    const RetainedImportRecord& imported = allocator.retainedImports[allocation.importedResource];
                    state.initial = imported.initialState;
                    state.terminal = imported.terminalState;
                }
            }
            for (const PendingExportRecord& exported : scratch->pendingExports)
                if (exported.kind == FrameResourceKind::Texture && !exported.textureDesc.keepInitialState)
                {
                    history[exported.physical.index].terminal = exported.terminalState;
                }
            u32 totalEntryStates = 0;
            for (u32 packetIndex = 0; packetIndex < scratch->packets.Size(); ++packetIndex)
                if (!CompileScopeEntryStates(allocator, *scratch, scratch->packets[packetIndex], packetIndex, nativeIndices, history, totalEntryStates, compiledResourceActionCount, failure))
                {
                    // Preserve the precise compile failure while rolling back unpublished native assignments.
                    static_cast<void>(FailResolve(allocator, scratch, nullptr, RenderFlowResourceFailureCode::BackendContractViolation, "scope entry-state compilation failed"));
                    return false;
                }
            for (u32 index = 0; index < history.Size(); ++index)
            {
                if (!scratch->physicalBindings[index].explicitState)
                    continue;
                const PhysicalScopeState& state = history[index];
                const bool buffer = scratch->physicalBindings[index].buffer.IsValid();
                if (state.lastPacket >= scratch->packets.Size() || (!buffer && state.terminal == rhi::ResourceState::Unknown))
                    return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::BackendContractViolation, "explicit physical resource has no terminal scope/state");
                CompiledPacket& packet = scratch->packets[state.lastPacket];
                for (const PendingExportRecord& exported : scratch->pendingExports)
                    if (exported.physical.index == index && exported.terminalCommandScope != packet.commandScope)
                        return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::BackendContractViolation, "explicit export terminal fence does not cover its final physical scope");
                if (buffer)
                    continue; // Every explicit buffer scope already closes in Common, including the final scope.
                rhi::TextureDesc desc;
                rhi::Failure rhiFailure;
                if (!rhi::GetTextureDesc(scratch->physicalBindings[index].texture, desc, &rhiFailure))
                    return FailResolve(allocator, scratch, failure, MapRhiFailure(rhiFailure, RhiFailureContext::BackendContract), "explicit terminal texture descriptor query failed");
                for (u32 cell = 0; cell < state.cells.Size(); ++cell)
                {
                    if (state.cells[cell] == state.terminal)
                        continue;
                    if (compiledResourceActionCount >= allocator.config.maximumCompiledResourceActions)
                        return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::CapacityExceeded, "scope exit transitions exceed the compiled action capacity");
                    CompiledResourceAction action;
                    action.kind = CompiledResourceActionKind::TextureTransition;
                    action.physical = {index, scratch->id.generation};
                    action.before = state.cells[cell];
                    action.after = state.terminal;
                    action.textureSubresources = {static_cast<u16>(cell % desc.mipCount), 1, static_cast<u16>(cell / desc.mipCount), 1};
                    const u32 size = packet.exitActions.Size();
                    packet.exitActions.PushBack(action);
                    if (packet.exitActions.Size() != size + 1u)
                        return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::CapacityExceeded, "scope exit transition storage allocation failed");
                    ++compiledResourceActionCount;
                }
            }
        }

        if (scratch->hasNativeResourceBindings && allocator.config.resourceDescriptors.IsValid())
        {
            scratch->resourceDescriptors = rhi::DescriptorDomain(allocator.config.resourceDescriptors);
            for (PhysicalBindingRecord& binding : scratch->physicalBindings)
            {
                // Persistent owners already publish their own descriptors.
                if (binding.kind == PhysicalBindingKind::RetainedImport)
                    continue;
                rhi::Failure descriptorFailure;
                rhi::BufferDesc buffer;
                rhi::TextureDesc texture;
                if (binding.buffer.IsValid() ? !rhi::GetBufferDesc(binding.buffer, buffer, &descriptorFailure)
                                             : !rhi::GetTextureDesc(binding.texture, texture, &descriptorFailure))
                    return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::BackendContractViolation, "render-flow descriptor source is unavailable");
                for (u32 access = 0; access < 2; ++access)
                {
                    const bool writable = access != 0;
                    const bool needed = binding.buffer.IsValid()
                                            ? HasAny(buffer.usage, writable ? rhi::BufferUsage::UnorderedAccess : rhi::BufferUsage::ShaderResource)
                                            : HasAny(texture.usage, writable ? rhi::TextureUsage::UnorderedAccess : rhi::TextureUsage::ShaderResource);
                    if (!needed)
                        continue;
                    auto& descriptor = writable ? binding.unorderedAccess : binding.shaderResource;
                    descriptor = rhi::AllocateDescriptor(allocator.config.resourceDescriptors, &descriptorFailure);
                    if (!descriptor.IsValid())
                        return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::CapacityExceeded, "render-flow descriptor allocation failed");
                    bool written;
                    if (binding.buffer.IsValid())
                    {
                        const rhi::BindingType type = HasAny(buffer.usage, rhi::BufferUsage::Structured)
                            ? (writable ? rhi::BindingType::StructuredBufferUnorderedAccess : rhi::BindingType::StructuredBufferShaderResource)
                            : HasAny(buffer.usage, rhi::BufferUsage::Raw)
                                ? (writable ? rhi::BindingType::ByteAddressBufferUnorderedAccess : rhi::BindingType::ByteAddressBufferShaderResource)
                                : (writable ? rhi::BindingType::TypedBufferUnorderedAccess : rhi::BindingType::TypedBufferShaderResource);
                        written = rhi::WriteDescriptor(allocator.config.resourceDescriptors, descriptor, binding.buffer, type,
                                                      {buffer.format, 0, buffer.size, buffer.structureStride}, &descriptorFailure);
                    }
                    else
                        written = rhi::WriteDescriptor(allocator.config.resourceDescriptors, descriptor, binding.texture,
                            writable ? rhi::BindingType::TextureUnorderedAccess : rhi::BindingType::TextureShaderResource, {}, &descriptorFailure);
                    if (!written)
                        return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::BackendContractViolation, "render-flow descriptor write failed");
                }
            }
        }
        allocator.executionGeneration = scratch->id.generation;
        allocator.publishedGeneration = scratch;
        allocator.state = RenderFlowResourceSessionState::Ready;
        allocator.stats.state = allocator.state;
        ++allocator.stats.resolvedFrames;
        allocator.stats.compiledOperations += compiledOperationCount;
        allocator.stats.compiledResourceActions += compiledResourceActionCount;
        allocator.stats.logicalAllocations = scratch->allocations.Size();
        allocator.stats.compiledPackets = scratch->packets.Size();
        allocator.stats.retainedImports = scratch->retainedImports.Size();
        // Physical identities are fixed into every executable step and action;
        // the planner-only logical allocation table must not survive publication.
        scratch->allocations.Clear();
        // The immutable execution generation now owns every datum required by
        // Consume. Candidate tapes are no longer replayed and must not overlap
        // the execution generation's lifetime.
        allocator.writerBatches.Clear();
        allocator.stats.rejectedOperations += allocator.pendingRejectedOperations.Exchange(0);
        allocator.writerReservations.Clear();
        allocator.queueRequestGroups.Clear();
        allocator.retainedImports.Clear();
        return true;
    }
} // namespace vanguard::rendering::detail
