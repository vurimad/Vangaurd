#pragma once

#include <vanguard/rendering/gpu_scene_tables.hpp>
#include <vanguard/containers/containers.hpp>

namespace vanguard::rendering
{
    enum class GpuSceneAllocationState : u8
    {
        Invalid,
        Allocated,
        Active,
        Retiring
    };

    struct GpuSceneAllocation
    {
        GpuSceneTableKind table = GpuSceneTableKind::Count;
        u32 first = InvalidGpuSceneIndex;
        u32 count = 0;
        u32 generation = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return static_cast<u32>(table) < GpuSceneTableCount && first != InvalidGpuSceneIndex &&
                   count != 0 && generation != 0;
        }

        template<typename Handle>
        [[nodiscard]] constexpr Handle AsSlotHandle() const noexcept
        {
            return count == 1 ? Handle{first, generation} : Handle{};
        }

        [[nodiscard]] friend constexpr bool operator==(const GpuSceneAllocation&,
                                                       const GpuSceneAllocation&) noexcept = default;
    };

    enum class GpuSceneQueueMask : u8
    {
        None = 0,
        Graphics = 1u << 0u,
        Compute = 1u << 1u,
        Copy = 1u << 2u
    };

    [[nodiscard]] constexpr GpuSceneQueueMask operator|(const GpuSceneQueueMask left,
                                                        const GpuSceneQueueMask right) noexcept
    {
        return static_cast<GpuSceneQueueMask>(static_cast<u8>(left) | static_cast<u8>(right));
    }

    [[nodiscard]] constexpr GpuSceneQueueMask operator&(const GpuSceneQueueMask left,
                                                        const GpuSceneQueueMask right) noexcept
    {
        return static_cast<GpuSceneQueueMask>(static_cast<u8>(left) & static_cast<u8>(right));
    }

    struct GpuSceneLifetimeConfig
    {
        u32 retirementEpochCount = 8;
        u32 initialRetirementsPerEpoch = 4096;
        GpuSceneQueueMask retirementQueues = GpuSceneQueueMask::Graphics | GpuSceneQueueMask::Compute |
                                             GpuSceneQueueMask::Copy;
    };

    inline constexpr u32 MaximumGpuSceneRetirementEpochs = 16;

    struct GpuSceneAllocationRequest
    {
        GpuSceneTableKind table = GpuSceneTableKind::Count;
        u32 count = 0;
    };

    enum class GpuSceneLifetimeFailureCode : u8
    {
        None,
        NotInitialized,
        AlreadyInitialized,
        WrongThread,
        InvalidConfiguration,
        InvalidAllocation,
        InvalidState,
        DuplicateAllocation,
        MismatchedBatch,
        CapacityExceeded,
        TableGrowthFailure,
        MissingRetirementFence,
        RetirementEpochsExhausted,
        LiveAllocationsRemain,
        UnsealedRetirementsRemain
    };

    struct GpuSceneLifetimeFailure
    {
        GpuSceneLifetimeFailureCode code = GpuSceneLifetimeFailureCode::None;
        GpuSceneAllocation allocation;
        const char* message = nullptr;
        GpuSceneTablesFailure tableFailure;
    };

    struct GpuSceneTableLifetimeStats
    {
        u32 allocated = 0;
        u32 active = 0;
        u32 retiring = 0;
        u32 free = 0;
        u32 capacity = 0;
        u32 allocationRanges = 0;
        u32 freeRanges = 0;
        u64 allocations = 0;
        u64 activations = 0;
        u64 cancellations = 0;
        u64 retirements = 0;
        u64 reclaimed = 0;
        u64 failedAllocations = 0;
        u64 staleOperations = 0;
    };

    struct GpuSceneLifetimeStats
    {
        u32 allocated = 0;
        u32 active = 0;
        u32 retiring = 0;
        u32 pendingRetirements = 0;
        u32 sealedRetirements = 0;
        u32 sealedEpochs = 0;
        u32 retirementEpochCapacity = 0;
        u64 allocations = 0;
        u64 retirements = 0;
        u64 reclaimed = 0;
        u64 epochsSealed = 0;
        u64 epochFencePolls = 0;
        u64 epochCapacityStalls = 0;
        u64 rejectedOperations = 0;
        u64 collectionPasses = 0;
    };

    /// Owns CPU identities for elements inside persistent GPU Scene tables. It never owns the RHI buffers;
    /// table growth is delegated to GpuSceneTables and physical resource destruction remains in the RHI.
    class GpuSceneLifetime final
    {
    public:
        struct Impl;

        GpuSceneLifetime() noexcept = default;
        ~GpuSceneLifetime();

        GpuSceneLifetime(const GpuSceneLifetime&) = delete;
        GpuSceneLifetime& operator=(const GpuSceneLifetime&) = delete;

        [[nodiscard]] bool Initialize(GpuSceneTables& tables, const GpuSceneLifetimeConfig& config = {},
                                      GpuSceneLifetimeFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool Shutdown(GpuSceneLifetimeFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;

        /// Materializes GPU pages and corresponding logical metadata ahead of demand. Streaming lookahead
        /// should use this outside the urgent frame-publication path.
        [[nodiscard]] bool ReserveCapacity(GpuSceneTableKind table, u32 requiredElements,
                                           GpuSceneLifetimeFailure* failure = nullptr) noexcept;

        [[nodiscard]] bool Allocate(GpuSceneTableKind table, u32 count, GpuSceneAllocation& allocation,
                                    GpuSceneLifetimeFailure* failure = nullptr) noexcept;

        /// All successful allocations are returned together. If any request fails, earlier allocations
        /// from this call are cancelled before failure is returned.
        [[nodiscard]] bool AllocateBatch(containers::ArraySpan<const GpuSceneAllocationRequest> requests,
                                         containers::ArraySpan<GpuSceneAllocation> allocations,
                                         GpuSceneLifetimeFailure* failure = nullptr) noexcept;

        template<typename T>
        [[nodiscard]] bool Allocate(const u32 count, GpuSceneAllocation& allocation,
                                    GpuSceneLifetimeFailure* const failure = nullptr) noexcept
        {
            static_assert(GpuSceneTableKindOf<T> != GpuSceneTableKind::Count, "type is not a GPU Scene table element");
            return Allocate(GpuSceneTableKindOf<T>, count, allocation, failure);
        }

        /// Commits an allocation only after its complete initial payload has been admitted to the GPU
        /// publication stream. The sparse uploader is the runtime owner of this boundary.
        [[nodiscard]] bool CommitInitialPublication(GpuSceneAllocation allocation,
                                                    GpuSceneLifetimeFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool CommitInitialPublications(
            containers::ArraySpan<const GpuSceneAllocation> allocations,
            GpuSceneLifetimeFailure* failure = nullptr) noexcept;
        /// Cancels an allocation that has never become visible to GPU work. Active allocations must retire.
        [[nodiscard]] bool Cancel(GpuSceneAllocation allocation,
                                  GpuSceneLifetimeFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool CancelBatch(containers::ArraySpan<const GpuSceneAllocation> allocations,
                                       GpuSceneLifetimeFailure* failure = nullptr) noexcept;
        /// Moves an active allocation into the current unsealed retirement epoch.
        [[nodiscard]] bool Retire(GpuSceneAllocation allocation,
                                  GpuSceneLifetimeFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool RetireBatch(containers::ArraySpan<const GpuSceneAllocation> allocations,
                                       GpuSceneLifetimeFailure* failure = nullptr) noexcept;

        /// Seals the current retirement epoch. Every queue selected by retirementQueues must provide its
        /// latest submission fence; an empty or partial fence set cannot publish a non-empty epoch.
        [[nodiscard]] bool SealRetirements(const rhi::ResidencyFenceSet& safeAfter,
                                           GpuSceneLifetimeFailure* failure = nullptr) noexcept;
        /// Non-blocking maintenance; only fence-complete sealed allocations return to their free lists.
        [[nodiscard]] u32 Collect(GpuSceneLifetimeFailure* failure = nullptr) noexcept;

        [[nodiscard]] bool IsValid(GpuSceneAllocation allocation) const noexcept;
        [[nodiscard]] GpuSceneAllocationState State(GpuSceneAllocation allocation) const noexcept;
        [[nodiscard]] GpuSceneTableLifetimeStats GetTableStats(GpuSceneTableKind table) const noexcept;
        [[nodiscard]] GpuSceneLifetimeStats GetStats() const noexcept;

    private:
        Impl* m_impl = nullptr;
    };
} // namespace vanguard::rendering
