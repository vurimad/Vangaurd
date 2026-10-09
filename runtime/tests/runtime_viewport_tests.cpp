#include <vanguard/runtime/runtime_viewport_service.hpp>

#include <vanguard/concurrency/concurrency.hpp>
#include <vanguard/containers/containers.hpp>
#include <vanguard/diagnostics/diagnostics.hpp>
#include <vanguard/engine/engine_services.hpp>
#include <vanguard/memory/memory.hpp>

#include <cstdio>

namespace
{
    namespace app = vanguard::application;
    namespace engine = vanguard::engine;
    namespace pipelines = vanguard::pipelines;
    namespace rendering = vanguard::rendering;
    namespace rhi = vanguard::rhi;
    namespace runtime = vanguard::runtime;
    namespace shaders = vanguard::shaders;
    namespace window = vanguard::window;

    int g_failures = 0;

    void Check(const bool condition, const char* const message) noexcept
    {
        if (condition)
            return;
        std::fprintf(stderr, "[runtimeViewportTests] FAILED: %s\n", message);
        ++g_failures;
    }

    engine::FrameParticipantStatus ExecuteNoopParticipant(const engine::FrameContext&, void*) noexcept
    {
        return engine::FrameParticipantStatus::Success();
    }

    app::Service* CreateUnusedService(void*) noexcept
    {
        return nullptr;
    }

    void DestroyUnusedService(app::Service*, void*) noexcept {}

    bool RegisterCompileOnlyService(app::EngineHost& host, const app::ServiceId id, const char* const name) noexcept
    {
        app::ServiceDescriptor descriptor;
        descriptor.id = id;
        descriptor.name = name;
        descriptor.profiles = app::ApplicationProfile::Runtime;
        descriptor.scope = app::ServiceScope::Process;
        descriptor.affinity = app::ThreadAffinity::MainThread;
        descriptor.create = &CreateUnusedService;
        descriptor.destroy = &DestroyUnusedService;
        return host.RegisterService(engine::EngineModuleId, descriptor);
    }

    void TestRuntimeSdrOutputContract() noexcept
    {
        const pipelines::AttachmentSignature attachments = runtime::detail::MakeRuntimeSdrOutputAttachments();
        Check(attachments.colorCount == 1, "runtime SDR output declares exactly one color attachment");
        Check(attachments.colors[0].format == pipelines::Format::B8G8R8A8UNorm,
              "runtime SDR output attachment matches PresentationService SDR swapchain format");
        Check(attachments.colors[0].numericClass == shaders::NumericClass::FloatingPoint,
              "runtime SDR output attachment uses the renderer floating-point fragment-output class");
        Check(attachments.depthStencilFormat == pipelines::Format::Unknown && attachments.depthStencilClass == pipelines::DepthStencilClass::None,
              "runtime final output contract does not invent a depth attachment");
        Check(attachments.sampleCount == 1, "runtime final output contract is single-sampled");
        Check(runtime::detail::MatchesRuntimeSdrPresentation(rhi::Format::B8G8R8A8UNorm, rhi::ColorSpace::Srgb),
              "runtime SDR active presentation format/color space matches the declared renderer output contract");
        Check(!runtime::detail::MatchesRuntimeSdrPresentation(rhi::Format::R10G10B10A2UNorm, rhi::ColorSpace::Hdr10) &&
                  !runtime::detail::MatchesRuntimeSdrPresentation(rhi::Format::B8G8R8A8UNorm, rhi::ColorSpace::Hdr10),
              "runtime SDR contract rejects an active HDR or mismatched presentation output");
    }

