#pragma once

#include <vanguard/system/types.hpp>
#include <cmath>

namespace vanguard::rendering
{
    enum class RenderLightKind : u8
    {
        Directional = 0,
        Point = 1,
        Spot = 2
    };

    // Canonical light contract shared by components, proxies and GPU publication:
    // RGB is non-negative linear Rec.709; intensity is lux for directional lights
    // and candela for point/spot lights. Range is a world-space cutoff in metres,
    // independent of transform scale. Direction is local +Z (emitted rays), so
    // surface-to-light direction for a directional light is its negative.
    // Spot angles are cone half-angles in radians. Equal angles mean a hard edge.
    // Importers may convert authored lumen/EV values before creating this data.
    [[nodiscard]] inline bool ValidRenderLight(const RenderLightKind kind, const f32* color, const f32 intensity,
                                               const f32 range, const f32 innerCone, const f32 outerCone) noexcept
    {
        if (kind > RenderLightKind::Spot || color == nullptr || !std::isfinite(intensity) || intensity < 0.0f ||
            !std::isfinite(range) || range < 0.0f || (kind != RenderLightKind::Directional && range <= 0.0f) ||
            !std::isfinite(innerCone) || !std::isfinite(outerCone) || innerCone < 0.0f || outerCone < innerCone ||
            (kind == RenderLightKind::Spot && (outerCone <= 0.0f || outerCone >= 1.5707963268f)))
            return false;
        for (u32 axis = 0; axis < 3; ++axis)
            if (!std::isfinite(color[axis]) || color[axis] < 0.0f)
                return false;
        return true;
    }
}
