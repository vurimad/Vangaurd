# Geometry batcher: source ledger and ownership boundary

9D source checkpoint (2026-09-09): RenderScene now retains the strong drawable
provided by the 9C StaticMeshComponent and publishes its renderable through the
existing GPU binding revision/acceptance path. See
[the component ledger](../../entities/docs/concrete-rendering-components.md).
No 9B proof gate was closed by this source-only work; 9D executable proof remains
deferred.

9B.2 recovery checkpoint, 2026-09-09: the interruption stopped after individual
shell/bin leases, before anchor-LOD aggregation, shared placement publication,
strong drawable binding, or accepted-lifecycle retirement. Those production
source paths are now implemented and statically inspected. Phase exit remains
unverified: no compilation, project generation, test execution or test authoring
was performed. No live publication acceptance or drawable scene output was
observed in this continuation.

## Source-to-implementation ledger

RED source root: `D:/root/R6.Root/Mainline/dev/src/common/renderer/src/`.

| RED source/responsibility | Vanguard implementation and boundary |
| --- | --- |
| `renderBatchMask.h`, `RenderBatchKey`: geometry and PSO compatibility distinctions | `GeometryShellKey` identifies phase, exact native pipeline and generational vertex/index arenas plus index format. `GeometryBinKey` adds exact geometry/allocation generations and indexed/vertex ranges. These are semantic adaptations, not verbatim packed-key ports. |
| `renderGeometryBatcher.cpp:214`, `BindBatchGeometry`: native geometry binding depends on the selected geometry | Shared shell keys preserve compatible arena bindings. Bins preserve arena-relative offsets without rebasing/copying vertices per instance. Actual indirect recording is not implemented in this checkpoint. |
| `renderGeometryBatcher.cpp:1385`, `ExecuteRenderGeometry`: finish compatible work before incompatible state changes | Shared destinations now exist; recording-local changed-state handling remains for indirect recording integration. No empty recording API is added to imply completion. |
| Parameter changes / material parameter binding | Bindless adaptation: neither material identity nor parameter/texture values enter shell/bin keys. Each retained placement still owns its exact material through the existing technique requests. |
| CPU visible-chunk sorting, transform gathering and static/dynamic instance pools | Not ported. GPU Scene retains transforms; Phase 10 produces per-view visible work and indirect arguments. |

`RenderGeometryBatcher` belongs to `MeshResidencyManager`. Its bounded lookup
tables are internal implementation storage, not an independent registry/service.
It does not compile native pipelines, submit uploads, collect scene instances,
perform culling or allocate per-view counters.

## Current resource-time transaction

1. `PrepareDraw` returns a retained mesh demand and normal/mirrored technique
   requests for one anchor primitive and phase.
2. `AcquireDrawPlacement` validates the retained topology/geometry/material
   identities and checks both technique states without waiting.
3. Successful acquisition interns both shell/bin variants and transfers the
   preparation into a `GeometryBatchLease`. Pending/failure leave the preparation
   and output untouched. Partial destination acquisition rolls back its references.
4. Equal effective pipelines reuse destinations. Every incompatible key field
   splits them. Cull-none alone does not make normal/mirrored variants equal:
   front-face winding still controls back-side normal orientation in the shader.
5. `Retain` shares ownership of the same immutable preparation/placement;
   releasing one lease cannot invalidate another. Last release removes unreferenced
   bins before shells, then returns mesh/material/technique ownership to the
   existing residency owners. Slot generations prevent stale identity reuse;
   generation exhaustion permanently removes that slot from reuse.

All ownership mutation is main-thread-only. Joined jobs may read the immutable
placement while its owner retains the lease. Main-thread resource preparation
uses bounded hash lookups; no global draw-loop lock or full-table scan is added.
The current static-surface contract has no per-object stencil reference, blend
constant or dynamic depth-bias overrides. Such future state must be represented
in compatibility/recording before it is supported, not silently ignored.

## Complete drawable closure and shared publication

A primitive lease does not prove complete LOD closure and does not write
`GpuPrimitivePlacement`, `GpuPhasePlacement` or `GpuRenderableResidency`. No
resident bit or new drawable state is exposed by this implementation.

