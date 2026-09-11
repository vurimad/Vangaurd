# Material runtime time-memory checkpoint

Date: 2026-09-02

## Purpose

This is the authoritative handoff for starting a fresh implementation thread
without replaying the long material-system conversation. Read this file together
with:

- `docs/development/material-system-study-plan.md`;
- `docs/development/material-system-design.md`;
- `docs/development/resource-flow-allocator-execution-plan.md`;
- `docs/development/mesh-residency-execution-plan.md`;
- `docs/development/world-rendering-resume-checkpoint.md`.

Do not infer progress from historical headings or from old chat messages. The
current repository and the status boundaries below are authoritative.

## Immediate position

The material system has completed **Phase 3C.4 -- production vertical and
adversarial Phase 3 freeze**. Phase 3 is complete and frozen at the headless,
no-draw runtime boundary.

```text
Phase 0   broad architecture scan                              COMPLETE
Phase 1   material interface and typed IR                      COMPLETE
Phase 2   compilation, cooking, caching, and preview           COMPLETE
Phase 3A  loaded resource closure and runtime identity         COMPLETE / SEALED
Phase 3B  bindless resolution and atomic GPU materialization   COMPLETE / SEALED
Phase 3C.1 renderer-owned residency and technique admission    COMPLETE
Phase 3C.2 scene binding and atomic replacement                COMPLETE
Phase 3C.3 service, cutover, and terminal lifecycle            COMPLETE
Phase 3C.4 production vertical and adversarial freeze          COMPLETE / FROZEN
Phase 4   instances, overrides, and live hot reload            DEFERRED
```

Do not reopen 3A, 3B, or 3C.1-3C.3 unless 3C.4 exposes a concrete contract bug.
Do not split 3C.4 into another long sequence of speculative subphases. It is one
closure pass with a vertical proof, adversarial cases, cleanup, documentation,
and the final Phase 3 test matrix.

## Locked user intent and scope

The following decisions are deliberate:

1. This is early development. Cooked artifacts, DDC entries, dependency indexes,
   and intermediate compiler inputs are disposable. When a contract changes,
   support only the new current format, reject stale data, clear/rebuild caches,
   and recook. Do not add legacy-version migration, dual decoders, compatibility
   adapters, or deprecated parallel APIs.
2. Phase 3 must provide everything the later runtime consumer needs to load,
   validate, resolve, materialize, retain, bind, technique-resolve, replace, and
   retire a cooked material. It stops before any draw.
3. No visual editor, editor widgets, authoring integration, draw submission,
   visibility, culling, indirect-command generation, Render Graph execution, or
   concrete GBuffer/forward/depth rendering belongs to this phase.
4. Do not broaden the graph opcode/function library merely to close Phase 3.
   Additional authored operations are additive future compiler work, not a hole
   in the current runtime contract.
5. There is one material-evaluation model. Generated material logic is called by
   handwritten engine shaders and returns ordinary per-invocation surface data.
   A so-called "data-only material" is not a second runtime path; it is simply a
   trivial generated evaluator whose parameters/resources are still handled by
   the same machinery.
6. Opaque shader resources do not cross generated value/domain-output aggregate
   boundaries. Resource values cross those boundaries as descriptor-index
   `uint`s. A consuming generated operation materializes the typed texture,
   sampler, buffer, or acceleration structure and uses it; returned domain data
   remains ordinary scalars/vectors/matrices/aggregates. This is the portable
   DXIL/SPIR-V contract sealed by the Phase 2D.4.1 correction.
7. Preserve existing authorities. Do not add a second resource loader,
   descriptor cache/domain, GPU material allocator, scene database, pipeline/PSO
   cache, submission path, scheduler, or lifetime registry.
8. Use Premake, not CMake. Apply the repository `.clang-format` to every touched
   C/C++ file before the final build. Preserve unrelated user changes in the
   already dirty worktree.

## What Phase 2 already provides

Phase 2 is closed at the offline/runtime boundary. It provides:

