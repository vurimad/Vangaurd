#include <vanguard/resources/resources.hpp>

#include <vanguard/concurrency/concurrency.hpp>
#include <vanguard/memory/memory.hpp>
#include <vanguard/system/assert.hpp>

#include <new>

namespace
{
    using namespace vanguard;
    using namespace vanguard::resources;

    inline constexpr u32 InvalidRegistrySlot = 0xffffffffu;

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
        Control(ResourceRegistry* const registry, const ResourceReference reference, const u32 slotIndex, const u32 entryGeneration, const u64 operationSerial,
                const State initialState, const Failure initialFailure) noexcept
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
            u32 replacementSlot = InvalidRegistrySlot;
            u32 replacesSlot = InvalidRegistrySlot;
            bool evictAfterReplacement = false;
            bool recycled = false;
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
        explicit Impl(const ResourceRegistryConfig& registryConfig) noexcept
            : entries(memory::pools::Resources::GetInstance()), recycledSlots(memory::pools::Resources::GetInstance()), pathToSlot(memory::pools::Resources::GetInstance()),
              loaders(memory::pools::Resources::GetInstance()), config(registryConfig)
        {
        }

        concurrency::RWLock lock;
        containers::DynamicArray<RegistryEntry*> entries;
        containers::DynamicArray<u32> recycledSlots;
        containers::HashMap<ResourceId, u32> pathToSlot;
        containers::HashMap<ResourceTypeId, LoaderDescriptor> loaders;
        ResourceRegistryConfig config;
        u64 nextRequestSerial = 1;
        u64 issuedRequests = 0;
        u64 coalescedRequests = 0;

        [[nodiscard]] u32 AllocateSlot() noexcept
        {
            if (!recycledSlots.Empty())
            {
                const u32 slot = recycledSlots.Back();
                recycledSlots.PopBack();
                RegistryEntry* const entry = entries[slot];
                VG_ASSERT_MSG(entry != nullptr && entry->recycled, "recycled resource-registry slot must be empty");
                entry->recycled = false;
                return slot;
            }
            if (entries.Size() >= config.maximumEntries)
                return InvalidRegistrySlot;
            RegistryEntry* const entry = AllocateObject<RegistryEntry>();
            if (entry == nullptr)
                return InvalidRegistrySlot;
            const u32 slot = entries.Size();
            entries.PushBack(entry);
            if (entries.Size() != slot + 1u)
            {
                DeleteObject(entry);
                return InvalidRegistrySlot;
            }
            return slot;
        }

