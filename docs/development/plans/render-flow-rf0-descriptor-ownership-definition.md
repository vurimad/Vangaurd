# RenderFlow RF0 — persistent descriptor ownership definition

Status: **RF0 Definition CLOSED — 0 BLOCKER / 0 HIGH / 0 MEDIUM. RF1 production implementation is not authorized by this document.**

Date: 2026-10-04

Program: [`render-flow-rf-descriptor-ownership-plan.md`](render-flow-rf-descriptor-ownership-plan.md)

## 1. RF0 decision

Whole-resource SRV/UAV descriptor identity follows **persistent physical resource
identity**, not execution-generation identity.

For allocator-owned physical resources:

- `DedicatedResourcePool::Impl::Entry` is the descriptor owner for a dedicated
  texture/buffer;
- `PlacedResourcePool::Impl::ObjectEntry` is the descriptor owner for a placed
  texture/buffer;
- execution generations and resolved uses borrow immutable handles only;
- pool reuse preserves descriptor handles while the physical object remains the same;
- native descriptor publication happens on first whole-resource demand, not once per
  frame; and
- descriptor retirement happens when the persistent descriptor owner gives up the
  descriptor, not merely because one frame finished.

The normal warm path is therefore:

```text
physical pool hit -> borrow cached handles -> execute
```

not:

```text
physical pool hit -> Allocate -> Write/Create*View -> execute -> fence-retire
```

## 2. Private canonical bundle

RF2/RF3 introduce one private representation shared by the two physical pools:

```cpp
struct CanonicalResourceDescriptors
{
    rhi::DescriptorHandle shaderResource;
    rhi::DescriptorHandle unorderedAccess;
    u64 lastDemandFrame = 0;
};
```

The helper contract is private RenderFlow implementation, colocated with the physical
pool contract (`render_flow_resource_pool.hpp/.cpp`) so placed code can reuse it without
adding a public API.

Required operations are conceptually:

- `CanonicalDescriptorReferenceCount(bundle)` — number of populated valid handles;
- `EnsureCanonicalResourceDescriptors(...)` — lazily install the complete
  physically-supported whole-resource SRV/UAV set;
- `RetireCanonicalResourceDescriptors(...)` — retire valid handles against an exact
  fence set, clearing each local handle immediately after that handle's retirement is
  admitted; and
- `ClearCanonicalResourceDescriptorsOnDeviceLoss(...)` — best-effort retirement/
  local invalidation for terminal device-loss teardown.

`Ensure` is transactional for the handles created by that call. A failure retires any
newly allocated/published handles with zero fences and leaves the previously installed
bundle unchanged. No half-installed new bundle may escape through an assignment.

`lastDemandFrame` is updated whenever a whole-resource logical allocation successfully
borrows this bundle, whether the handles were just created or were a warm hit. Explicit-
view-only physical use does not refresh descriptor demand age.

Retirement cannot be rolled back after the RHI accepts a handle. If retiring a two-
handle bundle admits the first handle and the second fails, the admitted handle stays
cleared, the unretired handle stays owned, and the physical resource owner remains
intact. The caller propagates failure; a later `Ensure` may republish the missing
canonical handle once the owner returns to a legal reusable state. RF never resets the
native resource merely to make such a partial retirement look atomic.

The descriptor shape remains exactly today's `FinalizeDescriptors` shape:

- textures: whole-resource SRV and/or UAV according to immutable physical usage;
- buffers: Structured, Raw or Typed SRV/UAV according to immutable physical usage;
- default whole-resource view; dedicated buffer capacity remains the physical buffer
  size; and
- no descriptor is created for a capability the physical resource does not expose.

This does **not** narrow descriptors according to one declared use state. A whole-
resource use continues to expose every generic SRV/UAV descriptor supported by that
physical resource, matching the current public `Resolved*Use` contract and tests.

## 3. Demand discovery and propagation

Descriptor demand is compiled before physical assignment.

`LogicalAllocationRecord` gains a boolean whole-resource descriptor-demand fact. It is
set when compilation observes any non-explicit-view texture/buffer use of that logical
allocation. Explicit-view-only uses do not set it.

