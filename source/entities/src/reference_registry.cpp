#include <vanguard/entities/reference_registry.hpp>

#include <vanguard/concurrency/synchronization.hpp>
#include <vanguard/memory/pool.hpp>

#include <new>

namespace
{
    using namespace vanguard;

    template <typename Type, typename... Args> [[nodiscard]] Type* AllocateReferenceObject(Args&&... args) noexcept
    {
        memory::MemoryBlock block = memory::Allocate(memory::PoolId::World, sizeof(Type), alignof(Type));
        return block ? ::new (block.address) Type(static_cast<Args&&>(args)...) : nullptr;
    }

    template <typename Type> void DeleteReferenceObject(Type* const object) noexcept
    {
        if (object == nullptr)
            return;
        object->~Type();
        memory::MemoryBlock block{object, sizeof(Type), memory::PoolId::World};
        memory::Free(block);
    }

    [[nodiscard]] bool IsRequired(const world::EntityReferenceKind kind) noexcept
    {
        return kind == world::EntityReferenceKind::RequiredLocal || kind == world::EntityReferenceKind::RequiredWorld;
    }

    struct ReferenceSlotKey
    {
        ecs::EntityId source = ecs::InvalidEntityId;
        u64 slot = 0;

        [[nodiscard]] u32 CalcHash() const noexcept
        {
            u64 value = source;
            value ^= slot + 0x9e3779b97f4a7c15ull + (value << 6u) + (value >> 2u);
            return static_cast<u32>(value ^ (value >> 32u));
        }

        [[nodiscard]] friend constexpr bool operator==(const ReferenceSlotKey&, const ReferenceSlotKey&) noexcept = default;
    };

} // namespace

namespace vanguard::entities
{
    struct EntityReferenceRegistry::Impl
    {
        struct Binding
        {
            ecs::EntityId source = ecs::InvalidEntityId;
            u64 slot = 0;
            ecs::EntityId target = ecs::InvalidEntityId;
            world::EntityReferenceKind kind = world::EntityReferenceKind::RequiredLocal;
            ecs::Entity resolved;
            u64 revision = 0;
        };

        struct SourceRecord
        {
            SourceRecord() noexcept : bindings(memory::pools::World::GetInstance()) {}
            u64 cellId = 0;
            u64 partitionId = 0;
            u32 generation = 0;
            ecs::EntityId entity = ecs::InvalidEntityId;
            containers::DynamicArray<Binding> bindings;
        };

        struct TargetRecord
        {
            TargetRecord() noexcept : incoming(memory::pools::World::GetInstance()) {}
            containers::DynamicArray<Binding*> incoming;
        };

        struct PartitionRecord
        {
            PartitionRecord() noexcept : entities(memory::pools::World::GetInstance()), sources(memory::pools::World::GetInstance()) {}
            u64 partitionId = 0;
            u32 unresolvedRequired = 0;
            containers::DynamicArray<ecs::EntityId> entities;
            containers::DynamicArray<SourceRecord*> sources;
        };

        struct CellRecord
        {
            CellRecord() noexcept : partitions(memory::pools::World::GetInstance()) {}
            u64 cellId = 0;
            u32 generation = 0;
            u32 unresolvedRequired = 0;
            containers::HashMap<u64, PartitionRecord*> partitions;
        };

        struct LiveEntity
        {
            u64 cellId = 0;
            u64 partitionId = 0;
            u32 generation = 0;
            ecs::Entity entity;
        };

        explicit Impl(ecs::World& value) noexcept
            : world(&value), cells(memory::pools::World::GetInstance()), live(memory::pools::World::GetInstance()),
              sources(memory::pools::World::GetInstance()), targets(memory::pools::World::GetInstance())
        {
            cells.Reserve(256);
            live.Reserve(65536);
            sources.Reserve(32768);
            targets.Reserve(32768);
        }

        [[nodiscard]] CellRecord* FindCell(const u64 cellId) const noexcept
        {
            CellRecord* cell = nullptr;
            return cells.Find(cellId, cell) ? cell : nullptr;
        }

