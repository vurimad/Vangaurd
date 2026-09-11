# Vanguard Render Graph Execution Plan

Date: 2026-09-03

Status: preserved former V1 plan; non-authoritative until revision R9 regenerates it

Authority: superseded by `render-graph-design.md` Sections 42 onward

## 0. Active Revision Override: Same-Frame Graph Presentation

Any later section in this preserved plan that describes publishing
`PresentReady` to a main-thread mailbox or presenting during the next frame
boundary is superseded. Present is a frame-global terminal node in the current
frame's Render Graph. Main-thread output acquisition and lifecycle reconciliation
are serialized through a per-viewport operation gate with worker-side
Present/Abandon/DeviceLost ticket consumption. See
`render-graph-design.md` Section 44. R9 must rewrite the affected stages before
this file becomes implementation authority.

## 1. Objective And Boundary

Implement the complete baseline Render Graph as one reviewable project, then
begin renderer pass/content work. "Complete baseline" means:

- a real resource-centric compiler, not a temporary node list;
- a bounded renderer-owned cache of immutable compiled structural templates with
  explicit revision keys and frame-local instance binding;
- exact resource-version dependencies, roots, culling, and stable ordering;
- one canonical use IR driving the allocator and executor;
- real allocator resolution, command recording, submission, and terminal
  receipts;
- one retained output transaction with truthful Present/Abandon/device-loss
  behavior;
- the current supported Graphics/Compute lowering and explicit rejection of
  unsupported Copy crossings;
- Debug and Shipping coverage of all critical failure paths.

It does not mean that every later optimization is mandatory. Parallel
recording, aggressive pass merging, range-precise dependency pruning, and
generic Copy-queue waits are explicit post-baseline work.

No production stage begins merely because it appears in this file. Each stage
requires review of the previous correction gate and explicit user approval.

## 2. Non-Negotiable Contracts

1. `FrameRenderer` is the graph runtime owner; `RenderCommandSystem` remains the
   only CPU render-chain owner.
2. One sealed typed-use IR is the source for dependencies, allocator planning,
   runtime packet replay, and callback bindings.
3. Every resource write produces an explicit content version. Every root names
   an exact version.
4. Culling and executable queue validation finish before allocator `Resolve`.
5. Every surviving pass initially maps one-to-one to allocator node, flow group,
   command scope, packet, callback, and scope receipt.
6. The abstract scope DAG has Graphics, Compute, and Copy. The executable
   lowering admits only patterns the current RHI can actually synchronize.
7. An allocator session that publishes an execution generation reaches
   `Finish` exactly once. A pre-publication session reaches
   `CancelBeforePublication`.
8. Full `rhi::SubmissionReceipt` evidence is retained. Submitted work is never
   relabeled as discarded because fence signaling subsequently failed.
9. Resolved resources and packet cursors never escape their synchronous pass
   callback or migrate between command-list threads.
   The callback is `noexcept` and returns an explicit typed status; failure is
   never inferred from exceptions or mutable global state.
10. Main-thread output acquisition has exactly one terminal disposition:
    PresentReady, TextureReady, AbandonSafe, or DeviceLost.
11. The graph extends `RenderFrameContext::GetJobs()` and never creates a second
    renderer scheduler or detached completion chain.
12. Production `FrameRenderer` keeps its explicit missing-graph failure until
    the end-to-end output stage passes.
13. `FrameRenderer` owns a bounded immutable structural-template cache. Cache
    hits never reuse frame-local payloads, imports, view IDs, output tokens,
    allocator generations, or execution state.

## 3. File Strategy

Begin compactly and split only at ownership boundaries:

```text
source/rendering/include/vanguard/rendering/render_graph.hpp
    public handles, descriptors, builder, pass declaration/context, failures,
    compiled-graph reference and read-only diagnostics

source/rendering/private/vanguard/rendering/render_graph_internal.hpp
    owned records, canonical typed-use IR, versions, edges, roots, scopes,
    retained execution state

source/rendering/src/render_graph.cpp
    public lifetime and builder operations

source/rendering/src/render_graph_compile.cpp
    validation, hazards, cycles, culling, topo order, scope DAG, queue lowering

source/rendering/private/vanguard/rendering/render_graph_cache.hpp
source/rendering/src/render_graph_cache.cpp
    bounded immutable-template lookup, atomic publication, eviction,
    invalidation, and frame-instance binding

source/rendering/src/render_graph_resource_adapter.cpp
    canonical-IR replay into RenderFlowResourceAllocator

source/rendering/src/render_graph_execution.cpp
    packet replay, command-list guard, submission, terminal transaction

source/rendering/tests/render_graph_tests.cpp
    CPU compiler, ownership, failure, adapter, and façade tests
```

