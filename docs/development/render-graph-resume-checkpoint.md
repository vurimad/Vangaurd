# Vanguard Render Graph Resume Checkpoint

Date: 2026-09-06

## Current packet lifecycle correction

Phase 3 is now complete at the source-only gate, superseding the pending-Phase-3 status below. The closure traced actual imported `job::Builder` continuation attachment and full-fence dispatch through the frame tail, kickoff, group children/epilogues, terminal cleanup and shutdown paths. No additional broken ordering edge was identified in that bounded review. Cursor ownership documentation and unrun sequential duplicate-claim/move checks were added. See the correction document for the precise RED mapping and the distinction between source closure and deferred runtime validation. No compilation or tests; no new synchronization machinery.

Phases 1 and 2 of the three-phase lock correction are implemented at a source-only gate. Follow the existing RED-style frame tail, kickoff, node continuation and terminal join ordering; do not add a competing synchronization mechanism. The allocator/packet API requires one ordered recording owner and joined teardown. Abandonment/destruction rejects an executing cursor instead of invalidating it beneath recording, and setup-failure cleanup checks its kickoff wait. Phase 2 removed both lifecycle lock fields and all 13 acquisition sites from opening/seeding, cursor cleanup/finalization, terminal validation, and abandonment. State checks, terminal atomic, retained ownership, GPU receipts and unrelated locks remain unchanged. Inactive retained views may still survive a quiescent allocator. Phase 3 correction/closure is next. See [packet-execution-lifecycle-correction.md](packet-execution-lifecycle-correction.md) for evidence, path audit, and exact boundaries. No compilation or tests.

Status: RED-faithful revisions R0 through R9 and stages RG1 through RG7 complete; the RG7C.2 behavioral exit gate passes

## RG6C output and lifecycle closure

`SubmitFrame` now acquires the exact current-frame Texture output or requested Presentation back buffer before dispatch and transfers a move-only `RenderFrameOutputTransaction` into the retained command frame. Synchronous dispatch failures recover the transaction; unconsumed ownership returns the acquisition. The retained transaction reaches every node context, and the translated Present node records terminal-output reachability.

Viewport lifecycle and applied-property mutation are ordered by the serialized render-command tail rather than per-slot operation locks. The exact output acquisition pins the slot and swapchain through native Present, texture completion, abandonment, or device-loss disposition. Its single atomic ownership latch rejects duplicate acquisition without serializing unrelated viewport work. Acquisition and lifecycle entry points remain main-thread-only; terminal output disposition remains worker-safe. Texture output completes after allocator success and camera commit.

The acquired presentation texture is registered directly with the allocator before node declaration. The common-resource node imports it as `FrameOutput`; `RenderNodeEndRender` owns the final Graphics use and the allocator emits exactly one token-aware `SwapChainPresentTransition` action at its use end. The existing final synchronization node submits that scope. Successful allocator completion requires its Submitted Graphics receipt and valid fence before the retained terminal path calls native `Present` on the worker. Discard and post-execution fence-signal failure cannot acknowledge the transition, while abandonment rejects a transition already known to be submitted and falls into recovery. No generic duplicate transition, graph-definition adapter, second scheduler, or next-frame drain was added.

RG6C.3 closes service lifecycle ownership. Viewport shutdown rejects un-abandoned frame construction, drains the command tail without holding a viewport lock, and then proves no retained output or native acquisition survived. After command-system shutdown, the service explicitly releases graph/node cache ownership before device teardown; allocator pools remain a distinct later cleanup step. Allocator shutdown repeats graph-cache clearing as an idempotent safety boundary for device-abandonment and rollback paths, and the explicit service step also covers rendering without an RHI backend. Cached graph storage can no longer survive until after allocator/RHI shutdown. No new coordinator or scheduler was introduced.

RG7A.1 consolidates production source before the integration gate. Failed per-view cache builds now reset their selected entry immediately, so partial graphs and node arenas cannot occupy the cache after a rejected build. Render-node command-list failures and placed/resolve backend failures use the allocator's single context-sensitive RHI classifier. The obsolete phase-branched `RenderNodeGraph::Execute` / `ExecuteParallel` declarations, undeclared implementation path, dead graph debug declaration, and its unused dependency-count helper are removed; resource declaration remains `PrepareResourcesParallel`, while execution remains `RenderNodeJob::RunRenderNodeJobs`. Render-target-binder and global-binding metadata remain intentionally because the imported execution epilogue uses those markers to preserve render-target bindings and restore modified global slots; their Vanguard consumer arrives with the deferred renderer-node binding work. Production source contains none of the removed session, survivor-overlay, public queue-schedule, or graph-definition APIs.

The fresh RG7A.1 consolidation audit removed the unused stored/public `RenderNodeImplContext::IsUniqueNode` property. Node uniqueness is consumed locally by `SetupNodeData()` only to select the frame-global resource-flow namespace; it is not persistent execution-context state. The obsolete API sweep, shared RHI-failure-classifier trace, failed-cache-build rollback, and production-comment policy otherwise remain clean at the source-only gate.

RG7A.2 test migration is complete. `render_flow_resource_allocator_tests.cpp` drives the direct `RenderFlowResourceAllocator` API throughout: deterministic merge/execution, dedicated-resource lifetime, poisoned writers, duplicate declarations, aggregate caps, range/subresource checks, logical state/active-use checks, typed clears, missing-wait rejection, Graphics/Compute fork/join execution, bounded action growth, idempotent cancellation, terminal recovery, and provider requirements. `geometry_allocator_tests.cpp` has migrated every native resource-flow case: dedicated acquisition/reuse, placed execution/readback, Copy fallback, provider-boundary rollback, hard-budget recovery, device-loss completion, native buffer/texture typed-clear action execution, retained imports, terminal exports, and real Graphics/Compute fork/join submission. The import case preserves descriptor/token conflicts, unsupported readiness and Copy continuation, repeated-token coalescing, exact native identity, generation-owned reference release, terminal-state restoration, and rejection of an unused import requiring an unrecordable terminal transition. The export case preserves early-take rejection, allocator-owned buffer and texture publication, imported-resource publication, exact terminal state/queue/fence/descriptors, native-charge transfer, single-consumer ownership, abort non-publication, and duplicate-resource ownership rejection. The cross-queue case now authors Graphics packet, fork boundary, Compute packet, join boundary, and Graphics packet as distinct ordered flow positions; it still performs native submissions and supplies exact command-scope and dependency receipts. Graph culling is represented at the correct seam: the allocator receives only the surviving node's declaration stream, and an unmatched cross-node scope close is rejected atomically without a survivor overlay. Graphics remains the implicit ordinary queue; Compute and Copy cases author balanced `RequestBeginQueue` / `RequestEndQueue` pairs. Tests that supplied reversed or duplicate caller-owned stable-order positions were removed because caller-authored schedules are no longer a valid contract and deterministic writer-order coverage already proves the retained invariant. Session-wrapper destruction cases were replaced by the direct invariants: explicit shutdown rejects a published generation, allocator destruction terminalizes retained packet views safely, and outstanding writers observe owner abandonment. Per-view feature builders remain separate renderer-feature work and their bodies remain deliberately unimplemented. No compilation or tests were run.

