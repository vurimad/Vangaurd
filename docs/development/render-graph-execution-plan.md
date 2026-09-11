# Vanguard Render Graph Execution Plan

Date: 2026-09-06

Status: authoritative R9 implementation plan; RG1 through RG7 and the RG7C.2 behavioral exit gate are complete

## 1. Authority And Goal

This plan implements the RED-faithful architecture settled in `render-graph-design.md` Section 51. The superseded V1 plan is preserved in `render-graph-execution-plan-former-v1.md` for history only.

The goal is a production Render Graph built around RED's direct `RenderNodeGraph`, external `NodesContainer`, ordinary dependency groups, ordered command-list groups, CPU and GPU links, multi-view graph composition, bounded graph caching, Vanguard's declaration-before-execution resource planning, truthful queue submission, and same-frame output terminalization. It must drive the existing `RenderFlowResourceAllocator`; it must not replace or duplicate allocator policy.

Implementation remains bounded to Render Graph foundation and its required seams. Renderer feature-node libraries begin only after the final RG7 exit gate.

## 2. Non-Negotiable Contracts

1. `FrameRenderer` owns the graph cache and graph/allocator integration.
2. A cache entry owns the composed `RenderNodeGraph` and every external `NodesContainer` that keeps its node implementations alive, matching RED's ownership split.
3. Every copied graph node record is a distinct scheduled node, even when composed records reuse one implementation pointer.
4. Concrete renderer work lives in named `RenderNodeImpl` classes, not anonymous execution lambdas.
5. `Declare()` emits complete canonical resource/side-effect information before allocator `Resolve()`.
6. `Execute()` runs exactly once per scheduled node in the composed graph and never discovers undeclared resource use.
7. CPU dependencies and GPU dependencies remain separate compiled domains.
8. CPU completion, recorder completion, submission completion, and GPU completion are never conflated.
9. An ordinary group is an all-member CPU boundary. A command-list group owns one recorder and ordered children.
10. Only real resource-recording scope owners receive allocator writers and execution packets.
11. Same-queue order lowers to submission FIFO. Cross-queue order requires an executable queue wait and receipt.
12. Unsupported Copy crossings fail before execution; graph order or a lock may not impersonate GPU synchronization.
13. Independently recorded scopes begin from immutable physical/subresource-exact state and explicitly seed the RHI tracker.
14. The graph cache is a fixed four-entry fully associative baseline with full key equality, RED-shaped least-recently-used replacement, invalid-until-built entries, and frame-serial recency.
15. The command system's one stable retained-frame allocation owns every frame-local binding and terminal obligation; cheap strong references held by the command-system root, independently scheduled root branches, and terminal continuation keep that one allocation alive until the serialized CPU tail completes, while individual node children borrow it under a joined branch lifetime.
16. Presentation is a terminal node in the current graph execution. There is no next-frame presentation drain.
17. RED-style scene/camera custom-data preparation remains a frame-wide renderer preamble in the reserved `StorageData` command scope; it is not a graph node.
18. The exact output transaction moves from the caller to the command system's private retained frame only after command dispatch succeeds and remains there through graph terminalization.
19. All terminal paths report truthful allocator receipts and dispose output, camera, and retained-frame ownership exactly once.
20. Validation and stable failure reporting remain active in Shipping.
21. Public graph-authoring names stay close to RED: node, graph, dependency group, command-list group, sequence, and execution. Cached graphs are not called templates.

## 3. File Strategy

Preserve RED's file and component boundaries during the mechanical port. A RED filename changes only to Vanguard snake_case and, for headers, the `.hpp` convention. Do not split a RED file into newly invented public files or silently merge its responsibility into an unrelated component.

The implementation begins from the permanent byte-exact RED core stored in `source/rendering/port/red_render_graph`. That directory is an isolated mechanical port baseline and is intentionally outside all production Premake globs. Every `.red-source` snapshot remains present after its Vanguard counterpart is translated. Each RG stage changes the corresponding Vanguard file in place while the immutable RED file remains available for line-by-line comparison.

```text
source/rendering/include/vanguard/rendering/render_node_graph.hpp
source/rendering/include/vanguard/rendering/render_node_graph_array.hpp
source/rendering/include/vanguard/rendering/render_node_graph_factory.hpp
source/rendering/include/vanguard/rendering/render_node_impl_context.hpp
source/rendering/include/vanguard/rendering/render_node_job.hpp

source/rendering/private/vanguard/rendering/render_graph_cache.hpp

source/rendering/src/render_node_graph.cpp
source/rendering/src/render_node_graph_factory.cpp
source/rendering/src/render_graph_cache.cpp
source/rendering/src/render_node_impl_context.cpp
source/rendering/src/render_node_job.cpp

source/rendering/tests/render_graph_tests.cpp
source/engine/tests/render_graph_service_tests.cpp
```

Required seam edits are expected in `frame_renderer`, `rendering_service`, `render_command_system`, `viewport`, `rhi`, and `render_flow_resource_allocator` files. They remain narrow contract changes, not subsystem rewrites.

## 4. Stage RG1 — Graph Ownership And RED Authoring Model

### Purpose

Mechanically establish RED's graph representation, ownership split, factory, dependency groups, command-list groups, and graph composition before translating resource preparation or execution.

### Work

- Port RED's masked `RenderNodeGraphArray` storage, allocation reuse, usage ordering, removal, and reindexing behavior using Vanguard containers and memory.
- Port `RenderNodeImpl`, node parameters, node records, separately typed CPU/GPU dependency records, per-node camera/flow context, and the direct mutable `RenderNodeGraph` API.
- Keep implementation ownership external to graph topology: `NodesContainer` owns top-level implementations, while a command-list-group implementation owns its ordered child implementations.
- Route ordinary `new Node` factory construction through the Vanguard Rendering memory pool without changing RED's authoring shape.
- Port direct node/dependency insertion, lookup, copying, removal, helper-node removal, reset, and explicit exclusive-update locking.
- Port `AddGraph()` reindexing, camera-index override, unique-node merging, sequence concatenation, and dependency transfer without introducing a separate definition/builder abstraction.
- Port RED's `NodeGraphFactory`, `NodeGroupId`, named creation macros, CPU/GPU links, conditional links, dependency-group tags, lazy dummy group boundaries, and previous/next GPU-link helpers.
- Preserve RED's public naming and direct mutation model except for Vanguard casing, snake-case filenames, allocator/container seams, and documented correctness fixes.
- Correct RED's late-membership dependency-group hole: registering a member after a group's dummy input or output exists must immediately connect that member to the existing boundary.
- Leave execution, job dispatch, command-list creation, and resource preparation declared but untranslated until their corresponding RG stages.