Do not create public node base classes, a separate graph module, one file per
tiny record, or a new test executable initially. The required cache remains a
private `FrameRenderer` implementation detail. `renderingTests` already owns
deterministic and logical-only coverage. Native RHI conformance is added to the
existing `rhiNvrhiTests`; service integration belongs in `engineServicesTests`
and the existing real-device service tests.

## 4. Stage RG1 — CPU Graph Compiler

### Purpose

Build and exhaustively test the graph model without touching the allocator,
RHI command lists, viewport, service lifecycle, or `FrameRenderer`.

### Files

Add:

```text
source/rendering/include/vanguard/rendering/render_graph.hpp
source/rendering/private/vanguard/rendering/render_graph_internal.hpp
source/rendering/private/vanguard/rendering/render_graph_cache.hpp
source/rendering/src/render_graph.cpp
source/rendering/src/render_graph_compile.cpp
source/rendering/src/render_graph_cache.cpp
source/rendering/tests/render_graph_tests.cpp
```

`source/rendering/premake5.lua` already includes these glob patterns; change it
only if target routing proves otherwise.

### RG1A — Ownership, handles, and canonical declarations

Implement:

- configured exact budgets for passes, resources, versions, uses, edges,
  scopes, roots, owned payload/callback bytes, aggregate owned text, and fixed
  per-name/reason/message lengths;
- generation-checked pass/texture/buffer/view handles;
- owned copied names and descriptors;
- move-only builder with Building and Sealed phases;
- a template-owned pass-data schema with explicit move/destroy operations and a
  noncapturing `noexcept` function pointer/thunk, plus a separate frame-instance
  owned payload value; arbitrary capturing lambdas are rejected by the API;
- an explicit `RenderGraphPassStatus` return contract with a typed code, stable
  copied message, and optional copied nested RHI evidence;
- direct reuse of allocator frame-resource/use/view descriptors and RHI
  state/queue vocabulary rather than duplicate graph enums;
- canonical typed texture/buffer use records;
- texture/buffer views as descriptor-and-range projections of an exact parent
  version, with view writes producing the next parent version rather than an
  independent resource lineage;
- typed pass-local binding keys; runtime resources are resolvable only through
  a declared binding, never through a bare graph resource handle;
- imported defined version zero, clear-initialized created version zero, and
  undefined created version zero;
- write and read-modify-write version production;
- exact-version Present and ExternalWrite roots;
- named SideEffect roots and separately typed explicit GPU-order edges.

The public surface must make dynamic resource mutation inside a recording
callback impossible. Do not expose allocator IDs or RHI command lists through
the builder.

### RG1B — Compiler

Implement:

- validation of all handles, descriptors, versions, callbacks, and roots;
- whole-resource RAW, WAR, and WAW dependency derivation;
- separate liveness propagation from ordering-only anti-dependencies, then
  rebuild hazards over the surviving access history;
- duplicate/overlapping same-pass use normalization and rejection;
- full candidate-graph cycle detection;
- reverse reachability and dead-pass/resource culling;
- exactly one frame-output root when an output ticket exists, followed by an
  internal epilogue edge from every other surviving pass to that terminal output
  pass and another pass/scope cycle check;
- deterministic stable topological sort;
- compiler-internal prologue/epilogue sentinels where needed;
- one-pass, queue-homogeneous command scopes;
- independently constructed/deduplicated/cycle-checked scope DAG;
- abstract three-queue schedule;
- current executable-lowering validation without calling the allocator;
- stable text dump and optional DOT serialization.

Core graph walks use indexed adjacency/indegree tables and are linear in passes
plus edges after bounded sorting. Pairwise duplicate searches in pass, resource,
or edge hot paths are not accepted merely because the initial test graphs are
small.

### RG1C — Structural template cache and frame-instance binding

Implement:

- a fully comparable `RenderGraphTemplateKey` whose hash is an accelerator, not
  equality proof;
- explicit renderer, feature, queue-policy, rendering-mode, frame-purpose,
  output, ordered view-slot, camera-dependency, and structural-decision revision
  fields;
- bounded cache capacity and deterministic LRU replacement;
- immutable reference-counted templates that survive lookup eviction while a
  frame instance retains them;
- cache-miss construction outside the cache lock and atomic publication only
  after successful seal and compile;
- frame-local instance binding for prepared view slots, owned pass payloads,
  imports, output acquisition, and allocator generation;
- exact validation that every required template slot is bound once and no
  instance operation changes passes, uses, versions, roots, edges, or queues;
