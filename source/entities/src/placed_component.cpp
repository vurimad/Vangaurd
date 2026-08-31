#include <vanguard/entities/placed_component.hpp>
#include <vanguard/entities/transform_attachment.hpp>
#include <vanguard/entities/transform_system.hpp>

#include <vanguard/memory/pool.hpp>

namespace vanguard::entities
{
    IPlacedComponent::WorldTransformUpdater::WorldTransformUpdater(IPlacedComponent& component) noexcept : m_component(component) {}

    void IPlacedComponent::WorldTransformUpdater::ApplyLocalToWorld(const math::WorldTransform& parentLocalToWorld) noexcept
    {
        m_component.ApplyLocalToWorld(parentLocalToWorld);
    }

    void IPlacedComponent::WorldTransformUpdater::ApplyLocalToWorldNoParentTransform() noexcept
    {
        m_component.ApplyLocalToWorldNoParentTransform();
    }

    void IPlacedComponent::WorldTransformUpdater::ApplyLocalToWorldToSelfOnly(const math::WorldTransform& parentLocalToWorld) noexcept
    {
        m_component.ApplyLocalToWorldToSelfOnly(parentLocalToWorld);
    }

    void IPlacedComponent::WorldTransformUpdater::SetLocalToWorld(const math::WorldTransform& localToWorld) noexcept
    {
        m_component.SetLocalToWorld(localToWorld);
    }

    IPlacedComponent::IPlacedComponent() noexcept
        : m_outgoingAttachments(memory::pools::World::GetInstance()), m_cachedHierarchy(memory::pools::World::GetInstance()),
          m_localTransform(math::WorldTransform::IDENTITY()), m_localToWorld(math::WorldTransform::IDENTITY()), m_worldBounds(math::Box::EMPTY())
    {
    }

    IPlacedComponent::~IPlacedComponent() = default;

    math::Transform IPlacedComponent::GetLocalTransform() const noexcept
    {
        return m_localTransform._ToXForm();
    }

    const math::WorldTransform& IPlacedComponent::GetLocalTransformAsWorld() const noexcept
    {
        return m_localTransform;
    }

    math::Vector3 IPlacedComponent::GetLocalPosition() const noexcept
    {
        return m_localTransform.GetPosition().AsVector3();
    }

    const math::Quaternion& IPlacedComponent::GetLocalOrientation() const noexcept
    {
        return m_localTransform.GetOrientation();
    }

    math::Vector3 IPlacedComponent::GetLocalForward() const noexcept
    {
        return m_localTransform.GetForward();
    }

    math::Vector3 IPlacedComponent::GetLocalRight() const noexcept
    {
        return m_localTransform.GetRight();
    }

    math::Vector3 IPlacedComponent::GetLocalUp() const noexcept
    {
        return m_localTransform.GetUp();
    }

    const math::WorldTransform& IPlacedComponent::GetLocalToWorld() const noexcept
    {
        return m_localToWorld;
    }

    math::Matrix IPlacedComponent::GetLocalToWorldAsMatrix() const noexcept
    {
        return m_localToWorld.ToMatrix();
    }

    const math::WorldPosition& IPlacedComponent::GetWorldPosition() const noexcept
    {
        return m_localToWorld.GetPosition();
    }

    math::Quaternion IPlacedComponent::GetWorldOrientation() const noexcept
    {
        return m_localToWorld.GetOrientation();
    }

    math::Vector3 IPlacedComponent::GetWorldForward() const noexcept
    {
        return m_localToWorld.GetForward();
    }

    math::Vector3 IPlacedComponent::GetWorldRight() const noexcept
    {
        return m_localToWorld.GetRight();
    }

    math::Vector3 IPlacedComponent::GetWorldUp() const noexcept
    {
        return m_localToWorld.GetUp();
    }

    const math::Box& IPlacedComponent::GetWorldBounds() const noexcept
    {
        return m_worldBounds;
    }

    bool IPlacedComponent::HasDependentTransform() const noexcept
    {
        return m_parentTransform != nullptr;
    }

    ITransformAttachment* IPlacedComponent::GetParentTransform() const noexcept
    {
        return m_parentTransform;
    }

    bool IPlacedComponent::IsRegistered() const noexcept
    {
        return m_registered;
    }

    void IPlacedComponent::SetInitialPlacement(const math::Vector3& position, const math::Quaternion& rotation) noexcept
    {
        if (!m_registered && position.IsOk() && rotation.IsOk())
            m_localTransform = math::WorldTransform(position, rotation);
    }

    void IPlacedComponent::SetLocalPosition(const math::Vector3& localPosition) noexcept
    {
        if (!localPosition.IsOk() || (m_localTransform.GetPosition() - localPosition).IsAlmostZero() || !ScheduleTransformUpdate())
            return;
        m_localTransform.SetPosition(localPosition);
    }

    void IPlacedComponent::SetLocalOrientation(const math::Quaternion& localOrientation) noexcept
    {
        if (!localOrientation.IsOk() || m_localTransform.GetOrientation() == localOrientation || !ScheduleTransformUpdate())
            return;
        m_localTransform.SetOrientation(localOrientation);
    }

    void IPlacedComponent::SetLocalTransform(const math::Transform& localTransform) noexcept
    {
        SetLocalTransform(math::WorldTransform(localTransform));
    }

