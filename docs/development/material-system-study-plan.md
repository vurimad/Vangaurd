# Material System Study Plan

## Objective

Design Vanguard's material path from a future shader/material editor to a
bindless, GPU-resident runtime material without replacing foundations that
already work.

The target flow is:

```text
authored material graph / template / instance
    -> validated material IR and dependencies
    -> cooked vshader + vpipeline + vmat
    -> ResourcePipeline material object
    -> retained texture and other resource demands
    -> parameter storage + GpuMaterialResource range
    -> stable GpuMaterialHandle
```

This study does not implement rendering, visibility, pipeline sorting, indirect
commands, or the editor UI. It defines contracts those systems can consume
later.

## Constraints Already Locked In Vanguard

- The primary renderer path is bindless.
- `vmat` is a small immutable cooked resource.
- Material byte layout comes from shader reflection, not a built-in PBR struct.
- A material can select multiple named `vpipeline` techniques.
- Cooked materials store logical resource references, not descriptor indices.
- Source graphs, functions, templates, and inheritance are authoring data and
  are flattened before runtime.
- `GpuTextureResidencyHandle` is the stable material-visible texture identity.
- `GpuSceneDefinitions` owns content-addressed GPU Scene material compounds and
  stages new material publication through `GpuSceneRuntime`.
- The generic asset build graph, DDC, loose artifact materializer, VPAK, and
  `ResourcePipeline` remain shared infrastructure. Materials must not create
  substitutes for them.

## Phase 0 — Broad Architecture Scan

Status: complete.

### Vanguard findings

The following foundations already exist and should be retained:

- `source/materials/include/vanguard/materials/materials.hpp`
- `source/materials/src/materials.cpp`
- `source/materials/README.md`
- `docs/formats/vmat-format.md`
- `source/shaders/include/vanguard/shaders/shaders.hpp`
- `source/pipelines/include/vanguard/pipelines/pipelines.hpp`
- `source/rendering/include/vanguard/rendering/gpu_scene_types.hpp`
- `source/rendering/include/vanguard/rendering/gpu_scene_definitions.hpp`
- `source/rendering/include/vanguard/rendering/texture_residency_runtime.hpp`
- `source/rendering/docs/shader-pipeline-runtime.md`
- `docs/architecture/asset-build-graph.md`

`VMAT` already supplies a strong renderer-agnostic runtime contract:

- exact shader-reflected constant-buffer layouts;
- named parameters with reflected offsets and types;
- logical texture, buffer, sampler, and acceleration-structure parameters;
- named compatible pipeline techniques;
- deterministic dependencies and content identity;
- no descriptor slots, heap indices, or hardcoded surface model.

The GPU endpoint also exists:

```text
GpuMaterial
    parameterByteOffset / parameterByteSize
    firstResource / resourceCount
    materialInterface

GpuMaterialResource
    stable texture-residency index or another resolved bindless resource
```

At the Phase 0 scan, the production middle was missing. Phase 2 subsequently
completed the offline compiler adapter; the remaining items are runtime work:

- no `MaterialResourceObject` or `MaterialResourceLoader`;
- no renderer-owned persistent parameter-byte storage;
- no resolver from `VMAT` logical resources to retained texture demands;
- no material residency owner that creates and retires `GpuMaterialHandle`;
- no live compact registry backing `GpuMaterial::materialLayout`;
- no material instance/override contract feeding `GpuMaterialSet`.

### Unreal findings

The broad Unreal scan covered:

- `Engine/Source/Runtime/Engine/Public/Materials/Material.h`
- `Engine/Source/Runtime/Engine/Public/Materials/MaterialInterface.h`
- `Engine/Source/Runtime/Engine/Public/Materials/MaterialInstance.h`
- `Engine/Source/Runtime/Engine/Public/Materials/MaterialExpression.h`
- `Engine/Source/Runtime/Engine/Public/MaterialCompiler.h`
- `Engine/Source/Runtime/Engine/Public/MaterialShared.h`
- `Engine/Source/Runtime/Engine/Public/MaterialCachedData.h`
- `Engine/Source/Runtime/Engine/Public/Materials/MaterialIRModule.h`
- `Engine/Source/Runtime/Engine/Public/Materials/MaterialIRModuleBuilder.h`
- `Engine/Source/Runtime/Engine/Private/Materials/MaterialShared.cpp`
- `Engine/Source/Runtime/Engine/Private/Materials/HLSLMaterialTranslator.*`
- `Engine/Source/Runtime/Engine/Private/Materials/MaterialIR*`
- `Engine/Source/Runtime/Engine/Private/Materials/MaterialRenderProxy.cpp`
- `Engine/Source/Runtime/Engine/Private/Materials/MaterialUniformExpressions.*`
- `Engine/Source/Editor/UnrealEd/Classes/MaterialGraph/MaterialGraph.h`
- `Engine/Source/Editor/UnrealEd/Private/MaterialGraph.cpp`
- `Engine/Source/Editor/MaterialEditor/Private/MaterialEditor.cpp`

