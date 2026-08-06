#include <vanguard/resources/resources.hpp>

#include <vanguard/concurrency/concurrency.hpp>
#include <vanguard/memory/memory.hpp>

#include <new>

namespace
{
    using namespace vanguard;
    using namespace vanguard::resources;

    template <typename T, typename... Args> [[nodiscard]] T* AllocateObject(Args&&... args) noexcept
    {
        memory::MemoryBlock block = memory::Allocate(memory::PoolId::Resources, sizeof(T), alignof(T));
        if (!block)
        {
            return nullptr;
        }
        return ::new (block.address) T(static_cast<Args&&>(args)...);
    }

    template <typename T> void DeleteObject(T* const object) noexcept
    {
        if (object == nullptr)
        {
            return;
        }
        object->~T();
        memory::MemoryBlock block{object, sizeof(T), memory::PoolId::Resources};
        memory::Free(block);
    }

    [[nodiscard]] constexpr u32 NextGeneration(const u32 generation) noexcept
    {
        const u32 next = generation + 1u;
        return next == 0 ? 1u : next;
    }
} // namespace

namespace vanguard::resources
{
    struct ResourceRequest::Control
    {
        Control(ResourceRegistry* const registry, const ResourceReference reference, const u32 slotIndex, const u32 entryGeneration,
                const u64 operationSerial, const State initialState, const Failure initialFailure) noexcept
            : owner(registry), resourceReference(reference), slot(slotIndex), generation(entryGeneration), serial(operationSerial),
              state(static_cast<u32>(initialState)), failure(static_cast<u32>(initialFailure)), completion(IsTerminal(initialState))
        {
        }

        void AddRef() noexcept
        {
            static_cast<void>(references.Increment());
        }

        void Release() noexcept
        {
            if (references.Decrement() == 0)
            {
                DeleteObject(this);
            }
        }

        ResourceRegistry* owner = nullptr;
        ResourceReference resourceReference;
        u32 slot = 0;
        u32 generation = 0;
        u64 serial = 0;
        concurrency::Atomic<u32> references{1};
        concurrency::Atomic<u32> state;
        concurrency::Atomic<u32> failure;
        concurrency::ManualResetEvent completion;
    };

    namespace
    {
        struct RegistryEntry
        {
            ResourceKey key;
            u32 generation = 1;
            State state = State::Unloaded;
            Failure failure = Failure::None;
            ResourceObject* resource = nullptr;
            DestroyResourceFunction destroyResource = nullptr;
            void* loaderUserData = nullptr;
            u32 strongHandles = 0;
            u32 weakHandles = 0;
            u64 requestSerial = 0;
            ResourceRequest::Control* activeRequest = nullptr;
        };

        struct PendingDestruction
        {
            ResourceObject* resource = nullptr;
            DestroyResourceFunction destroy = nullptr;
            void* userData = nullptr;
            ResourceRequest::Control* request = nullptr;

            void Execute() noexcept
            {
                if (resource != nullptr && destroy != nullptr)
                {
                    destroy(resource, userData);
                }
                if (request != nullptr)
                {
                    request->Release();
                }
            }
        };
    } // namespace

    struct ResourceRegistry::Impl
    {
        Impl()
            : entries(memory::pools::Resources::GetInstance()), pathToSlot(memory::pools::Resources::GetInstance()),
              loaders(memory::pools::Resources::GetInstance())
        {
        }

        concurrency::RWLock lock;
        containers::DynamicArray<RegistryEntry*> entries;
        containers::HashMap<ResourceId, u32> pathToSlot;
        containers::HashMap<ResourceTypeId, LoaderDescriptor> loaders;
        u64 nextRequestSerial = 1;
        u64 issuedRequests = 0;
        u64 coalescedRequests = 0;
    };

