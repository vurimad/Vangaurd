#pragma once

#include <vanguard/system/types.hpp>

namespace vanguard::rhi
{
    struct GpuFence;
    class IBackend;

    inline constexpr u32 InvalidReferenceIndex = 0xffffffffu;
    inline constexpr u32 MaximumResourceReferenceIndex = 0x0fffffffu;
    inline constexpr u32 MaximumColorAttachments = 8;
    inline constexpr u32 MaximumCommandListsPerSubmission = 128;
    inline constexpr u32 MaximumBindingLayoutEntries = 64;
    inline constexpr u32 MaximumBindingLayoutsPerPipeline = 16;
    inline constexpr u32 MaximumDescriptorDomainsPerPipeline = 4;
    inline constexpr u32 MaximumFixedBindingArraySize = 65535;
    inline constexpr u32 MaximumVertexBindings = 16;
    inline constexpr u32 MaximumVertexAttributes = 32;
    inline constexpr u32 MaximumViewports = 16;
    inline constexpr u32 MaximumVertexSemanticNameLength = 32;
    inline constexpr u32 MaximumRayTracingShaders = 4096;
    inline constexpr u32 MaximumRayTracingHitGroups = 4096;
    inline constexpr u32 MaximumTextureSubresourcesPerUpload = 4096;

