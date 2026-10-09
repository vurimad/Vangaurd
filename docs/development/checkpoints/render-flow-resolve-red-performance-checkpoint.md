# RenderFlow Resolve RED-aligned performance checkpoint

Status: **items 1-10 are closed and focused-validated; item 11 is next but not authorized.**

This checkpoint preserves the user-directed optimization sequence for the current
RenderFlow/Resolve CPU investigation so it survives chat compaction and thread handoff.
The reference implementation is RED at:

`D:\root\R6.Root\Mainline\dev\src`

The standing rule is: **make Vanguard 1:1 with RED in this area unless Vanguard has
stronger required semantics or a measured faster design.** Performance is the priority;
do not preserve avoidable scans, allocations, hashing, remapping or bookkeeping just
because they already exist. Do not weaken correctness merely to resemble RED.

Work the items below **one at a time**, in order. Each completed item should keep the
smallest focused correctness proof needed and, where the change is performance-sensitive,
use matched measurement before claiming a win.

## Ordered changes

1. **CLOSED — move operation counting to recording time.**
   RED records useful cardinalities while requests are being produced; Vanguard previously
   rediscovered some of them afterward. `CandidateWriterBatch` now records exact
   declaration, `UseBegin`, compiled-step, explicit-scope-open, decision and export counts
   in the single `PushOperation()` append path. Resolve Setup consumes those counts directly
   and the extra full `batch.operations` sizing pre-scan is gone. Existing
   `resourceCount`, `textureViews.Size()` and `bufferViews.Size()` remain the exact O(1)
   resource/view cardinalities rather than duplicating counters that could drift.

2. **CLOSED — make planning storage persistent and reusable like RED.**
   `WriterReservation` now permanently owns its per-flow-group `CandidateWriterBatch`.
   Frame publication/cancellation clears logical contents in place rather than destroying
   the batch, so reusable resource/view/planning storage keeps useful backing capacity. Seal
   publishes ordered flow-group indices rather than moving batch
   ownership twice. Writer control blocks come from allocator-lifetime pooled storage on
   the normal path, with a heap fallback only when an old-generation handle deliberately
   outlives cancellation and occupies a pool lease. Vanguard's configurable limits and
   stale-generation/failure semantics are preserved. Item 7 later hardens request/cold-payload
   retention with a RED-shaped per-writer hard cap plus the existing aggregate frame cap;
   Vanguard still reports graceful capacity failure rather than copying RED's fatal overflow.

3. **CLOSED — build a per-allocation use timeline once.**
   `CompileOperations` now links every `ActiveUseRecord` into one immutable forward
   allocation-local sequence and records direct first-use and latest-UseEnd endpoints.
   Later queue validation, imports, exports, placed alias destinations and predecessors use
   those resource-local facts instead of rescanning the global use array. The mutable
   simultaneous-use chain remains separate. This follows RED's reduce-once/use-local-
   lifetime principle without copying its centralized global sort.

4. **CLOSED — use `PhysicalResourceId.index` as the authoritative scope-state identity.**
   `CompileScopeEntryStates` now carries the already-known dense physical identity through
   gather, replay and history lookup. The generation-wide native-ref -> physical-index hash
   and packet-local binary-search replay are gone. One native `ResourceRef` is materialized
   when a physical is first admitted to a packet's tracked table; that table is sorted by
   native ref only because the RHI entry-state ABI requires unique ResourceRef ordering.
   Aliased predecessors retain their native execution refs plus direct physical-index
   compile metadata, so state compilation never reverse-discovers allocation identity.

5. **CLOSED — reuse Resolve scratch instead of allocating packet scratch repeatedly.**
   The hot Resolve-only working set now lives in allocator-owned `ResolveScratch`, with
   aggregate flat arenas for batch resource/view/use mappings and physical state cells,
   O(1)-reset generation-stamped associative scratch, and reusable placed-range planner
   working memory. Retention is bounded by aggregate frame demand rather than historical
   transient batch/physical indices. Published execution-generation data remains
   generation-owned because it may outlive Resolve.

6. **CLOSED after radical reopen/correction — precompute named-resource hashes before allocator recording.**
   RED carries a compact prehashed tag and compile-time prehashes static names. Vanguard now
   carries a name prehash through `LogicalResourceName`/`LogicalResourceKey`, derives the
   flow-space composite hash in O(1), and retains that composite in `CandidateResource`.
   Resolve consumes the retained hash without storing it twice in each `namedSlots` bucket.
   Full string + flow-space equality remains the collision check, preserving stronger
   collision correctness than RED's hash-only identity.

7. **CLOSED after radical reopen/correction — compact and hard-bound the candidate request tape.**
   RED overlaps mutually exclusive request operands in unions and gives every parallel group a
   fixed request capacity independent from its aggregate request-index limit. Vanguard now records
   a **32-byte** `CandidateOperation`, keeps ordinary texture-use state/subresource/access/content
   inline, and spills only declaration descriptors or non-None texture clear values into one
   **88-byte tagged cold-payload union tape**. Each operation contributes at most one cold entry.
   A separate `maximumOperationsPerPlanningWriter = 768` hard bound sits under the existing
   aggregate `maximumOperations = 262,144` gate; dynamic growth is clamped to the local bound,
   rejected/canceled recording releases its request backing, and inactive reservation tails are
   released when a later graph uses fewer groups. Tape position and batch context still reconstruct
   public IDs. At item 7 closure, item 8 queue-order work remained untouched; item 8 closes separately below.

