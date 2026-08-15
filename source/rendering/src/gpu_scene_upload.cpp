#include <vanguard/rendering/gpu_scene_upload.hpp>

#include <vanguard/concurrency/synchronization.hpp>
#include <vanguard/concurrency/thread.hpp>
#include <vanguard/memory/memory.hpp>

#include <algorithm>
#include <new>

namespace vanguard::rendering
{
    namespace
    {
        enum class BatchState : u8 { None, Open, Submitting };

        struct PlannedRequest
        {
            GpuSceneAllocation allocation;
            u32 request = 0;
            u32 first = 0;
            u32 count = 0;
            u32 stride = 0;
            bool initialPublication = false;
        };

        struct PlannedUpdate
        {
            GpuSceneAllocation allocation;
            u32 request = 0;
            u32 first = 0;
            u32 count = 0;
            u64 stagingOffset = 0;
            u64 size = 0;
            bool ready = false;
            bool initialPublication = false;
        };

        struct PlannedCopy
        {
            rhi::BufferRef destination;
            u64 destinationOffset = 0;
            u64 sourceOffset = 0;
            u64 size = 0;
        };

        [[nodiscard]] constexpr bool SameDestination(const PlannedRequest& left,
                                                     const PlannedRequest& right) noexcept
        {
            return left.allocation.table == right.allocation.table && left.first == right.first &&
                   left.count == right.count;
        }

        [[nodiscard]] constexpr u64 AlignUp(const u64 value, const u64 alignment) noexcept
        {
            return (value + alignment - 1u) & ~(alignment - 1u);
        }

        void ClearFailure(GpuSceneUploadFailure* const failure) noexcept
        {
            if (failure != nullptr) *failure = {};
        }

        [[nodiscard]] bool Fail(GpuSceneUploadFailure* const failure,
                                const GpuSceneUploadFailureCode code, const char* const message,
                                const u32 request = 0xffffffffu, const rhi::Failure& rhiFailure = {},
                                const GpuSceneLifetimeFailure& lifetimeFailure = {}) noexcept
        {
            if (failure != nullptr) *failure = {code, request, message, rhiFailure, lifetimeFailure};
            return false;
        }
    } // namespace

    struct GpuSceneUploader::Impl
    {
        struct Segment
        {
            rhi::BufferRef buffer;
            u8* mapped = nullptr;
            rhi::GpuFence completion;
        };

        Impl() noexcept
            : requests(memory::pools::Rendering::GetInstance()), uniqueRequests(memory::pools::Rendering::GetInstance()),
              updates(memory::pools::Rendering::GetInstance()), copies(memory::pools::Rendering::GetInstance()),
              affectedPages(memory::pools::Rendering::GetInstance()),
              initialPublications(memory::pools::Rendering::GetInstance())
        {
        }

        Segment segments[MaximumGpuSceneUploadSegments];
        containers::DynamicArray<PlannedRequest> requests;
        containers::DynamicArray<PlannedRequest> uniqueRequests;
        containers::DynamicArray<PlannedUpdate> updates;
        containers::DynamicArray<PlannedCopy> copies;
        containers::DynamicArray<rhi::BufferRef> affectedPages;
        containers::DynamicArray<GpuSceneAllocation> initialPublications;
        mutable concurrency::RWLock batchLock;
        GpuSceneTables* tables = nullptr;
        GpuSceneLifetime* lifetime = nullptr;
        GpuSceneUploadConfig config;
        GpuSceneUploadStats stats;
        BatchState batchState = BatchState::None;
        u32 batchGeneration = 0;
        u32 currentSegment = 0xffffffffu;
        u32 nextSegment = 0;
        u32 requestedUpdateCount = 0;
        u64 usedBytes = 0;
        u64 payloadBytes = 0;
        u64 supersededBytes = 0;

        void ResetBatch() noexcept
        {
            requests.Clear();
            uniqueRequests.Clear();
            updates.Clear();
            copies.Clear();
            affectedPages.Clear();
            initialPublications.Clear();
            batchState = BatchState::None;
            currentSegment = 0xffffffffu;
            requestedUpdateCount = 0;
            usedBytes = 0;
            payloadBytes = 0;
            supersededBytes = 0;
        }