Physical assignment consumes that fact:

- dedicated `Acquire` ensures the canonical bundle only when the logical allocation
  needs whole-resource descriptors;
- `DedicatedResourceAssignment` returns borrowed descriptor handles with the native
  resource refs;
- `PlacedObjectRequest` carries the demand bit and placed `Acquire` ensures the bundle
  on the exact `ObjectEntry` only when required;
- `PlacedResourceAssignment` returns borrowed descriptor handles; and
- `PhysicalBindingRecord::shaderResource/unorderedAccess` remain the execution-facing
  handles but become explicitly **borrowed**, not generation-owned.

Same-frame dedicated reuse is an explicit edge case: a lane may first be created for
an explicit-view-only logical allocation and later be reused by a whole-resource
allocation. The reuse path must call the dedicated pool's `Ensure` for the lane's
`poolEntry` before publishing the later logical binding, then update the shared
`PhysicalBindingRecord` handles. Reuse may never assume the first logical allocation's
demand describes all later aliases.

## 4. Descriptor-domain lifetime

Persistent descriptors require a persistent domain owner.

`RenderFlowResourceAllocator::Impl` owns one strong `rhi::DescriptorDomain` copied from
`config.resourceDescriptors`. It is declared before the dedicated/placed pool members
so C++ reverse member destruction destroys both pools before releasing the domain.

The pools retain only the non-owning domain ref required to publish/retire their
bundles. An invalid configured domain preserves today's behavior: physical resources
can exist, but no generic whole-resource handles are published.

Execution generations stop being descriptor-domain owners once RF4 cuts over the
ownership model. `ExecutionGenerationRef::Impl::{resourceDescriptors,
descriptorRetirement,descriptorDeviceLost}` and the normal
`RetireGenerationDescriptors` path are then removed. Future explicit/subresource
descriptor support must use an explicit generation-owned list rather than silently
reusing canonical pool handles.

## 5. Exact ownership/refcount invariant

A populated RHI descriptor slot owns one strong RHI reference to its resource. RF does
not hide or weaken that fact.

For a pool-owned physical object:

```text
poolOwnedReferenceBaseline = 1 + CanonicalDescriptorReferenceCount(bundle)
```

The `1` is the pool's owning `rhi::Texture` or `rhi::Buffer`. Any reference above the
baseline is still an escaped/external owner and blocks reuse/trim exactly as today.

The exact-current ownership sites that must change atomically with RF2/RF3 are:

- dedicated `PoolOwnsOnlyReference`;
- dedicated reusable lookup/promotion and trim/cache-clear callers of that helper;
- placed `ObjectBlocksPoolOwnership`;
- placed `InspectHeapOwnership` / `PoolOwnsAllHeapObjects`;
- placed reusable-object lookup's literal `GetRefCount(...) == 1`; and
- dedicated `TransferOwnership` export validation.

No caller may independently invent a different descriptor baseline.

## 6. State/lifetime contract

Canonical handles survive the ordinary physical-cache lifecycle:

```text
Assigned -> PendingRetirement -> Reusable -> Assigned
```

They are **not** retired by normal frame `Finish`, normal pool retirement, or Resolve
rollback.

Descriptor retirement occurs only for:

1. descriptor-only cache-pressure eviction from a `Reusable` physical object;
2. native resource trim/cache clear/quarantine/destruction;
3. terminal export leaving dedicated-pool ownership; or
4. device-loss/allocator terminal teardown.

Ordering rules:

- Reusable descriptor-only eviction retires with empty fences; the physical object
  remains cached and valid.
- Reusable native-resource trim retires the bundle with empty fences **before** native
  release observation/resource reset.
- Pending-retirement cache clear/quarantine retires the bundle against that physical
  owner's `safeAfter` fences before resource reset; descriptor-held resource ownership
  then remains until those fences complete.
- Placed heap destruction retires every contained object's bundle before the object
  refs and heap owner are released. The heap's `safeAfter` supplies the retirement
  fences for pending objects.
- Resolve rollback keeps successfully installed canonical bundles on reusable physical
  entries. A failed `Ensure` itself leaves no newly created descriptor behind.

