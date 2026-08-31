#include <vanguard/entities/transform_system.hpp>
#include <vanguard/entities/placed_component.hpp>
#include <vanguard/entities/transform_attachment.hpp>

#include <vanguard/concurrency/atomic.hpp>
#include <vanguard/concurrency/thread.hpp>
#include <vanguard/containers/containers.hpp>
#include <vanguard/memory/memory.hpp>
#include <vanguard/memory/pool.hpp>

#include <new>

namespace vanguard::entities
{
    namespace
    {
        template <typename Type, typename... Args> [[nodiscard]] Type* AllocateTransformObject(Args&&... args) noexcept
        {
            memory::MemoryBlock block = memory::Allocate(memory::PoolId::World, sizeof(Type), alignof(Type));
            return block ? ::new (block.address) Type(static_cast<Args&&>(args)...) : nullptr;
        }

        template <typename Type> void DeleteTransformObject(Type* const object) noexcept
        {
            if (object == nullptr)
                return;
            object->~Type();
            memory::MemoryBlock block{object, sizeof(Type), memory::PoolId::World};
            memory::Free(block);
        }

        void ClearFailure(TransformFailure* const failure) noexcept
        {
            if (failure != nullptr)
                *failure = {};
        }

        [[nodiscard]] bool Fail(TransformFailure* const failure, const TransformFailureCode code, const char* const message,
                                IPlacedComponent* const component = nullptr, IPlacedComponent* const source = nullptr,
                                IPlacedComponent* const destination = nullptr) noexcept
        {
            if (failure != nullptr)
                *failure = {code, component, source, destination, message};
            return false;
        }

        template <typename Type> [[nodiscard]] bool RemovePointer(containers::DynamicArray<Type*>& values, Type* const value) noexcept
        {
            for (u32 index = 0; index < values.Size(); ++index)
            {
                if (values[index] != value)
                    continue;
                for (u32 move = index + 1u; move < values.Size(); ++move)
                    values[move - 1u] = values[move];
                static_cast<void>(values.PopBack());
                return true;
            }
            return false;
        }
    } // namespace

    struct TransformSystem::Impl
    {
        explicit Impl(const TransformSystemConfig& value) noexcept
            : components(memory::pools::World::GetInstance()), attachments(memory::pools::World::GetInstance()),
              scheduledRoots(memory::pools::World::GetInstance()), config(value)
        {
            components.Reserve(config.maximumComponents < 4096u ? config.maximumComponents : 4096u);
            attachments.Reserve(config.maximumAttachments < 4096u ? config.maximumAttachments : 4096u);
            scheduledRoots.Reserve(config.maximumScheduledRoots);
        }

        [[nodiscard]] IPlacedComponent* GetRoot(IPlacedComponent& component) const noexcept
        {
            IPlacedComponent* root = &component;
            for (u32 depth = 0; depth < config.maximumHierarchyDepth; ++depth)
            {
                ITransformAttachment* const parent = root->m_parentTransform;
                if (parent == nullptr || parent->IsSystemUpdateBinding() || !parent->IsTransformUpdateCascaded())
                    return root;
                root = &parent->GetSource();
            }
            return nullptr;
        }

        [[nodiscard]] bool Schedule(IPlacedComponent& component) noexcept
        {
            if (processing.GetValue() != 0 || !component.m_registered)
                return false;
            IPlacedComponent* const root = GetRoot(component);
            if (root == nullptr)
                return false;
            if (root->m_scheduled)
            {
                static_cast<void>(redundantSchedules.Increment());
                return true;
            }
            if (scheduledRoots.Size() == config.maximumScheduledRoots)
                return false;
            root->m_scheduled = true;
            scheduledRoots.PushBack(root);
            scheduledRootCount.SetValue(scheduledRoots.Size());
            return true;
        }

        void Cancel(IPlacedComponent& component) noexcept
        {
            if (component.m_scheduled && RemovePointer(scheduledRoots, &component))
            {
                component.m_scheduled = false;
                scheduledRootCount.SetValue(scheduledRoots.Size());
            }
        }

        void ApplyScheduledRoot(const u32 index) noexcept
        {
            IPlacedComponent* const root = index < scheduledRoots.Size() ? scheduledRoots[index] : nullptr;
            if (root == nullptr || !root->m_registered)
                return;

            IPlacedComponent::WorldTransformUpdater updater(*root);
            ITransformAttachment* const parent = root->m_parentTransform;
            if (parent == nullptr)
            {
                updater.ApplyLocalToWorldNoParentTransform();
                return;
            }
            math::WorldTransform newLocalToWorld;
            if (parent->ComputeFinalTransform(&parent->GetSource(), newLocalToWorld))
                updater.ApplyLocalToWorld(newLocalToWorld);
        }

