#include <vanguard/rendering/render_flow_resource_placed.hpp>

#include <vanguard/memory/memory.hpp>
#include <vanguard/rendering/render_flow_resource_internal.hpp>
#include <vanguard/system/assert.hpp>

#include <algorithm>
#include <new>

namespace vanguard::rendering::detail
{
    namespace
    {
        struct FreeFragment
        {
            u64 offset = 0;
            u64 size = 0;
            u32 predecessor = InvalidRenderFlowResourceIndex;
        };

        struct ActiveRange
        {
            u64 offset = 0;
            u64 size = 0;
            u32 allocation = InvalidRenderFlowResourceIndex;
            PlanPosition release;
            bool active = true;
        };

        struct HeapState
        {
            HeapState() noexcept : availableFragments(memory::pools::Rendering::GetInstance()), active(memory::pools::Rendering::GetInstance()) {}

            PlacedHeapPlan plan;
            containers::DynamicArray<FreeFragment> availableFragments;
            containers::DynamicArray<ActiveRange> active;
        };

        [[nodiscard]] constexpr bool PositionBefore(const PlanPosition left, const PlanPosition right) noexcept
        {
            return left.flowGroup.value < right.flowGroup.value || (left.flowGroup == right.flowGroup && left.ordinal < right.ordinal);
        }

        [[nodiscard]] constexpr bool IsPowerOfTwo(const u64 value) noexcept
        {
            return value != 0 && (value & (value - 1u)) == 0;
        }

        [[nodiscard]] bool CheckedEnd(const u64 offset, const u64 size, u64& end) noexcept
        {
            if (size == 0 || offset > ~u64{0} - size)
                return false;
            end = offset + size;
            return true;
        }

        [[nodiscard]] bool CheckedAlignUp(const u64 value, const u64 alignment, u64& result) noexcept
        {
            if (!IsPowerOfTwo(alignment) || value > ~u64{0} - (alignment - 1u))
                return false;
            result = (value + alignment - 1u) & ~(alignment - 1u);
            return true;
        }

        [[nodiscard]] bool PlannerFail(RenderFlowResourceFailure* const failure, const RenderFlowResourceFailureCode code, const char* const message) noexcept
        {
            if (failure != nullptr)
            {
                *failure = {};
                failure->code = code;
                failure->phase = RenderFlowResourceSessionState::Resolving;
                failure->message = message;
            }
            return false;
        }

        [[nodiscard]] bool HeapCompatible(const PlacedHeapPlan& heap, const PlacedRangeRequest& request) noexcept
        {
            return heap.kind == request.kind && heap.queue == request.queue && heap.memoryType == request.requirements.memoryType && heap.heapCategory == request.requirements.heapCategory &&
                   heap.compatibilityClass == request.requirements.compatibilityClass && request.requirements.alignment <= heap.alignment;
        }

        void CoalesceFreeFragments(HeapState& heap) noexcept
        {
            std::sort(heap.availableFragments.Begin(), heap.availableFragments.End(), [](const FreeFragment& left, const FreeFragment& right) noexcept { return left.offset < right.offset; });
            u32 output = 0;
            for (const FreeFragment fragment : heap.availableFragments)
            {
                if (fragment.size == 0)
                    continue;
                if (output != 0)
                {
                    FreeFragment& previous = heap.availableFragments[output - 1u];
                    u64 previousEnd = 0;
                    if (CheckedEnd(previous.offset, previous.size, previousEnd) && previousEnd == fragment.offset && previous.predecessor == fragment.predecessor)
                    {
                        previous.size += fragment.size;
                        continue;
                    }
                }
                heap.availableFragments[output++] = fragment;
            }
            heap.availableFragments.Resize(output);
        }

        void ReleaseBefore(HeapState& heap, const PlanPosition acquire) noexcept
        {
            for (ActiveRange& active : heap.active)
            {
                if (!active.active || !PositionBefore(active.release, acquire))
                    continue;
                heap.availableFragments.PushBack({active.offset, active.size, active.allocation});
                active.active = false;
            }
            CoalesceFreeFragments(heap);
        }

        [[nodiscard]] bool FindFirstFit(const HeapState& heap, const PlacedRangeRequest& request, u64& offset, containers::DynamicArray<PlacedPredecessorFragment>& fragments) noexcept
        {
            fragments.Clear();
            for (u32 first = 0; first < heap.availableFragments.Size(); ++first)
            {
                const FreeFragment& firstFragment = heap.availableFragments[first];
                u64 candidate = 0;
                u64 firstEnd = 0;
                if (!CheckedAlignUp(firstFragment.offset, request.requirements.alignment, candidate) || !CheckedEnd(firstFragment.offset, firstFragment.size, firstEnd) || candidate >= firstEnd)
                    continue;
                u64 candidateEnd = 0;
                if (!CheckedEnd(candidate, request.requirements.size, candidateEnd) || candidateEnd > heap.plan.capacity)
                    continue;

                fragments.Clear();
                u64 cursor = candidate;
                bool sawVirgin = false;
                bool sawPredecessor = false;
                for (u32 index = first; index < heap.availableFragments.Size() && cursor < candidateEnd; ++index)
                {
                    const FreeFragment& fragment = heap.availableFragments[index];
                    u64 fragmentEnd = 0;
                    if (!CheckedEnd(fragment.offset, fragment.size, fragmentEnd) || fragmentEnd <= cursor)
                        continue;
                    if (fragment.offset > cursor)
                        break;
                    const u64 overlapEnd = fragmentEnd < candidateEnd ? fragmentEnd : candidateEnd;
                    const u64 overlapSize = overlapEnd - cursor;
                    if (fragment.predecessor == InvalidRenderFlowResourceIndex)
                        sawVirgin = true;
                    else
                    {
                        sawPredecessor = true;
                        fragments.PushBack({fragment.predecessor, cursor, overlapSize});
                    }
                    cursor = overlapEnd;
                }
                // The semantic RHI activation requires exact predecessor
                // coverage. A partially virgin range is therefore not a legal
                // alias candidate; a wholly virgin range is a first activation.
                if (cursor == candidateEnd && !(sawVirgin && sawPredecessor))
                {
                    offset = candidate;
                    if (sawVirgin)
                        fragments.Clear();
                    return true;
                }
            }
            fragments.Clear();
            return false;
        }

