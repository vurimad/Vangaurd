#include <vanguard/rendering/gpu_scene_visibility.hpp>

namespace vanguard::rendering
{
    namespace
    {
        void ClearFailure(GpuVisibilityBuildFailure* const failure) noexcept
        {
            if (failure != nullptr)
                *failure = {};
        }

        [[nodiscard]] bool Fail(GpuVisibilityBuildFailure* const failure, const GpuVisibilityBuildFailureCode code, const char* const message,
                                const u32 viewOrdinal = InvalidGpuSceneIndex) noexcept
        {
            if (failure != nullptr)
                *failure = {code, viewOrdinal, message};
            return false;
        }

        [[nodiscard]] bool AddWouldOverflow(const u32 left, const u32 right) noexcept
        {
            return right > ~u32{0} - left;
        }
    } // namespace

    bool GpuVisibilityPlanBuilder::Begin(const GpuVisibilityBuildStorage& storage, const u64 frameSerial, GpuVisibilityBuildFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_building)
            return Fail(failure, GpuVisibilityBuildFailureCode::AlreadyBuilding, "GPU visibility plan builder already has an open plan");
        if (frameSerial == 0)
            return Fail(failure, GpuVisibilityBuildFailureCode::InvalidFrame, "GPU visibility plan requires a nonzero frame serial");
        if (storage.views.Empty() || storage.results.Size() < storage.views.Size() || storage.views.Size() > MaximumGpuVisibilityViews)
            return Fail(failure, GpuVisibilityBuildFailureCode::InvalidStorage, "GPU visibility planning storage is missing or inconsistent");

