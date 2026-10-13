#include <vanguard/entities/transform_component.hpp>

#include <vanguard/entities/component_registry.hpp>

namespace vanguard::entities
{
    bool RegisterTransformComponent(ComponentRegistry& registry) noexcept
    {
        const reflection::Schema& schema = GetTransformComponentSchema();
        const reflection::Schema* const registered = reflection::FindSchema(schema.id);
        if ((registered != nullptr && registered != &schema) || (registered == nullptr && !reflection::RegisterSchema(schema)))
            return false;
        return registry.RegisterObject<TransformComponentData, TransformComponent>(schema);
    }

    void TransformComponent::OnTransformUpdated(math::Box& worldBounds) noexcept
    {
        worldBounds = math::Box(GetLocalToWorld().GetPosition().AsVector3(), 0.1f);
    }

} // namespace vanguard::entities