- instance-scoped allocation/failure injection for lookup, miss build,
  publication, retention, and binding;
- explicit invalidation and `ClearPersistentCaches` integration at the
  renderer's quiescent lifecycle boundary.

The cached template may retain canonical structural compiler products. It must
not retain frame-local payload values, view IDs, native resources, output
tokens, allocator IDs, receipts, or mutable terminal state. Instance binding is
not a second resource-planning callback.

### Required tests

- RAW, WAR, WAW, read-read, read/write, and read-modify-write;
- imported and clear-initialized version zero, invalid first read of an
  undefined transient, duplicate producer, and stale/cross-generation handles;
- exact-version output: an earlier version roots only its required chain and a
  later unreferenced version can be culled;
- an ExternalWrite root must name the final externally observed version of its
  imported lineage;
- a rooted discard overwrite does not retain an otherwise dead prior writer,
  and a WAR edge does not retain an otherwise dead reader;
- explicit, resource-derived, and mixed-edge cycles, including a dead cycle;
- dead branch removal, named side-effect retention, and no implicit side-effect
  ordering;
- same-pass compatible-read normalization and conflicting-overlap rejection;
- RTV/SRV/UAV and buffer views of one parent derive hazards through the same
  lineage; stale-version view writes fail, while disjoint views remain
  conservatively ordered in the baseline;
- deterministic pass/scope order across repeated compiles and randomized
  internal backing-storage layout while public declaration ordinals are held
  fixed; changing declaration order follows the documented ordinal tie-break;
- every initial scope contains exactly one pass and the independently built
  scope DAG preserves the live pass DAG without a cycle;
- valid isolated Graphics/Compute/Copy schedules;
- valid balanced Graphics-to-Compute-to-Graphics region;
- nested, overlapping, fan-in/fan-out, and cross-queue Copy rejection;
- builder destruction before compiled-graph destruction;
- payload destruction exactly once after survival, culling, compile failure,
  and builder move;
- reference-capturing callback rejection and pass-data move/destroy coverage;
- callback status construction copies short and nested-RHI diagnostics without
  borrowing caller storage;
- fixed-budget, arithmetic-overflow, allocation, and instance-scoped provider
  failure paths, including aggregate/per-string text exhaustion and callback
  diagnostic capture charged to the configured budgets.
- cache hit/miss and full-key equality under deliberate hash collision;
- mode, purpose, output, view-layout, camera-dependency, queue-policy, and
  revision changes produce misses or explicit invalidation;
- failed miss construction/publication leaves the old cache and current frame
  ownership unchanged;
- eviction cannot invalidate an instance retaining the evicted template;
- two instances of one template have independent payloads, imports, view-slot
  mappings, allocator generations, receipts, and terminal outcomes;
- missing, duplicate, stale, or topology-mutating instance bindings fail in
  Debug and Shipping;
- bounded LRU replacement and quiescent `ClearPersistentCaches`/shutdown.

### Exit gate

`renderingTests` passes in fresh Debug and Shipping builds. A static audit
confirms that no compiled object borrows frame-pool or builder storage, no cache
entry retains frame-local state, and no registration-order edge exists outside
the documented version rule and stable tie-break.

### RG1 correction pass

Review in this order: dependency correctness, ownership/destruction, Shipping
validation, algorithmic complexity, duplicate data, then naming. Remove any
abstraction that has no consumer in RG2-RG5. Do not proceed with ambiguous
multiple-write or output-version semantics.

## 5. Stage RG2 — Allocator Planning Adapter

### Purpose

Prove that the real compiled graph, not a hand-written test schedule, drives the
existing allocator contract. Use logical-only allocator mode with created
transient resources first; do not record or submit commands. Successful retained
imports require authoritative RHI descriptors/native resources and move to RG3.
Generic Export and Readback delivery are post-baseline because neither yet has
a truthful graph-facing result owner.

### Files

Add:

```text
source/rendering/src/render_graph_resource_adapter.cpp
```

Modify only as required:

```text
source/rendering/private/vanguard/rendering/render_graph_internal.hpp
source/rendering/src/render_graph.cpp
source/rendering/tests/render_graph_tests.cpp
```

Do not widen `RenderFlowResourceAllocator` to duplicate graph topology or
content versions.

### Work

- begin one allocator session per executable graph frame;
- assign deterministic allocator node/flow/scope IDs to every live pass;
- create exactly one planning writer per live pass;
- map graph resource lineages to copied generation-unique
  `LogicalResourceKey` values;
- replay the canonical use list, including views, content intent, state, and
  exact Begin/End ordering;
