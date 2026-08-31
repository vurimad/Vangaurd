#pragma once

#include <vanguard/ecs/ecs.hpp>
#include <vanguard/jobs/jobs.hpp>
#include <vanguard/reflection/reflection.hpp>
#include <vanguard/resources/resource_pipeline.hpp>

namespace vanguard::game
{
    class GameWorld;
}

namespace vanguard::entities
{
    class ComponentDirectory;
    class Component;
    class IPlacedComponent;

    using ResolveSiblingComponent = Component* (*)(ecs::EntityId entity, u64 stableId, void* userData) noexcept;

    /// Immutable entity-local component lookup captured before parallel initialization begins.
    class ComponentResolver final
    {
    public:
        ComponentResolver() noexcept = default;
        ComponentResolver(const ecs::EntityId entity, const ResolveSiblingComponent resolve, void* const userData) noexcept
            : m_entity(entity), m_resolve(resolve), m_userData(userData)
        {
        }

        [[nodiscard]] Component* Find(const u64 stableId) const noexcept
        {
            return m_resolve != nullptr ? m_resolve(m_entity, stableId, m_userData) : nullptr;
        }

    private:
        ecs::EntityId m_entity = ecs::InvalidEntityId;
        ResolveSiblingComponent m_resolve = nullptr;
        void* m_userData = nullptr;
    };

    /// Access to resources already retained by the streaming dependency graph.
    class ComponentResourceAccess final
    {
    public:
        ComponentResourceAccess() noexcept = default;
        ComponentResourceAccess(resources::ResourceRegistry* const registry, resources::ResourcePipeline* const pipeline) noexcept
            : m_registry(registry), m_pipeline(pipeline)
        {
        }

        [[nodiscard]] resources::ResourceHandle TryAcquire(const resources::ResourceReference reference) const noexcept
        {
            return m_registry != nullptr ? m_registry->TryAcquire(reference) : resources::ResourceHandle{};
        }

        [[nodiscard]] resources::PipelineRequest Request(const resources::ResourceReference reference,
                                                         const resources::LoadPriority priority) const noexcept
        {
            return m_pipeline != nullptr ? m_pipeline->Request(reference, priority) : resources::PipelineRequest{};
        }

    private:
        resources::ResourceRegistry* m_registry = nullptr;
        resources::ResourcePipeline* m_pipeline = nullptr;
    };

    struct ComponentHandle
    {
        u32 index = ~u32{0};
        u32 generation = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return index != ~u32{0} && generation != 0;
        }

        [[nodiscard]] constexpr explicit operator bool() const noexcept
        {
            return IsValid();
        }

        [[nodiscard]] friend constexpr bool operator==(const ComponentHandle&, const ComponentHandle&) noexcept = default;
    };

    struct ComponentContext
    {
        game::GameWorld& world;
    };

    struct ComponentInitializeContext
    {
        game::GameWorld& world;
        ComponentResolver components;
        ComponentResourceAccess resources;
        resources::LoadPriority ioPriority = resources::LoadPriority::Normal;
        const jobs::JobContext& continuation;
    };

    /// Stable, individually allocated runtime component. ComponentDirectory owns the object and
    /// drives this lifecycle; derived classes implement only the transition callbacks.
    class Component
    {
    public:
        Component() noexcept = default;
        virtual ~Component();

        Component(const Component&) = delete;
        Component& operator=(const Component&) = delete;

        [[nodiscard]] ComponentHandle GetHandle() const noexcept;
        [[nodiscard]] ecs::EntityId GetEntity() const noexcept;
        [[nodiscard]] u64 GetStableId() const noexcept;
        [[nodiscard]] reflection::SchemaTypeId GetType() const noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;
        [[nodiscard]] bool IsAttached() const noexcept;
        [[nodiscard]] bool IsPostAttached() const noexcept;
        [[nodiscard]] bool IsEnabled() const noexcept;
        [[nodiscard]] bool IsComponentEnabled() const noexcept;
        [[nodiscard]] bool IsEntityEnabled() const noexcept;

        /// Optional spatial capability exposed explicitly so materialization does not depend
        /// on C++ RTTI.
        [[nodiscard]] virtual IPlacedComponent* GetPlacedComponent() noexcept;
        [[nodiscard]] virtual const IPlacedComponent* GetPlacedComponent() const noexcept;

    protected:
        [[nodiscard]] virtual bool OnInitialize(const ComponentInitializeContext& context) noexcept;
        virtual void OnUninitialize(const ComponentContext& context) noexcept;
        [[nodiscard]] virtual bool OnAttach(const ComponentContext& context) noexcept;
        virtual void OnPostAttach(const ComponentContext& context) noexcept;
        virtual void OnEnabled(const ComponentContext& context, bool enabled) noexcept;
        virtual void OnDetach(const ComponentContext& context) noexcept;

    private:
        friend class ComponentDirectory;

        ComponentDirectory* m_directory = nullptr;
        ComponentHandle m_handle;
        ecs::EntityId m_entity = ecs::InvalidEntityId;
        u64 m_stableId = 0;
        reflection::SchemaTypeId m_type = reflection::InvalidSchemaTypeId;
        bool m_initialized = false;
        bool m_initializing = false;
        bool m_attached = false;
        bool m_postAttached = false;
        bool m_componentEnabled = true;
        bool m_entityEnabled = true;
        bool m_enableNotificationPending = false;
    };
} // namespace vanguard::entities
