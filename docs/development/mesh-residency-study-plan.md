# Vanguard Mesh Residency, Geometry Allocation, and GPU Batching Study Plan

Date: 2026-08-28

Status: Study complete; implementation approval pending. No production
implementation begins until the user approves the synthesized design and
execution plan.

Current progress (2026-08-28): Phases 0–5 are complete. Vanguard and Bevy
are pinned by Git revision; the non-Git RED and Unreal snapshots are pinned by
source version/path plus study-corpus hashes in `mesh-residency-design.md`.
Phase 1 locked end-to-end identity, ownership, request/cancellation, thread
authority, and teardown rules. Phase 2 retained `vmesh` V1 and locked the mesh
resource, owned page source, LOD install-set, and asynchronous page-read
contract. Phase 3 locked non-moving fixed-function arena chunks, transactional
LOD installation, graphics-queue upload publication, bounded residency,
LOD-atomic eviction, and multi-queue fence retirement. Phase 4 locked one
canonical renderable identity, mutable per-LOD placement publication, GPU LOD
selection and delayed streaming feedback, stable CPU batch shells, and GPU-built
counted MDI without a visible-set readback.
Phase 5 reconciled the complete pipeline, locked the next GPU Scene ABI,
publication and retirement transactions, capacity/failure policy, diagnostics,
and the file-level implementation sequence. Production implementation remains
prohibited until the user explicitly approves the synthesized design and
execution plan.

## 1. Objective

This study will define Vanguard's production static-mesh path end to end:

```text
serialized component/reference
  -> ResourcePipeline request and dependency retention
  -> .vmesh metadata and page streaming
  -> bounded CPU/GPU residency
  -> renderer-global fixed-function vertex/index arenas
  -> GPU Scene geometry, LOD, primitive, and material records
  -> visibility and desired-LOD feedback
  -> binding-compatible batch sets
  -> GPU-generated indirect commands and counts
  -> CPU-recorded MDI calls without GPU-to-CPU readback
  -> fence-safe eviction and allocation reuse
```

The work is deliberately split into evidence-driven phases. Each phase studies
the relevant engine sources, records findings, makes only the decisions enabled
by that evidence, and updates the design and execution documents. Implementation
starts only after the final synthesis gate.

## 2. Locked Starting Direction

The study may refine details, but it begins with these already-selected
constraints:

- Vanguard uses fixed-function vertex input (`BindVertexBuffers`), not general
  SSBO/manual vertex pulling, for the normal static-mesh path.
- Vertex attributes may be split across multiple vertex buffers. Every stream
  in one `VertexArenaSet` must use a synchronized logical vertex range because
  indexed indirect drawing provides one `baseVertex` for the draw.
- Geometry uses one logical global pool backed by a controlled number of large
  physical arena chunks. A live, published allocation is not routinely moved.
- Vertex and index capacity may exhaust independently. Placement therefore
  identifies both the vertex arena set and the index arena instead of pretending
  they are necessarily one physical allocation.
- An MDI batch set is binding-compatible work. Its key includes at least render
  phase, pipeline bucket, vertex arena set, index arena/buffer, index format, and
  fixed topology/raster/depth state not already represented by the pipeline.
- The CPU owns the stable batch-set shell and records pipeline/buffer bindings
  plus MDI calls. The GPU performs visibility, LOD selection, compaction/binning,
  indirect argument generation, and count generation. No per-frame GPU readback
  is required to discover which MDI calls to issue.
- `.vmesh` pages are storage and streaming units. Once installed, shaders and
  fixed-function input use direct physical arena ranges; Vanguard V1 does not
  add a virtual GPU address translation on every vertex/index access.
- Nanite is a reference for residency lifecycle, minimum/root residency,
  bounded pools, requests, feedback, prioritization, eviction, and fallback. It
  is not authorization to copy Nanite's cluster rasterizer or virtual-addressed
  geometry access model.
- Existing `GpuRenderable`, `GpuLod`, `GpuPrimitive`, `GpuPhaseParticipation`,
  `GpuGeometryRange`, `GpuVertexStream`, `GpuPositionDecode`, and GPU material
  tables remain the canonical renderer vocabulary. The design must not create a
  second mesh/renderable identity system.

If a phase finds that a locked direction is technically impossible, it must
record the concrete evidence and stop for an explicit architecture decision.

## 3. Study Outputs

This plan is the permanent phase checklist. The study will create and maintain:

- `docs/development/mesh-residency-design.md`
  - source-backed findings;
  - accepted, adapted, and rejected reference-engine ideas;
  - state machines, ownership, data layouts, and invariants;
  - an open-question and decision log.
- `docs/development/mesh-residency-execution-plan.md`
  - implementation consequences discovered by each study phase;
  - dependency ordering, migrations, validation, and rollout;
  - final implementation stages only after design synthesis.

The execution plan may accumulate constraints during the study, but it must not
be treated as authorization to implement partial architecture.

## 4. Evidence Rules

Every study phase must follow these rules:

1. Record the exact engine version/revision being studied.
2. Cite local file path, symbol, and useful line range for every architectural
   claim. Memory or a class name alone is not evidence.
3. Mark every borrowed idea as `Copy`, `Adapt`, `Reject`, or `Vanguard-specific`.
4. Distinguish current code from proposed code.
5. Preserve fixed-function vertex-input and RenderDoc inspectability unless an
   explicit later decision changes them.
6. Do not infer a production feature merely because a type name suggests it.
7. Do not fill unavailable Unreal source gaps from recollection. Mark the
   question blocked until the matching source/version is available.
8. End each phase by updating both output documents, listing unresolved
   questions, checking its exit gate, and stopping before the next phase.
9. Do not modify production code during these studies.

## 5. Reference Source Index

Paths below are the initial study corpus. A phase may add a directly related
file when source navigation proves it necessary, but it must record why.

### 5.1 Unreal Engine / Nanite

Pinned local source status on 2026-08-28:

```text
D:\UnrealEngine
Unreal Engine 5.8.1
Build.version Changelist: 0
Build.version CompatibleChangelist: 55116800
BranchName: UE5
```

The snapshot has no Git metadata and reports no exact source changelist, so the
Phase 0 design document records a SHA-256 manifest for the core corpus. Verified
source files and symbols include:

```text
Engine/Source/Runtime/Engine/Public/Rendering/NaniteStreamingManager.h
  Nanite::FStreamingManager
  FStreamingRequest, FPendingPage, FRegisteredPage, FResidentPage,
  FRootPageInfo, dependency/LRU/request/install state

Engine/Source/Runtime/Engine/Private/Rendering/NaniteStreamingManager.cpp
  registration, request intake, dependency closure, prioritization, LRU
  replacement, IO readiness, install/uninstall, pool resize, statistics

Engine/Source/Runtime/Engine/Public/Rendering/NaniteResources.h
Engine/Source/Runtime/Engine/Private/Rendering/NaniteResources.cpp
  FPageInfo, FPageStreamingState, FResources, cooked hierarchy/page state

Engine/Source/Runtime/Engine/Internal/Nanite/NaniteFixupChunk.h
Engine/Source/Runtime/Engine/Private/Nanite/NaniteStreamingPageUploader.h
Engine/Source/Runtime/Engine/Private/Nanite/NaniteStreamingPageUploader.cpp
Engine/Shaders/Private/Nanite/NaniteStreaming.usf
Engine/Shaders/Private/Nanite/NaniteStreaming.ush
Engine/Shaders/Private/Nanite/NaniteTranscode.usf
  page fixups, upload, transcode, scatter/install support

Engine/Source/Runtime/Engine/Public/StaticMeshResources.h
Engine/Source/Runtime/Engine/Private/StaticMeshResources.cpp
Engine/Source/Runtime/Engine/Public/Streaming/RenderAssetOwnerStreamingState.h
Engine/Source/Runtime/Engine/Internal/Streaming/RenderAssetUpdate.inl
Engine/Source/Runtime/Engine/Private/Streaming/RenderAssetUpdate.cpp
  classic static-mesh LOD resources and render-asset streaming lifecycle

Engine/Source/Runtime/Renderer/Private/MeshDrawCommands.h/.cpp
Engine/Source/Runtime/Renderer/Public/InstanceCulling/InstanceCullingContext.h
Engine/Source/Runtime/Renderer/Private/InstanceCulling/
Engine/Source/Runtime/Renderer/Private/GPUScene.h/.cpp
Engine/Shaders/Private/InstanceCulling/
Engine/Shaders/Private/GPUScene/
  stable draw state, GPU Scene, instance culling, command construction,
  indirect arguments, and submission

Engine/Source/Runtime/Renderer/Private/Nanite/NaniteCullRaster.h/.cpp
Engine/Shaders/Private/Nanite/NaniteClusterCulling.usf
  inspect only where culling emits residency/fallback signals
```

