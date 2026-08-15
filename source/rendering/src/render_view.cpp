#include <vanguard/rendering/render_view.hpp>

#include <cmath>

namespace vanguard::rendering
{
    namespace
    {
        void ClearFailure(RenderViewFailure* const failure) noexcept
        {
            if (failure != nullptr) *failure = {};
        }

        [[nodiscard]] bool Fail(RenderViewFailure* const failure, const RenderViewFailureCode code,
                                const char* const message, const RenderViewId view = {},
                                const RenderViewFamilyId family = {}) noexcept
        {
            if (failure != nullptr) *failure = {code, view, family, message};
            return false;
        }

        [[nodiscard]] bool ValidPurpose(const RenderViewPurpose purpose) noexcept
        {
            return static_cast<u32>(purpose) <= static_cast<u32>(RenderViewPurpose::Diagnostic);
        }

        [[nodiscard]] bool Finite(const f32* const values, const u32 count) noexcept
        {
            for (u32 index = 0; index < count; ++index)
                if (!std::isfinite(values[index])) return false;
            return true;
        }

        [[nodiscard]] bool ValidFrustum(const VisibilityFrustum& frustum) noexcept
        {
            if (frustum.planeCount == 0 || frustum.planeCount > MaximumVisibilityFrustumPlanes) return false;
            for (u32 index = 0; index < frustum.planeCount; ++index)
            {
                const VisibilityPlane& plane = frustum.planes[index];
                if (!Finite(plane.normal, 3) || !std::isfinite(plane.distance)) return false;
                const f32 magnitudeSquared = plane.normal[0] * plane.normal[0] +
                                             plane.normal[1] * plane.normal[1] +
                                             plane.normal[2] * plane.normal[2];
                if (!std::isfinite(magnitudeSquared) || magnitudeSquared <= 0.0f) return false;
            }
            return true;
        }

        [[nodiscard]] bool ValidName(const char* const name) noexcept
        {
            if (name[0] == '\0') return false;
            for (u32 index = 0; index < MaximumRenderViewNameBytes; ++index)
                if (name[index] == '\0') return true;
            return false;
        }
    } // namespace

