cbuffer FrameData : register(b0, space0)
{
    float4x4 viewProjection;
    float3 cameraPosition;
    uint candidateCount;
};

Texture2D<float4> materialTextures[] : register(t0, space1);
SamplerState materialSamplers[] : register(s0, space1);
StructuredBuffer<float4> candidateBounds : register(t0, space2);
RWStructuredBuffer<uint> visibleCandidates : register(u0, space2);

[shader("compute")]
[numthreads(8, 4, 1)]
void BuildCandidates(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x >= candidateCount) return;
    const uint textureIndex = dispatchThreadId.x & 1u;
    const float4 value = materialTextures[textureIndex].SampleLevel(materialSamplers[textureIndex], float2(0.5), 0.0);
    visibleCandidates[dispatchThreadId.x] = candidateBounds[dispatchThreadId.x].x + value.x + cameraPosition.x > 0.0;
}
