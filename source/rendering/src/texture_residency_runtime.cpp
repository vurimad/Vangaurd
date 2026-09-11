#include <vanguard/rendering/texture_residency_runtime.hpp>

#include <vanguard/concurrency/concurrency.hpp>
#include <vanguard/concurrency/synchronization.hpp>
#include <vanguard/memory/memory.hpp>
#include <vanguard/system/assert.hpp>
#include <vanguard/textures/texture_resource.hpp>

#include <new>
#include <utility>

namespace vanguard::rendering
{
    namespace
    {
        inline constexpr u32 InvalidRuntimeRecordIndex = 0xffffffffu;

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

        void ClearFailure(TextureResidencyRuntimeFailure* const failure) noexcept
        {
            if (failure != nullptr)
                *failure = {};
        }

        [[nodiscard]] bool Fail(TextureResidencyRuntimeFailure* const failure, const TextureResidencyRuntimeFailureCode code, const char* const message) noexcept
        {
            if (failure != nullptr)
            {
                failure->code = code;
                failure->message = message;
            }
            return false;
        }

        [[nodiscard]] bool FailResidency(TextureResidencyRuntimeFailure* const failure, const TextureResidencyFailure& residencyFailure, const char* const message) noexcept
        {
            if (failure != nullptr)
            {
                failure->code = TextureResidencyRuntimeFailureCode::ResidencyFailure;
                failure->message = message;
                failure->residencyFailure = residencyFailure;
            }
            return false;
        }

        [[nodiscard]] bool FailUploader(TextureResidencyRuntimeFailure* const failure, const TextureUploadFailure& uploadFailure, const char* const message) noexcept
        {
            if (failure != nullptr)
            {
                failure->code = TextureResidencyRuntimeFailureCode::UploaderFailure;
                failure->message = message;
                failure->uploadFailure = uploadFailure;
            }
            return false;
        }

        [[nodiscard]] bool FailGpuScene(TextureResidencyRuntimeFailure* const failure, const GpuSceneRuntimeFailure& gpuSceneFailure, const char* const message) noexcept
        {
            if (failure != nullptr)
            {
                failure->code = TextureResidencyRuntimeFailureCode::GpuSceneFailure;
                failure->message = message;
                failure->gpuSceneFailure = gpuSceneFailure;
            }
            return false;
        }

        [[nodiscard]] constexpr GpuSceneContributionToken EncodeTextureBatch(const TextureResidencyBatch batch) noexcept
        {
            return {static_cast<u64>(batch.serial) | (static_cast<u64>(batch.installationCount) << 32u), 0x5458545245534944ull};
        }

        [[nodiscard]] constexpr TextureResidencyBatch DecodeTextureBatch(const GpuSceneContributionToken token) noexcept
        {
            return {static_cast<u32>(token.value0), static_cast<u32>(token.value0 >> 32u)};
        }

        [[nodiscard]] bool WriteTextureContribution(void* const owner, const GpuSceneContributionToken token, const containers::ArraySpan<const GpuSceneUploadReservation> reservations,
                                                    const char*& failureMessage) noexcept
        {
            auto* const runtime = static_cast<TextureResidencyRuntime*>(owner);
            TextureResidencyFailure failure;
            const bool result = runtime != nullptr && runtime->GetResidencyManager().WriteBatch(DecodeTextureBatch(token), reservations, &failure);
            failureMessage = result ? nullptr : failure.message;
            return result;
        }

        void AcceptTextureContribution(void* const owner, const GpuSceneContributionToken token, const rhi::GpuFence sharedCompletion) noexcept
        {
            auto* const runtime = static_cast<TextureResidencyRuntime*>(owner);
            if (runtime != nullptr)
                runtime->GetResidencyManager().AcceptSubmittedBatch(DecodeTextureBatch(token), sharedCompletion);
        }

        [[nodiscard]] bool RetryTextureContribution(void* const owner, const GpuSceneContributionToken token, const char*& failureMessage) noexcept
        {
            auto* const runtime = static_cast<TextureResidencyRuntime*>(owner);
            TextureResidencyFailure failure;
            const bool result = runtime != nullptr && runtime->GetResidencyManager().RetryBatch(DecodeTextureBatch(token), &failure);
            failureMessage = result ? nullptr : failure.message;
            return result;
        }
    } // namespace

    struct TextureResidencyRuntime::Impl
    {
        struct ResidencySlot
        {
            resources::ResourceHandle resource;
            GpuTextureResidencyHandle residency;
            TextureUploadRequestId uploadRequest;
            TextureUploadFailure uploadFailure;
            u32 generation = 1;
            u32 demandCount = 0;
            u32 nextSamePath = InvalidRuntimeRecordIndex;
            u32 workIndex = InvalidRuntimeRecordIndex;
            TextureRuntimeState state = TextureRuntimeState::Invalid;
            bool active = false;
        };