- seal, validate the already-compiled schedule, and Resolve;
- pass no creation-job builder in RG2, keeping publication synchronous;
- retain the generation and expose read-only packet mapping to the future
  executor;
- after packet assertions, terminalize every successfully published logical-only
  session as `Aborted` with `CompletedSynchronously(generation)` and exactly
  one `DiscardedBeforeSubmission` receipt for every scope and queue dependency;
- reject a graph that requires retained imports before entering/publishing a
  logical-only allocator session;
- cancel all pre-publication failures without mutating a prior generation.

### Required tests

- culled passes create no planning writer and no packet;
- every live pass maps to exactly one node, flow group, scope, and packet;
- graph use order equals compiled packet step order for texture, buffer, view,
  read, write, read-modify-write, and clear/discard;
- content versions of one graph resource map to one allocator logical lineage;
- retained-import graphs fail the RG2 capability preflight without querying the
  RHI or leaving an allocator session active;
- graph stable order agrees with allocator flow/scope stable order;
- bad graph IDs never reach allocator APIs;
- every adapter failure before publication returns allocator state to Idle;
- destroy the original builder and reset Frame pools before inspecting the
  retained compiled generation;
- successful packet inspection ends with allocator state Idle and no published
  generation; dropping a Ready session is not used as cleanup;
- dedicated-resource and placed-resource planning remain allocator-owned.

### Exit gate

Fresh Debug and Shipping `renderingTests` pass in logical-only mode. Existing
allocator test suites remain unchanged and passing. No RHI device or command
list is required for the gate.

### RG2 correction pass

Compare the canonical graph uses, writer replay, compiled packet steps, and
callback binding slots record-by-record. Any duplicated planning callback,
inferred access, or second resource-state planner blocks RG3.

## 6. Stage RG3 — Graphics-Only Serial Offscreen Executor

### Purpose

Run an actual graph through native allocation, packet execution, submission,
and allocator terminal completion without viewport or swap-chain complexity.

### Files

Add:

```text
source/rendering/src/render_graph_execution.cpp
```

Modify:

```text
source/rendering/private/vanguard/rendering/render_graph_internal.hpp
source/rendering/include/vanguard/rendering/render_graph.hpp
source/rendering/tests/render_graph_tests.cpp
source/rendering/tests/render_flow_resource_allocator_tests.cpp  only if a
    missing allocator contract test belongs there rather than graph tests
```

Native backend tests remain in:

```text
source/rhi/nvrhi/tests/d3d12_backend_tests.cpp
source/rhi/nvrhi/tests/render_graph_execution_tests.cpp
```

Add the second file as a callable test group and invoke it from the existing
`rhiNvrhiTests` main. Do not add another executable or place native D3D12 graph
tests in logical-only `renderingTests`.

### Work

- add one retained execution envelope and stable first-failure latch;
- register only surviving retained imports before creating any allocator
  planning writer, now that a real RHI device is present;
- retain each ExternalWrite owner through terminal commit/abort and publish its
  actual terminal queue fence without implying CPU-readable GPU completion;
- keep command recording serial and Graphics-only for the first executable
  vertical slice;
- keep allocator native creation synchronous by passing no creation-job builder;
- create one command list per scope;
- add an RAII recorder guard that requires an empty TLS binding, binds exactly
  once, and unbinds before every submit/discard/return;
- open each allocator packet, bind uses in canonical order, call the pass once,
  end uses in fixed reverse order, and finalize the packet;
- if the callback returns failure, copy its status, cancel the packet, invalidate
  bindings, unbind/discard the current list, classify every remaining scope, and
  terminalize from the truthful prior-submission evidence;
- flush required barriers through existing RHI/allocator actions;
- submit with full `SubmissionReceipt` and derive the scope's queue fence from
  `receipt.residency`;
- build every command-scope and queue-dependency receipt exactly once;
- use synchronous terminal-join proof for the serial path;
- call allocator `Finish` exactly once for Completed, Aborted, and DeviceLost;
- publish terminal commit/abort hooks only after the terminal outcome is known.

### Required failure injection

- retained-envelope, command-list creation, bind, cursor open, begin use,
  callback, end use, packet finalize, barrier flush, close, and submit;
- retained-import descriptor query and registration;
- failure before native submission discards and reports Aborted;
- failure after one or more scopes submitted retains every real fence;
- false submit with `WasSubmitted()==true` reports DeviceLost and
  `UnknownDueToDeviceLoss`;
- allocator `Finish` failure is captured once, performs no success commit, is
  not retried, releases the published session into allocator fail-closed
  `DeviceUnavailable`, and requests device recovery;
- every injected path leaves no bound command list, no executing cursor, and no
  allocator session in Planning/Ready/Executing;
