# 10.4.7 real cooked and project-derived execution proof

## Status and authorization

The user explicitly authorized **Definition only** on 2026-09-28, then explicitly
authorized **P0** on 2026-09-29. Definition is **CLOSED by explicit user progression**:
the planned independent reviewer could not be launched because retained worker
configuration referenced a stale model id, and no independent verdict is claimed.
P0 is **CLOSED** on exact-current source/static evidence and docs-only hygiene. The
fresh independent P0 reviewer could not be launched because the retained worker default
still referenced stale model id `5.6`; the prime read-only acceptance pass is
0 BLOCKER / 0 HIGH / 0 MEDIUM and no independent P0 verdict is claimed. The user
explicitly authorized **P1** on 2026-09-29. P1 is **CLOSED** on coherent production
source plus exact-current source/static review at 0 BLOCKER / 0 HIGH / 0 MEDIUM. A fresh
independent P1 reviewer could not be launched because the retained worker default still
references stale model id `5.6`; no independent P1 verdict is claimed. The user
explicitly authorized **P2** on 2026-09-29. P2 is **CLOSED** on coherent production
source plus an exact-current source/static acceptance pass at 0 BLOCKER / 0 HIGH /
0 MEDIUM. The user explicitly authorized **P3** on 2026-09-29. P3 is **CLOSED** on
focused real Development/D3D12 runtime/editor execution, exact-current focused builds,
clean normal shutdown, and a final independent implementation/evidence review at
0 BLOCKER / 0 HIGH / 0 MEDIUM. **10.4.7 is CLOSED.**

10.4.1-10.4.6 are closed. This phase is the final execution/acceptance slice proposed
by the world/rendering integration study; it does not reopen their ownership or
lifecycle designs.

Root `AGENTS.md` is authoritative for validation. In particular, ordinary and
intermediate work does **not** add/build/run tests by default. The final authorized
gate must leave the relevant source compiling and run only the smallest focused proof
needed for closure. Historical matrices in earlier studies are context, not permission
for broad regression, multi-configuration, native/UI or performance suites.

## Objective

Prove the already-built production rendering chain with real content and real output:

1. a normal cooked package starts through the ordinary runtime path and renders a
   visible static surface through world/cell/component materialization, RenderScene,
   GPU Scene, visibility/binning/indirect drawing, the render graph and D3D12
   presentation;
2. the editor consumes a visibly relevant project-derived resource through the
   existing `ProjectAssetPipeline` -> committed loose publication ->
   `ResourceStreamer` path, renders the same world through the existing editor scene
   texture producer, and displays that texture through one production scene panel;
3. both paths shut down through the already-closed 10.4.6 ownership/drain contract;
   and
4. one bounded, repeatable Development/D3D12 workload records enough existing
   geometry and timing evidence to describe actual CPU/GPU work without inventing a
   performance claim from source inspection.

The phase is an integration/observability closure slice, not a new renderer or a new
asset system.

## Exact first-pixel proof

The baseline cooked image remains intentionally small and visually unmistakable:

- one static quad, 3 x 2 world units, centered at `z = 5`;
- runtime uses the normal gameplay camera selected by the cooked world;
- editor uses the one fixed live editor camera owned by the 10.4.7 scene-view
  controller, through the existing `WorldSessionService` live-camera binding;
- one warm-white directional light;
- opaque/depth techniques through the existing static-surface path; and
- the bootstrap material obtains visible base color from one ordinary sampled VTEX.
  The packaged fallback keeps the historical uniform burnt-orange/red appearance
  (`linear RGBA ~= {0.8, 0.15, 0.05, 1.0}`), while the project source is deliberately
  uniform cool cyan/teal so source replacement is obvious without a pixel-exact oracle.

The fixed editor proof camera deliberately mirrors the already-working cooked bootstrap
camera instead of inventing a new look-at convention. Its camera state is:

- world/local position `{0, 0, 0}` and identity orientation quaternion `{0, 0, 0, 1}`;
- perspective projection, vertical FOV `1.0471975512` radians (60 degrees), near
  `0.1`, far `100.0`, reverse depth enabled, finite far plane, zero projection offset,
  and aspect inherited from the frame;
