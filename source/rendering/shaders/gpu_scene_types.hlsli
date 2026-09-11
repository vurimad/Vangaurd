#ifndef VANGUARD_GPU_SCENE_TYPES_HLSLI
#define VANGUARD_GPU_SCENE_TYPES_HLSLI

static const uint VG_GPU_SCENE_LAYOUT_VERSION = 9;
static const uint VG_GPU_INSTANCE_NEGATIVE_SCALE = 1u << 5u;
static const uint VG_GPU_GEOMETRY_NORMAL_PACKED_UNORM10 = 1u << 2u;
static const uint VG_GPU_GEOMETRY_TANGENT_PACKED_UNORM10 = 1u << 3u;
static const uint VG_GPU_SCENE_TABLE_GEOMETRY_SHELL = 20u;
static const uint VG_GPU_SCENE_TABLE_GEOMETRY_BIN = 21u;
static const uint VG_GPU_GEOMETRY_CATALOG_ACTIVE = 1u;

struct GpuGeometryShell
{
    uint generation;
    uint flags;
    uint phase;
    uint binCount;
    uint vertexArena;
    uint vertexArenaGeneration;
    uint indexArena;
    uint indexArenaGeneration;
};

struct GpuGeometryBin
{
    uint generation;
    uint flags;
    uint shell;
    uint shellGeneration;
    uint geometry;
    uint geometryGeneration;
    uint shellOrdinal;
    uint reserved;
    uint firstIndex;
    uint indexCount;
    uint firstVertex;
    uint vertexCount;
};
static const uint VG_GPU_SCENE_INVALID_INDEX = 0xffffffffu;
static const uint VG_GPU_SCENE_MAXIMUM_FRUSTUM_PLANES = 8u;
static const uint VG_GPU_SCENE_MAXIMUM_PAGES_PER_TABLE = 64u;

static const uint VG_GPU_SCENE_TABLE_INSTANCE = 0u;
static const uint VG_GPU_SCENE_TABLE_MOTION = 1u;
static const uint VG_GPU_SCENE_TABLE_RENDERABLE = 2u;
static const uint VG_GPU_SCENE_TABLE_RENDERABLE_RESIDENCY = 3u;
static const uint VG_GPU_SCENE_TABLE_LOD = 4u;
static const uint VG_GPU_SCENE_TABLE_PRIMITIVE = 5u;
static const uint VG_GPU_SCENE_TABLE_PRIMITIVE_PLACEMENT = 6u;
static const uint VG_GPU_SCENE_TABLE_PHASE_PARTICIPATION = 7u;
static const uint VG_GPU_SCENE_TABLE_PHASE_PLACEMENT = 8u;
static const uint VG_GPU_SCENE_TABLE_GEOMETRY_RANGE = 9u;
static const uint VG_GPU_SCENE_TABLE_VERTEX_STREAM = 10u;
static const uint VG_GPU_SCENE_TABLE_POSITION_DECODE = 11u;
static const uint VG_GPU_SCENE_TABLE_MATERIAL = 12u;
static const uint VG_GPU_SCENE_TABLE_MATERIAL_RESOURCE = 13u;
static const uint VG_GPU_SCENE_TABLE_MATERIAL_SET = 14u;
static const uint VG_GPU_SCENE_TABLE_MATERIAL_INDEX = 15u;
static const uint VG_GPU_SCENE_TABLE_LIGHT = 16u;
static const uint VG_GPU_SCENE_TABLE_DECAL = 17u;
static const uint VG_GPU_SCENE_TABLE_TEXTURE_RESIDENCY = 18u;
static const uint VG_GPU_SCENE_TABLE_MATERIAL_PARAMETER_WORD = 19u;

static const uint VG_GPU_MATERIAL_RESOURCE_TEXTURE = 0u;
static const uint VG_GPU_MATERIAL_RESOURCE_BUFFER = 1u;
static const uint VG_GPU_MATERIAL_RESOURCE_SAMPLER = 2u;
static const uint VG_GPU_MATERIAL_RESOURCE_ACCELERATION_STRUCTURE = 3u;

struct GpuScenePageDirectoryEntry
{
    uint shaderResourceDescriptor;
    uint unorderedAccessDescriptor;
    uint baseElement;
    uint elementCount;
};

struct GpuSceneTableDirectory
{
    uint firstPageEntry;
    uint pageCapacity;
    uint pageShift;
    uint pageMask;

    uint elementsPerPage;
    uint elementStride;
    uint reserved0;
    uint reserved1;
};

uint2 DecodeGpuSceneIndex(uint index, GpuSceneTableDirectory table)
{
    return uint2(index >> table.pageShift, index & table.pageMask);
}

struct GpuInstance
{
    int3 worldCell;
    uint flags;
    float3 localPosition;
    float boundsRadius;
    float3 boundsCenterOffset;
    uint renderable;
    uint materialSet;
    uint generation;
    uint visibilityMask;
    uint layerMaskLow;
    uint layerMaskHigh;
    uint motion;
    uint deformation;
    uint reserved;
    float4 rotation;
    float3 scale;
    uint reservedTransform;
};

