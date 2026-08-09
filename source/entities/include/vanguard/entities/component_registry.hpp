#pragma once

#include <vanguard/ecs/native.hpp>
#include <vanguard/schemas/schemas.hpp>

#include <new>
#include <type_traits>

namespace vanguard::entities
{
    enum class Result : u8
    {
        Success,
        InvalidArgument,
        InvalidState,
        DuplicateIdentifier,
        UnknownSchema,
        UnsupportedSchemaVersion,
        SchemaFailure,
        MissingPrefab,
        InvalidPrefab,
        MissingOverrideTarget,
        DuplicateEntity,
        StaleGeneration,
        QueueFailure,
        InvalidReference,
        NotReady,
        LimitExceeded,
        OutOfMemory
    };

    [[nodiscard]] const char* ToString(Result result) noexcept;

    class ComponentRegistry;

    /// Owns one default-constructed and schema-decoded component value in staging memory. Decoded
    /// values are never visible to Flecs until ComponentRegistry::Queue is called and the world commits.
    class DecodedComponent final
    {
    public:
        DecodedComponent() noexcept = default;
        DecodedComponent(DecodedComponent&& other) noexcept;
        ~DecodedComponent();

        DecodedComponent(const DecodedComponent&) = delete;
        DecodedComponent& operator=(const DecodedComponent&) = delete;
        DecodedComponent& operator=(DecodedComponent&& other) noexcept;

        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] reflection::SchemaTypeId Schema() const noexcept;
        [[nodiscard]] u16 SourceVersion() const noexcept;
        void Reset() noexcept;

    private:
        using DestroyFunction = void (*)(void*) noexcept;
        using QueueFunction = bool (*)(ecs::World&, ecs::CommandBatch, ecs::EntityId, ecs::ComponentId,
                                       const ecs_world_t*, const void*, bool) noexcept;

        memory::MemoryBlock m_value;
        reflection::SchemaTypeId m_schema = reflection::InvalidSchemaTypeId;
        ecs::ComponentId m_runtimeComponent = ecs::InvalidComponentId;
        const ecs_world_t* m_world = nullptr;
        u16 m_sourceVersion = 0;
        DestroyFunction m_destroy = nullptr;
        QueueFunction m_queue = nullptr;

        friend class ComponentRegistry;
    };

    /// World-scoped mapping from stable cooked schema identities to transient Flecs component types.
    /// Registration is serialized startup work. Seal freezes the mapping, after which Decode and Queue
    /// may run concurrently because descriptors are immutable and ECS command submission is synchronized.
    /// An empty mapping is valid for worlds whose prefabs contain no application-defined components.
    class ComponentRegistry final
    {
    public:
        struct Impl;

        ComponentRegistry() noexcept = default;
        ~ComponentRegistry();

        ComponentRegistry(const ComponentRegistry&) = delete;
        ComponentRegistry& operator=(const ComponentRegistry&) = delete;

        [[nodiscard]] bool Initialize(ecs::World& world) noexcept;
        [[nodiscard]] bool Seal() noexcept;
        [[nodiscard]] bool Shutdown() noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;
        [[nodiscard]] bool IsSealed() const noexcept;

        template<typename Component>
        [[nodiscard]] bool Register(const reflection::Schema& schema) noexcept
        {
            static_assert(std::is_nothrow_default_constructible_v<Component>,
                          "Materialized components must be nothrow default constructible");
            static_assert(std::is_nothrow_copy_constructible_v<Component>,
                          "Materialized components must be nothrow copy constructible");
            static_assert(std::is_nothrow_copy_assignable_v<Component>,
                          "Materialized components must be nothrow copy assignable");
            static_assert(std::is_nothrow_destructible_v<Component>,
                          "Materialized components must be nothrow destructible");
            ecs::World* const world = RegisteredWorld();
            if (world == nullptr || IsSealed() || schema.size != sizeof(Component) || schema.alignment != alignof(Component) ||
                reflection::FindSchema(schema.id) != &schema || HasSchema(schema.id)) return false;
            const ecs::ComponentType<Component> type = ecs::RegisterComponent<Component>(*world, true);
            if (!type) return false;
            const Registration registration{
                type.id,
                type.world,
                sizeof(Component),
                alignof(Component),
                [](void* const address) noexcept { ::new (address) Component(); },
                [](void* const address) noexcept { static_cast<Component*>(address)->~Component(); },
                [](ecs::World& target, const ecs::CommandBatch batch, const ecs::EntityId entity,
                   const ecs::ComponentId runtimeComponent, const ecs_world_t* const owner,
                   const void* const value, const bool enabled) noexcept
                {
                    const ecs::ComponentType<Component> componentType{runtimeComponent, owner};
                    return target.QueueAddComponent(batch, entity, componentType) &&
                           target.QueueSetComponent(batch, entity, componentType, *static_cast<const Component*>(value)) &&
                           (enabled || target.QueueSetComponentEnabled(batch, entity, componentType, false));
                }};
            return RegisterDescriptor(schema, registration);
        }

        [[nodiscard]] Result Decode(reflection::SchemaTypeId schema, u16 sourceVersion,
                                    containers::ArraySpan<const u8> data, DecodedComponent& output) const noexcept;
        [[nodiscard]] bool Queue(ecs::CommandBatch batch, ecs::EntityId entity,
                                 const DecodedComponent& component, bool enabled = true) const noexcept;
        [[nodiscard]] bool HasSchema(reflection::SchemaTypeId schema) const noexcept;
        [[nodiscard]] ecs::ComponentId RuntimeComponent(reflection::SchemaTypeId schema) const noexcept;
        [[nodiscard]] u32 Count() const noexcept;
        [[nodiscard]] ecs::World* RegisteredWorld() const noexcept;

    private:
        using ConstructFunction = void (*)(void*) noexcept;
        using DestroyFunction = void (*)(void*) noexcept;
        using QueueFunction = DecodedComponent::QueueFunction;

        struct Registration
        {
            ecs::ComponentId runtimeComponent = ecs::InvalidComponentId;
            const ecs_world_t* world = nullptr;
            usize objectSize = 0;
            usize objectAlignment = 0;
            ConstructFunction construct = nullptr;
            DestroyFunction destroy = nullptr;
            QueueFunction queue = nullptr;
        };

        [[nodiscard]] bool RegisterDescriptor(const reflection::Schema& schema,
                                              const Registration& registration) noexcept;

        Impl* m_impl = nullptr;
    };
} // namespace vanguard::entities
