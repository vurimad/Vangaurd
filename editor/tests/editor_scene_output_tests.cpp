#include <vanguard/editor/editor_scene_output_service.hpp>
#include <vanguard/editor/editor_scene_view_service.hpp>
#include <vanguard/editor/editor_ui_render_data.hpp>

#include <vanguard/concurrency/concurrency.hpp>
#include <vanguard/containers/containers.hpp>
#include <vanguard/diagnostics/diagnostics.hpp>
#include <vanguard/engine/engine_services.hpp>
#include <vanguard/memory/memory.hpp>

#include <cstdio>

namespace
{
    namespace app = vanguard::application;
    namespace editor = vanguard::editor;
    namespace engine = vanguard::engine;
    namespace pipelines = vanguard::pipelines;
    namespace rendering = vanguard::rendering;
    namespace rhi = vanguard::rhi;
    namespace shaders = vanguard::shaders;

    int g_failures = 0;

    void Check(const bool condition, const char* const message) noexcept
    {
        if (condition)
            return;
        std::fprintf(stderr, "[editorSceneOutputTests] FAILED: %s\n", message);
        ++g_failures;
    }

    engine::FrameParticipantStatus ExecuteNoopParticipant(const engine::FrameContext&, void*) noexcept
    {
        return engine::FrameParticipantStatus::Success();
    }

    struct TextureRevocationFixture final
    {
        editor::EditorTextureHandle expected;
        vanguard::u32 calls = 0;
        bool succeeds = true;
    };

    bool RevokeTexture(const editor::EditorTextureHandle texture, void* const userData) noexcept
    {
        auto& fixture = *static_cast<TextureRevocationFixture*>(userData);
        ++fixture.calls;
        return fixture.succeeds && texture == fixture.expected;
    }

    app::Service* CreateUnusedService(void*) noexcept { return nullptr; }
    void DestroyUnusedService(app::Service*, void*) noexcept {}

    bool RegisterCompileOnlyService(app::EngineHost& host, const app::ModuleId module, const app::ServiceId id,
                                    const char* const name) noexcept
    {
        app::ServiceDescriptor descriptor;
        descriptor.id = id;
        descriptor.name = name;
        descriptor.profiles = app::ApplicationProfile::Editor | app::ApplicationProfile::Tool;
        descriptor.scope = app::ServiceScope::Process;
        descriptor.affinity = app::ThreadAffinity::MainThread;
        descriptor.create = &CreateUnusedService;
        descriptor.destroy = &DestroyUnusedService;
        return host.RegisterService(module, descriptor);
    }

    void TestSceneOutputContracts() noexcept
    {
        const pipelines::AttachmentSignature attachments = editor::detail::MakeEditorSceneOutputAttachments();
        Check(attachments.colorCount == 1 && attachments.colors[0].format == pipelines::Format::R8G8B8A8UNorm &&
                  attachments.colors[0].numericClass == shaders::NumericClass::FloatingPoint && attachments.sampleCount == 1 &&
                  attachments.depthStencilFormat == pipelines::Format::Unknown,
              "editor scene output declares the locked single-sampled R8 UNorm renderer attachment contract");

        const rendering::ViewportExtent extent{960, 540};
        const rhi::TextureDesc texture = editor::detail::MakeEditorSceneTextureDesc(extent);
        Check(texture.extent.width == extent.width && texture.extent.height == extent.height && texture.extent.depth == 1 &&
                  texture.dimension == rhi::TextureDimension::Texture2D && texture.format == rhi::Format::R8G8B8A8UNorm &&
                  texture.initialState == rhi::ResourceState::ShaderResourceGraphics &&
                  (static_cast<vanguard::u32>(texture.usage) & static_cast<vanguard::u32>(rhi::TextureUsage::RenderTarget)) != 0 &&
                  (static_cast<vanguard::u32>(texture.usage) & static_cast<vanguard::u32>(rhi::TextureUsage::ShaderResource)) != 0,
              "editor scene generation texture is render-target and shader-resource capable in its locked resting state");

        const rendering::RenderViewportDesc viewport = editor::detail::MakeEditorSceneRenderViewportDesc(extent, {});
        Check(viewport.outputKind == rendering::RenderViewportOutputKind::Texture && viewport.renderExtent == extent &&
                  viewport.outputExtent == extent && viewport.textureOutput.restingState == rhi::ResourceState::ShaderResourceGraphics &&
                  viewport.textureOutput.restingQueue == rhi::QueueType::Graphics &&
                  viewport.textureOutput.publicationReadiness == rendering::RenderOutputPublicationReadinessKind::SameQueueContinuation &&
                  viewport.outputColorEncoding == rendering::RenderOutputColorEncoding::DisplaySrgb,
              "editor scene RenderViewport is Texture-backed, same-Graphics continuation, and carries DisplaySrgb metadata");
    }

