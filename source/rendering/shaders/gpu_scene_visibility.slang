#include "gpu_scene_types.hlsli"

static const uint VG_GPU_VISIBILITY_THREADS = 128u;
static const uint VG_GPU_INSTANCE_ACTIVE = 1u << 0u;
static const uint VG_GPU_INSTANCE_FORCE_VISIBLE = 1u << 4u;
static const uint VG_GPU_RENDERABLE_RESIDENT = 1u << 0u;

struct GpuVisibilityWorkRange
{
    uint candidateOffset;
    uint candidateCount;
    uint viewIndex;
    uint resultIndex;
};

struct GpuVisibilityResultRange
{
    uint visibleOffset;
    uint visibleCapacity;
    uint counterIndex;
    uint reserved;
};

struct GpuVisibilityCounters
{
    uint visibleCount;
    uint culledCount;
    uint overflowCount;
    uint reserved;
};

struct GpuVisibilityConstants
{
    uint tableDirectoryDescriptor;
    uint pageDirectoryDescriptor;
    uint candidateDescriptor;
    uint workRangeDescriptor;

    uint viewDescriptor;
    uint resultRangeDescriptor;
    uint visibleDescriptor;
    uint counterDescriptor;

    uint workRangeCount;
    uint instanceTable;
    uint renderableTable;
    uint layoutVersion;

    float worldCellSize;
    uint reserved0;
    uint reserved1;
    uint reserved2;
};

[[push_constant]] ConstantBuffer<GpuVisibilityConstants> constants;

groupshared uint groupVisibility[VG_GPU_VISIBILITY_THREADS];
groupshared uint groupOutputBase;

GpuInstance LoadGpuInstance(uint index,
                            StructuredBuffer<GpuSceneTableDirectory> tableDirectory,
                            StructuredBuffer<GpuScenePageDirectoryEntry> pageDirectory)
{
    GpuSceneTableDirectory table = tableDirectory[constants.instanceTable];
    uint2 address = DecodeGpuSceneIndex(index, table);
    GpuScenePageDirectoryEntry page = pageDirectory[table.firstPageEntry + address.x];
    StructuredBuffer<GpuInstance> instances =
        ResourceDescriptorHeap[NonUniformResourceIndex(page.shaderResourceDescriptor)];
    return instances[address.y];
}

GpuRenderable LoadGpuRenderable(uint index,
                                StructuredBuffer<GpuSceneTableDirectory> tableDirectory,
                                StructuredBuffer<GpuScenePageDirectoryEntry> pageDirectory)
{
    GpuSceneTableDirectory table = tableDirectory[constants.renderableTable];
    uint2 address = DecodeGpuSceneIndex(index, table);
    GpuScenePageDirectoryEntry page = pageDirectory[table.firstPageEntry + address.x];
    StructuredBuffer<GpuRenderable> renderables =
        ResourceDescriptorHeap[NonUniformResourceIndex(page.shaderResourceDescriptor)];
    return renderables[address.y];
}

bool IntersectsRequestedPhases(GpuRenderable renderable, GpuView view)
{
    return ((renderable.phaseMaskLow & view.phaseMaskLow) |
            (renderable.phaseMaskHigh & view.phaseMaskHigh)) != 0u;
}

bool SphereIntersectsFrustum(float3 center, float radius, GpuView view)
{
    [loop]
    for (uint planeIndex = 0u; planeIndex < view.frustumPlaneCount; ++planeIndex)
    {
        GpuVisibilityPlane plane = view.frustum[planeIndex];
        if (dot(plane.normal, center) + plane.distance < -radius)
            return false;
    }
    return true;
}

