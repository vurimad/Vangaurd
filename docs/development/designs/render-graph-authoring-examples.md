# Vanguard Render Graph Authoring Examples

Status: RG3E-aligned provisional authoring examples. The architecture and lifecycle shown here are authoritative; execution-only details remain subject to their owning RG4-RG9 translation slices.

## Purpose

This document defines the intended C++ authoring syntax for Vanguard's Render
Graph. Each example starts from a concrete REDengine pattern, identifies the
semantics that pattern provides, and then gives the Vanguard equivalent.

The examples supplement `render-graph-design.md` Section 52 and the current
`render-graph-execution-plan.md`. The superseded plan is preserved separately as
`render-graph-execution-plan-former-v1.md`. If an example conflicts with Section
52, Section 52 wins and the example must be corrected.

## Example Rules

- Keep graph declaration, node execution, and CPU preparation visibly separate.
- Keep cached graph topology separate from frame-local execution bindings; the cache entry's built `RenderNodeGraph` remains the executable graph.
- Put GPU recording in named `noexcept` execution functions, not retained
  capturing lambdas.
- Keep Present as a named terminal node in the current frame's graph. Output
  acquisition and lifecycle reconciliation remain main-thread-only, but normal
  presentation is not deferred to the next frame boundary.
- Use exact resource versions and typed bindings for correctness dependencies.
- Treat illustrative signatures as subject to repository naming conventions;
  their R9 ownership and lifecycle meaning is fixed.
- Keep C++ example signatures, conditions, calls, and initializers on long lines
  unless a structural split is necessary for comprehension.
- Preserve the relevant RED source location beside each translation.
- Add and approve one example before starting the next.

## Examples

### 1. Creating A Frame Graph

#### 1.1 Frame Entry And Early Validation

RED source: `renderRenderFrame.cpp`, `CRenderInterface::RenderFrame`, from the
function entry through the checks immediately before `// Toggle DLSS`.

This first chunk does not create the Render Graph. It establishes a valid frame
invocation before feature policy, camera preparation, or graph construction:

1. enter profiling and debug lifetime tracking;
2. obtain the frame information;
3. identify the scene, output, rendering mode, and frame purpose;
4. reject a device-unavailable or zero-sized frame.

The corresponding Vanguard responsibilities are:

| RED | Vanguard |
| --- | --- |
| `ScopedProfilerChannel` and `PC_SCOPE` | A Vanguard profiling scope once the profiling facade is defined. The example does not invent that API. |
| `ScopeAtomicRelease(&GDebugRenderFrame)` | No direct equivalent. `RenderCommandSystem` owns frame-tail serialization; a process-global debug atomic would duplicate lifecycle ownership. |
| `frame->GetFrameInfo()` | `context.GetFrame()` returns the immutable `RenderFrameInfo` captured for this invocation. |
| Raw `RenderViewport*` | A stable `RenderViewportHandle` is captured by `RenderFrameInfo`; worker code does not retain a mutable viewport pointer. |
| `scene`, `originalScene`, and `nonInteractiveScene` | Do not fetch these eagerly. Later feature chunks resolve only their required retained scene inputs from the frame view setup or prepared view family. |
| `scene->GetCameraStorage()` | `FrameRenderer` owns `RenderCameraStorage`; camera preparation produces a `PreparedRenderViewFamily`. |
| `ERenderingMode` and `EFramePurpose` | `RenderingMode` and `RenderFramePurpose`. |
| A nullable viewport for thumbnails | Output kind and `ShouldPresent()` describe presentation independently from whether rendering work exists. |
| `IsDeviceLost()` followed by a silent return | Test the RHI state and return an explicit `RenderFrameExecutionStatus`. |
| Zero viewport dimensions | Validate the immutable render extent captured in `RenderFrameInfo`. |

Cumulative R9 translation:

