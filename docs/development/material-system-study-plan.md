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
- `GpuSceneDefinitions::AcquireMaterials` owns content-addressed GPU Scene
  material definitions.
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

The production middle is missing:

- no `MaterialResourceObject` or `MaterialResourceLoader`;
- no production material compiler adapter for the generic asset build graph;
- no renderer-owned persistent parameter-byte storage;
- no resolver from `VMAT` logical resources to retained texture demands;
- no material residency owner that creates and retires `GpuMaterialHandle`;
- no accepted definition of `GpuMaterial::materialInterface`;
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

## Phase 3 — Runtime Loading and Bindless GPU Residency

### Question

How does a validated `VMAT` become one stable, ready-to-reference
`GpuMaterialHandle` with bounded memory and correct lifetime behavior?

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

### Deliverables

- `MaterialResourceObject` and normal loose/VPAK `ResourcePipeline` loader;
- required shader/pipeline/resource dependency readiness rules;
- renderer-owned bounded parameter-byte arena and batched uploader;
- logical texture parameter to retained `TextureDemandHandle` resolution;
- defaults for optional, missing, loading, and failed resources;
- canonical `GpuMaterialDefinition` key and resource range construction;
- one renderer-global material residency owner with coalescing, readiness,
  cancellation, hot-generation replacement, and fence-safe retirement;
- shared GPU Scene update participation with no per-material command list or
  fence;
- exact shutdown and failure rollback behavior.

This phase ends when a material reaches the GPU and can be referenced by future
renderable/indirect work. It does not perform a draw.

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

The immediate next task is Phase 1. It must stop after the material-interface
and typed-IR contract is clear. It must not drift into editor widgets, draw
submission, culling, indirect command generation, or a library of concrete PBR
nodes.