        [[nodiscard]] PartitionRecord* FindPartition(CellRecord& cell, const u64 partitionId) const noexcept
        {
            PartitionRecord* partition = nullptr;
            return cell.partitions.Find(partitionId, partition) ? partition : nullptr;
        }

        void ChangeResolution(Binding& binding, const ecs::Entity value) noexcept
        {
            const bool wasResolved = static_cast<bool>(binding.resolved);
            const bool isResolved = static_cast<bool>(value);
            if (wasResolved == isResolved && (!isResolved || binding.resolved == value))
                return;
            SourceRecord* source = nullptr;
            static_cast<void>(sources.Find(binding.source, source));
            CellRecord* const owner = source != nullptr ? FindCell(source->cellId) : nullptr;
            if (owner != nullptr && IsRequired(binding.kind))
            {
                if (wasResolved && !isResolved)
                    ++owner->unresolvedRequired;
                if (!wasResolved && isResolved && owner->unresolvedRequired > 0)
                    --owner->unresolvedRequired;
                PartitionRecord* const partition = FindPartition(*owner, source->partitionId);
                if (partition != nullptr)
                {
                    if (wasResolved && !isResolved)
                        ++partition->unresolvedRequired;
                    if (!wasResolved && isResolved && partition->unresolvedRequired > 0)
                        --partition->unresolvedRequired;
                }
            }
            binding.resolved = value;
            binding.revision = ++resolutionRevision;
        }

        void RemoveIncoming(TargetRecord& target, const Binding* const binding) noexcept
        {
            for (u32 index = 0; index < target.incoming.Size(); ++index)
            {
                if (target.incoming[index] != binding)
                    continue;
                target.incoming.RemoveAt(index);
                return;
            }
        }

        ecs::World* world = nullptr;
        mutable concurrency::RWSpinLock lock;
        containers::HashMap<u64, CellRecord*> cells;
        containers::HashMap<ecs::EntityId, LiveEntity> live;
        containers::HashMap<ecs::EntityId, SourceRecord*> sources;
        containers::HashMap<ecs::EntityId, TargetRecord*> targets;
        u64 resolutionRevision = 0;
        u64 registrationEntitiesVisited = 0;
        u64 registrationReferencesVisited = 0;
        u64 registrationSlotLookups = 0;
    };

    namespace
    {
        [[nodiscard]] ReferenceResult RemovePartition(EntityReferenceRegistry::Impl& impl, EntityReferenceRegistry::Impl::CellRecord& cell,
                                                      const u64 partitionId) noexcept
        {
            EntityReferenceRegistry::Impl::PartitionRecord* const partition = impl.FindPartition(cell, partitionId);
            if (partition == nullptr)
                return ReferenceResult::InvalidState;
            for (EntityReferenceRegistry::Impl::SourceRecord* const source : partition->sources)
            {
                for (EntityReferenceRegistry::Impl::Binding& binding : source->bindings)
                {
                    EntityReferenceRegistry::Impl::TargetRecord* target = nullptr;
                    if (!impl.targets.Find(binding.target, target))
                        continue;
                    impl.RemoveIncoming(*target, &binding);
                    if (target->incoming.Empty())
                    {
                        static_cast<void>(impl.targets.Remove(binding.target));
                        DeleteReferenceObject(target);
                    }
                }
                static_cast<void>(impl.sources.Remove(source->entity));
            }
            for (const ecs::EntityId identity : partition->entities)
            {
                EntityReferenceRegistry::Impl::TargetRecord* target = nullptr;
                if (impl.targets.Find(identity, target))
                    for (EntityReferenceRegistry::Impl::Binding* const incoming : target->incoming)
                        impl.ChangeResolution(*incoming, {});
                static_cast<void>(impl.live.Remove(identity));
            }
            if (cell.unresolvedRequired >= partition->unresolvedRequired)
                cell.unresolvedRequired -= partition->unresolvedRequired;
            else
                cell.unresolvedRequired = 0;
            static_cast<void>(cell.partitions.Remove(partitionId));
            for (EntityReferenceRegistry::Impl::SourceRecord* const source : partition->sources)
                DeleteReferenceObject(source);
            DeleteReferenceObject(partition);
            return ReferenceResult::Success;
        }
    } // namespace

