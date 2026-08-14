#include <vanguard/entities/world_render_bridge.hpp>

#include <vanguard/memory/memory.hpp>
#include <vanguard/memory/pool.hpp>

#include <flecs.h>

#include <new>

namespace
{
    using namespace vanguard;
    namespace entity = vanguard::entities;

    template<typename Type, typename... Args>
    [[nodiscard]] Type* AllocateBridgeObject(Args&&... args) noexcept
    {
        memory::MemoryBlock block = memory::Allocate(memory::PoolId::Rendering, sizeof(Type), alignof(Type));
        return block ? ::new (block.address) Type(static_cast<Args&&>(args)...) : nullptr;
    }

    template<typename Type>
    void DeleteBridgeObject(Type* const object) noexcept
    {
        if (object == nullptr) return;
        object->~Type();
        memory::MemoryBlock block{object, sizeof(Type), memory::PoolId::Rendering};
        memory::Free(block);
    }

    void ClearFailure(entity::WorldRenderBridgeFailure* const failure) noexcept
    {
        if (failure != nullptr) *failure = {};
    }

    [[nodiscard]] bool Fail(entity::WorldRenderBridgeFailure* const failure,
                            const entity::WorldRenderBridgeFailureCode code, const char* const message,
                            const ecs::EntityId entityId = ecs::InvalidEntityId,
                            const ecs::ComponentId component = ecs::InvalidComponentId,
                            const rendering::RenderSceneFailure* const renderFailure = nullptr) noexcept
    {
        if (failure != nullptr)
        {
            failure->code = code;
            failure->entity = entityId;
            failure->component = component;
            failure->message = message;
            if (renderFailure != nullptr) failure->renderScene = *renderFailure;
        }
        return false;
    }

    [[nodiscard]] bool IsRemoval(const ecs::CommittedChangeKind kind) noexcept
    {
        return kind == ecs::CommittedChangeKind::ComponentRemoved ||
               kind == ecs::CommittedChangeKind::ComponentDisabled;
    }

    [[nodiscard]] bool HasField(const entity::WorldRenderStateFields fields,
                                const entity::WorldRenderStateFields field) noexcept
    {
        return (fields & field) != entity::WorldRenderStateFields::None;
    }
} // namespace

namespace vanguard::entities
{
    struct WorldRenderBridge::Impl
    {
        struct ContributorRecord
        {
            WorldRenderContributorKind kind = WorldRenderContributorKind::Mesh;
            rendering::RenderProxyHandle proxy;
        };

        struct EntityRecord
        {
            EntityRecord() noexcept : contributors(memory::pools::Rendering::GetInstance()) {}
            containers::HashMap<ecs::ComponentId, ContributorRecord> contributors;
        };

        struct PendingDetachment
        {
            ecs::EntityId entity = ecs::InvalidEntityId;
            ecs::ComponentId contributor = ecs::InvalidComponentId;
            rendering::RenderProxyHandle proxy;
            u64 bridgeGeneration = 0;
        };

        struct CoalescedEntity
        {
            CoalescedEntity() noexcept : changes(memory::pools::Rendering::GetInstance()) {}
            ecs::EntityId entity = ecs::InvalidEntityId;
            u64 destroyedSequence = 0;
            containers::DynamicArray<ecs::CommittedChange> changes;
        };

        enum class PreparedKind : u8
        {
            DestroyEntity,
            DestroyContributor,
            UpsertContributor,
            UpdateState
        };

        struct PreparedOperation
        {
            PreparedKind kind = PreparedKind::DestroyEntity;
            ecs::EntityId entity = ecs::InvalidEntityId;
            ecs::ComponentId component = ecs::InvalidComponentId;
            u32 descriptorIndex = 0;
            WorldRenderContributorBuild build;
            WorldRenderState state;
        };

        explicit Impl(const WorldRenderBridgeConfig& value) noexcept
            : config(value), contributors(memory::pools::Rendering::GetInstance()),
              states(memory::pools::Rendering::GetInstance()), entities(memory::pools::Rendering::GetInstance()),
              pendingDetachments(memory::pools::Rendering::GetInstance())
        {
            contributors.Reserve(value.maximumContributorTypes);
            states.Reserve(value.maximumStateTypes);
            entities.Reserve(value.maximumTrackedEntities < 4096u ? value.maximumTrackedEntities : 4096u);
            pendingDetachments.Reserve(4096);
        }