    void TestParticipantContracts() noexcept
    {
        int presentationCookie = 1;
        const engine::FrameParticipantDescriptor presentation =
            runtime::detail::MakeRuntimePresentationParticipant(&ExecuteNoopParticipant, &presentationCookie);
        Check(presentation.id == runtime::RuntimePresentationReconcileParticipantId && presentation.id != engine::InvalidFrameParticipantId,
              "runtime presentation participant has the reserved nonzero identity");
        Check(presentation.phase == engine::FramePhase::Presentation && presentation.profiles == app::ApplicationProfile::Runtime &&
                  presentation.affinity == app::ThreadAffinity::MainThread,
              "runtime presentation reconciliation runs on the Runtime main thread in Presentation phase");
        Check(presentation.after.Size() == 1 && presentation.after[0] == engine::RenderingUpdateFrameParticipantId,
              "runtime presentation reconciliation follows the rendering update boundary");
        Check(presentation.execute == &ExecuteNoopParticipant && presentation.userData == &presentationCookie,
              "runtime presentation participant preserves its owner callback payload");

        int worldCookie = 2;
        const engine::FrameParticipantDescriptor world = runtime::detail::MakeRuntimeWorldFrameParticipant(&ExecuteNoopParticipant, &worldCookie);
        Check(world.id == runtime::RuntimeWorldFrameParticipantId && world.id != engine::InvalidFrameParticipantId && world.id != presentation.id,
              "runtime world-frame participant has the distinct reserved identity");
        Check(world.phase == engine::FramePhase::Render && world.profiles == app::ApplicationProfile::Runtime &&
                  world.affinity == app::ThreadAffinity::MainThread,
              "runtime world-frame supply runs on the Runtime main thread in Render phase");
        Check(world.after.Size() == 1 && world.after[0] == engine::RenderingFrameTickParticipantId,
              "runtime world-frame supply follows the single renderer FrameTick");
        Check(world.execute == &ExecuteNoopParticipant && world.userData == &worldCookie,
              "runtime world-frame participant preserves its owner callback payload");
    }

    void TestPresentationFailurePolicy() noexcept
    {
        rendering::PresentationFailure failure;
        failure.code = rendering::PresentationFailureCode::WindowFailure;
        failure.windowFailure.code = window::FailureCode::PresentationUnavailable;
        Check(runtime::detail::IsRetryablePresentationFailure(rendering::PresentationOutputState::AwaitingSurface, failure),
              "native presentation-surface unavailability is retryable while AwaitingSurface");
        Check(!runtime::detail::IsRetryablePresentationFailure(rendering::PresentationOutputState::Ready, failure),
              "surface-unavailable failure is not hidden for a non-AwaitingSurface output");

        failure = {};
        failure.code = rendering::PresentationFailureCode::RhiFailure;
        failure.rhiFailure.code = rhi::FailureCode::Busy;
        Check(runtime::detail::IsRetryablePresentationFailure(rendering::PresentationOutputState::AwaitingSurface, failure),
              "RHI Busy is retryable while AwaitingSurface");
        failure.rhiFailure.code = rhi::FailureCode::Timeout;
        Check(runtime::detail::IsRetryablePresentationFailure(rendering::PresentationOutputState::AwaitingSurface, failure),
              "RHI Timeout is retryable while AwaitingSurface");

        constexpr rhi::FailureCode terminalRhiFailures[]{rhi::FailureCode::DeviceLost, rhi::FailureCode::OutOfMemory,
                                                         rhi::FailureCode::Unsupported, rhi::FailureCode::BackendFailure};
        for (const rhi::FailureCode code : terminalRhiFailures)
        {
            failure.rhiFailure.code = code;
            Check(!runtime::detail::IsRetryablePresentationFailure(rendering::PresentationOutputState::AwaitingSurface, failure),
                  "terminal RHI failures are not hidden by AwaitingSurface state");
        }

        failure = {};
        failure.code = rendering::PresentationFailureCode::ViewportFailure;
        failure.viewportFailure.code = rendering::ViewportFailureCode::Busy;
        Check(!runtime::detail::IsRetryablePresentationFailure(rendering::PresentationOutputState::AwaitingSurface, failure),
              "only the locked native-surface and RHI transient classes are retried");
    }

