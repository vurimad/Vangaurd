#pragma once

#include <vanguard/containers/containers.hpp>

#include <redMathPublic.h>
#include <vector3.h>
#include <vector4.h>
#include <worldTransform.h>

namespace vanguard::entities
{
    class IPlacedComponent;
    class TransformSystem;

    class ITransformAttachment
    {
    public:
        ITransformAttachment(IPlacedComponent& source, IPlacedComponent& destination) noexcept;
        virtual ~ITransformAttachment() = default;

        ITransformAttachment(const ITransformAttachment&) = delete;
        ITransformAttachment& operator=(const ITransformAttachment&) = delete;

        [[nodiscard]] IPlacedComponent& GetSource() const noexcept;
        [[nodiscard]] IPlacedComponent& GetDestination() const noexcept;
        [[nodiscard]] bool IsCrossEntityBinding() const noexcept;
        [[nodiscard]] bool IsSystemUpdateBinding() const noexcept;
        [[nodiscard]] bool IsFloatingBinding() const noexcept;
        [[nodiscard]] bool IsTransformUpdateCascaded() const noexcept;
        [[nodiscard]] bool IsAttached() const noexcept;

        void SetCrossEntityBindingFlag(bool value) noexcept;
        void SetSystemUpdateBinding(bool value) noexcept;
        void SetFloatingBinding(bool value) noexcept;
        void SetTransformUpdateCascaded(bool value) noexcept;

        [[nodiscard]] virtual bool ComputeFinalTransform(const IPlacedComponent* source,
                                                         math::WorldTransform& newLocalToWorld) const noexcept = 0;

    private:
        friend class TransformSystem;

        IPlacedComponent* m_source = nullptr;
        IPlacedComponent* m_destination = nullptr;
        bool m_crossEntityBinding = false;
        bool m_systemUpdateBinding = false;
        bool m_floatingBinding = false;
        bool m_transformUpdateCascaded = true;
        bool m_attached = false;
    };

} // namespace vanguard::entities
