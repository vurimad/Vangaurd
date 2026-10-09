# RenderFlow RF descriptor-ownership program

Status: **RF0 Definition CLOSED. RF1 Descriptor-domain hygiene CLOSED. RF2 Dedicated persistent descriptors CLOSED. RF3 Placed canonical descriptors/global pressure/lifetime CLOSED. RF4 Export/failure/generation ownership cutover CLOSED. RF5 Delete steady-state publication/performance closure CLOSED — 0 BLOCKER / 0 HIGH / 0 MEDIUM.**

Date: 2026-10-05

## Program goal

RF removes normal per-generation whole-resource descriptor churn from RenderFlow.
Descriptor identity follows persistent physical-resource identity, so a warm pool hit
borrows stable SRV/UAV handles instead of repeating Allocate -> Write -> native
Create*View -> AddRef -> fence retirement every frame.

The intended steady-state result is simple: if a physical resource is reused and its
canonical descriptor shape has not changed, RenderFlow performs **zero native
whole-resource descriptor publication work for that resource in that frame**.

This is a focused RenderFlow ownership redesign, not a new render graph, descriptor
heap architecture, RHI lifetime manager, or public resource-binding API.

## Permanent invariants

- Preserve recording -> seal -> resolve -> execution/Finish and receipt-driven
  completion.
- Preserve logical-vs-physical separation, dense/generation-stamped identities,
  dedicated/placed split, exact alias provenance, and defensive export validation.
- Whole-resource resolved uses retain today's capability contract: if the physical
  resource supports SRV and UAV, both handles remain available regardless of the
  state of one particular use.
- Retained imports keep external descriptor authority; explicit/subresource views are
  not silently converted into canonical whole-resource descriptors.
- A descriptor-held RHI reference remains a real ownership reference. Pool ownership
  proofs must account for canonical descriptor references exactly rather than weaken
  refcount validation.
- No broad descriptor cache is added to generic RHI resource payloads in RF0-RF5.
- No performance claim is closed without matched profiling at the 320-resource probe.

## RF gates

| Gate | Scope | Exit condition |
| --- | --- | --- |
| **RF0 — Definition / contract** | Freeze descriptor ownership, borrowing, lifetime, export, placed-alias, failure/device-loss, pressure, and measurement contracts. Map exact symbols/tests and the migration order. | One reviewable architecture contract exists; no unresolved ownership ambiguity; RF1 entry is explicit. |
| **RF1 — Descriptor-domain hygiene** | Remove repeated retirement-list scans from normal allocation/retirement: O(1) free-list allocation while capacity exists, immediate zero-fence recycle, fenced enqueue, maintenance/pressure reclaim. Add focused RHI proofs. | Descriptor-domain behavior is semantically unchanged but no longer performs D×R/D² reclaim work; matched 320 capture establishes the new floor. |
| **RF2 — Canonical bundle + dedicated ownership/budget/lifetime** | Introduce the private lazy SRV/UAV bundle on `DedicatedResourcePool::Impl::Entry`, plus the allocator-level descriptor ledger/target/cap and dedicated cold-bundle pressure eviction. Create descriptors only on first whole-resource demand, return borrowed handles through assignment/binding records, update pool ownership baselines including same-frame reuse, and cut healthy `Shutdown`/abandon/destructor handling away from `DeviceLost()` for dedicated canonical owners. | Dedicated warm hits perform no descriptor Allocate/Write/Retire; dedicated persistence is bounded and teardown-safe from its first production gate; rollback/reuse/trim/quarantine/pressure/shutdown proofs pass; public resolved-use behavior is unchanged. |
| **RF3 — Placed-object ownership + global pressure/lifetime** | Put the same canonical bundle on `PlacedResourcePool::Impl::ObjectEntry`; preserve exact object/placement identity and alias rules. Extend the RF2 ledger/coordinator so cold eviction selects globally across dedicated + placed without cross-pool lock nesting, and extend RF2's healthy terminal-lifetime rules to placed objects/heaps. | Placed warm object hits reuse descriptors safely; held-owner, retirement, trim/cache-clear, alias, target/cap, global-pressure and healthy terminal teardown retain exact ownership proofs. |
| **RF4 — Export, failure, and generation cutover** | Make generations borrow canonical handles instead of owning them. On terminal export, detach/retire the pool bundle with the completed generation's submitted-fence union before releasing pool ownership. Keep retained imports external and explicit/dynamic descriptors as explicit exceptions. Remove descriptor-ref assumptions that belong only to generation ownership. | `Finish` remains terminal ownership authority; export refcount validation is exact; device-loss/failure paths cannot leak or prematurely recycle descriptors; normal generations own no canonical descriptors. |
| **RF5 — Delete steady-state publication + performance closure** | Remove the normal per-binding `FinalizeDescriptors` Allocate/Write path and any obsolete generation descriptor scan/state. Keep only explicit exceptional publication paths. Run full correctness suites and matched Profile captures: normal quad, graph-only, 64-resource mix, and 320-resource ceiling. | Warm 320-resource frames perform ~0 canonical descriptor Allocate/Write/Retire operations; `FinalizeDescriptors` is deleted or reduced to exceptional work; measured Resolve/phase results and regressions are documented. |

