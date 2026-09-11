#include <vanguard/rendering/mesh_lod_geometry_uploader.hpp>

#include <vanguard/concurrency/concurrency.hpp>
#include <vanguard/memory/memory.hpp>
#include <vanguard/meshes/mesh_resource.hpp>
#include <vanguard/jobs/jobs.hpp>
#include <vanguard/system/assert.hpp>

#include <new>
#include <utility>

namespace vanguard::rendering
{
    namespace
    {
        struct PendingPageRead
        {
            u32 page = meshes::InvalidRecordIndex;
            meshes::MeshPageReadRequest request;
        };

        struct UploadEntry
        {
            UploadEntry() noexcept : pages(memory::pools::Rendering::GetInstance()), reads(memory::pools::Rendering::GetInstance()) {}

            resources::WeakResourceHandle resource;
            crypto::Digest256 meshContentFingerprint;
            containers::DynamicArray<u32> pages;
            containers::DynamicArray<PendingPageRead> reads;
            PendingMeshLodGeometry pendingGeometry;
            PreparedMeshLodUpload prepared;
            jobs::Counter preparationCounter;
            MeshGeometryUploadFailure asyncPreparationFailure;
            MeshLodGeometryUploadFailure failure;
            MeshLodGeometryUploadState state = MeshLodGeometryUploadState::Invalid;
            io::AsyncPriority priority = io::eAsyncPriority_Streaming;
            u16 lod = 0xffffu;
            u32 preparationDeferrals = 0;
            u32 interests = 1;
            u64 plannedBytes = 0;
            bool pendingBytesCharged = false;
            bool preparationSucceeded = false;
            bool cancelRequested = false;
        };

        template <typename T, typename... Args> [[nodiscard]] T* AllocateObject(Args&&... args) noexcept
        {
            memory::MemoryBlock block = memory::Allocate(memory::PoolId::Rendering, sizeof(T), alignof(T));
            return block ? ::new (block.address) T(static_cast<Args&&>(args)...) : nullptr;
        }

        template <typename T> void DeleteObject(T* object) noexcept
        {
            if (object == nullptr)
                return;
            object->~T();
            memory::MemoryBlock block{object, sizeof(T), memory::PoolId::Rendering};
            memory::Free(block);
        }

        void ClearFailure(MeshLodGeometryUploadFailure* const failure) noexcept
        {
            if (failure != nullptr)
                *failure = {};
        }

        [[nodiscard]] bool Fail(MeshLodGeometryUploadFailure* const failure, const MeshLodGeometryUploadFailureCode code, const char* const message, const MeshLodGeometryUploadRequestId request = {},
                                const u32 page = meshes::InvalidRecordIndex) noexcept
        {
            if (failure != nullptr)
                *failure = {code, request, page, message};
            return false;
        }

        [[nodiscard]] bool AddChecked(const u64 left, const u64 right, u64& result) noexcept
        {
            if (left > ~u64{0} - right)
                return false;
            result = left + right;
            return true;
        }

        [[nodiscard]] bool IsDeferrable(const MeshGeometryUploadFailure& failure) noexcept
        {
            if (failure.code == MeshGeometryUploadFailureCode::AllocationFailure)
                return failure.allocatorFailure.code == GeometryAllocatorFailureCode::CapacityExceeded;
            if (failure.code != MeshGeometryUploadFailureCode::UploadFailure)
                return false;
            return failure.uploadFailure.code == GeometryUploadFailureCode::BatchAlreadyOpen || failure.uploadFailure.code == GeometryUploadFailureCode::StagingExhausted;
        }
    } // namespace

    PendingMeshLodGeometry::PendingMeshLodGeometry() noexcept
        : pages(memory::pools::Rendering::GetInstance()), geometries(memory::pools::Rendering::GetInstance()), submeshes(memory::pools::Rendering::GetInstance())
    {
    }

    PendingMeshLodGeometry::~PendingMeshLodGeometry()
    {
        VG_ASSERT_MSG(!HasOwnership(), "pending mesh LOD geometry must be published or explicitly aborted before destruction");
    }

    PendingMeshLodGeometry::PendingMeshLodGeometry(PendingMeshLodGeometry&& other) noexcept : PendingMeshLodGeometry()
    {
        *this = std::move(other);
    }

