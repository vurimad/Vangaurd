#include <vanguard/entities/materializer.hpp>

#include <vanguard/entities/transform_runtime.hpp>

#include <vanguard/jobs/jobs.hpp>

#include <vanguard/memory/pool.hpp>

#include <new>

namespace
{
    using namespace vanguard;
    namespace entity = vanguard::entities;

    template <typename Type, typename... Args> [[nodiscard]] Type* AllocateMaterializerObject(Args&&... args) noexcept
    {
        memory::MemoryBlock block = memory::Allocate(memory::PoolId::World, sizeof(Type), alignof(Type));
        return block ? ::new (block.address) Type(static_cast<Args&&>(args)...) : nullptr;
    }

    template <typename Type> void DeleteMaterializerObject(Type* const object) noexcept
    {
        if (object == nullptr)
            return;
        object->~Type();
        memory::MemoryBlock block{object, sizeof(Type), memory::PoolId::World};
        memory::Free(block);
    }

    template <typename Enum> [[nodiscard]] constexpr bool HasFlag(const Enum value, const Enum flag) noexcept
    {
        return (static_cast<u32>(value) & static_cast<u32>(flag)) != 0;
    }

    [[nodiscard]] u64 HashIdentityPart(u64 hash, const u64 value) noexcept
    {
        constexpr u64 prime = 1099511628211ull;
        for (u32 byte = 0; byte < sizeof(value); ++byte)
        {
            hash ^= static_cast<u8>(value >> (byte * 8u));
            hash *= prime;
        }
        return hash;
    }

    [[nodiscard]] bool ShouldMaterializeEntity(const prefabs::PrefabFile& prefab, const prefabs::EntityRecord& entity, const bool includeEditorData) noexcept
    {
        if (includeEditorData)
            return true;
        const prefabs::EntityRecord* cursor = &entity;
        while (cursor != nullptr)
        {
            if (HasFlag(cursor->flags, prefabs::EntityFlags::EditorOnly))
                return false;
            cursor = cursor->parentStableId != prefabs::InvalidStableId ? prefab.FindEntity(cursor->parentStableId) : nullptr;
        }
        return true;
    }

    [[nodiscard]] bool IsInitiallyActive(const world::CellFile& cell, const world::PlacementRecord& placement, const bool includeEditorData,
                                         const bool includeInactiveGroups) noexcept
    {
        const world::ActivationGroupRecord* const group = cell.FindActivationGroup(placement.activationGroup);
        if (group == nullptr)
            return false;
        if (!includeEditorData && HasFlag(group->flags, world::ActivationGroupFlags::EditorOnly))
            return false;
        return includeInactiveGroups || HasFlag(group->flags, world::ActivationGroupFlags::DefaultActive);
    }

    [[nodiscard]] bool IsEligiblePlacement(const world::CellFile& cell, const world::PlacementRecord& placement, const bool includeEditorData) noexcept
    {
        if (!includeEditorData && HasFlag(placement.flags, world::PlacementFlags::EditorOnly))
            return false;
        const world::ActivationGroupRecord* const group = cell.FindActivationGroup(placement.activationGroup);
        return group != nullptr && (includeEditorData || !HasFlag(group->flags, world::ActivationGroupFlags::EditorOnly));
    }
} // namespace

namespace vanguard::entities
{
    ecs::EntityId DeriveEntityId(const ecs::EntityId instanceId, const u64 prefabEntityStableId, const u64 prefabRootStableId) noexcept
    {
        if (instanceId == ecs::InvalidEntityId || prefabEntityStableId == prefabs::InvalidStableId || prefabRootStableId == prefabs::InvalidStableId)
            return ecs::InvalidEntityId;
        if (prefabEntityStableId == prefabRootStableId)
            return instanceId;
        u64 hash = 14695981039346656037ull;
        hash = HashIdentityPart(hash, 0x76616e6775617264ull);
        hash = HashIdentityPart(hash, instanceId);
        hash = HashIdentityPart(hash, prefabEntityStableId);
        return hash != ecs::InvalidEntityId ? hash : 1u;
    }

    struct CellMaterializer::Impl
    {
        enum class ObjectAssemblyState : u8
        {
            Unassembled,
            Initializing,
            Attached,
            Failed
        };

        struct ExpectedComponent
        {
            ecs::EntityId entity = ecs::InvalidEntityId;
            ecs::ComponentId component = ecs::InvalidComponentId;
        };

        struct ObjectComponent
        {
            ecs::EntityId entity = ecs::InvalidEntityId;
            u64 stableId = prefabs::InvalidStableId;
            bool enabled = true;
            DecodedComponent value;
            ComponentHandle instance;
            Component* component = nullptr;
            ComponentInitialization initialization;
            PlacedComponentBindingHandle transformBinding;
        };

        struct GroupRecord;
        struct GroupLease;

        struct CellRecord
        {
            CellRecord() noexcept
                : entities(memory::pools::World::GetInstance()), expected(memory::pools::World::GetInstance()), references(memory::pools::World::GetInstance()),
                  objects(memory::pools::World::GetInstance()), baseGroups(memory::pools::World::GetInstance()),
                  groups(memory::pools::World::GetInstance()), leases(memory::pools::World::GetInstance())
            {
            }

            u64 cellId = 0;
            u32 generation = 0;
            CellState state = CellState::Unknown;
            ecs::CommandBatch commandBatch;
            containers::DynamicArray<ecs::EntityId> entities;
            containers::DynamicArray<ExpectedComponent> expected;
            containers::DynamicArray<world::EntityReferenceRecord> references;
            containers::DynamicArray<ObjectComponent> objects;
            containers::HashMap<u64, u8> baseGroups;
            containers::HashMap<u64, GroupRecord*> groups;
            containers::DynamicArray<GroupLease*> leases;
            jobs::Counter initialization;
            ObjectAssemblyState objectAssembly = ObjectAssemblyState::Unassembled;
            bool referencesPublished = false;
        };

        struct GroupRecord
        {
            GroupRecord() noexcept
                : entities(memory::pools::World::GetInstance()), expected(memory::pools::World::GetInstance()), references(memory::pools::World::GetInstance()),
                  objects(memory::pools::World::GetInstance())
            {
            }
            u64 groupId = 0;
            u32 retainCount = 0;
            ActivationGroupState state = ActivationGroupState::Inactive;
            ecs::CommandBatch commandBatch;
            containers::DynamicArray<ecs::EntityId> entities;
            containers::DynamicArray<ExpectedComponent> expected;
            containers::DynamicArray<world::EntityReferenceRecord> references;
            containers::DynamicArray<ObjectComponent> objects;
            jobs::Counter initialization;
            ObjectAssemblyState objectAssembly = ObjectAssemblyState::Unassembled;
            bool referencesPublished = false;
        };

        struct GroupLease
        {
            GroupLease() noexcept : groups(memory::pools::World::GetInstance()) {}
            u64 requestedGroup = 0;
            ActivationOwnerId owner = InvalidActivationOwnerId;
            containers::DynamicArray<u64> groups;
        };

        struct PlannedEntity
        {
            ecs::EntityId identity = ecs::InvalidEntityId;
            ecs::EntityId parent = ecs::InvalidEntityId;
            u64 instanceId = 0;
            u64 prefabEntityStableId = 0;
            const world::PlacementRecord* placement = nullptr;
            bool root = false;
            bool disabled = false;
        };

        struct PlannedComponent
        {
            ecs::EntityId entity = ecs::InvalidEntityId;
            u64 stableId = prefabs::InvalidStableId;
            ecs::ComponentId runtimeComponent = ecs::InvalidComponentId;
            ComponentStorageKind storageKind = ComponentStorageKind::Value;
            bool enabled = true;
            DecodedComponent value;
        };

        Impl(ComponentRegistry& componentRegistry, ComponentDirectory& componentOwner, EntityReferenceRegistry& referenceRegistry,
             resources::ResourceRegistry* const resourceRegistry,
             resources::ResourcePipeline* const pipeline,
             const MaterializationConfig& value) noexcept
            : components(&componentRegistry), componentDirectory(&componentOwner), references(&referenceRegistry), resources(resourceRegistry),
              resourcePipeline(pipeline), world(componentRegistry.RegisteredWorld()), config(value),
              lookup(memory::pools::World::GetInstance()), reservedIdentities(memory::pools::World::GetInstance())
        {
        }

        [[nodiscard]] CellRecord* Find(const u64 cellId) noexcept
        {
            CellRecord* record = nullptr;
            return lookup.Find(cellId, record) ? record : nullptr;
        }

        [[nodiscard]] const CellRecord* Find(const u64 cellId) const noexcept
        {
            CellRecord* record = nullptr;
            return lookup.Find(cellId, record) ? record : nullptr;
        }

        [[nodiscard]] GroupRecord* FindGroup(CellRecord& cell, const u64 groupId) const noexcept
        {
            GroupRecord* group = nullptr;
            return cell.groups.Find(groupId, group) ? group : nullptr;
        }

        [[nodiscard]] const GroupRecord* FindGroup(const CellRecord& cell, const u64 groupId) const noexcept
        {
            GroupRecord* group = nullptr;
            return cell.groups.Find(groupId, group) ? group : nullptr;
        }

        [[nodiscard]] GroupLease* FindLease(CellRecord& cell, const u64 groupId, const ActivationOwnerId owner) const noexcept
        {
            for (GroupLease* const lease : cell.leases)
                if (lease->requestedGroup == groupId && lease->owner == owner)
                    return lease;
            return nullptr;
        }

        [[nodiscard]] bool AddExpected(containers::DynamicArray<ExpectedComponent>& expected, const ExpectedComponent value) const noexcept
        {
            for (const ExpectedComponent& existing : expected)
                if (existing.entity == value.entity && existing.component == value.component)
                    return false;
            const u32 size = expected.Size();
            expected.PushBack(value);
            return expected.Size() == size + 1u;
        }

        [[nodiscard]] bool AddPlanned(containers::DynamicArray<PlannedComponent>& components, PlannedComponent&& component) const noexcept
        {
            for (const PlannedComponent& existing : components)
                if (existing.entity == component.entity && existing.stableId == component.stableId)
                    return false;
            components.PushBack(static_cast<PlannedComponent&&>(component));
            return true;
        }

