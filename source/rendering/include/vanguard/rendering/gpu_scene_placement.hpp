#pragma once

#include <vanguard/rendering/gpu_scene_upload.hpp>

namespace vanguard::rendering
{
    struct GpuSceneLodPlacementInstall
    {
        GpuSceneAllocation renderableOwner;
        GpuRenderableResidency residency;

        GpuSceneAllocation primitiveOwner;
        u32 firstPrimitive = 0;
        containers::ArraySpan<const GpuPrimitivePlacement> primitives;

        GpuSceneAllocation phaseOwner;
        u32 firstPhase = 0;
        containers::ArraySpan<const GpuPhasePlacement> phases;
    };

    struct GpuSceneLodPlacementPublication
    {
        /// Placement records may have been submitted while residency remains unchanged. This state is
        /// intentionally safe: no consumer may select the new LOD until residencyCompletion is submitted.
        rhi::GpuFence placementCompletion;
        rhi::GpuFence residencyCompletion;

        [[nodiscard]] constexpr bool IsPublished() const noexcept
        {
            return residencyCompletion.IsValid();
        }
    };

    enum class GpuScenePlacementFailureCode : u8
    {
        None,
        InvalidInstall,
        PlacementUploadFailure,
        ResidencyUploadFailure
    };

    struct GpuScenePlacementFailure
    {
        GpuScenePlacementFailureCode code = GpuScenePlacementFailureCode::None;
        const char* message = nullptr;
        GpuSceneUploadFailure uploadFailure;
    };

    /// Submits complete primitive and phase placement images first, then submits the authoritative
    /// RenderableResidency image in a second ordered upload. Failure before the second submission leaves
    /// the old residency entry authoritative, so partially written placements cannot become selectable.
    [[nodiscard]] bool PublishGpuSceneLodPlacement(GpuSceneUploader& uploader, const GpuSceneLodPlacementInstall& install,
                                                   GpuSceneLodPlacementPublication& publication,
                                                   GpuScenePlacementFailure* failure = nullptr) noexcept;
} // namespace vanguard::rendering
