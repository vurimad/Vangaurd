#ifndef VANGUARD_GPU_SCENE_TYPES_HLSLI
#define VANGUARD_GPU_SCENE_TYPES_HLSLI

static const uint VG_GPU_SCENE_LAYOUT_VERSION = 2;
static const uint VG_GPU_SCENE_INVALID_INDEX = 0xffffffffu;
static const uint VG_GPU_SCENE_MAXIMUM_FRUSTUM_PLANES = 8u;
static const uint VG_GPU_SCENE_MAXIMUM_PAGES_PER_TABLE = 64u;

static const uint VG_GPU_SCENE_TABLE_INSTANCE = 0u;
static const uint VG_GPU_SCENE_TABLE_MOTION = 1u;
static const uint VG_GPU_SCENE_TABLE_RENDERABLE = 2u;
static const uint VG_GPU_SCENE_TABLE_LOD = 3u;
static const uint VG_GPU_SCENE_TABLE_PRIMITIVE = 4u;
static const uint VG_GPU_SCENE_TABLE_PHASE_PARTICIPATION = 5u;
static const uint VG_GPU_SCENE_TABLE_GEOMETRY_RANGE = 6u;
static const uint VG_GPU_SCENE_TABLE_VERTEX_STREAM = 7u;
static const uint VG_GPU_SCENE_TABLE_POSITION_DECODE = 8u;
static const uint VG_GPU_SCENE_TABLE_MATERIAL = 9u;
static const uint VG_GPU_SCENE_TABLE_MATERIAL_RESOURCE = 10u;
static const uint VG_GPU_SCENE_TABLE_MATERIAL_SET = 11u;
static const uint VG_GPU_SCENE_TABLE_MATERIAL_INDEX = 12u;
static const uint VG_GPU_SCENE_TABLE_LIGHT = 13u;
static const uint VG_GPU_SCENE_TABLE_DECAL = 14u;

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
    int3 worldCell; uint flags;
    float3 localPosition; float boundsRadius;
    float3 boundsCenterOffset; uint renderable;
    uint materialSet; uint generation; uint visibilityMask; uint motion;
    float4 rotation;
    float3 scale; uint userData;
};

struct GpuMotion
{
    int3 previousWorldCell; uint generation;
    float3 previousLocalPosition; uint reserved0;
    float4 previousRotation;
    float3 previousScale; uint reserved1;
};

struct GpuRenderable
{
    uint firstLod; uint lodCount; uint phaseMaskLow; uint phaseMaskHigh;
    uint generation; uint flags; uint reserved0; uint reserved1;
};

struct GpuLod
{
    uint firstPrimitive; uint primitiveCount; float minimumScreenCoverage; float maximumNormalizedError;
};

struct GpuPrimitive
{
    uint geometry; uint material; uint firstPhaseParticipation; uint phaseParticipationCount;
    uint stableSubmesh; uint flags; uint reserved0; uint reserved1;
};

struct GpuPhaseParticipation
{
    uint phase; uint pipelineBucket; uint flags; int sortBias;
};

struct GpuGeometryRange
{
    uint firstVertexStream; uint vertexStreamCount; uint indexArena; uint indexByteOffset;
    uint indexCount; int baseVertex; uint vertexCount; uint indexFormat;
    uint positionDecode; uint flags; uint generation; uint reserved;
};

struct GpuVertexStream
{
    uint arena; uint byteOffset; uint stride; uint formatLayout;
};

struct GpuPositionDecode
{
    float3 scale; uint reserved0;
    float3 bias; uint reserved1;
};

struct GpuMaterial
{
    uint parameterByteOffset; uint parameterByteSize; uint firstResource; uint resourceCount;
    uint materialInterface; uint generation; uint flags; uint reserved;
};

struct GpuMaterialResource
{
    uint descriptor; uint samplerDescriptor; uint type; uint flags;
};

struct GpuMaterialSet
{
    uint firstMaterial; uint materialCount; uint generation; uint flags;
};

struct GpuMaterialIndex
{
    uint material;
};

struct GpuLight
{
    int3 worldCell; uint type;
    float3 localPosition; float range;
    float3 direction; float innerConeCosine;
    float3 color; float intensity;
    float outerConeCosine; float sourceRadius; float sourceLength; uint flags;
    uint shadowData; uint generation; uint visibilityMask; uint reserved;
};

struct GpuDecal
{
    int3 worldCell; uint flags;
    float3 localPosition; float fade;
    float4 rotation;
    float3 halfExtent; uint material;
    uint visibilityMask; uint generation; int sortBias; uint reserved;
};

struct GpuVisibilityPlane
{
    float3 normal; float distance;
};

struct GpuView
{
    float4 worldToView[4];
    float4 viewToClip[4];
    float4 worldToClip[4];
    float4 previousWorldToClip[4];
    GpuVisibilityPlane frustum[VG_GPU_SCENE_MAXIMUM_FRUSTUM_PLANES];
    int3 worldCell; uint frustumPlaneCount;
    float3 localPosition; uint flags;
    int3 previousWorldCell; uint purpose;
    float3 previousLocalPosition; uint viewIndex;
    uint4 rect;
    uint layerMaskLow; uint layerMaskHigh; uint visibilityMask; uint phaseMaskLow;
    uint phaseMaskHigh; uint viewGeneration; uint familyIndex; uint familyGeneration;
    uint temporalIdentityLow; uint temporalIdentityHigh; uint frameSerialLow; uint frameSerialHigh;
    float nearPlane; float farPlane; float lodBias; uint reserved;
    float2 jitter; float2 previousJitter;
};

#endif