## 7. Terminal export contract

Terminal exports remain dedicated-only and `Finish` remains the terminal ownership
authority.

For an exported entry with `N` canonical descriptor handles, `Finish` first creates the
publication owner as today. `DedicatedResourcePool::TransferOwnership` then requires:

```text
exact reference count = 2 + N
```

where the owners are pool + publication + N descriptor-held references. The pool
derives `N` from its entry; `DedicatedResourceTransfer::descriptorReferences` becomes
obsolete after RF4.

`TransferOwnership` first validates **all** transfer entries and exact reference counts
before mutating any pool resource owner. It then retires/detaches canonical handles
against a conservative union of every submitted command-scope fence in the completed
execution receipt. This deliberately preserves the current generation-wide descriptor
safety envelope rather than attempting a new per-use fence proof during RF.

RF4 therefore computes that submitted-fence union **before** the export-transfer block;
exact-current code accumulates `descriptorRetirement` later only because generation-
owned descriptors are retired during teardown. Moving the accumulation earlier is an
ordering change, not a broader fence contract.

Only after descriptor retirement has been admitted for every transfer entry does a
second commit pass release the pool resource owners/entry slots. `PublishedResourceExport`
does not gain descriptor ownership. If a later persistent owner needs descriptors for
that exported resource, it publishes its own bundle under its own lifetime contract.

If descriptor retirement admission fails during a normal completed export, ownership
transfer fails before **any** pool resource owner is relinquished and no publication is
committed. Handles whose retirement was already admitted remain cleared; remaining
handles stay on their entries. Those entries remain `Assigned` and therefore cannot be
reused; terminal failure/abandon cleanup owns the recovery. Device-loss follows the
terminal rules below instead of pretending normal transfer succeeded.

## 8. Descriptor-cache pressure contract

Persistent descriptors must not consume the shared global domain without a RenderFlow
bound.

RF adds two explicit RenderFlow-local slot budgets:

```cpp
u32 targetCachedWholeResourceDescriptors = 8'192;
u32 maximumCachedWholeResourceDescriptors = 16'384;
```

The target counts canonical descriptor slots retained on fully `Reusable` physical
objects. The maximum counts every live pool-owned canonical descriptor slot across
`Assigned`, `PendingRetirement` and `Reusable` objects. With the current 262,144-slot
engine resource domain these defaults are 3.125% and 6.25% respectively; the existing
material descriptor cache independently defaults to 16,384 entries, so RF does not
silently assume ownership of the shared domain.

When `resourceDescriptors` is valid, initialization rejects target > maximum or
maximum > the domain capacity. Composition that intentionally supplies a different
domain size must explicitly override the RF budgets instead of relying on an implicit
percentage/clamp. When the domain is invalid, canonical publication remains disabled
as today.

One allocator-level private `AllocatorDescriptorCacheLedger` is the authority for both
physical pools. It tracks at least:

- live canonical descriptor slots across dedicated + placed;
- descriptor slots on currently evictable reusable owners;
- current/peak cached slots;
- canonical publication hits/misses; and
- pressure/maintenance evictions and rejected reservations.

Pools reserve/release slot count through this shared ledger as handles are installed,
retired, detached or change reuse eligibility. There is **not** one independent budget
per pool. The ledger does not reserve global RHI slots exclusively; other descriptor
users remain free to consume the shared domain.

There is no unconditional per-frame descriptor-cache scan. Candidate maintenance is
incremental. After the existing pool `Poll` calls at `BeginFrame`, an O(1) cached-count
check does nothing while the reusable descriptor count is at/below target. Only when
above target **and `FrameResourcePolicy::processEviction` is enabled** does descriptor-
only maintenance pop cold candidates until the reusable count reaches target.

When `processEviction` is disabled, proactive soft-target trimming is skipped just like
native cache eviction. The hard live-cache maximum and actual shared-domain capacity
remain mandatory admission constraints: an `Ensure` may still perform its bounded
pressure eviction/retry because otherwise the requested descriptor set cannot be
published safely.

