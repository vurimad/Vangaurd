# RenderFlow post-#12 performance backlog

Status: **active audit backlog; implementation is separately authorization-gated.**

Date: 2026-10-04

This checkpoint records the remaining exact-current RenderFlow performance/scaling
opportunities found after the large post-#12 correctness and scaling closure. It is
not a claim that every item is a measured bottleneck. Structural findings are kept
separate from changes that require matched profiling or a stronger RHI/lifecycle
proof before implementation.

The standing rules are:

- re-read exact-current source before touching an item because the shared checkout
  changes frequently;
- do not recycle previously closed findings unless they have actually regressed;
- distinguish structural/asymptotic improvements from measured frame-time wins;
- preserve recording -> seal -> resolve -> execution/Finish, logical-vs-physical
  authority, generation-stamped scratch, dense identities, receipt-driven
  completion, dedicated/placed split, and Finish as the terminal native-owner
  release authority;
- do not weaken defensive validation merely to reduce bookkeeping;
- placed resources remain default-disabled until their scaling/locking shape is
  acceptable at production cardinalities.

## Recently closed HIGHs — do not reopen without regression evidence

The following fresh HIGHs were implemented and independently re-audited before
this backlog was written. `renderingTests` and `geometryAllocatorTests` rebuilt and
passed in Development x64 with outer MSBuild `/m:1`; the final source-static closure
audits reported 0 BLOCKER / 0 HIGH / 0 MEDIUM caused by these fixes.

1. **Active-use validation O(K^2) — CLOSED.** `UseBegin` no longer scans the full
   active-use chain for every simultaneous use. Buffer compatibility uses the live
   active head as the inductive same-queue/read/state witness; textures use an
   allocation queue witness plus per-subresource active counts while preserving the
   intrusive list for identity and non-LIFO `EndUse`.
2. **Placed provenance consume O(K^2) — CLOSED.** First-fit now returns exact
   provenance overlap cursors; consume detaches untouched prefix/suffix chains in
   O(1), walks only overlap pieces, and preserves exact predecessor coverage.
3. **Dedicated scalar retirement/shared-fence repetition — CLOSED.** Resolve failure
   bulk-rolls back dedicated entries. Finish reuses dead terminal scratch to batch
   reusable/quarantine entries through one pool lock and one shared `safeAfter`
   fence evaluation. Pending-retirement polling uses stable compaction.

## A — remaining exact-current performance candidates

These items originated from the follow-up static performance review and were then
checked against exact-current source. "Confirmed" means the structural shape exists
today; it does **not** mean a frame-time win has been measured.

### A1 — compact placed alignment summaries

**Status: CLOSED and Development/x64 test-validated; no timing claim without profiling.**

The fixed 64-level representation is gone. Planning first validates every request,
records the exact power-of-two alignment classes in one `u64` mask, freezes that mask
before heap/group construction, and maps each observed alignment to a dense slot.
Free-run nodes no longer embed `u64 maxFit[64]`; each planner heap owns a flat
`freeRunFits` table with one value per live free-run slot per observed alignment.
Compatibility-group fit trees use the same compact stride.

Summary update/search storage and work therefore scale with the number of alignment
classes actually present in the current plan instead of the full 64-bit exponent
domain. Scratch reset clears the logical mask/count and restrides reused backing for
the next frame. Focused coverage exercises `{16,64} -> {32} -> {16,64}` reuse and
checks exact first-fit/provenance plus compact summary sizes.

Primary source:

- `source/rendering/private/vanguard/rendering/render_flow_resource_placed.hpp`
- `source/rendering/src/render_flow_resource_placed.cpp`

### A2 — placed held-owner/progress scans

**Status: CLOSED and Development/x64 test-validated for the intended held-owner/
progress-scan shape; lock primitive and stats-lock work remain deferred.**