8. **CLOSED after radical reopen/correction — precompute command-scope and queue ordering facts.**
   RED makes order cheap by encoding `(group, request-within-group)` directly into request
   timestamps, sorting its request keys once, and separately sorting the small async-compute
   fork/join timestamp stream. Vanguard keeps its stronger explicit executable dependency
   semantics, but no longer re-proves them by rescanning graph metadata. Previously each
   cross-queue `QueueOrderProved()` could scan all `S` command scopes, then rescan the scopes
   for every matching queue dependency: **O(S + D*S) per proof**, repeated by explicit-state
   handoff, adjacent cross-queue use validation and dedicated-resource reuse selection.

   The first closure already made the hot proof O(1), but the user-requested radical re-audit
   found more linear setup/storage work to remove. The final shape consumes the prepared
   flow-group stream and the already-sorted sealed-writer stream in **one ascending merge**.
   That single walk validates queue groups, constructs packets/scopes, lowers each pending
   Fork/Join into the published `CompiledQueueDependency`, updates the corresponding running
   producer watermark, and emits the Resolve-only order fact for the consumer. The old
   `batchPackets` identity array, `queueGroupsUsed` reconciliation hash set,
   `syncRequestGroups` staging list, two-channel `CommandScopeOrderFact`, and separate scope
   prefix pass are all gone.

   Construction now enforces **sealed batch index == packet index == dense command-scope
   index**. Dense index order is order-isomorphic with the published stable order because
   `SealPlanning()` collects writers in ascending flow-group order. A consumer can only use
   one cross-queue primitive: Compute consumers can require Fork, Graphics consumers can
   require Join, and Copy has no supported Graphics/Compute dependency. Resolve therefore
   stores only one `u32` maximum producer scope index per consumer. The fact table is also
   **lazy**: on a frame with no real Fork/Join its logical size stays zero and a fresh scratch
   allocator performs no fact allocation or per-packet fact writes. The first real boundary
   reserves the known packet count, fills the earlier prefix with Invalid, and then appends one
   4-byte fact per remaining scope. A frame that uses async compute therefore has at most
   `4*S` active fact bytes (**16 KiB at 4096 packets**, down from the first closure's 32 KiB).
   Reusable high-water backing is intentionally retained after a prior async frame, consistent
   with item 5; later no-async frames still perform no fact writes.

   `QueueOrderProved()` remains **O(1)**. It bounds the dense scope indices, returns immediately
   for the same queue, rejects unsupported queue pairs, and only then reads the lazy consumer
   watermark and compares `producerScopeIndex <= admittedProducerScopeIndex`. This is exactly
   the old existential predicate under the dense/stable order isomorphism: among all matching
   boundaries whose consumer is no later than the queried consumer, only the greatest producer
   index can matter. Repeated balanced non-nested Fork/Join regions therefore retain the same
   monotonic witness semantics without carrying both boundary types in every scope.

   Duplicate public command-scope validation is now lazy too. Production
   `RenderNodeGraph` authors `CommandScopeId{nodeId}`, so the already-required
   `packetByNode` insertion proves both node and command-scope uniqueness on that canonical
   path; no second scope hash/probe stream or scope-set allocation is paid. On the first caller
   that supplies an independent scope ID, Resolve reserves the fallback set, backfills the
   already-proven canonical prefix once, and thereafter validates every scope normally. The
   fallback set itself is a specialized generation-stamped `u32` set containing only keys and
   stamps, not the old generic map's unused value and cached-hash fields. At `S=4096` its
   fallback bucket backing is 8192 keys + 8192 stamps = **64 KiB**. Reset remains O(1).

   Because final packet cardinality is known before construction, the fresh published
   `packets`, `commandScopes`, and `packetByNode` containers now reserve exactly `S` once rather
   than growing/reallocating/rehashing incrementally. The configured execution-packet ceiling
   is checked **before** `ResolveScratch::ResetFrame()` or any `S`-sized reserve, so a caller
   with a deliberately smaller `maximumExecutionPackets` cannot inflate retained #8 scratch
   before being rejected. Published `CompiledCommandScope` and `CompiledQueueDependency` truth
   remains unchanged for execution and terminal-receipt validation. Logical allocation
   first/last use and whole-frame reuse lanes continue to carry the already-known dense scope
   index directly.

   Focused regression coverage includes two sequential Graphics -> Compute -> Graphics
   regions (`Fork/Join/Fork/Join`) with one resource crossing all five command scopes; Resolve
   succeeds only if the later dense Fork/Join watermarks advance correctly. A new fallback
   regression uses two distinct nodes where the first canonical scope collides with a later
   noncanonical scope and proves lazy scope-index materialization still rejects the duplicate.
   The no-sync cross-queue rejection test also exercises the zero-fact lazy path. Fresh final
   Development `renderingTests` and `geometryAllocatorTests` both rebuilt with `/m:1` and pass,
   including real graphics/compute fork-join, dedicated-resource reuse, explicit subresource
   state, placed-resource, retained-import and terminal-export proofs. Two independent fresh
   exact-current source-static closure audits both report **0 BLOCKER / 0 HIGH / 0 MEDIUM**,
   including dense/stable-order equivalence, lazy-fact indexing, canonical/fallback scope-ID
   uniqueness, failure/reset safety, exact async fact-write accounting and #9 isolation.
   `git diff --check` is clean apart from the repository's existing LF/CRLF working-copy
   warnings. Item 9 transition coalescing remains untouched. These are structural pass/storage
   results only; no frame-time, microsecond or percentage speedup is claimed without matched
   timing evidence.

9. **CLOSED — coalesce texture state transitions.**
   RED keeps exact resource/subresource state, suppresses old-state == new-state barriers,
   and batches its pending DX12 barriers instead of treating every recorded state touch as
   independently executable work. Vanguard keeps its stronger explicit logical/physical
   state compilation, but now applies the same structural rule at the compiled-action edge:
   preserve exact per-cell truth, then publish only representable transition rectangles.

   All five Vanguard texture-transition producer families are covered: ordinary texture-use
   before-actions, retained-import terminal restoration, terminal exports, explicit
   cross-queue predecessor exit-to-Common actions, and final explicit texture exit actions.
   Compatible actions require the same logical/physical identity, exact before/after state
   (except `Unknown` as a native provisional wildcard), and an exact adjacent rectangular
   mip/slice union. Merged counts are widened to `u32`, finite compiled ranges never synthesize
   the public `0xffff` remaining-subresources sentinel, and concrete before == after actions
   are suppressed.

   Native ordinary/import/export compilation no longer materializes one provisional action
   per cell. It records **one uncharged range intent per texture action block** when any cell
   logically needs the target state while still walking/updating every logical cell exactly as
   before. `CompileScopeEntryStates()` then replays that intent against the exact physical
   entry-state cells, drops concrete no-ops, splits heterogeneous before-states, coalesces the
   surviving exact rectangles, rewrites the owning step's action offset/count, and swaps the
   rebuilt stream from reusable allocator-owned `ResolveScratch::scopeActions`. Source action
   slices remain immutable until the final swap, so nonmonotonic source offsets and execution
   order are preserved. No scratch pointer/view is published.

   Rectangle folding is block-bounded: it never crosses a use, UAV barrier, typed clear,
   alias activation/finalization, packet, or terminal-action boundary. The fast online tail
   fold handles the common homogeneous/run-shaped case. A transition-only block finalizer
   groups compatible residual rectangles in place and compacts them before capacity is charged;
   this closes alternating heterogeneous row patterns such as `A,B / A,B` that a tail-only
   fold cannot combine vertically. The finalizer uses in-place `std::sort` only when more than
   one residual transition remains, so the common already-collapsed block pays no sort.

   `maximumCompiledResourceActions` now bounds **final executable cardinality**, not transient
   row-builder/provisional cardinality. Logical-only transition blocks are charged only after
   final coalescing. Native provisional texture intents are uncharged; exact replay charges only
   final concrete texture rectangles. Cross-queue and final explicit texture exit blocks are
   likewise charged from their final merged block size. Buffer/UAV/clear/Present actions and
   alias synthetic actions retain their existing one-time charging. This prevents false
   `CapacityExceeded` failures while preserving the hard published-action bound.

   Focused regression coverage now proves a logical **2 mip x 2 slice** homogeneous rectangle
   succeeds with `maximumCompiledResourceActions = 1`, while two independent texture resources
   still fail under that cap. A native D3D12 explicit-state proof creates heterogeneous
   predecessor states across a 2x2 texture, consumes the combined range in the next packet,
   and verifies exact split/re-coalesced execution plus final restoration under a tight action
   budget. Existing typed-clear, placed-resource, retained-import, terminal-export and
   Graphics/Compute fork-join tests remain green.

   Fresh exact-current Development `renderingTests` and `geometryAllocatorTests` both rebuild
   with `/m:1` and pass. Multiple independent read-only closure audits report **0 BLOCKER /
   0 HIGH / 0 MEDIUM** on final executable-cardinality accounting, heterogeneous-before replay,
   action-slice rewriting, rectangle validity, sentinel/u16 safety, scratch ownership,
   alias/UAV/clear/Present ordering, terminal semantics and #10 isolation. `git diff --check`
   is clean apart from the repository's existing LF/CRLF working-copy notices. These are
   structural/action-cardinality results only; no microsecond, frame-time or percentage
   speedup is claimed without matched profiling evidence.

10. **CLOSED — collapse `SealPlanning()` into fewer passes.**
    RED records useful request cardinality/order metadata while requests are appended to each
    parallel-group tape, then Resolve consumes those prepared facts instead of rediscovering
    frame-wide sizing from the tapes. Vanguard now follows that shape without weakening its
    stronger writer/queue validation. Queue-request and nonempty-queue-group cardinalities are
    recorded with atomic counters at successful request publication, matching the existing
    parallel resource-preparation ownership; Seal reads them only after the planning join.

    Before item 10, pre-operation metadata work was structurally **4*G in Seal + 2*W in
    Resolve setup + G in Resolve queue/packet lowering** (`G` = prepared flow-group capacity,
    `W` = sealed writers). `SealPlanning()` now performs one ascending **G** walk that validates
    closed/unpoisoned writer identity, proves node uniqueness, gathers resource/view/operation/
    use/scope/export cardinalities, emits the stable writer stream, records four flat-arena
    prefix offsets per writer, and preserves only nonempty queue groups. Aggregate and prefix
    arithmetic remains `u64` through admission; the existing `u32` hard caps prove committed
    summary/offset values representable. Rejected sealing clears only derived metadata and keeps
    candidate tapes intact, preserving retry/cancellation semantics and validation precedence.

    The derived writer record is now exactly **20 bytes** (`groupIndex` plus resource-slot,
    texture-view, buffer-view and use-record offsets; compile-time asserted). The old shape kept
    a 4-byte sealed group index plus a 32-byte Resolve `BatchResolveMap`, or **36 bytes/W** of
    equivalent derived metadata. The new logical payload is **20 bytes/W + 4 bytes/Q** for the
    compact nonempty queue-group stream (`Q` = nonempty queue groups), plus one constant summary.
    Writer/node scratch reserves use the exact recorded writer count and queue storage reserves
    the exact atomic `Q`, rather than the often-4096 prepared capacity. Seal node uniqueness and
    Resolve's lazy noncanonical command-scope uniqueness also reuse the same generation-stamped
    identity set serially; `ResolveScratch::ResetFrame()` resets it between the two roles, so a
    second ~64 KiB max-writer hash high-water is not retained.

    Resolve no longer rescans all sealed writers to total cardinalities and no longer constructs
    or stores `BatchResolveMap`. `SealedPlanningSummary` sizes flat arenas/reserves directly, and
    immutable batch sizes provide the counts paired with each preserved prefix. Queue/packet
    lowering no longer walks every prepared group either: it merge-consumes the sorted writer and
    nonempty-queue streams in **W+Q**, preserving queue-only synchronization groups, Begin/End
    ownership, Fork/Join ordering, and the item-8 invariant **sealed writer index == packet index
    == dense command-scope index**. Queue-shape validation and published `packetByNode` ownership
    correctly remain Resolve/execution-generation responsibilities.

    Focused regression coverage now proves duplicate node IDs are rejected by the single seal
    pass and that the shared stamped identity set is reset before noncanonical command-scope
    fallback reuse. Another focused cap proof verifies two queue requests plus one writer
    operation still exceed `maximumOperations = 2`, preserving the old aggregate accounting.
    Existing sparse Graphics/Compute Fork/Join tests exercise queue-only groups in the compact
    W+Q merge. Fresh Development `renderingTests` and `geometryAllocatorTests` both rebuild with
    serialized `/m:1` and pass on the final atomic-counter source, including #8 queue ordering,
    #9 explicit/coalesced texture-state execution, placed resources, retained imports and terminal
    exports. A fresh independent post-atomic read-only closure audit reports **0 BLOCKER / 0 HIGH /
    0 MEDIUM** across queue-counter concurrency, one-pass sealing, W+Q merge semantics, u64
    narrowing, failure/reset safety, #8/#9 preservation and #11 isolation. Scoped
    `git diff --check` is clean apart from normal LF/CRLF working-copy notices. Item 11's
    duplicate-export/allocation/physical-binding search work remains untouched. These are
    structural pass/storage results only; no microsecond, frame-time or percentage speedup is
    claimed without matched profiling evidence.

11. **Remove small quadratic metadata searches.**
    **Closed 2026-10-03.** A fresh RED-first re-study confirmed the useful pattern is to
    assign compact request/allocation identities once, carry direct tag/allocation facts
    forward, and hash only genuinely sparse external identities. Vanguard now follows that
    rule across the remaining metadata rediscovery paths without changing the placed
    allocator, subresource-state algorithms, or descriptor finalization.

    Resolve no longer performs O(E^2) terminal export-slot checks, O(E^2) final-allocation
    publication checks, or O(P*E) physical/export rediscovery. Export slots and physical
    bindings use phase-reused dense sentinel tables; final logical-allocation duplication uses
    the existing `LogicalAllocationRecord::exported` sentinel directly; and
    `PendingExportRecord` carries the terminal dense packet index instead of forcing a later
    command-scope search. The remaining pending-export loops are single O(E) application
    passes, not relationship searches.

    Finish treats public command/dependency receipts as unordered evidence, indexes them once,
    and thereafter uses direct dense scope/dependency indices. The old repeated S^2/D^2
    cross-matching, repeated lowering/predecessor/export receipt scans, and linear scope-order
    lookups are gone. Submitted Graphics/Compute ordering is rebuilt as the same monotonic
    Fork/Join producer watermark used by item 8; Resolve emits at most one queue dependency per
    consumer packet. Each compiled dependency's producer stable ID is resolved once into
    allocator scratch and reused for lowering validation and submitted-order proof. Decision
    IDs likewise carry dense decision ordinals, so execution no longer linearly searches packet
    decisions.

    Session retained-import registration now uses generation-stamped sparse token/native-
    resource/presentation indexes instead of O(I^2) scans. Persistent terminal publications use
    an exact 64-bit `{generation,index}` slot index plus swap-removal, replacing repeated linear
    `TakeExport` scans/order-preserving removal. Publication staging is transactional: index
    entries are rolled back on duplicate/insert/ownership-transfer failure, and `TakeExport`
    validates and rewrites index state before moving an RHI owner. Normal BeginFrame preserves
    outstanding publications across generations; teardown/device-loss paths clear vector and
    index together.

    Storage is deliberately bounded outside the published execution generation. Resolve/Finish
    relationships reuse existing scratch high-water arrays wherever their prior phase is dead;
    the final dependency correction adds only one scratch `u32` per compiled queue dependency.
    Retained-import hashes are session scratch, while the one necessary persistent structure is
    the outstanding-export hash (roughly <=0.5 load, keyed by the exact 64-bit slot identity).

    Final Development x64 validation rebuilt `renderingTests.vcxproj` and
    `geometryAllocatorTests.vcxproj` with serialized `/m:1`; both executables pass. Focused
    proofs now include unordered queue-dependency receipts, duplicate command/dependency receipt
    rejection, retained-import idempotence/native-identity conflict, non-tail export
    swap-removal, an outstanding publication surviving an aborted later frame, and simultaneous
    same numeric slot indices from different generations taken independently. Two independent
    final post-implementation read-only audits report **0 BLOCKER / 0 HIGH / 0 MEDIUM**; one
    earlier pass had identified the redundant allocation fill and repeated dependency-producer
    hashing, both corrected before that final result. Scoped `git diff --check` is clean apart from normal
    LF/CRLF working-copy notices. These are structural complexity/storage results only; no
    microsecond, frame-time, or percentage speedup is claimed without matched profiling.

12. **Revisit descriptor finalization only after the structural work.**
    **Closed 2026-10-03.** The previous full native-descriptor cache was not restored. A fresh
    RED-first re-study showed that RED's render flow carries a compact allocator descriptor
    contract (`SRenderFlowTargetDesc`) through requests/allocations and performs no
    `GetTextureDesc`/`GetBufferDesc` rediscovery in render-flow finalization. RED caches only
    genuinely expensive derived data such as texture-aliasing metadata, rather than cloning
    every native descriptor.

    Vanguard now follows that narrower rule. `PhysicalBindingRecord` carries only the physical
    usage class required to decide generic SRV/UAV publication: the existing 4-byte texture
    usage slot is a texture/buffer usage union, populated from the canonical physical assignment.
    A single `wholeResourceDescriptorUse` bit is ORed on the physical binding during the already
    required compiled-step physical fixup whenever a non-explicit-view use exists. The bit uses
    padding that already existed before the descriptor handles; `PhysicalBindingRecord` remains
    **68 bytes**, compile-time asserted.

    `FinalizeDescriptors` therefore performs **zero native texture/buffer descriptor queries**.
    Retained imports remain skipped as before. Physical bindings used only through explicit
    views no longer allocate/write unreachable whole-resource descriptors, because the public
    `ResolvedTextureUse`/`ResolvedBufferUse` generic descriptor getters already return invalid
    for explicit-view uses. Any physical binding with at least one whole-resource use preserves
    the old API contract and still publishes every physically-supported generic SRV/UAV handle;
    item 12 deliberately does not narrow descriptor availability by individual use state.

    Buffer binding class (Structured/Raw/Typed) and SRV/UAV capability come from the cached
    physical usage. Buffer descriptor writes use `BufferViewDesc{}` so the RHI resource payload
    remains authoritative for actual format, stride and full physical range. This is especially
    important for dedicated-pool buffer reuse where the physical capacity may exceed the current
    logical request. Placed-resource usage remains exact, and retained-import registration plus
    dedicated/placed creation validation keep their existing ownership-boundary authoritative
    descriptor checks.

    The remaining Resolve `GetTextureDesc`/`GetBufferDesc` calls are only the one full
    authoritative query per terminal export. That query intentionally remains: it feeds the
    public `PublishedResourceExport` descriptor contract and validates native
    `keepInitialState`/terminal-state semantics. Removing it would require the broad physical
    descriptor cache that item 12 explicitly avoids.

    Focused D3D12-native coverage creates a resource descriptor domain with capacity exactly
    four and compiles four distinct physical resources: whole-resource and explicit-view-only
    buffers plus whole-resource and explicit-view-only textures, with overlapping lifetimes.
    Resolve succeeds with exactly four populated descriptors (one SRV+UAV pair for each whole
    resource), the whole-resource uses expose both handles, and both explicit-view uses expose
    neither generic handle. Under the old unconditional publication shape the same proof would
    require eight descriptor slots. Final Development x64 `renderingTests` and
    `geometryAllocatorTests` builds use serialized `/m:1` and both executables pass. A discarded
    overlapping validation attempt hit only a shared `nvToolsExt64_1.dll` copy lock; the required
    serialized geometry rebuild immediately afterward passed and is the recorded evidence.

    Descriptor allocation/write failures still unwind through `FailResolve` and
    `ExecutionGenerationRef::Impl` retirement of every already-allocated/populated descriptor.
    Independent RED-first and exact-current Vanguard read-only audits both report
    **0 BLOCKER / 0 HIGH / 0 MEDIUM**. Scoped `git diff --check` is clean apart from normal
    LF/CRLF working-copy notices. These are structural query/publication-count results only;
    **no microsecond, frame-time or percentage speedup is claimed without matched profiling**.

## RED behavior intentionally not copied by default

- RED's centralized atomic request collection plus global sorting is not automatically
  better than Vanguard's writer-local tapes. Keep Vanguard's local recording unless
  matched measurement proves RED's ordering model faster for this implementation.
- RED pushes more state tracking into its GPU API. Vanguard deliberately has stronger
  explicit state/queue compilation. Keep that architecture and make it cheaper instead
  of weakening semantics merely to look like RED.

## Current next action

**Items 1-12 are closed.** No further render-flow performance item is authorized by this
checkpoint. Any additional optimization should begin from a fresh matched profile rather than
expanding the descriptor cache speculatively.

## Item 1 closure evidence — 2026-10-02

- `CandidateWriterBatch` owns exact `declarationCount`, `useBeginCount`, `stepCount`,
  `explicitScopeOpenCount`, `decisionCount` and `exportCount`. `PushOperation()` captures
  the accepted operation kind, appends once, then updates the corresponding counters;
  rejected appends therefore never inflate cardinalities.
- Resolve Setup no longer walks each sealed `batch.operations` merely to rediscover
  `UseBegin`/step counts. Its existing O(writer-batches) setup loop aggregates only the
  global counts that are actually needed.
- Exact counts now pre-size `scratch->allocations`, allocation logical-state storage,
  named-scope state, global active-use records, explicit scope records/indexing,
  pending exports, per-packet compiled steps, decisions and liveness. The action reserve
  remains only a `UseBegin`-count seed because action count is state/subresource dependent.
- No recording-time action count or texture-subresource-cell count was invented; those
  values are not authoritative until Resolve has state and descriptor information.
- Development-only (`VG_ENABLE_ASSERTS`) cardinality consistency checks compare recorded
  counts to compiled packet/frame output. Shipping does not pay those verification branches.
- Counter lifetime is move-safe through writer close/seal. Successful sealing already
  bounds aggregate operations in `u64` against the allocator's `u32 maximumOperations`,
  so the per-batch and Resolve aggregate `u32` cardinalities cannot wrap on an accepted
  frame.
- Focused Development x64 validation passed after the final implementation:
  `renderingTests.vcxproj` and `geometryAllocatorTests.vcxproj` build successfully with
  serialized `/m:1`; `renderingTests.exe` and `geometryAllocatorTests.exe` both pass.
  The rendering library still emits the pre-existing duplicate-definition LNK4006 warnings;
  they are unrelated to item 1.
- Scoped `git diff --check` is clean apart from normal LF/CRLF working-copy warnings.
- No Runtime/Editor/native launch and no post-change Tracy capture was performed. The
  structural result is removal of the full Resolve sizing scan plus exact pre-sizing of
  several outputs; **no frame-time improvement is claimed without matched measurement**.

## Item 2 closure evidence — 2026-10-02

- Actual RED source was inspected under
  `D:\root\R6.Root\Mainline\dev\src\common\renderer\src\renderFlowInternalData.{h,cpp}`.
  RED keeps hot request backing physically stable (`SParallelGroup` and its inline request
  arrays) and cleanup resets logical counts/contents instead of reallocating request tapes.
  Vanguard adopts that reuse principle without copying RED's historical fixed capacities.
- `WriterReservation` is now high-water allocator storage. `CandidateWriterBatch` carries a
  separate logical `resourceCount`; frame reset drops that count to zero without destroying
  any high-water `CandidateResource` object. A reclaimed slot calls `CandidateResource::Reset`,
  whose `name.Clear()` preserves long-string backing. `textureViews`, `bufferViews`, and
  `operations` still clear logically while retaining their outer array capacities.
- `SealPlanning()` no longer moves each batch into another owning array. It emits compact
  ordered `sealedWriterGroups` indices after the existing validation/cardinality passes;
  Resolve dereferences the persistent reservation batch directly. The broader seal-pass
  collapse remains intentionally deferred to item 10.
- `preparedPlanningGroupCount` separates the current frame's active group range from the
  retained high-water backing size. Import/export ordering gates, queue requests, Seal and
  Resolve all use the active count, so persistent storage does not change frame semantics.
- `ResourcePlanningWriter::Impl` is preallocated once up to Vanguard's configured
  `maximumPlanningWriters`, with primary slot `N` permanently associated with flow group `N`.
  Each primary embeds its own atomic-ref lease. Normal acquire/Close/Abandon therefore does
  not touch the shared `writerLock`, a global free list, or a densely packed global lease
  array. If a canceled old-generation writer deliberately keeps the primary leased, the next
  generation uses a heap fallback until that stale handle dies, preserving the no-ABA rule.
- Writer validity checks compare the captured session generation/state before dereferencing
  the reservation. A retained stale reservation pointer can therefore become physically
  obsolete after a later high-water grow without being dereferenced. Active-session group
  count changes remain rejected, so a live writer's reservation cannot move under it.
- Focused `render_flow_resource_allocator_tests.cpp` coverage now holds the sole primary
  writer lease across cancellation (`maximumPlanningWriters = 1`), reuses the same flow
  group in the next frame, verifies the stale writer is rejected, and verifies the current
  writer remains usable. This exercises the rare fallback lease path deterministically.
  A second regression records three long resource names, shrinks to one logical resource,
  then regrows to three and proves compact `0..N-1` indices with no stale high-water name
  becoming logically visible.
- Fresh post-audit Development x64 validation passed with serialized `/m:1`:
  `renderingTests.vcxproj` and `geometryAllocatorTests.vcxproj` both build successfully;
  `renderingTests.exe` and `geometryAllocatorTests.exe` both exit 0 with their expected
  RenderFlow/resource-flow proof suites passing. The rendering link retains the same 9
  pre-existing LNK4006 duplicate-definition warnings; the geometry build is clean.
- The final adversarial item-2 correction pass found 0 BLOCKER / 0 HIGH / 0 MEDIUM issues:
  logical-resource bounds, Seal and Resolve all use `resourceCount`; declaration rollback
  decrements it before clearing the reclaimed slot; stale writers are generation-gated before
  reservation dereference; primary release resets payload before the release-store that makes
  the slot claimable again. `writerLock` remains only on unrelated import/export/stats paths.
- No Runtime/Editor/native launch was performed. No matched post-item-2 Tracy capture was
  taken, so the closure claims only the structural removal of normal per-frame writer-Impl
  allocation, shared writer-return locking, destructive long-name reset, batch ownership
  moves, and nested tape capacity loss; **no microsecond or frame-time improvement is claimed**.

## Item 3 closure evidence — 2026-10-02

- Actual RED `common/renderer/src/renderFlowInternalData.{h,cpp}` was re-read for item 3.
  RED sorts the recorded request stream once by tag/time, reduces trivial tags to direct
  start/end allocation intervals, builds only vital events for non-trivial tag lifetimes,
  and later consumes those stored intervals/events for overlap, reuse, aliasing and lookup.
  RED does not repeatedly rediscover an allocation's requests from the global tape.
- Vanguard now keeps one immutable forward allocation-use chain in the existing global
  `ActiveUseRecord` storage. `AllocationLogicalState` carries `firstUseRecord`, the
  UseBegin-order tail, and `terminalUseRecord`; `ActiveUseRecord` carries
  `nextAllocationUse` and the direct compiled Begin step. Existing `previousActive` /
  `nextActive` remain exclusively the mutable simultaneous-use chain and are still unlinked
  at `UseEnd`.
- Each accepted `UseBegin` appends to the allocation-local chain while the operation stream
  is already being compiled. Each accepted `UseEnd` updates `terminalUseRecord`, making the
  latest actual end event authoritative even when valid read-only uses overlap and end
  non-LIFO.
- This fixes a correctness defect in retained-import finalization: the old global scan chose
  its terminal use by UseBegin-array order, so `A begin, B begin, B end, A end` could attach
  the terminal transition to B while A was still active. The terminal transition now attaches
  to the true latest `UseEnd`, matching RED's ordered lifetime-end semantics.
- Cross-queue use validation now traverses each allocation's chain once. Explicit-fence
  imports still apply their wait to every use packet, but traverse only that imported
  allocation's chain. Export terminal lookup and placed predecessor terminal lookup are
  direct endpoint accesses; placed destination Begin lookup and its compiled Begin-step
  lookup are direct as well.
- `LogicalAllocationRecord::firstUsePosition` / `lastUsePosition` remain separate physical
  lifetime bounds because Vanguard's stronger named/cross-node scopes intentionally extend
  lifetime outside concrete Begin/End use endpoints. The placed range planner continues to
  use those wider bounds, while alias activation/finalization attaches to the real first and
  terminal executable use. Focused placed-resource execution coverage now opens/closes
  resource scopes around those uses so this separation is exercised.
- The previous nested global scans are removed: allocation queue validation is no longer
  O(allocations x all uses); import work is proportional to the uses of each imported
  allocation; export and placed endpoint discovery are O(1). One global O(all uses)
  completeness pass remains intentionally, and the allocation-local active-overlap scans
  during `UseBegin` remain because they answer a different simultaneous-use legality query.
- The allocation timeline is internal append-only bookkeeping, so Resolve does not pay a
  second coverage/cycle/back-link validation layer for facts it just constructed. The one
  global O(all uses) unended-use contract check remains, while import/export/placed endpoint
  consumers retain their boundary validation. `QueueOrderProved` and Vanguard's stronger
  queue/state/subresource contracts are unchanged.
- Focused Development x64 validation passed with serialized `/m:1` after the implementation:
  `renderingTests.vcxproj` and `geometryAllocatorTests.vcxproj` build successfully;
  `renderingTests.exe` and `geometryAllocatorTests.exe` both exit 0. The retained-import
  proof now records `A begin, B begin, B end, A end` and uses the D3D12 state tracker after
  B ends to prove the terminal transition has not fired while A is still active. Existing
  terminal-export, cross-queue fork/join and placed-resource execution proofs also pass.
- Scoped `git diff --check` is clean apart from normal LF/CRLF working-copy warnings. No
  Runtime/Editor/native launch was performed. No matched post-item-3 Tracy capture was taken,
  so item 3 claims the structural complexity reduction and corrected endpoint ownership only;
  **no microsecond or frame-time improvement is claimed without matched measurement**.

## Item 4 closure evidence — 2026-10-02

- RED was studied before the Vanguard edit. `renderFlowInternalData.{h,cpp}` carries the
  resolved `SAllocationRequest::m_allocIndirection -> SAllocation` identity forward and
  reaches `m_texRef` / `m_buffRef` from that allocation at consume/API boundaries. RED's GPU
  state code similarly keys state directly by compact `TextureRef.id` / `BufferRef.id` and
  does not gather GPU refs only to reverse-map them back to allocation identity.
- Vanguard now follows that identity principle with its stronger generation-qualified dense
  `PhysicalResourceId.index`. `CompileScopeEntryStates` admits each physical once per packet,
  using `PhysicalScopeState::trackedPacket` as the dense packet stamp and `trackedSlot` as the
  direct packet-local lookup. Action, step and alias replay therefore perform O(1) indexed
  lookup instead of converting physical -> native ref and `lower_bound` searching a
  ResourceRef-sorted metadata table.
- The old generation-wide `HashMap<ResourceRef.value, physicalIndex>` is gone. It existed both
  to rediscover identity and to re-check native uniqueness. The reverse lookup is no longer
  needed, and production ownership already makes two live physical-binding indices for one
  native ref unreachable: dedicated and placed pool acquisitions mark entries Assigned,
  same-frame lane reuse retains the existing physical index, retained import registration and
  installation reject duplicates, and exported dedicated ownership leaves the pool before a
  later import can own it. Resolve does not retain a release-path hash/sort solely to re-prove
  that owner invariant.
- Vanguard intentionally differs from RED at the final RHI boundary. NVRHI
  `CommonBackend::SeedCommandListStates` requires resources to be strictly sorted/unique by
  `ResourceRef.value` and texture cells to be complete in slice/mip order. The packet-local
  `TrackedResource` records are therefore sorted once by native ref before entry-state
  emission. Native identity is only the required output ordering key; the physical index
  remains the compile/replay identity.
- The final item-4 shape eliminates the intermediate physical-index/reference staging array:
  first sight of a dense physical index creates one minimal `TrackedResource` carrying its
  physical index and native output ref, repeated sightings return through the packet stamp,
  the tracked records are sorted once for the RHI ABI, and descriptor/state fields are then
  filled in place. Reusable packet scratch is intentionally still not introduced; that is
  item 5.
- Placed alias execution still owns the native `aliasPredecessors` array required by
  `ActivateAliasedResource`. A parallel compact `aliasPredecessorPhysicalIndices` array carries
  compile-time identity so scope-state compilation never reverse-discovers it. Both arrays are
  published with lockstep allocation checks and their size parity is validated before use.
- Vanguard's stronger state model is unchanged: generation checks, exact texture subresource
  cells, keepInitialState/explicit-state policy, queue ordering and cross-queue handoff,
  retained-import readiness/terminal contracts, terminal exports, and placed alias
  activation/finalization remain intact. RED's weaker/global synchronization model was not
  copied.
- Focused Development x64 validation passed after the final implementation with serialized
  `/m:1`: `renderingTests.vcxproj` and `geometryAllocatorTests.vcxproj` both build; the rendering
  link retains the same 9 pre-existing LNK4006 duplicate-definition warnings and the geometry
  build is clean. `renderingTests.exe` and `geometryAllocatorTests.exe` both exit 0, including
  retained-import, placed-resource, explicit-subresource-state, terminal-export and
  graphics/compute fork-join proofs.
- Scoped `git diff --check` is clean apart from normal LF/CRLF working-copy warnings. No
  Runtime/Editor/native launch was performed. No matched post-item-4 Tracy capture was taken,
  so item 4 claims only the structural removal of reverse identity hashing, repeated binary
  lookup and redundant staging; **no microsecond or frame-time improvement is claimed without
  matched measurement**.

## Item 5 closure evidence — 2026-10-02

- RED was re-studied before the Vanguard edit. `renderFlowInternalData.{h,cpp}` keeps the
  dominant RenderFlow working set in persistent/fixed internal storage: per-group request
  tapes, tag/fork-join tables, frame allocation requests and member lifetime maps are reset
  logically at cleanup. RED deliberately does **not** make every irregular container a
  permanent high-water cache: non-trivial Resolve maps/sets use `job::PoolJobScope()` and
  frame lifetime maps are rebuilt from `PoolFrameRendering`. The final Vanguard shape follows
  that bounded-persistent-vs-irregular-temporary distinction rather than pooling blindly.
- Vanguard previously created 21 direct Resolve/`CompileScopeEntryStates` dynamic scratch
  containers per frame/packet. The first item-5 implementation moved them into allocator
  lifetime storage, but a radical follow-up audit correctly reopened the item: nested
  `BatchResolveMap` arrays and `PhysicalScopeState::cells` were keyed by transient indices, so
  legal peak work could migrate across indices over many frames and retain many multiples of
  the configured single-frame aggregate limits. The placed planner also still constructed
  fresh working arrays, and RED `HashMap::Clear()`/`HashSet::Clear()` made retained peak bucket
  capacity an O(capacity) reset tax.
- The corrected `BatchResolveMap` is metadata only. One allocator-owned flat
  `batchResourceSlots`, `batchTextureViews`, `batchBufferViews`, and `batchUseRecords` arena is
  sized from the sealed frame's aggregate counts; each batch owns an offset/count slice.
  Retained capacity is therefore bounded by the existing aggregate resource/view/operation
  limits instead of multiplying by historical batch ordinal.
- `PhysicalScopeState` is likewise metadata only. Explicit texture/buffer state cells are
  leased from one flat `physicalStateCells` arena by `cellOffset`/`cellCount`. Dense physical
  history still owns packet/slot stamps and state endpoints, but no transient physical index
  can permanently retain its own large nested cell allocation.
- `LogicalSlot` now contains only `currentAllocation`. The dead copied identity, flow-space,
  declaration position and `String name` were removed. Resolve no longer copies every
  candidate name into a second high-water string buffer.
- `CompileScopeEntryStates` now clears and reuses the same `scopeResources`/`scopeStates`
  buffers before every packet. Its eager resource reserve is capped by the number of physical
  bindings, so a transition/action-heavy packet touching one resource cannot pin scratch for
  every reference occurrence.
- At item-5 closure, Resolve's irregular associative working sets (`namedSlots`, command scopes,
  queue-group membership and open resource scopes) used an internal generation-stamped
  open-addressed scratch table. Normal reset increments a stamp generation in O(1); retained
  bucket capacity is not walked. Growth rehashes only entries from the current generation.
  Item 8 later removes queue-group membership scratch entirely and specializes/lazily activates
  command-scope uniqueness. At item-5 closure,
  `namedSlots` still hashed the full string on lookup/insert while preserving full flow-space +
  string equality; item 6 later removes that repeated hashing. Borrowed candidate-name
  `StringView`s are invalidated by stamp reset before planning storage becomes writable; stale
  generations are never dereferenced or migrated.
