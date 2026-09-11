#include <vanguard/entities/static_mesh_component.hpp>

#include <vanguard/concurrency/thread.hpp>
#include <vanguard/diagnostics/diagnostics.hpp>
#include <vanguard/entities/component_registry.hpp>
#include <vanguard/entities/rendering_runtime.hpp>
#include <vanguard/meshes/mesh_resource.hpp>
#include <vanguard/system/assert.hpp>

#include <cstddef>
#include <cmath>

namespace vanguard::entities
{
    namespace
    {
        using Data = StaticMeshComponentData;
        const reflection::SchemaField Fields[]{
            reflection::MakeField("mesh", reflection::builtin::ResourceReference, reflection::ValueKind::ResourceReference,
                                  offsetof(Data, mesh), sizeof(Data::mesh), alignof(resources::ResourceReference), 1, 0,
                                  reflection::FieldFlags::Required),
            reflection::MakeField("scaleX", reflection::builtin::F32, reflection::ValueKind::F32, offsetof(Data, scaleX), sizeof(f32), alignof(f32)),
            reflection::MakeField("scaleY", reflection::builtin::F32, reflection::ValueKind::F32, offsetof(Data, scaleY), sizeof(f32), alignof(f32)),
            reflection::MakeField("scaleZ", reflection::builtin::F32, reflection::ValueKind::F32, offsetof(Data, scaleZ), sizeof(f32), alignof(f32)),
            reflection::MakeField("layerMask", reflection::builtin::U64, reflection::ValueKind::U64, offsetof(Data, layerMask), sizeof(u64), alignof(u64)),
            reflection::MakeField("visibilityMask", reflection::builtin::U32, reflection::ValueKind::U32, offsetof(Data, visibilityMask), sizeof(u32), alignof(u32)),
            reflection::MakeField("castsShadow", reflection::builtin::Bool, reflection::ValueKind::Bool, offsetof(Data, castsShadow), sizeof(bool), alignof(bool)),
            reflection::MakeField("receivesDecals", reflection::builtin::Bool, reflection::ValueKind::Bool, offsetof(Data, receivesDecals), sizeof(bool), alignof(bool))
        };
        const reflection::Schema Schema{StaticMeshComponentType, "vanguard.static_mesh_component", sizeof(Data), alignof(Data), 1, 1,
                                        Fields, static_cast<u32>(sizeof(Fields) / sizeof(Fields[0]))};
    }

    const reflection::Schema& GetStaticMeshComponentSchema() noexcept { return Schema; }

    bool RegisterStaticMeshComponent(ComponentRegistry& registry) noexcept
    {
        const reflection::Schema* const registered = reflection::FindSchema(Schema.id);
        if ((registered != nullptr && registered != &Schema) || (registered == nullptr && !reflection::RegisterSchema(Schema)))
            return false;
        return registry.RegisterObject<Data, StaticMeshComponent>(Schema);
    }

    StaticMeshComponent::StaticMeshComponent(const Data& data) noexcept : m_data(data) {}

    StaticMeshComponent::~StaticMeshComponent()
    {
        if (m_pending || m_rendering != nullptr || IsProxyAdmissionPending() || IsProxyBound())
            VG_FATAL("StaticMeshComponent destroyed before lifecycle teardown");
    }

    bool StaticMeshComponent::OnInitialize(const ComponentInitializeContext& context) noexcept
    {
        // Initialization can run on a worker. Renderer mutation starts at attachment.
        if (!m_data.mesh.IsValid() || m_data.mesh.ExpectedType() != meshes::MeshResourceType ||
            !std::isfinite(m_data.scaleX) || !std::isfinite(m_data.scaleY) || !std::isfinite(m_data.scaleZ) ||
            m_data.scaleX == 0.0f || m_data.scaleY == 0.0f || m_data.scaleZ == 0.0f)
            return false;
        m_resources = context.resources;
        m_priority = context.ioPriority;
        return true;
    }

    bool StaticMeshComponent::OnAttach(const ComponentContext& context) noexcept
    {
        m_rendering = static_cast<RenderingRuntime*>(context.world.GetSystem(RenderingRuntimeSystemId));
        if (m_rendering == nullptr || !SetVisualScale(math::Vector3(m_data.scaleX, m_data.scaleY, m_data.scaleZ)))
        {
            m_rendering = nullptr;
            return false;
        }
        if (IsEnabled() && !Start())
        {
            Stop();
            m_rendering = nullptr;
            return false;
        }
        return true;
    }

    bool StaticMeshComponent::Start() noexcept
    {
        if (m_rendering == nullptr || m_rendering->GetMeshResidency() == nullptr ||
            !m_rendering->GetMeshResidency()->IsInitialized() || m_rendering->GetMeshDrawPhases().Empty())
            return false;
        m_failure = nullptr;
        m_mesh = m_resources.TryAcquire(m_data.mesh);
        if (!m_mesh.IsValid())
        {
            m_request = m_resources.Request(m_data.mesh, m_priority);
            if (!m_request.IsValid())
                return false;
        }
        m_state = StaticMeshComponentState::Loading;
        return m_rendering->QueueMeshPreparation(*this);
    }

    void StaticMeshComponent::OnEnabled(const ComponentContext&, const bool enabled) noexcept
    {
        if (!enabled)
            Stop();
        else if (!Start())
            Fail("static mesh could not start preparation");
    }

    void StaticMeshComponent::Stop() noexcept
    {
        if (m_rendering != nullptr)
            m_rendering->CancelMeshPreparation(*this);
        // RenderScene owns an independent retained drawable after admission.
        // Releasing this component's copy cannot invalidate that scene ownership.
        if (!ReleaseVisualProxy(true) && !ReleaseVisualProxy(false))
        {
            Fail("static mesh proxy could not be released");
            return;
        }
        m_drawable.Reset();
        m_demand.Reset();
        m_mesh.Reset();
        m_request.Reset();
        m_state = StaticMeshComponentState::Inactive;
    }