### Tests

- RED-to-Vanguard source comparison for graph-array behavior, graph ownership, factory ownership, group boundaries, links, and composition;
- top-level and command-list-child destruction ownership;
- CPU and GPU links remain separately typed;
- ordinary dependency groups and ordered command-list groups retain distinct behavior;
- sequence entry/exit composition and unique merge preserve RED dependency transfer;
- camera-index override and implementation-pointer reuse preserve distinct copied node records;
- late dependency-group membership is connected to already-created dummy boundaries.

### Exit gate

A RED-shaped graph can be authored, linked, grouped, composed, reindexed, reset, and destroyed while its external node containers provide the implementation lifetime. During the mechanical port this gate is established by line-by-line source comparison and explicit seam documentation. Compilation and runtime tests are deferred until the user authorizes the integration gate after the required RED files have been translated.

## 5. Stage RG2 — Resource Preparation And Node Context

### Purpose

Translate RED's per-node context and pre-consume graph traversal while preserving Vanguard's one deliberate allocator departure: resource declaration is separate from node execution.

### Work

- Translate RED's single `RenderNodeImplContext` type, `InitData`, retained-frame borrowing, dispatcher-thread copy, per-node setup/reset, camera/view selection, unique-node state, and GPU flow/flow-space identity.
- Keep one context type for both preparation and execution. Capabilities change with the active operation; do not invent separate declaration-context and execution-context classes.
- Add one explicit resource-preparation callback beside RED's execution callback so renderer nodes never branch on an allocator phase inside `Execute()`.
- Preserve RED's `RenderNodeImpl::Process()` orchestration wrapper. `Process()` contains the one centralized preparation-versus-execution branch: preparation calls the resource-declaration callback, while execution performs RED's command-list/profiler wrapper, calls `Execute()`, and runs the epilogue. Concrete nodes never repeat that branch.
- Keep `DeclareResources()` synchronous and do not give it a Jobs builder. Its planning writer is borrowed for exactly that call; only execution callbacks may extend their lifetime through child jobs.
- Translate RED's deterministic parallel pre-consume traversal as `RenderNodeGraph::PrepareResourcesParallel`; it visits every real top-level graph node once and never records GPU commands.
- Make a command-list group prepare its ordered children through the same group-owned preparation scope, matching RED's child ownership and order.
- Bind one existing allocator `ResourcePlanningWriter` to each real top-level occurrence during its preparation callback. A resource-less occurrence closes an empty writer so execution retains one occurrence/one packet identity; dummy helper nodes remain skipped. Node code talks to that writer through the context, and the graph does not own or perform allocator Resolve.
- Preserve typed texture, buffer, import, output, view, state, access, decision, scope, swap, and terminal declaration operations already provided by `ResourcePlanningWriter`; do not duplicate allocator validation in the graph.
- Preserve the complete authored graph as executable. Do not add survivor overlays, resource-root culling, SSA resource versions, a second dependency compiler, or an immutable definition/sealing layer.
- Keep authored CPU and GPU dependency arrays and the flow-group ordering produced by RG1. Any later queue-schedule adaptation must consume those facts without replacing RED's graph model.
- Keep frame/camera/scene/binding convenience accessors in `RenderNodeImplContext` close to RED and add them incrementally with the RED files that require them.

### Tests

- RED-to-Vanguard source comparison for context initialization, dispatcher copies, node setup/reset, flow identity, and parallel preparation traversal;
- each real top-level node prepares exactly once without calling `Execute()`;
- command-list-group children prepare once in declared order under their owning group;
- unique nodes receive shared flow space while camera nodes receive their composed view slot;
- invalid camera/view association and missing retained-frame state fail explicitly;
- failed writer creation or declaration closes or abandons only that allocator planning path and is reported to the frame coordinator;
- no graph method calls allocator Resolve and no survivor/definition/compiler abstraction reappears.

### Exit gate

The composed RED-shaped graph can prepare every node's allocator declarations through one RED-shaped context and existing allocator writers without executing renderer work or resolving native resources. During the mechanical port this gate is source comparison and seam documentation only; compilation and runtime tests remain deferred until the explicitly authorized integration gate.

## 6. Stage RG3 — Allocator Replay And RHI Scope-State Contract

### Purpose

Connect compiled graph structure to the existing allocator and make independent command recording truthful.

### Work

- Before graph integration, simplify the allocator's public frame API: remove `FrameResourceSession` and `SurvivingGraphOverlay`, keep the active generation and publication state inside the renderer-owned allocator, and move frame lifecycle operations directly onto `RenderFlowResourceAllocator`.
- Prepare every resource-recording occurrence in the finalized composed `RenderNodeGraph` deterministically into allocator resources, nodes, uses, flow groups, command scopes, queue requests, decisions, imports, and output declarations.
- Expose that operation as `RenderNodeGraph::PrepareResourcesParallel(RenderNodeImplContext&, RenderFlowResourceAllocator&, RenderNodeResourceBindings&, RenderNodeResourcePreparationFailures&, jobs::Builder&)`; the retained frame owns the binding table, declaration remains separate from node execution, and allocator Resolve remains a following visible job.
- Extend `FrameResourcePolicy` with `processEviction` so blank or offscreen game frames can preserve the normal rendering working set while onscreen game and non-game/tool frames retain normal trimming behavior.
- Before adding RHI scope-entry state, close the RED node-authoring API gap through the bounded RG3E passes in `render-flow-resource-red-api-alignment.md`: lifecycle/policy parity, a RED-shaped context facade backed by frame-owned per-occurrence bindings, then removal of raw writer/allocator access from ordinary nodes.
- Map command-group children into ordered use intervals on their shared allocator writer and packet.
- Make direct `RenderFlowResourceAllocator::Resolve(jobs::Builder*, RenderFlowResourceFailure*)` consume the complete request stream registered during preparation; it receives no graph definition, survivor overlay, or public graph queue schedule.
- Keep generation checking internally on planning writers, packet views, resolved uses, and terminal receipts, and add explicit direct cancellation for failures before publication.
- Add immutable resolved scope-entry state records keyed by physical resource and exact texture subresource or buffer range supported by the RHI.
- Add a narrow RHI operation that seeds a fresh command list's NVRHI state tracker from compiled scope-entry truth before allocator actions execute.
- Define the fixed `ReservedFrameCommandList::StorageData` recording-slot prefix separately from allocator command-scope identities and include both in the per-frame command plan.
- Reject missing, conflicting, stale, or nonrepresentable entry state.
- Add residency-working-set population for declared indirect/bindless resources.
- Add the acquisition-aware `SwapChainPresentTransition` allocator action and exact output-import validation.
- Lower a Presentation root by appending that action to the real final Graphics recording scope; keep the later CPU `PresentNode` packetless and require it to validate the final Submitted receipt.
- Require `MarkPresentTransitionSubmitted()` evidence to carry a valid Graphics-owned fence.
- Preserve the initial Graphics and existing bounded Graphics/Compute lowering. Keep true `CopyAsync` external crossings fail closed until the explicit post-baseline contract exists.