- late child failure is published exactly once through stable storage;
- callback cannot retain or use a resolved reference after `EndUse`;
- payload destructor and terminal hook each run exactly once.

### End-to-end tests

- create/write -> read -> named GPU side effect plus a dead branch;
- retained imported texture with truthful initial and terminal state and an
  exact final ExternalWrite root;
- ExternalWrite commit receives the real terminal queue fence, while abort
  publishes no false readiness;
- a culled/dead import is never registered or queried through the RHI;
- two non-overlapping transients demonstrate allocator-owned dedicated reuse;
- enabled placed resources execute the allocator's alias activation path;
- abort before first submission, abort after partial submission, and device
  loss after native execution;
- builder and Frame-pool storage die before the retained execution completes.

### Exit gate

Fresh Debug and Shipping `renderingTests`, `geometryAllocatorTests`, `rhiTests`,
and `rhiNvrhiTests` pass.
The native test may skip only for an explicitly unsupported adapter/profile.
No viewport, swapchain, or `FrameRenderer` production path is changed.

### RG3 correction pass

Audit the terminal state machine and every early return. The decisive questions
are: was work submitted, which queue fence proves it, who owns the command
list, and did the allocator receive exactly one terminal call?

## 7. Stage RG4 — Frame Output And Frame Integration

### Purpose

Connect the complete serial graph to a real retained frame and output. This is
the first stage allowed to remove `FrameRenderer`'s missing-executor failure.

### RG4A — RHI presentation terminal corrections

Modify:

```text
source/rhi/nvrhi/src/d3d12_backend.cpp
source/rhi/nvrhi/tests/d3d12_backend_tests.cpp
```

Require a valid Graphics fence before acknowledging a submitted acquired-buffer
transition. When D3D12 marks a swap chain Failed after current-buffer
validation, native Present, or presentation-fence signaling fails, consume the
active acquisition and reset its transition acknowledgement so recovery cannot
strand a live token. Prove:

- transition -> discard -> Present rejected;
- transition -> post-execution signal failure -> Present rejected and device
  recovery required;
- transition -> valid Graphics submission -> Present accepted.
- native Present backend failure consumes the acquisition and requires
  swap-chain recovery;
- presentation-fence signal/device failure consumes the acquisition and
  requires device recovery.

This correction is a release blocker for graph presentation, not an optional
cleanup.

### RG4B — Specialized allocator presentation import

Modify:

```text
source/rendering/include/vanguard/rendering/render_flow_resource_allocator.hpp
source/rendering/private/vanguard/rendering/render_flow_resource_internal.hpp
source/rendering/src/render_flow_resource_allocator.cpp
source/rendering/src/render_flow_resource_resolve.cpp
source/rendering/src/render_flow_resource_execution.cpp
source/rendering/tests/render_flow_resource_allocator_tests.cpp
```

Add a narrow retained-presentation texture import carrying the exact immutable
`rhi::AcquiredBackBuffer`. Validate matching texture/token and fixed
Present-to-Present Graphics same-queue semantics. Compile one
`SwapChainPresentTransition` action at the terminal use; do not emit a generic
terminal texture transition. The allocator records the special transition but
never acquires, presents, or abandons.

Test invalid/stale token, mismatched texture, wrong queue/state, duplicate
terminal action, incomplete packet, discard, submitted receipt, and device
loss.

### RG4C — Frame-output ticket and main-thread drain

Modify:

```text
source/rendering/include/vanguard/rendering/viewport.hpp
source/rendering/src/viewport.cpp
source/rendering/include/vanguard/rendering/render_command_system.hpp
source/rendering/src/render_command_system.cpp
source/rendering/include/vanguard/rendering/frame_renderer.hpp
source/rendering/src/frame_renderer.cpp
source/engine/include/vanguard/engine/rendering_service.hpp
source/engine/src/rendering_service.cpp
source/rendering/tests/viewport_tests.cpp
source/engine/tests/engine_services_tests.cpp
source/rhi/nvrhi/tests/render_graph_execution_tests.cpp
```

Work:

- make `FrameRenderer` own and configure the bounded structural-template cache,
  construct a complete key from the immutable frame/view/output configuration,
  build and atomically publish misses, then bind a fresh frame instance for
  every submitted frame;
- replace the allocator-only cache-clear façade with one renderer-level
  `ClearPersistentCaches` operation that, after render-tail quiescence, clears
  graph-template lookup ownership and the allocator's persistent caches without
  invalidating retained in-flight references;
- create a move-only/reference-counted frame-output ticket holding the output
  kind, complete `RenderOutputAcquisition`, output revision, real terminal
  queue fence, and one atomic terminal disposition;