        [[nodiscard]] Result PlanSelection(const world::CellFile& cell, const containers::HashMap<ecs::EntityId, u8>& selectedPlacements,
                                           const PrefabResolver resolvePrefab, void* const userData, containers::DynamicArray<PlannedEntity>& plannedEntities,
                                           containers::DynamicArray<PlannedComponent>& plannedComponents, containers::DynamicArray<ExpectedComponent>& expected,
                                           MaterializationReport& report) noexcept
        {
            containers::HashMap<ecs::EntityId, u8> identities(memory::pools::World::GetInstance());
            identities.Reserve(selectedPlacements.Size());
            for (const world::PlacementRecord& placement : cell.GetPlacements())
            {
                u8 selected = 0;
                if (!selectedPlacements.Find(placement.entityId, selected))
                {
                    ++report.placementsSkippedInactive;
                    continue;
                }
                const prefabs::PrefabFile* const prefab = resolvePrefab(placement.prefab, userData);
                if (prefab == nullptr)
                    return Result::MissingPrefab;
                if (!prefab->IsOpen() || prefab->GetEntities().Empty())
                    return Result::InvalidPrefab;

                const prefabs::EntityRecord* root = nullptr;
                for (const prefabs::EntityRecord& prefabEntity : prefab->GetEntities())
                {
                    if (prefabEntity.parentStableId != prefabs::InvalidStableId)
                        continue;
                    if (root != nullptr)
                        return Result::InvalidPrefab;
                    root = &prefabEntity;
                }
                if (root == nullptr)
                    return Result::InvalidPrefab;

                ++report.placements;
                for (const prefabs::EntityRecord& prefabEntity : prefab->GetEntities())
                {
                    if (!ShouldMaterializeEntity(*prefab, prefabEntity, config.includeEditorData))
                    {
                        ++report.entitiesSkippedEditorOnly;
                        continue;
                    }
                    const ecs::EntityId identity = DeriveEntityId(placement.entityId, prefabEntity.stableId, root->stableId);
                    u64 reservingCell = 0;
                    if (identity == ecs::InvalidEntityId || world->Resolve(identity) || reservedIdentities.Find(identity, reservingCell) ||
                        !identities.Insert(identity, 1).IsSuccessful())
                        return Result::DuplicateEntity;
                    ecs::EntityId parent = ecs::InvalidEntityId;
                    if (prefabEntity.stableId == root->stableId)
                        parent = placement.parentEntityId;
                    else
                        parent = DeriveEntityId(placement.entityId, prefabEntity.parentStableId, root->stableId);
                    const bool disabled = HasFlag(placement.flags, world::PlacementFlags::InitiallyDisabled) ||
                                          HasFlag(prefabEntity.flags, prefabs::EntityFlags::DisabledByDefault);
                    plannedEntities.PushBack(
                        {identity, parent, placement.entityId, prefabEntity.stableId, &placement, prefabEntity.stableId == root->stableId, disabled});
                    if (plannedEntities.Size() > config.maximumEntitiesPerCell)
                        return Result::LimitExceeded;

                    const containers::ArraySpan<const world::ComponentOverrideRecord> overrides = cell.GetOverridesFor(placement);
                    for (u32 local = 0; local < prefabEntity.componentCount; ++local)
                    {
                        const prefabs::ComponentRecord& component = prefab->GetComponents()[prefabEntity.firstComponent + local];
                        if (!config.includeEditorData && HasFlag(component.flags, prefabs::ComponentFlags::EditorOnly))
                            continue;
                        const world::ComponentOverrideRecord* overrideRecord = nullptr;
                        for (const world::ComponentOverrideRecord& candidate : overrides)
                            if (candidate.componentStableId == component.stableId)
                            {
                                overrideRecord = &candidate;
                                break;
                            }
                        if (overrideRecord != nullptr && overrideRecord->mode == world::OverrideMode::Remove)
                        {
                            ++report.overridesRemoved;
                            continue;
                        }
                        reflection::SchemaTypeId schema = component.schema;
                        u16 schemaVersion = component.schemaVersion;
                        containers::ArraySpan<const u8> data = prefab->GetComponentData(component);
                        if (overrideRecord != nullptr)
                        {
                            if (overrideRecord->mode != world::OverrideMode::Replace || overrideRecord->schema != component.schema)
                                return Result::MissingOverrideTarget;
                            schema = overrideRecord->schema;
                            schemaVersion = overrideRecord->schemaVersion;
                            data = cell.GetOverrideData(*overrideRecord);
                            ++report.overridesApplied;
                        }
                        PlannedComponent planned;
                        planned.entity = identity;
                        planned.stableId = component.stableId;
                        planned.enabled = !HasFlag(component.flags, prefabs::ComponentFlags::DisabledByDefault);
                        const Result decoded = components->Decode(schema, schemaVersion, data, planned.value);
                        if (decoded != Result::Success)
                            return decoded;
                        planned.storageKind = planned.value.GetStorageKind();
                        planned.runtimeComponent = components->RuntimeComponent(schema);
                        if (planned.storageKind == ComponentStorageKind::Value && !AddExpected(expected, {identity, planned.runtimeComponent}))
                            return Result::DuplicateIdentifier;
                        if (!AddPlanned(plannedComponents, static_cast<PlannedComponent&&>(planned)))
                            return Result::DuplicateIdentifier;
                        if (plannedComponents.Size() > config.maximumComponentsPerCell)
                            return Result::LimitExceeded;
                    }

                    if (prefabEntity.stableId != root->stableId)
                        continue;
                    for (const world::ComponentOverrideRecord& overrideRecord : overrides)
                    {
                        bool targetsExisting = false;
                        for (const prefabs::ComponentRecord& component : prefab->GetComponents())
                            targetsExisting |= component.stableId == overrideRecord.componentStableId;
                        if (overrideRecord.mode != world::OverrideMode::Add)
                        {
                            if (!targetsExisting)
                                return Result::MissingOverrideTarget;
                            continue;
                        }
                        if (targetsExisting)
                            return Result::MissingOverrideTarget;
                        if (!config.includeEditorData && HasFlag(overrideRecord.flags, world::OverrideFlags::EditorOnly))
                            continue;
                        PlannedComponent planned;
                        planned.entity = DeriveEntityId(placement.entityId, root->stableId, root->stableId);
                        planned.stableId = overrideRecord.componentStableId;
                        const Result decoded =
                            components->Decode(overrideRecord.schema, overrideRecord.schemaVersion, cell.GetOverrideData(overrideRecord), planned.value);
                        if (decoded != Result::Success)
                            return decoded;
                        planned.storageKind = planned.value.GetStorageKind();
                        planned.runtimeComponent = components->RuntimeComponent(overrideRecord.schema);
                        if (planned.storageKind == ComponentStorageKind::Value && !AddExpected(expected, {planned.entity, planned.runtimeComponent}))
                            return Result::DuplicateIdentifier;
                        if (!AddPlanned(plannedComponents, static_cast<PlannedComponent&&>(planned)))
                            return Result::DuplicateIdentifier;
                        ++report.overridesApplied;
                        if (plannedComponents.Size() > config.maximumComponentsPerCell)
                            return Result::LimitExceeded;
                    }
                }
            }
            return Result::Success;
        }

        [[nodiscard]] Result QueueGroupPlan(const world::CellFile& cell, const containers::DynamicArray<PlannedEntity>& plannedEntities,
                                            containers::DynamicArray<PlannedComponent>& plannedComponents,
                                            containers::DynamicArray<ExpectedComponent>& expected, GroupRecord& group) noexcept
        {
            u32 reservedCount = 0;
            for (const PlannedEntity& entity : plannedEntities)
            {
                if (!reservedIdentities.Insert(entity.identity, cell.GetCellId()).IsSuccessful())
                {
                    ReleaseReservations(plannedEntities, reservedCount);
                    return Result::DuplicateEntity;
                }
                ++reservedCount;
            }
            const ecs::CommandBatch commandBatch = world->BeginCommandBatch();
            if (!commandBatch)
            {
                ReleaseReservations(plannedEntities, reservedCount);
                return Result::OutOfMemory;
            }
            for (const PlannedEntity& entity : plannedEntities)
            {
                if (world->QueueCreate(commandBatch, entity.identity))
                    continue;
                static_cast<void>(world->CancelCommandBatch(commandBatch));
                ReleaseReservations(plannedEntities, reservedCount);
                return Result::DuplicateEntity;
            }
            for (const PlannedEntity& entity : plannedEntities)
            {
                const EntityOrigin origin{cell.GetCellId(), entity.instanceId, entity.prefabEntityStableId};
                if (!world->QueueAddComponent(commandBatch, entity.identity, originType) ||
                    !world->QueueSetComponent(commandBatch, entity.identity, originType, origin) || !AddExpected(expected, {entity.identity, originType.id}))
                {
                    static_cast<void>(world->CancelCommandBatch(commandBatch));
                    ReleaseReservations(plannedEntities, reservedCount);
                    return Result::QueueFailure;
                }
                if (entity.parent != ecs::InvalidEntityId)
                {
                    const EntityParent parent{entity.parent};
                    if (!world->QueueAddComponent(commandBatch, entity.identity, parentType) ||
                        !world->QueueSetComponent(commandBatch, entity.identity, parentType, parent) ||
                        !AddExpected(expected, {entity.identity, parentType.id}))
                    {
                        static_cast<void>(world->CancelCommandBatch(commandBatch));
                        ReleaseReservations(plannedEntities, reservedCount);
                        return Result::QueueFailure;
                    }
                }
                if (entity.root)
                {
                    WorldPlacement placement;
                    for (u32 axis = 0; axis < 3; ++axis)
                    {
                        placement.translation[axis] = cell.GetOrigin()[axis] + entity.placement->transform.translation[axis];
                        placement.scale[axis] = entity.placement->transform.scale[axis];
                    }
                    for (u32 axis = 0; axis < 4; ++axis)
                        placement.rotation[axis] = entity.placement->transform.rotation[axis];
                    if (!world->QueueAddComponent(commandBatch, entity.identity, placementType) ||
                        !world->QueueSetComponent(commandBatch, entity.identity, placementType, placement) ||
                        !AddExpected(expected, {entity.identity, placementType.id}))
                    {
                        static_cast<void>(world->CancelCommandBatch(commandBatch));
                        ReleaseReservations(plannedEntities, reservedCount);
                        return Result::QueueFailure;
                    }
                }
                if (entity.disabled &&
                    (!world->QueueAddComponent(commandBatch, entity.identity, disabledType) || !AddExpected(expected, {entity.identity, disabledType.id})))
                {
                    static_cast<void>(world->CancelCommandBatch(commandBatch));
                    ReleaseReservations(plannedEntities, reservedCount);
                    return Result::QueueFailure;
                }
            }
            for (PlannedComponent& component : plannedComponents)
            {
                if (component.storageKind == ComponentStorageKind::Object)
                {
                    ObjectComponent object;
                    object.entity = component.entity;
                    object.stableId = component.stableId;
                    object.enabled = component.enabled;
                    object.value = static_cast<DecodedComponent&&>(component.value);
                    group.objects.PushBack(static_cast<ObjectComponent&&>(object));
                    continue;
                }
                if (components->Queue(commandBatch, component.entity, component.value, component.enabled))
                    continue;
                static_cast<void>(world->CancelCommandBatch(commandBatch));
                ReleaseReservations(plannedEntities, reservedCount);
                return Result::QueueFailure;
            }
            group.commandBatch = commandBatch;
            group.entities.Reserve(plannedEntities.Size());
            for (const PlannedEntity& entity : plannedEntities)
                group.entities.PushBack(entity.identity);
            group.expected = static_cast<containers::DynamicArray<ExpectedComponent>&&>(expected);
            if (!world->SealCommandBatch(commandBatch))
            {
                static_cast<void>(world->CancelCommandBatch(commandBatch));
                ReleaseReservations(group);
                group.commandBatch = {};
                return Result::QueueFailure;
            }
            group.state = ActivationGroupState::PendingActivation;
            queuedEntities += group.entities.Size();
            return Result::Success;
        }

