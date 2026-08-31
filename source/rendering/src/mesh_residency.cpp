#include <vanguard/rendering/mesh_residency.hpp>

#include <vanguard/concurrency/concurrency.hpp>
#include <vanguard/concurrency/synchronization.hpp>
#include <vanguard/memory/memory.hpp>
#include <vanguard/meshes/mesh_resource.hpp>
#include <vanguard/system/assert.hpp>

#include <new>
#include <utility>

namespace vanguard::rendering
{
    namespace
    {
        [[nodiscard]] constexpr u32 NextGeneration(const u32 generation) noexcept
        {
            const u32 next = generation + 1u;
            return next != 0 ? next : 1u;
        }

        template <typename T, typename... Args> [[nodiscard]] T* AllocateObject(Args&&... args) noexcept
        {
            memory::MemoryBlock block = memory::Allocate(memory::PoolId::Rendering, sizeof(T), alignof(T));
            return block ? ::new (block.address) T(static_cast<Args&&>(args)...) : nullptr;
        }

        template <typename T> void DeleteObject(T* const object) noexcept
        {
            if (object == nullptr)
                return;
            object->~T();
            memory::MemoryBlock block{object, sizeof(T), memory::PoolId::Rendering};
            memory::Free(block);
        }

        void ClearFailure(MeshResidencyFailure* const failure) noexcept
        {
            if (failure != nullptr)
                *failure = {};
        }

        [[nodiscard]] bool Fail(MeshResidencyFailure* const failure, const MeshResidencyFailureCode code,
                                const char* const message) noexcept
        {
            if (failure != nullptr)
            {
                failure->code = code;
                failure->message = message;
            }
            return false;
        }
    } // namespace

    struct MeshResidencyManager::Impl
    {
        struct ResidencyWork
        {
            ResidencyWork() noexcept : retiringAllocations(memory::pools::Rendering::GetInstance()) {}

            ~ResidencyWork()
            {
                VG_ASSERT_MSG(!geometry.HasOwnership(),
                              "mesh residency work must retire or transfer pending geometry before destruction");
                VG_ASSERT_MSG(!definitions.HasOwnership(),
                              "mesh residency work must retire or transfer pending definitions before destruction");
            }

            MeshLodGeometryUploadRequestId uploadRequest;
            PendingMeshLodGeometry geometry;
            PendingMeshLodDefinitions definitions;
            containers::DynamicArray<GeometryAllocationId> retiringAllocations;
            MeshLodGeometryUploadFailure uploadFailure;
        };

        struct ResidencySlot
        {
            resources::ResourceHandle resource;
            ResidencyWork* work = nullptr;
            u32 generation = 1;
            u32 demandCount = 0;
            u32 nextSamePath = InvalidMeshResidencyIndex;
            u16 anchorLod = 0xffffu;
            MeshResidencyState state = MeshResidencyState::Invalid;
            bool active = false;
        };

        struct DemandSlot
        {
            MeshResidencyHandle residency;
            u32 generation = 1;
            bool active = false;
        };

        explicit Impl(const MeshResidencyConfig& residencyConfig) noexcept
            : residencies(memory::pools::Rendering::GetInstance()), recycledResidencies(memory::pools::Rendering::GetInstance()),
              residencyByPath(memory::pools::Rendering::GetInstance()),
              demands(memory::pools::Rendering::GetInstance()), recycledDemands(memory::pools::Rendering::GetInstance()), config(residencyConfig)
        {
            residencies.Reserve(config.maximumResidencyRecords);
            recycledResidencies.Reserve(config.maximumResidencyRecords);
            residencyByPath.Reserve(config.maximumResidencyRecords);
            demands.Reserve(config.maximumDemands);
            recycledDemands.Reserve(config.maximumDemands);
        }

        [[nodiscard]] ResidencySlot* Find(const MeshResidencyHandle residency) noexcept
        {
            return residency.IsValid() && residency.index < residencies.Size() && residencies[residency.index].active &&
                           residencies[residency.index].generation == residency.generation
                       ? &residencies[residency.index]
                       : nullptr;
        }

        [[nodiscard]] const ResidencySlot* Find(const MeshResidencyHandle residency) const noexcept
        {
            return residency.IsValid() && residency.index < residencies.Size() && residencies[residency.index].active &&
                           residencies[residency.index].generation == residency.generation
                       ? &residencies[residency.index]
                       : nullptr;
        }