- `RenderCameraResolutionMode::InheritFrame`, `RenderViewPurpose::Main`, ordinary
  `Primary | OcclusionCulling` flags, all layer/visibility bits, zero LOD bias and the
  normal renderer temporal-history/jitter defaults; and
- authoritative phases from `RenderingService::GetDefaultCameraPhases()` and normal
  on-demand camera policy.

This is exact-current rather than guessed: the cooked bootstrap camera component is on
the identity/root transform, uses the same 60-degree/0.1 projection with far `100.0`,
and its quad vertices are already authored at positive view-space `z = 5`.

The important oracle is not artistic complexity. The visible result must have reached
the output through the ordinary chain:

```text
normal source
  -> BuildSystem / package or committed project loose publication
  -> ResourceStreamer / ResourcePipeline
  -> world -> cell -> prefab -> concrete component
  -> RenderingRuntime -> RenderScene -> GPU Scene
  -> spatial candidates -> GPU visibility/geometry work -> indirect draw
  -> render graph -> D3D12
  -> runtime presentation OR editor texture -> editor UI presentation
```

`--validate-bootstrap`, an isolated RHI draw, a direct texture upload, a test-only
camera injection or a synthetic renderer bypass does not satisfy this proof.

The final image oracle is semantic and bounded. In the same production Scene panel and
without changing the material or logical VTEX `ResourceReference`, an interior quad
region must first read visually warm from the packaged fallback, then read visibly cool
after the committed project loose generation becomes resident. The quad silhouette and
placement must remain stable and the background must remain visually distinct. The
proof may inspect a tiny number of interior image/pixel observations to establish that
warm-to-cool transition, but it must not introduce screenshot matching, tolerance maps,
or a general image-analysis framework.

## Exact-current facts the Definition locks

### Runtime is already a production execution path

- Normal `RuntimeApplication` startup mounts the DATA package set, resolves the
  packaged renderer bootstrap and starts the configured world session.
- `RuntimeViewportService` submits a shaded normal-purpose `EngineViewport` frame only
  for a Running session with a valid selected camera and drawable presentation output.
- Runtime presentation stays on the existing `PresentationService` /
  `RenderViewportOutputKind::Presentation` path and the retained render tail owns the
  terminal Present.
- 10.4.6 already owns stop/replacement/final drain. 10.4.7 must observe that contract,
  not add a second shutdown or GPU-retirement path.

Therefore the runtime side of 10.4.7 should be dominated by real execution and small
fixture/observability corrections, not application architecture work.

### The cooked fixture has already absorbed earlier G7 correctness fixes

The integration study's original G7 text predates 10.4.3-10.4.6. Exact-current
`bootstrapImage` now:

- derives depth comparison from the renderer camera's reverse-depth convention;
- calculates mesh and placement bounds from the actual quad at `z = 5`;
- carries an explicit startup-camera locator;
- builds renderer feature programs through `ShaderAssetCompiler`; and
- feeds generated-resource content fingerprints through the existing
  `DependencyIndex` when resources depend on other generated artifacts.

P0 re-audited recipe/template/input freshness on the exact-current cooker before
changing anything and confirmed the historical stale-DDC finding is not still live.

### Project-derived publication already has an owner

- E2 is closed and `ProjectAssetPipeline` owns project build/reconciliation,
  committed derived state and loose publication.
- `ProjectLoosePublisher` publishes committed artifact generations into the existing
  `ResourceStreamer`; its default priority is 100, while source selection already
  compares loose and package priorities/generations.
- The editor `ProjectWorkspaceService` composes that shared pipeline with the live
  `ResourceStreamer`.
- Exact-current editor composition currently registers only
  `TextureAssetCompiler` + its importer with the workspace pipeline. This is a useful
  boundary, not a reason for 10.4.7 to create another asset backend.
- E2.4 already owns overlapping VTEX generations and resident replacement: a newly
  current texture can retain the same stable GPU residency identity while the old GPU
  state retires behind the existing fences.
- Exact-current `ProjectWorkspaceService` does **not** currently drive either half of
  live source convergence: production editor code has no owner calling
  `EnableObservation()` / `TickObservation()`, while
  `ProjectAssetAutomaticRecookConfig` defaults disabled and no production owner calls
  `EnableAutomaticRecook()`, `TickAutomaticRecook()` or `RequestAssetReimport()`.
  Activity UI currently observes pipeline status only.

