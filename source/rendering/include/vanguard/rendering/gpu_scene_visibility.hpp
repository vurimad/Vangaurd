#pragma once

#include <vanguard/rendering/gpu_scene_types.hpp>

#include <vanguard/containers/containers.hpp>
#include <vanguard/rhi/rhi_types.hpp>

namespace vanguard::rendering
{
    inline constexpr u32 GpuVisibilityThreadsPerGroup = 128;
    inline constexpr u32 MaximumGpuVisibilityViews = MaximumRenderViews;
    inline constexpr u32 GpuGeometryWorkMirrored = 1u << 0u;
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

    /// One bounded expansion group consumes at most GpuVisibilityThreadsPerGroup
    /// entries from one view-owned visible partition.
    struct alignas(16) GpuGeometryExpansionWorkRange
    {
        u32 visibleOrdinal = 0;
        u32 visibleCount = 0;
        u32 resultIndex = InvalidGpuSceneIndex;
        u32 prefixBlockIndex = InvalidGpuSceneIndex;
    };

    /// Count writes the first two lanes. The block-prefix stages assign the
    /// final two lanes, giving scatter a disjoint range with no append atomic.
    struct alignas(16) GpuGeometryExpansionItem
    {
        u32 validWorkCount = 0;
        u32 rejectedWorkCount = 0;
        u32 outputOffset = 0;
        u32 outputCapacity = 0;
    };

    /// Validated primitive/phase work. Binning consumes shell/bin and writes
    /// instance/primitive/geometry/material to MeshDrawInstance.
    struct alignas(16) GpuGeometryWork
    {
        u32 instance = InvalidGpuSceneIndex;
        u32 primitive = InvalidGpuSceneIndex;
        u32 geometry = InvalidGpuSceneIndex;
        u32 material = InvalidGpuSceneIndex;

        u32 phase = InvalidGpuSceneIndex;
        u32 shell = InvalidGpuSceneIndex;
        u32 bin = InvalidGpuSceneIndex;
        u32 flags = 0;
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

        u32 expansionBlockOffset = 0;
        u32 expansionBlockCount = 0;
        u32 binBlockOffset = 0;
        u32 binBlockCount = 0;
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

    /// Capacity-derived work over one view partition. It is shared by bin count
    /// and instance scatter, both of which clamp to emittedWorkCount.
    struct alignas(16) GpuGeometryWorkRange
    {
        u32 workOrdinal = 0;
        u32 workCount = 0;
        u32 resultIndex = InvalidGpuSceneIndex;
        u32 reserved = 0;
    };

    struct alignas(16) GpuGeometryPrefixWorkRange
    {
        u32 itemOrdinal = 0;
        u32 itemCount = 0;
        u32 resultIndex = InvalidGpuSceneIndex;
        u32 prefixBlockIndex = InvalidGpuSceneIndex;
    };

    struct alignas(16) GpuGeometryPrefixBlock
    {
        u32 requestedCount = 0;
        u32 rejectedCount = 0;
        u32 outputOffset = 0;
        u32 outputCapacity = 0;
    };

    struct alignas(16) GpuGeometryPrefixConstants
    {
        u32 visibilityResultDescriptor = InvalidGpuDescriptorIndex;
        u32 visibilityCounterDescriptor = InvalidGpuDescriptorIndex;
        u32 itemDescriptor = InvalidGpuDescriptorIndex;
        u32 resultDescriptor = InvalidGpuDescriptorIndex;

        u32 counterDescriptor = InvalidGpuDescriptorIndex;
        u32 expansionRangeDescriptor = InvalidGpuDescriptorIndex;
        u32 binRangeDescriptor = InvalidGpuDescriptorIndex;
        u32 binItemDescriptor = InvalidGpuDescriptorIndex;

        u32 prefixBlockDescriptor = InvalidGpuDescriptorIndex;
        u32 expansionRangeCount = 0;
        u32 binRangeCount = 0;
        u32 resultCount = 0;

