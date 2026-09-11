#include <vanguard/rendering/geometry_allocator.hpp>

#include <vanguard/concurrency/thread.hpp>
#include <vanguard/memory/memory.hpp>

#include <new>

namespace vanguard::rendering
{
    namespace
    {
        struct FreeRange
        {
            u32 first = 0;
            u32 count = 0;
        };

        [[nodiscard]] constexpr u32 NextGeneration(const u32 generation) noexcept
        {
            const u32 next = generation + 1u;
            return next != 0 ? next : 1u;
        }

        [[nodiscard]] constexpr u64 GreatestCommonDivisor(u64 left, u64 right) noexcept
        {
            while (right != 0)
            {
                const u64 remainder = left % right;
                left = right;
                right = remainder;
            }
            return left;
        }

        [[nodiscard]] bool AlignUp(const u64 value, const u64 alignment, u64& output) noexcept
        {
            if (alignment == 0)
                return false;
            const u64 remainder = value % alignment;
            if (remainder == 0)
            {
                output = value;
                return true;
            }
            const u64 increment = alignment - remainder;
            if (value > ~u64{0} - increment)
                return false;
            output = value + increment;
            return true;
        }

        [[nodiscard]] bool LeastCommonMultiple(const u64 left, const u64 right, u64& output) noexcept
        {
            if (left == 0 || right == 0)
                return false;
            const u64 reduced = left / GreatestCommonDivisor(left, right);
            if (reduced > ~u64{0} / right)
                return false;
            output = reduced * right;
            return true;
        }

        [[nodiscard]] bool FencesComplete(const rhi::ResidencyFenceSet& fences) noexcept
        {
            return (fences.graphics == 0 || rhi::IsGpuFenceComplete({rhi::QueueType::Graphics, fences.graphics})) &&
                   (fences.compute == 0 || rhi::IsGpuFenceComplete({rhi::QueueType::Compute, fences.compute})) && (fences.copy == 0 || rhi::IsGpuFenceComplete({rhi::QueueType::Copy, fences.copy}));
        }

        [[nodiscard]] constexpr bool HasQueue(const GeometryQueueMask mask, const GeometryQueueMask queue) noexcept
        {
            return (mask & queue) != GeometryQueueMask::None;
        }

        [[nodiscard]] constexpr bool CoversQueues(const rhi::ResidencyFenceSet& fences, const GeometryQueueMask queues) noexcept
        {
            return (!HasQueue(queues, GeometryQueueMask::Graphics) || fences.Covers(rhi::QueueType::Graphics)) && (!HasQueue(queues, GeometryQueueMask::Compute) || fences.Covers(rhi::QueueType::Compute)) &&
                   (!HasQueue(queues, GeometryQueueMask::Copy) || fences.Covers(rhi::QueueType::Copy));
        }

        void ClearFailure(GeometryAllocatorFailure* const failure) noexcept
        {
            if (failure != nullptr)
                *failure = {};
        }

        [[nodiscard]] bool Fail(GeometryAllocatorFailure* const failure, const GeometryAllocatorFailureCode code, const char* const message, const GeometryAllocationId allocation = {},
                                const rhi::Failure& rhiFailure = {}) noexcept
        {
            if (failure != nullptr)
                *failure = {code, allocation, message, rhiFailure};
            return false;
        }

        void AddFreeRange(containers::DynamicArray<FreeRange>& ranges, FreeRange range) noexcept
        {
            if (range.count == 0)
                return;
            u32 insertion = 0;
            while (insertion < ranges.Size() && ranges[insertion].first < range.first)
                ++insertion;
            ranges.Grow(1);
            for (u32 index = ranges.Size() - 1u; index > insertion; --index)
                ranges[index] = ranges[index - 1u];
            ranges[insertion] = range;

            for (u32 index = 0; index + 1u < ranges.Size();)
            {
                FreeRange& left = ranges[index];
                const FreeRange right = ranges[index + 1u];
                const u64 leftEnd = static_cast<u64>(left.first) + left.count;
                if (leftEnd < right.first)
                {
                    ++index;
                    continue;
                }
                const u64 rightEnd = static_cast<u64>(right.first) + right.count;
                const u64 end = leftEnd > rightEnd ? leftEnd : rightEnd;
                left.count = static_cast<u32>(end - left.first);
                ranges.RemoveAt(index + 1u);
            }
        }

        [[nodiscard]] bool TakeBestFit(containers::DynamicArray<FreeRange>& ranges, const u32 count, const u32 alignment, u32& first) noexcept
        {
            u32 best = InvalidGeometryIndex;
            u64 bestWaste = ~u64{0};
            u32 bestFirst = 0;
            for (u32 index = 0; index < ranges.Size(); ++index)
            {
                const FreeRange range = ranges[index];
                u64 aligned = 0;
                if (!AlignUp(range.first, alignment, aligned) || aligned > 0xffffffffu)
                    continue;
                const u64 end = aligned + count;
                const u64 rangeEnd = static_cast<u64>(range.first) + range.count;
                if (end > rangeEnd)
                    continue;
                const u64 waste = range.count - count;
                if (waste < bestWaste || (waste == bestWaste && aligned < bestFirst))
                {
                    best = index;
                    bestWaste = waste;
                    bestFirst = static_cast<u32>(aligned);
                }
            }
            if (best == InvalidGeometryIndex)
                return false;

            const FreeRange selected = ranges[best];
            ranges.RemoveAt(best);
            if (bestFirst > selected.first)
                AddFreeRange(ranges, {selected.first, bestFirst - selected.first});
            const u64 allocationEnd = static_cast<u64>(bestFirst) + count;
            const u64 selectedEnd = static_cast<u64>(selected.first) + selected.count;
            if (allocationEnd < selectedEnd)
                AddFreeRange(ranges, {static_cast<u32>(allocationEnd), static_cast<u32>(selectedEnd - allocationEnd)});
            first = bestFirst;
            return true;
        }
    } // namespace

