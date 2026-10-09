# 10.4.4 camera activation and streaming-observer binding

## Status and authorization

Defined for cross-agent implementation. **P0 and P1 are closed. P1's final
independent exact-CURRENT verdict is 0 BLOCKER / 0 HIGH / 0 MEDIUM** after focused
Premake/VS2022 validation and scoped hygiene. **P2 is closed at a final independent
exact-CURRENT verdict of 0 BLOCKER / 0 HIGH / 0 MEDIUM** after implementation,
post-review provenance correction, focused validation and scoped hygiene. **P3 is
closed at an independent exact-CURRENT read-only verdict of 0 BLOCKER / 0 HIGH /
0 MEDIUM** after implementation and scoped source/hygiene review; Premake/build/
executable validation is intentionally deferred to P6. **P4 is closed at a fresh
independent exact-CURRENT read-only verdict of 0 BLOCKER / 0 HIGH / 0 MEDIUM**
after implementation and scoped source/hygiene review; its behavioral proof matrix
is authored in P5. **P5 - deterministic lifecycle and integration proof - is closed
at an independent exact-CURRENT read-only verdict of 0 BLOCKER / 0 HIGH / 0 MEDIUM.**
Its proof matrix was intentionally unexecuted at P5 closure and has now executed in
the authorized P6 batch. **P6 and 10.4.4 are closed at a final independent
exact-CURRENT verdict of 0 BLOCKER / 0 HIGH / 0 MEDIUM.** The P4 additions own
exactly one generational streaming observer from the session `BeginFrame`
participant.
10.4.5 viewport/output supply, 10.4.6 terminal draining and 10.4.7 pixel and
performance proof remain outside this contract.

10.4.3 is complete. This phase closes G4 from the
[world/rendering integration study](../studies/world-rendering-integration-study.md):
the engine has camera components and streaming-observer infrastructure, but no
production owner currently selects a materialized world camera, keeps that
selection valid across streaming, or supplies its motion to streaming.

This is a cross-agent contract. The active working triplet is
`.planning/world-rendering-10-4-4-camera-activation/`. Durable architecture and
accepted completion stay in this plan and the development progress documents.
Do not place task-specific state in `.pi/`.

## Objective

Establish one bounded camera-selection path shared by packaged runtime and editor
worlds:

1. a runtime session can name a camera by durable world/component identity;
2. that locator can remain pending while the world materializes asynchronously;
3. the resolved `CameraComponent` supplies its existing generational
   `RenderCameraHandle` without copying camera state or scanning the world;
4. an editor-owned camera can enter the same session-owned binding boundary
   explicitly without being serialized as world startup data; and
5. the active session selection can own one existing streaming observer,
   including position, velocity, enablement and primary policy.

The output of 10.4.4 is a valid, stale-safe camera binding that 10.4.5 can place in
`RenderFrameViewSetup::rootCameras`. This phase does not create a viewport,
swapchain, editor scene texture, render graph, camera-blend stack or gameplay
camera policy.

## Mandatory read-first set

Read in order before any authorized gate:

1. root `AGENTS.md` and [development document map](../README.md);
2. [world/rendering integration study](../studies/world-rendering-integration-study.md),
   especially G4 and 10.4.4-10.4.6;
3. [10.4.3 draw-contract plan](world-rendering-10-4-3-plan.md) and its closed
   `.planning` triplet;
4. `source/engine/include/vanguard/engine/{world_session_service,
   game_world_service,streaming_observer_service}.hpp` and implementations;
5. `source/entities/include/vanguard/entities/{component_directory,
   reference_registry,materializer,rendering_runtime,scene_components}.hpp` and
   implementations;
6. `source/rendering/include/vanguard/rendering/render_camera.hpp`, frame-view
   setup APIs and camera-storage implementation;
7. runtime/editor startup state code, package-set startup metadata, `.vproject`,
   `.vworld`, `.vcell` and `.vprefab` formats and their cookers; and
8. the RED sources listed below, refreshed against the exact CURRENT tree.

For every authorized gate, inspect `git status --short` and scoped diffs first.
Preserve all unrelated tracked and untracked work.

## Current Vanguard facts to preserve

1. `CameraComponent` already registers one camera with the existing render-camera
   command owner on attach, updates it through the changed-camera queue and
   unregisters it on detach. Construction deliberately does not select a viewport
   camera.
