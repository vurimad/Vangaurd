# Vanguard Mesh Residency Execution Plan

Latest source checkpoint (2026-09-09): 9C component adapters and 9D RenderScene
drawable ownership are implemented; see
[the component ledger](../../source/entities/docs/concrete-rendering-components.md).
They reuse 9B bindings and existing component/runtime infrastructure. 9D adds
revision-safe bind/clear acceptance and bounded changed-proxy resolution. Executable
verification has not run. Phase 10 drawing remains outstanding; preceding 9B.2
proof gates are still open.

## Current continuation authority -- 2026-09-07

[Geometry rendering study](geometry-rendering-study.md) refines the remaining
work after completed mesh Phase 9A. Continue with 9B RED batcher/binding, 9C
components, 9D scene lifecycle and Phase 10 GPU-driven indirect drawing. Historical
Stage numbers below are not additional phases to redo. Render Graph execution
and material runtime now exist; reuse them. The study supersedes the independent
pipeline-bucket/mesh-batch registry proposal below and records the exact
source-port boundaries required by bindless, existing GPU Scene and GPU driving.
The user's clarified 1:1 scope is relevant RED CPU preparation/recording, not
CPU-visible sorting or direct draws. The first geometry path is GPU-driven and
indirect; the former direct-first recommendation is withdrawn. Reuse existing
RenderScene spatial GPU-candidate production instead of adding a chunk collector.
Current slice (2026-09-09): **9B.2 -- shell/bin placement and strong drawable mesh
binding**. The interruption stopped after unpublished shell/bin leases. Recovery
now implements complete anchor closure, shared placement/residency acceptance,
strong shared binding, withdrawal and existing GPU Scene lifetime retention.
Normal retirement sealing consumes joined actual RHI receipts, with explicit
coverage for never-submitted queues. **9B.1 implementation is complete; combined
verification is deferred. 9B.2 production source is implemented, but phase exit
is unverified.** No live acceptance or scene output was observed. The detailed
checkpoint is `source/rendering/docs/geometry-batcher-port.md`. No compilation, test execution
or writing tests until the user explicitly requests the final batch; no new
component or CPU direct-draw path is included.

9B.2 static review follow-up: new drawable table scans were replaced by an
intrusive active-work queue, bounded to 64 closures and 256 preparation member
checks per Tick. Publication examines at most 64 queued closures separately.
Ready bindings remain off the queue. Oversized scene uploads and failed receipt
snapshots now fail explicitly. Source inspection only; verification is deferred.

Date: 2026-08-28

Status: Final study-derived plan. Production implementation was explicitly
authorized on 2026-08-28 and is in progress.

This file contains the final dependency-ordered, file-level implementation plan
produced by Phase 5. Earlier headings retain the evidence and constraints that
led to it.

## Global implementation gate

Implementation begins only after:

1. Study Phases 0–4 pass their exit gates.
2. Phase 5 reconciles ownership, format, threading, publication, fallback,
   batching, and fence lifetime into one design.
3. The resulting design and this execution plan receive explicit approval.

Current state:

```text
Phase 0: complete
Phase 1: complete
Phase 2: complete
Phase 3: complete
Phase 4: complete
Phase 5: complete
Production implementation: authorized and in progress
```

## Corrected implementation order

This is the authoritative implementation numbering. The later detailed stage
headings preserve the original synthesis breakdown as historical detail; they
must not be used to identify the current stage.

```text
Stage 1: focused RHI indirect capability/offset hardening                 complete
Stage 2: runtime mesh metadata and owned loose/VPAK page source           in progress
Stage 3: fixed-function geometry allocator and uploader                   pending
Stage 4: atomic GPU Scene v5 placement, residency publication, manager    pending
Stage 5+: proxy migration, feedback, batching/MDI, diagnostics            pending
```

Stage 2 checkpoint implemented so far:

- The pre-Stage-2 streaming overlap has been removed before adding more mesh
  policy. `ResourceStreamer` full-resource loads and specialized pageable
  loaders now use the same `ResourceSource::ReadAsync` physical I/O, VPAK
  authentication, segment decode, cancellation, and result mapping path.
  Source resolution and dependency snapshotting also have one implementation.
- A mounted VPAK now owns one parsed, pinned immutable package generation.
  Every published `ResourceSource` retains that generation, so individual mesh
  opens neither reopen nor reparse the package index, and unmount cannot
  invalidate an already published source.
- The resource pipeline owns one opaque operation-local loader-state slot.
  The generic streamer and specialized mesh loader use it instead of creating
  a second mesh request/lifetime map. Pipeline shutdown rejects leaked loader
  state.
- Full-resource output staging, package stored/decoded scratch, and specialized
  mesh prefix/META bytes are admitted through one generic streaming staging
  budget and reported by one accounting source. This mechanism is deliberately
  format-neutral; mesh code supplies only mesh sizes and policy.
- Generic `ResourceRangeReadQueue` now coalesces equal ranges from one immutable
  source generation. Each caller retains an independent cancellable interest;
  the physical operation is cancelled only when the final interest leaves.
  Mesh page requests use this queue with their page-derived byte range.
- Range output plus VPAK stored/decoded scratch is admitted as one transaction.
  Requests that fit the configured ceiling but not the currently available
  space wait in bounded FIFO order and start when earlier shared buffers are
  released; a request larger than the ceiling still fails immediately.
- Generic `streaming::ResourceSource` owns and pins one immutable loose or VPAK
  physical generation. Independent `ResourceSourceReader`s provide positional
  logical range reads, package segment decoding, and stored/decode telemetry.
  This source primitive is intentionally format-neutral and reusable by texture,
  animation, audio, world-cell, and other pageable formats.
- `ResourceSource::PlanRead` reports the exact logical, stored, and decoded
  costs of a range. `ReadAsync` now submits real redIO operations rather than
  blocking a worker, authenticates and decodes every touched VPAK segment, and
  keeps the physical source generation alive through terminal callbacks.
  `ResourceReadRequest` is move-only, waitable, and best-effort cancellable;
  cancellation remains a distinct terminal result.
- `MeshPageSource` is now a thin mesh-policy adapter over that generic source;
  loose and VPAK metadata/page reads use existing `MeshFile` validation and
  exact VPAK segmentation. It does not duplicate generic file/package loading.
- `MeshPageSource::ReadPageAsync` owns the temporary page bytes until the
  caller releases its request and exposes them only after the existing page
  SHA-256 check succeeds. Source closure or movement cannot invalidate an
  already submitted read.
- `MeshResourceObject` owns immutable metadata, exact retained dependency
  handles, and the page source; mismatched dependencies fail before
  publication.
- fallback LOD install pages are derived through `CollectLodPages` and checked
  for `RequiredForLowestLod`.
- generic streaming tests prove independent synchronous readers, exact async
  read planning, source pinning, and byte-exact loose/VPAK async ranges, while
  focused mesh tests prove metadata-only opening and synchronous/asynchronous
  one-segment page validation.
- Offline source-mesh ingestion now uses the pinned private Assimp snapshot for
  file and memory imports. The mesh-tools-owned `ImportedMesh` representation
  converts scene transforms, vertex streams, submeshes, materials, texture
  dependencies, and four-weight skin data without exposing Assimp types.
- `ImportedMesh::BuildSourceView` adapts that owned import directly to the
  existing `SourceMesh`/`CookMesh` contract. This deliberately avoids a second
  intermediate mesh format or a mesh-specific copy of the resource pipeline.
- Focused tests cover memory import, file dependency discovery and strict
  missing-dependency failure, coordinate/scale conversion, winding, and direct
  importer-to-cooker handoff.
- `MeshAssetCompiler` now registers the mesh source-to-vmesh transformation
  with the generic `assets::BuildSystem`. Its canonical settings include import
  policy, cooking profile, packing/optimization limits, and LOD policy; source,
  compiler-tool, material, skeleton, and Assimp-opened file dependencies enter
  the ordinary build fingerprint instead of a mesh-only cache.