        void CompleteUpdates() noexcept
        {
            for (IPlacedComponent* const root : scheduledRoots)
                if (root != nullptr)
                    root->m_scheduled = false;
            scheduledRoots.Clear();
            scheduledRootCount.SetValue(0);
            processing.SetValue(0);
        }

        containers::DynamicArray<IPlacedComponent*> components;
        containers::DynamicArray<ITransformAttachment*> attachments;
        containers::DynamicArray<IPlacedComponent*> scheduledRoots;
        TransformSystemConfig config;
        concurrency::Atomic<u32> processing{0};
        concurrency::Atomic<u32> scheduledRootCount{0};
        concurrency::Atomic<u64> dispatchedRoots{0};
        concurrency::Atomic<u64> updatedComponents{0};
        concurrency::Atomic<u64> redundantSchedules{0};
        const char* updateJobName = "EntityTransforms/ApplyScheduledTransformUpdates";
    };

    TransformSystem::~TransformSystem()
    {
        if (m_impl != nullptr)
            static_cast<void>(Shutdown());
    }

    bool TransformSystem::Initialize(const TransformSystemConfig& config, TransformFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl != nullptr)
            return Fail(failure, TransformFailureCode::AlreadyInitialized, "TransformSystem is already initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, TransformFailureCode::WrongThread, "TransformSystem initialization must run on the main thread");
        if (config.maximumComponents == 0 || config.maximumAttachments >= config.maximumComponents || config.maximumScheduledRoots == 0 ||
            config.maximumScheduledRoots > config.maximumComponents || config.maximumHierarchyDepth == 0)
            return Fail(failure, TransformFailureCode::InvalidConfiguration, "invalid TransformSystem configuration");
        m_impl = AllocateTransformObject<Impl>(config);
        if (m_impl == nullptr)
            return Fail(failure, TransformFailureCode::CapacityExceeded, "TransformSystem allocation failed");
        return true;
    }

    bool TransformSystem::Shutdown(TransformFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return true;
        if (!concurrency::IsMainThread())
            return Fail(failure, TransformFailureCode::WrongThread, "TransformSystem shutdown must run on the main thread");
        if (m_impl->processing.GetValue() != 0)
            return Fail(failure, TransformFailureCode::Busy, "TransformSystem update work is still processing");
        if (!m_impl->components.Empty() || !m_impl->attachments.Empty())
            return Fail(failure, TransformFailureCode::Busy, "TransformSystem shutdown requires all components and attachments to be removed");
        DeleteTransformObject(m_impl);
        m_impl = nullptr;
        return true;
    }

    bool TransformSystem::IsInitialized() const noexcept
    {
        return m_impl != nullptr;
    }

    bool TransformSystem::IsProcessing() const noexcept
    {
        return m_impl != nullptr && m_impl->processing.GetValue() != 0;
    }

    bool TransformSystem::RegisterComponent(IPlacedComponent& component, TransformFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, TransformFailureCode::NotInitialized, "TransformSystem is not initialized", &component);
        if (!concurrency::IsMainThread())
            return Fail(failure, TransformFailureCode::WrongThread, "placed components must register on the main thread", &component);
        if (m_impl->processing.GetValue() != 0)
            return Fail(failure, TransformFailureCode::Busy, "placed components cannot register during transform processing", &component);
        if (component.m_registered || component.m_system != nullptr)
            return Fail(failure, TransformFailureCode::InvalidComponent, "placed component is already registered", &component);
        if (m_impl->components.Size() == m_impl->config.maximumComponents)
            return Fail(failure, TransformFailureCode::CapacityExceeded, "maximum placed-component count exceeded", &component);