2. `RenderCameraHandle` and `ComponentHandle` are generational runtime identities.
   Neither may be serialized or treated as durable across detach, replacement or
   world change.
3. `ComponentDirectory::Find(entity, stableComponentId)` hashes to one known
   entity and walks only that entity's component chain.
   `EntityReferenceRegistry` publishes world-scoped stable entity identities only
   after materialization attachment. These existing owners must be reused; no
   all-camera directory or world scan is allowed.
4. `WorldSessionService` owns the world/input start-stop transaction.
   `GameWorldService` owns the live component directory, materializer systems and
   rendering runtime for that session.
5. `StreamingObserverService` already owns fixed-capacity generational observer
   registrations, velocity prediction, explicit primary selection, a pre-simulation
   immutable snapshot and world-origin fallback. It feeds `GameWorldService` before
   the simulation tick. Do not create a second observer registry or feed the cell
   system directly from a camera controller.
6. Runtime startup gets its world/input from mounted package metadata. Editor
   startup gets a project world through `WorldSessionStartRequest`. Neither path
   currently provides a camera locator.
7. `FrameRenderer` already consumes explicit root camera handles and supports
   multiple cameras/regions. Supplying frames and outputs belongs to 10.4.5.

## RED architectural evidence to refresh in P0

The RED source tree used for these citations lives at
`D:\root\R6.Root\Mainline\dev\src`; RED paths below resolve under that root. It is a
separate checkout and is never edited by this repository.

P0 must re-read and record exact paths, symbols and line ranges before accepting
an implementation design. Starting evidence is:

- `common/worldEntities/src/runtimeSystemCamera.cpp` keeps component registration
  in the world camera runtime and updates registered cameras as a distinct runtime
  system.
- `common/game/src/cameraSystem.{h,cpp}` owns active-camera policy separately from
  component registration. Activation initializes the camera's streaming observer;
  camera removal deactivates that relationship, and camera update runs in an
  explicit update group.
- `common/engine/include/updateTickGroup.h`, `common/game/src/cameraSystem.cpp` and
  `common/world/src/runtimeSystemNodeStreaming.cpp` order `CameraUpdate` before
  `PlayerAimUpdate`: RED updates camera streaming observers in the former and node
  streaming gathers them in the latter.
- `common/world/include/runtimeSystemNodeStreaming.h` and
  `common/world/src/runtimeSystemNodeStreaming.cpp` make streaming observers
  explicit caller-owned objects. Node streaming retains only registered raw
  pointers under its lock. Creation attaches an observer, the caller-owned
  observer's destruction detaches it, and shutdown asserts none remain.
- `common/world/include/worldContentObserver.h` and
  `common/world/src/worldContentObserver.cpp` keep observer position, velocity
  class and forward prediction separate from render-view construction.
- `backend/backendWorld/src/worldPreviewPanelSharedData.cpp` and preview/thumbnail
  callers create observers explicitly for the tool that needs them. RED does not
  infer that every render camera is a world-streaming source.
- `common/game/src/cameraDirector.cpp` demonstrates replaceable active-camera
  policy and a fallback camera without putting viewport dimensions in the camera
  policy owner.

Vanguard intentionally differs from RED in camera-handle representation, ECS and
its GPU-driven bindless renderer. Keep Vanguard's generational handles, explicit
frame-view families and existing observer snapshot service. Copy RED's useful
ownership split: component registration, camera policy, streaming observation and
render-output construction remain separate lifetimes.

## Locked ownership and data-flow contract

### Durable selection versus live handles

The serialized/runtime-startup form is a small world-scoped locator containing the
stable entity identity and stable component identity, plus enough explicit validity
or version information required by the chosen owning format. It never contains a
`ComponentHandle`, `RenderCameraHandle`, pointer, renderer slot or viewport ID.

The canonical locator owner is `.vworld`, because the locator names a component
inside that world. Its portable fields are `cellId`, the final materialized stable
`entityId`, and `componentStableId`. Do not add equivalent camera fields to
`.vproject` or package metadata. The package continues to name only its startup
world; the editor continues to name its project world. `WorldSessionStartRequest`
carries a camera-source policy (`WorldDefault` or `None`) rather than a second copy
of the locator. `WorldDefault` requires a locator in the loaded world and fails
startup if it is absent; only `None` permits a camera-less session. An editor
editor view code may instead supply an explicitly owned live camera through the
session binding.

