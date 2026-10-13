# V1 — Vellum runtime kernel

## Status and authorization

The user explicitly started **V1 Runtime Kernel** on 2026-10-10. V1 is **CLOSED**.

V0 Architecture is closed and is the governing architecture contract. V1 may refine concrete type,
file, capacity and algorithm choices from exact-current implementation evidence, but it may not weaken
the V0 ownership or hot-path invariants to make the first tree easier. **V2, V2.5 and V3-V9 remain
unauthorized.**

V1 is the first production-code phase. It implements the headless runtime kernel only; it does not add
renderer integration, text shaping, routed input/focus, styles/animation playback, data virtualization,
world/projected surfaces, accessibility bridges or Editor authoring UI.

Repository validation follows `AGENTS.md`: no test/build expansion during ordinary intermediate gates.
V1 P6 is the final-gate exception and must compile the focused target and run the smallest focused proof
tests necessary for closure. A genuinely new-subsystem viability compile may be used earlier only if a
source-only review cannot establish viability and the reason is recorded first.

## V1 objective

Implement the exact V0 entry path without architectural invention:

```text
final-format cooked Vellum document bytes/resource
  -> bounded decoder/validation
  -> immutable exact-generation document resource
  -> unique bounded spawn/materialization operation
  -> compact generational document/node runtime
  -> root + production stack-panel policy + production leaf payload
  -> typed property writes through compact property addressing
  -> deterministic structural mutation commit
  -> sparse semantic invalidation
  -> measure/arrange to one committed geometry truth
  -> diagnostics/query
  -> destroy/cancel/release with zero stale ownership
```

The first visible rectangle is deliberately **not** a V1 objective. V1 proves the runtime architecture
that V2 will later render.

## Gate structure

| Gate | Purpose | State |
| --- | --- | --- |
| Definition | Re-read exact-current Vanguard/RED seams; lock concrete V1 module, ABI, format, capacities and proof shape | **CLOSED** |
| P0 | Add `vellum` module/build boundary and public value/config/stat/failure ABI | **CLOSED** |
| P1 | Compact generational document/node arena, ordered topology and deterministic structural mutation | **CLOSED** |
| P2 | Typed core property slots, sparse invalidation/active sets and basic measure/arrange | **CLOSED** |
| P3 | Final sectioned cooked document format, bounded validation and immutable resource decoder | **CLOSED** |
| P4 | Injected ResourceStreamer integration plus bounded unique spawn/materialization/cancellation | **CLOSED** |
| P5 | Capacity/teardown/cardinality hardening and focused proof instrumentation/source coverage | **CLOSED** |
| P6 | Focused compile/proof tests, adversarial review and V1 closure | **CLOSED** |

Only V1 is authorized. Closing a gate does not authorize V2.

## Definition — exact-current source grounding

**State: CLOSED.** Exact-current Vanguard module/memory/container/generation/serialization/resource/
streaming seams and RED retained/layout behavior were reconciled before production source edits. The
Definition closes with no implementation blocker: V1 can be implemented as one headless `vellum`
module using checked fixed-capacity runtime blocks, the existing `VLUM` sectioned document container,
an injected `ResourceStreamer&`, and exact strong resource-generation pins. No build/test/native
execution was used for Definition.

### Vanguard module/dependency direction

The production module is `source/vellum`, static library `vellum`, in `Engine/Runtime`.

Locked dependency direction:

```text
vellum
  -> system
  -> memory
  -> concurrency
  -> containers
  -> resources
  -> streaming
  -> crypto headers only, required transitively by Streaming's public resource-source header
  -> serialization only when format helpers require it
  -/-> engine
  -/-> runtime
  -/-> editor
  -/-> rendering
```

`vellum` is included after `source/streaming` in the root Premake graph. V1 does **not** create
`engine::VellumService`; the headless kernel takes exact low-level authorities explicitly.

`Runtime::Initialize` captures the calling `concurrency::ThreadId` as the one semantic owner. Structural
mutation, invalidation/layout update, quiesce/drain and shutdown execute only on that owner thread and
return `WrongThread` otherwise. V1 deliberately does not hard-code the owner to Vanguard's main thread:
interactive composition initializes there, while headless/tool compositions may choose another stable
thread without weakening single-owner determinism.