        struct DemandSlot
        {
            u32 residencyIndex = InvalidRuntimeRecordIndex;
            u32 residencyGeneration = 0;
            u32 generation = 1;
            bool active = false;
        };

        struct WorkRef
        {
            u32 index = InvalidRuntimeRecordIndex;
            u32 generation = 0;
        };

        explicit Impl(const TextureResidencyRuntimeConfig& runtimeConfig) noexcept
            : residencies(memory::pools::Rendering::GetInstance()), recycledResidencies(memory::pools::Rendering::GetInstance()), residencyByPath(memory::pools::Rendering::GetInstance()),
              residencyByGpuIndex(memory::pools::Rendering::GetInstance()), demands(memory::pools::Rendering::GetInstance()), recycledDemands(memory::pools::Rendering::GetInstance()),
              activeWork(memory::pools::Rendering::GetInstance()), workScratch(memory::pools::Rendering::GetInstance()), config(runtimeConfig)
        {
            residencies.Reserve(config.maximumResidencyRecords);
            recycledResidencies.Reserve(config.maximumResidencyRecords);
            residencyByPath.Reserve(config.maximumResidencyRecords);
            residencyByGpuIndex.Reserve(config.maximumResidencyRecords);
            demands.Reserve(config.maximumDemands);
            recycledDemands.Reserve(config.maximumDemands);
            activeWork.Reserve(config.maximumResidencyRecords);
            workScratch.Reserve(config.maximumStateChecksPerTick);
            contributionRequests.Resize(config.maximumTableInstallationsPerFrame);
        }

        [[nodiscard]] ResidencySlot* FindRecord(const u32 index, const u32 generation) noexcept
        {
            return index < residencies.Size() && residencies[index].active && residencies[index].generation == generation ? &residencies[index] : nullptr;
        }

        [[nodiscard]] const ResidencySlot* FindRecord(const u32 index, const u32 generation) const noexcept
        {
            return index < residencies.Size() && residencies[index].active && residencies[index].generation == generation ? &residencies[index] : nullptr;
        }

        [[nodiscard]] DemandSlot* FindDemand(const TextureDemandId demand) noexcept
        {
            return demand.IsValid() && demand.index < demands.Size() && demands[demand.index].active && demands[demand.index].generation == demand.generation ? &demands[demand.index] : nullptr;
        }

        [[nodiscard]] TextureDemandId DemandId(const u32 index) const noexcept
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

        void AddWork(const u32 index) noexcept
        {
            ResidencySlot& slot = residencies[index];
            if (slot.workIndex != InvalidRuntimeRecordIndex)
                return;
            slot.workIndex = activeWork.Size();
            activeWork.PushBack(index);
        }

        void RemoveWork(const u32 index) noexcept
        {
            ResidencySlot& slot = residencies[index];
            if (slot.workIndex == InvalidRuntimeRecordIndex)
                return;
            const u32 workIndex = slot.workIndex;
            const u32 moved = activeWork.Back();
            activeWork[workIndex] = moved;
            activeWork.PopBack();
            slot.workIndex = InvalidRuntimeRecordIndex;
            if (workIndex < activeWork.Size())
                residencies[moved].workIndex = workIndex;
            if (nextWork >= activeWork.Size())
                nextWork = 0;
        }

        void BuildWorkScratch() noexcept
        {
            workScratch.Clear();
            if (activeWork.Empty())
            {
                nextWork = 0;
                return;
            }
            const u32 count = activeWork.Size() < config.maximumStateChecksPerTick ? activeWork.Size() : config.maximumStateChecksPerTick;
            for (u32 offset = 0; offset < count; ++offset)
            {
                const u32 workIndex = (nextWork + offset) % activeWork.Size();
                const u32 recordIndex = activeWork[workIndex];
                workScratch.PushBack({recordIndex, residencies[recordIndex].generation});
            }
            nextWork = (nextWork + count) % activeWork.Size();
        }