    EntityReferenceRegistry::~EntityReferenceRegistry()
    {
        if (m_impl == nullptr)
            return;
        for (auto iterator = m_impl->cells.Begin(), end = m_impl->cells.End(); iterator != end; ++iterator)
        {
            Impl::CellRecord* const cell = iterator.Value();
            for (auto partitionIterator = cell->partitions.Begin(), partitionEnd = cell->partitions.End(); partitionIterator != partitionEnd;
                 ++partitionIterator)
            {
                Impl::PartitionRecord* const partition = partitionIterator.Value();
                for (Impl::SourceRecord* const source : partition->sources)
                    DeleteReferenceObject(source);
                DeleteReferenceObject(partition);
            }
            DeleteReferenceObject(cell);
        }
        for (auto iterator = m_impl->targets.Begin(), end = m_impl->targets.End(); iterator != end; ++iterator)
            DeleteReferenceObject(iterator.Value());
        DeleteReferenceObject(m_impl);
        m_impl = nullptr;
    }

    bool EntityReferenceRegistry::Initialize(ecs::World& world) noexcept
    {
        if (m_impl != nullptr || !world.IsInitialized())
            return false;
        m_impl = AllocateReferenceObject<Impl>(world);
        return m_impl != nullptr;
    }

    bool EntityReferenceRegistry::Shutdown() noexcept
    {
        if (m_impl == nullptr)
            return true;
        if (!m_impl->cells.Empty() || !m_impl->live.Empty() || !m_impl->sources.Empty() || !m_impl->targets.Empty())
            return false;
        Impl* const impl = m_impl;
        m_impl = nullptr;
        DeleteReferenceObject(impl);
        return true;
    }

    bool EntityReferenceRegistry::IsInitialized() const noexcept
    {
        return m_impl != nullptr;
    }

    ReferenceResult EntityReferenceRegistry::RegisterCell(const u64 cellId, const u32 generation, const containers::ArraySpan<const ecs::EntityId> entities,
                                                          const containers::ArraySpan<const world::EntityReferenceRecord> references) noexcept
    {
        return RegisterPartition(cellId, generation, BasePartition, entities, references);
    }