- The prior `namedSlots.Reserve(totalDeclarationCount)` was removed. Temporary declarations no
  longer force named-resource hash capacity when no named key is inserted.
- Production placed-range planning now reuses `ResolveScratch::placedPlan` and a persistent
  `PlacedRangePlannerScratch` containing order, heap, active-range and predecessor-fragment
  working memory. `ConsumeFreeRange` compacts/splits the free-fragment array in place instead
  of allocating a fresh `remaining` array for every assignment. Heap-local nested scratch is
  hysteretically trimmed after large-to-small changes, inactive historical heaps release their
  nested backing, and the outer heap high-water is also trimmed after a large workload drop;
  stable workloads retain their useful capacity without the old transient-index accumulation.
- The focused placed-range tests now exercise the reusable planner-scratch overload repeatedly
  across exact-fit alias reuse, sorting, coalescing, touching lifetimes, partially-virgin
  rejection, fragmentation, queue/resource classes and early/late failure rollback. The
  compatibility overload remains for non-hot callers; Resolve uses the reusable path.
- One allocator-owned scratch instance is safe under the existing externally serialized
  coordinator contract: Resolve enters the single `Resolving` phase from `CandidatesSealed`.
  The immutable `ExecutionGenerationRef::Impl`, compiled packets/actions, packet liveness and
  other published data remain generation-owned and are intentionally **not** moved into
  reusable scratch because callers/execution can retain them after Resolve returns.
