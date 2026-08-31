#pragma once

#include <vanguard/entities/component.hpp>
#include <vanguard/memory/memory.hpp>

#include <new>
#include <type_traits>

namespace vanguard::entities
{
    inline constexpr u64 InvalidComponentStableId = 0;

    struct ComponentDirectoryConfig
    {
        u32 maximumComponents = 1u << 20u;
    };

    enum class ComponentDirectoryFailureCode : u8
    {
        None,
        NotInitialized,
        WrongThread,
        InvalidConfiguration,
        InvalidDescriptor,
        DuplicateIdentifier,
        CapacityExceeded,
        InvalidHandle,
        InvalidState,
        InitializationFailure,
        AttachmentFailure
    };

    struct ComponentDirectoryFailure
    {
        ComponentDirectoryFailureCode code = ComponentDirectoryFailureCode::None;
        ComponentHandle component;
        ecs::EntityId entity = ecs::InvalidEntityId;
        u64 stableId = InvalidComponentStableId;
        const char* message = nullptr;
    };

    struct ComponentSnapshot
    {
        ComponentHandle handle;
        Component* component = nullptr;
        ecs::EntityId entity = ecs::InvalidEntityId;
        u64 stableId = InvalidComponentStableId;
        reflection::SchemaTypeId type = reflection::InvalidSchemaTypeId;
        bool initialized = false;
        bool attached = false;
        bool postAttached = false;
        bool componentEnabled = true;
        bool entityEnabled = true;
        bool enabled = true;
    };

    struct ComponentDirectoryStats
    {
        u32 components = 0;
        u32 initializedComponents = 0;
        u32 attachedComponents = 0;
        u32 postAttachedComponents = 0;
        u32 entities = 0;
        u64 createdComponents = 0;
        u64 destroyedComponents = 0;
        u64 rejectedOperations = 0;
        bool initialized = false;
    };

    using VisitComponent = bool (*)(const ComponentSnapshot& component, void* userData) noexcept;

    /// Opaque ownership token for one component initialization callback. A token is begun on
    /// the world thread, executed once on a worker, and completed on the world thread.
    class ComponentInitialization final
    {
    public:
        ComponentInitialization() noexcept = default;

        [[nodiscard]] bool IsValid() const noexcept
        {
            return m_component != nullptr && m_handle.IsValid();
        }

    private:
        friend class ComponentDirectory;

        Component* m_component = nullptr;
        ComponentHandle m_handle;
        bool m_executed = false;
        bool m_succeeded = false;
    };

    /// Per-world ownership and identity directory for stable runtime components. It deliberately
    /// provides no typed store: frame systems consume their own dense runtime data instead.
    class ComponentDirectory final
    {
    public:
        struct Impl;

        ComponentDirectory() noexcept = default;
        ~ComponentDirectory();

        ComponentDirectory(const ComponentDirectory&) = delete;
        ComponentDirectory& operator=(const ComponentDirectory&) = delete;