`WorldResource` owns the immutable locator. `WorldSessionService` is the single
selection/controller owner for the active session: it owns source policy,
selection generation, observer handle, `BeginFrame` participant and teardown.
Runtime startup and editor view code both drive this existing service; no camera
controller service is added. `GameWorldService` owns only world-locator resolution
against the live component/materialization lifetime and exposes its current
generation-checked result to `WorldSessionService`. The renderer continues to own
the render-camera slot.

### Asynchronous binding state

The binding has explicit `none`, `pending`, `ready` and `failed` outcomes, or an
equivalent bounded state model accepted in P0. Session running, component attached,
camera render handle valid, drawable resources resident and output ready remain
different facts.

Resolution is driven after the existing owner-thread world flush. A pending
locator performs at most one entity hash lookup per completed world tick. That
lookup must prove the live identity was published by the locator cell's current
materializer generation through the base/default-active partition; a same-ID
entity in another cell or a later activation partition is not the authored startup
target. Once the entity is published with that provenance, component resolution
hashes to that entity's component head and walks only its short component chain.
It may not scan entities, visit all world components or enumerate cameras. Once
ready, retain only generation-checked
`ComponentHandle` and `RenderCameraHandle` values. Do not retain a component
pointer across a world tick or cache a copied camera-state snapshot as authority.

Wrong entity/component identity, wrong schema type and a terminally unavailable
startup camera are visible diagnostics with stable failure categories. Ordinary
late materialization remains pending and must not block renderer maintenance,
streaming progress or the main thread. The selected startup camera's cell must be
`AlwaysLoaded`, and its placement/entity/component must be eligible for
initial/default activation. The existing streaming grid locks every such cell
during initialization, so camera loading does not depend on world-origin distance
or on that same unresolved camera acting as an observer.

A terminal failure of a required `WorldDefault` selection sets
`GameWorldStatus::Failed`, fails the owning frame participant, and is mirrored by
`WorldSessionService::Poll` as `WorldSessionStatus::Failed` with a dedicated
required-camera failure code. Runtime therefore observes it through the normal
frame failure path even after startup has left its polling state. `None` has no
camera failure. Failure of an explicit optional editor camera is reported through
the session binding result, clears its observer, and does not fail the world.

Explicit selection change and world stop revoke the old binding and unregister its
observer before release begins. An asynchronous component detach can occur inside
the world tick before a post-tick resolver runs; the contract does not pretend to
pre-revoke that event. Instead, every consumer validates both stored generations:
the `BeginFrame` observer publisher validates before using component state, and
10.4.5 frame construction validates again before supplying a render handle. A
detach during Simulation may leave the already-copied PreSimulation observer
sample valid for that frame, but cannot expose a pointer or stale handle afterward.
Post-tick resolution then moves the binding to pending/failed as policy requires.
Rebinding is allowed only through newly resolved generations.

### Controller and editor boundary

The binding API exposes selection state and a current `RenderCameraHandle`; it does
not build a frame or own output dimensions. Runtime gameplay policy may replace the
startup selection. Editor navigation can provide an explicitly owned live camera
binding and can choose a different camera from runtime for the same world.

Multiple render views do not imply multiple streaming observers. Reflection,
preview, thumbnail, shadow and UI views have no observer authority unless their
owner explicitly selects them through `WorldSessionService`. 10.4.4 establishes a
policy seam that a future blend/director system can drive; it does not implement
RED's camera stack or silently designate the first camera.

### Streaming-observer binding

Reuse `StreamingObserverService` exactly once. `WorldSessionService`, acting as the
active session's controller, registers one observer when its selected camera has
streaming authority, updates position and velocity, chooses the velocity
class/prediction policy and sets primary status on acquisition. Teardown disables
and unregisters the exact observer handle; unregister clears primary status only if
that handle is still primary. It unregisters before releasing its camera/world
binding. Editor code supplies an
explicit live selection/pose through this same service; it does not own a parallel
observer or controller registry.

Observer publication stays in the existing `PreSimulation` participant. Camera
transforms finish in `PostSimulation`, so frame N streaming intentionally consumes
the pose completed at the end of frame N-1. The `WorldSessionService` `BeginFrame`
participant updates at most one observer before the snapshot, using
`WorldPosition::x/y/z.AsDouble()` and velocity derived from consecutive completed
poses. Work is O(`MaximumStreamingObservers`), a fixed 16-slot scan, with no
per-entity work, GPU readback, global camera scan or newly added hot-path lock. P4
must retain the existing short observer critical sections and prove the selected
update cadence adds no unbounded contention.

