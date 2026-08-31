#include <vanguard/rendering/geometry_upload.hpp>

#include <vanguard/concurrency/synchronization.hpp>
#include <vanguard/concurrency/thread.hpp>
#include <vanguard/memory/memory.hpp>

#include <algorithm>
#include <new>

namespace vanguard::rendering
{
    namespace
    {
        enum class BatchState : u8
        {
            None,
            Open,
            Submitting
        };

        struct PlannedUpload
        {
            GeometryReservation geometry;
            GeometryUploadReservation staging;
            bool ready = false;
        };

        struct PlannedPiece
        {
            rhi::BufferRef destination;
            rhi::ResourceState finalState = rhi::ResourceState::Unknown;
            u64 destinationOffset = 0;
            u64 sourceOffset = 0;
            u64 size = 0;
            u32 request = 0;
            u32 binding = 0;
            bool indices = false;
        };

        struct PlannedCopy
        {
            rhi::BufferRef destination;
            u64 destinationOffset = 0;
            u64 sourceOffset = 0;
            u64 size = 0;
        };

        struct AffectedBuffer
        {
            rhi::BufferRef buffer;
            rhi::ResourceState finalState = rhi::ResourceState::Unknown;
        };

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

        void ClearFailure(GeometryUploadFailure* const failure) noexcept
        {
            if (failure != nullptr)
                *failure = {};
        }

        [[nodiscard]] bool Fail(GeometryUploadFailure* const failure, const GeometryUploadFailureCode code, const char* const message,
                                const u32 request = 0xffffffffu, const rhi::Failure& rhiFailure = {},
                                const GeometryAllocatorFailure& allocatorFailure = {}) noexcept
        {
            if (failure != nullptr)
                *failure = {code, request, message, rhiFailure, allocatorFailure};
            return false;
        }

        [[nodiscard]] bool SameReservation(const GeometryUploadReservation& left, const GeometryUploadReservation& right) noexcept
        {
            if (left.bindingCount != right.bindingCount || left.ticket != right.ticket || left.batch != right.batch ||
                left.indices.destination != right.indices.destination || left.indices.size != right.indices.size)
                return false;
            for (u32 binding = 0; binding < left.bindingCount; ++binding)
                if (left.vertexStreams[binding].destination != right.vertexStreams[binding].destination ||
                    left.vertexStreams[binding].size != right.vertexStreams[binding].size)
                    return false;
            return true;
        }
    } // namespace

    struct GeometryUploader::Impl
    {
        struct Segment
        {
            rhi::BufferRef buffer;
            u8* mapped = nullptr;
            rhi::GpuFence completion;
            u64 submittedBytes = 0;
        };

        Impl() noexcept
            : uploads(memory::pools::Rendering::GetInstance()), pieces(memory::pools::Rendering::GetInstance()),
              copies(memory::pools::Rendering::GetInstance()), affectedBuffers(memory::pools::Rendering::GetInstance()),
              commitReservations(memory::pools::Rendering::GetInstance())
        {
        }

        Segment segments[MaximumGeometryUploadSegments];
        containers::DynamicArray<PlannedUpload> uploads;
        containers::DynamicArray<PlannedPiece> pieces;
        containers::DynamicArray<PlannedCopy> copies;
        containers::DynamicArray<AffectedBuffer> affectedBuffers;
        containers::DynamicArray<GeometryReservation> commitReservations;
        mutable concurrency::RWLock batchLock;
        GeometryAllocator* allocator = nullptr;
        GeometryUploadConfig config;
        GeometryUploadStats stats;
        BatchState batchState = BatchState::None;
        u32 batchGeneration = 0;
        u32 currentSegment = 0xffffffffu;
        u32 nextSegment = 0;
        rhi::BufferRef currentSource;
        u8* currentMapped = nullptr;
        u64 currentCapacity = 0;
        bool currentSourceIsOverflow = false;
        u64 usedBytes = 0;
        u64 payloadBytes = 0;

        void ResetBatch() noexcept
        {
            if (currentSourceIsOverflow && currentSource.IsValid())
            {
                if (currentMapped != nullptr)
                    rhi::UnlockBuffer(currentSource);
                static_cast<void>(rhi::SafeRelease(currentSource));
            }
            uploads.Clear();
            pieces.Clear();
            copies.Clear();
            affectedBuffers.Clear();
            commitReservations.Clear();
            batchState = BatchState::None;
            currentSegment = 0xffffffffu;
            currentSource = {};
            currentMapped = nullptr;
            currentCapacity = 0;
            currentSourceIsOverflow = false;
            usedBytes = 0;
            payloadBytes = 0;
        }

