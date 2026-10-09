# Resource Flow Allocator Execution Plan

Status: Stages 1–5 complete; Render Graph integration remains a separate project

Date: 2026-09-01
Implementation: planning, synchronous logical Resolve, validation, immutable packets, and terminal receipts

Integration correction: Section 13 supersedes the public session-and-survivor calling shape in Sections 2, 3, and 9 before Render Graph RG3. The completed implementation has not yet been migrated.

This plan turns the authoritative V1 contract in
`resource-flow-allocator-design.md`, Sections 81 through 90, into bounded code
stages for Vanguard. It does not reopen the RED/Unreal study and it does not
records the approved Stage 1 implementation. Later production stages remain
approval-gated.

The final V1 intended result is a renderer-owned Resource Flow Allocator with:

- one planning pass and one virtual node execution;
- deterministic logical lifetime compilation after graph culling;
- mandatory whole-texture and whole-buffer reuse;
- optional same-kind, same-queue placed aliasing only on a proven RHI profile;
- immutable per-node execution packets;
- retained imports and atomic terminal exports;
- real submission receipts for retirement;
- one hard native-byte ledger that remains charged until native destruction;
- no second render graph, scheduler, submission system, or RHI lifetime manager.

Planning is instantiated once. Execution never reruns the declaration body to
reconstruct resource state.

---

## 1. Existing Vanguard Boundary

The implementation must extend what Vanguard already has.

| Existing system | Reuse | Missing allocator-facing contract |
| --- | --- | --- |
| `FrameRenderer` | Device-lifetime renderer owner and future graph seam | Allocator ownership and lifecycle |
| `RenderCommandSystem` | Existing serialized renderer CPU chain and `jobs::Builder` | Future terminal/late failure reporting only |
| RHI owning `Texture`, `Buffer`, and `Heap` refs | Physical ownership | No replacement ownership layer |
| Explicit RHI transitions, discard, and alias operations | Backend command lowering | Compiled semantic action source |
| RHI command-list submission | Real GPU fences | A complete multi-queue submission receipt |
| RHI `ResourceLifetimeManager` | Fence-safe native destruction | A narrow, pollable native-release observation |
| RHI memory requirements and heaps | Placed-resource primitives | Truthful descriptor queries, heap classes, and validation |
| Existing test targets | Test harnesses and device setup | Focused allocator cases |

Current hard boundaries:

- There is no production Render Graph or `process_node` implementation under
  `source/`. `FrameRenderer::RenderFrame` intentionally ends with
  `"FrameRenderer has no installed Render Graph executor"`.
- `FrameRenderer` runs on the existing render Jobs chain. The allocator must not
  create another frame scheduler or CPU tail.
- viewport output acquisition and presentation are currently main-thread-only.
  The allocator must not acquire a viewport output from the renderer worker.
- the current public `RenderFrameSubmission` serial is not an allocator
  retirement receipt and must not be expanded into one merely for this system.
- RHI `virtualResource` means deferred native memory binding. It does not mean a
  shader-visible virtual resource or lookup table.

The allocator can be implemented and tested with a synthetic graph schedule.
Production graph connection remains a later Render Graph task.

---

## 2. Locked File Layout

### New public rendering files

```text
source/rendering/include/vanguard/rendering/render_flow_resource_allocator.hpp
source/rendering/include/vanguard/rendering/render_flow_resource_execution.hpp
```

`render_flow_resource_allocator.hpp` owns the public planning side:

- typed ids;
- texture/buffer descriptors and access contracts;
- configuration, statistics, and structured failure;
- `RenderFlowResourceAllocator`;
- move-only `FrameResourceSession`;
- worker-local `ResourcePlanningWriter`;
- export identity placeholders, survivor-overlay, and queue-schedule contracts;
- retained import registration and executable export publication begin in Stage 2.

`render_flow_resource_execution.hpp` owns the narrow execution side:

- immutable execution-generation reference;
- packet view and compiled step identities;
- captured decisions;
- typed, move-only resolved texture/buffer uses;
- step cursor and packet finalization contract;
- terminal execution receipt.

Do not add a separate public `resource_flow_types.hpp` or a fake
`RenderNodeExecutionContext` in V1. Split a header only if real compile-time or
dependency pressure appears during implementation. The later real node context
will compose the execution API.

### New private rendering files

```text
source/rendering/private/vanguard/rendering/render_flow_resource_internal.hpp
source/rendering/private/vanguard/rendering/render_flow_resource_pool.hpp       Stage 2
source/rendering/private/vanguard/rendering/render_flow_resource_placed.hpp     Stage 4
```

- Stage 1 `internal` contains candidate tapes, logical scratch/publication
  records, lifetime/state facts, packet runtime state, and pending export
  placeholders. Stage 2 adds physical ownership/provider and pool records.
- `pool` contains whole-object cache keys, retirement states, byte accounting,
  and import/export backing records.
- `placed` is not created until Stage 4. It contains heap-range allocation,
  placement provenance, and placed-object/empty-heap caches.

The Stage 2 physical-provider interface will remain private. It separates
logical compilation from whole and placed assignment without becoming another
public RHI or owning a second native retirement queue.

### New rendering sources

```text
source/rendering/src/render_flow_resource_allocator.cpp
source/rendering/src/render_flow_resource_resolve.cpp
source/rendering/src/render_flow_resource_execution.cpp
source/rendering/src/render_flow_resource_pool.cpp       Stage 2
source/rendering/src/render_flow_resource_placed.cpp     Stage 4
```

Responsibilities must remain distinct:

- Stage 1 `allocator.cpp`: lifecycle, sessions, worker-local tapes, seal, and
  pre-publication cancellation;
- Stage 1 `resolve.cpp`: survivor/schedule validation, deterministic logical
  compilation, retained lifetime/state facts, direct synthetic ids, packet
  compilation, and atomic publication;
- Stage 1 `execution.cpp`: generation/cursor/use/packet validation, scoped
  resolved access, packet exhaustion, and terminal-receipt validation;
- `pool.cpp`: dedicated resources, budgets, retirement, and native-release tickets;
- `placed.cpp`: optional placed physical planning only.

### New tests

```text
source/rendering/tests/render_flow_resource_allocator_tests.cpp
source/rhi/nvrhi/tests/render_flow_resource_allocator_d3d12_tests.cpp  Stage 4
```

The logical and whole-provider tests join the existing `renderingTests` test
main through `RunRenderFlowResourceAllocatorTests`. The D3D12 file joins the
existing `rhiNvrhiTests` main. Do not attach allocator tests to
`geometryAllocatorTests` and do not create another executable initially.