Allocator-aware runtime storage uses existing Vanguard pools rather than adding a new global memory-pool
identity in V1. Vellum-owned runtime blocks/objects use `memory::PoolId::Runtime` /
`memory::pools::Runtime`; streaming-owned staging continues to use the streaming/resource owners.
Vellum-specific allocation/cardinality diagnostics are tracked by Vellum counters rather than inferred
from a new process-global pool.

### Exact-current Vanguard patterns adopted

- value handles use `{index, generation}` with generation zero invalid;
- generational/dense modules commonly use allocator-aware Vanguard containers and explicit capacity;
- capacity is reserved explicitly and admission failure is returned, not hidden by unbounded growth;
- logical identity and dense physical storage may be separated by indirection;
- `ResourceStreamer` remains runtime resolution authority;
- a Vellum decoder is a normal `streaming::DecoderDescriptor` over raw final-format bytes;
- each spawn owns a move-only `resources::PipelineRequest`; underlying pipeline work may coalesce;
- after load success, spawn calls `Acquire()` and retains the exact strong `ResourceHandle` generation
  before request interest may be reset;
- decoder unregister occurs only after Vellum requests/strong handles/resources drain.

### RED behavior adopted, RED storage deliberately not copied

RED ink confirms the semantic baseline V1 needs now: retained ordered hierarchy, explicit parent/child
mutation, layout invalidation, cached desired size, visibility/layout gating and panel measure/arrange.
`BasePanelWidget` demonstrates content-size aggregation and ordered child arrange semantics.

Vellum intentionally does **not** copy RED's heap/RTTI `Widget` object per node, recursive traversal or
runtime name/reflection property access. Those are capability/behavior references, not Vellum's runtime
storage ABI.

### Runtime handle and storage ABI

Public value identities are generation checked:

- `VellumInstanceHandle { index, generation }`;
- `VellumSurfaceHandle { index, generation }` is declared as a value ABI now even though V1 does not
  create a surface registry;
- `LocalNodeHandle { slot, generation }` is valid only inside one instance;
- `VellumNodeHandle { instance, local }` prevents equal local slots in different instances aliasing;
- `PropertyHandle { node, compact schema slot, expected value type }`; invalidation is descriptor
  metadata and is not caller-owned address state;
- `SpawnHandle { index, generation }` with move-only caller interest layered over it.

Generation zero is invalid. A generation never wraps back into a previously valid value: exhausting the
representable generation permanently exhausts that slot. Destroyed slots are not reused during the same
semantic update; they enter a deferred-free set and become reusable only at a later update boundary after
all old dirty/active/queued work has been quarantined.

The **semantic update epoch**, not a mutation-commit checkpoint, is the reuse fence. One top-level
semantic update may perform more than one bounded structural/layout settlement pass. Any instance/node
slot retired during that epoch stays in `deferredFree` for all later checkpoints in the same update.
Only the next top-level semantic-update boundary promotes those slots: generation increments before the
slot becomes free; `0xffffffff` retires the slot permanently. Commit never advances this epoch by itself.
Destroy itself leaves the old generation unchanged while the slot is `DeferredFree`, so all stale work
from that epoch continues to fail by lifecycle state even before the generation advances.

Exact-current RED/Vanguard `DynArray::Reserve()` is a `void` operation whose underlying reallocation path
does not expose a recoverable allocation result. V1 therefore does **not** use growable `DynArray` as the
authoritative admission mechanism for node/cardinality guarantees. Core instance/node storage, ordinary
structural commands, per-slot terminal-destroy metadata/index storage, every dirty frontier/active set,
iterative layout scratch, spawn/materialization records, and immutable decoded-resource tables use a
bounded number of checked `memory::Allocate` blocks with immutable capacities plus overflow checks.
Temporary/offline helper containers may still use Vanguard containers where allocation failure is not
being presented as recoverable semantic admission. Warm semantic update performs no container growth.

Each live document owns one compact node arena made from a bounded O(1) number of checked blocks:

- stable public slot table: generation, dense row, deferred/free state;
- dense authoritative node rows;
- row -> public slot back-reference;
- topology links stored as stable document-local slot indices: parent, first/last child,
  previous/next sibling;