- validated typed material IR, safe constant folding, poison propagation, and
  authored diagnostics;
- generated Slang and reflection-derived accessors for the declared surface;
- canonical current MPGI/MPLI/MVLI inputs, with the old manual constant-buffer
  and resource-selection path removed;
- aggregate arrays, nested aggregates, row/column-major matrices, matrix arrays,
  texture arrays, and reflected VMAT packing;
- independently cached VSHADER, VPPL, and VMAT artifacts through persistent DDC;
- byte-exact atomic loose-file reconstruction and production-reader reopening;
- indexed VPAK assembly/reopening with dependency closure and corruption
  rejection;
- Required, Optional, and Soft dependency semantics preserved through build,
  index, package, and cooked metadata boundaries;
- Windows/D3D12, Windows/Vulkan, and Linux/Vulkan cooking proof, including DXIL
  and SPIR-V domain/accessor agreement;
- preview last-valid acceptance and cleanup without making preview an editor or
  renderer.

The runtime consumes flattened cooked artifacts. It does not see the authored
graph, templates, inheritance, compiler objects, or editor objects.

## What Phase 3 already provides

### 3A -- loaded resource closure

Production VSHADER, VPPL, and VMAT resource objects and decoders load through the
normal loose/VPAK `ResourcePipeline`. A VMAT root owns its validated immutable CPU
closure. Target, domain, layout, accessor ABI, shader, technique, dependency, and
full-fingerprint material-program-layout compatibility are checked before the
closure becomes usable. No native GPU material is created at this boundary.

Primary code:

- `source/shaders/include/vanguard/shaders/shaders.hpp` and `src/shaders.cpp`;
- `source/pipelines/include/vanguard/pipelines/pipelines.hpp` and
  `src/pipelines.cpp`;
- `source/materials/include/vanguard/materials/materials.hpp` and
  `src/materials.cpp`;
- `source/rendering/include/vanguard/rendering/material_program_layout.hpp` and
  `src/material_program_layout.cpp`.

### 3B -- bindless resolution and atomic materialization

`MaterialResourceResolver` resolves reflected logical roles to retained typed GPU
resource references or the exact legal fallback. `MaterialMaterializer` joins
those references with reflected parameter bytes and publishes the complete
content-addressed GPU material compound through the existing shared GPU Scene
transaction. Publication is all-or-nothing and rollback-safe.

The material parameter-word table, resource range, compact layout identifier,
descriptor domains, and `GpuMaterialHandle` use existing renderer ownership.
There is no per-material command list, fence, upload owner, or private GPU Scene
submission path.

The former `MaterialResourceLease` name was intentionally replaced by
`MaterialResourceReference`: it is a retained runtime reference keeping a
resolved resource identity alive, not a time-limited rental or a scene/component
material asset handle.

Primary code:

- `source/rendering/include/vanguard/rendering/material_resource_resolver.hpp`;
- `source/rendering/src/material_resource_resolver.cpp`;
- `source/rendering/include/vanguard/rendering/material_materializer.hpp`;
- `source/rendering/src/material_materializer.cpp`;
- `source/rendering/include/vanguard/rendering/material_program_layout.hpp`;
- `source/rendering/src/material_program_layout.cpp`.

### 3C.1 -- residency and technique admission

`MaterialResidencyRuntime` owns bounded generational residency records,
move-only coalesced `MaterialDemandHandle`s, cancellation/failure, lazy technique
requests, and exact-generation native shader/program sharing over the existing
pipeline cache. It accepts an already loaded VMAT closure; it does not secretly
request resources or record rendering commands.

Material residency and native technique readiness are intentionally distinct. A
material may be resident before an attachment-dependent PSO is ready. Final
demand and technique release moves the material to fence-backed retirement.

The cost correction is part of the accepted implementation: exact-identity hash
chains, dense active-work sets, binary dependency lookup, direct dense-role
admission, independent progress budgets, and deterministic complexity counters
replace high-water scans and repeated linear searches.

