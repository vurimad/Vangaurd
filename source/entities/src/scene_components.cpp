#include <vanguard/entities/scene_components.hpp>

#include <vanguard/entities/component_registry.hpp>
#include <vanguard/concurrency/thread.hpp>
#include <vanguard/diagnostics/diagnostics.hpp>
#include <vanguard/entities/rendering_runtime.hpp>
#include <vanguard/rendering/mesh_residency.hpp>
#include <vanguard/rendering/render_command_system.hpp>
#include <vanguard/system/assert.hpp>

#include <cmath>
#include <cstddef>

namespace vanguard::entities
{
    namespace
    {
        using L = LightComponentData;
        using C = CameraComponentData;
        const reflection::SchemaField LightFields[]{
            reflection::MakeField("kind", reflection::builtin::U8, reflection::ValueKind::U8, offsetof(L, kind), sizeof(u8), alignof(u8)),
            reflection::MakeField("red", reflection::builtin::F32, reflection::ValueKind::F32, offsetof(L, red), sizeof(f32), alignof(f32)),
            reflection::MakeField("green", reflection::builtin::F32, reflection::ValueKind::F32, offsetof(L, green), sizeof(f32), alignof(f32)),
            reflection::MakeField("blue", reflection::builtin::F32, reflection::ValueKind::F32, offsetof(L, blue), sizeof(f32), alignof(f32)),
            reflection::MakeField("intensity", reflection::builtin::F32, reflection::ValueKind::F32, offsetof(L, intensity), sizeof(f32), alignof(f32)),
            reflection::MakeField("range", reflection::builtin::F32, reflection::ValueKind::F32, offsetof(L, range), sizeof(f32), alignof(f32)),
            reflection::MakeField("innerConeRadians", reflection::builtin::F32, reflection::ValueKind::F32, offsetof(L, innerConeRadians), sizeof(f32), alignof(f32)),
            reflection::MakeField("outerConeRadians", reflection::builtin::F32, reflection::ValueKind::F32, offsetof(L, outerConeRadians), sizeof(f32), alignof(f32)),
            reflection::MakeField("castsShadow", reflection::builtin::Bool, reflection::ValueKind::Bool, offsetof(L, castsShadow), sizeof(bool), alignof(bool)),
            reflection::MakeField("layerMask", reflection::builtin::U64, reflection::ValueKind::U64, offsetof(L, layerMask), sizeof(u64), alignof(u64), 2),
            reflection::MakeField("visibilityMask", reflection::builtin::U32, reflection::ValueKind::U32, offsetof(L, visibilityMask), sizeof(u32), alignof(u32), 2)
        };
        const reflection::SchemaField CameraFields[]{
            reflection::MakeField("verticalFieldOfViewRadians", reflection::builtin::F32, reflection::ValueKind::F32, offsetof(C, verticalFieldOfViewRadians), sizeof(f32), alignof(f32)),
            reflection::MakeField("nearPlane", reflection::builtin::F32, reflection::ValueKind::F32, offsetof(C, nearPlane), sizeof(f32), alignof(f32)),
            reflection::MakeField("farPlane", reflection::builtin::F32, reflection::ValueKind::F32, offsetof(C, farPlane), sizeof(f32), alignof(f32)),
            reflection::MakeField("layerMask", reflection::builtin::U64, reflection::ValueKind::U64, offsetof(C, layerMask), sizeof(u64), alignof(u64)),
            reflection::MakeField("visibilityMask", reflection::builtin::U32, reflection::ValueKind::U32, offsetof(C, visibilityMask), sizeof(u32), alignof(u32))
        };
        const reflection::Schema LightSchema{LightComponentType, "vanguard.light_component", sizeof(L), alignof(L), 2, 1, LightFields, 11};
        const reflection::Schema CameraSchema{CameraComponentType, "vanguard.camera_component", sizeof(C), alignof(C), 1, 1, CameraFields, 5};

        bool EnsureSchema(const reflection::Schema& schema) noexcept
        {
            const auto* const existing = reflection::FindSchema(schema.id);
            return existing != nullptr ? existing == &schema : reflection::RegisterSchema(schema);
        }
    }

    const reflection::Schema& GetLightComponentSchema() noexcept { return LightSchema; }
    const reflection::Schema& GetCameraComponentSchema() noexcept { return CameraSchema; }
    bool RegisterLightComponent(ComponentRegistry& registry) noexcept
    {
        return EnsureSchema(LightSchema) && registry.RegisterObject<L, LightComponent>(LightSchema);
    }
    bool RegisterCameraComponent(ComponentRegistry& registry) noexcept
    {
        return EnsureSchema(CameraSchema) && registry.RegisterObject<C, CameraComponent>(CameraSchema);
    }

    LightComponent::~LightComponent()
    {
        if (m_rendering != nullptr || IsProxyAdmissionPending() || IsProxyBound())
            VG_FATAL("LightComponent destroyed before lifecycle teardown");
    }