The current Premake source globs already include these files. Premake changes
are allowed only if a test genuinely needs a private include directory or a
future dedicated executable becomes measurably useful.

---

## 3. Public Calling Shape

Vanguard uses `bool`, structured output failures, move-only owners, and
`noexcept`. The implementation should follow that existing style rather than
introducing a new general `Result` framework.

The implemented Stage 1 calls are:

```cpp
RenderFlowResourceAllocator::Initialize(config, failure)
RenderFlowResourceAllocator::Shutdown(failure)
RenderFlowResourceAllocator::IsInitialized()
RenderFlowResourceAllocator::BeginFrame(frameSerial, policy, outSession, failure)
RenderFlowResourceAllocator::GetStats()

FrameResourceSession::CreatePlanningWriter(node, flowGroup, commandScope, outWriter, failure)
FrameResourceSession::SealCandidates(planningJoinProof, failure)
FrameResourceSession::Resolve(survivingOverlay, queueSchedule, jobs, outGeneration, failure)
FrameResourceSession::BeginExecution(failure)
FrameResourceSession::PacketFor(node, outPacket, failure)
FrameResourceSession::Finish(terminalReceipt, failure)
FrameResourceSession::CancelBeforePublication()
```

The exact overload syntax may adapt to existing container/ref conventions, but
the ownership and failure behavior above is locked.

`SealCandidates` is non-blocking. The caller presents proof that its planning
counter is ready, and the session verifies that every writer was closed. It must
never wait on a Jobs worker.

Stage 1 `Resolve` is synchronous and publishes atomically before returning. Its
builder parameter is reserved for Stage 2 physical creation work; any such jobs
must join before publication.

---

## 4. Stage 1 — Logical Compiler And Execution Packets

### Files

Add:

```text
render_flow_resource_allocator.hpp
render_flow_resource_execution.hpp
render_flow_resource_internal.hpp
render_flow_resource_allocator.cpp
render_flow_resource_resolve.cpp
render_flow_resource_execution.cpp
render_flow_resource_allocator_tests.cpp
```

Modify:

```text
source/rendering/tests/viewport_tests.cpp
```

Only add the allocator test entry point to the existing test main.

### Work

1. Add all typed logical, plan-position, node, command-scope, use, decision,
   generation, and physical ids required by Design Section 83.
2. Add the frame session state machine through logical `Ready`, plus
   pre-publication abort and stale-generation rejection.
3. Implement worker-local planning writers. Each GPU flow group owns one ordinal
   domain, including all ordered subnodes in one command-list group.
4. Seal verifies the join proof, writer health, and aggregate limits without
   merging. Resolve deterministically orders surviving flow groups and local
   ordinals; worker id, arrival order, and completion order are irrelevant.
5. Match resource names through exact bytes as authority; hashes select buckets
   only. Temporary identity derives from declaration position, not display name.
6. Implement declarations, declare-like, temporary resources, typed views,
   uses/scopes, logical swaps, and captured decisions. Retained imports,
   executable export requests, and external exchange begin in Stage 2.
7. Reject conflicting duplicate declarations and invalid scope/view/use
   operations before publishing resolved state.
8. Accept a read-only survivor overlay and compiled queue schedule supplied by a
   synthetic test harness. The allocator does not perform graph culling.
9. Build logical allocation versions, time-varying mappings, use bindings, and
   validate ordered texture-subresource and whole-buffer state/content
   progression. Retain allocation lifetime/scope/queue facts for Stage 2.
10. Compile immutable node packets and the narrow step cursor. Virtual node
    execution will consume these once; no planning request is replayed.
11. Enforce move-only, packet-liveness-safe resolved-use scope and exact packet
    exhaustion. Each packet with resolved resource uses owns a compact, lazily
    allocated liveness witness; resolved uses retain only that witness, never the
    execution generation or an independent physical resource. Begin/use/end
    remains lock-free after the packet is claimed.
12. Build Resolve entirely in scratch and publish one generation atomically.

Stage 1 assigns direct deterministic synthetic physical ids. It has no physical
provider, creates no RHI resources, and adds no production mini Render Graph.

### Tests

- reversed planning-writer close order produces identical synthetic assignments;
- temporary declarations at distinct positions remain distinct;
- duplicate declarations fail atomically;
- culling one endpoint rejects a broken cross-node scope;
- swaps affect only uses after their position;
- captured decisions replay from immutable packets;
- explicit `BeginExecution`, queue/kind/order/exhaustion, stale packet, and
  generation-bound terminal-token checks fail safely;
- overlapping uses are tracked independently, while incompatible active uses
  of one allocation are rejected;
- state masks, access intent, queue capability, texture subresources, and view
  ranges are validated during Resolve;
- expired resolved uses remain memory-safe and invalid after matching `EndUse`;
- writer poisoning, aggregate resource/view caps, duplicate schedule order, and
  pre-publication abort are verified.

### Exit gate

`renderingTests` passes without initializing an RHI device, and the logical
compiler can drive a complete synthetic frame through planning, Resolve, packet
consumption, and Finish.

---

## 5. Stage 2 — Whole-Resource Baseline And Hard Budget

Whole pooling does not use deferred binding, heaps, or alias barriers. It uses
ordinary `Texture`/`Buffer` owners with `virtualResource = false`, explicit
state transitions, real submission fences, and the existing RHI lifetime
manager.

Stage 2 has two ordered parts because the hard-budget claim requires data the
current RHI does not yet expose truthfully.

### Stage 2A — Minimal RHI foundation

Re-evaluation lock (2026-08-31): keep this stage strictly at the RHI boundary.
It does not add allocator pools, imports/exports, heap placement, aliasing, or
budget policy. Descriptor validation and native descriptor conversion are
shared by creation and pre-create requirement queries so admission cannot
estimate a different object from the one later created.

Hardening gate (2026-08-31): checked requirement queries reject zero-sized,
undersized-buffer, or non-power-of-two-alignment results; successful submissions
must return residency evidence covering their completion fence; descriptor
queries reject logically retired handles. Hidden initial-data uploads also
publish their NVRHI graphics submission instance so a later compute fork waits
for the upload. Native-release observations are valid only for the backend
initialization that issued them and must be discarded before RHI shutdown.

Modify:

```text
source/rhi/include/vanguard/rhi/rhi_types.hpp
source/rhi/include/vanguard/rhi/rhi.hpp
source/rhi/include/vanguard/rhi/rhi_backend.hpp
source/rhi/src/rhi.cpp
source/rhi/tests/rhi_tests.cpp

source/rhi/nvrhi/include/vanguard/rhi/d3d12/backend.hpp
source/rhi/nvrhi/private/vanguard/rhi/backend/common_backend.hpp
source/rhi/nvrhi/private/vanguard/rhi/backend/resource_lifetime.hpp
source/rhi/nvrhi/src/common_backend.cpp
source/rhi/nvrhi/src/resource_lifetime.cpp
source/rhi/nvrhi/src/d3d12_backend.cpp
source/rhi/nvrhi/tests/d3d12_backend_tests.cpp
```

Add only these general RHI contracts:

1. `SubmissionReceipt` containing the actual `ResidencyFenceSet` produced by a
   submission. `CloseAndSubmitCommandLists` already obtains per-queue fences in
   the common backend; it must stop discarding part of that evidence. A
   compatibility overload returning one `GpuFence` may remain temporarily.
2. checked `GetTextureDesc` and `GetBufferDesc` queries so the physical object,
   not an importer guess, is descriptor authority;
3. checked descriptor-based texture/buffer memory-requirement queries, with
   `u64` size/alignment, so hard-budget admission occurs before native creation;
4. structured create failure propagation that preserves at least out-of-memory,
   device loss, unsupported capability, invalid input, and general backend
   failure instead of flattening every failed create into `BackendFailure`;
5. a generation-safe, non-owning native-release observation:

```cpp
NativeReleaseObservation ObserveNativeRelease(ResourceRef, Failure*);
bool IsNativeReleaseComplete(NativeReleaseObservation);
void ReleaseNativeReleaseObservation(NativeReleaseObservation&);
```

The observation does not retain the resource and becomes complete only after
the existing lifetime manager destroys the native payload. Logical final
`Release` is too early. This is a completion signal over the existing queue,
not a second destruction system. Registration occurs before the allocator drops
its final pool-only reference. No observation allocation, callback list, or lock
is required: the ticket stores the exact `ResourceRef` generation, and the
existing lifetime slot publishes completion by advancing that generation in
`FinishDestroy`. Releasing the observation only invalidates the local ticket.

The allocator observes native allocations it charges: standalone whole objects
and native heaps. A placed child does not independently release heap bytes.

Tests must prove descriptor round-trip, pre-create requirements, complete
per-queue receipt, create failure fidelity, stale observation rejection, and
observation completion only after native destruction.

### Stage 2B — Dedicated provider, imports, exports, caches, and budget

Implementation checkpoint (2026-09-01): the first coherent Stage 2B slice is
implemented. Resolve now acquires ordinary dedicated textures/buffers when RHI is
available, publishes non-owning typed handles only through active execution
uses, rolls back unpublished assignments, and retires published assignments
from the complete terminal receipt. The persistent pool uses exact canonical
texture matching, compatible buffer-capacity matching, one hard byte ledger,
stable age-ranked soft trimming, and native-release observations that keep
evicted bytes charged until actual destruction. Logical-only validation is now
an explicit test mode; production frame planning fails immediately when no RHI
is initialized instead of silently publishing null native resources. The D3D12
integration harness proves real buffers and textures are created and reused
across completed frames.

The correction pass also locks the distinction between active resource shape
and future backing capacity. A dedicated texture is created at its exact active
extent and mip count; its maximum extent and mip count remain planning metadata
for the later placed-resource allocator. Compatible dedicated buffers may still be
created at maximum byte capacity because buffer subranges preserve one native
buffer identity. A D3D12 regression verifies that an active `8x8`, two-mip
texture remains physically `8x8`, two-mip while being reused across frames whose
planning maxima differ.

The lifetime-and-ordering correction phase now consumes the compiled first/last
`PlanPosition` and command-scope facts instead of merely recording them. Resolve
assigns compatible, non-exported, allocator-owned logical allocations to one
whole object only when their lifetimes are strictly disjoint. Same-queue reuse
requires increasing compiled order; Graphics/Compute reuse requires the exact
fork or join dependency between the predecessor's last scope and the successor's
first scope. Imports and terminal exports remain standalone ownership domains.
The same canonical compatibility predicate is shared with the persistent pool,
and compatible buffers use deterministic smallest-capacity selection.

The scoped-ownership correction enforces the corresponding native-reference
invariant at every allocator boundary. A pooled dedicated resource is eligible for
cross-frame reuse, pressure eviction, or soft trimming only while the pool owns
the sole strong RHI reference. Terminal publication is transactional and
requires exactly the pool reference plus the prepared publication reference;
an additional escaped owner rejects the complete transfer before any ledger or
ownership mutation. Active execution-use handles remain scoped borrows by
contract. The render-graph command facade should preserve that contract without
offering a promotable long-lived owner, while these pool checks remain the
authoritative backstop.

The budget/provider correction also closes the post-creation accounting gap.
Once native creation succeeds, every later descriptor, requirement, commit, or
native-release-observation failure keeps the object charged and quarantined
until the backend proves its native destruction. Provider failures retain their
specific allocator failure classification instead of being flattened into a
budget error, soft trimming reports failure to `BeginFrame`, and statistics poll
the release ledger before publication. Fault-injection coverage now crosses
every acquisition boundary and proves rollback, charge retention, atomic export
transfer, strong-owner exclusion, and eventual uncharge after native release.

`GpuFlowGroupId` remains the logical tape order and command-scope `stableOrder`
remains the submission order, but Resolve now rejects a surviving schedule when
those orders run backwards relative to one another. This closes the prior
implicit assumption that a total logical order was automatically a valid GPU
schedule. Logical tests cover overlap, strict same-queue reuse, reversed-order
rejection, and dependency-proved Graphics/Compute reuse; the D3D12 harness
compares the actual buffer handles and proves that three logical allocations
with two overlapping lifetimes require two native objects while the strictly
later lifetime reuses the first.

The use-boundary action checkpoint compiles ordinary dedicated-resource texture and
buffer transitions directly into each `UseBegin` step. The execution cursor
validates the exact packet, command scope, planned queue, and thread-bound RHI
command list before recording those actions. First use and command-scope
handoffs transition from an intentionally unknown backend state so NVRHI can
lower from its authoritative persistent tracking; transitions within one
command scope retain a concrete before-state assertion. Sequential UAV hazards
also compile explicit texture/buffer UAV barriers even when the required state
does not change. Action storage is bounded by
`maximumCompiledResourceActions`, and action failure poisons the packet so its
partially recorded command list must be discarded.