Only fully `Reusable`, pool-owned physical objects with valid canonical handles are
descriptor-eviction candidates. `Assigned`, `PendingRetirement`, quarantined,
pending-native-destruction, externally held, retained-import and published-export
resources are never descriptor-cache eviction candidates.

Eviction order is global by canonical descriptor **last-demand frame**, oldest first,
with deterministic owner-class then entry/object-index tie breaking. Descriptor demand
age is separate from native-resource `lastUsedFrame`: using the physical object without
requesting a generic descriptor does not make an old descriptor bundle hot.

On a canonical `Ensure` that would exceed the RenderFlow-local hard maximum, or whose
first RHI allocation fails specifically from descriptor-domain capacity pressure:

1. for local-cap pressure, evict cold bundles toward target until enough local headroom
   exists; if no eligible candidates can create the required headroom, fail before
   touching RHI;
2. retire evicted handles with zero fences without evicting their native resources;
3. attempt the complete transactional `Ensure`;
4. if that attempt fails specifically because the shared descriptor domain has no
   capacity, rollback every handle newly created by that attempt, perform one bounded
   global-pressure eviction pass (which may go below the soft target), and retry the
   complete `Ensure` exactly once; and
5. if the retry fails, propagate capacity failure. Write/backend/device-loss/invalid-
   view failures are not pressure-retried.

There are at most two RHI publication attempts for one `Ensure`; there is no spin,
unbounded retry, full-domain search or frame-wide candidate scan.

RF3 may implement the cold index with pool-local heaps and compare the two roots at
maintenance/pressure time; the contract requires global-oldest behavior without a
frame-wide scan, not one particular container.

No pressure path may hold one physical pool lock while acquiring the other pool lock.
The allocator/coordinator queries the current cold root from each pool in separate
short calls, chooses the older root, then asks only that pool to evict/revalidate it.
Canonical `Ensure` may reserve from the shared descriptor ledger while holding its own
pool lock, but if it needs cross-pool pressure relief it returns an internal pressure
result, releases the pool lock, and lets the allocator perform the bounded global
pressure pass before the one allowed retry. This preserves the existing pool -> ledger
lock direction and introduces no dedicated <-> placed lock edge.

Fenced descriptor handles detached by terminal export are not counted as live
pool-owned cache slots after detach even though they may temporarily remain pending in
the shared RHI domain. Global domain capacity remains authoritative for that backlog;
RF must not falsify its own cache occupancy to hide it.

## 9. Normal allocator shutdown contract

Persistent descriptors make healthy allocator shutdown a different operation from
device-loss teardown. `RenderFlowResourceAllocator::Shutdown()` must not route a healthy
`Idle` allocator through the pool `DeviceLost()` fast-clear path.

Healthy normal `Shutdown()` is a **two-phase, retryable terminal drain**:

1. Validate the existing public precondition: allocator state is `Idle` **or** the
   allocator-terminal `DeviceUnavailable`, no published execution generation exists,
   and no recording/execution session is active. `DeviceUnavailable` is not itself GPU
   loss proof. Keep `m_impl`, published exports, both pools, and the allocator-owned
   strong descriptor domain intact throughout preparation.
2. Preflight the RHI before mutating descriptors. If the backend is already
   uninitialized, return failure with ownership untouched. If the backend is alive and
   `rhi::TestDeviceState()` returns `Removed` **or** `ResetRequired`, select §10
   immediately. Otherwise continue the healthy path; `Unknown` is not device-loss proof
   and is resolved by the concrete retirement/flush results below.
3. Each pool runs a descriptor-shutdown preparation pass without releasing any native
   resource owner or pool slot. Canonical bundles on GPU-complete reusable/quarantine
   owners retire with empty fences; bundles on pending-retirement owners retire against
   that entry/heap `safeAfter`. A pending-native-destruction owner must already have no
   canonical bundle. An impossible `Assigned` owner in an otherwise `Idle` allocator is
   a backend-contract violation.
4. Retirement remains per-handle irreversible. If preparation fails after one handle
   was admitted, that handle stays cleared, every unretired handle/resource owner stays
   owned, and `Shutdown()` returns `false` with the allocator still initialized and
   retryable. A later normal frame may lazily republish any missing canonical handle;
   a later `Shutdown()` continues from the remaining handles.