    void IPlacedComponent::SetLocalTransform(const math::Vector3& localPosition, const math::Quaternion& localOrientation) noexcept
    {
        SetLocalTransform(math::WorldTransform(localPosition, localOrientation));
    }

    void IPlacedComponent::SetLocalTransform(const math::WorldTransform& localTransform) noexcept
    {
        if (!localTransform.IsOk() || m_localTransform == localTransform || !ScheduleTransformUpdate())
            return;
        m_localTransform = localTransform;
    }

    void IPlacedComponent::SetLocalTransformNoScheduleUpdate(const math::WorldTransform& localTransform) noexcept
    {
        if (localTransform.IsOk() && m_localTransform != localTransform)
            m_localTransform = localTransform;
    }

    void IPlacedComponent::ForceUpdateCallback(const bool value) noexcept
    {
        m_forceUpdateCallback = value;
    }

    void IPlacedComponent::ForceUpdateCallbackOnce() noexcept
    {
        m_forceUpdateCallbackOnce = true;
    }

    void IPlacedComponent::RequestRefreshHierarchy() noexcept
    {
        if (m_system != nullptr && m_system->IsProcessing())
            return;
        m_refreshHierarchyCache = true;
        if (m_system != nullptr)
            RefreshHierarchyCache();
    }

    void IPlacedComponent::TransformUpdated() noexcept
    {
        OnTransformUpdated(m_worldBounds);
        if (m_system != nullptr)
            m_system->RecordTransformUpdated();
    }

    void IPlacedComponent::RefreshHierarchyCache() noexcept
    {
        m_cachedHierarchy.Clear();
        m_cachedHierarchy.Reserve(m_outgoingAttachments.Size());
        for (ITransformAttachment* const attachment : m_outgoingAttachments)
        {
            if (attachment == nullptr || !attachment->IsAttached() || !attachment->IsTransformUpdateCascaded() ||
                attachment->IsSystemUpdateBinding())
                continue;
            Relationship& relationship = m_cachedHierarchy.EmplaceBack();
            relationship.child = &attachment->GetDestination();
            relationship.attachment = attachment;
        }
        m_refreshHierarchyCache = false;
    }

    void IPlacedComponent::CascadeLocalToWorld(const bool forceCallback, const u32 depth) noexcept
    {
        if (m_system == nullptr || m_refreshHierarchyCache || depth >= m_system->GetMaximumHierarchyDepth())
            return;

        math::WorldTransform newLocalToWorld;
        for (Relationship& relationship : m_cachedHierarchy)
        {
            if (relationship.child == nullptr || relationship.attachment == nullptr || !relationship.child->m_registered ||
                !relationship.attachment->IsAttached())
            {
                m_refreshHierarchyCache = true;
                continue;
            }
            if (relationship.attachment->ComputeFinalTransform(this, newLocalToWorld))
            {
                WorldTransformUpdater updater(*relationship.child);
                updater.ApplyLocalToWorldToSelfOnly(newLocalToWorld);
            }
            if (forceCallback)
                relationship.child->ForceUpdateCallbackOnce();
            relationship.child->CascadeLocalToWorld(forceCallback, depth + 1u);
        }
    }

    bool IPlacedComponent::ScheduleTransformUpdate() noexcept
    {
        return m_system != nullptr && m_system->ScheduleTransformUpdate(*this);
    }

    bool IPlacedComponent::IsTransformUpdateProcessing() const noexcept
    {
        return m_system != nullptr && m_system->IsProcessing();
    }

    void IPlacedComponent::ApplyLocalToWorld(const math::WorldTransform& parentLocalToWorld) noexcept
    {
        const math::WorldTransform previous = m_localToWorld;
        m_localToWorld = parentLocalToWorld.TransformXForm(m_localTransform);
        const bool forceCallback = m_forceUpdateCallbackOnce || m_forceUpdateCallback;
        if (previous != m_localToWorld || forceCallback)
            TransformUpdated();
        CascadeLocalToWorld(forceCallback, 0u);
        m_forceUpdateCallbackOnce = false;
    }

    void IPlacedComponent::ApplyLocalToWorldNoParentTransform() noexcept
    {
        const math::WorldTransform previous = m_localToWorld;
        m_localToWorld = math::WorldTransform(m_localTransform);
        const bool forceCallback = m_forceUpdateCallbackOnce || m_forceUpdateCallback;
        if (previous != m_localToWorld || forceCallback)
            TransformUpdated();
        CascadeLocalToWorld(forceCallback, 0u);
        m_forceUpdateCallbackOnce = false;
    }

    void IPlacedComponent::ApplyLocalToWorldToSelfOnly(const math::WorldTransform& parentLocalToWorld) noexcept
    {
        const math::WorldTransform previous = m_localToWorld;
        m_localToWorld = parentLocalToWorld.TransformXForm(m_localTransform);
        const bool forceCallback = m_forceUpdateCallbackOnce || m_forceUpdateCallback;
        if (previous != m_localToWorld || forceCallback)
            TransformUpdated();
        m_forceUpdateCallbackOnce = false;
    }

    void IPlacedComponent::SetLocalToWorld(const math::WorldTransform& localToWorld) noexcept
    {
        m_localToWorld = localToWorld;
        TransformUpdated();
        CascadeLocalToWorld(true, 0u);
    }
} // namespace vanguard::entities
