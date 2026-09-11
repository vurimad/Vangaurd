# Geometry rendering: RED batcher port and remaining phases

Date: 2026-09-07

Status (2026-09-09): 9B.1 implementation complete; combined verification deferred
to the user's explicit final batch. 9B.2 production source now includes complete
anchor closure, shared placement/residency publication, strong bindings and
withdrawal through existing retirement. Source inspection only; phase exit and
live publication acceptance have not been verified.
No compilation, test execution or writing tests until that final batch is requested.
Static review follow-up: drawable work now uses a bounded intrusive active queue;
ready bindings are not scanned. Publication no longer repeats preparation, and
oversized scene uploads and incomplete retirement receipt sets fail explicitly. See the
geometry batcher checkpoint for the exact budgets and verification boundary.
This refines the existing 9A / 9B / 9C / 9D / 10 order.
It supersedes the earlier proposal for an independent `PipelineBucketRegistry`
and `mesh_batch_registry` owner. It does not reopen material Phase 3 or replace
the now-implemented Render Graph/resource allocator.

## 1. Destination and the meaning of 1:1

The destination is a programmatically authored scene, without an editor, whose
static meshes use cooked materials and real vertex/index geometry, survive
movement and removal, and appear through the normal viewport/frame path.
GPU-driven opaque/masked drawing is the only planned production geometry path,
starting with the first mesh draw. There is no direct-draw baseline or CPU-renderer
bring-up phase to implement and later replace.

The user's 1:1 requirement applies to RED machinery relevant to CPU preparation
and recording for this GPU-driven renderer: pass context, dependency ordering,
compatible graphics state, geometry binding and recording-local state. Port that
machinery directly where applicable. It does not require RED's CPU visible-chunk
sorter, transform gathering, direct draws, or unrelated geometry features.

Keep a source-to-port ledger beside the implementation. It must distinguish
retained RED code, adapters to existing Vanguard owners, and GPU work that RED's
inspected batcher does not implement. Do not copy unused APIs merely to keep the
class outline identical, and do not call a newly invented CPU framework a 1:1
port. The earlier direct-first recommendation is withdrawn, including a new
direct-renderer test oracle.

## 2. Source evidence: what RED actually owns

RED source root: `D:/root/R6.Root/Mainline/dev/src/`.

| Source | Observed responsibility |
| --- | --- |
| `common/renderer/src/renderProxyMesh.cpp`, `CollectSingleLodModeInstances`, material-pass collection around lines 1098-1141 | Select mesh chunks/material passes and add keyed chunks to stage collectors. Collection is upstream of the batcher. |
| `common/renderer/src/renderCollector.cpp:176`, `DoCull_PrepareSceneQuery`; `DoCull_Main:339` | Prepare scene queries, dispatch collection work and establish an explicit producer fence. Reuse this dependency discipline with Vanguard's existing spatial candidate producers, not RED's final CPU chunk output. |
| `common/renderer/src/renderBatchMask.h`, `RenderBatchKey` | Packed geometry, PSO, parameters, priority/order, masking, instance-storage and modifier distinctions. |
| `common/renderer/src/renderGeometryBatcher.h`, `CRenderGeometryBatcher` | Context, sorting modes, per-worker recording scratch, instance storage, geometry binding and drawing. |
| `common/renderer/src/renderGeometryBatcher.cpp:1035`, `RenderGeometry` | Copies a collected bucket into work storage and enters execution. |
| Same file, `ExecuteRenderGeometry:1385` | Filter, optional instance split, sort, compare batch keys, flush before incompatible changes, bind changed state, gather/draw instances, final flush. |
| Same file, `BindBatchGeometry:214`, `DrawMeshInstances:352` | Real fixed-function geometry binding and indexed instanced drawing. |
| `common/renderer/src/renderGraphNodes.cpp`, geometry-stage recording around line 1857 | Graph node supplies view/pass context and invokes the batcher; the batcher does not own the graph. |

This is not evidence that RED draws by GPU culling/count/prefix/scatter. Those
would be Vanguard extensions. Nor does copying the batcher alone supply the
upstream proxy/material-pass collection or shader contracts.

### Member-by-member port policy

| RED member/family | Vanguard treatment and reason |
| --- | --- |
| `RenderContext`, `RenderGeometry`, `ExecuteRenderGeometry` | Preserve useful structure and graph-to-batcher call direction. Input is retained ready shell segments plus GPU argument/count ranges, not a CPU visible-chunk array. Omit redundant entry layers if they have no remaining responsibility. |
| `FilterNullDraws`, `SplitInstances`, `SortChunks` | Do not port CPU per-visible-chunk filtering/sorting/splitting. GPU work selects and groups instances. Unsupported priority/keep-order features are not dormant APIs. |
| `GetBatchKeyDiff`, state-change/flush discipline | Retain compatibility and changed-state logic for CPU shell recording. Shells can be ordered when their state-key revision changes; this is not a per-frame visible-object sort. Flush means finishing the compatible indirect segment before binding incompatible state. |
| `BindBatchGeometry`, mesh draw preparation | Port applicable vertex/index binding and indexed-range semantics to existing RHI/arenas. Record `DrawIndexedPrimitiveIndirect` or its counted form from the start; CPU does not gather visible transforms or derive the visible instance count. Do not copy RED-specific doubled-index/two-sided assumptions without matching VMESH evidence. |
| Parameter-key changes and `CRenderMaterialParameters::BindParameters` | Bindless adaptation: select a GPU material record per work item; do not rebind textures/constant buffers or split otherwise identical batches for parameter values. Shader/PSO differences still split. |
| `ThreadData`, last-bound geometry/PSO state | Keep recording-local scratch. A job/recorder owns it until joined; do not assume an OS thread stays fixed if jobs can migrate. No global draw-loop mutex. |
| `StaticInstanceBuffer`, `AllocateStaticInstanceBuffer`, pending uploads, `DynamicInstanceBuffer` | Existing GPU Scene already owns transforms, uploads and retirement. Adapt instance addressing to it instead of adding a duplicate transform pool, buddy allocator or upload queue. This deviation is reuse, not bindless itself. |
| `OnFrameTick`, upload-flush lifecycle | Fit existing frame preparation and GPU Scene acceptance. No independent frame scheduler or second retirement service. |
| Skinned/decal/debug/text draw branches and material modifiers | Not part of static-mesh first rendering. Record as unported; do not import dummy managers, empty branches or unsupported public APIs to mimic file size. |
| Packed key widths and platform SIMD details | Preserve ordering/compatibility semantics, not RED's unrelated fixed capacity limits or platform assumptions. Validate any packed representation against Vanguard limits. |

RED is not entirely lock-free: its dynamic ring uses a CAS reservation and
end-frame/read locks; static instance allocation and pending uploads have short
locks (`renderGeometryBatcher.cpp:79`, `:183`, `:206`, `:922`, `:971`, `:1004`).
Its pending list is detached before upload work. Copy the ownership discipline,
not unnecessary locks for pools Vanguard will not have. Capacity checks must
remain effective in Shipping; do not import debug-only overflow protection.

## 3. Already implemented: reuse, do not rebuild

| Foundation | Current source and boundary |
| --- | --- |
| Cooked material/shader/pipeline contracts and runtime residency | `materialTools`, `materials`, `shaders`, `pipelines`, `material_residency_runtime.cpp`. Exact generation ownership and technique requests exist; material Phase 3 is headless/no-draw, not a missing material loader. |
| GPU material shader access | `source/rendering/shaders/gpu_scene_types.hlsli:205`: `LoadGpuMaterial`, parameter-word and resource-role loaders already exist. The renderer domain should wrap these, not invent another layout/decoder. |
| Pipeline materialization/cache | `RequestRenderPipeline`, `PipelineCache`, `MaterialTechniqueRequest`. Material technique readiness already exists. Renderer feature catalogs and startup binding-layout descriptions exist too. No second PSO compilation/readiness service. |
| Mesh 9A | `mesh_residency.cpp:784` onward resolves materials and publishes immutable topology. `sourceSubmesh` is a source-table ordinal. `RenderableTopologySubmitted` is not drawable readiness. |
| Geometry and mutable GPU Scene placement foundations | Geometry allocators/uploaders plus `GpuRenderableResidency`, `GpuPrimitivePlacement`, `GpuPhasePlacement` already exist. Populate and own them; do not redesign their purpose. |
| World/scene foundations | Placed/visual component lifecycle, transform/runtime bridge, typed RenderScene proxies, camera storage, light tables and GPU Scene publishing already exist. Concrete mesh/camera/light adapters and owning mesh binding integration are the remaining work. |
| CPU broad phase to GPU candidates | `RenderSceneManager::PrepareGpuVisibilityCandidates`, `WriteGpuVisibilityCandidateBatch`, `CompleteGpuVisibilityCandidates` already exist. They seal a scene epoch and fill disjoint ranges of GPU instance identities from the spatial index. |
| GPU visibility planning | `GpuVisibilityPlanBuilder` already reserves per-view candidate/output storage and converts filled ranges into bounded workgroups without an internal candidate mirror. Integrate it; do not create a new candidate service. |
| Render Graph execution | `FrameRenderer::ExecuteBuiltGraph`, node resource declarations/jobs, terminal receipts, allocator Finish and output transaction are implemented. Reuse this executor. |
| Native drawing/output | RHI indexed/direct/indirect/count commands exist (`rhi.hpp:182`). FullscreenCopy has a real shader/node and native pixel proof. This is not yet a service-driven scene mesh proof. |

### Existing CPU broad phase is the right starting point

The live source chain is:

```text
sealed RenderScene mutation epoch
  -> PrepareGpuVisibilityCandidates (spatial work partition)
  -> GpuVisibilityPlanBuilder::ReserveViewRanges
  -> parallel WriteGpuVisibilityCandidateBatch into disjoint reservations
  -> join producers; CompleteGpuVisibilityCandidates releases the scene seal
  -> CompleteViewRanges / Finalize; retain/upload the filled candidate ranges
  -> GPU visibility, LOD/phase expansion, binning and indirect arguments
```

Evidence: `render_scene.cpp:2440`, `render_scene_spatial.cpp:644`,
`gpu_scene_visibility.cpp:109`; `viewport_tests.cpp:681` exercises the epoch seal,
reservation and missing-GPU-identity failure. Production source currently defines
these APIs but does not call them outside tests. The existing test is not evidence
that FrameRenderer already schedules candidate production.

`WriteGpuCandidateRange` rejects cells by aggregate bounds/frustum and can also
test individual proxies, then applies layer/visibility/mesh filters and writes
GPU instance indices. This is candidate reduction, not material-pass expansion,
final LOD choice, batch formation or draw submission. It fits GPU-driven rendering.
CPU rejection must be conservative for the same view/epoch and all supported
force-visible, jitter, large-coordinate and bounds conventions; a GPU cannot
recover an object the CPU omitted incorrectly.

Reuse the current query initially. Measure the optional individual-proxy tests
before changing their granularity; do not build a second spatial index or query
framework just to make this "coarse." Cell-only filtering, if measurements justify
it, is a narrow refinement of the existing candidate writer. Broad-phase results
may include false positives, but must not omit eligible visible geometry.

Capacity is not magically proportional to the final visible set: the current
planner sums raw indexed/unindexed proxies and reserves worst-case candidate
space. Cell rejection reduces filled work, not the reservation requirement.
Reuse bounded frame storage; test limits and measure scanned cells, visited
proxies and submitted candidates. Do not hide this cost behind a claim of
zero-copy or perfect culling.

Planning is serialized, writers use separate ranges, and completion happens after
the producer join. There is one active candidate seal per scene today, and the
visibility builder completes views in reservation order. Respect these contracts
for multi-view work instead of introducing per-candidate atomics or concurrent
plans the API cannot support. Releasing the CPU seal does not release frame/GPU
ownership: retain referenced generations and GPU storage through actual draws.

### CPU and GPU division of work

| CPU prepares/records | GPU decides/produces |
| --- | --- |
| Exact resident mesh/material/PSO closure and stable shell/bin metadata | Validate candidate instance and current residency; select resident LOD |
| Conservative spatial candidates and view data | Fine visibility and primitive/phase expansion |
| Immutable ready shell catalog view, compatible bindings and indirect buffer offsets | Per-bin counts, prefix offsets, scattered instance/primitive identities |
| Bind PSO/vertex/index/global descriptor state and record indirect calls per shell segment | Indexed arguments/counts, then execution and per-pixel material evaluation |

The CPU may visit ready shell segments even when the GPU finds no work for some
of them; a GPU count of zero handles that without a readback. It must not iterate
visible objects to rediscover their bins. GPU indirect drawing still cannot
change arbitrary PSOs inside one standard indexed-indirect call. Track CPU shell
visits separately from GPU nonempty bins/draws to expose submission overhead.

