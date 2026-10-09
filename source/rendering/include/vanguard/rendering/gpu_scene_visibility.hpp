#pragma once

#include <vanguard/rendering/gpu_scene_types.hpp>

#include <vanguard/containers/containers.hpp>
#include <vanguard/rhi/rhi_types.hpp>

namespace vanguard::rendering
{
    inline constexpr u32 GpuVisibilityThreadsPerGroup = 128;
    inline constexpr u32 MaximumGpuVisibilityViews = MaximumRenderViews;
    using GpuGeometryIndirectArguments = rhi::IndirectDrawIndexedArguments;

    /// One compute workgroup consumes one range. candidateCount never exceeds
    /// GpuVisibilityThreadsPerGroup, keeping scheduling regular and preventing long-tail groups.
    struct alignas(16) GpuVisibilityWorkRange
    {
        u32 candidateOffset = 0;
        u32 candidateCount = 0;
        u32 viewIndex = InvalidGpuSceneIndex;
        u32 resultIndex = InvalidGpuSceneIndex;
    };

    /// A view owns a disjoint output partition. viewIndex addresses the compact
    /// frame view array. The graph clears its counter before visibility; the
    /// shader clamps writes to visibleCapacity and reports every discarded result.
    struct alignas(16) GpuVisibilityResultRange
    {
        u32 visibleOffset = 0;
        u32 visibleCapacity = 0;
        u32 counterIndex = InvalidGpuSceneIndex;
        u32 viewIndex = InvalidGpuSceneIndex;
    };

    /// visibleCount is the requested count and may exceed the output capacity. Consumers use
    /// min(visibleCount, visibleCapacity); overflowCount preserves the exact capacity failure.
    struct alignas(16) GpuVisibilityCounters
    {
        u32 visibleCount = 0;
        u32 culledCount = 0;
        u32 overflowCount = 0;
        u32 rejectedCount = 0;
    };

    /// Compact output of coarse visibility. lod is the selected global GpuLod
    /// index; placementRevision freezes the mutable placement image consumed by
    /// the expansion passes that follow on the same graph dependency chain.
    struct alignas(16) GpuVisibleInstance
    {
        u32 instance = InvalidGpuSceneIndex;
        u32 renderable = InvalidGpuSceneIndex;
        u32 lod = InvalidGpuSceneIndex;
        u32 placementRevision = 0;
    };

    /// Validated primitive/phase work. Sparse expansion appends only actual
    /// work and records the bin-local ordinal needed by the final scatter.
    struct alignas(16) GpuGeometryWork
    {
        u32 instance = InvalidGpuSceneIndex;
        u32 primitive = InvalidGpuSceneIndex;
        u32 geometry = InvalidGpuSceneIndex;
        u32 material = InvalidGpuSceneIndex;

        u32 phase = InvalidGpuSceneIndex;
        u32 shell = InvalidGpuSceneIndex;
        u32 bin = InvalidGpuSceneIndex;
        u32 binOrdinal = InvalidGpuSceneIndex;
    };

    /// One CPU-reserved partition per view. Bin ordinals are direct GeometryBin
    /// indices; indirect arguments are compacted into shell-owned subranges.
    struct alignas(16) GpuGeometryResultRange
    {
        u32 workOffset = 0;
        u32 workCapacity = 0;
        u32 binOffset = 0;
        u32 binCapacity = 0;

        u32 instanceOffset = 0;
        u32 instanceCapacity = 0;
        u32 argumentOffset = 0;
        u32 argumentCapacity = 0;

        u32 activeBinOffset = 0;
        u32 activeBinCapacity = 0;
        u32 reserved0 = 0;
        u32 reserved1 = 0;
    };

    /// Requested values retain the full GPU count. Emitted values are clamped
    /// to their view reservation; every discarded record remains observable.
    struct alignas(16) GpuGeometryCounters
    {
        u32 requestedWorkCount = 0;
        u32 emittedWorkCount = 0;
        u32 rejectedWorkCount = 0;
        u32 expansionOverflowCount = 0;

        u32 invalidBinWorkCount = 0;
        u32 instanceOverflowCount = 0;
        u32 visibleBinCount = 0;
        u32 rejectedIndirectBinCount = 0;
    };