        [[nodiscard]] bool AddAffectedPage(const rhi::BufferRef page) noexcept
        {
            for (const rhi::BufferRef existing : affectedPages)
                if (existing == page) return true;
            if (affectedPages.Size() >= config.maximumCopiesPerBatch) return false;
            affectedPages.PushBack(page);
            return true;
        }

        [[nodiscard]] bool AddCopy(const PlannedCopy copy) noexcept
        {
            if (!copies.Empty())
            {
                PlannedCopy& previous = copies.Back();
                if (previous.destination == copy.destination &&
                    previous.destinationOffset + previous.size == copy.destinationOffset &&
                    previous.sourceOffset + previous.size == copy.sourceOffset)
                {
                    previous.size += copy.size;
                    return true;
                }
            }
            if (copies.Size() >= config.maximumCopiesPerBatch) return false;
            copies.PushBack(copy);
            return true;
        }
    };

    GpuSceneUploader::~GpuSceneUploader()
    {
        if (m_impl != nullptr) static_cast<void>(Shutdown());
    }

    bool GpuSceneUploader::Initialize(GpuSceneTables& tables, GpuSceneLifetime& lifetime,
                                      const GpuSceneUploadConfig& config,
                                      GpuSceneUploadFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl != nullptr)
            return Fail(failure, GpuSceneUploadFailureCode::AlreadyInitialized,
                        "GPU Scene uploader is already initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, GpuSceneUploadFailureCode::WrongThread,
                        "GPU Scene uploader must initialize on the main thread");
        if (!tables.IsInitialized() || !lifetime.IsInitialized() || !rhi::IsInitialized() ||
            config.bytesPerSegment == 0 || config.segmentCount < 2 ||
            config.segmentCount > MaximumGpuSceneUploadSegments || config.maximumUpdatesPerBatch == 0 ||
            config.maximumCopiesPerBatch < config.maximumUpdatesPerBatch)
            return Fail(failure, GpuSceneUploadFailureCode::InvalidConfiguration,
                        "GPU Scene uploader configuration is invalid");

        memory::MemoryBlock block = memory::Allocate(memory::PoolId::Rendering, sizeof(Impl), alignof(Impl));
        if (!block)
            return Fail(failure, GpuSceneUploadFailureCode::InvalidConfiguration,
                        "GPU Scene uploader metadata allocation failed");
        Impl* const impl = ::new (block.address) Impl();
        impl->tables = &tables;
        impl->lifetime = &lifetime;
        impl->config = config;
        impl->requests.Reserve(config.maximumUpdatesPerBatch);
        impl->uniqueRequests.Reserve(config.maximumUpdatesPerBatch);
        impl->updates.Reserve(config.maximumUpdatesPerBatch);
        impl->copies.Reserve(config.maximumCopiesPerBatch);
        impl->affectedPages.Reserve(config.maximumCopiesPerBatch);
        impl->initialPublications.Reserve(config.maximumUpdatesPerBatch);
        m_impl = impl;

