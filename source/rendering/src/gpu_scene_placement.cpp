#include <vanguard/rendering/gpu_scene_placement.hpp>

#include <cstring>

namespace vanguard::rendering
{
    namespace
    {
        void ClearFailure(GpuScenePlacementFailure* const failure) noexcept
        {
            if (failure != nullptr)
                *failure = {};
        }

        [[nodiscard]] bool Fail(GpuScenePlacementFailure* const failure, const GpuScenePlacementFailureCode code,
                                const char* const message, const GpuSceneUploadFailure& uploadFailure = {}) noexcept
        {
            if (failure != nullptr)
                *failure = {code, message, uploadFailure};
            return false;
        }

        [[nodiscard]] bool ValidRange(const u32 first, const u32 count, const u32 capacity) noexcept
        {
            return first <= capacity && count <= capacity - first;
        }
    } // namespace

    bool PublishGpuSceneLodPlacement(GpuSceneUploader& uploader, const GpuSceneLodPlacementInstall& install,
                                     GpuSceneLodPlacementPublication& publication, GpuScenePlacementFailure* const failure) noexcept
    {
        ClearFailure(failure);
        publication = {};
        if (!uploader.IsInitialized() || install.renderableOwner.table != GpuSceneTableKind::Renderable ||
            install.renderableOwner.count != 1 || install.primitiveOwner.table != GpuSceneTableKind::Primitive ||
            install.phaseOwner.table != GpuSceneTableKind::PhaseParticipation || install.primitives.Empty() || install.phases.Empty() ||
            !ValidRange(install.firstPrimitive, install.primitives.Size(), install.primitiveOwner.count) ||
            !ValidRange(install.firstPhase, install.phases.Size(), install.phaseOwner.count) || install.residency.generation != install.renderableOwner.generation ||
            install.residency.placementRevision == 0)
            return Fail(failure, GpuScenePlacementFailureCode::InvalidInstall, "GPU Scene LOD placement install is invalid");

        for (const GpuPrimitivePlacement& primitive : install.primitives)
            if (primitive.placementRevision != install.residency.placementRevision || primitive.geometry == InvalidGpuSceneIndex ||
                primitive.geometryGeneration == 0)
                return Fail(failure, GpuScenePlacementFailureCode::InvalidInstall,
                            "GPU Scene primitive placement is incomplete or uses another revision");
        for (const GpuPhasePlacement& phase : install.phases)
            if (phase.placementRevision != install.residency.placementRevision)
                return Fail(failure, GpuScenePlacementFailureCode::InvalidInstall, "GPU Scene phase placement uses another revision");

        const GpuSceneUploadRequest placementRequests[] = {
            {install.primitiveOwner, install.firstPrimitive, install.primitives.Size(), GpuSceneTableKind::PrimitivePlacement},
            {install.phaseOwner, install.firstPhase, install.phases.Size(), GpuSceneTableKind::PhasePlacement}};
        GpuSceneUploadReservation placementReservations[2];
        GpuSceneUploadFailure uploadFailure;
        if (!uploader.Begin({placementRequests, 2}, {placementReservations, 2}, &uploadFailure))
            return Fail(failure, GpuScenePlacementFailureCode::PlacementUploadFailure, "GPU Scene placement upload reservation failed", uploadFailure);

        std::memcpy(placementReservations[0].destination, install.primitives.Data(), placementReservations[0].size);
        std::memcpy(placementReservations[1].destination, install.phases.Data(), placementReservations[1].size);
        if (!uploader.Complete(placementReservations[0], &uploadFailure) || !uploader.Complete(placementReservations[1], &uploadFailure))
        {
            static_cast<void>(uploader.Cancel());
            return Fail(failure, GpuScenePlacementFailureCode::PlacementUploadFailure, "GPU Scene placement upload completion failed", uploadFailure);
        }

        GpuSceneUploadResult placementResult;
        if (!uploader.Submit(placementResult, &uploadFailure))
            return Fail(failure, GpuScenePlacementFailureCode::PlacementUploadFailure, "GPU Scene placement upload submission failed", uploadFailure);
        publication.placementCompletion = placementResult.completion;

        const GpuSceneUploadRequest residencyRequest{install.renderableOwner, 0, 1, GpuSceneTableKind::RenderableResidency};
        GpuSceneUploadReservation residencyReservation;
        if (!uploader.Begin({&residencyRequest, 1}, {&residencyReservation, 1}, &uploadFailure))
            return Fail(failure, GpuScenePlacementFailureCode::ResidencyUploadFailure,
                        "GPU Scene residency upload reservation failed after safe placement submission", uploadFailure);
        *static_cast<GpuRenderableResidency*>(residencyReservation.destination) = install.residency;
        if (!uploader.Complete(residencyReservation, &uploadFailure))
        {
            static_cast<void>(uploader.Cancel());
            return Fail(failure, GpuScenePlacementFailureCode::ResidencyUploadFailure, "GPU Scene residency upload completion failed", uploadFailure);
        }

        GpuSceneUploadResult residencyResult;
        if (!uploader.Submit(residencyResult, &uploadFailure))
            return Fail(failure, GpuScenePlacementFailureCode::ResidencyUploadFailure, "GPU Scene residency upload submission failed", uploadFailure);
        publication.residencyCompletion = residencyResult.completion;
        return true;
    }
} // namespace vanguard::rendering