    /// One direct-index entry per (view, GeometryBin). Count and prefix assign
    /// a disjoint instance interval; scatter uses only the per-bin cursor.
    struct alignas(16) GpuGeometryBinItem
    {
        u32 instanceCount = 0;
        // Scatter reservation cursor, not a diagnostic total. Zero-capacity
        // bins skip reservations; instanceCount retains the requested count.
        u32 instanceCursor = 0;
        u32 instanceOffset = 0;
        u32 instanceCapacity = 0;
    };

    /// Sparse expansion consumes the compact visible partitions directly. All
    /// views use one capacity policy, so no CPU-authored expansion ranges exist.
    struct alignas(16) GpuGeometryExpandConstants
    {
        u32 tableDirectoryDescriptor = InvalidGpuDescriptorIndex;
        u32 pageDirectoryDescriptor = InvalidGpuDescriptorIndex;
        u32 visibleDescriptor = InvalidGpuDescriptorIndex;
        u32 visibilityResultDescriptor = InvalidGpuDescriptorIndex;

        u32 visibilityCounterDescriptor = InvalidGpuDescriptorIndex;
        u32 viewDescriptor = InvalidGpuDescriptorIndex;
        u32 workDescriptor = InvalidGpuDescriptorIndex;
        u32 resultDescriptor = InvalidGpuDescriptorIndex;

        u32 counterDescriptor = InvalidGpuDescriptorIndex;
        u32 binItemDescriptor = InvalidGpuDescriptorIndex;
        u32 activeBinDescriptor = InvalidGpuDescriptorIndex;
        u32 resultCount = 0;

        u32 visibleCapacityPerResult = 0;
        u32 layoutVersion = GpuSceneLayoutVersion;
        u32 reserved0 = 0;
        u32 reserved1 = 0;
    };

    /// Frame addresses only. The batcher retains pipeline/arena ownership.
    /// A dense CPU upload is scattered to (resultIndex * shellCapacity + shell)
    /// on the GPU. Generation zero marks unused lookup entries after clearing.
    struct alignas(16) GpuGeometryShellRange
    {
        u32 shell = InvalidGpuSceneIndex;
        u32 generation = 0;
        u32 resultIndex = InvalidGpuSceneIndex;
        u32 phase = InvalidRenderPhaseIndex;
        u32 argumentOffset = 0;
        u32 argumentCapacity = 0;
        u32 counterIndex = InvalidGpuSceneIndex;
        u32 reserved = 0;
    };

    struct alignas(16) GpuGeometryShellCounters
    {
        u32 drawCount = 0;
        u32 overflowCount = 0;
        u32 reserved0 = 0;
        u32 reserved1 = 0;
    };

    struct alignas(16) GpuGeometryInitializeConstants
    {
        u32 shellPlanDescriptor = InvalidGpuDescriptorIndex;
        u32 shellRangeDescriptor = InvalidGpuDescriptorIndex;
        u32 shellCounterDescriptor = InvalidGpuDescriptorIndex;
        u32 counterDescriptor = InvalidGpuDescriptorIndex;

