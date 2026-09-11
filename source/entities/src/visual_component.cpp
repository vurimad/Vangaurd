#include <vanguard/entities/visual_component.hpp>
#include <vanguard/entities/rendering_runtime.hpp>

#include <vanguard/concurrency/thread.hpp>

namespace vanguard::entities
{
    namespace
    {
        [[nodiscard]] rendering::RenderProxyTransform FlattenTransform(const math::Matrix& matrix) noexcept
        {
            rendering::RenderProxyTransform result;
            result.row0[0] = matrix.X.X;
            result.row0[1] = matrix.X.Y;
            result.row0[2] = matrix.X.Z;
            result.row0[3] = matrix.W.X;
            result.row1[0] = matrix.Y.X;
            result.row1[1] = matrix.Y.Y;
            result.row1[2] = matrix.Y.Z;
            result.row1[3] = matrix.W.Y;
            result.row2[0] = matrix.Z.X;
            result.row2[1] = matrix.Z.Y;
            result.row2[2] = matrix.Z.Z;
            result.row2[3] = matrix.W.Z;
            return result;
        }

        [[nodiscard]] rendering::RenderProxyBounds FlattenBounds(const math::Box& bounds) noexcept
        {
            rendering::RenderProxyBounds result;
            result.minimum[0] = bounds.Min.X;
            result.minimum[1] = bounds.Min.Y;
            result.minimum[2] = bounds.Min.Z;
            result.maximum[0] = bounds.Max.X;
            result.maximum[1] = bounds.Max.Y;
            result.maximum[2] = bounds.Max.Z;
            return result;
        }
    } // namespace

    VisualComponent::VisualComponent() noexcept : m_visualScale(math::Vector3::ONES()), m_localBounds(math::Box::EMPTY()) {}

    bool VisualComponent::BindProxy(const VisualProxyBinding& binding) noexcept
    {
        if (!concurrency::IsMainThread() || !binding.IsValid() || m_binding.scenes != nullptr || !PrepareSpatialStateChange())
            return false;
        m_binding = binding;
        return true;
    }

    bool VisualComponent::UnbindProxy() noexcept
    {
        if (!concurrency::IsMainThread())
            return false;
        if (m_binding.scenes == nullptr)
            return true;
        if (IsTransformUpdateProcessing())
            return false;
        m_binding = {};
        m_teleport = false;
        return true;
    }

    bool VisualComponent::IsProxyBound() const noexcept
    {
        return m_binding.IsValid();
    }

    const VisualProxyBinding& VisualComponent::GetProxyBinding() const noexcept
    {
        return m_binding;
    }

    bool VisualComponent::SetVisualScale(const math::Vector3& scale) noexcept
    {
        if (!concurrency::IsMainThread() || !scale.IsOk())
            return false;
        if (m_visualScale == scale)
            return true;
        if (!PrepareSpatialStateChange())
            return false;
        m_visualScale = scale;
        return true;
    }

    const math::Vector3& VisualComponent::GetVisualScale() const noexcept
    {
        return m_visualScale;
    }

    bool VisualComponent::SetLocalBounds(const math::Box& bounds) noexcept
    {
        if (!concurrency::IsMainThread() || bounds.IsEmpty() || !bounds.IsOk())
            return false;
        if (m_localBounds == bounds)
            return true;
        if (!PrepareSpatialStateChange())
            return false;
        m_localBounds = bounds;
        return true;
    }

    const math::Box& VisualComponent::GetLocalBounds() const noexcept
    {
        return m_localBounds;
    }

    bool VisualComponent::MarkTeleported() noexcept
    {
        if (!concurrency::IsMainThread())
            return false;
        if (m_teleport)
            return true;
        if (!PrepareSpatialStateChange())
            return false;
        m_teleport = true;
        return true;
    }

    bool VisualComponent::IsProxyAdmissionPending() const noexcept
    {
        return m_admission.IsValid();
    }

    bool VisualComponent::BeginProxyAdmission(RenderingRuntime& runtime, rendering::RenderProxyDesc desc) noexcept
    {
        ProxyAdmissionHandle admission;
        const u64 producerGeneration = desc.producerGeneration;
        if (!runtime.QueueProxyAdmission(desc, {&CompleteAdmission, this}, admission))
            return false;
        return BeginAdmission(runtime, producerGeneration, admission);
    }

    bool VisualComponent::BeginProxyAdmission(RenderingRuntime& runtime, rendering::MeshProxyDesc desc) noexcept
    {
        ProxyAdmissionHandle admission;
        const u64 producerGeneration = desc.proxy.producerGeneration;
        if (!runtime.QueueProxyAdmission(desc, {&CompleteAdmission, this}, admission))
            return false;
        return BeginAdmission(runtime, producerGeneration, admission);
    }

    bool VisualComponent::BeginProxyAdmission(RenderingRuntime& runtime, rendering::LightProxyDesc desc) noexcept
    {
        ProxyAdmissionHandle admission;
        const u64 producerGeneration = desc.proxy.producerGeneration;
        if (!runtime.QueueProxyAdmission(desc, {&CompleteAdmission, this}, admission))
            return false;
        return BeginAdmission(runtime, producerGeneration, admission);
    }

    bool VisualComponent::BeginProxyAdmission(RenderingRuntime& runtime, rendering::DecalProxyDesc desc) noexcept
    {
        ProxyAdmissionHandle admission;
        const u64 producerGeneration = desc.proxy.producerGeneration;
        if (!runtime.QueueProxyAdmission(desc, {&CompleteAdmission, this}, admission))
            return false;
        return BeginAdmission(runtime, producerGeneration, admission);
    }