        u32 layoutVersion = GpuSceneLayoutVersion;
        u32 reserved0 = 0;
        u32 reserved1 = 0;
        u32 reserved2 = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return visibilityResultDescriptor != InvalidGpuDescriptorIndex &&
                   visibilityCounterDescriptor != InvalidGpuDescriptorIndex && itemDescriptor != InvalidGpuDescriptorIndex &&
                   resultDescriptor != InvalidGpuDescriptorIndex && counterDescriptor != InvalidGpuDescriptorIndex && resultCount != 0 &&
                   expansionRangeDescriptor != InvalidGpuDescriptorIndex && binRangeDescriptor != InvalidGpuDescriptorIndex &&
                   binItemDescriptor != InvalidGpuDescriptorIndex && prefixBlockDescriptor != InvalidGpuDescriptorIndex &&
                   expansionRangeCount != 0 && binRangeCount != 0 &&
                   layoutVersion == GpuSceneLayoutVersion;
        }
    };

    /// Shared contract for bin count and instance scatter. All offsets are
    /// resolved through per-view results; indirect generation has its own inputs.
    struct alignas(16) GpuGeometryBinningConstants
    {
        u32 workDescriptor = InvalidGpuDescriptorIndex;
        u32 workRangeDescriptor = InvalidGpuDescriptorIndex;
        u32 resultDescriptor = InvalidGpuDescriptorIndex;
        u32 counterDescriptor = InvalidGpuDescriptorIndex;

        u32 binItemDescriptor = InvalidGpuDescriptorIndex;
        u32 instanceDescriptor = InvalidGpuDescriptorIndex;
        u32 workRangeCount = 0;
        u32 resultCount = 0;

        u32 layoutVersion = GpuSceneLayoutVersion;
        u32 reserved0 = 0;
        u32 reserved1 = 0;
        u32 reserved2 = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return workDescriptor != InvalidGpuDescriptorIndex && workRangeDescriptor != InvalidGpuDescriptorIndex &&
                   resultDescriptor != InvalidGpuDescriptorIndex && counterDescriptor != InvalidGpuDescriptorIndex &&
                   binItemDescriptor != InvalidGpuDescriptorIndex && instanceDescriptor != InvalidGpuDescriptorIndex &&
                   workRangeCount != 0 && resultCount != 0 &&
                   layoutVersion == GpuSceneLayoutVersion;
        }
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