Useful Unreal principles:

1. The editable expression model is distinct from the editor canvas and from
   render resources. `UMaterialGraph` synchronizes editor nodes with material
   expressions; neither representation is the final render object.
2. Material compilation has a typed intermediate boundary. Unreal's newer
   `FMaterialIRModule` explicitly describes itself as backend-agnostic and owns
   a value graph, stage entry points, resource metadata, statistics, and
   expression-associated errors before HLSL emission.
3. Static parameters affect shader identity and permutations. Dynamic scalar,
   vector, and texture parameters remain runtime values. This distinction must
   be deliberate because uncontrolled static switches multiply shader work.
4. Material instances form authoring inheritance chains, but cache resolved
   properties and compiled resources instead of walking the chain in the hot
   render path.
5. Shader-map identity includes the material graph, static parameters,
   platform/quality inputs, referenced functions, collections, and compiler
   environment. Dependency identity is part of caching correctness.
6. The editor uses a transient preview material and an explicit Apply boundary.
   Invalid or incomplete edits need not replace the last valid asset/runtime
   material.
7. The render proxy and uniform-expression cache separate parameter resolution
   from shader compilation. Vanguard needs the separation, but should lower it
   to persistent GPU Scene records and bindless indices rather than copying
   Unreal's per-draw proxy and uniform-buffer behavior.

Things not to copy:

- the UObject inheritance tree as an engine-wide material ABI;
- Unreal's fixed material-property and shading-model catalog as Vanguard's
  universal schema;
- its accumulated legacy translator paths and permutation breadth;
- its hybrid bindless/explicit-binding decisions;
- live parent-chain traversal or name lookup in the render hot path;
- editor widget architecture before the underlying document and compiler
  contracts are stable.

## Phase 1 — Material Interface and Typed IR

Status: complete. The accepted contracts are recorded in
`docs/development/material-system-design.md`.

Foundational implementation is also complete: shader annotations and reflection,
`VSHADER`/`VPPL`/`VMAT` compatibility metadata, contract-derived material cooking, and
the compact typed IR core are present. Source-graph lowering begins in Phase 2.

### Question

What exactly does a shader expose as a material interface, and what typed,
backend-neutral representation connects an authored graph to the existing
`vshader`/`vpipeline`/`vmat` contracts?

### Vanguard sources

- `source/materials/include/vanguard/materials/materials.hpp`
- `source/shaders/include/vanguard/shaders/shaders.hpp`
- `source/pipelines/include/vanguard/pipelines/pipelines.hpp`
- `docs/formats/vmat-format.md`
- `source/rendering/shaders/gpu_scene_types.hlsli`
- the provisional material-interface section in
  `source/rendering/docs/gpu-scene-implementation.md`

### Unreal sources

- `MaterialExpression.h` and representative arithmetic, parameter, texture,
  function, and material-attributes expressions;
- `MaterialCompiler.h`;
- `MaterialIR.h`, `MaterialIRModule.h`, `MaterialIRModuleBuilder.*`;
- `MaterialExpressionsToMIR*.cpp`;
- `MaterialIRToHLSLTranslator.cpp`;
- `MaterialAttributeDefinitionMap.*`.

### Deliverables

- authored node/value type system and graph validation rules;
- graph outputs expressed as extensible shader/interface entry points rather
  than one permanent PBR root schema;
- exact definition of a shader-declared material interface;
- dynamic parameters versus compile-time/static specialization values;
- functions/subgraphs and cycle/dependency rules;
- canonical IR identity and diagnostics tied back to authored nodes;
- decision for `GpuMaterial::materialInterface` and generated shader accessors;
- mapping from IR output to existing `BuildDescription` inputs without changing
  `VMAT` into an editor document.

This phase must prove that surface, decal, terrain, hair, UI, compute-driven,
and project-defined material families can coexist without changing the base
runtime material record.

## Phase 2 — Compilation, Cooking, Caching, and Preview

