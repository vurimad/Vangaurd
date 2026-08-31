#pragma once

#include <vanguard/rendering/gpu_scene_types.hpp>

#include <vanguard/containers/containers.hpp>

namespace vanguard::rendering
{
    inline constexpr u32 GpuVisibilityThreadsPerGroup = 128;
    inline constexpr u32 MaximumGpuVisibilityViews = MaximumRenderViews;

    /// One compute workgroup consumes one range. candidateCount never exceeds
    /// GpuVisibilityThreadsPerGroup, keeping scheduling regular and preventing long-tail groups.
    struct alignas(16) GpuVisibilityWorkRange
    {
        u32 candidateOffset = 0;
        u32 candidateCount = 0;
        u32 viewIndex = InvalidGpuSceneIndex;
        u32 resultIndex = InvalidGpuSceneIndex;
    };

    /// A view owns a disjoint output partition. The graph clears its counter before visibility;
    /// the shader clamps writes to visibleCapacity and reports every discarded result.
    struct alignas(16) GpuVisibilityResultRange
    {
        u32 visibleOffset = 0;
        u32 visibleCapacity = 0;
        u32 counterIndex = InvalidGpuSceneIndex;
        u32 reserved = 0;
    };

    /// visibleCount is the requested count and may exceed the output capacity. Consumers use
    /// min(visibleCount, visibleCapacity); overflowCount preserves the exact capacity failure.
    struct alignas(16) GpuVisibilityCounters
    {
        u32 visibleCount = 0;
        u32 culledCount = 0;
        u32 overflowCount = 0;
        u32 reserved = 0;
    };

    /// The GPU-visibility service supplies these descriptor indices as push constants when its graph
    /// pass executes. Persistent table directories and frame-local buffers share one descriptor domain.
    struct alignas(16) GpuVisibilityConstants
    {
        u32 tableDirectoryDescriptor = InvalidGpuDescriptorIndex;
        u32 pageDirectoryDescriptor = InvalidGpuDescriptorIndex;
        u32 candidateDescriptor = InvalidGpuDescriptorIndex;
        u32 workRangeDescriptor = InvalidGpuDescriptorIndex;

        u32 viewDescriptor = InvalidGpuDescriptorIndex;
        u32 resultRangeDescriptor = InvalidGpuDescriptorIndex;
        u32 visibleDescriptor = InvalidGpuDescriptorIndex;
        u32 counterDescriptor = InvalidGpuDescriptorIndex;

        u32 workRangeCount = 0;
        u32 instanceTable = 0;
        u32 renderableTable = 2;
        u32 layoutVersion = GpuSceneLayoutVersion;

