#pragma once

#include <vanguard/entities/component_schemas.hpp>
#include <vanguard/entities/placed_component.hpp>

namespace vanguard::entities
{
    class ComponentRegistry;

    [[nodiscard]] bool RegisterTransformComponent(ComponentRegistry& registry) noexcept;

    class TransformComponent : public IPlacedComponent
    {
    public:
        explicit TransformComponent(const TransformComponentData&) noexcept {}

    private:
        void OnTransformUpdated(math::Box& worldBounds) noexcept override;
    };

} // namespace vanguard::entities