Status: complete. Phase 2B, Phase 2C, Phase 2D.1 through Phase 2D.4, and the
final 2D.4.1 resource-value ABI correction are implemented. The frozen
frontend/reflected shader identity, offline target
capabilities, concrete resource-type compatibility, Required/Optional/Soft
dependency semantics, indexed VPAK closure, declared surface, transactional
recook matrix, robustness/preview lifecycle, and cross-target artifact path are
sealed. The final boundary is recorded in
`docs/development/material-system-design.md`.

### Question

How does one edited material become deterministic shader, pipeline, and material
artifacts without redundant compilation or invalid preview state replacing a
working material?

### Vanguard sources

- `source/assets` build graph/compiler contracts;
- `docs/architecture/asset-build-graph.md`;
- `source/shaders`, `source/pipelines`, and `source/materials` writers/tests;
- DDC, dependency-index, incremental-recooking, loose materialization, and VPAK
  contracts already used by mesh and texture tools.

### Unreal sources

- `HLSLMaterialTranslator.*` and the new MIR-to-HLSL path;
- `MaterialShared.*`, especially `FMaterialShaderMapId` and shader caching;
- `MaterialCachedData.*`;
- `Material.cpp` and `MaterialInstance.cpp` cooked resource handling;
- `MaterialEditor.cpp` preview, error reporting, statistics, and Apply flow;
- `MaterialStats*` and `MaterialEditorValidation.*`.

### Deliverables

- material source document and compiler input boundary;
- deterministic source/IR/compiler/platform/dependency cache key;
- precise ownership of shader generation versus `VPPL` pipeline construction
  versus `VMAT` value cooking;
- static-permutation limits and rejection/diagnostic policy;
- asynchronous compile result and node-correlated error model;
- preview isolation, cancellation, last-valid-result, and Apply semantics;
- production material compiler adapter using the generic build graph and DDC;
- dependency output sufficient for texture/shader/pipeline recooking and package
  assembly.

No editor UI is implemented in this phase. The compile service must be usable
headlessly and later by the editor.

### Study result

Phase 2 uses three independently cached generated operations:

```text
canonical program input                    -> VSHADER
canonical pipeline input + VSHADER         -> VPPL
canonical value input + VSHADER + VPPLs    -> VMAT
```

This keeps dynamic values and texture assignments out of shader identity and
keeps pipeline-state-only edits out of VSHADER identity. The generic build graph
and DDC remain authoritative.

The audit first required two general seams: bounded, read-only views of completed
generated dependency artifacts, and structured compiler reports that survive
asynchronous failure. Shader work also required a truthful resource estimate and
a reflection-only layout probe before final material accessor generation. Those
shared build/shader capabilities are now implemented; they remain general
infrastructure rather than material-specific substitutes.

The implementation is deliberately collapsed into four slices:

1. 2A — build dependency views, diagnostics, shader estimates, and reflection
   probing;
2. 2B — the canonical material frontend, registries, functions, IR completion,
   and source mapping;
3. 2C — VSHADER, VPPL, and VMAT compiler adapters and artifact-graph proof;
4. 2D — isolated headless preview, cancellation, last-valid/Apply behavior, and
   deterministic loose/VPAK tests.

The ordered closure subphases and their current status are:

1. **2D.1 — full declared-surface vertical proof and correction closure — complete;**
2. **2D.2 — material-specific incremental recooking and reproducibility — complete;**
3. **2D.3 — adversarial robustness and preview lifecycle — complete;**
4. **2D.4 — cross-target proof and final freeze — complete;**
5. **2D.4.1 — portable resource-value ABI correction — complete.** Resource
   values and domain outputs carry descriptor-index `uint`s; annotated roles
   retain their exact logical resource types; consuming operations materialize
   opaque resources locally; and the full declared-surface graph passes DXIL
   and SPIR-V. No parallel "data-only material" execution path exists.

This is an early-development clean cut. All cooked outputs, DDC entries,
dependency indexes, and intermediate compiler inputs are disposable. Contract
changes support only the new current format: reject stale data, clear caches,
and rebuild. Do not add migration, dual-version decoding, compatibility
adapters, or fallback paths for historical cooked data.

## Phase 3 — Runtime Loading and Bindless GPU Residency

Status: **Phase 3 is complete and frozen.** 3A, 3B, and all four 3C slices are
implemented and proven. Phase 4 remains deliberately deferred.

### Question

How does a validated `VMAT` become one stable, ready-to-reference
`GpuMaterialHandle` with bounded memory and correct lifetime behavior?

### General Vanguard scan