```cpp
#define VG_RETURN_FRAME_GRAPH_FAILURE(failure, fallback) do { if ((failure).HasFailure()) return RenderFrameExecutionStatus::Failure((failure).message != nullptr ? (failure).message : (fallback)); } while (false)

RenderFrameExecutionStatus FrameRenderer::RenderFrame(RenderFrameContext& context) noexcept
{
    // Add the Vanguard profiling scope once its profiling facade exists.

    RetainedRenderFrameRef retainedFrame = context.RetainFrame();
    if (!retainedFrame.IsValid())
        return RenderFrameExecutionStatus::Failure("render frame retention failed");

    const RenderFrameInfo& frame = retainedFrame.GetFrame();
    jobs::Builder& builder = context.GetBuilder();

    const RenderingMode renderingMode = frame.GetMode();
    const RenderFramePurpose framePurpose = frame.GetPurpose();
    const ViewportExtent renderExtent = frame.GetRenderExtent();
    const u64 frameSerial = frame.GetSerial();

    if (!renderExtent.IsValid())
    {
        return RenderFrameExecutionStatus::Failure("render frame has an invalid render extent");
    }

    if (!rhi::IsInitialized() || rhi::TestDeviceState() != rhi::DeviceState::Operational)
    {
        return RenderFrameExecutionStatus::Failure("render device is unavailable");
    }

    // Provisional feature-policy API. The policy is resolved and captured before this worker-side function is dispatched.
    const RenderFrameFeaturePolicy& features = frame.GetFeaturePolicy();
    const bool isWireframe = features.IsWireframe();

    // RED forces its scene-global dissolve clock to a settled state for captures and baking here. Vanguard has no dissolve subsystem today.
    // A future equivalent must be a frame-local scene/view preparation policy, not shared-scene mutation from this worker.

    PreparedRenderViewFamily& preparedFamily = retainedFrame.GetPreparedViewFamilyStorage();
    const PreparedRenderViewFamily* family = frame.GetViewFamily().IsValid() ? &frame.GetViewFamily() : nullptr;

    const RenderFrameExecutionStatus preparation = PrepareViewFamily(frame, preparedFamily, family);
    if (!preparation)
        return preparation;

    RenderViewGraphKey viewKeys[MaximumRenderViewsPerFamily]{};
    u32 viewKeyCount = 0;
    RenderGraphFailure graphFailure;

    if (family != nullptr)
    {
        for (const RenderView& view : family->GetViews())
        {
            if (!BuildRenderViewGraphKey(frame, features, view, viewKeys[viewKeyCount], &graphFailure))
                return RenderFrameExecutionStatus::Failure(graphFailure.message != nullptr ? graphFailure.message : "render view graph key construction failed");

            ++viewKeyCount;
        }
    }

    RenderGraphKey graphKey;
    if (!BuildRenderGraphKey(frame, features, containers::ArraySpan<const RenderViewGraphKey>(viewKeys, viewKeyCount), graphKey, &graphFailure))
        return RenderFrameExecutionStatus::Failure(graphFailure.message != nullptr ? graphFailure.message : "render graph key construction failed");

    const u32 viewGraphCount = viewKeyCount == 0 || framePurpose == RenderFramePurpose::Blank ? 1 : viewKeyCount;
    bool needsRebuild = false;
    RenderGraphCacheEntry* graphCacheEntry = nullptr;
    if (!m_graphCache.GetGraph(frameSerial, graphKey, viewGraphCount, needsRebuild, graphCacheEntry, &graphFailure))
        return RenderFrameExecutionStatus::Failure(graphFailure.message != nullptr ? graphFailure.message : "render graph cache lookup failed");
    if (graphCacheEntry == nullptr)
        return RenderFrameExecutionStatus::Failure("render graph cache returned no entry");

    RenderNodeGraph& graph = graphCacheEntry->graph;

    if (needsRebuild)
    {
        if (viewKeyCount == 0)
        {
            RenderViewGraphCacheEntry& storage = graphCacheEntry->viewGraphs.Front();
            BuildRenderGraphBlank(storage.graph, storage.nodes, isWireframe, false, graphFailure);
            VG_RETURN_FRAME_GRAPH_FAILURE(graphFailure, "blank render graph build failed");
            graph.AddGraph(storage.graph, /*mergeUnique*/ true, /*viewIndex*/ 0, /*mergeSequences*/ true, graphFailure);
            VG_RETURN_FRAME_GRAPH_FAILURE(graphFailure, "blank render graph composition failed");
        }
        else if (framePurpose == RenderFramePurpose::Blank)
        {
            RenderViewGraphCacheEntry& storage = graphCacheEntry->viewGraphs.Front();
            BuildRenderGraphBlank(storage.graph, storage.nodes, isWireframe, true, graphFailure);
            VG_RETURN_FRAME_GRAPH_FAILURE(graphFailure, "blank camera render graph build failed");
            graph.AddGraph(storage.graph, /*mergeUnique*/ true, /*viewIndex*/ 0, /*mergeSequences*/ true, graphFailure);
            VG_RETURN_FRAME_GRAPH_FAILURE(graphFailure, "blank camera render graph composition failed");
        }
        else
        {
            if (frame.HasNonViewDebugDrawer())
                return RenderFrameExecutionStatus::Failure("non-view debug drawing cannot be used when prepared views exist");

            const containers::ArraySpan<const RenderView> views = family->GetViews();

            for (u32 viewIndex = 0; viewIndex < views.Size(); ++viewIndex)
            {
                const RenderView& view = views[viewIndex];

                if (view.IsOffscreen() && renderingMode == RenderingMode::HitProxies)
                    continue;

                const RenderViewGraphKey& viewKey = viewKeys[viewIndex];
                RenderViewGraphCacheEntry* storage = graphCacheEntry->FindViewGraph(viewKey, graphFailure);
                VG_RETURN_FRAME_GRAPH_FAILURE(graphFailure, "render view graph storage was not found");
                if (storage == nullptr)
                    return RenderFrameExecutionStatus::Failure("render view graph storage was not found");

                if (storage->NeedsRebuild(viewKey))
                {
                    switch (renderingMode)
                    {
                    case RenderingMode::HitProxies:
                        BuildRenderGraphHitProxies(storage->graph, storage->nodes, features.IsMultilayerSelection(), false, graphFailure);
                        break;

                    case RenderingMode::TodvisBake:
                        BuildRenderGraphTodvis(storage->graph, storage->nodes, false, graphFailure);
                        break;

                    case RenderingMode::Shaded:
                    case RenderingMode::ShadedNoAmbient:
                        if (view.HasRenderScene())
                        {
                            if (features.IsTodvisDebug())
                                BuildRenderGraphTodvis(storage->graph, storage->nodes, true, graphFailure);
                            else if (features.IsDebugVisualization())
                                BuildRenderGraphDebugVisualization(storage->graph, storage->nodes, view.GetFeatures(), features.GetDisplayMode(), graphFailure);
                            else if (features.IsHitProxiesDebug())
                                BuildRenderGraphHitProxies(storage->graph, storage->nodes, false, true, graphFailure);
                            else
                                BuildRenderGraphCamera(storage->graph, storage->nodes, view.GetFeatures(), features.GetDisplayMode(), graphFailure);
                        }
                        else
                        {
                            BuildRenderGraphNoScene(storage->graph, storage->nodes, isWireframe, graphFailure);
                        }
                        break;

                    case RenderingMode::SafeMode:
                        if (view.HasRenderScene())
                            BuildRenderGraphSafeMode(storage->graph, storage->nodes, view.GetFeatures(), graphFailure);
                        else
                            BuildRenderGraphNoScene(storage->graph, storage->nodes, isWireframe, graphFailure);
                        break;

                    case RenderingMode::GBufferOnly:
                        BuildRenderGraphGBufferOnly(storage->graph, storage->nodes, graphFailure);
                        break;

                    default:
                        return RenderFrameExecutionStatus::Failure("invalid rendering mode");
                    }

                    VG_RETURN_FRAME_GRAPH_FAILURE(graphFailure, "render view graph build failed");
                }

                graph.AddGraph(storage->graph, /*mergeUnique*/ true, viewIndex, /*mergeSequences*/ true, graphFailure);
                VG_RETURN_FRAME_GRAPH_FAILURE(graphFailure, "render view graph composition failed");
            }
        }

        graph.BuildRenderFlowGroups(graphFailure);
        VG_RETURN_FRAME_GRAPH_FAILURE(graphFailure, "render flow group construction failed");
        graphCacheEntry->PostBuildClear(graphFailure);
        VG_RETURN_FRAME_GRAPH_FAILURE(graphFailure, "render graph definition publication failed");
    }

    RenderNodeImplContext rctx;
    {
        RenderNodeImplContext::InitData nodeImplContextInitData(context.GetDispatcherThreadIndex());
        nodeImplContextInitData.cameraStorage = &m_cameras;
        nodeImplContextInitData.frame = &frame;
        nodeImplContextInitData.viewFamily = family;
        rctx.Init(nodeImplContextInitData);
    }

    context.GetFrameCommandLists().PrepareForFrame(graph.GetNumNodes() + static_cast<u32>(ReservedFrameCommandList::Count));

    if (rctx.HasAnyCameraStorage())
    {
        rhi::Failure rhiFailure;
        const rhi::CommandListRef commandList = rhi::CreateCommandList(rhi::CommandListType::Default, HashString64("StorageData"), &rhiFailure);
        if (!commandList.IsValid())
            return RenderFrameExecutionStatus::Failure(rhiFailure.message != nullptr ? rhiFailure.message : "StorageData command-list creation failed");

        rhi::SetResourceDebugName(commandList, "StorageData");
        context.GetFrameCommandLists().SetCommandList(static_cast<u32>(ReservedFrameCommandList::StorageData), commandList);

        if (!rhi::BindCommandList(commandList, &rhiFailure))
            return RenderFrameExecutionStatus::Failure(rhiFailure.message != nullptr ? rhiFailure.message : "StorageData command-list binding failed");

        RenderCameraFailure cameraFailure;
        // Performs RED's two body-level passes internally: prepare every scene custom-data object, then prepare every custom-data object for every ordered camera/view.
        const bool customDataPrepared = rctx.GetCameraStorage().PrepareCustomData(rctx, &cameraFailure);
        rhi::UnbindCommandList();
        if (!customDataPrepared)
            return RenderFrameExecutionStatus::Failure(cameraFailure.message != nullptr ? cameraFailure.message : "render custom-data preparation failed");

        if (family != nullptr)
        {
            if (!m_cameras.CheckCustomDataReadiness(*family, &cameraFailure))
                return RenderFrameExecutionStatus::Failure(cameraFailure.message != nullptr ? cameraFailure.message : "render frame custom data is not ready");

            FrameCustomData frameCustomData(*family);
            if (!frameCustomData.IsValid())
                return RenderFrameExecutionStatus::Failure("render frame custom-data access could not retain its prepared view family");
            retainedFrame.SetCustomData(std::move(frameCustomData));
        }
    }

    const bool isBlankFrame = viewKeyCount == 0 || framePurpose == RenderFramePurpose::Blank;
    const bool processEviction = !frame.IsGameMode() || (frame.HasOnscreenOutput() && !isBlankFrame);

    FrameResourcePolicy resourcePolicy;
    resourcePolicy.enablePlacedResources = features.EnablePlacedResources();
    resourcePolicy.processEviction = processEviction;

    RenderFlowResourceFailure resourceFailure;
    if (!m_resourceAllocator.BeginFrame(frameSerial, resourcePolicy, &resourceFailure))
        return RenderFrameExecutionStatus::Failure(resourceFailure.message != nullptr ? resourceFailure.message : "render-flow allocator frame startup failed");

    RenderNodeResourceBindings& resourceBindings = retainedFrame.GetRenderNodeResourceBindings();
    RenderNodeResourcePreparationFailures& resourcePreparationFailures = retainedFrame.GetRenderNodeResourcePreparationFailures();
    graph.PrepareResourcesParallel(rctx, m_resourceAllocator, resourceBindings, resourcePreparationFailures, builder);

    jobs::Builder nodesKickoffBuilder({jobs::Priority::RenderPath, jobs::Affinity::AnyWorker});
    if (!nodesKickoffBuilder.IsValid())
    {
        m_resourceAllocator.CancelBeforePublication();
        return RenderFrameExecutionStatus::Failure("render-node kickoff builder creation failed");
    }

    jobs::Counter nodesKickoffCounter = nodesKickoffBuilder.ExtractCounter();
    if (!nodesKickoffCounter.IsValid())
    {
        m_resourceAllocator.CancelBeforePublication();
        return RenderFrameExecutionStatus::Failure("render-node kickoff counter creation failed");
    }

    jobs::CompletionDeferral nodesKickoffDeferral = nodesKickoffCounter.CreateDeferral("RenderNodesKickoff", &frame);
    if (!nodesKickoffDeferral.IsValid())
    {
        m_resourceAllocator.CancelBeforePublication();
        return RenderFrameExecutionStatus::Failure("render-node kickoff deferral creation failed");
    }

    RenderNodeJobsDebugCounter debugCounter;
    debugCounter.Add(1);

    jobs::Builder renderNodeJobsBuilder({jobs::Priority::RenderPath, jobs::Affinity::AnyWorker});
    if (!renderNodeJobsBuilder.IsValid())
    {
        m_resourceAllocator.CancelBeforePublication();
        return RenderFrameExecutionStatus::Failure("render-node job builder creation failed");
    }

    static jobs::JobName runRenderNodeJobsName{"RunRenderNodeJobs"};
    jobs::Task runRenderNodeJobs = jobs::Task::Create([retainedFrame, graph = &graph, debugCounter, nodesKickoffCounter = std::move(nodesKickoffCounter)](const jobs::JobContext& jobContext) mutable noexcept
    {
        RunRenderNodeJobs(retainedFrame, *graph, debugCounter, jobContext, nodesKickoffCounter);
    });

    static jobs::JobName resolveResourcesName{"FlowAllocator_Resolve"};
    jobs::Task resolveResourcesTask = jobs::Task::Create([this, retainedFrame](const jobs::JobContext& jobContext) noexcept
    {
        RenderNodeResourcePreparationFailures& preparationFailures = retainedFrame.GetRenderNodeResourcePreparationFailures();
        if (preparationFailures.HasFailure())
        {
            retainedFrame.RecordFailure(preparationFailures.GetFailure());
            return;
        }

        jobs::Builder creationJobs(jobContext);
        RenderFlowResourceFailure resolveFailure;
        if (!m_resourceAllocator.SealPlanning(PlanningJoinToken::CompletedSynchronously(), &resolveFailure))
        {
            retainedFrame.RecordFailure(resolveFailure);
            return;
        }
        if (!m_resourceAllocator.Resolve(&creationJobs, &resolveFailure))
            retainedFrame.RecordFailure(resolveFailure);
    });

    static jobs::JobName finishResourceResolutionName{"FlowAllocator_Resolve_Finish"};
    jobs::Task finishResourceResolutionTask = jobs::Task::Create([this, retainedFrame, nodesKickoffDeferral = std::move(nodesKickoffDeferral)](const jobs::JobContext&) mutable noexcept
    {
        if (!retainedFrame.HasFailure())
        {
            RenderFlowResourceFailure executionFailure;
            if (!m_resourceAllocator.BeginExecution(&executionFailure))
                retainedFrame.RecordFailure(executionFailure);
        }
        nodesKickoffDeferral.Finish();
    });

    static jobs::JobName frameTerminalName{"RenderFrame/Terminal"};
    jobs::Task frameTerminalTask = jobs::Task::Create([this, retainedFrame, debugCounter](const jobs::JobContext&) noexcept
    {
        VG_ASSERT_MSG(debugCounter.IsZero(), "render-node jobs did not finish before their terminal join");
        TerminalizeRenderFrame(retainedFrame, m_resourceAllocator);
    });

    if (!runRenderNodeJobs || !resolveResourcesTask || !finishResourceResolutionTask || !frameTerminalTask)
    {
        m_resourceAllocator.CancelBeforePublication();
        return RenderFrameExecutionStatus::Failure("render-frame job allocation failed");
    }

    VG_ASSERT_MSG(RenderNodeJob::GetJobsRenderFrame() == nullptr, "a render frame is still installed for node jobs");
    retainedFrame.InstallJobsRenderFrame();

    if (!renderNodeJobsBuilder.Dispatch(runRenderNodeJobsName, std::move(runRenderNodeJobs)))
    {
        retainedFrame.ClearJobsRenderFrame();
        m_resourceAllocator.CancelBeforePublication();
        return RenderFrameExecutionStatus::Failure("render-node setup job dispatch failed");
    }

    if (!builder.Dispatch(resolveResourcesName, std::move(resolveResourcesTask)))
        retainedFrame.RecordFailure("failed to dispatch render-flow allocator resolution");

    if (!builder.Dispatch(finishResourceResolutionName, std::move(finishResourceResolutionTask)))
        retainedFrame.RecordFailure("failed to dispatch render-flow allocator completion");

    jobs::Counter renderNodeJobsCounter = renderNodeJobsBuilder.ExtractCounter();
    if (!renderNodeJobsCounter.IsValid())
    {
        retainedFrame.ArmFailClosedTerminal(m_resourceAllocator, "render-node completion counter extraction failed");
        return RenderFrameExecutionStatus::Failure("render-node completion counter extraction failed");
    }

    builder.AddDependency(renderNodeJobsCounter);

    if (!builder.Dispatch(frameTerminalName, std::move(frameTerminalTask)))
    {
        retainedFrame.ArmFailClosedTerminal(m_resourceAllocator, "render-frame terminal job dispatch failed");
        return RenderFrameExecutionStatus::Failure("render-frame terminal job dispatch failed");
    }

    return RenderFrameExecutionStatus::Success();
}
```