Held reusable heaps now retain both the exact object index that most recently proved
an external owner and a monotonic cursor to the first object not yet proven during
that held episode. Subsequent polls recheck the witness in O(1); when it clears, the
proof resumes after that object instead of rescanning the already-proven prefix. A
heap with N sequentially released external owners therefore performs O(N) ownership
visits across the held episode rather than O(N^2) prefix rescans. Reusable publication
has a proof-preserving fast path so Poll does not immediately rescan the list again.

Acquire and trim still run the full ownership classifier before consuming a published
reusable heap, and ClearPersistentCache fully verifies candidates before native
release. Those boundaries preserve defensive whole-heap validation while the held
progress path remains monotonic. Any unexpected non-Reusable object encountered by
the held cursor remains a blocker rather than being published early.

Routine full progress scans were removed from `Acquire` and `Retire`: BeginFrame is
the normal progress point and Retire is enqueue-only. If native-byte admission fails,
one Acquire transaction may perform at most one pressure Poll, re-run the exact
reusable-heap lookup/demotion logic, and retry reservation before returning
`BudgetExceeded`. This preserves opportunistic reclamation without paying the full
pending/held scan on every acquisition.

Focused coverage pins two different objects from one heap and proves witness handoff
does not publish early, and separately proves a hard-limit-one acquisition recovers a
completed retirement through the single pressure Poll/reuse retry. The final A-series
Development/x64 validation pass executed this coverage successfully.

### A3 — split placed reusable trim heaps by resource kind

**Status: CLOSED and Development/x64 test-validated; no timing claim without profiling.**

The global placed trim heap is now split into `reusableTextureTrimHeap` and
`reusableBufferTrimHeap`. Insert/remove/sift operations remain O(log N) with one
`trimPosition` because a heap belongs to exactly one resource kind. When only one
soft target is exceeded, trimming reads only that kind's root. When both are over
target, comparing the two kind-local roots with the existing `TrimLess` ordering is
equivalent to choosing the global oldest/tie-broken candidate.

ClearPersistentCache and DeviceLost cover both indices. Focused mixed-kind coverage
keeps an older texture heap below its target while buffers are over target and proves
only the buffer heap is retired; final cache cleanup still releases both kinds.

Primary source: `source/rendering/src/render_flow_resource_placed.cpp` trim path.

### A4 — eliminate physical-assignment discovery scan + first-use sort

**Status: CLOSED and Development/x64 test-validated; no timing claim without profiling.**

`CompileOperations` now appends each allocation id exactly once when `TouchLifetime`
establishes its first lifetime touch. The existing scratch vector keeps the reserved
export-slot map as a fixed prefix and stores the already ordered physical-assignment
stream as a suffix, so no second high-water array is introduced. Physical assignment
walks only that suffix; the former O(A) used-allocation discovery pass and O(U log U)
`std::sort` are removed.

Current first-touch producers are the executable use-begin, explicit scope-open, and
named-scope-open paths. Use-end/scope-close paths still call `TouchLifetime` but cannot
be the first valid touch because their matching begin/open endpoint must already
exist. Imports acquire order through the same first-touch paths; exports do not create
lifetime by themselves and still require an already-used allocation. The export-slot
prefix remains index-stable while assignment ids are appended, and `ResetFrame()`
clears the combined scratch after failed/cancelled sessions before the next Resolve.

Runtime lifetime completeness validation is folded into the existing placed-candidate
walk when placed resources are enabled, otherwise into the physical-assignment walk,
avoiding a replacement validation pre-pass.

Focused validation now also declares two compatible buffers in one order and first
touches them in the opposite order, then proves their physical ids follow first-touch
order rather than declaration order.

Primary source: `source/rendering/src/render_flow_resource_resolve.cpp`.

### A5 — stop rescanning historical writer-reservation tails

**Status: CLOSED and Development/x64 test-validated; no timing claim without profiling.**

