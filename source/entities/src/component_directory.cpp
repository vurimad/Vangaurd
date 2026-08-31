#include <vanguard/entities/component_directory.hpp>

#include <vanguard/concurrency/thread.hpp>
#include <vanguard/containers/containers.hpp>
#include <vanguard/game_world/game_world.hpp>
#include <vanguard/memory/pool.hpp>

namespace vanguard::entities
{
    namespace
    {
        inline constexpr u32 InvalidIndex = ~u32{0};

        void ClearFailure(ComponentDirectoryFailure* const failure) noexcept
        {
            if (failure != nullptr)
                *failure = {};
        }

        [[nodiscard]] bool Fail(ComponentDirectoryFailure* const failure, const ComponentDirectoryFailureCode code,
                                const char* const message, const ComponentHandle component = {},
                                const ecs::EntityId entity = ecs::InvalidEntityId, const u64 stableId = InvalidComponentStableId) noexcept
        {
            if (failure != nullptr)
            {
                failure->code = code;
                failure->component = component;
                failure->entity = entity;
                failure->stableId = stableId;
                failure->message = message;
            }
            return false;
        }

        [[nodiscard]] constexpr u32 NextGeneration(const u32 generation) noexcept
        {
            const u32 next = generation + 1u;
            return next != 0 ? next : 1u;
        }
    } // namespace

    struct ComponentDirectory::Impl
    {
        VANGUARD_USE_MEMORY_POOL(memory::pools::World);

        struct Slot
        {
            Component* component = nullptr;
            memory::MemoryBlock storage;
            DestroyFunction destroy = nullptr;
            ecs::EntityId entity = ecs::InvalidEntityId;
            u64 stableId = InvalidComponentStableId;
            reflection::SchemaTypeId type = reflection::InvalidSchemaTypeId;
            u32 generation = 0;
            u32 previousEntity = InvalidIndex;
            u32 nextEntity = InvalidIndex;
            u32 previousActive = InvalidIndex;
            u32 nextActive = InvalidIndex;
            u32 nextFree = InvalidIndex;
            bool active = false;
        };

        explicit Impl(const ComponentDirectoryConfig& value) noexcept
            : config(value), slots(memory::pools::World::GetInstance()), entityHeads(memory::pools::World::GetInstance()),
              entityCounts(memory::pools::World::GetInstance())
        {
            slots.Reserve(value.maximumComponents < 4096u ? value.maximumComponents : 4096u);
        }

        [[nodiscard]] Slot* Find(const ComponentHandle handle) noexcept
        {
            if (!handle.IsValid() || handle.index >= slots.Size())
                return nullptr;
            Slot& slot = slots[handle.index];
            return slot.active && slot.generation == handle.generation ? &slot : nullptr;
        }

        [[nodiscard]] const Slot* Find(const ComponentHandle handle) const noexcept
        {
            return const_cast<Impl*>(this)->Find(handle);
        }

        [[nodiscard]] u32 Find(const ecs::EntityId entity, const u64 stableId) const noexcept
        {
            const u32* const head = entityHeads.FindPtr(entity);
            u32 index = head != nullptr ? *head : InvalidIndex;
            while (index != InvalidIndex)
            {
                const Slot& slot = slots[index];
                if (slot.active && slot.stableId == stableId)
                    return index;
                index = slot.nextEntity;
            }
            return InvalidIndex;
        }

        [[nodiscard]] bool Allocate(u32& index, Slot*& slot) noexcept
        {
            if (freeHead != InvalidIndex)
            {
                index = freeHead;
                slot = &slots[index];
                freeHead = slot->nextFree;
            }
            else
            {
                if (slots.Size() == config.maximumComponents)
                    return false;
                index = slots.Size();
                slots.PushBack(Slot{});
                slot = &slots[index];
            }
            const u32 generation = NextGeneration(slot->generation);
            *slot = {};
            slot->generation = generation;
            slot->previousEntity = InvalidIndex;
            slot->nextEntity = InvalidIndex;
            slot->previousActive = InvalidIndex;
            slot->nextActive = InvalidIndex;
            slot->nextFree = InvalidIndex;
            slot->active = true;
            return true;
        }