struct GpuMotion
{
    int3 previousWorldCell;
    uint generation;
    float3 previousLocalPosition;
    uint reserved0;
    float4 previousRotation;
    float3 previousScale;
    uint reserved1;
};

struct GpuRenderable
{
    uint firstLod;
    uint lodCount;
    uint phaseMaskLow;
    uint phaseMaskHigh;
    uint generation;
    uint flags;
    uint reserved0;
    uint reserved1;
};

struct GpuRenderableResidency
{
    uint generation;
    uint placementRevision;
    uint residentLodMaskLow;
    uint residentLodMaskHigh;
    uint anchorLod;
    uint flags;
    uint reserved0;
    uint reserved1;
};

struct GpuLod
{
    uint firstPrimitive;
    uint primitiveCount;
    float minimumScreenCoverage;
    float maximumNormalizedError;
};

struct GpuPrimitive
{
    uint material;
    uint firstPhaseParticipation;
    uint phaseParticipationCount;
    uint sourceSubmesh;
    uint flags;
    uint reserved0;
    uint reserved1;
    uint reserved2;
};

struct GpuPrimitivePlacement
{
    uint geometry;
    uint geometryGeneration;
    uint placementRevision;
    uint flags;
};

struct GpuPhaseParticipation
{
    uint phase;
    uint flags;
    int sortBias;
    uint reserved;
};

struct GpuPhasePlacement
{
    uint normalShell;
    uint normalShellGeneration;
    uint normalBin;
    uint normalBinGeneration;
    uint mirroredShell;
    uint mirroredShellGeneration;
    uint mirroredBin;
    uint mirroredBinGeneration;
    uint placementRevision;
    uint flags;
    uint reserved0;
    uint reserved1;
};

struct GpuGeometryRange
{
    uint firstVertexStream;
    uint vertexStreamCount;
    uint vertexArenaSet;
    uint vertexArenaGeneration;
    uint indexArena;
    uint indexArenaGeneration;
    uint firstIndex;
    uint indexCount;
    int baseVertex;
    uint vertexCount;
    uint indexFormat;
    uint positionDecode;
    uint flags;
    uint generation;
    uint reserved0;
    uint reserved1;
};

struct GpuVertexStream
{
    uint binding;
    uint stride;
    uint formatLayout;
    uint reserved;
};

struct GpuPositionDecode
{
    float3 scale;
    uint reserved0;
    float3 bias;
    uint reserved1;
};

struct GpuMaterial
{
    uint parameterByteOffset;
    uint parameterByteSize;
    uint firstResource;
    uint resourceCount;
    uint materialLayout;
    uint generation;
    uint flags;
    uint reserved;
};

GpuMaterial LoadGpuMaterial(uint materialIndex, StructuredBuffer<GpuSceneTableDirectory> tableDirectory, StructuredBuffer<GpuScenePageDirectoryEntry> pageDirectory)
{
    GpuSceneTableDirectory table = tableDirectory[VG_GPU_SCENE_TABLE_MATERIAL];
    uint2 address = DecodeGpuSceneIndex(materialIndex, table);
    GpuScenePageDirectoryEntry page = pageDirectory[table.firstPageEntry + address.x];
    StructuredBuffer<GpuMaterial> materials = ResourceDescriptorHeap[NonUniformResourceIndex(page.shaderResourceDescriptor)];
    return materials[address.y];
}

uint LoadGpuMaterialParameterStorageWord(uint wordIndex, StructuredBuffer<GpuSceneTableDirectory> tableDirectory, StructuredBuffer<GpuScenePageDirectoryEntry> pageDirectory)
{
    GpuSceneTableDirectory table = tableDirectory[VG_GPU_SCENE_TABLE_MATERIAL_PARAMETER_WORD];
    uint2 address = DecodeGpuSceneIndex(wordIndex, table);
    GpuScenePageDirectoryEntry page = pageDirectory[table.firstPageEntry + address.x];
    StructuredBuffer<uint> words = ResourceDescriptorHeap[NonUniformResourceIndex(page.shaderResourceDescriptor)];
    return words[address.y];
}

uint LoadGpuMaterialParameterWord(GpuMaterial material, uint localByteOffset, StructuredBuffer<GpuSceneTableDirectory> tableDirectory, StructuredBuffer<GpuScenePageDirectoryEntry> pageDirectory)
{
    if (localByteOffset >= material.parameterByteSize)
        return 0u;
    uint globalByteOffset = material.parameterByteOffset + localByteOffset;
    uint byteInWord = globalByteOffset & 3u;
    uint low = LoadGpuMaterialParameterStorageWord(globalByteOffset >> 2u, tableDirectory, pageDirectory);
    if (byteInWord == 0u)
        return low;
    uint value = low >> (byteInWord * 8u);
    uint remainingBytes = material.parameterByteSize - localByteOffset;
    if (remainingBytes > 4u - byteInWord)
    {
        uint high = LoadGpuMaterialParameterStorageWord((globalByteOffset >> 2u) + 1u, tableDirectory, pageDirectory);
        value |= high << ((4u - byteInWord) * 8u);
    }
    return value;
}