World-origin fallback remains valid while no observer has a usable position. It is
a loading fallback, not proof that a camera is ready. Failure of one optional editor
observer must not corrupt the world session; failure of a required runtime startup
binding must be reported by `WorldSessionService` policy.

## Required implementation gates

For P3-P5, implementation closure uses exact-CURRENT source review, scoped hygiene
and an independent read-only review. Premake regeneration, compilation and
executable execution are intentionally batched into P6 so the same major-phase
state is validated once rather than rebuilding after every intermediate gate.
Tests/proofs may still be authored in the gate that owns them; execution evidence
is recorded only when P6 actually runs it.

### P0 - exact architecture proof and gate refinement

Read-only except for planning documents.

- Trace the exact CURRENT Vanguard flow from authored/cooked startup data through
  session start, world materialization, component attach, camera registration,
  observer snapshot and future frame-view consumption.
- Trace the RED evidence above, including activation, update ordering, observer
  ownership and detach/shutdown behavior. Record exact paths/lines and every
  intentional Vanguard deviation.
- Record and re-review the accepted `.vworld` locator owner and prove how runtime
  packaging and editor policy reach the one `WorldSessionService` selection seam
  without duplicate fields or a test-only path.
- Prove how the selected camera's cell/activation group becomes reachable before
  the camera can act as an observer. No origin-dependent coincidence is accepted.
- Choose the smallest existing synchronization hook for resolve/revoke. Compare a
  materialization publication hook with bounded named lookup; reject global
  callbacks, directories and frame scans.
- Produce an exact file/test matrix, failure-state table, lifecycle sequence and
  performance budget for P1-P6.
- Obtain a fresh independent CURRENT-tree review. P0 closes only at
  0 BLOCKER / 0 HIGH / 0 MEDIUM after corrections.

Stop after documentation and review. P1 remains unauthorized.

### P1 - canonical locator and session-source contract

- Add the optional portable `(cellId, entityId, componentStableId)` locator to
  `.vworld`; advance its format/body version through the normal deterministic
  cooker and validate nonzero fields, an existing cell and `AlwaysLoaded`.
- This pre-shipping format has no legacy read guarantee: reject the current
  version-1 body deterministically as `UnsupportedVersion` after the version
  advance and require recooking. Test that exact behavior; do not add a migration
  backend or ambiguous absent-locator interpretation for old data.
- Add a by-value camera-source policy to `WorldSessionStartRequest`; do not copy
  the locator into package metadata or `.vproject`.
- Make packaged runtime choose `WorldDefault`; make editor startup explicitly
  choose `WorldDefault` or `None`. Do not hard-code fixture IDs or infer the first
  camera.
- Include deterministic round-trip, exact legacy rejection and invalid-value tests.
- Do not resolve components, register observers or create views in P1.

Stop for focused compile/tests, scoped diff hygiene and independent review.

### P2 - world-scoped asynchronous resolution and revocation

- Add the minimal selection state owned by the existing session/game-world owners.
- Copy the immutable locator from the loaded `WorldResource` into that live
  transaction, then resolve the named entity/component only after identity
  publication and component attachment. Validate exact camera schema/type and
  obtain the component's existing render handle.
- Keep late attachment pending without blocking. Report terminal identity/type and
  lifecycle failures with bounded stable diagnostics.
- A terminal required-world-camera failure sets `GameWorldStatus::Failed`, fails
  frame execution and is mirrored by `WorldSessionService::Poll` through a new
  required-camera failure code. Optional explicit editor-camera failure remains a
  binding result and does not fail the world.
- Revoke before explicit switch, stop or world change. For asynchronous detach or
  replacement inside a world tick, require generation validation at observer and
  frame-construction use boundaries, then transition after the tick. Rebind only
  from current owner state.
- Add no camera registry, copied camera snapshot, all-world scan or general event
  bus.

Stop for focused compile/tests and independent review.

### P3 - session-owned camera binding

- Make `WorldSessionService` the one selection owner and expose the smallest
  read-only/current binding needed by runtime and editor view code: state, current
  world/selection generation and current `RenderCameraHandle`.