5. Only after **both** pools have admitted retirement for every canonical handle does
   `Shutdown()` call `rhi::FlushRetiredResources()` while the allocator still owns the
   descriptor domain and all pool owners. This existing RHI operation waits for GPU
   idle and drains descriptor/resource retirements, so the final strong domain owner
   cannot disappear while one of its descriptor slots still protects an in-flight
   resource.
6. If the flush succeeds, terminal commit begins: clear allocator-owned published
   export records, release pool release-observation tickets/native owners/ledger state,
   destroy both pools, then release the allocator's strong descriptor-domain owner and
   allocator metadata. No failure is permitted after this commit point.
7. If the flush fails for a non-device-loss/backend-retryable reason, `Shutdown()`
   returns `false` and preserves the allocator/pools/domain for retry. Descriptors whose
   retirement was already admitted remain retired; native resource owners remain
   intact.
8. If the flush reports concrete `rhi::FailureCode::DeviceLost`, or
   `rhi::TestDeviceState()` establishes `Removed` or `ResetRequired` **while the RHI
   backend is still initialized**, normal retirement guarantees are no longer
   achievable. `Shutdown()` switches to the explicit device-loss terminal path in §10
   and completes local teardown under that contract. `Unknown` never grants this
   leniency by itself.
9. If the RHI backend becomes shut down behind the allocator during this procedure while
   the allocator still owns RHI
   references, teardown ordering has already been violated. RHI `Ref::Reset()`/
   `SafeRelease()` explicitly asserts in that state. `Shutdown()` therefore returns
   failure without touching pool/domain owners; it never pretends post-RHI teardown is
   a device-loss cleanup opportunity.

The global wait/drain is intentional and restricted to explicit allocator shutdown; it
never appears in `BeginFrame`, Resolve, pool pressure maintenance, or normal `Finish`.

### Destructor policy

`~RenderFlowResourceAllocator()` is non-returning and therefore cannot silently turn a
healthy teardown failure into device-loss leniency.

- If no active session exists and the RHI is healthy, the destructor attempts the same
  graceful terminal drain as `Shutdown()`.
- If an execution must first be abandoned on a healthy device, abandonment gathers the
  best-known submitted fences, converts every still-assigned physical owner into its
  normal pending-retirement state using that conservative fence set, invalidates
  generation/runtime borrows, then the destructor performs the same RHI idle/drain
  before releasing pools/domain. Canonical handles remain attached until shutdown
  preparation retires them against those `safeAfter` fences. Healthy abandonment does
  **not** by itself authorize `DeviceLost()` semantics.
- If healthy abandonment cannot establish the submitted-fence set, it must not invent
  a never-completing fence and then enter a blocking graceful drain. Ownership remains
  intact and terminal cleanup follows the same fatal/strand safety rule unless a later
  RHI query proves actual device removal.
- If the initialized RHI reports `Removed` or `ResetRequired`, or a concrete terminal
  operation reports `rhi::FailureCode::DeviceLost`, the destructor uses §10 device-loss
  cleanup.
- If the RHI backend is already uninitialized, the destructor cannot legally release
  any surviving RHI owner. It emits the fatal/terminal ownership-order diagnostic and,
  if execution continues, strands the allocator `Impl` exactly as for an unresolved
  healthy drain failure.
- If graceful retirement/drain fails while the device still reports healthy, safety
  wins over cleanup: emit the existing fatal/terminal diagnostic and, if execution is
  allowed to continue by that build policy, intentionally strand the allocator `Impl`
  (including pools and its strong descriptor-domain owner) rather than release a domain
  or native owner whose GPU safety was not proved. This is an emergency misuse/failure
  path, never a normal ownership mechanism.

The production engine remains expected to call and check
`FrameRenderer::ShutdownResourceAllocator()`; the destructor is the last safety net,
not a substitute for successful explicit shutdown.

### `DeviceUnavailable` state is not device-loss proof