- give `ViewportManager` a preallocated pending-ticket registry with at most one
  active output ticket per render viewport; the worker holds a stable
  ticket reference and only atomically publishes disposition, with no manager
  callback or terminal allocation;
- during the Render phase, after `RenderingFrameTick`, let its dependent frame
  source acquire on the main thread immediately before `SubmitFrame`, not inside
  a worker job, and reserve terminal-ticket capacity before acquisition;
- acquire presentation output only when `ShouldPresent()` is true, acquire a
  Texture viewport output for every submitted frame, and create no output ticket
  for a Headless viewport;
- add an explicit Texture-output continuation state/queue to the viewport
  descriptor and snapshot, validate it against the RHI texture, and retain the
  last submitted ready fence for its external owner;
- copy that state, queue, and prior ready-fence metadata into the immutable
  `RenderOutputAcquisition`; let the retained envelope and specialized graph
  import consume only that copied contract;
- render to an offscreen color resource, then use one terminal Graphics pass to
  touch and transition the acquired back buffer;
- publish PresentReady only after valid terminal-scope submission, successful
  allocator `Finish`, and whole-graph terminal commit;
- publish TextureReady with the real terminal queue fence only after a
  successful ExternalWrite, allocator `Finish`, and whole-graph terminal commit;
- publish AbandonSafe only when no submitted work touched the acquisition;
- publish DeviceLost when work may have submitted without usable completion;
- after the CPU tail joins, drain tickets at the main-thread RenderUpdate
  boundary before publishing a late failure that could skip later frame phases;
  consume each exactly once through `ViewportManager::Present`, a new narrow
  main-thread `CompleteOutput` operation for successful non-back-buffer Texture
  output, or `AbandonOutput`;
- if Present fails, clear the caller ticket and route swap-chain/device recovery
  according to the nested RHI failure; never retry the same token or abandon a
  back buffer whose terminal transition was submitted;
- on recoverable backend Present failure, unbind/release the failed swap chain,
  increment output revision, enter `AwaitingOutput`, and recreate/rebind on the
  next Presentation tick; device loss uses device abandonment;
- perform the same post-join settlement from explicit
  `EngineViewport::FlushFrame`;
- add a main-thread viewport device-loss consumption path that clears the
  logical acquisition and marks the output failed without calling RHI Present
  or Abandon, before renderer device abandonment owns native cleanup;
- flush and settle all tickets before resize, rebind, viewport destruction,
  graph/runtime shutdown, or RHI shutdown;
- make `RenderingServiceImpl` the single owner of
  `rendering::PresentationService`, add an optional `WindowService` dependency,
  expose presentation output access through `RenderingService`, and register a
  no-op-capable main-thread Presentation-phase reconciliation participant;
  headless and device-disabled profiles leave it inactive, while graph terminal
  ticket draining remains at the earlier RenderUpdate boundary;
- route late graph failure to the existing command-system latch;
- expose that route as a small copyable execution-failure sink on
  `RenderFrameContext`; the retained envelope copies the sink and never retains
  the stack context or reaches into command-system internals;
- commit prepared view-family/history state only on successful graph terminal
  commit;
- retain a renderer/device-wide accumulated last-submitted fence set separately
  from exact per-frame receipts.

Only after the complete matrix passes may `FrameRenderer::RenderFrame` replace:

```text
FrameRenderer has no installed Render Graph executor
```

with graph build/compile/execute dispatch.

### Required tests

- callback returns after spawning graph descendants; descendants safely retain
  frame/view/custom data and publish one late failure observed after Flush;
- repeated structurally identical frames hit one immutable template while using
  different view mappings, pass payloads, imports, output tickets, allocator
  generations, receipts, and terminal outcomes;
- mode, purpose, output structure, ordered view features/dependencies, or any
  renderer/feature/queue-policy revision changes the full template key and
  cannot produce a stale cache hit;
- successful acquisition is presented exactly once;
- a Texture viewport frame imports and externally writes its retained texture,
  publishes its real terminal fence, completes exactly once without Present,
  and can run again on its declared continuation queue;
- the imported state, queue, and prior ready fence exactly equal the immutable
  values captured by `AcquireOutput`, even if viewport state changes later;
- an invalid Texture continuation state/queue is rejected at viewport creation,
  and an external consumer is never told that CPU-tail completion means GPU
  completion;
- a presentation ticket with zero, duplicate, foreign, or non-terminal Present
  roots fails compilation and is abandoned exactly once;
- the frame-output pass is the final compiled/submitted scope even when an
  otherwise independent side-effect pass was declared before or after it;
