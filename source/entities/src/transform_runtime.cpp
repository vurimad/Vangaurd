#include <vanguard/entities/transform_runtime.hpp>

#include <vanguard/entities/hard_attachment.hpp>
#include <vanguard/entities/transform_binding.hpp>

#include <vanguard/concurrency/thread.hpp>
#include <vanguard/ecs/native.hpp>
#include <vanguard/memory/memory.hpp>
#include <vanguard/memory/pool.hpp>

#include <flecs.h>

#include <new>

namespace vanguard::entities
{
    namespace
    {
        template <typename Type, typename... Args> [[nodiscard]] Type* AllocateRuntimeObject(Args&&... args) noexcept
        {
            memory::MemoryBlock block = memory::Allocate(memory::PoolId::World, sizeof(Type), alignof(Type));
            return block ? ::new (block.address) Type(static_cast<Args&&>(args)...) : nullptr;
        }

        template <typename Type> void DeleteRuntimeObject(Type* const object) noexcept
        {
            if (object == nullptr)
                return;
            object->~Type();
            memory::MemoryBlock block{object, sizeof(Type), memory::PoolId::World};
            memory::Free(block);
        }

        void ClearFailure(TransformRuntimeFailure* const failure) noexcept
        {
            if (failure != nullptr)
                *failure = {};
        }

        [[nodiscard]] bool Fail(TransformRuntimeFailure* const failure, const TransformRuntimeFailureCode code, const char* const message,
                                const TransformFailure* const transform = nullptr, const ecs::EntityId entity = ecs::InvalidEntityId,
                                const PlacedComponentBindingHandle binding = {}, IPlacedComponent* const component = nullptr) noexcept
        {
            if (failure != nullptr)
            {
                failure->code = code;
                failure->entity = entity;
                failure->binding = binding;
                failure->component = component;
                failure->message = message;
                if (transform != nullptr)
                    failure->transform = *transform;
            }
            return false;
        }

        [[nodiscard]] math::Vector3 PlacementPosition(const WorldPlacement& placement) noexcept
        {
            return math::Vector3(static_cast<f32>(placement.translation[0]), static_cast<f32>(placement.translation[1]),
                                 static_cast<f32>(placement.translation[2]));
        }

        [[nodiscard]] math::Quaternion PlacementOrientation(const WorldPlacement& placement) noexcept
        {
            return math::Quaternion(placement.rotation);
        }

        [[nodiscard]] bool IsRemoval(const ecs::CommittedChangeKind kind) noexcept
        {
            return kind == ecs::CommittedChangeKind::ComponentRemoved || kind == ecs::CommittedChangeKind::ComponentDisabled;
        }

        [[nodiscard]] constexpr u32 NextGeneration(const u32 generation) noexcept
        {
            const u32 next = generation + 1u;
            return next != 0 ? next : 1u;
        }

        template <typename Component> [[nodiscard]] const Component* ReadComponent(const ecs::World& world, const ecs::EntityId entity,
                                                                                  const ecs::ComponentId component) noexcept
        {
            if (entity == ecs::InvalidEntityId || component == ecs::InvalidComponentId)
                return nullptr;
            const ecs::Entity runtime = world.Resolve(entity);
            return runtime ? static_cast<const Component*>(ecs_get_id(world.GetNative(), runtime.value, component)) : nullptr;
        }
    } // namespace

    struct TransformRuntime::Impl
    {
        struct EntityRecord
        {
            ecs::EntityId entity = ecs::InvalidEntityId;
            PlaceholderComponent* placeholder = nullptr;
            ITransformAttachment* incomingParent = nullptr;
            ecs::EntityId parentEntity = ecs::InvalidEntityId;
            EntityRecord* parentRecord = nullptr;
            EntityRecord* firstChild = nullptr;
            EntityRecord* previousSibling = nullptr;
            EntityRecord* nextSibling = nullptr;
            u32 denseIndex = 0;
            u32 firstPlacedBinding = ~u32{0};
            u64 ownerGeneration = 0;
        };

        struct PlacedBindingSlot
        {
            IPlacedComponent* component = nullptr;
            ITransformAttachment* attachment = nullptr;
            EntityRecord* entity = nullptr;
            u32 generation = 0;
            u32 previousEntityBinding = ~u32{0};
            u32 nextEntityBinding = ~u32{0};
            u32 nextFree = ~u32{0};
            u64 rootGeneration = 0;
            bool active = false;
        };

