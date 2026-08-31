#pragma once

#include <vanguard/crypto/crypto.hpp>
#include <vanguard/rhi/rhi.hpp>

namespace vanguard::rendering
{
    inline constexpr u32 MaximumGeometryRetirementEpochs = 16;
    inline constexpr u32 InvalidGeometryIndex = 0xffffffffu;

    struct VertexArenaSetId
    {
        u32 index = InvalidGeometryIndex;
        u32 generation = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return index != InvalidGeometryIndex && generation != 0;
        }

        [[nodiscard]] friend constexpr bool operator==(const VertexArenaSetId&, const VertexArenaSetId&) noexcept = default;
    };

    struct IndexArenaId
    {
        u32 index = InvalidGeometryIndex;
        u32 generation = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return index != InvalidGeometryIndex && generation != 0;
        }

        [[nodiscard]] friend constexpr bool operator==(const IndexArenaId&, const IndexArenaId&) noexcept = default;
    };

    struct GeometryAllocationId
    {
        u32 index = InvalidGeometryIndex;
        u32 generation = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return index != InvalidGeometryIndex && generation != 0;
        }

        [[nodiscard]] friend constexpr bool operator==(const GeometryAllocationId&, const GeometryAllocationId&) noexcept = default;
    };

    enum class GeometryAllocationState : u8
    {
        Invalid,
        Reserved,
        Active,
        Retiring
    };

    enum class GeometryQueueMask : u8
    {
        None = 0,
        Graphics = 1u << 0u,
        Compute = 1u << 1u,
        Copy = 1u << 2u
    };

    [[nodiscard]] constexpr GeometryQueueMask operator|(const GeometryQueueMask left, const GeometryQueueMask right) noexcept
    {
        return static_cast<GeometryQueueMask>(static_cast<u8>(left) | static_cast<u8>(right));
    }

    [[nodiscard]] constexpr GeometryQueueMask operator&(const GeometryQueueMask left, const GeometryQueueMask right) noexcept
    {
        return static_cast<GeometryQueueMask>(static_cast<u8>(left) & static_cast<u8>(right));
    }

    struct GeometryVertexLayoutDesc
    {
        crypto::Digest256 fingerprint;
        containers::ArraySpan<const rhi::VertexBindingDesc> bindings;
    };

    struct GeometryAllocationRequest
    {
        GeometryVertexLayoutDesc vertexLayout;
        u32 vertexCount = 0;
        rhi::IndexFormat indexFormat = rhi::IndexFormat::UInt16;
        u32 indexCount = 0;
    };

    struct GeometryVertexAllocation
    {
        VertexArenaSetId arena;
        u32 firstVertex = 0;
        u32 vertexCount = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return arena.IsValid() && vertexCount != 0;
        }
    };

    struct GeometryIndexAllocation
    {
        IndexArenaId arena;
        u32 firstIndex = 0;
        u32 indexCount = 0;
        rhi::IndexFormat format = rhi::IndexFormat::UInt16;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return arena.IsValid() && indexCount != 0;
        }

        [[nodiscard]] constexpr u64 ByteOffset() const noexcept
        {
            return static_cast<u64>(firstIndex) * (format == rhi::IndexFormat::UInt32 ? 4u : 2u);
        }
    };

    struct GeometryReservation
    {
        GeometryAllocationId allocation;
        GeometryVertexAllocation vertex;
        GeometryIndexAllocation index;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return allocation.IsValid() && vertex.IsValid() && index.IsValid();
        }
    };

    struct GeometryPlacement
    {
        GeometryAllocationId allocation;
        GeometryVertexAllocation vertex;
        GeometryIndexAllocation index;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return allocation.IsValid() && vertex.IsValid() && index.IsValid();
        }
    };

    struct GeometryVertexArenaView
    {
        VertexArenaSetId id;
        crypto::Digest256 layoutFingerprint;
        rhi::BufferRef buffers[rhi::MaximumVertexBindings];
        rhi::VertexBindingDesc bindings[rhi::MaximumVertexBindings];
        u32 bindingCount = 0;
        u32 vertexCapacity = 0;
        bool dedicated = false;
    };

    struct GeometryIndexArenaView
    {
        IndexArenaId id;
        rhi::BufferRef buffer;
        rhi::IndexFormat format = rhi::IndexFormat::UInt16;
        u32 indexCapacity = 0;
        bool dedicated = false;
    };

    struct GeometryAllocatorConfig
    {
        u32 verticesPerArena = 1u << 18u;
        u64 indexBytesPerArena = 64ull * 1024ull * 1024ull;
        u64 maximumCommittedVertexBytes = 512ull * 1024ull * 1024ull;
        u64 maximumCommittedIndexBytes = 256ull * 1024ull * 1024ull;
        u64 rangeAlignmentBytes = 16;
        u64 maximumIndexArenaBytes = 0xfffffff0ull;
        u32 maximumVertexArenas = 256;
        u32 maximumIndexArenas = 256;
        u32 maximumAllocations = 65'536;
        u32 retirementEpochCount = 8;
        u32 initialRetirementsPerEpoch = 1024;
        GeometryQueueMask retirementQueues = GeometryQueueMask::Graphics | GeometryQueueMask::Compute | GeometryQueueMask::Copy;
    };

    struct GeometryAllocatorStats
    {
        u32 vertexArenas = 0;
        u32 indexArenas = 0;
        u32 dedicatedVertexArenas = 0;
        u32 dedicatedIndexArenas = 0;
        u32 reservedAllocations = 0;
        u32 activeAllocations = 0;
        u32 retiringAllocations = 0;
        u32 sealedEpochs = 0;
        u64 committedVertexBytes = 0;
        u64 committedIndexBytes = 0;
        u64 reservedBytes = 0;
        u64 activeBytes = 0;
        u64 retiringBytes = 0;
        u64 freeVertexBytes = 0;
        u64 freeIndexBytes = 0;
        u64 largestFreeVertexRangeBytes = 0;
        u64 largestFreeIndexRangeBytes = 0;
        u64 allocations = 0;
        u64 cancellations = 0;
        u64 retirements = 0;
        u64 reclaimed = 0;
        u64 staleOperations = 0;
        u64 capacityFailures = 0;
        u64 collectionPasses = 0;
    };

    enum class GeometryAllocatorFailureCode : u8
    {
        None,
        NotInitialized,
        AlreadyInitialized,
        WrongThread,
        InvalidConfiguration,
        InvalidRequest,
        InvalidState,
        CapacityExceeded,
        ArithmeticOverflow,
        RhiFailure,
        MissingRetirementFence,
        RetirementEpochsExhausted,
        LiveAllocationsRemain
    };

    struct GeometryAllocatorFailure
    {
        GeometryAllocatorFailureCode code = GeometryAllocatorFailureCode::None;
        GeometryAllocationId allocation;
        const char* message = nullptr;
        rhi::Failure rhiFailure;
    };

    /// Renderer-owned physical fixed-function geometry storage. Calls are serialized by the renderer command chain;
    /// producer jobs may fill uploader reservations later, but they do not mutate this allocator directly.
    class GeometryAllocator final
    {
    public:
        struct Impl;

        GeometryAllocator() noexcept = default;
        ~GeometryAllocator();

        GeometryAllocator(const GeometryAllocator&) = delete;
        GeometryAllocator& operator=(const GeometryAllocator&) = delete;

        [[nodiscard]] bool Initialize(const GeometryAllocatorConfig& config = {}, GeometryAllocatorFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool Shutdown(GeometryAllocatorFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;

        [[nodiscard]] bool Reserve(const GeometryAllocationRequest& request, GeometryReservation& reservation,
                                   GeometryAllocatorFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool ReserveBatch(containers::ArraySpan<const GeometryAllocationRequest> requests,
                                        containers::ArraySpan<GeometryReservation> reservations,
                                        GeometryAllocatorFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool Cancel(GeometryReservation reservation, GeometryAllocatorFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool CancelBatch(containers::ArraySpan<const GeometryReservation> reservations,
                                       GeometryAllocatorFailure* failure = nullptr) noexcept;

        /// Called only after the corresponding geometry copy submission succeeded.
        [[nodiscard]] bool Commit(GeometryReservation reservation, GeometryPlacement& placement,
                                  GeometryAllocatorFailure* failure = nullptr) noexcept;
        /// Atomically validates and commits a complete submitted upload batch. No reservation changes state if validation fails.
        [[nodiscard]] bool CommitBatch(containers::ArraySpan<const GeometryReservation> reservations,
                                       containers::ArraySpan<GeometryPlacement> placements,
                                       GeometryAllocatorFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool Retire(GeometryPlacement placement, GeometryAllocatorFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool SealRetirements(const rhi::ResidencyFenceSet& safeAfter, GeometryAllocatorFailure* failure = nullptr) noexcept;
        [[nodiscard]] u32 Collect(GeometryAllocatorFailure* failure = nullptr) noexcept;

        [[nodiscard]] GeometryAllocationState GetState(GeometryAllocationId allocation) const noexcept;
        [[nodiscard]] bool ValidateReservation(GeometryReservation reservation) const noexcept;
        [[nodiscard]] bool GetVertexArena(VertexArenaSetId arena, GeometryVertexArenaView& view) const noexcept;
        [[nodiscard]] bool GetIndexArena(IndexArenaId arena, GeometryIndexArenaView& view) const noexcept;
        [[nodiscard]] GeometryAllocatorStats GetStats() const noexcept;

    private:
        Impl* m_impl = nullptr;
    };
} // namespace vanguard::rendering