- dense side stores aligned with rows for core properties, committed geometry and node-kind payload
  references;
- stack-panel payloads live in a compact panel side store; leaves need no per-node heap object.

The spawned document's node capacity is explicit: at least the cooked node count and at most the runtime
maximum. A spawn option may request additional node headroom for runtime Create; if it does not, the
document need not reserve speculative growth. Allocation failure during off-tree materialization fails
the spawn atomically before publication.

Deleting a row may swap-move the last physical row and repairs only slot->row/back-reference/side-store
indices. Topology does not embed row indices, so physical compaction cannot invalidate public identity.

No ordinary V1 node owns a `shared_ptr`, virtual table, lock or atomic.

### Initial production node/property surface

V1 implements two real runtime node kinds, sufficient to prove extensibility without fake rendering:

- `StackPanel`: ordered horizontal/vertical panel policy with fit-to-content measure and deterministic
  child arrange;
- `Spacer`: renderer-neutral production leaf with intrinsic logical width/height and no children.

Core typed properties are compiled numeric identities, not strings/reflection traversal. The initial
set is intentionally small but final-direction:

- `Width` / `Height`: non-negative finite float logical size on `Spacer` only; Measure + Arrange
  invalidation. `StackPanel` remains fit-to-content in V1 rather than inventing a float sentinel for
  `Auto`;
- `Visibility`: `Visible | Hidden | Collapsed`; Collapsed gates subtree layout/presentation work;
- `Opacity`: float; Paint-only invalidation, proving a render-only property never forces layout;
- `PanelOrientation`: horizontal/vertical panel side-store property; Measure + Arrange invalidation; and
- `PanelSpacing`: non-negative logical spacing between participating children; Measure + Arrange invalidation.

Public code may resolve an authored/stable property ID to a compact `PropertyHandle` once; steady writes
operate on that typed compact address. Semantic equality is checked before mutation/invalidation.

### Sparse invalidation and layout

V1 defines dirty domains even before V2 exists:

- TextShape;
- Measure;
- Arrange;
- Paint;
- HitTest;
- Navigation.

`TextShape` and `Navigation` remain in the compact domain ABI even though V1 has no text shaper or
navigation runtime yet. P5's architecture extension appends the dedicated `Semantics`/Accessibility
frontier after these six V1 domains; the existing V1 bit identities remain unchanged. V1 P2 only
produces active frontier work for kernel-owned `Measure` and `Arrange`. `TextShape`, `Paint`, `HitTest`
and `Navigation` remain compact dirty evidence only until their owning phases provide consumers; P2 does
not retain live WorkRefs for unconsumed domains and therefore cannot create unchanged-frame discovery
cost from future-domain dirtiness.

Each **active** domain uses first-dirty-only generation/stamp dedup plus a sparse frontier; dormant future
domains retain only compact per-slot dirty evidence in P2. Clean nodes are not discovered by walking the
tree. Measure invalidation may climb only the required layout ancestry; Arrange invalidation marks the
narrow required subtree/root boundary. Paint-only writes do not enqueue Measure/Arrange.

Layout is iterative/non-recursive. Measure runs bottom-up over dirty layout closure. Arrange runs from
the smallest invalidated arranged roots downward. One committed geometry record per node contains its
rectangle in **instance-root logical space** plus a geometry generation that is independent of semantic
epoch. `Collapsed` ancestor gating excludes descendants from warm
measure/arrange and future paint/hit discovery until reveal explicitly schedules bounded re-entry work.

### Structural mutation contract

Structural mutation is queued and committed deterministically on one semantic owner thread. Initial
commands are Create, Destroy and Reparent. Create/Reparent use the ordinary fixed command budget. Destroy
does **not** consume a smaller fallible reserve: every admitted node slot owns terminal-destroy
sequence/pending state and each instance owns a fixed sparse terminal-index array sized to its admitted
node capacity. Therefore every distinct live node can have one terminal destroy pending even when the
ordinary command lane is saturated; duplicate destroys coalesce/reject against the slot state. Spawn
cancel/release similarly records terminal intent in the already-admitted spawn slot instead of allocating
another terminal command.

