#pragma once

#include <vanguard/entities/transform_attachment.hpp>

namespace vanguard::entities
{
    class HardAttachment final : public ITransformAttachment
    {
    public:
        HardAttachment(IPlacedComponent& source, IPlacedComponent& destination) noexcept;

        [[nodiscard]] bool ComputeFinalTransform(const IPlacedComponent* source,
                                                 math::WorldTransform& newLocalToWorld) const noexcept override;
    };
} // namespace vanguard::entities
