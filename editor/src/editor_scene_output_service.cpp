#include <vanguard/editor/editor_scene_output_service.hpp>

#include <vanguard/concurrency/atomic.hpp>
#include <vanguard/concurrency/thread.hpp>
#include <vanguard/engine/engine_services.hpp>
#include <vanguard/engine/rendering_service.hpp>
#include <vanguard/engine/world_session_service.hpp>
#include <vanguard/memory/memory.hpp>

#include <new>

namespace
{
    namespace app = vanguard::application;
    namespace editor = vanguard::editor;
    namespace engine = vanguard::engine;
    namespace rendering = vanguard::rendering;
    namespace rhi = vanguard::rhi;

    using SceneOutputGenerationRole = editor::detail::SceneOutputGenerationRole;

    struct SceneOutputGeneration final
    {
        rendering::ViewportManager* viewports = nullptr;
        rendering::RenderViewportHandle renderViewport;
        rendering::EngineViewportHandle engineViewport;
        editor::EditorTextureHandle editorTexture;
        rhi::Texture texture;
        rendering::ViewportExtent extent;
        vanguard::u64 identity = 0;
        vanguard::u64 outputRevision = 0;
        vanguard::u64 retireAfterUiFrame = 0;
        vanguard::concurrency::Atomic<vanguard::u64> producerSerial{0};
        SceneOutputGenerationRole role = SceneOutputGenerationRole::Vacant;
    };

    [[nodiscard]] bool ResolveSceneOutputReceipt(void* const userData, rendering::RenderOutputReceipt& receipt) noexcept
    {
        receipt = {};
        auto* const generation = static_cast<SceneOutputGeneration*>(userData);
        // Role transitions are main-thread policy. The immutable output identity and manager pointer
        // remain alive until the next UI BeginFrame CPU join, so the retained render worker only
        // synchronizes on the accepted producer serial here.
        if (generation == nullptr || generation->viewports == nullptr || !generation->renderViewport.IsValid() || generation->outputRevision == 0)
            return false;
        const vanguard::u64 producerSerial = generation->producerSerial.GetValue();
        return editor::detail::QueryEditorSceneOutputReceipt(*generation->viewports, generation->renderViewport,
                                                             generation->outputRevision, producerSerial, receipt);
    }

    [[nodiscard]] bool IsExpectedSceneReceipt(const SceneOutputGeneration& generation,
                                              const rendering::RenderOutputReceipt& receipt) noexcept
    {
        return receipt.IsReady() && receipt.viewport == generation.renderViewport && receipt.outputRevision == generation.outputRevision &&
               receipt.producerSerial == generation.producerSerial.GetValue() &&
               receipt.terminalState == rhi::ResourceState::ShaderResourceGraphics && receipt.terminalQueue == rhi::QueueType::Graphics &&
               receipt.readiness == rendering::RenderOutputPublicationReadinessKind::SameQueueContinuation;
    }

    [[nodiscard]] bool RevokeSceneOutputEditorTexture(const editor::EditorTextureHandle texture, void* const userData) noexcept
    {
        auto* const ui = static_cast<editor::EditorUiService*>(userData);
        return ui != nullptr && texture.IsValid() && ui->UnregisterTexture(texture);
    }

    class ManagedEditorSceneOutputService final : public editor::EditorSceneOutputService
    {
    public:
        ManagedEditorSceneOutputService() noexcept = default;

        [[nodiscard]] bool RequestOutputExtent(const rendering::ViewportExtent extent) noexcept override
        {
            if (!vanguard::concurrency::IsMainThread() || !extent.IsValid())
                return false;
            m_requestedExtent = extent;
            return true;
        }

        [[nodiscard]] bool ClearOutputDemand() noexcept override
        {
            if (!vanguard::concurrency::IsMainThread())
                return false;
            m_requestedExtent = {};
            return true;
        }