### Tests

- graph declarations replay to exact allocator packets and captured decisions;
- direct allocator lifecycle rejects overlapping frames and stale generation-stamped writers, packets, uses, and receipts without a public session proxy;
- `PrepareResourcesParallel` registers the complete built-graph request stream without invoking node execution, recording commands, or hiding allocator Resolve;
- the named direct allocator Resolve continuation records failure without deadlocking waiters and releases the node-kickoff deferral only after all continuation-local creation work finishes;
- `processEviction == false` skips soft-target trimming without disabling fence polling or safe resource reuse, while `true` retains normal trimming;
- shared command groups preserve child use order;
- independent scopes seed exact initial state and record without hidden global state;
- mismatched state/subresource identity fails before node work;
- alias activation remains allocator-owned and ordered;
- indirect resources enter the residency working set;
- exact acquired backbuffer receives one semantic Present transition;
- invalid/default/non-Graphics Present fence is rejected;
- unsupported Copy producer/import and terminal crossings fail with stable diagnostics;
- dedicated and placed allocator modes both pass the seam matrix.

### Exit gate

An offscreen built graph can reach allocator Ready state with truthful packets and recorder-entry state. The presentation import resolves but is not yet presented. Run allocator, RHI, and focused graph targets in fresh Debug and Shipping builds, then correct findings before RG4.

## 7. Stage RG4 — Retained Frame And RED Node Context

### Purpose

Execute CPU and recording nodes once while retaining every dependency and owner until terminalization.

### Work

- Extend the command system's existing retained frame with prepared family/custom data, selected graph-cache entry, occurrence bindings, output slot, command lists, receipts, first failure, and terminal continuation; the `FrameRenderer`-owned allocator retains its own active generation.
- Allocate the retained-frame object at one stable address, expose only borrowed references to its exact runtime members through `RenderFrameContext`, and use a cheap `RetainedRenderFrameRef` ownership handle without copying or relocating the frame state after jobs capture member pointers.
- Retain the same frame allocation from the command-system root, terminal continuation, and each independently scheduled root branch; `RunRenderNodeJobs` moves its reference into a joined branch-cleanup continuation so individual node children borrow the frame without one reference-count operation per node, and normal terminalization remains ordered after every graph child job.
- Allocate all required task objects before dispatching the first independent node branch. Add a fail-closed last-reference fallback for counter-extraction or terminal-dispatch failure so allocator, output, current-job-frame, and prepared-family ownership cannot be stranded when the normal terminal job cannot be established.
- Add RED-style `RenderNodeJob::JobsRenderFrame` access: assert the slot is empty, install a non-owning pointer to the retained `RenderFrameInfo` before node dispatch, and clear it from named `EndFrame` or the exactly-once failure terminalizer.
- Add one rich per-occurrence `RenderNodeImplContext`, initialized with declaration-only or execution-only capabilities, with optional view identity, groups, dispatcher, Jobs continuation, and recording-only queue/scope/command-list/packet bindings.
- Provide narrow frame, camera, scene, custom-data, resource-use, and child-job accessors; expose no raw allocator mutation.
- Give the base frame context borrowed `RenderCameraStorage` access and replace CPU-only `CustomDataPrepareInfo` with one RED-shaped, failure-returning scene/camera `Prepare` contract.
- Record all scene custom data and then all ordered prepared-view custom data through the reserved `StorageData` scope before allocator declaration and Resolve; preserve fixed type directories, readiness checks, eviction, and prepared-frame marking.
- Schedule occurrences from the built CPU graph through `RenderFrameContext::GetBuilder()`.
- Make child jobs explicitly extend their occurrence continuation.
- Record separate ready scopes concurrently where permitted and serialize children within a command-list group.
- Make each scope executor open/finalize its packet exactly once. An independent occurrence consumes its packet; a command-list group invokes ordered children serially against restricted intervals of one shared cursor, and children never open or finalize that packet.
- Enforce one `Execute()` call per occurrence in the built composed graph.
- Preserve named StartRender, EndRender, cleanup, and EndFrame responsibilities.
- Join occurrence and recorder ownership independently.

### RG4A progress

Status: complete at the source-only gate; cache selection and invocation remain RG6-owned.

RG4A.1 made the command system allocate one stable, reference-counted retained-frame object instead of storing the frame directly inside the root task capture. `RenderFrameContext::RetainFrame()` returns a cheap strong reference to that same allocation. The allocation owns the copied `RenderFrameInfo`, any locally prepared view family, frame custom-data access, per-occurrence resource bindings, and the parallel preparation failure latch, so those members cannot remain on the root callback stack when graph branches are introduced. The job-visible `RenderNodeJob` current-frame slot also exists with an atomic, Shipping-active occupied-slot contract, but installation and clearing remain intentionally unwired until the joined graph branch and terminal cleanup that prove its borrowed lifetime are present.

RG4A.2 aligned the graph-facing command-handler seam without importing unrelated command queues. `RenderFrameTickContext` and `RenderFrameContext` now expose their continuation as `GetBuilder()`, matching its actual role, and that builder remains the only path which extends `RenderCommandSystem`'s serialized CPU tail. The render-node builder is deliberately a separate renderer-local builder whose extracted completion counter must be joined into this continuation. No synthetic draw-buffer prerequisite is exposed: Vanguard currently has no independent draw-buffer flush branch, while allocator Resolve supplies the real deferred node-kickoff prerequisite inside frame execution.

