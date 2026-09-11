# Material residency onward: conversation and implementation backup

Date captured: 2026-09-09

## Purpose and fidelity

This file preserves the decisions, implementation sequence, proof boundaries and
resume position from the long Vanguard conversation beginning when production
material residency was started. It is intended to survive Codex conversation
history/hydration failures and to seed a fresh implementation thread.

This is a curated conversation record, not a byte-for-byte transcript. Repeated
event records, raw command output, compiler logs and crash/reopen duplicates are
omitted. Statements below distinguish user decisions, implementation outcomes,
historical validation and work that remains unverified.

The original local thread is:

```text
thread: 019fa4d6-80bc-7271-ac07-9ac80c6f5233
session: C:/Users/Ark/.codex/sessions/2026/07/27/
         rollout-2026-07-27T19-29-08-019fa4d6-80bc-7271-ac07-9ac80c6f5233.jsonl
material-start vicinity: JSONL line 87993, 2026-08-31T00:35:20Z
```

The JSONL grew beyond 410 MB and 106,000 records. On application reopen the UI
twice showed an older Slang-language-server answer as the apparent final message,
although later material, mesh and 9B.2 turns remained present and ordered in the
JSONL. Repository checkpoints, not the UI scroll position, are therefore the
continuation authority.

## Standing user instructions

- Vanguard uses Premake, not CMake.
- Preserve the heavily dirty shared worktree. Never reset, clean, restore or
  broadly format unrelated changes.
- Keep implementation reports bounded to the requested subsystem. An affected
  target matrix is not a green whole solution.
- Material Phase 3 ends at a headless, no-draw runtime boundary.
- The first production mesh rendering path is GPU-driven and indirect. Do not
  create a temporary CPU direct-draw renderer.
- ImGui is editor-only and hidden behind Vanguard APIs; it is unrelated to the
  material/geometry runtime work recorded here.
- Current explicit verification instruction: do not compile, run tests or write
  tests until the user explicitly requests the final batch.

## Conversation starting point

The material work began after the texture path had established cooked VTEX,
streaming, physical upload, bindless descriptors and GPU Scene texture residency.
The conversation established that Vanguard had two strong ends:

```text
offline assets/compiler                              persistent GPU endpoint
VMAT + VSHADER + VPPL + reflection                  GpuMaterial tables
material graph and cooked dependency metadata       bindless resource indices
```

The production middle was missing: runtime resource objects, retained dependency
resolution, reflected parameter storage, atomic GPU material publication,
residency ownership, native technique readiness and scene binding lifecycle.

The transition message was effectively:

> We are ready to start materials. Texture residency and mesh geometry now have
> stable GPU-facing boundaries, and GPU Scene already provides the persistent
> table/lifetime endpoint. The material task is to connect cooked VMAT closure to
> that endpoint without replacing those foundations.

The next scan concluded:

> The cooked format and GPU destination are real, but the production runtime
> middle is missing.

That became `docs/development/material-system-study-plan.md` and
`docs/development/material-system-design.md`.

## Locked material architecture

The agreed target flow was:

```text
authored material graph/template/instance
    -> validated typed material IR and dependencies
    -> generated Slang and reflected interface
    -> cooked VSHADER + VPPL + VMAT
    -> ResourcePipeline loaded material closure
    -> retained logical resource resolution
    -> parameter words + GpuMaterialResource range
    -> atomic GpuMaterial definition publication
    -> stable GpuMaterialHandle
    -> lazy native phase/attachment technique readiness
    -> scene or mesh owner retains the accepted binding
```

The core decisions were:

1. VMAT remains a small immutable cooked resource. It stores logical resource
   references, not runtime descriptor indices.
2. Material byte layout comes from shader reflection, not a built-in universal
   PBR structure.
3. Named VPPL techniques provide phase-compatible pipelines.
4. Authoring graphs, templates, functions and inheritance are flattened before
   runtime. Runtime never walks an editor graph or parent chain.
5. Static choices affect compiled shader/pipeline identity. Scalar/vector/texture
   values remain data and must not spuriously split compatible draw batches.
6. There is one generated material-evaluation model. A parameter-only or
   “data-only” material is a trivial evaluator, not a second runtime path.
7. Opaque shader objects do not cross generated aggregate/domain boundaries.
   Logical resources cross as descriptor-index `uint` values; the consuming
   operation materializes the typed texture/sampler/buffer object.
