#pragma once

#include <vanguard/containers/containers.hpp>

#include <new>
#include <type_traits>

struct ecs_world_t;

namespace vanguard::ecs
{
    using EntityId = u64;
    inline constexpr EntityId InvalidEntityId = 0;
    using ComponentId = u64;
    inline constexpr ComponentId InvalidComponentId = 0;
    using CommandBatchId = u64;
    inline constexpr CommandBatchId InvalidCommandBatchId = 0;

    struct Entity final
    {
        u64 value = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept { return value != 0; }
        [[nodiscard]] constexpr explicit operator bool() const noexcept { return IsValid(); }
        [[nodiscard]] friend constexpr bool operator==(const Entity&, const Entity&) noexcept = default;
    };

    enum class ActionType : u8
    {
        Create,
        Destroy
    };

    enum class ComponentActionType : u8
    {
        Add,
        Set,
        Remove,
        Enable,
        Disable
    };

    struct CommandBatch final
    {
        CommandBatchId id = InvalidCommandBatchId;
        const ecs_world_t* world = nullptr;

        [[nodiscard]] constexpr explicit operator bool() const noexcept
        {
            return id != InvalidCommandBatchId && world != nullptr;
        }
    };

    enum class CommandBatchStatus : u8
    {
        Unknown,
        Open,
        Pending,
        Succeeded,
        Failed
    };

    struct CommandBatchReport
    {
        u32 queued = 0;
        u32 completed = 0;
        u32 rejected = 0;
    };

    /// Runtime component token registered for one ECS world. It is neither stable nor serializable.
    template<typename Component>
    struct ComponentType final
    {
        ComponentId id = InvalidComponentId;
        const ecs_world_t* world = nullptr;

        [[nodiscard]] constexpr explicit operator bool() const noexcept
        {
            return id != InvalidComponentId && world != nullptr;
        }
    };

    struct ActionReport
    {
        u32 queued = 0;
        u32 created = 0;
        u32 destroyed = 0;
        u32 rejected = 0;
    };

    struct ComponentActionReport
    {
        u32 queued = 0;
        u32 added = 0;
        u32 set = 0;
        u32 removed = 0;
        u32 enabled = 0;
        u32 disabled = 0;
        u32 rejected = 0;
    };

    enum class CommittedChangeKind : u8
    {
        EntityCreated,
        EntityDestroyed,
        ComponentAdded,
        ComponentSet,
        ComponentRemoved,
        ComponentEnabled,
        ComponentDisabled
    };

    /// Exact identity-level record published only after a structural operation commits successfully.
    /// The component field is invalid for entity creation and destruction records.
    struct CommittedChange
    {
        u64 sequence = 0;
        EntityId entity = InvalidEntityId;
        ComponentId component = InvalidComponentId;
        CommandBatchId commandBatch = InvalidCommandBatchId;
        CommittedChangeKind kind = CommittedChangeKind::EntityCreated;
    };

    struct CommittedChangeCursor
    {
        /// Zero starts at the oldest retained record. Otherwise this is the next sequence to read.
        u64 nextSequence = 0;
    };

    struct CommittedChangeReadResult
    {
        u64 firstSequence = 0;
        u64 nextSequence = 0;
        u32 records = 0;
        u32 lostRecords = 0;
    };

    struct WorldConfig
    {
        u32 initialEntityCapacity = 16 * 1024;
        u32 initialActionCapacity = 4 * 1024;
        u32 committedChangeCapacity = 64 * 1024;
    };

    struct WorldStats
    {
        u32 entities = 0;
        u32 pendingActions = 0;
        u32 pendingComponentActions = 0;
        u32 commandBatches = 0;
        u64 createdEntities = 0;
        u64 destroyedEntities = 0;
        u64 rejectedActions = 0;
        u64 addedComponents = 0;
        u64 setComponents = 0;
        u64 removedComponents = 0;
        u64 enabledComponents = 0;
        u64 disabledComponents = 0;
        u64 rejectedComponentActions = 0;
        u64 committedChanges = 0;
        u64 overwrittenCommittedChanges = 0;
        bool progressing = false;
    };