Exact-current abandonment also uses `RenderFlowResourceSessionState::DeviceUnavailable`
as an allocator-terminal state. RF preserves that public state value if convenient, but
it does **not** use the enum alone to select device-loss leniency. A healthy
`AbandonPublishedExecution()` may leave the allocator terminal/`DeviceUnavailable` with
pool entries conservatively pending on submitted fences; a later `Shutdown()` still
performs the healthy two-phase drain above. Only an RHI device-loss failure or
`rhi::TestDeviceState()` proving `Removed`/`ResetRequired`, or a concrete RHI
`FailureCode::DeviceLost`, selects §10. `Unknown` does not.

## 10. Device-loss and abnormal failure contract

The allocator-owned strong descriptor domain outlives both physical pools.

For an **actual unavailable-device path while the RHI backend still exists**, proven
by `DeviceState::Removed`, `DeviceState::ResetRequired`, or a concrete RHI
`FailureCode::DeviceLost` from the operation being performed:

1. collect the best-known submitted graphics/compute/copy fence set exactly as the
   existing generation descriptor teardown does;
2. pools retire/clear every canonical bundle against the union of that submitted set
   and any entry/heap `safeAfter` fence;
3. descriptor-retirement failure is tolerated only on this actual device-loss path;
4. local handles are invalidated before pool metadata is discarded;
5. native resource owners/release observations/ledger charges are then cleared through
   the existing device-loss path; and
6. only after both pools are gone may the allocator release its strong descriptor
   domain.

If submitted-fence discovery fails during actual device loss, use the same conservative
never-complete fence sentinel policy as the current generation descriptor teardown;
backend emergency/device-idle destruction remains the final terminal recovery path.

The never-complete sentinel is **device-loss-only**. Healthy abandonment/shutdown never
feeds that sentinel into `FlushRetiredResources()` because doing so could turn teardown
into an unfinishable wait.

Normal Resolve/Finish failures, healthy execution abandonment, normal `Shutdown()`, the
healthy-device destructor path, and post-RHI ownership-order violations do not use
device-loss leniency.

## 11. Retained imports, explicit views and aliasing

- Retained imports remain skipped by canonical pool publication. Their persistent
  owner remains responsible for descriptor authority.
- Explicit-view-only uses continue to return invalid generic whole-resource handles.
  RF does not introduce explicit/subresource descriptor caching.
- Placed aliasing does not share descriptor identity merely because resources share a
  heap range. The canonical bundle follows the persistent placed `ObjectEntry`/native
  resource identity. Distinct aliased objects therefore own distinct bundles.
- Dynamic/upload resources whose descriptor depends on a changing GPU address are not
  generalized into this cache. If such a RenderFlow resource class appears, it must
  declare a separate republish/repin contract.

## 12. Validation matrix

### RF1 — RHI reclaim hygiene

- Preserve concurrent unique descriptor allocation.
- Preserve immediate `{}` retirement: completed/free immediately, generation bumps,
  stale handles rejected.
- Preserve fenced retirement: no early reuse before the fence; maintenance/pressure
  reclaim makes it reusable after completion.
- Small-domain exhaustion proves allocation performs one pressure reclaim before
  returning capacity failure.
- Matched 320-resource Profile capture establishes the post-reclaim performance floor.

Primary suite: `source/rhi/nvrhi/tests/d3d12_backend_tests.cpp` descriptor-domain
coverage plus existing façade retirement coverage in `source/rhi/tests/rhi_tests.cpp`.

### RF2 — dedicated persistent descriptors

- Two warm frames reuse identical descriptor `GpuIndex()` values for the same dedicated
  physical resource.
- Descriptor-domain allocated/populated count does not return to zero at normal Finish
  and does not increase on the warm pool hit.
- Whole-resource UAV use still exposes both SRV/UAV where physically supported.
- Explicit-view-only resource consumes zero canonical descriptors.
- Explicit-view-only first occupant followed by same-frame whole-resource lane reuse
  lazily installs descriptors before the later use.
- In that same-frame late-demand case, the earlier explicit-view use still returns
  invalid generic descriptor getters even though its shared physical binding record now
  contains canonical handles; the existing per-step `hasExplicitView` gate remains the
  public semantic authority.
