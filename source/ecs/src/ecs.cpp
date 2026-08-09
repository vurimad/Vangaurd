#include <vanguard/ecs/ecs.hpp>

#include <vanguard/concurrency/concurrency.hpp>
#include <vanguard/diagnostics/diagnostics.hpp>
#include <vanguard/memory/memory.hpp>
#include <vanguard/memory/pool.hpp>

#include <flecs.h>

#include <cstring>
#include <new>

namespace
{
    using namespace vanguard;

    template<typename Type, typename... Args>
    [[nodiscard]] Type* AllocateEcsObject(Args&&... args) noexcept
    {
        memory::MemoryBlock block = memory::Allocate(memory::PoolId::Gameplay, sizeof(Type), alignof(Type));
        if (!block) return nullptr;
        return ::new (block.address) Type(static_cast<Args&&>(args)...);
    }

    template<typename Type>
    void DeleteEcsObject(Type* const object) noexcept
    {
        if (object == nullptr) return;
        object->~Type();
        memory::MemoryBlock block{object, sizeof(Type), memory::PoolId::Gameplay};
        memory::Free(block);
    }

    void* FlecsAllocate(const ecs_size_t size)
    {
        return size > 0 ? VANGUARD_ALLOCATE(memory::pools::Gameplay, static_cast<usize>(size)) : nullptr;
    }

    void FlecsFree(void* const address)
    {
        if (address != nullptr) VANGUARD_FREE(memory::pools::Gameplay, address);
    }

    void* FlecsReallocate(void* const address, const ecs_size_t size)
    {
        if (address == nullptr) return FlecsAllocate(size);
        if (size <= 0)
        {
            FlecsFree(address);
            return nullptr;
        }
        return VANGUARD_REALLOCATE(memory::pools::Gameplay, address, static_cast<usize>(size));
    }

    void* FlecsCallocate(const ecs_size_t size)
    {
        void* const address = FlecsAllocate(size);
        if (address != nullptr) std::memset(address, 0, static_cast<usize>(size));
        return address;
    }

    char* FlecsDuplicate(const char* const source)
    {
        if (source == nullptr) return nullptr;
        const usize size = std::strlen(source) + 1u;
        char* const destination = static_cast<char*>(FlecsAllocate(static_cast<ecs_size_t>(size)));
        if (destination != nullptr) std::memcpy(destination, source, size);
        return destination;
    }

    void FlecsLog(const i32 level, const char* const file, const i32 line, const char* const message)
    {
        diagnostics::Level output = diagnostics::Level::Trace;
        if (level <= -4) output = diagnostics::Level::Fatal;
        else if (level == -3) output = diagnostics::Level::Error;
        else if (level == -2) output = diagnostics::Level::Warning;
        else if (level == -1) output = diagnostics::Level::Info;
        else if (level == 0) output = diagnostics::Level::Debug;
        diagnostics::Logf(output, diagnostics::Category::Entity, "Flecs: %s (%s:%d)", message != nullptr ? message : "",
                          file != nullptr ? file : "unknown", line);
    }

    [[nodiscard]] bool ConfigureFlecs() noexcept
    {
        static bool configured = false;
        if (configured) return true;
        if (!memory::IsInitialized()) return false;
        ecs_os_set_api_defaults();
        ecs_os_api_t api = ecs_os_get_api();
        api.malloc_ = &FlecsAllocate;
        api.realloc_ = &FlecsReallocate;
        api.calloc_ = &FlecsCallocate;
        api.free_ = &FlecsFree;
        api.strdup_ = &FlecsDuplicate;
        api.log_ = &FlecsLog;
        ecs_os_set_api(&api);
        configured = true;
        return true;
    }

    struct StableIdentity
    {
        vanguard::ecs::EntityId value = vanguard::ecs::InvalidEntityId;
    };
} // namespace

namespace vanguard::ecs
{
    struct World::Impl
    {
        struct Action
        {
            ActionType type = ActionType::Create;
            EntityId identity = InvalidEntityId;
            CommandBatchId batch = InvalidCommandBatchId;
        };

        struct ComponentAction
        {
            ComponentActionType type = ComponentActionType::Add;
            EntityId identity = InvalidEntityId;
            ComponentId component = InvalidComponentId;
            CommandBatchId batch = InvalidCommandBatchId;
            memory::MemoryBlock value;
            usize valueSize = 0;
            usize valueAlignment = 0;
            ComponentDestroy destroy = nullptr;
        };

