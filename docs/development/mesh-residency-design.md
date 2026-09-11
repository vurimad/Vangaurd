# Vanguard Mesh Residency Design Study

Date: 2026-08-28

Status: Study complete. Phases 0–5 are complete; implementation approval is pending.

This document records evidence and the final synthesized specification produced
by `mesh-residency-study-plan.md`. It does not authorize implementation until
the user approves it together with the execution plan.

## Phase 0 — Source Pinning, Vocabulary, and Baseline

### Phase result

```text
Study work: complete
Exit gate: passed
Blocker: none
Next allowed work: Phase 1 on a later explicit request
Phase 1 state when Phase 0 closed: not started
```

### Sources inspected

#### Vanguard

- Repository: `D:\ENGINE`
- Branch: `main`
- Base commit: `bbb197171989ec56a5cd5dd0d0f72301bc37e71a`
- Live workspace at inspection: 329 tracked changes and 52 untracked paths.
- Authority for this study: the live workspace, not the base commit alone.

Primary Phase 0 evidence:

- `source/resources/include/vanguard/resources/resources.hpp:146-425`
- `source/resources/include/vanguard/resources/resource_pipeline.hpp:10-222`
- `source/resources/src/resource_pipeline.cpp:40-837`
- `source/streaming/include/vanguard/streaming/streaming.hpp:16-166`
- `source/streaming/src/streaming.cpp:788-904`
- `source/meshes/include/vanguard/meshes/meshes.hpp:253-402`
- `source/meshes/src/meshes.cpp:1135-1608`
- `docs/formats/vmesh-format.md:1-71`
- `source/rendering/include/vanguard/rendering/gpu_scene_types.hpp:10-269`
- `source/rendering/include/vanguard/rendering/gpu_scene_tables.hpp`
- `source/rendering/include/vanguard/rendering/gpu_scene_lifetime.hpp:8-193`
- `source/rendering/include/vanguard/rendering/gpu_scene_definitions.hpp:7-158`
- `source/rendering/include/vanguard/rendering/gpu_scene_runtime.hpp:9-101`
- `source/rendering/include/vanguard/rendering/render_scene_gpu.hpp`
- `source/rendering/include/vanguard/rendering/render_command_system.hpp:66-218`
- `source/rendering/include/vanguard/rendering/frame_renderer.hpp:8-32`
- `source/rendering/include/vanguard/rendering/render_phase.hpp:7-210`
- `source/rendering/docs/gpu-scene-implementation.md:3-207`
- `source/entities/include/vanguard/entities/component.hpp:21-150`
- `source/entities/include/vanguard/entities/rendering_runtime.hpp:12-125`
- `source/entities/src/rendering_runtime.cpp:290-370`
- `docs/development/world-rendering-resume-checkpoint.md:25-86`
- `source/rhi/include/vanguard/rhi/rhi.hpp:137-219`
- `source/rhi/include/vanguard/rhi/rhi_types.hpp:565-655`
- `source/rhi/include/vanguard/rhi/rhi_types.hpp:1244-1305`
- `source/rhi/include/vanguard/rhi/rhi_types.hpp:1552-1592`

The working tree was already dirty before this study. No existing production
change is attributed to this phase.

#### Bevy

- Repository: `C:\Users\Ark\Documents\bevy`
- Branch: `main`
- Commit: `71ad427aa70292ff18b9d78997c04f75404ec348`
- Workspace version: `0.19.0-dev`; Rust version: `1.95.0`.
- One tracked modification exists in
  `crates/bevy_render/src/renderer/mod.rs`; the allocator and batching files in
  the Phase 0 corpus are not reported modified.

Verified initial evidence locations:

- `crates/bevy_render/src/mesh/allocator.rs:114` — `MeshSlabs`.
- `crates/bevy_render/src/slab_allocator.rs:146-189` — allocator settings and
  growth factor.
- `crates/bevy_render/src/slab_allocator.rs:315-320` — resident and pending
  allocation maps.
- `crates/bevy_render/src/slab_allocator.rs:800-850` — physical slab growth and
  old-buffer copy.
- `crates/bevy_pbr/src/render/mesh.rs:2794-2813` — mesh batch-set comparison
  data and separate per-bin mesh identity.
- `crates/bevy_pbr/src/render/mesh.rs:4494-4612` — counted and fixed-count MDI
  recording paths.
- `crates/bevy_render/src/render_phase/mod.rs:1092-1181` — capability-aware
  multidraw batch-set preparation.

Phase 0 only pins this corpus. Detailed Bevy allocator and batching decisions
belong to Phases 3 and 4.

#### RED

- Snapshot root:
  `D:\root\R6.Root\Mainline\dev\src\common`
- No Git repository was found at or above this root.
- No usable local Perforce revision marker was found during Phase 0.
- The required world and `resourceMesh` files listed in the study plan were
  verified to exist.
- The 18-file Phase 0 RED corpus is pinned by this aggregate manifest hash:
  `2527FED54028DD3297679BF3F94093881BC637EB9B580EC148A3ECFB0AD0E75F`.

RED may be used as a content-pinned local source snapshot. Each later phase must
record exact files and hashes for additions to the corpus. If a Perforce
changelist or archival revision becomes available, record it as stronger
provenance without discarding the content manifest.

#### Unreal Engine

- Source root: `D:\UnrealEngine`
- Version: Unreal Engine 5.8.1.
- `Build.version`: `Changelist` 0, `CompatibleChangelist` 55116800,
  `BranchName` `UE5`.
- The snapshot has no Git metadata, so it is content-pinned rather than
  commit-pinned.
- The 12-file Phase 0 Unreal corpus is pinned by this aggregate manifest hash:
  `5E9B8141BB200753FE05BD2077768B1CD14FA0B8B799CEA3604AB61655141ED6`.

Verified Phase 0 entry points:

- `Engine/Source/Runtime/Engine/Public/Rendering/NaniteStreamingManager.h:54-408`
  — `FStreamingManager`, requests, pending/registered/resident/root pages,
  dependencies, LRU state, uploader ownership, install/uninstall APIs.
- `Engine/Source/Runtime/Engine/Private/Rendering/NaniteStreamingManager.cpp:57-198`
  — pool/root/install budgets and quality thresholds.
- `NaniteStreamingManager.cpp:950-1028` — page registration, dependency
  references, and unregistration.
- `NaniteStreamingManager.cpp:1331-1818` — uninstall, ready-page install, pool
  resize, hierarchy/page-buffer update.
- `NaniteStreamingManager.cpp:2023-2306` — IO readiness and async update.
- `NaniteStreamingManager.cpp:2442-2984` — GPU/explicit/parent requests,
  dependency closure, LRU selection, eviction, and page registration.
- `Engine/Source/Runtime/Engine/Public/Rendering/NaniteResources.h:228-452`
  — `FPageInfo`, `FPageStreamingState`, and `FResources`.
- `Engine/Source/Runtime/Engine/Internal/Nanite/NaniteFixupChunk.h`
  — install/uninstall fixup representation.
- `Engine/Source/Runtime/Engine/Private/Nanite/NaniteStreamingPageUploader.h/.cpp`
  and `Engine/Shaders/Private/Nanite/NaniteStreaming.usf`/
  `NaniteTranscode.usf` — page upload/transcode path.
- `Engine/Source/Runtime/Engine/Public/StaticMeshResources.h` and
  `Engine/Private/StaticMeshResources.cpp` — classic fixed-function mesh LOD
  resources.
- `Engine/Source/Runtime/Renderer/Private/MeshDrawCommands.h/.cpp`,
  `Renderer/Public/InstanceCulling/InstanceCullingContext.h`, and
  `Renderer/Private/GPUScene.h/.cpp` — classic draw state, GPU Scene, instance
  culling, indirect construction, and submission entry points.

Phase 0 establishes the exact corpus only. Detailed Nanite lifecycle findings
remain assigned to the consolidated Phases 2–5.

### Observed Vanguard architecture

#### Process-owned resource identity and loading

`ResourceRegistry` owns loaded `ResourceObject` identities. Strong and weak
handles contain a registry pointer, slot, generation, and typed resource key
(`resources.hpp:221-293`). Registry states are explicit:

```text
Unloaded -> Queued -> Loading -> Loaded
                       |          |
                       v          v
                 Failed/Cancelled Reloading/Evicting
```

The registry coalesces concurrent requests for the same key and refuses
shutdown with live requests or handles (`resources.hpp:382-405`). This is the
existing process-level object lifetime system and should not be duplicated by a
mesh-specific asset handle.

`ResourcePipeline` adds dependency discovery, dependency retention,
prioritization, asynchronous preparation, construction, cancellation, and
failure traces. A caller-owned `PipelineRequest` cancels only its own interest;
shared root requests and dependency edges keep the underlying operation alive
(`resource_pipeline.hpp:142-172`). Dependency discovery and construction run as
Jobs work, and shared operation state is protected by explicit locks/events and
counters (`resource_pipeline.cpp:119-205`, `446-837`).

`ResourcesService` owns the process registry and pipeline; callers receive
non-owning access and must release requests/handles before service shutdown
(`resources_service.hpp:8-18`).

#### Generic streaming versus pageable mesh access

`ResourceStreamer` supplies loose-file/package sources and decoder callbacks.
Its public decoder receives one contiguous logical resource image
(`streaming.hpp:49-64`), and the current construction path invokes the decoder
with `logicalData` and `logicalSize` (`streaming.cpp:788-843`). Its default
staging budget is 512 MiB and maximum resource size is 2 GiB
(`streaming.hpp:16-22`).

The `.vmesh` reader has a different useful capability. `MeshFile::Open` reads
and retains metadata records, while `ReadPage` retrieves one geometry page
(`meshes.hpp:346-395`). Package `ResourceFileReader` decodes only package
segments touched by a requested logical range, and `.vmesh` storage segments
place metadata first and each later geometry page at a segment boundary
(`packages.hpp:372-409`, `meshes.hpp:334-343`). `CollectLodPages` resolves the
page set of a requested LOD (`meshes.hpp:400-402`).

Consequently:

```text
Current generic ResourceStreamer
  -> stages/decodes a complete logical resource for its decoder

Current MeshFile + ResourceFileReader
  -> can open metadata and read individual page-aligned ranges
```

The production mesh resource must preserve the second behavior. Loading a
multi-gigabyte `.vmesh` into the generic decoder blob would erase the format's
streaming boundary.

No production class deriving `ResourceObject` for `MeshResourceType` was found.
Mesh parsing exists as `MeshFile`; current `ResourceObject` uses of mesh type in
`RenderingRuntime` only validate an externally supplied generic handle before
proxy admission (`rendering_runtime.cpp:313-350`).

#### Cooked mesh vocabulary already available

`.vmesh` already represents:

- vertex/index buffers and page intersections;
- page flags including `RequiredForLowestLod`;
- split vertex streams with semantic, format, binding, buffer, offset, and
  stride;
- vertex layouts;
- external typed material references;
- LOD records and stable submesh identities;
- direct vertex/index ranges, topology, index format, bounds, and quantization;
- independently readable pages and exact LOD-to-page collection.

The format deliberately excludes meshlets at this version
(`vmesh-format.md:39-71`). Phase 2 must validate whether this contract is
sufficient for atomic LOD installation and fixed-function arena placement; it
must not assume a format rewrite is required.

#### Renderer-global GPU Scene

`GpuSceneRuntime` already owns the renderer-wide persistent stack in dependency
order: tables, lifetime, uploader, immutable definitions, and RenderScene
publisher (`gpu_scene_runtime.hpp:55-100`).

The CPU/GPU ABI already has generational handles and canonical records:

```text
GpuRenderable -> GpuLod -> GpuPrimitive
GpuPrimitive -> GpuGeometryRange + GpuMaterial
GpuPrimitive -> GpuPhaseParticipation span
GpuGeometryRange -> GpuVertexStream span + optional GpuPositionDecode
GpuInstance -> GpuRenderable + optional GpuMaterialSet
```

Relevant fields are defined in `gpu_scene_types.hpp:149-269`.
`GpuGeometryRange` already contains an index-arena index, index byte offset,
index count, base vertex, vertex count, index format, and vertex stream span.
Each `GpuVertexStream` independently carries an arena index, byte offset,
stride, and format-layout identity (`gpu_scene_types.hpp:193-221`).

This is close to—but not yet a complete expression of—the selected
fixed-function binding model. The current GPU record does not name one stable
`VertexArenaSetId` suitable for the CPU batch-set key. Phase 3 must define the
physical allocation identity, and Phase 4 must decide whether GPU records store
that identity directly or derive it from an immutable stream-set definition.

`GpuSceneDefinitions` already content-addresses immutable geometry, material,
and renderable definitions and reference-counts reused content. Calls are
main-thread transactions (`gpu_scene_definitions.hpp:117-154`). Geometry
registration accepts already-resolved `GpuGeometryRange` and stream records
(`gpu_scene_definitions.hpp:22-28`); it does not allocate or upload vertex/index
arena bytes. That missing ownership belongs before definition publication.

#### Publication and GPU-safe lifetime

Persistent table allocations follow:

```text
Allocated -> Active -> Retiring -> reclaimed
```

Initial publication is committed only after complete data enters the sparse
upload stream. Unpublished allocations may be cancelled; active allocations
must retire. Retirement epochs require fences for all configured graphics,
compute, and copy queues before slots return to free lists
(`gpu_scene_lifetime.hpp:133-188`).

This is reusable identity/table lifetime, but it is not yet geometry-arena byte
range lifetime. The geometry allocator must integrate with the same frame fence
coverage without pretending GPU Scene table slots own physical vertex/index
buffers.

`RenderCommandSystem` owns one CPU rendering chain. FrameTick and RenderFrame
append work to that chain, while GPU completion remains an independent RHI fence
contract (`render_command_system.hpp:171-208`). `GpuSceneRuntime::Publish`
appends bounded publication work through the supplied Jobs continuation without
a CPU wait (`gpu_scene_runtime.hpp:73-77`).

#### RenderScene and component boundary

`ComponentDirectory` owns stable runtime component objects. Initialization gets
an immutable sibling resolver, retained-resource access, I/O priority, and a
Jobs continuation (`component.hpp:21-101`). `ComponentResourceAccess` already
routes typed acquisitions and requests through the process registry/pipeline
(`component.hpp:42-66`).

Per-world `RenderingRuntime` owns only world-session rendering state; the engine
rendering service owns renderer state (`rendering_runtime.hpp:44-47`). It queues
bounded proxy admission, supports cancellation, removes visibility immediately
on retirement, and delays actual proxy destruction (`rendering_runtime.hpp:77-105`).

The distant-proxy path already retains a mesh-typed resource handle and admits a
mesh proxy, but it cannot bind real GPU geometry until the missing resource to
residency/definition bridge exists (`rendering_runtime.cpp:313-350` and
`world-rendering-resume-checkpoint.md:66-74`). `StaticMeshComponent` remains
deliberately deferred until that bridge is designed.

#### RHI capability baseline

The RHI already exposes the selected drawing primitives:

- up to 16 fixed-function vertex bindings (`rhi_types.hpp:17`);
- explicit vertex and index buffer binding (`rhi.hpp:149-150`);
- portable indexed indirect arguments with one `baseVertex`
  (`rhi_types.hpp:1283-1290`);
- fixed-count and counted indexed MDI (`rhi.hpp:174-175`);
- graphics/compute/copy fences and a three-queue `ResidencyFenceSet`;
- upload/copy and resource-state operations;
- GPU memory-budget queries and optional native explicit-residency support
  (`rhi_types.hpp:565-655`).

Native memory-object residency is not the same thing as Vanguard's logical
`.vmesh` page residency. The latter still needs a renderer-owned geometry pool,
allocation map, requested/installing/resident state, and eviction policy.

### Current end-to-end ownership sketch

Solid arrows exist. Dashed arrows are missing production seams.

```text
Engine ResourcesService
  owns ResourceRegistry + ResourcePipeline
        |
        v
ResourcePipeline operation
  owns shared load interest + dependency edges + construction
        |
        v
ResourceRegistry slot/generation
  owns ResourceObject; ResourceHandle retains it
        |
        : missing production MeshResourceObject/decoder
        : missing metadata/page residency owner
        : missing geometry allocator/upload/install transaction
        v
RenderingService
  owns RHI + RenderCommandSystem + GpuSceneRuntime
        |
        +-> geometry arena buffers/ranges (missing)
        |
        +-> GpuSceneDefinitions
              owns immutable definition refs and GPU table identities
                    |
                    v
RenderSceneGpuPublisher
  binds RenderProxyHandle -> GpuRenderableHandle/material set
        ^
        |
per-world RenderingRuntime
  owns admission/retirement and world-session proxy state
        ^
        |
VisualComponent / future StaticMeshComponent
  owns authored reference, retained request/handle, and proxy lifecycle
```