Later phases must follow symbols from these verified entry points and append any
additional files they inspect. Paths are specific to this 5.8.1 snapshot and
must not be assumed stable across Unreal versions.

### 5.2 Bevy Classic Mesh Path

Pinned local checkout:

```text
C:\Users\Ark\Documents\bevy
version observed: 0.19.0-dev
```

Core geometry allocation:

- `crates/bevy_render/src/mesh/allocator.rs`
- `crates/bevy_render/src/slab_allocator.rs`
- `crates/bevy_render/src/diagnostic/mesh_allocator_diagnostic_plugin.rs`

Batching and indirect work:

- `crates/bevy_render/src/batching/gpu_preprocessing.rs`
- `crates/bevy_render/src/batching/no_gpu_preprocessing.rs`
- `crates/bevy_render/src/render_phase/mod.rs`
- `crates/bevy_pbr/src/render/mesh.rs`
- `crates/bevy_pbr/src/render/mesh_preprocess.wgsl`
- `crates/bevy_pbr/src/render/build_indirect_params.wgsl`
- `crates/bevy_pbr/src/render/reset_indirect_batch_sets.wgsl`
- `crates/bevy_pbr/src/render/unpack_bins.wgsl`

Meshlet path, studied only as an explicit contrast to Vanguard's fixed-function
path:

- `crates/bevy_pbr/src/meshlet/meshlet_mesh_manager.rs`
- `crates/bevy_pbr/src/meshlet/persistent_buffer.rs`
- `crates/bevy_pbr/src/meshlet/persistent_buffer_impls.rs`
- `crates/bevy_pbr/src/meshlet/asset.rs`
- `crates/bevy_pbr/src/meshlet/from_mesh.rs`

The Bevy study must cover `MeshSlabs`, pending versus resident allocations,
staged allocation/deallocation, slab growth/copy behavior, dedicated-large
allocations, `MeshBatchSetCompareData`, geometry bin keys, indirect batch-set
base/count records, and GPU preprocessing limits. Vanguard is not expected to
copy Bevy's live-buffer growth/copy policy or storage-buffer meshlet renderer.

### 5.3 RED Engine

RED is the integration and ownership reference, not the global geometry-arena
reference. Inspect exact local revisions under:

```text
D:\root\R6.Root\Mainline\dev\src\common\
```

World/component/runtime integration:

- `world/include/runtimeSystemRendering.h`
- `world/src/runtimeSystemRendering.cpp`
- `world/include/meshNode.h`
- `world/src/meshNode.cpp`
- `world/include/meshNodeInstance.h`
- `world/src/meshNodeInstance.cpp`
- `world/include/prefabProxyMeshNode.h`
- `world/src/prefabProxyMeshNode.cpp`
- `world/include/prefabProxyMeshNodeInstance.h`
- `world/src/prefabProxyMeshNodeInstance.cpp`
- `world/src/runtimeSystemNodeStreaming.cpp`
- directly referenced world streaming proxy/grid/resource-monitor files

Mesh resource, appearances, and material relationships:

- `resourceMesh/include/mesh.h`
- `resourceMesh/include/meshChunk.h`
- `resourceMesh/include/meshAppearance.h`
- `resourceMesh/include/meshMaterialBuffer.h`
- `resourceMesh/include/meshMaterialBank.h`
- `resourceMesh/include/meshDataViewRO.h`
- `resourceMesh/include/resourceMeshPublic.h`
- corresponding implementations under `resourceMesh/src/`
- `meshParamGpuBuffer` declarations/implementation when reached by the above

For every RED path, verify the file exists before citing it. Follow concrete
calls into resource loading, streaming admission, render-proxy creation, and
destruction rather than treating serialized component fields as runtime
ownership.

### 5.4 Vanguard Current Code

Resource and streaming foundation:

- `source/resources/include/vanguard/resources/resource_pipeline.hpp`
- `source/resources/src/resource_pipeline.cpp`
- `source/resources/include/vanguard/resources/resources.hpp`
- `source/resources/src/resource_registry.cpp`
- `source/resources/tests/resource_pipeline_tests.cpp`
- `source/streaming/include/vanguard/streaming/streaming.hpp`
- `source/streaming/src/streaming.cpp`
- `source/streaming/tests/streaming_tests.cpp`
- `source/engine/include/vanguard/engine/resources_service.hpp`
- `source/engine/src/resources_service.cpp`
- `source/engine/include/vanguard/engine/resource_streaming_service.hpp`
- `source/engine/src/resource_streaming_service.cpp`
- `source/world/include/vanguard/world/streaming_grid.hpp`
- `source/world/src/streaming_grid.cpp`
- `source/world/include/vanguard/world/streaming_executor.hpp`
- `source/world/src/streaming_executor.cpp`

Mesh format, resource, cooker, and asset dependencies:

- `docs/formats/vmesh-format.md`
- `docs/migration/red-mesh-vmesh.md`
- `source/meshes/include/vanguard/meshes/meshes.hpp`
- `source/meshes/src/meshes.cpp`
- `source/meshes/tests/meshes_tests.cpp`
- `source/meshTools/include/vanguard/mesh_tools/mesh_tools.hpp`
- `source/meshTools/src/mesh_tools.cpp`
- `source/meshTools/tests/mesh_tools_tests.cpp`
- `source/materials/include/vanguard/materials/materials.hpp`
- `source/materials/src/materials.cpp`
- `source/assets/include/vanguard/assets/assets.hpp`
- `source/assets/include/vanguard/assets/asset_graph.hpp`
- `source/assets/include/vanguard/assets/asset_index.hpp`
- `source/assets/include/vanguard/assets/package_planner.hpp`
- `source/assets/include/vanguard/assets/package_set_assembly.hpp`
- corresponding `source/assets/src/` implementations and tests

Renderer and GPU Scene:

- all `gpu_scene_*` headers, sources, shaders, and tests under
  `source/rendering/`
- `source/rendering/include/vanguard/rendering/render_scene.hpp`
- `source/rendering/src/render_scene.cpp`
- `source/rendering/include/vanguard/rendering/render_scene_gpu.hpp`
- `source/rendering/src/render_scene_gpu.cpp`
- `source/rendering/private/vanguard/rendering/render_scene_gpu_read.hpp`
- `source/rendering/private/vanguard/rendering/render_scene_spatial.hpp`
- `source/rendering/include/vanguard/rendering/render_phase.hpp`
- `source/rendering/src/render_phase.cpp`
- `source/rendering/include/vanguard/rendering/pipeline_cache.hpp`
- `source/rendering/src/pipeline_cache.cpp`
- `source/rendering/include/vanguard/rendering/render_pipeline_factory.hpp`
- `source/rendering/src/render_pipeline_factory.cpp`
- `source/rendering/include/vanguard/rendering/frame_renderer.hpp`
- `source/rendering/src/frame_renderer.cpp`
- `source/rendering/include/vanguard/rendering/render_command_system.hpp`
- `source/rendering/src/render_command_system.cpp`
- `source/rendering/include/vanguard/rendering/visibility_feedback.hpp`
- `source/rendering/src/visibility_feedback.cpp`
- `source/rendering/docs/shader-pipeline-runtime.md`

RHI and lifetime primitives:

- `source/rhi/include/vanguard/rhi/rhi.hpp`
- `source/rhi/include/vanguard/rhi/rhi_types.hpp`
- `source/rhi/include/vanguard/rhi/rhi_backend.hpp`
- `source/rhi/src/rhi.cpp`
- `source/rhi/nvrhi/src/common_backend.cpp`
- `source/rhi/nvrhi/src/d3d12_backend.cpp`
- `source/rhi/nvrhi/src/resource_lifetime.cpp`
- corresponding private headers and backend tests

Residency consumer boundary only:

- `source/entities/include/vanguard/entities/component.hpp`
- `source/entities/include/vanguard/entities/rendering_runtime.hpp`
- `source/entities/src/rendering_runtime.cpp`
- `docs/development/world-rendering-resume-checkpoint.md`

These files are inspected only to validate the residency request/release,
readiness, binding, and detach API used by existing consumers. Designing a
`StaticMeshComponent`, its serialization/materialization, transforms, or general
world/render lifecycle is outside this study.

## 6. Study Phases

### Phase 0 — Source Pinning, Vocabulary, and Baseline

Study:

- Restore/acquire the intended Unreal source worktree and record its revision.
- Record Bevy, RED, and Vanguard revisions or workspace state.
- Generate a verified Unreal symbol-to-file index for the discovery targets.
- Inventory current Vanguard types, handles, threads, services, GPU tables, and
  relevant backend limits.