    PendingMeshLodGeometry& PendingMeshLodGeometry::operator=(PendingMeshLodGeometry&& other) noexcept
    {
        if (this != &other)
        {
            VG_ASSERT_MSG(!HasOwnership(), "move assignment cannot overwrite live pending mesh LOD geometry");
            resource = std::move(other.resource);
            meshContentFingerprint = other.meshContentFingerprint;
            pages = std::move(other.pages);
            geometries = std::move(other.geometries);
            submeshes = std::move(other.submeshes);
            copyCompletion = other.copyCompletion;
            lod = other.lod;
            vertexBytes = other.vertexBytes;
            indexBytes = other.indexBytes;
            other.Reset();
        }
        return *this;
    }

    bool PendingMeshLodGeometry::IsValid() const noexcept
    {
        return !meshContentFingerprint.IsEmpty() && lod != 0xffffu && !pages.Empty() && !geometries.Empty() && geometries.Size() == submeshes.Size() && copyCompletion.IsValid();
    }

    bool PendingMeshLodGeometry::HasOwnership() const noexcept
    {
        return !geometries.Empty();
    }

    void PendingMeshLodGeometry::Reset() noexcept
    {
        VG_ASSERT_MSG(!HasOwnership(), "live pending mesh LOD geometry must be explicitly aborted, not reset");
        resource.Reset();
        meshContentFingerprint = {};
        pages.Clear();
        geometries.Clear();
        submeshes.Clear();
        copyCompletion = {};
        lod = 0xffffu;
        vertexBytes = 0;
        indexBytes = 0;
    }

    bool AbortPendingMeshLodGeometry(PendingMeshLodGeometry& geometry, GeometryAllocator& allocator, GeometryAllocatorFailure* const failure) noexcept
    {
        if (!concurrency::IsMainThread())
        {
            if (failure != nullptr)
                *failure = {GeometryAllocatorFailureCode::WrongThread, {}, "pending mesh LOD geometry must retire on the main thread"};
            return false;
        }
        if (!geometry.HasOwnership())
            return true;
        for (const GeometryPlacement placement : geometry.geometries)
        {
            const GeometryAllocationState state = allocator.GetState(placement.allocation);
            if (state != GeometryAllocationState::Active && state != GeometryAllocationState::Retiring)
            {
                if (failure != nullptr)
                    *failure = {GeometryAllocatorFailureCode::InvalidState, placement.allocation, "pending mesh LOD geometry contains a placement that is neither active nor already retiring"};
                return false;
            }
        }
        for (const GeometryPlacement placement : geometry.geometries)
            if (allocator.GetState(placement.allocation) == GeometryAllocationState::Active && !allocator.Retire(placement, failure))
                return false;
        geometry.geometries.Clear();
        geometry.Reset();
        return true;
    }

    struct MeshLodGeometryUploader::Impl
    {
        struct Slot
        {
            UploadEntry* entry = nullptr;
            u32 generation = 1;
        };

        explicit Impl(GeometryAllocator& geometryAllocator, GeometryUploader& geometryUploader, const MeshLodGeometryUploaderConfig& uploaderConfig) noexcept
            : slots(memory::pools::Rendering::GetInstance()), recycledSlots(memory::pools::Rendering::GetInstance()), allocator(&geometryAllocator), uploader(&geometryUploader), config(uploaderConfig)
        {
            slots.Reserve(config.maximumRequests);
            recycledSlots.Reserve(config.maximumRequests);
        }

        [[nodiscard]] UploadEntry* Find(const MeshLodGeometryUploadRequestId id) const noexcept
        {
            return id.IsValid() && id.index < slots.Size() && slots[id.index].generation == id.generation ? slots[id.index].entry : nullptr;
        }

        [[nodiscard]] MeshLodGeometryUploadRequestId Id(const u32 index) const noexcept
        {
            return {index, slots[index].generation};
        }

        void ReleasePendingBytes(UploadEntry& entry) noexcept
        {
            if (!entry.pendingBytesCharged)
                return;
            pendingUploadBytes -= entry.plannedBytes;
            entry.pendingBytesCharged = false;
        }

