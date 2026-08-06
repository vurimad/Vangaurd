#include <vanguard/pipeline_cache/pipeline_cache.hpp>

#include <vanguard/concurrency/concurrency.hpp>
#include <vanguard/jobs/jobs.hpp>
#include <vanguard/memory/pool.hpp>

namespace vanguard::pipeline_cache
{
    struct Entry
    {
        VANGUARD_USE_MEMORY_POOL(memory::pools::Rendering);

        crypto::Digest256 key;
        pipelines::PipelineKind kind = pipelines::PipelineKind::Graphics;
        Priority priority = Priority::Normal;
        CreationPayload payload;
        NativePipeline native;
        FailureEvidence failure;
        concurrency::Atomic<u32> state{static_cast<u32>(State::Pending)};
        concurrency::ManualResetEvent finished;
        u64 generation = 0;
        u32 interests = 0;
        bool queued = false;
        bool creating = false;
        bool retired = false;
    };

    namespace
    {
        struct Bucket
        {
            Entry* entry = nullptr;
            bool tombstone = false;
        };

        [[nodiscard]] u64 KeyHash(const crypto::Digest256& key) noexcept
        {
            u64 value = 0xcbf29ce484222325ull;
            for (u32 index = 0; index < crypto::Digest256::ByteCount; ++index)
            {
                value = (value ^ key.bytes[index]) * 0x100000001b3ull;
            }
            return value;
        }

        [[nodiscard]] jobs::Priority ToJobPriority(const Priority priority) noexcept
        {
            switch (priority)
            {
            case Priority::Background:
                return jobs::Priority::Latent;
            case Priority::Normal:
                return jobs::Priority::CriticalPath;
            case Priority::Critical:
                return jobs::Priority::RenderPath;
            }
            return jobs::Priority::CriticalPath;
        }

        [[nodiscard]] u32 BucketCapacity(const u32 maximumEntries) noexcept
        {
            u32 capacity = 2;
            while (capacity < maximumEntries * 2u)
            {
                capacity <<= 1u;
            }
            return capacity;
        }

        void SetFailure(Entry& entry, const Failure failure, const char* const message) noexcept
        {
            entry.failure = {};
            entry.failure.failure = failure;
            if (message == nullptr)
            {
                return;
            }
            u32 index = 0;
            for (; index + 1u < sizeof(entry.failure.message) && message[index] != '\0'; ++index)
            {
                entry.failure.message[index] = message[index];
            }
            entry.failure.message[index] = '\0';
        }
    } // namespace

    struct PipelineCache::Impl
    {
        VANGUARD_USE_MEMORY_POOL(memory::pools::Rendering);

        explicit Impl(const Backend& backendValue, const Config& configValue) noexcept
            : backend(backendValue), config(configValue), workerName("Rendering/PipelineCache/Create"),
              buckets(memory::pools::Rendering::GetInstance()), entries(memory::pools::Rendering::GetInstance()),
              pending(memory::pools::Rendering::GetInstance())
        {
        }

        Backend backend;
        Config config;
        jobs::JobName workerName;
        mutable concurrency::Mutex lock;
        mutable concurrency::ManualResetEvent idle{true};
        containers::DynamicArray<Bucket> buckets;
        containers::DynamicArray<Entry*> entries;
        containers::DynamicArray<Entry*> pending;
        Stats stats;
        u64 nextGeneration = 1;
        bool initialized = false;
        bool shuttingDown = false;

        [[nodiscard]] Entry* FindLocked(const crypto::Digest256& key) const noexcept
        {
            if (buckets.Empty())
            {
                return nullptr;
            }
            const u32 mask = buckets.Size() - 1u;
            u32 index = static_cast<u32>(KeyHash(key)) & mask;
            for (u32 probe = 0; probe < buckets.Size(); ++probe)
            {
                const Bucket& bucket = buckets[index];
                if (bucket.entry != nullptr)
                {
                    if (bucket.entry->key == key)
                    {
                        return bucket.entry;
                    }
                }
                else if (!bucket.tombstone)
                {
                    return nullptr;
                }
                index = (index + 1u) & mask;
            }
            return nullptr;
        }

