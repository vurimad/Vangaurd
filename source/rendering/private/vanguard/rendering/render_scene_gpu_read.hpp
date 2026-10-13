#pragma once

#include <vanguard/rendering/render_scene.hpp>

namespace vanguard::rendering
{
    /// Non-owning access to one proxy while its completed scene epoch remains under
    /// the renderer owner boundary. Every pointer addresses authoritative
    /// RenderScene storage; no proxy payload is copied.
    struct RenderSceneGpuReadView
    {
        const RenderProxyTransform* transform = nullptr;
        const RenderProxyBounds* bounds = nullptr;
        const RenderProxyVisibilityFlags* visibility = nullptr;
        const u64* layerMask = nullptr;
        const u32* visibilityMask = nullptr;
        const RenderProxySpatialMode* spatialMode = nullptr;
        const GpuInstanceIndex* gpuInstanceIndex = nullptr;
        // Borrowed from the exact completed scene epoch. Render-frame owner
        // completion fences the next scene mutation, matching the common-data
        // pointer lifetime above. Never Retain/Reset this binding on a worker.
        const MeshDrawableBinding* meshDrawable = nullptr;
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
            return transform != nullptr && bounds != nullptr && visibility != nullptr && layerMask != nullptr && visibilityMask != nullptr && spatialMode != nullptr;
        }
    };
} // namespace vanguard::rendering