- Compilation verifies that the resolved physical source matches the bytes in
  `BuildRequest`, imports and cooks through the existing mesh tools, and emits
  metadata plus independently streamable geometry-page artifacts through
  `ArtifactWriter`. Metadata is primary/memory-resident and lowest-LOD pages
  retain their bootstrap residency flag, so the existing package planner can
  preserve vmesh page boundaries without mesh-specific package machinery.
- The integration test exercises `BuildSystem::Prepare`, generated material
  dependency resolution, `Execute`, derived-data cache reuse, artifact
  reconstruction, and runtime `MeshFile` validation.
- The repository Stanford Bunny OBJ is a real-asset checkpoint through that
  same registered compiler path. Its 34,817 vertices and 69,630 triangles cook
  to 208,890 indices, two fixed-function vertex streams, three hash-validated
  geometry pages, and four package-ready artifacts (one metadata plus three
  page artifacts). Y-up to Vanguard Z-up conversion produces bounds
  `(-1.8938, -1.17599, -0.340252)` to `(1.22018, 1.23747, 2.74642)`.

Still required before Stage 2 completes: project-level orchestration that owns
the physical source resolver and persists generic build artifacts/index records
under `DerivedData`. This belongs to the future project/editor asset service;
the mesh compiler, artifact segmentation, generic package-planner contract, and
runtime mesh loading path no longer require an editor-specific implementation.
Generic range reads now classify only physical `IoFailure` as transient and
retry it a bounded number of times (two retries by default) while retaining the
same admission. Cancellation is distinct; integrity, version, layout, digest,
capacity, and argument failures are terminal. Mesh page SHA validation, LOD
upload sets, and fallback policy remain in `meshes`.

## Phase 0 execution consequences

These are constraints discovered from the current Vanguard baseline. They are
not yet final implementation stages.

### Preserve existing authorities

- Keep `ResourceRegistry` as the owner of published `ResourceObject` identity
  and strong/weak generational handles.
- Keep `ResourcePipeline` as the dependency-aware, coalesced load-operation
  authority.
- Keep `RenderingService` as the renderer-global composition owner.
- Keep per-world `RenderingRuntime` as the bounded proxy admission/retirement
  owner.
- Keep `GpuSceneDefinitions` and existing GPU Scene handles as the canonical
  geometry/material/renderable identity.
- Keep `GpuSceneUploader` and `GpuSceneLifetime` publication/retirement
  boundaries rather than adding an unrelated GPU table system.

### Required missing boundaries

The final plan must provide, but does not yet name or locate permanently:

1. A production mesh `ResourceObject` and loader/preparation integration that
   publishes validated metadata without staging all optional geometry pages.
2. A durable source/page-read boundary for loose and packaged `.vmesh` data.
3. A renderer-global geometry residency owner covering demand, budgets,
   allocation, page IO/decode, upload, pendingGeometry, fallback, eviction, and
   fence retirement.
4. A fixed-function geometry allocator covering synchronized split vertex
   streams plus independent index arenas.
5. A transactional adapter from resident `.vmesh` LODs and resolved `.vmat`
   dependencies into existing GPU Scene geometry/material/renderable
   definitions.
6. A component/proxy readiness bridge that exposes valid fallback renderability
   and propagates failure/cancellation.
7. A stable batch-set registry and GPU indirect-command/count generation path
   using physical buffer bindings without per-frame readback.

### Provisional dependency order

The final implementation will likely respect this dependency graph, but exact
stages remain subject to Phases 2–5:

```text
resource/source ownership + format contract
  -> geometry placement types and allocator
  -> page upload/install/publication transaction
  -> GPU Scene ABI/residency integration
  -> budgets/eviction/fence retirement
  -> minimal component/proxy consumer boundary
  -> visibility LOD feedback
  -> batch-set registry and GPU MDI generation
  -> diagnostics, failure recovery, and rollout
```

### Anticipated compatibility work

- `GpuGeometryRange`/`GpuVertexStream` may require a layout-versioned ABI change
  to express the fixed-function vertex arena set and physical binding key.
- `.vmesh` may require a versioned extension only if Phase 2 proves current page
  dependency/install information insufficient.
- The generic `ResourceStreamer` contract may need a specialized preparation or
  pageable-source path; it should not be expanded casually until Phase 2 defines
  ownership and cancellation.
- Existing distant-proxy admission must be upgraded from retaining a mesh-typed
  generic handle to resolving a drawable `GpuRenderableHandle`.
- `StaticMeshComponent` design remains outside this study. The residency work
  defines only the request/release, readiness/fallback, binding, and detach
  contract that a future component will consume.
- RHI APIs already expose fixed-function bindings, counted indexed indirect
  drawing, copies, and queue fences. Backend extensions should be justified by
  concrete missing capability rather than assumed.

### Validation obligations carried forward

Every later implementation stage must include proportional validation for:

- no full `.vmesh` staging for optional pages;
- atomic LOD publication and rollback;
- stable generational identities;
- bounded CPU and GPU memory;
- fence-safe physical range reuse across graphics/compute/copy;
- fixed-function split-stream correctness;
- RenderDoc mesh inspectability;
- no-readback MDI command issuance;
- cancellation, failure, device loss, and world/component teardown.

## Historical pre-implementation conditions

- Complete the remaining study phases and resolve the open questions in
  `mesh-residency-design.md`.
- Record the RED snapshot's Perforce changelist if it becomes available; the
  current content hash is sufficient for the study to continue.

These conditions were satisfied before the corrected implementation sequence
above began.

## Phase 1 execution consequences

These are ownership and lifetime constraints for the eventual implementation,
not authorization to add production types yet.

### Required authorities and handles

- Keep `ResourceReference`, `PipelineRequest`, `ResourceHandle`, and
  `ResourceRegistry` unchanged as the public resource identity/load foundation.
- Add one renderer-global mesh residency authority under `RenderingService`.
- Give that authority private generational `MeshResidencyHandle` records keyed
  by loaded resource identity `(path, type, resource generation)`.
- Provide move-only, reference-counted/coalesced `MeshResidencyRequest` demand
  handles. Dropping one interest must not cancel other callers.
- Provide an owning `MeshRenderBindingHandle` for proxy admission and live proxy
  payloads. It retains the published `GpuRenderableHandle`, its definition
  reference, and the installed physical-placement lifetime.
- Do not expose residency handles as serialized asset IDs or replace existing
  GPU Scene geometry/renderable handles.

### Required ownership changes

- The production mesh `ResourceObject` owns validated metadata, resource
  dependencies, and a durable page-source locator/session; it does not own GPU
  arena allocations.
- The residency manager owns page operations, installed LOD records, physical
  placements, definition references, budgets, and retirement state.
- `GpuSceneDefinitions` continues to own definition slots and dependency
  reference counts but never frees geometry arena byte ranges.
- Mesh proxy descriptors/payloads must carry both the existing strong mesh
  `ResourceHandle` and the new strong binding handle. Storing a bare
  `GpuRenderableHandle` is not sufficient lifetime ownership.
- Components own authored references, request/admission handles, and proxy
  bindings only. They never own page buffers, physical allocations, or GPU
  definition release.

### Required thread handoffs

```text
component/world demand
  -> thread-safe residency command queue
  -> serialized residency arbitration
  -> I/O / Jobs-owned page work
  -> generation-checked completion queue
  -> renderer command-chain upload/install
  -> main-thread GPU definition and proxy binding transaction
```

- Worker callbacks return owned immutable completion payloads; they do not
  mutate components, RenderScene, residency records, or GPU definition tables.
- Proxy/admission/visibility/definition reference mutations remain main-thread
  operations in V1, matching current contracts.
- Buffer copies may use copy or graphics queues, but pendingGeometry stays
  unpublished until its completion requirements are satisfied.

### Required teardown transaction

The implementation must encode this order explicitly:

1. Cancel pending admission or hide the live proxy immediately.
2. Detach the component binding and enqueue proxy/GPU-instance retirement.
3. Wait for the covering GPU Scene retirement epoch.
4. Destroy the proxy payload and drop its `MeshRenderBindingHandle`.
5. Release definition references no longer retained by residency or bindings.
6. Retire physical geometry placements behind all relevant graphics, compute,
   and copy fences.
7. Release the residency record's mesh `ResourceHandle` when no work remains.
8. Only then acknowledge `WorldStreamingExecutor::CompleteRelease`.

