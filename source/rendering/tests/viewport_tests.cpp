#include <vanguard/rendering/render_scene.hpp>
#include <vanguard/rendering/gpu_scene_types.hpp>
#include <vanguard/rendering/gpu_scene_tables.hpp>
#include <vanguard/rendering/gpu_scene_lifetime.hpp>
#include <vanguard/rendering/gpu_scene_visibility.hpp>
#include <vanguard/rendering/render_phase.hpp>
#include <vanguard/rendering/render_command_system.hpp>
#include <vanguard/rendering/render_view.hpp>
#include <vanguard/rendering/visibility_feedback.hpp>
#include <vanguard/rendering/viewport.hpp>

#include <vanguard/concurrency/atomic.hpp>
#include <vanguard/containers/containers.hpp>
#include <vanguard/memory/memory.hpp>
#include <vanguard/memory/pool.hpp>

#include <cstdio>
#include <cstring>
#include <limits>
#include <utility>

#include <Windows.h>
void RunRenderCameraTests(void (*check)(bool condition, const char* message) noexcept) noexcept;
void RunRenderFlowResourceAllocatorTests(void (*check)(bool condition, const char* message) noexcept) noexcept;
void RunRenderGraphTests(void (*check)(bool condition, const char* message) noexcept) noexcept;
int RunRenderGraphFatalCase(const char* name) noexcept;
void RunTextureUploadCandidateTests(void (*check)(bool condition, const char* message) noexcept) noexcept;

namespace
{
    namespace concurrency = vanguard::concurrency;
    namespace containers = vanguard::containers;
    namespace jobs = vanguard::jobs;
    namespace memory = vanguard::memory;
    namespace rendering = vanguard::rendering;
    namespace resources = vanguard::resources;
    namespace window = vanguard::window;
    using vanguard::u32;
    using vanguard::u64;
    using vanguard::usize;

    int g_failures = 0;

    void Check(const bool condition, const char* const message) noexcept
    {
        if (condition)
            return;
        std::fprintf(stderr, "[renderingTests] FAILED: %s\n", message);
        ++g_failures;
    }

    struct PayloadState
    {
        concurrency::Atomic<u32> retains{0};
        concurrency::Atomic<u32> releases{0};
        u32 expectedOrder = 0;
        bool fail = false;
    };

    struct ExecutionState
    {
        concurrency::Atomic<u32> executions{0};
        concurrency::Atomic<u32> tickContinuationExecutions{0};
        concurrency::Atomic<u32> continuationExecutions{0};
        concurrency::Atomic<u32> nextOrder{0};
        concurrency::Atomic<u32> orderFailures{0};
        concurrency::Atomic<u32> frameTickSceneCount{0};
        concurrency::Atomic<u32> retainedViewSetups{0};
        concurrency::Atomic<u32> lastRootCameraCount{0};
        concurrency::Atomic<u64> frameTickMutationEpoch{~u64{0}};
        concurrency::Atomic<u32> lastViewSetupSceneGeneration{0};
        concurrency::Atomic<u64> lastSerial{0};
        concurrency::Atomic<bool> failFrameTick{false};
        rendering::RenderViewport* expectedViewport = nullptr;
    };

    class TestResource final : public resources::ResourceObject
    {
    public:
        VANGUARD_USE_MEMORY_POOL(memory::pools::Resources);

        explicit TestResource(const resources::ResourceTypeId type) noexcept : m_type(type) {}

        [[nodiscard]] resources::ResourceTypeId GetType() const noexcept override
        {
            return m_type;
        }

    private:
        resources::ResourceTypeId m_type = resources::InvalidResourceTypeId;
    };

    struct ResourceHarness
    {
        concurrency::Atomic<u32> starts{0};
        concurrency::Atomic<u32> destructions{0};
    };

    void BeginResourceLoad(resources::ResourceRegistry& registry, const resources::ResourceRequest& request, void* const userData) noexcept
    {
        auto* const harness = static_cast<ResourceHarness*>(userData);
        static_cast<void>(harness->starts.Increment());
        static_cast<void>(registry.BeginLoading(request));
    }

    void DestroyResource(resources::ResourceObject* const resource, void* const userData) noexcept
    {
        auto* const harness = static_cast<ResourceHarness*>(userData);
        static_cast<void>(harness->destructions.Increment());
        VANGUARD_DELETE(static_cast<TestResource*>(resource));
    }

    void RetainPayload(void* const data) noexcept
    {
        auto* const payload = static_cast<PayloadState*>(data);
        static_cast<void>(payload->retains.Increment());
    }

    void ReleasePayload(void* const data) noexcept
    {
        auto* const payload = static_cast<PayloadState*>(data);
        static_cast<void>(payload->releases.Increment());
    }

    rendering::RenderFrameExecutionStatus ExecuteFrame(rendering::RenderFrameContext& context, void* const userData) noexcept
    {
        const rendering::RenderFrameInfo& frame = context.GetFrame();
        auto* const execution = static_cast<ExecutionState*>(userData);
        auto* const payload = static_cast<PayloadState*>(frame.GetPayload().data);
        const u32 order = execution->nextOrder.PostIncrement();
        if (payload == nullptr || payload->expectedOrder != order)
            static_cast<void>(execution->orderFailures.Increment());
        static_cast<void>(execution->executions.Increment());
        execution->lastSerial.SetValue(frame.GetSerial());
        if (!context.IsValid() || context.GetViewport() == nullptr || context.GetViewport() != execution->expectedViewport ||
            context.GetViewport() != frame.GetViewport())
            static_cast<void>(execution->orderFailures.Increment());
        if (frame.HasViewSetup())
        {
            const rendering::RenderFrameViewSetup setup = frame.GetViewSetup();
            if (frame.GetViewFamily().IsValid() || !setup.scene.IsValid() || setup.rootCameras.Empty())
                static_cast<void>(execution->orderFailures.Increment());
            else
            {
                static_cast<void>(execution->retainedViewSetups.Increment());
                execution->lastRootCameraCount.SetValue(setup.rootCameras.Size());
                execution->lastViewSetupSceneGeneration.SetValue(setup.scene.generation);
            }
        }
        static jobs::JobName continuationName{"RenderingTests.RenderFrameContinuation"};
        jobs::Task continuation =
            jobs::Task::Create([execution](const jobs::JobContext&) noexcept { static_cast<void>(execution->continuationExecutions.Increment()); });
        if (!continuation || !context.GetBuilder().Dispatch(continuationName, std::move(continuation)))
            return rendering::RenderFrameExecutionStatus::Failure("render frame continuation dispatch failed");
        return payload != nullptr && payload->fail ? rendering::RenderFrameExecutionStatus::Failure("intentional viewport test failure")
                                                   : rendering::RenderFrameExecutionStatus::Success();
    }

    rendering::RenderFrameExecutionStatus ExecuteFrameTick(rendering::RenderFrameTickContext& context, void* const userData) noexcept
    {
        if (!context.IsValid())
            return rendering::RenderFrameExecutionStatus::Failure("invalid rendering test FrameTick context");
        auto* const execution = static_cast<ExecutionState*>(userData);
        execution->frameTickSceneCount.SetValue(context.GetScenes().Size());
        if (!context.GetScenes().Empty())
            execution->frameTickMutationEpoch.SetValue(context.GetScenes()[0].mutationEpoch);
        static jobs::JobName continuationName{"RenderingTests.FrameTickContinuation"};
        jobs::Task continuation =
            jobs::Task::Create([execution](const jobs::JobContext&) noexcept { static_cast<void>(execution->tickContinuationExecutions.Increment()); });
        if (!continuation || !context.GetBuilder().Dispatch(continuationName, std::move(continuation)))
            return rendering::RenderFrameExecutionStatus::Failure("render FrameTick continuation dispatch failed");
        return execution->failFrameTick.GetValue() ? rendering::RenderFrameExecutionStatus::Failure("intentional FrameTick test failure")
                                                   : rendering::RenderFrameExecutionStatus::Success();
    }

    rendering::RenderFrameInfo Begin(rendering::ViewportManager& manager, const rendering::EngineViewportHandle viewport, const bool present = true) noexcept
    {
        rendering::RenderFrameInfo frame;
        rendering::RenderFrameSetup setup;
        setup.present = present;
        rendering::ViewportFailure failure;
        Check(manager.BeginFrame(viewport, setup, frame, &failure), "begin render frame");
        return frame;
    }

    rendering::RenderFrameInfo Begin(rendering::EngineViewport& viewport, const bool present = true) noexcept
    {
        rendering::RenderFrameInfo frame;
        rendering::RenderFrameSetup setup;
        setup.present = present;
        rendering::ViewportFailure failure;
        Check(viewport.BeginFrame(setup, frame, &failure), "begin render frame through EngineViewport");
        return frame;
    }

    [[nodiscard]] bool RunFatalRenderGraphCase(const char* const name) noexcept
    {
        char executable[MAX_PATH]{};
        const DWORD executableLength = GetModuleFileNameA(nullptr, executable, MAX_PATH);
        if (executableLength == 0 || executableLength == MAX_PATH)
            return false;
        char commandLine[MAX_PATH + 128]{};
        const int commandLength = std::snprintf(commandLine, sizeof(commandLine), "\"%s\" --render-graph-fatal %s", executable, name);
        if (commandLength <= 0 || static_cast<usize>(commandLength) >= sizeof(commandLine))
            return false;

        STARTUPINFOA startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        if (!CreateProcessA(executable, commandLine, nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process))
            return false;
        const DWORD wait = WaitForSingleObject(process.hProcess, 30000);
        DWORD exitCode = 0;
        const bool exited = wait == WAIT_OBJECT_0 && GetExitCodeProcess(process.hProcess, &exitCode) != FALSE;
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return exited && exitCode == 0xc0000602u;
    }
} // namespace