        [[nodiscard]] bool AddAffectedBuffer(const rhi::BufferRef buffer, const rhi::ResourceState finalState) noexcept
        {
            for (const AffectedBuffer affected : affectedBuffers)
                if (affected.buffer == buffer)
                    return affected.finalState == finalState;
            if (affectedBuffers.Size() >= config.maximumCopiesPerBatch)
                return false;
            affectedBuffers.PushBack({buffer, finalState});
            return true;
        }

        [[nodiscard]] bool AddCopy(const PlannedCopy copy) noexcept
        {
            if (!copies.Empty())
            {
                PlannedCopy& previous = copies.Back();
                if (previous.destination == copy.destination && previous.destinationOffset + previous.size == copy.destinationOffset &&
                    previous.sourceOffset + previous.size == copy.sourceOffset)
                {
                    previous.size += copy.size;
                    return true;
                }
            }
            if (copies.Size() >= config.maximumCopiesPerBatch)
                return false;
            copies.PushBack(copy);
            return true;
        }
    };

    GeometryUploader::~GeometryUploader()
    {
        if (m_impl != nullptr)
            static_cast<void>(Shutdown());
    }

    bool GeometryUploader::Initialize(GeometryAllocator& allocator, const GeometryUploadConfig& config,
                                      GeometryUploadFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl != nullptr)
            return Fail(failure, GeometryUploadFailureCode::AlreadyInitialized, "geometry uploader is already initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, GeometryUploadFailureCode::WrongThread, "geometry uploader must initialize on the main thread");
        if (!allocator.IsInitialized() || !rhi::IsInitialized() || config.bytesPerSegment == 0 || config.maximumOverflowBytes < config.bytesPerSegment || config.segmentCount != 3 ||
            config.segmentCount > MaximumGeometryUploadSegments || config.maximumGeometriesPerBatch == 0 ||
            config.maximumCopiesPerBatch < config.maximumGeometriesPerBatch || config.stagingAlignment == 0)
            return Fail(failure, GeometryUploadFailureCode::InvalidConfiguration, "geometry uploader configuration is invalid");

        memory::MemoryBlock block = memory::Allocate(memory::PoolId::Rendering, sizeof(Impl), alignof(Impl));
        if (!block)
            return Fail(failure, GeometryUploadFailureCode::InvalidConfiguration, "geometry uploader metadata allocation failed");
        Impl* const impl = new (block.address) Impl();
        impl->allocator = &allocator;
        impl->config = config;
        impl->uploads.Reserve(config.maximumGeometriesPerBatch);
        impl->pieces.Reserve(config.maximumCopiesPerBatch);
        impl->copies.Reserve(config.maximumCopiesPerBatch);
        impl->affectedBuffers.Reserve(config.maximumCopiesPerBatch);
        impl->commitReservations.Reserve(config.maximumGeometriesPerBatch);
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
                return Fail(failure, GeometryUploadFailureCode::RhiFailure, "geometry upload segment creation failed", 0xffffffffu, rhiFailure);
            }
            rhi::SetResourceDebugName(segment.buffer, "Geometry Upload Segment");
            segment.mapped = static_cast<u8*>(rhi::LockBuffer(segment.buffer, 0, config.bytesPerSegment, &rhiFailure));
            if (segment.mapped == nullptr)
            {
                static_cast<void>(Shutdown());
                return Fail(failure, GeometryUploadFailureCode::RhiFailure, "geometry upload segment mapping failed", 0xffffffffu, rhiFailure);
            }
        }
        impl->stats.stagingBytesCommitted = config.bytesPerSegment * config.segmentCount;
        return true;
    }

    bool GeometryUploader::Shutdown(GeometryUploadFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return true;
        if (!concurrency::IsMainThread())
            return Fail(failure, GeometryUploadFailureCode::WrongThread, "geometry uploader must shutdown on the main thread");
        Impl* const impl = m_impl;
        {
            concurrency::ScopedSharedLock<concurrency::RWLock> guard(impl->batchLock);
            if (impl->batchState != BatchState::None)
                return Fail(failure, GeometryUploadFailureCode::BatchAlreadyOpen,
                            "geometry uploader cannot shut down while an upload batch owns staging reservations");
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

    bool GeometryUploader::IsInitialized() const noexcept
    {
        return m_impl != nullptr;
    }

    bool GeometryUploader::Begin(const containers::ArraySpan<const GeometryUploadRequest> requests,
                                 containers::ArraySpan<GeometryUploadReservation> reservations, GeometryUploadFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, GeometryUploadFailureCode::NotInitialized, "geometry uploader is not initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, GeometryUploadFailureCode::WrongThread, "geometry upload batches must begin on the main thread");
        concurrency::ScopedLock<concurrency::RWLock> guard(m_impl->batchLock);
        if (m_impl->batchState != BatchState::None)
            return Fail(failure, GeometryUploadFailureCode::BatchAlreadyOpen, "geometry uploader already has an open batch");
        if (requests.Empty() || requests.Size() != reservations.Size())
            return Fail(failure, GeometryUploadFailureCode::InvalidRequest, "geometry upload requests and reservations do not match");
        if (requests.Size() > m_impl->config.maximumGeometriesPerBatch)
            return Fail(failure, GeometryUploadFailureCode::GeometryCapacityExceeded, "geometry upload batch exceeds its geometry capacity");
        for (GeometryUploadReservation& reservation : reservations)
            reservation = {};

        m_impl->ResetBatch();
        for (u32 requestIndex = 0; requestIndex < requests.Size(); ++requestIndex)
        {
            const GeometryReservation geometry = requests[requestIndex].geometry;
            if (!m_impl->allocator->ValidateReservation(geometry))
            {
                m_impl->ResetBatch();
                return Fail(failure, GeometryUploadFailureCode::InvalidAllocationState,
                            "geometry upload request is not the exact live reservation", requestIndex);
            }
            for (u32 previous = 0; previous < requestIndex; ++previous)
                if (requests[previous].geometry.allocation == geometry.allocation)
                {
                    m_impl->ResetBatch();
                    return Fail(failure, GeometryUploadFailureCode::DuplicateAllocation,
                                "geometry upload batch contains the same allocation twice", requestIndex);
                }

            GeometryVertexArenaView vertexArena;
            GeometryIndexArenaView indexArena;
            if (!m_impl->allocator->GetVertexArena(geometry.vertex.arena, vertexArena) ||
                !m_impl->allocator->GetIndexArena(geometry.index.arena, indexArena) || vertexArena.bindingCount == 0)
            {
                m_impl->ResetBatch();
                return Fail(failure, GeometryUploadFailureCode::InvalidAllocationState, "geometry upload arena identity is stale", requestIndex);
            }
            const u32 pieceCount = vertexArena.bindingCount + 1u;
            if (pieceCount > m_impl->config.maximumCopiesPerBatch ||
                m_impl->pieces.Size() > m_impl->config.maximumCopiesPerBatch - pieceCount)
            {
                m_impl->ResetBatch();
                return Fail(failure, GeometryUploadFailureCode::CopyCapacityExceeded, "geometry upload batch exceeds its copy capacity", requestIndex);
            }

            m_impl->uploads.PushBack({geometry, {}, false});
            m_impl->commitReservations.PushBack(geometry);
            for (u32 binding = 0; binding < vertexArena.bindingCount; ++binding)
            {
                const u64 stride = vertexArena.bindings[binding].stride;
                const u64 size = static_cast<u64>(geometry.vertex.vertexCount) * stride;
                const u64 destinationOffset = static_cast<u64>(geometry.vertex.firstVertex) * stride;
                m_impl->pieces.PushBack({vertexArena.buffers[binding], rhi::ResourceState::VertexBuffer, destinationOffset, 0, size, requestIndex,
                                         binding, false});
            }
            const u64 indexStride = geometry.index.format == rhi::IndexFormat::UInt32 ? 4u : 2u;
            m_impl->pieces.PushBack({indexArena.buffer, rhi::ResourceState::IndexBuffer, geometry.index.ByteOffset(), 0,
                                     static_cast<u64>(geometry.index.indexCount) * indexStride, requestIndex, 0, true});
        }

        std::sort(m_impl->pieces.Begin(), m_impl->pieces.End(), [](const PlannedPiece& left, const PlannedPiece& right) noexcept
                  {
                      if (left.destination.index != right.destination.index)
                          return left.destination.index < right.destination.index;
                      if (left.destination.generation != right.destination.generation)
                          return left.destination.generation < right.destination.generation;
                      if (left.destinationOffset != right.destinationOffset)
                          return left.destinationOffset < right.destinationOffset;
                      return left.request < right.request;
                  });

        u64 stagingOffset = 0;
        for (u32 pieceIndex = 0; pieceIndex < m_impl->pieces.Size(); ++pieceIndex)
        {
            PlannedPiece& piece = m_impl->pieces[pieceIndex];
            if (pieceIndex != 0)
            {
                const PlannedPiece& previous = m_impl->pieces[pieceIndex - 1u];
                if (previous.destination == piece.destination && piece.destinationOffset < previous.destinationOffset + previous.size)
                {
                    m_impl->ResetBatch();
                    return Fail(failure, GeometryUploadFailureCode::InvalidRequest, "geometry upload destinations overlap", piece.request);
                }
            }
            if (!AlignUp(stagingOffset, m_impl->config.stagingAlignment, stagingOffset))
            {
                m_impl->ResetBatch();
                return Fail(failure, GeometryUploadFailureCode::ArithmeticOverflow, "geometry staging alignment overflowed", piece.request);
            }
            if (stagingOffset > m_impl->config.maximumOverflowBytes || piece.size > m_impl->config.maximumOverflowBytes - stagingOffset)
            {
                m_impl->ResetBatch();
                return Fail(failure, GeometryUploadFailureCode::StagingExhausted, "geometry upload batch exceeds its bounded overflow capacity", piece.request);
            }
            piece.sourceOffset = stagingOffset;
            if (!m_impl->AddAffectedBuffer(piece.destination, piece.finalState) ||
                !m_impl->AddCopy({piece.destination, piece.destinationOffset, piece.sourceOffset, piece.size}))
            {
                m_impl->ResetBatch();
                return Fail(failure, GeometryUploadFailureCode::CopyCapacityExceeded, "geometry upload batch exceeds its copy capacity", piece.request);
            }
            stagingOffset += piece.size;
            m_impl->payloadBytes += piece.size;
        }

        if (stagingOffset <= m_impl->config.bytesPerSegment)
        {
            u32 selectedSegment = 0xffffffffu;
            for (u32 attempt = 0; attempt < m_impl->config.segmentCount; ++attempt)
            {
                const u32 index = (m_impl->nextSegment + attempt) % m_impl->config.segmentCount;
                const rhi::GpuFence completion = m_impl->segments[index].completion;
                if (!completion.IsValid() || rhi::IsGpuFenceComplete(completion))
                {
                    selectedSegment = index;
                    break;
                }
            }
            if (selectedSegment == 0xffffffffu)
            {
                ++m_impl->stats.segmentBusyEvents;
                m_impl->ResetBatch();
                return Fail(failure, GeometryUploadFailureCode::StagingExhausted, "all regular geometry upload segments are still in flight");
            }
            m_impl->currentSegment = selectedSegment;
            m_impl->currentSource = m_impl->segments[selectedSegment].buffer;
            m_impl->currentMapped = m_impl->segments[selectedSegment].mapped;
            m_impl->currentCapacity = m_impl->config.bytesPerSegment;
        }
        else
        {
            rhi::BufferDesc desc;
            desc.size = stagingOffset;
            desc.usage = rhi::BufferUsage::CopySource;
            desc.initialState = rhi::ResourceState::CopySource;
            desc.memoryType = rhi::MemoryType::Upload;
            rhi::Failure rhiFailure;
            m_impl->currentSource = rhi::CreateBuffer(desc, {}, &rhiFailure);
            if (!m_impl->currentSource.IsValid())
            {
                m_impl->ResetBatch();
                return Fail(failure, GeometryUploadFailureCode::RhiFailure, "geometry overflow upload buffer creation failed", 0xffffffffu, rhiFailure);
            }
            rhi::SetResourceDebugName(m_impl->currentSource, "Geometry Overflow Upload");
            m_impl->currentMapped = static_cast<u8*>(rhi::LockBuffer(m_impl->currentSource, 0, stagingOffset, &rhiFailure));
            if (m_impl->currentMapped == nullptr)
            {
                m_impl->currentSourceIsOverflow = true;
                m_impl->ResetBatch();
                return Fail(failure, GeometryUploadFailureCode::RhiFailure, "geometry overflow upload buffer mapping failed", 0xffffffffu, rhiFailure);
            }
            m_impl->currentCapacity = stagingOffset;
            m_impl->currentSourceIsOverflow = true;
            ++m_impl->stats.overflowBatches;
            m_impl->stats.overflowBytes += stagingOffset;
        }

        for (const PlannedPiece& piece : m_impl->pieces)
        {
            GeometryUploadReservation& reservation = reservations[piece.request];
            GeometryUploadSlice& slice = piece.indices ? reservation.indices : reservation.vertexStreams[piece.binding];
            slice = {m_impl->currentMapped + piece.sourceOffset, piece.size};
        }

        ++m_impl->batchGeneration;
        if (m_impl->batchGeneration == 0)
            ++m_impl->batchGeneration;
        for (u32 request = 0; request < reservations.Size(); ++request)
        {
            reservations[request].bindingCount = [&]() noexcept
            {
                u32 count = 0;
                while (count < rhi::MaximumVertexBindings && reservations[request].vertexStreams[count].IsValid())
                    ++count;
                return count;
            }();
            reservations[request].ticket = request;
            reservations[request].batch = m_impl->batchGeneration;
            m_impl->uploads[request].staging = reservations[request];
        }
        m_impl->usedBytes = stagingOffset;
        m_impl->batchState = BatchState::Open;
        return true;
    }

    bool GeometryUploader::Complete(const GeometryUploadReservation reservation, GeometryUploadFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, GeometryUploadFailureCode::NotInitialized, "geometry uploader is not initialized");
        concurrency::ScopedLock<concurrency::RWLock> guard(m_impl->batchLock);
        if (m_impl->batchState != BatchState::Open)
            return Fail(failure, GeometryUploadFailureCode::NoOpenBatch, "geometry uploader has no completable batch");
        if (!reservation.IsValid() || reservation.batch != m_impl->batchGeneration || reservation.ticket >= m_impl->uploads.Size())
            return Fail(failure, GeometryUploadFailureCode::InvalidRequest, "geometry upload reservation is stale");
        PlannedUpload& upload = m_impl->uploads[reservation.ticket];
        if (upload.ready || !SameReservation(reservation, upload.staging))
            return Fail(failure, GeometryUploadFailureCode::InvalidRequest, "geometry upload reservation does not match its planned upload",
                        reservation.ticket);
        upload.ready = true;
        return true;
    }

    bool GeometryUploader::Submit(const containers::ArraySpan<GeometryPlacement> placements, GeometryUploadResult& result,
                                  GeometryUploadFailure* const failure) noexcept
    {
        ClearFailure(failure);
        result = {};
        if (m_impl == nullptr)
            return Fail(failure, GeometryUploadFailureCode::NotInitialized, "geometry uploader is not initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, GeometryUploadFailureCode::WrongThread, "geometry upload submission must run on the main thread");
        {
            concurrency::ScopedLock<concurrency::RWLock> guard(m_impl->batchLock);
            if (m_impl->batchState != BatchState::Open)
                return Fail(failure, GeometryUploadFailureCode::NoOpenBatch, "geometry uploader has no open batch");
            if (placements.Size() != m_impl->uploads.Size())
                return Fail(failure, GeometryUploadFailureCode::InvalidRequest, "geometry placement outputs do not match the upload batch");
            for (u32 request = 0; request < m_impl->uploads.Size(); ++request)
            {
                if (!m_impl->uploads[request].ready)
                    return Fail(failure, GeometryUploadFailureCode::BatchNotReady, "geometry upload batch contains an incomplete reservation", request);
                if (!m_impl->allocator->ValidateReservation(m_impl->uploads[request].geometry))
                {
                    ++m_impl->stats.rejectedOperations;
                    m_impl->ResetBatch();
                    return Fail(failure, GeometryUploadFailureCode::InvalidAllocationState,
                                "geometry allocation changed state before upload submission", request);
                }
            }
            m_impl->batchState = BatchState::Submitting;
        }

        rhi::Failure rhiFailure;
        rhi::CommandListRef commandList = rhi::CreateCommandList(rhi::CommandListType::CopySync, 0x47454f4d55504c44ull, &rhiFailure);
        bool recorded = commandList.IsValid() && rhi::BindCommandList(commandList, &rhiFailure);
        if (recorded)
            for (const AffectedBuffer affected : m_impl->affectedBuffers)
                if (!rhi::TransitionBuffer(affected.buffer, rhi::ResourceState::Unknown, rhi::ResourceState::CopyDestination, &rhiFailure))
                {
                    recorded = false;
                    break;
                }
        if (recorded)
        {
            for (const PlannedCopy copy : m_impl->copies)
                if (!rhi::CopyBuffer(copy.destination, copy.destinationOffset, m_impl->currentSource, copy.sourceOffset, copy.size, &rhiFailure))
                {
                    recorded = false;
                    break;
                }
        }
        if (recorded)
            for (const AffectedBuffer affected : m_impl->affectedBuffers)
                if (!rhi::TransitionBuffer(affected.buffer, rhi::ResourceState::Unknown, affected.finalState, &rhiFailure))
                {
                    recorded = false;
                    break;
                }
        rhi::UnbindCommandList();
        if (!recorded)
        {
            if (commandList.IsValid())
                rhi::DiscardCommandList(commandList);
            concurrency::ScopedLock<concurrency::RWLock> guard(m_impl->batchLock);
            ++m_impl->stats.rejectedOperations;
            m_impl->ResetBatch();
            return Fail(failure, GeometryUploadFailureCode::RhiFailure, "geometry upload command recording failed", 0xffffffffu, rhiFailure);
        }

        rhi::GpuFence completion;
        const rhi::CommandListRef submission[] = {commandList};
        if (!rhi::CloseAndSubmitCommandLists("Fixed-function geometry upload", {submission, 1}, rhi::CommandListSyncType::None, completion, &rhiFailure))
        {
            rhi::DiscardCommandList(commandList);
            concurrency::ScopedLock<concurrency::RWLock> guard(m_impl->batchLock);
            ++m_impl->stats.rejectedOperations;
            m_impl->ResetBatch();
            return Fail(failure, GeometryUploadFailureCode::RhiFailure, "geometry upload submission failed", 0xffffffffu, rhiFailure);
        }

        GeometryAllocatorFailure allocatorFailure;
        if (!m_impl->allocator->CommitBatch({m_impl->commitReservations.TypedData(), m_impl->commitReservations.Size()}, placements, &allocatorFailure))
        {
            concurrency::ScopedLock<concurrency::RWLock> guard(m_impl->batchLock);
            ++m_impl->stats.rejectedOperations;
            if (!m_impl->currentSourceIsOverflow)
            {
                m_impl->segments[m_impl->currentSegment].completion = completion;
                m_impl->segments[m_impl->currentSegment].submittedBytes = m_impl->usedBytes;
                m_impl->nextSegment = (m_impl->currentSegment + 1u) % m_impl->config.segmentCount;
            }
            m_impl->ResetBatch();
            return Fail(failure, GeometryUploadFailureCode::AllocatorFailure, "geometry upload commit failed after submission", 0xffffffffu, {},
                        allocatorFailure);
        }

        result = {completion, m_impl->uploads.Size(), m_impl->copies.Size(), m_impl->affectedBuffers.Size(), m_impl->payloadBytes, m_impl->usedBytes};
        {
            concurrency::ScopedLock<concurrency::RWLock> guard(m_impl->batchLock);
            if (!m_impl->currentSourceIsOverflow)
            {
                Impl::Segment& segment = m_impl->segments[m_impl->currentSegment];
                segment.completion = completion;
                segment.submittedBytes = m_impl->usedBytes;
                m_impl->nextSegment = (m_impl->currentSegment + 1u) % m_impl->config.segmentCount;
            }
            ++m_impl->stats.batchesSubmitted;
            m_impl->stats.geometriesUploaded += result.geometryCount;
            m_impl->stats.copiesRecorded += result.copyCount;
            m_impl->stats.bytesUploaded += result.uploadedBytes;
            m_impl->ResetBatch();
        }
        return true;
    }

    bool GeometryUploader::Cancel(GeometryUploadFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, GeometryUploadFailureCode::NotInitialized, "geometry uploader is not initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, GeometryUploadFailureCode::WrongThread, "geometry upload cancellation must run on the main thread");
        concurrency::ScopedLock<concurrency::RWLock> guard(m_impl->batchLock);
        if (m_impl->batchState == BatchState::None)
            return true;
        if (m_impl->batchState != BatchState::Open)
            return Fail(failure, GeometryUploadFailureCode::BatchNotReady, "geometry upload batch is already submitting");
        m_impl->ResetBatch();
        return true;
    }

    GeometryUploadStats GeometryUploader::GetStats() const noexcept
    {
        if (m_impl == nullptr)
            return {};
        concurrency::ScopedSharedLock<concurrency::RWLock> guard(m_impl->batchLock);
        GeometryUploadStats stats = m_impl->stats;
        for (u32 index = 0; index < m_impl->config.segmentCount; ++index)
        {
            const Impl::Segment& segment = m_impl->segments[index];
            if (segment.completion.IsValid() && !rhi::IsGpuFenceComplete(segment.completion))
            {
                ++stats.inFlightSegments;
                stats.inFlightBytes += segment.submittedBytes;
            }
        }
        return stats;
    }
} // namespace vanguard::rendering
