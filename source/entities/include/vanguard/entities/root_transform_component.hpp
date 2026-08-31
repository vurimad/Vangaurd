#pragma once

#include <vanguard/entities/placed_component.hpp>

namespace vanguard::entities
{
    class PlaceholderComponent final : public IPlacedComponent
    {
    private:
        void OnTransformUpdated(math::Box& worldBounds) noexcept override;
    };
} // namespace vanguard::entities