    void TestPresentationRetirementGatePolicy() noexcept
    {
        using Stage = rendering::detail::PresentationRecreationStage;
        bool pending = true;
        constexpr Stage blockedStages[]{Stage::Suspended, Stage::SurfaceUnavailable, Stage::UnsupportedSurface, Stage::HdrPrerequisiteMissing};
        for (const Stage stage : blockedStages)
        {
            const rendering::detail::PresentationSurfaceRetirementDecision decision =
                rendering::detail::EvaluatePresentationSurfaceRetirement(pending, stage);
            Check(decision.pendingRetirement && !decision.drainBeforeCreate,
                  "pending retired swapchain ownership survives every no-recreation prerequisite without a device-wide drain");
            pending = decision.pendingRetirement;
        }
        const rendering::detail::PresentationSurfaceRetirementDecision recreatable =
            rendering::detail::EvaluatePresentationSurfaceRetirement(pending, Stage::ReadyToCreate);
        Check(recreatable.pendingRetirement && recreatable.drainBeforeCreate,
              "retired swapchain drain is selected exactly at the first recreatable swapchain-create boundary");
        const rendering::detail::PresentationSurfaceRetirementDecision failedDrain =
            rendering::detail::EvaluatePresentationSurfaceRetirement(recreatable.pendingRetirement, Stage::ReadyToCreate, false);
        Check(failedDrain.pendingRetirement && failedDrain.drainBeforeCreate,
              "failed retired-resource drain preserves pending ownership for retry");
        const rendering::detail::PresentationSurfaceRetirementDecision drained =
            rendering::detail::EvaluatePresentationSurfaceRetirement(recreatable.pendingRetirement, Stage::ReadyToCreate, true);
        Check(!drained.pendingRetirement && !drained.drainBeforeCreate &&
                  !rendering::detail::EvaluatePresentationSurfaceRetirement(false, Stage::ReadyToCreate).drainBeforeCreate,
              "successful retired-resource drain clears pending ownership before the replacement create attempt");
        Check(rendering::detail::ShouldInvalidatePresentationActiveFormat(false) &&
                  !rendering::detail::ShouldInvalidatePresentationActiveFormat(true),
              "presentation active format is invalidated whenever no live swapchain remains");
    }

    void TestPresentationDrawabilityPolicy() noexcept
    {
        using OutputState = rendering::PresentationOutputState;
        using ViewportState = rendering::RenderViewportState;
        Check(runtime::detail::IsDrawablePresentationState(OutputState::Ready, ViewportState::Ready, true, true, true, false, true, true),
              "Ready visible non-occluded SDR presentation output is drawable");
        Check(!runtime::detail::IsDrawablePresentationState(OutputState::AwaitingSurface, ViewportState::AwaitingOutput, true, true, true, false, true, false),
              "AwaitingSurface presentation output is a no-frame state");
        Check(!runtime::detail::IsDrawablePresentationState(OutputState::Suspended, ViewportState::Suspended, true, true, false, false, true, true),
              "suspended/minimized presentation output is a no-frame state");
        Check(!runtime::detail::IsDrawablePresentationState(OutputState::Ready, ViewportState::Ready, true, true, true, true, true, true),
              "occluded presentation output is a no-frame state");
        Check(!runtime::detail::IsDrawablePresentationState(OutputState::Ready, ViewportState::Ready, true, true, false, false, true, true),
              "invisible presentation output is a no-frame state");
        Check(!runtime::detail::IsDrawablePresentationState(OutputState::Ready, ViewportState::Ready, true, false, true, false, true, true),
              "Ready presentation output with a mismatched SDR contract is not drawable");
        Check(!runtime::detail::IsDrawablePresentationState(OutputState::Ready, ViewportState::Ready, true, true, true, false, false, true) &&
                  !runtime::detail::IsDrawablePresentationState(OutputState::Ready, ViewportState::Ready, true, true, true, false, true, false),
              "invalid extent or missing swapchain is a no-frame state");
        Check(!runtime::detail::IsDrawablePresentationState(OutputState::Failed, ViewportState::Failed, true, true, true, false, true, false),
              "Failed presentation output is terminal and never drawable");
    }

