#include <vanguard/entities/hard_attachment.hpp>
#include <vanguard/entities/placed_component.hpp>

namespace vanguard::entities
{
    HardAttachment::HardAttachment(IPlacedComponent& source, IPlacedComponent& destination) noexcept
        : ITransformAttachment(source, destination)
    {
    }

    bool HardAttachment::ComputeFinalTransform(const IPlacedComponent* const source,
                                               math::WorldTransform& newLocalToWorld) const noexcept
    {
        if (source == nullptr)
            return false;
        newLocalToWorld = source->GetLocalToWorld();
        return true;
    }
} // namespace vanguard::entities