Frame delays and visual dissolves do not authorize physical range reuse.

### Failure and cancellation obligations

- Resource-pipeline and residency cancellation are interest-based, not global
  booleans owned by an arbitrary caller.
- Unpublished reservation/upload work must roll back transactionally after
  non-cancellable GPU work completes.
- Failure to install an optional quality LOD preserves the current fallback.
- Failure to install the minimum/fallback LOD prevents drawable proxy admission
  and propagates the original failure trace.
- All asynchronous completion payloads carry the target residency generation;
  stale completions are discarded without touching a reused slot.

### Validation required with the eventual implementation

- coalesced demand and independent cancellation tests;
- detach tests at every asynchronous boundary;
- stale-generation tests for resource, residency, definition, and proxy handles;
- strong proxy-binding retention tests;
- ordered world-release acknowledgement tests;
- multi-queue fence-safe definition/allocation retirement tests;
- shutdown diagnostics identifying every live request, strong owner, proxy, placement,
  and retirement epoch.

### Phase 1 gate

```text
Phase 1 ownership design: complete
Phase 2 state at the Phase 1 checkpoint: not started
Implementation: prohibited
```

## Phase 2 execution consequences

These constraints define the eventual mesh metadata and page-I/O stage. They do
not authorize production implementation before Phase 5 synthesis.

### Preserve the current format authority

- Keep `vmesh` V1, `MeshFile` validation, `BuildStorageSegments`, and
  `CollectLodPages`.
- Treat the last LOD's exact collected page set as the fallback install atom.
- Do not add virtual-page tables, page fixups, or runtime geometry transcode.
- Keep page/buffer/LOD dense indexes scoped to one loaded resource generation;
  retain resource path, submesh stable ID, material-slot name, and content
  fingerprint for their distinct stable/content roles.

### Required production seams

1. A `MeshResourceObject` owning immutable metadata, retained material/skeleton
   handles, and an owned `MeshPageSource`.
2. A specialized `VMSH` loader that asynchronously reads metadata only and
   validates META references against source dependency metadata.
3. An owned loose/package `MeshPageSource` that refers to one immutable source
   generation and supports independent concurrent asynchronous page reads.
4. Mesh cooker/build integration that emits the existing storage segmentation
   and required dependency table; current proof exists only in tests.
5. Budgeted, coalesced page operations keyed by loaded resource generation and
   page index, ending in verified owned bytes at `ReadyForInstall`.

The generic `ResourceStreamer` continues reconstructing full resources for
ordinary decoders. Mesh optional geometry must not use that staging path.

### Required state and failure behavior

```text
Absent -> WaitingForBudget -> ReadingStoredBytes
       -> DecodingSegment -> VerifyingPageDigest -> ReadyForInstall
       -> Cancelled | Failed
```

- Stored and decoded bytes are both charged before I/O.
- Multiple LOD demands share a page operation; cancellation is interest-based.
- Active cancellation is best effort and completion remains generation checked.
- Transient I/O retry is bounded; corruption, digest mismatch, invalid format,
  and unsupported versions are terminal.
- V1 retains no long-lived CPU geometry cache after Phase 3 consumes the
  handoff payload.

### Phase 2 gate

```text
Phase 2 format/resource/streaming design: complete
Phase 3 state at the Phase 2 checkpoint: not started
Implementation: prohibited
```

## Phase 3 execution consequences

These constraints define the eventual physical geometry-residency stage. They
do not authorize production implementation before Phase 5 synthesis.

### Required placement types and allocator

- Add generational `VertexArenaSetId` and `IndexArenaId` identities owned by the
  renderer-global mesh residency authority.
- Key each vertex arena set by an exact fixed-function vertex-layout
  fingerprint and allocate one synchronized logical vertex interval across all
  of its split binding buffers.
- Keep independent index arenas segregated by `UInt16` and `UInt32`.
- Use normal device-local `Vertex | CopyDestination` and
  `Index | CopyDestination` RHI buffers, not virtual resources or storage-buffer
  vertex pulling.
- Make chunks fixed and non-moving after publication. Capacity growth adds a
  chunk; oversized requests receive dedicated chunks. V1 has no live
  compaction or published slab replacement.
- Use staged deterministic best-fit allocation with address-ordered coalescing,
  exact alignment validation, and configurable arena-count limits.

### Required geometry uploader and publication boundary

Add a `GeometryUploader` patterned after, but separate from,
`GpuSceneUploader`:

```text
complete verified LodInstallSet
  -> reserve all vertex/index ranges and staging
  -> fill every upload reservation
  -> record copies on CopySync (graphics queue)
  -> successful geometry submission
  -> batch-acquire GpuGeometry definitions
  -> atomically publish complete LodPlacement
```

- The uploader owns bounded mapped staging segments and reuses a segment only
  after its submission fence completes.
- V1 uses `CommandListType::CopySync`; the current RHI does not expose the
  cross-queue join needed to publish `CopyAsync` bytes safely to following
  graphics work without a CPU wait.
- Release Phase 2 page payloads after successful geometry submission. Retain
  them across a bounded pre-submit retry; submitted staging remains uploader
  owned until fence completion.
- Failures before submission cancel all unpublished reservations. Failures or
  cancellation after submission retire the unpublished ranges behind the
  upload fence. They never free those ranges immediately.
- `GpuSceneDefinitions` remains the geometry-definition authority, while the
  residency manager remains the physical byte-range owner.
- Phase 4 must integrate resident LOD placements into the canonical mutable
  renderable/LOD view; Phase 3 must not invent a second GPU renderable identity.

### Required budget and eviction accounting

Track independently:

- stored/decoded CPU page bytes;
- upload-staging committed/in-flight bytes;
- GPU committed arena bytes;
- pending, live resident, and retiring placement bytes;
- free bytes, largest free range, and unusable fragmentation.

Pending and retiring memory remains charged to the hard admission cap. Use
configurable high/low watermarks, collect completed retirements before eviction,
and defer work rather than waiting when staging, budget, chunk, or retirement
capacity is exhausted.

Eviction is a complete optional `LodPlacement`, never an individual physical
page or range. Live binding/demand, fallback, pendingGeometry/publication, and
in-flight use are explicit pins. The required coarsest fallback cannot be
evicted while a mesh must remain drawable; if hard capacity cannot admit a new
fallback, admission waits or fails without sacrificing an existing fallback.

### Required fence handoff and retirement

- Add bounded geometry retirement epochs patterned after `GpuSceneLifetime`.
- Remove a placement from future publication and release its definition/binding
  owners before appending its ranges to retirement.
- Seal non-empty epochs with an actual `rhi::ResidencyFenceSet` covering every
  configured graphics, compute, and copy queue.
- Extend the renderer frame/submission epilogue to expose those real GPU fences.
  `RenderFrameSubmission::serial` is only a CPU submission identity and cannot
  authorize reuse.
- Collect non-blockingly; reuse ranges only after all queue fences complete,
  and increment allocation generation on every reuse.
- Release an empty arena's RHI buffers only after every contained range is
  reclaimed. The RHI's whole-resource generational lifetime remains the final
  destruction barrier.
- Native explicit residency is a whole-empty-arena optimization only. It must
  not pretend to evict a live suballocation from a mixed arena.

### Validation required with the eventual implementation

- synchronized split-stream allocation and alignment tests;
- layout/index-format segregation, oversize, fragmentation, coalescing, arena
  limit, non-moving identity, and stale-generation tests;
- fault injection at every reserve, stage, record, submit, definition, publish,
  cancel, and rollback boundary;
- proof that a partial LOD is never drawable and a submitted range is never
  immediately reused;
- staging/budget exhaustion, high/low watermark, deterministic eviction, pin,
  fallback preservation, and bounded-work tests;
- graphics/compute/copy partial-fence rejection, epoch exhaustion, delayed
  range reuse, and delayed empty-chunk destruction tests;
- diagnostics for committed/live/pending/retiring/free bytes, fragmentation,
  staging pressure, install states, eviction reasons, pins, epochs, and stale
  operations.

### Phase 3 gate