The queue schedule now carries explicit executable Graphics/Compute dependency
edges in addition to scope order. Resolve still rejects a queue change unless
the exact producer and consumer scopes are connected by the matching fork/join
edge; total flow order remains insufficient evidence.

Stage 2B.1 adds retained texture and buffer imports. The frame coordinator
registers each external resource before planning writers exist; registration
queries the authoritative RHI descriptor, validates the complete typed
contract, and retains the object exactly once. Repeated tokens coalesce only
when the physical object, descriptor, states, and queue match. Writers receive
only a generation-scoped `ImportedResourceId`.

Resolve installs an import into exactly one logical destination, starts state
tracking from the caller's concrete initial state, preserves imported content,
and appends the required terminal transition to the last use. The immutable
execution generation owns the retained RHI reference. Imports never enter the
dedicated-resource pool and never contribute to allocator-owned native bytes. The
real D3D12 harness verifies exact handle resolution, initial/use/terminal
transitions, duplicate-token behavior, ownership release, and zero pool charge.

Stage 2B.3 extends retained imports across an internally scheduled Graphics/
Compute region. The import's first and final uses must match its registered
initial and terminal queues, and every intervening queue change must carry an
exact executable dependency. Copy ownership and arbitrary incoming-fence waits
still fail explicitly because Vanguard has no general GPU queue-wait primitive
for either contract.

Stage 2B.2 adds same-queue terminal exports. The frame coordinator reserves
generation-scoped export slots before planning writers exist. A writer maps one
terminal logical allocation to each slot and declares its terminal state and
queue. Resolve requires a real executable final use, rejects duplicate ownership
of one physical allocation, compiles the terminal transition at that final use,
and records the final command scope.

Only successful terminal completion publishes exports. `Finish` first validates
every packet and scope receipt, then publishes the complete set atomically with
owning texture/buffer references, authoritative native descriptors, terminal
state and queue, and the actual fence from each resource's final submitted
scope. Allocator-created dedicated resources leave the persistent pool and native
byte ledger exactly once; an imported resource is republished from its retained
owner without entering that ledger. Abort and device loss publish nothing, and
an export can be taken from its slot only once between frame sessions.

Stage 2B.3 adds `CompiledQueueDependency` with exact producer/consumer scopes and
the existing RHI `ForkAsyncCompute` or `JoinAsyncCompute` mode. Resolve validates
balanced, non-nested regions, rejects simultaneous cross-queue uses, publishes
the dependencies in the immutable execution generation, and requires exact
dependency evidence again in the terminal receipt. Each dependency is classified
as submitted, discarded before submission, or unknown due to device loss;
successful completion accepts only submitted dependencies and verifies that the
corresponding lowering scope was also submitted. The executor remains the owner
of submission and lowers those modes through `CloseAndSubmitCommandLists`.
The D3D12 smoke path proves the actual NVRHI Graphics-to-Compute wait and
Compute-to-Graphics join, including a retained import whose registered ownership
moves from Graphics to Compute.

This is the bounded model selected by the study: RED emits the same fork/join
markers to allocation and submission, while Unreal derives explicit producer
relationships and enclosing graphics fork/join points for async-compute work.
Vanguard does not introduce an allocator-specific synchronization API.

`ExplicitFenceSignal` exports are now supported and preserve that readiness kind
beside the actual final-scope fence. `ExplicitFenceWait` imports remain rejected:
the existing `WaitForGpuFence` is a CPU wait and must not be substituted for a
missing GPU queue wait. Copy-queue transfers likewise remain deferred, as locked
by Design Sections 47, 75, and 87.

Stage 2B.4 adds typed texture and buffer clear values to declaration-time
initialization and individual resource uses. Resolve validates the value against
the exact format, usage, required state, queue, and texture subresource range,
coalesces matching declaration/first-use values, and emits one immutable clear
action after the required transitions and UAV barriers. Execution lowers those
actions through the graph-bound recorder to color-target, depth, stencil,
depth-stencil, floating/integer texture-UAV, or integer buffer-UAV RHI clears.
A scheduled clear establishes content provenance only on the compiled path;
recording failure poisons the packet through the same action-failure path used
by transitions and barriers.

The bounded declaration-time contract requires the first writable texture use
to cover the complete resource. Per-use render-target and depth/stencil clears
may cover validated subresources; the current RHI requires texture-UAV clears to
cover every array slice. This is explicit validation, not a hidden state
excursion. RED's optimized clear descriptor and executable node clear remain
separate concepts, and Unreal's activation/discard evidence likewise keeps
alias activation distinct from content initialization. Vanguard adapts that
separation while rejecting RED's untyped four-float clear representation.

Stage 2B.5 closes the bounded V1 failure, retirement, and hard-ledger matrix. A
private whole-provider seam can stop acquisition deterministically at requirement
query, native creation, authoritative descriptor verification, authoritative
requirement verification, or pool commit without adding a public fake-device
abstraction. Real-D3D12 tests prove scratch publication rollback after an earlier
assignment, hard-budget rejection and subsequent reuse, pending-retirement reuse
exclusion, pressure eviction and soft trimming with charges retained until the
RHI native-release observation completes, and the no-wait device-loss cleanup
path. The same critical cases run in Debug and Shipping.

Final Stage 2 correction checkpoint (2026-09-01): terminal receipts now prove
that each fork/join dependency agrees with the command scope that lowered it;
native successful completion accepts only submitted scopes and dependencies.
`SubmissionReceipt::WasSubmitted()` preserves the otherwise-lost distinction
between a command discarded before execution and native work issued before a
fence-signal/device-loss failure. The latter maps to unknown completion and can
never be reported as discarded.

Allocator, session, writer, and execution-generation lifetime now share an
internal retained control block. Losing either the public allocator or a
published frame coordinator fail-closes the generation, invalidates live use
handles, releases allocator-owned native owners through the RHI lifetime path,
and leaves a surviving public handle in `DeviceUnavailable` rather than dangling
or leaking. `ClearPersistentCaches` provides the same non-blocking native-release
observation path between sessions without consuming published exports.

The consolidation pass removes planner-only state after physical fixup, shares
queue/RHI-failure validation, scopes provider-failure injection to one allocator,
and replaces whole-pool linear compatibility lookup plus repeated oldest-entry
rescans with compatibility buckets, reusable slot tracking, and one stable sort
per pressure/trim pass. The stale NVRHI integration fixture was updated to the
current pipeline, GPU Scene lifetime, and material-resource APIs; its brittle
exact global page-count assertion now checks the intended minimum contract.