Ordinary and terminal commands carry one runtime-global monotonic sequence. A commit snapshots one
runtime-global sequence fence before applying anything, then uses fixed `maximumInstances` merge scratch
to k-way merge the per-instance ordinary/terminal ring fronts in exact enqueue order. Work enqueued by a
callback during commit receives a sequence beyond the fence and waits for the next explicit checkpoint;
storage class or instance slot order therefore cannot change semantics. Admission validates
instance/generation/capacity. Commit revalidates owner/generation, rejects cycles/cross-instance
references, and leaves authoritative topology unchanged on a failed structural operation. A node
destroyed earlier in the same commit cannot be resurrected/reused by later commands carrying the stale
handle.

Detach is distinct from destroy. Each instance owns a checked fixed `detachedRoots[nodeCapacity]` sparse
set with one per-slot back-index, so detached roots are enumerable without a row scan and swap removal is
O(1). Each live node also carries compact detached-domain evidence; reparent's already-required bounded
subtree traversal updates that evidence and exact detached-node cardinality only when a subtree crosses
between the document-root domain and a detached domain. Parentless runtime Create adds a detached root;
reattach removes it; Destroy and instance release retire detached-root/node counts exactly. The document
root is never a member of the detached-root set.

### Initial configurable bounds

V1 uses explicit configurable limits rather than hidden process growth. Definition locks these as initial
defaults, all overrideable downward/upward within representable/index limits:

- maximum live instances: 64;
- maximum nodes per document: 131,072;
- maximum total live nodes across all instances: 1,048,576;
- maximum hierarchy depth: 1,024;
- maximum cooked property records per document: 524,288;
- maximum queued ordinary Create/Reparent structural commands per instance: 65,536;
- terminal Destroy storage: derived from each instance's admitted node capacity, one pending entry per
  live node slot, not a smaller configurable reserve;
- maximum concurrent spawn operations: 256;
- maximum materialized nodes per runtime update: 4,096.

The decoder also enforces byte/count/offset arithmetic and section-count limits before allocating final
runtime objects. Numeric defaults may be tuned in later V1 gates only from implementation evidence; the
hard-bound/rejection doctrine is not negotiable.

### Runtime lifecycle boundary

The headless kernel exposes the same terminal ownership sequence required by V0:

```text
Active -> Quiescing -> Drained -> Shutdown/Uninitialized
```

`Quiesce()` runs on the semantic owner, stops new admission and begins terminal cancellation/destruction.
`Drain()` is owner-thread progress/evidence: it cannot report Drained while live instances/spawns,
ordinary structural work, terminal Destroy work, resource requests or exact strong handles remain.
`Shutdown()` is legal only from Drained and never shuts down the injected shared ResourceStreamer.
P1-P4 extend Drain's work predicates as those owners are implemented; the public lifecycle state is
locked in P0 so later gates cannot weaken teardown ordering.

### Final cooked document direction

V1 defines one sectioned, versioned binary runtime resource; no JSON/reflection placeholder is allowed.
The first format reuses Vanguard's existing `serialization::DocumentHeader` + `SectionDescriptor`
container rather than inventing a second section-table encoding. V1 locks `VLUM`
(`serialization::MakeFourCC('V','L','U','M')`) as both the document magic and Vellum document
`ResourceTypeId`, matching the existing world/cell/material/pipeline binary-resource convention.
Payload/body versions remain independently versioned inside Vellum sections so later payload evolution
does not require replacing the generic serialization container.

The first format carries:

- `serialization::DocumentHeader` with `VLUM` magic + Vellum file version;
- total byte size and section directory;
- document root record/index;
- ordered node records with stable authored identity, node kind, parent identity/index and property span;
- typed property records using stable numeric property identity/type plus canonical bytes;
- no native pointers, vtables, editor objects or transient runtime slot/generation values.

Decoder validation is atomic: generic document/section validation plus Vellum version,
section bounds/alignment/overlap, count*stride overflow, root,
parent indices, exactly-one-parent/tree reachability, duplicate/invalid authored IDs, node-kind/property
compatibility and configured count limits all pass before an immutable `DocumentResource` becomes
publishable. Unknown incompatible major version returns `UnsupportedVersion`; malformed content returns a
deserialization/integrity-class failure and never publishes a half resource. Vellum retains a finer
internal decode result such as `LimitExceeded` for diagnostics, but exact-current `resources::Failure`
has no limit-specific value; decoder-facing bound/allocation rejection maps to `OutOfMemory` unless the
resource API is separately extended by an authorized owner.

