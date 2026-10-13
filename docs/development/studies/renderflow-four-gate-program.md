# RenderFlow Aggressive Two-Layer Optimization Program

Status: **design locked. RF10-RF12 source implementation is complete and all three non-profile correctness gates are validated in Debug/Development; their performance gates remain open because at least one numeric criterion in each gate missed. RF13 is implemented, validated in Debug/Development, and closed against its compiled-steady-frame objective with matched Desktop profile evidence.**

Purpose: preserve the exact four-task program for the renderer CPU redesign so it survives chat compaction and can be resumed from the workspace alone.

## Direction

The program deliberately has two layers:

1. Make the uncached RenderFlow path intrinsically fast, bounded, and scalable, inspired by the strongest ideas in the inspected RED branch.
2. Add revision-driven caching so unchanged semantic contracts are not reprocessed at all.

Layer 1 remains mandatory even after Layer 2 exists. Cache misses, graph revisions, editor transitions, feature changes, and new graph variants must not fall off a performance cliff.

The target split is:

```
COLD / REVISION CHANGE:
    O(actual RenderFlow problem size), but aggressively bounded and cheap

WARM / UNCHANGED REVISION:
    O(dynamic patches + changed owners + real hazards + command recording)
```

Object-count scaling remains owned by GPU Scene / geometry and must not leak into RenderFlow.

---

## RF10 — Bounded Fast Compiler

### Goal

Make the cache-miss path RED-fast.

This is Layer 1, part A.

`SealPlanning` / `ResolveFrame` should explicitly separate the overwhelmingly ordinary resource-lifetime case from the exceptional semantic machinery.

The ordinary path should linearize:

```
declaration
→ uses
→ lifetime
```

directly from the existing dense `CandidateUse` representation, recognizing the normal balanced local-use/scope pattern without forcing every resource through the full general scope/swap state machine.

Arbitrary swaps, explicit cross-node scopes, unusual lifetime extensions, complicated aliasing semantics, and other genuinely nontrivial cases remain on the existing general compiler as the correctness fallback.

This is analogous to RED's trivial/nontrivial split, but Vanguard keeps its stronger semantics and proofs.

### Exact-current implementation refinement

Prefer making `SealPlanning()` publish one immutable **dense resolution image** rather than making `ResolveFrame()` reinterpret the writer representation again.

That image should use:

- compact sealed semantic op records;
- dense local-resource -> global-slot remaps;
- direct use ordinals;
- recording-time per-resource exception facts collapsed into global-slot fast-path flags;
- dense semantic ordinals suitable for lifetime/release buckets.

The trivial classifier is conservative. Any allocation affected by a mapping-sensitive `DeclareLike`, `SwapMappings`, explicit scopes, nested/unbalanced named-scope history, export mapping, or another relation that requires temporal interpretation must fall back to the existing general interpreter. Balanced non-nested named scopes remain eligible because RF10 preserves their exact open/close lifetime touches while skipping only the redundant depth-state machinery.

Physical release scheduling should use the already-monotonic semantic order rather than rebuilding a general-purpose heap/sort where ordering is already known. Dedicated assignment should retain the existing compatibility hash / ready indexes; placed planning should consume preordered requests and use bounded release buckets where possible.

Do not add another persistent physical-plan cache here. Keep `PersistentDedicatedPhysicalPlan` as the existing warm lane/lease optimization.

### RF10 implementation status — 2026-10-10

RF10 is implemented and its **non-profile correctness gate is validated** in the
current shared checkout. Matched Desktop profiling has now been run; RF10's
**performance gate remains open** because the fast lifetime path is nowhere near
the required approximately 2x improvement over the exact-current general path.

Implemented:

- Planning now publishes one compact 32-bit `CandidateSemanticEvent` stream for
  both operations and `UseEnd`. Resolve consumes that stream directly; the old
  `operations` + `useEndOrder` merge is gone.
- `SealPlanning()` publishes a global semantic-event offset/count. Compiled
  allocations retain first/last dense semantic ordinals in addition to the
  existing `PlanPosition` and command-scope witnesses.
- Writer recording maintains exact open-use cardinality. Seal publishes the
  aggregate unterminated-use count, so Resolve preserves the existing atomic
  `InvalidUseOrScope` failure boundary without rescanning every compiled use.
- Candidate resources record fast-path exception facts at authoring time.
  `DeclareLike`, swaps, explicit scopes, exports, nested/unbalanced named scopes,
  and every named alias that coalesces with one of those facts route to the
  existing general interpreter.
- Resolve performs one O(resources) named-identity coalescing pass, producing
  stable writer-local -> global-slot bindings and a global slot fast-path bit.
  The semantic compiler then direct-indexes those sealed bindings instead of
  repeating name/hash lookup for every operation.
- Stable, non-nested named scopes retain their exact open/close lifetime touches
  but bypass the general per-allocation scope-depth state machine. The renderer's
  normal `RTUseBegin`/`RTUseEnd` shape therefore remains semantically identical.