    ReferenceResult EntityReferenceRegistry::RegisterPartition(const u64 cellId, const u32 generation, const u64 partitionId,
                                                               const containers::ArraySpan<const ecs::EntityId> entities,
                                                               const containers::ArraySpan<const world::EntityReferenceRecord> references) noexcept
    {
        if (m_impl == nullptr || cellId == 0 || generation == 0 || entities.Empty())
            return ReferenceResult::InvalidArgument;
        VG_SCOPE_LOCK(m_impl->lock);
        Impl::CellRecord* cell = m_impl->FindCell(cellId);
        const bool newCell = cell == nullptr;
        if (!newCell && cell->generation != generation)
            return ReferenceResult::StaleGeneration;
        if (!newCell && m_impl->FindPartition(*cell, partitionId) != nullptr)
            return ReferenceResult::DuplicateCell;
        if (newCell)
        {
            cell = AllocateReferenceObject<Impl::CellRecord>();
            if (cell == nullptr)
                return ReferenceResult::InvalidState;
            cell->cellId = cellId;
            cell->generation = generation;
        }
        Impl::PartitionRecord* const partition = AllocateReferenceObject<Impl::PartitionRecord>();
        if (partition == nullptr)
        {
            if (newCell)
                DeleteReferenceObject(cell);
            return ReferenceResult::InvalidState;
        }
        partition->partitionId = partitionId;
        partition->entities.Reserve(entities.Size());
        containers::HashMap<ecs::EntityId, u8> partitionEntitySet(memory::pools::World::GetInstance());
        partitionEntitySet.Reserve(entities.Size());
        for (const ecs::EntityId identity : entities)
        {
            ++m_impl->registrationEntitiesVisited;
            Impl::LiveEntity existing;
            u8 present = 0;
            if (identity == ecs::InvalidEntityId || !m_impl->world->Resolve(identity) || m_impl->live.Find(identity, existing) ||
                partitionEntitySet.Find(identity, present))
            {
                DeleteReferenceObject(partition);
                if (newCell)
                    DeleteReferenceObject(cell);
                return ReferenceResult::DuplicateEntity;
            }
            if (!partitionEntitySet.Insert(identity, 1u).IsSuccessful())
            {
                DeleteReferenceObject(partition);
                if (newCell)
                    DeleteReferenceObject(cell);
                return ReferenceResult::InvalidState;
            }
            partition->entities.PushBack(identity);
        }

        containers::HashMap<ecs::EntityId, Impl::SourceRecord*> preparedSources(memory::pools::World::GetInstance());
        containers::HashSet<ReferenceSlotKey> preparedSlots(memory::pools::World::GetInstance());
        preparedSources.Reserve(entities.Size() < references.Size() ? entities.Size() : references.Size());
        preparedSlots.Reserve(references.Size());
        for (const world::EntityReferenceRecord& reference : references)
        {
            ++m_impl->registrationReferencesVisited;
            u8 sourcePresent = 0;
            if (!partitionEntitySet.Find(reference.sourceEntityId, sourcePresent))
                continue;
            if (reference.slot == 0 || reference.targetEntityId == ecs::InvalidEntityId)
            {
                for (Impl::SourceRecord* const source : partition->sources)
                    DeleteReferenceObject(source);
                DeleteReferenceObject(partition);
                if (newCell)
                    DeleteReferenceObject(cell);
                return ReferenceResult::InvalidArgument;
            }
            Impl::SourceRecord* source = nullptr;
            static_cast<void>(preparedSources.Find(reference.sourceEntityId, source));
            if (source == nullptr)
            {
                source = AllocateReferenceObject<Impl::SourceRecord>();
                if (source == nullptr)
                {
                    for (Impl::SourceRecord* const prepared : partition->sources)
                        DeleteReferenceObject(prepared);
                    DeleteReferenceObject(partition);
                    if (newCell)
                        DeleteReferenceObject(cell);
                    return ReferenceResult::InvalidState;
                }
                source->cellId = cellId;
                source->partitionId = partitionId;
                source->generation = generation;
                source->entity = reference.sourceEntityId;
                partition->sources.PushBack(source);
                if (!preparedSources.Insert(reference.sourceEntityId, source).IsSuccessful())
                {
                    for (Impl::SourceRecord* const prepared : partition->sources)
                        DeleteReferenceObject(prepared);
                    DeleteReferenceObject(partition);
                    if (newCell)
                        DeleteReferenceObject(cell);
                    return ReferenceResult::InvalidState;
                }
            }
            const ReferenceSlotKey slotKey{reference.sourceEntityId, reference.slot};
            ++m_impl->registrationSlotLookups;
            if (preparedSlots.Exist(slotKey))
            {
                for (Impl::SourceRecord* const prepared : partition->sources)
                    DeleteReferenceObject(prepared);
                DeleteReferenceObject(partition);
                if (newCell)
                    DeleteReferenceObject(cell);
                return ReferenceResult::DuplicateSlot;
            }
            if (!preparedSlots.Insert(slotKey).IsSuccessful())
            {
                for (Impl::SourceRecord* const prepared : partition->sources)
                    DeleteReferenceObject(prepared);
                DeleteReferenceObject(partition);
                if (newCell)
                    DeleteReferenceObject(cell);
                return ReferenceResult::InvalidState;
            }
            source->bindings.PushBack({reference.sourceEntityId, reference.slot, reference.targetEntityId, reference.kind, {}, 0});
        }

        containers::HashMap<ecs::EntityId, u32> incomingCounts(memory::pools::World::GetInstance());
        for (Impl::SourceRecord* const source : partition->sources)
        {
            for (const Impl::Binding& binding : source->bindings)
            {
                u32 count = 0;
                static_cast<void>(incomingCounts.Find(binding.target, count));
                if (!incomingCounts.Set(binding.target, count + 1u).IsSuccessful())
                {
                    for (Impl::SourceRecord* const prepared : partition->sources)
                        DeleteReferenceObject(prepared);
                    DeleteReferenceObject(partition);
                    if (newCell)
                        DeleteReferenceObject(cell);
                    return ReferenceResult::InvalidState;
                }
            }
        }
        containers::DynamicArray<ecs::EntityId> preparedTargetIds(memory::pools::World::GetInstance());
        containers::DynamicArray<Impl::TargetRecord*> preparedTargets(memory::pools::World::GetInstance());
        preparedTargetIds.Reserve(incomingCounts.Size());
        preparedTargets.Reserve(incomingCounts.Size());
        for (auto iterator = incomingCounts.Begin(), end = incomingCounts.End(); iterator != end; ++iterator)
        {
            Impl::TargetRecord* target = nullptr;
            if (m_impl->targets.Find(iterator.Key(), target))
            {
                target->incoming.Reserve(target->incoming.Size() + iterator.Value());
                continue;
            }
            target = AllocateReferenceObject<Impl::TargetRecord>();
            if (target == nullptr)
            {
                for (Impl::TargetRecord* const prepared : preparedTargets)
                    DeleteReferenceObject(prepared);
                for (Impl::SourceRecord* const prepared : partition->sources)
                    DeleteReferenceObject(prepared);
                DeleteReferenceObject(partition);
                if (newCell)
                    DeleteReferenceObject(cell);
                return ReferenceResult::InvalidState;
            }
            target->incoming.Reserve(iterator.Value());
            preparedTargetIds.PushBack(iterator.Key());
            preparedTargets.PushBack(target);
        }
        if (newCell)
            m_impl->cells.Reserve(m_impl->cells.Size() + 1u);
        cell->partitions.Reserve(cell->partitions.Size() + 1u);
        m_impl->live.Reserve(m_impl->live.Size() + partition->entities.Size());
        m_impl->sources.Reserve(m_impl->sources.Size() + partition->sources.Size());
        m_impl->targets.Reserve(m_impl->targets.Size() + preparedTargets.Size());
        u32 insertedTargets = 0;
        for (; insertedTargets < preparedTargets.Size(); ++insertedTargets)
        {
            if (m_impl->targets.Insert(preparedTargetIds[insertedTargets], preparedTargets[insertedTargets]).IsSuccessful())
                continue;
            for (u32 index = 0; index < insertedTargets; ++index)
                static_cast<void>(m_impl->targets.Remove(preparedTargetIds[index]));
            for (Impl::TargetRecord* const prepared : preparedTargets)
                DeleteReferenceObject(prepared);
            for (Impl::SourceRecord* const prepared : partition->sources)
                DeleteReferenceObject(prepared);
            DeleteReferenceObject(partition);
            if (newCell)
                DeleteReferenceObject(cell);
            return ReferenceResult::InvalidState;
        }
        if (newCell && !m_impl->cells.Insert(cellId, cell).IsSuccessful())
        {
            for (u32 index = 0; index < preparedTargets.Size(); ++index)
                static_cast<void>(m_impl->targets.Remove(preparedTargetIds[index]));
            for (Impl::TargetRecord* const prepared : preparedTargets)
                DeleteReferenceObject(prepared);
            for (Impl::SourceRecord* const source : partition->sources)
                DeleteReferenceObject(source);
            DeleteReferenceObject(partition);
            DeleteReferenceObject(cell);
            return ReferenceResult::InvalidState;
        }
        if (!cell->partitions.Insert(partitionId, partition).IsSuccessful())
        {
            if (newCell)
                static_cast<void>(m_impl->cells.Remove(cellId));
            for (u32 index = 0; index < preparedTargets.Size(); ++index)
                static_cast<void>(m_impl->targets.Remove(preparedTargetIds[index]));
            for (Impl::TargetRecord* const prepared : preparedTargets)
                DeleteReferenceObject(prepared);
            for (Impl::SourceRecord* const source : partition->sources)
                DeleteReferenceObject(source);
            DeleteReferenceObject(partition);
            if (newCell)
                DeleteReferenceObject(cell);
            return ReferenceResult::InvalidState;
        }
        for (const ecs::EntityId identity : partition->entities)
        {
            const ecs::Entity entity = m_impl->world->Resolve(identity);
            static_cast<void>(m_impl->live.Insert(identity, {cellId, partitionId, generation, entity}));
            Impl::TargetRecord* target = nullptr;
            if (!m_impl->targets.Find(identity, target))
                continue;
            for (Impl::Binding* const incoming : target->incoming)
                m_impl->ChangeResolution(*incoming, entity);
        }
        for (Impl::SourceRecord* const source : partition->sources)
        {
            static_cast<void>(m_impl->sources.Insert(source->entity, source));
            for (Impl::Binding& binding : source->bindings)
            {
                Impl::TargetRecord* target = nullptr;
                if (!m_impl->targets.Find(binding.target, target))
                    return ReferenceResult::InvalidState;
                target->incoming.PushBack(&binding);
                Impl::LiveEntity liveTarget;
                if (m_impl->live.Find(binding.target, liveTarget))
                {
                    binding.resolved = liveTarget.entity;
                    binding.revision = ++m_impl->resolutionRevision;
                }
                else
                {
                    if (IsRequired(binding.kind))
                    {
                        ++cell->unresolvedRequired;
                        ++partition->unresolvedRequired;
                    }
                    binding.revision = ++m_impl->resolutionRevision;
                }
            }
        }
        return ReferenceResult::Success;
    }