### Canonical vocabulary

| Term | Meaning in Vanguard |
|---|---|
| Asset | Authoring/build identity and dependency-graph input. It is not automatically a live runtime object. |
| Resource reference | Typed serialized key (`path + type`) used by packages, components, and dependency discovery. |
| Resource object | Process-owned loaded object published into a generational `ResourceRegistry` slot. |
| Resource handle | Strong generational reference retaining a published resource object. |
| Pipeline request | Caller interest in one coalesced dependency-aware asynchronous load operation. |
| Mesh metadata residency | Validated `.vmesh` records needed to plan LODs/pages without retaining all geometry bytes. |
| Geometry page | Independently validated `.vmesh` storage/streaming unit; not a shader-visible virtual address. |
| LOD residency | All page dependencies and physical ranges needed to publish one complete drawable LOD. |
| Arena chunk | One large physical RHI buffer participating in the logical renderer-global geometry pool. |
| Vertex arena set | Binding-compatible set of split vertex-stream arena buffers sharing synchronized logical vertex ranges. |
| Geometry placement | Physical vertex-arena-set/range plus index-arena/format/range assigned to installed geometry. |
| GPU definition | Immutable content-addressed GPU Scene geometry/material/renderable record and its child table ranges. |
| Renderable | Canonical GPU Scene LOD/primitive definition referenced by instances. It is not a second mesh asset object. |
| Proxy | Per-world RenderScene object carrying transform/visibility and a resolved renderable/material-set binding. |
| Instance | Stable GPU Scene slot for an admitted ordinary renderable proxy. |
| Render phase | Stable renderer pass/category identity resolved to a compact phase index. |
| Pipeline bucket | Stable renderer-registered pipeline-compatible ordinal used by GPU phase participation. |
| Batch set | Work sharing every CPU-bound state required by one MDI call, including physical vertex/index bindings. |
| Bin | Finer grouping inside a batch set, normally identifying the same geometry/primitive command template. |
| Indirect command | GPU-produced draw argument record consumed by a known batch-set shell. |
| Residency feedback | Bounded GPU-to-CPU signal requesting better future geometry residency; never required to issue the current frame's MDI calls. |
| Retirement | Logical removal followed by deferred physical identity/range reuse after all covering GPU fences complete. |

### Copy / Adapt / Reject / Vanguard-specific baseline

| Classification | Reference idea | Phase 0 direction |
|---|---|---|
| Copy | Bevy separation between physical mesh slabs in the batch-set comparison key and mesh identity in the finer bin key | Preserve the conceptual split; exact key fields wait for Phase 4. |
| Adapt | Bevy staged pending/resident allocation | Use transactional reserve/upload/publish states, adapted to non-moving published arena chunks and fence-safe release. |
| Reject | Bevy grow-by-reallocate-and-copy for already published global mesh slabs | Vanguard's stable published geometry should normally remain in place; allocate another chunk instead. |
| Reject | Bevy meshlet storage-buffer vertex pulling as the normal mesh path | Conflicts with fixed-function input and RenderDoc mesh inspection. Study only as contrast. |
| Adapt | Nanite root/minimum residency, bounded pool, request feedback, priority, fallback, and eviction lifecycle | Apply to ordinary `.vmesh` LOD/pages without copying virtual shader address translation or Nanite rasterization. Unreal source validation remains blocked. |
| Copy | RED/Vanguard separation of serialized component reference, retained resource, runtime proxy, and renderer-owned GPU state | Preserve explicit owners and teardown order. |
| Vanguard-specific | Existing GPU Scene definitions, sparse uploader, generational slots, and multi-queue retirement epochs | Geometry residency must integrate with these systems rather than replace them. |

### Decisions locked by Phase 0

1. `ResourceRegistry` and `ResourcePipeline` remain the canonical resource
   identity/load systems. Mesh residency may add a specialized resource and
   subresource/page manager, but not another asset-handle universe.
2. The mesh resource path must not require staging the full logical `.vmesh`
   before metadata publication or page requests.
3. Existing GPU Scene handles and definition records remain canonical.
4. Geometry arena byte ownership is separate from GPU Scene table-slot
   ownership, though both must use compatible publication and fence coverage.
5. `RenderingService` is the renderer-global composition owner; per-world
   `RenderingRuntime` remains the proxy/world-session owner.
6. Fixed-function split-stream input and counted indexed MDI are supported by
   the current RHI and remain the target path.
7. Native RHI memory residency and logical `.vmesh` page residency remain
   separate layers.

### Alternatives rejected in Phase 0

- Creating a second mesh/renderable handle system beside `GpuSceneDefinitions`:
  it would produce ambiguous instance and batching identity.
- Treating `MeshFile` itself as sufficient production residency ownership: it
  parses metadata/pages but owns no asynchronous demand, GPU placement,
  definition handles, budget, or retirement.
- Feeding all `.vmesh` bytes through the generic contiguous decoder path: it
  defeats independently streamable package segments and can exceed the generic
  streamer's configured maximum resource size.
- Treating RHI explicit residency as the mesh streamer: it controls native
  memory objects, not LOD/page selection or suballocations within geometry
  arenas.

### Concurrency, lifetime, and failure implications

- Resource dependency planning/construction can execute on Jobs workers.
- Process resource-service startup/shutdown is externally serialized.
- Component materialization and `RenderingRuntime` mutation are main-thread
  controlled; transform relinking has a separate bounded worker path.
- Immutable GPU Scene definition registration is currently a main-thread
  transaction.
- GPU Scene publication appends worker/submission work to the single renderer
  CPU chain without a CPU wait.
- GPU completion is represented by queue fences, not completion of the CPU
  rendering chain.
- Mesh installation will require a named handoff from asynchronous IO/decoding
  to renderer-owned reservation/upload/publication.
- Cancelling caller interest must not tear down a coalesced resource or resident
  geometry still retained by another component, world record, definition, or
  in-flight GPU epoch.

### Tests and diagnostics identified

Phase 0 does not implement tests. Later plans must cover:

- generational stale-handle rejection across resource, geometry, definition,
  component, proxy, and GPU Scene identities;
- metadata-only load proof for packaged `.vmesh`;
- no full-resource staging for optional higher LOD pages;
- transactional allocation/upload/publication rollback;
- multi-queue fence-safe range reuse;
- split-stream synchronized vertex offsets;
- RenderDoc fixed-function mesh inspection;
- no-readback counted-MDI issuance;
- cancellation/coalescing races and world/component teardown.

### Open questions

Correctness-critical questions intentionally deferred to their study phases:

- Does the mesh `ResourceObject` own an open source/session, or does a separate
  residency service reopen package ranges from a durable resource locator?
- Are minimum-LOD pages part of initial resource construction or a distinct
  readiness stage after metadata publication?
- What is the precise component-facing readiness contract: metadata loaded,
  fallback drawable, or requested quality drawable?
- How are content-addressed GPU definition keys revised when physical placement
  changes but source content does not?
- Does one renderable definition remain stable while its resident LOD mask and
  geometry placements change, or are placement-specific immutable versions
  swapped atomically?
- Where is the authoritative `VertexArenaSetId` stored and how is it exposed to
  the CPU batch registry?
- What queue/fence closes the last possible reference to an evicted geometry
  range when copy, compute culling, and graphics draws overlap?
- What RED changelist produced the local snapshot?

### Execution-plan consequences

- A production mesh decoder cannot be implemented as only another generic
  contiguous `DecoderDescriptor` callback.
- The renderer-global residency owner must be reachable through
  `RenderingService` or a renderer-owned subordinate, not per world/component.
- Geometry allocation/upload must precede `GpuGeometryDefinition` acquisition.
- Renderable acquisition must follow resolution of geometry and default material
  handles.
- Proxy admission may occur structurally before binding, but renderability must
  remain inactive until a valid fallback renderable is published and bound.
- Geometry range retirement and GPU Scene definition retirement require one
  coordinated release transaction with independently owned allocations.
- Batch registry/indirect generation must be built after physical placement and
  GPU Scene ABI are locked, not before.

### Exit-gate evaluation

Phase 0 requires every reference source to be readable and pinned well enough
for repeatable source claims. Vanguard and Bevy are commit-pinned. RED and
Unreal lack repository revisions, but their selected corpora are content-pinned
with SHA-256 manifests; Unreal additionally identifies itself as 5.8.1 with
compatible changelist 55116800.

```text
PHASE 0 EXIT GATE: PASSED
SOURCE CORPORA: READABLE AND PINNED
NON-BLOCKING PROVENANCE GAP: RED changelist remains unknown
```

That explicit request was later received; the completed Phase 1 study follows.

## Phase 1 — References, Handles, and End-to-End Ownership

### Phase result

```text
Study work: complete
Exit gate: passed
Blocker: none
Next allowed work at this checkpoint: Phase 2 on a later explicit request
Phase 2 state at this checkpoint: not started
```

Phase 1 fixes identity and lifetime authority. It deliberately does not decide
the `.vmesh` page layout, residency budget algorithm, physical allocator, or GPU
Scene ABI changes assigned to later phases.

### Sources inspected

Vanguard resource identity and coalesced loading:

- `source/resources/include/vanguard/resources/resources.hpp:105-144,
  146-219, 221-425`
- `source/resources/include/vanguard/resources/resource_pipeline.hpp:10-108`
- `source/resources/src/resource_registry.cpp:34-148, 261-383, 507-704`
- `source/resources/src/resource_pipeline.cpp:108-180, 217-340, 749-837,
  966-1136, 1158-1200`

Vanguard world, component, proxy, and GPU-definition lifetime:

- `source/entities/include/vanguard/entities/component.hpp:19-101`
- `source/entities/include/vanguard/entities/materializer.hpp:101-165`
- `source/entities/include/vanguard/entities/visual_component.hpp:61-130`
- `source/entities/src/visual_component.cpp:43-62, 120-210, 260-298`
- `source/entities/include/vanguard/entities/rendering_runtime.hpp:44-105`
- `source/entities/src/rendering_runtime.cpp:290-388, 401-475, 520-557`
- `source/world/include/vanguard/world/streaming_executor.hpp:8-70`
- `source/world/src/streaming_executor.cpp:44-68, 118-227, 291-338`
- `source/entities/src/cell_streaming_system.cpp:264-329, 348-482`
- `source/rendering/include/vanguard/rendering/render_scene.hpp:22-65,
  95-155, 190-254, 583-611`
- `source/rendering/src/render_scene.cpp:1259-1381, 1384-1430,
  1570-1630`
- `source/rendering/include/vanguard/rendering/gpu_scene_definitions.hpp:22-154`
- `source/rendering/src/gpu_scene_definitions.cpp:1020-1275`
- `source/rendering/src/render_scene_gpu.cpp:744-823, 876-932`

RED ownership comparison:

- `D:\root\R6.Root\Mainline\dev\src\common\world\include\meshNode.h:36-50,
  103-160`
- `D:\root\R6.Root\Mainline\dev\src\common\world\src\meshNode.cpp:27-56,
  73-95, 146-158, 183-231`
- `D:\root\R6.Root\Mainline\dev\src\common\world\include\meshNodeInstance.h:33-59,
  81-107`
- `D:\root\R6.Root\Mainline\dev\src\common\world\src\meshNodeInstance.cpp:153-341,
  343-401, 412-490`
- `D:\root\R6.Root\Mainline\dev\src\common\world\src\prefabProxyMeshNodeInstance.cpp:52-104`
- `D:\root\R6.Root\Mainline\dev\src\common\world\include\runtimeSystemRendering.h:50-103`
- `D:\root\R6.Root\Mainline\dev\src\common\world\src\runtimeSystemRendering.cpp:222-275,
  315-335`
- `D:\root\R6.Root\Mainline\dev\src\common\resourceMesh\include\mesh.h:125-137,
  168-196, 222-255, 339-393`

Unreal ownership comparison:

- `D:\UnrealEngine\Engine\Source\Runtime\Engine\Public\StaticMeshResources.h:621-632,
  673-683, 768-820, 878-893, 916-944`
- `D:\UnrealEngine\Engine\Source\Runtime\Engine\Public\Streaming\RenderAssetOwnerStreamingState.h:8-75`
- `D:\UnrealEngine\Engine\Source\Runtime\Engine\Internal\Streaming\RenderAssetUpdate.inl:13-109`
- `D:\UnrealEngine\Engine\Source\Runtime\Engine\Private\Streaming\RenderAssetUpdate.cpp:1-320`

Bevy was inspected as a narrow ownership contrast:

- `C:\Users\Ark\Documents\bevy\crates\bevy_render\src\mesh\mod.rs:120-232`
- `C:\Users\Ark\Documents\bevy\crates\bevy_render\src\mesh\allocator.rs:241-312,
  360-430, 498-517`
- `C:\Users\Ark\Documents\bevy\crates\bevy_render\src\slab_allocator.rs:315-320,
  463-511, 732-786, 937-972`

### Evidence findings

#### Vanguard already separates reference, request, and loaded ownership

`ResourceReference` is a value identity containing a resource path and expected
type. It neither proves that a resource is loaded nor retains one. A
`PipelineRequest` represents one caller's interest in a coalesced load
operation. A `ResourceHandle` is the strong generational reference to a
published `ResourceObject` (`resources.hpp:105-144, 221-256` and
`resource_pipeline.cpp:966-1136`).

The pipeline records root interests and dependency interests separately. It
cancels the underlying operation only when both reach zero, releases dependency
interests recursively, and invokes asynchronous preparation cancellation before
construction when available (`resource_pipeline.cpp:108-146, 276-333`). A
single caller cancelling its request therefore does not cancel other callers or
parents that still retain the same dependency.

The registry owns the slot, generation, loaded object pointer, destroy callback,
and strong/weak counts. Strong handle copy/reset updates the registry counts;
resolution checks the generation (`resource_registry.cpp:78-91, 279-383`).
User destruction callbacks run outside the registry lock, after the entry has
been made unreachable (`resource_registry.cpp:507-550`). These rules remain the
authority for the mesh metadata resource.

#### World streaming owns load interest until downstream detaches

`WorldStreamingExecutor::Operation` owns either the active `PipelineRequest` or
the acquired `ResourceHandle`. Successful polling converts request interest to
a strong handle, emits `ResourceAvailable`, and enters `Resident`. Stream-out
of a requesting node cancels only that request. Stream-out of a resident node
enters `ReleasePending` and emits `ReleaseRequested`; only
`CompleteRelease` drops the strong handle (`streaming_executor.cpp:44-68,
118-227, 299-338`).

This is the correct back-pressure boundary. A world node is not released merely
because it left the spatial grid. Its downstream consumer first removes ECS or
renderer state and acknowledges completion.

#### Component and proxy identity are deliberately separate

The component directory owns stable runtime component objects. Component
initialization receives an immutable sibling resolver and resource access that
can acquire already-retained resources or issue pipeline requests
(`component.hpp:19-66`). The materializer serializes cell state changes and
requires explicit queue/complete release operations (`materializer.hpp:101-165`).

`RenderProxyHandle` is scene-local and generational. It explicitly does not
reuse serialized, ECS, networking, or editor identity (`render_scene.hpp:22-65`).
Proxy creation and destruction are main-thread transactions; creation reserves
a slot and increments its generation (`render_scene.cpp:1259-1381`). A mesh
proxy currently stores both the authored `ResourceReference` and strong
`ResourceHandle` (`render_scene.hpp:215-254`; `render_scene.cpp:1384-1430`).

`VisualComponent` owns pending admission and the eventual proxy binding. Detach
first cancels queued admission or unbinds the proxy, then asks
`RenderingRuntime` to retire it. `RenderingRuntime::RetireProxy` removes
visibility immediately and delays destruction (`visual_component.cpp:161-210`;
`rendering_runtime.cpp:458-475`). GPU publication has a separate retirement
record for the instance allocation (`render_scene_gpu.cpp:744-775`).

#### GPU definitions are identities, not geometry-memory owners

`GpuSceneDefinitions` provides content-keyed, generational geometry, material,
and renderable handles. Reference changes are main-thread-only. A renderable
retains its geometry/material children, and final renderable release recursively
releases those definition records (`gpu_scene_definitions.cpp:1020-1275`).

`RenderSceneGpuPublisher::BindMesh` requires a valid `GpuRenderableHandle`, but
the tracked binding is not itself a strong definition reference
(`render_scene_gpu.cpp:789-823`). Consequently, the new residency bridge must
own the definition reference for at least as long as any proxy can publish or
draw that binding. Physical vertex/index allocations remain independently owned
and fence-retired by the residency layer.

#### RED confirms the reference → loaded object → proxy split

