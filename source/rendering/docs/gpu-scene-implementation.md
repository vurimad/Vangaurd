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

### Provisional material-interface direction

This section records a design discussion, not an accepted or implemented contract. `GpuMaterial::materialInterface` currently reserves a stable identity connecting a GPU material instance to some future shader-derived material ABI. The engine does not yet declare, reflect, cook, register, validate, or generate accessors for such an ABI, and the field must not be treated as evidence that those systems already exist.

Shader reflection cannot reliably infer which declarations in an arbitrary bindless shader constitute material state. A shader may expose global GPU Scene tables, view data, pass resources, render targets, helper structures, and several entry points alongside actual material parameters. If Vanguard pursues this model, material participation must therefore be explicit rather than guessed. A shader module or entry point would declare or reference a designated material interface, while shaders such as culling, depth-pyramid construction, bloom, or tone mapping may declare none.

A reflected interface could describe parameter byte offsets and logical resource roles for one material family. A material instance would continue to store only its parameter range, resolved bindless-resource range, and interface identity. The preferred hot shading path would use reflection-generated constants or accessors compiled into the compatible shader variant:

StandardSurfaceMaterial loadMaterial(uint materialIndex)
{
    GpuMaterial material = materials[materialIndex];

    StandardSurfaceMaterial result;
    result.baseColor = loadParameterFloat4(material.parameterByteOffset + 0);
    result.roughness = loadParameterFloat(material.parameterByteOffset + 16);
    result.metallic = loadParameterFloat(material.parameterByteOffset + 20);

    result.baseColorTexture =
        materialResources[material.firstResource + 0];

    result.normalTexture =
        materialResources[material.firstResource + 1];

    result.sampler =
        materialResources[material.firstResource + 2];

    return result;
}


A shader that is not material-driven simply declares no material interface:
Depth-pyramid compute shader → no material interface
Bloom shader                 → no material interface
Tonemapper                    → no material interface
GPU culling shader            → no material interface

materialInterface = InvalidMaterialInterface;

```text
GpuPrimitive.material
    -> GpuMaterial.parameterByteOffset / firstResource
    -> shader-generated parameter offset or resource slot
    -> material parameter storage or bindless descriptor index
    -> global resource descriptor domain
```

For example, a reflected `normalTexture` role might become resource slot 1. The shader would read `materialResources[material.firstResource + 1]` and use the resulting descriptor index with the global bindless resource domain. `GpuMaterial` itself remains layout-agnostic and contains no hardcoded normal-map, base-color, or roughness fields.

The primary draw path should not necessarily perform a dynamic `MaterialInterfaces[material.materialInterface]` lookup for every parameter or texture. Pipeline and GPU batching work can already group compatible primitives by pipeline/material-interface bucket, allowing the compiled shader to know its generated layout. A runtime interface table may still be useful for genuinely generic evaluation, validation, editor inspection, debugging, or future ray-tracing paths, but its cost and purpose must be demonstrated before adoption.

If implemented, the missing work includes:

- explicit material-interface declarations or metadata in the Slang authoring model;
- entry-point-specific reflection that filters unrelated shader declarations;
- stable interface identity, layout hashing, and compatibility rules;
- generated shader-side material accessors;
- cooked `vshader` interface metadata and `vmat` validation;
- runtime interface registration and pipeline compatibility checks;
- resolution of material resource roles into real bindless descriptor indices.

This direction may be revised or discarded if a simpler pipeline-specialized ABI, a fixed renderer material model, or another measured design provides better scalability. Until that decision is made, Render Scene and GPU Scene code should treat material and interface indices as externally resolved stable identities and must not invent material-layout interpretation locally.

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

## Phase 7: candidate ingestion and GPU visibility contracts

Status: implemented as contracts, planning, and shaders. Production execution intentionally waits for the Render Graph.

`GpuVisibilityPlanBuilder` divides each view's broad-phase candidates into fixed 128-candidate work ranges. It operates entirely over caller-owned storage and returns direct candidate reservations, allowing future Render Scene producer jobs to write compact `GpuInstanceIndex` values into mapped graph upload memory without an intermediate payload array. Views complete in reservation order so the resulting workload is deterministic even when the disjoint candidate spans were filled concurrently.

Every view owns a disjoint visible-index partition and one counter entry. Visibility requests that exceed the partition never write out of bounds: `visibleCount` preserves the requested count, consumers clamp it to the declared capacity, and `overflowCount` records the exact loss. Work, candidate, result, view, visible-index, and counter structures contain explicit 32-bit lanes and have CPU/shader size contracts.

`gpu_scene_visibility.slang` validates instance activation, visibility masks, renderable residency, requested render-phase intersection, and view-relative bounding spheres. Each workgroup performs group-local prefix compaction and reserves its output with one global atomic rather than one atomic per visible instance. Persistent instance and renderable pages are reached through the global bindless table/page directories; frame-local buffers are also named by descriptor indices supplied through a compact push-constant structure.

The shader compiles to DXIL and SPIR-V through the Vanguard Slang toolchain. No rendering service currently binds these resources, records commands, dispatches the kernel, or submits it. The future rendering/GPU-visibility service owns counter clearing, descriptor materialization, constants, and dispatch policy. It contributes opaque passes and their resource accesses to the Render Graph; the graph owns placement, transitions, queue synchronization, and execution ordering without interpreting visibility semantics.

