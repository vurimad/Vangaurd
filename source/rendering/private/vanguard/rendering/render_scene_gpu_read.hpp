#pragma once

#include <vanguard/rendering/render_scene.hpp>

namespace vanguard::rendering
{
    /// Non-owning access to one proxy while its scene is sealed for GPU publication.
    /// Every pointer addresses authoritative RenderScene storage; no proxy payload is copied.
    struct RenderSceneGpuReadView
    {
        const RenderProxyTransform* transform = nullptr;
        const RenderProxyBounds* bounds = nullptr;
        const RenderProxyVisibilityFlags* visibility = nullptr;
        const u64* layerMask = nullptr;
        const u32* visibilityMask = nullptr;
        RenderProxyPayloadKind payloadKind = RenderProxyPayloadKind::None;

        const RenderLightKind* lightKind = nullptr;
        const f32* lightColor = nullptr;
        const f32* lightIntensity = nullptr;
        const f32* lightRange = nullptr;
        const f32* lightInnerConeRadians = nullptr;
        const f32* lightOuterConeRadians = nullptr;
        const bool* lightCastsShadow = nullptr;

        const f32* decalExtents = nullptr;
        const f32* decalFadeDistance = nullptr;
        const u32* decalSortKey = nullptr;

        [[nodiscard]] bool HasCommonData() const noexcept
        {
            return transform != nullptr && bounds != nullptr && visibility != nullptr && layerMask != nullptr && visibilityMask != nullptr;
        }
    };
} // namespace vanguard::rendering
