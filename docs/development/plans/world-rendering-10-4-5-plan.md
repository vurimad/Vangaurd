# 10.4.5 production viewport frames and editor scene output

## Status and authorization

Defined on 2026-09-20 for cross-agent implementation. **The definition gate and
P0-P6 are closed. Every implementation gate closed at a fresh independent
exact-CURRENT 0 BLOCKER / 0 HIGH / 0 MEDIUM review; the final P6 closure review on
2026-09-25 also returned 0 BLOCKER / 0 HIGH / 0 MEDIUM.** 10.4.3 and 10.4.4 are
closed. 10.4.6 session replacement/terminal draining and 10.4.7 pixel/performance
acceptance remain outside this contract and are unstarted by this task.

This phase closes G5 from the
[world/rendering integration study](../studies/world-rendering-integration-study.md).
At definition time the renderer already had production frame construction, graph
execution, presentation and texture-output primitives, but runtime did not yet supply a
world presentation frame and the editor had no generation-safe world-scene texture
producer/consumer handoff. P0-P6 below record how that gap was closed.

The active task triplet is
`.planning/world-rendering-10-4-5-production-viewport-output/`. Durable
architecture and accepted completion stay in this plan and the development
documentation map. Do not put task-specific state in `.pi/`.

## Objective

Connect the already-closed world/draw/camera path to real runtime and editor
outputs without creating a second renderer path:

1. runtime owns one normal presentation supplier backed by the actual native
   window/output capabilities and the existing `PresentationService`;
2. runtime and editor scene suppliers construct ordinary `EngineViewport` frames
   after the single process `RenderingFrameTickParticipantId`;
3. editor scene output is a `RenderViewportOutputKind::Texture` generation sampled
   through the existing `EditorTextureHandle`/ImGui path, while the editor UI keeps
   sole ownership of native-window presentation;
4. texture producer state, queue and readiness are handed to the editor consumer
   explicitly through existing RenderFlow/RHI synchronization semantics; and
5. output resize/replacement, suspension and failure preserve generational
   ownership and every begun frame is submitted or abandoned exactly once.

10.4.5 does not redesign camera selection, streaming observers, scene retirement,
world-stop draining, panel/docking ownership, the render graph, the RHI or the
renderer draw contract.

## Mandatory read-first set

Before every authorized gate:

1. root `AGENTS.md` and [development document map](../README.md);
2. [world/rendering integration study](../studies/world-rendering-integration-study.md),
   especially G5 and 10.4.4-10.4.7;
3. closed [10.4.3](world-rendering-10-4-3-plan.md) and
   [10.4.4](world-rendering-10-4-4-plan.md) plans plus their `.planning` triplets;
4. `source/rendering/include/vanguard/rendering/{viewport,presentation_service,
   render_flow_resource_allocator,render_flow_resource_execution}.hpp` and the
   corresponding implementations;
5. `source/rendering/src/{frame_renderer,render_node_job,render_graph_nodes}.cpp`;
6. `source/engine/include/vanguard/engine/{rendering_service,
   world_session_service,frame_pipeline_service}.hpp` and implementations;
7. runtime/editor application composition and the editor UI renderer/texture API;
8. exact CURRENT RED viewport/render-frame/render-command sources listed below.

Inspect `git status --short` and scoped diffs before edits. Preserve unrelated
tracked/untracked work. Premake/build/executable validation is batched into P6;
intermediate gates use source/hygiene checks plus independent read-only review.

## Current Vanguard facts to preserve

1. `RenderViewportOutputKind::{Presentation,Texture,Headless}` is already the
   output lifecycle/ownership classification. `RenderFramePurpose` separately
   describes why the frame exists (`Normal`, `Thumbnail`, `Capture`, ...). Do not
   add `EditorPanel`, `Thumbnail` or similar output kinds.
2. `EngineViewport` is the frame-construction/submission object;
   `RenderViewport` owns the physical/logical output. Existing
   `BeginFrame`/`ConfigureViews`/`SubmitFrame`/`AbandonFrame` remain the only world
   frame path.
3. Presentation frames acquire an exact swapchain image. The graph imports it with
   presentation semantics, reaches the terminal output node, then asynchronous
   retained-frame completion validates RenderFlow and calls `RenderViewport::Present`
   -> `rhi::Present`. `FlushPreviousFrameProcessing()` does not call Present; it
   remains the critical next-frame CPU dispatch boundary only.
4. `RenderNodePresent` currently marks that the graph reached its terminal output
   node. `RenderGraph/NodesComplete` is dependency-scheduled from the terminal leaf
   and runs `FinishBuiltGraph`; normal presentation does not poll or synchronously
   wait for graph completion before calling Present.
5. Texture outputs are retained external textures. Exact-current `FrameRenderer`
   imports them using their texture descriptor `initialState` as both initial and
   terminal state and `CompleteOutput()` only releases the acquisition. This is not
   yet a sufficient live producer->consumer receipt.
6. RenderFlow already models retained-import `initialState`, `terminalState`, queue,
   `SameQueueContinuation`/`ExplicitFenceWait`, and terminal exports with
   `SameQueueContinuation`/`ExplicitFenceSignal` plus a ready fence. Reuse this
   machinery rather than adding a private synchronization system.
7. `EditorTextureHandle` is a generation-checked sampling identity. The existing
   descriptor carries texture, sampler and color space; editor UI RenderFlow imports
   sampled textures in their creation `initialState`. Scene-output readiness/state
   is therefore a real missing seam at this boundary.
8. The editor UI renderer already owns each editor native-window presentation
   output and submits its UI graph in `EndFrame` after
   `RenderingFrameTickParticipantId`. A scene panel must feed that graph as a
   texture; it must not attach another swapchain or manually Present the window.
9. Texture `RenderViewport` creation currently retains one `outputTexture` and has
   no public in-place texture rebind API. Resize/replacement therefore needs an
   explicit generation-safe design in P0; do not silently replace a texture behind
   an existing `EditorTextureHandle`.
10. `WorldSessionService::GetCameraBinding()` is the 10.4.4 handoff. A Ready binding
    supplies an exact live `RenderCameraHandle`; 10.4.5 must validate it at frame
    supply and must not recreate selection/observer policy.

## RED architectural evidence to refresh in P0

The RED checkout is `D:\root\R6.Root\Mainline\dev\src` and is read-only.
Definition evidence already establishes the intended ownership; P0 must refresh
exact CURRENT paths/line ranges before implementation:

- `common/renderData/include/renderFrameInfo.h` and
  `common/renderData/src/renderFrameInfo.cpp`: viewport frames default to
  presentation while extent-only frames are offscreen/no-present; explicit dynamic
  texture outputs are separate from viewport ownership.
- `common/renderer/src/renderRenderFrame.cpp`: final GPU flush is CPU-linked to
  `CRenderNode_Present`, then optional texture-grab flush/EndFrame. Graph nodes run
  as RenderPath jobs with dependency counters.
- `common/renderer/src/renderGraphNodes.cpp`, `CRenderNode_Present::Execute`, and
  `common/renderer/src/renderViewport.cpp`: the graph-tail Present node calls the
  viewport and ultimately `GpuApi::Present`.
- `common/renderer/src/renderCommandHandler.cpp`: `RenderScene` asynchronously
  chains retained RenderPath work; `FlushPreviousFrameCommandsProcessing()` only
  flushes the prior render counter and contains no explicit Present.
- `common/gameFramework/src/gameEngine.cpp`: normal frames keep the previous
  render-tail flush as a critical dispatch boundary before the next renderer frame.
- `common/worldEntities/src/renderToTextureCameraComponent.cpp` and
  `common/renderer/src/renderNode_Finalize.cpp`: offscreen scene rendering uses an
  explicit GPU texture sink and does not fake presentation.
- `backend/backendEngine/src/thumbnailRendering.cpp`, extractor/grab nodes and
  preview-panel code: CPU thumbnails/readback are downstream extraction operations,
  not additional presentation targets.

Intentional Vanguard deviations remain: generational handles instead of raw
viewport/resource ownership, RenderFlow explicit state/queue/readiness contracts,
and a retained output transaction. Do not copy RED's raw target fields or weaker
implicit resource handoff.

## Locked ownership and scheduling invariants

### One common frame path

- `RenderingFrameTickParticipantId` remains the single process-global renderer
  frame tick/GPU Scene publication point.
- Runtime/editor world-view suppliers run after it and submit ordinary
  `EngineViewport` frames. Multiple viewports do not repeat global GPU Scene work.
- The runtime frame/output supplier owns the lifetime of its runtime
  `PresentationService`. Its reconciliation participant is registered before the
  Frame Pipeline is compiled, runs on the main thread in
  `FramePhase::Presentation` (after the renderer's `RenderUpdate` boundary and
  before `RenderingFrameTickParticipantId` in `FramePhase::Render`), and ticks the
  service before the separate runtime world-frame supplier attempts the
  corresponding presentation frame. Reconciliation must consume the current
  native-window extent/visibility/occlusion/surface revisions and bind/resize the
  swapchain through the existing `PresentationService::Tick` path; frame supply
  must not duplicate those window/swapchain rules. P0 confirms the exact IDs,
  dependency edges and transient Ready/AwaitingSurface/Suspended/Failed policy;
  P2 implements one bounded owner/lifetime from startup through output destruction.
- `RenderFrameViewSetup` consumes the exact current 10.4.4 camera binding and
  existing scene. Multi-camera/output-region graph dependencies remain renderer
  owned.
- Every successful `BeginFrame` has exactly one terminal action:
  `SubmitFrame` or `AbandonFrame`.

### Presentation stays in the asynchronous render tail

- No application, editor panel, frame supplier or
  `FlushPreviousFrameProcessing()` path may call `rhi::Present` directly.
- Actual presentation remains owned by the Presentation `RenderViewport` and the
  asynchronous retained-frame terminal continuation after RenderFlow terminal
  validation.
- The next-frame `FlushPreviousFrameProcessing()` remains intact as the renderer's
  CPU dispatch/order boundary, matching RED's model. It is not GPU-readiness
  evidence for editor textures and is not moved merely to make scene output work.

### Texture output is a producer/consumer contract

- A scene texture is `RenderViewportOutputKind::Texture`, normally
  `RenderFramePurpose::Normal`; thumbnails/captures remain purposes/consumers, not
  new output kinds.
- The producer publishes the exact terminal state, terminal queue and readiness for
  the produced generation. The consumer import must use that contract.
- If producer and consumer are provably ordered on the same Graphics queue, use
  same-queue continuation. **CPU frame-participant order or a root-task dependency
  alone is not that proof:** the proof must include all retained continuations through
  terminal RenderFlow/RHI command submission. P0 has now traced that exact chain for
  the shared `RenderCommandSystem`; the locked result is recorded below. CPU job
  completion must never masquerade as GPU completion.
- Do not wait on the CPU between the scene-frame submission and the editor UI frame
  merely to make the texture safe to sample.

### Generation and resize ownership

- A scene-output generation owns one concrete RHI texture, its Texture
  `RenderViewport` relationship, editor texture registration and producer receipt.
- The generation captured while authoring one editor UI frame is immutable for that
  retained UI frame. If resize/format/output replacement requests a newer
  generation after capture, that retained frame continues to reference the old
  generation and cutover is deterministically deferred to a later UI frame; the
  resource behind a captured `EditorTextureHandle` is never mutated in place.
- Resize/format replacement creates a new logical generation. The old generation
  remains retained until no submitted producer or editor consumer can reference it;
  only then may the viewport/texture/editor-handle resources retire.
- No stale `EditorTextureHandle` may resolve to a replacement resource and no new
  generation may reuse an old identity while an in-flight UI frame retains it.
- Logical `UnregisterTexture` is not GPU-retirement proof. P0 must name the owner of
  each old generation and the exact evidence for both sides of retirement: the last
  producer submission/receipt and the last retained UI consumer submission/receipt.
  The existing retained `EditorUiRenderTexture` RHI reference is useful lifetime
  evidence, but resource/descriptor destruction still follows submitted GPU
  retirement fences/receipts.
- P0 must choose the smallest existing-owner implementation: recreate the bounded
  viewport/output generation or add a narrowly-scoped generation-aware rebind seam.
  It must not add a second texture registry or private retirement queue.

### Editor-frame receipt timing

- Editor panel/UI authoring occurs before the Render-phase scene producer, so the
  current frame cannot snapshot a producer fence that does not exist yet during
  early `EditorTextureHandle` resolution/capture.