## Target ownership model

```text
Persistent physical owner
  Dedicated Entry / Placed ObjectEntry
        |
        +-- Texture/Buffer owner ref
        +-- Canonical SRV handle (optional)
        +-- Canonical UAV handle (optional)
        |
        +----> RenderFlow execution generation borrows handles
                    |
                    +----> ResolvedTextureUse / ResolvedBufferUse
```

Canonical descriptors live through `Assigned -> PendingRetirement -> Reusable` and
are retired only when their physical owner leaves the cache, is exported, loses its
device/domain, or the descriptor itself is deliberately evicted under descriptor
pressure. Generation teardown does not retire borrowed canonical handles.

## RF0 closure

RF0's definition draft closes the six required questions in
[`render-flow-rf0-descriptor-ownership-definition.md`](render-flow-rf0-descriptor-ownership-definition.md):

1. one private canonical descriptor bundle and transactional Ensure/retire contract;
2. exact pool-owned reference baseline `1 + populated canonical descriptor count`;
3. terminal export detach/retirement against the completed generation's submitted-
   fence union before pool ownership release;
4. bounded descriptor-cache policy with an 8,192 reusable target, 16,384 live hard
   cap, global cold-owner ordering and one bounded shared-domain retry;
5. allocator-lifetime descriptor-domain ownership plus exact rollback/device-loss
   ordering; and
6. focused RF1-RF5 correctness proofs plus matched normal/graph-only/64/320 Profile
   closure captures.

RF0 source/document review closed at **0 BLOCKER / 0 HIGH / 0 MEDIUM** after adversarial
rereads of ownership, terminal lifetime, pressure and validation contracts. RF1's exact
entry was the descriptor-domain reclaim hygiene gate. The user later explicitly
authorized RF1, which is now closed below.

## RF1 closure

RF1 is closed. Its durable implementation/validation record is
[`render-flow-rf1-descriptor-domain-hygiene.md`](render-flow-rf1-descriptor-domain-hygiene.md).

The gate removed repeated retirement-list sweeps from normal descriptor allocation and
retirement without changing public descriptor semantics:

- allocation pops the free list directly and performs one reclaim pass only on
  capacity pressure;
- retirement records fence use first, completes the known slot synchronously when its
  fences are already complete (including empty retirement), and otherwise only links
  that slot into the retiring list;
- maintenance `RetireResources()` retains the full retiring-list reclaim pass; and
- generation exhaustion, strong resource references, stale-handle rejection and
  descriptor-domain lock ordering remain intact.

Focused D3D12 coverage now directly proves immediate recycle/generation advance, stale
write/retire rejection, no reuse before a future fence, allocation-pressure reclaim
without `RetireResources()`, deterministic multi-entry staggered list unlink/reclaim,
and concurrent allocate/retire/recycle uniqueness and generation advancement. Final
adversarial source and test rereads closed at **0 BLOCKER / 0 HIGH / 0 MEDIUM**.

The established 320-resource Profile workload still shows that native descriptor
publication dominates. An older pre-RF1 trace cannot be used as a causal comparison
because the dirty checkout changed between captures. A same-checkout paired A/B that
temporarily restored only the pre-RF1 hot-path sweep behavior measured median
`FinalizeDescriptors` at **690.505 -> 664.607 us** and median `Resolve` at
**1676.515 -> 1627.278 us** after RF1. The direction is consistent with removing the
reclaim sweep, but RF1 does not claim a broad frame-speedup: run-to-run noise remains
material and the per-generation Allocate/Write/Create*View publication floor is still
present. RF2 remains the architectural gate that begins removing that work.