        void RecycleSlotIfPossible(const u32 slot) noexcept
        {
            if (slot >= entries.Size())
                return;
            RegistryEntry* const entry = entries[slot];
            if (entry == nullptr || entry->recycled || entry->state != State::Unloaded || entry->resource != nullptr || entry->activeRequest != nullptr ||
                entry->strongHandles != 0 || entry->weakHandles != 0 || entry->replacementSlot != InvalidRegistrySlot || entry->replacesSlot != InvalidRegistrySlot)
                return;
            u32 current = InvalidRegistrySlot;
            if (entry->key.path.IsValid() && pathToSlot.Find(entry->key.path.Id(), current) && current == slot)
                static_cast<void>(pathToSlot.Remove(entry->key.path.Id()));
            entry->key = {};
            entry->failure = Failure::None;
            entry->destroyResource = nullptr;
            entry->loaderUserData = nullptr;
            entry->requestSerial = 0;
            entry->evictAfterReplacement = false;
            entry->recycled = true;
            recycledSlots.PushBack(slot);
        }
    };

    namespace
    {
        template <typename Implementation>
        [[nodiscard]] bool IsRequestCurrent(const Implementation& impl, const ResourceRequest::Control& control, RegistryEntry*& entry) noexcept
        {
            if (control.slot >= impl.entries.Size())
            {
                return false;
            }
            entry = impl.entries[control.slot];
            return entry != nullptr && entry->generation == control.generation && entry->requestSerial == control.serial && entry->activeRequest == &control;
        }

        [[nodiscard]] bool IsObjectAlive(const State state) noexcept
        {
            return state == State::Loaded || state == State::Reloading || state == State::Evicting;
        }
    } // namespace

    ResourceRequest ResourceRegistry::MakeFailedRequest(const ResourceReference reference, const Failure failure) noexcept
    {
        ResourceRequest::Control* const control = AllocateObject<ResourceRequest::Control>(this, reference, 0, 0, 0, State::Failed, failure);
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

    State ResourceRequest::GetStatus() const noexcept
    {
        return m_control != nullptr ? static_cast<State>(m_control->state.GetValue()) : State::Failed;
    }

    Failure ResourceRequest::GetError() const noexcept
    {
        return m_control != nullptr ? static_cast<Failure>(m_control->failure.GetValue()) : Failure::InternalError;
    }

    bool ResourceRequest::HasFinished() const noexcept
    {
        return m_control != nullptr && IsTerminal(GetStatus());
    }

    bool ResourceRequest::HasLoaded() const noexcept
    {
        return m_control != nullptr && GetStatus() == State::Loaded;
    }

    bool ResourceRequest::HasFailed() const noexcept
    {
        return m_control != nullptr && (GetStatus() == State::Failed || GetStatus() == State::Cancelled);
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
        if (m_control == nullptr || GetStatus() != State::Loaded || m_control->owner == nullptr)
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

    ResourceHandle::ResourceHandle(ResourceRegistry* const registry, const u32 slot, const u32 generation, const ResourceKey key, AdoptTag) noexcept
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

    ResourcePath ResourceHandle::GetPath() const noexcept
    {
        return m_key.path;
    }

    ResourceTypeId ResourceHandle::GetType() const noexcept
    {
        return m_key.type;
    }

    u32 ResourceHandle::GetGeneration() const noexcept
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

    WeakResourceHandle::WeakResourceHandle(ResourceRegistry* const registry, const u32 slot, const u32 generation, const ResourceKey key, AdoptTag) noexcept
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

    ResourcePath WeakResourceHandle::GetPath() const noexcept
    {
        return m_key.path;
    }

    ResourceTypeId WeakResourceHandle::GetType() const noexcept
    {
        return m_key.type;
    }

    u32 WeakResourceHandle::GetGeneration() const noexcept
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

    bool ResourceRegistry::Initialize(const ResourceRegistryConfig& config) noexcept
    {
        if (m_impl != nullptr)
        {
            return true;
        }
        if (config.maximumEntries == 0 || !memory::Initialize() || !containers::Initialize())
        {
            return false;
        }
        m_impl = AllocateObject<Impl>(config);
        if (m_impl == nullptr)
            return false;
        m_impl->entries.Reserve(config.maximumEntries);
        m_impl->recycledSlots.Reserve(config.maximumEntries);
        m_impl->pathToSlot.Reserve(config.maximumEntries);
        if (m_impl->entries.Capacity() < config.maximumEntries || m_impl->recycledSlots.Capacity() < config.maximumEntries ||
            m_impl->pathToSlot.Capacity() < config.maximumEntries)
        {
            DeleteObject(m_impl);
            m_impl = nullptr;
            return false;
        }
        return true;
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
            if (entry->strongHandles != 0 || entry->weakHandles != 0 || (entry->activeRequest != nullptr && entry->activeRequest->references.GetValue() != 1))
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
            if (entry->key.type == type && (entry->state == State::Queued || entry->state == State::Loading || entry->state == State::Loaded ||
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
        if (m_impl->pathToSlot.Find(reference.GetPath().Id(), slot))
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
            slot = m_impl->AllocateSlot();
            if (slot == InvalidRegistrySlot)
            {
                m_impl->lock.Release();
                return MakeFailedRequest(reference, Failure::OutOfMemory);
            }
            entry = m_impl->entries[slot];
            entry->key = reference.GetKey();
            if (!m_impl->pathToSlot.Insert(reference.GetPath().Id(), slot).IsSuccessful())
            {
                entry->state = State::Unloaded;
                m_impl->RecycleSlotIfPossible(slot);
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
        if (entry->activeRequest != nullptr &&
            (entry->state == State::Queued || entry->state == State::Loading || entry->state == State::Loaded || entry->state == State::Reloading))
        {
            // A replacement may have started from an Evicting generation retained
            // only by old strong handles. A genuinely new ordinary request revives
            // that current generation just like the Evicting path above, so a
            // failed/cancelled candidate must restore it to Loaded rather than
            // destroying it underneath the new caller's request interest.
            if (entry->state == State::Reloading && entry->evictAfterReplacement)
                entry->evictAfterReplacement = false;
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

    ResourceRequest ResourceRegistry::RequestReplacement(const ResourceReference reference) noexcept
    {
        if (m_impl == nullptr || !reference.IsValid())
            return MakeFailedRequest(reference, Failure::InvalidPath);
        if (!reference.IsTyped())
            return MakeFailedRequest(reference, Failure::UnknownType);

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

        u32 currentSlot = InvalidRegistrySlot;
        if (!m_impl->pathToSlot.Find(reference.GetPath().Id(), currentSlot) || currentSlot >= m_impl->entries.Size())
        {
            m_impl->lock.Release();
            return MakeFailedRequest(reference, Failure::NotFound);
        }
        RegistryEntry* const current = m_impl->entries[currentSlot];
        if (current == nullptr || current->key.type != reference.ExpectedType() || current->resource == nullptr ||
            (current->state != State::Loaded && current->state != State::Reloading &&
             !(current->state == State::Evicting && current->strongHandles != 0)))
        {
            m_impl->lock.Release();
            return MakeFailedRequest(reference, Failure::NotFound);
        }

        if (current->replacementSlot != InvalidRegistrySlot)
        {
            const u32 candidateSlot = current->replacementSlot;
            RegistryEntry* const candidate = candidateSlot < m_impl->entries.Size() ? m_impl->entries[candidateSlot] : nullptr;
            if (candidate != nullptr && candidate->replacesSlot == currentSlot && candidate->activeRequest != nullptr &&
                (candidate->state == State::Queued || candidate->state == State::Loading))
            {
                ++m_impl->coalescedRequests;
                request = ResourceRequest(candidate->activeRequest);
                m_impl->lock.Release();
                return request;
            }
            m_impl->lock.Release();
            return MakeFailedRequest(reference, Failure::InternalError);
        }

        const u32 candidateSlot = m_impl->AllocateSlot();
        if (candidateSlot == InvalidRegistrySlot)
        {
            m_impl->lock.Release();
            return MakeFailedRequest(reference, Failure::OutOfMemory);
        }
        RegistryEntry* const candidate = m_impl->entries[candidateSlot];
        candidate->key = reference.GetKey();
        // ResourceHandle exposes path/type/generation but intentionally not its
        // registry slot. A replacement therefore needs a path-wise generation
        // distinct from the current slot even when the candidate uses a freshly
        // allocated slot whose local generation would otherwise also be one.
        candidate->generation = NextGeneration(current->generation);
        candidate->replacesSlot = currentSlot;
        candidate->destroyResource = loader.destroyResource;
        candidate->loaderUserData = loader.userData;

        u64 serial = m_impl->nextRequestSerial++;
        if (serial == 0)
            serial = m_impl->nextRequestSerial++;
        ResourceRequest::Control* const control =
            AllocateObject<ResourceRequest::Control>(this, reference, candidateSlot, candidate->generation, serial, State::Queued, Failure::None);
        if (control == nullptr)
        {
            candidate->replacesSlot = InvalidRegistrySlot;
            m_impl->RecycleSlotIfPossible(candidateSlot);
            m_impl->lock.Release();
            return MakeFailedRequest(reference, Failure::OutOfMemory);
        }

        candidate->state = State::Queued;
        candidate->failure = Failure::None;
        candidate->requestSerial = serial;
        candidate->activeRequest = control;
        current->replacementSlot = candidateSlot;
        current->evictAfterReplacement = current->state == State::Evicting;
        current->state = State::Reloading;
        request = ResourceRequest(control);
        invokeLoader = true;
        m_impl->lock.Release();

        if (invokeLoader)
            loader.beginLoad(*this, request, loader.userData);
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

        PendingDestruction retired;
        m_impl->lock.Acquire();
        RegistryEntry* entry = nullptr;
        if (!IsRequestCurrent(*m_impl, *request.m_control, entry) || entry->state != State::Loading || resource->GetType() != entry->key.type ||
            entry->resource != nullptr)
        {
            m_impl->lock.Release();
            return false;
        }

        entry->resource = resource;
        entry->state = State::Loaded;
        entry->failure = Failure::None;
        if (entry->replacesSlot != InvalidRegistrySlot)
        {
            const u32 candidateSlot = request.m_control->slot;
            const u32 previousSlot = entry->replacesSlot;
            if (previousSlot >= m_impl->entries.Size())
            {
                entry->resource = nullptr;
                entry->state = State::Loading;
                m_impl->lock.Release();
                return false;
            }
            RegistryEntry* const previous = m_impl->entries[previousSlot];
            u32 mapped = InvalidRegistrySlot;
            if (previous == nullptr || previous->replacementSlot != candidateSlot || previous->resource == nullptr ||
                previous->state != State::Reloading || !m_impl->pathToSlot.Find(entry->key.path.Id(), mapped) || mapped != previousSlot ||
                !m_impl->pathToSlot.Set(entry->key.path.Id(), candidateSlot).IsSuccessful())
            {
                entry->resource = nullptr;
                entry->state = State::Loading;
                m_impl->lock.Release();
                return false;
            }

            previous->replacementSlot = InvalidRegistrySlot;
            previous->state = State::Evicting;
            entry->replacesSlot = InvalidRegistrySlot;
            if (previous->strongHandles == 0)
            {
                retired.resource = previous->resource;
                retired.destroy = previous->destroyResource;
                retired.userData = previous->loaderUserData;
                retired.request = previous->activeRequest;
                previous->resource = nullptr;
                previous->activeRequest = nullptr;
                previous->generation = NextGeneration(previous->generation);
                previous->state = State::Unloaded;
                previous->failure = Failure::None;
                previous->evictAfterReplacement = false;
                m_impl->RecycleSlotIfPossible(previousSlot);
            }
        }
        request.m_control->failure.SetValue(static_cast<u32>(Failure::None));
        request.m_control->state.SetValue(static_cast<u32>(State::Loaded));
        request.m_control->completion.Signal();
        m_impl->lock.Release();
        retired.Execute();
        return true;
    }

    bool ResourceRegistry::Fail(const ResourceRequest& request, const Failure failure) noexcept
    {
        if (m_impl == nullptr || request.m_control == nullptr || request.m_control->owner != this || failure == Failure::None || failure == Failure::Cancelled)
        {
            return false;
        }

        PendingDestruction pending;
        m_impl->lock.Acquire();
        RegistryEntry* entry = nullptr;
        if (!IsRequestCurrent(*m_impl, *request.m_control, entry) ||
            (entry->state != State::Queued && entry->state != State::Loading && entry->state != State::Reloading))
        {
            m_impl->lock.Release();
            return false;
        }
        const u32 failedSlot = request.m_control->slot;
        const u32 previousSlot = entry->replacesSlot;
        entry->state = State::Failed;
        entry->failure = failure;
        request.m_control->failure.SetValue(static_cast<u32>(failure));
        request.m_control->state.SetValue(static_cast<u32>(State::Failed));
        request.m_control->completion.Signal();
        if (previousSlot != InvalidRegistrySlot && previousSlot < m_impl->entries.Size())
        {
            RegistryEntry* const previous = m_impl->entries[previousSlot];
            if (previous != nullptr && previous->replacementSlot == failedSlot)
            {
                previous->replacementSlot = InvalidRegistrySlot;
                previous->state = previous->evictAfterReplacement ? State::Evicting : State::Loaded;
                if (previous->evictAfterReplacement && previous->strongHandles == 0)
                {
                    pending.resource = previous->resource;
                    pending.destroy = previous->destroyResource;
                    pending.userData = previous->loaderUserData;
                    pending.request = previous->activeRequest;
                    previous->resource = nullptr;
                    previous->activeRequest = nullptr;
                    previous->generation = NextGeneration(previous->generation);
                    previous->state = State::Unloaded;
                    previous->failure = Failure::None;
                    previous->evictAfterReplacement = false;
                    m_impl->RecycleSlotIfPossible(previousSlot);
                }
            }
            entry->replacesSlot = InvalidRegistrySlot;
            ResourceRequest::Control* const registryReference = entry->activeRequest;
            entry->activeRequest = nullptr;
            entry->generation = NextGeneration(entry->generation);
            entry->state = State::Unloaded;
            entry->failure = Failure::None;
            m_impl->RecycleSlotIfPossible(failedSlot);
            m_impl->lock.Release();
            if (registryReference != nullptr)
                registryReference->Release();
            pending.Execute();
            return true;
        }
        m_impl->lock.Release();
        return true;
    }

    bool ResourceRegistry::Cancel(const ResourceRequest& request) noexcept
    {
        if (m_impl == nullptr || request.m_control == nullptr || request.m_control->owner != this)
        {
            return false;
        }

        PendingDestruction pending;
        m_impl->lock.Acquire();
        RegistryEntry* entry = nullptr;
        if (!IsRequestCurrent(*m_impl, *request.m_control, entry) || (entry->state != State::Queued && entry->state != State::Loading))
        {
            m_impl->lock.Release();
            return false;
        }
        const u32 cancelledSlot = request.m_control->slot;
        const u32 previousSlot = entry->replacesSlot;
        entry->state = State::Cancelled;
        entry->failure = Failure::Cancelled;
        request.m_control->failure.SetValue(static_cast<u32>(Failure::Cancelled));
        request.m_control->state.SetValue(static_cast<u32>(State::Cancelled));
        request.m_control->completion.Signal();
        if (previousSlot != InvalidRegistrySlot && previousSlot < m_impl->entries.Size())
        {
            RegistryEntry* const previous = m_impl->entries[previousSlot];
            if (previous != nullptr && previous->replacementSlot == cancelledSlot)
            {
                previous->replacementSlot = InvalidRegistrySlot;
                previous->state = previous->evictAfterReplacement ? State::Evicting : State::Loaded;
                if (previous->evictAfterReplacement && previous->strongHandles == 0)
                {
                    pending.resource = previous->resource;
                    pending.destroy = previous->destroyResource;
                    pending.userData = previous->loaderUserData;
                    pending.request = previous->activeRequest;
                    previous->resource = nullptr;
                    previous->activeRequest = nullptr;
                    previous->generation = NextGeneration(previous->generation);
                    previous->state = State::Unloaded;
                    previous->failure = Failure::None;
                    previous->evictAfterReplacement = false;
                    m_impl->RecycleSlotIfPossible(previousSlot);
                }
            }
            entry->replacesSlot = InvalidRegistrySlot;
            ResourceRequest::Control* const registryReference = entry->activeRequest;
            entry->activeRequest = nullptr;
            entry->generation = NextGeneration(entry->generation);
            entry->state = State::Unloaded;
            entry->failure = Failure::None;
            m_impl->RecycleSlotIfPossible(cancelledSlot);
            m_impl->lock.Release();
            if (registryReference != nullptr)
                registryReference->Release();
            pending.Execute();
            return true;
        }
        m_impl->lock.Release();
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
        if (!m_impl->pathToSlot.Find(reference.GetPath().Id(), slot))
        {
            return {};
        }
        RegistryEntry* const entry = m_impl->entries[slot];
        if (!IsObjectAlive(entry->state) || entry->resource == nullptr || (reference.IsTyped() && reference.ExpectedType() != entry->key.type))
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
        if ((entry->state != State::Loaded && entry->state != State::Reloading) || entry->resource == nullptr)
        {
            m_impl->lock.Release();
            return false;
        }

        if (entry->state == State::Reloading)
        {
            entry->evictAfterReplacement = true;
            m_impl->lock.Release();
            return true;
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
            m_impl->RecycleSlotIfPossible(slot);
        }
        m_impl->lock.Release();
        pending.Execute();
        return true;
    }

    bool ResourceRegistry::Evict(const ResourceRequest& request) noexcept
    {
        if (m_impl == nullptr || request.m_control == nullptr || request.m_control->owner != this)
            return false;

        PendingDestruction pending;
        m_impl->lock.Acquire();
        RegistryEntry* entry = nullptr;
        if (!IsRequestCurrent(*m_impl, *request.m_control, entry) || entry->resource == nullptr ||
            (entry->state != State::Loaded && entry->state != State::Reloading && entry->state != State::Evicting))
        {
            m_impl->lock.Release();
            return false;
        }
        if (entry->state == State::Reloading)
        {
            entry->evictAfterReplacement = true;
            m_impl->lock.Release();
            return true;
        }
        if (entry->state == State::Evicting)
        {
            m_impl->lock.Release();
            return true;
        }

        entry->state = State::Evicting;
        if (entry->strongHandles == 0)
        {
            const u32 slot = request.m_control->slot;
            pending.resource = entry->resource;
            pending.destroy = entry->destroyResource;
            pending.userData = entry->loaderUserData;
            pending.request = entry->activeRequest;
            entry->resource = nullptr;
            entry->activeRequest = nullptr;
            entry->generation = NextGeneration(entry->generation);
            entry->state = State::Unloaded;
            entry->failure = Failure::None;
            entry->evictAfterReplacement = false;
            m_impl->RecycleSlotIfPossible(slot);
        }
        m_impl->lock.Release();
        pending.Execute();
        return true;
    }

    bool ResourceRegistry::IsCurrent(const ResourceRequest& request) const noexcept
    {
        if (m_impl == nullptr || request.m_control == nullptr || request.m_control->owner != this)
            return false;
        VG_SCOPE_SHARED_LOCK(m_impl->lock);
        RegistryEntry* entry = nullptr;
        if (!IsRequestCurrent(*m_impl, *request.m_control, entry))
            return false;
        u32 currentSlot = InvalidRegistrySlot;
        return m_impl->pathToSlot.Find(entry->key.path.Id(), currentSlot) && currentSlot == request.m_control->slot &&
               IsObjectAlive(entry->state) && entry->resource != nullptr;
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
                    entry->evictAfterReplacement = false;
                    m_impl->RecycleSlotIfPossible(slot);
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
            m_impl->RecycleSlotIfPossible(slot);
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
        if (!IsRequestCurrent(*m_impl, *request.m_control, entry) || !IsObjectAlive(entry->state) || entry->resource == nullptr)
        {
            return {};
        }
        ++entry->strongHandles;
        return ResourceHandle(this, request.m_control->slot, entry->generation, entry->key, ResourceHandle::AdoptTag{});
    }
} // namespace vanguard::resources