P0 confirms the project-derived proof is a **visibly consumed texture** built and
published by the editor's already-supported texture pipeline while the world, cell,
prefab and other unsupported authoring types remain supplied by the normal package.
The source may be one ordinary image accepted by `TextureAssetCompiler`'s existing
direct importer; no compiler-catalog expansion is required.

The exact runtime identity is deliberate. Project output references are
`assets/<32-hex AssetId>/<output-key>`, so 10.4.7 locks one fixed project texture
`AssetId`, output key `source`, and VTEX type. `bootstrapImage` supplies the
packaged fallback at that **same typed ResourceReference**. The committed loose
publication then competes at the same identity with the existing default priority
100, and E2.4's existing texture-residency generation replacement changes the visible
resident without material reload or a renderer special case.

The smallest missing production drive is the existing E2 observation + recook pair,
owned by `ProjectWorkspaceService`. The exact P2 composition is one main-thread
workspace participant in `FramePhase::WorldStreaming`, with a required
`FramePipelineService` dependency so it is registered during service initialization
before the frame pipeline compiles. `OnStart()` enables source observation only. The
first observation session deliberately performs its existing authoritative catch-up;
`EnableAutomaticRecook()` is retried by the owner participant only after that catch-up
can release its admission (the API correctly returns `Busy` while reconciliation owns
it). Enabling automatic recook then queues its existing authoritative startup-recovery
pass, so delaying policy enable until catch-up cannot lose dirty current truth.

On each admitted workspace frame the exact-current owner order is `TickObservation()`,
then enable/tick automatic recook when its policy is available, then bounded resident
delivery/cutover for newly installed VTEX publications through the existing
`ResourceStreamer` + `TextureResidencyRuntime`, then the existing resident-retirement
pump. P3 real execution exposed that committed loose publication alone does not replace
an already-resident texture, so the resident-delivery step was added at that existing E2
lifecycle boundary rather than by creating a second loader/streamer/residency path.
`Pending`/ordinary coordination `Busy` states remain bounded retry states; a sticky
cook/recovery failure remains pipeline-owned and visible through the existing Project
Asset Activity rather than turning an authored asset failure into a renderer/frame-
pipeline crash. Lifecycle/invariant failures remain explicit failures.

Quiesce closes this owner-frame admission and retains the existing bounded resident-
retirement pump; it does **not** invent a parallel disable/drain loop. In particular,
`DisableAutomaticRecook()` and `DisableObservation()` can legitimately return `Busy`
while accepted work or reconciliation owns admission. `OnShutdown()` therefore keeps
the established single teardown authority: `ProjectAssetPipeline::Shutdown()` stops
and joins observation first, drains observation/automatic work and settles committed
publication as required, then the workspace releases the texture compiler.
`ProjectAssetPipeline::Shutdown()` already stops/joins observation and drains
automatic work before its graph/index owners are destroyed. No proof-only watcher,
cooker loop, second scheduler or DerivedData scanner is allowed.

This phase does **not** claim full source authoring for worlds/prefabs/cells, FBX import,
`.vmeta` editing, or automatic recooking of every render resource type.

### Material texture sampling has one narrow production gap

The material IR/generator already supports a D2 texture parameter, sampler parameter
and `TextureSample(texture, sampler, uv)`, and the production material resolver
already has the VTEX provider/residency path. P0 found one smaller missing seam:
`MaterialOfflineContract` has no valid sampler asset type and production registers no
sampler provider/fallback, while material materialization rejects roles whose expected
asset type is invalid.

P1 therefore may add exactly one general renderer-owned **standard filtering sampler**
material role:

- give `ResourceParameterKind::Sampler` one valid renderer/material resource type in
  the offline contract;
- register one provider-created read-only sampler fallback through the existing
  `MaterialResourceResolver` provider/fallback API;
- keep the material's sampler resource value unset/Soft so no sampler source asset,
  compiler, streaming backend or second registry is invented; and
- use that role only through the same shader reflection/bindless sampler-descriptor
  path as every other material resource.

The proof texture itself is Required and typed VTEX. Its package fallback and project
loose generation share the fixed ResourceReference above.

### Editor scene production exists; the production panel consumer does not

