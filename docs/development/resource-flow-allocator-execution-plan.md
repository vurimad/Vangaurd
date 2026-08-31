# Resource Flow Allocator Execution Plan

Status: Stage 1 logical-only baseline implemented and verified; Stage 2 awaits approval  
Date: 2026-08-31  
Implementation: planning, synchronous logical Resolve, validation, immutable packets, and terminal receipts

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
- `pool.cpp`: whole resources, budgets, retirement, and native-release tickets;
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
void ReleaseNativeReleaseObservation(NativeReleaseObservation);
```

The observation does not retain the resource and becomes complete only after
the existing lifetime manager destroys the native payload. Logical final
`Release` is too early. This is a completion signal over the existing queue,
not a second destruction system. Registration occurs before the allocator drops
its final pool-only reference.

The allocator observes native allocations it charges: standalone whole objects
and native heaps. A placed child does not independently release heap bytes.

Tests must prove descriptor round-trip, pre-create requirements, complete
per-queue receipt, create failure fidelity, stale observation rejection, and
observation completion only after native destruction.

### Stage 2B — Whole provider, imports, exports, caches, and budget

Add:

```text
source/rendering/private/vanguard/rendering/render_flow_resource_pool.hpp
source/rendering/src/render_flow_resource_pool.cpp
```

Implement:

1. Session-level import registry. It retains each external object once, obtains
   its authoritative descriptor from RHI, validates the expected contract, and
   exposes only `ImportedResourceId` to planning writers.
2. Terminal exports. Resolve captures the terminal logical mapping; successful
   Finish publishes all export owners and readiness contracts atomically.
   Failure publishes none. Internally allocated V1 exports are standalone whole
   resources and leave allocator cache ownership.
3. Exact canonical texture pool matching and explicit compatible buffer
   capacity classes. Full equality follows every hash lookup.
4. Explicit state/content provenance. Reusing an object does not transfer the
   previous logical owner's content; an unjustified `Load` fails.
5. Persistent states:

```text
Assigned
  -> PendingRetirement(real ResidencyFenceSet)
  -> Reusable
  -> EvictedPendingNativeDestruction(NativeReleaseObservation)
  -> NativeReleased
```

6. One allocator-wide hard native-byte ledger across texture and buffer pools,
   with optional per-memory-class soft targets. Pending retirement and pending
   native destruction remain charged.
7. Reserve descriptor-query bytes before native creation, verify the created
   object's authoritative requirements against that reservation, and only then
   commit the exact charge. A backend mismatch is a contract failure, not
   permission to overshoot the hard limit.
8. Stable trimming: eligible reusable objects first; CPU age ranks candidates
   but proves no safety.
9. Scratch physical assignment and atomic generation publication. Every failure
   boundary releases unpublished ownership through normal RHI refs.
10. Normal Finish, execution abort, and device loss. Submitted work is
   quarantined by its actual receipt; unsubmitted recorders are discarded;
   device loss enters `DeviceUnavailable` without waiting on poisoned fences.
11. Private deterministic provider-failure injection for tests. Do not add a
    public fake-device abstraction.

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

A complete synthetic graph frame uses real whole textures and buffers, executes
compiled packets, retires by actual submission receipt, and maintains an exact
hard live-native-byte ledger. Placed aliasing is still disabled.

---

## 6. Stage 3 — Truthful Placed-Resource RHI Contract

Stage 3 hardens RHI while the allocator's placed provider remains disabled.

Modify the Stage 2A RHI/NVRHI files and their tests. Add:

1. a granular placed-resource capability profile by resource kind, heap class,
   alias/discard lowering, and supported queue handoff mode; the existing two
   coarse booleans are not enough;
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
Everything else reports placed unsupported and uses the whole provider.

Tests cover non-deferred bind, repeated bind, misalignment, arithmetic overflow,
out-of-range, wrong memory/class/kind, immutable placement query, disjoint/wrong
heap alias rejection, exact overlap success, one discard package, and capability
fallback.

### Exit gate

The active RHI reports placed support only for profiles that pass façade tests
and real D3D12 conformance in Debug and Shipping. The allocator still uses only
whole resources.

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
10. whole-resource fallback under the same hard ledger. Fragmentation may fall
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

---

## 9. Later Render Graph Handoff

This is a dependency contract, not part of allocator implementation.

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
Stage 2  Whole-resource baseline and hard-budget RHI support  READY AFTER APPROVAL
Stage 3  Truthful placed-resource RHI contract                OPTIONAL; BLOCKED BY STAGE 2
Stage 4  Same-kind/same-queue placed provider                 OPTIONAL; BLOCKED BY STAGE 3
Stage 5  FrameRenderer/RenderingService ownership             BLOCKED BY STAGE 2 ONLY
Graph    Real frame/executor connection                       SEPARATE TASK
```

The shortest usable baseline is Stage 1, Stage 2, then Stage 5. The placed path
is a separate optimization track: Stage 3 then Stage 4. It may be deferred
without weakening correctness because Stage 2 is the mandatory provider. Every
started stage is reviewed and tested before its dependent stage begins.
Further production implementation remains blocked until the user approves the
next stage.
