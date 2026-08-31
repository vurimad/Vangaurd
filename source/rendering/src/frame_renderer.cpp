#include <vanguard/rendering/frame_renderer.hpp>

namespace vanguard::rendering
{
    namespace
    {
        [[nodiscard]] const char* FailureMessage(const char* const message, const char* const fallback) noexcept
        {
            return message != nullptr ? message : fallback;
        }
    } // namespace

    FrameRenderer::FrameRenderer(RenderCameraStorage& cameras, GpuSceneRuntime& gpuScene) noexcept : m_cameras(cameras), m_gpuScene(gpuScene) {}

    RenderFrameExecutionStatus FrameRenderer::FrameTick(RenderFrameTickContext& context) noexcept
    {
        if (!m_gpuScene.IsInitialized())
            return RenderFrameExecutionStatus::Success();

        GpuSceneRuntimeFailure failure;
        if (!m_gpuScene.Publish(context, &failure))
            return RenderFrameExecutionStatus::Failure(FailureMessage(failure.message, "GPU Scene publication dispatch failed"));
        return RenderFrameExecutionStatus::Success();
    }

    RenderFrameExecutionStatus FrameRenderer::RenderFrame(RenderFrameContext& context) noexcept
    {
        const RenderFrameInfo& frame = context.GetFrame();
        PreparedRenderViewFamily preparedFamily;
        const PreparedRenderViewFamily* family = frame.GetViewFamily().IsValid() ? &frame.GetViewFamily() : nullptr;

        const RenderFrameExecutionStatus preparation = PrepareViewFamily(frame, preparedFamily, family);
        if (!preparation)
            return preparation;
        const RenderFrameExecutionStatus customData = PrepareCustomData(family);
        if (!customData)
            return customData;
        const RenderFrameExecutionStatus readiness = CheckCustomDataReadiness(family);
        if (!readiness)
            return readiness;

        FrameCustomData frameCustomData;
        if (family != nullptr)
        {
            frameCustomData = FrameCustomData(*family);
            if (!frameCustomData.IsValid())
                return RenderFrameExecutionStatus::Failure("render frame custom-data access could not retain its prepared view family");
        }
        return RenderFrameExecutionStatus::Failure("FrameRenderer has no installed Render Graph executor");
    }

    RenderFrameExecutionStatus FrameRenderer::ExecuteFrameTick(RenderFrameTickContext& context, void* const userData) noexcept
    {
        if (userData == nullptr)
            return RenderFrameExecutionStatus::Failure("FrameRenderer FrameTick target is unavailable");
        return static_cast<FrameRenderer*>(userData)->FrameTick(context);
    }

    RenderFrameExecutionStatus FrameRenderer::ExecuteFrame(RenderFrameContext& context, void* const userData) noexcept
    {
        if (userData == nullptr)
            return RenderFrameExecutionStatus::Failure("FrameRenderer RenderFrame target is unavailable");
        return static_cast<FrameRenderer*>(userData)->RenderFrame(context);
    }

    RenderFrameExecutionStatus FrameRenderer::PrepareViewFamily(const RenderFrameInfo& frame, PreparedRenderViewFamily& preparedFamily,
                                                                 const PreparedRenderViewFamily*& family) noexcept
    {
        if (family != nullptr || !frame.HasViewSetup())
            return RenderFrameExecutionStatus::Success();

        const RenderFrameViewSetup setup = frame.GetViewSetup();
        const RenderViewFamilyPrepareRequest request{setup.scene,
                                                     setup.rootCameras,
                                                     {frame.GetRenderExtent().width, frame.GetRenderExtent().height},
                                                     frame.GetSerial(),
                                                     setup.jitterIndex,
                                                     setup.enableTemporalJitter,
                                                     setup.forceCameraCut};
        RenderCameraFailure failure;
        if (!m_cameras.PrepareViewFamily(request, preparedFamily, &failure))
            return RenderFrameExecutionStatus::Failure(FailureMessage(failure.message, "render frame view-family preparation failed"));

        family = &preparedFamily;
        return RenderFrameExecutionStatus::Success();
    }

    RenderFrameExecutionStatus FrameRenderer::PrepareCustomData(const PreparedRenderViewFamily* const family) noexcept
    {
        if (family == nullptr)
            return RenderFrameExecutionStatus::Success();

        RenderCameraFailure failure;
        if (!m_cameras.PrepareCustomData(*family, &failure))
            return RenderFrameExecutionStatus::Failure(FailureMessage(failure.message, "render frame custom-data preparation failed"));
        return RenderFrameExecutionStatus::Success();
    }

    RenderFrameExecutionStatus FrameRenderer::CheckCustomDataReadiness(const PreparedRenderViewFamily* const family) noexcept
    {
        if (family == nullptr)
            return RenderFrameExecutionStatus::Success();

        RenderCameraFailure failure;
        if (!m_cameras.CheckCustomDataReadiness(*family, &failure))
            return RenderFrameExecutionStatus::Failure(FailureMessage(failure.message, "render frame custom data is not ready"));
        return RenderFrameExecutionStatus::Success();
    }
} // namespace vanguard::rendering
