# 10.4.6 session replacement and terminal draining

## Status and authorization

Definition work opened on 2026-09-25 for cross-agent implementation planning and is
now **CLOSED** after a fresh independent exact-current docs-only review returned
**0 BLOCKER / 0 HIGH / 0 MEDIUM**. The user subsequently authorized **P0 only** on
2026-09-25. **P0 is now CLOSED** after exact-current lifecycle/RED proof, failure/
capacity/API/test inventory refinement and a fresh final independent read-only review
returned **0 BLOCKER / 0 HIGH / 0 MEDIUM**. The user explicitly authorized **P1 only**
on 2026-09-26; **P1 is now CLOSED** after implementation, focused static proofs and a
fresh final independent exact-current read-only **0 BLOCKER / 0 HIGH / 0 MEDIUM**
review. The user explicitly authorized **P2 only** on 2026-09-27; **P2 is now CLOSED**
after its release-only GameWorld/component/materializer/streaming implementation,
focused source/static proofs and a fresh final independent exact-current read-only
**0 BLOCKER / 0 HIGH / 0 MEDIUM** review. The user then explicitly authorized **P3**
on 2026-09-27; **P3 is now CLOSED** after session first-fault/finalization and
application-owner ordering implementation, focused source/static proofs and fresh
final independent exact-current read-only **0 BLOCKER / 0 HIGH / 0 MEDIUM** review.
The user explicitly authorized **P4** on 2026-09-27; **P4 is now CLOSED** after
cancellation/rollback/terminal-device implementation, focused source/static proofs,
scoped hygiene and a fresh final independent exact-current read-only **0 BLOCKER /
0 HIGH / 0 MEDIUM** review. P4 remained source/static only: no Premake generation,
compilation, executable test, cooker, runtime/editor/native execution or product
launch was performed. The user then explicitly authorized **P5** on 2026-09-27;
**P5 is now CLOSED** after deterministic lifecycle/replacement source proofs, scoped
static hygiene and a fresh final independent exact-current read-only **0 BLOCKER /
0 HIGH / 0 MEDIUM** review. P5 also remained source/static only: no Premake
generation, compilation, executable test, cooker, runtime/editor/native execution or
product launch was performed. The user explicitly authorized **P6** on 2026-09-28;
**P6 is now CLOSED** after evidence-backed scaling/synchronization/source-hygiene
corrections, focused source/static proofs and a fresh final exact-current read-only
**0 BLOCKER / 0 HIGH / 0 MEDIUM** review. The user then explicitly authorized
**P7** on 2026-09-28. The repository's later hard validation rule in root `AGENTS.md`
supersedes the older broad P7 acceptance wording below: the final gate must leave the
relevant source compiling and run only the smallest focused proof tests needed for
closure; broad regression/native/UI matrices require separate explicit authorization,
and historical plans do not override that rule. **P7 is now CLOSED and 10.4.6 is
CLOSED** because exact-current relevant Debug + Development source compiles, focused
session/replacement/drain and P6 ownership proofs are green, the standard runtime
package/replacement path is green, scoped hygiene is clean, and the final independent
source review returned **0 BLOCKER / 0 HIGH / 0 MEDIUM**. The unrelated concurrent
E3.6 editor-preference compile failure does not block 10.4.6 closure, and the additional
editor/native/UI acceptance rows are deferred/not run under the superseding rule.
10.4.1-10.4.6 are closed; 10.4.7 remains outside this contract and must not start from
this task.

This phase closes G6 from the
[world/rendering integration study](../studies/world-rendering-integration-study.md).
10.4.5 proved that the production world/render path reaches runtime presentation
and editor texture output. Its normal cooked editor run then exposed the remaining
terminal-lifetime boundary: after successful scene production, UI consumption and
exact output-receipt validation, shutdown can destroy a visual component while its
renderer proxy/publication retirement is still live. 10.4.6 fixes that ownership
and progress contract without redesigning the renderer.

The active task triplet is
`.planning/world-rendering-10-4-6-session-replacement-terminal-drain/`.
Durable architecture and accepted completion stay in this plan and the development
documentation map. Do not put task-specific state in `.pi/` or production source.

## Objective

Make world-session stop, replacement and final application shutdown complete the
existing asynchronous ownership chain cleanly:

1. revoke session camera/observer and world-frame admission before teardown begins;
2. keep only the renderer progress required to settle already-owned component,
   scene-publication, residency and GPU-retirement work while the world is stopping;
3. distinguish legitimate asynchronous `Pending` from a concrete terminal failure;
4. release components, RenderScene state, world resources and source ownership only
   after their downstream consumers are finished; and
5. prove the same contract for cancellation, partial startup, replacement, output
   loss and final shutdown without a recursive gameplay frame or fabricated GPU
   completion.

The phase also contains a dedicated **P6 production cleanup and scaling gate**.
P6 removes integration bloat and major algorithmic/synchronization pathologies in
the 10.4.1-10.4.6 production path, makes the result structurally closer to RED's
simple ownership model in Vanguard's managed/generational design, and proves that
test/proof/phase scaffolding has not leaked into production source or product
targets. This is targeted production hardening, not a renderer rewrite and not the
broad performance acceptance owned by 10.4.7.

## Mandatory read-first set

Before every authorized gate:

1. root `AGENTS.md`, [development document map](../README.md), and
   [test-support isolation policy](../test-support-isolation.md);
2. [world/rendering integration study](../studies/world-rendering-integration-study.md),
   especially G6 and 10.4.5-10.4.7;
3. closed [10.4.4](world-rendering-10-4-4-plan.md) and
   [10.4.5](world-rendering-10-4-5-plan.md) plans plus their `.planning` triplets;
4. `source/engine/src/{world_session_service,game_world_service,rendering_service}.cpp`;
5. `source/entities/src/{materializer,visual_component,rendering_runtime}.cpp` and
   `source/entities/docs/rendering-performance-preflight.md`;
6. `source/gameWorld/src/game_world.cpp`;
7. `source/rendering/src/{render_scene,render_scene_gpu,render_command_system}.cpp`
   plus GPU Scene/residency owners reached by normal `RenderingService::RenderUpdate`;
8. runtime/editor 10.4.5 frame/output suppliers and their quiesce/drain paths; and
9. exact-CURRENT RED lifecycle/render-command evidence listed below.

Inspect `git status --short` and scoped diffs before edits. Preserve unrelated
tracked/untracked work. Premake/build/executable/native validation is batched into
P7 unless the user explicitly grants a narrower exception at an earlier gate.

## Exact-current Vanguard facts

### The stop controller already has the right outer state

- `WorldSessionService::RequestStop()` revokes the selected camera/streaming
  observer, sets `WorldSessionStatus::Stopping`, and prevents a new session from
  beginning until the old one returns to `Idle`.
- `WorldSessionService::PollStop()` already treats `GameWorldStopResult::Pending`
  as nonterminal. It releases the world resource and session input only after the
  game-world owner reports `Complete`.
- 10.4.5 runtime/editor frame suppliers already require a `Running` session before
  producing world frames, so Stopping naturally closes new world-view admission.
  Do not add a second stop flag to each frame supplier unless exact-current P0
  evidence proves a real race that this status cannot express.

### The confirmed G6 mismatch is below that controller

- `GameWorldService::StopWorld()` progresses streaming/materialization by ticking
  the world with zero delta and returns `Pending` while cell/executor release work
  remains. After those owners appear empty it shuts down `GameWorld`, then calls
  `RenderingRuntime::ReleaseScene()`.
- `RenderingRuntime::ReleaseScene()` cancels queued admissions, attempts pending
  proxy destruction, and finally asks `RenderSceneManager::DestroyScene()` to
  detach the scene.
- `RenderSceneManager::DestroyScene()` legitimately reports `Busy` while relink
  jobs, visibility feedback, cameras, GPU publication or GPU retirement still own
  scene state. `RenderSceneGpuPublisher::DetachScene()` likewise rejects non-Serial
  publication state, dirty indices, retirements or tracked proxies.
- `GameWorldService::StopWorld()` currently maps any `ReleaseScene()` failure to
  terminal `Failure`. Normal application stop polling is therefore unable to
  advance the renderer work that would make that same scene releasable on a later
  attempt.
- Visual-component teardown is fail-closed: `LightComponent`, `StaticMeshComponent`
  and camera/component lifetimes assert if renderer ownership survives their
  lifecycle. The 10.4.5 standard cooked editor run reached exactly this boundary
  after the pixel/output path had otherwise succeeded.

### Normal renderer progress already has the needed owners

`RenderingService::RenderUpdate()` already owns the ordered work that settles the
renderer side of this lifecycle: join the previous retained CPU render tail,
resolve GPU Scene contributions, accept mesh bindings, consume asynchronous
failures, collect GPU Scene/mesh/texture/native retirements, progress residency and
material bindings, then seal retirements from actual submitted RHI receipts.
`RenderingService::OnQuiesce()` performs related final checks for whole-service
shutdown, but it requires global output/residency/scene emptiness and is therefore
not itself a per-session replacement API.

The 10.4.6 implementation must expose/reuse the smallest existing-owner progression
needed by a stopping session. It must not duplicate the RenderUpdate algorithm in a
new application service and must not call the whole gameplay/frame pipeline
recursively from `PollStop()`.

### CPU completion and GPU retirement remain different facts

`RenderCommandSystem::FlushPreviousFrameProcessing()` joins only the retained CPU
process-frame tail. It does not call Present and is not GPU-completion evidence.
GPU-safe retirement continues to use the renderer/RHI's submitted receipts/fences,
including the existing device-loss terminal policy. 10.4.6 must not turn the CPU
flush into a device-wide wait or invent frame/fence values to make shutdown pass.

## RED architectural evidence to refresh in P0

The RED checkout is `D:\root\R6.Root\Mainline\dev\src` and is read-only.
Definition-time exact-current inspection establishes the direction; P0 must refresh
the exact paths/line ranges before implementation:

- `common/gameFramework/src/gameStateMachineImpl.cpp`: retiring states call
  `ProcessShutdown()` once, then remain in a retiring set while lazy asynchronous
  operations are progressed to completion. State retirement is explicitly guarded
  against recursive/out-of-order re-entry.
- `common/gameFramework/src/gameEngine.cpp`: ordinary frames keep one previous-render
  command flush and one renderer FrameTick before viewport submission. Engine
  shutdown releases the game framework/session state before the viewport and then
  proceeds through base-engine teardown.
- `common/renderer/src/renderCommandHandler.cpp`:
  `FlushPreviousFrameCommandsProcessing()` is only the process-frame job-counter
  boundary; it is not presentation or fabricated GPU completion.
- `common/renderer/src/renderViewport.cpp`: viewport destruction flushes outstanding
  renderer work before destroying its native output ownership.
- RED session/scene teardown keeps the same reverse-ownership shape at finer grain:
  `BaseGameSession` explicitly detaches/shuts down its scene; `RuntimeScene::DetachGame`
  flushes render-command work before mass reverse detach; entity-system shutdown runs
  before dependent systems are released; `RenderProxyHandle` unregisters and queues
  scene removal rather than silently dropping renderer ownership; and the rendering
  runtime releases its scene while asserting that proxy/scene ownership is actually
  gone.
- RED's renderer `Flush`/previous-command flush is renderer-command synchronization,
  not proof that arbitrary GPU work has completed.

The reusable RED principle is **retire asynchronously through the existing owners,
then destroy in reverse ownership order**. Vanguard intentionally keeps managed
services, generation-checked handles, explicit RenderFlow/RHI receipts and its
GPU-driven bindless/GPU Scene design. Those differences require one narrow
renderer-owned publication/cutover progression during terminal drain rather than
RED-style globals or implicit completion. Do not port RED globals, raw viewport IDs,
implicit completion, or a second state machine beside the existing Vanguard
session/service owners.

## Locked 10.4.6 lifecycle contract

### 1. Stop is an asynchronous transaction, not destructor cleanup

- `RequestStop()` is admission closure. Once a session is Stopping, no new world
  viewport frame, camera selection, streaming-observer update, component admission
  or world-owned renderer mutation may be introduced for that session.
- Already accepted work retains its normal owner and is allowed to finish. The
  session remains Stopping while any legitimate downstream owner is still busy.
- A component must not be marked lifecycle-detached/uninitialized and then destroyed
  if releasing its renderer ownership failed. The accepted implementation must
  preserve enough component/materializer ownership to retry through the normal
  owner; destructor fatal checks remain invariants, not cleanup mechanisms.
- Renderer-backed component teardown is transactional at the component boundary:
  specialized detach/uninitialize cleanup that drops runtime/resource references
  must not commit before the base renderer-proxy release has either been accepted or
  was never owned. If release is Busy, the component remains in a retryable lifecycle
  state with its pre-release ownership intact. Do not “half detach” and hope a later
  destructor/scene shutdown repairs it.
- Stop completion is monotonic and exactly once. A terminal worker/RHI/device/
  ownership fault is latched as the first terminal cause, but the session remains in
  its stopping transaction while already-accepted world/render ownership still needs
  safe release or explicit abandonment. Only after those owners are quiescent or
  terminalized may the stop surface the latched failure and enter `Failed`. A clean
  stop alone reaches `Idle`; no hidden reset may make a failed owner appear complete.

### 2. Renderer drain is narrow progression through existing owners

- Join previously submitted CPU recording through the existing render-command tail.
- Progress only the renderer subsystems needed to settle already-admitted scene
  changes/retirements and resource ownership. Prefer extracting/reusing the current
  `RenderingService::RenderUpdate()` owner steps over copying them.
- GPU Scene proxy removals may require the existing renderer FrameTick/publication
  machinery even when there is no drawable viewport. If P0 confirms that exact
  dependency, the drain uses a **renderer-owned retirement-only progression** for
  the stopping scene: no viewport Begin/Configure/Submit, no output supplier, no UI
  and no Present. It must preserve the one authoritative GPU Scene publisher rather
  than inventing a shutdown publication path.
- `GameWorld::Tick(0)` is a real world/gameplay tick and is not an allowed terminal
  drain primitive. P0 must identify/extract only the stop-owned streaming,
  materializer and ECS-commit progress required under their existing owners.
- If the existing render FrameTick/publication machinery needs a nonzero advancing
  identity after application/game frames stop, 10.4.6 uses exactly one authoritative
  monotonic **renderer-owned publication/progress epoch** coherent with normal
  renderer progression. `WorldSessionService` and `GameWorldService` never fabricate
  application/game frame values. The epoch is scheduling/publication identity only;
  it is not a fake GPU receipt, fence or completion signal.
- Do not recursively run `FramePipelineService`, `GameWorld::Tick`, the application
  gameplay frame, UI, presentation or a new synthetic world frame merely to advance
  retirement.
- Do not create a private scene-retirement queue, shutdown renderer, fence registry,
  or second GPU Scene publication path. Existing dirty queues, publication phases,
  residency owners and GPU lifetime/fences remain authoritative.
- A stopping session with no new drawable view is still allowed to advance
  retirement. Drain progress must not depend on producing another visible frame.

### 3. Pending and Failure are explicit and propagated

- `Busy` caused by known in-flight CPU publication, GPU publication, residency,
  submitted retirement or component/materializer release maps to session stop
  `Pending`, with bounded forward progress on subsequent stop polls.
- Invalid generation/ownership, lost required state, failed worker/RHI operation,
  capacity corruption or a terminal device failure latches a concrete terminal
  failure; do not spin forever by relabeling an actual failure as Pending. While
  accepted ownership still exists, polling continues only the safe release/
  abandonment work and does not expose final `Failure` early enough to stop the
  application state machine from invoking cleanup again.
- Device removed/reset follows the existing abandonment/terminalization policy.
  Never wait for a fence that cannot complete. P0/P4 must specifically cover the
  case where renderer-owner abandonment has invalidated GPU publisher internals
  while RenderScenes still exist: terminal device teardown needs an explicit
  ownership-release/abandon branch, not a `Busy` result that can never become Ready.
  The latched device failure is surfaced only after that ownership is structurally
  terminalized.
- No fixed sleeps, timeout-as-success, fabricated queue/fence/frame values, process
  kills, destructor suppression or catch-and-ignore teardown paths.

### 4. Destruction remains reverse ownership

The stop transaction locks this ordering. The exact helper/API shape is P0/P1 work,
but implementations may not invert these owners:

`session admission/camera/observer`
-> `world streaming/materializer release requests`
-> `component renderer-release acceptance`
-> `RenderingRuntime proxy/GPU publication/retirement completion`
-> `RenderScene release while RenderingRuntime/GameWorld are still initialized`
-> `GameWorld runtime-system/world shutdown`
-> `world resource + session input`
-> `replacement Begin or final host service shutdown`.

This corrects the exact-current inversion where `GameWorldService::StopWorld()` can
reach `m_world.Shutdown()` before the explicit `RenderingRuntime::ReleaseScene()`
attempt. Renderer-backed components must never depend on a later scene release to
repair an already-completed component destruction.

Component destruction and GPU retirement are deliberately separate milestones. A
visual component may clear its binding and be destroyed after its existing
renderer owner has **accepted** the proxy retirement; the component need not remain
alive until the GPU fence retires. If the renderer cannot yet accept that retirement
because publication/relink/mutation state is Busy, the component/materializer
release remains Pending and the component stays lifecycle-owned. Once accepted,
`RenderingRuntime` owns the delayed/physical proxy retirement and 10.4.6 progresses
that owner to completion before scene release.

`RenderingRuntimeConfig::proxyRetirementDelayFrames` is currently expressed in
complete-frame boundaries. P0 must classify whether terminal stop keeps that policy
through explicit drain epochs or may collapse a purely visual delay after admission
has closed. It must never collapse actual GPU safety: physical resource retirement
still follows real submitted RHI receipts/fences.

Engine-lifetime renderer catalogs/resources and process source ownership survive a
world replacement. Runtime/editor output owners retain the 10.4.5 lifecycle: a
world replacement does not create another swapchain/texture registry or silently
destroy an unrelated editor/native UI output. Final host shutdown still tears down
viewports before `RenderingService` and source owners through the service DAG.

### 5. Replacement is old-session completion followed by new admission

10.4.6 does not introduce overlapping live worlds. A replacement request must
complete the old session's stop transaction **successfully to `Idle`** before
`Begin()` can admit the new session. A session that terminalizes in `Failed` blocks
replacement unless a separately specified recovery/reset contract is added in a
later phase; 10.4.6 defines no such reset. World generation/camera generation handles
continue to make old references stale at the existing boundaries.

## P6 cleanup, scaling and production-source hygiene contract

P6 is deliberately part of 10.4.6 because terminal/replacement paths can expose
high-cardinality costs that do not appear in a one-object happy path. Its scope is
the production streaming-to-render integration established by 10.4.1-10.4.6 and
the direct owners/callees changed or newly exercised by those phases. It is **not**
a license for unrelated engine cleanup.

### Complexity and synchronization audit

P6 must inspect operation counts at real cardinality boundaries rather than merely
reviewing code style. In the scoped production path:

- no per-frame/per-resource/per-entity/per-proxy full-table scan may scale with a
  project/world maximum when an exact handle/index, dirty list, active list or
  existing changed queue can express the same work;
- no O(N^2) mass removal/replacement behavior is accepted where ownership already
  supplies an O(1) unlink/index or bounded batch path;
- no lock/spinlock acquisition **per element** across a high-count hot batch when
  the same existing owner can validate/process the batch under one bounded lock or
  disjoint worker ranges;
- no per-element heap churn is added to high-frequency publication/retirement loops
  where existing pools/preallocated ranges/batch scratch are available;