The fresh RG7A.2 source audit found no surviving dependency on the removed session wrapper, survivor overlay, graph definition, public queue schedule, or caller-authored stable order. Every allocator-specific test helper is invoked by its target entry point, and the logical and native cases still cover the migration promises above. Native test diagnostics were corrected for the current inline `rhi::Failure::message` buffer contract; they now test for an empty string instead of comparing an array with `nullptr`. This is source inspection only; the RG7 runtime matrix remains pending.

The fresh RG7B.1 dead-state and bounded-validation audit found no additional removable production field. The render-target-binder and global-binding markers look dormant only because their execution-epilogue consumer belongs to deferred renderer-node binding work; the imported implementation proves both semantics, so removing them would cut an intentional port seam. Factory group IDs are checked against the fixed 16-entry storage with Shipping-active fatal validation before indexing. The fixed four-entry cache, full equality after hash acceleration, accepted-frame recency, invalid-until-built publication, failed-build reset, and serialized-tail lifetime remain coherent. Stale documentation promises for configurable cache capacity, private candidate publication, and placeholder queue/backend key fields were removed. No compilation or tests were run.

RG7B.2 closes the source-only execution and lifetime audit. A continuation `jobs::Builder` created from a node `JobContext` attaches its complete child chain to that node's completion counter, so command-list close/submission work and deferred node epilogues cannot be overtaken by dependent nodes or terminal cleanup. Ordered submission continuations preserve real per-scope and cross-queue receipts; a post-execution fence-signal failure remains submitted work with unknown completion and forces device-loss terminalization, while untouched scopes are discarded only after the joined node branch. Native Present consumes the same-frame acquisition outside the viewport lock, and failure recovery consumes that token exactly once. The serialized command tail prevents cache eviction or reset while a cached graph is executing; command-system shutdown drains that tail before graph-cache and allocator/RHI teardown. Allocator publication, abandonment, retained generation references, pool retirement, and shutdown ordering contain no additional source-level ownership gap. Empty diagnostics now select their intended fallback text in frame execution, allocator acquisition/resolve, provider rollback, and rendering-service lifecycle paths instead of publishing an empty message. No compilation or tests were run; the runtime exit matrix remains pending.

RG7C.1 closes compilation and linkage integration. Premake VS2022 regeneration and its engine-source audit pass, and the complete seven-target Render Graph matrix compiles and links in Development, Profile, and Debug using serialized `/m:1` builds. Mechanical corrections were limited to includes, type agreement, existing memory-pool policy, asynchronous per-occurrence context construction, authoring return-value policy, and stale test access to a private allocator schedule. No executable was run; runtime behavior and Shipping exit validation remain pending.

RG7C.2 is complete. All seven executables pass serially in Debug and fresh Shipping builds, with the NVRHI tests using the available real D3D12 adapter. A focused graph suite now proves node identities, separate CPU/GPU dependencies, flow groups, dependency-group boundaries, unique and sequence composition, forced view indices, helper removal, cache hits, hash collisions, unpublished-build retries, LRU replacement, invalidation, and exact parallel declaration coverage at 1021 and 2042 nodes. Shipping exposed and the suite corrected a plain-counter race in its own declaration witness. The RHI presentation test also stopped reusing a previously submitted command list and now records the transition on a fresh list. Viewport lock removal, retained-cache eviction, bounded fatal/recording/submission contracts, and deterministic pre-scheduling capacity failure are complete.

The copied viewport/presentation state model and its compensating per-slot locks are removed. Stable manager-owned viewport and presentation-output objects are accessed directly, applied mutation is serialized after the previous command tail, and the live viewport is retained through `RenderFrameInfo`. Present remains a terminal node in the current graph, and the exact acquired-output transaction remains the NVRHI seam.

RG7C.2A is complete. `ViewportManager` slots now own the canonical non-copyable `RenderViewport` and `EngineViewport` objects at stable addresses; copied manager/handle facades are gone. External handles still resolve with generation checks, but successful resolution returns the owned object pointer. `RenderFrameInfo`, its retained asynchronous copy, and every node context carry the same output object pointer until terminal cleanup. Destruction still requires an idle command tail, no building frame, no engine-output reference, and no acquired output before invalidating the object. Compatibility snapshot queries remain readouts of the object fields and are scheduled for deletion after RG7C.2B establishes the serialized mutation boundary.

RG7C.2B is complete. Viewport application and lifecycle mutation now join the previous render-command tail before changing a manager-owned viewport, while unchanged presentation and render-extent requests remain no-op operations that do not flush. Any applied mutation is rejected after frame construction begins until all building frames are submitted or abandoned. Presentation reconciliation therefore waits for the prior graph's terminal Present instead of using `Busy` as its ordinary resize retry path, and the next frame captures the newly applied dimensions. Same-frame Present and the exact acquired-output transaction remain intact. Focused `renderingTests` and native D3D12 `rhiNvrhiTests` builds and executions pass in Debug and Shipping. The native presentation gate caught and corrected minimized-window suspension in the no-op detector. Snapshot compatibility APIs and per-slot Present synchronization remain intentionally present until their ordered removal passes.

RG7C.2C is complete. Render-frame callbacks and render-node contexts now reach the retained `RenderViewport*` directly. `RenderFrameInfo` no longer carries a duplicate output handle or output kind, while its accepted render/output extents deliberately remain frozen for graph construction. Live applied viewport state is available through narrow object getters. The exact output transaction retains the viewport rather than a manager pointer and copied kind, and graph-terminal abandonment, texture completion, device-loss handling, and Present call the viewport object directly. Debug and Shipping `renderingTests` and native D3D12 `rhiNvrhiTests` all pass.

RG7C.2D is complete. Copied render-viewport, engine-viewport, and presentation-output structures and their query/visitor APIs are removed. Enumeration visits the stable owned objects directly. Presentation reconciliation uses a narrow property request and explicit applied-result flags, while `PresentationService` exposes stable owned outputs and retains the resolved viewport directly. The WindowManager attachment read remains the intentionally bounded window-to-presentation seam. Debug and Shipping `renderingTests` and native D3D12 `rhiNvrhiTests` all pass.