Exit-gate evidence: `rhiTests`, `rhiNvrhiTests`, `renderingTests`, and
`geometryAllocatorTests` were each rebuilt from scratch and passed in both Debug
and Shipping. The NVRHI and geometry suites exercised real D3D12 on an NVIDIA
GeForce RTX 4080 Laptop GPU.

The approved bounded Stage 2B scope is therefore complete. Arbitrary incoming
GPU-fence waits and Copy ownership transfers remain explicit future contracts if
the renderer requires them; they are not inferred from total graph order and are
not silently emulated with a CPU wait. Resource actions belong to execution
through the graph command recorder; they must not be issued by the persistent
resource pool.

Add:

```text
source/rendering/private/vanguard/rendering/render_flow_resource_pool.hpp
source/rendering/src/render_flow_resource_pool.cpp
```

Implement:

1. **Implemented in Stage 2B.1:** session-level import registry. It retains each external object once, obtains
   its authoritative descriptor from RHI, validates the expected contract, and
   exposes only `ImportedResourceId` to planning writers.
2. **Implemented in Stage 2B.2:** terminal exports. Resolve captures the terminal logical mapping; successful
   Finish publishes all export owners and readiness contracts atomically.
   Failure publishes none. Internally allocated V1 exports are standalone whole
   resources and leave allocator cache ownership.
3. **Implemented in bounded Stage 2B.3:** immutable, balanced Graphics/Compute
   fork/join dependencies; exact cross-queue use proof; retained import queue
   transfer; terminal dependency receipts; and explicit-fence export readiness.
   Arbitrary incoming waits and Copy transfers remain unsupported.
4. **Implemented in Stage 2B.4:** typed declaration/use clears compiled after
   transitions and lowered through the active graph recorder. Successful clear
   scheduling establishes explicit content provenance.
5. Exact canonical texture pool matching and explicit compatible buffer
   capacity classes. Full equality follows every hash lookup.
6. Explicit state/content provenance. Reusing an object does not transfer the
   previous logical owner's content; an unjustified `Load` fails.
7. Persistent states:

```text
Assigned
  -> PendingRetirement(real ResidencyFenceSet)
  -> Reusable
  -> EvictedPendingNativeDestruction(NativeReleaseObservation)
  -> NativeReleased
```

8. One allocator-wide hard native-byte ledger across texture and buffer pools,
   with optional per-memory-class soft targets. Pending retirement and pending
   native destruction remain charged.
9. Reserve descriptor-query bytes before native creation, verify the created
   object's authoritative requirements against that reservation, and only then
   commit the exact charge. A backend mismatch is a contract failure, not
   permission to overshoot the hard limit.
10. Stable trimming: eligible reusable objects first; CPU age ranks candidates
   but proves no safety.
11. Scratch physical assignment and atomic generation publication. Every failure
   boundary releases unpublished ownership through normal RHI refs.
12. Normal Finish, execution abort, and device loss. Submitted work is
   quarantined by its actual receipt; unsubmitted recorders are discarded;
   device loss enters `DeviceUnavailable` without waiting on poisoned fences.
13. **Implemented in Stage 2B.5:** private deterministic provider-failure
    injection and the rollback/retirement/hard-ledger closeout tests. No public
    fake-device abstraction is added.

Imported resources are reported but are not charged as allocator-owned native
bytes. A successfully published standalone export leaves allocator ownership and
its budget ledger atomically with publication.

### Tests

- canonical texture equality and compatible buffer capacity reuse;
- incompatible descriptors never reuse;
- same-frame reuse requires proven schedule order;
- cross-frame reuse waits for every graphics/compute/copy fence in the receipt;
- imports remain retained but are never pooled or aliased;
- import descriptor/ownership conflicts fail;
- exports remain empty on Resolve, Execute, or Finish failure;
- standalone export ownership leaves the cache exactly once;
- previous-owner content is undefined unless provenance permits preservation;
- soft trimming and hard admission use the same byte ledger;
- evicted bytes remain charged until their native-release observation completes;
- provider/native failure at every scratch-publication boundary rolls back;
- normal completion, ordinary abort, and device loss each clean up once.

### Exit gate

A complete synthetic graph frame uses real dedicated textures and buffers, executes
compiled packets, retires by actual submission receipt, and maintains an exact
hard live-native-byte ledger. Placed aliasing is still disabled.

---

## 6. Stage 3 — Truthful Placed-Resource RHI Contract

Stage 3 hardens RHI while the allocator's placed provider remains disabled.

Modify the Stage 2A RHI/NVRHI files and their tests. Add:

1. replace the coarse transient-heap/aliasing booleans with one granular
   placed-resource capability profile by resource kind, heap class,
   alias/discard lowering, and supported queue handoff mode;
2. authoritative nonzero compatibility classes and heap categories for buffers
   and textures, including any D3D12 RT/DS/MSAA restrictions;
3. complete stored `HeapDesc`; do not drop alignment or compatibility class;
4. immutable `PlacementRecord` on every bound texture/buffer containing heap,
   offset, size, alignment, compatibility class, and placement generation;
5. checked one-time `BindMemory` validation for deferred-binding status,
   resource kind, memory type, class, power-of-two alignment, aligned offset,
   checked `offset + size`, heap range, and double binding;
6. one semantic alias-activation API accepting the destination and all exact
   overlapping predecessor fragments. The backend chooses legal legacy or
   enhanced lowering and performs one discard/undefined activation package;
7. defensive validation that predecessor and destination occupy overlapping
   ranges in the same compatible heap;
8. precise structured failures in Shipping as well as Debug.

Practical V1 rule: do not patch upstream NVRHI merely to pretend D3D12 heap tier
1 is representable. Initially enable only a D3D12 profile whose separate
texture/buffer heap classes, requirements, binding, and barriers are proven.
Everything else reports placed unsupported and uses the dedicated provider.

Tests cover non-deferred bind, repeated bind, misalignment, arithmetic overflow,
out-of-range, wrong memory/class/kind, immutable placement query, disjoint/wrong
heap alias rejection, exact overlap success, one discard package, and capability
fallback.

### Stage 3A checkpoint — truthful placement foundation complete

The first Stage 3 slice is implemented while the Resource Flow Allocator remains
on `DedicatedResourcePool`:

- D3D12 advertises the placed profile only when native resource heap tier 2 and
  NVRHI virtual resources are both available. Tier 1 reports unsupported rather
  than exposing NVRHI's RT/DS-only heap as a general heap.