    void TestParticipantAndGenerationPolicy() noexcept
    {
        int cookie = 3;
        const engine::FrameParticipantDescriptor participant =
            editor::detail::MakeEditorSceneFrameParticipant(&ExecuteNoopParticipant, &cookie);
        Check(participant.id == editor::EditorSceneFrameParticipantId && participant.phase == engine::FramePhase::Render &&
                  participant.affinity == app::ThreadAffinity::MainThread &&
                  participant.profiles == (app::ApplicationProfile::Editor | app::ApplicationProfile::Tool) &&
                  participant.after.Size() == 1 && participant.after[0] == engine::RenderingFrameTickParticipantId &&
                  participant.execute == &ExecuteNoopParticipant && participant.userData == &cookie,
              "editor scene producer is the reserved main-thread Render participant after the renderer FrameTick");
        Check(static_cast<vanguard::u8>(participant.phase) < static_cast<vanguard::u8>(engine::FramePhase::EndFrame),
              "editor scene producer Render phase precedes retained editor UI composition in EndFrame");

        Check(editor::MaximumEditorSceneOutputGenerations == 2 && editor::detail::CanAllocateSceneOutputCandidate(false, false) &&
                  !editor::detail::CanAllocateSceneOutputCandidate(true, false) &&
                  !editor::detail::CanAllocateSceneOutputCandidate(false, true),
              "editor scene replacement permits only active plus one candidate or retiring generation");

        editor::detail::SceneOutputGenerationRole oldRole = editor::detail::SceneOutputGenerationRole::Active;
        editor::detail::SceneOutputGenerationRole candidateRole = editor::detail::SceneOutputGenerationRole::Candidate;
        editor::EditorTextureHandle oldTexture{3, 7};
        TextureRevocationFixture revocation{oldTexture};
        vanguard::u64 retireAfter = 0;
        Check(editor::detail::PromoteSceneOutputGeneration(candidateRole, &oldRole, &oldTexture, false, 12, &retireAfter,
                                                           &RevokeTexture, &revocation) &&
                  candidateRole == editor::detail::SceneOutputGenerationRole::Active &&
                  oldRole == editor::detail::SceneOutputGenerationRole::Retiring && !oldTexture.IsValid() && revocation.calls == 1 && retireAfter == 12 &&
                  !editor::detail::CanAllocateSceneOutputCandidate(false, true) &&
                  !editor::detail::CanRetireSceneOutputGeneration(retireAfter, 12) &&
                  editor::detail::CanRetireSceneOutputGeneration(retireAfter, 13),
              "Ready candidate cutover revokes the old authoring handle immediately, retains its generation as retiring, blocks a third generation, and releases only after the next UI frame");
        editor::detail::SceneOutputGenerationRole blockedCandidate = editor::detail::SceneOutputGenerationRole::Candidate;
        Check(!editor::detail::PromoteSceneOutputGeneration(blockedCandidate, nullptr, nullptr, true, 13, nullptr, nullptr, nullptr) &&
                  blockedCandidate == editor::detail::SceneOutputGenerationRole::Candidate,
              "candidate promotion is coalesced while another generation is still retiring");

        const rendering::ViewportExtent firstRequested{800, 450};
        const rendering::ViewportExtent latestRequested{1024, 576};
        Check(!editor::detail::HasEditorSceneOutputDemand({}) && editor::detail::HasEditorSceneOutputDemand(latestRequested) &&
                  editor::detail::ShouldDiscardSceneOutputCandidate(firstRequested, latestRequested) &&
                  editor::detail::ShouldDiscardSceneOutputCandidate(firstRequested, {}) &&
                  !editor::detail::ShouldDiscardSceneOutputCandidate(latestRequested, latestRequested),
              "no-demand accepts no new producer and repeated resize coalesces an obsolete candidate to the latest requested extent");
        Check(editor::detail::CanAcceptSceneOutputProducer(editor::detail::SceneOutputGenerationRole::Candidate, 0) &&
                  !editor::detail::CanAcceptSceneOutputProducer(editor::detail::SceneOutputGenerationRole::Candidate, 44) &&
                  editor::detail::CanAcceptSceneOutputProducer(editor::detail::SceneOutputGenerationRole::Active, 44),
              "candidate accepts exactly one proving producer epoch before reconciliation while active generations may continue steady-state production");
        Check(!editor::detail::CanRetireSceneOutputGeneration(0, 10) &&
                  !editor::detail::CanRetireSceneOutputGeneration(10, 10) &&
                  editor::detail::CanRetireSceneOutputGeneration(10, 11),
              "retiring generation is releasable only after a later UI BeginFrame boundary");
        Check(!editor::detail::SceneOutputNeedsReplacement({640, 360}, {640, 360}) &&
                  editor::detail::SceneOutputNeedsReplacement({640, 360}, {800, 450}) &&
                  editor::detail::SceneOutputNeedsReplacement({}, {800, 450}),
              "extent changes select replacement generation rather than steady-state reuse");

        const rendering::ViewportExtent publishedExtent{800, 450};
        const rendering::ViewportExtent resizeExtent{1024, 576};
        Check(!editor::detail::ShouldCommitEditorSceneResize(resizeExtent, publishedExtent, resizeExtent, 10.0, 10.05) &&
                  editor::detail::ShouldCommitEditorSceneResize(resizeExtent, publishedExtent, resizeExtent, 10.0, 10.11) &&
                  editor::detail::ShouldCommitEditorSceneResize(publishedExtent, publishedExtent, resizeExtent, 10.0, 10.01) &&
                  editor::detail::ShouldCommitEditorSceneResize(resizeExtent, {}, resizeExtent, 10.0, 10.01),
              "live Scene resize keeps the published generation during motion, converges after the settle window, and never delays first output");

        using ReceiptAction = editor::detail::ActiveSceneOutputReceiptAction;
        Check(editor::detail::ClassifyActiveSceneOutputReceipt(rendering::RenderOutputReceiptState::Pending, false) == ReceiptAction::Wait &&
                  editor::detail::ClassifyActiveSceneOutputReceipt(rendering::RenderOutputReceiptState::Ready, true) == ReceiptAction::Produce &&
                  editor::detail::ClassifyActiveSceneOutputReceipt(rendering::RenderOutputReceiptState::Ready, false) == ReceiptAction::Reject &&
                  editor::detail::ClassifyActiveSceneOutputReceipt(rendering::RenderOutputReceiptState::Failed, false) == ReceiptAction::Retire,
              "active producer receipt policy waits Pending, produces only exact Ready, and retires Failed physical generations");

        Check(editor::detail::CanProduceEditorSceneFromCamera(engine::WorldSessionCameraBindingStatus::Ready, true, true) &&
                  !editor::detail::CanProduceEditorSceneFromCamera(engine::WorldSessionCameraBindingStatus::None, false, false) &&
                  !editor::detail::CanProduceEditorSceneFromCamera(engine::WorldSessionCameraBindingStatus::Pending, false, false) &&
                  !editor::detail::CanProduceEditorSceneFromCamera(engine::WorldSessionCameraBindingStatus::Failed, false, false) &&
                  !editor::detail::CanProduceEditorSceneFromCamera(engine::WorldSessionCameraBindingStatus::Ready, false, false) &&
                  !editor::detail::CanProduceEditorSceneFromCamera(engine::WorldSessionCameraBindingStatus::Ready, true, false),
              "editor scene producer treats absent/pending/failed/stale optional camera bindings as no-frame rather than fallback or termination");

        using FrameAdmission = editor::detail::EditorSceneFrameAdmission;
        Check(editor::detail::ClassifyEditorSceneFrameAdmission(false, engine::WorldSessionStatus::Running,
                                                                engine::WorldSessionCameraBindingStatus::Ready, true, true) == FrameAdmission::NoFrame &&
                  editor::detail::ClassifyEditorSceneFrameAdmission(true, engine::WorldSessionStatus::LoadingWorld,
                                                                    engine::WorldSessionCameraBindingStatus::Ready, true, true) == FrameAdmission::NoFrame &&
                  editor::detail::ClassifyEditorSceneFrameAdmission(true, engine::WorldSessionStatus::Running,
                                                                    engine::WorldSessionCameraBindingStatus::Pending, false, false) == FrameAdmission::NoFrame &&
                  editor::detail::ClassifyEditorSceneFrameAdmission(true, engine::WorldSessionStatus::Running,
                                                                    engine::WorldSessionCameraBindingStatus::Ready, true, true) == FrameAdmission::Produce,
              "editor scene admission requires explicit demand, a Running session, and one live Ready selected camera");

        editor::detail::SceneOutputGenerationRole failedActive = editor::detail::SceneOutputGenerationRole::Active;
        editor::EditorTextureHandle failedTexture{5, 11};
        TextureRevocationFixture failedRevocation{failedTexture};
        vanguard::u64 failedRetireAfter = 0;
        const bool cameraAbsent = !editor::detail::CanProduceEditorSceneFromCamera(engine::WorldSessionCameraBindingStatus::None, false, false);
        Check(cameraAbsent &&
                  editor::detail::ClassifyActiveSceneOutputReceipt(rendering::RenderOutputReceiptState::Failed, false) == ReceiptAction::Retire &&
                  editor::detail::BeginSceneOutputRetirement(failedActive, failedTexture, 20, failedRetireAfter,
                                                             &RevokeTexture, &failedRevocation) &&
                  failedActive == editor::detail::SceneOutputGenerationRole::Retiring && !failedTexture.IsValid() &&
                  failedRevocation.calls == 1 && failedRetireAfter == 20,
              "failed active producer revokes its authoring handle and enters retirement even when the camera is absent, before no-frame camera gating");
    }

