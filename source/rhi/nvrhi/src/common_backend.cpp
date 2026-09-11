#include <vanguard/rhi/backend/common_backend.hpp>

#include <vanguard/diagnostics/diagnostics.hpp>
#include <vanguard/memory/memory.hpp>
#include <vanguard/system/assert.hpp>
#include <vanguard/system/time.hpp>
#include <vanguard/concurrency/thread.hpp>

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

        [[nodiscard]] constexpr bool IsDepthFormat(const Format format) noexcept
        {
            return format == Format::D16UNorm || format == Format::D24UNormS8UInt || format == Format::D32Float || format == Format::D32FloatS8UInt;
        }

        [[nodiscard]] constexpr bool HasStencilPlane(const Format format) noexcept
        {
            return format == Format::D24UNormS8UInt || format == Format::D32FloatS8UInt;
        }

        [[nodiscard]] constexpr bool IsUnsignedIntegerFormat(const Format format) noexcept
        {
            return format == Format::R8UInt || format == Format::R8G8UInt || format == Format::R8G8B8A8UInt || format == Format::R16UInt ||
                   format == Format::R16G16UInt || format == Format::R16G16B16A16UInt || format == Format::R32UInt || format == Format::R32G32UInt;
        }

        [[nodiscard]] bool ResolveSubresources(const TextureDesc& desc, const SubresourceRange& requested, SubresourceRange& resolved) noexcept
        {
            if (requested.firstMip >= desc.mipCount || requested.firstSlice >= desc.arraySize)
                return false;
            const u32 mipCount = requested.mipCount == 0xffffu ? desc.mipCount - requested.firstMip : requested.mipCount;
            const u32 sliceCount = requested.sliceCount == 0xffffu ? desc.arraySize - requested.firstSlice : requested.sliceCount;
            if (mipCount == 0 || sliceCount == 0 || mipCount > static_cast<u32>(desc.mipCount - requested.firstMip) ||
                sliceCount > static_cast<u32>(desc.arraySize - requested.firstSlice))
                return false;
            resolved = {requested.firstMip, static_cast<u16>(mipCount), requested.firstSlice, static_cast<u16>(sliceCount)};
            return true;
        }

        [[nodiscard]] constexpr u32 MipDimension(const u32 dimension, const u16 mipLevel) noexcept
        {
            const u32 value = dimension >> mipLevel;
            return value != 0 ? value : 1;
        }

        [[nodiscard]] Extent3D GetMipExtent(const TextureDesc& desc, const u16 mipLevel) noexcept
        {
            return {MipDimension(desc.extent.width, mipLevel), desc.dimension == TextureDimension::Texture1D ? 1u : MipDimension(desc.extent.height, mipLevel),
                    desc.dimension == TextureDimension::Texture3D ? MipDimension(desc.extent.depth, mipLevel) : 1u};
        }

        [[nodiscard]] constexpr bool IsBlockCompressed(const Format format) noexcept
        {
            return format >= Format::BC1UNorm && format <= Format::BC7UNormSrgb;
        }

        [[nodiscard]] constexpr bool IsBlockRegionValid(const u32 offset, const u32 extent, const u32 dimension) noexcept
        {
            return offset % 4u == 0 && (extent % 4u == 0 || offset + extent == dimension);
        }

        [[nodiscard]] constexpr bool RegionsOverlap(const u32 firstOffset, const u32 firstExtent, const u32 secondOffset, const u32 secondExtent) noexcept
        {
            return firstOffset < secondOffset + secondExtent && secondOffset < firstOffset + firstExtent;
        }

        [[nodiscard]] bool ValidateClearRectangle(const TextureDesc& desc, const SubresourceRange& range, const Rect& rectangle) noexcept
        {
            if (range.mipCount != 1 || rectangle.x < 0 || rectangle.y < 0 || rectangle.width <= 0 || rectangle.height <= 0)
                return false;
            const u32 mipWidth = desc.extent.width >> range.firstMip != 0 ? desc.extent.width >> range.firstMip : 1;
            const u32 mipHeight = desc.extent.height >> range.firstMip != 0 ? desc.extent.height >> range.firstMip : 1;
            return static_cast<u32>(rectangle.x) <= mipWidth && static_cast<u32>(rectangle.width) <= mipWidth - rectangle.x &&
                   static_cast<u32>(rectangle.y) <= mipHeight && static_cast<u32>(rectangle.height) <= mipHeight - rectangle.y;
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
            if (HasFlag(state, ResourceState::ResolveSource))
                include(nvrhi::ResourceStates::ResolveSource);
            if (HasFlag(state, ResourceState::ResolveDestination))
                include(nvrhi::ResourceStates::ResolveDest);
            if (HasFlag(state, ResourceState::ShadingRate))
                include(nvrhi::ResourceStates::ShadingRateSurface);
            return static_cast<nvrhi::ResourceStates>(result);
        }

        [[nodiscard]] nvrhi::TextureDesc ToNativeTextureDesc(const TextureDesc& desc, const bool requirementsProbe = false) noexcept
        {
            nvrhi::TextureDesc native{};
            native.width = desc.extent.width;
            native.height = desc.extent.height;
            native.depth = desc.extent.depth;
            native.arraySize = desc.arraySize;
            native.mipLevels = desc.mipCount;
            native.sampleCount = desc.sampleCount;
            native.format = ToNativeFormat(desc.format);
            native.dimension = desc.dimension == TextureDimension::Texture1D
                                   ? (desc.arraySize > 1 ? nvrhi::TextureDimension::Texture1DArray : nvrhi::TextureDimension::Texture1D)
                               : desc.dimension == TextureDimension::Texture3D ? nvrhi::TextureDimension::Texture3D
                               : desc.dimension == TextureDimension::TextureCube
                                   ? (desc.arraySize > 6 ? nvrhi::TextureDimension::TextureCubeArray : nvrhi::TextureDimension::TextureCube)
                               : desc.sampleCount > 1 ? (desc.arraySize > 1 ? nvrhi::TextureDimension::Texture2DMSArray : nvrhi::TextureDimension::Texture2DMS)
                                                      : (desc.arraySize > 1 ? nvrhi::TextureDimension::Texture2DArray : nvrhi::TextureDimension::Texture2D);
            native.isShaderResource = HasFlag(desc.usage, TextureUsage::ShaderResource) || !HasFlag(desc.usage, TextureUsage::DepthStencil);
            native.isRenderTarget = HasFlag(desc.usage, TextureUsage::RenderTarget) || HasFlag(desc.usage, TextureUsage::DepthStencil);
            native.isUAV = HasFlag(desc.usage, TextureUsage::UnorderedAccess);
            native.isShadingRateSurface = HasFlag(desc.usage, TextureUsage::ShadingRate);
            native.isVirtual = requirementsProbe || desc.virtualResource;
            native.initialState = ToNativeState(desc.initialState);
            native.keepInitialState = desc.keepInitialState;
            return native;
        }

        [[nodiscard]] nvrhi::BufferDesc ToNativeBufferDesc(const BufferDesc& desc, const bool requirementsProbe = false) noexcept
        {
            nvrhi::BufferDesc native{};
            native.byteSize = desc.size;
            native.structStride = desc.structureStride;
            native.format = ToNativeFormat(desc.format);
            native.canHaveUAVs = HasFlag(desc.usage, BufferUsage::UnorderedAccess);
            native.canHaveTypedViews = desc.format != Format::Unknown;
            native.canHaveRawViews = HasFlag(desc.usage, BufferUsage::Raw);
            native.isVertexBuffer = HasFlag(desc.usage, BufferUsage::Vertex);
            native.isIndexBuffer = HasFlag(desc.usage, BufferUsage::Index);
            native.isConstantBuffer = HasFlag(desc.usage, BufferUsage::Constant);
            native.isDrawIndirectArgs = HasFlag(desc.usage, BufferUsage::IndirectArguments);
            native.isAccelStructBuildInput = HasFlag(desc.usage, BufferUsage::AccelerationStructure);
            native.isShaderBindingTable = HasFlag(desc.usage, BufferUsage::ShaderBindingTable);
            native.isVirtual = requirementsProbe || desc.virtualResource;
            native.initialState = ToNativeState(desc.initialState);
            native.keepInitialState = desc.keepInitialState;
            native.cpuAccess = desc.memoryType == MemoryType::Upload     ? nvrhi::CpuAccessMode::Write
                               : desc.memoryType == MemoryType::Readback ? nvrhi::CpuAccessMode::Read
                                                                         : nvrhi::CpuAccessMode::None;
            return native;
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

        [[nodiscard]] constexpr bool IsPowerOfTwo(const u64 value) noexcept
        {
            return value != 0 && (value & (value - 1)) == 0;
        }

        [[nodiscard]] constexpr MemoryRequirements CompleteRequirements(const TextureDesc&, const nvrhi::MemoryRequirements& native) noexcept
        {
            return {native.size, native.alignment, DeviceLocalTextureCompatibilityClass, MemoryType::DeviceLocal, PlacedHeapCategory::Texture};
        }

        [[nodiscard]] constexpr MemoryRequirements CompleteRequirements(const BufferDesc& desc, const nvrhi::MemoryRequirements& native) noexcept
        {
            const u64 compatibilityClass = desc.memoryType == MemoryType::Upload     ? UploadBufferCompatibilityClass
                                           : desc.memoryType == MemoryType::Readback ? ReadbackBufferCompatibilityClass
                                                                                     : DeviceLocalBufferCompatibilityClass;
            return {native.size, native.alignment, compatibilityClass, desc.memoryType, PlacedHeapCategory::Buffer};
        }

        [[nodiscard]] BackendStatus ValidatePlacement(const MemoryRequirements& requirements, const HeapDesc& heap, const u64 offset) noexcept
        {
            if (requirements.size == 0 || !IsPowerOfTwo(requirements.alignment) || requirements.compatibilityClass == 0 ||
                requirements.heapCategory == PlacedHeapCategory::None)
                return BackendStatus::Failure(FailureCode::BackendFailure, 0, "backend returned invalid placed-resource requirements");
            if (heap.memoryType != requirements.memoryType)
                return BackendStatus::Failure(FailureCode::IncompatibleBinding, 0, "placed resource and heap memory types do not match");
            if (heap.heapCategory != requirements.heapCategory)
                return BackendStatus::Failure(FailureCode::IncompatibleBinding, 0, "placed resource and heap categories do not match");
            if (heap.compatibilityClass != requirements.compatibilityClass)
                return BackendStatus::Failure(FailureCode::IncompatibleBinding, 0, "placed resource and heap compatibility classes do not match");
            if (!IsPowerOfTwo(heap.alignment) || heap.alignment < requirements.alignment || (offset & (requirements.alignment - 1)) != 0)
                return BackendStatus::Failure(FailureCode::IncompatibleBinding, 0, "placed resource offset or heap alignment is incompatible");
            if (offset > ~u64{0} - requirements.size)
                return BackendStatus::Failure(FailureCode::CapacityExceeded, 0, "placed resource range overflows");
            if (offset + requirements.size > heap.size)
                return BackendStatus::Failure(FailureCode::CapacityExceeded, 0, "placed resource range exceeds heap capacity");
            return BackendStatus::Success();
        }

        [[nodiscard]] bool AcquirePlacementGeneration(concurrency::Atomic<u64>& counter, u64& generation) noexcept
        {
            u64 current = counter.GetValue();
            for (;;)
            {
                if (current == ~u64{0})
                    return false;
                const u64 observed = counter.CompareExchange(current + 1u, current);
                if (observed == current)
                {
                    generation = current + 1u;
                    return true;
                }
                current = observed;
            }
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

        void CopyDebugName(char* const destination, const u32 capacity, const char* const source) noexcept
        {
            if (destination == nullptr || capacity == 0)
                return;
            u32 length = 0;
            if (source != nullptr)
                while (length + 1u < capacity && source[length] != '\0')
                {
                    destination[length] = source[length];
                    ++length;
                }
            destination[length] = '\0';
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
            PlacementRecord placement;
            char debugName[96]{};
        };
        struct TextureReadbackPayload
        {
            nvrhi::StagingTextureHandle native;
            mutable concurrency::SpinLock lock;
            Format format = Format::Unknown;
            Extent3D extent{};
            GpuFence completion;
            TextureReadbackState state = TextureReadbackState::PendingSubmission;
            bool mapped = false;
            char debugName[96]{};
        };

        void DestroyTextureReadback(void* const context, ResourceRef, void* const address) noexcept
        {
            if (address == nullptr)
                return;
            auto& common = *static_cast<CommonBackend*>(context);
            auto* const payload = static_cast<TextureReadbackPayload*>(address);
            if (payload->mapped)
                common.GetDevice()->unmapStagingTexture(payload->native);
            DestroyPayload<TextureReadbackPayload>(nullptr, {}, payload);
        }

        void CompleteTextureReadbackSubmission(void* const context, const GpuFence completion, const u64 value) noexcept
        {
            auto& common = *static_cast<CommonBackend*>(context);
            ResourceRef resource;
            resource.value = value;
            auto* const payload = static_cast<TextureReadbackPayload*>(common.GetResourcePayload(resource));
            if (payload == nullptr)
                return;
            concurrency::ScopedLock guard(payload->lock);
            payload->completion = completion;
            payload->state = completion.IsValid() ? TextureReadbackState::PendingGpu : TextureReadbackState::Failed;
        }
        struct BufferPayload
        {
            nvrhi::BufferHandle native;
            BufferDesc desc;
            PlacementRecord placement;
            char debugName[96]{};
            bool mapped = false;
        };
        struct HeapPayload
        {
            nvrhi::HeapHandle native;
            HeapDesc desc;
            char debugName[96]{};
        };
        struct SamplerPayload
        {
            nvrhi::SamplerHandle native;
            SamplerStateDesc desc;
            char debugName[96]{};
        };
        struct ShaderPayload
        {
            nvrhi::ShaderHandle native;
            ShaderStage stage = ShaderStage::Vertex;
            char debugName[96]{};
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
            char debugName[96]{};
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
            char debugName[96]{};
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
            explicit DescriptorDomainPayload(const DescriptorDomainDesc& source) noexcept : slots(memory::pools::Rendering::GetInstance()), desc(source) {}

            nvrhi::BindingLayoutHandle nativeLayout;
            nvrhi::DescriptorTableHandle nativeTable;
            containers::DynamicArray<DescriptorSlot> slots;
            mutable concurrency::SpinLock lock;
            DescriptorDomainDesc desc;
            DescriptorDomainStats stats;
            u32 freeHead = InvalidReferenceIndex;
            u32 gpuBaseIndex = 0;
        };

        [[nodiscard]] bool ResolveDescriptorSlot(const DescriptorDomainPayload& domain, const DescriptorHandle descriptor, u32& slot) noexcept
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
                    static_cast<void>(common.GetDevice()->writeDescriptorTable(domain.nativeTable, nvrhi::BindingSetItem::None(index)));
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
            domain.stats.free = domain.desc.capacity - domain.stats.allocated - domain.stats.pendingRetirement - domain.stats.exhaustedSlots;
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
            struct SubmissionCallback
            {
                CommandSubmissionCallback callback = nullptr;
                void* context = nullptr;
                u64 value = 0;
            };

            explicit CommandListPayload(nvrhi::CommandListHandle&& commandList, const CommandListType commandListType, const u64 commandListRole) noexcept
                : native(static_cast<nvrhi::CommandListHandle&&>(commandList)), resources(memory::pools::Rendering::GetInstance()),
                  residencyResources(memory::pools::Rendering::GetInstance()), submissionCallbacks(memory::pools::Rendering::GetInstance()),
                  role(commandListRole), type(commandListType)
            {
            }
            nvrhi::CommandListHandle native;
            containers::DynamicArray<ResourceRef> resources;
            containers::DynamicArray<ResourceRef> residencyResources;
            containers::DynamicArray<SubmissionCallback> submissionCallbacks;
            nvrhi::FramebufferHandle framebuffer;
            PipelineRef pipeline;
            ViewportDesc viewport;
            Rect scissors;
            VertexBufferBinding vertexBuffers[MaximumVertexBindings]{};
            IndexBufferBinding indexBuffer;
            BufferRef indirectArguments;
            BufferRef indirectCount;
            u8 pushConstants[nvrhi::c_MaxPushConstantSize]{};
            ColorValue blendFactor{1.0f, 1.0f, 1.0f, 1.0f};
            u32 vertexBufferCount = 0;
            u32 pushConstantSize = 0;
            u32 markerDepth = 0;
            u8 stencilReference = 0;
            u64 role = 0;
            CommandListType type = CommandListType::None;
            bool renderTargetsSet = false;
            bool viewportSet = false;
            bool scissorsSet = false;
            bool indexBufferSet = false;
            bool vertexBufferSet[MaximumVertexBindings]{};
            bool open = true;
            bool entryStatesSeeded = false;
            u64 incomingWaits[3]{};
            bool recycleAfterCompletion = false;
            char debugName[96]{};
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
            bool retained = false;
            for (const ResourceRef tracked : command.resources)
                if (tracked == resource)
                {
                    retained = true;
                    break;
                }
            if (!retained)
            {
                if (!common.IsResourceReferenceValid(resource))
                    return false;
                common.AddRef(resource);
                const u32 expected = command.resources.Size() + 1u;
                command.resources.PushBack(resource);
                if (command.resources.Size() != expected)
                {
                    static_cast<void>(common.Release(resource));
                    return false;
                }
            }

            const ResourceRef allocation = common.GetResidencyAllocation(resource);
            if (!allocation.IsValid())
                return true;
            if (allocation != resource)
            {
                bool allocationRetained = false;
                for (const ResourceRef tracked : command.resources)
                    allocationRetained = allocationRetained || tracked == allocation;
                if (!allocationRetained)
                {
                    if (!common.IsResourceReferenceValid(allocation))
                        return false;
                    common.AddRef(allocation);
                    const u32 expected = command.resources.Size() + 1u;
                    command.resources.PushBack(allocation);
                    if (command.resources.Size() != expected)
                    {
                        static_cast<void>(common.Release(allocation));
                        return false;
                    }
                }
            }
            for (const ResourceRef tracked : command.residencyResources)
                if (tracked == allocation)
                    return true;
            const u32 expected = command.residencyResources.Size() + 1u;
            command.residencyResources.PushBack(allocation);
            return command.residencyResources.Size() == expected;
        }

        [[nodiscard]] bool PopulatePipelineBindings(CommonBackend& common, PipelinePayload& pipeline, const BindingLayoutRef* const bindingLayouts,
                                                    const u32 bindingLayoutCount, const DescriptorDomainRef* const descriptorDomains,
                                                    const u32 descriptorDomainCount) noexcept
        {
            pipeline.fixedBindingCount = bindingLayoutCount;
            for (u32 index = 0; index < bindingLayoutCount; ++index)
            {
                const auto* const layout = static_cast<const BindingLayoutPayload*>(common.GetResourcePayload(ResourceRef(bindingLayouts[index])));
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
                    pipeline.fixedBindings[index] = common.GetDevice()->createBindingSet(bindingSetDesc, layout->native);
                    if (!pipeline.fixedBindings[index])
                        return false;
                }
            }
            pipeline.descriptorDomainCount = 0;
            for (u32 index = 0; index < descriptorDomainCount; ++index)
            {
                const auto* const domain = static_cast<const DescriptorDomainPayload*>(common.GetResourcePayload(ResourceRef(descriptorDomains[index])));
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
                       ? BackendStatus::Failure(FailureCode::IncompatibleBinding, 0, "push constants were supplied to a pipeline that does not declare them")
                       : BackendStatus::Failure(FailureCode::MissingBinding, 0, "the pipeline's complete push-constant block was not supplied");
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
            state.blendConstantColor = nvrhi::Color(command.blendFactor.red, command.blendFactor.green, command.blendFactor.blue, command.blendFactor.alpha);
            state.dynamicStencilRefValue = command.stencilReference;
            state.viewport.addViewport(nvrhi::Viewport(command.viewport.x, command.viewport.x + command.viewport.width, command.viewport.y,
                                                       command.viewport.y + command.viewport.height, command.viewport.minimumDepth,
                                                       command.viewport.maximumDepth));
            if (command.scissorsSet)
                state.viewport.addScissorRect(nvrhi::Rect(command.scissors.x, command.scissors.x + command.scissors.width, command.scissors.y,
                                                          command.scissors.y + command.scissors.height));
            else
                state.viewport.addScissorRect(nvrhi::Rect(static_cast<i32>(command.viewport.x), static_cast<i32>(command.viewport.x + command.viewport.width),
                                                          static_cast<i32>(command.viewport.y),
                                                          static_cast<i32>(command.viewport.y + command.viewport.height)));
            status = PopulateBoundResources(*pipeline, state.bindings);
            if (!status)
                return status;
            for (u32 index = 0; index < command.vertexBufferCount; ++index)
            {
                if (!command.vertexBufferSet[index])
                    continue;
                const auto* const buffer = static_cast<const BufferPayload*>(common.GetResourcePayload(ResourceRef(command.vertexBuffers[index].buffer)));
                if (buffer == nullptr)
                    return BackendStatus::Failure(FailureCode::InvalidReference, 0, "a bound vertex buffer became invalid");
                state.vertexBuffers.push_back(
                    nvrhi::VertexBufferBinding().setBuffer(buffer->native).setSlot(index).setOffset(command.vertexBuffers[index].offset));
            }
            if (command.indexBufferSet)
            {
                const auto* const buffer = static_cast<const BufferPayload*>(common.GetResourcePayload(ResourceRef(command.indexBuffer.buffer)));
                if (buffer == nullptr)
                    return BackendStatus::Failure(FailureCode::InvalidReference, 0, "the bound index buffer became invalid");
                state.indexBuffer = nvrhi::IndexBufferBinding()
                                        .setBuffer(buffer->native)
                                        .setFormat(command.indexBuffer.format == IndexFormat::UInt32 ? nvrhi::Format::R32_UINT : nvrhi::Format::R16_UINT)
                                        .setOffset(static_cast<u32>(command.indexBuffer.offset));
            }
            if (command.indirectArguments)
            {
                const auto* const arguments = static_cast<const BufferPayload*>(common.GetResourcePayload(ResourceRef(command.indirectArguments)));
                const auto* const count =
                    command.indirectCount ? static_cast<const BufferPayload*>(common.GetResourcePayload(ResourceRef(command.indirectCount))) : nullptr;
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
                const auto* const arguments = static_cast<const BufferPayload*>(common.GetResourcePayload(ResourceRef(command.indirectArguments)));
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

    nvrhi::CommandListHandle CommonBackend::AcquireNativeCommandList(const CommandListType type, const u64 role) noexcept
    {
        {
            concurrency::ScopedLock poolGuard(m_commandListPoolLock);
            u32 selected = InvalidReferenceIndex;
            u64 newestStamp = 0;
            for (u32 index = 0; index < m_recycledCommandListCount; ++index)
            {
                const RecycledCommandList& entry = m_recycledCommandLists[index];
                if (entry.type != type || entry.role != role)
                    continue;
                if (selected == InvalidReferenceIndex || entry.stamp > newestStamp)
                {
                    selected = index;
                    newestStamp = entry.stamp;
                }
            }
            if (selected != InvalidReferenceIndex)
            {
                RecycledCommandList& entry = m_recycledCommandLists[selected];
                nvrhi::CommandListHandle native = static_cast<nvrhi::CommandListHandle&&>(entry.native);
                --m_recycledCommandListCount;
                if (selected != m_recycledCommandListCount)
                    entry = static_cast<RecycledCommandList&&>(m_recycledCommandLists[m_recycledCommandListCount]);
                m_recycledCommandLists[m_recycledCommandListCount] = {};
                return native;
            }
        }

        nvrhi::CommandListParameters parameters{};
        parameters.enableImmediateExecution = false;
        parameters.queueType = ToNativeQueue(type);
        return m_device->createCommandList(parameters);
    }

    void CommonBackend::RecycleNativeCommandList(nvrhi::CommandListHandle&& native, const CommandListType type, const u64 role) noexcept
    {
        if (!native || type == CommandListType::None)
            return;
        nvrhi::CommandListHandle evicted;
        {
            concurrency::ScopedLock poolGuard(m_commandListPoolLock);
            u32 matchingRoleCount = 0;
            u32 oldestMatching = InvalidReferenceIndex;
            u32 oldestOverall = InvalidReferenceIndex;
            for (u32 index = 0; index < m_recycledCommandListCount; ++index)
            {
                const RecycledCommandList& entry = m_recycledCommandLists[index];
                if (oldestOverall == InvalidReferenceIndex || entry.stamp < m_recycledCommandLists[oldestOverall].stamp)
                    oldestOverall = index;
                if (entry.type != type || entry.role != role)
                    continue;
                ++matchingRoleCount;
                if (oldestMatching == InvalidReferenceIndex || entry.stamp < m_recycledCommandLists[oldestMatching].stamp)
                    oldestMatching = index;
            }

            u32 destination = InvalidReferenceIndex;
            if (matchingRoleCount >= MaximumRecycledCommandListsPerRole)
                destination = oldestMatching;
            else if (m_recycledCommandListCount < MaximumRecycledCommandLists)
                destination = m_recycledCommandListCount++;
            else
                destination = oldestOverall;

            VG_ASSERT_MSG(destination != InvalidReferenceIndex, "command-list recycling could not select a bounded pool destination");
            if (destination == InvalidReferenceIndex)
                return;
            RecycledCommandList& entry = m_recycledCommandLists[destination];
            evicted = static_cast<nvrhi::CommandListHandle&&>(entry.native);
            entry.native = static_cast<nvrhi::CommandListHandle&&>(native);
            entry.role = role;
            entry.stamp = ++m_commandListRecycleStamp;
            entry.type = type;
        }
    }

    void CommonBackend::ClearRecycledCommandLists() noexcept
    {
        concurrency::ScopedLock poolGuard(m_commandListPoolLock);
        for (u32 index = 0; index < m_recycledCommandListCount; ++index)
            m_recycledCommandLists[index] = {};
        m_recycledCommandListCount = 0;
        m_commandListRecycleStamp = 0;
    }

    bool CommonBackend::Initialize(nvrhi::DeviceHandle&& device, const FenceCompleteCallback fenceComplete, const SignalFenceCallback signalFence,
                                   const WaitFenceCallback waitFence, const QueueWaitCallback queueWait, const AliasingBarrierCallback aliasingBarrier,
                                   const RectColorClearCallback rectColorClear, const RectDepthStencilClearCallback rectDepthStencilClear,
                                   const DiscardTextureCallback discardTexture, const PrepareResidencyCallback prepareResidency,
                                   const CommitResidencyCallback commitResidency, const ReleaseResidencyCallback releaseResidency,
                                   void* const fenceContext) noexcept
    {
        if (IsInitialized() || !device || fenceComplete == nullptr || signalFence == nullptr || waitFence == nullptr || queueWait == nullptr || aliasingBarrier == nullptr ||
            rectColorClear == nullptr || rectDepthStencilClear == nullptr || discardTexture == nullptr || prepareResidency == nullptr ||
            commitResidency == nullptr || releaseResidency == nullptr)
            return false;
        ClearRecycledCommandLists();
        m_device = static_cast<nvrhi::DeviceHandle&&>(device);
        if (!m_lifetime.Initialize({}, fenceComplete, fenceContext, releaseResidency, fenceContext))
        {
            m_device = nullptr;
            return false;
        }
        m_submittedFences[0].SetValue(0);
        m_submittedFences[1].SetValue(0);
        m_submittedFences[2].SetValue(0);
        for (u64& value : m_signaledFences)
            value = 0;
        m_submittedInstances[0] = 0;
        m_submittedInstances[1] = 0;
        m_submittedInstances[2] = 0;
        m_nextPlacementGeneration.SetValue(0);
        m_fenceComplete = fenceComplete;
        m_signalFence = signalFence;
        m_waitFence = waitFence;
        m_queueWait = queueWait;
        m_aliasingBarrier = aliasingBarrier;
        m_rectColorClear = rectColorClear;
        m_rectDepthStencilClear = rectDepthStencilClear;
        m_discardTexture = discardTexture;
        m_prepareResidency = prepareResidency;
        m_commitResidency = commitResidency;
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
        ClearRecycledCommandLists();
        m_device = nullptr;
        m_fenceComplete = nullptr;
        m_signalFence = nullptr;
        m_waitFence = nullptr;
        m_queueWait = nullptr;
        m_aliasingBarrier = nullptr;
        m_rectColorClear = nullptr;
        m_rectDepthStencilClear = nullptr;
        m_discardTexture = nullptr;
        m_prepareResidency = nullptr;
        m_commitResidency = nullptr;
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
        ClearRecycledCommandLists();
        m_device = nullptr;
        m_fenceComplete = nullptr;
        m_signalFence = nullptr;
        m_waitFence = nullptr;
        m_queueWait = nullptr;
        m_aliasingBarrier = nullptr;
        m_rectColorClear = nullptr;
        m_rectDepthStencilClear = nullptr;
        m_discardTexture = nullptr;
        m_prepareResidency = nullptr;
        m_commitResidency = nullptr;
        m_fenceContext = nullptr;
    }

    bool CommonBackend::IsInitialized() const noexcept
    {
        return m_device != nullptr;
    }
    nvrhi::IDevice* CommonBackend::GetDevice() const noexcept
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
        capabilities.rayTracing = m_device->queryFeatureSupport(nvrhi::Feature::RayTracingAccelStruct);
        capabilities.rayTracingPipeline = m_device->queryFeatureSupport(nvrhi::Feature::RayTracingPipeline);
        capabilities.meshShaders = m_device->queryFeatureSupport(nvrhi::Feature::Meshlets);
        capabilities.variableRateShading = m_device->queryFeatureSupport(nvrhi::Feature::VariableRateShading);
    }

    bool CommonBackend::WaitIdle() noexcept
    {
        if (!IsInitialized())
            return false;
#if VG_BUILD_DEBUG
        const u64 start = system::GetMonotonicTicks();
#endif
        concurrency::ScopedLock submissionGuard(m_submissionLock);
#if VG_BUILD_DEBUG
        const u64 acquired = system::GetMonotonicTicks();
#endif
        const bool result = m_device->waitForIdle();
#if VG_BUILD_DEBUG
        const double ticksPerMillisecond = double(system::GetMonotonicFrequency()) / 1000.0;
        const double lockMs = double(acquired - start) / ticksPerMillisecond;
        const double gpuMs = double(system::GetMonotonicTicks() - acquired) / ticksPerMillisecond;
        if (lockMs + gpuMs >= 100.0)
            VG_LOG_WARNING(diagnostics::Category::Rendering, "GPU idle stall: lock=%.2f ms gpu=%.2f ms mainThread=%u", lockMs, gpuMs, u32(concurrency::IsMainThread()));
#endif
        return result;
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

    BackendStatus CommonBackend::CreateTexture(const TextureDesc& desc, const TextureInitData& initialData, TextureRef& texture) noexcept
    {
        texture = {};
        const nvrhi::TextureDesc nativeDesc = ToNativeTextureDesc(desc);
        nvrhi::TextureHandle native = m_device->createTexture(nativeDesc);
        if (!native)
            return BackendStatus::Failure(FailureCode::BackendFailure, 0, "NVRHI failed to create texture");
        TexturePayload* const payload = AllocatePayload<TexturePayload>(TexturePayload{static_cast<nvrhi::TextureHandle&&>(native), desc});
        if (payload == nullptr)
            return BackendStatus::Failure(FailureCode::OutOfMemory, 0, "failed to allocate texture lifetime payload");
        const ResourceRef resource = m_lifetime.Create(
            ResourceKind::Texture, payload,
            [](void* context, ResourceRef, void* address) noexcept
            {
                auto& common = *static_cast<CommonBackend*>(context);
                auto* const texture = static_cast<TexturePayload*>(address);
                const HeapRef heap = texture->placement.heap;
                texture->native = nullptr;
                if (heap)
                    static_cast<void>(common.Release(ResourceRef(heap)));
                DestroyPayload<TexturePayload>(nullptr, {}, texture);
            },
            this);
        if (!resource)
        {
            DestroyPayload<TexturePayload>(nullptr, {}, payload);
            return BackendStatus::Failure(FailureCode::CapacityExceeded, 0, "texture lifetime table is full");
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
                return BackendStatus::Failure(FailureCode::BackendFailure, 0, "failed to create texture upload command list");
            }
            upload->open();
            if (!desc.keepInitialState)
                upload->beginTrackingTextureState(payload->native, nvrhi::AllSubresources, ToNativeState(desc.initialState));
            for (u32 index = 0; index < initialData.subresourceCount; ++index)
            {
                const TextureSubresourceData& subresource = initialData.subresources[index];
                upload->writeTexture(payload->native, subresource.arraySlice, subresource.mipLevel, subresource.data, static_cast<usize>(subresource.rowPitch),
                                     static_cast<usize>(subresource.depthPitch));
            }
            if (!desc.keepInitialState)
            {
                upload->setTextureState(payload->native, nvrhi::AllSubresources, ToNativeState(desc.initialState));
                upload->commitBarriers();
            }
            upload->close();
            concurrency::ScopedLock submissionGuard(m_submissionLock);
            // A later compute-only ForkAsyncCompute waits on this NVRHI queue instance.
            m_submittedInstances[QueueIndex(QueueType::Graphics)] = m_device->executeCommandList(upload, nvrhi::CommandQueue::Graphics);
            const u64 fenceValue = m_submittedFences[0].Increment();
            const bool fenceSignaled = m_signalFence(m_fenceContext, QueueType::Graphics, fenceValue);
            if (!fenceSignaled)
            {
                static_cast<void>(m_device->waitForIdle());
                static_cast<void>(m_lifetime.Release(resource));
                return BackendStatus::Failure(FailureCode::DeviceLost, 0, "failed to signal texture upload fence");
            }
            m_signaledFences[QueueIndex(QueueType::Graphics)] = fenceValue;
            static_cast<void>(m_lifetime.RecordUse(resource, QueueType::Graphics, fenceValue));
        }
        texture = CastResourceRef<TextureRef>(resource);
        return BackendStatus::Success();
    }

    TextureRef CommonBackend::AdoptNativeTexture(nvrhi::TextureHandle&& native, const TextureDesc& desc) noexcept
    {
        if (!native || native->getDesc().keepInitialState != desc.keepInitialState)
            return {};
        TexturePayload* const payload = AllocatePayload<TexturePayload>(TexturePayload{static_cast<nvrhi::TextureHandle&&>(native), desc});
        if (payload == nullptr)
            return {};
        const ResourceRef resource = m_lifetime.Create(
            ResourceKind::Texture, payload,
            [](void* context, ResourceRef, void* address) noexcept
            {
                auto& common = *static_cast<CommonBackend*>(context);
                auto* const texture = static_cast<TexturePayload*>(address);
                const HeapRef heap = texture->placement.heap;
                texture->native = nullptr;
                if (heap)
                    static_cast<void>(common.Release(ResourceRef(heap)));
                DestroyPayload<TexturePayload>(nullptr, {}, texture);
            },
            this);
        if (!resource)
        {
            DestroyPayload<TexturePayload>(nullptr, {}, payload);
            return {};
        }
        return CastResourceRef<TextureRef>(resource);
    }

    ResourceRef CommonBackend::CreateBackendResource(const ResourceKind kind, void* const payload, const DestroyResourceCallback destroy,
                                                     void* const destroyContext) noexcept
    {
        return m_lifetime.Create(kind, payload, destroy, destroyContext);
    }

    BackendStatus CommonBackend::CreateBuffer(const BufferDesc& desc, const BufferInitData& initialData, BufferRef& buffer) noexcept
    {
        buffer = {};
        if (!desc.keepInitialState && (desc.memoryType != MemoryType::DeviceLocal || desc.initialState != ResourceState::Common))
            return BackendStatus::Failure(FailureCode::Unsupported, 0, "explicit buffer tracking requires device-local Common creation state");
        const nvrhi::BufferDesc nativeDesc = ToNativeBufferDesc(desc);
        nvrhi::BufferHandle native = m_device->createBuffer(nativeDesc);
        if (!native)
            return BackendStatus::Failure(FailureCode::BackendFailure, 0, "NVRHI failed to create buffer");
        BufferPayload* const payload = AllocatePayload<BufferPayload>(BufferPayload{static_cast<nvrhi::BufferHandle&&>(native), desc});
        if (payload == nullptr)
            return BackendStatus::Failure(FailureCode::OutOfMemory, 0, "failed to allocate buffer lifetime payload");
        const ResourceRef resource = m_lifetime.Create(
            ResourceKind::Buffer, payload,
            [](void* context, ResourceRef, void* address) noexcept
            {
                auto& common = *static_cast<CommonBackend*>(context);
                auto* const buffer = static_cast<BufferPayload*>(address);
                if (buffer->mapped)
                    common.GetDevice()->unmapBuffer(buffer->native);
                const HeapRef heap = buffer->placement.heap;
                buffer->native = nullptr;
                if (heap)
                    static_cast<void>(common.Release(ResourceRef(heap)));
                DestroyPayload<BufferPayload>(nullptr, {}, buffer);
            },
            this);
        if (!resource)
        {
            DestroyPayload<BufferPayload>(nullptr, {}, payload);
            return BackendStatus::Failure(FailureCode::CapacityExceeded, 0, "buffer lifetime table is full");
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
                return BackendStatus::Failure(FailureCode::BackendFailure, 0, "failed to create buffer upload command list");
            }
            upload->open();
            if (!desc.keepInitialState)
                upload->beginTrackingBufferState(payload->native, ToNativeState(desc.initialState));
            upload->writeBuffer(payload->native, initialData.data, static_cast<usize>(initialData.size));
            if (!desc.keepInitialState)
            {
                upload->setBufferState(payload->native, ToNativeState(desc.initialState));
                upload->commitBarriers();
            }
            upload->close();
            concurrency::ScopedLock submissionGuard(m_submissionLock);
            // A later compute-only ForkAsyncCompute waits on this NVRHI queue instance.
            m_submittedInstances[QueueIndex(QueueType::Graphics)] = m_device->executeCommandList(upload, nvrhi::CommandQueue::Graphics);
            const u64 fenceValue = m_submittedFences[0].Increment();
            const bool fenceSignaled = m_signalFence(m_fenceContext, QueueType::Graphics, fenceValue);
            if (!fenceSignaled)
            {
                static_cast<void>(m_device->waitForIdle());
                static_cast<void>(m_lifetime.Release(resource));
                return BackendStatus::Failure(FailureCode::DeviceLost, 0, "failed to signal buffer upload fence");
            }
            m_signaledFences[QueueIndex(QueueType::Graphics)] = fenceValue;
            static_cast<void>(m_lifetime.RecordUse(resource, QueueType::Graphics, fenceValue));
        }
        buffer = CastResourceRef<BufferRef>(resource);
        return BackendStatus::Success();
    }

    BackendStatus CommonBackend::CreateHeap(const HeapDesc& desc, HeapRef& heap) noexcept
    {
        heap = {};
        nvrhi::HeapDesc nativeDesc{};
        nativeDesc.capacity = desc.size;
        nativeDesc.type = desc.memoryType == MemoryType::Upload     ? nvrhi::HeapType::Upload
                          : desc.memoryType == MemoryType::Readback ? nvrhi::HeapType::Readback
                                                                    : nvrhi::HeapType::DeviceLocal;
        nvrhi::HeapHandle native = m_device->createHeap(nativeDesc);
        if (!native)
            return BackendStatus::Failure(FailureCode::BackendFailure, 0, "NVRHI failed to create placed-resource heap");
        HeapPayload* const payload = AllocatePayload<HeapPayload>(HeapPayload{static_cast<nvrhi::HeapHandle&&>(native), desc});
        if (payload == nullptr)
            return BackendStatus::Failure(FailureCode::OutOfMemory, 0, "failed to allocate heap lifetime payload");
        const ResourceRef resource = m_lifetime.Create(ResourceKind::Heap, payload, &DestroyPayload<HeapPayload>);
        if (!resource)
        {
            DestroyPayload<HeapPayload>(nullptr, {}, payload);
            return BackendStatus::Failure(FailureCode::CapacityExceeded, 0, "heap lifetime table is full");
        }
        heap = CastResourceRef<HeapRef>(resource);
        return BackendStatus::Success();
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
                if (payload->entries[entry].slot != desc.entries[entry].slot || payload->entries[entry].arrayCount != desc.entries[entry].arrayCount ||
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
        const ResourceRef resource = m_lifetime.Create(ResourceKind::BindingLayout, payload, &DestroyPayload<BindingLayoutPayload>, nullptr, 2);
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
        domain->stats.free = domain->desc.capacity - domain->stats.allocated - domain->stats.pendingRetirement - domain->stats.exhaustedSlots;
        return {domain->gpuBaseIndex + index, slot.generation};
    }

    BackendStatus CommonBackend::WriteDescriptor(const DescriptorDomainRef domainRef, const DescriptorHandle descriptor, const TextureRef textureRef,
                                                 const BindingType type, const TextureViewDesc& view) noexcept
    {
        auto* const domain = static_cast<DescriptorDomainPayload*>(m_lifetime.GetPayload(ResourceRef(domainRef)));
        auto* const texture = static_cast<TexturePayload*>(m_lifetime.GetPayload(ResourceRef(textureRef)));
        if (domain == nullptr || texture == nullptr)
            return BackendStatus::Failure(FailureCode::InvalidReference, 0, "stale descriptor domain or texture reference");
        const bool shaderResource = type == BindingType::TextureShaderResource;
        const bool unorderedAccess = type == BindingType::TextureUnorderedAccess;
        if ((!shaderResource && !unorderedAccess) || (shaderResource && !HasFlag(texture->desc.usage, TextureUsage::ShaderResource)) ||
            (unorderedAccess && !HasFlag(texture->desc.usage, TextureUsage::UnorderedAccess)) || view.subresources.firstMip >= texture->desc.mipCount ||
            view.subresources.firstSlice >= texture->desc.arraySize)
            return BackendStatus::Failure(FailureCode::InvalidArgument, 0, "texture descriptor view is incompatible with the texture");
        const u32 mipCount =
            view.subresources.mipCount == 0xffffu ? static_cast<u32>(texture->desc.mipCount) - view.subresources.firstMip : view.subresources.mipCount;
        const u32 sliceCount =
            view.subresources.sliceCount == 0xffffu ? static_cast<u32>(texture->desc.arraySize) - view.subresources.firstSlice : view.subresources.sliceCount;
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
        item.subresources = {view.subresources.firstMip,
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

    BackendStatus CommonBackend::WriteDescriptor(const DescriptorDomainRef domainRef, const DescriptorHandle descriptor, const BufferRef bufferRef,
                                                 const BindingType type, const BufferViewDesc& view) noexcept
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
        const bool unorderedAccess = type == BindingType::TypedBufferUnorderedAccess || type == BindingType::StructuredBufferUnorderedAccess ||
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
        ShaderPayload* const payload = AllocatePayload<ShaderPayload>(ShaderPayload{static_cast<nvrhi::ShaderHandle&&>(native), desc.stage});
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
            if (payload == nullptr || payload->bindingCount != canonical.bindingCount || payload->attributeCount != canonical.attributeCount)
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
                equal = left.location == right.location && left.binding == right.binding && left.offset == right.offset && left.format == right.format &&
                        left.semanticIndex == right.semanticIndex && StringsEqual(payload->attributes[attribute].semanticName, right.semanticName);
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
        if (!nativeDesc.VS || (desc.hullShader && !nativeDesc.HS) || (desc.domainShader && !nativeDesc.DS) || (desc.geometryShader && !nativeDesc.GS) ||
            (desc.pixelShader && !nativeDesc.PS) || static_cast<bool>(desc.hullShader) != static_cast<bool>(desc.domainShader))
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
                while (position != 0 && source->attributes[order[position - 1u]].desc.location > source->attributes[order[position]].desc.location)
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
                    if (next.desc.semanticIndex != arraySize || next.desc.binding != first.desc.binding || next.desc.format != first.desc.format ||
                        next.desc.offset != first.desc.offset + arraySize * elementBytes)
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
            const auto* const domain = static_cast<const DescriptorDomainPayload*>(m_lifetime.GetPayload(ResourceRef(desc.descriptorDomains[index])));
            if (domain == nullptr)
                return {};
            nativeDesc.bindingLayouts.push_back(domain->nativeLayout);
        }

        nvrhi::RasterState& raster = nativeDesc.renderState.rasterState;
        raster.fillMode = desc.rasterizer.fill == RasterFillMode::Wireframe ? nvrhi::RasterFillMode::Wireframe : nvrhi::RasterFillMode::Solid;
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
        if (!PopulatePipelineBindings(*this, *payload, desc.bindingLayouts, desc.bindingLayoutCount, desc.descriptorDomains, desc.descriptorDomainCount))
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
            const auto* const layout = static_cast<const BindingLayoutPayload*>(m_lifetime.GetPayload(ResourceRef(desc.bindingLayouts[index])));
            if (layout == nullptr)
                return {};
            nativeDesc.bindingLayouts.push_back(layout->native);
        }
        for (u32 index = 0; index < desc.descriptorDomainCount; ++index)
        {
            const auto* const domain = static_cast<const DescriptorDomainPayload*>(m_lifetime.GetPayload(ResourceRef(desc.descriptorDomains[index])));
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
        if (!PopulatePipelineBindings(*this, *payload, desc.bindingLayouts, desc.bindingLayoutCount, desc.descriptorDomains, desc.descriptorDomainCount))
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
            const auto* const domain = static_cast<const DescriptorDomainPayload*>(m_lifetime.GetPayload(ResourceRef(desc.descriptorDomains[index])));
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
            if ((closest != nullptr && closest->stage != ShaderStage::ClosestHit) || (any != nullptr && any->stage != ShaderStage::AnyHit) ||
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
    BackendStatus CommonBackend::BindMemory(const TextureRef texture, const HeapRef heap, const u64 offset) noexcept
    {
        auto* const texturePayload = static_cast<TexturePayload*>(m_lifetime.GetPayload(ResourceRef(texture)));
        auto* const heapPayload = static_cast<HeapPayload*>(m_lifetime.GetPayload(ResourceRef(heap)));
        if (texturePayload == nullptr || heapPayload == nullptr)
            return BackendStatus::Failure(FailureCode::InvalidReference, 0, "invalid texture or heap reference");
        if (!texturePayload->desc.virtualResource)
            return BackendStatus::Failure(FailureCode::IncompatibleBinding, 0, "only deferred-binding textures may be placed in a heap");
        if (texturePayload->placement.IsValid())
            return BackendStatus::Failure(FailureCode::InvalidArgument, 0, "texture memory is already bound");
        const MemoryRequirements requirements = CompleteRequirements(texturePayload->desc, m_device->getTextureMemoryRequirements(texturePayload->native));
        const BackendStatus validation = ValidatePlacement(requirements, heapPayload->desc, offset);
        if (!validation)
            return validation;
        u64 placementGeneration = 0;
        if (!AcquirePlacementGeneration(m_nextPlacementGeneration, placementGeneration))
            return BackendStatus::Failure(FailureCode::CapacityExceeded, 0, "placed-resource generation space is exhausted");
        static_cast<void>(m_lifetime.AddRef(ResourceRef(heap)));
        if (!m_device->bindTextureMemory(texturePayload->native, heapPayload->native, offset))
        {
            static_cast<void>(m_lifetime.Release(ResourceRef(heap)));
            return BackendStatus::Failure(FailureCode::BackendFailure, 0, "NVRHI failed to bind texture memory");
        }
        texturePayload->placement = {heap, offset, requirements.size, requirements.alignment, requirements.compatibilityClass, placementGeneration, requirements.memoryType, requirements.heapCategory};
        return BackendStatus::Success();
    }
    BackendStatus CommonBackend::BindMemory(const BufferRef buffer, const HeapRef heap, const u64 offset) noexcept
    {
        auto* const bufferPayload = static_cast<BufferPayload*>(m_lifetime.GetPayload(ResourceRef(buffer)));
        auto* const heapPayload = static_cast<HeapPayload*>(m_lifetime.GetPayload(ResourceRef(heap)));
        if (bufferPayload == nullptr || heapPayload == nullptr)
            return BackendStatus::Failure(FailureCode::InvalidReference, 0, "invalid buffer or heap reference");
        if (!bufferPayload->desc.virtualResource)
            return BackendStatus::Failure(FailureCode::IncompatibleBinding, 0, "only deferred-binding buffers may be placed in a heap");
        if (bufferPayload->placement.IsValid())
            return BackendStatus::Failure(FailureCode::InvalidArgument, 0, "buffer memory is already bound");
        const MemoryRequirements requirements = CompleteRequirements(bufferPayload->desc, m_device->getBufferMemoryRequirements(bufferPayload->native));
        const BackendStatus validation = ValidatePlacement(requirements, heapPayload->desc, offset);
        if (!validation)
            return validation;
        u64 placementGeneration = 0;
        if (!AcquirePlacementGeneration(m_nextPlacementGeneration, placementGeneration))
            return BackendStatus::Failure(FailureCode::CapacityExceeded, 0, "placed-resource generation space is exhausted");
        static_cast<void>(m_lifetime.AddRef(ResourceRef(heap)));
        if (!m_device->bindBufferMemory(bufferPayload->native, heapPayload->native, offset))
        {
            static_cast<void>(m_lifetime.Release(ResourceRef(heap)));
            return BackendStatus::Failure(FailureCode::BackendFailure, 0, "NVRHI failed to bind buffer memory");
        }
        bufferPayload->placement = {heap, offset, requirements.size, requirements.alignment, requirements.compatibilityClass, placementGeneration, requirements.memoryType, requirements.heapCategory};
        return BackendStatus::Success();
    }
    BackendStatus CommonBackend::GetHeapDesc(const HeapRef heap, HeapDesc& desc) const noexcept
    {
        desc = {};
        const auto* const payload = static_cast<const HeapPayload*>(m_lifetime.GetPayload(ResourceRef(heap)));
        if (payload == nullptr)
            return BackendStatus::Failure(FailureCode::InvalidReference, 0, "invalid heap reference");
        desc = payload->desc;
        return BackendStatus::Success();
    }
    BackendStatus CommonBackend::GetPlacement(const TextureRef texture, PlacementRecord& placement) const noexcept
    {
        placement = {};
        const auto* const payload = static_cast<const TexturePayload*>(m_lifetime.GetPayload(ResourceRef(texture)));
        if (payload == nullptr)
            return BackendStatus::Failure(FailureCode::InvalidReference, 0, "invalid texture reference");
        if (!payload->placement.IsValid())
            return BackendStatus::Failure(FailureCode::MissingBinding, 0, "texture has no immutable heap placement");
        placement = payload->placement;
        return BackendStatus::Success();
    }
    BackendStatus CommonBackend::GetPlacement(const BufferRef buffer, PlacementRecord& placement) const noexcept
    {
        placement = {};
        const auto* const payload = static_cast<const BufferPayload*>(m_lifetime.GetPayload(ResourceRef(buffer)));
        if (payload == nullptr)
            return BackendStatus::Failure(FailureCode::InvalidReference, 0, "invalid buffer reference");
        if (!payload->placement.IsValid())
            return BackendStatus::Failure(FailureCode::MissingBinding, 0, "buffer has no immutable heap placement");
        placement = payload->placement;
        return BackendStatus::Success();
    }
    MemoryRequirements CommonBackend::GetMemoryRequirements(const TextureRef texture) const noexcept
    {
        const auto* const payload = static_cast<const TexturePayload*>(m_lifetime.GetPayload(ResourceRef(texture)));
        if (payload == nullptr)
            return {};
        const nvrhi::MemoryRequirements requirements = m_device->getTextureMemoryRequirements(payload->native);
        return CompleteRequirements(payload->desc, requirements);
    }
    MemoryRequirements CommonBackend::GetMemoryRequirements(const BufferRef buffer) const noexcept
    {
        const auto* const payload = static_cast<const BufferPayload*>(m_lifetime.GetPayload(ResourceRef(buffer)));
        if (payload == nullptr)
            return {};
        const nvrhi::MemoryRequirements requirements = m_device->getBufferMemoryRequirements(payload->native);
        return CompleteRequirements(payload->desc, requirements);
    }
    BackendStatus CommonBackend::GetTextureDesc(const TextureRef texture, TextureDesc& desc) const noexcept
    {
        desc = {};
        if (!m_lifetime.IsValid(ResourceRef(texture)))
            return BackendStatus::Failure(FailureCode::InvalidReference, 0, "invalid texture reference");
        const auto* const payload = static_cast<const TexturePayload*>(m_lifetime.GetPayload(ResourceRef(texture)));
        if (payload == nullptr)
            return BackendStatus::Failure(FailureCode::InvalidReference, 0, "invalid texture reference");
        desc = payload->desc;
        return BackendStatus::Success();
    }
    BackendStatus CommonBackend::GetBufferDesc(const BufferRef buffer, BufferDesc& desc) const noexcept
    {
        desc = {};
        if (!m_lifetime.IsValid(ResourceRef(buffer)))
            return BackendStatus::Failure(FailureCode::InvalidReference, 0, "invalid buffer reference");
        const auto* const payload = static_cast<const BufferPayload*>(m_lifetime.GetPayload(ResourceRef(buffer)));
        if (payload == nullptr)
            return BackendStatus::Failure(FailureCode::InvalidReference, 0, "invalid buffer reference");
        desc = payload->desc;
        return BackendStatus::Success();
    }
    BackendStatus CommonBackend::GetMemoryRequirements(const TextureDesc& desc, MemoryRequirements& requirements) const noexcept
    {
        requirements = {};
        // A virtual NVRHI wrapper stores the exact D3D12 resource description
        // but deliberately creates no native resource or backing allocation.
        const nvrhi::TextureDesc nativeDesc = ToNativeTextureDesc(desc, true);
        nvrhi::TextureHandle probe = m_device->createTexture(nativeDesc);
        if (!probe)
            return BackendStatus::Failure(FailureCode::Unsupported, 0, "backend cannot query texture allocation requirements");
        const nvrhi::MemoryRequirements native = m_device->getTextureMemoryRequirements(probe);
        requirements = CompleteRequirements(desc, native);
        return native.size != 0 && native.alignment != 0
                   ? BackendStatus::Success()
                   : BackendStatus::Failure(FailureCode::BackendFailure, 0, "backend returned empty texture requirements");
    }
    BackendStatus CommonBackend::GetMemoryRequirements(const BufferDesc& desc, MemoryRequirements& requirements) const noexcept
    {
        requirements = {};
        const nvrhi::BufferDesc nativeDesc = ToNativeBufferDesc(desc, true);
        nvrhi::BufferHandle probe = m_device->createBuffer(nativeDesc);
        if (!probe)
            return BackendStatus::Failure(FailureCode::Unsupported, 0, "backend cannot query buffer allocation requirements");
        const nvrhi::MemoryRequirements native = m_device->getBufferMemoryRequirements(probe);
        requirements = CompleteRequirements(desc, native);
        return native.size != 0 && native.alignment != 0 ? BackendStatus::Success()
                                                         : BackendStatus::Failure(FailureCode::BackendFailure, 0, "backend returned empty buffer requirements");
    }
    BackendStatus CommonBackend::ValidateNativeReleaseObservation(const ResourceRef resource) const noexcept
    {
        if (!m_lifetime.IsValid(resource))
            return BackendStatus::Failure(FailureCode::InvalidReference, 0, "native release can only be observed for a live resource generation");
        return BackendStatus::Success();
    }
    bool CommonBackend::IsNativeReleaseComplete(const ResourceRef resource) const noexcept
    {
        return resource.IsValid() && m_lifetime.IsNativeReleaseComplete(resource);
    }
    bool CommonBackend::IsResourceReferenceValid(const ResourceRef resource) const noexcept
    {
        return m_lifetime.IsValid(resource);
    }
    void* CommonBackend::GetResourcePayload(const ResourceRef resource) noexcept
    {
        return m_lifetime.GetPayload(resource);
    }
    const void* CommonBackend::GetResourcePayload(const ResourceRef resource) const noexcept
    {
        return m_lifetime.GetPayload(resource);
    }
    i32 CommonBackend::GetResourceReferenceCount(const ResourceRef resource) const noexcept
    {
        return m_lifetime.GetRefCount(resource);
    }
    void CommonBackend::AddRef(const ResourceRef resource) noexcept
    {
        static_cast<void>(m_lifetime.AddRef(resource));
    }
    bool CommonBackend::TryAddRef(const ResourceRef resource) noexcept
    {
        return m_lifetime.AddRef(resource);
    }
    i32 CommonBackend::Release(const ResourceRef resource) noexcept
    {
        return m_lifetime.Release(resource);
    }
    ResourceLifetimeStats CommonBackend::GetResourceLifetimeStats() const noexcept
    {
        return m_lifetime.GetStats();
    }
    void CommonBackend::DrainRetiredResourcesAfterGpuIdle() noexcept
    {
        RetireResources();
        m_lifetime.WaitForReclamation();
        RetireResources();
        m_lifetime.WaitForReclamation();
    }
    CommandListRef CommonBackend::CreateCommandList(const CommandListType type, const u64 role) noexcept
    {
        nvrhi::CommandListHandle native = AcquireNativeCommandList(type, role);
        if (!native)
            return {};
        native->open();
        native->setEnableAutomaticBarriers(false);
        CommandListPayload* const payload = AllocatePayload<CommandListPayload>(static_cast<nvrhi::CommandListHandle&&>(native), type, role);
        if (payload == nullptr)
            return {};
        const ResourceRef resource = m_lifetime.Create(
            ResourceKind::CommandList, payload,
            [](void* context, ResourceRef, void* address) noexcept
            {
                auto& common = *static_cast<CommonBackend*>(context);
                auto* const command = static_cast<CommandListPayload*>(address);
                nvrhi::CommandListHandle native = static_cast<nvrhi::CommandListHandle&&>(command->native);
                const bool recycle = command->recycleAfterCompletion;
                const CommandListType type = command->type;
                const u64 role = command->role;
                for (const CommandListPayload::SubmissionCallback& notification : command->submissionCallbacks)
                    notification.callback(notification.context, {}, notification.value);
                for (const ResourceRef used : command->resources)
                    static_cast<void>(common.Release(used));
                DestroyPayload<CommandListPayload>(nullptr, {}, command);
                if (recycle)
                    common.RecycleNativeCommandList(static_cast<nvrhi::CommandListHandle&&>(native), type, role);
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
        for (const CommandListPayload::SubmissionCallback& notification : payload->submissionCallbacks)
            notification.callback(notification.context, {}, notification.value);
        payload->submissionCallbacks.Clear();
        for (const ResourceRef used : payload->resources)
            static_cast<void>(m_lifetime.Release(used));
        payload->resources.Clear();
        static_cast<void>(m_lifetime.Release(resource));
    }
    BackendStatus CommonBackend::CloseCommandList(const CommandListRef commandList) noexcept
    {
        auto* const payload = static_cast<CommandListPayload*>(m_lifetime.GetPayload(ResourceRef(commandList)));
        if (payload == nullptr || !payload->open)
            return BackendStatus::Failure(FailureCode::InvalidCommandList, 0, "cannot close a stale or already closed command list");
        if (payload->markerDepth != 0)
            return BackendStatus::Failure(FailureCode::InvalidCommandList, 0, "cannot close a command list with unterminated GPU events");
        payload->native->commitBarriers();
        payload->native->close();
        payload->open = false;
        return BackendStatus::Success();
    }
    BackendStatus CommonBackend::SubmitCommandLists(const char*, const containers::ArraySpan<const CommandListRef> commandLists,
                                                     const CommandListSyncType sync, SubmissionReceipt& receipt) noexcept
    {
        concurrency::ScopedLock submissionGuard(m_submissionLock);
        receipt = {};
        nvrhi::ICommandList* nativeLists[3][MaximumCommandListsPerSubmission]{};
        CommandListPayload* payloads[MaximumCommandListsPerSubmission]{};
        ResourceRef references[MaximumCommandListsPerSubmission]{};
        u32 queueCounts[3]{};
        u64 incomingWaits[3][3]{};

        for (u32 index = 0; index < commandLists.Size(); ++index)
        {
            const ResourceRef resource(commandLists[index]);
            auto* const payload = static_cast<CommandListPayload*>(m_lifetime.GetPayload(resource));
            if (payload == nullptr || payload->open)
                return BackendStatus::Failure(FailureCode::InvalidCommandList, 0, "submission contains a stale or open command list");
            const u32 queue = QueueIndex(GetQueueType(payload->type));
            nativeLists[queue][queueCounts[queue]++] = payload->native;
            payloads[index] = payload;
            references[index] = resource;
            for (u32 producer = 0; producer < 3; ++producer)
            {
                const u64 value = payload->incomingWaits[producer];
                // Never wait for a future signal: this also prevents cycles
                // between the queues in this submission.
                if (value > m_signaledFences[producer])
                    return BackendStatus::Failure(FailureCode::InvalidArgument, 0, "incoming queue fence has not been submitted");
                if (value > incomingWaits[queue][producer])
                    incomingWaits[queue][producer] = value;
            }
        }
        if (sync == CommandListSyncType::None)
        {
            u32 activeQueues = 0;
            for (u32 queue = 0; queue < 3; ++queue)
                activeQueues += queueCounts[queue] != 0 ? 1u : 0u;
            if (activeQueues > 1)
                return BackendStatus::Failure(FailureCode::InvalidArgument, 0,
                                              "a submission spanning multiple queues requires an explicit fork or join synchronization mode");
        }

        for (u32 index = 0; index < commandLists.Size(); ++index)
        {
            CommandListPayload* const payload = payloads[index];
            if (payload == nullptr)
                return BackendStatus::Failure(FailureCode::InvalidCommandList, 0, "submission lost a validated command list");
            const containers::DynamicArray<ResourceRef>& workingSet = payload->residencyResources;
            for (u32 first = 0; first < workingSet.Size(); first += MaximumResidencyBatchSize)
            {
                const u32 count = workingSet.Size() - first < MaximumResidencyBatchSize ? workingSet.Size() - first : MaximumResidencyBatchSize;
                const BackendStatus status = m_prepareResidency(m_fenceContext, {workingSet.TypedData() + first, count});
                if (!status)
                    return status;
            }
        }

        // Use the existing native submission lock. Per-list recording has no
        // shared wait state; at most one wait per consumer/producer queue pair
        // is issued here. Same-queue dependencies follow submission order.
        for (u32 consumer = 0; consumer < 3; ++consumer)
        {
            for (u32 producer = 0; producer < 3; ++producer)
            {
                const u64 value = incomingWaits[consumer][producer];
                if (value == 0 || consumer == producer)
                    continue;
                const bool waitQueued = m_queueWait(m_fenceContext, static_cast<QueueType>(consumer), {static_cast<QueueType>(producer), value});
                if (!waitQueued)
                    return BackendStatus::Failure(FailureCode::DeviceLost, 0, "failed to enqueue an incoming GPU queue wait");
            }
        }

        const auto executeQueue = [&](const QueueType queue) noexcept
        {
            const u32 index = QueueIndex(queue);
            if (queueCounts[index] == 0)
                return;
            m_submittedInstances[index] = m_device->executeCommandLists(nativeLists[index], queueCounts[index], static_cast<nvrhi::CommandQueue>(index));
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
        receipt.workSubmitted = true;
        const auto finalizeSubmittedPayloads = [&]() noexcept
        {
            for (u32 index = 0; index < commandLists.Size(); ++index)
            {
                CommandListPayload* const submittedPayload = payloads[index];
                VG_ASSERT_MSG(submittedPayload != nullptr, "a submitted command list must retain its validated payload");
                if (submittedPayload == nullptr)
                    continue;
                CommandListPayload& payload = *submittedPayload;
                const QueueType queue = GetQueueType(payload.type);
                const GpuFence fence = queueFences[QueueIndex(queue)];
                if (fence.IsValid())
                    m_commitResidency(m_fenceContext, {payload.residencyResources.TypedData(), payload.residencyResources.Size()}, fence);
                for (const CommandListPayload::SubmissionCallback& notification : payload.submissionCallbacks)
                    notification.callback(notification.context, fence, notification.value);
                payload.submissionCallbacks.Clear();
                for (const ResourceRef resource : payload.resources)
                {
                    if (fence.IsValid())
                        static_cast<void>(m_lifetime.RecordUse(resource, queue, fence.value));
                    static_cast<void>(m_lifetime.Release(resource));
                }
                payload.resources.Clear();
                payload.residencyResources.Clear();
                payload.recycleAfterCompletion = fence.IsValid() && m_lifetime.RecordUse(references[index], queue, fence.value);
                static_cast<void>(m_lifetime.Release(references[index]));
            }
        };
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
            const bool fenceSignaled = m_signalFence(m_fenceContext, queue, value);
            if (!fenceSignaled)
            {
                // Native execution already happened. Release the closed list
                // payloads and publish truthful submitted-without-completion
                // evidence; device-loss recovery owns the fence-less work.
                finalizeSubmittedPayloads();
                return BackendStatus::Failure(FailureCode::DeviceLost, 0, "failed to signal a submission fence");
            }
            queueFences[queueIndex] = {queue, value};
            m_signaledFences[queueIndex] = value;
            receipt.residency.Include(queueFences[queueIndex]);
        }

        finalizeSubmittedPayloads();

        if (sync == CommandListSyncType::ForkAsyncCompute)
            receipt.completion = queueFences[QueueIndex(QueueType::Compute)];
        else if (sync == CommandListSyncType::JoinAsyncCompute)
            receipt.completion = queueFences[QueueIndex(QueueType::Graphics)];
        else
        {
            const u32 completionQueue = queueCounts[0] != 0 ? 0u : queueCounts[1] != 0 ? 1u : 2u;
            receipt.completion = queueFences[completionQueue];
        }
        return BackendStatus::Success();
    }

    BackendStatus CommonBackend::ExecuteSerializedQueueOperation(const SerializedQueueOperation operation, void* const context) noexcept
    {
        if (operation == nullptr)
            return BackendStatus::Failure(FailureCode::InvalidArgument, 0, "serialized queue operation is null");
#if VG_BUILD_DEBUG
        const u64 start = system::GetMonotonicTicks();
#endif
        concurrency::ScopedLock submissionGuard(m_submissionLock);
#if VG_BUILD_DEBUG
        const double lockMs = 1000.0 * double(system::GetMonotonicTicks() - start) / double(system::GetMonotonicFrequency());
        if (lockMs >= 100.0)
            VG_LOG_WARNING(diagnostics::Category::Rendering, "Queue operation lock stall: %.2f ms mainThread=%u", lockMs, u32(concurrency::IsMainThread()));
#endif
        return operation(context);
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
            if (texture == nullptr || !HasFlag(texture->desc.usage, TextureUsage::RenderTarget) || attachment.mipLevel >= texture->desc.mipCount ||
                attachment.arraySlice >= texture->desc.arraySize)
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
            if (texture == nullptr || !HasFlag(texture->desc.usage, TextureUsage::DepthStencil) || attachment.mipLevel >= texture->desc.mipCount ||
                attachment.arraySlice >= texture->desc.arraySize)
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
            !HasFlag(buffer->desc.usage, BufferUsage::Index) || binding.offset >= buffer->desc.size || (binding.offset & (alignment - 1u)) != 0 ||
            binding.offset > 0xffffffffu)
            return BackendStatus::Failure(FailureCode::IncompatibleBinding, 0, "invalid index-buffer binding");
        if (!TrackCommandResource(*this, *command, ResourceRef(binding.buffer)))
            return BackendStatus::Failure(FailureCode::OutOfMemory, 0, "failed to retain an index buffer");
        command->indexBuffer = binding;
        command->indexBufferSet = true;
        return BackendStatus::Success();
    }

    BackendStatus CommonBackend::BindIndirectArguments(const CommandListRef commandList, const BufferRef arguments, const BufferRef count) noexcept
    {
        auto* const command = static_cast<CommandListPayload*>(m_lifetime.GetPayload(ResourceRef(commandList)));
        const auto* const argumentBuffer = static_cast<const BufferPayload*>(m_lifetime.GetPayload(ResourceRef(arguments)));
        const auto* const countBuffer = count ? static_cast<const BufferPayload*>(m_lifetime.GetPayload(ResourceRef(count))) : nullptr;
        if (command == nullptr || !command->open || argumentBuffer == nullptr || !HasFlag(argumentBuffer->desc.usage, BufferUsage::IndirectArguments) ||
            (count && (countBuffer == nullptr || !HasFlag(countBuffer->desc.usage, BufferUsage::IndirectArguments))))
            return BackendStatus::Failure(FailureCode::IncompatibleBinding, 0, "invalid indirect-argument buffers");
        if (!TrackCommandResource(*this, *command, ResourceRef(arguments)) || (count && !TrackCommandResource(*this, *command, ResourceRef(count))))
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

    BackendStatus CommonBackend::ClearColorTarget(const CommandListRef commandList, const TextureRef target, const ColorValue& value,
                                                  const SubresourceRange& requested, const Rect* const rectangle) noexcept
    {
        auto* const command = static_cast<CommandListPayload*>(m_lifetime.GetPayload(ResourceRef(commandList)));
        const auto* const texture = static_cast<const TexturePayload*>(m_lifetime.GetPayload(ResourceRef(target)));
        if (command == nullptr || !command->open || command->type != CommandListType::Default)
            return BackendStatus::Failure(FailureCode::InvalidCommandList, 0, "color clears require an open graphics command list");
        if (texture == nullptr)
            return BackendStatus::Failure(FailureCode::InvalidReference, 0, "color clear references a stale texture");
        if (!HasFlag(texture->desc.usage, TextureUsage::RenderTarget) || IsDepthFormat(texture->desc.format))
            return BackendStatus::Failure(FailureCode::IncompatibleBinding, 0, "texture is not a color render target");
        SubresourceRange range;
        if (!ResolveSubresources(texture->desc, requested, range) || (rectangle != nullptr && !ValidateClearRectangle(texture->desc, range, *rectangle)))
            return BackendStatus::Failure(FailureCode::InvalidArgument, 0, "invalid color-clear subresource range or rectangle");
        if (!TrackCommandResource(*this, *command, ResourceRef(target)))
            return BackendStatus::Failure(FailureCode::OutOfMemory, 0, "failed to retain a color-clear target");
        const nvrhi::TextureSubresourceSet nativeRange(range.firstMip, range.mipCount, range.firstSlice, range.sliceCount);
        if (rectangle == nullptr)
        {
            for (u32 mip = range.firstMip; mip < static_cast<u32>(range.firstMip) + range.mipCount; ++mip)
                command->native->clearTextureFloat(texture->native, nvrhi::TextureSubresourceSet(mip, 1, range.firstSlice, range.sliceCount),
                                                   nvrhi::Color(value.red, value.green, value.blue, value.alpha));
        }
        else
        {
            command->native->setTextureState(texture->native, nativeRange, nvrhi::ResourceStates::RenderTarget);
            command->native->commitBarriers();
            if (!m_rectColorClear(m_fenceContext, command->native, texture->native, value, range, *rectangle))
                return BackendStatus::Failure(FailureCode::BackendFailure, 0, "native rectangular color clear failed");
        }
        return BackendStatus::Success();
    }

    BackendStatus CommonBackend::ClearDepthStencilTarget(const CommandListRef commandList, const TextureRef target, const bool clearDepth, const f32 depth,
                                                         const bool clearStencil, const u8 stencil, const SubresourceRange& requested,
                                                         const Rect* const rectangle) noexcept
    {
        auto* const command = static_cast<CommandListPayload*>(m_lifetime.GetPayload(ResourceRef(commandList)));
        const auto* const texture = static_cast<const TexturePayload*>(m_lifetime.GetPayload(ResourceRef(target)));
        if (command == nullptr || !command->open || command->type != CommandListType::Default)
            return BackendStatus::Failure(FailureCode::InvalidCommandList, 0, "depth-stencil clears require an open graphics command list");
        if (texture == nullptr)
            return BackendStatus::Failure(FailureCode::InvalidReference, 0, "depth-stencil clear references a stale texture");
        if (!clearDepth && !clearStencil)
            return BackendStatus::Failure(FailureCode::InvalidArgument, 0, "depth-stencil clear has no selected plane");
        if (!HasFlag(texture->desc.usage, TextureUsage::DepthStencil) || !IsDepthFormat(texture->desc.format) ||
            (clearStencil && !HasStencilPlane(texture->desc.format)))
            return BackendStatus::Failure(FailureCode::IncompatibleBinding, 0, "texture does not provide the requested depth-stencil planes");
        SubresourceRange range;
        if (!ResolveSubresources(texture->desc, requested, range) || (rectangle != nullptr && !ValidateClearRectangle(texture->desc, range, *rectangle)))
            return BackendStatus::Failure(FailureCode::InvalidArgument, 0, "invalid depth-stencil-clear subresource range or rectangle");
        if (!TrackCommandResource(*this, *command, ResourceRef(target)))
            return BackendStatus::Failure(FailureCode::OutOfMemory, 0, "failed to retain a depth-stencil-clear target");
        const nvrhi::TextureSubresourceSet nativeRange(range.firstMip, range.mipCount, range.firstSlice, range.sliceCount);
        if (rectangle == nullptr)
        {
            for (u32 mip = range.firstMip; mip < static_cast<u32>(range.firstMip) + range.mipCount; ++mip)
                command->native->clearDepthStencilTexture(texture->native, nvrhi::TextureSubresourceSet(mip, 1, range.firstSlice, range.sliceCount), clearDepth,
                                                          depth, clearStencil, stencil);
        }
        else
        {
            command->native->setTextureState(texture->native, nativeRange, nvrhi::ResourceStates::DepthWrite);
            command->native->commitBarriers();
            if (!m_rectDepthStencilClear(m_fenceContext, command->native, texture->native, clearDepth, depth, clearStencil, stencil, range, *rectangle))
                return BackendStatus::Failure(FailureCode::BackendFailure, 0, "native rectangular depth-stencil clear failed");
        }
        return BackendStatus::Success();
    }

    BackendStatus CommonBackend::ClearTextureUav(const CommandListRef commandList, const TextureRef textureRef, const ColorValue& value,
                                                 const SubresourceRange& requested) noexcept
    {
        auto* const command = static_cast<CommandListPayload*>(m_lifetime.GetPayload(ResourceRef(commandList)));
        const auto* const texture = static_cast<const TexturePayload*>(m_lifetime.GetPayload(ResourceRef(textureRef)));
        if (command == nullptr || !command->open || (command->type != CommandListType::Default && command->type != CommandListType::Compute))
            return BackendStatus::Failure(FailureCode::InvalidCommandList, 0, "texture UAV clears require an open graphics or compute command list");
        if (texture == nullptr)
            return BackendStatus::Failure(FailureCode::InvalidReference, 0, "texture UAV clear references a stale texture");
        if (!HasFlag(texture->desc.usage, TextureUsage::UnorderedAccess) || IsDepthFormat(texture->desc.format) ||
            IsUnsignedIntegerFormat(texture->desc.format) || HasFlag(texture->desc.usage, TextureUsage::RenderTarget))
            return BackendStatus::Failure(FailureCode::IncompatibleBinding, 0, "texture does not provide an unambiguous floating-point UAV clear view");
        SubresourceRange range;
        if (!ResolveSubresources(texture->desc, requested, range) || range.firstSlice != 0 || range.sliceCount != texture->desc.arraySize)
            return BackendStatus::Failure(FailureCode::InvalidArgument, 0, "invalid texture UAV clear range");
        if (!TrackCommandResource(*this, *command, ResourceRef(textureRef)))
            return BackendStatus::Failure(FailureCode::OutOfMemory, 0, "failed to retain a texture UAV clear target");
        for (u32 mip = range.firstMip; mip < static_cast<u32>(range.firstMip) + range.mipCount; ++mip)
            command->native->clearTextureFloat(texture->native, nvrhi::TextureSubresourceSet(mip, 1, 0, texture->desc.arraySize),
                                               nvrhi::Color(value.red, value.green, value.blue, value.alpha));
        return BackendStatus::Success();
    }

    BackendStatus CommonBackend::ClearTextureUav(const CommandListRef commandList, const TextureRef textureRef, const u32 value,
                                                 const SubresourceRange& requested) noexcept
    {
        auto* const command = static_cast<CommandListPayload*>(m_lifetime.GetPayload(ResourceRef(commandList)));
        const auto* const texture = static_cast<const TexturePayload*>(m_lifetime.GetPayload(ResourceRef(textureRef)));
        if (command == nullptr || !command->open || (command->type != CommandListType::Default && command->type != CommandListType::Compute))
            return BackendStatus::Failure(FailureCode::InvalidCommandList, 0, "texture UAV clears require an open graphics or compute command list");
        if (texture == nullptr)
            return BackendStatus::Failure(FailureCode::InvalidReference, 0, "texture UAV clear references a stale texture");
        if (!HasFlag(texture->desc.usage, TextureUsage::UnorderedAccess) || !IsUnsignedIntegerFormat(texture->desc.format))
            return BackendStatus::Failure(FailureCode::IncompatibleBinding, 0, "texture does not provide an unsigned-integer UAV clear view");
        SubresourceRange range;
        if (!ResolveSubresources(texture->desc, requested, range) || range.firstSlice != 0 || range.sliceCount != texture->desc.arraySize)
            return BackendStatus::Failure(FailureCode::InvalidArgument, 0, "invalid texture UAV clear range");
        if (!TrackCommandResource(*this, *command, ResourceRef(textureRef)))
            return BackendStatus::Failure(FailureCode::OutOfMemory, 0, "failed to retain a texture UAV clear target");
        for (u32 mip = range.firstMip; mip < static_cast<u32>(range.firstMip) + range.mipCount; ++mip)
            command->native->clearTextureUInt(texture->native, nvrhi::TextureSubresourceSet(mip, 1, 0, texture->desc.arraySize), value);
        return BackendStatus::Success();
    }

    BackendStatus CommonBackend::ClearBufferUav(const CommandListRef commandList, const BufferRef bufferRef, const u32 value) noexcept
    {
        auto* const command = static_cast<CommandListPayload*>(m_lifetime.GetPayload(ResourceRef(commandList)));
        const auto* const buffer = static_cast<const BufferPayload*>(m_lifetime.GetPayload(ResourceRef(bufferRef)));
        if (command == nullptr || !command->open || (command->type != CommandListType::Default && command->type != CommandListType::Compute))
            return BackendStatus::Failure(FailureCode::InvalidCommandList, 0, "buffer UAV clears require an open graphics or compute command list");
        if (buffer == nullptr || !HasFlag(buffer->desc.usage, BufferUsage::UnorderedAccess))
            return BackendStatus::Failure(FailureCode::IncompatibleBinding, 0, "buffer does not support unordered access");
        if (!TrackCommandResource(*this, *command, ResourceRef(bufferRef)))
            return BackendStatus::Failure(FailureCode::OutOfMemory, 0, "failed to retain a buffer UAV clear target");
        command->native->clearBufferUInt(buffer->native, value);
        return BackendStatus::Success();
    }

    BackendStatus CommonBackend::DiscardTexture(const CommandListRef commandList, const TextureRef textureRef, const SubresourceRange& requested) noexcept
    {
        auto* const command = static_cast<CommandListPayload*>(m_lifetime.GetPayload(ResourceRef(commandList)));
        const auto* const texture = static_cast<const TexturePayload*>(m_lifetime.GetPayload(ResourceRef(textureRef)));
        if (command == nullptr || !command->open || (command->type != CommandListType::Default && command->type != CommandListType::Compute))
            return BackendStatus::Failure(FailureCode::InvalidCommandList, 0, "texture discard requires an open graphics or compute command list");
        if (texture == nullptr)
            return BackendStatus::Failure(FailureCode::InvalidReference, 0, "texture discard references a stale texture");
        SubresourceRange range;
        if (!ResolveSubresources(texture->desc, requested, range))
            return BackendStatus::Failure(FailureCode::InvalidArgument, 0, "invalid texture discard range");
        nvrhi::ResourceStates discardState = nvrhi::ResourceStates::Unknown;
        if (command->type == CommandListType::Default && HasFlag(texture->desc.usage, TextureUsage::RenderTarget))
            discardState = nvrhi::ResourceStates::RenderTarget;
        else if (command->type == CommandListType::Default && HasFlag(texture->desc.usage, TextureUsage::DepthStencil))
            discardState = nvrhi::ResourceStates::DepthWrite;
        else if (HasFlag(texture->desc.usage, TextureUsage::UnorderedAccess))
            discardState = nvrhi::ResourceStates::UnorderedAccess;
        if (discardState == nvrhi::ResourceStates::Unknown)
            return BackendStatus::Failure(FailureCode::IncompatibleBinding, 0, "texture has no discard-compatible usage on this queue");
        if (!TrackCommandResource(*this, *command, ResourceRef(textureRef)))
            return BackendStatus::Failure(FailureCode::OutOfMemory, 0, "failed to retain a discarded texture");
        command->native->setTextureState(texture->native, nvrhi::TextureSubresourceSet(range.firstMip, range.mipCount, range.firstSlice, range.sliceCount),
                                         discardState);
        command->native->commitBarriers();
        return m_discardTexture(m_fenceContext, command->native, texture->native, range)
                   ? BackendStatus::Success()
                   : BackendStatus::Failure(FailureCode::BackendFailure, 0, "native texture discard failed");
    }

    BackendStatus CommonBackend::SetStencilRefValue(const CommandListRef commandList, const u8 value) noexcept
    {
        auto* const command = static_cast<CommandListPayload*>(m_lifetime.GetPayload(ResourceRef(commandList)));
        if (command == nullptr || !command->open || command->type != CommandListType::Default)
            return BackendStatus::Failure(FailureCode::InvalidCommandList, 0, "stencil reference requires an open graphics command list");
        command->stencilReference = value;
        return BackendStatus::Success();
    }

    BackendStatus CommonBackend::SetBlendFactor(const CommandListRef commandList, const ColorValue& value) noexcept
    {
        auto* const command = static_cast<CommandListPayload*>(m_lifetime.GetPayload(ResourceRef(commandList)));
        if (command == nullptr || !command->open || command->type != CommandListType::Default)
            return BackendStatus::Failure(FailureCode::InvalidCommandList, 0, "blend factor requires an open graphics command list");
        command->blendFactor = value;
        return BackendStatus::Success();
    }

    BackendStatus CommonBackend::BeginGpuEvent(const CommandListRef commandList, const char* const name) noexcept
    {
        auto* const command = static_cast<CommandListPayload*>(m_lifetime.GetPayload(ResourceRef(commandList)));
        if (command == nullptr || !command->open)
            return BackendStatus::Failure(FailureCode::InvalidCommandList, 0, "GPU event requires an open command list");
        if (name == nullptr || name[0] == '\0')
            return BackendStatus::Failure(FailureCode::InvalidArgument, 0, "GPU event name is empty");
        command->native->beginMarker(name);
        ++command->markerDepth;
        return BackendStatus::Success();
    }

    BackendStatus CommonBackend::EndGpuEvent(const CommandListRef commandList) noexcept
    {
        auto* const command = static_cast<CommandListPayload*>(m_lifetime.GetPayload(ResourceRef(commandList)));
        if (command == nullptr || !command->open)
            return BackendStatus::Failure(FailureCode::InvalidCommandList, 0, "GPU event requires an open command list");
        if (command->markerDepth == 0)
            return BackendStatus::Failure(FailureCode::InvalidArgument, 0, "GPU event stack is empty");
        command->native->endMarker();
        --command->markerDepth;
        return BackendStatus::Success();
    }

    BackendStatus CommonBackend::SetGpuMarker(const CommandListRef commandList, const char* const name) noexcept
    {
        const BackendStatus begin = BeginGpuEvent(commandList, name);
        return begin ? EndGpuEvent(commandList) : begin;
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

    BackendStatus CommonBackend::DrawPrimitiveIndirect(const CommandListRef commandList, const u64 argumentsOffset, const u32 commandCount) noexcept
    {
        if (argumentsOffset > 0xffffffffu || (argumentsOffset & 3u) != 0 || commandCount == 0)
            return BackendStatus::Failure(FailureCode::InvalidArgument, 0, "invalid native indirect draw arguments");
        auto* const command = static_cast<CommandListPayload*>(m_lifetime.GetPayload(ResourceRef(commandList)));
        const auto* const arguments =
            command != nullptr ? static_cast<const BufferPayload*>(m_lifetime.GetPayload(ResourceRef(command->indirectArguments))) : nullptr;
        const u64 bytes = static_cast<u64>(sizeof(IndirectDrawArguments)) * commandCount;
        if (command == nullptr || arguments == nullptr || argumentsOffset + bytes < argumentsOffset || argumentsOffset + bytes > arguments->desc.size)
            return BackendStatus::Failure(FailureCode::MissingBinding, 0, "indirect draw arguments are not fully bound");
        const BackendStatus status = SetupForGraphics(*this, *command);
        if (!status)
            return status;
        command->native->drawIndirect(static_cast<u32>(argumentsOffset), commandCount);
        return BackendStatus::Success();
    }

    BackendStatus CommonBackend::DrawIndexedPrimitiveIndirect(const CommandListRef commandList, const u64 argumentsOffset, const u32 commandCount) noexcept
    {
        if (argumentsOffset > 0xffffffffu || (argumentsOffset & 3u) != 0 || commandCount == 0)
            return BackendStatus::Failure(FailureCode::InvalidArgument, 0, "invalid native indexed indirect draw arguments");
        auto* const command = static_cast<CommandListPayload*>(m_lifetime.GetPayload(ResourceRef(commandList)));
        const auto* const arguments =
            command != nullptr ? static_cast<const BufferPayload*>(m_lifetime.GetPayload(ResourceRef(command->indirectArguments))) : nullptr;
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

    BackendStatus CommonBackend::DrawIndexedPrimitiveIndirectCount(const CommandListRef commandList, const u64 argumentsOffset, const u64 countOffset,
                                                                   const u32 maximumCommandCount) noexcept
    {
        if (argumentsOffset > 0xffffffffu || countOffset > 0xffffffffu || (argumentsOffset & 3u) != 0 || (countOffset & 3u) != 0 ||
            maximumCommandCount == 0)
            return BackendStatus::Failure(FailureCode::InvalidArgument, 0, "invalid native counted indirect draw arguments");
        auto* const command = static_cast<CommandListPayload*>(m_lifetime.GetPayload(ResourceRef(commandList)));
        const auto* const arguments =
            command != nullptr ? static_cast<const BufferPayload*>(m_lifetime.GetPayload(ResourceRef(command->indirectArguments))) : nullptr;
        const auto* const count = command != nullptr ? static_cast<const BufferPayload*>(m_lifetime.GetPayload(ResourceRef(command->indirectCount))) : nullptr;
        const u64 bytes = static_cast<u64>(sizeof(IndirectDrawIndexedArguments)) * maximumCommandCount;
        if (command == nullptr || !command->indexBufferSet || arguments == nullptr || count == nullptr || argumentsOffset + bytes < argumentsOffset ||
            argumentsOffset + bytes > arguments->desc.size || countOffset + sizeof(u32) < countOffset || countOffset + sizeof(u32) > count->desc.size)
            return BackendStatus::Failure(FailureCode::MissingBinding, 0, "counted indirect draw state is incomplete");
        const BackendStatus status = SetupForGraphics(*this, *command);
        if (!status)
            return status;
        command->native->drawIndexedIndirectCount(static_cast<u32>(argumentsOffset), static_cast<u32>(countOffset), maximumCommandCount);
        return BackendStatus::Success();
    }

    BackendStatus CommonBackend::DispatchCompute(const CommandListRef commandList, const u32 groupCountX, const u32 groupCountY, const u32 groupCountZ) noexcept
    {
        auto* const command = static_cast<CommandListPayload*>(m_lifetime.GetPayload(ResourceRef(commandList)));
        if (command == nullptr || !command->open || (command->type != CommandListType::Default && command->type != CommandListType::Compute))
            return BackendStatus::Failure(FailureCode::InvalidCommandList, 0, "dispatch requires an open graphics or compute command list");
        const BackendStatus status = SetupForCompute(*this, *command);
        if (!status)
            return status;
        command->native->dispatch(groupCountX, groupCountY, groupCountZ);
        return BackendStatus::Success();
    }

    BackendStatus CommonBackend::DispatchIndirectCompute(const CommandListRef commandList, const u64 argumentsOffset) noexcept
    {
        if (argumentsOffset > 0xffffffffu || (argumentsOffset & 3u) != 0)
            return BackendStatus::Failure(FailureCode::InvalidArgument, 0, "invalid native indirect dispatch arguments");
        auto* const command = static_cast<CommandListPayload*>(m_lifetime.GetPayload(ResourceRef(commandList)));
        const auto* const arguments =
            command != nullptr ? static_cast<const BufferPayload*>(m_lifetime.GetPayload(ResourceRef(command->indirectArguments))) : nullptr;
        if (command == nullptr || arguments == nullptr || argumentsOffset + sizeof(IndirectDispatchArguments) < argumentsOffset ||
            argumentsOffset + sizeof(IndirectDispatchArguments) > arguments->desc.size)
            return BackendStatus::Failure(FailureCode::MissingBinding, 0, "indirect dispatch arguments are not fully bound");
        const BackendStatus status = SetupForCompute(*this, *command);
        if (!status)
            return status;
        command->native->dispatchIndirect(static_cast<u32>(argumentsOffset));
        return BackendStatus::Success();
    }

    BackendStatus CommonBackend::WriteBuffer(const CommandListRef commandList, const BufferRef buffer, const void* const data, const u64 size,
                                             const u64 destinationOffset) noexcept
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

    BackendStatus CommonBackend::WriteTexture(const CommandListRef commandList, const TextureRef texture, const TextureSubresourceData& data) noexcept
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

    BackendStatus CommonBackend::CopyBuffer(const CommandListRef commandList, const BufferRef destination, const u64 destinationOffset, const BufferRef source,
                                            const u64 sourceOffset, const u64 size) noexcept
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

    BackendStatus CommonBackend::CopyTexture(const CommandListRef commandList, const TextureRef destination, const TextureRef source,
                                             const TextureCopyRegion& requested) noexcept
    {
        auto* const command = static_cast<CommandListPayload*>(m_lifetime.GetPayload(ResourceRef(commandList)));
        auto* const destinationPayload = static_cast<TexturePayload*>(m_lifetime.GetPayload(ResourceRef(destination)));
        auto* const sourcePayload = static_cast<TexturePayload*>(m_lifetime.GetPayload(ResourceRef(source)));
        if (command == nullptr || !command->open)
            return BackendStatus::Failure(FailureCode::InvalidCommandList, 0, "texture copy requires an open command list");
        if (destinationPayload == nullptr || sourcePayload == nullptr)
            return BackendStatus::Failure(FailureCode::InvalidReference, 0, "texture copy references a stale texture");
        const TextureDesc& destinationDesc = destinationPayload->desc;
        const TextureDesc& sourceDesc = sourcePayload->desc;
        if (!HasFlag(destinationDesc.usage, TextureUsage::CopyDestination) || !HasFlag(sourceDesc.usage, TextureUsage::CopySource))
            return BackendStatus::Failure(FailureCode::IncompatibleBinding, 0, "texture copy requires copy-source and copy-destination usage");
        if (destinationDesc.format != sourceDesc.format || destinationDesc.dimension != sourceDesc.dimension ||
            destinationDesc.sampleCount != sourceDesc.sampleCount || IsDepthFormat(sourceDesc.format))
            return BackendStatus::Failure(FailureCode::IncompatibleBinding, 0, "texture copy resources have incompatible formats, dimensions or sample counts");
        if (requested.source.mipLevel >= sourceDesc.mipCount || requested.source.arraySlice >= sourceDesc.arraySize ||
            requested.destination.mipLevel >= destinationDesc.mipCount || requested.destination.arraySlice >= destinationDesc.arraySize)
            return BackendStatus::Failure(FailureCode::InvalidArgument, 0, "texture copy selects an invalid subresource");

        const Extent3D sourceExtent = GetMipExtent(sourceDesc, requested.source.mipLevel);
        const Extent3D destinationExtent = GetMipExtent(destinationDesc, requested.destination.mipLevel);
        TextureCopyRegion region = requested;
        const bool implicitExtent = region.extent.width == 0 && region.extent.height == 0 && region.extent.depth == 0;
        if (implicitExtent)
        {
            if (region.sourceX != 0 || region.sourceY != 0 || region.sourceZ != 0)
                return BackendStatus::Failure(FailureCode::InvalidArgument, 0, "an implicit texture-copy extent requires a zero source origin");
            region.extent = sourceExtent;
        }
        else if (region.extent.width == 0 || region.extent.height == 0 || region.extent.depth == 0)
            return BackendStatus::Failure(FailureCode::InvalidArgument, 0, "texture copy contains a partially zero extent");

        if (region.sourceX > sourceExtent.width || region.extent.width > sourceExtent.width - region.sourceX || region.sourceY > sourceExtent.height ||
            region.extent.height > sourceExtent.height - region.sourceY || region.sourceZ > sourceExtent.depth ||
            region.extent.depth > sourceExtent.depth - region.sourceZ || region.destinationX > destinationExtent.width ||
            region.extent.width > destinationExtent.width - region.destinationX || region.destinationY > destinationExtent.height ||
            region.extent.height > destinationExtent.height - region.destinationY || region.destinationZ > destinationExtent.depth ||
            region.extent.depth > destinationExtent.depth - region.destinationZ)
            return BackendStatus::Failure(FailureCode::InvalidArgument, 0, "texture copy region exceeds a selected subresource");
        if (IsBlockCompressed(sourceDesc.format) && (!IsBlockRegionValid(region.sourceX, region.extent.width, sourceExtent.width) ||
                                                     !IsBlockRegionValid(region.sourceY, region.extent.height, sourceExtent.height) ||
                                                     !IsBlockRegionValid(region.destinationX, region.extent.width, destinationExtent.width) ||
                                                     !IsBlockRegionValid(region.destinationY, region.extent.height, destinationExtent.height)))
            return BackendStatus::Failure(FailureCode::InvalidArgument, 0, "compressed texture-copy regions must follow 4x4 block boundaries");
        if (sourceDesc.sampleCount > 1 &&
            (region.sourceX != 0 || region.sourceY != 0 || region.sourceZ != 0 || region.destinationX != 0 || region.destinationY != 0 ||
             region.destinationZ != 0 || region.extent.width != sourceExtent.width || region.extent.height != sourceExtent.height ||
             region.extent.depth != sourceExtent.depth || region.extent.width != destinationExtent.width || region.extent.height != destinationExtent.height ||
             region.extent.depth != destinationExtent.depth))
            return BackendStatus::Failure(FailureCode::InvalidArgument, 0, "multisampled texture copies must cover complete matching subresources");
        if (destination == source && requested.destination.mipLevel == requested.source.mipLevel &&
            requested.destination.arraySlice == requested.source.arraySlice &&
            RegionsOverlap(region.sourceX, region.extent.width, region.destinationX, region.extent.width) &&
            RegionsOverlap(region.sourceY, region.extent.height, region.destinationY, region.extent.height) &&
            RegionsOverlap(region.sourceZ, region.extent.depth, region.destinationZ, region.extent.depth))
            return BackendStatus::Failure(FailureCode::InvalidArgument, 0, "texture copy source and destination regions overlap");

        if (command->native->getTextureSubresourceState(sourcePayload->native, requested.source.arraySlice, requested.source.mipLevel) !=
                nvrhi::ResourceStates::CopySource ||
            command->native->getTextureSubresourceState(destinationPayload->native, requested.destination.arraySlice, requested.destination.mipLevel) !=
                nvrhi::ResourceStates::CopyDest)
            return BackendStatus::Failure(FailureCode::ResourceStateMismatch, 0, "texture copy resources are not in their declared copy states");
        if (!TrackCommandResource(*this, *command, ResourceRef(source)) || !TrackCommandResource(*this, *command, ResourceRef(destination)))
            return BackendStatus::Failure(FailureCode::OutOfMemory, 0, "failed to retain a texture copy resource");

        nvrhi::TextureSlice sourceSlice;
        sourceSlice.setOrigin(region.sourceX, region.sourceY, region.sourceZ)
            .setSize(region.extent.width, region.extent.height, region.extent.depth)
            .setMipLevel(region.source.mipLevel)
            .setArraySlice(region.source.arraySlice);
        nvrhi::TextureSlice destinationSlice;
        destinationSlice.setOrigin(region.destinationX, region.destinationY, region.destinationZ)
            .setSize(region.extent.width, region.extent.height, region.extent.depth)
            .setMipLevel(region.destination.mipLevel)
            .setArraySlice(region.destination.arraySlice);
        command->native->commitBarriers();
        command->native->copyTexture(destinationPayload->native, destinationSlice, sourcePayload->native, sourceSlice);
        return BackendStatus::Success();
    }

    BackendStatus CommonBackend::ResolveTexture(const CommandListRef commandList, const TextureRef destination, const TextureRef source,
                                                const TextureResolveRegion& region) noexcept
    {
        auto* const command = static_cast<CommandListPayload*>(m_lifetime.GetPayload(ResourceRef(commandList)));
        auto* const destinationPayload = static_cast<TexturePayload*>(m_lifetime.GetPayload(ResourceRef(destination)));
        auto* const sourcePayload = static_cast<TexturePayload*>(m_lifetime.GetPayload(ResourceRef(source)));
        if (command == nullptr || !command->open || command->type != CommandListType::Default)
            return BackendStatus::Failure(FailureCode::InvalidCommandList, 0, "texture resolve requires an open graphics command list");
        if (destinationPayload == nullptr || sourcePayload == nullptr)
            return BackendStatus::Failure(FailureCode::InvalidReference, 0, "texture resolve references a stale texture");
        const TextureDesc& destinationDesc = destinationPayload->desc;
        const TextureDesc& sourceDesc = sourcePayload->desc;
        if (!HasFlag(destinationDesc.usage, TextureUsage::ResolveDestination) || !HasFlag(sourceDesc.usage, TextureUsage::ResolveSource))
            return BackendStatus::Failure(FailureCode::IncompatibleBinding, 0, "texture resolve requires resolve-source and resolve-destination usage");
        if (destinationDesc.format != sourceDesc.format || destinationDesc.dimension != sourceDesc.dimension || sourceDesc.sampleCount <= 1 ||
            destinationDesc.sampleCount != 1 || IsDepthFormat(sourceDesc.format))
            return BackendStatus::Failure(FailureCode::IncompatibleBinding, 0,
                                          "texture resolve resources have incompatible formats, dimensions or sample counts");
        const Extent3D sourceExtent = region.source.mipLevel < sourceDesc.mipCount ? GetMipExtent(sourceDesc, region.source.mipLevel) : Extent3D{};
        const Extent3D destinationExtent =
            region.destination.mipLevel < destinationDesc.mipCount ? GetMipExtent(destinationDesc, region.destination.mipLevel) : Extent3D{};
        if (region.source.mipLevel >= sourceDesc.mipCount || region.source.arraySlice >= sourceDesc.arraySize ||
            region.destination.mipLevel >= destinationDesc.mipCount || region.destination.arraySlice >= destinationDesc.arraySize ||
            sourceExtent.width != destinationExtent.width || sourceExtent.height != destinationExtent.height || sourceExtent.depth != destinationExtent.depth)
            return BackendStatus::Failure(FailureCode::InvalidArgument, 0, "texture resolve selects invalid or differently sized subresources");
        if (command->native->getTextureSubresourceState(sourcePayload->native, region.source.arraySlice, region.source.mipLevel) !=
                nvrhi::ResourceStates::ResolveSource ||
            command->native->getTextureSubresourceState(destinationPayload->native, region.destination.arraySlice, region.destination.mipLevel) !=
                nvrhi::ResourceStates::ResolveDest)
            return BackendStatus::Failure(FailureCode::ResourceStateMismatch, 0, "texture resolve resources are not in their declared resolve states");
        if (!TrackCommandResource(*this, *command, ResourceRef(source)) || !TrackCommandResource(*this, *command, ResourceRef(destination)))
            return BackendStatus::Failure(FailureCode::OutOfMemory, 0, "failed to retain a texture resolve resource");

        const nvrhi::TextureSubresourceSet sourceSubresource(region.source.mipLevel, 1, region.source.arraySlice, 1);
        const nvrhi::TextureSubresourceSet destinationSubresource(region.destination.mipLevel, 1, region.destination.arraySlice, 1);
        command->native->commitBarriers();
        command->native->resolveTexture(destinationPayload->native, destinationSubresource, sourcePayload->native, sourceSubresource);
        return BackendStatus::Success();
    }

    BackendStatus CommonBackend::RequestTextureReadback(const CommandListRef commandList, const TextureRef source, const TextureReadbackRegion& requested,
                                                        TextureReadbackRef& readback) noexcept
    {
        readback = {};
        auto* const command = static_cast<CommandListPayload*>(m_lifetime.GetPayload(ResourceRef(commandList)));
        auto* const sourcePayload = static_cast<TexturePayload*>(m_lifetime.GetPayload(ResourceRef(source)));
        if (command == nullptr || !command->open)
            return BackendStatus::Failure(FailureCode::InvalidCommandList, 0, "texture readback requires an open command list");
        if (sourcePayload == nullptr)
            return BackendStatus::Failure(FailureCode::InvalidReference, 0, "texture readback references a stale texture");
        const TextureDesc& desc = sourcePayload->desc;
        if (!HasFlag(desc.usage, TextureUsage::CopySource))
            return BackendStatus::Failure(FailureCode::IncompatibleBinding, 0, "texture readback requires copy-source usage");
        if (desc.sampleCount != 1)
            return BackendStatus::Failure(FailureCode::Unsupported, 0, "multisampled textures must be resolved before readback");
        if (IsDepthFormat(desc.format))
            return BackendStatus::Failure(FailureCode::Unsupported, 0, "depth-stencil readback requires an explicit plane contract");
        if (desc.dimension == TextureDimension::Texture3D)
            return BackendStatus::Failure(FailureCode::Unsupported, 0, "3D texture readback is not supported by the portable backend contract");
        if (requested.source.mipLevel >= desc.mipCount || requested.source.arraySlice >= desc.arraySize)
            return BackendStatus::Failure(FailureCode::InvalidArgument, 0, "texture readback selects an invalid subresource");

        const Extent3D sourceExtent = GetMipExtent(desc, requested.source.mipLevel);
        TextureReadbackRegion region = requested;
        const bool implicitExtent = region.extent.width == 0 && region.extent.height == 0 && region.extent.depth == 0;
        if (implicitExtent)
        {
            if (region.sourceX != 0 || region.sourceY != 0 || region.sourceZ != 0)
                return BackendStatus::Failure(FailureCode::InvalidArgument, 0, "an implicit texture-readback extent requires a zero source origin");
            region.extent = sourceExtent;
        }
        else if (region.extent.width == 0 || region.extent.height == 0 || region.extent.depth == 0)
            return BackendStatus::Failure(FailureCode::InvalidArgument, 0, "texture readback contains a partially zero extent");
        if (region.sourceX > sourceExtent.width || region.extent.width > sourceExtent.width - region.sourceX || region.sourceY > sourceExtent.height ||
            region.extent.height > sourceExtent.height - region.sourceY || region.sourceZ > sourceExtent.depth ||
            region.extent.depth > sourceExtent.depth - region.sourceZ)
            return BackendStatus::Failure(FailureCode::InvalidArgument, 0, "texture readback region exceeds the selected subresource");
        if (region.extent.depth != 1 || region.sourceZ != 0)
            return BackendStatus::Failure(FailureCode::Unsupported, 0, "portable texture readback currently supports one 2D image plane");
        if (IsBlockCompressed(desc.format) && (!IsBlockRegionValid(region.sourceX, region.extent.width, sourceExtent.width) ||
                                               !IsBlockRegionValid(region.sourceY, region.extent.height, sourceExtent.height)))
            return BackendStatus::Failure(FailureCode::InvalidArgument, 0, "compressed texture-readback regions must follow 4x4 block boundaries");
        if (command->native->getTextureSubresourceState(sourcePayload->native, region.source.arraySlice, region.source.mipLevel) !=
            nvrhi::ResourceStates::CopySource)
            return BackendStatus::Failure(FailureCode::ResourceStateMismatch, 0, "texture readback source is not in copy-source state");

        nvrhi::TextureDesc stagingDesc{};
        stagingDesc.width = region.extent.width;
        stagingDesc.height = region.extent.height;
        stagingDesc.depth = 1;
        stagingDesc.arraySize = 1;
        stagingDesc.mipLevels = 1;
        stagingDesc.sampleCount = 1;
        stagingDesc.format = ToNativeFormat(desc.format);
        stagingDesc.dimension = desc.dimension == TextureDimension::Texture1D ? nvrhi::TextureDimension::Texture1D : nvrhi::TextureDimension::Texture2D;
        nvrhi::StagingTextureHandle native = m_device->createStagingTexture(stagingDesc, nvrhi::CpuAccessMode::Read);
        if (!native)
            return BackendStatus::Failure(FailureCode::OutOfMemory, 0, "failed to create texture readback staging storage");
        TextureReadbackPayload* const payload = AllocatePayload<TextureReadbackPayload>();
        if (payload == nullptr)
            return BackendStatus::Failure(FailureCode::OutOfMemory, 0, "failed to allocate texture readback state");
        payload->native = static_cast<nvrhi::StagingTextureHandle&&>(native);
        payload->format = desc.format;
        payload->extent = region.extent;
        const ResourceRef resource = m_lifetime.Create(ResourceKind::TextureReadback, payload, &DestroyTextureReadback, this);
        if (!resource)
        {
            DestroyTextureReadback(this, {}, payload);
            return BackendStatus::Failure(FailureCode::CapacityExceeded, 0, "texture readback capacity is exhausted");
        }
        readback = CastResourceRef<TextureReadbackRef>(resource);
        if (!TrackCommandResource(*this, *command, ResourceRef(source)) || !TrackCommandResource(*this, *command, resource) ||
            !RegisterCommandSubmissionCallback(commandList, &CompleteTextureReadbackSubmission, this, resource.value))
        {
            static_cast<void>(m_lifetime.Release(resource));
            readback = {};
            return BackendStatus::Failure(FailureCode::OutOfMemory, 0, "failed to retain texture readback submission state");
        }

        nvrhi::TextureSlice sourceSlice;
        sourceSlice.setOrigin(region.sourceX, region.sourceY, region.sourceZ)
            .setSize(region.extent.width, region.extent.height, region.extent.depth)
            .setMipLevel(region.source.mipLevel)
            .setArraySlice(region.source.arraySlice);
        nvrhi::TextureSlice destinationSlice;
        destinationSlice.setOrigin(0, 0, 0).setSize(region.extent.width, region.extent.height, 1).setMipLevel(0).setArraySlice(0);
        command->native->commitBarriers();
        command->native->copyTexture(payload->native, destinationSlice, sourcePayload->native, sourceSlice);
        return BackendStatus::Success();
    }

    BackendStatus CommonBackend::GetTextureReadbackInfo(const TextureReadbackRef readback, TextureReadbackInfo& info) noexcept
    {
        auto* const payload = static_cast<TextureReadbackPayload*>(m_lifetime.GetPayload(ResourceRef(readback)));
        if (payload == nullptr)
            return BackendStatus::Failure(FailureCode::InvalidReference, 0, "texture readback reference is stale");
        concurrency::ScopedLock guard(payload->lock);
        if (payload->state == TextureReadbackState::PendingGpu && IsGpuFenceComplete(payload->completion))
            payload->state = TextureReadbackState::Ready;
        info = {payload->format, payload->extent, payload->state, payload->completion};
        return BackendStatus::Success();
    }

    BackendStatus CommonBackend::MapTextureReadback(const TextureReadbackRef readback, TextureReadbackMapping& mapping) noexcept
    {
        mapping = {};
        auto* const payload = static_cast<TextureReadbackPayload*>(m_lifetime.GetPayload(ResourceRef(readback)));
        if (payload == nullptr)
            return BackendStatus::Failure(FailureCode::InvalidReference, 0, "texture readback reference is stale");
        concurrency::ScopedLock guard(payload->lock);
        if (payload->state == TextureReadbackState::PendingGpu && IsGpuFenceComplete(payload->completion))
            payload->state = TextureReadbackState::Ready;
        if (payload->state == TextureReadbackState::Mapped || payload->mapped)
            return BackendStatus::Failure(FailureCode::Busy, 0, "texture readback is already mapped");
        if (payload->state == TextureReadbackState::Failed)
            return BackendStatus::Failure(FailureCode::BackendFailure, 0, "texture readback command list was discarded before submission");
        if (payload->state != TextureReadbackState::Ready)
            return BackendStatus::Failure(FailureCode::Busy, 0, "texture readback has not completed on the GPU");
        usize rowPitch = 0;
        const void* const data = m_device->mapStagingTexture(payload->native, {}, nvrhi::CpuAccessMode::Read, &rowPitch);
        if (data == nullptr)
            return BackendStatus::Failure(FailureCode::BackendFailure, 0, "failed to map texture readback staging storage");
        const nvrhi::FormatInfo& format = nvrhi::getFormatInfo(ToNativeFormat(payload->format));
        const u64 blockRows = (payload->extent.height + format.blockSize - 1u) / format.blockSize;
        const u64 depthPitch = static_cast<u64>(rowPitch) * blockRows;
        mapping = {data, static_cast<u64>(rowPitch), depthPitch, depthPitch * payload->extent.depth, payload->format, payload->extent};
        payload->mapped = true;
        payload->state = TextureReadbackState::Mapped;
        return BackendStatus::Success();
    }

    BackendStatus CommonBackend::UnmapTextureReadback(const TextureReadbackRef readback) noexcept
    {
        auto* const payload = static_cast<TextureReadbackPayload*>(m_lifetime.GetPayload(ResourceRef(readback)));
        if (payload == nullptr)
            return BackendStatus::Failure(FailureCode::InvalidReference, 0, "texture readback reference is stale");
        concurrency::ScopedLock guard(payload->lock);
        if (!payload->mapped || payload->state != TextureReadbackState::Mapped)
            return BackendStatus::Failure(FailureCode::InvalidArgument, 0, "texture readback is not mapped");
        m_device->unmapStagingTexture(payload->native);
        payload->mapped = false;
        payload->state = TextureReadbackState::Ready;
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
        const nvrhi::CpuAccessMode access = payload->desc.memoryType == MemoryType::Readback ? nvrhi::CpuAccessMode::Read : nvrhi::CpuAccessMode::Write;
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

    BackendStatus CommonBackend::AddCommandListWait(const CommandListRef commandList, const GpuFence fence) noexcept
    {
        auto* const payload = static_cast<CommandListPayload*>(m_lifetime.GetPayload(ResourceRef(commandList)));
        if (payload == nullptr || !payload->open)
            return BackendStatus::Failure(FailureCode::InvalidCommandList, 0, "incoming queue wait requires an open command list");
        if (!fence.IsValid() || (fence.queue != QueueType::Graphics && fence.queue != QueueType::Compute && fence.queue != QueueType::Copy))
            return BackendStatus::Failure(FailureCode::InvalidArgument, 0, "incoming queue wait has an invalid producer fence");
        u64& value = payload->incomingWaits[QueueIndex(fence.queue)];
        if (fence.value > value)
            value = fence.value;
        return BackendStatus::Success();
    }

    BackendStatus CommonBackend::SeedCommandListStates(const CommandListRef commandList, const containers::ArraySpan<const CommandListEntryState> entries) noexcept
    {
        auto* const command = static_cast<CommandListPayload*>(m_lifetime.GetPayload(ResourceRef(commandList)));
        if (command == nullptr || !command->open || command->entryStatesSeeded || command->resources.Size() != 0)
            return BackendStatus::Failure(FailureCode::InvalidCommandList, 0, "entry-state seeding requires an unseeded command list with no recorded resources");

        // Validate the entire immutable table before retaining resources or changing native tracking. Full, sorted texture cells make missing/duplicate state detectable in linear time.
        for (u32 first = 0; first < entries.Size();)
        {
            const ResourceRef reference = entries[first].resource;
            if (!reference.IsValid() || (first != 0 && entries[first - 1u].resource.value >= reference.value))
                return BackendStatus::Failure(FailureCode::InvalidArgument, 0, "entry-state resources must be valid, unique and sorted");
            u32 mipCount = 1;
            u32 cellCount = 1;
            ResourceState initialState = ResourceState::Unknown;
            bool keepInitialState = true;
            if (reference.GetKind() == ResourceKind::Texture)
            {
                const auto* const texture = static_cast<const TexturePayload*>(m_lifetime.GetPayload(reference));
                if (texture == nullptr)
                    return BackendStatus::Failure(FailureCode::InvalidReference, 0, "entry state references a stale texture");
                mipCount = texture->desc.mipCount;
                cellCount = mipCount * texture->desc.arraySize;
                initialState = texture->desc.initialState;
                keepInitialState = texture->desc.keepInitialState;
                if (texture->native->getDesc().keepInitialState != keepInitialState)
                    return BackendStatus::Failure(FailureCode::ResourceStateMismatch, 0, "texture native close-state policy disagrees with its RHI descriptor");
            }
            else if (reference.GetKind() == ResourceKind::Buffer)
            {
                const auto* const buffer = static_cast<const BufferPayload*>(m_lifetime.GetPayload(reference));
                if (buffer == nullptr)
                    return BackendStatus::Failure(FailureCode::InvalidReference, 0, "entry state references a stale buffer");
                if (buffer->desc.memoryType != MemoryType::DeviceLocal)
                    return BackendStatus::Failure(FailureCode::Unsupported, 0, "entry-state seeding does not override upload/readback heap states");
                initialState = buffer->desc.initialState;
                keepInitialState = buffer->desc.keepInitialState;
                if (buffer->native->getDesc().keepInitialState != keepInitialState)
                    return BackendStatus::Failure(FailureCode::ResourceStateMismatch, 0, "buffer native close-state policy disagrees with its RHI descriptor");
            }
            else
                return BackendStatus::Failure(FailureCode::InvalidArgument, 0, "entry state requires a texture or buffer");
            if (cellCount == 0 || cellCount > entries.Size() - first)
                return BackendStatus::Failure(FailureCode::InvalidArgument, 0, "entry-state table is missing texture subresources");
            for (u32 cell = 0; cell < cellCount; ++cell)
            {
                const CommandListEntryState& entry = entries[first + cell];
                if (entry.resource != reference || entry.subresource.mipLevel != cell % mipCount || entry.subresource.arraySlice != cell / mipCount)
                    return BackendStatus::Failure(FailureCode::InvalidArgument, 0, "entry-state subresources must be complete and ordered by slice then mip");
                if (entry.state == ResourceState::Unknown || (static_cast<u32>(entry.state) & ~((1u << 19u) - 1u)) != 0)
                    return BackendStatus::Failure(FailureCode::InvalidArgument, 0, "entry state must be explicit and representable");
                if (keepInitialState && entry.state != initialState)
                    return BackendStatus::Failure(FailureCode::ResourceStateMismatch, 0, "entry state disagrees with automatic close-time initial-state restoration");
            }
            first += cellCount;
        }

        command->entryStatesSeeded = true;
        ResourceRef retained;
        for (const CommandListEntryState& entry : entries)
            if (entry.resource != retained)
            {
                if (!TrackCommandResource(*this, *command, entry.resource))
                    return BackendStatus::Failure(FailureCode::OutOfMemory, 0, "failed to retain entry-state resources; discard the command list");
                retained = entry.resource;
            }
        // Tracking only: no transitions, barrier commits, submission, or mutation of another command list's state.
        for (const CommandListEntryState& entry : entries)
        {
            if (entry.resource.GetKind() == ResourceKind::Texture)
            {
                const auto* const texture = static_cast<const TexturePayload*>(m_lifetime.GetPayload(entry.resource));
                command->native->beginTrackingTextureState(texture->native, nvrhi::TextureSubresourceSet(entry.subresource.mipLevel, 1, entry.subresource.arraySlice, 1), ToNativeState(entry.state));
            }
            else
            {
                const auto* const buffer = static_cast<const BufferPayload*>(m_lifetime.GetPayload(entry.resource));
                command->native->beginTrackingBufferState(buffer->native, ToNativeState(entry.state));
            }
        }
        return BackendStatus::Success();
    }

    BackendStatus CommonBackend::TransitionTexture(const CommandListRef commandList, const TextureRef texture, const ResourceState before,
                                                   const ResourceState after, const SubresourceRange& range) noexcept
    {
        auto* const command = static_cast<CommandListPayload*>(m_lifetime.GetPayload(ResourceRef(commandList)));
        auto* const resource = static_cast<TexturePayload*>(m_lifetime.GetPayload(ResourceRef(texture)));
        if (command == nullptr || !command->open)
            return BackendStatus::Failure(FailureCode::InvalidCommandList, 0, "texture transition requires an open command list");
        if (resource == nullptr)
            return BackendStatus::Failure(FailureCode::InvalidReference, 0, "texture transition references a stale texture");
        SubresourceRange resolved;
        if (!ResolveSubresources(resource->desc, range, resolved))
            return BackendStatus::Failure(FailureCode::InvalidArgument, 0, "texture transition contains an invalid subresource range");
        if (before != ResourceState::Unknown || !resource->desc.keepInitialState)
        {
            const nvrhi::ResourceStates expected = ToNativeState(before);
            for (u32 slice = resolved.firstSlice; slice < static_cast<u32>(resolved.firstSlice) + resolved.sliceCount; ++slice)
                for (u32 mip = resolved.firstMip; mip < static_cast<u32>(resolved.firstMip) + resolved.mipCount; ++mip)
                {
                    const nvrhi::ResourceStates tracked = command->native->getTextureSubresourceState(resource->native, slice, mip);
                    if (tracked == nvrhi::ResourceStates::Unknown || (before != ResourceState::Unknown && tracked != expected))
                        return BackendStatus::Failure(FailureCode::ResourceStateMismatch, 0,
                                                      "texture transition before-state does not match command-list tracking");
                }
        }
        if (!TrackCommandResource(*this, *command, ResourceRef(texture)))
            return BackendStatus::Failure(FailureCode::OutOfMemory, 0, "failed to retain a texture transition resource");
        const nvrhi::TextureSubresourceSet subresources(resolved.firstMip, resolved.mipCount, resolved.firstSlice, resolved.sliceCount);
        command->native->setTextureState(resource->native, subresources, ToNativeState(after));
        return BackendStatus::Success();
    }
    BackendStatus CommonBackend::TransitionBuffer(const CommandListRef commandList, const BufferRef buffer, const ResourceState before,
                                                  const ResourceState after) noexcept
    {
        auto* const command = static_cast<CommandListPayload*>(m_lifetime.GetPayload(ResourceRef(commandList)));
        auto* const resource = static_cast<BufferPayload*>(m_lifetime.GetPayload(ResourceRef(buffer)));
        if (command == nullptr || !command->open)
            return BackendStatus::Failure(FailureCode::InvalidCommandList, 0, "buffer transition requires an open command list");
        if (resource == nullptr)
            return BackendStatus::Failure(FailureCode::InvalidReference, 0, "buffer transition references a stale buffer");
        if (before != ResourceState::Unknown || !resource->desc.keepInitialState)
        {
            nvrhi::ResourceStates tracked = command->native->getBufferState(resource->native);
            if (tracked == nvrhi::ResourceStates::Unknown && resource->desc.keepInitialState)
                tracked = ToNativeState(resource->desc.initialState);
            if (tracked == nvrhi::ResourceStates::Unknown || (before != ResourceState::Unknown && tracked != ToNativeState(before)))
                return BackendStatus::Failure(FailureCode::ResourceStateMismatch, 0, "buffer transition before-state does not match command-list tracking");
        }
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
    BackendStatus CommonBackend::ActivateAliasedResource(const CommandListRef commandList, const ResourceRef destination,
                                                         const containers::ArraySpan<const ResourceRef> predecessors) noexcept
    {
        auto* const command = static_cast<CommandListPayload*>(m_lifetime.GetPayload(ResourceRef(commandList)));
        if (command == nullptr || !command->open || (command->type != CommandListType::Default && command->type != CommandListType::Compute))
            return BackendStatus::Failure(FailureCode::InvalidCommandList, 0, "alias activation requires an open graphics or compute command list");
        if ((destination.GetKind() != ResourceKind::Texture && destination.GetKind() != ResourceKind::Buffer) || predecessors.Size() == 0)
            return BackendStatus::Failure(FailureCode::InvalidArgument, 0, "alias activation requires a placed destination and predecessor set");

        auto resolvePlacedResource = [this](const ResourceRef resource, PlacementRecord& placement,
                                            nvrhi::IResource** const native) noexcept -> BackendStatus {
            placement = {};
            nvrhi::IResource* resolvedNative = nullptr;
            if (resource.GetKind() == ResourceKind::Texture)
            {
                const auto* const payload = static_cast<const TexturePayload*>(m_lifetime.GetPayload(resource));
                if (payload == nullptr)
                    return BackendStatus::Failure(FailureCode::InvalidReference, 0, "alias activation references a stale texture");
                placement = payload->placement;
                resolvedNative = payload->native.Get();
            }
            else if (resource.GetKind() == ResourceKind::Buffer)
            {
                const auto* const payload = static_cast<const BufferPayload*>(m_lifetime.GetPayload(resource));
                if (payload == nullptr)
                    return BackendStatus::Failure(FailureCode::InvalidReference, 0, "alias activation references a stale buffer");
                placement = payload->placement;
                resolvedNative = payload->native.Get();
            }
            else
                return BackendStatus::Failure(FailureCode::InvalidReference, 0, "alias activation references a non-placeable resource");
            if (!placement.IsValid())
                return BackendStatus::Failure(FailureCode::MissingBinding, 0, "alias activation requires immutable heap placements");
            if (resolvedNative == nullptr)
                return BackendStatus::Failure(FailureCode::BackendFailure, 0, "immutable placement metadata disagrees with its native resource");
            if (native != nullptr)
                *native = resolvedNative;
            return BackendStatus::Success();
        };

        PlacementRecord destinationPlacement;
        nvrhi::IResource* destinationNative = nullptr;
        bool requiresExplicitDiscard = false;
        {
            BackendStatus validation = resolvePlacedResource(destination, destinationPlacement, &destinationNative);
            if (!validation)
                return validation;
            if (destinationPlacement.offset > ~u64{0} - destinationPlacement.size)
                return BackendStatus::Failure(FailureCode::BackendFailure, 0, "destination placement range overflows");
            const u64 destinationEnd = destinationPlacement.offset + destinationPlacement.size;
            u64 coverageCursor = destinationPlacement.offset;
            for (u32 index = 0; index < predecessors.Size(); ++index)
            {
                const ResourceRef predecessor = predecessors[index];
                PlacementRecord predecessorPlacement;
                validation = resolvePlacedResource(predecessor, predecessorPlacement, nullptr);
                if (!validation)
                    return validation;
                if (predecessor == destination || predecessor.GetKind() != destination.GetKind() ||
                    predecessorPlacement.heap != destinationPlacement.heap ||
                    predecessorPlacement.memoryType != destinationPlacement.memoryType ||
                    predecessorPlacement.heapCategory != destinationPlacement.heapCategory ||
                    predecessorPlacement.compatibilityClass != destinationPlacement.compatibilityClass)
                    return BackendStatus::Failure(FailureCode::IncompatibleBinding, 0,
                                                  "alias predecessor is not a distinct resource in the destination's compatible heap");
                if (predecessorPlacement.offset > ~u64{0} - predecessorPlacement.size)
                    return BackendStatus::Failure(FailureCode::BackendFailure, 0, "predecessor placement range overflows");
                const u64 predecessorEnd = predecessorPlacement.offset + predecessorPlacement.size;
                const u64 fragmentBegin = predecessorPlacement.offset > destinationPlacement.offset ? predecessorPlacement.offset : destinationPlacement.offset;
                const u64 fragmentEnd = predecessorEnd < destinationEnd ? predecessorEnd : destinationEnd;
                if (fragmentBegin >= fragmentEnd)
                    return BackendStatus::Failure(FailureCode::IncompatibleBinding, 0, "alias predecessor does not overlap the destination placement");
                if (fragmentBegin != coverageCursor)
                    return BackendStatus::Failure(FailureCode::IncompatibleBinding, 0,
                                                  fragmentBegin < coverageCursor ? "alias predecessor fragments overlap or are out of order"
                                                                                 : "alias predecessor fragments leave a coverage gap");
                coverageCursor = fragmentEnd;
            }
            if (coverageCursor != destinationEnd)
                return BackendStatus::Failure(FailureCode::IncompatibleBinding, 0, "alias predecessor fragments do not exactly cover the destination");

            const TexturePayload* destinationTexture =
                destination.GetKind() == ResourceKind::Texture ? static_cast<const TexturePayload*>(m_lifetime.GetPayload(destination)) : nullptr;
            requiresExplicitDiscard =
                destinationTexture != nullptr &&
                (HasFlag(destinationTexture->desc.usage, TextureUsage::RenderTarget) || HasFlag(destinationTexture->desc.usage, TextureUsage::DepthStencil));
            if (requiresExplicitDiscard && command->type == CommandListType::Compute &&
                !HasFlag(destinationTexture->desc.usage, TextureUsage::UnorderedAccess))
                return BackendStatus::Failure(FailureCode::IncompatibleBinding, 0,
                                              "texture alias activation has no discard-compatible usage on the compute queue");
        }

        if (!TrackCommandResource(*this, *command, destination))
            return BackendStatus::Failure(FailureCode::OutOfMemory, 0, "failed to retain alias destination");
        for (const ResourceRef predecessor : predecessors)
            if (!TrackCommandResource(*this, *command, predecessor))
                return BackendStatus::Failure(FailureCode::OutOfMemory, 0, "failed to retain alias predecessor");
        command->native->commitBarriers();
        if (!m_aliasingBarrier(m_fenceContext, command->native, destinationNative))
            return BackendStatus::Failure(FailureCode::BackendFailure, 0, "native alias activation barrier failed");
        if (!requiresExplicitDiscard)
            return BackendStatus::Success();
        const TextureRef destinationRef{destination.Index(), destination.GetGeneration()};
        return DiscardTexture(commandList, destinationRef, {});
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
    bool CommonBackend::RetainCommandResource(const CommandListRef commandList, const ResourceRef resource) noexcept
    {
        auto* const command = static_cast<CommandListPayload*>(m_lifetime.GetPayload(ResourceRef(commandList)));
        return command != nullptr && command->open && resource.IsValid() && TrackCommandResource(*this, *command, resource);
    }
    bool CommonBackend::RegisterCommandSubmissionCallback(const CommandListRef commandList, const CommandSubmissionCallback callback, void* const context,
                                                          const u64 value, bool* const added) noexcept
    {
        if (added != nullptr)
            *added = false;
        auto* const command = static_cast<CommandListPayload*>(m_lifetime.GetPayload(ResourceRef(commandList)));
        if (command == nullptr || !command->open || callback == nullptr)
            return false;
        for (const CommandListPayload::SubmissionCallback& existing : command->submissionCallbacks)
            if (existing.callback == callback && existing.context == context && existing.value == value)
                return true;
        const u32 expected = command->submissionCallbacks.Size() + 1u;
        command->submissionCallbacks.PushBack({callback, context, value});
        const bool succeeded = command->submissionCallbacks.Size() == expected;
        if (succeeded && added != nullptr)
            *added = true;
        return succeeded;
    }

    nvrhi::ICommandList* CommonBackend::GetNativeCommandList(const CommandListRef commandList) noexcept
    {
        auto* const command = static_cast<CommandListPayload*>(m_lifetime.GetPayload(ResourceRef(commandList)));
        return command != nullptr && command->open ? command->native.Get() : nullptr;
    }

    nvrhi::IHeap* CommonBackend::GetNativeHeap(const HeapRef heap) noexcept
    {
        auto* const payload = static_cast<HeapPayload*>(m_lifetime.GetPayload(ResourceRef(heap)));
        return payload != nullptr ? payload->native.Get() : nullptr;
    }

    ResourceRef CommonBackend::GetResidencyAllocation(const ResourceRef resource) noexcept
    {
        if (resource.GetKind() == ResourceKind::Heap)
            return m_lifetime.GetPayload(resource) != nullptr ? resource : ResourceRef{};
        if (resource.GetKind() == ResourceKind::Texture)
        {
            const auto* const payload = static_cast<const TexturePayload*>(m_lifetime.GetPayload(resource));
            return payload != nullptr ? (payload->placement.IsValid() ? ResourceRef(payload->placement.heap) : resource) : ResourceRef{};
        }
        if (resource.GetKind() == ResourceKind::Buffer)
        {
            const auto* const payload = static_cast<const BufferPayload*>(m_lifetime.GetPayload(resource));
            return payload != nullptr ? (payload->placement.IsValid() ? ResourceRef(payload->placement.heap) : resource) : ResourceRef{};
        }
        return {};
    }

    FenceSet CommonBackend::GetResourceLastUse(const ResourceRef resource) const noexcept
    {
        return m_lifetime.GetLastUse(resource);
    }

    nvrhi::Object CommonBackend::GetNativeObject(const ResourceRef resource, const nvrhi::ObjectType objectType) noexcept
    {
        void* const address = m_lifetime.GetPayload(resource);
        if (address == nullptr)
            return nullptr;
        switch (resource.GetKind())
        {
        case ResourceKind::Texture:
            return static_cast<TexturePayload*>(address)->native->getNativeObject(objectType);
        case ResourceKind::Buffer:
            return static_cast<BufferPayload*>(address)->native->getNativeObject(objectType);
        case ResourceKind::Heap:
            return static_cast<HeapPayload*>(address)->native->getNativeObject(objectType);
        case ResourceKind::SamplerState:
            return static_cast<SamplerPayload*>(address)->native->getNativeObject(objectType);
        case ResourceKind::Shader:
            return static_cast<ShaderPayload*>(address)->native->getNativeObject(objectType);
        case ResourceKind::Pipeline:
        {
            auto* const pipeline = static_cast<PipelinePayload*>(address);
            if (pipeline->kind == PipelineKind::Graphics)
                return pipeline->graphics->getNativeObject(objectType);
            if (pipeline->kind == PipelineKind::Compute)
                return pipeline->compute->getNativeObject(objectType);
            return pipeline->rayTracing->getNativeObject(objectType);
        }
        case ResourceKind::CommandList:
            return static_cast<CommandListPayload*>(address)->native->getNativeObject(objectType);
        default:
            return nullptr;
        }
    }
    void CommonBackend::SetResourceDebugName(const TextureRef resource, const char* const name) noexcept
    {
        auto* const payload = static_cast<TexturePayload*>(m_lifetime.GetPayload(ResourceRef(resource)));
        if (payload != nullptr)
            CopyDebugName(payload->debugName, sizeof(payload->debugName), name);
    }
    void CommonBackend::SetResourceDebugName(const TextureReadbackRef resource, const char* const name) noexcept
    {
        auto* const payload = static_cast<TextureReadbackPayload*>(m_lifetime.GetPayload(ResourceRef(resource)));
        if (payload != nullptr)
            CopyDebugName(payload->debugName, sizeof(payload->debugName), name);
    }
    void CommonBackend::SetResourceDebugName(const BufferRef resource, const char* const name) noexcept
    {
        auto* const payload = static_cast<BufferPayload*>(m_lifetime.GetPayload(ResourceRef(resource)));
        if (payload != nullptr)
            CopyDebugName(payload->debugName, sizeof(payload->debugName), name);
    }
    void CommonBackend::SetResourceDebugName(const HeapRef resource, const char* const name) noexcept
    {
        auto* const payload = static_cast<HeapPayload*>(m_lifetime.GetPayload(ResourceRef(resource)));
        if (payload != nullptr)
            CopyDebugName(payload->debugName, sizeof(payload->debugName), name);
    }
    void CommonBackend::SetResourceDebugName(const SamplerStateRef resource, const char* const name) noexcept
    {
        auto* const payload = static_cast<SamplerPayload*>(m_lifetime.GetPayload(ResourceRef(resource)));
        if (payload != nullptr)
            CopyDebugName(payload->debugName, sizeof(payload->debugName), name);
    }
    void CommonBackend::SetResourceDebugName(const ShaderRef resource, const char* const name) noexcept
    {
        auto* const payload = static_cast<ShaderPayload*>(m_lifetime.GetPayload(ResourceRef(resource)));
        if (payload != nullptr)
            CopyDebugName(payload->debugName, sizeof(payload->debugName), name);
    }
    void CommonBackend::SetResourceDebugName(const VertexLayoutRef resource, const char* const name) noexcept
    {
        auto* const payload = static_cast<VertexLayoutPayload*>(m_lifetime.GetPayload(ResourceRef(resource)));
        if (payload != nullptr)
            CopyDebugName(payload->debugName, sizeof(payload->debugName), name);
    }
    void CommonBackend::SetResourceDebugName(const PipelineRef resource, const char* const name) noexcept
    {
        auto* const payload = static_cast<PipelinePayload*>(m_lifetime.GetPayload(ResourceRef(resource)));
        if (payload != nullptr)
            CopyDebugName(payload->debugName, sizeof(payload->debugName), name);
    }
    void CommonBackend::SetResourceDebugName(const CommandListRef resource, const char* const name) noexcept
    {
        auto* const payload = static_cast<CommandListPayload*>(m_lifetime.GetPayload(ResourceRef(resource)));
        if (payload != nullptr)
            CopyDebugName(payload->debugName, sizeof(payload->debugName), name);
    }

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