        explicit Impl(const TransformRuntimeConfig& value) noexcept
            : config(value), records(memory::pools::World::GetInstance()), routes(memory::pools::World::GetInstance()),
              captured(memory::pools::World::GetInstance()), placedBindings(memory::pools::World::GetInstance())
        {
            const u32 initialRecords = value.maximumTrackedEntities < 4096u ? value.maximumTrackedEntities : 4096u;
            const u32 initialBindings = value.maximumPlacedComponents < 4096u ? value.maximumPlacedComponents : 4096u;
            records.Reserve(initialRecords);
            captured.Reserve(value.maximumChangesPerSynchronize);
            placedBindings.Reserve(initialBindings);
        }

        [[nodiscard]] EntityRecord* FindRecord(const ecs::EntityId entity) noexcept
        {
            EntityRecord** const record = routes.FindPtr(entity);
            return record != nullptr ? *record : nullptr;
        }

        [[nodiscard]] const EntityRecord* FindRecord(const ecs::EntityId entity) const noexcept
        {
            EntityRecord* const* const record = routes.FindPtr(entity);
            return record != nullptr ? *record : nullptr;
        }

        [[nodiscard]] PlacedBindingSlot* FindPlacedBinding(const PlacedComponentBindingHandle binding) noexcept
        {
            if (!binding.IsValid() || binding.index >= placedBindings.Size())
                return nullptr;
            PlacedBindingSlot& slot = placedBindings[binding.index];
            return slot.active && slot.generation == binding.generation ? &slot : nullptr;
        }

        [[nodiscard]] const PlacedBindingSlot* FindPlacedBinding(const PlacedComponentBindingHandle binding) const noexcept
        {
            return const_cast<Impl*>(this)->FindPlacedBinding(binding);
        }

        [[nodiscard]] bool AllocatePlacedBinding(u32& index, PlacedBindingSlot*& slot) noexcept
        {
            index = ~u32{0};
            slot = nullptr;
            if (freePlacedBinding != ~u32{0})
            {
                index = freePlacedBinding;
                slot = &placedBindings[index];
                freePlacedBinding = slot->nextFree;
            }
            else
            {
                if (placedBindings.Size() == config.maximumPlacedComponents)
                    return false;
                index = placedBindings.Size();
                placedBindings.PushBack(PlacedBindingSlot{});
                slot = &placedBindings[index];
            }
            const u32 generation = NextGeneration(slot->generation);
            *slot = {};
            slot->generation = generation;
            slot->active = true;
            return true;
        }

        void ReleasePlacedBinding(const u32 index) noexcept
        {
            PlacedBindingSlot& slot = placedBindings[index];
            const u32 generation = slot.generation;
            slot = {};
            slot.generation = generation;
            slot.nextFree = freePlacedBinding;
            freePlacedBinding = index;
        }

        void LinkPlacedBinding(EntityRecord& record, const u32 index) noexcept
        {
            PlacedBindingSlot& slot = placedBindings[index];
            slot.entity = &record;
            slot.previousEntityBinding = ~u32{0};
            slot.nextEntityBinding = record.firstPlacedBinding;
            if (record.firstPlacedBinding != ~u32{0})
                placedBindings[record.firstPlacedBinding].previousEntityBinding = index;
            record.firstPlacedBinding = index;
        }

        void UnlinkPlacedBinding(const u32 index) noexcept
        {
            PlacedBindingSlot& slot = placedBindings[index];
            EntityRecord* const record = slot.entity;
            if (record == nullptr)
                return;
            if (slot.previousEntityBinding != ~u32{0})
                placedBindings[slot.previousEntityBinding].nextEntityBinding = slot.nextEntityBinding;
            else
                record->firstPlacedBinding = slot.nextEntityBinding;
            if (slot.nextEntityBinding != ~u32{0})
                placedBindings[slot.nextEntityBinding].previousEntityBinding = slot.previousEntityBinding;
            slot.entity = nullptr;
            slot.previousEntityBinding = ~u32{0};
            slot.nextEntityBinding = ~u32{0};
        }

        [[nodiscard]] bool DetachPlacedBinding(const u32 index, TransformFailure* const failure = nullptr) noexcept
        {
            PlacedBindingSlot& slot = placedBindings[index];
            if (!slot.active || slot.component == nullptr || slot.attachment == nullptr)
                return false;
            if (!transforms.Detach(*slot.attachment, failure))
                return false;
            hardBinding.DestroyAttachment(slot.attachment);
            slot.attachment = nullptr;
            if (!transforms.UnregisterComponent(*slot.component, failure))
                return false;
            UnlinkPlacedBinding(index);
            ReleasePlacedBinding(index);
            --activePlacedBindings;
            return true;
        }

