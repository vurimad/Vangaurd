#pragma once

#include <vanguard/rendering/gpu_scene_runtime.hpp>
#include <vanguard/rendering/render_command_system.hpp>

namespace vanguard::rendering
{
    /// Renderer-side frame driver. Owns the ordered preparation that occurs after RenderCommandSystem enters
    /// the renderer CPU chain and before render-graph jobs are dispatched.
    class FrameRenderer final
    {
    public:
        FrameRenderer(RenderCameraStorage& cameras, GpuSceneRuntime& gpuScene) noexcept;

        FrameRenderer(const FrameRenderer&) = delete;
        FrameRenderer& operator=(const FrameRenderer&) = delete;

        [[nodiscard]] RenderFrameExecutionStatus FrameTick(RenderFrameTickContext& context) noexcept;
        [[nodiscard]] RenderFrameExecutionStatus RenderFrame(RenderFrameContext& context) noexcept;

        [[nodiscard]] static RenderFrameExecutionStatus ExecuteFrameTick(RenderFrameTickContext& context, void* userData) noexcept;
        [[nodiscard]] static RenderFrameExecutionStatus ExecuteFrame(RenderFrameContext& context, void* userData) noexcept;

    private:
        [[nodiscard]] RenderFrameExecutionStatus PrepareViewFamily(const RenderFrameInfo& frame, PreparedRenderViewFamily& preparedFamily,
                                                                   const PreparedRenderViewFamily*& family) noexcept;
        [[nodiscard]] RenderFrameExecutionStatus PrepareCustomData(const PreparedRenderViewFamily* family) noexcept;
        [[nodiscard]] RenderFrameExecutionStatus CheckCustomDataReadiness(const PreparedRenderViewFamily* family) noexcept;

        RenderCameraStorage& m_cameras;
        GpuSceneRuntime& m_gpuScene;
    };
} // namespace vanguard::rendering