8. Generated material logic is called by handwritten renderer shader code and
   returns ordinary surface values.
9. Existing owners must be reused: ResourcePipeline, texture residency,
   descriptor domains, GPU Scene definitions/uploader/lifetime, pipeline cache,
   RenderScene publication and RenderingService lifecycle.
10. No second loader, descriptor cache, GPU material allocator, scene database,
    pipeline cache, command submission path, scheduler or retirement registry.

Early development policy was also explicit: cooked artifacts and caches are
disposable. When contracts change, support the new format, reject stale data and
recook. Do not accumulate compatibility decoders or deprecated parallel APIs.

## Material Phase 0 and Phase 1

### Phase 0: architecture scan — complete

The scan compared Vanguard with relevant Unreal material architecture. The useful
principles retained were separation of editor graph, typed compiler IR, cooked
runtime resource, render data and native shader/pipeline identity. Vanguard did
not copy UObject inheritance, Unreal's fixed material-property catalog, legacy
translator breadth, per-draw render proxies or live inheritance traversal.

### Phase 1: material interface and typed IR — complete

The implementation established:

- Shader-declared material interfaces and legal stages/capabilities.
- Typed backend-neutral material IR.
- Stable semantic/interface identity.
- Contract-derived material input/output validation.
- VSHADER, VPPL and VMAT compatibility metadata.
- Compact graph operations, values and diagnostics.
- Renderer-independent material authoring/compiler boundaries.

The material interface is declared by shader/domain contracts. It is not inferred
from a hard-coded engine surface struct.

## Material Phase 2: compilation, cooking, caching and preview

Phase 2 is complete at the offline/runtime boundary.

### Phase 2B: compiler core

The compiler gained typed graph lowering, poison propagation, safe constant
folding, aggregate handling and authored diagnostics. Invalid graph state does
not silently generate a replacement runtime material.

### Phase 2C: canonical cooked inputs and artifact pipeline

The current canonical material inputs became MPGI/MPLI/MVLI. Material compilation
produces independently cached VSHADER, VPPL and VMAT artifacts through the shared
DDC/build graph.

The production artifact path proves:

- Deterministic cooking and dependency identity.
- Byte-exact loose artifact reconstruction.
- Indexed VPAK assembly and reopening.
- Required, Optional and Soft dependencies.
- Corruption/incomplete closure rejection.
- Incremental recooking and reproducibility.

### Phase 2D: surface code generation and portability

Generated Slang and reflection-derived accessors support:

- Declared surface inputs and outputs.
- Scalars, vectors and matrices.
- Nested aggregates and arrays.
- Row/column-major matrices and matrix arrays.
- Texture/resource arrays.
- Reflected VMAT parameter packing.
- DXIL and SPIR-V agreement across the supported target matrix.

Phase 2D.4.1 corrected the resource-value ABI: resource identities remain integer
descriptor indices through generated values/aggregates, while consuming code
constructs typed opaque objects. This avoids returning opaque textures/samplers
inside ordinary generated output aggregates.

Preview retained the last valid result and cleaned up explicitly. It did not
become an editor or renderer.

## Material Phase 3: runtime loading and bindless GPU residency

Material Phase 3 is complete and frozen at the no-draw boundary.

```text
3A    loaded resource closure and runtime identity             COMPLETE / SEALED
3B    bindless resolution and atomic GPU materialization       COMPLETE / SEALED
3C.1  renderer-owned residency and technique admission         COMPLETE
3C.2  scene binding and atomic replacement                     COMPLETE
3C.3  service, cutover, shutdown and terminal lifecycle        COMPLETE
3C.4  production vertical and adversarial Phase 3 freeze       COMPLETE / FROZEN
```

Do not reopen these slices unless later integration exposes a concrete contract
defect. Material Phase 4 is deferred.

### Phase 3A: loaded resource closure

Production VSHADER, VPPL and VMAT resource objects load through the normal loose
or VPAK ResourcePipeline.

A VMAT root owns an immutable validated CPU closure. Validation covers target,
domain, interface/layout, accessor ABI, shader, techniques, dependencies and full
material-program-layout fingerprint compatibility.

No native GPU material is created merely because a resource object loaded.

Primary ownership lives in the existing shaders, pipelines, materials and
material-program-layout modules.

### Phase 3B: bindless resolution and atomic materialization

`MaterialResourceResolver` resolves reflected logical roles to retained typed
runtime references or exact legal fallbacks.

`MaterialMaterializer` combines:

- Reflected parameter bytes/words.
- Resolved resource-role records.
- Compact material layout identity.
- Existing descriptor-domain identities.
- Content-addressed GPU material identity.

It publishes the complete material compound through the existing shared GPU
Scene contribution transaction. Publication is all-or-nothing. There is no
per-material command list, private upload fence or separate submission owner.

The former `MaterialResourceLease` name was replaced with
`MaterialResourceReference`: it is a retained resource identity, not a temporary
rental and not a scene/component material asset handle.

Equal complete GPU material images share existing content-addressed definitions.
Rollback releases all acquired resources and table ranges.

### Phase 3C.1: residency and native technique admission

`MaterialResidencyRuntime` owns:

- Bounded generational residency records.
- Move-only coalesced `MaterialDemandHandle`s.
- Cancellation and failure progression.
- Lazy `MaterialTechniqueRequest`s.
- Exact-generation native shader/program sharing.
- Existing pipeline-cache requests.
- Fence-backed retirement after final demand/technique release.

Material residency and native pipeline readiness are distinct. A material can be
resident while an attachment-dependent technique remains pending.

The accepted performance corrections replaced high-water scans and repeated
linear lookups with exact-identity hash chains, dense active sets, binary
dependency lookup, direct dense-role admission, independent progression budgets
and deterministic complexity counters.

### Phase 3C.2: scene binding and replacement

`MaterialSceneBindingBridge` is a narrow bridge keyed by existing generational
`RenderProxyHandle`s. Its production responsibility is tracked decal material
binding.

Each binding owns at most one accepted active demand and one candidate demand.
A candidate replaces active ownership only after the matching shared GPU Scene
receipt is accepted. Failed, canceled or retried publication preserves the last
valid active material.

Explicit clear also preserves the active demand until clear acceptance. A bug was
fixed where superseding a pending clear could otherwise let an old clear receipt
erase a newer candidate.

Mesh-default materials were deliberately removed from this bridge. Mesh
residency/drawable binding owns the complete per-submesh material/phase closure;
a second partial mesh-material bridge would violate ownership.

### Phase 3C.3: RenderingService and terminal lifecycle

`RenderingService` owns in dependency order:

- Material program-layout registry.
- Material resource resolver.
- Material materializer.
- Shared pipeline cache.
- Material residency runtime.
- Material scene-binding bridge.

Per update, the service collects retirements, progresses texture/material
residency and materialization, and then progresses scene bindings.

One renderer-owned retirement cutover validates a complete graphics/compute/copy
`ResidencyFenceSet` and fans it out to material, texture, mesh and GPU Scene
owners. CPU completion or elapsed frames are not GPU completion.

Normal shutdown requires zero live bindings, demands, residencies, technique
requests, materializer operations, resource references and descriptor/cache
entries before reverse-order destruction.

Device loss uses a terminal abandon path after CPU work joins. It invalidates
owners in dependency order, makes handles stale, uses provider abandon callbacks
and does not call `WaitIdle` or fabricate fences.

RenderingService was corrected to declare its Resources dependency and clear
unreferenced native programs/fallbacks before the Resources service checks for
strong handles.

### Phase 3C.4: complete production vertical

The frozen headless production flow is:

```text
real loose or indexed-VPAK VMAT root
    -> ResourcePipeline VSHADER/VPPL/VMAT closure
    -> MaterialResourceResolver retained typed roles
    -> MaterialMaterializer atomic GPU Scene contribution
    -> MaterialResidencyRuntime resident GpuMaterialHandle
    -> MaterialSceneBindingBridge accepted scene binding
    -> concrete native technique request/readiness
    -> exact GPU material/resource/parameter readback
    -> distinct-root replacement and retirement
    -> zero-state service shutdown
```

This proof performs no draw.

Coverage includes loose/VPAK loading, exact closure identity, coalescing,
backpressure, Required failure, Optional fallback, pending/ready/failed technique
states, distinct-root last-valid replacement, cancellation, publication retry,
three-queue retirement and zero-state shutdown.

The final harness correction recognized that `RunFrame` can return while the
renderer CPU publication chain remains in flight. Before taking direct test
control of the GPU Scene publisher, the fixture now flushes preceding renderer
work and resolves contribution outcome. Debug timing had hidden the overlap;
Shipping correctly returned `Busy`. This was a proof-ordering fix, not a
production contract change.

### Historical Phase 3 validation