- `EditorSceneOutputService` already owns bounded texture-output generations,
  `EngineViewport` submission, exact producer receipts, UI texture registration and
  next-UI-frame retirement.
- It produces only while there is explicit output demand, a Running world session and
  a valid camera.
- The UI renderer already imports registered editor textures through the normal graph
  and keeps native-window presentation owned by the editor UI path.
- Exact-current production code has no scene panel that requests
  `EditorSceneOutputService::RequestOutputExtent()` and draws its published
  `EditorTextureHandle`; current callers are test support only.
- Exact-current `EditorApplication` deliberately starts its world session with
  `WorldSessionCameraSource::None`. No production editor owner currently creates/
  selects a live editor camera, so scene-output demand alone still cannot produce a
  frame. The existing integration support proves the service with the authored world
  camera, but that is not the intended long-term editor-navigation owner.

10.4.7 may therefore add the **smallest production scene-view controller needed to
close both missing ends**: one bounded editor-owned live `RenderCameraHandle` with a
fixed initial pose/projection selected through the existing `WorldSessionService`
live-camera API, plus one scene panel that requests extent and consumes the existing
scene texture. This is not a second camera system. Interactive camera navigation,
picking, gizmos, hierarchy/selection, inspector behavior and general viewport tooling
remain later editor work.

P0 selected the narrow production owner and exact camera lifetime relative to the world
scene/session. The design does not add a generic service locator to extension activation
merely to reach scene output. The controller may own the one editor camera identity it
creates; neither it nor the panel may own RenderScene, camera storage, render graphs,
swapchains or presentation.

P0 locks that owner as one typed `EditorSceneViewService`-style controller composed
with existing editor services. It depends on `EditorUiService`,
`EditorSceneOutputService`, `RenderingService`, `WorldSessionService` and
`GameWorldService`; the active world scene is obtained from the current
`GameWorldService` rendering runtime. The shell/extension activation surface may
expose only a narrow typed scene-view accessor for the built-in Scene panel, not a
generic service locator.

Panel demand is a per-UI-frame lease. The panel authors during
`EditorUiFrameParticipantId`, records its current content extent through the
scene-view controller and draws the last published `EditorTextureHandle` through the
existing ImGui texture conversion. A small controller participant immediately after
the editor UI participant reconciles that lease before the later Render phase: if the
Scene panel was not authored in the current UI frame because it is closed/hidden, it
clears scene-output demand that same frame. The panel itself owns no render state.

The camera descriptor must use the renderer's authoritative
`RenderingService::GetDefaultCameraPhases()` rather than hard-coded compact phase IDs.
More importantly, the scene-view controller must relinquish its borrowed world-session
binding and caller-owned camera **before normal session stop/replacement begins**:

1. stop/clear scene-output demand so no new prepared scene frame is admitted;
2. while the session is still Running, clear the owned live-camera selection token;
3. unregister through `RenderingService::GetCommands().UnregisterCamera()`; this
   existing command-system boundary performs `FlushPreviousFrameProcessing()`
   internally before touching `RenderCameraStorage`, so the already-prepared CPU
   view-family tail is joined without adding another fence or forcing GPU idle; and
4. only after successful unregister permit `WorldSessionService::RequestStop()` or
   replacement to begin.

This ordering is mandatory because `RenderCameraStorage::CanDetachScene()` refuses to
detach a scene with active cameras, while 10.4.6 correctly revokes the session binding
without owning or unregistering a caller-created live camera.

## RED alignment

Exact-current local RED evidence establishes the sequencing to preserve:

- `common/resource/src/resourceGameDepot.cpp:259` resolves normal cooked resources
  through the existing depot/archive owner rather than a proof-specific loader.
- `common/gameFramework/src/gameEngine.cpp:839-895` begins the game viewport frame,
  flushes previous render-command CPU processing, gathers scenes, performs one
  renderer `FrameTick`, then submits the viewport frame.
- `common/engine/src/renderEngineViewport.cpp:387-445` keeps frame construction and
  submission on the existing viewport owner; Present intent is part of frame setup.
- `backend/backendEngine/src/renderingWindow.cpp:98-201` has the editor prepare its
  camera and issue it into the ordinary scene/frame path rather than owning another
  renderer.
