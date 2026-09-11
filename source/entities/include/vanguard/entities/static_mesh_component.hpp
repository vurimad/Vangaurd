#pragma once

#include <vanguard/entities/visual_component.hpp>
#include <vanguard/rendering/mesh_residency.hpp>

namespace vanguard::entities
{
    class ComponentRegistry;

    inline constexpr reflection::SchemaTypeId StaticMeshComponentType = reflection::HashSchemaName("vanguard.static_mesh_component");

    // Serialized values decoded by ComponentRegistry::RegisterObject. Transform placement
    // remains in the existing prefab placement/attachment records.
    struct StaticMeshComponentData
    {
        resources::ResourceReference mesh;
        f32 scaleX = 1.0f;
        f32 scaleY = 1.0f;
        f32 scaleZ = 1.0f;
        u64 layerMask = ~0ull;
        u32 visibilityMask = ~0u;
        bool castsShadow = true;
        bool receivesDecals = true;
    };

    enum class StaticMeshComponentState : u8
    {
        Inactive,
        Loading,
        Preparing,
        Prepared,
        Failed
    };

    [[nodiscard]] const reflection::Schema& GetStaticMeshComponentSchema() noexcept;
    [[nodiscard]] bool RegisterStaticMeshComponent(ComponentRegistry& registry) noexcept;

    class StaticMeshComponent final : public VisualComponent
    {
    public:
        explicit StaticMeshComponent(const StaticMeshComponentData& data) noexcept;
        ~StaticMeshComponent() override;

        [[nodiscard]] const StaticMeshComponentData& GetData() const noexcept { return m_data; }
        [[nodiscard]] StaticMeshComponentState GetState() const noexcept { return m_state; }
        [[nodiscard]] const char* GetFailure() const noexcept { return m_failure; }
        // Owner-thread borrow. 9D must retain its own binding before a scene/frame
        // consumes the renderable; a prepared component alone is not scene publication.
        [[nodiscard]] const rendering::MeshDrawableBinding& GetDrawableBinding() const noexcept { return m_drawable; }

    protected:
        [[nodiscard]] bool OnInitialize(const ComponentInitializeContext& context) noexcept override;
        [[nodiscard]] bool OnAttach(const ComponentContext& context) noexcept override;
        void OnEnabled(const ComponentContext& context, bool enabled) noexcept override;
        void OnVisualDetach(const ComponentContext& context) noexcept override;
        void OnVisualUninitialize(const ComponentContext& context) noexcept override;
        void OnProxyAdmissionFailed(const rendering::RenderSceneFailure& failure) noexcept override;

    private:
        friend class RenderingRuntime;
        [[nodiscard]] bool Start() noexcept;
        void Stop() noexcept;
        // Returns true while work remains. Called only by RenderingRuntime's bounded queue.
        [[nodiscard]] bool Progress() noexcept;
        void Fail(const char* message) noexcept;

        StaticMeshComponentData m_data;
        ComponentResourceAccess m_resources;
        resources::LoadPriority m_priority = resources::LoadPriority::Normal;
        resources::PipelineRequest m_request;
        resources::ResourceHandle m_mesh;
        rendering::MeshDemandHandle m_demand;
        rendering::MeshDrawableBinding m_drawable;
        RenderingRuntime* m_rendering = nullptr;
        StaticMeshComponent* m_previousPending = nullptr;
        StaticMeshComponent* m_nextPending = nullptr;
        StaticMeshComponentState m_state = StaticMeshComponentState::Inactive;
        const char* m_failure = nullptr;
        bool m_pending = false;
    };
}
