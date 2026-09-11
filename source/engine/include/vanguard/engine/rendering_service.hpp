#pragma once

#include <vanguard/engine/frame_pipeline_service.hpp>
#include <vanguard/rendering/geometry_frame_work.hpp>
#include <vanguard/rendering/geometry_diagnostics.hpp>
#include <vanguard/rendering/gpu_scene_runtime.hpp>
#include <vanguard/rendering/material_materializer.hpp>
#include <vanguard/rendering/material_residency_runtime.hpp>
#include <vanguard/rendering/material_resource_resolver.hpp>
#include <vanguard/rendering/material_scene_binding.hpp>
#include <vanguard/rendering/mesh_residency.hpp>
#include <vanguard/rendering/pipeline_cache.hpp>
#include <vanguard/rendering/render_shader.hpp>
#include <vanguard/rendering/render_camera.hpp>
#include <vanguard/rendering/render_command_system.hpp>
#include <vanguard/rendering/render_flow_resource_allocator.hpp>
#include <vanguard/rendering/render_phase.hpp>
#include <vanguard/rendering/texture_residency_runtime.hpp>

namespace vanguard::engine
{
    enum class RenderingDeviceMode : u8
    {
        Disabled,
        Required
    };

    struct RenderingServiceConfig
    {
        RenderingDeviceMode deviceMode = RenderingDeviceMode::Disabled;
        rhi::BackendFactory backendFactory;
        rhi::DeviceParams device;
        rhi::DescriptorDomainDesc resourceDescriptors{rhi::DescriptorDomainKind::Resources, 262'144, 0, (1u << static_cast<u32>(rhi::ShaderStage::Count)) - 1u};
        rhi::DescriptorDomainDesc samplerDescriptors{rhi::DescriptorDomainKind::Samplers, 2'048, 0, (1u << static_cast<u32>(rhi::ShaderStage::Count)) - 1u};
        rendering::RenderFlowResourceAllocatorConfig frameResources;
        rendering::GeometryFrameWorkConfig geometryFrames;
        rendering::GpuSceneRuntimeConfig gpuScene;
        rendering::RenderPhaseRegistryConfig renderPhases;
        u32 maximumMaterialProgramLayouts = 65'536;
        rendering::MeshResidencyConfig meshResidency;
        rendering::TextureResidencyRuntimeConfig textureResidency;
        rendering::MaterialResourceResolverConfig materialResources;
        rendering::MaterialMaterializerConfig materialMaterializer;
        pipeline_cache::Config materialPipelines;
        rendering::MaterialResidencyRuntimeConfig materialResidency;
        rendering::MaterialSceneBindingConfig materialBindings;
        // Common material draw interface; startup descriptions are copied into
        // service-owned native layouts retained until all material pipelines stop.
        containers::ArraySpan<const rhi::BindingLayoutDesc> materialBindingLayouts;
        struct Shader
        {
            containers::StringView name;
            resources::ResourceReference resource;
        };
        struct Pipeline
        {
            containers::StringView name;
            resources::ResourceReference resource;
            pipelines::AttachmentSignature attachments;
            // Startup-only descriptions; entries remain valid through service initialization.
            containers::ArraySpan<const rhi::BindingLayoutDesc> bindingLayouts;
        };
        // Startup catalogs; backing storage must remain valid through service initialization.
        containers::ArraySpan<const Shader> rendererShaders;
        containers::ArraySpan<const Pipeline> rendererPipelines;
        // Explicit catalog takes precedence over boot discovery. Native startup
        // arrays and a resource catalog are mutually exclusive.
        resources::ResourceReference rendererBootstrap;
        bool usePackageRendererBootstrap = false;
        // Required by attachment-polymorphic output pipelines. Supplied by the
        // output owner; never inferred from a convenient default texture format.
        pipelines::AttachmentSignature rendererOutputAttachments;
        // Optional startup service that mounts the catalog sources before rendering initialization.
        application::ServiceId rendererCatalogSourceService = application::InvalidServiceId;
    };

    enum class RenderingRetirementFailureCode : u8
    {
        None,
        DeviceDisabled,
        MissingCutover,
        MaterialFailure,
        TextureFailure,
        MeshFailure,
        GpuSceneFailure
    };

    struct RenderingRetirementFailure
    {
        RenderingRetirementFailureCode code = RenderingRetirementFailureCode::None;
        const char* message = nullptr;
        rendering::MaterialResidencyRuntimeFailure materialFailure;
        rendering::TextureResidencyRuntimeFailure textureFailure;
        rendering::MeshResidencyFailure meshFailure;
        rendering::GpuSceneLifetimeFailure gpuSceneFailure;
    };

    struct RenderingResourceAllocatorDiagnostics
    {
        bool initialized = false;
        rendering::RenderFlowResourceAllocatorStats stats;
    };

    /// Stable extension points for render-side producers and viewport frame sources. A producer running in RenderUpdate
    /// must depend on RenderingUpdateFrameParticipantId. A frame source running in Render must depend on
    /// RenderingFrameTickParticipantId.
    inline constexpr FrameParticipantId RenderingUpdateFrameParticipantId = 0x72656e6475706401ull;
    inline constexpr FrameParticipantId RenderingFrameTickParticipantId = 0x72656e6466746b01ull;

    /// Engine-lifetime composition owner for renderer state, device/runtime, command-chain, and viewports. Renderer-side
    /// frame processing is delegated to rendering::FrameRenderer through RenderCommandSystem callbacks.
    class RenderingService : public application::Service
    {
    public:
        ~RenderingService() override = default;
        [[nodiscard]] virtual rhi::DescriptorDomainRef GetResourceDescriptorDomain() const noexcept = 0;
        [[nodiscard]] virtual rhi::DescriptorDomainRef GetSamplerDescriptorDomain() const noexcept = 0;

        [[nodiscard]] virtual rendering::RenderCommandSystem& GetCommands() noexcept = 0;
        [[nodiscard]] virtual const rendering::RenderCommandSystem& GetCommands() const noexcept = 0;
        [[nodiscard]] virtual rendering::ViewportManager& GetViewports() noexcept = 0;
        [[nodiscard]] virtual const rendering::ViewportManager& GetViewports() const noexcept = 0;
        [[nodiscard]] virtual rendering::RenderSceneManager& GetScenes() noexcept = 0;
        [[nodiscard]] virtual const rendering::RenderSceneManager& GetScenes() const noexcept = 0;
        [[nodiscard]] virtual const rendering::RenderCameraStorage& GetCameras() const noexcept = 0;
        [[nodiscard]] virtual rendering::GpuSceneRuntime& GetGpuScene() noexcept = 0;
        [[nodiscard]] virtual const rendering::GpuSceneRuntime& GetGpuScene() const noexcept = 0;
        [[nodiscard]] virtual rendering::MaterialProgramLayoutRegistry& GetMaterialProgramLayouts() noexcept = 0;
        [[nodiscard]] virtual const rendering::MaterialProgramLayoutRegistry& GetMaterialProgramLayouts() const noexcept = 0;
        [[nodiscard]] virtual const rendering::RenderPhaseRegistry& GetRenderPhases() const noexcept = 0;
        [[nodiscard]] virtual rendering::MeshResidencyManager& GetMeshResidency() noexcept = 0;
        [[nodiscard]] virtual const rendering::MeshResidencyManager& GetMeshResidency() const noexcept = 0;
        [[nodiscard]] virtual rendering::TextureResidencyRuntime& GetTextureResidency() noexcept = 0;
        [[nodiscard]] virtual const rendering::TextureResidencyRuntime& GetTextureResidency() const noexcept = 0;
        [[nodiscard]] virtual rendering::MaterialResourceResolver& GetMaterialResources() noexcept = 0;
        [[nodiscard]] virtual const rendering::MaterialResourceResolver& GetMaterialResources() const noexcept = 0;
        [[nodiscard]] virtual rendering::MaterialMaterializer& GetMaterialMaterializer() noexcept = 0;
        [[nodiscard]] virtual const rendering::MaterialMaterializer& GetMaterialMaterializer() const noexcept = 0;
        [[nodiscard]] virtual rendering::MaterialResidencyRuntime& GetMaterialResidency() noexcept = 0;
        [[nodiscard]] virtual const rendering::MaterialResidencyRuntime& GetMaterialResidency() const noexcept = 0;
        [[nodiscard]] virtual rendering::MaterialSceneBindingBridge& GetMaterialBindings() noexcept = 0;
        [[nodiscard]] virtual const rendering::MaterialSceneBindingBridge& GetMaterialBindings() const noexcept = 0;
        [[nodiscard]] virtual RenderingResourceAllocatorDiagnostics GetResourceAllocatorDiagnostics() const noexcept = 0;
        /// Single consumer on the host thread. Stop polling before renderer shutdown.
        [[nodiscard]] virtual rendering::GeometryDiagnosticsPoll PollGeometryDiagnostics(rendering::GeometryFrameDiagnostics& output, rhi::Failure* failure = nullptr) noexcept = 0;
        [[nodiscard]] virtual u64 GetDroppedGeometryDiagnosticSamples() const noexcept = 0;

        /// Seals every renderer-owned residency retirement against the same submitted queue cutover.
        /// The caller must supply real graphics, compute, and copy fences from its submission boundary.
        [[nodiscard]] virtual bool SealResidencyRetirements(const rhi::ResidencyFenceSet& safeAfter, RenderingRetirementFailure* failure = nullptr) noexcept = 0;

    protected:
        RenderingService() noexcept = default;
    };

    [[nodiscard]] RenderingService* FindRenderingService(application::EngineHost& host) noexcept;
    [[nodiscard]] RenderingService* FindRenderingService(application::ServiceContext& context) noexcept;
} // namespace vanguard::engine