        void ReleaseReservations(const containers::DynamicArray<PlannedEntity>& entities, const u32 count) noexcept
        {
            for (u32 index = 0; index < count; ++index)
                static_cast<void>(reservedIdentities.Remove(entities[index].identity));
        }

        void ReleaseReservations(const CellRecord& record) noexcept
        {
            for (const ecs::EntityId identity : record.entities)
                static_cast<void>(reservedIdentities.Remove(identity));
        }

        void ReleaseReservations(const GroupRecord& record) noexcept
        {
            for (const ecs::EntityId identity : record.entities)
                static_cast<void>(reservedIdentities.Remove(identity));
        }

        void DeleteGroups(CellRecord& cell) noexcept
        {
            for (auto iterator = cell.groups.Begin(), end = cell.groups.End(); iterator != end; ++iterator)
                DeleteMaterializerObject(iterator.Value());
            for (GroupLease* const lease : cell.leases)
                DeleteMaterializerObject(lease);
            cell.groups.Clear();
            cell.leases.Clear();
        }

        [[nodiscard]] Result RollbackObjectAssembly(containers::DynamicArray<ObjectComponent>& objects, const u32 createdCount,
                                                    const Result failureResult) noexcept
        {
            bool rollbackSucceeded = true;
            for (u32 index = createdCount; index > 0; --index)
            {
                ObjectComponent& object = objects[index - 1u];
                if (!object.instance.IsValid())
                    continue;
                if (!DestroyObject(object))
                    rollbackSucceeded = false;
            }
            return rollbackSucceeded ? failureResult : Result::InvalidState;
        }

        [[nodiscard]] TransformRuntime* GetTransformRuntime() const noexcept
        {
            game::GameWorld* const gameWorld = componentDirectory != nullptr ? componentDirectory->GetWorld() : nullptr;
            return gameWorld != nullptr ? static_cast<TransformRuntime*>(gameWorld->GetSystem(TransformRuntimeSystemId)) : nullptr;
        }

        [[nodiscard]] bool DetachObjectTransform(ObjectComponent& object) noexcept
        {
            IPlacedComponent* const placed = object.component != nullptr ? object.component->GetPlacedComponent() : nullptr;
            if (!object.transformBinding.IsValid())
                return placed == nullptr || !placed->IsRegistered();
            TransformRuntime* const transforms = GetTransformRuntime();
            if (placed == nullptr || transforms == nullptr)
                return false;

            PlacedComponentBindingSnapshot snapshot;
            if (!transforms->GetPlacedComponentBinding(object.transformBinding, snapshot))
            {
                if (placed->IsRegistered())
                    return false;
                object.transformBinding = {};
                return true;
            }
            if (snapshot.entity != object.entity || snapshot.component != placed ||
                !transforms->DetachPlacedComponent(object.transformBinding))
                return false;
            object.transformBinding = {};
            return true;
        }

        [[nodiscard]] bool DestroyObject(ObjectComponent& object) noexcept
        {
            if (!object.instance.IsValid())
                return !object.transformBinding.IsValid();
            if (object.component == nullptr)
                return false;
            if (object.component->IsAttached() && !componentDirectory->DetachComponent(object.instance))
                return false;
            if (object.component->IsInitialized() && !componentDirectory->UninitializeComponent(object.instance))
                return false;
            if (!DetachObjectTransform(object) || !componentDirectory->Destroy(object.instance))
                return false;
            object.instance = {};
            object.component = nullptr;
            return true;
        }

        [[nodiscard]] static Component* ResolveSibling(const ecs::EntityId entity, const u64 stableId, void* const userData) noexcept
        {
            auto* const objects = static_cast<containers::DynamicArray<ObjectComponent>*>(userData);
            if (objects == nullptr)
                return nullptr;
            for (ObjectComponent& object : *objects)
                if (object.entity == entity && object.stableId == stableId)
                    return object.component;
            return nullptr;
        }

        [[nodiscard]] Result BeginObjectAssembly(containers::DynamicArray<ObjectComponent>& objects, jobs::Counter& completion,
                                                 ObjectAssemblyState& state) noexcept
        {
            // Establish an entity-assembly barrier: every stable sibling must exist in the
            // directory before any sibling is allowed to initialize or attach.
            u32 createdCount = 0;
            for (ObjectComponent& object : objects)
            {
                ComponentDirectoryFailure failure;
                if (!components->CreateObject(*componentDirectory, object.entity, object.stableId, object.value, object.instance, &failure))
                {
                    const Result result = failure.code == ComponentDirectoryFailureCode::CapacityExceeded ? Result::OutOfMemory : Result::InvalidState;
                    return RollbackObjectAssembly(objects, createdCount, result);
                }
                object.component = componentDirectory->Get(object.instance);
                if (object.component == nullptr)
                    return RollbackObjectAssembly(objects, createdCount + 1u, Result::InvalidState);
                ++createdCount;
            }

            for (ObjectComponent& object : objects)
            {
                const ecs::Entity runtimeEntity = world->Resolve(object.entity);
                const bool entityEnabled = runtimeEntity && !ecs_has_id(world->GetNative(), runtimeEntity.value, disabledType.id);
                ComponentDirectoryFailure failure;
                if (!runtimeEntity || !componentDirectory->SetInitialEnabledState(object.instance, object.enabled, entityEnabled, &failure))
                    return RollbackObjectAssembly(objects, createdCount, Result::InvalidState);
            }

            // Create the entity transform root and connect every floating placed component before
            // component initialization. Initialization can therefore resolve the stable transform
            // relationship while propagation remains owned by the transform CPU stage.
            TransformRuntime* transforms = nullptr;
            for (ObjectComponent& object : objects)
            {
                IPlacedComponent* const placed = object.component->GetPlacedComponent();
                if (placed == nullptr)
                    continue;
                if (transforms == nullptr)
                    transforms = GetTransformRuntime();
                if (transforms == nullptr ||
                    !transforms->AttachPlacedComponent(object.entity, *placed, object.transformBinding))
                    return RollbackObjectAssembly(objects, createdCount, Result::InvalidState);
            }

            u32 begunCount = 0;
            for (ObjectComponent& object : objects)
            {
                ComponentDirectoryFailure failure;
                if (!componentDirectory->BeginInitialization(object.instance, object.initialization, &failure))
                {
                    for (u32 index = begunCount; index > 0; --index)
                        static_cast<void>(componentDirectory->CancelInitialization(objects[index - 1u].initialization));
                    return RollbackObjectAssembly(objects, createdCount, Result::InvalidState);
                }
                ++begunCount;
            }

            if (objects.Empty())
            {
                state = ObjectAssemblyState::Attached;
                return Result::Success;
            }

            jobs::Builder builder({jobs::Priority::CriticalPath, jobs::Affinity::AnyWorker}, this);
            jobs::ParallelTask task = jobs::ParallelTask::Create(
                [this, objectsPointer = &objects](const u32 index, const jobs::JobContext& continuation) noexcept
                {
                    ObjectComponent& object = (*objectsPointer)[index];
                    game::GameWorld* const gameWorld = componentDirectory->GetWorld();
                    if (gameWorld == nullptr)
                        return;
                    ComponentInitializeContext context{*gameWorld,
                                                       ComponentResolver{object.entity, &ResolveSibling, objectsPointer},
                                                       ComponentResourceAccess{resources, resourcePipeline}, config.componentIoPriority, continuation};
                    componentDirectory->ExecuteInitialization(object.initialization, context);
                });
            if (!builder.IsValid() || !task || !builder.DispatchParallel(componentInitializationJobName, objects.Size(), static_cast<jobs::ParallelTask&&>(task)))
            {
                for (u32 index = begunCount; index > 0; --index)
                    static_cast<void>(componentDirectory->CancelInitialization(objects[index - 1u].initialization));
                return RollbackObjectAssembly(objects, createdCount, Result::OutOfMemory);
            }
            completion = builder.ExtractCounter();
            if (!completion.IsValid())
            {
                // A successful dispatch owns the callbacks. Losing its completion counter is
                // unrecoverable because their storage cannot safely be reclaimed.
                state = ObjectAssemblyState::Failed;
                return Result::InvalidState;
            }
            state = ObjectAssemblyState::Initializing;
            return Result::NotReady;
        }

