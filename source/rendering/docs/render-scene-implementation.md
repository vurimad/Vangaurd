# Render Scene implementation study

## Purpose

This is the working implementation record for Vanguard's Render Scene. It preserves conclusions from the RED rendering
study before code is adapted, and separates observed behavior from Vanguard decisions. The document is updated after
each source chunk is reviewed; an unchecked chunk must not be treated as understood.

The reference source root used by this study is:

```text
D:/root/R6.Root/Mainline/dev/src/common
```

No Vanguard public contract may depend on that tree, its file formats, global renderer objects, or third-party backend
types. Source provenance belongs in this document or the module's `UPSTREAM.md`, never in runtime source comments.

## Scope boundary

Render Scene owns renderer-facing object identity, scene membership, transforms and bounds, spatial membership,
visibility classification, mutation handoff, and a stable scene state for later view collection.

Render Scene does not own:

- Flecs entities or component storage;
- world streaming or resource cooking;
- cameras and camera custom data;
- viewports, windows, swap chains, or presentation;
- Render Graph construction or GPU submission;
- material binding policy;
- NVRHI or native graphics API objects.

Those systems may consume or produce Render Scene contracts through explicit adapters.

## Review discipline

Every chunk records four things:

1. observed ownership and execution behavior;
2. invariants required for correctness;
3. mechanisms worth preserving;
4. coupling, legacy policy, or hazards that Vanguard must change.

Large files are reviewed by semantic ranges rather than read front-to-back. Feature-specific proxy implementations are
sampled only after the base proxy and scene contracts are understood.

## Authoritative source inventory

Paths below are relative to the reference source root.

### Tier 1: mandatory core

| Review unit | Files | Why it is authoritative |
|---|---|---|
| Object lifetime | `renderData/include/renderObject.h`, `renderData/src/renderObject.cpp`, `renderData/include/renderObjectPtr.h/.inl` | Intrusive ownership and command/job-safe lifetime used by scenes and proxies. |
| Public scene | `renderData/include/renderScene.h`, `renderData/src/renderScene.cpp`, `renderData/include/renderPublicTypes.h`, relevant scene-layer declarations in `renderData/include/renderPublicEnums.h` | Small public scene façade, scene kinds, null scene, query-only objects, relink request shapes, and layers. |
| Public proxy handle | `renderData/include/renderProxy.h`, `renderData/src/renderProxy.cpp`, `renderData/include/renderProxySystemInterface.h` | Producer-facing attach/detach/relink behavior and the boundary between world code and renderer-owned proxies. |
| Proxy creation data | `renderData/include/renderProxyInitData.h`, `renderData/src/renderProxyInitData.cpp`, `renderData/include/renderWorldTransform.h` | Initial transform, bounds, type data, scene classification, and ownership transferred during proxy creation. |
| Concrete proxy base | `renderer/src/renderProxy.h`, `renderer/src/renderProxy.cpp` | Renderer-side proxy state, scene attachment, spatial keys, frame tracking, and base relink behavior. |
| Concrete scene | `renderer/src/renderScene.h`, `renderer/src/renderScene.cpp` | Scene storage, proxy insertion/removal, layers, spatial membership, relink buffering, job dispatch, and frame publication. |
| Render command path | `renderData/include/renderCommandHandler.h`, `renderer/src/renderCommandInterface.h/.cpp`, `renderer/src/renderCommandHandler.h/.cpp` | Cross-thread mutation entry points, command ownership, frame tick ordering, add/remove execution, and synchronization. |
| World bridge | `world/include/runtimeSystemRendering.h`, targeted ranges of `world/src/runtimeSystemRendering.cpp` | Scene creation/ownership, proxy registration, world lifecycle, delayed destruction, and producer integration. |

### Tier 2: mandatory integration boundaries

| Review unit | Files | Question answered |
|---|---|---|
| Spatial scene | `renderer/src/visibilityCommon.h`, `renderer/src/visibilityScene.h/.cpp` | What acceleration structure is owned, how it is published, and how writers/readers synchronize. |
| Query-only spatial data | `renderer/src/renderVisibilityQueryOnlyBox.h/.cpp` | How non-rendered spatial objects participate in visibility and how their results cross frames. |
| Collection boundary | `renderer/src/renderCollector.h/.cpp`, `renderer/src/visibilityQueryBase.h/.cpp` | What scene data a view collector needs without importing Render Graph policy into Render Scene. |
| Frame handoff | targeted ranges of `renderer/src/renderRenderFrame.cpp` | Exact ordering of scene preparation, update jobs, collection, and rendering. |

### Tier 3: representative proxy samples

These are not foundational contracts. They will be inspected to verify that the core design supports real workloads:

- `renderer/src/renderProxyDrawable.h/.cpp`
- `renderer/src/renderProxyMesh.h/.cpp`
- `renderer/src/renderProxyLight.h/.cpp`
- `renderer/src/renderProxyDecal.h/.cpp`
- `renderer/src/renderProxyCameraData.h/.cpp`

Mesh checks geometry/resource ownership and instancing. Light and decal check non-mesh spatial proxies. Camera-data checks
whether RED placed policy in a proxy that Vanguard should instead move to the later camera/custom-data machinery.

### Explicitly deferred studies

The following are downstream of the Render Scene foundation and do not participate in its first implementation:

- specialized visibility queries, raster sectors, occlusion buffers, and ray-traced visibility;
- particles, fog, GI, foliage, reflection probes, SpeedTree, cloth, morph targets, and vehicle updates;
- render-node, Render Graph, batching, lighting, post-processing, and GPU submission code;
- camera storage and scene/camera custom-data implementations.

They may be revisited after the core scene, proxy, and collection boundary are stable.

## Chunk ledger

| Chunk | Content | Status |
|---|---|---|
| 01 | Public scene and base render-object lifetime | Reviewed |
| 02 | Public `RenderProxyHandle` lifecycle and relink path | Reviewed |
| 03 | Proxy initialization data and compact world transform | Reviewed |
| 04 | Renderer-side proxy base and spatial identity | Reviewed |
| 05 | Concrete scene declarations and owned storage | Reviewed |
| 06 | Scene construction plus proxy insertion/removal | Reviewed |
| 07 | Relink ingestion, double buffering, coalescing, and redJobs execution | Reviewed |
| 08 | Command queue ownership and frame-tick ordering | Reviewed |
| 09 | World runtime bridge, registration, and delayed destruction | Reviewed |
| 10 | Visibility scene and acceleration-structure publication | Reviewed |
| 11 | Query-only objects and collection boundary | Reviewed |
| 12 | Render-frame integration ranges | Reviewed |
| 13 | Representative mesh, light, decal, and camera-data proxies | Reviewed |
| 14 | Cross-check against Vanguard Jobs, memory, Flecs materialization, resources, and frame pipeline | Reviewed |

## Findings

### Chunk 01: public scene and base render-object lifetime

Reviewed files:

- `renderData/include/renderObject.h`
- `renderData/src/renderObject.cpp`
- `renderData/include/renderObjectPtr.h`
- `renderData/include/renderObjectPtr.inl`
- `renderData/include/renderScene.h`
- `renderData/src/renderScene.cpp`
- `renderData/include/renderProxySystemInterface.h`

#### Observed architecture

`IRenderObject` is an intrusive, atomically reference-counted base allocated from rendering memory pools. Reference
operations are deliberately hidden behind ownership helpers. A render object is deleted immediately when its last
reference is released; safety comes from commands and jobs retaining references, not from an implicit frame-deferred
delete inside `IRenderObject`. Development builds add race detection to each smart-pointer storage object.

`IRenderScene` inherits that lifetime model, so queued commands and render jobs can retain a scene independently from the
world-side owner.

`IRenderScene` is a narrow façade relative to the concrete renderer scene. It exposes four scene identities: world,
preview, thumbnail, and a null scene. A process-wide null object implements safe no-op behavior and avoids repeated null
branches at selected call sites.

The public scene contract exposes:

- rendering readiness and statistics;
- query-only box creation, update, visibility, and optional statistics;
- a lightweight `PrepareForUpdate(tickCounter)` frame boundary;
- relink scheduling;
- forced-LOD and dissolve-time policy.

Relink parameters support four meaningful payload shapes:

1. transform, bounds, skinning data, float-track data, and teleport state;
2. transform, bounds, and teleport state;
3. an array of compact render transforms plus aggregate bounds;
4. an array of matrices plus aggregate bounds.

The public `IRenderProxySystemInterface` is only a destruction callback from a `RenderProxyHandle` to its owning runtime
system. This confirms that producer registration belongs outside `IRenderScene`.

#### Invariants to preserve

- A queued operation must retain every scene/proxy object it can outlive.
- Scene kind is immutable for a scene's lifetime.
- Relink inputs must remain valid until the scene has copied or retained them.
- `PrepareForUpdate` is a synchronization boundary immediately before scene update jobs, not an arbitrary simulation tick.
- Tool scenes are first-class scenes; editor preview and thumbnail rendering must not require a runtime world.
- A no-scene state must be cheap and explicit.

#### Vanguard decisions

- Preserve independent `World`, `Preview`, `Thumbnail`, and `Tool` scene kinds.
- Preserve a small scene façade and a distinct concrete implementation.
- Preserve pooled lifetime and safe retention across queued work, but use Vanguard's existing lifetime/deferred-release
  facilities and generational public handles rather than exposing intrusive raw-pointer identity.
- Preserve all four relink capabilities, but express them as explicit tagged payloads or separate request constructors so
  inactive fields cannot be accidentally observed.
- Preserve a lightweight per-frame preparation boundary; its exact `PrepareForUpdate`/build/publish split remains open
  until command and frame-handoff chunks are reviewed.
- Provide an explicit invalid/null scene façade if it removes client branching, but never report successful mutation for
  discarded work. RED's null scene returns success for relink; Vanguard should return an explicit ignored/invalid result.
- Keep query-only spatial objects in scope, but implement them after the main proxy/spatial machinery.
- Do not place forced LOD or dissolve timing in the foundational scene interface yet. They are rendering policy until later
  evidence shows that every scene implementation requires them.
- Do not copy ownership macros, global allocation syntax, or the global renderer route.

#### Questions carried forward

- Does `ScheduleRelink` copy instance arrays synchronously or retain caller memory until the update job?
- Is the null scene used as a true null object or as a loading/shutdown compatibility device?
- Are scene-layer masks part of spatial storage, collector filtering, or both?

### Chunk 02: public `RenderProxyHandle` lifecycle and relink path

Reviewed ranges:

- `renderData/include/renderProxy.h`: `IRenderProxy` and all of `RenderProxyHandle`
- `renderData/src/renderProxy.cpp`: construction, initialization, unregister, attach, detach, relink, and visibility paths

#### Observed architecture

The public `IRenderProxy` is the renderer-owned polymorphic object. `RenderProxyHandle` is the producer-side wrapper and
strongly retains both the proxy and its target scene. Construction receives a proxy but does not attach it. Initialization
later assigns the scene, initial visibility, and an optional owning-system callback; `Attach()` is a separate operation.

Destruction ordering is deliberate:

1. notify the owning runtime system so a pending attachment is removed or resolved;
2. queue scene detachment if logically attached;
3. release the scene reference;
4. release the proxy reference.

This prevents a pending world-side attach from racing with handle destruction. Queued add/remove commands retain their
own scene and proxy references, so releasing the handle does not invalidate already submitted work.

The mutation paths are split by expected frequency:

- add/remove and ordinary proxy properties go through the global render-command handler;
- transform, bounds, skinning, float-track, teleport, and instance relinks call `IRenderScene::ScheduleRelink` directly;
- handle-local state such as selection, scanning, visibility filtering, and deferred local-shadow settings is cached and
  replayed when attachment becomes possible.

Visibility has two producer inputs: external visibility and filtering visibility. Their conjunction controls effective
visibility. Most invisible proxies are removed from the scene, but proxies that still contribute while hidden may remain
attached and carry an `invisible but still attached` state.

RED contains two different notions of attachment in the same handle:

- possession of a scene pointer;
- `m_isAttached`, meaning that `Attach()` has run;
- actual membership in scene storage, which also depends on visibility and queued-command completion.

The public `IsAttached()` checks only whether a scene pointer exists, not `m_isAttached`. This ambiguity must not be copied.

The handle has accumulated many feature-specific operations: selection, scanning, particles, cloth, material overrides,
lights, morph targets, dismemberment, rain, and mesh overrides. That breadth is evidence that the generic producer handle
became a routing façade for the whole renderer rather than a coherent base contract.

#### Invariants to preserve

- Pending-attachment ownership must be cancelled before destruction queues a detach.
- A handle must retain its scene and proxy while it can submit mutations.
- Queued mutations must independently retain all objects and payloads they use.
- Relinking must be legal from producer threads without entering the large general render-command path.
- Bounds must be valid before a relink is accepted.
- Cached state that affects initial renderer state must be included atomically with creation or replayed in deterministic
  order before the proxy becomes observable.
- Hidden and absent are different states for objects that continue contributing to shadows or other render features.

#### Vanguard decisions

- Preserve a small producer-side `RenderProxyHandle`, separate from the renderer-owned proxy representation.
- Give the handle explicit states such as `Unbound`, `PendingCreate`, `Resident`, `HiddenResident`, `PendingDestroy`, and
  `Invalid`; do not infer attachment from a scene pointer.
- Keep relink as a dedicated high-throughput scene ingress path rather than routing it through a generic heap-allocated
  command stream.
- Place initial visibility, layers, transform, bounds, selection/tool flags, and other state required at first observation
  in one creation descriptor. This avoids a visible partially initialized proxy.
- Preserve a distinction between render visibility and scene residency. Do not require removal/reinsertion merely to hide
  an object when a flag update is cheaper; spatial-query exclusion and shadow-only contribution will be explicit policy.
- Keep the base handle free of feature-specific methods. Mesh, light, decal, skinning, editor-selection, and effect updates
  use typed mutation payloads or typed producer façades layered over the same proxy identity.
- Replace the global command route with an injected/owned scene mutation endpoint.
- Return explicit mutation results in production and assert contract violations in checked builds. Do not silently repair
  invalid lifecycle order.

#### Questions carried forward

- Does the concrete scene copy all instance relink data before `ScheduleRelink` returns?
- Does queued add followed immediately by remove preserve FIFO ordering for one proxy across producer threads?
- Which proxy states require residence while invisible, and can that be represented as collection flags instead of a
  virtual `ShouldDetachWhenInvisible()` policy?
- How does the runtime system distinguish pending creation from a proxy already inserted into scene storage?

### Chunk 03: proxy initialization data and compact world transform

Reviewed files and ranges:

- `renderData/include/renderWorldTransform.h`
- `renderData/include/renderProxyInitData.h`
- `renderData/src/renderProxyInitData.cpp`
- `renderer/src/renderHelpers.cpp`: proxy factory
- representative creation call sites in `world` and `worldEntities`
- `world/src/runtimeSystemRendering.cpp`: create-and-register range
- existing Vanguard `math` world-position and world-transform contracts

#### Observed architecture

RED separates the creation envelope from its concrete payload. `RenderProxyInitInfo` contains a non-owning pointer to a
polymorphic `RenderProxyComponentInitData`. The common component prefix contains the minimum scene-placement state:

- compact world transform;
- world-space bounding box;
- proxy type;
- scene-layer mask;
- a development constructor-validation byte.

Derived descriptors contain the complete initial state for concrete proxy families. Drawable state adds rendering plane,
visibility, dynamic/static classification, auto-hide ranges, and motion-blur policy. Mesh, light, decal, particle, GI,
reflection-probe, fog, and other descriptors append their type-specific resources and policy.

The proxy factory switches on the explicit proxy type and constructs the matching renderer-owned proxy synchronously.
Most constructors copy scalar data and retain render resources immediately. A limited `InitAsync` hook can append proxy
initialization work to a supplied job builder, but the creation descriptor itself remains borrowed.

Call sites commonly allocate both the concrete descriptor and the envelope on the stack:

```text
construct concrete descriptor on stack
fill transform, bounds, resources, and policy
point RenderProxyInitInfo at it
CreateAndRegisterRenderProxy(...)
return and destroy both stack objects
```

Therefore the descriptor pointer, raw strings, spans, and raw callback pointers cannot be retained beyond the synchronous
creation call unless the concrete proxy explicitly copies data or retains a separate owner. RED does this inconsistently:
resource smart pointers and owning arrays are safe, while raw flicker pointers, friendly-name strings, spans, and callbacks
depend on constructor behavior or external lifetime. The constructor-validation sentinel catches descriptors erased with
`memset`, but is not structural validation.

`RenderProxyTransform` stores:

- fixed-point large-world position inside `WorldTransform`;
- quaternion orientation;
- non-uniform floating-point scale as a separate vector.

It converts to a matrix only when needed. Matrix decomposition is explicitly marked slow. The fixed-point fractional shift
is statically tied to shader unpacking, showing that world-coordinate representation is a CPU/GPU data contract rather
than an incidental math choice.

Vanguard already contains the compatible `WorldPosition` and `WorldTransform` foundation with the same 17-bit fractional
fixed-point representation. A renderer-specific transform wrapper is still useful because scale is intentionally separate
from the rigid world transform.

Creation and scene registration are separate in RED:

1. create the renderer-owned proxy from borrowed initialization data;
2. create the producer handle;
3. assign its target scene and initial visibility;
4. register it with the runtime rendering system;
5. attach now or place it in pending attachment, depending on runtime state.

This ordering prevents a partially constructed proxy from entering scene storage, but initial handle-only state can still
be replayed later through separate commands.

#### Invariants to preserve

- Proxy type, payload type, and factory entry must agree before construction begins.
- Initial transform and world bounds must be valid and mutually consistent enough for spatial insertion.
- No scene may observe a proxy before construction and required initialization jobs reach the declared readiness point.
- Creation descriptors are borrowed for the duration of creation only.
- Every resource needed by the proxy after creation must be retained in renderer ownership before creation returns.
- Every variable-sized payload needed asynchronously must be deep-copied, moved into owned storage, or retain a documented
  owner before creation returns.
- Large-world position must remain precise until a camera-relative/GPU conversion boundary is explicitly chosen.
- Matrix decomposition must not enter ordinary transform or instancing paths.
- Scene layers are initial scene-placement data, not a post-creation cosmetic property.

#### Vanguard decisions

- Add a renderer-specific `RenderProxyTransform` using Vanguard `math::WorldTransform` plus non-uniform scale. Keep its
  representation independent from serialized world schemas and from GPU matrix formats.
- Preserve fixed-point large-world position through Render Scene storage. Camera-relative conversion belongs to later view
  collection/GPU Scene construction.
- Do not use a polymorphic raw-pointer descriptor envelope. Use an explicit `RenderProxyCreateInfo` common header plus a
  typed creation entry point or validated tagged payload.
- Make the common creation header contain at least: proxy kind, transform, world bounds, scene layers, collection flags,
  initial visibility/residency policy, stable producer correlation ID, and debug name ID in non-shipping configurations.
- Each concrete proxy family owns a separate typed descriptor. Adding a family must register both validation and creation;
  exhaustive compile-time or startup checks replace RED's manual drawable/non-drawable switch assertions.
- Creation inputs are borrowed only during the call. Public descriptor fields may use spans and string views, but the
  factory must copy/move them into pool-backed proxy storage before returning or reject asynchronous use.
- Resource fields use Vanguard shared loaded-resource handles and generation-safe identities. The Render Scene does not
  retain VPAK objects, source paths, or cooked-format implementation details.
- Initial renderer-visible state is submitted as one creation transaction. A proxy becomes `Resident` only after required
  initialization and scene insertion complete; no observer sees an incomplete proxy.
- Optional heavyweight initialization may produce a readiness counter/job dependency, but the borrowed descriptor never
  crosses into the asynchronous job.
- Use real validation results and checked assertions rather than constructor magic bytes. Memory must never be cleared as
  a replacement for constructors.
- Keep feature policy out of the common header. Auto-hide, LOD, shadow, cloth, material, and effect fields belong to typed
  proxy payloads or later systems.

#### Questions carried forward

- Does the renderer-side base proxy copy transform and bounds once, while concrete proxies retain all resource handles?
- Is proxy construction allowed on arbitrary producer threads, or serialized by the world runtime system?
- When `InitAsync` is used, what prevents registration/attachment before its jobs finish?
- Are static/dynamic classification and scene layers immutable after insertion, or can commands migrate them?
- How are instance transforms initially owned, since the common creation descriptor contains only one transform?

### Chunk 04: renderer-side proxy base and spatial identity

Reviewed files and ranges:

- `renderer/src/renderProxy.h/.cpp`: complete base proxy state, construction, attachment, detachment, relinking, custom data,
  and once-per-frame update
- `renderer/src/renderProxyInstanceMatrix.h`
- `renderer/src/renderFrameTracker.h/.cpp`
- `renderer/src/renderProxyMotionData.h/.cpp`
- `renderer/src/renderProxyDrawable.h`: collectable/drawable boundary
- `renderer/src/renderProxyDrawable.cpp`: collectable relink fast path
- acceleration user-data and leaf-key declarations in `visibility/include/visVolumeHierarchyGrid.h` and
  `visibility/include/visBoundingVolumeHierarchy.h`
- scene proxy-index allocation/free and camera-data clearing ranges in `renderer/src/renderScene.cpp`
- proxy-indexed camera-data ranges in `renderer/src/renderProxyCameraData.h/.cpp`

#### Observed architecture