RED serializes `MeshNode::m_mesh` as `TResAsyncRef<CMesh>`, while
`MeshNodeInstance` separately owns a loading token, `THandle<CMesh>`, and
`RenderProxyPtr` (`meshNode.h:36-50, 160`; `meshNodeInstance.h:81-107`). The
instance loads the resource, waits through jobs, resolves appearance/material
setup, then creates and registers a render proxy
(`meshNodeInstance.cpp:153-341, 412-490`). Detach transfers the proxy into a
dissolve owner rather than destroying it immediately
(`meshNodeInstance.cpp:377-401`; `runtimeSystemRendering.cpp:222-275`).

The useful rule is separation of authored reference, loaded resource,
instance/proxy lifetime, and delayed render destruction. Vanguard does not copy
RED's blocking/spinning load paths or time-based dissolve as a GPU-safety
mechanism.

#### Unreal confirms asset-owned LOD data plus thread-explicit updates

Classic Unreal `FStaticMeshRenderData` owns the per-LOD resource array and
tracks `CurrentFirstLODIdx`; stream cancellation can discard the loaded CPU
vertex/index data (`StaticMeshResources.h:621-632, 768-820, 916-944`). Render
resource initialization and release are explicit operations. Render-asset
updates schedule different stages onto game, render, and async thread contexts
(`RenderAssetUpdate.inl:13-109`).

This supports explicit thread handoffs and fallback residency, but Vanguard
does not copy UObject ownership, raw per-LOD resource ownership, or Unreal's
task framework. The asset object and streaming update are distinct authorities.

#### Bevy ties extracted asset lifetime directly to allocator membership

Bevy converts an extracted `Mesh` into `RenderMesh`, keys slab placement by
`AssetId<Mesh>`, allocates newly extracted/modified meshes, and stages frees for
removed/modified meshes (`mesh/mod.rs:120-232`; `mesh/allocator.rs:241-312,
360-430, 498-517`). This is a clean identity-to-placement map for a renderer
subsystem, but it lacks Vanguard's world acknowledgement, proxy retirement,
multi-queue fence epoch, and pageable LOD lifetime requirements. Vanguard adapts
the keyed/coalesced record, not direct removal-driven physical reuse.

### Locked identity model

| Identity | Meaning | Owner | May cross the boundary to |
|---|---|---|---|
| `ResourceReference` | Serialized `path + expected type`; no liveness guarantee | Serialized asset/component/world record | Resource pipeline and diagnostics |
| `PipelineRequest` | One caller's interest in a coalesced dependency-aware load | Requesting component/world/residency operation | Completion/cancellation only |
| `ResourceHandle` | Strong generational handle to the published mesh metadata object | Every retaining consumer; registry owns the slot/object | World executor, residency record, proxy payload |
| `MeshResidencyHandle` | Proposed renderer-private generational handle to one residency record | Renderer-global mesh residency manager | Internal requests, diagnostics, strong binding handle; never serialized |
| `MeshResidencyRequest` | Proposed move-only caller interest in a readiness target | Component/world admission while demand exists | Residency manager only |
| `MeshRenderBindingHandle` | Proposed owning handle over a published fallback-or-better renderable and its physical placements | Mesh proxy payload / pending proxy admission | RenderScene and retirement path |
| `GpuGeometryHandle` | Existing immutable GPU Scene geometry-definition identity | `GpuSceneDefinitions` | Renderable definitions |
| `GpuRenderableHandle` | Existing canonical draw-visible renderable identity | `GpuSceneDefinitions`; retained by the strong binding handle | GPU Scene instance binding and batching |
| `RenderProxyHandle` | Existing scene-local generational instance/proxy identity | `RenderSceneManager` | Component binding and GPU Scene publisher |

`MeshResidencyHandle` is not a second asset or draw identity. It names mutable
renderer residency state. The resource remains identified by `ResourceHandle`;
draws remain identified by existing GPU Scene handles.

### Locked ownership model

| Object/state | Sole authority | Strong references held |
|---|---|---|
| Serialized mesh reference | Component, prefab, world, or package record containing it | None |
| Published `MeshResourceObject` metadata | `ResourceRegistry` | Its dependency `ResourceHandle`s and durable page-source locator/session chosen in Phase 2 |
| Coalesced whole-resource load operation | `ResourcePipeline` | Dependency interests until terminal completion/cancellation |
| World stream-in operation | `WorldStreamingExecutor` | `PipelineRequest` while requesting; `ResourceHandle` while resident/release-pending |
| Component runtime object | `ComponentDirectory` through materializer lifecycle | Its authored references, request handles, and proxy admission state |
| Mesh residency record | Renderer-global mesh residency manager under `RenderingService` | Mesh `ResourceHandle`, coalesced demand interests, installed LOD records, definition references, in-flight work |
| Compressed/decompressed page bytes | Residency record's in-flight page operation | Exactly one stage owner at a time; detailed states wait for Phase 2 |
| Physical vertex/index placement | Residency manager's installed LOD record | Arena allocation tokens until fence-safe retirement |
| GPU geometry/material/renderable definitions | `GpuSceneDefinitions` | Internal child-definition references; no ownership of arena bytes |
| Proxy admission | `RenderingRuntime` slot, with caller-owned admission handle | Full descriptor including resource and strong binding handles |
| Live mesh proxy | `RenderSceneManager` | Mesh resource handle and `MeshRenderBindingHandle` |
| GPU Scene instance | `RenderSceneGpuPublisher`/`GpuSceneLifetime` | Published binding value; lifetime is covered by the proxy's strong binding handle until retirement completion |

Components must never own page buffers, arena allocations, or GPU definition
release decisions. GPU Scene definitions must never free arena allocations.

### Stable IDs and generation rules

1. Serialized references remain path/type values and carry no generation.
2. Loaded resource identity is `(path, type, ResourceHandle generation)`.
3. A residency record is keyed internally by that loaded resource identity. A
   reload generation cannot silently reuse the previous residency record.
4. `MeshResidencyHandle`, allocation tokens, GPU definition handles, and proxy
   handles use nonzero generations incremented on slot reuse. Wrap skips zero.
5. Stale handles fail validation; they never alias the current occupant.
6. Content fingerprints may coalesce immutable payload or GPU definitions only
   after Phase 2 defines their exact scope. They do not replace lifetime handles.
7. Physical placement changes do not mutate an already-published definition in
   place. Later phases must choose atomic version replacement or a stable
   indirection record; readers never observe half-old/half-new placement.

### Coalescing, cancellation, and failure rules

```text
ResourcePipeline
  coalesces by typed resource path
  cancels underlying work only when root interests + dependency interests == 0

Mesh residency manager
  coalesces by loaded resource generation + readiness target
  promotes priority monotonically while interests exist
  cancels not-yet-published work only when all interests and strong binding handles are gone
  never revokes the installed fallback while a strong binding handle exists
```

- Cancelling one `MeshResidencyRequest` removes only that caller's interest.
- Cancellation before reservation completion drops queued I/O/decode work.
- Cancellation after reservation/upload begins marks the install transaction for
  rollback; non-cancellable GPU work finishes, then unpublished allocations are
  retired safely.
- Cancellation after publication becomes ordinary demand reduction. Eviction is
  budget policy, not synchronous cancellation.
- Required dependency failure fails mesh readiness with the original failure
  trace. Optional dependency failure records degradation and uses an explicit
  fallback only if the format contract permits it.
- A metadata-loaded mesh is not automatically drawable. Proxy admission that
  requires drawing succeeds only after a valid `MeshRenderBindingHandle` exists.
- Existing visible bindings survive failure to install a better LOD. Failure of
  the minimum/fallback LOD makes the mesh non-drawable and is propagated to the
  component/world operation.

### Thread and queue authority

| Transition | Authority |
|---|---|
| Create/release `PipelineRequest`; dependency discovery/construction | Thread-safe pipeline API; work may run on Jobs workers |
| Publish/destroy `ResourceObject` | Registry transaction; destruction callback outside registry lock |
| Component materialization planning | Serialized game-world/materializer transaction; component initialization may use Jobs continuation |
| Proxy admission, cancellation, binding, visibility removal, scene mutation | Main thread, matching current `RenderSceneManager` and `RenderingRuntime` contracts |
| Residency demand submission | Thread-safe enqueue from components/world; no direct state mutation by caller |
| Residency state arbitration, definition acquire/release | Renderer residency authority serialized onto the main/render-control boundary; definition reference changes remain main-thread-only in V1 |
| Page I/O/decompression | I/O and Jobs workers; results return as owned completion messages |
| Arena reservation/install bookkeeping | Renderer residency authority; physical upload recording occurs on renderer command chain |
| Buffer copies | Copy queue when supported, otherwise graphics queue; install remains unpublished until required completion is known |
| Culling/draw consumption | Compute/graphics queues through render graph ordering |
| Allocation/definition reuse | Only after the combined graphics/compute/copy retirement epoch completes |

No worker callback may directly mutate a component, `RenderScene`, residency
record, or GPU definition table. It returns an immutable completion payload to
the owning authority.

### Attach and detach ordering

Attach for a drawable mesh:

```text
deserialize ResourceReference
  -> acquire/request MeshResourceObject
  -> request fallback readiness from residency manager
  -> residency manager retains ResourceHandle
  -> reserve/upload/install physical placement
  -> acquire GPU geometry/material/renderable definitions
  -> create MeshRenderBindingHandle
  -> enqueue main-thread proxy admission carrying resource + binding handles
  -> create RenderProxyHandle
  -> bind GpuRenderableHandle to GPU Scene instance
  -> publish instance
  -> acknowledge world render-ready
```

Detach/release is the reverse ownership transaction:

```text
stop new component/proxy updates
  -> cancel pending proxy admission, or clear visibility immediately
  -> unbind component from RenderProxyHandle
  -> enqueue proxy/GPU-instance retirement
  -> wait for the GPU Scene retirement epoch covering all queues
  -> destroy proxy payload and drop MeshRenderBindingHandle
  -> decrement residency demand
  -> release GPU definition references when no binding/install owner remains
  -> retire physical placements behind their final queue fences
  -> release residency record's ResourceHandle when no work/state needs metadata
  -> acknowledge WorldStreamingExecutor::CompleteRelease
```

Frame delays or RED-style dissolve may affect presentation, but they are not a
substitute for GPU fence completion. A dissolve path itself owns a strong binding handle
until its final draw retires.

### State machines fixed by Phase 1

Caller request:

```text
Requested -> Ready | Failed | Cancelled
Ready -> Released
```

Residency record lifecycle (coarse; Phase 2 expands page states):

```text
Vacant
  -> MetadataRetained
  -> PreparingAnchorLod
  -> AnchorLodReady
  -> Retiring
  -> Vacant(next generation)

MetadataRetained | PreparingAnchorLod -> Failed
PreparingAnchorLod -> MetadataRetained       (clean cancellation/rollback)
AnchorLodReady -> PreparingQuality           (optional higher LOD demand)
PreparingQuality -> AnchorLodReady           (failure/cancellation preserves the anchor)
```

Proxy lifecycle remains:

```text
NoProxy -> AdmissionQueued -> Alive -> RetirementQueued -> Retired
                 |              |
                 +-> Cancelled   +-> hidden immediately
```

### Copy / Adapt / Reject decisions from Phase 1

| Classification | Reference idea | Decision |
|---|---|---|
| Copy | Vanguard `ResourceReference` / `PipelineRequest` / `ResourceHandle` separation | Keep as the only resource identity and load-interest system. |
| Copy | Vanguard world `ReleaseRequested` / downstream `CompleteRelease` handshake | Use for renderer residency and component teardown; never drop resource ownership before downstream detach. |
| Copy | RED separation of serialized async reference, loaded mesh handle, node instance, and render proxy | Preserve the ownership boundaries with Vanguard handles and non-blocking jobs. |
| Adapt | RED delayed proxy destruction/dissolve | Allow visual delay, but retain strong binding handles and use GPU fence epochs for actual reuse safety. |
| Adapt | Unreal asset-owned LOD resources plus explicit asynchronous update stages | Keep metadata resource ownership separate from renderer residency updates and use explicit thread handoffs. |
| Adapt | Bevy `AssetId`-keyed renderer allocation map | Key residency by a loaded resource generation, not only an asset ID, and add strong ownership plus fence retirement. |
| Reject | Component-owned physical buffers or GPU definitions | Creates duplicate ownership and makes shared residency, eviction, and safe teardown impossible. |
| Reject | Directly freeing geometry when an asset-extraction/remove event occurs | Does not cover proxies, queued publication, dissolve, or multi-queue GPU use. |
| Vanguard-specific | `MeshRenderBindingHandle` covering renderable definition and physical placement lifetime | Required because current GPU Scene bindings store handles but do not retain definition references. |

### Tests and diagnostics required by the Phase 1 contract

- Multiple requests for the same mesh coalesce; cancelling one preserves the
  other and dependency interests.
- A stale resource/residency/proxy/definition handle never resolves after slot
  reuse.
- Component detach during resource load, page preparation, proxy admission,
  GPU publication, and live drawing reaches a leak-free terminal state.
- World `CompleteRelease` cannot occur before component/proxy/binding detach.
- A strong proxy-binding handle prevents renderable definition and physical placement
  retirement.
- Failed higher-LOD preparation preserves the existing fallback binding.
- Failed fallback preparation never admits a visible drawable proxy and carries
  the original failure trace.
- Worker completion after cancellation is harmless and cannot mutate a reused
  generation.
- Definition release and physical-range reuse wait for graphics, compute, and
  copy fences.
- Shutdown refuses live requests, residency records, proxy admissions, binding
  strong owners, or retirement epochs and reports their owners.

Diagnostics must be able to walk this chain in both directions:

```text
ResourceReference
  -> resource slot/generation
  -> residency slot/generation and demand owners
  -> installed LOD placements
  -> GPU definition handles/reference counts
  -> proxy handles and GPU Scene instances
  -> pending retirement epoch/fences
```

### Open questions carried into later phases

Phase 1 no longer leaves ownership ambiguous. The following are contract details
assigned to later phases:

- Phase 2: exact metadata/page identity, fallback completeness, content-key
  scope, durable page-source/session ownership, detailed asynchronous page
  states, retry policy, and compressed/decompressed byte budgets.
- Phase 3: exact allocation-token and `VertexArenaSetId` representation,
  upload/install atomicity, budgets, eviction, and the combined retirement
  epoch.
- Phase 4: definition replacement versus stable placement indirection, and the
  minimal readiness/binding interface consumed by proxies and future mesh
  components. Component schema and general world integration remain out of
  scope.

### Execution-plan consequences

1. Add one renderer-global mesh residency authority under `RenderingService`;
   do not add a second resource registry.
2. Add private generational residency records and move-only demand handles.
3. Add an owning proxy binding handle that retains both GPU definition references
   and installed placement lifetime.
4. Extend mesh proxy payload/admission/update paths to carry that handle; a bare
   `GpuRenderableHandle` is insufficient ownership.
5. Route worker/I/O results through generation-checked completion messages.
6. Coordinate proxy retirement, GPU Scene instance retirement, definition
   release, placement retirement, resource-handle release, and world release
   acknowledgement in that order.
7. Preserve main-thread definition/proxy mutation until a later explicit design
   changes the existing contract.

### Exit-gate evaluation

Every object in the target pipeline now has one authority, explicit retaining
references, legal coarse state transitions, thread ownership, cancellation and
failure behavior, and a destruction path. Page formats and allocator mechanics
remain deliberately deferred without leaving their owner ambiguous.

```text
PHASE 1 EXIT GATE: PASSED
IDENTITY AND OWNERSHIP: LOCKED
IMPLICIT GLOBAL LIFETIME: REJECTED
PRODUCTION IMPLEMENTATION: STILL PROHIBITED
```

That explicit request was later received; the completed Phase 2 study follows.

## Phase 2 — Mesh Format, Resource Object, and Streaming Contract

### Phase result

```text
Study work: complete
Exit gate: passed
Blocker: none
Next allowed work at this checkpoint: Phase 3 on a later explicit request
Phase 3 state at this checkpoint: not started
```

### Sources inspected

Vanguard evidence is from the live `D:\ENGINE` workspace pinned in Phase 0:

- `docs/formats/vmesh-format.md:1-69` and
  `docs/migration/red-mesh-vmesh.md:1-70`;
- `source/meshes/include/vanguard/meshes/meshes.hpp:10-402`;
- `source/meshes/src/meshes.cpp:884-1057`, `1143-1345`, and `1454-1618`;
- `source/meshes/tests/meshes_tests.cpp:164-295`;
- `source/meshTools/tests/mesh_tools_tests.cpp:481-618`;
- `source/resources/include/vanguard/resources/resource_pipeline.hpp:10-225`;
- `source/resources/include/vanguard/resources/resources.hpp:146-425`;
- `source/resources/src/resource_pipeline.cpp:565-816`;
- `source/streaming/include/vanguard/streaming/streaming.hpp:10-166`;
- `source/streaming/src/streaming.cpp:437-918`;
- `source/packages/include/vanguard/packages/packages.hpp:351-419` and
  `source/packages/src/packages.cpp:1566-1778`;