        void ConsumeFreeRange(HeapState& heap, const u64 offset, const u64 size) noexcept
        {
            u64 end = offset + size;
            containers::DynamicArray<FreeFragment> remaining{memory::pools::Rendering::GetInstance()};
            remaining.Reserve(heap.availableFragments.Size() + 1u);
            for (const FreeFragment fragment : heap.availableFragments)
            {
                const u64 fragmentEnd = fragment.offset + fragment.size;
                if (fragmentEnd <= offset || fragment.offset >= end)
                {
                    remaining.PushBack(fragment);
                    continue;
                }
                if (fragment.offset < offset)
                    remaining.PushBack({fragment.offset, offset - fragment.offset, fragment.predecessor});
                if (fragmentEnd > end)
                    remaining.PushBack({end, fragmentEnd - end, fragment.predecessor});
            }
            heap.availableFragments = static_cast<containers::DynamicArray<FreeFragment>&&>(remaining);
        }
    } // namespace

    void PlacedRangePlan::Clear() noexcept
    {
        heaps.Clear();
        assignments.Clear();
        predecessors.Clear();
    }

    bool BuildPlacedRangePlan(const containers::ArraySpan<const PlacedRangeRequest> requests, const PlacedRangePlannerConfig& config, PlacedRangePlan& plan, RenderFlowResourceFailure* const failure) noexcept
    {
        plan.Clear();
        PlacedRangePlan scratch;
        if (failure != nullptr)
            *failure = {};
        if (config.minimumHeapBytes == 0 || !IsPowerOfTwo(config.heapAlignment))
            return PlannerFail(failure, RenderFlowResourceFailureCode::InvalidConfiguration, "placed-range planner requires a nonzero heap size and power-of-two heap alignment");

        containers::DynamicArray<u32> order{memory::pools::Rendering::GetInstance()};
        order.Reserve(requests.Size());
        for (u32 index = 0; index < requests.Size(); ++index)
        {
            const PlacedRangeRequest& request = requests[index];
            const bool validKind = request.kind == FrameResourceKind::Texture || request.kind == FrameResourceKind::Buffer;
            const bool validQueue = request.queue == rhi::QueueType::Graphics || request.queue == rhi::QueueType::Compute;
            const bool validCategory = (request.kind == FrameResourceKind::Texture && request.requirements.heapCategory == rhi::PlacedHeapCategory::Texture) ||
                                       (request.kind == FrameResourceKind::Buffer && request.requirements.heapCategory == rhi::PlacedHeapCategory::Buffer);
            if (request.allocation == InvalidRenderFlowResourceIndex || !validKind || !validQueue || !request.firstAcquire.IsValid() || !request.lastRelease.IsValid() ||
                PositionBefore(request.lastRelease, request.firstAcquire) || request.requirements.size == 0 || !IsPowerOfTwo(request.requirements.alignment) ||
                request.requirements.alignment > config.heapAlignment || request.requirements.compatibilityClass == 0 || request.requirements.memoryType != rhi::MemoryType::DeviceLocal || !validCategory)
                return PlannerFail(failure, validQueue ? RenderFlowResourceFailureCode::BackendContractViolation : RenderFlowResourceFailureCode::UnsupportedCapability,
                                   validQueue ? "placed-range request violates the Stage 3 placement profile" : "placed-range planning supports only graphics or compute queues");
            for (u32 prior = 0; prior < index; ++prior)
                if (requests[prior].allocation == request.allocation)
                    return PlannerFail(failure, RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "placed-range request duplicates a logical allocation");
            order.PushBack(index);
        }

        std::sort(order.Begin(), order.End(),
                  [&requests](const u32 leftIndex, const u32 rightIndex) noexcept
                  {
                      const PlacedRangeRequest& left = requests[leftIndex];
                      const PlacedRangeRequest& right = requests[rightIndex];
                      if (left.firstAcquire != right.firstAcquire)
                          return PositionBefore(left.firstAcquire, right.firstAcquire);
                      if (left.requirements.size != right.requirements.size)
                          return left.requirements.size > right.requirements.size;
                      if (left.requirements.alignment != right.requirements.alignment)
                          return left.requirements.alignment > right.requirements.alignment;
                      return left.allocation < right.allocation;
                  });

        containers::DynamicArray<HeapState> heaps{memory::pools::Rendering::GetInstance()};
        containers::DynamicArray<PlacedPredecessorFragment> candidateFragments{memory::pools::Rendering::GetInstance()};
        heaps.Reserve(requests.Size());
        scratch.assignments.Reserve(requests.Size());
        for (const u32 requestIndex : order)
        {
            const PlacedRangeRequest& request = requests[requestIndex];
            u32 selectedHeap = InvalidRenderFlowResourceIndex;
            u64 selectedOffset = 0;
            for (u32 heapIndex = 0; heapIndex < heaps.Size(); ++heapIndex)
            {
                HeapState& heap = heaps[heapIndex];
                if (!HeapCompatible(heap.plan, request))
                    continue;
                ReleaseBefore(heap, request.firstAcquire);
                if (FindFirstFit(heap, request, selectedOffset, candidateFragments))
                {
                    selectedHeap = heapIndex;
                    break;
                }
            }

            if (selectedHeap == InvalidRenderFlowResourceIndex)
            {
                u64 requiredCapacity = request.requirements.size > config.minimumHeapBytes ? request.requirements.size : config.minimumHeapBytes;
                u64 capacity = 0;
                if (!CheckedAlignUp(requiredCapacity, config.heapAlignment, capacity))
                    return PlannerFail(failure, RenderFlowResourceFailureCode::ArithmeticOverflow, "placed heap capacity alignment overflows u64");
                HeapState heap;
                heap.plan.kind = request.kind;
                heap.plan.queue = request.queue;
                heap.plan.memoryType = request.requirements.memoryType;
                heap.plan.heapCategory = request.requirements.heapCategory;
                heap.plan.compatibilityClass = request.requirements.compatibilityClass;
                heap.plan.capacity = capacity;
                heap.plan.alignment = config.heapAlignment;
                heap.availableFragments.PushBack({0, capacity, InvalidRenderFlowResourceIndex});
                heaps.PushBack(static_cast<HeapState&&>(heap));
                selectedHeap = heaps.Size() - 1u;
                if (!FindFirstFit(heaps[selectedHeap], request, selectedOffset, candidateFragments))
                    return PlannerFail(failure, RenderFlowResourceFailureCode::BackendContractViolation, "fresh placed heap cannot satisfy the request used to size it");
            }

            HeapState& heap = heaps[selectedHeap];
            PlacedRangeAssignment assignment;
            assignment.allocation = request.allocation;
            assignment.heap = selectedHeap;
            assignment.offset = selectedOffset;
            assignment.size = request.requirements.size;
            assignment.alignment = request.requirements.alignment;
            assignment.predecessorOffset = scratch.predecessors.Size();
            assignment.predecessorCount = candidateFragments.Size();
            for (const PlacedPredecessorFragment fragment : candidateFragments)
                scratch.predecessors.PushBack(fragment);
            ConsumeFreeRange(heap, selectedOffset, request.requirements.size);
            heap.active.PushBack({selectedOffset, request.requirements.size, request.allocation, request.lastRelease, true});
            scratch.assignments.PushBack(assignment);
        }

        scratch.heaps.Reserve(heaps.Size());
        for (const HeapState& heap : heaps)
            scratch.heaps.PushBack(heap.plan);
        plan.heaps = static_cast<containers::DynamicArray<PlacedHeapPlan>&&>(scratch.heaps);
        plan.assignments = static_cast<containers::DynamicArray<PlacedRangeAssignment>&&>(scratch.assignments);
        plan.predecessors = static_cast<containers::DynamicArray<PlacedPredecessorFragment>&&>(scratch.predecessors);
        return true;
    }