    void TestRuntimeFrameAdmissionPolicy() noexcept
    {
        using Admission = runtime::detail::RuntimeFrameAdmission;
        using CameraStatus = engine::WorldSessionCameraBindingStatus;
        using OutputState = rendering::PresentationOutputState;

        Check(runtime::detail::IsRuntimeWorldSessionReady(engine::WorldSessionStatus::Running) &&
                  !runtime::detail::IsRuntimeWorldSessionReady(engine::WorldSessionStatus::Idle) &&
                  !runtime::detail::IsRuntimeWorldSessionReady(engine::WorldSessionStatus::LoadingWorld) &&
                  !runtime::detail::IsRuntimeWorldSessionReady(engine::WorldSessionStatus::Stopping) &&
                  !runtime::detail::IsRuntimeWorldSessionReady(engine::WorldSessionStatus::Failed),
              "runtime scene submission is admitted only while the world session is Running");
        Check(runtime::detail::ClassifyRuntimeWorldCamera(CameraStatus::None, false, false) == Admission::NoFrame &&
                  runtime::detail::ClassifyRuntimeWorldCamera(CameraStatus::Pending, false, false) == Admission::NoFrame,
              "missing/loading runtime camera bindings conservatively produce no frame");
        Check(runtime::detail::ClassifyRuntimeWorldCamera(CameraStatus::Ready, true, true) == Admission::Produce,
              "only a live Ready runtime camera reaches frame submission");
        Check(runtime::detail::ClassifyRuntimeWorldCamera(CameraStatus::Failed, false, false) == Admission::Fail &&
                  runtime::detail::ClassifyRuntimeWorldCamera(CameraStatus::Ready, false, false) == Admission::Fail &&
                  runtime::detail::ClassifyRuntimeWorldCamera(CameraStatus::Ready, true, false) == Admission::Fail,
              "failed, invalid, or stale Ready runtime camera bindings fail closed");

        Check(runtime::detail::ClassifyRuntimePresentationOutput(false, OutputState::Failed, false) == Admission::Fail &&
                  runtime::detail::ClassifyRuntimePresentationOutput(true, OutputState::Failed, false) == Admission::Fail,
              "missing or Failed runtime presentation output is terminal");
        Check(runtime::detail::ClassifyRuntimePresentationOutput(true, OutputState::Ready, true) == Admission::Produce,
              "only a drawable Ready runtime presentation output reaches frame submission");
        Check(runtime::detail::ClassifyRuntimePresentationOutput(true, OutputState::AwaitingSurface, false) == Admission::NoFrame &&
                  runtime::detail::ClassifyRuntimePresentationOutput(true, OutputState::Suspended, false) == Admission::NoFrame &&
                  runtime::detail::ClassifyRuntimePresentationOutput(true, OutputState::Ready, false) == Admission::NoFrame,
              "awaiting, suspended, occluded/invisible, or otherwise non-drawable runtime outputs produce no frame");
    }

    struct FakeRuntimeViewport
    {
        bool beginSucceeds = true;
        bool configureSucceeds = true;
        bool submitSucceeds = true;
        bool abandonSucceeds = true;
        rendering::ViewportFailureCode beginFailure = rendering::ViewportFailureCode::InvalidState;
        vanguard::u32 beginCalls = 0;
        vanguard::u32 configureCalls = 0;
        vanguard::u32 submitCalls = 0;
        vanguard::u32 abandonCalls = 0;
        vanguard::u32 presentCalls = 0;