    struct ReceiptFixture final
    {
        rendering::RenderOutputReceipt receipt;
        rendering::RenderViewportHandle viewport;
        vanguard::u64 outputRevision = 0;
        vanguard::u64 producerSerial = 0;
        vanguard::u32 calls = 0;
        bool available = true;

        bool QueryOutputReceipt(const rendering::RenderViewportHandle requestedViewport, const vanguard::u64 requestedRevision,
                                const vanguard::u64 requestedSerial, rendering::RenderOutputReceipt& output) noexcept
        {
            ++calls;
            output = {};
            if (!available || requestedViewport != viewport || requestedRevision != outputRevision || requestedSerial != producerSerial ||
                receipt.viewport != requestedViewport || receipt.outputRevision != requestedRevision || receipt.producerSerial != requestedSerial)
                return false;
            output = receipt;
            return true;
        }
    };

    bool ResolveReceipt(void* const userData, rendering::RenderOutputReceipt& receipt) noexcept
    {
        auto& fixture = *static_cast<ReceiptFixture*>(userData);
        return editor::detail::QueryEditorSceneOutputReceipt(fixture, fixture.viewport, fixture.outputRevision,
                                                             fixture.producerSerial, receipt);
    }

    rendering::RenderOutputReceipt ReadyReceipt(const rendering::RenderViewportHandle viewport,
                                                const vanguard::u64 revision,
                                                const vanguard::u64 serial) noexcept
    {
        rendering::RenderOutputReceipt receipt;
        receipt.viewport = viewport;
        receipt.outputRevision = revision;
        receipt.producerSerial = serial;
        receipt.state = rendering::RenderOutputReceiptState::Ready;
        receipt.terminalState = rhi::ResourceState::ShaderResourceGraphics;
        receipt.terminalQueue = rhi::QueueType::Graphics;
        receipt.readiness = rendering::RenderOutputPublicationReadinessKind::SameQueueContinuation;
        receipt.readyFence = {rhi::QueueType::Graphics, serial};
        return receipt;
    }