    namespace
    {
        enum class PlacedCacheState : u8
        {
            Reusable,
            Assigned,
            PendingRetirement,
            PendingNativeDestruction,
            ProviderFailureQuarantine,
            NativeReleased
        };

        [[nodiscard]] bool ExtentEqual(const rhi::Extent3D& left, const rhi::Extent3D& right) noexcept
        {
            return left.width == right.width && left.height == right.height && left.depth == right.depth;
        }

        [[nodiscard]] bool TextureDescriptorEqual(const rhi::TextureDesc& left, const rhi::TextureDesc& right) noexcept
        {
            return ExtentEqual(left.extent, right.extent) && left.dimension == right.dimension && left.format == right.format && left.mipCount == right.mipCount && left.arraySize == right.arraySize &&
                   left.sampleCount == right.sampleCount && left.usage == right.usage && left.initialState == right.initialState && left.virtualResource == right.virtualResource && left.keepInitialState == right.keepInitialState;
        }

        [[nodiscard]] bool BufferDescriptorEqual(const rhi::BufferDesc& left, const rhi::BufferDesc& right) noexcept
        {
            return left.size == right.size && left.structureStride == right.structureStride && left.format == right.format && left.usage == right.usage && left.initialState == right.initialState &&
                   left.memoryType == right.memoryType && left.virtualResource == right.virtualResource && left.keepInitialState == right.keepInitialState;
        }

        [[nodiscard]] bool ObjectDescriptorEqual(const FrameResourceDesc& left, const FrameResourceDesc& right) noexcept
        {
            if (left.kind != right.kind)
                return false;
            return left.kind == FrameResourceKind::Texture ? TextureDescriptorEqual(left.texture.active, right.texture.active)
                                                           : left.kind == FrameResourceKind::Buffer && BufferDescriptorEqual(left.buffer.active, right.buffer.active);
        }

        [[nodiscard]] bool HeapPlanEqual(const PlacedHeapPlan& left, const PlacedHeapPlan& right) noexcept
        {
            return left.kind == right.kind && left.queue == right.queue && left.memoryType == right.memoryType && left.heapCategory == right.heapCategory &&
                   left.compatibilityClass == right.compatibilityClass && left.capacity == right.capacity && left.alignment == right.alignment;
        }

        [[nodiscard]] bool FencesComplete(const rhi::ResidencyFenceSet& fences) noexcept
        {
            return (fences.graphics == 0 || rhi::IsGpuFenceComplete({rhi::QueueType::Graphics, fences.graphics})) &&
                   (fences.compute == 0 || rhi::IsGpuFenceComplete({rhi::QueueType::Compute, fences.compute})) && (fences.copy == 0 || rhi::IsGpuFenceComplete({rhi::QueueType::Copy, fences.copy}));
        }

        [[nodiscard]] bool PlacedFail(RenderFlowResourceFailure* const failure, const RenderFlowResourceFailureCode code, const char* const message,
                                      const RenderFlowResourceSessionState phase = RenderFlowResourceSessionState::Resolving) noexcept
        {
            if (failure != nullptr)
                *failure = {code, phase, {}, {}, {}, message};
            return false;
        }
    } // namespace

    struct PlacedResourcePool::Impl
    {
        struct HeapEntry
        {
            PlacedHeapPlan plan;
            rhi::Heap heap;
            rhi::ResidencyFenceSet safeAfter;
            rhi::NativeReleaseObservation releaseObservation;
            u64 lastUsedFrame = 0;
            PlacedCacheState state = PlacedCacheState::Reusable;
            bool reusableAfterRetirement = true;
        };