        [[nodiscard]] editor::EditorSceneOutputInfo GetPublishedOutput() const noexcept override
        {
            if (!vanguard::concurrency::IsMainThread())
                return {};
            const SceneOutputGeneration* const active = Find(SceneOutputGenerationRole::Active);
            return active != nullptr && active->editorTexture.IsValid()
                       ? editor::EditorSceneOutputInfo{active->editorTexture, active->extent, active->identity}
                       : editor::EditorSceneOutputInfo{};
        }

    protected:
        app::LifecycleStatus OnInitialize(app::ServiceContext& context) noexcept override
        {
            m_framePipeline = engine::FindFramePipelineService(context);
            m_rendering = engine::FindRenderingService(context);
            m_session = engine::FindWorldSessionService(context);
            m_ui = static_cast<editor::EditorUiService*>(context.Find(editor::EditorUiServiceId));
            if (m_framePipeline == nullptr || m_rendering == nullptr || m_session == nullptr || m_ui == nullptr)
                return app::LifecycleStatus::Failure("editor scene-output dependencies are unavailable");

            const engine::FrameParticipantDescriptor participant =
                editor::detail::MakeEditorSceneFrameParticipant(&ExecuteSceneFrame, this);
            engine::FrameFailure failure;
            if (!m_framePipeline->RegisterParticipant(participant, &failure))
                return app::LifecycleStatus::Failure(
                    editor::detail::SceneOutputFailureMessage(failure.message, "editor scene-output frame participant registration failed"));
            return app::LifecycleStatus::Success();
        }

        app::LifecycleStatus OnStart(app::ServiceContext&) noexcept override
        {
            rhi::SamplerStateDesc samplerDesc;
            samplerDesc.addressU = rhi::SamplerAddressMode::Clamp;
            samplerDesc.addressV = rhi::SamplerAddressMode::Clamp;
            samplerDesc.addressW = rhi::SamplerAddressMode::Clamp;
            rhi::Failure failure;
            m_sampler = rhi::SamplerState(rhi::AdoptReference, rhi::RequestSamplerState(samplerDesc, &failure));
            if (!m_sampler.IsValid())
                return app::LifecycleStatus::Failure("editor scene-output sampler creation failed");
            m_acceptFrames = true;
            return app::LifecycleStatus::Success();
        }

        app::LifecycleStatus OnQuiesce(app::ServiceContext&) noexcept override
        {
            m_acceptFrames = false;
            if (m_rendering != nullptr)
            {
                rendering::RenderCommandFailure commandFailure;
                if (!m_rendering->GetCommands().FlushPreviousFrameProcessing(&commandFailure))
                    return app::LifecycleStatus::Failure("editor scene-output retained rendering tail could not be joined during quiesce");
            }
            for (SceneOutputGeneration& generation : m_generations)
                if (!ReleaseGeneration(generation))
                    return app::LifecycleStatus::Failure("editor scene-output generation release failed during quiesce");
            m_sampler.Reset();
            return app::LifecycleStatus::Success();
        }

        app::LifecycleStatus OnDrain(app::ServiceContext&) noexcept override
        {
            for (const SceneOutputGeneration& generation : m_generations)
                if (generation.role != SceneOutputGenerationRole::Vacant || generation.renderViewport.IsValid() ||
                    generation.engineViewport.IsValid() || generation.editorTexture.IsValid())
                    return app::LifecycleStatus::Failure("editor scene-output ownership survived drain");
            return app::LifecycleStatus::Success();
        }

        app::LifecycleStatus OnShutdown(app::ServiceContext&) noexcept override
        {
            m_acceptFrames = false;
            m_sampler.Reset();
            m_framePipeline = nullptr;
            m_rendering = nullptr;
            m_session = nullptr;
            m_ui = nullptr;
            return app::LifecycleStatus::Success();
        }