        void Remove(const u32 index) noexcept
        {
            UploadEntry* const entry = slots[index].entry;
            if (entry == nullptr)
                return;
            ReleasePendingBytes(*entry);
            DeleteObject(entry);
            slots[index].entry = nullptr;
            ++slots[index].generation;
            if (slots[index].generation == 0)
                ++slots[index].generation;
            recycledSlots.PushBack(index);
            --stats.liveRequests;
        }

        void SetFailed(const u32 index, const MeshLodGeometryUploadFailure& failure) noexcept
        {
            UploadEntry& entry = *slots[index].entry;
            for (PendingPageRead& read : entry.reads)
                read.request.Reset();
            entry.reads.Clear();
            ReleasePendingBytes(entry);
            entry.failure = failure;
            entry.failure.request = Id(index);
            entry.state = MeshLodGeometryUploadState::Failed;
            ++stats.requestsFailed;
        }

        containers::DynamicArray<Slot> slots;
        containers::DynamicArray<u32> recycledSlots;
        GeometryAllocator* allocator = nullptr;
        GeometryUploader* uploader = nullptr;
        MeshLodGeometryUploaderConfig config;
        MeshLodGeometryUploaderStats stats;
        u64 pendingUploadBytes = 0;
    };

    MeshLodGeometryUploader::~MeshLodGeometryUploader()
    {
        if (m_impl != nullptr)
            static_cast<void>(Shutdown());
    }

    bool MeshLodGeometryUploader::Initialize(GeometryAllocator& allocator, GeometryUploader& uploader, const MeshLodGeometryUploaderConfig& config, MeshLodGeometryUploadFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl != nullptr)
            return Fail(failure, MeshLodGeometryUploadFailureCode::AlreadyInitialized, "mesh LOD geometry uploader is already initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, MeshLodGeometryUploadFailureCode::WrongThread, "mesh LOD geometry uploader must initialize on the main thread");
        if (!allocator.IsInitialized() || !uploader.IsInitialized() || !jobs::IsInitialized() || config.maximumRequests == 0 || config.maximumLodsPerTick == 0 || config.maximumUploadBytesPerTick == 0 ||
            config.maximumPendingUploadBytes == 0 || config.maximumPreparationDeferrals == 0)
            return Fail(failure, MeshLodGeometryUploadFailureCode::InvalidConfiguration, "mesh LOD geometry uploader configuration is invalid");
        m_impl = AllocateObject<Impl>(allocator, uploader, config);
        if (m_impl == nullptr)
            return Fail(failure, MeshLodGeometryUploadFailureCode::CapacityExceeded, "mesh LOD geometry uploader storage allocation failed");
        return true;
    }

    bool MeshLodGeometryUploader::Shutdown(MeshLodGeometryUploadFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, MeshLodGeometryUploadFailureCode::NotInitialized, "mesh LOD geometry uploader is not initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, MeshLodGeometryUploadFailureCode::WrongThread, "mesh LOD geometry uploader must shut down on the main thread");
        if (m_impl->stats.liveRequests != 0)
            return Fail(failure, MeshLodGeometryUploadFailureCode::LiveRequestsRemain, "mesh LOD geometry uploader still owns pending, failed, or completed requests");
        DeleteObject(m_impl);
        m_impl = nullptr;
        return true;
    }

    void MeshLodGeometryUploader::AbandonDevice() noexcept
    {
        if (m_impl == nullptr)
            return;
        for (Impl::Slot& slot : m_impl->slots)
        {
            UploadEntry* const entry = slot.entry;
            if (entry == nullptr)
                continue;
            if (entry->preparationCounter.IsValid())
                static_cast<void>(entry->preparationCounter.Wait());
            for (PendingPageRead& read : entry->reads)
                read.request.Reset();
            entry->pendingGeometry.geometries.Clear();
            entry->pendingGeometry.Reset();
            entry->prepared.Reset();
            DeleteObject(entry);
            slot.entry = nullptr;
        }
        DeleteObject(m_impl);
        m_impl = nullptr;
    }

    bool MeshLodGeometryUploader::IsInitialized() const noexcept
    {
        return m_impl != nullptr;
    }