`PreparePlanningGroups` now resizes `writerReservations` and `queueRequestGroups`
directly to the current graph breadth. RED `DynArray::Resize` destroys the inactive
tail but deliberately retains the outer buffer capacity, so a large historical graph
releases its nested per-writer recording backing once when the graph contracts while
future preparations no longer rescan that dormant tail. Regrowth within the retained
outer capacity reconstructs only the newly active entries without reallocating the
outer reservation/request storage.

`writerImpls` remains allocated once at `maximumPlanningWriters` and is never resized,
preserving the allocator-lifetime pointer stability required by stale writer handles.
Those handles may retain an obsolete reservation pointer, but exact-current writer
validation/abandonment checks session generation and allocator phase before any
reservation dereference, so reservation storage can safely contract between sessions.

Normal render-node production still calls `PreparePlanningGroups(realNodeCount)`.
The direct writer/queue-request fallback paths intentionally retain their existing
`maximumPlanningWriters` preparation when no explicit preparation occurred; A5 changes
only repeated historical-tail work, not that API fallback contract.

Focused validation exercises the real `RenderNodeGraph::PrepareResourcesParallel`
caller across an eight-node frame, a one-node frame, and an eight-node frame again on
one allocator. That directly covers contraction/regrowth of reservation/request
storage while the existing stale-writer proof separately preserves writer-control-block
lifetime semantics.

Primary source: `source/rendering/src/render_flow_resource_allocator.cpp`.

### A6 — reduce repeated dedicated descriptor canonicalization/copying

**Status: CLOSED and Development/x64 test-validated; no timing claim without profiling.**

Independent exact-current READ-ONLY closure after the lean ownership/copy tightening:
**BLOCKER 0 / HIGH 0 / MEDIUM 0.** The final validation pass subsequently built and
executed the focused rendering/native allocator suites successfully.

Dedicated assignment now derives one `DedicatedResourceRequest` at the authoritative
logical-to-physical boundary. It contains the single canonical physical descriptor
needed for actual creation/publication, plus a compact immutable compatibility class
and separate buffer capacity. Same-frame reuse and the cross-frame dedicated pool
consume the compact class/capacity rather than repeatedly materializing canonical
`FrameResourceDesc` copies and re-hashing/re-comparing them.

Texture compatibility remains exact over active extent, dimension, format, mip/array
counts, sample count, usage, initial state, virtual-resource flag and initial-state
policy. Buffer compatibility remains structure stride, format, usage, initial state,
memory type, virtual-resource flag and initial-state policy; canonical `maximumSize`
is carried separately as reusable capacity. Hash collisions still fall through an
exact compact-class comparison before reuse.

`WholeFrameReuseClass` owns one compact class per same-frame reuse class; individual
`WholeFrameLane` records carry only capacity plus that class index. Likewise the
dedicated pool owns one compact class per compatibility bucket; individual pool entries
carry capacity plus their bucket index rather than duplicating the class. Pool entries
still keep their authoritative full physical descriptor for native creation,
provider verification and export semantics, but `DedicatedResourceAssignment` no
longer copies that full descriptor back out on every hit/miss. Resolve reuses the
already-canonical request for the narrow physical-state metadata it publishes; buffer
capacity is carried separately because size is not part of that state metadata. This
is deliberately not broad native-descriptor caching and does not alter placed-resource
compatibility.

Focused validation explicitly proves that two buffers with identical compact class
fields but different canonical `maximumSize` remain one compatibility class, that the
larger capacity satisfies the smaller request but not vice versa, and that a forced
equal hash with a differing semantic field is rejected by exact class comparison.

### A7 — zero-export Finish fast path

**Status: CLOSED and Development/x64 test-validated; no timing claim without profiling.**

Independent exact-current READ-ONLY closure: **BLOCKER 0 / HIGH 0 / MEDIUM 0.**

`Finish` now snapshots whether the published generation has any pending exports after
receipt/dependency indexing. Successful zero-export completion still performs all
terminal packet/state-order validation, descriptor-fence accumulation, dedicated and
placed retirement, runtime-owner release, generation completion and statistics, but
skips terminal publication/transfer preparation entirely.

