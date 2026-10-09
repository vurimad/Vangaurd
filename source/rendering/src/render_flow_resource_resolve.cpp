#include <vanguard/rendering/render_flow_resource_internal.hpp>
#include <vanguard/profiling/profiling.hpp>
#include <vanguard/system/time.hpp>

#include <algorithm>
#include <cmath>
#include <new>
#include <optional>
#if VG_ENABLE_PROFILING && defined(_MSC_VER)
#include <intrin.h>
#endif

#define VG_COMPILE_OPERATIONS_PROBE 0

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

        template <typename Attempt>
        [[nodiscard]] bool RunCanonicalDescriptorPublicationWithGlobalPressure(RenderFlowResourceAllocator::Impl& allocator, Attempt&& attempt,
                                                                               RenderFlowResourceFailure* const failure) noexcept
        {
            bool relievedLocalLimit = false;
            bool relievedSharedDomain = false;
            bool recordPublicationStats = true;
            for (;;)
            {
                CanonicalDescriptorPressure pressure;
                if (attempt(&pressure, recordPublicationStats))
                    return true;
                if (!pressure.IsRequired())
                    return false;
                recordPublicationStats = false;
                if (pressure.kind == CanonicalDescriptorPressureKind::LocalLimit)
                {
                    if (relievedLocalLimit)
                        return Fail(failure, RenderFlowResourceFailureCode::CapacityExceeded, RenderFlowResourceSessionState::Resolving,
                                    "canonical descriptor cache pressure repeated after global relief");
                    relievedLocalLimit = true;
                }
                else if (pressure.kind == CanonicalDescriptorPressureKind::SharedDomain)
                {
                    // SharedDomain means this call already consumed one
                    // transactional RHI publication attempt. Permit exactly one
                    // global relief plus complete retry.
                    if (relievedSharedDomain)
                        return Fail(failure, RenderFlowResourceFailureCode::CapacityExceeded, RenderFlowResourceSessionState::Resolving,
                                    "canonical descriptor shared-domain pressure persisted after the bounded retry");
                    relievedSharedDomain = true;
                }
                else
                    return false;
                if (!RelieveCanonicalDescriptorPressure(allocator, pressure, failure))
                    return false;
            }
        }

        void CachePhysicalStateDescriptor(PhysicalBindingRecord& binding, const FrameResourceDesc& desc) noexcept
        {
            binding.stateDescriptorKind = desc.kind;
            if (desc.kind == FrameResourceKind::Texture)
            {
                binding.initialState = desc.texture.active.initialState;
                binding.textureUsage = desc.texture.active.usage;
                binding.textureMipCount = desc.texture.active.mipCount;
                binding.textureArraySize = desc.texture.active.arraySize;
                binding.explicitState = !desc.texture.active.keepInitialState;
            }
            else if (desc.kind == FrameResourceKind::Buffer)
            {
                binding.initialState = desc.buffer.active.initialState;
                binding.bufferUsage = desc.buffer.active.usage;
                binding.bufferMemoryType = desc.buffer.active.memoryType;
                binding.explicitState = !desc.buffer.active.keepInitialState;
            }
        }

        [[nodiscard]] bool HasRequiredCanonicalDescriptors(const PhysicalBindingRecord& binding, const FrameResourceDesc& desc) noexcept
        {
            if (desc.kind == FrameResourceKind::Texture)
            {
                const rhi::TextureUsage usage = desc.texture.active.usage;
                return (!HasAny(usage, rhi::TextureUsage::ShaderResource) || binding.shaderResource.IsValid()) &&
                       (!HasAny(usage, rhi::TextureUsage::UnorderedAccess) || binding.unorderedAccess.IsValid());
            }
            if (desc.kind == FrameResourceKind::Buffer)
            {
                const rhi::BufferUsage usage = desc.buffer.active.usage;
                return (!HasAny(usage, rhi::BufferUsage::ShaderResource) || binding.shaderResource.IsValid()) &&
                       (!HasAny(usage, rhi::BufferUsage::UnorderedAccess) || binding.unorderedAccess.IsValid());
            }
            return false;
        }

        [[nodiscard]] bool EnsureRetainedImportCanonicalDescriptors(RenderFlowResourceAllocator::Impl& allocator,
                                                                    PhysicalBindingRecord& binding,
                                                                    const FrameResourceDesc& desc,
                                                                    CanonicalDescriptorPressure* const pressure,
                                                                    RenderFlowResourceFailure* const failure) noexcept
        {
            if (pressure != nullptr)
                *pressure = {};
            if (!allocator.resourceDescriptors.IsValid())
                return true;
            const bool buffer = binding.buffer.IsValid();
            if (buffer == binding.texture.IsValid())
                return Fail(failure, RenderFlowResourceFailureCode::BackendContractViolation, RenderFlowResourceSessionState::Resolving,
                            "retained import canonical descriptor source kind is invalid");

            const bool needShaderResource = buffer ? HasAny(desc.buffer.active.usage, rhi::BufferUsage::ShaderResource)
                                                   : HasAny(desc.texture.active.usage, rhi::TextureUsage::ShaderResource);
            const bool needUnorderedAccess = buffer ? HasAny(desc.buffer.active.usage, rhi::BufferUsage::UnorderedAccess)
                                                    : HasAny(desc.texture.active.usage, rhi::TextureUsage::UnorderedAccess);
            const u32 missing = static_cast<u32>(needShaderResource && !binding.shaderResource.IsValid()) +
                                static_cast<u32>(needUnorderedAccess && !binding.unorderedAccess.IsValid());
            if (missing == 0)
                return true;
            if (!allocator.descriptorLedger.TryReserve(missing))
            {
                if (pressure != nullptr)
                    *pressure = {CanonicalDescriptorPressureKind::LocalLimit, missing};
                return false;
            }

            rhi::DescriptorHandle createdShaderResource;
            rhi::DescriptorHandle createdUnorderedAccess;
            rhi::Failure rhiFailure;
            bool allocationCapacityFailure = false;
            const auto create = [&](const bool writable, rhi::DescriptorHandle& created) noexcept
            {
                created = rhi::AllocateDescriptor(allocator.resourceDescriptors.GetRef(), &rhiFailure);
                if (!created.IsValid())
                {
                    allocationCapacityFailure = rhiFailure.code == rhi::FailureCode::CapacityExceeded;
                    return false;
                }
                if (buffer)
                {
                    const rhi::BufferUsage usage = desc.buffer.active.usage;
                    const rhi::BindingType type = HasAny(usage, rhi::BufferUsage::Structured)
                                                      ? (writable ? rhi::BindingType::StructuredBufferUnorderedAccess : rhi::BindingType::StructuredBufferShaderResource)
                                                  : HasAny(usage, rhi::BufferUsage::Raw)
                                                      ? (writable ? rhi::BindingType::ByteAddressBufferUnorderedAccess : rhi::BindingType::ByteAddressBufferShaderResource)
                                                      : (writable ? rhi::BindingType::TypedBufferUnorderedAccess : rhi::BindingType::TypedBufferShaderResource);
                    return rhi::WriteDescriptor(allocator.resourceDescriptors.GetRef(), created, binding.buffer, type, {}, &rhiFailure);
                }
                return rhi::WriteDescriptor(allocator.resourceDescriptors.GetRef(), created, binding.texture,
                                            writable ? rhi::BindingType::TextureUnorderedAccess : rhi::BindingType::TextureShaderResource,
                                            {}, &rhiFailure);
            };

            bool published = true;
            if (needShaderResource && !binding.shaderResource.IsValid())
                published = create(false, createdShaderResource);
            if (published && needUnorderedAccess && !binding.unorderedAccess.IsValid())
                published = create(true, createdUnorderedAccess);
            if (published)
            {
                if (createdShaderResource.IsValid())
                    binding.shaderResource = createdShaderResource;
                if (createdUnorderedAccess.IsValid())
                    binding.unorderedAccess = createdUnorderedAccess;
                return true;
            }

            if (createdShaderResource.IsValid())
                static_cast<void>(rhi::RetireDescriptor(allocator.resourceDescriptors.GetRef(), createdShaderResource, {}));
            if (createdUnorderedAccess.IsValid())
                static_cast<void>(rhi::RetireDescriptor(allocator.resourceDescriptors.GetRef(), createdUnorderedAccess, {}));
            allocator.descriptorLedger.Release(missing);
            if (allocationCapacityFailure && pressure != nullptr)
            {
                *pressure = {CanonicalDescriptorPressureKind::SharedDomain, missing};
                return false;
            }
            return Fail(failure, allocationCapacityFailure ? RenderFlowResourceFailureCode::CapacityExceeded
                                                           : MapRhiFailure(rhiFailure, RhiFailureContext::BackendContract),
                        RenderFlowResourceSessionState::Resolving,
                        rhiFailure.message[0] != '\0' ? rhiFailure.message : "retained import canonical descriptor publication failed");
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

        [[nodiscard]] bool SameTextureTransition(const CompiledResourceAction& left, const CompiledResourceAction& right, const bool unknownBeforeWildcard = false) noexcept
        {
            return left.kind == CompiledResourceActionKind::TextureTransition && right.kind == CompiledResourceActionKind::TextureTransition &&
                   left.logicalAllocation == right.logicalAllocation && left.physical.index == right.physical.index && left.physical.generation == right.physical.generation &&
                   (left.before == right.before || (unknownBeforeWildcard && (left.before == rhi::ResourceState::Unknown || right.before == rhi::ResourceState::Unknown))) && left.after == right.after;
        }

        [[nodiscard]] bool MergeTextureTransitionRange(CompiledResourceAction& left, const CompiledResourceAction& right, const bool unknownBeforeWildcard = false) noexcept
        {
            if (!SameTextureTransition(left, right, unknownBeforeWildcard))
                return false;
            rhi::SubresourceRange& leftRange = left.textureSubresources;
            const rhi::SubresourceRange& rightRange = right.textureSubresources;
            // 0xffff is the public "remaining subresources" sentinel. Compiled
            // transitions use explicit finite ranges; never synthesize the
            // sentinel while widening one.
            if (leftRange.mipCount == 0 || leftRange.sliceCount == 0 || rightRange.mipCount == 0 || rightRange.sliceCount == 0 || leftRange.mipCount == 0xffffu ||
                leftRange.sliceCount == 0xffffu || rightRange.mipCount == 0xffffu || rightRange.sliceCount == 0xffffu)
                return false;
            const u32 leftMipEnd = static_cast<u32>(leftRange.firstMip) + leftRange.mipCount;
            const u32 leftSliceEnd = static_cast<u32>(leftRange.firstSlice) + leftRange.sliceCount;
            if (leftRange.firstSlice == rightRange.firstSlice && leftRange.sliceCount == rightRange.sliceCount && leftMipEnd == rightRange.firstMip)
            {
                const u32 mergedCount = static_cast<u32>(leftRange.mipCount) + rightRange.mipCount;
                if (mergedCount >= 0xffffu)
                    return false;
                if (unknownBeforeWildcard && left.before != right.before)
                    left.before = rhi::ResourceState::Unknown;
                leftRange.mipCount = static_cast<u16>(mergedCount);
                return true;
            }
            if (leftRange.firstMip == rightRange.firstMip && leftRange.mipCount == rightRange.mipCount && leftSliceEnd == rightRange.firstSlice)
            {
                const u32 mergedCount = static_cast<u32>(leftRange.sliceCount) + rightRange.sliceCount;
                if (mergedCount >= 0xffffu)
                    return false;
                if (unknownBeforeWildcard && left.before != right.before)
                    left.before = rhi::ResourceState::Unknown;
                leftRange.sliceCount = static_cast<u16>(mergedCount);
                return true;
            }
            return false;
        }

        [[nodiscard]] bool AppendOrMergeTextureTransition(containers::DynamicArray<CompiledResourceAction>& actions, const u32 blockStart, const CompiledResourceAction& action,
                                                          const bool unknownBeforeWildcard) noexcept
        {
            if (blockStart > actions.Size() || action.kind != CompiledResourceActionKind::TextureTransition)
                return false;
            if (action.before != rhi::ResourceState::Unknown && action.before == action.after)
                return true;
            if (actions.Size() > blockStart && MergeTextureTransitionRange(actions[actions.Size() - 1u], action, unknownBeforeWildcard))
            {
                while (actions.Size() >= blockStart + 2u && MergeTextureTransitionRange(actions[actions.Size() - 2u], actions[actions.Size() - 1u], unknownBeforeWildcard))
                    actions.Resize(actions.Size() - 1u);
                return true;
            }
            const u32 size = actions.Size();
            actions.PushBack(action);
            return actions.Size() == size + 1u;
        }

        [[nodiscard]] bool FinalizeTextureTransitionBlock(containers::DynamicArray<CompiledResourceAction>& actions, const u32 blockStart,
                                                          ResolveScratchHashMap<TextureTransitionRunKey, u32>& runs) noexcept
        {
            if (blockStart > actions.Size())
                return false;
            if (actions.Size() - blockStart <= 1u)
                return true;
            for (u32 index = blockStart; index < actions.Size(); ++index)
                if (actions[index].kind != CompiledResourceActionKind::TextureTransition)
                    return false;

            // Emission is slice-major/mip-minor, so horizontal mip runs are
            // already folded by AppendOrMergeTextureTransition. Track the most
            // recent output rectangle for each exact run signature and extend it
            // vertically when the next slice is adjacent. This preserves the
            // #9 A,B/A,B rectangle proof without an O(C log C) global sort.
            runs.Reset();
            runs.Reserve(actions.Size() - blockStart);
            const u32 end = actions.Size();
            u32 write = blockStart;
            for (u32 read = blockStart; read < end; ++read)
            {
                const CompiledResourceAction action = actions[read];
                const TextureTransitionRunKey key{action.logicalAllocation, action.physical.index, action.physical.generation, Bits(action.before), Bits(action.after),
                                                  action.textureSubresources.firstMip, action.textureSubresources.mipCount};
                u32 prior = InvalidRenderFlowResourceIndex;
                if (runs.Find(key, prior) && prior >= blockStart && prior < write && MergeTextureTransitionRange(actions[prior], action, false))
                {
                    continue;
                }
                if (write != read)
                    actions[write] = action;
                if (!runs.Set(key, write))
                    return false;
                ++write;
            }
            actions.Resize(write);
            return true;
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
            return PhysicalTextureDescEqual(left, right);
        }

        [[nodiscard]] bool BufferCreationEqual(const rhi::BufferDesc& left, const rhi::BufferDesc& right) noexcept
        {
            return PhysicalBufferDescEqual(left, right);
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

        [[nodiscard]] bool PersistentPlanAllocationMatches(const PersistentDedicatedPlanAllocation& planned, const LogicalAllocationRecord& current) noexcept
        {
            return ResourceDescEqual(planned.desc, current.desc) && planned.firstUsePosition == current.firstUsePosition &&
                   planned.lastUsePosition == current.lastUsePosition && planned.firstUseCommandScopeIndex == current.firstUseCommandScopeIndex &&
                   planned.lastUseCommandScopeIndex == current.lastUseCommandScopeIndex && planned.queueMask == current.queueMask &&
                   planned.used == current.used && planned.exported == current.exported && planned.imported == current.imported &&
                   planned.requiresCanonicalDescriptors == current.requiresCanonicalDescriptors;
        }

        [[nodiscard]] bool PersistentDedicatedPlanMatches(const PersistentDedicatedPhysicalPlan& plan, const ExecutionGenerationRef::Impl& generation,
                                                          const ResolveScratch& resolveScratch) noexcept
        {
            if (!plan.valid || !generation.hasNativeResourceBindings || plan.allocations.Size() != generation.allocations.Size() ||
                plan.scopeQueues.Size() != generation.commandScopes.Size() || plan.crossQueueProducerScopes.Size() != resolveScratch.crossQueueProducerScopes.Size() ||
                plan.lanes.Size() != plan.laneRuntime.Size())
                return false;
            for (u32 index = 0; index < generation.commandScopes.Size(); ++index)
                if (plan.scopeQueues[index] != generation.commandScopes[index].queue)
                    return false;
            for (u32 index = 0; index < resolveScratch.crossQueueProducerScopes.Size(); ++index)
                if (plan.crossQueueProducerScopes[index] != resolveScratch.crossQueueProducerScopes[index])
                    return false;
            for (u32 allocation = 0; allocation < generation.allocations.Size(); ++allocation)
            {
                const LogicalAllocationRecord& logical = generation.allocations[allocation];
                const PersistentDedicatedPlanAllocation& planned = plan.allocations[allocation];
                if (!PersistentPlanAllocationMatches(planned, logical))
                    return false;
                if (!logical.used || logical.imported || logical.exported)
                {
                    if (planned.lane != InvalidRenderFlowResourceIndex)
                        return false;
                    continue;
                }
                if (planned.lane >= plan.lanes.Size())
                    return false;
                const DedicatedResourceRequest request = BuildDedicatedResourceRequest(logical.desc);
                const PersistentDedicatedPlanLane& lane = plan.lanes[planned.lane];
                if (!DedicatedResourceSameCompatibilityClass(lane.compatibility, request.compatibility) || request.capacity > lane.requiredCapacity)
                    return false;
            }
            return true;
        }

        [[nodiscard]] constexpr u64 StructuralHashMix(const u64 hash, const u64 value) noexcept
        {
            return (hash ^ (value + 0x9e3779b97f4a7c15ull + (hash << 6u) + (hash >> 2u))) * 0x100000001b3ull;
        }

        [[nodiscard]] u64 ComputeStructuralSignature(const RenderFlowResourceAllocator::Impl& allocator) noexcept
        {
            u64 hash = 0xcbf29ce484222325ull;
            hash = StructuralHashMix(hash, allocator.activePolicy.enablePlacedResources ? 1u : 0u);
            hash = StructuralHashMix(hash, allocator.activePolicy.processEviction ? 1u : 0u);
            hash = StructuralHashMix(hash, allocator.sealedWriters.Size());
            hash = StructuralHashMix(hash, allocator.sealedQueueGroups.Size());
            for (const SealedWriterRecord& sealed : allocator.sealedWriters)
            {
                if (sealed.groupIndex >= allocator.writerReservations.Size())
                    return 0;
                const CandidateWriterBatch& batch = allocator.writerReservations[sealed.groupIndex].batch;
                hash = StructuralHashMix(hash, sealed.groupIndex);
                hash = StructuralHashMix(hash, batch.node.value);
                hash = StructuralHashMix(hash, batch.flowGroup.value);
                hash = StructuralHashMix(hash, batch.commandScope.value);
                hash = StructuralHashMix(hash, batch.resourceCount);
                hash = StructuralHashMix(hash, batch.textureViews.Size());
                hash = StructuralHashMix(hash, batch.bufferViews.Size());
                hash = StructuralHashMix(hash, batch.uses.Size());
                hash = StructuralHashMix(hash, batch.useEndOrder.Size());
                hash = StructuralHashMix(hash, batch.operations.Size());
                hash = StructuralHashMix(hash, batch.declarationCount);
                hash = StructuralHashMix(hash, batch.semanticSequenceCount);
                hash = StructuralHashMix(hash, batch.stepCount);
                hash = StructuralHashMix(hash, batch.explicitScopeOpenCount);
                hash = StructuralHashMix(hash, batch.decisionCount);
                hash = StructuralHashMix(hash, batch.exportCount);
            }
            for (const u32 groupIndex : allocator.sealedQueueGroups)
            {
                if (groupIndex >= allocator.queueRequestGroups.Size())
                    return 0;
                const QueueRequestGroup& group = allocator.queueRequestGroups[groupIndex];
                hash = StructuralHashMix(hash, groupIndex);
                hash = StructuralHashMix(hash, group.count);
                for (u32 requestIndex = 0; requestIndex < group.count && requestIndex < 2u; ++requestIndex)
                {
                    const QueueRequestRecord& request = group.requests[requestIndex];
                    hash = StructuralHashMix(hash, request.flowGroup.value);
                    hash = StructuralHashMix(hash, static_cast<u64>(request.queue));
                    hash = StructuralHashMix(hash, static_cast<u64>(request.sync));
                    hash = StructuralHashMix(hash, static_cast<u64>(request.kind));
                }
            }
            return hash != 0 ? hash : 1u;
        }

        [[nodiscard]] bool TextureUseEqual(const TextureUseDesc& left, const TextureUseDesc& right) noexcept
        {
            return left.requiredState == right.requiredState && SubresourcesEqual(left.subresources, right.subresources) && left.access == right.access &&
                   left.content == right.content && TextureClearValueEqual(left.clearValue, right.clearValue);
        }

        [[nodiscard]] bool BufferUseEqual(const BufferUseDesc& left, const BufferUseDesc& right) noexcept
        {
            return left.requiredState == right.requiredState && left.access == right.access && left.content == right.content &&
                   BufferClearValueEqual(left.clearValue, right.clearValue);
        }

        [[nodiscard]] bool CandidateUseEqual(const CandidateUse& left, const CandidateUse& right) noexcept
        {
            if (left.kind != right.kind || left.resource != right.resource || left.view != right.view || left.beginSequence != right.beginSequence ||
                left.endSequence != right.endSequence)
                return false;
            return left.kind == CandidateUseKind::Texture ? TextureUseEqual(left.payload.texture, right.payload.texture)
                                                         : BufferUseEqual(left.payload.buffer, right.payload.buffer);
        }

        [[nodiscard]] bool ExportDescEqual(const TerminalResourceExportDesc& left, const TerminalResourceExportDesc& right) noexcept
        {
            return left.terminalState == right.terminalState && left.terminalQueue == right.terminalQueue && left.readiness == right.readiness;
        }

        [[nodiscard]] StructuralImportContract MakeStructuralImportContract(const RetainedImportRecord& imported) noexcept
        {
            StructuralImportContract contract;
            contract.desc = imported.desc;
            contract.initialState = imported.initialState;
            contract.terminalState = imported.terminalState;
            contract.initialQueue = imported.initialQueue;
            contract.terminalQueue = imported.terminalQueue;
            contract.incomingWaitQueue = imported.incomingWait.queue;
            contract.readiness = imported.readiness;
            contract.presentation = imported.presentationAcquisition.IsValid();
            return contract;
        }

        [[nodiscard]] bool StructuralImportContractEqual(const StructuralImportContract& left, const StructuralImportContract& right) noexcept
        {
            return ResourceDescEqual(left.desc, right.desc) && left.initialState == right.initialState && left.terminalState == right.terminalState &&
                   left.initialQueue == right.initialQueue && left.terminalQueue == right.terminalQueue && left.readiness == right.readiness &&
                   (left.readiness != ImportReadinessKind::ExplicitFenceWait || left.incomingWaitQueue == right.incomingWaitQueue) &&
                   left.presentation == right.presentation;
        }

        [[nodiscard]] bool QueueRequestEqual(const QueueRequestRecord& left, const QueueRequestRecord& right) noexcept
        {
            return left.flowGroup == right.flowGroup && left.queue == right.queue && left.sync == right.sync && left.kind == right.kind;
        }

        void ResetStructuralNormalization(ResolveScratch& scratch) noexcept
        {
            scratch.structuralScopeOrdinals.Reset();
            scratch.structuralScopeClosed.Clear();
            scratch.structuralImportIndices.Clear();
            scratch.structuralExportSlots.Clear();
            scratch.structuralDecisionValues.Clear();
        }

        [[nodiscard]] bool ContainsStructuralIndex(const containers::DynamicArray<u32>& values, const u32 value) noexcept
        {
            for (const u32 existing : values)
                if (existing == value)
                    return true;
            return false;
        }

        struct StructuralOperationExtras
        {
            FrameResourceDesc declarationDesc;
            StructuralImportContract importContract;
            bool hasDeclaration = false;
            bool hasImport = false;
        };

        [[nodiscard]] bool NormalizeStructuralOperation(const CandidateWriterBatch& batch, const CandidateOperation& operation,
                                                        const RenderFlowResourceAllocator::Impl& allocator, ResolveScratch& scratch,
                                                        u32& declarationOrdinal, CandidateOperation& normalized, StructuralOperationExtras& extras) noexcept
        {
            normalized = operation;
            extras = {};
            switch (operation.kind)
            {
            case CandidateOperationKind::DeclareTexture:
                if (operation.b >= batch.coldPayloads.Size())
                    return false;
                normalized.b = declarationOrdinal++;
                extras.declarationDesc = FrameResourceDesc::Texture(batch.coldPayloads[operation.b].textureDeclaration);
                extras.hasDeclaration = true;
                break;
            case CandidateOperationKind::DeclareBuffer:
                if (operation.b >= batch.coldPayloads.Size())
                    return false;
                normalized.b = declarationOrdinal++;
                extras.declarationDesc = FrameResourceDesc::Buffer(batch.coldPayloads[operation.b].bufferDeclaration);
                extras.hasDeclaration = true;
                break;
            case CandidateOperationKind::ImportTexture:
            case CandidateOperationKind::ImportBuffer:
                if (operation.b >= allocator.retainedImports.Size() || ContainsStructuralIndex(scratch.structuralImportIndices, operation.b))
                    return false;
                normalized.b = scratch.structuralImportIndices.Size();
                scratch.structuralImportIndices.PushBack(operation.b);
                extras.importContract = MakeStructuralImportContract(allocator.retainedImports[operation.b]);
                extras.hasImport = true;
                break;
            case CandidateOperationKind::ScopeOpen:
            {
                const ResourceScopeId scope = operation.payload.scope;
                if (!scope.IsValid())
                    return false;
                const u32 ordinal = scratch.structuralScopeClosed.Size();
                if (!scratch.structuralScopeOrdinals.Insert(scope.value, ordinal))
                    return false;
                scratch.structuralScopeClosed.PushBack(0);
                normalized.payload.scope = {static_cast<u64>(ordinal) + 1u};
                break;
            }
            case CandidateOperationKind::ScopeClose:
            {
                const ResourceScopeId scope = operation.payload.scope;
                u32 ordinal = InvalidRenderFlowResourceIndex;
                if (!scope.IsValid() || !scratch.structuralScopeOrdinals.Find(scope.value, ordinal) || ordinal >= scratch.structuralScopeClosed.Size() ||
                    scratch.structuralScopeClosed[ordinal] != 0)
                    return false;
                scratch.structuralScopeClosed[ordinal] = 1;
                normalized.payload.scope = {static_cast<u64>(ordinal) + 1u};
                break;
            }
            case CandidateOperationKind::Decision:
                normalized.flags = 0;
                scratch.structuralDecisionValues.PushBack(operation.FlagSet() ? u8{1} : u8{0});
                break;
            case CandidateOperationKind::Export:
                if (operation.b >= allocator.reservedExportSlots || ContainsStructuralIndex(scratch.structuralExportSlots, operation.b))
                    return false;
                normalized.b = scratch.structuralExportSlots.Size();
                scratch.structuralExportSlots.PushBack(operation.b);
                break;
            default:
                break;
            }
            return true;
        }

        [[nodiscard]] bool StructuralOperationEqual(const CandidateOperation& left, const CandidateOperation& right) noexcept
        {
            if (left.kind != right.kind || left.sequence != right.sequence || left.a != right.a || left.b != right.b)
                return false;
            if (left.kind == CandidateOperationKind::ScopeOpen || left.kind == CandidateOperationKind::ScopeClose)
                return left.payload.scope == right.payload.scope;
            if (left.kind == CandidateOperationKind::Export)
                return ExportDescEqual(left.payload.exportDesc, right.payload.exportDesc);
            return true;
        }

        [[nodiscard]] bool StructuralScopesClosed(const ResolveScratch& scratch) noexcept
        {
            for (const u8 closed : scratch.structuralScopeClosed)
                if (closed == 0)
                    return false;
            return true;
        }

        [[nodiscard]] bool StructuralPlanMatchesCurrent(const PersistentStructuralPlan& plan, const RenderFlowResourceAllocator::Impl& allocator,
                                                       ResolveScratch& scratch) noexcept
        {
            if (!plan.valid || plan.policy.enablePlacedResources != allocator.activePolicy.enablePlacedResources ||
                plan.policy.processEviction != allocator.activePolicy.processEviction ||
                plan.batches.Size() != allocator.sealedWriters.Size() || plan.queueGroups.Size() != allocator.sealedQueueGroups.Size())
                return false;
            ResetStructuralNormalization(scratch);
            u32 declarationOrdinal = 0;
            u32 expectedImportOrdinal = 0;
            for (u32 batchIndex = 0; batchIndex < allocator.sealedWriters.Size(); ++batchIndex)
            {
                const SealedWriterRecord& sealed = allocator.sealedWriters[batchIndex];
                if (sealed.groupIndex >= allocator.writerReservations.Size())
                    return false;
                const CandidateWriterBatch& batch = allocator.writerReservations[sealed.groupIndex].batch;
                const StructuralBatchKey& key = plan.batches[batchIndex];
                if (key.groupIndex != sealed.groupIndex || key.node != batch.node || key.flowGroup != batch.flowGroup || key.commandScope != batch.commandScope ||
                    key.declarationCount != batch.declarationCount || key.semanticSequenceCount != batch.semanticSequenceCount || key.stepCount != batch.stepCount ||
                    key.explicitScopeOpenCount != batch.explicitScopeOpenCount || key.decisionCount != batch.decisionCount || key.exportCount != batch.exportCount ||
                    key.resources.Size() != batch.resourceCount || key.textureViews.Size() != batch.textureViews.Size() ||
                    key.bufferViews.Size() != batch.bufferViews.Size() || key.uses.Size() != batch.uses.Size() ||
                    key.useEndOrder.Size() != batch.useEndOrder.Size() || key.operations.Size() != batch.operations.Size())
                    return false;
                for (u32 resourceIndex = 0; resourceIndex < batch.resourceCount; ++resourceIndex)
                {
                    const CandidateResource& current = batch.resources[resourceIndex];
                    const StructuralResourceKey& expected = key.resources[resourceIndex];
                    if (current.identity != expected.identity)
                        return false;
                    if (current.identity == CandidateIdentityKind::Named &&
                        (current.flowSpace != expected.flowSpace || containers::StringView(current.name) != containers::StringView(expected.name)))
                        return false;
                }
                for (u32 viewIndex = 0; viewIndex < batch.textureViews.Size(); ++viewIndex)
                {
                    const CandidateTextureView& current = batch.textureViews[viewIndex];
                    const CandidateTextureView& expected = key.textureViews[viewIndex];
                    if (current.resource.flowGroup != batch.flowGroup || current.resource.generation != batch.sessionGeneration ||
                        current.resource.index != expected.resource.index || expected.resource.flowGroup != batch.flowGroup ||
                        !TextureViewDescEqual(current.desc, expected.desc))
                        return false;
                }
                for (u32 viewIndex = 0; viewIndex < batch.bufferViews.Size(); ++viewIndex)
                {
                    const CandidateBufferView& current = batch.bufferViews[viewIndex];
                    const CandidateBufferView& expected = key.bufferViews[viewIndex];
                    if (current.resource.flowGroup != batch.flowGroup || current.resource.generation != batch.sessionGeneration ||
                        current.resource.index != expected.resource.index || expected.resource.flowGroup != batch.flowGroup ||
                        !BufferViewDescEqual(current.desc, expected.desc))
                        return false;
                }
                for (u32 useIndex = 0; useIndex < batch.uses.Size(); ++useIndex)
                    if (!CandidateUseEqual(batch.uses[useIndex], key.uses[useIndex]))
                        return false;
                for (u32 endIndex = 0; endIndex < batch.useEndOrder.Size(); ++endIndex)
                    if (batch.useEndOrder[endIndex] != key.useEndOrder[endIndex])
                        return false;
                for (u32 operationIndex = 0; operationIndex < batch.operations.Size(); ++operationIndex)
                {
                    CandidateOperation normalized;
                    StructuralOperationExtras extras;
                    if (!NormalizeStructuralOperation(batch, batch.operations[operationIndex], allocator, scratch, declarationOrdinal, normalized, extras) ||
                        !StructuralOperationEqual(normalized, key.operations[operationIndex]))
                        return false;
                    if (extras.hasDeclaration)
                    {
                        if (normalized.b >= plan.declarationDescs.Size() || !ResourceDescEqual(extras.declarationDesc, plan.declarationDescs[normalized.b]))
                            return false;
                    }
                    if (extras.hasImport)
                    {
                        if (normalized.b != expectedImportOrdinal++ || normalized.b >= plan.importContracts.Size() ||
                            !StructuralImportContractEqual(extras.importContract, plan.importContracts[normalized.b]))
                            return false;
                    }
                }
            }
            if (!StructuralScopesClosed(scratch) || declarationOrdinal != plan.declarationDescs.Size() ||
                expectedImportOrdinal != plan.importContracts.Size() || scratch.structuralExportSlots.Size() != plan.pendingExports.Size())
                return false;
            for (u32 queueIndex = 0; queueIndex < allocator.sealedQueueGroups.Size(); ++queueIndex)
            {
                const u32 groupIndex = allocator.sealedQueueGroups[queueIndex];
                if (groupIndex >= allocator.queueRequestGroups.Size())
                    return false;
                const QueueRequestGroup& current = allocator.queueRequestGroups[groupIndex];
                const StructuralQueueGroupKey& expected = plan.queueGroups[queueIndex];
                if (expected.groupIndex != groupIndex || expected.count != current.count || current.count > 2u)
                    return false;
                for (u32 requestIndex = 0; requestIndex < current.count; ++requestIndex)
                    if (!QueueRequestEqual(current.requests[requestIndex], expected.requests[requestIndex]))
                        return false;
            }
            return true;
        }

        [[nodiscard]] u32 FindStructuralImportOrdinal(const ResolveScratch& scratch, const u32 currentImport) noexcept
        {
            for (u32 ordinal = 0; ordinal < scratch.structuralImportIndices.Size(); ++ordinal)
                if (scratch.structuralImportIndices[ordinal] == currentImport)
                    return ordinal;
            return InvalidRenderFlowResourceIndex;
        }

        [[nodiscard]] u32 FindStructuralExportOrdinal(const ResolveScratch& scratch, const u32 currentSlot) noexcept
        {
            for (u32 ordinal = 0; ordinal < scratch.structuralExportSlots.Size(); ++ordinal)
                if (scratch.structuralExportSlots[ordinal] == currentSlot)
                    return ordinal;
            return InvalidRenderFlowResourceIndex;
        }

        [[nodiscard]] bool BuildStructuralPlanCandidate(const u64 signature, const RenderFlowResourceAllocator::Impl& allocator,
                                                        const ExecutionGenerationRef::Impl& generation, ResolveScratch& scratch,
                                                        const u32 physicalAssignmentFirst) noexcept
        {
            PersistentStructuralPlan& candidate = scratch.candidateStructuralPlan;
            candidate.Clear();
            if (allocator.config.allowLogicalOnlyValidation || allocator.activePolicy.enablePlacedResources)
                return false;

            ResetStructuralNormalization(scratch);
            u32 declarationOrdinal = 0;
            candidate.signature = signature;
            candidate.policy = allocator.activePolicy;
            candidate.batches.Reserve(allocator.sealedWriters.Size());
            for (u32 batchIndex = 0; batchIndex < allocator.sealedWriters.Size(); ++batchIndex)
            {
                const SealedWriterRecord& sealed = allocator.sealedWriters[batchIndex];
                if (sealed.groupIndex >= allocator.writerReservations.Size())
                    return false;
                const CandidateWriterBatch& batch = allocator.writerReservations[sealed.groupIndex].batch;
                StructuralBatchKey& key = candidate.batches.EmplaceBack();
                key.node = batch.node;
                key.flowGroup = batch.flowGroup;
                key.commandScope = batch.commandScope;
                key.groupIndex = sealed.groupIndex;
                key.declarationCount = batch.declarationCount;
                key.semanticSequenceCount = batch.semanticSequenceCount;
                key.stepCount = batch.stepCount;
                key.explicitScopeOpenCount = batch.explicitScopeOpenCount;
                key.decisionCount = batch.decisionCount;
                key.exportCount = batch.exportCount;
                key.resources.Reserve(batch.resourceCount);
                for (u32 resourceIndex = 0; resourceIndex < batch.resourceCount; ++resourceIndex)
                {
                    const CandidateResource& current = batch.resources[resourceIndex];
                    StructuralResourceKey& resource = key.resources.EmplaceBack();
                    resource.identity = current.identity;
                    if (current.identity == CandidateIdentityKind::Named)
                    {
                        resource.flowSpace = current.flowSpace;
                        resource.name.Set(containers::StringView(current.name));
                    }
                }
                key.textureViews.Reserve(batch.textureViews.Size());
                for (const CandidateTextureView& current : batch.textureViews)
                {
                    CandidateTextureView view = current;
                    view.resource.generation = 0;
                    key.textureViews.PushBack(view);
                }
                key.bufferViews.Reserve(batch.bufferViews.Size());
                for (const CandidateBufferView& current : batch.bufferViews)
                {
                    CandidateBufferView view = current;
                    view.resource.generation = 0;
                    key.bufferViews.PushBack(view);
                }
                key.uses.Reserve(batch.uses.Size());
                for (const CandidateUse& use : batch.uses)
                    key.uses.PushBack(use);
                key.useEndOrder.Reserve(batch.useEndOrder.Size());
                for (const u32 end : batch.useEndOrder)
                    key.useEndOrder.PushBack(end);
                key.operations.Reserve(batch.operations.Size());
                for (const CandidateOperation& operation : batch.operations)
                {
                    CandidateOperation normalized;
                    StructuralOperationExtras extras;
                    if (!NormalizeStructuralOperation(batch, operation, allocator, scratch, declarationOrdinal, normalized, extras))
                    {
                        candidate.Clear();
                        return false;
                    }
                    key.operations.PushBack(normalized);
                    if (extras.hasDeclaration)
                        candidate.declarationDescs.PushBack(extras.declarationDesc);
                    if (extras.hasImport)
                        candidate.importContracts.PushBack(extras.importContract);
                }
            }
            if (!StructuralScopesClosed(scratch) || declarationOrdinal != candidate.declarationDescs.Size())
            {
                candidate.Clear();
                return false;
            }
            candidate.queueGroups.Reserve(allocator.sealedQueueGroups.Size());
            for (const u32 groupIndex : allocator.sealedQueueGroups)
            {
                if (groupIndex >= allocator.queueRequestGroups.Size())
                {
                    candidate.Clear();
                    return false;
                }
                const QueueRequestGroup& current = allocator.queueRequestGroups[groupIndex];
                if (current.count > 2u)
                {
                    candidate.Clear();
                    return false;
                }
                StructuralQueueGroupKey queue;
                queue.groupIndex = groupIndex;
                queue.count = current.count;
                for (u32 requestIndex = 0; requestIndex < current.count; ++requestIndex)
                    queue.requests[requestIndex] = current.requests[requestIndex];
                candidate.queueGroups.PushBack(queue);
            }

            candidate.allocations.Reserve(generation.allocations.Size());
            for (const LogicalAllocationRecord& current : generation.allocations)
            {
                LogicalAllocationRecord allocation = current;
                allocation.physical = {};
                if (allocation.imported)
                {
                    allocation.importedResource = FindStructuralImportOrdinal(scratch, current.importedResource);
                    if (allocation.importedResource == InvalidRenderFlowResourceIndex)
                    {
                        candidate.Clear();
                        return false;
                    }
                }
                candidate.allocations.PushBack(allocation);
            }
            candidate.packets.Reserve(generation.packets.Size());
            for (const CompiledPacket& current : generation.packets)
            {
                StructuralPacketTemplate& packet = candidate.packets.EmplaceBack();
                packet.node = current.node;
                packet.flowGroup = current.flowGroup;
                packet.commandScope = current.commandScope;
                packet.queue = current.queue;
                packet.decisionCount = current.decisions.Size();
                packet.useSlotCount = current.runtime.liveness != nullptr ? current.runtime.liveness->useActive.Size() : 0u;
                packet.steps.Reserve(current.steps.Size());
                for (const CompiledStep& currentStep : current.steps)
                {
                    CompiledStep step = currentStep;
                    step.use.generation = 0;
                    step.physical = {};
                    step.beforeActionOffset = 0;
                    step.beforeActionCount = 0;
                    step.afterActionOffset = 0;
                    step.afterActionCount = 0;
                    step.aliasPredecessorOffset = 0;
                    step.aliasPredecessorCount = 0;
                    step.finalizeForAlias = false;
                    packet.steps.PushBack(step);
                }
            }
            candidate.commandScopes.Reserve(generation.commandScopes.Size());
            for (const CompiledCommandScope& scope : generation.commandScopes)
                candidate.commandScopes.PushBack(scope);
            candidate.queueDependencies.Reserve(generation.queueDependencies.Size());
            for (const CompiledQueueDependency& dependency : generation.queueDependencies)
                candidate.queueDependencies.PushBack(dependency);
            candidate.crossQueueProducerScopes.Reserve(scratch.crossQueueProducerScopes.Size());
            for (const u32 producer : scratch.crossQueueProducerScopes)
                candidate.crossQueueProducerScopes.PushBack(producer);
            if (physicalAssignmentFirst > scratch.physicalAssignmentOrder.Size())
            {
                candidate.Clear();
                return false;
            }
            candidate.firstTouchAllocationOrder.Reserve(scratch.physicalAssignmentOrder.Size() - physicalAssignmentFirst);
            for (u32 order = physicalAssignmentFirst; order < scratch.physicalAssignmentOrder.Size(); ++order)
                candidate.firstTouchAllocationOrder.PushBack(scratch.physicalAssignmentOrder[order]);
            candidate.pendingExports.Reserve(generation.pendingExports.Size());
            for (const PendingExportRecord& current : generation.pendingExports)
            {
                PendingExportRecord exported = current;
                const u32 ordinal = FindStructuralExportOrdinal(scratch, current.slot.index);
                if (ordinal == InvalidRenderFlowResourceIndex)
                {
                    candidate.Clear();
                    return false;
                }
                exported.slot = {ordinal, 0};
                exported.physical = {};
                exported.textureDesc = {};
                exported.bufferDesc = {};
                candidate.pendingExports.PushBack(exported);
            }
            if (candidate.importContracts.Size() != scratch.structuralImportIndices.Size() ||
                candidate.pendingExports.Size() != scratch.structuralExportSlots.Size())
            {
                candidate.Clear();
                return false;
            }
            candidate.valid = true;
            return true;
        }

        [[nodiscard]] bool InstantiateStructuralPlan(const PersistentStructuralPlan& plan, RenderFlowResourceAllocator::Impl& allocator,
                                                     ExecutionGenerationRef::Impl& generation, ResolveScratch& scratch,
                                                     RenderFlowResourceFailure* const failure) noexcept
        {
            if (!plan.valid || allocator.config.allowLogicalOnlyValidation || allocator.activePolicy.enablePlacedResources ||
                scratch.structuralImportIndices.Size() != plan.importContracts.Size() ||
                scratch.structuralExportSlots.Size() != plan.pendingExports.Size())
                return false;
            generation.allocations.Reserve(plan.allocations.Size());
            for (const LogicalAllocationRecord& current : plan.allocations)
            {
                LogicalAllocationRecord allocation = current;
                allocation.physical = {};
                if (allocation.imported)
                {
                    if (allocation.importedResource >= scratch.structuralImportIndices.Size())
                        return false;
                    allocation.importedResource = scratch.structuralImportIndices[allocation.importedResource];
                    if (allocation.importedResource >= allocator.retainedImports.Size())
                        return false;
                }
                generation.allocations.PushBack(allocation);
            }

            generation.packets.Reserve(plan.packets.Size());
            generation.packetByNode.Reserve(plan.packets.Size());
            u32 decisionOrdinal = 0;
            for (u32 packetIndex = 0; packetIndex < plan.packets.Size(); ++packetIndex)
            {
                const StructuralPacketTemplate& source = plan.packets[packetIndex];
                if (!generation.packetByNode.Insert(source.node.value, packetIndex).IsSuccessful())
                    return false;
                CompiledPacket& packet = generation.packets.EmplaceBack();
                packet.node = source.node;
                packet.flowGroup = source.flowGroup;
                packet.commandScope = source.commandScope;
                packet.queue = source.queue;
                packet.steps.Reserve(source.steps.Size());
                packet.beginStepByUseOrdinal.Reserve(source.useSlotCount);
                packet.endStepByUseOrdinal.Resize(source.useSlotCount, InvalidRenderFlowResourceIndex);
                u32 endUseCount = 0;
                for (const CompiledStep& current : source.steps)
                {
                    CompiledStep step = current;
                    step.use.generation = allocator.sessionGeneration;
                    step.physical = {};
                    const u32 stepIndex = packet.steps.Size();
                    if (step.kind == CompiledResourceStepKind::TextureUseBegin || step.kind == CompiledResourceStepKind::BufferUseBegin)
                    {
                        if (!step.use.IsValid() || step.use.flowGroup != packet.flowGroup || step.use.ordinal != packet.beginStepByUseOrdinal.Size() ||
                            step.use.ordinal >= source.useSlotCount)
                            return false;
                        packet.beginStepByUseOrdinal.PushBack(stepIndex);
                    }
                    else if (step.kind == CompiledResourceStepKind::UseEnd)
                    {
                        if (!step.use.IsValid() || step.use.flowGroup != packet.flowGroup || step.use.ordinal >= packet.endStepByUseOrdinal.Size() ||
                            packet.endStepByUseOrdinal[step.use.ordinal] != InvalidRenderFlowResourceIndex)
                            return false;
                        packet.endStepByUseOrdinal[step.use.ordinal] = stepIndex;
                        ++endUseCount;
                    }
                    else
                    {
                        return false;
                    }
                    packet.steps.PushBack(step);
                }
                if (packet.beginStepByUseOrdinal.Size() != source.useSlotCount || endUseCount != source.useSlotCount)
                    return false;
                packet.decisions.Reserve(source.decisionCount);
                for (u32 localDecision = 0; localDecision < source.decisionCount; ++localDecision)
                {
                    if (decisionOrdinal >= scratch.structuralDecisionValues.Size())
                        return false;
                    packet.decisions.PushBack({{source.flowGroup, localDecision, allocator.sessionGeneration}, scratch.structuralDecisionValues[decisionOrdinal++] != 0});
                }
                if (source.useSlotCount != 0)
                {
                    packet.runtime.liveness = AllocatePacketUseLiveness();
                    if (packet.runtime.liveness == nullptr)
                        return Fail(failure, RenderFlowResourceFailureCode::CapacityExceeded, allocator.state, "structural-plan packet liveness allocation failed", packet.node);
                    packet.runtime.liveness->useActive.Resize(source.useSlotCount, u8{0});
                }
            }
            if (decisionOrdinal != scratch.structuralDecisionValues.Size())
                return false;
            generation.commandScopes.Reserve(plan.commandScopes.Size());
            for (const CompiledCommandScope& scope : plan.commandScopes)
                generation.commandScopes.PushBack(scope);
            generation.queueDependencies.Reserve(plan.queueDependencies.Size());
            for (const CompiledQueueDependency& dependency : plan.queueDependencies)
                generation.queueDependencies.PushBack(dependency);
            scratch.crossQueueProducerScopes.Clear();
            scratch.crossQueueProducerScopes.Reserve(plan.crossQueueProducerScopes.Size());
            for (const u32 producer : plan.crossQueueProducerScopes)
                scratch.crossQueueProducerScopes.PushBack(producer);

            generation.pendingExports.Reserve(plan.pendingExports.Size());
            for (const PendingExportRecord& current : plan.pendingExports)
            {
                PendingExportRecord exported = current;
                if (exported.slot.index >= scratch.structuralExportSlots.Size())
                    return false;
                exported.slot = {scratch.structuralExportSlots[exported.slot.index], allocator.sessionGeneration};
                exported.physical = {};
                exported.textureDesc = {};
                exported.bufferDesc = {};
                generation.pendingExports.PushBack(exported);
            }
            if (generation.pendingExports.Size() >
                allocator.config.maximumOutstandingPublishedExports -
                    (allocator.publishedExports.Size() <= allocator.config.maximumOutstandingPublishedExports ? allocator.publishedExports.Size() : allocator.config.maximumOutstandingPublishedExports))
                return Fail(failure, RenderFlowResourceFailureCode::CapacityExceeded, allocator.state, "outstanding terminal export publication capacity is exhausted");
            allocator.publishedExports.Reserve(allocator.publishedExports.Size() + generation.pendingExports.Size());
            allocator.publishedExportIndices.Reserve(allocator.publishedExports.Size() + generation.pendingExports.Size());

            scratch.physicalAssignmentOrder.Clear();
            scratch.physicalAssignmentOrder.Reserve(allocator.reservedExportSlots + plan.firstTouchAllocationOrder.Size());
            scratch.physicalAssignmentOrder.Resize(allocator.reservedExportSlots, InvalidRenderFlowResourceIndex);
            for (const u32 allocation : plan.firstTouchAllocationOrder)
            {
                if (allocation >= generation.allocations.Size())
                    return false;
                scratch.physicalAssignmentOrder.PushBack(allocation);
            }

            generation.presentationAcquisition = {};
            generation.presentationCommandScope = {};
            for (CompiledPacket& packet : generation.packets)
            {
                for (const CompiledStep& step : packet.steps)
                {
                    if (step.logicalAllocation >= generation.allocations.Size())
                        return false;
                    const LogicalAllocationRecord& allocation = generation.allocations[step.logicalAllocation];
                    if (!allocation.imported)
                        continue;
                    if (allocation.importedResource >= allocator.retainedImports.Size())
                        return false;
                    const RetainedImportRecord& imported = allocator.retainedImports[allocation.importedResource];
                    if ((step.kind == CompiledResourceStepKind::TextureUseBegin || step.kind == CompiledResourceStepKind::BufferUseBegin) &&
                        imported.readiness == ImportReadinessKind::ExplicitFenceWait)
                    {
                        const u32 queue = static_cast<u32>(imported.incomingWait.queue);
                        if (queue >= 3u)
                            return false;
                        if (imported.incomingWait.value > packet.incomingWaits[queue])
                            packet.incomingWaits[queue] = imported.incomingWait.value;
                    }
                    if (step.terminalPresent)
                    {
                        if (!imported.presentationAcquisition.IsValid() || generation.presentationAcquisition.IsValid())
                            return false;
                        generation.presentationAcquisition = imported.presentationAcquisition;
                        generation.presentationCommandScope = packet.commandScope;
                    }
                }
            }
            generation.hasNativeResourceBindings = true;
            return true;
        }

        [[nodiscard]] bool BuildPersistentDedicatedCandidate(const ExecutionGenerationRef::Impl& generation, ResolveScratch& resolveScratch,
                                                             const u32 physicalAssignmentFirst) noexcept
        {
            PersistentDedicatedPhysicalPlan& candidate = resolveScratch.candidateDedicatedPlan;
            candidate.Clear();
            if (!generation.hasNativeResourceBindings || resolveScratch.persistentAllocationLanes.Size() != generation.allocations.Size())
                return false;

            candidate.allocations.Resize(generation.allocations.Size());
            candidate.scopeQueues.Reserve(generation.commandScopes.Size());
            for (const CompiledCommandScope& scope : generation.commandScopes)
                candidate.scopeQueues.PushBack(scope.queue);
            candidate.crossQueueProducerScopes = resolveScratch.crossQueueProducerScopes;

            u32 laneCount = 0;
            for (const u32 lane : resolveScratch.persistentAllocationLanes)
                if (lane != InvalidRenderFlowResourceIndex && lane + 1u > laneCount)
                    laneCount = lane + 1u;
            candidate.lanes.Resize(laneCount);
            candidate.laneRuntime.Resize(laneCount);
            for (PersistentDedicatedPlanLaneRuntime& runtime : candidate.laneRuntime)
                runtime.leases.Reserve(1);

            // Lane capacity must be derived in the exact first-touch order that
            // authored WholeFrameLane. Declaration/allocation index order is not
            // equivalent: a later-declared 2 KiB seed can acquire an incidental
            // 4 KiB backing and then satisfy an earlier-declared 4 KiB member in
            // the same frame. That observed oversizing is not a stable topology.
            for (u32 order = physicalAssignmentFirst; order < resolveScratch.physicalAssignmentOrder.Size(); ++order)
            {
                const u32 allocation = resolveScratch.physicalAssignmentOrder[order];
                if (allocation >= generation.allocations.Size())
                {
                    candidate.Clear();
                    return false;
                }
                const u32 laneIndex = resolveScratch.persistentAllocationLanes[allocation];
                if (laneIndex == InvalidRenderFlowResourceIndex)
                    continue;
                if (laneIndex >= candidate.lanes.Size())
                {
                    candidate.Clear();
                    return false;
                }
                const DedicatedResourceRequest request = BuildDedicatedResourceRequest(generation.allocations[allocation].desc);
                PersistentDedicatedPlanLane& lane = candidate.lanes[laneIndex];
                if (lane.compatibility.kind == FrameResourceKind::Invalid)
                {
                    lane.compatibility = request.compatibility;
                    lane.requiredCapacity = request.capacity;
                }
                else if (!DedicatedResourceSameCompatibilityClass(lane.compatibility, request.compatibility) || request.capacity > lane.requiredCapacity)
                {
                    // The current frame only reused this lane because the seed
                    // happened to receive more capacity than it requested.
                    candidate.Clear();
                    return false;
                }
            }

            for (u32 allocation = 0; allocation < generation.allocations.Size(); ++allocation)
            {
                const LogicalAllocationRecord& logical = generation.allocations[allocation];
                PersistentDedicatedPlanAllocation& planned = candidate.allocations[allocation];
                planned.desc = logical.desc;
                planned.firstUsePosition = logical.firstUsePosition;
                planned.lastUsePosition = logical.lastUsePosition;
                planned.firstUseCommandScopeIndex = logical.firstUseCommandScopeIndex;
                planned.lastUseCommandScopeIndex = logical.lastUseCommandScopeIndex;
                planned.queueMask = logical.queueMask;
                planned.used = logical.used;
                planned.exported = logical.exported;
                planned.imported = logical.imported;
                planned.requiresCanonicalDescriptors = logical.requiresCanonicalDescriptors;
                planned.lane = resolveScratch.persistentAllocationLanes[allocation];

                if (!logical.used || logical.imported || logical.exported)
                {
                    if (planned.lane != InvalidRenderFlowResourceIndex)
                        return false;
                    continue;
                }
                if (planned.lane >= candidate.lanes.Size())
                    return false;
            }
            for (const PersistentDedicatedPlanLane& lane : candidate.lanes)
                if (lane.compatibility.kind == FrameResourceKind::Invalid)
                {
                    candidate.Clear();
                    return false;
                }
            candidate.valid = true;
            return true;
        }

        [[nodiscard]] bool QueueOrderProved(const u32 producerScopeIndex, const u32 consumerScopeIndex, const containers::DynamicArray<CompiledCommandScope>& scopes,
                                            const containers::DynamicArray<u32>& crossQueueProducerScopes) noexcept
        {
            if (producerScopeIndex >= scopes.Size() || consumerScopeIndex >= scopes.Size())
                return false;
            const CompiledCommandScope& producer = scopes[producerScopeIndex];
            const CompiledCommandScope& consumer = scopes[consumerScopeIndex];
            if (producer.queue == consumer.queue)
                return true;
            if (RequiredQueueSync(producer.queue, consumer.queue) == rhi::CommandListSyncType::None)
                return false;
            if (consumerScopeIndex >= crossQueueProducerScopes.Size())
                return false;
            const u32 admittedProducerScopeIndex = crossQueueProducerScopes[consumerScopeIndex];
            return admittedProducerScopeIndex != InvalidRenderFlowResourceIndex && producerScopeIndex <= admittedProducerScopeIndex;
        }

        void TouchLifetime(LogicalAllocationRecord& allocation, const u32 allocationIndex, const PlanPosition position, const u32 commandScopeIndex, const CompiledPacket& packet,
                           containers::DynamicArray<u32>& physicalAssignmentOrder) noexcept
        {
            if (!allocation.firstUsePosition.IsValid())
            {
                allocation.firstUsePosition = position;
                allocation.firstUseCommandScopeIndex = commandScopeIndex;
                // CompileOperations already visits resource operations in strict
                // PlanPosition order. Record the allocation exactly once at the
                // authoritative first lifetime touch so physical assignment does
                // not have to rediscover and sort used allocations later.
                physicalAssignmentOrder.PushBack(allocationIndex);
            }
            allocation.lastUsePosition = position;
            allocation.lastUseCommandScopeIndex = commandScopeIndex;
            allocation.queueMask |= QueueMask(packet.queue);
        }

        [[nodiscard]] bool FailResolve(RenderFlowResourceAllocator::Impl& allocator, ExecutionGenerationRef::Impl* const scratch, RenderFlowResourceFailure* const failure,
                                       const RenderFlowResourceFailureCode code, const char* const message, const RenderFlowNodeId node = {}, const PlanPosition position = {},
                                       const ResourceUseId use = {}) noexcept
        {
            const bool result = Fail(failure, code, allocator.state, message, node, position, use);
            const rhi::DeviceState deviceState = !allocator.config.allowLogicalOnlyValidation && rhi::IsInitialized() ? rhi::TestDeviceState() : rhi::DeviceState::Operational;
            const bool deviceLost = deviceState == rhi::DeviceState::Removed || deviceState == rhi::DeviceState::ResetRequired;
            if (scratch != nullptr)
            {
                RenderFlowResourceFailure descriptorCleanupFailure;
                if (!RetireRetainedImportDescriptors(allocator, *scratch, {}, deviceLost, &descriptorCleanupFailure) && !deviceLost)
                    VG_FATAL(descriptorCleanupFailure.message != nullptr ? descriptorCleanupFailure.message
                                                                       : "retained import descriptor rollback failed");
                if (!deviceLost && scratch->hasNativeResourceBindings)
                {
                    allocator.placedPool.Rollback(scratch->placedBatch);
                    containers::DynamicArray<u32>& dedicatedRollbacks = allocator.resolveScratch.batchResourceSlots;
                    dedicatedRollbacks.Clear();
                    for (const PhysicalBindingRecord& binding : scratch->physicalBindings)
                        if (binding.kind == PhysicalBindingKind::DedicatedPool)
                            dedicatedRollbacks.PushBack(binding.poolEntry);
                    allocator.dedicatedPool.Rollback(dedicatedRollbacks);
                }
                if (!deviceLost && !allocator.resolveScratch.releasedPersistentLeases.Empty())
                {
                    // A failed dedicated->placed cutover must not lose the only
                    // identities capable of reclaiming the released RF6 bytes
                    // under a later shared hard-budget attempt.
                    for (const DedicatedResourceLease& lease : allocator.resolveScratch.releasedPersistentLeases)
                        allocator.dedicatedPool.EvictLease(lease);
                    allocator.dedicatedPool.Poll();
                }
                ReleaseGeneration(scratch);
            }
            // namedSlots borrows candidate-name bytes. Invalidate those entries
            // in O(1) before cancellation makes candidate storage writable.
            allocator.resolveScratch.namedSlots.Reset();
            if (deviceLost)
            {
                allocator.structuralPlan.Clear();
                ReleasePersistentDedicatedPlan(allocator, false);
                allocator.dedicatedPool.DeviceLost();
                allocator.placedPool.DeviceLost();
                allocator.stats.rejectedOperations += allocator.pendingRejectedOperations.Exchange(0);
                ResetPlanningStorage(allocator);
                allocator.retainedImports.Clear();
                allocator.publishedExports.Clear();
                allocator.publishedExportIndices.Reset();
                allocator.reservedExportSlots = 0;
                allocator.activePolicy = {};
                allocator.state = RenderFlowResourceSessionState::DeviceUnavailable;
                allocator.stats.state = allocator.state;
                ++allocator.stats.abortedFrames;
                return result;
            }
            CancelSession(allocator, allocator.sessionGeneration);
            return result;
        }

        // RF8: native state lowering owns one dense physical-state history for
        // the whole generation. Semantic compilation records intent on steps;
        // this pass binds that intent to the selected physical objects exactly
        // once, emits final actions directly, and carries only explicit-state
        // objects across command-list boundaries.
        [[nodiscard]] bool LowerPhysicalStates(RenderFlowResourceAllocator::Impl& allocator, ExecutionGenerationRef::Impl& generation, ResolveScratch& resolveScratch,
                                              u64& totalActions, RenderFlowResourceFailure* const failure) noexcept
        {
            VG_PROFILE_ZONE_NAMED("RenderFlow/Resolve/LowerPhysicalStates");
            containers::DynamicArray<PhysicalScopeState>& history = resolveScratch.physicalHistory;
            containers::DynamicArray<rhi::ResourceState>& cells = resolveScratch.physicalStateCells;
            containers::DynamicArray<u8>& accessCells = resolveScratch.physicalAccessCells;
            containers::DynamicArray<u32>& packetPhysicals = resolveScratch.packetPhysicalIndices;
            const u32 historyCount = resolveScratch.physicalHistoryCount;
            u32 totalEntries = 0;

            constexpr u8 physicalAccessSeen = 1u << 0u;
            constexpr u8 physicalAccessUav = 1u << 1u;
            constexpr u8 physicalAccessWrite = 1u << 2u;

            const auto reject = [&](const RenderFlowResourceFailureCode code, const char* const message, const CompiledPacket* const packet = nullptr,
                                    const ResourceUseId use = {}) noexcept
            {
                return Fail(failure, code, allocator.state, message, packet != nullptr ? packet->node : RenderFlowNodeId{}, {}, use);
            };
            const auto chargeActions = [&](const u32 count) noexcept
            {
                if (totalActions > allocator.config.maximumCompiledResourceActions ||
                    static_cast<u64>(count) > static_cast<u64>(allocator.config.maximumCompiledResourceActions) - totalActions)
                    return false;
                totalActions += count;
                return true;
            };
            const auto bindingReference = [](const PhysicalBindingRecord& binding) noexcept
            {
                return binding.texture.IsValid() ? rhi::ResourceRef(binding.texture) : rhi::ResourceRef(binding.buffer);
            };
            const auto bindingKind = [](const PhysicalBindingRecord& binding) noexcept
            {
                return binding.texture.IsValid() ? FrameResourceKind::Texture : FrameResourceKind::Buffer;
            };

            for (u32 packetIndex = 0; packetIndex < generation.packets.Size(); ++packetIndex)
            {
                CompiledPacket& packet = generation.packets[packetIndex];
                packetPhysicals.Clear();
                if (!packet.actions.Empty() || !packet.entryStates.Empty() || !packet.statePredecessorPackets.Empty())
                    return reject(RenderFlowResourceFailureCode::BackendContractViolation,
                                  "native semantic compilation leaked provisional state output into RF8 lowering", &packet);

                const auto ensurePhysical = [&](const PhysicalResourceId physical) noexcept -> bool
                {
                    if (!physical.IsValid() || physical.generation != generation.id.generation || physical.index >= historyCount ||
                        physical.index >= generation.physicalBindings.Size())
                        return reject(RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "physical-state lowering lost a physical assignment", &packet);
                    PhysicalScopeState& tracked = history[physical.index];
                    if (tracked.trackedPacket == packetIndex)
                        return true;

                    const PhysicalBindingRecord& binding = generation.physicalBindings[physical.index];
                    if (binding.texture.IsValid() == binding.buffer.IsValid())
                        return reject(RenderFlowResourceFailureCode::BackendContractViolation, "physical-state lowering found an invalid native binding", &packet);
                    const bool texture = binding.texture.IsValid();
                    const rhi::ResourceRef reference = bindingReference(binding);
                    rhi::ResourceState initial = binding.initialState;
                    u32 cellCount = 1;
                    if (texture)
                    {
                        if (binding.stateDescriptorKind != FrameResourceKind::Texture || binding.textureMipCount == 0 || binding.textureArraySize == 0)
                            return reject(RenderFlowResourceFailureCode::BackendContractViolation, "physical texture has no canonical state descriptor", &packet);
                        if (binding.explicitState && binding.kind == PhysicalBindingKind::PlacedPool && binding.initialState != rhi::ResourceState::Common)
                            return reject(RenderFlowResourceFailureCode::UnsupportedCapability, "explicit placed textures require Common creation and reuse state", &packet);
                        cellCount = static_cast<u32>(binding.textureMipCount) * binding.textureArraySize;
                        if (binding.explicitState)
                            initial = tracked.initial;
                    }
                    else
                    {
                        if (binding.stateDescriptorKind != FrameResourceKind::Buffer)
                            return reject(RenderFlowResourceFailureCode::BackendContractViolation, "physical buffer has no canonical state descriptor", &packet);
                        if (binding.explicitState && (binding.bufferMemoryType != rhi::MemoryType::DeviceLocal || binding.initialState != rhi::ResourceState::Common))
                            return reject(RenderFlowResourceFailureCode::UnsupportedCapability, "explicit buffers require device-local Common creation state", &packet);
                    }
                    if (!reference.IsValid() || initial == rhi::ResourceState::Unknown || cellCount == 0)
                        return reject(RenderFlowResourceFailureCode::BackendContractViolation, "physical state has no explicit native initial state", &packet);

                    if (tracked.cellCount == 0)
                    {
                        tracked.cellOffset = cells.Size();
                        tracked.cellCount = cellCount;
                        cells.Resize(cells.Size() + cellCount, initial);
                        accessCells.Resize(accessCells.Size() + cellCount, u8{0});
                    }
                    else if (tracked.cellCount != cellCount || tracked.cellOffset > cells.Size() || cellCount > cells.Size() - tracked.cellOffset ||
                             tracked.cellOffset > accessCells.Size() || cellCount > accessCells.Size() - tracked.cellOffset)
                    {
                        return reject(RenderFlowResourceFailureCode::BackendContractViolation, "physical-state cell storage disagrees with the binding descriptor", &packet);
                    }

                    if (!binding.explicitState)
                    {
                        for (u32 cell = 0; cell < cellCount; ++cell)
                            cells[tracked.cellOffset + cell] = initial;
                    }
                    else if (tracked.lastPacket != InvalidRenderFlowResourceIndex)
                    {
                        if (tracked.lastPacket >= generation.packets.Size())
                            return reject(RenderFlowResourceFailureCode::BackendContractViolation, "explicit physical state predecessor is stale", &packet);
                        CompiledPacket& predecessor = generation.packets[tracked.lastPacket];
                        if (predecessor.queue != packet.queue)
                        {
                            if (!QueueOrderProved(tracked.lastPacket, packetIndex, generation.commandScopes, resolveScratch.crossQueueProducerScopes))
                                return reject(RenderFlowResourceFailureCode::QueueOrCommandScopeMismatch,
                                              "explicit physical state crosses queues without a matching fork/join boundary", &packet);
                            const FrameResourceKind kind = bindingKind(binding);
                            const u32 transitionStart = predecessor.exitActions.Size();
                            for (u32 cell = 0; cell < cellCount; ++cell)
                            {
                                rhi::ResourceState& priorState = cells[tracked.cellOffset + cell];
                                if (QueueStateAllowed(packet.queue, kind, priorState))
                                    continue;
                                if (predecessor.queue != rhi::QueueType::Graphics || packet.queue != rhi::QueueType::Compute ||
                                    !QueueStateAllowed(predecessor.queue, kind, priorState) || !QueueStateAllowed(predecessor.queue, kind, rhi::ResourceState::Common) ||
                                    !QueueStateAllowed(packet.queue, kind, rhi::ResourceState::Common))
                                    return reject(RenderFlowResourceFailureCode::QueueOrCommandScopeMismatch,
                                                  "explicit physical state cannot be handed to the consumer queue", &packet);
                                CompiledResourceAction action;
                                action.kind = texture ? CompiledResourceActionKind::TextureTransition : CompiledResourceActionKind::BufferTransition;
                                action.physical = physical;
                                action.before = priorState;
                                action.after = rhi::ResourceState::Common;
                                if (texture)
                                {
                                    action.textureSubresources = {static_cast<u16>(cell % binding.textureMipCount), 1,
                                                                  static_cast<u16>(cell / binding.textureMipCount), 1};
                                    if (!AppendOrMergeTextureTransition(predecessor.exitActions, transitionStart, action, false))
                                        return reject(RenderFlowResourceFailureCode::CapacityExceeded, "cross-queue texture handoff storage allocation failed", &packet);
                                }
                                else
                                {
                                    if (!chargeActions(1))
                                        return reject(RenderFlowResourceFailureCode::CapacityExceeded, "cross-queue buffer handoff exceeds the action capacity", &packet);
                                    predecessor.exitActions.PushBack(action);
                                }
                                priorState = rhi::ResourceState::Common;
                            }
                            if (texture)
                            {
                                if (!FinalizeTextureTransitionBlock(predecessor.exitActions, transitionStart, resolveScratch.textureTransitionRuns))
                                    return reject(RenderFlowResourceFailureCode::BackendContractViolation, "cross-queue texture handoff block is not self-contained", &packet);
                                if (!chargeActions(predecessor.exitActions.Size() - transitionStart))
                                    return reject(RenderFlowResourceFailureCode::CapacityExceeded, "cross-queue texture handoff exceeds the action capacity", &packet);
                            }
                        }
                        packet.statePredecessorPackets.PushBack(tracked.lastPacket);
                    }

                    if (cellCount > allocator.config.maximumCommandScopeEntryStates - totalEntries)
                        return reject(RenderFlowResourceFailureCode::CapacityExceeded, "command-scope entry states exceed the aggregate metadata capacity", &packet);
                    totalEntries += cellCount;
                    for (u32 cell = 0; cell < cellCount; ++cell)
                    {
                        rhi::CommandListEntryState entry;
                        entry.resource = reference;
                        entry.state = cells[tracked.cellOffset + cell];
                        if (texture)
                            entry.subresource = {static_cast<u16>(cell % binding.textureMipCount), static_cast<u16>(cell / binding.textureMipCount)};
                        if (!QueueStateAllowed(packet.queue, bindingKind(binding), entry.state))
                            return reject(RenderFlowResourceFailureCode::QueueOrCommandScopeMismatch, "native entry state is illegal on the packet queue", &packet);
                        packet.entryStates.PushBack(entry);
                    }
                    tracked.trackedPacket = packetIndex;
                    packetPhysicals.PushBack(physical.index);
                    return true;
                };

                const auto transitionTexture = [&](const CompiledStep& step, const PhysicalResourceId physical, const rhi::SubresourceRange range,
                                                   const rhi::ResourceState after, const u32 blockStart) noexcept -> bool
                {
                    if (!ensurePhysical(physical))
                        return false;
                    const PhysicalBindingRecord& binding = generation.physicalBindings[physical.index];
                    if (!binding.texture.IsValid() || binding.buffer.IsValid() || after == rhi::ResourceState::Unknown)
                        return reject(RenderFlowResourceFailureCode::BackendContractViolation, "texture state intent targets a non-texture physical binding", &packet, step.use);
                    const u32 firstMip = range.firstMip;
                    const u32 firstSlice = range.firstSlice;
                    const u32 mipCount = range.mipCount == 0xffffu ? binding.textureMipCount - firstMip : range.mipCount;
                    const u32 sliceCount = range.sliceCount == 0xffffu ? binding.textureArraySize - firstSlice : range.sliceCount;
                    if (firstMip >= binding.textureMipCount || firstSlice >= binding.textureArraySize || mipCount == 0 || sliceCount == 0 ||
                        mipCount > binding.textureMipCount - firstMip || sliceCount > binding.textureArraySize - firstSlice)
                        return reject(RenderFlowResourceFailureCode::BackendContractViolation, "texture state intent has an invalid physical subresource range", &packet, step.use);
                    PhysicalScopeState& tracked = history[physical.index];
                    for (u32 slice = firstSlice; slice < firstSlice + sliceCount; ++slice)
                        for (u32 mip = firstMip; mip < firstMip + mipCount; ++mip)
                        {
                            rhi::ResourceState& state = cells[tracked.cellOffset + slice * binding.textureMipCount + mip];
                            if (state == after)
                                continue;
                            CompiledResourceAction action;
                            action.kind = CompiledResourceActionKind::TextureTransition;
                            action.logicalAllocation = step.logicalAllocation;
                            action.physical = physical;
                            action.before = state;
                            action.after = after;
                            action.textureSubresources = {static_cast<u16>(mip), 1, static_cast<u16>(slice), 1};
                            if (!AppendOrMergeTextureTransition(packet.actions, blockStart, action, false))
                                return reject(RenderFlowResourceFailureCode::CapacityExceeded, "texture transition storage allocation failed", &packet, step.use);
                            state = after;
                        }
                    return true;
                };
                const auto transitionBuffer = [&](const CompiledStep& step, const PhysicalResourceId physical, const rhi::ResourceState after) noexcept -> bool
                {
                    if (!ensurePhysical(physical))
                        return false;
                    const PhysicalBindingRecord& binding = generation.physicalBindings[physical.index];
                    if (!binding.buffer.IsValid() || binding.texture.IsValid() || after == rhi::ResourceState::Unknown)
                        return reject(RenderFlowResourceFailureCode::BackendContractViolation, "buffer state intent targets a non-buffer physical binding", &packet, step.use);
                    rhi::ResourceState& state = cells[history[physical.index].cellOffset];
                    if (state == after)
                        return true;
                    if (!chargeActions(1))
                        return reject(RenderFlowResourceFailureCode::CapacityExceeded, "buffer transitions exceed the action capacity", &packet, step.use);
                    CompiledResourceAction action;
                    action.kind = CompiledResourceActionKind::BufferTransition;
                    action.logicalAllocation = step.logicalAllocation;
                    action.physical = physical;
                    action.before = state;
                    action.after = after;
                    packet.actions.PushBack(action);
                    state = after;
                    return true;
                };

                for (const u32 predecessorPhysicalIndex : packet.aliasPredecessorPhysicalIndices)
                {
                    const PhysicalResourceId predecessor{predecessorPhysicalIndex, generation.id.generation};
                    if (!ensurePhysical(predecessor))
                        return false;
                    const PhysicalScopeState& tracked = history[predecessorPhysicalIndex];
                    for (u32 cell = 0; cell < tracked.cellCount; ++cell)
                        if (cells[tracked.cellOffset + cell] != rhi::ResourceState::Common)
                            return reject(RenderFlowResourceFailureCode::UnsupportedCapability,
                                          "aliased predecessor close-time restoration must remain Common before activation", &packet);
                }

                for (CompiledStep& step : packet.steps)
                {
                    if (step.logicalAllocation >= generation.allocations.Size())
                        return reject(RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "compiled step lost its logical allocation", &packet, step.use);
                    const PhysicalResourceId physical = generation.allocations[step.logicalAllocation].physical;
                    if (!physical.IsValid() || physical.index >= generation.physicalBindings.Size())
                        return reject(RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "compiled step lost its physical allocation", &packet, step.use);
                    if (step.kind != CompiledResourceStepKind::UseEnd)
                    {
                        step.physical = physical;
                        if (!ensurePhysical(physical))
                            return false;
                        const PhysicalBindingRecord& binding = generation.physicalBindings[physical.index];
                        PhysicalScopeState& tracked = history[physical.index];
                        if (step.aliasPredecessorCount != 0 && binding.texture.IsValid() &&
                            HasAny(binding.textureUsage, rhi::TextureUsage::RenderTarget | rhi::TextureUsage::DepthStencil))
                        {
                            const rhi::ResourceState discard = packet.queue == rhi::QueueType::Graphics
                                                                   ? (HasAny(binding.textureUsage, rhi::TextureUsage::RenderTarget) ? rhi::ResourceState::RenderTarget : rhi::ResourceState::DepthWrite)
                                                                   : rhi::ResourceState::UnorderedAccess;
                            for (u32 cell = 0; cell < tracked.cellCount; ++cell)
                                cells[tracked.cellOffset + cell] = discard;
                        }

                        step.beforeActionOffset = packet.actions.Size();
                        const u32 transitionStart = packet.actions.Size();
                        bool requiresUavBarrier = false;
                        if (step.kind == CompiledResourceStepKind::TextureUseBegin)
                        {
                            const u32 firstMip = step.texture.subresources.firstMip;
                            const u32 firstSlice = step.texture.subresources.firstSlice;
                            const u32 mipCount = step.texture.subresources.mipCount == 0xffffu ? binding.textureMipCount - firstMip : step.texture.subresources.mipCount;
                            const u32 sliceCount = step.texture.subresources.sliceCount == 0xffffu ? binding.textureArraySize - firstSlice : step.texture.subresources.sliceCount;
                            if (firstMip >= binding.textureMipCount || firstSlice >= binding.textureArraySize || mipCount == 0 || sliceCount == 0 ||
                                mipCount > binding.textureMipCount - firstMip || sliceCount > binding.textureArraySize - firstSlice)
                                return reject(RenderFlowResourceFailureCode::BackendContractViolation, "texture UAV history has an invalid physical subresource range", &packet, step.use);
                            const bool currentWrites = AccessWrites(step.texture.access) || step.nativeHasClear;
                            if (step.texture.requiredState == rhi::ResourceState::UnorderedAccess)
                                for (u32 slice = firstSlice; slice < firstSlice + sliceCount; ++slice)
                                    for (u32 mip = firstMip; mip < firstMip + mipCount; ++mip)
                                    {
                                        const u32 cell = tracked.cellOffset + slice * binding.textureMipCount + mip;
                                        const u8 prior = accessCells[cell];
                                        if (cells[cell] == rhi::ResourceState::UnorderedAccess && (prior & physicalAccessSeen) != 0 && (prior & physicalAccessUav) != 0 &&
                                            ((prior & physicalAccessWrite) != 0 || currentWrites))
                                            requiresUavBarrier = true;
                                    }
                            if (!transitionTexture(step, physical, step.texture.subresources, step.texture.requiredState, transitionStart))
                                return false;
                            if (!FinalizeTextureTransitionBlock(packet.actions, transitionStart, resolveScratch.textureTransitionRuns))
                                return reject(RenderFlowResourceFailureCode::BackendContractViolation, "texture transition block is not self-contained", &packet, step.use);
                            if (!chargeActions(packet.actions.Size() - transitionStart))
                                return reject(RenderFlowResourceFailureCode::CapacityExceeded, "texture transitions exceed the action capacity", &packet, step.use);
                        }
                        else if (step.kind == CompiledResourceStepKind::BufferUseBegin)
                        {
                            const u8 prior = accessCells[tracked.cellOffset];
                            const bool currentWrites = AccessWrites(step.buffer.access) || step.nativeHasClear;
                            requiresUavBarrier = step.buffer.requiredState == rhi::ResourceState::UnorderedAccess &&
                                                 cells[tracked.cellOffset] == rhi::ResourceState::UnorderedAccess && (prior & physicalAccessSeen) != 0 &&
                                                 (prior & physicalAccessUav) != 0 && ((prior & physicalAccessWrite) != 0 || currentWrites);
                            if (!transitionBuffer(step, physical, step.buffer.requiredState))
                                return false;
                        }
                        else
                        {
                            return reject(RenderFlowResourceFailureCode::BackendContractViolation, "native state lowerer encountered an unknown begin-step kind", &packet, step.use);
                        }

                        if (requiresUavBarrier)
                        {
                            if (!chargeActions(1))
                                return reject(RenderFlowResourceFailureCode::CapacityExceeded, "UAV barriers exceed the action capacity", &packet, step.use);
                            if (step.kind == CompiledResourceStepKind::TextureUseBegin)
                            {
                                for (u32 cell = 0; cell < tracked.cellCount; ++cell)
                                    if (cells[tracked.cellOffset + cell] != rhi::ResourceState::UnorderedAccess)
                                        return reject(RenderFlowResourceFailureCode::UnsupportedCapability,
                                                      "whole-resource texture UAV barrier requires every physical subresource in UAV state", &packet, step.use);
                            }
                            CompiledResourceAction action;
                            action.kind = step.kind == CompiledResourceStepKind::TextureUseBegin ? CompiledResourceActionKind::TextureUavBarrier
                                                                                                : CompiledResourceActionKind::BufferUavBarrier;
                            action.logicalAllocation = step.logicalAllocation;
                            action.physical = physical;
                            packet.actions.PushBack(action);
                        }
                        if (step.kind == CompiledResourceStepKind::TextureUseBegin)
                        {
                            const u32 firstMip = step.texture.subresources.firstMip;
                            const u32 firstSlice = step.texture.subresources.firstSlice;
                            const u32 mipCount = step.texture.subresources.mipCount == 0xffffu ? binding.textureMipCount - firstMip : step.texture.subresources.mipCount;
                            const u32 sliceCount = step.texture.subresources.sliceCount == 0xffffu ? binding.textureArraySize - firstSlice : step.texture.subresources.sliceCount;
                            const u8 current = physicalAccessSeen | (step.texture.requiredState == rhi::ResourceState::UnorderedAccess ? physicalAccessUav : u8{0}) |
                                               ((AccessWrites(step.texture.access) || step.nativeHasClear) ? physicalAccessWrite : u8{0});
                            for (u32 slice = firstSlice; slice < firstSlice + sliceCount; ++slice)
                                for (u32 mip = firstMip; mip < firstMip + mipCount; ++mip)
                                    accessCells[tracked.cellOffset + slice * binding.textureMipCount + mip] = current;
                        }
                        else
                        {
                            accessCells[tracked.cellOffset] = physicalAccessSeen | (step.buffer.requiredState == rhi::ResourceState::UnorderedAccess ? physicalAccessUav : u8{0}) |
                                                              ((AccessWrites(step.buffer.access) || step.nativeHasClear) ? physicalAccessWrite : u8{0});
                        }
                        if (step.nativeHasClear)
                        {
                            if (!chargeActions(1))
                                return reject(RenderFlowResourceFailureCode::CapacityExceeded, "resource clears exceed the action capacity", &packet, step.use);
                            CompiledResourceAction action;
                            action.kind = step.nativeClearKind;
                            action.logicalAllocation = step.logicalAllocation;
                            action.physical = physical;
                            if (step.kind == CompiledResourceStepKind::TextureUseBegin)
                            {
                                action.textureSubresources = step.texture.subresources;
                                action.textureClearValue = step.nativeTextureClearValue;
                            }
                            else
                            {
                                action.bufferClearValue = step.nativeBufferClearValue;
                            }
                            packet.actions.PushBack(action);
                        }
                        step.beforeActionCount = packet.actions.Size() - step.beforeActionOffset;
                        step.afterActionOffset = packet.actions.Size();
                        step.afterActionCount = 0;
                        continue;
                    }

                    step.afterActionOffset = packet.actions.Size();
                    step.physical = physical;
                    if (step.terminalState != rhi::ResourceState::Unknown)
                    {
                        if (step.terminalPresent)
                        {
                            if (!ensurePhysical(physical))
                                return false;
                            const PhysicalBindingRecord& binding = generation.physicalBindings[physical.index];
                            PhysicalScopeState& tracked = history[physical.index];
                            if (!binding.texture.IsValid() || binding.buffer.IsValid() || tracked.cellCount != 1 || step.terminalState != rhi::ResourceState::Present)
                                return reject(RenderFlowResourceFailureCode::BackendContractViolation, "presentation terminal intent is not a single-subresource texture", &packet, step.use);
                            if (!chargeActions(1))
                                return reject(RenderFlowResourceFailureCode::CapacityExceeded, "presentation transition exceeds the action capacity", &packet, step.use);
                            CompiledResourceAction action;
                            action.kind = CompiledResourceActionKind::SwapChainPresentTransition;
                            action.logicalAllocation = step.logicalAllocation;
                            action.physical = physical;
                            action.before = cells[tracked.cellOffset];
                            action.after = rhi::ResourceState::Present;
                            packet.actions.PushBack(action);
                            cells[tracked.cellOffset] = rhi::ResourceState::Present;
                        }
                        else if (generation.physicalBindings[physical.index].texture.IsValid())
                        {
                            const PhysicalBindingRecord& binding = generation.physicalBindings[physical.index];
                            const u32 transitionStart = packet.actions.Size();
                            if (!transitionTexture(step, physical, {0, binding.textureMipCount, 0, binding.textureArraySize}, step.terminalState, transitionStart))
                                return false;
                            if (!FinalizeTextureTransitionBlock(packet.actions, transitionStart, resolveScratch.textureTransitionRuns))
                                return reject(RenderFlowResourceFailureCode::BackendContractViolation, "terminal texture transition block is not self-contained", &packet, step.use);
                            if (!chargeActions(packet.actions.Size() - transitionStart))
                                return reject(RenderFlowResourceFailureCode::CapacityExceeded, "terminal texture transitions exceed the action capacity", &packet, step.use);
                        }
                        else if (!transitionBuffer(step, physical, step.terminalState))
                        {
                            return false;
                        }
                    }
                    if (step.finalizeForAlias)
                    {
                        if (!ensurePhysical(physical))
                            return false;
                        PhysicalScopeState& tracked = history[physical.index];
                        for (u32 cell = 0; cell < tracked.cellCount; ++cell)
                            cells[tracked.cellOffset + cell] = rhi::ResourceState::Common;
                    }
                    step.afterActionCount = packet.actions.Size() - step.afterActionOffset;
                }

                for (const u32 physicalIndex : packetPhysicals)
                {
                    if (physicalIndex >= historyCount || physicalIndex >= generation.physicalBindings.Size())
                        return reject(RenderFlowResourceFailureCode::BackendContractViolation, "packet physical-state list is stale", &packet);
                    PhysicalScopeState& tracked = history[physicalIndex];
                    const PhysicalBindingRecord& binding = generation.physicalBindings[physicalIndex];
                    if (!binding.explicitState)
                        continue;
                    if (binding.buffer.IsValid() && cells[tracked.cellOffset] != rhi::ResourceState::Common)
                    {
                        if (!chargeActions(1))
                            return reject(RenderFlowResourceFailureCode::CapacityExceeded, "buffer packet-exit transitions exceed the action capacity", &packet);
                        CompiledResourceAction action;
                        action.kind = CompiledResourceActionKind::BufferTransition;
                        action.physical = {physicalIndex, generation.id.generation};
                        action.before = cells[tracked.cellOffset];
                        action.after = rhi::ResourceState::Common;
                        packet.exitActions.PushBack(action);
                        cells[tracked.cellOffset] = rhi::ResourceState::Common;
                    }
                    tracked.lastPacket = packetIndex;
                }

                if (packet.statePredecessorPackets.Size() > 1)
                {
                    std::sort(packet.statePredecessorPackets.Begin(), packet.statePredecessorPackets.End());
                    const auto end = std::unique(packet.statePredecessorPackets.Begin(), packet.statePredecessorPackets.End());
                    packet.statePredecessorPackets.Resize(static_cast<u32>(end - packet.statePredecessorPackets.Begin()));
                }
                if (packet.entryStates.Size() > 1)
                {
                    std::sort(packet.entryStates.Begin(), packet.entryStates.End(), [](const rhi::CommandListEntryState& left, const rhi::CommandListEntryState& right) noexcept
                              {
                                  if (left.resource.value != right.resource.value)
                                      return left.resource.value < right.resource.value;
                                  if (left.subresource.arraySlice != right.subresource.arraySlice)
                                      return left.subresource.arraySlice < right.subresource.arraySlice;
                                  return left.subresource.mipLevel < right.subresource.mipLevel;
                              });
                    for (u32 index = 1; index < packet.entryStates.Size(); ++index)
                        if (packet.entryStates[index - 1].resource == packet.entryStates[index].resource &&
                            packet.entryStates[index - 1].subresource.arraySlice == packet.entryStates[index].subresource.arraySlice &&
                            packet.entryStates[index - 1].subresource.mipLevel == packet.entryStates[index].subresource.mipLevel)
                            return reject(RenderFlowResourceFailureCode::BackendContractViolation, "packet entry-state list contains a duplicate native subresource", &packet);
                }
            }

            for (u32 physicalIndex = 0; physicalIndex < historyCount; ++physicalIndex)
            {
                const PhysicalBindingRecord& binding = generation.physicalBindings[physicalIndex];
                if (!binding.explicitState)
                    continue;
                PhysicalScopeState& tracked = history[physicalIndex];
                const bool buffer = binding.buffer.IsValid();
                if (tracked.lastPacket >= generation.packets.Size() || (!buffer && tracked.terminal == rhi::ResourceState::Unknown))
                    return reject(RenderFlowResourceFailureCode::BackendContractViolation, "explicit physical resource has no terminal packet/state");
                CompiledPacket& packet = generation.packets[tracked.lastPacket];
                const u32 exportIndex = physicalIndex < resolveScratch.physicalAssignmentOrder.Size()
                                            ? resolveScratch.physicalAssignmentOrder[physicalIndex]
                                            : InvalidRenderFlowResourceIndex;
                if (exportIndex != InvalidRenderFlowResourceIndex &&
                    (exportIndex >= generation.pendingExports.Size() || generation.pendingExports[exportIndex].terminalPacket != tracked.lastPacket))
                    return reject(RenderFlowResourceFailureCode::BackendContractViolation, "explicit export terminal fence does not cover its final physical packet", &packet);
                if (buffer)
                    continue;
                const u32 transitionStart = packet.exitActions.Size();
                for (u32 cell = 0; cell < tracked.cellCount; ++cell)
                {
                    rhi::ResourceState& state = cells[tracked.cellOffset + cell];
                    if (state == tracked.terminal)
                        continue;
                    CompiledResourceAction action;
                    action.kind = CompiledResourceActionKind::TextureTransition;
                    action.physical = {physicalIndex, generation.id.generation};
                    action.before = state;
                    action.after = tracked.terminal;
                    action.textureSubresources = {static_cast<u16>(cell % binding.textureMipCount), 1,
                                                  static_cast<u16>(cell / binding.textureMipCount), 1};
                    if (!AppendOrMergeTextureTransition(packet.exitActions, transitionStart, action, false))
                        return reject(RenderFlowResourceFailureCode::CapacityExceeded, "explicit terminal texture transition storage allocation failed", &packet);
                    state = tracked.terminal;
                }
                if (!FinalizeTextureTransitionBlock(packet.exitActions, transitionStart, resolveScratch.textureTransitionRuns))
                    return reject(RenderFlowResourceFailureCode::BackendContractViolation, "explicit terminal texture transition block is not self-contained", &packet);
                if (!chargeActions(packet.exitActions.Size() - transitionStart))
                    return reject(RenderFlowResourceFailureCode::CapacityExceeded, "explicit terminal texture transitions exceed the action capacity", &packet);
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

    void ReleasePersistentDedicatedPlan(RenderFlowResourceAllocator::Impl& allocator, const bool releaseLeases) noexcept
    {
        PersistentDedicatedPhysicalPlan& plan = allocator.persistentDedicatedPlan;
        if (releaseLeases)
            for (PersistentDedicatedPlanLaneRuntime& runtime : plan.laneRuntime)
                for (const DedicatedResourceLease& lease : runtime.leases)
                    allocator.dedicatedPool.ReleaseLease(lease);
        plan.Clear();
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
        return ExtentEqual(left.extent, right.extent) && left.dimension == right.dimension && left.format == right.format && left.mipCount == right.mipCount && left.arraySize == right.arraySize &&
               left.sampleCount == right.sampleCount && left.usage == right.usage && left.initialState == right.initialState && left.virtualResource == right.virtualResource && left.keepInitialState == right.keepInitialState;
    }

    bool PhysicalBufferDescEqual(const rhi::BufferDesc& left, const rhi::BufferDesc& right) noexcept
    {
        return left.size == right.size && left.structureStride == right.structureStride && left.format == right.format && left.usage == right.usage && left.initialState == right.initialState &&
               left.memoryType == right.memoryType && left.virtualResource == right.virtualResource && left.keepInitialState == right.keepInitialState;
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
#if VG_ENABLE_PROFILING
        static profiling::SourceLocation setupProfileLocation{"RenderFlow/Resolve/Setup", __FUNCTION__, __FILE__, __LINE__};
        static profiling::SourceLocation compileProfileLocation{"RenderFlow/Resolve/CompileOperations", __FUNCTION__, __FILE__, __LINE__};
        static profiling::SourceLocation validateProfileLocation{"RenderFlow/Resolve/ValidateLifetimes", __FUNCTION__, __FILE__, __LINE__};
        static profiling::SourceLocation physicalProfileLocation{"RenderFlow/Resolve/PhysicalAssignment", __FUNCTION__, __FILE__, __LINE__};
        static profiling::SourceLocation finalizeBindingsProfileLocation{"RenderFlow/Resolve/FinalizeBindings", __FUNCTION__, __FILE__, __LINE__};
        static profiling::SourceLocation finalizeStatesProfileLocation{"RenderFlow/Resolve/FinalizeStates", __FUNCTION__, __FILE__, __LINE__};
        static profiling::SourceLocation publishProfileLocation{"RenderFlow/Resolve/PublishCleanup", __FUNCTION__, __FILE__, __LINE__};
        std::optional<profiling::Scope> resolveProfilePhase;
        resolveProfilePhase.emplace(setupProfileLocation);
#endif
        if (allocator.state != RenderFlowResourceSessionState::CandidatesSealed)
            return Fail(failure, RenderFlowResourceFailureCode::InvalidPhase, allocator.state, "candidate plan is not sealed");

        allocator.state = RenderFlowResourceSessionState::Resolving;
        allocator.stats.state = allocator.state;
        ExecutionGenerationRef::Impl* const scratch = AllocateGeneration();
        if (scratch == nullptr)
            return FailResolve(allocator, nullptr, failure, RenderFlowResourceFailureCode::CapacityExceeded, "execution generation metadata allocation failed");
        scratch->id = {0, NextGeneration(allocator.executionGeneration)};
        if (allocator.sealedWriters.Size() > allocator.config.maximumExecutionPackets)
            return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::CapacityExceeded, "execution packet count exceeds allocator capacity");

        ResolveScratch& resolveScratch = allocator.resolveScratch;
        resolveScratch.ResetFrame();
        const u32 packetCount = allocator.sealedWriters.Size();
        containers::DynamicArray<u32>& batchResourceSlots = resolveScratch.batchResourceSlots;
        containers::DynamicArray<ResolvedTextureViewRecord>& batchTextureViews = resolveScratch.batchTextureViews;
        containers::DynamicArray<ResolvedBufferViewRecord>& batchBufferViews = resolveScratch.batchBufferViews;
        containers::DynamicArray<LogicalSlot>& slots = resolveScratch.slots;
        containers::DynamicArray<ActiveUseRecord>& uses = resolveScratch.uses;
        containers::DynamicArray<OpenScopeRecord>& scopes = resolveScratch.scopes;
        containers::DynamicArray<NamedScopeState>& namedScopes = resolveScratch.namedScopes;
        containers::DynamicArray<AllocationLogicalState>& allocationStates = resolveScratch.allocationStates;
        containers::DynamicArray<TextureSubresourceLogicalState>& textureSubresourceStates = resolveScratch.textureSubresourceStates;
        ResolveScratchHashMap<LogicalNameKey, u32>& namedSlots = resolveScratch.namedSlots;
        ResolveScratchHashSet& commandScopeIds = resolveScratch.identityIds;
        containers::DynamicArray<u32>& crossQueueProducerScopes = resolveScratch.crossQueueProducerScopes;
        ResolveScratchHashMap<u64, u32>& openScopeIndices = resolveScratch.openScopeIndices;
        containers::DynamicArray<u8>& importedInstalled = resolveScratch.importedInstalled;
        // Multi-purpose dense metadata index. During CompileOperations the fixed
        // prefix maps reserved export slot -> pending export while first lifetime
        // touches append allocation ids after that prefix in exact plan order.
        // Physical assignment consumes only that suffix; finalization then reuses
        // the same backing for physical -> export.
        containers::DynamicArray<u32>& physicalAssignmentOrder = resolveScratch.physicalAssignmentOrder;
        const auto batchAt = [&](const u32 batchIndex) noexcept -> const CandidateWriterBatch&
        {
            return allocator.writerReservations[allocator.sealedWriters[batchIndex].groupIndex].batch;
        };

        u64 compiledOperationCount = 0;
        u64 compiledResourceActionCount = 0;
        const bool structuralPlanEligible = !allocator.config.allowLogicalOnlyValidation && !allocator.activePolicy.enablePlacedResources;
        const u64 structuralSignature = structuralPlanEligible ? ComputeStructuralSignature(allocator) : 0;
        bool structuralPlanHit = false;
        bool structuralCandidateReady = false;
        if (structuralPlanEligible && allocator.structuralPlan.valid && structuralSignature != 0 &&
            structuralSignature == allocator.structuralPlan.signature &&
            StructuralPlanMatchesCurrent(allocator.structuralPlan, allocator, resolveScratch))
        {
            if (!InstantiateStructuralPlan(allocator.structuralPlan, allocator, *scratch, resolveScratch, failure))
                return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::BackendContractViolation,
                                   "cached structural resource plan could not instantiate a fresh execution generation");
            structuralPlanHit = true;
            ++allocator.stats.structuralPlanHits;
        }
        else if (structuralPlanEligible)
        {
            ++allocator.stats.structuralPlanMisses;
        }

        if (!structuralPlanHit)
        {
        importedInstalled.Resize(allocator.retainedImports.Size(), u8{0});
        const SealedPlanningSummary& sealedPlanning = allocator.sealedPlanning;
        batchResourceSlots.Resize(sealedPlanning.resourceSlotCount, InvalidRenderFlowResourceIndex);
        batchTextureViews.Resize(sealedPlanning.textureViewCount);
        batchBufferViews.Resize(sealedPlanning.bufferViewCount);
        scratch->allocations.Reserve(sealedPlanning.declarationCount);
        slots.Reserve(sealedPlanning.declarationCount);
        allocationStates.Reserve(sealedPlanning.declarationCount);
        namedScopes.Reserve(sealedPlanning.declarationCount);
        uses.Reserve(sealedPlanning.useBeginCount);
        scopes.Reserve(sealedPlanning.explicitScopeOpenCount);
        openScopeIndices.Reserve(sealedPlanning.explicitScopeOpenCount);
        scratch->packets.Reserve(packetCount);
        scratch->packetByNode.Reserve(packetCount);
        scratch->commandScopes.Reserve(packetCount);
        scratch->pendingExports.Reserve(sealedPlanning.exportCount);
        const u64 physicalAssignmentCapacity = static_cast<u64>(allocator.reservedExportSlots) + sealedPlanning.declarationCount;
        if (physicalAssignmentCapacity > InvalidRenderFlowResourceIndex)
            return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::CapacityExceeded, "physical assignment metadata exceeds the allocator index space");
        physicalAssignmentOrder.Reserve(static_cast<u32>(physicalAssignmentCapacity));
        physicalAssignmentOrder.Resize(allocator.reservedExportSlots, InvalidRenderFlowResourceIndex);

        // RED pays once to linearize temporal ordering before lifetime work.
        // The sealed writer stream is already strictly ordered by flow group, so
        // merge it directly with the queue-request stream. This keeps packet,
        // batch, and dense command-scope indices identical, lowers dependencies
        // as their consumer packet is encountered, and emits the one watermark
        // that consumer queue can actually use. No reconciliation maps or later
        // prefix pass are necessary.
        bool asyncComputeOpen = false;
        u32 batchCursor = 0;
        u32 queueCursor = 0;
        u32 previousGroupIndex = InvalidRenderFlowResourceIndex;
        u32 previousPacket = InvalidRenderFlowResourceIndex;
        rhi::CommandListSyncType pendingSync = rhi::CommandListSyncType::None;
        u32 pendingProducerPacket = InvalidRenderFlowResourceIndex;
        u32 forkProducerPacket = InvalidRenderFlowResourceIndex;
        u32 joinProducerPacket = InvalidRenderFlowResourceIndex;
        bool commandScopeIdsActive = false;
        bool crossQueueFactsActive = false;
        while (batchCursor < allocator.sealedWriters.Size() || queueCursor < allocator.sealedQueueGroups.Size())
        {
            const u32 batchGroup = batchCursor < allocator.sealedWriters.Size() ? allocator.sealedWriters[batchCursor].groupIndex : InvalidRenderFlowResourceIndex;
            const u32 queueGroup = queueCursor < allocator.sealedQueueGroups.Size() ? allocator.sealedQueueGroups[queueCursor] : InvalidRenderFlowResourceIndex;
            const u32 groupIndex = batchGroup == InvalidRenderFlowResourceIndex ? queueGroup
                                   : queueGroup == InvalidRenderFlowResourceIndex ? batchGroup
                                                                                : std::min(batchGroup, queueGroup);
            if (groupIndex == InvalidRenderFlowResourceIndex || groupIndex >= allocator.preparedPlanningGroupCount ||
                (previousGroupIndex != InvalidRenderFlowResourceIndex && groupIndex <= previousGroupIndex))
                return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::BackendContractViolation, "sealed planning stream is not strictly ordered by GPU flow group");
            previousGroupIndex = groupIndex;

            const bool hasBatch = batchGroup == groupIndex;
            const bool hasQueue = queueGroup == groupIndex;
            if (hasBatch && batchAt(batchCursor).flowGroup.value != groupIndex)
                return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::BackendContractViolation, "sealed writer ordering diverged from its flow-group stream", batchAt(batchCursor).node);
            const QueueRequestGroup* group = nullptr;
            if (hasQueue)
            {
                group = &allocator.queueRequestGroups[groupIndex];
                if (group->count == 0)
                    return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::BackendContractViolation, "sealed queue-group stream references an empty request group");
                ++queueCursor;
            }
            rhi::QueueType packetQueue = rhi::QueueType::Graphics;
            if (group != nullptr)
            {
                const QueueRequestRecord& request = group->requests[0];
                if (!request.flowGroup.IsValid() || request.flowGroup.value != groupIndex)
                    return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::QueueOrCommandScopeMismatch, "allocator request stream contains an invalid GPU flow group");
                if (request.kind == QueueRequestKind::Sync)
                {
                    if (group->count != 1)
                        return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::QueueOrCommandScopeMismatch, "one GPU flow group cannot mix synchronization and command-queue requests");
                    if (request.sync != rhi::CommandListSyncType::None && request.sync != rhi::CommandListSyncType::ForkAsyncCompute && request.sync != rhi::CommandListSyncType::JoinAsyncCompute)
                        return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::UnsupportedCapability, "queue synchronization request uses an unsupported primitive");
                    if (hasBatch)
                        return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::QueueOrCommandScopeMismatch, "queue synchronization request cannot own a resource-recording packet",
                                           batchAt(batchCursor).node);
                    if (previousPacket == InvalidRenderFlowResourceIndex)
                        return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::QueueOrCommandScopeMismatch, "queue synchronization request has no preceding command scope");
                    if (request.sync != rhi::CommandListSyncType::None)
                    {
                        if (pendingSync != rhi::CommandListSyncType::None)
                            return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::QueueOrCommandScopeMismatch,
                                               "multiple cross-queue synchronization requests cannot share one consumer command scope");
                        pendingSync = request.sync;
                        pendingProducerPacket = previousPacket;
                    }
                    continue;
                }
                if (group->count != 2 || group->requests[0].kind != QueueRequestKind::Begin || group->requests[1].kind != QueueRequestKind::End)
                    return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::QueueOrCommandScopeMismatch, "queue begin and end requests must be unique and balanced");
                const QueueRequestRecord& end = group->requests[1];
                if (!ValidQueue(request.queue) || end.queue != request.queue)
                    return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::QueueOrCommandScopeMismatch, "one GPU flow group cannot contain duplicate or mixed queue requests");
                if (!hasBatch)
                    return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::QueueOrCommandScopeMismatch, "queue begin/end requests do not belong to a resource-recording packet");
                packetQueue = request.queue;
            }
            if (!hasBatch)
                continue;

            const CandidateWriterBatch& batch = batchAt(batchCursor);
            const u32 packetIndex = scratch->packets.Size();
            if (packetIndex != batchCursor || packetIndex != scratch->commandScopes.Size())
                return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::BackendContractViolation, "execution packet, sealed batch, and command-scope dense indices diverged", batch.node);
            if (!scratch->packetByNode.Insert(batch.node.value, packetIndex).IsSuccessful())
                return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "execution generation contains duplicate node packets", batch.node);
            CompiledPacket& packet = scratch->packets.EmplaceBack();
            packet.node = batch.node;
            packet.flowGroup = batch.flowGroup;
            packet.commandScope = batch.commandScope;
            packet.queue = packetQueue;
            packet.steps.Reserve(batch.stepCount);
            packet.actions.Reserve(batch.uses.Size());
            packet.decisions.Reserve(batch.decisionCount);
            // Production render nodes use CommandScopeId{nodeId}. packetByNode
            // already proves uniqueness for that canonical path, so avoid a
            // second hash/probe stream entirely. If a caller uses independent
            // scope identities, lazily materialize the exact uniqueness set and
            // backfill the already-proven canonical prefix once.
            if (!commandScopeIdsActive && batch.commandScope.value != batch.node.value)
            {
                commandScopeIds.Reserve(packetCount);
                for (const CompiledCommandScope& priorScope : scratch->commandScopes)
                    if (!commandScopeIds.Insert(priorScope.scope.value))
                        return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::BackendContractViolation,
                                           "canonical command-scope identities lost uniqueness before fallback indexing", batch.node);
                commandScopeIdsActive = true;
            }
            if (commandScopeIdsActive && !commandScopeIds.Insert(batch.commandScope.value))
                return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::QueueOrCommandScopeMismatch, "allocator request stream contains duplicate command-scope identities", batch.node);
            scratch->commandScopes.PushBack({batch.commandScope, packet.queue, batch.flowGroup.value});

            if (pendingSync != rhi::CommandListSyncType::None)
            {
                if (pendingProducerPacket >= packetIndex || pendingProducerPacket >= scratch->commandScopes.Size())
                    return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::BackendContractViolation, "compiled queue dependency is not forward ordered");
                const CompiledPacket& producer = scratch->packets[pendingProducerPacket];
                if (pendingSync == rhi::CommandListSyncType::ForkAsyncCompute)
                {
                    if (asyncComputeOpen || producer.queue != rhi::QueueType::Graphics || packet.queue != rhi::QueueType::Compute)
                        return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::QueueOrCommandScopeMismatch, "async-compute fork must be a non-nested Graphics-to-Compute boundary");
                    asyncComputeOpen = true;
                    forkProducerPacket = pendingProducerPacket;
                }
                else
                {
                    if (!asyncComputeOpen || producer.queue != rhi::QueueType::Compute || packet.queue != rhi::QueueType::Graphics)
                        return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::QueueOrCommandScopeMismatch, "async-compute join must close an open Compute-to-Graphics boundary");
                    asyncComputeOpen = false;
                    joinProducerPacket = pendingProducerPacket;
                }
                if (!crossQueueFactsActive)
                {
                    crossQueueProducerScopes.Reserve(packetCount);
                    crossQueueProducerScopes.Resize(packetIndex, InvalidRenderFlowResourceIndex);
                    crossQueueFactsActive = true;
                }
                scratch->queueDependencies.PushBack({producer.commandScope, packet.commandScope, pendingSync});
                pendingSync = rhi::CommandListSyncType::None;
                pendingProducerPacket = InvalidRenderFlowResourceIndex;
            }

            if (crossQueueFactsActive)
                crossQueueProducerScopes.PushBack(packet.queue == rhi::QueueType::Compute ? forkProducerPacket
                                                   : packet.queue == rhi::QueueType::Graphics ? joinProducerPacket
                                                                                              : InvalidRenderFlowResourceIndex);
            previousPacket = packetIndex;
            ++batchCursor;
        }
        if (batchCursor != allocator.sealedWriters.Size() || queueCursor != allocator.sealedQueueGroups.Size())
            return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::BackendContractViolation, "sealed planning stream was not consumed completely");
        if (pendingSync != rhi::CommandListSyncType::None)
            return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::QueueOrCommandScopeMismatch, "cross-queue synchronization request has no following command scope");
        if (asyncComputeOpen)
            return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::QueueOrCommandScopeMismatch, "async-compute fork/join requests must be balanced");

        const auto getSlot = [&](const u32 batchIndex, const LogicalResourceId resource, u32& result) noexcept -> bool
        {
            result = InvalidRenderFlowResourceIndex;
            const CandidateWriterBatch& batch = batchAt(batchIndex);
            if (!resource.IsValid() || resource.flowGroup != batch.flowGroup || resource.generation != batch.sessionGeneration || resource.index >= batch.resourceCount)
                return false;
            const SealedWriterRecord& sealed = allocator.sealedWriters[batchIndex];
            u32& mappedSlot = batchResourceSlots[sealed.resourceSlotOffset + resource.index];
            if (mappedSlot != InvalidRenderFlowResourceIndex)
            {
                result = mappedSlot;
                return true;
            }
            const CandidateResource& candidate = batch.resources[resource.index];
            if (candidate.identity == CandidateIdentityKind::Named)
            {
                // The key views candidate-owned name bytes. Sealed candidate
                // batches are immutable until this resolve-local map is gone.
                const LogicalNameKey key{candidate.flowSpace, containers::StringView(candidate.name)};
                u32 index = InvalidRenderFlowResourceIndex;
                if (namedSlots.FindPrehashed(key, candidate.logicalNameHash, index))
                {
                    mappedSlot = index;
                    result = index;
                    return true;
                }
            }
            const u32 slotIndex = resolveScratch.slotCount++;
            if (slotIndex == slots.Size())
                slots.EmplaceBack();
            LogicalSlot& slot = slots[slotIndex];
            slot.Reset();
            mappedSlot = slotIndex;
            result = slotIndex;
            if (candidate.identity == CandidateIdentityKind::Named &&
                !namedSlots.InsertPrehashed({candidate.flowSpace, containers::StringView(candidate.name)}, candidate.logicalNameHash, result))
                return false;
            return true;
        };

        const auto failOperation = [&](const OperationVisit& mergedOperation, const RenderFlowResourceFailureCode code, const char* const message, const ResourceUseId use = {}) noexcept -> bool
        {
            const CandidateWriterBatch& batch = batchAt(mergedOperation.batch);
            return FailResolve(allocator, scratch, failure, code, message, batch.node, mergedOperation.position, use);
        };