    struct alignas(16) GpuGeometryIndirectConstants
    {
        u32 tableDirectoryDescriptor = InvalidGpuDescriptorIndex;
        u32 pageDirectoryDescriptor = InvalidGpuDescriptorIndex;
        u32 resultDescriptor = InvalidGpuDescriptorIndex;
        u32 binItemDescriptor = InvalidGpuDescriptorIndex;
        u32 shellPlanDescriptor = InvalidGpuDescriptorIndex;
        u32 shellRangeDescriptor = InvalidGpuDescriptorIndex;
        u32 shellCounterDescriptor = InvalidGpuDescriptorIndex;
        u32 argumentDescriptor = InvalidGpuDescriptorIndex;
        u32 binRangeDescriptor = InvalidGpuDescriptorIndex;
        u32 binRangeCount = 0;
        u32 shellPlanCount = 0;
        u32 shellCapacity = 0;
        u32 counterDescriptor = InvalidGpuDescriptorIndex;
        u32 resultCount = 0;
        u32 layoutVersion = GpuSceneLayoutVersion;
        u32 reserved = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return tableDirectoryDescriptor != InvalidGpuDescriptorIndex && pageDirectoryDescriptor != InvalidGpuDescriptorIndex &&
                   resultDescriptor != InvalidGpuDescriptorIndex && binItemDescriptor != InvalidGpuDescriptorIndex &&
                   shellPlanDescriptor != InvalidGpuDescriptorIndex && shellRangeDescriptor != InvalidGpuDescriptorIndex &&
                   shellCounterDescriptor != InvalidGpuDescriptorIndex && argumentDescriptor != InvalidGpuDescriptorIndex &&
                   binRangeDescriptor != InvalidGpuDescriptorIndex && counterDescriptor != InvalidGpuDescriptorIndex &&
                   resultCount != 0 && binRangeCount != 0 && shellCapacity != 0 &&
                   static_cast<u64>(resultCount) * shellCapacity < InvalidGpuSceneIndex &&
                   static_cast<u64>(shellPlanCount) * sizeof(GpuGeometryShellCounters) <= 0xffffffffull &&
                   layoutVersion == GpuSceneLayoutVersion;
        }
    };

    /// Shared descriptor contract for the count and scatter expansion entries.
    /// Dispatch ranges are capacity-derived on the CPU; each shader thread still
    /// clamps itself to the GPU-produced visible count for its result partition.
    struct alignas(16) GpuGeometryExpansionConstants
    {
        u32 tableDirectoryDescriptor = InvalidGpuDescriptorIndex;
        u32 pageDirectoryDescriptor = InvalidGpuDescriptorIndex;
        u32 visibleDescriptor = InvalidGpuDescriptorIndex;
        u32 visibilityResultDescriptor = InvalidGpuDescriptorIndex;

        u32 visibilityCounterDescriptor = InvalidGpuDescriptorIndex;
        u32 workRangeDescriptor = InvalidGpuDescriptorIndex;
        u32 itemDescriptor = InvalidGpuDescriptorIndex;
        u32 workDescriptor = InvalidGpuDescriptorIndex;

        u32 viewDescriptor = InvalidGpuDescriptorIndex;
        u32 workRangeCount = 0;
        u32 layoutVersion = GpuSceneLayoutVersion;
        u32 reserved0 = 0;

        u32 reserved1 = 0;
        u32 reserved2 = 0;
        u32 reserved3 = 0;
        u32 reserved4 = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return tableDirectoryDescriptor != InvalidGpuDescriptorIndex && pageDirectoryDescriptor != InvalidGpuDescriptorIndex &&
                   visibleDescriptor != InvalidGpuDescriptorIndex && visibilityResultDescriptor != InvalidGpuDescriptorIndex &&
                   visibilityCounterDescriptor != InvalidGpuDescriptorIndex && workRangeDescriptor != InvalidGpuDescriptorIndex &&
                   itemDescriptor != InvalidGpuDescriptorIndex && workDescriptor != InvalidGpuDescriptorIndex &&
                   viewDescriptor != InvalidGpuDescriptorIndex && workRangeCount != 0 &&
                   layoutVersion == GpuSceneLayoutVersion;
        }
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
    static_assert(sizeof(GpuVisibleInstance) == 16);
    static_assert(sizeof(GpuGeometryExpansionWorkRange) == 16);
    static_assert(sizeof(GpuGeometryExpansionItem) == 16);
    static_assert(sizeof(GpuGeometryWork) == 32);
    static_assert(sizeof(GpuGeometryResultRange) == 48);
    static_assert(sizeof(GpuGeometryCounters) == 32);
    static_assert(sizeof(GpuGeometryBinItem) == 16);
    static_assert(sizeof(GpuGeometryWorkRange) == 16);
    static_assert(sizeof(GpuGeometryPrefixWorkRange) == 16);
    static_assert(sizeof(GpuGeometryPrefixBlock) == 16);
    static_assert(sizeof(GpuGeometryPrefixConstants) == 64);
    static_assert(sizeof(GpuGeometryBinningConstants) == 48);
    static_assert(sizeof(GpuGeometryIndirectArguments) == 20);
    static_assert(sizeof(GpuGeometryShellRange) == 32);
    static_assert(sizeof(GpuGeometryShellCounters) == 16);
    static_assert(sizeof(GpuGeometryIndirectConstants) == 64);
    static_assert(sizeof(GpuGeometryExpansionConstants) == 64);
    static_assert(sizeof(GpuVisibilityConstants) == 64);
} // namespace vanguard::rendering