    ReferenceResult EntityReferenceRegistry::UnregisterCell(const u64 cellId, const u32 generation) noexcept
    {
        if (m_impl == nullptr || cellId == 0 || generation == 0)
            return ReferenceResult::InvalidArgument;
        VG_SCOPE_LOCK(m_impl->lock);
        Impl::CellRecord* const cell = m_impl->FindCell(cellId);
        if (cell == nullptr)
            return ReferenceResult::InvalidState;
        if (cell->generation != generation)
            return ReferenceResult::StaleGeneration;

        while (!cell->partitions.Empty())
        {
            const u64 partitionId = cell->partitions.Begin().Key();
            const ReferenceResult result = RemovePartition(*m_impl, *cell, partitionId);
            if (result != ReferenceResult::Success)
                return result;
        }
        static_cast<void>(m_impl->cells.Remove(cellId));
        DeleteReferenceObject(cell);
        return ReferenceResult::Success;
    }

    ReferenceResult EntityReferenceRegistry::UnregisterPartition(const u64 cellId, const u32 generation, const u64 partitionId) noexcept
    {
        if (m_impl == nullptr || cellId == 0 || generation == 0)
            return ReferenceResult::InvalidArgument;
        VG_SCOPE_LOCK(m_impl->lock);
        Impl::CellRecord* const cell = m_impl->FindCell(cellId);
        if (cell == nullptr)
            return ReferenceResult::InvalidState;
        if (cell->generation != generation)
            return ReferenceResult::StaleGeneration;
        const ReferenceResult result = RemovePartition(*m_impl, *cell, partitionId);
        if (result != ReferenceResult::Success)
            return result;
        if (cell->partitions.Empty())
        {
            static_cast<void>(m_impl->cells.Remove(cellId));
            DeleteReferenceObject(cell);
        }
        return ReferenceResult::Success;
    }