### RF2 closure evidence

RF2 is closed on the dedicated-resource boundary only. Debug and Development
`geometryAllocatorTests` passed the dedicated canonical ownership, warm-handle,
same-frame late-demand, descriptor-pressure, retryable-shutdown, and RHI-ordering
proofs. Debug and Development `renderingTests` and core `rhiTests` also passed. The
`rhiNvrhiTests` executable passed its descriptor-domain/state-lifetime coverage in
both configurations, then failed later in the independently dirty GPU Scene runtime
integration test; RF2 does not modify that subsystem and does not claim that failure
as closed by this gate.

A final post-closure adversarial reread temporarily reopened RF2 for two correctness
gaps. Descriptor pressure now refreshes externally-held reusable entries only when
local/shared descriptor capacity is actually exhausted, so a newly pool-owned cold
canonical bundle cannot remain invisible and cause a false `CapacityExceeded`.
Healthy shutdown now validates `Assigned` independently of descriptor presence and
rejects canonical descriptors on detached-native states. Focused regressions reproduce
the stale-Held pressure case while proving native-resource preservation, reject a
zero-descriptor `Assigned` shutdown, and synthesize both detached-native corruption
states through a narrow private test seam. The final Debug and Development
`geometryAllocatorTests` builds/runs pass, and exact-current adversarial rereads close
again at **0 BLOCKER / 0 HIGH / 0 MEDIUM**.

Matched Profile captures on the RF2 source show the intended steady-state ownership
shift. In the separate 64-resource RED-like workload, median `FinalizeDescriptors`
is **0.521 us** and median `Resolve` is **386.636 us**. In the preserved 320-resource
ceiling workload, median `FinalizeDescriptors` is **0.901 us** and median `Resolve`
is **1078.494 us**. The prior RF1 320 capture measured **595.898 us** median
`FinalizeDescriptors` and **1502.942 us** median `Resolve`; RF2 therefore removes
approximately **99.85%** of the median `FinalizeDescriptors` cost in that comparison
and reduces median `Resolve` by approximately **28.2%**. Canonical Allocate/Write/Miss
events appear in cold/republication bursts, while steady-state capture intervals show
only canonical Hit events and no canonical Retire events.

The RF2 boundary remains explicit historically: placed-object persistent descriptors
were RF3, and terminal-export canonical ownership transfer was RF4. Retained imports
remain externally owned. RF4 has now closed the exported-dedicated generation-owned
descriptor exception described here.

### RF3 closure evidence

RF3 is closed on the placed-resource and allocator-global pressure/lifetime boundary.
`PlacedResourcePool::Impl::ObjectEntry` now owns the same lazy canonical SRV/UAV bundle
as dedicated entries, using the allocator's single descriptor ledger and strong
descriptor-domain owner. Exact placed-object ownership remains `1 + populated canonical
descriptor count`; aliased native objects keep distinct descriptor bundles even when
they share heap storage, and escaped native owners are removed from descriptor-eviction
eligibility until the exact pool-owned baseline is recovered.

Descriptor pressure is now allocator-global without cross-pool lock nesting. The
coordinator queries dedicated and placed cold roots separately, orders candidates by
canonical `lastDemandFrame`, deterministic owner class and entry/object index, then asks
only the selected pool to revalidate and retire that descriptor bundle. Descriptor-only
eviction leaves the native resource/object and byte charge intact. Begin-frame
maintenance checks the shared reusable-slot count before any candidate refresh and runs
only when `FrameResourcePolicy::processEviction` permits it. Production Resolve uses the
pressure-aware paths exclusively: `LocalLimit` is relieved before RHI publication,
while `SharedDomain` rolls back partial publication and permits one global relief plus
one complete retry, preserving the two-total-RHI-publication-attempt bound.

Placed rollback, trim/cache-clear, quarantine and device-loss paths now retire or clear
canonical bundles before native object release. Healthy published-generation
abandonment keeps dedicated and placed owners on their normal reusable/pending-retirement
lifetime path instead of using `DeviceLost()`, so canonical bundles remain attached
until shutdown preparation. Healthy shutdown is two-phase for both pools: preparation
admits descriptor retirement without releasing native owners, the allocator performs
one shared `FlushRetiredResources()`, both pools are preflighted for commit, and only
then do non-recoverable commits release native objects/heaps and byte charges. Focused
coverage includes a placed non-device-loss retirement-flush failure that leaves native
ownership retryable until a later successful shutdown.

