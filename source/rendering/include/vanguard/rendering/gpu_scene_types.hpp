#pragma once

#include <vanguard/rendering/render_view.hpp>

#include <cstddef>
#include <type_traits>

namespace vanguard::rendering
{
    inline constexpr u32 GpuSceneLayoutVersion = 2;
    inline constexpr u32 InvalidGpuSceneIndex = 0xffffffffu;
    inline constexpr u32 InvalidGpuDescriptorIndex = 0xffffffffu;

    template<typename Tag>
    struct GpuTableHandle
    {
        u32 index = InvalidGpuSceneIndex;
        u32 generation = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return index != InvalidGpuSceneIndex && generation != 0;
        }
        [[nodiscard]] friend constexpr bool operator==(const GpuTableHandle&, const GpuTableHandle&) noexcept = default;
    };

    struct GpuInstanceTag;
    struct GpuRenderableTag;
    struct GpuPrimitiveTag;
    struct GpuGeometryTag;
    struct GpuMaterialTag;
    struct GpuMaterialSetTag;
    struct GpuLightTag;
    struct GpuDecalTag;

    using GpuInstanceHandle = GpuTableHandle<GpuInstanceTag>;
    using GpuRenderableHandle = GpuTableHandle<GpuRenderableTag>;
    using GpuPrimitiveHandle = GpuTableHandle<GpuPrimitiveTag>;
    using GpuGeometryHandle = GpuTableHandle<GpuGeometryTag>;
    using GpuMaterialHandle = GpuTableHandle<GpuMaterialTag>;
    using GpuMaterialSetHandle = GpuTableHandle<GpuMaterialSetTag>;
    using GpuLightHandle = GpuTableHandle<GpuLightTag>;
    using GpuDecalHandle = GpuTableHandle<GpuDecalTag>;

    /// Candidate, visible, and final draw-instance lists contain this index only. Generation validation
    /// happens when a CPU mutation is admitted; a slot is not reused until its GPU retirement fence completes.
    using GpuInstanceIndex = u32;

    enum class GpuInstanceFlags : u32
    {
        None = 0,
        Active = 1u << 0u,
        CastsShadow = 1u << 1u,
        ReceivesDecals = 1u << 2u,
        HasMotion = 1u << 3u,
        ForceVisible = 1u << 4u,
        NegativeScale = 1u << 5u
    };

    enum class GpuRenderableFlags : u32
    {
        None = 0,
        Resident = 1u << 0u,
        Skinned = 1u << 1u,
        Deformable = 1u << 2u
    };

    enum class GpuPrimitiveFlags : u32
    {
        None = 0,
        Indexed = 1u << 0u,
        AlphaTested = 1u << 1u,
        TwoSided = 1u << 2u
    };

    enum class GpuGeometryFlags : u32
    {
        None = 0,
        Resident = 1u << 0u,
        Indexed = 1u << 1u
    };

    enum class GpuIndexFormat : u32
    {
        UInt16,
        UInt32
    };

    enum class GpuMaterialFlags : u32
    {
        None = 0,
        Resident = 1u << 0u
    };

    enum class GpuPhaseParticipationFlags : u32
    {
        None = 0,
        DepthWrite = 1u << 0u,
        Velocity = 1u << 1u
    };

    /// One stable slot per ordinary renderable. Culling reads the first four 16-byte lanes; complete
    /// transforms are fetched only by work that survives visibility and phase classification.
    struct alignas(16) GpuInstance
    {
        i32 worldCell[3]{};
        GpuInstanceFlags flags = GpuInstanceFlags::None;

        f32 localPosition[3]{};
        f32 boundsRadius = 0.0f;

        /// World-axis offset from the instance origin to the already transformed culling-sphere center.
        /// This is not a mesh-local point; boundsRadius already includes the instance's maximum scale.
        f32 boundsCenterOffset[3]{};
        u32 renderable = InvalidGpuSceneIndex;

        u32 materialSet = InvalidGpuSceneIndex;
        u32 generation = 0;
        u32 visibilityMask = ~0u;
        u32 motion = InvalidGpuSceneIndex;

        f32 rotation[4]{0.0f, 0.0f, 0.0f, 1.0f};

        f32 scale[3]{1.0f, 1.0f, 1.0f};
        u32 userData = InvalidGpuSceneIndex;
    };

    /// Previous transform data exists only for instances that require motion history.
    struct alignas(16) GpuMotion
    {
        i32 previousWorldCell[3]{};
        u32 generation = 0;

        f32 previousLocalPosition[3]{};
        u32 reserved0 = 0;

        f32 previousRotation[4]{0.0f, 0.0f, 0.0f, 1.0f};

        f32 previousScale[3]{1.0f, 1.0f, 1.0f};
        u32 reserved1 = 0;
    };

    struct alignas(16) GpuRenderable
    {
        u32 firstLod = 0;
        u32 lodCount = 0;
        u32 phaseMaskLow = 0;
        u32 phaseMaskHigh = 0;

        u32 generation = 0;
        GpuRenderableFlags flags = GpuRenderableFlags::None;
        u32 reserved0 = 0;
        u32 reserved1 = 0;
    };

    struct alignas(16) GpuLod
    {
        u32 firstPrimitive = 0;
        u32 primitiveCount = 0;
        f32 minimumScreenCoverage = 0.0f;
        f32 maximumNormalizedError = 0.0f;
    };

    struct alignas(16) GpuPrimitive
    {
        u32 geometry = InvalidGpuSceneIndex;
        u32 material = InvalidGpuSceneIndex;
        u32 firstPhaseParticipation = 0;
        u32 phaseParticipationCount = 0;

        u32 stableSubmesh = 0;
        GpuPrimitiveFlags flags = GpuPrimitiveFlags::None;
        u32 reserved0 = 0;
        u32 reserved1 = 0;
    };

    /// A primitive may participate in several phases without duplicating its geometry or material.
    /// pipelineBucket is a renderer-registered stable ordinal used directly by GPU batching.
    struct alignas(16) GpuPhaseParticipation
    {
        u32 phase = InvalidRenderPhaseIndex;
        u32 pipelineBucket = InvalidGpuSceneIndex;
        GpuPhaseParticipationFlags flags = GpuPhaseParticipationFlags::None;
        i32 sortBias = 0;
    };

    /// A geometry entry describes ranges in large GPU arenas. Vertex and index bytes remain in those
    /// arenas; this table is the indirection used by culling, batching, indirect generation, and shaders.
    struct alignas(16) GpuGeometryRange
    {
        u32 firstVertexStream = 0;
        u32 vertexStreamCount = 0;
        u32 indexArena = InvalidGpuSceneIndex;
        u32 indexByteOffset = 0;

        u32 indexCount = 0;
        i32 baseVertex = 0;
        u32 vertexCount = 0;
        GpuIndexFormat indexFormat = GpuIndexFormat::UInt16;

        u32 positionDecode = InvalidGpuSceneIndex;
        GpuGeometryFlags flags = GpuGeometryFlags::None;
        u32 generation = 0;
        u32 reserved = 0;
    };

    /// One entry per packed vertex binding. formatLayout identifies shader-reflected decoding metadata;
    /// the GPU address is resolved from arena and byteOffset without assuming a fixed mesh layout.
    struct alignas(16) GpuVertexStream
    {
        u32 arena = InvalidGpuSceneIndex;
        u32 byteOffset = 0;
        u32 stride = 0;
        u32 formatLayout = InvalidGpuSceneIndex;
    };

    struct alignas(16) GpuPositionDecode
    {
        f32 scale[3]{1.0f, 1.0f, 1.0f};
        u32 reserved0 = 0;

        f32 bias[3]{};
        u32 reserved1 = 0;
    };

    /// Runtime material resolution writes parameter and bindless-resource table ranges here. Cooked
    /// materials remain independent of descriptor indices, spaces, registers, and draw submission style.
    struct alignas(16) GpuMaterial
    {
        u32 parameterByteOffset = 0;
        u32 parameterByteSize = 0;
        u32 firstResource = 0;
        u32 resourceCount = 0;

        u32 materialInterface = InvalidGpuSceneIndex;
        u32 generation = 0;
        GpuMaterialFlags flags = GpuMaterialFlags::None;
        u32 reserved = 0;
    };

    struct alignas(16) GpuMaterialResource
    {
        u32 descriptor = InvalidGpuDescriptorIndex;
        u32 samplerDescriptor = InvalidGpuDescriptorIndex;
        u32 type = 0;
        u32 flags = 0;
    };

    /// Invalid on an instance means that every primitive uses its default material. A valid set is a
    /// compact primitive-local override span; InvalidGpuSceneIndex entries retain individual defaults.
    struct alignas(16) GpuMaterialSet
    {
        u32 firstMaterial = 0;
        u32 materialCount = 0;
        u32 generation = 0;
        u32 flags = 0;
    };

    /// Element of the compact primitive-local material override array referenced by GpuMaterialSet.
    struct GpuMaterialIndex
    {
        u32 material = InvalidGpuSceneIndex;
    };

    struct alignas(16) GpuLight
    {
        i32 worldCell[3]{};
        u32 type = 0;

        f32 localPosition[3]{};
        f32 range = 0.0f;

        f32 direction[3]{0.0f, 0.0f, 1.0f};
        f32 innerConeCosine = 1.0f;

        f32 color[3]{1.0f, 1.0f, 1.0f};
        f32 intensity = 0.0f;

        f32 outerConeCosine = 1.0f;
        f32 sourceRadius = 0.0f;
        f32 sourceLength = 0.0f;
        u32 flags = 0;

        u32 shadowData = InvalidGpuSceneIndex;
        u32 generation = 0;
        u32 visibilityMask = ~0u;
        u32 reserved = 0;
    };

    struct alignas(16) GpuDecal
    {
        i32 worldCell[3]{};
        u32 flags = 0;

        f32 localPosition[3]{};
        f32 fade = 1.0f;

        f32 rotation[4]{0.0f, 0.0f, 0.0f, 1.0f};

        f32 halfExtent[3]{};
        u32 material = InvalidGpuSceneIndex;

        u32 visibilityMask = ~0u;
        u32 generation = 0;
        i32 sortBias = 0;
        u32 reserved = 0;
    };

    struct alignas(16) GpuVisibilityPlane
    {
        f32 normal[3]{};
        f32 distance = 0.0f;
    };

    /// Frame-scoped shader image of RenderView. Matrices are arrays of four vectors so CPU/HLSL
    /// packing is explicit and independent of compiler matrix-major defaults.
    struct alignas(16) GpuView
    {
        f32 worldToView[16]{};
        f32 viewToClip[16]{};
        f32 worldToClip[16]{};
        f32 previousWorldToClip[16]{};

        GpuVisibilityPlane frustum[8];

        i32 worldCell[3]{};
        u32 frustumPlaneCount = 0;
        f32 localPosition[3]{};
        u32 flags = 0;
        i32 previousWorldCell[3]{};
        u32 purpose = 0;
        f32 previousLocalPosition[3]{};
        u32 viewIndex = InvalidGpuSceneIndex;

        u32 rect[4]{};
        u32 layerMaskLow = ~0u;
        u32 layerMaskHigh = ~0u;
        u32 visibilityMask = ~0u;
        u32 phaseMaskLow = 0;
        u32 phaseMaskHigh = 0;
        u32 viewGeneration = 0;
        u32 familyIndex = InvalidGpuSceneIndex;
        u32 familyGeneration = 0;
        u32 temporalIdentityLow = 0;
        u32 temporalIdentityHigh = 0;
        u32 frameSerialLow = 0;
        u32 frameSerialHigh = 0;
        f32 nearPlane = 0.0f;
        f32 farPlane = 0.0f;
        f32 lodBias = 0.0f;
        u32 reserved = 0;
        f32 jitter[2]{};
        f32 previousJitter[2]{};
    };

    /// Encodes an already validated CPU view into its exact shader ABI image.
    [[nodiscard]] bool BuildGpuView(const RenderView& source, GpuView& output) noexcept;

    static_assert(sizeof(GpuTableHandle<GpuInstanceTag>) == 8);
    static_assert(sizeof(GpuInstance) == 96 && offsetof(GpuInstance, rotation) == 64);
    static_assert(sizeof(GpuMotion) == 64);
    static_assert(sizeof(GpuRenderable) == 32);
    static_assert(sizeof(GpuLod) == 16);
    static_assert(sizeof(GpuPrimitive) == 32);
    static_assert(sizeof(GpuPhaseParticipation) == 16);
    static_assert(sizeof(GpuGeometryRange) == 48);
    static_assert(sizeof(GpuVertexStream) == 16);
    static_assert(sizeof(GpuPositionDecode) == 32);
    static_assert(sizeof(GpuMaterial) == 32);
    static_assert(sizeof(GpuMaterialResource) == 16);
    static_assert(sizeof(GpuMaterialSet) == 16);
    static_assert(sizeof(GpuMaterialIndex) == 4);
    static_assert(sizeof(GpuLight) == 96);
    static_assert(sizeof(GpuDecal) == 80);
    static_assert(sizeof(GpuVisibilityPlane) == 16);
    static_assert(sizeof(GpuView) == 544);
    static_assert(offsetof(GpuView, frustum) == 256);
    static_assert(offsetof(GpuView, worldCell) == 384);
    static_assert(offsetof(GpuView, rect) == 448);

    static_assert(std::is_standard_layout_v<GpuInstance> && std::is_trivially_copyable_v<GpuInstance>);
    static_assert(std::is_standard_layout_v<GpuGeometryRange> && std::is_trivially_copyable_v<GpuGeometryRange>);
    static_assert(std::is_standard_layout_v<GpuMaterial> && std::is_trivially_copyable_v<GpuMaterial>);
    static_assert(std::is_standard_layout_v<GpuView> && std::is_trivially_copyable_v<GpuView>);
} // namespace vanguard::rendering