    namespace
    {
        template <typename Implementation>
        [[nodiscard]] bool IsRequestCurrent(const Implementation& impl, const ResourceRequest::Control& control,
                                            RegistryEntry*& entry) noexcept
        {
            if (control.slot >= impl.entries.Size())
            {
                return false;
            }
            entry = impl.entries[control.slot];
            return entry != nullptr && entry->generation == control.generation && entry->requestSerial == control.serial &&
                   entry->activeRequest == &control;
        }

        [[nodiscard]] bool IsObjectAlive(const State state) noexcept
        {
            return state == State::Loaded || state == State::Reloading || state == State::Evicting;
        }
    } // namespace

    ResourceRequest ResourceRegistry::MakeFailedRequest(const ResourceReference reference, const Failure failure) noexcept
    {
        ResourceRequest::Control* const control =
            AllocateObject<ResourceRequest::Control>(this, reference, 0, 0, 0, State::Failed, failure);
        if (control == nullptr)
        {
            return {};
        }
        return ResourceRequest(control, ResourceRequest::AdoptTag{});
    }

    ResourceRequest::ResourceRequest(Control* const control) noexcept : m_control(control)
    {
        if (m_control != nullptr)
        {
            m_control->AddRef();
        }
    }

    ResourceRequest::ResourceRequest(Control* const control, AdoptTag) noexcept : m_control(control) {}

    ResourceRequest::ResourceRequest(const ResourceRequest& other) noexcept : ResourceRequest(other.m_control) {}

    ResourceRequest::ResourceRequest(ResourceRequest&& other) noexcept : m_control(other.m_control)
    {
        other.m_control = nullptr;
    }

    ResourceRequest::~ResourceRequest()
    {
        Reset();
    }

    ResourceRequest& ResourceRequest::operator=(const ResourceRequest& other) noexcept
    {
        if (this != &other)
        {
            ResourceRequest copy(other);
            *this = static_cast<ResourceRequest&&>(copy);
        }
        return *this;
    }