- build, compile, cull/root, allocation, recording, dispatch, and pre-submit
  failure abandon exactly once;
- submitted terminal-scope failure is never abandoned as if untouched;
- DeviceLost consumes and invalidates the ticket without Present/Abandon and
  leaves no acquired viewport state after renderer-device recovery;
- injected native Present and present-fence-signal failures consume the ticket,
  leave no active backend acquisition, and enter the appropriate swap-chain or
  device recovery path;
- a recoverable Present failure invalidates the old swap chain and the next
  Presentation tick recreates it before a fresh acquisition;
- stale output revision rejects terminal consumption;
- no path both presents and abandons, and normal shutdown leaves no acquisition;
- ticket-registry capacity exhaustion is reported before dispatch and never loses
  an already-acquired output;
- output resize/rebind/destruction while outstanding fails or flushes according
  to the documented API contract;
- allocator `Finish`, view-family commit/release, payload release, and output
  disposition each occur exactly once and independently;
- RenderUpdate drains the previous tail, Presentation reconciles outputs,
  RenderingFrameTick runs in Render, and only then may a dependent frame source
  acquire and submit; a phase-order regression fails deterministically;
- device-disabled and headless profiles continue to work without manufacturing
  a swapchain transaction;
- a presentation viewport frame with `ShouldPresent()==false` does not acquire a
  back buffer or require a Present root.

### Exit gate

Fresh Debug and Shipping builds and runs pass for `renderingTests`,
`geometryAllocatorTests`, `rhiNvrhiTests`, `engineServicesTests`,
`materialRuntimeServiceTests`, and `textureResidencyServiceTests`.
`viewport_tests.cpp` remains part of `renderingTests`; no new executable is
created. Manual smoke validation presents consecutive frames, resizes,
minimizes/restores, skips a frame, injects a pre-submit abort, and shuts down
with no live acquisition.

### RG4 correction pass

Trace every acquisition from main-thread creation to exactly one terminal
consumer. Trace every retained frame member to destruction. RED and Unreal are
consulted again only to challenge the result; Vanguard's token-aware RHI and
main-thread viewport contract remain authoritative.

### RG4D — Device-wide retirement cutover

Modify the RHI/backend, RenderingService, and the texture, geometry, and GPU
Scene upload producers. Expose a device-wide `ResidencyFenceSet` watermark that
advances only after successful queue signals. Bootstrap nonzero Graphics,
Compute, and Copy values with truthful initialization submissions. Migrate all
three legacy producers from the fence-only submission overload to full
`SubmissionReceipt` handling.

At RenderUpdate, after the previous CPU tail and output reconciliation, sample
the watermark, call `SealResidencyRetirements`, and only then collect. Never use
pre-incremented backend counters or manufacture a missing queue fence. A failed
signal/device loss does not advance or seal the watermark and enters device
recovery.

Tests prove the three-queue bootstrap, monotonic successful-only watermark,
single-queue frames retaining prior values for other queues, seal-before-collect
ordering, and truthful submitted-without-completion handling for all three
legacy producers.

## 8. Stage RG5 — Bounded Async Compute

### Purpose

Enable the exact Graphics/Compute schedule the allocator and current RHI already
support. Do not generalize it into fake arbitrary queue waits.

### Files

Modify primarily:

```text
source/rendering/src/render_graph_compile.cpp
source/rendering/src/render_graph_execution.cpp
source/rendering/private/vanguard/rendering/render_graph_internal.hpp
source/rendering/tests/render_graph_tests.cpp
```

Modify RHI or allocator code only to correct a proven contract defect, not to
hide an unsupported graph shape.

### Work

- lower one balanced, non-nested Graphics -> Compute fork / Compute -> Graphics
  join region;
- keep one pass per scope until receipt behavior is proven;
- submit in the compiled scope DAG order;
- derive each scope's real queue fence from `SubmissionReceipt::residency`;
- generate exact queue-dependency receipts for allocator `Finish`;
- retain serial CPU recording initially; queue concurrency comes from GPU
  submission, not a second CPU scheduler;
- continue to reject Copy crossings, nested regions, unsupported overlap, and
  unrepresentable fan-in/fan-out before Resolve.

### Required tests

- one Graphics -> Compute -> Graphics frame with resource RAW/WAR/WAW edges;
- dead async branch culled before lowering;
- fork producer receives the Graphics residency fence even when aggregate
  completion is Compute;
- join consumer receives the Graphics fence and Compute producer its Compute
  fence;
- abort before fork, between fork/join, after join, and post-execution signal
  failure;
- all invalid region shapes and every cross-queue Copy edge fail with precise
  compile diagnostics;