- Queue-sequence validation walks only allocations whose actual use queue mask
  spans more than one queue. Single-queue allocations do not enter the
  fork/join timeline proof; multi-queue allocations still use the existing exact
  `QueueOrderProved()` path.
- Same-frame dedicated reuse now releases lanes through semantic-ordinal buckets
  rather than an O(log R) release heap. Cross-queue pending candidates are
  append-only monotonic streams with cursors; final admission still requires the
  existing queue-order proof.
- Resolve-originated placed planning has a preordered mode using the already
  monotonic first-touch stream. It skips the temporary order array, global sort,
  and release heap. Generic/standalone planner callers retain the old deterministic
  sort/heap path.
- Placed alias predecessor fragments are hard-bounded **before** scratch append,
  using the allocator compiled-action cap for the Resolve path.
- Dense semantic ordinals are acceleration metadata only. They do not replace
  `PlanPosition`, command-scope identity, generation checks, swap semantics,
  export-after-swap binding, alias provenance, or cross-queue proof.

Validation performed on the exact-current implementation:

- `renderingTests` builds and passes in Debug and Development.
- Focused regressions cover begin-use-before-swap, export-after-swap,
  named-vs-explicit scopes, non-LIFO `EndUse`, direct `EndUse` capacity poisoning,
  stale use/range/decision identities, the balanced named fast path versus forced
  explicit/general fallback, and pre-append placed alias predecessor bounds.
- `geometryAllocatorTests` builds and passes in Debug and Development on the same
  implementation, covering real-backend reuse/alias/cross-queue integration.
- Targeted source searches show no remaining `useEndOrder`, dedicated release heap,
  dedicated cross-queue pending heap, `StructuralPlanMatchesCurrent`, or old full
  structural-signature warm path. Targeted `git diff --check` is clean apart from
  the repository's existing LF/CRLF conversion warnings.
- Desktop Profile measurements deliberately disabled the RF12 semantic contract
  cache so the cold compiler remained visible, then compared the ordinary RF10
  path against the same binary/source with the balanced named-scope fast-path bits
  forced onto the general lifetime path. On the 64-resource workload,
  `CompileOperations + ValidateLifetimes` is **219.875 us p50** on the ordinary
  path versus **214.866 us** on the forced-general path (ordinary is about 2.3%
  slower, i.e. noise/adverse rather than a win). At 320 resources it is
  **593.582 us** versus **597.932 us**, only about **0.7% cheaper**. This misses
  the approximately 2x criterion decisively.
- Cold ordinary-path scaling is sublinear from 64 -> 320 resources for the same
  compile/lifetime slice (**219.875 -> 593.582 us**, 2.70x CPU for 5x resources).
  A temporary Profile-only 1,280-resource extension measured **2,645.750 us**,
  4.46x the 320-resource CPU for 4x the resources and 12.03x the 64-resource CPU
  for 20x the resources. That is modestly superlinear at the large endpoint rather
  than an explosive cliff, but it does not rescue the failed fast-vs-general gate.
  The temporary 1,280-resource extension and forced-general/cache-disable hooks
  were removed after capture; the normal Profile source was rebuilt.

### Required invariants

- Preserve non-LIFO `EndUse` ordering.
- Begin-use identity remains bound to the allocation active at begin time.
- Exports resolve against the final logical mapping after swaps.
- Named-scope and explicit-scope semantics remain distinct.
- Queue ordering and cross-queue proofs remain exact.
- Generation/stale identity validation remains exact.
- Existing general compiler remains available as a differential oracle until parity is proven.
- Dense semantic ordinals are acceleration indices only; they must never replace `QueueOrderProved()` or command-scope/cross-queue ordering proofs.
- Reuse remains strictly-after-lifetime-end; equal/touching lifetime endpoints must not alias unless existing semantics explicitly prove it.

### Primary seams

- `source/rendering/src/render_flow_resource_allocator.cpp`
- `source/rendering/src/render_flow_resource_resolve.cpp`
- `source/rendering/private/vanguard/rendering/render_flow_resource_internal.hpp`

### Exit gate

- Forced-cache-miss execution is semantically equivalent to the general compiler.
- Focused tests cover:
  - begin-use before swap;
  - export after swap;
  - named-vs-explicit scope behavior;
  - non-LIFO use endings;
  - direct capacity rejection / poisoned writer behavior.
- 64 / 320 / large synthetic resource probes stay linear with no complexity cliff.
- No full-frame operation sort or release heap remains on the ordinary path where the sealed semantic order already provides the needed ordering.
- Placed alias predecessor fragments gain an explicit aggregate pre-append bound; no unbounded fragment growth is permitted.
- With semantic caches deliberately disabled, the compile/lifetime portion is approximately **2x cheaper** than the current general path on the matched desktop workload before RF10 closes.

---

## RF11 — Persistent Physical Owners + Sparse State

### Goal

Make physical resolution intrinsically cheap.

This is Layer 1, part B.

Dedicated and placed resources already have persistent physical owners. Retained imports should converge on the same ownership model.