`RequestDrawable` now collects every anchor primitive and registered phase, with
explicit attachment contexts and both effective winding pipelines. Equal calls
on the same residency share a move-only, retainable `MeshDrawableBinding`.
Incompatible contexts are rejected without overwriting the accepted generation.
Missing contexts and impossible compound capacities leave the output untouched;
asynchronous technique or shell/bin failure clears unpublished member ownership
and exposes `Failed` plus `GetFailure`. Cancellation releases unpublished work,
or preserves staged ownership until the contribution resolves.

The runtime-enabled manager initialization deliberately supplies the existing
`GpuSceneRuntime`. `GetRenderableAllocations` borrows the definition's real owner
ranges while the mesh demand keeps them alive. Renderable definition keys include
the exact resource and residency generations so separate mutable owners cannot
alias a parallel placement image merely because VMESH content matches.

Publication proceeds through one shared contribution, containing at most 64
complete compounds and respecting remaining update/byte capacity. Scene updates
defer when the combined staging byte budget is exhausted. No private uploader,
submission loop, wait, or retirement scheduler was added.

1. Every preparation and lease is ready; the geometry copy's actual GPU fence
   has completed (non-blocking polling).
2. Publish a disabled residency image plus complete primitive/phase placements.
   Frozen payloads remain pinned through acceptance or pre-submit retry.
3. Only after placement acceptance, stage the resident anchor bit in a later
   shared publication. The shared uploader uses the graphics queue.
4. Expose `GetDrawable` and placement borrows only after that residency receipt
   actually completes on the GPU. CPU acceptance alone is insufficient.

Each contribution has a non-wrapping serial and frozen member count; each
drawable has a non-wrapping placement revision. `PrepareDraw` also seals private
technique/phase/submesh provenance, so swapping public request fields cannot
silently admit another preparation. Internal shell/bin stale-release checks now
remain fatal in Shipping instead of relying on disabled assertions.

## Withdrawal, retirement, and failure

Last binding release prevents further retained access and queues residency
withdrawal. Staged payloads cannot be cancelled by deleting their owner. After
withdrawal acceptance, the renderable's actual definition allocations enter
`GpuSceneLifetime` retirement. The manager retains the complete lease/material/
geometry closure until the original renderable allocation becomes invalid after
collection. Only then can shell/bin references recycle. Remaining metadata
demands may build fresh topology afterward; old accepted placement is never
overwritten in place. Other binding holders keep the shared closure alive.

`RenderingService::RenderUpdate` joins the previous CPU submission tail, resolves
shared contributions, progresses residency and seals the existing retirement
owners using `rhi::GetSubmittedResidencyFences`. The RHI receipt set accumulates real
submission receipts from all queues, including failed calls which did submit.
Queues which have never submitted carry explicit no-work coverage, not invented
fence values. Ordinary empty/partial fence sets without that evidence remain
invalid. Unknown submitted completion prevents retirement sealing.

The shared uploader now preserves `workSubmitted` on failure and quarantines
its storage after submitted failure. GPU Scene retains affected contributions
until device abandonment; it cannot retry them as unpublished CPU ownership.
This handles the dangerous difference between pre-submit rollback and failure
after native execution.

Normal batcher shutdown rejects live leases. Device abandonment, after recording
jobs join, explicitly invalidates them and releases requests while mesh/material
owners still exist. Outstanding invalid leases remain safely resettable after
manager destruction; `GetPlacement` then returns null. Borrowed placement pointers
must not survive that teardown join. No invented fence values are permitted.

The same abandonment rule covers outstanding drawable binding objects: their
manager pointer is invalidated and their requests are released while owners are
alive. Normal shutdown continues to reject live ownership. Callers must release
bindings and continue normal publication/retirement maintenance before shutdown;
shutdown does not invent a private withdrawal submit path.

## Verification boundary

Static review follow-up, 2026-09-09: replaced the new drawable progression and
publication residency-table scans with one intrusive active-closure queue inside
the existing mesh owner. Ready and failed bindings are not polled. Creation,
last release, and acceptance/retry callbacks enqueue work without heap allocation
or a new lock. Staged closures leave the queue until their callback; teardown
unlinks queued closures before deletion. Publication skips newly cancelled
closures until Tick prepares withdrawal.