- `source/materials/README.md:1-30`, `docs/formats/vmat-format.md:1-36`, and
  `source/materials/src/materials.cpp:461-680`.

RED remains the content-pinned snapshot from Phase 0. Phase 2 additionally
records these exact file hashes:

- `resourceMesh/include/mesh.h` —
  `6B26023E441F314D147294F2775393093E785416239E33D3E6615ACEC1A773ED`;
- `resourceMesh/include/meshChunk.h` —
  `287DB8BCB72600B4FEC1572264F0FE824BEC52046DDE882F916F7C8E9DBB759E`;
- `resourceMesh/include/meshDataViewRO.h` —
  `0760957C93591328992DD6CB64BF5DBCEB82603CEB3FAF5E71E14C342AA64F07`;
- `renderData/include/renderMeshBlob.h` —
  `F2EFF105265349248801A53BE00D616F6EBA9642CD6A121E92A10CA7765C817E`;
- `renderer/src/renderMeshStreaming.h/.cpp` —
  `1806D801964A667798D1A2E908E2417ED0D3CE4BFD580EC9894495D12BAE2EF4` /
  `4541653F07E4AC94996A531A2546201E78F213AD529BB231549D75D4A6E1E676`.

Unreal evidence is from the Phase 0-pinned UE 5.8.1 snapshot:

- `NaniteResources.h:228-257` and `452-559`;
- `NaniteResources.cpp:307-316` and `333-472`;
- `NaniteStreamingManager.h:149-336`;
- `NaniteStreamingManager.cpp:950-1028`, `2020-2193`, and `2432-2672`;
- `StaticMeshResources.h:415-632` and `768-934`.

### Observed architecture

#### Vanguard format is already pageable

`vmesh` V1 separates checksummed `META` from streamable `GEOM`. Metadata owns
bounded arrays for buffers, pages, layouts/streams, material slots, LODs, and
submeshes. Each page carries its logical-buffer offset, document offset, byte
size, alignment, flags, and SHA-256 digest (`meshes.hpp:233-299`). Opening reads
and validates only `META`; page bytes are read and hashed independently
(`meshes.cpp:1143-1345`, `1598-1618`).

The last LOD is the coarsest fallback. Validation proves that every vertex and
index range used by it is covered by `RequiredForLowestLod` pages
(`meshes.cpp:1036-1056`). `CollectLodPages` derives the exact deduplicated page
set from submesh ranges (`1514-1595`). The flag may conservatively mark extra
pages, so the collected set—not every flagged page—is the install authority.

`BuildStorageSegments` emits one prefix/metadata segment followed by one
independently decodable segment beginning at each page (`1454-1511`). The
package test proves metadata opening decodes one segment and a requested page
decodes only its segment (`mesh_tools_tests.cpp:526-558`). This format behavior
is implemented, although no production mesh compiler/loader currently wires it
into `ArtifactWriter` or `ResourceStreamer`.

The metadata content fingerprint hashes metadata containing every page digest.
It therefore identifies the complete cooked content transitively. The source
fingerprint remains cooker/source provenance. Neither replaces the loaded
resource generation used for lifetime safety.

#### The missing seam is the resource/page source

`ResourcePipeline` already provides dependency fan-in, interest-based
cancellation, priority promotion, asynchronous preparation, and construction
(`resource_pipeline.hpp:65-107`, `142-209`). `ResourceStreamer`, however,
reserves `logicalSize + compressedBytes`, reads every package segment, rebuilds
the complete resource, and passes one contiguous buffer to a decoder
(`streaming.cpp:564-705`, `788-856`). That path is correct for ordinary small
resources and wrong for optional mesh geometry.

`packages::ResourceFileReader` demonstrates the required range semantics, but
it is synchronous, single-view, and keeps only one decoded segment cache
(`packages.hpp:370-418`, `packages.cpp:1678-1751`). It is evidence for logical
range mapping, not the production asynchronous residency API. Its current
`IFile` error surface also makes `MeshFile::ReadPage` report package corruption
as generic mesh `IoFailure`; the specialized path must retain structured
storage-versus-page-integrity failures.

There is no production `MeshResourceObject`, mesh decoder registration, durable
owned loose/package page source, or asynchronous page request API in the inspected
Vanguard code.

#### Reference-engine conclusions

RED confirms the cooked boundary: `CMesh` owns resource/material/appearance
metadata while `RenderMeshBlobHeader` stores render LODs, chunk stream/index
offsets, formats, counts, and quantization beside one cooked render buffer
(`mesh.h:56-126`, `339-380`; `renderMeshBlob.h:128-185`, `267-269`). Its mesh
streaming class only tracks pending buffer totals
(`renderMeshStreaming.h:6-30`). RED is useful for metadata/render-data
separation, not as a page-residency algorithm.

Unreal Nanite keeps root data resident so something is always drawable and
stores other pages separately with offsets, sizes, and explicit page
dependencies (`NaniteResources.h:248-257`, `452-466`). Requests recursively add
dependencies, registration pins them, and I/O retries are explicit
(`NaniteStreamingManager.cpp:950-1028`, `2020-2193`, `2579-2672`). Classic
static meshes independently reinforce the fallback rule through non-streaming /
non-optional LODs and `CurrentFirstLODIdx`
(`StaticMeshResources.h:878-881`, `919-933`).

Vanguard should copy the guaranteed-fallback and bounded asynchronous-state
principles, not Nanite's virtual pages, fixups, or transcode representation.
Vanguard pages are already final fixed-function buffer bytes, and a drawable LOD
requires a set of complete vertex/index ranges rather than a virtual hierarchy.

### Decisions locked

1. Keep `vmesh` V1. Phase 2 found no format change required for fixed-function
   LOD residency.
2. The fallback is `lods.back()`. Its authoritative install set is
   `CollectLodPages(last_lod)`; all members must carry
   `RequiredForLowestLod`, while extra flagged pages are permitted.
3. An LOD is published only as a complete `LodInstallSet`. Individual pages may
   be read and shared independently, but they never make a partially complete
   LOD drawable.
4. Do not add Nanite-style page dependency/fixup tables to V1. Page sharing is
   represented by overlap between derived LOD page sets and retained by page
   reference counts. A future representation requiring relocation, transcode,
   or partial hierarchical drawability requires a versioned extension.
5. Page identity is `(loaded resource generation, page index)`. Page indices,
   buffer indices, and dense LOD indices are not stable external identities
   across recooks. Resource path, submesh `stableId`, material-slot name, and
   content fingerprint serve their existing stable/content roles.
6. Every material slot and the skeleton of a skinned mesh is an external typed,
   required resource dependency. The specialized loader must compare META
   references with the loose/VPAK dependency table before publication and retain
   the resolved `ResourceHandle`s. Material graphs and resources remain owned by
   `vmat`; `vmesh` owns only slot names and references.
7. A production `MeshResourceObject` owns immutable parsed metadata, retained
   dependency handles, and one owned `MeshPageSource`. It never owns GPU
   placements or resident page state.
8. `MeshPageSource` is ordinary strong ownership of one immutable loose/package
   source generation and content identity. It has no expiry, renewal, or
   revocation protocol. Mount replacement or loose-file change creates a new
   source; existing resources keep reading their owned source. Destruction
   happens naturally when the final owner is dropped.
9. Mesh metadata loading uses a specialized loader that asynchronously reads
   only the prefix/metadata segment. It uses the generic `ResourceSource`
   physical I/O/package-segment engine, but does not route through the generic
   full-resource decoder or stage `GEOM`.
10. Page reads are coalesced by resource generation and page index. Priority is
    the highest active interest; an optional target frame uses the earliest
    active deadline and is advisory, not a correctness timeout.
11. V1 keeps no long-lived CPU geometry cache. CPU memory covers in-flight
    stored bytes, decoded segment/page bytes, and the handoff payload until the
    Phase 3 upload transaction releases it. Stored plus decoded memory is
    charged before I/O begins.
12. Transient I/O failures receive a small configured retry limit with
    diagnostics. Integrity, unsupported-version, invalid-layout, and digest
    failures are terminal. Budget pressure waits in a bounded queue; it is not
    reported as corruption and does not spin-retry.

### Shared streaming boundary locked before continuing Phase 2

Mesh residency does not own a second streaming stack. The consolidation audit
established these ownership rules:

- `resources::ResourcePipeline` owns request coalescing identity and one opaque
  loader-state slot for the lifetime of an operation. The mesh metadata loader
  attaches its state there; it does not maintain a parallel request map.
- `streaming::ResourceStreamer` owns source selection, dependency snapshots,
  shared staging admission/accounting, and mounted package generations.
- `streaming::ResourceSource` is the single positional byte-read engine for
  loose files and VPAK segments, including authentication, decompression,
  cancellation, and telemetry. A VPAK generation is parsed and pinned once per
  mount and retained by all sources opened from it.
- `meshes::MeshResourceLoader` owns only VMSH prefix/META sequencing,
  `MeshFile` validation, dependency agreement, and publication of
  `MeshResourceObject`.
- `meshes::MeshPageSource` owns only mesh page-range and SHA-256 policy over a
  generic `ResourceSource`.

The same staging ceiling covers generic full-resource output, package
stored/decoded scratch, and specialized metadata buffers. Scheduling policy at
this boundary must remain in the generic streaming layer and be consumed by
meshes, textures, animations, and other formats rather than reimplemented for
`.vmesh`.

The generic range queue now implements the queued-admission and exact-range
coalescing portions of that rule. Its identity is the retained source generation
plus logical `(offset, size)`; meshes obtain that range from their generation
and page index, then perform only the page digest check. Staging remains charged
until the final shared result interest releases the bytes, preventing queued
work from overwriting or outliving its accounted payload.

Failure handling is also generic and bounded. Only a physical `IoFailure` is
classified as transient; it receives two retries by default while keeping the
same admitted storage and shared interests. Cancellation has its own class.
Integrity, unsupported version, invalid layout/arguments, digest mismatch, and
capacity failures are permanent and are never retried. The request reports its
attempt count and accumulated physical-read statistics for diagnostics.

### Proposed resource and page-read shape

```text
MeshResourceObject : ResourceObject
  immutable MeshFile metadata
  retained material/skeleton ResourceHandles
  MeshPageSource source
  cooked content fingerprint

MeshPageSource
  exact source generation + logical size
  owning loose-file or package-storage handle

LodInstallSet
  resource generation + LOD index
  sorted unique page indices from CollectLodPages
```

The owned page source exposes asynchronous logical page acquisition; it does not
expose mutable residency records. For package data, a page request reads and
decodes its one storage segment, then validates the vmesh page SHA-256. For a
loose file it reads the exact page range and validates the same digest.

Detailed pre-install state:

```text
Absent
  -> WaitingForBudget
  -> ReadingStoredBytes
  -> DecodingSegment          (package-compressed source only)
  -> VerifyingPageDigest
  -> ReadyForInstall

any non-terminal state -> CancelRequested -> Cancelled
any active state -> Failed
```

Dropping one LOD/request interest does not cancel a shared page operation.
Queued work is removed when its final interest disappears. Active I/O receives
best-effort cancellation, but its completion must still release its budget and
is discarded if its resource/residency generation is stale. `ReadyForInstall`
is the Phase 2 terminal handoff; upload, atomic publication, rollback, and GPU
fences belong to Phase 3.

### Copy / Adapt / Reject / Vanguard-specific

| Classification | Reference idea | Decision |
|---|---|---|
| Copy | Unreal always-available root/fallback data | Guarantee one complete coarsest LOD before drawable admission. |
| Adapt | Unreal page offsets, sizes, dependency closure, retries | Keep bounded page reads and explicit states; derive fixed-function LOD sets instead of virtual-page dependencies. |
| Copy | RED cooked metadata addressing packed render bytes | Keep cooker-authored layouts/ranges and direct-upload bytes. |
| Reject | RED monolithic render-buffer loading as residency | It cannot independently budget and request optional LOD pages. |
| Reject | Generic `ResourceStreamer` full logical reconstruction for meshes | It stages all optional geometry and loses the durable source after construction. |
| Reject | Nanite virtual page tables, fixups, and GPU transcode | They solve a hierarchical compressed representation Vanguard V1 does not have. |
| Vanguard-specific | `META` plus per-page VPAK segments and two integrity layers | Preserve package CRC/decompression and vmesh SHA-256 as distinct checks. |
| Vanguard-specific | Exact derived `LodInstallSet` | This is the atomic drawability boundary for fixed-function geometry. |

### Required validation and diagnostics

- metadata-only loose and packaged loads read no `GEOM` page bytes;
- dependency-table/META mismatches fail before resource publication;
- package and loose sources produce identical page bytes and content identity;
- concurrent LOD demands coalesce shared page reads and independent cancellation
  preserves remaining interests;
- stored and decoded bytes never exceed configured transient budgets;
- replacing or unmounting a registered source does not invalidate an already
  owned `MeshPageSource`;
- storage CRC/decompression failure, vmesh digest failure, I/O failure,
  cancellation, and budget wait remain distinguishable;
- retry limits are deterministic and integrity failures are never retried;
- stale-generation completions cannot enter `ReadyForInstall`;
- the fallback set is complete, sorted, deduplicated, and entirely marked
  `RequiredForLowestLod`;
- no page/buffer dense index is persisted as cross-recook identity.

Diagnostics must report resource path/generation, content fingerprint, source
kind/generation, page/LOD, active interests, priority/deadline, state, attempt,
stored/decoded byte charges, and structured failure.

### Open questions carried into Phase 3

- exact vertex/index allocation tokens and arena identities;
- whether shared pages are copied once into shared physical ranges or whether
  cooker-per-LOD buffers make sharing irrelevant in the initial implementation;
- staging-buffer ownership and the precise point CPU page bytes may be released;
- all-or-none GPU publication, rollback, budgets, eviction, and multi-queue
  retirement fences.

No Phase 2 question blocks the format, resource object, or asynchronous read
contract.

### Execution-plan consequences

1. Add a production mesh resource object and specialized metadata loader; do
   not register `VMSH` with the full-resource decoder path.
2. Add an owning `MeshPageSource` range-read facility below mesh residency, usable
   for both loose and packaged sources with independent concurrent readers.
3. Wire the mesh cooker/build pipeline to emit `BuildStorageSegments` artifacts
   and exact required material/skeleton dependencies; the existing test-only
   packaging path is insufficient.
4. Preserve `MeshFile` validation and `CollectLodPages` as the single format
   authority rather than duplicating range logic in the renderer.
5. Introduce generation-checked, budgeted page-operation records in the future
   residency manager and hand only verified owned bytes to Phase 3 installation.
6. Keep the generic streamer unchanged for normal resources unless the final
   synthesis finds a reusable owned-source API that does not weaken its current
   contract.

### Exit-gate evaluation

The format independently validates metadata and pages, identifies an exact
drawable fallback set, and maps every LOD to deterministic packed vertex/index
page inputs. The missing production resource/source path now has one owner,
explicit dependencies, bounded asynchronous states, cancellation and retry
rules, and a deterministic `ReadyForInstall` handoff.

```text
PHASE 2 EXIT GATE: PASSED
VMESH VERSION 1: RETAINED
VIRTUAL PAGE/FIXUP MODEL: REJECTED FOR V1
PRODUCTION IMPLEMENTATION: STILL PROHIBITED
```

That explicit request was later received; the completed Phase 3 study follows.

## Phase 3 — Geometry Residency Core

### Phase result

```text
Study work: complete
Exit gate: passed
Blocker: none
Next allowed work: Phase 4 on a later explicit request
Phase 4 state when Phase 3 closed: not started
```

This phase fixes the correctness boundary from verified page bytes to a
published, drawable physical placement. It deliberately does not define GPU LOD
selection, visibility feedback, batch-set keys, indirect argument generation,
or MDI recording; those remain Phase 4.

### Sources inspected

Vanguard evidence is from the live `D:\ENGINE` workspace pinned in Phase 0:

- `source/meshes/include/vanguard/meshes/meshes.hpp:233-299` and
  `docs/formats/vmesh-format.md:55-69`;
- `source/rhi/include/vanguard/rhi/rhi_types.hpp:262-291`, `493-531`,
  `583-644`, and `688-697`;
- `source/rhi/include/vanguard/rhi/rhi_backend.hpp` buffer creation, write,
  copy, transition, fixed-function binding, submission, and residency APIs;
- `source/rhi/nvrhi/src/common_backend.cpp:1213-1217`, `1341-1420`, and
  `2446-2580`;
- `source/rhi/nvrhi/src/resource_lifetime.cpp:912-930` and the corresponding
  lifetime declarations;