    void TestLatePublicationReceiptResolution() noexcept
    {
        const rendering::RenderViewportHandle viewport{4, 9};
        constexpr vanguard::u64 revision = 6;
        ReceiptFixture fixture;
        fixture.viewport = viewport;
        fixture.outputRevision = revision;
        fixture.producerSerial = 17;
        fixture.receipt = ReadyReceipt(viewport, revision, 17);
        editor::EditorTextureRenderOutputPublication publication{viewport, revision, &ResolveReceipt, &fixture};
        Check(publication.IsSpecified() && publication.IsValid(), "complete render-output publication binding is valid");

        rendering::RetainedTextureImportDesc import;
        Check(editor::detail::ResolveEditorTexturePublicationImport(publication, import) && fixture.calls == 1 &&
                  import.initialState == rhi::ResourceState::ShaderResourceGraphics &&
                  import.terminalState == rhi::ResourceState::ShaderResourceGraphics && import.initialQueue == rhi::QueueType::Graphics &&
                  import.terminalQueue == rhi::QueueType::Graphics && import.readiness == rendering::ImportReadinessKind::SameQueueContinuation &&
                  !import.incomingWait.IsValid(),
              "late Ready receipt maps to same-Graphics UI import without a CPU or explicit-fence wait");

        fixture.receipt.state = rendering::RenderOutputReceiptState::Pending;
        Check(!editor::detail::ResolveEditorTexturePublicationImport(publication, import) && fixture.calls == 2,
              "late Pending exact producer receipt fails closed instead of reusing earlier Ready contents");
        fixture.receipt.state = rendering::RenderOutputReceiptState::Failed;
        Check(!editor::detail::ResolveEditorTexturePublicationImport(publication, import) && fixture.calls == 3,
              "late Failed exact producer receipt fails closed");

        fixture.producerSerial = 18;
        fixture.receipt = ReadyReceipt({5, 9}, revision, 18);
        Check(!editor::detail::ResolveEditorTexturePublicationImport(publication, import),
              "receipt from a different RenderViewport generation is rejected");
        fixture.producerSerial = 19;
        fixture.receipt = ReadyReceipt(viewport, revision + 1, 19);
        Check(!editor::detail::ResolveEditorTexturePublicationImport(publication, import),
              "receipt from a different output revision is rejected");

        fixture.producerSerial = 20;
        fixture.receipt = ReadyReceipt(viewport, revision, 20);
        Check(editor::detail::ResolveEditorTexturePublicationImport(publication, import) && fixture.calls == 6,
              "publication binding is re-resolved on every late import instead of capturing producer state earlier");

        fixture.producerSerial = 21;
        fixture.receipt = ReadyReceipt(viewport, revision, 22);
        Check(!editor::detail::ResolveEditorTexturePublicationImport(publication, import),
              "late import rejects a receipt from any producer serial other than the exact currently accepted epoch");

        fixture.producerSerial = 20;
        fixture.receipt = ReadyReceipt(viewport, revision, 20);
        fixture.receipt.readiness = rendering::RenderOutputPublicationReadinessKind::ExplicitFence;
        fixture.receipt.readyFence = {rhi::QueueType::Graphics, 21};
        Check(editor::detail::ResolveEditorTexturePublicationImport(publication, import) &&
                  import.readiness == rendering::ImportReadinessKind::ExplicitFenceWait && import.incomingWait == fixture.receipt.readyFence,
              "neutral publication handoff preserves the existing explicit-fence path without adding an editor fence registry");

        editor::EditorTextureRenderOutputPublication partial = publication;
        partial.resolveReceipt = nullptr;
        Check(partial.IsSpecified() && !partial.IsValid(), "partially specified publication binding is rejected");

        // Logical registry revocation at cutover must prevent a later UI frame from
        // recapturing the old handle without invalidating publication data already retained
        // by the current UI frame. The generation/receipt owner is released only later.
        editor::detail::SceneOutputGenerationRole capturedRole = editor::detail::SceneOutputGenerationRole::Active;
        editor::detail::SceneOutputGenerationRole replacementRole = editor::detail::SceneOutputGenerationRole::Candidate;
        editor::EditorTextureHandle capturedHandle{7, 13};
        TextureRevocationFixture capturedRevocation{capturedHandle};
        vanguard::u64 capturedRetireAfter = 0;
        fixture.producerSerial = 22;
        fixture.receipt = ReadyReceipt(viewport, revision, 22);
        const editor::EditorTextureRenderOutputPublication retainedPublication = publication;
        Check(editor::detail::PromoteSceneOutputGeneration(replacementRole, &capturedRole, &capturedHandle, false, 30,
                                                           &capturedRetireAfter, &RevokeTexture, &capturedRevocation) &&
                  replacementRole == editor::detail::SceneOutputGenerationRole::Active &&
                  !capturedHandle.IsValid() && capturedRevocation.calls == 1 &&
                  capturedRole == editor::detail::SceneOutputGenerationRole::Retiring &&
                  !editor::detail::CanRetireSceneOutputGeneration(capturedRetireAfter, 30) &&
                  editor::detail::ResolveEditorTexturePublicationImport(retainedPublication, import) &&
                  editor::detail::CanRetireSceneOutputGeneration(capturedRetireAfter, 31),
              "replacement becoming Ready after UI capture revokes the old authoring handle while the captured old publication remains resolvable until the next UI BeginFrame boundary");
    }