The live feature gap remains. `BuildRenderGraphCamera` and
`BuildRenderGraphGBufferOnly` are empty; numerous named feature nodes have empty
Execute bodies. Their names do not establish that a GBuffer or mesh pass works.
The current shader-runtime document explicitly distinguishes startup proof from
the separate native graph-copy proof.

## 4. Materials inside handwritten engine shaders

### Evidence and the existing Vanguard seam

RED's `win32/shaderCompiler/src/materialCompiler.cpp:81`,
`CreateShaderSpecificConstantBufferAndCodeProvider`, injects generated parameter,
texture and sampler declarations through shader-code annotations. Its mesh proxy
selects material passes, and its batcher binds the resulting PSO and parameter
batch data. Do not copy those fixed binding slots into Vanguard.

Unreal's local source provides the particularly explicit graph/template example:

- `D:/UnrealEngine/Engine/Shaders/Private/MaterialTemplate.ush:4` identifies the
  template as filled by `FHLSLMaterialTranslator::GetMaterialShaderCode`.
- `BasePassPixelShader.usf:94` includes `/Engine/Generated/Material.ush`.
- The handwritten base pass calls `CalcMaterialParameters` around line 956 and
  reads `GetMaterialBaseColor` at line 1014 from `FPixelMaterialInputs`.

Vanguard already has the corresponding tool seam: `MaterialSlangDomain` in
`material_slang_generator.hpp`. Its prefix supplies trusted domain types and
loaders; generated evaluation lives between prefix and suffix; the suffix owns
real annotated shader entry points. Reflection finalizes parameter offsets.
Use that facility, not a new shader linker or a runtime compiler.

### Production contract to finish

One renderer-owned static-surface domain supplies:

1. Mesh vertex input/decoding, transform and camera-relative conventions,
   interpolation, UVs and normal/tangent conventions.
2. Draw context carrying stable instance AND primitive identity. Instance alone
   cannot select the material of a multi-submesh mesh. Material identity must
   remain flat/non-interpolated through the raster stages.
3. `VanguardLoadMaterialParameterWord`, resource and sampler accessors backed by
   the existing GPU Scene records and renderer descriptor domains.
4. One material-evaluation entry returning the declared surface result, called
   inside each pixel invocation by handwritten depth/color/GBuffer pass logic.
5. Cooked shader/technique variants for the actual phase, vertex layout, material
   program and required fixed state. The runtime requests ready variants; it
   does not inspect a graph or choose a "data-only" evaluation mode.

Conceptual shader composition, not a claim these entry names already exist:

```text
handwritten mesh vertex entry -> interpolated surface inputs + flat draw identity
handwritten pixel entry -> generated EvaluateMaterial(inputs, material context)
                       -> shared handwritten surface helpers where needed
                       -> color/depth/GBuffer output
```

A minimal/master-only material and a material with arithmetic/TextureSample
nodes use this same route. A shared handwritten helper can sample a texture
selected by material data, or generated graph code can sample it; those are
different expressions inside the same contract, not different runtime systems.
Current codegen represents logical resources as descriptor-index uints, including
in value/output aggregates, and creates typed opaque objects at operations that
consume them. Do not reintroduce the old claim that data-only materials require
returning opaque texture objects or cannot use the common evaluator.

The surface domain must say whether an output is an evaluated color or an index
to be consumed by a shared helper. Prefer evaluated surface fields for ordinary
passes. Depth and color must agree on masked opacity; opaque depth can omit work
only when its phase contract permits it. Nonuniform descriptor selection,
derivatives and sampler handling need real pixel proofs, not just reflection.

Same shader program/layout/PSO with different parameter values or texture indices
can share a batch. Different compiled graph logic normally means a different
shader/PSO and therefore a different batch. Bindless does not merge arbitrary
programs, eliminate vertex/index state, or permit pipeline changes inside an
ordinary indexed-indirect command.

## 5. Refined phases and proof gates

### 9A -- mesh-to-renderable topology: complete, retain the boundary

Reuse immutable renderable/LOD/primitive/material/phase definitions and exact
asset ownership. Do not repeat material residency or call topology drawable.

### 9B -- RED batcher foundation and drawable mesh binding: next

- Port RED's relevant CPU preparation/recording structure with the ledger above;
  no CPU-visible chunk sorter, instance-gathering path or direct-draw baseline.
  Add shell/bin storage inside that owner, not a sibling registry service.
  Reuse material technique requests and native pipeline cache ownership.
- Seal the renderer surface/pass/vertex/draw-context contract from section 4
  before freezing shell compatibility. Cook minimal real graphics fixtures.
- A shell groups phase + exact native pipeline/interface + vertex arena set +
  index arena/format + required non-PSO state. A geometry bin adds exact geometry
  generation and indexed range. Material parameter values are not shell keys.
- Resolve normal/mirrored winding and two-sided behavior truthfully.
  `MaterialTechniqueDesc` now carries the required mirrored/two-sided selectors
  through the existing factory/cache; effective native state is in the cache key.
  Never publish two placement IDs backed by an identical wrong-cull PSO.
- Publish complete anchor-LOD geometry AND phase placements before residency.
  Retain their dependencies in one strong mesh binding; do not expose empty
  shell/bin fields as ready. Pending/failed PSOs remain non-drawable. A fallback
  is valid only when it is a real ready compatible pipeline/material.
- Define frame borrow ownership and withdrawal/retirement ordering; reuse
  the existing lifetime machinery. No native compilation wait during recording.

Proof: complete versus partial LOD admission; compatible-material coalescing;
every incompatible key field; normal/mirrored/two-sided variants; stale handles;
cancel/rollback/release; compile/reflection and native pipeline proofs for the
common surface shader and GPU material layout. Test recording through indirect
commands where needed, using bounded fixture argument data rather than a CPU
renderer. The integrated GPU-generated-argument mesh draw belongs to Phase 10.

#### 9B.1 -- material/geometry draw preparation: first implementation slice

Implementation checkpoint (2026-09-09): **implementation complete; combined
verification deferred**. Historical executed proofs below remain historical;
no additional tests may be written or run until the explicit final batch.

- Implemented normal/mirrored/two-sided requests through the existing material
  technique and graphics pipeline factory. Effective winding/cull state extends
  the existing cache identity; no separate variant registry. Native binding
  layouts and descriptor-domain identities also participate, preventing shared
  feature/material cache aliasing across different root interfaces.
- Implemented startup material binding-layout descriptions and service-owned
  native layouts. Later material technique requests receive them. Normal
  shutdown, startup rollback and device abandonment release them after the
  material runtime and pipeline cache. Feature-pipeline layouts remain separate.
- Added native variant/coalescing coverage and changed the production material
  service fixture to require vertex-stage draw constants. This tests a real
  non-empty layout on lazy technique creation, not just an unused configuration.
- Validation: `geometryAllocatorTests`, `materialRuntimeServiceTests` and
  `rhiNvrhiTests` built and passed in Debug and Shipping (six runs). This is the
  affected-target matrix, not whole-solution or complete 9B.1 exit proof.
- Added trusted `static_surface_prefix.vsl` / `static_surface_suffix.vsl`:
  ordinary vertex/pixel entries, GPU Scene instance/geometry/material reads,
  float or quantized position decode, camera-relative transforms, and one generated
  `EvaluateStaticSurface` path for parameter-only and texture-sampled graphs.
  Texture roles resolve through the existing texture-residency table. Generated
  texture/sampler heap accesses explicitly mark non-uniform descriptor indices.
  GBuffer outputs are baseColor, signed world-normal/roughness, emissive/metallic;
  depth uses the same evaluator and opacity clipping with no color output.
- Added allocation-free `ValidateMeshDrawLayout` and retained `PrepareDraw` on
  the existing `MeshResidencyManager`, not a second batcher/owner registry.
  Preparation holds one coalesced mesh demand and existing normal/mirrored
  technique requests. Both must succeed before later drawable admission.
  Partial request failure rolls back without modifying the output.
- The preparation call is resource-time and main-thread only. Geometry and
  material use direct authored ordinals; phase lookup inspects only the bounded
  primitive phase list. Layout checks inspect bounded vertex attributes. The
  existing short demand lock protects only the ownership state, never native
  pipeline work. No scene scan, per-frame/per-instance preparation, new mutex,
  CPU instance collector, or generic lifecycle abstraction was added.
- Actual shader proof covers DXIL/SPIR-V, GBuffer/depth, parameter-only/sampled
  graphs, float/quantized VPPL compatibility, and 24 D3D12 native pipeline variant
  requests. Separate layout fixtures cover interleaved/separate streams and
  UInt16/UInt32 indices. The cooked service fixture covers retained preparation,
  capacity rollback, exact geometry identity and shared-demand cleanup.
- Proofs exposed and corrected three existing limits: void fragment reflection,
  stored normalized vertex lanes versus fetched shader inputs, and a hard-coded
  one-entry native vertex-layout lifetime table. The latter now uses a bounded
  capacity in the existing table/cache, not a new cache.
- Latest affected validation: `materialToolsTests`, `materialRuntimeServiceTests`,
  `geometryAllocatorTests`, `pipelinesTests`, `shaderToolsTests`, `shadersTests`,
  and `rhiNvrhiTests` all built and passed in Debug and Shipping (14 runs).
  This is not a whole-solution result or an executed indirect draw proof.
- **Deferred to the final verification batch:** the common generated surface fixture and
  cooked VMESH/VMAT service preparation fixture are separate proofs. Join them
  into the single complete vertical fixture required below. This verification is
  not a runtime implementation prerequisite for starting 9B.2. No drawable placement,
  component, indirect execution, or scene pixel result is claimed by these tests.