#if VG_ENABLE_PROFILING
        resolveProfilePhase.reset();
        resolveProfilePhase.emplace(compileProfileLocation);
#endif
        const auto appendProvisionalTextureTransition = [&](containers::DynamicArray<CompiledResourceAction>& actions, const u32 blockStart,
                                                            const CompiledResourceAction& action) noexcept
        {
            // Capacity is charged only after a complete transition block has
            // reached its final rectangle cardinality. A 2-D rectangle can
            // transiently need one action per row before the last cell lets
            // those rows fold vertically.
            return AppendOrMergeTextureTransition(actions, blockStart, action, !allocator.config.allowLogicalOnlyValidation);
        };
        const auto chargeCompiledActionBlock = [&](const u32 count) noexcept
        {
            if (compiledResourceActionCount > allocator.config.maximumCompiledResourceActions ||
                static_cast<u64>(count) > static_cast<u64>(allocator.config.maximumCompiledResourceActions) - compiledResourceActionCount)
                return false;
            compiledResourceActionCount += count;
            return true;
        };
#if VG_ENABLE_PROFILING && VG_COMPILE_OPERATIONS_PROBE
        struct CompileOperationsProbe
        {
            u64 bufferUseBeginCount = 0;
            u64 sampledBufferUseBeginCount = 0;
            u64 sampledBufferUseBeginCycles = 0;
            u64 sampledLookupValidationCycles = 0;
            u64 sampledSetupCycles = 0;
            u64 sampledActiveScanCycles = 0;
            u64 sampledStateLoweringCycles = 0;
            u64 sampledPublicationCycles = 0;
        } compileOperationsProbe;
        constexpr u64 CompileOperationsProbeSampleMask = 63u;
        const u64 compileOperationsProbeSamplePhase = allocator.frameSerial & CompileOperationsProbeSampleMask;
