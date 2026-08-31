#include <vanguard/rendering/gpu_scene_types.hpp>

namespace vanguard::rendering
{
    namespace
    {
        void CopyViewFloats(f32* const destination, const f32* const source, const u32 count) noexcept
        {
            for (u32 index = 0; index < count; ++index)
                destination[index] = source[index];
        }

        void CopyCells(i32* const destination, const i32* const source) noexcept
        {
            for (u32 index = 0; index < 3; ++index)
                destination[index] = source[index];
        }
    } // namespace

    bool BuildGpuView(const RenderView& source, GpuView& output) noexcept
    {
        if (!source.id.IsValid() || !source.family.IsValid() || !source.rect.IsValid() || source.frustum.planeCount > MaximumVisibilityFrustumPlanes ||
            source.phases.Empty())
            return false;

        output = {};
        CopyViewFloats(output.worldToView, source.matrices.worldToView, 16);
        CopyViewFloats(output.viewToClip, source.matrices.viewToClip, 16);
        CopyViewFloats(output.worldToClip, source.matrices.worldToClip, 16);
        CopyViewFloats(output.previousWorldToClip, source.matrices.previousWorldToClip, 16);
        for (u32 index = 0; index < source.frustum.planeCount; ++index)
        {
            CopyViewFloats(output.frustum[index].normal, source.frustum.planes[index].normal, 3);
            output.frustum[index].distance = source.frustum.planes[index].distance;
        }

        CopyCells(output.worldCell, source.origin.worldCell);
        CopyViewFloats(output.localPosition, source.origin.localPosition, 3);
        CopyCells(output.previousWorldCell, source.previousOrigin.worldCell);
        CopyViewFloats(output.previousLocalPosition, source.previousOrigin.localPosition, 3);
        output.frustumPlaneCount = source.frustum.planeCount;
        output.flags = static_cast<u32>(source.flags);
        output.purpose = static_cast<u32>(source.purpose);
        output.viewIndex = source.id.index;
        output.rect[0] = source.rect.x;
        output.rect[1] = source.rect.y;
        output.rect[2] = source.rect.width;
        output.rect[3] = source.rect.height;
        output.layerMaskLow = static_cast<u32>(source.layerMask);
        output.layerMaskHigh = static_cast<u32>(source.layerMask >> 32u);
        output.visibilityMask = source.visibilityMask;
        output.phaseMaskLow = static_cast<u32>(source.phases.Bits());
        output.phaseMaskHigh = static_cast<u32>(source.phases.Bits() >> 32u);
        output.viewGeneration = source.id.generation;
        output.familyIndex = source.family.index;
        output.familyGeneration = source.family.generation;
        output.temporalIdentityLow = static_cast<u32>(source.temporalIdentity);
        output.temporalIdentityHigh = static_cast<u32>(source.temporalIdentity >> 32u);
        output.frameSerialLow = static_cast<u32>(source.frameSerial);
        output.frameSerialHigh = static_cast<u32>(source.frameSerial >> 32u);
        output.nearPlane = source.nearPlane;
        output.farPlane = source.farPlane;
        output.lodBias = source.lodBias;
        output.jitter[0] = source.jitter[0];
        output.jitter[1] = source.jitter[1];
        output.previousJitter[0] = source.previousJitter[0];
        output.previousJitter[1] = source.previousJitter[1];
        return true;
    }
} // namespace vanguard::rendering