    template <typename Tag> struct Reference
    {
        u32 index = InvalidReferenceIndex;
        u32 generation = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return index != InvalidReferenceIndex && generation != 0;
        }
        [[nodiscard]] constexpr explicit operator bool() const noexcept
        {
            return IsValid();
        }
        [[nodiscard]] friend constexpr bool operator==(const Reference&, const Reference&) noexcept = default;
    };

    struct TextureTag;
    struct TextureReadbackTag;
    struct BufferTag;
    struct HeapTag;
    struct SamplerStateTag;
    struct ShaderTag;
    struct VertexLayoutTag;
    struct PipelineTag;
    struct BindingLayoutTag;
    struct DescriptorDomainTag;
    struct AccelerationStructureTag;
    struct ShaderTableTag;
    struct QueryPoolTag;
    struct SwapChainTag;
    struct CommandListTag;

    using TextureRef = Reference<TextureTag>;
    using TextureReadbackRef = Reference<TextureReadbackTag>;
    using BufferRef = Reference<BufferTag>;
    using HeapRef = Reference<HeapTag>;
    using SamplerStateRef = Reference<SamplerStateTag>;
    using ShaderRef = Reference<ShaderTag>;
    using VertexLayoutRef = Reference<VertexLayoutTag>;
    using PipelineRef = Reference<PipelineTag>;
    using BindingLayoutRef = Reference<BindingLayoutTag>;
    using DescriptorDomainRef = Reference<DescriptorDomainTag>;
    using AccelerationStructureRef = Reference<AccelerationStructureTag>;
    using ShaderTableRef = Reference<ShaderTableTag>;
    using QueryPoolRef = Reference<QueryPoolTag>;
    using SwapChainRef = Reference<SwapChainTag>;
    using CommandListRef = Reference<CommandListTag>;

    template <typename ReferenceType> inline constexpr bool IsReferenceCountedResource = false;
    template <> inline constexpr bool IsReferenceCountedResource<TextureRef> = true;
    template <> inline constexpr bool IsReferenceCountedResource<TextureReadbackRef> = true;
    template <> inline constexpr bool IsReferenceCountedResource<BufferRef> = true;
    template <> inline constexpr bool IsReferenceCountedResource<HeapRef> = true;
    template <> inline constexpr bool IsReferenceCountedResource<SamplerStateRef> = true;
    template <> inline constexpr bool IsReferenceCountedResource<ShaderRef> = true;
    template <> inline constexpr bool IsReferenceCountedResource<PipelineRef> = true;
    template <> inline constexpr bool IsReferenceCountedResource<BindingLayoutRef> = true;
    template <> inline constexpr bool IsReferenceCountedResource<DescriptorDomainRef> = true;
    template <> inline constexpr bool IsReferenceCountedResource<AccelerationStructureRef> = true;
    template <> inline constexpr bool IsReferenceCountedResource<ShaderTableRef> = true;
    template <> inline constexpr bool IsReferenceCountedResource<SwapChainRef> = true;

    enum class ResourceKind : u8
    {
        None,
        Texture,
        Buffer,
        Heap,
        SamplerState,
        Shader,
        VertexLayout,
        Pipeline,
        BindingLayout,
        DescriptorDomain,
        AccelerationStructure,
        ShaderTable,
        SwapChain,
        CommandList,
        TextureReadback,
        Count
    };
    static_assert(static_cast<u8>(ResourceKind::Count) <= 16, "ResourceRef reserves four bits for resource kind");

    // Type-erased GPU identity used by allocators and resource tables. It is never accepted by recording APIs
    // without an explicit checked conversion back to a typed reference.
    struct ResourceRef
    {
        u64 value = 0;

        constexpr ResourceRef() noexcept = default;
        constexpr ResourceRef(const TextureRef reference) noexcept : value(Pack(ResourceKind::Texture, reference.index, reference.generation)) {}
        constexpr ResourceRef(const TextureReadbackRef reference) noexcept : value(Pack(ResourceKind::TextureReadback, reference.index, reference.generation))
        {
        }
        constexpr ResourceRef(const BufferRef reference) noexcept : value(Pack(ResourceKind::Buffer, reference.index, reference.generation)) {}
        constexpr ResourceRef(const HeapRef reference) noexcept : value(Pack(ResourceKind::Heap, reference.index, reference.generation)) {}
        constexpr ResourceRef(const SamplerStateRef reference) noexcept : value(Pack(ResourceKind::SamplerState, reference.index, reference.generation)) {}
        constexpr ResourceRef(const ShaderRef reference) noexcept : value(Pack(ResourceKind::Shader, reference.index, reference.generation)) {}
        constexpr ResourceRef(const VertexLayoutRef reference) noexcept : value(Pack(ResourceKind::VertexLayout, reference.index, reference.generation)) {}
        constexpr ResourceRef(const PipelineRef reference) noexcept : value(Pack(ResourceKind::Pipeline, reference.index, reference.generation)) {}
        constexpr ResourceRef(const BindingLayoutRef reference) noexcept : value(Pack(ResourceKind::BindingLayout, reference.index, reference.generation)) {}
        constexpr ResourceRef(const DescriptorDomainRef reference) noexcept : value(Pack(ResourceKind::DescriptorDomain, reference.index, reference.generation))
        {
        }
        constexpr ResourceRef(const AccelerationStructureRef reference) noexcept
            : value(Pack(ResourceKind::AccelerationStructure, reference.index, reference.generation))
        {
        }
        constexpr ResourceRef(const ShaderTableRef reference) noexcept : value(Pack(ResourceKind::ShaderTable, reference.index, reference.generation)) {}
        constexpr ResourceRef(const SwapChainRef reference) noexcept : value(Pack(ResourceKind::SwapChain, reference.index, reference.generation)) {}
        constexpr ResourceRef(const CommandListRef reference) noexcept : value(Pack(ResourceKind::CommandList, reference.index, reference.generation)) {}

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return GetKind() != ResourceKind::None && Index() <= MaximumResourceReferenceIndex && GetGeneration() != 0;
        }
        [[nodiscard]] constexpr ResourceKind GetKind() const noexcept
        {
            return static_cast<ResourceKind>(value >> 60u);
        }
        [[nodiscard]] constexpr u32 Index() const noexcept
        {
            return static_cast<u32>(value & MaximumResourceReferenceIndex);
        }
        [[nodiscard]] constexpr u32 GetGeneration() const noexcept
        {
            return static_cast<u32>((value >> 28u) & 0xffffffffu);
        }
        [[nodiscard]] constexpr explicit operator bool() const noexcept
        {
            return IsValid();
        }
        [[nodiscard]] friend constexpr bool operator==(const ResourceRef&, const ResourceRef&) noexcept = default;

        [[nodiscard]] static constexpr ResourceRef FromParts(const ResourceKind kind, const u32 index, const u32 generation) noexcept
        {
            ResourceRef result;
            result.value = Pack(kind, index, generation);
            return result;
        }

    private:
        [[nodiscard]] static constexpr u64 Pack(const ResourceKind kind, const u32 index, const u32 generation) noexcept
        {
            return kind != ResourceKind::None && index <= MaximumResourceReferenceIndex && generation != 0
                       ? (static_cast<u64>(kind) << 60u) | (static_cast<u64>(generation) << 28u) | index
                       : 0;
        }
    };

    // Non-owning ticket for one exact ResourceRef generation. The ticket does not
    // keep the resource alive; completion is published only when the lifetime
    // manager has destroyed the native payload and advanced the slot generation.
    // Tickets are scoped to one initialized backend and must be reset before RHI
    // shutdown; they are deliberately not cross-device identities.
    class NativeReleaseObservation
    {
    public:
        constexpr NativeReleaseObservation() noexcept = default;
        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return m_resource.IsValid();
        }
        [[nodiscard]] constexpr explicit operator bool() const noexcept
        {
            return IsValid();
        }
        constexpr void Reset() noexcept
        {
            m_resource = {};
        }
        [[nodiscard]] friend constexpr bool operator==(const NativeReleaseObservation&, const NativeReleaseObservation&) noexcept = default;

    private:
        friend class IBackend;
        explicit constexpr NativeReleaseObservation(const ResourceRef resource) noexcept : m_resource(resource) {}
        ResourceRef m_resource;
    };

    static_assert(sizeof(ResourceRef) == sizeof(TextureRef));
    static_assert(sizeof(NativeReleaseObservation) == sizeof(ResourceRef));

    template <typename ReferenceType> [[nodiscard]] constexpr ResourceKind GetResourceKind() noexcept;
    template <> [[nodiscard]] constexpr ResourceKind GetResourceKind<TextureRef>() noexcept
    {
        return ResourceKind::Texture;
    }
    template <> [[nodiscard]] constexpr ResourceKind GetResourceKind<TextureReadbackRef>() noexcept
    {
        return ResourceKind::TextureReadback;
    }
    template <> [[nodiscard]] constexpr ResourceKind GetResourceKind<BufferRef>() noexcept
    {
        return ResourceKind::Buffer;
    }
    template <> [[nodiscard]] constexpr ResourceKind GetResourceKind<HeapRef>() noexcept
    {
        return ResourceKind::Heap;
    }
    template <> [[nodiscard]] constexpr ResourceKind GetResourceKind<SamplerStateRef>() noexcept
    {
        return ResourceKind::SamplerState;
    }
    template <> [[nodiscard]] constexpr ResourceKind GetResourceKind<ShaderRef>() noexcept
    {
        return ResourceKind::Shader;
    }
    template <> [[nodiscard]] constexpr ResourceKind GetResourceKind<VertexLayoutRef>() noexcept
    {
        return ResourceKind::VertexLayout;
    }
    template <> [[nodiscard]] constexpr ResourceKind GetResourceKind<PipelineRef>() noexcept
    {
        return ResourceKind::Pipeline;
    }
    template <> [[nodiscard]] constexpr ResourceKind GetResourceKind<BindingLayoutRef>() noexcept
    {
        return ResourceKind::BindingLayout;
    }
    template <> [[nodiscard]] constexpr ResourceKind GetResourceKind<DescriptorDomainRef>() noexcept
    {
        return ResourceKind::DescriptorDomain;
    }
    template <> [[nodiscard]] constexpr ResourceKind GetResourceKind<AccelerationStructureRef>() noexcept
    {
        return ResourceKind::AccelerationStructure;
    }
    template <> [[nodiscard]] constexpr ResourceKind GetResourceKind<ShaderTableRef>() noexcept
    {
        return ResourceKind::ShaderTable;
    }
    template <> [[nodiscard]] constexpr ResourceKind GetResourceKind<SwapChainRef>() noexcept
    {
        return ResourceKind::SwapChain;
    }
    template <> [[nodiscard]] constexpr ResourceKind GetResourceKind<CommandListRef>() noexcept
    {
        return ResourceKind::CommandList;
    }

    template <typename ReferenceType> [[nodiscard]] constexpr ReferenceType CastResourceRef(const ResourceRef resource) noexcept
    {
        return resource.GetKind() == GetResourceKind<ReferenceType>() && resource.IsValid() ? ReferenceType{resource.Index(), resource.GetGeneration()}
                                                                                         : ReferenceType{};
    }

    enum class BackendKind : u8
    {
        Unknown,
        D3D12,
        Vulkan
    };
    enum class DeviceVendor : u8
    {
        Unknown,
        Nvidia,
        Amd,
        Intel
    };
    enum class DeviceState : u8
    {
        Operational,
        ResetRequired,
        Removed,
        Suspended,
        Unknown
    };
    enum class QueueType : u8
    {
        Graphics,
        Compute,
        Copy
    };

    // Command-list roles describe submission and synchronization behavior rather than backend API types.
    enum class CommandListType : u8
    {
        None,
        Default,
        CopySync,
        CopyAsync,
        Compute
    };
    enum class CommandListSyncType : u8
    {
        None,
        ForkAsyncCompute,
        JoinAsyncCompute
    };

    [[nodiscard]] constexpr QueueType GetQueueType(const CommandListType type) noexcept
    {
        if (type == CommandListType::Compute)
            return QueueType::Compute;
        if (type == CommandListType::CopyAsync)
            return QueueType::Copy;
        return QueueType::Graphics;
    }

    enum class Format : u16
    {
        Unknown,
        R8UNorm,
        R8SNorm,
        R8UInt,
        R8G8UNorm,
        R8G8SNorm,
        R8G8UInt,
        R8G8B8A8UNorm,
        R8G8B8A8UNormSrgb,
        R8G8B8A8SNorm,
        R8G8B8A8UInt,
        B8G8R8A8UNorm,
        B8G8R8A8UNormSrgb,
        R16UNorm,
        R16SNorm,
        R16UInt,
        R16Float,
        R16G16UNorm,
        R16G16SNorm,
        R16G16UInt,
        R16G16Float,
        R16G16B16A16UNorm,
        R16G16B16A16SNorm,
        R16G16B16A16UInt,
        R16G16B16A16Float,
        R32UInt,
        R32Float,
        R32G32UInt,
        R32G32Float,
        R32G32B32Float,
        R32G32B32A32Float,
        R10G10B10A2UNorm,
        R11G11B10Float,
        D16UNorm,
        D24UNormS8UInt,
        D32Float,
        D32FloatS8UInt,
        BC1UNorm,
        BC1UNormSrgb,
        BC2UNorm,
        BC2UNormSrgb,
        BC3UNorm,
        BC3UNormSrgb,
        BC4UNorm,
        BC4SNorm,
        BC5UNorm,
        BC5SNorm,
        BC6HUFloat,
        BC6HSFloat,
        BC7UNorm,
        BC7UNormSrgb
    };

    enum class TextureDimension : u8
    {
        Texture1D,
        Texture2D,
        Texture3D,
        TextureCube
    };
    enum class MemoryType : u8
    {
        DeviceLocal,
        Upload,
        Readback
    };

    // A heap category is a native-placement compatibility boundary, not a
    // resource usage hint. Stage 3 deliberately keeps buffers and textures in
    // separate categories even on D3D12 heap-tier-2 hardware.
    enum class PlacedHeapCategory : u8
    {
        None,
        Buffer,
        Texture
    };

    enum class PlacedAliasDiscardLowering : u8
    {
        Unsupported,
        LegacyBarrierAndDiscard,
        EnhancedBarrierAndDiscard
    };

    inline constexpr u64 DeviceLocalBufferCompatibilityClass = 1;
    inline constexpr u64 DeviceLocalTextureCompatibilityClass = 2;
    inline constexpr u64 UploadBufferCompatibilityClass = 3;
    inline constexpr u64 ReadbackBufferCompatibilityClass = 4;
    enum class MemorySegment : u8
    {
        Local,
        NonLocal
    };
    enum class ResidencyPriority : u8
    {
        Minimum,
        Low,
        Normal,
        High,
        Maximum
    };
    enum class ShaderStage : u8
    {
        Vertex,
        Hull,
        Domain,
        Geometry,
        Pixel,
        Compute,
        Mesh,
        Amplification,
        RayGeneration,
        Miss,
        ClosestHit,
        AnyHit,
        Intersection,
        Callable,
        Count
    };
    using ShaderStageMask = u32;
    [[nodiscard]] constexpr ShaderStageMask ShaderStageBit(const ShaderStage stage) noexcept
    {
        return stage < ShaderStage::Count ? 1u << static_cast<u32>(stage) : 0;
    }
    enum class QueryType : u8
    {
        Occlusion,
        PipelineStatistics,
        Timestamp,
        AccelerationStructureCompactedSize
    };

    enum class VariableRateShadingTier : u8
    {
        None,
        PerDraw,
        ShadingRateImage
    };
    enum class ShadingRate : u8
    {
        Rate1x1,
        Rate1x2,
        Rate2x1,
        Rate2x2,
        Rate2x4,
        Rate4x2,
        Rate4x4,
        Count
    };
    using ShadingRateMask = u32;
    [[nodiscard]] constexpr ShadingRateMask ShadingRateBit(const ShadingRate rate) noexcept
    {
        return rate < ShadingRate::Count ? 1u << static_cast<u32>(rate) : 0;
    }
    enum class ShadingRateCombiner : u8
    {
        Passthrough,
        Override,
        Minimum,
        Maximum,
        ApplyRelative,
        Count
    };
    using ShadingRateCombinerMask = u32;
    [[nodiscard]] constexpr ShadingRateCombinerMask ShadingRateCombinerBit(const ShadingRateCombiner combiner) noexcept
    {
        return combiner < ShadingRateCombiner::Count ? 1u << static_cast<u32>(combiner) : 0;
    }

    struct VariableRateShadingCapabilities
    {
        VariableRateShadingTier tier = VariableRateShadingTier::None;
        ShadingRateMask supportedRates = ShadingRateBit(ShadingRate::Rate1x1);
        ShadingRateCombinerMask supportedCombiners = ShadingRateCombinerBit(ShadingRateCombiner::Passthrough);
        u16 shadingRateImageTileWidth = 0;
        u16 shadingRateImageTileHeight = 0;
        bool perPrimitive = false;

        [[nodiscard]] constexpr bool Supports(const ShadingRate rate) const noexcept
        {
            return (supportedRates & ShadingRateBit(rate)) != 0;
        }
        [[nodiscard]] constexpr bool Supports(const ShadingRateCombiner combiner) const noexcept
        {
            return (supportedCombiners & ShadingRateCombinerBit(combiner)) != 0;
        }
    };

    struct PipelineStatistics
    {
        u64 inputAssemblerVertices = 0;
        u64 inputAssemblerPrimitives = 0;
        u64 vertexShaderInvocations = 0;
        u64 geometryShaderInvocations = 0;
        u64 geometryShaderPrimitives = 0;
        u64 clippingInvocations = 0;
        u64 clippingPrimitives = 0;
        u64 pixelShaderInvocations = 0;
        u64 hullShaderInvocations = 0;
        u64 domainShaderInvocations = 0;
        u64 computeShaderInvocations = 0;
    };

    enum class TextureUsage : u32
    {
        None = 0,
        ShaderResource = 1u << 0u,
        UnorderedAccess = 1u << 1u,
        RenderTarget = 1u << 2u,
        DepthStencil = 1u << 3u,
        CopySource = 1u << 4u,
        CopyDestination = 1u << 5u,
        ResolveSource = 1u << 6u,
        ResolveDestination = 1u << 7u,
        Present = 1u << 8u,
        ShadingRate = 1u << 9u,
        RayTracing = 1u << 10u
    };

    enum class BufferUsage : u32
    {
        None = 0,
        Vertex = 1u << 0u,
        Index = 1u << 1u,
        Constant = 1u << 2u,
        Structured = 1u << 3u,
        Raw = 1u << 4u,
        IndirectArguments = 1u << 5u,
        ShaderResource = 1u << 6u,
        UnorderedAccess = 1u << 7u,
        CopySource = 1u << 8u,
        CopyDestination = 1u << 9u,
        AccelerationStructure = 1u << 10u,
        ShaderBindingTable = 1u << 11u
    };

    enum class ResourceState : u32
    {
        Unknown = 0,
        Common = 1u << 0u,
        CopySource = 1u << 1u,
        CopyDestination = 1u << 2u,
        ShaderResourceGraphics = 1u << 3u,
        ShaderResourceCompute = 1u << 4u,
        UnorderedAccess = 1u << 5u,
        RenderTarget = 1u << 6u,
        DepthWrite = 1u << 7u,
        DepthRead = 1u << 8u,
        VertexBuffer = 1u << 9u,
        IndexBuffer = 1u << 10u,
        ConstantBuffer = 1u << 11u,
        IndirectArgument = 1u << 12u,
        AccelerationStructureRead = 1u << 13u,
        AccelerationStructureWrite = 1u << 14u,
        Present = 1u << 15u,
        ResolveSource = 1u << 16u,
        ResolveDestination = 1u << 17u,
        ShadingRate = 1u << 18u
    };

    template <typename Enum> [[nodiscard]] constexpr Enum CombineFlags(const Enum left, const Enum right) noexcept
    {
        return static_cast<Enum>(static_cast<u32>(left) | static_cast<u32>(right));
    }
    [[nodiscard]] constexpr TextureUsage operator|(const TextureUsage left, const TextureUsage right) noexcept
    {
        return CombineFlags(left, right);
    }
    [[nodiscard]] constexpr BufferUsage operator|(const BufferUsage left, const BufferUsage right) noexcept
    {
        return CombineFlags(left, right);
    }
    [[nodiscard]] constexpr ResourceState operator|(const ResourceState left, const ResourceState right) noexcept
    {
        return CombineFlags(left, right);
    }

    struct Extent3D
    {
        u32 width = 1;
        u32 height = 1;
        u32 depth = 1;
    };
    struct SubresourceRange
    {
        u16 firstMip = 0;
        u16 mipCount = 0xffffu;
        u16 firstSlice = 0;
        u16 sliceCount = 0xffffu;
    };

    struct DeviceParams
    {
        u32 adapterIndex = 0;
        struct ResidencyPolicy
        {
            /// Begin local-memory pressure recovery above this percentage of the operating-system budget.
            u8 pressureThresholdPercent = 95;
            /// Stop selecting allocations after estimated usage reaches this lower hysteresis threshold.
            u8 recoveryThresholdPercent = 85;
            /// Upper bound on native allocations evicted by one maintenance call.
            u16 maximumEvictionsPerMaintenance = 256;
            bool enabled = true;
        } residencyPolicy;
        bool editor = false;
        bool enableValidation = false;
        bool preferHighPerformanceAdapter = true;
    };

    struct Capabilities
    {
        struct PlacedResourceClass
        {
            PlacedHeapCategory heapCategory = PlacedHeapCategory::None;
            u64 compatibilityClass = 0;
            bool deferredBinding = false;
            // Largest caller-requested heap alignment that the native heap
            // creation path guarantees. Smaller power-of-two alignments are
            // satisfied by the same native guarantee.
            u64 maximumHeapAlignment = 0;

            [[nodiscard]] constexpr bool IsSupported() const noexcept
            {
                return deferredBinding && heapCategory != PlacedHeapCategory::None && compatibilityClass != 0 && maximumHeapAlignment != 0 &&
                       (maximumHeapAlignment & (maximumHeapAlignment - 1)) == 0;
            }
        };

        struct PlacedResourceProfile
        {
            PlacedResourceClass buffers;
            PlacedResourceClass textures;
            PlacedAliasDiscardLowering aliasDiscard = PlacedAliasDiscardLowering::Unsupported;
            bool sameQueueGraphics = false;
            bool sameQueueCompute = false;
            bool sameQueueCopy = false;
            bool graphicsComputeHandoff = false;
            bool copyQueueHandoff = false;

            [[nodiscard]] constexpr bool IsSupported() const noexcept
            {
                return buffers.IsSupported() || textures.IsSupported();
            }
        };

        BackendKind backend = BackendKind::Unknown;
        DeviceVendor vendor = DeviceVendor::Unknown;
        u32 vendorId = 0;
        u32 deviceId = 0;
        u64 dedicatedVideoMemory = 0;
        u64 uploadBufferAlignment = 1;
        u64 constantBufferAlignment = 1;
        u32 maximumTextureDimension2D = 0;
        u32 maximumTextureDimension3D = 0;
        u32 maximumTextureArrayLayers = 0;
        u32 maximumBindlessResources = 0;
        u32 maximumBindlessSamplers = 0;
        u32 maximumPushConstantBytes = 0;
        bool bindlessResources = false;
        bool bindlessSamplers = false;
        bool descriptorIndexing = false;
        bool asyncCompute = false;
        bool copyQueue = false;
        PlacedResourceProfile placedResources;
        bool rayTracing = false;
        bool rayTracingPipeline = false;
        bool meshShaders = false;
        bool variableRateShading = false;
        VariableRateShadingCapabilities variableRateShadingDetails;
        bool occlusionQueries = false;
        bool pipelineStatisticsQueries = false;
        bool timestampQueries = false;
        bool timestampCalibration = false;
        bool gpuMarkers = false;
        bool memoryBudgetQueries = false;
        bool explicitResidency = false;
        char adapterName[128]{};
    };

    struct MemoryBudgetSnapshot
    {
        u64 budget = 0;
        u64 currentUsage = 0;
        u64 availableForReservation = 0;
        u64 currentReservation = 0;
        MemorySegment segment = MemorySegment::Local;

        [[nodiscard]] constexpr u64 Available() const noexcept
        {
            return currentUsage < budget ? budget - currentUsage : 0;
        }
        [[nodiscard]] constexpr bool IsOverBudget() const noexcept
        {
            return currentUsage > budget;
        }
    };

    struct ResidencyFenceSet
    {
        u64 graphics = 0;
        u64 compute = 0;
        u64 copy = 0;
        // Set only by the joined RHI submission snapshot for queues which have
        // never submitted since device initialization. Zero-valued fences alone
        // remain incomplete coverage. This is evidence of no work, not a fence.
        u8 neverSubmittedQueues = 0;

        [[nodiscard]] constexpr bool Covers(const QueueType queue) const noexcept
        {
            const u64 fence = queue == QueueType::Graphics ? graphics : queue == QueueType::Compute ? compute : queue == QueueType::Copy ? copy : 0;
            return (queue == QueueType::Graphics || queue == QueueType::Compute || queue == QueueType::Copy) &&
                   (fence != 0 || (neverSubmittedQueues & (1u << static_cast<u32>(queue))) != 0);
        }

        void Include(GpuFence fence) noexcept;
    };

    struct ResidencyStats
    {
        u64 budgetQueries = 0;
        u64 priorityChanges = 0;
        u64 makeResidentCalls = 0;
        u64 evictCalls = 0;
        u64 objectsMadeResident = 0;
        u64 objectsEvicted = 0;
        u64 rejectedEvictions = 0;
        u64 residencyFailures = 0;
        u64 automaticWorkingSetChecks = 0;
        u64 automaticMakeResidentCalls = 0;
        u64 automaticObjectsMadeResident = 0;
        u64 automaticWorkingSetFailures = 0;
        u64 policyMaintenanceCalls = 0;
        u64 policyPressureEvents = 0;
        u64 policyObjectsEvicted = 0;
        u64 policyBytesEvicted = 0;
        u64 policyPinnedObjects = 0;
        u64 policyInFlightSkips = 0;
        u64 policyNoCandidateEvents = 0;
        u64 policyFailures = 0;
        u64 trackedAllocations = 0;
        u64 trackedResidentBytes = 0;
        u64 trackedEvictedBytes = 0;
        u64 lastObservedBudget = 0;
        u64 lastObservedUsage = 0;
    };

    struct TextureDesc
    {
        Extent3D extent;
        TextureDimension dimension = TextureDimension::Texture2D;
        Format format = Format::Unknown;
        u16 mipCount = 1;
        u16 arraySize = 1;
        u8 sampleCount = 1;
        TextureUsage usage = TextureUsage::ShaderResource;
        ResourceState initialState = ResourceState::Common;
        bool virtualResource = false;
        // Restore initialState when a command list closes. False requires explicit entry-state tracking and caller-owned exit states.
        bool keepInitialState = true;
    };

    struct BufferDesc
    {
        u64 size = 0;
        u32 structureStride = 0;
        Format format = Format::Unknown;
        BufferUsage usage = BufferUsage::None;
        ResourceState initialState = ResourceState::Common;
        MemoryType memoryType = MemoryType::DeviceLocal;
        bool virtualResource = false;
        // Explicitly tracked buffers currently require DeviceLocal memory and Common creation state.
        bool keepInitialState = true;
    };

    struct BufferInitData
    {
        const void* data = nullptr;
        u64 size = 0;
    };
    struct TextureSubresourceData
    {
        const void* data = nullptr;
        u64 size = 0;
        u64 rowPitch = 0;
        u64 depthPitch = 0;
        u16 mipLevel = 0;
        u16 arraySlice = 0;
    };
    struct TextureInitData
    {
        const TextureSubresourceData* subresources = nullptr;
        u32 subresourceCount = 0;
    };
    struct TextureSubresource
    {
        u16 mipLevel = 0;
        u16 arraySlice = 0;
    };
    struct CommandListEntryState
    {
        ResourceRef resource;
        ResourceState state = ResourceState::Unknown;
        // One texture mip/slice; buffers use zero/zero and track the entire buffer.
        TextureSubresource subresource;
    };
    struct TextureCopyRegion
    {
        TextureSubresource source;
        TextureSubresource destination;
        u32 sourceX = 0;
        u32 sourceY = 0;
        u32 sourceZ = 0;
        u32 destinationX = 0;
        u32 destinationY = 0;
        u32 destinationZ = 0;
        // An all-zero extent selects the complete source mip. Partially zero extents are invalid.
        Extent3D extent{0, 0, 0};
    };
    struct TextureResolveRegion
    {
        TextureSubresource source;
        TextureSubresource destination;
    };
    struct MemoryRequirements
    {
        u64 size = 0;
        u64 alignment = 0;
        u64 compatibilityClass = 0;
        MemoryType memoryType = MemoryType::DeviceLocal;
        PlacedHeapCategory heapCategory = PlacedHeapCategory::None;
    };
    struct HeapDesc
    {
        u64 size = 0;
        u64 alignment = 0;
        u64 compatibilityClass = 0;
        MemoryType memoryType = MemoryType::DeviceLocal;
        PlacedHeapCategory heapCategory = PlacedHeapCategory::None;
    };

    struct PlacementRecord
    {
        HeapRef heap;
        u64 offset = 0;
        u64 size = 0;
        u64 alignment = 0;
        u64 compatibilityClass = 0;
        u64 generation = 0;
        MemoryType memoryType = MemoryType::DeviceLocal;
        PlacedHeapCategory heapCategory = PlacedHeapCategory::None;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return heap.IsValid() && size != 0 && alignment != 0 && compatibilityClass != 0 && generation != 0 &&
                   heapCategory != PlacedHeapCategory::None;
        }
    };

    enum class FilterMode : u8
    {
        Nearest,
        Linear
    };
    enum class SamplerAddressMode : u8
    {
        Clamp,
        Wrap,
        Mirror,
        Border
    };
    enum class ComparisonFunction : u8
    {
        Never,
        Less,
        Equal,
        LessEqual,
        Greater,
        NotEqual,
        GreaterEqual,
        Always
    };
    struct SamplerStateDesc
    {
        FilterMode minification = FilterMode::Linear;
        FilterMode magnification = FilterMode::Linear;
        FilterMode mip = FilterMode::Linear;
        SamplerAddressMode addressU = SamplerAddressMode::Wrap;
        SamplerAddressMode addressV = SamplerAddressMode::Wrap;
        SamplerAddressMode addressW = SamplerAddressMode::Wrap;
        ComparisonFunction comparison = ComparisonFunction::Never;
        f32 mipLodBias = 0.0f;
        f32 minimumLod = 0.0f;
        f32 maximumLod = 1000.0f;
        f32 maximumAnisotropy = 1.0f;
        f32 borderColor[4]{};
    };

    struct ShaderDesc
    {
        ShaderStage stage = ShaderStage::Vertex;
        const void* bytecode = nullptr;
        u64 bytecodeSize = 0;
        const char* entryPoint = nullptr;
    };

    enum class VertexInputRate : u8
    {
        PerVertex,
        PerInstance
    };
    struct VertexBindingDesc
    {
        u8 binding = 0;
        u16 stride = 0;
        VertexInputRate inputRate = VertexInputRate::PerVertex;
        u16 instanceStepRate = 1;
    };
    struct VertexAttributeDesc
    {
        u8 location = 0;
        u8 binding = 0;
        u16 offset = 0;
        Format format = Format::Unknown;
        const char* semanticName = nullptr;
        u32 semanticIndex = 0;
    };
    struct VertexLayoutDesc
    {
        const VertexBindingDesc* bindings = nullptr;
        u32 bindingCount = 0;
        const VertexAttributeDesc* attributes = nullptr;
        u32 attributeCount = 0;
    };

    enum class BindingType : u8
    {
        ConstantBuffer,
        TextureShaderResource,
        TextureUnorderedAccess,
        TypedBufferShaderResource,
        TypedBufferUnorderedAccess,
        StructuredBufferShaderResource,
        StructuredBufferUnorderedAccess,
        ByteAddressBufferShaderResource,
        ByteAddressBufferUnorderedAccess,
        Sampler,
        AccelerationStructure,
        PushConstants
    };
    struct BindingLayoutEntry
    {
        u32 slot = 0;
        u32 arrayCount = 1;
        BindingType type = BindingType::TextureShaderResource;
    };
    struct BindingLayoutDesc
    {
        const BindingLayoutEntry* entries = nullptr;
        u32 entryCount = 0;
        u32 registerSpace = 0;
        ShaderStageMask visibility = 0;
    };

    enum class PipelineKind : u8
    {
        Graphics,
        Compute,
        RayTracing
    };
    enum class PrimitiveTopology : u8
    {
        PointList,
        LineList,
        LineStrip,
        TriangleList,
        TriangleStrip,
        PatchList
    };
    enum class RasterFillMode : u8
    {
        Solid,
        Wireframe
    };
    enum class RasterCullMode : u8
    {
        None,
        Front,
        Back
    };
    enum class BlendFactor : u8
    {
        Zero,
        One,
        SourceColor,
        OneMinusSourceColor,
        DestinationColor,
        OneMinusDestinationColor,
        SourceAlpha,
        OneMinusSourceAlpha,
        DestinationAlpha,
        OneMinusDestinationAlpha,
        ConstantColor,
        OneMinusConstantColor,
        SourceAlphaSaturate,
        SourceOneColor,
        OneMinusSourceOneColor,
        SourceOneAlpha,
        OneMinusSourceOneAlpha
    };
    enum class BlendOperation : u8
    {
        Add,
        Subtract,
        ReverseSubtract,
        Minimum,
        Maximum
    };
    enum class StencilOperation : u8
    {
        Keep,
        Zero,
        Replace,
        IncrementClamp,
        DecrementClamp,
        Invert,
        IncrementWrap,
        DecrementWrap
    };

    struct RasterizerStateDesc
    {
        RasterFillMode fill = RasterFillMode::Solid;
        RasterCullMode cull = RasterCullMode::Back;
        bool frontCounterClockwise = true;
        bool depthClipEnable = true;
        bool scissorEnable = true;
        bool multisampleEnable = false;
        bool antialiasedLineEnable = false;
        bool conservativeRasterization = false;
        i32 depthBias = 0;
        f32 depthBiasClamp = 0.0f;
        f32 slopeScaledDepthBias = 0.0f;
    };

    struct StencilFaceStateDesc
    {
        StencilOperation fail = StencilOperation::Keep;
        StencilOperation depthFail = StencilOperation::Keep;
        StencilOperation pass = StencilOperation::Keep;
        ComparisonFunction comparison = ComparisonFunction::Always;
    };

    struct DepthStencilStateDesc
    {
        bool depthTestEnable = false;
        bool depthWriteEnable = false;
        ComparisonFunction depthComparison = ComparisonFunction::LessEqual;
        bool stencilEnable = false;
        u8 stencilReadMask = 0xff;
        u8 stencilWriteMask = 0xff;
        bool dynamicStencilReference = true;
        u8 stencilReference = 0;
        StencilFaceStateDesc front;
        StencilFaceStateDesc back;
    };

    struct BlendAttachmentStateDesc
    {
        bool blendEnable = false;
        BlendFactor sourceColor = BlendFactor::One;
        BlendFactor destinationColor = BlendFactor::Zero;
        BlendOperation colorOperation = BlendOperation::Add;
        BlendFactor sourceAlpha = BlendFactor::One;
        BlendFactor destinationAlpha = BlendFactor::Zero;
        BlendOperation alphaOperation = BlendOperation::Add;
        u8 colorWriteMask = 0x0f;
    };

    struct BlendStateDesc
    {
        BlendAttachmentStateDesc attachments[MaximumColorAttachments]{};
        bool alphaToCoverageEnable = false;
    };

    struct PipelineAttachmentSignature
    {
        Format colorFormats[MaximumColorAttachments]{};
        u32 colorCount = 0;
        Format depthStencilFormat = Format::Unknown;
        u8 sampleCount = 1;
        u8 sampleQuality = 0;
    };

    struct GraphicsPipelineDesc
    {
        ShaderRef vertexShader;
        ShaderRef hullShader;
        ShaderRef domainShader;
        ShaderRef geometryShader;
        ShaderRef pixelShader;
        VertexLayoutRef vertexLayout;
        const BindingLayoutRef* bindingLayouts = nullptr;
        u32 bindingLayoutCount = 0;
        const DescriptorDomainRef* descriptorDomains = nullptr;
        u32 descriptorDomainCount = 0;
        PrimitiveTopology topology = PrimitiveTopology::TriangleList;
        u32 patchControlPoints = 0;
        RasterizerStateDesc rasterizer;
        DepthStencilStateDesc depthStencil;
        BlendStateDesc blend;
        PipelineAttachmentSignature attachments;
    };

    struct ComputePipelineDesc
    {
        ShaderRef computeShader;
        const BindingLayoutRef* bindingLayouts = nullptr;
        u32 bindingLayoutCount = 0;
        const DescriptorDomainRef* descriptorDomains = nullptr;
        u32 descriptorDomainCount = 0;
    };

    struct RayTracingShaderDesc
    {
        const char* exportName = nullptr;
        ShaderRef shader;
        BindingLayoutRef localBindingLayout;
    };

    struct RayTracingHitGroupDesc
    {
        const char* exportName = nullptr;
        ShaderRef closestHitShader;
        ShaderRef anyHitShader;
        ShaderRef intersectionShader;
        BindingLayoutRef localBindingLayout;
        bool proceduralPrimitive = false;
    };

    struct RayTracingPipelineDesc
    {
        const RayTracingShaderDesc* shaders = nullptr;
        u32 shaderCount = 0;
        const RayTracingHitGroupDesc* hitGroups = nullptr;
        u32 hitGroupCount = 0;
        const BindingLayoutRef* globalBindingLayouts = nullptr;
        u32 globalBindingLayoutCount = 0;
        const DescriptorDomainRef* descriptorDomains = nullptr;
        u32 descriptorDomainCount = 0;
        u32 maximumPayloadBytes = 0;
        u32 maximumAttributeBytes = 8;
        u32 maximumRecursionDepth = 1;
    };

    enum class IndexFormat : u8
    {
        UInt16,
        UInt32
    };

    enum class AccelerationStructureKind : u8
    {
        BottomLevel,
        TopLevel
    };
    enum class RayTracingGeometryType : u8
    {
        Triangles,
        AxisAlignedBoundingBoxes
    };
    enum class RayTracingGeometryFlags : u8
    {
        None = 0,
        Opaque = 1u << 0u,
        NoDuplicateAnyHitInvocation = 1u << 1u
    };
    enum class AccelerationStructureBuildFlags : u8
    {
        None = 0,
        AllowUpdate = 1u << 0u,
        AllowCompaction = 1u << 1u,
        PreferFastTrace = 1u << 2u,
        PreferFastBuild = 1u << 3u,
        MinimizeMemory = 1u << 4u
    };
    [[nodiscard]] constexpr RayTracingGeometryFlags operator|(const RayTracingGeometryFlags left, const RayTracingGeometryFlags right) noexcept
    {
        return CombineFlags(left, right);
    }
    [[nodiscard]] constexpr AccelerationStructureBuildFlags operator|(const AccelerationStructureBuildFlags left,
                                                                      const AccelerationStructureBuildFlags right) noexcept
    {
        return CombineFlags(left, right);
    }

    struct RayTracingTriangleGeometryDesc
    {
        BufferRef vertexBuffer;
        u64 vertexOffset = 0;
        u32 vertexCount = 0;
        u32 vertexStride = 0;
        Format positionFormat = Format::R32G32B32Float;
        BufferRef indexBuffer;
        u64 indexOffset = 0;
        u32 indexCount = 0;
        IndexFormat indexFormat = IndexFormat::UInt32;
        BufferRef transformBuffer;
        u64 transformOffset = 0;
    };
    struct RayTracingAabbGeometryDesc
    {
        BufferRef buffer;
        u64 offset = 0;
        u32 count = 0;
        u32 stride = 24;
    };
    struct RayTracingGeometryDesc
    {
        RayTracingGeometryType type = RayTracingGeometryType::Triangles;
        RayTracingGeometryFlags flags = RayTracingGeometryFlags::None;
        RayTracingTriangleGeometryDesc triangles;
        RayTracingAabbGeometryDesc axisAlignedBoundingBoxes;
    };
    struct AccelerationStructureDesc
    {
        AccelerationStructureKind kind = AccelerationStructureKind::BottomLevel;
        const RayTracingGeometryDesc* geometries = nullptr;
        u32 geometryCount = 0;
        u32 maximumInstanceCount = 0;
        AccelerationStructureBuildFlags buildFlags = AccelerationStructureBuildFlags::PreferFastTrace;
    };
    enum class AccelerationStructureBuildMode : u8
    {
        Build,
        Update
    };
    enum class AccelerationStructureCopyMode : u8
    {
        Clone,
        Compact
    };
    enum class RayTracingInstanceFlags : u8
    {
        None = 0,
        TriangleCullDisable = 1u << 0u,
        TriangleFrontCounterClockwise = 1u << 1u,
        ForceOpaque = 1u << 2u,
        ForceNonOpaque = 1u << 3u
    };
    struct RayTracingInstanceDesc
    {
        /// Row-major 3x4 object-to-world transform.
        f32 transform[12]{1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
        AccelerationStructureRef bottomLevel;
        u32 instanceId = 0;
        u32 instanceMask = 0xffu;
        u32 hitGroupOffset = 0;
        RayTracingInstanceFlags flags = RayTracingInstanceFlags::None;
    };
    struct RayTracingGpuInstanceDesc
    {
        /// Row-major 3x4 object-to-world transform.
        f32 transform[12]{1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
        /// Low 24 bits contain the instance id; high 8 bits contain the visibility mask.
        u32 instanceIdAndMask = 0xff000000u;
        /// Low 24 bits contain the hit-group offset; high 8 bits contain RayTracingInstanceFlags.
        u32 hitGroupOffsetAndFlags = 0;
        u64 bottomLevelDeviceAddress = 0;
    };
    static_assert(sizeof(RayTracingGpuInstanceDesc) == 64);

    struct ShaderTableRecord
    {
        const char* exportName = nullptr;
        const void* localData = nullptr;
        u32 localDataSize = 0;
    };
    struct ShaderTableDesc
    {
        PipelineRef pipeline;
        ShaderTableRecord rayGeneration;
        const ShaderTableRecord* missRecords = nullptr;
        u32 missRecordCount = 0;
        const ShaderTableRecord* hitGroupRecords = nullptr;
        u32 hitGroupRecordCount = 0;
        const ShaderTableRecord* callableRecords = nullptr;
        u32 callableRecordCount = 0;
    };
    struct DispatchRaysArguments
    {
        u32 width = 1;
        u32 height = 1;
        u32 depth = 1;
    };

    struct ViewportDesc
    {
        f32 x = 0.0f;
        f32 y = 0.0f;
        f32 width = 0.0f;
        f32 height = 0.0f;
        f32 minimumDepth = 0.0f;
        f32 maximumDepth = 1.0f;
    };

    struct Rect
    {
        i32 x = 0;
        i32 y = 0;
        i32 width = 0;
        i32 height = 0;
    };

    struct ColorValue
    {
        f32 red = 0.0f;
        f32 green = 0.0f;
        f32 blue = 0.0f;
        f32 alpha = 0.0f;
    };

    struct RenderTargetAttachment
    {
        TextureRef texture;
        Format format = Format::Unknown;
        u16 mipLevel = 0;
        u16 arraySlice = 0;
        bool readOnly = false;
    };

    struct RenderTargetSetup
    {
        RenderTargetAttachment colorTargets[MaximumColorAttachments]{};
        u32 colorTargetCount = 0;
        RenderTargetAttachment depthStencilTarget;
    };

    struct VariableRateShadingState
    {
        ShadingRate rate = ShadingRate::Rate1x1;
        ShadingRateCombiner primitiveCombiner = ShadingRateCombiner::Passthrough;
        ShadingRateCombiner imageCombiner = ShadingRateCombiner::Passthrough;
        TextureRef image;
        SubresourceRange imageSubresources;
        bool enabled = false;
    };

    struct VertexBufferBinding
    {
        BufferRef buffer;
        u64 offset = 0;
        u8 binding = 0;
    };

    struct IndexBufferBinding
    {
        BufferRef buffer;
        u64 offset = 0;
        IndexFormat format = IndexFormat::UInt16;
    };

    struct DrawArguments
    {
        u32 vertexCount = 0;
        u32 instanceCount = 1;
        u32 firstVertex = 0;
        u32 firstInstance = 0;
    };

    struct DrawIndexedArguments
    {
        u32 indexCount = 0;
        u32 instanceCount = 1;
        u32 firstIndex = 0;
        i32 baseVertex = 0;
        u32 firstInstance = 0;
    };

    // These structures are the portable byte layouts consumed by indirect draw and dispatch commands.
    struct IndirectDrawArguments
    {
        u32 vertexCount = 0;
        u32 instanceCount = 1;
        u32 firstVertex = 0;
        u32 firstInstance = 0;
    };
    struct IndirectDrawIndexedArguments
    {
        u32 indexCount = 0;
        u32 instanceCount = 1;
        u32 firstIndex = 0;
        i32 baseVertex = 0;
        u32 firstInstance = 0;
    };
    struct IndirectDispatchArguments
    {
        u32 groupCountX = 1;
        u32 groupCountY = 1;
        u32 groupCountZ = 1;
    };
    struct TextureViewDesc
    {
        Format format = Format::Unknown;
        SubresourceRange subresources;
    };
    struct BufferViewDesc
    {
        Format format = Format::Unknown;
        u64 offset = 0;
        u64 size = 0;
        u32 structureStride = 0;
    };

    enum class DescriptorDomainKind : u8
    {
        Resources,
        Samplers
    };

    struct GpuFence;

    struct DescriptorDomainDesc
    {
        DescriptorDomainKind kind = DescriptorDomainKind::Resources;
        u32 capacity = 0;
        u32 firstShaderSlot = 0;
        ShaderStageMask visibility = 0;
    };

    // CPU-safe descriptor identity. Index is the value stored in GPU-visible records; generation is retained on
    // the CPU and rejects stale release or rewrite attempts after the slot has been recycled.
    struct DescriptorHandle
    {
        u32 index = InvalidReferenceIndex;
        u32 generation = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return index != InvalidReferenceIndex && generation != 0;
        }
        [[nodiscard]] constexpr u32 GpuIndex() const noexcept
        {
            return index;
        }
        [[nodiscard]] constexpr explicit operator bool() const noexcept
        {
            return IsValid();
        }
        [[nodiscard]] friend constexpr bool operator==(const DescriptorHandle&, const DescriptorHandle&) noexcept = default;
    };

    struct DescriptorRetirement
    {
        u64 graphicsFence = 0;
        u64 computeFence = 0;
        u64 copyFence = 0;

        void Include(GpuFence fence) noexcept;
    };

    struct DescriptorDomainStats
    {
        u32 capacity = 0;
        u32 allocated = 0;
        u32 populated = 0;
        u32 pendingRetirement = 0;
        u32 exhaustedSlots = 0;
        u32 free = 0;
        u32 peakAllocated = 0;
        u64 completedRetirements = 0;
        u64 staleHandleOperations = 0;
        u64 rejectedWrites = 0;
        u64 allocationFailures = 0;
    };

    inline constexpr u32 MaximumQueryPoolEntries = 65'536;
    inline constexpr u32 MaximumResidencyBatchSize = 1'024;

    struct QueryPoolDesc
    {
        QueryType type = QueryType::Timestamp;
        u32 capacity = 0;
    };

    struct TimestampCalibration
    {
        u64 gpuTimestamp = 0;
        u64 cpuTimestamp = 0;
        u64 gpuFrequency = 0;
        u64 cpuFrequency = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return gpuFrequency != 0 && cpuFrequency != 0;
        }
    };

    struct ResourceLifetimeStats
    {
        u32 liveResources = 0;
        u32 pendingRetirements = 0;
        u32 peakPendingRetirements = 0;
        u32 destroyingResources = 0;
        u64 totalReferences = 0;
        u64 completedRetirements = 0;
        u64 staleReferenceOperations = 0;
        u64 retirementQueueRecoveries = 0;
        u64 retirementBucketOverflows = 0;
        bool reclamationJobActive = false;
    };

    enum class PresentationSurfaceKind : u8
    {
        None,
        Win32,
        Vulkan
    };
    struct PresentationSurface
    {
        PresentationSurfaceKind kind = PresentationSurfaceKind::None;
        void* nativeWindow = nullptr;
        void* nativeDisplay = nullptr;
    };

    enum class PresentMode : u8
    {
        Immediate,
        Mailbox,
        Fifo
    };
    enum class ColorSpace : u8
    {
        Srgb,
        Hdr10,
        ScRgb
    };

    struct Chromaticity
    {
        f32 x = 0.0f;
        f32 y = 0.0f;
    };

    struct Hdr10Metadata
    {
        Chromaticity redPrimary{0.708f, 0.292f};
        Chromaticity greenPrimary{0.170f, 0.797f};
        Chromaticity bluePrimary{0.131f, 0.046f};
        Chromaticity whitePoint{0.3127f, 0.3290f};
        f32 maximumMasteringLuminanceNits = 1'000.0f;
        f32 minimumMasteringLuminanceNits = 0.001f;
        u16 maximumContentLightLevelNits = 1'000;
        u16 maximumFrameAverageLightLevelNits = 400;
    };

    struct FrameLatencyPolicy
    {
        u8 maximumFramesInFlight = 2;
        u32 waitTimeoutMilliseconds = 5'000;
        bool enabled = true;
    };

    struct DisplayColorCapabilities
    {
        Chromaticity redPrimary;
        Chromaticity greenPrimary;
        Chromaticity bluePrimary;
        Chromaticity whitePoint;
        f32 minimumLuminanceNits = 0.0f;
        f32 maximumLuminanceNits = 0.0f;
        f32 maximumFullFrameLuminanceNits = 0.0f;
        u8 bitsPerColor = 0;
        bool hdr10Output = false;
        bool hdrActive = false;
    };

    struct SwapChainDesc
    {
        PresentationSurface surface;
        u32 width = 0;
        u32 height = 0;
        u8 bufferCount = 3;
        Format format = Format::B8G8R8A8UNorm;
        PresentMode presentMode = PresentMode::Fifo;
        ColorSpace colorSpace = ColorSpace::Srgb;
        Hdr10Metadata hdr10Metadata;
        FrameLatencyPolicy frameLatency;
        bool allowTearing = false;
    };
    inline constexpr u32 MaximumSwapChainBuffers = 8;

    enum class SwapChainState : u8
    {
        Available,
        Acquired,
        Reconfiguring,
        Failed
    };

    struct PresentParameters
    {
        PresentMode mode = PresentMode::Fifo;
        u8 synchronizationInterval = 1;
        bool allowTearing = false;
    };

    struct AcquiredBackBuffer
    {
        SwapChainRef swapChain;
        TextureRef texture;
        u64 serial = 0;
        u32 bufferIndex = 0;
        u32 width = 0;
        u32 height = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return swapChain.IsValid() && texture.IsValid() && serial != 0 && width != 0 && height != 0;
        }
        [[nodiscard]] constexpr explicit operator bool() const noexcept
        {
            return IsValid();
        }
    };

    struct SwapChainStats
    {
        SwapChainState state = SwapChainState::Failed;
        PresentParameters presentParameters;
        u64 acquisitions = 0;
        u64 abandonedAcquisitions = 0;
        u64 presentedFrames = 0;
        u64 acquireWaits = 0;
        u64 acquireWaitNanoseconds = 0;
        u64 longestAcquireWaitNanoseconds = 0;
        u64 frameLatencyWaits = 0;
        u64 frameLatencyWaitNanoseconds = 0;
        u64 longestFrameLatencyWaitNanoseconds = 0;
        u64 frameLatencyTimeouts = 0;
        u64 presentationFailures = 0;
        u64 rejectedOperations = 0;
        u64 resizeCount = 0;
        u64 lastAcquisitionSerial = 0;
        u64 lastPresentationFence = 0;
        u32 currentBufferIndex = 0;
        u32 bufferCount = 0;
        u32 width = 0;
        u32 height = 0;
        ColorSpace colorSpace = ColorSpace::Srgb;
        FrameLatencyPolicy frameLatency;
        DisplayColorCapabilities displayColor;
        bool tearingSupported = false;
        bool tearingEnabled = false;
    };

    struct GpuFence
    {
        QueueType queue = QueueType::Graphics;
        u64 value = 0;
        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return value != 0;
        }
        [[nodiscard]] constexpr explicit operator bool() const noexcept
        {
            return IsValid();
        }
        [[nodiscard]] friend constexpr bool operator==(const GpuFence&, const GpuFence&) noexcept = default;
    };

    // Full fence evidence produced by a submission. `completion` retains the
    // legacy synchronization-point meaning; `residency` contains every queue
    // fence that can protect submitted resources.
    struct SubmissionReceipt
    {
        ResidencyFenceSet residency;
        GpuFence completion;
        // True once native command execution has been issued, even if the
        // backend subsequently loses the device while signaling its fence.
        // A false API result with this bit set must be recovered as submitted
        // work with unknown completion, never as an unsubmitted discard.
        bool workSubmitted = false;

        [[nodiscard]] constexpr bool WasSubmitted() const noexcept
        {
            return workSubmitted;
        }

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            if (!workSubmitted || !completion.IsValid())
                return false;
            if (completion.queue == QueueType::Graphics)
                return residency.graphics >= completion.value;
            if (completion.queue == QueueType::Compute)
                return residency.compute >= completion.value;
            if (completion.queue == QueueType::Copy)
                return residency.copy >= completion.value;
            return false;
        }
    };

    // A readback request copies one mip and one array slice into owned CPU-visible staging storage. The source
    // region remains in its native format; row and depth pitches describe backend padding and must be respected.
    struct TextureReadbackRegion
    {
        TextureSubresource source;
        u32 sourceX = 0;
        u32 sourceY = 0;
        u32 sourceZ = 0;
        // An all-zero extent selects the complete source mip. Partially zero extents are invalid.
        Extent3D extent{0, 0, 0};
    };
    enum class TextureReadbackState : u8
    {
        PendingSubmission,
        PendingGpu,
        Ready,
        Mapped,
        Failed
    };
    struct TextureReadbackInfo
    {
        Format format = Format::Unknown;
        Extent3D extent{};
        TextureReadbackState state = TextureReadbackState::Failed;
        GpuFence completion;
    };
    struct TextureReadbackMapping
    {
        const void* data = nullptr;
        u64 rowPitch = 0;
        u64 depthPitch = 0;
        u64 dataSize = 0;
        Format format = Format::Unknown;
        Extent3D extent{};

        [[nodiscard]] constexpr explicit operator bool() const noexcept
        {
            return data != nullptr;
        }
    };

    enum class FailureCode : u8
    {
        None,
        NotInitialized,
        AlreadyInitialized,
        InvalidArgument,
        InvalidReference,
        InvalidCommandList,
        NoBoundCommandList,
        Unsupported,
        CapacityExceeded,
        OutOfMemory,
        DeviceLost,
        BackendFailure,
        Busy,
        Timeout,
        IncompatibleBinding,
        MissingBinding,
        ResourceStateMismatch
    };

    struct Failure
    {
        FailureCode code = FailureCode::None;
        i64 backendCode = 0;
        char message[192]{};
    };

    struct BackendStatus
    {
        bool success = true;
        FailureCode code = FailureCode::None;
        i64 backendCode = 0;
        const char* message = nullptr;

        [[nodiscard]] static constexpr BackendStatus Success() noexcept
        {
            return {};
        }
        [[nodiscard]] static constexpr BackendStatus Failure(const FailureCode code, const i64 backendCode, const char* const message) noexcept
        {
            return {false, code, backendCode, message};
        }
        [[nodiscard]] constexpr explicit operator bool() const noexcept
        {
            return success;
        }
    };
} // namespace vanguard::rhi