- Final source-static review after the radical correction found **0 BLOCKER / 0 HIGH / 0 MEDIUM
  item-5 issues**. Shrink/regrow resets remain logically correct; no Resolve scratch pointer or
  borrowed view escapes into a published generation; retained flat high-water storage is
  bounded by legal aggregate frame demand instead of historical transient identities.
- Fresh final Development x64 validation passed with serialized `/m:1`:
  `renderingTests.vcxproj` and `geometryAllocatorTests.vcxproj` both build with exit 0;
  `renderingTests.exe` and `geometryAllocatorTests.exe` both exit 0. The geometry suite's
  multi-frame physical allocator proofs exercise successful Resolve/Finish reuse, abort/failure
  recovery, explicit subresource state, placed resources, retained imports, terminal exports
  and graphics/compute fork-join paths. The rendering link retains the same pre-existing
  LNK4006 duplicate-definition warnings; no new build errors were introduced.
- Final scoped `git diff --check` is clean apart from normal LF/CRLF working-copy warnings.
  No Runtime/Editor/native launch was performed. No matched post-item-5 timing/Tracy capture
  was taken, so item 5 claims structural removal of repeated Resolve/planner scratch allocation,
  O(capacity) associative reset, and historical nested-capacity amplification only;
  **no microsecond or frame-time improvement is claimed without matched measurement**.