    struct FakeSceneViewport final
    {
        bool beginSucceeds = true;
        bool configureSucceeds = true;
        bool submitSucceeds = true;
        bool abandonSucceeds = true;
        rendering::ViewportFailureCode beginFailure = rendering::ViewportFailureCode::InvalidState;
        vanguard::u64 submitSerial = 41;
        vanguard::u32 beginCalls = 0;
        vanguard::u32 configureCalls = 0;
        vanguard::u32 submitCalls = 0;
        vanguard::u32 abandonCalls = 0;
        bool sawPresent = true;
        rendering::RenderingMode mode = rendering::RenderingMode::OverlayOnly;
        rendering::RenderFramePurpose purpose = rendering::RenderFramePurpose::Blank;
        rendering::RenderSceneHandle configuredScene;
        rendering::RenderCameraHandle configuredRoot;
        vanguard::u32 configuredRootCount = 0;
        vanguard::u32 configuredRegionCount = 0;
        vanguard::u32 configuredJitter = 0;
        bool configuredTemporalJitter = false;

        bool BeginFrame(const rendering::RenderFrameSetup& setup, rendering::RenderFrameInfo&,
                        rendering::ViewportFailure* const failure) noexcept
        {
            ++beginCalls;
            sawPresent = setup.present;
            mode = setup.mode;
            purpose = setup.purpose;
            if (beginSucceeds)
                return true;
            if (failure != nullptr)
            {
                *failure = {};
                failure->code = beginFailure;
                failure->message = "fixture begin rejection";
            }
            return false;
        }