RED's renderer-owned `IRenderProxyBase` combines several roles:

- polymorphic render-proxy behavior;
- a single packed transform/instance record;
- acceleration-structure user data;
- scene membership and dense scene index;
- world bounds and scene layers;
- per-camera collection/update entry points;
- append-only RTTI custom data;
- editor and distance-culling policy.

The base instance representation is exactly 48 bytes: a packed 3x4 transform. The 3x3 rotation/scale portion is stored as
floats, while translation occupies the fourth value of each row as fixed-point world-coordinate bits. It can return:

- an unpacked floating matrix;
- a matrix retaining packed translation bits;
- an unpacked 3x4 matrix with an optional origin offset.

This supports compact CPU storage, precise large-world translation, direct structured transfer, and camera-relative
conversion without keeping a full 4x4 matrix per object.

On scene attachment the proxy:

1. verifies it is currently unattached;
2. stores a raw back-pointer to the concrete scene;
3. allocates a dense `SceneProxyID`;
4. adds an intrusive reference owned by scene membership.

Detachment frees the dense ID, clears all camera-specific data at that index, clears the scene pointer, and releases the
scene-membership reference. Destruction asserts that both the scene pointer and spatial leaf key have already been cleared.
This makes teardown-order violations visible.

`SceneProxyID` is a reusable dense array index, not a generation-checked identity. Allocation and free are deliberately
single-threaded. The high-water mark only grows, allowing camera data to address a proxy with direct array indexing. Before
an index is reused, every registered camera's per-proxy data at that index is cleared. This is fast, but correctness relies
on serialized add/remove and complete dependent-data clearing.

The base proxy itself implements the acceleration structure's user-data callback. It stores a `LeafKey` containing node and
object indices; the acceleration structure can update that key when internal compaction moves an object. Scene removal must
invalidate the key before proxy destruction.

Base relink replaces the packed transform and bounds. Collectable proxies add a spatial fast path:

1. compare old and new bounds;
2. update proxy transform/bounds;
3. attempt `QuickConditionalMoveObject` using the current leaf key;
4. if the object no longer fits that fast path, report that a full visibility-structure move is needed;
5. the scene batches those slow moves outside the individual proxy operation.

This is why base `Relink` returns a Boolean even though it always returns false: concrete spatial proxy layers use the return
value to propagate expensive relocation work.

The acceleration structure stores a pointer back to proxy user data, bounds, inclusion mask, and time-of-day visibility
mask. RED can compile either a hierarchy grid or a BVH behind a common alias, although the hierarchy grid is the active
configuration in the inspected source.

Once-per-frame proxy updates are actually once per proxy *per camera*. Frame trackers live in camera custom data indexed by
the dense scene proxy ID. A proxy-level spin lock prevents two collection jobs for the same camera/proxy from executing its
update simultaneously. The update reports whether it already ran this frame and whether it also ran during the preceding
frame, supporting coherent LOD/dissolve behavior.

Per-proxy custom data is an append-only RTTI linked list. An atomic eight-bit type mask gives a fast rejection path, a lock
serializes creation, and reads avoid locking because nodes are never removed. It is flexible but pointer-chasing, limited to
eight fast-mask types, RTTI-dependent, and permanently grows for the proxy lifetime.

Motion history is allocated sparsely from a fixed registry only for proxies that request it. Previous transforms survive
for the small number of frames needed by motion-vector generation, then the registry releases the slot. This avoids paying
previous-transform storage on every proxy.

#### Invariants to preserve

- A resident proxy has exactly one owning scene and exactly one valid scene-local identity.
- Scene membership retains the proxy independently of producer handles and queued commands.
- A proxy cannot be destroyed while it owns a valid spatial key or scene membership.
- Transform and bounds for one published proxy generation change coherently.
- Spatial relocation must update both acceleration membership and the proxy-to-leaf mapping.
- Dense-index reuse cannot occur until all camera/view/GPU data associated with its previous occupant is cleared or retired.
- Scene insertion/removal and dense-index allocation occur through one serialized mutation-commit path.
- Large-world translation remains fixed-point in compact proxy storage; floating conversion uses an explicit origin.
- Per-camera temporal state is keyed by both the camera/view and the proxy's valid scene-local generation.
- Expensive spatial relocation is separated from the common in-cell/in-node movement path and is batchable.

#### Vanguard decisions

- Preserve a compact 48-byte CPU transform payload with float rotation/scale and fixed-point translation. Implement packed
  access through explicit storage/bit conversion, not a public matrix whose translation floats secretly contain integer
  bits.
- Keep `RenderProxyTransform` as the convenient producer representation and convert it once into internal
  `PackedRenderTransform` during creation/relink commit.
- Split identity into:
  - generation-checked `RenderProxyId` for commands, handles, diagnostics, and external references;
  - dense internal `SceneProxyIndex` for SoA arrays, camera/view state, and future GPU Scene records;
  - private `SpatialEntryHandle` owned by the scene's acceleration adapter.
- Do not make proxy records inherit from acceleration-structure user data. The spatial adapter maps its entry back to a
  generation-checked scene proxy identity and privately handles compaction callbacks.
- Preserve scene-membership ownership and strict destruction assertions. Actual reclamation also respects queued mutation,
  read-snapshot, and GPU-frame retirement boundaries.
- Preserve the two-level relink path: cheap in-place spatial movement first, followed by batched reinsertion only when the
  acceleration structure says it is required.
- Store common proxy state data-oriented in scene-owned arrays: identity generation, kind, transform, bounds, layers,
  collection flags, lifecycle state, and spatial entry. Type-specific payloads live in type-owned pools keyed by proxy ID or
  dense index.
- Do not require a large virtual inheritance tree for `Base -> Collectable -> Drawable -> Mesh`. Collection capability and
  inclusion masks are explicit data/type operations. Specialized behavior can use registered type operations without
  putting camera or material policy in the common record.
- Keep per-camera frame trackers and LOD/dissolve state in the later camera/view-data machinery, not in the Render Scene
  base record. They remain directly indexed by dense scene index plus a generation check.
- Do not copy the append-only RTTI custom-data linked list into the base proxy. Later custom-data machinery will use
  registered typed slots or sparse type-owned stores with explicit lifetime and iteration costs.
- Preserve sparse, short-lived motion-history allocation as a later GPU Scene/motion-vector design input; do not add it to
  every base proxy now.
- Keep scene layers and collection masks close to spatial entries so traversal rejects irrelevant objects before proxy
  collection work.

#### Questions carried forward

- Which concrete scene arrays retain all proxies, collectable proxies, and non-default-layer proxies, and are their
  insert/remove operations ordered around spatial insertion?
- Does scene removal invalidate the spatial key before type-specific detach hooks can observe it?
- What exact synchronization protects hierarchy-grid fast moves during parallel relink jobs?
- Does the command handler serialize full spatial reinsertions after all relink jobs, or can it overlap them safely?
- How are scene snapshots made stable while collection jobs traverse acceleration data?
- Are inclusion masks immutable after insertion, and what migration path is used when render/visibility flags change?

### Chunk 05: concrete scene declarations and owned storage

Reviewed files and ranges:

- complete `renderer/src/renderScene.h`
- `renderer/src/renderScene.cpp`: constants, pending-relink storage lifecycle, construction, destruction, render-state
  transitions, frame allocation, proxy-index allocation, and store use sites
- `renderData/include/renderPublicEnums.h`: scene-layer declarations
- `renderer/src/renderCollectorTickList.h/.cpp`
- store use sites in `renderer/src/renderCommandHandler.cpp` and `renderer/src/renderRenderFrame.cpp`

#### Observed ownership map

RED's concrete scene contains four different categories of state.

| Category | RED-owned state | Architectural meaning |
|---|---|---|
| Core scene | scene kind/ID, rendering flag, frame and tick counters, dense proxy-ID allocator, main proxy acceleration structure, type/layer indexes, relink buffers and locks, statistics | Identity, lifecycle, mutation handoff, object storage/indexing, spatial collection, and observability. |
| Attached scene subsystems | camera storage, atomically shared visibility scene, query-only boxes | Per-scene consumers or secondary spatial facilities with independent internal state/lifetime. |
| Optional cross-cutting facilities | motion registry, collected-tick list, coherent-frame trackers | Sparse temporal work requested by selected proxy/view features. |
| Feature policy accumulated in scene | dissolve, forced LOD, particles, dynamic decals, shader custom slots, interior-map data, ray-tracing recycle hooks | Renderer features that use a scene but are not required to define scene membership or spatial state. |

The primary proxy collection structure is `m_rpMainSceneProxies`, currently a hierarchy grid selected through the common
acceleration alias. It is separate from `m_visibilityScene`. The former stores render proxies directly for CPU collection;
the latter is a retained visibility/occluder/query system used by collectors, occluders, distant shadows, and query-only
boxes. These are cooperating systems, not two names for the same structure.

In addition to the main acceleration structure, RED maintains specialized indexes:

- a light hash set;
- a dense particle array with swap-remove and repaired particle indices;
- a background drawable array;
- one collectable hash set for each non-default scene layer.

These indexes duplicate references to proxies to make feature collection direct. The declaration warns against adding a
new scene-wide collection merely for main-camera culling; such results should be produced during collection instead. This
is a useful constraint against permanent indexes for transient view results.

Scene layers are a small fixed mask: default, cyberspace, and world map. The main/default path uses spatial traversal;
alternate layers use flat hash sets because they are expected to contain few objects. The storage strategy is therefore
cardinality- and use-case-specific rather than one universal container.

The scene constructs:

- a renderer-global 8-bit scene ID;
- camera storage;
- an atomically shared secondary visibility scene;
- per-layer proxy sets;
- a free-index array preallocated for 2,048 proxies;
- two complete fixed-capacity relink buffers: 16,384 entries in game mode or 65,536 entries in editor mode.

Each relink slot owns strong proxy/skinning/track references and an owning instance-transform array. Reset releases all
references and both clears and shrinks the instance array. The cost and overflow behavior are examined in Chunk 07; the
declaration already shows significant per-scene reserved metadata and potential allocator churn for instanced relinks.

Scene destruction asserts that rendering has ended, flushes pending feature removal, deletes camera storage, processes
pending query-only operations, then releases the renderer-global scene ID. The declaration does not itself prove that all
proxies or relink requests are empty before destruction; command/world lifecycle chunks must establish the external drain
contract.

`BeginRendering`/`EndRendering` provide a checked Boolean guard around a render interval. `AllocateFrame` increments a scene
frame counter used for work that runs once even if the same scene is rendered through multiple views. `PrepareForUpdate`
separately uses the engine tick counter to avoid publishing/resetting scene state more than once per tick. This distinction
supports one scene being collected by multiple viewports or cameras.

Rendering statistics are accumulated into a write object and periodically copied to a read object under a lock. Readers
therefore do not observe partially accumulated current-frame values.

The collected-tick list is a fixed 2,048-entry atomic ring of raw proxy pointers. Producers reserve slots with an atomic
counter, while the consumer busy-waits for a reserved slot to be populated. It does not visibly retain proxy lifetime and
fails through assertions on overflow. It is a specialized optimization, not a model Vanguard should use for its general
scene mutation or deferred feature work.

#### Invariants to preserve

- One scene can be collected multiple times while its scene update/publish work runs only once for the corresponding engine
  tick.
- Core proxy stores and every secondary type/layer index agree after each committed add/remove transaction.
- Permanent scene indexes exist only for cross-view queries or feature traversal that justifies their memory/update cost.
- A scene cannot be destroyed while rendering, collection, mutation processing, or retained subsystem access is active.
- Scene-level statistics are published as a coherent snapshot rather than read from live accumulators.
- Primary render-proxy acceleration and secondary occluder/query visibility have explicit, separate ownership and update
  contracts.
- Alternate scene layers can choose a cheaper storage strategy when their cardinality and query pattern differ from the
  default world layer.
- Capacity exhaustion in bounded per-scene work queues is observable and cannot silently discard correctness-critical work.

#### Vanguard decisions

- Keep the core `RenderScene` ownership surface narrow:
  - immutable scene kind and generation-safe scene identity;
  - lifecycle/update/collection epochs;
  - proxy slot allocator and dense proxy storage;
  - common/type-specific indexes;
  - primary spatial index;
  - mutation ingress buffers;
  - coherent telemetry snapshots.
- Use explicit `SceneUpdateEpoch` and `ViewCollectionSerial` concepts. Updating one scene once must remain independent from
  collecting it for several editor/runtime views in the same frame.
- Replace the Boolean `isRendering` guard with scoped read/collection leases and explicit lifecycle states. Destruction
  begins only after producers close, queued mutations drain, jobs finish, read leases retire, and deferred resources clear.
- Keep the primary proxy spatial index behind a scene-owned adapter. If Vanguard later adopts a separate occluder/query
  visibility scene, attach it through a defined subsystem interface rather than exposing two mutable public structures.
- Maintain compact type indexes only where a feature needs scene-wide traversal. Type pools naturally provide most of these
  views; do not duplicate every proxy category in additional hash sets.
- Preserve specialized storage for low-cardinality alternate layers, but make layer definitions renderer/project agnostic.
  Names such as cyberspace and world map are game policy and will not enter Vanguard's core enums.
- Make scene-layer masks wide and registration-driven enough for editor/tool layers while retaining a fast inline common
  mask. The exact width waits for collection study; it must not be tied to one game's three layers.
- Camera definitions and camera custom data remain a later subsystem. Render Scene exposes stable proxy identity/state that
  camera/view storage can index, but the core scene does not construct cameras.
- Query-only spatial objects remain an attached Render Scene facility because their lifetime and results are scene-relative,
  but their implementation is isolated from ordinary proxy storage.
- Motion history, dissolve, dynamic decals, particles, interior data, collected ticks, ray tracing, and shader-specific
  parameters are registered scene extensions or renderer services, not fields in the foundational scene object.
- Do not use the RED collected-tick ring as a generic mechanism. Deferred collected work will use job dependencies or a
  bounded retained queue with explicit overflow/failure telemetry and no busy-waiting consumer.
- Retain double-buffered mutation ingress as the provisional direction, but do not copy RED's preconstructed array of
  thousands of heavyweight request objects. Chunk 07 will select storage after reviewing exact producer/consumer behavior.
- Publish statistics through a stable scene telemetry snapshot, including capacity, rejected mutations, coalescing,
  spatial fast/slow moves, active proxies by type/layer, and retirement backlog.

#### Questions carried forward

- What exact order does `AddProxy` use for attach hooks, spatial insertion, type indexes, and alternate layers, and how does it
  roll back a partial failure?
- What exact order does `RemoveProxy` use, especially spatial invalidation versus type-specific detach hooks?
- Are add/remove mutations processed before relinks in the same frame, and what happens to relinks targeting a removed proxy?
- How is the primary acceleration structure protected while parallel relink jobs use its quick-move path and collectors
  traverse it?
- Does the retained secondary visibility scene publish immutable state, lock internal mutation, or rely on frame ordering?
- Which scene extensions genuinely need callbacks at create, update, collection, and destroy boundaries?
- What drains pending relink references and feature queues before scene destruction?

### Chunk 06: proxy insertion and removal transactions

Reviewed files and ranges:

- `renderer/src/renderScene.cpp`: proxy ID allocation/recycling and complete `AddProxy`/`RemoveProxy` implementations
- `renderer/src/renderProxy.cpp`: base attach/detach ownership and scene-ID lifecycle
- `renderer/src/renderProxyDrawable.cpp`: collectable spatial-key invalidation
- `renderer/src/renderCommandHandler.cpp`: pending-operation preparation, ordering, execution, and queue fences
- `renderer/src/renderInterface.h/.cpp`: pending-operation representation, retention, coalescing algebra, and frame handoff
- `visibility/src/visVolumeHierarchyGrid.cpp`: spatial insert, swap-remove, relocation callback, and deferred rebalance

#### Observed transaction path

Producer threads never invoke the concrete scene insertion/removal methods. They call the render command facade, which
validates the scene/proxy references and appends an operation to a renderer-owned pending map under one lock. The pending
map retains both the scene and every proxy, groups operations by scene, and is moved into a private drain snapshot at the
frame handoff. New producer writes therefore land in the next pending map rather than racing with the current drain.

Before applying a scene's batch, RED merges all entries referring to the same proxy. The effective operation algebra is:

| Existing operation | Later addition | Later removal | Later move |
|---|---:|---:|---:|
| None | Addition | Removal | Move |
| Addition | Addition | None | Addition |
| Removal | None | Removal | Removal |
| Move | Invalid sequence | Removal | Move |

This makes add-then-remove and remove-then-add within one drain interval no-ops, absorbs moves into a pending addition,
and lets removal dominate a pending move. A move followed by addition is asserted because movement is only valid for a
proxy already attached to the scene.

The merged operations are then ordered for spatial locality and safe destructive updates:

1. removals and moves precede additions;
2. removals/moves are ordered by descending spatial node and object indices;
3. additions are ordered by their target cluster index.

Descending destructive order minimizes invalidation from packed-array swap-removal. The acceleration structure still
repairs the moved object's leaf key through `RelinkBVHNodeIndex`, so the spatial key is treated as mutable private metadata,
not stable identity. Addition cluster indices are captured from bounds when the request is enqueued, while the actual
spatial structure recomputes placement from bounds during insertion.

The command-handler job obtains all camera-data records once for the scene and performs the batch in this order:

1. remove proxies and apply full spatial moves;
2. rebalance dirty spatial nodes once after all destructive operations;
3. add proxies, grouped by target cluster;
4. release the queue's retained proxy references.

Additions expand their destination node bounds immediately, so RED does not run a second explicit rebalance after the
addition loop. Commands marked safe with proxy addition/removal may execute alongside this job. An explicit redJobs fence
then prevents commands classified as unsafe from starting until the proxy transaction and safe jobs have completed.

#### Exact insertion order

`AddProxy` is a synchronous, assertion-driven commit with this order:

1. attach the proxy to the scene;
2. allocate its dense scene proxy index and add a scene-owned intrusive reference;
3. read its scene-layer mask;
4. for the default layer, compute the visibility/shadow/time-of-day mask and insert its bounds plus user-data pointer into
   the primary acceleration structure without immediate rebalancing;
5. validate dynamic/drawable classification;
6. update specialized particle, light, or background-drawable indexes;
7. insert collectable proxies into every selected non-default layer set.

Spatial insertion writes the proxy's leaf key through a relocation callback. Particle insertion stores a dense index in
the proxy so later swap-removal can repair the moved particle's index.

There is no result value, reservation phase, or rollback path. Allocation/container failure is assumed to be handled by
the underlying engine facilities, and duplicate or invalid registration is guarded mainly by assertions/verifications.
Consequently, the method is only transaction-like because execution is serialized; it is not strongly exception/failure
atomic on its own.

#### Exact removal order

`RemoveProxy` performs the inverse operation in a deliberately different hook order:

1. read the proxy's scene-layer mask and current spatial leaf key;
2. if the default-layer key is valid, swap-remove it from the primary acceleration structure without immediate rebalancing;
3. remove pending dynamic-decal work and specialized particle, light, or background-drawable indexes;
4. remove the proxy from every selected non-default layer set;
5. invoke the proxy's type-specific detach chain;
6. invalidate collectable spatial-key state, clear all per-camera data indexed by the dense scene proxy index, recycle that
   index, clear the scene pointer, and release the scene-owned proxy reference.

Camera data must be cleared before the dense index becomes reusable. The removed spatial object's own leaf key is not
invalidated by the acceleration structure; collectable detach performs that invalidation. Swap-removal can relocate another
object and updates that surviving object's key immediately. The queue retains a strong proxy reference throughout removal,
so releasing the scene's ownership inside `DetachFromScene` cannot destroy the object while the transaction still uses it.

#### Invariants to preserve

- Scene membership has one serialized commit authority even though mutation requests may come from many producer threads.
- Pending work retains scene and proxy lifetime until the operation is committed or cancelled.
- Repeated operations for one proxy are reduced by an explicit, tested state-transition table before storage is mutated.
- Removal and movement cannot invalidate later destructive operations in the same packed spatial node.
- The proxy becomes externally collectable only after all mandatory identity, bounds, masks, and type indexes are valid.
- Every secondary index agrees with the primary proxy store at the publication boundary.
- Per-view/per-camera data is cleared before a dense scene index is recycled.
- The removed object's spatial entry is invalid before destruction, and relocation of a surviving object repairs its private
  entry handle immediately.
- Spatial repair is amortized across the batch rather than performed once per removal or full move.
- Commands that can observe or modify affected proxy state have an explicit dependency on transaction completion.

#### Vanguard decisions

- Preserve the multi-producer, per-scene retained ingress and move-at-drain handoff, but represent each target with
  generation-checked `RenderSceneHandle` and `RenderProxyId` values. Queue retention protects backing storage; identity
  validation prevents an old request from targeting a recycled slot.
- Preserve coalescing before mutation and encode it as a total transition table over explicit lifecycle states. Invalid
  sequences return a visible enqueue/commit failure in Development builds and increment telemetry; they are not silently
  repaired by an implicit API convenience.
- Keep one scene mutation commit job per drained scene batch. Parallel preparation is allowed, but updates to the canonical
  proxy store, dense indexes, and spatial adapter remain single-writer until profiling proves a partitioned writer is needed.
- Preserve remove/move-before-add ordering, descending destructive spatial order, cluster-local additions, and one amortized
  dirty-node repair. Compute the addition locality key from the final commit payload after coalescing so it cannot become
  stale if an accepted pre-commit update changes bounds.