        [[nodiscard]] Result CompleteObjectAssembly(containers::DynamicArray<ObjectComponent>& objects, jobs::Counter& completion,
                                                    ObjectAssemblyState& state) noexcept
        {
            if (state == ObjectAssemblyState::Attached)
                return Result::Success;
            if (state != ObjectAssemblyState::Initializing || !completion.IsValid())
                return Result::InvalidState;
            if (!completion.IsReady())
                return Result::NotReady;
            completion = {};

            bool initialized = true;
            for (ObjectComponent& object : objects)
            {
                ComponentDirectoryFailure failure;
                initialized &= componentDirectory->CompleteInitialization(object.initialization, &failure);
            }
            if (!initialized)
            {
                state = ObjectAssemblyState::Failed;
                return RollbackObjectAssembly(objects, objects.Size(), Result::InvalidState);
            }

            // Attachment remains serialized after the full initialization continuation tree.
            for (ObjectComponent& object : objects)
            {
                ComponentDirectoryFailure failure;
                if (!componentDirectory->AttachComponent(object.instance, &failure))
                {
                    state = ObjectAssemblyState::Failed;
                    return RollbackObjectAssembly(objects, objects.Size(), Result::InvalidState);
                }
            }
            // Attachment uses two distinct sibling-wide passes. PostAttach may safely resolve
            // any sibling because the entire attach pass is complete.
            for (ObjectComponent& object : objects)
            {
                ComponentDirectoryFailure failure;
                if (!componentDirectory->PostAttachComponent(object.instance, &failure))
                {
                    state = ObjectAssemblyState::Failed;
                    return RollbackObjectAssembly(objects, objects.Size(), Result::InvalidState);
                }
            }
            for (ObjectComponent& object : objects)
                object.value.Reset();
            state = ObjectAssemblyState::Attached;
            return Result::Success;
        }

        [[nodiscard]] Result DiscardObjectAssembly(containers::DynamicArray<ObjectComponent>& objects, jobs::Counter& completion,
                                                   ObjectAssemblyState& state) noexcept
        {
            if (state == ObjectAssemblyState::Initializing)
            {
                if (!completion.IsValid() || !completion.IsReady())
                    return Result::NotReady;
                completion = {};
                for (ObjectComponent& object : objects)
                {
                    ComponentDirectoryFailure failure;
                    static_cast<void>(componentDirectory->CompleteInitialization(object.initialization, &failure));
                }
                state = ObjectAssemblyState::Failed;
                if (!DestroyObjects(objects))
                    return Result::InvalidState;
                return Result::Success;
            }
            if (state == ObjectAssemblyState::Attached || state == ObjectAssemblyState::Failed)
            {
                state = ObjectAssemblyState::Failed;
                return DestroyObjects(objects) ? Result::Success : Result::InvalidState;
            }
            return Result::Success;
        }

        [[nodiscard]] bool ValidateObjects(const containers::DynamicArray<ObjectComponent>& objects) const noexcept
        {
            for (const ObjectComponent& object : objects)
            {
                if (!object.instance.IsValid())
                    continue;
                ComponentSnapshot snapshot;
                if (!componentDirectory->GetSnapshot(object.instance, snapshot) || snapshot.entity != object.entity ||
                    snapshot.stableId != object.stableId)
                    return false;
            }
            return true;
        }

        [[nodiscard]] bool DestroyObjects(containers::DynamicArray<ObjectComponent>& objects) noexcept
        {
            if (!ValidateObjects(objects))
                return false;
            for (u32 index = objects.Size(); index > 0; --index)
            {
                ObjectComponent& object = objects[index - 1u];
                if (!object.instance.IsValid())
                    continue;
                if (!DestroyObject(object))
                    return false;
            }
            return true;
        }

        ComponentRegistry* components = nullptr;
        ComponentDirectory* componentDirectory = nullptr;
        EntityReferenceRegistry* references = nullptr;
        resources::ResourceRegistry* resources = nullptr;
        resources::ResourcePipeline* resourcePipeline = nullptr;
        ecs::World* world = nullptr;
        MaterializationConfig config;
        ecs::ComponentType<EntityOrigin> originType;
        ecs::ComponentType<EntityParent> parentType;
        ecs::ComponentType<WorldPlacement> placementType;
        ecs::ComponentType<DisabledEntity> disabledType;
        containers::HashMap<u64, CellRecord*> lookup;
        containers::HashMap<ecs::EntityId, u64> reservedIdentities;
        u64 queuedEntities = 0;
        u64 activatedEntities = 0;
        u64 releasedEntities = 0;
        jobs::JobName componentInitializationJobName{"CellMaterializer/InitializeComponents"};
        u64 cancelledCells = 0;
    };

    CellMaterializer::~CellMaterializer()
    {
        if (m_impl != nullptr)
        {
            for (auto iterator = m_impl->lookup.Begin(), end = m_impl->lookup.End(); iterator != end; ++iterator)
            {
                Impl::CellRecord* const record = iterator.Value();
                m_impl->DeleteGroups(*record);
                DeleteMaterializerObject(record);
            }
            DeleteMaterializerObject(m_impl);
            m_impl = nullptr;
        }
    }

    bool CellMaterializer::Initialize(ComponentRegistry& components, ComponentDirectory& componentDirectory, EntityReferenceRegistry& references,
                                      resources::ResourceRegistry& resources, resources::ResourcePipeline& resourcePipeline,
                                      const MaterializationConfig& config) noexcept
    {
        return InitializeInternal(components, componentDirectory, references, &resources, &resourcePipeline, config);
    }

    bool CellMaterializer::Initialize(ComponentRegistry& components, ComponentDirectory& componentDirectory, EntityReferenceRegistry& references,
                                      const MaterializationConfig& config) noexcept
    {
        return InitializeInternal(components, componentDirectory, references, nullptr, nullptr, config);
    }

    bool CellMaterializer::InitializeInternal(ComponentRegistry& components, ComponentDirectory& componentDirectory,
                                              EntityReferenceRegistry& references, resources::ResourceRegistry* const resources,
                                              resources::ResourcePipeline* const resourcePipeline,
                                              const MaterializationConfig& config) noexcept
    {
        if (m_impl != nullptr || !jobs::IsInitialized() || !components.IsInitialized() || config.maximumEntitiesPerCell == 0 ||
            config.maximumComponentsPerCell == 0 ||
            !componentDirectory.IsInitialized() || (resources != nullptr && !resources->IsInitialized()) ||
            (resourcePipeline != nullptr && !resourcePipeline->IsInitialized()) || ((resources == nullptr) != (resourcePipeline == nullptr)) ||
            references.RegisteredWorld() != components.RegisteredWorld() ||
            !components.Seal())
            return false;
        Impl* const impl = AllocateMaterializerObject<Impl>(components, componentDirectory, references, resources, resourcePipeline, config);
        if (impl == nullptr)
            return false;
        impl->originType = ecs::RegisterComponent<EntityOrigin>(*impl->world);
        impl->parentType = ecs::RegisterComponent<EntityParent>(*impl->world);
        impl->placementType = ecs::RegisterComponent<WorldPlacement>(*impl->world);
        impl->disabledType = ecs::RegisterComponent<DisabledEntity>(*impl->world);
        if (!impl->originType || !impl->parentType || !impl->placementType || !impl->disabledType)
        {
            DeleteMaterializerObject(impl);
            return false;
        }
        m_impl = impl;
        return true;
    }

    bool CellMaterializer::Shutdown() noexcept
    {
        if (m_impl == nullptr)
            return true;
        if (!m_impl->lookup.Empty() || !m_impl->reservedIdentities.Empty())
            return false;
        DeleteMaterializerObject(m_impl);
        m_impl = nullptr;
        return true;
    }

    bool CellMaterializer::IsInitialized() const noexcept
    {
        return m_impl != nullptr;
    }