- `source/rhi/nvrhi/src/d3d12_backend.cpp:618-787` and `1580-1688`;
- `source/rendering/include/vanguard/rendering/gpu_scene_types.hpp` layout
  version 4 geometry records;
- `source/rendering/include/vanguard/rendering/gpu_scene_lifetime.hpp:150-188`
  and `source/rendering/src/gpu_scene_lifetime.cpp:31-46`, `493-594`, and
  `650-796`;
- `source/rendering/include/vanguard/rendering/gpu_scene_upload.hpp` and
  `source/rendering/src/gpu_scene_upload.cpp:255-540`;
- `source/rendering/src/gpu_scene_definitions.cpp:368-540`, `748-991`, and
  `1077-1254`;
- `source/rendering/include/vanguard/rendering/viewport.hpp:296-303` and
  `source/rendering/src/render_command_system.cpp:185-252`;
- `source/rhi/nvrhi/tests/d3d12_backend_tests.cpp:124-363`, `1522-1881`, and
  `2023-2210`.

Bevy evidence is from the Phase 0-pinned `71ad427aa70292ff18b9d78997c04f75404ec348`
checkout:

- `crates/bevy_render/src/mesh/allocator.rs:31-42`, `103-189`, and `360-429`;
- `crates/bevy_render/src/mesh/slab_allocator.rs:146-190`, `292-327`,
  `391-512`, `624-855`, and `901-999`;
- `crates/bevy_render/src/diagnostic/mesh_allocator_diagnostic_plugin.rs`.

Unreal evidence is from the Phase 0-pinned UE 5.8.1 snapshot:

- `NaniteStreamingManager.h:54-63`, `158-203`, `240-270`, and `317-339`;
- `NaniteStreamingManager.cpp:82-96`, `536-543`, `950-1029`, `1331-1640`,
  and `2674-2984`;
- `NaniteStreamingPageUploader.h` and
  `NaniteStreamingPageUploader.cpp:137-369`;
- `NaniteFixupChunk.h` and `NaniteTranscode.usf` only as contrasts to the
  rejected virtual-page/fixup/transcode design.

### Evidence findings

#### Vanguard already has the required low-level lifetime pieces

Normal RHI buffers are committed, generational resources. Submission records
every referenced resource's last-use fence per graphics, compute, and copy
queue; RHI destruction therefore protects the whole buffer object. The RHI
does not, however, know that byte range A inside a shared arena has been freed.
Suballocation lifetime must be owned by mesh residency.

`CommandListType::CopySync` maps to the graphics queue, while `CopyAsync` maps
to the copy queue (`rhi_types.hpp:270-291`). The current public RHI has no
general graph-facing cross-queue wait/join operation that would make an async
copy publication automatically safe for a subsequent fixed-function graphics
draw. Consequently V1 geometry installation uses `CopySync`: upload and later
GPU Scene publication/draw submissions share graphics-queue order without a CPU
wait or GPU-to-CPU round trip.

The existing `GpuSceneUploader` already demonstrates the desired transaction
shape: reserve a bounded persistently mapped segment, fill all reservations,
record transitions and copies, submit once, and only then commit initial table
publication. Its segment is reusable only after its fence completes. It is
table-specific and is evidence for a separate `GeometryUploader`, not a place
to mix variable-sized mesh bytes into GPU Scene table uploads.

`GpuSceneLifetime` also provides the right retirement pattern: generational
allocations, batch rollback before publication, retirement epochs, mandatory
coverage for every configured queue, and non-blocking collection only after all
fences complete. Geometry ranges need a parallel range-level lifetime owner.

One integration gap is explicit: `RenderFrameSubmission` currently contains
only a CPU serial. It is not a GPU completion token. The renderer must hand the
residency/lifetime epilogue the latest actual `ResidencyFenceSet` from all queues
that may have referenced published geometry. A frame serial or fixed frame delay
cannot authorize range reuse.

#### Bevy contributes transaction structure, not its moving-slab policy

Bevy groups meshes into slabs to reduce vertex/index rebinding, separates
vertex and index slab identities, stages allocation/deallocation, and keeps new
allocations pending until upload completes. It also segregates incompatible
element layouts and gives oversized allocations dedicated buffers. These are
directly useful principles.

Bevy may grow a published slab by creating a larger buffer, copying the old
prefix, and replacing the physical buffer. Vanguard must reject that behavior:
its fixed-function batch identity includes the physical vertex arena set and
index arena. Moving a published slab would invalidate cached bindings and make
range retirement much harder to prove. Vanguard grows capacity by adding a new
fixed chunk. Empty chunks are released only after range retirement fences,
rather than being destroyed immediately at allocator commit.

Bevy's diagnostics report useful aggregate slab counts and bytes, but are too
small for Vanguard's streaming and multi-queue failure analysis. Fragmentation,
pending and retiring bytes, generation failures, staging pressure, pins, and
eviction reasons must also be observable.

#### Unreal contributes bounded pressure and safe replacement rules

Nanite uses fixed-capacity resident/pending structures, bounded per-frame
installation, dependency reference counts, an LRU-like order, and a rule that a
page referenced this update or carrying references cannot be selected as the
victim. If staging or pool capacity is unavailable, work is postponed rather
than forcing an unsafe install. An old slot is unregistered before replacement,
and the new page is not installed until its upload/fixup transaction is ready.

Vanguard copies bounded work, pinning, pressure deferral, and replacement safety.
It does not copy virtual page slots, dependency fixups, GPU transcode, or
in-place slot overwrite. A Vanguard `LodInstallSet` contains final fixed-function
bytes and remains the publication/eviction atom.

### Locked physical arena model

#### Vertex arenas

A `VertexArenaSet` is one physical chunk for one exact vertex-layout
fingerprint. It owns one normal device-local RHI vertex buffer for each binding
in that layout. All binding buffers have the same logical vertex capacity, and
one allocation reserves the same `[firstVertex, vertexCount)` interval in every
binding. This is required because indexed drawing supplies one `baseVertex`
shared by all fixed-function vertex streams.

```text
VertexArenaSetId
  slot index + nonzero generation

VertexAllocation
  VertexArenaSetId
  firstVertex
  vertexCount
  allocation generation
```

The layout fingerprint includes the binding count and, for every binding, its
stride, input rate, and fixed-function attribute layout. A set cannot mix
incompatible layouts. Its GPU buffers use `Vertex | CopyDestination`; they do
not receive `CopySource` merely to enable future compaction.

The first vertex must make every binding's byte offset legal. For a required
copy/buffer alignment `A` and binding stride `S`, the allocator aligns
`firstVertex` to `A / gcd(A, S)` vertices; across bindings it uses the least
common multiple of those quanta. Descriptor/device alignment requirements are
validated before reservation and overflow in the LCM calculation is a hard
error.

#### Index arenas

Index storage is independent because vertex and index capacity exhaust
independently. `IndexArenaId` is also a slot plus nonzero generation. V1 keeps
separate arenas for `UInt16` and `UInt32`, and an allocation records byte offset,
byte size, index count, format, and allocation generation. Buffers use
`Index | CopyDestination`; offsets satisfy the maximum of format, page, copy,
and backend alignment requirements.

Separating formats is not an API necessity, but it makes validation,
fragmentation accounting, fixed-function binding, and the later batch key
unambiguous. Phase 4 will decide the complete binding-compatible batch identity.

#### Chunk and free-range policy

- Published chunks never move, resize, or replace their RHI buffer identities.
- Exhaustion creates another fixed-size chunk of the same layout/format class.
- A request larger than the normal chunk class gets a dedicated oversized
  chunk rounded to validated alignment.
- Free ranges are address-ordered and coalesced; allocation uses a deterministic
  best-fit candidate with address as the tie breaker.
- Allocation, cancellation, and retirement operate as staged batches. No
  half-reserved split-stream set becomes visible.
- V1 performs no live compaction. Fragmentation diagnostics and measurements,
  not speculation, decide whether a later offline/idle relocation facility is
  warranted.
- Configurable per-class and global arena-count limits prevent accidental
  unbounded rebinding. Phase 5 will choose defaults from measurements rather
  than copying Bevy's slab sizes.
- Device-native explicit residency operates on whole RHI allocations. It may be
  used only as a whole-empty-chunk optimization; it never represents eviction
  of one LOD range inside a mixed arena.

The current cooker emits dense complete vertex and index buffers per LOD. V1
therefore allocates each LOD placement independently even if two LOD install
sets happened to read a shared source page. Page I/O remains coalesced, but no
GPU subrange-sharing/refcount layer is added without cooker evidence that it
would save meaningful memory.

### Locked installation and publication transaction

One `LodPlacement` owns all vertex allocations, index allocations, generated
`GpuGeometryHandle`s, and page identities required by one exact
`LodInstallSet`. It is visible only in `Resident`.

```text
Absent / Requested
  -> Reserving
  -> Reserved
  -> Staging
  -> UploadSubmitted
  -> PublishingDefinitions
  -> Resident
  -> RetireQueued
  -> Retiring
  -> Reclaimed
```

The transaction is:

1. Revalidate resource/residency generation and the complete sorted
   `LodInstallSet`; all page payloads must be verified and present.
2. Charge pending logical/committed budgets, then reserve every required vertex
   and index range plus upload staging as one plan. Any reservation failure
   cancels the whole plan.
3. Copy final cooker-authored bytes into uploader-owned mapped staging and mark
   every reservation complete. No GPU definition refers to the destination yet.
4. Record all buffer transitions and copies into one or more bounded
   `CopySync` graphics command lists and submit them. Adjacent copies to the same
   destination may be coalesced without changing page validation identity.
5. After successful geometry submission, acquire all `GpuGeometry` definitions
   as one transactional batch. Their table upload is a later graphics-queue
   submission, so graphics consumers observe completed geometry bytes without a
   CPU fence wait.
6. Atomically publish the complete `LodPlacement` in the residency record only
   after every geometry definition succeeds. Partial geometry handles and
   reserved ranges are never returned to consumers.
7. Release Phase 2 page payloads after geometry submission succeeds. If command
   recording or submission fails, retain them for bounded retry or explicit
   cancellation. The selected regular or bounded overflow staging buffer owns the submitted bytes until
   its fence completes.

Failure before GPU submission immediately cancels unpublished reservations.
Failure, cancellation, or stale generation after submission retires the
unpublished placement behind at least the returned upload fence; it cannot put
the ranges directly on a free list. If definition publication partially
progresses internally, its batch API must roll back definition references and
the placement still follows fence retirement.

No stage blocks for memory or a fence. Exhausted staging segments, arena limits,
budgets, or retirement epochs defer bounded work to a later tick. Failure of an
optional LOD preserves the current resident fallback. Failure of the fallback
prevents drawable admission.

Phase 3 publishes geometry handles and an atomic resident LOD placement. The
canonical mutable resident mask, best-resident LOD selection, and replacement
of a live renderable definition are intentionally deferred to Phase 4.

### Locked budgets and eviction

The residency owner tracks separate quantities because none can safely stand in
for another:

```text
CPU stored/decoded page bytes
upload-staging committed and in-flight bytes
GPU committed arena bytes
GPU live resident allocation bytes
GPU pending installation bytes
GPU retiring-but-not-reusable bytes
free and unusable-fragment bytes inside committed chunks
```

Pending, resident, and retiring placements all count against the hard GPU
admission cap. Committed arena bytes include unused capacity and dedicated
chunks. `MemoryBudgetSnapshot` may lower the allocator's soft target under OS
pressure, but the geometry allocator remains the semantic budget authority.

Eviction begins at a configurable high watermark and continues to a lower
watermark to avoid oscillation. Before selecting victims, maintenance collects
completed retirement epochs and cancels no-longer-demanded unpublished work.
Victims are complete optional LOD placements, ordered deterministically by:

1. no active demand or binding pin;
2. not fallback-required by a live mesh binding;
3. not installing, publishing, or already retiring;
4. least recently required, then lowest priority, then finest dispensable LOD;
5. stable resource-generation/LOD identity as the tie breaker.

The coarsest fallback is pinned while any live demand/binding requires that mesh
to remain drawable. It is not immortal: when the mesh has no owners, its whole
residency may retire. If the hard cap cannot admit a new fallback without
violating existing fallback pins, admission waits or fails with budget pressure;
the manager does not silently destroy an existing mesh's drawability.

V1 eviction is LOD-atomic and does not overwrite a live range with incoming
bytes. The old placement first disappears from future publication/bindings,
then enters retirement. Only a later fence-complete collection frees its ranges.

### Locked retirement and reuse

Geometry placement lifetime uses a bounded ring of retirement epochs patterned
after `GpuSceneLifetime`. The residency/rendering authority accumulates retired
placements, then seals the open epoch with the latest real RHI submission fence
for every configured queue that could reference geometry or its definitions.
V1 conservatively requires graphics, compute, and copy coverage, matching the
current GPU Scene default.

```text
remove placement from future publication
  -> release/retire GPU Scene definitions and binding owners
  -> append physical allocations to open geometry retirement epoch
  -> renderer submits final relevant work
  -> seal with ResidencyFenceSet { graphics, compute, copy }
  -> non-blocking poll
  -> reclaim ranges only when every fence is complete
```

The future frame execution shell must expose that queue-fence set directly;
`RenderFrameSubmission::serial` is insufficient. A missing required fence rejects
sealing a non-empty epoch. Exhausted epochs defer further reclamation/admission;
they never trigger early reuse.

Every reuse increments a nonzero allocation generation. A stale page completion,
upload completion, placement token, or free request fails validation instead of
redirecting to a new owner. An empty physical chunk releases its RHI `BufferRef`s
only after all contained allocations have been reclaimed; the RHI's own
generational lifetime then provides a second whole-resource fence barrier.

### Copy / Adapt / Reject / Vanguard-specific

| Classification | Reference idea | Decision |
|---|---|---|
| Copy | Bevy pending versus resident allocations and batched commit | Reserve and upload complete LOD plans before publication. |
| Adapt | Bevy layout-specific slabs and dedicated large allocations | Use non-moving fixed chunks keyed by vertex layout or index format. |
| Reject | Bevy growth by copying and replacing a published slab | Physical buffer identity is part of Vanguard fixed-function batching and remains stable. |
| Adapt | Bevy immediate empty-slab removal | Release an empty chunk only after geometry range retirement and RHI lifetime fences. |
| Copy | Unreal bounded pending/install capacity and pressure deferral | Never block or overrun staging/pool limits to satisfy optional quality. |
| Adapt | Unreal fallback/dependency/in-flight pins and LRU replacement | Pin complete fallback/LOD placements; evict only an unreferenced LOD atom. |
| Reject | Nanite virtual slot overwrite, fixups, and GPU transcode | Vanguard uploads final bytes to direct fixed-function ranges. |
| Copy | Vanguard GPU Scene transactional publication and retirement epochs | Give geometry bytes a separate variable-range transaction and epoch owner. |
| Vanguard-specific | `CopySync` graphics-queue geometry upload in V1 | Avoid an unsupported cross-queue publication join and all CPU waits. |

### Required validation and diagnostics

Allocator tests must cover split-stream synchronized ranges, incompatible
layouts, alignment LCM overflow, `UInt16`/`UInt32` segregation, best-fit and
coalescing, dedicated oversize chunks, arena limits, generation reuse, and the
guarantee that a published buffer identity never moves.

Fault injection is required after each transaction boundary: partial reservation,
staging exhaustion, page copy, command recording, submission, definition batch,
publication, cancellation, and stale completion. Every path must prove that it
either cancels unpublished reservations or retires submitted ranges, with no
partially drawable LOD.

Budget/eviction tests must cover hard-cap fallback admission, optional-LOD
failure preserving fallback, high/low hysteresis, pending and retiring charges,
deterministic victim ordering, live/fallback/in-flight pins, and pressure that
defers rather than waits.

Retirement tests must reject partial queue coverage, retain ranges until the
last graphics/compute/copy fence, handle epoch exhaustion, reject stale frees,
and delay empty-chunk release. Device-loss/shutdown tests must report or safely
discard every pending transaction according to the RHI shutdown contract.

Diagnostics must expose, per layout/format class and globally:

- chunk/dedicated counts; committed, live, pending, retiring, and free bytes;
- total free bytes, largest free range, allocation count, and fragmentation;
- upload batches/bytes/copies, staging occupancy, deferrals, and failures;
- install state, resource generation, LOD, pages, and geometry handles;
- eviction attempts, selected bytes, pin/rejection reasons, and fallback pins;
- retirement epoch occupancy, queue fences, collection latency, stale operations,
  and allocation failures.

### Open questions carried into Phase 4 and Phase 5

- Phase 4 must define how resident `LodPlacement`s update the canonical GPU
  renderable/LOD representation without duplicating identity.
- Phase 4 must lock the complete physical binding/batch-set key and decide
  whether arena-count limits need a stronger placement-affinity heuristic.