- isolated all-Compute and all-Copy graphs execute without cross-queue fiction.

### Exit gate

Fresh Debug and Shipping graph, allocator, RHI façade, and NVRHI suites pass.
GPU captures or backend instrumentation confirm that the waits/signals match
the compiled dependency records. No CPU wait is used to disguise missing GPU
synchronization.

### RG5 correction pass

Reconstruct the GPU happens-before proof from receipts alone. If any edge relies
only on pass ordinal, CPU job completion, or coincidental submission ordering
across queues, the stage is incomplete.

## 9. Stage RG6 — Consolidation And Baseline Exit Gate

### Work

- run a bounded dead-code and duplication audit over only the graph, its narrow
  allocator/RHI seams, and frame-output integration;
- consolidate repeated validation/failure policy without hiding stage-specific
  evidence;
- remove fields or abstractions with no baseline consumer;
- verify configured budgets and instance-scoped failure providers;
- inspect hot algorithms for accidental quadratic behavior, especially edge
  deduplication, reverse culling, stable ready sets, and ID lookup;
- verify stable dumps contain every edge reason, resource version, root, culled
  pass, scope, queue, and lowering failure;
- update rendering architecture/readme documentation and the resume checkpoint;
- rerun the complete matrix from freshly generated Premake projects.

### Complete baseline matrix

```text
Compiler
  versions, RAW/WAR/WAW, roots, culling, cycles, deterministic order, budgets

Template cache
  full-key equality, hit/miss, revision invalidation, atomic publication,
  bounded eviction, retained-template lifetime, independent frame instances

Allocator adapter
  surviving imports, external writes, writer replay, packets, dedicated reuse,
  placed activation

Executor
  bind/use/finalize, callback failure, submit/discard, partial submit, signal
  failure, successful or failed Finish exactly once

Output
  Presentation/Texture/Headless, acquire, semantic Present transition,
  Present/Complete/Abandon/DeviceLost, failed Present, resize, shutdown

Queues
  Graphics, isolated Compute/Copy, bounded Graphics/Compute, rejected Copy edge

Lifetime
  builder death, Frame-pool reset, child Jobs, payload destruction, stale IDs
```

Required fresh Debug and Shipping targets:

```text
renderingTests
geometryAllocatorTests
rhiTests
rhiNvrhiTests
engineServicesTests
materialRuntimeServiceTests
textureResidencyServiceTests
```

Development and Profile configurations must compile before final acceptance.
D3D12 tests may cleanly skip only when the environment genuinely lacks the
required backend/adapter.

### Baseline completion definition

The Render Graph is complete enough to begin renderer pass work when:

- a real frame reaches a real output through the graph;
- every declared dependency has a proved execution meaning;
- every graph/allocator/output lifetime closes exactly once;
- failure after any partial progress has a truthful recovery path;
- the dedicated and placed allocator paths are both driven by real graph uses;
- unsupported Copy crossings fail before execution with a stable diagnostic;
- structural cache hits reuse only immutable compiler products while every
  frame-local binding and terminal state remains independent;
- no temporary executor, node list, or hidden insertion-order synchronizer
  remains.

At that point the next project is the renderer pass library and render pipeline,
not more allocator or graph foundation.

## 10. Explicit Post-Baseline Work

These items are preserved but do not block moving into renderer content:

1. generic RHI producer-fence/consumer-wait primitives and unrestricted Copy
   crossings;
2. range-precise dependency pruning;
3. contiguous compatible pass/scope merging, gated by a regression where an
   acyclic pass DAG would become cyclic under illegal non-contiguous grouping;
4. parallel command recording using a separate ready graph-work counter;
5. generic allocator Export roots plus a bounded asynchronous result owner that
   always takes publications and preserves their ready fences;
6. Readback roots plus pending-submission, pending-GPU, ready, mapping, and
   result-delivery ownership;
7. richer GPU timing, trace UI, and capture tooling;
8. multi-GPU and backend-specific scheduling.

Each is introduced only with a measured need and without changing the public
resource-version or queue-neutral graph contracts.

## 11. Stage Status

```text
Study  Vanguard + RED + Unreal architecture evidence       COMPLETE
V1     Authoritative Vanguard architecture                 COMPLETE
V2     File-level execution plan                           COMPLETE
RG1    CPU graph compiler                                  NOT STARTED
RG2    Allocator planning adapter                          NOT STARTED
RG3    Graphics-only serial offscreen executor             NOT STARTED
RG4    Frame output and FrameRenderer integration          NOT STARTED
RG5    Bounded async compute                               NOT STARTED
RG6    Consolidation and baseline exit gate                NOT STARTED
```
