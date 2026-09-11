#include <vanguard/rendering/material_scene_binding.hpp>

#include <vanguard/concurrency/thread.hpp>
#include <vanguard/memory/memory.hpp>
#include <vanguard/system/assert.hpp>

#include <new>

namespace vanguard::rendering
{
    namespace
    {
        inline constexpr u32 InvalidBindingIndex = 0xffffffffu;

        struct BindingKey
        {
            RenderProxyHandle proxy;

            [[nodiscard]] u32 CalcHash() const noexcept
            {
                u64 value = proxy.scene.index;
                value ^= static_cast<u64>(proxy.scene.generation) + 0x9e3779b97f4a7c15ull + (value << 6u) + (value >> 2u);
                value ^= static_cast<u64>(proxy.index) + 0x9e3779b97f4a7c15ull + (value << 6u) + (value >> 2u);
                value ^= static_cast<u64>(proxy.generation) + 0x9e3779b97f4a7c15ull + (value << 6u) + (value >> 2u);
                return static_cast<u32>(value ^ (value >> 32u));
            }

            [[nodiscard]] friend constexpr bool operator==(const BindingKey&, const BindingKey&) noexcept = default;
        };

        void ClearFailure(MaterialSceneBindingFailure* const failure) noexcept
        {
            if (failure != nullptr)
                *failure = {};
        }

        [[nodiscard]] bool Fail(MaterialSceneBindingFailure* const failure, const MaterialSceneBindingFailureCode code, const char* const message, const RenderProxyHandle proxy = {},
                                const MaterialResidencyRuntimeFailure& residencyFailure = {}, const RenderSceneGpuFailure& sceneFailure = {}) noexcept
        {
            if (failure != nullptr)
                *failure = {code, message, proxy, residencyFailure, sceneFailure};
            return false;
        }
    } // namespace

    struct MaterialSceneBindingBridge::Impl
    {
        struct Record
        {
            RenderProxyHandle proxy;
            MaterialDemandHandle active;
            MaterialDemandHandle candidate;
            GpuMaterialHandle activeMaterial;
            RenderSceneGpuBindingReceipt sceneReceipt;
            const resources::ResourceObject* activeObject = nullptr;
            const resources::ResourceObject* candidateObject = nullptr;
            u32 workIndex = InvalidBindingIndex;
            MaterialResidencyState candidateState = MaterialResidencyState::Invalid;
            bool clearing = false;
            bool activeRecord = false;
        };

        struct WorkRef
        {
            u32 index = InvalidBindingIndex;
            RenderProxyHandle proxy;
        };

        explicit Impl(const MaterialSceneBindingConfig& value) noexcept
            : records(memory::pools::Rendering::GetInstance()), recycled(memory::pools::Rendering::GetInstance()), byProxy(memory::pools::Rendering::GetInstance()),
              work(memory::pools::Rendering::GetInstance()), scratch(memory::pools::Rendering::GetInstance()), config(value)
        {
            records.Reserve(config.maximumBindings);
            recycled.Reserve(config.maximumBindings);
            byProxy.Reserve(config.maximumBindings);
            work.Reserve(config.maximumBindings);
            scratch.Reserve(config.maximumChecksPerUpdate);
        }

        MaterialResidencyRuntime* residency = nullptr;
        RenderSceneGpuPublisher* publisher = nullptr;
        containers::DynamicArray<Record> records;
        containers::DynamicArray<u32> recycled;
        containers::HashMap<BindingKey, u32> byProxy;
        containers::DynamicArray<u32> work;
        containers::DynamicArray<WorkRef> scratch;
        MaterialSceneBindingConfig config;
        MaterialSceneBindingStats stats;
        u32 nextWork = 0;

        [[nodiscard]] Record* Find(const RenderProxyHandle proxy) noexcept
        {
            u32 index = InvalidBindingIndex;
            const BindingKey key{proxy};
            return byProxy.Find(key, index) && index < records.Size() && records[index].activeRecord && records[index].proxy == proxy ? &records[index] : nullptr;
        }