- the capability contract now identifies separate device-local buffer and
  texture classes, legacy alias-plus-discard lowering, graphics/compute
  same-queue support, and explicitly disabled copy/cross-queue handoff;
- memory requirements carry nonzero compatibility class, memory type, and heap
  category, while heap payloads preserve the complete public `HeapDesc`;
- successful one-time binding stores an immutable `PlacementRecord` with heap,
  checked range, alignment, class, category, and generation. Resource kind stays
  authoritative in the typed texture/buffer query rather than being duplicated;
- Shipping-visible validation rejects non-deferred resources, repeated binds,
  wrong memory/class/category, insufficient heap alignment, misaligned offsets,
  arithmetic overflow, and out-of-range placement without partially binding;
- façade fallback and real D3D12 tests pass in fresh Debug and Shipping builds.

The Stage 3A RED sanity correction also makes two native guarantees explicit:

- each placed class publishes the largest heap alignment guaranteed by its
  native heap creation path. D3D12 publishes 4 MiB, matching the fixed NVRHI
  heap alignment, and the façade rejects stronger caller claims;
- legacy alias activation always emits the D3D12 alias barrier, never performs
  a generic buffer discard, and performs an explicit texture discard only for
  render-target/depth-stencil destinations on a compatible graphics/compute
  queue. Copy-queue alias activation is rejected by the advertised profile.

### Stage 3B checkpoint — semantic alias activation complete

The pairwise alias-barrier façade and caller-controlled discard flag are
removed. `ActivateAliasedResource` now accepts one placed destination and the
complete immediate-predecessor set in canonical ascending clipped heap order.
Before recording anything, the backend proves:

- every resource has an immutable placement and is a distinct resource of the
  same kind, heap, memory type, category, and compatibility class;
- every predecessor overlaps the destination;
- clipped predecessor fragments are ordered, non-overlapping, gap-free, and
  cover the destination exactly;
- the active queue can legally lower the destination activation.

Validation is a linear, allocation-free command-recording operation. Successful
legacy D3D12 lowering emits exactly one null-before alias barrier, followed only
by the resource-appropriate texture discard established in 3A. Buffers never
receive a generic discard. Façade and real D3D12 tests cover missing placement,
coverage gaps, duplicate overlap, disjoint ranges, wrong heap, wrong kind,
noncanonical order, exact multi-fragment coverage, graphics RT texture
activation, compute buffer activation, incompatible compute-texture rejection,
and copy-queue rejection.

### Stage 3 correction checkpoint — RED parity and contract consolidation

The closing correction pass removes representation and backend surface that did
not add a distinct contract:

- `Capabilities::placedResources` is the sole placed-resource capability truth;
  the two coarse booleans are removed;
- resource payloads keep one authoritative `PlacementRecord`; the duplicated
  bound heap and placement kind fields are removed;
- alias lowering exposes only the semantic null-before destination barrier and
  a texture-typed discard callback. The unused generic buffer-discard path is
  removed because legacy D3D12 buffer activation requires no discard;
- placement validation remains serialized, but the placement lock is released
  before command-resource retention and native command recording;
- native tests now execute predecessor use, safe retirement, activation, and
  destination use on graphics and compute command lists, with explicit copy
  rejection.

The corrected façade and native suites pass fresh Debug and Shipping builds.

Stage 3 is complete. The placed allocator provider remains disabled until Stage
4 supplies the same-kind, same-queue planner and cache.

### Exit gate

The active RHI reports placed support only for profiles that pass façade tests
and real D3D12 conformance in Debug and Shipping. The allocator still uses only
dedicated resources.

---

## 7. Stage 4 — Same-Kind, Same-Queue Placed Provider

Add:

```text
source/rendering/private/vanguard/rendering/render_flow_resource_placed.hpp
source/rendering/src/render_flow_resource_placed.cpp
source/rhi/nvrhi/tests/render_flow_resource_allocator_d3d12_tests.cpp
```

Modify:

```text
source/rendering/src/render_flow_resource_resolve.cpp
source/rendering/src/render_flow_resource_execution.cpp
source/rhi/nvrhi/tests/d3d12_backend_tests.cpp
```

Implement:

1. checked `u64` heap-range arithmetic;
2. chronological first-acquire event sweep from Design Section 85;
3. stable same-event order: descending bytes, descending alignment, then stable
   logical allocation id;
4. split, release, and correct adjacent coalescing without losing predecessor
   fragment provenance;
5. stable first-fit compatible heap search;
6. separate texture and buffer heap classes and caches;
7. exact descriptor/heap/offset placed-object cache matching, with one assignment
   per cached object per frame;
8. semantic predecessor-finalization and alias-activation compiled steps;
9. same-kind, same-queue eligibility only. Reject copy queue, cross-kind, and
   graphics/compute handoff in initial V1;
10. dedicated-resource fallback under the same hard ledger. Fragmentation may fall
    back; budget exceeded, OOM, or device loss may not be hidden by another
    provider.

Tests cover exact fit, aligned splits, coalescing, fragmentation, multi-owner
spans, stable first-fit, touching/equal-position non-aliasing, overlap proof,
cached assignment uniqueness, unsupported queue/kind paths, rollback at every
provider failure, and byte-ledger consistency.

The real D3D12 test must:

1. create one eligible heap range;
2. bind owner A, write a known pattern, and finalize it;
3. activate owner B over the same range through the compiled semantic action;
4. write/read B and validate the result;
5. prove the resource and heap survive the real submission fences;
6. prove native bytes leave the ledger only after native-release observation.

### Stage 4A checkpoint — deterministic range planner complete

The first Stage 4 slice adds the pure scratch planner in
`render_flow_resource_placed.hpp/.cpp`. It deliberately performs no native RHI
mutation. The planner now provides:

- checked `u64` end and alignment arithmetic;
- chronological first-acquire ordering with descending bytes, descending
  alignment, then stable logical allocation id at one event;
- stable first-fit across heap classes separated by resource kind, queue,
  memory type, heap category, and compatibility class;
- release only when the prior last position is strictly before the acquisition,
  so touching/equal positions remain overlapping;
- aligned range splitting and adjacent coalescing without erasing exact
  predecessor fragments;
- rejection of partially virgin alias ranges because the Stage 3 semantic
  activation contract requires exact predecessor coverage;
- scratch publication: any late overflow or invalid request leaves the output
  plan empty.