The offline/runtime handoff is complete, but the production runtime middle is
not. The scan found the following reusable foundations:

- `ResourcePipeline` already provides coalesced asynchronous dependency loading,
  cancellation, priority promotion, failure traces, and generation-safe handles;
- `RenderShader`, `RequestRenderPipeline`, and `PipelineCache` already own native
  shader and pipeline materialization without inventing a material binding model;
- `TextureResidencyRuntime` already turns a loaded texture generation into a
  retained stable `GpuTextureResidencyHandle`, contributes installations to the
  shared GPU Scene transaction, and retires through renderer fences;
- `GpuSceneDefinitions` owns content-addressed `GpuMaterial` compounds,
  reference counting, rollback, and deferred index reuse; 3B.1 removed its
  duplicate privately submitting material path and added shared contribution
  staging for material/resource/parameter ranges;
- `RenderingService` owns the global resource and sampler descriptor domains,
  the material program-layout registry, and the
  frame points at which residency progresses, GPU Scene contributions are
  staged, publication failures are consumed, and retirements are collected.

The scan also found concrete gaps which Phase 3 must not hide. Phase 3A has now
closed the production artifact-decoder and compact full-fingerprint layout-
registry gaps. The remaining 3B/3C gaps are:

- texture residency exists, but material buffer, sampler, and acceleration-
  structure resolution/ownership do not;
- `ResourcePipeline` has Required and Optional execution edges while cooked
  material metadata also has Soft references, so the runtime meaning of all
  three kinds must be made explicit rather than collapsed accidentally;
- no owner currently joins loaded material generations, retained dependencies,
  parameter/resource allocations, `GpuMaterialHandle`, rollback, and shutdown.

### Unreal runtime audit

Unreal is useful here as a separation and lifecycle audit, not as a class model
to copy. Its runtime distinguishes the game-thread `UMaterialInterface`, the
compiled/renderable `FMaterial`/`FMaterialResource`, and the render-thread-facing
`FMaterialRenderProxy`. The proxy provides current parameter values and owns a
cached uniform-expression result; cache invalidation and an explicit serial make
replacement observable. Incomplete shader maps can resolve through a default
material fallback. Material instances may form authoring/runtime parent chains,
but resolved parameters and static-permutation resources are cached rather than
rediscovered for every draw. Render-resource initialization is explicitly
marshalled to the rendering thread.

Vanguard should adopt the separation of loaded asset, compiled program state,
render-ready values, fallback/readiness, cache invalidation, and deferred GPU
lifetime. It should not copy UObject ownership, parent-chain lookup, one uniform
buffer per material, legacy binding paths, or Unreal's broad render-proxy class
hierarchy. `VMAT` is already flat, and Vanguard's endpoint is one compact
renderer-owned record in global bindless/GPU Scene storage.

### Vanguard sources

- `source/resources/include/vanguard/resources/resource_pipeline.hpp`;
- mesh and texture resource loaders as patterns, not code to duplicate;
- `texture_residency_runtime.*`;
- `gpu_scene_definitions.*`, `gpu_scene_runtime.*`, and GPU Scene tables;
- rendering-service ownership and shutdown ordering;
- existing mesh material-slot dependency and GPU binding seams.

### Unreal sources

- `MaterialInterface.*` and `MaterialInstance.*`;
- `MaterialRenderProxy.cpp`;
- `MaterialUniformExpressions.*`;
- cooked/inline material resource loading in `Material.cpp`,
  `MaterialInstance.cpp`, and `MaterialShared.cpp`.

### Phase 3 implementation deliverables

- production `VSHADER`, `VPPL`, and `VMAT` resource objects/loaders through the
  normal loose/VPAK `ResourcePipeline` path;
- required shader/pipeline/resource dependency readiness rules;
- collision-safe compact material-layout registration;
- renderer-owned bounded parameter-byte arena and batched uploader;
- global resource and sampler descriptor domains;
- logical texture, buffer, sampler, and acceleration-structure roles resolved to
  retained runtime identities or descriptor references;
- defaults for optional, missing, loading, and failed resources;
- canonical `GpuMaterialDefinition` key and resource range construction;
- one renderer-global material residency owner with coalescing, readiness,
  cancellation, hot-generation replacement, and fence-safe retirement;
- shared GPU Scene update participation with no per-material command list or
  fence;
- exact shutdown and failure rollback behavior.

This phase ends when a material reaches the GPU and can be referenced by future
renderable/indirect work. It does not perform a draw.

### Ordered Phase 3 studies