RG7C.2E is complete. Viewport slots no longer contain an operation spinlock or `presentInProgress` state. Lifecycle/property mutation joins the broad command tail, while the exact output transaction and its atomic owned bit provide the only per-output cross-thread ownership handshake. Unchanged requests remain no-flush operations, and requested suspension is main-thread-owned so their comparison never reads worker-written terminal state. The complete seven-target Debug and Shipping build-and-execution matrix passes, including native D3D12 `rhiNvrhiTests` on the available NVIDIA adapter. Remaining RG7C.2 work is limited to the non-viewport controlled fatal/scheduling and retained-cache cases.

RG7C.2F closes retained-cache eviction behavior. A focused concurrent test retains the least-recently-used cached graph through its existing exclusive execution flag, starts eviction on a worker, proves eviction cannot complete while execution owns the graph, releases the terminal ownership, and proves the same cache entry is then reset and reused. No production cache or lifetime mechanism was added. `renderingTests` builds and passes in Debug and Shipping.

The controlled fatal and execution-contract slice is complete. `renderingTests` launches isolated child modes for CPU dependency cycles, GPU dependency cycles, out-of-range dependency-group identifiers, a scheduled node whose execution consumes fewer occurrence ranges than it declared, and an ordered submission boundary whose synchronization kind disagrees with the compiled dependency stream. The parent requires the exact Windows fail-fast termination code in both Debug and Shipping. The malformed node travels through parallel declaration, allocator seal/resolve/publication, packet preparation, and the real node scheduler before execution rejects the mismatch; the submission case travels through the real close/submission Jobs continuation. Existing RHI tests separately prove malformed receipts and post-execution fence failure with submitted-work evidence. The final recoverable case constrains a two-occurrence graph to one planning writer, proves `CapacityExceeded` is reported before either declaration runs, cancels without publishing an execution generation, and returns the allocator to `Idle`. No production hook, copied state, shared failure flag, or new lock was added. The Jobs adapter was also traced end to end: after wrapper validity checks, the imported builder dispatch operations do not expose a recoverable rejection result, so adding a global dispatch-failure switch would test an invented state and add hot-path overhead. The existing pool budget was evaluated and correctly rejected as an allocation-failure control because it is policy/telemetry rather than a deterministic hard failure gate. Genuine process OOM is not manufactured through unsafe exhaustion or a global allocator switch.

## Latest RenderFrame alignment pass

Early node-job setup now overlaps StorageData preparation and resource declarations, with stable occurrence storage allocated before either branch and kickoff held until resolution and frame setup finish. Scene custom data then camera custom data still use the existing combined preparation function. Early setup/dispatch failures retain the graph until preparation is joined; no extra scheduler or DrawBuffers producer was added.

Frame setup now carries wireframe, multilayer selection, debug-view, game-mode, and placed-resource options. Blank/prewarm, OverlayOnly, ShadedNoAmbient, GBufferOnly, TodvisBake, debug, and NoScene selection paths are explicit alongside Shaded/Selection/SafeMode. Structural choices reach both full and per-view cache keys; eviction and placed-resource policy are applied every execution, including hits. Device availability is checked before view preparation.

Per-view builders and feature-node bodies remain deliberately unimplemented. NoScene camera creation and offscreen-camera Selection filtering still need camera-model support; do not infer offscreen from Primary. Presentation/viewport serialization, native same-frame presentation, and shutdown integration are implemented at the source-only gate. No compilation or tests were run. Preserve these boundaries when resuming rather than interpreting this as permission to implement camera pipelines.

## Intent

Complete the full Render Graph and render-node study before implementing it.
The project deliberately follows the now-complete resource allocator instead of
being mixed into its final stages.

The original RED/Unreal comparison and Vanguard synthesis remain preserved, but
the former independent pass-compiler architecture and execution plan are
superseded. The active revision follows RED's Render Graph structure by default,
with Vanguard's resource-declaration/allocator seam and synchronization,
ownership, and Shipping-correctness requirements applied explicitly.

## Durable Files

```text
docs/development/render-graph-study-plan.md
docs/development/render-graph-design.md
docs/development/render-graph-execution-plan.md
docs/development/render-graph-execution-plan-former-v1.md
docs/development/render-graph-resume-checkpoint.md
docs/development/render-graph-authoring-examples.md
```

The study plan owns scope, source routing, evidence rules, and revision stages.
The design document preserves accumulated evidence and owns the authoritative
R9 architecture in Section 51. The execution plan is the regenerated R9
implementation authority; its predecessor is preserved in
`render-graph-execution-plan-former-v1.md`. The authoring examples use the final
R9 vocabulary and lifecycle. This checkpoint owns the exact restart position.

## Current Progress

```text
Original G0/R1-R3/U1-U2 studies                         PRESERVED
Former V1 Vanguard synthesis                            SUPERSEDED BY REVISION R0
Former V2/RG1-RG6 execution plan                        PRESERVED AS HISTORY
Revision R0 preservation/equivalence ledger             COMPLETE
Revision R1 RED representation and ownership            COMPLETE
Revision R2 RED graph authoring and composition         COMPLETE
Revision R3 RED node declaration and single execution   COMPLETE
Revision R4 RED CPU graph and GPU graph                 COMPLETE
Revision R5 command groups, recording, and submission  COMPLETE
Revision R6 declaration-to-allocator replay seam        COMPLETE
Revision R7 RED implicit-synchronization audit          COMPLETE
Revision R8 cache, cameras, context, and terminal chain COMPLETE
Revision R9 rewrite, regenerate plan, and verify        COMPLETE
Post-R9 direct allocator seam correction                SETTLED
Presentation threading decision                         SETTLED IN DESIGN SECTION 44
```

## Active Presentation Threading Decision

Present is a frame-global terminal Render Graph node and executes in the current
frame after truthful terminal graphics submission. It is not returned to a
main-thread mailbox or delayed until the next frame boundary.

Output acquisition, window reconciliation, viewport creation/destruction, and
swap-chain create/recreate/resize/rebind remain main-thread-only. A per-viewport
presentation-operation gate serializes those lifecycle operations with
worker-side Present, abandonment, and device-loss terminalization. Lifecycle
code never waits for the graph while holding that gate. The dispatched graph
owns the exact acquisition until it consumes it once; synchronous failure before
ownership transfer remains the main-thread caller's responsibility.

This decision supersedes historical finding 4, the main-thread terminal wording
in finding 11, and any former RG plan text that describes a next-frame
`PresentReady` drain. See
`render-graph-design.md` Section 44 for the authoritative contract.

## Active Direct Allocator Seam Decision

The RED-faithful baseline calls the renderer-owned `RenderFlowResourceAllocator` directly. `BeginFrame`, Resolve, BeginExecution, PacketFor, Finish, and pre-publication cancellation are allocator operations; the public `FrameResourceSession` proxy has been removed. The allocator stores its active generation and publication state internally and continues generation-stamping writers, packet views, uses, identities, and receipts.