    Result CellMaterializer::QueueCell(const world::CellFile& cell, const u32 generation, const PrefabResolver resolvePrefab, void* const userData,
                                       MaterializationReport* const output) noexcept
    {
        if (output != nullptr)
            *output = {};
        if (m_impl == nullptr || !cell.IsOpen() || cell.GetCellId() == 0 || generation == 0 || resolvePrefab == nullptr)
            return Result::InvalidArgument;
        if (m_impl->Find(cell.GetCellId()) != nullptr)
            return Result::InvalidState;

        containers::DynamicArray<Impl::PlannedEntity> plannedEntities(memory::pools::World::GetInstance());
        containers::DynamicArray<Impl::PlannedComponent> plannedComponents(memory::pools::World::GetInstance());
        containers::DynamicArray<Impl::ExpectedComponent> expected(memory::pools::World::GetInstance());
        containers::HashMap<ecs::EntityId, u8> selectedPlacements(memory::pools::World::GetInstance());
        plannedEntities.Reserve(cell.GetPlacements().Size());
        selectedPlacements.Reserve(cell.GetPlacements().Size());

        MaterializationReport report;
        report.cellId = cell.GetCellId();
        report.generation = generation;

        for (const world::PlacementRecord& placement : cell.GetPlacements())
        {
            if (IsEligiblePlacement(cell, placement, m_impl->config.includeEditorData) &&
                IsInitiallyActive(cell, placement, m_impl->config.includeEditorData, m_impl->config.includeInactiveActivationGroups))
                static_cast<void>(selectedPlacements.Insert(placement.entityId, 1));
        }
        bool selectionChanged = true;
        while (selectionChanged)
        {
            selectionChanged = false;
            for (const world::EntityReferenceRecord& reference : cell.GetEntityReferences())
            {
                u8 selected = 0;
                if (reference.kind != world::EntityReferenceKind::RequiredLocal || !selectedPlacements.Find(reference.sourceEntityId, selected) ||
                    selectedPlacements.Find(reference.targetEntityId, selected))
                    continue;
                const world::PlacementRecord* const target = cell.FindPlacement(reference.targetEntityId);
                if (target == nullptr || !IsEligiblePlacement(cell, *target, m_impl->config.includeEditorData))
                    return Result::InvalidReference;
                if (!selectedPlacements.Insert(reference.targetEntityId, 1).IsSuccessful())
                    return Result::OutOfMemory;
                selectionChanged = true;
            }
            for (const world::PlacementRecord& placement : cell.GetPlacements())
            {
                u8 selected = 0;
                if (placement.parentEntityId == ecs::InvalidEntityId || !selectedPlacements.Find(placement.entityId, selected) ||
                    selectedPlacements.Find(placement.parentEntityId, selected))
                    continue;
                const world::PlacementRecord* const parent = cell.FindPlacement(placement.parentEntityId);
                if (parent == nullptr)
                    continue;
                if (!IsEligiblePlacement(cell, *parent, m_impl->config.includeEditorData))
                    return Result::InvalidReference;
                if (!selectedPlacements.Insert(parent->entityId, 1).IsSuccessful())
                    return Result::OutOfMemory;
                selectionChanged = true;
            }
        }

        const Result planResult =
            m_impl->PlanSelection(cell, selectedPlacements, resolvePrefab, userData, plannedEntities, plannedComponents, expected, report);
        if (planResult != Result::Success)
            return planResult;

        u32 reservedCount = 0;
        for (const Impl::PlannedEntity& entity : plannedEntities)
        {
            if (!m_impl->reservedIdentities.Insert(entity.identity, cell.GetCellId()).IsSuccessful())
            {
                m_impl->ReleaseReservations(plannedEntities, reservedCount);
                return Result::DuplicateEntity;
            }
            ++reservedCount;
        }

        const ecs::CommandBatch commandBatch = m_impl->world->BeginCommandBatch();
        if (!commandBatch)
        {
            m_impl->ReleaseReservations(plannedEntities, reservedCount);
            return Result::OutOfMemory;
        }

        for (const Impl::PlannedEntity& entity : plannedEntities)
        {
            if (!m_impl->world->QueueCreate(commandBatch, entity.identity))
            {
                static_cast<void>(m_impl->world->CancelCommandBatch(commandBatch));
                m_impl->ReleaseReservations(plannedEntities, reservedCount);
                return Result::DuplicateEntity;
            }
        }
        for (const Impl::PlannedEntity& entity : plannedEntities)
        {
            const EntityOrigin origin{cell.GetCellId(), entity.instanceId, entity.prefabEntityStableId};
            if (!m_impl->world->QueueAddComponent(commandBatch, entity.identity, m_impl->originType) ||
                !m_impl->world->QueueSetComponent(commandBatch, entity.identity, m_impl->originType, origin) ||
                !m_impl->AddExpected(expected, {entity.identity, m_impl->originType.id}))
            {
                static_cast<void>(m_impl->world->CancelCommandBatch(commandBatch));
                m_impl->ReleaseReservations(plannedEntities, reservedCount);
                return Result::QueueFailure;
            }
            if (entity.parent != ecs::InvalidEntityId)
            {
                const EntityParent parent{entity.parent};
                if (!m_impl->world->QueueAddComponent(commandBatch, entity.identity, m_impl->parentType) ||
                    !m_impl->world->QueueSetComponent(commandBatch, entity.identity, m_impl->parentType, parent) ||
                    !m_impl->AddExpected(expected, {entity.identity, m_impl->parentType.id}))
                {
                    static_cast<void>(m_impl->world->CancelCommandBatch(commandBatch));
                    m_impl->ReleaseReservations(plannedEntities, reservedCount);
                    return Result::QueueFailure;
                }
            }
            if (entity.root)
            {
                WorldPlacement placement;
                for (u32 axis = 0; axis < 3; ++axis)
                {
                    placement.translation[axis] = cell.GetOrigin()[axis] + entity.placement->transform.translation[axis];
                    placement.scale[axis] = entity.placement->transform.scale[axis];
                }
                for (u32 axis = 0; axis < 4; ++axis)
                    placement.rotation[axis] = entity.placement->transform.rotation[axis];
                if (!m_impl->world->QueueAddComponent(commandBatch, entity.identity, m_impl->placementType) ||
                    !m_impl->world->QueueSetComponent(commandBatch, entity.identity, m_impl->placementType, placement) ||
                    !m_impl->AddExpected(expected, {entity.identity, m_impl->placementType.id}))
                {
                    static_cast<void>(m_impl->world->CancelCommandBatch(commandBatch));
                    m_impl->ReleaseReservations(plannedEntities, reservedCount);
                    return Result::QueueFailure;
                }
            }
            if (entity.disabled)
            {
                if (!m_impl->world->QueueAddComponent(commandBatch, entity.identity, m_impl->disabledType) ||
                    !m_impl->AddExpected(expected, {entity.identity, m_impl->disabledType.id}))
                {
                    static_cast<void>(m_impl->world->CancelCommandBatch(commandBatch));
                    m_impl->ReleaseReservations(plannedEntities, reservedCount);
                    return Result::QueueFailure;
                }
            }
        }
        for (const Impl::PlannedComponent& component : plannedComponents)
        {
            if (component.storageKind == ComponentStorageKind::Object)
                continue;
            if (!m_impl->components->Queue(commandBatch, component.entity, component.value, component.enabled))
            {
                static_cast<void>(m_impl->world->CancelCommandBatch(commandBatch));
                m_impl->ReleaseReservations(plannedEntities, reservedCount);
                return Result::QueueFailure;
            }
        }

        Impl::CellRecord* const record = AllocateMaterializerObject<Impl::CellRecord>();
        if (record == nullptr)
        {
            static_cast<void>(m_impl->world->CancelCommandBatch(commandBatch));
            m_impl->ReleaseReservations(plannedEntities, reservedCount);
            return Result::OutOfMemory;
        }
        record->cellId = cell.GetCellId();
        record->generation = generation;
        record->state = CellState::PendingActivation;
        record->commandBatch = commandBatch;
        record->entities.Reserve(plannedEntities.Size());
        for (const Impl::PlannedEntity& entity : plannedEntities)
            record->entities.PushBack(entity.identity);
        record->objects.Reserve(plannedComponents.Size());
        for (Impl::PlannedComponent& component : plannedComponents)
        {
            if (component.storageKind != ComponentStorageKind::Object)
                continue;
            Impl::ObjectComponent object;
            object.entity = component.entity;
            object.stableId = component.stableId;
            object.enabled = component.enabled;
            object.value = static_cast<DecodedComponent&&>(component.value);
            record->objects.PushBack(static_cast<Impl::ObjectComponent&&>(object));
        }
        record->expected = static_cast<containers::DynamicArray<Impl::ExpectedComponent>&&>(expected);
        record->baseGroups.Reserve(cell.GetActivationGroups().Size());
        for (const world::PlacementRecord& placement : cell.GetPlacements())
        {
            u8 selected = 0;
            if (selectedPlacements.Find(placement.entityId, selected))
                static_cast<void>(record->baseGroups.Set(placement.activationGroup, 1));
        }
        for (const world::EntityReferenceRecord& reference : cell.GetEntityReferences())
        {
            u8 selected = 0;
            if (selectedPlacements.Find(reference.sourceEntityId, selected))
                record->references.PushBack(reference);
        }
        if (!m_impl->lookup.Insert(record->cellId, record).IsSuccessful())
        {
            static_cast<void>(m_impl->world->CancelCommandBatch(commandBatch));
            m_impl->ReleaseReservations(plannedEntities, reservedCount);
            DeleteMaterializerObject(record);
            return Result::OutOfMemory;
        }
        if (!m_impl->world->SealCommandBatch(commandBatch))
        {
            static_cast<void>(m_impl->lookup.Remove(record->cellId));
            static_cast<void>(m_impl->world->CancelCommandBatch(commandBatch));
            m_impl->ReleaseReservations(plannedEntities, reservedCount);
            DeleteMaterializerObject(record);
            return Result::QueueFailure;
        }
        m_impl->queuedEntities += record->entities.Size();
        report.entities = record->entities.Size();
        report.components = plannedComponents.Size();
        if (output != nullptr)
            *output = report;
        return Result::Success;
    }

    Result CellMaterializer::PrepareActivation(const u64 cellId, const u32 generation) noexcept
    {
        if (m_impl == nullptr || cellId == 0 || generation == 0)
            return Result::InvalidArgument;
        Impl::CellRecord* const record = m_impl->Find(cellId);
        if (record == nullptr || record->state != CellState::PendingActivation)
            return Result::InvalidState;
        if (record->generation != generation)
            return Result::StaleGeneration;
        if (record->objectAssembly == Impl::ObjectAssemblyState::Unassembled)
        {
            const ecs::CommandBatchStatus batchStatus = m_impl->world->GetCommandBatchStatus(record->commandBatch);
            if (batchStatus == ecs::CommandBatchStatus::Open || batchStatus == ecs::CommandBatchStatus::Pending)
                return Result::NotReady;
            if (batchStatus == ecs::CommandBatchStatus::Failed)
            {
                static_cast<void>(m_impl->world->RetireCommandBatch(record->commandBatch));
                record->commandBatch = {};
                record->state = CellState::Failed;
                return Result::QueueFailure;
            }
            if (batchStatus != ecs::CommandBatchStatus::Succeeded)
                return Result::InvalidState;
            for (const ecs::EntityId identity : record->entities)
            {
                if (m_impl->world->Resolve(identity))
                    continue;
                static_cast<void>(m_impl->world->RetireCommandBatch(record->commandBatch));
                record->commandBatch = {};
                record->state = CellState::Failed;
                return Result::QueueFailure;
            }
            for (const Impl::ExpectedComponent& expected : record->expected)
            {
                const ecs::Entity entity = m_impl->world->Resolve(expected.entity);
                if (!entity || !ecs_has_id(m_impl->world->GetNative(), entity.value, expected.component))
                {
                    static_cast<void>(m_impl->world->RetireCommandBatch(record->commandBatch));
                    record->commandBatch = {};
                    record->state = CellState::Failed;
                    return Result::QueueFailure;
                }
            }
            if (!m_impl->world->RetireCommandBatch(record->commandBatch))
                return Result::InvalidState;
            record->commandBatch = {};
            const Result beginResult = m_impl->BeginObjectAssembly(record->objects, record->initialization, record->objectAssembly);
            if (beginResult != Result::Success)
            {
                if (beginResult != Result::NotReady)
                    record->state = CellState::Failed;
                return beginResult;
            }
        }
        else if (record->objectAssembly == Impl::ObjectAssemblyState::Initializing)
        {
            const Result completeResult = m_impl->CompleteObjectAssembly(record->objects, record->initialization, record->objectAssembly);
            if (completeResult != Result::Success)
            {
                if (completeResult != Result::NotReady)
                    record->state = CellState::Failed;
                return completeResult;
            }
        }
        else if (record->objectAssembly != Impl::ObjectAssemblyState::Attached)
            return Result::InvalidState;
        return Result::Success;
    }

    Result CellMaterializer::PublishActivation(const u64 cellId, const u32 generation) noexcept
    {
        if (m_impl == nullptr || cellId == 0 || generation == 0)
            return Result::InvalidArgument;
        Impl::CellRecord* const record = m_impl->Find(cellId);
        if (record == nullptr ||
            (record->state != CellState::PendingActivation && record->state != CellState::PendingReferences && record->state != CellState::Active))
            return Result::InvalidState;
        if (record->generation != generation)
            return Result::StaleGeneration;
        if (record->state == CellState::Active)
            return m_impl->references->IsPartitionReady(cellId, generation, EntityReferenceRegistry::BasePartition) ? Result::Success : Result::NotReady;
        if (record->state == CellState::PendingReferences)
        {
            if (!m_impl->references->IsPartitionReady(cellId, generation, EntityReferenceRegistry::BasePartition))
                return Result::NotReady;
            record->state = CellState::Active;
            m_impl->activatedEntities += record->entities.Size();
            return Result::Success;
        }
        if (record->objectAssembly != Impl::ObjectAssemblyState::Attached || record->referencesPublished)
            return Result::InvalidState;
        const ReferenceResult referenceResult =
            m_impl->references->RegisterCell(cellId, generation, containers::ArraySpan<const ecs::EntityId>(record->entities),
                                             containers::ArraySpan<const world::EntityReferenceRecord>(record->references));
        if (referenceResult != ReferenceResult::Success)
        {
            record->state = CellState::Failed;
            return Result::InvalidReference;
        }
        record->referencesPublished = true;
        if (!m_impl->references->IsPartitionReady(cellId, generation, EntityReferenceRegistry::BasePartition))
        {
            record->state = CellState::PendingReferences;
            return Result::NotReady;
        }
        record->state = CellState::Active;
        m_impl->activatedEntities += record->entities.Size();
        return Result::Success;
    }