Phase 3 is divided into exactly three studies. Each study ends in locked
contracts and proof requirements before its implementation slice begins.

#### 3A — loaded resource closure and runtime identity

Answer how loose/VPAK `VMAT`, `VSHADER`, and `VPPL` resources become one
validated immutable CPU closure without touching descriptor heaps yet.

Status: **study and implementation complete**. The accepted contract is recorded in
`material-system-design.md` under "Phase 3A: Loaded Resource Closure and Runtime
Identity."

This study must lock:

- production `ResourcePipeline` object/loader ownership for all three artifact
  types, with native `RenderShader` and render-pipeline materialization kept out
  of the CPU load boundary;
- Required, Optional, and Soft runtime edge semantics, including absence,
  failure propagation, cancellation, and priority;
- target, domain, layout, accessor-ABI, shader, and technique compatibility
  checks at the load boundary;
- a collision-safe full-fingerprint `MaterialProgramLayout` registry and the
  lifetime of its compact `materialLayout` id;
- stable CPU material request/handle states, coalescing key, diagnostics,
  generation replacement, and last-valid/fallback policy;
- service ownership and thread rules, with no compiler or editor dependency in
  the shipping runtime.

The proof is byte-equivalent loose/VPAK loading, exact dependency closure,
coalesced requests, cancellation, corrupt/mismatched artifact rejection, and
clean loader shutdown. No RHI material allocation is part of 3A.

The study resolves 3A into three implementation slices:

1. **3A.1 — artifact resource objects and decoders (complete):** immutable typed
   `VSHADER`, `VPPL`, and `VMAT` resource objects now use bounded full-file
   decoders, validate metadata against parsed non-Soft dependencies exactly, and
   retain the strong dependency generations and Optional failures needed by the
   published object. Focused loose and indexed-VPAK proofs cover byte-equivalent
   VMAT closure, Required/Optional/Soft behavior, retained transitive ownership,
   and rejection of incomplete dependency metadata;
2. **3A.2 — closure validation and layout identity (complete):** VPPL and VMAT
   decoders now repeat stable cross-artifact compatibility checks before
   publication, including exact retained shader generations, permutation and
   interface fingerprints, material domain/layout identity, reflected numeric
   and resource layout, and technique pipelines. The bounded renderer-owned
   registry admits backend-compatible shader artifacts, interns the complete
   canonical reflected payload behind a monotonic compact id, and rejects full-
   fingerprint collisions without native RHI allocation;
3. **3A.3 — service and adversarial closure proof (complete):** the process-wide
   resource-streaming service registers VSHADER, VPPL, and VMAT in dependency
   order and removes them in reverse order. Focused proofs cover normal service
   ownership, partial-init rollback, loose/VPAK closure parity, coalescing,
   priority promotion, caller cancellation, Required/Optional/Soft behavior,
   generation replacement, dependency-metadata and payload corruption, and
   clean shutdown. Generic last-interest cancellation and stored-segment
   corruption remain proven in the lower resource/streaming suites.

#### 3B — bindless resolution and atomic GPU materialization

Answer how one valid CPU closure becomes parameter bytes, resolved resource
roles, and one published `GpuMaterialHandle` without exposing partial state.

Status: **complete and sealed**. The accepted contract and 3B.4 closure evidence
are recorded in `material-system-design.md` under "Phase 3B: Bindless Resolution
and Atomic GPU Materialization."

This study must lock:

- the bounded renderer-owned parameter-byte allocator, shader-visible storage,
  upload batching, alignment, compaction policy, and fence-safe range reuse;
- global resource and sampler descriptor-domain ownership;
- role resolution and retained references for every declared family: texture,
  buffer, sampler, and acceleration structure;
- Required failure versus Optional/Soft fallback values, using retained,
  shape-correct resources that are legal on the active backend;
- readiness rules for dependencies whose stable identity exists before their
  physical bindless installation is visible;
- the canonical resolved-material key and the all-or-nothing transaction joining
  dependency retention, parameter allocation, resource-range construction, and
  deferred `GpuSceneDefinitions` publication;
- bounded per-frame progress through the shared GPU Scene publication path, with
  no per-material command list, submission, wait, descriptor set, or fence.

The proof covers every declared resource family, repeated equal materials,
distinct dependency generations, capacity/backpressure, injected allocation and
upload failures, rollback with no leaked demand/descriptor/range, and successful
shader-readable publication.

The study found one required clean-cut correction and resolved 3B into four
implementation slices:

1. **3B.1 — runtime role shape and GPU storage foundations (complete):** preserve
   reconstructable reflected resource shape beside the full type fingerprint,
   add the renderer-global Samplers domain and layout-registry ownership, append
   a paged `GpuMaterialParameterWord` table, and make material definitions a
   deferred material/resource/parameter compound for shared publication;
2. **3B.2 — typed role resolution and retained references (complete):** the bounded
   provider registry, production VTEX adapter, centralized immutable resource/
   sampler descriptor caches, exact typed fallbacks, readiness polling, and
   move-only retirement-safe references now cover texture, typed/structured/raw
   buffer, filtering/comparison sampler, and acceleration-structure roles. The
   3B.2.1 correction removed mutable provider unregistration and unused GPU
   fallback/writable flags, indexed layout and descriptor lookup by hashed full
   identity with collision verification, and rejects non-portable Texture3D
   arrays plus non-Texture2D multisampling at both offline and runtime contract
   boundaries;
3. **3B.3 — atomic materialization transaction (complete):** the bounded
   `MaterialMaterializer` retains the validated VMAT closure, resolves its dense
   reflected role set, hashes the complete resolved GPU image, and feeds 3B.1's
   single deferred GPU Scene contribution. Equal images share the existing
   content-addressed definition owner; caller-visible handles appear only after
   acceptance. Prepared cancellation rolls back the compound allocation, staged
   cancellation accepts then retires without exposure, and successful references
   retire their definition and role references through complete queue cutover fences;
4. **3B.4 — adversarial and cross-backend closure proof (complete):** the D3D12
   vertical proof joins six material requests, texture installation, and scene
   mutation in one shared publication; reads back exact material/resource rows
   and a seven-byte parameter image; exhausts and recovers the materializer
   operation bound; and retires through real three-queue fences to zero state.
   The actual paged GPU Scene material/resource/parameter accessor include
   compiles to DXIL and SPIR-V. Live Vulkan descriptor execution remains
   unavailable because the repository has no Vulkan RHI backend; existing lower
   owner tests remain authoritative for their bounded fault matrices.

The type-shape correction does not add legacy format support. Current cooked
artifacts and caches are rebuilt, and stale data is rejected.

#### 3C — lifecycle integration and final runtime proof

Answer when a material may become visible to Render Scene consumers and how its
old generation disappears safely.

Status: **study sealed; 3C.1 through 3C.4 implementation complete**. The
accepted ownership, state, replacement, technique, fence, device-loss, and
shutdown contracts are recorded in `material-system-design.md` under "Phase 3C:
Lifecycle Integration and Final Runtime Proof." Phase 3 is frozen at the
headless, no-draw runtime boundary.

This study must lock:

- the state machine from requested through CPU-ready, dependency-ready,
  publication-pending, resident, replacing, failed, cancelling, and retiring;
- atomic replacement which keeps the last valid resident generation until the
  new generation is completely publishable;
- references from mesh material slots, decals, and future `GpuMaterialSet`
  owners without implementing instance overrides yet;
- shader/pipeline technique readiness and fallback behavior without performing
  draw submission;
- service tick order, asynchronous failure reporting, device-loss policy,
  shutdown order, and renderer-fence cutover/collection;
- budgets, statistics, diagnostics, and an explicit guarantee that no authored
  graph, compiler, or editor object reaches the runtime.

The final proof starts from production loose and indexed VPAK resources, reaches
a readable `GpuMaterial`, parameter-byte range, and resolved resource range,
survives cancellation/distinct-root replacement/failure/retirement adversaries,
and shuts down with zero live material, dependency, descriptor, parameter, or
GPU Scene allocations. Same-path overlapping hot reload remains Phase 4 because
the current resource registry does not publish a replacement generation while
the resident generation is strongly retained. The proof is headless and stops
before Render Graph draw execution, editor integration, material-instance
inheritance, or a broader graph opcode library.

The study resolved 3C into four implementation slices:

1. **3C.1 -- renderer-owned material residency and technique admission:**
   bounded generational material demands over the existing 3B materializer,
   exact-root coalescing, shared native shader generations, and lazy concrete
   technique requests through the existing pipeline factory/cache. **3C.1.1**
   removed high-water polling and linear cache lookup: indexed exact-identity
   chains, dense active-work sets, explicit progress/retirement budgets, direct
   dense-role admission, binary dependency lookup, and deterministic complexity
   counters are now proven;