    ResolvedEntityReference EntityReferenceRegistry::Resolve(const ecs::EntityId sourceEntityId, const u64 slot) const noexcept
    {
        ResolvedEntityReference result;
        result.sourceEntityId = sourceEntityId;
        result.slot = slot;
        if (m_impl == nullptr || sourceEntityId == ecs::InvalidEntityId || slot == 0)
            return result;
        VG_SCOPE_SHARED_LOCK(m_impl->lock);
        Impl::SourceRecord* source = nullptr;
        if (!m_impl->sources.Find(sourceEntityId, source))
            return result;
        for (const Impl::Binding& binding : source->bindings)
        {
            if (binding.slot != slot)
                continue;
            result.targetEntityId = binding.target;
            result.kind = binding.kind;
            result.entity = binding.resolved;
            result.revision = binding.revision;
            result.state = binding.resolved ? ReferenceState::Resolved : ReferenceState::Unresolved;
            return result;
        }
        return result;
    }

    bool EntityReferenceRegistry::IsCellReady(const u64 cellId, const u32 generation) const noexcept
    {
        if (m_impl == nullptr || cellId == 0 || generation == 0)
            return false;
        VG_SCOPE_SHARED_LOCK(m_impl->lock);
        const Impl::CellRecord* const cell = m_impl->FindCell(cellId);
        return cell != nullptr && cell->generation == generation && cell->unresolvedRequired == 0;
    }