    Result CellMaterializer::QueueRelease(const u64 cellId, const u32 generation) noexcept
    {
        if (m_impl == nullptr || cellId == 0 || generation == 0)
            return Result::InvalidArgument;
        Impl::CellRecord* const record = m_impl->Find(cellId);
        if (record == nullptr || (record->state != CellState::Active && record->state != CellState::PendingReferences && record->state != CellState::Failed))
            return Result::InvalidState;
        if (record->generation != generation)
            return Result::StaleGeneration;
        for (auto iterator = record->groups.Begin(), end = record->groups.End(); iterator != end; ++iterator)
        {
            Impl::GroupRecord* const group = iterator.Value();
            if (group->state == ActivationGroupState::PendingRelease)
                return Result::InvalidState;
            if (group->state != ActivationGroupState::PendingActivation)
                continue;
            if (group->objectAssembly == Impl::ObjectAssemblyState::Initializing ||
                group->objectAssembly == Impl::ObjectAssemblyState::Attached || group->objectAssembly == Impl::ObjectAssemblyState::Failed)
            {
                const Result discardResult = m_impl->DiscardObjectAssembly(group->objects, group->initialization, group->objectAssembly);
                if (discardResult != Result::Success)
                    return discardResult;
            }
            else
            {
                const ecs::CommandBatchStatus status = m_impl->world->GetCommandBatchStatus(group->commandBatch);
                if (status == ecs::CommandBatchStatus::Open || status == ecs::CommandBatchStatus::Pending)
                {
                    if (!m_impl->world->CancelCommandBatch(group->commandBatch))
                        return Result::NotReady;
                }
                else if (status == ecs::CommandBatchStatus::Succeeded || status == ecs::CommandBatchStatus::Failed)
                {
                    if (!m_impl->world->RetireCommandBatch(group->commandBatch))
                        return Result::InvalidState;
                }
                else
                    return Result::InvalidState;
                group->commandBatch = {};
            }
            group->state = ActivationGroupState::Failed;
        }
        const ecs::CommandBatch commandBatch = m_impl->world->BeginCommandBatch();
        if (!commandBatch)
            return Result::OutOfMemory;
        for (auto iterator = record->groups.Begin(), end = record->groups.End(); iterator != end; ++iterator)
        {
            Impl::GroupRecord* const group = iterator.Value();
            for (u32 index = group->entities.Size(); index > 0; --index)
                if (m_impl->world->Resolve(group->entities[index - 1u]) && !m_impl->world->QueueDestroy(commandBatch, group->entities[index - 1u]))
                {
                    static_cast<void>(m_impl->world->CancelCommandBatch(commandBatch));
                    return Result::QueueFailure;
                }
        }
        for (u32 index = record->entities.Size(); index > 0; --index)
            if (m_impl->world->Resolve(record->entities[index - 1u]) && !m_impl->world->QueueDestroy(commandBatch, record->entities[index - 1u]))
            {
                static_cast<void>(m_impl->world->CancelCommandBatch(commandBatch));
                return Result::QueueFailure;
            }
        if (!m_impl->world->SealCommandBatch(commandBatch))
        {
            static_cast<void>(m_impl->world->CancelCommandBatch(commandBatch));
            return Result::QueueFailure;
        }
        for (auto iterator = record->groups.Begin(), end = record->groups.End(); iterator != end; ++iterator)
            if (!m_impl->ValidateObjects(iterator.Value()->objects))
            {
                static_cast<void>(m_impl->world->CancelCommandBatch(commandBatch));
                return Result::InvalidState;
            }
        if (!m_impl->ValidateObjects(record->objects))
        {
            static_cast<void>(m_impl->world->CancelCommandBatch(commandBatch));
            return Result::InvalidState;
        }
        if (record->referencesPublished)
        {
            if (m_impl->references->UnregisterCell(cellId, generation) != ReferenceResult::Success)
            {
                static_cast<void>(m_impl->world->CancelCommandBatch(commandBatch));
                return Result::InvalidReference;
            }
            record->referencesPublished = false;
        }
        for (auto iterator = record->groups.Begin(), end = record->groups.End(); iterator != end; ++iterator)
            if (!m_impl->DestroyObjects(iterator.Value()->objects))
            {
                static_cast<void>(m_impl->world->CancelCommandBatch(commandBatch));
                return Result::InvalidState;
            }
        if (!m_impl->DestroyObjects(record->objects))
        {
            static_cast<void>(m_impl->world->CancelCommandBatch(commandBatch));
            return Result::InvalidState;
        }
        record->commandBatch = commandBatch;
        record->state = CellState::PendingRelease;
        return Result::Success;
    }

    Result CellMaterializer::CompleteRelease(const u64 cellId, const u32 generation) noexcept
    {
        if (m_impl == nullptr || cellId == 0 || generation == 0)
            return Result::InvalidArgument;
        Impl::CellRecord* const record = m_impl->Find(cellId);
        if (record == nullptr || record->state != CellState::PendingRelease)
            return Result::InvalidState;
        if (record->generation != generation)
            return Result::StaleGeneration;
        const ecs::CommandBatchStatus batchStatus = m_impl->world->GetCommandBatchStatus(record->commandBatch);
        if (batchStatus == ecs::CommandBatchStatus::Open || batchStatus == ecs::CommandBatchStatus::Pending)
            return Result::NotReady;
        if (batchStatus == ecs::CommandBatchStatus::Failed)
        {
            static_cast<void>(m_impl->world->RetireCommandBatch(record->commandBatch));
            record->commandBatch = {};
            record->state = CellState::Failed;
            return Result::QueueFailure;
        }
        if (batchStatus != ecs::CommandBatchStatus::Succeeded)
            return Result::InvalidState;
        for (const ecs::EntityId identity : record->entities)
        {
            if (!m_impl->world->Resolve(identity))
                continue;
            static_cast<void>(m_impl->world->RetireCommandBatch(record->commandBatch));
            record->commandBatch = {};
            record->state = CellState::Failed;
            return Result::QueueFailure;
        }
        for (auto iterator = record->groups.Begin(), end = record->groups.End(); iterator != end; ++iterator)
            for (const ecs::EntityId identity : iterator.Value()->entities)
                if (m_impl->world->Resolve(identity))
                    return Result::QueueFailure;
        if (!m_impl->world->RetireCommandBatch(record->commandBatch))
            return Result::InvalidState;
        record->commandBatch = {};
        m_impl->releasedEntities += record->entities.Size();
        for (auto iterator = record->groups.Begin(), end = record->groups.End(); iterator != end; ++iterator)
        {
            m_impl->releasedEntities += iterator.Value()->entities.Size();
            m_impl->ReleaseReservations(*iterator.Value());
        }
        m_impl->ReleaseReservations(*record);
        static_cast<void>(m_impl->lookup.Remove(cellId));
        m_impl->DeleteGroups(*record);
        DeleteMaterializerObject(record);
        return Result::Success;
    }

    Result CellMaterializer::Cancel(const u64 cellId, const u32 generation) noexcept
    {
        if (m_impl == nullptr || cellId == 0 || generation == 0)
            return Result::InvalidArgument;
        Impl::CellRecord* const record = m_impl->Find(cellId);
        if (record == nullptr || (record->state != CellState::PendingActivation && record->state != CellState::PendingReferences))
            return Result::InvalidState;
        if (record->generation != generation)
            return Result::StaleGeneration;
        if (record->state == CellState::PendingReferences)
        {
            ++m_impl->cancelledCells;
            return QueueRelease(cellId, generation);
        }
        if (record->objectAssembly == Impl::ObjectAssemblyState::Initializing ||
            record->objectAssembly == Impl::ObjectAssemblyState::Attached || record->objectAssembly == Impl::ObjectAssemblyState::Failed)
        {
            const Result discardResult = m_impl->DiscardObjectAssembly(record->objects, record->initialization, record->objectAssembly);
            if (discardResult != Result::Success)
                return discardResult;
            ++m_impl->cancelledCells;
            record->state = CellState::Failed;
            return QueueRelease(cellId, generation);
        }
        if (!m_impl->world->CancelCommandBatch(record->commandBatch))
            return Result::InvalidState;
        record->commandBatch = {};
        bool anyLive = false;
        for (const ecs::EntityId identity : record->entities)
            anyLive |= static_cast<bool>(m_impl->world->Resolve(identity));
        ++m_impl->cancelledCells;
        if (anyLive)
        {
            record->state = CellState::Failed;
            return QueueRelease(cellId, generation);
        }
        static_cast<void>(m_impl->lookup.Remove(cellId));
        m_impl->ReleaseReservations(*record);
        DeleteMaterializerObject(record);
        return Result::Success;
    }