The deliberate architectural difference is that RED gathers several mutable
subsystem pointers at function entry. Vanguard keeps worker-side frame entry
based on the immutable `RenderFrameInfo` snapshot and resolves retained inputs
only when a later chunk needs them.

The exact treatment of `rhi::DeviceState::Suspended` remains open. It may map to
`RenderFrameExecutionStatus::Skipped` rather than `Failure`; that policy belongs
to the later device-recovery translation and is not decided by this example.

#### 1.2 Frame Feature Policy

RED source: `renderRenderFrame.cpp`, the `// Toggle DLSS` block through the
`#endif // USE_DLSS` immediately before `// Scene prewarming`.

This block converts global configuration into active renderer feature state. It
does not execute DLSS or add the DLSS Render Graph node:

```cpp
#ifdef USE_DLSS
if (m_ngx && Config::cvRayTracingOverrideEnable.Get())
{
    m_ngx->EnableDLSS(Config::cvDLSSEnable.Get());
}
#endif
```

The RED conditions have four distinct meanings:

| RED | Meaning |
| --- | --- |
| `USE_DLSS` | The provider was compiled into this build. |
| `m_ngx` | The provider is installed and available. |
| `cvRayTracingOverrideEnable` | The surrounding rendering path permits the feature. |
| `cvDLSSEnable` | The current user configuration requests the feature. |
| `EnableDLSS` | Mutate the provider's global enabled state. |