        void Release(const u32 index) noexcept
        {
            Slot& slot = slots[index];
            const u32 generation = slot.generation;
            slot = {};
            slot.generation = generation;
            slot.previousEntity = InvalidIndex;
            slot.nextEntity = InvalidIndex;
            slot.previousActive = InvalidIndex;
            slot.nextActive = InvalidIndex;
            slot.nextFree = freeHead;
            freeHead = index;
        }

        [[nodiscard]] bool LinkEntity(const u32 index) noexcept
        {
            Slot& slot = slots[index];
            u32* const head = entityHeads.FindPtr(slot.entity);
            slot.nextEntity = head != nullptr ? *head : InvalidIndex;
            if (head != nullptr)
                *head = index;
            else
            {
                if (!entityHeads.Insert(slot.entity, index).IsSuccessful())
                    return false;
                if (!entityCounts.Insert(slot.entity, 0u).IsSuccessful())
                {
                    static_cast<void>(entityHeads.Remove(slot.entity));
                    return false;
                }
                ++stats.entities;
            }
            if (slot.nextEntity != InvalidIndex)
                slots[slot.nextEntity].previousEntity = index;
            ++*entityCounts.FindPtr(slot.entity);
            return true;
        }

        void UnlinkEntity(const u32 index) noexcept
        {
            Slot& slot = slots[index];
            if (slot.previousEntity != InvalidIndex)
                slots[slot.previousEntity].nextEntity = slot.nextEntity;
            else if (u32* const head = entityHeads.FindPtr(slot.entity))
                *head = slot.nextEntity;
            if (slot.nextEntity != InvalidIndex)
                slots[slot.nextEntity].previousEntity = slot.previousEntity;
            u32* const count = entityCounts.FindPtr(slot.entity);
            if (count != nullptr && --*count == 0)
            {
                static_cast<void>(entityCounts.Remove(slot.entity));
                static_cast<void>(entityHeads.Remove(slot.entity));
                --stats.entities;
            }
            slot.previousEntity = InvalidIndex;
            slot.nextEntity = InvalidIndex;
        }

        void LinkActive(const u32 index) noexcept
        {
            Slot& slot = slots[index];
            slot.previousActive = activeTail;
            if (activeTail != InvalidIndex)
                slots[activeTail].nextActive = index;
            else
                activeHead = index;
            activeTail = index;
        }

        void UnlinkActive(const u32 index) noexcept
        {
            Slot& slot = slots[index];
            if (slot.previousActive != InvalidIndex)
                slots[slot.previousActive].nextActive = slot.nextActive;
            else
                activeHead = slot.nextActive;
            if (slot.nextActive != InvalidIndex)
                slots[slot.nextActive].previousActive = slot.previousActive;
            else
                activeTail = slot.previousActive;
            slot.previousActive = InvalidIndex;
            slot.nextActive = InvalidIndex;
        }

        game::GameWorld* world = nullptr;
        ComponentDirectoryConfig config;
        containers::DynamicArray<Slot> slots;
        containers::HashMap<ecs::EntityId, u32> entityHeads;
        containers::HashMap<ecs::EntityId, u32> entityCounts;
        u32 freeHead = InvalidIndex;
        u32 activeHead = InvalidIndex;
        u32 activeTail = InvalidIndex;
        ComponentDirectoryStats stats;
        bool lifecycleMutation = false;
    };

    ComponentDirectory::~ComponentDirectory()
    {
        if (m_impl != nullptr)
            static_cast<void>(Shutdown());
    }

    bool ComponentDirectory::Initialize(game::GameWorld& world, const ComponentDirectoryConfig& config,
                                        ComponentDirectoryFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl != nullptr)
            return Fail(failure, ComponentDirectoryFailureCode::InvalidState, "ComponentDirectory is already initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, ComponentDirectoryFailureCode::WrongThread, "ComponentDirectory must initialize on the main thread");
        if (!world.GetEntities().IsInitialized())
            return Fail(failure, ComponentDirectoryFailureCode::InvalidState, "ComponentDirectory requires an initialized ECS world");
        if (config.maximumComponents == 0)
            return Fail(failure, ComponentDirectoryFailureCode::InvalidConfiguration, "ComponentDirectory maximum component count must be nonzero");
        m_impl = VANGUARD_NEW(Impl)(config);
        if (m_impl == nullptr)
            return Fail(failure, ComponentDirectoryFailureCode::CapacityExceeded, "ComponentDirectory allocation failed");
        m_impl->world = &world;
        m_impl->stats.initialized = true;
        return true;
    }