struct GpuMaterialResource
{
    uint resource;
    uint samplerDescriptor;
    uint type;
    uint flags;
};

GpuMaterialResource LoadGpuMaterialResource(GpuMaterial material, uint localRoleSlot, StructuredBuffer<GpuSceneTableDirectory> tableDirectory, StructuredBuffer<GpuScenePageDirectoryEntry> pageDirectory)
{
    GpuMaterialResource invalidResource;
    invalidResource.resource = VG_GPU_SCENE_INVALID_INDEX;
    invalidResource.samplerDescriptor = VG_GPU_SCENE_INVALID_INDEX;
    invalidResource.type = VG_GPU_MATERIAL_RESOURCE_TEXTURE;
    invalidResource.flags = 0u;
    if (localRoleSlot >= material.resourceCount)
        return invalidResource;

    GpuSceneTableDirectory table = tableDirectory[VG_GPU_SCENE_TABLE_MATERIAL_RESOURCE];
    uint2 address = DecodeGpuSceneIndex(material.firstResource + localRoleSlot, table);
    GpuScenePageDirectoryEntry page = pageDirectory[table.firstPageEntry + address.x];
    StructuredBuffer<GpuMaterialResource> resources = ResourceDescriptorHeap[NonUniformResourceIndex(page.shaderResourceDescriptor)];
    return resources[address.y];
}

struct GpuTextureResidency
{
    uint descriptor;
    uint firstResidentMip;
    uint residentMipCount;
    uint generationAndFlags;
};

struct GpuMaterialSet
{
    uint firstMaterial;
    uint materialCount;
    uint generation;
    uint flags;
};

struct GpuMaterialIndex
{
    uint material;
};

static const uint VG_GPU_LIGHT_CASTS_SHADOW = 1u << 0u;
static const uint VG_GPU_LIGHT_ACTIVE = 1u << 1u;
static const uint VG_GPU_LIGHT_DIRECTIONAL = 0u;
static const uint VG_GPU_LIGHT_POINT = 1u;
static const uint VG_GPU_LIGHT_SPOT = 2u;

static const uint VG_MAXIMUM_DIRECTIONAL_LIGHTS_PER_VIEW = 8u;
struct GpuDirectionalLightSelection
{
    uint count;
    uint3 reserved;
    uint2 lights[VG_MAXIMUM_DIRECTIONAL_LIGHTS_PER_VIEW];
};

// Linear Rec.709 color; intensity is lux (directional), candela (point/spot).
// Direction follows emitted rays; range is world metres; cones are half angles.
struct GpuLight
{
    int3 worldCell;
    uint type;
    float3 localPosition;
    float range;
    float3 direction;
    float innerConeCosine;
    float3 color;
    float intensity;
    float outerConeCosine;
    float sourceRadius;
    float sourceLength;
    uint flags;
    uint shadowData;
    uint generation;
    uint visibilityMask;
    uint reserved;
};

// Call only for a light selected for this view (including CPU layer filtering).
// The expected generation comes from that selected light's GPU identity.
bool IsGpuLightActive(GpuLight light, uint expectedGeneration, uint viewVisibilityMask)
{
    return expectedGeneration != 0u && light.generation == expectedGeneration &&
        (light.flags & VG_GPU_LIGHT_ACTIVE) != 0u &&
        (light.visibilityMask & viewVisibilityMask) != 0u;
}

bool HasGpuLightShadowData(GpuLight light)
{
    return (light.flags & VG_GPU_LIGHT_CASTS_SHADOW) != 0u &&
        light.shadowData != VG_GPU_SCENE_INVALID_INDEX;
}

struct GpuDecal
{
    int3 worldCell;
    uint flags;
    float3 localPosition;
    float fade;
    float4 rotation;
    float3 halfExtent;
    uint material;
    uint visibilityMask;
    uint generation;
    int sortBias;
    uint reserved;
};

struct GpuVisibilityPlane
{
    float3 normal;
    float distance;
};

struct GpuView
{
    float4 worldToView[4];
    float4 viewToClip[4];
    float4 worldToClip[4];
    float4 previousWorldToClip[4];
    GpuVisibilityPlane frustum[VG_GPU_SCENE_MAXIMUM_FRUSTUM_PLANES];
    int3 worldCell;
    uint frustumPlaneCount;
    float3 localPosition;
    uint flags;
    int3 previousWorldCell;
    uint purpose;
    float3 previousLocalPosition;
    uint viewIndex;
    uint4 rect;
    uint layerMaskLow;
    uint layerMaskHigh;
    uint visibilityMask;
    uint phaseMaskLow;
    uint phaseMaskHigh;
    uint viewGeneration;
    uint familyIndex;
    uint familyGeneration;
    uint temporalIdentityLow;
    uint temporalIdentityHigh;
    uint frameSerialLow;
    uint frameSerialHigh;
    float nearPlane;
    float farPlane;
    float lodBias;
    uint reserved;
    float2 jitter;
    float2 previousJitter;
};

#endif