### Spawn/materialization lifecycle

Core `VellumRuntime` (final name may refine in P0) accepts a non-owning injected
`streaming::ResourceStreamer&`. It never constructs a private registry/pipeline/streamer.

Each spawn is unique and bounded even when two spawns request the same document resource:

```text
Queued -> Loading -> Materializing -> ReadyToCommit -> Succeeded
   \          \            \              \
    +----------+------------+---------------+-> Failed / Cancelled
```

The underlying `PipelineRequest` may represent coalesced pipeline work, but each Vellum spawn owns its
own caller interest, materialization state, reservations and final document instance. Loading is polled;
Vellum semantic update never calls `Wait()`.

On loaded terminal state the operation calls `Acquire()`, validates the resource type/generation, and
retains the exact strong handle before releasing request interest. Materialization may be sliced by the
configured node budget and builds off-tree. `ReadyToCommit` performs final spawn-owner/capacity/resource-
generation validation and publishes atomically. Cancellation uses `CancelWithStatus` for pipeline
interest but has a separate Vellum semantic-owner terminal linearization so Cancelled and Succeeded cannot
both win.

### Diagnostics/proof counters designed into V1

Runtime stats expose at least live/capacity/high-water/rejection counts for documents, node slots/dense
rows, mutation queues/reserve use, dirty frontiers, layout visits, spawn states/materialization work and
decoded-resource ownership. Development proof counters include semantic heap allocations during update,
clean-node discovery count, nodes measured/arranged, collapsed descendants skipped, row moves and stale
work rejected. Allocation evidence distinguishes checked document/storage-block allocations from semantic
update work so the V1 proof can show O(chunks/documents) allocation rather than one allocation per node.

These counters are product diagnostics; test-only checkpoint names never enter production identifiers.

## P0 — module and value ABI

**State: CLOSED.** P0 added the real `source/vellum` production module, final-direction public value
handles/property/invalidation/config/failure/stats ABI, `VLUM` document identity, captured semantic-owner
thread, aggregate/per-instance/depth capacities and Active -> Quiescing -> Drained -> Shutdown lifecycle.
Post-fix read-only review: **0 BLOCKER / 0 HIGH / 0 MEDIUM**. No build/test/native execution was used.

P0 creates only final-direction production boundaries:

- root Premake include and `source/vellum/premake5.lua`;
- public `vanguard/vellum` headers for handles, value/layout/property/config/failure/stat types;
- runtime shell with injected ResourceStreamer reference but no EngineHost dependency;
- stable resource-type registration helper surface;
- private implementation boundary ready for dense arenas.

P0 does not implement a renderer adapter or engine service.

## P1 — compact arena and topology

**State: CLOSED.** P1 closes after exact-current adversarial review at **0 BLOCKER / 0 HIGH / 0 MEDIUM**.
The final source uses checked fixed-capacity instance/node/command/terminal/merge/detached storage,
semantic-update quarantine before generation reuse, permanent generation exhaustion, runtime-global
mutation ordering/fencing, explicit detached-root sparse ownership/cardinality and iterative
Create/Destroy/Reparent validation/commit. No build/test/native execution was used for P1 closure.

P1 implements instance slots, dense node rows/side stores, generation exhaustion, deferred reuse,
intrusive ordered topology, Create/Destroy/Reparent queue/commit, cycle/cross-instance/stale rejection,
per-slot non-failing terminal destroy storage, explicit detached-root sparse ownership/cardinality and
iterative traversal primitives.

Reparent validation is fully pre-commit/atomic. It validates the new-parent ancestor chain for cycle/base
depth **and** iteratively measures the maximum relative depth of the entire moved subtree using fixed
node-capacity scratch. Only if `newParentDepth + movedSubtreeDepth` is within
`maximumHierarchyDepth` may intrusive links be rewired; failure leaves old/new topology untouched.

## P2 — typed properties, invalidation and layout

