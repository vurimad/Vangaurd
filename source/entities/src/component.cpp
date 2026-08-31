#include <vanguard/entities/component.hpp>

namespace vanguard::entities
{
    Component::~Component() = default;

    ComponentHandle Component::GetHandle() const noexcept
    {
        return m_handle;
    }

    ecs::EntityId Component::GetEntity() const noexcept
    {
        return m_entity;
    }

    u64 Component::GetStableId() const noexcept
    {
        return m_stableId;
    }

    reflection::SchemaTypeId Component::GetType() const noexcept
    {
        return m_type;
    }

    bool Component::IsInitialized() const noexcept
    {
        return m_initialized;
    }

    bool Component::IsAttached() const noexcept
    {
        return m_attached;
    }

    bool Component::IsPostAttached() const noexcept
    {
        return m_postAttached;
    }

    bool Component::IsEnabled() const noexcept
    {
        return m_componentEnabled && m_entityEnabled;
    }

    bool Component::IsComponentEnabled() const noexcept
    {
        return m_componentEnabled;
    }

    bool Component::IsEntityEnabled() const noexcept
    {
        return m_entityEnabled;
    }

    IPlacedComponent* Component::GetPlacedComponent() noexcept
    {
        return nullptr;
    }

    const IPlacedComponent* Component::GetPlacedComponent() const noexcept
    {
        return nullptr;
    }

    bool Component::OnInitialize(const ComponentInitializeContext&) noexcept
    {
        return true;
    }

    void Component::OnUninitialize(const ComponentContext&) noexcept {}

    bool Component::OnAttach(const ComponentContext&) noexcept
    {
        return true;
    }

    void Component::OnPostAttach(const ComponentContext&) noexcept {}

    void Component::OnEnabled(const ComponentContext&, const bool) noexcept {}

    void Component::OnDetach(const ComponentContext&) noexcept {}
} // namespace vanguard::entities