Focused tests cover exact-fit reuse, aligned same-event packing, multi-owner
provenance, touching lifetimes, partially virgin ranges, fragmentation, kind and
queue separation, copy rejection, and early/late overflow. Debug and Shipping
`renderingTests` pass.

At the Stage 4A boundary, `enablePlacedResources` remained fail-closed because
the pure planner owned no native heaps, objects, retirement, or executable alias
semantics. Stage 4B below closes the native ownership half of that gap.

### Stage 4B checkpoint — transactional native provider complete

The second Stage 4 slice adds the native ownership layer below Resolve while
leaving public placed selection fail-closed. It now provides:

- one locked allocator-wide byte ledger shared by dedicated resources and
  placed heaps, with admission charged before native creation;
- exact reusable-heap matching by kind, queue, memory type, heap category,
  compatibility class, capacity, and alignment;
- exact placed-object matching by authoritative descriptor, heap, and offset,
  with pool-only-reference and one-assignment-per-batch guards;
- post-create verification of the authoritative heap descriptor, resource
  descriptor, memory requirements, bind result, and immutable placement record;
- fence-gated whole-batch retirement for heaps and their child objects;
- transactional acquisition: any later failure restores pre-existing cache
  states and removes every object created by that call;
- observation-backed failed-heap destruction. A reset wrapper does not release
  its ledger charge; the charge remains until `NativeReleaseObservation`
  confirms that the RHI destroyed the native heap;
- device-loss cleanup for both providers and public statistics for placed heap
  and object hits/misses, pending retirement, and pending native destruction.

The real D3D12 contract test proves two sequential logical buffers receive
distinct RHI objects over one aliased heap range, exact heap/object reuse on the
next batch, shared-budget exclusion against the dedicated provider,
pending-retirement exclusion, device-loss cleanup, and a two-heap partial
failure that remains charged until native-release observation. Fresh Debug and
Shipping `renderingTests` and `geometryAllocatorTests` pass.

### Stage 4C checkpoint — Resolve, execution, and retirement complete

The final Stage 4 slice connects the transactional provider to the published
execution generation. It now provides:

- public `enablePlacedResources` admission only for physical execution with an
  advertised placed-resource profile;
- Resolve-time selection only for transient, non-imported, non-exported
  resources whose complete lifetime stays on one supported graphics or compute
  queue. Copy and multi-queue allocations retain the dedicated-resource path;
- authoritative memory-requirement validation before range planning, followed
  by transactional placed-heap/object acquisition and generation ownership;
- predecessor finalization at the predecessor packet's terminal `UseEnd`, and
  alias activation at the successor packet's first `UseBegin`. Both are explicit
  compiled semantic actions and execute on the packet's bound command list;
- rollback of an unpublished placed batch, submitted-fence retirement of a
  completed or aborted generation, and device-loss invalidation;
- observation-backed persistent placed-cache clearing. Child objects are
  released before their heap, while the shared ledger remains charged until the
  RHI confirms native heap destruction;
- defensive physical-kind/provenance validation at the Resolve and execution
  seams, including the rule that terminal exports cannot own placed bindings.

The D3D12 execution proof assigns two distinct buffer objects to one aliased heap
range, clears the predecessor, finalizes it, activates and clears the successor,
copies the successor to readback, and validates the exact successor pattern after
a real graphics submission fence. The same test proves copy-queue dedicated
fallback, batch retirement, persistent-cache release, and a zero native-byte
ledger after release observation.

Fresh Debug and Shipping builds and runs pass for `renderingTests`,
`geometryAllocatorTests`, and `rhiNvrhiTests`. The public placed path is no longer
fail-closed on the supported D3D12 profile. Stage 4 is complete; broader
cross-queue ownership transfer remains outside its deliberately same-queue V1
contract.

### Exit gate

The tested D3D12 profile selects placed resources when eligible. Disabled or
ineligible profiles behave identically to Stage 2 through whole fallback.

---

## 8. Stage 5 — Renderer Ownership And Lifecycle

Modify:

```text
source/rendering/include/vanguard/rendering/frame_renderer.hpp
source/rendering/src/frame_renderer.cpp
source/engine/include/vanguard/engine/rendering_service.hpp
source/engine/src/rendering_service.cpp
source/engine/tests/engine_services_tests.cpp
source/engine/tests/texture_residency_service_tests.cpp   only for real-device coverage
```

Implement only the ownership boundary that exists today:

1. `FrameRenderer` owns `RenderFlowResourceAllocator` for the device lifetime.
2. `RenderingServiceConfig` carries allocator policy.
3. The service initializes it after RHI/device initialization and before render
   callbacks become reachable.
4. Initialization rollback releases allocator ownership before RHI teardown.
5. Quiesce drains the existing render CPU chain and verifies the session is
   `Idle`; it does not introduce a second tail.
6. Shutdown clears caches, lets RHI retire/flush observed resources, then
   finalizes the allocator observation state before RHI/device destruction.
7. A disabled or lost device leaves the allocator inactive or
   `DeviceUnavailable`, never partially initialized.
8. Expose diagnostics/stats through the existing renderer/service diagnostics
   style; no global singleton or public engine-service getter is required.

Do not replace the current missing-Render-Graph failure with a fake executor.
Do not acquire or present viewport output from the allocator. Those connections
require the real frame-output and Render Graph work.

### Exit gate

Service initialization, rollback, quiesce, recreation, and shutdown leave no
allocator-owned RHI refs or native-release observations. Existing rendering
behavior remains unchanged while no graph executor is installed.

### Stage 5 checkpoint — renderer ownership complete

- `FrameRenderer` privately owns the device-lifetime
  `RenderFlowResourceAllocator`.
- `RenderingServiceConfig::frameResources` carries allocator configuration.
- Required-device startup initializes the allocator immediately after the RHI;
  device-disabled startup leaves it inactive.
- Initialization rollback, device-loss abandonment, quiesce, and normal
  shutdown now include allocator lifecycle checks.
- Normal shutdown clears persistent allocator caches, flushes RHI retirement,
  verifies zero allocator native ownership/observations, and shuts down the
  allocator before destroying the RHI.
- `RenderingService` exposes allocator status and statistics, but not direct
  allocator access.
- The existing missing-Render-Graph failure remains intact. Stage 5 does not
  begin allocator frame sessions, acquire frame output, submit work, or invent
  a temporary graph executor.

Verified in affected-target builds with `engineServicesTests` and
`textureResidencyServiceTests` in Debug and Shipping. Coverage includes the
device-disabled path, real D3D12 initialization/shutdown, and rollback after a
later renderer subsystem fails initialization.

---

