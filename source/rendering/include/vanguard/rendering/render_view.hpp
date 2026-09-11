#pragma once

#include <vanguard/rendering/render_phase.hpp>

namespace vanguard::rendering
{
    inline constexpr u32 MaximumRenderViews = 1024;
    inline constexpr u32 MaximumRenderViewFamilies = 256;
    inline constexpr u32 MaximumRenderViewsPerFamily = 32;
    inline constexpr u32 MaximumRenderViewNameBytes = 64;
    inline constexpr u32 MaximumVisibilityFrustumPlanes = 8;

    struct RenderViewId
    {
        u32 index = ~u32{0};
        u32 generation = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return index < MaximumRenderViews && generation != 0;
        }
        [[nodiscard]] friend constexpr bool operator==(const RenderViewId&, const RenderViewId&) noexcept = default;
    };

    struct RenderViewFamilyId
    {
        u32 index = ~u32{0};
        u32 generation = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return index < MaximumRenderViewFamilies && generation != 0;
        }
        [[nodiscard]] friend constexpr bool operator==(const RenderViewFamilyId&, const RenderViewFamilyId&) noexcept = default;
    };

    struct VisibilityPlane
    {
        f32 normal[3]{};
        f32 distance = 0.0f;
    };

    struct VisibilityFrustum
    {
        VisibilityPlane planes[MaximumVisibilityFrustumPlanes];
        u32 planeCount = 0;
    };

    enum class RenderViewPurpose : u8
    {
        Main,
        Shadow,
        Reflection,
        Editor,
        Selection,
        Probe,
        Capture,
        Diagnostic
    };

    enum class RenderViewFlags : u32
    {
        None = 0,
        Primary = 1u << 0u,
        Orthographic = 1u << 1u,
        ReverseDepth = 1u << 2u,
        TemporalHistory = 1u << 3u,
        OcclusionCulling = 1u << 4u,
        Jittered = 1u << 5u,
        InfiniteFarPlane = 1u << 6u
    };

    [[nodiscard]] constexpr RenderViewFlags operator|(const RenderViewFlags left, const RenderViewFlags right) noexcept
    {
        return static_cast<RenderViewFlags>(static_cast<u32>(left) | static_cast<u32>(right));
    }

    [[nodiscard]] constexpr RenderViewFlags operator&(const RenderViewFlags left, const RenderViewFlags right) noexcept
    {
        return static_cast<RenderViewFlags>(static_cast<u32>(left) & static_cast<u32>(right));
    }

    struct RenderViewOrigin
    {
        i32 worldCell[3]{};
        f32 localPosition[3]{};
    };

    struct RenderViewRect
    {
        u32 x = 0;
        u32 y = 0;
        u32 width = 0;
        u32 height = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return width != 0 && height != 0;
        }
    };

    /// Matrices use the renderer's shader convention and are stored explicitly to avoid deriving or
    /// transposing them independently in every view consumer.
    struct RenderViewMatrices
    {
        f32 worldToView[16]{};
        f32 viewToClip[16]{};
        f32 worldToClip[16]{};
        f32 previousWorldToClip[16]{};
    };

    struct RenderView
    {
        RenderViewId id;
        RenderViewFamilyId family;
        RenderViewPurpose purpose = RenderViewPurpose::Main;
        RenderViewFlags flags = RenderViewFlags::None;
        RenderPhaseSet phases;
        RenderViewOrigin origin;
        RenderViewOrigin previousOrigin;
        RenderViewRect rect;
        VisibilityFrustum frustum;
        RenderViewMatrices matrices;
        u64 layerMask = ~0ull;
        u32 visibilityMask = ~0u;
        u64 temporalIdentity = 0;
        u64 frameSerial = 0;
        f32 nearPlane = 0.1f;
        /// Zero when InfiniteFarPlane is set; otherwise strictly greater than nearPlane.
        f32 farPlane = 1000.0f;
        /// Positive values select coarser LODs; negative values select finer LODs.
        f32 lodBias = 0.0f;
        f32 jitter[2]{};
        f32 previousJitter[2]{};
        char name[MaximumRenderViewNameBytes]{};
    };

    /// A family is a frame-scoped, non-owning span. Compatible views may later share candidate sets,
    /// history resources, and one immutable scene publication without being tied to a viewport.
    struct RenderViewFamily
    {
        RenderViewFamilyId id;
        const RenderView* views = nullptr;
        u32 viewCount = 0;
        u64 frameSerial = 0;
        u64 sceneIdentity = 0;
        u64 sceneVersion = 0;
    };

    enum class RenderViewFailureCode : u8
    {
        None,
        InvalidIdentity,
        InvalidFamily,
        InvalidPurpose,
        InvalidFlags,
        EmptyPhaseSet,
        UnsealedPhaseRegistry,
        UnknownPhase,
        InvalidFrustum,
        InvalidMatrices,
        InvalidOrigin,
        InvalidExtent,
        InvalidDepthRange,
        InvalidTemporalState,
        InvalidName,
        CapacityExceeded,
        DuplicateView,
        ForeignView
    };

    struct RenderViewFailure
    {
        RenderViewFailureCode code = RenderViewFailureCode::None;
        RenderViewId view;
        RenderViewFamilyId family;
        const char* message = nullptr;
    };

    [[nodiscard]] bool ValidateRenderView(const RenderView& view, const RenderPhaseRegistry& phases, RenderViewFailure* failure = nullptr) noexcept;
    [[nodiscard]] bool ValidateRenderViewFamily(const RenderViewFamily& family, const RenderPhaseRegistry& phases,
                                                RenderViewFailure* failure = nullptr) noexcept;
} // namespace vanguard::rendering