Final x64 `geometryAllocatorTests` builds and runs pass in Debug and Development with
outer MSBuild `/m:1`, including placed warm-handle reuse, alias separation, exact
reference baselines, held-owner recovery, descriptor-only eviction/republish,
dedicated-vs-placed global oldest selection, tiny target/cap pressure, shared-domain
transactional retry, healthy abandonment, retryable placed shutdown and device-loss
accounting. Exact-current adversarial source/test rereads close RF3 at **0 BLOCKER / 0
HIGH / 0 MEDIUM**.

RF3 makes no new performance claim. The preserved 64-resource and 320-resource Profile
SchedulerProbe paths do not enable placed resources in exact-current runtime, so their
existing captures are dedicated-only evidence and cannot measure RF3. Enabling placed
resources solely to manufacture an RF3 capture is explicitly outside this program's
scope. A deliberately placed-enabled matched workload can still be used for owner-specific
placed profiling if needed; the program-level RF5 matched performance closure is recorded
below and does not retroactively turn the earlier dedicated-only RF3 captures into placed
evidence.

### RF4 closure evidence

RF4 is closed on the export/failure/generation ownership boundary. Execution generations
no longer own a descriptor domain, descriptor-retirement fence set, or descriptor-loss
state, and generation destruction no longer retires whole-resource descriptors. Normal
resolved bindings, including dedicated resources selected for terminal export, borrow the
canonical SRV/UAV handles owned by their physical pool entry. Retained imports remain
externally owned and the explicit/subresource exception boundary is unchanged.

Terminal `Finish` remains the ownership authority. It first derives the submitted-fence
union for the completed generation and prepares publication owners, then dedicated export
transfer validates the exact native reference baseline as pool owner + publication owner
+ populated canonical descriptor references. All transfers are validated before any
destructive work. The pool then admits retirement of every canonical bundle against that
submitted-fence union before releasing any native pool owner or native-byte charge. Only
after every retirement admission succeeds are the physical resources detached from the
pool and their publications committed.

The failure path is explicitly retryable. Focused coverage forces the first canonical
handle retirement to succeed and the second to fail: `Finish` publishes nothing, keeps
the native pool owner and byte charge, clears only the already-retired handle, and leaves
the generation terminal operation retryable. After the submitted fence drains, retry
retires only the remaining handle and completes the export exactly once. A separate
pool-level proof checks the same partial-retirement invariant together with the exact
`2 + descriptorCount` reference baseline and escaped-owner rejection.

Actual device-loss cleanup now receives the best-known submitted-fence union. Dedicated
entries merge it with their own `safeAfter`; placed objects merge it with the parent heap
`safeAfter`. Canonical descriptors are retired when the backend still accepts retirement,
or cleared with device-loss leniency when it does not, before native object/heap owners
and byte charges are released. Healthy shutdown/abandonment continues to use RF2/RF3's
normal retirement path rather than treating ordinary failure as device loss.

Final x64 `geometryAllocatorTests` builds pass in Debug and Development with outer
MSBuild `/m:1`. The focused `--viewport-output-proof` passes in both configurations and
contains the RF4 fenced-detach and forced partial-retirement retry oracles. Full Debug
and Development `geometryAllocatorTests` runs also pass end-to-end on the exact-current
tree, including retained imports, explicit texture subresource state, terminal exports,
placed ownership and the downstream material/GPU-scene integration coverage. Final
static rereads close RF4 at **0 BLOCKER / 0 HIGH / 0 MEDIUM**.

RF4 made no performance claim. The old generation-owned Allocate/Write publication path
was gone because generations became borrowers, while the `FinalizeDescriptors` phase scan
was deliberately retained as a contract check for RF5 to remove or reduce after ownership
cutover was proven.

### RF5 closure evidence

RF5 was explicitly authorized on 2026-10-05. Exact-current inspection found that RF4 had
already removed normal generation-owned descriptor Allocate/Write work, leaving
`FinalizeDescriptors` as a pure O(physical-bindings) canonical-owner contract scan. RF5
deleted that phase and scan entirely and removed the two generation-publication-only
`PhysicalBindingRecord` flags (`wholeResourceDescriptorUse` and
`borrowedCanonicalDescriptors`). The logical-allocation `wholeResourceDescriptorUse`
driver remains because physical assignment still needs to know whether the persistent
owner must provide its canonical SRV/UAV bundle.