        f32 worldCellSize = 0.0f;
        u32 reserved0 = 0;
        u32 reserved1 = 0;
        u32 reserved2 = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return tableDirectoryDescriptor != InvalidGpuDescriptorIndex && pageDirectoryDescriptor != InvalidGpuDescriptorIndex &&
                   candidateDescriptor != InvalidGpuDescriptorIndex && workRangeDescriptor != InvalidGpuDescriptorIndex &&
                   viewDescriptor != InvalidGpuDescriptorIndex && resultRangeDescriptor != InvalidGpuDescriptorIndex &&
                   visibleDescriptor != InvalidGpuDescriptorIndex && counterDescriptor != InvalidGpuDescriptorIndex && workRangeCount != 0 &&
                   layoutVersion == GpuSceneLayoutVersion && worldCellSize > 0.0f;
        }
    };

    struct GpuVisibilityBuildStorage
    {
        containers::ArraySpan<GpuView> views;
        containers::ArraySpan<GpuInstanceIndex> candidates;
        containers::ArraySpan<GpuVisibilityWorkRange> workRanges;
        containers::ArraySpan<GpuVisibilityResultRange> results;
    };

    struct GpuVisibilityCandidateReservation
    {
        GpuInstanceIndex* destination = nullptr;
        u32 capacity = 0;
        u32 workRangeCapacity = 0;
        u32 candidateOffset = 0;
        u32 viewOrdinal = InvalidGpuSceneIndex;
        u32 token = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return (destination != nullptr || capacity == 0) && viewOrdinal != InvalidGpuSceneIndex && token != 0;
        }
    };

    /// Filled prefix of one preassigned candidate slice. offset is relative to the owning view reservation.
    struct GpuVisibilityCandidateRange
    {
        u32 offset = 0;
        u32 count = 0;
    };

    struct GpuVisibilityPlan
    {
        u64 frameSerial = 0;
        containers::ArraySpan<const GpuView> views;
        containers::ArraySpan<const GpuInstanceIndex> candidates;
        containers::ArraySpan<const GpuVisibilityWorkRange> workRanges;
        containers::ArraySpan<const GpuVisibilityResultRange> results;
        u32 visibleCapacity = 0;
        u32 counterCount = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return frameSerial != 0 && views.Size() == results.Size() && counterCount == results.Size() && (!workRanges.Empty() || candidates.Empty());
        }
    };

    enum class GpuVisibilityBuildFailureCode : u8
    {
        None,
        AlreadyBuilding,
        NotBuilding,
        InvalidStorage,
        InvalidFrame,
        InvalidView,
        DuplicateView,
        CapacityExceeded,
        InvalidReservation,
        CompletionOrderViolation,
        IncompleteReservations
    };

    struct GpuVisibilityBuildFailure
    {
        GpuVisibilityBuildFailureCode code = GpuVisibilityBuildFailureCode::None;
        u32 viewOrdinal = InvalidGpuSceneIndex;
        const char* message = nullptr;
    };

    /// Planning is single-threaded; candidate producers may fill disjoint returned reservations in
    /// parallel. CompleteView is called in reservation order after those producer jobs have joined.
    /// No payload copy or internal candidate mirror is created.
    class GpuVisibilityPlanBuilder final
    {
    public:
        [[nodiscard]] bool Begin(const GpuVisibilityBuildStorage& storage, u64 frameSerial, GpuVisibilityBuildFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool ReserveView(const GpuView& view, u32 candidateCapacity, u32 visibleCapacity, GpuVisibilityCandidateReservation& reservation,
                                       GpuVisibilityBuildFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool ReserveViewRanges(const GpuView& view, u32 candidateCapacity, u32 maximumWorkRanges, u32 visibleCapacity,
                                             GpuVisibilityCandidateReservation& reservation, GpuVisibilityBuildFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool CompleteView(GpuVisibilityCandidateReservation& reservation, u32 candidateCount,
                                        GpuVisibilityBuildFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool CompleteViewRanges(GpuVisibilityCandidateReservation& reservation, containers::ArraySpan<const GpuVisibilityCandidateRange> ranges,
                                              GpuVisibilityBuildFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool Finalize(GpuVisibilityPlan& plan, GpuVisibilityBuildFailure* failure = nullptr) noexcept;
        void Cancel() noexcept;
        [[nodiscard]] bool IsBuilding() const noexcept
        {
            return m_building;
        }

    private:
        GpuVisibilityBuildStorage m_storage;
        u64 m_frameSerial = 0;
        u32 m_token = 0;
        u32 m_viewCount = 0;
        u32 m_nextCompletion = 0;
        u32 m_candidateCapacity = 0;
        u32 m_candidateExtent = 0;
        u32 m_reservedWorkRanges = 0;
        u32 m_workRangeCount = 0;
        u32 m_visibleCapacity = 0;
        u32 m_viewAdmissionStamp = 0;
        u32 m_viewAdmissionStamps[MaximumGpuVisibilityViews]{};
        bool m_building = false;
    };

    static_assert(sizeof(GpuVisibilityWorkRange) == 16);
    static_assert(sizeof(GpuVisibilityResultRange) == 16);
    static_assert(sizeof(GpuVisibilityCounters) == 16);
    static_assert(sizeof(GpuVisibilityConstants) == 64);
} // namespace vanguard::rendering