The finalized composed `RenderNodeGraph` is already the executable graph. `PrepareResourcesParallel` registers every canonical resource operation, GPU flow group, command scope, queue, and explicit Sync request directly into the allocator. Direct allocator Resolve consumes that complete stream and receives no `SurvivingGraphOverlay`, `RenderGraphDefinition`, or public graph queue schedule. Historical mandatory post-build culling and survivor-overlay conclusions are superseded. Explicit queue synchronization remains required internally and lowers to real RHI waits/signals and truthful receipts.

## Former V1 Findings Preserved For Reclassification

The following findings are historical inputs, not a current architecture
contract. The R0 ledger in `render-graph-design.md` classifies them; R1-R8 gather
replacement evidence and R9 rewrites the authoritative synthesis.

1. `FrameRenderer` is the correct private owner of the future graph runtime and
   already owns `RenderFlowResourceAllocator`.
2. `RenderCommandSystem` must remain the only CPU render-chain owner.
3. Superseded in part by the Active Direct Allocator Seam Decision: the retained
   frame owns copied scalar frame facts, prepared view family/custom data, graph
   bindings, command scopes, receipts, failure state, and output transaction.
   The renderer-owned allocator, not the retained frame, owns the active
   allocator generation and publication state until the terminal epilogue.
4. Historical and superseded by the active Presentation Threading Decision:
   the former design kept output completion/abandonment/presentation on the main
   thread and returned dispatched tickets to a next-frame completion drain.
5. Success/failure of view-family commit, allocator `Finish`, and output
   present/texture-ready/abandon are separate exactly-once terminal
   responsibilities.
6. Superseded: the RED-faithful baseline performs no mandatory post-build resource-root culling; graph preparation registers every finalized-definition request and the allocator resolves the complete stream.
7. Each allocator command scope must correspond to a homogeneous RHI queue and
   receive a truthful submission/discard/device-loss receipt.
8. The current RHI can lower graphics/compute fork and join, but has no explicit
   cross-queue Copy wait primitive. The graph IR should retain three queues while
   the first compiler rejects unsupported Copy crossings.
9. No fake executor, second scheduler, second allocator, or second retirement
   manager will be introduced.
10. Jobs continuation semantics are proven: tasks dispatched through
    `RenderFrameContext::GetBuilder()` extend the outer RenderFrame job's completion
    counter, so the existing `cpuTail` can cover the graph terminal epilogue.
    The graph must not detach work onto an unrelated builder or extract the
    continuation builder's counter.
11. An acquired swap-chain output needs the exact token-aware
    `rhi::TransitionSwapChainPresent()` call. The allocator's ordinary texture
    transition cannot replace it, and executing both would double-transition.
    V1 defines a specialized retained-presentation allocator import that emits
    exactly one semantic terminal action. Its former statement that Acquire,
    Present, and Abandon all remain main-thread-only is superseded: Acquire stays
    main-thread-only, while dispatched Present/Abandon terminalization follows
    the thread-safe current-frame graph contract in Section 44.
12. The baseline uses a bounded renderer-owned cache of immutable compiled
    structural templates instantiated into one-shot frame-local graphs. It has
    no polymorphic render-node hierarchy. A feature node is ordinary code that
    contributes one or more passes to a template; frame instances bind only
    owned payloads, prepared view slots, imports, and output state.
13. One canonical typed-use IR drives hazards, allocator preparation, packet use order, and callback bindings. A second planning callback is forbidden.
14. Resources have stable allocation identities and explicit content versions.
    Writes produce versions; roots consume exact versions; RAW/WAR/WAW are
    explicit compiler rules.
15. The first dependency compiler is conservatively whole-resource. Precise
    subresource state tracking remains in the allocator; range-based dependency
    pruning is deferred.
16. The initial scope policy is one live pass per scope. Any later merged scope
    must be contiguous, queue-homogeneous, and followed by independent scope-DAG
    cycle validation.
17. The abstract graph preserves Graphics, Compute, and Copy. Current execution
    supports isolated single-queue work and the existing balanced non-nested
    Graphics/Compute fork/join. Cross-queue Copy and other unrepresentable
    shapes fail before allocator Resolve.
18. Serial execution uses `TerminalJoinToken::CompletedSynchronously`. A future
    terminal Job uses a separate already-ready graph-work counter; it never
    tries to prove readiness with the counter that contains itself.
19. Full submission receipts are mandatory. Per-scope fences come from that
    scope queue's residency fence, which matters because a fork's aggregate
    completion can be a Compute fence for a Graphics producer.
20. Updated: the command system's move-owned retained frame owns graph bindings, command records, output ownership, and failure state beyond builder/frame-stack lifetime; the `FrameRenderer`-owned allocator owns its active generation internally and reaches direct allocator Finish exactly once after publication.
21. A critical existing D3D12 presentation bug was found: the present-transition
    acknowledgement callback checks only `queue == Graphics`, so an invalid
    default fence can look submitted. RG4A must require a valid Graphics fence
    and pass discard/signal-failure/valid-submit tests before graph presentation.
22. Structural-template caching is required baseline work. Its fully comparable
    key covers every topology-affecting feature/configuration and revision epoch;
    immutable templates are bounded and eviction-safe, while frame-local
    payloads, imports, view IDs, output tokens, allocator generations, receipts,
    and terminal state are never cached. Parallel recording, pass merging,
    range-precise hazard pruning, and generic Copy waits remain explicit
    post-baseline work.
23. `RenderingServiceImpl` will own the existing
    `rendering::PresentationService` directly, with an optional `WindowService`
    dependency and a main-thread Presentation participant. Headless and
    device-disabled profiles leave it inactive; no second presentation service
    is introduced.
24. RG2 is intentionally transient-only in logical validation mode. Real
    retained imports need RHI descriptor/native-resource contracts and first
    enter the successful path in RG3.
25. Generic allocator Export and Readback graph roots are post-baseline until
    each has a bounded asynchronous result owner. A future Export must identify
    the final live version, use at most one slot per lineage, and explicitly copy
    an older value to a new lineage before export.
26. A pass callback is `noexcept` and returns explicit
    `RenderGraphPassStatus`; callback failure copies stable diagnostics, cancels
    the packet, invalidates bindings, discards the current list, and enters the
    truthful terminal path.
27. Texture viewport output has its own successful current-frame
    `CompleteOutput` terminal operation with a real terminal fence. It is not
    forced through Present or mislabeled as abandonment.
28. A failed native Present consumes the backend acquisition and caller ticket,
    then requests swap-chain/device recovery. It is never retried or followed by
    abandonment after submitted Present work.
29. The actual phase order is RenderUpdate join/collection, Presentation
    reconcile, then Render's RenderingFrameTick followed by a dependent frame
    source that acquires and submits. RenderUpdate collects failure and
    retirement state; it does not perform the previous frame's Present or drain
    terminal output tickets.