#if defined(_MSC_VER)
        const u64 compileOperationsProbeFrameClockBegin = system::GetMonotonicTicks();
        const u64 compileOperationsProbeFrameCycleBegin = __rdtsc();
#endif
#endif
        for (u32 batchIndex = 0; batchIndex < allocator.sealedWriters.Size(); ++batchIndex)
        {
            const CandidateWriterBatch& batch = batchAt(batchIndex);
            const auto localResource = [&](const u32 index) noexcept -> LogicalResourceId
            {
                return {batch.flowGroup, index, batch.sessionGeneration};
            };
            const u32 packetIndex = batchIndex;
            CompiledPacket& packet = scratch->packets[packetIndex];
            const SealedWriterRecord& sealed = allocator.sealedWriters[batchIndex];
            packet.beginStepByUseOrdinal.Reserve(batch.uses.Size());
            packet.endStepByUseOrdinal.Resize(batch.uses.Size(), InvalidRenderFlowResourceIndex);
            const auto compileUseEnd = [&](const u32 candidateUseIndex, const u32 sequence) noexcept -> bool
            {
                const OperationVisit mergedEnd{batchIndex, candidateUseIndex, {batch.flowGroup, sequence}};
                const ResourceUseId use{batch.flowGroup, candidateUseIndex, batch.sessionGeneration};
                if (candidateUseIndex >= batch.uses.Size())
                    return failOperation(mergedEnd, RenderFlowResourceFailureCode::InvalidUseOrScope, "resource use end has an invalid dense identity", use);
                const u32 useIndex = sealed.useRecordOffset + candidateUseIndex;
                ActiveUseRecord* const active = useIndex < uses.Size() ? &uses[useIndex] : nullptr;
                if (active == nullptr || active->id != use || active->ended || active->packet != packetIndex)
                    return failOperation(mergedEnd, RenderFlowResourceFailureCode::InvalidUseOrScope, "resource use end is missing, duplicated, or owned by another packet", use);
                AllocationLogicalState& logicalState = allocationStates[active->allocation];
                if ((active->previousActive == InvalidRenderFlowResourceIndex && logicalState.firstActiveUse != useIndex) ||
                    (active->previousActive != InvalidRenderFlowResourceIndex && (active->previousActive >= uses.Size() || uses[active->previousActive].nextActive != useIndex)) ||
                    (active->nextActive != InvalidRenderFlowResourceIndex && (active->nextActive >= uses.Size() || uses[active->nextActive].previousActive != useIndex)))
                    return failOperation(mergedEnd, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "active resource-use chain is internally inconsistent", use);
                if (active->previousActive == InvalidRenderFlowResourceIndex)
                    logicalState.firstActiveUse = active->nextActive;
                else
                    uses[active->previousActive].nextActive = active->nextActive;
                if (active->nextActive != InvalidRenderFlowResourceIndex)
                    uses[active->nextActive].previousActive = active->previousActive;
                if (active->texture)
                {
                    const u32 resourceMipCount = scratch->allocations[active->allocation].desc.texture.active.mipCount;
                    for (u32 slice = active->textureSubresources.firstSlice; slice < static_cast<u32>(active->textureSubresources.firstSlice) + active->textureSubresources.sliceCount; ++slice)
                        for (u32 mip = active->textureSubresources.firstMip; mip < static_cast<u32>(active->textureSubresources.firstMip) + active->textureSubresources.mipCount; ++mip)
                        {
                            TextureSubresourceLogicalState& subresource = textureSubresourceStates[logicalState.textureOffset + slice * resourceMipCount + mip];
                            if (subresource.activeUseCount == 0)
                                return failOperation(mergedEnd, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "active texture-use summary is internally inconsistent", use);
                            --subresource.activeUseCount;
                        }
                }
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
                step.use = use;
                step.logicalAllocation = active->allocation;
                step.runtimeUseSlot = active->runtimeUseSlot;
                active->endStep = packet.steps.Size();
                active->endPosition = mergedEnd.position;
                if (use.ordinal >= packet.endStepByUseOrdinal.Size() || packet.endStepByUseOrdinal[use.ordinal] != InvalidRenderFlowResourceIndex ||
                    use.ordinal >= packet.beginStepByUseOrdinal.Size() || packet.beginStepByUseOrdinal[use.ordinal] != active->beginStep)
                    return failOperation(mergedEnd, RenderFlowResourceFailureCode::BackendContractViolation,
                                         "compiled resource-use end disagrees with its dense begin topology", use);
                packet.endStepByUseOrdinal[use.ordinal] = packet.steps.Size();
                packet.steps.PushBack(step);
                logicalState.terminalUseRecord = useIndex;
                TouchLifetime(scratch->allocations[active->allocation], active->allocation, mergedEnd.position, packetIndex, packet, physicalAssignmentOrder);
                return true;
            };

            u32 operationIndex = 0;
            u32 useEndIndex = 0;
            u32 expectedSequence = 0;
            while (operationIndex < batch.operations.Size() || useEndIndex < batch.useEndOrder.Size())
            {
                const u32 operationSequence = operationIndex < batch.operations.Size() ? batch.operations[operationIndex].sequence : InvalidRenderFlowResourceIndex;
                u32 endUseOrdinal = InvalidRenderFlowResourceIndex;
                u32 endSequence = InvalidRenderFlowResourceIndex;
                if (useEndIndex < batch.useEndOrder.Size())
                {
                    endUseOrdinal = batch.useEndOrder[useEndIndex];
                    if (endUseOrdinal >= batch.uses.Size())
                        return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::BackendContractViolation,
                                           "dense resource-use end stream references an invalid use", batch.node, {batch.flowGroup, expectedSequence});
                    endSequence = batch.uses[endUseOrdinal].endSequence;
                }
                const bool visitEnd = endSequence != InvalidRenderFlowResourceIndex &&
                                      (operationSequence == InvalidRenderFlowResourceIndex || endSequence < operationSequence);
                const u32 sequence = visitEnd ? endSequence : operationSequence;
                if (sequence != expectedSequence)
                    return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::BackendContractViolation,
                                       "planning semantic sequence is sparse, duplicated, or out of order", batch.node, {batch.flowGroup, sequence});
                ++expectedSequence;
                ++compiledOperationCount;
                if (visitEnd)
                {
                    if (!compileUseEnd(endUseOrdinal, sequence))
                        return false;
                    ++useEndIndex;
                    continue;
                }

                const CandidateOperation& operation = batch.operations[operationIndex];
                const OperationVisit mergedOperation{batchIndex, operationIndex, {batch.flowGroup, operation.sequence}};
                ++operationIndex;

                u32 slotIndex = InvalidRenderFlowResourceIndex;
                switch (operation.kind)
                {
                case CandidateOperationKind::DeclareTexture:
                case CandidateOperationKind::DeclareBuffer:
                case CandidateOperationKind::DeclareLike:
                case CandidateOperationKind::ImportTexture:
                case CandidateOperationKind::ImportBuffer:
                {
                    if (!getSlot(mergedOperation.batch, localResource(operation.a), slotIndex))
                        return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "declaration references an invalid candidate resource");
                    LogicalSlot& slot = slots[slotIndex];
                    if (slot.currentAllocation != InvalidRenderFlowResourceIndex)
                        return failOperation(mergedOperation, RenderFlowResourceFailureCode::DescriptorConflict, "declaration cannot replace an already-mapped logical resource");
                    const bool imported = operation.kind == CandidateOperationKind::ImportTexture || operation.kind == CandidateOperationKind::ImportBuffer;
                    const FrameResourceDesc* inheritedDesc = nullptr;
                    if (operation.kind == CandidateOperationKind::DeclareTexture)
                    {
                        if (operation.b >= batch.coldPayloads.Size())
                            return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "texture declaration descriptor index is invalid");
                    }
                    else if (operation.kind == CandidateOperationKind::DeclareBuffer)
                    {
                        if (operation.b >= batch.coldPayloads.Size())
                            return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "buffer declaration descriptor index is invalid");
                    }
                    else if (operation.kind == CandidateOperationKind::DeclareLike)
                    {
                        u32 sourceSlot = InvalidRenderFlowResourceIndex;
                        if (!getSlot(mergedOperation.batch, localResource(operation.b), sourceSlot) || slots[sourceSlot].currentAllocation == InvalidRenderFlowResourceIndex)
                            return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "declare-like source is not mapped at this plan position");
                        inheritedDesc = &scratch->allocations[slots[sourceSlot].currentAllocation].desc;
                    }
                    else if (imported)
                    {
                        if (operation.b >= allocator.retainedImports.Size())
                            return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "import references an invalid session import");
                        if (importedInstalled[operation.b] != 0)
                            return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidUseOrScope, "one retained import cannot be installed into multiple logical destinations");
                        inheritedDesc = &allocator.retainedImports[operation.b].desc;
                        importedInstalled[operation.b] = 1;
                    }
                    const FrameResourceDesc resolvedDesc =
                        operation.kind == CandidateOperationKind::DeclareTexture ? FrameResourceDesc::Texture(batch.coldPayloads[operation.b].textureDeclaration)
                        : operation.kind == CandidateOperationKind::DeclareBuffer ? FrameResourceDesc::Buffer(batch.coldPayloads[operation.b].bufferDeclaration)
                                                                                 : *inheritedDesc;
                    if ((operation.kind == CandidateOperationKind::DeclareTexture && resolvedDesc.kind != FrameResourceKind::Texture) ||
                        (operation.kind == CandidateOperationKind::DeclareBuffer && resolvedDesc.kind != FrameResourceKind::Buffer) ||
                        (operation.kind == CandidateOperationKind::ImportTexture && resolvedDesc.kind != FrameResourceKind::Texture) ||
                        (operation.kind == CandidateOperationKind::ImportBuffer && resolvedDesc.kind != FrameResourceKind::Buffer))
                        return failOperation(mergedOperation, RenderFlowResourceFailureCode::DescriptorConflict, "declaration kind and descriptor kind disagree");
                    LogicalAllocationRecord allocation;
                    allocation.desc = resolvedDesc;
                    allocation.declarationPosition = mergedOperation.position;
                    allocation.imported = imported;
                    allocation.importedResource = imported ? operation.b : InvalidRenderFlowResourceIndex;
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
                        initial.state = imported ? allocator.retainedImports[operation.b].initialState : resolvedDesc.texture.active.initialState;
                        initial.contentsDefined = imported;
                        initial.authoritativeBefore = imported;
                        for (u32 index = 0; index < logicalState.textureCount; ++index)
                            textureSubresourceStates.PushBack(initial);
                    }
                    else
                    {
                        logicalState.bufferState = imported ? allocator.retainedImports[operation.b].initialState : resolvedDesc.buffer.active.initialState;
                        logicalState.bufferContentsDefined = imported;
                        logicalState.bufferAuthoritativeBefore = imported;
                    }
                    logicalState.initializationPending = !imported && (resolvedDesc.kind == FrameResourceKind::Texture ? resolvedDesc.texture.initialization == FrameResourceInitialization::Clear
                                                                                                                       : resolvedDesc.buffer.initialization == FrameResourceInitialization::Clear);
                    allocationStates.PushBack(logicalState);
                    namedScopes.EmplaceBack();
                    slot.currentAllocation = scratch->allocations.Size() - 1u;
                    break;
                }
                case CandidateOperationKind::CreateTextureView:
                {
                    if (!getSlot(mergedOperation.batch, localResource(operation.a), slotIndex) || slotIndex >= resolveScratch.slotCount || slots[slotIndex].currentAllocation == InvalidRenderFlowResourceIndex)
                        return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "texture view was created before its resource was mapped");
                    const u32 allocation = slots[slotIndex].currentAllocation;
                    if (scratch->allocations[allocation].desc.kind != FrameResourceKind::Texture || operation.b >= batch.textureViews.Size())
                        return failOperation(mergedOperation, RenderFlowResourceFailureCode::DescriptorConflict, "texture view is incompatible with its logical resource");
                    ResolvedTextureViewRecord& resolvedView = batchTextureViews[sealed.textureViewOffset + operation.b];
                    if (!NormalizeTextureView(scratch->allocations[allocation].desc.texture, batch.textureViews[operation.b].desc, resolvedView.desc))
                        return failOperation(mergedOperation, RenderFlowResourceFailureCode::DescriptorConflict, "texture view range or format is incompatible with its logical resource");
                    resolvedView.slot = slotIndex;
                    break;
                }
                case CandidateOperationKind::CreateBufferView:
                {
                    if (!getSlot(mergedOperation.batch, localResource(operation.a), slotIndex) || slotIndex >= resolveScratch.slotCount || slots[slotIndex].currentAllocation == InvalidRenderFlowResourceIndex)
                        return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "buffer view was created before its resource was mapped");
                    const u32 allocation = slots[slotIndex].currentAllocation;
                    if (scratch->allocations[allocation].desc.kind != FrameResourceKind::Buffer || operation.b >= batch.bufferViews.Size())
                        return failOperation(mergedOperation, RenderFlowResourceFailureCode::DescriptorConflict, "buffer view is incompatible with its logical resource");
                    ResolvedBufferViewRecord& resolvedView = batchBufferViews[sealed.bufferViewOffset + operation.b];
                    if (!NormalizeBufferView(scratch->allocations[allocation].desc.buffer, batch.bufferViews[operation.b].desc, resolvedView.desc))
                        return failOperation(mergedOperation, RenderFlowResourceFailureCode::DescriptorConflict, "buffer view range, format, or stride is incompatible with its logical resource");
                    resolvedView.slot = slotIndex;
                    break;
                }
                case CandidateOperationKind::TextureUseBegin:
                case CandidateOperationKind::BufferUseBegin:
                {
                    const bool texture = operation.kind == CandidateOperationKind::TextureUseBegin;
                    if (operation.a >= batch.uses.Size())
                        return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidUseOrScope, "resource use begin has an invalid dense identity");
                    const CandidateUse& candidateUse = batch.uses[operation.a];
                    const CandidateUseKind expectedUseKind = texture ? CandidateUseKind::Texture : CandidateUseKind::Buffer;
                    const ResourceUseId use{batch.flowGroup, operation.a, batch.sessionGeneration};
                    if (candidateUse.kind != expectedUseKind || candidateUse.beginSequence != operation.sequence)
                        return failOperation(mergedOperation, RenderFlowResourceFailureCode::BackendContractViolation,
                                             "dense resource-use begin disagrees with its semantic record", use);
                    const bool hasExplicitView = candidateUse.view != InvalidRenderFlowResourceIndex;