        bool ConfigureViews(rendering::RenderFrameInfo&, const rendering::RenderFrameViewSetup& setup,
                            rendering::ViewportFailure* const failure) noexcept
        {
            ++configureCalls;
            configuredScene = setup.scene;
            configuredRootCount = setup.rootCameras.Size();
            configuredRegionCount = setup.outputRegions.Size();
            configuredRoot = !setup.rootCameras.Empty() ? setup.rootCameras[0] : rendering::RenderCameraHandle{};
            configuredJitter = setup.jitterIndex;
            configuredTemporalJitter = setup.enableTemporalJitter;
            if (configureSucceeds)
                return true;
            if (failure != nullptr)
            {
                *failure = {};
                failure->code = rendering::ViewportFailureCode::InvalidDescriptor;
                failure->message = "fixture configure rejection";
            }
            return false;
        }

        bool SubmitFrame(rendering::RenderFrameInfo&, rendering::RenderFrameSubmission& submission,
                         rendering::ViewportFailure* const failure) noexcept
        {
            ++submitCalls;
            if (submitSucceeds)
            {
                submission.serial = submitSerial;
                return true;
            }
            if (failure != nullptr)
            {
                *failure = {};
                failure->code = rendering::ViewportFailureCode::SubmissionFailure;
                failure->message = "fixture submit rejection";
            }
            return false;
        }

        bool AbandonFrame(rendering::RenderFrameInfo&, rendering::ViewportFailure* const failure) noexcept
        {
            ++abandonCalls;
            if (abandonSucceeds)
                return true;
            if (failure != nullptr)
            {
                *failure = {};
                failure->code = rendering::ViewportFailureCode::InvalidState;
                failure->message = "fixture abandon rejection";
            }
            return false;
        }
    };