    /// Owns one Flecs world and the stable-identity boundary around it. Structural actions may be
    /// queued concurrently, but FlushActions, Progress and Shutdown are serialized world operations.
    class World final
    {
    public:
        struct Impl;

        World() noexcept = default;
        ~World();

        World(const World&) = delete;
        World& operator=(const World&) = delete;

        [[nodiscard]] bool Initialize(const WorldConfig& config = {}) noexcept;
        [[nodiscard]] bool Shutdown() noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;

        /// Groups commands into one owned publication unit. A sealed batch succeeds only when every
        /// queued structural and component command commits. Cancellation removes only commands owned
        /// by that batch and never consumes another producer's work; already committed work remains live.
        [[nodiscard]] CommandBatch BeginCommandBatch() noexcept;
        [[nodiscard]] bool SealCommandBatch(CommandBatch batch) noexcept;
        [[nodiscard]] bool CancelCommandBatch(CommandBatch batch) noexcept;
        [[nodiscard]] CommandBatchStatus GetCommandBatchStatus(
            CommandBatch batch, CommandBatchReport* report = nullptr) const noexcept;
        [[nodiscard]] bool RetireCommandBatch(CommandBatch batch) noexcept;

        [[nodiscard]] bool QueueCreate(EntityId identity) noexcept;
        [[nodiscard]] bool QueueCreate(CommandBatch batch, EntityId identity) noexcept;
        [[nodiscard]] bool QueueDestroy(EntityId identity) noexcept;
        [[nodiscard]] bool QueueDestroy(CommandBatch batch, EntityId identity) noexcept;
        [[nodiscard]] bool FlushActions(ActionReport* report = nullptr) noexcept;

        /// Live component structure is changed only when FlushComponentActions is called. Tokens
        /// must have been registered for this exact world through RegisterComponent in native.hpp.
        template<typename Component>
        [[nodiscard]] bool QueueAddComponent(EntityId identity, const ComponentType<Component> type) noexcept
        {
            return QueueComponentAction({}, identity, type.id, type.world, ComponentActionType::Add, nullptr, 0, 0,
                                        nullptr, nullptr);
        }

        template<typename Component>
        [[nodiscard]] bool QueueAddComponent(const CommandBatch batch, const EntityId identity,
                                             const ComponentType<Component> type) noexcept
        {
            return QueueComponentAction(batch, identity, type.id, type.world, ComponentActionType::Add, nullptr, 0, 0,
                                        nullptr, nullptr);
        }

        template<typename Component>
        [[nodiscard]] bool QueueSetComponent(EntityId identity, const ComponentType<Component> type,
                                             const Component& value) noexcept
        {
            static_assert(std::is_nothrow_copy_constructible_v<Component>,
                          "Queued component values must be nothrow copy constructible");
            static_assert(std::is_nothrow_copy_assignable_v<Component>,
                          "Queued component values must be nothrow copy assignable for Flecs Set");
            static_assert(std::is_nothrow_destructible_v<Component>,
                          "Queued component values must be nothrow destructible");
            const auto copy = [](void* const destination, const void* const source) noexcept
            {
                ::new (destination) Component(*static_cast<const Component*>(source));
            };
            const auto destroy = [](void* const valueAddress) noexcept
            {
                static_cast<Component*>(valueAddress)->~Component();
            };
            return QueueComponentAction({}, identity, type.id, type.world, ComponentActionType::Set, &value,
                                        sizeof(Component), alignof(Component), copy, destroy);
        }