#if VG_ENABLE_PROFILING && VG_COMPILE_OPERATIONS_PROBE
                    const bool probeBufferUse = !texture;
                    u64 probeBufferUseOrdinal = 0;
                    if (probeBufferUse)
                        probeBufferUseOrdinal = compileOperationsProbe.bufferUseBeginCount++;
                    const bool probeSample = probeBufferUse && (probeBufferUseOrdinal & CompileOperationsProbeSampleMask) == compileOperationsProbeSamplePhase;
#if defined(_MSC_VER)
                    const u64 probeBeginCycles = probeSample ? __rdtsc() : 0;
                    u64 probePhaseBeginCycles = probeBeginCycles;
#endif
#endif
                    if (texture && hasExplicitView)
                    {
                        if (candidateUse.view >= batch.textureViews.Size())
                            return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "texture use references an invalid view", use);
                        slotIndex = batchTextureViews[sealed.textureViewOffset + candidateUse.view].slot;
                    }
                    else if (!texture && hasExplicitView)
                    {
                        if (candidateUse.view >= batch.bufferViews.Size())
                            return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "buffer use references an invalid view", use);
                        slotIndex = batchBufferViews[sealed.bufferViewOffset + candidateUse.view].slot;
                    }
                    else if (candidateUse.resource == InvalidRenderFlowResourceIndex || !getSlot(mergedOperation.batch, localResource(candidateUse.resource), slotIndex))
                    {
                        return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "resource use references an invalid logical resource", use);
                    }
                    if (slotIndex == InvalidRenderFlowResourceIndex || slots[slotIndex].currentAllocation == InvalidRenderFlowResourceIndex)
                        return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidUseOrScope, "resource use begins before the logical resource is mapped", use);
                    const u32 allocation = slots[slotIndex].currentAllocation;
                    const FrameResourceKind expected = texture ? FrameResourceKind::Texture : FrameResourceKind::Buffer;
                    if (scratch->allocations[allocation].desc.kind != expected)
                        return failOperation(mergedOperation, RenderFlowResourceFailureCode::DescriptorConflict, "resource use kind does not match the active allocation", use);
                    if (!hasExplicitView)
                        scratch->allocations[allocation].requiresCanonicalDescriptors = true;
                    TextureUseDesc textureUse;
                    BufferUseDesc bufferUse;
                    if (texture)
                        textureUse = candidateUse.payload.texture;
                    else
                        bufferUse = candidateUse.payload.buffer;
                    const LogicalAccessIntent access = texture ? textureUse.access : bufferUse.access;
                    const ResourceContentIntent content = texture ? textureUse.content : bufferUse.content;
                    const rhi::ResourceState requiredState = texture ? textureUse.requiredState : bufferUse.requiredState;
                    if (!ValidAccessAndContent(access, content))
                        return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidUseOrScope, "resource use contains an invalid access/content combination", use);
                    if ((texture && !TextureStateCompatible(scratch->allocations[allocation].desc.texture.active, requiredState)) ||
                        (!texture && !BufferStateCompatible(scratch->allocations[allocation].desc.buffer.active, requiredState)) || !StateMatchesAccess(expected, requiredState, access) ||
                        !QueueSupportsState(packet.queue, expected, requiredState))
                        return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidUseOrScope,
                                             "resource use state is incompatible with its kind, creation usage, access intent, or command queue", use);
                    AllocationLogicalState& allocationState = allocationStates[allocation];
                    const bool initializationClear = allocationState.initializationPending;
                    if (initializationClear && !AccessWrites(access))
                        return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidUseOrScope, "declaration-time clear initialization requires a writable first use", use);
                    if ((texture && content != ResourceContentIntent::Clear && textureUse.clearValue.kind != TextureClearValueKind::None) ||
                        (!texture && content != ResourceContentIntent::Clear && bufferUse.clearValue.kind != BufferClearValueKind::None))
                        return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidUseOrScope, "a resource use supplies a clear value without clear content intent", use);
                    const u32 denseUseIndex = sealed.useRecordOffset + operation.a;
                    if (denseUseIndex != uses.Size())
                        return failOperation(mergedOperation, RenderFlowResourceFailureCode::BackendContractViolation,
                                             "dense resource-use stream is out of order or duplicated", use);