30. Allocator `Finish` failure is not retried or committed as success. Releasing
    the published session deliberately invokes allocator fail-closed
    `DeviceUnavailable`, and the graph requests device recovery.
31. Native allocator coverage requires `geometryAllocatorTests`; service
    lifecycle coverage also requires `materialRuntimeServiceTests`.
32. RG2 must explicitly finish every successful logical-only published session
    as Aborted with synchronous join proof and discarded scope/dependency
    receipts; dropping Ready state would fail-close the allocator.
33. Retained callbacks use owned pass data plus a noncapturing `noexcept` thunk.
    Arbitrary reference-capturing lambdas are not accepted.
34. The device-wide retirement watermark advances only after successful queue
    signals, is bootstrapped on all three queues, and is sealed before collect.
35. Texture, geometry, and GPU Scene upload migrate to full submission receipts;
    none may erase submitted-without-completion evidence.
36. Recoverable Present failure invalidates/releases the old swap chain and the
    next Presentation tick recreates it before another acquisition.
37. Texture continuation state, queue, and prior ready fence are copied into the
    immutable output acquisition used by the graph.
38. Owned text and callback diagnostics are budgeted, and GPU timing is wholly
    deferred until a truthful timestamp-query/readback path exists.
39. CPU-completion and GPU-order edges remain separate typed domains; neither silently changes kind.
40. CPU occurrence completion includes declared child continuations, wrapper cleanup, and allocator-packet terminalization, but does not prove command submission or GPU completion.
41. GPU order is a deterministic compile-time/resource-order constraint; same-scope recording, same-queue submission, and cross-queue waits require distinct R5 lowering.
42. Ordinary dependency groups are sealed non-nested CPU member sets with exact all-member input/output boundary semantics; their structural vertices are not executable nodes.
43. Command-list groups are distinct non-nested recorder-owning boundaries whose exit proves recording completion only.
44. Declaration-order and CPU-to-GPU bridge helpers remain explicit RED-compatible authoring operations and expand into inspectable typed edges at seal time.
45. Counter readiness does not encode success; a failed parent releases scheduling dependents into the cancellation path rather than deadlocking or executing them normally.

## Work Performed In This Session

- froze the study boundary and non-goals;
- recorded primary Vanguard, RED, and Unreal source routes;
- captured line-count and SHA-256 evidence for the Vanguard G0 snapshot;
- mapped existing ownership and frame lifetimes;
- identified the main-thread output/worker execution mismatch;
- identified the bounded RHI queue-sync model;
- recorded the exact allocator contract the graph must drive;
- proved continuation completion from Vanguard's Jobs wrapper, imported RED
  Jobs implementation, and the continuation test;
- identified the token-specific swap-chain Present transition as a required
  allocator/executor integration seam;
- completed RED R1-R3 and Unreal U1-U2 source studies with pinned local source
  hashes and decision ledgers;
- closed the full Vanguard producer, lifetime, output, allocator, and service
  seam inventory;
- selected the former V1 pass/version/use/root/compiler/scope/executor
  architecture, now preserved only as pre-R0 history;
- defined the exact specialized presentation-import boundary and recorded the
  pre-existing RHI acknowledgement blocker;
- wrote the former RG1-RG6 file-level execution plan, correction gates, failure
  matrix, and deferrals now preserved in the historical plan file;
- corrected the RG2/RG3 boundary so logical-only tests do not claim native
  import support, and deferred generic Export/Readback until their result
  ownership is designed;
- closed callback-failure, Texture-output completion, failed-Present,
  allocator-Finish-failure, frame-phase-order, and missing test-target gaps;
- closed the RG2 Ready-session teardown, callback-capture, device-wide
  retirement-cutover, immutable Texture acquisition, text-budget, and GPU-timing
  gaps found by the final consistency audit;
- marked the allocator plan's older virtual-node handoff wording as superseded
  without deleting its historical contract;
- completed revision R0's preservation/equivalence ledger and explicitly
  withdrew the former V1 completion claim;
- completed revision R1's RED graph representation and ownership audit with a
  pinned source snapshot;
- selected RED-faithful ownership for polymorphic node implementations,
  scheduled occurrences, command-list-group children, composed camera graphs,
  cached definitions, and frame-local execution contexts;
- adapted only the ownership hazards: the graph definition owns its node arena,
  records use generation-checked identities, the serialized CPU tail prevents
  definition eviction while used, and mutable frame state lives in the retained
  frame rather than cached node implementations;
- kept the former execution plan and authoring examples preserved but
  non-authoritative until the remaining RED studies and R9 synthesis complete;
- completed revision R2's direct trace of RED factory creation, dependency-tag
  sets, command-list groups, declaration-order helpers, unique merging, sequence
  concatenation, camera-fragment reuse, and final graph composition;
- preserved RED's authoring capabilities while rejecting only macro accidents,
  ignored names, untyped collisions, late-membership traps, and partial invalid
  composition;
- classified Blank, NoScene, and full Camera builders without inventing final
  Vanguard API spelling or changing the provisional authoring examples.
- recorded the current-frame presentation decision: Present remains a terminal
  graph node, while a per-viewport operation gate serializes its worker-side
  ticket consumption against main-thread lifecycle reconciliation;
- explicitly superseded the former next-frame `PresentReady` mailbox/drain
  design without weakening exact-token, receipt, failure, or shutdown rules.
- completed revision R3's trace of RED's `CRenderNodeBase::Process()`, dual
  PreConsume/Consume invocation, per-occurrence context, command-list modes,
  stable decisions, CPU-only nodes, and child-job-producing nodes;
- retained RED's named polymorphic node implementations and nonvirtual engine
  wrapper while splitting resource `Declare()` from one work-producing
  `Execute()` per surviving scheduled occurrence;
- kept CPU preparation as ordinary node execution, moved mutable execution state
  out of cached implementations, and preserved Present as a same-frame terminal
  node rather than an out-of-band callback;