- Replace virtual proxy-owned spatial relocation callbacks with a scene-private spatial adapter that repairs
  `SpatialEntryHandle` values in scene metadata. Proxy IDs and type payloads remain independent of acceleration-structure
  compaction.
- Make publication strongly transactional. Prevalidate identities, lifecycle transitions, bounds, layer registrations, type
  payloads, and required capacity before mutating canonical stores. Once commit begins, foundational insertion/removal steps
  are non-failing. If preflight fails, no partial scene membership becomes visible.
- Use an explicit attach sequence: reserve/validate slot and type storage, initialize all common/type metadata, install
  spatial and secondary indexes, then transition `PendingAttach -> Resident` at publication. Scene-owned lifetime begins at
  reservation and is released only after detach retirement.
- Use an explicit detach sequence: transition `Resident -> PendingDetach`, remove the object from published indexes, clear
  per-view state, retire its dense index and type payload, then transition to `Detached`. Public queries reject
  `PendingDetach` immediately even if physical reclamation waits for readers or GPU retirement.
- Do not let type-specific detach hooks control foundational cleanup. Registered type operations may release their own payload
  or feature registrations, but core scene, spatial, layer, and per-view teardown has fixed engine-owned ordering.
- Keep addition/removal operations allocation-light by reserving pending arrays and using Vanguard memory pools. Expose
  coalesced, rejected, cancelled, attached, detached, moved, dirty-node, and batch-size statistics through scene telemetry.
- Preserve command safety classification as a dependency property, not as separate ad hoc queues exposed to callers. The
  later frame/command study will decide whether this becomes access declarations, mutation epochs, or typed command lanes.

#### Questions carried forward

- Where do relink requests enter relative to the pending add/remove snapshot, and can a relink target a proxy in
  `PendingAttach` or `PendingDetach`?
- Does RED's quick-move path mutate the acceleration structure concurrently, or only prepare a slow-relink request for this
  serialized transaction?
- Which frame boundary makes the committed mutation batch visible to collectors, and is visibility based on job ordering or
  a published immutable scene snapshot?
- What external drain prevents scene destruction while operations written just after the pending-map swap still exist?
- Which concrete proxy types require attach/detach callbacks that cannot be expressed as type-pool construction and
  retirement?

### Chunk 07: relink ingestion, coalescing, and redJobs execution

Reviewed files and ranges:

- `renderData/include/renderScene.h` and `renderData/src/renderScene.cpp`: the four relink payload forms
- `renderData/src/renderProxy.cpp`: producer-side relink, relink-and-skin, and instanced-relink entry points
- `renderer/src/renderScene.h/.cpp`: retained relink slots, double-buffer handoff, admission, duplicate removal, parallel
  execution, slow-spatial escalation, and cleanup
- `renderer/src/renderProxy.cpp` and `renderer/src/renderProxyDrawable.cpp`: base state update and collectable spatial path
- `visibility/src/visVolumeHierarchyGrid.cpp` and `visibility/src/visBoundingVolumeHierarchy.cpp`: quick conditional moves
- `renderer/src/renderCommandHandler.cpp` and `renderer/src/renderRenderFrame.cpp`: frame preparation and job ordering

#### Producer ingress and ownership

Relinks bypass the general render-command byte queues and enter their owning scene directly. The public handle supplies one
of four payloads: transform/bounds, transform/bounds plus skinning and float-track references, packed instance transforms,
or matrices that are converted into packed instance transforms during admission. Bounds are validated at the handle.

Each scene owns two fixed-size arrays of fully constructed `PendingRelinkRequest` objects. Game scenes allocate 16,384
slots per side and editor scenes allocate 65,536 slots per side. A request owns strong references to its proxy, skinning
data, and float-track data, plus an owning dynamic array for instance transforms.

Multiple producers take a shared relink-index lock, reserve a unique slot with an atomic increment, and populate the current
write buffer. `PrepareForUpdate` takes the exclusive lock, flips the write index, atomically takes the request count, and
records the old side as the process buffer. The exclusive lock is the publication barrier: it cannot complete while a
producer still holds a shared lock and is filling a reserved slot. New requests immediately target the other buffer.

`PrepareForUpdate` is tick-gated and runs synchronously on the main thread while no render work is in flight. It also
publishes statistics and swaps query-only work. `ExecuteUpdateState` later consumes the captured relink side through the
same redJobs builder used by the frame tick. The scene retains itself until an epilogue job releases that ownership.

#### Admission and overflow behavior

RED avoids enqueueing an ordinary single-instance request when its submitted transform and bounds compare equal to the
current renderer-side proxy state and it has no skinning or float-track payload. Multiple-instance requests are always
accepted. This optimization reads mutable renderer-owned state on the producer path.

The admission predicate does not include the teleport flag. A teleport-only request with unchanged transform/bounds and no
auxiliary payload is therefore treated as a successful no-op and never reaches `MarkTeleport`.

The reservation counter increments even after the fixed array is full. Out-of-range requests log an error and return
failure; the later buffer handoff clamps the consumed count to capacity. Ordinary `Relink` and `RelinkAndSkin` handle methods
discard this result, while instanced relink returns it to the caller. Consequently, a correctness-relevant transform or
skinning update can be dropped with no recovery path at most call sites.

#### Duplicate reduction semantics

The first update job walks the captured requests backwards and uses a temporary pointer hash set to retain only the newest
whole request for each proxy. Older entries have their proxy reference cleared and are skipped by the parallel phase. This
is required because two jobs must never update the same proxy concurrently.

Whole-request latest-wins coalescing is not equivalent to field-aware state merging:

- the newest transform/bounds or instance set correctly replaces an older one;
- an earlier teleport can be lost when the newest request has `isTeleport == false`;
- an earlier skinning or float-track update can be lost when a later ordinary relink omits those fields;
- there is no explicit distinction between "field unchanged," "replace field," and "clear field."

The reduction allocates a hash set and performs a serial pass immediately before parallel work. It removes duplicate
execution cost, but all duplicate request payloads have already been copied and retained at ingress.

#### Parallel update phase

RED divides the original request count into `dispatcher thread count + 1` ranges. Each range processes its remaining unique
proxies sequentially, while the ranges run in parallel. For each live request it:

1. installs skinning and optional float-track data for supported mesh types;
2. updates transform/bounds or the complete mesh instance set;
3. marks mesh/morph motion history when the request is a teleport;
4. asks the spatial structure whether a full move is required;
5. batches proxies requiring slow spatial movement in groups of 64;
6. clears retained references and shrinks the request's instance array.

The duplicate-removal job, parallel update, and epilogue are ordered phases in the redJobs builder. The frame's general
command flush, including add/remove transactions, precedes scene relink execution. The final renderer frame-tick work
depends on all scene-update phases.

The parallel contract is therefore stronger than "different proxy pointers": each proxy type's relink implementation and
every scene extension it calls must also be safe to execute concurrently with relinks of other proxies. RED relies on this
discipline through implementation knowledge rather than declaring access requirements in the type contract.

#### Fast and slow spatial movement

For collectable proxies, the relink first updates proxy-owned transform and bounds. If spatial bounds changed and the proxy
has a valid leaf key, `QuickConditionalMoveObject` checks whether the new box remains within the current parent node.

When it remains contained, workers update distinct spatial object records in place. Structural mutation is fenced out, and
deduplication guarantees that two workers do not write the same proxy record. Parent bounds need no expansion, so the
contained move is immediately usable for later collection.

When it escapes the parent, the worker records a slow move through the renderer's pending proxy-operation map. Because the
current command-flush snapshot was already acquired before relink jobs execute, this move is not structurally applied until
a later frame. Temporary behavior differs by acceleration implementation:

- the hierarchy grid writes the new object bounds immediately even though the parent bounds may no longer contain them;
- the alternate BVH leaves the old object bounds in place until the deferred move;
- both report that a slow move is required, and the later serialized path performs remove/reinsert or a full move.

The grid source explicitly acknowledges that its temporary parent/object mismatch is only approximately correct and can
fail for small, fast-moving objects. The abstraction therefore does not provide one backend-independent publication
semantic for escaped-parent movement.

#### Lifecycle interaction

Add/remove command flushing precedes relink execution in the same frame builder:

- an addition followed by a captured relink becomes resident first and is then updated;
- a removal followed by a captured relink is detached first, but its retained relink request can still update the detached
  proxy object afterward;
- a slow move emitted by relink is added to the next pending-operation map rather than the already drained batch.

The relink path checks pointer validity but not a generation-checked scene membership state at execution. Scene destruction
depends on external command/frame draining and retained references; its destructor does not explicitly assert that both
relink buffers are empty.

#### Invariants to preserve

- Producers may publish relinks concurrently without exposing a partially written request to the consumer.
- A captured update epoch is immutable to producers; requests arriving after the flip belong to the next epoch.
- Retained proxy and auxiliary-resource lifetimes cover admission, queued residence, job execution, and cancellation.
- At most one update job mutates a given proxy in one scene update epoch.
- Transform, bounds, instance data, skinning state, teleport state, and spatial membership become visible as one defined
  scene version rather than a mixture of epochs.
- Structural spatial changes never race with parallel leaf-data updates or collection readers.
- Capacity exhaustion and stale lifecycle targets are observable and have deterministic handling.
- A proxy pending removal cannot receive a later published relink, while a proxy pending attachment can absorb its latest
  initial state before first publication.
- Slow spatial relocation completes before a scene version that contains the new proxy bounds is published to collectors.
- Coalescing preserves edge-triggered semantics such as teleport and explicit auxiliary-resource replacement/clearing.

#### Historical Vanguard decisions (superseded)

This subsection records the earlier attempt to redesign RED's relink machinery. It is not the active implementation plan.
The GPU-driven reevaluation later in this document now requires mirroring RED's bounded double-buffered requests,
newest-request-wins reduction, live proxy mutation, structural command lane, and Jobs ordering before adding Vanguard-specific
extensions.

- Preserve scene-direct, multi-producer relink ingress and an epoch flip before jobs are dispatched. Do not route
  high-frequency transform updates through a general command object or render function assumption.
- Replace the two enormous arrays of preconstructed heavyweight requests with pooled owning packets plus a per-proxy dirty
  mailbox/index. Repeated updates to one proxy should consume one dirty identity per epoch rather than one full queue slot
  per call.
- Address requests by generation-checked `RenderProxyId`. Admission validates scene ownership and lifecycle without reading
  mutable renderer proxy fields from producer threads.
- Make coalescing field-aware: latest sequence wins for transform/bounds or the complete instance set; teleport is ORed across
  the epoch; skinning and float-track fields use explicit `Unchanged`, `Replace`, and `Clear` operations. Incompatible
  single-instance/instance-set payloads produce a visible validation failure.
- Include teleport in admission even when transform and bounds are unchanged. Edge-triggered state must never be optimized
  away by value equality.
- Return a relink submission status from every public entry point. Normal operation remains non-blocking, but overflow,
  stale identity, wrong scene, invalid lifecycle, and invalid payload are distinct results with telemetry. A required update
  is never silently discarded.
- Coalesce at ingress or dirty-publication time so the job graph does not pay a serial hash-deduplication pass and does not
  copy payloads that are guaranteed to be superseded. Preserve deterministic per-proxy sequence ordering.
- Split execution into explicit jobs: captured-state preparation, parallel proxy/type-local updates, a dependent
  single-writer spatial commit, dependent serialized scene-extension updates where required, then scene publication.
- Type operations must declare whether their relink work is parallel proxy-local or requires a serialized scene-extension
  phase. Arbitrary virtual hooks are not assumed safe merely because proxy identities differ.
- Preserve the contained-leaf fast path, but keep the acceleration adapter's checks and writes private. Workers may prepare
  movement classification; the scene-owned spatial phase applies all in-place bound updates and escaped-parent
  relocations before publication.
- Do not round-trip slow movement into the next frame's general command queue. Gather slow IDs in per-job output and perform
  the structural moves in the same update epoch, followed by one dirty-node repair/rebalance.
- Define identical temporal behavior for every acceleration backend. A published scene version never exposes a child bound
  outside its parent and never pairs new proxy state with stale spatial bounds.
- Merge a relink targeting `PendingAttach` into the unpublished creation record. Reject/cancel relinks targeting
  `PendingDetach`, `Detached`, or a stale generation. Removal dominates any already captured relink before update jobs run.
- Reuse pooled variable-size instance storage; consuming a request clears logical contents without shrinking capacity on the
  hot path. Large retained blocks are reclaimed through explicit pool/budget maintenance rather than per-request churn.
- Schedule independent scenes concurrently where possible. Dependencies are per scene/update epoch; multiple preview,
  thumbnail, editor, and world scenes should not serialize solely because they share one builder.

#### Questions carried forward

- What exact command-lane and builder fences establish collection visibility after scene updates?
- Can general or per-proxy commands submitted during relink jobs observe partially updated proxy state before publication?
- How does the world runtime bridge order registration, visibility changes, relinks, and delayed destruction for the same
  producer handle?
- Which representative proxy types perform non-local work during relink and therefore require a serialized extension phase?
- Does the secondary visibility scene consume proxy transforms directly, or is it updated through a separate publication
  mechanism?

### Chunk 08: command ownership, lanes, and frame-tick ordering

Reviewed files and ranges:

- `renderData/include/renderCommandHandler.h`: public command facade and frame entry points
- complete `renderer/src/renderCommandInterface.h/.cpp`: command metadata and execution contract
- `renderer/src/renderCommandHandler.h`: page layout, queue families, counters, locks, and synchronization helpers
- `renderer/src/renderCommandHandler.cpp`: command construction, multi-producer publication, queue acquisition, execution,
  page recycling, phase dispatch, frame tick, render submission, resource retirement, and explicit synchronization
- `redJobs2/include/jobBuilder.h` and `redJobs2/src/jobBuilder.cpp`: authoritative `Fence::Full`, `Fence::None`, explicit
  fence, wait, and extracted-counter semantics
- targeted `gameFramework/src/gameEngine.cpp` and `engine/src/renderEngineViewport.cpp` ranges: caller-visible frame order

#### Command representation and ownership

The public command handler is a broad renderer facade rather than a public generic command API. Individual methods capture
their arguments into a private templated command containing a callable. Smart pointers and owning values captured by that
callable retain resources until the command executes and its placement-constructed object is explicitly destroyed.

Commands are stored inline in pooled linked pages. Each entry contains a 16-bit aligned blob size followed by a command
object aligned to 16 bytes. The compile-time command size must fit one page: 16 KiB for the globally ordered lane and 2 KiB
for a keyed parallel lane. This avoids one heap allocation and one pointer chase per command while allowing arbitrary
move-constructible captures within the size limit.

There are four queue families:

| Queue | Storage | Ordering purpose |
|---|---|---|
| In-order commands | one chain of 16 KiB pages | Commands without a key execute serially in reservation order. |
| Keyed parallel commands | 63 chains per safety class on PC, 31 on console | Commands sharing the same pointer-derived bucket execute serially; different buckets may run concurrently. |
| Subscene frames | retained frame pointers in four-entry pages | Deferred subscene rendering issued as dependent jobs. |
| Draw buffers | retained shared pointers in four-entry pages | Parallel command-list generation followed by batched submission. |

`CommitCommand` treats a null key as globally ordered. A non-null key is reduced modulo the fixed bucket count. The key is
usually a proxy pointer, but some commands use another affected object. Hash collisions only reduce parallelism; they do not
break correctness. Correctness across different keys is not inferred, so callers must avoid commands that mutate shared
state concurrently unless their implementations provide synchronization.

#### Multi-producer publication and queue snapshots

Producers take a shared commit/consume lock. Within a page they reserve byte ranges with compare-exchange, construct the
command in place, and release the shared lock only after construction completes. When a page is full, one producer takes the
exclusive lock, obtains a clean page from the pooled free list (or allocates one), and appends it.

Frame/flush entry points take the exclusive commit/consume lock before acquiring queues. `AcquireQueue` detaches the entire
published page chain and installs a fresh empty page. Because exclusive acquisition waits for every shared producer, the
consumer cannot observe a reserved but incompletely constructed command. Commands submitted after the swap enter the next
snapshot.

Execution walks each detached page linearly, invokes every command, explicitly destroys its placement object, resets the
page, and returns the chain to its queue-class free list. Page capacity therefore grows to observed command bursts and is
reused rather than reallocated every frame.

The handler has no command cancellation, per-command completion result, or recoverable execution failure. Shutdown assumes
external synchronization and drained queues. Its destructor also visibly contains cleanup defects: one subscene free-list
loop is duplicated and parallel free-page chains are not deleted in the reviewed implementation. These are implementation
hazards, not architectural behavior to preserve.

#### Keyed safety classes

Each keyed command carries one manually selected classification:

- safe with proxy addition/removal;
- not safe with proxy addition/removal.

The default is the unsafe class. A command qualifies as safe only when it neither depends on nor modifies data created,
removed, or changed by proxy scene membership operations, including bounds and camera custom data. The source comments show
this classification was introduced after concurrency failures between camera updates and proxy membership changes.

This Boolean class answers only one conflict question. It does not describe reads/writes of spatial data, proxy core data,
type payloads, camera state, scene extensions, resources, or global renderer state. It is assigned manually at each command
construction site, and two commands with different keys can still race through shared secondary state. The 63 buckets are a
contention heuristic rather than a dependency graph.

#### Exact queue-flush phases

redJobs builders default to `Fence::Full`: a default-dispatched job completes before the next phase can begin. Jobs
dispatched with `Fence::None` share the current dependency phase, and the builder requires an explicit fence before a later
full-fence job or final extraction.

Using those semantics, `DispatchQueueFlushJobs` establishes this exact order:

1. the detached in-order command chain executes on one job;
2. after that full fence, the scene add/remove/move transaction and every safe keyed bucket execute concurrently;
3. an explicit fence waits for the membership transaction and all safe buckets;
4. every unsafe keyed bucket executes concurrently;
5. a final explicit fence waits for all unsafe buckets before subsequent builder work.

Commands within one keyed bucket remain sequential. Commands in distinct buckets within the same phase have no ordering.
The in-order lane is conservatively serialized before everything else because it can affect camera custom data and other
state also touched by membership operations.

#### Exact frame-tick and render order

The normal game path performs the following sequence:

1. explicitly finish the previous command/render chain through `FlushPreviousFrameCommandsProcessing`;
2. gather all scenes that need this engine update;
3. call `FrameTick` once for the gathered set;
4. build UI/debug work and submit the viewport frame;
5. viewport submission eventually calls `RenderScene` with a retained render frame.

`FrameTick` first calls the synchronous, main-thread `FrameTickPrepare`. It advances the renderer tick, deduplicates scenes,
increments each scene frame, flips its relink/query ingress, and prepares skinning. The contract states that no render work
may be in flight, but this is enforced by caller convention—the common game paths explicitly flush beforehand—not by
`FrameTickPrepare` itself.

The asynchronous frame-tick builder then executes:

1. a wait on the handler's previous global flush counter;
2. frame-pool reset;
3. the complete command-flush phases above, including scene membership;
4. each gathered scene's relink/update phases;
5. renderer-wide frame-tick work such as skinning/resource maintenance.

The extracted completion counter replaces the handler's global flush counter. A subsequent `RenderScene` acquires the
exclusive queue-snapshot lock and builds another chain that first waits for this counter, flushes commands submitted since
`FrameTick`, processes queued subscenes and draw buffers, and finally invokes `RenderFrame`. Scene collection inside that
later render path therefore cannot begin before membership, relinks, and renderer frame-tick work complete.

Every `RenderScene`, resource-retirement operation, explicit synchronization operation, and synced renderer callback uses
the same global flush counter. This is simple and safe, but it serializes unrelated scenes, independent preview/thumbnail
views, and consecutive viewport submissions. Repeated screenshot renders intentionally become a chain through this counter.

`SyncWithRenderCommands` is the strong drain: it waits for the current global chain, snapshots and flushes every command,
subscene, and draw-buffer queue, installs the new completion counter, and then waits for it. `RetirePendingResources` is an
asynchronous reduced frame path that drains ordinary commands before backend resource retirement.

#### Invariants to preserve

- Published command storage owns every captured value until execution, cancellation, or shutdown destruction.
- A consumer snapshot never includes a partially constructed command, and producers can immediately write the next epoch.
- Commands targeting the same ordered identity execute in producer publication order.
- Parallel commands run together only when their declared accesses do not conflict.
- Scene membership commits before relink/update, and all scene mutation completes before collection consumes that version.
- Commands submitted after an epoch snapshot have a deterministic destination: a defined late phase or the next epoch.
- Page recycling occurs only after every command in the detached chain has executed and been destroyed.
- Synchronous drains are explicit exceptional operations, not an implicit per-frame requirement.
- Scene/version dependencies retain required objects without forcing unrelated scenes and views through one global barrier.
- Shutdown closes producers, drains or cancels all snapshots, destroys every retained payload, and releases every page pool.

#### Vanguard decisions

- Preserve inline type-erased packets in pooled pages for low-frequency heterogeneous renderer commands. Keep high-frequency
  scene membership and relink data in typed scene mutation ingress rather than representing every update as a captured
  callable.
- Keep the public renderer API typed. Arbitrary external lambdas/function pointers must not become an unreviewable path that
  bypasses lifecycle validation, memory attribution, telemetry, or dependency declarations.
- Replace raw pointer scheduling keys with stable generation-checked identities and explicit sequence numbers. Hashing may
  choose a worker lane, but it does not define lifetime or identity.