    struct GeometryAllocator::Impl
    {
        struct VertexArena
        {
            VertexArena() noexcept : freeRanges(memory::pools::Rendering::GetInstance()) {}

            rhi::BufferRef buffers[rhi::MaximumVertexBindings];
            rhi::VertexBindingDesc bindings[rhi::MaximumVertexBindings];
            containers::DynamicArray<FreeRange> freeRanges;
            crypto::Digest256 fingerprint;
            u32 generation = 0;
            u32 bindingCount = 0;
            u32 capacity = 0;
            u32 allocationCount = 0;
            u64 committedBytes = 0;
            bool alive = false;
            bool dedicated = false;
        };

        struct IndexArena
        {
            IndexArena() noexcept : freeRanges(memory::pools::Rendering::GetInstance()) {}

            rhi::BufferRef buffer;
            containers::DynamicArray<FreeRange> freeRanges;
            u32 generation = 0;
            u32 capacity = 0;
            u32 allocationCount = 0;
            u64 committedBytes = 0;
            rhi::IndexFormat format = rhi::IndexFormat::UInt16;
            bool alive = false;
            bool dedicated = false;
        };

        struct AllocationRecord
        {
            GeometryReservation reservation;
            GeometryAllocationState state = GeometryAllocationState::Invalid;
            u64 bytes = 0;
            u32 generation = 0;
        };

        enum class EpochState : u8
        {
            Available,
            Open,
            Sealed
        };

        struct RetirementEpoch
        {
            RetirementEpoch() noexcept : allocations(memory::pools::Rendering::GetInstance()) {}

            containers::DynamicArray<GeometryAllocationId> allocations;
            rhi::ResidencyFenceSet safeAfter;
            EpochState state = EpochState::Available;
        };

        explicit Impl(const GeometryAllocatorConfig& allocatorConfig) noexcept
            : vertexArenas(memory::pools::Rendering::GetInstance()), indexArenas(memory::pools::Rendering::GetInstance()), recycledVertexArenas(memory::pools::Rendering::GetInstance()),
              recycledIndexArenas(memory::pools::Rendering::GetInstance()), allocationRecords(memory::pools::Rendering::GetInstance()), recycledAllocations(memory::pools::Rendering::GetInstance()),
              config(allocatorConfig)
        {
            vertexArenas.Reserve(config.maximumVertexArenas);
            indexArenas.Reserve(config.maximumIndexArenas);
            allocationRecords.Reserve(config.maximumAllocations);
            recycledAllocations.Reserve(config.maximumAllocations);
            for (u32 index = 0; index < config.retirementEpochCount; ++index)
                epochs[index].allocations.Reserve(config.initialRetirementsPerEpoch);
            epochs[0].state = EpochState::Open;
        }

        containers::DynamicArray<VertexArena> vertexArenas;
        containers::DynamicArray<IndexArena> indexArenas;
        containers::DynamicArray<u32> recycledVertexArenas;
        containers::DynamicArray<u32> recycledIndexArenas;
        containers::DynamicArray<AllocationRecord> allocationRecords;
        containers::DynamicArray<u32> recycledAllocations;
        RetirementEpoch epochs[MaximumGeometryRetirementEpochs];
        GeometryAllocatorConfig config;
        GeometryAllocatorStats lifetimeStats;
        u32 openEpoch = 0;
        u32 nextCollectEpoch = 0;
        u64 committedVertexBytes = 0;
        u64 committedIndexBytes = 0;

        [[nodiscard]] static u32 IndexStride(const rhi::IndexFormat format) noexcept
        {
            return format == rhi::IndexFormat::UInt32 ? 4u : 2u;
        }

        [[nodiscard]] bool ValidateLayout(const GeometryVertexLayoutDesc& layout, u32& alignmentVertices, GeometryAllocatorFailure* const failure) noexcept
        {
            if (layout.fingerprint.IsEmpty() || layout.bindings.Empty() || layout.bindings.Size() > rhi::MaximumVertexBindings)
                return Fail(failure, GeometryAllocatorFailureCode::InvalidRequest, "geometry vertex layout is empty or exceeds fixed-function bindings");
            u64 quantum = 1;
            for (u32 index = 0; index < layout.bindings.Size(); ++index)
            {
                const rhi::VertexBindingDesc binding = layout.bindings[index];
                if (binding.binding != index || binding.stride == 0 || binding.inputRate != rhi::VertexInputRate::PerVertex)
                    return Fail(failure, GeometryAllocatorFailureCode::InvalidRequest, "geometry vertex bindings must be contiguous nonzero per-vertex streams");
                const u64 bindingQuantum = config.rangeAlignmentBytes / GreatestCommonDivisor(config.rangeAlignmentBytes, binding.stride);
                if (!LeastCommonMultiple(quantum, bindingQuantum, quantum) || quantum > 0xffffffffu)
                    return Fail(failure, GeometryAllocatorFailureCode::ArithmeticOverflow, "geometry vertex alignment quantum overflowed");
            }
            alignmentVertices = static_cast<u32>(quantum);
            return true;
        }

        [[nodiscard]] static bool SameLayout(const VertexArena& arena, const GeometryVertexLayoutDesc& layout) noexcept
        {
            if (!arena.alive || arena.dedicated || arena.fingerprint != layout.fingerprint || arena.bindingCount != layout.bindings.Size())
                return false;
            for (u32 index = 0; index < arena.bindingCount; ++index)
                if (arena.bindings[index].binding != layout.bindings[index].binding || arena.bindings[index].stride != layout.bindings[index].stride ||
                    arena.bindings[index].inputRate != layout.bindings[index].inputRate || arena.bindings[index].instanceStepRate != layout.bindings[index].instanceStepRate)
                    return false;
            return true;
        }