    bool LightComponent::OnInitialize(const ComponentInitializeContext&) noexcept
    {
        const f32 color[]{m_data.red, m_data.green, m_data.blue};
        return rendering::ValidRenderLight(static_cast<rendering::RenderLightKind>(m_data.kind), color,
            m_data.intensity, m_data.range, m_data.innerConeRadians, m_data.outerConeRadians);
    }

    bool LightComponent::OnAttach(const ComponentContext& context) noexcept
    {
        m_rendering = static_cast<RenderingRuntime*>(context.world.GetSystem(RenderingRuntimeSystemId));
        if (m_rendering == nullptr || !Admit())
        {
            m_rendering = nullptr;
            return false;
        }
        return true;
    }

    bool LightComponent::Admit() noexcept
    {
        rendering::LightProxyDesc desc;
        desc.proxy.scene = m_rendering->GetScene();
        desc.proxy.producerId = GetStableId();
        desc.proxy.producerGeneration = GetHandle().generation;
        desc.proxy.debugName = "LightComponent";
        desc.proxy.visibility = IsEnabled() ? rendering::RenderProxyVisibilityFlags::Visible : rendering::RenderProxyVisibilityFlags::None;
        desc.proxy.layerMask = m_data.layerMask;
        desc.proxy.visibilityMask = m_data.visibilityMask;
        desc.kind = static_cast<rendering::RenderLightKind>(m_data.kind);
        if (desc.kind == rendering::RenderLightKind::Directional)
            desc.proxy.spatialMode = rendering::RenderProxySpatialMode::Global;
        const math::Matrix transform = GetLocalToWorldAsMatrix();
        math::Box bounds;
        if (!CalculateWorldBounds(transform, bounds))
            return false;
        desc.proxy.bounds = {{bounds.Min.X, bounds.Min.Y, bounds.Min.Z}, {bounds.Max.X, bounds.Max.Y, bounds.Max.Z}};
        desc.proxy.transform = {{transform.X.X, transform.X.Y, transform.X.Z, transform.W.X},
                                {transform.Y.X, transform.Y.Y, transform.Y.Z, transform.W.Y},
                                {transform.Z.X, transform.Z.Y, transform.Z.Z, transform.W.Z}};
        desc.color[0] = m_data.red;
        desc.color[1] = m_data.green;
        desc.color[2] = m_data.blue;
        desc.intensity = m_data.intensity;
        desc.range = m_data.range;
        desc.innerConeRadians = m_data.innerConeRadians;
        desc.outerConeRadians = m_data.outerConeRadians;
        desc.castsShadow = m_data.castsShadow;
        return BeginProxyAdmission(*m_rendering, desc);
    }

    bool LightComponent::CalculateWorldBounds(const math::Matrix& transform, math::Box& bounds) const noexcept
    {
        const f32 radius = m_data.kind == static_cast<u8>(rendering::RenderLightKind::Directional) ? 0.1f : m_data.range;
        bounds = math::Box(transform.W.AsVector3(), radius);
        return bounds.IsOk() && !bounds.IsEmpty();
    }

    bool LightComponent::ApplyProperties(const LightComponentData& data) noexcept
    {
        rendering::LightProxyUpdate update;
        update.kind = static_cast<rendering::RenderLightKind>(data.kind);
        update.color[0] = data.red;
        update.color[1] = data.green;
        update.color[2] = data.blue;
        update.intensity = data.intensity;
        update.range = data.range;
        update.innerConeRadians = data.innerConeRadians;
        update.outerConeRadians = data.outerConeRadians;
        update.castsShadow = data.castsShadow;
        update.updateFiltering = true;
        update.visibility = IsEnabled() ? rendering::RenderProxyVisibilityFlags::Visible : rendering::RenderProxyVisibilityFlags::None;
        update.layerMask = data.layerMask;
        update.visibilityMask = data.visibilityMask;
        const auto& binding = GetProxyBinding();
        return binding.scenes->UpdateLightProxy(binding.proxy, update);
    }

    bool LightComponent::SetProperties(const LightComponentData& data) noexcept
    {
        const f32 color[]{data.red, data.green, data.blue};
        if (!concurrency::IsMainThread() || IsTransformUpdateProcessing() ||
            !rendering::ValidRenderLight(static_cast<rendering::RenderLightKind>(data.kind), color,
                data.intensity, data.range, data.innerConeRadians, data.outerConeRadians))
            return false;
        const bool influenceChanged = data.kind != m_data.kind || data.range != m_data.range;
        if (influenceChanged && IsRegistered() && !ScheduleTransformUpdate())
            return false;
        if (IsProxyBound() && !ApplyProperties(data))
            return false;
        m_data = data;
        if (influenceChanged)
            ForceUpdateCallbackOnce();
        return true;
    }

    bool LightComponent::OnVisualProxyAdmitted() noexcept
    {
        return ApplyProperties(m_data);
    }