#if VG_ENABLE_PROFILING && VG_COMPILE_OPERATIONS_PROBE
                    if (probeSample)
                    {
#if defined(_MSC_VER)
                        const u64 probeNow = __rdtsc();
                        compileOperationsProbe.sampledLookupValidationCycles += probeNow - probePhaseBeginCycles;
                        probePhaseBeginCycles = probeNow;
#endif
                    }
#endif
                    CompiledStep step;
                    step.kind = texture ? CompiledResourceStepKind::TextureUseBegin : CompiledResourceStepKind::BufferUseBegin;
                    step.use = use;
                    step.logicalAllocation = allocation;
                    if (packet.runtime.liveness == nullptr)
                    {
                        packet.runtime.liveness = AllocatePacketUseLiveness();
                        if (packet.runtime.liveness == nullptr)
                            return failOperation(mergedOperation, RenderFlowResourceFailureCode::CapacityExceeded, "packet liveness metadata allocation failed", use);
                        packet.runtime.liveness->useActive.Reserve(batch.uses.Size());
                    }
                    step.runtimeUseSlot = packet.runtime.liveness->useActive.Size();
                    if (texture)
                        step.texture = textureUse;
                    else
                        step.buffer = bufferUse;
                    step.hasExplicitView = hasExplicitView;
                    step.beforeActionOffset = packet.actions.Size();
                    if (texture)
                    {
                        if (step.texture.requiredState == rhi::ResourceState::Unknown ||
                            !ResolveSubresources(scratch->allocations[allocation].desc.texture.active, textureUse.subresources, step.texture.subresources))
                            return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidUseOrScope, "texture use contains an invalid required state or subresource range", use);
                        if (hasExplicitView)
                        {
                            step.textureView = batchTextureViews[sealed.textureViewOffset + candidateUse.view].desc;
                            if (!ContainsSubresources(step.texture.subresources, step.textureView.subresources))
                                return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidUseOrScope, "texture use range does not cover every subresource exposed by its view", use);
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
                                return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidUseOrScope, "declaration-time texture clear requires a full-resource first use", use);
                            if (explicitClear && !TextureClearValueEqual(clearValue, step.texture.clearValue))
                                return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidUseOrScope, "declaration and first-use texture clear values disagree", use);
                        }
                        else if (explicitClear)
                        {
                            clearValue = step.texture.clearValue;
                        }
                        CompiledResourceActionKind clearKind = CompiledResourceActionKind::TextureTransition;
                        const bool compileClear = initializationClear || explicitClear;
                        if ((explicitClear && step.texture.clearValue.kind == TextureClearValueKind::None) ||
                            (compileClear && !ClassifyTextureClear(textureDesc, step.texture.requiredState, packet.queue, clearValue, clearKind)))
                            return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidUseOrScope, "texture clear value, format, state, usage, or queue is incompatible", use);
                        if (compileClear && (clearKind == CompiledResourceActionKind::TextureUavFloatClear || clearKind == CompiledResourceActionKind::TextureUavUintClear) &&
                            (step.texture.subresources.firstSlice != 0 || step.texture.subresources.sliceCount != textureDesc.arraySize))
                            return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidUseOrScope, "texture UAV clears must cover every array slice", use);
                        step.nativeHasClear = compileClear;
                        step.nativeClearKind = clearKind;
                        step.nativeTextureClearValue = clearValue;
                        bool requiresUavBarrier = false;
                        if (logicalState.firstActiveUse != InvalidRenderFlowResourceIndex)
                        {
                            if (logicalState.firstActiveUse >= uses.Size())
                                return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "active texture-use chain contains an invalid head", use);
                            const ActiveUseRecord& head = uses[logicalState.firstActiveUse];
                            if (!head.texture || head.ended || head.packet >= scratch->packets.Size())
                                return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "active texture-use chain contains an invalid head record", use);
                            if (scratch->packets[head.packet].queue != packet.queue)
                                return failOperation(mergedOperation, RenderFlowResourceFailureCode::QueueOrCommandScopeMismatch, "simultaneous texture uses cannot span command queues", use);
                        }
                        const u32 resourceMipCount = scratch->allocations[allocation].desc.texture.active.mipCount;
                        for (u32 slice = step.texture.subresources.firstSlice; slice < static_cast<u32>(step.texture.subresources.firstSlice) + step.texture.subresources.sliceCount; ++slice)
                        {
                            for (u32 mip = step.texture.subresources.firstMip; mip < static_cast<u32>(step.texture.subresources.firstMip) + step.texture.subresources.mipCount; ++mip)
                            {
                                TextureSubresourceLogicalState& subresource = textureSubresourceStates[logicalState.textureOffset + slice * resourceMipCount + mip];
                                if (subresource.activeUseCount != 0 &&
                                    (subresource.lastAccess != LogicalAccessIntent::Read || access != LogicalAccessIntent::Read || subresource.state != requiredState))
                                    return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidUseOrScope, "overlapping active texture uses require the same read-only state", use);
                                if (content == ResourceContentIntent::Preserve && !subresource.contentsDefined && !compileClear)
                                    return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidUseOrScope, "texture use attempts to preserve undefined subresource contents", use);
                                requiresUavBarrier = requiresUavBarrier || (subresource.hasPriorUse && subresource.state == rhi::ResourceState::UnorderedAccess &&
                                                                            step.texture.requiredState == rhi::ResourceState::UnorderedAccess && (AccessWrites(subresource.lastAccess) || AccessWrites(access)));
                                const bool sameCommandScope = subresource.lastCommandScope == packet.commandScope;
                                if (allocator.config.allowLogicalOnlyValidation && (!sameCommandScope || subresource.state != step.texture.requiredState))
                                {
                                    CompiledResourceAction action;
                                    action.kind = CompiledResourceActionKind::TextureTransition;
                                    action.logicalAllocation = allocation;
                                    action.before = subresource.authoritativeBefore || sameCommandScope ? subresource.state : rhi::ResourceState::Unknown;
                                    action.after = step.texture.requiredState;
                                    action.textureSubresources = {static_cast<u16>(mip), 1, static_cast<u16>(slice), 1};
                                    if (!appendProvisionalTextureTransition(packet.actions, step.beforeActionOffset, action))
                                        return failOperation(mergedOperation, RenderFlowResourceFailureCode::CapacityExceeded,
                                                             "compiled texture transitions exceed the allocator action capacity", use);
                                }
                                subresource.state = step.texture.requiredState;
                                subresource.lastCommandScope = packet.commandScope;
                                subresource.lastAccess = access;
                                subresource.hasPriorUse = true;
                                subresource.authoritativeBefore = false;
                                ++subresource.activeUseCount;
                            }
                        }
                        if (allocator.config.allowLogicalOnlyValidation && !FinalizeTextureTransitionBlock(packet.actions, step.beforeActionOffset, resolveScratch.textureTransitionRuns))
                            return failOperation(mergedOperation, RenderFlowResourceFailureCode::BackendContractViolation, "compiled texture transition block is not self-contained", use);
                        if (allocator.config.allowLogicalOnlyValidation && !chargeCompiledActionBlock(packet.actions.Size() - step.beforeActionOffset))
                            return failOperation(mergedOperation, RenderFlowResourceFailureCode::CapacityExceeded, "compiled texture transitions exceed the allocator action capacity", use);
                        if (allocator.config.allowLogicalOnlyValidation && requiresUavBarrier)
                        {
                            if (compiledResourceActionCount >= allocator.config.maximumCompiledResourceActions)
                                return failOperation(mergedOperation, RenderFlowResourceFailureCode::CapacityExceeded, "compiled texture UAV barriers exceed the allocator action capacity", use);
                            CompiledResourceAction action;
                            action.kind = CompiledResourceActionKind::TextureUavBarrier;
                            action.logicalAllocation = allocation;
                            packet.actions.PushBack(action);
                            ++compiledResourceActionCount;
                        }
                        if (allocator.config.allowLogicalOnlyValidation && compileClear)
                        {
                            if (compiledResourceActionCount >= allocator.config.maximumCompiledResourceActions)
                                return failOperation(mergedOperation, RenderFlowResourceFailureCode::CapacityExceeded, "compiled texture clears exceed the allocator action capacity", use);
                            CompiledResourceAction action;
                            action.kind = clearKind;
                            action.logicalAllocation = allocation;
                            action.textureSubresources = step.texture.subresources;
                            action.textureClearValue = clearValue;
                            packet.actions.PushBack(action);
                            ++compiledResourceActionCount;
                        }
                        if (compileClear)
                        {
                            for (u32 slice = step.texture.subresources.firstSlice; slice < static_cast<u32>(step.texture.subresources.firstSlice) + step.texture.subresources.sliceCount; ++slice)
                                for (u32 mip = step.texture.subresources.firstMip; mip < static_cast<u32>(step.texture.subresources.firstMip) + step.texture.subresources.mipCount; ++mip)
                                    textureSubresourceStates[logicalState.textureOffset + slice * resourceMipCount + mip].contentsDefined = true;
                            logicalState.initializationPending = false;
                        }
                    }
                    else
                    {
                        if (step.buffer.requiredState == rhi::ResourceState::Unknown)
                            return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidUseOrScope, "buffer use contains an invalid required state", use);
                        if (hasExplicitView)
                        {
                            step.bufferView = batchBufferViews[sealed.bufferViewOffset + candidateUse.view].desc;
                        }
                        AllocationLogicalState& logicalState = allocationState;
                        const bool explicitClear = content == ResourceContentIntent::Clear;
                        BufferClearValue clearValue;
                        if (initializationClear)
                        {
                            clearValue = scratch->allocations[allocation].desc.buffer.clearValue;
                            if (explicitClear && !BufferClearValueEqual(clearValue, step.buffer.clearValue))
                                return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidUseOrScope, "declaration and first-use buffer clear values disagree", use);
                        }
                        else if (explicitClear)
                        {
                            clearValue = step.buffer.clearValue;
                        }
                        CompiledResourceActionKind clearKind = CompiledResourceActionKind::BufferTransition;
                        const bool compileClear = initializationClear || explicitClear;
                        if ((explicitClear && step.buffer.clearValue.kind == BufferClearValueKind::None) ||
                            (compileClear && !ClassifyBufferClear(scratch->allocations[allocation].desc.buffer.active, step.buffer.requiredState, packet.queue, clearValue, clearKind)))
                            return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidUseOrScope, "buffer clear value, state, usage, or queue is incompatible", use);
                        step.nativeHasClear = compileClear;
                        step.nativeClearKind = clearKind;
                        step.nativeBufferClearValue = clearValue;