- `backend/backendEngine/src/renderingWindowManager.cpp:93-129` prepares editor
  windows, performs one shared renderer update, then submits their viewports.

Vanguard intentionally differs through managed services, generation handles,
GPU-driven bindless geometry, package/loose resource sources and ImGui texture
composition. 10.4.7 preserves the same ownership direction: application/editor
supplies source/camera/output intent; the existing renderer owns rendering.

## Observability and performance contract

### Reuse before adding

The renderer already exposes bounded geometry diagnostics with per-view:

- candidate count and unresolved GPU identities;
- planned shells and CPU pipeline binds;
- visible/work/instance/argument capacities;
- GPU visibility and geometry counters;
- emitted indirect arguments, overflow count and nonempty shells.

RHI also already contains `GpuCounterSystem`, timestamp calibration/query support and
nonblocking collection. The frame pipeline already exposes CPU phase/participant
execution times, so no new CPU profiler is needed. P0 confirms one narrow GPU timing
seam remains: production rendering does not currently drive `GpuCounterSystem`.

P2 may therefore add only an opt-in, bounded FrameRenderer-owned timing bridge using
the existing graphics-queue counter system. The measured frame writes scopes around
the already-authored `GeometryWork` command-list group and `CameraGeometry` draw
group. `RenderNodeImplContext::GetCommandScope()` identifies the exact graph command
scope, and `RenderFrameCommandLists` already retains the matching
`CommandScopeExecutionReceipt` with the actual submission fence. The counter frame is
committed from that receipt and collected nonblocking; no extra fence registry, GPU
wait, telemetry service or benchmark scheduler is permitted.

### What 10.4.7 may claim

The final phase records one repeatable **Development x64 / D3D12** measurement with
the exact GPU/driver/workload identified. It separates:

- CPU preparation/recording observations;
- GPU visibility/binning/draw observations; and
- correctness/image evidence.

There is no invented universal millisecond target. Success means the measured path
is real, bounded and structurally proportional to the configured views/candidates/
active shells, with no overflow or newly discovered capacity-wide/per-item serialized
path that invalidates the closed architecture. Numbers are reported as measurements,
not generalized performance guarantees.

The final workload is deliberately the existing one-quad baseline, not a synthetic
multi-object benchmark. Expanding it would require unrelated prefab/content work and
would add little confidence to this integration proof. P3 measures one steady-state
Development/D3D12 editor scene-output frame after convergence and one bounded
project-texture replacement from the packaged burnt-orange/red fallback to a visibly
different project texture. It records the exact machine/GPU/driver/backend, frame
pipeline CPU timings, geometry diagnostics, the two bounded GPU scopes above and the
semantic before/after visible result. No second view, stress/soak loop or universal
performance target is part of 10.4.7.

## Ownership locks

1. `ResourceStreamer` remains the only runtime resource-source selector/loader.
2. `ProjectAssetPipeline` + `ProjectLoosePublisher` remain the editor project-derived
   observation/build/publication owners; `ProjectWorkspaceService` may only drive
   their existing observation/automatic-recook/tick surfaces. No second watcher,
   build loop, DDC scanner or guessed DerivedData filename.
3. `WorldSessionService` / `GameWorldService` remain world lifecycle owners.
4. `RenderingService`, one `RenderScene` per world, shared GPU Scene, existing
   residency owners, `FrameRenderer`, RenderFlow and RHI remain the only renderer path.
5. Runtime presentation remains `PresentationService` + presentation RenderViewport.
6. Editor scene output remains `EditorSceneOutputService` + texture RenderViewport;
   the panel only requests extent and consumes the published `EditorTextureHandle`.
7. A 10.4.7 editor scene-view controller may own one live editor camera handle and its
   WorldSession selection token, but `RenderCameraStorage` remains renderer authority.
8. Editor UI remains sole owner of editor native-window presentation and Present.
9. 10.4.6 remains sole stop/replacement/terminal-drain contract.
10. No production source consumes test/support code or proof-only data paths.

## Explicit non-goals

- No second renderer, RenderScene, GPU Scene, scheduler, frame pipeline, resource
  registry, asset database, DDC, retirement manager, profiler or presentation path.