    bool MeshLodGeometryUploader::Request(const resources::ResourceHandle& resource, const u16 lod, MeshLodGeometryUploadRequestId& request, const io::AsyncPriority priority,
                                          MeshLodGeometryUploadFailure* const failure) noexcept
    {
        ClearFailure(failure);
        request = {};
        if (m_impl == nullptr)
            return Fail(failure, MeshLodGeometryUploadFailureCode::NotInitialized, "mesh LOD geometry uploader is not initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, MeshLodGeometryUploadFailureCode::WrongThread, "mesh LOD upload requests must be issued on the main thread");
        if (!resource.IsValid() || resource.GetType() != meshes::MeshResourceType)
            return Fail(failure, MeshLodGeometryUploadFailureCode::InvalidArgument, "mesh LOD upload request requires a live mesh resource handle");

        for (u32 index = 0; index < m_impl->slots.Size(); ++index)
        {
            UploadEntry* const entry = m_impl->slots[index].entry;
            if (entry != nullptr && entry->resource.GetPath() == resource.GetPath() && entry->resource.GetGeneration() == resource.GetGeneration() && entry->lod == lod)
            {
                if (entry->interests == 0xffffffffu)
                    return Fail(failure, MeshLodGeometryUploadFailureCode::CapacityExceeded, "mesh LOD upload request interest count overflowed");
                ++entry->interests;
                request = m_impl->Id(index);
                ++m_impl->stats.requestsCoalesced;
                return true;
            }
        }
        if (m_impl->stats.liveRequests >= m_impl->config.maximumRequests)
            return Fail(failure, MeshLodGeometryUploadFailureCode::CapacityExceeded, "mesh LOD upload request capacity is exhausted");

        auto* const mesh = static_cast<meshes::MeshResourceObject*>(resource.Get());
        if (mesh == nullptr || !mesh->IsOpen() || lod >= mesh->GetMetadata().GetLods().Size())
            return Fail(failure, MeshLodGeometryUploadFailureCode::InvalidArgument, "mesh LOD upload request references an invalid LOD or resource object");

        UploadEntry* const entry = AllocateObject<UploadEntry>();
        if (entry == nullptr)
            return Fail(failure, MeshLodGeometryUploadFailureCode::CapacityExceeded, "mesh LOD upload request allocation failed");
        entry->resource = resource.ToWeak();
        entry->meshContentFingerprint = mesh->GetMetadata().GetContentFingerprint();
        entry->lod = lod;
        entry->priority = priority;
        const meshes::Result pageSetResult = mesh->BuildLodUploadSet(lod, entry->pages);
        if (pageSetResult != meshes::Result::Success || entry->pages.Empty())
        {
            DeleteObject(entry);
            if (failure != nullptr)
            {
                failure->code = MeshLodGeometryUploadFailureCode::InvalidArgument;
                failure->message = "mesh LOD upload set could not be derived";
                failure->pageFailure = pageSetResult;
            }
            return false;
        }
        for (const u32 page : entry->pages)
            if (!AddChecked(entry->plannedBytes, mesh->GetMetadata().GetPages()[page].byteSize, entry->plannedBytes))
            {
                DeleteObject(entry);
                return Fail(failure, MeshLodGeometryUploadFailureCode::CapacityExceeded, "mesh LOD upload byte cost overflowed");
            }
        if (entry->plannedBytes > m_impl->config.maximumPendingUploadBytes || m_impl->pendingUploadBytes > m_impl->config.maximumPendingUploadBytes - entry->plannedBytes)
        {
            DeleteObject(entry);
            return Fail(failure, MeshLodGeometryUploadFailureCode::CapacityExceeded, "mesh LOD pending upload byte budget is exhausted");
        }

        entry->reads.Resize(entry->pages.Size());
        if (entry->reads.Size() != entry->pages.Size())
        {
            DeleteObject(entry);
            return Fail(failure, MeshLodGeometryUploadFailureCode::CapacityExceeded, "mesh LOD page request storage allocation failed");
        }
        for (u32 index = 0; index < entry->pages.Size(); ++index)
        {
            entry->reads[index].page = entry->pages[index];
            const meshes::Result readResult = mesh->GetPageSource().ReadPageAsync(mesh->GetMetadata(), entry->pages[index], entry->reads[index].request, priority);
            if (readResult != meshes::Result::Success)
            {
                const u32 failedPage = entry->pages[index];
                for (PendingPageRead& read : entry->reads)
                    read.request.Reset();
                DeleteObject(entry);
                if (failure != nullptr)
                {
                    failure->code = MeshLodGeometryUploadFailureCode::PageReadFailure;
                    failure->page = failedPage;
                    failure->message = "mesh LOD page request could not be started";
                    failure->pageFailure = readResult;
                }
                return false;
            }
        }

        u32 slot = InvalidMeshLodGeometryUploadIndex;
        if (!m_impl->recycledSlots.Empty())
        {
            slot = m_impl->recycledSlots.Back();
            m_impl->recycledSlots.PopBack();
            m_impl->slots[slot].entry = entry;
        }
        else
        {
            slot = m_impl->slots.Size();
            m_impl->slots.PushBack({entry, 1});
        }
        entry->state = MeshLodGeometryUploadState::ReadingPages;
        entry->pendingBytesCharged = true;
        m_impl->pendingUploadBytes += entry->plannedBytes;
        request = m_impl->Id(slot);
        ++m_impl->stats.requestsIssued;
        ++m_impl->stats.liveRequests;
        return true;
    }

    bool MeshLodGeometryUploader::Cancel(const MeshLodGeometryUploadRequestId request, MeshLodGeometryUploadFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, MeshLodGeometryUploadFailureCode::NotInitialized, "mesh LOD geometry uploader is not initialized", request);
        if (!concurrency::IsMainThread())
            return Fail(failure, MeshLodGeometryUploadFailureCode::WrongThread, "mesh LOD upload cancellation must run on the main thread", request);
        UploadEntry* const entry = m_impl->Find(request);
        if (entry == nullptr || entry->state == MeshLodGeometryUploadState::Complete)
            return Fail(failure, MeshLodGeometryUploadFailureCode::InvalidArgument, "mesh LOD upload request is stale or already owns completed geometry", request);
        if (entry->interests > 1)
        {
            --entry->interests;
            ++m_impl->stats.requestsCancelled;
            return true;
        }
        entry->interests = 0;
        if (entry->state == MeshLodGeometryUploadState::PreparingUpload)
        {
            entry->cancelRequested = true;
            ++m_impl->stats.requestsCancelled;
            return true;
        }
        for (PendingPageRead& read : entry->reads)
            read.request.Reset();
        m_impl->Remove(request.index);
        ++m_impl->stats.requestsCancelled;
        return true;
    }