- A producer generation may publish Ready only from successful terminal RenderFlow
  execution with its actual terminal state/queue and, for explicit-fence readiness,
  the submitted final-scope GPU fence. Failure, abandonment and device loss never
  publish a Ready receipt for that generation.
- An explicit-fence UI consumer resolves/retains the producer receipt at RenderFlow
  import/execution time, or through an equivalent deferred receipt object whose
  value is published by the producer; it must not copy a stale/nonexistent fence
  during BeginFrame UI capture. P0 may instead choose to sample the most recent
  already-ready generation, but must state the resulting frame latency explicitly.
- Same-queue continuation remains allowed only under the actual GPU submission-order
  proof above; it does not waive generation immutability or retirement evidence.

### Editor ownership

- The editor UI keeps the only native-window presentation owner. ImGui samples a
  scene texture through the existing `EditorTextureHandle` API and the existing UI
  renderer graph.
- 10.4.5 may extend the editor texture handoff with neutral output-publication readiness/state
  metadata, but must not duplicate the texture registry, panel registry, docking,
  extension system or UI render graph.
- E3.3 panel/docking policy remains owned by the editor foundation task. 10.4.5
  supplies/consumes scene-output resources through that public panel/UI boundary
  without redefining panel lifecycle.

### Output capabilities and encoding

- Presentation dimensions, format, visibility, occlusion, minimization and surface
  revisions come from the actual native presentation owner; no fixture resolution
  or guessed swapchain format becomes production authority.
- Camera output is scene-linear. P0 must trace exact current resolve/UI color-space
  behavior and lock one minimal correct SDR path for this milestone. HDR/PBR/
  exposure maturity is not required, but an SDR result must not be produced by
  silently treating linear HDR data as display-encoded bytes.
- Minimized/suspended/unavailable outputs skip or abandon deterministically without
  fabricating a presentable image.

## P0 exact-current decisions

The following decisions are the implementation boundary produced by the authorized
P0 source/RED proof. They refine the definition; they do not implement P1.

### Participant topology and runtime presentation owner

- P2 adds one runtime-owned frame/output service, initialized with the other engine
  services before `RunningState::OnEnter` compiles the Frame Pipeline. It owns one
  `PresentationService`, its single runtime `PresentationOutput`, the corresponding
  Presentation `RenderViewport`, and one `EngineViewport` for the world frame.
- That service declares required dependencies on FramePipeline, Rendering, Window and
  WorldSession (and no independent copies of those owners). EngineHost compiles
  dependencies provider-first and runs Quiesce/Drain/Stop/Shutdown in reverse, so the
  runtime output owner quiesces before its rendering/window/session providers. Its
  Quiesce first disables further supply, destroys the EngineViewport/output after the
  retained render tail is drained, and requires `PresentationService` to reach zero
  live outputs. FramePipeline itself refuses quiesce while Executing and clears all
  participant records in `OnShutdown` before the later service-instance destruction
  pass; therefore its compiled callbacks cannot outlive their `userData` owner.
- The reserved participant identities are
  `RuntimePresentationReconcileParticipantId = 0x7274707265736e01` and
  `RuntimeWorldFrameParticipantId = 0x7274776f726c6401`. Reconciliation runs on the
  main thread in `FramePhase::Presentation` after
  `RenderingUpdateFrameParticipantId`. World-frame supply runs on the main thread in
  `FramePhase::Render` after `RenderingFrameTickParticipantId`. Phase order plus those
  explicit dependencies gives
  `RenderUpdate -> runtime presentation reconcile -> renderer FrameTick -> runtime world frame`.
- P3 reserves `EditorSceneFrameParticipantId = 0x65647363656e6501` for the editor
  scene producer. It runs in `FramePhase::Render` after
  `RenderingFrameTickParticipantId`. Existing UI authoring stays in BeginFrame and
  existing editor UI/native presentation stays in EndFrame; the scene producer never
  creates or presents an editor swapchain.
- The P3 editor scene-output service similarly depends on FramePipeline, Rendering,
  WorldSession and EditorUi so its scene generations/participant become quiescent
  before those providers; no post-compile participant removal API is invented.
- `PresentationService::Tick()` remains the only runtime swapchain reconciliation
  authority. For the one runtime output, `Ready` allows frame supply only when the
  viewport is visible, non-occluded and has a valid extent. `Suspended`, minimized
  and occluded are retryable no-frame states. `AwaitingSurface` is not blindly
  treated as success: native-surface unavailability and explicitly transient RHI
  `Busy`/`Timeout` remain retryable, while device loss, out-of-memory, unsupported/
  backend failure or invalid ownership escalates through the Tick failure even if
  the current output state is `AwaitingSurface`. `Failed` is terminal for this owner.
  Surface/extent revisions are never reimplemented by the frame supplier.
- Session not-Running, no/pending camera binding, or no valid output means no
  `BeginFrame`. After a successful `BeginFrame`, every later failure before accepted
  submission takes exactly one `AbandonFrame`; a successful `SubmitFrame` owns the
  frame thereafter.

### Texture output identity and per-production receipt

- P0 found an exact-current prerequisite defect: Texture `RenderViewport` creation
  leaves `m_outputRevision == 0`; `AcquireOutput` copies that zero while
  `RenderOutputAcquisition::IsValid()` requires a nonzero revision. Therefore a
  Texture frame cannot currently form a valid retained output transaction. P1 must
  initialize a nonzero Texture output revision before the new handoff is exercised.
- The neutral sampled-output identity is the existing generational
  `RenderViewportHandle` plus its nonzero `outputRevision`. P1 exposes a narrow
  Texture-output publication/receipt query at that boundary; it does not put editor
  concepts into rendering core.
- P1 also makes the Texture viewport's **resting/release contract** explicit in its
  descriptor: resting resource state, queue and consumer publication-readiness mode. The caller's
  RHI texture must be created in the same resting state, FrameRenderer imports it with
  that state/queue as both entry and terminal contract, and any external consumer must
  return it to that contract before the next producer. This removes the current
  accidental dependence on `TextureDesc.initialState` as an implicit policy. The
  10.4.5 scene contract is `ShaderResourceGraphics` / Graphics /
  `SameQueueContinuation`. P1's single terminal `EndRender` owner submits on Graphics,
  so P1 rejects non-Graphics resting queues up front rather than accepting a contract it
  cannot terminate truthfully. Producer entry remains the already-proved same-Graphics
  retained continuation; `ExplicitFence` in this descriptor means the completed producer
  publication is consumable through its exported fence, not that P1 invents a missing
  incoming consumer-return fence.