- completed revision R4's trace of RED's separately stored CPU/GPU edges, Jobs-counter construction, continuation completion, GPU flow ordering, ordinary dependency groups, command-list-group boundaries, and CPU-to-GPU bridge helpers;
- defined four separate milestones for CPU occurrence completion, recorder completion, queue submission, and GPU completion;
- retained RED's two dependency domains and group capabilities while making `GpuOrder` explicitly non-synchronizing, sealing group membership, and requiring executable queue waits for cross-queue order;
- defined ordinary groups as CPU all-member boundaries and command-list groups as separate recorder-owning boundaries, including empty-group, culling, composition, failure, and multi-camera semantics.
- completed revision R5's trace of RED command-list ownership, flow-ordered synchronization ranges, Graphics/Compute fork/join submission, and final Sync-to-Present ordering;
- replaced RED's mutable flush cursor and implicit Copy flush with immutable compiled submission batches, explicit supported queue lowering, and truthful per-scope/dependency receipts;
- defined recorder binding, grouped-child serialization, independent-scope parallel recording, partial submission, post-execution signal failure, cancellation, and same-frame terminal Present behavior.
- completed revision R6's trace from one node declaration through graph-owned canonical tapes, resource-root culling, deterministic allocator-writer replay, packet execution, and terminal receipt assembly;
- corrected packet cardinality so only real resource-recording command-scope owners receive writers and packets; command-group children share one packet, while CPU-only, Sync, and Present nodes receive none;
- identified the remaining narrow presentation-import seam: the allocator needs one exact-acquisition `SwapChainPresentTransition` action before a viewport graph can execute without a duplicate or generic Present transition.
- completed revision R7's audit of synchronization and lifetime behavior supplied implicitly by RED's renderer and GpuApi implementation, then classified each behavior as an explicit Vanguard graph/RHI contract, an existing backend guarantee, or a fail-closed limitation;
- identified a major pre-production command-scope state seam: independently recorded command lists require immutable, physical/subresource-exact scope-entry state metadata and RHI tracker seeding before allocator actions execute;
- retained the initial Graphics/`CopySync` producer baseline through same-queue FIFO submission while keeping true `CopyAsync` imports and unsupported Copy queue crossings rejected until executable queue-wait edges exist;
- confirmed that the RHI submission lock provides physical submit/Present serialization but not logical graph order, and preserved explicit batch scheduling, retained resource residency, truthful partial-submission receipts, and same-frame presentation terminalization;
- recorded two presentation prerequisites: the allocator's exact-acquisition `SwapChainPresentTransition` action and validation that the submitted Present-transition fence is both valid and Graphics-owned.
- completed revision R8's trace of RED's four-entry graph cache, camera-fragment reuse, multi-camera composition, rich node context, Jobs-global frame lifetime, and StartRender-to-EndFrame chain;
- selected a four-entry fully associative `RenderGraphDefinitionCache` with complete comparable keys, frame-serial recency, private candidate builds, atomic publication, immutable definitions, and strong in-flight references;
- kept per-view definition reuse candidate-local like RED instead of adding a second persistent cache, while preserving distinct view occurrences, camera dependency outputs, flow spaces, and frame-global unique nodes;
- selected the command system's existing move-owned retained frame as the owner of frame payload, prepared family/custom data, selected definition, output transaction, allocator session/generation, command scopes, receipts, and terminal failure reporting; node jobs borrow it under the serialized CPU-tail lifetime;
- fixed the exact normal and failure terminal chains, including completed allocator Finish before camera commit and same-frame Present, plus an executor terminalizer that runs once on every outcome;
- confirmed remaining implementation seams: transfer an acquired output transaction during `SubmitFrame`, make worker terminal operations use the viewport operation gate, correct root-callback completion statistics, and add graph-cache clearing after RenderPath quiescence.
- completed revision R9's authoritative synthesis in `render-graph-design.md` Section 51 and explicitly made Sections 29-41 historical where they conflict;
- settled the final node/group/definition/occurrence/execution vocabulary and removed cached-graph `template` language from active contracts;
- preserved the former execution plan as `render-graph-execution-plan-former-v1.md` and generated a new RG1-RG7 file-level implementation plan from R0-R8;
- aligned the authoring examples with private candidate construction, atomic definition publication, named `RenderNodeImpl` declaration/execution through one `RenderNodeImplContext`, retained-frame ownership, RED-style non-owning `RenderNodeJob::JobsRenderFrame` access, and same-frame output ownership;
- completed the R9 documentation consistency and coverage gate without changing production code.

After the documentation study, the byte-exact RED core graph, factory, cache,
job, and implementation-context files were copied into
`source/rendering/port/red_render_graph`. Its `README.md` records the source
paths, hashes, exclusions, and port rules. The directory is outside the
rendering Premake globs and is not production code. The resource allocator,
`FrameRenderer`, RHI, viewport, and all other production sources remain
unchanged by the study and mechanical import.

## RG1 Translation And Correction Record

The first RG1 pass translated parts of the imported RED graph, graph-array, and factory behavior into
Vanguard production files. The resulting provisional definition layer owns polymorphic
node implementations, uses generation-checked definition/implementation/
occurrence/group/command-group/sequence identities, stores CPU and GPU-order
edges separately, expands ordinary all-member CPU boundaries including empty
groups, preserves ordered non-nested command-list children, merges compatible
unique and sequence occurrences, and retains composed source definitions so
implementation reuse cannot dangle.

Composition preserves distinct occurrence, `RenderViewId`, view slot, graph
flow-space, and dependency-output identity even when two camera occurrences
reuse the same implementation. Diagnostic strings never establish identity.
Definitions publish only after cycle, sequence, group, command-group, stale-ID,
and structural-compatibility validation succeeds. Failure and ownership checks
remain active in Shipping.

Historical focused verification from the first pass on 2026-09-04:

```text
premake5.exe vs2022                                      PASS (engine audit included)
renderingTests Debug x64 /m:1 build                      PASS
build/output/Debug/renderingTests.exe                    PASS
renderingTests Shipping x64 /m:1 build                   PASS
build/output/Shipping/renderingTests.exe                 PASS
```

That verification proves only that the provisional RG1 subset compiled and passed its focused tests. It does not prove fidelity to RED's file boundaries or behavior. The first pass incorrectly consumed five imported snapshots, split `renderNodeGraph.h` across invented headers, and described Vanguard private storage as a direct conversion of `renderNodeGraphArray.h`. The snapshots have now been restored permanently, filenames preserve RED component names in Vanguard snake_case, and RG1 is reopened for source-level correction.

The active RG1 correction has now removed the provisional `RenderGraphDefinitionRef`, `RenderGraphBuilder`, generation-ID family, view-binding overlay, and private definition-storage model from these five files. `RenderNodeGraphArray` again uses RED's masked IDs and separate usage-order storage. `RenderNodeGraph` again owns node/dependency arrays and exposes RED-shaped mutation, composition, flow-group, and exclusive-update operations. RED's `NodeGraphFactory` and `NodeGroupId` names are preserved, along with the external `NodesContainer`, group tags, lazy dummy input/output nodes, command-list-group authoring, and authoring-order CPU/GPU link helpers.

This remains an intentionally non-compiling mechanical port. `RenderNodeImpl::Process`, graph `Execute`/`ExecuteParallel`, command-list-group recording, profiling, and debug logging belong to later RG phases and currently have declarations or explicit placeholder bodies only. RED's class-local render-graph pool policy is preserved through `RenderNodeImpl::operator new/delete`, which keeps the RED-shaped factory and external owner syntax while routing allocations through Vanguard's Rendering pool. `RenderFlowGroup` and `RenderFlowSpace` are temporary scalar counterparts until the existing allocator seam is translated.