- No direct-draw fallback, CPU visible-object renderer or readback-driven draw path.
- No Vulkan runtime claim; offline SPIR-V compilation is not Vulkan execution.
- No PBR/HDR/exposure/tone-mapping/shadows/GI/reflections/post-processing expansion.
- No transparency/skinning/particles/decal-rendering feature expansion.
- No full editor viewport tool: no free camera controller, picking, gizmos, hierarchy,
  inspector, selection model or document editing.
- No full project authoring/import proof for world/prefab/cell/material/mesh unless a
  separately authorized asset phase supplies those source compilers.
- No pixel-perfect image-comparison framework. A tiny number of semantic pixel/image
  observations may be used at the final gate only when they are the smallest credible
  proof of the real rendered output.
- No broad Debug/Development/Profile/Shipping matrix, long soak, exhaustive native UI
  automation or unrelated regression sweep under this phase's default authorization.

## Gates

### Definition - execution contract

**CLOSED.** Documentation and source/RED inspection only. The independent review was
not obtained because reviewer infrastructure failed before chat creation; the user
explicitly advanced the work to P0 on 2026-09-29, superseding that pending closure
item. No independent 0B/0H/0M verdict is claimed.

- Lock the actual visible result and end-to-end ownership chain.
- Reconcile stale integration-study assumptions with 10.4.1-10.4.6 and closed E2.
- Identify the missing production scene-panel consumer and the current texture-only
  editor project compiler boundary.
- Lock proportional validation and the final focused proof before implementation.
- The original intended closure item was an independent exact-current docs/source
  review at 0 BLOCKER / 0 HIGH / 0 MEDIUM. It was not obtained; the user's explicit
  progression to P0 superseded that pending item, as recorded above.
- No Premake generation, compile, test, cooker, runtime/editor launch or native
  automation in Definition.

### P0 - exact-current gap and evidence lock

**CLOSED on 2026-09-29.** Read-only/source-static only. Fresh prime acceptance review:
**0 BLOCKER / 0 HIGH / 0 MEDIUM**. The attempted independent review failed before chat
creation because retained worker configuration still used stale model id `5.6`; no
independent P0 verdict is claimed. Scoped `git diff --check`, trailing-whitespace and
authorization-state hygiene are clean. No Premake/build/test/cooker/runtime/native
execution was performed.

- Re-trace exact runtime and editor composition after concurrent work has settled.
- Re-audit bootstrap build fingerprints and shader/include/generated-resource
  dependencies; classify each old G7 item as already closed or genuinely remaining.
- Lock the exact visible project-derived resource. Prefer one texture because it is
  already supported by production `ProjectWorkspaceService`; do not expand compiler
  breadth without a demonstrated need.
- Lock package/loose priority and replacement behavior for that exact resource.
- Lock the smallest production owner that actually drives the existing E2 project
  pipeline for this resource, including source observation before automatic-recook,
  activation/quiesce/shutdown ordering and failure reporting.
- Lock the smallest production scene-panel owner and dependency route without a
  generic service locator or test seam.
- Lock the minimal editor live-camera owner, fixed initial pose/projection and
  authoritative renderer phase set, plus create/select/demand-stop/clear/
  command-system-unregister ordering **before** world/session stop or replacement
  admission.
- If the visible texture is selected, lock the exact material IR texture/sampler role,
  resource identity/dependency edges and packaged fallback so a loose VTEX replacement
  changes visible pixels without reloading or special-casing the renderer.
- Inventory existing geometry diagnostics/RHI counters and specify only genuinely
  missing runtime timing fields.
- Lock the one Development/D3D12 final workload and exact semantic image oracle.
- Close on coherent source evidence + read-only review; no build/test/native run.

### P1 - cooked runtime and visible-resource implementation

**CLOSED on 2026-09-29.** Production implementation only; no broad validation. The
prime exact-current source/static acceptance pass is 0 BLOCKER / 0 HIGH / 0 MEDIUM.
The independent reviewer launcher still resolves its retained default to stale model id
`5.6`, so no independent P1 verdict is claimed. No Premake generation, compilation,
test, cooker, runtime/editor launch or native automation was performed. P2-P3 remain
unauthorized.

- Make only P0-proven cooker/content corrections needed for the ordinary runtime
  first-pixel path and dependency freshness.
- If P0 selected a project-derived texture, make the bootstrap material consume a
  normal texture resource and give the package an ordinary fallback artifact with an
  unmistakable baseline appearance. Do not special-case the renderer for the proof.