- Allow editor view code to submit an explicitly owned live camera and precise pose
  through the same session binding, with ownership and revocation proved; never
  serialize that handle.
- Support selection replacement and distinct editor/runtime choices. Preserve
  multi-view support by returning an explicit selected root, not process-global
  "the camera" state.
- Do not construct `RenderFrameViewSetup`, output regions, viewports or graph nodes.

Stop for scoped source/hygiene checks and independent read-only review. Executable
validation is deferred to P6.

### P4 - explicit streaming-observer ownership

- Have `WorldSessionService` bind its explicitly selected camera to the existing
  `StreamingObserverService` using one generational observer handle and one
  `BeginFrame` participant.
- Publish valid precise world position, velocity, velocity class, prediction and
  primary policy at most once for the active session from `BeginFrame`, before the
  existing pre-simulation snapshot. Convert fixed-point axes directly with `AsDouble()` and
  use the previous completed post-simulation pose; reset velocity on first sample,
  cut/teleport, invalid delta and replacement.
- Disable/unregister the exact session observer in the correct order for camera
  switch, detach, world stop, explicit editor selection withdrawal and partial
  startup failure; per-handle unregister clears primary only if that handle is still
  primary.
- Prove world-origin fallback while pending and no automatic observer creation for
  secondary render views.
- Measure/inspect lock and update behavior; do not add per-entity work, an observer
  copy owned by `GameWorldService`, or a direct `CellStreamingSystem` bypass.

Stop for scoped source/hygiene checks and independent read-only review. Executable
validation is deferred to P6.

Implementation status: the CURRENT tree owns exactly one generational observer from
the `WorldSessionService` `BeginFrame` participant, lazily registered only for a
`Ready` binding, updated once per frame from `WorldPosition::AsDouble()` with
bounded consecutive-pose velocity and zero velocity on first sample, replacement,
invalid delta and cut/teleport. The velocity sample timestamp is the completed-pose
time `context.simulationTimeSeconds - context.simulationDeltaSeconds`, so variable
frame times, pause/unpause and spikes cannot skew prediction. The observer is
promoted to the single primary observer on registration and released in
disable/unregister/history order on switch, live withdrawal, detach, stop and
partial failure; unregister only clears primary status when the released handle is
still primary, so a later primary owner is undisturbed. Required `WorldDefault`
observer failure sets `CameraObserverFailure` and fails the frame; optional `Live`
failure keeps the world session valid. No new lock, scan, registry or bypass was
added. **P4 is closed at a fresh independent exact-CURRENT read-only verdict of
0 BLOCKER / 0 HIGH / 0 MEDIUM.** The behavioral observer proof matrix is now
authored in the P5 gate (see the P5 implementation status below); those tests were
unexecuted at P5 closure and now pass in both Debug and Development under the P6
batch. Final P6 independent review returned 0 BLOCKER / 0 HIGH / 0 MEDIUM.

### P5 - deterministic lifecycle and integration proof

Add focused native proofs for:

- locator format/source round-trip, deterministic legacy-version rejection and
  runtime/editor source parity;
- camera present before session start, camera attached late and camera replaced;
- wrong entity, wrong component stable ID and wrong component type;
- selection switch, component detach, world stop/restart and stale-generation
  rejection;
- explicit editor camera distinct from authored runtime selection;
- one primary observer, non-primary observers, velocity prediction and world-origin
  fallback;
- no observer for a render-only secondary/preview camera unless explicitly opted in;
- no world/component scan and bounded lookup/update counts; and
- rollback/shutdown with zero remaining camera bindings or observer registrations.

Use existing focused test targets and `tests/support/scratch_directory.hpp` for
filesystem data. Generated data belongs in test scratch or explicit build images,
never the repository root.

Implementation status: the deterministic matrix is authored in
`source/engine/tests/engine_services_tests.cpp` as
`RunWorldSessionObserverLifecycleTests()`, a real `EngineHost` executed in a fresh
subprocess after the parent test host finishes its first lifecycle. It reuses the
existing P1 `worldTests` locator/version proofs
and the existing CPU-only `RunWorldCameraResolverTests()` fixture for the
resolver/materialization/replacement/stale-generation cases, and adds the full
session/observer lifecycle through the real service graph:

- `None` session reaches `Running` with a `None` binding, and a running session
  with no observer publishes the world-origin streaming fallback;