        [[nodiscard]] DemandSlot* Find(const MeshDemandId demand) noexcept
        {
            return demand.IsValid() && demand.index < demands.Size() && demands[demand.index].active &&
                           demands[demand.index].generation == demand.generation
                       ? &demands[demand.index]
                       : nullptr;
        }

        [[nodiscard]] MeshResidencyHandle ResidencyId(const u32 index) const noexcept
        {
            return {index, residencies[index].generation};
        }

        [[nodiscard]] MeshDemandId DemandId(const u32 index) const noexcept
        {
            return {index, demands[index].generation};
        }

        u32 AllocateResidency() noexcept
        {
            if (!recycledResidencies.Empty())
            {
                const u32 index = recycledResidencies.Back();
                recycledResidencies.PopBack();
                return index;
            }
            residencies.PushBack({});
            return residencies.Size() - 1u;
        }

        u32 AllocateDemand() noexcept
        {
            if (!recycledDemands.Empty())
            {
                const u32 index = recycledDemands.Back();
                recycledDemands.PopBack();
                return index;
            }
            demands.PushBack({});
            return demands.Size() - 1u;
        }

        void RemoveResidency(const u32 index) noexcept
        {
            ResidencySlot& slot = residencies[index];
            VG_ASSERT_MSG(slot.active && slot.demandCount == 0, "only an unreferenced mesh residency record may be recycled");
            VG_ASSERT_MSG(slot.work == nullptr, "mesh residency work must finish before its record may be recycled");
            const resources::ResourceId path = slot.resource.GetPath().Id();
            u32 head = InvalidMeshResidencyIndex;
            const bool found = residencyByPath.Find(path, head);
            VG_ASSERT_MSG(found, "active mesh residency record must be reachable from its resource-path lookup");
            if (head == index)
            {
                if (slot.nextSamePath == InvalidMeshResidencyIndex)
                    static_cast<void>(residencyByPath.Remove(path));
                else
                    static_cast<void>(residencyByPath.Set(path, slot.nextSamePath));
            }
            else
            {
                u32 previous = head;
                while (previous != InvalidMeshResidencyIndex && residencies[previous].nextSamePath != index)
                    previous = residencies[previous].nextSamePath;
                VG_ASSERT_MSG(previous != InvalidMeshResidencyIndex,
                              "active mesh residency record must be linked from its resource-path head");
                if (previous != InvalidMeshResidencyIndex)
                    residencies[previous].nextSamePath = slot.nextSamePath;
            }
            slot.resource.Reset();
            slot.generation = NextGeneration(slot.generation);
            slot.nextSamePath = InvalidMeshResidencyIndex;
            slot.anchorLod = 0xffffu;
            slot.state = MeshResidencyState::Invalid;
            slot.active = false;
            recycledResidencies.PushBack(index);
            --residencyCount;
        }

        mutable concurrency::SpinLock lock;
        containers::DynamicArray<ResidencySlot> residencies;
        containers::DynamicArray<u32> recycledResidencies;
        containers::HashMap<resources::ResourceId, u32> residencyByPath;
        containers::DynamicArray<DemandSlot> demands;
        containers::DynamicArray<u32> recycledDemands;
        MeshResidencyConfig config;
        u32 residencyCount = 0;
        u32 demandCount = 0;
        u64 demandsIssued = 0;
        u64 demandsCoalesced = 0;
        u64 demandsReleased = 0;
    };

    MeshDemandHandle::MeshDemandHandle(MeshResidencyManager& manager, const MeshDemandId demand,
                                       const MeshResidencyHandle residency) noexcept
        : m_manager(&manager), m_demand(demand), m_residency(residency)
    {
    }

    MeshDemandHandle::~MeshDemandHandle()
    {
        Reset();
    }

    MeshDemandHandle::MeshDemandHandle(MeshDemandHandle&& other) noexcept
        : m_manager(other.m_manager), m_demand(other.m_demand), m_residency(other.m_residency)
    {
        other.m_manager = nullptr;
        other.m_demand = {};
        other.m_residency = {};
    }