    void TestSceneFrameTerminalBalance() noexcept
    {
        rendering::RenderFrameSetup frameSetup;
        frameSetup.mode = rendering::RenderingMode::Shaded;
        frameSetup.purpose = rendering::RenderFramePurpose::Normal;
        frameSetup.present = false;
        const rendering::RenderSceneHandle scene{2, 5};
        const rendering::RenderCameraHandle rootCamera{scene, 3, 7};
        const rendering::RenderCameraHandle roots[]{rootCamera};
        rendering::RenderFrameViewSetup viewSetup;
        viewSetup.scene = scene;
        viewSetup.rootCameras = roots;
        viewSetup.jitterIndex = 0;
        viewSetup.enableTemporalJitter = false;
        vanguard::u64 serial = 0;

        FakeSceneViewport viewport;
        viewport.beginSucceeds = false;
        viewport.beginFailure = rendering::ViewportFailureCode::OutputUnavailable;
        engine::FrameParticipantStatus status = editor::detail::SubmitEditorSceneFrame(viewport, frameSetup, viewSetup, serial);
        Check(static_cast<bool>(status) && serial == 0 && viewport.beginCalls == 1 && viewport.configureCalls == 0 &&
                  viewport.submitCalls == 0 && viewport.abandonCalls == 0,
              "OutputUnavailable scene Begin rejection creates no producer epoch or abandon");

        viewport = {};
        viewport.beginSucceeds = false;
        viewport.beginFailure = rendering::ViewportFailureCode::Busy;
        status = editor::detail::SubmitEditorSceneFrame(viewport, frameSetup, viewSetup, serial);
        Check(static_cast<bool>(status) && serial == 0 && viewport.beginCalls == 1 && viewport.configureCalls == 0 &&
                  viewport.submitCalls == 0 && viewport.abandonCalls == 0,
              "transient scene Begin rejection creates no producer epoch or abandon");

        viewport = {};
        viewport.configureSucceeds = false;
        status = editor::detail::SubmitEditorSceneFrame(viewport, frameSetup, viewSetup, serial);
        Check(!static_cast<bool>(status) && serial == 0 && viewport.beginCalls == 1 && viewport.configureCalls == 1 &&
                  viewport.submitCalls == 0 && viewport.abandonCalls == 1,
              "post-Begin scene view failure takes exactly one abandonment");

        viewport = {};
        viewport.configureSucceeds = false;
        viewport.abandonSucceeds = false;
        status = editor::detail::SubmitEditorSceneFrame(viewport, frameSetup, viewSetup, serial);
        Check(!static_cast<bool>(status) && serial == 0 && viewport.abandonCalls == 1,
              "scene configure rejection with cleanup failure still performs exactly one abandonment attempt");

        viewport = {};
        viewport.submitSucceeds = false;
        status = editor::detail::SubmitEditorSceneFrame(viewport, frameSetup, viewSetup, serial);
        Check(!static_cast<bool>(status) && serial == 0 && viewport.beginCalls == 1 && viewport.configureCalls == 1 &&
                  viewport.submitCalls == 1 && viewport.abandonCalls == 1,
              "synchronous scene Submit rejection takes exactly one abandonment and publishes no producer serial");

        viewport = {};
        viewport.submitSucceeds = false;
        viewport.abandonSucceeds = false;
        status = editor::detail::SubmitEditorSceneFrame(viewport, frameSetup, viewSetup, serial);
        Check(!static_cast<bool>(status) && serial == 0 && viewport.abandonCalls == 1,
              "scene Submit rejection with cleanup failure still performs exactly one abandonment attempt");

        viewport = {};
        viewport.submitSerial = 73;
        status = editor::detail::SubmitEditorSceneFrame(viewport, frameSetup, viewSetup, serial);
        Check(static_cast<bool>(status) && serial == 73 && viewport.beginCalls == 1 && viewport.configureCalls == 1 &&
                  viewport.submitCalls == 1 && viewport.abandonCalls == 0 && !viewport.sawPresent &&
                  viewport.mode == rendering::RenderingMode::Shaded && viewport.purpose == rendering::RenderFramePurpose::Normal &&
                  viewport.configuredScene == scene && viewport.configuredRootCount == 1 && viewport.configuredRoot == rootCamera &&
                  viewport.configuredRegionCount == 0 && viewport.configuredJitter == 0 && !viewport.configuredTemporalJitter,
              "accepted editor scene frame uses one selected root camera with no explicit regions, the ordinary Shaded/Normal path, no Present, no unresolved temporal jitter, and its exact producer serial");
    }

    struct ServiceOrder final
    {
        vanguard::u32 framePipeline = ~vanguard::u32{0};
        vanguard::u32 rendering = ~vanguard::u32{0};
        vanguard::u32 worldSession = ~vanguard::u32{0};
        vanguard::u32 ui = ~vanguard::u32{0};
        vanguard::u32 scene = ~vanguard::u32{0};
    };