    private:
        [[nodiscard]] SceneOutputGeneration* Find(const SceneOutputGenerationRole role) noexcept
        {
            for (SceneOutputGeneration& generation : m_generations)
                if (generation.role == role)
                    return &generation;
            return nullptr;
        }

        [[nodiscard]] const SceneOutputGeneration* Find(const SceneOutputGenerationRole role) const noexcept
        {
            for (const SceneOutputGeneration& generation : m_generations)
                if (generation.role == role)
                    return &generation;
            return nullptr;
        }

        [[nodiscard]] SceneOutputGeneration* FindVacant() noexcept
        {
            return Find(SceneOutputGenerationRole::Vacant);
        }

        void ResetGenerationRecord(SceneOutputGeneration& generation) noexcept
        {
            generation.viewports = nullptr;
            generation.renderViewport = {};
            generation.engineViewport = {};
            generation.editorTexture = {};
            generation.texture.Reset();
            generation.extent = {};
            generation.identity = 0;
            generation.outputRevision = 0;
            generation.retireAfterUiFrame = 0;
            generation.producerSerial.SetValue(0);
            generation.role = SceneOutputGenerationRole::Vacant;
        }

        [[nodiscard]] bool ReleaseGeneration(SceneOutputGeneration& generation) noexcept
        {
            if (generation.role == SceneOutputGenerationRole::Vacant)
                return true;
            if (generation.engineViewport.IsValid())
            {
                rendering::ViewportFailure failure;
                if (m_rendering == nullptr || !m_rendering->GetViewports().DestroyEngineViewport(generation.engineViewport, &failure))
                    return false;
                generation.engineViewport = {};
            }
            if (generation.editorTexture.IsValid())
            {
                editor::EditorUiFailure uiFailure;
                if (m_ui == nullptr || !m_ui->UnregisterTexture(generation.editorTexture, &uiFailure))
                    return false;
                generation.editorTexture = {};
            }
            if (generation.renderViewport.IsValid())
            {
                rendering::ViewportFailure failure;
                if (m_rendering == nullptr || !m_rendering->GetViewports().DestroyRenderViewport(generation.renderViewport, &failure))
                    return false;
                generation.renderViewport = {};
            }
            ResetGenerationRecord(generation);
            return true;
        }

        [[nodiscard]] bool CreateCandidate(const rendering::ViewportExtent extent) noexcept
        {
            if (!extent.IsValid() ||
                !editor::detail::CanAllocateSceneOutputCandidate(Find(SceneOutputGenerationRole::Candidate) != nullptr,
                                                                 Find(SceneOutputGenerationRole::Retiring) != nullptr) ||
                m_rendering == nullptr)
                return false;
            SceneOutputGeneration* const generation = FindVacant();
            if (generation == nullptr)
                return false;
            if (m_generationIdentity == ~vanguard::u64{0})
                return false;
            const vanguard::u64 identity = m_generationIdentity + 1u;
            generation->viewports = &m_rendering->GetViewports();
            generation->extent = extent;
            generation->role = SceneOutputGenerationRole::Candidate;

            const rhi::TextureDesc textureDesc = editor::detail::MakeEditorSceneTextureDesc(extent);
            rhi::Failure rhiFailure;
            generation->texture = rhi::Texture(rhi::AdoptReference, rhi::CreateTexture(textureDesc, {}, &rhiFailure));
            if (!generation->texture.IsValid())
            {
                ResetGenerationRecord(*generation);
                return false;
            }

            const rendering::RenderViewportDesc outputDesc =
                editor::detail::MakeEditorSceneRenderViewportDesc(extent, generation->texture.GetRef());
            rendering::ViewportFailure viewportFailure;
            if (!m_rendering->GetViewports().CreateRenderViewport(outputDesc, generation->renderViewport, &viewportFailure))
            {
                ResetGenerationRecord(*generation);
                return false;
            }

            rendering::RenderViewport* const output = m_rendering->GetViewports().Resolve(generation->renderViewport);
            if (output == nullptr || !output->IsValid() || output->GetOutputRevision() == 0)
            {
                static_cast<void>(ReleaseGeneration(*generation));
                return false;
            }

            rendering::EngineViewportDesc viewportDesc;
            viewportDesc.contextName = "EditorSceneOutput";
            viewportDesc.output = generation->renderViewport;
            viewportDesc.presentByDefault = false;
            if (!m_rendering->GetViewports().CreateEngineViewport(viewportDesc, generation->engineViewport, &viewportFailure))
            {
                static_cast<void>(ReleaseGeneration(*generation));
                return false;
            }

            generation->identity = identity;
            generation->outputRevision = output->GetOutputRevision();
            generation->producerSerial.SetValue(0);
            m_generationIdentity = identity;
            return true;
        }