The export-only block now runs only for `Completed && hasPendingExports`, avoiding the
temporary publication/transfer arrays, physical-to-export scratch resize, dedicated
`TransferOwnership` call and publication lock when no export can exist. Retirement
also requires `hasPendingExports` before interpreting the reused scratch vector as a
physical-to-export map, so its earlier queue-dependency contents cannot suppress
normal dedicated retirement on the zero-export path.

Focused validation extends the Graphics/Compute fork-join zero-export frame with a
dedicated transient binding while dependency receipts remain non-empty. After Finish,
the test clears persistent caches and requires native charged bytes to reach zero,
directly proving stale dependency-receipt scratch cannot suppress dedicated retirement.

This remains a micro-optimization rather than an asymptotic redesign.

### A1-A7 validation pass

Final Development/x64 validation completed after all A-series changes and focused
regression proofs were present:

- `renderingTests.vcxproj` built successfully with `/m:1`; the final incremental build
  completed without compile errors. The earlier cold build emitted nine pre-existing
  `LNK4006` duplicate-symbol warnings from the rendering library but still completed
  with zero errors.
- `geometryAllocatorTests.vcxproj` built successfully with `/m:1`, zero errors.
- `renderingTests.exe` exited 0: Render Graph, Resource Flow Allocator, texture upload
  candidate, and rendering viewport suites all passed.
- `geometryAllocatorTests.exe` exited 0, including dedicated provider/reuse,
  placed-provider/provenance/trim, descriptor publication, retained import, terminal
  export, Graphics/Compute fork-join, and the added A6/A7 focused proofs.
- Scoped `git diff --check` across the A-series source/test surface reports no whitespace
  errors; only the repository's LF-to-CRLF conversion warnings remain.

No Runtime, Editor, or Tracy executable was launched, and no performance timing claim
is made by this correctness/regression validation pass.

## L — locking and synchronization findings

Fresh exact-current locking audit result: **0 BLOCKER / 2 HIGH / 3 MEDIUM.** Two
independent source reviews found no live lock-order inversion or reentrant deadlock.
Observed ordering is consistently `writerLock -> pool lock -> native-byte-ledger
lock` or `pool lock -> native-byte-ledger lock`; no `ledger -> pool` or `pool ->
writerLock` reverse edge was found. Dedicated and placed pool locks are not held
together, and the inspected RHI calls do not callback into RenderFlow pool code.

### L1 — HIGH: placed Acquire holds a SpinLock across the full native transaction

`PlacedResourcePool::Acquire` takes `Impl::lock` around
`render_flow_resource_placed.cpp:1524` and holds it through the full batch transaction
to roughly `:1766`. While locked it can perform:

- nested native-byte-ledger reservation/release;
- `rhi::CreateHeap`;
- `rhi::CreateTexture` / `rhi::CreateBuffer`;
- authoritative descriptor and memory-requirement queries;
- `rhi::BindMemory`;
- `rhi::GetPlacement`;
- metadata commit/rollback for every heap/object in the batch.

The underlying Vanguard `SpinLock` delegates to RED's spin lock: it busy-spins for
the first iterations and then repeatedly yields. Driver/native work is not a bounded
metadata critical section, so a concurrent Poll/Retire/GetStats caller can spin/yield
behind N heap/object creations and binds.

**Smallest safe containment:** replace this pool serialization primitive with a
sleepable/yielding mutex while preserving the current transaction boundary.

**Deeper optimization, only if profiling warrants it:** two-phase misses — reserve
transaction identity/metadata under lock, unlock for native create/query/bind, then
relock to commit or rollback. Do not attempt that without explicit reservation tokens
and failure/concurrency proofs.

### L2 — CLOSED scan portion via A2; lock-duration work remains under L1/L3