RG4A.3 is in progress. The retained allocation now owns a bounded, thread-safe first-failure message and exposes direct `RecordFailure()` / `HasFailure()` access to asynchronous frame branches. It also owns whether its exact `RenderFrameInfo` address is installed in `RenderNodeJob::JobsRenderFrame`: normal cleanup compares against that expected address, while last-reference destruction performs the same clear if terminal scheduling failed. This state exists but is not yet installed by `FrameRenderer`. The retained allocation also owns `RenderFrameCommandLists`, preserving the direct indexed `PrepareForFrame()` / `SetCommandList()` / `GetCommandList()` recording contract. Its destructor discards every list that has not reached a later terminal disposition. `RenderNodeResourceBindings` now preallocates one contiguous block of stable `RenderNodeImplContext` objects before its parallel declaration jobs are dispatched. Each composed occurrence keeps its context pointer, compiled packet view, packet cursor, resource ranges, and failure record in the same frame-owned entry; reset cancels any open cursor before destroying the contexts and releasing the block. `PrepareExecutionPackets()` validates that table after allocator publication and retains one compiled packet view for each command-list-owning occurrence. The context exposes the direct flow-group-indexed command-list slot and delays `OpenCursor()` until node processing has bound that exact list. Execution-side `RenderNodeImpl::Process()` preserves the create-or-require, bind, execute, and immediate-or-delayed epilogue boundary. An `Own` occurrence opens and finalizes its packet; a `Require` child reuses that open cursor and closes only its own resource range. Command-list groups now execute leading builder-free children inline and then dispatch ordered child ranges; a builder-using child must terminate its range, so its continuation completes before the next range touches the shared context, cursor, or command list. The owning group epilogue remains the only packet finalizer. Any execution failure cancels the still-open packet during epilogue or retained-state teardown. `RenderNodeJob::RunRenderNodeJobs()` now materializes the built CPU dependency graph as one RenderPath task per top-level occurrence. Roots wait on the allocator kickoff counter, every non-root waits on all CPU-parent completion counters, the graph must have one terminal leaf, and that leaf gates the supplied terminal continuation. Occurrence tasks reuse their preallocated contexts and extend their own continuation for child work. Every node task and independent builder is allocated before the first node dispatch. Counter wrapper allocation now occurs before destructive extraction; if it fails, only this scheduler invokes the explicit builder-drain path and then joins all earlier counters before returning. Terminal-dispatch failure likewise joins every issued occurrence, so no borrowed graph, context, or retained-frame member escapes a failed setup call.

RG4A.3 closes through a private, cache-entry-neutral `FrameRenderer::ExecuteBuiltGraph` handoff. It retains the frame once for the complete independent node branch, records `StorageData` through the reserved command-list slot, starts and resolves the allocator directly, releases node kickoff only after packet publication or fail-closed blocking, joins the independent branch into the command-system continuation, and clears the exact `RenderNodeJob::JobsRenderFrame` pointer after every issued node and child job completes. The selected graph is borrowed under its exclusive-update flag; no temporary public graph setter or duplicate graph owner was introduced. RG6 remains responsible only for selecting and pinning the cache entry before calling this handoff.

### Tests

- no job retains stack frame context or borrowed builder memory;
- cache-entry replacement cannot invalidate an execution;
- every occurrence in the built composed graph executes exactly once unless frame failure routes it through cancellation;
- CPU-only nodes work without command lists or packets;
- child jobs delay CPU occurrence completion;
- independent scopes may record concurrently;
- grouped children never record concurrently and retain order;
- command-list mismatch, child-interval escape, packet-step mismatch, dropped cursor, and execution failure terminalize safely;
- retained-frame destruction is impossible before all graph child jobs join.
- the job-visible current-frame slot rejects an occupied slot, remains valid through child completion, and is null after success, failure, cancellation, and isolated-test cleanup.
- scene custom data prepares before camera custom data, every prepared view receives exactly one camera-data callback, and the exact failing kind/type index is reported.
- `StorageData` recording always unbinds its command list and reaches an owned terminal disposition after success or failure.

### Exit gate

A headless/offscreen frame records all supported work and reaches recorder join with exact lifetime and first-failure behavior. Submission and terminal allocator closure are introduced next. Fresh Debug and Shipping tests pass, followed by a correction pass.

## 8. Stage RG5 — Submission, Receipts, And Exactly-Once Terminalizer

### Purpose

Submit immutable batches in compiled GPU order and close allocator/execution ownership truthfully on every outcome.

### Work

- Submit only after each batch's recorder and executable queue dependencies are satisfied.
- Lower same-queue order to FIFO and existing supported Graphics/Compute fork-join edges to real RHI operations.
- Submit the reserved `StorageData` scope before graph scopes on Graphics and compile an explicit wait when a prepared persistent import is first consumed on Compute or Copy.
- Keep graph scheduling explicit; never rely on the RHI submission lock as graph order.
- Record per-scope Submitted, DiscardedBeforeSubmission, or UnknownDueToDeviceLoss receipts and all required fences.
- Preserve prior successful submissions after later close, execute, or fence-signal failure.
- Build a terminal join token only after every occurrence, recorder, submission, packet owner, and cleanup continuation joins.
- Add an exactly-once terminalizer that preserves the first failure and continues mandatory cleanup.
- Call allocator `Finish(Completed)`, `Finish(Aborted)`, or `Finish(DeviceLost)` from truthful receipts.
- Commit `PreparedRenderViewFamily` only after successful allocator completion.
- Correct frame statistics so callback return is not reported as completed graph execution; either rename them as dispatch/callback statistics or update true completion from the terminal sink.

### RG5 progress

Status: complete at the source-only gate; integration validation remains deferred.

The RG5A submission slices translate and close the synchronization-node recorder and queue-dependency boundary. `RenderFrameCommandLists` tracks RED's `nextFlushStart` and schedules the same contiguous indexed ranges through the synchronization occurrence's child builder. The reserved `StorageData` slot is included before graph recorders. After allocator publication and before kickoff, every compiled command-list scope is registered privately in its flow-group slot; node authoring retains the direct `SetCommandList(index, commandList)` shape and cannot supply or fabricate scope metadata. A private read-only allocator handoff registers the exact compiled producer, consumer, and sync identities without restoring a public queue-schedule getter. Before native submission, each non-None Sync must match exactly one compiled boundary crossing its flow position. The existing RHI Fork/Join operation remains the sole native queue-wait mechanism. Successful native submissions produce one real queue fence per submitted allocator scope and a Submitted receipt for the matched dependency; pre-submission failure discards untouched scopes/dependencies, while native execution followed by fence-signal failure records unknown completion and device loss. Both Fork and Join lower immediately after the producer range; Join's queued Graphics wait orders its subsequent consumer rather than pretending that future consumer was present in the lowering batch. Empty synchronization ranges without a dependency are valid no-ops, and a range exceeding the RHI submission limit fails before native submission. After the node branch joins, any registered scope or dependency without a receipt is recorded as discarded. If no earlier failure explains it, an unsubmitted recorder or dependency is diagnosed as a missing authored final synchronization boundary. No hidden tail submission is synthesized.