The concrete draw-input ABI is a GPU-written 16-byte record (instance, primitive,
geometry, material), supplied at vertex binding 15 as two `R32G32UInt` attributes
with instance step rate 1. Indirect `firstInstance` selects this stream's records.
The GPU producer/recording belongs to Phase 10; no CPU collector was introduced.
This avoids interpreting Slang `SV_InstanceID` as an absolute record index or
requiring SM 6.8 `SV_StartInstanceLocation`: see the
[Slang target semantics](https://github.com/shader-slang/slang/blob/master/docs/user-guide/a2-01-spirv-target-specific.md)
and [SM 6.8 specification](https://microsoft.github.io/DirectX-Specs/d3d/HLSL_ShaderModel6_8.html).
The per-view/pass push-constant context is 24 bytes (`StaticSurfaceDrawContext`),
visible to vertex and pixel stages. Native tests supply it through the existing
layout description; no shader-name startup branch or per-material binding set.

Purpose: turn an exact mesh primitive + resident material + renderer phase into
validated, retained, ready graphics inputs for the batcher. This is resource-time
preparation, not a per-frame object walk. Start here because shell identity and
drawable admission cannot be correct until the shader/geometry compatibility is
real. Section 8 records the component/scene ownership this result will serve.

Inputs: retained VMESH/VMAT generations, authored primitive/phase metadata,
anchor-LOD geometry definitions, and the renderer's surface/attachment contract.
Output: bounded per-primitive/phase preparation with exact material identity,
retained technique request, matching vertex/decode convention, normal/mirrored/
two-sided fixed state and geometry arena/range identity. Pending and failed
preparation are explicit. This is not yet a published drawable mesh binding.

Implementation work, kept in one slice:

1. Supply one trusted static-surface domain and handwritten mesh vertex/depth/
   color entry wrappers through the existing material tool pipeline. Wire their
   loaders to GPU Scene and define flat instance/primitive identity and indirect
   instance-offset semantics. No separate master-only shader path.
2. Validate VMESH vertex semantics/formats/bindings/strides against the cooked
   pipeline, including position decode and required material inputs. Cover
   float and quantized static position formats, separate/interleaved streams,
   and UInt16/UInt32 indices. Unsupported layouts must fail explicitly; do not
   assume arbitrary reflected layouts can run one incompatible vertex entry.
3. Use `MaterialTechniqueRequest` / `RequestRenderPipeline` for ready phase
   variants. Resolve the narrow winding/two-sided variant seam in their existing
   identity/cache path; no second pipeline registry. A phase name alone does not
   establish layout, attachments or fixed-state compatibility.
4. Provide the material draw-context binding layout through existing renderer
   initialization. Implemented via `RenderingServiceConfig::materialBindingLayouts`;
   the surface contract supplies a 24-byte slot-0 vertex/pixel push-constant layout.
   Feature-pipeline startup layout support remains independent of material
   techniques. Retain the common layout until its pipelines are gone; no
   shader-name-specific startup branch or per-material descriptor set.
5. Add only the implemented batcher preparation surface and necessary existing
   owner integration. Do not stub recording methods, create parallel material
   resolution, or allocate/publish shell/bin residency just to claim readiness.

Expected touch points: renderer-owned surface shader sources and their trusted
tool-side assembly; existing `MeshResidencyManager` preparation; existing
`material_residency_runtime` / `render_pipeline_factory` where variant identity
requires it; service initialization; focused shader/material/geometry tests and
Premake entries. Component and RenderScene source changes are outside this slice.

Exit proof: a real cooked VMESH/VMAT/VPPL/VSHADER fixture prepares every required
anchor primitive/phase; matching layouts pass and mismatches fail; material
values do not spuriously split compatible pipeline identity; winding variants
are genuinely distinct when required; pending/failure/cancel leave no partially
ready output; shared ownership and cleanup balance in Debug and Shipping.
Shader compilation/reflection/native pipeline proof is not a scene pixel proof.
No CPU renderer, direct-first milestone, GPU culling implementation, component,
or strong drawable publication is part of 9B.1. Remaining 9B work installs the
prepared state into complete shell/bin/LOD placements and the owning binding.

#### 9B.2 -- shell/bin placement and strong drawable mesh binding

Recovery checkpoint (2026-09-09): production source implemented, phase exit not
verified. The interruption stopped after the initial resource-time slice, which
adds `RenderGeometryBatcher` inside `MeshResidencyManager`, bounded generational
shell/bin interning and retained `GeometryBatchLease` ownership. Both winding
variants must be ready before placement acquisition; failure/pending preserve
inputs, and partial destination acquisition rolls back. Exact native pipeline
and generational arena identity split shells; exact geometry/ranges split bins.
Material parameter/texture values do not split otherwise compatible destinations.

Individual leases remain unpublished preparation. The continuation adds complete
anchor aggregation and a shared `MeshDrawableBinding`, explicit per-phase
attachment contexts, disabled residency plus full placement publication, then
resident-bit publication after placement acceptance. Initial readiness polls real
geometry/residency GPU completion. Last release withdraws eligibility and retains
all leases until the actual renderable allocation retires in `GpuSceneLifetime`.
Normal frame maintenance seals existing retirement owners from joined RHI
submission receipts, with explicit evidence for never-submitted queues. Submitted
upload failures retain frozen ownership until device abandonment.
No resident bit is published merely because one primitive has a ready lease.
The implementation/source ledger and exact boundary are in
`source/rendering/docs/geometry-batcher-port.md`.

Shared shell/bin definitions contain no per-view visibility or chosen LOD.
View/pass constants, candidates, visibility, LOD selection, bin membership/counts,
scatter ranges and indirect outputs belong to the per-view Phase 10 path.
Compatible pass reuse must be explicit; sharing destinations does not share
the visible-instance lists of different cameras.

Compilation, execution and test authoring are deferred to the user's explicit
final batch, including the combined 9B.1 vertical fixture. Source inspection is
not a substitute for that verification and does not close the 9B.2 proof gates.

### 9C -- StaticMeshComponent and minimal scene authoring

2026-09-09 source checkpoint: the initial adapters are implemented in
`source/entities` and registered by ManagedGameWorldService. See
[concrete rendering components](../../source/entities/docs/concrete-rendering-components.md)
for configuration, ownership boundaries and deferred proof. StaticMeshComponent
uses typed references, independent demand and the shared strong binding, with a
bounded intrusive preparation queue. Directional/point/spot lights use existing proxies;
perspective cameras use existing storage and a bounded post-transform dirty queue.
Directional lights use explicit global candidate collection. No compilation, project
generation, test authoring or executable scene proof has been performed. The 9D
source pass now owns strong binding transfer into RenderScene and exposes the
accepted binding for retained-frame consumption; Phase 10 still has to consume
that handoff in actual indirect work.

Make `StaticMeshComponent` a thin consumer of the strong mesh binding through
the existing placed/visual component and rendering runtime. Store authored mesh
reference and render properties, not a second copy of default mesh materials.
Shared components retain independent demand; releasing one must not evict another.

After mesh ownership is sound, add the small camera/light component adapters
already anticipated by the world checkpoint, using existing camera storage and
light proxies. A light is unnecessary for the first unlit pixel but needed for
the simple-lit scene gate. Do not create another transform, camera, or light system.

Proof: scripted scene creation, transform propagation, pending load/cancel,
shared mesh use, world/level removal and invalid asset handling, with no editor.
Material-instance overrides/hot reload remain separate future scope unless their
existing support is explicitly used; do not hide a new override subsystem here.

### 9D -- RenderScene mesh lifecycle

2026-09-09 source checkpoint: implemented strong drawable retention in mesh
payloads, revision receipts for GPU bind/clear, old-until-accepted replacement,
bounded changed-binding resolution, destruction unlinking and an owner-thread
accepted-binding handoff for later recording. RenderingService resolves cutovers
after the previous CPU rendering join. See the 9D scene-cutover section in the
[geometry batcher ledger](../../source/rendering/docs/geometry-batcher-port.md).
Compilation, executable lifecycle proof and Phase 10 consumption remain deferred.

Connect the strong binding to existing mesh proxies and the GPU publisher.
Publish replace/clear with revision-safe acceptance, bounds/relink and retirement;
preserve the old accepted state until its replacement is accepted. Exercise
removal while work is pending and while earlier frames still retain the binding.
Keep immutable frame consumption separate from mutable owner-thread updates.

Proof: shared resources, mesh replacement, late/stale completions, clear before
acceptance, component/world destruction, multiple frames in flight and terminal
shutdown. Define the retained-frame cutover handoff here; Phase 10 connects its
actual draw submissions. This is completion of existing RenderScene machinery,
not a RenderScene rewrite.

Also prove the existing candidate writer against the new mesh bindings: stable
GPU identities, conservative bounds after movement, disjoint range production,
scene-seal release on failure, and retained identity lifetime after CPU collection.
Do not implement a parallel RED proxy/chunk collector.

### Phase 10 -- visible geometry, GPU batching and end-to-end closure

Keep one phase with three implementation subphases. They are ownership and
execution boundaries, not three temporary renderers.

#### Phase 10.1 -- production geometry work path and first pixels

Status (2026-09-10): items 1 through 8 have source implementations. Item 7
wires the camera graph and the item 6 recording caller; item 8 connects incoming
GPU fences through imports, recording and native submission. The cooked renderer
pipeline catalog and final executable validation remain outstanding. No first-pixel
result is claimed. The batcher exposes
sparse dirty metadata for the shared publisher without depending on that
publisher. Each retained frame now owns one bounded geometry-work package. It
uses one checked Rendering-pool allocation partitioned into direct builder spans
and carries only logical render-flow resource identities; it does not retain or
copy scene payloads. Candidate planning now covers the whole prepared view family:
one exact completed scene epoch and shared spatial batch layout, with separate
view queries, candidate reservations, work ranges, counters and visible-output
partitions. Checked family capacity products precede allocation. Coarse lock-free
worker groups write disjoint (view, batch) slots. Their joined continuation
completes every view in reservation order and releases the seal before starting
graph construction and resource preparation. Terminal completion is deferred
before dispatch; worker failure cannot report completion while other writers
remain active. Empty scenes finalize every view without dispatching workers.
Setup and joined-worker failures both release the seal and fail the frame.
Multi-view planning is a 10.1 foundation, not a deferred 10.2 extension. Graph
publication ordering has an item 8 source implementation. Compilation and
executable verification remain deferred to Phase 10.3.

Correction pass (2026-09-09): CPU candidate queries translate the camera-relative
frustum into scene space once per view, using the GPU Scene publisher's configured
world-cell size. Translation uses double precision and rounds plane distances
outward when narrowed; unrepresentable queries fail preparation. The original
camera-relative planes remain in `GpuView`. The candidate dispatch stays visible
under `RenderFrame`, with graph preparation in its joined continuation.

Mesh residency now contributes shell/bin catalog records through its existing
shared GPU Scene contribution. Layout version 8 appends direct-index shell/bin
tables without changing earlier table indices. The batcher remains the CPU owner;
publication writes GPU records directly into mapped reservations. Dedicated ranges
cover the configured shell/bin limits and are initialized once, including inactive
slots. Later publications sort dirty indices into adjacent runs and write only
those runs, including removal tombstones and moved-bin ordinals. The complete
dirty catalog must fit the available shared upload budget; insufficient capacity
fails staging and preserves dirtiness rather than publishing inconsistent partial
shell/bin state. Catalog acceptance uses the exact revision; pre-submit failure
retries preserve changes. Shutdown cancels unpublished ranges or retires published
ranges through the shared lifetime manager. GPU consumer queue dependencies and
graph dispatch still belong to the remaining items.
Renderer shutdown drains the final catalog retirement epoch after producer
shutdown, using actual submitted RHI receipts and the existing device-idle boundary.

Item 4 source checkpoint (2026-09-09): visibility now validates same-index
`GpuRenderableResidency` against the immutable renderable generation and rejects
zero placement revisions. It computes per-view projected sphere coverage, applies
positive view LOD bias toward coarser levels, chooses the desired authored LOD,
and searches only toward coarser resident LODs after first proving the complete
anchor bit. Its compacted
16-byte result carries instance, renderable, selected global LOD and placement
revision; each result partition also carries the exact view-array index.
Residency/topology contract failures accumulate in the partition's rejected
counter instead of being reported as ordinary frustum or mask culls.

Primitive/phase expansion has count and scatter shader entries over CPU-bounded
per-view work ranges. Count writes one fixed-size item per visible instance.
The following prefix stage assigns each item a disjoint output interval before
scatter, so expansion has no global append atomic and cannot cross a view
partition. Both passes revalidate the selected resident LOD and placement
revision. Emitted work requires matching primitive-placement geometry generation,
phase-placement revision, normal or mirrored shell/bin generations, active
catalog ownership, shell phase/arena identity, bin shell/geometry identity and
exact geometry ranges. The current first-pixel path intentionally emits VMESH
default materials; component material-set overrides remain outside this
milestone because compatible pipeline placement must change with them.

The item 4 checkpoint defined shader ABI and algorithms only; graph resources,
prefix/capacity assignment, bin counts, final instance scatter and indirect
arguments were still open at that boundary.

Item 5 source checkpoint (2026-09-10): retained geometry frame work now carries
checked, disjoint per-view reservations for expanded work, direct-index bin
state, 16-byte `MeshDrawInstance` records and 20-byte indexed indirect
arguments. The expansion budget is configurable and defaults to eight emitted
primitive/phase records per visible instance. Requested, emitted, rejected,
invalid-bin and overflow counts remain separate for every view. An exhausted
reservation clamps each item or bin to zero writable records before scatter.

Expansion and bin prefixing use parallel 128-entry block scans. A second pass
prefixes only block summaries per view, and a resolve pass adds each block base.
There is no per-view append cursor and no CPU scan of entities. Bin count and
instance scatter use direct `(view, bin)` entries; their only contended operation
is the GPU atomic counter/cursor for the destination bin. Indirect generation
wrote one fixed direct-bin slot per view with exact `indexCount`, `firstIndex`,
non-negative `baseVertex` and instance-stream `firstInstance` at this checkpoint.
Item 6 replaces those direct slots with compact shell ranges (see below).
Item 7 owns render-graph allocation, descriptors and dispatch wiring for these
stages. No shader compilation, build or executable test was run.

Atomic correction source checkpoint (2026-09-10, portability correction
2026-09-11): bin count and instance scatter group the absolute frame-bin index
with standard wave ballots and lane reads. Each wave removes one matching key
set per iteration. Only that set's first active lane increments the count/cursor
by its population. Scatter broadcasts the returned cursor base with
`WaveReadLaneAt`; the population of matching lower-numbered lanes gives each lane
its unique consecutive slot. All four words of the ballot mask are handled,
including partial waves and mask word boundaries; no fixed 32/64-lane width is
assumed. Out-of-range and invalid lanes leave before matching. Invalid-bin
diagnostics use one wave-counted atomic per nonempty invalid group in the
view-owned work range.

Scatter reads only stable offset/capacity fields alongside the atomic cursor.
It checks the remaining capacity before adding the lane rank, retaining partial
reservations and the prefix pass's overflow accounting. Counters still require
initialization and ordered count/prefix/scatter graph dependencies; wave
aggregation does not replace inter-dispatch barriers. This path requires ballot,
lane-read and wave-count operations within the current SM 6.6 compiler profile;
it does not require the vendor-specific SPIR-V subgroup partition capability.
DXIL and SPIR-V compilation passed in 10.3.3. Native wave-width cases,
mixed-bin/partial-wave coverage and overflow execution remain deferred.

Contention audit before item 6 (2026-09-10): all seven `InterlockedAdd` sites in
`gpu_scene_visibility.vsl` were reviewed. No per-element shared-destination
atomic remains; distinct keys can still require one update for each active lane.
Visibility now skips zero visible/culled increments and initializes the shared
output base even when no reservation is needed. Scatter skips wholly unwritable
bins before matching/reserving. Their requested count and dropped-instance
diagnostics remain in `instanceCount` and the prefix-produced overflow counter;
the scatter cursor is not a diagnostic count.

| Atomic destination | Scope and maximum update rate | Remaining contention |
| --- | --- | --- |
| Visibility visible count | One nonzero reservation per 128-candidate group, per view | All visible groups share the view counter |
| Visibility culled count | One nonzero add per 128-candidate group, per view | Fully culled views still update this counter |
| Visibility rejected count | One nonzero add per 128-candidate group, per view | Invalid-content bursts can concentrate updates |
| Visibility overflow count | One nonzero add per overflowing group, per view | Capacity exhaustion can concentrate updates |
| Invalid-bin work count | One nonzero add per participating wave, per view | Invalid-content bursts can concentrate updates |
| Bin instance count | One add per distinct `(view, bin)` in each wave | Multiple waves can target the same bin |
| Bin scatter cursor | One reservation per distinct writable `(view, bin)` in each wave | Multiple waves can target the same bin |

The graph must zero visibility counters and bin items before their producers.
The expansion summary prefix initializes geometry counters (including invalid
bin count) before bin counting. Bin prefix resets scatter cursors and assigns
capacities before scatter. Ordered consumers read finalized counts/instances;
there is no cross-dispatch polling. Aggregate candidate/work reservations are
checked below the 32-bit invalid-index sentinel, bounding total counter updates
provided each planned range runs once. A zero-capacity bin now keeps cursor zero;
a partially writable bin still reserves the complete wave group and clamps each
write. This preserves the packed writable prefix and does not double-count drops.

Keep wave grouping as the current binning baseline, not as a claim of optimal
performance. Cross-wave shared-memory aggregation would need key matching,
scratch initialization and group barriers. Counter sharding/sorting would add
scratch or passes and complicate contiguous scatter ranges. Visibility could
use group-owned counts followed by prefix/scatter to remove its global
reservation, at the cost of another compaction stage and storage. None of those
tradeoffs has target-GPU timing evidence yet. The final 10.3 batch should measure
all-same-bin and all-distinct-bin distributions, fully visible/culled views,
invalid-content bursts and capacity exhaustion before choosing another level of
aggregation. See [the shader atomic rules](../../source/rendering/AGENTS.md).

Item 6 source checkpoint (2026-09-10): the joined candidate continuation now
prefixes live-bin capacities from the existing phase-indexed shell catalog for
each requested view/phase. The retained frame holds dense 32-byte shell range
records and small phase ranges. It copies only generation and frame addresses;
pipeline, arena, mesh and material ownership stays in the existing owners. The
CPU visits active shells, never bins or entities, and checks catalog revision,
view partition capacity and the RHI's 32-bit indirect byte-offset limits.
RenderingService passes the residency-owned batcher into FrameRenderer.

`InitializeGpuSceneGeometryShellRanges` scatters this dense upload into a GPU
lookup indexed by `(view, shell)` and zeros one 16-byte counter record per plan.
The graph must first clear the bounded lookup to generation zero. Unrequested
or unused slots therefore cannot retain a previous frame's mapping. This is
transient frame addressing, not a second persistent shell catalog.

`BuildGpuSceneGeometryIndirectArguments` now writes directly into the final
shell ranges; there is no intermediate direct-bin argument buffer or copy pass.
Only bins with writable instances participate. It validates published bin/shell
generations, phase, live-bin capacity and per-view argument/instance bounds.
The portable ballot grouping loop groups the dense shell-counter index, which
includes view identity. The leader reserves one span; matching lanes write
consecutive arguments using their lane rank. No fixed wave width is assumed.
Shells have independent counts, and argument order inside a shell is
intentionally unspecified for opaque draws.

| New atomic destination | Scope and maximum update rate | Initialization and consumer |
| --- | --- | --- |
| Rejected indirect bins | One nonzero add per participating wave, per view | Expansion prefix zeros; diagnostics consume after argument generation |
| Shell draw count | One reservation per distinct `(view, shell)` in each wave | Shell initialization zeros; counted indirect reads after argument generation |
| Shell overflow count | One nonzero add per overflowing matching group | Shell initialization zeros; diagnostics consume after argument generation |

Together with the previous seven sites, the shader now has ten aggregated
atomic sites. All-same-shell work uses one reservation per wave; distinct shells
use independent destinations. Invalid and zero-instance lanes leave before
reservation. Partial groups use remaining-capacity subtraction before adding
their rank. Draw count preserves requested arguments; writes and the RHI's
maximum draw count clamp to the shell capacity. With a stable valid catalog,
one visit per bin and capacity equal to live-bin count, shell overflow should
be zero. The counter still exposes violations. Cross-wave contention on a large
shell remains a 10.3 profiling case; source review does not establish timings.

`GeometryFrameWork::RecordPhase` is the recording body for the item 7 graph node.
It visits the borrowed phase catalog once, resolves bounded arena bindings,
binds the exact shell pipeline, static-surface context and whole instance stream,
and records one `DrawIndexedPrimitiveIndirectCount` per nonempty shell. Argument
and count byte offsets are CPU-known; visibility stays on the GPU. Absolute
`firstInstance` is paired with instance-buffer binding offset zero. Empty phases
return without buffer lookup or draw recording. A catalog revision change fails
recording; the existing resource-update join remains the synchronization owner.

Item 7 must allocate/upload the dense plans, allocate/clear the direct lookup,
run shell initialization and argument generation, and call `RecordPhase` from
the real depth/GBuffer nodes. Declare initialization-to-compaction UAV ordering,
bin-prefix/scatter-to-argument dependencies, instance UAV-to-vertex transition,
and argument/count UAV-to-indirect transitions. GPU Scene, arena, descriptor and
target uses must belong to those graph nodes. Shared publication queue waits
and retirement remain in item 8. No graph dispatch or geometry pixels are claimed
at this checkpoint; no build, shader compilation or tests were run.

Item 7 source checkpoint (2026-09-10): `BuildRenderGraphCamera` now builds a
shared `GeometryWork` command-list group and a per-camera `CameraGeometry`
group through the existing factory. The shared group is Unique and merges when
camera fragments compose. Its declaration node, uploads, clears and thirteen
compute entries therefore execute once for the whole prepared family. All
compute dispatches use that family's separate view partitions. The initial
implementation records compute on Graphics, preserving one existing submission
path; async Compute scheduling is not required for this milestone.

Two explicit declaration-only nodes precede their consumers:

- `DeclareGeometryWorkResources` declares the twenty named frame buffers and
  injects retained GPU Scene/arena imports in the shared flow space.
- `DeclareCommonResourceAllocs` declares each camera's depth, three GBuffer
  attachments and camera color in its own flow space.

They are the first children of the respective existing command-list groups.
Vanguard currently assigns planning writers to these groups, so these children
can declare resources while their `Execute` bodies stay empty. They create no
extra native command list, submission or planning service. Initialization is
separate: frame uploads and the visibility/bin/shell-map clears happen in
`InitializeGeometryWork`; shell counter initialization remains its shader entry.

A fixed compute-stage table declares each entry's SRV reads, UAV writes and
discard/preserve behavior. The existing allocator compiles inter-stage UAV and
state-transition actions. The native one-dimensional dispatch limit is checked
before candidate dispatch/allocation; oversized plans fail rather than truncate.
No new shader atomics were introduced in this item.

Each camera clears targets, records requested depth/opaque phases through
`RecordPhase`, visualizes GBuffer base color, and resolves the selected camera's
color into the acquired frame output. The existing terminal nodes submit and
complete/present the output transaction. Texture outputs are imported through
that same transaction, alongside the existing swap-chain import. A sole Primary
view selects the output; otherwise the first requested root camera selects it
(or view zero for a supplied family without view setup). Multiple Primary views
fail preparation. Other views retain independent targets and geometry ranges.
Visualization uses a fullscreen triangle with texture loads; it is not lighting.

The attachment contract is D32Float depth, R8G8B8A8UNorm base color, and
R16G16B16A16Float normal/roughness and emissive/metallic. Drawable preparation's
attachment signatures must match. Camera color and the visualization pipeline
use the acquired output's color format. Opaque depth remains writable so a
camera can omit the prepass; each cooked shell PSO still defines comparison and
write behavior, including reverse depth.

Renderer feature pipelines come from the existing cooked pipeline catalog,
not runtime shader compilation. Required names are the thirteen shader entry
names in `RenderNodeGeometryCompute`'s stage table, plus `VisualizeGBuffer`.
The latter uses `geometry_visualization.vsl`, a 16-byte push-constant block,
bindless resource access and the frame output attachment format. Missing
prepared pipelines fail resource declaration before GPU execution. Cooking
these fixtures and native validation remain in the final requested batch.

Resolved resource uses now expose scoped whole-resource SRV/UAV descriptors.
Resolve creates them for allocator-owned resources in the renderer's existing
bindless domain; it does not duplicate persistent owners' imported descriptors.
The execution generation owns their retirement behind submitted Graphics,
Compute and Copy receipts, including partial submission. Pre-publication failure
retires unused descriptors; abandonment conservatively covers submitted queues.
Explicit subresource-view descriptors are outside this whole-resource API.
Nodes cache pipeline borrows only, never frame descriptor indices or native
transient resource addresses.

GPU Scene maintains a dense buffer-handle catalog when pages materialize, so
import preparation visits existing buffers without scanning tables' unused page
slots. Arena imports are deduplicated by native identity while visiting requested
phase shells once. The retained frame stores only import names, identities and
read states; the allocator retains native ownership. No entity/bin scan or
per-instance retain/lock was added.

At the item 7 checkpoint, incoming GPU fences were still an execution gate:
`RegisterImport` rejected `ExplicitFenceWait`, preventing uploaded-scene frames
from reaching resource execution. Item 8 below supplies that missing source path;
uploaded-scene pixels still require the cooked pipelines and final validation.
Source review and whitespace checks only; no builds, shader compilation,
project generation, test writing or test execution were performed.

Item 8 source checkpoint (2026-09-10): retained buffer and texture imports now
preserve readiness and the producer fence. An explicit fence must match the
registered initial queue; repeated tokens must agree on both readiness and fence.
Same-queue continuation retains its existing queue contract. Copy handoffs require
Common state, and the imported initial state must be legal on the first consumer
queue. Final-state transitions and terminal queue checks remain in the allocator.

Resolve folds imported fence requirements into three maximum values per consumer
packet during the existing import-use traversal. Opening a native packet records
those requirements through `rhi::AddCommandListWait` on its bound command list,
before entry-state seeding. Each recorder owns its three values; recording adds
no shared wait registry, allocation, lock, entity scan or CPU fence wait. Discarding
a command list discards its requirements; recycled native lists get fresh metadata.

The existing `CommonBackend::SubmitCommandLists` combines requirements into a
fixed consumer/producer queue matrix under its existing submission lock. It
rejects fences beyond successfully signaled producer values before issuing work,
including same-queue requirements. Successful creation-upload signals contribute
to that validation too. Each distinct cross-queue pair emits at most one native
`ID3D12CommandQueue::Wait` per submission; same-queue waits are omitted because
submission order already satisfies them. Requirements can reference only prior
signals, so they cannot create a future-signal cycle inside the current submission.
Native wait failure occurs before command-list execution; existing submission
receipts continue to distinguish discarded work from submitted work that loses
its completion fence. Internal graph fork/join dependencies retain their existing
lowering and receipts.

GPU Scene currently uploads with `CopySync`, which maps to Graphics, and publishes
the actual returned fence. Its table, page and batch-catalog data therefore use
same-queue ordering in today's camera path. The new seam also accepts actual
Compute and Copy producer fences without pretending CopySync is asynchronous Copy.
GPU Scene buffers retain their Common creation state at command-list close through
NVRHI's existing `keepInitialState` behavior, matching the import contract. Frame
geometry metadata uploads and compute consumers remain ordered inside the graph;
arena admission still requires existing completed-upload residency evidence.

Transient descriptors remain with execution-generation resources and retire behind
actual submitted consumer receipts, including partial submission and abandonment.
Persistent imported material and GPU Scene descriptors stay with their existing
owners. No material-residency work moved into the allocator.

RED source evidence: `gpuApi/src/dx12/gpuApiDX12CommandList.cpp:1482-1496` selects
producer/consumer queues, skips identical native queues, and issues the queue wait
under the existing global submission mutex. Vanguard uses the explicitly supplied
RHI fence instead of selecting RED's current submit fence.

Validation is source review and whitespace checking only. The final 10.3 batch
must cover Graphics/Compute/Copy incoming waits, repeated-fence coalescing,
same-queue elision, malformed and unsignaled fences, legal Common-state Copy
handoffs, discarded/recycled lists, wait/signal failure, partial submission,
descriptor retirement, multi-view output, zero-work frames and cooked first pixels.
Existing tests that asserted unsupported explicit imports will need contract review
in that batch. No builds, shader compilation, project generation, test writing or
test execution were performed for item 8.

Build the smallest complete opaque path that already has the final CPU/GPU split.
There is no direct-draw fallback milestone.

1. Extend the existing `RenderGeometryBatcher` with a recording catalog view.
   Keep phase-indexed dense active-shell spans and generation-checked bin metadata,
   including each bin's owning shell and shell-local ordinal, in the batcher that
   already interns those objects. Expose coalesced dirty shell/bin indices for
   the existing GPU Scene publication and lifetime seam; the publication owner
   will copy those records without uploading the whole directory each frame.
   Update this catalog only with resource-time shell/bin acquire and release. Do
   not add a sibling PSO table, geometry registry or visible-object collector.
2. Prepare one bounded geometry-work package per retained render frame. It owns
   only frame-local views, candidate IDs, work ranges, counters and graph resource
   handles. Reserve separate candidate, counter and visible-output partitions for
   every prepared view, checking aggregate capacities before allocation. Share
   immutable GPU Scene and batch-catalog tables across those views. It does not
   copy mesh, material, proxy or drawable payloads. The
   existing `RenderUpdate` join keeps scene-owned accepted bindings immutable
   through candidate production and command recording; do not perform an atomic
   retain or a lock for every candidate. Use `RetainMeshDrawable` only for a
   handoff that actually outlives that joined scene boundary.
3. Feed the existing `GpuVisibilityPlanBuilder` from
   `PrepareGpuVisibilityCandidates`/`WriteGpuVisibilityCandidateBatch` and release
   every scene candidate seal through `CompleteGpuVisibilityCandidates`, including
   cancellation paths. Seal once for the family and build the spatial batch layout
   once; apply each view's own frustum and masks while workers write disjoint
   caller-reserved ranges. Join all writers before completing reservations in
   view order, releasing the seal or constructing the consuming graph. Keep
   results and failures per (view, batch); no second candidate array, per-item
   lock or shared atomic append cursor is introduced.
4. Correct `gpu_scene_visibility.vsl` to validate mutable
   `GpuRenderableResidency` and its generation. Select the desired resident LOD,
   with the complete anchor LOD as the fail-closed fallback, then expand its
   primitives and phase participations. Validate renderable, residency, geometry,
   shell, bin and placement generations before emitting work. Consume the work
   range's view identity for visibility and LOD, preserving its result partition.
5. Build bounded GPU stages for visibility, primitive/phase expansion, bin
   instance counts, prefix assignment, instance-record scatter and indexed
   indirect-argument generation. The 16-byte static-surface instance record is
   the produced draw stream; `firstInstance` addresses that stream. Capacity
   overflow increments an observable counter and drops work without writing
   outside a reservation. Expansion, bin counts, instance scatter and arguments
   must retain separate per-view output partitions from their first implementation.
6. Prefix the current phase's live-bin capacities to give every active shell a
   CPU-known bounded output range. GPU work compacts only visible bins into that
   range and writes its counted-indirect count. The recording node visits the
   batcher's phase-indexed dense shell span once per view, binds the shell pipeline
   and arena state once, and issues one
   `DrawIndexedPrimitiveIndirectCount` call for that shell. It does not scan scene
   entities, read visibility back, or issue one CPU command for every candidate.
7. Implement the first real camera graph using the existing graph cache,
   `RenderNodeImplContext`, render-flow allocator, command-list submission and
   output transaction. Add only the geometry-work compute nodes, real depth and
   GBuffer targets, opaque static-surface recording, and a minimal GBuffer
   visualization into the existing output/presentation tail. Do not create a
   private graph executor or submission scheduler. Reuse graph-cache topology;
   frame-varying counts and addresses belong to frame data, not graph structure.
8. Establish the narrow upload-to-consumer queue dependency for GPU Scene and
   batch-catalog buffers. Extend the existing imported-resource/submission seam if
   it needs an incoming fence; a CPU job dependency is not a GPU queue wait.
   Transient SRV/UAV/indirect descriptors remain owned and retired with their
   render-flow resources.

The 10.1 source gate is one cooked static mesh reaching real depth and color
targets through GPU visibility, GPU binning and counted indexed indirect draw.
Zero candidates and fully culled candidates must still complete and present.
The one-mesh fixture narrows content only; multi-view ownership and execution
remain required foundations throughout 10.1.
Compilation and executable proof remain in the final batched verification.

#### Phase 10.2 -- surface, batching and first-light coverage

Extend the same nodes and buffers; do not fork a more capable camera path.

1. Cover multiple LOD primitives and submeshes, shared geometry with different
   material parameter/texture values, and distinct generated graph programs.
   Preserve one instance record per emitted instance/primitive pair and exact
   `firstIndex`, `indexCount`, `baseVertex` and `firstInstance` addressing.
2. Route normal and mirrored placement through their published shell/bin pairs.
   Preserve mirrored front-face identity even for two-sided surfaces; cull-none
   does not make opposite winding states interchangeable. Only identical
   effective pipelines share shells.
3. Add depth and GBuffer phase participation for opaque and masked surfaces.
   Masked depth and GBuffer evaluation must use the same material closure and
   alpha decision. Keep translucency outside unordered opaque bins until its
   ordering contract is designed.
4. Add simple directional diffuse shading over the real GBuffer. Emissive
   accumulation is excluded by the current user instruction.
   Correctness is the immediate goal. PBR, a common shader-lighting API and mature
   composition require their own later studies and implementation. Keep the pass
   explicit and preserve correct resource, synchronization and per-view ownership.
   Directional light direction comes from the component transform and does not
   enter the spatial candidate index. Keep point and spot lights on their existing
   spatial bounds contract; a separate half-light registry or per-object lighting
   list is out of scope.
5. Exercise the multi-view foundation already established in 10.1 with distinct
   frusta, layer/visibility masks and LOD choices, including an empty or fully
   culled view beside a populated view. Verify isolation of counters and output
   ranges and graph reuse as per-view candidate counts change. Include explicit
   camera regions in one logical viewport, separate local render resolutions,
   deterministic output composition and producer/consumer camera dependencies.
6. Add bounded diagnostics for candidate count, visible instances, expanded
   primitive/phase work, active bins, emitted arguments, shell state changes and
   every overflow class. Diagnostic collection must not require a visible-set
   readback in normal frames.

10.2.1 source checkpoint (2026-09-10): vertex decoding and draw-layout admission
are corrected, and the existing multi-primitive/material/indirect path has been
reviewed at source level. No executable coverage or rendered result is claimed.

- `mesh_lod_definitions.cpp` derives normal/tangent encoding flags from semantic
  zero's stored VMESH attributes during the existing bounded layout traversal.
  The flags occupy existing `GpuGeometryRange::flags` bits, and the existing
  geometry-definition content key already hashes them. The record remains 64
  bytes; CPU and shader GPU Scene contract versions advance together to 9.
- `static_surface_prefix.vsl` restores packed 10-bit UNORM normal/tangent XYZ
  with `value * 2 - 1`, and tangent W with `0 -> -1`, `1 -> +1`, before the
  existing transform/normalization. Float and SNORM directions stay signed.
  Position scale/bias applies only when POSITION0 is quantized; unrelated extra
  position attributes cannot enable decoding of a float POSITION0. Half UVs
  continue to use vertex-input conversion. No per-vertex layout scan, extra
  descriptor, CPU vertex expansion or new shader-variant registry was introduced.
- `ValidateMeshDrawLayout` now requires both VG_DRAW fields in binding 15 with
  the existing 16-byte, step-one instance contract. Matching ordinary vertex
  attributes alone no longer admits a pipeline that cannot read indirect instance
  identity. Consumed static-surface position/normal/tangent/UV encodings must match
  the prefix's interpretation; unsupported integer or unsigned direction formats
  are rejected. Resource-time format/stride/offset checks remain allocation-free.
- LOD selection already uses per-view coverage/bias and both halves of the
  resident mask, choosing the desired or a coarser installed LOD. Expansion
  traverses that LOD's complete primitive span and each requested phase; count
  and scatter use the same placement/generation checks. Production residency
  still installs only the anchor LOD. Finer-LOD demand/installation policy was
  not added by this source pass; multiple resident-LOD fixtures remain in 10.3.
- Each emitted work/instance record carries its primitive's material index.
  Shells distinguish phase, native pipeline and arenas; bins distinguish physical
  geometry and indexed ranges, not material values. Compatible immutable material
  variants can therefore share geometry/bin state, while distinct program PSOs
  select different shells. Material table-page and generated texture/sampler
  accesses retain nonuniform descriptor indexing. Dynamic component material
  overrides remain outside this milestone, as described below.
- Uploads copy each submesh's vertex/index slices into their reserved arena
  ranges. Published `firstIndex` and `baseVertex` are destination arena offsets;
  CPU binding uses byte offset zero and indirect arguments apply those offsets
  once. `firstInstance` selects the GPU-produced per-view instance-stream range.
  The recording caller now separates descriptor queries, arena resolution,
  binding, pipeline setup and counted-draw calls from their success checks.

Deferred verification cases: packed and float layouts side by side; negative
normal/tangent components and both tangent signs; missing VG_DRAW fields and
unsupported encodings; multiple submeshes at nonzero arena offsets; 16/32-bit
indices; shared geometry with different parameter/texture values; distinct graph
programs; multiple resident LODs including bits 31/32; and bounded expansion
overflow. Cooked fixture pipelines must match their VMESH layouts and include the
updated shader contract. No builds, shader compilation, project generation,
test writing or test execution were performed for 10.2.1.

10.2.2 source checkpoint (2026-09-10): mirrored/two-sided orientation has been
reviewed through publication, placement, effective PSO state and surface shading.
Source corrections are complete; executable and rendered proof remains in 10.3.

- GPU publication now extracts the forward quaternion from the proxy's basis
  rows, matching the RED-derived `Matrix::ToQuat` signs in
  `source/math/adapted/src/matrix.cpp`. Previously the difference terms produced
  the conjugate rotation. This corrects rotated ordinary and mirrored meshes;
  decals share the corrected helper. The existing quaternion/scale representation
  assumes orthogonal TRS and does not add support for general affine shear.
- The existing negative-scale flag is published using scale-sign parity, avoiding
  scale-product overflow/underflow. GPU expansion selects the retained normal or
  mirrored shell/bin pair with that flag; vertex tangent handedness now consumes
  the same flag through the shared shader header. Layout version and record sizes
  are unchanged. Collapsed scales (including the existing decomposition epsilon)
  and nonfinite decomposition outputs keep the mesh slot inactive, so normal
  transformation cannot divide by a zero published scale. A later valid transform
  uses the existing dirty publication path to make the slot eligible again.
- The existing pipeline factory disables culling for the VMESH two-sided flag
  and also preserves authored cull-none materials. Mirroring flips front-face
  winding in either case. Effective PSO keys already retain this difference:
  `SV_IsFrontFace` still matters with culling disabled. Shell/bin sharing remains
  keyed by actual compatible pipeline and geometry state, with transactional
  acquisition, balanced references and rollback through existing owners.
- Signed inverse-scale normals, forward-scale tangents and orthogonalization
  remain in the vertex path. Tangent W combines the authored sign with the
  published mirror sign. Depth and GBuffer material evaluation receive the same
  geometric normal; the GBuffer orients the completed normal-map result once for
  the back side. No new material evaluation convention or lighting API was added.
- RED's `renderGeometryBatcher.cpp::DrawMeshInstances` halves index counts only
  for its doubled-index two-sided content convention. Vanguard retains ordinary
  VMESH index ranges and uses rasterizer cull-none; no index duplication or
  halving is introduced. No new scene owner, pass, per-candidate lock, scan,
  atomic operation or CPU visibility readback was added. Relevant technique,
  acquisition and release calls are separated from their result checks.

Deferred 10.3 cases: rotations exercising both quaternion extraction branches;
one, two and three negative scale axes; rotated nonuniform mirrored scales;
transform sign changes on the same instance slot; collapsed-to-valid recovery;
front/back views of authored and VMESH-forced two-sided surfaces; both tangent
signs with a non-flat normal map; matching depth/GBuffer coverage; effective PSO
sharing/separation and partial acquisition rollback. Shader compilation, cooked
fixtures, native image checks, test writing and execution remain deferred.

10.2.3 source checkpoint (2026-09-10): opaque/masked shader entries, technique
program cooking/closure and camera-phase state admission are implemented at
source level. No shader compilation, test authoring/execution or rendered proof
was performed; those checks remain in the explicit final 10.3 batch.

The existing phase identities are retained. Both opaque and masked materials
participate in `vanguard.render.opaque`, plus `vanguard.render.depth_prepass`
when authored. Masking changes the cooked shader/PSO selected by the technique,
not the candidate registry, phase node, material record or GPU binning algorithm.

| Surface | Depth fragment entry | GBuffer fragment entry |
|---|---|---|
| Opaque | `StaticSurfaceDepthMain` | `StaticSurfaceGBufferMain` |
| Masked | `StaticSurfaceMaskedDepthMain` | `StaticSurfaceMaskedGBufferMain` |

- Both pairs use `StaticSurfaceVertexMain`. Opaque GBuffer ignores alpha for
  coverage; opaque depth performs no material loads/evaluation. The annotated
  material contract remains available to reflection even for that empty pixel
  body. Existing cooks that relied on clipping in the old generic entries must
  select the masked pair and be recooked.
- Masked depth and GBuffer use the same generated `EvaluateStaticSurface`,
  material identity, inputs and `ApplyStaticSurfaceCoverage` helper. Coverage is
  `clip(baseColor.a - opacityCutoff)`; equality survives. Both passes evaluate
  coverage, since a camera may omit the prepass. There is no early-Z-only masking
  optimization, per-pass threshold, texture-LOD override, dither or alpha blending.
  Compiler elimination of unused depth outputs is expected, not measured.
- A blocking material integration restriction was corrected: techniques formerly
  had to contain the exact primary VSHADER, preventing distinct depth/GBuffer
  fragment programs. VMAT cook and loaded-closure validation now admit different
  technique shader resources with the exact same material domain, material layout
  and graph permutation. Every material-bearing stage must agree; a pipeline
  cannot hide an incompatible stage alongside a matching one. Actual shader
  resource, permutation, binding and pipeline-interface fingerprints remain
  validated against each pipeline's retained dependencies. References to the
  primary shader still require its exact loaded generation.
- `MaterialCanonicalTechnique` can optionally name a program input, shader output
  and entry-point span. The existing canonical builder serializes those variants
  from the same finalized graph source, IR permutation, defines and settings;
  program byte spans are published only after storage stops growing. The combined
  program-input byte budget is bounded. `Programs()` returns the primary program
  and variants in deterministic technique order; `Program()` still identifies
  the primary. Submit all `Programs()`, then the existing pipelines and material.
  Variant requests use the existing artifact compilers and dependency graph.
  No VMAT wire change or new residency owner is needed; the material compiler
  policy fingerprint advances to invalidate old cached compilation policy.
- Existing topology preparation rejects blended/logic-op, alpha-to-coverage,
  multisampled, non-depth-writing and incompatible attachment state in these two
  camera phases. Both use inclusive depth comparison so GBuffer works with or
  without prepass coverage. Authored depth/GBuffer pairs must agree on depth
  direction, front-face identity, depth clipping and effective culling. Checks
  occur in the existing bounded resource-time technique traversal, never per
  entity/candidate or while recording draws. Other phase keys remain independent.

Reference evidence inspected locally:

- RED `common/renderer/src/renderMaterial.cxx::GetRenderPassParamsKey` publishes
  `isMasked` in the batch key; `renderGeometryBatcher.cpp::BuildDiscardFlags` and
  its recording caller propagate explicit discard classification. Vanguard's
  existing cooked PSO identity supplies this distinction without another bin flag.
- Unreal `Engine/Shaders/Private/MaterialTemplate.ush` defines
  `GetMaterialCoverageAndClipping`, used by `DepthOnlyPixelShader.usf` and
  `BasePassPixelShader.usf`. Unreal's base-pass skip is conditional on
  `EARLY_Z_PASS_ONLY_MATERIAL_MASKING`; Vanguard does not assume that precondition.
  `Renderer/Private/DepthRendering.cpp` avoids unnecessary pixel work when a
  material writes every pixel. Its advanced coverage/dither policies are not
  prerequisites for this first-scene path.

Deferred 10.3 proof: all four entries on DXIL/SPIR-V; complete material reflection
from the opaque depth entry; one VMAT retaining distinct depth/GBuffer programs;
canonical variant request ordering, duplicate identities and combined-byte limit;
wrong graph permutation/layout/domain and stale-generation rejection; opaque
alpha below cutoff; masked samples below/equal/above cutoff, including textured
cutouts viewed against geometry behind them; mirrored/two-sided masks; invalid
phase state and mismatched depth direction rejection; prepass enabled/disabled.
Recipes must choose the matching entry pair and a depth comparison compatible
with the view. ABI agreement alone cannot prove arbitrary custom shader coverage
equivalence; native fixture checks must verify the actual cooked programs. The
existing pipeline fixtures with default depth testing/writing disabled must be
updated during that authorized batch. Per-view depth-direction integration
continues in 10.2.5; transparent sorting, PBR and advanced coverage remain outside
this subphase.

10.2.4 source checkpoint (2026-09-10): simple directional diffuse lighting is
implemented at source level. The user's scope correction explicitly excludes
emissive accumulation. Compilation, fixture cooking, test authoring/execution and
rendered proof remain deferred to the authorized 10.3 batch.

- Shaded camera graphs now run the explicit `RenderNodeDirectionalDiffuse` after
  GBuffer, then resolve `CameraColor` into the frame output. GBufferOnly keeps its
  visualization path through the same camera graph builder. The graph renderer
  revision advances to 3. No new camera graph executor or scene-light owner exists.
- `directional_diffuse.vsl` computes linear, unshadowed Lambert diffuse:
  `baseColor * sum(light.color * light.intensity * saturate(dot(N, -direction))) / pi`.
  Direction follows emitted rays, so surface-to-light uses its negative. Normal
  data is signed world-space, renormalized after GBuffer storage. Cleared zero
  normals identify background/masked holes; no depth reconstruction or descriptor
  is required. Emissive/metallic and roughness are not lighting inputs. There is
  no ambient, specular, shadow evaluation, exposure or tone mapping in this pass.
- Per-view selection runs during existing retained frame preparation under the
  active family scene seal. It examines exclusive Global spatial membership once
  for the family, checks directional kind, enabled/query flags, nonblack color,
  positive intensity, and each view's layer/visibility masks. One family read guard
  covers collection; no per-light locking, scene-capacity scan or copied payload
  is introduced. Light payload slots borrow their original GPU index/generation
  at admission, following the existing mesh-index pattern. The GPU publisher
  continues to own allocation and fence-deferred retirement.
- `GpuDirectionalLightSelection` stores count plus at most eight index/generation
  pairs (80 bytes per view). Overflow fails preparation instead of truncating
  lights. This is an explicit first-light workload budget, not an unlimited shader
  loop. The shader bounds-checks table/page addresses and validates generation,
  active state, visibility mask and directional type before reading contribution.
  Disabled or zero-light views write black. GBufferOnly does not collect lights.
- Global proxies now occupy their own exclusive membership array within the
  existing spatial WriteIndex, rather than sharing the out-of-range bounded
  array. Insert, swap removal, global/local moves, validation, CPU visibility and
  GPU candidate traversal all use the same membership. This avoids scanning
  unrelated out-of-range objects to find directional lights; it is not a second
  light registry. Scene serialization and generational slot ownership are intact.
- Selection storage is part of the existing frame allocation and uploads once
  as `Lighting.DirectionalSelections` through the shared initialization node.
  The lighting node declares that buffer, the two GBuffer reads, retained GPU
  Scene reads and CameraColor write. Existing upload waits, graph transitions,
  submission and retirement cover the pass. No new atomic operations are used.

Current renderer pipeline recipes (superseding 10.1's shared visualization/output
PSO and output-format CameraColor) must provide:

| Catalog name | Shader and fragment entry | Color target | Push bytes |
|---|---|---|---|
| `DirectionalDiffuse` | `directional_diffuse.vsl`, `DirectionalDiffuseFragmentMain` | R16G16B16A16Float | 32 |
| `VisualizeGBuffer` (GBufferOnly) | `geometry_visualization.vsl`, `GeometryVisualizationFragmentMain` | R16G16B16A16Float | 16 |
| `ResolveCameraOutput` | `geometry_visualization.vsl`, `GeometryVisualizationFragmentMain` | acquired frame-output format | 16 |

Each uses its matching fullscreen vertex entry, triangle-list topology, no
vertex streams, no culling, no depth/stencil testing or writes, disabled blending,
one fully writable color target, sample count one, and the existing bindless
resource domain. Readiness requires the mode's color producer plus output resolve
and the existing geometry compute pipelines. CameraColor is linear HDR; resolve
currently copies RGB. An UNORM output clamps values outside its range; display
exposure/tone mapping is a later composition study, not silently added here.

Deferred 10.3 cases: DXIL/SPIR-V selection stride and constant reflection; native
pipeline/attachment compatibility; front-facing, perpendicular and back-facing
Lambert response; light rotation and intensity/color edits; zero/disabled/deleted
lights and slot reuse across frames; per-view mask isolation; eight lights and
ninth-light overflow; global-only and mixed global/out-of-range/cell traversals,
swap removals and point/directional transitions; masked/two-sided geometry;
GBufferOnly behavior; background clear and HDR values above one. Executable GPU
lifetime/performance proof is still required; this source review does not provide
it. The existing emissive GBuffer channel remains part of the material ABI but is
not bound or accumulated by the new lighting pass.

The 10.2 source gate covers the complete first-scene fixture: multiple static
meshes/submeshes, shared geometry with different materials, one nontrivial graph
material, mirrored/two-sided and masked surfaces, camera movement, more than one
view, and one directional light. Exact argument and sampled-image validation is
deferred to 10.3 with the rest of the requested test batch.

#### 10.2.5 source checkpoint -- camera regions and dependency integration

Source implementation is complete; executable verification remains deferred to
10.3. No projects were generated, shaders compiled, or tests written/run in this
pass. Runtime performance and rendered correctness are not established yet.

`RenderFrameViewSetup::outputRegions` selects root cameras and destination pixel
rectangles in the existing logical viewport output. `ConfigureViews` validates
and copies the bounded array into the existing frame packet; it retains no caller
span. One camera has at most one output region per frame. Rectangles must be
nonempty and wholly inside that frame's output extent. Overlap is allowed:
later entries overwrite earlier entries. An empty region list preserves the
single-primary full-output path, with primary selection restricted to explicit
roots when roots are supplied. Multiple primaries require explicit regions.
Dependency cameras do not implicitly become viewport outputs.

For example, with a 1920 x 1080 logical output:

```cpp
RenderCameraHandle roots[]{leftCamera, rightCamera};
RenderCameraOutputRegion regions[]{
    {leftCamera,  {0,   0, 960, 1080}},
    {rightCamera, {960, 0, 960, 1080}},
};
RenderFrameViewSetup setup{scene, {roots, 2}};
setup.outputRegions = {regions, 2};
const bool configured = viewport.ConfigureViews(frame, setup, &failure);
// Check configured before submitting the frame through its existing owner.
```

Camera preparation scales region edges into the internal render extent before
applying `InheritFrame` or `ScaleFrame`. Fixed camera resolution stays fixed.
Automatic projection aspect follows the destination region, including when a
fixed render resolution uses a different pixel aspect; an explicit projection
aspect remains authoritative. Targets and GPU raster coordinates stay local to
(0, 0). Output placement is not a culling-space offset. A changed render extent
or projection scale cuts the existing camera history. Callers attaching an
already prepared family remain responsible for preparing its view dimensions
and projections, using the same region-aware preparation request when needed.

The family still has one shared geometry initialization/compute branch and
separate visibility, LOD, expansion, bin, instance, argument and shell-counter
ranges per compact view ordinal. Directional selections remain per view. Camera
targets stay in their existing resource flow spaces. No new batcher, snapshot,
material owner, entity scan, per-candidate lock or shader atomic was introduced.
The added metadata is bounded by the existing 32-view family limit.

`ComposeCameraOutput` is one unique command-list node merged after every camera
branch. It declares the selected cameras' `CameraColor` textures through
`RTCameraNameTag`, clears the shared output once to black, and records each copy
with its destination viewport and scissor. This preserves previous regions,
defines uncovered pixels and avoids concurrent writers to one output. It reuses
the existing `ResolveCameraOutput` pipeline and copy shader. GBuffer visualization
now has its own node without the former output-selection boolean. Camera passes
still record in parallel; only the small output composition is ordered.

Producer/consumer edges are now linked into the existing graph GPU dependency
domain. A single bounded graph-node pass finds camera command-list endpoints
when rebuilding the graph; there is no per-entity search. The begin/end camera
dependency scope holds child `CameraColor` resources through the consumer command
list. `Color` and `Final` currently resolve to the same simple camera result;
future post-processing must distinguish those producers. Actual reflection or
portal sampling is a separate renderer feature and is not supplied by this scope.

RED evidence: `renderRenderFrame.cpp` surrounds camera work with
`CRenderNode_CameraResourceDependencyScope` (for example lines 1986 and 3119).
`renderGraphNodes.cpp:1494` opens/closes selected camera color resource scopes
using the producer's resource flow space. Vanguard uses the corresponding scope
in its existing declaration/execution split; it does not add another scheduler.
Those files are under
`D:/root/R6.Root/Mainline/dev/src/common/renderer/src/`.

Cached graphs retain topology only. Region order/coordinates, compact output
selection, descriptors and actual extents are consumed from the current retained
frame during resource planning/recording. View dimensions, phases and dependency
edges already participate in graph keys. The renderer revision is now 4. The
unconditional failure after an implemented camera graph builder was corrected;
unimplemented empty builders still fail explicitly.

Depth convention is checked against the admitted shell's immutable PSO metadata
during existing shell planning, before dispatching the graph. A mismatched
forward/reverse view fails with a specific preparation error. This is a
conservative catalog-wide check for participating phases, even if a shell would
be culled. Mixed depth conventions using the same material still require matching
PSO variants; this pass does not synthesize them. Empty candidate completion also
avoids null-pointer arithmetic, and directional input descriptors are validated
and converted explicitly to GPU indices.

Deferred 10.3 cases: same/different-sized camera graphs; two roots sharing one
output; odd output/internal extents; fixed resolution with region-derived aspect;
gaps, overlap order, nonzero scissors and invalid rectangles; one empty or fully
culled view beside a populated view; masks/phases/lights and resident LOD choices
that differ per view; changing candidates/region order without topology rebuild;
resize and camera history cuts; dependency chain/fan-out and aliasing lifetimes;
default-root selection with a primary child; disabled/stale output cameras;
forward/reverse mismatch failures; cache reuse, final output receipts and
retirement across frames. Production finer-LOD streaming remains unchanged.

The following checkpoint implements 10.2.6; executable validation remains in 10.3.

#### 10.2.6 source checkpoint -- bounded geometry diagnostics

Implemented opt-in `RenderFrameFeatures::geometryDiagnostics` through the existing
frame/graph/renderer owners. The optional `GeometryDiagnostics` compute pipeline
reduces existing counters into one 64-byte record per view, using disjoint shell
traversal and group-shared reduction with no new GPU atomics. One graph copy sends
at most 2048 bytes into a three-slot readback ring. Disabled frames have no
diagnostic graph work; full rings skip samples without waiting.

CPU candidate counts reuse the existing completion loop. Pipeline-bind counts
publish once per phase. Reports identify frame, viewport, scene and view, and
separate planned CPU work from emitted GPU work and all existing overflow counts.
`RenderingService::PollGeometryDiagnostics` provides the agnostic consumer API;
the editor does not gain renderer resource ownership. Exact copy-scope receipts
gate nonblocking readback. Lost completion evidence quarantines slots, and
joined renderer teardown releases them through RHI retirement.

See [geometry diagnostics](../../source/rendering/docs/geometry-diagnostics.md)
for the optional cooked-pipeline hookup, field semantics, cost/lifetime contract,
RED evidence and deferred cases. Renderer graph revision is 5. Source and
whitespace review only: no shader compilation, builds, project generation or
test creation/execution was performed. Runtime correctness and overhead remain
unverified until 10.3. The next phase is 10.3 lifecycle and batched validation.

#### Phase 10.3 -- lifecycle, performance and batched verification

The 2026-09-11 user-approved sequence starts with compilation only. The editor
thread is paused during this build pass. Tests, smoke/stress/soak programs and
benchmarks are excluded from the build targets and must not be written or run.
Do not launch the editor/runtime or render a fixture during this pass. The
lifecycle and performance requirements below remain outstanding afterward.

| Compile subphase | Scope | Status |
| --- | --- | --- |
| 10.3.1 | Toolchain, production target inventory, Premake generation and source inclusion | Complete, 2026-09-11 |
| 10.3.2 | Debug engine libraries and their production dependencies; fix C++ compilation failures | Complete, 2026-09-11 |
| 10.3.3 | Required standalone and generated material shader entries/variants through the existing compiler, DXIL and SPIR-V | Complete, 2026-09-11 |
| 10.3.4 | Link runtime, editor and production tools, including normal dependency deployment | Complete, 2026-09-11 |
| 10.3.5 | Development, Profile and Shipping production build matrix and exact result ledger | Complete, 2026-09-11 |

##### 10.3.1 preparation checkpoint

`generate-vs2022.bat` completed with exit code 0. The existing engine-service
source audit passed. Its sole initial blocker was a redundant source-lineage
phrase in `render_geometry_batcher.hpp`; the comment now points to ownership
details. The existing geometry-batcher port document retains the lineage.
No behavior or audit rule was changed.

The generated solution is `build/projects/vs2022/REDVanguard.sln`. Production
application roots are `runtime`, `editor`, `nanovanguard` and `bootstrapImage`.
Also include the production compiler libraries `shaderTools`, `materialTools`,
`meshTools`, `textureTools` and `gameInputTools`. Their generated project-reference
closure contains 87 projects; adding SDL3's solution-level dependency covers all
88 production projects. No verification project appears in that closure.

Use explicit production targets, with serialized outer MSBuild invocations
(`/m:1`), rather than the solution's default Build target, which also includes
verification programs. SDL3 is a Makefile wrapper around its repository-contained
native Visual C++ project; `platformWindows` and `windowSdl` depend on it at
solution level. Preserve that dependency when building individual project files
(build SDL3 first), or use explicit solution targets. Its existing nested command
uses `/m`; do not mistake outer serialization for disabling compiler parallelism.

Local prerequisites verified by file inspection/version query:

- Visual Studio 2022 Community at
  `C:/Program Files/Microsoft Visual Studio/2022/Community`;
  MSBuild `17.14.40.60911`, default MSVC `14.44.35207`, generated toolset `v143`.
- Windows SDK `10.0.26100.0` headers and x64 D3D12/UCRT libraries are present.
  Generated projects request SDK `10.0` (installed-version selection).
- Repository Slang headers/import library/compiler and support DLLs, DXC/DXIL
  DLLs, and Oodle/NVTX import libraries/runtime DLLs are present.
- SDL3's native `VisualC/SDL/SDL.vcxproj` is present.

All 88 production projects contain Debug, Development, Profile and Shipping x64
configurations. Their explicit compile/header/project-reference input paths
exist, and no production `ClCompile` item points into a tests directory. All
current `src/*.cpp` files, including nested sources, were matched against the
generated rendering (55), entities (19), engine (15), shaderTools (2),
materialTools (6) and editor framework (4) compile items. Geometry diagnostics,
frame work, graph nodes and concrete scene components are included.

Rendering `.vsl` files remain `None` project items: C++ build success will not
establish shader compilation. In 10.3.3, generate static-surface programs through
the material compiler before compiling them; prefix/suffix fragments are not
standalone programs. No C++ or shader compilation, linking, test authoring,
test execution or application launch was performed in 10.3.1. Tool presence and
project generation do not establish successful compilation or runtime behavior.

##### 10.3.2 Debug engine-library checkpoint

The generated `engine.vcxproj` and its complete project-reference closure build
successfully in Debug x64 with outer MSBuild parallelism fixed at `/m:1`.
`rendering.lib`, `entities.lib` and `engine.lib` were produced. No test, smoke,
stress, soak or benchmark project was built or run, and no application was linked
or launched.

The compile pass corrected source contract drift rather than weakening warnings
or build policy:

- GPU Scene declares table ordinals 0 through 21; its table-count assertion now
  agrees with the 22-entry enum and the shader-side geometry-shell/bin ordinals.
- Drawable topology validates a stored phase ordinal before explicitly narrowing
  it to the registry's compact `u8` ID. The registry call and result check remain
  separate. A shadowing local in the publication loop was renamed.
- Directional-light output storage is mutable in the implementation, and its
  visibility tests use the public flag operators available in that translation
  unit.
- Entities now declares the window include path required by rendering's public
  viewport header. Project generation was rerun successfully after the Premake
  dependency correction.
- New component/runtime sources include the assertion contract directly, use
  `VG_ASSERT_MSG` when supplying a message, and construct the phase span from its
  typed data pointer.
- The rendering-service implementation qualifies the engine-wide `u64` type from
  its anonymous namespace.

This checkpoint proves Debug C++ compilation of the engine libraries only. Shader
program compilation is recorded separately in 10.3.3 below; native backend and
application linking remain 10.3.4; other configurations remain 10.3.5.

##### 10.3.3 shader-compilation checkpoint

A generated-build-only driver used the production `ShaderCompiler`, material IR
builder and material Slang generator. It compiled every required program with
warnings as errors for both DXIL and SPIR-V. No test target was built or run.

- All 13 GPU Scene visibility, expansion, scan, binning, scatter and counted
  indirect compute entries compiled as their own compute artifacts from the
  shared source module.
- Geometry diagnostics compiled as compute. Fullscreen copy, geometry
  visualization and directional diffuse compiled as vertex/fragment programs.
- Generated static surfaces compiled with parameter-only and sampled material
  graphs. Each graph covered opaque GBuffer, opaque depth, masked GBuffer and
  masked depth fragment entries paired with the static-surface vertex entry.
  Probe reflection and material-source finalization used the existing material
  compiler path, and every final program exposed its material contract.

The pass found and corrected two shader-source issues. The visibility output is
now explicitly initialized before a conditionally executed resolver, satisfying
Slang definite assignment without changing the visible write condition.
`WaveMatch` was rejected by the SPIR-V target because it requires the
vendor-specific `spvGroupNonUniformPartitionedNV` capability. Destination
aggregation now uses standard subgroup ballot and lane-read operations while
retaining one atomic reservation per distinct destination represented in a wave.

The temporary driver links only against already built production libraries and
resides under generated `build/shader-compile-check`; no production compiler or
runtime ownership path was added. Its link required explicit MSVC 14.44 selection
because the local `v143` default file selects 14.38 while existing objects expose
14.44 STL helper references. Production target toolset consistency remains a
10.3.4 link-pass check. Application linking and launch, shader execution, native
wave-width behavior, image output and performance remain unverified.

##### 10.3.4 production-link checkpoint

The complete Debug x64 production inventory builds and links with the generated
projects' normal MSVC 14.38 toolset. SDL3 was built first. `Vanguard.exe`,
`VanguardEditor.exe`, `nanovanguard.exe` and `bootstrapImage.exe` then linked
successfully with serialized outer MSBuild execution. The production
`shaderTools`, `materialTools`, `meshTools`, `textureTools` and `gameInputTools`
libraries and their project-reference closures also built successfully. This
resolves the mixed-toolset concern from the temporary 10.3.3 driver: a full
default-toolset rebuild is self-consistent, so no machine-specific MSVC version
was pinned in Premake.

The link pass corrected four production build-contract failures:

- `mesh_draw_layout.hpp` now exposes only the system scalar types it stores and
  forward-declares mesh and pipeline files. Full mesh, pipeline and RHI contracts
  moved to its implementation, avoiding transitive rendering-header coupling.
- Runtime explicitly includes the public shader, input, game-input, pipeline,
  pipeline-cache, texture and material roots exposed through its engine-service
  headers. It suppresses C4324 for the intentionally aligned geometry frame type,
  matching the focused rendering/editor policy.
- `bootstrapImage` implements the current package-artifact callback signature,
  keeps each artifact-set origin and rejects reads for a mismatched origin.
- Windows compilation retains `/MP` and now adds `/FS`, allowing parallel
  translation units to coordinate writes to each project's shared compiler PDB.
  Outer project builds remain serialized with `/m:1`.

Normal post-build deployment produced SDL3, NVTX and the Debug Oodle DLL beside
the runtime; those files plus Slang, GLSLang, GLSL module, DXC and DXIL DLLs beside
the editor; and NVTX/Oodle beside `bootstrapImage`. No test, smoke, stress, soak
or benchmark target was built or run. No executable was launched. Development,
Profile and Shipping production builds remain 10.3.5.

##### 10.3.5 production-configuration matrix checkpoint

The complete Development, Profile and Shipping x64 production matrix builds and
links with the generated projects' normal MSVC 14.38 toolset. The directly
confirmed roots in each configuration are SDL3, runtime, editor, `nanovanguard`,
`bootstrapImage`, `shaderTools`, `materialTools`, `meshTools`, `textureTools` and
`gameInputTools`. All thirty target/configuration builds completed successfully
with serialized outer MSBuild execution.

Artifact verification found all 22 expected production files in every
configuration: the runtime executable and its SDL3, NVTX and Oodle DLLs; the
editor executable and its SDL3, NVTX, Oodle, Slang, GLSLang, GLSL module, DXC and
DXIL DLLs; the `nanovanguard` executable; the `bootstrapImage` executable with
NVTX and Oodle; and the five production tool libraries. No test, smoke, stress,
soak or benchmark target was built or run, and no executable was launched.

This closes the compile-and-link portion of 10.3. Lifecycle, executable, image
correctness and performance verification remain deferred below, so Phase 10 is
not complete.

The 2026-09-11 [world execution and rendering integration study](world-rendering-integration-study.md)
audits the production application path and the assembled cooked fixture against
RED. It proposes 10.4.1–10.4.7 for source/session ownership, renderer bootstrap,
world draw contracts, camera binding, viewport supply, draining and executable
proof. These are implementation plans, not completed integration checkpoints.

##### Deferred lifecycle and executable verification

Close the production path rather than adding render features.

1. Connect graphics/compute/copy receipts from actual visibility and draw work to
   the existing readiness and retirement cutover. Exercise replacement, clear,
   removal and stale completion while earlier GPU submissions still reference the
   old geometry/material closure.
2. Close empty frames, resize, minimize/restore, scene and camera destruction,
   cancellation before and after graph dispatch, capacity failure, device
   removal, allocator abandonment and terminal shutdown. Every opened scene seal,
   retained output transaction and transient descriptor must have one terminal
   path.
3. Keep CPU work proportional to views, spatial candidates and active shells.
   Keep GPU work proportional to candidates and emitted primitive/phase work.
   Clear only current bounded shell/bin extents or use frame stamps; do not sweep
   entity capacity, take a per-item mutex, allocate hidden growable arrays, or
   synchronize the CPU on GPU counters.
4. Record CPU candidate/build/recording time and GPU visibility/binning/draw time,
   together with high-water marks and overflows. Soak scene churn and camera
   motion long enough to expose catalog generation reuse and retirement errors.
5. Write the deferred focused tests, regenerate Premake projects if source lists
   require it, compile the affected target matrix serially, run the exact
   argument/count and image-sample fixtures, then run lifecycle and soak coverage.
   Report affected-target success separately from unrelated whole-solution
   failures.

All three subphases are required to call Phase 10 complete. Phase 10.1 is the
first real scene-on-screen milestone, but it does not satisfy the broader
surface, lifecycle or performance gates by itself.

## 6. Integration gaps the proof must not hide

This is the historical pre-10.1 gap inventory. Later implementation checkpoints
above address several entries; do not treat every item here as still missing.
Use the [2026-09-11 integration audit](world-rendering-integration-study.md) for
the current application-to-world execution gaps and retain the deferred
validation obligations separately.

- **Visibility ABI:** `gpu_scene_visibility.vsl:118` tests bit 0 of immutable
  renderable flags as residency. Current topology writes `GpuRenderableFlags`
  independently and residency has its own table. Replace this stale assumption
  with generation-checked residency/placement consumption in Phase 10.
- **Upload-to-consumer ordering:** graph imports currently reject `incomingWait`
  and anything except `SameQueueContinuation` (`render_flow_resource_allocator.cpp`
  around lines 709/775). A completed CPU job is not a GPU queue dependency.
  Initial resident admission may rely on a genuinely completed upload fence;
  continuing mutations must be ordered before readers using supported queue
  semantics. Do not tag Copy-produced resources as a same-queue continuation.
  If explicit imported waits are needed, coordinate that narrow seam with the
  allocator/graph owner; do not build a private submit/wait scheduler.
- **Draw-to-retirement ordering:** 9B.2 now connects normal `RenderUpdate`
  maintenance to `SealResidencyRetirements` after the render tail joins. RHI
  receipt capture retains actual graphics/compute/copy submission values and
  explicitly identifies never-used queues. Drawable closures use existing GPU Scene retirement
  to pin leases. 9D now retains the drawable in RenderScene, preserves accepted
  ownership through bind/clear cutover, and exposes an owner-thread recording
  retain. Phase 10 consumes the accepted binding under the joined scene/recording
  boundary; the explicit retain is reserved for work that really crosses that
  boundary instead of being paid once per candidate. This remains source
  implementation, not execution proof.
- **Transient descriptors:** allocator-resolved frame buffers still need valid
  views/descriptors and retirement coverage when used by bindless compute/draw
  shaders. Reuse RHI descriptor facilities and graph resource lifetimes; ensure
  a descriptor cannot outlive or prematurely release its underlying allocation.
- **Bounded GPU output:** counter overflow, zero work, bin capacity and argument
  ranges need fail-closed/observable behavior in Shipping, not only assertions.
- **Ordering-sensitive phases:** RED priority/keep-order modes are not equivalent
  to unordered opaque bins. Do not silently route translucency through them.

## 7. Scope and validation honesty

First scene acceptance: multiple static meshes/submeshes, shared geometry with
different material values/textures, one nontrivial generated graph, camera
movement, mirrored/two-sided coverage, masked-depth agreement, a simple light,
spawn/remove/replacement and actual viewport presentation without an editor.
Pixel readback is a test oracle only. Include Debug and Shipping builds/tests and
native validation; add a second backend draw proof only where that runtime is
available. Offline DXIL/SPIR-V compilation is not a Vulkan runtime draw proof.

Deferred, not hidden completion gates: skinning, particles, decals as draw
features, full transparency sorting, shadows/GI/reflections/postprocessing,
occlusion/HZB sophistication, advanced streaming feedback policy, editor,
additional graph operations and the deferred authoring decoder. Resident-anchor
LOD drawing is sufficient for first pixels; demand-driven fine-LOD streaming
must not be claimed complete without its own feedback/eviction proof.

This study and its implementation checkpoints have only static source validation.
Existing test claims belong to their original checkpoints. The present shared
worktree is dirty and contains ongoing work from other threads.

## 8. World/Scene to RenderScene: RED alignment and ownership

### Keep the current world-to-renderer split

Vanguard already has the structural counterpart of RED's world rendering runtime:

```text
GameWorld / streamed world content                 renderer-global services
  ComponentRuntime + TransformRuntime               mesh/material residency
    StaticMeshComponent (thin adapter)                 shared exact generations
      VisualComponent / IPlacedComponent                    |
      per-world RenderingRuntime ---- strong mesh binding --+
        RenderScene mesh proxy (transform, bounds, visibility, binding)
          GPU Scene instance -> immutable renderable/material topology
            current complete LOD/geometry/phase placements
              spatial candidate production -> GPU-driven frame
```

One world runtime creates or borrows one RenderScene; it does not own another
renderer. Components author intent, proxies expose renderer state, and global
residency shares assets across proxies/worlds. Do not resurrect an ECS scanning
bridge or duplicate the world spatial index to build this connection.

| RED evidence under `dev/src/common` | Vanguard counterpart / action |
| --- | --- |
| `worldEntities/src/meshComponent.cpp:107`: request mesh, wait by job dependency, request appearance material setup; `:334`: pass render mesh + material setup to proxy | `ComponentInitializeContext` already offers resource access and a jobs continuation. Request the existing mesh-residency owner after load; it already resolves default VMATs. Inject renderer access through existing runtime setup, not a new per-world mesh/material service. |
| Same file, `OnAttach:214`, `OnDetach:227`, `CreateRenderingProxy:402` | Use the existing visual-component admission/cancellation and retirement callbacks. Concrete StaticMeshComponent is the missing adapter, not the base lifecycle. |
| `world/src/runtimeSystemRendering.cpp:327`, `:399`, `:435`: budget proxy attachment, create/register through the renderer, transfer proxy ownership on removal | `RenderingRuntime::OnSetup`, `OnBeginFrame`, `QueueProxyAdmission`, `RetireProxy` already provide these roles. Keep attachment outside shared bookkeeping critical sections and retain the owner-thread discipline. |
| `worldEntities/src/placedComponent.cpp:505`: transform update produces world bounds | `VisualComponent::OnTransformUpdated:212` computes bounds and calls `ScheduleRelink`. Preserve direct component-to-proxy updates; do not add a second transform queue/bridge. |
| `renderer/src/renderProxyMesh.cpp:404`, `:421`, `MaterialLodGroups::Apply:2987`: render mesh/material setup resolves per-LOD chunk material information | `MeshResidencyManager` already builds shared immutable LOD/primitive/default-material/phase topology. Complete its ready placement ownership; do not reconstruct the same material/chunk table in every proxy each frame. |

RED's lifecycle ownership is the reference, not a requirement to copy its exact
resource types, fixed texture binding or CPU LOD/visible-chunk selection. Vanguard's
prepared topology remains shared; per-view draw LOD selection remains on the GPU.

### What each owner must keep alive

- Component: authored mesh reference, transform/render properties and its own
  demand. It can detach while resources or admission are still pending.
- Mesh residency: shared exact mesh generation, default material demands,
  uploaded geometry and prepared primitive/phase closure. One component leaving
  must not release another component's shared mesh/material residency.
- RenderScene binding: a strong reference to the accepted drawable generation,
  not only the indices in `RenderSceneGpuMeshBinding`. The retained binding uses
  the existing mesh owner; there is no competing residency registry.
- Retained frame and GPU retirement: keep every referenced placement, arena,
  material descriptor and PSO generation valid past submitted work. Releasing a
  CPU candidate seal is not permission to recycle GPU instance/placement indices.

There are three distinct readiness events: decoded resource; proxy admitted;
complete drawable binding accepted. `MeshResidencyState::RenderableTopologySubmitted`
is only topology. Proxy admission success must not make an unresolved instance
eligible for GPU candidates. The current writer deliberately fails on a selected
mesh proxy without a stable GPU identity. Install the binding before enabling
candidate eligibility, or keep the pending proxy out of candidates; do not change
that failure into silently lost geometry. This belongs in 9C/9D integration.

The 9D source pass closes these two lifecycle debts; executable proof remains:

- `BindMesh` / `ClearMeshBinding` now return the publisher's existing revision
  receipt pattern. RenderScene retains accepted/candidate drawable ownership and
  commits only the exact accepted revision. `UpdateMeshProxy` stages binding or
  clear before committing logical resource fields; stale completion fails closed.
  Bounds continue through the existing complete transform/relink transaction.
- `RenderingRuntime::RetireProxy` hides the proxy and uses a configurable frame
  delay. That is a CPU proxy-lifecycle delay, not a GPU fence. Keep GPU binding
  ownership separately until actual accepted removal and terminal fence coverage;
  9D keeps GPU binding ownership separately until proxy removal; last drawable
  release enters existing withdrawal and terminal fence coverage. The frame delay
  is therefore not used as a GPU-completion substitute.

### Materials are part of mesh preparation, not a parallel component path

The current production default mapping is:

```text
VMESH LOD -> submesh ordinal -> material slot -> exact VMAT
  -> resident GpuMaterial + technique for registered RenderPhaseKey
  -> matching shader/pipeline and phase placement
```

`mesh_residency.cpp:785-932` already resolves slot materials and publishes that
topology. RED's material setup/LOD grouping is the analogous responsibility.
`MaterialSceneBindingBridge` explicitly serves decals; do not use it to resolve
mesh defaults a second time. The component need not know whether a VMAT originated
from a master-only input or a graph with arithmetic/sampling.

The current `MeshProxyDesc.material/materialHandle` singular fields are not a
complete submesh-material model. Reconcile their call sites during mesh-proxy
integration instead of maintaining a competing "one material per mesh" path.
For the first component path use VMESH defaults, with no material-set override.
Future overrides must resolve both material data AND compatible phase/program
placement. Changing only a `GpuMaterialSet` index cannot safely switch to unrelated
graph code while keeping the original PSO. Different-value grouping can be
proved using prepared immutable test renderables sharing geometry; it does not
require implementing dynamic component material overrides in this milestone.

### LOD and geometry: retain definitions, publish only complete placement

Keep world streaming/cell attachment, geometry LOD selection and GPU residency as
different decisions. A CPU spatial query proposes instances, not the mesh LOD to
draw. An instance has one renderable identity; two cameras may choose different
LODs without changing that identity or creating different proxies.

Today all authored LOD/submesh topology is known, but only the coarsest anchor
LOD geometry is uploaded. Example: LODs 0/1/2 exist and only bit 2 is resident.
A nearby camera desires 0 and draws 2; a distant camera desiring 2 also draws 2.
Once 0 or 1 is fully installed, the same GPU selection algorithm can use it.
Do not claim fine-LOD streaming merely because its metadata is present.

Use existing screen-coverage thresholds, view bias and the 64-bit resident mask:
desired LOD, then the first available coarser authored LOD. Admission guarantees
the anchor remains complete. Never read placements for unset bits. Installation
publishes all geometry/material-phase placements before setting a bit; eviction
clears eligibility before retirement, and in-flight readers retain old storage.
Demand feedback for finer LODs is later policy, not needed to rebuild the first
anchor-only renderer. Fixtures must still test multiple resident LOD choices and
both halves of the mask.

Geometry bytes already live in vertex/index arenas. `GpuGeometryRange` contains
arena generations, `firstIndex`, `indexCount`, `baseVertex` and decode metadata;
the CPU binds the arena state and the GPU emits the corresponding indirect
arguments. Do not copy vertices per instance, double-apply offsets, or add a new
vertex-pulling architecture. `sourceSubmesh` remains the VMESH table ordinal.
Bounds must conservatively enclose every eligible resident LOD under component
scale/rotation, so broad-phase culling cannot discard a valid selected LOD.
