# RED/Vanguard world-to-renderer ownership alignment

## Purpose

This study records the P1.5 architecture pass inserted between the corrected P1
draw-interface work and P2 world activation. The goal is not API parity with RED.
It is to apply RED's useful ownership split where Vanguard had accumulated a
renderer-policy bounce through world state, while preserving Vanguard's stronger
durable phase identity, shared residency, bindless material model, GPU Scene and
GPU-driven visibility.

The user added a second explicit design constraint for this pass: minimize data
copying and avoid carrying non-owning renderer-policy views through long-lived
objects. A copy is justified only when it gives the canonical owner stable lifetime;
small generation-local values may be copied by value when that is safer than a
borrowed reference.

## Direct RED evidence

The comparison was re-read against the local RED source rather than inferred from
names:

- `D:/root/R6.Root/Mainline/dev/src/common/world/src/runtimeSystemRendering.cpp:327-345`
  registers world render proxies; `:399-414` asks `GRender` to create a proxy and
  initializes it against the world's render scene; `:538-569` adopts or creates the
  world/preview/thumbnail render scene.
- `common/worldEntities/src/meshComponent.cpp:183-205` loads the mesh and appearance
  material setup before attachment; `:214-224` creates a proxy when the component is
  enabled; `:330-352` supplies render mesh/material setup; `:402-429` routes proxy
  creation through `RuntimeSystemRendering`.
- `common/renderer/src/renderProxyMesh.cpp:402-423` takes ownership of renderer mesh
  and material setup; `:2516-2559` resolves chunk layout + render-stage context and
  asks the renderer material to create the compatible PSO.
- `common/renderer/src/renderMaterial.cxx:2549+` owns compiled technique construction;
  `:2753-2758` resolves/registers the renderer stage used by the pass.
- `common/renderer/src/renderStage.cpp:13-33` owns RED's mutable stage registry. Vanguard
  deliberately keeps its stronger durable `RenderPhaseKey` plus sealed local
  `RenderPhaseId` model instead.
- `common/renderer/src/renderRenderFrame.cpp:2041-2052` selects the depth-prepass stage
  inside renderer graph construction and `:2083-2093` selects solid GBuffer stages.
  Camera/frame configuration therefore selects renderer stages on the renderer side;
  a world component does not carry a copied attachment/stage table to the renderer.

The useful RED rule is consequently an ownership rule: the world owns scene/proxy
lifecycle and authored instance intent; renderer owners decide material/stage/PSO
compatibility and renderer graph/view work.

## Vanguard state before P1.5

Vanguard already had the correct large-scale split:

```text
GameWorld / components                       renderer-global owners
  per-world RenderingRuntime                  MeshResidencyManager
    one RenderScene                           MaterialResidencyRuntime
    proxy admission/retirement                sealed RenderPhaseRegistry
        |                                     GPU Scene / geometry batcher
        +---- retained drawable binding ------+
```

The exception was `RenderingRuntimeConfig::meshDrawPhases`:

```text
RenderingService static contexts
        -> planned service/world projection
        -> RenderingRuntimeConfig span
        -> RenderingRuntime DynamicArray copy
        -> StaticMeshComponent
        -> MeshResidencyManager::RequestDrawable
        -> RetainedMeshDrawable DynamicArray copy
```

The same world copy also fed `CameraComponent`, which discarded attachment
signatures and used only the phase keys to construct a `RenderPhaseSet`.

This was not real per-world policy. `MeshResidencyManager` coalesces one retained
drawable for a shared exact mesh generation and previously rejected a second caller
whose attachment contexts differed. The API therefore exposed variability that the
owner could not support. It also copied renderer policy through layers that did not
own or interpret most of it.

## P1.5 decisions

### Draw compatibility belongs to mesh residency

Topology-enabled `MeshResidencyManager::Initialize` now receives the selected
`MeshDrawPhaseContext` span after the renderer phase registry is sealed. It validates
the keys/attachments once and copies the bounded contexts once into manager-owned
storage. The input span is not retained. This one copy is intentional: standalone
rendering-layer callers may supply temporary source storage, so borrowing the span
would create a lifetime trap even though production currently uses static constants.

