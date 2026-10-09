#pragma once

#include <vanguard/application/engine_host.hpp>
#include <vanguard/engine/frame_pipeline_service.hpp>
#include <vanguard/engine/rendering_service.hpp>
#include <vanguard/engine/world_session_service.hpp>
#include <vanguard/pipelines/pipelines.hpp>
#include <vanguard/rendering/presentation_service.hpp>

namespace vanguard::runtime
{
    inline constexpr application::ModuleId RuntimeModuleId = 0x72756e74696d6501ull;
    inline constexpr application::ServiceId RuntimeViewportServiceId = 0x7274766965777301ull;

    inline constexpr engine::FrameParticipantId RuntimePresentationReconcileParticipantId = 0x7274707265736e01ull;
    inline constexpr engine::FrameParticipantId RuntimeWorldFrameParticipantId = 0x7274776f726c6401ull;

    namespace detail
    {
        enum class RuntimeFrameAdmission : u8
        {
            NoFrame,
            Produce,
            Fail
        };

        inline constexpr engine::FrameParticipantId RuntimePresentationDependencies[]{engine::RenderingUpdateFrameParticipantId};
        inline constexpr engine::FrameParticipantId RuntimeWorldFrameDependencies[]{engine::RenderingFrameTickParticipantId};

        [[nodiscard]] constexpr bool IsRuntimeWorldSessionReady(const engine::WorldSessionStatus status) noexcept
        {
            return status == engine::WorldSessionStatus::Running;
        }

        [[nodiscard]] constexpr RuntimeFrameAdmission ClassifyRuntimeWorldCamera(
            const engine::WorldSessionCameraBindingStatus status, const bool handleValid, const bool handleAlive) noexcept
        {
            if (status == engine::WorldSessionCameraBindingStatus::None || status == engine::WorldSessionCameraBindingStatus::Pending)
                return RuntimeFrameAdmission::NoFrame;
            return status == engine::WorldSessionCameraBindingStatus::Ready && handleValid && handleAlive
                       ? RuntimeFrameAdmission::Produce
                       : RuntimeFrameAdmission::Fail;
        }

        [[nodiscard]] constexpr RuntimeFrameAdmission ClassifyRuntimePresentationOutput(
            const bool outputAvailable, const rendering::PresentationOutputState state, const bool drawable) noexcept
        {
            if (!outputAvailable || state == rendering::PresentationOutputState::Failed)
                return RuntimeFrameAdmission::Fail;
            return drawable ? RuntimeFrameAdmission::Produce : RuntimeFrameAdmission::NoFrame;
        }

        [[nodiscard]] constexpr pipelines::AttachmentSignature MakeRuntimeSdrOutputAttachments() noexcept
        {
            pipelines::AttachmentSignature attachments;
            attachments.colorCount = 1;
            attachments.colors[0] = {pipelines::Format::B8G8R8A8UNorm, shaders::NumericClass::FloatingPoint};
            return attachments;
        }

        [[nodiscard]] constexpr bool MatchesRuntimeSdrPresentation(const rhi::Format format, const rhi::ColorSpace colorSpace) noexcept
        {
            return format == rhi::Format::B8G8R8A8UNorm && colorSpace == rhi::ColorSpace::Srgb;
        }

        [[nodiscard]] constexpr bool IsDrawablePresentationState(const rendering::PresentationOutputState outputState,
                                                                 const rendering::RenderViewportState viewportState,
                                                                 const bool viewportValid, const bool sdrContractMatches,
                                                                 const bool visible, const bool occluded, const bool extentValid,
                                                                 const bool swapChainValid) noexcept
        {
            return outputState == rendering::PresentationOutputState::Ready && viewportState == rendering::RenderViewportState::Ready &&
                   viewportValid && sdrContractMatches && visible && !occluded && extentValid && swapChainValid;
        }

        [[nodiscard]] inline const char* FailureMessage(const char* const message, const char* const fallback) noexcept
        {
            return message != nullptr ? message : fallback;
        }

        [[nodiscard]] inline engine::FrameParticipantDescriptor MakeRuntimePresentationParticipant(const engine::ExecuteFrameParticipant execute,
                                                                                                   void* const userData) noexcept
        {
            engine::FrameParticipantDescriptor participant;
            participant.id = RuntimePresentationReconcileParticipantId;
            participant.name = "runtime.presentation";
            participant.phase = engine::FramePhase::Presentation;
            participant.profiles = application::ApplicationProfile::Runtime;
            participant.affinity = application::ThreadAffinity::MainThread;
            participant.after = {RuntimePresentationDependencies, 1};
            participant.execute = execute;
            participant.userData = userData;
            return participant;
        }