        component.m_system = this;
        component.m_registered = true;
        component.m_localToWorld = component.m_localTransform;
        component.m_refreshHierarchyCache = true;
        component.m_forceUpdateCallbackOnce = true;
        component.RefreshHierarchyCache();
        m_impl->components.PushBack(&component);
        if (m_impl->Schedule(component))
            return true;
        static_cast<void>(m_impl->components.PopBack());
        component.m_system = nullptr;
        component.m_registered = false;
        return Fail(failure, TransformFailureCode::CapacityExceeded, "transform root scheduling capacity exceeded", &component);
    }

    bool TransformSystem::UnregisterComponent(IPlacedComponent& component, TransformFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, TransformFailureCode::NotInitialized, "TransformSystem is not initialized", &component);
        if (!concurrency::IsMainThread())
            return Fail(failure, TransformFailureCode::WrongThread, "placed components must unregister on the main thread", &component);
        if (m_impl->processing.GetValue() != 0)
            return Fail(failure, TransformFailureCode::Busy, "placed components cannot unregister during transform processing", &component);
        if (!component.m_registered || component.m_system != this)
            return Fail(failure, TransformFailureCode::InvalidComponent, "placed component is not registered with this system", &component);
        if (component.m_parentTransform != nullptr || !component.m_outgoingAttachments.Empty())
            return Fail(failure, TransformFailureCode::InvalidAttachment, "placed component must be detached before unregistering", &component);
        if (!RemovePointer(m_impl->components, &component))
            return Fail(failure, TransformFailureCode::InvalidComponent, "placed-component registry is inconsistent", &component);
        m_impl->Cancel(component);
        component.m_cachedHierarchy.Clear();
        component.m_system = nullptr;
        component.m_registered = false;
        return true;
    }

    bool TransformSystem::Attach(ITransformAttachment& attachment, TransformFailure* const failure) noexcept
    {
        ClearFailure(failure);
        IPlacedComponent& source = attachment.GetSource();
        IPlacedComponent& destination = attachment.GetDestination();
        if (m_impl == nullptr)
            return Fail(failure, TransformFailureCode::NotInitialized, "TransformSystem is not initialized", nullptr, &source, &destination);
        if (!concurrency::IsMainThread())
            return Fail(failure, TransformFailureCode::WrongThread, "transform attachments must register on the main thread", nullptr, &source,
                        &destination);
        if (m_impl->processing.GetValue() != 0)
            return Fail(failure, TransformFailureCode::Busy, "transform attachments cannot change during transform processing", nullptr, &source,
                        &destination);
        if (attachment.m_attached || &source == &destination || !source.m_registered || !destination.m_registered || source.m_system != this ||
            destination.m_system != this || destination.m_parentTransform != nullptr)
            return Fail(failure, TransformFailureCode::InvalidAttachment, "invalid transform attachment", nullptr, &source, &destination);
        if (m_impl->attachments.Size() == m_impl->config.maximumAttachments)
            return Fail(failure, TransformFailureCode::CapacityExceeded, "maximum transform-attachment count exceeded", nullptr, &source, &destination);

        IPlacedComponent* ancestor = &source;
        for (u32 depth = 0; depth < m_impl->config.maximumHierarchyDepth; ++depth)
        {
            if (ancestor == &destination)
                return Fail(failure, TransformFailureCode::AttachmentCycle, "transform attachment would create a cycle", nullptr, &source, &destination);
            if (ancestor->m_parentTransform == nullptr)
                break;
            ancestor = &ancestor->m_parentTransform->GetSource();
            if (depth + 1u == m_impl->config.maximumHierarchyDepth)
                return Fail(failure, TransformFailureCode::AttachmentCycle, "transform attachment exceeds the hierarchy-depth bound", nullptr, &source,
                            &destination);
        }

        m_impl->Cancel(destination);
        source.m_outgoingAttachments.PushBack(&attachment);
        destination.m_parentTransform = &attachment;
        attachment.m_attached = true;
        source.RefreshHierarchyCache();
        m_impl->attachments.PushBack(&attachment);
        if (m_impl->Schedule(destination))
            return true;

        static_cast<void>(m_impl->attachments.PopBack());
        attachment.m_attached = false;
        destination.m_parentTransform = nullptr;
        static_cast<void>(RemovePointer(source.m_outgoingAttachments, &attachment));
        source.RefreshHierarchyCache();
        static_cast<void>(m_impl->Schedule(destination));
        return Fail(failure, TransformFailureCode::CapacityExceeded, "transform root scheduling capacity exceeded", nullptr, &source, &destination);
    }

    bool TransformSystem::Detach(ITransformAttachment& attachment, TransformFailure* const failure) noexcept
    {
        ClearFailure(failure);
        IPlacedComponent& source = attachment.GetSource();
        IPlacedComponent& destination = attachment.GetDestination();
        if (m_impl == nullptr)
            return Fail(failure, TransformFailureCode::NotInitialized, "TransformSystem is not initialized", nullptr, &source, &destination);
        if (!concurrency::IsMainThread())
            return Fail(failure, TransformFailureCode::WrongThread, "transform attachments must detach on the main thread", nullptr, &source,
                        &destination);
        if (m_impl->processing.GetValue() != 0)
            return Fail(failure, TransformFailureCode::Busy, "transform attachments cannot change during transform processing", nullptr, &source,
                        &destination);
        if (!attachment.m_attached || destination.m_parentTransform != &attachment || source.m_system != this || destination.m_system != this)
            return Fail(failure, TransformFailureCode::InvalidAttachment, "transform attachment is not registered with this system", nullptr, &source,
                        &destination);
        if (!destination.m_scheduled && m_impl->scheduledRoots.Size() == m_impl->config.maximumScheduledRoots)
            return Fail(failure, TransformFailureCode::CapacityExceeded, "transform root scheduling capacity exceeded", &destination, &source,
                        &destination);
        if (!RemovePointer(source.m_outgoingAttachments, &attachment) || !RemovePointer(m_impl->attachments, &attachment))
            return Fail(failure, TransformFailureCode::InvalidAttachment, "transform-attachment registry is inconsistent", nullptr, &source,
                        &destination);

        source.RefreshHierarchyCache();
        destination.m_parentTransform = nullptr;
        attachment.m_attached = false;
        static_cast<void>(m_impl->Schedule(destination));
        return true;
    }

    bool TransformSystem::DispatchUpdates(jobs::Builder& builder, TransformUpdateDispatch& dispatch, TransformFailure* const failure) noexcept
    {
        ClearFailure(failure);
        dispatch = {};
        if (m_impl == nullptr)
            return Fail(failure, TransformFailureCode::NotInitialized, "TransformSystem is not initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, TransformFailureCode::WrongThread, "transform updates must dispatch on the main thread");
        if (m_impl->processing.GetValue() != 0)
            return Fail(failure, TransformFailureCode::Busy, "transform updates are already processing");
        if (!builder.IsValid())
            return Fail(failure, TransformFailureCode::DispatchFailure, "transform updates require a valid job builder");
        dispatch.roots = m_impl->scheduledRoots.Size();
        if (dispatch.roots == 0)
            return true;

        m_impl->processing.SetValue(1);
        jobs::ParallelTask updates = jobs::ParallelTask::Create(
            [impl = m_impl](const u32 index, const jobs::JobContext&) noexcept { impl->ApplyScheduledRoot(index); });
        jobs::Task cleanup = jobs::Task::Create([impl = m_impl](const jobs::JobContext&) noexcept { impl->CompleteUpdates(); });
        jobs::JobName updateJobName(m_impl->updateJobName);
        if (!updates || !cleanup || !builder.DispatchParallel(updateJobName, dispatch.roots, static_cast<jobs::ParallelTask&&>(updates),
                                                               static_cast<jobs::Task&&>(cleanup), 1u, jobs::Fence::Full))
        {
            m_impl->processing.SetValue(0);
            return Fail(failure, TransformFailureCode::DispatchFailure, "transform update job dispatch failed");
        }
        static_cast<void>(m_impl->dispatchedRoots.ExchangeAdd(dispatch.roots));
        dispatch.dispatched = true;
        return true;
    }

    bool TransformSystem::ScheduleTransformUpdate(IPlacedComponent& component) noexcept
    {
        return m_impl != nullptr && m_impl->Schedule(component);
    }

    void TransformSystem::CancelTransformUpdate(IPlacedComponent& component) noexcept
    {
        if (m_impl != nullptr && m_impl->processing.GetValue() == 0)
            m_impl->Cancel(component);
    }

    void TransformSystem::RecordTransformUpdated() noexcept
    {
        if (m_impl != nullptr)
            static_cast<void>(m_impl->updatedComponents.Increment());
    }

    u32 TransformSystem::GetMaximumHierarchyDepth() const noexcept
    {
        return m_impl != nullptr ? m_impl->config.maximumHierarchyDepth : 0u;
    }

    TransformSystemStats TransformSystem::GetStats() const noexcept
    {
        TransformSystemStats result;
        if (m_impl == nullptr)
            return result;
        result.components = m_impl->components.Size();
        result.attachments = m_impl->attachments.Size();
        result.scheduledRoots = m_impl->scheduledRootCount.GetValue();
        result.dispatchedRoots = m_impl->dispatchedRoots.GetValue();
        result.updatedComponents = m_impl->updatedComponents.GetValue();
        result.redundantSchedules = m_impl->redundantSchedules.GetValue();
        result.processing = m_impl->processing.GetValue() != 0;
        result.initialized = true;
        return result;
    }
} // namespace vanguard::entities