    bool MeshLodGeometryUploader::Tick(MeshLodGeometryUploadFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, MeshLodGeometryUploadFailureCode::NotInitialized, "mesh LOD geometry uploader is not initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, MeshLodGeometryUploadFailureCode::WrongThread, "mesh LOD geometry uploader tick must run on the main thread");

        u32 submittedLods = 0;
        u64 submittedBytes = 0;
        for (u32 slot = 0; slot < m_impl->slots.Size() && submittedLods < m_impl->config.maximumLodsPerTick; ++slot)
        {
            UploadEntry* const entry = m_impl->slots[slot].entry;
            if (entry == nullptr || entry->state != MeshLodGeometryUploadState::PreparingUpload || !entry->preparationCounter.IsReady())
                continue;
            entry->preparationCounter = {};
            if (entry->cancelRequested || !entry->preparationSucceeded)
            {
                MeshGeometryUploadFailure cancelFailure;
                if (!CancelPreparedMeshLodUpload(*m_impl->allocator, *m_impl->uploader, entry->prepared, &cancelFailure))
                    return Fail(failure, MeshLodGeometryUploadFailureCode::GeometryRetirementFailure, "prepared mesh LOD upload could not be cancelled after worker completion", m_impl->Id(slot));
                if (entry->cancelRequested)
                {
                    m_impl->Remove(slot);
                    continue;
                }
                MeshLodGeometryUploadFailure failed;
                failed.code = MeshLodGeometryUploadFailureCode::GeometryPreparationFailure;
                failed.message = "mesh LOD staging fill failed";
                failed.preparationFailure = entry->asyncPreparationFailure;
                m_impl->SetFailed(slot, failed);
                continue;
            }

            resources::ResourceHandle resource = entry->resource.Lock();
            auto* const mesh = resource.IsValid() && resource.GetType() == meshes::MeshResourceType ? static_cast<meshes::MeshResourceObject*>(resource.Get()) : nullptr;
            if (mesh == nullptr || !mesh->IsOpen() || !(mesh->GetMetadata().GetContentFingerprint() == entry->meshContentFingerprint))
            {
                MeshGeometryUploadFailure cancelFailure;
                if (!CancelPreparedMeshLodUpload(*m_impl->allocator, *m_impl->uploader, entry->prepared, &cancelFailure))
                    return Fail(failure, MeshLodGeometryUploadFailureCode::GeometryRetirementFailure, "stale prepared mesh LOD upload could not be cancelled", m_impl->Id(slot));
                MeshLodGeometryUploadFailure stale;
                stale.code = MeshLodGeometryUploadFailureCode::StaleResourceGeneration;
                stale.message = "mesh resource generation changed while staging geometry";
                m_impl->SetFailed(slot, stale);
                continue;
            }

            containers::DynamicArray<GeometryPlacement> placements{memory::pools::Rendering::GetInstance()};
            placements.Resize(entry->prepared.geometries.Size());
            GeometryUploadResult uploadResult;
            GeometryUploadFailure uploadFailure;
            if (!m_impl->uploader->Submit({placements.TypedData(), placements.Size()}, uploadResult, &uploadFailure))
            {
                if (uploadFailure.code != GeometryUploadFailureCode::AllocatorFailure)
                {
                    GeometryAllocatorFailure cancelFailure;
                    if (!m_impl->allocator->CancelBatch({entry->prepared.geometries.TypedData(), entry->prepared.geometries.Size()}, &cancelFailure))
                        return Fail(failure, MeshLodGeometryUploadFailureCode::GeometryRetirementFailure, "failed mesh upload reservations could not be cancelled", m_impl->Id(slot));
                }
                MeshLodGeometryUploadFailure failed;
                failed.code = MeshLodGeometryUploadFailureCode::GeometrySubmissionFailure;
                failed.message = "mesh LOD geometry submission failed";
                failed.submissionFailure = uploadFailure;
                entry->prepared.Reset();
                m_impl->SetFailed(slot, failed);
                continue;
            }

            entry->pendingGeometry.resource = resource.ToWeak();
            entry->pendingGeometry.meshContentFingerprint = entry->prepared.meshContentFingerprint;
            entry->pendingGeometry.lod = entry->prepared.lod;
            entry->pendingGeometry.vertexBytes = entry->prepared.vertexBytes;
            entry->pendingGeometry.indexBytes = entry->prepared.indexBytes;
            entry->pendingGeometry.copyCompletion = uploadResult.completion;
            entry->pendingGeometry.pages = std::move(entry->prepared.pages);
            entry->pendingGeometry.submeshes = std::move(entry->prepared.submeshes);
            entry->pendingGeometry.geometries = std::move(placements);
            entry->prepared.geometries.Clear();
            entry->prepared.Reset();
            for (PendingPageRead& read : entry->reads)
                read.request.Reset();
            entry->reads.Clear();
            m_impl->ReleasePendingBytes(*entry);
            entry->state = MeshLodGeometryUploadState::Complete;
            ++submittedLods;
            submittedBytes += uploadResult.uploadedBytes;
            ++m_impl->stats.lodsSubmitted;
            m_impl->stats.bytesSubmitted += uploadResult.uploadedBytes;
        }

        for (u32 slot = 0; slot < m_impl->slots.Size(); ++slot)
        {
            UploadEntry* const entry = m_impl->slots[slot].entry;
            if (entry == nullptr || entry->state != MeshLodGeometryUploadState::ReadingPages)
                continue;
            bool finished = true;
            for (const PendingPageRead& read : entry->reads)
                finished = finished && read.request.HasFinished();
            if (!finished)
                continue;
            MeshLodGeometryUploadFailure readFailure;
            for (const PendingPageRead& read : entry->reads)
            {
                const meshes::Result result = read.request.GetResult();
                if (result != meshes::Result::Success)
                {
                    readFailure.code = MeshLodGeometryUploadFailureCode::PageReadFailure;
                    readFailure.page = read.page;
                    readFailure.message = "verified mesh LOD page read failed";
                    readFailure.pageFailure = result;
                    break;
                }
            }
            if (readFailure.code != MeshLodGeometryUploadFailureCode::None)
                m_impl->SetFailed(slot, readFailure);
            else
                entry->state = MeshLodGeometryUploadState::ReadyForUpload;
        }

        for (u32 slot = 0; slot < m_impl->slots.Size() && submittedLods < m_impl->config.maximumLodsPerTick; ++slot)
        {
            UploadEntry* const entry = m_impl->slots[slot].entry;
            if (entry == nullptr || entry->state != MeshLodGeometryUploadState::ReadyForUpload)
                continue;
            if (submittedLods != 0 && (entry->plannedBytes > m_impl->config.maximumUploadBytesPerTick || submittedBytes > m_impl->config.maximumUploadBytesPerTick - entry->plannedBytes))
                continue;

            resources::ResourceHandle resource = entry->resource.Lock();
            auto* const mesh = resource.IsValid() && resource.GetType() == meshes::MeshResourceType ? static_cast<meshes::MeshResourceObject*>(resource.Get()) : nullptr;
            if (mesh == nullptr || !mesh->IsOpen() || !(mesh->GetMetadata().GetContentFingerprint() == entry->meshContentFingerprint))
            {
                MeshLodGeometryUploadFailure stale;
                stale.code = MeshLodGeometryUploadFailureCode::StaleResourceGeneration;
                stale.message = "mesh resource generation changed before geometry submission";
                m_impl->SetFailed(slot, stale);
                continue;
            }
            containers::DynamicArray<u32> currentPages{memory::pools::Rendering::GetInstance()};
            if (mesh->BuildLodUploadSet(entry->lod, currentPages) != meshes::Result::Success || currentPages.Size() != entry->pages.Size())
            {
                MeshLodGeometryUploadFailure stale;
                stale.code = MeshLodGeometryUploadFailureCode::StaleResourceGeneration;
                stale.message = "mesh LOD upload set changed before geometry submission";
                m_impl->SetFailed(slot, stale);
                continue;
            }
            bool samePages = true;
            for (u32 page = 0; page < currentPages.Size(); ++page)
                samePages = samePages && currentPages[page] == entry->pages[page];
            if (!samePages)
            {
                MeshLodGeometryUploadFailure stale;
                stale.code = MeshLodGeometryUploadFailureCode::StaleResourceGeneration;
                stale.message = "mesh LOD page identity changed before geometry submission";
                m_impl->SetFailed(slot, stale);
                continue;
            }

            containers::DynamicArray<VerifiedMeshPagePayload> payloads{memory::pools::Rendering::GetInstance()};
            payloads.Resize(entry->reads.Size());
            for (u32 page = 0; page < entry->reads.Size(); ++page)
                payloads[page] = {entry->reads[page].page, entry->reads[page].request.GetBytes()};

            MeshGeometryUploadFailure prepareFailure;
            if (!PrepareMeshLodGeometryUpload(mesh->GetMetadata(), entry->lod, {payloads.TypedData(), payloads.Size()}, *m_impl->allocator, *m_impl->uploader, entry->prepared, &prepareFailure))
            {
                if (IsDeferrable(prepareFailure) && ++entry->preparationDeferrals <= m_impl->config.maximumPreparationDeferrals)
                {
                    ++m_impl->stats.preparationDeferrals;
                    continue;
                }
                MeshLodGeometryUploadFailure failed;
                failed.code = MeshLodGeometryUploadFailureCode::GeometryPreparationFailure;
                failed.message = IsDeferrable(prepareFailure) ? "mesh LOD geometry preparation exceeded its bounded deferral limit" : "mesh LOD geometry preparation failed";
                failed.preparationFailure = prepareFailure;
                m_impl->SetFailed(slot, failed);
                continue;
            }

            jobs::Task task = jobs::Task::Create([entry, uploader = m_impl->uploader](const jobs::JobContext&) noexcept
                                                 { entry->preparationSucceeded = FillPreparedMeshLodUpload(*uploader, entry->prepared, &entry->asyncPreparationFailure); });
            jobs::Builder builder({jobs::Priority::RenderPath, jobs::Affinity::AnyWorker}, entry);
            static jobs::JobName fillJobName{"MeshGeometry.FillStaging"};
            if (!task || !builder.IsValid() || !builder.Dispatch(fillJobName, std::move(task)))
            {
                MeshGeometryUploadFailure cancelFailure;
                static_cast<void>(CancelPreparedMeshLodUpload(*m_impl->allocator, *m_impl->uploader, entry->prepared, &cancelFailure));
                MeshLodGeometryUploadFailure failed;
                failed.code = MeshLodGeometryUploadFailureCode::GeometryPreparationFailure;
                failed.message = "mesh LOD staging worker could not be dispatched";
                m_impl->SetFailed(slot, failed);
                continue;
            }
            entry->preparationCounter = builder.ExtractCounter();
            VG_ASSERT_MSG(entry->preparationCounter.IsValid(), "successfully dispatched mesh staging work must return a completion counter");
            entry->state = MeshLodGeometryUploadState::PreparingUpload;
        }
        return true;
    }