        [[nodiscard]] bool InsertLocked(Entry& entry) noexcept
        {
            const u32 mask = buckets.Size() - 1u;
            u32 index = static_cast<u32>(KeyHash(entry.key)) & mask;
            u32 firstTombstone = buckets.Size();
            for (u32 probe = 0; probe < buckets.Size(); ++probe)
            {
                Bucket& bucket = buckets[index];
                if (bucket.entry == nullptr)
                {
                    if (!bucket.tombstone)
                    {
                        Bucket& selected = firstTombstone != buckets.Size() ? buckets[firstTombstone] : bucket;
                        selected.entry = &entry;
                        selected.tombstone = false;
                        return true;
                    }
                    if (firstTombstone == buckets.Size())
                    {
                        firstTombstone = index;
                    }
                }
                index = (index + 1u) & mask;
            }
            if (firstTombstone != buckets.Size())
            {
                buckets[firstTombstone].entry = &entry;
                buckets[firstTombstone].tombstone = false;
                return true;
            }
            return false;
        }

        void RemoveLocked(const crypto::Digest256& key, Entry* const expected) noexcept
        {
            const u32 mask = buckets.Size() - 1u;
            u32 index = static_cast<u32>(KeyHash(key)) & mask;
            for (u32 probe = 0; probe < buckets.Size(); ++probe)
            {
                Bucket& bucket = buckets[index];
                if (bucket.entry == expected)
                {
                    bucket.entry = nullptr;
                    bucket.tombstone = true;
                    return;
                }
                if (bucket.entry == nullptr && !bucket.tombstone)
                {
                    return;
                }
                index = (index + 1u) & mask;
            }
        }

        void RemovePendingLocked(Entry& entry) noexcept
        {
            for (u32 index = 0; index < pending.Size(); ++index)
            {
                if (pending[index] == &entry)
                {
                    static_cast<void>(pending.RemoveAtReorder(index));
                    entry.queued = false;
                    return;
                }
            }
        }

        [[nodiscard]] Entry* PopPendingLocked() noexcept
        {
            u32 selected = pending.Size();
            Priority selectedPriority = Priority::Background;
            for (u32 index = 0; index < pending.Size(); ++index)
            {
                Entry* const candidate = pending[index];
                if (candidate != nullptr && !candidate->retired && (selected == pending.Size() || candidate->priority > selectedPriority))
                {
                    selected = index;
                    selectedPriority = candidate->priority;
                }
            }
            if (selected == pending.Size())
            {
                pending.Clear();
                return nullptr;
            }
            Entry* const entry = pending[selected];
            static_cast<void>(pending.RemoveAtReorder(selected));
            entry->queued = false;
            entry->creating = true;
            return entry;
        }

        [[nodiscard]] Entry* DetachIfReclaimableLocked(Entry& entry, NativePipeline& native) noexcept
        {
            if (!entry.retired || entry.interests != 0 || entry.queued || entry.creating)
            {
                return nullptr;
            }
            for (u32 index = 0; index < entries.Size(); ++index)
            {
                if (entries[index] == &entry)
                {
                    entries[index] = nullptr;
                    break;
                }
            }
            native = entry.native;
            entry.native = {};
            if (stats.knownEntries != 0)
            {
                --stats.knownEntries;
            }
            return &entry;
        }

        void DestroyDetached(Entry* const entry, const NativePipeline native) noexcept
        {
            if (entry == nullptr)
            {
                return;
            }
            if (native)
            {
                backend.destroy(native, backend.userData);
            }
            if (entry->payload.IsValid())
            {
                entry->payload.release(entry->payload.data);
            }
            VANGUARD_DELETE(entry);
        }