    void StaticMeshComponent::OnVisualDetach(const ComponentContext&) noexcept
    {
        Stop();
        m_rendering = nullptr;
    }

    void StaticMeshComponent::OnVisualUninitialize(const ComponentContext&) noexcept
    {
        Stop();
        m_rendering = nullptr;
        m_resources = {};
    }

    void StaticMeshComponent::Fail(const char* const message) noexcept
    {
        m_state = StaticMeshComponentState::Failed;
        m_failure = message;
        if (m_rendering != nullptr)
        {
            m_rendering->CancelMeshPreparation(*this);
            m_rendering->ReportComponentFailure(message);
        }
        m_request.Reset();
        m_drawable.Reset();
        m_demand.Reset();
        m_mesh.Reset();
    }

    void StaticMeshComponent::OnProxyAdmissionFailed(const rendering::RenderSceneFailure& failure) noexcept
    {
        Fail(failure.message);
    }

    bool StaticMeshComponent::Progress() noexcept
    {
        VG_ASSERT_MSG(concurrency::IsMainThread(), "static mesh preparation requires the owner thread");
        if (!m_mesh.IsValid())
        {
            if (!m_request.HasFinished())
                return true;
            m_mesh = m_request.Acquire();
            m_request.Reset();
            if (!m_mesh.IsValid())
            {
                Fail("static mesh resource loading failed");
                return false;
            }
        }
        rendering::MeshResidencyManager& residency = *m_rendering->GetMeshResidency();
        if (!m_demand.IsValid())
        {
            if (m_mesh.GetType() != meshes::MeshResourceType || !residency.RequestMesh(m_mesh, m_demand))
            {
                Fail("static mesh residency demand failed");
                return false;
            }
            const auto* const mesh = static_cast<const meshes::MeshResourceObject*>(m_mesh.Get());
            const meshes::Bounds& bounds = mesh->GetMetadata().GetMeshBounds();
            if (!SetLocalBounds(math::Box(math::Vector3(bounds.minimum[0], bounds.minimum[1], bounds.minimum[2]),
                                          math::Vector3(bounds.maximum[0], bounds.maximum[1], bounds.maximum[2]))))
            {
                Fail("static mesh bounds could not be applied");
                return false;
            }
            m_state = StaticMeshComponentState::Preparing;
        }
        if (!m_drawable.IsValid())
        {
            rendering::MeshResidencyInfo info;
            if (!residency.GetInfo(m_demand.GetResidency(), info) || info.state == rendering::MeshResidencyState::Failed ||
                info.state == rendering::MeshResidencyState::Invalid ||
                info.state == rendering::MeshResidencyState::Retiring)
            {
                Fail("static mesh topology preparation failed");
                return false;
            }
            if (info.state != rendering::MeshResidencyState::RenderableTopologySubmitted)
                return true;
            rendering::MeshResidencyFailure failure;
            if (!residency.RequestDrawable(m_demand, m_rendering->GetMeshDrawPhases(), m_drawable, &failure))
            {
                // A rapid disable/re-enable can meet the preceding closure's
                // withdrawal. Keep this independent demand until retirement finishes.
                if (failure.code == rendering::MeshResidencyFailureCode::DrawPreparationNotReady)
                    return true;
                Fail("static mesh drawable request failed");
                return false;
            }
        }
        if (m_drawable.GetState() == rendering::MeshDrawableState::Failed || !m_drawable.IsValid())
        {
            Fail("static mesh drawable preparation failed");
            return false;
        }
        if (m_drawable.GetDrawable() == nullptr)
            return true;

        rendering::MeshProxyDesc desc;
        desc.proxy.scene = m_rendering->GetScene();
        desc.proxy.producerId = GetStableId();
        desc.proxy.producerGeneration = GetHandle().generation;
        desc.proxy.layerMask = m_data.layerMask;
        desc.proxy.visibilityMask = m_data.visibilityMask;
        desc.proxy.visibility = rendering::RenderProxyVisibilityFlags::Visible;
        if (m_data.castsShadow)
            desc.proxy.visibility = desc.proxy.visibility | rendering::RenderProxyVisibilityFlags::CastsShadow;
        if (m_data.receivesDecals)
            desc.proxy.visibility = desc.proxy.visibility | rendering::RenderProxyVisibilityFlags::ReceivesDecals;
        desc.proxy.debugName = "StaticMeshComponent";
        desc.mesh = m_data.mesh;
        desc.meshHandle = m_mesh;
        desc.drawable = &m_drawable;
        // BindProxy schedules a complete placed-component relink after admission.
        // Until then use conservative current bounds, not an empty origin box.
        math::Matrix transform = GetLocalToWorldAsMatrix();
        transform.SetScale33(GetVisualScale());
        const math::Box bounds = transform.TransformBox(GetLocalBounds());
        desc.proxy.bounds = {{bounds.Min.X, bounds.Min.Y, bounds.Min.Z}, {bounds.Max.X, bounds.Max.Y, bounds.Max.Z}};
        desc.proxy.transform = {{transform.X.X, transform.X.Y, transform.X.Z, transform.W.X},
                                {transform.Y.X, transform.Y.Y, transform.Y.Z, transform.W.Y},
                                {transform.Z.X, transform.Z.Y, transform.Z.Z, transform.W.Z}};
        if (!BeginProxyAdmission(*m_rendering, desc))
        {
            Fail("static mesh proxy admission queue is full");
            return false;
        }
        m_state = StaticMeshComponentState::Prepared;
        return false;
    }
}