        struct ObjectEntry
        {
            FrameResourceDesc desc;
            rhi::Texture texture;
            rhi::Buffer buffer;
            rhi::PlacementRecord placement;
            u32 heapEntry = InvalidPlacedResourceEntry;
            u64 lastUsedFrame = 0;
            PlacedCacheState state = PlacedCacheState::Reusable;
        };

        Impl(const RenderFlowResourceAllocatorConfig& allocatorConfig, AllocatorNativeByteLedger& nativeByteLedger) noexcept
            : heaps(memory::pools::Rendering::GetInstance()), objects(memory::pools::Rendering::GetInstance()), ledger(&nativeByteLedger), textureSoftTargetBytes(allocatorConfig.textureSoftTargetBytes),
              bufferSoftTargetBytes(allocatorConfig.bufferSoftTargetBytes)
        {
        }

        containers::DynamicArray<HeapEntry> heaps;
        containers::DynamicArray<ObjectEntry> objects;
        mutable concurrency::SpinLock lock;
        AllocatorNativeByteLedger* ledger = nullptr;
        u64 textureSoftTargetBytes = ~u64{0};
        u64 bufferSoftTargetBytes = ~u64{0};
        PlacedResourcePoolStats stats;
    };

    PlacedResourcePool::PlacedResourcePool(const RenderFlowResourceAllocatorConfig& config, AllocatorNativeByteLedger& ledger) noexcept
    {
        memory::MemoryBlock block = memory::Allocate(memory::PoolId::Rendering, sizeof(Impl), alignof(Impl));
        if (block)
            m_impl = new (block.address) Impl(config, ledger);
    }

    PlacedResourcePool::~PlacedResourcePool()
    {
        if (m_impl == nullptr)
            return;
        for (Impl::ObjectEntry& object : m_impl->objects)
        {
            object.texture.Reset();
            object.buffer.Reset();
        }
        for (Impl::HeapEntry& heap : m_impl->heaps)
        {
            rhi::ReleaseNativeReleaseObservation(heap.releaseObservation);
            heap.heap.Reset();
            if (heap.plan.capacity != 0)
                m_impl->ledger->Release(heap.plan.kind, heap.plan.capacity);
        }
        Impl* const impl = m_impl;
        m_impl = nullptr;
        impl->~Impl();
        memory::MemoryBlock block{impl, sizeof(Impl), memory::PoolId::Rendering};
        memory::Free(block);
    }

    void PlacedResourcePool::PollUnlocked() noexcept
    {
        if (m_impl == nullptr || !rhi::IsInitialized())
            return;
        m_impl->stats.pendingRetirementHeaps = 0;
        m_impl->stats.pendingNativeDestructionHeaps = 0;
        bool hasAssignedHeap = false;
        bool hasReleasedHeap = false;
        for (u32 heapIndex = 0; heapIndex < m_impl->heaps.Size(); ++heapIndex)
        {
            Impl::HeapEntry& heap = m_impl->heaps[heapIndex];
            hasAssignedHeap = hasAssignedHeap || heap.state == PlacedCacheState::Assigned;
            hasReleasedHeap = hasReleasedHeap || heap.state == PlacedCacheState::NativeReleased;
            if (heap.state == PlacedCacheState::PendingNativeDestruction)
            {
                if (!rhi::IsNativeReleaseComplete(heap.releaseObservation))
                {
                    ++m_impl->stats.pendingNativeDestructionHeaps;
                    continue;
                }
                rhi::ReleaseNativeReleaseObservation(heap.releaseObservation);
                m_impl->ledger->Release(heap.plan.kind, heap.plan.capacity);
                heap.plan.capacity = 0;
                heap.state = PlacedCacheState::NativeReleased;
                hasReleasedHeap = true;
                continue;
            }
            if (heap.state == PlacedCacheState::PendingRetirement)
            {
                if (!FencesComplete(heap.safeAfter))
                {
                    ++m_impl->stats.pendingRetirementHeaps;
                    continue;
                }
                heap.safeAfter = {};
                if (heap.reusableAfterRetirement)
                {
                    heap.state = PlacedCacheState::Reusable;
                    for (Impl::ObjectEntry& object : m_impl->objects)
                        if (object.heapEntry == heapIndex && object.state == PlacedCacheState::PendingRetirement)
                            object.state = PlacedCacheState::Reusable;
                    continue;
                }
                heap.state = PlacedCacheState::ProviderFailureQuarantine;
            }
            if (heap.state == PlacedCacheState::ProviderFailureQuarantine)
            {
                rhi::Failure ignored;
                heap.releaseObservation = rhi::ObserveNativeRelease(rhi::ResourceRef(heap.heap.GetRef()), &ignored);
                if (!heap.releaseObservation.IsValid())
                {
                    ++m_impl->stats.pendingNativeDestructionHeaps;
                    continue;
                }
                for (Impl::ObjectEntry& object : m_impl->objects)
                    if (object.heapEntry == heapIndex && object.state != PlacedCacheState::NativeReleased)
                    {
                        object.texture.Reset();
                        object.buffer.Reset();
                        object.placement = {};
                        object.state = PlacedCacheState::NativeReleased;
                    }
                heap.heap.Reset();
                heap.state = PlacedCacheState::PendingNativeDestruction;
                ++m_impl->stats.pendingNativeDestructionHeaps;
                continue;
            }
        }

        // Retire/Rollback consume the caller's batch. Compact only when no
        // outstanding batch can still refer to pool indices; native references
        // and pending release observations do not depend on those indices.
        if (hasAssignedHeap || !hasReleasedHeap)
            return;

        containers::DynamicArray<u32> heapIndices{memory::pools::Rendering::GetInstance()};
        heapIndices.Resize(m_impl->heaps.Size());
        u32 heapCount = 0;
        for (u32 index = 0; index < m_impl->heaps.Size(); ++index)
        {
            if (m_impl->heaps[index].state == PlacedCacheState::NativeReleased)
            {
                heapIndices[index] = InvalidPlacedResourceEntry;
                continue;
            }
            heapIndices[index] = heapCount;
            if (heapCount != index)
                m_impl->heaps[heapCount] = static_cast<Impl::HeapEntry&&>(m_impl->heaps[index]);
            ++heapCount;
        }
        u32 objectCount = 0;
        for (u32 index = 0; index < m_impl->objects.Size(); ++index)
        {
            Impl::ObjectEntry& object = m_impl->objects[index];
            if (object.state == PlacedCacheState::NativeReleased)
                continue;
            VG_ASSERT(object.heapEntry < heapIndices.Size() && heapIndices[object.heapEntry] != InvalidPlacedResourceEntry);
            object.heapEntry = heapIndices[object.heapEntry];
            if (objectCount != index)
                m_impl->objects[objectCount] = static_cast<Impl::ObjectEntry&&>(object);
            ++objectCount;
        }
        m_impl->objects.Resize(objectCount);
        m_impl->heaps.Resize(heapCount);
    }

