#pragma once

#include <vanguard/engine/frame_pipeline_service.hpp>
#include <vanguard/rendering/gpu_scene_runtime.hpp>
#include <vanguard/rendering/mesh_residency.hpp>
#include <vanguard/rendering/render_camera.hpp>
#include <vanguard/rendering/render_command_system.hpp>
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
        rendering::GpuSceneRuntimeConfig gpuScene;
        rendering::MeshResidencyConfig meshResidency;
        rendering::TextureResidencyRuntimeConfig textureResidency;
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

        [[nodiscard]] virtual rendering::RenderCommandSystem& GetCommands() noexcept = 0;
        [[nodiscard]] virtual const rendering::RenderCommandSystem& GetCommands() const noexcept = 0;
        [[nodiscard]] virtual rendering::ViewportManager& GetViewports() noexcept = 0;
        [[nodiscard]] virtual const rendering::ViewportManager& GetViewports() const noexcept = 0;
        [[nodiscard]] virtual rendering::RenderSceneManager& GetScenes() noexcept = 0;
        [[nodiscard]] virtual const rendering::RenderSceneManager& GetScenes() const noexcept = 0;
        [[nodiscard]] virtual const rendering::RenderCameraStorage& GetCameras() const noexcept = 0;
        [[nodiscard]] virtual rendering::GpuSceneRuntime& GetGpuScene() noexcept = 0;
        [[nodiscard]] virtual const rendering::GpuSceneRuntime& GetGpuScene() const noexcept = 0;
        [[nodiscard]] virtual rendering::MeshResidencyManager& GetMeshResidency() noexcept = 0;
        [[nodiscard]] virtual const rendering::MeshResidencyManager& GetMeshResidency() const noexcept = 0;
        [[nodiscard]] virtual rendering::TextureResidencyRuntime& GetTextureResidency() noexcept = 0;
        [[nodiscard]] virtual const rendering::TextureResidencyRuntime& GetTextureResidency() const noexcept = 0;

    protected:
        RenderingService() noexcept = default;
    };

    [[nodiscard]] RenderingService* FindRenderingService(application::EngineHost& host) noexcept;
    [[nodiscard]] RenderingService* FindRenderingService(application::ServiceContext& context) noexcept;
} // namespace vanguard::engine