        [[nodiscard]] bool DetachAllPlacedBindings(EntityRecord& record, TransformFailure* const failure = nullptr) noexcept
        {
            while (record.firstPlacedBinding != ~u32{0})
                if (!DetachPlacedBinding(record.firstPlacedBinding, failure))
                    return false;
            return true;
        }

        [[nodiscard]] EntityRecord* EnsureRecord(const ecs::EntityId entity) noexcept
        {
            if (EntityRecord* const record = FindRecord(entity))
                return record;
            if (entity == ecs::InvalidEntityId || records.Size() == config.maximumTrackedEntities)
                return nullptr;
            EntityRecord* const record = AllocateRuntimeObject<EntityRecord>();
            if (record == nullptr)
                return nullptr;
            record->entity = entity;
            record->denseIndex = records.Size();
            records.PushBack(record);
            if (!routes.Insert(entity, record).IsSuccessful())
            {
                static_cast<void>(records.PopBack());
                DeleteRuntimeObject(record);
                return nullptr;
            }
            return record;
        }

        void UnlinkParent(EntityRecord& child) noexcept
        {
            if (child.parentRecord == nullptr)
                return;
            if (child.previousSibling != nullptr)
                child.previousSibling->nextSibling = child.nextSibling;
            else
                child.parentRecord->firstChild = child.nextSibling;
            if (child.nextSibling != nullptr)
                child.nextSibling->previousSibling = child.previousSibling;
            child.parentRecord = nullptr;
            child.previousSibling = nullptr;
            child.nextSibling = nullptr;
        }

        void LinkParent(EntityRecord& child, EntityRecord& parent) noexcept
        {
            child.parentRecord = &parent;
            child.previousSibling = nullptr;
            child.nextSibling = parent.firstChild;
            if (parent.firstChild != nullptr)
                parent.firstChild->previousSibling = &child;
            parent.firstChild = &child;
        }

        void DiscardEmptyRecord(EntityRecord* const record) noexcept
        {
            if (record == nullptr || record->placeholder != nullptr || record->incomingParent != nullptr ||
                record->parentRecord != nullptr || record->firstChild != nullptr || record->firstPlacedBinding != ~u32{0})
                return;
            if (!routes.Remove(record->entity).IsSuccessful())
                return;
            const u32 index = record->denseIndex;
            EntityRecord* const last = records.Back();
            records[index] = last;
            last->denseIndex = index;
            static_cast<void>(records.PopBack());
            DeleteRuntimeObject(record);
        }

        [[nodiscard]] bool DetachIncoming(EntityRecord& record, TransformFailure* const failure = nullptr) noexcept
        {
            if (record.incomingParent == nullptr)
                return true;
            if (!transforms.Detach(*record.incomingParent, failure))
                return false;
            hardBinding.DestroyAttachment(record.incomingParent);
            record.incomingParent = nullptr;
            return true;
        }

        [[nodiscard]] bool AttachChildToParent(EntityRecord& child, EntityRecord& parent) noexcept
        {
            if (child.incomingParent != nullptr)
                return true;
            if (parent.placeholder == nullptr || child.placeholder == nullptr)
                return true;
            ITransformAttachment* const attachment = hardBinding.CreateAttachment(*parent.placeholder, *child.placeholder);
            if (attachment == nullptr)
                return false;
            attachment->SetCrossEntityBindingFlag(true);
            TransformFailure transformFailure;
            if (!transforms.Attach(*attachment, &transformFailure))
            {
                hardBinding.DestroyAttachment(attachment);
                return false;
            }
            child.incomingParent = attachment;
            return true;
        }

        [[nodiscard]] bool AttachPendingParent(EntityRecord& record) noexcept
        {
            if (record.parentRecord == nullptr || record.placeholder == nullptr || record.incomingParent != nullptr)
                return true;
            return AttachChildToParent(record, *record.parentRecord);
        }

        [[nodiscard]] bool AttachPendingChildren(EntityRecord& parent) noexcept
        {
            for (EntityRecord* child = parent.firstChild; child != nullptr; child = child->nextSibling)
                if (!AttachChildToParent(*child, parent))
                    return false;
            return true;
        }

        [[nodiscard]] u64 NextOwnerGeneration() noexcept
        {
            const u64 generation = nextOwnerGeneration++;
            if (nextOwnerGeneration == 0)
                nextOwnerGeneration = 1;
            return generation;
        }