        [[nodiscard]] bool CreateVertexArena(const GeometryVertexLayoutDesc& layout, const u32 minimumCount, const u32 alignmentVertices, u32& arenaIndex, GeometryAllocatorFailure* const failure) noexcept
        {
            if (recycledVertexArenas.Empty() && vertexArenas.Size() >= config.maximumVertexArenas)
            {
                ++lifetimeStats.capacityFailures;
                return Fail(failure, GeometryAllocatorFailureCode::CapacityExceeded, "geometry vertex arena limit is exhausted");
            }
            const bool dedicated = minimumCount > config.verticesPerArena;
            u64 capacity64 = dedicated ? minimumCount : config.verticesPerArena;
            if (!AlignUp(capacity64, alignmentVertices, capacity64) || capacity64 > 0xffffffffu)
                return Fail(failure, GeometryAllocatorFailureCode::ArithmeticOverflow, "geometry vertex arena capacity overflowed");

            u64 committedBytes = 0;
            for (const rhi::VertexBindingDesc binding : layout.bindings)
            {
                if (capacity64 > ~u64{0} / binding.stride || committedBytes > ~u64{0} - capacity64 * binding.stride)
                    return Fail(failure, GeometryAllocatorFailureCode::ArithmeticOverflow, "geometry vertex arena byte size overflowed");
                committedBytes += capacity64 * binding.stride;
            }
            if (committedBytes > config.maximumCommittedVertexBytes || committedVertexBytes > config.maximumCommittedVertexBytes - committedBytes)
            {
                ++lifetimeStats.capacityFailures;
                return Fail(failure, GeometryAllocatorFailureCode::CapacityExceeded, "geometry vertex memory budget is exhausted");
            }

            if (!recycledVertexArenas.Empty())
                arenaIndex = recycledVertexArenas.PopBack();
            else
            {
                arenaIndex = vertexArenas.Size();
                vertexArenas.Grow(1);
            }
            VertexArena& arena = vertexArenas[arenaIndex];
            arena.generation = NextGeneration(arena.generation);
            arena.fingerprint = layout.fingerprint;
            arena.bindingCount = layout.bindings.Size();
            arena.capacity = static_cast<u32>(capacity64);
            arena.dedicated = dedicated;
            arena.alive = true;
            arena.committedBytes = committedBytes;
            committedVertexBytes += committedBytes;
            arena.freeRanges.PushBack({0, arena.capacity});

            for (u32 index = 0; index < arena.bindingCount; ++index)
            {
                arena.bindings[index] = layout.bindings[index];
                const u64 stride = arena.bindings[index].stride;
                if (capacity64 > ~u64{0} / stride)
                {
                    RecycleVertexArena(arenaIndex);
                    return Fail(failure, GeometryAllocatorFailureCode::ArithmeticOverflow, "geometry vertex buffer size overflowed");
                }
                rhi::BufferDesc desc;
                desc.size = capacity64 * stride;
                desc.usage = rhi::BufferUsage::Vertex | rhi::BufferUsage::CopyDestination;
                desc.initialState = rhi::ResourceState::VertexBuffer;
                rhi::Failure rhiFailure;
                arena.buffers[index] = rhi::CreateBuffer(desc, {}, &rhiFailure);
                if (!arena.buffers[index])
                {
                    RecycleVertexArena(arenaIndex);
                    return Fail(failure, GeometryAllocatorFailureCode::RhiFailure, "geometry vertex arena buffer creation failed", {}, rhiFailure);
                }
                rhi::SetResourceDebugName(arena.buffers[index], "Geometry Vertex Arena");
            }
            return true;
        }

        void DestroyVertexArena(VertexArena& arena) noexcept
        {
            for (u32 index = 0; index < arena.bindingCount; ++index)
                static_cast<void>(rhi::SafeRelease(arena.buffers[index]));
            arena.freeRanges.Clear();
            arena.bindingCount = 0;
            arena.capacity = 0;
            arena.allocationCount = 0;
            committedVertexBytes -= arena.committedBytes;
            arena.committedBytes = 0;
            arena.alive = false;
            arena.dedicated = false;
            arena.fingerprint = {};
        }

        void RecycleVertexArena(const u32 arenaIndex) noexcept
        {
            DestroyVertexArena(vertexArenas[arenaIndex]);
            recycledVertexArenas.PushBack(arenaIndex);
        }