        [[nodiscard]] bool QueryCurrentReceipt(const SceneOutputGeneration& generation,
                                               rendering::RenderOutputReceipt& receipt) const noexcept
        {
            receipt = {};
            const vanguard::u64 producerSerial = generation.producerSerial.GetValue();
            return producerSerial != 0 && generation.viewports != nullptr &&
                   generation.viewports->QueryOutputReceipt(generation.renderViewport, generation.outputRevision, producerSerial, receipt);
        }

        [[nodiscard]] bool PromoteCandidate(SceneOutputGeneration& candidate) noexcept
        {
            if (candidate.role != SceneOutputGenerationRole::Candidate || m_ui == nullptr || !m_sampler.IsValid())
                return false;
            rendering::RenderOutputReceipt receipt;
            if (!QueryCurrentReceipt(candidate, receipt) || !IsExpectedSceneReceipt(candidate, receipt))
                return false;

            editor::EditorTextureDesc textureDesc;
            textureDesc.texture = candidate.texture.GetRef();
            textureDesc.sampler = m_sampler.GetRef();
            textureDesc.colorSpace = editor::EditorTextureColorSpace::DisplaySrgb;
            textureDesc.publication = {candidate.renderViewport, candidate.outputRevision, ResolveSceneOutputReceipt, &candidate};
            editor::EditorUiFailure uiFailure;
            if (!m_ui->RegisterTexture(textureDesc, candidate.editorTexture, &uiFailure))
                return false;

            SceneOutputGeneration* const active = Find(SceneOutputGenerationRole::Active);
            SceneOutputGeneration* const retiring = Find(SceneOutputGenerationRole::Retiring);
            if (!editor::detail::PromoteSceneOutputGeneration(candidate.role, active != nullptr ? &active->role : nullptr,
                                                              active != nullptr ? &active->editorTexture : nullptr, retiring != nullptr,
                                                              m_ui->GetFrame(), active != nullptr ? &active->retireAfterUiFrame : nullptr,
                                                              &RevokeSceneOutputEditorTexture, m_ui))
            {
                if (!m_ui->UnregisterTexture(candidate.editorTexture))
                    return false;
                candidate.editorTexture = {};
                return false;
            }
            return true;
        }

        [[nodiscard]] bool ReconcileCandidate() noexcept
        {
            SceneOutputGeneration* const candidate = Find(SceneOutputGenerationRole::Candidate);
            if (candidate == nullptr)
                return true;
            const vanguard::u64 producerSerial = candidate->producerSerial.GetValue();
            if (producerSerial == 0)
            {
                if (editor::detail::ShouldDiscardSceneOutputCandidate(candidate->extent, m_requestedExtent))
                    return ReleaseGeneration(*candidate);
                return true;
            }

            rendering::RenderOutputReceipt receipt;
            if (!QueryCurrentReceipt(*candidate, receipt))
                return false;
            if (receipt.state == rendering::RenderOutputReceiptState::Pending)
                return true;
            if (receipt.state == rendering::RenderOutputReceiptState::Failed)
                return ReleaseGeneration(*candidate);
            if (!IsExpectedSceneReceipt(*candidate, receipt))
                return false;
            if (editor::detail::ShouldDiscardSceneOutputCandidate(candidate->extent, m_requestedExtent))
                return ReleaseGeneration(*candidate);
            return PromoteCandidate(*candidate);
        }