- Publication is **per accepted producer serial**, not merely per resource
  generation. Once Texture producer frame serial N is accepted, that output epoch is
  Pending. The bounded in-slot publication state is synchronized for main-thread
  submission, render-worker completion and later render-worker/UI import reads. P1
  pre-arms exact serial N in `ViewportManager::SubmitFrame`, after output acquisition
  but **before** calling `RenderCommandSystem::RenderFrame`; if that call is
  synchronously rejected, it restores the prior receipt and the returned/local output
  transaction unwinds the valid acquisition. This keeps the rule inside the existing
  viewport boundary and prevents a fast terminal Ready publication from being
  overwritten by a late main-thread Pending store. Only successful terminal
  RenderFlow execution may publish Ready for the exact
  `{viewport, outputRevision, producerSerial}` epoch. Asynchronous execution failure
  or device loss makes accepted serial N non-Ready/Failed; a later consumer must not
  fall back to an older Ready receipt for the same physical texture after N may have
  partially used it. A frame abandoned before submission creates no new epoch.
- The receipt carries the exact terminal resource state, terminal queue, readiness
  mode and the real terminal GPU fence. P1 obtains those facts from the existing
  terminal RenderFlow export of the imported `FrameOutput`: reserve/request one
  export for Texture output, take/validate that export after successful allocator
  Finish, then call `CompleteOutput()`. Ready is published only after both the export
  extraction and Texture output completion succeed; any failure leaves exact serial
  N Failed/non-Ready and releases the temporary export owner. Imported RenderFlow
  resources already support terminal export, so no editor fence registry or second
  synchronization system is introduced.
- The locked steady-state scene-output contract is resting/terminal
  `rhi::ResourceState::ShaderResourceGraphics`, queue Graphics and
  `SameQueueContinuation`. The actual exported Graphics fence is still retained as
  exact producer/retirement evidence and as the neutral fallback datum if a future
  consumer cannot prove same-queue order.

### Actual same-Graphics submission proof and editor receipt timing

- Sequential `RenderCommandSystem::RenderFrame` roots depend on the shared
  `cpuTail`. Crucially, that tail is extended by `jobs::Builder` child continuations;
  `FrameRenderer` joins its independent render-node branch back into the retained
  continuation, terminal graph leaves gate `RenderGraph/NodesComplete`, and
  `FinishBuiltGraph` performs RenderFlow completion and final RHI command submission
  inside that chain. This is stronger than frame-phase or root-task ordering alone.
- RHI Graphics submissions are serialized in issue order. The scene output write and
  editor UI sample are both Graphics operations. With the P3 producer on the same
  command system, the proven order is
  `producer N Graphics submission -> UI N Graphics submission -> producer N+1 Graphics submission`.
  This proves same-queue execution order without claiming that the CPU tail is a GPU
  completion fence and without adding a CPU wait.
- Therefore P3 uses current-frame deferred receipt resolution, not a previous-ready
  ring. BeginFrame/UI capture retains the immutable Texture-output identity and RHI
  refs; the UI RenderFlow import queries the current output receipt later, after the
  producer's terminal continuation. It requires the current accepted producer serial
  to be Ready before importing. A Pending/Failed current epoch fails closed rather
  than sampling an older receipt for a texture that may have been touched.
- If no new producer frame is accepted at all because demand/camera/output is absent,
  the last successfully Ready epoch remains a valid unchanged image. This is
  distinct from an accepted producer epoch that later fails.

### Generation replacement and retirement

- P1/P3 recreate the Texture `RenderViewport` plus its `EngineViewport` for a new
  size/format generation; they do not add in-place output-texture rebind authority.
  One physical texture is reused for steady-state frames because the same-Graphics
  ordering above prevents producer N+1 from overwriting it before UI N samples it.
- A scene output owns at most two live application generation records: the active
  sampled generation and one pending/replacing generation. Further resize/format
  requests coalesce while the pending generation is in flight; no unbounded resize
  ring is allowed. A failed/obsolete pending record is logically released before a
  replacement is allocated; RHI may of course retain its native allocation internally
  until recorded GPU fences retire. A pending generation is not promoted into a new
  `EditorTextureHandle` until its first exact producer epoch publishes Ready. Until
  then the UI keeps the old Ready generation, or a placeholder when no prior
  generation exists.
- If UI capture has already retained an old generation when a replacement becomes
  Ready, that old generation record -- including its `RenderViewport` receipt owner --
  remains alive through the captured UI frame's late `RegisterImports` and submission.
  The next `EditorUiRenderer::BeginFrame` joins the previous retained UI CPU tail via
  the existing `FlushPreviousFrameProcessing()` boundary; only after that join may the
  application generation record/receipt owner be released. Native texture/descriptor
  lifetime after logical release remains governed by the RHI/descriptor GPU fences
  described below. This still fits the two-generation bound: after cutover, the new
  generation is active while the old captured generation is the one retiring record.
- For a successful Ready epoch, the exact last-producer proof exposed to the scene
  owner is its terminal-export fence. An accepted producer may instead submit some
  Graphics work and fail before an export is published; that failed epoch has no
  fabricated export fence. In that case the generation is first removed from logical
  reuse after the retained CPU tail has joined, while ordinary native safety comes
  from the RHI resource's recorded per-queue last-use fences. If native execution was
  issued but a completion fence could not be signaled, the existing device-loss/
  `AbandonDevice` path owns that fence-less work rather than normal generation reuse.
  The last retained UI consumer is protected by the UI frame's retained RHI reference
  and by the resource's RHI-recorded Graphics last-use fence; descriptor retirement
  continues to use the existing joined submitted-fence snapshot plus
  `RetireDescriptor`. Final RHI reference release already defers native destruction
  until all recorded queue uses retire. `UnregisterTexture()` remains logical
  revocation only and is never treated as GPU-retirement proof.
- Output/extent generation replacement follows existing RenderCamera/history policy;
  it does not automatically request a camera cut. Terminal world/session draining is
  still 10.4.6.

### Minimal SDR contract

- Camera color remains scene-linear `R16G16B16A16Float`. The current
  `ResolveCameraOutput` shader is a raw copy and therefore is not by itself a valid
  display transform.