A stable retained resource such as `Geometry.ShellPlans` should have persistent canonical descriptor ownership associated with the actual persistent resource owner / allocator-level ledger, keyed by the real resource generation plus device and descriptor-domain incarnation.

Frame generations borrow those canonical handles. They must not allocate, populate, and fence-retire the same SRV/UAV every frame merely because a new execution generation was created.

State handling should also become sparse:

- RenderFlow retains global semantic proof only where global knowledge is required:
  - alias activation/finalization;
  - UAV hazards;
  - cross-queue handoff;
  - import/export terminal requirements;
  - clears/discards;
  - presentation.
- Ordinary same-queue state progression should execute against the command-list-local RHI state machine and its existing local barrier coalescing.

The current compiled action tape remains the mechanism; the target is to make it sparse, not replace it with another replay system.

### Exact-current state split

`PhysicalBindingRecord::explicitState` already gives the useful dividing line.

For automatic-state resources, do not allocate/replay RF8 physical-state cells merely to derive an ordinary before-state. The existing RHI command-list tracker already supports a transition with an unknown before-state and validates/tracks the actual command-list-local state. The fast path should therefore carry the required state/clear intent to execution and let the command list perform local transition/coalescing.

Keep the existing RF8-style global lowering for the cases that actually require a global witness:

- explicit-state resources;
- cross-command-list/cross-queue predecessor state;
- retained import/export boundaries;
- alias handoff;
- terminal state contracts.

A small physical witness remains necessary for UAV hazard ordering even on the automatic-state path.

The common compiled representation should move toward a dense `CompiledUse[ordinal]` style image with direct allocation/physical/use/view information and compact exceptional work offsets. Avoid rebuilding a second `actionTape` post-pass from already-compiled step metadata when the sealed/compiled image can author the work tape directly.

### RF11 implementation status — 2026-10-10

RF11 is implemented and its **non-profile correctness gate is validated** in the
current shared checkout. Matched Desktop profiling has now been run with the RF12
semantic contract cache disabled. RF11's **performance gate remains open**:
physical assignment improved substantially, but state finalization did not.

Implemented:

- RHI now exposes a monotonic device incarnation. Retained-import canonical
  descriptor owners are scoped by that incarnation plus the allocator's
  generational descriptor-domain identity and the actual generational
  `ResourceRef`, preventing resource/device/domain ABA reuse.
- Retained-import SRV/UAV bundles are allocator-persistent. Execution generations
  borrow the canonical handles; normal Finish only publishes safe-after fences and
  releases the borrow instead of rewriting and retiring the descriptors every
  frame.
- Retained owners participate in the existing descriptor ledger and global
  pressure/maintenance victim selection alongside dedicated and placed owners.
  Only zero-borrow owners are evictable; eviction is fence-safe and full eviction
  removes the resource key so owner metadata stays bounded by cached descriptor
  ownership rather than historical imports.
- Resolve failure, abandonment, explicit cache clear, healthy shutdown and device
  loss now handle retained descriptor borrows/cache ownership separately. Device
  loss remains lenient; healthy retirement remains retryable.
- Native `LowerPhysicalStates()` no longer authors ordinary same-queue
  before/after transitions or command-scope entry tables for automatic-state
  resources. Each compiled begin step carries only its required state; execution
  applies `Unknown -> required` through the existing command-list-local RHI
  tracker/barrier coalescer.
- Automatic-state admission also proves the resource's close-time initial state
  is legal on the packet queue. This prevents `keepInitialState` from restoring a
  Graphics-only state while a Compute command list is closing.
- Whole-frame physical state cells remain only for resources that require a
  global witness: explicit-state owners, retained imports/exports, UAV history,
  aliasing, terminal contracts and presentation. Explicit cross-queue handoff
  still uses `QueueOrderProved()` and explicit Common bridging where required.
- Texture UAV hazards preserve mixed mip/slice state. Because the backend UAV
  primitive is resource-wide, exceptional lowering temporarily normalizes only
  non-UAV subresources around the barrier and restores their exact prior states
  immediately afterward rather than rejecting mixed-state textures.
- The exceptional action tape remains sparse: ordinary local state requests are
  intrinsic begin-step work, while alias activation/finalization, UAV barriers,
  clears, terminal transitions and presentation remain explicit semantic work.
  Legacy use replay and compiled-range execution preserve the same
  alias -> local-state -> exceptional-action ordering.
- On the native path, begin/end exceptional tape entries are authored inline
  while `LowerPhysicalStates()` walks the steps it already has to lower. The old
  second O(all-steps) tape-construction scan and dense `Reserve(steps)` remain
  only for logical-only validation; native finalization appends packet-exit tape
  entries in O(packet count).

Validation now covers the RF11 ownership and state-lowering contract directly on
the real D3D12/NVRHI backend in Debug and Development:

- automatic-state and explicit/general frames are paired across buffer/texture,
  UAV/clear, disjoint alias reuse, submitted execution, and stale-generation use
  rejection;
- texture subresources, mixed-state UAV barriers, buffers, placed aliasing,
  cross-queue fork/join, actionless/no-op state boundaries, imports and exports
  remain covered by the focused resource-flow integration suite;