        [[nodiscard]] bool RetireReleasedUiGeneration() noexcept
        {
            SceneOutputGeneration* const retiring = Find(SceneOutputGenerationRole::Retiring);
            if (retiring == nullptr)
                return true;
            if (!editor::detail::CanRetireSceneOutputGeneration(retiring->retireAfterUiFrame, m_ui != nullptr ? m_ui->GetFrame() : 0))
                return true;
            return ReleaseGeneration(*retiring);
        }

        [[nodiscard]] engine::FrameParticipantStatus SubmitGeneration(SceneOutputGeneration& generation,
                                                                       const rendering::RenderCameraHandle camera) noexcept
        {
            if (generation.role != SceneOutputGenerationRole::Candidate && generation.role != SceneOutputGenerationRole::Active)
                return engine::FrameParticipantStatus::Failure("editor scene-output producer generation is not writable");
            rendering::EngineViewport* const viewport = m_rendering->GetViewports().Resolve(generation.engineViewport);
            if (viewport == nullptr || !viewport->IsValid())
                return engine::FrameParticipantStatus::Failure("editor scene-output EngineViewport is unavailable");

            const vanguard::u64 previousSerial = generation.producerSerial.GetValue();
            if (!editor::detail::CanAcceptSceneOutputProducer(generation.role, previousSerial))
                return engine::FrameParticipantStatus::Success();
            if (previousSerial != 0)
            {
                rendering::RenderOutputReceipt receipt;
                if (!QueryCurrentReceipt(generation, receipt))
                    return engine::FrameParticipantStatus::Failure("editor scene-output current producer receipt is unavailable");
                const editor::detail::ActiveSceneOutputReceiptAction action =
                    editor::detail::ClassifyActiveSceneOutputReceipt(receipt.state, IsExpectedSceneReceipt(generation, receipt));
                if (action == editor::detail::ActiveSceneOutputReceiptAction::Wait)
                    return engine::FrameParticipantStatus::Success();
                if (action == editor::detail::ActiveSceneOutputReceiptAction::Retire && generation.role == SceneOutputGenerationRole::Active)
                {
                    // This physical generation can no longer be treated as a usable current image.
                    // Keep it alive only through the UI frame that may already have captured its
                    // registration; the next UI BeginFrame join makes logical release safe.
                    if (m_ui == nullptr || !editor::detail::BeginSceneOutputRetirement(
                                               generation.role, generation.editorTexture, m_ui->GetFrame(), generation.retireAfterUiFrame,
                                               &RevokeSceneOutputEditorTexture, m_ui))
                        return engine::FrameParticipantStatus::Failure("editor scene-output failed generation could not enter retirement");
                    return engine::FrameParticipantStatus::Success();
                }
                if (action != editor::detail::ActiveSceneOutputReceiptAction::Produce)
                    return engine::FrameParticipantStatus::Failure("editor scene-output current producer receipt failed its publication contract");
            }

            rendering::RenderFrameSetup frameSetup;
            frameSetup.mode = rendering::RenderingMode::Shaded;
            frameSetup.purpose = rendering::RenderFramePurpose::Normal;
            frameSetup.present = false;
            frameSetup.features.geometryDiagnostics = m_rendering->IsFrameDiagnosticsEnabled();

            rendering::RenderFrameViewSetup viewSetup;
            viewSetup.scene = camera.scene;
            viewSetup.rootCameras = {&camera, 1};
            // The editor Scene output currently has no temporal reconstruction pass. Feeding a
            // per-frame projection jitter directly to the displayed texture therefore moves the
            // image itself instead of being resolved into a stable result.
            viewSetup.jitterIndex = 0;
            viewSetup.enableTemporalJitter = false;

            vanguard::u64 acceptedSerial = 0;
            const engine::FrameParticipantStatus status =
                editor::detail::SubmitEditorSceneFrame(*viewport, frameSetup, viewSetup, acceptedSerial);
            if (status && acceptedSerial != 0)
                generation.producerSerial.SetValue(acceptedSerial);
            return status;
        }