- Escaped resource owner above `1 + descriptorCount` still blocks pool reuse/trim.
- Resolve failure after successful installation leaves a reusable cached bundle;
  failed installation leaks no handles.
- Cache clear retires all bundles and returns descriptor-domain counts to baseline.
- RF2 installs the allocator-level 8,192/16,384 target/cap ledger before persistent
  dedicated caching can ship; tiny dedicated-only budgets prove cold reusable eviction,
  hard-cap failure, and the single capacity-pressure retry.
- RF2 also lands the allocator/domain healthy terminal-lifetime cutover for dedicated
  canonical owners: healthy `Shutdown()`, healthy execution abandonment, and the
  healthy-device destructor path must no longer route persistent dedicated descriptors
  through pool `DeviceLost()` cleanup. RF3 extends the same contract to placed
  objects/heaps before placed persistent descriptors can ship.
- Strong-domain lifetime proof: initialize from a caller-owned `DescriptorDomain`, then
  immediately reset/drop that caller owner. Using only the saved non-owning domain ref,
  execute cold + warm dedicated frames, cache clear, and normal `Shutdown()`. All
  descriptor operations remain valid until shutdown, proving the allocator owns a
  strong domain reference; after successful shutdown the saved ref becomes stale when
  no other owner exists.
- Healthy shutdown failure proof: inject canonical retirement admission failure during
  shutdown. `Shutdown()` returns false, allocator remains initialized, no pool native
  owner/domain is dropped, and retry after clearing the injection succeeds.
- Post-admission drain failure proof: admit every canonical descriptor retirement, then
  inject a non-device-loss `FlushRetiredResources()` failure. `Shutdown()` returns
  false with allocator/pools/domain/native owners intact and retryable; no path converts
  that failure into `DeviceLost()` semantics.
- RHI-ordering fail-safe proof: shut RHI down before allocator `Shutdown()`. Explicit
  `Shutdown()` fails without touching any pool/domain owner. The non-returning
  destructor path uses its fatal/terminal diagnostic and, if execution continues,
  strands the allocator implementation rather than calling `SafeRelease`/`Reset` on
  stale RHI owners.
- Healthy-abandon/destructor proof: with submitted work on a healthy backend, abandon
  execution and destroy/shutdown the allocator. Submitted-fence collection and
  canonical retirement precede graceful RHI idle/drain; allocator `DeviceUnavailable`
  state alone does not choose §10. Repeat with `Removed`/`ResetRequired` or a concrete
  `FailureCode::DeviceLost` and verify only those proven cases select device-loss
  cleanup.

Primary suite: `geometry_allocator_tests.cpp`, extending
`RunDedicatedResourceProviderSmokeTest`, `RunDedicatedResourceLifetimeReuseTest`,
`RunDedicatedResourceFailureAndBudgetTest`, and
`RunRenderFlowDescriptorPublicationSmokeTest`.

### RF3 — placed descriptors and pressure

- Warm placed-object hit reuses descriptor indices.
- Two aliased native objects sharing heap storage retain distinct descriptor bundles.
- Placed ownership/held-owner proofs accept exactly `1 + descriptorCount` and reject an
  additional escaped owner.
- Reusable descriptor maintenance trims only when the cached count exceeds the 8,192
  target and frame policy permits eviction; an under-target warm frame performs no
  candidate walk.
- Descriptor-budget pressure evicts the globally coldest last-demand bundle while the
  native resource remains cached/charged.
- Assigned/pending/external-held objects are never descriptor-eviction victims.
- Tiny configured target/cap values prove target hysteresis and hard-cap rejection.
- Initialization rejects target > maximum and maximum > a valid descriptor-domain
  capacity; invalid/no domain preserves descriptor-disabled behavior.
- Shared-domain allocation pressure triggers one rollback/eviction/retry cycle and no
  more than two total publication attempts.
- Reusing a descriptor-evicted physical object republishes its bundle correctly.
- Placed transaction rollback retires bundles for newly destroyed transaction objects
  and may retain bundles on pre-existing reusable objects.

Primary suite: `RunPlacedResourcePoolContractTest` and
`RunPlacedResourceExecutionSmokeTest`.

### RF4 — export/failure cutover