```text
Phase 3 geometry residency design: complete
Phase 4 state when the Phase 3 gate closed: not started
Production implementation: prohibited
```

## Phase 4 execution consequences

These constraints define the eventual GPU-consumption and draw-submission
stage. They do not authorize production implementation before Phase 5
synthesis and explicit approval.

### Required GPU Scene migration

- Keep `GpuRenderableHandle` as the sole renderable identity stored by
  `GpuInstance`, RenderScene proxy bindings, visibility, feedback, and draws.
- Advance `GpuSceneLayoutVersion` and split immutable renderable/LOD/primitive/
  phase topology from mutable residency placement.
- Add a parallel `GpuRenderableResidency` entry at the renderable index with the
  matching generation, a 64-bit resident LOD mask, required fallback LOD, and
  placement revision.
- Move installed geometry out of immutable `GpuPrimitive`; add mutable,
  generation-checked primitive geometry placements and per-phase shell/bin
  placements. Exact ABI packing is a Phase 5 deliverable.
- Add explicit `VertexArenaSetId` identity to the geometry range consumed by
  batching. Keep independent `IndexArenaId` and index format.
- Publish an installed LOD by uploading all primitive/phase placements before
  setting its resident bit. Evict by clearing the bit before releasing or
  retiring any downstream record.
- Order V1 mutation uploads, visibility compute, indirect construction, and
  graphics consumption on the graphics queue. Async compute requires a future
  explicit queue join and is not enabled implicitly.

### Required pipeline bucket and batch registry

Current refinement: keep ready shell/bin ownership in the RED-derived
`RenderGeometryBatcher`, reusing `MaterialTechniqueRequest` and `PipelineCache`.
Do not add a renderer-global `PipelineBucketRegistry` above the existing cache:

- poll asynchronous requests without calling `Wait` during frame execution;
- publish only successful complete graphics pipeline descriptions;
- assign stable generational bucket identities and normal/mirrored variants;
- make pipeline hot reload create a new generation and retire the old one;
- require a real ready compatible pipeline before drawable admission; a fallback,
  if used, must itself satisfy that requirement.

Keep stable batch data inside that owner with:

```text
GpuBatchShellKey
  render phase
  pipeline bucket + generation
  vertex arena set + generation
  index arena + generation
  index format
  non-PSO state profile

GpuGeometryBin
  shell identity
  geometry identity/generation
  fixed indexed indirect arguments
```

Shell and bin slots are non-moving while referenced, reference-counted by
resident phase placements, snapshotted immutably for each frame, and retired
behind the same relevant queue-fence coverage as their geometry consumers.
Bindless material parameters do not split a shell; shader-interface or fixed
pipeline differences do.

### Required visibility, LOD, feedback, and draw passes

Extend the render graph with ordered GPU stages:

1. Apply GPU Scene placement mutations and clear frame-local counters.
2. Cull candidates and select desired plus first coarser resident LOD.
3. Aggregate one streaming-demand record per renderable across authority views.
4. Expand selected LOD primitives/phases into stable shell/bin work.
5. Count instances per bin and scan output ranges.
6. Scatter compact `(instance, primitive)` draw records.
7. Build indexed indirect arguments, compact nonempty commands, and write one
   count per shell segment.
8. Bind stable shells and record counted indexed MDI.
9. Compact and copy streaming feedback into a delayed readback-ring slot.

The existing visibility result must become a selected-LOD record rather than
only `GpuInstanceIndex`. Its capacity is at least candidate count. Expansion
uses immutable metadata to compute a conservative worst-case packet count.
Views that exceed configured transient limits are processed in deterministic
waves; no visible draw may be silently dropped.

Feedback is a distinct service from `VisibilityFeedbackService`. It aggregates
renderable generation, placement revision, finest desired LOD, selected LOD,
fixed-point priority, and view category. The residency manager consumes only
the newest completed readback without waiting. Stale results are discarded;
missing/overflowing feedback extends demand retention and cannot authorize
eviction.

### Required no-readback shell executor

At frame snapshot time, preassign each eligible view/phase/shell segment:

- vertex/index bindings and index format;
- pipeline and non-PSO state;
- compact draw-instance range;
- indirect argument base and maximum command count;
- count-buffer offset.

The CPU records every known active shell segment in stable registry order after
the indirect-generation GPU dependency. It never maps visibility, bin, argument,
or count output and never sorts current visible instances. A zero count makes a
counted call execute no draw commands.

Primary path:

```text
Bind pipeline + split vertex buffers + index buffer
Bind draw-instance and indirect/count buffers
DrawIndexedPrimitiveIndirectCount(known argument offset,
                                  known count offset,
                                  known maximum)
```

Fallback when indirect count is unavailable: keep one known command slot per
bin, write zero `instanceCount` for empty bins, and call fixed-count indexed
MDI. A device without indexed indirect does not run this production path; a
separate CPU direct/debug renderer may be chosen during device admission but
must not emulate support with GPU readback.

Split oversized shells into stable segments satisfying checked buffer sizes,
argument/count alignment, `u32` maximum command counts, and every native
backend's offset limit.

### Phase eligibility

- `Unordered` and `State` mesh phases use the shell executor.
- `FrontToBack` may use approximate GPU depth buckets only when order is an
  optimization rather than a correctness requirement.
- Exact `BackToFront` transparency is excluded. It requires a separate direct,
  GPU-sort, or OIT renderer contract and cannot be represented as arbitrary
  cross-shell MDI order.

### Required capacity, failure, and retirement behavior

- Candidate and selected-LOD output are lossless for admitted candidate waves.
- Expansion capacity is derived from immutable maximum packet counts; checked
  arithmetic failure rejects planning instead of truncating work.
- Streaming feedback alone may clamp because it affects future quality. Its
  requested count and overflow are retained, and overflow is conservative.
- Pipeline failure, missing placements, generation mismatch, missing fallback,
  graph ordering errors, and shell/bin capacity exhaustion are explicit errors
  or policy decisions, never stale-table reads.
- Clearing a resident bit, removing shell/bin references, releasing definitions,
  and retiring physical ranges occur in that order. Reuse waits for the final
  graphics/compute/copy fence coverage established in Phase 3.

### Validation required with the eventual implementation

- stable renderable identity across every optional LOD transition;
- resident-mask and placement publication/eviction ordering;
- all desired/resident/fallback combinations across 64 LOD bits;
- feedback deduplication, latency, stale generation, overflow, and no-wait
  behavior;
- exhaustive batch-key equality/splitting, mirrored winding, bindless material,
  pipeline readiness/hot reload, and shell/bin generation tests;
- direct-versus-counted/fixed MDI image and argument equivalence;
- zero/one/many commands, all phases/views/waves/segments, reset and dependency
  failures, and native command/offset limits;
- instrumentation proving no map/readback or visible sort occurs before MDI;
- RenderDoc inspection of fixed-function split vertex buffers, index buffer,
  indirect arguments, selected instance data, and reconstructed mesh;
- exact transparent work rejected from the general shell executor.

### Phase 4 gate

```text
Phase 4 GPU consumption/batching/MDI design: complete
Phase 5 synthesis/implementation gate at Phase 4 closure: not started
Production implementation: prohibited
```

## Phase 5 final implementation sequence

This sequence is the only authorized dependency order once the user separately
approves implementation. Each stage must compile and pass its completion gate
before the next stage begins. A rollback point means the branch can disable or
remove that stage without changing cooked asset identity or accepting partially
published runtime state.

### Stage 1 — RHI capability and GPU Scene ABI foundation

Modify:

- `source/rhi/include/vanguard/rhi/rhi_types.hpp`
- `source/rhi/nvrhi/src/d3d12_backend.cpp`
- `source/rhi/nvrhi/src/common_backend.cpp`
- `source/rhi/tests/rhi_tests.cpp`
- `source/rhi/nvrhi/tests/d3d12_backend_tests.cpp`
- `source/rendering/include/vanguard/rendering/gpu_scene_types.hpp`
- `source/rendering/include/vanguard/rendering/gpu_scene_tables.hpp`
- `source/rendering/src/gpu_scene_tables.cpp`
- `source/rendering/shaders/gpu_scene_types.hlsli`
- GPU Scene type/table shader tests.