On 2026-09-02 the affected Debug matrix passed 27/27 and the meaningful Shipping
closure passed 8/8. Corrected Shipping `materialRuntimeServiceTests` also passed
ten consecutive reruns. `git diff --check` passed except line-ending notices.

This was not a green whole solution. A broader solution build still had unrelated
pre-existing failures in bootstrapImage, entitiesTests, runtime and editor.

Do not rerun these tests until the user requests the final batch.

## Deferred material Phase 4

Material instances, overrides and hot reload remain deferred. That work includes:

- Parent/instance flattening.
- Static versus dynamic overrides.
- `GpuMaterialSet` ownership for primitive-local overrides.
- Same-path overlapping hot-generation replacement.
- Editor-facing material service APIs.
- A later authored-to-runtime proof.

Do not begin Phase 4 automatically. The conversation deliberately returned to
mesh residency and geometry rendering first.

## Return to mesh residency: Phase 9A

After material Phase 3 froze, mesh residency was connected to material residency,
sealed render phases and immutable GPU Scene topology.

The production mapping is:

```text
VMESH LOD
    -> source submesh ordinal
    -> material slot
    -> exact retained VMAT generation
    -> resident GpuMaterial
    -> registered RenderPhaseKey technique
    -> immutable primitive/phase topology
```

`MeshResidencyManager` resolves default material slots and publishes immutable
renderable/LOD/primitive/material/phase definitions. `RenderingService` owns and
seals `RenderPhaseRegistry`, initializes mesh residency with the material runtime
and phase authority, and progresses materials before meshes.

The coarsest authored LOD is the initial complete anchor. Authored finer LOD
metadata may exist without claiming fine-LOD geometry residency.

`sourceSubmesh` is the source VMESH table ordinal. Never reinterpret it as the
authored 64-bit stable submesh ID.

9A ended before drawable residency, geometry/phase placement, shell/bin ownership,
components, RenderScene binding, culling or drawing.

Historical focused Debug/Shipping validation passed for 9A. That result does not
prove later drawable residency.

## Geometry rendering direction

The user clarified that the first real mesh rendering path must be GPU-driven.
An earlier direct-first recommendation was withdrawn.

The remaining order is:

```text
9B    RED batcher preparation + drawable mesh binding
9C    StaticMeshComponent and minimal scene authoring
9D    RenderScene mesh lifecycle and retained acceptance
10    per-view GPU visibility, batching and indirect drawing
```

Relevant RED batcher machinery should be ported 1:1 where it applies to CPU
preparation/recording: pass context, dependency ordering, compatible graphics
state, geometry binding, recording-local scratch and changed-state/flush
discipline.

Do not port RED's CPU-visible chunk sorter, CPU transform gathering, direct-draw
baseline, duplicate instance pools or unrelated geometry branches.

## Phase 9B.1: material/geometry draw preparation

9B.1 implementation is complete. Its combined verification fixture is deferred
to the user's final test batch.

Implemented work includes:

- Normal, mirrored and two-sided native pipeline requests through existing
  material technique/pipeline cache identity.
- Binding layouts and descriptor-domain identity in native pipeline keys.
- Renderer-owned static-surface shader domain.
- `static_surface_prefix.vsl` and `static_surface_suffix.vsl`.
- Vertex, depth and GBuffer entries.
- GPU Scene material/resource/parameter loading.
- Float and quantized position decode.
- Camera-relative transforms.
- Parameter-only and sampled generated evaluators.
- Non-uniform texture/sampler descriptor access.
- Shared masked opacity behavior between depth/color.
- Allocation-free `ValidateMeshDrawLayout`.
- Retained `MeshResidencyManager::PrepareDraw`.
- Direct authored ordinal and bounded primitive-phase lookup.

The renderer shader extension was deliberately changed from `.slang` to `.vsl`.
The four current files are:

```text
source/rendering/shaders/fullscreen_copy.vsl
source/rendering/shaders/gpu_scene_visibility.vsl
source/rendering/shaders/static_surface_prefix.vsl
source/rendering/shaders/static_surface_suffix.vsl
```

`gpu_scene_types.hlsli` remains an include. References were updated. Do not
restore the old `.slang` names.

The static draw ABI is a GPU-written 16-byte `MeshDrawInstance` record containing
instance, primitive, geometry and material identities. It is supplied at vertex
binding 15 as two `R32G32UInt` instance-rate attributes. Indirect `firstInstance`
selects the records. Phase 10 owns production of this stream.