- Add only the P0-locked standard filtering-sampler material fallback/provider needed
  by the existing material resource resolver; do not add a sampler asset compiler or
  second descriptor/resource system.
- Add only bounded production diagnostics needed to identify source/world/component/
  geometry/output progression; prefer existing status/counter surfaces.
- Preserve all 10.4.1-10.4.6 owners and no-test intermediate policy.
- Close on coherent source + source/static review only.

Closure preserves the Required texture contract end to end: material IR now carries
the resource Required flag into generated `VanguardMaterialResource` metadata, shader
reflection therefore marks the VTEX role Required, and the VMAT/materializer/resolver
dependency remains Required at the same fixed typed resource identity. The sampler role
remains unset/Soft and uses the one renderer-owned `VSMP` provider/exact-shape fallback;
external provider and fallback registration cannot claim that renderer-owned type.

### P2 - editor scene consumer/camera and bounded measurement wiring

**CLOSED on 2026-09-29.** Production implementation is source-complete and the fresh
exact-current source/static acceptance pass is 0 BLOCKER / 0 HIGH / 0 MEDIUM. Per the
intermediate-gate rule, no Premake/build/test/cooker/runtime/editor/native execution was
performed. P3 was subsequently authorized and completed as the final gate below.

- Add the P0-locked minimal scene-view controller: create one renderer-owned camera
  record for the active world scene, select it through `WorldSessionService`'s existing
  live-camera API, use the exact P0 camera state above, clear the selection before
  unregistering the camera, and rebuild it only through normal session/world lifecycle
  after replacement.
- Connect one minimal production scene panel to `EditorSceneOutputService`: request
  extent only while visible, draw the published editor texture, and stop demand when
  hidden/closed. The panel does not own RenderScene/render graph/presentation state.
- Route the selected visible project resource through the existing
  `ProjectAssetPipeline` and committed loose publication path. The project-derived
  resource must be visibly distinguishable from the packaged fallback so source
  convergence is observable without inspecting private state.
- Add only the P0-selected production project-pipeline drive needed to build/publish
  that resource. Reuse the existing source-observation and automatic-recook surfaces
  in `ProjectWorkspaceService`: one main-thread `WorldStreaming` participant, observation
  enabled on Start, automatic policy enabled only after the initial observation catch-up
  releases admission, and the original P2 per-frame observation -> automatic-recook ->
  resident-retirement ordering. P3 real execution later inserted the missing existing-E2
  resident-delivery/cutover step between publication and retirement for already-resident
  VTEX generations. Quiesce stops owner-frame drive; pipeline Shutdown remains the sole
  observer-stop/join and automatic-drain owner. Do not create another watcher, scheduler,
  build loop, or service-local shutdown protocol.
- Before session stop/replacement, stop scene-output demand, clear the live-camera
  selection while Running, then unregister through `RenderCommandSystem` so its
  existing CPU-tail join settles prepared-family ownership before `RequestStop()`.
  No extra CPU fence and no GPU-idle wait.
- Add only the P0-proven bounded timing hookup, reusing existing RHI counters and
  geometry diagnostics; no benchmark framework.
- Close on coherent source + source/static review only.

### P3 - focused real execution and 10.4.7 closure

**CLOSED on 2026-09-29. Final gate complete.** Root `AGENTS.md` final-gate rule applied.

The smallest required evidence is:

1. leave the directly relevant production/tool source compiling with focused
   serialized Premake/VS2022 builds; do not rebuild the whole solution;
2. one normal **Development x64 / D3D12 runtime** launch from a freshly cooked image
   reaches the expected visible surface through ordinary world startup, reports no
   geometry overflow/failure, and exits through normal 10.4.6 shutdown;
3. one normal **Development x64 / D3D12 editor** project launch uses the production
   project pipeline to publish the selected loose resource, visibly distinguishes it
   from the packaged fallback in the production scene panel, and exits cleanly; and
4. record the single P0-locked bounded workload's CPU/GPU/candidate/shell/draw evidence
   from production instrumentation and report it without extrapolating beyond the
   measured machine/backend/configuration.