#if VG_ENABLE_PROFILING && VG_COMPILE_OPERATIONS_PROBE
                        if (probeSample)
                        {
#if defined(_MSC_VER)
                            const u64 probeNow = __rdtsc();
                            compileOperationsProbe.sampledSetupCycles += probeNow - probePhaseBeginCycles;
                            probePhaseBeginCycles = probeNow;
#endif
                        }
#endif
                        if (logicalState.firstActiveUse != InvalidRenderFlowResourceIndex)
                        {
                            if (logicalState.firstActiveUse >= uses.Size())
                                return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "active buffer-use chain contains an invalid head", use);
                            const ActiveUseRecord& head = uses[logicalState.firstActiveUse];
                            if (head.texture || head.ended || head.packet >= scratch->packets.Size())
                                return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "active buffer-use chain contains an invalid head record", use);
                            if (scratch->packets[head.packet].queue != packet.queue)
                                return failOperation(mergedOperation, RenderFlowResourceFailureCode::QueueOrCommandScopeMismatch, "simultaneous buffer uses cannot span command queues", use);
                            if (head.access != LogicalAccessIntent::Read || access != LogicalAccessIntent::Read || head.requiredState != requiredState)
                                return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidUseOrScope, "overlapping active buffer uses require the same read-only state", use);
                        }
#if VG_ENABLE_PROFILING && VG_COMPILE_OPERATIONS_PROBE
                        if (probeSample)
                        {
#if defined(_MSC_VER)
                            const u64 probeNow = __rdtsc();
                            compileOperationsProbe.sampledActiveScanCycles += probeNow - probePhaseBeginCycles;
                            probePhaseBeginCycles = probeNow;
#endif
                        }
#endif
                        if (content == ResourceContentIntent::Preserve && !logicalState.bufferContentsDefined && !compileClear)
                            return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidUseOrScope, "buffer use attempts to preserve undefined contents", use);
                        const bool sameCommandScope = logicalState.bufferLastCommandScope == packet.commandScope;
                        if (allocator.config.allowLogicalOnlyValidation && (!sameCommandScope || logicalState.bufferState != step.buffer.requiredState))
                        {
                            if (compiledResourceActionCount >= allocator.config.maximumCompiledResourceActions)
                                return failOperation(mergedOperation, RenderFlowResourceFailureCode::CapacityExceeded, "compiled buffer transitions exceed the allocator action capacity", use);
                            CompiledResourceAction action;
                            action.kind = CompiledResourceActionKind::BufferTransition;
                            action.logicalAllocation = allocation;
                            action.before = logicalState.bufferAuthoritativeBefore || sameCommandScope ? logicalState.bufferState : rhi::ResourceState::Unknown;
                            action.after = step.buffer.requiredState;
                            packet.actions.PushBack(action);
                            ++compiledResourceActionCount;
                        }
                        const bool requiresUavBarrier = logicalState.bufferHasPriorUse && logicalState.bufferState == rhi::ResourceState::UnorderedAccess &&
                                                        step.buffer.requiredState == rhi::ResourceState::UnorderedAccess &&
                                                        (AccessWrites(logicalState.bufferLastAccess) || AccessWrites(access));
                        if (allocator.config.allowLogicalOnlyValidation && requiresUavBarrier)
                        {
                            if (compiledResourceActionCount >= allocator.config.maximumCompiledResourceActions)
                                return failOperation(mergedOperation, RenderFlowResourceFailureCode::CapacityExceeded, "compiled buffer UAV barriers exceed the allocator action capacity", use);
                            CompiledResourceAction action;
                            action.kind = CompiledResourceActionKind::BufferUavBarrier;
                            action.logicalAllocation = allocation;
                            packet.actions.PushBack(action);
                            ++compiledResourceActionCount;
                        }
                        if (allocator.config.allowLogicalOnlyValidation && compileClear)
                        {
                            if (compiledResourceActionCount >= allocator.config.maximumCompiledResourceActions)
                                return failOperation(mergedOperation, RenderFlowResourceFailureCode::CapacityExceeded, "compiled buffer clears exceed the allocator action capacity", use);
                            CompiledResourceAction action;
                            action.kind = clearKind;
                            action.logicalAllocation = allocation;
                            action.bufferClearValue = clearValue;
                            packet.actions.PushBack(action);
                            ++compiledResourceActionCount;
                        }
                        if (compileClear)
                        {
                            logicalState.bufferContentsDefined = true;
                            logicalState.initializationPending = false;
                        }
                        logicalState.bufferState = step.buffer.requiredState;
                        logicalState.bufferLastCommandScope = packet.commandScope;
                        logicalState.bufferLastAccess = access;
                        logicalState.bufferHasPriorUse = true;
                        logicalState.bufferAuthoritativeBefore = false;
#if VG_ENABLE_PROFILING && VG_COMPILE_OPERATIONS_PROBE
                        if (probeSample)
                        {
#if defined(_MSC_VER)
                            const u64 probeNow = __rdtsc();
                            compileOperationsProbe.sampledStateLoweringCycles += probeNow - probePhaseBeginCycles;
                            probePhaseBeginCycles = probeNow;
#endif
                        }
#endif
                    }
                    step.beforeActionCount = packet.actions.Size() - step.beforeActionOffset;
                    step.afterActionOffset = packet.actions.Size();
                    packet.runtime.liveness->useActive.PushBack(0);
                    const u32 useIndex = uses.Size();
                    ActiveUseRecord activeUse;
                    activeUse.id = use;
                    activeUse.allocation = allocation;
                    activeUse.packet = packetIndex;
                    activeUse.access = access;
                    activeUse.content = content;
                    activeUse.requiredState = requiredState;
                    activeUse.textureSubresources = step.texture.subresources;
                    activeUse.runtimeUseSlot = step.runtimeUseSlot;
                    activeUse.nextActive = allocationStates[allocation].firstActiveUse;
                    AllocationLogicalState& allocationTimeline = allocationStates[allocation];
                    activeUse.beginStep = packet.steps.Size();
                    activeUse.beginPosition = mergedOperation.position;
                    activeUse.texture = texture;
                    if (use.ordinal != packet.beginStepByUseOrdinal.Size() || use.ordinal >= packet.endStepByUseOrdinal.Size())
                        return failOperation(mergedOperation, RenderFlowResourceFailureCode::BackendContractViolation,
                                             "compiled resource-use begin stream is not dense", use);
                    packet.beginStepByUseOrdinal.PushBack(packet.steps.Size());
                    uses.PushBack(activeUse);
                    if (activeUse.nextActive != InvalidRenderFlowResourceIndex)
                        uses[activeUse.nextActive].previousActive = useIndex;
                    allocationTimeline.firstActiveUse = useIndex;
                    if (allocationTimeline.lastUseBeginRecord != InvalidRenderFlowResourceIndex)
                        uses[allocationTimeline.lastUseBeginRecord].nextAllocationUse = useIndex;
                    else
                        allocationTimeline.firstUseRecord = useIndex;
                    allocationTimeline.lastUseBeginRecord = useIndex;
                    packet.steps.PushBack(step);
                    scratch->allocations[allocation].used = true;
                    TouchLifetime(scratch->allocations[allocation], allocation, mergedOperation.position, packetIndex, packet, physicalAssignmentOrder);
#if VG_ENABLE_PROFILING && VG_COMPILE_OPERATIONS_PROBE
                    if (probeSample)
                    {
#if defined(_MSC_VER)
                        const u64 probeEndCycles = __rdtsc();
                        compileOperationsProbe.sampledPublicationCycles += probeEndCycles - probePhaseBeginCycles;
                        compileOperationsProbe.sampledBufferUseBeginCycles += probeEndCycles - probeBeginCycles;
#endif
                        ++compileOperationsProbe.sampledBufferUseBeginCount;
                    }
#endif
                    break;
                }
                case CandidateOperationKind::ScopeOpen:
                {
                    const ResourceScopeId scope = operation.payload.scope;
                    if (!getSlot(mergedOperation.batch, localResource(operation.a), slotIndex) || slots[slotIndex].currentAllocation == InvalidRenderFlowResourceIndex)
                        return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidUseOrScope, "resource scope begins before the logical resource is mapped");
                    u32 existingScope = InvalidRenderFlowResourceIndex;
                    if (!scope.IsValid() || openScopeIndices.Find(scope.value, existingScope))
                        return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidUseOrScope, "resource scope identity is invalid or duplicated");
                    const u32 allocation = slots[slotIndex].currentAllocation;
                    if (!openScopeIndices.Insert(scope.value, scopes.Size()))
                        return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidUseOrScope, "resource scope identity became duplicated during Resolve");
                    scopes.PushBack({scope, allocation, InvalidRenderFlowResourceIndex, mergedOperation.position, false, false});
                    scratch->allocations[allocation].used = true;
                    TouchLifetime(scratch->allocations[allocation], allocation, mergedOperation.position, packetIndex, packet, physicalAssignmentOrder);
                    break;
                }
                case CandidateOperationKind::ScopeClose:
                {
                    const ResourceScopeId scope = operation.payload.scope;
                    u32 scopeIndex = InvalidRenderFlowResourceIndex;
                    OpenScopeRecord* const found = scope.IsValid() && openScopeIndices.Find(scope.value, scopeIndex) && scopeIndex < scopes.Size() ? &scopes[scopeIndex] : nullptr;
                    if (found == nullptr || found->closed)
                        return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidUseOrScope, "resource scope end has no matching open endpoint");
                    found->closed = true;
                    TouchLifetime(scratch->allocations[found->allocation], found->allocation, mergedOperation.position, packetIndex, packet, physicalAssignmentOrder);
                    break;
                }
                case CandidateOperationKind::NamedScopeOpen:
                {
                    if (!getSlot(mergedOperation.batch, localResource(operation.a), slotIndex) || slots[slotIndex].currentAllocation == InvalidRenderFlowResourceIndex)
                        return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidUseOrScope, "named resource scope begins before the logical resource is mapped");
                    const u32 allocation = slots[slotIndex].currentAllocation;
                    if (allocation >= namedScopes.Size())
                        return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidUseOrScope, "named resource scope references an invalid allocation");
                    NamedScopeState& scope = namedScopes[allocation];
                    if (scope.depth == 0)
                        scope.firstOpen = mergedOperation.position;
                    ++scope.depth;
                    scratch->allocations[allocation].used = true;
                    TouchLifetime(scratch->allocations[allocation], allocation, mergedOperation.position, packetIndex, packet, physicalAssignmentOrder);
                    break;
                }
                case CandidateOperationKind::NamedScopeClose:
                {
                    if (!getSlot(mergedOperation.batch, localResource(operation.a), slotIndex) || slots[slotIndex].currentAllocation == InvalidRenderFlowResourceIndex)
                        return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidUseOrScope, "named resource scope ends before the logical resource is mapped");
                    const u32 allocation = slots[slotIndex].currentAllocation;
                    if (allocation >= namedScopes.Size() || namedScopes[allocation].depth == 0)
                        return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidUseOrScope, "named resource scope end has no matching open endpoint");
                    --namedScopes[allocation].depth;
                    TouchLifetime(scratch->allocations[allocation], allocation, mergedOperation.position, packetIndex, packet, physicalAssignmentOrder);
                    break;
                }
                case CandidateOperationKind::SwapMappings:
                {
                    u32 leftSlot = InvalidRenderFlowResourceIndex;
                    u32 rightSlot = InvalidRenderFlowResourceIndex;
                    if (!getSlot(mergedOperation.batch, localResource(operation.a), leftSlot) || !getSlot(mergedOperation.batch, localResource(operation.b), rightSlot) || leftSlot == rightSlot ||
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
                {
                    const DecisionId decision{batch.flowGroup, packet.decisions.Size(), batch.sessionGeneration};
                    packet.decisions.PushBack({decision, operation.FlagSet()});
                    break;
                }
                case CandidateOperationKind::Export:
                {
                    if (!getSlot(mergedOperation.batch, localResource(operation.a), slotIndex))
                        return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "export references an invalid logical resource");
                    const ExportSlotId exportSlot{operation.b, batch.sessionGeneration};
                    const TerminalResourceExportDesc exportDesc = operation.payload.exportDesc;
                    if (operation.b >= allocator.reservedExportSlots)
                        return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "export references an unreserved session slot");
                    if (exportDesc.readiness != ExportReadinessKind::SameQueueContinuation && exportDesc.readiness != ExportReadinessKind::ExplicitFenceSignal)
                        return failOperation(mergedOperation, RenderFlowResourceFailureCode::UnsupportedCapability, "export readiness kind is unsupported");
                    if (physicalAssignmentOrder[operation.b] != InvalidRenderFlowResourceIndex)
                        return failOperation(mergedOperation, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "export slot is requested more than once");
                    PendingExportRecord pending;
                    pending.slot = exportSlot;
                    pending.logicalSlot = slotIndex;
                    pending.terminalState = exportDesc.terminalState;
                    pending.terminalQueue = exportDesc.terminalQueue;
                    pending.readiness = exportDesc.readiness;
                    scratch->pendingExports.PushBack(pending);
                    physicalAssignmentOrder[operation.b] = scratch->pendingExports.Size() - 1u;
                    break;
                }
                }
            }
            if (expectedSequence != batch.semanticSequenceCount || operationIndex != batch.operations.Size() || useEndIndex != batch.useEndOrder.Size() ||
                packet.beginStepByUseOrdinal.Size() != batch.uses.Size())
                return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::BackendContractViolation,
                                   "planning semantic sequence cardinality disagrees with its recorded streams", batch.node,
                                   {batch.flowGroup, expectedSequence});
#if VG_ENABLE_ASSERTS
            const CompiledPacket& compiledPacket = scratch->packets[batchIndex];
            if (compiledPacket.steps.Size() != batch.stepCount || compiledPacket.decisions.Size() != batch.decisionCount ||
                (!batch.uses.Empty() && (compiledPacket.runtime.liveness == nullptr || compiledPacket.runtime.liveness->useActive.Size() != batch.uses.Size())))
                return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::BackendContractViolation,
                                   "recorded operation cardinalities disagree with compiled packet output", batch.node);
#endif
        }
#if VG_ENABLE_ASSERTS
        if (scratch->allocations.Size() != sealedPlanning.declarationCount || allocationStates.Size() != sealedPlanning.declarationCount || namedScopes.Size() != sealedPlanning.declarationCount ||
            uses.Size() != sealedPlanning.useBeginCount || scopes.Size() != sealedPlanning.explicitScopeOpenCount || scratch->pendingExports.Size() != sealedPlanning.exportCount)
            return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::BackendContractViolation,
                               "recorded operation cardinalities disagree with compiled frame output");
#endif

#if VG_ENABLE_PROFILING
#if VG_COMPILE_OPERATIONS_PROBE
#if defined(_MSC_VER)
        const u64 compileOperationsProbeFrameCycleEnd = __rdtsc();
        const u64 compileOperationsProbeFrameClockEnd = system::GetMonotonicTicks();
#endif
#endif
        resolveProfilePhase.reset();
#if VG_COMPILE_OPERATIONS_PROBE
        const auto emitProbeValue = [](const profiling::SourceLocation& location, const u64 value) noexcept
        {
            profiling::Scope scope(location);
            scope.SetValue(value);
        };
        static profiling::SourceLocation probeBufferCountLocation{"RenderFlow/Resolve/CompileOperationsProbe/BufferUseBeginCount", __FUNCTION__, __FILE__, __LINE__};
        static profiling::SourceLocation probeBufferSampleCountLocation{"RenderFlow/Resolve/CompileOperationsProbe/BufferUseBeginSampleCount", __FUNCTION__, __FILE__, __LINE__};
        static profiling::SourceLocation probeBufferTotalLocation{"RenderFlow/Resolve/CompileOperationsProbe/BufferUseBeginSampledTotalCycles", __FUNCTION__, __FILE__, __LINE__};
        static profiling::SourceLocation probeBufferLookupLocation{"RenderFlow/Resolve/CompileOperationsProbe/BufferUseBeginLookupValidationCycles", __FUNCTION__, __FILE__, __LINE__};
        static profiling::SourceLocation probeBufferSetupLocation{"RenderFlow/Resolve/CompileOperationsProbe/BufferUseBeginSetupCycles", __FUNCTION__, __FILE__, __LINE__};
        static profiling::SourceLocation probeBufferScanLocation{"RenderFlow/Resolve/CompileOperationsProbe/BufferUseBeginActiveScanCycles", __FUNCTION__, __FILE__, __LINE__};
        static profiling::SourceLocation probeBufferLowerLocation{"RenderFlow/Resolve/CompileOperationsProbe/BufferUseBeginStateLoweringCycles", __FUNCTION__, __FILE__, __LINE__};
        static profiling::SourceLocation probeBufferPublishLocation{"RenderFlow/Resolve/CompileOperationsProbe/BufferUseBeginPublicationCycles", __FUNCTION__, __FILE__, __LINE__};
#if defined(_MSC_VER)
        static profiling::SourceLocation probeFrameCycleLocation{"RenderFlow/Resolve/CompileOperationsProbe/FrameCycles", __FUNCTION__, __FILE__, __LINE__};
        static profiling::SourceLocation probeFrameClockLocation{"RenderFlow/Resolve/CompileOperationsProbe/FrameClockTicks", __FUNCTION__, __FILE__, __LINE__};
#endif
        emitProbeValue(probeBufferCountLocation, compileOperationsProbe.bufferUseBeginCount);
        emitProbeValue(probeBufferSampleCountLocation, compileOperationsProbe.sampledBufferUseBeginCount);
        emitProbeValue(probeBufferTotalLocation, compileOperationsProbe.sampledBufferUseBeginCycles);
        emitProbeValue(probeBufferLookupLocation, compileOperationsProbe.sampledLookupValidationCycles);
        emitProbeValue(probeBufferSetupLocation, compileOperationsProbe.sampledSetupCycles);
        emitProbeValue(probeBufferScanLocation, compileOperationsProbe.sampledActiveScanCycles);
        emitProbeValue(probeBufferLowerLocation, compileOperationsProbe.sampledStateLoweringCycles);
        emitProbeValue(probeBufferPublishLocation, compileOperationsProbe.sampledPublicationCycles);
#if defined(_MSC_VER)
        emitProbeValue(probeFrameCycleLocation, compileOperationsProbeFrameCycleEnd - compileOperationsProbeFrameCycleBegin);
        emitProbeValue(probeFrameClockLocation, compileOperationsProbeFrameClockEnd - compileOperationsProbeFrameClockBegin);
#endif
#endif
        resolveProfilePhase.emplace(validateProfileLocation);