- P1 adds a neutral `RenderOutputColorEncoding` property to `RenderViewportDesc`/
  `RenderViewport`; it is orthogonal to `RenderViewportOutputKind` and
  `RenderFramePurpose`. `SceneLinear` preserves today's raw-copy behavior for generic
  capture/texture callers; `DisplaySrgb` requests the explicit SDR display transfer.
  This avoids changing all Texture/Capture outputs merely because the editor scene
  needs display-ready pixels.
- 10.4.5 deliberately selects the existing SDR presentation policy for runtime. The
  current `PresentationService` SDR path is `B8G8R8A8UNorm` with sRGB output color
  space; P4 maps that SDR policy to `DisplaySrgb` on the owned RenderViewport. The
  owner still reads and validates the actual active format/color space rather than
  guessing them.
- Editor scene generations use display-encoded `R8G8B8A8UNorm` and register as
  `EditorTextureColorSpace::DisplaySrgb` and set the viewport encoding to
  `DisplaySrgb`. P4 gives `ResolveCameraOutput` a resolve-specific shader entrypoint/
  constant selected from that neutral viewport encoding; the existing GBuffer
  visualization entrypoint remains unchanged. For the supported non-sRGB UNorm SDR
  targets, scene-linear RGB is clamped to the supported display range and encoded
  with the sRGB transfer function before it is written. The editor UI then passes
  those display-ready bytes through instead of treating linear HDR bytes as sRGB.
  Unsupported encoding/format pairs fail closed rather than double-encoding.
  Mature exposure/tone mapping, HDR10/scRGB and final image-quality policy remain
  outside 10.4.5.

### Bounded implementation and proof matrix

- P1 rendering-core surface is bounded to
  `source/rendering/include/vanguard/rendering/{viewport,render_node_impl_context,
  render_graph_nodes}.hpp`, their corresponding `.cpp` files plus
  `frame_renderer.cpp`. One private production helper,
  `source/rendering/private/vanguard/rendering/texture_output_receipt_state.hpp`, may
  hold the exact receipt-transition rules so production and deterministic proofs do not
  duplicate that authority. Focused contract/source proofs live in
  `source/rendering/tests/viewport_tests.cpp` and `render_graph_tests.cpp`; the existing
  D3D12-backed `geometry_allocator_tests.cpp` may additionally exercise that private
  receipt helper, real Texture viewport admission/lifecycle and imported terminal-export
  publication. Targets: `renderingTests` and, where the receipt/allocator/export proof
  changes, `geometryAllocatorTests`.
- P2 runtime surface is one new application-owned service under
  `runtime/private/vanguard/runtime/runtime_viewport_service.hpp` and
  `runtime/src/runtime_viewport_service.cpp`, composed by
  `runtime/src/runtime_application.cpp`. A bounded `runtimeViewportTests` target in
  `runtime/premake5.lua` owns its participant/transient/begin-submit-abandon proofs.
- P3 editor surface is exactly one scene-output service under
  `editor/private/vanguard/editor/editor_scene_output_service.hpp` and
  `editor/src/editor_scene_output_service.cpp`, composed by
  `editor/src/editor_application.cpp`, plus
  `editor/framework/include/vanguard/editor/editor_ui.hpp`,
  `editor/framework/src/editor_ui.cpp`,
  `editor/framework/src/editor_ui_imgui_platform.cpp`,
  `editor/private/vanguard/editor/editor_ui_render_data.hpp`,
  `editor/framework/src/editor_ui_render_data.cpp`,
  `editor/private/vanguard/editor/editor_ui_renderer.hpp`, and
  `editor/framework/src/editor_ui_renderer.cpp` for immutable output identity and
  late receipt resolution in `EditorUiRenderer::RegisterImports`. A separate
  `editor/tests/editor_scene_output_tests.cpp` / `editorSceneOutputTests` target in
  `editor/premake5.lua` keeps 10.4.5 tests out of E3.3 panel ownership.
- No new unbounded output table is introduced. Editor scene records consume the
  existing hard `MaximumRenderViewports == 64` / `MaximumEngineViewports == 64`
  pools, with at most two live generation records per logical scene output; capacity
  exhaustion is an explicit failure and pending resize churn is coalesced rather than
  growing a side queue. `MaximumEditorTextures == 4096` remains the existing UI
  registry bound rather than a new 10.4.5 authority.
- P4 encoding is limited to
  `source/rendering/shaders/geometry_visualization.vsl`,
  `source/rendering/include/vanguard/rendering/geometry_graph_nodes.hpp`,
  `source/rendering/src/geometry_graph_nodes.cpp`,
  `source/rendering/include/vanguard/rendering/renderer_feature_programs.hpp`, and
  `source/rendering/src/presentation_service.cpp`; focused resolve-selection/format
  proofs stay in `source/rendering/tests/render_graph_tests.cpp` and
  `source/rendering/tests/viewport_tests.cpp` under `renderingTests`. P6 builds and
  executes `bootstrapImage` against scratch output rather than adding a runtime
  shader compiler.
- P5 authors deterministic cases for nonzero output identity; invalid/incompatible
  Texture resting contract; SceneLinear vs DisplaySrgb resolve selection and rejected
  encoding/format pairs; Pending/Ready/Failed
  serial replacement; producer failure after prior success; failed-after-partial-GPU-
  submit retirement without a terminal export; same-Graphics ordering;
  explicit-fence fallback validation; resize coalescing/two-generation cap; stale
  editor handle; retained-consumer retirement; Ready/AwaitingSurface/Suspended/
  Failed/occluded runtime states; camera loading/missing/Ready; one FrameTick with
  multiple outputs; exact submit/abandon balance; device loss; and proof that only
  the existing retained render tail owns Present. These cases are authored in the
  already named `source/rendering/tests/viewport_tests.cpp` and
  `source/rendering/tests/render_graph_tests.cpp`, reusing
  `geometry_allocator_tests.cpp` only for allocator/export coverage,
  `runtime/tests/runtime_viewport_tests.cpp` / `runtimeViewportTests`, and
  `editor/tests/editor_scene_output_tests.cpp` / `editorSceneOutputTests`; P5 adds no
  extra production or test owner. The resize matrix includes the exact race where a
  replacement becomes Ready after UI capture and proves the captured old generation
  remains queryable/sampleable until the next UI BeginFrame tail join. P6 is the only
  gate that executes the broad Debug/Development/native matrix.

## Explicit non-goals