- a genuine Win32 swapchain backbuffer is registered as a presentation import,
  used as a render target, lowered to `SwapChainPresentTransition`, submitted and
  presented successfully through RenderFlow;
- retained-import canonical SRV/UAV ownership remains warm after Finish, reuses
  stable handles, participates in pressure bounds, retires fence-safely on cache
  clear, rolls back transactionally after a later borrow cannot publish, cleans up
  on `DeviceLost`, and rejects stale cache ownership after an RHI/device
  incarnation change;
- `geometryAllocatorTests` passes in both Debug and Development with these cases.

At 320 resources, the matched cold-path p50 results are:

- `RenderFlow/Resolve/PhysicalAssignment`: **148.226 us**, down about **53.1%**
  from the preserved approximately 316 us baseline;
- `RenderFlow/Resolve/FinalizeStates`: **163.981 us**;
- nested `RenderFlow/Resolve/LowerPhysicalStates`: **159.925 us**.

The old state reconstruction/finalization band was approximately 158-163 us, so
the latter two measurements show essentially **no material reduction**. Therefore
RF11 does not satisfy its required two-phase performance gate even though
PhysicalAssignment itself is a strong win.

### Required invariants

- No weakening of aliasing correctness.
- No weakening of UAV RAW/WAW ordering.
- Automatic-state lazy lowering is allowed only for resources whose RHI contract permits command-list-local state discovery; explicit-state resources remain on the proven explicit path.
- Texture mip/slice/subresource state remains exact.
- Cross-queue ownership/state transfer remains explicit and proven.
- Import/export terminal state and queue contracts remain exact.
- Persistent descriptor eviction is fence-safe.
- Healthy shutdown, pressure rollback, abandonment, and device loss invalidate/retire correctly.

### Primary seams

- `source/rendering/src/render_flow_resource_resolve.cpp`
- `source/rendering/src/render_flow_resource_execution.cpp`
- `source/rendering/src/render_flow_resource_pool.cpp`
- `source/rendering/private/vanguard/rendering/render_flow_resource_internal.hpp`
- `source/rhi/nvrhi/src/common_backend.cpp`

### Exit gate

Real D3D12/NVRHI coverage must include:

- texture subresources;
- buffers;
- UAV RAW/WAW;
- aliasing / placed resources;
- cross-queue fork/join;
- zero-command / actionless nodes;
- clears;
- imports;
- exports;
- presentation;
- failure rollback;
- device loss.

Tests must also compare the lazy automatic-state path against the explicit/general path for texture, buffer, UAV, clear, alias-reuse, and stale-generation behavior.

Retained descriptor ownership must be bounded, pressure-aware, fence-safe, and device/domain-epoch protected.

Matched profiling with semantic caches disabled must show a major reduction in:

- `RenderFlow/Resolve/PhysicalAssignment`;
- `RenderFlow/Resolve/FinalizeStates` / `LowerPhysicalStates`.

The win must not simply move into command recording or the RHI backend.

---

## RF12 — Authoritative Revision-Compiled Resource Contract

### Goal

Stop rediscovering unchanged resource declarations.

This is the first true Layer 2 gate.

The compiled resource contract belongs to the relevant `RenderGraphCache::CacheEntry`, not to the allocator as one global "last structural plan".

Each graph-cache entry owns an immutable semantic contract for that exact graph variant / structural revision:

- logical slots and descriptors;
- names / stable lookup data;
- ordered uses;
- canonical local scope topology;
- views;
- static lifetime structure;
- queue/dependency structure;
- import patch locations;
- export patch locations;
- decision patch locations.

On an unchanged warm hit, Vanguard must **not**:

- execute `PrepareResourcesParallel()` merely to replay `DeclareResources()`;
- create planning writers merely to rediscover the same shape;
- `SealPlanning()` a duplicate candidate tape;
- compute a full structural hash and then walk the whole candidate contract again in `StructuralPlanMatchesCurrent()`.

The existing `PersistentStructuralPlan` should become the cold compiler artifact or be subsumed by this cache-entry-owned contract. Do not create a redundant second structural cache alongside it.

### Structural revision rule

Any setting that can change declaration topology, resource descriptors, usage flags, queue structure, swap/scope structure, or another semantic contract property must participate in the graph/resource-contract revision key.

Example:

```
GBuffer normally:
    Depth
    GBuffer0
    GBuffer1
    Velocity

EnableObjectPicking changes to:
    Depth
    GBuffer0
    GBuffer1
    Velocity
    ObjectId
```

That setting must alter the relevant `featureMask` / graph key / structural revision. The next frame becomes a cold compile for the new graph-cache entry or revision. Compatible physical resources can still be reused by RF11; only the semantic contract is rebuilt.

Initially, any structural node revision may rebuild the entire RenderFlow contract for that graph entry. Incremental per-node recompilation is explicitly deferred until profiling proves it is necessary.

### Safe cache payload

Only immutable **semantic structure** may persist.

Safe examples:

- normalized declarations/descriptors;
- names;
- ordered uses;
- scopes;
- swaps;
- views;
- queue/dependency topology;
- export contract shape;
- import/export/decision patch locations.

### Forbidden evergreen cache payload

Do **not** persist these across executions as if they were semantic structure:

- `PhysicalResourceId`;
- RHI texture/buffer/native references;
- SRV/UAV descriptor handles or GpuIndex values;
- `ImportedResourceId`;
- `ExportSlotId`;
- old `ResourceUseId` generations;
- old `DecisionId` generations;
- execution-range epochs / packet liveness;
- physical transition / alias action objects containing physical IDs;
- presentation acquisitions;
- incoming fence values;
- completion receipts / ready fences.

These must be patched, rebound, or regenerated for the current execution generation.

### Dynamic patch layer

Typical warm-hit patches:

- current retained import resources;
- current native resource generation;
- backbuffer / swapchain acquisition;
- incoming wait fence values;
- dynamic decision truth;
- current export slots/generations;
- frame serial;
- device / allocator / descriptor-domain epochs;
- other explicitly declared frame-dynamic inputs.

Executable `DeclareResources()` code must not be allowed to hide structural mutations behind an unchanged revision. Every topology-affecting input must be represented by an explicit structural revision/key witness.

### RF12 implementation status — 2026-10-10

RF12 is implemented and its **non-profile correctness gate is validated** in the
current shared checkout. Debug/Development focused validation is green. The
matched Desktop profile is now complete. Warm structural work is successfully
deleted, but RF12's **performance gate remains open** because `Resolve/Setup`
misses the <=25%-of-baseline target. RF13 has not been started.

Implemented:

- `RenderGraphCache::CacheEntry` is the sole owner of the persistent semantic
  `RenderFlowResourceContract`. Cache reset/eviction destroys the contract with
  the graph revision; the allocator no longer owns an evergreen structural-plan
  cache beside it.
- Cold cached revisions compile declarations normally, capture normalized
  generationless node binding templates, build the structural compiler artifact,
  and publish both transactionally only after Resolve succeeds. Failed capture,
  Resolve, cancellation, or device loss discards candidate state while leaving a
  previously published cache-entry contract untouched.
- Warm unchanged revisions rebuild only frame-owned binding witnesses from the
  normalized template, stamp the current **planning session generation**, patch
  dynamic decisions, and bind the current import/export patch sets. They skip
  `PrepareResourcesParallel()`, planning writers, duplicate `SealPlanning()`, the
  old full structural signature, and the old full structural comparison.
- `ResourceContractPatchId` is a stable semantic patch identity independent of
  the current `ExternalResourceToken` / native `ResourceRef`. Production cached
  frame output, presentation, geometry shell plan, diagnostic readback, GPU Scene
  tables, and vertex/index arena imports use explicit semantic patch IDs. Warm
  matching is key-based and order-independent, while exact patch-set cardinality
  prevents a structural import/export superset from being silently ignored.
  Legacy callers that omit an explicit semantic key receive a session-scoped
  compatibility patch, deliberately forcing a cold compile on the next frame
  instead of treating native identity or reservation position as an evergreen key.
- Import matching revalidates descriptor shape, entry/terminal state and queue,
  readiness, presentation role, and explicit-wait queue. Incoming fence **values**,
  presentation/backbuffer acquisition, current retained native resources, export
  slot generations, and current session generations remain per-frame patches.
- The cached node template contains names, lookup topology, normalized use and
  decision ordinals, and range shape only. It does not retain execution packets,
  cursors, resolved texture/buffer witnesses, compiled execution ranges, native
  descriptor handles, current imported/export IDs, or live execution-generation
  identities. Structural records that share runtime-capable record types are
  explicitly normalized by clearing generation/physical/action fields before
  publication.
- Dynamic declaration-time truth has a dedicated decision-only
  `PatchResourceContract()` path. Production currently has no `RTDecision()`
  users, so zero-decision contracts bypass that virtual walk entirely. If a patch
  hook accidentally attempts to replay a static declaration/resource operation,
  the warm attempt is rejected and falls back to cold compilation rather than
  turning the recoverable miss into a fatal error.
- Contract reuse is scoped to the current RHI device incarnation and descriptor
  domain generation. Device/domain drift rejects warm binding; a later successful
  cold publication installs the new scope. Planning-session and execution
  generations are issued from process-wide nonzero atomic sequences, so
  allocator/RHI teardown and reinitialization do not restart value-only
  use/decision/import/export/range identities at generation 1.
- The renderer carries a lightweight resource-contract revision witness for
  geometry descriptor-shaping capacity, indirect-argument capacity, shell-draw
  count, actual diagnostic-readback presence, ordered output-region view
  membership, frame-output presence/kind/color encoding, and texture-output
  terminal state/queue/publication readiness. Per-camera dimensions/phases and
  dependency topology are already part of `RenderGraphKey`. External geometry
  topology is additionally witnessed by the exact semantic import patch set, so
  descriptor/topology/output-contract changes fall back to a cold compile instead
  of replaying stale shape. The revision keeps a digest only as a fast rejection
  gate and then exact-compares every declaration-shaping field; warm correctness
  does not depend on a 64-bit digest being collision-free.