Primary code:

- `source/rendering/include/vanguard/rendering/material_residency_runtime.hpp`;
- `source/rendering/src/material_residency_runtime.cpp`;
- `source/rendering/tests/material_residency_runtime_tests.cpp`.

### 3C.2 -- scene binding and atomic replacement

`MaterialSceneBindingBridge` is a narrow bridge keyed by the existing
generational `RenderProxyHandle`. The current implementation covers tracked decal
materials. Each record owns at most one active and one candidate demand.

A candidate becomes active only after the matching
`RenderSceneGpuBindingReceipt` is accepted by the shared GPU Scene publication.
Cancellation, failure, or a canceled publication preserves the last valid active
material. Explicit clear also keeps the old demand until the clear is accepted.
Proxy destruction releases both interests.

Mesh-default material binding was deliberately removed from this bridge. The
future mesh renderable/material-set assembly owner must retain and publish the
complete mesh binding; a second partial mesh-material path would contradict that
ownership.

Primary code:

- `source/rendering/include/vanguard/rendering/material_scene_binding.hpp`;
- `source/rendering/src/material_scene_binding.cpp`;
- `source/rendering/include/vanguard/rendering/render_scene_gpu.hpp`;
- `source/rendering/src/render_scene_gpu.cpp`;
- `source/rendering/src/render_scene.cpp`.

### 3C.3 -- service, cutover, shutdown, and device loss

`RenderingService` owns the material program-layout registry, resource resolver,
materializer, shared pipeline cache, residency runtime, and scene-binding bridge
in dependency order. Its bounded RenderUpdate participant collects existing
retirements, progresses texture and material residency/materialization, and then
progresses scene bindings. Failures use the existing frame-participant failure
channel.

One renderer-owned retirement cutover validates one real complete
graphics/compute/copy `ResidencyFenceSet` and fans it out to material, texture,
mesh, and GPU Scene owners. Normal quiesce requires every binding, demand,
residency, technique, materializer operation, resource reference, and descriptor
cache entry to be gone before reverse-order shutdown.

Device loss is a separate terminal abandon path. CPU work is joined, every
renderer owner is invalidated in dependency order, live generational handles
become stale and harmless, provider `abandon` callbacks replace normal
fence-backed release, and the mandatory RHI backend abandonment does not call
`WaitIdle` or invent fences.

Primary code:

- `source/engine/include/vanguard/engine/rendering_service.hpp`;
- `source/engine/src/rendering_service.cpp`;
- `source/engine/tests/engine_services_tests.cpp`;
- `source/engine/tests/resource_streaming_service_tests.cpp`;
- the material, texture, mesh, GPU Scene, pipeline, and RHI lifetime files
  reached by that service.

## Completed work: 3C.4

The completed production headless vertical path is:

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

The proof performs no draw. It covers, without duplicating lower-layer fault
matrices:

1. loose and indexed-VPAK production loading;
2. exact closure, layout, material, resource-range, and parameter-byte identity;
3. demand/request coalescing and bounded backpressure;
4. Required dependency failure and Optional fallback;
5. concrete technique pending, ready, and failure behavior;
6. distinct-root last-valid replacement;
7. candidate cancellation/failure and publication retry;
8. real graphics, compute, and copy retirement fences;
9. final zero live bindings, demands, residencies, techniques, materializer
   operations, retained resource references, descriptor entries, parameter
   allocations, GPU Scene allocations, and native program/pipeline interests;
10. clean normal shutdown, while retaining the already proven terminal-abandon
    behavior.

`materialRuntimeServiceTests` supplies the missing service-level proof with real
production writers, D3D bytecode, loose files, indexed VPAK loading, exact GPU
Scene readback, concrete pipeline admission, replacement, and real queue fences.
The pass also corrected two concrete contract defects:

- `MaterialSceneBindingBridge` restores the active binding before a pending clear
  is superseded, so an old clear receipt cannot accept or erase a later request;