- cold lifecycle scans are not automatically bugs, but a scan proportional to
  configured maximum capacity must be justified or replaced when live-count/index
  state already proves the invariant. Definition-time example to re-check in P6:
  `RenderSceneGpuPublisher::DetachScene()` scans all non-null tracked pages/slots
  after it has already rejected dirty and retirement state; scene tracked counts may
  permit a cheaper exact emptiness proof;
- `RenderingRuntime` can carry up to 64K pending proxy retirements and currently
  visits the pending list each normal frame; preserve the existing O(1) removal
  fixes but measure whether large backlogs need an owner-local due/ready structure;
- `MeshResidencyManager` currently has normal-progress paths that scan its residency
  high-water array and reacquire the shared spinlock around individual slots; audit
  actual lock/slot operation counts and prefer changed/active work lists or a bounded
  batch only where evidence establishes a material cost;
- existing bounded dirty queues, spatial candidates, job ranges, GPU count/prefix/
  scatter and wave aggregation are preserved rather than replaced by convenient
  CPU-wide scans.

The pre-existing `source/entities/docs/rendering-performance-preflight.md` records
earlier corrections for O(N) admission prefix removal, O(C^2) camera cancellation,
O(R^2) retirement duplicate search and repeated retirement-array shifting. P6 must
verify those wins remain intact and must not reintroduce equivalent pathologies in
the new drain/replacement code.

P6 acceptance requires evidence, not intuition. For any path whose scalability is
material, add a focused deterministic operation/lock-count or cardinality-scaling
oracle using normal test instrumentation/diagnostics. P6 may use already-available
measurements, but new executable timing runs stay batched into P7 unless the user
explicitly authorizes a focused exception. Static inspection can identify a suspect;
it cannot by itself support a timing/performance claim. Broad CPU/GPU performance
acceptance remains 10.4.7.

### RED-like cleanliness in Vanguard's architecture

“More like RED” means retaining RED's direct ownership/sequencing virtues while
keeping Vanguard's deliberate safety/model differences:

- one session stop owner, one renderer progress owner, one RenderScene/GPU Scene
  lifetime, one viewport/presentation path;
- no duplicate stop coordinator, registry, scheduler, cache, retirement queue or
  “temporary” production abstraction that survives only because tests use it;
- narrow APIs named for product behavior, not development phases or proof needs;
- collapse redundant helpers/state after the integrated ownership is clear, but do
  not reduce line count at the cost of explicit generation/failure/receipt checks;
- keep generation handles, managed service lifetime, explicit RHI receipts and the
  GPU-driven bindless path as intentional Vanguard deviations from RED.

### Test/proof isolation is a release gate

P6 must apply `docs/development/test-support-isolation.md` to the scoped production
path and its Premake targets:

- production source/headers contain no `P0`-`P7`, `10.4.6`, ticket/checkpoint,
  agent/session/worker identity, proof-only log/ID, or phase-test concept;
- no test/fixture/harness/automation-only source lives in production `src`,
  `private`, `framework`, `extensions`, `runtime` or analogous product trees;
- no production header includes `tests/**`, and no product target wildcard or file
  list compiles/links `tests/**` or `tests/support/**`;
- no validation-only CLI/environment variable/plugin/service/catalog is added to a
  product executable to make a proof convenient;
- integration fixtures/drivers live in explicit test trees with behavior-based
  names and dedicated test targets. Tests may consume production APIs; production
  code may not consume test support;
- any small production-shared decision seam added for deterministic testing must be
  independently justified product/internal behavior and actually used by the
  production path. Otherwise the seam remains test-owned or is deleted.

Definition-time inspection already found minor cleanup examples: a phase-number
reference in an editor scene-output production comment and “focused proofs” wording
in the production-private texture receipt helper. P6 removes/rewords such history;
production comments describe product ownership and invariants only.

## Capacity and hot-path principles

- World/session stop polling itself may be repeated, but each poll performs bounded
  owner progress; it must not rescan all live resources/entities simply to discover
  unchanged state.
- Component/proxy retirement remains generation-checked and changed-item driven.
- Renderer drain must not hold a scene/global lock while waiting on a CPU job or GPU
  fence. Acquire locks for state mutation/inspection, release them before joins.
- If a batch contains many elements, use the existing batch/range APIs or add one
  owner-local bounded batch primitive rather than N public lock-taking calls.
- Bounded fixed directories such as the maximum RenderScene directory may be scanned
  where the bound is deliberately tiny and independent of content scale; distinguish
  that from scanning maximum proxies/resources/entities.

## Failure and lifecycle matrix to lock in P0

P0 must produce the exact-current state/failure table before P1 implementation,
including at least:

- stop while LoadingWorld/input loading;
- stop from Running with no visual proxies;
- stop with pending proxy admission, dirty/relink work, pending GPU publication,
  proxy retirement and resource residency retirement;
- visual component release that returns Busy before succeeding on a later poll;
- no drawable camera/output while retirement still needs renderer progress;
- output resize/loss and retained texture/presentation generations already in flight;
- world replacement after clean stop and rejection of Begin while old stop is
  `Stopping`/Pending or terminalized `Failed`;
- partial GameWorld/session startup rollback;
- failed worker/render command/publication;
- device removed/reset during drain;
- terminal worker/RHI/device failure while accepted ownership still requires release
  or abandonment, including the point at which the latched failure is finally
  surfaced;
- final application shutdown with runtime and editor output owners; and
- source/resource release ordering after the last world/render consumer.

Every case names the owner, retryable condition, progress primitive, terminal
evidence and final state. “Call until it works” is not an accepted contract.

P0 must replace/classify the existing `GameWorldService::StopWorld()` call to
`GameWorld::Tick(0)` as forbidden terminal-drain behavior. Exact-current
`GameWorld::Tick` runs the normal world/runtime-system frame path; 10.4.6 therefore
extracts only the stop-owned streaming/materializer/ECS-commit progress needed by
Draining under the existing owners. `WorldSessionService::PollStop()` never invokes
`GameWorld::Tick`, the application/frame pipeline, or an equivalent synthetic
gameplay frame.

## P0 exact-current lifecycle and failure lock

P0 was authorized on 2026-09-25 and is **CLOSED**. This section is source/planning
evidence only: no 10.4.6 production implementation, Premake generation, compilation,
executable test, cooker run or native launch was part of P0. The final independent
exact-current adversarial review returned **0 BLOCKER / 0 HIGH / 0 MEDIUM** after the
source-order, admission-boundary, ECS-journal, materializer-resume and streaming-race
corrections below were incorporated.

### Exact-current ownership facts

- `source/application/src/state_machine.cpp:229-245` re-invokes `OnExit` only while
  it returns `Pending`; `Failure` immediately moves the state machine to `Failed`.
  Runtime/editor `StopSession` (`runtime/src/runtime_application.cpp:18-40`,
  `editor/src/editor_application.cpp:57-75`) translate a public
  `WorldSessionStatus::Failed` directly into `OnExit` failure. A stop-time terminal
  cause therefore cannot be exposed publicly before accepted ownership is settled.
- `source/engine/src/world_session_service.cpp:40-46` admits `Begin()` only from
  `Idle`; `101-114` returns public `Failed` without running `PollStop`; `202-213`
  enters `Stopping`; and `612-653` already provides the repeated stop-poll owner.
  The stop transaction, not the application state, owns first-fault latching and
  cleanup progress.
- `source/engine/src/game_world_service.cpp:176-220` currently calls
  `GameWorld::Tick(0)` and later calls `GameWorld::Shutdown()` before
  `RenderingRuntime::ReleaseScene()`. `source/gameWorld/src/game_world.cpp:313-333`
  proves that `Tick(0)` is a real runtime-system frame (all begin/end/after-flush
  callbacks plus ECS progress), while `222-258` proves world shutdown uninitializes
  systems in reverse only after ECS ownership is empty. Both current stop choices
  must therefore change in P1/P2, without changing the one stop owner.
- `source/entities/src/cell_streaming_system.cpp:246-339` owns executor event intake
  and materializer release requests, while `341+` mixes release completion with
  activation/publication work. `source/world/src/streaming_executor.cpp:118-227`
  proves shutdown can cancel Requesting nodes, clear Failed nodes and turn Resident
  nodes into explicit `ReleaseRequested` ownership. Terminal drain needs a
  release-only CellStreaming progression, not those normal frame hooks. Non-cell
  distant-proxy release events remain routed to their existing RenderingRuntime
  owner; a late ResourceAvailable during shutdown must not become a new durable proxy
  admission merely to be cancelled again. `WorldStreamingExecutor::Process()` polls
  completed requests before asking the shutdown grid for stream-out commands, so the
  same release-only poll can legitimately observe `ResourceAvailable` followed by
  `ReleaseRequested` for a node that was Requesting when stop began. In that case the
  downstream owner consumes the resource directly into release/cancellation and
  acknowledges executor release without materializing a new cell/proxy.
- `source/entities/src/materializer.cpp:1242-1391` queues release as one ECS command
  batch and reports `NotReady` until the batch commits. The entity-destroy batch is
  sealed first (`1303-1307`), then published references are unregistered and stable
  components are destroyed (`1319-1337`), while the sealed ECS destroy batch is only
  executed later by the explicit ECS flush. Renderer-backed component release must
  therefore be accepted before entity destruction can become committed.
  `materializer.cpp:591-605` already
  supplies the per-object retry boundary once component lifecycle can report Pending
  precisely. However `QueueRelease()` is not currently an explicit resumable release
  transaction: it unregisters published cell references, then destroys objects
  sequentially, and a later object failure can return after earlier objects were
  already destroyed while the cell record still has its pre-release state. P2 must
  make that monotonic partial progress explicit (release phase/cursor or equivalent),
  unpublish references exactly once, preserve the blocked object's lifecycle state,
  and resume without rolling back already accepted renderer/component releases.