A2 made BeginFrame the normal full-progress point, made Retire enqueue-only, removed
Acquire's unconditional Poll, and retained one bounded pressure Poll/retry when hard
native-byte admission fails. The placed lock itself is unchanged and `GetStats`,
Trim/Clear and explicit Poll still retain their current synchronization behavior.

### L3 — MEDIUM: allocator/pool GetStats is side-effecting maintenance

`RenderFlowResourceAllocator::GetStats` takes `writerLock`, snapshots allocator
stats, then while still holding `writerLock` calls dedicated-pool `GetStats`,
placed-pool `GetStats`, and ledger `GetStats`.

Both pool `GetStats` implementations take their pool lock and call `PollUnlocked`.
Therefore an observational diagnostics call can query fences, inspect refcounts,
create/check native release observations, release native charges, mutate pool state,
and perform O(pending) work while widening `writerLock` over nested pool/backend work.

**Smallest correction:** make pool `GetStats` cached-snapshot-only. Keep progress
explicit at BeginFrame/Trim/Clear/lifecycle points. In allocator `GetStats`, snapshot
allocator/export fields under `writerLock`, release it, then read pool/ledger snapshots
under their own short synchronization.

Diagnostics should not perturb the timing or ownership state being observed.

### L4 — MEDIUM: dedicated pool lock/ledger locks cover externally serialized coordinator work

`DedicatedResourcePool::Acquire` holds its pool `SpinLock` across requirement
queries, held/refcount checks, native release observation, nested ledger queries,
`CreateTexture/CreateBuffer`, authoritative descriptor queries, memory-requirement
queries and pool commit. Other dedicated Poll/Trim/Clear/TransferOwnership paths also
perform backend/refcount work while locked.

The public allocator contract states coordinator lifetime operations are externally
serialized. That makes some of this internal locking potentially redundant on the
actual hot owner path.

**Preferred design decision:** first prove the exact caller/threading contract. If
pool/ledger mutation is coordinator-owned, remove redundant pool/ledger locks and use
debug owner-thread assertions; expose independently safe cached/atomic diagnostics if
needed. If real concurrent mutation must remain supported, use a sleepable mutex or a
two-phase native miss transaction rather than holding a spinlock over backend work.

Do not blindly replace every lock until the ownership contract is proven.

### L5 — CLOSED ownership-proof portion via A2

A2 now carries one ownership classification/blocker through held promotion, acquire
demotion and trim. The publish-after-proof path does not repeat the refcount walk.
This does not change the placed-pool lock primitive; the remaining lock-duration
finding is L1.

## Recommended implementation order

This is an ordering recommendation, not blanket authorization.

1. **L1 containment:** use an appropriate sleepable/yielding lock for placed native
   transactions before attempting a larger two-phase redesign.
2. **L3:** make stats observational and flatten the `writerLock -> pool/backend`
   diagnostic path.
3. **A4:** record first-use physical assignment order directly and delete the
   discovery scan/sort after focused invariant proof.
4. **A5:** collapse historical writer-reservation tail work.
5. **Profile exact-current behavior.** Use the results to decide whether A6 and
   the deeper L4/two-phase pool redesign are worth the additional representation
   complexity.
6. **A7:** take opportunistically with nearby Finish work unless measurements show
   terminal-export setup is material.

## Measurement guidance

Before claiming performance wins, collect matched evidence for the paths changed.
Useful counters/zones include:

- placed `PollUnlocked`: held heaps visited, object refcounts queried, pending
  retirement/quarantine/destruction entries visited, calls per frame;
- placed `Acquire`: lock hold time, heap misses, object misses, native Create/Bind
  calls, hard-budget retry count;
- dedicated `Acquire`: lock hold time, native creation/query calls, held promotion;
- allocator/pool `GetStats`: call count and time, especially when diagnostics logging
  is enabled;
- physical assignment order: allocation scan count, used allocations, sort size/time;
- placed alignment summaries: distinct alignment levels and summary update time.

No FPS, microsecond or percentage improvement should be recorded here without
matched captures.
