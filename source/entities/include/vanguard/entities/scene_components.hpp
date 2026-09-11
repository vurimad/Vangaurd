#pragma once

#include <vanguard/entities/visual_component.hpp>
#include <vanguard/rendering/render_camera.hpp>

namespace vanguard::entities
{
    class ComponentRegistry;
    inline constexpr reflection::SchemaTypeId LightComponentType = reflection::HashSchemaName("vanguard.light_component");
    inline constexpr reflection::SchemaTypeId CameraComponentType = reflection::HashSchemaName("vanguard.camera_component");

    struct LightComponentData
    {
        // Directional lights use global collection; range/cones apply to local lights.
        u8 kind = static_cast<u8>(rendering::RenderLightKind::Point);
        f32 red = 1.0f;
        f32 green = 1.0f;
        f32 blue = 1.0f;
        f32 intensity = 1.0f;
        f32 range = 10.0f;
        f32 innerConeRadians = 0.0f;
        f32 outerConeRadians = 0.7853981634f;
        bool castsShadow = false;
        u64 layerMask = ~0ull;
        u32 visibilityMask = ~0u;
    };

    struct CameraComponentData
    {
        f32 verticalFieldOfViewRadians = 1.0471975512f;
        f32 nearPlane = 0.1f;
        f32 farPlane = 1000.0f;
        u64 layerMask = ~0ull;
        u32 visibilityMask = ~0u;
    };

    [[nodiscard]] const reflection::Schema& GetLightComponentSchema() noexcept;
    [[nodiscard]] const reflection::Schema& GetCameraComponentSchema() noexcept;
    [[nodiscard]] bool RegisterLightComponent(ComponentRegistry& registry) noexcept;
    [[nodiscard]] bool RegisterCameraComponent(ComponentRegistry& registry) noexcept;

    class LightComponent final : public VisualComponent
    {
    public:
        explicit LightComponent(const LightComponentData& data) noexcept : m_data(data) {}
        ~LightComponent() override;
        [[nodiscard]] const LightComponentData& GetData() const noexcept { return m_data; }
        /// Main-thread transaction. Failure preserves authored data and proxy state.
        /// Pending admission consumes the latest values in its admission epilogue.
        [[nodiscard]] bool SetProperties(const LightComponentData& data) noexcept;
    protected:
        [[nodiscard]] bool OnInitialize(const ComponentInitializeContext& context) noexcept override;
        [[nodiscard]] bool OnAttach(const ComponentContext& context) noexcept override;
        void OnEnabled(const ComponentContext& context, bool enabled) noexcept override;
        void OnVisualDetach(const ComponentContext& context) noexcept override;
        void OnVisualUninitialize(const ComponentContext& context) noexcept override;
        [[nodiscard]] bool CalculateWorldBounds(const math::Matrix& localToWorld, math::Box& bounds) const noexcept override;
        [[nodiscard]] bool OnVisualProxyAdmitted() noexcept override;
        void OnProxyAdmissionFailed(const rendering::RenderSceneFailure& failure) noexcept override;
    private:
        [[nodiscard]] bool Admit() noexcept;
        [[nodiscard]] bool ApplyProperties(const LightComponentData& data) noexcept;
        LightComponentData m_data;
        RenderingRuntime* m_rendering = nullptr;
    };

    // Perspective, on-demand camera. Viewport selection uses GetCamera(); creating
    // the component does not silently replace a viewport's current camera.
    class CameraComponent final : public IPlacedComponent
    {
    public:
        explicit CameraComponent(const CameraComponentData& data) noexcept : m_data(data) {}
        ~CameraComponent() override;
        [[nodiscard]] rendering::RenderCameraHandle GetCamera() const noexcept { return m_camera; }
        [[nodiscard]] const CameraComponentData& GetData() const noexcept { return m_data; }
    protected:
        [[nodiscard]] bool OnInitialize(const ComponentInitializeContext& context) noexcept override;
        [[nodiscard]] bool OnAttach(const ComponentContext& context) noexcept override;
        void OnEnabled(const ComponentContext& context, bool enabled) noexcept override;
        void OnDetach(const ComponentContext& context) noexcept override;
        void OnUninitialize(const ComponentContext& context) noexcept override;
        void OnTransformUpdated(math::Box& worldBounds) noexcept override;
    private:
        friend class RenderingRuntime;
        [[nodiscard]] rendering::RenderCameraState MakeState() const noexcept;
        [[nodiscard]] bool ApplyTransform() noexcept;
        void ReleaseCamera() noexcept;
        CameraComponentData m_data;
        RenderingRuntime* m_rendering = nullptr;
        rendering::RenderCameraHandle m_camera;
        rendering::RenderPhaseSet m_phases;
        CameraComponent* m_nextDirty = nullptr;
        CameraComponent* m_previousDirty = nullptr;
        // One transform job owns a given component; main-thread access is after join.
        bool m_dirty = false;
    };
}