        [[nodiscard]] bool CreatePlaceholder(EntityRecord& record) noexcept
        {
            if (record.placeholder != nullptr)
                return true;
            PlaceholderComponent* const placeholder = AllocateRuntimeObject<PlaceholderComponent>();
            if (placeholder == nullptr)
                return false;
            if (const WorldPlacement* const placement = ReadComponent<WorldPlacement>(*world, record.entity, placementType.id))
                placeholder->SetInitialPlacement(PlacementPosition(*placement), PlacementOrientation(*placement));

            TransformFailure transformFailure;
            if (!transforms.RegisterComponent(*placeholder, &transformFailure))
            {
                DeleteRuntimeObject(placeholder);
                return false;
            }
            record.placeholder = placeholder;
            record.ownerGeneration = NextOwnerGeneration();
            if (!AttachPendingParent(record) || !AttachPendingChildren(record))
            {
                static_cast<void>(RemovePlaceholder(record, false, false));
                return false;
            }
            if (!world->QueueSetComponent(record.entity, ownerType, TransformOwner{record.ownerGeneration}))
            {
                static_cast<void>(RemovePlaceholder(record, false, false));
                return false;
            }
            ownerDirty = true;
            return true;
        }

        [[nodiscard]] bool ApplyPlacement(EntityRecord& record, const bool removed) noexcept
        {
            const WorldPlacement* const placement = removed ? nullptr : ReadComponent<WorldPlacement>(*world, record.entity, placementType.id);
            if (placement == nullptr)
                return record.placeholder == nullptr || RemovePlaceholder(record, false, true);
            if (record.placeholder == nullptr && !CreatePlaceholder(record))
                return false;
            const math::WorldTransform local(PlacementPosition(*placement), PlacementOrientation(*placement));
            record.placeholder->SetLocalTransform(local);
            return record.placeholder->GetLocalTransformAsWorld() == local;
        }

        [[nodiscard]] bool ApplyParent(EntityRecord& child, const ecs::EntityId requestedParent) noexcept
        {
            const ecs::EntityId parentEntity = requestedParent == child.entity ? ecs::InvalidEntityId : requestedParent;
            if (child.parentEntity == parentEntity)
                return AttachPendingParent(child);

            EntityRecord* parent = nullptr;
            if (parentEntity != ecs::InvalidEntityId)
            {
                parent = EnsureRecord(parentEntity);
                if (parent == nullptr)
                    return false;
            }
            TransformFailure transformFailure;
            if (!DetachIncoming(child, &transformFailure))
                return false;
            EntityRecord* const previousParent = child.parentRecord;
            UnlinkParent(child);
            child.parentEntity = parentEntity;
            if (parent != nullptr)
                LinkParent(child, *parent);
            if (!AttachPendingParent(child))
                return false;
            DiscardEmptyRecord(previousParent);
            return true;
        }

        [[nodiscard]] bool RemovePlaceholder(EntityRecord& record, const bool clearChildrenParents, const bool updateOwnerComponent) noexcept
        {
            for (EntityRecord* child = record.firstChild; child != nullptr;)
            {
                EntityRecord* const next = child->nextSibling;
                TransformFailure transformFailure;
                if (!DetachIncoming(*child, &transformFailure))
                    return false;
                if (clearChildrenParents)
                {
                    UnlinkParent(*child);
                    child->parentEntity = ecs::InvalidEntityId;
                }
                child = next;
            }

            TransformFailure transformFailure;
            if (!DetachIncoming(record, &transformFailure))
                return false;
            if (record.placeholder != nullptr)
            {
                if (!DetachAllPlacedBindings(record, &transformFailure))
                    return false;
                if (!transforms.UnregisterComponent(*record.placeholder, &transformFailure))
                    return false;
                DeleteRuntimeObject(record.placeholder);
                record.placeholder = nullptr;
                record.ownerGeneration = 0;
            }
            if (updateOwnerComponent)
            {
                if (!world->QueueRemoveComponent(record.entity, ownerType))
                    return false;
                ownerDirty = true;
            }
            return true;
        }

        [[nodiscard]] bool DestroyEntityRecord(const ecs::EntityId entity) noexcept
        {
            EntityRecord* const record = FindRecord(entity);
            if (record == nullptr)
                return true;
            if (!RemovePlaceholder(*record, true, false))
                return false;
            EntityRecord* const previousParent = record->parentRecord;
            UnlinkParent(*record);
            if (!routes.Remove(entity).IsSuccessful())
                return false;
            const u32 index = record->denseIndex;
            EntityRecord* const last = records.Back();
            records[index] = last;
            last->denseIndex = index;
            static_cast<void>(records.PopBack());
            DeleteRuntimeObject(record);
            DiscardEmptyRecord(previousParent);
            return true;
        }