## Item 6 closure evidence — 2026-10-02, radical correction 2026-10-03

- RED was re-studied before the Vanguard edit. In
  `common/renderer/src/renderFlowNameTag.h`, `RenderFlowNameAndHash` computes/stores a 32-bit
  name hash before RenderFlow consumption and `RenderFlowNameTag` folds the flow-space/autogen
  identity into its stored 32-bit hash. `CalcHash()` is therefore an O(1) field read. In
  `renderFlowInternalData.cpp::HandlePreConsumeRequest`, RED records that stored value directly
  into `m_parallelPerTagKey`; Resolve later sorts/decodes the compact hash rather than walking
  resource-name bytes again.
- The first item-6 implementation removed full-string hashing from Resolve but a radical
  follow-up audit reopened the item with **0 BLOCKER / 1 HIGH / 1 MEDIUM**. HIGH: the full name
  walk still happened inside `FindOrCreateNamedResource()` on every named recording call, so the
  cost had mostly moved from Resolve instead of matching RED's prehashed-name ownership. MEDIUM:
  `LogicalNameKey` retained the composite hash while `ResolveScratchHashMap::Entry` also retained
  the same hash, wasting four bytes in every high-water `namedSlots` bucket.
- The corrected ownership is RED-shaped. `LogicalResourceName` owns the name-only FNV prehash.
  Dynamic names compute it once when the wrapper is formed. `VG_RENDER_FLOW_NAME("literal")`
  uses a self-computing `consteval` literal constructor, matching RED's static
  `RENDER_NAME_TAG` principle without accepting an unchecked caller-supplied hash;
  the core repeated renderer names (`FrameOutput`, `CameraColor`, depth/G-buffer/geometry names)
  now use that static path. `LogicalResourceKey` carries the prehash and combines it with
  `FlowSpaceId` through an O(1) 32-bit avalanche; allocator recording never walks name bytes.