    bool PlacedResourcePool::Acquire(const PlacedRangePlan& plan, const containers::ArraySpan<const PlacedObjectRequest> objects, const u64 frameSerial, PlacedResourceBatch& batch,
                                     RenderFlowResourceFailure* const failure) noexcept
    {
        batch.Clear();
        if (failure != nullptr)
            *failure = {};
        if (m_impl == nullptr)
            return PlacedFail(failure, RenderFlowResourceFailureCode::CapacityExceeded, "placed-resource pool metadata allocation failed");
        if (plan.heaps.Empty() || plan.assignments.Empty() || objects.Size() != plan.assignments.Size())
            return PlacedFail(failure, RenderFlowResourceFailureCode::BackendContractViolation, "placed-resource batch disagrees with its range plan");
        concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
        PollUnlocked();

        const u32 initialHeapCount = m_impl->heaps.Size();
        const u32 initialObjectCount = m_impl->objects.Size();
        const u64 initialHeapHits = m_impl->stats.heapHits;
        const u64 initialHeapMisses = m_impl->stats.heapMisses;
        const u64 initialObjectHits = m_impl->stats.objectHits;
        const u64 initialObjectMisses = m_impl->stats.objectMisses;
        const auto rollbackUnlocked = [this, &batch, initialHeapCount, initialObjectCount, initialHeapHits, initialHeapMisses, initialObjectHits, initialObjectMisses]() noexcept
        {
            for (u32 index = initialObjectCount; index < m_impl->objects.Size(); ++index)
            {
                m_impl->objects[index].texture.Reset();
                m_impl->objects[index].buffer.Reset();
            }
            m_impl->objects.Resize(initialObjectCount);
            for (const u32 objectIndex : batch.objectEntries)
                if (objectIndex < initialObjectCount && m_impl->objects[objectIndex].state == PlacedCacheState::Assigned)
                    m_impl->objects[objectIndex].state = PlacedCacheState::Reusable;
            for (u32 index = initialHeapCount; index < m_impl->heaps.Size(); ++index)
            {
                Impl::HeapEntry& heap = m_impl->heaps[index];
                if (heap.state != PlacedCacheState::Assigned || !heap.heap.IsValid())
                    continue;
                rhi::Failure observationFailure;
                heap.releaseObservation = rhi::ObserveNativeRelease(rhi::ResourceRef(heap.heap.GetRef()), &observationFailure);
                if (heap.releaseObservation.IsValid())
                {
                    heap.heap.Reset();
                    heap.state = PlacedCacheState::PendingNativeDestruction;
                    ++m_impl->stats.pendingNativeDestructionHeaps;
                }
                else
                    heap.state = PlacedCacheState::ProviderFailureQuarantine;
            }
            for (const u32 heapIndex : batch.heapEntries)
                if (heapIndex < initialHeapCount && m_impl->heaps[heapIndex].state == PlacedCacheState::Assigned)
                    m_impl->heaps[heapIndex].state = PlacedCacheState::Reusable;
            m_impl->stats.heapHits = initialHeapHits;
            m_impl->stats.heapMisses = initialHeapMisses;
            m_impl->stats.objectHits = initialObjectHits;
            m_impl->stats.objectMisses = initialObjectMisses;
            batch.Clear();
        };
        const auto fail = [&rollbackUnlocked, failure](const RenderFlowResourceFailureCode code, const char* const message) noexcept
        {
            rollbackUnlocked();
            return PlacedFail(failure, code, message);
        };

        batch.heapEntries.Reserve(plan.heaps.Size());
        batch.objectEntries.Reserve(plan.assignments.Size());
        batch.assignments.Reserve(plan.assignments.Size());
        for (const PlacedHeapPlan& requested : plan.heaps)
        {
            u32 selected = InvalidPlacedResourceEntry;
            for (u32 index = 0; index < m_impl->heaps.Size(); ++index)
            {
                const Impl::HeapEntry& candidate = m_impl->heaps[index];
                if (candidate.state == PlacedCacheState::Reusable && candidate.heap.IsValid() && HeapPlanEqual(candidate.plan, requested))
                {
                    selected = index;
                    break;
                }
            }
            if (selected == InvalidPlacedResourceEntry)
            {
                if (!m_impl->ledger->TryReserve(requested.kind, requested.capacity))
                    return fail(RenderFlowResourceFailureCode::BudgetExceeded, "placed heap exceeds the allocator-wide hard native-byte budget");
                rhi::HeapDesc desc;
                desc.size = requested.capacity;
                desc.alignment = requested.alignment;
                desc.compatibilityClass = requested.compatibilityClass;
                desc.memoryType = requested.memoryType;
                desc.heapCategory = requested.heapCategory;
                rhi::Failure rhiFailure;
                rhi::Heap heap(rhi::AdoptReference, rhi::CreateHeap(desc, &rhiFailure));
                if (!heap.IsValid())
                {
                    m_impl->ledger->Release(requested.kind, requested.capacity);
                    return fail(MapRhiFailure(rhiFailure, RhiFailureContext::BackendContract), "placed heap creation failed");
                }
                rhi::HeapDesc authoritative;
                if (!rhi::GetHeapDesc(heap.GetRef(), authoritative, &rhiFailure) || authoritative.size != desc.size || authoritative.alignment != desc.alignment ||
                    authoritative.compatibilityClass != desc.compatibilityClass || authoritative.memoryType != desc.memoryType || authoritative.heapCategory != desc.heapCategory)
                {
                    Impl::HeapEntry quarantined;
                    quarantined.plan = requested;
                    quarantined.heap = static_cast<rhi::Heap&&>(heap);
                    rhi::Failure observationFailure;
                    quarantined.releaseObservation = rhi::ObserveNativeRelease(rhi::ResourceRef(quarantined.heap.GetRef()), &observationFailure);
                    if (quarantined.releaseObservation.IsValid())
                    {
                        quarantined.heap.Reset();
                        quarantined.state = PlacedCacheState::PendingNativeDestruction;
                        ++m_impl->stats.pendingNativeDestructionHeaps;
                    }
                    else
                        quarantined.state = PlacedCacheState::ProviderFailureQuarantine;
                    m_impl->heaps.PushBack(static_cast<Impl::HeapEntry&&>(quarantined));
                    return fail(RenderFlowResourceFailureCode::BackendContractViolation, "placed heap differs from its canonical descriptor");
                }
                Impl::HeapEntry entry;
                entry.plan = requested;
                entry.heap = static_cast<rhi::Heap&&>(heap);
                entry.lastUsedFrame = frameSerial;
                selected = m_impl->heaps.Size();
                m_impl->heaps.PushBack(static_cast<Impl::HeapEntry&&>(entry));
                ++m_impl->stats.heapMisses;
            }
            else
                ++m_impl->stats.heapHits;
            m_impl->heaps[selected].state = PlacedCacheState::Assigned;
            m_impl->heaps[selected].lastUsedFrame = frameSerial;
            batch.heapEntries.PushBack(selected);
        }

        for (const PlacedRangeAssignment& range : plan.assignments)
        {
            const PlacedObjectRequest* requested = nullptr;
            for (const PlacedObjectRequest& candidate : objects)
                if (candidate.allocation == range.allocation)
                {
                    if (requested != nullptr)
                        return fail(RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "placed-object request duplicates a logical allocation");
                    requested = &candidate;
                }
            if (requested == nullptr || range.heap >= batch.heapEntries.Size())
                return fail(RenderFlowResourceFailureCode::InvalidOrStaleIdentity, "placed-object request is missing from its range plan");
            const u32 heapEntry = batch.heapEntries[range.heap];
            Impl::HeapEntry& heap = m_impl->heaps[heapEntry];
            FrameResourceDesc desc = requested->desc;
            if (desc.kind == FrameResourceKind::Texture)
                desc.texture.active.virtualResource = true;
            else if (desc.kind == FrameResourceKind::Buffer)
                desc.buffer.active.virtualResource = true;
            else
                return fail(RenderFlowResourceFailureCode::BackendContractViolation, "placed-object request has an invalid resource kind");

            u32 selected = InvalidPlacedResourceEntry;
            for (u32 index = 0; index < m_impl->objects.Size(); ++index)
            {
                const Impl::ObjectEntry& candidate = m_impl->objects[index];
                if (candidate.state == PlacedCacheState::Reusable && candidate.heapEntry == heapEntry && candidate.placement.offset == range.offset && ObjectDescriptorEqual(candidate.desc, desc) &&
                    (candidate.texture.IsValid() ? rhi::GetRefCount(candidate.texture.GetRef()) == 1 : candidate.buffer.IsValid() && rhi::GetRefCount(candidate.buffer.GetRef()) == 1))
                {
                    selected = index;
                    break;
                }
            }
            if (selected == InvalidPlacedResourceEntry)
            {
                rhi::Failure rhiFailure;
                Impl::ObjectEntry entry;
                entry.desc = desc;
                entry.heapEntry = heapEntry;
                entry.lastUsedFrame = frameSerial;
                if (desc.kind == FrameResourceKind::Texture)
                    entry.texture = rhi::Texture(rhi::AdoptReference, rhi::CreateTexture(desc.texture.active, {}, &rhiFailure));
                else
                    entry.buffer = rhi::Buffer(rhi::AdoptReference, rhi::CreateBuffer(desc.buffer.active, {}, &rhiFailure));
                if (!entry.texture.IsValid() && !entry.buffer.IsValid())
                    return fail(MapRhiFailure(rhiFailure, RhiFailureContext::BackendContract), "placed object creation failed");
                if (entry.texture.IsValid())
                {
                    rhi::TextureDesc authoritative;
                    if (!rhi::GetTextureDesc(entry.texture.GetRef(), authoritative, &rhiFailure))
                        return fail(MapRhiFailure(rhiFailure, RhiFailureContext::BackendContract), "placed texture descriptor query failed");
                    if (!TextureDescriptorEqual(authoritative, desc.texture.active))
                        return fail(RenderFlowResourceFailureCode::BackendContractViolation, "placed texture differs from its canonical creation descriptor");
                }
                else
                {
                    rhi::BufferDesc authoritative;
                    if (!rhi::GetBufferDesc(entry.buffer.GetRef(), authoritative, &rhiFailure))
                        return fail(MapRhiFailure(rhiFailure, RhiFailureContext::BackendContract), "placed buffer descriptor query failed");
                    if (!BufferDescriptorEqual(authoritative, desc.buffer.active))
                        return fail(RenderFlowResourceFailureCode::BackendContractViolation, "placed buffer differs from its canonical creation descriptor");
                }
                const rhi::MemoryRequirements requirements = entry.texture.IsValid() ? rhi::GetMemoryRequirements(entry.texture.GetRef()) : rhi::GetMemoryRequirements(entry.buffer.GetRef());
                if (requirements.size != range.size || requirements.alignment != range.alignment || requirements.compatibilityClass != heap.plan.compatibilityClass ||
                    requirements.memoryType != heap.plan.memoryType || requirements.heapCategory != heap.plan.heapCategory)
                    return fail(RenderFlowResourceFailureCode::BackendContractViolation, "placed object requirements disagree with the range plan");
                const bool bound = entry.texture.IsValid() ? rhi::BindMemory(entry.texture.GetRef(), heap.heap.GetRef(), range.offset, &rhiFailure)
                                                           : rhi::BindMemory(entry.buffer.GetRef(), heap.heap.GetRef(), range.offset, &rhiFailure);
                if (!bound)
                    return fail(MapRhiFailure(rhiFailure, RhiFailureContext::BackendContract), "placed object binding failed");
                const bool queried = entry.texture.IsValid() ? rhi::GetPlacement(entry.texture.GetRef(), entry.placement, &rhiFailure) : rhi::GetPlacement(entry.buffer.GetRef(), entry.placement, &rhiFailure);
                if (!queried || entry.placement.heap != heap.heap.GetRef() || entry.placement.offset != range.offset || entry.placement.size != range.size || entry.placement.alignment != range.alignment ||
                    entry.placement.compatibilityClass != heap.plan.compatibilityClass)
                    return fail(RenderFlowResourceFailureCode::BackendContractViolation, "placed object published an incorrect immutable placement");
                selected = m_impl->objects.Size();
                m_impl->objects.PushBack(static_cast<Impl::ObjectEntry&&>(entry));
                ++m_impl->stats.objectMisses;
            }
            else
                ++m_impl->stats.objectHits;

            Impl::ObjectEntry& object = m_impl->objects[selected];
            object.state = PlacedCacheState::Assigned;
            object.lastUsedFrame = frameSerial;
            batch.objectEntries.PushBack(selected);
            batch.assignments.PushBack({range.allocation, selected, object.texture.GetRef(), object.buffer.GetRef(), object.placement});
        }
        return true;
    }