    bool ValidateRenderView(const RenderView& view, const RenderPhaseRegistry& phases,
                            RenderViewFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!view.id.IsValid())
            return Fail(failure, RenderViewFailureCode::InvalidIdentity,
                        "render view identity is invalid", view.id, view.family);
        if (!view.family.IsValid())
            return Fail(failure, RenderViewFailureCode::InvalidFamily,
                        "render view family identity is invalid", view.id, view.family);
        if (!ValidPurpose(view.purpose))
            return Fail(failure, RenderViewFailureCode::InvalidPurpose,
                        "render view purpose is invalid", view.id, view.family);
        constexpr u32 validFlagBits = static_cast<u32>(RenderViewFlags::Primary) |
                                      static_cast<u32>(RenderViewFlags::Orthographic) |
                                      static_cast<u32>(RenderViewFlags::ReverseDepth) |
                                      static_cast<u32>(RenderViewFlags::TemporalHistory) |
                                      static_cast<u32>(RenderViewFlags::OcclusionCulling) |
                                      static_cast<u32>(RenderViewFlags::Jittered) |
                                      static_cast<u32>(RenderViewFlags::InfiniteFarPlane);
        if ((static_cast<u32>(view.flags) & ~validFlagBits) != 0)
            return Fail(failure, RenderViewFailureCode::InvalidFlags,
                        "render view contains unknown policy flags", view.id, view.family);
        if (!phases.IsSealed())
            return Fail(failure, RenderViewFailureCode::UnsealedPhaseRegistry,
                        "render views require a sealed render phase registry", view.id, view.family);
        if (view.phases.Empty())
            return Fail(failure, RenderViewFailureCode::EmptyPhaseSet,
                        "render view does not request any phases", view.id, view.family);
        const RenderPhaseRegistryStats phaseStats = phases.GetStats();
        const u64 knownBits = phaseStats.registeredPhases == 64u
                                  ? ~0ull
                                  : ((1ull << phaseStats.registeredPhases) - 1ull);
        if ((view.phases.Bits() & ~knownBits) != 0)
            return Fail(failure, RenderViewFailureCode::UnknownPhase,
                        "render view requests an unregistered phase", view.id, view.family);
        if (!view.rect.IsValid())
            return Fail(failure, RenderViewFailureCode::InvalidExtent,
                        "render view extent is invalid", view.id, view.family);
        if (!ValidFrustum(view.frustum))
            return Fail(failure, RenderViewFailureCode::InvalidFrustum,
                        "render view frustum is invalid", view.id, view.family);
        if (!Finite(view.origin.localPosition, 3) || !Finite(view.previousOrigin.localPosition, 3))
            return Fail(failure, RenderViewFailureCode::InvalidOrigin,
                        "render view origin is invalid", view.id, view.family);
        if (!Finite(view.matrices.worldToView, 16) || !Finite(view.matrices.viewToClip, 16) ||
            !Finite(view.matrices.worldToClip, 16) || !Finite(view.matrices.previousWorldToClip, 16))
            return Fail(failure, RenderViewFailureCode::InvalidMatrices,
                        "render view matrices contain non-finite values", view.id, view.family);
        const bool infiniteFarPlane = (view.flags & RenderViewFlags::InfiniteFarPlane) != RenderViewFlags::None;
        if (!std::isfinite(view.nearPlane) || view.nearPlane < 0.0f || !std::isfinite(view.lodBias) ||
            (infiniteFarPlane ? view.farPlane != 0.0f
                              : (!std::isfinite(view.farPlane) || view.farPlane <= view.nearPlane)))
            return Fail(failure, RenderViewFailureCode::InvalidDepthRange,
                        "render view depth range or LOD bias is invalid", view.id, view.family);
        if (!Finite(view.jitter, 2) || !Finite(view.previousJitter, 2))
            return Fail(failure, RenderViewFailureCode::InvalidMatrices,
                        "render view jitter is invalid", view.id, view.family);
        if ((view.flags & RenderViewFlags::TemporalHistory) != RenderViewFlags::None && view.temporalIdentity == 0)
            return Fail(failure, RenderViewFailureCode::InvalidTemporalState,
                        "temporal render view requires a stable temporal identity", view.id, view.family);
        if (view.frameSerial == 0)
            return Fail(failure, RenderViewFailureCode::InvalidIdentity,
                        "render view frame serial is invalid", view.id, view.family);
        if (!ValidName(view.name))
            return Fail(failure, RenderViewFailureCode::InvalidName,
                        "render view name is empty or unterminated", view.id, view.family);
        return true;
    }

    bool ValidateRenderViewFamily(const RenderViewFamily& family, const RenderPhaseRegistry& phases,
                                  RenderViewFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!family.id.IsValid())
            return Fail(failure, RenderViewFailureCode::InvalidFamily,
                        "render view family identity is invalid", {}, family.id);
        if (family.views == nullptr || family.viewCount == 0 || family.viewCount > MaximumRenderViewsPerFamily)
            return Fail(failure, family.viewCount > MaximumRenderViewsPerFamily
                                     ? RenderViewFailureCode::CapacityExceeded
                                     : RenderViewFailureCode::InvalidFamily,
                        "render view family span is invalid", {}, family.id);
        if (family.frameSerial == 0 || family.sceneIdentity == 0 || family.sceneVersion == 0)
            return Fail(failure, RenderViewFailureCode::InvalidIdentity,
                        "render view family frame or scene identity is invalid", {}, family.id);

        for (u32 index = 0; index < family.viewCount; ++index)
        {
            const RenderView& view = family.views[index];
            if (view.family != family.id || view.frameSerial != family.frameSerial)
                return Fail(failure, RenderViewFailureCode::ForeignView,
                            "render view does not belong to this family publication", view.id, family.id);
            if (!ValidateRenderView(view, phases, failure)) return false;
            for (u32 previous = 0; previous < index; ++previous)
                if (family.views[previous].id == view.id)
                    return Fail(failure, RenderViewFailureCode::DuplicateView,
                                "render view family contains a duplicate view", view.id, family.id);
        }
        return true;
    }
} // namespace vanguard::rendering
