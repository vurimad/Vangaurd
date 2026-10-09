#pragma once

#include <vanguard/editor/editor_ui.hpp>
#include <vanguard/editor/editor_ui_frame_retirement.hpp>

#include <vanguard/application/engine_host.hpp>
#include <vanguard/engine/frame_pipeline_service.hpp>
#include <vanguard/engine/rendering_service.hpp>
#include <vanguard/engine/world_session_service.hpp>
#include <vanguard/pipelines/pipelines.hpp>
#include <vanguard/rendering/viewport.hpp>

namespace vanguard::editor
{
    inline constexpr application::ServiceId EditorSceneOutputServiceId = 0x65647363656e7301ull;
    inline constexpr engine::FrameParticipantId EditorSceneFrameParticipantId = 0x65647363656e6501ull;
    inline constexpr u32 MaximumEditorSceneOutputGenerations = 2;

    struct EditorSceneOutputInfo
    {
        EditorTextureHandle texture;
        rendering::ViewportExtent extent;
        u64 generation = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return texture.IsValid() && extent.IsValid() && generation != 0;
        }
    };

    class EditorSceneOutputService : public application::Service
    {
    public:
        ~EditorSceneOutputService() override = default;

        /// Requests scene-output demand at the given extent. Replacement is generation based;
        /// an already captured editor texture is never rebound in place.
        [[nodiscard]] virtual bool RequestOutputExtent(rendering::ViewportExtent extent) noexcept = 0;
        /// Clears producer demand. A last published Ready generation may remain available as a
        /// frozen editor texture until later demand or lifecycle retirement; no new producer is accepted.
        [[nodiscard]] virtual bool ClearOutputDemand() noexcept = 0;
        [[nodiscard]] virtual EditorSceneOutputInfo GetPublishedOutput() const noexcept = 0;

    protected:
        EditorSceneOutputService() noexcept = default;
    };

    namespace detail
    {
        using RevokeEditorSceneTextureRegistration = bool (*)(EditorTextureHandle texture, void* userData) noexcept;

        enum class SceneOutputGenerationRole : u8
        {
            Vacant,
            Candidate,
            Active,
            Retiring
        };

        enum class ActiveSceneOutputReceiptAction : u8
        {
            Wait,
            Produce,
            Retire,
            Reject
        };

        enum class EditorSceneFrameAdmission : u8
        {
            NoFrame,
            Produce
        };

        inline constexpr engine::FrameParticipantId EditorSceneFrameDependencies[]{engine::RenderingFrameTickParticipantId};

        [[nodiscard]] constexpr pipelines::AttachmentSignature MakeEditorSceneOutputAttachments() noexcept
        {
            pipelines::AttachmentSignature attachments;
            attachments.colorCount = 1;
            attachments.colors[0] = {pipelines::Format::R8G8B8A8UNorm, shaders::NumericClass::FloatingPoint};
            attachments.sampleCount = 1;
            return attachments;
        }

        [[nodiscard]] inline rhi::TextureDesc MakeEditorSceneTextureDesc(const rendering::ViewportExtent extent) noexcept
        {
            rhi::TextureDesc desc;
            desc.extent = {extent.width, extent.height, 1};
            desc.dimension = rhi::TextureDimension::Texture2D;
            desc.format = rhi::Format::R8G8B8A8UNorm;
            desc.usage = rhi::TextureUsage::RenderTarget | rhi::TextureUsage::ShaderResource;
            desc.initialState = rhi::ResourceState::ShaderResourceGraphics;
            return desc;
        }

        [[nodiscard]] inline rendering::RenderViewportDesc MakeEditorSceneRenderViewportDesc(const rendering::ViewportExtent extent,
                                                                                             const rhi::TextureRef texture) noexcept
        {
            rendering::RenderViewportDesc desc;
            desc.name = "EditorSceneOutput";
            desc.outputKind = rendering::RenderViewportOutputKind::Texture;
            desc.renderExtent = extent;
            desc.outputExtent = extent;
            desc.outputTexture = texture;
            desc.textureOutput.restingState = rhi::ResourceState::ShaderResourceGraphics;
            desc.textureOutput.restingQueue = rhi::QueueType::Graphics;
            desc.textureOutput.publicationReadiness = rendering::RenderOutputPublicationReadinessKind::SameQueueContinuation;
            desc.outputColorEncoding = rendering::RenderOutputColorEncoding::DisplaySrgb;
            return desc;
        }

        [[nodiscard]] inline engine::FrameParticipantDescriptor MakeEditorSceneFrameParticipant(const engine::ExecuteFrameParticipant execute,
                                                                                                void* const userData) noexcept
        {
            engine::FrameParticipantDescriptor participant;
            participant.id = EditorSceneFrameParticipantId;
            participant.name = "editor.sceneOutput";
            participant.phase = engine::FramePhase::Render;
            participant.profiles = application::ApplicationProfile::Editor | application::ApplicationProfile::Tool;
            participant.affinity = application::ThreadAffinity::MainThread;
            participant.after = {EditorSceneFrameDependencies, 1};
            participant.execute = execute;
            participant.userData = userData;
            return participant;
        }

        [[nodiscard]] constexpr bool CanRetireSceneOutputGeneration(const u64 capturedUiFrame, const u64 currentUiFrame) noexcept
        {
            return CanRetireEditorUiFrameResource(capturedUiFrame, currentUiFrame);
        }

        [[nodiscard]] inline bool BeginSceneOutputRetirement(SceneOutputGenerationRole& active, EditorTextureHandle& registeredTexture,
                                                             const u64 uiFrame, u64& retireAfterUiFrame,
                                                             const RevokeEditorSceneTextureRegistration revoke,
                                                             void* const revokeUserData) noexcept
        {
            if (active != SceneOutputGenerationRole::Active || !registeredTexture.IsValid() || uiFrame == 0 || revoke == nullptr)
                return false;
            if (!revoke(registeredTexture, revokeUserData))
                return false;
            registeredTexture = {};
            active = SceneOutputGenerationRole::Retiring;
            retireAfterUiFrame = uiFrame;
            return true;
        }

        [[nodiscard]] constexpr bool CanAllocateSceneOutputCandidate(const bool hasCandidate, const bool hasRetiring) noexcept
        {
            return !hasCandidate && !hasRetiring;
        }

        [[nodiscard]] inline bool PromoteSceneOutputGeneration(SceneOutputGenerationRole& candidate,
                                                               SceneOutputGenerationRole* const active,
                                                               EditorTextureHandle* const activeTexture,
                                                               const bool hasRetiring, const u64 uiFrame,
                                                               u64* const activeRetireAfterUiFrame,
                                                               const RevokeEditorSceneTextureRegistration revoke,
                                                               void* const revokeUserData) noexcept
        {
            if (candidate != SceneOutputGenerationRole::Candidate || hasRetiring)
                return false;
            if (active != nullptr)
            {
                if (activeTexture == nullptr || activeRetireAfterUiFrame == nullptr ||
                    !BeginSceneOutputRetirement(*active, *activeTexture, uiFrame, *activeRetireAfterUiFrame, revoke, revokeUserData))
                    return false;
            }
            candidate = SceneOutputGenerationRole::Active;
            return true;
        }

        [[nodiscard]] constexpr bool HasEditorSceneOutputDemand(const rendering::ViewportExtent requested) noexcept
        {
            return requested.IsValid();
        }

        [[nodiscard]] constexpr bool ShouldDiscardSceneOutputCandidate(const rendering::ViewportExtent candidate,
                                                                       const rendering::ViewportExtent requested) noexcept
        {
            return !requested.IsValid() || candidate != requested;
        }

        [[nodiscard]] constexpr bool CanAcceptSceneOutputProducer(const SceneOutputGenerationRole role,
                                                                  const u64 acceptedProducerSerial) noexcept
        {
            // A candidate gets exactly one proving producer epoch. Once accepted, only
            // reconciliation may promote/release it; a second write before publication would
            // move the receipt underneath the cutover decision.
            return role == SceneOutputGenerationRole::Active ||
                   (role == SceneOutputGenerationRole::Candidate && acceptedProducerSerial == 0);
        }

        [[nodiscard]] constexpr bool SceneOutputNeedsReplacement(const rendering::ViewportExtent active,
                                                                 const rendering::ViewportExtent requested) noexcept
        {
            return !active.IsValid() || active != requested;
        }

        [[nodiscard]] constexpr ActiveSceneOutputReceiptAction ClassifyActiveSceneOutputReceipt(
            const rendering::RenderOutputReceiptState state, const bool exactReadyContract) noexcept
        {
            if (state == rendering::RenderOutputReceiptState::Pending)
                return ActiveSceneOutputReceiptAction::Wait;
            if (state == rendering::RenderOutputReceiptState::Failed)
                return ActiveSceneOutputReceiptAction::Retire;
            return state == rendering::RenderOutputReceiptState::Ready && exactReadyContract
                       ? ActiveSceneOutputReceiptAction::Produce
                       : ActiveSceneOutputReceiptAction::Reject;
        }

        [[nodiscard]] constexpr bool CanProduceEditorSceneFromCamera(const engine::WorldSessionCameraBindingStatus status,
                                                                      const bool handleValid, const bool handleAlive) noexcept
        {
            return status == engine::WorldSessionCameraBindingStatus::Ready && handleValid && handleAlive;
        }

        [[nodiscard]] constexpr EditorSceneFrameAdmission ClassifyEditorSceneFrameAdmission(
            const bool hasDemand, const engine::WorldSessionStatus sessionStatus,
            const engine::WorldSessionCameraBindingStatus cameraStatus, const bool cameraValid, const bool cameraAlive) noexcept
        {
            if (!hasDemand || sessionStatus != engine::WorldSessionStatus::Running)
                return EditorSceneFrameAdmission::NoFrame;
            return CanProduceEditorSceneFromCamera(cameraStatus, cameraValid, cameraAlive) ? EditorSceneFrameAdmission::Produce
                                                                                           : EditorSceneFrameAdmission::NoFrame;
        }

        template <typename ReceiptSource>
        [[nodiscard]] bool QueryEditorSceneOutputReceipt(ReceiptSource& source, const rendering::RenderViewportHandle viewport,
                                                         const u64 outputRevision, const u64 producerSerial,
                                                         rendering::RenderOutputReceipt& receipt) noexcept
        {
            receipt = {};
            return viewport.IsValid() && outputRevision != 0 && producerSerial != 0 &&
                   source.QueryOutputReceipt(viewport, outputRevision, producerSerial, receipt);
        }

        [[nodiscard]] inline const char* SceneOutputFailureMessage(const char* const message, const char* const fallback) noexcept
        {
            return message != nullptr ? message : fallback;
        }

        template <typename Viewport>
        [[nodiscard]] engine::FrameParticipantStatus SubmitEditorSceneFrame(Viewport& viewport,
                                                                           const rendering::RenderFrameSetup& frameSetup,
                                                                           const rendering::RenderFrameViewSetup& viewSetup,
                                                                           u64& producerSerial) noexcept
        {
            producerSerial = 0;
            rendering::RenderFrameInfo frame;
            rendering::ViewportFailure viewportFailure;
            if (!viewport.BeginFrame(frameSetup, frame, &viewportFailure))
            {
                if (viewportFailure.code == rendering::ViewportFailureCode::OutputUnavailable ||
                    viewportFailure.code == rendering::ViewportFailureCode::Busy)
                    return engine::FrameParticipantStatus::Success();
                return engine::FrameParticipantStatus::Failure(
                    SceneOutputFailureMessage(viewportFailure.message, "editor scene frame begin failed"));
            }

            if (!viewport.ConfigureViews(frame, viewSetup, &viewportFailure))
            {
                rendering::ViewportFailure abandonFailure;
                const bool abandoned = viewport.AbandonFrame(frame, &abandonFailure);
                return engine::FrameParticipantStatus::Failure(
                    abandoned ? SceneOutputFailureMessage(viewportFailure.message, "editor scene frame view configuration failed")
                              : SceneOutputFailureMessage(abandonFailure.message,
                                                          "editor scene frame abandonment failed after view configuration failure"));
            }

            rendering::RenderFrameSubmission submission;
            if (!viewport.SubmitFrame(frame, submission, &viewportFailure))
            {
                rendering::ViewportFailure abandonFailure;
                const bool abandoned = viewport.AbandonFrame(frame, &abandonFailure);
                return engine::FrameParticipantStatus::Failure(
                    abandoned ? SceneOutputFailureMessage(viewportFailure.message, "editor scene frame submission failed")
                              : SceneOutputFailureMessage(abandonFailure.message,
                                                          "editor scene frame abandonment failed after submission rejection"));
            }
            if (!submission.IsValid())
                return engine::FrameParticipantStatus::Failure("accepted editor scene frame has no producer serial");
            producerSerial = submission.serial;
            return engine::FrameParticipantStatus::Success();
        }
    } // namespace detail

    [[nodiscard]] bool RegisterEditorSceneOutputService(application::EngineHost& host,
                                                        application::HostFailure* failure = nullptr) noexcept;
    [[nodiscard]] EditorSceneOutputService* FindEditorSceneOutputService(application::EngineHost& host) noexcept;
    [[nodiscard]] EditorSceneOutputService* FindEditorSceneOutputService(application::ServiceContext& context) noexcept;
} // namespace vanguard::editor