    void PlacedResourcePool::Rollback(PlacedResourceBatch& batch) noexcept
    {
        if (m_impl == nullptr)
        {
            batch.Clear();
            return;
        }
        concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
        for (const u32 objectIndex : batch.objectEntries)
            if (objectIndex < m_impl->objects.Size() && m_impl->objects[objectIndex].state == PlacedCacheState::Assigned)
                m_impl->objects[objectIndex].state = PlacedCacheState::Reusable;
        for (const u32 heapIndex : batch.heapEntries)
            if (heapIndex < m_impl->heaps.Size() && m_impl->heaps[heapIndex].state == PlacedCacheState::Assigned)
                m_impl->heaps[heapIndex].state = PlacedCacheState::Reusable;
        batch.Clear();
    }

    void PlacedResourcePool::Retire(PlacedResourceBatch& batch, const rhi::ResidencyFenceSet& safeAfter, const bool reusable) noexcept
    {
        if (m_impl == nullptr)
        {
            batch.Clear();
            return;
        }
        concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
        for (const u32 objectIndex : batch.objectEntries)
            if (objectIndex < m_impl->objects.Size() && m_impl->objects[objectIndex].state == PlacedCacheState::Assigned)
                m_impl->objects[objectIndex].state = PlacedCacheState::PendingRetirement;
        for (const u32 heapIndex : batch.heapEntries)
            if (heapIndex < m_impl->heaps.Size() && m_impl->heaps[heapIndex].state == PlacedCacheState::Assigned)
            {
                m_impl->heaps[heapIndex].safeAfter = safeAfter;
                m_impl->heaps[heapIndex].reusableAfterRetirement = reusable;
                m_impl->heaps[heapIndex].state = PlacedCacheState::PendingRetirement;
            }
        batch.Clear();
        PollUnlocked();
    }