- The ECS committed-change journal is itself a teardown-capacity boundary.
  `ecs::WorldConfig::committedChangeCapacity` defaults to 65,536 and
  `TransformRuntimeConfig::maximumChangesPerSynchronize` also defaults to 65,536,
  while one materialized cell may legally contain up to 1,048,576 entities and
  4,194,304 stable components. `World::FlushActions()` publishes every queued entity
  destroy before downstream consumers read the journal and overwrites the oldest
  record when the ring is full. P2 may therefore not flush an arbitrarily large
  terminal-release batch and then synchronize afterward. It must preserve every
  committed teardown record through bounded release/commit slices, larger proven
  journal capacity, or an equivalent owner-correct delivery mechanism. Lost journal
  records are terminal correctness failures, not a P6 performance concern.
- `source/entities/src/visual_component.cpp:161-210` can rebind a proxy when renderer
  release fails, but `source/entities/src/component_directory.cpp:588-631` invokes
  void detach/uninitialize callbacks and commits lifecycle completion unconditionally.
  This is the exact P2 seam: Busy renderer release must remain Pending without a
  half-detached component.
- `source/entities/src/rendering_runtime.cpp:637-650` makes successful
  `BeginProxyRetirement` + pending-retirement admission the component-to-renderer
  ownership-transfer point. `835-938` proves normal `OnBeginFrame` also performs mesh
  preparation and new proxy admission, so terminal drain must not reuse it wholesale;
  it needs retirement-only progression after admission closes.
- `source/rendering/src/render_scene.cpp:1228-1266` makes scene destruction Busy for
  relink/visibility/camera/GPU-publication ownership and separately reports
  `ProxiesRemainAlive`; `1639-1696` distinguishes retryable `Busy`/`PendingDestroy`
  proxy states from stale/invalid/capacity failures. Stop code must preserve those
  distinctions rather than treating every false renderer call as Pending.
- `source/rendering/src/gpu_scene_runtime.cpp:483-653` confirms a renderer FrameTick
  can complete a retirement-only publication with no upload requests and no viewport
  render. `source/rendering/src/render_command_system.cpp:949-1045` requires a
  nonzero renderer tick identity and dispatches the retained CPU tail; its
  `1073-1089` flush joins that CPU tail only. The terminal path therefore uses one
  renderer-owned monotonic publication/progress epoch and never fabricates an
  application frame, Present, queue receipt or GPU fence.
- `source/engine/src/rendering_service.cpp:1084-1155` is the authoritative normal
  ordering for CPU-tail join, contribution/binding resolution, asynchronous failure
  consumption, GPU/mesh/texture/native retirement collection, residency progress and
  sealing from actual submitted RHI fences. P1 extracts/reuses only the stop-needed
  subset in this owner; WorldSession/GameWorld do not clone it.
- On Removed/Reset, `rendering_service.cpp:983-1027` abandons renderer-global owners;
  `gpu_scene_runtime.cpp:326-350` requires the CPU publication chain joined before
  abandonment, and `render_scene_gpu.cpp:517-529` deletes publisher scene state and
  detaches the publisher from `RenderSceneManager`. P4 must use that real abandonment
  boundary so scene cleanup never waits on an impossible device fence.
- Runtime/editor output owners are not world owners. Runtime quiesce destroys its
  EngineViewport/output only at final host teardown
  (`runtime/src/runtime_viewport_service.cpp:109-148`); editor scene-output quiesce
  likewise joins its retained CPU tail and releases output generations only at host
  teardown (`editor/src/editor_scene_output_service.cpp:130-163`). Ordinary world
  replacement merely stops new session frames; it does not destroy these outputs.

### P0 refreshed RED teardown evidence

The P0 refresh re-read the exact-current RED checkout rather than relying only on
the Definition summary:

- `common/gameFramework/src/gameStateMachineImpl.cpp:126-137` invokes
  `ProcessShutdown()` once when a state enters retirement; `375-389` keeps polling
  its lazy operations until their async status completes; `517-535` rejects
  recursive/out-of-order retirement. The transferable rule is one shutdown owner
  plus owner-local asynchronous progress, not repeated re-entry through gameplay.
- `common/gameCore/src/baseGameSession.cpp:445-505` explicitly detaches the game
  before scene shutdown and shuts the game instance down afterward.
- `common/world/src/worldRuntimeScene.cpp:93-119` calls renderer `Flush`
  before reverse system detach; `332-369` tears down explicit dependencies and then
  remaining systems in reverse.
- `common/renderData/src/renderProxy.cpp:77-97` unregisters pending/system ownership
  before detach and releases the scene/proxy references only afterward; `136-154`
  queues `RemoveProxyFromScene` rather than treating detach as physical completion.
- `common/world/src/runtimeSystemRendering.cpp:315-325` asserts that non-final
  rendering-system destruction has no registered proxies and no RenderScene;
  `388-397` removes unregistering proxies from pending attach; `520-535` and
  `606-619` release the scene through the renderer owner.
- `common/renderer/src/renderCommandHandler.cpp:870-909` chains FrameTick behind the
  renderer flush counter; `912-965` chains scene rendering on the same ownership;
  `967-991` synchronizes queued render commands; `1008-1011` shows
  `FlushPreviousFrameCommandsProcessing()` is only the process-frame counter flush;
  and `1104-1111` queues proxy removal. `common/renderer/src/renderInterface.cpp:676-684`
  delegates renderer `Flush` to that command synchronization.

RED therefore reinforces four P0 rules without dictating Vanguard implementation:
begin shutdown once and poll accepted work; synchronize renderer command ingress
before reverse producer teardown; treat proxy-removal acceptance separately from
completion; and release the render scene before destroying its rendering-system
owner. Vanguard keeps managed services, generational handles, GPU Scene and explicit
RHI receipts rather than RED globals or implicit GPU completion.

### Locked stop progression

P1-P4 may choose exact function names, but they may not change this ownership flow:

1. **WorldSession admission closure:** revoke camera/observer authority, reject new
   world frames and begin/retain exactly one `Stopping` transaction. Rejected
   `Begin()` requests that accepted no ownership are command failures, not a reason
   to manufacture an admitted failed session; an admitted-session fault follows the
   stop transaction below. The exact-current admission boundary is after request/
   generation validation and successful pre-session input clearing: a failure of
   that initial `GameInputService::Clear()` occurs before `m_ownsInput` is set and
   therefore leaves the session `Idle`. Once `m_ownsInput`, an input request, or a
   world request has been accepted, later startup failure is a partial admitted
   transaction and uses the terminal stop-cleanup path.
2. **Join already-dispatched world CPU work:** `GameWorldService` completes an
   existing transform tail and consumes its exact failure. It does **not** dispatch
   another transform update and does not call `GameWorld::Tick`. Already-accepted
   pre-stop ECS structural/component queues are committed through an explicit
   stop-owned ECS commit boundary exactly because they were accepted before admission
   closed; this commit does not run Flecs gameplay progress or RuntimeSystem frame
   callbacks. Before P2 may use the current all-at-once `FlushActions` /
   `FlushComponentActions`, it must prove the accepted queue fits the lossless journal
   delivery budget. If not, the commit API/journal delivery must become bounded or
   otherwise lossless first. The exact Component/Transform consumers then synchronize
   those committed records before release-only teardown starts.
3. **Release-only streaming/materializer progress:** the existing
   `CellStreamingSystem`/`WorldStreamingExecutor` owner polls/cancels accepted
   requests, emits release requests, retries materializer/component release, commits
   only the subsequently queued ECS teardown batches required by that release, and acknowledges
   completion. It does not activate/publish new cells or run unrelated runtime-system
   frame callbacks. Distant-proxy ReleaseRequested ownership is forwarded to
   `RenderingRuntime` so an already-resident/admission-pending proxy is cancelled or
   retired before the executor release is acknowledged; shutdown does not promote a
   late resource completion into new durable renderer admission. Exact post-commit
   Transform/Component synchronization is invoked
   only where the committed teardown records require the existing owner to consume
   them; no generic `OnAfterWorldFlush` loop is used as a hidden frame. Each commit
   slice must fit the proven committed-change delivery budget for every consumer;
   teardown may not overflow the ECS journal between synchronizations.
   The same rule applies to cells: a shutdown-raced `ResourceAvailable` is not fed
   through `QueueCell`; when its paired stream-out reaches `ReleaseRequested` without
   a downstream materialized operation, the release-only owner completes the executor
   resource release directly rather than treating the missing downstream object as
   failure.
   Materializer release itself is resumable: it may retire already-releasable
   components monotonically, but a Busy component leaves the cell/group in an
   explicit release-in-progress state and returns Pending rather than terminalizing
   or pretending the whole release was atomic. Published references are revoked once
   at the release cutover and never re-published during stop.
4. **Component-to-renderer transfer:** a visual component stays attached/initialized
   while renderer release is Busy. Success at the existing renderer retirement
   admission boundary transfers delayed/GPU lifetime to `RenderingRuntime`; the
   component/materializer may then finish destruction without waiting for a GPU
   fence.
5. **Renderer-owned drain:** `RenderingService` joins the existing render CPU tail,
   resolves only already-accepted publication/binding work, advances retirement and
   residency owners, issues an existing renderer FrameTick only when scene
   publication/retirement requires it, and seals/collects lifetime with actual
   submitted RHI receipts. There is no viewport Begin/Configure/Submit, UI or
   Present. The monotonic renderer progress epoch is owned here. P1 uses **one epoch
   source across both normal and terminal renderer FrameTicks**; it does not bolt a
   second shutdown counter beside `context.frame + 1` and risk duplicate identities.
6. **Reverse release:** after component/proxy ownership and GPU-publisher scene work
   are empty, release the world-owned RenderScene while `RenderingRuntime` and
   `GameWorld` still exist. For an externally owned adopted scene, detach all
   world-owned proxies/publication state and clear the RenderingRuntime binding but
   leave the scene itself to its external owner. Only then run `GameWorld::Shutdown`,
   release `WorldService`, and clear session input. Runtime/editor output owners
   survive ordinary replacement.