        TransformRuntimeConfig config;
        ecs::World* world = nullptr;
        ecs::ComponentType<WorldPlacement> placementType;
        ecs::ComponentType<EntityParent> parentType;
        ecs::ComponentType<TransformOwner> ownerType;
        TransformSystem transforms;
        HardTransformBinding hardBinding;
        ecs::CommittedChangeCursor cursor;
        containers::DynamicArray<EntityRecord*> records;
        containers::HashMap<ecs::EntityId, EntityRecord*> routes;
        containers::DynamicArray<ecs::CommittedChange> captured;
        containers::DynamicArray<PlacedBindingSlot> placedBindings;
        u32 freePlacedBinding = ~u32{0};
        u32 activePlacedBindings = 0;
        u64 nextOwnerGeneration = 1;
        u64 synchronizedChanges = 0;
        u64 lostChanges = 0;
        bool ownerDirty = false;
    };

    TransformRuntime::TransformRuntime(const TransformRuntimeConfig& config) noexcept
        : RuntimeSystem({TransformRuntimeSystemId, "EntityTransforms", game::RuntimeSystemFlags::All}), m_config(config)
    {
    }

    TransformRuntime::~TransformRuntime()
    {
        if (m_impl != nullptr)
            static_cast<void>(ShutdownRuntime(nullptr));
    }

    bool TransformRuntime::InitializeRuntime(ecs::World& world, TransformRuntimeFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl != nullptr)
            return Fail(failure, TransformRuntimeFailureCode::InvalidState, "TransformRuntime is already initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, TransformRuntimeFailureCode::WrongThread, "TransformRuntime must initialize on the main thread");
        const u64 requiredComponents = static_cast<u64>(m_config.maximumTrackedEntities) + m_config.maximumPlacedComponents;
        const u64 requiredAttachments = static_cast<u64>(m_config.maximumTrackedEntities - (m_config.maximumTrackedEntities != 0 ? 1u : 0u)) +
                                        m_config.maximumPlacedComponents;
        if (!world.IsInitialized() || m_config.maximumTrackedEntities == 0 || m_config.maximumPlacedComponents == 0 ||
            m_config.maximumChangesPerSynchronize == 0 || m_config.transformSystem.maximumComponents == 0 ||
            requiredComponents > m_config.transformSystem.maximumComponents || requiredAttachments > m_config.transformSystem.maximumAttachments)
            return Fail(failure, TransformRuntimeFailureCode::InvalidConfiguration, "invalid TransformRuntime configuration");

        Impl* const impl = AllocateRuntimeObject<Impl>(m_config);
        if (impl == nullptr)
            return Fail(failure, TransformRuntimeFailureCode::CapacityExceeded, "TransformRuntime allocation failed");
        impl->world = &world;
        impl->placementType = ecs::RegisterComponent<WorldPlacement>(world);
        impl->parentType = ecs::RegisterComponent<EntityParent>(world);
        impl->ownerType = ecs::RegisterComponent<TransformOwner>(world);
        if (!impl->placementType || !impl->parentType || !impl->ownerType)
        {
            DeleteRuntimeObject(impl);
            return Fail(failure, TransformRuntimeFailureCode::InvalidConfiguration, "failed to register transform ECS component types");
        }

        TransformFailure transformFailure;
        if (!impl->transforms.Initialize(m_config.transformSystem, &transformFailure))
        {
            DeleteRuntimeObject(impl);
            return Fail(failure, TransformRuntimeFailureCode::TransformFailure, "TransformSystem initialization failed", &transformFailure);
        }
        impl->cursor.nextSequence = world.GetNextCommittedChangeSequence();
        m_impl = impl;
        m_readinessBlocker = nullptr;
        return true;
    }

    bool TransformRuntime::ShutdownRuntime(TransformRuntimeFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return true;
        if (!concurrency::IsMainThread())
            return Fail(failure, TransformRuntimeFailureCode::WrongThread, "TransformRuntime must shut down on the main thread");
        if (m_impl->transforms.IsProcessing())
            return Fail(failure, TransformRuntimeFailureCode::Busy, "transform jobs must complete before TransformRuntime shutdown");

        while (!m_impl->records.Empty())
        {
            const ecs::EntityId entity = m_impl->records.Back()->entity;
            if (!m_impl->DestroyEntityRecord(entity))
                return Fail(failure, TransformRuntimeFailureCode::TransformFailure, "failed to destroy a transform entity record");
        }
        if (m_impl->activePlacedBindings != 0)
            return Fail(failure, TransformRuntimeFailureCode::InvalidState, "TransformRuntime retained placed-component bindings after world teardown");
        TransformFailure transformFailure;
        if (!m_impl->transforms.Shutdown(&transformFailure))
            return Fail(failure, TransformRuntimeFailureCode::TransformFailure, "TransformSystem shutdown failed", &transformFailure);
        DeleteRuntimeObject(m_impl);
        m_impl = nullptr;
        return true;
    }