Validation completed so far on the exact-current implementation:

- the provider smoke cold-compiles a real `RenderFlowResourceContract` and warm
  binds it by stable semantic import/export patch IDs; warm execution has zero
  planning writers and does not replay declaration planning;
- a focused dynamic-patch proof has passed on real D3D12 for native import A -> B
  replacement and now **requires** the released native buffer slot to recycle the
  same `ResourceRef` index with an advanced generation before the warm patch is
  accepted. It also rejects same-token/different-native and
  same-native/different-token registration conflicts, proves an increasing
  incoming fence value does not invalidate an otherwise identical contract,
  rejects an incoming queue change, reserves two export patches in opposite
  orders while preserving current slot generations, distinguishes submitted from
  aborted publication, and proves a failed warm Resolve preserves the prior
  published contract for subsequent warm recovery;
- the real presentation proof now cold-captures a contract on one genuine
  swapchain acquisition, warm-binds a second acquisition with the same semantic
  patch, resolves the current backbuffer, rejects the old-generation use, keeps
  one structural build/one warm hit/zero planning writers, and successfully
  Presents both frames;
- `RF-EX1` covers decision true -> false warm patching, missing dynamic-decision
  patch rejection, current-generation use IDs and publication stability;
- exact resource-contract revision-key equality is tested independently of hash
  collisions, and `RenderGraphCache` tests prove renderer-revision and graph
  dependency-topology changes select cold cache entries;
- a real-D3D12 cache-entry revision proof cold-publishes a baseline contract,
  confirms the unchanged graph key selects the same warm cache entry, then changes
  `rendererRevision` and proves a distinct cache entry cold-publishes the changed
  semantic shape. One case changes swap count/order and verifies the resulting
  physical mapping; another changes Graphics-only execution into
  fork/Compute/join and verifies the compiled packet queues. Each case records two
  structural builds/two misses/zero warm hits rather than reusing the old
  contract. The parallel logical-only tests deliberately expect no persistent
  structural publication because `allowLogicalOnlyValidation` forbids it;
- same-device descriptor-domain replacement rejects the old contract with
  `InvalidOrStaleIdentity` without changing its publication serial, then cold
  republishes under the new domain. An isolated `AbandonDevice()`/D3D12
  reinitialization proof does the same across a new RHI device incarnation while
  keeping old-incarnation owners away from the replacement backend;
- `geometryAllocatorTests` and `renderingTests` both build and pass in Debug and
  Development with the above exact-current coverage. The already-green RHI suites
  were not rerun because this closure work changed no RHI production source.

Targeted searches show no production `StructuralPlanMatchesCurrent`, full
structural-signature warm path, or allocator-owned evergreen structural plan. No
known RF12 non-profile correctness/stress gap remains in the documented exit
matrix.

Matched Desktop profiling used the physical Desktop host
`DESKTOP-SE2KD37` (Intel i7-4790K 4.00 GHz, GTX 1060 6GB, Windows 10 IoT
Enterprise LTSC 19044), exact 1280x720 client pixels, verified foreground focus,
and the final 12 seconds of each Tracy capture. The canonical warm runs were:

- final restored normal quad `rf12_final_normal_quad_20261010_105326`:
  **88.108 us p50 Resolve**, **31.916 us Setup**, **23.722 us
  PhysicalAssignment**, **29.064 us FinalizeStates** / **28.101 us
  LowerPhysicalStates**, with zero
  `CompileOperations` and zero `ValidateLifetimes` occurrences in the final window;
- 64-resource RED-like `rf12_resources64_20261010_103204`: **180.533 us p50
  Resolve**, **57.015 us Setup**, again zero compile/lifetime replay;
- 320-resource ceiling `rf12_resources320_20261010_103316`: **448.506 us p50
  Resolve**, **123.071 us Setup**, again zero compile/lifetime replay.

Against the preserved RF5 warm Resolve baselines (153.598 / 246.963 / 853.268 us
for normal / 64 / 320), those totals are approximately **43.0% / 26.9% / 47.4%
lower**. (Using the final restored normal run precisely gives a 42.6% normal-quad
reduction.) However, the RF12 gate is specifically `Resolve/Setup`: the final
normal-quad **31.916 us** is **49.9%** of the documented approximately 64 us baseline, versus
the required <=25% (approximately <=16 us). Warm semantic replay deletion is
proven; the numeric Setup target is not met.

The final normal Profile Runtime was rebuilt after removing every temporary
cache-disable/general-path/1,280-resource instrumentation hook at
`D:\ENGINE\bin\Runtime\Profile\Vanguard.exe` (SHA-256
`5DDE2AD298943A30EA2928268EC9A600BBC22EE66CFB54BE08772460DF854237`).

### Exit gate

Required stale/ABA coverage includes:

- warm import A → B replacement with identical semantic descriptor contract;
- changed import registration order;
- duplicate token/ref conflicts;
- recycled native index/generation ABA;
- increasing incoming fence value;
- incoming queue variation;
- new swapchain acquisition;
- dynamic decision true → false without topology change;
- swap-order change causing structural miss;
- incompatible descriptor change causing structural miss;
- export slot generation/reordering/abort vs submitted success;
- stale old use/decision/range/packet IDs rejected after terminal completion;
- renderer revision / graph topology / queue topology change;
- failed warm bind/resolve transaction preserving the previous proven contract;
- device loss / abandonment / RHI reinitialization;
- descriptor-domain epoch replacement.

Warm unchanged execution must record:

- zero declaration replay;
- zero planning writers for static contract rediscovery;
- zero full structural comparison.

The current roughly 64 us `RenderFlow/Resolve/Setup` baseline should collapse to the dynamic patch/instantiation layer. Use **<= 25% of that matched baseline** as the initial performance exit criterion rather than accepting a token improvement.

---

## RF13 — Compiled Steady-Frame Program

### Goal

Remove the remaining static graph-size CPU work after RF12 proves the cache boundary.

Compile the safe static execution topology into the same graph-cache entry:

- flattened CPU node order;
- dependency adjacency;
- incoming/outgoing count templates;
- node → packet mapping;
- command-scope topology;
- queue topology;
- resource lookup/range layouts;
- structural skeleton of semantic action sites.

`RenderNodeJob::RunRenderNodeJobs()` currently rebuilds CPU ordering and dependency counts every frame. That topology should become an immutable graph artifact.

Jobs2 tasks, counters, continuations, and current frame payloads remain per-frame objects, but they are instantiated from compiled topology rather than rediscovered by scanning all graph edges.

The current `PersistentDedicatedPhysicalPlan` should evolve into or be subsumed by a separately epoch-protected physical-program layer capable of keeping strong pool leases per graph variant where safe.

Fresh current-generation `PhysicalBindingRecord` values must still be produced. Do not leak generation-scoped physical identities through the semantic cache.

Placed-resource topology may join this program only after its alias provenance and stale-chain rules are proven under the same epoch model.

### Required invariants

- No stale execution identity survives graph-cache eviction/reuse.
- No physical/native chain survives device/allocator/domain reincarnation without an epoch proof.
- Dynamic imports/fences/decisions/external replacements remain patches, not silent topology changes.
- Task/counter lifetime remains frame-scoped.
- Job continuations and command-list ownership remain correct.

### Exit gate

Stress:

- all four graph-cache entries;
- repeated alternation between graph variants;
- cache eviction and rebuild;
- renderer revision changes;
- device loss and RHI recreation;
- descriptor-domain recreation;
- dedicated / placed policy changes;
- external resource replacement;
- stale witness/ABA cases.

Profile graph scales such as approximately:

- 80 / 250 / 500 passes;
- 300 / 1000 / 3000 resources.

On an unchanged revision, planning/resource-contract CPU should scale with:

```
dynamic patches
+ changed physical owners
+ actual semantic hazards
+ command recording
```

not with the total number of static declarations/uses.

Scheduler setup should scale with actual recording groups/jobs, not by rescanning the complete static graph topology every frame.

The current roughly 31 us `RenderGraph/ScheduleNodes` topology work should largely disappear rather than merely receive a smaller constant-factor optimization.

### RF13 closure record — 2026-10-10

RF13 deliberately reuses RF12's normalized resource-contract image instead of
creating another immutable monolith. `PersistentStructuralPlan` and the
cache-entry-owned node templates continue to own packet, command-scope, queue,
resource-range, lookup, and semantic-action structure.

The remaining CPU execution topology is now compiled once by
`RenderNodeGraph::BuildRenderFlowGroups()` and retained by the graph-cache
entry. The graph stores the CPU node order, CSR parent offsets/parent usage
indices, root/leaf counts, and the unique terminal leaf. Steady
`RenderNodeJob::RunRenderNodeJobs()` no longer rebuilds a flattened node array,
incoming/outgoing counts, scans `m_dependencies`, walks parent linked lists, or
rediscovers the terminal leaf. A final source search has no Vanguard production
call to the old `BuildFlattenedNodesArray` path.

Jobs2 task/counter lifetime remains frame-scoped. RF13 also removes the
avoidable per-node heap allocation previously performed by
`jobs::Task::Create`: render-node tasks now borrow their already frame-owned
`RenderNodeResourceBindings::Entry` payload through `Task::CreateBorrowed`.
The binding entry already had to outlive `NodesComplete`; only the payload
ownership changed, not the job lifetime or continuation model.

The dedicated physical-plan side is now a bounded allocator-owned cache of four
`PersistentDedicatedPhysicalProgram` entries. A program is admitted only for an
exact process-unique resource-contract identity plus publication serial, RHI
device incarnation, and descriptor-domain identity. Entries retain the proven
strong dedicated-pool leases and use bounded LRU replacement. Fresh
generation-bound `PhysicalBindingRecord` values are still instantiated every
frame. Placed resources remain intentionally outside this cache; enabling the
placed policy releases dedicated physical programs instead of silently reusing
an incompatible chain.