Work:

1. Add explicit indexed/multi-draw/counted-indirect capabilities and native
   alignment/count/offset limits.
2. Reject `u64` indirect offsets that cannot be represented by the backend.
3. Change `GpuSceneLayoutVersion` to 5 and add the three parallel placement
   tables and exact records from the design.
4. Teach table capacity growth to grow owner and parallel tables together.
5. Make lifetime allocation/publication/retirement reject parallel table kinds.

Completion gate:

- every ordinal/size/offset/page-size assert passes in C++ and shaders;
- known-pattern GPU ABI test passes;
- indirect boundary/alignment/overflow tests pass;
- all existing GPU Scene tests are migrated, with no compatibility shim for
  layout 4.

Rollback point: revert layout/capability changes as one atomic commit; no asset
format or runtime data migration exists yet.

### Stage 2 — Parallel placement upload transactions

Modify:

- `source/rendering/include/vanguard/rendering/gpu_scene_upload.hpp`
- `source/rendering/src/gpu_scene_upload.cpp`
- `source/rendering/include/vanguard/rendering/gpu_scene_lifetime.hpp`
- `source/rendering/src/gpu_scene_lifetime.cpp`
- `source/rendering/tests/gpu_scene_tests.cpp` or the existing equivalent test
  target.

Work:

1. Add an owner-validated parallel upload request/target.
2. Preserve duplicate/overlap detection across ordinary and parallel targets.
3. Add ordered multi-table publication helpers for placement commit and clear.
4. Propagate real graphics/compute/copy `ResidencyFenceSet` coverage through
   retirement records.

Completion gate: tests prove owner-index equality, forged/wrong-generation
rejection, rollback before commit, resident-bit-last pendingGeometry,
resident-bit-first eviction, and no reuse until all queue fences complete.

Rollback point: layout 5 can remain with zeroed placement tables while the old
test renderer remains disabled; no mesh-residency consumer is connected.

### Original synthesis Stage 3 — Runtime mesh metadata and owned page source (current Stage 2)

Add:

- `source/meshes/include/vanguard/meshes/mesh_resource.hpp`
- `source/meshes/include/vanguard/meshes/mesh_page_source.hpp`
- `source/meshes/src/mesh_resource.cpp`
- `source/meshes/src/mesh_page_source.cpp`
- `source/meshes/src/mesh_resource_loader.cpp`

Modify:

- `source/meshes/include/vanguard/meshes/meshes.hpp`
- `source/meshes/src/meshes.cpp`
- `source/meshes/premake5.lua`
- the existing resource-pipeline loader registration point;
- `source/meshes/tests/meshes_tests.cpp`
- resource/VPAK integration tests.

Work: publish metadata-only `MeshResourceObject`, retain dependencies and one
owned loose/VPAK `MeshPageSource`, expose asynchronous exact page reads and LOD
upload sets, and account stored/decoded bytes. Do not change the cooker or
`vmesh` version.

Implemented so far: the engine composition root automatically registers the
specialized `VMSH` loader. A normal pipeline request resolves the existing
`ResourceStreamer` winner, retains its non-soft dependency table, performs a
RED-style staged asynchronous header/prefix/META read, validates parsed META
against the resolved dependency handles, and publishes `MeshResourceObject`
without staging `GEOM`. It carries its state in the existing pipeline operation,
uses the same package generation and physical I/O engine as normal resource
loads, and charges prefix/META storage to the shared streaming staging budget.
The generic source resolver and accounting remain in `streaming`; the format
parser and publication policy remain in `meshes`.

Completion gate: opening a mesh reads metadata only; optional LOD page reads are
range-limited, coalesced, hash-validated, cancellable, and safe after the caller
releases temporary request state; loose and VPAK results are byte-identical.

Rollback point: unregister the specialized loader and retain the existing
`MeshFile` tooling path. Cooked files remain valid.

### Original synthesis Stage 4 — Fixed-function geometry allocator and uploader (current Stage 3)

Add:

- `source/rendering/include/vanguard/rendering/geometry_allocator.hpp`
- `source/rendering/include/vanguard/rendering/geometry_upload.hpp`
- `source/rendering/src/geometry_allocator.cpp`
- `source/rendering/src/geometry_upload.cpp`
- focused allocator/uploader tests.

Modify:

- `source/rendering/premake5.lua`
- RHI buffer-creation/copy tests only if a demonstrated contract is missing.

Work: implement layout-keyed non-moving split vertex arena sets, separate
`UInt16`/`UInt32` index arenas, synchronized base-vertex intervals, dedicated
oversize allocations, best-fit/coalescing, three regular upload segments plus bounded overflow staging, bounded
submission, cancellation, stats, and fence retirement.

#### Current Stage 3 RHI/runtime audit

The preimplementation audit found that Stage 3 does not need another generic
buffer, upload, fence, or resource-lifetime layer.

Reuse directly:

- `rhi::BufferDesc` already expresses device-local vertex/index destinations,
  CPU-visible upload buffers, copy usage, initial resource state, and `u64`
  byte sizes.
- `rhi::LockBuffer` supports keeping one upload buffer mapped for the lifetime
  of an uploader segment. The existing `GpuSceneUploader` already proves the
  three-regular-segment mapped-staging pattern, bounded overflow allocation,
  and non-blocking segment selection.
- `CopySync` maps to the graphics queue. `TransitionBuffer`, `CopyBuffer`, and
  `CloseAndSubmitCommandLists` provide the complete V1 geometry-copy path and
  return a real `GpuFence`.
- Command lists retain every referenced RHI resource through submission and the
  RHI records per-queue resource use before releasing those command references.
  Whole arena-buffer destruction can therefore use existing intrusive RHI
  lifetime handling.
- `GpuSceneUploader` supplies the reusable transaction shape: plan bounded
  reservations, let producers fill mapped memory, reject incomplete batches,
  coalesce adjacent copies, submit once, and reuse staging only after fence
  completion. Geometry needs a separate uploader because its destinations and
  range ownership are not GPU Scene table allocations.
- `GpuSceneLifetime` supplies the reusable generational/retirement-epoch model:
  cancellation before publication, multi-queue fence coverage after
  publication, non-blocking collection, and stale-handle rejection. Geometry
  needs a range allocator with equivalent rules, not table allocation through
  `GpuSceneLifetime`.
- Fixed-function binding is already present and validated: up to 16 vertex
  bindings, `Vertex`/`Index` usage checks, and 2/4-byte index alignment.
- `MeshResourceObject`, `BuildLodUploadSet`, `MeshPageSource`, and immutable
  `MeshFile` records already provide the verified page bytes and exact
  buffer/layout/submesh metadata from which Stage 3 builds an upload plan.

Demonstrated narrow gaps and constraints:

- The corrected implementation table marks Stage 1 indirect-capability
  hardening complete, but the current `rhi::Capabilities` still contains none
  of the planned indexed-indirect, multi-draw, counted-indirect, command-count,
  or indirect-offset limit fields. The call paths currently validate the
  NVRHI-shaped 32-bit offsets directly. This drift does not block physical
  geometry allocation, but Stage 5 MDI work must not assume those documented
  capabilities exist; either restore the focused Stage 1 change before MDI or
  correct the completion record.
- The public RHI does not currently report a truthful maximum physical buffer
  size or fixed-function vertex/index binding offset. The NVRHI index binding
  path explicitly rejects offsets above `u32`, while the public type is `u64`.
  Stage 3 must initially cap every index arena chunk and binding below 4 GiB;
  before configurable arena sizing is exposed, add only the narrow capability
  fields/tests needed to prevent an invalid configuration. Do not create a new
  allocation API for this.
- `uploadBufferAlignment` is populated from D3D12 texture-placement alignment.
  It must not be interpreted as the required alignment of ordinary buffer-copy
  source/destination offsets. Geometry allocation uses format, stride, and an
  explicit conservative internal buffer alignment unless a backend contract
  demonstrates another requirement.
- RHI lifetime protects whole `BufferRef` objects, not byte suballocations.
  Fence-delayed geometry-range reuse remains the responsibility of the new
  geometry allocator.