        m_storage = storage;
        m_frameSerial = frameSerial;
        ++m_token;
        if (m_token == 0)
            ++m_token;
        m_viewCount = 0;
        m_nextCompletion = 0;
        m_candidateCapacity = 0;
        m_candidateExtent = 0;
        m_reservedWorkRanges = 0;
        m_workRangeCount = 0;
        m_visibleCapacity = 0;
        if (++m_viewAdmissionStamp == 0)
        {
            for (u32 index = 0; index < MaximumGpuVisibilityViews; ++index)
                m_viewAdmissionStamps[index] = 0;
            ++m_viewAdmissionStamp;
        }
        m_building = true;
        return true;
    }

    bool GpuVisibilityPlanBuilder::ReserveView(const GpuView& view, const u32 candidateCapacity, const u32 visibleCapacity,
                                               GpuVisibilityCandidateReservation& reservation, GpuVisibilityBuildFailure* const failure) noexcept
    {
        const u32 requiredRanges = candidateCapacity / GpuVisibilityThreadsPerGroup + (candidateCapacity % GpuVisibilityThreadsPerGroup != 0 ? 1u : 0u);
        return ReserveViewRanges(view, candidateCapacity, requiredRanges, visibleCapacity, reservation, failure);
    }

    bool GpuVisibilityPlanBuilder::ReserveViewRanges(const GpuView& view, const u32 candidateCapacity, const u32 maximumWorkRanges, const u32 visibleCapacity,
                                                     GpuVisibilityCandidateReservation& reservation, GpuVisibilityBuildFailure* const failure) noexcept
    {
        ClearFailure(failure);
        reservation = {};
        if (!m_building)
            return Fail(failure, GpuVisibilityBuildFailureCode::NotBuilding, "GPU visibility plan builder has no open plan");
        if (view.viewIndex >= MaximumRenderViews || view.viewGeneration == 0 || view.frustumPlaneCount == 0 ||
            view.frustumPlaneCount > MaximumVisibilityFrustumPlanes || visibleCapacity == 0)
            return Fail(failure, GpuVisibilityBuildFailureCode::InvalidView, "GPU visibility view or capacity is invalid", m_viewCount);
        if (m_viewAdmissionStamps[view.viewIndex] == m_viewAdmissionStamp)
            return Fail(failure, GpuVisibilityBuildFailureCode::DuplicateView, "GPU visibility plan contains the same view index twice", view.viewIndex);

        if ((candidateCapacity == 0) != (maximumWorkRanges == 0))
            return Fail(failure, GpuVisibilityBuildFailureCode::InvalidView, "GPU visibility candidate and work-range capacities are inconsistent",
                        m_viewCount);
        if (m_viewCount >= m_storage.views.Size() || candidateCapacity > m_storage.candidates.Size() - m_candidateCapacity ||
            maximumWorkRanges > m_storage.workRanges.Size() - m_reservedWorkRanges || AddWouldOverflow(m_visibleCapacity, visibleCapacity))
            return Fail(failure, GpuVisibilityBuildFailureCode::CapacityExceeded, "GPU visibility planning storage capacity was exceeded", m_viewCount);

        const u32 ordinal = m_viewCount++;
        m_viewAdmissionStamps[view.viewIndex] = m_viewAdmissionStamp;
        m_storage.views[ordinal] = view;
        m_storage.results[ordinal] = {m_visibleCapacity, visibleCapacity, ordinal, 0};
        reservation.destination = m_storage.candidates.Data() != nullptr ? m_storage.candidates.Data() + m_candidateCapacity : nullptr;
        reservation.capacity = candidateCapacity;
        reservation.workRangeCapacity = maximumWorkRanges;
        reservation.candidateOffset = m_candidateCapacity;
        reservation.viewOrdinal = ordinal;
        reservation.token = m_token;
        m_candidateCapacity += candidateCapacity;
        m_reservedWorkRanges += maximumWorkRanges;
        m_visibleCapacity += visibleCapacity;
        return true;
    }

    bool GpuVisibilityPlanBuilder::CompleteView(GpuVisibilityCandidateReservation& reservation, const u32 candidateCount,
                                                GpuVisibilityBuildFailure* const failure) noexcept
    {
        const GpuVisibilityCandidateRange range{0, candidateCount};
        return CompleteViewRanges(reservation, {&range, 1}, failure);
    }

    bool GpuVisibilityPlanBuilder::CompleteViewRanges(GpuVisibilityCandidateReservation& reservation,
                                                      const containers::ArraySpan<const GpuVisibilityCandidateRange> ranges,
                                                      GpuVisibilityBuildFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!m_building)
            return Fail(failure, GpuVisibilityBuildFailureCode::NotBuilding, "GPU visibility plan builder has no open plan");
        if (!reservation.IsValid() || reservation.token != m_token || reservation.viewOrdinal >= m_viewCount)
            return Fail(failure, GpuVisibilityBuildFailureCode::InvalidReservation, "GPU visibility candidate reservation is invalid or stale",
                        reservation.viewOrdinal);
        if (reservation.viewOrdinal != m_nextCompletion)
            return Fail(failure, GpuVisibilityBuildFailureCode::CompletionOrderViolation, "GPU visibility views must complete in reservation order",
                        reservation.viewOrdinal);

        u32 previousEnd = 0;
        u32 requiredWorkRanges = 0;
        for (const GpuVisibilityCandidateRange range : ranges)
        {
            if (range.offset < previousEnd || range.offset > reservation.capacity || range.count > reservation.capacity - range.offset)
                return Fail(failure, GpuVisibilityBuildFailureCode::InvalidReservation, "GPU visibility candidate ranges overlap or exceed their reservation",
                            reservation.viewOrdinal);
            previousEnd = range.offset + range.count;
            const u32 rangeWorkCount = range.count / GpuVisibilityThreadsPerGroup + (range.count % GpuVisibilityThreadsPerGroup != 0 ? 1u : 0u);
            if (rangeWorkCount > reservation.workRangeCapacity - requiredWorkRanges)
                return Fail(failure, GpuVisibilityBuildFailureCode::CapacityExceeded,
                            "GPU visibility candidate ranges exceed their reserved work-range capacity", reservation.viewOrdinal);
            requiredWorkRanges += rangeWorkCount;
        }

        const u32 candidateOffset = reservation.candidateOffset;
        for (const GpuVisibilityCandidateRange range : ranges)
        {
            if (range.count == 0)
                continue;
            u32 remaining = range.count;
            u32 offset = candidateOffset + range.offset;
            while (remaining != 0)
            {
                const u32 count = remaining > GpuVisibilityThreadsPerGroup ? GpuVisibilityThreadsPerGroup : remaining;
                m_storage.workRanges[m_workRangeCount++] = {offset, count, reservation.viewOrdinal, reservation.viewOrdinal};
                offset += count;
                remaining -= count;
            }
            const u32 extent = candidateOffset + range.offset + range.count;
            if (extent > m_candidateExtent)
                m_candidateExtent = extent;
        }
        ++m_nextCompletion;
        reservation = {};
        return true;
    }

    bool GpuVisibilityPlanBuilder::Finalize(GpuVisibilityPlan& plan, GpuVisibilityBuildFailure* const failure) noexcept
    {
        ClearFailure(failure);
        plan = {};
        if (!m_building)
            return Fail(failure, GpuVisibilityBuildFailureCode::NotBuilding, "GPU visibility plan builder has no open plan");
        if (m_nextCompletion != m_viewCount)
            return Fail(failure, GpuVisibilityBuildFailureCode::IncompleteReservations, "GPU visibility plan still has incomplete candidate reservations",
                        m_nextCompletion);

        plan.frameSerial = m_frameSerial;
        plan.views = {m_storage.views.Data(), m_viewCount};
        plan.candidates = {m_storage.candidates.Data(), m_candidateExtent};
        plan.workRanges = {m_storage.workRanges.Data(), m_workRangeCount};
        plan.results = {m_storage.results.Data(), m_viewCount};
        plan.visibleCapacity = m_visibleCapacity;
        plan.counterCount = m_viewCount;
        m_building = false;
        return true;
    }

    void GpuVisibilityPlanBuilder::Cancel() noexcept
    {
        m_building = false;
        m_frameSerial = 0;
        m_viewCount = 0;
        m_nextCompletion = 0;
        m_candidateCapacity = 0;
        m_candidateExtent = 0;
        m_reservedWorkRanges = 0;
        m_workRangeCount = 0;
        m_visibleCapacity = 0;
    }
} // namespace vanguard::rendering