        rhi::BufferDesc desc;
        desc.size = config.bytesPerSegment;
        desc.usage = rhi::BufferUsage::CopySource;
        desc.initialState = rhi::ResourceState::CopySource;
        desc.memoryType = rhi::MemoryType::Upload;
        for (u32 index = 0; index < config.segmentCount; ++index)
        {
            Impl::Segment& segment = impl->segments[index];
            rhi::Failure rhiFailure;
            segment.buffer = rhi::CreateBuffer(desc, {}, &rhiFailure);
            if (!segment.buffer)
            {
                static_cast<void>(Shutdown());
                return Fail(failure, GpuSceneUploadFailureCode::RhiFailure,
                            "GPU Scene upload segment creation failed", 0xffffffffu, rhiFailure);
            }
            rhi::SetResourceDebugName(segment.buffer, "GPU Scene Upload Segment");
            segment.mapped = static_cast<u8*>(rhi::LockBuffer(segment.buffer, 0, config.bytesPerSegment, &rhiFailure));
            if (segment.mapped == nullptr)
            {
                static_cast<void>(Shutdown());
                return Fail(failure, GpuSceneUploadFailureCode::RhiFailure,
                            "GPU Scene upload segment mapping failed", 0xffffffffu, rhiFailure);
            }
        }
        return true;
    }

    bool GpuSceneUploader::Shutdown(GpuSceneUploadFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr) return true;
        if (!concurrency::IsMainThread())
            return Fail(failure, GpuSceneUploadFailureCode::WrongThread,
                        "GPU Scene uploader must shutdown on the main thread");

        Impl* const impl = m_impl;
        {
            concurrency::ScopedLock<concurrency::RWLock> guard(impl->batchLock);
            impl->ResetBatch();
        }
        for (u32 index = 0; index < impl->config.segmentCount; ++index)
        {
            Impl::Segment& segment = impl->segments[index];
            if (segment.mapped != nullptr)
            {
                rhi::UnlockBuffer(segment.buffer);
                segment.mapped = nullptr;
            }
            static_cast<void>(rhi::SafeRelease(segment.buffer));
        }
        m_impl = nullptr;
        impl->~Impl();
        memory::MemoryBlock block{impl, sizeof(Impl), memory::PoolId::Rendering};
        memory::Free(block);
        return true;
    }

    bool GpuSceneUploader::IsInitialized() const noexcept
    {
        return m_impl != nullptr;
    }

    bool GpuSceneUploader::Begin(const containers::ArraySpan<const GpuSceneUploadRequest> batchRequests,
                                 containers::ArraySpan<GpuSceneUploadReservation> reservations,
                                 GpuSceneUploadFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, GpuSceneUploadFailureCode::NotInitialized,
                        "GPU Scene uploader is not initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, GpuSceneUploadFailureCode::WrongThread,
                        "GPU Scene upload planning must run on the main thread");

        concurrency::ScopedLock<concurrency::RWLock> guard(m_impl->batchLock);
        if (m_impl->batchState != BatchState::None)
            return Fail(failure, GpuSceneUploadFailureCode::BatchAlreadyOpen,
                        "GPU Scene uploader already has an open batch");
        if (batchRequests.Empty() || batchRequests.Size() != reservations.Size())
            return Fail(failure, GpuSceneUploadFailureCode::InvalidRequest,
                        "GPU Scene upload requests and reservations do not match");
        if (batchRequests.Size() > m_impl->config.maximumUpdatesPerBatch)
            return Fail(failure, GpuSceneUploadFailureCode::UpdateCapacityExceeded,
                        "GPU Scene upload batch exceeds its update capacity");
        for (GpuSceneUploadReservation& reservation : reservations) reservation = {};

        u32 selectedSegment = 0xffffffffu;
        for (u32 attempt = 0; attempt < m_impl->config.segmentCount; ++attempt)
        {
            const u32 index = (m_impl->nextSegment + attempt) % m_impl->config.segmentCount;
            const rhi::GpuFence fence = m_impl->segments[index].completion;
            if (!fence.IsValid() || rhi::IsGpuFenceComplete(fence))
            {
                selectedSegment = index;
                break;
            }
        }
        if (selectedSegment == 0xffffffffu)
        {
            ++m_impl->stats.segmentBusyEvents;
            return Fail(failure, GpuSceneUploadFailureCode::StagingExhausted,
                        "every GPU Scene upload segment is still in flight");
        }

        m_impl->ResetBatch();
        m_impl->currentSegment = selectedSegment;
        m_impl->requestedUpdateCount = batchRequests.Size();
        for (u32 requestIndex = 0; requestIndex < batchRequests.Size(); ++requestIndex)
        {
            const GpuSceneUploadRequest request = batchRequests[requestIndex];
            const GpuSceneAllocationState state = m_impl->lifetime->State(request.allocation);
            if (!request.allocation.IsValid() || request.elementCount == 0 ||
                request.allocationOffset > request.allocation.count ||
                request.elementCount > request.allocation.count - request.allocationOffset)
            {
                m_impl->ResetBatch();
                return Fail(failure, GpuSceneUploadFailureCode::InvalidRequest,
                            "GPU Scene upload request exceeds its allocation", requestIndex);
            }
            if (state != GpuSceneAllocationState::Allocated && state != GpuSceneAllocationState::Active)
            {
                m_impl->ResetBatch();
                return Fail(failure, GpuSceneUploadFailureCode::InvalidAllocationState,
                            "GPU Scene upload request does not reference allocated or active data", requestIndex);
            }
            if (state == GpuSceneAllocationState::Allocated &&
                (request.allocationOffset != 0 || request.elementCount != request.allocation.count))
            {
                m_impl->ResetBatch();
                return Fail(failure, GpuSceneUploadFailureCode::InvalidRequest,
                            "initial GPU Scene publication must cover the complete allocation", requestIndex);
            }
            const GpuSceneTableStats table = m_impl->tables->GetTableStats(request.allocation.table);
            if (table.elementStride == 0)
            {
                m_impl->ResetBatch();
                return Fail(failure, GpuSceneUploadFailureCode::InvalidRequest,
                            "GPU Scene upload request references an invalid table", requestIndex);
            }
            m_impl->requests.PushBack({request.allocation, requestIndex,
                                       request.allocation.first + request.allocationOffset,
                                       request.elementCount, table.elementStride,
                                       state == GpuSceneAllocationState::Allocated});
        }

        std::sort(m_impl->requests.Begin(), m_impl->requests.End(),
                  [](const PlannedRequest& left, const PlannedRequest& right) noexcept
                  {
                      if (left.allocation.table != right.allocation.table)
                          return left.allocation.table < right.allocation.table;
                      if (left.first != right.first) return left.first < right.first;
                      if (left.count != right.count) return left.count < right.count;
                      return left.request < right.request;
                  });

        for (u32 index = 0; index < m_impl->requests.Size();)
        {
            u32 end = index + 1u;
            while (end < m_impl->requests.Size() && SameDestination(m_impl->requests[index], m_impl->requests[end])) ++end;
            const PlannedRequest selected = m_impl->requests[end - 1u];
            for (u32 superseded = index; superseded + 1u < end; ++superseded)
                m_impl->supersededBytes += static_cast<u64>(m_impl->requests[superseded].count) *
                                           m_impl->requests[superseded].stride;
            if (!m_impl->uniqueRequests.Empty())
            {
                const PlannedRequest& previous = m_impl->uniqueRequests.Back();
                const u64 previousEnd = static_cast<u64>(previous.first) + previous.count;
                if (previous.allocation.table == selected.allocation.table && selected.first < previousEnd)
                {
                    m_impl->ResetBatch();
                    return Fail(failure, GpuSceneUploadFailureCode::OverlappingUpdates,
                                "GPU Scene upload batch contains partially overlapping destinations", selected.request);
                }
            }
            m_impl->uniqueRequests.PushBack(selected);
            index = end;
        }

        u64 stagingOffset = 0;
        for (const PlannedRequest request : m_impl->uniqueRequests)
        {
            const u64 size = static_cast<u64>(request.count) * request.stride;
            const u64 alignment = request.stride < 16u ? request.stride : 16u;
            stagingOffset = AlignUp(stagingOffset, alignment);
            if (stagingOffset > m_impl->config.bytesPerSegment || size > m_impl->config.bytesPerSegment - stagingOffset)
            {
                m_impl->ResetBatch();
                return Fail(failure, GpuSceneUploadFailureCode::StagingExhausted,
                            "GPU Scene upload batch exceeds one staging segment", request.request);
            }

            const u32 updateIndex = m_impl->updates.Size();
            m_impl->updates.PushBack({request.allocation, request.request, request.first, request.count,
                                      stagingOffset, size, false, request.initialPublication});
            reservations[request.request] = {m_impl->segments[selectedSegment].mapped + stagingOffset,
                                             size, updateIndex, m_impl->batchGeneration + 1u};
            if (request.initialPublication) m_impl->initialPublications.PushBack(request.allocation);

            u32 remaining = request.count;
            u32 element = request.first;
            u64 sourceOffset = stagingOffset;
            while (remaining != 0)
            {
                GpuSceneElementAddress destination;
                if (!m_impl->tables->Resolve(request.allocation.table, element, destination))
                {
                    m_impl->ResetBatch();
                    return Fail(failure, GpuSceneUploadFailureCode::InvalidRequest,
                                "GPU Scene upload destination is not materialized", request.request);
                }
                const GpuSceneTableStats table = m_impl->tables->GetTableStats(request.allocation.table);
                const u32 pageRemaining = table.elementsPerPage - destination.element;
                const u32 copiedElements = remaining < pageRemaining ? remaining : pageRemaining;
                const u64 copiedBytes = static_cast<u64>(copiedElements) * request.stride;
                if (!m_impl->AddAffectedPage(destination.buffer) ||
                    !m_impl->AddCopy({destination.buffer, destination.byteOffset, sourceOffset, copiedBytes}))
                {
                    m_impl->ResetBatch();
                    return Fail(failure, GpuSceneUploadFailureCode::CopyCapacityExceeded,
                                "GPU Scene upload batch exceeds its copy capacity", request.request);
                }
                element += copiedElements;
                remaining -= copiedElements;
                sourceOffset += copiedBytes;
            }
            stagingOffset += size;
            m_impl->payloadBytes += size;
        }

        ++m_impl->batchGeneration;
        if (m_impl->batchGeneration == 0) ++m_impl->batchGeneration;
        for (GpuSceneUploadReservation& reservation : reservations)
            if (reservation.destination != nullptr) reservation.batch = m_impl->batchGeneration;
        m_impl->usedBytes = stagingOffset;
        m_impl->batchState = BatchState::Open;
        return true;
    }

    bool GpuSceneUploader::Complete(const GpuSceneUploadReservation reservation,
                                    GpuSceneUploadFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, GpuSceneUploadFailureCode::NotInitialized, "GPU Scene uploader is not initialized");
        concurrency::ScopedLock<concurrency::RWLock> guard(m_impl->batchLock);
        if (m_impl->batchState != BatchState::Open)
            return Fail(failure, GpuSceneUploadFailureCode::NoOpenBatch, "GPU Scene uploader has no completable batch");
        if (!reservation.IsValid() || reservation.batch != m_impl->batchGeneration || reservation.ticket >= m_impl->updates.Size())
            return Fail(failure, GpuSceneUploadFailureCode::InvalidRequest, "GPU Scene upload reservation is stale");
        PlannedUpdate& update = m_impl->updates[reservation.ticket];
        if (reservation.destination != m_impl->segments[m_impl->currentSegment].mapped + update.stagingOffset ||
            reservation.size != update.size || update.ready)
            return Fail(failure, GpuSceneUploadFailureCode::InvalidRequest,
                        "GPU Scene upload reservation does not match its planned update", update.request);
        update.ready = true;
        return true;
    }

    bool GpuSceneUploader::Submit(GpuSceneUploadResult& result, GpuSceneUploadFailure* const failure) noexcept
    {
        ClearFailure(failure);
        result = {};
        if (m_impl == nullptr)
            return Fail(failure, GpuSceneUploadFailureCode::NotInitialized, "GPU Scene uploader is not initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, GpuSceneUploadFailureCode::WrongThread, "GPU Scene upload submission must run on the main thread");
        {
            concurrency::ScopedLock<concurrency::RWLock> guard(m_impl->batchLock);
            if (m_impl->batchState != BatchState::Open)
                return Fail(failure, GpuSceneUploadFailureCode::NoOpenBatch, "GPU Scene uploader has no open batch");
            for (const PlannedUpdate& update : m_impl->updates)
                if (!update.ready)
                    return Fail(failure, GpuSceneUploadFailureCode::BatchNotReady,
                                "GPU Scene upload batch contains an incomplete reservation", update.request);
            for (const PlannedUpdate& update : m_impl->updates)
            {
                const GpuSceneAllocationState expected = update.initialPublication
                                                             ? GpuSceneAllocationState::Allocated
                                                             : GpuSceneAllocationState::Active;
                if (m_impl->lifetime->State(update.allocation) != expected)
                {
                    ++m_impl->stats.rejectedOperations;
                    m_impl->ResetBatch();
                    return Fail(failure, GpuSceneUploadFailureCode::InvalidAllocationState,
                                "GPU Scene allocation changed state before upload submission", update.request);
                }
            }
            m_impl->batchState = BatchState::Submitting;
        }

        rhi::Failure rhiFailure;
        rhi::CommandListRef commandList = rhi::CreateCommandList(rhi::CommandListType::CopySync, 0x475055534355504cull, &rhiFailure);
        bool recorded = commandList.IsValid() && rhi::BindCommandList(commandList, &rhiFailure);
        const rhi::ResourceState shaderRead = rhi::ResourceState::ShaderResourceGraphics | rhi::ResourceState::ShaderResourceCompute;
        if (recorded)
            for (const rhi::BufferRef page : m_impl->affectedPages)
                if (!rhi::TransitionBuffer(page, rhi::ResourceState::Unknown, rhi::ResourceState::CopyDestination, &rhiFailure))
                { recorded = false; break; }
        if (recorded)
        {
            const rhi::BufferRef source = m_impl->segments[m_impl->currentSegment].buffer;
            for (const PlannedCopy copy : m_impl->copies)
                if (!rhi::CopyBuffer(copy.destination, copy.destinationOffset, source, copy.sourceOffset, copy.size, &rhiFailure))
                { recorded = false; break; }
        }
        if (recorded)
            for (const rhi::BufferRef page : m_impl->affectedPages)
                if (!rhi::TransitionBuffer(page, rhi::ResourceState::Unknown, shaderRead, &rhiFailure))
                { recorded = false; break; }
        rhi::UnbindCommandList();
        if (!recorded)
        {
            if (commandList.IsValid()) rhi::DiscardCommandList(commandList);
            concurrency::ScopedLock<concurrency::RWLock> guard(m_impl->batchLock);
            ++m_impl->stats.rejectedOperations;
            m_impl->ResetBatch();
            return Fail(failure, GpuSceneUploadFailureCode::RhiFailure,
                        "GPU Scene upload command recording failed", 0xffffffffu, rhiFailure);
        }

        rhi::GpuFence completion;
        const rhi::CommandListRef submission[] = {commandList};
        if (!rhi::CloseAndSubmitCommandLists("GPU Scene sparse publication", {submission, 1},
                                             rhi::CommandListSyncType::None, completion, &rhiFailure))
        {
            rhi::DiscardCommandList(commandList);
            concurrency::ScopedLock<concurrency::RWLock> guard(m_impl->batchLock);
            ++m_impl->stats.rejectedOperations;
            m_impl->ResetBatch();
            return Fail(failure, GpuSceneUploadFailureCode::RhiFailure,
                        "GPU Scene upload submission failed", 0xffffffffu, rhiFailure);
        }

        if (!m_impl->initialPublications.Empty())
        {
            GpuSceneLifetimeFailure lifetimeFailure;
            if (!m_impl->lifetime->CommitInitialPublications(
                    {m_impl->initialPublications.TypedData(), m_impl->initialPublications.Size()}, &lifetimeFailure))
            {
                concurrency::ScopedLock<concurrency::RWLock> guard(m_impl->batchLock);
                ++m_impl->stats.rejectedOperations;
                m_impl->segments[m_impl->currentSegment].completion = completion;
                m_impl->ResetBatch();
                return Fail(failure, GpuSceneUploadFailureCode::LifetimeFailure,
                            "GPU Scene initial publication commit failed", 0xffffffffu, {}, lifetimeFailure);
            }
        }

        result = {completion, m_impl->requestedUpdateCount, m_impl->updates.Size(), m_impl->copies.Size(),
                  m_impl->affectedPages.Size(), m_impl->payloadBytes, m_impl->supersededBytes};
        {
            concurrency::ScopedLock<concurrency::RWLock> guard(m_impl->batchLock);
            m_impl->segments[m_impl->currentSegment].completion = completion;
            m_impl->nextSegment = (m_impl->currentSegment + 1u) % m_impl->config.segmentCount;
            ++m_impl->stats.batchesSubmitted;
            m_impl->stats.requestedUpdates += result.requestedUpdates;
            m_impl->stats.uniqueUpdates += result.uniqueUpdates;
            m_impl->stats.copiesRecorded += result.copyCount;
            m_impl->stats.bytesUploaded += result.uploadedBytes;
            m_impl->stats.bytesSuperseded += result.supersededBytes;
            m_impl->ResetBatch();
        }
        return true;
    }

    bool GpuSceneUploader::Cancel(GpuSceneUploadFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, GpuSceneUploadFailureCode::NotInitialized, "GPU Scene uploader is not initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, GpuSceneUploadFailureCode::WrongThread, "GPU Scene upload cancellation must run on the main thread");
        concurrency::ScopedLock<concurrency::RWLock> guard(m_impl->batchLock);
        if (m_impl->batchState == BatchState::None) return true;
        if (m_impl->batchState != BatchState::Open)
            return Fail(failure, GpuSceneUploadFailureCode::BatchNotReady, "GPU Scene upload batch is already submitting");
        m_impl->ResetBatch();
        return true;
    }

    GpuSceneUploadStats GpuSceneUploader::GetStats() const noexcept
    {
        if (m_impl == nullptr) return {};
        concurrency::ScopedSharedLock<concurrency::RWLock> guard(m_impl->batchLock);
        return m_impl->stats;
    }
} // namespace vanguard::rendering