2. **3C.2 -- scene binding and atomic replacement:** a narrow scene-publication
   binding receipt, removal of eager clear-on-replacement behavior, and a bridge
   retaining active/candidate demands for decals. Mesh-default demand remains with
   the future renderable/material-set assembly owner instead of creating a partial
   second path. **Complete:** exact resource-object identity and revision polling,
   logical-change preservation of the accepted GPU binding, last-valid
   replacement, retryable canceled publication, failure/cancellation preservation,
   superseded-receipt restoration, non-starving record failure, explicit clear,
   and proxy-destruction cleanup are proven;
3. **3C.3 -- service, cutover, and terminal lifecycle integration:** renderer
    ownership/tick order, asynchronous diagnostics, common real-fence retirement
    fan-out, quiesce/rollback/shutdown, and terminal device abandonment.
    **Complete:** 3C.3.1 proves normal service lifecycle and 3C.3.2 proves
    fence-free terminal invalidation after device loss;
4. **3C.4 -- production vertical and adversarial Phase 3 freeze:** production
   loose/VPAK loading through service-visible binding and concrete technique
   readiness, exact readback, replacement/failure/cancellation/backpressure,
   real three-queue retirement, and zero-state shutdown. **Complete.**

The 3C.4 service proof starts with current-format loose and indexed-VPAK VMAT
closures, verifies exact CPU closure and GPU material/resource/parameter/layout
identity, and observes concrete pipeline pending, ready, and fail-closed states.
It covers coalesced bounded demand, Required dependency failure, exact Optional
fallback, last-valid cancellation, canceled-publication retry, distinct-root
replacement, real graphics/compute/copy retirement fences, terminal cache
release, and clean zero-state shutdown. The pass corrected two concrete lifecycle
defects: replacing a pending clear now restores the active binding before staging
the new request, and renderer-owned native-program/fallback caches are released
during resource-dependent service quiesce.

A final Debug/Shipping recheck also corrected the test's direct-publication
ordering. After a service frame, the proof now explicitly joins the renderer CPU
chain and resolves its contribution outcome before manually preparing and
canceling a Render Scene GPU publication. Shipping had correctly reported
`Busy`; no production API or lifecycle behavior changed. The post-correction
matrix passes 27/27 Debug tests and 8/8 focused Shipping builds and tests; the
corrected Shipping production vertical also passes ten consecutive reruns.

No 3C slice may add a second resource loader, descriptor cache, material
allocator, scene database, PSO cache, private submission path, or implicit
layout-incompatible default material.

## Phase 4 — Instances, Overrides, Hot Reload, and End-to-End Proof

### Question

How can the future editor expose reusable parent materials and lightweight
instances while runtime records remain flat, bounded, and fast?

### Vanguard sources

- `GpuMaterialSet`, mesh material slots, render-scene material handles, and
  existing resource generation/hot-reload contracts;
- project asset identity, `.vmeta`, DDC overlay, and incremental recooking docs;
- completed Phase 1–3 material contracts.

### Unreal sources

- `MaterialInstance.h/.cpp` and `MaterialInstanceConstant.cpp`;
- material functions, parameter metadata/groups, and cached inheritance data;
- material editor preview/apply and child-instance invalidation paths;
- material editor tests and headless editing utilities.

### Deliverables

- base material, authoring instance, runtime instance, and per-renderable
  override distinctions;
- flattening rules and cycle-safe inheritance;
- static override recook versus dynamic override GPU-data update;
- deduplicated immutable base materials and compact override storage;
- `GpuMaterialSet` ownership for primitive-local overrides;
- hot reload that keeps the last valid resident material until its replacement
  is complete;
- a headless proof from authored test graph through DDC/loose/VPAK loading to a
  readable GPU material row and parameter/resource data;
- editor-facing service APIs and diagnostics, without implementing the visual
  editor itself.

## Study Order and Stop Conditions

Study and design one phase at a time. Do not begin implementation until that
phase answers its named question and records its locked contracts.