The RG5 terminalizer slice is complete at the source-only gate. The one node-branch terminal continuation folds execution and submission failures into the retained frame, finalizes every unsubmitted scope and dependency, derives Completed, Aborted, or DeviceLost from truthful command-list evidence, constructs the complete `TerminalExecutionReceipt`, and calls the allocator's direct `Finish` exactly once. Successful completion commits the prepared view family only after allocator terminalization. A rejected terminal receipt records the first error and invokes the allocator's existing fail-closed published-generation abandonment path, preventing a poisoned generation from surviving into the next frame. The same terminal helper is used after node-scheduler setup failure once any partially scheduled node work has joined. It then releases the graph pin and clears the exact installed Jobs frame. No retry state, second scheduler, or graph-owned allocator wrapper was added.

Frame statistics now publish from that true terminal sink rather than from the earlier renderer callback return. The retained frame carries one non-owning pointer back to its owning command system, one exactly-once atomic publication bit, and one callback-thread-only deferred flag. Immediate callback failures, skips, and synchronous successes publish at callback completion. `FrameRenderer` sets the deferred flag only after establishing the graph's terminal continuation, so an accepted asynchronous graph publishes only after allocator, camera, graph-pin, and Jobs-frame dispositions complete. This costs one atomic exchange and one direct call per frame, with no per-node work. Dispatches are provisionally counted before their task can execute and rolled back if dispatch rejects the task, so a very short frame cannot transiently appear completed before it appears submitted. If public tail-counter extraction fails after dispatch, the local builder synchronously joins that already-issued frame before returning the error, preventing retained-frame state from outliving its command-system owner. RG5 source translation is complete; compilation and integration tests remain deliberately deferred to the authorized gate.

### Tests

- successful multi-batch submission produces exact scope/fence receipts;
- the reserved `StorageData` scope submits before the first Graphics graph scope and produces an explicit wait before a first Compute or Copy consumer;
- failure before submission discards every untouched scope;
- partial submission retains Submitted receipts and discards only untouched scopes;
- post-execution fence-signal failure becomes DeviceLost/unknown evidence, never discarded work;
- allocator Finish is called exactly once with the correct terminal kind;
- camera family commits only after `Finish(Completed)`;
- cleanup failure does not suppress earlier failure or skip ownership closure;
- true completion metrics update only at terminal execution.

### Exit gate

Headless and texture-output executions close every allocator, camera, command-list, and result owner correctly under success, abort, partial submission, and DeviceLost. Fresh Debug and Shipping receipt/failure-injection matrices pass, followed by a correction pass.

## 9. Stage RG6 — Same-Frame Output, Cache, And Service Lifecycle

### Purpose

Integrate the graph into real frame ownership, use the bounded RED-shaped graph cache, and make the current frame reach its exact output.

### Work

- Add complete comparable `RenderGraphKey` and `RenderViewGraphKey` types with cached hash acceleration.
- Include every structural mode, output, ordered-view, camera-dependency, feature, and renderer-revision fact that current builders branch on; exclude frame-local values, and add queue-policy or backend-capability facts only when a builder actually branches on them.
- Add the four-entry fully associative `RenderGraphCache` with frame-serial recency and complete-key equality.
- Keep RED's direct `GetGraph` hit/miss contract: reuse the least-recently-used entry on a miss, report `needsRebuild`, and keep the selected entry invalid until rebuilding completes.
- Prevent cache-entry reset while the serialized frame tail uses it and add `ClearPersistentCaches()` after RenderPath quiescence.
- Extend command submission so `ViewportManager::SubmitFrame` acquires the exact output immediately before dispatch and creates a move-only `RenderFrameOutputTransaction` owned separately from public `RenderFrameInfo`.
- Keep synchronous dispatch failure ownership with the caller; on success move the transaction into the command system's private retained frame and keep it there through graph terminalization.
- Add the per-viewport operation gate shared by worker Present/Complete/Abandon/DeviceLost and main-thread resize/rebind/destruction.
- Execute Present or CompleteOutput in the same graph terminal chain after allocator `Finish(Completed)` and camera commit.
- Classify failed Present after submission without falsely abandoning the acquisition.
- Integrate `FrameRenderer` and `RenderingService` initialization, per-frame dispatch, cache ownership, failure reporting, device recreation, and shutdown.
- Enforce shutdown order: stop frames/acquisitions, flush RenderPath tail, close outputs/families, shut down command system, clear graph cache, clear allocator caches, then allocator/RHI and camera/scene teardown.

### RG6 progress

Status: RG6A is complete and RG6B's RenderFrame alignment pass is implemented at the source-only gate. Feature-node bodies and per-view graph topologies remain deliberately unimplemented; this does not close RG6.

The imported four-entry `RenderGraphCache` shape is now present directly in Vanguard. Each entry owns its final composed `RenderNodeGraph` and a Rendering-pool array of per-camera setup owners; every setup owns the same separate `RenderNodeGraph` and `NodesContainer` pair as the source cache. `GetGraph` linearly checks all four entries, accelerates hits with the cached hash but requires full canonical-key equality, updates accepted-frame recency on a hit, and reuses the oldest entry on a miss. A failed per-camera allocation rolls the selected entry back to an invalid empty state. `PostBuildClear` preserves the source lifetime rule by clearing temporary camera graphs while retaining their node containers, and it is also the point that makes a successfully rebuilt entry hittable. `FrameRenderer` directly owns the cache. The key types retain structural fields and exclude frame-local matrices, native handles, acquisitions, fences, and payloads.

RG6B constructs the supported frame/view key directly from `RenderFrameInfo` and the prepared family, maps ordered camera dependencies to prepared-view indices, and selects the four-entry cache using the accepted frame serial. `RenderFrameInfo` captures output kind and `RenderFrameFeatures` at `BeginFrame`. Mode, purpose, debug view, wireframe, multilayer selection, and scene availability participate in graph selection and key equality; per-view keys receive the same structural feature bits. Queue-policy and backend-capability fields are absent until builders actually branch on them. Game mode and placed-resource enablement are allocation policy, not cached graph topology, and are applied every frame, including cache hits.