    bool VisualComponent::ReleaseVisualProxy(const bool retire) noexcept
    {
        if (!concurrency::IsMainThread())
            return false;
        if (m_runtime == nullptr)
            return !m_admission.IsValid() && !IsProxyBound();
        if (m_admission.IsValid())
        {
            if (!m_runtime->CancelProxyAdmission(m_admission))
                return false;
            m_admission = {};
            m_admissionProducerGeneration = 0;
            m_runtime = nullptr;
            return true;
        }
        if (!IsProxyBound())
        {
            m_runtime = nullptr;
            return true;
        }

        const VisualProxyBinding binding = m_binding;
        if (!UnbindProxy())
            return false;
        rendering::RenderSceneFailure failure;
        const bool succeeded = retire ? m_runtime->RetireProxy(binding.proxy, &failure) : m_runtime->DestroyProxy(binding.proxy, &failure);
        if (!succeeded)
        {
            static_cast<void>(BindProxy(binding));
            ReportFailure(failure);
            return false;
        }
        m_runtime = nullptr;
        m_admissionProducerGeneration = 0;
        return true;
    }

    void VisualComponent::OnUninitialize(const ComponentContext& context) noexcept
    {
        if (!ReleaseVisualProxy(true))
            static_cast<void>(ReleaseVisualProxy(false));
        OnVisualUninitialize(context);
    }

    void VisualComponent::OnDetach(const ComponentContext& context) noexcept
    {
        OnVisualDetach(context);
        if (!ReleaseVisualProxy(true))
            static_cast<void>(ReleaseVisualProxy(false));
    }

    void VisualComponent::OnTransformUpdated(math::Box& worldBounds) noexcept
    {
        math::Matrix localToWorld = GetLocalToWorldAsMatrix();
        localToWorld.SetScale33(m_visualScale);
        worldBounds = math::Box::EMPTY();
        if (!CalculateWorldBounds(localToWorld, worldBounds))
        {
            if (m_binding.IsValid())
            {
                const rendering::RenderSceneFailure failure{rendering::RenderSceneFailureCode::InvalidDescriptor, m_binding.proxy.scene,
                                                            m_binding.proxy, "visual component produced invalid world bounds"};
                ReportFailure(failure);
            }
            return;
        }
        if (!m_binding.IsValid())
            return;

        rendering::RenderProxyRelinkRequest request;
        request.proxy = m_binding.proxy;
        request.transform = FlattenTransform(localToWorld);
        request.bounds = FlattenBounds(worldBounds);
        request.producerGeneration = m_binding.producerGeneration;
        request.teleport = m_teleport;

        rendering::RenderSceneFailure failure;
        if (!m_binding.scenes->ScheduleRelink(request, &failure))
        {
            ReportFailure(failure);
            return;
        }
        m_teleport = false;
    }

    bool VisualComponent::CalculateWorldBounds(const math::Matrix& localToWorld, math::Box& worldBounds) const noexcept
    {
        if (m_localBounds.IsEmpty() || !m_localBounds.IsOk())
            return false;
        worldBounds = localToWorld.TransformBox(m_localBounds);
        return !worldBounds.IsEmpty() && worldBounds.IsOk();
    }

    void VisualComponent::OnProxyAdmissionFailed(const rendering::RenderSceneFailure&) noexcept {}
    bool VisualComponent::OnVisualProxyAdmitted() noexcept { return true; }

    void VisualComponent::OnVisualUninitialize(const ComponentContext&) noexcept {}

    void VisualComponent::OnVisualDetach(const ComponentContext&) noexcept {}

    bool VisualComponent::CompleteAdmission(const ProxyAdmissionHandle admission, const rendering::RenderProxyHandle proxy,
                                             const rendering::RenderSceneFailure* const failure, void* const userData) noexcept
    {
        auto* const component = static_cast<VisualComponent*>(userData);
        if (component == nullptr || component->m_runtime == nullptr || component->m_admission != admission)
            return false;
        component->m_admission = {};
        if (failure != nullptr)
        {
            component->m_runtime = nullptr;
            component->m_admissionProducerGeneration = 0;
            component->OnProxyAdmissionFailed(*failure);
            return true;
        }

        VisualProxyBinding binding;
        if (!component->m_runtime->GetVisualProxyBinding(proxy, component->m_admissionProducerGeneration, binding) ||
            !component->BindProxy(binding) || !component->OnVisualProxyAdmitted())
        {
            static_cast<void>(component->UnbindProxy());
            const rendering::RenderSceneFailure admissionFailure{rendering::RenderSceneFailureCode::InvalidState, proxy.scene, proxy,
                                                                  "visual component rejected an admitted RenderProxy"};
            component->m_runtime = nullptr;
            component->m_admissionProducerGeneration = 0;
            component->OnProxyAdmissionFailed(admissionFailure);
            return false;
        }
        return true;
    }

    bool VisualComponent::BeginAdmission(RenderingRuntime& runtime, const u64 producerGeneration, const ProxyAdmissionHandle admission) noexcept
    {
        if (!concurrency::IsMainThread() || m_runtime != nullptr || m_admission.IsValid() || IsProxyBound())
        {
            static_cast<void>(runtime.CancelProxyAdmission(admission));
            return false;
        }
        m_runtime = &runtime;
        m_admission = admission;
        m_admissionProducerGeneration = producerGeneration;
        return true;
    }

    bool VisualComponent::PrepareSpatialStateChange() noexcept
    {
        if (IsRegistered() && !ScheduleTransformUpdate())
            return false;
        ForceUpdateCallbackOnce();
        return true;
    }

    void VisualComponent::ReportFailure(const rendering::RenderSceneFailure& failure) const noexcept
    {
        if (m_binding.failures.IsValid())
            m_binding.failures.report({m_binding.proxy, failure}, m_binding.failures.userData);
    }
} // namespace vanguard::entities