        void RemoveResidency(const u32 index) noexcept
        {
            ResidencySlot& slot = residencies[index];
            VG_ASSERT_MSG(slot.active && slot.demandCount == 0, "only an unreferenced texture residency record may be recycled");
            RemoveWork(index);
            const resources::ResourceId path = slot.resource.GetPath().Id();
            u32 head = InvalidRuntimeRecordIndex;
            const bool found = residencyByPath.Find(path, head);
            VG_ASSERT_MSG(found, "active texture residency record must be reachable from its resource-path lookup");
            if (head == index)
            {
                if (slot.nextSamePath == InvalidRuntimeRecordIndex)
                    static_cast<void>(residencyByPath.Remove(path));
                else
                    static_cast<void>(residencyByPath.Set(path, slot.nextSamePath));
            }
            else
            {
                u32 previous = head;
                while (previous != InvalidRuntimeRecordIndex && residencies[previous].nextSamePath != index)
                    previous = residencies[previous].nextSamePath;
                VG_ASSERT_MSG(previous != InvalidRuntimeRecordIndex, "active texture residency record must be linked from its path head");
                if (previous != InvalidRuntimeRecordIndex)
                    residencies[previous].nextSamePath = slot.nextSamePath;
            }
            static_cast<void>(residencyByGpuIndex.Remove(slot.residency.index));
            slot.resource.Reset();
            slot.residency = {};
            slot.uploadRequest = {};
            slot.uploadFailure = {};
            slot.generation = NextGeneration(slot.generation);
            slot.nextSamePath = InvalidRuntimeRecordIndex;
            slot.state = TextureRuntimeState::Invalid;
            slot.active = false;
            recycledResidencies.PushBack(index);
            --residencyCount;
        }

        mutable concurrency::SpinLock lock;
        containers::DynamicArray<ResidencySlot> residencies;
        containers::DynamicArray<u32> recycledResidencies;
        containers::HashMap<resources::ResourceId, u32> residencyByPath;
        containers::HashMap<u32, u32> residencyByGpuIndex;
        containers::DynamicArray<DemandSlot> demands;
        containers::DynamicArray<u32> recycledDemands;
        containers::DynamicArray<u32> activeWork;
        containers::DynamicArray<WorkRef> workScratch;
        containers::DynamicArray<GpuSceneUploadRequest> contributionRequests{memory::pools::Rendering::GetInstance()};
        TextureResidencyRuntimeConfig config;
        u32 nextWork = 0;
        u32 residencyCount = 0;
        u32 demandCount = 0;
        u64 demandsIssued = 0;
        u64 demandsCoalesced = 0;
        u64 demandsReleased = 0;
        u64 failedResidencies = 0;
    };

    TextureDemandHandle::TextureDemandHandle(TextureResidencyRuntime& runtime, const TextureDemandId demand, const GpuTextureResidencyHandle residency) noexcept
        : m_runtime(&runtime), m_demand(demand), m_residency(residency)
    {
    }

    TextureDemandHandle::~TextureDemandHandle()
    {
        Reset();
    }

    TextureDemandHandle::TextureDemandHandle(TextureDemandHandle&& other) noexcept : m_runtime(other.m_runtime), m_demand(other.m_demand), m_residency(other.m_residency)
    {
        other.m_runtime = nullptr;
        other.m_demand = {};
        other.m_residency = {};
    }