Tick examines at most 64 active closures and 256 preparation members total,
with at most 64 member checks per closure. It rotates processed work and stops
when the global member budget is consumed, so a large mesh cannot monopolize
each following tick. Publication examines at most 64 queued closures and no
longer invokes progression a second time. Full closure payload construction and
rollback remain one-time work proportional to that mesh's required members.
The older 9A topology/geometry maintenance loops were not rewritten by this
focused drawable review; this is not a claim that all mesh residency is scan-free.

The review also makes an individually oversized scene upload fail explicitly
instead of deferring forever. Failed submission receipt queries return empty output;
any failed call reporting submitted work invalidates automatic retirement
coverage until device abandonment, even if it carries a partial-looking receipt.
The atomic receipt maxima remain per native submission, where concurrent callers
can return out of order; no per-instance or draw-loop synchronization was added.

The changes were reviewed through source/call-site inspection and whitespace
diff checks only. Compilation, existing-suite execution, new adversarial cases,
and the combined 9B.1 vertical fixture remain deferred to the explicitly requested
final batch. In particular, the new retained lifecycle, idle-queue coverage,
submitted-failure quarantine, multi-caller cancellation, and capacity/retry paths
have not been exercised. 9B.2 is not declared verified or phase-complete.

## View separation

Shell/bin definitions and resident geometry are shared. View/pass constants,
candidates, selected LOD, visible membership, bin counts/prefixes/scatter output
and indirect argument/count ranges are per view or explicitly compatible pass.
They are deliberately absent from these shared tables. A shared bin never owns
one globally shared visible-instance list.

## 9D scene cutover

StaticMeshComponent passes its ready MeshDrawableBinding during normal proxy
admission. RenderScene retains independent ownership before returning success.
Direct low-level mesh proxies may omit a drawable and remain inactive in GPU
visibility; they cannot accidentally publish a bare renderable index.

The publisher returns a revision receipt for mesh bind and clear, matching the
existing decal acceptance mechanism. A mesh payload holds one accepted binding
and at most one candidate. Replacement or clear changes the publisher first,
then keeps the old accepted binding until that exact revision is accepted.
Superseding a pending candidate releases only that candidate. Proxy destruction
unlinks pending acceptance work and releases both retained bindings.

RenderingService resolves GPU contribution callbacks after joining the prior CPU
rendering tail, then asks RenderScene to resolve at most 256 changed bindings.
Pending payloads form an intrusive index queue inside existing scene storage.
Polling takes one existing publisher shared lock per affected scene batch, with
round-robin fairness across scenes and pending proxies. There is no live-proxy,
entity, residency-table or drawable-table scan.

`RetainMeshDrawable` is available when CPU work really crosses the joined scene
boundary. Ordinary Phase 10 candidate and recording work remains inside the
existing render-command tail, so it borrows the scene-owned accepted binding and
does not pay a retain per candidate. GPU execution after scene removal is
protected by the established drawable withdrawal and GPU Scene/RHI fence
retirement chain. 9D adds no second frame refcount, uploader, retirement queue or
visibility collector.

## Phase 10.1 recording-catalog checkpoint

Source implementation, 2026-09-09: `RenderGeometryBatcher` now exposes the
recording catalog directly over its existing interned shell/bin ownership.
Shells are stored in contiguous active spans per render phase. Each active shell
owns a dense list of its live bin IDs; each bin records its shell-local ordinal.
Activation occurs only on the first reference and removal only on the last, so
shared normal/mirrored placements do not duplicate catalog entries.

Removal uses swap-remove and repairs the moved shell/bin index immediately.
Generational shell/bin access rejects stale IDs without a table scan. Recording
access returns borrowed spans and performs no allocation or locking under the
existing joined owner-thread mutation boundary.

Shell/bin changes also enter bounded, coalesced dirty-index arrays reserved to
the configured maximum shell/bin counts. Direct-index lookup returns either the
current active record or its inactive generational tombstone. Publication
acknowledgement requires the exact catalog revision, preventing a late acceptance
from clearing newer dirtiness. The next 10.1 slice will copy these sparse records
into the shared GPU publication path; no GPU table or second publisher was added
in this catalog-only slice.

This checkpoint was inspected statically only. Compilation, project generation,
test writing and test execution remain deferred to the final requested batch.
