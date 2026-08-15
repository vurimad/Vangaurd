# GPU Scene implementation

## Objective

The GPU Scene is the persistent renderer-owned image of renderable state. CPU scene changes update stable GPU slots in batches; views move only compact indices through visibility, phase expansion, batching, and indirect execution. The runtime frame path never requires a GPU-to-CPU readback.

## Invariants

- Ordinary renderable state has one stable `GpuInstance` slot. Per-view buffers never copy complete instances.
- Candidate, visible, and final draw-instance lists carry 32-bit `GpuInstance` indices.
- Slot reuse is deferred until every GPU submission that could contain the old index has completed.
- Views and render phases are independent axes. A viewport may create views, but no view is owned by presentation.
- A view family shares one frame and scene publication. Compatible views may share candidate sets.
- Geometry, material, primitive, and phase-participation data is shared by persistent GPU definitions rather than duplicated per instance or view.
- Visibility, LOD selection, phase expansion, batching, indirect command generation, and command counts remain GPU-resident.
- The CPU registers stable pipeline buckets and records execution for them, but never reads per-frame generated counts.
- Public types do not expose NVRHI.

## Phase 1: render phases and views

Status: implemented.

`RenderPhaseKey` is a durable name-and-pass identity used by configuration and future cooked renderer metadata. `RenderPhaseRegistry` resolves keys to compact `RenderPhaseId` ordinals, supports idempotent registration, detects incompatible definitions and hash collisions, and becomes immutable after `Seal`. `RenderPhaseSet` is a 64-bit GPU-friendly membership set.

`RenderView` contains frame identity, purpose, requested phases, camera-relative world origin, matrices, frustum, masks, temporal identity, and view policy. `RenderViewFamily` is a non-owning frame-scoped span of views sharing a scene identity and publication version. Neither type owns a viewport, presentation output, Render Scene, or GPU resource.

Standard registered phase identities currently cover shadow depth, depth prepass, opaque, decal, transparent, velocity, and selection. Registration remains extensible up to the fixed hot-path capacity.

## Phase 2: persistent GPU layouts

Status: implemented.

`gpu_scene_types.hpp` is the CPU image of the GPU Scene ABI and `gpu_scene_types.hlsli` is its shader image. Every structured-buffer element is composed exclusively of explicit 32-bit lanes, aligned to 16 bytes, and guarded by compile-time size and offset checks. Matrices are represented as four vectors rather than relying on compiler-specific matrix-major packing. Layout changes require advancing `GpuSceneLayoutVersion` and the shader-side version together.

Ordinary renderables occupy one stable `GpuInstance` slot. Its first 64 bytes contain culling and classification data; quaternion and scale occupy the final 32 bytes. Previous transforms are sparse `GpuMotion` entries allocated only when motion history is required. Handles are generational on the CPU, while frame-local candidate, visible, and final instance lists contain only a 32-bit slot index. Slot reuse is a fence-deferred lifetime concern and never requires copying an instance into per-view storage.

Shared data is normalized into `GpuRenderable`, `GpuLod`, `GpuPrimitive`, and `GpuPhaseParticipation` tables. A primitive references geometry and material once, then a compact participation range describes its phase and pipeline-bucket membership. `GpuGeometryRange` points into large index arenas and an arbitrary range of `GpuVertexStream` entries, preserving importer-neutral and custom vertex layouts. Position decoding is shared through `GpuPositionDecode` rather than repeated per instance.

`GpuMaterial` is populated by the runtime material resolver. It points to raw parameter bytes and `GpuMaterialResource` entries containing resolved bindless descriptor indices. The cooked material format remains unaware of descriptor heaps, binding slots, or draw submission. An optional `GpuMaterialSet` is a compact primitive-local override span; instances without overrides continue to use primitive defaults without allocating one. Lights and decals have dedicated persistent layouts because they do not share ordinary mesh-instance consumption patterns.

`GpuView` is frame-scoped rather than persistent scene state. It mirrors the validated CPU view with exact matrices, camera-relative origins, frustum planes, view masks, render-phase bits, temporal identity, and jitter. Views therefore select and classify stable scene indices without duplicating persistent instance, primitive, geometry, or material data.

## Phase 3: paged persistent GPU tables

Status: implemented.

`GpuSceneTables` owns typed, device-local structured-buffer pages for every persistent GPU Scene layout. Logical indices are decoded into a page and page-local element using fixed power-of-two element counts. Growing a table allocates another page and never reallocates, relocates, or copies earlier pages, so every previously issued index remains stable.

Each possible page receives stable shader-resource and unordered-access descriptor identities during initialization. The compact table and page directories are immutable GPU buffers containing those identities, page geometry, element stride, logical base index, and power-of-two page shift/mask. GPU memory for a page is still allocated only on demand. Shader addressing uses shifts and masks rather than dynamic integer division. This separates stable addressing from physical commitment and prevents directory rewrites when a table grows.

The table owner retains the renderer's global bindless resource domain rather than creating a private binding model. Directory bindings expose only bindless indices, and public rendering types remain independent of the native backend. Each materialized page supports shader reads, compute scatter writes, and copy-destination uploads.

Only page metadata is retained on the CPU. There is deliberately no second full CPU mirror owned by the GPU Scene; sparse publication carries only changed elements transiently.

## Phase 4: GPU-safe logical lifetime

Status: implemented.

`GpuSceneLifetime` manages identities inside the persistent table pages while the RHI continues to own complete buffers and descriptors. Individual high-volume objects use compact generational slot metadata. Shared definition arrays use contiguous range allocation with best-fit selection, deferred reuse, and free-range coalescing. Both forms expose the same `GpuSceneAllocation` identity and `Allocated -> Active -> Retiring` state machine.