- renderer quiesce explicitly clears unreferenced native material programs and
  registered fallbacks before the dependent Resources service checks for strong
  handles. Rendering now declares that Resources dependency.

The final cross-configuration recheck found a proof-harness ordering issue. A
service `RunFrame` may return with its renderer CPU publication chain still in
flight; the test was immediately taking direct control of the Render Scene GPU
publisher. Debug timing hid the overlap while Shipping correctly returned
`Busy`. The test now flushes the preceding rendering work and resolves its GPU
Scene contribution outcome before manually preparing/canceling the adversarial
publication. This changed no production contract.

### 2026-09-02 verification record

Premake generation and focused builds used `premake5.exe vs2022` followed by
MSBuild on the generated `build/projects/vs2022/*.vcxproj` projects. All commands
below ran from `D:\ENGINE`.

The affected Debug executable matrix passed **27/27**, including a fresh rerun
after the direct-publication ordering correction:

```powershell
$tests = @('resourcesTests','resourcePipelineTests','packagesTests','streamingTests','assetsTests','assetsPersistentCacheTests','assetGraphTests','assetIndexTests','assetRecookerTests','packagePlannerTests','shadersTests','shaderToolsTests','pipelinesTests','pipelineCacheTests','materialsTests','materialResourceTests','materialToolsTests','texturesTests','rhiTests','rhiNvrhiTests','renderingTests','geometryAllocatorTests','engineServicesTests','resourceStreamingServiceTests','framePipelineCycleTests','textureResidencyServiceTests','materialRuntimeServiceTests')
foreach ($test in $tests) { & "build\output\Debug\$test.exe" }
```

The meaningful Shipping closure projects built successfully and their
executables passed **8/8**, including a fresh post-correction build and run:

```powershell
$tests = @('resourcePipelineTests','streamingTests','materialResourceTests','materialToolsTests','rhiNvrhiTests','geometryAllocatorTests','textureResidencyServiceTests','materialRuntimeServiceTests')
foreach ($test in $tests) { & "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" "build\projects\vs2022\$test.vcxproj" /p:Configuration=Shipping /p:Platform=x64 /m /v:minimal }
foreach ($test in $tests) { & "build\output\Shipping\$test.exe" }
```

The corrected Shipping `materialRuntimeServiceTests` executable also passed ten
consecutive reruns, guarding the asynchronous publication-ordering boundary that
exposed the original harness failure.

`git diff --check` passed apart from existing LF-to-CRLF conversion notices.
The broader command below was also attempted, but the whole solution is **not**
claimed green:

```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" build\projects\vs2022\REDVanguard.sln /p:Configuration=Debug /p:Platform=x64 /m /v:minimal
```

It failed in out-of-scope pre-existing targets: `bootstrapImage` has a
`BootstrapContent*`/`ReadPackageArtifactFunction` mismatch, `entitiesTests`
references the absent `world_render_bridge.hpp`, and `runtime`/`editor` do not
propagate the shaders include path required by `material_program_layout.hpp`.
The affected Phase 3 targets built and passed as recorded above.

## Deferred material work

Material **Phase 4 -- instances, overrides, hot reload, and end-to-end proof** is
planned but is not required to close the current no-rendering runtime milestone.
It includes parent/instance flattening, static versus dynamic overrides,
`GpuMaterialSet` ownership for primitive-local overrides, same-path overlapping
hot-generation replacement, editor-facing service APIs, and a later authored-to-
runtime proof. The visual editor itself remains outside that phase.

Do not begin Phase 4 automatically after 3C.4. The user explicitly wants to
return to the older detours first.

## Older detours and exact return stack

These tracks are not all one dependency chain. Preserve the distinctions below
instead of collapsing them into a fictitious single phase number.

### Return 1 -- Resource Flow Allocator

The Resource Flow Allocator currently has:

```text
Stage 1  logical compiler and execution packets                COMPLETE
Stage 2  dedicated whole-resource baseline and hard budget     COMPLETE
Stage 3  truthful placed-resource RHI contract                 COMPLETE
Stage 4A deterministic placed-range planner                    COMPLETE
Stage 4B transactional native placed provider                  COMPLETE
Stage 4C Resolve/Finish and executable alias integration       COMPLETE
Stage 5  FrameRenderer/RenderingService ownership              OPEN
Graph    real Render Graph/executor connection                 SEPARATE
```

Stage 4C now connects the placed batch to Resolve and Finish, compiles
predecessor finalization at terminal `UseEnd` and successor activation at first
`UseBegin`, enforces transient same-kind/same-queue eligibility, and preserves
dedicated fallback for imports, exports, copy, and multi-queue lifetimes. A real
D3D12 submitted/readback proof passed in Debug and Shipping, together with
generation retirement, cache clearing, and native-release-ledger checks.

Stage 5 remains the next bounded allocator task: establish
`FrameRenderer`/`RenderingService` ownership and lifecycle around the completed
allocator providers. The real graph/executor connection remains a separate later
task.

Do not call the full renderer integration finished merely because allocator
Stages 1-4 pass. Stage 5 ownership and the real graph/executor connection remain.

### Return 2 -- production mesh residency

2026-09-07 refinement: [Geometry rendering study](geometry-rendering-study.md)
is the current source-backed handoff for 9B-10. The user's clarified 1:1 request
applies to RED's relevant CPU preparation/recording machinery, not its CPU chunk
sorter/direct-draw path. GPU-driven indirect drawing is required from the start;
the previous direct-first recommendation is withdrawn. Reuse the existing spatial
GPU-candidate writer and visibility plan builder. No separate pipeline bucket,
mesh batch registry service or temporary CPU renderer is planned.

The first implementation slice is **9B.1 -- material/geometry draw preparation**
in that study: real surface/vertex contract, compatible phase pipeline preparation
and retained geometry/material inputs. It does not yet publish a drawable binding
or implement components/draw passes. The study's world/scene section explains
ownership, LOD fallback and the existing proxy-integration debts.

The post-material re-audit split the continuation into explicit ordered phases.
**Phase 9A -- mesh-to-renderable topology is complete.** Production
`RenderingService` now owns a sealed standard `RenderPhaseRegistry`, progresses
material residency before mesh residency, and initializes `MeshResidencyManager`
with both authorities. For every exact loaded VMESH generation, the manager now:

- retains and coalesces mesh demand;
- uploads the complete anchor LOD and owns its immutable geometry definitions;
- resolves each default VMESH material slot through `MaterialResidencyRuntime`;
- treats a VMAT technique name equal to a durable `RenderPhaseKey` as phase
  participation and ignores techniques not registered by the sealed registry;
- publishes immutable GPU Scene renderable, LOD, primitive, material, and phase
  topology under one content-derived identity;
- releases renderable/material references before fence-backed geometry retirement.

The service-level D3D12 proof loads a real loose VMESH/VMAT closure, verifies
coalescing, reads the published renderable/LOD/primitive/phase records back from
GPU Scene, and reaches zero mesh/material ownership before shutdown.

9A deliberately does **not** publish `GpuRenderableResidency`, primitive geometry
placement, phase shell/bin placement, or a `MeshRenderBindingHandle`. A topology
handle is therefore a stable definition identity, not proof that the mesh is
drawable.

The remaining ordered path is:

1. **9B -- RED batcher foundation and drawable mesh binding:** port the supported
   CPU preparation/indirect-recording machinery, seal the real surface/pass/vertex
   shader contract, keep
   shell/bin ownership inside the batcher, publish complete anchor-LOD placements,
   and expose one strong binding only after the complete GPU image is accepted.
2. **9C -- StaticMeshComponent and minimal scene authoring:** request/release that
   binding through the established lifecycle without duplicating default materials;
   then add the thin camera/light adapters already anticipated by the world plan.