    TextureDemandHandle& TextureDemandHandle::operator=(TextureDemandHandle&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            m_runtime = other.m_runtime;
            m_demand = other.m_demand;
            m_residency = other.m_residency;
            other.m_runtime = nullptr;
            other.m_demand = {};
            other.m_residency = {};
        }
        return *this;
    }

    bool TextureDemandHandle::IsValid() const noexcept
    {
        return m_runtime != nullptr && m_runtime->IsDemandValid(m_demand);
    }

    GpuTextureResidencyHandle TextureDemandHandle::GetResidency() const noexcept
    {
        return IsValid() ? m_residency : GpuTextureResidencyHandle{};
    }

    void TextureDemandHandle::Reset() noexcept
    {
        if (m_runtime != nullptr && m_demand.IsValid())
            m_runtime->ReleaseDemand(m_demand);
        m_runtime = nullptr;
        m_demand = {};
        m_residency = {};
    }

    TextureResidencyRuntime::~TextureResidencyRuntime()
    {
        VG_ASSERT_MSG(m_impl == nullptr && !m_residency.IsInitialized() && !m_uploader.IsInitialized(), "texture residency runtime must be shut down before destruction");
    }

    bool TextureResidencyRuntime::Initialize(GpuSceneLifetime& lifetime, const rhi::DescriptorDomainRef resourceDescriptors, const TextureResidencyRuntimeConfig& config,
                                             TextureResidencyRuntimeFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl != nullptr || m_residency.IsInitialized() || m_uploader.IsInitialized())
            return Fail(failure, TextureResidencyRuntimeFailureCode::AlreadyInitialized, "texture residency runtime is already initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, TextureResidencyRuntimeFailureCode::WrongThread, "texture residency runtime must initialize on the main thread");
        if (!lifetime.IsInitialized() || !resourceDescriptors.IsValid())
            return Fail(failure, TextureResidencyRuntimeFailureCode::InvalidDependency, "texture residency runtime requires GPU Scene lifetime and the global resource descriptor domain");
        if (config.maximumResidencyRecords == 0 || config.maximumDemands == 0 || config.maximumInstallationsPerTick == 0 || config.maximumTableInstallationsPerFrame == 0 ||
            config.maximumStateChecksPerTick == 0 || config.maximumResidencyRecords > config.residency.maximumTextures)
            return Fail(failure, TextureResidencyRuntimeFailureCode::InvalidConfiguration, "texture residency runtime configuration is invalid");

        TextureResidencyFailure residencyFailure;
        if (!m_residency.Initialize(lifetime, resourceDescriptors, config.residency, &residencyFailure))
            return FailResidency(failure, residencyFailure, "texture residency manager initialization failed");
        TextureUploadFailure uploadFailure;
        if (!m_uploader.Initialize(config.uploader, &uploadFailure))
        {
            static_cast<void>(m_residency.Shutdown());
            return FailUploader(failure, uploadFailure, "texture uploader initialization failed");
        }
        m_impl = AllocateObject<Impl>(config);
        if (m_impl == nullptr)
        {
            static_cast<void>(m_uploader.Shutdown());
            static_cast<void>(m_residency.Shutdown());
            return Fail(failure, TextureResidencyRuntimeFailureCode::CapacityExceeded, "texture residency runtime storage allocation failed");
        }
        return true;
    }

    bool TextureResidencyRuntime::Shutdown(TextureResidencyRuntimeFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr && !m_residency.IsInitialized() && !m_uploader.IsInitialized())
            return true;
        if (!concurrency::IsMainThread())
            return Fail(failure, TextureResidencyRuntimeFailureCode::WrongThread, "texture residency runtime must shut down on the main thread");
        if (m_impl != nullptr)
        {
            concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
            if (m_impl->demandCount != 0 || m_impl->residencyCount != 0)
                return Fail(failure, TextureResidencyRuntimeFailureCode::LiveResidencyRemains, "texture residency runtime still owns live demands or residency records");
        }
        TextureUploadFailure uploadFailure;
        if (m_uploader.IsInitialized() && !m_uploader.Shutdown(&uploadFailure))
            return FailUploader(failure, uploadFailure, "texture uploader shutdown failed");
        TextureResidencyFailure residencyFailure;
        if (m_residency.IsInitialized() && !m_residency.Shutdown(&residencyFailure))
            return FailResidency(failure, residencyFailure, "texture residency manager shutdown failed");
        DeleteObject(m_impl);
        m_impl = nullptr;
        return true;
    }

    bool TextureResidencyRuntime::AbandonDevice(TextureResidencyRuntimeFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr && !m_residency.IsInitialized() && !m_uploader.IsInitialized())
            return true;
        if (!concurrency::IsMainThread())
            return Fail(failure, TextureResidencyRuntimeFailureCode::WrongThread, "texture residency abandonment must run on the main thread");
        m_uploader.AbandonDevice();
        m_residency.AbandonDevice();
        DeleteObject(m_impl);
        m_impl = nullptr;
        return true;
    }

    bool TextureResidencyRuntime::IsInitialized() const noexcept
    {
        return m_impl != nullptr && m_residency.IsInitialized() && m_uploader.IsInitialized();
    }

    bool TextureResidencyRuntime::RequestTexture(const resources::ResourceHandle& resource, TextureDemandHandle& demand, TextureResidencyRuntimeFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!IsInitialized())
            return Fail(failure, TextureResidencyRuntimeFailureCode::NotInitialized, "texture residency runtime is not initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, TextureResidencyRuntimeFailureCode::WrongThread, "texture residency demand must be issued on the main thread");
        if (demand.IsValid() || !resource.IsValid() || resource.GetType() != textures::TextureResourceType)
            return Fail(failure, TextureResidencyRuntimeFailureCode::InvalidArgument, "texture demand requires a live texture resource and empty output handle");
        auto* const texture = static_cast<textures::TextureResourceObject*>(resource.Get());
        if (texture == nullptr || !texture->IsOpen() || texture->GetMetadata().GetMipCount() == 0)
            return Fail(failure, TextureResidencyRuntimeFailureCode::InvalidArgument, "texture demand references closed or empty texture metadata");

        {
            concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
            u32 candidate = InvalidRuntimeRecordIndex;
            if (m_impl->residencyByPath.Find(resource.GetPath().Id(), candidate))
            {
                while (candidate != InvalidRuntimeRecordIndex)
                {
                    Impl::ResidencySlot& slot = m_impl->residencies[candidate];
                    if (slot.active && slot.resource.GetGeneration() == resource.GetGeneration() && slot.state != TextureRuntimeState::Failed && slot.state != TextureRuntimeState::Cancelling &&
                        slot.state != TextureRuntimeState::Retiring)
                    {
                        if (m_impl->demandCount >= m_impl->config.maximumDemands || slot.demandCount == 0xffffffffu)
                            return Fail(failure, TextureResidencyRuntimeFailureCode::CapacityExceeded, "texture demand capacity is exhausted");
                        const u32 demandIndex = m_impl->AllocateDemand();
                        Impl::DemandSlot& demandSlot = m_impl->demands[demandIndex];
                        demandSlot.residencyIndex = candidate;
                        demandSlot.residencyGeneration = slot.generation;
                        demandSlot.active = true;
                        ++slot.demandCount;
                        ++m_impl->demandCount;
                        ++m_impl->demandsIssued;
                        ++m_impl->demandsCoalesced;
                        demand = TextureDemandHandle(*this, m_impl->DemandId(demandIndex), slot.residency);
                        return true;
                    }
                    candidate = slot.nextSamePath;
                }
            }
            if (m_impl->residencyCount >= m_impl->config.maximumResidencyRecords || m_impl->demandCount >= m_impl->config.maximumDemands)
                return Fail(failure, TextureResidencyRuntimeFailureCode::CapacityExceeded, "texture residency or demand capacity is exhausted");
        }

        GpuTextureResidencyHandle residency;
        TextureResidencyFailure residencyFailure;
        if (!m_residency.Allocate(residency, &residencyFailure))
            return FailResidency(failure, residencyFailure, "texture residency identity allocation failed");
        TextureUploadRequestId uploadRequest;
        TextureUploadFailure uploadFailure;
        if (!m_uploader.RequestMipTail(resource, m_residency, residency, uploadRequest, io::eAsyncPriority_Streaming, &uploadFailure))
        {
            static_cast<void>(m_residency.Retire(residency));
            return FailUploader(failure, uploadFailure, "texture guaranteed mip-tail request failed");
        }

        concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
        const u32 residencyIndex = m_impl->AllocateResidency();
        Impl::ResidencySlot& slot = m_impl->residencies[residencyIndex];
        slot.resource = resource;
        slot.residency = residency;
        slot.uploadRequest = uploadRequest;
        slot.demandCount = 1;
        u32 previousHead = InvalidRuntimeRecordIndex;
        static_cast<void>(m_impl->residencyByPath.Find(resource.GetPath().Id(), previousHead));
        slot.nextSamePath = previousHead;
        slot.state = TextureRuntimeState::MipTailLoading;
        slot.active = true;
        const bool pathIndexed = previousHead == InvalidRuntimeRecordIndex ? m_impl->residencyByPath.Insert(resource.GetPath().Id(), residencyIndex).IsSuccessful()
                                                                           : m_impl->residencyByPath.Set(resource.GetPath().Id(), residencyIndex).IsSuccessful();
        const bool gpuIndexed = m_impl->residencyByGpuIndex.Insert(residency.index, residencyIndex).IsSuccessful();
        VG_ASSERT_MSG(pathIndexed && gpuIndexed, "reserved texture runtime lookup capacity must accept a fresh record");
        m_impl->AddWork(residencyIndex);
        const u32 demandIndex = m_impl->AllocateDemand();
        Impl::DemandSlot& demandSlot = m_impl->demands[demandIndex];
        demandSlot.residencyIndex = residencyIndex;
        demandSlot.residencyGeneration = slot.generation;
        demandSlot.active = true;
        ++m_impl->residencyCount;
        ++m_impl->demandCount;
        ++m_impl->demandsIssued;
        demand = TextureDemandHandle(*this, m_impl->DemandId(demandIndex), residency);
        return true;
    }

    bool TextureResidencyRuntime::CancelDemand(TextureDemandHandle& demand, TextureResidencyRuntimeFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (demand.m_runtime != this || !demand.m_demand.IsValid())
            return Fail(failure, TextureResidencyRuntimeFailureCode::InvalidArgument, "texture demand does not belong to this residency runtime");
        if (m_impl == nullptr)
            return Fail(failure, TextureResidencyRuntimeFailureCode::NotInitialized, "texture residency runtime is not initialized");
        {
            concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
            if (m_impl->FindDemand(demand.m_demand) == nullptr)
                return Fail(failure, TextureResidencyRuntimeFailureCode::StaleDemand, "texture demand handle is stale");
        }
        demand.Reset();
        return true;
    }

    void TextureResidencyRuntime::ReleaseDemand(const TextureDemandId demand) noexcept
    {
        if (m_impl == nullptr)
            return;
        concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
        Impl::DemandSlot* const demandSlot = m_impl->FindDemand(demand);
        if (demandSlot == nullptr)
            return;
        const u32 residencyIndex = demandSlot->residencyIndex;
        const u32 residencyGeneration = demandSlot->residencyGeneration;
        demandSlot->residencyIndex = InvalidRuntimeRecordIndex;
        demandSlot->residencyGeneration = 0;
        demandSlot->active = false;
        demandSlot->generation = NextGeneration(demandSlot->generation);
        m_impl->recycledDemands.PushBack(demand.index);
        --m_impl->demandCount;
        ++m_impl->demandsReleased;
        Impl::ResidencySlot* const slot = m_impl->FindRecord(residencyIndex, residencyGeneration);
        if (slot == nullptr || slot->demandCount == 0)
            return;
        --slot->demandCount;
        if (slot->demandCount == 0)
            m_impl->AddWork(residencyIndex);
    }

    bool TextureResidencyRuntime::Tick(const rhi::ResidencyFenceSet& prerequisiteFences, TextureResidencyRuntimeFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!IsInitialized())
            return Fail(failure, TextureResidencyRuntimeFailureCode::NotInitialized, "texture residency runtime is not initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, TextureResidencyRuntimeFailureCode::WrongThread, "texture residency runtime progress must run on the main thread");

        TextureResidencyFailure residencyFailure;
        static_cast<void>(m_residency.CollectRetirements(&residencyFailure));
        if (residencyFailure.code != TextureResidencyFailureCode::None)
            return FailResidency(failure, residencyFailure, "texture residency retirement collection failed");

        const auto snapshotWork = [this]() noexcept
        {
            concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
            m_impl->BuildWorkScratch();
        };

        const auto retireRecord = [this, failure](const Impl::WorkRef work) noexcept -> bool
        {
            GpuTextureResidencyHandle residency;
            {
                concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
                Impl::ResidencySlot* const slot = m_impl->FindRecord(work.index, work.generation);
                if (slot == nullptr || slot->demandCount != 0)
                    return true;
                residency = slot->residency;
            }
            TextureResidencyFailure retireFailure;
            if (!m_residency.Retire(residency, &retireFailure))
            {
                if (retireFailure.code == TextureResidencyFailureCode::Busy)
                {
                    concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
                    Impl::ResidencySlot* const slot = m_impl->FindRecord(work.index, work.generation);
                    if (slot != nullptr)
                        slot->state = TextureRuntimeState::Cancelling;
                    return true;
                }
                return FailResidency(failure, retireFailure, "texture residency retirement failed");
            }
            TextureResidencyInfo lowerInfo;
            const bool stillLive = m_residency.GetInfo(residency, lowerInfo, &retireFailure);
            concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
            Impl::ResidencySlot* const slot = m_impl->FindRecord(work.index, work.generation);
            if (slot == nullptr || slot->demandCount != 0)
                return true;
            slot->uploadRequest = {};
            if (!stillLive && retireFailure.code == TextureResidencyFailureCode::StaleHandle)
                m_impl->RemoveResidency(work.index);
            else
                slot->state = TextureRuntimeState::Retiring;
            return true;
        };

        const auto processWork = [this, failure, &retireRecord]() noexcept -> bool
        {
            for (const Impl::WorkRef work : m_impl->workScratch)
            {
                TextureRuntimeState state = TextureRuntimeState::Invalid;
                TextureUploadRequestId request;
                GpuTextureResidencyHandle residency;
                u32 demandCount = 0;
                {
                    concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
                    const Impl::ResidencySlot* const slot = m_impl->FindRecord(work.index, work.generation);
                    if (slot == nullptr)
                        continue;
                    state = slot->state;
                    request = slot->uploadRequest;
                    residency = slot->residency;
                    demandCount = slot->demandCount;
                }

                if (state == TextureRuntimeState::Retiring)
                {
                    TextureResidencyInfo lowerInfo;
                    TextureResidencyFailure queryFailure;
                    if (!m_residency.GetInfo(residency, lowerInfo, &queryFailure) && queryFailure.code == TextureResidencyFailureCode::StaleHandle)
                    {
                        concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
                        Impl::ResidencySlot* const slot = m_impl->FindRecord(work.index, work.generation);
                        if (slot != nullptr && slot->demandCount == 0)
                            m_impl->RemoveResidency(work.index);
                    }
                    continue;
                }

                if (demandCount == 0)
                {
                    if (request.IsValid())
                    {
                        const TextureUploadState uploadState = m_uploader.GetState(request);
                        if (state != TextureRuntimeState::Cancelling && uploadState != TextureUploadState::Invalid)
                        {
                            TextureUploadFailure cancelFailure;
                            if (!m_uploader.Cancel(request, &cancelFailure))
                                return FailUploader(failure, cancelFailure, "texture mip-tail cancellation failed");
                            concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
                            Impl::ResidencySlot* const slot = m_impl->FindRecord(work.index, work.generation);
                            if (slot != nullptr)
                                slot->state = TextureRuntimeState::Cancelling;
                        }
                        if (m_uploader.GetState(request) != TextureUploadState::Invalid)
                            continue;
                    }
                    if (!retireRecord(work))
                        return false;
                    continue;
                }

                if (state == TextureRuntimeState::InstallationPending)
                {
                    TextureResidencyInfo lowerInfo;
                    TextureResidencyFailure queryFailure;
                    if (!m_residency.GetInfo(residency, lowerInfo, &queryFailure))
                        return FailResidency(failure, queryFailure, "pending texture residency query failed");
                    if (lowerInfo.state == TextureResidencyState::BindlessReady)
                    {
                        concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
                        Impl::ResidencySlot* const slot = m_impl->FindRecord(work.index, work.generation);
                        if (slot != nullptr)
                        {
                            slot->state = TextureRuntimeState::BindlessReady;
                            m_impl->RemoveWork(work.index);
                        }
                    }
                    continue;
                }

                if (state != TextureRuntimeState::MipTailLoading && state != TextureRuntimeState::Cancelling)
                    continue;
                const TextureUploadState uploadState = m_uploader.GetState(request);
                if (uploadState == TextureUploadState::Failed)
                {
                    TextureUploadFailure requestFailure;
                    static_cast<void>(m_uploader.GetRequestFailure(request, requestFailure));
                    TextureUploadFailure cancelFailure;
                    if (!m_uploader.Cancel(request, &cancelFailure))
                        return FailUploader(failure, cancelFailure, "failed texture mip-tail request could not be released");
                    concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
                    Impl::ResidencySlot* const slot = m_impl->FindRecord(work.index, work.generation);
                    if (slot != nullptr)
                    {
                        slot->uploadRequest = {};
                        slot->uploadFailure = requestFailure;
                        slot->state = TextureRuntimeState::Failed;
                        m_impl->RemoveWork(work.index);
                        ++m_impl->failedResidencies;
                    }
                }
                else if (uploadState == TextureUploadState::Invalid)
                {
                    TextureResidencyInfo lowerInfo;
                    TextureResidencyFailure queryFailure;
                    if (!m_residency.GetInfo(residency, lowerInfo, &queryFailure))
                        return FailResidency(failure, queryFailure, "installed texture residency query failed");
                    concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
                    Impl::ResidencySlot* const slot = m_impl->FindRecord(work.index, work.generation);
                    if (slot != nullptr)
                    {
                        slot->uploadRequest = {};
                        slot->state = lowerInfo.state == TextureResidencyState::BindlessReady ? TextureRuntimeState::BindlessReady : TextureRuntimeState::InstallationPending;
                        if (slot->state == TextureRuntimeState::BindlessReady)
                            m_impl->RemoveWork(work.index);
                    }
                }
            }
            return true;
        };

        snapshotWork();
        if (!processWork())
            return false;

        TextureUploadFailure uploadFailure;
        if (!m_uploader.Tick(prerequisiteFences, &uploadFailure))
            return FailUploader(failure, uploadFailure, "texture uploader progress failed");
        u32 installed = 0;
        if (!m_uploader.InstallReadyCandidates(m_residency, m_impl->config.maximumInstallationsPerTick, installed, &uploadFailure))
            return FailUploader(failure, uploadFailure, "completed textures could not enter residency installation");

        snapshotWork();
        return processWork();
    }

    bool TextureResidencyRuntime::StageGpuSceneContribution(GpuSceneRuntime& gpuScene, TextureResidencyRuntimeFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!IsInitialized())
            return Fail(failure, TextureResidencyRuntimeFailureCode::NotInitialized, "texture residency runtime is not initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, TextureResidencyRuntimeFailureCode::WrongThread, "texture GPU Scene contribution must stage on the main thread");
        if (!gpuScene.IsInitialized())
            return Fail(failure, TextureResidencyRuntimeFailureCode::InvalidDependency, "texture GPU Scene contribution requires an initialized GPU Scene runtime");
        if (m_residency.GetStats().pendingInstallations == 0)
            return true;

        TextureResidencyBatch batch;
        TextureResidencyFailure residencyFailure;
        if (!m_residency.PrepareBatch(m_impl->config.maximumTableInstallationsPerFrame, batch, &residencyFailure))
            return FailResidency(failure, residencyFailure, "texture GPU Scene contribution preparation failed");
        if (batch.installationCount == 0)
        {
            m_residency.AcceptSubmittedBatch(batch, {});
            return true;
        }
        const containers::ArraySpan<GpuSceneUploadRequest> requests{m_impl->contributionRequests.TypedData(), batch.installationCount};
        if (!m_residency.BuildUploadRequests(batch, requests, &residencyFailure))
        {
            static_cast<void>(m_residency.RetryBatch(batch));
            return FailResidency(failure, residencyFailure, "texture GPU Scene upload request planning failed");
        }

        GpuSceneContributionDesc contribution;
        contribution.owner = this;
        contribution.token = EncodeTextureBatch(batch);
        contribution.requests = {requests.Data(), requests.Size()};
        contribution.write = WriteTextureContribution;
        contribution.accept = AcceptTextureContribution;
        contribution.retry = RetryTextureContribution;
        GpuSceneRuntimeFailure gpuSceneFailure;
        if (!gpuScene.StageContribution(contribution, &gpuSceneFailure))
        {
            static_cast<void>(m_residency.RetryBatch(batch));
            return FailGpuScene(failure, gpuSceneFailure, "texture GPU Scene contribution staging failed");
        }
        return true;
    }

    bool TextureResidencyRuntime::SealRetirements(const rhi::ResidencyFenceSet& safeAfter, TextureResidencyRuntimeFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!IsInitialized())
            return Fail(failure, TextureResidencyRuntimeFailureCode::NotInitialized, "texture residency runtime is not initialized");
        TextureResidencyFailure residencyFailure;
        return m_residency.SealRetirements(safeAfter, &residencyFailure) ? true : FailResidency(failure, residencyFailure, "texture residency retirement sealing failed");
    }

    u32 TextureResidencyRuntime::CollectRetirements(TextureResidencyRuntimeFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!IsInitialized())
        {
            static_cast<void>(Fail(failure, TextureResidencyRuntimeFailureCode::NotInitialized, "texture residency runtime is not initialized"));
            return 0;
        }
        TextureResidencyFailure residencyFailure;
        const u32 collected = m_residency.CollectRetirements(&residencyFailure);
        if (residencyFailure.code != TextureResidencyFailureCode::None)
        {
            static_cast<void>(FailResidency(failure, residencyFailure, "texture residency retirement collection failed"));
            return 0;
        }
        return collected;
    }

    bool TextureResidencyRuntime::GetInfo(const GpuTextureResidencyHandle residency, TextureRuntimeInfo& info, TextureResidencyRuntimeFailure* const failure) const noexcept
    {
        ClearFailure(failure);
        info = {};
        if (m_impl == nullptr)
            return Fail(failure, TextureResidencyRuntimeFailureCode::NotInitialized, "texture residency runtime is not initialized");
        concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
        u32 recordIndex = InvalidRuntimeRecordIndex;
        if (!residency.IsValid() || !m_impl->residencyByGpuIndex.Find(residency.index, recordIndex) || recordIndex >= m_impl->residencies.Size())
            return Fail(failure, TextureResidencyRuntimeFailureCode::InvalidArgument, "texture runtime query references an unknown residency identity");
        const Impl::ResidencySlot& slot = m_impl->residencies[recordIndex];
        if (!slot.active || slot.residency != residency)
            return Fail(failure, TextureResidencyRuntimeFailureCode::InvalidArgument, "texture runtime query references a stale residency identity");
        info.residency = residency;
        info.resourcePath = slot.resource.GetPath();
        info.resourceGeneration = slot.resource.GetGeneration();
        info.demandCount = slot.demandCount;
        info.state = slot.state;
        info.uploadFailure = slot.uploadFailure;
        return true;
    }

    TextureResidencyRuntimeStats TextureResidencyRuntime::GetStats() const noexcept
    {
        TextureResidencyRuntimeStats stats;
        stats.residency = m_residency.GetStats();
        stats.uploader = m_uploader.GetStats();
        if (m_impl == nullptr)
            return stats;
        concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
        stats.residencyRecords = m_impl->residencyCount;
        stats.liveDemands = m_impl->demandCount;
        stats.demandsIssued = m_impl->demandsIssued;
        stats.demandsCoalesced = m_impl->demandsCoalesced;
        stats.demandsReleased = m_impl->demandsReleased;
        stats.failedResidencies = m_impl->failedResidencies;
        return stats;
    }

    TextureResidencyManager& TextureResidencyRuntime::GetResidencyManager() noexcept
    {
        return m_residency;
    }

    bool TextureResidencyRuntime::IsDemandValid(const TextureDemandId demand) const noexcept
    {
        if (m_impl == nullptr || !demand.IsValid())
            return false;
        concurrency::ScopedLock<concurrency::SpinLock> guard(m_impl->lock);
        return demand.index < m_impl->demands.Size() && m_impl->demands[demand.index].active && m_impl->demands[demand.index].generation == demand.generation;
    }

    const TextureResidencyManager& TextureResidencyRuntime::GetResidencyManager() const noexcept
    {
        return m_residency;
    }
} // namespace vanguard::rendering