- Phase 4 must specify compute/graphics consumers precisely; if future compute
  reads geometry bytes directly, an explicit cross-queue synchronization path
  is required before enabling async-copy installation.
- Phase 5 must choose measured chunk sizes, watermarks, staging capacity,
  retirement-epoch count, per-tick install/eviction limits, and failure policy
  defaults. The architecture does not hard-code Bevy or Unreal numbers.

No Phase 3 question blocks the physical allocator, transaction, budget,
eviction, or retirement model.

### Execution-plan consequences

1. Add a renderer-global, serialized geometry residency owner below future
   proxy/batch consumers and beside existing GPU Scene authorities.
2. Add generational non-moving vertex arena sets, format-specific index arenas,
   staged range allocation, and a separate graphics-queue `GeometryUploader`.
3. Add an atomic LOD installation adapter that batch-acquires existing GPU Scene
   geometry definitions only after geometry upload submission.
4. Add geometry retirement epochs and a renderer epilogue that supplies actual
   graphics/compute/copy fence coverage; do not use frame serials or delays.
5. Add explicit CPU, staging, committed, live, pending, and retiring budgets,
   LOD-atomic eviction, fallback pins, pressure hysteresis, and bounded work.
6. Keep the current RHI whole-resource lifetime and optional native residency
   policy; do not teach it mesh suballocation semantics.

### Exit-gate evaluation

Residency is bounded by explicit page, staging, arena, pending, resident, and
retirement limits. Split streams reserve one synchronized vertex interval.
Installation exposes either a complete LOD or nothing, and every failure path
has immediate cancellation or fence-delayed rollback. Optional eviction cannot
remove a required fallback, and no submitted physical range becomes reusable
until every relevant queue fence completes.

```text
PHASE 3 EXIT GATE: PASSED
PUBLISHED ARENA MOVEMENT: REJECTED
ASYNC COPY QUEUE FOR V1 GEOMETRY INSTALL: REJECTED
PARTIAL LOD PUBLICATION: REJECTED
PRODUCTION IMPLEMENTATION: STILL PROHIBITED
```

That explicit request was later received; the completed Phase 4 study follows.

## Phase 4 — GPU Consumption, Feedback, Batching, and MDI

### Phase result

```text
Study work: complete
Exit gate: passed
Blocker: none
Next allowed work: Phase 5 on a later explicit request
Phase 5 state when Phase 4 closed: not started
Production implementation: prohibited
```

### Sources inspected

#### Vanguard live workspace

The Phase 0 Vanguard revision and live-workspace rule still apply. Several of
these paths are pre-existing user changes or untracked production work; this
study only read them.

- `source/rendering/include/vanguard/rendering/gpu_scene_types.hpp:10-221,
  323-382` — layout V4, generational identities, immutable topology records,
  physical geometry ranges, and per-view LOD bias.
- `source/rendering/include/vanguard/rendering/gpu_scene_definitions.hpp:7-57,
  117-154` and `source/rendering/src/gpu_scene_definitions.cpp:748-1012` —
  immutable content-addressed geometry/material/renderable acquisition.
- `source/rendering/include/vanguard/rendering/render_scene_gpu.hpp:37-51,
  150-170` and `source/rendering/src/render_scene_gpu.cpp:789-823,1014-1043` —
  one resolved renderable handle per mesh proxy and explicit binding clear.
- `source/rendering/include/vanguard/rendering/gpu_scene_visibility.hpp:9-185`,
  `source/rendering/src/gpu_scene_visibility.cpp:27-193`, and
  `source/rendering/shaders/gpu_scene_visibility.vsl:1-187` — bounded
  per-view candidate/result partitions and current frustum-only compaction.
- `source/rendering/include/vanguard/rendering/render_phase.hpp:7-210` — stable
  phase keys, compact sealed IDs, four sort modes, and the 64-phase limit.
- `source/rendering/include/vanguard/rendering/pipeline_cache.hpp:10-63` and
  `source/rendering/src/pipeline_cache.cpp:375-420` — asynchronous pipeline
  request status, nonblocking polling, and optional blocking waits.
- `source/rendering/src/frame_renderer.cpp:35-50` — frame preparation exists,
  but no render-graph executor is installed yet.
- `source/rhi/include/vanguard/rhi/rhi.hpp:151,174-175`,
  `source/rhi/include/vanguard/rhi/rhi_types.hpp:866-1008`, and
  `source/rhi/nvrhi/src/common_backend.cpp:2735-2750,3035-3070` — fixed-function
  pipeline state plus fixed-count and counted indexed indirect drawing.
- `source/meshes/include/vanguard/meshes/meshes.hpp:301-313` — at most 64 LODs.
- `source/rendering/include/vanguard/rendering/visibility_feedback.hpp:23-187`
  and `source/rendering/src/visibility_feedback.cpp:400-633` — CPU probe
  feedback, inspected to establish that it is not mesh-streaming feedback.

#### Bevy classic mesh path

The pinned checkout remains commit
`71ad427aa70292ff18b9d78997c04f75404ec348`.

- `crates/bevy_pbr/src/render/mesh.rs:2792-2825` — physical mesh slabs are in
  the multi-draw compatibility key while mesh identity remains the finer bin
  identity.
- `crates/bevy_render/src/render_phase/mod.rs:2029-2075` — bins contain
  batchable work and batch sets contain work that may be multi-drawn.
- `crates/bevy_render/src/batching/gpu_preprocessing.rs:879-925,
  1090-1115,1870-2263,2431-2720` — CPU-owned indirect batch-set shells,
  per-phase indexed/non-indexed storage, bin unpacking, and GPU preprocessing.
- `crates/bevy_pbr/src/render/mesh_preprocess.wgsl:320-396` — GPU visibility
  compacts instances and atomically accumulates per-batch counts.
- `crates/bevy_pbr/src/render/build_indirect_params.wgsl:1-141` — the GPU
  omits empty counted commands, reserves command slots atomically, and writes
  indexed draw arguments.
- `crates/bevy_pbr/src/render/reset_indirect_batch_sets.wgsl:1-31` and
  `unpack_bins.wgsl:1-89` — per-pass count reset and GPU expansion of CPU bins.
- `crates/bevy_pbr/src/render/mesh.rs:4488-4615` — CPU recording chooses
  counted MDI when supported and otherwise records fixed-count MDI over a known
  range. Neither path reads the current visible count on the CPU.

#### Unreal Engine 5.8.1 snapshot

The Phase 0 Unreal version/path pin still applies. The additional Phase 4
corpus is content-pinned by these SHA-256 values:

```text
CD1F1DDD7233ADF64B3080E442755D3B0F19E18AC5B0B52DF157C2F776F3628D  MeshDrawCommands.cpp
07408C92F60218111D05C1D8FE686995EAF17DDAC1D9AD2210D812BA289B0575  BuildInstanceDrawCommands.usf
5373DCC1B3D788C4C4878D6CF2C3FCF626CCBCBAAB51246D2FD5ED6FF519E943  CompactVisibleInstances.usf
72B062C855B91E8DA0FFDC4A578652D3C6C38674C7DDDEE0842C4F52A3E95563  NaniteClusterCulling.usf
2DED65983DBA488C1A3606F85D43B97025FF7A7F426041FB0D30A32AD9EC8556  NaniteStreamingManager.cpp
```

- `MeshDrawCommands.cpp:276-452` — pipeline and state/index-buffer identities
  are explicit sort/batching boundaries.
- `MeshDrawCommands.cpp:483-585` — adjacent compatible state buckets become
  one instanced command while instance/primitive IDs remain separate data.
- `MeshDrawCommands.cpp:1643-1767` — stable CPU draw commands remain the
  recording shell while instance culling supplies indirect buffers.
- `BuildInstanceDrawCommands.usf:80-340` and
  `CompactVisibleInstances.usf:17-153` — GPU Scene instance visibility writes
  compact instance data and indirect instance counts for predeclared commands.
- `NaniteClusterCulling.usf:579-584` — visible leaf traversal emits future
  streaming requests.
- `NaniteStreamingManager.cpp:2281-2357` — readback is queued, the latest
  available result is locked, and processing may continue asynchronously. This
  is delayed streaming control, not current-frame draw submission.

### Current-code findings

Vanguard already has the correct public identity: instances contain a
`GpuRenderable` index and RenderScene bindings retain a generational
`GpuRenderableHandle`. It does not yet have the mutable resident-placement
layer needed below that identity. `GpuRenderableDefinition` currently owns
geometry-bearing primitives and is immutable/content-addressed. Rebuilding that
definition whenever an optional LOD arrives would change the handle and force
every bound proxy to rebind.

The current visibility shader only rejects inactive/nonresident/masked/frustum
candidates and compacts `GpuInstanceIndex`. It does not select LODs, emit mesh
streaming requests, expand phases/primitives, bin work, or generate indirect
arguments. Those are missing systems, not hidden behavior.

`GpuPhaseParticipation::pipelineBucket` claims to contain a registered stable
GPU-batching ordinal, but no pipeline-bucket registry exists. The pipeline cache
can finish asynchronously and exposes `HasSucceeded`/`TryWait`, while its
blocking `Wait` API would be unsafe in frame recording. A batch-shell registry
must bridge these systems.

The current `VisibilityFeedbackService` evaluates CPU-owned visibility probes.
It must not be renamed or reused as mesh LOD feedback; their identities,
latency, overflow behavior, and ownership are different.

### Canonical identity and mutable placement

`GpuRenderableHandle` remains the only GPU mesh/renderable identity exposed to
instances, RenderScene proxies, materials, visibility, and draw work. No
`GpuResidentMeshHandle`, per-LOD renderable handle, or parallel component-facing
identity is introduced.

The next GPU Scene layout version must split records by mutation frequency:

```text
immutable, content-addressed topology
  GpuRenderable -> authored LOD span and phase mask
  GpuLod -> authored thresholds and primitive span
  GpuPrimitive -> default material, stable submesh, flags, phase span
  GpuPhaseParticipation -> phase, pipeline-bucket identity, flags, sort bias

mutable residency placement under the same renderable index
  GpuRenderableResidency -> renderable generation, resident LOD mask,
                            fallback LOD, placement revision
  GpuPrimitivePlacement  -> geometry index + generation for an installed LOD
  GpuPhasePlacement      -> normal/mirrored batch-shell and geometry-bin handles
```

The exact ABI packing/table ordinals are Phase 5 synthesis work, but these
semantic tables and references are locked. `GpuPrimitive.geometry` can no
longer be the immutable topology authority. `GpuGeometryRange` must also expose
the owning `VertexArenaSetId` explicitly; deriving a fixed-function binding key
from unrelated stream-buffer indices is rejected.

All authored LOD/primitive/phase topology is published when the fallback
renderable becomes ready. Optional geometry definitions and physical placements
are retained by the residency manager only while their LOD is installed. A
renderable's parallel residency slot is indexed by its existing renderable
index and carries the same generation, so it is indirection beneath the
identity rather than a new identity.

Installation publication is ordered:

```text
geometry bytes submitted and definitions acquired
  -> register/ref stable shell and bin placements
  -> upload every primitive and phase placement for the complete LOD
  -> publish the new residency revision and set its resident-mask bit
  -> later visibility/LOD compute may select it
```

Eviction performs the inverse safety boundary:

```text
clear the LOD resident-mask bit before future visibility
  -> submit/complete the ordered GPU Scene mutation
  -> retire shell/bin and geometry-definition references
  -> retire physical arena ranges behind the Phase 3 fence set
```

V1 placement updates and all consumers run in graphics-queue order. An async
compute consumer cannot be enabled until the graph supplies an explicit
graphics/compute join for placement publication and eviction. Placement slots,
shell slots, bin slots, geometry definitions, and physical ranges are
generational and cannot be reused while an older frame may reference them.

### Desired and best-resident LOD

LOD zero is the finest authored LOD and `lodCount - 1` is the coarsest required
fallback, matching Phase 2. For every coarse-visible candidate and view, the GPU:

1. computes projected coverage/error from instance bounds and `GpuView`;
2. applies the view's `lodBias` and chooses the desired authored LOD;
3. tests the 64-bit resident mask;
4. if desired is absent, scans increasing authored indices toward coarser LODs;
5. selects the first resident LOD and emits the instance/renderable/LOD tuple.

The mask is exactly 64 bits because `.vmesh` validation already caps LOD count
at 64. The shader never selects a finer-than-desired LOD merely to hide missing
coarser residency. A live drawable renderable must always have its fallback bit
set. No set bit is a contract violation: reject the candidate, increment a hard
diagnostic, and do not read stale placement data.

V1 draw selection is deterministic and stateless per view. Residency demand
uses high/low watermarks and age hysteresis on the CPU to absorb feedback delay
and threshold noise. An unbounded per-view/per-instance LOD-history table is
rejected. Phase 5 may add a bounded visual-history policy only if measurements
show that stateless thresholds are insufficient.

### Mesh LOD streaming feedback

Mesh feedback is generated for coarse-visible candidates before any future
aggressive occlusion stage can erase prefetch demand. Only views marked as
streaming authorities participate by default. Main views have normal priority;
shadow, reflection, editor, and auxiliary views may be disabled or assigned a
lower priority category.

Frame-local aggregation has one entry per live renderable slot. The graph clears
the active range, and shaders atomically aggregate:

```text
renderable index and generation
placement revision observed
finest desired LOD across contributing views
selected resident LOD observed
maximum fixed-point priority/projected coverage
view-category mask
```

A compaction pass writes a bounded `GpuMeshLodRequest` list plus both clamped
and requested counts. This deduplicates instances and views before readback.
The residency manager consumes the newest fence-complete slot from a bounded
readback ring without waiting, discards stale renderable/resource generations,
touches the selected resident LOD, and requests the desired LOD for a future
frame.

Feedback readback is explicitly allowed because streaming is a future-frame
control loop. It is explicitly forbidden as an input to current-frame command
recording. Missing or overflowing feedback is conservative: it cannot expire
unreported demand or trigger eviction. It extends the demand grace period,
records diagnostics, and may cause bounded capacity growth/configuration work.
It never stalls rendering and never removes the fallback.

Newly admitted renderables publish the fallback before proxy visibility. CPU
camera/world prediction may prewarm the desired or next-better LOD, while GPU
feedback becomes authoritative once available. Prewarming is advisory and
budgeted; it does not create a second visibility or residency owner.

### Fixed-function batch shell and geometry bin

One batch shell is the exact state the CPU can bind once around one indexed MDI
call. Its authoritative key is:

```text
RenderPhaseId
PipelineBucketHandle (generation included)
VertexArenaSetId (generation included; identifies every split vertex buffer)
IndexArenaId (generation included)
GpuIndexFormat
dynamic/fixed state profile not baked into the pipeline bucket
```

The pipeline bucket resolves the complete graphics pipeline description:
shader/interface permutation, vertex layout, topology, raster state, depth/
stencil state, blend state, and attachment signature. Those fields are not
duplicated in the shell key. Dynamic stencil reference or another state set
outside the PSO must be represented by the final state-profile field.

Material parameter/resource identity is not a shell field for the normal path:
materials are selected through GPU Scene/bindless tables. A material that
changes shader interface, alpha/two-sided fixed state, or another PSO property
resolves to a different pipeline bucket. A future nonbindless compatibility
path must add its material binding group to the key explicitly.

Negative-scale instances cannot share a fixed cull/front-face state with normal
instances. Each phase placement therefore resolves normal and mirrored pipeline
variants/shells where culling requires it. Index/vertex offsets, index count,
base vertex, primitive/material identities, and instance lists do not belong in
the shell key.

Within a shell, a stable geometry bin represents one indexed draw geometry and
its fixed indirect arguments. Many instances with different transforms and
bindless material overrides may compact into that bin. Shell and bin slots are
generational, non-moving while referenced, reference-counted by resident phase
placements, and fence-retired before reuse.

A new `PipelineBucketRegistry` is required. It registers only asynchronously
successful graphics pipelines, publishes a stable generational bucket with its
complete compatibility description, and creates/retires shell variants. Frame
execution polls readiness; it never calls `PipelineRequest::Wait`. A required
fallback pipeline must be ready before drawable admission. An unavailable
optional pipeline skips its phase with a visible diagnostic or uses an
explicitly compatible fallback selected before shell creation.

### GPU work and CPU recording sequence

For each frame/view wave, the render graph orders:

```text
publish GPU Scene placement mutations
  -> clear visibility, bin, shell-count, indirect-count, and feedback outputs
  -> coarse visibility + desired/best-resident LOD selection
  -> expand selected LOD primitives and phase participations to shell/bin pairs
  -> count instances per geometry bin
  -> prefix/scan bin instance ranges within each shell
  -> scatter compact draw-instance records
  -> build/compact nonempty indexed indirect arguments and shell counts
  -> graphics pass binds each known shell and issues indexed MDI
  -> copy compact mesh feedback into a delayed readback-ring slot
```