## 9. Later Render Graph Handoff

This is a dependency contract, not part of allocator implementation.

Historical note: this handoff predates the completed RED/Unreal Render Graph
study. Its references to one virtual node and planning every candidate are
provisional and are superseded by `render-graph-design.md` Sections 29-41 and
`render-graph-execution-plan.md`. The allocator lifecycle sequence below remains
valid; the new graph design supplies it from one canonical pass/use IR after
culling.

The future executor must:

```text
BeginFrame
  -> register retained imports, including an already-acquired frame output token
  -> instantiate one planning writer per stable node/flow/scope
  -> join planning counters and close every writer
  -> SealCandidates
  -> cull a frame-local graph overlay
  -> compile the real queue/command-scope schedule
  -> Resolve (Stage 1 synchronously; Stage 2 may publish after joined creation jobs)
  -> BeginExecution exactly once
  -> install PacketFor(node) in process_node
  -> execute virtual node Execute once
  -> finalize every node packet and executor control packet
  -> submit or discard every command scope
  -> terminal join
  -> build TerminalExecutionReceipt from actual SubmissionReceipt values and a
     generation-bound TerminalJoinToken
  -> Finish exactly once
```

The frame-execution object must copy scalar frame facts and retain the prepared
view family/custom data until terminal epilogue; it must not capture a dead
`RenderFrameInfo` reference in child jobs. Success commits the prepared view
family; failure releases it without commit.

`RenderCommandSystem` may later gain a thread-safe terminal failure reporter so
late Resolve/Execute/Finish failures reach existing frame diagnostics. It must
not own the allocator or graph.

---

## 10. Failure And Rollback Matrix

| Failure point | Required result |
| --- | --- |
| Planning validation | Candidate storage unchanged or writer-local failure |
| Seal/join proof | Remain in Planning; no Resolve starts |
| Logical Resolve | Release scratch records; publish nothing |
| Native creation/binding | Release unpublished refs through RHI; publish nothing |
| Generation publication | Atomic all-or-nothing visibility |
| Packet/command recording before submit | Discard recorder; cancel unstarted steps |
| Failure after any submit | Quarantine touched assignments by actual receipt fences |
| Terminal receipt incomplete | Reject without mutation; after work joins, submit a valid Aborted/DeviceLost receipt |
| Export preparation | Abort before any slot changes |
| Device loss | Invalidate caches/exports; no wait on poisoned fences |
| Cache eviction | Remove lookup ownership, retain byte charge until native release |
| Allocator shutdown | Drain CPU ownership, then coordinate existing RHI flush; no second retirement queue |

All critical validation is runtime validation in Shipping, not assertion-only
behavior.

---

## 11. Verification Matrix

| Concern | Existing target |
| --- | --- |
| Logical compiler, packets, deterministic algorithms | `renderingTests` |
| Whole pool, imports/exports, budgets with private provider seam | `renderingTests` |
| RHI façade, receipt, descriptor, and observation validation | `rhiTests` |
| Native requirements, placement, barriers, and readback | `rhiNvrhiTests` |
| FrameRenderer/service ownership | `engineServicesTests` |
| Real-device service shutdown/leak path | `textureResidencyServiceTests` when needed |

Per-stage verification runs the smallest affected targets first. Final V1
acceptance requires:

- Debug, Development, Profile, and Shipping compilation;
- critical allocator state/failure tests in Debug and Shipping;
- D3D12 conformance on a supported adapter, with a clean skip only when the
  backend/profile is genuinely unavailable;
- no changes to existing mesh/texture residency ownership;
- no hidden `WaitIdle` in normal frame Finish or cache maintenance;
- no leak of RHI refs, observations, generation refs, imports, or export slots.

---

## 12. Implementation Order And Approval Gates

```text
Stage 1  Logical-only compiler and execution baseline         COMPLETE
Stage 2  Dedicated-resource baseline and hard-budget support  COMPLETE
Stage 3  Truthful placed-resource RHI contract                COMPLETE
Stage 4  Same-kind/same-queue placed provider                 COMPLETE
Stage 5  FrameRenderer/RenderingService ownership             COMPLETE
Graph    Real frame/executor connection                       SEPARATE TASK
```

The shortest usable baseline is Stage 1, Stage 2, then Stage 5. The placed path
is a separate optimization track: Stage 3 then Stage 4. It may be deferred
without weakening correctness because Stage 2 is the mandatory provider. Every
started stage is reviewed and tested before its dependent stage begins.
Further production implementation remains blocked until the user approves the
next stage.

---

## 13. Approved Direct-Frame API Correction Before Render Graph RG3

The completed allocator's physical planning, packets, generation validation, receipts, pooling, and failure behavior remain valid. Its public `FrameResourceSession` and `SurvivingGraphOverlay` integration shape is superseded before production Render Graph connection.

The renderer owns one allocator and its frame chain is serialized. The allocator therefore owns its single active frame, session generation, collected request stream, published execution generation, and terminal phase internally. Public frame operations move directly onto `RenderFlowResourceAllocator`:

```cpp
RenderFlowResourceAllocator::BeginFrame(frameSerial, policy, failure)
RenderFlowResourceAllocator::RegisterImport(desc, outImport, failure)
RenderFlowResourceAllocator::ReserveExportSlot(outSlot, failure)
RenderFlowResourceAllocator::CreatePlanningWriter(node, flowGroup, commandScope, outWriter, failure)
RenderFlowResourceAllocator::Resolve(creationJobs, failure)
RenderFlowResourceAllocator::BeginExecution(failure)
RenderFlowResourceAllocator::PacketFor(node, outPacket, failure)
RenderFlowResourceAllocator::Finish(terminalReceipt, failure)
RenderFlowResourceAllocator::CancelBeforePublication()
```

`PrepareResourcesParallel` registers every finalized-definition resource operation plus its GPU flow group, command scope, queue, and explicit queue-sync request. `Resolve` consumes that complete allocator-owned stream. It receives no `RenderGraphDefinition`, `SurvivingGraphOverlay`, or `CompiledQueueSchedule`; the allocator knows resource execution positions and synchronization requests, not graph topology.

Removing the public session proxy does not remove generation safety. `ResourcePlanningWriter`, `CompiledExecutionPacketView`, resolved-use witnesses, identities, and terminal receipts remain stamped against the allocator's internal active generation. Pre-publication failure calls direct cancellation, while every published generation still leaves through direct `Finish` with complete receipts. Existing behavior and tests must be migrated before RG3 rather than weakened or duplicated through compatibility wrappers.