        [[nodiscard]] bool CreateIndexArena(const rhi::IndexFormat format, const u32 minimumCount, const u32 alignmentIndices, u32& arenaIndex, GeometryAllocatorFailure* const failure) noexcept
        {
            if (recycledIndexArenas.Empty() && indexArenas.Size() >= config.maximumIndexArenas)
            {
                ++lifetimeStats.capacityFailures;
                return Fail(failure, GeometryAllocatorFailureCode::CapacityExceeded, "geometry index arena limit is exhausted");
            }
            const u32 stride = IndexStride(format);
            const u64 normalCapacity = config.indexBytesPerArena / stride;
            const bool dedicated = minimumCount > normalCapacity;
            u64 capacity64 = dedicated ? minimumCount : normalCapacity;
            if (!AlignUp(capacity64, alignmentIndices, capacity64) || capacity64 > 0xffffffffu || capacity64 > config.maximumIndexArenaBytes / stride)
            {
                ++lifetimeStats.capacityFailures;
                return Fail(failure, GeometryAllocatorFailureCode::CapacityExceeded, "geometry index arena exceeds its fixed-function binding limit");
            }
            const u64 committedBytes = capacity64 * stride;
            if (committedBytes > config.maximumCommittedIndexBytes || committedIndexBytes > config.maximumCommittedIndexBytes - committedBytes)
            {
                ++lifetimeStats.capacityFailures;
                return Fail(failure, GeometryAllocatorFailureCode::CapacityExceeded, "geometry index memory budget is exhausted");
            }

            if (!recycledIndexArenas.Empty())
                arenaIndex = recycledIndexArenas.PopBack();
            else
            {
                arenaIndex = indexArenas.Size();
                indexArenas.Grow(1);
            }
            IndexArena& arena = indexArenas[arenaIndex];
            arena.generation = NextGeneration(arena.generation);
            arena.capacity = static_cast<u32>(capacity64);
            arena.format = format;
            arena.dedicated = dedicated;
            arena.alive = true;
            arena.committedBytes = committedBytes;
            committedIndexBytes += committedBytes;
            arena.freeRanges.PushBack({0, arena.capacity});

            rhi::BufferDesc desc;
            desc.size = capacity64 * stride;
            desc.usage = rhi::BufferUsage::Index | rhi::BufferUsage::CopyDestination;
            desc.initialState = rhi::ResourceState::IndexBuffer;
            rhi::Failure rhiFailure;
            arena.buffer = rhi::CreateBuffer(desc, {}, &rhiFailure);
            if (!arena.buffer)
            {
                RecycleIndexArena(arenaIndex);
                return Fail(failure, GeometryAllocatorFailureCode::RhiFailure, "geometry index arena buffer creation failed", {}, rhiFailure);
            }
            rhi::SetResourceDebugName(arena.buffer, "Geometry Index Arena");
            return true;
        }

        void DestroyIndexArena(IndexArena& arena) noexcept
        {
            static_cast<void>(rhi::SafeRelease(arena.buffer));
            arena.freeRanges.Clear();
            arena.capacity = 0;
            arena.allocationCount = 0;
            committedIndexBytes -= arena.committedBytes;
            arena.committedBytes = 0;
            arena.alive = false;
            arena.dedicated = false;
        }

        void RecycleIndexArena(const u32 arenaIndex) noexcept
        {
            DestroyIndexArena(indexArenas[arenaIndex]);
            recycledIndexArenas.PushBack(arenaIndex);
        }

        [[nodiscard]] bool ReserveVertex(const GeometryAllocationRequest& request, GeometryVertexAllocation& allocation, GeometryAllocatorFailure* const failure) noexcept
        {
            u32 alignment = 0;
            if (!ValidateLayout(request.vertexLayout, alignment, failure))
                return false;
            for (u32 index = 0; index < vertexArenas.Size(); ++index)
            {
                VertexArena& arena = vertexArenas[index];
                if (!SameLayout(arena, request.vertexLayout))
                    continue;
                u32 first = 0;
                if (TakeBestFit(arena.freeRanges, request.vertexCount, alignment, first))
                {
                    ++arena.allocationCount;
                    allocation = {{index, arena.generation}, first, request.vertexCount};
                    return true;
                }
            }
            u32 arenaIndex = 0;
            if (!CreateVertexArena(request.vertexLayout, request.vertexCount, alignment, arenaIndex, failure))
                return false;
            VertexArena& arena = vertexArenas[arenaIndex];
            u32 first = 0;
            if (!TakeBestFit(arena.freeRanges, request.vertexCount, alignment, first))
                return Fail(failure, GeometryAllocatorFailureCode::InvalidState, "created geometry vertex arena could not satisfy its request");
            ++arena.allocationCount;
            allocation = {{arenaIndex, arena.generation}, first, request.vertexCount};
            return true;
        }

        [[nodiscard]] bool ReserveIndex(const GeometryAllocationRequest& request, GeometryIndexAllocation& allocation, GeometryAllocatorFailure* const failure) noexcept
        {
            const u32 stride = IndexStride(request.indexFormat);
            const u64 quantum64 = config.rangeAlignmentBytes / GreatestCommonDivisor(config.rangeAlignmentBytes, stride);
            if (quantum64 == 0 || quantum64 > 0xffffffffu)
                return Fail(failure, GeometryAllocatorFailureCode::ArithmeticOverflow, "geometry index alignment quantum overflowed");
            const u32 alignment = static_cast<u32>(quantum64);
            for (u32 index = 0; index < indexArenas.Size(); ++index)
            {
                IndexArena& arena = indexArenas[index];
                if (!arena.alive || arena.dedicated || arena.format != request.indexFormat)
                    continue;
                u32 first = 0;
                if (TakeBestFit(arena.freeRanges, request.indexCount, alignment, first))
                {
                    ++arena.allocationCount;
                    allocation = {{index, arena.generation}, first, request.indexCount, request.indexFormat};
                    return true;
                }
            }
            u32 arenaIndex = 0;
            if (!CreateIndexArena(request.indexFormat, request.indexCount, alignment, arenaIndex, failure))
                return false;
            IndexArena& arena = indexArenas[arenaIndex];
            u32 first = 0;
            if (!TakeBestFit(arena.freeRanges, request.indexCount, alignment, first))
                return Fail(failure, GeometryAllocatorFailureCode::InvalidState, "created geometry index arena could not satisfy its request");
            ++arena.allocationCount;
            allocation = {{arenaIndex, arena.generation}, first, request.indexCount, request.indexFormat};
            return true;
        }

        [[nodiscard]] AllocationRecord* FindRecord(const GeometryAllocationId allocation) noexcept
        {
            if (!allocation.IsValid() || allocation.index >= allocationRecords.Size())
                return nullptr;
            AllocationRecord& record = allocationRecords[allocation.index];
            return record.reservation.allocation == allocation && record.state != GeometryAllocationState::Invalid ? &record : nullptr;
        }

        [[nodiscard]] const AllocationRecord* FindRecord(const GeometryAllocationId allocation) const noexcept
        {
            return const_cast<Impl*>(this)->FindRecord(allocation);
        }