        [[nodiscard]] engine::FrameParticipantStatus SupplySceneFrame() noexcept
        {
            if (!m_acceptFrames || m_rendering == nullptr || m_session == nullptr || m_ui == nullptr)
                return engine::FrameParticipantStatus::Success();
            if (!RetireReleasedUiGeneration())
                return engine::FrameParticipantStatus::Failure("editor scene-output retiring generation could not be released");
            if (!ReconcileCandidate())
                return engine::FrameParticipantStatus::Failure("editor scene-output candidate generation could not be reconciled");

            SceneOutputGeneration* const currentActive = Find(SceneOutputGenerationRole::Active);
            if (currentActive != nullptr && currentActive->producerSerial.GetValue() != 0)
            {
                rendering::RenderOutputReceipt receipt;
                if (!QueryCurrentReceipt(*currentActive, receipt))
                    return engine::FrameParticipantStatus::Failure("editor scene-output active producer receipt is unavailable");
                const editor::detail::ActiveSceneOutputReceiptAction action =
                    editor::detail::ClassifyActiveSceneOutputReceipt(receipt.state, IsExpectedSceneReceipt(*currentActive, receipt));
                if (action == editor::detail::ActiveSceneOutputReceiptAction::Retire)
                {
                    if (!editor::detail::BeginSceneOutputRetirement(currentActive->role, currentActive->editorTexture, m_ui->GetFrame(),
                                                                    currentActive->retireAfterUiFrame, &RevokeSceneOutputEditorTexture, m_ui))
                        return engine::FrameParticipantStatus::Failure("editor scene-output failed generation could not enter retirement");
                }
                else if (action == editor::detail::ActiveSceneOutputReceiptAction::Reject)
                    return engine::FrameParticipantStatus::Failure("editor scene-output active producer receipt failed its publication contract");
            }

            // No-demand is a first-class steady state. Reconciliation above still retires failed
            // physical generations and drains obsolete candidates, but it never accepts a new
            // producer until a consumer explicitly requests an extent.
            const bool hasDemand = editor::detail::HasEditorSceneOutputDemand(m_requestedExtent);
            if (!hasDemand)
                return engine::FrameParticipantStatus::Success();
            const engine::WorldSessionStatus sessionStatus = m_session->GetStatus();
            if (sessionStatus != engine::WorldSessionStatus::Running)
                return engine::FrameParticipantStatus::Success();
            const engine::WorldSessionCameraBinding binding = m_session->GetCameraBinding();
            // Editor camera selection is optional and remains separately owned. A failed or
            // revoked Live selection is binding-local: do not invent a fallback camera and do
            // not terminate the editor merely because no scene producer can run this frame.
            const editor::detail::EditorSceneFrameAdmission admission =
                editor::detail::ClassifyEditorSceneFrameAdmission(
                    true, sessionStatus, binding.status, binding.camera.IsValid(),
                    binding.camera.IsValid() && m_rendering->GetCameras().IsAlive(binding.camera));
            if (admission == editor::detail::EditorSceneFrameAdmission::NoFrame)
                return engine::FrameParticipantStatus::Success();

            SceneOutputGeneration* active = Find(SceneOutputGenerationRole::Active);
            SceneOutputGeneration* candidate = Find(SceneOutputGenerationRole::Candidate);
            if (candidate == nullptr && Find(SceneOutputGenerationRole::Retiring) == nullptr &&
                editor::detail::SceneOutputNeedsReplacement(active != nullptr ? active->extent : rendering::ViewportExtent{}, m_requestedExtent))
            {
                if (!CreateCandidate(m_requestedExtent))
                    return engine::FrameParticipantStatus::Failure("editor scene-output replacement generation creation failed");
                candidate = Find(SceneOutputGenerationRole::Candidate);
            }

            SceneOutputGeneration* const producer = candidate != nullptr ? candidate : active;
            if (producer == nullptr)
                return engine::FrameParticipantStatus::Success();
            return SubmitGeneration(*producer, binding.camera);
        }