        [[nodiscard]] EntityRecord* FindEntity(const ecs::EntityId entity) const noexcept
        {
            EntityRecord* record = nullptr;
            return entities.Find(entity, record) ? record : nullptr;
        }

        [[nodiscard]] const WorldRenderContributorDescriptor* FindContributor(
            const ecs::ComponentId component, u32* const index = nullptr) const noexcept
        {
            for (u32 descriptorIndex = 0; descriptorIndex < contributors.Size(); ++descriptorIndex)
            {
                if (contributors[descriptorIndex].component != component) continue;
                if (index != nullptr) *index = descriptorIndex;
                return &contributors[descriptorIndex];
            }
            return nullptr;
        }

        [[nodiscard]] const WorldRenderStateDescriptor* FindState(
            const ecs::ComponentId component, u32* const index = nullptr) const noexcept
        {
            for (u32 descriptorIndex = 0; descriptorIndex < states.Size(); ++descriptorIndex)
            {
                if (states[descriptorIndex].component != component) continue;
                if (index != nullptr) *index = descriptorIndex;
                return &states[descriptorIndex];
            }
            return nullptr;
        }

        void QueueDetachment(const ecs::EntityId entity, const ecs::ComponentId component,
                             const rendering::RenderProxyHandle proxy) noexcept
        {
            pendingDetachments.PushBack({entity, component, proxy, bridgeGeneration});
        }

        WorldRenderBridgeConfig config;
        ecs::World* world = nullptr;
        rendering::RenderSceneManager* scenes = nullptr;
        rendering::RenderSceneHandle scene;
        u64 bridgeGeneration = 0;
        ecs::CommittedChangeCursor cursor;
        containers::DynamicArray<WorldRenderContributorDescriptor> contributors;
        containers::DynamicArray<WorldRenderStateDescriptor> states;
        containers::HashMap<ecs::EntityId, EntityRecord*> entities;
        containers::DynamicArray<PendingDetachment> pendingDetachments;
        WorldRenderBridgeStats stats;
    };

    WorldRenderBridge::~WorldRenderBridge()
    {
        if (m_impl == nullptr) return;
        for (auto iterator = m_impl->entities.Begin(), end = m_impl->entities.End(); iterator != end; ++iterator)
            DeleteBridgeObject(iterator.Value());
        DeleteBridgeObject(m_impl);
        m_impl = nullptr;
    }