- No second render graph, command scheduler, swapchain system, texture registry,
  camera registry, GPU Scene update, renderer world, geometry fallback or direct
  component draw path.
- No serialized renderer/viewport/editor texture handles.
- No new thumbnail/panel output kinds.
- No editor-side manual Present or CPU wait between world texture production and UI
  sampling.
- No 10.4.6 world-stop/replacement drain redesign. 10.4.5 owns only the output
  generation lifecycle necessary to make its live frame path correct; terminal
  session draining stays in 10.4.6.
- No 10.4.7 final pixel equivalence, broad surface coverage or performance claim.

## Gates

### P0 - exact-current architecture proof and gate refinement

- Re-trace Vanguard runtime/editor frame pipeline, viewport/output transaction,
  RenderFlow import/export receipts, UI texture imports and output retirement.
- Refresh the RED evidence above, including the previous-frame dispatch flush and
  graph-tail Present ownership.
- Lock exact participant ordering for runtime scene frames, editor scene frames and
  editor UI presentation.
- Confirm the runtime `PresentationService` owner/lifetime and its main-thread
  `FramePhase::Presentation` reconciliation participant before renderer FrameTick,
  including Ready/AwaitingSurface/Suspended/Failed behavior and registration before
  Frame Pipeline compilation.
- Decide the smallest neutral producer-receipt surface and the texture-generation
  resize/replacement mechanism without editor-specific rendering APIs.
- For any proposed same-Graphics-queue editor handoff, prove actual cross-retained-
  frame GPU submission order through the complete retained continuation and RHI
  submission chain; frame-pipeline/root-task order alone is insufficient. Fall back
  to explicit producer fence/wait when that proof is absent.
- Lock editor-frame receipt timing. P0 selected a deferred current-frame receipt
  consumed at UI RenderFlow import/execution time and named the producer/consumer GPU
  retirement evidence above.
- Lock minimal SDR format/color-space policy and explicit loading/minimized states.
- Produce exact file/test, failure/lifecycle and bounded-capacity matrices.
- Independent exact-CURRENT review must return 0 BLOCKER / 0 HIGH / 0 MEDIUM.
- P0 closed after the corrected contract reached a fresh independent exact-CURRENT
  0 BLOCKER / 0 HIGH / 0 MEDIUM re-review. P1 was subsequently authorized and is now
  closed; at that P1-closure checkpoint P2-P6 were still separately unauthorized.

### P1 - neutral viewport output handoff contract

- Complete the shared Texture-output terminal-state/queue/readiness receipt at the
  viewport/RenderFlow boundary.
- Preserve Presentation behavior and `FlushPreviousFrameProcessing()` scheduling;
  add no Present call to frame suppliers or flush code.
- Establish a nonzero Texture output revision, per-producer-serial receipt publication
  and generation-safe Texture-viewport recreation exactly as selected by P0; do not
  add in-place texture rebind authority.
- Add focused deterministic unit/source proofs for same-queue and explicit-fence
  contracts, stale generations, output acquisition and failure/abandonment.
- Source/hygiene review + independent read-only review; execution batched to P6.
- Closed after fresh independent exact-CURRENT read-only review returned
  **0 BLOCKER / 0 HIGH / 0 MEDIUM**. The accepted source keeps receipt authority in
  `ViewportManager::RenderSlot` under `outputReceiptLock`; the one private helper only
  shares transition predicates/mutations between production and deterministic proofs.
- A user-authorized one-off compile-only exception earlier in P1 passed serialized x64
  Debug and Development builds for `rendering`, `renderingTests`,
  `geometryAllocatorTests`, `engine`, `runtime` and `editor`. That evidence predates the
  final private receipt helper and expanded focused proofs. No test executable or native
  runtime/editor smoke was run at P1 closure, and the exact-current post-helper source was
  intentionally left for the later P6 acceptance matrix, which is now recorded closed below.
- Stop.

### P2 - runtime production presentation supplier

- Closed after fresh independent exact-CURRENT read-only review returned
  **0 BLOCKER / 0 HIGH / 0 MEDIUM**. At P2 closure, P3-P6 were still separately
  unauthorized; P3 was authorized later and is recorded closed below.
- Create runtime presentation from the primary native window and actual output
  capabilities through `PresentationService`.
- Make that runtime supplier the single owner of the runtime `PresentationService`;
  register its reconciliation participant before Frame Pipeline compilation and
  tick it in main-thread `FramePhase::Presentation`, before the renderer FrameTick
  and later world-frame acquisition. Current window/surface revisions, suspension
  and swapchain replacement remain applied by the existing presentation owner.
- Own one ordinary runtime EngineViewport, consume the Ready 10.4.4 world camera,
  configure the world scene/root camera/output region and submit after the single
  process renderer FrameTick.
- Handle loading/no-camera/output unavailable/minimized with explicit no-frame or
  abandonment behavior; never begin a frame that is silently leaked.
- Keep actual Present in the retained render tail.
- Source/hygiene review + independent read-only review; execution batched to P6.
- Accepted P2 source is bounded to the runtime-private viewport service/composition and
  `runtimeViewportTests`: one ApplicationSession owner depends on FramePipeline,
  Rendering, Window and WorldSession; registers Presentation reconcile after
  RenderingUpdate and world-frame supply after RenderingFrameTick; declares the locked
  B8G8R8A8UNorm/FloatingPoint/sample-1 renderer output signature before RenderingService
  startup; and validates actual Ready B8G8R8A8UNorm/Srgb presentation state before
  admission. P4 still owns DisplaySrgb resolve/encoding rather than this gate.
- Focused authored proofs cover participant identities/order, required-service topology,
  SDR signature/active-output match, AwaitingSurface retry classification,
  Ready/Suspended/occluded/invisible/no-output admission, and the shared production
  BeginFrame -> ConfigureViews -> SubmitFrame path including exactly one Abandon after a
  post-Begin rejection and no supplier-side Present. These proofs are source-only here:
  no P2 build, test executable or native runtime launch was performed at that checkpoint;
  broad execution was deferred to the later P6 gate, now recorded closed below.
- Stop.

### P3 - editor scene texture supplier and UI consumer handoff

- Closed after fresh independent exact-CURRENT read-only review returned
  **0 BLOCKER / 0 HIGH / 0 MEDIUM**. At P3 closure, P4-P6 were still separately
  unauthorized; P4 was authorized later and is recorded closed below.