Correctness validation after the final borrowed-task change:

- Development: `jobsTests`, `renderingTests`, and `geometryAllocatorTests` all PASS.
- Debug: `jobsTests`, `renderingTests`, and `geometryAllocatorTests` all PASS.
- The RF13 five-contract physical-program oracle fills the four-entry cache,
  proves warm hits for the retained B/C/D variants, evicts the oldest physical
  companion with a fifth contract, and then proves contract A remains a warm
  semantic hit while rebuilding only its evicted physical program.
- Existing graph-cache coverage exercises all four `RenderGraphCache` entries,
  LRU eviction/rebuild, and renderer-revision/topology misses. RF12's retained
  dynamic-patch, descriptor-domain replacement, RHI-incarnation, external
  replacement, and stale-identity proofs are reused as RF13 epoch guardrails;
  these are not weakened or duplicated into a second cache.
- Existing placed-provider/execution coverage remains green while RF13 keeps
  placed physical topology explicitly uncached.

The final measured Profile Runtime is
`D:\ENGINE\bin\Runtime\Profile\Vanguard.exe`, 7,703,552 bytes, SHA-256
`25BFFBAF1F356D502E011D5E07132F56322803F3E51FBF736F3C09D661BB59A2`.
Matched Desktop captures used the same 1280x720 foreground-verified, 18-second
capture / 2-second warmup / final-12-second statistic rule as RF10-RF12.

Normal quad, `rf13_normal_finalcandidate_20261010_123618`:

- whole CPU frame p50 **1.151660 ms**, p95 **1.635740 ms**, p99 **1.916585 ms**,
  mean **1.205835 ms**, approximately **829.25 FPS**;
- pre-RF13 whole-frame p50 was 1.150655 ms, so RF13 introduces no measurable
  normal-frame regression;
- `RenderGraph/ScheduleNodes` p50 **27.586 -> 23.955 us**;
- more importantly, `ScheduleNodes` self p50 **11.299 -> 5.780 us**
  (**48.8% lower**), p95 **20.159 -> 11.290 us**.

Representative scheduler/resource stress,
`rf13_taskinline320_20261010_123406`, uses the existing approximately 80-node
scheduler graph plus 320 probe resources:

- whole CPU frame p50 **5.168751 ms**, p95 **6.106935 ms**, p99 **6.678771 ms**;
  this number remains dominated by deliberate synthetic CPU saturation and is
  not counted as renderer CPU cost;
- `RenderGraph/ScheduleNodes` p50 **179.525 -> 155.347 us**, p95
  **296.974 -> 239.821 us**;
- `ScheduleNodes` self p50 **59.601 -> 24.345 us** (**59.2% lower**), p95
  **100.237 -> 40.503 us**.

The total `ScheduleNodes` zone intentionally does not collapse by the same
percentage because it includes real per-frame Jobs2 builder creation,
dependency waits, dispatches, and completion-counter extraction. Temporary
nested profiling isolated the now-removed render-node task-preparation seam at
approximately 23.9 us p50 on the 80-node stress graph; the dominant remaining
children were Jobs2 builder and wire/dispatch mechanics required for actual
jobs. The temporary profiling scopes were removed before the final binary.

Therefore RF13's performance claim is specifically the documented one: static
graph topology is no longer rediscovered each frame, and the renderer-owned
`ScheduleNodes` self component largely disappears while required frame-scoped
job instantiation remains. The current probe covers the representative ~80-pass
/ 320-resource point; the design's larger 250/500-pass and 1000/3000-resource
examples were not separately captured in this closure and are not represented
as measured results.

RF10, RF11, and RF12 performance gates remain **OPEN**. RF13 does not retroactively
close those missed numeric criteria.

---

## Final target architecture

```
                    COLD / REVISION CHANGE

RenderGraph
    ↓
RF10 bounded semantic compiler
    ↓
RF11 persistent-owner + sparse-state compiler
    ↓
Compiled Resource Contract
Compiled Execution Topology
    │
    └──────────── retained by RenderGraphCache::CacheEntry


                    STEADY FRAME

graph-entry structural revision check
    ↓
patch:
  imports / current native generations
  backbuffer / acquisition
  fences
  decisions
  export slots
  allocator/device/descriptor epochs
    ↓
borrow persistent physical owners/descriptors
    ↓
instantiate current-generation witnesses
    ↓
record commands
```

## Non-goals / guardrails

- Do not optimize only for the current quad workload.
- Do not let scene object count enter RenderFlow complexity.
- Do not weaken correctness for RED likeness.
- Do not cache physical/resource chains without explicit owner/device/epoch proof.
- Do not build a giant immutable monolith in one step.
- Do not duplicate the current `PersistentStructuralPlan` and `PersistentDedicatedPhysicalPlan`; subsume their proven invariants.
- Do not reopen already-closed RenderFlow #1-#12 / A1-A7 work without new profile evidence.
- Preserve unrelated dirty checkout state.
