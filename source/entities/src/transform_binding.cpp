#include <vanguard/entities/transform_binding.hpp>
#include <vanguard/entities/hard_attachment.hpp>
#include <vanguard/entities/placed_component.hpp>

#include <vanguard/memory/memory.hpp>

#include <new>

namespace vanguard::entities
{
    ITransformAttachment* HardTransformBinding::CreateAttachment(IPlacedComponent& source,
                                                                 IPlacedComponent& destination) const noexcept
    {
        if (!CanCreateAttachment(source, destination))
            return nullptr;
        memory::MemoryBlock block = memory::Allocate(memory::PoolId::World, sizeof(HardAttachment), alignof(HardAttachment));
        return block ? ::new (block.address) HardAttachment(source, destination) : nullptr;
    }

    void HardTransformBinding::DestroyAttachment(ITransformAttachment* const attachment) const noexcept
    {
        if (attachment == nullptr || attachment->IsAttached())
            return;
        HardAttachment* const hardAttachment = static_cast<HardAttachment*>(attachment);
        hardAttachment->~HardAttachment();
        memory::MemoryBlock block{hardAttachment, sizeof(HardAttachment), memory::PoolId::World};
        memory::Free(block);
    }

    bool HardTransformBinding::CanCreateAttachment(const IPlacedComponent& source,
                                                   const IPlacedComponent& destination) const noexcept
    {
        return &source != &destination && source.IsRegistered() && destination.IsRegistered();
    }
} // namespace vanguard::entities
