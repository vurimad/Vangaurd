#pragma once

#include <vanguard/entities/placed_component.hpp>
#include <vanguard/rendering/render_scene.hpp>

namespace vanguard::entities
{
    class RenderingRuntime;

    struct ProxyAdmissionHandle
    {
        u32 index = ~0u;
        u32 generation = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return index != ~0u && generation != 0;
        }

        [[nodiscard]] friend constexpr bool operator==(const ProxyAdmissionHandle&, const ProxyAdmissionHandle&) noexcept = default;
    };

    /// Called on the main thread when a deferred proxy has either entered RenderScene or
    /// permanently failed admission. Returning false for a successfully created proxy
    /// returns ownership to RenderingRuntime, which destroys it immediately.
    using CompleteProxyAdmission = bool (*)(ProxyAdmissionHandle admission, rendering::RenderProxyHandle proxy,
                                            const rendering::RenderSceneFailure* failure, void* userData) noexcept;

    struct ProxyAdmissionSink
    {
        CompleteProxyAdmission complete = nullptr;
        void* userData = nullptr;

        [[nodiscard]] bool IsValid() const noexcept
        {
            return complete != nullptr;
        }
    };

    struct VisualRelinkFailure
    {
        rendering::RenderProxyHandle proxy;
        rendering::RenderSceneFailure sceneFailure;
    };

    /// Called from transform worker jobs. Implementations must be bounded, allocation-free,
    /// non-blocking, and remain alive until the complete transform CPU tail has finished.
    using ReportVisualRelinkFailure = void (*)(const VisualRelinkFailure& failure, void* userData) noexcept;

    struct VisualRelinkFailureSink
    {
        ReportVisualRelinkFailure report = nullptr;
        void* userData = nullptr;

        [[nodiscard]] bool IsValid() const noexcept
        {
            return report != nullptr;
        }
    };

    struct VisualProxyBinding
    {
        rendering::RenderSceneManager* scenes = nullptr;
        rendering::RenderProxyHandle proxy;
        u64 producerGeneration = 0;
        VisualRelinkFailureSink failures;

        [[nodiscard]] bool IsValid() const noexcept
        {
            return scenes != nullptr && scenes->IsInitialized() && proxy.IsValid() && failures.IsValid();
        }
    };

    /// Stable runtime-side visual component using the placed-component relink path.
    /// Authored spatial state is changed before transform dispatch. OnTransformUpdated then
    /// derives one complete transform/bounds request and submits it directly after the
    /// runtime has admitted the proxy to RenderScene.
    ///
    /// Binding and authored-state mutations are main-thread structural operations and must
    /// not overlap transform processing. TransformRuntime enforces that ownership rule for
    /// every attached visual component.
    class VisualComponent : public IPlacedComponent
    {
    public:
        VisualComponent() noexcept;

        VisualComponent(const VisualComponent&) = delete;
        VisualComponent& operator=(const VisualComponent&) = delete;

        [[nodiscard]] bool BindProxy(const VisualProxyBinding& binding) noexcept;
        [[nodiscard]] bool UnbindProxy() noexcept;
        [[nodiscard]] bool IsProxyBound() const noexcept;
        [[nodiscard]] const VisualProxyBinding& GetProxyBinding() const noexcept;

        [[nodiscard]] bool SetVisualScale(const math::Vector3& scale) noexcept;
        [[nodiscard]] const math::Vector3& GetVisualScale() const noexcept;
        [[nodiscard]] bool SetLocalBounds(const math::Box& bounds) noexcept;
        [[nodiscard]] const math::Box& GetLocalBounds() const noexcept;
        [[nodiscard]] bool MarkTeleported() noexcept;
        [[nodiscard]] bool IsProxyAdmissionPending() const noexcept;

    protected:
        [[nodiscard]] bool BeginProxyAdmission(RenderingRuntime& runtime, rendering::RenderProxyDesc desc) noexcept;
        [[nodiscard]] bool BeginProxyAdmission(RenderingRuntime& runtime, rendering::MeshProxyDesc desc) noexcept;
        [[nodiscard]] bool BeginProxyAdmission(RenderingRuntime& runtime, rendering::LightProxyDesc desc) noexcept;
        [[nodiscard]] bool BeginProxyAdmission(RenderingRuntime& runtime, rendering::DecalProxyDesc desc) noexcept;
        /// Cancels pending admission or transfers an admitted proxy into runtime retirement.
        [[nodiscard]] bool ReleaseVisualProxy(bool retire = true) noexcept;

        void OnUninitialize(const ComponentContext& context) noexcept final;
        void OnDetach(const ComponentContext& context) noexcept final;
        void OnTransformUpdated(math::Box& worldBounds) noexcept override;

        /// Specialized visual components, such as lights, can replace the ordinary
        /// scaled-local-box calculation while retaining direct relink admission.
        [[nodiscard]] virtual bool CalculateWorldBounds(const math::Matrix& localToWorld, math::Box& worldBounds) const noexcept;
        virtual void OnProxyAdmissionFailed(const rendering::RenderSceneFailure& failure) noexcept;
        virtual void OnVisualUninitialize(const ComponentContext& context) noexcept;
        virtual void OnVisualDetach(const ComponentContext& context) noexcept;

    private:
        static bool CompleteAdmission(ProxyAdmissionHandle admission, rendering::RenderProxyHandle proxy,
                                      const rendering::RenderSceneFailure* failure, void* userData) noexcept;
        [[nodiscard]] bool BeginAdmission(RenderingRuntime& runtime, u64 producerGeneration, ProxyAdmissionHandle admission) noexcept;
        [[nodiscard]] bool PrepareSpatialStateChange() noexcept;
        void ReportFailure(const rendering::RenderSceneFailure& failure) const noexcept;

        VisualProxyBinding m_binding;
        RenderingRuntime* m_runtime = nullptr;
        ProxyAdmissionHandle m_admission;
        u64 m_admissionProducerGeneration = 0;
        math::Vector3 m_visualScale;
        math::Box m_localBounds;
        bool m_teleport = false;
    };
} // namespace vanguard::entities