7. **Finalize once:** a clean transaction clears stop state and reaches `Idle`. The
   first terminal cause remains latched while steps 2-6 perform safe cleanup or
   explicit abandonment, then is surfaced exactly once as terminal session `Failed`.
   `Begin()` remains rejected from `Failed`; no implicit failed-session reset is part
   of 10.4.6. Host service quiesce/shutdown must distinguish this ownership-empty
   terminal `Failed` state from a failed session that still owns teardown work.
   `RequestStop`/application `StopSession` must not reopen an ownership-empty final
   `Failed` session into another Stopping transaction.

### Pending / failure / final-state matrix

| Case | Owner and retryable Pending evidence | Allowed stop progress | Terminal evidence / final state |
|---|---|---|---|
| Idle / empty stop | `WorldSessionService`; no accepted world/input | none | idempotent clean `Idle` |
| Begin rejected before ownership admission, including initial input-clear failure | WorldSession command boundary; `m_ownsInput` is still false and no input/world request is accepted | none beyond local operation rollback | return typed Begin failure and remain clean `Idle`; replacement/retry remains legal |
| Stop while world/input request loads | WorldSession/WorldService/resource requests | cancel accepted request, then `WorldService::ReleaseWorld` | cancel/release invariant failure latches; cleanup terminalizes before `Failed` |
| Begin fails after accepting input/world ownership | WorldSession owns a partial startup transaction | enter the same `Stopping` cleanup transaction; cancel/release only what was actually accepted | first startup failure remains latched; clean ownership release completes to terminal `Failed`, not `Idle` |
| Loading/startup fault discovered by `Poll` after Begin succeeded | WorldSession latches the concrete resource/input/GameWorld/camera cause and begins terminal cleanup | startup application state may transition to Exiting, but it must report the latched cause and let `OnExit` keep polling cleanup; do not replace it with generic invalid-state text | ownership-empty session surfaces the original cause as final `Failed`; application/runner preserves that first failure |
| Streaming Requesting/Resident/ReleasePending/Failed nodes | WorldStreamingExecutor + CellStreamingSystem; node stats/operations remain | release-only executor polling/cancel/stream-out/materializer handoff | executor/ownership corruption latches failure; zero downstream ownership permits continuation/finalization |
| Pre-stop ECS queues already accepted | ECS World plus exact Component/Transform consumers | lossless bounded structural/component commit + owner-specific synchronization; current all-at-once flush is usable only when proven within journal headroom; no Flecs gameplay progress or generic RuntimeSystem frame callbacks | queue/committed-change loss or synchronization failure latches; stop does not silently discard accepted structural ownership |
| Large cell teardown exceeds committed-change delivery budget | CellMaterializer + ECS journal + Component/Transform consumers | release/commit in bounded owner-correct slices (or equivalent proven lossless delivery) with consumers synchronized between slices | any `lostRecords`, overwritten required change, or unbounded atomic batch beyond proven journal headroom is terminal correctness failure; never accepted as scale-only debt |
| Distant proxy AdmissionPending/Resident during stream-out | RenderingRuntime + streaming executor retain the exact non-cell ownership | cancel pending proxy admission or accept proxy retirement, then acknowledge executor release; do not create a new stop-time durable proxy from a late ResourceAvailable | cancellation/retirement invalid state latches failure; release is complete only when both renderer and executor ownership transfer is explicit |
| Materializer release batch open/pending | CellMaterializer command-batch status `Open`/`Pending` or lifecycle `NotReady` | retry component release and commit teardown ECS batch only | stale generation, invalid state/reference, queue failure or lost ownership latches failure |
| Materializer component-release pass partially completes before a later component is Busy | CellMaterializer owns an explicit release-in-progress phase/cursor; already released objects stay released, blocked object remains lifecycle-owned | resume from owner-tracked release progress after renderer drain; do not rollback accepted retirement or re-publish references | only a real component/reference/invariant error latches failure; Busy is Pending and cannot leave the cell masquerading as Active |
| Visual renderer release Busy | component/materializer still owns component and pre-release state | retry same component release after renderer owner progresses | invalid/stale renderer ownership is terminal; successful retirement admission transfers ownership |
| Queued proxy admission / mesh preparation at stop | RenderingRuntime owns accepted pre-render work | close admission, cancel/terminalize queued work; do not process it as a normal BeginFrame | inability to cancel/terminalize exact accepted work latches failure |
| Pending RenderingRuntime retirement delay | RenderingRuntime pending-retirement record | retirement-only progress using renderer progress epochs for policy delay | invalid/stale proxy or capacity/invariant failure terminal; accepted DestroyProxy advances to publisher ownership |
| `DestroyProxy` Busy / PendingDestroy | RenderScene publication/relink owner | renderer publication progression, then retry exact proxy | invalid handle/scene/state, wrong thread or capacity failure terminal |
| `DestroyScene` Busy | RenderScene relink/visibility/camera/GPU publisher still owns state | drain those existing owners and retry | `ProxiesRemainAlive` means upstream release is incomplete; invalid handle/state is terminal, not Busy |
| GPU Scene dirty/publication/retirement work | one RenderSceneGpuPublisher + GpuSceneRuntime | renderer-owned FrameTick/publication epoch, CPU-tail join, lifetime collect/seal from real receipts | publication/write/lifetime failure latches; no fabricated completion |
| Mesh/texture/material/native retirement | renderer residency/lifetime owners | reuse ordered RenderingService retirement/residency subset | owner/RHI failure latches; device Removed/Reset follows abandonment row |
| No drawable camera/output | session/output supplier admits no new world frame | same renderer-only drain; no view required | absence of view is not failure and cannot block retirement |
| Resize/lost/in-flight output generation | runtime/editor output owner | finish already-accepted output CPU ownership as required; freeze/no new world frame | ordinary world stop never destroys output; final host quiesce owns output teardown |
| Render worker / command / publication failure | failing renderer owner; first failure latched | join accepted CPU work, cancel/abandon what can no longer complete, continue ownership teardown | surface the first cause only after ownership is terminalized; session ends `Failed` |
| Device Removed/Reset | RenderingService/RHI | join CPU publication chain, invoke existing renderer-owner abandonment, then release CPU scene/world ownership without GPU waits | device cause remains first terminal failure; no impossible fence wait; session ends `Failed` after ownership is empty |
| World resource release after GameWorld drain | WorldService executor/grid should have only Idle operations after downstream release completion | perform its one final executor/grid shutdown and resource release | false after GameWorld reports stop complete is a terminal ordering/invariant failure, not unbounded Pending; never drop resource before consumers |
| Stop-time session input clear fails after admitted ownership | GameInputService/session logical owner | do not admit replacement; terminalize session ownership without claiming a successful clean stop | `InputClearFailure` becomes terminal `Failed`; host-owned input service remains responsible through final host teardown |
| Clean world replacement | WorldSessionService | complete every row above to clean `Idle` | only `Idle` may admit replacement `Begin()` |
| Final application shutdown | application state -> WorldSession -> service DAG | `OnExit` remains Pending until session cleanup is complete; final host then quiesces outputs/renderer | clean stop may exit normally; terminalized fault is reported once and host teardown must not misdiagnose already-empty ownership as a leak |

The public result vocabulary remains intentionally small. `Pending` means a named
existing owner has a concrete forward-progress action. `Failure` means a concrete
first terminal cause has been latched; during cleanup it is **not yet** the public
session state. A retryable renderer `Busy` is never silently converted into terminal
failure, and a terminal invalid/stale/capacity/worker/RHI error is never relabeled
`Pending` simply to keep polling.

P0 also locks **failure detail preservation**. Exact-current
`GameWorldStopResult::Failure` has no failure payload and `WorldSessionService`
therefore collapses every stop failure to `WorldSessionFailureCode::GameWorldStopFailure`.
P1/P2 must not extend that information loss. Every new drain/progress boundary that
can return Failure carries a typed reason (and the relevant downstream RenderScene/
RenderCommand/GPU/RHI or streaming/component cause where one already exists). The
GameWorld stop boundary gains the minimum typed stop-failure output needed for
WorldSession to retain the first concrete cause. WorldSession may still expose the
top-level `GameWorldStopFailure` category, but deterministic diagnostics/tests must be
able to distinguish transform/streaming-component/renderer-progress/scene-release/
world-shutdown terminal causes. A bool-only helper is acceptable only where false is
unambiguously one terminal class and cannot mean Busy/Pending.

The same distinction applies to **command rejection versus admitted transaction
failure**. Invalid Begin arguments, generation exhaustion, Begin while non-Idle, and
failure of the initial input clear occur before new session ownership is accepted and
must report a typed failure without poisoning the Idle service state. Input/world
request rejection after `m_ownsInput` is established, or any later loading/start
failure, already has session-owned cleanup and therefore latches the first cause,
enters/stays in the stop transaction, and surfaces terminal `Failed` only after that
partial ownership is released.

`RenderingRuntimeConfig::proxyRetirementDelayFrames` is retained in 10.4.6 and is
interpreted through the same renderer progress epochs that correspond one-for-one
with normal FrameTicks while Running. Terminal drain may advance those real renderer
publication epochs without producing a viewport frame; it does not special-case the
delay to zero. The delay remains policy only: actual GPU safety is still determined
by RenderScene/GPU-lifetime ownership and submitted RHI receipts.

### P0 API / file / proof inventory for later gates

P0 does not implement these surfaces. It locks the minimum ownership boundaries that
P1-P4 must realize; names may be refined only if the same owner and semantics remain:

| Later gate | Minimum production surface | Primary files | Deterministic proof owner |
|---|---|---|---|
| P1 | one `RenderingService` session-drain/progress operation returning explicit progress/Pending/failure **with typed renderer failure detail**; one renderer-owned monotonic progress epoch used by both normal and terminal FrameTicks; reuse/extract ordered RenderUpdate helpers without staging new stop-time work | `source/engine/{include/vanguard/engine/rendering_service.hpp,src/rendering_service.cpp}`, `source/rendering/{render_command_system,frame_renderer,gpu_scene_runtime,render_scene_gpu}` | `renderingTests` plus `engineServicesTests` for service routing; no viewport test mode |
| P2 | release-only CellStreaming/materializer/distant-proxy stop progression; an explicit resumable materializer release transaction for partial component progress/reference cutover; typed retryable component detach/uninitialize result or equivalent transaction; RenderingRuntime admission closure/cancellation + retirement-only progress; lossless bounded ECS commit/delivery when accepted queues exceed journal headroom; a typed `GameWorld` stop-failure output that preserves the first downstream cause; RenderScene released before GameWorld shutdown | `source/engine/{game_world_service.hpp,game_world_service.cpp}`, `source/gameWorld/{include/vanguard/game_world/game_world.hpp,src/game_world.cpp}`, `source/entities/{cell_streaming_system,materializer,component,component_directory,visual_component,rendering_runtime,transform_runtime}` and matching public/private headers; `source/ecs/{include/vanguard/ecs/ecs.hpp,src/ecs.cpp}` if the existing all-at-once flush/journal boundary must change | `gameWorldTests`, `engineServicesTests`, `ecsTests`, existing entities/rendering focused tests; behavior names only |
| P3 | WorldSession first-fault latch/finalization, replacement only from `Idle`, ownership-empty terminal `Failed` service-lifecycle handling; runtime/editor startup states preserve the original latched failure while transitioning to Exiting and their `StopSession` path never reopens final Failed cleanup | `source/engine/{world_session_service.hpp,world_session_service.cpp,game_world_service.cpp}`, `runtime/src/runtime_application.cpp`, `editor/src/editor_application.cpp` | `engineServicesTests`, application state-machine tests, later runtime/editor lifecycle integration |
| P4 | device-loss and async-failure terminalization using existing renderer abandonment; partial-start/cancellation cleanup with no reset-as-success | RenderingService/GPU Scene/residency owners plus WorldSession/GameWorld rollback owners already listed | rendering/engine deterministic fault matrices; native device behavior remains P7 |

No new coordinator, service, queue, fence registry or output owner is justified by
P0. Test seams must be production-used decision/progress functions or remain under
`tests/**`; production must never depend on a fixture.

### Capacity and P6 audit inventory fixed by P0

- **P2 correctness capacity:** ECS committed-change retention defaults to 65,536;
  TransformRuntime reads at most 65,536 changes per synchronize and ComponentRuntime
  consumes the same journal in 4,096-record reads. Materialization permits up to
  1,048,576 entities and 4,194,304 components per cell. P2/P5 must prove lossless
  bounded publication for both already-accepted pre-stop queues and teardown releases
  larger than one journal window before P0's terminal-drain contract can be considered
  implemented. If an accepted queue cannot be partially committed under current ECS
  semantics, P2 must change the commit/journal delivery boundary rather than flush it
  and knowingly lose records. This is correctness, not deferred P6 performance
  acceptance.
- `RenderingRuntimeConfig`: 65,536 pending proxy admissions, 65,536 pending proxy
  retirements and 65,536 pending mesh preparations. The normal retirement countdown
  currently visits every pending retirement each world frame; P6 measures large
  release bursts and records whether a due/ready structure is warranted.
- `RenderSceneDesc`: default 1,048,576 proxies and 65,536 pending proxy mutations.
  `RenderSceneGpuPublisher::DetachScene` scans tracked pages/slots during cold scene
  detach; P6 compares this with maintained tracked counts/direct state before deciding
  whether to remove the scan.
- `MeshResidencyConfig::maximumResidencyRecords` defaults to 4,096.
  `MeshResidencyManager::Tick` takes the shared spinlock around individual slots in
  multiple high-water passes, and `CollectRetirements` scans the high-water range with
  per-slot lock acquisition (and a second lock for completed retirements). P6 records
  slot visits and lock acquisitions at active/high-water cardinality before changing
  concurrency. Exact-current default-cap accounting is three 4,096-slot passes per
  `RenderingService::RenderUpdate` (two Tick passes plus CollectRetirements): 12,288
  high-water slot visits and a baseline 12,290 acquisitions of the same global
  spinlock before branch-specific commit locks.
- `RenderSceneGpuPublisher` uses 4,096 tracked-proxy slots per allocated page. With
  the default 1,048,576-proxy scene cap that is 256 page pointers; cold detach checks
  all page pointers and all slots in every allocated page, with a worst-case
  256 + 1,048,576 checks. P6 must first prove the maintained tracked counts cover
  every rollback/retirement path before replacing this invariant scan; a debug-only
  cross-check may remain if useful.
- `MaximumRenderScenes` is 64 and remains a deliberately small fixed directory; its
  simple loops are not automatically optimization targets.
- P6 must preserve the existing O(1) admission unlink, camera-dirty backlink,
  retirement duplicate marker and swap-remove wins documented in
  `source/entities/docs/rendering-performance-preflight.md`.
- P6 production-hygiene manifest includes the phase-history comment in
  `editor/src/editor_scene_output_service.cpp` and the “focused proofs” wording in
  `source/rendering/private/vanguard/rendering/texture_output_receipt_state.hpp`, plus
  proof/history wording in `material_program_layout_internal.hpp`,
  `material_materializer_internal.hpp` and the viewport terminal-export comment.
  P6 also performs a fresh product-Premake/test-support dependency scan. These are
  cleanup candidates, not P0 source edits; the exact-current runtime/editor product
  targets remain isolated from their dedicated test/support projects.

## Explicit non-goals

- No second renderer, RenderScene, GPU Scene publisher, scheduler, frame pipeline,
  presentation service, texture registry, fence registry or resource source owner.
- No recursive gameplay/application frame from session stop.
- No forced global GPU-idle wait merely to simplify ordinary world replacement.
- No weakening of component destructor/lifecycle assertions to hide live ownership.
- No overlapping dual-world transition model unless separately defined later.
- No renderer rewrite, new streaming policy, PBR/HDR/image-quality work or broad
  10.4.7 performance/pixel acceptance.
- No production test mode, test-only package injection API, phase-tagged runtime
  behavior or automation seam.

## Gates

### P0 - exact-current lifecycle proof and contract refinement

- Refresh exact Vanguard and RED ownership/ordering evidence with paths/line ranges.
- Trace every stop/replacement owner from `RequestStop()` through materializer/
  components, RenderScene/GPU publication/residency, outputs and source release.
- Lock the exact `Pending` versus terminal `Failure` classification and device-loss
  policy; identify which existing renderer progression steps are required while a
  session is Stopping.
- Produce the concrete API/file/capacity/test matrix and P6 performance/hygiene
  audit inventory. Do not implement P1.
- Independent exact-CURRENT review must return 0 BLOCKER / 0 HIGH / 0 MEDIUM.
- Stop.

### P1 - renderer-owned session-drain progression

- Introduce the smallest renderer-owned, main-thread progression surface needed to
  settle already-admitted scene publication, residency and retirement while no new
  world frame is produced.
- Reuse/extract normal `RenderingService::RenderUpdate()` owner steps instead of
  cloning them. Preserve CPU-tail versus GPU-receipt semantics and release locks
  before waits/joins.
- Where exact GPU Scene retirement publication requires it, drive the existing
  renderer FrameTick machinery only for retirement-eligible stopping scenes and
  without any viewport/output supplier. Do not create a second publisher or a fake
  frame receipt.
- Lock the single renderer-owned monotonic publication/progress epoch used when
  normal application/game frame identity is no longer advancing. It must remain
  coherent with normal renderer publication and must never be interpreted as GPU
  completion evidence.
- Return explicit progress/Pending/failure evidence suitable for the session owner.
- Add focused deterministic source/unit proofs; no broad execution before P7.
- Independent review 0/0/0, then stop.

P1 was explicitly authorized on 2026-09-26. The exact-current implementation adds one
typed RenderingService::ProgressSessionDrain surface, factors normal RenderUpdate
owner progression for reuse, uses one RenderingService-owned monotonic epoch across
ordinary and terminal RenderCommand FrameTicks, and adds exact per-scene GPU-publisher
pending-work observation including non-serial zero-change state. Texture/mesh
producers return direct staged-work evidence rather than using diagnostic counters for
dispatch control. Terminal progression never submits a viewport/output frame
and continues to use real submitted RHI fences for retirement safety. Focused tests
are authored but intentionally unexecuted at this intermediate gate. Fresh final
independent exact-current read-only review returned **0 BLOCKER / 0 HIGH / 0 MEDIUM**;
P1 is closed. P2 was subsequently authorized separately on 2026-09-27.

### P2 - deferred visual/component and GameWorld teardown

- Make renderer-backed component release retryable without falsely marking a
  component detached/uninitialized when its proxy owner is still Busy.
- Make specialized visual teardown commit only after base proxy release acceptance
  (or provide an equivalent rollback-safe two-stage transaction). Exact-current
  `OnVisualDetach`/`OnVisualUninitialize` hooks can currently clear derived runtime
  state even though the base proxy release may subsequently fail; P2 must remove
  that half-detached state.
- Integrate materializer/world draining with that deferred ownership so normal
  destruction runs only after renderer retirement is accepted. Once accepted, move
  lifetime responsibility fully to `RenderingRuntime`; do not keep dead component
  objects around solely for GPU fences.
- Make `RenderingRuntime::ReleaseScene()` / `RenderSceneManager::DestroyScene()`
  preserve legitimate Busy as pending progress rather than converting it to fatal
  world-stop failure; keep real invalid-state failures fail-closed.
- Release the RenderScene before `GameWorld::Shutdown()` uninitializes its runtime
  systems. `RenderingRuntime::OnUninitialize()` must then observe an already-released
  scene (or enforce the same final invariant), not be the first best-effort cleanup.
- Keep component destructor invariants intact.
- Focused source proofs + independent review 0/0/0, then stop.