- `FindOrCreateNamedResource()` reads `key.CalcHash()` in O(1), uses the composite hash as the
  first reject in its writer-local candidate scan, and still requires exact flow-space +
  full-string equality before treating two candidates as the same logical name. The composite
  hash is retained in `CandidateResource`, so hash collisions remain semantically harmless.
- `LogicalNameKey` is identity-only again: `{FlowSpaceId, StringView}`. The generation-stamped
  scratch table exposes `FindPrehashed()` / `InsertPrehashed()`; `namedSlots` supplies the
  candidate's retained composite hash explicitly. `ResolveScratchHashMap::Entry::hash` is now
  the single stored probe/rehash hash instead of duplicating it inside the key. Rehash continues
  to migrate only current-generation entries using that stored value and never re-hashes names.
- Candidate high-water reuse remains safe. `CandidateResource::Reset()` clears the stored hash;
  rollback paths reset a reclaimed resource slot; frame reset hides retained high-water slots
  behind `resourceCount = 0`; and a slot is reset again before reclamation. No stale hash can
  become logically visible after shrink/regrow.
- The item-5 borrowed-name lifetime rule is unchanged: `namedSlots` is stamp-reset before sealed
  candidate tapes become writable, and rehash migrates only current-generation entries using
  their already-stored hash. Item 6 adds no longer-lived string/hash owner.