The template `render_node_graph_array.hpp` counterpart lives beside the exposed `render_node_graph.hpp`. RED's graph stores that template by value, so placing it under Vanguard's private include tree would make the exposed graph header depend on an unavailable private include path; this is a location adaptation, not a new abstraction.

The second RG1 correction slice retained command-list-group names instead of borrowing caller strings, restored the wrapper's aggregate child `GetJobBuilderUsage` query, restored RED's simple and synchronization authoring macros, strengthened RED-fatal special-node/sequence invariants with `VG_FATAL`, and kept the graph's flattened-order checks. It also applies one approved behavioral correction without changing the factory API: registering a member after a lazy group boundary already exists now connects that member to the existing boundary immediately. RED omits those links and can leave late group members outside declared dependencies.

The authoritative RG1 execution-plan contract has also been corrected to match the mechanical port. RG1 no longer asks for an invented immutable `RenderGraphDefinition`, graph builder, sealing protocol, definition generation IDs, or definition-owned node arena. Its active contract is RED's direct mutable graph plus externally owned node containers, with Vanguard-specific allocator/container substitutions and explicitly documented correctness fixes. Later stages may add only the seams that their translated RED files and the existing resource allocator actually require.

RG1 is now closed at the source-comparison gate. The five Vanguard counterparts were compared against their permanent snapshots for public shape, masked-array behavior, ownership, dependency insertion/removal, composition/reindexing, unique and sequence merging, dependency groups, command-list-group child ownership, and authoring-order links. Closure also restores RED's public `NodeGraphFactory` and `NodeGroupId` names, keeps RED-fatal authoring and flattened-order invariants fatal, and restores `AddGraph`'s post-allocation shape checks. No execution behavior was pulled forward.

RG2 has started with a contract correction and the first `render_node_impl_context` slice. The former definition/compiler/culling stage has been replaced by RED's actual next boundary: one reusable per-node context and a parallel resource-preparation traversal over the composed graph. Vanguard keeps resource declaration separate from `Execute()` and will bind the existing allocator's per-node planning writer through this context; the graph will not own allocator Resolve. The initial context translation now contains retained-frame borrowing, dispatcher-thread copies, node setup/reset, unique versus camera-local flow-space selection, GPU flow identity, prepared-view lookup, and RED-style camera-storage presence. CPU ordering remains in `RenderNodeGraph` rather than being duplicated in the implementation context. Binding dirtiness, allocator writer access, scene/custom-data convenience, command lists, and execution-only helpers remain for their owning slices.

RED's `RenderNodeImpl::Process()` wrapper must not be removed. Vanguard preserves it as the single orchestration point containing one preparation-versus-execution branch. The preparation side calls the separate resource-declaration callback; the execution side retains RED's command-list setup, profiling, `Execute()`, and epilogue responsibilities. Concrete node `Execute()` implementations never inspect an allocator phase and never perform resource discovery.

The next RG2 slice now preserves that boundary in source. `RenderNodeImpl::DeclareResources()` is the distinct Vanguard callback; preparation-mode `Process()` calls it, while the still-untranslated execution side remains fail-closed for RG4. `DeclareResources()` is deliberately synchronous and receives no Jobs builder because its planning writer is borrowed only for the callback. As in RED, `Process()` validates the dispatcher and begins a fresh per-node context boundary; `SetupNodeData()` does not, which ensures every ordered child inside a command-list group receives the same reset. `RenderNodeGraph::PrepareResourcesParallel()` follows RED's deterministic eight-node bucket traversal, opens one allocator planning writer for each real top-level occurrence, and records the first preparation failure through a thread-safe latch. A command-list group visits its children in authoring order through the same context and therefore the same group-owned writer. The allocator header exposes the direct writer-opening seam demanded by the final architecture; RG3 owns its implementation together with removal of the public session proxy. No Resolve operation moved into the graph.

RG2 is closed at its source-comparison gate. The direct allocator implementation, execution capabilities, command recording, submission, cache, and presentation integration remain RG3 through RG6 work.

RG3A is complete at its source-only gate. `RenderFlowResourceAllocator` now exposes direct frame startup, import and export-slot registration, planning-writer creation, planning seal, Resolve, execution start, packet lookup, Finish, cancellation, and state inspection. The old `FrameResourceSession` API remains only as the temporary implementation adapter and compatibility surface; RG3B removes it after moving its state guards directly into the allocator. The adapter retains exactly where legacy Finish or cancellation consumes a session reference, and detaches without triggering session-destructor recovery on failed or already-published operations. Resolve still accepts the old survivor and queue-schedule arguments until RG3C moves those facts into the allocator request stream.

RG3B is complete at its source-only gate. The `FrameResourceSession` type, compatibility `BeginFrame` overload, friendship, move/destructor machinery, and adapter calls are removed from production headers and sources. Every lifecycle operation now executes directly against the renderer-owned allocator's active generation and state. `BeginFrame` no longer creates a synthetic allocator retain; `Finish` releases only the published execution generation; and pre-publication cancellation resets allocator-owned frame state without pretending to release an external session owner. Allocator destruction remains fail closed through `AbandonAllocatorSession`, while planning writers and execution-generation views retain their existing independent lifetime witnesses. Legacy standalone allocator tests still express session-era fixtures and are intentionally deferred to the later authorized integration/test gate; no tests or compilation run during this mechanical phase.

RG3C is complete at its source-only gate. Public `SurvivingGraphOverlay` and `CompiledQueueSchedule` inputs are removed. `RenderFlowResourceAllocator::Resolve(jobs::Builder*, RenderFlowResourceFailure*)` now compiles every closed planning batch, command-group `RequestBeginQueue` / `RequestEndQueue` pair, and explicit `RequestQueueSync` record registered during graph preparation. Command scopes and cross-queue dependencies remain immutable execution-generation output for packet execution and terminal receipts; they are no longer caller-authored Resolve inputs. Cross-queue resource ordering is proved transitively through the registered submission boundary instead of requiring a fabricated dependency for every use pair. `RenderNodeImplContext` exposes the active allocator only during declaration, command-list groups use the same RED-shaped queue calls around their ordered child declarations, and packetless nodes can declare allocator-wide Sync requests without receiving fake writers or packets. The concrete RED `RenderNodeSynchronize` feature node remains in its original `renderGraphNodes` file boundary for that file's later translation. The allocator retains the published generation internally and exposes only its inexpensive ID through `GetExecutionGeneration()` so the coordinator can construct terminal join evidence without a session or ownership wrapper. Legacy allocator tests still use the removed session/schedule surface and remain deferred; no compilation or tests ran.