P2 was explicitly authorized on 2026-09-27. The exact-current implementation adds
bounded structural/component ECS commit with structural-before-component ordering and
exact ComponentRuntime/TransformRuntime synchronization, transactional renderer-backed
component detach, monotonic materializer release/reference cutover, release-only
cell/distant streaming and RenderingRuntime retirement progression, typed nested stop
failure detail, and RenderScene release/external detach before GameWorld shutdown.
Pending retirement records are stamped with the renderer progress epoch only after
renderer ownership transfer is accepted, so an earlier renderer epoch cannot consume
the configured delay. Terminal component synchronization consumes renderer lifecycle
failure after each component-sync attempt before evaluating the sync result. Stop-time
distant-proxy callback failure is a transient per-poll signal while the first typed
failure remains latched, allowing later polls to finish accepted ownership before
final FailedComplete. Focused source proofs cover >65K lossless delivery, retry/no-
replay behavior, real RenderScene Busy rollback, late/pending/resident streaming races,
epoch-before-acceptance exclusion and external-scene survival. P2 remained a
source/static intermediate gate: no P2 Premake generation, compilation, executable
test, cooker/native/editor run was performed; the earlier one-off compile exception
completed before P2 and is not P2 validation. Fresh final independent exact-current
post-fix read-only review returned **0 BLOCKER / 0 HIGH / 0 MEDIUM**. **At this
historical P2 closure checkpoint, P2 was closed and work stopped before P3; P3 was
authorized later as recorded below.**

### P3 - session replacement and application-owner ordering

- Drive P1/P2 from the existing `WorldSessionService` Stopping transaction.
- Preserve camera/observer revocation and no-new-frame admission while allowing
  narrow renderer retirement progress.
- Complete old world/session ownership before admitting the replacement; preserve
  process-lived renderer catalogs and source owner lifetime.
- Admit replacement only after the old stop completes cleanly to `Idle`. A latched
  terminal failure completes cleanup into `Failed` and blocks replacement; this
  phase does not invent an implicit reset/recovery transition.
- Reconcile runtime/editor 10.4.5 output generation teardown only where final host
  shutdown requires it; ordinary world replacement must not invent a second output
  lifecycle.
- Focused source proofs + independent review 0/0/0, then stop.

**P3 closure (2026-09-27):** pre-admission `Begin()` rejection now remains clean
`Idle`, while any admitted terminal fault is first-write latched and drives the
existing `Stopping` transaction until ownership is terminalized. `WorldSessionFailure`
retains exact nested `GameWorldStopFailure` detail; `StopWorld` detail is captured
even while GameWorld still reports Pending, later cleanup faults cannot replace an
earlier session cause, and `WorldService::ReleaseWorld()==false` is a terminal
`WorldReleaseFailure`, never indefinite Pending. Clean stop alone returns to
`Idle`; terminal cleanup ends in non-reopenable/non-replaceable `Failed`.
WorldSession and GameWorld host lifecycle accepts terminal Failed only when the
corresponding session/runtime ownership is already empty. Runtime/editor startup and
`StopSession` preserve the original session cause while application `OnExit`
continues polling cleanup. Focused `engineServicesTests` source proofs include
Idle-preserving command rejection, replacement rejection while Stopping, exact nested
GameWorld renderer detail, ownership-empty Failed shutdown, and an isolated competing-
fault fixture proving the first session cause wins. Scoped source/static hygiene and
fresh final independent exact-current read-only review returned **0 BLOCKER /
0 HIGH / 0 MEDIUM**. P3 remained an intermediate source/static gate: **no P3 Premake
generation, compilation, executable test, cooker, native/editor execution or product
launch was performed. At this historical P3 closure checkpoint, P3 was closed and
work stopped before P4; P4 was explicitly authorized later on 2026-09-27.**

### P4 - cancellation, rollback and terminal-device behavior

- Cover loading cancellation, partial startup failure, no-view/no-output drain,
  output loss/resize in flight, async execution failure and device removal/reset.
- Prove every accepted operation is either retired once or terminalized once; no
  leaked request, source, camera, proxy, scene generation or output receipt.
- Use the established terminal device-failure path rather than waits that can never
  complete. Add the explicit scene/publisher ownership abandonment needed so a
  device-lost scene cannot remain permanently Busy after renderer internals are
  abandoned.
- Prove first-fault latching: terminal failure is remembered immediately, but final
  failure is not surfaced to the application state machine until all previously
  accepted ownership is quiescent or explicitly terminalized.
- Focused source proofs + independent review 0/0/0, then stop.

**P4 is CLOSED.** Resource-request cancellation now atomically distinguishes
unfinished cancellation from already-terminal success/failure so stop preserves the
exact resource cause. Admitted partial startup failures enter the one existing
`Stopping` transaction; input failure is observed independently while world loading
is still pending; clean replacement remains legal only from `Idle`.

Renderer terminal handling preserves the first typed failure separately from
`ownersAbandoned`. Async participant/command/publication failures are transferred
once into session drain before later device state can replace them. Device
`Removed`/`ResetRequired`, lost submitted-retirement receipt coverage, submitted GPU
Scene failure, failed producer rollback, stale accepted mesh binding and the sealed
GPU-scene/geometry retirement `InvalidState` cases use the existing renderer-owner
abandonment path because those accepted owners have no forward-progress action.
Retryable `Busy`/prepared/requeue states remain retryable rather than being blanket
abandoned.

Terminal GPU-scene abandonment now severs the RenderScene manager/publisher
relationship even while scenes are live; normal publisher detachment remains strict.
`GameWorldService` persists renderer-abandonment evidence and switches only then to
`RenderingRuntime::AbandonDeviceReleaseOnly`, which terminalizes world-scoped CPU
proxy/scene ownership without fabricating renderer epochs, GPU completion or another
frame. Ordinary world stop still does not own runtime/editor output teardown.

Focused source proofs cover atomic cancellation dispositions, no-view/no-input stop,
participant first-failure transfer, Removed/ResetRequired and lost-receipt
abandonment evidence, failed contribution rollback, live-scene publisher
abandonment, real delayed-retirement Pending-to-CPU-only abandonment, and a
fresh-process admitted partial-start rollback to ownership-empty `Failed`. Scoped
static hygiene is clean and a fresh final independent exact-current read-only review
returned **0 BLOCKER / 0 HIGH / 0 MEDIUM**. P4 had **no** Premake generation,
compilation, executable test, cooker, runtime/editor/native execution or product
launch. **At this P4 checkpoint P5-P7 were unauthorized/unstarted; P5 was later
separately authorized and is closed below, and P6 was later separately authorized
and is closed below. P7 remains unauthorized/unstarted.**

### P5 - deterministic lifecycle and replacement proof matrix

- Author production-path deterministic proofs for the P0 matrix, including repeated
  Pending -> progress -> Complete transitions and terminal failure propagation.
- Prove replacement cannot begin before old ownership is complete and that normal
  world replacement does not rebuild engine-lifetime renderer/source owners.
- Prove a terminal fault encountered mid-stop remains latched while release/
  abandonment progresses, then surfaces exactly once after ownership is settled;
  verify `Failed` does not admit replacement.
- Prove CPU-tail join alone cannot satisfy GPU retirement and that no recursive
  gameplay/world tick or Present path is used. Where renderer-only publication needs
  an advancing epoch, prove that identity comes from the renderer owner rather than
  a fabricated application/game frame or GPU-completion receipt.
- Keep proof scaffolding test-owned and behavior-named. No broad execution before P7.
- Independent review 0/0/0, then stop.

P5 closure evidence — 2026-09-27:

- The focused engine-service matrix now drives one real delayed renderer retirement
  through repeated `Stopping` polls, rejects replacement throughout that ownership
  window, directly proves the live GameWorld frame counter does not advance, and
  admits replacement only after clean `Idle`.
- First-terminal-fault proof rejects replacement both while cleanup is `Stopping` and
  after final ownership-empty `Failed`, while preserving the original session cause
  against a later renderer cleanup failure.
- Ordinary replacement preserves exact engine-lifetime renderer/source owners,
  including the mounted root `PackageReader`, while retiring the old per-world scene
  and creating a distinct replacement scene.
- Renderer-only drain advances the one renderer-owned progress epoch without
  advancing the application FramePipeline or viewport Begin/Submit/Present counters.
  The exact-current proof sequence contains exactly three dispatching drain calls.
- Direct CPU-tail flush leaves renderer policy retirement Pending when no new
  renderer epoch is dispatched. The existing D3D12 proof separately demonstrates
  that real physical GPU retirement still requires submitted queue fences, waits and
  lifetime collection; CPU-tail completion is not GPU-completion evidence.
- Existing ECS proof processes 65,537 accepted structural/component changes through
  a 65,536-record committed-change journal without subscriber loss, and production
  `CommitAcceptedWorkSlice` couples teardown to bounded flushes plus immediate
  terminal-consumer synchronization.
- Scoped source/static hygiene is clean, and the fresh final independent exact-current
  whole-P5 read-only review returned **0 BLOCKER / 0 HIGH / 0 MEDIUM**. P5 made no
  production-source changes and ran no Premake generation, compilation, executable
  test, cooker, runtime/editor/native execution or product launch.

**P5 is CLOSED. At this checkpoint P6-P7 were unauthorized/unstarted; P6 was later
separately authorized and is closed below. P7 remains unauthorized/unstarted.**

### P6 - production overhaul: bloat, scaling, synchronization and test leakage

- Re-read the integrated 10.4.1-10.4.6 production path against the P6 contract
  above and exact-current RED ownership shapes.
- Produce a touched-production manifest and a complexity/synchronization ledger for
  its material high-cardinality paths. Each candidate is explicitly classified as
  retained-with-bound/evidence, corrected-with-proof, or outside the scoped
  streaming-to-render ownership path; do not leave “looks suspicious” findings
  unresolved.
- Remove redundant integration state/helpers/indirections whose only purpose has
  disappeared after ownership converged. Preserve explicit Vanguard safety
  contracts; this is structural cleanup, not code golf.
- Correct material high-cardinality problems discovered by evidence: accidental
  full-capacity/live-set scans in hot/replacement paths, O(N^2) mass operations,
  lock-per-element high-count loops where an owner-local batch is valid, and
  avoidable per-element allocation/churn. Record why every retained large scan or
  synchronization point is necessary.