    bool WorldRenderBridge::Initialize(ecs::World& world, rendering::RenderSceneManager& scenes,
                                       const rendering::RenderSceneHandle scene, const u64 bridgeGeneration,
                                       const WorldRenderBridgeConfig& config,
                                       WorldRenderBridgeFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl != nullptr)
            return Fail(failure, WorldRenderBridgeFailureCode::AlreadyInitialized,
                        "WorldRenderBridge is already initialized");
        if (!world.IsInitialized() || !scenes.IsAlive(scene) || bridgeGeneration == 0 ||
            config.maximumContributorTypes == 0 || config.maximumStateTypes == 0 ||
            config.maximumTrackedEntities == 0 || config.maximumChangesPerFlush == 0)
            return Fail(failure, WorldRenderBridgeFailureCode::InvalidArgument,
                        "invalid WorldRenderBridge initialization descriptor");
        Impl* const impl = AllocateBridgeObject<Impl>(config);
        if (impl == nullptr)
            return Fail(failure, WorldRenderBridgeFailureCode::CapacityExceeded,
                        "WorldRenderBridge allocation failed");
        impl->world = &world;
        impl->scenes = &scenes;
        impl->scene = scene;
        impl->bridgeGeneration = bridgeGeneration;
        impl->cursor.nextSequence = world.NextCommittedChangeSequence();
        impl->stats.initialized = true;
        m_impl = impl;
        return true;
    }

    bool WorldRenderBridge::Shutdown(WorldRenderBridgeFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr) return true;
        if (!m_impl->entities.Empty() || !m_impl->pendingDetachments.Empty())
            return Fail(failure, WorldRenderBridgeFailureCode::LiveContributorsRemain,
                        "WorldRenderBridge shutdown requires every proxy detachment to retire");
        DeleteBridgeObject(m_impl);
        m_impl = nullptr;
        return true;
    }

    bool WorldRenderBridge::IsInitialized() const noexcept { return m_impl != nullptr; }

    bool WorldRenderBridge::RegisterContributor(const WorldRenderContributorDescriptor& descriptor,
                                                WorldRenderBridgeFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, WorldRenderBridgeFailureCode::NotInitialized,
                        "WorldRenderBridge is not initialized");
        if (descriptor.component == ecs::InvalidComponentId || descriptor.componentWorld != m_impl->world->Native() ||
            !ecs_is_alive(m_impl->world->Native(), descriptor.component) || descriptor.build == nullptr)
            return Fail(failure, WorldRenderBridgeFailureCode::InvalidRegistration,
                        "invalid world render contributor registration", ecs::InvalidEntityId, descriptor.component);
        if (m_impl->FindContributor(descriptor.component) != nullptr)
            return Fail(failure, WorldRenderBridgeFailureCode::DuplicateRegistration,
                        "duplicate world render contributor registration", ecs::InvalidEntityId, descriptor.component);
        if (m_impl->contributors.Size() == m_impl->config.maximumContributorTypes)
            return Fail(failure, WorldRenderBridgeFailureCode::CapacityExceeded,
                        "world render contributor registration capacity exceeded");
        m_impl->contributors.PushBack(descriptor);
        m_impl->stats.contributorTypes = m_impl->contributors.Size();
        return true;
    }

    bool WorldRenderBridge::RegisterState(const WorldRenderStateDescriptor& descriptor,
                                          WorldRenderBridgeFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, WorldRenderBridgeFailureCode::NotInitialized,
                        "WorldRenderBridge is not initialized");
        if (descriptor.component == ecs::InvalidComponentId || descriptor.componentWorld != m_impl->world->Native() ||
            !ecs_is_alive(m_impl->world->Native(), descriptor.component) ||
            descriptor.fields == WorldRenderStateFields::None || descriptor.read == nullptr)
            return Fail(failure, WorldRenderBridgeFailureCode::InvalidRegistration,
                        "invalid world render state registration", ecs::InvalidEntityId, descriptor.component);
        if (m_impl->FindState(descriptor.component) != nullptr)
            return Fail(failure, WorldRenderBridgeFailureCode::DuplicateRegistration,
                        "duplicate world render state registration", ecs::InvalidEntityId, descriptor.component);
        if (m_impl->states.Size() == m_impl->config.maximumStateTypes)
            return Fail(failure, WorldRenderBridgeFailureCode::CapacityExceeded,
                        "world render state registration capacity exceeded");
        m_impl->states.PushBack(descriptor);
        m_impl->stats.stateTypes = m_impl->states.Size();
        return true;
    }

    bool WorldRenderBridge::ResetSession(const u64 bridgeGeneration,
                                         WorldRenderBridgeFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, WorldRenderBridgeFailureCode::NotInitialized,
                        "WorldRenderBridge is not initialized");
        if (bridgeGeneration == 0 || bridgeGeneration == m_impl->bridgeGeneration)
            return Fail(failure, WorldRenderBridgeFailureCode::InvalidArgument,
                        "replacement world render bridge generation must be nonzero and different");
        rendering::RenderSceneFailure renderFailure;
        for (auto iterator = m_impl->entities.Begin(), end = m_impl->entities.End(); iterator != end; ++iterator)
        {
            Impl::EntityRecord* const record = iterator.Value();
            for (auto contributor = record->contributors.Begin(), contributorEnd = record->contributors.End();
                 contributor != contributorEnd; ++contributor)
            {
                if (!m_impl->scenes->DestroyProxy(contributor.Value().proxy, &renderFailure))
                {
                    m_impl->stats.rebuildRequired = true;
                    return Fail(failure, WorldRenderBridgeFailureCode::RenderSceneFailure,
                                "failed to detach a proxy during world session reset", iterator.Key(),
                                contributor.Key(), &renderFailure);
                }
                m_impl->QueueDetachment(iterator.Key(), contributor.Key(), contributor.Value().proxy);
                ++m_impl->stats.destroyedProxies;
            }
            DeleteBridgeObject(record);
        }
        m_impl->entities.Clear();
        m_impl->bridgeGeneration = bridgeGeneration;
        m_impl->cursor.nextSequence = m_impl->world->NextCommittedChangeSequence();
        m_impl->stats.trackedEntities = 0;
        m_impl->stats.liveContributors = 0;
        m_impl->stats.pendingDetachments = m_impl->pendingDetachments.Size();
        return true;
    }

    bool WorldRenderBridge::Flush(WorldRenderBridgeFlushResult& result,
                                  WorldRenderBridgeFailure* const failure) noexcept
    {
        ClearFailure(failure);
        result = {};
        if (m_impl == nullptr)
            return Fail(failure, WorldRenderBridgeFailureCode::NotInitialized,
                        "WorldRenderBridge is not initialized");
        result.bridgeGeneration = m_impl->bridgeGeneration;
        if (m_impl->stats.rebuildRequired)
            return Fail(failure, WorldRenderBridgeFailureCode::RebuildRequired,
                        "WorldRenderBridge requires a full scene rebuild before publication");

        containers::DynamicArray<ecs::CommittedChange> captured(memory::pools::Rendering::GetInstance());
        captured.Reserve(m_impl->config.maximumChangesPerFlush);
        ecs::CommittedChangeReadResult read;
        if (!m_impl->world->ReadCommittedChanges(m_impl->cursor, captured,
                                                  m_impl->config.maximumChangesPerFlush, &read))
            return Fail(failure, WorldRenderBridgeFailureCode::InvalidArgument,
                        "failed to read the ECS committed-change journal");
        result.firstChangeSequence = read.firstSequence;
        result.nextChangeSequence = read.nextSequence;
        result.capturedChanges = read.records;
        if (read.lostRecords != 0)
        {
            m_impl->stats.rebuildRequired = true;
            m_impl->stats.staleSessionChanges += read.lostRecords;
            return Fail(failure, WorldRenderBridgeFailureCode::LostCommittedChanges,
                        "world render bridge fell behind the ECS committed-change retention window");
        }
        if (m_impl->cursor.nextSequence < m_impl->world->NextCommittedChangeSequence())
        {
            m_impl->stats.rebuildRequired = true;
            return Fail(failure, WorldRenderBridgeFailureCode::CapacityExceeded,
                        "one bridge flush exceeded the configured committed-change budget");
        }
        if (captured.Empty())
        {
            result.sceneCommitAllowed = true;
            return true;
        }

        containers::DynamicArray<Impl::CoalescedEntity*> coalesced(memory::pools::Rendering::GetInstance());
        containers::HashMap<ecs::EntityId, Impl::CoalescedEntity*> byEntity(memory::pools::Rendering::GetInstance());
        coalesced.Reserve(captured.Size());
        byEntity.Reserve(captured.Size());
        const auto releaseCoalesced = [&]() noexcept
        {
            for (Impl::CoalescedEntity* const entry : coalesced) DeleteBridgeObject(entry);
        };

        for (const ecs::CommittedChange& change : captured)
        {
            Impl::CoalescedEntity* entry = nullptr;
            if (!byEntity.Find(change.entity, entry))
            {
                entry = AllocateBridgeObject<Impl::CoalescedEntity>();
                if (entry == nullptr || !byEntity.Insert(change.entity, entry).IsSuccessful())
                {
                    DeleteBridgeObject(entry);
                    releaseCoalesced();
                    m_impl->stats.rebuildRequired = true;
                    return Fail(failure, WorldRenderBridgeFailureCode::CapacityExceeded,
                                "failed to allocate bridge change coalescing state", change.entity);
                }
                entry->entity = change.entity;
                coalesced.PushBack(entry);
            }
            if (change.kind == ecs::CommittedChangeKind::EntityDestroyed)
            {
                entry->destroyedSequence = change.sequence;
                entry->changes.Clear();
                continue;
            }
            if (change.kind == ecs::CommittedChangeKind::EntityCreated ||
                change.sequence <= entry->destroyedSequence) continue;
            bool replaced = false;
            for (ecs::CommittedChange& existing : entry->changes)
            {
                if (existing.component != change.component) continue;
                existing = change;
                replaced = true;
                break;
            }
            if (!replaced) entry->changes.PushBack(change);
        }

        containers::DynamicArray<Impl::PreparedOperation> prepared(memory::pools::Rendering::GetInstance());
        prepared.Reserve(captured.Size() * 2u);
        for (const Impl::CoalescedEntity* const entry : coalesced)
        {
            if (entry->destroyedSequence != 0)
                prepared.PushBack({Impl::PreparedKind::DestroyEntity, entry->entity});
            for (const ecs::CommittedChange& change : entry->changes)
            {
                u32 descriptorIndex = 0;
                if (const WorldRenderContributorDescriptor* const descriptor =
                        m_impl->FindContributor(change.component, &descriptorIndex))
                {
                    Impl::PreparedOperation operation;
                    operation.kind = IsRemoval(change.kind) ? Impl::PreparedKind::DestroyContributor
                                                            : Impl::PreparedKind::UpsertContributor;
                    operation.entity = change.entity;
                    operation.component = change.component;
                    operation.descriptorIndex = descriptorIndex;
                    if (operation.kind == Impl::PreparedKind::UpsertContributor &&
                        !descriptor->build(*m_impl->world, change.entity, operation.build, descriptor->userData))
                    {
                        releaseCoalesced();
                        return Fail(failure, WorldRenderBridgeFailureCode::TranslationFailure,
                                    "world render contributor translation failed", change.entity, change.component);
                    }
                    prepared.PushBack(operation);
                }
                if (const WorldRenderStateDescriptor* const descriptor =
                        m_impl->FindState(change.component, &descriptorIndex))
                {
                    Impl::PreparedOperation operation;
                    operation.kind = Impl::PreparedKind::UpdateState;
                    operation.entity = change.entity;
                    operation.component = change.component;
                    operation.descriptorIndex = descriptorIndex;
                    if (!descriptor->read(*m_impl->world, change.entity, change.kind,
                                          operation.state, descriptor->userData) ||
                        (operation.state.fields & descriptor->fields) != operation.state.fields)
                    {
                        releaseCoalesced();
                        return Fail(failure, WorldRenderBridgeFailureCode::TranslationFailure,
                                    "world render state translation failed", change.entity, change.component);
                    }
                    prepared.PushBack(operation);
                }
            }
        }
        releaseCoalesced();

        rendering::RenderSceneSnapshot sceneSnapshot;
        if (!m_impl->scenes->Snapshot(m_impl->scene, sceneSnapshot))
            return Fail(failure, WorldRenderBridgeFailureCode::RenderSceneFailure,
                        "world render bridge scene is no longer alive");
        u32 requiredMutations = 0;
        containers::HashMap<ecs::EntityId, i64> finalContributorCounts(memory::pools::Rendering::GetInstance());
        containers::HashMap<ecs::EntityId, u8> destroyedEntities(memory::pools::Rendering::GetInstance());
        finalContributorCounts.Reserve(prepared.Size());
        destroyedEntities.Reserve(prepared.Size());
        for (const Impl::PreparedOperation& operation : prepared)
        {
            const Impl::EntityRecord* const entityRecord = m_impl->FindEntity(operation.entity);
            if (finalContributorCounts.FindPtr(operation.entity) == nullptr)
                static_cast<void>(finalContributorCounts.Insert(
                    operation.entity, entityRecord != nullptr ? static_cast<i64>(entityRecord->contributors.Size()) : 0));
            if (operation.kind == Impl::PreparedKind::DestroyEntity)
            {
                requiredMutations += entityRecord != nullptr ? entityRecord->contributors.Size() : 0;
                *finalContributorCounts.FindPtr(operation.entity) = 0;
                static_cast<void>(destroyedEntities.Insert(operation.entity, 1));
            }
        }
        for (const Impl::PreparedOperation& operation : prepared)
        {
            const Impl::EntityRecord* const entityRecord = m_impl->FindEntity(operation.entity);
            const bool entityDestroyed = destroyedEntities.FindPtr(operation.entity) != nullptr;
            const bool contributorExisted = entityRecord != nullptr &&
                                            entityRecord->contributors.FindPtr(operation.component) != nullptr;
            i64* const finalCount = finalContributorCounts.FindPtr(operation.entity);
            if (operation.kind == Impl::PreparedKind::DestroyContributor && !entityDestroyed && contributorExisted)
            {
                ++requiredMutations;
                --*finalCount;
            }
            else if (operation.kind == Impl::PreparedKind::UpsertContributor)
            {
                const bool replacesLiveContributor = !entityDestroyed && contributorExisted;
                requiredMutations += replacesLiveContributor ? 2u : 1u;
                if (!replacesLiveContributor) ++*finalCount;
            }
        }
        for (const Impl::PreparedOperation& operation : prepared)
        {
            if (operation.kind != Impl::PreparedKind::UpdateState) continue;
            const i64* const finalCount = finalContributorCounts.FindPtr(operation.entity);
            const u32 fieldMutations = static_cast<u32>(
                HasField(operation.state.fields, WorldRenderStateFields::TransformAndBounds)) +
                static_cast<u32>(HasField(operation.state.fields, WorldRenderStateFields::Visibility)) +
                static_cast<u32>(HasField(operation.state.fields, WorldRenderStateFields::LayerMask)) +
                static_cast<u32>(HasField(operation.state.fields, WorldRenderStateFields::UserDataEpoch));
            if (finalCount != nullptr && *finalCount > 0)
                requiredMutations += static_cast<u32>(*finalCount) * fieldMutations;
        }
        i64 proxyDelta = 0;
        for (auto iterator = finalContributorCounts.Begin(), end = finalContributorCounts.End(); iterator != end; ++iterator)
        {
            const Impl::EntityRecord* const entityRecord = m_impl->FindEntity(iterator.Key());
            const i64 initialCount = entityRecord != nullptr ? static_cast<i64>(entityRecord->contributors.Size()) : 0;
            proxyDelta += iterator.Value() - initialCount;
        }
        if (proxyDelta > 0 && sceneSnapshot.activeProxies + static_cast<u64>(proxyDelta) > sceneSnapshot.maximumProxies)
            return Fail(failure, WorldRenderBridgeFailureCode::CapacityExceeded,
                        "activation transaction exceeds the render scene proxy capacity");
        if (sceneSnapshot.pendingProxyMutations + static_cast<u64>(requiredMutations) >
            sceneSnapshot.maximumPendingProxyMutations)
            return Fail(failure, WorldRenderBridgeFailureCode::CapacityExceeded,
                        "activation transaction exceeds the render scene mutation budget");

        rendering::RenderSceneFailure renderFailure;
        for (u32 applicationPass = 0; applicationPass < 3; ++applicationPass)
        {
          for (const Impl::PreparedOperation& operation : prepared)
          {
            const bool selected = applicationPass == 0
                                      ? operation.kind == Impl::PreparedKind::DestroyEntity ||
                                            operation.kind == Impl::PreparedKind::DestroyContributor
                                      : applicationPass == 1
                                          ? operation.kind == Impl::PreparedKind::UpsertContributor
                                          : operation.kind == Impl::PreparedKind::UpdateState;
            if (!selected) continue;
            Impl::EntityRecord* entityRecord = m_impl->FindEntity(operation.entity);
            if (operation.kind == Impl::PreparedKind::DestroyEntity)
            {
                if (entityRecord == nullptr) continue;
                for (auto contributor = entityRecord->contributors.Begin(), end = entityRecord->contributors.End();
                     contributor != end; ++contributor)
                {
                    if (!m_impl->scenes->DestroyProxy(contributor.Value().proxy, &renderFailure)) goto render_failure;
                    m_impl->QueueDetachment(operation.entity, contributor.Key(), contributor.Value().proxy);
                    ++result.destroyedProxies;
                }
                m_impl->stats.liveContributors -= entityRecord->contributors.Size();
                static_cast<void>(m_impl->entities.Remove(operation.entity));
                DeleteBridgeObject(entityRecord);
                continue;
            }

            Impl::ContributorRecord* existing = entityRecord != nullptr
                                                    ? entityRecord->contributors.FindPtr(operation.component)
                                                    : nullptr;
            if (operation.kind == Impl::PreparedKind::DestroyContributor)
            {
                if (existing == nullptr) continue;
                if (!m_impl->scenes->DestroyProxy(existing->proxy, &renderFailure)) goto render_failure;
                m_impl->QueueDetachment(operation.entity, operation.component, existing->proxy);
                static_cast<void>(entityRecord->contributors.Remove(operation.component));
                --m_impl->stats.liveContributors;
                ++result.destroyedProxies;
                if (entityRecord->contributors.Empty())
                {
                    static_cast<void>(m_impl->entities.Remove(operation.entity));
                    DeleteBridgeObject(entityRecord);
                }
                continue;
            }

            if (operation.kind == Impl::PreparedKind::UpsertContributor)
            {
                const WorldRenderContributorDescriptor& descriptor =
                    m_impl->contributors[operation.descriptorIndex];
                if (existing != nullptr)
                {
                    if (!m_impl->scenes->DestroyProxy(existing->proxy, &renderFailure)) goto render_failure;
                    m_impl->QueueDetachment(operation.entity, operation.component, existing->proxy);
                    ++result.replacedProxies;
                }
                rendering::RenderProxyHandle proxy;
                bool created = false;
                if (descriptor.kind == WorldRenderContributorKind::Mesh)
                {
                    rendering::MeshProxyDesc build = operation.build.mesh;
                    build.proxy.scene = m_impl->scene;
                    build.proxy.producerId = operation.entity;
                    build.proxy.producerGeneration = m_impl->bridgeGeneration;
                    created = m_impl->scenes->CreateMeshProxy(build, proxy, &renderFailure);
                }
                else if (descriptor.kind == WorldRenderContributorKind::Light)
                {
                    rendering::LightProxyDesc build = operation.build.light;
                    build.proxy.scene = m_impl->scene;
                    build.proxy.producerId = operation.entity;
                    build.proxy.producerGeneration = m_impl->bridgeGeneration;
                    created = m_impl->scenes->CreateLightProxy(build, proxy, &renderFailure);
                }
                else
                {
                    rendering::DecalProxyDesc build = operation.build.decal;
                    build.proxy.scene = m_impl->scene;
                    build.proxy.producerId = operation.entity;
                    build.proxy.producerGeneration = m_impl->bridgeGeneration;
                    created = m_impl->scenes->CreateDecalProxy(build, proxy, &renderFailure);
                }
                if (!created) goto render_failure;
                if (entityRecord == nullptr)
                {
                    if (m_impl->entities.Size() == m_impl->config.maximumTrackedEntities)
                    {
                        static_cast<void>(m_impl->scenes->DestroyProxy(proxy));
                        return Fail(failure, WorldRenderBridgeFailureCode::CapacityExceeded,
                                    "world render bridge entity capacity exceeded", operation.entity,
                                    operation.component);
                    }
                    entityRecord = AllocateBridgeObject<Impl::EntityRecord>();
                    if (entityRecord == nullptr ||
                        !m_impl->entities.Insert(operation.entity, entityRecord).IsSuccessful())
                    {
                        DeleteBridgeObject(entityRecord);
                        static_cast<void>(m_impl->scenes->DestroyProxy(proxy));
                        goto render_failure;
                    }
                }
                if (existing != nullptr)
                    *existing = {descriptor.kind, proxy};
                else
                {
                    static_cast<void>(entityRecord->contributors.Insert(operation.component,
                                                                        {descriptor.kind, proxy}));
                    ++m_impl->stats.liveContributors;
                }
                ++result.createdProxies;
                continue;
            }

            if (entityRecord == nullptr) continue;
            for (auto contributor = entityRecord->contributors.Begin(), end = entityRecord->contributors.End();
                 contributor != end; ++contributor)
            {
                const rendering::RenderProxyHandle proxy = contributor.Value().proxy;
                if (HasField(operation.state.fields, WorldRenderStateFields::TransformAndBounds) &&
                    !m_impl->scenes->UpdateProxyTransform(proxy, operation.state.transform, operation.state.bounds,
                                                          m_impl->bridgeGeneration, &renderFailure)) goto render_failure;
                if (HasField(operation.state.fields, WorldRenderStateFields::Visibility) &&
                    !m_impl->scenes->UpdateProxyVisibility(proxy, operation.state.visibility,
                                                           operation.state.visibilityMask, &renderFailure)) goto render_failure;
                if (HasField(operation.state.fields, WorldRenderStateFields::LayerMask) &&
                    !m_impl->scenes->UpdateProxyLayerMask(proxy, operation.state.layerMask, &renderFailure)) goto render_failure;
                if (HasField(operation.state.fields, WorldRenderStateFields::UserDataEpoch) &&
                    !m_impl->scenes->UpdateProxyUserDataEpoch(proxy, operation.state.userDataEpoch,
                                                              &renderFailure)) goto render_failure;
                ++result.updatedProxies;
            }
          }
        }

        result.coalescedChanges = prepared.Size();
        result.sceneCommitAllowed = true;
        m_impl->stats.capturedChanges += result.capturedChanges;
        m_impl->stats.coalescedChanges += result.coalescedChanges;
        m_impl->stats.createdProxies += result.createdProxies;
        m_impl->stats.replacedProxies += result.replacedProxies;
        m_impl->stats.destroyedProxies += result.destroyedProxies;
        m_impl->stats.updatedProxies += result.updatedProxies;
        m_impl->stats.trackedEntities = m_impl->entities.Size();
        m_impl->stats.pendingDetachments = m_impl->pendingDetachments.Size();
        return true;

    render_failure:
        m_impl->stats.rebuildRequired = true;
        result.sceneCommitAllowed = false;
        return Fail(failure, WorldRenderBridgeFailureCode::RenderSceneFailure,
                    "render scene rejected a prepared world bridge transaction",
                    ecs::InvalidEntityId, ecs::InvalidComponentId, &renderFailure);
    }

    bool WorldRenderBridge::RetireDetachedProxies(containers::DynamicArray<WorldRenderDetachedProxy>& detached,
                                                  WorldRenderBridgeFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, WorldRenderBridgeFailureCode::NotInitialized,
                        "WorldRenderBridge is not initialized");
        for (u32 index = 0; index < m_impl->pendingDetachments.Size();)
        {
            const Impl::PendingDetachment& pending = m_impl->pendingDetachments[index];
            if (m_impl->scenes->IsProxyRetained(pending.proxy))
            {
                ++index;
                continue;
            }
            detached.PushBack({pending.entity, pending.contributor, pending.proxy, pending.bridgeGeneration});
            static_cast<void>(m_impl->pendingDetachments.RemoveAt(index));
            ++m_impl->stats.retiredDetachments;
        }
        m_impl->stats.pendingDetachments = m_impl->pendingDetachments.Size();
        return true;
    }

    bool WorldRenderBridge::FindProxy(const ecs::EntityId entity, const ecs::ComponentId contributor,
                                      rendering::RenderProxyHandle& proxy) const noexcept
    {
        proxy = {};
        if (m_impl == nullptr || entity == ecs::InvalidEntityId || contributor == ecs::InvalidComponentId) return false;
        const Impl::EntityRecord* const record = m_impl->FindEntity(entity);
        const Impl::ContributorRecord* const found = record != nullptr ? record->contributors.FindPtr(contributor) : nullptr;
        if (found == nullptr) return false;
        proxy = found->proxy;
        return true;
    }

    bool WorldRenderBridge::ValidateFullRebuild(WorldRenderBridgeValidationReport& report,
                                                WorldRenderBridgeFailure* const failure) const noexcept
    {
        ClearFailure(failure);
        report = {};
        if (m_impl == nullptr)
            return Fail(failure, WorldRenderBridgeFailureCode::NotInitialized,
                        "WorldRenderBridge is not initialized");
        for (auto iterator = m_impl->entities.Begin(), end = m_impl->entities.End(); iterator != end; ++iterator)
        {
            const ecs::EntityId entityId = iterator.Key();
            const ecs::Entity entity = m_impl->world->Resolve(entityId);
            if (!entity)
            {
                report.staleEntities += iterator.Value()->contributors.Size();
                continue;
            }
            for (auto contributor = iterator.Value()->contributors.Begin(), contributorEnd = iterator.Value()->contributors.End();
                 contributor != contributorEnd; ++contributor)
            {
                ++report.trackedContributors;
                if (!ecs_has_id(m_impl->world->Native(), entity.value, contributor.Key())) ++report.staleEntities;
                if (!m_impl->scenes->IsProxyAlive(contributor.Value().proxy)) ++report.staleProxies;
            }
        }
        for (const WorldRenderContributorDescriptor& descriptor : m_impl->contributors)
        {
            ecs_iter_t iterator = ecs_each_id(m_impl->world->Native(), descriptor.component);
            while (ecs_each_next(&iterator))
            {
                for (i32 row = 0; row < iterator.count; ++row)
                {
                    if (!ecs_is_enabled_id(m_impl->world->Native(), iterator.entities[row], descriptor.component))
                        continue;
                    ++report.scannedWorldContributors;
                    const ecs::EntityId identity = m_impl->world->Identity({iterator.entities[row]});
                    const Impl::EntityRecord* const record = m_impl->FindEntity(identity);
                    if (identity == ecs::InvalidEntityId || record == nullptr ||
                        record->contributors.FindPtr(descriptor.component) == nullptr) ++report.missingProxies;
                }
            }
        }
        report.valid = report.missingProxies == 0 && report.staleProxies == 0 && report.staleEntities == 0 &&
                       report.scannedWorldContributors == report.trackedContributors;
        return true;
    }

    WorldRenderBridgeStats WorldRenderBridge::GetStats() const noexcept
    {
        return m_impl != nullptr ? m_impl->stats : WorldRenderBridgeStats{};
    }
} // namespace vanguard::entities