    bool TransformRuntime::Synchronize(TransformRuntimeFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, TransformRuntimeFailureCode::NotInitialized, "TransformRuntime is not initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, TransformRuntimeFailureCode::WrongThread, "TransformRuntime synchronization must run on the main thread");
        if (m_impl->transforms.IsProcessing())
            return Fail(failure, TransformRuntimeFailureCode::Busy, "transform jobs must complete before ECS transform synchronization");

        m_impl->captured.Clear();
        ecs::CommittedChangeReadResult read;
        if (!m_impl->world->ReadCommittedChanges(m_impl->cursor, m_impl->captured, m_impl->config.maximumChangesPerSynchronize, &read))
            return Fail(failure, TransformRuntimeFailureCode::InvalidState, "failed to read ECS committed changes");
        m_impl->synchronizedChanges += read.records;
        if (read.lostRecords != 0)
        {
            m_impl->lostChanges += read.lostRecords;
            return Fail(failure, TransformRuntimeFailureCode::LostChanges,
                        "TransformRuntime lost committed ECS changes and requires reconstruction");
        }

        m_impl->ownerDirty = false;
        for (const ecs::CommittedChange& change : m_impl->captured)
        {
            if (change.kind == ecs::CommittedChangeKind::EntityDestroyed)
            {
                if (!m_impl->DestroyEntityRecord(change.entity))
                    return Fail(failure, TransformRuntimeFailureCode::TransformFailure, "failed to destroy a transform entity record");
                continue;
            }
            if (change.component == m_impl->ownerType.id)
                continue;
            if (change.component == m_impl->placementType.id)
            {
                Impl::EntityRecord* record = m_impl->FindRecord(change.entity);
                const bool removed = IsRemoval(change.kind);
                if (record == nullptr && !removed)
                    record = m_impl->EnsureRecord(change.entity);
                if (record == nullptr)
                {
                    if (removed)
                        continue;
                    return Fail(failure, TransformRuntimeFailureCode::CapacityExceeded, "transform entity tracking capacity exceeded");
                }
                if (!m_impl->ApplyPlacement(*record, removed))
                    return Fail(failure, TransformRuntimeFailureCode::TransformFailure, "failed to mirror entity placement");
            }
            else if (change.component == m_impl->parentType.id)
            {
                Impl::EntityRecord* record = m_impl->FindRecord(change.entity);
                const bool removed = IsRemoval(change.kind);
                if (record == nullptr && !removed)
                    record = m_impl->EnsureRecord(change.entity);
                if (record == nullptr)
                {
                    if (removed)
                        continue;
                    return Fail(failure, TransformRuntimeFailureCode::CapacityExceeded, "transform entity tracking capacity exceeded");
                }
                const EntityParent* const parent = removed ? nullptr : ReadComponent<EntityParent>(*m_impl->world, change.entity, m_impl->parentType.id);
                const ecs::EntityId parentEntity = parent != nullptr ? parent->stableParent : ecs::InvalidEntityId;
                if (!m_impl->ApplyParent(*record, parentEntity))
                    return Fail(failure, TransformRuntimeFailureCode::TransformFailure, "failed to mirror entity parent attachment");
            }
        }

        if (m_impl->ownerDirty && !m_impl->world->FlushComponentActions())
            return Fail(failure, TransformRuntimeFailureCode::InvalidState, "failed to flush transform-owner component changes");
        return true;
    }