    MeshDemandHandle& MeshDemandHandle::operator=(MeshDemandHandle&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            m_manager = other.m_manager;
            m_demand = other.m_demand;
            m_residency = other.m_residency;
            other.m_manager = nullptr;
            other.m_demand = {};
            other.m_residency = {};
        }
        return *this;
    }

    bool MeshDemandHandle::IsValid() const noexcept
    {
        return m_manager != nullptr && m_demand.IsValid() && m_residency.IsValid();
    }

    MeshResidencyHandle MeshDemandHandle::GetResidency() const noexcept
    {
        return m_residency;
    }

    void MeshDemandHandle::Reset() noexcept
    {
        if (m_manager != nullptr && m_demand.IsValid())
            m_manager->ReleaseDemand(m_demand);
        m_manager = nullptr;
        m_demand = {};
        m_residency = {};
    }

    MeshResidencyManager::~MeshResidencyManager()
    {
        VG_ASSERT_MSG(m_impl == nullptr && m_definitions == nullptr && !m_geometryAllocator.IsInitialized() && !m_geometryUploader.IsInitialized() &&
                          !m_lodUploader.IsInitialized(),
                      "mesh residency manager must be shut down before destruction");
    }

    bool MeshResidencyManager::Initialize(GpuSceneDefinitions& definitions, const MeshResidencyConfig& config,
                                          MeshResidencyFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (IsInitialized() || m_impl != nullptr || m_geometryAllocator.IsInitialized() || m_geometryUploader.IsInitialized() || m_lodUploader.IsInitialized())
            return Fail(failure, MeshResidencyFailureCode::AlreadyInitialized, "mesh residency manager is already initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, MeshResidencyFailureCode::WrongThread, "mesh residency manager must initialize on the main thread");
        if (!definitions.IsInitialized())
            return Fail(failure, MeshResidencyFailureCode::InvalidDependency, "mesh residency manager requires initialized GPU Scene definitions");
        if (config.maximumResidencyRecords == 0 || config.maximumDemands == 0)
            return Fail(failure, MeshResidencyFailureCode::InvalidConfiguration, "mesh residency record configuration is invalid");

        GeometryAllocatorFailure allocatorFailure;
        if (!m_geometryAllocator.Initialize(config.geometryAllocator, &allocatorFailure))
        {
            if (failure != nullptr)
            {
                failure->code = MeshResidencyFailureCode::GeometryAllocatorFailure;
                failure->message = "mesh residency geometry allocator initialization failed";
                failure->allocatorFailure = allocatorFailure;
            }
            return false;
        }

        GeometryUploadFailure uploadFailure;
        if (!m_geometryUploader.Initialize(m_geometryAllocator, config.geometryUpload, &uploadFailure))
        {
            static_cast<void>(m_geometryAllocator.Shutdown());
            if (failure != nullptr)
            {
                failure->code = MeshResidencyFailureCode::GeometryUploaderFailure;
                failure->message = "mesh residency geometry uploader initialization failed";
                failure->uploadFailure = uploadFailure;
            }
            return false;
        }

        MeshLodGeometryUploadFailure lodUploadFailure;
        if (!m_lodUploader.Initialize(m_geometryAllocator, m_geometryUploader, config.lodUpload, &lodUploadFailure))
        {
            static_cast<void>(m_geometryUploader.Shutdown());
            static_cast<void>(m_geometryAllocator.Shutdown());
            if (failure != nullptr)
            {
                failure->code = MeshResidencyFailureCode::LodUploaderFailure;
                failure->message = "mesh residency LOD uploader initialization failed";
                failure->lodUploadFailure = lodUploadFailure;
            }
            return false;
        }

        m_impl = AllocateObject<Impl>(config);
        if (m_impl == nullptr)
        {
            static_cast<void>(m_lodUploader.Shutdown());
            static_cast<void>(m_geometryUploader.Shutdown());
            static_cast<void>(m_geometryAllocator.Shutdown());
            return Fail(failure, MeshResidencyFailureCode::CapacityExceeded, "mesh residency record storage allocation failed");
        }
        m_definitions = &definitions;
        return true;
    }

    bool MeshResidencyManager::Shutdown(MeshResidencyFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr && m_definitions == nullptr && !m_geometryAllocator.IsInitialized() && !m_geometryUploader.IsInitialized() && !m_lodUploader.IsInitialized())
            return true;
        if (!concurrency::IsMainThread())
            return Fail(failure, MeshResidencyFailureCode::WrongThread, "mesh residency manager must shut down on the main thread");

        if (m_impl != nullptr)
        {
            {
                concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
                if (m_impl->demandCount != 0 || m_impl->residencyCount != 0)
                    return Fail(failure, MeshResidencyFailureCode::LiveResidencyRemains,
                                "mesh residency manager still owns live demands or residency records");
            }
            DeleteObject(m_impl);
            m_impl = nullptr;
        }

        if (m_lodUploader.IsInitialized())
        {
            MeshLodGeometryUploadFailure lodUploadFailure;
            if (!m_lodUploader.Shutdown(&lodUploadFailure))
            {
                if (failure != nullptr)
                {
                    failure->code = MeshResidencyFailureCode::LodUploaderFailure;
                    failure->message = "mesh residency LOD uploader shutdown failed";
                    failure->lodUploadFailure = lodUploadFailure;
                }
                return false;
            }
        }

        if (m_geometryUploader.IsInitialized())
        {
            GeometryUploadFailure uploadFailure;
            if (!m_geometryUploader.Shutdown(&uploadFailure))
            {
                if (failure != nullptr)
                {
                    failure->code = MeshResidencyFailureCode::GeometryUploaderFailure;
                    failure->message = "mesh residency geometry uploader shutdown failed";
                    failure->uploadFailure = uploadFailure;
                }
                return false;
            }
        }

        if (m_geometryAllocator.IsInitialized())
        {
            GeometryAllocatorFailure allocatorFailure;
            if (!m_geometryAllocator.Shutdown(&allocatorFailure))
            {
                if (failure != nullptr)
                {
                    failure->code = MeshResidencyFailureCode::GeometryAllocatorFailure;
                    failure->message = "mesh residency geometry allocator shutdown failed";
                    failure->allocatorFailure = allocatorFailure;
                }
                return false;
            }
        }

        m_definitions = nullptr;
        return true;
    }

    bool MeshResidencyManager::IsInitialized() const noexcept
    {
        return m_impl != nullptr && m_definitions != nullptr && m_geometryAllocator.IsInitialized() && m_geometryUploader.IsInitialized() &&
               m_lodUploader.IsInitialized();
    }

    bool MeshResidencyManager::RequestMesh(const resources::ResourceHandle& resource, MeshDemandHandle& demand,
                                           MeshResidencyFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, MeshResidencyFailureCode::NotInitialized, "mesh residency manager is not initialized");
        if (demand.IsValid() || !resource.IsValid() || resource.GetType() != meshes::MeshResourceType)
            return Fail(failure, MeshResidencyFailureCode::InvalidArgument,
                        "mesh residency demand requires a live mesh resource and empty output handle");
        auto* const mesh = static_cast<meshes::MeshResourceObject*>(resource.Get());
        if (mesh == nullptr || !mesh->IsOpen() || mesh->GetMetadata().GetLods().Empty() ||
            mesh->GetMetadata().GetLods().Size() > 0xffffu)
            return Fail(failure, MeshResidencyFailureCode::InvalidArgument,
                        "mesh residency demand references invalid or empty mesh metadata");
        const u16 anchorLod = static_cast<u16>(mesh->GetMetadata().GetLods().Size() - 1u);

        concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
        u32 residencyIndex = InvalidMeshResidencyIndex;
        u32 candidate = InvalidMeshResidencyIndex;
        if (m_impl->residencyByPath.Find(resource.GetPath().Id(), candidate))
        {
            while (candidate != InvalidMeshResidencyIndex)
            {
                const Impl::ResidencySlot& slot = m_impl->residencies[candidate];
                if (slot.active && slot.state != MeshResidencyState::Failed &&
                    slot.state != MeshResidencyState::Retiring &&
                    slot.resource.GetGeneration() == resource.GetGeneration())
                {
                    residencyIndex = candidate;
                    break;
                }
                candidate = slot.nextSamePath;
            }
        }
        const bool coalesced = residencyIndex != InvalidMeshResidencyIndex;
        if (!coalesced && m_impl->residencyCount >= m_impl->config.maximumResidencyRecords)
            return Fail(failure, MeshResidencyFailureCode::CapacityExceeded, "mesh residency record capacity is exhausted");
        if (m_impl->demandCount >= m_impl->config.maximumDemands)
            return Fail(failure, MeshResidencyFailureCode::CapacityExceeded, "mesh residency demand capacity is exhausted");

        if (!coalesced)
        {
            residencyIndex = m_impl->AllocateResidency();
            Impl::ResidencySlot& slot = m_impl->residencies[residencyIndex];
            slot.resource = resource;
            slot.demandCount = 0;
            u32 previousHead = InvalidMeshResidencyIndex;
            static_cast<void>(m_impl->residencyByPath.Find(resource.GetPath().Id(), previousHead));
            slot.nextSamePath = previousHead;
            slot.anchorLod = anchorLod;
            slot.state = MeshResidencyState::MetadataRetained;
            slot.active = true;
            const bool indexed = previousHead == InvalidMeshResidencyIndex
                                     ? m_impl->residencyByPath.Insert(resource.GetPath().Id(), residencyIndex).IsSuccessful()
                                     : m_impl->residencyByPath.Set(resource.GetPath().Id(), residencyIndex).IsSuccessful();
            VG_ASSERT_MSG(indexed, "reserved mesh residency lookup capacity must accept a fresh record");
            ++m_impl->residencyCount;
        }

        Impl::ResidencySlot& residency = m_impl->residencies[residencyIndex];
        if (residency.demandCount == 0xffffffffu)
        {
            if (!coalesced)
                m_impl->RemoveResidency(residencyIndex);
            return Fail(failure, MeshResidencyFailureCode::CapacityExceeded, "mesh residency demand reference count overflowed");
        }
        const u32 demandIndex = m_impl->AllocateDemand();
        Impl::DemandSlot& demandSlot = m_impl->demands[demandIndex];
        demandSlot.residency = m_impl->ResidencyId(residencyIndex);
        demandSlot.active = true;
        ++residency.demandCount;
        ++m_impl->demandCount;
        ++m_impl->demandsIssued;
        if (coalesced)
            ++m_impl->demandsCoalesced;
        demand = MeshDemandHandle(*this, m_impl->DemandId(demandIndex), demandSlot.residency);
        return true;
    }

    bool MeshResidencyManager::CancelDemand(MeshDemandHandle& demand, MeshResidencyFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (demand.m_manager != this || !demand.m_demand.IsValid())
            return Fail(failure, MeshResidencyFailureCode::InvalidArgument,
                        "mesh demand does not belong to this residency manager");
        if (m_impl == nullptr)
            return Fail(failure, MeshResidencyFailureCode::NotInitialized, "mesh residency manager is not initialized");
        {
            concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
            if (m_impl->Find(demand.m_demand) == nullptr)
                return Fail(failure, MeshResidencyFailureCode::StaleDemand, "mesh demand handle is stale");
        }
        demand.Reset();
        return true;
    }

    void MeshResidencyManager::ReleaseDemand(const MeshDemandId demand) noexcept
    {
        if (m_impl == nullptr)
            return;
        concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
        Impl::DemandSlot* const demandSlot = m_impl->Find(demand);
        if (demandSlot == nullptr)
            return;
        const MeshResidencyHandle residencyId = demandSlot->residency;
        demandSlot->residency = {};
        demandSlot->active = false;
        demandSlot->generation = NextGeneration(demandSlot->generation);
        m_impl->recycledDemands.PushBack(demand.index);
        --m_impl->demandCount;
        ++m_impl->demandsReleased;

        Impl::ResidencySlot* const residency = m_impl->Find(residencyId);
        if (residency == nullptr || residency->demandCount == 0)
            return;
        --residency->demandCount;
        if (residency->demandCount == 0 && residency->state == MeshResidencyState::MetadataRetained)
            m_impl->RemoveResidency(residencyId.index);
    }

    bool MeshResidencyManager::Tick(MeshResidencyFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, MeshResidencyFailureCode::NotInitialized, "mesh residency manager is not initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, MeshResidencyFailureCode::WrongThread, "mesh residency progress must run on the main thread");

        const auto failLodUpload = [failure](const MeshLodGeometryUploadFailure& uploadFailure,
                                             const char* const message) noexcept
        {
            if (failure != nullptr)
            {
                failure->code = MeshResidencyFailureCode::LodUploaderFailure;
                failure->message = message;
                failure->lodUploadFailure = uploadFailure;
            }
            return false;
        };
        const auto failAllocator = [failure](const GeometryAllocatorFailure& allocatorFailure,
                                             const char* const message) noexcept
        {
            if (failure != nullptr)
            {
                failure->code = MeshResidencyFailureCode::GeometryAllocatorFailure;
                failure->message = message;
                failure->allocatorFailure = allocatorFailure;
            }
            return false;
        };
        const auto failDefinitions = [failure](const MeshLodDefinitionFailure& definitionFailure,
                                               const char* const message) noexcept
        {
            if (failure != nullptr)
            {
                failure->code = MeshResidencyFailureCode::LodDefinitionFailure;
                failure->message = message;
                failure->lodDefinitionFailure = definitionFailure;
            }
            return false;
        };

        u32 slotCount = 0;
        {
            concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
            slotCount = m_impl->residencies.Size();
        }

        // Start each exact resource generation once. RequestMesh remains cheap and thread-safe; all page-source and
        // uploader mutation stays serialized here on the renderer/main thread.
        for (u32 index = 0; index < slotCount; ++index)
        {
            Impl::ResidencyWork* work = nullptr;
            resources::ResourceHandle resource;
            u16 anchorLod = 0xffffu;
            {
                concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
                Impl::ResidencySlot& slot = m_impl->residencies[index];
                if (!slot.active || slot.state != MeshResidencyState::MetadataRetained || slot.demandCount == 0)
                    continue;
                work = AllocateObject<Impl::ResidencyWork>();
                if (work == nullptr)
                    return Fail(failure, MeshResidencyFailureCode::CapacityExceeded,
                                "mesh residency anchor-LOD work allocation failed");
                slot.work = work;
                slot.state = MeshResidencyState::AnchorLodLoading;
                resource = slot.resource;
                anchorLod = slot.anchorLod;
            }

            MeshLodGeometryUploadFailure uploadFailure;
            MeshLodGeometryUploadRequestId request;
            if (!m_lodUploader.Request(resource, anchorLod, request, io::eAsyncPriority_Streaming, &uploadFailure))
            {
                concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
                Impl::ResidencySlot& slot = m_impl->residencies[index];
                VG_ASSERT_MSG(slot.active && slot.work == work, "anchor-LOD request record changed during serialized issue");
                work->uploadFailure = uploadFailure;
                slot.state = MeshResidencyState::Failed;
            }
            else
            {
                work->uploadRequest = request;
            }
        }

        MeshLodGeometryUploadFailure tickFailure;
        if (!m_lodUploader.Tick(&tickFailure))
            return failLodUpload(tickFailure, "mesh residency anchor-LOD uploader progress failed");

        const auto removeUnreferenced = [this](const u32 index, Impl::ResidencyWork* const expectedWork) noexcept
        {
            Impl::ResidencyWork* removed = nullptr;
            {
                concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
                Impl::ResidencySlot& slot = m_impl->residencies[index];
                if (!slot.active || slot.demandCount != 0 || slot.work != expectedWork)
                    return;
                removed = slot.work;
                slot.work = nullptr;
                m_impl->RemoveResidency(index);
            }
            DeleteObject(removed);
        };

        for (u32 index = 0; index < slotCount; ++index)
        {
            Impl::ResidencyWork* work = nullptr;
            MeshResidencyState residencyState = MeshResidencyState::Invalid;
            u32 demandCount = 0;
            {
                concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
                const Impl::ResidencySlot& slot = m_impl->residencies[index];
                if (!slot.active || slot.work == nullptr)
                    continue;
                work = slot.work;
                residencyState = slot.state;
                demandCount = slot.demandCount;
            }

            if (residencyState == MeshResidencyState::Failed)
            {
                if (demandCount == 0)
                    removeUnreferenced(index, work);
                continue;
            }

            if (residencyState == MeshResidencyState::AnchorLodUploadSubmitted)
            {
                if (demandCount != 0)
                {
                    MeshLodDefinitionFailure definitionFailure;
                    if (!PreparePendingMeshLodDefinitions(work->geometry, *m_definitions, work->definitions,
                                                          &definitionFailure))
                        return failDefinitions(definitionFailure,
                                               "mesh residency could not acquire anchor-LOD geometry definitions");
                    concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
                    Impl::ResidencySlot& slot = m_impl->residencies[index];
                    if (slot.active && slot.work == work)
                        slot.state = MeshResidencyState::AnchorLodDefinitionsSubmitted;
                    continue;
                }
            }
            else if (residencyState == MeshResidencyState::AnchorLodDefinitionsSubmitted)
            {
                if (demandCount != 0)
                    continue;
            }
            else if (residencyState == MeshResidencyState::AnchorLodLoading ||
                     residencyState == MeshResidencyState::Cancelling)
            {
                const MeshLodGeometryUploadState uploadState = m_lodUploader.GetState(work->uploadRequest);
                if (uploadState == MeshLodGeometryUploadState::Complete)
                {
                    MeshLodGeometryUploadFailure takeFailure;
                    if (!m_lodUploader.TakeCompleted(work->uploadRequest, work->geometry, &takeFailure))
                        return failLodUpload(takeFailure, "mesh residency could not take submitted anchor-LOD geometry");
                    work->uploadRequest = {};
                    if (demandCount != 0)
                    {
                        concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
                        Impl::ResidencySlot& slot = m_impl->residencies[index];
                        if (slot.active && slot.work == work)
                            slot.state = MeshResidencyState::AnchorLodUploadSubmitted;
                        continue;
                    }
                }
                else if (uploadState == MeshLodGeometryUploadState::Failed)
                {
                    static_cast<void>(m_lodUploader.GetRequestFailure(work->uploadRequest, work->uploadFailure));
                    MeshLodGeometryUploadFailure cancelFailure;
                    if (!m_lodUploader.Cancel(work->uploadRequest, &cancelFailure))
                        return failLodUpload(cancelFailure, "mesh residency could not release a failed anchor-LOD request");
                    work->uploadRequest = {};
                    bool remove = false;
                    bool retry = false;
                    {
                        concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
                        Impl::ResidencySlot& slot = m_impl->residencies[index];
                        if (slot.active && slot.work == work)
                        {
                            remove = slot.demandCount == 0;
                            retry = !remove && residencyState == MeshResidencyState::Cancelling;
                            if (remove || retry)
                                slot.work = nullptr;
                            if (remove)
                                m_impl->RemoveResidency(index);
                            else
                                slot.state = retry ? MeshResidencyState::MetadataRetained : MeshResidencyState::Failed;
                        }
                    }
                    if (remove || retry)
                        DeleteObject(work);
                    continue;
                }
                else if (uploadState == MeshLodGeometryUploadState::Invalid)
                {
                    bool remove = false;
                    bool retry = false;
                    {
                        concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
                        Impl::ResidencySlot& slot = m_impl->residencies[index];
                        if (slot.active && slot.work == work)
                        {
                            remove = slot.demandCount == 0;
                            retry = !remove && residencyState == MeshResidencyState::Cancelling;
                            if (remove || retry)
                                slot.work = nullptr;
                            if (remove)
                                m_impl->RemoveResidency(index);
                            else if (retry)
                                slot.state = MeshResidencyState::MetadataRetained;
                        }
                    }
                    if (remove || retry)
                        DeleteObject(work);
                    continue;
                }
                else if (demandCount == 0 && residencyState != MeshResidencyState::Cancelling)
                {
                    MeshLodGeometryUploadFailure cancelFailure;
                    if (!m_lodUploader.Cancel(work->uploadRequest, &cancelFailure))
                        return failLodUpload(cancelFailure, "mesh residency anchor-LOD cancellation failed");
                    if (m_lodUploader.GetState(work->uploadRequest) == MeshLodGeometryUploadState::Invalid)
                    {
                        work->uploadRequest = {};
                        removeUnreferenced(index, work);
                    }
                    else
                    {
                        concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
                        Impl::ResidencySlot& slot = m_impl->residencies[index];
                        if (slot.active && slot.work == work)
                            slot.state = MeshResidencyState::Cancelling;
                    }
                    continue;
                }
                else
                {
                    continue;
                }
            }
            else
            {
                continue;
            }

            // No demand remains and ownership has reached physical geometry. Preserve the exact allocation IDs
            // before Abort clears the movable geometry object; record recycling waits for allocator collection.
            PendingMeshLodGeometry& ownedGeometry = work->definitions.HasOwnership() ? work->definitions.geometry : work->geometry;
            work->retiringAllocations.Clear();
            work->retiringAllocations.Reserve(ownedGeometry.geometries.Size());
            for (const GeometryPlacement placement : ownedGeometry.geometries)
                work->retiringAllocations.PushBack(placement.allocation);
            {
                concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
                Impl::ResidencySlot& slot = m_impl->residencies[index];
                if (!slot.active || slot.work != work)
                    continue;
                if (slot.demandCount != 0)
                {
                    slot.state = work->definitions.HasOwnership() ? MeshResidencyState::AnchorLodDefinitionsSubmitted
                                                                  : MeshResidencyState::AnchorLodUploadSubmitted;
                    continue;
                }
                // Cancellation is rare and allocator retirement is bounded metadata work. Holding the record lock
                // here closes the only race in which a new demand could resurrect geometry while it is being retired.
                if (work->definitions.HasOwnership())
                {
                    MeshLodDefinitionFailure definitionFailure;
                    if (!AbortPendingMeshLodDefinitions(work->definitions, *m_definitions, m_geometryAllocator,
                                                        &definitionFailure))
                        return failDefinitions(definitionFailure,
                                               "mesh residency could not retire cancelled anchor-LOD definitions");
                }
                else
                {
                    GeometryAllocatorFailure allocatorFailure;
                    if (!AbortPendingMeshLodGeometry(work->geometry, m_geometryAllocator, &allocatorFailure))
                        return failAllocator(allocatorFailure,
                                             "mesh residency could not retire cancelled anchor-LOD geometry");
                }
                slot.state = MeshResidencyState::Retiring;
            }
        }
        return true;
    }

    bool MeshResidencyManager::SealRetirements(const rhi::ResidencyFenceSet& safeAfter,
                                               MeshResidencyFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, MeshResidencyFailureCode::NotInitialized, "mesh residency manager is not initialized");
        GeometryAllocatorFailure allocatorFailure;
        if (!m_geometryAllocator.SealRetirements(safeAfter, &allocatorFailure))
        {
            if (failure != nullptr)
            {
                failure->code = MeshResidencyFailureCode::GeometryAllocatorFailure;
                failure->message = "mesh residency geometry retirement sealing failed";
                failure->allocatorFailure = allocatorFailure;
            }
            return false;
        }
        return true;
    }

    u32 MeshResidencyManager::CollectRetirements(MeshResidencyFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
        {
            static_cast<void>(Fail(failure, MeshResidencyFailureCode::NotInitialized,
                                   "mesh residency manager is not initialized"));
            return 0;
        }
        GeometryAllocatorFailure allocatorFailure;
        const u32 reclaimed = m_geometryAllocator.Collect(&allocatorFailure);
        if (allocatorFailure.code != GeometryAllocatorFailureCode::None)
        {
            if (failure != nullptr)
            {
                failure->code = MeshResidencyFailureCode::GeometryAllocatorFailure;
                failure->message = "mesh residency geometry retirement collection failed";
                failure->allocatorFailure = allocatorFailure;
            }
            return reclaimed;
        }

        u32 slotCount = 0;
        {
            concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
            slotCount = m_impl->residencies.Size();
        }
        for (u32 index = 0; index < slotCount; ++index)
        {
            Impl::ResidencyWork* work = nullptr;
            {
                concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
                const Impl::ResidencySlot& slot = m_impl->residencies[index];
                if (!slot.active || slot.state != MeshResidencyState::Retiring || slot.demandCount != 0)
                    continue;
                work = slot.work;
            }
            if (work == nullptr)
                continue;
            bool complete = true;
            for (const GeometryAllocationId allocation : work->retiringAllocations)
                complete = complete && m_geometryAllocator.GetState(allocation) == GeometryAllocationState::Invalid;
            if (!complete)
                continue;
            {
                concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
                Impl::ResidencySlot& slot = m_impl->residencies[index];
                if (!slot.active || slot.state != MeshResidencyState::Retiring || slot.demandCount != 0 || slot.work != work)
                    continue;
                slot.work = nullptr;
                m_impl->RemoveResidency(index);
            }
            DeleteObject(work);
        }
        return reclaimed;
    }

    bool MeshResidencyManager::GetInfo(const MeshResidencyHandle residency, MeshResidencyInfo& info,
                                       MeshResidencyFailure* const failure) const noexcept
    {
        ClearFailure(failure);
        info = {};
        if (m_impl == nullptr)
            return Fail(failure, MeshResidencyFailureCode::NotInitialized, "mesh residency manager is not initialized");
        concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
        const Impl::ResidencySlot* const slot = m_impl->Find(residency);
        if (slot == nullptr)
            return Fail(failure, MeshResidencyFailureCode::InvalidArgument, "mesh residency handle is stale");
        info.residency = residency;
        info.resourcePath = slot->resource.GetPath();
        info.resourceGeneration = slot->resource.GetGeneration();
        info.demandCount = slot->demandCount;
        info.anchorLod = slot->anchorLod;
        info.state = slot->state;
        if (slot->work != nullptr)
        {
            const PendingMeshLodGeometry& geometry = slot->work->definitions.IsValid()
                                                         ? slot->work->definitions.geometry
                                                         : slot->work->geometry;
            if (geometry.IsValid())
            {
                info.geometryCount = geometry.geometries.Size();
                info.vertexBytes = geometry.vertexBytes;
                info.indexBytes = geometry.indexBytes;
            }
        }
        return true;
    }

    MeshResidencyStats MeshResidencyManager::GetStats() const noexcept
    {
        MeshResidencyStats stats;
        if (m_geometryAllocator.IsInitialized())
            stats.allocator = m_geometryAllocator.GetStats();
        if (m_geometryUploader.IsInitialized())
            stats.upload = m_geometryUploader.GetStats();
        if (m_lodUploader.IsInitialized())
            stats.lodUpload = m_lodUploader.GetStats();
        if (m_impl != nullptr)
        {
            concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
            stats.residencyRecords = m_impl->residencyCount;
            stats.liveDemands = m_impl->demandCount;
            stats.demandsIssued = m_impl->demandsIssued;
            stats.demandsCoalesced = m_impl->demandsCoalesced;
            stats.demandsReleased = m_impl->demandsReleased;
        }
        return stats;
    }
} // namespace vanguard::rendering