    MeshLodGeometryUploadState MeshLodGeometryUploader::GetState(const MeshLodGeometryUploadRequestId request) const noexcept
    {
        const UploadEntry* const entry = m_impl != nullptr ? m_impl->Find(request) : nullptr;
        return entry != nullptr ? entry->state : MeshLodGeometryUploadState::Invalid;
    }

    bool MeshLodGeometryUploader::GetRequestFailure(const MeshLodGeometryUploadRequestId request, MeshLodGeometryUploadFailure& failure) const noexcept
    {
        failure = {};
        const UploadEntry* const entry = m_impl != nullptr ? m_impl->Find(request) : nullptr;
        if (entry == nullptr || entry->state != MeshLodGeometryUploadState::Failed)
            return false;
        failure = entry->failure;
        return true;
    }

    bool MeshLodGeometryUploader::TakeCompleted(const MeshLodGeometryUploadRequestId request, PendingMeshLodGeometry& pendingGeometry, MeshLodGeometryUploadFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, MeshLodGeometryUploadFailureCode::NotInitialized, "mesh LOD geometry uploader is not initialized", request);
        if (!concurrency::IsMainThread())
            return Fail(failure, MeshLodGeometryUploadFailureCode::WrongThread, "completed mesh LOD ownership must be taken on the main thread", request);
        UploadEntry* const entry = m_impl->Find(request);
        if (entry == nullptr || entry->state != MeshLodGeometryUploadState::Complete || pendingGeometry.IsValid())
            return Fail(failure, MeshLodGeometryUploadFailureCode::InvalidArgument, "mesh LOD request is not complete or output already owns pending geometry", request);
        pendingGeometry = std::move(entry->pendingGeometry);
        m_impl->Remove(request.index);
        return true;
    }

    MeshLodGeometryUploaderStats MeshLodGeometryUploader::GetStats() const noexcept
    {
        if (m_impl == nullptr)
            return {};
        MeshLodGeometryUploaderStats stats = m_impl->stats;
        stats.readingRequests = 0;
        stats.readyRequests = 0;
        stats.preparingRequests = 0;
        stats.completeRequests = 0;
        stats.failedRequests = 0;
        for (const Impl::Slot& slot : m_impl->slots)
        {
            if (slot.entry == nullptr)
                continue;
            switch (slot.entry->state)
            {
            case MeshLodGeometryUploadState::ReadingPages:
                ++stats.readingRequests;
                break;
            case MeshLodGeometryUploadState::ReadyForUpload:
                ++stats.readyRequests;
                break;
            case MeshLodGeometryUploadState::PreparingUpload:
                ++stats.preparingRequests;
                break;
            case MeshLodGeometryUploadState::Complete:
                ++stats.completeRequests;
                break;
            case MeshLodGeometryUploadState::Failed:
                ++stats.failedRequests;
                break;
            default:
                break;
            }
        }
        stats.pendingUploadBytes = m_impl->pendingUploadBytes;
        return stats;
    }
} // namespace vanguard::rendering
