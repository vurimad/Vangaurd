#pragma once

#include <vanguard/entities/component_registry.hpp>
#include <vanguard/entities/reference_registry.hpp>
#include <vanguard/world/cells.hpp>

namespace vanguard::entities
{
    struct EntityOrigin
    {
        u64 cellId = 0;
        ecs::EntityId instanceId = ecs::InvalidEntityId;
        u64 prefabEntityStableId = prefabs::InvalidStableId;
    };

    struct EntityParent
    {
        ecs::EntityId stableParent = ecs::InvalidEntityId;
    };

    struct WorldPlacement
    {
        f64 translation[3]{};
        f32 rotation[4]{0.0f, 0.0f, 0.0f, 1.0f};
        f32 scale[3]{1.0f, 1.0f, 1.0f};
    };

    struct DisabledEntity
    {
    };

    enum class CellState : u8
    {
        Unknown,
        PendingActivation,
        PendingReferences,
        Active,
        PendingRelease,
        Failed
    };

    enum class ActivationGroupState : u8
    {
        Inactive,
        PendingActivation,
        PendingReferences,
        Active,
        PendingRelease,
        Failed
    };

    using ActivationOwnerId = u64;
    constexpr ActivationOwnerId InvalidActivationOwnerId = 0;

    using PrefabResolver = const prefabs::PrefabFile* (*)(resources::ResourceReference prefab, void* userData) noexcept;

    struct MaterializationConfig
    {
        u32 maximumEntitiesPerCell = 1u << 20u;
        u32 maximumComponentsPerCell = 1u << 22u;
        bool includeEditorData = false;
        bool includeInactiveActivationGroups = false;
    };

    struct MaterializationReport
    {
        u64 cellId = 0;
        u32 generation = 0;
        u32 placements = 0;
        u32 placementsSkippedInactive = 0;
        u32 entitiesSkippedEditorOnly = 0;
        u32 entities = 0;
        u32 components = 0;
        u32 overridesApplied = 0;
        u32 overridesRemoved = 0;
    };

    struct MaterializerStats
    {
        u32 trackedCells = 0;
        u32 pendingActivationCells = 0;
        u32 pendingReferenceCells = 0;
        u32 activeCells = 0;
        u32 pendingReleaseCells = 0;
        u32 failedCells = 0;
        u32 trackedActivationGroups = 0;
        u32 pendingActivationGroups = 0;
        u32 pendingReferenceGroups = 0;
        u32 activeActivationGroups = 0;
        u32 pendingReleaseGroups = 0;
        u32 failedActivationGroups = 0;
        u32 activationOwners = 0;
        u64 queuedEntities = 0;
        u64 activatedEntities = 0;
        u64 releasedEntities = 0;
        u64 cancelledCells = 0;
    };

    /// Returns the placement identity for the prefab root and a deterministic hierarchical identity
    /// for every prefab child. The materializer still collision-checks every derived value before queueing.
    [[nodiscard]] ecs::EntityId DeriveEntityId(ecs::EntityId instanceId, u64 prefabEntityStableId,
                                               u64 prefabRootStableId) noexcept;

    class CellMaterializer final
    {
    public:
        struct Impl;

        CellMaterializer() noexcept = default;
        ~CellMaterializer();

        CellMaterializer(const CellMaterializer&) = delete;
        CellMaterializer& operator=(const CellMaterializer&) = delete;

        [[nodiscard]] bool Initialize(ComponentRegistry& components, EntityReferenceRegistry& references,
                                      const MaterializationConfig& config = {}) noexcept;
        [[nodiscard]] bool Shutdown() noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;

        /// Materializer state transitions are serialized operations owned by the game-world synchronization
        /// thread; callers must not invoke this object concurrently. Component decoding itself is thread-safe
        /// after the registry is sealed and can be moved into a parallel planning stage later.
        ///
        /// Resolves and decodes an entire cell before queueing any live-world mutation. Prefab files
        /// need to remain valid only for this call because every queued value owns its staging copy.
        [[nodiscard]] Result QueueCell(const world::CellFile& cell, u32 generation, PrefabResolver resolvePrefab,
                                      void* userData = nullptr, MaterializationReport* report = nullptr) noexcept;

        /// Called after the owning GameWorld commits its structural queues. Readiness is published
        /// only when every planned entity and component can be observed in the expected generation.
        [[nodiscard]] Result CompleteActivation(u64 cellId, u32 generation) noexcept;
        [[nodiscard]] Result QueueRelease(u64 cellId, u32 generation) noexcept;
        [[nodiscard]] Result CompleteRelease(u64 cellId, u32 generation) noexcept;
        [[nodiscard]] Result Cancel(u64 cellId, u32 generation) noexcept;

        /// Acquires one authored activation group for an explicit owner. Required-local references pull their
        /// target groups into the same retained dependency closure. Overlapping owners share one live instance.
        [[nodiscard]] Result AcquireActivationGroup(const world::CellFile& cell, u32 generation, u64 groupId,
                                                    ActivationOwnerId ownerId, PrefabResolver resolvePrefab,
                                                    void* userData = nullptr) noexcept;

        /// Releases the exact ownership lease created by AcquireActivationGroup. Entities are removed only when
        /// the final direct or dependency owner releases the group.
        [[nodiscard]] Result ReleaseActivationGroup(u64 cellId, u32 generation, u64 groupId,
                                                    ActivationOwnerId ownerId) noexcept;

        /// Completes committed group batches and reference publication after the owning GameWorld flush.
        [[nodiscard]] Result SynchronizeActivationGroups(u64 cellId, u32 generation) noexcept;
        [[nodiscard]] ActivationGroupState ActivationState(u64 cellId, u64 groupId) const noexcept;
        [[nodiscard]] u32 ActivationOwnerCount(u64 cellId, u64 groupId) const noexcept;

        [[nodiscard]] CellState State(u64 cellId) const noexcept;
        [[nodiscard]] u32 Generation(u64 cellId) const noexcept;
        [[nodiscard]] MaterializerStats GetStats() const noexcept;

    private:
        Impl* m_impl = nullptr;
    };
} // namespace vanguard::entities