        [[nodiscard]] const Record* Find(const RenderProxyHandle proxy) const noexcept
        {
            u32 index = InvalidBindingIndex;
            const BindingKey key{proxy};
            return byProxy.Find(key, index) && index < records.Size() && records[index].activeRecord && records[index].proxy == proxy ? &records[index] : nullptr;
        }

        [[nodiscard]] u32 Allocate(const RenderProxyHandle proxy) noexcept
        {
            u32 index = InvalidBindingIndex;
            if (!recycled.Empty())
            {
                index = recycled.Back();
                recycled.PopBack();
            }
            else if (records.Size() < config.maximumBindings)
            {
                index = records.Size();
                records.EmplaceBack();
            }
            if (index == InvalidBindingIndex)
                return index;
            Record& record = records[index];
            record.proxy = proxy;
            record.activeRecord = true;
            record.workIndex = work.Size();
            work.PushBack(index);
            const bool indexed = byProxy.Insert({proxy}, index).IsSuccessful();
            VG_ASSERT_MSG(indexed, "reserved material scene-binding lookup capacity must accept a fresh record");
            ++stats.bindings;
            return index;
        }

        void Recycle(const u32 index) noexcept
        {
            Record& record = records[index];
            static_cast<void>(byProxy.Remove({record.proxy}));
            if (record.active.IsValid())
                --stats.activeBindings;
            if (record.candidate.IsValid())
                --stats.candidateBindings;
            const u32 workIndex = record.workIndex;
            const u32 moved = work.Back();
            work[workIndex] = moved;
            work.PopBack();
            if (workIndex < work.Size())
                records[moved].workIndex = workIndex;
            if (nextWork >= work.Size())
                nextWork = 0;
            record.active.Reset();
            record.candidate.Reset();
            record = {};
            recycled.PushBack(index);
            --stats.bindings;
        }

        void BuildScratch() noexcept
        {
            scratch.Clear();
            if (work.Empty())
            {
                nextWork = 0;
                return;
            }
            const u32 count = work.Size() < config.maximumChecksPerUpdate ? work.Size() : config.maximumChecksPerUpdate;
            for (u32 offset = 0; offset < count; ++offset)
            {
                const u32 workIndex = (nextWork + offset) % work.Size();
                const u32 recordIndex = work[workIndex];
                scratch.PushBack({recordIndex, records[recordIndex].proxy});
            }
            nextWork = (nextWork + count) % work.Size();
        }

        [[nodiscard]] bool RestoreActiveSceneBinding(Record& record, MaterialSceneBindingFailure* const failure) noexcept
        {
            if (!record.sceneReceipt.IsValid() && !record.clearing)
                return true;
            RenderSceneGpuBindingReceipt ignored;
            RenderSceneGpuFailure sceneFailure;
            const bool restored = record.activeMaterial.IsValid() ? publisher->BindDecalMaterial(record.proxy, record.activeMaterial, ignored, &sceneFailure)
                                                                  : publisher->ClearDecalMaterialBinding(record.proxy, &ignored, &sceneFailure);
            if (!restored)
                return Fail(failure, MaterialSceneBindingFailureCode::ScenePublicationFailure, sceneFailure.message != nullptr ? sceneFailure.message : "active decal binding could not be restored",
                            record.proxy, {}, sceneFailure);
            record.sceneReceipt = {};
            record.clearing = false;
            return true;
        }

        void CommitCandidate(Record& record) noexcept
        {
            const bool replacing = record.active.IsValid();
            if (!replacing)
                ++stats.activeBindings;
            record.active = static_cast<MaterialDemandHandle&&>(record.candidate);
            record.activeObject = record.candidateObject;
            record.candidateObject = nullptr;
            record.activeMaterial = {};
            MaterialResidencyInfo info;
            if (residency->GetInfo(record.active.GetResidency(), info))
                record.activeMaterial = info.material;
            --stats.candidateBindings;
            record.candidateState = MaterialResidencyState::Invalid;
            record.sceneReceipt = {};
            if (replacing)
                ++stats.replacements;
        }
    };

    MaterialSceneBindingBridge::~MaterialSceneBindingBridge()
    {
        VG_ASSERT_MSG(m_impl == nullptr, "material scene-binding bridge must be shut down before destruction");
    }