Phase 2B is complete: safe optimization/poison behavior, the generalized typed
IR-to-Slang bridge, reflection-derived accessors, authored diagnostics, and
canonical MPGI/MPLI/MVLI partitioning are implemented. Phase 2C.1 is complete:
independently cached VSHADER, VPPL, and VMAT outputs reconstruct as byte-identical
loose resources, reopen through production readers, mutually validate, and
cannot be replaced by corrupt indexed metadata. Phase 2C.2 is complete: the
frontend and reflected shader share one checked domain contract, target features
are an offline cook policy, resource assignments have concrete expected asset
types, and Required/Optional/Soft survive the generic build/index/package seams
without collapse. Phase 2C.3 is complete: real compiler-produced records persist
through the production dependency index, a VMAT root closes over its exact
required VSHADER/VPPL set, the generic assembler publishes an atomic LZ4 VPAK
from persistent DDC, decoded logical bytes match standalone artifacts, production
readers reopen the package resources, and corrupt indexed data cannot replace
the last validated package. Phase 2D.1 is complete: the production canonical
path now covers two frozen material domains, two techniques in one material,
all declared scalar families, vectors, fixed arrays, row/column-major matrices,
arrays of nested aggregates with matrix leaves, texture arrays,
sampler/buffer/acceleration roles, authored and
domain defaults, and positive/negative resource assignments. Typed logical
MVLI values are packed only after VSHADER reflection, so bool width and
array/matrix/aggregate layout are proven rather than guessed. Aggregate-major
authored bytes are transposed into reflection-derived strided leaves, and
interleaved leaf spans are copied without overwriting neighboring fields. The
manual buffer/resource selector is removed; MVLI v3 is the only accepted
intermediate value format and stale formats are rejected. The same complete
artifact set is rebuilt from persistent DDC as loose files and as an exact
four-resource indexed VPAK closure; absent Optional/Soft textures are not pulled
into the package.

The 2D.1 correction is closed as three ordered parts: **2D.1.1** declared-surface
aggregate-array correction, **2D.1.2** canonical-only contract de-bloating, and
**2D.1.3** named proof-suite/document hygiene. No compatibility decoder or
manual fallback remains.

Phase 2D.2 is complete. The canonical partition is proven through the real
persistent `DependencyIndex` and transactional `IncrementalRecooker`: MVLI,
MPLI, and MPGI edits invalidate exact 1/2/3-output closures; build settings and
the real material-tool fingerprint invalidate all three outputs; VMAT target
identity and generated VTEX-to-VMAT invalidation are explicit; Optional VTEX
absence remains legal; coalescing, cancellation, failed-build rollback, restart,
DDC reuse, and deterministic reversion preserve the last committed records.

Phase 2D.3 is complete. MPGI, MPLI, and MVLI malformed-input matrices reject bad
magic, truncation, and trailing bytes; exact entry-point, vertex-stream, and
technique caps are accepted while their first excessive counts are rejected
before allocation. Preview acceptance now releases transient graph retention
and copy-then-commits one last-valid VMAT. Pipeline and byte budgets, failed and
owner-rejected Apply paths, active replacement, stale-result exclusion,
explicit cancellation, last-valid preservation, and shutdown during a blocked
generated VTEX build are covered. Cancellation and shutdown join active graph
roots before graph teardown.

Phase 2D.4 and its 2D.4.1 correction are complete. The full declared-surface
material now cooks through the real VSHADER/VPPL/VMAT graph for Windows/D3D12,
Windows/Vulkan, and Linux/Vulkan.
The Vulkan targets contain SPIR-V, keep platform-distinct DDC identities, and
produce byte-identical artifacts; DXIL and SPIR-V preserve the same domain
fingerprint and accessor ABI. Generated bindless texture/sampler sampling also
compiles to SPIR-V with stable reflected roles. Resource values—including direct
outputs and arrays—cross generated value/domain-output aggregates only as
descriptor-index `uint`s; exact opaque texture, sampler, buffer and acceleration
types remain in annotated role reflection and are materialized only by consuming
operations. Reflected VMAT packing permits backend-required trailing padding but
still bounds every addressed component and stride. The affected asset, shader,
pipeline, material, and material-tool test matrix passes.

Phase 2 is closed at the offline/runtime handoff. **Phase 3 is complete and
frozen**: 3A, 3B, and 3C.1 through 3C.4 are complete. **3C.3.1** proves service ownership,
bounded progress, common cutover, normal drain, and partial-init rollback.
**3C.3.2** adds explicit fence-free terminal invalidation through GPU Scene,
mesh, texture, material-resource, materializer, residency, pipeline, and
scene-binding ownership. Live move-only handles become stale before their owners
release native state, provider abandonment is explicit, CPU work is joined, and
the mandatory RHI/backend abandonment contract never calls `WaitIdle` or invents
fences. **3C.4** closes the production loose/VPAK service vertical, adversarial
matrix, exact GPU readback, three-queue retirement, and zero-state shutdown.
The freeze does not authorize same-path live hot reload, editor widgets, draw
submission, culling, indirect command generation, legacy cooked-data
compatibility, material-instance overrides, or a broader concrete PBR
node/opcode library.