**State: CLOSED.** P2 closes after final exact-current adversarial review at
**0 BLOCKER / 0 HIGH / 0 MEDIUM**. Stale/retired dirty WorkRefs compact before deferred generation
promotion; only Measure/Arrange physically allocate node-capacity frontiers in V1 while future domains
remain compact per-slot evidence. Gated work demotes to dormant evidence, reveal/reparent schedules bounded
repair, and stable collapsed/detached subtrees retain zero live layout WorkRefs. No build/test/native
execution was used for P2 closure.

P2 implements compact property resolution/write, semantic equality, dirty-domain frontiers, ancestor
gating, StackPanel/Spacer measure-arrange, committed geometry, collapsed-subtree suppression/reveal and
cardinality counters. No rendering is introduced.

## P3 — cooked format and decoder

**State: CLOSED.** P3 adds only the immutable document resource, final `VLUM` binary wire contract and
normal injected-streamer decoder ownership required by V1. It does not introduce a private
registry/pipeline/streamer or start spawn/materialization early.

P3 locks one canonical `TREE` section over Vanguard's existing 40-byte `DocumentHeader` and 48-byte
`SectionDescriptor`, with explicit wire node/property/enum codes independent of runtime enum ordinals.
Decode validates the complete file/section/body/tree/property contract before publishing one immutable
`DocumentResource` backed by a checked `PoolId::Resources` block. Runtime initialization registers a normal
`streaming::DecoderDescriptor` as its last fallible step and shutdown keeps callback state alive through a
retryable explicit unregister. The shared streamer/pipeline unregister seam was strengthened so successful
per-type unregister proves both active `StreamLoad` state and every copied `PipelineOperation` loader
descriptor are gone before callback/userData state may be released. Final exact-current format and lifetime
audits report **0 BLOCKER / 0 HIGH / 0 MEDIUM**. No build/test/native execution was used for P3 closure.

## P4 — spawn/materialization

**State: CLOSED.** P4 implements fixed-capacity unique spawn operations over the injected
`ResourceStreamer`, exact strong-generation pinning before request-interest release, sliced direct
materialization into `ReservedCreate` instances, O(1) atomic publication, bounded weighted priority
service, off-thread cancel/release intent and one semantic-owner Cancelled-vs-Succeeded commit
linearization. Spawn slots use deferred generation quarantine/permanent exhaustion; terminal caller
interest remains counted until release/Take, abandoned successful results retire their instance, and
`TakeSpawnResult` is the explicit ownership transfer. Quiesce cancels active spawn work and Drain cannot
complete while caller interest, instances, requests or exact strong document generations remain. Final
exact-current integration and staging audits report **0 BLOCKER / 0 HIGH / 0 MEDIUM**. No
build/test/native execution was used for P4 closure.

## P5 — hardening and proof instrumentation

**State: ACTIVE — reopened by final audit.**

P5 closes non-build correctness gaps in source: generation exhaustion/quarantine, conflict ordering,
capacity rejection/terminal progress, iterative deep hierarchy, 100k-class storage behavior, collapsed
subtree gating, corrupt cooked-data atomic failure, shared-load/independent-spawn semantics and zero-leak
teardown instrumentation. Focused tests may be authored here but are not built/run until P6 unless the
new-subsystem viability exception is explicitly invoked and recorded.

The final focused `vellumTests` source covers permanent generation exhaustion and deferred reuse, runtime-
global mutation sequencing, multiple terminal destroys under ordinary-lane saturation, topology/cycle
atomicity, typed invalidation, collapsed warm gating/reveal, 1,024-level iterative hierarchy traversal,
literal TREE CRC/version/schema/count/topology failures, 100k flat cardinality with exactly three Runtime
instance-storage allocations, shared-load/independent-spawn cancellation, exact replacement-generation
pinning, off-thread `SpawnRequest::Reset` intent publication, zero ownership gauges and retryable decoder
unregister with external resource/pipeline ownership. The P5 source audit found and fixed invalid test
configurations where `maximumMaterializedNodesPerUpdate` exceeded a reduced node cap. A later independent
pre-execution audit found four HIGH proof gaps and one MEDIUM fixture gap: clean-node discovery had no
production counter update, the warm 100k proof observed only outstanding rather than submitted jobs, the
shared-load witness used timing-sensitive coalescing statistics, cycle rejection snapshotted too little
topology, and two private fixture configs still violated the materialization-budget bound. All five were
closed before P6 execution. No P5 build/test/native execution or profiling was run.