        [[nodiscard]] inline engine::FrameParticipantDescriptor MakeRuntimeWorldFrameParticipant(const engine::ExecuteFrameParticipant execute,
                                                                                                  void* const userData) noexcept
        {
            engine::FrameParticipantDescriptor participant;
            participant.id = RuntimeWorldFrameParticipantId;
            participant.name = "runtime.worldFrame";
            participant.phase = engine::FramePhase::Render;
            participant.profiles = application::ApplicationProfile::Runtime;
            participant.affinity = application::ThreadAffinity::MainThread;
            participant.after = {RuntimeWorldFrameDependencies, 1};
            participant.execute = execute;
            participant.userData = userData;
            return participant;
        }

        [[nodiscard]] inline bool IsRetryablePresentationFailure(const rendering::PresentationOutputState state,
                                                                 const rendering::PresentationFailure& failure) noexcept
        {
            if (state != rendering::PresentationOutputState::AwaitingSurface)
                return false;
            if (failure.code == rendering::PresentationFailureCode::WindowFailure)
                return failure.windowFailure.code == window::FailureCode::PresentationUnavailable;
            if (failure.code == rendering::PresentationFailureCode::RhiFailure)
                return failure.rhiFailure.code == rhi::FailureCode::Busy || failure.rhiFailure.code == rhi::FailureCode::Timeout;
            return false;
        }

        [[nodiscard]] inline bool IsDrawablePresentationOutput(const rendering::PresentationOutput& output) noexcept
        {
            const rendering::RenderViewport* const viewport = output.GetRenderViewport();
            return viewport != nullptr &&
                   IsDrawablePresentationState(output.GetState(), viewport->GetState(), viewport->IsValid(),
                                               MatchesRuntimeSdrPresentation(output.GetActiveFormat(), output.GetActiveColorSpace()),
                                               viewport->IsVisible(), viewport->IsOccluded(), viewport->GetOutputExtent().IsValid(),
                                               viewport->GetSwapChain().IsValid());
        }

        template <typename Viewport>
        [[nodiscard]] engine::FrameParticipantStatus SubmitRuntimeWorldFrame(Viewport& viewport,
                                                                             const rendering::RenderFrameSetup& frameSetup,
                                                                             const rendering::RenderFrameViewSetup& viewSetup) noexcept
        {
            rendering::RenderFrameInfo frame;
            rendering::ViewportFailure viewportFailure;
            if (!viewport.BeginFrame(frameSetup, frame, &viewportFailure))
            {
                if (viewportFailure.code == rendering::ViewportFailureCode::OutputUnavailable ||
                    viewportFailure.code == rendering::ViewportFailureCode::Busy)
                    return engine::FrameParticipantStatus::Success();
                return engine::FrameParticipantStatus::Failure(FailureMessage(viewportFailure.message, "runtime frame begin failed"));
            }

            if (!viewport.ConfigureViews(frame, viewSetup, &viewportFailure))
            {
                rendering::ViewportFailure abandonFailure;
                const bool abandoned = viewport.AbandonFrame(frame, &abandonFailure);
                return engine::FrameParticipantStatus::Failure(
                    abandoned ? FailureMessage(viewportFailure.message, "runtime frame view configuration failed")
                              : FailureMessage(abandonFailure.message, "runtime frame abandonment failed after view configuration failure"));
            }

            rendering::RenderFrameSubmission submission;
            if (!viewport.SubmitFrame(frame, submission, &viewportFailure))
            {
                rendering::ViewportFailure abandonFailure;
                const bool abandoned = viewport.AbandonFrame(frame, &abandonFailure);
                return engine::FrameParticipantStatus::Failure(
                    abandoned ? FailureMessage(viewportFailure.message, "runtime frame submission failed")
                              : FailureMessage(abandonFailure.message, "runtime frame abandonment failed after submission rejection"));
            }
            return engine::FrameParticipantStatus::Success();
        }
    } // namespace detail

    [[nodiscard]] bool RegisterRuntimeViewportService(application::EngineHost& host, application::HostFailure* failure = nullptr) noexcept;
} // namespace vanguard::runtime