    void VisitCompiledService(const app::CompiledServiceInfo& info, void* const userData) noexcept
    {
        auto& order = *static_cast<ServiceOrder*>(userData);
        if (info.service == engine::FramePipelineServiceId)
            order.framePipeline = info.startupIndex;
        else if (info.service == engine::RenderingServiceId)
            order.rendering = info.startupIndex;
        else if (info.service == engine::WorldSessionServiceId)
            order.worldSession = info.startupIndex;
        else if (info.service == editor::EditorUiServiceId)
            order.ui = info.startupIndex;
        else if (info.service == editor::EditorSceneOutputServiceId)
            order.scene = info.startupIndex;
    }

    void TestServiceDependencyTopology() noexcept
    {
        app::EngineHost host;
        Check(host.RegisterModule({engine::EngineModuleId, "engine-test", 1}), "engine test module registers");
        Check(host.RegisterModule({editor::EditorModuleId, "editor-test", 1}), "editor test module registers");
        Check(RegisterCompileOnlyService(host, engine::EngineModuleId, engine::FramePipelineServiceId, "framePipeline"),
              "frame-pipeline compile-only provider registers");
        Check(RegisterCompileOnlyService(host, engine::EngineModuleId, engine::RenderingServiceId, "rendering"),
              "rendering compile-only provider registers");
        Check(RegisterCompileOnlyService(host, engine::EngineModuleId, engine::WorldSessionServiceId, "worldSession"),
              "world-session compile-only provider registers");
        Check(RegisterCompileOnlyService(host, editor::EditorModuleId, editor::EditorUiServiceId, "editorUi"),
              "editor-UI compile-only provider registers");
        Check(editor::RegisterEditorSceneOutputService(host), "editor scene-output product service registers");

        app::HostFailure failure;
        Check(host.Compile(app::ApplicationProfile::Editor, &failure), "editor scene-output dependency graph compiles for Editor profile");
        ServiceOrder order;
        host.VisitCompiledServices(&VisitCompiledService, &order);
        Check(order.scene != ~vanguard::u32{0} && order.framePipeline < order.scene && order.rendering < order.scene &&
                  order.worldSession < order.scene && order.ui < order.scene,
              "editor scene-output service starts after FramePipeline, Rendering, WorldSession and EditorUi owners");

        app::EngineHost missingUiHost;
        Check(missingUiHost.RegisterModule({engine::EngineModuleId, "engine-test", 1}), "missing-UI engine module registers");
        Check(missingUiHost.RegisterModule({editor::EditorModuleId, "editor-test", 1}), "missing-UI editor module registers");
        Check(RegisterCompileOnlyService(missingUiHost, engine::EngineModuleId, engine::FramePipelineServiceId, "framePipeline"),
              "missing-UI frame-pipeline provider registers");
        Check(RegisterCompileOnlyService(missingUiHost, engine::EngineModuleId, engine::RenderingServiceId, "rendering"),
              "missing-UI rendering provider registers");
        Check(RegisterCompileOnlyService(missingUiHost, engine::EngineModuleId, engine::WorldSessionServiceId, "worldSession"),
              "missing-UI world-session provider registers");
        Check(editor::RegisterEditorSceneOutputService(missingUiHost), "missing-UI scene-output product service registers");
        failure = {};
        Check(!missingUiHost.Compile(app::ApplicationProfile::Editor, &failure) && failure.code == app::HostFailureCode::MissingDependency &&
                  failure.service == editor::EditorSceneOutputServiceId && failure.relatedService == editor::EditorUiServiceId,
              "editor scene-output service fails closed when EditorUi owner is absent");
    }
} // namespace

int main()
{
    vanguard::concurrency::InitializeMainThread();
    Check(vanguard::memory::Initialize(), "memory initializes");
    Check(vanguard::diagnostics::Initialize(vanguard::diagnostics::Mode::Synchronous, "editorSceneOutputTests"), "diagnostics initializes");
    Check(vanguard::containers::Initialize(), "containers initialize");

    TestSceneOutputContracts();
    TestParticipantAndGenerationPolicy();
    TestLatePublicationReceiptResolution();
    TestSceneFrameTerminalBalance();
    TestServiceDependencyTopology();
    vanguard::diagnostics::Shutdown();
    if (g_failures != 0)
        return 1;
    std::printf("editorSceneOutputTests: PASS\n");
    return 0;
}