        bool BeginFrame(const rendering::RenderFrameSetup&, rendering::RenderFrameInfo&, rendering::ViewportFailure* const failure) noexcept
        {
            ++beginCalls;
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

        bool ConfigureViews(rendering::RenderFrameInfo&, const rendering::RenderFrameViewSetup&,
                            rendering::ViewportFailure* const failure) noexcept
        {
            ++configureCalls;
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

        bool SubmitFrame(rendering::RenderFrameInfo&, rendering::RenderFrameSubmission&,
                         rendering::ViewportFailure* const failure) noexcept
        {
            ++submitCalls;
            if (submitSucceeds)
                return true;
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

        void Present() noexcept
        {
            ++presentCalls;
        }
    };

    void TestFrameTerminalBalance() noexcept
    {
        rendering::RenderFrameSetup frameSetup;
        frameSetup.present = true;
        rendering::RenderFrameViewSetup viewSetup;

        FakeRuntimeViewport viewport;
        viewport.beginSucceeds = false;
        viewport.beginFailure = rendering::ViewportFailureCode::OutputUnavailable;
        engine::FrameParticipantStatus status = runtime::detail::SubmitRuntimeWorldFrame(viewport, frameSetup, viewSetup);
        Check(static_cast<bool>(status) && viewport.beginCalls == 1 && viewport.configureCalls == 0 && viewport.submitCalls == 0 &&
                  viewport.abandonCalls == 0 && viewport.presentCalls == 0,
              "transient Begin rejection creates no frame epoch and performs no abandon or supplier-side Present");

        viewport = {};
        viewport.beginSucceeds = false;
        viewport.beginFailure = rendering::ViewportFailureCode::Busy;
        status = runtime::detail::SubmitRuntimeWorldFrame(viewport, frameSetup, viewSetup);
        Check(static_cast<bool>(status) && viewport.beginCalls == 1 && viewport.configureCalls == 0 && viewport.submitCalls == 0 &&
                  viewport.abandonCalls == 0 && viewport.presentCalls == 0,
              "Busy Begin rejection is the second transient no-frame path and performs no abandon or supplier-side Present");

        viewport = {};
        viewport.beginSucceeds = false;
        viewport.beginFailure = rendering::ViewportFailureCode::InvalidState;
        status = runtime::detail::SubmitRuntimeWorldFrame(viewport, frameSetup, viewSetup);
        Check(!static_cast<bool>(status) && viewport.beginCalls == 1 && viewport.configureCalls == 0 && viewport.submitCalls == 0 &&
                  viewport.abandonCalls == 0 && viewport.presentCalls == 0,
              "terminal Begin rejection fails without abandoning a frame that never began");

        viewport = {};
        viewport.configureSucceeds = false;
        status = runtime::detail::SubmitRuntimeWorldFrame(viewport, frameSetup, viewSetup);
        Check(!static_cast<bool>(status) && viewport.beginCalls == 1 && viewport.configureCalls == 1 && viewport.submitCalls == 0 &&
                  viewport.abandonCalls == 1 && viewport.presentCalls == 0,
              "Configure failure after Begin performs exactly one Abandon and no supplier-side Present");

        viewport = {};
        viewport.submitSucceeds = false;
        status = runtime::detail::SubmitRuntimeWorldFrame(viewport, frameSetup, viewSetup);
        Check(!static_cast<bool>(status) && viewport.beginCalls == 1 && viewport.configureCalls == 1 && viewport.submitCalls == 1 &&
                  viewport.abandonCalls == 1 && viewport.presentCalls == 0,
              "synchronous Submit rejection after Begin performs exactly one Abandon and no supplier-side Present");

        viewport = {};
        status = runtime::detail::SubmitRuntimeWorldFrame(viewport, frameSetup, viewSetup);
        Check(static_cast<bool>(status) && viewport.beginCalls == 1 && viewport.configureCalls == 1 && viewport.submitCalls == 1 &&
                  viewport.abandonCalls == 0 && viewport.presentCalls == 0,
              "accepted Submit transfers frame ownership without Abandon or supplier-side Present");

        viewport = {};
        viewport.configureSucceeds = false;
        viewport.abandonSucceeds = false;
        status = runtime::detail::SubmitRuntimeWorldFrame(viewport, frameSetup, viewSetup);
        Check(!static_cast<bool>(status) && viewport.abandonCalls == 1,
              "failed cleanup is surfaced after exactly one abandonment attempt");
    }

    struct ServiceOrder
    {
        vanguard::u32 framePipeline = ~vanguard::u32{0};
        vanguard::u32 rendering = ~vanguard::u32{0};
        vanguard::u32 window = ~vanguard::u32{0};
        vanguard::u32 worldSession = ~vanguard::u32{0};
        vanguard::u32 runtimeViewport = ~vanguard::u32{0};
    };

    void VisitCompiledService(const app::CompiledServiceInfo& info, void* const userData) noexcept
    {
        auto& order = *static_cast<ServiceOrder*>(userData);
        if (info.service == engine::FramePipelineServiceId)
            order.framePipeline = info.startupIndex;
        else if (info.service == engine::RenderingServiceId)
            order.rendering = info.startupIndex;
        else if (info.service == engine::WindowServiceId)
            order.window = info.startupIndex;
        else if (info.service == engine::WorldSessionServiceId)
            order.worldSession = info.startupIndex;
        else if (info.service == runtime::RuntimeViewportServiceId)
            order.runtimeViewport = info.startupIndex;
    }

    void TestRuntimeServiceDependencyTopology() noexcept
    {
        app::EngineHost host;
        Check(host.RegisterModule({engine::EngineModuleId, "engine-test", 1}), "engine test module registers");
        Check(host.RegisterModule({runtime::RuntimeModuleId, "runtime-test", 1}), "runtime test module registers");
        Check(RegisterCompileOnlyService(host, engine::FramePipelineServiceId, "framePipeline"), "frame pipeline compile-only provider registers");
        Check(RegisterCompileOnlyService(host, engine::RenderingServiceId, "rendering"), "rendering compile-only provider registers");
        Check(RegisterCompileOnlyService(host, engine::WindowServiceId, "window"), "window compile-only provider registers");
        Check(RegisterCompileOnlyService(host, engine::WorldSessionServiceId, "worldSession"), "world-session compile-only provider registers");
        Check(runtime::RegisterRuntimeViewportService(host), "runtime viewport product service registers");

        app::HostFailure failure;
        Check(host.Compile(app::ApplicationProfile::Runtime, &failure), "runtime viewport dependency graph compiles for Runtime profile");
        ServiceOrder order;
        host.VisitCompiledServices(&VisitCompiledService, &order);
        Check(order.runtimeViewport != ~vanguard::u32{0}, "runtime viewport service is selected by Runtime profile");
        Check(order.framePipeline < order.runtimeViewport && order.rendering < order.runtimeViewport && order.window < order.runtimeViewport &&
                  order.worldSession < order.runtimeViewport,
              "runtime viewport service starts after all four required owners");

        app::EngineHost missingWindowHost;
        Check(missingWindowHost.RegisterModule({engine::EngineModuleId, "engine-test", 1}), "missing-window engine module registers");
        Check(missingWindowHost.RegisterModule({runtime::RuntimeModuleId, "runtime-test", 1}), "missing-window runtime module registers");
        Check(RegisterCompileOnlyService(missingWindowHost, engine::FramePipelineServiceId, "framePipeline"),
              "missing-window frame pipeline provider registers");
        Check(RegisterCompileOnlyService(missingWindowHost, engine::RenderingServiceId, "rendering"), "missing-window rendering provider registers");
        Check(RegisterCompileOnlyService(missingWindowHost, engine::WorldSessionServiceId, "worldSession"),
              "missing-window world-session provider registers");
        Check(runtime::RegisterRuntimeViewportService(missingWindowHost), "missing-window runtime viewport service registers");
        failure = {};
        Check(!missingWindowHost.Compile(app::ApplicationProfile::Runtime, &failure) && failure.code == app::HostFailureCode::MissingDependency &&
                  failure.service == runtime::RuntimeViewportServiceId && failure.relatedService == engine::WindowServiceId,
              "runtime viewport service fails closed when its Window owner is absent");
    }
} // namespace

int main()
{
    vanguard::concurrency::InitializeMainThread();
    Check(vanguard::memory::Initialize(), "memory initializes");
    Check(vanguard::diagnostics::Initialize(vanguard::diagnostics::Mode::Synchronous, "runtimeViewportTests"), "diagnostics initializes");
    Check(vanguard::containers::Initialize(), "containers initialize");

    TestRuntimeSdrOutputContract();
    TestParticipantContracts();
    TestPresentationFailurePolicy();
    TestPresentationRetirementGatePolicy();
    TestPresentationDrawabilityPolicy();
    TestRuntimeFrameAdmissionPolicy();
    TestFrameTerminalBalance();
    TestRuntimeServiceDependencyTopology();

    vanguard::diagnostics::Shutdown();
    if (g_failures == 0)
        std::printf("[runtimeViewportTests] all tests passed\n");
    return g_failures == 0 ? 0 : 1;
}