- Recheck the earlier rendering-component performance-preflight wins and the
  definition-time `RenderSceneGpuPublisher::DetachScene()` capacity scan.
- Add focused operation-count/cardinality/lock-batching proofs for corrected paths;
  do not claim broad frame performance yet.
- Perform the test-support isolation audit across production source and product
  Premake targets; move/delete any leaked proof/fixture/phase artifact rather than
  normalizing it as product behavior.
- Fresh independent exact-CURRENT architecture/performance/hygiene review must
  return 0 BLOCKER / 0 HIGH / 0 MEDIUM.
- Stop.

P6 closure evidence — 2026-09-28:

- `RenderingRuntime::pendingRetirements` is retained as the exact bounded pending
  work list rather than replaced by a second due scheduler. Its configured bound is
  65,536, the default retirement delay is two frames, traversal is O(R) over pending
  ownership with no per-record lock/allocation, successful removals are O(1), and
  `remainingFrames` and `acceptedProgressEpoch` encode distinct delay/acceptance
  semantics. Static evidence did not justify another timer/retirement owner.
- `MeshResidencyManager` now traverses pre-reserved dense active/retiring indices
  instead of the residency high-water table, uses O(1) dense-list and same-path
  backlinks and batches candidate capture/commit under bounded lock sections. The
  start/progress/retirement scratch records are ownership-free and statically required
  to remain trivially copyable. Fresh resource retention occurs before reacquiring the
  mesh lock and is revalidated on commit; record removal only move-extracts the exact
  strong resource handle under the lock and releases it afterward. Uploader,
  definition, material, allocator and ResourceRegistry ownership operations therefore
  stay outside the global residency lock. Exact generation/work/demand is revalidated
  before mutation. Real diagnostics expose active/retiring counts and progress/
  retirement visits. The focused source proof reaches three same-path generations,
  removes middle/head records, proves recycled-generation invalidation and then
  measures two progress visits with one live record after high-water three plus one
  visit for the sole retiring record.
- `RenderSceneGpuPublisher::DetachScene()` now uses its maintained exact tracked
  instance/light/decal counts after the existing phase/dirty/retirement checks rather
  than scanning up to the configured proxy-slot capacity. `QueueDirty` is an
  infallible internal transition rather than carrying dead rollback branches. The
  D3D12 fixture drives instance/light/decal admission and retirement through public
  `RenderSceneManager` APIs before final scene detach.
- `CellMaterializer` required-local/parent closure and activation-group closure now
  use indexed worklists over newly selected placements/groups and authored direct
  ranges instead of repeated whole-cell fixed-point scans. Production diagnostics and
  focused tests record exact placement/group/reference visits for those closures.
- `EntityReferenceRegistry::RegisterPartition` now uses transient indexed entity
  membership, prepared-source lookup and `(source, slot)` duplicate detection rather
  than growing linear membership/source searches. Registration diagnostics record
  one authored entity/reference/slot admission visit per corresponding record.
- The production-only-for-tests `MaterialMaterializerAccess::BeginResolved` friend,
  private header and implementation were removed. Material materialization coverage
  now enters through the real public production path. Scoped production comments were
  also rewritten to remove phase/proof history, while product Premake ownership of
  `tests/support` remains isolated to explicit test projects.
- P6 has remained source/static only. No Premake generation, compilation, executable
  test, cooker, runtime/editor/native execution or product launch has been performed;
  those executable acceptance steps remain P7 work unless separately authorized.
- The whole-P6 exact-current read-only review found one final MeshResidency HIGH:
  `ResourceHandle` retain/release work could still nest ResourceRegistry ownership
  beneath the mesh spinlock. After the ownership-free scratch/move-extract correction,
  a fresh exact-current post-fix recheck returned **0 BLOCKER / 0 HIGH / 0 MEDIUM**;
  all other whole-P6 areas had already been reviewed clean.

**P6 is CLOSED. P7 was later explicitly authorized and is active below.**

### P7 - batched validation and 10.4.6 closure

**Superseding validation rule:** root `AGENTS.md` now requires the final gate to leave
the relevant source compiling and run the smallest focused proof tests necessary for
closure. Broad matrices, unrelated regressions and native/UI acceptance require
separate explicit authorization. The older checklist below is retained as historical
planning context; rows beyond the focused final-gate proof are not closure requirements.

- Fresh Premake generation and engine-service/project audit.
- Serialized VS2022 x64 Debug and Development focused builds for every touched
  owner and deterministic proof target.
- Execute the P5/P6 deterministic matrices in both configurations.
- Build/run the normal cooked package path and require the previously failing
  visual-component world/session teardown to exit cleanly through ordinary stop;
  the camera-only 10.4.5 fixture is not sufficient evidence for this gate.
- Run runtime/editor native lifecycle, world replacement and final-shutdown smokes,
  warning before any interactive input-driving runner is used.
- Execute focused cardinality/scaling/operation-count checks for P6 corrections;
  report measured scope honestly. Broad 10.4.7 CPU/GPU performance acceptance
  remains deferred.
- Run scoped ownership/hygiene/dependency checks and fresh exact-CURRENT RED
  comparison for any production architecture changed beyond the definition-time
  contract.
- Final independent exact-CURRENT implementation/evidence review must return
  0 BLOCKER / 0 HIGH / 0 MEDIUM before 10.4.6 closes.
- Update durable docs and stop before 10.4.7.

P7 execution evidence — 2026-09-28:

- Fresh `generate-vs2022.bat` passes the Vanguard engine-service audit and VS2022
  generation on the exact-current shared checkout.
- Serialized x64 Debug and Development focused builds are green for the independent
  10.4.6 owner/proof/product targets, including `resourcePipelineTests`, `ecsTests`,
  `gameWorldTests`, `entitiesTests`, `renderingTests`, full `geometryAllocatorTests`,
  `rhiNvrhiTests`, `engineServicesTests`, `applicationTests`, `runtimeViewportTests`,
  `runtimeWorldFrameIntegrationTest`, `editorSceneOutputTests`, `bootstrapImage` and
  `runtime`. Exact-current dependency changes were followed by focused relinks rather
  than a whole-solution rebuild.
- The complete deterministic P5/P6 executable matrix is green in both configurations.
  Full `geometryAllocatorTests` executes the P6 MeshResidency cardinality/operation-
  count proof, and `rhiNvrhiTests` executes against the real D3D12 backend.
- A P7 test-fixture defect found by execution was corrected without changing product
  ownership: the device-abandonment TransformRuntime fixture now allocates the exact
  required transform capacities, and its local RenderScene update `jobs::Builder` is
  scoped before managed Jobs shutdown and waits only when asynchronous scene work was
  dispatched. `engineServicesTests` is green in Debug and Development afterward.
- Standard `bootstrapImage` cooks into fresh P7-owned roots in both configurations and
  reports `P4 fixture artifacts validated` plus `catalog=Success entries=18 resources=47`.
  The normal package contains the static mesh, camera and directional-light path; no
  camera-only acceptance substitute is used.
- `VanguardRuntimeWorldFrameIntegrationTest.exe --validate-world-frame` and
  `--validate-world-replacement` both exit 0 in Debug and Development from contained
  executable roots with the standard `DATA000.vpak`, using the real D3D12 backend.
- `git diff --check` exits 0 apart from expected line-ending warnings; scoped production
  scans find no conflict markers or phase/proof/agent history wording. The sole native
  `rhi::Present` call remains in `ViewportManager`, no stop/drain path calls
  `GameWorld::Tick`, and the two engine detail headers remain under `private/`.
- Exact-current Debug `editorApplicationLifecycleIntegrationTest` is presently blocked
  before link by the separate active E3.6 editor-preference work (`editor_commands.hpp`
  declaration ordering, command-service const correctness and user-state path APIs).
  Those files are outside 10.4.6 and are deliberately not patched here. Under the
  superseding root validation rule, the additional editor ordinary-stop/native-UI rows
  are **DEFERRED / NOT REQUIRED FOR 10.4.6 CLOSURE** rather than blockers.
- The final focused closure evidence is therefore sufficient: relevant exact-current
  source compiles in Debug + Development, the session/replacement/drain proof owner is
  green in both configs, P6 ownership/cardinality proofs are green, standard runtime
  package/replacement execution is green, hygiene is clean, and the independent final
  source review is **0 BLOCKER / 0 HIGH / 0 MEDIUM**. **P7 and 10.4.6 are CLOSED.**

## Definition acceptance

The Definition gate may close only when an independent review confirms that it
unambiguously establishes:

- one asynchronous stop transaction with explicit Pending/failure semantics;
- first-terminal-fault latching that keeps cleanup progressing until accepted
  ownership is quiescent/terminalized, with replacement admitted only from `Idle`;
- one renderer-owned narrow drain/progression path using existing publication,
  residency and GPU-retirement owners, including one renderer-owned progress epoch
  if normal application/game frame identity no longer advances;
- an absolute ban on using `GameWorld::Tick` or a recursive gameplay/application
  frame as terminal-drain progress;
- reverse ownership for component/scene/world/source/output teardown and clean
  replacement sequencing;
- preservation of 10.4.5 Present, CPU-tail, output-generation and camera authority;
- a dedicated P6 cleanup gate with evidence-based complexity/synchronization
  criteria and explicit RED-like/Vanguard-safe structural goals;
- production/test separation as a release invariant rather than an optional tidy-up;
- P7 as the only broad build/native validation gate; and
- 10.4.7 remains unstarted/outside this contract.

No production implementation, Premake generation, compilation, executable test or
native launch is authorized by this Definition gate alone.

Definition closure evidence: the final post-correction independent review returned
**0 BLOCKER / 0 HIGH / 0 MEDIUM** on the durable plan, planning triplet, development
README and integration study. Closure used source/document inspection only; no
10.4.6 production implementation or executable validation was performed.