    bool MaterialSceneBindingBridge::Initialize(MaterialResidencyRuntime& residency, RenderSceneGpuPublisher& publisher, const MaterialSceneBindingConfig& config,
                                                MaterialSceneBindingFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl != nullptr)
            return Fail(failure, MaterialSceneBindingFailureCode::AlreadyInitialized, "material scene-binding bridge is already initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, MaterialSceneBindingFailureCode::WrongThread, "material scene-binding bridge must initialize on the main thread");
        if (!residency.IsInitialized() || !publisher.IsInitialized() || config.maximumBindings == 0 || config.maximumChecksPerUpdate == 0)
            return Fail(failure, MaterialSceneBindingFailureCode::InvalidConfiguration, "material scene-binding configuration or dependency is invalid");
        memory::MemoryBlock block = memory::Allocate(memory::PoolId::Rendering, sizeof(Impl), alignof(Impl));
        if (!block)
            return Fail(failure, MaterialSceneBindingFailureCode::CapacityExceeded, "material scene-binding allocation failed");
        m_impl = new (block.address) Impl(config);
        m_impl->residency = &residency;
        m_impl->publisher = &publisher;
        return true;
    }

    bool MaterialSceneBindingBridge::Shutdown(MaterialSceneBindingFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return true;
        if (!concurrency::IsMainThread())
            return Fail(failure, MaterialSceneBindingFailureCode::WrongThread, "material scene-binding bridge must shut down on the main thread");
        if (m_impl->stats.bindings != 0)
            return Fail(failure, MaterialSceneBindingFailureCode::LiveBindingsRemain, "material scene-binding bridge cannot shut down with live bindings");
        Impl* const impl = m_impl;
        m_impl = nullptr;
        impl->~Impl();
        memory::MemoryBlock block{impl, sizeof(Impl), memory::PoolId::Rendering};
        memory::Free(block);
        return true;
    }

    bool MaterialSceneBindingBridge::AbandonDevice(MaterialSceneBindingFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return true;
        if (!concurrency::IsMainThread())
            return Fail(failure, MaterialSceneBindingFailureCode::WrongThread, "material scene-binding abandonment must run on the main thread");
        while (!m_impl->work.Empty())
            m_impl->Recycle(m_impl->work.Back());
        return true;
    }

    bool MaterialSceneBindingBridge::IsInitialized() const noexcept
    {
        return m_impl != nullptr;
    }

    namespace
    {
        [[nodiscard]] bool SetMaterial(MaterialSceneBindingBridge::Impl& impl, const RenderProxyHandle proxy, const resources::ResourceHandle& material, MaterialSceneBindingFailure* const failure) noexcept
        {
            if (!proxy.IsValid() || !material.IsValid())
                return Fail(failure, MaterialSceneBindingFailureCode::InvalidArgument, "material scene-binding request is invalid", proxy);
            RenderSceneGpuIdentity identity;
            if (!impl.publisher->GetIdentity(proxy, identity) || identity.kind != RenderSceneGpuObjectKind::Decal)
                return Fail(failure, MaterialSceneBindingFailureCode::InvalidProxy, "material scene binding requires a tracked decal", proxy);
            MaterialSceneBindingBridge::Impl::Record* record = impl.Find(proxy);
            if (record != nullptr && record->candidate.IsValid())
            {
                MaterialResidencyInfo candidateInfo;
                if (record->candidateObject == material.Get() && impl.residency->GetInfo(record->candidate.GetResidency(), candidateInfo) && candidateInfo.resourcePath == material.GetPath() &&
                    candidateInfo.resourceGeneration == material.GetGeneration())
                    return true;
                if (!impl.RestoreActiveSceneBinding(*record, failure))
                    return false;
                record->candidate.Reset();
                record->candidateObject = nullptr;
                record->candidateState = MaterialResidencyState::Invalid;
                --impl.stats.candidateBindings;
                ++impl.stats.candidateCancellations;
            }
            if (record != nullptr && record->clearing && !impl.RestoreActiveSceneBinding(*record, failure))
                return false;
            if (record != nullptr && record->active.IsValid())
            {
                MaterialResidencyInfo activeInfo;
                if (record->activeObject == material.Get() && impl.residency->GetInfo(record->active.GetResidency(), activeInfo) && activeInfo.resourcePath == material.GetPath() &&
                    activeInfo.resourceGeneration == material.GetGeneration())
                    return true;
            }
            u32 recordIndex = InvalidBindingIndex;
            if (record == nullptr)
            {
                recordIndex = impl.Allocate(proxy);
                if (recordIndex == InvalidBindingIndex)
                    return Fail(failure, MaterialSceneBindingFailureCode::CapacityExceeded, "material scene-binding capacity is exhausted", proxy);
                record = &impl.records[recordIndex];
            }
            MaterialResidencyRuntimeFailure residencyFailure;
            if (!impl.residency->RequestMaterial(material, record->candidate, &residencyFailure))
            {
                if (recordIndex != InvalidBindingIndex)
                    impl.Recycle(recordIndex);
                return Fail(failure, MaterialSceneBindingFailureCode::ResidencyFailure, residencyFailure.message != nullptr ? residencyFailure.message : "material residency request failed", proxy,
                            residencyFailure);
            }
            record->candidateObject = material.Get();
            record->candidateState = MaterialResidencyState::Resolving;
            record->clearing = false;
            ++impl.stats.candidateBindings;
            ++impl.stats.requests;
            return true;
        }
    } // namespace