        [[nodiscard]] static bool IsExactReservation(const AllocationRecord& record, const GeometryReservation reservation) noexcept
        {
            return record.reservation.allocation == reservation.allocation && record.reservation.vertex.arena == reservation.vertex.arena &&
                   record.reservation.vertex.firstVertex == reservation.vertex.firstVertex && record.reservation.vertex.vertexCount == reservation.vertex.vertexCount &&
                   record.reservation.index.arena == reservation.index.arena && record.reservation.index.firstIndex == reservation.index.firstIndex &&
                   record.reservation.index.indexCount == reservation.index.indexCount && record.reservation.index.format == reservation.index.format;
        }

        [[nodiscard]] static bool IsExactPlacement(const AllocationRecord& record, const GeometryPlacement placement) noexcept
        {
            return IsExactReservation(record, {placement.allocation, placement.vertex, placement.index});
        }

        [[nodiscard]] u64 AllocationBytes(const GeometryVertexAllocation vertex, const GeometryIndexAllocation index) const noexcept
        {
            const VertexArena& vertexArena = vertexArenas[vertex.arena.index];
            u64 bytes = static_cast<u64>(index.indexCount) * IndexStride(index.format);
            for (u32 binding = 0; binding < vertexArena.bindingCount; ++binding)
                bytes += static_cast<u64>(vertex.vertexCount) * vertexArena.bindings[binding].stride;
            return bytes;
        }

        void ReleaseRanges(const GeometryReservation reservation) noexcept
        {
            VertexArena& vertex = vertexArenas[reservation.vertex.arena.index];
            IndexArena& index = indexArenas[reservation.index.arena.index];
            AddFreeRange(vertex.freeRanges, {reservation.vertex.firstVertex, reservation.vertex.vertexCount});
            AddFreeRange(index.freeRanges, {reservation.index.firstIndex, reservation.index.indexCount});
            if (vertex.allocationCount != 0)
                --vertex.allocationCount;
            if (index.allocationCount != 0)
                --index.allocationCount;
            if (vertex.allocationCount == 0)
                RecycleVertexArena(reservation.vertex.arena.index);
            if (index.allocationCount == 0)
                RecycleIndexArena(reservation.index.arena.index);
        }

        void RecycleRecord(AllocationRecord& record) noexcept
        {
            const u32 index = record.reservation.allocation.index;
            record.reservation = {};
            record.state = GeometryAllocationState::Invalid;
            record.bytes = 0;
            recycledAllocations.PushBack(index);
        }
    };

    GeometryAllocator::~GeometryAllocator()
    {
        if (m_impl != nullptr)
            static_cast<void>(Shutdown());
    }

    bool GeometryAllocator::Initialize(const GeometryAllocatorConfig& config, GeometryAllocatorFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl != nullptr)
            return Fail(failure, GeometryAllocatorFailureCode::AlreadyInitialized, "geometry allocator is already initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, GeometryAllocatorFailureCode::WrongThread, "geometry allocator must initialize on the main thread");
        if (!rhi::IsInitialized() || config.verticesPerArena == 0 || config.indexBytesPerArena < 4 || config.maximumCommittedVertexBytes == 0 || config.maximumCommittedIndexBytes < config.indexBytesPerArena ||
            config.rangeAlignmentBytes == 0 || config.maximumIndexArenaBytes < config.indexBytesPerArena || config.maximumIndexArenaBytes > 0xffffffffu || config.maximumVertexArenas == 0 ||
            config.maximumIndexArenas == 0 || config.maximumAllocations == 0 || config.retirementEpochCount < 2 || config.retirementEpochCount > MaximumGeometryRetirementEpochs ||
            config.initialRetirementsPerEpoch == 0 || config.retirementQueues == GeometryQueueMask::None)
            return Fail(failure, GeometryAllocatorFailureCode::InvalidConfiguration, "geometry allocator configuration is invalid");

        memory::MemoryBlock block = memory::Allocate(memory::PoolId::Rendering, sizeof(Impl), alignof(Impl));
        if (!block)
            return Fail(failure, GeometryAllocatorFailureCode::InvalidConfiguration, "geometry allocator metadata allocation failed");
        m_impl = new (block.address) Impl(config);
        return true;
    }

    bool GeometryAllocator::Shutdown(GeometryAllocatorFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return true;
        if (!concurrency::IsMainThread())
            return Fail(failure, GeometryAllocatorFailureCode::WrongThread, "geometry allocator must shutdown on the main thread");
        for (const Impl::AllocationRecord& record : m_impl->allocationRecords)
            if (record.state != GeometryAllocationState::Invalid)
                return Fail(failure, GeometryAllocatorFailureCode::LiveAllocationsRemain, "geometry allocator still owns live allocations", record.reservation.allocation);
        for (u32 index = 0; index < m_impl->config.retirementEpochCount; ++index)
            if (!m_impl->epochs[index].allocations.Empty())
                return Fail(failure, GeometryAllocatorFailureCode::LiveAllocationsRemain, "geometry allocator still owns retirement records");

        Impl* const impl = m_impl;
        for (Impl::VertexArena& arena : impl->vertexArenas)
            if (arena.alive)
                impl->DestroyVertexArena(arena);
        for (Impl::IndexArena& arena : impl->indexArenas)
            if (arena.alive)
                impl->DestroyIndexArena(arena);
        m_impl = nullptr;
        impl->~Impl();
        memory::MemoryBlock block{impl, sizeof(Impl), memory::PoolId::Rendering};
        memory::Free(block);
        return true;
    }