The zero-view `BuildRenderGraphBlank` path retains separate cache-entry graph/node storage, StartRender, the ordered blank command-list group, EndRender, final synchronization, Present, grab flush, allocator cleanup, EndFrame, GPU/CPU linking, final composition, flow-group building, and `PostBuildClear`. Frame-global and blank feature-node bodies deliberately execute no work until their dedicated implementation passes; synchronization remains the real submission node. Zero cameras or OverlayOnly select ordinary blank rendering; Blank purpose with cameras selects the prewarm branch. Neither is a working visual renderer yet.

The prepared-view body retains `FindCameraSetup`, `DoesNeedRebuild`, mode selection, and per-view `AddGraph`. Shaded/ShadedNoAmbient choose Camera, Todvis/debug visualization/HitProxies debug, or NoScene; Selection forwards multilayer selection; SafeMode retains its NoScene alternative; GBufferOnly and TodvisBake have explicit branches. Per-view builders still only reset private graph/node storage, and the caller fails before composition or `PostBuildClear`. Their actual topologies remain dedicated work. The current prepared-family API requires a valid scene and has no explicit offscreen-camera property: NoScene camera construction and RED's offscreen Selection skip remain documented camera-model seams, not features claimed implemented or inferred from the unrelated Primary flag.

The alignment pass restores RED's early `RunRenderNodeJobs` dispatch before StorageData/custom-data preparation and resource declarations. Stable occurrence storage is allocated once by `PrepareResourceBindings`; declarations no longer reset storage that job setup is borrowing. Scene custom data and then camera custom data remain prepared through the existing combined call, now documented at the call site. Roots wait for Resolve/packet publication and the dispatching thread's setup hold; setup failures cannot release the graph before preparation finishes. Dispatch-failure paths drain issued preparation before releasing execution, and frame joining is established before the final setup hold is released. No separate DrawBuffers gate is invented: the existing command-system CPU tail remains authoritative until a real independent producer exists.

`RenderFrame` checks device availability before camera preparation/cache lookup. Allocator policy is applied directly on each execution: placed resources use the explicit frame option, and game-mode eviction runs only for a presentation-backed nonblank frame; non-game frames keep eviction enabled. Graph presentation, viewport mutation serialization, and service-shutdown integration remain RG6C work rather than part of the RG6B alignment pass. Validation was source inspection and scoped diff checks only; no compilation or tests.

### RG6C progress

> Status: RG6C.3 lifecycle closure is implemented at the source-only gate; RG6 implementation is complete and the RG7 integration gate remains.

`ViewportManager::SubmitFrame` now acquires the exact Texture output or requested Presentation back buffer immediately before command dispatch. A move-only `RenderFrameOutputTransaction` owns that acquisition separately from public `RenderFrameInfo`; the command system moves it into the retained frame only for the dispatched work and restores it on synchronous allocation, builder, or dispatch failure. Its destructor returns an unconsumed acquisition, with a failed return putting the viewport into device-recovery state instead of silently losing the token.

Viewport lifecycle and applied-property mutation use the serialized render-command tail rather than a per-slot operation gate. Exactly one output transaction may be outstanding per viewport; its atomic owned bit is the narrow cross-thread ownership handshake, while an unchanged presentation-property request remains a no-op. Acquisition and lifecycle mutation remain main-thread-only. `Present`, output completion, abandonment, and device-loss disposition are worker-safe and validate the viewport generation, output revision, output kind, and exact RHI acquisition before consuming it. Presentation statistics retain an atomic global counter because worker Present is their real producer.

The retained frame exposes its output transaction to node contexts. The common-resource node imports the exact acquired presentation texture as `FrameOutput`; `RenderNodeEndRender` declares and consumes its final Graphics use, so the allocator appends one acquisition-aware `SwapChainPresentTransition` action to that real command scope. The existing final synchronization node submits the scope. Allocator completion requires a matching Submitted Graphics receipt with a valid Graphics fence, and only then does the retained terminal path call worker-safe native `Present`. `RenderNodePresent` remains the translated packetless terminal marker rather than a second transition or submission site. Discard and post-execution fence-signal failure cannot falsely acknowledge the transition, and a known submitted transition cannot be abandoned. Texture output retains its separate completion path. No next-frame presentation queue or drain was introduced.

Lifecycle closure preserves the existing service sequence and makes its ownership assumptions explicit. `ViewportManager::Shutdown` first rejects un-abandoned main-thread frame construction, drains the command tail without holding a viewport gate, then verifies that no retained output transaction or native acquisition survived before releasing viewport resources. After command-system shutdown, `RenderingService` explicitly clears the renderer-owned graph cache before device teardown; allocator pool clearing remains a distinct later step after GPU owners are shut down. Allocator shutdown repeats graph-cache clearing as an idempotent safety boundary used by device-abandonment and rollback paths. Consequently cached node storage cannot outlive allocator/RHI shutdown even when rendering has no backend, and no new shutdown coordinator or frame scheduler is required.

### Tests

- identical structural frames hit one graph entry while retaining independent frame-local bindings;
- hash collision requires full key equality;
- failed graph rebuild leaves the selected entry invalid and never exposes partial topology;
- LRU uses accepted frame serial and mutation waits for the preceding serialized frame tail;
- feature/output/view/dependency changes select a different graph entry, and any future queue/backend builder branch first becomes part of the complete key;
- main-thread acquisition transfers exactly once to the retained frame on successful command dispatch and remains there until its terminal disposition;
- synchronous failure abandons on the caller; worker never sees the transaction;
- resize/rebind/destruction serialize with Present without a renderer-wide race;
- Presentation, Texture, and Headless outputs take their exact terminal path;
- Present never runs after missing final submission, allocator failure, stale acquisition, or DeviceLost;
- failed Present preserves already-completed allocator and camera dispositions;
- device recreation invalidates old lookup ownership;
- shutdown with frames in flight drains safely and holds no lock while waiting.

### Exit gate

A real frame travels through `RenderingService` and `FrameRenderer`, selects or rebuilds a RED-shaped graph-cache entry, executes its built `RenderNodeGraph`, submits, closes the allocator, and presents or completes its exact current-frame output. Fresh Debug and Shipping engine/rendering/RHI tests pass, followed by a correction pass.