- `WorldDefault` is `Pending` during `LoadingWorld` with no observer, and a
  missing authored locator becomes a terminal `RequiredCameraFailure` with no
  observer and a clean stop;
- one caller-owned `Live` `RenderCameraHandle` registered through the real
  `RenderCommandSystem` in the world `RenderScene` becomes the single `Ready`
  selection and takes primary ownership from an unrelated primary observer;
- precise pose publication, zero first-sample velocity, previous-completed-pose
  velocity under variable deltas, paused-frame zero velocity, teleport/cut reset,
  and selection-replacement reset;
- an unselected render-only secondary camera never becomes a streaming observer;
- `Live -> WorldDefault` revokes the Live observer immediately, and
  `WorldDefault -> Live` re-registers exactly one observer;
- a destroyed Live camera is a binding-local `LiveCameraUnavailable` failure that
  removes the observer without failing the world session;
- exhausting the fixed 16-slot observer capacity is an optional
  `CameraObserverFailure` that stays binding-local and self-heals;
- `ClearCameraSelection` removes observer authority immediately, stale selection
  tokens cannot mutate/revoke the replacement, stop/restart leave zero observer
  registrations and no camera binding, and host shutdown succeeds.

At P5 closure the tests were **authored but unexecuted**. P6 has now compiled and
executed the containing `engineServicesTests` target successfully in both Debug and
Development. P5's independent exact-CURRENT read-only review returned **0 BLOCKER /
0 HIGH / 0 MEDIUM**. The required-observer
(`WorldDefault`) failure
classification branch and a world-materialized `CameraComponent` observer remain
source-reviewed only because the device-disabled host cannot populate the renderer
`GetDefaultCameraPhases()` set; the P2 CPU fixture proves the resolver `Ready` state
that the shared observer path consumes.

Stop after the proof matrix is authored, scoped source/hygiene checks are clean and
an independent read-only review is complete. Execute the exact Debug/Development
matrix once in P6.

### P6 - closure and handoff to 10.4.5

- Generate VS2022 projects with Premake only when required and run serialized
  outer MSBuild (`/m:1`).
- Build the full focused dependency matrix and run every relevant owner suite.
- Recheck CURRENT source against RED ownership/lifecycle evidence, root generated
  output, `git diff --check`, stale status text and shutdown invariants.
- Record compilation, linkage, executable tests and source review as separate
  claims. Do not claim native pixels or performance unless actually measured.
- Obtain final independent CURRENT-tree review at
  0 BLOCKER / 0 HIGH / 0 MEDIUM.
- Mark 10.4.4 complete only when 10.4.5 can consume an explicit valid camera
  binding without inventing selection or observer policy.

Validation status: user-authorized P6 has completed its batched validation. Fresh
Premake generation passes after one comment-only source-audit correction. Serialized
Debug and Development x64 builds pass for `worldTests`, `entitiesTests`,
`renderingTests`, `engineServicesTests`, `runtime` and `editor`.
`worldTests` and the P5-bearing `engineServicesTests` pass in both configurations,
and runtime/editor `--validate-bootstrap` composition/shutdown smokes exit 0 in both.
That line records the historical validation command used at 10.4.4 closure; the
editor product smoke switch was later retired to the dedicated editor application
lifecycle integration-test executable. Runtime's separate bootstrap switch is not
changed by that editor cleanup.
`entitiesTests` reproduces its pre-existing materializer/reference-lifecycle failure
family and `renderingTests` reproduces one pre-existing stale GPU Scene layout-version
oracle (`7` versus the current CPU/shader ABI version `9`); independent causality
review found neither failure is reachable from or caused by 10.4.4 production code.
Scoped source hygiene is clean. The final independent exact-CURRENT P6 review
returned **0 BLOCKER / 0 HIGH / 0 MEDIUM**, so P6 and 10.4.4 are closed. No pixel or
performance result is claimed; 10.4.5 remains unstarted.

## Acceptance boundary

10.4.4 is complete when packaged runtime and editor can establish different,
explicit camera selections for the same normal world lifecycle; a durable locator
survives asynchronous materialization; switch/detach/replacement cannot expose a
stale camera; and only the explicitly selected `WorldSessionService` binding drives
the existing streaming-observer service with bounded work.

Completion does **not** mean a window or editor panel renders pixels. That proof
begins in 10.4.5 and finishes in 10.4.7.