    void GeometryAllocator::AbandonDevice() noexcept
    {
        if (m_impl == nullptr)
            return;
        Impl* const impl = m_impl;
        for (Impl::VertexArena& arena : impl->vertexArenas)
            if (arena.alive)
                impl->DestroyVertexArena(arena);
        for (Impl::IndexArena& arena : impl->indexArenas)
            if (arena.alive)
                impl->DestroyIndexArena(arena);
        m_impl = nullptr;
        impl->~Impl();
        memory::MemoryBlock block{impl, sizeof(Impl), memory::PoolId::Rendering};
        memory::Free(block);
    }

    bool GeometryAllocator::IsInitialized() const noexcept
    {
        return m_impl != nullptr;
    }

    bool GeometryAllocator::Reserve(const GeometryAllocationRequest& request, GeometryReservation& reservation, GeometryAllocatorFailure* const failure) noexcept
    {
        ClearFailure(failure);
        reservation = {};
        if (m_impl == nullptr)
            return Fail(failure, GeometryAllocatorFailureCode::NotInitialized, "geometry allocator is not initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, GeometryAllocatorFailureCode::WrongThread, "geometry reservations must run on the main thread");
        if (request.vertexCount == 0 || request.indexCount == 0 || request.indexFormat > rhi::IndexFormat::UInt32)
            return Fail(failure, GeometryAllocatorFailureCode::InvalidRequest, "geometry allocation request is invalid");
        if (m_impl->recycledAllocations.Empty() && m_impl->allocationRecords.Size() >= m_impl->config.maximumAllocations)
        {
            ++m_impl->lifetimeStats.capacityFailures;
            return Fail(failure, GeometryAllocatorFailureCode::CapacityExceeded, "geometry allocation-record capacity is exhausted");
        }

        GeometryVertexAllocation vertex;
        if (!m_impl->ReserveVertex(request, vertex, failure))
            return false;
        GeometryIndexAllocation index;
        if (!m_impl->ReserveIndex(request, index, failure))
        {
            Impl::VertexArena& arena = m_impl->vertexArenas[vertex.arena.index];
            AddFreeRange(arena.freeRanges, {vertex.firstVertex, vertex.vertexCount});
            if (arena.allocationCount != 0)
                --arena.allocationCount;
            if (arena.allocationCount == 0)
                m_impl->RecycleVertexArena(vertex.arena.index);
            return false;
        }

        u32 recordIndex = 0;
        if (!m_impl->recycledAllocations.Empty())
        {
            recordIndex = m_impl->recycledAllocations.Back();
            m_impl->recycledAllocations.PopBack();
        }
        else
        {
            recordIndex = m_impl->allocationRecords.Size();
            m_impl->allocationRecords.Grow(1);
        }
        Impl::AllocationRecord& record = m_impl->allocationRecords[recordIndex];
        record.generation = NextGeneration(record.generation);
        const u32 generation = record.generation;
        reservation = {{recordIndex, generation}, vertex, index};
        record.reservation = reservation;
        record.state = GeometryAllocationState::Reserved;
        record.bytes = m_impl->AllocationBytes(vertex, index);
        ++m_impl->lifetimeStats.allocations;
        return true;
    }

    bool GeometryAllocator::ReserveBatch(const containers::ArraySpan<const GeometryAllocationRequest> requests, containers::ArraySpan<GeometryReservation> reservations,
                                         GeometryAllocatorFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, GeometryAllocatorFailureCode::NotInitialized, "geometry allocator is not initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, GeometryAllocatorFailureCode::WrongThread, "geometry reservation batches must run on the main thread");
        if (requests.Empty() || requests.Size() != reservations.Size())
            return Fail(failure, GeometryAllocatorFailureCode::InvalidRequest, "geometry allocation batch does not match its outputs");
        for (GeometryReservation& reservation : reservations)
            reservation = {};
        for (u32 index = 0; index < requests.Size(); ++index)
        {
            if (Reserve(requests[index], reservations[index], failure))
                continue;
            for (u32 rollback = 0; rollback < index; ++rollback)
                static_cast<void>(Cancel(reservations[rollback]));
            for (GeometryReservation& reservation : reservations)
                reservation = {};
            return false;
        }
        return true;
    }

    bool GeometryAllocator::Cancel(const GeometryReservation reservation, GeometryAllocatorFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, GeometryAllocatorFailureCode::NotInitialized, "geometry allocator is not initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, GeometryAllocatorFailureCode::WrongThread, "geometry reservation cancellation must run on the main thread");
        Impl::AllocationRecord* const record = m_impl->FindRecord(reservation.allocation);
        if (record == nullptr || record->state != GeometryAllocationState::Reserved || !Impl::IsExactReservation(*record, reservation))
        {
            ++m_impl->lifetimeStats.staleOperations;
            return Fail(failure, GeometryAllocatorFailureCode::InvalidState, "only the exact reserved geometry allocation can be cancelled", reservation.allocation);
        }
        m_impl->ReleaseRanges(record->reservation);
        m_impl->RecycleRecord(*record);
        ++m_impl->lifetimeStats.cancellations;
        return true;
    }

    bool GeometryAllocator::CancelBatch(const containers::ArraySpan<const GeometryReservation> reservations, GeometryAllocatorFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (reservations.Empty())
            return Fail(failure, GeometryAllocatorFailureCode::InvalidRequest, "geometry cancellation batch is empty");
        if (m_impl == nullptr)
            return Fail(failure, GeometryAllocatorFailureCode::NotInitialized, "geometry allocator is not initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, GeometryAllocatorFailureCode::WrongThread, "geometry reservation batch cancellation must run on the main thread");
        for (const GeometryReservation reservation : reservations)
        {
            const Impl::AllocationRecord* const record = m_impl->FindRecord(reservation.allocation);
            if (record == nullptr || record->state != GeometryAllocationState::Reserved || !Impl::IsExactReservation(*record, reservation))
                return Fail(failure, GeometryAllocatorFailureCode::InvalidState, "geometry cancellation batch contains a non-reserved allocation", reservation.allocation);
        }
        for (const GeometryReservation reservation : reservations)
            if (!Cancel(reservation, failure))
                return false;
        return true;
    }

    bool GeometryAllocator::Commit(const GeometryReservation reservation, GeometryPlacement& placement, GeometryAllocatorFailure* const failure) noexcept
    {
        ClearFailure(failure);
        placement = {};
        if (m_impl == nullptr)
            return Fail(failure, GeometryAllocatorFailureCode::NotInitialized, "geometry allocator is not initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, GeometryAllocatorFailureCode::WrongThread, "geometry commits must run on the main thread");
        Impl::AllocationRecord* const record = m_impl->FindRecord(reservation.allocation);
        if (record == nullptr || record->state != GeometryAllocationState::Reserved || !Impl::IsExactReservation(*record, reservation))
        {
            ++m_impl->lifetimeStats.staleOperations;
            return Fail(failure, GeometryAllocatorFailureCode::InvalidState, "only the exact reserved geometry allocation can be committed", reservation.allocation);
        }
        record->state = GeometryAllocationState::Active;
        placement = {reservation.allocation, reservation.vertex, reservation.index};
        return true;
    }

    bool GeometryAllocator::CommitBatch(const containers::ArraySpan<const GeometryReservation> reservations, containers::ArraySpan<GeometryPlacement> placements,
                                        GeometryAllocatorFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, GeometryAllocatorFailureCode::NotInitialized, "geometry allocator is not initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, GeometryAllocatorFailureCode::WrongThread, "geometry commit batches must run on the main thread");
        if (reservations.Empty() || reservations.Size() != placements.Size())
            return Fail(failure, GeometryAllocatorFailureCode::InvalidRequest, "geometry commit batch does not match its outputs");
        for (GeometryPlacement& placement : placements)
            placement = {};
        for (u32 index = 0; index < reservations.Size(); ++index)
        {
            const GeometryReservation reservation = reservations[index];
            const Impl::AllocationRecord* const record = m_impl->FindRecord(reservation.allocation);
            if (record == nullptr || record->state != GeometryAllocationState::Reserved || !Impl::IsExactReservation(*record, reservation))
                return Fail(failure, GeometryAllocatorFailureCode::InvalidState, "geometry commit batch contains a stale reservation", reservation.allocation);
            for (u32 previous = 0; previous < index; ++previous)
                if (reservations[previous].allocation == reservation.allocation)
                    return Fail(failure, GeometryAllocatorFailureCode::InvalidRequest, "geometry commit batch contains a duplicate reservation", reservation.allocation);
        }
        for (u32 index = 0; index < reservations.Size(); ++index)
        {
            Impl::AllocationRecord& record = *m_impl->FindRecord(reservations[index].allocation);
            record.state = GeometryAllocationState::Active;
            placements[index] = {reservations[index].allocation, reservations[index].vertex, reservations[index].index};
        }
        return true;
    }

    bool GeometryAllocator::Retire(const GeometryPlacement placement, GeometryAllocatorFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, GeometryAllocatorFailureCode::NotInitialized, "geometry allocator is not initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, GeometryAllocatorFailureCode::WrongThread, "geometry retirement must run on the main thread");
        Impl::AllocationRecord* const record = m_impl->FindRecord(placement.allocation);
        if (record == nullptr || record->state != GeometryAllocationState::Active || !Impl::IsExactPlacement(*record, placement))
        {
            ++m_impl->lifetimeStats.staleOperations;
            return Fail(failure, GeometryAllocatorFailureCode::InvalidState, "only the exact active geometry placement can retire", placement.allocation);
        }
        Impl::RetirementEpoch& epoch = m_impl->epochs[m_impl->openEpoch];
        epoch.allocations.PushBack(placement.allocation);
        record->state = GeometryAllocationState::Retiring;
        ++m_impl->lifetimeStats.retirements;
        return true;
    }

    bool GeometryAllocator::SealRetirements(const rhi::ResidencyFenceSet& safeAfter, GeometryAllocatorFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, GeometryAllocatorFailureCode::NotInitialized, "geometry allocator is not initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, GeometryAllocatorFailureCode::WrongThread, "geometry retirement sealing must run on the main thread");
        Impl::RetirementEpoch& epoch = m_impl->epochs[m_impl->openEpoch];
        if (epoch.allocations.Empty())
            return true;
        if (!CoversQueues(safeAfter, m_impl->config.retirementQueues))
            return Fail(failure, GeometryAllocatorFailureCode::MissingRetirementFence, "geometry retirement epoch does not cover every configured queue");
        const u32 next = (m_impl->openEpoch + 1u) % m_impl->config.retirementEpochCount;
        if (m_impl->epochs[next].state != Impl::EpochState::Available)
            return Fail(failure, GeometryAllocatorFailureCode::RetirementEpochsExhausted, "geometry retirement epoch ring is full");
        epoch.safeAfter = safeAfter;
        epoch.state = Impl::EpochState::Sealed;
        m_impl->openEpoch = next;
        m_impl->epochs[next].state = Impl::EpochState::Open;
        return true;
    }

    u32 GeometryAllocator::Collect(GeometryAllocatorFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
        {
            static_cast<void>(Fail(failure, GeometryAllocatorFailureCode::NotInitialized, "geometry allocator is not initialized"));
            return 0;
        }
        if (!concurrency::IsMainThread())
        {
            static_cast<void>(Fail(failure, GeometryAllocatorFailureCode::WrongThread, "geometry retirement collection must run on the main thread"));
            return 0;
        }
        ++m_impl->lifetimeStats.collectionPasses;
        u32 reclaimed = 0;
        for (u32 visited = 0; visited < m_impl->config.retirementEpochCount; ++visited)
        {
            Impl::RetirementEpoch& epoch = m_impl->epochs[m_impl->nextCollectEpoch];
            if (epoch.state != Impl::EpochState::Sealed || !FencesComplete(epoch.safeAfter))
                break;
            for (const GeometryAllocationId allocation : epoch.allocations)
            {
                Impl::AllocationRecord* const record = m_impl->FindRecord(allocation);
                if (record == nullptr || record->state != GeometryAllocationState::Retiring)
                {
                    static_cast<void>(Fail(failure, GeometryAllocatorFailureCode::InvalidState, "geometry retirement epoch contains a stale allocation", allocation));
                    return reclaimed;
                }
            }
            for (const GeometryAllocationId allocation : epoch.allocations)
            {
                Impl::AllocationRecord& record = *m_impl->FindRecord(allocation);
                m_impl->ReleaseRanges(record.reservation);
                m_impl->RecycleRecord(record);
                ++m_impl->lifetimeStats.reclaimed;
                ++reclaimed;
            }
            epoch.allocations.Clear();
            epoch.safeAfter = {};
            epoch.state = Impl::EpochState::Available;
            m_impl->nextCollectEpoch = (m_impl->nextCollectEpoch + 1u) % m_impl->config.retirementEpochCount;
        }
        return reclaimed;
    }

    GeometryAllocationState GeometryAllocator::GetState(const GeometryAllocationId allocation) const noexcept
    {
        if (m_impl == nullptr)
            return GeometryAllocationState::Invalid;
        const Impl::AllocationRecord* const record = m_impl->FindRecord(allocation);
        return record != nullptr ? record->state : GeometryAllocationState::Invalid;
    }

    bool GeometryAllocator::ValidateReservation(const GeometryReservation reservation) const noexcept
    {
        if (m_impl == nullptr)
            return false;
        const Impl::AllocationRecord* const record = m_impl->FindRecord(reservation.allocation);
        return record != nullptr && record->state == GeometryAllocationState::Reserved && Impl::IsExactReservation(*record, reservation);
    }

    bool GeometryAllocator::GetVertexArena(const VertexArenaSetId id, GeometryVertexArenaView& view) const noexcept
    {
        view = {};
        if (m_impl == nullptr || !id.IsValid() || id.index >= m_impl->vertexArenas.Size())
            return false;
        const Impl::VertexArena& arena = m_impl->vertexArenas[id.index];
        if (!arena.alive || arena.generation != id.generation)
            return false;
        view.id = id;
        view.layoutFingerprint = arena.fingerprint;
        view.bindingCount = arena.bindingCount;
        view.vertexCapacity = arena.capacity;
        view.dedicated = arena.dedicated;
        for (u32 index = 0; index < arena.bindingCount; ++index)
        {
            view.buffers[index] = arena.buffers[index];
            view.bindings[index] = arena.bindings[index];
        }
        return true;
    }

    bool GeometryAllocator::GetIndexArena(const IndexArenaId id, GeometryIndexArenaView& view) const noexcept
    {
        view = {};
        if (m_impl == nullptr || !id.IsValid() || id.index >= m_impl->indexArenas.Size())
            return false;
        const Impl::IndexArena& arena = m_impl->indexArenas[id.index];
        if (!arena.alive || arena.generation != id.generation)
            return false;
        view = {id, arena.buffer, arena.format, arena.capacity, arena.dedicated};
        return true;
    }

    GeometryAllocatorStats GeometryAllocator::GetStats() const noexcept
    {
        if (m_impl == nullptr)
            return {};
        GeometryAllocatorStats stats = m_impl->lifetimeStats;
        for (const Impl::VertexArena& arena : m_impl->vertexArenas)
        {
            if (!arena.alive)
                continue;
            ++stats.vertexArenas;
            stats.dedicatedVertexArenas += arena.dedicated ? 1u : 0u;
            u64 strideSum = 0;
            for (u32 binding = 0; binding < arena.bindingCount; ++binding)
                strideSum += arena.bindings[binding].stride;
            stats.committedVertexBytes += static_cast<u64>(arena.capacity) * strideSum;
            for (const FreeRange range : arena.freeRanges)
            {
                const u64 bytes = static_cast<u64>(range.count) * strideSum;
                stats.freeVertexBytes += bytes;
                if (stats.largestFreeVertexRangeBytes < bytes)
                    stats.largestFreeVertexRangeBytes = bytes;
            }
        }
        for (const Impl::IndexArena& arena : m_impl->indexArenas)
        {
            if (!arena.alive)
                continue;
            ++stats.indexArenas;
            stats.dedicatedIndexArenas += arena.dedicated ? 1u : 0u;
            const u64 stride = Impl::IndexStride(arena.format);
            stats.committedIndexBytes += static_cast<u64>(arena.capacity) * stride;
            for (const FreeRange range : arena.freeRanges)
            {
                const u64 bytes = static_cast<u64>(range.count) * stride;
                stats.freeIndexBytes += bytes;
                if (stats.largestFreeIndexRangeBytes < bytes)
                    stats.largestFreeIndexRangeBytes = bytes;
            }
        }
        for (const Impl::AllocationRecord& record : m_impl->allocationRecords)
        {
            switch (record.state)
            {
            case GeometryAllocationState::Reserved:
                ++stats.reservedAllocations;
                stats.reservedBytes += record.bytes;
                break;
            case GeometryAllocationState::Active:
                ++stats.activeAllocations;
                stats.activeBytes += record.bytes;
                break;
            case GeometryAllocationState::Retiring:
                ++stats.retiringAllocations;
                stats.retiringBytes += record.bytes;
                break;
            default:
                break;
            }
        }
        for (u32 index = 0; index < m_impl->config.retirementEpochCount; ++index)
            stats.sealedEpochs += m_impl->epochs[index].state == Impl::EpochState::Sealed ? 1u : 0u;
        return stats;
    }
} // namespace vanguard::rendering