## 10. Stage RG7 — Consolidation And Production Exit Gate

> Status: RG7A source consolidation and direct-allocator test migration are complete. RG7B.1 removed dead cache-key promises, a unique-node identifier and a StorageData fence copy, and added Shipping-active factory group bounds. The subsequent full reevaluation found recording-failure propagation, presentation-lock, accepted-frame ownership, failure-message lifetime, factory, cycle-validation, and declaration-permutation defects. Both authorized correction passes below are implemented. RG7B.2 then closed the retained-frame, continuation, submission-receipt, allocator/RHI, viewport-output, cache-lifetime, and shutdown source audit and corrected empty inline RHI diagnostic fallback handling. RG7C.1 regenerated the Premake projects and compiled and linked the complete seven-target matrix in Development, Profile, and Debug. No test executable has run, no behavioral proof is claimed, and Shipping remains part of the later exit gate.

### Post-reevaluation correction record (2026-09-06)

**Policy update:** the user subsequently selected fail-fast recording. The shared recording-cancellation latch/checks described below are historical and have been removed. Required recording actions and post-dispatch scheduling failures now terminate through `VG_FATAL`; `Process()` calls `BeginNewNode()` and `Execute()` directly. Pre-recording setup skips and native device-loss receipt handling remain. See design Section 52's current recording policy. The other corrections below remain implemented. Lock optimization is separate and has not started.

Pass 1: execution, ownership, and synchronization.

- Reuse `RenderFrameCommandLists`' first-failure state as an atomic recording/submission stop flag. `Process()` checks again after `BeginNewNode()`, so failed resource actions cannot fall through into concrete `Execute()`. The epilogue publishes failures before dependent jobs become ready, and failed finalization cancels the packet cursor. Scheduling failures publish before draining issued work; command-list child scheduling uses the shared atomic state without racing a still-recording child's local failure object.
- Submission stops when that flag is set. Already submitted work retains its real fence/unknown-completion receipts; remaining command lists and dependencies receive discarded dispositions only after the recording branch has joined. No attempt is made to undo GPU work already submitted, and device loss remains a recovery boundary rather than a promised recoverable frame error.
- Allocate a node's deferred epilogue before `Execute()` can issue children. Join the independent node branch into the frame continuation through a small Jobs builder-to-builder adapter using the imported native counter operations; do not allocate a public Counter wrapper and then wait on the current continuation if allocation fails. Rejection of a preallocated task by a valid builder is a fatal backend-contract violation, not a recoverable self-wait path.
- Preserve dispatch acceptance when public CPU-tail counter allocation fails: join the independent root builder synchronously, then return the accepted serial. The viewport must not retry a frame already executed.
- Native Present consumes the exact acquired-output transaction without a viewport operation lock. Main-thread lifecycle mutation joins the render-command tail first, and the atomic output-owned bit rejects competing acquisition without serializing unrelated viewports. Stable manager-owned viewport objects remain directly accessible. Retain StorageData RHI failure text before its stack-owned failure object goes out of scope.

Pass 2: authoring integrity and bounded cleanup.

- Fail fast on node/group allocation failure and invalid factory ownership/group structure instead of silently constructing a smaller graph. Make dependency endpoint/type/self-edge/duplicate checks and graph-capacity checks Shipping-active.
- Validate each CPU/GPU dependency domain for cycles before its existing recursive flow-level calculation. This is build-time validation, not a replacement planner or scheduler.
- Keep the original 1021 traversal stride whenever it is coprime to node count; otherwise select a coprime stride so every occurrence is declared exactly once, including 1021 and its multiples.
- Remove the unused flattened-order validator, obsolete 512-node constant/bucket helper, the unused Resolve creation builder, and continuation-builder allocation for nodes that declare they do not use one. Preserve the optional direct allocator Resolve builder parameter and all external node-container/group APIs.

Validation here is source inspection and whitespace checks only. The later authorized gate must exercise failed begin/execute/epilogue/submit, grouped children after partial scheduling failure, accepted-frame counter allocation failure, ordered resize across terminal Present, factory allocation failure, CPU/GPU cycles, and traversal at 1021/2042 nodes, in addition to the full matrix below. These regressions have not been run.

### RG7C.1 compilation and linkage closure (2026-09-06)

- Premake VS2022 regeneration and the engine-source policy audit pass.
- `renderingTests`, `geometryAllocatorTests`, `rhiTests`, `rhiNvrhiTests`, `engineServicesTests`, `materialRuntimeServiceTests`, and `textureResidencyServiceTests` compile and link in Development, Profile, and Debug with serialized `/m:1` builds.
- The gate corrected only integration seams: explicit concurrency includes, const-qualified pointer atomics, allocator count/helper types, Rendering-pool ownership for polymorphic graph nodes and cache camera storage, per-occurrence context construction for asynchronous declaration jobs, intentional ignored factory IDs, viewport slot construction, and one stale test dependency on a now-private allocator schedule accessor.
- No test executable was run. Runtime behavior, fresh Shipping builds, and the complete behavioral matrix remain open.

### RG7C.2 behavioral exit gate complete (2026-09-06)

- All seven test executables pass serially in Debug and in fresh Shipping builds. The NVRHI suite exercised the real D3D12 backend on the available NVIDIA adapter rather than taking an unavailable-backend skip.
- `renderingTests` now directly covers graph identity and CPU/GPU dependency domains, flow-group construction, dependency groups including members registered after a boundary was authored, unique/sequence composition, forced per-view indices, helper removal, cache hit and full-key hash-collision rejection, unpublished-build retry, LRU replacement, invalidation, and exact declaration traversal at both 1021 and 2042 nodes.
- The RHI presentation test now creates a fresh recording command list for its presentation transition. Rebinding an already closed and submitted list was a stale test assumption and correctly fails under the current RHI contract.
- The new declaration witness is atomic because declaration buckets execute concurrently; the initial plain counter was a test race detected by the Shipping run and was not a graph failure.
- Deterministic pre-scheduling failure is covered through the allocator's real bounded-capacity contract: a two-occurrence graph configured for one planning writer reports `CapacityExceeded`, executes no declaration callback, publishes no generation, cancels, and returns to `Idle`. CPU/GPU cycles, dependency-group bounds, mismatched scheduled node occurrences, and invalid ordered submission synchronization run in isolated subprocesses. A valid imported Jobs builder does not expose a recoverable dispatch-rejection state after wrapper validation, so no global scheduler-failure switch was added solely to manufacture one.
- Viewport alignment RG7C.2A through RG7C.2E is complete: stable owned viewport objects, flush-before-mutation ordering, direct frame retention, removal of copied viewport/presentation state, and deletion of the compensating per-slot locks are implemented. Same-frame graph Present and Vanguard's exact NVRHI output transaction remain unchanged.
- RG7C.2F proves cache eviction while execution is retained. The least-recently-used graph's existing exclusive execution flag blocks its worker-side reset until terminal release, after which eviction reuses that exact entry. No additional retention or cache synchronization was introduced. Debug and Shipping `renderingTests` pass.
- Controlled fatal-contract coverage launches the same `renderingTests` binary in isolated child modes and requires the exact Windows fail-fast termination code for CPU cycles, GPU cycles, out-of-range dependency groups, a scheduled declaration/execution occurrence mismatch, and invalid submission synchronization in Debug and Shipping. The latter two use the real graph/allocator/node scheduler and ordered close/submission continuation. Existing RHI tests separately cover malformed submission receipts and post-execution fence failure after work submission. This adds no production hook or dispatch-time branch. Memory-pool budgets are not treated as deterministic allocation-failure injection.

