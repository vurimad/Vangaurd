#include <vanguard/entities/root_transform_component.hpp>

namespace vanguard::entities
{
    void PlaceholderComponent::OnTransformUpdated(math::Box& worldBounds) noexcept
    {
        worldBounds = math::Box(GetLocalToWorld().GetPosition().AsVector3(), 0.1f);
    }
} // namespace vanguard::entities