        template<typename Component>
        [[nodiscard]] bool QueueSetComponent(const CommandBatch batch, const EntityId identity,
                                             const ComponentType<Component> type, const Component& value) noexcept
        {
            static_assert(std::is_nothrow_copy_constructible_v<Component>,
                          "Queued component values must be nothrow copy constructible");
            static_assert(std::is_nothrow_copy_assignable_v<Component>,
                          "Queued component values must be nothrow copy assignable for Flecs Set");
            static_assert(std::is_nothrow_destructible_v<Component>,
                          "Queued component values must be nothrow destructible");
            const auto copy = [](void* const destination, const void* const source) noexcept
            {
                ::new (destination) Component(*static_cast<const Component*>(source));
            };
            const auto destroy = [](void* const valueAddress) noexcept
            {
                static_cast<Component*>(valueAddress)->~Component();
            };
            return QueueComponentAction(batch, identity, type.id, type.world, ComponentActionType::Set, &value,
                                        sizeof(Component), alignof(Component), copy, destroy);
        }

        template<typename Component>
        [[nodiscard]] bool QueueRemoveComponent(EntityId identity, const ComponentType<Component> type) noexcept
        {
            return QueueComponentAction({}, identity, type.id, type.world, ComponentActionType::Remove, nullptr, 0, 0,
                                        nullptr, nullptr);
        }

        template<typename Component>
        [[nodiscard]] bool QueueRemoveComponent(const CommandBatch batch, const EntityId identity,
                                                const ComponentType<Component> type) noexcept
        {
            return QueueComponentAction(batch, identity, type.id, type.world, ComponentActionType::Remove,
                                        nullptr, 0, 0, nullptr, nullptr);
        }

        template<typename Component>
        [[nodiscard]] bool QueueSetComponentEnabled(const CommandBatch batch, const EntityId identity,
                                                    const ComponentType<Component> type,
                                                    const bool enabled) noexcept
        {
            return QueueComponentAction(batch, identity, type.id, type.world,
                                        enabled ? ComponentActionType::Enable : ComponentActionType::Disable,
                                        nullptr, 0, 0, nullptr, nullptr);
        }

        [[nodiscard]] bool FlushComponentActions(ComponentActionReport* report = nullptr) noexcept;
        /// Reads retained committed changes without consuming them for other subscribers. A slow reader is
        /// advanced to the oldest retained sequence and receives an exact lost-record count.
        [[nodiscard]] bool ReadCommittedChanges(CommittedChangeCursor& cursor,
                                                containers::DynamicArray<CommittedChange>& changes,
                                                u32 maximumRecords,
                                                CommittedChangeReadResult* result = nullptr) const noexcept;
        /// Tiny observer boundary for intentional native Flecs writers. Observer callbacks may publish only
        /// the exact identity/component/event record; derived-system work remains outside Flecs execution.
        [[nodiscard]] bool CaptureNativeComponentChange(EntityId entity, ComponentId component,
                                                        CommittedChangeKind kind) noexcept;
        [[nodiscard]] u64 NextCommittedChangeSequence() const noexcept;
        [[nodiscard]] bool Progress(f32 deltaSeconds) noexcept;

        [[nodiscard]] Entity Resolve(EntityId identity) const noexcept;
        [[nodiscard]] EntityId Identity(Entity entity) const noexcept;
        [[nodiscard]] bool IsAlive(Entity entity) const noexcept;
        [[nodiscard]] WorldStats GetStats() const noexcept;

        /// Runtime-only integration boundary. Flecs entity values obtained from this world must never
        /// be serialized or retained after the corresponding stable identity is destroyed.
        [[nodiscard]] ecs_world_t* Native() noexcept;
        [[nodiscard]] const ecs_world_t* Native() const noexcept;

    private:
        using ComponentCopy = void (*)(void*, const void*) noexcept;
        using ComponentDestroy = void (*)(void*) noexcept;

        [[nodiscard]] bool QueueComponentAction(CommandBatch batch, EntityId identity, ComponentId component,
                                                const ecs_world_t* tokenWorld, ComponentActionType action,
                                                const void* value, usize valueSize, usize valueAlignment,
                                                ComponentCopy copy, ComponentDestroy destroy) noexcept;

        Impl* m_impl = nullptr;
    };
} // namespace vanguard::ecs