The compact draw-instance record contains the GPU instance and primitive
identity needed by shaders to resolve transforms, default/override material,
and other per-instance data. It does not replace fixed-function vertex input;
vertex and index fetch still use the CPU-bound arena buffers and indirect
`firstIndex`/`baseVertex`.

The CPU freezes an immutable active-shell snapshot for the frame. For every
eligible view/phase it visits that stable registry order, binds the pipeline,
all split vertex buffers, index buffer/format, draw-instance data, indirect
argument buffer, and count buffer, then records
`DrawIndexedPrimitiveIndirectCount` with known offsets and maximum count. A
zero GPU count executes no commands. The CPU never asks which bins are visible.

This is the no-readback proof:

1. shell identities, bindings, argument ranges, count offsets, and maxima exist
   before visibility;
2. GPU dependencies guarantee argument/count generation completes first;
3. the GPU count, not the CPU, chooses the number of commands inside a shell;
4. CPU recording visits known shells without mapping any visibility buffer;
5. there is no per-frame sort of visible instances or visible draw commands.

### Bounded work and overflow policy

Rendering output must not silently drop geometry. A view's visible-instance
capacity is at least its known candidate count because coarse visibility emits
at most one selected LOD record per candidate. Primitive/phase expansion uses a
CPU-known conservative maximum packet count from immutable renderable metadata.

If a whole view exceeds configured transient capacity, the candidate planner
partitions it into deterministic waves whose worst-case expansion fits. Each
wave repeats compute and shell draws with different frame-buffer partitions;
it does not wait for a GPU count. Capacity arithmetic is checked and all
overflows become frame-planning failures/diagnostics, never discarded draws.

One shell may be split into stable fixed-size segments to satisfy indirect
buffer, count, maximum-command, or backend offset limits. The CPU records one
call per segment. Argument/count offsets must satisfy both Vanguard's `u64` API
and the narrower native backend limits verified during Phase 5.

Counted indexed MDI is the primary path. If counted indirect is unavailable but
fixed-count indexed MDI exists, the GPU writes one command per known bin slot
and writes `instanceCount = 0` for empty bins; the CPU records the known fixed
count. If indexed indirect itself is unavailable, the production GPU-driven
path is unsupported. A separate CPU direct/debug renderer may be selected at
device admission, but it must not read back GPU visibility to emulate support.

### Phase ordering limits

`Unordered` and `State` mesh phases directly use stable shells. `FrontToBack`
may use optional approximate GPU depth buckets within compatible shells because
its ordering is an optimization, not a correctness contract.

Exact `BackToFront` blending cannot be globally ordered across different
pipeline/vertex/index shells by ordinary MDI. It is not sent through this
batching path. Transparent rendering remains a separate renderer contract
(future CPU/direct sorting, a dedicated GPU sort, or an OIT design). This does
not authorize a GPU-to-CPU visible-set readback and does not weaken opaque/depth/
shadow/velocity/selection no-readback guarantees.

### Copy / Adapt / Reject / Vanguard-specific

| Classification | Reference idea | Decision |
|---|---|---|
| Copy | Bevy CPU-known batch sets plus GPU-written command count | Record stable shells without learning current visibility. |
| Copy | Bevy fixed-count fallback with zero-instance empty commands | Use when counted indexed MDI is unavailable. |
| Adapt | Bevy physical slabs in batch-set compatibility | Use generational non-moving `VertexArenaSetId` and `IndexArenaId`; do not copy moving slab growth. |
| Adapt | Unreal stable mesh draw command/state bucket with GPU instance compaction | Split Vanguard into stable shell/bin registry and frame-local GPU draw-instance/argument output. |
| Copy | Unreal delayed latest-available streaming readback | Use only for future-frame LOD demand, never draw issuance. |
| Adapt | Nanite visible-leaf requests | Emit ordinary renderable/LOD demand, not virtual page addresses or cluster requests. |
| Reject | Replacing immutable `GpuRenderableDefinition` for every LOD residency change | It changes canonical identity and forces proxy rebinding. |
| Reject | Per-frame CPU visible sorting/binning for GPU-driven phases | GPU expansion and bin counters own current-frame visible work. |
| Reject | General SSBO vertex pulling | Fixed-function split-stream arenas remain the normal static-mesh path. |
| Reject | Exact transparent ordering inside arbitrary cross-shell MDI | It is not representable by one global draw order. |
| Vanguard-specific | Parallel mutable placement tables under `GpuRenderableHandle` | Preserve existing GPU Scene identity and generational retirement. |
| Vanguard-specific | Graphics-queue V1 placement/visibility ordering | Avoid inventing an unsupported async compute join. |

### Required validation and diagnostics

GPU Scene tests must cover one stable renderable handle across optional LOD
install/evict, topology/placement generation mismatch, resident-mask publication
ordering, fallback preservation, no-placement rejection, and no slot reuse before
the covering fences.

LOD tests must cover projected thresholds and bias, all 64 mask bits, desired
resident selection, coarser fallback search, absence of finer substitution,
multiple views, camera cuts, and deterministic stateless results.

Feedback tests must cover multi-instance/view deduplication, priority aggregation,
stale generations/revisions, readback-ring latency, no-ready-result behavior,
overflow conservatism, prewarming, demand aging, and proof that rendering never
waits for or consumes the readback.

Batching tests must vary every shell-key field independently; prove that offsets,
materials, and instances do not split compatible bins; prove normal/mirrored
separation; and cover shell/bin reference, generation, snapshot, retirement,
pipeline failure/hot reload, and arena-count pressure.

GPU execution tests must compare direct reference draws against counted and
fixed-count MDI, cover zero/nonzero/multiple bins, all views/phases/waves/segments,
capacity arithmetic, count resets, graph dependency omissions, and backend
command/offset limits. RenderDoc validation must still show the fixed-function
vertex/index buffers and reconstructed mesh for an indirect draw.

Diagnostics must expose selected/desired/fallback LOD distributions, resident
masks/revisions, missing-placement errors, feedback requested/written/overflow/
age/stale counts, shell/bin counts and key splits, active/empty commands, draw
instances, wave/segment counts, pipeline readiness/failures, MDI fallback mode,
and per-pass GPU timings.

### Open questions carried into Phase 5

- Lock the byte-exact next GPU Scene ABI: table ordinals, structure packing,
  placement-generation fields, and CPU/Slang static assertions.
- Choose measured capacities for feedback/readback rings, shell/bin registries,
  wave expansion, indirect segments, and per-tick shell mutations.
- Verify every backend's counted/fixed indexed indirect capabilities, alignment,
  maximum-command count, and native offset width; define device admission.
- Decide whether measurements justify a bounded temporal visual-LOD history or
  optional front-to-back GPU depth buckets.
- Specify the separate transparent renderer boundary without expanding mesh
  residency into a general transparency study.
- Reconcile shell/bin retirement fences with Phase 3 geometry and existing GPU
  Scene retirement epochs into one final teardown sequence.

None of these questions changes the canonical identity, no-readback submission,
or fixed-function batch compatibility decisions.

### Execution-plan consequences

1. Version GPU Scene and separate immutable topology from mutable renderable,
   primitive, and phase placement while keeping `GpuRenderableHandle` canonical.
2. Add ordered residency publication/eviction batches and generation validation
   to `GpuSceneUploader` integration.
3. Add a nonblocking mesh-feedback aggregation/compaction/readback-ring service
   distinct from `VisibilityFeedbackService`.
4. Add a generational `PipelineBucketRegistry` and stable, reference-counted,
   fence-retired batch-shell/geometry-bin registry.
5. Extend visibility into bounded LOD selection, expansion, bin counting,
   scatter, indirect construction, and count generation passes.
6. Add a shell executor that records counted or fixed-count indexed MDI using
   CPU-known ranges only, plus a separate unsupported/debug policy for devices
   without indexed indirect.
7. Keep exact back-to-front transparency out of the shell executor.

### Exit-gate evaluation

GPU Scene topology, mutable placement, visibility, feedback, residency,
batching, and indirect drawing all use the existing renderable index/generation.
LOD changes do not rebind proxies. Streaming readback is delayed and can affect
only future residency. Current-frame visibility, binning, arguments, and counts
remain GPU data, while CPU recording needs only immutable shell snapshots.

```text
PHASE 4 EXIT GATE: PASSED
SECOND GPU MESH IDENTITY: REJECTED
CURRENT-FRAME VISIBLE-SET READBACK: REJECTED
PER-FRAME CPU VISIBLE SORT FOR GPU-DRIVEN PHASES: REJECTED
EXACT BACK-TO-FRONT THROUGH GENERAL MDI: REJECTED
PRODUCTION IMPLEMENTATION: STILL PROHIBITED
```

Per the study discipline, Phase 5 begins only on a later explicit request.

## Phase 5 — Synthesis, Diagnostics, and Implementation Gate

Date completed: 2026-08-28

### Sources re-inspected

Phase 5 re-read the Phase 0–4 findings and the Vanguard code at each proposed
change boundary:

- `docs/formats/vmesh-format.md` and `docs/migration/red-mesh-vmesh.md`;
- `source/meshes`, `source/resources`, and the world streaming executor;
- `rendering_service.hpp/.cpp` and `entities/rendering_runtime.hpp/.cpp`;
- `render_scene.hpp/.cpp` and `render_scene_gpu.hpp/.cpp`;
- GPU Scene types, tables, lifetime, upload, definitions, runtime, visibility,
  views, shaders, and tests;
- render phases, pipeline cache/factory, frame renderer, command system, and
  viewport submission;
- RHI capabilities, fixed-function vertex/index bindings, indirect commands,
  queue fences, memory-budget queries, and the current D3D12/NVRHI backend.

No extra Unreal or Bevy excavation was needed. Their relevant ownership,
allocator, streaming, and diagnostics evidence was already recorded in Phases
1–4; Phase 5 used Vanguard's real modification seams to settle the remaining
choices.

### Final system boundary

This system owns static-mesh metadata residency, geometry-page demand,
fixed-function arena placement, GPU Scene mesh topology and placement,
future-frame LOD feedback, and compatible indexed MDI batching. It does not own
world streaming policy, components, materials, transparent sorting, generic
frame resources, or RHI native residency.

```text
RenderingService (engine lifetime, renderer-global)
  ├─ ResourceRegistry / ResourcePipeline       existing resource authority
  ├─ GpuSceneRuntime                           existing GPU table authority
  ├─ MeshResidencyManager                      new mesh residency authority
  │   ├─ MeshPageSource references
  │   ├─ GeometryAllocator
  │   ├─ GeometryUploader
  │   ├─ PipelineBucketRegistry
  │   └─ MeshBatchRegistry
  └─ FrameRenderer
      ├─ visibility + LOD selection
      ├─ delayed MeshLodFeedbackService
      └─ draw expansion + indirect construction + shell executor

RenderingRuntime (one world)
  └─ demand/binding/proxy handles only; never owns arenas or residency records
```

The reference chain is:

```text
serialized ResourceReference
  -> strong ResourceHandle<MeshResourceObject>
  -> move-only MeshDemandHandle
  -> strong MeshRenderBindingHandle
  -> canonical GpuRenderableHandle
  -> RenderScene instance binding
```

There is no second GPU mesh identity. `MeshResidencyHandle` is a private CPU
manager handle, and arena/shell/bin handles are private placement identities.

### Final `vmesh` decision

`vmesh` remains version 1 without an extension. Its existing metadata already
contains exact LOD/submesh ranges, split vertex layouts, index width, page
ranges, page hashes, destination ranges, material dependencies, and
`RequiredForLowestLod`. Runtime residency adds no asset-visible virtual address
or page-table lookup.

`MeshResourceObject` eagerly owns only validated `META`, dependency handles,
and an owned `MeshPageSource`. The page source is a seekable loose-file or VPAK
logical-range reader and outlives every asynchronous page request it accepts.
`CollectLodPages` remains the source of the exact deduplicated install set.
Decoded page storage is caller-owned and released after upload submission;
there is no long-lived decoded CPU geometry cache in V1.

### GPU Scene layout 5

GPU Scene changes from layout version 4 to 5. Table ordinals are bytecode ABI:

| Ordinal | Table | Elements/page | Lifetime owner |
|---:|---|---:|---|
| 0 | `Instance` | 16,384 | independent |
| 1 | `Motion` | 16,384 | independent |
| 2 | `Renderable` | 32,768 | independent, canonical handle |
| 3 | `RenderableResidency` | 32,768 | same-index `Renderable` |
| 4 | `Lod` | 65,536 | renderable definition |
| 5 | `Primitive` | 32,768 | renderable definition |
| 6 | `PrimitivePlacement` | 32,768 | same-index `Primitive` |
| 7 | `PhaseParticipation` | 65,536 | renderable definition |
| 8 | `PhasePlacement` | 65,536 | same-index `PhaseParticipation` |
| 9 | `GeometryRange` | 16,384 | installed LOD |
| 10 | `VertexStream` | 65,536 | installed geometry |
| 11 | `PositionDecode` | 32,768 | installed geometry |
| 12 | `Material` | 32,768 | independent definition |
| 13 | `MaterialResource` | 65,536 | material definition |
| 14 | `MaterialSet` | 65,536 | independent |
| 15 | `MaterialIndex` | 262,144 | material set |
| 16 | `Light` | 8,192 | independent |
| 17 | `Decal` | 16,384 | independent |

The three placement tables are parallel tables, not allocations. They cannot
be passed to `GpuSceneLifetime::Allocate`, `Retire`, or `Publish`. Creating the
owner allocation ensures equal capacity in its parallel table; owner retirement
controls index reuse. Uploads use a new validated parallel-upload target that
contains the live owner allocation, destination table, owner-relative offset,
and count. This prevents forged placement allocations and same-index drift.

The byte-exact C++/Slang records are:

```text
GpuRenderable (32 bytes, unchanged size)
  u32 firstLod, lodCount, phaseMaskLow, phaseMaskHigh
  u32 generation, flags, reserved0, reserved1

GpuRenderableResidency (32 bytes)
  u32 generation, placementRevision
  u32 residentLodMaskLow, residentLodMaskHigh
  u32 anchorLod, flags, reserved0, reserved1

GpuLod (16 bytes, unchanged)
  u32 firstPrimitive, primitiveCount
  f32 minimumScreenCoverage, maximumNormalizedError

GpuPrimitive (32 bytes)
  u32 material, firstPhaseParticipation, phaseParticipationCount, stableSubmesh
  u32 flags, reserved0, reserved1, reserved2

GpuPrimitivePlacement (16 bytes)
  u32 geometry, geometryGeneration, placementRevision, flags

GpuPhaseParticipation (16 bytes)
  u32 phase, flags
  i32 sortBias
  u32 reserved

GpuPhasePlacement (48 bytes)
  u32 normalShell, normalShellGeneration, normalBin, normalBinGeneration
  u32 mirroredShell, mirroredShellGeneration, mirroredBin, mirroredBinGeneration
  u32 placementRevision, flags, reserved0, reserved1

GpuGeometryRange (64 bytes)
  u32 firstVertexStream, vertexStreamCount
  u32 vertexArenaSet, vertexArenaGeneration
  u32 indexArena, indexArenaGeneration, firstIndex, indexCount
  i32 baseVertex
  u32 vertexCount, indexFormat, positionDecode
  u32 flags, generation, reserved0, reserved1

GpuVertexStream (16 bytes)
  u32 binding, stride, formatLayout, reserved
```

All fields are naturally 32-bit, records are `alignas(16)`, and C++ asserts
check size, alignment, standard layout, trivial copyability, every offset, every
table ordinal, and elements per page. Slang has matching constants and compile-
time size/offset checks where supported; a CPU-produced shader ABI test uploads
known patterns and verifies shader reads. `GpuSceneLayoutVersion == 5` is checked
by every consuming shader.

`GpuRenderableFlags::Resident` is removed. Renderability is true only when the
parallel residency generation matches the immutable renderable generation,
`placementRevision != 0`, the fallback is in range, and the fallback bit is set.
`GpuPrimitive` no longer owns geometry, and `GpuPhaseParticipation` no longer
owns a mutable pipeline-bucket ordinal. Immutable topology can consequently
outlive every geometry placement and pipeline hot reload.

One vertex arena set belongs to one fixed vertex-layout signature and contains
one non-moving physical buffer for each required binding. Every stream in an
allocation receives the same logical `baseVertex`; buffers are bound at offset
zero and indirect `baseVertex` selects the allocation. Index arenas are separate
for `UInt16` and `UInt32`; `firstIndex` selects an allocation while the index
buffer remains bound at zero. This is the fixed-function property that permits
MDI across many meshes in one arena set.

### Immutable definitions and mutable placement