RG3D closes the direct allocator-seam correction pass. It removes the stale `ResolveFrame` output assignment left behind by RG3C, removes unreachable public queue-dependency accessors from `ExecutionGenerationRef`, and removes survivor-era failure wording from the production allocator path. Queue Begin/End/Sync registration now revalidates the Planning phase while holding the request lock, so no request can be appended after `SealPlanning()` commits the stream. The aggregate `maximumOperations` gate now counts queue requests as well as writer operations. RED's intentional default Graphics behavior for ordinary command-list-owning nodes is retained; explicit Compute groups still require their balanced RED-shaped Begin/End requests. Source references and whitespace were checked only; compilation and tests remain deliberately deferred.

The post-RG3D RED API comparison is recorded in `render-flow-resource-red-api-alignment.md`. It found three bounded alignment passes rather than a broad allocator redesign. RG3E.1 is complete: `FrameResourcePolicy::processEviction` now controls both dedicated-resource and whole placed-heap soft-target trimming while retirement polling remains unconditional. Placed heaps are eligible only when all of their live objects are reusable and pool-owned; their native bytes remain charged until release observation completes. RG3E.2 is complete at its source-only gate: `RenderNodeImplContext` now owns the RED-shaped typed allocation/use/swap/decision facade, the external `RenderNodeResourceBindings` table records generation-local IDs per composed occurrence, and execution lookup remains scoped through exact packet order. RED's cross-node and nested named-use pattern is represented by ordered named allocation scopes, while packet-local uses are automatically balanced at each concrete node boundary. RG3E.3 is also complete: representative texture, buffer, conditional, temporary, cross-node, group, and sync shapes close against that facade; raw writer and general allocator access are private; group/sync queue registration remains a private orchestration capability. RED's unused `RTFree`/`RTIsAlloc`, raw resource getters, injection, and reference swapping are explicitly not parity targets.

## Current Resume Action

1. Post-RG7 evaluation reopened bounded corrections: mixed CPU/GPU command-list-group finalization, released placed-pool metadata, and repeated dedicated-pool scans. These are corrected without additional synchronization: group children complete only their resource range through existing continuations; placed metadata compacts only after all acquired batches have been consumed; dedicated retirement polling stays at frame startup except budget-pressure recovery. The native provider test now covers mixed group continuation ordering, repeated cache release/reacquisition, and an outstanding batch surviving deferred compaction. The broader exit gate remains open for actual execution-context/task/builder allocation-failure injection and fatal-test identification; configured planning-capacity rejection and a generic fail-fast exit code do not prove those paths. Do not treat the earlier COMPLETE label as verification of these missing cases.
2. Keep the direct `RenderNodeGraph`, external `NodesContainer`, direct allocator API, and separate declaration/execute callbacks. Do not reintroduce a definition/compiler layer, survivor overlay, graph-owned Resolve wrapper, public queue-schedule getter, copied viewport state, or compensating viewport locks.

Post-review validation: `renderingTests` and the native D3D12 `geometryAllocatorTests` rebuilt and passed in Debug and Shipping. This includes mixed command-list-group CPU/GPU children with a deferred decision-consuming continuation, sixteen placed-cache release/reacquisition cycles, and compaction deferred while another batch owns pool indices. The complete seven-target RG7 matrix and deterministic metadata/task/builder allocation-failure tests were not rerun or supplied by this correction pass.

## Superseded Resume Action

1. Continue RG4A.3 from its selected-graph execution handoff. RG4A.1 replaced the root-task-local retained object with one Rendering-pool allocation owned through `RetainedRenderFrameRef`; RG4A.2 aligned the command-handler seam around `GetBuilder()` and the single command-system CPU tail. RG4A.3 now has the bounded first-failure message, expected-pointer `JobsRenderFrame` installation state, retained `RenderFrameCommandLists`, and one stable execution entry per composed occurrence. `RenderNodeResourceBindings` allocates every `RenderNodeImplContext` in one contiguous Rendering-pool block before declaration jobs are dispatched; each entry owns its compiled packet view, packet cursor, and execution failure. After allocator publication, `RenderNodeGraph::PrepareExecutionPackets()` retains the packet belonging to each command-list-owning occurrence. Execution-side `Process()` creates or obtains the flow-group command list, binds it, opens the stored packet, calls the concrete `Execute()`, and runs an immediate or child-job-delayed epilogue. `Own` finalizes the occurrence packet while `Require` closes only its child resource range against the shared group cursor. Command-list groups execute leading builder-free children inline, then dispatch serialized child ranges in which any builder-using child is last; the owning group epilogue runs only after those continuations and remains the sole packet finalizer. `RenderNodeJob::RunRenderNodeJobs()` creates one RenderPath task for every top-level occurrence, waits roots on allocator kickoff, waits children on all CPU parents, and gates its terminal continuation on the graph's single CPU leaf. It now preallocates every task and builder before dispatch, and any counter-extraction or terminal-dispatch failure drains all issued work before borrowed state can be released. The Jobs backend reserves wrapper storage before destructive counter extraction and exposes an explicit builder wait used only by this failure path. The command-list container follows the direct indexed recording contract, and retained-state reset cancels any cursor and discards any command list that did not reach a later terminal disposition. Allocator scope identities and receipts remain separate terminal evidence. `FrameRenderer` still does not install the Jobs frame or invoke this branch because the selected renderer-owned graph cache entry is RG6 work; do not add a temporary public graph setter or duplicate graph owner. Next provide the cache-entry-neutral renderer execution handoff, keep one retained owner through joined cleanup, join it into the command-system continuation, and clear the exact Jobs-frame pointer on every disposition. RG3F remains complete at its source-only correction gate with the explicit Graphics/Compute state and placed-resource restrictions recorded in design Section 52. No compilation or tests were run.
2. Keep the direct `RenderNodeGraph` and external `NodesContainer` ownership established by RG1; do not reintroduce a definition, builder, sealing layer, survivor overlay, graph compiler, or graph-owned allocator Resolve wrapper.
3. Preserve one RED file as one Vanguard file, changing only to snake_case and `.hpp` naming unless an unavoidable Vanguard seam is explicitly approved.
4. Do not compile or run tests during the mechanical translation stages. Wait for an explicitly authorized integration gate after the required RED files are translated.

## Verification State

The documentation study and excluded byte-exact snapshots require no runtime test. The old RG1 matrix is retained only as historical evidence for the provisional subset; no further build or runtime validation is authorized during mechanical translation.
The R9 gate checks authority markers, vocabulary, cache and camera identity,
definition/execution lifetime, declaration and packet cardinality, CPU/GPU
dependency separation, command-scope state seeding, output ownership transfer,
terminal ordering, failure unwinding, shutdown, stage ordering, and current
status claims. No production file was changed as part of R9.