- Replace the safe/unsafe Boolean with compact access declarations over renderer domains such as scene membership, proxy
  core state, type payload, spatial structure, view/camera data, scene extensions, resource state, and renderer-global
  state. Each access declares read/write intent and its target scene/object identity.
- Build redJobs phases from declared conflicts. Independent targets and scenes may execute concurrently; conflicting writes
  receive explicit dependencies. Unknown or global access is conservatively serialized and visible in telemetry.
- Give each scene mutation epoch a completion token and published `SceneVersion`. View collection depends on the required
  version token instead of a renderer-global flush counter. Renderer-global maintenance retains its own narrower token.
- Preserve the frame relationship `membership -> relink/type update -> spatial commit -> publication -> collection`, but
  construct it as named dependencies rather than relying on command submission order and one global fence chain.
- Commands arriving after scene mutation capture normally target the next scene epoch. A small explicitly defined
  pre-collection lane may accept genuinely late view data, but it cannot mutate published scene membership or spatial state.
- Do not require the application thread to flush the entire previous render chain before `PrepareSceneUpdates`. The frame
  coordinator carries dependencies into jobs and applies back-pressure only when CPU frame-ahead limits, mutable ingress
  ownership, or shutdown genuinely require it.
- Allow one simulation scene version to feed multiple runtime/editor views concurrently. Presentation order and swap-chain
  ownership must not serialize CPU scene collection for unrelated viewports.
- Retain per-identity ordering without fixed pointer-modulo buckets as the semantic mechanism. A work-stealing scheduler may
  distribute ready packets after dependency construction; bucket count remains an implementation tuning choice.
- Provide enqueue and epoch telemetry: bytes/pages, command counts by kind/domain, contention, late submissions, dependency
  stalls, conservative global accesses, execution time, cancellation, and high-water marks.
- Define explicit failure policy. Fire-and-forget commands report validation/execution failures through diagnostics and
  telemetry; operations requiring an answer use a retained asynchronous result/ticket. Render workers do not throw through
  the scheduler.
- Implement a checked shutdown state machine: close ingress, reject new work, wait/cancel snapshots according to command
  policy, destroy all unexecuted packets, verify retained-object counts, then free every active and cached page.

#### Questions carried forward

- How does the world runtime bridge assign ordering between proxy creation, registration, visibility, relink, and delayed
  destruction before these operations enter renderer ingress?
- Which late camera/view updates must be accepted between scene publication and collection, and which belong to the next
  frame?
- Does RED collection read live mutable scene storage after the command fence, or a separately published visibility state?
- Which renderer-wide operations truly require a global dependency rather than per-scene or per-resource ordering?
- How should editor transactions request completion/undo evidence without turning the runtime mutation path synchronous?

### Chunk 09: world runtime bridge, registration, and delayed destruction

Reviewed files and targeted call sites:

- `world/include/runtimeSystemRendering.h`
- targeted ranges of `world/src/runtimeSystemRendering.cpp`
- `renderData/include/renderProxy.h`
- targeted ranges of `renderData/src/renderProxy.cpp`
- proxy creation/destruction paths in `world/src/meshNodeInstance.cpp`
- node attachment, detachment, cleanup, and shutdown ranges in `world/src/runtimeSystemNodeStreaming.cpp` and
  `world/src/worldRuntimeScene.cpp`

#### Observed ownership boundary

RED places a `RuntimeSystemRendering` between world objects and the low-level Render Scene. It retains the scene, creates
renderer proxies, initializes producer-side proxy handles with that scene, registers the handles, applies editor-wide
filtering, and owns the delayed-destruction queue. World node instances retain `RenderProxyHandle` objects; the low-level
scene does not know about nodes, entities, streaming cells, or the world object model.

The runtime system either creates a world, preview, or thumbnail scene or retains an externally supplied scene. Both cases
are stored as the same intrusive scene pointer. This correctly lets previews reuse a scene, but ownership intent and teardown
authority are implicit rather than represented by the type. Scene creation occurs during setup, while release may occur in
either post-world-detach or final uninitialization depending on build configuration.

Proxy construction and publication are separate operations. `CreateRenderProxy` only creates a renderer proxy and its
handle. `RegisterRenderProxy` later binds it to a scene and registers it. The convenience path does both. This is a useful
boundary for async construction, although RED does not expose a formal lifecycle state or transaction identifier across the
two steps.

#### Registration and attachment throttling

Registration records the handle in a development-only set, then either attaches it immediately or puts its raw address in a
pending-attachment array. At most 1,024 newly registered proxies are attached in one frame. A pre-render update drains the
remaining array under the registry lock and resets the per-frame counter. Destroying a handle first calls back into the
runtime system so its address can be removed from the pending array, then detaches it from the scene.

This establishes several important behaviors:

- proxy allocation is not equivalent to scene residency;
- bursts caused by streaming are admitted through bounded work rather than producing an unbounded frame spike;
- a proxy destroyed before admission never reaches the scene;
- attachment occurs before the scene command/mutation path is consumed for rendering;
- the producer-side owner, not `RenderScene`, decides when a constructed proxy becomes eligible for publication.

The mechanism is nevertheless incomplete as a general contract. The pending array contains raw pointers, the destruction
callback contains a raw runtime-system pointer, the complete registered-proxy set disappears in final builds, admission is
count-based rather than cost-based, and no ticket tells streaming when a proxy became resident. Holding the spin lock while
calling `Attach` also couples registry synchronization to command production.

#### Logical visibility versus physical membership

The handle combines two independent logical visibility inputs: source/world visibility and runtime/editor filtering. The
effective result is their conjunction. Changing either input recomputes the result only when it changes. Once attached, a
visible proxy is added to the scene and its auxiliary selection/scanning state is restored; an invisible proxy normally has
those effects cleared and is removed from the scene.

Physical membership is deliberately not identical to effective visibility. A proxy may report that it must remain attached
while invisible, for example to continue contributing local shadows. In that case RED updates the renderer-side visible bit
but keeps the proxy resident and remembers this exceptional state so a later visibility change does not add it twice.

This separation is worth preserving, but two Boolean inputs are not sufficient for Vanguard. Streaming activation, entity
enablement, component enablement, editor isolation, game visibility, resource readiness, fade state, and feature-specific
contribution policy must not overwrite one another. They need named policy bits or independently owned channels that reduce
to two results:

1. whether the proxy is logically drawable for a view family;
2. whether the proxy must remain physically resident in the scene for any contribution.

View-local visibility, frustum culling, occlusion, and per-camera filtering remain later collection decisions; they must not
cause scene insertion/removal.

#### Streaming producer behavior

Mesh node instances create their proxy only after the mesh resource and initialization data are available, retain the handle
as node state, and pass their current logical visibility during registration. On normal game detachment the node transfers
the final handle reference to the dissolve handler. Preview detachment releases it immediately. Other node types use the
same runtime-system creation boundary.

Node streaming can attach and detach node instances through redJobs in parallel batches. Shutdown first forces node-instance
cleanup so node detachment releases or transfers renderer proxies before runtime systems are destroyed. The source contains
explicit shutdown-order workarounds and TODOs around inter-system dependencies, demonstrating that this ordering is required
but not encoded as a dependency graph.

RED therefore provides the right high-level separation—world nodes own producer handles and a bridge owns scene admission—
but not an atomic world-to-render transaction. Parallel node callbacks can independently create and register proxies, and
the fixed attachment budget may make a streamed group partially visible across frames.

#### Delayed destruction and dissolve

Normal game removal does not immediately destroy a mesh proxy. The handle first receives a renderer command that changes
its dissolve visibility, then ownership moves into a thread-safe pending queue. During the pre-render update, pending handles
are moved into time-stamped batches. A batch is released after twice the configured global dissolve duration; releasing its
last handle detaches the proxy and queues normal scene removal. The factor of two accounts for a global dissolve phase that
may already be in progress when a proxy enters the queue.

This is cheap and gives streaming removals a visual grace period, but elapsed CPU time is only a conservative heuristic. It
does not prove that the dissolve command executed, that a relevant frame was presented, or that GPU references retired.
`ForceFlush` simply releases all retained handles. Dissolve is therefore presentation policy layered over ordinary proxy
lifetime, not the lifetime mechanism itself.

Vanguard should preserve optional visual retirement while splitting it into explicit milestones:

```text
logical removal requested
    -> optional fade resident
    -> scene detach committed
    -> no published scene reader can reference the proxy
    -> GPU/resource retirement completed
    -> slot generation advances and storage is reclaimed
```

Forced world shutdown may skip the visual fade, but it may not skip scene-reader and GPU retirement dependencies.

#### Shutdown hazards to reject

RED attempts to verify in development that all proxy handles are gone, but the corresponding assertion in runtime-system
uninitialization is disabled. The system can then release its scene while surviving handles still retain both the scene and
a raw callback pointer to the destroyed runtime system. The destructor checks that its own scene pointer was cleared, not
that no producer can call it later. Correctness consequently depends on world/node teardown order and external flushing.

Vanguard must encode this order rather than trust it:

1. close the bridge to new Flecs and streaming mutations;
2. wait for or cancel producer jobs that can emit bridge changes;
3. collapse pending creation, attachment, relink, visibility, and removal operations by stable identity;
4. force visual retirements to their detach stage during shutdown;
5. commit all remaining detach operations and wait for the published-scene reader epoch;
6. retire GPU-facing resources through the RHI lifetime manager;
7. verify that the proxy registry, pending admission queue, and retained retirement queue are empty;
8. release or return the scene according to an explicit ownership mode.

No proxy destructor may call an owner through a raw pointer. Explicit bridge records and generation-checked handles make
unregistration an ordinary state transition; RAII handles may request release, but their destruction is not the sole source
of lifecycle correctness.

#### Vanguard world bridge contract

`WorldRenderBridge` will be a per-world-session adapter above Render Scene and below Flecs-facing game-world services. It
consumes committed materialization changes, not Flecs mutation callbacks directly. This prevents renderer work from running
inside table moves, component constructors/destructors, or partially committed activation groups.

The bridge owns a registry keyed by stable entity identity plus render-contributor identity. One entity may produce zero,
one, or many proxies; submeshes, lights, decals, and future feature contributors are not forced into a one-entity/one-proxy
model. Each record carries a scene generation, proxy generation, lifecycle state, visibility channels, latest transform and
bounds revision, resource-readiness state, and optional retirement policy.

The minimum lifecycle is:

```text
Absent
    -> Constructing
    -> AwaitingAdmission
    -> PendingPublish
    -> Resident
    -> FadingOut (optional)
    -> PendingDetach
    -> Retiring
    -> Absent with a new generation
```

Removal dominates pending creation, admission, visibility, and relink in the same bridge epoch. Re-creation after removal
uses a new generation. Operations carrying a stale world, scene, entity, contributor, or proxy generation are rejected and
reported rather than applied to a newly reused slot.

Streaming activation groups are submitted as bridge transactions. Resource preparation and proxy construction may run in
parallel, but a group becomes render-published only after its required members pass preflight. Admission budgeting may defer
the complete group or explicitly split it according to authored streaming policy; an arbitrary count limit must not expose a
half-materialized group. Deactivation makes the group logically absent immediately, then schedules batched scene detachment
and retirement.

Admission should use measured budgets—estimated CPU setup cost, mutation bytes, proxy count, and optional GPU upload
pressure—with starvation prevention and priority classes for gameplay-critical, near-field, persistent distant, preview,
and background content. It produces a completion ticket consumed by streaming/editor state, while render jobs depend only on
the resulting scene-version token.

Scene ownership is explicit:

- `Owned`: the bridge creates and ultimately destroys the scene;
- `Borrowed`: an editor/preview owner supplies a scene lease and remains its teardown authority;
- `Shared`: a retained scene may outlive one bridge, with an explicit detach and handoff protocol.

A world switch creates a new bridge/scene generation and retires the old generation independently. Multiple editor views
may read one published scene concurrently; editor windows and viewports never own the bridge or scene.

#### Invariants to preserve

- World/ECS ownership and renderer scene ownership meet only through an explicit adapter.
- Constructed, admitted, scene-resident, logically visible, and safely reclaimable are distinct states.
- Every queued operation carries generation-checked world, scene, and proxy identity.
- Destroying a not-yet-admitted proxy cancels admission without publishing it.
- Named visibility owners compose; no subsystem restores visibility by overwriting another subsystem's decision.
- Invisible contributors may remain physically resident when their declared renderer roles require it.
- Streaming activation-group publication is transactional according to explicit group policy.
- Scene attachment work is bounded and observable, with completion evidence and starvation prevention.
- Logical removal takes effect before visual/GPU retirement, and reclamation waits for both scene and RHI lifetime domains.
- Shutdown closes producers and proves every queue and registry empty before releasing bridge or scene ownership.

#### Questions carried forward

- Does the visibility scene publish immutable acceleration state, use reader/writer epochs, or rely on the global render
  command fence while collection traverses live storage?
- Which proxy roles need physical residency while logically invisible beyond the demonstrated shadow case?
- What query-only spatial objects require bridge ownership without becoming render proxies?
- Which collection inputs are scene-versioned and which are legitimately view-local late data?

### Chunk 10: visibility scene and acceleration-structure publication

Reviewed files and integration ranges:

- `renderer/src/visibilityCommon.h`
- `renderer/src/visibilityScene.h/.cpp`
- `renderer/src/visibilityQueryBase.h/.cpp`
- `renderer/src/visibilityViewPersistentData.h/.cpp`
- `visibility/include/visVolumeHierarchyGrid.h`
- `visibility/src/visVolumeHierarchyGrid.cpp`
- comparison ranges in `visibility/include/visBoundingVolumeHierarchy.h`
- spatial mutation ranges in `renderer/src/renderScene.cpp` and `renderer/src/renderCommandHandler.cpp`
- query preparation and traversal ranges in `renderer/src/renderCollector.cpp`
- render bracketing ranges in `renderer/src/renderRenderFrame.cpp`

#### Two distinct visibility structures

The name `vis::Scene` is misleading if read as the owner of RED's main proxy acceleration structure. It is primarily a
query-lifetime and per-view temporal-data coordinator. The concrete `RenderScene` separately owns
`m_rpMainSceneProxies`, which is the spatial index traversed for main-camera, cascade, local-light, decal, and ray-tracing
candidate collection.

The responsibilities are therefore split as follows:

| Structure | Actual responsibility |
|---|---|
| `CRenderSceneEx::m_rpMainSceneProxies` | Live proxy spatial membership, cell bounds, inclusion masks, time-of-day masks, and traversal batches. |
| `vis::Scene` | Per-render query creation, query retention until render completion, view-ID keyed current/previous-frame data, debug accumulation, and occluder collection. |
| `vis::QueryBase` | Prepared frustum/occlusion policy and target testing against batches supplied by the spatial index. |

This separation is valuable. Spatial membership is scene-versioned world state, while a query is ephemeral view work. A
camera query must not own or mutate the scene index, and the spatial index must not absorb camera history or occlusion
policy.

#### RED's primary spatial index

The active configuration aliases `MainSceneAccelerationStructure` to `visGrid`; a compile-time option can substitute a
dynamic BVH, but that path is disabled in the reviewed configuration. The grid is a sparse hash map of 32-metre cells. It
uses X/Y coordinates only, deliberately collapsing Z after profiling showed cheaper traversal for this world. An object's
centre selects exactly one cell, while that cell's aggregate bounds expand to cover every contained object. The constants
also encode a fixed 16-kilometre-wide world assumption.

Each cell stores a dense array of compact records containing bounds, a raw user-data pointer, a 16-bit query inclusion mask,
and a 16-bit time-of-day visibility mask. A leaf key is `(cell index, dense object index)`. Removal uses swap-with-last and
immediately informs the moved object's callback of its new key. The key is consequently private mutable spatial metadata,
not durable public identity.

Objects are added and removed without immediate cell repair. Shrinking cell bounds and storage is deferred by marking dirty
cells, then `RebalanceTree` repairs the complete mutation batch once. Additions expand aggregate bounds immediately. This is
the same amortization already observed in the Render Scene membership transaction.

Movement has two paths:

- `QuickConditionalMoveObject` always writes the object's new bounds and succeeds when those bounds remain inside the
  existing aggregate cell bounds;
- if the bounds expand the cell, it reports that a structural move is required; the later commit either updates the object
  in its current cell or removes and reinserts it when its centre crosses a cell boundary.

The quick path can temporarily leave an object outside its parent cell bounds if the structural move is deferred. Its source
comment explicitly accepts this as an approximate result for objects that are neither too small nor too fast. Chunk 07
already rejected carrying that approximation across Vanguard's scene-publication boundary.

#### Batched traversal and job fan-out

Grid traversal first tests aggregate cell bounds against a prepared query. Passing cells are accumulated until either an
estimated object count or a fixed cell count reaches the template-selected batch threshold. The collector then dispatches
redJobs work for those cell batches. A job tests the dense object arrays, obtains visible proxy pointers and masks, and invokes
proxy collection. Separate call sites tune thresholds for main views, cascades, ray tracing, decals, and local-light boxes.

This two-level process is efficient for RED's workload:

1. sparse cells reject broad regions cheaply;
2. dense arrays give predictable target testing and prefetching;
3. batching avoids one job per cell or proxy;
4. aggregate frustum results avoid repeating per-object plane tests for fully accepted cells;
5. inclusion masks let one shared index serve several collection purposes.

The jobs receive pointers to the live grid cells and their live dense arrays. They do not receive copied node data or an
immutable snapshot. Address stability and lifetime therefore depend on excluding every add, remove, move, rehash, and cell
repair until all traversal and target-testing jobs have finished.

#### Actual publication and synchronization model

RED does not publish a separate immutable acceleration state. Scene membership and relink work mutate the live grid before
render collection. The renderer's global command chain orders those mutations ahead of `RenderFrame`; the frame then calls
`BeginRendering`, performs query creation and collection, waits for collection jobs through builder fences, and finally calls
`EndRendering`. A checked Boolean catches some overlapping render misuse, while comments and call ordering prohibit spatial
mutation during queries.

`vis::Scene` locks query registration and current-frame view-data maps, but its target lock is unused and the source explicitly
notes that add, remove, and update are unsafe while queries execute. These locks do not protect the primary grid. The safe
reader boundary is therefore the renderer-wide scheduling convention, not synchronization local to either visibility class.

This is fast when one render frame exclusively owns the scene, but it prevents independent overlapping readers, makes raw
cell pointers part of the hidden contract, and turns an unrelated late scene mutation into a renderer-wide ordering concern.
It also makes `m_isRendering` too weak to describe multiple collectors or editor views consuming the same scene version.

#### Query and view-data lifetime

At render start, `vis::Scene::PrepareForQueries` rotates this-frame view data into last-frame storage and recycles older
objects. Queries may be created concurrently and can prepare themselves through redJobs. The scene retains every query until
`FinishQueries`, where development builds assert that no external strong reference survived the render.

View IDs allow main-camera and related shadow queries to share temporal state. The reviewed persistent-data implementation
currently carries only recycled debug-draw storage, despite comments describing future activation state. It is not a general
camera history system. There is also a likely implementation defect: `GetLastFrameViewPersistentData` searches
`m_thisFrameViewData` rather than `m_lastFrameViewData`. Neither behavior should define Vanguard's contract.

Vanguard should keep query objects frame-scoped and allocate them from a frame arena. A query receives a retained
`SpatialReadView` and immutable view parameters; it cannot escape the owning collection epoch. Persistent occlusion and
activation history belongs to later camera/view custom data keyed by a generational view identity, not to the core spatial
index.

#### Vanguard spatial update contract

The Render Scene owns stable live proxy records and one live spatial index. It mirrors RED's ordering model rather than
publishing immutable copies: producers append retained relink requests to the active bounded buffer, frame preparation swaps
the two buffers, unique relinks update proxy-local state in parallel, and only structural spatial moves enter the serialized
render-command lane. Collection receives an explicit dependency on completion of that scene update chain.

```text
producer relink ingress
    -> swap bounded producer/processing buffers
    -> newest-request-wins duplicate removal
    -> parallel proxy-local transform/bounds updates
    -> quick conditional VisGrid bound update
    -> batch escaped-cell structural moves
    -> merge add/remove/move operations per proxy
    -> remove/move, repair dirty cells once, then add
    -> signal scene-update completion
    -> create view queries and produce candidates
```

Spatial records use compact generation-checked proxy identities rather than public raw addresses. The update/collection Jobs
chain retains the required proxy storage until every dependent consumer finishes. Runtime and editor views may coexist, but
they consume the same completed live scene state for that update epoch; Vanguard does not manufacture overlapping immutable
copies merely to let them observe different scene revisions.

#### Spatial-index shape for Vanguard

RED's 2D 32-metre grid is a strong measured choice for its city workload, not a universal engine contract. Vanguard should
begin with a sparse, world-coordinate grid whose cell coordinates are not limited by a fixed world half-size. Dimensionality,
cell size, looseness, and overflow treatment belong to scene/world configuration chosen by profiling. Very large or
persistent distant objects require an explicit coarse/overflow tier so one tower does not inflate a local cell across a huge
area. Dense cells may later gain a small local BVH when object density justifies it without changing the read-view API.

Broad inclusion policy should use registered renderer role masks rather than RED's fixed 16-bit game-specific and
time-of-day fields. Frequently tested masks remain compact and stored beside bounds; uncommon or extensible classification
belongs in proxy/type data. Time-of-day, editor isolation, ray tracing, shadow participation, and world-streaming policy must
not compete for undocumented bits.