    bool TransformRuntime::DispatchUpdates(jobs::Builder& builder, TransformUpdateDispatch& dispatch,
                                           TransformRuntimeFailure* const failure) noexcept
    {
        ClearFailure(failure);
        dispatch = {};
        if (m_impl == nullptr)
            return Fail(failure, TransformRuntimeFailureCode::NotInitialized, "TransformRuntime is not initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, TransformRuntimeFailureCode::WrongThread, "transform dispatch must run on the main thread");
        if (m_impl->transforms.IsProcessing())
            return Fail(failure, TransformRuntimeFailureCode::Busy, "previous transform dispatch is still processing");

        TransformFailure transformFailure;
        if (!m_impl->transforms.DispatchUpdates(builder, dispatch, &transformFailure))
            return Fail(failure, TransformRuntimeFailureCode::TransformFailure, "TransformSystem dispatch failed", &transformFailure);
        return true;
    }

    bool TransformRuntime::AttachPlacedComponent(const ecs::EntityId entity, IPlacedComponent& component,
                                                 PlacedComponentBindingHandle& binding, TransformRuntimeFailure* const failure) noexcept
    {
        ClearFailure(failure);
        binding = {};
        if (m_impl == nullptr)
            return Fail(failure, TransformRuntimeFailureCode::NotInitialized, "TransformRuntime is not initialized", nullptr, entity, {}, &component);
        if (!concurrency::IsMainThread())
            return Fail(failure, TransformRuntimeFailureCode::WrongThread, "placed components must attach on the main thread", nullptr, entity, {},
                        &component);
        if (m_impl->transforms.IsProcessing())
            return Fail(failure, TransformRuntimeFailureCode::Busy, "placed components cannot attach during transform processing", nullptr, entity, {},
                        &component);
        Impl::EntityRecord* const record = m_impl->FindRecord(entity);
        if (record == nullptr || record->placeholder == nullptr || record->ownerGeneration == 0 || !m_impl->world->Resolve(entity))
            return Fail(failure, TransformRuntimeFailureCode::InvalidHandle, "placed component requires a live entity transform root", nullptr, entity, {},
                        &component);
        if (component.IsRegistered())
            return Fail(failure, TransformRuntimeFailureCode::InvalidState, "placed component is already registered", nullptr, entity, {}, &component);

        u32 slotIndex = ~u32{0};
        Impl::PlacedBindingSlot* slot = nullptr;
        if (!m_impl->AllocatePlacedBinding(slotIndex, slot))
            return Fail(failure, TransformRuntimeFailureCode::CapacityExceeded, "maximum runtime placed-component count exceeded", nullptr, entity, {},
                        &component);

        TransformFailure transformFailure;
        if (!m_impl->transforms.RegisterComponent(component, &transformFailure))
        {
            m_impl->ReleasePlacedBinding(slotIndex);
            return Fail(failure, TransformRuntimeFailureCode::TransformFailure, "placed-component registration failed", &transformFailure, entity, {},
                        &component);
        }

        ITransformAttachment* const attachment = m_impl->hardBinding.CreateAttachment(*record->placeholder, component);
        if (attachment == nullptr)
        {
            TransformFailure rollbackFailure;
            if (!m_impl->transforms.UnregisterComponent(component, &rollbackFailure))
            {
                m_readinessBlocker = "placed-component registration rollback failed";
                m_impl->ReleasePlacedBinding(slotIndex);
                return Fail(failure, TransformRuntimeFailureCode::TransformFailure, m_readinessBlocker, &rollbackFailure, entity, {}, &component);
            }
            m_impl->ReleasePlacedBinding(slotIndex);
            return Fail(failure, TransformRuntimeFailureCode::CapacityExceeded, "placed-component hard attachment allocation failed", nullptr, entity, {},
                        &component);
        }
        attachment->SetFloatingBinding(true);
        if (!m_impl->transforms.Attach(*attachment, &transformFailure))
        {
            m_impl->hardBinding.DestroyAttachment(attachment);
            TransformFailure rollbackFailure;
            if (!m_impl->transforms.UnregisterComponent(component, &rollbackFailure))
            {
                m_readinessBlocker = "placed-component attachment rollback failed";
                m_impl->ReleasePlacedBinding(slotIndex);
                return Fail(failure, TransformRuntimeFailureCode::TransformFailure, m_readinessBlocker, &rollbackFailure, entity, {}, &component);
            }
            m_impl->ReleasePlacedBinding(slotIndex);
            return Fail(failure, TransformRuntimeFailureCode::TransformFailure, "placed-component hard attachment failed", &transformFailure, entity, {},
                        &component);
        }

        slot->component = &component;
        slot->attachment = attachment;
        slot->rootGeneration = record->ownerGeneration;
        m_impl->LinkPlacedBinding(*record, slotIndex);
        ++m_impl->activePlacedBindings;
        binding = {slotIndex, slot->generation};
        return true;
    }

    bool TransformRuntime::DetachPlacedComponent(const PlacedComponentBindingHandle binding, TransformRuntimeFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, TransformRuntimeFailureCode::NotInitialized, "TransformRuntime is not initialized", nullptr, ecs::InvalidEntityId,
                        binding);
        if (!concurrency::IsMainThread())
            return Fail(failure, TransformRuntimeFailureCode::WrongThread, "placed components must detach on the main thread", nullptr,
                        ecs::InvalidEntityId, binding);
        if (m_impl->transforms.IsProcessing())
            return Fail(failure, TransformRuntimeFailureCode::Busy, "placed components cannot detach during transform processing", nullptr,
                        ecs::InvalidEntityId, binding);
        Impl::PlacedBindingSlot* const slot = m_impl->FindPlacedBinding(binding);
        if (slot == nullptr)
            return Fail(failure, TransformRuntimeFailureCode::InvalidHandle, "invalid or stale placed-component binding", nullptr, ecs::InvalidEntityId,
                        binding);
        IPlacedComponent* const component = slot->component;
        const ecs::EntityId entity = slot->entity != nullptr ? slot->entity->entity : ecs::InvalidEntityId;
        TransformFailure transformFailure;
        if (!m_impl->DetachPlacedBinding(binding.index, &transformFailure))
            return Fail(failure, TransformRuntimeFailureCode::TransformFailure, "placed-component detachment failed", &transformFailure, entity, binding,
                        component);
        return true;
    }