RED's adjacent TODO already questions both the provider-specific interface and
the placement in `RenderFrame`. Vanguard preserves the required policy decision
without copying the global worker-thread mutation.

Before dispatching the worker-side frame, Vanguard must resolve:

```text
requested renderer settings
  + compiled and installed provider capabilities
  + viewport and output properties
  + frame mode and purpose
  = immutable frame feature policy
```

`FrameRenderer::RenderFrame` consumes that captured policy directly in the
cumulative function example above. `RenderFrameFeaturePolicy`,
`GetFeaturePolicy`, and `RenderUpscalerKind` are illustrative names, not an
implemented public interface. Their required semantic contract is the
immutable, frame-local snapshot.

The later graph-construction chunk uses the selected upscaler kind as structural
input when selecting or building the cached graph definition. If upscaling is
enabled, that definition contains a generic upscaler node; the provider invocation
belongs in the node's named `noexcept` execution function.

Persistent provider operations such as creation, destruction, resolution
changes, and quality reconfiguration belong to serialized viewport/output
reconciliation before frame dispatch. They must not race worker-side graph
construction or execution.

The resulting responsibility mapping is:

| RED | Vanguard |
| --- | --- |
| `USE_DLSS` | Provider compiled and registered. |
| `m_ngx` | Captured provider capability and availability. |
| Mutable global console variables | Renderer settings captured before dispatch. |
| `EnableDLSS` in `RenderFrame` | Immutable per-frame feature-policy resolution. |
| `CRenderNode_ApplyDLSS` later in RED | A generic upscaler graph node selected later. |

This chunk therefore becomes feature-policy capture and consumption. It is not
a graph node, and it performs no provider mutation on a rendering worker.

#### 1.3 Settled Temporal Scene State

RED source: `renderRenderFrame.cpp`, the `// Scene prewarming` block immediately
before `// Camera setup`.

Despite its label, this block does not prewarm shaders, resources, or GPU work.
For screenshots, environment probes, GI baking, and particular time-of-day
visualization modes, it calls `scene->FinishDissolveSynchronizer()`.

RED's `CRenderDissolveSynchronizer::Finish()` snaps the dissolve clock to its
final phase and clears its fractional carry. The resulting render does not
capture a transient fade or LOD crossfade. This is CPU scene-state preparation,
not a wait for jobs or GPU completion.

Vanguard currently has no dissolve synchronizer, so the cumulative function
contains no placeholder call or unused policy object. If an equivalent temporal
scene-effect system is introduced, the required behavior is a frame-local
`PreserveTransitions` or `SettleTransitions` input consumed while producing the
immutable scene/view snapshot. A capture viewport must not mutate shared scene
state from a rendering worker and thereby change another viewport.

This work precedes camera preparation and graph declaration because
settled temporal state may change visibility or selected LOD. It is not a Render
Graph node.

#### 1.4 Camera And View-Family Preparation

RED source: `renderRenderFrame.cpp`, the `// Camera setup` block through the
`numCameras` calculation immediately before `// Graph caching`.

RED turns its persistent camera storage into the complete frame camera array.
The preparation context carries frame information, temporal-jitter permission,
temporary-dependency flushing policy, and the render viewport rectangle. The
result includes root cameras and dependent cameras, and `numCameras` becomes a
structural input to graph-cache selection.

Vanguard expresses the same operation through a retained, immutable
`PreparedRenderViewFamily`. The frame may already own a prepared family; if it
instead carries deferred `RenderFrameViewSetup`, `FrameRenderer` prepares the
family through its owned `RenderCameraStorage`:

```cpp
const RenderFrameViewSetup setup = frame.GetViewSetup();

const RenderViewFamilyPrepareRequest request{setup.scene, setup.rootCameras, {frame.GetRenderExtent().width, frame.GetRenderExtent().height}, frame.GetSerial(), setup.jitterIndex, setup.enableTemporalJitter, setup.forceCameraCut};

RenderCameraFailure failure;
if (!m_cameras.PrepareViewFamily(request, preparedFamily, &failure))
{
    return RenderFrameExecutionStatus::Failure(failure.message != nullptr ? failure.message : "render frame view-family preparation failed");
}
```

The responsibility mapping is:

| RED | Vanguard |
| --- | --- |
| Scene-owned `cameraStorage` pointer | `FrameRenderer` owns `RenderCameraStorage`. |
| `info.m_cameras` | Stable root handles in `RenderFrameViewSetup::rootCameras`. |
| `AllocateCameraData` | `RenderCameraStorage::PrepareViewFamily`. |
| Mutable frame camera array | Retained immutable `PreparedRenderViewFamily`. |
| Viewport rectangle | Immutable `RenderFrameInfo::GetRenderExtent()`. |
| `m_allowCameraJitter` | Captured `enableTemporalJitter` and `jitterIndex`. |
| `m_flushTemporaries` | No equivalent: persistent dependencies are compiled into a frame-local retained family rather than flushed from shared temporary storage. |
| `GetNumCameras()` | `family->GetViews().Size()`. |
| `RM_OverlayOnly` exclusion | A future explicit view/feature policy if needed; Vanguard has no corresponding rendering mode today. |