Traversal preserves RED's coarse-to-fine batching, but batch sizing becomes data-driven. Telemetry records cells visited,
aggregate rejects, objects tested, visible candidates, jobs emitted, average batch occupancy, oversized objects, relink queue
depth, duplicate relinks removed, quick moves, structural moves, dirty cells, overflow, and update-chain latency. The scheduler
can target a minimum work estimate rather than hardcoding one threshold for every scene and platform.

#### Invariants to preserve

- Spatial mutation has one command-owned authority; readers never observe a partially repaired membership/relink batch.
- Collection starts only after the scene-update dependency covering relinks and structural VisGrid work completes.
- Producers continue in the alternate relink buffer while the captured update epoch is processed.
- Spatial entries use generation-checked scene identity; dense index relocation cannot retarget a stale job.
- Fast in-cell bounds updates and structural moves both complete before collection is released.
- Dirty aggregate bounds are repaired once per mutation batch, not after every removal or movement.
- Broad-phase traversal produces dense, coarse-to-fine work batches suitable for redJobs fan-out.
- View queries are ephemeral consumers of the completed live scene epoch and cannot mutate scene storage.
- Proxy retirement waits for retained command/update/collection work, independently of GPU resource retirement.
- Spatial configuration is renderer-agnostic and world-scale capable; no project-specific world extent or visibility bit
  allocation leaks into the public Render Scene API.

#### Questions carried forward

- Which exact proxy fields and type payloads collection dereferences after resolving a spatial candidate, and must those be
  part of the same immutable scene version?
- How should query-only boxes share spatial pages and reader epochs without becoming render proxies?
- Which view histories need stable generational identity across editor viewport creation, destruction, and reparenting?
- Can one collection query serve several related passes, or should prepared query geometry be shared while result sets remain
  pass-specific?

### Chunk 11: query-only objects and the collection boundary

Reviewed files and targeted integration ranges:

- `renderer/src/renderVisibilityQueryOnlyBox.h/.cpp`
- query-only declarations in `renderData/include/renderPublicTypes.h` and `renderData/include/renderScene.h`
- query-only lifecycle ranges in `renderer/src/renderScene.cpp`
- query-only culling in `renderer/src/renderCollector.cpp`
- streaming-side result consumption and teardown ranges in `world/src/worldNodeStreamingGrid.cpp`
- collection declarations and storage in `renderer/src/renderCollector.h`
- main-scene, cascade, and local-shadow collection ranges in `renderer/src/renderCollector.cpp` and
  `renderer/src/renderScene.cpp`
- base/collectable/drawable proxy contracts and once-per-frame update ranges in `renderer/src/renderProxy.h/.cpp` and
  `renderer/src/renderProxyDrawable.h/.cpp`

#### What a RED query-only box is

A query-only box is a renderer-evaluated visibility probe with no drawable proxy, material, render batch, or scene membership.
Its public descriptor contains bounds, an activation distance, and a development-only usage label. World streaming retains
the returned handle and polls whether the region was visible to the render camera. This lets visual visibility contribute to
streaming decisions without materializing fake renderable objects.

The implementation has a fixed capacity of 16,384 slots. The public 32-bit handle contains a 16-bit index and 16-bit
generation. Creation allocates a slot immediately and queues a copied descriptor. Destruction and bounds changes enter
separate multi-producer queues. `ProcessPending`, called from the scene update path, drains additions, removals, and the
previous update buffer into dense live-box and test-data arrays.

The boxes are not inserted into RED's main `visGrid`. Every render evaluates the complete dense `m_nodeData` array directly
against the prepared main-camera query, then applies a camera-distance test. This is a SIMD/cache-friendly flat test for a
bounded count, but it is O(number of probes) per evaluated view and does not share the proxy broad phase.

#### Double-buffered feedback

Visibility results are bitsets indexed by probe slot. Collection clears the back bitset, writes visible results in one job,
and requests a swap. At the next scene `PrepareForUpdate`, the front result bitset is swapped, so engine consumers observe
completed feedback with at least one frame of latency. Bounds updates use a similar front/back handoff: producers reserve
fixed-array entries atomically, the frame boundary flips buffers, and `ProcessPending` applies the captured updates.

This asynchronous contract is useful for streaming. Visibility feedback is advisory and should never require the game thread
to wait for rendering. RED also correctly uses a generation check when applying queued bounds updates to the current live
record and fixes dense indices after swap-removal.

Several hazards must not be copied:

- `IsVisible` verifies only that the index is alive, not that its current generation matches the queried handle, so a stale
  handle can read the visibility of a new occupant;
- stale destruction checks only index liveness before a fatal full-ID assertion, allowing delayed misuse to terminate the
  process after slot reuse;
- the fixed update buffer drops overflow and performs no latest-update coalescing;
- invalid handles, absent scenes, and not-yet-evaluated probes produce inconsistent Boolean fallbacks rather than an explicit
  unknown state;
- result bits carry no scene version, view identity, evaluation frame, or age;
- one global back bitset assumes one authoritative evaluation; two runtime/editor views can clear or overwrite one another;
- the distance comparison mixes visibility-query policy and streaming activation policy in the renderer;
- the reviewed world source contains result polling and teardown but no active creation call, so this particular integration
  appears dormant or incomplete and cannot be treated as a fully exercised production path.

#### Vanguard visibility-feedback service

Query-only targets remain first-class non-renderable spatial consumers, but they are not miscellaneous methods on
`IRenderScene`. `VisibilityFeedbackService` binds to a scene identity and validates the exact completed mutation epoch while
owning its probe storage independently. World streaming, editor diagnostics, audio, AI, or gameplay may create probes without
gaining access to RenderScene internals.

```text
VisibilityProbeHandle
    generational producer identity

VisibilityProbeDesc
    bounds, query roles, evaluation policy, debug category

VisibilityFeedback
    Unknown | Visible | NotVisible
    evaluated mutation epoch
    evaluated ViewSetRevision
    evaluation frame/time and age
```

Every public operation validates the complete generation. Creation, update, and destruction are blocked only while one
evaluation owns the unpublished bank. Active probes live in a dense directory with O(1) swap-removal. Jobs consume exact
disjoint ranges, and bank completion or cancellation prevents partial feedback from becoming observable.

The descriptor selects an explicit view policy:

- one generational view identity;
- any view in a named view family;
- the designated streaming-authority view set;
- no arbitrary hot-path callback; aggregation is fixed to visible-if-any for matching views.

Runtime streaming normally uses the streaming-authority game views; editor scene views do not accidentally keep the entire
world resident. Split-screen or future multi-camera games can use `AnyVisible` aggregation. A view-set revision makes results
from destroyed, recreated, or reconfigured views distinguishable. Unknown/stale feedback is surfaced to the consumer; the
streaming policy can conservatively retain content instead of interpreting missing renderer feedback as hidden.

Probe visibility and streaming distance are separate decisions. The implemented service reports conservative CPU
bounds/frustum visibility for the declared view policy; it does not claim occlusion truth or require GPU readback. A later
renderer observation source may refine this through a separately proven contract. The streaming observer combines feedback
with reference distance, hysteresis, prediction, priority, persistent-distant policy, and load state.

The probe store may use a flat dense SIMD batch below a measured threshold and a separate sparse spatial index above it. It
can reuse Vanguard's immutable spatial-page and reader-epoch machinery without inserting probe records into render-proxy
candidate spans. The read API stays unchanged whichever implementation profiling selects.

#### RED's collection boundary

The main spatial query returns raw `IBVHUserData*` values. Main-scene jobs cast them to `IRenderProxyCollectable*`, prefetch
several cache lines, apply editor selection filtering, and call virtual `CollectElements`. Cascade and local-shadow paths cast
the same pointers to drawable or mesh types and invoke separate virtual collection functions. Background and non-default
layer arrays bypass the spatial query and call those virtual functions directly.

Consequently, RED's spatial record is only a candidate locator. Collection dereferences substantially more live proxy state:

- proxy type, bounds, transform, scene ID, scene layers, selection identity, and inclusion roles;
- logical visibility, static/dynamic and shadow flags, auto-hide ranges, secondary reference points, and rendering plane;
- type-specific geometry, material, light, decal, particle, skinning, and custom-data pointers;
- persistent batch identifiers and resource references;
- camera custom data and scene custom managers reached through the update context.

Collection is not a const operation. `UpdateOncePerFrame` locks and mutates proxy frame trackers, calls type-specific update
hooks, and drawable updates cache camera distance used for LOD and visibility. Some collected proxies request a later
`CollectedTick`. The collector itself receives concurrent writes through atomics, locks, fixed-capacity arrays, stage
collectors, stats, and feature-specific containers.

This means an immutable spatial index alone is insufficient. A spatial read lease that resolves to a live mutable virtual
proxy would retain RED's global exclusion requirement and permit multiple camera jobs to race on shared per-view caches.

#### Vanguard collection read model

This earlier immutable-read proposal is superseded by the RED-aligned update/collection chain. Collection reads stable live
proxy/type storage after the per-scene update dependency completes. Generational identities are validated at command/relink
admission, and retained scene/proxy ownership spans every dependent collection job.

The base live record contains only broadly shared collection inputs:

```text
ProxyReadRecord
    proxy generation and type
    transform and world bounds
    renderer role/layer masks
    logical contribution flags
    auto-hide/reference-point policy
    type-payload index and GPU Scene identity
```

Per-view computations must not write shared state that can race another view. Distance, temporal visibility, occlusion history,
and feature state belong to later camera/view custom-data storage. The primary GPU-driven collection output is a compact
`GpuInstanceIndex` candidate span, not copied proxy/type packets. Specialized non-mesh proxy systems may retain RED-style
renderer-owned collection functions when their real requirements are implemented.

This keeps the useful RED flow:

```text
prepared view query
    -> coarse spatial cells
    -> dense candidate testing
    -> compact persistent candidate indices
    -> GPU visibility and later graph stages
```

#### Collector output rules

Collection outputs belong to a view/frame, not the scene. Each job writes to an exclusive page or range whenever possible.
Atomics may reserve ranges, but overflow cannot leave counts beyond physical capacity as RED's fixed light arrays can; every
bounded output reports accepted, dropped, and fallback counts. Locks are reserved for low-frequency aggregation such as
diagnostics, not per-candidate insertion.

Outputs retain stable GPU Scene indices until the consuming graph phase completes. They never retain pointers into temporary
query results. Main-view results may feed dependent work such as local-shadow query construction through explicit redJobs
dependencies, while independent views collect after the same completed scene-update epoch.

#### Invariants to preserve

- Non-renderable visibility probes do not masquerade as proxies and do not enter render batches.
- Visibility feedback is asynchronous, versioned, view-policy-specific, and explicitly allowed to be stale or unknown.
- Every probe operation validates full generational identity; slot reuse cannot redirect polling or destruction.
- Multiple views publish independent results before an explicit aggregation policy combines them.
- Streaming distance, hysteresis, and residency decisions remain outside renderer visibility evaluation.
- The update/collection dependency and retained proxy ownership cover all base and type-specific data dereferenced by jobs.
- Parallel collection reads immutable scene data and writes only job-local or declared reduction outputs.
- Per-view and temporal calculations never mutate shared published proxy records.
- Collection outputs retain version-safe identities/references until dependent batching work completes.
- Capacity and overflow behavior are observable and deterministic in every build configuration.

#### Questions carried forward

- Which exact frame-stage dependencies surround collection, collected-tick work, camera custom-data updates, and render-node
  consumption in RED's complete render-frame path?
- Which mesh, light, decal, and camera-data payload fields belong in immutable type pools versus view-local state?
- Does any representative proxy require scene mutation during collection, or can every such action be expressed as a deferred
  reduction/next-epoch command?
- What minimum visibility history contract must be ready before persistent camera custom data is implemented?

### Chunk 12: render-frame integration ranges

Reviewed files and ranges:

- `renderer/src/renderRenderFrame.cpp`: `FrameTickPrepare`, `FrameTick`, graph selection/build, `RenderFrame`,
  `StartFrameRendering`, and `EndFrameRendering`
- `renderer/src/renderGraphNodes.h/.cpp`: `CRenderNode_StartRender`, `CRenderNode_DoCulling`, `CRenderNode_EndRender`,
  `CRenderNode_EndFrame`, and related node scheduling
- `renderer/src/renderScene.cpp`: scene frame allocation, begin/end rendering, scene end-frame cleanup, update-state entry
- `renderer/src/renderCommandHandler.cpp`: command flush ordering around pending scene proxy operations

#### RED frame order

RED separates scene/frame work into three major intervals:

1. `FrameTickPrepare` runs on the main-thread side before pending render commands are kicked. It resets double-buffered frame
   memory, advances the renderer tick counter, prepares skinning flush state, deduplicates the submitted scenes, then calls
   each scene's `AllocateFrame` and `PrepareForUpdate`.
2. command flushing and scene update work apply pending proxy operations, relinks, camera updates, and resource-side work
   through redJobs. This is where scene mutation is serialized enough that render collection can later assume a coherent live
   scene.
3. `RenderFrame` builds or reuses the render-node graph for the current rendering mode and camera setup, prepares camera and
   scene custom data, runs the graph once in resource-allocation/pre-consume mode, resolves the flow allocator into consume
   mode, then releases the node jobs to execute the actual render work.

Within the graph, `CRenderNode_StartRender` calls `StartFrameRendering` during consume. `StartFrameRendering` begins renderer
frame state, sets render settings, advances batch-data frame memory, calls `visScene.PrepareForQueries`, ticks render-side
scene proxies, and marks the scene as rendering. Culling then runs in `CRenderNode_DoCulling`, where main-scene, cascades,
local-shadow, ray-tracing, and query-only culls are dispatched as `Fence::None` jobs followed by an explicit fence before
dependent nodes consume collection output. `CRenderNode_EndRender` calls `EndFrameRendering`; that updates stats, resets
collectors, ticks any collected-proxy list that remains, clears the scene rendering flag, finishes visibility queries, and
ends global renderer frame state. `CRenderNode_EndFrame` performs scene end-frame cleanup and clears the jobs-frame pointer.

The important RED dependency shape is:

```text
FrameTickPrepare
    -> command flush and scene mutation/update jobs
    -> graph build/cache and custom-data preparation
    -> flow allocator pre-consume
    -> flow allocator resolve to consume
    -> render-node jobs
        -> StartRender
        -> DoCulling and collection jobs
        -> draw/feature nodes
        -> EndRender
        -> EndFrame
```

#### Why this is performant

RED does not treat culling and collection as a game-thread callback. It is part of the render graph's CPU job path. The graph
is cached by rendering mode, camera count, feature flags, and selected camera properties, so the expensive dependency wiring is
reused when the frame shape is stable. The resource-flow allocator gets one dry run to declare/resolve transient resources
before consume work starts, and node execution is held behind a kickoff counter until draw-buffer commands and flow allocation
are ready. Culling itself fans out at the pass level and again at the spatial-cell batch level.

This model also explains why RED can use live scene pointers in visibility results: it relies on a broad render-exclusive
interval bracketed by `BeginRendering` and `EndRendering`, plus command flushing before render jobs. That implicit global
exclusion is simple and fast, but it is also the thing Vanguard should replace with explicit leases and versions.

#### Hazards not to inherit

- `m_isRendering` is a debug-era Boolean, not a complete synchronization primitive. It cannot describe multiple readers,
  frame overlap, editor/runtime simultaneous views, or which scene version a job is using.
- Camera custom data, scene custom data, collector output, and proxy updates are interleaved in `RenderFrame`; Vanguard should
  keep the same ordering but make each stage a named contract so RenderScene can be tested without the full renderer.
- RED's pre-consume/consume split leaks through `rctx.IsConsumePhase()` checks in many node implementations. Vanguard should
  preserve the two-phase resource-flow performance model, but node code that only renders should not branch on allocator phase.
- RED uses one global jobs-frame pointer and a global render debug flag. Vanguard needs per-frame execution objects so
  preview, thumbnail, editor viewport, and runtime frames can be reasoned about independently.
- End-frame cleanup mixes scene cleanup, collector reset, profiler updates, query finalization, and global renderer frame
  state. Vanguard should keep the dependency order, but expose narrower services for ownership and testing.

#### Vanguard frame-stage contract

RenderScene implementation should expose the following frame-facing operations without depending on the future full
RenderingService:

```text
RenderSceneFramePrepare
    input: scene handle, engine frame index, mutation admission budget
    output: prepared mutation batch and publish token

RenderSceneCommit
    input: prepared mutation batch
    output: SceneVersion, SpatialVersion, completion token

SceneReadLease AcquireSceneRead
    input: required SceneVersion or latest completed version
    output: coherent immutable scene/spatial/proxy/type-payload read view

RenderSceneCollect
    input: SceneReadLease, ViewCollectionRequest, job builder
    output: collection completion token and view-local output pages

RenderSceneEndFrame
    input: completed collection tokens for the frame/view set
    output: retired probe results, stats, reclaim candidates
```

`RenderSceneFramePrepare` and `RenderSceneCommit` correspond to RED's `AllocateFrame`, `PrepareForUpdate`, pending operation
application, relink execution, and query-only pending processing. `AcquireSceneRead` is Vanguard's stronger replacement for
RED's `BeginRendering` plus live-pointer access. `RenderSceneCollect` maps to RED's `DoCulling` and collection jobs. It may be
called by the later render graph, but the collection boundary itself is CPU-only and testable today. `RenderSceneEndFrame`
publishes feedback and diagnostics once dependent jobs are complete; it is not GPU present or swap-chain work.

Render graph integration later should schedule scene collection like RED schedules `CRenderNode_DoCulling`: as render-path
jobs with explicit fan-out and a dependency token before draw/batch consuming nodes. The graph may own the high-level ordering,
but it should not own scene mutation, proxy lifetimes, or visibility probe state.

#### Invariants to preserve

- Scene mutation publish completes before any read lease for that version is issued.
- Culling and collection run under a pinned scene read lease; no collector reads mutable staging storage.
- Collection completion is a token consumed by later batching/render-node work, not a blocking wait in the game loop.
- Query-only feedback is published after the collection token for its view policy completes.
- Scene end-frame cleanup cannot reclaim proxy, spatial, or probe storage still pinned by a read lease or dependent GPU work.
- Render graph resource-flow phases may schedule collection, but RenderScene nodes do not manually manage transient GPU
  resource allocation.
- Multiple viewports may collect from the same `SceneVersion` concurrently; each has independent `ViewCollectionState`.
- Frame-stage names are renderer-owned contracts, not application-state lifecycle events.

#### Questions carried forward

- Which custom-data preparation pieces must be available before the first real RenderScene collector can run?
- Should `RenderSceneCommit` live in the existing frame pipeline service or in a dedicated render-scene service registered
  with the engine host?
- What minimum no-scene/blank-frame contract should the editor and thumbnail renderer use before cameras are implemented?

### Chunk 13: representative mesh, light, decal, and camera-data proxies

Reviewed files and ranges:

- `renderer/src/renderProxyMesh.h/.cpp`: mesh payload fields, custom data, collection, LOD/dissolve, dynamic decals, relink,
  scene attach/detach, clustered instances, and ray-tracing-facing state
- `renderer/src/renderProxyLight.h/.cpp`: light payload fields, distance fading, collection, flicker, shadow-manager coupling,
  and parameter updates
- `renderer/src/renderProxyDecal.h/.cpp`: static decal payload, dynamic decal spawner, projection cache, material/routing data,
  and decal collection
- `renderer/src/renderProxyCameraData.h/.cpp`: per-camera proxy state, dissolve state, frame trackers, and clustered instance
  buckets
- `renderer/src/renderFrameCamera.h/.cpp`: camera and scene custom-data storage/preparation/readiness
- representative collection and scene membership ranges in `renderer/src/renderScene.cpp` and `renderer/src/renderCollector.h/.cpp`

#### Mesh proxy shape

RED's mesh proxy is a dense mix of renderer identity, resource references, material layout, visibility policy, LOD policy,
instance data, feature flags, and several optional custom-data records. Hot collection fields include:

- mesh resource, material LOD groups, mesh chunk mask, render mask, shadow flags, object type, camera target, LOD setup, forced
  LOD state, dissolve-visible state, rendering plane, and ordering;
- instance transforms, instance count, optional static instance-buffer offset, clustered-proxy state, and disabled-instance
  updates;
- skinning, float-track, GPU skinning buffers, material overrides, material parameter vectors, dismemberment/destruction
  buffers, index-buffer override, vehicle parameters, rain data, wind data, garment data, mirror data, and ray-tracing data.

`CollectElements` performs feature filtering, camera-target filtering, debug filtering, velocity/rain/wind feature selection,
per-camera `UpdateOncePerFrame`, LOD and dissolve selection, clustered-instance bucketing, material pass discovery, dynamic decal
collection, and render-stage chunk emission. Shadow collection uses a different path and context, but still calls into mesh LOD,
dissolve, skinning, and material data.

Mesh proves that Vanguard cannot model collection as "visibility result -> draw object". The renderer needs an immutable
`MeshProxyPayload` plus a view-local `MeshViewState` and typed mesh collector output. Optional custom data should become typed
optional payload sidecars with explicit mutation commands, not a base-proxy custom-data map that arbitrary code can mutate
during collection.

#### Light proxy shape