- Temporary render-flow identities bypass `LogicalResourceName` hashing when the temporary tag is
  formed because they never participate in the named-resource index; their diagnostic string is
  retained without paying a named hash cost.
- The exact-current `CandidateResource::declarationPosition` field was dead: it was written only
  for temporary candidates and had no reader. It was removed while adding the prehash so item 6
  does not widen every retained high-water candidate merely to gain the cached 32-bit value.
  `LogicalAllocationRecord::declarationPosition`, which is live Resolve metadata, is unchanged.
  At item-6 closure `CandidateOperation` itself was deliberately left untouched; item 7 later
  compacts that tape independently.
- Post-correction source-static review found **0 BLOCKER / 0 HIGH / 0 MEDIUM item-6 issues** on
  the prime pass. The only name-byte hash loop now belongs to `LogicalResourceName::CalculateHash`
  at name-wrapper construction; static core names use the compile-time prehashed form, while
  allocator recording and Resolve perform only O(1) hash operations. Queue-order work remains
  untouched for item 8.
- Focused Development x64 validation passed with serialized `/m:1`:
  `renderingTests.vcxproj` and `geometryAllocatorTests.vcxproj` both build with exit 0;
  `renderingTests.exe` and `geometryAllocatorTests.exe` both exit 0. The rendering link retains
  the same pre-existing LNK4006 duplicate-definition warnings; no new compile/link failure was
  introduced. No Runtime/Editor/native UI launch was performed.
- Scoped `git diff --check` is clean apart from normal LF/CRLF working-copy warnings. No matched
  post-item-6 Tracy/timing capture was taken, so item 6 claims only the structural removal of
  repeated Resolve full-string hashing and the writer-local hash fast-reject; **no microsecond,
  frame-time, percentage or absolute timing improvement is claimed without matched measurement**.

