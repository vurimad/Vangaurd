#pragma once

namespace vanguard::entities
{
    class IPlacedComponent;
    class ITransformAttachment;

    class ITransformBinding
    {
    public:
        virtual ~ITransformBinding() = default;

        [[nodiscard]] virtual ITransformAttachment* CreateAttachment(IPlacedComponent& source,
                                                                      IPlacedComponent& destination) const noexcept = 0;
        virtual void DestroyAttachment(ITransformAttachment* attachment) const noexcept = 0;
        [[nodiscard]] virtual bool CanCreateAttachment(const IPlacedComponent& source,
                                                       const IPlacedComponent& destination) const noexcept = 0;
    };

    class HardTransformBinding final : public ITransformBinding
    {
    public:
        [[nodiscard]] ITransformAttachment* CreateAttachment(IPlacedComponent& source,
                                                              IPlacedComponent& destination) const noexcept override;
        void DestroyAttachment(ITransformAttachment* attachment) const noexcept override;
        [[nodiscard]] bool CanCreateAttachment(const IPlacedComponent& source,
                                               const IPlacedComponent& destination) const noexcept override;
    };
} // namespace vanguard::entities