- `RenderFrameSubmission` currently contains only a CPU serial. It is not a GPU
  completion token. Stage 3 exposes retirement/collection in terms of an
  explicit `ResidencyFenceSet` and tests it with real queue fences; Stage 4 must
  route the frame's latest graphics/compute/copy submission fences to active
  geometry retirement before live published ranges can be reclaimed.
- No public cross-queue copy-to-graphics join exists. Keep V1 geometry upload
  on `CopySync`; do not add a CPU fence wait or switch to `CopyAsync`.

Audit decision: implement the allocator and uploader in `rendering`, reuse the
existing RHI and GPU Scene patterns, and make no broad RHI changes. The only RHI
work permitted by this audit is a focused physical-buffer/binding-limit
capability if Stage 3 configuration cannot otherwise be validated truthfully.

#### Stage 3A implementation checkpoint — geometry allocator complete

Implemented:

- Layout-fingerprint-keyed fixed-function vertex arena sets. Every stream in a
  set shares one synchronized `firstVertex`, so normal vertex-buffer binding and
  indexed MDI remain compatible without storage-buffer vertex pulling.
- Separate `UInt16` and `UInt32` index arenas, conservative byte alignment, and
  a hard sub-4-GiB index-arena limit matching the current NVRHI binding path.
- Deterministic best-fit allocation, sorted free-range coalescing, dedicated
  oversize arenas, and generation-checked arena/allocation ids.
- Explicit `Reserved -> Active -> Retiring -> Invalid` ownership, exact-token
  validation, batch reservation rollback, and cancellation before publication.
- Fence-sealed retirement epochs covering graphics, compute, and copy queues;
  range reuse is impossible until every configured queue fence completes.
- Byte/capacity/fragmentation/lifetime statistics and a focused D3D12
  integration target independent of the currently stale monolithic RHI suite.

Verified by `geometryAllocatorTests`: layout and index-format separation,
synchronized split-stream offsets, deterministic reuse, transactional rollback,
dedicated-arena generation invalidation, forged-token rejection, and delayed
three-queue reclamation all pass against the real RHI.

Not implemented in Stage 3A: staging uploads, upload planning/coalescing, copy
submission, and install publication. Those form Stage 3B and must consume this
allocator rather than create another physical geometry owner.

#### Stage 3B.1 implementation checkpoint — bounded geometry uploader complete

Implemented:

- Exactly three persistently mapped upload buffers owned by `GeometryUploader`;
  a segment is selected without waiting and cannot be reused while its real RHI
  submission fence remains incomplete.
- A complete atomic LOD larger than one regular segment uses one exact-size,
  bounded overflow upload buffer. This avoids a permanent worst-case staging
  commitment while removing the old single-segment LOD limit.
- One upload ticket per exact `GeometryReservation`, exposing one mapped slice
  per fixed-function vertex binding and one mapped index slice. Producer jobs
  may fill and complete tickets, but cannot mutate allocator ownership.
- Deterministic destination ordering, conservative staging alignment, overlap
  rejection, bounded geometry/copy counts, and coalescing of adjacent source and
  destination ranges.
- One `CopySync` command list per batch, with destination transitions to copy
  state and restoration to vertex/index state. V1 introduces no async-copy join
  and no CPU fence wait.
- Atomic allocator `CommitBatch` after successful GPU submission. Failed or
  cancelled pre-submit batches leave their exact geometry reservations under
  caller ownership and unpublished.
- Upload/fence/segment occupancy statistics and focused real-D3D12 validation
  for mapped slice sizing, incomplete/stale completion rejection, cancellation,
  staging exhaustion, copy submission, allocator commit, and later retirement.

Geometry arena buffers deliberately remain `Vertex | CopyDestination` or
`Index | CopyDestination`; the test does not add `CopySource` merely for
readback because that would weaken the locked non-compaction contract.

#### Stage 3B.2 implementation checkpoint — verified pages to completed upload tickets

Implemented in `mesh_geometry_upload.hpp/.cpp`:

- A mesh-specific rendering adapter consumes a complete set of already-verified
  `.vmesh` page payloads. It performs no resource I/O, request coalescing,
  retries, decoded-byte admission, or page ownership; those remain Phase 2
  responsibilities.
- The adapter independently derives the exact sorted `LodInstallSet` and rejects
  missing, duplicate, unrelated, null, or incorrectly sized payloads before it
  allocates geometry.
- Every drawable submesh becomes one fixed-function geometry request. Vertex
  attributes sharing a binding must resolve to the same source buffer and
  stride, bindings must be contiguous from zero, and mesh index formats lower
  explicitly to the RHI format.
- Checked page-to-destination fragments map each vertex-binding and index range
  across cooked page boundaries. Final cooker bytes scatter directly into the
  mapped `GeometryUploader` slices; no second assembled CPU vertex/index buffer
  or format conversion is introduced.
- The complete LOD reserves all physical geometry through one
  `GeometryAllocator::ReserveBatch`, then opens one uploader batch. Planning
  records validated source-to-staging copies; a worker fills the mapped bytes
  and completes every ticket. Any pre-submit failure cancels only the
  reservations and uploader batch owned by this attempt.
- `PreparedMeshLodUpload` retains the exact content fingerprint, LOD, sorted
  page identities, submesh-to-geometry mapping, reservations, and byte totals.
  It represents unpublished ownership and must next be submitted or explicitly
  cancelled; it is not a second streaming cache or independently retained copy.

Verified by `geometryAllocatorTests` against the real D3D12 backend: a cooked
two-binding LOD crosses vertex page boundaries and uploads a partial index
range; duplicate/incomplete page identity is rejected before allocation;
cancellation rolls back every range; an unrelated already-open uploader batch
is preserved; and the prepared batch submits, commits, retires, and reclaims
through real fences.

Not implemented in Stage 3B.2: bounded per-tick upload orchestration,
resource-generation revalidation owned by the future residency manager, GPU
definition publication, or atomic resident-LOD publication. Those later
integration steps must consume `PreparedMeshLodUpload` and reuse the existing
allocator/uploader rather than introduce another geometry owner.

#### Stage 3B.3 implementation checkpoint - asynchronous LOD geometry uploader complete

Implemented in `mesh_lod_geometry_uploader.hpp/.cpp`:

- `MeshLodGeometryUploader` accepts a live `MeshResourceObject` handle and one LOD,
  derives the complete sorted page set, starts one asynchronous
  `MeshPageSource` request per page, and polls without a CPU wait.
- Repeated demand for the same resource generation and LOD returns the same
  typed request id and increments an independent demand-interest count, so one
  caller cannot cancel work still requested by another. Overlapping page reads still use Phase 2's existing
  `ResourceRangeReadQueue`, so exact-range coalescing, FIFO decoded/stored-byte
  admission, source-generation pinning, and bounded transient-I/O retry were
  not reimplemented in rendering.
- The coordinator stores a weak handle to the exact immutable resource
  generation. Immediately before preparing geometry it locks and revalidates
  the generation, content fingerprint, LOD, and exact page identities. A stale
  generation fails without allocating GPU geometry.
- Pending upload bytes, LOD submissions per tick, and upload bytes per tick
  are bounded. Arena pressure, a busy uploader, or staging
  pressure defers without waiting; repeated preparation pressure has an
  explicit bounded deferral limit.
- A ready request reserves and plans on the renderer thread, fills mapped
  staging on a Jobs worker, then returns to the renderer thread for submission
  and allocator commit. Successful submission releases every page-read payload
  because the uploader staging allocation then owns the bytes through its copy fence.
- `PendingMeshLodGeometry` returns the exact generation identity, content
  fingerprint, page set, submesh mapping, active geometry placements, byte
  totals, and copy fence. This is unpublished physical ownership: the next
  phase must atomically publish all geometry definitions or retire all ranges.
- Cancellation releases page interests and pending accounting. Shutdown
  refuses live requests, including completed ownership, instead of hiding an
  allocation leak.
- SHA-256 page verification runs once in generic asynchronous range-read
  completion and is shared by every coalesced mesh-page interest; polling does
  not re-hash page bytes.