        u32 shellPlanCount = 0;
        u32 shellCapacity = 0;
        u32 resultCount = 0;
        u32 layoutVersion = GpuSceneLayoutVersion;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return shellPlanDescriptor != InvalidGpuDescriptorIndex && shellRangeDescriptor != InvalidGpuDescriptorIndex &&
                   shellCounterDescriptor != InvalidGpuDescriptorIndex && counterDescriptor != InvalidGpuDescriptorIndex &&
                   resultCount != 0 && shellCapacity != 0 &&
                   static_cast<u64>(resultCount) * shellCapacity < InvalidGpuSceneIndex &&
                   static_cast<u64>(shellPlanCount) * sizeof(GpuGeometryShellCounters) <= 0xffffffffull &&
                   layoutVersion == GpuSceneLayoutVersion;
        }
    };

    /// Active-bin allocation visits only bins touched by sparse expansion and
    /// emits one counted-indirect argument per valid nonempty bin.
    struct alignas(16) GpuGeometryAllocateConstants
    {
        u32 tableDirectoryDescriptor = InvalidGpuDescriptorIndex;
        u32 pageDirectoryDescriptor = InvalidGpuDescriptorIndex;
        u32 resultDescriptor = InvalidGpuDescriptorIndex;
        u32 counterDescriptor = InvalidGpuDescriptorIndex;

        u32 binItemDescriptor = InvalidGpuDescriptorIndex;
        u32 activeBinDescriptor = InvalidGpuDescriptorIndex;
        u32 shellRangeDescriptor = InvalidGpuDescriptorIndex;
        u32 shellCounterDescriptor = InvalidGpuDescriptorIndex;

        u32 argumentDescriptor = InvalidGpuDescriptorIndex;
        u32 resultCount = 0;
        u32 shellCapacity = 0;
        u32 shellPlanCount = 0;

        u32 layoutVersion = GpuSceneLayoutVersion;
        u32 reserved0 = 0;
        u32 reserved1 = 0;
        u32 reserved2 = 0;
    };

    struct alignas(16) GpuGeometryScatterConstants
    {
        u32 workDescriptor = InvalidGpuDescriptorIndex;
        u32 resultDescriptor = InvalidGpuDescriptorIndex;
        u32 counterDescriptor = InvalidGpuDescriptorIndex;
        u32 binItemDescriptor = InvalidGpuDescriptorIndex;
        u32 instanceDescriptor = InvalidGpuDescriptorIndex;
        u32 resultCount = 0;
        u32 workCapacityPerResult = 0;
        u32 layoutVersion = GpuSceneLayoutVersion;
    };

    /// The GPU-visibility service supplies these descriptor indices as push constants when its graph
    /// pass executes. Persistent table directories and frame-local buffers share one descriptor domain.
    struct alignas(16) GpuVisibilityConstants
    {
        u32 tableDirectoryDescriptor = InvalidGpuDescriptorIndex;
        u32 pageDirectoryDescriptor = InvalidGpuDescriptorIndex;
        u32 viewDescriptor = InvalidGpuDescriptorIndex;
        u32 resultRangeDescriptor = InvalidGpuDescriptorIndex;

        u32 visibleDescriptor = InvalidGpuDescriptorIndex;
        u32 counterDescriptor = InvalidGpuDescriptorIndex;
        u32 sceneIndex = InvalidGpuSceneIndex;
        u32 sceneGeneration = 0;

        u32 instanceTraversalExtent = 0;
        u32 viewCount = 0;
        u32 instanceTable = 0;
        u32 renderableTable = 2;

        u32 layoutVersion = GpuSceneLayoutVersion;
        f32 worldCellSize = 0.0f;
        u32 reserved0 = 0;
        u32 reserved1 = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return tableDirectoryDescriptor != InvalidGpuDescriptorIndex && pageDirectoryDescriptor != InvalidGpuDescriptorIndex &&
                   viewDescriptor != InvalidGpuDescriptorIndex && resultRangeDescriptor != InvalidGpuDescriptorIndex &&
                   visibleDescriptor != InvalidGpuDescriptorIndex && counterDescriptor != InvalidGpuDescriptorIndex &&
                   sceneIndex != InvalidGpuSceneIndex && sceneGeneration != 0 && viewCount != 0 &&
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
        u32 sceneIndex = InvalidGpuSceneIndex;
        u32 sceneGeneration = 0;
        u32 instanceTraversalExtent = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            const bool directInstanceTraversal = sceneIndex != InvalidGpuSceneIndex && sceneGeneration != 0;
            return frameSerial != 0 && views.Size() == results.Size() && counterCount == results.Size() &&
                   (directInstanceTraversal || !workRanges.Empty() || candidates.Empty());
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
    static_assert(sizeof(GpuVisibleInstance) == 16);
    static_assert(sizeof(GpuGeometryWork) == 32);
    static_assert(sizeof(GpuGeometryResultRange) == 48);
    static_assert(sizeof(GpuGeometryCounters) == 32);
    static_assert(sizeof(GpuGeometryBinItem) == 16);
    static_assert(sizeof(GpuGeometryExpandConstants) == 64);
    static_assert(sizeof(GpuGeometryIndirectArguments) == 20);
    static_assert(sizeof(GpuGeometryShellRange) == 32);
    static_assert(sizeof(GpuGeometryShellCounters) == 16);
    static_assert(sizeof(GpuGeometryInitializeConstants) == 32);
    static_assert(sizeof(GpuGeometryAllocateConstants) == 64);
    static_assert(sizeof(GpuGeometryScatterConstants) == 32);
    static_assert(sizeof(GpuVisibilityConstants) == 64);
} // namespace vanguard::rendering
