#include <vanguard/entities/transform_attachment.hpp>
#include <vanguard/entities/placed_component.hpp>

namespace vanguard::entities
{
    ITransformAttachment::ITransformAttachment(IPlacedComponent& source, IPlacedComponent& destination) noexcept
        : m_source(&source), m_destination(&destination)
    {
    }

    IPlacedComponent& ITransformAttachment::GetSource() const noexcept
    {
        return *m_source;
    }

    IPlacedComponent& ITransformAttachment::GetDestination() const noexcept
    {
        return *m_destination;
    }

    bool ITransformAttachment::IsCrossEntityBinding() const noexcept
    {
        return m_crossEntityBinding;
    }

    bool ITransformAttachment::IsSystemUpdateBinding() const noexcept
    {
        return m_systemUpdateBinding;
    }

    bool ITransformAttachment::IsFloatingBinding() const noexcept
    {
        return m_floatingBinding;
    }

    bool ITransformAttachment::IsTransformUpdateCascaded() const noexcept
    {
        return m_transformUpdateCascaded;
    }

    bool ITransformAttachment::IsAttached() const noexcept
    {
        return m_attached;
    }

    void ITransformAttachment::SetCrossEntityBindingFlag(const bool value) noexcept
    {
        if (!m_attached)
            m_crossEntityBinding = value;
    }

    void ITransformAttachment::SetSystemUpdateBinding(const bool value) noexcept
    {
        if (!m_attached)
            m_systemUpdateBinding = value;
    }

    void ITransformAttachment::SetFloatingBinding(const bool value) noexcept
    {
        if (!m_attached)
            m_floatingBinding = value;
    }

    void ITransformAttachment::SetTransformUpdateCascaded(const bool value) noexcept
    {
        if (!m_attached)
            m_transformUpdateCascaded = value;
    }

} // namespace vanguard::entities