    bool EntityReferenceRegistry::IsPartitionReady(const u64 cellId, const u32 generation, const u64 partitionId) const noexcept
    {
        if (m_impl == nullptr || cellId == 0 || generation == 0)
            return false;
        VG_SCOPE_SHARED_LOCK(m_impl->lock);
        Impl::CellRecord* const cell = m_impl->FindCell(cellId);
        if (cell == nullptr || cell->generation != generation)
            return false;
        Impl::PartitionRecord* const partition = m_impl->FindPartition(*cell, partitionId);
        return partition != nullptr && partition->unresolvedRequired == 0;
    }

    bool EntityReferenceRegistry::HasEntity(const ecs::EntityId entityId) const noexcept
    {
        if (m_impl == nullptr || entityId == ecs::InvalidEntityId)
            return false;
        VG_SCOPE_SHARED_LOCK(m_impl->lock);
        Impl::LiveEntity value;
        return m_impl->live.Find(entityId, value);
    }

    bool EntityReferenceRegistry::HasEntityInPartition(const ecs::EntityId entityId, const u64 cellId, const u32 generation,
                                                       const u64 partitionId) const noexcept
    {
        if (m_impl == nullptr || entityId == ecs::InvalidEntityId || cellId == 0 || generation == 0)
            return false;
        VG_SCOPE_SHARED_LOCK(m_impl->lock);
        Impl::LiveEntity value;
        return m_impl->live.Find(entityId, value) && value.cellId == cellId && value.partitionId == partitionId &&
               value.generation == generation;
    }

    ReferenceRegistryStats EntityReferenceRegistry::GetStats() const noexcept
    {
        ReferenceRegistryStats stats;
        if (m_impl == nullptr)
            return stats;
        VG_SCOPE_SHARED_LOCK(m_impl->lock);
        stats.cells = m_impl->cells.Size();
        stats.liveEntities = m_impl->live.Size();
        stats.sourceEntities = m_impl->sources.Size();
        stats.resolutionRevision = m_impl->resolutionRevision;
        stats.registrationEntitiesVisited = m_impl->registrationEntitiesVisited;
        stats.registrationReferencesVisited = m_impl->registrationReferencesVisited;
        stats.registrationSlotLookups = m_impl->registrationSlotLookups;
        for (auto iterator = m_impl->sources.Begin(), end = m_impl->sources.End(); iterator != end; ++iterator)
        {
            for (const Impl::Binding& binding : iterator.Value()->bindings)
            {
                ++stats.references;
                if (binding.resolved)
                    ++stats.resolvedReferences;
                else if (IsRequired(binding.kind))
                    ++stats.unresolvedRequiredReferences;
                else
                    ++stats.unresolvedOptionalReferences;
            }
        }
        return stats;
    }

    ecs::World* EntityReferenceRegistry::RegisteredWorld() const noexcept
    {
        return m_impl != nullptr ? m_impl->world : nullptr;
    }
} // namespace vanguard::entities