Light collection is not mesh-like. RED lights are collectable proxies that usually push themselves into the collector's light
array after camera-distance and intensity/fade checks. The payload includes color/intensity/EV, radius, shadow radius, shadow
softness, projection texture, IES profile, light channels, local/contact shadow toggles, GI/fog/env/transparent/particle
participation toggles, group fade, roughness bias, movable/component bits, secondary reference point, and optional flicker
state.

If collected, the light updates once per frame; flickering mutates light state in that update. Local shadow ownership is
handled by scene custom data: lights register with and release from `SceneCustomData_ShadowManager`, and later shadow-manager
code schedules shadow slices and renders mesh collections for those light cameras.

For Vanguard, light collection should emit `CollectedLight` records into a typed light output page, not render chunks. Flicker
and distance fade are view/frame update products unless explicitly authored as producer-side mutation. Shadow scheduling is a
scene/view custom-data concern that consumes collected lights and emits shadow queries; it should not be hidden in light proxy
destructors or mutable pointer callbacks.

#### Decal proxy shape

Screen-space decals are drawable but not mesh proxies. RED stores material info, initial transform, cached projection matrix,
atlas/sub-UV data, tangent frame, alpha/scale/depth-fade/emissive values, packed normal threshold and roughness scale, order,
surface routing, render mode, flip/stretch flags, normals-blending mode, and ray-tracing participation. Collection filters by
decal render mask, material validity, alpha, projection validity, camera-facing/backface threshold, and autohide fade. It then
discovers material passes and emits decal render chunks with decal-specific geometry batch data.

Dynamic decal spawners are another distinct proxy: they can exist as collectable scene objects whose collection triggers decal
creation or target attachment rather than direct drawing. Meshes can also collect dynamic decal chunks against their own mesh
passes.

For Vanguard, decal payloads should live in a dedicated `DecalProxyPayload` pool and emit `CollectedDecal`/decal-render-packet
records. Decal-on-mesh interaction is not part of the base mesh collector; it is a feature output that reads mesh and decal
payloads through declared domains.

#### Camera and scene custom data

RED's camera storage owns two custom-data arrays: scene-level storage and per-camera storage. Scene custom data is prepared once
for the frame storage; camera custom data is prepared for each camera before render nodes run. Readiness checks sort prepared
custom data by priority and can block rendering/loading if a required data object is not ready.

The most important representative custom data is `CameraCustomData_RenderProxyData`. It stores:

- a per-scene-proxy `CRenderDissolveState`;
- a per-scene-proxy `SFrameTracker`;
- optional clustered-proxy data containing instance LOD/dissolve buckets, per-instance state, reordered dynamic-buffer
  transforms, visible LOD masks, transition counts, and refresh flags.

This confirms that Vanguard needs a first-class `ViewProxyState` indexed by generational proxy identity and view identity. It
must be prepared/resized before collection, retained until dependent jobs finish, and cleared by proxy-generation retirement.
It should not be a hidden mutable side object accessed from arbitrary proxy methods.

#### Vanguard payload and collector model

The first implementation should use a closed initial collector table for the representative types, while keeping registration
possible later:

```text
ProxyReadRecord
    base identity, transform, bounds, role masks, layer mask, visibility flags, type id

MeshProxyPayload
    mesh resource, material groups, chunk/feature masks, LOD setup, instances, optional sidecar refs

LightProxyPayload
    light type, color/intensity/radius, channel/features, shadow flags, projection/IES refs

DecalProxyPayload
    material, projection/tangent frame, packed params, routing, ordering, effect params

ViewProxyState
    frame tracker, dissolve state, selected LODs, clustered instance buckets, cached distance

CollectorOutputPages
    mesh draw packets, decals, lights, shadow candidates, feedback, diagnostics
```

Collection dispatch should be by proxy type over dense payload pools. A collector receives immutable base/payload records plus
exclusive view-local state and writes to job-local output pages. It cannot mutate the published scene, cannot retain raw
pointers to mutable payload storage, and cannot call back into arbitrary producer code. Any feature needing persistent
post-collection work emits a typed follow-up command for the owning system to reduce after collection.

#### Invariants to preserve

- Meshes, lights, decals, and dynamic decal spawners are separate payload/output families; a generic draw-packet-only model is
  rejected.
- Base proxy data is small and shared; mesh/light/decal fields live in typed payload pools.
- Optional mesh sidecars are explicit typed payload references with mutation commands and generation checks.
- View-local state owns LOD, dissolve, frame tracking, clustered buckets, cached distance, and feature-temporal values.
- Collection output is typed, bounded, and observable; fixed-capacity overflow is reported in every build configuration.
- Shadow scheduling consumes collected light/proxy outputs through custom-data or frame-stage reducers, not through hidden
  mutable proxy callbacks.
- Dynamic decals are a feature interaction between decals and mesh targets, not a responsibility of the base RenderScene API.
- Camera/scene custom data must be prepared before collection and must expose readiness without requiring the full renderer.

#### Questions carried forward

- Which mesh sidecars are needed in the first implementation: skinning, material overrides, clustered instances, dynamic decals,
  or only static mesh data?
- Should the first collector table be closed at compile time, or should proxy types register collector descriptors through the
  rendering module startup path immediately?
- How much of dissolve/LOD should be implemented with the first mesh collector versus represented as placeholder state until
  material/batcher work exists?
- Which scene custom-data services are mandatory before local-shadow and dynamic-decal features can leave placeholder status?

### Chunk 14: cross-check against Vanguard systems

Reviewed Vanguard files and ranges:

- `source/jobs/include/vanguard/jobs/jobs.hpp`: priority classes, builder/counter/fence contract, explicit fence closure, and
  non-copyable completion tokens
- `source/engine/include/vanguard/engine/frame_pipeline_service.hpp` and
  `source/engine/src/frame_pipeline_service.cpp`: global frame phases, main-thread participant scheduler, profile filtering,
  dependencies, frame context, and participant failure path
- `source/engine/include/vanguard/engine/game_world_service.hpp` and
  `source/engine/src/game_world_service.cpp`: Flecs world ownership, cell streaming system ownership, prefab/cell decoder
  registration, simulation-frame participant, and shutdown drain
- `source/entities/include/vanguard/entities/materializer.hpp` and
  `source/entities/include/vanguard/entities/cell_streaming_system.hpp`: queued cell materialization, activation groups,
  release synchronization, reference publication, and streaming event forwarding
- `source/world/include/vanguard/world/streaming_executor.hpp`: resource-available, release-requested, ready, and complete-release
  boundaries
- `source/resources/include/vanguard/resources/resources.hpp` and
  `source/resources/include/vanguard/resources/resource_pipeline.hpp`: resource references, strong/weak handles, async requests,
  dependency discovery, cancellation, coalescing, and failure traces
- `source/memory/include/vanguard/memory/pool.hpp`: canonical engine pools, `Rendering` pool, and object-to-pool macros
- `source/rendering/include/vanguard/rendering/presentation_service.hpp`: presentation output, viewport, swap-chain, and window
  attachment ownership that must stay outside RenderScene

#### Existing engine hooks that RenderScene can use

Vanguard already has the important non-rendering side of the pipeline. Jobs exposes a dedicated `RenderPath` priority, explicit
`Builder`, `Counter`, `DispatchAfter`, `DispatchParallel`, and a strict `Fence::None`/`DispatchFence` contract. RenderScene
collection should use those counters directly. It must not create another scheduler, and it must not hide blocking waits inside
the application loop. A later render-command/graph layer can decide where to wait or apply frame-ahead back-pressure.

The frame pipeline already owns engine frame order and supports participant dependencies. The current global phases include
`WorldStreaming`, `Presentation`, `Render`, and `EndFrame`. `GameWorldService` is already a frame participant in `Simulation`.
It owns one Flecs-backed `GameWorld`, registers the cell streaming system, loads `.vcell`/`.vprefab`, and ticks the world. That
means RenderScene should be registered as an engine service and frame participant after the world/materialization side has had a
chance to commit, not as a branch in presentation or window code.

The world and entity systems already expose the correct streaming seams. `WorldStreamingExecutor::Process` reports when a
resource is available, failed, release-requested, or cancelled. The executor also requires downstream systems to call `SetReady`
and `CompleteRelease`. The cell streaming system materializes ECS entities only after resources are available and completes
activation/release after the owning game world flushes structural changes. RenderScene should mirror that shape for renderer
proxy residency: a proxy or cell render payload is not "ready" merely because its package bytes loaded; it is ready after scene
mutation, resource handle capture, and any required renderer-residency admission have completed.

Resources are already suitable for renderer payload ownership. `ResourceReference` is the persistent logical identity;
`ResourceHandle` pins loaded objects with generation safety; `PipelineRequest` represents an async caller interest and supports
priority promotion, cancellation, and failure traces. RenderScene payloads should therefore store resource references for
identity and strong or weak handles for residency, never raw source paths, depot-style paths, or unowned decoded pointers.

Memory is also far enough along. The engine has a canonical `Rendering` pool and RED-style object-to-pool macros through the
Vanguard-facing surface. First implementation code should allocate scene records, mutation pages, spatial pages, collector pages,
and view state from `memory::pools::Rendering` or narrower render-scene pools declared through the same mechanism. Standard
containers or `std::malloc` are not acceptable for hot scene storage when an engine equivalent exists.

#### Required service shape

RenderScene should remain a renderer subsystem with explicit frame-facing operations. `RenderingService` is the engine
composition point; scene state does not require an independent engine service. The shape is:

```text
RenderingService
    owns RenderSceneManager and frame-pipeline registration
    exposes scene creation/destruction for runtime, editor, preview, and thumbnail worlds
    owns frame-stage admission budgets and service stats

RenderSceneManager
    owns scene slots and generational RenderSceneHandle values

RenderScene
    owns proxy storage, payload pools, bounded mutation accounting, spatial state, and scene-update epochs

WorldRenderBridge
    optional per-world adapter attached above GameWorld/Flecs
    converts committed world/entity/component changes into typed RenderScene operations
```

The service should not depend on Flecs. The bridge may know about the game-world/materialization layer, because that is exactly
the adapter boundary. Preview scenes, editor inspection scenes, and thumbnail scenes must be able to exist with no Flecs world at
all. Conversely, `GameWorldService` must not learn about concrete mesh/light/decal proxy storage.

#### Frame ordering contract

The current frame phases are sufficient for a first integration:

```text
PlatformEvents
Input
BeginFrame
PreSimulation
FixedSimulation
Simulation
WorldStreaming
PostSimulation
Presentation
Render
EndFrame
```

RenderScene should not execute rendering from `Presentation`. Presentation owns window attachments, render viewports, swap
chains, HDR/display policy, acquire/present, and resize reconciliation. The render-facing scene update should instead sit around
the `Render` phase, with explicit dependencies on the systems that produce mutations. The first CPU-only implementation can use
this order:

```text
Simulation / WorldStreaming
    GameWorld and CellStreamingSystem commit entity/resource lifecycle changes

PostSimulation
    WorldRenderBridge captures committed component/entity changes and queues RenderScene mutations

Render
    RenderSceneFramePrepare drains mutation ingress under budget
    RenderSceneCommit publishes the next coherent SceneVersion/SpatialVersion
    RenderSceneCollect dispatches visibility/collection jobs for requested views

EndFrame
    RenderSceneEndFrame publishes feedback, retires completed CPU state, and reports stats
```

The exact participant IDs can be chosen during implementation, but the dependency direction is non-negotiable: world and bridge
mutation production before scene publication; scene publication before collection; collection completion before any later batcher
or render graph consumes output pages.

#### Gaps that remain before implementation

The cross-check did not find a blocking dependency for a CPU RenderScene. It did find boundaries that must stay explicit:

- There is no RenderScene service yet. The first code phase must create this service and register it with the engine host.
- There is no camera service or camera custom-data system yet. The first collector should accept a plain `ViewDescription` or
  `ViewCollectionRequest`; full camera ownership can be layered later.
- There is no RenderBatcher or RenderGraph yet. Collector output pages should be testable CPU products and should not assume a
  final draw-submission format.
- There is no renderer residency layer yet. Mesh/material/texture resources can be pinned by resource handles, while GPU
  upload/readiness remains a later RHI/batcher boundary.
- The existing frame pipeline is main-thread participant based. RenderScene may use main-thread participants to schedule work,
  but actual culling/collection fan-out should happen through `jobs::Builder` with `Priority::RenderPath`.
- The materializer is serialized by the game-world synchronization owner. RenderScene must not call materializer operations; it
  consumes the already-committed results through the bridge.

#### Implementation green light

The first RenderScene implementation can start without waiting for RHI, cameras, batching, or render graph. The safe first scope
is CPU-only:

1. create the `RenderingService`-owned `RenderSceneManager`, generational scene/proxy handles, stats, failure records, and
   rendering pools;
2. implement proxy creation, mutation admission, destruction, and scene-version publication with no Flecs dependency;
3. implement basic immutable read leases and lifetime pinning;
4. add typed payload pools for initial mesh/light/decal descriptors, with resource references/handles but no GPU residency;
5. implement simple spatial membership and a view collection request that emits typed CPU collector pages;
6. add tests for handle generation, mutation coalescing, read-lease retirement, resource-handle retention, and job-token
   completion.

The world bridge remains the producer layer above this CPU core. World systems produce renderer mutation intent, RenderScene
publishes immutable scene state, and later render-path jobs produce compact GPU Scene candidates without importing batching or
graph-execution policy into RenderScene.

## GPU-driven architecture reevaluation

This checkpoint supersedes both the original complete per-view CPU packet path and the later proposal to repair it with
copy-on-write scene versions. RED does neither. Its RenderScene owns stable live proxies and a live VisGrid; safety comes from
bounded double-buffered update queues, render-command ownership, and explicit Jobs ordering. Vanguard will mirror that model
while its persistent GPU Scene receives sparse changes and views move compact persistent indices through visibility,
classification, batching, and indirect execution.

The audit found that Phases 0 through 8 exist in code and tests. Phases 9 and 10 were not implemented. Runtime and editor now
register `RenderingService`, which owns `RenderSceneManager` and drives its dense scene directory through the live frame loop.

### Required producer contract

RenderScene is responsible for producing only these GPU Scene inputs:

- sparse create, update, and retirement intent for mutable instances, lights, and decals;
- stable private mappings from exact proxy generations to generational GPU Scene handles;
- references to renderer-resolved immutable renderable/material definitions, without owning their GPU allocation policy;
- compact `GpuInstanceIndex` candidates written directly into caller-owned per-view reservations after CPU spatial broad phase;
- scene publication and mutation epochs that let later renderer work correlate CPU and GPU publications.

RenderScene must not select final LODs, expand render phases, choose pipelines, build material bindings, sort draw work, emit
indirect arguments, allocate graph resources, record RHI commands, or submit GPU work. Camera storage supplies `RenderView` /
`GpuView`; mesh residency supplies resident geometry and LOD fallback facts; the Render Graph owns transient resources and
execution.

### RED scene-update mechanism to mirror

The implementation reference is `renderer/src/renderScene.h/.cpp`, `renderer/src/renderProxyDrawable.cpp`,
`renderer/src/renderCommandHandler.cpp`, and `visibility/include|src/visVolumeHierarchyGrid.*`:

1. `ScheduleRelink` rejects requests that change neither transform nor bounds, then appends a complete retained relink request
   to one of two preallocated bounded arrays using an atomic count under a shared index lock.
2. `PrepareForUpdate` takes the index lock briefly, swaps producer and processing buffers, and atomically extracts the request
   count. Producers immediately continue in the other buffer.
3. `ExecuteUpdateState` scans requests newest-first and removes duplicate proxy relinks, then dispatches unique requests across
   render-path Jobs. A proxy updates its live transform, bounds, motion/skinning state, and other dependent state in place.
4. A collectable proxy asks `QuickConditionalMoveObject` to update object bounds immediately. If the new bounds can invalidate
   the cell's aggregate coverage, the proxy is appended to a small structural-move batch instead of moving the VisGrid from the
   parallel relink job.
5. Structural add/remove/move operations enter the render-command owner. It merges repeated operations for the same proxy,
   orders removals and moves before additions, processes the affected proxies, and calls `RebalanceTree` once to repair dirty
   cells.
6. The render command chain waits for the previous frame's command processing, flushes scene commands, executes scene update
   jobs, and only then permits render collection. Stable retained proxy references keep objects alive through queued work; RED
   does not create an immutable copy of every proxy for each frame.

Vanguard should preserve those behaviors with Vanguard handles, pools, redJobs, the existing configurable VisGrid, and sparse
GPU Scene change emission. RED-specific raw proxy pointers and global renderer access are not copied into Vanguard's public API.

### Audit decisions

| Existing area | Decision | Required change |
| --- | --- | --- |
| Scene/service ownership and generational scene handles | Keep | Register through runtime/editor composition only after the revised frame participants exist. |
| Generational proxy lifecycle and change-driven world bridge | Keep and adapt | Add private GPU identity and dirty-category state; preserve exact ECS change capture and coalescing. |
| Prepare/commit publication boundary and read leases | Removed | Stable renderer-owned proxies now advance through an ordered update job chain. Complete versions, read leases, retirement lists, synchronous commit wrappers, and broad proxy snapshots no longer exist. |
| Typed mesh/light/decal CPU pools | Keep and adapt | They remain authoritative CPU state. Mesh payloads resolve to shared renderable definitions; light/decal payloads produce sparse persistent table updates. Do not duplicate strong resource handles into every published version. |
| Spatial write index and dirty-cell repair | Keep | It is the CPU broad phase and must remain change-driven. Add stable GPU identities to published spatial membership. |
| Spatial query semantics | Keep and adapt | Like RED, query the live VisGrid only after scene-update jobs complete. Queries borrow only bounds, masks, flags, and payload kind from authoritative proxy storage and return compact handles without copying proxy state. |
| `RenderSceneCollector` Jobs fan-out | Removed | Future candidate production writes compact persistent GPU indices and is designed against the GPU Scene contract. |
| `MeshCollectorPacket`, `LightCollectorPacket`, `DecalCollectorPacket`, packet pages, and final packet reduction | Removed | They copied full proxy/payload state through temporary pages. GPU-driven rendering needs stable indices, not duplicated draw payloads. |
| CPU `selectedLod`, `dissolve`, and per-packet distance state | Remove from the primary runtime path | LOD selection and draw classification belong to GPU work once mesh residency and camera policy exist. Any editor/tool fallback must be explicitly separate. |
| Collector-owned `RenderSceneViewHandle` registry | Removed | Camera/view storage will own view identity. RenderScene must not invent a parallel camera identity system. |
| Visibility feedback probes | Reintroduced independently | `VisibilityFeedbackService` uses current generational view/family identities and explicit frame-scoped frusta without collector, proxy, or camera ownership. |
| `RenderSceneFrameLifecycle` | Removed | Frame ordering will be composed by the later rendering command/service layer rather than a collector/version retirement wrapper. |
| Broad proxy and typed snapshot APIs | Removed | GPU publication and visibility use private borrowed reads. Future tooling must introduce purpose-specific inspection contracts rather than restoring complete proxy or payload copies. |
| Original Phase 9 frame-pipeline plan | Redesign before implementation | Do not wire the existing full-copy commit and CPU packet collector into the engine loop. |
| Original Phase 10 generic extension seam | Supersede | Replace it with the explicit GPU Scene publication and candidate-production boundary described below. |

### Concrete hot-path removals and adaptations

The following audit records the removed snapshot path:

1. `CommitScene` previously scanned every proxy slot and rebuilt complete proxy, payload, lookup, spatial-cell, and membership
   arrays. That runtime image has been removed. The active path now retains stable live proxies, swaps bounded relink buffers
   once per tick, applies only newest unique changes, and exposes `PrepareSceneUpdate`/`ExecuteSceneUpdate` for Jobs ordering.
   The compatibility versions and leases have also been deleted; queries validate an exact completed mutation epoch.
2. `RenderSceneCollector::CollectBatch` first builds a temporary `RenderProxyHandle` array, then resolves a full base snapshot,
   then resolves a full typed snapshot, then copies the resulting packet into a job-local page. `Finalize` copies those packets
   again into final arrays. The revised candidate path writes only stable `GpuInstanceIndex` values directly into the reserved
   output span. Light and decal candidate streams may use their own compact persistent indices when their GPU processing path
   is defined.
3. `ViewProxyState::selectedLod` and `dissolve` currently imply CPU ownership of decisions that belong to future GPU LOD and
   transition policy. They must not become persistent runtime contracts. CPU tools needing preview LOD selection should use a
   separate opt-in utility.
4. Visibility feedback no longer piggybacks on collector registration or collection completion. It consumes explicit
   camera-supplied view identities/frusta, evaluates dense disjoint probe ranges into an unpublished bank, and publishes only
   after the caller's Jobs dependency joins.
5. Transform mutation ingress now uses fixed-capacity double-buffered relink requests: producer/consumer buffers swap under a
   short index lock, duplicates are removed newest-first through paged direct-address stamps without hashing or transient
   allocation, unique proxies update in parallel, and only structural VisGrid moves enter the serialized epilogue. The
   `RenderCommandSystem` now owns the CPU ordering tail; generalized typed add/remove/property ingress remains later work.
6. GPU publication no longer resolves broad base/light/decal snapshots. It freezes retained dirty-index slices, subdivides the
   serial stream into bounded ranges, and lets disjoint workers read narrow borrowed fields from sealed proxy storage while
   constructing final GPU ABI objects directly in mapped upload reservations.

### Revised continuation phases