- Produce editor world views into Texture RenderViewports using the same
  EngineViewport/FrameRenderer path as runtime.
- Register the current scene-output generation through existing Editor UI texture
  ownership, and make the UI import consume the producer state/queue/readiness
  contract without a CPU join.
- Keep the generation captured by a retained UI frame immutable through that
  frame's EndFrame submission. Resolve the selected current-frame producer receipt
  at the UI RenderFlow import/execution boundary rather than capturing readiness
  before the scene producer runs; do not add a previous-ready ring.
- Keep the editor UI native-window presentation path unchanged; scene rendering is
  an input to ImGui, not a second swapchain/presentation path.
- Prove stale handle, resize/replacement generation and in-flight consumer safety.
- Do not absorb E3.3 docking/focus policy.
- Source/hygiene review + independent read-only review; execution batched to P6.
- Accepted P3 source adds one editor-private scene-output service and one Render-phase
  participant after `RenderingFrameTickParticipantId`. A demanded extent creates an
  R8G8B8A8UNorm Texture `RenderViewport` plus ordinary `EngineViewport`; scene frames
  use `Shaded` / `Normal`, `present=false`, and the existing Begin/Configure/Submit /
  one-Abandon path. Editor composition supplies the matching
  R8G8B8A8UNorm/FloatingPoint/sample-1 output attachment signature before
  `RenderingService` startup.
- The editor texture handoff retains immutable texture/sampler/output-generation
  metadata. Late `EditorUiRenderer::RegisterImports` resolves the exact P1 receipt
  `{RenderViewportHandle, outputRevision, producerSerial}`, requires Ready with
  `ShaderResourceGraphics` / Graphics / `SameQueueContinuation`, and adds no CPU wait
  or editor fence registry. Ordinary editor textures retain their existing import path.
- Replacement is bounded to two scene-output records. Candidate generations prove one
  accepted producer before promotion; active-to-retiring cutover immediately revokes
  the old editor texture handle while keeping its generation/RenderViewport/receipt
  owner alive through the retained UI tail. Release occurs only after a later UI frame,
  whose `EditorUiRenderer::BeginFrame` has already joined the previous retained CPU
  dispatch tail. Failed active producers enter the same retirement path before
  no-demand/session/camera early-outs; absent or stale editor camera selection remains
  a conservative no-frame state.
- Focused `editorSceneOutputTests` proofs are authored for contracts, two-generation
  policy/coalescing, cutover/release, failed-active/no-demand/camera behavior, exact late
  receipt rejection, ordinary scene frame terminal balance and service topology. No P3
  Premake regeneration, compilation, test executable, cooker run or native editor launch
  was performed at P3 closure; broad execution was deferred to the later P6 gate.
- Stop.

### P4 - output capability, encoding and transient-state behavior

- Closed after fresh independent exact-CURRENT read-only review returned
  **0 BLOCKER / 0 HIGH / 0 MEDIUM**. At that P4 checkpoint P5-P6 were separately
  unauthorized; both were authorized and closed later as recorded below.
- Finish minimal SDR resolve/color-space selection for runtime presentation and
  editor scene sampling from exact output/texture formats.
- Cover actual extent changes, occlusion, minimization/suspension, surface revision,
  output loss/recreation, temporal-history/camera-cut behavior for extent/output-
  generation changes according to existing renderer policy, and explicit
  loading/empty frames. P0 decides whether an actual camera cut is required; output
  replacement alone does not create camera-selection authority.
- Ensure no resize/rebind mutates live output identity while producer/consumer work
  is in flight.
- Keep HDR/exposure and broad image-quality work outside this gate.
- Source/hygiene review + independent read-only review; execution batched to P6.
- Accepted P4 resolve behavior keeps graph topology/cache selection unchanged. Both
  scene-linear and display-sRGB resolve pipelines are prepared; the existing camera
  output node selects at planning/execution from the immutable viewport encoding plus
  the actual acquired target texture format. `DisplaySrgb` is supported only for the
  owned non-sRGB `R8G8B8A8UNorm` editor target and `B8G8R8A8UNorm` runtime target;
  HDR/float, already-sRGB and unknown encoding/format pairs fail closed. Scene-linear
  Texture output retains the generic raw-copy path, while camera-scene Presentation with
  no explicit supported display transfer fails closed instead of silently presenting
  linear bytes.
- `ResolveCameraOutputDisplaySrgb` clamps scene-linear RGB to [0,1] and applies the
  standard sRGB transfer before writing those UNorm targets. The scene-linear copy is
  now a resolve-specific entrypoint too, so the existing GBuffer visualization shader
  remains semantically separate. No tone mapping, exposure, HDR10 or scRGB transfer
  policy was added.
- Explicit SDR `PresentationService` outputs mark their viewport `DisplaySrgb`. Native
  reconciliation first applies the existing output/surface revision transaction and then
  synchronizes render extent to the accepted attachment pixel extent through
  `RequestRenderExtent`. Surface replacement records pending retired-native ownership;
  that pending condition survives suspension plus native-surface resolution/conversion and
  required-HDR prerequisite failures. Suspended/unrecreatable outputs take no device-wide
  retirement wait; after prerequisites pass, the rare drain runs immediately before the
  first replacement `CreateSwapChainWithBackBuffer` attempt. Active format is invalidated
  whenever no live swapchain remains.
  Minimized/suspended/occluded/loading behavior otherwise stays in the existing owners.
- Editor output resize remains P3 generation recreation, never in-place RHI rebinding.
  Existing camera history already cuts when the effective render extent changes; P4 adds
  no `RequestCameraCut`/forced-cut authority, so same-extent surface/output replacement
  does not invent camera policy.
- Focused source proofs were added to `render_graph_tests.cpp` and `viewport_tests.cpp`
  for resolve selection/rejection, neutral output-encoding validation and the existing
  joined render-extent mutation seam. No P4 Premake regeneration, compilation, shader or
  bootstrap cook, executable test, runtime/editor native launch or broad matrix was run at
  P4 closure; execution was deferred to P6 and the larger lifecycle/order matrix to P5.
- Stop.

### P5 - deterministic integration proof matrix

- Closed after fresh independent exact-CURRENT runtime/rendering and editor read-only
  reviews both returned **0 BLOCKER / 0 HIGH / 0 MEDIUM**. At that P5 checkpoint P6 was
  separately unauthorized; it was authorized and closed later as recorded below.