        struct BatchRecord
        {
            u32 queued = 0;
            u32 completed = 0;
            u32 rejected = 0;
            bool sealed = false;
        };

        explicit Impl(const WorldConfig& value) noexcept
            : config(value), identities(memory::pools::Gameplay::GetInstance()),
              pendingActions(memory::pools::Gameplay::GetInstance()),
              pendingComponentActions(memory::pools::Gameplay::GetInstance()),
              batches(memory::pools::Gameplay::GetInstance()),
              pendingIdentities(memory::pools::Gameplay::GetInstance())
        {
            identities.Reserve(value.initialEntityCapacity);
            pendingActions.Reserve(value.initialActionCapacity);
            pendingComponentActions.Reserve(value.initialActionCapacity);
        }

        [[nodiscard]] bool IsOpenBatch(const CommandBatch batch) const noexcept
        {
            if (!batch) return true;
            if (batch.world != world) return false;
            const BatchRecord* const record = batches.FindPtr(batch.id);
            return record != nullptr && !record->sealed;
        }

        [[nodiscard]] bool QueueComponent(const CommandBatch batch, const ComponentAction action) noexcept
        {
            VG_SCOPE_LOCK(queueLock);
            if (!IsOpenBatch(batch)) return false;
            const CommandBatchId* const identityOwner = pendingIdentities.FindPtr(action.identity);
            if (identityOwner != nullptr && *identityOwner != batch.id) return false;
            const u32 expected = pendingComponentActions.Size() + 1u;
            pendingComponentActions.PushBack(action);
            if (BatchRecord* const record = batches.FindPtr(action.batch)) ++record->queued;
            return pendingComponentActions.Size() == expected;
        }

        static void ReleaseComponentValue(ComponentAction& action) noexcept
        {
            if (!action.value) return;
            if (action.destroy != nullptr) action.destroy(action.value.address);
            memory::Free(action.value);
            action.destroy = nullptr;
        }

        [[nodiscard]] bool QueueCreate(const CommandBatch batch, const EntityId identity) noexcept
        {
            VG_SCOPE_LOCK(queueLock);
            if (!IsOpenBatch(batch) || pendingIdentities.FindPtr(identity) != nullptr) return false;
            if (!pendingIdentities.Insert(identity, batch.id).IsSuccessful()) return false;
            const u32 expected = pendingActions.Size() + 1u;
            pendingActions.PushBack({ActionType::Create, identity, batch.id});
            if (BatchRecord* const record = batches.FindPtr(batch.id)) ++record->queued;
            return pendingActions.Size() == expected;
        }

        [[nodiscard]] bool QueueDestroy(const CommandBatch batch, const EntityId identity) noexcept
        {
            VG_SCOPE_LOCK(queueLock);
            if (!IsOpenBatch(batch)) return false;
            const CommandBatchId* const identityOwner = pendingIdentities.FindPtr(identity);
            if (identityOwner != nullptr && *identityOwner != batch.id) return false;
            if (identityOwner == nullptr && !pendingIdentities.Insert(identity, batch.id).IsSuccessful()) return false;
            const u32 expected = pendingActions.Size() + 1u;
            pendingActions.PushBack({ActionType::Destroy, identity, batch.id});
            if (BatchRecord* const record = batches.FindPtr(batch.id)) ++record->queued;
            return pendingActions.Size() == expected;
        }

        void ReleaseBatchReservations(const CommandBatchId batch) noexcept
        {
            containers::DynamicArray<EntityId> identitiesToRelease(memory::pools::Gameplay::GetInstance());
            identitiesToRelease.Reserve(pendingIdentities.Size());
            for (auto iterator = pendingIdentities.Begin(), end = pendingIdentities.End(); iterator != end; ++iterator)
                if (iterator.Value() == batch) identitiesToRelease.PushBack(iterator.Key());
            for (const EntityId identity : identitiesToRelease)
                static_cast<void>(pendingIdentities.Remove(identity));
        }

        void CompleteCommand(const CommandBatchId batch, const bool rejected) noexcept
        {
            if (batch == InvalidCommandBatchId) return;
            VG_SCOPE_LOCK(queueLock);
            if (BatchRecord* const record = batches.FindPtr(batch))
            {
                ++record->completed;
                record->rejected += rejected;
            }
        }