These phases replace the unimplemented original Phases 9 and 10. They deliberately stop before camera, mesh residency and
Render Graph work. The command-chain and engine ownership shells now exist, but do not yet pretend to be the renderer.

1. **RED-style scene update core — implemented:** remove complete runtime scene versions and implement bounded double-buffered relink queues,
   newest-request-wins duplicate removal, parallel proxy relink jobs, quick conditional VisGrid updates, batched structural
   moves, and one dirty-cell rebalance after removals/moves. Expose an update completion dependency rather than read leases.
2. **GPU identity and mutation boundary — implemented:** add private proxy-to-GPU-handle mappings and emit coalesced instance/light/decal
   sparse changes from the same final relink operations. Resource-definition resolution and retirement remain explicit. This
   phase emits plans only and performs no RHI work.
3. **Direct candidate production — implemented:** replace typed runtime collector packets with Jobs producers that query the live post-update
   VisGrid and write compact persistent indices directly into `GpuVisibilityCandidateReservation` spans. Preserve deterministic
   capacity and overflow reporting.
4. **Feedback separation — implemented:** `VisibilityFeedbackService` owns scene-relative generational probes outside proxy,
   payload, spatial, camera, and collector storage. Callers provide explicit frame-scoped `RenderViewId`, family, frustum,
   query-mask, and streaming-authority inputs. Disjoint Jobs batches write an unpublished feedback bank; completion verifies
   every batch and flips the bank atomically at the phase boundary. Cancellation cannot expose partial results and no GPU
   readback is required.
5. **Command-chain and service ownership shell — implemented:** `RenderCommandSystem` owns one CPU rendering tail. Its
   `FrameTick` appends unique live-scene updates and its `RenderFrame` appends retained viewport frames to that same chain;
   `FlushPreviousFrameProcessing` waits only this CPU chain. `RenderingService` owns the command system and viewport manager,
   and runtime/editor composition registers both Render Scene and Rendering services. No bridge is hard-coded into the generic
   service. The private frame dispatcher constructs a renderer continuation context inside the retained frame job, matching
   the RED command-handler boundary without adding another tail. The service destination currently fails closed because no
   Render Graph executor is installed. Automatic frame-participant ingress ordering, camera/frame publication, and the graph
   executor remain the next integration step.

### RED-to-Vanguard frame insertion contract

The command shell deliberately preserves the same ownership split as RED while leaving renderer policy unimplemented:

1. The future engine Render-phase driver owns the normal-frame CPU boundary. It first calls
   `FlushPreviousFrameProcessing`, then asks registered producers to finish mutation ingress and gather the live scenes for
   the tick. `WorldRenderBridge` is one producer, not a hard-coded `RenderingService` dependency.
2. `RenderCommandSystem::FrameTick` performs main-thread scene preparation, appends scene-update jobs, and finally enters
   `RenderingService::FrameTick(RenderFrameTickContext&)`. GPU Scene publication, fence-based retirement, renderer-global
   streaming decisions, and other tick-wide work extend this same chain through `RenderFrameTickContext::Jobs()`. If later
   renderer state requires RED-style synchronous `FrameTickPrepare`, its callback belongs at the start of this function,
   before `PrepareSceneUpdate`; it must not be hidden inside the asynchronous continuation.
3. `ViewportManager::BeginFrame` creates the frame identity and snapshots output policy and dimensions. Camera requests,
   scene references, view-family requests, and retained frame-owned storage are populated after this point and sealed before
   submission; camera-derived renderer data is not allocated here.
4. `ViewportManager::SubmitFrame` transfers the sealed frame to `RenderCommandSystem`. When output rendering is connected,
   this main-thread boundary also acquires the exact output image and retains that acquisition in the frame packet. Dispatch
   failure must abandon it immediately.
5. The private `RenderFrameDispatcher` retains the packet, appends one root job behind the shared CPU tail, and constructs
   `RenderFrameContext` from that job's continuation context. There is no second renderer queue or frame-local CPU tail.
   Future generic renderer-command ingress is drained into the dispatcher builder before this root job, matching RED's queue
   flush location. World mutation ingress remains before `FrameTick` and does not enter this command queue.
6. `RenderingService::RenderFrame(RenderFrameContext&)` is the equivalent of RED's `CRenderInterface::RenderFrame`. Camera
   storage allocation and derived views occur first, followed by the graph key/cache lookup and graph construction. Scene
   custom data is prepared once per frame; camera custom data is prepared once per derived camera/view.
7. GPU Scene uploads, candidate production, GPU visibility, phase batching, indirect generation, command recording, queue
   submission, and graph cleanup are Render Graph work appended through `RenderFrameContext::Jobs()`. The context exposes the
   dispatcher-thread index needed by thread-affine renderer/RHI scratch state.
8. Presentation completion and viewport bookkeeping return to a main-thread completion stage. GPU resources retire from RHI
   fences during later frame ticks; `FlushPreviousFrameProcessing` remains a CPU-chain boundary and never becomes a per-frame
   GPU idle wait.

After this checkpoint, work should move to mesh global buffers/residency, camera storage, camera/scene custom data, and the
Render Graph. GPU LOD selection, phase expansion, batching, and indirect generation resume only when those dependencies provide
real contracts.

## RenderScene execution phases

The following is the original phase history. It remains useful as an implementation inventory, but every phase is now governed
by the GPU-driven decision recorded above and beside it. The revised continuation phases, not the original Phases 9 and 10,
are the active roadmap.

### Phase 0: module boundary and service shell

Goal: make RenderScene a real engine subsystem without adding renderer policy.

Status: implemented and consolidated. `RenderingService` owns the manager and runtime/editor application composition is live.

Add:

- public rendering headers for scene handles, failure codes, stats, service descriptors, and basic creation settings;
- `RenderingService` registered through the engine host;
- `RenderSceneManager` owned directly by `RenderingService`;
- deterministic scene-slot allocation with generational `RenderSceneHandle`;
- explicit runtime/editor/preview/thumbnail scene creation modes;
- rendering memory-pool use from the first allocation site;
- tests for service lifecycle, scene handle generation, duplicate shutdown, and scene-slot reuse.

The service participates in the engine, but this phase does not yet register a frame participant. It proves ownership and
lifetime first. No Flecs, no RHI, no cameras, no presentation.

Done when:

- a runtime or editor app can create and destroy a named render scene through the service;
- stale scene handles fail cleanly;
- shutdown refuses live scenes or drains them through an explicit policy;
- all hot scene objects allocate through rendering pools.

### Phase 1: proxy identity and mutation ingress

Goal: add the producer-facing RenderProxy lifecycle without spatial indexing or collection.

Status: implemented and retained with adaptation required. Proxy slots need private GPU Scene identities and coalesced dirty
categories, but producer-facing generational lifecycle semantics remain correct.

Add:

- generational `RenderProxyHandle`;
- proxy base records: stable proxy identity, owning scene, lifecycle state, transform, bounds, visibility flags, layer/mask bits,
  type id, producer generation, and debug name;
- call-borrowed creation descriptors that are synchronously internalized;
- allocation-free mutation admission accounting for create, destroy, visibility, layer/mask, user-data, and typed payload updates;
- admission budgets and overflow reporting;
- explicit results for invalid handle, stale generation, wrong scene, duplicate create, pending destroy, and capacity exceeded;
- tests for stale proxy handles, create/destroy coalescing, mutation ordering, budget limits, and shutdown drain.

This phase is still single-threaded internally. It builds the command shape and lifecycle table before adding publication and
jobs. Producers get a handle, but readers cannot yet acquire a scene version.

Done when:

- proxy handles survive slot reuse safely;
- repeated create/update/destroy requests reduce to deterministic lifecycle transitions;
- invalid transitions are reported instead of asserted or silently repaired;
- the public proxy interface is generic and does not expose mesh/light/decal-specific methods.

### Phase 2: scene publication and read leases

Goal: split mutable ingress from immutable published scene state.

Status: implemented, but superseded for the runtime hot path. RED does not publish complete immutable scene versions. Replace
runtime read leases and full snapshots with stable live proxies, command-owned mutation, and a Jobs dependency that makes
collection run after scene updates. Retain on-demand snapshots only for tooling, validation, and tests.

Add:

- `SceneVersion` and mutation epoch counters;
- `RenderSceneFramePrepare` to drain and validate ingress pages;
- `RenderSceneCommit` to publish a coherent scene version;
- `SceneReadLease` with reader epoch pinning;
- retired-version queues that cannot reclaim records while a lease is alive;
- completion tokens for publish work, even if the first implementation completes synchronously;
- tests for read-while-write safety, stale lease rejection, proxy retirement under readers, and version monotonicity.

This phase is the replacement for a global `BeginRendering` / `EndRendering` Boolean. It allows runtime, editor, preview, and
thumbnail views to read a stable scene without blocking unrelated producers.

Done when:

- readers can acquire latest or exact completed scene versions;
- mutations after lease acquisition are invisible to that lease;
- proxy destruction cannot invalidate data pinned by an outstanding lease;
- service shutdown can prove no readers remain, or fail with a clear live-reader diagnostic.

### Phase 3: typed payload pools

Goal: add representative renderer payload families without turning the base proxy into a giant inheritance tree.

Status: implemented and retained with adaptation required. The pools remain CPU-authoritative, while their runtime publication
changes from copied per-version/per-view snapshots to sparse GPU Scene mutation and shared-definition references.

Add:

- `MeshProxyPayload`, `LightProxyPayload`, and `DecalProxyPayload` pools;
- typed creation descriptors for static mesh, light, and decal proxy records;
- resource identity fields using `resources::ResourceReference`;
- optional strong `resources::ResourceHandle` retention for already-loaded resources;
- payload generation checks tied to the base proxy generation;
- typed in-place payload updates for resource replacement and common authored property changes;
- tests for payload attach/detach, resource-handle retention, stale payload generation, and cross-type rejection.

This phase does not upload anything to GPU memory. Mesh/material/texture handles mean the CPU scene knows what resource is
referenced and can keep loaded objects alive when necessary. Renderer residency remains a later layer.

Done when:

- base proxy records are small and type-neutral;
- mesh, light, and decal payloads live in separate dense pools;
- a stale mesh handle cannot mutate a light payload slot;
- resource handles are retained/released deterministically across scene publication and retirement.

### Phase 4: spatial write index

Goal: make scene membership spatially queryable while preserving single-writer publication.

Status: implemented as the first CPU write-side spatial adapter. It uses sparse hashed 3D cells, private per-proxy spatial
entry metadata, explicit finite-extent policy, dirty-cell repair during frame preparation, empty-cell recycling, an explicit
overflow lane for retained out-of-range proxies, and validation/stat counters. Immutable spatial snapshots and visibility
queries are provided by Phase 5.

GPU-driven decision: retain and align more closely with RED. Relinks first update proxy-local bounds and attempt the VisGrid
quick conditional move; only moves that can invalidate cell coverage enter the structural move queue. Remove/move batches
repair dirty cell bounds once before later collection.

Add:

- `SpatialWriteIndex` owned by `RenderScene`;
- configurable world-origin, cell/page size, and extent policy without hardcoding one game scale;
- insert, remove, fast in-place move, and structural move paths;
- dirty-cell/page repair after a mutation batch, not after every operation;
- private spatial-entry handles stored as scene metadata, not public ABI;
- spatial stats and debug validation;
- tests for insertion, removal, fast movement, structural movement, dirty repair, and out-of-range policy.

The first implementation can be conservative. It should optimize for correctness, stable publication, and easy replacement with
a more advanced page tree later. The API must not assume a 2D-only grid or a fixed city-sized world.

Done when:

- every published proxy with spatial membership can be found by its bounds;
- moving a proxy never exposes a published version with stale aggregate bounds;
- spatial metadata is invisible to producers;
- dirty repair is amortized per commit.

### Phase 5: spatial read leases and visibility queries

Goal: let views query immutable scene/spatial versions safely.

Status: CPU visibility-query foundation implemented. Committed scene versions publish flat spatial cell ranges, one contiguous
proxy-membership array, and dense generation-checked proxy lookup tables. `SceneReadLease` is a uniquely registered reader
capability rather than a version/count hint, and publication storage is protected by a reader/writer lock so later Jobs readers
cannot race version reallocation or retirement. Collection supports bounds, frustum planes, layer masks, visibility masks and
flags, payload-family filtering, deterministic version-bound batches, bounded whole-query results, and the out-of-range overflow
lane. Per-view feedback and Jobs fan-out remain later phases.

The current synchronous staging model enforces an exact prepare/commit epoch: once `PrepareSceneFrame` succeeds, mutations and
duplicate preparation are rejected with `Busy` until `CommitScene` publishes that captured state. This prevents a newer mutable
proxy state from being published under an older mutation epoch or paired with unrepaired spatial aggregates. A later
multi-producer ingress implementation may replace this temporary freeze with buffer flipping, but it must preserve the same
publication boundary.

GPU-driven decision: retain the CPU broad-phase semantics but remove immutable scene replicas from the hot path. Queries read
the stable live VisGrid after the scene-update dependency and write compact GPU Scene indices directly. Full snapshot reads
become cold tooling/diagnostic operations.

Add:

- `SpatialReadLease` included inside `SceneReadLease`;
- `VisibilityQuery` / `ViewCollectionRequest` with frustum, layer mask, visibility role mask, camera position, LOD policy input,
  and feedback policy input;
- query-local candidate buffers allocated from rendering/frame pools;
- exact version binding between query, scene lease, and spatial lease;
- tests for view-mask filtering, layer filtering, bounds/frustum filtering, query lifetime, and multiple concurrent views.

This phase still does not emit draw packets. It proves that view queries can find candidate proxy records from an immutable
scene version. Query objects cannot escape their lease.

Done when:

- multiple views can query the same scene version concurrently;
- query results are deterministic for a fixed scene version;
- query storage is reclaimed only after the owning query/lease completes;
- no query reads mutable staging storage.

### Phase 6: view-local state and CPU collector pages

Goal: convert visibility candidates into typed per-view renderer inputs without mutating published proxies.

Status: implemented as the CPU collector foundation. `RenderSceneCollector` owns generational view identities, retains the exact
scene version used by each collection, partitions its immutable spatial snapshot into deterministic Jobs `RenderPath` work,
and gives every batch private fixed-capacity mesh/light/decal packet pages. A dependent epilogue reduces those pages in batch
order, publishes per-view proxy history, releases the internally retained scene lease, and completes the exposed Jobs counter.
Dispatch itself does not wait. Packet budgets produce explicit overflow counts in every build configuration, while visibility
history is still updated for packets omitted from bounded output. One collection may mutate a view's state at a time; different
views over the same scene version remain independent.

GPU-driven decision: superseded for the primary runtime path. Do not extend the typed packet/page/reduction design. Replace it
with direct candidate-index production; move view ownership to the future camera system and remove CPU LOD/dissolve ownership.

Add:

- `ViewProxyState` indexed by view identity and generational proxy identity;
- frame trackers, cached distance, initial LOD/dissolve placeholders, and feature state slots;
- job-local `CollectorOutputPages`;
- mesh, light, and decal collector functions over dense payload pools;
- deterministic reduction of job-local pages into one view output;
- overflow diagnostics that are reported in every build configuration;
- tests for collector determinism, page overflow, per-view state isolation, and no mutation of published payloads.

Collectors should be CPU-only and bindless-agnostic. Mesh collection emits mesh packets, light collection emits light records,
and decal collection emits decal packets. None of this chooses a final material binding strategy.

Done when:

- two viewports can collect different outputs from the same scene version;
- collection can fan out through Jobs `Priority::RenderPath`;
- collection completion is represented by a `jobs::Counter`;
- no collector blocks the game loop.

### Phase 7: feedback separation

Goal: retain conservative streaming visibility without restoring collector ownership or fake render proxies.

Status: implemented on the revised GPU-driven boundary. `VisibilityFeedbackService` is bound to one scene but owns its own
generational non-renderable probes and dense active-probe directory. Probes are never inserted into RenderScene proxy/payload or
spatial storage. Evaluation consumes explicit frame-scoped view descriptions using current `RenderViewId` and
`RenderViewFamilyId` identities; it does not register or own cameras.

Planning partitions the dense probe directory into disjoint caller-owned Jobs batches. Each batch writes only its probe range
into the unpublished feedback bank. `CompleteEvaluation` verifies every exact prepared range before flipping banks, while
`CancelEvaluation` discards the unpublished bank. There is no shared append cursor, per-probe atomic, observation array,
collector reduction, full-capacity scan, or mandatory GPU readback.

View-policy routing is direct: specific views use a stamped index table, families use per-family linked ordinals, and
streaming-authority views use a dense ordinal list. Evaluation cost is therefore proportional to active probes plus the views
actually selected by each probe policy, rather than active probes multiplied by every submitted view.

One feedback service attaches to its owning scene. The attachment is only a lifetime edge: it exposes no probe data to
RenderScene, and scene destruction is rejected until the service has destroyed its probes and detached.

Feedback is generation-, descriptor-, mutation-epoch-, frame-, and view-set-versioned. A probe is `Visible` when any matching
view intersects its bounds, `NotVisible` when at least one matching view evaluated it and all rejected it, and `Unknown` when
no view matches its specific-view, family, or streaming-authority policy. Unknown feedback retains the last genuine evaluation
frame so consumers can measure age and remain conservative.

Add:

- `VisibilityFeedbackService` for query-only probes and conservative CPU feedback;
- explicit view/family/streaming-authority policies over current renderer view identities;
- versioned tri-state feedback: unknown, visible, not visible;
- double-buffered publication with verified disjoint evaluation batches and cancellation;
- tests for visible/not-visible classification, open-evaluation read rejection, and cancellation isolation.

This is where visibility can feed world streaming decisions later. Streaming policy remains outside both RenderScene and the
feedback service; the service only reports what the selected view policy conservatively observed.

Done when:

- query-only objects are not modeled as drawable proxies;
- feedback names the scene mutation epoch, view-set revision, view policy, published frame, and evaluation age;
- parallel evaluation has no shared output cursor or serial probe reduction;
- an empty view set deterministically publishes `Unknown` without losing the last real evaluation age.

### Phase 8: world bridge integration

Goal: connect committed world/entity changes to RenderScene without making RenderScene depend on Flecs.

Status: implemented at the explicit world/render boundary. The ECS world publishes a bounded sequence-numbered journal for
successful entity and component commits, with independent subscriber cursors, overrun reporting, command-batch identity, and a
minimal native-observer capture entry point. `WorldRenderBridge` lives in the entities integration module so dependency flow is
`entities -> rendering`; RenderScene has no Flecs dependency. Project-defined contributor/state descriptors translate mesh,
light, decal, transform/bounds, visibility, layer, and user-data changes after world commit. Changes coalesce by stable entity
and component, all translations complete before scene mutation begins, transaction capacity is preflighted against the exact
final contributor counts and mutation count, detachments run before admissions, and any unexpected scene rejection forbids
publication and requires a rebuild. Session generations discard retained work from previous worlds. Destroyed proxies are
acknowledged only after no published scene version retains their exact generation. A cold full-world validator compares
tracked contributor components against Flecs state without entering the normal frame path. Frame-pipeline ownership and
automatic invocation remain Phase 9.

GPU-driven decision: retain and adapt. Exact change capture, coalescing, and session invalidation are correct. Detach safety
will follow RED's retained command/update jobs plus Vanguard's GPU Scene retirement fences; it no longer waits for copied CPU
scene versions.

Add:

- `WorldRenderBridge` as a per-world/session adapter;
- component-to-proxy registration descriptors for renderable mesh, light, decal, and transform components;
- changed-component capture streams for transform, bounds, visibility, payload, and destruction changes;
- bridge generation tied to world/session switches;
- activation-group transactional proxy admission;
- proxy release synchronized with cell/entity release;
- resource/request readiness propagation back to the streaming executor where appropriate;
- tests using the existing GameWorld and CellStreamingSystem path;
- debug-only/full-rebuild validation that can compare the dirty stream against world state without being part of the frame hot
  path.

This phase must preserve the existing materialization rule: world changes are committed first, then the bridge observes/captures
them, then RenderScene publishes renderer-visible changes. Partial activation groups must not become partially visible.

The bridge is change-driven. It must not query every Flecs table every frame and then decide whether each entity needs a relink.
For a transform update, the expected path is:

```text
Transform component written
    -> component/store revision or observer marks entity/proxy dirty
    -> WorldRenderBridge captures only dirty transform identities for the current bridge epoch
    -> latest transform/bounds per proxy coalesces into one relink packet
    -> RenderSceneCommit applies only changed proxies and repairs only affected spatial pages
```

Full scans are allowed only for cold initial import, scene rebuild, hot-reload recovery, editor validation, or corruption
diagnostics. They are not a normal runtime frame mechanism.

Flecs can help, but it is not the whole contract. Flecs cached queries support change detection through
`EcsQueryDetectChanges`, `ecs_query_changed`, `ecs_iter_changed`, and `ecs_iter_skip`. This tracks whether a matched table/result
changed since the query last consumed it. It is useful for skipping completely untouched tables, and for validation/debug
queries, but it is table-granularity change detection, not per-entity value checksumming. If one transform changed in a large
table, a changed Flecs iterator result can still contain the whole table.

Vanguard should therefore use two layers:

```text
Primary runtime path
    Vanguard ECS committed-component-action journal
        -> exact stable entity IDs touched by Add/Set/Remove/Enable/Disable
        -> bridge maps those IDs to render proxy handles
        -> coalesced relink/payload/visibility packets

Secondary table gate / validation path
    Flecs cached query with EcsQueryDetectChanges
        -> skip untouched archetype tables
        -> validate or rebuild dirty streams when needed
```