3. **9D -- RenderScene mesh lifecycle:** atomically publish proxy changes,
   replacement, removal, cancellation, and terminal retirement.
4. **Phase 10 -- visible geometry, GPU batching and end-to-end closure:** first
   scene pixels already use CPU broad-phase candidates, GPU visibility/binning/
   argument generation and indirect drawing. Expand coverage of that same path
   and close normal frame/retirement integration. No direct-first detour.

Phase 10 implements the direct graph-consumption connection. However, queue
readiness, frame snapshots and retirement contracts must be agreed earlier in
9B/9D, not discovered after calling a binding drawable. The graph executor is
now implemented; add real feature nodes rather than rebuilding it. Existing
import-wait limitations and the missing normal residency cutover caller are
explicit proof gates in the study. Do not invent private queue ownership.

### Return 3 -- original world/component rendering work

`docs/development/world-rendering-resume-checkpoint.md` is the authority. World
component-integration Phases 1-8 are complete. Phase 9 was deliberately paused
for the production mesh-residency detour.

Resume Phase 9 with `StaticMeshComponent` only after the stable production mesh
binding path exists. The component stores a mesh reference, authored appearance/
material overrides, and render flags; it does not duplicate every mesh-default
material. It requests the mesh through the established component initialization
context, receives the stable renderable/material-set binding from mesh residency,
and uses the existing `VisualComponent`, `IPlacedComponent`, `RenderingRuntime`,
and Render Scene lifecycle. Light components and `CameraComponent` follow.

There is no agreed world/component Phase 10. Historical Render Scene headings
with that number are superseded and must not be confused with this plan.

## Recommended order after this checkpoint

```text
1. Phase 9B: RED batcher foundation and drawable mesh binding
2. Phase 9C: StaticMeshComponent and minimal scene authoring
3. Phase 9D: RenderScene mesh lifecycle proof
4. Phase 10: visible geometry, GPU batching and end-to-end closure
5. Return to material Phase 4 only when instances/overrides/hot reload are wanted
```

The other thread owns Resource Flow Allocator and Render Graph execution. Keep
9B-9D on stable CPU/GPU Scene contracts, coordinate the narrow readiness/lifetime
seams before consumption, and integrate actual feature passes in Phase 10. This
order is a closure-oriented recommendation, not permission to silently broaden
a requested slice. The geometry study records the exact RED-parity boundary.

## Working-tree and review discipline

The working tree contains a large, connected set of uncommitted changes across
assets, material tools, materials, shaders, pipelines, rendering, RHI, and engine
services. Treat all existing changes as user-owned. Never reset, discard, or
rewrite unrelated files. Before editing:

1. inspect `git status --short` and the relevant diffs;
2. read the two material documents and the code at the target seam;
3. search for existing authority before introducing any type or state machine;
4. audit call sites, ownership, failure rollback, device loss, and shutdown;
5. keep recurring work bounded and proportional to live work;
6. use the existing tests and add only the missing vertical/adversarial proof;
7. run `.clang-format` on touched C/C++ files;
8. use `git diff --check` and report exact build/test evidence.

RED and Unreal are architectural sanity checks, not class diagrams to copy.
Compare ownership, publication, caching, fallback, render-thread handoff, and
deferred destruction. Vanguard remains flat, bindless, generational, bounded,
and built around its existing ResourcePipeline, GPU Scene, RHI, and service
contracts.

## Fresh-thread starting instruction

Use the following as the first request in the new thread:

> Read `docs/development/material-runtime-resume-checkpoint.md` and verify the
> post-3C.4 repository state. Material Phase 3 is frozen; do not reopen it or
> begin Phase 4. Resource Flow Allocator Stage 4 is complete. Re-evaluate the
> exact Stage 5 `FrameRenderer`/`RenderingService` ownership boundary against
> current code, then define and implement only that bounded continuation if
> directed. Preserve the recorded Debug/Shipping verification boundary and all
> unrelated dirty-worktree changes.