bool IsCandidateVisible(uint instanceIndex, GpuView view,
                        StructuredBuffer<GpuSceneTableDirectory> tableDirectory,
                        StructuredBuffer<GpuScenePageDirectoryEntry> pageDirectory)
{
    if (instanceIndex == VG_GPU_SCENE_INVALID_INDEX)
        return false;

    GpuInstance instance = LoadGpuInstance(instanceIndex, tableDirectory, pageDirectory);
    if ((instance.flags & VG_GPU_INSTANCE_ACTIVE) == 0u || instance.generation == 0u ||
        instance.renderable == VG_GPU_SCENE_INVALID_INDEX ||
        (instance.visibilityMask & view.visibilityMask) == 0u ||
        ((instance.layerMaskLow & view.layerMaskLow) == 0u &&
         (instance.layerMaskHigh & view.layerMaskHigh) == 0u))
        return false;

    GpuRenderable renderable = LoadGpuRenderable(instance.renderable, tableDirectory, pageDirectory);
    if ((renderable.flags & VG_GPU_RENDERABLE_RESIDENT) == 0u || renderable.generation == 0u ||
        !IntersectsRequestedPhases(renderable, view))
        return false;

    if ((instance.flags & VG_GPU_INSTANCE_FORCE_VISIBLE) != 0u)
        return true;

    float3 relativeCenter = float3(instance.worldCell - view.worldCell) * constants.worldCellSize +
                            instance.localPosition - view.localPosition + instance.boundsCenterOffset;
    return SphereIntersectsFrustum(relativeCenter, instance.boundsRadius, view);
}

[shader("compute")]
[numthreads(VG_GPU_VISIBILITY_THREADS, 1, 1)]
void CullGpuSceneCandidates(uint3 groupId : SV_GroupID, uint groupThreadIndex : SV_GroupIndex)
{
    if (constants.layoutVersion != VG_GPU_SCENE_LAYOUT_VERSION || groupId.x >= constants.workRangeCount)
        return;

    StructuredBuffer<GpuSceneTableDirectory> tableDirectory =
        ResourceDescriptorHeap[constants.tableDirectoryDescriptor];
    StructuredBuffer<GpuScenePageDirectoryEntry> pageDirectory =
        ResourceDescriptorHeap[constants.pageDirectoryDescriptor];
    StructuredBuffer<uint> candidates = ResourceDescriptorHeap[constants.candidateDescriptor];
    StructuredBuffer<GpuVisibilityWorkRange> workRanges =
        ResourceDescriptorHeap[constants.workRangeDescriptor];
    StructuredBuffer<GpuView> views = ResourceDescriptorHeap[constants.viewDescriptor];
    StructuredBuffer<GpuVisibilityResultRange> resultRanges =
        ResourceDescriptorHeap[constants.resultRangeDescriptor];
    RWStructuredBuffer<uint> visibleIndices = ResourceDescriptorHeap[constants.visibleDescriptor];
    RWStructuredBuffer<GpuVisibilityCounters> counters = ResourceDescriptorHeap[constants.counterDescriptor];

    GpuVisibilityWorkRange work = workRanges[groupId.x];
    GpuVisibilityResultRange result = resultRanges[work.resultIndex];
    GpuView view = views[work.viewIndex];
    bool participates = groupThreadIndex < work.candidateCount;
    uint instanceIndex = participates ? candidates[work.candidateOffset + groupThreadIndex] : VG_GPU_SCENE_INVALID_INDEX;
    bool visible = participates && IsCandidateVisible(instanceIndex, view, tableDirectory, pageDirectory);
    groupVisibility[groupThreadIndex] = visible ? 1u : 0u;
    GroupMemoryBarrierWithGroupSync();

    [unroll]
    for (uint offset = 1u; offset < VG_GPU_VISIBILITY_THREADS; offset <<= 1u)
    {
        uint preceding = groupThreadIndex >= offset ? groupVisibility[groupThreadIndex - offset] : 0u;
        GroupMemoryBarrierWithGroupSync();
        groupVisibility[groupThreadIndex] += preceding;
        GroupMemoryBarrierWithGroupSync();
    }

    uint groupVisibleCount = groupVisibility[VG_GPU_VISIBILITY_THREADS - 1u];
    if (groupThreadIndex == 0u)
    {
        InterlockedAdd(counters[result.counterIndex].visibleCount, groupVisibleCount, groupOutputBase);
        InterlockedAdd(counters[result.counterIndex].culledCount, work.candidateCount - groupVisibleCount);
        uint writable = groupOutputBase < result.visibleCapacity
                            ? min(groupVisibleCount, result.visibleCapacity - groupOutputBase)
                            : 0u;
        if (writable != groupVisibleCount)
            InterlockedAdd(counters[result.counterIndex].overflowCount, groupVisibleCount - writable);
    }
    GroupMemoryBarrierWithGroupSync();

    if (visible)
    {
        uint destination = groupOutputBase + groupVisibility[groupThreadIndex] - 1u;
        if (destination < result.visibleCapacity)
            visibleIndices[result.visibleOffset + destination] = instanceIndex;
    }
}