`StaticSurfaceDrawContext` is a 24-byte per-view/pass push-constant context. It is
not per material.

The deferred 9B.1 fixture must eventually combine what separate tests proved:

1. Cook a material using the static-surface shaders.
2. Reference it from a cooked VMESH.
3. Load both through production services.
4. Prepare all required primitive/phase pipelines.
5. Verify compatibility, ownership, failure cleanup and release.

The user explicitly decided this missing item is integrated verification, not a
runtime subsystem and not a blocker for 9B.2.

## Phase 9B.2: interrupted current implementation

9B.2 is in progress and not complete.

The first source slice added:

```text
source/rendering/include/vanguard/rendering/render_geometry_batcher.hpp
source/rendering/src/render_geometry_batcher.cpp
source/rendering/docs/geometry-batcher-port.md
```

It also extended:

```text
source/rendering/include/vanguard/rendering/mesh_residency.hpp
source/rendering/src/mesh_residency.cpp
```

The current `RenderGeometryBatcher` is internal to `MeshResidencyManager`. It must
not become a sibling global registry/service.

### Shared shell/bin preparation now present

`GeometryShellKey` groups compatible shared recording state:

- Render phase.
- Exact native pipeline reference/generation.
- Vertex arena identity/generation.
- Index arena identity/generation.
- Index format.

`GeometryBinKey` adds exact geometry identity:

- Shell identity/generation.
- `GpuGeometryHandle`.
- Geometry allocation identity/generation.
- Vertex/index starts and counts.

Material parameter values and texture indices are not shell/bin keys. Different
values can share a destination when shader/pipeline and geometry state match.

The bounded tables use hash lookup, generational IDs and reference counts.
Equal keys coalesce. Incompatible fields split. Last release removes bins before
shells. Generation handling is intended to prevent stale ABA reuse.

### GeometryBatchLease

The move-only lease retains one primitive/phase preparation:

- Coalesced mesh demand.
- Normal and mirrored technique requests.
- Exact geometry placement.
- Normal/mirrored shell/bin destinations.
- Exact material.
- Source submesh and phase.

`Retain` shares immutable placement ownership. One release cannot invalidate
another retained lease.

Ownership mutation is main-thread-only. Joined recording jobs may read placement
while an owner keeps the lease alive.

Normal shutdown rejects live leases. Device abandonment, after recording jobs
join, invalidates leases and releases their retained requests while dependent
owners still exist. An outstanding invalid CPU lease remains resettable and
returns no placement.

### AcquireDrawPlacement

`MeshResidencyManager::AcquireDrawPlacement` validates that an unmodified
`MeshDrawPreparation` still matches retained topology:

- Correct manager and live demand/residency.
- `RenderableTopologySubmitted` state.
- Source primitive belongs to anchor LOD.
- Uploaded source-submesh mapping matches.
- Exact geometry handle/allocation/ranges match.
- Primitive participates in phase.
- Normal/mirrored requests resolve the primitive material.
- Both techniques are ready.

Pending/failure must leave preparation and output untouched. Partial shell/bin
acquisition rolls back. Success transfers preparation ownership into a lease.

Normal/mirrored entries naturally coalesce if their effective native state is
identical, such as compatible two-sided state. Do not force distinct entries.

### Critical unimplemented boundary

Current leases are unpublished preparation ownership. They do not yet publish:

- `GpuPrimitivePlacement`.
- `GpuPhasePlacement`.
- `GpuRenderableResidency`.
- Anchor resident LOD mask.
- A complete strong drawable mesh binding.

No mesh becomes drawable because one primitive/phase lease exists.

The interrupted source has not been compiled. It must be statically audited for
type/API/include/lifetime/rollback mistakes before extension. Compilation and
tests remain forbidden until the final batch request.

## Remaining 9B.2 work

1. Inspect the entire interrupted batcher and mesh-residency changes.
2. Correct source-level API, ownership, rollback or teardown defects found by
   static review.
3. Aggregate every required anchor-LOD primitive and registered phase.
4. Keep the aggregate pending until every required normal/mirrored technique is
   ready; fail without partial publication if any required item fails.
5. Stage complete `GpuPrimitivePlacement` and `GpuPhasePlacement` payloads through
   the existing shared GPU Scene contribution/publication mechanism.
6. Publish anchor `GpuRenderableResidency` eligibility only after complete
   placement acceptance and correct upload ordering.