- Author runtime and editor production-path proofs for: ready camera; pending/
  missing camera; one and multiple views/regions; output unavailable; minimized;
  resize/replacement; stale generation; same-queue readiness; explicit fence path
  where applicable; submit/abandon balance; presentation failure; device loss;
  editor UI consumption and native-window Present ownership.
- Prove one process FrameTick/global GPU Scene update regardless of viewport count.
- Prove `FlushPreviousFrameProcessing()` contains no Present and is not used as GPU
  readiness for the editor texture.
- Accepted source proofs use production-shared decision seams rather than a parallel
  test owner: runtime world/session/camera/output admission; Presentation retired-surface
  transition state; retained-output terminal action; and the editor's exact
  `{viewport, outputRevision, producerSerial}` receipt query.
- `viewport_tests.cpp` now retains a two-root/two-region frame and submits two
  independent RenderViewport outputs under one RenderCommandSystem FrameTick. The
  process FrameTick count remains one while both outputs receive one frame; production
  `FrameRenderer::FrameTick` remains the sole GPU Scene `Publish` owner.
- `runtimeViewportTests` covers Running/non-running world state, None/Pending/Ready/
  Failed/stale camera binding, Ready/AwaitingSurface/Suspended/Failed/occluded/
  invisible output admission, OutputUnavailable/Busy Begin, submit/abandon balance,
  retry/terminal presentation failures, and the exact P4 retired-swapchain sequence.
- `editorSceneOutputTests` covers demand/session/camera admission, one selected root,
  exact late receipt lookup and Pending/Ready/Failed/stale identity behavior,
  SameQueue/ExplicitFence import mapping, generation coalescing, failed-active
  retirement and the resize race where replacement becomes Ready after UI capture.
  The old authoring handle is revoked while the already-captured publication remains
  valid until the later UI BeginFrame CPU-tail boundary.
- `render_graph_tests.cpp` proves retained output finalization: device loss owns
  failure, graph/allocator/terminal-node failure abandons, failed Texture never
  publishes, successful Texture completes its exact publication, and successful
  Presentation alone selects Present. Existing `geometry_allocator_tests.cpp`
  remains the authoritative receipt/export/device-loss proof rather than adding a
  second receipt state machine.
- Source inspection confirms the only native `rhi::Present` remains under
  `ViewportManager::Present`; `FlushPreviousFrameProcessing()` only joins/clears the
  CPU process-frame tail and provides no GPU readiness evidence.
- Keep fixture data in scratch/build images.
- The matrix was authored but **not compiled or executed** in P5. No Premake
  regeneration, shader/bootstrap cook, executable test or runtime/editor native
  launch was performed at P5 closure; execution was deferred to the later P6 gate.
- Stop.

### P6 - batched validation and closure

- Closed on 2026-09-25 after fresh Premake generation/audit and serialized x64 Debug
  and Development focused builds. The executed matrix passes in both configurations:
  `geometryAllocatorTests --viewport-output-proof`, `renderingTests`,
  `runtimeViewportTests` and `editorSceneOutputTests` all exit 0. The focused geometry
  mode includes the real-D3D12 no-op transition-boundary regression added for the P6
  RenderFlow correction; the unrelated full geometry suite is not claimed.
- `ExecuteActions` now commits pending RHI barriers after each successful nonempty
  native compiled action slice. This fixes the exact GeometryWork state-tracking
  failure without adding a CPU/GPU wait or weakening exact-before validation. Temporary
  localization `fprintf` diagnostics were removed; fresh Premake audit is clean.
- Exact-current `bootstrapImage` was rebuilt and executed in Debug and Development to
  fresh scratch roots. Both runs validated the P4 fixture and reported
  `catalog=Success entries=18 resources=47`. Runtime `--validate-world-frame` native
  smokes also exit 0 in both configurations.
- The standard bootstrap package now advances past the historical GeometryWork failure
  but still reaches the known `LightComponent` terminal teardown assertion owned by
  G6/10.4.6. That nonzero process is diagnostic evidence only and is not counted as a
  passing P6 editor native smoke.
- A Tests/Editor-only `editorSceneOutputPackageFixture` derives a camera-only package
  from the standard package while preserving package metadata, the authored startup
  camera, all 47 resource identities/dependencies and unchanged segment policy. Its
  corrected transaction writes and validates `DATA000.vpak.tmp` before publishing the
  final file. Post-correction Debug and Development fixture runs pass with 47 resources
  and no surviving temp; waited `--scene-output-path` editor runs against those derived
  packages exit 0 in both configurations. The generic noninteractive native-UI lifecycle
  driver also exits 0 with `lifecycle=PASS` in Debug and Development.
- Scoped hygiene/ownership/shutdown checks pass: `git diff --check` has no error; the
  only native `rhi::Present` remains `ViewportManager::Present`;
  `FlushPreviousFrameProcessing()` only joins/clears the CPU process-frame tail; no
  runtime/editor `RequestCameraCut` caller was added; runtime destroys its
  `EngineViewport` before its Presentation output; editor cutover revokes the old UI
  texture handle before retaining the physical generation through the later UI
  `BeginFrame` boundary; and P4 pending surface retirement still survives blocked
  prerequisites and drains only immediately before replacement swapchain creation.
- The already-completed exact-CURRENT RED comparison remains authoritative under the
  user-approved rule: the later RenderFlow change is execution-state correctness only
  and the camera-only package support is test-only, so neither changes production
  ownership architecture. Final independent exact-current P6 review returned
  **0 BLOCKER / 0 HIGH / 0 MEDIUM**. **P6 is closed; stop before 10.4.6.**

## Definition acceptance

The definition is ready for independent review when it unambiguously establishes:

- Presentation / Texture / Headless as lifecycle kinds and frame purpose as a
  separate concern;
- actual Present remains in asynchronous retained-frame output completion while the
  previous-frame CPU flush remains a dispatch boundary only;
- runtime and editor scene suppliers run after the single process FrameTick;
- editor scene output is consumed through the existing UI texture path, not another
  presentation path;
- producer state/queue/readiness is explicit and never replaced by a CPU wait;
- resize/replacement is generation-safe; and
- 10.4.6/10.4.7 ownership remains outside the phase.

No production implementation, Premake generation, compilation, executable test or
native launch is authorized by this definition alone.