    Result CellMaterializer::AcquireActivationGroup(const world::CellFile& cell, const u32 generation, const u64 groupId, const ActivationOwnerId ownerId,
                                                    const PrefabResolver resolvePrefab, void* const userData) noexcept
    {
        if (m_impl == nullptr || !cell.IsOpen() || cell.GetCellId() == 0 || generation == 0 || groupId == world::AlwaysActiveGroup ||
            ownerId == InvalidActivationOwnerId || resolvePrefab == nullptr)
            return Result::InvalidArgument;
        Impl::CellRecord* const record = m_impl->Find(cell.GetCellId());
        if (record == nullptr || record->generation != generation || record->state != CellState::Active)
            return record != nullptr && record->generation != generation ? Result::StaleGeneration : Result::InvalidState;
        if (cell.FindActivationGroup(groupId) == nullptr || m_impl->FindLease(*record, groupId, ownerId) != nullptr)
            return Result::InvalidState;

        Impl::GroupLease* const lease = AllocateMaterializerObject<Impl::GroupLease>();
        if (lease == nullptr)
            return Result::OutOfMemory;
        lease->requestedGroup = groupId;
        lease->owner = ownerId;
        lease->groups.PushBack(groupId);
        bool changed = true;
        while (changed)
        {
            changed = false;
            for (const world::EntityReferenceRecord& reference : cell.GetEntityReferences())
            {
                if (reference.kind != world::EntityReferenceKind::RequiredLocal)
                    continue;
                const world::PlacementRecord* const source = cell.FindPlacement(reference.sourceEntityId);
                const world::PlacementRecord* const target = cell.FindPlacement(reference.targetEntityId);
                if (source == nullptr || target == nullptr)
                {
                    DeleteMaterializerObject(lease);
                    return Result::InvalidReference;
                }
                bool sourceSelected = false;
                bool targetSelected = false;
                for (const u64 selectedGroup : lease->groups)
                {
                    sourceSelected |= selectedGroup == source->activationGroup;
                    targetSelected |= selectedGroup == target->activationGroup;
                }
                if (!sourceSelected || targetSelected)
                    continue;
                if (!IsEligiblePlacement(cell, *target, m_impl->config.includeEditorData))
                {
                    DeleteMaterializerObject(lease);
                    return Result::InvalidReference;
                }
                lease->groups.PushBack(target->activationGroup);
                changed = true;
            }
            for (const world::PlacementRecord& placement : cell.GetPlacements())
            {
                bool sourceGroupSelected = false;
                for (const u64 selectedGroup : lease->groups)
                    sourceGroupSelected |= selectedGroup == placement.activationGroup;
                if (!sourceGroupSelected || placement.parentEntityId == ecs::InvalidEntityId)
                    continue;
                const world::PlacementRecord* const parent = cell.FindPlacement(placement.parentEntityId);
                if (parent == nullptr)
                    continue;
                bool alreadySelected = false;
                for (const u64 selectedGroup : lease->groups)
                    alreadySelected |= selectedGroup == parent->activationGroup;
                if (!alreadySelected)
                {
                    lease->groups.PushBack(parent->activationGroup);
                    changed = true;
                }
            }
        }

        containers::DynamicArray<u64> createdGroups(memory::pools::World::GetInstance());
        const auto rollbackCreatedGroups = [&]() noexcept
        {
            for (const u64 createdId : createdGroups)
            {
                Impl::GroupRecord* const created = m_impl->FindGroup(*record, createdId);
                if (created == nullptr)
                    continue;
                static_cast<void>(m_impl->world->CancelCommandBatch(created->commandBatch));
                m_impl->ReleaseReservations(*created);
                static_cast<void>(record->groups.Remove(createdId));
                DeleteMaterializerObject(created);
            }
        };
        record->groups.Reserve(record->groups.Size() + lease->groups.Size());
        record->leases.Reserve(record->leases.Size() + 1u);
        for (const u64 dependencyGroup : lease->groups)
        {
            u8 base = 0;
            if (record->baseGroups.Find(dependencyGroup, base))
                continue;
            Impl::GroupRecord* existing = m_impl->FindGroup(*record, dependencyGroup);
            if (existing != nullptr)
            {
                if (existing->state == ActivationGroupState::PendingRelease || existing->state == ActivationGroupState::Failed)
                {
                    rollbackCreatedGroups();
                    DeleteMaterializerObject(lease);
                    return Result::InvalidState;
                }
                continue;
            }

            Impl::GroupRecord* const group = AllocateMaterializerObject<Impl::GroupRecord>();
            if (group == nullptr)
            {
                rollbackCreatedGroups();
                DeleteMaterializerObject(lease);
                return Result::OutOfMemory;
            }
            group->groupId = dependencyGroup;
            containers::HashMap<ecs::EntityId, u8> selected(memory::pools::World::GetInstance());
            const world::ActivationGroupRecord* const authoredGroup = cell.FindActivationGroup(dependencyGroup);
            if (authoredGroup == nullptr)
            {
                DeleteMaterializerObject(group);
                rollbackCreatedGroups();
                DeleteMaterializerObject(lease);
                return Result::InvalidReference;
            }
            for (const world::PlacementRecord& placement : cell.GetPlacementsInGroup(*authoredGroup))
                if (IsEligiblePlacement(cell, placement, m_impl->config.includeEditorData))
                    static_cast<void>(selected.Insert(placement.entityId, 1));
            containers::DynamicArray<Impl::PlannedEntity> plannedEntities(memory::pools::World::GetInstance());
            containers::DynamicArray<Impl::PlannedComponent> plannedComponents(memory::pools::World::GetInstance());
            containers::DynamicArray<Impl::ExpectedComponent> expected(memory::pools::World::GetInstance());
            MaterializationReport report;
            report.cellId = cell.GetCellId();
            report.generation = generation;
            const Result planResult = m_impl->PlanSelection(cell, selected, resolvePrefab, userData, plannedEntities, plannedComponents, expected, report);
            if (planResult != Result::Success)
            {
                DeleteMaterializerObject(group);
                rollbackCreatedGroups();
                DeleteMaterializerObject(lease);
                return planResult;
            }
            for (const world::EntityReferenceRecord& reference : cell.GetEntityReferences())
            {
                u8 sourceSelected = 0;
                if (selected.Find(reference.sourceEntityId, sourceSelected))
                    group->references.PushBack(reference);
            }
            const Result queueResult = m_impl->QueueGroupPlan(cell, plannedEntities, plannedComponents, expected, *group);
            if (queueResult != Result::Success)
            {
                DeleteMaterializerObject(group);
                rollbackCreatedGroups();
                DeleteMaterializerObject(lease);
                return queueResult;
            }
            if (!record->groups.Insert(dependencyGroup, group).IsSuccessful())
            {
                static_cast<void>(m_impl->world->CancelCommandBatch(group->commandBatch));
                m_impl->ReleaseReservations(*group);
                DeleteMaterializerObject(group);
                rollbackCreatedGroups();
                DeleteMaterializerObject(lease);
                return Result::OutOfMemory;
            }
            createdGroups.PushBack(dependencyGroup);
        }
        for (const u64 dependencyGroup : lease->groups)
        {
            Impl::GroupRecord* const group = m_impl->FindGroup(*record, dependencyGroup);
            if (group != nullptr)
                ++group->retainCount;
        }
        record->leases.PushBack(lease);
        return Result::Success;
    }

    Result CellMaterializer::PrepareActivationGroups(const u64 cellId, const u32 generation) noexcept
    {
        if (m_impl == nullptr || cellId == 0 || generation == 0)
            return Result::InvalidArgument;
        Impl::CellRecord* const record = m_impl->Find(cellId);
        if (record == nullptr)
            return Result::InvalidState;
        if (record->generation != generation)
            return Result::StaleGeneration;
        bool pending = false;
        containers::DynamicArray<u64> completedReleases(memory::pools::World::GetInstance());
        for (auto iterator = record->groups.Begin(), end = record->groups.End(); iterator != end; ++iterator)
        {
            Impl::GroupRecord* const group = iterator.Value();
            if (group->state == ActivationGroupState::PendingActivation)
            {
                if (group->objectAssembly == Impl::ObjectAssemblyState::Unassembled)
                {
                    const ecs::CommandBatchStatus status = m_impl->world->GetCommandBatchStatus(group->commandBatch);
                    if (status == ecs::CommandBatchStatus::Open || status == ecs::CommandBatchStatus::Pending)
                    {
                        pending = true;
                        continue;
                    }
                    if (status != ecs::CommandBatchStatus::Succeeded)
                    {
                        static_cast<void>(m_impl->world->RetireCommandBatch(group->commandBatch));
                        group->commandBatch = {};
                        group->state = ActivationGroupState::Failed;
                        return Result::QueueFailure;
                    }
                    for (const ecs::EntityId identity : group->entities)
                        if (!m_impl->world->Resolve(identity))
                        {
                            group->state = ActivationGroupState::Failed;
                            return Result::QueueFailure;
                        }
                    for (const Impl::ExpectedComponent& expected : group->expected)
                    {
                        const ecs::Entity entity = m_impl->world->Resolve(expected.entity);
                        if (!entity || !ecs_has_id(m_impl->world->GetNative(), entity.value, expected.component))
                        {
                            pending = true;
                            goto next_group;
                        }
                    }
                    if (!m_impl->world->RetireCommandBatch(group->commandBatch))
                        return Result::InvalidState;
                    group->commandBatch = {};
                    const Result beginResult = m_impl->BeginObjectAssembly(group->objects, group->initialization, group->objectAssembly);
                    if (beginResult == Result::NotReady)
                    {
                        pending = true;
                        continue;
                    }
                    if (beginResult != Result::Success)
                    {
                        group->state = ActivationGroupState::Failed;
                        return beginResult;
                    }
                }
                else if (group->objectAssembly == Impl::ObjectAssemblyState::Initializing)
                {
                    const Result completeResult = m_impl->CompleteObjectAssembly(group->objects, group->initialization, group->objectAssembly);
                    if (completeResult == Result::NotReady)
                    {
                        pending = true;
                        continue;
                    }
                    if (completeResult != Result::Success)
                    {
                        group->state = ActivationGroupState::Failed;
                        return completeResult;
                    }
                }
                else if (group->objectAssembly != Impl::ObjectAssemblyState::Attached)
                    return Result::InvalidState;
            }
            if (group->state == ActivationGroupState::PendingRelease)
            {
                const ecs::CommandBatchStatus status = m_impl->world->GetCommandBatchStatus(group->commandBatch);
                if (status == ecs::CommandBatchStatus::Open || status == ecs::CommandBatchStatus::Pending)
                {
                    pending = true;
                    continue;
                }
                if (status != ecs::CommandBatchStatus::Succeeded)
                    return Result::QueueFailure;
                for (const ecs::EntityId identity : group->entities)
                    if (m_impl->world->Resolve(identity))
                        return Result::QueueFailure;
                if (!m_impl->world->RetireCommandBatch(group->commandBatch))
                    return Result::InvalidState;
                group->commandBatch = {};
                m_impl->releasedEntities += group->entities.Size();
                m_impl->ReleaseReservations(*group);
                completedReleases.PushBack(group->groupId);
            }
        next_group:;
        }
        for (const u64 completedGroup : completedReleases)
        {
            Impl::GroupRecord* const group = m_impl->FindGroup(*record, completedGroup);
            static_cast<void>(record->groups.Remove(completedGroup));
            DeleteMaterializerObject(group);
        }
        pending = false;
        for (auto iterator = record->groups.Begin(), end = record->groups.End(); iterator != end; ++iterator)
        {
            Impl::GroupRecord* const group = iterator.Value();
            pending |= (group->state == ActivationGroupState::PendingActivation &&
                        group->objectAssembly != Impl::ObjectAssemblyState::Attached) ||
                       group->state == ActivationGroupState::PendingRelease;
        }
        return pending ? Result::NotReady : Result::Success;
    }