Do not automatically add new tests, run both configurations, execute an unrelated
regression matrix or use interactive mouse/focus automation. If a focused automated
driver is genuinely the smallest way to establish one of the four rows, it must live
under explicit test/support ownership and remain narrower than the product path it
observes. Interactive native automation still requires a warning and separate user
authorization.

A final independent exact-current implementation/evidence review must return
0 BLOCKER / 0 HIGH / 0 MEDIUM before P3 and 10.4.7 close.

#### P3 closure evidence — 2026-09-29

- Focused serialized Development x64 VS2022 builds through the Premake-generated
  projects left the directly relevant runtime/editor/tool source compiling. After the
  final resident-overlap correction, `editor.vcxproj` compiled and linked
  `VanguardEditor.exe` successfully with outer `/m:1`.
- A fresh runtime image at `build/p3-10-4-7-runtime-image-10-current` cooked successfully
  (`catalog=Success entries=18 resources=48`, `DATA000.vpak` size 280544). The ordinary
  `Vanguard.exe --render-diagnostics` Development/D3D12 path rendered the warm packaged
  quad through normal world startup with `candidates=1`, `shells=4`,
  `emittedArguments=2`, zero relevant geometry overflow/rejection fields, real
  `GeometryWork`/`CameraGeometry` GPU samples and CPU frame samples, then closed normally
  through `WM_CLOSE`.
- The isolated production editor project first rendered the packaged warm fallback. With
  the editor still running, restoring the exact project sidecar then PNG drove normal
  source observation, automatic cook, VDDC/index update and loose `.vres` publication.
  The production owner then traced one exact resident transition: Installed publication
  -> stable residency generation 6 -> ordinary `ResourceStreamer::Request` ->
  `ResourceStreamer::Replace` -> `TextureResidencyRuntime::ReplaceTexture` generation 7
  -> resident Current generation 7.
- On the final rebuilt editor binary, direct `PrintWindow` observations changed the Scene
  interior from warm `231,105,60,255` to cool `13,199,195,255`; sampled background
  remained `0,0,0,255`, and the changed pixels stayed localized to the same quad region.
  The normal temporal/raster edge varied by one pixel between the two observations, which
  is within the deliberately semantic, non-pixel-exact oracle. The editor then closed via
  `WM_CLOSE_POSTED=True` with `EDITOR_EXIT=0`.
- Final editor diagnostics remained healthy (`candidates=1`, `shells=4`,
  `emittedArguments=2`, zero relevant overflow/rejection/failure fields) with real
  `GeometryWork`, `CameraGeometry` and CPU samples. Measurements are scoped only to this
  Development/D3D12 run on an NVIDIA GeForce RTX 4080 Laptop GPU, renderer-reported
  11997 MiB dedicated VRAM, NVIDIA driver `32.0.15.8088`; no broader performance claim is
  made.
- The final independent review found one pre-fix HIGH: a newer publication could encounter
  `ReplacementBusy` while the previous GPU replacement was still uploading/installing,
  and the workspace owner treated that ordinary coordination state as a frame failure.
  The exact-current correction resets only the transient owner candidate and returns
  Pending without advancing the bounded sweep cursor; a fresh focused build and full
  editor warm-to-cool execution then passed. The independent post-fix verdict is
  **0 BLOCKER / 0 HIGH / 0 MEDIUM**.
- No broad regression matrix, dual-configuration sweep, automatic new tests or interactive
  mouse/focus automation was run; that is intentional under the final-gate proportional
  validation contract.

## Original Definition acceptance target

The Definition originally intended to close only after an independent review confirmed
that this contract:

- proves real pixels rather than another isolated subsystem;
- keeps runtime package and editor project-derived sources converged at the existing
  resource/runtime owners;
- names the editor scene-panel gap without turning 10.4.7 into a full editor viewport
  feature phase;
- does not reopen 10.4.1-10.4.6 lifecycle/render architecture;
- requires actual measured evidence for performance wording while avoiding arbitrary
  budgets or broad benchmark matrices;
- obeys the repository's proportional validation rule; and
- leaves later rendering/editor/content features explicitly outside scope.

That reviewer never launched successfully because the saved worker configuration used a
stale model id. The user explicitly advanced to P0 on 2026-09-29, so Definition is
closed by that progression and no independent Definition 0B/0H/0M verdict is claimed.