`RequestDrawable` no longer accepts contexts from the caller. It uses the manager's
immutable renderer-generation policy when requesting material techniques. A retained
drawable no longer stores another copy and no longer compares one world's copy with
another world's copy.

The geometry-only `MeshResidencyManager::Initialize(GpuSceneDefinitions&, config)`
overload remains unchanged. Draw policy is required only by topology/draw-capable
initialization.

### World runtime keeps no attachment-policy cache

`RenderingRuntimeConfig::meshDrawPhases`, the runtime `DynamicArray` and
`GetMeshDrawPhases()` are removed. `StaticMeshComponent` requests its shared drawable
from `MeshResidencyManager` without forwarding renderer policy.

The runtime also no longer stores a `RenderPhaseRegistry*`. Nothing in the world layer
needs durable-key-to-compact-ID resolution after the camera policy is separated from
mesh preparation. Removing that pointer reduces lifetime coupling and keeps the sealed
registry entirely renderer-side.

### Camera selection is a small generation-local value

`RenderingService` resolves the default DepthPrepass/Opaque IDs once from its sealed
registry and exposes a `RenderPhaseSet` by value. Disabled/unavailable rendering
returns an empty set.

`RenderingRuntime::BindCommands` copies that 64-bit set by value beside the existing
command-system dependency. This tiny copy is preferred to a borrowed pointer because
it cannot dangle and is already the native runtime representation required by
`RenderCameraState`/`GpuView`. `CameraComponent` does not keep a second phase cache;
`MakeState` reads the runtime value directly.

The compact set is never serialized or carried across renderer generations. It is
produced and consumed under the same live `RenderingService`/world session. Durable
asset/runtime boundaries continue using `RenderPhaseKey`.

## Minimum-copy / lifetime rule established by this pass

Use these rules for later world-renderer work:

1. Large or structured immutable renderer policy is stored once by the renderer owner
   that consumes it. Do not mirror it into each world, component or retained drawable.
2. Do not retain a caller-provided span merely to avoid a copy when the source lifetime
   is not encoded in the type. One bounded owner copy is safer than distributed
   dangling assumptions.
3. Small renderer-generation runtime values such as `RenderPhaseSet` may cross the
   boundary by value. An eight-byte value copy is preferable to another owner pointer.
4. Components pass authored intent, resource/retained handles and instance state.
   They do not relay renderer attachment, PSO or material-technique policy.
5. Cross-owner pointers remain only where there is an actual service/runtime
   dependency with an explicit lifetime, such as the existing shared mesh-residency
   and render-command owners.

## Intentional divergences from RED

P1.5 does **not** copy RED's older implementation choices:

- Vanguard keeps durable `RenderPhaseKey` and a sealed compact registry rather than
  RED's mutable append-order stage IDs.
- Vanguard keeps global bindless Resources/Samplers domains, stable material/resource
  identities and GPU Scene material IDs. No RED-style per-material fixed-slot binding
  packet is introduced.
- Vanguard keeps shared exact-generation mesh/material residency across worlds rather
  than rebuilding mesh/material topology in each proxy.
- Vanguard keeps GPU-driven visibility and explicit per-view phase masks rather than
  importing RED's CPU stage-bucket machinery.
- Physical attachments, clear behavior, graph scheduling, command recording,
  submission and presentation remain with existing renderer/graph owners.
- Reverse-depth remains per camera/view.

The intended direction is therefore **RED ownership boundaries with Vanguard renderer
technology**, not RED implementation parity.

## Resulting P2 boundary

P2 no longer copies a renderer attachment table into a world. Its remaining job is
activation/order proof around existing owners:

```text
RenderingService ready for the active generation
        -> GameWorldService::BeginWorld
        -> allocate one RenderingRuntime
        -> bind shared MeshResidencyManager
        -> bind RenderCommandSystem + value default camera phase set
        -> register runtime systems
        -> initialize world
        -> only then may streamed renderable components attach
```

Headless/disabled rendering remains valid: mesh residency is unavailable and the
default camera phase set is empty; worlds without rendering components can still
exist, while enabled static-mesh/camera components fail their existing capability
checks rather than fabricating renderer state.

P1.5 does not implement P3 reflection closure, P4 graph/cooker/fixture correction,
camera selection, viewport/output policy or presentation.