        void FailQueuedForScheduling() noexcept
        {
            containers::DynamicArray<Entry*> failed{memory::pools::Rendering::GetInstance()};
            lock.Acquire();
            failed = static_cast<containers::DynamicArray<Entry*>&&>(pending);
            pending = containers::DynamicArray<Entry*>{memory::pools::Rendering::GetInstance()};
            for (Entry* entry : failed)
            {
                if (entry == nullptr || entry->retired)
                {
                    continue;
                }
                entry->queued = false;
                SetFailure(*entry, Failure::SchedulingFailed, "Vanguard Jobs rejected the pipeline creation worker");
                entry->state.SetValue(static_cast<u32>(State::Invalid));
                ++stats.failedCreations;
            }
            if (stats.activeWorkers == 0)
            {
                idle.Signal();
            }
            lock.Release();
            for (Entry* entry : failed)
            {
                if (entry != nullptr && !entry->retired)
                {
                    if (entry->payload.IsValid())
                    {
                        entry->payload.release(entry->payload.data);
                        entry->payload = {};
                    }
                    entry->finished.Signal();
                }
            }
        }

        void Worker() noexcept
        {
            for (;;)
            {
                lock.Acquire();
                Entry* const entry = PopPendingLocked();
                if (entry == nullptr)
                {
                    if (stats.activeWorkers != 0)
                    {
                        --stats.activeWorkers;
                    }
                    if (stats.activeWorkers == 0 && pending.Empty())
                    {
                        idle.Signal();
                    }
                    lock.Release();
                    return;
                }
                lock.Release();

                NativePipeline created;
                FailureEvidence evidence;
                const bool succeeded = backend.create(entry->kind, entry->key, entry->payload.data, created, evidence, backend.userData);
                entry->payload.release(entry->payload.data);
                entry->payload = {};
                if (succeeded && !created)
                {
                    evidence = {};
                    evidence.failure = Failure::InvalidNativeObject;
                }
                else if (!succeeded && evidence.failure == Failure::None)
                {
                    evidence.failure = Failure::BackendRejected;
                }

                NativePipeline destroyCreated;
                NativePipeline destroyRetired;
                Entry* detached = nullptr;
                lock.Acquire();
                entry->creating = false;
                if (entry->retired)
                {
                    destroyCreated = created;
                    detached = DetachIfReclaimableLocked(*entry, destroyRetired);
                }
                else if (succeeded && created)
                {
                    entry->native = created;
                    entry->failure = {};
                    entry->state.SetValue(static_cast<u32>(State::Valid));
                    ++stats.completedCreations;
                }
                else
                {
                    destroyCreated = created;
                    entry->failure = evidence;
                    entry->state.SetValue(static_cast<u32>(State::Invalid));
                    ++stats.failedCreations;
                }
                lock.Release();
                if (!entry->retired)
                {
                    entry->finished.Signal();
                }
                if (destroyCreated)
                {
                    backend.destroy(destroyCreated, backend.userData);
                }
                DestroyDetached(detached, destroyRetired);
            }
        }

        void StartWorkers() noexcept
        {
            for (;;)
            {
                Priority priority = Priority::Background;
                lock.Acquire();
                if (shuttingDown || pending.Empty() || stats.activeWorkers >= config.maximumConcurrentCreations)
                {
                    lock.Release();
                    return;
                }
                for (Entry* entry : pending)
                {
                    if (entry != nullptr && entry->priority > priority)
                    {
                        priority = entry->priority;
                    }
                }
                ++stats.activeWorkers;
                idle.Reset();
                lock.Release();

                jobs::Builder builder({ToJobPriority(priority), jobs::Affinity::AnyWorker}, this);
                jobs::Task task = jobs::Task::Create([this](const jobs::JobContext&) noexcept { Worker(); });
                if (!builder.IsValid() || !task || !builder.Dispatch(workerName, static_cast<jobs::Task&&>(task)))
                {
                    lock.Acquire();
                    if (stats.activeWorkers != 0)
                    {
                        --stats.activeWorkers;
                    }
                    lock.Release();
                    FailQueuedForScheduling();
                    return;
                }
            }
        }
    };

    const char* ToString(const Result result) noexcept
    {
        switch (result)
        {
        case Result::Success:
            return "Success";
        case Result::InvalidArgument:
            return "InvalidArgument";
        case Result::InvalidState:
            return "InvalidState";
        case Result::CapacityExceeded:
            return "CapacityExceeded";
        case Result::OutOfMemory:
            return "OutOfMemory";
        case Result::PayloadRetentionFailed:
            return "PayloadRetentionFailed";
        case Result::SchedulingFailed:
            return "SchedulingFailed";
        }
        return "Unknown";
    }