The prepared family retains its camera and dependency-ordered view state across
worker jobs. A secondary selection or diagnostic frame therefore cannot flush
dependencies underneath a normal frame already in flight.

#### 1.5 Whole-Frame And Per-View Graph Keys

RED source: `renderRenderFrame.cpp`, from the start of `// Graph caching`
through construction of `allCamerasHash`, stopping before
`hack_asyncComputeFlagsChanged`. The related per-camera key construction occurs
inside the subsequent camera loop through `AppendRenderGraphCameraHash` and
`FindCameraSetup`.

RED uses `CRenderGraphCache::SCameraHash` at two different structural levels.
The complete-frame key is accumulated into `allCamerasHash`:

```cpp
CRenderGraphCache::SCameraHash allCamerasHash;
allCamerasHash.Append(numCameras);
allCamerasHash.Append(displayMode);
allCamerasHash.Append(Config::GGlobalRenderingSettings.LocalShadowsProcessedPerFrame);

for (Uint32 cameraIndex = 0; cameraIndex < numCameras; ++cameraIndex)
    AppendRenderGraphCameraHash(allCamerasHash, frame.Get(), cameraIndex);
```

During a rebuild, RED constructs another `SCameraHash` for each camera and uses
it to select reusable `SCameraSetupData`:

```cpp
CRenderGraphCache::SCameraHash cameraSetupHash;
AppendRenderGraphCameraHash(cameraSetupHash, frame.Get(), cameraIndex);
CRenderGraphCache::SCameraSetupData* storage = renderGraphCacheEntry.FindCameraSetup(cameraSetupHash);
```

The first value identifies the complete ordered multi-camera graph. The second
identifies the reusable graph structure for one camera configuration. RED uses
the same hash type for both meanings and relies on variable names to distinguish
them.

Vanguard preserves both cache levels with distinct types:

| RED | Vanguard |
| --- | --- |
| `SCameraHash allCamerasHash` | `RenderGraphKey graphKey` |
| `SCameraHash cameraSetupHash` | `RenderViewGraphKey viewKey` |
| `CacheEntry::m_cameraHash` | Key stored by the complete `RenderGraphCacheEntry` |
| `SCameraSetupData::m_cameraSetupHash` | Key stored by a reusable `RenderViewGraphCacheEntry` |

`RenderViewGraphKey` contains rendering mode, display/visualization mode,
full-render versus no-scene structure, and the complete camera feature set.
`RenderGraphKey` contains the ordered view keys plus frame-level structural
facts such as view count, frame purpose, output structure, current feature bits,
and the renderer revision epoch. Queue or backend policy belongs in the key only
after graph construction actually branches on it.

This preserves RED's wireframe, time-of-day visualization, overdraw, hit-proxy,
safe-mode, G-buffer-only, no-scene, blank-frame, multi-camera, and shared-camera-
configuration variants. Vanguard's current enums do not yet expose every RED
mode; the key and feature-policy contracts must remain extensible rather than
silently collapsing those variants.

Unlike RED's `SCameraHash`, both Vanguard keys retain their canonical fields and
perform full equality after hash lookup. Their cached hashes are accelerators,
not correctness evidence. Separate key types also prevent passing a per-view key
to the whole-frame cache API.

The cached structures retain RED's composition behavior:

```text
reusable per-view RenderNodeGraph values
  + explicit shared frame-level nodes
  -> one built composed RenderNodeGraph
```

Per-view graphs are topology fragments, not independently resolved allocator schedules. Vanguard composes them with ordered view slots, merges explicit shared work, and builds render-flow groups on the complete graph. A frame later binds current view IDs and native resources through the command system's retained frame and one per-occurrence `RenderNodeImplContext`.

`RenderNodeGraph`, `RenderNodeImplContext`, `RenderGraphCacheEntry`, `RenderViewGraphCacheEntry`, `RenderGraphKey`, `RenderViewGraphKey`, and the key-builder functions are the implementation vocabulary.

#### 1.6 Queue Policy And Bounded Graph-Cache Lookup

RED source: `renderRenderFrame.cpp`, from
`hack_asyncComputeFlagsChanged` through the call to
`m_graphCache->GetGraph(...)`, stopping before `if (needsRebuild)`. Cache behavior
is defined by `renderGraphCache.h` and `renderGraphCache.cpp`.

RED compares the current async-compute settings with manually retained previous
values because those topology-affecting values are absent from
`allCamerasHash`. A change forces cache replacement:

```cpp
const Bool hack_asyncComputeFlagsChanged = false || Config::cvAsyncComputeEnable.Get() != Config::previousAsyncComputeEnable || Config::cvAsyncSSAO.Get() != Config::previousAsyncSSAO || Config::cvAsyncHairClears.Get() != Config::previousAsyncHairClears || Config::cvAsyncLutGeneration.Get() != Config::previousAsyncLutGeneration || Config::cvAsyncDynamicTextureGeneration.Get() != Config::previousAsyncDynamicTextureGeneration || Config::cvAsyncFlattenNormals.Get() != Config::previousAsyncFlattenNormals || Config::cvAsyncBuildDepthChain.Get() != Config::previousAsyncBuildDepthChain;
```

The ray-tracing build flag is included under RED's `USE_RAY_TRACING` build
condition. These settings are evidence for what must enter Vanguard's key when
equivalent builders arrive. The current builders do not branch on an async-compute
or ray-tracing policy, so the production key does not carry placeholder fields for
those future modes. Adding the branch and its complete immutable key fact is one
change; global `previousAsync...` state is unnecessary.

RED's `GetGraph` is not a hash-map lookup. `CRenderGraphCache` owns a fixed array
of four entries and linearly examines every entry. During the same scan it tests
for a hit and remembers the entry with the oldest `m_lastUsedFrame`. Therefore
its organization is:

```text
four-entry fixed array
  + linear fully-associative lookup
  + least-recently-used replacement
```

Four entries make the bounded linear scan inexpensive and deterministic.
Vanguard keeps that fixed capacity rather than introducing configuration or a
hash map. Each entry retains a complete `RenderGraphKey`, the final `RenderNodeGraph`,
its temporary per-view graphs, its last-used frame serial, and a validity flag.
Hash comparison may accelerate the scan, but a hit still requires complete key
equality.

The cache operation stays close to RED:

```cpp
bool needsRebuild = false;
RenderGraphCache::CacheEntry* const graphCacheEntry = m_graphCache.GetGraph(frameSerial, graphKey, viewGraphCount, needsRebuild);
if (graphCacheEntry == nullptr)
    return RenderFrameExecutionStatus::Failure("render graph cache lookup failed");
```