    ResourceRequest& ResourceRequest::operator=(ResourceRequest&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            m_control = other.m_control;
            other.m_control = nullptr;
        }
        return *this;
    }

    ResourceReference ResourceRequest::Reference() const noexcept
    {
        return m_control != nullptr ? m_control->resourceReference : ResourceReference{};
    }

    State ResourceRequest::Status() const noexcept
    {
        return m_control != nullptr ? static_cast<State>(m_control->state.GetValue()) : State::Failed;
    }

    Failure ResourceRequest::Error() const noexcept
    {
        return m_control != nullptr ? static_cast<Failure>(m_control->failure.GetValue()) : Failure::InternalError;
    }

    bool ResourceRequest::HasFinished() const noexcept
    {
        return m_control != nullptr && IsTerminal(Status());
    }

    bool ResourceRequest::HasLoaded() const noexcept
    {
        return m_control != nullptr && Status() == State::Loaded;
    }

    bool ResourceRequest::HasFailed() const noexcept
    {
        return m_control != nullptr && (Status() == State::Failed || Status() == State::Cancelled);
    }

    bool ResourceRequest::IsValid() const noexcept
    {
        return m_control != nullptr;
    }

    ResourceRequest::operator bool() const noexcept
    {
        return IsValid();
    }

    bool ResourceRequest::IsSameOperation(const ResourceRequest& other) const noexcept
    {
        return m_control != nullptr && m_control == other.m_control;
    }

    void ResourceRequest::Wait() const noexcept
    {
        if (m_control != nullptr)
        {
            m_control->completion.Wait();
        }
    }

    bool ResourceRequest::TryWait(const u32 timeoutMilliseconds) const noexcept
    {
        return m_control != nullptr && m_control->completion.TryWait(timeoutMilliseconds);
    }

    ResourceHandle ResourceRequest::Acquire() const noexcept
    {
        if (m_control == nullptr || Status() != State::Loaded || m_control->owner == nullptr)
        {
            return {};
        }
        return m_control->owner->AcquireRequest(*this);
    }

    void ResourceRequest::Reset() noexcept
    {
        if (m_control != nullptr)
        {
            m_control->Release();
            m_control = nullptr;
        }
    }

    ResourceHandle::ResourceHandle(ResourceRegistry* const registry, const u32 slot, const u32 generation, const ResourceKey key,
                                   AdoptTag) noexcept
        : m_registry(registry), m_slot(slot), m_generation(generation), m_key(key)
    {
    }

    ResourceHandle::ResourceHandle(const ResourceHandle& other) noexcept
        : m_registry(other.m_registry), m_slot(other.m_slot), m_generation(other.m_generation), m_key(other.m_key)
    {
        if (m_registry != nullptr)
        {
            m_registry->AddStrong(m_slot, m_generation);
        }
    }

    ResourceHandle::ResourceHandle(ResourceHandle&& other) noexcept
        : m_registry(other.m_registry), m_slot(other.m_slot), m_generation(other.m_generation), m_key(other.m_key)
    {
        other.m_registry = nullptr;
        other.m_slot = 0;
        other.m_generation = 0;
        other.m_key = {};
    }

    ResourceHandle::~ResourceHandle()
    {
        Reset();
    }

    ResourceHandle& ResourceHandle::operator=(const ResourceHandle& other) noexcept
    {
        if (this != &other)
        {
            ResourceHandle copy(other);
            *this = static_cast<ResourceHandle&&>(copy);
        }
        return *this;
    }

    ResourceHandle& ResourceHandle::operator=(ResourceHandle&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            m_registry = other.m_registry;
            m_slot = other.m_slot;
            m_generation = other.m_generation;
            m_key = other.m_key;
            other.m_registry = nullptr;
            other.m_slot = 0;
            other.m_generation = 0;
            other.m_key = {};
        }
        return *this;
    }

    ResourceObject* ResourceHandle::Get() const noexcept
    {
        return m_registry != nullptr ? m_registry->Resolve(m_slot, m_generation) : nullptr;
    }

    ResourcePath ResourceHandle::Path() const noexcept
    {
        return m_key.path;
    }

    ResourceTypeId ResourceHandle::Type() const noexcept
    {
        return m_key.type;
    }

    u32 ResourceHandle::Generation() const noexcept
    {
        return m_generation;
    }

    bool ResourceHandle::IsValid() const noexcept
    {
        return Get() != nullptr;
    }

    ResourceHandle::operator bool() const noexcept
    {
        return IsValid();
    }

    void ResourceHandle::Reset() noexcept
    {
        if (m_registry != nullptr)
        {
            m_registry->ReleaseStrong(m_slot, m_generation);
            m_registry = nullptr;
            m_slot = 0;
            m_generation = 0;
            m_key = {};
        }
    }

    WeakResourceHandle ResourceHandle::ToWeak() const noexcept
    {
        if (!IsValid())
        {
            return {};
        }
        m_registry->AddWeak(m_slot, m_generation);
        return WeakResourceHandle(m_registry, m_slot, m_generation, m_key, WeakResourceHandle::AdoptTag{});
    }

    WeakResourceHandle::WeakResourceHandle(ResourceRegistry* const registry, const u32 slot, const u32 generation, const ResourceKey key,
                                           AdoptTag) noexcept
        : m_registry(registry), m_slot(slot), m_generation(generation), m_key(key)
    {
    }

    WeakResourceHandle::WeakResourceHandle(const WeakResourceHandle& other) noexcept
        : m_registry(other.m_registry), m_slot(other.m_slot), m_generation(other.m_generation), m_key(other.m_key)
    {
        if (m_registry != nullptr)
        {
            m_registry->AddWeak(m_slot, m_generation);
        }
    }

    WeakResourceHandle::WeakResourceHandle(WeakResourceHandle&& other) noexcept
        : m_registry(other.m_registry), m_slot(other.m_slot), m_generation(other.m_generation), m_key(other.m_key)
    {
        other.m_registry = nullptr;
        other.m_slot = 0;
        other.m_generation = 0;
        other.m_key = {};
    }

    WeakResourceHandle::~WeakResourceHandle()
    {
        Reset();
    }

    WeakResourceHandle& WeakResourceHandle::operator=(const WeakResourceHandle& other) noexcept
    {
        if (this != &other)
        {
            WeakResourceHandle copy(other);
            *this = static_cast<WeakResourceHandle&&>(copy);
        }
        return *this;
    }

    WeakResourceHandle& WeakResourceHandle::operator=(WeakResourceHandle&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            m_registry = other.m_registry;
            m_slot = other.m_slot;
            m_generation = other.m_generation;
            m_key = other.m_key;
            other.m_registry = nullptr;
            other.m_slot = 0;
            other.m_generation = 0;
            other.m_key = {};
        }
        return *this;
    }

    ResourceHandle WeakResourceHandle::Lock() const noexcept
    {
        return m_registry != nullptr ? m_registry->LockWeak(m_slot, m_generation, m_key) : ResourceHandle{};
    }

    ResourcePath WeakResourceHandle::Path() const noexcept
    {
        return m_key.path;
    }

    ResourceTypeId WeakResourceHandle::Type() const noexcept
    {
        return m_key.type;
    }

    u32 WeakResourceHandle::Generation() const noexcept
    {
        return m_generation;
    }

    bool WeakResourceHandle::IsStale() const noexcept
    {
        return m_registry == nullptr || !m_registry->IsGenerationCurrent(m_slot, m_generation);
    }

    bool WeakResourceHandle::IsValid() const noexcept
    {
        return m_registry != nullptr && !IsStale();
    }

    WeakResourceHandle::operator bool() const noexcept
    {
        return IsValid();
    }

    void WeakResourceHandle::Reset() noexcept
    {
        if (m_registry != nullptr)
        {
            m_registry->ReleaseWeak(m_slot, m_generation);
            m_registry = nullptr;
            m_slot = 0;
            m_generation = 0;
            m_key = {};
        }
    }

    ResourceRegistry::~ResourceRegistry()
    {
        static_cast<void>(Shutdown());
    }

    bool ResourceRegistry::Initialize() noexcept
    {
        if (m_impl != nullptr)
        {
            return true;
        }
        if (!memory::Initialize() || !containers::Initialize())
        {
            return false;
        }
        m_impl = AllocateObject<Impl>();
        return m_impl != nullptr;
    }

    bool ResourceRegistry::Shutdown() noexcept
    {
        if (m_impl == nullptr)
        {
            return true;
        }

        m_impl->lock.Acquire();
        for (RegistryEntry* const entry : m_impl->entries)
        {
            if (entry->strongHandles != 0 || entry->weakHandles != 0 ||
                (entry->activeRequest != nullptr && entry->activeRequest->references.GetValue() != 1))
            {
                m_impl->lock.Release();
                return false;
            }
        }

        // Make every entry unreachable before user destruction callbacks run.
        // A callback may call back into the registry, so it must never execute
        // under the registry lock.
        m_impl->pathToSlot.Clear();
        m_impl->loaders.Clear();
        for (RegistryEntry* const entry : m_impl->entries)
        {
            entry->state = State::Unloaded;
        }
        m_impl->lock.Release();

        for (RegistryEntry* const entry : m_impl->entries)
        {
            if (entry->resource != nullptr && entry->destroyResource != nullptr)
            {
                entry->destroyResource(entry->resource, entry->loaderUserData);
            }
            if (entry->activeRequest != nullptr)
            {
                entry->activeRequest->Release();
            }
            DeleteObject(entry);
        }
        m_impl->entries.Clear();
        DeleteObject(m_impl);
        m_impl = nullptr;
        return true;
    }

    bool ResourceRegistry::IsInitialized() const noexcept
    {
        return m_impl != nullptr;
    }

    bool ResourceRegistry::RegisterLoader(const LoaderDescriptor& loader) noexcept
    {
        if (m_impl == nullptr || !loader.IsValid())
        {
            return false;
        }
        VG_SCOPE_LOCK(m_impl->lock);
        return m_impl->loaders.Insert(loader.type, loader).IsSuccessful();
    }

    bool ResourceRegistry::UnregisterLoader(const ResourceTypeId type) noexcept
    {
        if (m_impl == nullptr || type == InvalidResourceTypeId)
        {
            return false;
        }
        VG_SCOPE_LOCK(m_impl->lock);
        for (const RegistryEntry* const entry : m_impl->entries)
        {
            if (entry->key.type == type &&
                (entry->state == State::Queued || entry->state == State::Loading || entry->state == State::Loaded ||
                 entry->state == State::Reloading || entry->state == State::Evicting))
            {
                return false;
            }
        }
        return m_impl->loaders.Remove(type).IsSuccessful();
    }

    bool ResourceRegistry::HasLoader(const ResourceTypeId type) const noexcept
    {
        if (m_impl == nullptr)
        {
            return false;
        }
        VG_SCOPE_SHARED_LOCK(m_impl->lock);
        return m_impl->loaders.Find(type) != m_impl->loaders.End();
    }

    ResourceRequest ResourceRegistry::Request(const ResourceReference reference) noexcept
    {
        if (m_impl == nullptr || !reference.IsValid())
        {
            return MakeFailedRequest(reference, Failure::InvalidPath);
        }
        if (!reference.IsTyped())
        {
            return MakeFailedRequest(reference, Failure::UnknownType);
        }

        LoaderDescriptor loader;
        ResourceRequest request;
        bool invokeLoader = false;

        m_impl->lock.Acquire();
        ++m_impl->issuedRequests;
        if (!m_impl->loaders.Find(reference.ExpectedType(), loader))
        {
            m_impl->lock.Release();
            return MakeFailedRequest(reference, Failure::UnknownType);
        }

        u32 slot = 0;
        RegistryEntry* entry = nullptr;
        if (m_impl->pathToSlot.Find(reference.Path().Id(), slot))
        {
            entry = m_impl->entries[slot];
            if (entry->key.type != reference.ExpectedType())
            {
                m_impl->lock.Release();
                return MakeFailedRequest(reference, Failure::UnknownType);
            }
        }
        else
        {
            entry = AllocateObject<RegistryEntry>();
            if (entry == nullptr)
            {
                m_impl->lock.Release();
                return MakeFailedRequest(reference, Failure::OutOfMemory);
            }
            entry->key = reference.Key();
            slot = m_impl->entries.Size();
            m_impl->entries.PushBack(entry);
            if (!m_impl->pathToSlot.Insert(reference.Path().Id(), slot).IsSuccessful())
            {
                m_impl->entries.PopBack();
                DeleteObject(entry);
                m_impl->lock.Release();
                return MakeFailedRequest(reference, Failure::InternalError);
            }
        }

        if (entry->state == State::Evicting && entry->resource != nullptr && entry->strongHandles != 0 && entry->activeRequest != nullptr)
        {
            // A retained dependency may make an evicting object reachable
            // again before its last strong handle is released. Cancel the
            // deferred eviction and coalesce onto the published operation.
            entry->state = State::Loaded;
        }
        if (entry->activeRequest != nullptr && (entry->state == State::Queued || entry->state == State::Loading ||
                                                entry->state == State::Loaded || entry->state == State::Reloading))
        {
            ++m_impl->coalescedRequests;
            request = ResourceRequest(entry->activeRequest);
            m_impl->lock.Release();
            return request;
        }
        if (entry->state == State::Evicting)
        {
            m_impl->lock.Release();
            return MakeFailedRequest(reference, Failure::Cancelled);
        }

        if (entry->activeRequest != nullptr)
        {
            entry->activeRequest->Release();
            entry->activeRequest = nullptr;
        }

        u64 serial = m_impl->nextRequestSerial++;
        if (serial == 0)
        {
            serial = m_impl->nextRequestSerial++;
        }
        ResourceRequest::Control* const control =
            AllocateObject<ResourceRequest::Control>(this, reference, slot, entry->generation, serial, State::Queued, Failure::None);
        if (control == nullptr)
        {
            m_impl->lock.Release();
            return MakeFailedRequest(reference, Failure::OutOfMemory);
        }

        entry->state = State::Queued;
        entry->failure = Failure::None;
        entry->requestSerial = serial;
        entry->activeRequest = control;
        entry->destroyResource = loader.destroyResource;
        entry->loaderUserData = loader.userData;
        request = ResourceRequest(control);
        invokeLoader = true;
        m_impl->lock.Release();

        if (invokeLoader)
        {
            loader.beginLoad(*this, request, loader.userData);
        }
        return request;
    }

    bool ResourceRegistry::BeginLoading(const ResourceRequest& request) noexcept
    {
        if (m_impl == nullptr || request.m_control == nullptr || request.m_control->owner != this)
        {
            return false;
        }
        VG_SCOPE_LOCK(m_impl->lock);
        RegistryEntry* entry = nullptr;
        if (!IsRequestCurrent(*m_impl, *request.m_control, entry) || entry->state != State::Queued)
        {
            return false;
        }
        entry->state = State::Loading;
        request.m_control->state.SetValue(static_cast<u32>(State::Loading));
        return true;
    }

    bool ResourceRegistry::Publish(const ResourceRequest& request, ResourceObject* const resource) noexcept
    {
        if (m_impl == nullptr || request.m_control == nullptr || request.m_control->owner != this || resource == nullptr)
        {
            return false;
        }

        VG_SCOPE_LOCK(m_impl->lock);
        RegistryEntry* entry = nullptr;
        if (!IsRequestCurrent(*m_impl, *request.m_control, entry) || entry->state != State::Loading ||
            resource->Type() != entry->key.type || entry->resource != nullptr)
        {
            return false;
        }

        entry->resource = resource;
        entry->state = State::Loaded;
        entry->failure = Failure::None;
        request.m_control->failure.SetValue(static_cast<u32>(Failure::None));
        request.m_control->state.SetValue(static_cast<u32>(State::Loaded));
        request.m_control->completion.Signal();
        return true;
    }

    bool ResourceRegistry::Fail(const ResourceRequest& request, const Failure failure) noexcept
    {
        if (m_impl == nullptr || request.m_control == nullptr || request.m_control->owner != this || failure == Failure::None ||
            failure == Failure::Cancelled)
        {
            return false;
        }

        VG_SCOPE_LOCK(m_impl->lock);
        RegistryEntry* entry = nullptr;
        if (!IsRequestCurrent(*m_impl, *request.m_control, entry) ||
            (entry->state != State::Queued && entry->state != State::Loading && entry->state != State::Reloading))
        {
            return false;
        }
        entry->state = State::Failed;
        entry->failure = failure;
        request.m_control->failure.SetValue(static_cast<u32>(failure));
        request.m_control->state.SetValue(static_cast<u32>(State::Failed));
        request.m_control->completion.Signal();
        return true;
    }

    bool ResourceRegistry::Cancel(const ResourceRequest& request) noexcept
    {
        if (m_impl == nullptr || request.m_control == nullptr || request.m_control->owner != this)
        {
            return false;
        }

        VG_SCOPE_LOCK(m_impl->lock);
        RegistryEntry* entry = nullptr;
        if (!IsRequestCurrent(*m_impl, *request.m_control, entry) || (entry->state != State::Queued && entry->state != State::Loading))
        {
            return false;
        }
        entry->state = State::Cancelled;
        entry->failure = Failure::Cancelled;
        request.m_control->failure.SetValue(static_cast<u32>(Failure::Cancelled));
        request.m_control->state.SetValue(static_cast<u32>(State::Cancelled));
        request.m_control->completion.Signal();
        return true;
    }

    ResourceHandle ResourceRegistry::TryAcquire(const ResourceReference reference) noexcept
    {
        if (m_impl == nullptr || !reference.IsValid())
        {
            return {};
        }

        VG_SCOPE_LOCK(m_impl->lock);
        u32 slot = 0;
        if (!m_impl->pathToSlot.Find(reference.Path().Id(), slot))
        {
            return {};
        }
        RegistryEntry* const entry = m_impl->entries[slot];
        if (!IsObjectAlive(entry->state) || entry->resource == nullptr ||
            (reference.IsTyped() && reference.ExpectedType() != entry->key.type))
        {
            return {};
        }
        ++entry->strongHandles;
        return ResourceHandle(this, slot, entry->generation, entry->key, ResourceHandle::AdoptTag{});
    }

    bool ResourceRegistry::Evict(const ResourcePath path) noexcept
    {
        if (m_impl == nullptr || !path.IsValid())
        {
            return false;
        }

        PendingDestruction pending;
        m_impl->lock.Acquire();
        u32 slot = 0;
        if (!m_impl->pathToSlot.Find(path.Id(), slot))
        {
            m_impl->lock.Release();
            return false;
        }
        RegistryEntry* const entry = m_impl->entries[slot];
        if (entry->state != State::Loaded || entry->resource == nullptr)
        {
            m_impl->lock.Release();
            return false;
        }

        entry->state = State::Evicting;
        if (entry->strongHandles == 0)
        {
            pending.resource = entry->resource;
            pending.destroy = entry->destroyResource;
            pending.userData = entry->loaderUserData;
            pending.request = entry->activeRequest;
            entry->resource = nullptr;
            entry->activeRequest = nullptr;
            entry->generation = NextGeneration(entry->generation);
            entry->state = State::Unloaded;
            entry->failure = Failure::None;
        }
        m_impl->lock.Release();
        pending.Execute();
        return true;
    }

    State ResourceRegistry::GetState(const ResourcePath path) const noexcept
    {
        if (m_impl == nullptr || !path.IsValid())
        {
            return State::Unloaded;
        }
        VG_SCOPE_SHARED_LOCK(m_impl->lock);
        u32 slot = 0;
        if (!m_impl->pathToSlot.Find(path.Id(), slot))
        {
            return State::Unloaded;
        }
        return m_impl->entries[slot]->state;
    }

    RegistryStats ResourceRegistry::GetStats() const noexcept
    {
        RegistryStats stats;
        if (m_impl == nullptr)
        {
            return stats;
        }
        VG_SCOPE_SHARED_LOCK(m_impl->lock);
        stats.registeredLoaders = m_impl->loaders.Size();
        stats.knownResources = m_impl->entries.Size();
        stats.issuedRequests = m_impl->issuedRequests;
        stats.coalescedRequests = m_impl->coalescedRequests;
        for (const RegistryEntry* const entry : m_impl->entries)
        {
            stats.strongHandles += entry->strongHandles;
            stats.weakHandles += entry->weakHandles;
            switch (entry->state)
            {
            case State::Queued:
                ++stats.queuedResources;
                break;
            case State::Loading:
                ++stats.loadingResources;
                break;
            case State::Loaded:
            case State::Reloading:
            case State::Evicting:
                ++stats.loadedResources;
                break;
            case State::Failed:
                ++stats.failedResources;
                break;
            case State::Cancelled:
                ++stats.cancelledResources;
                break;
            case State::Unloaded:
                break;
            }
        }
        return stats;
    }

    void ResourceRegistry::AddStrong(const u32 slot, const u32 generation) noexcept
    {
        if (m_impl == nullptr)
        {
            return;
        }
        VG_SCOPE_LOCK(m_impl->lock);
        if (slot < m_impl->entries.Size())
        {
            RegistryEntry* const entry = m_impl->entries[slot];
            if (entry->generation == generation && IsObjectAlive(entry->state) && entry->resource != nullptr)
            {
                ++entry->strongHandles;
            }
        }
    }

    void ResourceRegistry::ReleaseStrong(const u32 slot, const u32 generation) noexcept
    {
        if (m_impl == nullptr)
        {
            return;
        }
        PendingDestruction pending;
        m_impl->lock.Acquire();
        if (slot < m_impl->entries.Size())
        {
            RegistryEntry* const entry = m_impl->entries[slot];
            if (entry->generation == generation && entry->strongHandles != 0)
            {
                --entry->strongHandles;
                if (entry->strongHandles == 0 && entry->state == State::Evicting)
                {
                    pending.resource = entry->resource;
                    pending.destroy = entry->destroyResource;
                    pending.userData = entry->loaderUserData;
                    pending.request = entry->activeRequest;
                    entry->resource = nullptr;
                    entry->activeRequest = nullptr;
                    entry->generation = NextGeneration(entry->generation);
                    entry->state = State::Unloaded;
                    entry->failure = Failure::None;
                }
            }
        }
        m_impl->lock.Release();
        pending.Execute();
    }

    void ResourceRegistry::AddWeak(const u32 slot, const u32 generation) noexcept
    {
        if (m_impl == nullptr)
        {
            return;
        }
        VG_SCOPE_LOCK(m_impl->lock);
        if (slot < m_impl->entries.Size())
        {
            RegistryEntry* const entry = m_impl->entries[slot];
            if (entry->generation == generation)
            {
                ++entry->weakHandles;
            }
        }
    }

    void ResourceRegistry::ReleaseWeak(const u32 slot, const u32) noexcept
    {
        if (m_impl == nullptr)
        {
            return;
        }
        VG_SCOPE_LOCK(m_impl->lock);
        if (slot < m_impl->entries.Size() && m_impl->entries[slot]->weakHandles != 0)
        {
            --m_impl->entries[slot]->weakHandles;
        }
    }

    ResourceObject* ResourceRegistry::Resolve(const u32 slot, const u32 generation) const noexcept
    {
        if (m_impl == nullptr)
        {
            return nullptr;
        }
        VG_SCOPE_SHARED_LOCK(m_impl->lock);
        if (slot >= m_impl->entries.Size())
        {
            return nullptr;
        }
        const RegistryEntry* const entry = m_impl->entries[slot];
        return entry->generation == generation && IsObjectAlive(entry->state) ? entry->resource : nullptr;
    }

    ResourceHandle ResourceRegistry::LockWeak(const u32 slot, const u32 generation, const ResourceKey key) noexcept
    {
        if (m_impl == nullptr)
        {
            return {};
        }
        VG_SCOPE_LOCK(m_impl->lock);
        if (slot >= m_impl->entries.Size())
        {
            return {};
        }
        RegistryEntry* const entry = m_impl->entries[slot];
        if (entry->generation != generation || !IsObjectAlive(entry->state) || entry->resource == nullptr)
        {
            return {};
        }
        ++entry->strongHandles;
        return ResourceHandle(this, slot, generation, key, ResourceHandle::AdoptTag{});
    }

    bool ResourceRegistry::IsGenerationCurrent(const u32 slot, const u32 generation) const noexcept
    {
        if (m_impl == nullptr)
        {
            return false;
        }
        VG_SCOPE_SHARED_LOCK(m_impl->lock);
        if (slot >= m_impl->entries.Size())
        {
            return false;
        }
        const RegistryEntry* const entry = m_impl->entries[slot];
        return entry->generation == generation && IsObjectAlive(entry->state) && entry->resource != nullptr;
    }

    ResourceHandle ResourceRegistry::AcquireRequest(const ResourceRequest& request) noexcept
    {
        if (m_impl == nullptr || request.m_control == nullptr || request.m_control->owner != this)
        {
            return {};
        }
        VG_SCOPE_LOCK(m_impl->lock);
        RegistryEntry* entry = nullptr;
        if (!IsRequestCurrent(*m_impl, *request.m_control, entry) || entry->state != State::Loaded || entry->resource == nullptr)
        {
            return {};
        }
        ++entry->strongHandles;
        return ResourceHandle(this, request.m_control->slot, entry->generation, entry->key, ResourceHandle::AdoptTag{});
    }
} // namespace vanguard::resources