#endif
        for (const OpenScopeRecord& scope : scopes)
            if (!scope.closed)
                return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::InvalidUseOrScope, "a cross-node resource scope has no close endpoint", {}, scope.begin);
        for (const NamedScopeState& scope : namedScopes)
            if (scope.depth != 0)
                return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::InvalidUseOrScope, "a named resource scope has no close endpoint", {}, scope.firstOpen);

        for (const ActiveUseRecord& use : uses)
            if (!use.ended)
                return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::InvalidUseOrScope, "a resource use has no end endpoint", scratch->packets[use.packet].node, {}, use.id);

        for (u32 allocationIndex = 0; allocationIndex < scratch->allocations.Size(); ++allocationIndex)
        {
            const AllocationLogicalState& timeline = allocationStates[allocationIndex];
            u32 previousUseIndex = InvalidRenderFlowResourceIndex;
            for (u32 useIndex = timeline.firstUseRecord; useIndex != InvalidRenderFlowResourceIndex; useIndex = uses[useIndex].nextAllocationUse)
            {
                const ActiveUseRecord& use = uses[useIndex];
                if (previousUseIndex != InvalidRenderFlowResourceIndex)
                {
                    const ActiveUseRecord& previous = uses[previousUseIndex];
                    const CompiledPacket& producer = scratch->packets[previous.packet];
                    const CompiledPacket& consumer = scratch->packets[use.packet];
                    if (producer.queue != consumer.queue)
                    {
                        if (!QueueOrderProved(previous.packet, use.packet, scratch->commandScopes, crossQueueProducerScopes))
                            return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::QueueOrCommandScopeMismatch,
                                               "cross-queue resource use is not ordered through an executable Graphics/Compute fork or join boundary", consumer.node, use.beginPosition, use.id);
                    }
                }
                previousUseIndex = useIndex;
            }
        }

        if (!scratch->pendingExports.Empty() && allocator.config.allowLogicalOnlyValidation)
            return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::UnsupportedCapability, "terminal exports require real standalone physical resources");
        if (scratch->pendingExports.Size() >
            allocator.config.maximumOutstandingPublishedExports -
                (allocator.publishedExports.Size() <= allocator.config.maximumOutstandingPublishedExports ? allocator.publishedExports.Size() : allocator.config.maximumOutstandingPublishedExports))
            return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::CapacityExceeded, "outstanding terminal export publication capacity is exhausted");
        allocator.publishedExports.Reserve(allocator.publishedExports.Size() + scratch->pendingExports.Size());
        allocator.publishedExportIndices.Reserve(allocator.publishedExports.Size() + scratch->pendingExports.Size());
        for (u32 exportIndex = 0; exportIndex < scratch->pendingExports.Size(); ++exportIndex)
        {
            PendingExportRecord& exportRecord = scratch->pendingExports[exportIndex];
            if (exportRecord.logicalSlot >= resolveScratch.slotCount || slots[exportRecord.logicalSlot].currentAllocation == InvalidRenderFlowResourceIndex)
                return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "terminal export has no mapped logical allocation");
            exportRecord.allocation = slots[exportRecord.logicalSlot].currentAllocation;
            LogicalAllocationRecord& allocation = scratch->allocations[exportRecord.allocation];
            if (allocation.exported)
                return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::InvalidUseOrScope, "one terminal logical allocation cannot publish to multiple export slots");
            allocation.exported = true;
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

            AllocationLogicalState& timeline = allocationStates[allocationIndex];
            ActiveUseRecord* initialUse = timeline.firstUseRecord < uses.Size() ? &uses[timeline.firstUseRecord] : nullptr;
            ActiveUseRecord* terminalUse = timeline.terminalUseRecord < uses.Size() ? &uses[timeline.terminalUseRecord] : nullptr;
            if (initialUse == nullptr || terminalUse == nullptr || initialUse->allocation != allocationIndex || terminalUse->allocation != allocationIndex)
                return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::IncompleteExecution, "a used imported resource has no complete use endpoints");
            if (imported.readiness == ImportReadinessKind::ExplicitFenceWait)
            {
                for (u32 useIndex = timeline.firstUseRecord; useIndex != InvalidRenderFlowResourceIndex; useIndex = uses[useIndex].nextAllocationUse)
                {
                    if (useIndex >= uses.Size() || uses[useIndex].allocation != allocationIndex)
                        return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "imported resource use timeline is stale");
                    const ActiveUseRecord& use = uses[useIndex];
                    if (use.packet >= scratch->packets.Size())
                        return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "imported wait has an invalid consumer packet");
                    u64& wait = scratch->packets[use.packet].incomingWaits[static_cast<u32>(imported.incomingWait.queue)];
                    if (imported.incomingWait.value > wait)
                        wait = imported.incomingWait.value;
                }
            }
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
            if (terminalStep.terminalState != rhi::ResourceState::Unknown && terminalStep.terminalState != imported.terminalState)
                return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::DescriptorConflict,
                                   "retained import terminal state conflicts with another terminal contract", terminalPacket.node, terminalUse->endPosition, terminalUse->id);
            terminalStep.terminalState = imported.terminalState;
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
                    terminalStep.terminalPresent = true;
                    if (allocator.config.allowLogicalOnlyValidation)
                    {
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
                    }
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
                            if (allocator.config.allowLogicalOnlyValidation)
                            {
                                CompiledResourceAction action;
                                action.kind = CompiledResourceActionKind::TextureTransition;
                                action.logicalAllocation = allocationIndex;
                                action.before = subresource.state;
                                action.after = imported.terminalState;
                                action.textureSubresources = {static_cast<u16>(mip), 1, static_cast<u16>(slice), 1};
                                if (!appendProvisionalTextureTransition(terminalPacket.actions, terminalStep.afterActionOffset, action))
                                    return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::CapacityExceeded,
                                                       "retained texture terminal transitions exceed the allocator action capacity");
                            }
                            subresource.state = imported.terminalState;
                        }
                    }
                    if (allocator.config.allowLogicalOnlyValidation &&
                        !FinalizeTextureTransitionBlock(terminalPacket.actions, terminalStep.afterActionOffset, resolveScratch.textureTransitionRuns))
                        return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::BackendContractViolation,
                                           "retained texture terminal transition block is not self-contained");
                    if (allocator.config.allowLogicalOnlyValidation && !chargeCompiledActionBlock(terminalPacket.actions.Size() - terminalStep.afterActionOffset))
                        return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::CapacityExceeded,
                                           "retained texture terminal transitions exceed the allocator action capacity");
                }
            }
            else
            {
                AllocationLogicalState& logicalState = allocationStates[allocationIndex];
                if (logicalState.bufferState != imported.terminalState)
                {
                    if (allocator.config.allowLogicalOnlyValidation)
                    {
                        if (compiledResourceActionCount >= allocator.config.maximumCompiledResourceActions)
                            return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::CapacityExceeded,
                                               "retained buffer terminal transition exceeds the allocator action capacity");
                        CompiledResourceAction action;
                        action.kind = CompiledResourceActionKind::BufferTransition;
                        action.logicalAllocation = allocationIndex;
                        action.before = logicalState.bufferState;
                        action.after = imported.terminalState;
                        terminalPacket.actions.PushBack(action);
                        ++compiledResourceActionCount;
                    }
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
                if (imported.presentationAcquisition.IsValid())
                    return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::UnsupportedCapability,
                                       "presentation imports cannot escape through generic terminal export ownership");
                if (imported.terminalState != exportRecord.terminalState || imported.terminalQueue != exportRecord.terminalQueue)
                    return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::DescriptorConflict, "imported terminal export must match the import release contract");
            }

            const AllocationLogicalState& timeline = allocationStates[exportRecord.allocation];
            ActiveUseRecord* terminalUse = timeline.terminalUseRecord < uses.Size() ? &uses[timeline.terminalUseRecord] : nullptr;
            if (terminalUse == nullptr || terminalUse->packet >= scratch->packets.Size() || terminalUse->endStep >= scratch->packets[terminalUse->packet].steps.Size())
                return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::IncompleteExecution, "terminal export has no valid final resource-use endpoint");
            CompiledPacket& terminalPacket = scratch->packets[terminalUse->packet];
            if (terminalPacket.queue != exportRecord.terminalQueue)
                return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::QueueOrCommandScopeMismatch, "terminal export queue must match the resource's final executable use",
                                   terminalPacket.node);
            CompiledStep& terminalStep = terminalPacket.steps[terminalUse->endStep];
            if (terminalStep.terminalState != rhi::ResourceState::Unknown && terminalStep.terminalState != exportRecord.terminalState)
                return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::DescriptorConflict,
                                   "terminal export state conflicts with another terminal contract", terminalPacket.node, terminalUse->endPosition, terminalUse->id);
            terminalStep.terminalState = exportRecord.terminalState;
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
                        if (allocator.config.allowLogicalOnlyValidation)
                        {
                            CompiledResourceAction action;
                            action.kind = CompiledResourceActionKind::TextureTransition;
                            action.logicalAllocation = exportRecord.allocation;
                            action.before = subresource.state;
                            action.after = exportRecord.terminalState;
                            action.textureSubresources = {static_cast<u16>(mip), 1, static_cast<u16>(slice), 1};
                            if (!appendProvisionalTextureTransition(terminalPacket.actions, actionStart, action))
                                return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::CapacityExceeded,
                                                   "terminal texture export transitions exceed the allocator action capacity");
                        }
                        subresource.state = exportRecord.terminalState;
                    }
                }
                if (allocator.config.allowLogicalOnlyValidation && !FinalizeTextureTransitionBlock(terminalPacket.actions, actionStart, resolveScratch.textureTransitionRuns))
                    return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::BackendContractViolation,
                                       "terminal texture export transition block is not self-contained");
                if (allocator.config.allowLogicalOnlyValidation && !chargeCompiledActionBlock(terminalPacket.actions.Size() - actionStart))
                    return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::CapacityExceeded,
                                       "terminal texture export transitions exceed the allocator action capacity");
            }
            else
            {
                AllocationLogicalState& logicalState = allocationStates[exportRecord.allocation];
                if (logicalState.bufferState != exportRecord.terminalState)
                {
                    if (allocator.config.allowLogicalOnlyValidation)
                    {
                        if (compiledResourceActionCount >= allocator.config.maximumCompiledResourceActions)
                            return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::CapacityExceeded,
                                               "terminal buffer export transition exceeds the allocator action capacity");
                        CompiledResourceAction action;
                        action.kind = CompiledResourceActionKind::BufferTransition;
                        action.logicalAllocation = exportRecord.allocation;
                        action.before = logicalState.bufferState;
                        action.after = exportRecord.terminalState;
                        terminalPacket.actions.PushBack(action);
                        ++compiledResourceActionCount;
                    }
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
            exportRecord.terminalPacket = terminalUse->packet;
            exportRecord.kind = allocation.desc.kind;
        }
        if (structuralPlanEligible && structuralSignature != 0)
            structuralCandidateReady = BuildStructuralPlanCandidate(structuralSignature, allocator, *scratch, resolveScratch, allocator.reservedExportSlots);
        }

#if VG_ENABLE_PROFILING
        resolveProfilePhase.reset();
        resolveProfilePhase.emplace(physicalProfileLocation);
#endif
        const u32 physicalGeneration = scratch->id.generation;
        scratch->hasNativeResourceBindings = !allocator.config.allowLogicalOnlyValidation;
        containers::DynamicArray<WholeFrameLane>& wholeFrameLanes = resolveScratch.wholeFrameLanes;
        containers::DynamicArray<WholeFrameReleaseEvent>& wholeFrameReleaseEvents = resolveScratch.wholeFrameReleaseEvents;
        auto& wholeFrameReuseHeads = resolveScratch.wholeFrameReuseHeads;
        containers::DynamicArray<WholeFrameReuseClass>& wholeFrameReuseClasses = resolveScratch.wholeFrameReuseClasses;
        containers::DynamicArray<u32>& persistentAllocationLanes = resolveScratch.persistentAllocationLanes;
        containers::DynamicArray<u32>& persistentLanePhysical = resolveScratch.persistentLanePhysical;
        containers::DynamicArray<u32>& persistentLaneAdoptionEntries = resolveScratch.persistentLaneAdoptionEntries;
        const u32 physicalAssignmentFirst = allocator.reservedExportSlots;
        if (physicalAssignmentOrder.Size() < physicalAssignmentFirst)
            return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::BackendContractViolation, "physical assignment metadata lost its reserved export prefix");
        const u32 physicalAssignmentCount = physicalAssignmentOrder.Size() - physicalAssignmentFirst;
        persistentAllocationLanes.Resize(scratch->allocations.Size(), InvalidRenderFlowResourceIndex);
        wholeFrameLanes.Reserve(scratch->allocations.Size());
        wholeFrameReleaseEvents.Reserve(scratch->allocations.Size());
        wholeFrameReuseHeads.Reserve(scratch->allocations.Size());
        containers::DynamicArray<DedicatedResourceLease>& releasedPersistentLeases = resolveScratch.releasedPersistentLeases;

        // Resolve the dedicated-plan contract before *any* physical provider
        // acquisition. An ineligible old plan must not keep PlanReady bytes
        // reserved while the current frame tries the placed or generic path.
        const bool persistentPlanHit = scratch->hasNativeResourceBindings && !allocator.activePolicy.enablePlacedResources &&
                                       PersistentDedicatedPlanMatches(allocator.persistentDedicatedPlan, *scratch, resolveScratch);
        if (!persistentPlanHit && allocator.persistentDedicatedPlan.valid)
        {
            if (allocator.activePolicy.enablePlacedResources)
                for (PersistentDedicatedPlanLaneRuntime& runtime : allocator.persistentDedicatedPlan.laneRuntime)
                    for (const DedicatedResourceLease& lease : runtime.leases)
                        releasedPersistentLeases.PushBack(lease);
            ReleasePersistentDedicatedPlan(allocator, true);
        }
        if (persistentPlanHit)
        {
            ++allocator.stats.persistentDedicatedPlanHits;
            persistentLanePhysical.Resize(allocator.persistentDedicatedPlan.lanes.Size(), InvalidRenderFlowResourceIndex);
            persistentLaneAdoptionEntries.Resize(allocator.persistentDedicatedPlan.lanes.Size(), InvalidDedicatedResourceEntry);
            for (PersistentDedicatedPlanLaneRuntime& runtime : allocator.persistentDedicatedPlan.laneRuntime)
                runtime.leases.Reserve(runtime.leases.Size() + 1u);
        }

        PlacedRangePlan& placedPlan = resolveScratch.placedPlan;
        PlacedRangePlannerScratch& placedPlannerScratch = resolveScratch.placedPlannerScratch;
        containers::DynamicArray<PlacedRangeRequest>& placedRequests = resolveScratch.placedRequests;
        containers::DynamicArray<PlacedObjectRequest>& placedObjects = resolveScratch.placedObjects;
        if (scratch->hasNativeResourceBindings && allocator.activePolicy.enablePlacedResources)
        {
            const rhi::Capabilities::PlacedResourceProfile& profile = rhi::GetCapabilities().placedResources;
            u64 maximumRequiredAlignment = 1;
            u64 minimumSupportedHeapAlignment = ~u64{0};
            placedRequests.Reserve(physicalAssignmentCount);
            placedObjects.Reserve(physicalAssignmentCount);
            for (u32 assignmentIndex = physicalAssignmentFirst; assignmentIndex < physicalAssignmentOrder.Size(); ++assignmentIndex)
            {
                const u32 allocation = physicalAssignmentOrder[assignmentIndex];
                if (allocation >= scratch->allocations.Size())
                    return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::BackendContractViolation, "physical assignment order contains an invalid logical allocation");
                const LogicalAllocationRecord& logical = scratch->allocations[allocation];
                if (!logical.used || !logical.firstUsePosition.IsValid() || !logical.lastUsePosition.IsValid() || logical.firstUseCommandScopeIndex >= scratch->commandScopes.Size() ||
                    logical.lastUseCommandScopeIndex >= scratch->commandScopes.Size())
                    return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::BackendContractViolation, "used logical allocation has an incomplete compiled lifetime");
                if (logical.imported || logical.exported || (logical.queueMask != QueueMask(rhi::QueueType::Graphics) && logical.queueMask != QueueMask(rhi::QueueType::Compute)))
                    continue;
                const u32 firstScopeIndex = logical.firstUseCommandScopeIndex;
                const rhi::QueueType queue = scratch->commandScopes[firstScopeIndex].queue;
                if ((queue == rhi::QueueType::Graphics && !profile.sameQueueGraphics) || (queue == rhi::QueueType::Compute && !profile.sameQueueCompute))
                    continue;
                if (logical.desc.kind == FrameResourceKind::Texture && queue == rhi::QueueType::Compute &&
                    HasAny(logical.desc.texture.active.usage, rhi::TextureUsage::RenderTarget | rhi::TextureUsage::DepthStencil) &&
                    !HasAny(logical.desc.texture.active.usage, rhi::TextureUsage::UnorderedAccess))
                    continue;
                const rhi::ResourceState initialState = logical.desc.kind == FrameResourceKind::Texture ? logical.desc.texture.active.initialState : logical.desc.buffer.active.initialState;
                // Placed alias reuse currently finalizes predecessor ownership to
                // Common. Automatic-state resources close back to their creation
                // state, so a non-Common creation state cannot participate in that
                // alias contract either. Keep it on the mandatory dedicated path.
                if (initialState != rhi::ResourceState::Common)
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
                placedObjects.PushBack({allocation, physicalDesc, logical.requiresCanonicalDescriptors});
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
                if (!BuildPlacedRangePlan(placedRequests, plannerConfig, placedPlan, placedPlannerScratch, failure))
                {
                    const RenderFlowResourceFailureCode code = failure != nullptr ? failure->code : RenderFlowResourceFailureCode::BackendContractViolation;
                    const char* const message = failure != nullptr && failure->message != nullptr ? failure->message : "placed range planning failed";
                    return FailResolve(allocator, scratch, failure, code, message);
                }
                RenderFlowResourceFailure localPlacedFailure;
                RenderFlowResourceFailure* const placedFailure = failure != nullptr ? failure : &localPlacedFailure;
                const auto acquirePlaced = [&]() noexcept
                {
                    return RunCanonicalDescriptorPublicationWithGlobalPressure(
                        allocator,
                        [&](CanonicalDescriptorPressure* const pressure, const bool recordStats) noexcept
                        { return allocator.placedPool.Acquire(placedPlan, placedObjects, allocator.frameSerial, scratch->placedBatch, placedFailure, pressure, recordStats); },
                        placedFailure);
                };
                bool acquiredPlaced = acquirePlaced();
                if (!acquiredPlaced && placedFailure->code == RenderFlowResourceFailureCode::BudgetExceeded && !releasedPersistentLeases.Empty())
                {
                    for (const DedicatedResourceLease& lease : releasedPersistentLeases)
                        allocator.dedicatedPool.EvictLease(lease);
                    // Final release enters the RHI's fence-epoch retirement queue.
                    // Advance ordinary reclamation without waiting for the GPU;
                    // the backend may complete ready destruction synchronously or
                    // schedule its latent reclamation worker. Either way the old
                    // RF6 backing is no longer a permanently pinned hard-budget
                    // charge after an incompatible provider cutover.
                    rhi::Failure retirementFailure;
                    if (!rhi::RetireResources(&retirementFailure))
                        return FailResolve(allocator, scratch, failure, MapRhiFailure(retirementFailure, RhiFailureContext::BackendContract),
                                           retirementFailure.message[0] != '\0' ? retirementFailure.message : "dedicated hard-budget retirement progression failed");
                    allocator.dedicatedPool.Poll();
                    allocator.placedPool.Poll();
                    *placedFailure = {};
                    acquiredPlaced = acquirePlaced();
                }
                if (!acquiredPlaced)
                {
                    const RenderFlowResourceFailureCode code = placedFailure->code != RenderFlowResourceFailureCode::None ? placedFailure->code : RenderFlowResourceFailureCode::DeviceLostOrBackendFailure;
                    const char* const message = placedFailure->message != nullptr ? placedFailure->message : "placed-resource physical assignment failed";
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
                    binding.shaderResource = assignment.shaderResource;
                    binding.unorderedAccess = assignment.unorderedAccess;
                    const FrameResourceDesc& assignedDesc = scratch->allocations[assignment.allocation].desc;
                    if (scratch->allocations[assignment.allocation].requiresCanonicalDescriptors && allocator.resourceDescriptors.IsValid() &&
                        !HasRequiredCanonicalDescriptors(binding, assignedDesc))
                        return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::BackendContractViolation,
                                           "placed whole-resource descriptor binding is not backed by a canonical physical owner");
                    CachePhysicalStateDescriptor(binding, assignedDesc);
                    const u32 bindingIndex = scratch->physicalBindings.Size();
                    scratch->physicalBindings.PushBack(binding);
                    scratch->allocations[assignment.allocation].physical = {bindingIndex, physicalGeneration};
                }
            }
            else
            {
                placedPlannerScratch.DecayWithoutUse();
                placedRequests.Clear();
                placedObjects.Clear();
            }
        }

        const auto releaseEventGreater = [](const WholeFrameReleaseEvent& left, const WholeFrameReleaseEvent& right) noexcept
        {
            return PositionBefore(right.release, left.release);
        };
        const auto pendingGreater = [](const WholeFrameReuseCandidate& left, const WholeFrameReuseCandidate& right) noexcept
        {
            if (left.producerScope != right.producerScope)
                return left.producerScope > right.producerScope;
            if (left.capacity != right.capacity)
                return left.capacity > right.capacity;
            return left.lane > right.lane;
        };
        const auto candidateBetter = [](const WholeFrameReuseCandidate& left, const WholeFrameReuseCandidate& right) noexcept
        {
            return left.capacity != right.capacity ? left.capacity < right.capacity : left.lane < right.lane;
        };
        const auto sameReadyForQueue = [](WholeFrameReuseClass& reuseClass, const rhi::QueueType queue) noexcept -> WholeFrameReadyIndex&
        {
            return queue == rhi::QueueType::Graphics ? reuseClass.graphicsReady : queue == rhi::QueueType::Compute ? reuseClass.computeReady : reuseClass.copyReady;
        };
        const auto validCandidate = [&wholeFrameLanes](const WholeFrameReuseCandidate& candidate) noexcept
        {
            return candidate.lane < wholeFrameLanes.Size() && wholeFrameLanes[candidate.lane].epoch == candidate.epoch;
        };
        const auto peekReady = [&validCandidate](WholeFrameReadyIndex& ready, const u64 minimumCapacity, WholeFrameReuseCandidate& candidate, u32& nodeIndex) noexcept
        {
            for (;;)
            {
                nodeIndex = ready.LowerBoundCapacity(minimumCapacity);
                if (nodeIndex == InvalidRenderFlowResourceIndex)
                    return false;
                candidate = ready.Candidate(nodeIndex);
                if (validCandidate(candidate))
                    return true;
                ready.Remove(nodeIndex);
            }
        };
        const auto promoteCross = [&validCandidate, &pendingGreater](containers::DynamicArray<WholeFrameReuseCandidate>& pending, WholeFrameReadyIndex& ready,
                                                                     const u32 watermark) noexcept
        {
            for (;;)
            {
                while (!pending.Empty() && !validCandidate(pending[0]))
                {
                    std::pop_heap(pending.Begin(), pending.End(), pendingGreater);
                    pending.PopBack();
                }
                if (pending.Empty() || pending[0].producerScope > watermark)
                    return;
                std::pop_heap(pending.Begin(), pending.End(), pendingGreater);
                const WholeFrameReuseCandidate candidate = pending.Back();
                pending.PopBack();
                if (!validCandidate(candidate))
                    continue;
                static_cast<void>(ready.Insert(candidate));
            }
        };
        const auto findReuseClass = [&](const DedicatedResourceClassKey& compatibility, const bool create) noexcept -> u32
        {
            const u64 key = compatibility.hash;
            u32 head = InvalidRenderFlowResourceIndex;
            static_cast<void>(wholeFrameReuseHeads.Find(key, head));
            for (u32 current = head; current != InvalidRenderFlowResourceIndex; current = wholeFrameReuseClasses[current].nextCollision)
                if (DedicatedResourceSameCompatibilityClass(wholeFrameReuseClasses[current].compatibility, compatibility))
                    return current;
            if (!create)
                return InvalidRenderFlowResourceIndex;
            const u32 index = resolveScratch.wholeFrameReuseClassCount++;
            if (index == wholeFrameReuseClasses.Size())
                wholeFrameReuseClasses.EmplaceBack();
            else
                wholeFrameReuseClasses[index].Reset();
            WholeFrameReuseClass& reuseClass = wholeFrameReuseClasses[index];
            reuseClass.compatibility = compatibility;
            if (head == InvalidRenderFlowResourceIndex)
            {
                const bool inserted = wholeFrameReuseHeads.Insert(key, index);
                VG_ASSERT(inserted);
            }
            else
            {
                reuseClass.nextCollision = wholeFrameReuseClasses[head].nextCollision;
                wholeFrameReuseClasses[head].nextCollision = index;
            }
            return index;
        };
        const auto queueReleaseEvent = [&wholeFrameLanes, &wholeFrameReleaseEvents, &releaseEventGreater](const u32 laneIndex) noexcept
        {
            const WholeFrameLane& lane = wholeFrameLanes[laneIndex];
            wholeFrameReleaseEvents.PushBack({lane.lastUsePosition, laneIndex, lane.epoch});
            std::push_heap(wholeFrameReleaseEvents.Begin(), wholeFrameReleaseEvents.End(), releaseEventGreater);
        };
        const auto releaseBefore = [&](const PlanPosition position) noexcept
        {
            while (!wholeFrameReleaseEvents.Empty() && PositionBefore(wholeFrameReleaseEvents[0].release, position))
            {
                std::pop_heap(wholeFrameReleaseEvents.Begin(), wholeFrameReleaseEvents.End(), releaseEventGreater);
                const WholeFrameReleaseEvent event = wholeFrameReleaseEvents.Back();
                wholeFrameReleaseEvents.PopBack();
                if (event.lane >= wholeFrameLanes.Size())
                    continue;
                WholeFrameLane& lane = wholeFrameLanes[event.lane];
                if (lane.epoch != event.epoch || lane.reuseClass >= resolveScratch.wholeFrameReuseClassCount || lane.lastUseCommandScopeIndex >= scratch->commandScopes.Size())
                    continue;
                const rhi::QueueType producerQueue = scratch->commandScopes[lane.lastUseCommandScopeIndex].queue;
                WholeFrameReuseCandidate candidate;
                candidate.capacity = lane.capacity;
                candidate.lane = event.lane;
                candidate.epoch = event.epoch;
                candidate.producerScope = lane.lastUseCommandScopeIndex;
                WholeFrameReuseClass& reuseClass = wholeFrameReuseClasses[lane.reuseClass];
                auto& sameReady = sameReadyForQueue(reuseClass, producerQueue);
                static_cast<void>(sameReady.Insert(candidate));
                if (producerQueue == rhi::QueueType::Graphics)
                {
                    reuseClass.graphicsToComputePending.PushBack(candidate);
                    std::push_heap(reuseClass.graphicsToComputePending.Begin(), reuseClass.graphicsToComputePending.End(), pendingGreater);
                }
                else if (producerQueue == rhi::QueueType::Compute)
                {
                    reuseClass.computeToGraphicsPending.PushBack(candidate);
                    std::push_heap(reuseClass.computeToGraphicsPending.Begin(), reuseClass.computeToGraphicsPending.End(), pendingGreater);
                }
            }
        };

        u32 nextVirtualPhysical = 0;
        const bool assignmentLifetimesValidated = scratch->hasNativeResourceBindings && allocator.activePolicy.enablePlacedResources;
        for (u32 assignmentIndex = physicalAssignmentFirst; assignmentIndex < physicalAssignmentOrder.Size(); ++assignmentIndex)
        {
            const u32 allocation = physicalAssignmentOrder[assignmentIndex];
            if (allocation >= scratch->allocations.Size())
                return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::BackendContractViolation, "physical assignment order contains an invalid logical allocation");
            LogicalAllocationRecord& logical = scratch->allocations[allocation];
            if (!assignmentLifetimesValidated &&
                (!logical.used || !logical.firstUsePosition.IsValid() || !logical.lastUsePosition.IsValid() || logical.firstUseCommandScopeIndex >= scratch->commandScopes.Size() ||
                 logical.lastUseCommandScopeIndex >= scratch->commandScopes.Size()))
                return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::BackendContractViolation, "used logical allocation has an incomplete compiled lifetime");
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
                CachePhysicalStateDescriptor(binding, retained.desc);
                if (logical.requiresCanonicalDescriptors &&
                    !RunCanonicalDescriptorPublicationWithGlobalPressure(
                        allocator,
                        [&](CanonicalDescriptorPressure* const pressure, const bool) noexcept
                        { return EnsureRetainedImportCanonicalDescriptors(allocator, binding, retained.desc, pressure, failure); },
                        failure))
                {
                    const RenderFlowResourceFailureCode code = failure != nullptr && failure->code != RenderFlowResourceFailureCode::None
                                                                   ? failure->code
                                                                   : RenderFlowResourceFailureCode::DeviceLostOrBackendFailure;
                    const char* const message = failure != nullptr && failure->message != nullptr
                                                    ? failure->message
                                                    : "retained import canonical descriptor publication failed";
                    return FailResolve(allocator, scratch, failure, code, message);
                }
                if (logical.requiresCanonicalDescriptors && allocator.resourceDescriptors.IsValid() &&
                    !HasRequiredCanonicalDescriptors(binding, retained.desc))
                    return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::BackendContractViolation,
                                       "retained import whole-resource descriptor publication is incomplete");
                const u32 bindingIndex = scratch->physicalBindings.Size();
                scratch->physicalBindings.PushBack(std::move(binding));
                logical.physical = {bindingIndex, physicalGeneration};
                continue;
            }
            if (logical.physical.IsValid())
                continue;

            DedicatedResourceRequest dedicatedRequest = BuildDedicatedResourceRequest(logical.desc);
            dedicatedRequest.requiresCanonicalDescriptors = logical.requiresCanonicalDescriptors;

            if (persistentPlanHit && !logical.exported)
            {
                PersistentDedicatedPhysicalPlan& plan = allocator.persistentDedicatedPlan;
                const u32 laneIndex = plan.allocations[allocation].lane;
                if (laneIndex >= plan.lanes.Size() || laneIndex >= persistentLanePhysical.Size())
                    return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::BackendContractViolation,
                                       "persistent dedicated plan references an invalid physical lane");

                u32 physicalIndex = persistentLanePhysical[laneIndex];
                if (physicalIndex == InvalidRenderFlowResourceIndex)
                {
                    DedicatedResourceAssignment assignment;
                    bool acquired = false;
                    PersistentDedicatedPlanLaneRuntime& runtime = plan.laneRuntime[laneIndex];
                    for (u32 leaseIndex = 0; leaseIndex < runtime.leases.Size();)
                    {
                        const DedicatedResourceLease lease = runtime.leases[leaseIndex];
                        DedicatedResourceLeaseAcquireResult leaseResult = DedicatedResourceLeaseAcquireResult::Stale;
                        if (!RunCanonicalDescriptorPublicationWithGlobalPressure(
                                allocator,
                                [&](CanonicalDescriptorPressure* const pressure, const bool recordStats) noexcept
                                {
                                    leaseResult = allocator.dedicatedPool.TryAcquireLease(lease, dedicatedRequest, allocator.frameSerial, assignment,
                                                                                         failure, pressure, recordStats);
                                    return leaseResult != DedicatedResourceLeaseAcquireResult::Failed;
                                },
                                failure))
                        {
                            const RenderFlowResourceFailureCode code = failure != nullptr ? failure->code : RenderFlowResourceFailureCode::DeviceLostOrBackendFailure;
                            const char* const message = failure != nullptr && failure->message != nullptr ? failure->message : "persistent dedicated lease checkout failed";
                            return FailResolve(allocator, scratch, failure, code, message);
                        }
                        if (leaseResult == DedicatedResourceLeaseAcquireResult::Acquired)
                        {
                            ++allocator.stats.persistentDedicatedLeaseHits;
                            acquired = true;
                            break;
                        }
                        if (leaseResult == DedicatedResourceLeaseAcquireResult::Stale)
                        {
                            runtime.leases[leaseIndex] = runtime.leases.Back();
                            runtime.leases.PopBack();
                            continue;
                        }
                        ++leaseIndex;
                    }

                    if (!acquired)
                    {
                        if (!RunCanonicalDescriptorPublicationWithGlobalPressure(
                                allocator,
                                [&](CanonicalDescriptorPressure* const pressure, const bool recordStats) noexcept
                                { return allocator.dedicatedPool.Acquire(dedicatedRequest, allocator.frameSerial, assignment, failure, pressure, recordStats); },
                                failure))
                        {
                            const RenderFlowResourceFailureCode code = failure != nullptr ? failure->code : RenderFlowResourceFailureCode::DeviceLostOrBackendFailure;
                            const char* const message = failure != nullptr && failure->message != nullptr ? failure->message : "persistent dedicated lane fallback acquisition failed";
                            return FailResolve(allocator, scratch, failure, code, message);
                        }
                        persistentLaneAdoptionEntries[laneIndex] = assignment.entry;
                        ++allocator.stats.persistentDedicatedLeaseFallbacks;
                    }

                    PhysicalBindingRecord binding;
                    binding.kind = PhysicalBindingKind::DedicatedPool;
                    binding.poolEntry = assignment.entry;
                    binding.texture = assignment.texture;
                    binding.buffer = assignment.buffer;
                    binding.shaderResource = assignment.shaderResource;
                    binding.unorderedAccess = assignment.unorderedAccess;
                    CachePhysicalStateDescriptor(binding, dedicatedRequest.physicalDesc);
                    physicalIndex = scratch->physicalBindings.Size();
                    scratch->physicalBindings.PushBack(binding);
                    if (assignment.capacity < plan.lanes[laneIndex].requiredCapacity)
                        return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::BackendContractViolation,
                                           "persistent dedicated lane backing is smaller than its compiled capacity contract");
                    persistentLanePhysical[laneIndex] = physicalIndex;
                    logical.physical = {physicalIndex, physicalGeneration};
                    if (dedicatedRequest.requiresCanonicalDescriptors && allocator.resourceDescriptors.IsValid() &&
                        !HasRequiredCanonicalDescriptors(binding, dedicatedRequest.physicalDesc))
                        return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::BackendContractViolation,
                                           "persistent dedicated lane whole-resource descriptor binding is not backed by its physical owner");
                }
                else
                {
                    if (physicalIndex >= scratch->physicalBindings.Size())
                        return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::BackendContractViolation,
                                           "persistent dedicated lane lost its current-frame physical binding");
                    PhysicalBindingRecord& binding = scratch->physicalBindings[physicalIndex];
                    if (binding.kind != PhysicalBindingKind::DedicatedPool)
                        return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::BackendContractViolation,
                                           "persistent dedicated lane resolved to a non-dedicated physical owner");
                    logical.physical = {physicalIndex, physicalGeneration};
                    if (dedicatedRequest.requiresCanonicalDescriptors && !HasRequiredCanonicalDescriptors(binding, dedicatedRequest.physicalDesc))
                    {
                        if (!RunCanonicalDescriptorPublicationWithGlobalPressure(
                                allocator,
                                [&](CanonicalDescriptorPressure* const pressure, const bool recordStats) noexcept
                                {
                                    return allocator.dedicatedPool.EnsureCanonicalDescriptors(binding.poolEntry, allocator.frameSerial, binding.shaderResource,
                                                                                              binding.unorderedAccess, failure, pressure, recordStats);
                                },
                                failure))
                        {
                            const RenderFlowResourceFailureCode code = failure != nullptr ? failure->code : RenderFlowResourceFailureCode::DeviceLostOrBackendFailure;
                            const char* const message = failure != nullptr && failure->message != nullptr ? failure->message : "persistent dedicated lane canonical descriptor publication failed";
                            return FailResolve(allocator, scratch, failure, code, message);
                        }
                    }
                }
                continue;
            }

            const u32 firstScopeIndex = logical.firstUseCommandScopeIndex;
            releaseBefore(logical.firstUsePosition);

            u32 reusableLane = InvalidRenderFlowResourceIndex;
            if (!logical.exported)
            {
                const u32 reuseClassIndex = findReuseClass(dedicatedRequest.compatibility, false);
                if (reuseClassIndex != InvalidRenderFlowResourceIndex)
                {
                    WholeFrameReuseClass& reuseClass = wholeFrameReuseClasses[reuseClassIndex];
                    const rhi::QueueType consumerQueue = scratch->commandScopes[firstScopeIndex].queue;
                    auto& sameReady = sameReadyForQueue(reuseClass, consumerQueue);
                    WholeFrameReadyIndex* crossReady = nullptr;
                    if (firstScopeIndex < crossQueueProducerScopes.Size())
                    {
                        const u32 watermark = crossQueueProducerScopes[firstScopeIndex];
                        if (watermark != InvalidRenderFlowResourceIndex && consumerQueue == rhi::QueueType::Compute)
                        {
                            promoteCross(reuseClass.graphicsToComputePending, reuseClass.graphicsToComputeReady, watermark);
                            crossReady = &reuseClass.graphicsToComputeReady;
                        }
                        else if (watermark != InvalidRenderFlowResourceIndex && consumerQueue == rhi::QueueType::Graphics)
                        {
                            promoteCross(reuseClass.computeToGraphicsPending, reuseClass.computeToGraphicsReady, watermark);
                            crossReady = &reuseClass.computeToGraphicsReady;
                        }
                    }
                    const u64 minimumCapacity = dedicatedRequest.capacity;
                    WholeFrameReuseCandidate sameCandidate;
                    WholeFrameReuseCandidate crossCandidate;
                    u32 sameNode = InvalidRenderFlowResourceIndex;
                    u32 crossNode = InvalidRenderFlowResourceIndex;
                    const bool hasSame = peekReady(sameReady, minimumCapacity, sameCandidate, sameNode);
                    const bool hasCross = crossReady != nullptr && peekReady(*crossReady, minimumCapacity, crossCandidate, crossNode);
                    WholeFrameReuseCandidate candidate;
                    WholeFrameReadyIndex* selectedReady = nullptr;
                    u32 selectedNode = InvalidRenderFlowResourceIndex;
                    if (hasSame)
                    {
                        candidate = sameCandidate;
                        selectedReady = &sameReady;
                        selectedNode = sameNode;
                    }
                    if (hasCross && (!hasSame || candidateBetter(crossCandidate, candidate)))
                    {
                        candidate = crossCandidate;
                        selectedReady = crossReady;
                        selectedNode = crossNode;
                    }
                    if (selectedReady != nullptr)
                    {
                        selectedReady->Remove(selectedNode);
                        if (!validCandidate(candidate) || candidate.lane >= wholeFrameLanes.Size() || wholeFrameLanes[candidate.lane].reuseClass != reuseClassIndex ||
                            !DedicatedResourceCanSatisfy(reuseClass.compatibility, wholeFrameLanes[candidate.lane].capacity, dedicatedRequest.compatibility, dedicatedRequest.capacity))
                            return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::BackendContractViolation,
                                               "indexed dedicated-resource reuse candidate violates compatibility");
                        if (!QueueOrderProved(candidate.producerScope, firstScopeIndex, scratch->commandScopes, crossQueueProducerScopes))
                            return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::BackendContractViolation,
                                               "indexed dedicated-resource reuse candidate violates queue ordering");
                        reusableLane = candidate.lane;
                    }
                }
            }

            if (reusableLane != InvalidRenderFlowResourceIndex)
            {
                WholeFrameLane& lane = wholeFrameLanes[reusableLane];
                persistentAllocationLanes[allocation] = reusableLane;
                logical.physical = {lane.physicalIndex, physicalGeneration};
                if (scratch->hasNativeResourceBindings && dedicatedRequest.requiresCanonicalDescriptors)
                {
                    if (lane.physicalIndex >= scratch->physicalBindings.Size())
                        return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::BackendContractViolation,
                                           "same-frame dedicated reuse lost its physical binding");
                    PhysicalBindingRecord& binding = scratch->physicalBindings[lane.physicalIndex];
                    if (binding.kind != PhysicalBindingKind::DedicatedPool)
                        return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::BackendContractViolation,
                                           "same-frame dedicated reuse resolved to a non-dedicated physical owner");
                    if (!HasRequiredCanonicalDescriptors(binding, dedicatedRequest.physicalDesc))
                    {
                        if (!RunCanonicalDescriptorPublicationWithGlobalPressure(
                                allocator,
                                [&](CanonicalDescriptorPressure* const pressure, const bool recordStats) noexcept
                                {
                                    return allocator.dedicatedPool.EnsureCanonicalDescriptors(binding.poolEntry, allocator.frameSerial, binding.shaderResource,
                                                                                              binding.unorderedAccess, failure, pressure, recordStats);
                                },
                                failure))
                        {
                            const RenderFlowResourceFailureCode code = failure != nullptr ? failure->code : RenderFlowResourceFailureCode::DeviceLostOrBackendFailure;
                            const char* const message = failure != nullptr && failure->message != nullptr ? failure->message : "same-frame dedicated canonical descriptor publication failed";
                            return FailResolve(allocator, scratch, failure, code, message);
                        }
                        if (allocator.resourceDescriptors.IsValid() && !HasRequiredCanonicalDescriptors(binding, dedicatedRequest.physicalDesc))
                            return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::BackendContractViolation,
                                               "same-frame dedicated whole-resource descriptor binding is not backed by a canonical physical owner");
                    }
                }
                lane.lastUsePosition = logical.lastUsePosition;
                lane.lastUseCommandScopeIndex = logical.lastUseCommandScopeIndex;
                ++lane.epoch;
                if (lane.epoch == 0)
                    lane.epoch = 1;
                queueReleaseEvent(reusableLane);
                continue;
            }

            u32 physicalIndex = InvalidRenderFlowResourceIndex;
            u64 physicalCapacity = dedicatedRequest.capacity;
            if (!scratch->hasNativeResourceBindings)
            {
                physicalIndex = nextVirtualPhysical++;
                logical.physical = {physicalIndex, physicalGeneration};
            }
            else
            {
                DedicatedResourceAssignment assignment;
                if (!RunCanonicalDescriptorPublicationWithGlobalPressure(
                        allocator,
                        [&](CanonicalDescriptorPressure* const pressure, const bool recordStats) noexcept
                        { return allocator.dedicatedPool.Acquire(dedicatedRequest, allocator.frameSerial, assignment, failure, pressure, recordStats); },
                        failure))
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
                binding.shaderResource = assignment.shaderResource;
                binding.unorderedAccess = assignment.unorderedAccess;
                if (dedicatedRequest.requiresCanonicalDescriptors && allocator.resourceDescriptors.IsValid() &&
                    !HasRequiredCanonicalDescriptors(binding, dedicatedRequest.physicalDesc))
                    return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::BackendContractViolation,
                                       "dedicated whole-resource descriptor binding is not backed by a canonical physical owner");
                // Dedicated reuse classes preserve every state/usage field cached
                // here; buffer capacity is intentionally separate and irrelevant
                // to state publication. Reuse the already-canonical request instead
                // of copying the pool entry's full FrameResourceDesc back out.
                CachePhysicalStateDescriptor(binding, dedicatedRequest.physicalDesc);
                scratch->physicalBindings.PushBack(binding);
                logical.physical = {bindingIndex, physicalGeneration};
                physicalIndex = bindingIndex;
                physicalCapacity = assignment.capacity;
            }
            if (!logical.exported)
            {
                WholeFrameLane lane;
                lane.capacity = physicalCapacity;
                lane.lastUsePosition = logical.lastUsePosition;
                lane.lastUseCommandScopeIndex = logical.lastUseCommandScopeIndex;
                lane.physicalIndex = physicalIndex;
                lane.reuseClass = findReuseClass(dedicatedRequest.compatibility, true);
                lane.epoch = 1;
                wholeFrameLanes.PushBack(lane);
                const u32 laneIndex = wholeFrameLanes.Size() - 1u;
                persistentAllocationLanes[allocation] = laneIndex;
                queueReleaseEvent(laneIndex);
            }
        }

        const bool persistentCandidateReady = !persistentPlanHit && scratch->hasNativeResourceBindings && !allocator.activePolicy.enablePlacedResources &&
                                              BuildPersistentDedicatedCandidate(*scratch, resolveScratch, physicalAssignmentFirst);

        for (const PlacedRangeAssignment& range : placedPlan.assignments)
        {
            if (range.predecessorCount == 0)
                continue;
            if (range.allocation >= scratch->allocations.Size() || range.predecessorOffset > placedPlan.predecessors.Size() || range.predecessorCount > placedPlan.predecessors.Size() - range.predecessorOffset)
                return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::BackendContractViolation, "placed alias provenance is outside the compiled range plan");
            const LogicalAllocationRecord& destination = scratch->allocations[range.allocation];
            const AllocationLogicalState& destinationTimeline = allocationStates[range.allocation];
            const ActiveUseRecord* firstUse = destinationTimeline.firstUseRecord < uses.Size() ? &uses[destinationTimeline.firstUseRecord] : nullptr;
            // The physical lifetime may start earlier at a RED-shaped named or
            // cross-node scope. Alias activation still belongs to the first
            // executable use of the placed object.
            if (firstUse == nullptr || firstUse->allocation != range.allocation || firstUse->packet >= scratch->packets.Size())
                return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::BackendContractViolation, "placed destination has no compiled first-use packet");
            CompiledPacket& packet = scratch->packets[firstUse->packet];
            CompiledStep* beginStep = firstUse->beginStep < packet.steps.Size() ? &packet.steps[firstUse->beginStep] : nullptr;
            if (beginStep == nullptr || beginStep->aliasPredecessorCount != 0 || !destination.physical.IsValid() || destination.physical.index >= scratch->physicalBindings.Size() ||
                beginStep->use != firstUse->id || (beginStep->kind != CompiledResourceStepKind::TextureUseBegin && beginStep->kind != CompiledResourceStepKind::BufferUseBegin) ||
                scratch->physicalBindings[destination.physical.index].kind != PhysicalBindingKind::PlacedPool)
                return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::BackendContractViolation, "placed destination cannot receive its compiled alias activation");
            const PhysicalBindingRecord& destinationBinding = scratch->physicalBindings[destination.physical.index];
            const bool destinationIsTexture = destination.desc.kind == FrameResourceKind::Texture;
            if (destinationBinding.texture.IsValid() != destinationIsTexture || destinationBinding.buffer.IsValid() == destinationIsTexture)
                return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::BackendContractViolation, "placed destination kind disagrees with its physical resource");
            if (compiledResourceActionCount >= allocator.config.maximumCompiledResourceActions)
                return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::CapacityExceeded, "placed alias activations exceed the allocator action capacity");
            ++compiledResourceActionCount;
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
                const AllocationLogicalState& predecessorTimeline = allocationStates[fragment.allocation];
                const ActiveUseRecord* terminalUse = predecessorTimeline.terminalUseRecord < uses.Size() ? &uses[predecessorTimeline.terminalUseRecord] : nullptr;
                // A scope may keep the physical lifetime alive after the final
                // executable use. Finalize at that real use endpoint; the
                // placed range planner still honors the wider lifetime bound.
                if (terminalUse == nullptr || terminalUse->allocation != fragment.allocation || terminalUse->packet >= scratch->packets.Size() ||
                    terminalUse->endStep >= scratch->packets[terminalUse->packet].steps.Size())
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
                const u32 aliasCount = packet.aliasPredecessors.Size();
                packet.aliasPredecessors.PushBack(predecessorBinding.texture.IsValid() ? rhi::ResourceRef(predecessorBinding.texture) : rhi::ResourceRef(predecessorBinding.buffer));
                if (packet.aliasPredecessors.Size() != aliasCount + 1u)
                    return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::CapacityExceeded, "placed alias predecessor native metadata allocation failed");
                packet.aliasPredecessorPhysicalIndices.PushBack(predecessorPhysical.index);
                if (packet.aliasPredecessorPhysicalIndices.Size() != aliasCount + 1u)
                    return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::CapacityExceeded, "placed alias predecessor physical metadata allocation failed");
            }
        }