        WorldConfig config;
        ecs_world_t* world = nullptr;
        ecs_entity_t stableIdentityType = 0;
        containers::HashMap<EntityId, ecs_entity_t> identities;
        containers::DynamicArray<Action> pendingActions;
        containers::DynamicArray<ComponentAction> pendingComponentActions;
        containers::HashMap<CommandBatchId, BatchRecord> batches;
        containers::HashMap<EntityId, CommandBatchId> pendingIdentities;
        concurrency::Mutex queueLock;
        CommandBatchId nextBatch = 1;
        u64 createdEntities = 0;
        u64 destroyedEntities = 0;
        u64 rejectedActions = 0;
        u64 addedComponents = 0;
        u64 setComponents = 0;
        u64 removedComponents = 0;
        u64 enabledComponents = 0;
        u64 disabledComponents = 0;
        u64 rejectedComponentActions = 0;
        bool progressing = false;
    };

    World::~World()
    {
        if (m_impl != nullptr)
        {
            for (Impl::ComponentAction& action : m_impl->pendingComponentActions)
                Impl::ReleaseComponentValue(action);
            if (m_impl->world != nullptr) static_cast<void>(ecs_fini(m_impl->world));
            DeleteEcsObject(m_impl);
            m_impl = nullptr;
        }
    }

    bool World::Initialize(const WorldConfig& config) noexcept
    {
        if (m_impl != nullptr) return false;
        if (config.initialEntityCapacity == 0 || config.initialActionCapacity == 0 || !ConfigureFlecs()) return false;
        Impl* const impl = AllocateEcsObject<Impl>(config);
        if (impl == nullptr) return false;
        impl->world = ecs_init();
        if (impl->world == nullptr)
        {
            DeleteEcsObject(impl);
            return false;
        }
        ecs_entity_desc_t identityEntityDescriptor{};
        identityEntityDescriptor.name = "vanguard.ecs.StableIdentity";
        ecs_component_desc_t identityDescriptor{};
        identityDescriptor.entity = ecs_entity_init(impl->world, &identityEntityDescriptor);
        identityDescriptor.type.size = sizeof(StableIdentity);
        identityDescriptor.type.alignment = alignof(StableIdentity);
        impl->stableIdentityType = ecs_component_init(impl->world, &identityDescriptor);
        if (impl->stableIdentityType == 0)
        {
            static_cast<void>(ecs_fini(impl->world));
            DeleteEcsObject(impl);
            return false;
        }
        m_impl = impl;
        return true;
    }

    bool World::Shutdown() noexcept
    {
        if (m_impl == nullptr) return true;
        {
            VG_SCOPE_LOCK(m_impl->queueLock);
            if (!m_impl->pendingActions.Empty() || !m_impl->pendingComponentActions.Empty() ||
                !m_impl->batches.Empty() || !m_impl->pendingIdentities.Empty()) return false;
        }
        if (!m_impl->identities.Empty() || m_impl->progressing) return false;
        if (ecs_fini(m_impl->world) != 0) return false;
        m_impl->world = nullptr;
        DeleteEcsObject(m_impl);
        m_impl = nullptr;
        return true;
    }

    bool World::IsInitialized() const noexcept { return m_impl != nullptr; }

    CommandBatch World::BeginCommandBatch() noexcept
    {
        if (m_impl == nullptr) return {};
        VG_SCOPE_LOCK(m_impl->queueLock);
        CommandBatchId id = InvalidCommandBatchId;
        do
        {
            id = m_impl->nextBatch++;
        }
        while (id == InvalidCommandBatchId || m_impl->batches.FindPtr(id) != nullptr);
        if (!m_impl->batches.Insert(id, {}).IsSuccessful()) return {};
        return {id, m_impl->world};
    }

    bool World::SealCommandBatch(const CommandBatch batch) noexcept
    {
        if (m_impl == nullptr || !batch || batch.world != m_impl->world) return false;
        VG_SCOPE_LOCK(m_impl->queueLock);
        Impl::BatchRecord* const record = m_impl->batches.FindPtr(batch.id);
        if (record == nullptr || record->sealed) return false;
        record->sealed = true;
        return true;
    }