The existing Vanguard ECS wrapper already queues component actions before `FlushComponentActions`, which is the right place to
publish an exact committed-change journal. The current wrapper reports only aggregate counts, so Phase 8 must add a retained
change-journal or subscriber boundary for systems like `WorldRenderBridge`.

Flecs `OnSet`, `OnAdd`, and `OnRemove` observers may also be used as exact entity-level dirty markers, especially for native
Flecs systems that intentionally bypass Vanguard's queued component API. Observer callbacks must remain tiny: copy the stable
entity ID, component ID, event kind, and revision into a bridge-owned dirty stream, then return. They must not create render
proxies, relink spatial state, wait on jobs, allocate heavyweight payloads, or call back into RenderScene mutation commit while
Flecs is applying changes. An `OnSet` event means the component was set, not that its value is byte-different from the previous
value, so same-value writes may still produce dirty entries and should be coalesced by the bridge epoch.

Recommended runtime priority:

```text
1. Vanguard committed component journal for engine-authored writes
2. Flecs OnSet/OnAdd/OnRemove observers as exact dirty markers for native Flecs writes
3. Flecs query change detection as a table-level skip/validation aid
4. Full scans only for rebuild/recovery/debug validation
```

Done when:

- loading a streamed cell can materialize entities and queue matching render proxies;
- transform changes on N entities enqueue O(N changed) relinks, not O(total renderable entities) checks;
- repeated transform writes to one entity in one bridge epoch produce one final relink packet;
- a changed Flecs table does not force relinking every entity in that table unless the engine is explicitly rebuilding or
  validating;
- `FlushComponentActions` or an equivalent committed-change boundary can expose exact changed stable entity IDs to the bridge;
- Flecs observers, when enabled, only append dirty identities and never perform RenderScene work inside the observer callback;
- releasing a cell removes proxies through the scene lifecycle and acknowledges downstream release only after safe detachment;
- world switch/session generation invalidates stale queued bridge work;
- preview scenes still work without a world bridge.

### Phase 9: frame-pipeline wiring

Goal: make the CPU RenderScene lifecycle run automatically in engine frames.

Status: not implemented. The original plan is superseded and must not be implemented over the current full-copy publication and
typed CPU collector. Use the revised continuation phases in the reevaluation section.

Add:

- frame participants for bridge capture, scene prepare/commit, collection scheduling, and end-frame retirement;
- participant dependencies against existing `GameWorldFrameParticipantId`, streaming observer work, presentation readiness where
  needed, and future renderer participants;
- profile filtering for runtime, editor, preview, tool, and headless modes;
- service stats visible to diagnostics/editor panels;
- tests for frame order, participant dependency errors, no-scene frames, stopped worlds, and shutdown during pending work.

This phase is not the full RenderingService. It wires the CPU scene lifecycle into the engine loop so later camera, batcher,
render graph, and GPU submission systems have a stable place to attach.

Done when:

- a running world can tick, publish a scene version, collect zero or more views, and retire safely every frame;
- editor/runtime profiles can enable or disable scene participants without code forks;
- frame failures identify the responsible participant and scene;
- no presentation path owns RenderScene execution.

### Phase 10: renderer-facing extension seam

Goal: prepare for RenderScene consumers without implementing them prematurely.

Status: not implemented and superseded. The renderer-facing seam is now explicitly the GPU Scene mutation/publication contract
plus direct candidate reservations, rather than generic typed collector outputs.

Add:

- stable collector-output readers for future RenderBatcher;
- placeholder interfaces for camera custom-data preparation and scene custom-data reducers;
- explicit hooks where renderer residency, GPU scene upload, occlusion feedback, shadow scheduling, and dynamic decal reducers
  will attach;
- tests that prove these hooks can be absent without breaking CPU scene operation.

This is the checkpoint before moving into camera, RenderScene custom data, RenderBatcher, RenderGraph, and GPU-driven rendering.
The core scene must remain useful without those systems.

Done when:

- RenderScene can expose typed CPU outputs to a future batcher without leaking internal arrays;
- missing optional renderer extensions produce empty outputs, not failures;
- extension hooks are versioned and generation-safe;
- the engine can run a blank/no-render frame with RenderScene enabled.

## Provisional Vanguard shape

This is not final API. It records only conclusions already supported by reviewed chunks.

```text
RenderingService
    major engine service that owns RenderSceneManager, renderer/device lifetime,
    frame-pipeline hooks, command serialization, viewports, and GPU Scene publication

RenderSceneManager
    owns scene slots and generational RenderSceneHandle values

IRenderScene
    narrow scene lifecycle, mutation-ingress, update, and read-view contract

RenderProxyHandle
    producer-side attachment and mutation façade

RenderScene
    renderer-owned stable live proxy storage, spatial membership, and update buffers

RenderSceneUpdateQueue
    two bounded preallocated relink buffers with atomic producer admission and newest-wins reduction

ProxyRelinkMetadata
    paged per-proxy generation, admission-closed state, outstanding-request count, and deduplication stamp

SpatialIndex
    live VisGrid with quick conditional moves, serialized structural operations, and dirty-cell repair

RenderSceneUpdateDependency
    redJobs completion dependency covering command flush, relinks, structural VisGrid work, and repair

VisibilityQuery
    frame-scoped view policy consuming the completed live scene epoch

VisibilityFeedbackService
    owns generational non-renderable probes and publishes versioned per-view feedback

ViewCollectionState
    frame/view-local camera and feature state owned outside shared RenderScene proxy storage

MeshProxyPayload, LightProxyPayload, DecalProxyPayload
    typed renderer-owned live proxy payload pools with explicit update access behavior

GpuSceneCandidateProducer
    Jobs traversal writing compact persistent indices directly into caller-owned reservations

GpuSceneChangePlan
    sparse instance/light/decal changes emitted from the final scene update operations

RenderSceneFramePrepare
    swaps update buffers and captures the bounded relink epoch

RenderSceneUpdate
    removes duplicate relinks, applies proxy-local updates in parallel, commits structural spatial work, and signals completion

RenderSceneCollect
    dispatches candidate production after RenderSceneUpdateDependency

RenderSceneEndFrame
    publishes visibility feedback and retires retained proxy/update work after dependent tokens complete

WorldRenderBridge
    consumes committed Flecs/world transactions, owns proxy lifecycle records,
    budgets admission, and publishes scene mutation epochs
```

One world scene may be consumed by multiple engine viewports. Preview and thumbnail scenes may exist without Flecs or a
runtime world. No viewport owns a Render Scene.

## Decision log

| Decision | State | Evidence |
|---|---|---|
| Render Scene is independent from viewport/presentation | Accepted | RED scene kinds and existing Vanguard viewport/output separation. |
| Flecs integration is an adapter, not a Render Scene dependency | Accepted | RED's runtime-system/proxy-handle boundary and Vanguard's transactional materializer. |
| Render Scene public API contains no NVRHI types | Accepted | Existing Vanguard RHI boundary and the scene's CPU-side responsibility. |
| Public identity uses generational handles | Accepted | Chunks 02, 08, and 09 show queued relink, command, admission, destruction, and world-switch work can outlive the producer identity that emitted it. |
| Logical lifecycle state is distinct from scene membership | Accepted | RED handle contains scene assignment, logical attachment, visibility-dependent membership, and queued completion as separate realities but exposes them ambiguously. |
| Relinks use dedicated scene ingress | Accepted | RED routes high-frequency relinks directly to the scene while ordinary mutations use render commands. |
| Relink admission and destruction arbitrate per proxy | Accepted | One packed atomic generation/closed/count state prevents validate-then-enqueue races and makes destruction O(1) relative to queued relinks without a manager-wide exclusive lock. |
| Generic proxy handle excludes feature-specific mutation methods | Accepted | RED's handle accumulated unrelated mesh, particle, cloth, effect, light, and editor operations. |
| Render Scene preserves fixed-point world position | Accepted | RED carries fixed-point position through `RenderProxyTransform`; Vanguard already has the corresponding math types. |
| Creation descriptors are call-borrowed and synchronously internalized | Accepted | RED call sites routinely pass stack descriptors; safe asynchronous work must own its copied inputs. |
| Initial proxy publication is transactional | Accepted | RED separates construction from registration; Vanguard strengthens this so incomplete initial state is never observable. |
| Proxy creation uses typed descriptors rather than a polymorphic raw pointer | Accepted | RED's explicit type plus unchecked derived pointer relies on assertions and manual switch maintenance. |
| External proxy ID and dense scene index are distinct | Accepted | RED's dense ID enables fast camera arrays but lacks generation safety; Vanguard needs both properties. |
| Spatial entry handle is private scene metadata | Accepted | RED embeds the acceleration user-data interface and leaf key directly in every proxy; Vanguard can preserve relocation performance without coupling proxy ABI to one structure. |
| Packed proxy transform remains 48 bytes | Accepted | RED stores float rotation/scale and fixed-point translation in a compact 3x4 representation with camera-offset unpacking. |
| Relink has fast in-place movement and batched reinsertion paths | Accepted | RED collectables attempt `QuickConditionalMoveObject` and escalate only when the spatial node must change. |
| Camera-specific temporal state is outside the base proxy record | Accepted | RED already stores frame trackers and dissolve/LOD state in camera custom data indexed by scene proxy ID. |
| Base proxy storage is data-oriented rather than an inheritance forest | Provisional | RED's base roles and hot fields can be represented in SoA/type pools; representative proxy and collector chunks must validate dispatch needs. |
| Scene update epoch is separate from view collection serial | Accepted | RED distinguishes tick-gated `PrepareForUpdate`, scene frame allocation, and multiple renders/collections of one scene. |
| Core RenderScene excludes accumulated feature managers | Accepted | RED's concrete scene mixes foundational storage with decals, particles, dissolve, motion, interior data, and shader-specific state. |
| Primary proxy spatial index and secondary visibility subsystem are distinct | Accepted | RED owns both `m_rpMainSceneProxies` and a retained `vis::Scene`, and their use sites serve different query paths. |
| Permanent indexes require cross-view justification | Accepted | RED explicitly warns against adding scene collections for data needed only by one camera collection. |
| Scene access uses an explicit update/collection dependency rather than copied versions or a rendering Boolean | Accepted | RED's live proxy and VisGrid storage is safe because command flushing and scene updates precede collection in one Jobs chain. Vanguard makes that dependency explicit per scene. |
| Game-specific scene-layer names are rejected | Accepted | RED's `Cyberspace` and `WorldMap` are project policy; Vanguard needs renderer-agnostic registered layers. |
| Scene membership has one serialized commit authority | Accepted | RED accepts multi-producer pending operations but applies each drained scene batch in one mutation job. |
| Pending proxy operations coalesce through an explicit lifecycle transition table | Accepted | RED reduces repeated addition/removal/move requests before touching scene storage; Vanguard will make every transition defined and observable. |
| Proxy mutation uses preflight followed by a non-failing commit | Accepted | RED's commit is assertion-driven and has no rollback; Vanguard preserves the fast commit while adding capacity, identity, and lifecycle validation before publication. |
| Spatial repair is amortized across a mutation batch | Accepted | RED removes and moves without per-operation rebalance, then repairs dirty nodes once before clustered additions. |
| Relinks use scene-direct epoch ingress rather than general commands | Accepted | RED publishes retained relink requests into a double-buffered per-scene path and executes them through redJobs. |
| Relink coalescing mirrors RED's whole-request newest-wins reduction | Accepted | One complete retained request owns transform, bounds, teleport, skinning, float-track, and instance data; the reverse scan guarantees one writer per proxy. |
| Slow spatial moves complete in the producing update epoch | Accepted | RED's escaped-parent path defers structural movement and exposes backend-dependent temporary bounds; Vanguard will gather and commit these moves before publication. |
| Proxy types declare relink access behavior | Accepted | RED runs distinct proxy relinks in parallel but relies on implementation knowledge for non-local scene-extension work. |
| Relink overflow and stale lifecycle are explicit results | Accepted | RED can drop fixed-capacity updates while most handle entry points ignore the returned failure. |
| Heterogeneous renderer commands use inline packets in pooled pages | Accepted | RED move-constructs retained command payloads directly into aligned reusable pages and destroys them after execution. |
| Command parallelism uses declared resource-domain accesses | Accepted | RED's pointer buckets plus a manual safe/unsafe Boolean preserve some ordering but cannot describe conflicts through shared scene, camera, spatial, or renderer state. |
| Scene publication exposes a per-scene completion token | Accepted | RED correctly orders membership, relink, and rendering through one global flush counter; Vanguard preserves the dependency without serializing unrelated scenes and views. |
| The application loop does not perform an unconditional previous-render drain | Accepted | RED callers explicitly flush before frame preparation because mutable ingress and one global counter require it; Vanguard will use bounded frame-ahead dependencies and targeted back-pressure. |
| Renderer command shutdown is an explicit drain/cancel state machine | Accepted | RED command ownership is sound during execution, but reviewed teardown assumes external draining and leaves visible page-cleanup defects. |
| World-to-render integration is a per-session adapter | Accepted | RED isolates low-level Render Scene from world nodes through `RuntimeSystemRendering`; Vanguard applies the same boundary to committed Flecs transactions. |
| Proxy admission is distinct from construction and is budgeted | Accepted | RED separates proxy creation from registration and caps attachment bursts at 1,024 per frame; Vanguard generalizes this to cost-aware admission with completion tickets. |
| Activation groups publish transactionally | Accepted | RED's independent node callbacks and count cap can expose partially attached streamed content; Vanguard's existing materialization transactions provide the correct publication unit. |
| Visibility is represented by independently owned channels | Accepted | RED composes source visibility and editor filtering and allows invisible shadow contributors to remain scene-resident. |
| Proxy destruction never calls a raw owner pointer | Accepted | RED unregisters through a raw `IRenderProxySystemInterface*`, which becomes unsafe if teardown ordering leaves a handle alive. |
| Dissolve is optional visual retirement, not lifetime proof | Accepted | RED retains handles for twice a global dissolve duration, which does not prove scene-reader or GPU completion. |
| Scene ownership mode is explicit | Accepted | RED stores internally created and externally supplied scenes in the same pointer and relies on lifecycle context for teardown authority. |
| World/scene switches advance bridge generations | Accepted | Queued admission, relink, and retirement operations must not target a replacement world or reused proxy slot. |
| Main proxy spatial index is distinct from view-query coordination | Accepted | RED directly owns the live `visGrid` in Render Scene while `vis::Scene` owns ephemeral queries and per-view frame data. |
| Collection reads stable live state after a per-scene update dependency | Accepted | Mirror RED's retained proxies, double-buffered relinks, command-owned structural operations, and Jobs ordering; do not copy the scene to manufacture reader versions. |
| Spatial state remains live rather than copy-on-write | Accepted | Runtime/editor collections consume the same completed scene epoch. Overlap is expressed through the scheduler; mutation waits behind dependent collection instead of cloning the VisGrid. |
| Fast and structural spatial moves complete before collection | Accepted | RED updates object bounds immediately, queues escaped-cell moves, and repairs dirty nodes before the later collection chain proceeds. |
| Spatial configuration does not encode a fixed world extent or 2D grid | Accepted | RED's 32-metre XY grid and 8-kilometre half extent are workload-specific measured constants rather than general engine semantics. |
| Broad visibility roles are registered renderer masks | Accepted | RED embeds fixed 16-bit inclusion and time-of-day masks; Vanguard must remain project-agnostic and extensible while keeping hot filters compact. |
| Visibility queries are frame-scoped and cannot escape their collection epoch | Accepted | RED retains queries until `FinishQueries` and asserts that external references do not survive the render. |
| Live scene view with explicit scheduler ownership | Accepted | Vanguard makes RED's implicit global ordering a per-scene update/collection dependency while retaining stable live proxy and spatial storage. |
| Query-only visibility is a separate feedback service | Accepted | RED's query-only boxes are renderer-evaluated streaming probes with no drawable/proxy behavior, despite being exposed directly on `IRenderScene`. |
| Visibility feedback is versioned tri-state data | Accepted | RED's Boolean bitset omits view, scene version, age, and not-yet-evaluated state and can redirect stale handles after slot reuse. |
| Feedback aggregation names an authoritative view policy | Accepted | RED has one scene-global result buffer cleared by an evaluated camera; runtime/editor and multi-camera views require independent results plus explicit aggregation. |
| Streaming policy remains outside visibility evaluation | Accepted | RED combines frustum/occlusion and activation distance inside the renderer; Vanguard observers own distance, hysteresis, prediction, and residency decisions. |
| Update and collection jobs retain live type payloads through completion | Accepted | RED candidates resolve to retained live proxies whose collection dereferences transforms, flags, resources, materials, geometry, lights, and custom data. |
| Per-view collection state never mutates shared proxy state needed by other views | Accepted | RED writes some proxy frame trackers and LOD state during collection; Vanguard moves view-local state into camera/view storage instead. |
| Collector outputs are typed job-local pages followed by deterministic reduction | Provisional | Replaces RED's mixed atomics, locks, fixed arrays, and virtual callbacks; representative proxies must validate exact output families and dispatch shape. |
| Representative proxy payloads are typed pools, not one inheritance forest | Accepted | RED mesh, light, and decal proxies expose very different hot fields, sidecars, and output families while sharing only identity, transform, bounds, layers, and visibility roles. |
| Mesh collection requires immutable payload plus view-local state | Accepted | RED mesh collection reads resources/materials/instances/sidecars but mutates camera dissolve, frame tracker, LOD buckets, cached distance, and feature state. |
| Light collection emits light records, not draw chunks | Accepted | RED light proxies add themselves to collector light arrays and later scene custom data consumes them for clustered/tiled lighting and shadows. |
| Decal collection is a separate drawable family | Accepted | RED decals use cached projection/material/routing payloads and emit decal-specific render chunks rather than mesh chunks. |
| Camera custom data becomes an explicit ViewProxyState layer | Accepted | RED's `CameraCustomData_RenderProxyData` stores dissolve state, frame trackers, and clustered instance data by scene proxy ID. |
| Dynamic decal interaction remains feature-level, not base RenderScene API | Provisional | RED mixes decal spawning, mesh target attachment, and mesh pass generation through scene and mesh proxy methods; Vanguard should isolate this behind declared feature reducers. |
| RenderScene exposes explicit frame-stage operations | Accepted | RED's frame path has distinct prepare, update, graph allocation, culling/collection, and end-frame intervals but expresses part of the safety through global state. |
| Scene read leases replace BeginRendering/EndRendering as the safety boundary | Accepted | RED uses a render-exclusive Boolean plus command flushing; Vanguard needs versioned concurrent runtime/editor view collection and frame overlap. |
| Culling and collection are render-path jobs, not game-loop callbacks | Accepted | RED schedules culling inside render-node execution with redJobs fan-out and explicit fences before dependent render nodes. |
| Resource-flow phases stay outside RenderScene collector logic | Accepted | RED's pre-consume/consume split is useful for transient resource allocation, but `IsConsumePhase` checks should not leak into CPU-only scene collection. |
| RenderSceneEndFrame publishes feedback and retires CPU scene state only after collection completion | Accepted | RED finalizes visibility queries and query-only bitsets at render-frame boundaries, but Vanguard must include lease and GPU-retirement awareness. |
| RenderScene is an engine service with explicit frame-pipeline hooks | Accepted | Vanguard already owns service registration and global frame phases; RenderScene should plug into that loop instead of presentation or application-state code. |
| RenderScene uses Jobs RenderPath counters and no private scheduler | Accepted | The existing Jobs API exposes `Priority::RenderPath`, `Builder`, `Counter`, dependencies, parallel dispatch, and explicit fence closure. |
| RenderScene core has no Flecs dependency | Accepted | GameWorldService and CellStreamingSystem already own Flecs/materialization; RenderScene consumes mutations through a bridge so preview and thumbnail scenes can exist without a game world. |
| RenderScene payloads retain resource references and handles | Accepted | Vanguard resources already provide logical identity, generation-checked handles, async request coalescing, cancellation, and failure traces. |
| Renderer proxy readiness is distinct from resource byte availability | Accepted | WorldStreamingExecutor separates `ResourceAvailable`, downstream `SetReady`, and `CompleteRelease`; renderer residency needs the same boundary. |
| RenderScene hot storage uses rendering pools | Accepted | Vanguard already exposes canonical memory pools and object-to-pool macros; hot scene storage should not allocate through generic standard-library mechanisms. |
| Presentation never kicks scene rendering by ownership | Accepted | Presentation owns windows, attachments, viewports, swap chains, HDR/display policy, acquire, resize, and present; scene publication and collection are render-phase work. |
| First RenderScene implementation is CPU-only | Accepted | RHI, cameras, batching, and render graph are not required to validate scene handles, mutation publication, read leases, spatial membership, and typed collector pages. |
| WorldRenderBridge is dirty-stream driven, not table-scan driven | Accepted | Transform, bounds, visibility, payload, and destruction changes must arrive through changed-component streams/revisions; full world scans are reserved for cold build, recovery, and validation. |
| Flecs query change detection is a table gate, not the primary relink stream | Accepted | Flecs `EcsQueryDetectChanges`/`ecs_iter_changed` can skip untouched matched tables, but per-entity relinks need Vanguard's exact committed component-action journal. |
| Forced LOD and dissolve excluded from the base interface | Provisional | Present in RED but appears policy-specific; representative proxies and collectors will decide. |