- Dedicated export validates exactly `2 + cachedDescriptorCount` references.
- Export detaches/retire descriptors before dropping the pool owner; publication stays
  valid while descriptor retirement waits on submitted fences.
- After fence completion/`RetireResources`, detached descriptors return to the domain.
- Zero-export A7 behavior remains unchanged.
- Retained imports never acquire allocator-owned canonical descriptors.
- Abandon/device-loss with submitted work leaves no premature reuse, leaked local
  handles or invalid ownership assertion.
- Retained packet/generation metadata does not own or retire borrowed pool descriptors.
- Forced partial export-retirement proof: a private RenderFlow test seam fails the
  second canonical-handle retirement after the first succeeds. `Finish` commits no
  publication and releases no pool resource owner; the admitted handle stays cleared,
  the remaining handle/resource stay owned, and terminal recovery/retry cleans the
  entry without double-retirement or stale-handle reuse.
- Actual device-loss coverage separately proves the only path where descriptor
  retirement failure/never-complete sentinel may be tolerated; healthy terminal
  lifetime behavior is already mandatory in RF2 and extended to placed ownership in
  RF3.

### RF5 — performance closure

Correctness: full Development/x64 `renderingTests`, `geometryAllocatorTests`, and the
relevant RHI NVRHI descriptor suite.

Matched Profile captures use the established workloads:

- normal quad;
- ~70-node graph-only probe;
- 64-resource RED-like mix; and
- 320-resource allocation-ceiling probe.

The 320 capture is taken after warm-up. Required counters/evidence are canonical
descriptor Allocate/Write/Retire operations per frame, pool descriptor hits/misses and
pressure evictions, plus Resolve subphase timing. RF5 closes only when a warm unchanged
320-resource frame performs approximately zero canonical descriptor Allocate/Write/
Retire operations and the normal per-binding `FinalizeDescriptors` publication pass is
deleted or contains only explicit exceptional work.

No FPS/microsecond improvement is claimed until those matched captures exist.

## 13. Gate handoff

RF0 authorizes no production source changes. The exact implementation order after user
authorization is:

1. RF1 descriptor-domain reclaim hygiene;
2. RF2 dedicated canonical bundle, ownership baseline, bounded descriptor budget and
   healthy allocator/domain terminal-lifetime cutover for dedicated owners;
3. RF3 placed bundle + global dedicated/placed pressure coordination + extension of
   healthy terminal lifetime to placed objects/heaps;
4. RF4 generation/export/device-loss ownership cutover; and
5. RF5 deletion of steady-state publication and matched performance closure.

The program must not keep the old generation-owned publication path as a permanent
fallback for ordinary dedicated/placed whole-resource uses. Temporary dual-path code is
allowed only while crossing an RF gate and must be removed by RF5.

## 14. Exact implementation surface

Primary RF1 surface:

- `source/rhi/nvrhi/src/common_backend.cpp`
- `source/rhi/nvrhi/tests/d3d12_backend_tests.cpp`
- `source/rhi/tests/rhi_tests.cpp`

Primary RF2/RF3 surface:

- `source/rendering/include/vanguard/rendering/render_flow_resource_allocator.hpp`
- `source/rendering/private/vanguard/rendering/render_flow_resource_internal.hpp`
- `source/rendering/private/vanguard/rendering/render_flow_resource_pool.hpp`
- `source/rendering/private/vanguard/rendering/render_flow_resource_placed.hpp`
- `source/rendering/src/render_flow_resource_allocator.cpp`
- `source/rendering/src/render_flow_resource_resolve.cpp`
- `source/rendering/src/render_flow_resource_pool.cpp`
- `source/rendering/src/render_flow_resource_placed.cpp`

Primary RF4/RF5 surface:

- `source/rendering/src/render_flow_resource_execution.cpp`
- `source/rendering/src/render_graph_scheduler_probe.cpp`
- `source/rendering/tests/geometry_allocator_tests.cpp`
- existing rendering/RHI test entry points and the retained matched Tracy workload.

RF does not require a new public module, new RHI lifetime manager, new descriptor
domain type or new render-graph execution path.