## P6 — final V1 closure

**State: CLOSED.**

P6 is the focused build/proof gate. It must:

1. generate/update VS2022 projects with Premake if the new module requires it;
2. build the smallest Vellum production/test targets with serialized outer MSBuild `/m:1`;
3. run only the focused Vellum runtime-kernel proof executable(s);
4. prove stale handles/generation exhaustion, topology conflicts, terminal progress under saturation,
   compact allocation/cardinality shape, zero warm clean-node discovery/semantic allocations/jobs,
   collapsed-subtree zero-work then bounded reveal, typed invalidation, deep iterative traversal,
   corrupt decode failure, shared underlying load with independent spawns, cancellation/exact strong
   handle lifetime and zero teardown gauges;
5. perform final adversarial source/test audit and static hygiene;
6. close V1 without starting V2.

The focused Development/x64 `vellumTests` target was built with serialized outer MSBuild
`/m:1` at **0 warnings / 0 errors**, and `build/output/Development/vellumTests.exe` passed the complete
V1 runtime-kernel proof suite. The final proof includes an exact one-load witness for two independent
Vellum spawn interests (`ResourceStreamer::Stats::completedLoads + 1`), unchanged submitted-job count and
clean-node-discovery count on the 100k warm path, full cycle-topology atomicity, the 1,024-level iterative
hierarchy limit, literal cooked-format rejection cases, exact strong-generation replacement pinning and
retryable decoder unregister/teardown ownership.

Canonical root Premake project generation is currently pre-empted by the repository-global engine-code
audit, which reports 17 exact-current violations in unrelated Engine/Rendering/RHI/TextureTools work. P6
therefore used a temporary ignored `build/p6_vellum` wrapper that suppresses only that audit command and
then loads the repository's real `premake5.lua`; no source policy/allowlist was changed and generated
projects remained under `build/projects/vs2022`. The focused compile exposed and fixed Vellum's missing
Crypto include-path dependency inherited through Streaming's public resource-source header, plus
test-only RED/Vanguard `serialization`/`io` namespace qualification. One transient `streaming.lib` output
access failure disappeared on the targeted serialized retry. A final independent audit then found one real
teardown BLOCKER: a published resource could be detached into a registry-local pending-destruction callback,
become invisible to loader/decoder unregister scans, and execute copied Vellum callback/userData after a
successful unregister.

That race is closed in `ResourceRegistry`: every detached destruction is armed under the registry lock into
an atomic `pendingDestructions` lifetime witness before the entry can become unregisterable, and the witness
is released only after the destroy callback and retained request release have returned. Per-type loader
unregister and registry-wide shutdown both refuse while any detached destruction is pending. The deterministic
full-stack regression drops pipeline interest first, keeps the exact strong generation alive, blocks the
decoder destroy callback after the registry entry is already `Unloaded`, proves unregister remains retryably
busy with no retained pipeline operation, and then proves unregister succeeds only after the callback returns.
The proof also covers all six registry detach sites by exact-current source audit.

After the blocker fix, the project was regenerated from the canonical Premake definitions through the same
build-only wrapper, the redundant direct production Crypto link edge was removed while retaining the required
Crypto include path, and `vellumTests` Development/x64 rebuilt through outer MSBuild `/m:1` at **0 warnings /
0 errors**. `build/output/Development/vellumTests.exe` passed again. Final exact-current local adversarial
review and scoped static hygiene are **0 BLOCKER / 0 HIGH / 0 MEDIUM**. No profiling was run. V1 is CLOSED;
V2 remains unauthorized.

## V1 non-goals

V1 must not add or fake:

- `vellumRendering`, GPU/display-list execution or native Present ownership;
- text shaping/glyph rendering;
- pointer/focus/navigation/IME implementation;
- style/theme/animation runtime beyond the property/invalidation extension ABI;
- data views/virtualization;
- world/projected/offscreen surfaces;
- accessibility OS bridge;
- Editor authoring/preview UI;
- a second resource loader/depot/registry;
- per-node heap/shared ownership, per-node jobs, runtime property strings/reflection traversal or whole-tree
  unchanged-frame discovery.

V2 remains unauthorized until the user explicitly starts it.