int main(const int argumentCount, char** const arguments)
{
    Check(memory::Initialize(), "memory initialization");
    Check(vanguard::containers::Initialize(), "containers initialization");
    jobs::Config jobsConfig = jobs::ToolConfig();
    jobsConfig.maxWorkers = 2;
    Check(jobs::Initialize(jobsConfig), "Jobs initialization");
    if (argumentCount == 3 && std::strcmp(arguments[1], "--render-graph-fatal") == 0)
        return RunRenderGraphFatalCase(arguments[2]);
    Check(RunFatalRenderGraphCase("cpu-cycle"), "CPU dependency cycles terminate in Shipping-active graph validation");
    Check(RunFatalRenderGraphCase("gpu-cycle"), "GPU dependency cycles terminate in Shipping-active graph validation");
    Check(RunFatalRenderGraphCase("invalid-group"), "out-of-range dependency groups terminate before indexing factory storage");
    Check(RunFatalRenderGraphCase("unbalanced-occurrence"), "scheduled node execution terminates when it does not match declared resource occurrences");
    Check(RunFatalRenderGraphCase("invalid-submission-sync"), "invalid compiled submission synchronization terminates in the ordered submission continuation");
    RunRenderCameraTests(&Check);
    const int failuresBeforeRenderGraph = g_failures;
    RunRenderGraphTests(&Check);
    if (g_failures == failuresBeforeRenderGraph)
        std::printf("Vanguard Render Graph tests passed.\n");
    const int failuresBeforeResourceFlowAllocator = g_failures;
    RunRenderFlowResourceAllocatorTests(&Check);
    if (g_failures == failuresBeforeResourceFlowAllocator)
        std::printf("Vanguard Resource Flow Allocator tests passed.\n");

    {
        rendering::RenderPhaseRegistry phases;
        rendering::RenderPhaseFailure phaseFailure;
        Check(phases.Initialize({}, &phaseFailure), "render phase registry initialization");
        Check(rendering::RegisterStandardRenderPhases(phases, &phaseFailure), "standard render phase registration");
        const rendering::RenderPhaseId opaque = phases.Find(rendering::standardRenderPhases::Opaque);
        const rendering::RenderPhaseId transparent = phases.Find("vanguard.render.transparent");
        Check(opaque.IsValid() && transparent.IsValid() && opaque != transparent, "stable render phase keys resolve to compact identities");

        rendering::RenderPhaseId duplicateOpaque;
        Check(phases.Register({"vanguard.render.opaque", 0, rendering::RenderPhaseSortMode::FrontToBack}, duplicateOpaque, &phaseFailure) &&
                  duplicateOpaque == opaque,
              "identical render phase registration is idempotent");
        Check(!phases.Register({"vanguard.render.opaque", 0, rendering::RenderPhaseSortMode::BackToFront}, duplicateOpaque, &phaseFailure) &&
                  phaseFailure.code == rendering::RenderPhaseFailureCode::IncompatibleDefinition,
              "incompatible render phase redefinition is rejected");
        Check(phases.Seal(&phaseFailure) && phases.IsSealed(), "render phase registry sealing");
        rendering::RenderPhaseId forbiddenPhase;
        Check(!phases.Register({"vanguard.render.late", 0, rendering::RenderPhaseSortMode::State}, forbiddenPhase, &phaseFailure) &&
                  phaseFailure.code == rendering::RenderPhaseFailureCode::RegistrySealed,
              "sealed render phase registry rejects late registration");

        rendering::RenderPhaseSet mainPhases;
        Check(mainPhases.Add(phases.Find(rendering::standardRenderPhases::DepthPrepass)) && mainPhases.Add(opaque) && mainPhases.Add(transparent) &&
                  mainPhases.Contains(opaque),
              "render phase set stores compact phase membership");

        rendering::RenderView views[2];
        views[0].id = {0, 1};
        views[0].family = {0, 1};
        views[0].purpose = rendering::RenderViewPurpose::Main;
        views[0].flags = rendering::RenderViewFlags::Primary | rendering::RenderViewFlags::TemporalHistory | rendering::RenderViewFlags::OcclusionCulling;
        views[0].phases = mainPhases;
        views[0].rect = {0, 0, 1920, 1080};
        views[0].frustum.planeCount = 1;
        views[0].frustum.planes[0].normal[2] = 1.0f;
        views[0].temporalIdentity = 1001;
        views[0].frameSerial = 17;
        views[0].name[0] = 'M';
        views[0].name[1] = 'a';
        views[0].name[2] = 'i';
        views[0].name[3] = 'n';

        views[1] = views[0];
        views[1].id = {1, 1};
        views[1].purpose = rendering::RenderViewPurpose::Shadow;
        views[1].flags = rendering::RenderViewFlags::ReverseDepth;
        views[1].phases.Clear();
        Check(views[1].phases.Add(phases.Find(rendering::standardRenderPhases::ShadowDepth)), "shadow view phase selection");
        views[1].temporalIdentity = 0;
        views[1].name[0] = 'S';
        views[1].name[1] = 'h';
        views[1].name[2] = 'a';
        views[1].name[3] = 'd';
        views[1].name[4] = 'o';
        views[1].name[5] = 'w';
        views[1].name[6] = '\0';

        rendering::RenderViewFamily family;
        family.id = {0, 1};
        family.views = views;
        family.viewCount = 2;
        family.frameSerial = 17;
        family.sceneIdentity = 9;
        family.sceneVersion = 4;
        rendering::RenderViewFailure viewFailure;
        Check(rendering::ValidateRenderViewFamily(family, phases, &viewFailure), "main and shadow views form one valid view family");

        views[0].origin.worldCell[0] = -12;
        views[0].origin.localPosition[1] = 34.5f;
        views[0].layerMask = 0x1122334455667788ull;
        views[0].temporalIdentity = 0xaabbccddeeff0011ull;
        views[0].matrices.worldToClip[15] = 1.0f;
        rendering::GpuView gpuView;
        Check(rendering::BuildGpuView(views[0], gpuView) && gpuView.worldCell[0] == -12 && gpuView.localPosition[1] == 34.5f &&
                  gpuView.worldToClip[15] == 1.0f && gpuView.layerMaskLow == 0x55667788u && gpuView.layerMaskHigh == 0x11223344u &&
                  gpuView.temporalIdentityLow == 0xeeff0011u && gpuView.temporalIdentityHigh == 0xaabbccddu &&
                  gpuView.phaseMaskLow == static_cast<u32>(mainPhases.Bits()) && gpuView.phaseMaskHigh == static_cast<u32>(mainPhases.Bits() >> 32u),
              "validated render view encodes into the exact GPU scene ABI");
        rendering::GpuInstanceHandle gpuInstance{7, 3};
        Check(gpuInstance.IsValid() && !rendering::GpuInstanceHandle{}.IsValid() && sizeof(rendering::GpuInstance) == 112 && sizeof(rendering::GpuView) == 544,
              "GPU scene handles and structured-buffer strides are stable");
        constexpr rendering::GpuSceneLinearAddress firstInstancePageTwo =
            rendering::DecodeGpuSceneIndex<rendering::GpuInstance>(rendering::GpuSceneElementsPerPage<rendering::GpuInstance>() * 2u + 7u);
        Check(firstInstancePageTwo.page == 2 && firstInstancePageTwo.element == 7 &&
                  rendering::GpuSceneElementsPerPage<rendering::GpuMaterialIndex>() == 262'144 && sizeof(rendering::GpuSceneTableDirectory) == 32 &&
                  sizeof(rendering::GpuScenePageDirectoryEntry) == 16 && rendering::GpuSceneLayoutVersion == 10 &&
                  offsetof(rendering::GpuInstance, sceneIndex) == 76 && offsetof(rendering::GpuInstance, sceneGeneration) == 108 &&
                  static_cast<u32>(rendering::GpuSceneTableKind::TextureResidency) == 18 &&
                  static_cast<u32>(rendering::GpuSceneTableKind::MaterialParameterWord) == 19 &&
                  rendering::GpuSceneElementsPerPage<rendering::GpuMaterialParameterWord>() == 262'144 &&
                  sizeof(rendering::GpuTextureResidency) == 16,
              "GPU Scene paged addressing is stable and shader-compatible");
        constexpr rendering::GpuSceneAllocation allocation{rendering::GpuSceneTableKind::Instance, 19, 1, 7};
        Check(allocation.IsValid() && allocation.AsSlotHandle<rendering::GpuInstanceHandle>() == rendering::GpuInstanceHandle{19, 7},
              "GPU Scene allocation converts single identities to typed generational handles");

        rendering::GpuView visibilityViews[2]{};
        rendering::GpuInstanceIndex visibilityCandidates[512]{};
        rendering::GpuVisibilityWorkRange visibilityWork[8]{};
        rendering::GpuVisibilityResultRange visibilityResults[2]{};
        rendering::GpuVisibilityPlanBuilder visibilityBuilder;
        rendering::GpuVisibilityBuildFailure visibilityFailure;
        Check(visibilityBuilder.Begin({visibilityViews, visibilityCandidates, visibilityWork, visibilityResults}, 17, &visibilityFailure),
              "GPU visibility planning begins over caller-owned staging storage");
        rendering::GpuVisibilityCandidateReservation mainCandidates;
        rendering::GpuVisibilityCandidateReservation shadowCandidates;
        Check(visibilityBuilder.ReserveView(gpuView, 257, 192, mainCandidates, &visibilityFailure) && mainCandidates.destination == visibilityCandidates &&
                  mainCandidates.capacity == 257,
              "main-view candidates reserve their final staging destination");
        rendering::GpuView shadowGpuView = gpuView;
        shadowGpuView.viewIndex = 1;
        shadowGpuView.viewGeneration = 3;
        Check(visibilityBuilder.ReserveView(shadowGpuView, 128, 64, shadowCandidates, &visibilityFailure) &&
                  shadowCandidates.destination == visibilityCandidates + 257,
              "another view receives a disjoint candidate reservation");
        for (u32 index = 0; index < mainCandidates.capacity; ++index)
            mainCandidates.destination[index] = index + 10;
        for (u32 index = 0; index < shadowCandidates.capacity; ++index)
            shadowCandidates.destination[index] = index + 1000;
        rendering::GpuVisibilityCandidateReservation prematureShadow = shadowCandidates;
        Check(!visibilityBuilder.CompleteView(prematureShadow, 128, &visibilityFailure) &&
                  visibilityFailure.code == rendering::GpuVisibilityBuildFailureCode::CompletionOrderViolation,
              "visibility planning rejects nondeterministic view completion order");
        Check(visibilityBuilder.CompleteView(mainCandidates, 257, &visibilityFailure) &&
                  visibilityBuilder.CompleteView(shadowCandidates, 128, &visibilityFailure),
              "candidate producers complete their direct-write reservations");
        rendering::GpuVisibilityPlan visibilityPlan;
        Check(visibilityBuilder.Finalize(visibilityPlan, &visibilityFailure) && visibilityPlan.IsValid() && visibilityPlan.views.Size() == 2 &&
                  visibilityPlan.candidates.Size() == 385 && visibilityPlan.workRanges.Size() == 4 && visibilityPlan.visibleCapacity == 256 &&
                  visibilityPlan.workRanges[0].candidateOffset == 0 && visibilityPlan.workRanges[0].candidateCount == rendering::GpuVisibilityThreadsPerGroup &&
                  visibilityPlan.workRanges[2].candidateOffset == 256 && visibilityPlan.workRanges[2].candidateCount == 1 &&
                  visibilityPlan.workRanges[3].candidateOffset == 257 && visibilityPlan.results[1].visibleOffset == 192 &&
                  sizeof(rendering::GpuVisibilityConstants) == 64,
              "GPU visibility plan produces bounded per-view work and output partitions");

        rendering::GpuView capacityViews[2]{};
        rendering::GpuInstanceIndex capacityCandidates[512]{};
        rendering::GpuVisibilityWorkRange capacityWork[3]{};
        rendering::GpuVisibilityResultRange capacityResults[2]{};
        Check(visibilityBuilder.Begin({capacityViews, capacityCandidates, capacityWork, capacityResults}, 18, &visibilityFailure),
              "GPU visibility capacity audit begins");
        rendering::GpuVisibilityCandidateReservation capacityMain;
        rendering::GpuVisibilityCandidateReservation capacityShadow;
        Check(visibilityBuilder.ReserveView(gpuView, 257, 257, capacityMain, &visibilityFailure) &&
                  !visibilityBuilder.ReserveView(shadowGpuView, 1, 1, capacityShadow, &visibilityFailure) &&
                  visibilityFailure.code == rendering::GpuVisibilityBuildFailureCode::CapacityExceeded,
              "outstanding views cannot collectively overbook work-range storage");
        visibilityBuilder.Cancel();

        rendering::GpuView sparseViews[1]{};
        rendering::GpuInstanceIndex sparseCandidates[257]{};
        rendering::GpuVisibilityWorkRange sparseWork[3]{};
        rendering::GpuVisibilityResultRange sparseResults[1]{};
        Check(visibilityBuilder.Begin({sparseViews, sparseCandidates, sparseWork, sparseResults}, 19, &visibilityFailure),
              "sparse GPU visibility planning begins");
        rendering::GpuVisibilityCandidateReservation sparseReservation;
        Check(visibilityBuilder.ReserveViewRanges(gpuView, 257, 3, 128, sparseReservation, &visibilityFailure),
              "sparse candidate production reserves disjoint filled-prefix capacity");
        sparseReservation.destination[0] = 41;
        sparseReservation.destination[200] = 73;
        const rendering::GpuVisibilityCandidateRange sparseRanges[3]{{0, 1}, {200, 1}, {256, 0}};
        Check(visibilityBuilder.CompleteViewRanges(sparseReservation, sparseRanges, &visibilityFailure),
              "sparse candidate ranges complete without gathering their disjoint prefixes");
        rendering::GpuVisibilityPlan sparsePlan;
        Check(visibilityBuilder.Finalize(sparsePlan, &visibilityFailure) && sparsePlan.candidates.Size() == 201 && sparsePlan.workRanges.Size() == 2 &&
                  sparsePlan.workRanges[0].candidateOffset == 0 && sparsePlan.workRanges[1].candidateOffset == 200,
              "GPU visibility work references only filled candidate prefixes and skips holes");

        views[1].id = views[0].id;
        Check(!rendering::ValidateRenderViewFamily(family, phases, &viewFailure) && viewFailure.code == rendering::RenderViewFailureCode::DuplicateView,
              "view family rejects duplicate view identities");
        views[1].id = {1, 1};

        rendering::RenderView invalidPhaseView = views[0];
        invalidPhaseView.phases = rendering::RenderPhaseSet(1ull << 63u);
        Check(!rendering::ValidateRenderView(invalidPhaseView, phases, &viewFailure) && viewFailure.code == rendering::RenderViewFailureCode::UnknownPhase,
              "render view rejects phase bits outside the sealed registry");
        Check(phases.Shutdown(&phaseFailure), "render phase registry shutdown");
    }

    ResourceHarness resourceHarness;
    resources::ResourceRegistry resourceRegistry;
    Check(resourceRegistry.Initialize(), "test resource registry initialization");
    const resources::ResourceTypeId meshType = resources::HashTypeName("vanguard.mesh");
    const resources::ResourceTypeId materialType = resources::HashTypeName("vanguard.material");
    Check(resourceRegistry.RegisterLoader({meshType, "rendering test mesh loader", BeginResourceLoad, DestroyResource, &resourceHarness}),
          "test mesh loader registration");
    Check(resourceRegistry.RegisterLoader({materialType, "rendering test material loader", BeginResourceLoad, DestroyResource, &resourceHarness}),
          "test material loader registration");
    const resources::ResourceReference meshReference(resources::ResourcePath::FromString("meshes/render_scene_payload.vmesh"), meshType);
    const resources::ResourceReference materialReference(resources::ResourcePath::FromString("materials/render_scene_payload.vmat"), materialType);
    resources::ResourceRequest meshRequest = resourceRegistry.Request(meshReference);
    resources::ResourceRequest materialRequest = resourceRegistry.Request(materialReference);
    Check(resourceRegistry.Publish(meshRequest, VANGUARD_NEW(TestResource)(meshType)), "test mesh resource publication");
    Check(resourceRegistry.Publish(materialRequest, VANGUARD_NEW(TestResource)(materialType)), "test material resource publication");
    meshRequest.Wait();
    materialRequest.Wait();
    resources::ResourceHandle meshHandle = meshRequest.Acquire();
    resources::ResourceHandle materialHandle = materialRequest.Acquire();
    Check(meshHandle.IsValid() && materialHandle.IsValid(), "test resource handles acquired");

    {
        rendering::RenderSceneManager scenes;
        rendering::RenderSceneFailure sceneFailure;
        Check(scenes.Initialize({}, &sceneFailure), "RenderSceneManager initialization");

        rendering::RenderSceneDesc sceneDesc;
        sceneDesc.name = "Runtime scene";
        sceneDesc.mode = rendering::RenderSceneMode::Runtime;
        sceneDesc.maximumProxies = 4096;
        sceneDesc.maximumPendingProxyMutations = 32;
        sceneDesc.maximumViews = 4;
        rendering::RenderSceneHandle scene;
        Check(scenes.CreateScene(sceneDesc, scene, &sceneFailure) && scene.IsValid(), "runtime RenderScene creation");

        rendering::RenderSceneDesc editorSceneDesc = sceneDesc;
        editorSceneDesc.name = "Detached editor scene";
        editorSceneDesc.mode = rendering::RenderSceneMode::Editor;
        editorSceneDesc.maximumProxies = 8;
        editorSceneDesc.maximumPendingProxyMutations = 8;
        editorSceneDesc.maximumViews = 1;
        editorSceneDesc.allowFramePipelineParticipation = false;
        rendering::RenderSceneHandle editorScene;
        Check(scenes.CreateScene(editorSceneDesc, editorScene, &sceneFailure), "non-participating editor RenderScene creation");

        rendering::RenderSceneDesc previewSceneDesc = editorSceneDesc;
        previewSceneDesc.name = "Participating preview scene";
        previewSceneDesc.mode = rendering::RenderSceneMode::Preview;
        previewSceneDesc.allowFramePipelineParticipation = true;
        rendering::RenderSceneHandle previewScene;
        Check(scenes.CreateScene(previewSceneDesc, previewScene, &sceneFailure), "participating preview RenderScene creation");

        rendering::RenderSceneDesc thumbnailSceneDesc = previewSceneDesc;
        thumbnailSceneDesc.name = "Participating thumbnail scene";
        thumbnailSceneDesc.mode = rendering::RenderSceneMode::Thumbnail;
        rendering::RenderSceneHandle thumbnailScene;
        Check(scenes.CreateScene(thumbnailSceneDesc, thumbnailScene, &sceneFailure), "participating thumbnail RenderScene creation");

        containers::ArraySpan<const rendering::RenderSceneHandle> participatingScenes = scenes.GetFramePipelineScenes();
        Check(participatingScenes.Count() == 3 && participatingScenes[0] == scene && participatingScenes[1] == previewScene &&
                  participatingScenes[2] == thumbnailScene && scenes.GetStats().framePipelineScenes == 3,
              "frame-pipeline scene directory is dense, allocation-free, and excludes opted-out scenes");

        const rendering::RenderSceneHandle stalePreviewScene = previewScene;
        Check(scenes.DestroyScene(previewScene, &sceneFailure), "middle participating RenderScene destruction");
        participatingScenes = scenes.GetFramePipelineScenes();
        Check(participatingScenes.Count() == 2 && participatingScenes[0] == scene && participatingScenes[1] == thumbnailScene,
              "frame-pipeline scene directory repairs moved indices in constant time");

        previewSceneDesc.name = "Reused preview scene";
        rendering::RenderSceneHandle replacementPreviewScene;
        Check(scenes.CreateScene(previewSceneDesc, replacementPreviewScene, &sceneFailure) && replacementPreviewScene.index == stalePreviewScene.index &&
                  replacementPreviewScene.generation != stalePreviewScene.generation,
              "reused participating RenderScene slot advances its generation");
        participatingScenes = scenes.GetFramePipelineScenes();
        Check(participatingScenes.Count() == 3 && participatingScenes[0] == scene && participatingScenes[1] == thumbnailScene &&
                  participatingScenes[2] == replacementPreviewScene,
              "reused scene generation is appended exactly once to the dense frame directory");
        Check(scenes.DestroyScene(replacementPreviewScene, &sceneFailure) && scenes.DestroyScene(thumbnailScene, &sceneFailure) &&
                  scenes.DestroyScene(editorScene, &sceneFailure) && scenes.GetFramePipelineScenes().Count() == 1 && scenes.GetFramePipelineScenes()[0] == scene,
              "mixed scene modes retire without scanning or disturbing the remaining runtime scene");

        u64 updateTick = 1;
        const auto executeSceneUpdate = [&](rendering::RenderSceneUpdateResult& update) noexcept
        {
            rendering::RenderSceneFramePrepareResult prepare;
            if (!scenes.PrepareSceneUpdate(scene, updateTick++, prepare, &sceneFailure))
                return false;
            jobs::Builder builder({jobs::Priority::RenderPath, jobs::Affinity::AnyWorker});
            if (!scenes.ExecuteSceneUpdate(scene, builder, update, &sceneFailure))
                return false;
            if (!update.dispatched)
                return true;
            jobs::Counter completion = builder.ExtractCounter();
            return completion.IsValid() && completion.Wait();
        };

        rendering::RenderProxyDesc proxyDesc;
        proxyDesc.scene = scene;
        proxyDesc.typeId = 7;
        proxyDesc.producerId = 42;
        proxyDesc.producerGeneration = 3;
        proxyDesc.bounds.minimum[0] = -1.0f;
        proxyDesc.bounds.minimum[1] = -2.0f;
        proxyDesc.bounds.minimum[2] = -3.0f;
        proxyDesc.bounds.maximum[0] = 1.0f;
        proxyDesc.bounds.maximum[1] = 2.0f;
        proxyDesc.bounds.maximum[2] = 3.0f;
        proxyDesc.visibility = rendering::RenderProxyVisibilityFlags::Visible | rendering::RenderProxyVisibilityFlags::CastsShadow;
        proxyDesc.layerMask = 0x15ull;
        proxyDesc.visibilityMask = 0x3u;
        proxyDesc.debugName = "Runtime proxy";

        rendering::RenderProxyHandle proxy;
        Check(scenes.CreateProxy(proxyDesc, proxy, &sceneFailure) && proxy.IsValid(), "RenderProxy creation");
        rendering::RenderProxyTransform movedTransform;
        movedTransform.row0[3] = 12.0f;
        movedTransform.row1[3] = 20.0f;
        movedTransform.row2[3] = 30.0f;
        rendering::RenderProxyBounds movedBounds = proxyDesc.bounds;
        movedBounds.minimum[0] = 11.0f;
        movedBounds.maximum[0] = 13.0f;
        Check(scenes.UpdateProxyTransform(proxy, movedTransform, movedBounds, 5, &sceneFailure), "RenderProxy relink enters the retained update queue");

        // TODO: This former assertion does not compile against the snapshot-free RenderScene API. Replace it with a
        // purpose-specific relink-state query when test work resumes; do not restore broad proxy copies.
        rendering::RenderSceneUpdateResult firstUpdate;
        Check(executeSceneUpdate(firstUpdate) && firstUpdate.mutationEpoch != 0, "explicit scene update applies the newest retained proxy state");

        rendering::VisibilityQueryRequest visibilityRequest;
        visibilityRequest.scene = scene;
        visibilityRequest.mutationEpoch = firstUpdate.mutationEpoch;
        visibilityRequest.useBounds = false;
        containers::DynamicArray<rendering::RenderProxyHandle> visibleProxies(memory::pools::Rendering::GetInstance());
        rendering::VisibilityQueryResult visibilityResult;
        Check(scenes.CollectVisibleProxies(visibilityRequest, visibleProxies, visibilityResult, &sceneFailure) && visibilityResult.completed &&
                  visibleProxies.Size() == 1 && visibleProxies[0] == proxy,
              "visibility query consumes the exact completed live scene epoch");

        containers::DynamicArray<rendering::VisibilityQueryBatch> visibilityBatches(memory::pools::Rendering::GetInstance());
        rendering::VisibilityQueryPlan visibilityPlan;
        Check(scenes.BuildVisibilityQueryPlan(scene, firstUpdate.mutationEpoch, 1, visibilityBatches, visibilityPlan, &sceneFailure) &&
                  visibilityPlan.IsValid() && visibilityBatches.Size() != 0,
              "visibility batching is planned directly over completed spatial state");
        containers::DynamicArray<rendering::RenderProxyHandle> batchProxies(memory::pools::Rendering::GetInstance());
        rendering::VisibilityQueryResult batchResult;
        Check(scenes.CollectVisibleProxyBatch(visibilityRequest, visibilityBatches[0], batchProxies, batchResult, &sceneFailure),
              "visibility batch consumes live spatial state without a copied publication image");

        rendering::MeshProxyDesc meshDesc;
        meshDesc.proxy = proxyDesc;
        meshDesc.proxy.typeId = 9;
        meshDesc.proxy.debugName = "Mesh proxy";
        meshDesc.mesh = meshReference;
        meshDesc.material = materialReference;
        meshDesc.meshHandle = meshHandle;
        meshDesc.materialHandle = materialHandle;
        meshDesc.submeshMask = 0x7u;
        rendering::RenderProxyHandle meshProxy;
        Check(scenes.CreateMeshProxy(meshDesc, meshProxy, &sceneFailure), "typed mesh proxy creation");
        // TODO: The former typed-payload snapshot assertion is intentionally disabled. Add a narrow resource-binding
        // inspection contract if editor validation genuinely requires one; do not restore copied payload snapshots.

        Check(scenes.UpdateProxyLayerMask(proxy, 0x22ull, &sceneFailure), "direct proxy metadata mutation");
        rendering::RenderSceneUpdateResult secondUpdate;
        Check(executeSceneUpdate(secondUpdate) && secondUpdate.mutationEpoch > firstUpdate.mutationEpoch,
              "next explicit scene update publishes a newer completed mutation epoch");

        rendering::VisibilityFeedbackService feedback;
        Check(feedback.Initialize(scenes, scene, {16, 4}, &sceneFailure), "visibility feedback initializes as a scene-relative non-renderable service");
        rendering::VisibilityProbeDesc visibleProbeDesc;
        visibleProbeDesc.bounds = movedBounds;
        visibleProbeDesc.queryMask = 1;
        visibleProbeDesc.debugName = "Visible streaming probe";
        rendering::VisibilityProbeHandle visibleProbe;
        Check(feedback.CreateProbe(visibleProbeDesc, visibleProbe, &sceneFailure), "visibility feedback creates a probe outside RenderScene proxy storage");
        rendering::VisibilityProbeDesc hiddenProbeDesc = visibleProbeDesc;
        hiddenProbeDesc.bounds.minimum[0] = -20.0f;
        hiddenProbeDesc.bounds.maximum[0] = -19.0f;
        hiddenProbeDesc.debugName = "Hidden streaming probe";
        rendering::VisibilityProbeHandle hiddenProbe;
        Check(feedback.CreateProbe(hiddenProbeDesc, hiddenProbe, &sceneFailure), "visibility feedback creates an independently generated second probe");
        rendering::VisibilityProbeDesc familyProbeDesc = visibleProbeDesc;
        familyProbeDesc.viewPolicy.kind = rendering::VisibilityViewPolicyKind::ViewFamily;
        familyProbeDesc.viewPolicy.family = {0, 1};
        familyProbeDesc.debugName = "Family streaming probe";
        rendering::VisibilityProbeHandle familyProbe;
        Check(feedback.CreateProbe(familyProbeDesc, familyProbe, &sceneFailure), "visibility feedback accepts current generational view-family policy");
        rendering::VisibilityProbeDesc missingViewProbeDesc = visibleProbeDesc;
        missingViewProbeDesc.viewPolicy.kind = rendering::VisibilityViewPolicyKind::SpecificView;
        missingViewProbeDesc.viewPolicy.view = {9, 1};
        missingViewProbeDesc.debugName = "Missing-view streaming probe";
        rendering::VisibilityProbeHandle missingViewProbe;
        Check(feedback.CreateProbe(missingViewProbeDesc, missingViewProbe, &sceneFailure), "visibility feedback accepts a specific generational view policy");

        rendering::VisibilityFeedbackView feedbackView;
        feedbackView.view = {0, 1};
        feedbackView.family = {0, 1};
        feedbackView.frustum.planeCount = 1;
        feedbackView.frustum.planes[0].normal[0] = 1.0f;
        feedbackView.frustum.planes[0].distance = -10.0f;
        feedbackView.queryMask = 1;
        feedbackView.streamingAuthority = true;
        rendering::VisibilityFeedbackEvaluationRequest feedbackRequest;
        feedbackRequest.views = {&feedbackView, 1};
        feedbackRequest.mutationEpoch = secondUpdate.mutationEpoch;
        feedbackRequest.frameSerial = 10;
        feedbackRequest.viewSetRevision = 1;
        feedbackRequest.targetProbesPerBatch = 1;
        rendering::VisibilityFeedbackEvaluationBatch feedbackBatches[4]{};
        rendering::VisibilityFeedbackEvaluationPlan feedbackPlan;
        Check(feedback.PrepareEvaluation(feedbackRequest, feedbackBatches, feedbackPlan, &sceneFailure) && feedbackPlan.batchCount == 4 &&
                  feedbackPlan.probeCount == 4,
              "visibility feedback plans dense disjoint probe batches against an explicit view set");
        rendering::VisibilityFeedback feedbackRead;
        Check(!feedback.ReadFeedback(visibleProbe, 10, feedbackRead, &sceneFailure) && sceneFailure.code == rendering::RenderSceneFailureCode::Busy,
              "unpublished feedback bank cannot be observed while evaluation jobs are open");
        rendering::VisibilityFeedbackBatchResult feedbackBatchResults[4]{};
        u32 completedFeedbackBatches = 0;
        for (u32 batchIndex = 0; batchIndex < feedbackPlan.batchCount; ++batchIndex)
            if (feedback.EvaluateBatch(feedbackPlan, feedbackBatches[batchIndex], feedbackBatchResults[batchIndex], &sceneFailure))
                ++completedFeedbackBatches;
        Check(completedFeedbackBatches == feedbackPlan.batchCount && feedback.CompleteEvaluation(feedbackPlan, &sceneFailure),
              "visibility feedback publishes only after every disjoint evaluation batch completes");
        rendering::VisibilityFeedback visibleFeedback;
        rendering::VisibilityFeedback hiddenFeedback;
        rendering::VisibilityFeedback familyFeedback;
        rendering::VisibilityFeedback missingViewFeedback;
        Check(
            feedback.ReadFeedback(visibleProbe, 10, visibleFeedback, &sceneFailure) && feedback.ReadFeedback(hiddenProbe, 10, hiddenFeedback, &sceneFailure) &&
                feedback.ReadFeedback(familyProbe, 10, familyFeedback, &sceneFailure) &&
                feedback.ReadFeedback(missingViewProbe, 10, missingViewFeedback, &sceneFailure) &&
                visibleFeedback.state == rendering::VisibilityFeedbackState::Visible &&
                hiddenFeedback.state == rendering::VisibilityFeedbackState::NotVisible && familyFeedback.state == rendering::VisibilityFeedbackState::Visible &&
                missingViewFeedback.state == rendering::VisibilityFeedbackState::Unknown && visibleFeedback.mutationEpoch == secondUpdate.mutationEpoch &&
                visibleFeedback.ageInFrames == 0,
            "versioned tri-state feedback applies authority, family, and specific-view policies");

        feedbackRequest.frameSerial = 11;
        rendering::VisibilityFeedbackEvaluationPlan cancelledFeedbackPlan;
        Check(feedback.PrepareEvaluation(feedbackRequest, feedbackBatches, cancelledFeedbackPlan, &sceneFailure) &&
                  feedback.EvaluateBatch(cancelledFeedbackPlan, feedbackBatches[0], feedbackBatchResults[0], &sceneFailure) &&
                  feedback.CancelEvaluation(cancelledFeedbackPlan, &sceneFailure) && feedback.ReadFeedback(visibleProbe, 11, visibleFeedback, &sceneFailure) &&
                  visibleFeedback.publishedFrame == 10,
              "cancelled feedback evaluation cannot expose a partially written bank");
        Check(feedback.DestroyProbe(missingViewProbe, &sceneFailure) && feedback.DestroyProbe(familyProbe, &sceneFailure) &&
                  feedback.DestroyProbe(hiddenProbe, &sceneFailure) && feedback.DestroyProbe(visibleProbe, &sceneFailure),
              "visibility feedback retires probes independently from RenderScene proxies");

        rendering::VisibilityQueryRequest candidateRequest;
        candidateRequest.scene = scene;
        candidateRequest.mutationEpoch = secondUpdate.mutationEpoch;
        candidateRequest.payloadFilter = rendering::VisibilityQueryPayloadFilter::Mesh;
        candidateRequest.useBounds = false;
        rendering::RenderSceneGpuCandidateBatch candidateBatches[8]{};
        rendering::RenderSceneGpuCandidatePlan candidatePlan;
        Check(scenes.PrepareGpuVisibilityCandidates(candidateRequest, 1, candidateBatches, candidatePlan, &sceneFailure) && candidatePlan.IsValid() &&
                  candidatePlan.batchCount == 2 && candidatePlan.requiredCandidateCapacity == 2,
              "GPU visibility candidate planning seals the exact epoch and balances work by raw proxy count");
        Check(!scenes.UpdateProxyLayerMask(proxy, 0x23ull, &sceneFailure) && sceneFailure.code == rendering::RenderSceneFailureCode::Busy,
              "direct scene mutation is rejected while candidate jobs own the read seal");

        rendering::GpuView candidateViews[1]{};
        rendering::GpuInstanceIndex candidateIndices[8]{};
        rendering::GpuVisibilityWorkRange candidateWork[8]{};
        rendering::GpuVisibilityResultRange candidateViewResults[1]{};
        rendering::GpuView candidateGpuView;
        candidateGpuView.viewIndex = 0;
        candidateGpuView.viewGeneration = 1;
        candidateGpuView.frustumPlaneCount = 1;
        rendering::GpuVisibilityPlanBuilder candidateVisibilityBuilder;
        rendering::GpuVisibilityBuildFailure candidateVisibilityFailure;
        Check(candidateVisibilityBuilder.Begin({candidateViews, candidateIndices, candidateWork, candidateViewResults}, 20, &candidateVisibilityFailure),
              "candidate test visibility storage begins");
        rendering::GpuVisibilityCandidateReservation candidateReservation;
        Check(candidateVisibilityBuilder.ReserveViewRanges(candidateGpuView, candidatePlan.requiredCandidateCapacity, candidatePlan.requiredWorkRangeCapacity,
                                                           8, candidateReservation, &candidateVisibilityFailure),
              "RenderScene candidate plan reserves its exact disjoint staging capacity");
        rendering::GpuVisibilityCandidateRange candidateRanges[8]{};
        rendering::RenderSceneGpuCandidateBatchResult candidateResults[8]{};
        rendering::RenderSceneFailure candidateFailures[8]{};
        u32 unresolvedCandidates = 0;
        u32 completedCandidateBatches = 0;
        for (u32 batchIndex = 0; batchIndex < candidatePlan.batchCount; ++batchIndex)
        {
            if (scenes.WriteGpuVisibilityCandidateBatch(candidatePlan, candidateBatches[batchIndex], candidateReservation, candidateRanges[batchIndex],
                                                        candidateResults[batchIndex], &candidateFailures[batchIndex]))
                ++completedCandidateBatches;
            unresolvedCandidates += candidateResults[batchIndex].unresolvedGpuIdentities;
        }
        Check(completedCandidateBatches == 1 && unresolvedCandidates == 1,
              "direct candidate batches reject only the mesh lacking an attached GPU Scene identity");
        candidateVisibilityBuilder.Cancel();
        Check(scenes.CompleteGpuVisibilityCandidates(candidatePlan, &sceneFailure), "candidate dependency completion releases the scene read seal");

        Check(!scenes.CollectVisibleProxies(visibilityRequest, visibleProxies, visibilityResult, &sceneFailure) &&
                  sceneFailure.code == rendering::RenderSceneFailureCode::Busy,
              "visibility rejects stale epochs instead of retaining copied scene versions");

        Check(scenes.DestroyProxy(proxy, &sceneFailure) && scenes.DestroyProxy(meshProxy, &sceneFailure), "proxy destruction releases live scene ownership");
        rendering::RenderSceneUpdateResult destroyUpdate;
        Check(executeSceneUpdate(destroyUpdate), "proxy destruction mutations drain through the explicit update boundary");
        Check(!scenes.DestroyScene(scene, &sceneFailure) && sceneFailure.code == rendering::RenderSceneFailureCode::Busy,
              "scene destruction waits for its independently attached visibility feedback service");
        Check(feedback.Shutdown(&sceneFailure), "visibility feedback detaches after its probes and jobs retire");
        Check(scenes.DestroyScene(scene, &sceneFailure), "runtime RenderScene destruction");

        const rendering::RenderSceneManagerStats sceneStats = scenes.GetStats();
        Check(sceneStats.createdScenes == sceneStats.destroyedScenes && sceneStats.activeScenes == 0 && sceneStats.framePipelineScenes == 0 &&
                  sceneStats.createdProxies == sceneStats.destroyedProxies && sceneStats.activeProxies == 0 &&
                  sceneStats.createdPayloads == sceneStats.destroyedPayloads && sceneStats.activeMeshPayloads == 0 && sceneStats.pendingProxyMutations == 0 &&
                  sceneStats.activeSpatialEntries == 0,
              "RenderSceneManager records the direct scene lifecycle");
        Check(scenes.Shutdown(&sceneFailure), "RenderSceneManager shutdown");
    }

    meshHandle.Reset();
    materialHandle.Reset();
    meshRequest.Reset();
    materialRequest.Reset();
    Check(resourceRegistry.Evict(meshReference.GetPath()) && resourceRegistry.Evict(materialReference.GetPath()) &&
              resourceRegistry.GetState(materialReference.GetPath()) == resources::State::Unloaded && resourceHarness.destructions.GetValue() == 2,
          "test rendering resources evict after all live scene ownership is released");
    Check(resourceRegistry.Shutdown(), "test resource registry shutdown");

    ExecutionState execution;
    rendering::RenderSceneManager commandScenes;
    rendering::RenderSceneFailure commandSceneFailure;
    Check(commandScenes.Initialize({}, &commandSceneFailure), "render command scene manager initialization");
    rendering::RenderCameraStorage commandCameras;
    rendering::RenderCameraFailure commandCameraFailure;
    Check(commandCameras.Initialize(commandScenes, {}, &commandCameraFailure), "render command camera storage initialization");
    rendering::RenderSceneDesc commandSceneDesc;
    commandSceneDesc.name = "Render command scene";
    commandSceneDesc.maximumProxies = 8;
    commandSceneDesc.maximumPendingProxyMutations = 8;
    commandSceneDesc.maximumViews = 2;
    rendering::RenderSceneHandle commandScene;
    Check(commandScenes.CreateScene(commandSceneDesc, commandScene, &commandSceneFailure), "render command participating scene creation");
    rendering::RenderCommandSystem commands;
    rendering::RenderCommandFailure commandFailure;
    rendering::RenderCommandSystemConfig commandConfig;
    commandConfig.executeFrameTick = ExecuteFrameTick;
    commandConfig.executeFrame = ExecuteFrame;
    commandConfig.userData = &execution;
    Check(commands.Initialize(commandScenes, commandCameras, commandConfig, &commandFailure), "render command system initialization");
    rendering::RenderCameraDesc commandCameraDesc;
    commandCameraDesc.scene = commandScene;
    commandCameraDesc.name = "Render command main camera";
    commandCameraDesc.state.phases = rendering::RenderPhaseSet(1);
    rendering::RenderCameraHandle commandCamera;
    Check(commands.RegisterCamera(commandCameraDesc, commandCamera, &commandCameraFailure), "render command camera registration");
    commandCameraDesc.name = "Render command secondary camera";
    rendering::RenderCameraHandle commandCameraB;
    Check(commands.RegisterCamera(commandCameraDesc, commandCameraB, &commandCameraFailure), "second render command camera registration");
    rendering::ViewportFailure failure;

    rendering::ViewportManager manager;
    Check(manager.Initialize(commands, &failure), "viewport manager initialization");

    rendering::RenderTextureOutputContract defaultTextureContract;
    Check(defaultTextureContract.IsSupported() && defaultTextureContract.restingState == vanguard::rhi::ResourceState::Common &&
              defaultTextureContract.restingQueue == vanguard::rhi::QueueType::Graphics &&
              defaultTextureContract.publicationReadiness == rendering::RenderOutputPublicationReadinessKind::SameQueueContinuation,
          "texture output contract defaults to an explicit supported Graphics resting publication; EndRender transitions from the producer render-target endpoint");
    rendering::RenderTextureOutputContract invalidTextureQueue = defaultTextureContract;
    invalidTextureQueue.restingQueue = vanguard::rhi::QueueType::Copy;
    rendering::RenderTextureOutputContract invalidTextureState = defaultTextureContract;
    invalidTextureState.restingState = vanguard::rhi::ResourceState::Unknown;
    rendering::RenderTextureOutputContract invalidTextureReadiness = defaultTextureContract;
    invalidTextureReadiness.publicationReadiness = static_cast<rendering::RenderOutputPublicationReadinessKind>(0xffu);
    rendering::RenderTextureOutputContract explicitFenceTextureContract = defaultTextureContract;
    explicitFenceTextureContract.publicationReadiness = rendering::RenderOutputPublicationReadinessKind::ExplicitFence;
    Check(!invalidTextureQueue.IsSupported() && !invalidTextureState.IsSupported() && !invalidTextureReadiness.IsSupported(),
          "texture output contract rejects unsupported queue, state, and publication-readiness values before submission");
    Check(explicitFenceTextureContract.IsSupported() && rendering::RenderViewportDesc{}.outputColorEncoding == rendering::RenderOutputColorEncoding::SceneLinear,
          "texture output contract admits explicit-fence publication while generic output encoding defaults to scene-linear");
    rendering::RenderViewportDesc displayEncodingDesc;
    displayEncodingDesc.name = "Display encoding test output";
    displayEncodingDesc.outputKind = rendering::RenderViewportOutputKind::Headless;
    displayEncodingDesc.outputColorEncoding = rendering::RenderOutputColorEncoding::DisplaySrgb;
    rendering::RenderViewportHandle displayEncodingOutput;
    Check(manager.CreateRenderViewport(displayEncodingDesc, displayEncodingOutput, &failure) &&
              manager.Resolve(displayEncodingOutput) != nullptr &&
              manager.Resolve(displayEncodingOutput)->GetOutputColorEncoding() == rendering::RenderOutputColorEncoding::DisplaySrgb,
          "render viewport preserves the neutral DisplaySrgb output-encoding contract");
    Check(manager.DestroyRenderViewport(displayEncodingOutput, &failure), "display-encoding proof viewport destruction");
    rendering::RenderViewportDesc invalidEncodingDesc = displayEncodingDesc;
    invalidEncodingDesc.name = "Invalid encoding test output";
    invalidEncodingDesc.outputColorEncoding = static_cast<rendering::RenderOutputColorEncoding>(0xffu);
    rendering::RenderViewportHandle invalidEncodingOutput;
    Check(!manager.CreateRenderViewport(invalidEncodingDesc, invalidEncodingOutput, &failure) &&
              failure.code == rendering::ViewportFailureCode::InvalidDescriptor && !invalidEncodingOutput.IsValid(),
          "render viewport rejects an unknown output encoding before any frame can begin");
    rendering::RenderOutputReceipt receipt;
    Check(!receipt.IsValid() && !receipt.IsReady(), "empty texture output receipt has no producer epoch");
    receipt.viewport = {0, 1};
    receipt.outputRevision = 1;
    receipt.producerSerial = 7;
    Check(receipt.IsValid() && !receipt.IsReady(), "pending texture output receipt is exact but not consumable");
    receipt.state = rendering::RenderOutputReceiptState::Ready;
    receipt.readyFence = {vanguard::rhi::QueueType::Graphics, 9};
    Check(receipt.IsReady(), "ready texture output receipt requires exact producer identity and real GPU fence evidence");

    const rendering::RenderSceneHandle duplicateFrameScenes[]{commandScene, commandScene};
    rendering::RenderCommandFrameTickResult duplicateFrameTickResult;
    Check(!commands.FrameTick(duplicateFrameScenes, 1, duplicateFrameTickResult, &commandFailure) &&
              commandFailure.code == rendering::RenderCommandFailureCode::DuplicateScene,
          "direct scene stamps reject duplicate frame participation without a hash table");
    rendering::RenderCommandFrameTickResult frameTickResult;
    Check(commands.FrameTick(commandScenes.GetFramePipelineScenes(), 1, frameTickResult, &commandFailure) && frameTickResult.submittedScenes == 1 &&
              frameTickResult.asynchronousScenes == 0,
          "dense participating scene span reaches renderer FrameTick without coordinator discovery");
    rendering::RenderCommandFrameTickResult unflushedFrameTickResult;
    Check(!commands.FrameTick(commandScenes.GetFramePipelineScenes(), 2, unflushedFrameTickResult, &commandFailure) &&
              commandFailure.code == rendering::RenderCommandFailureCode::InvalidState,
          "FrameTick requires the explicit previous-frame flush even when the previous CPU tail has already completed");

    rendering::RenderViewportDesc outputDesc;
    outputDesc.name = "Headless test output";
    outputDesc.outputKind = rendering::RenderViewportOutputKind::Headless;
    outputDesc.renderExtent = {1920, 1080};
    outputDesc.outputExtent = {1920, 1080};
    rendering::RenderViewportHandle output;
    Check(manager.CreateRenderViewport(outputDesc, output, &failure), "headless render viewport creation");

    rendering::EngineViewportDesc engineDesc;
    engineDesc.contextName = "Game";
    engineDesc.output = output;
    rendering::EngineViewportHandle gameViewport;
    Check(manager.CreateEngineViewport(engineDesc, gameViewport, &failure), "game engine viewport creation");

    rendering::EngineViewport* const game = manager.Resolve(gameViewport);
    rendering::RenderViewport* const renderOutput = manager.Resolve(output);
    Check(game != nullptr && game->IsValid(), "resolve generation-checked EngineViewport object");
    Check(renderOutput != nullptr && renderOutput->IsValid(), "resolve generation-checked RenderViewport object");
    Check(renderOutput != nullptr && renderOutput->GetOutputKind() == rendering::RenderViewportOutputKind::Headless &&
              renderOutput->GetRenderExtent() == rendering::ViewportExtent{1920, 1080} && renderOutput->GetOutputExtent() == rendering::ViewportExtent{1920, 1080} &&
              renderOutput->GetRenderWidth() == 1920 && renderOutput->GetRenderHeight() == 1080 && renderOutput->GetOutputWidth() == 1920 &&
              renderOutput->GetOutputHeight() == 1080 && renderOutput->IsVisible() && !renderOutput->IsOccluded() && !renderOutput->GetSwapChain().IsValid() &&
              !renderOutput->GetOutputTexture().IsValid(),
          "manager-owned RenderViewport exposes its applied live output state directly");
    u32 visitedRenderViewports = 0;
    manager.VisitRenderViewports([](const rendering::RenderViewport& viewport, void* const userData) noexcept
                                 {
                                     if (viewport.IsValid())
                                         ++*static_cast<u32*>(userData);
                                 },
                                 &visitedRenderViewports);
    u32 visitedEngineViewports = 0;
    manager.VisitEngineViewports([](const rendering::EngineViewport& viewport, void* const userData) noexcept
                                 {
                                     if (viewport.IsValid())
                                         ++*static_cast<u32*>(userData);
                                 },
                                 &visitedEngineViewports);
    Check(visitedRenderViewports == 1 && visitedEngineViewports == 1, "viewport enumeration visits live owned objects without copied state");
    execution.expectedViewport = renderOutput;

    rendering::RenderFrameInfo first = Begin(*game);
    Check(first.GetSerial() != 0 && first.GetViewport() == renderOutput && first.GetRenderExtent() == rendering::ViewportExtent{1920, 1080},
          "begin frame retains the manager-owned output object and immutable frame dimensions");
    Check(!first.ShouldPresent(), "headless output never requests presentation");
    Check(!renderOutput->RequestRenderExtent({1600, 900}, &failure) && failure.code == rendering::ViewportFailureCode::FrameAlreadyBuilding,
          "viewport mutation is rejected after frame construction has begun");
    rendering::RenderFrameInfo duplicate;
    Check(!game->BeginFrame({}, duplicate, &failure) && failure.code == rendering::ViewportFailureCode::FrameAlreadyBuilding,
          "second building frame is rejected explicitly");

    const rendering::RenderCameraHandle commandRoots[]{commandCamera};
    rendering::RenderFrameViewSetup commandViewSetup;
    commandViewSetup.scene = commandScene;
    commandViewSetup.rootCameras = commandRoots;
    const rendering::RenderCameraHandle commandTwoRoots[]{commandCamera, commandCameraB};
    const rendering::RenderCameraOutputRegion commandOutputRegions[]{
        {commandCamera, {0, 0, 960, 1080}},
        {commandCameraB, {960, 0, 960, 1080}}
    };
    rendering::RenderFrameViewSetup commandTwoViewSetup;
    commandTwoViewSetup.scene = commandScene;
    commandTwoViewSetup.rootCameras = commandTwoRoots;
    commandTwoViewSetup.outputRegions = commandOutputRegions;
    Check(game->ConfigureViews(first, commandTwoViewSetup, &failure) && first.HasViewSetup() && !first.GetViewFamily().IsValid(),
          "building frame retains multiple camera roots and explicit output regions without preparing renderer-owned views early");
    const rendering::RenderFrameViewSetup retainedTwoViewSetup = first.GetViewSetup();
    Check(retainedTwoViewSetup.rootCameras.Size() == 2 && retainedTwoViewSetup.outputRegions.Size() == 2 &&
              retainedTwoViewSetup.rootCameras[0] == commandCamera && retainedTwoViewSetup.rootCameras[1] == commandCameraB &&
              retainedTwoViewSetup.outputRegions[0].camera == commandCamera &&
              retainedTwoViewSetup.outputRegions[0].rect.x == 0 && retainedTwoViewSetup.outputRegions[0].rect.y == 0 &&
              retainedTwoViewSetup.outputRegions[0].rect.width == 960 && retainedTwoViewSetup.outputRegions[0].rect.height == 1080 &&
              retainedTwoViewSetup.outputRegions[1].camera == commandCameraB &&
              retainedTwoViewSetup.outputRegions[1].rect.x == 960 && retainedTwoViewSetup.outputRegions[1].rect.y == 0 &&
              retainedTwoViewSetup.outputRegions[1].rect.width == 960 && retainedTwoViewSetup.outputRegions[1].rect.height == 1080,
          "two-view split output retains exact camera/region identity in the submitted frame packet");
    Check(!game->ConfigureViews(first, commandViewSetup, &failure) && failure.code == rendering::ViewportFailureCode::InvalidState,
          "building frame view setup is configured exactly once");

    PayloadState firstPayload;
    firstPayload.expectedOrder = 0;
    Check(first.SetPayload({&firstPayload, RetainPayload, ReleasePayload}), "valid retained frame payload");
    rendering::RenderFrameSubmission firstSubmission;
    Check(game->SubmitFrame(first, firstSubmission, &failure) && firstSubmission.IsValid(), "first frame submission");
    Check(first.GetSerial() == 0, "submitted caller frame is invalidated");
    const u64 flushesBeforeViewportUpdate = commands.GetStats().explicitFlushes;
    Check(renderOutput->RequestRenderExtent({1920, 1080}, &failure) && commands.GetStats().explicitFlushes == flushesBeforeViewportUpdate,
          "unchanged viewport state does not join the active render tail");
    Check(renderOutput->RequestRenderExtent({1600, 900}, &failure) && commands.GetStats().explicitFlushes == flushesBeforeViewportUpdate + 1,
          "changed viewport state joins the prior render tail before mutation");
    Check(renderOutput->GetRenderExtent() == rendering::ViewportExtent{1600, 900} && renderOutput->GetRenderWidth() == 1600 &&
              renderOutput->GetRenderHeight() == 900,
          "applied viewport mutation is immediately observable through the owned viewport object");

    rendering::RenderViewportDesc previewOutputDesc = outputDesc;
    previewOutputDesc.name = "Preview headless test output";
    previewOutputDesc.renderExtent = {1600, 900};
    previewOutputDesc.outputExtent = {1600, 900};
    rendering::RenderViewportHandle previewOutput;
    Check(manager.CreateRenderViewport(previewOutputDesc, previewOutput, &failure), "second independent render output creation");
    rendering::RenderViewport* const previewRenderOutput = manager.Resolve(previewOutput);
    Check(previewRenderOutput != nullptr && previewRenderOutput != renderOutput,
          "second EngineViewport is backed by a distinct RenderViewport output");

    rendering::EngineViewportDesc previewDesc;
    previewDesc.contextName = "EditorPreview";
    previewDesc.output = previewOutput;
    previewDesc.presentByDefault = false;
    rendering::EngineViewportHandle previewViewport;
    Check(manager.CreateEngineViewport(previewDesc, previewViewport, &failure), "second engine viewport creation");

    execution.expectedViewport = previewRenderOutput;
    rendering::RenderFrameInfo second = Begin(manager, previewViewport);
    Check(second.GetRenderExtent() == rendering::ViewportExtent{1600, 900}, "the next frame captures the applied viewport extent");
    Check(manager.ConfigureViews(previewViewport, second, commandViewSetup, &failure), "second viewport configures the same scene camera independently");
    PayloadState secondPayload;
    secondPayload.expectedOrder = 1;
    secondPayload.fail = true;
    Check(second.SetPayload({&secondPayload, RetainPayload, ReleasePayload}), "second retained frame payload");
    rendering::RenderFrameSubmission secondSubmission;
    Check(!manager.SubmitFrame(gameViewport, second, secondSubmission, &failure) && failure.code == rendering::ViewportFailureCode::ForeignFrame,
          "foreign engine viewport cannot submit a frame");
    Check(manager.SubmitFrame(previewViewport, second, secondSubmission, &failure), "second frame submission");
    Check(!game->FlushFrame(&failure) && failure.code == rendering::ViewportFailureCode::SubmissionFailure,
          "render submission barrier reports asynchronous execution failure through EngineViewport");

    const rendering::RenderCommandSystemStats commandStats = commands.GetStats();
    Check(commandStats.frameTicks == 1 && commandStats.completedFrameTicks == 1 && commandStats.submittedFrames == 2 && commandStats.completedFrames == 2 &&
              commandStats.failedFrames == 1 && commandStats.lastCompletedFrameSerial == secondSubmission.serial &&
              commandStats.suppressedExecutionFailures == 0 && !commandStats.executionFailurePending,
          "render command chain records and consumes the first asynchronous render execution failure");
    Check(execution.tickContinuationExecutions.GetValue() == 1 && execution.frameTickSceneCount.GetValue() == 1 &&
              execution.frameTickMutationEpoch.GetValue() == 1 && execution.executions.GetValue() == 2 && execution.continuationExecutions.GetValue() == 2 &&
              execution.retainedViewSetups.GetValue() == 2 && execution.lastRootCameraCount.GetValue() == 1 &&
              execution.lastViewSetupSceneGeneration.GetValue() == commandScene.generation && execution.orderFailures.GetValue() == 0,
          "RenderPath submissions retain deferred view setup and deterministic dependency order");
    Check(firstPayload.retains.GetValue() == 1 && firstPayload.releases.GetValue() == 1 && secondPayload.retains.GetValue() == 1 &&
              secondPayload.releases.GetValue() == 1,
          "frame payload ownership spans asynchronous execution exactly once");
    Check(commandStats.frameTicks == 1 && commandStats.completedFrameTicks == 1 &&
              renderOutput->GetRenderedFrameCount() == 1 && renderOutput->GetEngineViewportReferenceCount() == 1 &&
              previewRenderOutput->GetRenderedFrameCount() == 1 && previewRenderOutput->GetEngineViewportReferenceCount() == 1,
          "one process FrameTick feeds two submitted frames on two independent outputs without duplicating global renderer update");

    execution.failFrameTick.SetValue(true);
    rendering::RenderCommandFrameTickResult failedFrameTickResult;
    Check(commands.FrameTick(commandScenes.GetFramePipelineScenes(), 2, failedFrameTickResult, &commandFailure) &&
              commands.FlushPreviousFrameProcessing(&commandFailure),
          "failed renderer FrameTick still reaches its explicit CPU completion boundary");
    rendering::RenderCameraFailure drainedOwnerCameraFailure;
    Check(commands.SetCameraEnabled(commandCamera, true, &drainedOwnerCameraFailure),
          "owner mutation can make camera state safe without stealing the engine-level failure acknowledgment");
    rendering::RenderCommandFrameTickResult unacknowledgedFailureTickResult;
    Check(!commands.FrameTick(commandScenes.GetFramePipelineScenes(), 3, unacknowledgedFailureTickResult, &commandFailure) &&
              commandFailure.code == rendering::RenderCommandFailureCode::InvalidState,
          "new render generation is rejected while a drained renderer-owner failure still awaits engine acknowledgment");
    bool drainedOwnerRequiresTerminalResolution = false;
    Check(commands.FlushPreviousFrameOwnerProcessing(drainedOwnerRequiresTerminalResolution, &commandFailure) && drainedOwnerRequiresTerminalResolution,
          "owner failure evidence survives both an earlier strong terminal drain and an intervening owner mutation until the owner boundary acknowledges it");
    Check(commands.ConsumeExecutionFailure(commandFailure) && commandFailure.code == rendering::RenderCommandFailureCode::ExecutionFailure &&
              commandFailure.executionStage == rendering::RenderCommandExecutionStage::FrameTick && commandFailure.executionSerial == 2,
          "asynchronous failure latch preserves renderer stage and serial identity");
    execution.failFrameTick.SetValue(false);

    rendering::RenderCommandFrameTickResult ownerBoundaryTickResult;
    Check(commands.FrameTick(commandScenes.GetFramePipelineScenes(), 3, ownerBoundaryTickResult, &commandFailure),
          "owner-boundary regression starts from a fresh render generation");
    execution.expectedViewport = renderOutput;
    PayloadState ownerBoundaryPayloadA;
    ownerBoundaryPayloadA.expectedOrder = 2;
    rendering::RenderFrameInfo ownerBoundaryFrameA = Begin(*game);
    Check(game->ConfigureViews(ownerBoundaryFrameA, commandViewSetup, &failure) &&
              ownerBoundaryFrameA.SetPayload({&ownerBoundaryPayloadA, RetainPayload, ReleasePayload}),
          "first owner-boundary frame retains its view-family inputs and payload");
    rendering::RenderFrameSubmission ownerBoundarySubmissionA;
    Check(game->SubmitFrame(ownerBoundaryFrameA, ownerBoundarySubmissionA, &failure),
          "first owner-boundary frame submission");

    PayloadState ownerBoundaryPayloadB;
    ownerBoundaryPayloadB.expectedOrder = 3;
    rendering::RenderFrameInfo ownerBoundaryFrameB = Begin(*game);
    Check(game->ConfigureViews(ownerBoundaryFrameB, commandViewSetup, &failure) &&
              ownerBoundaryFrameB.SetPayload({&ownerBoundaryPayloadB, RetainPayload, ReleasePayload}),
          "second owner-boundary frame retains its view-family inputs and payload");
    rendering::RenderFrameSubmission ownerBoundarySubmissionB;
    Check(game->SubmitFrame(ownerBoundaryFrameB, ownerBoundarySubmissionB, &failure),
          "multiple frames append to one renderer-owner completion aggregate without an intervening flush");

    bool requiresTerminalResolution = true;
    Check(commands.FlushPreviousFrameOwnerProcessing(requiresTerminalResolution, &commandFailure) && !requiresTerminalResolution &&
              ownerBoundaryPayloadA.releases.GetValue() == 1 && ownerBoundaryPayloadB.releases.GetValue() == 1,
          "owner-only flush joins every accepted frame, releases retained owner state, and preserves a clean core result");
    rendering::RenderCommandFrameTickResult terminalStillOwnedTickResult;
    Check(!commands.FrameTick(commandScenes.GetFramePipelineScenes(), 4, terminalStillOwnedTickResult, &commandFailure) &&
              commandFailure.code == rendering::RenderCommandFailureCode::InvalidState,
          "owner-only flush permits mutation but does not authorize a new render generation before terminal consumption");
    Check(commands.FlushPreviousFrameProcessing(&commandFailure) && !commands.ConsumeExecutionFailure(commandFailure),
          "terminal flush completes the owner-boundary regression without manufacturing an execution failure");
    rendering::RenderCommandFrameTickResult postTerminalTickResult;
    Check(commands.FrameTick(commandScenes.GetFramePipelineScenes(), 4, postTerminalTickResult, &commandFailure) &&
              commands.FlushPreviousFrameProcessing(&commandFailure) && !commands.ConsumeExecutionFailure(commandFailure),
          "new render generation is admitted after the terminal boundary is consumed");

    rendering::RenderFrameInfo abandoned = Begin(*game, false);
    Check(game->ConfigureViews(abandoned, commandViewSetup, &failure), "abandoned frame can retain an unprepared view request");
    Check(game->AbandonFrame(abandoned, &failure) && abandoned.GetSerial() == 0, "building frame can be explicitly abandoned");

    Check(game->GetBegunFrameCount() == 4 && game->GetSubmittedFrameCount() == 3,
          "engine viewport tracks begun, submitted, and abandoned frames");
    Check(!manager.DestroyRenderViewport(output, &failure) && failure.code == rendering::ViewportFailureCode::OutputStillReferenced,
          "render output cannot be destroyed while engine viewports reference it");

    rendering::RenderViewportDesc presentationDesc;
    presentationDesc.name = "Detached editor viewport";
    presentationDesc.outputKind = rendering::RenderViewportOutputKind::Presentation;
    presentationDesc.presentation = {3, 9};
    rendering::RenderViewportHandle presentation;
    Check(manager.CreateRenderViewport(presentationDesc, presentation, &failure), "presentation render viewport can exist before swapchain binding");
    rendering::EngineViewportDesc detachedDesc;
    detachedDesc.contextName = "DetachedEditor";
    detachedDesc.output = presentation;
    rendering::EngineViewportHandle detached;
    Check(manager.CreateEngineViewport(detachedDesc, detached, &failure), "detached engine viewport creation");
    rendering::RenderFrameInfo unavailable;
    Check(!manager.BeginFrame(detached, {}, unavailable, &failure) && failure.code == rendering::ViewportFailureCode::OutputUnavailable,
          "presentation viewport does not render before swapchain binding");

    rendering::RenderViewportPresentationUpdate presentationState;
    presentationState.attachment = presentationDesc.presentation;
    presentationState.pixelExtent = {1600, 900};
    presentationState.requiredPixelExtentRevision = 4;
    presentationState.requiredSurfaceRevision = 2;
    presentationState.visible = true;
    Check(renderOutput->GetHandle() != presentation, "resolved object remains tied to its original render viewport");
    rendering::RenderViewport* const detachedOutput = manager.Resolve(presentation);
    rendering::RenderViewportPresentationResult presentationResult;
    Check(detachedOutput != nullptr && detachedOutput->UpdatePresentation(presentationState, presentationResult, &failure),
          "presentation state reaches the detached render viewport");
    Check(detachedOutput->GetState() == rendering::RenderViewportState::AwaitingOutput &&
              detachedOutput->GetRequestedOutputExtent() == rendering::ViewportExtent{1600, 900} && detachedOutput->GetOutputExtent() == presentationDesc.outputExtent &&
              detachedOutput->GetRequiredPixelExtentRevision() == 4 && detachedOutput->GetAppliedPixelExtentRevision() == 0 &&
              detachedOutput->GetRequiredSurfaceRevision() == 2 && detachedOutput->GetAppliedSurfaceRevision() == 0 && !presentationResult.resizeApplied &&
              !presentationResult.surfaceReplaced,
          "presentation viewport separates requested and successfully applied swapchain state");
    Check(detachedOutput->RequestRenderExtent(presentationState.pixelExtent, &failure) &&
              detachedOutput->GetRenderExtent() == presentationState.pixelExtent,
          "presentation render extent follows the actual native pixel extent through the existing joined viewport mutation boundary");
    window::PresentationAcknowledgement acknowledgement;
    Check(!detachedOutput->GetPresentationAcknowledgement(acknowledgement), "unbound presentation viewport does not acknowledge unapplied work");
    const u64 reconciledOutputRevision = detachedOutput->GetOutputRevision();
    Check(detachedOutput->UpdatePresentation(presentationState, presentationResult, &failure) && detachedOutput->GetOutputRevision() == reconciledOutputRevision,
          "reconciling an unchanged presentation update is idempotent");
    presentationState.requiredPixelExtentRevision = 3;
    Check(!detachedOutput->UpdatePresentation(presentationState, presentationResult, &failure) && failure.code == rendering::ViewportFailureCode::InvalidDescriptor,
          "stale presentation revisions are rejected explicitly");

    Check(manager.DestroyEngineViewport(detached, &failure), "destroy detached engine viewport");
    Check(manager.DestroyRenderViewport(presentation, &failure), "destroy unbound presentation viewport");
    const rendering::RenderViewportHandle staleOutput = output;
    Check(manager.DestroyEngineViewport(previewViewport, &failure), "destroy preview engine viewport");
    Check(manager.DestroyRenderViewport(previewOutput, &failure), "destroy preview render output");
    Check(manager.DestroyEngineViewport(gameViewport, &failure), "destroy game engine viewport");
    Check(!game->IsValid(), "engine viewport object is invalidated on destruction");
    Check(manager.DestroyRenderViewport(output, &failure), "destroy headless render viewport");
    Check(!renderOutput->IsValid(), "render viewport object is invalidated after the drained command tail");

    rendering::RenderViewportHandle replacement;
    Check(manager.CreateRenderViewport(outputDesc, replacement, &failure) && replacement.index == staleOutput.index &&
              replacement.generation != staleOutput.generation,
          "reused render viewport slot advances its generation");
    Check(manager.Resolve(staleOutput) == nullptr, "stale render viewport handle is rejected");
    Check(manager.DestroyRenderViewport(replacement, &failure), "destroy replacement viewport");

    const rendering::ViewportManagerStats managerStats = manager.GetStats();
    Check(managerStats.renderViewports == 0 && managerStats.engineViewports == 0 && managerStats.buildingFrames == 0 && managerStats.begunFrames == 5 &&
              managerStats.submittedFrames == 4,
          "viewport manager reaches a fully drained state");

    Check(manager.Shutdown(&failure), "viewport manager shutdown");
    Check(commands.UnregisterCamera(commandCameraB, &commandCameraFailure), "second render command camera unregistration");
    Check(commands.UnregisterCamera(commandCamera, &commandCameraFailure), "render command camera unregistration");
    Check(commands.Shutdown(&commandFailure), "render command system shutdown");
    Check(commandScenes.GetStats().preparedFrames == 4 && commandScenes.DestroyScene(commandScene, &commandSceneFailure),
          "render command scene participates once per frame and retires after the CPU tail drains");
    Check(commandCameras.Shutdown(&commandCameraFailure), "render command camera storage shutdown");
    Check(commandScenes.Shutdown(&commandSceneFailure), "render command scene manager shutdown");
    Check(jobs::Shutdown(), "Jobs shutdown");
    const int failuresBeforeTextureCandidates = g_failures;
    RunTextureUploadCandidateTests(&Check);
    if (g_failures == failuresBeforeTextureCandidates)
        std::printf("Vanguard texture upload candidate tests passed.\n");

    if (g_failures == 0)
        std::printf("Vanguard rendering viewport tests passed.\n");
    return g_failures == 0 ? 0 : 1;
}