Verified by `geometryAllocatorTests` against the real D3D12 backend and a real
loose `.vmesh` source: duplicate LOD requests coalesce, asynchronous verified
pages reach one CopySync submission, page payloads/accounting are released,
the returned geometry retires and reclaims through real fences, and eviction of
the resource generation before submission is rejected as stale.

Not implemented in Stage 3B.3: GPU geometry-definition allocation/upload and
atomic resident-LOD publication. That is the final mesh-pipeline publication
boundary before returning to the component/render integration work from which
this residency effort was a deliberate detour.

#### Stage 3B.4 implementation checkpoint - GPU Scene placement foundation started

Implemented without adding any culling, LOD selection, pipeline registry,
indirect-command construction, batching, render phase, or draw execution:

- GPU Scene CPU/shader ABI advanced atomically from layout 4 to layout 5 with
  the exact 18 table ordinals and byte layouts synthesized above.
- `GpuRenderable`, `GpuLod`, `GpuPrimitive`, and
  `GpuPhaseParticipation` are immutable topology. Geometry and mutable pipeline
  placement were removed from topology records.
- Same-index `GpuRenderableResidency`, `GpuPrimitivePlacement`, and
  `GpuPhasePlacement` tables were added. Owner-table capacity growth also grows
  its parallel table, while lifetime allocation rejects parallel table kinds.
- `GpuGeometryRange` now identifies the generational vertex arena set and
  index arena directly and uses fixed-function `firstIndex`/`baseVertex`.
  `GpuVertexStream` now describes a binding rather than another physical-buffer
  identity.
- Sparse GPU Scene uploads may target a parallel table only through a live,
  generation-checked owner allocation with the same index range.
- `PublishGpuSceneLodPlacement` submits complete primitive/phase placement
  images first and submits the authoritative renderable residency image second.
  If the second submission fails, the old residency entry remains authoritative
  and the partial placement records are not selectable.
- `GpuRenderableDefinition` is topology-only and no longer retains geometry
  definitions for the lifetime of the renderable identity.

The rendering library and its test executable compile after this checkpoint.
The existing `renderingTests` executable still reports its pre-existing
RenderPath dependency-order failure, which is outside mesh residency and was
not chased here.

#### Stage 3B.5 implementation checkpoint - pending geometry definitions complete

Implemented in `mesh_lod_definitions.hpp/.cpp`, without adding rendering work:

- `PreparePendingMeshLodDefinitions` revalidates the exact mesh resource
  generation and content fingerprint immediately before definition creation.
- It requires complete, one-to-one authored submesh coverage for the LOD and
  rejects stale, duplicate, missing, or incompatible geometry mappings.
- Every active physical placement becomes one immutable `GpuGeometryDefinition`
  with its generational vertex-arena set, index arena, fixed-function
  `firstIndex`/`baseVertex`, binding strides, index format, and optional position
  decode data.
- Geometry definitions are acquired as one existing `GpuSceneDefinitions`
  transaction. Failure rolls back that transaction and leaves the caller's
  `PendingMeshLodGeometry` untouched for retry or explicit retirement.
- Success transfers physical geometry and definition handles into
  `PendingMeshLodDefinitions`. It remains deliberately pending and does not set
  a renderable resident bit.

Still intentionally not implemented: residency-manager ownership/rollback and
actual phase shell/bin creation. The resident bit must not be set with invented
phase placements. Shell/bin creation remains in its separately approved later
rendering stage.

Completion gate: deterministic allocator model tests and RHI integration tests
prove split-stream addressing, index addressing, no live movement, full rollback,
staging backpressure without waits, exact byte accounting, and multi-queue-safe
reuse.

Rollback point: allocator/uploader remain isolated and destroyable after
`WaitIdle`; no GPU Scene placement refers to them.

### Stage 5 — Topology-only definitions and mesh residency manager

Add:

- `source/rendering/include/vanguard/rendering/mesh_residency.hpp`
- `source/rendering/src/mesh_residency.cpp`
- `source/rendering/tests/mesh_residency_tests.cpp`.

Modify:

- `source/rendering/include/vanguard/rendering/gpu_scene_definitions.hpp`
- `source/rendering/src/gpu_scene_definitions.cpp`
- `source/rendering/include/vanguard/rendering/gpu_scene_runtime.hpp`
- `source/rendering/src/gpu_scene_runtime.cpp`
- `source/engine/include/vanguard/engine/rendering_service.hpp`
- `source/engine/src/rendering_service.cpp`
- engine/rendering service tests and configuration.

Work:

1. Remove geometry ownership from immutable renderables and pipeline ordinals
   from immutable phase records.
2. Implement generational residency records, move-only demands, strong render
   bindings, page-read coalescing, budgets, fallback bootstrap, optional LOD
   scheduling, transactions, pressure eviction, and shutdown drain.
3. Compose one manager under `RenderingService` after resources/RHI/GPU Scene
   and before frame rendering; expose only the narrow manager facade needed by
   world runtimes.

#### Stage 5.1 implementation checkpoint - composition and lifecycle complete

`MeshResidencyManager` now owns the existing geometry allocator, three-segment
geometry uploader, and asynchronous LOD geometry uploader as one renderer-global
stack. `RenderingService` initializes it after GPU Scene definitions, shuts it
down before GPU Scene/RHI teardown, rolls it back on later startup failure, and
exposes only manager readiness and statistics rather than mutable access to the
owned layers.

This checkpoint deliberately adds no demand records, resident-LOD policy,
eviction, component integration, culling, indirect command generation, or draw
submission. Those remain behind later Stage 5/6 boundaries.

#### Stage 5.2 implementation checkpoint - residency identity and demand ownership complete

The manager now creates private nonzero-generational residency records keyed by
the exact loaded mesh resource path and generation. `MeshDemandHandle` is
move-only and releases one caller interest on reset/destruction. Concurrent
requests coalesce through a bounded resource-path hash lookup, while different
resource generations retain distinct records. The last demand removes the
metadata-only record and invalidates its old handle generation.

Snapshots expose only resource identity, anchor LOD, state, and interest
counts. Shutdown refuses live records/demands. This checkpoint intentionally
stops at `MetadataRetained`: it does not start page reads or uploads, because
the serialized install/rollback and multi-queue retirement boundary must own
completed physical geometry before demand processing is connected to the
existing LOD uploader.

#### Stage 5.3 implementation checkpoint - serialized anchor geometry preparation complete

`MeshResidencyManager::Tick` now starts one coalesced anchor-LOD request for each
exact demanded mesh generation and advances the existing page-source/LOD
uploader without waiting on IO or GPU fences. A successful request ends at
`AnchorLodUploadSubmitted`: the manager owns complete verified physical
geometry and its copy-completion fence, but no GPU Scene placement or drawable
residency has been published.

Demand cancellation is transactional. Reads and unsubmitted preparation are
cancelled through `MeshLodGeometryUploader`; completed physical placements are
retired through `GeometryAllocator`. The manager retains their exact allocation
ids until renderer-supplied graphics/compute/copy fences are sealed and
collection proves the ranges reusable. It does not submit artificial command
lists merely to manufacture retirement fences. A demand that returns while an
upload cancellation is finishing retries from retained metadata; a demand that
returns before completed geometry retirement prevents that retirement.

Focused integration coverage drives the real `.vmesh` anchor from retained
metadata through page reads and three-segment GPU upload, verifies byte and
placement counts, cancels it, seals comprehensive queue fences, and proves the
record and arena allocation are recycled only after collection.

Still intentionally absent at this checkpoint: immutable GPU Scene definition
acquisition, atomic placement/residency publication, strong drawable bindings,
component/proxy admission, culling, indirect command generation, and drawing.
The manager tick is not attached to an arbitrary service callback until the
normal renderer submission boundary can provide its real retirement fence set.

#### Stage 5.4 implementation checkpoint - immutable anchor geometry definitions complete

After physical upload, `MeshResidencyManager::Tick` now acquires one immutable
`GpuGeometryHandle` for every authored submesh placement and moves the complete
LOD transaction into `AnchorLodDefinitionsSubmitted`. The manager owns the GPU
arena ranges, definition references, copy-completion fence, and definition-upload
fence together. The operation remains non-blocking and does not reinterpret or
repack cooked vertex/index bytes.

