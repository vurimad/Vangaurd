#pragma once

#include <vanguard/ecs/native.hpp>
#include <vanguard/world/cells.hpp>

namespace vanguard::entities
{
    enum class ReferenceResult : u8
    {
        Success,
        InvalidArgument,
        InvalidState,
        DuplicateCell,
        DuplicateEntity,
        DuplicateSlot,
        MissingSource,
        MissingLocalTarget,
        StaleGeneration
    };

    enum class ReferenceState : u8
    {
        Undefined,
        Unresolved,
        Resolved
    };

    struct ResolvedEntityReference
    {
        ecs::EntityId sourceEntityId = ecs::InvalidEntityId;
        u64 slot = 0;
        ecs::EntityId targetEntityId = ecs::InvalidEntityId;
        world::EntityReferenceKind kind = world::EntityReferenceKind::RequiredLocal;
        ecs::Entity entity;
        u64 revision = 0;
        ReferenceState state = ReferenceState::Undefined;

        [[nodiscard]] explicit operator bool() const noexcept
        {
            return state == ReferenceState::Resolved && static_cast<bool>(entity);
        }
    };

    struct ReferenceRegistryStats
    {
        u32 cells = 0;
        u32 liveEntities = 0;
        u32 sourceEntities = 0;
        u32 references = 0;
        u32 resolvedReferences = 0;
        u32 unresolvedRequiredReferences = 0;
        u32 unresolvedOptionalReferences = 0;
        u64 resolutionRevision = 0;
    };

    /// World-scoped stable-identity registry. Runtime Flecs handles are transient observations and must
    /// be resolved again after a synchronization boundary; stable entity IDs remain valid across streaming.
    class EntityReferenceRegistry final
    {
    public:
        struct Impl;

        static constexpr u64 BasePartition = 0;

        EntityReferenceRegistry() noexcept = default;
        ~EntityReferenceRegistry();

        EntityReferenceRegistry(const EntityReferenceRegistry&) = delete;
        EntityReferenceRegistry& operator=(const EntityReferenceRegistry&) = delete;

        [[nodiscard]] bool Initialize(ecs::World& world) noexcept;
        [[nodiscard]] bool Shutdown() noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;

        /// Publishes one completely materialized cell. References whose source entity is not part of the
        /// published activation set are ignored. Required local targets must be present in the same set.
        [[nodiscard]] ReferenceResult RegisterCell(u64 cellId, u32 generation,
                                                   containers::ArraySpan<const ecs::EntityId> entities,
                                                   containers::ArraySpan<const world::EntityReferenceRecord> references) noexcept;

        /// Publishes an independently owned part of a cell. Activation groups use their stable group ID as
        /// the partition ID, allowing exact add/remove transactions without rebuilding the rest of the cell.
        [[nodiscard]] ReferenceResult RegisterPartition(
            u64 cellId, u32 generation, u64 partitionId, containers::ArraySpan<const ecs::EntityId> entities,
            containers::ArraySpan<const world::EntityReferenceRecord> references) noexcept;

        /// Removes outgoing references first, then makes the cell's identities unavailable. Incoming links
        /// immediately become unresolved and are relinked automatically if the identity is published again.
        [[nodiscard]] ReferenceResult UnregisterCell(u64 cellId, u32 generation) noexcept;
        [[nodiscard]] ReferenceResult UnregisterPartition(u64 cellId, u32 generation, u64 partitionId) noexcept;

        [[nodiscard]] ResolvedEntityReference Resolve(ecs::EntityId sourceEntityId, u64 slot) const noexcept;
        [[nodiscard]] bool IsCellReady(u64 cellId, u32 generation) const noexcept;
        [[nodiscard]] bool IsPartitionReady(u64 cellId, u32 generation, u64 partitionId) const noexcept;
        [[nodiscard]] bool ContainsEntity(ecs::EntityId entityId) const noexcept;
        [[nodiscard]] ReferenceRegistryStats GetStats() const noexcept;
        [[nodiscard]] ecs::World* RegisteredWorld() const noexcept;

    private:
        Impl* m_impl = nullptr;
    };

    /// Compile-time slot tags prevent unrelated reference roles from being mixed by gameplay code while the
    /// serialized representation remains the same stable 64-bit slot identity used by editor tooling.
    template<typename SlotTag>
    struct TypedEntityReference
    {
        ecs::EntityId sourceEntityId = ecs::InvalidEntityId;
        u64 slot = 0;

        [[nodiscard]] ResolvedEntityReference Resolve(const EntityReferenceRegistry& registry) const noexcept
        {
            return registry.Resolve(sourceEntityId, slot);
        }
    };
} // namespace vanguard::entities