An allocation is not GPU-visible while `Allocated`; this is the state in which the sparse uploader stages its complete initial value. Successful upload submission commits its initial publication. An unpublished allocation may be cancelled immediately, while an active allocation must retire and cannot be overwritten or recycled.

Allocation and initial publication are separate states. The sparse uploader owns the publication boundary and calls `CommitInitialPublication` only after the complete initial payload has entered its upload stream; ordinary scene code must not expose merely allocated identities to GPU work.

Retirement uses a bounded ring of reusable epochs rather than storing and polling fences per allocation. Removals accumulate in one open epoch. After the final graphics, compute, and copy submissions that could reference the previous scene publication have been issued, the frame machinery calls `SealRetirements` with the latest fence from every configured queue. Empty or partial coverage is rejected for a non-empty epoch. Non-blocking `Collect` polls the oldest epoch once and reclaims its complete allocation batch without shifting later work. A reused index always receives a new nonzero CPU generation; stale handles never redirect to its new owner.

Scalar lifetime operations remain available for uncommon mutations, while transactional batch allocation, publication, cancellation, and retirement avoid repeated overhead for streamed cells. `ReserveCapacity` materializes pages and logical metadata ahead of urgent demand so streaming lookahead can absorb resource-creation latency outside the frame-publication path.

`GpuInstance::boundsCenterOffset` is a world-axis offset from the instance origin after the object transform has been applied, and `boundsRadius` is already expanded by the maximum absolute instance scale. GPU culling therefore reads the compact leading lanes without fetching rotation or scale and does not accidentally treat a mesh-local bound as world-space data.

Logical table growth delegates to `GpuSceneTables`, and physical RHI resources remain under the existing reference-counted fence-safe destruction system. The lifetime layer therefore does not duplicate native resource ownership or destruction queues.

## Phase 5: sparse GPU publication

Status: implemented for direct copies; compute scatter remains a later extension for highly fragmented batches.

`GpuSceneUploader` owns a bounded set of persistently mapped upload segments. `Begin` validates and sorts compact update descriptors, applies deterministic last-request-wins elimination to exact duplicate ranges, rejects ambiguous partial overlaps, and assigns the surviving requests their final staging addresses. Producer jobs write the final GPU representation directly into those addresses and call `Complete`; no payload array or intermediate CPU blob is created by the uploader.

Destination-order planning makes adjacent GPU ranges adjacent in staging memory. Copies are split only at persistent table-page boundaries and contiguous source/destination spans are coalesced into one command. Every affected page transitions once into copy-destination state and once back into graphics/compute shader-read state rather than transitioning around each update.

Submission uses a graphics-queue copy command list so later graphics work observes the publication through queue ordering without a CPU wait. Each staging segment is protected by the returned GPU fence and cannot be selected again until that fence completes. If every segment is busy, planning reports bounded backpressure instead of spinning or stalling the calling thread. Initial identities become active only after command submission succeeds; active identities support sparse subrange updates.

The batch retains only descriptors, copy spans, affected page references, and initial-publication identities. Descriptor storage is reserved at initialization and reused at its high-water mark. Actual scene payload bytes travel once: producer to mapped upload memory, then GPU copy to the persistent device-local page.

## Phase 6: shared GPU definitions

Status: implemented.

`GpuSceneDefinitions` owns immutable, content-addressed geometry, material, and renderable definitions. A 256-bit `GpuSceneDefinitionKey` is supplied by the resource resolver; equal keys reuse the existing generational GPU handle and must represent identical resolved content. Reloaded content receives a different key, allowing instances to switch definitions before the old version retires.

Geometry publication owns one `GpuGeometryRange`, its complete `GpuVertexStream` span, and optional `GpuPositionDecode`. Material publication owns one `GpuMaterial` and its resolved bindless-resource span. Renderable publication owns `GpuRenderable`, LOD, primitive, and phase-participation ranges. Local authoring offsets are validated and rebased to persistent table indices during direct upload-heap emission. Definitions and their child arrays may cross table-page boundaries without changing their logical ranges.

Registration is batch-oriented. Existing keys increment references without allocating or uploading; new compound definitions are allocated transactionally and submitted through one sparse-publication batch. A failed allocation or upload cancels every unpublished range and rolls back references acquired by the transaction. Reused-only batches perform no GPU submission.

Renderable primitives retain generational geometry and material handles on the CPU while the GPU image stores their stable 32-bit indices. Acquiring another reference to the same renderable does not duplicate dependency edges. When its final reference is released, the definition owner decrements every primitive edge and atomically places the renderable plus any newly unreferenced geometry and material compounds into the same retirement transaction. Their indices remain unavailable for reuse until the configured graphics, compute, and copy fences complete.

No definition payload mirror is retained. CPU records contain keys, handles, reference counts, allocation identities, and renderable dependency edges only. The resolved GPU structures are written directly into mapped upload memory and remain exclusively in persistent GPU tables.

## Remaining phases

1. Compute-scatter selection for highly fragmented sparse updates.
2. Candidate ingestion and GPU visibility.
3. GPU phase expansion, sorting, compaction, and batching.
4. Indirect command and count generation.
5. Conformance, stress, overflow, and lifetime hardening.

After the GPU side is complete, Render Scene publication will be adapted to emit persistent changes and compact candidate indices directly. Existing spatial indexing and lifecycle rules remain inputs; full published-scene and collector-payload copies are removed from the runtime render path.
