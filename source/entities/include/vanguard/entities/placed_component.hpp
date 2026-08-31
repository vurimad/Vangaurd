#pragma once

#include <vanguard/containers/containers.hpp>
#include <vanguard/entities/component.hpp>
#include <vanguard/entities/transform_system.hpp>

#include <redMathPublic.h>
#include <vector3.h>
#include <vector4.h>
#include <worldTransform.h>

namespace vanguard::entities
{
    class ITransformAttachment;

    class IPlacedComponent : public Component
    {
    public:
        class WorldTransformUpdater
        {
        public:
            explicit WorldTransformUpdater(IPlacedComponent& component) noexcept;

            void ApplyLocalToWorld(const math::WorldTransform& parentLocalToWorld) noexcept;
            void ApplyLocalToWorldNoParentTransform() noexcept;
            void ApplyLocalToWorldToSelfOnly(const math::WorldTransform& parentLocalToWorld) noexcept;
            void SetLocalToWorld(const math::WorldTransform& localToWorld) noexcept;

        private:
            IPlacedComponent& m_component;
        };

        IPlacedComponent() noexcept;
        virtual ~IPlacedComponent();

        IPlacedComponent(const IPlacedComponent&) = delete;
        IPlacedComponent& operator=(const IPlacedComponent&) = delete;

        [[nodiscard]] IPlacedComponent* GetPlacedComponent() noexcept final
        {
            return this;
        }

        [[nodiscard]] const IPlacedComponent* GetPlacedComponent() const noexcept final
        {
            return this;
        }

        [[nodiscard]] math::Transform GetLocalTransform() const noexcept;
        [[nodiscard]] const math::WorldTransform& GetLocalTransformAsWorld() const noexcept;
        [[nodiscard]] math::Vector3 GetLocalPosition() const noexcept;
        [[nodiscard]] const math::Quaternion& GetLocalOrientation() const noexcept;
        [[nodiscard]] math::Vector3 GetLocalForward() const noexcept;
        [[nodiscard]] math::Vector3 GetLocalRight() const noexcept;
        [[nodiscard]] math::Vector3 GetLocalUp() const noexcept;
        [[nodiscard]] const math::WorldTransform& GetLocalToWorld() const noexcept;
        [[nodiscard]] math::Matrix GetLocalToWorldAsMatrix() const noexcept;
        [[nodiscard]] const math::WorldPosition& GetWorldPosition() const noexcept;
        [[nodiscard]] math::Quaternion GetWorldOrientation() const noexcept;
        [[nodiscard]] math::Vector3 GetWorldForward() const noexcept;
        [[nodiscard]] math::Vector3 GetWorldRight() const noexcept;
        [[nodiscard]] math::Vector3 GetWorldUp() const noexcept;
        [[nodiscard]] const math::Box& GetWorldBounds() const noexcept;

        [[nodiscard]] bool HasDependentTransform() const noexcept;
        [[nodiscard]] ITransformAttachment* GetParentTransform() const noexcept;
        [[nodiscard]] bool IsRegistered() const noexcept;

        void SetInitialPlacement(const math::Vector3& position, const math::Quaternion& rotation) noexcept;
        void SetLocalPosition(const math::Vector3& localPosition) noexcept;
        void SetLocalOrientation(const math::Quaternion& localOrientation) noexcept;
        void SetLocalTransform(const math::Transform& localTransform) noexcept;
        void SetLocalTransform(const math::Vector3& localPosition, const math::Quaternion& localOrientation) noexcept;
        void SetLocalTransform(const math::WorldTransform& localTransform) noexcept;
        void SetLocalTransformNoScheduleUpdate(const math::WorldTransform& localTransform) noexcept;
        void ForceUpdateCallback(bool value = true) noexcept;
        void ForceUpdateCallbackOnce() noexcept;
        void RequestRefreshHierarchy() noexcept;

    protected:
        virtual void OnTransformUpdated(math::Box& worldBounds) noexcept = 0;

        /// Schedules the root of this component's transform hierarchy. Derived runtime
        /// components use this when non-transform spatial state (for example bounds or
        /// visual scale) changes and therefore requires another transform callback.
        [[nodiscard]] bool ScheduleTransformUpdate() noexcept;
        [[nodiscard]] bool IsTransformUpdateProcessing() const noexcept;

    private:
        friend class ITransformAttachment;
        friend class TransformSystem;
        friend struct TransformSystem::Impl;

        struct Relationship
        {
            IPlacedComponent* child = nullptr;
            ITransformAttachment* attachment = nullptr;
        };

        void TransformUpdated() noexcept;
        void RefreshHierarchyCache() noexcept;
        void CascadeLocalToWorld(bool forceCallback, u32 depth) noexcept;
        void ApplyLocalToWorld(const math::WorldTransform& parentLocalToWorld) noexcept;
        void ApplyLocalToWorldNoParentTransform() noexcept;
        void ApplyLocalToWorldToSelfOnly(const math::WorldTransform& parentLocalToWorld) noexcept;
        void SetLocalToWorld(const math::WorldTransform& localToWorld) noexcept;

        TransformSystem* m_system = nullptr;
        ITransformAttachment* m_parentTransform = nullptr;
        containers::DynamicArray<ITransformAttachment*> m_outgoingAttachments;
        containers::DynamicArray<Relationship> m_cachedHierarchy;
        math::WorldTransform m_localTransform;
        math::WorldTransform m_localToWorld;
        math::Box m_worldBounds;
        bool m_forceUpdateCallback = false;
        bool m_forceUpdateCallbackOnce = true;
        bool m_refreshHierarchyCache = true;
        bool m_scheduled = false;
        bool m_registered = false;
    };
} // namespace vanguard::entities