### Work

- Trace every owner, reference, continuation, command list, allocator generation, prepared family, cache entry, output transaction, and failure branch for exactly-once closure.
- Remove dead provisional adapters, obsolete former-plan names, duplicate validation, hidden insertion-order dependencies, and unused cached fields.
- Consolidate shared failure classification without erasing subsystem-specific evidence.
- Audit all public APIs for bounded allocations, generation checks, noexcept behavior, and Shipping validation.
- Verify unsupported Copy paths fail closed and no code path fabricates synchronization.
- Verify command-scope tracker seeding, alias activation, Present transition, and fence ownership on NVRHI/D3D12.
- Audit cache contention, candidate rollback, eviction lifetime, multi-view implementation reuse, and shutdown ordering.
- Update design, examples, and checkpoint only for implemented truth; preserve post-baseline items explicitly.

### Complete matrix

Run fresh Debug and Shipping builds using the repository's Premake-generated projects, serialized with `/m:1` when concurrent agents could share PDBs:

```text
renderingTests
geometryAllocatorTests
rhiTests
rhiNvrhiTests
engineServicesTests
materialRuntimeServiceTests
textureResidencyServiceTests
```

Development and Profile configurations must compile. D3D12 tests may skip only when the environment genuinely lacks the backend or adapter; an unrelated whole-solution failure is reported separately from the affected-target matrix.

The behavioral matrix must cover:

```text
Graph        ownership, IDs, groups, sequences, unique merge, multi-view composition
Compiler     declaration, hazards, CPU/GPU cycles, scopes, budgets
Allocator    dedicated, placed, alias activation, imports, Present transition
Execution    CPU-only, grouped recording, parallel scopes, child jobs, cancellation
Submission   FIFO, Graphics/Compute edges, partial submit, signal failure, receipts
Cache        hit, collision, failed build, LRU, eviction in flight, invalidation
Output       Presentation, Texture, Headless, resize, failed Present, DeviceLost
Lifetime     builder death, frame-pool reset, shutdown in flight, stale identities
```

### Completion definition

The Render Graph foundation is complete enough to start renderer feature nodes when a real frame reaches a real output through the graph, every dependency has executable meaning, every lifetime closes once, partial progress has truthful recovery, dedicated and placed allocator paths are driven by real declarations, unsupported crossings fail before execution, and no temporary executor or hidden synchronizer remains.

## 11. Explicit Post-Baseline Work

These items are intentionally deferred until renderer content or profiling proves the need:

1. general external producer-fence/consumer-queue waits and unrestricted Copy crossings;
2. automatic cross-group compatible scope merging;
3. a persistent cache shared across whole-frame graphs for reusable view graphs;
4. generic asynchronous graph exports and readback-result delivery;
5. finer dependency-pruning optimizations beyond correctness-required ranges;
6. multi-GPU scheduling and richer GPU timing/capture UI.

They may not be approximated through CPU order, global flushes, mutable cached state, or weakened receipts.

## 12. Stage Status

```text
R0-R9  RED-faithful architecture study and final rewrite       COMPLETE
RG0P   Byte-exact RED core port baseline                       COMPLETE
RG1    Graph ownership and RED authoring model                 COMPLETE
RG2    Resource preparation and node context                   COMPLETE
RG3    Allocator replay and RHI scope-state contract           COMPLETE (source-only gate; runtime validation deferred)
RG4    Retained execution and RED node context                 COMPLETE (source-only gate; RG6 supplies cache selection)
RG5    Submission, receipts, and exactly-once terminalizer     COMPLETE (source-only gate; runtime validation deferred)
RG6    Same-frame output, cache, and service lifecycle         COMPLETE AT SOURCE-ONLY GATE
RG7    Consolidation and production exit gate                  REOPENED (bounded post-review corrections and failure-path verification)
```

RG1 and RG2 are closed at their authorized source-comparison gates. RG3A-RG3D completed the direct allocator API and request-stream correction boundary. RG3E.1 added the missing per-frame eviction policy, RG3E.2 added the RED-shaped typed node-resource facade with frame-owned per-occurrence bindings and nested named scopes, and RG3E.3 closed raw allocator/writer access from ordinary nodes while preserving private group/sync queue registration. RG3F is complete at its source-only correction gate: RHI policy/seeding, automatic-reset entry compilation, and opt-in explicit dedicated/imported/placed texture and buffer state are implemented. Explicit placed storage remains allocator-selected and single-queue Graphics-or-Compute/Common-only; successful batches return to Common, while an abort involving any explicit placed object destroys the heap-granular batch after submitted fences through observation-backed release. Compute placement now rejects render-target/depth textures without UAV capability before planning because their required alias discard cannot execute on a Compute recorder; those allocations fall back to dedicated storage. Explicit buffers normalize every scope to Common, making metadata independent of submission batching/decay; external buffer states remain Common only. Explicit state may cross Graphics/Compute only through authored `ForkAsyncCompute` / `JoinAsyncCompute`; Graphics-only producer state is normalized to Common on the producer before a Compute consumer, while Compute-to-Graphics preserves the exact state. Copy crossings remain gated. Defaults are unchanged. Runtime graph-to-RHI validation remains deferred. See design Section 52. Do not compile or run tests during these mechanical translation passes.