    bool ComponentDirectory::Shutdown(ComponentDirectoryFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return true;
        if (!concurrency::IsMainThread())
            return Fail(failure, ComponentDirectoryFailureCode::WrongThread, "ComponentDirectory must shut down on the main thread");
        while (m_impl->activeTail != InvalidIndex)
        {
            const Impl::Slot& slot = m_impl->slots[m_impl->activeTail];
            if (!Destroy({m_impl->activeTail, slot.generation}, failure))
                return false;
        }
        m_impl->stats.initialized = false;
        VANGUARD_DELETE(m_impl);
        m_impl = nullptr;
        return true;
    }

    bool ComponentDirectory::IsInitialized() const noexcept
    {
        return m_impl != nullptr;
    }

    game::GameWorld* ComponentDirectory::GetWorld() noexcept
    {
        return m_impl != nullptr ? m_impl->world : nullptr;
    }

    const game::GameWorld* ComponentDirectory::GetWorld() const noexcept
    {
        return const_cast<ComponentDirectory*>(this)->GetWorld();
    }

    bool ComponentDirectory::AllocationFailed(const ecs::EntityId entity, const u64 stableId,
                                              ComponentDirectoryFailure* const failure) noexcept
    {
        return Fail(failure, ComponentDirectoryFailureCode::CapacityExceeded, "stable component allocation failed", {}, entity, stableId);
    }