    bool World::CancelCommandBatch(const CommandBatch batch) noexcept
    {
        if (m_impl == nullptr || !batch || batch.world != m_impl->world) return false;
        VG_SCOPE_LOCK(m_impl->queueLock);
        if (m_impl->batches.FindPtr(batch.id) == nullptr) return false;
        for (u32 index = m_impl->pendingActions.Size(); index > 0; --index)
        {
            const Impl::Action& action = m_impl->pendingActions[index - 1u];
            if (action.batch != batch.id) continue;
            if (action.type == ActionType::Create)
            {
                const CommandBatchId* const owner = m_impl->pendingIdentities.FindPtr(action.identity);
                if (owner != nullptr && *owner == batch.id)
                    static_cast<void>(m_impl->pendingIdentities.Remove(action.identity));
            }
            static_cast<void>(m_impl->pendingActions.RemoveAt(index - 1u));
        }
        m_impl->ReleaseBatchReservations(batch.id);
        for (u32 index = m_impl->pendingComponentActions.Size(); index > 0; --index)
        {
            Impl::ComponentAction& action = m_impl->pendingComponentActions[index - 1u];
            if (action.batch != batch.id) continue;
            Impl::ReleaseComponentValue(action);
            static_cast<void>(m_impl->pendingComponentActions.RemoveAt(index - 1u));
        }
        static_cast<void>(m_impl->batches.Remove(batch.id));
        return true;
    }

    CommandBatchStatus World::GetCommandBatchStatus(const CommandBatch batch,
                                                     CommandBatchReport* const output) const noexcept
    {
        if (output != nullptr) *output = {};
        if (m_impl == nullptr || !batch || batch.world != m_impl->world) return CommandBatchStatus::Unknown;
        VG_SCOPE_LOCK(m_impl->queueLock);
        const Impl::BatchRecord* const record = m_impl->batches.FindPtr(batch.id);
        if (record == nullptr) return CommandBatchStatus::Unknown;
        if (output != nullptr) *output = {record->queued, record->completed, record->rejected};
        if (!record->sealed) return CommandBatchStatus::Open;
        if (record->completed < record->queued) return CommandBatchStatus::Pending;
        return record->rejected == 0 ? CommandBatchStatus::Succeeded : CommandBatchStatus::Failed;
    }

    bool World::RetireCommandBatch(const CommandBatch batch) noexcept
    {
        if (m_impl == nullptr || !batch || batch.world != m_impl->world) return false;
        VG_SCOPE_LOCK(m_impl->queueLock);
        const Impl::BatchRecord* const record = m_impl->batches.FindPtr(batch.id);
        if (record == nullptr || !record->sealed || record->completed != record->queued) return false;
        m_impl->ReleaseBatchReservations(batch.id);
        return m_impl->batches.Remove(batch.id).IsSuccessful();
    }

    bool World::QueueCreate(const EntityId identity) noexcept
    {
        return m_impl != nullptr && identity != InvalidEntityId && m_impl->QueueCreate({}, identity);
    }

    bool World::QueueCreate(const CommandBatch batch, const EntityId identity) noexcept
    {
        return m_impl != nullptr && identity != InvalidEntityId && m_impl->QueueCreate(batch, identity);
    }

    bool World::QueueDestroy(const EntityId identity) noexcept
    {
        return m_impl != nullptr && identity != InvalidEntityId && m_impl->QueueDestroy({}, identity);
    }

    bool World::QueueDestroy(const CommandBatch batch, const EntityId identity) noexcept
    {
        return m_impl != nullptr && identity != InvalidEntityId && m_impl->QueueDestroy(batch, identity);
    }

    bool World::QueueComponentAction(const CommandBatch batch, const EntityId identity, const ComponentId component,
                                     const ecs_world_t* const tokenWorld, const ComponentActionType action,
                                     const void* const value, const usize valueSize, const usize valueAlignment,
                                     const ComponentCopy copy, const ComponentDestroy destroy) noexcept
    {
        if (m_impl == nullptr || identity == InvalidEntityId || component == InvalidComponentId ||
            tokenWorld != m_impl->world) return false;
        Impl::ComponentAction queued{action, identity, component, batch.id};
        if (action == ComponentActionType::Set)
        {
            if (value == nullptr || valueSize == 0 || valueAlignment == 0 || copy == nullptr || destroy == nullptr)
                return false;
            queued.value = memory::Allocate(memory::PoolId::Gameplay, valueSize, valueAlignment);
            if (!queued.value) return false;
            queued.valueSize = valueSize;
            queued.valueAlignment = valueAlignment;
            queued.destroy = destroy;
            copy(queued.value.address, value);
        }
        if (m_impl->QueueComponent(batch, queued)) return true;
        Impl::ReleaseComponentValue(queued);
        return false;
    }