`GetGraph` linearly searches for a valid matching entry while remembering the
least-recently-used entry. A hit updates its frame serial and returns
`needsRebuild == false`. A miss resets the selected entry, allocates the required
per-view graph slots, stores the new key and frame serial, and returns
`needsRebuild == true`. Allocation or reset failure is reported explicitly.

The rebuilt entry stays invalid until the complete graph and its render-flow
groups have been constructed successfully. A failed rebuild therefore loses that
one selected cache value but cannot expose a partially built graph; a later frame
rebuilds the invalid entry. The existing serialized RenderPath CPU tail prevents
the entry from being reset while jobs from an earlier frame still use it.

RED reserves one `SCameraSetupData` when there are no cameras or the frame is
blank. Vanguard preserves the observable blank and no-view graph variants but
does not misrepresent their frame-level fragment as a camera. Normal frames
continue to compose reusable per-view graphs in prepared-family order.

`RenderGraphCacheEntry`, `RenderViewGraphCacheEntry`, and `GetGraph` are example
names for the RED-equivalent responsibilities. Their exact signatures are
finalized in RG1.

#### 1.7 Cache Entry And Blank Graph Rebuild

This chunk now follows RED directly. `GetGraph` receives the accepted render-frame serial used for cache recency, the complete graph key, and the number of temporary view graphs required by this cache entry. It returns the selected bounded-cache entry and reports whether that entry must be rebuilt.

`RenderGraphCacheEntry::graph` corresponds to RED's final `CacheEntry::m_graph`. Each `RenderViewGraphCacheEntry` contains a temporary `graph`, an external `NodesContainer nodes`, and its structural key, corresponding to RED's `SCameraSetupData::m_graph`, `m_nodes`, and `m_cameraSetupHash`. `RenderNodeGraph` owns topology only and its occurrence records reference implementations owned by those external containers, matching RED.

The node containers are external to `RenderNodeGraph`, preserving RED's cheap topology composition and implementation reuse. Their cache entry owns the final topology, every temporary per-view topology, and every node container referenced by those topologies. Clearing temporary topology therefore leaves the implementations referenced by the composed graph alive; resetting the cache entry destroys the graphs and their node containers together after in-flight use has joined.

The two blank branches remain distinct exactly as they are in RED. No prepared views builds the no-camera blank graph. `RenderFramePurpose::Blank` builds the camera-aware blank variant even when prepared views exist.

The normal loop preserves RED's no-camera debug-drawer validation, offscreen hit-proxy exclusion, per-view structural key, reusable view-graph lookup, rebuild test, complete rendering-mode selection, and unconditional per-view `AddGraph` after the optional rebuild. A cache hit reuses the fragment implementation but still appends a distinct occurrence for the current view index. After graph composition, `BuildRenderFlowGroups` derives the final flow groups once. `PostBuildClear` releases temporary build-only topology while retaining its externally owned nodes and marks the cache entry built; failure leaves the selected entry invalid so a later frame rebuilds it rather than observing partial state.

Graph-authoring operations take a required `RenderGraphFailure&`. `VG_RETURN_FRAME_GRAPH_FAILURE` keeps their call sites clean while retaining an immediate failure boundary. This macro is provisional local syntax; the engine-wide failure style remains deliberately undecided.

#### 1.8 Install The Current Frame For Node Jobs

This is the direct Vanguard translation of RED's `CRenderNodeJob::SetJobsRenderFrame(frame.Get())` block. `RenderNodeJob::JobsRenderFrame` is a non-owning job-visible pointer; it does not replace or duplicate frame ownership. `context.RetainFrame()` returns a cheap strong reference to the command system's one stable retained-frame allocation. The terminal job and each independently scheduled root branch retain that same allocation; node children borrow it under their root branch's joined cleanup continuation rather than performing one reference-count operation per node. The referenced `RenderFrameInfo`, a locally prepared view family, command scopes, output transaction, and failure state therefore remain alive through the serialized RenderPath tail even if the caller's original `RenderFrameInfo` was stack-allocated.

The slot must be empty before installation. The later named `EndFrame` responsibility clears it after all graph work that may read it has joined. The exactly-once failure terminalizer performs the same clear if normal `EndFrame` is not reached. Isolated node tests that install a frame directly must restore the slot to `nullptr` before returning.

#### 1.9 Start Building The Render-Node Jobs

Like RED, Vanguard starts constructing and linking the render-node job DAG before allocator Resolve. `RunRenderNodeJobs` may execute immediately, but every node job it creates depends on `nodesKickoffCounter`. The counter carries one explicit deferral; the current command-system CPU tail already ordered this callback after earlier frame preparation, so the provisional fake `GetFramePreparationCounter()` dependency is removed. A later allocator-resolution completion job enters allocator execution and finishes `nodesKickoffDeferral`; node execution cannot begin before Resolve and all continuation-local native creation work complete.

`renderNodeJobsBuilder` deliberately remains separate so its complete continuation counter can be joined into `context.GetBuilder()` later. `RunRenderNodeJobs` must construct child builders from its `jobContext`, ensuring the extracted counter covers the setup job and every node job it dispatches. The cache entry's built `RenderNodeGraph` is the executable graph, as in RED; the cache must keep that entry unavailable for rebuild until the terminal graph job joins.

The dispatch call's Shipping-active failure handling remains required. Its final compact syntax will follow the common graph/job failure policy selected during implementation; this provisional example keeps the RED-shaped operation visible without introducing another one-off macro.

#### 1.10 Base Node Context And Frame Command Scopes

Vanguard preserves RED's frame-wide `SRenderNodeImplContext` initialization. The base `RenderNodeImplContext` receives the dispatcher identity, the renderer-owned `RenderFlowResourceAllocator`, the retained current frame, and the exact retained prepared-family pointer. This last field is required when the family was prepared inside `RenderFrame` and therefore is not embedded in the immutable input packet. Later node dispatch applies occurrence-specific view, group, queue, command-scope, command-list, and packet data before invoking that node. Initializing the common context here does not execute a node or discover resource declarations.

RED's `RenderFrameCommandLists::PrepareForFrame` verifies that the preceding frame cleared every slot, resizes its indexed array, resets the next-flush cursor, and initializes every command-list reference to null. It creates no native command list.

Vanguard keeps the same indexed `RenderFrameCommandLists` contract in the retained frame. It reserves the small fixed `ReservedFrameCommandList` prefix and one upper-bound slot per graph node, matching RED's simple recording-time lookup. `StorageData` occupies the first reserved slot. Command-list groups store their command list at `GpuFlowGroup + ReservedFrameCommandList::Count`; CPU-only, Sync, and Present nodes leave their slots empty. Exact allocator command-scope identities and receipts remain separate terminal evidence and are not folded into this recording container.

#### 1.11 Camera Custom Data And The StorageData Scope