    bool MaterialSceneBindingBridge::SetDecalMaterial(const RenderProxyHandle proxy, const resources::ResourceHandle& material, MaterialSceneBindingFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr || !concurrency::IsMainThread())
            return Fail(failure, m_impl == nullptr ? MaterialSceneBindingFailureCode::NotInitialized : MaterialSceneBindingFailureCode::WrongThread, "decal material binding request is invalid", proxy);
        return SetMaterial(*m_impl, proxy, material, failure);
    }

    bool MaterialSceneBindingBridge::CancelCandidate(const RenderProxyHandle proxy, MaterialSceneBindingFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr || !concurrency::IsMainThread())
            return Fail(failure, m_impl == nullptr ? MaterialSceneBindingFailureCode::NotInitialized : MaterialSceneBindingFailureCode::WrongThread, "material candidate cancellation is invalid", proxy);
        Impl::Record* const record = m_impl->Find(proxy);
        if (record == nullptr || !record->candidate.IsValid())
            return Fail(failure, MaterialSceneBindingFailureCode::InvalidArgument, "material candidate does not exist", proxy);
        if (!m_impl->RestoreActiveSceneBinding(*record, failure))
            return false;
        record->candidate.Reset();
        record->candidateObject = nullptr;
        --m_impl->stats.candidateBindings;
        ++m_impl->stats.candidateCancellations;
        record->candidateState = MaterialResidencyState::Invalid;
        if (!record->active.IsValid())
        {
            const u32 index = record->workIndex < m_impl->work.Size() ? m_impl->work[record->workIndex] : InvalidBindingIndex;
            if (index != InvalidBindingIndex)
                m_impl->Recycle(index);
        }
        return true;
    }

    bool MaterialSceneBindingBridge::Remove(const RenderProxyHandle proxy, MaterialSceneBindingFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr || !concurrency::IsMainThread())
            return Fail(failure, m_impl == nullptr ? MaterialSceneBindingFailureCode::NotInitialized : MaterialSceneBindingFailureCode::WrongThread, "material scene-binding removal is invalid", proxy);
        Impl::Record* const record = m_impl->Find(proxy);
        if (record == nullptr)
            return Fail(failure, MaterialSceneBindingFailureCode::InvalidProxy, "material scene binding does not exist", proxy);
        RenderSceneGpuFailure sceneFailure;
        RenderSceneGpuBindingReceipt receipt;
        if (!m_impl->publisher->ClearDecalMaterialBinding(proxy, &receipt, &sceneFailure))
            return Fail(failure, MaterialSceneBindingFailureCode::ScenePublicationFailure, sceneFailure.message != nullptr ? sceneFailure.message : "decal material clearing failed", proxy, {}, sceneFailure);
        if (record->candidate.IsValid())
        {
            record->candidate.Reset();
            record->candidateObject = nullptr;
            --m_impl->stats.candidateBindings;
            ++m_impl->stats.candidateCancellations;
        }
        record->sceneReceipt = receipt;
        record->candidateState = MaterialResidencyState::Invalid;
        record->clearing = true;
        return true;
    }

    bool MaterialSceneBindingBridge::Update(MaterialSceneBindingFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr || !concurrency::IsMainThread())
            return Fail(failure, m_impl == nullptr ? MaterialSceneBindingFailureCode::NotInitialized : MaterialSceneBindingFailureCode::WrongThread, "material scene-binding update is invalid");
        MaterialSceneBindingFailure firstFailure;
        bool failed = false;
        const auto rememberFailure = [&firstFailure, &failed](const MaterialSceneBindingFailure& current) noexcept
        {
            if (!failed)
            {
                firstFailure = current;
                failed = true;
            }
        };
        m_impl->BuildScratch();
        for (const Impl::WorkRef work : m_impl->scratch)
        {
            Impl::Record* record = work.index < m_impl->records.Size() && m_impl->records[work.index].activeRecord && m_impl->records[work.index].proxy == work.proxy ? &m_impl->records[work.index] : nullptr;
            if (record == nullptr)
                continue;
            ++m_impl->stats.stateChecks;
            RenderSceneGpuIdentity identity;
            if (!m_impl->publisher->GetIdentity(record->proxy, identity) || identity.kind != RenderSceneGpuObjectKind::Decal)
            {
                m_impl->Recycle(work.index);
                ++m_impl->stats.destroyedProxies;
                continue;
            }
            if (record->clearing)
            {
                const RenderSceneGpuBindingStatus status = m_impl->publisher->PollBinding(record->sceneReceipt);
                if (status == RenderSceneGpuBindingStatus::Accepted)
                    m_impl->Recycle(work.index);
                else if (status == RenderSceneGpuBindingStatus::Stale)
                {
                    RenderSceneGpuBindingReceipt replacement;
                    RenderSceneGpuFailure sceneFailure;
                    if (!m_impl->publisher->ClearDecalMaterialBinding(record->proxy, &replacement, &sceneFailure))
                    {
                        if (sceneFailure.code != RenderSceneGpuFailureCode::Busy)
                        {
                            MaterialSceneBindingFailure current;
                            static_cast<void>(Fail(&current, MaterialSceneBindingFailureCode::ScenePublicationFailure,
                                                   sceneFailure.message != nullptr ? sceneFailure.message : "stale decal clear could not be restaged", record->proxy, {}, sceneFailure));
                            rememberFailure(current);
                        }
                    }
                    else
                        record->sceneReceipt = replacement;
                }
                continue;
            }
            if (!record->candidate.IsValid())
                continue;
            MaterialResidencyInfo info;
            MaterialResidencyRuntimeFailure residencyFailure;
            if (!m_impl->residency->GetInfo(record->candidate.GetResidency(), info, &residencyFailure))
            {
                MaterialSceneBindingFailure current;
                static_cast<void>(Fail(&current, MaterialSceneBindingFailureCode::ResidencyFailure, residencyFailure.message != nullptr ? residencyFailure.message : "material candidate became stale",
                                       record->proxy, residencyFailure));
                rememberFailure(current);
                if (record->sceneReceipt.IsValid())
                {
                    MaterialSceneBindingFailure restoreFailure;
                    if (!m_impl->RestoreActiveSceneBinding(*record, &restoreFailure))
                    {
                        rememberFailure(restoreFailure);
                        continue;
                    }
                }
                record->candidate.Reset();
                record->candidateObject = nullptr;
                record->candidateState = MaterialResidencyState::Invalid;
                --m_impl->stats.candidateBindings;
                ++m_impl->stats.candidateFailures;
                if (!record->active.IsValid())
                    m_impl->Recycle(work.index);
                continue;
            }
            record->candidateState = info.state;
            if (info.state == MaterialResidencyState::Failed)
            {
                MaterialResidencyRuntimeFailure currentResidencyFailure;
                currentResidencyFailure.code = MaterialResidencyRuntimeFailureCode::MaterializationFailure;
                currentResidencyFailure.message = info.failure.message != nullptr ? info.failure.message : "material candidate failed";
                currentResidencyFailure.materializationFailure = info.failure;
                MaterialSceneBindingFailure current;
                static_cast<void>(Fail(&current, MaterialSceneBindingFailureCode::ResidencyFailure, currentResidencyFailure.message, record->proxy, currentResidencyFailure));
                rememberFailure(current);
                record->candidate.Reset();
                record->candidateObject = nullptr;
                --m_impl->stats.candidateBindings;
                ++m_impl->stats.candidateFailures;
                record->candidateState = MaterialResidencyState::Invalid;
                if (!record->active.IsValid())
                    m_impl->Recycle(work.index);
                continue;
            }
            if (info.state != MaterialResidencyState::Resident)
                continue;
            if (!record->sceneReceipt.IsValid())
            {
                RenderSceneGpuFailure sceneFailure;
                if (!m_impl->publisher->BindDecalMaterial(record->proxy, info.material, record->sceneReceipt, &sceneFailure))
                {
                    if (sceneFailure.code == RenderSceneGpuFailureCode::Busy)
                        continue;
                    MaterialSceneBindingFailure current;
                    static_cast<void>(Fail(&current, MaterialSceneBindingFailureCode::ScenePublicationFailure, sceneFailure.message != nullptr ? sceneFailure.message : "decal material publication failed",
                                           record->proxy, {}, sceneFailure));
                    rememberFailure(current);
                    record->candidate.Reset();
                    record->candidateObject = nullptr;
                    --m_impl->stats.candidateBindings;
                    ++m_impl->stats.candidateFailures;
                    record->candidateState = MaterialResidencyState::Invalid;
                    if (!record->active.IsValid())
                        m_impl->Recycle(work.index);
                    continue;
                }
            }
            const RenderSceneGpuBindingStatus status = m_impl->publisher->PollBinding(record->sceneReceipt);
            if (status == RenderSceneGpuBindingStatus::Accepted)
                m_impl->CommitCandidate(*record);
            else if (status == RenderSceneGpuBindingStatus::Stale)
            {
                MaterialSceneBindingFailure current;
                static_cast<void>(Fail(&current, MaterialSceneBindingFailureCode::ScenePublicationFailure, "decal material receipt was superseded before acceptance", record->proxy));
                rememberFailure(current);
                MaterialSceneBindingFailure restoreFailure;
                if (!m_impl->RestoreActiveSceneBinding(*record, &restoreFailure))
                {
                    rememberFailure(restoreFailure);
                    continue;
                }
                record->candidate.Reset();
                record->candidateObject = nullptr;
                --m_impl->stats.candidateBindings;
                ++m_impl->stats.candidateFailures;
                record->candidateState = MaterialResidencyState::Invalid;
                record->sceneReceipt = {};
                if (!record->active.IsValid())
                    m_impl->Recycle(work.index);
            }
        }
        if (!failed)
            return true;
        if (failure != nullptr)
            *failure = firstFailure;
        return false;
    }

    bool MaterialSceneBindingBridge::GetInfo(const RenderProxyHandle proxy, MaterialSceneBindingInfo& info, MaterialSceneBindingFailure* const failure) const noexcept
    {
        ClearFailure(failure);
        info = {};
        if (m_impl == nullptr || !concurrency::IsMainThread())
            return Fail(failure, m_impl == nullptr ? MaterialSceneBindingFailureCode::NotInitialized : MaterialSceneBindingFailureCode::WrongThread, "material scene-binding query is invalid", proxy);
        const Impl::Record* const record = m_impl->Find(proxy);
        if (record == nullptr)
            return Fail(failure, MaterialSceneBindingFailureCode::InvalidProxy, "material scene binding does not exist", proxy);
        info.proxy = proxy;
        info.activeResidency = record->active.GetResidency();
        info.candidateResidency = record->candidate.GetResidency();
        info.activeMaterial = record->activeMaterial;
        info.candidateState = record->candidateState;
        info.awaitingScenePublication = record->sceneReceipt.IsValid();
        info.clearing = record->clearing;
        return true;
    }

    MaterialSceneBindingStats MaterialSceneBindingBridge::GetStats() const noexcept
    {
        return m_impl != nullptr ? m_impl->stats : MaterialSceneBindingStats{};
    }
} // namespace vanguard::rendering