    bool World::FlushActions(ActionReport* const output) noexcept
    {
        if (output != nullptr) *output = {};
        if (m_impl == nullptr || m_impl->progressing) return false;
        containers::DynamicArray<Impl::Action> actions(memory::pools::Gameplay::GetInstance());
        {
            VG_SCOPE_LOCK(m_impl->queueLock);
            actions.Swap(m_impl->pendingActions);
        }
        ActionReport report;
        report.queued = actions.Size();
        for (const Impl::Action action : actions)
        {
            bool rejected = false;
            ecs_entity_t existing = 0;
            const bool found = m_impl->identities.Find(action.identity, existing);
            if (action.type == ActionType::Create)
            {
                if (found && ecs_is_alive(m_impl->world, existing))
                {
                    ++report.rejected;
                    rejected = true;
                }
                else
                {
                    const ecs_entity_t entity = ecs_new(m_impl->world);
                    const StableIdentity identity{action.identity};
                    if (entity != 0)
                        ecs_set_id(m_impl->world, entity, m_impl->stableIdentityType, sizeof(identity), &identity);
                    if (entity == 0 || !m_impl->identities.Insert(action.identity, entity).IsSuccessful())
                    {
                        if (entity != 0) ecs_delete(m_impl->world, entity);
                        ++report.rejected;
                        rejected = true;
                    }
                    else
                    {
                        ++report.created;
                        ++m_impl->createdEntities;
                    }
                }
                {
                    VG_SCOPE_LOCK(m_impl->queueLock);
                    const CommandBatchId* const owner = m_impl->pendingIdentities.FindPtr(action.identity);
                    if (owner != nullptr && *owner == action.batch && action.batch == InvalidCommandBatchId)
                        static_cast<void>(m_impl->pendingIdentities.Remove(action.identity));
                }
            }
            else
            {
                if (!found || !ecs_is_alive(m_impl->world, existing))
                {
                    ++report.rejected;
                    rejected = true;
                }
                else
                {
                    static_cast<void>(m_impl->identities.Remove(action.identity));
                    ecs_delete(m_impl->world, existing);
                    ++report.destroyed;
                    ++m_impl->destroyedEntities;
                }
                {
                    VG_SCOPE_LOCK(m_impl->queueLock);
                    const CommandBatchId* const owner = m_impl->pendingIdentities.FindPtr(action.identity);
                    if (owner != nullptr && *owner == action.batch && action.batch == InvalidCommandBatchId)
                        static_cast<void>(m_impl->pendingIdentities.Remove(action.identity));
                }
            }
            m_impl->CompleteCommand(action.batch, rejected);
        }
        m_impl->rejectedActions += report.rejected;
        if (output != nullptr) *output = report;
        return true;
    }