    PipelineRequest::PipelineRequest(PipelineCache* const cache, Entry* const entry) noexcept : m_cache(cache), m_entry(entry) {}

    PipelineRequest::PipelineRequest(PipelineRequest&& other) noexcept : m_cache(other.m_cache), m_entry(other.m_entry)
    {
        other.m_cache = nullptr;
        other.m_entry = nullptr;
    }

    PipelineRequest& PipelineRequest::operator=(PipelineRequest&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            m_cache = other.m_cache;
            m_entry = other.m_entry;
            other.m_cache = nullptr;
            other.m_entry = nullptr;
        }
        return *this;
    }

    PipelineRequest::~PipelineRequest()
    {
        Reset();
    }

    bool PipelineRequest::IsValid() const noexcept
    {
        return m_entry != nullptr;
    }

    PipelineRequest::operator bool() const noexcept
    {
        return IsValid();
    }

    State PipelineRequest::Status() const noexcept
    {
        return m_entry != nullptr ? static_cast<State>(m_entry->state.GetValue()) : State::Invalid;
    }

    bool PipelineRequest::HasFinished() const noexcept
    {
        return m_entry != nullptr && Status() != State::Pending;
    }

    bool PipelineRequest::HasSucceeded() const noexcept
    {
        return m_entry != nullptr && Status() == State::Valid;
    }

    void PipelineRequest::Wait() const noexcept
    {
        if (m_entry != nullptr)
        {
            m_entry->finished.Wait();
        }
    }

    bool PipelineRequest::TryWait(const u32 timeoutMilliseconds) const noexcept
    {
        return m_entry != nullptr && m_entry->finished.TryWait(timeoutMilliseconds);
    }

    NativePipeline PipelineRequest::NativeObject() const noexcept
    {
        return HasSucceeded() ? m_entry->native : NativePipeline{};
    }

    FailureEvidence PipelineRequest::Error() const noexcept
    {
        return m_entry != nullptr && HasFinished() ? m_entry->failure : FailureEvidence{};
    }

    const crypto::Digest256& PipelineRequest::Key() const noexcept
    {
        static const crypto::Digest256 invalid;
        return m_entry != nullptr ? m_entry->key : invalid;
    }

    u64 PipelineRequest::Generation() const noexcept
    {
        return m_entry != nullptr ? m_entry->generation : 0;
    }

    bool PipelineRequest::IsSameGeneration(const PipelineRequest& other) const noexcept
    {
        return m_entry != nullptr && m_entry == other.m_entry;
    }

    void PipelineRequest::Reset() noexcept
    {
        if (m_cache != nullptr && m_entry != nullptr)
        {
            m_cache->ReleaseInterest(m_entry);
        }
        m_cache = nullptr;
        m_entry = nullptr;
    }

    PipelineCache::~PipelineCache()
    {
        static_cast<void>(Shutdown());
    }

    bool PipelineCache::Initialize(const Backend& backend, const Config& config) noexcept
    {
        if (m_impl != nullptr || !backend.IsValid() || !jobs::IsInitialized() || config.maximumEntries == 0 ||
            config.maximumEntries > (1u << 28u) || config.maximumConcurrentCreations == 0)
        {
            return false;
        }
        Impl* const impl = VANGUARD_NEW(Impl)(backend, config);
        if (impl == nullptr)
        {
            return false;
        }
        impl->buckets.Resize(BucketCapacity(config.maximumEntries));
        impl->entries.Reserve(config.maximumEntries);
        impl->pending.Reserve(config.maximumEntries);
        if (impl->buckets.Size() != BucketCapacity(config.maximumEntries))
        {
            VANGUARD_DELETE(impl);
            return false;
        }
        impl->initialized = true;
        m_impl = impl;
        return true;
    }

    bool PipelineCache::Shutdown() noexcept
    {
        if (m_impl == nullptr)
        {
            return true;
        }
        m_impl->lock.Acquire();
        if (m_impl->stats.liveRequests != 0 || m_impl->stats.activeWorkers != 0 || !m_impl->pending.Empty())
        {
            m_impl->lock.Release();
            return false;
        }
        m_impl->shuttingDown = true;
        containers::DynamicArray<Entry*> entries{memory::pools::Rendering::GetInstance()};
        entries = static_cast<containers::DynamicArray<Entry*>&&>(m_impl->entries);
        m_impl->lock.Release();
        for (Entry* entry : entries)
        {
            if (entry != nullptr)
            {
                m_impl->DestroyDetached(entry, entry->native);
            }
        }
        Impl* const impl = m_impl;
        m_impl = nullptr;
        VANGUARD_DELETE(impl);
        return true;
    }

    bool PipelineCache::IsInitialized() const noexcept
    {
        return m_impl != nullptr && m_impl->initialized && !m_impl->shuttingDown;
    }

    Result PipelineCache::Request(const crypto::Digest256& concreteKey, const pipelines::PipelineKind kind, const CreationPayload& payload,
                                  PipelineRequest& output, const Priority priority) noexcept
    {
        output.Reset();
        if (m_impl == nullptr)
        {
            return Result::InvalidState;
        }
        if (concreteKey.IsEmpty() || kind > pipelines::PipelineKind::RayTracing || !payload.IsValid() || priority > Priority::Critical)
        {
            return Result::InvalidArgument;
        }

        m_impl->lock.Acquire();
        if (m_impl->shuttingDown)
        {
            m_impl->lock.Release();
            return Result::InvalidState;
        }
        ++m_impl->stats.issuedRequests;
        Entry* entry = m_impl->FindLocked(concreteKey);
        if (entry != nullptr)
        {
            if (entry->kind != kind)
            {
                m_impl->lock.Release();
                return Result::InvalidArgument;
            }
            ++entry->interests;
            ++m_impl->stats.liveRequests;
            ++m_impl->stats.coalescedRequests;
            if (entry->state.GetValue() == static_cast<u32>(State::Pending) && priority > entry->priority)
            {
                entry->priority = priority;
            }
            output = PipelineRequest(this, entry);
            m_impl->lock.Release();
            m_impl->StartWorkers();
            return Result::Success;
        }
        if (m_impl->stats.knownEntries >= m_impl->config.maximumEntries)
        {
            m_impl->lock.Release();
            return Result::CapacityExceeded;
        }
        if (!payload.retain(payload.data))
        {
            m_impl->lock.Release();
            return Result::PayloadRetentionFailed;
        }
        entry = VANGUARD_NEW(Entry);
        if (entry == nullptr)
        {
            payload.release(payload.data);
            m_impl->lock.Release();
            return Result::OutOfMemory;
        }
        entry->key = concreteKey;
        entry->kind = kind;
        entry->priority = priority;
        entry->payload = payload;
        entry->generation = m_impl->nextGeneration++;
        entry->interests = 1;
        entry->queued = true;

        const u32 previousEntries = m_impl->entries.Size();
        const u32 previousPending = m_impl->pending.Size();
        m_impl->entries.PushBack(entry);
        m_impl->pending.PushBack(entry);
        if (m_impl->entries.Size() != previousEntries + 1u || m_impl->pending.Size() != previousPending + 1u ||
            !m_impl->InsertLocked(*entry))
        {
            if (m_impl->entries.Size() == previousEntries + 1u)
            {
                m_impl->entries.PopBack();
            }
            if (m_impl->pending.Size() == previousPending + 1u)
            {
                m_impl->pending.PopBack();
            }
            payload.release(payload.data);
            VANGUARD_DELETE(entry);
            m_impl->lock.Release();
            return Result::OutOfMemory;
        }
        ++m_impl->stats.knownEntries;
        ++m_impl->stats.liveRequests;
        output = PipelineRequest(this, entry);
        m_impl->idle.Reset();
        m_impl->lock.Release();
        m_impl->StartWorkers();
        return Result::Success;
    }

    WarmupResult PipelineCache::Warmup(const containers::ArraySpan<const WarmupItem> items) noexcept
    {
        WarmupResult result;
        for (const WarmupItem& item : items)
        {
            PipelineRequest request;
            if (Request(item.concreteKey, item.kind, item.payload, request, item.priority) == Result::Success)
            {
                ++result.accepted;
                if (m_impl != nullptr)
                {
                    concurrency::ScopedLock guard(m_impl->lock);
                    ++m_impl->stats.warmupRequests;
                }
            }
            else
            {
                ++result.rejected;
            }
        }
        return result;
    }

    bool PipelineCache::Invalidate(const crypto::Digest256& concreteKey) noexcept
    {
        if (m_impl == nullptr || concreteKey.IsEmpty())
        {
            return false;
        }
        NativePipeline native;
        Entry* detached = nullptr;
        m_impl->lock.Acquire();
        Entry* const entry = m_impl->FindLocked(concreteKey);
        if (entry == nullptr)
        {
            m_impl->lock.Release();
            return false;
        }
        m_impl->RemoveLocked(concreteKey, entry);
        entry->retired = true;
        if (entry->queued)
        {
            m_impl->RemovePendingLocked(*entry);
            entry->payload.release(entry->payload.data);
            entry->payload = {};
        }
        if (entry->state.GetValue() == static_cast<u32>(State::Pending))
        {
            SetFailure(*entry, Failure::Invalidated, "Pipeline was invalidated before native creation");
            entry->state.SetValue(static_cast<u32>(State::Invalid));
            entry->finished.Signal();
        }
        ++m_impl->stats.invalidations;
        detached = m_impl->DetachIfReclaimableLocked(*entry, native);
        if (m_impl->stats.activeWorkers == 0 && m_impl->pending.Empty())
        {
            m_impl->idle.Signal();
        }
        m_impl->lock.Release();
        m_impl->DestroyDetached(detached, native);
        return true;
    }

    u32 PipelineCache::InvalidateAll() noexcept
    {
        if (m_impl == nullptr)
        {
            return 0;
        }
        containers::DynamicArray<crypto::Digest256> keys{memory::pools::Rendering::GetInstance()};
        m_impl->lock.Acquire();
        keys.Reserve(m_impl->stats.knownEntries);
        for (const Bucket& bucket : m_impl->buckets)
        {
            if (bucket.entry != nullptr)
            {
                keys.PushBack(bucket.entry->key);
            }
        }
        m_impl->lock.Release();
        u32 count = 0;
        for (const crypto::Digest256& key : keys)
        {
            count += Invalidate(key) ? 1u : 0u;
        }
        return count;
    }

    void PipelineCache::WaitIdle() const noexcept
    {
        if (m_impl != nullptr)
        {
            m_impl->idle.Wait();
        }
    }

    bool PipelineCache::TryWaitIdle(const u32 timeoutMilliseconds) const noexcept
    {
        return m_impl != nullptr && m_impl->idle.TryWait(timeoutMilliseconds);
    }

    Stats PipelineCache::GetStats() const noexcept
    {
        Stats result;
        if (m_impl == nullptr)
        {
            return result;
        }
        concurrency::ScopedLock guard(m_impl->lock);
        result = m_impl->stats;
        for (Entry* entry : m_impl->entries)
        {
            if (entry == nullptr)
            {
                continue;
            }
            switch (static_cast<State>(entry->state.GetValue()))
            {
            case State::Pending:
                ++result.pendingEntries;
                break;
            case State::Valid:
                ++result.validEntries;
                break;
            case State::Invalid:
                ++result.invalidEntries;
                break;
            }
        }
        return result;
    }

    void PipelineCache::ReleaseInterest(Entry* const entry) noexcept
    {
        if (m_impl == nullptr || entry == nullptr)
        {
            return;
        }
        NativePipeline native;
        Entry* detached = nullptr;
        m_impl->lock.Acquire();
        if (entry->interests != 0)
        {
            --entry->interests;
        }
        if (m_impl->stats.liveRequests != 0)
        {
            --m_impl->stats.liveRequests;
        }
        detached = m_impl->DetachIfReclaimableLocked(*entry, native);
        m_impl->lock.Release();
        m_impl->DestroyDetached(detached, native);
    }
} // namespace vanguard::pipeline_cache