    bool TransformRuntime::GetPlacedComponentBinding(const PlacedComponentBindingHandle binding,
                                                     PlacedComponentBindingSnapshot& snapshot) const noexcept
    {
        snapshot = {};
        const Impl::PlacedBindingSlot* const slot = m_impl != nullptr ? m_impl->FindPlacedBinding(binding) : nullptr;
        if (slot == nullptr || slot->entity == nullptr || slot->component == nullptr || slot->attachment == nullptr)
            return false;
        snapshot.handle = binding;
        snapshot.entity = slot->entity->entity;
        snapshot.rootGeneration = slot->rootGeneration;
        snapshot.component = slot->component;
        snapshot.attached = slot->attachment->IsAttached();
        return snapshot.attached;
    }

    PlaceholderComponent* TransformRuntime::GetRoot(const ecs::EntityId entity) noexcept
    {
        if (m_impl == nullptr || m_impl->transforms.IsProcessing())
            return nullptr;
        Impl::EntityRecord* const record = m_impl->FindRecord(entity);
        return record != nullptr ? record->placeholder : nullptr;
    }

    const PlaceholderComponent* TransformRuntime::GetRoot(const ecs::EntityId entity) const noexcept
    {
        return const_cast<TransformRuntime*>(this)->GetRoot(entity);
    }

    TransformRuntimeStats TransformRuntime::GetStats() const noexcept
    {
        TransformRuntimeStats result;
        if (m_impl == nullptr)
            return result;
        result.trackedEntities = m_impl->records.Size();
        result.placedComponents = m_impl->activePlacedBindings;
        result.synchronizedChanges = m_impl->synchronizedChanges;
        result.lostChanges = m_impl->lostChanges;
        result.transformSystem = m_impl->transforms.GetStats();
        result.initialized = true;
        result.failed = m_readinessBlocker != nullptr;
        for (const Impl::EntityRecord* const record : m_impl->records)
        {
            if (record->placeholder != nullptr)
                ++result.placeholders;
            if (record->parentEntity != ecs::InvalidEntityId && record->incomingParent == nullptr)
                ++result.pendingParents;
        }
        return result;
    }

    bool TransformRuntime::OnInitialize(game::GameWorld& world) noexcept
    {
        TransformRuntimeFailure failure;
        if (InitializeRuntime(world.GetEntities(), &failure))
            return true;
        m_readinessBlocker = failure.message != nullptr ? failure.message : "TransformRuntime initialization failed";
        return false;
    }

    void TransformRuntime::OnUninitialize(game::GameWorld&) noexcept
    {
        TransformRuntimeFailure failure;
        if (!ShutdownRuntime(&failure))
            m_readinessBlocker = failure.message != nullptr ? failure.message : "TransformRuntime shutdown failed";
    }

    void TransformRuntime::OnAfterWorldFlush(game::GameWorld&) noexcept
    {
        if (m_readinessBlocker != nullptr)
            return;
        TransformRuntimeFailure failure;
        if (!Synchronize(&failure))
            m_readinessBlocker = failure.message != nullptr ? failure.message : "TransformRuntime synchronization failed";
    }

    const char* TransformRuntime::ReadinessBlocker() const noexcept
    {
        return m_readinessBlocker;
    }
} // namespace vanguard::entities