- Reconcile vocabulary: asset, resource, mesh, LOD, primitive, page, allocation,
  placement, arena, slab, batch set, bin, indirect command, instance, proxy, and
  component.

Deliverables:

- source manifest and revision table;
- current Vanguard end-to-end ownership sketch;
- locked constraints and conflicts;
- baseline design/execution document skeletons.

Exit gate: all sources required for the next phases are readable and versioned;
unverified Unreal paths have been replaced by exact files and symbols.

### Phase 1 — References, Handles, and End-to-End Ownership

Study:

- RED mesh/prefab component references, resource ownership, runtime proxy
  creation, streaming registration, and teardown.
- Vanguard `ResourcePipeline`, resource registry, materializer, component
  runtime, rendering runtime, world/cell streaming, and RenderScene admission.
- Unreal render-asset/resource ownership and release only where it clarifies
  cross-thread lifetime.

Decide:

- serialized reference versus loaded resource versus residency request versus
  renderer handle;
- owners of metadata, page bytes, GPU allocations, GPU Scene definitions, and
  component/proxy references;
- stable ID/generation rules;
- request coalescing, cancellation, failure propagation, and detach ordering;
- thread/queue ownership for every transition.

Exit gate: every object in the target pipeline has one owner, explicit
references, legal state transitions, and a destruction path with no implicit
global lifetime.

### Phase 2 — Mesh Format, Resource Object, and Streaming Contract

Study:

- Vanguard format/cooker/mesh/material files together with the resource
  pipeline, IO services, dependency loading, and tests.
- RED mesh chunks, appearances, material slots, resource references, and
  data-view boundaries.
- Unreal cooked Nanite metadata/pages and classic static-mesh LOD separation,
  plus registration, request generation, IO, cancellation, and removal.

Decide:

- metadata resident at resource-open time and the production resource/decoder
  boundary;
- LOD, primitive, material-slot, vertex-stream, index-format, bounds, and
  position-decode representation;
- page identity, contents, alignment, compression, dependency graph,
  validation, and drawable fallback;
- `.vmesh` to `.vmat` reference behavior and stable identities across recooks;
- metadata-only loads, page loads, request keys, coalescing, priorities,
  cancellation, retry, and failure semantics;
- ownership and CPU budgets for compressed/decompressed page data;
- the deterministic handoff from decoded pages to GPU installation.

Exit gate: metadata and pages validate independently, a fallback is identifiable,
and every asynchronous transition has one authority and deterministic GPU-install
handoff.

### Phase 3 — Geometry Residency Core

This phase combines physical allocation, upload, publication, budgets, eviction,
and fence retirement because those decisions form one correctness boundary.

Study:

- Bevy `MeshAllocator`, `SlabAllocator`, staged commits, diagnostics, slab
  growth, large allocations, and empty-slab retirement.
- Unreal page pools, uploads, fixups, install/publication, dependency pins,
  eviction, and resource removal.
- Vanguard RHI buffer/copy/fence APIs, upload queues, GPU Scene writes, device
  limits, and resource lifetime queues.

Decide:

- `VertexArenaSetId`, `IndexArenaId`, allocation generations, synchronized
  split-vertex ranges, independent index ranges, and fixed-function binding;
- alignment, chunk/growth policy, fragmentation control, arena-count limits,
  oversize allocation, and staged allocation/free;
- staging/copy batching, reservation, rollback, and all-or-none publication;
- CPU page-cache and GPU geometry budgets, fallback pins, dependency/in-flight
  pins, eviction score, hysteresis, and pressure behavior;
- the exact multi-frame/queue fence after which ranges and handles may be
  reused.

Exit gate: residency is bounded, publication is atomic, failure leaks nothing,
split streams remain synchronized, and no GPU-visible allocation is reused too
early.

Completion record (2026-08-28): passed. The design now fixes non-moving
layout-specific vertex arena sets, independent format-specific index arenas,
transactional graphics-queue upload/publication, explicit memory categories,
LOD-atomic eviction with fallback pins, and graphics/compute/copy fence epochs.
No production code was changed, and Phase 4 was not started.

### Phase 4 — GPU Consumption, Feedback, Batching, and MDI

Study:

- Vanguard GPU Scene tables/shaders, RenderScene handle flow, visibility,
  render phases, pipeline cache, command system, and indirect-draw support.