    bool World::FlushComponentActions(ComponentActionReport* const output) noexcept
    {
        if (output != nullptr) *output = {};
        if (m_impl == nullptr || m_impl->progressing) return false;
        containers::DynamicArray<Impl::ComponentAction> actions(memory::pools::Gameplay::GetInstance());
        {
            VG_SCOPE_LOCK(m_impl->queueLock);
            actions.Swap(m_impl->pendingComponentActions);
        }
        ComponentActionReport report;
        report.queued = actions.Size();
        for (Impl::ComponentAction& action : actions)
        {
            bool rejected = false;
            ecs_entity_t entity = 0;
            const bool entityFound = m_impl->identities.Find(action.identity, entity) &&
                                     ecs_is_alive(m_impl->world, entity);
            const bool componentAlive = ecs_is_alive(m_impl->world, action.component);
            if (!entityFound || !componentAlive)
            {
                ++report.rejected;
                rejected = true;
                Impl::ReleaseComponentValue(action);
                m_impl->CompleteCommand(action.batch, rejected);
                continue;
            }

            const bool componentPresent = ecs_has_id(m_impl->world, entity, action.component);
            if (action.type == ComponentActionType::Add)
            {
                if (componentPresent)
                {
                    ++report.rejected;
                    rejected = true;
                }
                else
                {
                    ecs_add_id(m_impl->world, entity, action.component);
                    if (ecs_has_id(m_impl->world, entity, action.component)) ++report.added;
                    else { ++report.rejected; rejected = true; }
                }
            }
            else if (action.type == ComponentActionType::Set)
            {
                const ecs_type_info_t* const typeInfo = ecs_get_type_info(m_impl->world, action.component);
                if (!componentPresent || typeInfo == nullptr || typeInfo->size != action.valueSize ||
                    typeInfo->alignment != action.valueAlignment)
                {
                    ++report.rejected;
                    rejected = true;
                }
                else
                {
                    ecs_set_id(m_impl->world, entity, action.component, action.valueSize, action.value.address);
                    ++report.set;
                }
            }
            else if (action.type == ComponentActionType::Remove)
            {
                if (!componentPresent)
                {
                    ++report.rejected;
                    rejected = true;
                }
                else
                {
                    ecs_remove_id(m_impl->world, entity, action.component);
                    if (!ecs_has_id(m_impl->world, entity, action.component)) ++report.removed;
                    else { ++report.rejected; rejected = true; }
                }
            }
            else
            {
                const bool enable = action.type == ComponentActionType::Enable;
                ecs_enable_id(m_impl->world, entity, action.component, enable);
                if (ecs_is_enabled_id(m_impl->world, entity, action.component) == enable)
                {
                    if (enable) ++report.enabled;
                    else ++report.disabled;
                }
                else
                {
                    ++report.rejected;
                    rejected = true;
                }
            }
            Impl::ReleaseComponentValue(action);
            m_impl->CompleteCommand(action.batch, rejected);
        }
        m_impl->addedComponents += report.added;
        m_impl->setComponents += report.set;
        m_impl->removedComponents += report.removed;
        m_impl->enabledComponents += report.enabled;
        m_impl->disabledComponents += report.disabled;
        m_impl->rejectedComponentActions += report.rejected;
        if (output != nullptr) *output = report;
        return true;
    }

    bool World::Progress(const f32 deltaSeconds) noexcept
    {
        if (m_impl == nullptr || m_impl->progressing || deltaSeconds < 0.0f) return false;
        m_impl->progressing = true;
        const bool running = ecs_progress(m_impl->world, deltaSeconds);
        m_impl->progressing = false;
        return running;
    }

    Entity World::Resolve(const EntityId identity) const noexcept
    {
        if (m_impl == nullptr || identity == InvalidEntityId) return {};
        ecs_entity_t entity = 0;
        return m_impl->identities.Find(identity, entity) && ecs_is_alive(m_impl->world, entity) ? Entity{entity} : Entity{};
    }

    EntityId World::Identity(const Entity entity) const noexcept
    {
        if (m_impl == nullptr || !entity || !ecs_is_alive(m_impl->world, entity.value)) return InvalidEntityId;
        const auto* const identity = static_cast<const StableIdentity*>(ecs_get_id(m_impl->world, entity.value,
                                                                                   m_impl->stableIdentityType));
        return identity != nullptr ? identity->value : InvalidEntityId;
    }

    bool World::IsAlive(const Entity entity) const noexcept
    {
        return m_impl != nullptr && entity && ecs_is_alive(m_impl->world, entity.value);
    }

    WorldStats World::GetStats() const noexcept
    {
        WorldStats stats;
        if (m_impl == nullptr) return stats;
        stats.entities = m_impl->identities.Size();
        {
            VG_SCOPE_LOCK(m_impl->queueLock);
            stats.pendingActions = m_impl->pendingActions.Size();
            stats.pendingComponentActions = m_impl->pendingComponentActions.Size();
            stats.commandBatches = m_impl->batches.Size();
        }
        stats.createdEntities = m_impl->createdEntities;
        stats.destroyedEntities = m_impl->destroyedEntities;
        stats.rejectedActions = m_impl->rejectedActions;
        stats.addedComponents = m_impl->addedComponents;
        stats.setComponents = m_impl->setComponents;
        stats.removedComponents = m_impl->removedComponents;
        stats.enabledComponents = m_impl->enabledComponents;
        stats.disabledComponents = m_impl->disabledComponents;
        stats.rejectedComponentActions = m_impl->rejectedComponentActions;
        stats.progressing = m_impl->progressing;
        return stats;
    }

    ecs_world_t* World::Native() noexcept { return m_impl != nullptr ? m_impl->world : nullptr; }
    const ecs_world_t* World::Native() const noexcept { return m_impl != nullptr ? m_impl->world : nullptr; }
} // namespace vanguard::ecs