Cancellation releases every definition reference before retiring its physical
geometry. GPU Scene lifetime retirement and geometry-arena retirement remain
separate owners, but both are sealed by the renderer's real comprehensive queue
fences. Focused integration coverage proves acquisition, state visibility,
definition release, and both retirement paths.

This checkpoint deliberately does not call `PublishGpuSceneLodPlacement` and
does not set a renderable resident-LOD bit. The manager has no renderable
definition yet, so it has no legitimate renderable, primitive, or phase owner
allocations. Inventing empty phase shell/bin placements would make an incomplete
LOD selectable. That activation belongs after the renderable/material/phase
definition boundary is supplied; culling, indirect commands, and drawing remain
later rendering work.

Completion gate: fallback readiness, every transaction fault point, stable
renderable identity across installs/evictions, shared multi-world demand,
pressure hysteresis, device loss, and full shutdown balance pass. The manager
never waits on frame IO, feedback, upload, or fences.

Rollback point: service configuration keeps mesh residency disabled and refuses
drawable mesh binding; legacy generic mesh proxies remain test-only until Stage
6 migration.

### Stage 6 — Minimal proxy/world consumer migration

Modify:

- `source/rendering/include/vanguard/rendering/render_scene.hpp`
- `source/rendering/src/render_scene.cpp`
- `source/rendering/include/vanguard/rendering/render_scene_gpu.hpp`
- `source/rendering/src/render_scene_gpu.cpp`
- `source/entities/include/vanguard/entities/rendering_runtime.hpp`
- `source/entities/src/rendering_runtime.cpp`
- `source/entities/include/vanguard/entities/visual_component.hpp`
- `source/entities/src/visual_component.cpp`
- rendering/entities/engine integration tests.

Work: replace mesh `ResourceHandle` payloads with strong
`MeshRenderBindingHandle`, add asynchronous pending-residency admission, transfer
bindings into GPU Scene retirement, expose generational proxy-retirement
completion, and delay world `CompleteRelease` until teardown really completes.
Keep component policy outside this stage.

Completion gate: no visible proxy exists without a fallback; optional LODs never
rebind the proxy; release during every admission/residency/retirement state is
safe; stale callbacks/tokens are rejected; no two-frame delay is used as a GPU
reuse proof.

Migration: change all `MeshProxyDesc` and `MeshProxyUpdate` call sites in one
compile-atomic change. Do not preserve an overload accepting generic mesh
handles, because it would reintroduce false readiness.

Rollback point: disable mesh-proxy admission as an explicit unsupported state;
never fall back to unsafe generic handles.

### Stage 7 — Delayed LOD feedback

Add:

- `source/rendering/include/vanguard/rendering/mesh_lod_feedback.hpp`
- `source/rendering/src/mesh_lod_feedback.cpp`
- `source/rendering/shaders/mesh_lod_feedback.slang`
- feedback tests.

Modify:

- `source/rendering/include/vanguard/rendering/gpu_scene_visibility.hpp`
- `source/rendering/src/gpu_scene_visibility.cpp`
- `source/rendering/shaders/gpu_scene_visibility.vsl`
- render-graph/frame-resource declarations when that executor is installed.

Work: select desired and best resident LOD on GPU, validate residency revision,
aggregate/compact future demand, maintain the four-slot nonblocking readback
ring, and feed completed old results to the manager.

Completion gate: all 64-bit mask combinations, stale generations, latency,
overflow freeze/rescan, ring wrap, and no-wait behavior pass. Instrumentation
proves current drawing does not depend on feedback readback.

Rollback point: keep every mesh at its fallback LOD and disable feedback passes;
identity and proxy binding remain valid.

### Stage 8 — Pipeline buckets, shells, and geometry bins

Add:

- `source/rendering/include/vanguard/rendering/render_geometry_batcher.hpp`
- `source/rendering/src/render_geometry_batcher.cpp`
- RED-port compatibility and placement ownership tests.

Modify:

- `source/rendering/include/vanguard/rendering/pipeline_cache.hpp`
- `source/rendering/src/pipeline_cache.cpp`
- `source/rendering/include/vanguard/rendering/render_pipeline_factory.hpp`
- `source/rendering/src/render_pipeline_factory.cpp`
- mesh-residency placement publication.

Work: implement generational pipeline buckets and stable, reference-counted,
fence-retired shells/bins using the exact key; publish normal/mirrored phase
placements; keep pending compilation nonblocking; migrate hot reload by
generation.

Completion gate: exhaustive key-field tests prove compatible offsets/materials/
instances do not split, incompatible binding/fixed state does, mirrored winding
is separate, pending/failed pipelines are observable, and retired generations
survive all last uses.

Rollback point: retain residency and fallback proxies but disable GPU-driven
phase eligibility. No placement may reference a destroyed registry entry.

### Stage 9 — GPU draw generation and shell executor

Add:

- `source/rendering/include/vanguard/rendering/mesh_draw_generation.hpp`
- `source/rendering/src/mesh_draw_generation.cpp`
- `source/rendering/shaders/mesh_draw_expand.slang`
- `source/rendering/shaders/mesh_draw_prefix.slang`
- `source/rendering/shaders/mesh_draw_scatter.slang`
- `source/rendering/shaders/mesh_draw_indirect.slang`
- GPU/draw-generation tests.

Modify:

- `source/rendering/include/vanguard/rendering/frame_renderer.hpp`
- `source/rendering/src/frame_renderer.cpp`
- render graph node/factory/execution files when the graph implementation is
  available;
- shader build manifests and `source/rendering/premake5.lua`.

Work: create bounded candidate waves, validate and expand phase placements,
count/prefix/scatter bins, generate portable 20-byte indexed arguments and
counts, and record one counted/fixed MDI per immutable shell segment. Admit only
supported RHI capability sets. Route exact back-to-front phases away from this
executor.

Completion gate: exact expected argument buffers/counts and GPU pixel samples
match small deterministic fixtures for all required variants; no new direct-draw
renderer is needed as an oracle. Zero/one/many bins and boundary offsets pass;
missing graph edges are detected; no map/readback or CPU visible sort appears in
the frame; RenderDoc reconstructs the fixed-function mesh.

Rollback point: disable GPU-driven mesh phases at device/renderer admission.
Never perform a visible-set readback fallback.

### Stage 10 — Diagnostics, fault injection, soak, and rollout

Add or modify:

- residency/draw stats and structured dumps in their owning modules;
- renderer debug-view registration and GPU timestamp instrumentation;
- deterministic fault-injection test support;
- `source/rendering/tests`, `source/meshes/tests`, `source/entities/tests`, and
  engine service integration tests;
- developer documentation and RenderDoc capture checklist.

Work: implement every counter, debug view, failure record, trace, injection point,
and stress scenario specified in the design. Record bootstrap-capacity telemetry
on representative scenes and devices; tune configuration without changing ABI.

Completion gate:

- all unit/integration/GPU/fault/stress tests pass under normal and constrained
  budgets;
- quiescent leak/reference/allocation checks balance;
- device-loss and shutdown drains are deterministic;
- RenderDoc checklist passes;
- production capacity values are backed by captured telemetry and documented.

Rollback point: feature flag/device admission keeps the new path disabled. Asset
format remains V1 and requires no recook.

## Cross-stage migration rules

- GPU Scene layout 4 and 5 are not runtime-compatible. All engine shaders and
  CPU records migrate in Stage 1; mixed binaries fail the layout check.
- `vmesh` is unchanged, so no asset migration or recook is required.
- Generic mesh-resource proxy overloads are deleted in Stage 6 rather than
  deprecated into an unsafe dual path.
- Every new manager has `Initialize`, bounded tick work, explicit failure
  consumption, stats, and `Shutdown` that reports live ownership.
- Stages may add debug feature flags, but no flag may change publication order,
  generation checks, or fence requirements.

## Final implementation gate

```text
Phases 0–5: complete
Architectural execution plan: complete
Correctness-critical open design questions: none
Performance tuning: Stage 10 telemetry gate, non-ABI
User approval to modify production code: pending
Production implementation: prohibited until that approval
```