#if VG_ENABLE_PROFILING
        resolveProfilePhase.reset();
        resolveProfilePhase.emplace(finalizeBindingsProfileLocation);
#endif
        // Assignment order is dead after alias metadata has been lowered. Reuse
        // its backing as the dense physical -> export relation needed by state
        // finalization (and reject any impossible physical sharing directly).
        physicalAssignmentOrder.Clear();
        physicalAssignmentOrder.Resize(scratch->physicalBindings.Size(), InvalidRenderFlowResourceIndex);
        for (u32 exportIndex = 0; exportIndex < scratch->pendingExports.Size(); ++exportIndex)
        {
            PendingExportRecord& exportRecord = scratch->pendingExports[exportIndex];
            if (exportRecord.allocation >= scratch->allocations.Size())
                return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "terminal export physical assignment lost its logical allocation");
            exportRecord.physical = scratch->allocations[exportRecord.allocation].physical;
            if (!exportRecord.physical.IsValid() || exportRecord.physical.index >= scratch->physicalBindings.Size())
                return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "terminal export has no physical assignment");
            u32& physicalExport = physicalAssignmentOrder[exportRecord.physical.index];
            if (physicalExport != InvalidRenderFlowResourceIndex)
                return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::BackendContractViolation, "one physical assignment cannot back multiple terminal exports");
            physicalExport = exportIndex;
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
        for (u32 packetIndex = 0; packetIndex < scratch->packets.Size(); ++packetIndex)
        {
            CompiledPacket& packet = scratch->packets[packetIndex];
            if (packet.beginStepByUseOrdinal.Size() != packet.endStepByUseOrdinal.Size() ||
                (packet.runtime.liveness != nullptr && packet.beginStepByUseOrdinal.Size() != packet.runtime.liveness->useActive.Size()) ||
                (packet.runtime.liveness == nullptr && !packet.beginStepByUseOrdinal.Empty()))
                return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::BackendContractViolation,
                                   "compiled packet dense resource-use topology disagrees with its liveness image", packet.node);
            if (packet.runtime.liveness != nullptr)
            {
                packet.runtime.liveness->ResetCompiledRange();
                packet.runtime.liveness->generation = scratch->id;
                packet.runtime.liveness->packetIndex = packetIndex;
            }
            // Native lowering is about to visit every begin step and emits every
            // native action with its physical identity already attached. Logical-only
            // validation has no lowering pass, so patch only that compatibility path.
            if (!scratch->hasNativeResourceBindings)
            {
                for (CompiledStep& step : packet.steps)
                {
                    if (step.kind == CompiledResourceStepKind::UseEnd)
                        continue;
                    if (step.logicalAllocation >= scratch->allocations.Size())
                        return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::InvalidOrStaleIdentity,
                                           "compiled resource use has an invalid logical allocation", packet.node, {}, step.use);
                    step.physical = scratch->allocations[step.logicalAllocation].physical;
                    if (!step.physical.IsValid())
                        return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::InvalidOrStaleIdentity,
                                           "compiled resource use has no physical assignment", packet.node, {}, step.use);
                }
                for (CompiledResourceAction& action : packet.actions)
                {
                    if (action.logicalAllocation >= scratch->allocations.Size())
                        return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::InvalidOrStaleIdentity,
                                           "compiled resource action has an invalid logical allocation", packet.node);
                    action.physical = scratch->allocations[action.logicalAllocation].physical;
                    if (!action.physical.IsValid())
                        return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::InvalidOrStaleIdentity,
                                           "compiled resource action has no physical assignment", packet.node);
                }
            }
        }

#if VG_ENABLE_PROFILING
        resolveProfilePhase.reset();
        resolveProfilePhase.emplace(finalizeStatesProfileLocation);
#endif
        if (scratch->hasNativeResourceBindings)
        {
            resolveScratch.ResetPhysicalHistory(scratch->physicalBindings.Size());
            containers::DynamicArray<PhysicalScopeState>& history = resolveScratch.physicalHistory;
            const u32 historyCount = resolveScratch.physicalHistoryCount;
            if (historyCount != scratch->physicalBindings.Size())
                return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::CapacityExceeded, "physical-state table allocation failed");
            for (const LogicalAllocationRecord& allocation : scratch->allocations)
            {
                if (!allocation.used || allocation.desc.kind != FrameResourceKind::Texture || allocation.desc.texture.active.keepInitialState)
                    continue;
                if (!allocation.physical.IsValid() || allocation.physical.index >= historyCount)
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
            if (!LowerPhysicalStates(allocator, *scratch, resolveScratch, compiledResourceActionCount, failure))
            {
                // Preserve the precise lowering failure while rolling back unpublished native assignments.
                static_cast<void>(FailResolve(allocator, scratch, nullptr, RenderFlowResourceFailureCode::BackendContractViolation, "physical state lowering failed"));
                return false;
            }
        }

        // Compile the immutable semantic execution tape only after native state
        // lowering has finalized every step action slice and packet exit action.
        // The tape omits actionless uses entirely; one entry represents one
        // step's real semantic work and preserves its exact intra-step ordering.
        for (CompiledPacket& packet : scratch->packets)
        {
            packet.actionTape.Clear();
            packet.exitActionTapeOffset = 0;
            packet.exitActionTapeCount = 0;
            packet.actionTape.Reserve(packet.steps.Size() + (packet.exitActions.Empty() ? 0u : 1u));

            const auto appendTapeEntry = [&](const CompiledActionTapePhase phase, const u32 stepIndex, const u32 actionOffset,
                                             const u32 actionCount) noexcept -> bool
            {
                const u32 expected = packet.actionTape.Size() + 1u;
                packet.actionTape.PushBack({phase, stepIndex, actionOffset, actionCount});
                return packet.actionTape.Size() == expected;
            };

            for (u32 stepIndex = 0; stepIndex < packet.steps.Size(); ++stepIndex)
            {
                CompiledStep& step = packet.steps[stepIndex];
                step.actionTapeOffset = packet.actionTape.Size();
                step.actionTapeCount = 0;
                if (step.kind == CompiledResourceStepKind::TextureUseBegin || step.kind == CompiledResourceStepKind::BufferUseBegin)
                {
                    if (step.beforeActionOffset > packet.actions.Size() || step.beforeActionCount > packet.actions.Size() - step.beforeActionOffset ||
                        step.afterActionCount != 0)
                        return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::BackendContractViolation,
                                           "compiled begin step has an invalid semantic action slice", packet.node, {}, step.use);
                    if (step.aliasPredecessorCount != 0 || step.beforeActionCount != 0)
                    {
                        if (!appendTapeEntry(CompiledActionTapePhase::Begin, stepIndex, step.beforeActionOffset, step.beforeActionCount))
                            return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::CapacityExceeded,
                                               "compiled begin action tape allocation failed", packet.node, {}, step.use);
                        step.actionTapeCount = 1;
                    }
                }
                else if (step.kind == CompiledResourceStepKind::UseEnd)
                {
                    if (step.afterActionOffset > packet.actions.Size() || step.afterActionCount > packet.actions.Size() - step.afterActionOffset ||
                        step.beforeActionCount != 0)
                        return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::BackendContractViolation,
                                           "compiled end step has an invalid semantic action slice", packet.node, {}, step.use);
                    if (step.afterActionCount != 0 || step.finalizeForAlias)
                    {
                        if (!appendTapeEntry(CompiledActionTapePhase::End, stepIndex, step.afterActionOffset, step.afterActionCount))
                            return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::CapacityExceeded,
                                               "compiled end action tape allocation failed", packet.node, {}, step.use);
                        step.actionTapeCount = 1;
                    }
                }
                else
                {
                    return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::BackendContractViolation,
                                       "compiled action tape encountered an unknown step kind", packet.node, {}, step.use);
                }
            }

            packet.exitActionTapeOffset = packet.actionTape.Size();
            if (!packet.exitActions.Empty())
            {
                if (!appendTapeEntry(CompiledActionTapePhase::Exit, InvalidRenderFlowResourceIndex, 0, packet.exitActions.Size()))
                    return FailResolve(allocator, scratch, failure, RenderFlowResourceFailureCode::CapacityExceeded,
                                       "compiled packet-exit action tape allocation failed", packet.node);
                packet.exitActionTapeCount = 1;
            }
        }

#if VG_ENABLE_PROFILING
        resolveProfilePhase.reset();
        resolveProfilePhase.emplace(publishProfileLocation);
#endif
        if (persistentPlanHit)
        {
            PersistentDedicatedPhysicalPlan& plan = allocator.persistentDedicatedPlan;
            for (u32 laneIndex = 0; laneIndex < persistentLaneAdoptionEntries.Size(); ++laneIndex)
            {
                const u32 entry = persistentLaneAdoptionEntries[laneIndex];
                if (entry == InvalidDedicatedResourceEntry)
                    continue;
                DedicatedResourceLease lease;
                if (allocator.dedicatedPool.ReserveLease(entry, lease))
                    plan.laneRuntime[laneIndex].leases.PushBack(lease);
            }
        }
        else if (persistentCandidateReady && resolveScratch.candidateDedicatedPlan.lanes.Size() == wholeFrameLanes.Size())
        {
            bool reserved = true;
            for (u32 laneIndex = 0; laneIndex < wholeFrameLanes.Size(); ++laneIndex)
            {
                const u32 physicalIndex = wholeFrameLanes[laneIndex].physicalIndex;
                if (physicalIndex >= scratch->physicalBindings.Size() || scratch->physicalBindings[physicalIndex].kind != PhysicalBindingKind::DedicatedPool)
                {
                    reserved = false;
                    break;
                }
                DedicatedResourceLease lease;
                if (!allocator.dedicatedPool.ReserveLease(scratch->physicalBindings[physicalIndex].poolEntry, lease))
                {
                    reserved = false;
                    break;
                }
                resolveScratch.candidateDedicatedPlan.laneRuntime[laneIndex].leases.PushBack(lease);
            }
            if (reserved)
            {
                ReleasePersistentDedicatedPlan(allocator, true);
                allocator.persistentDedicatedPlan = static_cast<PersistentDedicatedPhysicalPlan&&>(resolveScratch.candidateDedicatedPlan);
                ++allocator.stats.persistentDedicatedPlanBuilds;
            }
            else
            {
                for (PersistentDedicatedPlanLaneRuntime& runtime : resolveScratch.candidateDedicatedPlan.laneRuntime)
                    for (const DedicatedResourceLease& lease : runtime.leases)
                        allocator.dedicatedPool.ReleaseLease(lease);
                resolveScratch.candidateDedicatedPlan.Clear();
            }
        }
        else if (!persistentPlanHit)
            ReleasePersistentDedicatedPlan(allocator, true);

        if (structuralCandidateReady && resolveScratch.candidateStructuralPlan.valid)
        {
            allocator.structuralPlan = static_cast<PersistentStructuralPlan&&>(resolveScratch.candidateStructuralPlan);
            ++allocator.stats.structuralPlanBuilds;
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
        // Consume. Candidate tapes are no longer replayed; clear logical size
        // while retaining only hard-bounded per-writer backing, matching RED's
        // fixed parallel-group request storage behavior.
        allocator.stats.rejectedOperations += allocator.pendingRejectedOperations.Exchange(0);
        // Resolve's name index borrows StringViews from the sealed candidate
        // tapes. Release those views before making the tapes writable again.
        resolveScratch.namedSlots.Reset();
        ResetPlanningStorage(allocator, true);
        allocator.retainedImports.Clear();
        return true;
    }
} // namespace vanguard::rendering::detail