The ownership check was not weakened. Placed and newly acquired dedicated assignments now
validate the returned canonical handles immediately when whole-resource publication is
required. Same-frame dedicated reuse derives readiness from the physical resource usage
plus the actual SRV/UAV handles; if a physical resource was first used only through an
explicit view and later gains whole-resource demand in the same frame, the existing
bounded pressure/Ensure path upgrades that physical owner and validates the resulting
bundle in place. Retained imports remain externally owned, explicit/subresource semantics
remain unchanged, and RF4 export/failure/device-loss ordering is untouched.

Correctness validation is green on the RF5-owned surfaces. Serialized x64 Debug and
Development `geometryAllocatorTests` builds pass, the focused `--viewport-output-proof`
passes in both configurations, and both full geometry suites pass including same-frame
late canonical demand, retained imports, explicit texture subresources, placed resources,
terminal export retry, downstream material integration and GPU-scene coverage.
Development `renderingTests` also passes. `rhiNvrhiTests` reaches and passes its descriptor-
domain reclaim/state-lifetime stage, then reproduces the pre-existing later GPU Scene
runtime integration failure with an empty diagnostic; RF5 therefore does not report the
entire NVRHI executable as green. Development `rhiTests` separately builds with 0 warnings
and 0 errors and reports all tests passed.

Fresh Profile Runtime/Project Cardinal Tracy captures were taken for all four frozen RF5
workloads:

- normal quad: `build/profiling/rf5_normal_quad.tracy`;
- graph-only scheduler probe: `build/profiling/rf5_graph_only.tracy`;
- 64-resource RED-like mix: `build/profiling/rf5_resources64.tracy`; and
- 320-resource ceiling: `build/profiling/rf5_resources320.tracy`.

Tracy retained process history from startup, so steady-state measurements use the final
12 seconds of each trace; p50 uses the frozen lower-middle sample after sorting, without
interpolation.

| Workload | Warm Resolve p50 | Warm samples | `FinalizeDescriptors` occurrences |
| --- | ---: | ---: | ---: |
| normal quad | 153.598 us | 6,336 | 0 |
| graph-only scheduler probe | 140.333 us | 3,313 | 0 |
| 64-resource RED-like mix | 246.963 us | 2,732 | 0 |
| 320-resource ceiling | 853.268 us | 2,191 | 0 |

The canonical-descriptor event stream proves steady-state persistence rather than hidden
publication churn. In the warm final-12-second windows, both 64 and 320 workloads record
**0 Miss / 0 Allocate / 0 Write / 0 Retire** while `CanonicalDescriptor/Hit` remains active.
All observed misses/allocations/writes are startup work: the last 64-resource publication
event occurs at about 2.53 s and the last 320-resource publication event at about 5.17 s,
well before their warm windows.

For reference, the earlier RF2 matched captures measured 386.636 us Resolve p50 for the
64-resource workload and 1078.494 us for 320. The RF5 current tree measures 246.963 us
(36.13% lower) and 853.268 us (20.88% lower), respectively. Those total Resolve deltas are
reported as matched-current results, not attributed wholly to deleting the former
sub-microsecond `FinalizeDescriptors` scan because the shared checkout contains other
concurrent Resolve work. The RF5-specific causal statement is narrower: the standalone
`FinalizeDescriptors` phase is absent and warm canonical publication churn is zero.

The temporary Profile-only source gates used to isolate graph-only and normal-quad
captures were restored exactly afterward, and the final Profile Runtime was rebuilt from
the restored source with 0 warnings and 0 errors. RF5 therefore closes the descriptor-
ownership program at **0 BLOCKER / 0 HIGH / 0 MEDIUM** on its defined correctness and
steady-state performance boundary.

## Out of scope for RF0-RF5

- changing the public descriptor getter semantics based on declared use state;
- generic explicit-view/subresource descriptor caching;
- a new shader-visible heap/table-copy system modeled directly after RED;
- descriptor ownership embedded in generic RHI Texture/Buffer payloads;
- deferred locking backlog L1-L5;
- changing placed-resource default enablement solely to exercise this program.

## Authorization boundary

RF0 architecture closure did not authorize production implementation. RF1, RF2, RF3,
RF4 and RF5 were later explicitly authorized by the user and are now closed. RF5 was
explicitly authorized on 2026-10-05 and completes the frozen RF0-RF5 descriptor-ownership
program.