- A later independent radical re-audit found **0 BLOCKER / 0 HIGH / 2 MEDIUM** after the first
  corrected closure. Both are now closed. First, the cached name/hash invariant is sealed:
  `LogicalResourceName` and `LogicalResourceKey` keep their paired state private, named keys can
  no longer mutate the string independently of its hash, and the temporary no-hash constructor
  is private to `RenderNodeImplContext`. Second, stable dynamic production names now retain the
  hash at their actual owner: `RenderNodeFullscreenCopy` hashes source/target names once at
  construction, and `GeometryExternalBuffer` hashes its authored external name once and exposes
  a prehashed logical-name view. Repeated declaration/execution paths reuse those values.
- Those late item-6 corrections were revalidated with the same focused Development x64 gates:
  `renderingTests.vcxproj`/`renderingTests.exe` and
  `geometryAllocatorTests.vcxproj`/`geometryAllocatorTests.exe` all pass. No native UI run was
  needed.

## Item 7 closure evidence — 2026-10-03

- RED was re-studied first and again during the radical correction. Exact-current
  `common/renderer/src/renderFlowInternalData.h::SRequest` overlaps mutually exclusive request
  operands in unions and is statically **56 bytes**. `SParallelGroup` owns an inline
  `StaticArray<SRequest, 300>` (320 with ray tracing); overflow is a hard `StaticArray` assertion.
  Separately, RED has a fixed aggregate `kMaxParallelRequests = 4096`. Cleanup clears logical
  request counts while the fixed per-group backing remains in place.
- Vanguard's exact pre-item-7 Development/MSVC x64 layout was measured with CDB:
  **`CandidateOperation` = 328 bytes**, `FrameResourceDesc` = 144 bytes,
  `FrameTextureDesc` = 84 bytes, `FrameBufferDesc` = 56 bytes, `TextureUseDesc` = 48 bytes,
  `BufferUseDesc` = 16 bytes and `TerminalResourceExportDesc` = 8 bytes. The old operation stored
  mutually exclusive IDs/descriptors simultaneously.
- The first item-7 implementation reduced the hot record to 64 bytes, but the user-requested
  radical re-audit reopened the item with **0 BLOCKER / 1 HIGH / 1 MEDIUM**. HIGH: per-writer
  dynamic operation/declaration backing could retain independent historical peaks and generic
  1.5x `DynamicArray` growth could overshoot the aggregate `maximumOperations` value. MEDIUM: the
  64-byte record still carried the 32-byte texture clear union inside every texture use and was
  not guaranteed to be cache-line-contained because the array itself is not 64-byte aligned.
- The final hot record is **32 bytes**: a 16-byte common header (`kind`, flags and three local
  `u32` operands) plus a 16-byte tagged payload. Ordinary texture uses keep required state,
  subresource range, access and content inline; `BufferUseDesc` remains fully inline. CDB `dt -v`
  independently reports `CandidateOperation` = **0x20** and its payload = **0x10** in the final
  Development binary. No cache-line-containment claim is made.
- Explicit texture-use clear values are cold only when `TextureClearValue::kind != None`. The
  producer stores a batch-local cold index and rolls back the just-appended cold record if the
  operation append fails. Resolve bounds-checks that index and reconstructs a complete
  `TextureUseDesc` before applying the pre-existing clear/content validation. This preserves the
  intentional invalid non-Clear+clear case, Clear+None rejection, declaration-initialization clear
  equality, and every color/uint/depth/stencil clear form. The focused typed-clear proof passes.
- Declaration descriptors and texture-use clears share one `CandidateColdPayload` union tape
  instead of three independently growing arrays. CDB reports that cold union as **0x58 = 88 bytes**
  (`FrameTextureDesc` 0x54, `FrameBufferDesc` 0x38, `TextureClearValue` 0x20). By operation kind,
  each accepted operation contributes at most one cold entry, so `coldPayloads.Size()` cannot
  exceed the writer's accepted operation count and historical cold-kind peaks cannot stack.
- Vanguard now has RED's two-level count bound while retaining graceful failure semantics:
  `maximumOperationsPerPlanningWriter = 768` is a hard local cap and the existing
  `maximumOperations = 262,144` remains the aggregate Seal gate. The default local cap is 12x the
  aggregate-per-maximum-writer average (`262144 / 4096 = 64`), closely matching RED's fixed
  per-group-slot/global-request ratio (`160 * 300 / 4096 = 11.71875`; 12.5x with RT). This is a
  count-slack analogy, not a byte-parity claim.
- `PrepareBoundedPlanningAppend()` clamps operation and cold-tape growth to that local hard cap,
  so the generic 1.5x container policy cannot overshoot it. Successful validated frames clear
  logical sizes while retaining only this bounded per-group high-water, matching RED's stable
  group backing and avoiding per-frame shrink/regrow churn. Canceled/rejected planning clears and
  shrinks request/cold backing because it never passed aggregate admission. If a later graph uses
  fewer flow groups, `PreparePlanningGroups()` releases the request/cold backing in the inactive
  reservation tail, so an old larger graph cannot remain as hidden retained high-water.
- With the default limits, the hot accepted-frame tape is bounded at exactly **8 MiB**
  (`32 * 262,144`), versus **82 MiB** before item 7 (`328 * 262,144`): a **74 MiB / ~90.24%**
  hot-tape reduction. One writer's bounded retained item-7 backing is at most **24 KiB hot +
  66 KiB cold = 90 KiB** (`768 * (32 + 88)`). Across all 4096 configured writer slots, the
  theoretical retained item-7 high-water is therefore explicitly bounded at **360 MiB**, exactly
  12x the accepted-frame worst-case combined hot+cold bound of **30 MiB**. These are structural
  footprint bounds only, not measured runtime-speed claims.
- Tape-local storage still omits redundant full public IDs. Resources/views use batch-local
  indices; use/decision identities reconstruct `{batch.flowGroup, operationIndex,
  batch.sessionGeneration}`; retained import/export slots keep local indices after record-time
  generation/range validation. The operation array index remains the authoritative ordinal and
  `UseEnd` keeps only the validated begin ordinal plus the existing duplicate-end flag contract.
- Focused regression coverage now explicitly sets `maximumOperationsPerPlanningWriter = 2` under
  a larger aggregate operation limit and proves the third operation fails locally with
  `CapacityExceeded`, rolls its cold declaration back, and leaves cancellation/shutdown safe.
- Final Development x64 validation with serialized `/m:1` passes after the radical corrections:
  `renderingTests.vcxproj` / `renderingTests.exe` and
  `geometryAllocatorTests.vcxproj` / `geometryAllocatorTests.exe` all exit 0. The rendering link
  retains only the pre-existing LNK4006 duplicate-definition warnings. No Runtime/Editor/native UI
  execution was performed.
- Full `git diff --check` is clean apart from expected LF/CRLF working-copy notices. A scoped diff
  confirms item 7 did not modify `QueueOrderProved()`; command-scope/queue-order precomputation
  was still item 8 and had not started at item-7 closure.
- No matched post-item-7 timing/Tracy capture was taken. Item 7 therefore claims only the exact
  layout/bounded-storage changes above; **no microsecond, frame-time or percentage speedup is
  claimed without matched measurement**.