    void PlacedResourcePool::Poll() noexcept
    {
        if (m_impl == nullptr)
            return;
        concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
        PollUnlocked();
    }

    bool PlacedResourcePool::TrimToSoftTargets(ResourcePoolTrimState& trim, RenderFlowResourceFailure* const failure) noexcept
    {
        if (failure != nullptr)
            *failure = {};
        if (m_impl == nullptr || !rhi::IsInitialized())
            return true;
        concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);

        containers::DynamicArray<u32> evictionOrder{memory::pools::Rendering::GetInstance()};
        for (u32 heapIndex = 0; heapIndex < m_impl->heaps.Size(); ++heapIndex)
        {
            const Impl::HeapEntry& heap = m_impl->heaps[heapIndex];
            if (heap.state != PlacedCacheState::Reusable || !heap.heap.IsValid())
                continue;
            bool poolOwnsEveryObject = true;
            for (const Impl::ObjectEntry& object : m_impl->objects)
            {
                if (object.heapEntry != heapIndex || object.state == PlacedCacheState::NativeReleased)
                    continue;
                if (object.state != PlacedCacheState::Reusable ||
                    (object.texture.IsValid() ? rhi::GetRefCount(object.texture.GetRef()) != 1 : !object.buffer.IsValid() || rhi::GetRefCount(object.buffer.GetRef()) != 1))
                {
                    poolOwnsEveryObject = false;
                    break;
                }
            }
            if (poolOwnsEveryObject)
                evictionOrder.PushBack(heapIndex);
        }
        std::sort(evictionOrder.Begin(), evictionOrder.End(), [this](const u32 left, const u32 right) noexcept
        {
            const Impl::HeapEntry& leftHeap = m_impl->heaps[left];
            const Impl::HeapEntry& rightHeap = m_impl->heaps[right];
            return leftHeap.lastUsedFrame != rightHeap.lastUsedFrame ? leftHeap.lastUsedFrame < rightHeap.lastUsedFrame : left < right;
        });