`GpuRenderableDefinition` becomes topology-only. Its CPU input describes LODs,
primitives, material handles, phase participation, and stable pipeline keys; it
does not require geometry handles. The residency record retains the CPU topology
needed to create placements. Installed geometry definitions are referenced only
while their owning LOD is installed.

A material override is accepted only when its reflected material interface is
compatible with the primitive's pipeline key. An override that changes vertex
input, shader program, topology, fixed state, or pass signature requires another
renderable topology; it cannot mutate an existing batch placement.

### Residency states and transactions

Mesh state:

```text
Unrequested
  -> MetadataReady
  -> FallbackRequested
  -> FallbackInstalling
  -> Drawable
  -> Releasing
  -> Retiring
  -> Vacant

Any pre-drawable failure -> Failed (observable, retry policy explicit)
Demand returning while Releasing/Retiring -> new generation/request, never
resurrection of retiring physical ranges
```

Each LOD independently moves through:

```text
Absent -> Requested -> Reading -> Decoded -> Reserved -> Uploading
       -> Publishing -> Resident -> Unpublishing -> Retiring -> Absent
```

Installation is one serialized renderer transaction:

1. validate mesh generation, demand, LOD, page set, and material readiness;
2. coalesce page reads and validate stored/decoded byte budgets and hashes;
3. reserve one synchronized vertex interval, index interval, GPU Scene geometry
   records, and all shell/bin references without publishing any of them;
4. upload geometry bytes on the graphics queue (`CopySync` in V1);
5. after the copy fence, upload `GeometryRange`/`VertexStream`/decode records;
6. upload primitive placements and phase placements with one new nonzero
   `placementRevision`;
7. last, upload `RenderableResidency` with the same revision and resident bit;
8. expose `MeshRenderBindingHandle` only after the fallback transaction has
   reached step 7.

Any failure before step 7 cancels reservations or retires submitted resources;
the old residency record remains authoritative. No partially installed LOD is
visible.

Eviction and final release reverse the commit order:

1. publish a new residency revision with the LOD bit cleared; fallback is never
   chosen as an eviction victim while demand exists;
2. wait for that GPU Scene update to order before later visibility work;
3. detach primitive and phase placements from the active registry generation;
4. release shell/bin references and geometry-definition references;
5. retire arena intervals and all GPU Scene allocations with a
   `rhi::ResidencyFenceSet` containing the last graphics, compute, and copy use;
6. reuse nothing until all included fences complete and owner allocation
   retirement permits the same GPU Scene index to be reused.

Pending upload bytes and retiring bytes count against the budget. LOD eviction
is atomic; pages shared by two installed LODs remain referenced until both are
gone. A failed optional LOD leaves the fallback drawable. A failed fallback
fails readiness and never admits a visible proxy.

### Geometry allocation and bootstrap capacities

Correctness never depends on the numeric defaults; every limit is configured,
checked before allocation, exposed in stats, and covered by capacity tests. No
runtime measurements exist before implementation, so Phase 5 deliberately locks
bootstrap values rather than pretending they are measured production values:

| Capacity | Bootstrap value |
|---|---:|
| ordinary vertex arena set | 262,144 vertices for one fixed layout |
| ordinary `UInt16`/`UInt32` index arena chunk | 64 MiB |
| total committed vertex arena bytes | 512 MiB |
| total committed index arena bytes | 256 MiB |
| geometry upload staging | 3 x 16 MiB regular; bounded one-off overflow up to 256 MiB |
| decoded CPU page bytes | 256 MiB |
| pending GPU install bytes | 256 MiB |
| geometry upload per frame | 64 MiB and 64 LODs |
| optional LOD evictions per frame | 256 LODs |
| feedback readback ring | 4 slots |
| compact feedback entries | 65,536 |
| batch shells | 16,384 |
| geometry bins | 262,144 |
| shell/bin mutations per frame | 4,096 |
| draw packets per bounded wave | 1,048,576 |
| indirect commands per segment | 16,384 |

For a vertex layout, every binding buffer in one ordinary arena set uses the
same 262,144-vertex capacity. The allocator rejects growth beyond the separate
total committed vertex/index byte budgets. An allocation larger than an empty ordinary chunk receives a dedicated
non-moving arena set. Best-fit free ranges coalesce, but live allocations never
move and V1 has no compaction.

The default geometry commitment ceiling is the smaller of 1 GiB and 25% of a
valid OS local-memory budget. When budget queries are unavailable, the explicit
1 GiB configured ceiling is authoritative. Mesh-residency pressure begins at
90% of its effective ceiling and stops below 80%; the existing RHI native-
residency policy remains separately at 95%/85%. These are different layers and
must not double-count an eviction as physical destruction.

Bootstrap values must be tuned from telemetry before production rollout. A
configuration with smaller limits must remain correct by delaying installs,
evicting optional LODs, splitting waves/segments, or reporting a hard fallback
capacity failure; it must never truncate visible drawing silently.

### Demand, consumer, and teardown API

The public residency vocabulary avoids ownership ambiguity:

```text
MeshDemandHandle
  move-only request interest; dropping/cancelling removes future demand

MeshRenderBindingHandle
  strong, copyable drawable binding; retains the renderable definition and
  fallback residency until transferred/retired

MeshResidencySnapshot
  read-only state, desired/resident masks, bytes, revision, and failure
```

The minimal manager operations are `RequestMesh`, `CancelDemand`,
`TryAcquireRenderBinding`, `GetSnapshot`, and renderer-tick processing. They are
asynchronous and never wait for IO, upload, GPU feedback, or fences.

`MeshProxyDesc` and `MeshProxyUpdate` keep the serialized `ResourceReference`
for identity/debugging but replace the generic `meshHandle` with a valid
`MeshRenderBindingHandle`. `RenderSceneGpuMeshBinding` retains that strong
binding plus the material-set handle. A proxy is visible only after the binding
exists; a generic loaded resource is not render readiness.

`RenderingRuntime` receives the renderer-global residency manager from
`RenderingService`. On a distant-proxy `ResourceAvailable` event it requests
fallback residency, waits asynchronously for a binding, and only then queues
the visible proxy. On release it cancels any pending demand or hides/detaches the
live proxy and receives a generational `RenderProxyRetirementHandle`.

`RenderSceneGpuPublisher` transfers the strong mesh binding into its retirement
record when the instance is retired. It releases the binding only after the GPU
instance allocation and its references are fence-complete. `RenderingRuntime`
polls/consumes the retirement handle and calls world `CompleteRelease` only then;
the current two-frame proxy delay is not a geometry-reuse proof. This keeps the
strict Phase 1 teardown order:

```text
hide/detach proxy
  -> retire GPU Scene instance
  -> complete instance graphics/compute/copy fences
  -> release MeshRenderBindingHandle and renderable demand
  -> retire optional/fallback geometry as policy permits
  -> release mesh ResourceHandle
  -> world CompleteRelease
```

The future `StaticMeshComponent` needs only resource references, request/update/
cancel calls, asynchronous readiness/failure, and proxy detach completion. Its
data model and ECS behavior remain outside this design.

### Batch registry and device admission

One stable shell key is:

```text
render phase
pipeline bucket index + generation
vertex arena-set index + generation
index arena index + generation
index format
winding variant (normal or mirrored)
```

`PipelineBucketKey` already contains shader program, reflected vertex layout,
topology, raster, depth/stencil, blend, attachment formats/sample count, and pass
specialization. Any fixed state not represented there must be added there, not
silently appended elsewhere. Material identity, geometry offsets, and instance
identity do not split a shell. A shell owns one PSO, fixed-function vertex-buffer
set, index buffer/format, and stable indirect segments. Each installed primitive
phase owns a generational geometry bin within one shell; one bin produces at
most one indexed indirect command per view/wave/segment.

Pipeline compilation never blocks the frame. A pending bucket omits its bins
and records a pending counter; a failed bucket reports a durable failure. Hot
reload creates a new generation and placement transaction, while the old shell,
pipeline, and bins remain alive through their last fence.

RHI `Capabilities` gains:

```text
indexedIndirect
multiDrawIndexedIndirect
drawIndexedIndirectCount
maximumIndirectDrawCount
indirectArgumentOffsetAlignment
indirectCountOffsetAlignment
maximumIndirectBufferOffset
```

The current D3D12/NVRHI path supports indexed multi-draw and counted indexed
indirect, but narrows public `u64` argument/count offsets to native `u32`.
Vanguard therefore admits only segments whose checked end offsets are at most
`UINT32_MAX`, aligns argument offsets to 4 bytes and count offsets to 4 bytes,
and limits each call to both the configured 16,384 commands and the advertised
native maximum. The backend must reject narrowing overflow instead of casting
it.

Counted indexed MDI is preferred. A backend with indexed multi-draw but no count
buffer uses fixed-count MDI after the GPU zeroes unused commands. A backend
without indexed multi-draw is not admitted to the production GPU-driven mesh
path. A separately selected direct/debug renderer may exist, but it cannot read
back the visible set to emulate support.

Exact `BackToFront` phases are rejected by the general mesh shell executor and
routed to a separately owned transparent renderer. Phase 5 does not prescribe
whether that renderer uses CPU direct draws, GPU sorting, or OIT. No temporal
visual-LOD history or front-to-back depth buckets are included in V1; telemetry
may justify either later without changing residency identity.

### GPU frame sequence

The render graph expresses these ordered passes and resources:

```text
CPU stable candidate ranges + immutable shell snapshot
  -> clear visibility, feedback, bin, argument, and count counters
  -> frustum/mask visibility
  -> validate renderable generation/revision
  -> choose desired LOD from coverage/error
  -> choose finest resident LOD at or coarser than desired, else fallback
  -> aggregate desired-LOD feedback by renderable generation
  -> expand selected LOD primitives and eligible phase placements
  -> choose normal/mirrored placement from instance determinant
  -> validate placement, geometry, shell, bin, and pipeline generations
  -> count packets per geometry bin
  -> prefix-sum bin packet offsets
  -> scatter draw-instance records
  -> build one `IndirectDrawIndexedArguments` per nonempty bin
  -> write per-shell/segment command counts
  -> graphics pass visits CPU-known shell snapshot
       bind PSO + fixed vertex buffers + fixed index buffer
       DrawIndexedPrimitiveIndirectCount(known offsets, known maximum)
```

Candidate and draw-packet capacities are planned with checked 64-bit arithmetic.
If one frame does not fit, it is split into bounded waves before recording; an
individual unsplittable range exceeding a configured hard maximum fails frame
planning. Current-frame visibility, LOD selection, bin counts, arguments, and
counts are never mapped or read by the CPU.

LOD feedback is a different, delayed path. The GPU keeps the finest desired LOD
per `(renderable index, generation)`, compacts at most 65,536 entries, and copies
them into a four-slot readback ring. The manager consumes only an already-
completed older slot. Stale generations are discarded. Feedback overflow never
affects current drawing; it freezes optional eviction for one feedback horizon,
increments overflow telemetry, and requests a larger/configured rescan on a
future frame. Rendering always continues from current resident masks.

### Failure matrix

| Failure | Immediate behavior | Recovery/visibility |
|---|---|---|
| metadata or dependency invalid | fail mesh request | durable resource diagnostic; no proxy |
| page IO/hash/decode failure | fail that install | fallback failure blocks readiness; optional failure keeps fallback |
| CPU decoded/pending budget full | queue or cancel optional install | pressure/delay counters |
| arena/GPU Scene capacity full | evict optional LODs, retry once | fallback failure is hard and named |
| upload staging busy | defer without waiting | busy age and bytes visible |
| RHI copy/submit/device loss | cancel or retire transaction | old resident revision remains; device failure propagated |
| placement generation/revision mismatch | omit invalid packet | hard GPU validation counter and captured identity |
| no resident fallback | omit renderable | hard invariant failure; proxy should never have been admitted |
| feedback overflow | keep rendering, freeze optional eviction horizon | overflow/requested/written counters |
| shell/bin mutation capacity | defer publication | old placement remains; fallback preserved |
| expansion arithmetic/capacity | split wave or fail planning | never truncate silently |
| pipeline pending | omit affected bins this frame | pending age/count |
| pipeline failed | omit and report durable error | requires asset/shader fix or explicit fallback pipeline |
| unsupported indirect capability | reject production path at device admission | optional direct debug mode only |
| retirement queue capacity | backpressure release, retain ownership | never early-reuse or early-acknowledge |
| stale demand/binding/retirement handle | reject operation | generation failure counter |

Fault-injection points exist after every install step, before/after the resident-
bit commit, before/after eviction clear, on every allocation/upload/submit, at
readback-ring wrap, on pipeline pending/failure/hot reload, and on each queue
fence. Deterministic seeds and a transaction identifier make failures
reproducible.

### Diagnostics and tools

`MeshResidencyStats` exposes mesh/LOD state counts; demanded and drawable meshes;
stored/decoded/pending/resident/retiring bytes; arena sets/chunks/free ranges/
fragmentation; IO coalesces/cancellations/failures; install/rollback/eviction
counts and ages; pressure state; resident-mask distributions; and fence backlog.

`MeshDrawStats` exposes desired/selected/fallback LOD histograms; feedback
requested/written/overflow/stale/age; shell/bin counts and key-split reasons;
normal/mirrored counts; active/empty commands; draw instances; waves/segments;
pipeline pending/failure/hot-reload counts; MDI mode; invalid generation/
revision counts; and per-pass GPU timings.

Debug views include mesh residency state and revision, selected versus desired
LOD, fallback use, arena set/index arena, shell/bin identity, missing placement,
feedback age, and pressure/eviction candidates. Dumps can trace one mesh from
resource identity through pages, placements, proxies, draw packets, fences, and
retirement.

RenderDoc expectations are contractual: a captured draw shows ordinary fixed-
function split vertex buffers and an index buffer, a valid graphics pipeline,
the indirect argument/count ranges, and a reconstructable selected mesh. The
design does not use storage-buffer vertex pulling, so RenderDoc mesh inspection
remains useful.

### Test contract

Unit tests cover page-set deduplication; demand cancellation; generational
handles; allocator best-fit/coalescing/dedicated chunks; synchronized vertex
intervals; index formats; budget hysteresis; all LOD mask choices; exact ABI;
parallel table ownership; publication rollback; eviction ordering; fence union;
shell-key field variation; pipeline generations; feedback dedup/staleness/
overflow; checked wave and segment arithmetic; and every failure code.

Integration tests cover loose/VPAK partial reads, fallback-before-proxy admission,
optional installs and evictions without proxy rebinding, world release during
every state, same mesh across worlds, hot reload, device loss, shutdown drain,
and no premature `CompleteRelease`.

GPU tests compare direct reference rendering with counted and fixed-count MDI
for zero/one/many bins, `UInt16`/`UInt32`, all split vertex layouts, normal/
mirrored winding, multiple views/phases/waves/segments, and exact offset limits.
They intentionally omit graph dependencies and corrupt generations/revisions to
prove detection. Instrumentation asserts that the MDI path performs no visible-
set map/readback or CPU sort. RenderDoc validation is a release checklist item.

Long-running stress uses constrained budgets and capacities, random request/
cancel/install/evict/reload/release order, delayed queue fences, and injected IO/
RHI failures. At every quiescent checkpoint all references, allocations,
placement entries, shell/bin references, readback slots, and retirement tokens
must balance to zero.

### Decisions rejected in final synthesis

- virtual geometry addresses or shader page-table lookup for V1;
- one physical vertex buffer set for every layout and every mesh;
- storage-buffer vertex pulling for ordinary static meshes;
- geometry ownership in immutable renderable topology;
- independent lifetimes for parallel placement tables;
- frame-count-based geometry reuse;
- exposing a loaded `ResourceHandle` as drawable readiness;
- pipeline compilation waits on the frame path;
- GPU-visible-set readback, CPU visible sorting, or CPU-generated current-frame
  indirect counts;
- silent truncation, stale-placement fallback, or unreported direct-draw
  emulation;
- exact back-to-front transparency through unordered shell MDI.

### Exit-gate result

All correctness-critical ownership, identity, publication, rollback, visibility,
submission, and fence boundaries are now specified. Remaining numeric tuning is
explicitly a telemetry/rollout activity and cannot alter correctness or ABI.

```text
PHASE 5 SYNTHESIS GATE: PASSED
VMESH FORMAT: VERSION 1 UNCHANGED
GPU SCENE ABI: LAYOUT 5 LOCKED FOR IMPLEMENTATION
OWNERSHIP/PUBLICATION/FENCE BOUNDARIES: SPECIFIED
FAILURES/DIAGNOSTICS/TESTS: SPECIFIED
PRODUCTION CODE CHANGED BY STUDY: NO
USER IMPLEMENTATION APPROVAL: PENDING
PRODUCTION IMPLEMENTATION: STILL PROHIBITED
```