        [[nodiscard]] bool Initialize(game::GameWorld& world, const ComponentDirectoryConfig& config = {},
                                      ComponentDirectoryFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool Shutdown(ComponentDirectoryFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;
        [[nodiscard]] game::GameWorld* GetWorld() noexcept;
        [[nodiscard]] const game::GameWorld* GetWorld() const noexcept;

        template <typename Type, typename... Args>
        [[nodiscard]] bool Create(const ecs::EntityId entity, const u64 stableId, const reflection::SchemaTypeId type,
                                  ComponentHandle& handle, Type*& component, ComponentDirectoryFailure* const failure, Args&&... args) noexcept
        {
            static_assert(std::is_base_of_v<Component, Type>, "Directory objects must derive from Component");
            static_assert(std::is_nothrow_constructible_v<Type, Args...>, "Directory objects must be nothrow constructible");
            static_assert(std::is_nothrow_destructible_v<Type>, "Directory objects must be nothrow destructible");
            handle = {};
            component = nullptr;
            if (!ValidateCreate(entity, stableId, type, failure))
                return false;
            memory::MemoryBlock storage = memory::Allocate(memory::PoolId::World, sizeof(Type), alignof(Type));
            if (!storage)
                return AllocationFailed(entity, stableId, failure);
            Type* const object = ::new (storage.address) Type(static_cast<Args&&>(args)...);
            if (!Adopt(entity, stableId, type, *object, storage,
                       [](Component* const value) noexcept { static_cast<Type*>(value)->~Type(); }, handle, failure))
            {
                object->~Type();
                memory::Free(storage);
                return false;
            }
            component = object;
            return true;
        }

        template <typename Type, typename... Args>
        [[nodiscard]] bool Create(const ecs::EntityId entity, const u64 stableId, const reflection::SchemaTypeId type,
                                  ComponentHandle& handle, Type*& component, Args&&... args) noexcept
        {
            return Create(entity, stableId, type, handle, component, nullptr, static_cast<Args&&>(args)...);
        }

        [[nodiscard]] bool BeginInitialization(ComponentHandle component, ComponentInitialization& initialization,
                                               ComponentDirectoryFailure* failure = nullptr) noexcept;
        void ExecuteInitialization(ComponentInitialization& initialization, const ComponentInitializeContext& context) noexcept;
        [[nodiscard]] bool CompleteInitialization(ComponentInitialization& initialization,
                                                  ComponentDirectoryFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool CancelInitialization(ComponentInitialization& initialization,
                                                ComponentDirectoryFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool AttachComponent(ComponentHandle component, ComponentDirectoryFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool PostAttachComponent(ComponentHandle component, ComponentDirectoryFailure* failure = nullptr) noexcept;
        /// Sets both authored inputs before initialization. Runtime transitions must use the
        /// component- or entity-specific setters so neither source overwrites the other.
        [[nodiscard]] bool SetInitialEnabledState(ComponentHandle component, bool componentEnabled, bool entityEnabled,
                                                  ComponentDirectoryFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool SetComponentEnabled(ComponentHandle component, bool enabled,
                                               ComponentDirectoryFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool SetEntityEnabled(ecs::EntityId entity, bool enabled,
                                            ComponentDirectoryFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool DetachComponent(ComponentHandle component, ComponentDirectoryFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool UninitializeComponent(ComponentHandle component, ComponentDirectoryFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool Destroy(ComponentHandle component, ComponentDirectoryFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool DestroyEntityComponents(ecs::EntityId entity, ComponentDirectoryFailure* failure = nullptr) noexcept;

        [[nodiscard]] Component* Get(ComponentHandle component) noexcept;
        [[nodiscard]] const Component* Get(ComponentHandle component) const noexcept;
        [[nodiscard]] Component* Find(ecs::EntityId entity, u64 stableId) noexcept;
        [[nodiscard]] const Component* Find(ecs::EntityId entity, u64 stableId) const noexcept;
        [[nodiscard]] bool GetSnapshot(ComponentHandle component, ComponentSnapshot& snapshot) const noexcept;
        void Visit(ecs::EntityId entity, VisitComponent visitor, void* userData = nullptr) const noexcept;
        void VisitAll(VisitComponent visitor, void* userData = nullptr) const noexcept;
        [[nodiscard]] u32 GetEntityComponentCount(ecs::EntityId entity) const noexcept;
        [[nodiscard]] ComponentDirectoryStats GetStats() const noexcept;

    private:
        using DestroyFunction = void (*)(Component*) noexcept;

        [[nodiscard]] bool AllocationFailed(ecs::EntityId entity, u64 stableId, ComponentDirectoryFailure* failure) noexcept;
        [[nodiscard]] bool ValidateCreate(ecs::EntityId entity, u64 stableId, reflection::SchemaTypeId type,
                                          ComponentDirectoryFailure* failure) const noexcept;
        [[nodiscard]] bool Adopt(ecs::EntityId entity, u64 stableId, reflection::SchemaTypeId type, Component& component,
                                 memory::MemoryBlock storage, DestroyFunction destroy, ComponentHandle& handle,
                                 ComponentDirectoryFailure* failure) noexcept;

        Impl* m_impl = nullptr;
    };
} // namespace vanguard::entities