    bool ComponentDirectory::ValidateCreate(const ecs::EntityId entity, const u64 stableId, const reflection::SchemaTypeId type,
                                            ComponentDirectoryFailure* const failure) const noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, ComponentDirectoryFailureCode::NotInitialized, "ComponentDirectory is not initialized", {}, entity, stableId);
        if (!concurrency::IsMainThread())
            return Fail(failure, ComponentDirectoryFailureCode::WrongThread, "components must be created on the main thread", {}, entity, stableId);
        if (m_impl->lifecycleMutation)
            return Fail(failure, ComponentDirectoryFailureCode::InvalidState, "component creation is not allowed from a lifecycle callback", {}, entity,
                        stableId);
        if (entity == ecs::InvalidEntityId || stableId == InvalidComponentStableId || type == reflection::InvalidSchemaTypeId)
            return Fail(failure, ComponentDirectoryFailureCode::InvalidDescriptor, "invalid stable component descriptor", {}, entity, stableId);
        if (!m_impl->world->GetEntities().Resolve(entity))
            return Fail(failure, ComponentDirectoryFailureCode::InvalidDescriptor, "stable component owner is not a live entity", {}, entity, stableId);
        if (m_impl->Find(entity, stableId) != InvalidIndex)
            return Fail(failure, ComponentDirectoryFailureCode::DuplicateIdentifier, "component stable ID is already used by this entity", {}, entity,
                        stableId);
        if (m_impl->stats.components == m_impl->config.maximumComponents)
            return Fail(failure, ComponentDirectoryFailureCode::CapacityExceeded, "maximum stable component count exceeded", {}, entity, stableId);
        return true;
    }

    bool ComponentDirectory::Adopt(const ecs::EntityId entity, const u64 stableId, const reflection::SchemaTypeId type, Component& component,
                                   const memory::MemoryBlock storage, const DestroyFunction destroy, ComponentHandle& handle,
                                   ComponentDirectoryFailure* const failure) noexcept
    {
        ClearFailure(failure);
        handle = {};
        if (!ValidateCreate(entity, stableId, type, failure))
            return false;
        if (!storage || destroy == nullptr)
            return Fail(failure, ComponentDirectoryFailureCode::InvalidDescriptor, "invalid stable component descriptor", {}, entity, stableId);

        u32 index = InvalidIndex;
        Impl::Slot* slot = nullptr;
        if (!m_impl->Allocate(index, slot))
            return Fail(failure, ComponentDirectoryFailureCode::CapacityExceeded, "component slot allocation failed", {}, entity, stableId);
        slot->component = &component;
        slot->storage = storage;
        slot->destroy = destroy;
        slot->entity = entity;
        slot->stableId = stableId;
        slot->type = type;
        if (!m_impl->LinkEntity(index))
        {
            m_impl->Release(index);
            return Fail(failure, ComponentDirectoryFailureCode::CapacityExceeded, "component entity-directory insertion failed", {}, entity, stableId);
        }
        m_impl->LinkActive(index);

        handle = {index, slot->generation};
        component.m_directory = this;
        component.m_handle = handle;
        component.m_entity = entity;
        component.m_stableId = stableId;
        component.m_type = type;
        ++m_impl->stats.components;
        ++m_impl->stats.createdComponents;
        return true;
    }

    bool ComponentDirectory::BeginInitialization(const ComponentHandle handle, ComponentInitialization& initialization,
                                                  ComponentDirectoryFailure* const failure) noexcept
    {
        ClearFailure(failure);
        initialization = {};
        Impl::Slot* const slot = m_impl != nullptr ? m_impl->Find(handle) : nullptr;
        if (slot == nullptr)
            return Fail(failure, m_impl == nullptr ? ComponentDirectoryFailureCode::NotInitialized : ComponentDirectoryFailureCode::InvalidHandle,
                        "invalid or stale component handle", handle);
        if (!concurrency::IsMainThread())
            return Fail(failure, ComponentDirectoryFailureCode::WrongThread, "component initialization must begin on the main thread", handle, slot->entity,
                        slot->stableId);
        if (m_impl->lifecycleMutation || slot->component->m_initialized || slot->component->m_initializing)
            return Fail(failure, ComponentDirectoryFailureCode::InvalidState, "component cannot enter initialization", handle, slot->entity, slot->stableId);
        slot->component->m_initializing = true;
        initialization.m_component = slot->component;
        initialization.m_handle = handle;
        return true;
    }

    void ComponentDirectory::ExecuteInitialization(ComponentInitialization& initialization, const ComponentInitializeContext& context) noexcept
    {
        if (!initialization.IsValid() || initialization.m_executed)
            return;
        initialization.m_succeeded = initialization.m_component->OnInitialize(context);
        initialization.m_executed = true;
    }

    bool ComponentDirectory::CompleteInitialization(ComponentInitialization& initialization,
                                                     ComponentDirectoryFailure* const failure) noexcept
    {
        ClearFailure(failure);
        Impl::Slot* const slot = m_impl != nullptr ? m_impl->Find(initialization.m_handle) : nullptr;
        if (slot == nullptr || slot->component != initialization.m_component)
            return Fail(failure, m_impl == nullptr ? ComponentDirectoryFailureCode::NotInitialized : ComponentDirectoryFailureCode::InvalidHandle,
                        "invalid component initialization token", initialization.m_handle);
        if (!concurrency::IsMainThread())
            return Fail(failure, ComponentDirectoryFailureCode::WrongThread, "component initialization must complete on the main thread",
                        initialization.m_handle, slot->entity, slot->stableId);
        if (!slot->component->m_initializing)
            return Fail(failure, ComponentDirectoryFailureCode::InvalidState, "component initialization has not executed", initialization.m_handle,
                        slot->entity, slot->stableId);
        if (!initialization.m_executed)
        {
            const ComponentHandle failedHandle = initialization.m_handle;
            slot->component->m_initializing = false;
            initialization = {};
            return Fail(failure, ComponentDirectoryFailureCode::InitializationFailure, "component initialization callback did not execute", failedHandle,
                        slot->entity, slot->stableId);
        }
        slot->component->m_initializing = false;
        if (!initialization.m_succeeded)
        {
            const ComponentHandle failedHandle = initialization.m_handle;
            initialization = {};
            return Fail(failure, ComponentDirectoryFailureCode::InitializationFailure, "component initialization callback failed", failedHandle,
                        slot->entity, slot->stableId);
        }
        slot->component->m_initialized = true;
        ++m_impl->stats.initializedComponents;
        initialization = {};
        return true;
    }

    bool ComponentDirectory::CancelInitialization(ComponentInitialization& initialization,
                                                   ComponentDirectoryFailure* const failure) noexcept
    {
        ClearFailure(failure);
        Impl::Slot* const slot = m_impl != nullptr ? m_impl->Find(initialization.m_handle) : nullptr;
        if (slot == nullptr || slot->component != initialization.m_component)
            return Fail(failure, m_impl == nullptr ? ComponentDirectoryFailureCode::NotInitialized : ComponentDirectoryFailureCode::InvalidHandle,
                        "invalid component initialization token", initialization.m_handle);
        if (!concurrency::IsMainThread())
            return Fail(failure, ComponentDirectoryFailureCode::WrongThread, "component initialization must be cancelled on the main thread",
                        initialization.m_handle, slot->entity, slot->stableId);
        if (!slot->component->m_initializing)
            return Fail(failure, ComponentDirectoryFailureCode::InvalidState, "component is not initializing", initialization.m_handle, slot->entity,
                        slot->stableId);
        slot->component->m_initializing = false;
        initialization = {};
        return true;
    }

    bool ComponentDirectory::AttachComponent(const ComponentHandle handle, ComponentDirectoryFailure* const failure) noexcept
    {
        ClearFailure(failure);
        Impl::Slot* const slot = m_impl != nullptr ? m_impl->Find(handle) : nullptr;
        if (slot == nullptr)
            return Fail(failure, m_impl == nullptr ? ComponentDirectoryFailureCode::NotInitialized : ComponentDirectoryFailureCode::InvalidHandle,
                        "invalid or stale component handle", handle);
        if (!concurrency::IsMainThread())
            return Fail(failure, ComponentDirectoryFailureCode::WrongThread, "components must attach on the main thread", handle, slot->entity,
                        slot->stableId);
        if (m_impl->lifecycleMutation || slot->component->m_initializing || !slot->component->m_initialized || slot->component->m_attached ||
            slot->component->m_postAttached)
            return Fail(failure, ComponentDirectoryFailureCode::InvalidState, "component cannot enter attachment", handle, slot->entity, slot->stableId);
        m_impl->lifecycleMutation = true;
        const bool succeeded = slot->component->OnAttach({*m_impl->world});
        m_impl->lifecycleMutation = false;
        if (!succeeded)
            return Fail(failure, ComponentDirectoryFailureCode::AttachmentFailure, "component attachment callback failed", handle, slot->entity,
                        slot->stableId);
        slot->component->m_attached = true;
        ++m_impl->stats.attachedComponents;
        return true;
    }

    bool ComponentDirectory::PostAttachComponent(const ComponentHandle handle, ComponentDirectoryFailure* const failure) noexcept
    {
        ClearFailure(failure);
        Impl::Slot* const slot = m_impl != nullptr ? m_impl->Find(handle) : nullptr;
        if (slot == nullptr)
            return Fail(failure, m_impl == nullptr ? ComponentDirectoryFailureCode::NotInitialized : ComponentDirectoryFailureCode::InvalidHandle,
                        "invalid or stale component handle", handle);
        if (!concurrency::IsMainThread())
            return Fail(failure, ComponentDirectoryFailureCode::WrongThread, "components must post-attach on the main thread", handle, slot->entity,
                        slot->stableId);
        if (m_impl->lifecycleMutation || !slot->component->m_initialized || !slot->component->m_attached || slot->component->m_postAttached)
            return Fail(failure, ComponentDirectoryFailureCode::InvalidState, "component cannot enter post-attachment", handle, slot->entity,
                        slot->stableId);
        m_impl->lifecycleMutation = true;
        slot->component->OnPostAttach({*m_impl->world});
        m_impl->lifecycleMutation = false;
        slot->component->m_postAttached = true;
        ++m_impl->stats.postAttachedComponents;
        return true;
    }

    bool ComponentDirectory::SetInitialEnabledState(const ComponentHandle handle, const bool componentEnabled, const bool entityEnabled,
                                                    ComponentDirectoryFailure* const failure) noexcept
    {
        ClearFailure(failure);
        Impl::Slot* const slot = m_impl != nullptr ? m_impl->Find(handle) : nullptr;
        if (slot == nullptr)
            return Fail(failure, m_impl == nullptr ? ComponentDirectoryFailureCode::NotInitialized : ComponentDirectoryFailureCode::InvalidHandle,
                        "invalid or stale component handle", handle);
        if (!concurrency::IsMainThread())
            return Fail(failure, ComponentDirectoryFailureCode::WrongThread, "initial component enabled state must be set on the main thread", handle,
                        slot->entity, slot->stableId);
        if (m_impl->lifecycleMutation || slot->component->m_initializing || slot->component->m_initialized || slot->component->m_attached ||
            slot->component->m_postAttached)
            return Fail(failure, ComponentDirectoryFailureCode::InvalidState,
                        "initial component enabled state must be set before initialization", handle, slot->entity, slot->stableId);
        slot->component->m_componentEnabled = componentEnabled;
        slot->component->m_entityEnabled = entityEnabled;
        return true;
    }

    bool ComponentDirectory::SetComponentEnabled(const ComponentHandle handle, const bool enabled,
                                                 ComponentDirectoryFailure* const failure) noexcept
    {
        ClearFailure(failure);
        Impl::Slot* const slot = m_impl != nullptr ? m_impl->Find(handle) : nullptr;
        if (slot == nullptr)
            return Fail(failure, m_impl == nullptr ? ComponentDirectoryFailureCode::NotInitialized : ComponentDirectoryFailureCode::InvalidHandle,
                        "invalid or stale component handle", handle);
        if (!concurrency::IsMainThread())
            return Fail(failure, ComponentDirectoryFailureCode::WrongThread, "components must change enabled state on the main thread", handle,
                        slot->entity, slot->stableId);
        if (m_impl->lifecycleMutation || slot->component->m_initializing)
            return Fail(failure, ComponentDirectoryFailureCode::InvalidState, "component enabled state cannot change during a lifecycle callback", handle,
                        slot->entity, slot->stableId);
        if (slot->component->m_componentEnabled == enabled)
            return true;

        const bool wasEnabled = slot->component->IsEnabled();
        slot->component->m_componentEnabled = enabled;
        if (slot->component->m_attached && wasEnabled != slot->component->IsEnabled())
        {
            m_impl->lifecycleMutation = true;
            slot->component->OnEnabled({*m_impl->world}, slot->component->IsEnabled());
            m_impl->lifecycleMutation = false;
        }
        return true;
    }

    bool ComponentDirectory::SetEntityEnabled(const ecs::EntityId entity, const bool enabled,
                                              ComponentDirectoryFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, ComponentDirectoryFailureCode::NotInitialized, "ComponentDirectory is not initialized", {}, entity);
        if (entity == ecs::InvalidEntityId)
            return Fail(failure, ComponentDirectoryFailureCode::InvalidDescriptor, "invalid entity enabled-state target", {}, entity);
        if (!concurrency::IsMainThread())
            return Fail(failure, ComponentDirectoryFailureCode::WrongThread, "entities must change enabled state on the main thread", {}, entity);
        if (m_impl->lifecycleMutation)
            return Fail(failure, ComponentDirectoryFailureCode::InvalidState, "entity enabled state cannot change during a lifecycle callback", {}, entity);

        const u32* const head = m_impl->entityHeads.FindPtr(entity);
        u32 index = head != nullptr ? *head : InvalidIndex;
        while (index != InvalidIndex)
        {
            Component& component = *m_impl->slots[index].component;
            const bool wasEnabled = component.IsEnabled();
            if (component.m_entityEnabled != enabled)
            {
                component.m_entityEnabled = enabled;
                component.m_enableNotificationPending = component.m_attached && wasEnabled != component.IsEnabled();
            }
            index = m_impl->slots[index].nextEntity;
        }

        m_impl->lifecycleMutation = true;
        if (enabled)
        {
            index = head != nullptr ? *head : InvalidIndex;
            if (index != InvalidIndex)
                while (m_impl->slots[index].nextEntity != InvalidIndex)
                    index = m_impl->slots[index].nextEntity;
            while (index != InvalidIndex)
            {
                Component& component = *m_impl->slots[index].component;
                if (component.m_enableNotificationPending)
                {
                    component.m_enableNotificationPending = false;
                    component.OnEnabled({*m_impl->world}, true);
                }
                index = m_impl->slots[index].previousEntity;
            }
        }
        else
        {
            index = head != nullptr ? *head : InvalidIndex;
            while (index != InvalidIndex)
            {
                Component& component = *m_impl->slots[index].component;
                if (component.m_enableNotificationPending)
                {
                    component.m_enableNotificationPending = false;
                    component.OnEnabled({*m_impl->world}, false);
                }
                index = m_impl->slots[index].nextEntity;
            }
        }
        m_impl->lifecycleMutation = false;
        return true;
    }

    bool ComponentDirectory::DetachComponent(const ComponentHandle handle, ComponentDirectoryFailure* const failure) noexcept
    {
        ClearFailure(failure);
        Impl::Slot* const slot = m_impl != nullptr ? m_impl->Find(handle) : nullptr;
        if (slot == nullptr)
            return Fail(failure, m_impl == nullptr ? ComponentDirectoryFailureCode::NotInitialized : ComponentDirectoryFailureCode::InvalidHandle,
                        "invalid or stale component handle", handle);
        if (!concurrency::IsMainThread())
            return Fail(failure, ComponentDirectoryFailureCode::WrongThread, "components must detach on the main thread", handle, slot->entity,
                        slot->stableId);
        if (m_impl->lifecycleMutation || !slot->component->m_attached)
            return Fail(failure, ComponentDirectoryFailureCode::InvalidState, "component is not attached", handle, slot->entity, slot->stableId);
        m_impl->lifecycleMutation = true;
        slot->component->OnDetach({*m_impl->world});
        m_impl->lifecycleMutation = false;
        if (slot->component->m_postAttached)
        {
            slot->component->m_postAttached = false;
            --m_impl->stats.postAttachedComponents;
        }
        slot->component->m_attached = false;
        --m_impl->stats.attachedComponents;
        return true;
    }

    bool ComponentDirectory::UninitializeComponent(const ComponentHandle handle, ComponentDirectoryFailure* const failure) noexcept
    {
        ClearFailure(failure);
        Impl::Slot* const slot = m_impl != nullptr ? m_impl->Find(handle) : nullptr;
        if (slot == nullptr)
            return Fail(failure, m_impl == nullptr ? ComponentDirectoryFailureCode::NotInitialized : ComponentDirectoryFailureCode::InvalidHandle,
                        "invalid or stale component handle", handle);
        if (!concurrency::IsMainThread())
            return Fail(failure, ComponentDirectoryFailureCode::WrongThread, "components must uninitialize on the main thread", handle, slot->entity,
                        slot->stableId);
        if (m_impl->lifecycleMutation || !slot->component->m_initialized || slot->component->m_attached || slot->component->m_postAttached)
            return Fail(failure, ComponentDirectoryFailureCode::InvalidState, "component cannot uninitialize while attached or inactive", handle,
                        slot->entity, slot->stableId);
        m_impl->lifecycleMutation = true;
        slot->component->OnUninitialize({*m_impl->world});
        m_impl->lifecycleMutation = false;
        slot->component->m_initialized = false;
        --m_impl->stats.initializedComponents;
        return true;
    }

    bool ComponentDirectory::Destroy(const ComponentHandle handle, ComponentDirectoryFailure* const failure) noexcept
    {
        ClearFailure(failure);
        Impl::Slot* slot = m_impl != nullptr ? m_impl->Find(handle) : nullptr;
        if (slot == nullptr)
            return Fail(failure, m_impl == nullptr ? ComponentDirectoryFailureCode::NotInitialized : ComponentDirectoryFailureCode::InvalidHandle,
                        "invalid or stale component handle", handle);
        if (!concurrency::IsMainThread())
            return Fail(failure, ComponentDirectoryFailureCode::WrongThread, "components must be destroyed on the main thread", handle, slot->entity,
                        slot->stableId);
        if (m_impl->lifecycleMutation || slot->component->m_initializing)
            return Fail(failure, ComponentDirectoryFailureCode::InvalidState, "component destruction is not allowed from a lifecycle callback", handle,
                        slot->entity, slot->stableId);
        if (slot->component->m_attached && !DetachComponent(handle, failure))
            return false;
        if (slot->component->m_initialized && !UninitializeComponent(handle, failure))
            return false;

        const u32 index = handle.index;
        Component* const component = slot->component;
        const memory::MemoryBlock storage = slot->storage;
        const DestroyFunction destroy = slot->destroy;
        m_impl->UnlinkEntity(index);
        m_impl->UnlinkActive(index);
        component->m_directory = nullptr;
        component->m_handle = {};
        component->m_entity = ecs::InvalidEntityId;
        component->m_stableId = InvalidComponentStableId;
        component->m_type = reflection::InvalidSchemaTypeId;
        m_impl->Release(index);
        --m_impl->stats.components;
        ++m_impl->stats.destroyedComponents;
        destroy(component);
        memory::MemoryBlock ownedStorage = storage;
        memory::Free(ownedStorage);
        return true;
    }

    bool ComponentDirectory::DestroyEntityComponents(const ecs::EntityId entity, ComponentDirectoryFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, ComponentDirectoryFailureCode::NotInitialized, "ComponentDirectory is not initialized", {}, entity);
        if (!concurrency::IsMainThread())
            return Fail(failure, ComponentDirectoryFailureCode::WrongThread, "entity components must be destroyed on the main thread", {}, entity);
        while (const u32* const head = m_impl->entityHeads.FindPtr(entity))
        {
            const Impl::Slot& slot = m_impl->slots[*head];
            if (!Destroy({*head, slot.generation}, failure))
                return false;
        }
        return true;
    }

    Component* ComponentDirectory::Get(const ComponentHandle handle) noexcept
    {
        Impl::Slot* const slot = m_impl != nullptr ? m_impl->Find(handle) : nullptr;
        return slot != nullptr ? slot->component : nullptr;
    }

    const Component* ComponentDirectory::Get(const ComponentHandle handle) const noexcept
    {
        return const_cast<ComponentDirectory*>(this)->Get(handle);
    }

    Component* ComponentDirectory::Find(const ecs::EntityId entity, const u64 stableId) noexcept
    {
        if (m_impl == nullptr)
            return nullptr;
        const u32 index = m_impl->Find(entity, stableId);
        return index != InvalidIndex ? m_impl->slots[index].component : nullptr;
    }

    const Component* ComponentDirectory::Find(const ecs::EntityId entity, const u64 stableId) const noexcept
    {
        return const_cast<ComponentDirectory*>(this)->Find(entity, stableId);
    }

    bool ComponentDirectory::GetSnapshot(const ComponentHandle handle, ComponentSnapshot& snapshot) const noexcept
    {
        snapshot = {};
        const Impl::Slot* const slot = m_impl != nullptr ? m_impl->Find(handle) : nullptr;
        if (slot == nullptr || slot->component == nullptr)
            return false;
        snapshot.handle = handle;
        snapshot.component = slot->component;
        snapshot.entity = slot->entity;
        snapshot.stableId = slot->stableId;
        snapshot.type = slot->type;
        snapshot.initialized = slot->component->m_initialized;
        snapshot.attached = slot->component->m_attached;
        snapshot.postAttached = slot->component->m_postAttached;
        snapshot.componentEnabled = slot->component->m_componentEnabled;
        snapshot.entityEnabled = slot->component->m_entityEnabled;
        snapshot.enabled = slot->component->IsEnabled();
        return true;
    }

    void ComponentDirectory::Visit(const ecs::EntityId entity, const VisitComponent visitor, void* const userData) const noexcept
    {
        if (m_impl == nullptr || visitor == nullptr)
            return;
        const u32* const head = m_impl->entityHeads.FindPtr(entity);
        u32 index = head != nullptr ? *head : InvalidIndex;
        while (index != InvalidIndex)
        {
            const Impl::Slot& slot = m_impl->slots[index];
            const u32 next = slot.nextEntity;
            ComponentSnapshot snapshot;
            if (GetSnapshot({index, slot.generation}, snapshot) && !visitor(snapshot, userData))
                return;
            index = next;
        }
    }

    void ComponentDirectory::VisitAll(const VisitComponent visitor, void* const userData) const noexcept
    {
        if (m_impl == nullptr || visitor == nullptr)
            return;
        u32 index = m_impl->activeHead;
        while (index != InvalidIndex)
        {
            const Impl::Slot& slot = m_impl->slots[index];
            const u32 next = slot.nextActive;
            ComponentSnapshot snapshot;
            if (GetSnapshot({index, slot.generation}, snapshot) && !visitor(snapshot, userData))
                return;
            index = next;
        }
    }

    u32 ComponentDirectory::GetEntityComponentCount(const ecs::EntityId entity) const noexcept
    {
        const u32* const count = m_impl != nullptr ? m_impl->entityCounts.FindPtr(entity) : nullptr;
        return count != nullptr ? *count : 0;
    }

    ComponentDirectoryStats ComponentDirectory::GetStats() const noexcept
    {
        return m_impl != nullptr ? m_impl->stats : ComponentDirectoryStats{};
    }
} // namespace vanguard::entities