        for (const u32 heapIndex : evictionOrder)
        {
            Impl::HeapEntry& heap = m_impl->heaps[heapIndex];
            const bool overTarget =
                heap.plan.kind == FrameResourceKind::Texture ? trim.projectedTextureBytes > m_impl->textureSoftTargetBytes : trim.projectedBufferBytes > m_impl->bufferSoftTargetBytes;
            if (!overTarget)
                continue;

            rhi::Failure rhiFailure;
            heap.releaseObservation = rhi::ObserveNativeRelease(rhi::ResourceRef(heap.heap.GetRef()), &rhiFailure);
            if (!heap.releaseObservation.IsValid())
                return PlacedFail(failure, MapRhiFailure(rhiFailure, RhiFailureContext::BackendContract), rhiFailure.message[0] != '\0' ? rhiFailure.message : "placed heap native-release observation failed",
                                  RenderFlowResourceSessionState::Idle);
            for (Impl::ObjectEntry& object : m_impl->objects)
                if (object.heapEntry == heapIndex && object.state != PlacedCacheState::NativeReleased)
                {
                    object.texture.Reset();
                    object.buffer.Reset();
                    object.placement = {};
                    object.state = PlacedCacheState::NativeReleased;
                }
            heap.heap.Reset();
            heap.safeAfter = {};
            heap.state = PlacedCacheState::PendingNativeDestruction;
            if (heap.plan.kind == FrameResourceKind::Texture)
                trim.projectedTextureBytes -= heap.plan.capacity;
            else
                trim.projectedBufferBytes -= heap.plan.capacity;
        }
        PollUnlocked();
        return true;
    }

    bool PlacedResourcePool::ClearPersistentCache(RenderFlowResourceFailure* const failure) noexcept
    {
        if (failure != nullptr)
            *failure = {};
        if (m_impl == nullptr || !rhi::IsInitialized())
            return true;
        concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
        PollUnlocked();
        containers::DynamicArray<u32> candidates{memory::pools::Rendering::GetInstance()};
        containers::DynamicArray<rhi::NativeReleaseObservation> observations{memory::pools::Rendering::GetInstance()};
        for (u32 heapIndex = 0; heapIndex < m_impl->heaps.Size(); ++heapIndex)
        {
            const Impl::HeapEntry& heap = m_impl->heaps[heapIndex];
            if (heap.state != PlacedCacheState::Reusable && heap.state != PlacedCacheState::PendingRetirement)
                continue;
            for (const Impl::ObjectEntry& object : m_impl->objects)
            {
                if (object.heapEntry != heapIndex || object.state == PlacedCacheState::NativeReleased)
                    continue;
                if (object.state == PlacedCacheState::Assigned ||
                    (object.texture.IsValid() ? rhi::GetRefCount(object.texture.GetRef()) != 1 : !object.buffer.IsValid() || rhi::GetRefCount(object.buffer.GetRef()) != 1))
                    return PlacedFail(failure, RenderFlowResourceFailureCode::BackendContractViolation, "persistent placed cache contains an object owner outside the allocator",
                                      RenderFlowResourceSessionState::Idle);
            }
            candidates.PushBack(heapIndex);
        }
        observations.Reserve(candidates.Size());
        for (const u32 heapIndex : candidates)
        {
            rhi::Failure rhiFailure;
            rhi::NativeReleaseObservation observation = rhi::ObserveNativeRelease(rhi::ResourceRef(m_impl->heaps[heapIndex].heap.GetRef()), &rhiFailure);
            if (!observation.IsValid())
            {
                for (rhi::NativeReleaseObservation& prepared : observations)
                    rhi::ReleaseNativeReleaseObservation(prepared);
                return PlacedFail(failure, MapRhiFailure(rhiFailure, RhiFailureContext::BackendContract), rhiFailure.message[0] != '\0' ? rhiFailure.message : "placed heap native-release observation failed",
                                  RenderFlowResourceSessionState::Idle);
            }
            observations.PushBack(observation);
        }
        for (u32 position = 0; position < candidates.Size(); ++position)
        {
            const u32 heapIndex = candidates[position];
            for (Impl::ObjectEntry& object : m_impl->objects)
                if (object.heapEntry == heapIndex && object.state != PlacedCacheState::NativeReleased)
                {
                    object.texture.Reset();
                    object.buffer.Reset();
                    object.placement = {};
                    object.state = PlacedCacheState::NativeReleased;
                }
            Impl::HeapEntry& heap = m_impl->heaps[heapIndex];
            heap.releaseObservation = observations[position];
            observations[position] = {};
            heap.heap.Reset();
            heap.safeAfter = {};
            heap.state = PlacedCacheState::PendingNativeDestruction;
        }
        PollUnlocked();
        return true;
    }

    void PlacedResourcePool::DeviceLost() noexcept
    {
        if (m_impl == nullptr)
            return;
        concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
        for (Impl::ObjectEntry& object : m_impl->objects)
        {
            object.texture.Reset();
            object.buffer.Reset();
        }
        for (Impl::HeapEntry& heap : m_impl->heaps)
        {
            rhi::ReleaseNativeReleaseObservation(heap.releaseObservation);
            heap.heap.Reset();
            if (heap.plan.capacity != 0)
                m_impl->ledger->Release(heap.plan.kind, heap.plan.capacity);
        }
        m_impl->objects.Clear();
        m_impl->heaps.Clear();
        m_impl->stats = {};
    }

    PlacedResourcePoolStats PlacedResourcePool::GetStats() const noexcept
    {
        if (m_impl == nullptr)
            return {};
        concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
        const_cast<PlacedResourcePool*>(this)->PollUnlocked();
        return m_impl->stats;
    }
} // namespace vanguard::rendering::detail