    Result CellMaterializer::PublishActivationGroups(const u64 cellId, const u32 generation) noexcept
    {
        if (m_impl == nullptr || cellId == 0 || generation == 0)
            return Result::InvalidArgument;
        Impl::CellRecord* const record = m_impl->Find(cellId);
        if (record == nullptr)
            return Result::InvalidState;
        if (record->generation != generation)
            return Result::StaleGeneration;

        bool pending = false;
        for (auto iterator = record->groups.Begin(), end = record->groups.End(); iterator != end; ++iterator)
        {
            Impl::GroupRecord* const group = iterator.Value();
            if (group->state == ActivationGroupState::PendingActivation)
            {
                if (group->objectAssembly != Impl::ObjectAssemblyState::Attached || group->referencesPublished)
                {
                    pending = true;
                    continue;
                }
                if (m_impl->references->RegisterPartition(cellId, generation, group->groupId,
                                                          containers::ArraySpan<const ecs::EntityId>(group->entities),
                                                          containers::ArraySpan<const world::EntityReferenceRecord>(group->references)) !=
                    ReferenceResult::Success)
                {
                    group->state = ActivationGroupState::Failed;
                    return Result::InvalidReference;
                }
                group->referencesPublished = true;
                group->state = m_impl->references->IsPartitionReady(cellId, generation, group->groupId) ? ActivationGroupState::Active
                                                                                                        : ActivationGroupState::PendingReferences;
                m_impl->activatedEntities += group->entities.Size();
            }
            if (group->state == ActivationGroupState::PendingReferences)
            {
                if (m_impl->references->IsPartitionReady(cellId, generation, group->groupId))
                    group->state = ActivationGroupState::Active;
                else
                    pending = true;
            }
            pending |= group->state == ActivationGroupState::PendingActivation || group->state == ActivationGroupState::PendingRelease;
        }
        return pending ? Result::NotReady : Result::Success;
    }

    Result CellMaterializer::ReleaseActivationGroup(const u64 cellId, const u32 generation, const u64 groupId, const ActivationOwnerId ownerId) noexcept
    {
        if (m_impl == nullptr || cellId == 0 || generation == 0 || groupId == world::AlwaysActiveGroup || ownerId == InvalidActivationOwnerId)
            return Result::InvalidArgument;
        Impl::CellRecord* const record = m_impl->Find(cellId);
        if (record == nullptr)
            return Result::InvalidState;
        if (record->generation != generation)
            return Result::StaleGeneration;
        Impl::GroupLease* const lease = m_impl->FindLease(*record, groupId, ownerId);
        if (lease == nullptr)
            return Result::InvalidState;

        containers::DynamicArray<u64> removeImmediately(memory::pools::World::GetInstance());
        for (const u64 dependencyGroup : lease->groups)
        {
            Impl::GroupRecord* const group = m_impl->FindGroup(*record, dependencyGroup);
            if (group == nullptr || group->retainCount > 1u)
                continue;
            if (group->state == ActivationGroupState::PendingActivation)
            {
                if (group->objectAssembly == Impl::ObjectAssemblyState::Initializing ||
                    group->objectAssembly == Impl::ObjectAssemblyState::Attached || group->objectAssembly == Impl::ObjectAssemblyState::Failed)
                {
                    const Result discardResult = m_impl->DiscardObjectAssembly(group->objects, group->initialization, group->objectAssembly);
                    if (discardResult != Result::Success)
                        return discardResult;
                    group->state = ActivationGroupState::Failed;
                }
                else
                {
                if (!m_impl->world->CancelCommandBatch(group->commandBatch))
                    return Result::InvalidState;
                group->commandBatch = {};
                m_impl->ReleaseReservations(*group);
                removeImmediately.PushBack(dependencyGroup);
                continue;
                }
            }
            if (group->state != ActivationGroupState::Active && group->state != ActivationGroupState::PendingReferences &&
                group->state != ActivationGroupState::Failed)
                return Result::InvalidState;
            const ecs::CommandBatch batch = m_impl->world->BeginCommandBatch();
            if (!batch)
                return Result::OutOfMemory;
            for (u32 index = group->entities.Size(); index > 0; --index)
            {
                if (!m_impl->world->Resolve(group->entities[index - 1u]) || m_impl->world->QueueDestroy(batch, group->entities[index - 1u]))
                    continue;
                static_cast<void>(m_impl->world->CancelCommandBatch(batch));
                return Result::QueueFailure;
            }
            if (!m_impl->world->SealCommandBatch(batch))
            {
                static_cast<void>(m_impl->world->CancelCommandBatch(batch));
                return Result::QueueFailure;
            }
            if (!m_impl->ValidateObjects(group->objects))
            {
                static_cast<void>(m_impl->world->CancelCommandBatch(batch));
                return Result::InvalidState;
            }
            if (group->referencesPublished && m_impl->references->UnregisterPartition(cellId, generation, group->groupId) != ReferenceResult::Success)
            {
                static_cast<void>(m_impl->world->CancelCommandBatch(batch));
                return Result::InvalidReference;
            }
            group->referencesPublished = false;
            if (!m_impl->DestroyObjects(group->objects))
            {
                static_cast<void>(m_impl->world->CancelCommandBatch(batch));
                return Result::InvalidState;
            }
            group->commandBatch = batch;
            group->state = ActivationGroupState::PendingRelease;
        }
        for (const u64 dependencyGroup : lease->groups)
        {
            Impl::GroupRecord* const group = m_impl->FindGroup(*record, dependencyGroup);
            if (group != nullptr && group->retainCount > 0)
                --group->retainCount;
        }
        for (const u64 removedGroup : removeImmediately)
        {
            Impl::GroupRecord* const group = m_impl->FindGroup(*record, removedGroup);
            static_cast<void>(record->groups.Remove(removedGroup));
            DeleteMaterializerObject(group);
        }
        for (u32 index = 0; index < record->leases.Size(); ++index)
        {
            if (record->leases[index] != lease)
                continue;
            record->leases.RemoveAt(index);
            break;
        }
        DeleteMaterializerObject(lease);
        return Result::Success;
    }

    ActivationGroupState CellMaterializer::GetActivationState(const u64 cellId, const u64 groupId) const noexcept
    {
        if (m_impl == nullptr || cellId == 0)
            return ActivationGroupState::Inactive;
        const Impl::CellRecord* const record = m_impl->Find(cellId);
        if (record == nullptr)
            return ActivationGroupState::Inactive;
        u8 base = 0;
        if (record->baseGroups.Find(groupId, base))
            return ActivationGroupState::Active;
        const Impl::GroupRecord* const group = m_impl->FindGroup(*record, groupId);
        return group != nullptr ? group->state : ActivationGroupState::Inactive;
    }

    u32 CellMaterializer::GetActivationOwnerCount(const u64 cellId, const u64 groupId) const noexcept
    {
        if (m_impl == nullptr || cellId == 0)
            return 0;
        const Impl::CellRecord* const record = m_impl->Find(cellId);
        if (record == nullptr)
            return 0;
        const Impl::GroupRecord* const group = m_impl->FindGroup(*record, groupId);
        if (group != nullptr)
            return group->retainCount;
        u32 owners = 0;
        for (const Impl::GroupLease* const lease : record->leases)
            for (const u64 retainedGroup : lease->groups)
                owners += retainedGroup == groupId;
        return owners;
    }

    CellState CellMaterializer::GetState(const u64 cellId) const noexcept
    {
        if (m_impl == nullptr)
            return CellState::Unknown;
        const Impl::CellRecord* const record = m_impl->Find(cellId);
        if (record == nullptr)
            return CellState::Unknown;
        if (record->state == CellState::Active && record->referencesPublished &&
            !m_impl->references->IsPartitionReady(cellId, record->generation, EntityReferenceRegistry::BasePartition))
            return CellState::PendingReferences;
        return record->state;
    }

    u32 CellMaterializer::GetGeneration(const u64 cellId) const noexcept
    {
        if (m_impl == nullptr)
            return 0;
        const Impl::CellRecord* const record = m_impl->Find(cellId);
        return record != nullptr ? record->generation : 0;
    }

    MaterializerStats CellMaterializer::GetStats() const noexcept
    {
        MaterializerStats stats;
        if (m_impl == nullptr)
            return stats;
        stats.trackedCells = m_impl->lookup.Size();
        stats.queuedEntities = m_impl->queuedEntities;
        stats.activatedEntities = m_impl->activatedEntities;
        stats.releasedEntities = m_impl->releasedEntities;
        stats.cancelledCells = m_impl->cancelledCells;
        for (auto iterator = m_impl->lookup.Begin(), end = m_impl->lookup.End(); iterator != end; ++iterator)
        {
            const Impl::CellRecord* const record = iterator.Value();
            stats.pendingActivationCells += record->state == CellState::PendingActivation;
            const CellState visibleState = GetState(record->cellId);
            stats.pendingReferenceCells += visibleState == CellState::PendingReferences;
            stats.activeCells += visibleState == CellState::Active;
            stats.pendingReleaseCells += record->state == CellState::PendingRelease;
            stats.failedCells += record->state == CellState::Failed;
            stats.trackedActivationGroups += record->groups.Size();
            stats.activationOwners += record->leases.Size();
            for (auto groupIterator = record->groups.Begin(), groupEnd = record->groups.End(); groupIterator != groupEnd; ++groupIterator)
            {
                const ActivationGroupState groupState = groupIterator.Value()->state;
                stats.pendingActivationGroups += groupState == ActivationGroupState::PendingActivation;
                stats.pendingReferenceGroups += groupState == ActivationGroupState::PendingReferences;
                stats.activeActivationGroups += groupState == ActivationGroupState::Active;
                stats.pendingReleaseGroups += groupState == ActivationGroupState::PendingRelease;
                stats.failedActivationGroups += groupState == ActivationGroupState::Failed;
            }
        }
        return stats;
    }
} // namespace vanguard::entities