        static engine::FrameParticipantStatus ExecuteSceneFrame(const engine::FrameContext&, void* const userData) noexcept
        {
            auto* const service = static_cast<ManagedEditorSceneOutputService*>(userData);
            return service != nullptr ? service->SupplySceneFrame()
                                      : engine::FrameParticipantStatus::Failure("editor scene-output service is unavailable");
        }

        SceneOutputGeneration m_generations[editor::MaximumEditorSceneOutputGenerations];
        rhi::SamplerState m_sampler;
        rendering::ViewportExtent m_requestedExtent;
        vanguard::u64 m_generationIdentity = 0;
        engine::FramePipelineService* m_framePipeline = nullptr;
        engine::RenderingService* m_rendering = nullptr;
        engine::WorldSessionService* m_session = nullptr;
        editor::EditorUiService* m_ui = nullptr;
        bool m_acceptFrames = false;
    };

    app::Service* CreateEditorSceneOutputService(void*) noexcept
    {
        const vanguard::memory::MemoryBlock block =
            vanguard::memory::Allocate(vanguard::memory::PoolId::Editor, sizeof(ManagedEditorSceneOutputService), alignof(ManagedEditorSceneOutputService));
        return block ? ::new (block.address) ManagedEditorSceneOutputService() : nullptr;
    }

    void DestroyEditorSceneOutputService(app::Service* const service, void*) noexcept
    {
        if (service == nullptr)
            return;
        static_cast<ManagedEditorSceneOutputService*>(service)->~ManagedEditorSceneOutputService();
        vanguard::memory::MemoryBlock block{service, sizeof(ManagedEditorSceneOutputService), vanguard::memory::PoolId::Editor};
        vanguard::memory::Free(block);
    }
} // namespace

namespace vanguard::editor
{
    bool RegisterEditorSceneOutputService(application::EngineHost& host, application::HostFailure* const failure) noexcept
    {
        constexpr application::ServiceDependency dependencies[]{{engine::FramePipelineServiceId, application::DependencyKind::Required},
                                                                {engine::RenderingServiceId, application::DependencyKind::Required},
                                                                {engine::WorldSessionServiceId, application::DependencyKind::Required},
                                                                {EditorUiServiceId, application::DependencyKind::Required}};
        application::ServiceDescriptor descriptor;
        descriptor.id = EditorSceneOutputServiceId;
        descriptor.name = "editorSceneOutput";
        descriptor.profiles = application::ApplicationProfile::Editor | application::ApplicationProfile::Tool;
        descriptor.scope = application::ServiceScope::EditorWorkspace;
        descriptor.affinity = application::ThreadAffinity::MainThread;
        descriptor.dependencies = {dependencies, 4};
        descriptor.create = CreateEditorSceneOutputService;
        descriptor.destroy = DestroyEditorSceneOutputService;
        return host.RegisterService(EditorModuleId, descriptor, failure);
    }

    EditorSceneOutputService* FindEditorSceneOutputService(application::EngineHost& host) noexcept
    {
        return static_cast<EditorSceneOutputService*>(host.Find(EditorSceneOutputServiceId));
    }

    EditorSceneOutputService* FindEditorSceneOutputService(application::ServiceContext& context) noexcept
    {
        return static_cast<EditorSceneOutputService*>(context.Find(EditorSceneOutputServiceId));
    }
} // namespace vanguard::editor