RED creates and binds the reserved `CL_STORAGE_DATA` command list before calling `CRenderFrameCameraStorage::PrepareCustomData(rctx)`. Vanguard follows that structure directly. `StorageData` is frame-wide renderer preparation, not cached graph topology: it visits the scene custom-data directory and every camera custom-data directory regardless of which graph-node occurrences later survive. Its objects remain owned by scene/camera storage and persist independently of cached graph definitions.

The existing Vanguard custom-data catalog, fixed type indices, scene/camera ownership, initialization, eviction, readiness checks, and prepared-frame marking already match the useful RED machinery. RG implementation makes only the following contract changes:

```cpp
class SceneCustomData : public CustomData
{
public:
    [[nodiscard]] virtual bool Prepare(const RenderNodeImplContext& context, const RenderCameraStorage& storage, RenderCameraFailure* failure) noexcept = 0;
};

class CameraCustomData : public CustomData
{
public:
    [[nodiscard]] virtual bool Prepare(const RenderNodeImplContext& context, const RenderView& view, RenderCameraFailure* failure) noexcept = 0;
};

bool RenderCameraStorage::PrepareCustomData(RenderNodeImplContext& context, RenderCameraFailure* failure = nullptr) noexcept;
```

`RenderNodeImplContext::InitData` gains the borrowed renderer-owned `RenderCameraStorage`. During the camera loop, `RenderCameraStorage::PrepareCustomData` applies the current prepared view to the context before invoking that camera's polymorphic objects, replacing RED's dummy node-context setup while preserving the same observable access. After recording ends and the command list is unbound, the body validates readiness and moves one `FrameCustomData` view into the retained frame for node access. The current CPU-only `CustomDataPrepareInfo` is retired once this RED-shaped context is available; there is still one preparation callback, not separate CPU and GPU variants. Vanguard retains its explicit `bool`/failure path instead of copying RED's assertion-only `void Prepare`, and adds a `CustomDataPreparationFailed` camera failure code carrying the exact custom-data kind and type index.

The reserved `StorageData` command scope is recorded before allocator declaration/Resolve and is submitted before graph command scopes. Custom data may create, update, initialize, or transition renderer-owned persistent resources there. It may not access transient or aliased graph allocations. A persistent resource consumed by the graph is imported with its post-preparation state and owning queue; a consumer on another queue receives an explicit compiled wait. This preserves RED's general renderer-preparation boundary without letting implicit command-list order masquerade as cross-queue synchronization.

#### 1.12 Allocator Startup And Resource Preparation

RED configures render-flow eviction, enters `RFP_Startup`, enters `RFP_PreConsume`, and calls `graph->ExecuteParallel(...)`. Vanguard keeps the same direct ownership: `RenderFlowResourceAllocator::BeginFrame` replaces Startup, and `RenderNodeGraph::PrepareResourcesParallel` records every resource and queue-sync declaration directly into that allocator. It does not call node `Execute()`, record GPU commands, or perform allocator Resolve.

`FrameResourcePolicy::processEviction` preserves RED's useful working-set behavior: normal onscreen game frames process pool trimming, blank or offscreen game frames retain normally useful allocations, and non-game/tool rendering continues to process eviction. `enablePlacedResources` remains Vanguard's independent dedicated-versus-placed allocation policy. It is not an ESRAM switch, so RED's Durango-only ESRAM block has no false desktop equivalent here; a future platform fast-memory tier would receive its own explicit policy.

The cache entry's built `RenderNodeGraph` is both the composed topology and the executable graph, matching RED. Its external `NodesContainer` owns the node implementations. The retained frame owns only frame-local resource bindings, failures, command scopes, receipts, and other execution state; the renderer-owned allocator owns its current frame, collected requests, resolved generation, physical assignments, and terminal state. `PrepareResourcesParallel` is the graph's last participation in resource preparation.

`PrepareResourcesParallel` initializes the retained frame's per-occurrence binding table before publishing asynchronous declaration work. Its worker contexts copy the borrowed immutable frame/view inputs and use generation-checked allocator writers. The ordered Resolve job first reads the retained preparation-failure latch, then seals planning with synchronous evidence because every declaration predecessor has joined, and only then calls allocator Resolve. There is no invalid immediate failure check while declaration jobs may still be running.

#### 1.13 Allocator Resolve And Node Kickoff

RED dispatches `FlowAllocator_Resolve` and then a second ordered job that releases the render-node kickoff deferral. Vanguard preserves that visible barrier. After graph preparation has registered the complete request stream, the Resolve job talks directly to `m_resourceAllocator`; it receives no graph definition, survivor list, or graph schedule. The continuation-local `creationJobs` builder allows native resource creation work to extend the Resolve continuation. The following finish job therefore runs only after creation is joined, calls direct `RenderFlowResourceAllocator::BeginExecution` when Resolve succeeded, records any failure, and releases the kickoff deferral on every path.

The allocator stores its active generation internally and keeps generation checks on writers, packet views, resolved uses, and receipts. The retained frame therefore does not own or expose a `FrameResourceSession`. It retains only frame-local graph bindings, command records, output ownership, and the first-failure state required by asynchronous work.

Resolve failure is recorded in the retained frame's thread-safe first-failure state. The ordered finish job still releases `nodesKickoffDeferral`; otherwise failure would leave every render-node job permanently blocked. Released node jobs observe the recorded failure and cancel without executing. All job objects are allocated before the first independent node branch is dispatched. Once that branch exists, later scheduling failures are recorded and flow to the same terminal join instead of returning through ordinary setup cleanup.

The terminal job depends on both the normal continuation chain and the complete counter extracted from `renderNodeJobsBuilder`, so its Shipping body is not an empty debug check. `TerminalizeRenderFrame` performs the exactly-once allocator, camera, command-scope, output, `JobsRenderFrame`, and failure dispositions defined by the execution plan. `ArmFailClosedTerminal` is only the job-system failure fallback: the retained frame performs the same terminalization when its last strong job reference is released if the completion counter or terminal dispatch itself could not be established. It is not a second scheduler or a graph-owned allocator wrapper.

Required allocator correction before RG3: remove the public `FrameResourceSession` indirection and `SurvivingGraphOverlay`; move frame lifecycle operations (`RegisterImport`, planning-writer creation, Resolve, BeginExecution, PacketFor, Finish, and pre-publication cancellation) onto `RenderFlowResourceAllocator`; let `BeginFrame` establish the single serialized active frame; let `PrepareResourcesParallel` register GPU-flow groups, command scopes, and explicit queue-sync requests; and let graph-independent `Resolve` consume everything collected without graph topology or a public queue schedule. The allocator keeps its generation and published execution state internally, while its existing generation-stamped writers, packet views, uses, and terminal receipts continue rejecting stale work.

### 2. Named Node Implementation Shape

The graph builders add named implementation classes. Vanguard keeps RED's familiar `RT*` authoring vocabulary, but resource declaration and GPU execution remain separate callbacks. The context stores generation-local IDs in the retained frame's occurrence record; a cached node stores only stable authoring inputs.

#### 2.1 Texture Node