`GpuInstance::deformation` is an optional index reserved for a future renderer deformation table. Visibility continues to use conservative instance bounds, so static, skinned, morphing, and cloth-driven objects share the same candidate path without making immutable source geometry an architectural assumption.

## Phase 8: RenderScene sparse publication boundary

Status: implemented for stable identities, mutation coalescing, direct staging writes, deferred retirement, and direct candidate-index emission.

`RenderSceneGpuPublisher` attaches before scenes are created and assigns every mesh, light, and decal proxy one allocation from `GpuSceneLifetime`. A proxy therefore keeps the same GPU table index throughout its CPU lifetime. Destroyed allocations are not reused by CPU creation: unpublished allocations are cancelled, published allocations enter the existing fence-deferred retirement epochs, and a reused slot receives a new generation.

RenderScene mutations mark compact dirty metadata against that identity. Tracking is stored in lazily materialized 4096-slot pages, so growing the CPU identity directory never relocates existing entries. Repeated transform, visibility, binding, and property changes coalesce into one 32-bit proxy index per mutation epoch. Creation followed by destruction before publication collapses to retirement without uploading a dead object. The publisher does not retain a complete CPU image of `GpuInstance`, `GpuLight`, or `GpuDecal`.

Transform relinking uses an explicit `Serial -> Parallel -> Sealed -> Publishing` phase contract. Before dispatch, the scene owner assigns every Jobs group a disjoint slice of a persistent dirty-index arena. Workers update their proxy state and append directly to their own slice without taking the publisher lock, allocating memory, copying request objects, or concatenating worker results. The parallel-job epilogue seals all slices once and folds their counters into scene-local statistics. Structural creation, destruction, visibility, and resource mutations remain on the serial owner path.

After the exact RenderScene mutation epoch completes, `Prepare` freezes the existing index slices and retirement storage in place; it does not reconstruct a contiguous array of change records. While that publication is open, GPU-relevant proxy mutations are rejected explicitly. `BuildUploadRequests` walks the frozen indices to describe one final-table element per changed identity.

GPU conversion is range-oriented rather than one monolithic serial loop. The serial dirty-index stream is subdivided into bounded ranges, while every preassigned parallel dirty slice remains its own range. Retained prefix metadata gives every range a stable reservation offset without copying dirty indices into another queue. The future Render Graph can schedule non-empty ranges as Jobs without a shared output cursor, lock, concatenation pass, or temporary change array. `FinishWrites` runs only after their dependency joins and verifies that every non-empty range completed before publication may close.

Each range obtains non-owning pointers to the narrow authoritative fields in sealed RenderScene storage. No complete proxy or typed payload object is copied. Final `GpuInstance`, `GpuLight`, and `GpuDecal` objects are constructed directly at their mapped upload reservation, eliminating both the broad source copy and the previous stack-object-to-upload-memory copy. The publisher records no command list and performs no submission. Cancelling a publication preserves the sealed index storage for retry, while completing it clears dirty metadata and advances the mutation epoch.

Mesh definitions remain externally resolved. `BindMesh` connects a proxy identity to stable `GpuRenderable` and optional `GpuMaterialSet` handles, while `BindDecalMaterial` supplies the resolved material identity for decals. Missing bindings produce inactive GPU objects rather than exposing incomplete data to visibility work.

The instance ABI now carries both `visibilityMask` and the complete 64-bit `layerMask`; GPU visibility intersects both against the view. This advanced the CPU/shader GPU Scene layout contract to version 4.

The future rendering and GPU-visibility services own the policy and sequence around this boundary: begin the uploader batch, dispatch the disjoint range writers, join them, call `FinishWrites`, complete reservations, and close the publication. They declare opaque copy and visibility passes to the Render Graph, which owns transient resource placement, access transitions, queue synchronization, and execution ordering without understanding candidate or visibility semantics. No renderer service currently performs that sequence.

RenderScene visibility planning now walks a dense active-cell directory rather than the spatial cell-slot high-water mark. Candidate batches are balanced by raw proxy count and may split a highly populated cell. Every batch receives a permanent disjoint prefix in visibility-system-owned staging memory and writes stable `GpuInstanceIndex` values directly from exact live proxy generations. Filled prefixes remain sparse; `GpuVisibilityPlanBuilder::CompleteViewRanges` emits work only for those prefixes, so no shared append counter, temporary proxy array, gather copy, or count-and-refilter pass is required. One explicit scene read seal protects the completed mutation epoch across the caller's Jobs fan-out and join.

## Remaining phases

1. Compute-scatter selection for highly fragmented sparse updates.
2. GPU LOD selection, phase expansion, sorting, compaction, and batching.
3. Indirect command and count generation.
4. Render Graph integration and execution.
5. Conformance, stress, overflow, and lifetime hardening.

RenderScene now emits persistent changes through the sparse publication boundary and compact candidate indices directly into frame-scoped GPU visibility reservations. Full published-scene and collector-payload copies remain absent from the runtime path. Conservative visibility feedback is separated into its own scene-relative, non-renderable service with explicit view inputs. The next RenderScene increment is the revised frame-service shell; broader renderer work then moves to residency, camera storage, and Render Graph execution.