    void LightComponent::OnProxyAdmissionFailed(const rendering::RenderSceneFailure&) noexcept
    {
        if (m_rendering != nullptr)
            m_rendering->ReportComponentFailure("light component proxy admission failed");
    }

    void LightComponent::OnEnabled(const ComponentContext&, const bool) noexcept
    {
        // Pending admission applies the latest enabled state in its epilogue.
        if (IsProxyBound() && !ApplyProperties(m_data))
            m_rendering->ReportComponentFailure("light component visibility update failed");
    }
    void LightComponent::OnVisualDetach(const ComponentContext&) noexcept { m_rendering = nullptr; }
    void LightComponent::OnVisualUninitialize(const ComponentContext&) noexcept { m_rendering = nullptr; }

    CameraComponent::~CameraComponent()
    {
        if (m_dirty || m_camera.IsValid() || m_rendering != nullptr)
            VG_FATAL("CameraComponent destroyed before lifecycle teardown");
    }

    bool CameraComponent::OnInitialize(const ComponentInitializeContext&) noexcept
    {
        return std::isfinite(m_data.verticalFieldOfViewRadians) && m_data.verticalFieldOfViewRadians > 0.0f &&
               m_data.verticalFieldOfViewRadians < 3.1415926536f && std::isfinite(m_data.nearPlane) && m_data.nearPlane > 0.0f &&
               std::isfinite(m_data.farPlane) && m_data.farPlane > m_data.nearPlane;
    }

    rendering::RenderCameraState CameraComponent::MakeState() const noexcept
    {
        rendering::RenderCameraState state;
        const math::Vector3 position = GetWorldPosition().AsVector3();
        const math::Quaternion rotation = GetWorldOrientation();
        state.pose.origin.localPosition[0] = position.X;
        state.pose.origin.localPosition[1] = position.Y;
        state.pose.origin.localPosition[2] = position.Z;
        state.pose.orientation[0] = rotation.i;
        state.pose.orientation[1] = rotation.j;
        state.pose.orientation[2] = rotation.k;
        state.pose.orientation[3] = rotation.r;
        state.projection.verticalFieldOfViewRadians = m_data.verticalFieldOfViewRadians;
        state.projection.nearPlane = m_data.nearPlane;
        state.projection.farPlane = m_data.farPlane;
        state.layerMask = m_data.layerMask;
        state.visibilityMask = m_data.visibilityMask;
        state.phases = m_phases;
        return state;
    }

    bool CameraComponent::OnAttach(const ComponentContext& context) noexcept
    {
        m_rendering = static_cast<RenderingRuntime*>(context.world.GetSystem(RenderingRuntimeSystemId));
        if (m_rendering == nullptr || m_rendering->GetCommands() == nullptr || m_rendering->GetRenderPhases() == nullptr)
        {
            m_rendering = nullptr;
            return false;
        }
        rendering::RenderCameraDesc desc;
        m_phases.Clear();
        for (const auto& phase : m_rendering->GetMeshDrawPhases())
        {
            if (!m_phases.Add(m_rendering->GetRenderPhases()->Find(phase.phase)))
            {
                m_rendering = nullptr;
                return false;
            }
        }
        desc.scene = m_rendering->GetScene();
        desc.name = "CameraComponent";
        desc.enabled = IsEnabled();
        desc.state = MakeState();
        if (!m_rendering->GetCommands()->RegisterCamera(desc, m_camera))
        {
            m_rendering = nullptr;
            return false;
        }
        ForceUpdateCallbackOnce();
        if (IsRegistered() && !ScheduleTransformUpdate())
        {
            ReleaseCamera();
            return false;
        }
        return true;
    }

    void CameraComponent::OnEnabled(const ComponentContext&, const bool enabled) noexcept
    {
        if (!m_rendering->GetCommands()->SetCameraEnabled(m_camera, enabled))
            m_rendering->ReportComponentFailure("camera enable transition failed");
    }

    void CameraComponent::OnTransformUpdated(math::Box& worldBounds) noexcept
    {
        worldBounds = math::Box(GetWorldPosition().AsVector3(), 0.1f);
        if (m_rendering != nullptr && m_camera.IsValid())
            m_rendering->QueueCameraTransform(*this);
    }

    bool CameraComponent::ApplyTransform() noexcept
    {
        return m_rendering->GetCommands()->UpdateCamera(m_camera, MakeState());
    }

    void CameraComponent::ReleaseCamera() noexcept
    {
        if (m_rendering == nullptr)
            return;
        m_rendering->CancelCameraTransform(*this);
        if (m_camera.IsValid() && !m_rendering->GetCommands()->UnregisterCamera(m_camera))
        {
            m_rendering->ReportComponentFailure("camera removal failed");
            return;
        }
        m_camera = {};
        m_rendering = nullptr;
    }
    void CameraComponent::OnDetach(const ComponentContext&) noexcept { ReleaseCamera(); }
    void CameraComponent::OnUninitialize(const ComponentContext&) noexcept { ReleaseCamera(); }
}