- Bevy allocation references, batch-set/bin keys, GPU preprocessing, indirect
  argument/count generation, and CPU fallback.
- Unreal GPU Scene, hierarchy/LOD feedback, mesh draw commands, and instance
  culling only where they clarify residency consumption.

Decide:

- canonical renderable/LOD/primitive identity and the split between immutable
  asset definition and mutable resident placement;
- resident masks, desired-versus-best-resident LOD, fallback selection,
  feedback encoding, deduplication, overflow, latency, and prewarming;
- authoritative fixed-function batch key, known CPU batch-set shells, geometry
  bins, GPU cull/compact/argument passes, and per-phase/view handling;
- MDI limits/fallback and proof that CPU recording never waits for visible-set
  readback or performs a per-frame visible-object sort.

`StaticMeshComponent`, world streaming, and RenderScene are consumers at this
boundary, not subjects of a separate residency study. Inspect only enough of
them to validate the handles, request/release calls, readiness/fallback state,
and detach semantics required by the residency API. Component schema,
materialization, transform propagation, and general world/render integration
remain outside this study.

Exit gate: GPU Scene, visibility, LOD feedback, residency, batch shells, and MDI
use one canonical identity with no hidden CPU readback round trip.

Completion record (2026-08-28): passed. `GpuRenderableHandle` remains the only
GPU mesh identity. Immutable renderable/LOD/primitive/phase topology is paired
with mutable, generation-checked primitive/phase placement and a 64-bit resident
LOD mask. GPU visibility selects the desired and best coarser resident LOD,
aggregates bounded future-frame streaming feedback, expands stable geometry bins,
and builds indirect arguments/counts. The CPU visits stable binding-compatible
shells and records counted indexed MDI calls without reading or sorting the
current visible set. Back-to-front transparency is explicitly outside this MDI
path. No production code was changed, and Phase 5 was not started.

### Phase 5 — Synthesis, Diagnostics, and Implementation Gate

Study:

- Re-read Phases 0–4 and Vanguard code at every proposed modification boundary.
- Inspect Bevy/Unreal diagnostics and tests only where needed to validate the
  chosen allocator, streaming, and batching contracts.
- Resolve contradictions among format, ownership, streaming, residency,
  publication, GPU Scene, feedback, and MDI decisions.

Produce:

- final ownership/reference and residency state diagrams;
- `.vmesh` and GPU ABI changes;
- allocator, upload, publication, eviction, and fence algorithms;
- GPU visibility/feedback/MDI sequence;
- the minimal component/world/RenderScene consumer API, without expanding into
  a component-system design;
- counters, debug views, RenderDoc expectations, failure matrix, fault
  injection, and unit/integration/GPU tests;
- exact file-level implementation stages, migrations, completion criteria, and
  rollback points.

Final gate: implementation starts only after every correctness-critical
ownership, publication, and fence boundary is specified, failures are observable
and testable, and the user approves the synthesized design and execution plan.

Completion record (2026-08-28): synthesis complete. The design retains `vmesh`
V1, versions GPU Scene to layout 5, keeps one canonical `GpuRenderableHandle`,
uses owner-indexed mutable placement tables, and specifies atomic LOD
installation/eviction with graphics/compute/copy fence retirement. It also
specifies the minimal residency-to-proxy binding, the GPU visibility/feedback/
MDI sequence, bootstrap capacity policy, diagnostics, fault injection, tests,
and ordered file-level implementation stages. No production code was changed.
The final architectural gate is passed; the separate user-approval gate is
still pending.

## 7. Per-Phase Report Template

Each phase appends this structure to the design document:

```text
Phase N — title
Sources inspected
Observed architecture
Copy / Adapt / Reject / Vanguard-specific table
Decisions locked
Alternatives rejected and why
Data structures and state transitions
Concurrency, lifetime, and failure implications
Tests/diagnostics required
Open questions
Execution-plan consequences
Exit-gate result
```

The phase ends after this report and the two output documents are updated. The
next phase begins only on a later explicit request.

## 8. Implementation Boundary

The studies are complete only when the system is specified as one coherent
pipeline rather than independent allocator, streamer, consumer binding, and MDI
features. In particular, implementation must not begin with an allocator alone:
the allocation handle, publication boundary, GPU Scene reference, fallback LOD,
batch-set key, and retirement fence must already agree.

The first implementation task will be selected from the final execution plan,
not inferred from whichever reference-engine subsystem was studied last.