```cpp
class ClearFinalColorNode final : public RenderNodeImpl
{
public:
    [[nodiscard]] bool DeclareResources(const RenderNodeImplContext& context, RenderFlowResourceFailure* failure) const noexcept override
    {
        const RenderFlowNameTag color = context.RTAlloc("FinalColor", BuildFinalColorDesc(context.GetFrameInfo()));
        context.RTUseBegin(color, TextureUseDesc{rhi::ResourceState::RenderTarget, {}, LogicalAccessIntent::Write, ResourceContentIntent::Discard});
        context.RTUseEnd(color);
        return failure == nullptr || failure->code == RenderFlowResourceFailureCode::None;
    }

    void Execute(const RenderNodeImplContext& context, jobs::Builder*) const override
    {
        const ResolvedTextureUse& color = context.RTTexture("FinalColor");
        RecordClearFinalColor(color.GetTexture());
    }

    [[nodiscard]] RenderNodeCommandListUsage GetCommandListUsage() const noexcept override { return RenderNodeCommandListUsage::Own; }
};
```

#### 2.2 Buffer Node

```cpp
class BuildLightListNode final : public RenderNodeImpl
{
public:
    [[nodiscard]] bool DeclareResources(const RenderNodeImplContext& context, RenderFlowResourceFailure* failure) const noexcept override
    {
        const RenderFlowNameTag lightList = context.RTAlloc("LightList", BuildLightListDesc(context.GetFrameInfo()));
        context.RTUseBegin(lightList, BufferUseDesc{rhi::ResourceState::UnorderedAccess, LogicalAccessIntent::Write, ResourceContentIntent::Discard});
        context.RTUseEnd(lightList);
        return failure == nullptr || failure->code == RenderFlowResourceFailureCode::None;
    }

    void Execute(const RenderNodeImplContext& context, jobs::Builder*) const override
    {
        const ResolvedBufferUse& lightList = context.RTBuffer("LightList");
        RecordBuildLightList(lightList.GetBuffer());
    }

    [[nodiscard]] RenderNodeCommandListUsage GetCommandListUsage() const noexcept override { return RenderNodeCommandListUsage::Own; }
};
```

#### 2.3 Conditional Node

`RTDecision` captures the declaration result and replays it during execution, so mutable frame state cannot silently produce a different resource sequence.

```cpp
class OptionalBloomNode final : public RenderNodeImpl
{
public:
    explicit OptionalBloomNode(const bool enabled) noexcept : m_enabled(enabled) {}

    [[nodiscard]] bool DeclareResources(const RenderNodeImplContext& context, RenderFlowResourceFailure* failure) const noexcept override
    {
        if (context.RTDecision(m_enabled))
        {
            const RenderFlowNameTag bloom = context.RTAlloc("Bloom", BuildBloomDesc(context.GetFrameInfo()));
            context.RTUseBegin(bloom, TextureUseDesc{rhi::ResourceState::UnorderedAccess, {}, LogicalAccessIntent::Write, ResourceContentIntent::Discard});
            context.RTUseEnd(bloom);
        }
        return failure == nullptr || failure->code == RenderFlowResourceFailureCode::None;
    }

    void Execute(const RenderNodeImplContext& context, jobs::Builder*) const override
    {
        if (!context.RTDecision(m_enabled))
            return;
        RecordBloom(context.RTTexture("Bloom").GetTexture());
    }

    [[nodiscard]] RenderNodeCommandListUsage GetCommandListUsage() const noexcept override { return RenderNodeCommandListUsage::Own; }

private:
    bool m_enabled = false;
};
```

#### 2.4 Temporary Resource Node

```cpp
class BlurNode final : public RenderNodeImpl
{
public:
    [[nodiscard]] bool DeclareResources(const RenderNodeImplContext& context, RenderFlowResourceFailure* failure) const noexcept override
    {
        const RenderFlowNameTag scratch = context.RTTempAlloc("BlurScratch", context.RTNameTag("FinalColor"));
        context.RTUseBegin(scratch, TextureUseDesc{rhi::ResourceState::UnorderedAccess, {}, LogicalAccessIntent::ReadWrite, ResourceContentIntent::Discard});
        context.RTUseEnd(scratch);
        return failure == nullptr || failure->code == RenderFlowResourceFailureCode::None;
    }

    void Execute(const RenderNodeImplContext& context, jobs::Builder*) const override { RecordBlur(context.RTTexture("BlurScratch").GetTexture()); }
    [[nodiscard]] RenderNodeCommandListUsage GetCommandListUsage() const noexcept override { return RenderNodeCommandListUsage::Own; }
};
```

#### 2.5 Cross-Node Use And Command-List Group

RED sometimes begins a render-target interval in one node and ends it in another. Vanguard preserves that syntax. The opening node receives the packet-local resolved access; the named interval keeps the allocation alive through the later closing node.

```cpp
class BeginGBufferNode final : public RenderNodeImpl
{
public:
    [[nodiscard]] bool DeclareResources(const RenderNodeImplContext& context, RenderFlowResourceFailure* failure) const noexcept override { context.RTUseBegin("gbuffer0", TextureUseDesc{rhi::ResourceState::RenderTarget, {}, LogicalAccessIntent::Write, ResourceContentIntent::Preserve}); return failure == nullptr || failure->code == RenderFlowResourceFailureCode::None; }
    void Execute(const RenderNodeImplContext& context, jobs::Builder*) const override { RecordBindGBuffer(context.RTTexture("gbuffer0").GetTexture()); }
    [[nodiscard]] RenderNodeCommandListUsage GetCommandListUsage() const noexcept override { return RenderNodeCommandListUsage::Require; }
};

class EndGBufferNode final : public RenderNodeImpl
{
public:
    [[nodiscard]] bool DeclareResources(const RenderNodeImplContext& context, RenderFlowResourceFailure* failure) const noexcept override { context.RTUseEnd("gbuffer0"); return failure == nullptr || failure->code == RenderFlowResourceFailureCode::None; }
    void Execute(const RenderNodeImplContext&, jobs::Builder*) const override {}
    [[nodiscard]] RenderNodeCommandListUsage GetCommandListUsage() const noexcept override { return RenderNodeCommandListUsage::Require; }
};

RENDER_COMMAND_LIST(NodeGroupId::Camera, "GBuffer")
{
    ADD_SUBNODE("BeginGBuffer", BeginGBufferNode);
    ADD_SUBNODE("DrawOpaque", DrawOpaqueNode);
    ADD_SUBNODE("EndGBuffer", EndGBufferNode);
}
```

The command-list group privately emits one queue begin/end pair around its ordered children. `RenderNodeSynchronize` privately emits the explicit queue-sync request used by `SYNC_SUBMIT`. Ordinary render nodes cannot obtain the allocator or planning writer directly.

A CPU-only preparation node simply declares no resources and returns `RenderNodeCommandListUsage::None`. `PresentNode` is also named, but its specialized terminal execution consumes the exact `RenderFrameOutputTransaction` only after final Graphics submission, allocator `Finish(Completed)`, and prepared-family commit. It does not pretend to own a normal command scope.