7. Expose a strong drawable mesh binding retaining the exact VMESH generation,
   renderable, complete placement closure, leases, material/native pipelines and
   acceptance revision.
8. Let multiple users retain the binding independently.
9. Withdrawal must block new use before ownership enters retirement.
10. Retain accepted/in-flight dependencies until real graphics/compute/copy
    submission fences cover use.

Do not create private command lists, submissions, waits, upload coordinators,
retirement systems or fake fence values. A completed CPU job or elapsed frame
count is not GPU completion.

The existing GPU Scene runtime, uploader, lifetime and contribution acceptance
must remain authoritative. If mesh residency needs a narrow integration seam to
the shared mutable publication path, add that seam deliberately through existing
RenderingService ownership.

## Shared versus per-view rendering state

The user explicitly confirmed that culling and similar decisions are per view.

Shared across views:

- Mesh topology and exact resource generation.
- Resident geometry.
- Material records and native pipelines.
- Shell and bin definitions.
- Strong drawable binding.
- GPU Scene instance/renderable identities.

Per view or explicitly compatible pass:

- Camera/projection and view constants.
- Frustum and conservative candidates.
- Fine visibility.
- Selected resident LOD.
- Primitive/phase expansion.
- Visible bin membership.
- Counts and prefix offsets.
- Scatter output and `MeshDrawInstance` records.
- Indirect arguments/count ranges.

A shared bin is a destination definition, never one globally shared visible list.
Two cameras can share mesh residency while selecting different LODs or culling
the same instance differently.

This per-view work belongs to Phase 10, not 9B.2.

## Later phases

### 9C: StaticMeshComponent and scene authoring

`StaticMeshComponent` will be a thin consumer of the strong binding through the
existing placed/visual component and per-world `RenderingRuntime`. It must not
duplicate VMESH default materials, transforms, camera systems or residency.

### 9D: RenderScene mesh lifecycle

Connect strong bindings to mesh proxies and GPU publication with revision-safe
replace/clear acceptance, relink/bounds updates, stale completion rejection,
retained old ownership until replacement acceptance and real retirement.

The existing candidate writer must then be proven against stable GPU identities
and conservative moved bounds. Do not add a parallel RED proxy/chunk collector.

### Phase 10: GPU-driven rendering

Use existing spatial candidate APIs and `GpuVisibilityPlanBuilder`, then implement
per-view GPU visibility, resident LOD selection, primitive/phase expansion,
bounded count/prefix/scatter, GPU-written draw records and indexed indirect
recording through the existing Render Graph/frame path.

The first scene pixel must come from this path, not a disposable direct renderer.

## Validation policy and historical evidence

Historical passes recorded above remain valid only for their original source
checkpoints. They do not prove the current interrupted 9B.2 source.

Current rule remains:

```text
NO compilation
NO test execution
NO writing tests
until the user explicitly requests the final batch
```

When that batch is requested, use Premake and serialized target builds where
concurrent agents could cause PDB collisions. Report affected-target results
separately from whole-solution status.

## Fresh-thread resume prompt

```text
Work in D:/ENGINE. Read
docs/development/material-residency-conversation-backup.md and
source/rendering/docs/geometry-batcher-port.md completely, then inspect the
current dirty worktree and interrupted 9B.2 source.

Resume Phase 9B.2: statically audit the partial shell/bin/lease implementation,
then continue complete anchor-LOD placement publication, strong drawable mesh
binding, withdrawal and retirement integration. Preserve existing material,
pipeline, GPU Scene, mesh-residency, RenderScene and RenderingService owners.

9B.1 implementation is complete; its combined vertical fixture is deferred and
does not block 9B.2. Do not compile, execute tests or write tests until I
explicitly request the final batch. Preserve unrelated changes in the shared
dirty worktree. Do not restore renderer shader names from .vsl to .slang.
```

## Current authority order

When chat wording conflicts or is incomplete, use this order:

1. Current user instruction.
2. Current source and ownership contracts.
3. `source/rendering/docs/geometry-batcher-port.md` for 9B.2.
4. This conversation backup.
5. `docs/development/material-runtime-resume-checkpoint.md` for frozen Phase 3.
6. `docs/development/geometry-rendering-study.md` for 9B-10 design.
7. `docs/development/material-system-design.md` and study plan.
8. Historical chat/session JSONL only for missing exact wording.

Do not infer completion from an old heading. State implementation and validation
boundaries precisely at every handoff.
