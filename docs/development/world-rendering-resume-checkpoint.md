# World-to-rendering resume checkpoint

Date: 2026-08-28

## Purpose

Work on concrete game-world rendering components is intentionally paused while Vanguard builds the production mesh resource and GPU-residency path. Resume from this document after that detour; do not reconstruct the phase order from the historical RenderScene plan.

## Active plan and completed boundary

The game-world/component integration plan contains nine phases. There is no Phase 10 in this plan.

1. **Component initialization context and entity assembly barrier — complete.** Create the complete stable sibling set before initializing any sibling; provide sibling resolution, retained resource access, I/O priority, Jobs continuation, one completion counter, and exact reverse rollback.
2. **Entity-reference integration — complete.** Publish stable references only after component lifecycle preparation, wait for required references, support revisions, and withdraw references before destruction.
3. **Placed-component integration — complete.** Bind every `IPlacedComponent` to its entity transform root through initialization, attachment, release, and rollback.
4. **Attachment lifecycle — complete.** Forward `Attach`, separate sibling-wide `PostAttach`, reverse `Detach`, and exact rollback between lifecycle states.
5. **Enable/disable semantics — complete.** Combine component-local and entity enabled state and preserve correct callback order for runtime transitions.
6. **Rendering ownership decision — complete.** Stable RED-style visual components and the per-world `RenderingRuntime` are the primary ownership model. `WorldRenderBridge` was removed from production ownership; stale tests and documentation mentioning it are historical cleanup, not an alternate architecture.
7. **Proxy admission and retirement — complete.** `RenderingRuntime` provides bounded deferred admission, cancellation, readiness blocking, immediate visibility removal, and frame-delayed retirement.
8. **Non-cell streaming integration — complete.** Distant-proxy streaming events enter dense per-world proxy state in `RenderingRuntime`. A proxy becomes render-ready only after `RenderScene` admission. Release hides it, transfers it to deferred retirement, and only then acknowledges the streaming resource release. Required-child saturation and anti-streaming locking remain in `WorldStreamingGrid`, matching RED's prefab-proxy replacement behavior.
9. **Concrete rendering components — deliberately deferred.** Implement `StaticMeshComponent` first, then light components and `CameraComponent`, using the common lifecycle above.

The old `source/rendering/docs/render-scene-implementation.md` sections named “Original Phase 9” and “Original Phase 10” belong to an earlier RenderScene collector/frame-wiring roadmap. That document explicitly marks both as superseded. They are not the remaining Phases 9 and 10 of the game-world/component plan.

## Temporary detour: production mesh residency

Before Phase 9, build a clean production path from streamed `.vmesh` data to stable GPU residency and make it fit the existing GPU Scene ABI rather than introducing parallel renderable structures.

The detour must establish these ownership transitions:

```text
ResourcePipeline request
    -> validated vmesh resource object and retained dependency handles
    -> mesh metadata residency and requested geometry-page residency
    -> allocation in renderer-global vertex/index/auxiliary buffers
    -> stable GPU geometry/range/stream/decode records
    -> stable renderable/LOD/primitive definitions
    -> RenderScene mesh proxy resolves to GpuRenderableHandle
    -> GPU Scene instance references the stable renderable
    -> fence-safe eviction and allocation reuse
```

It must reuse and satisfy the existing GPU Scene tables and definitions, especially:

- `GpuRenderable`
- `GpuLod`
- `GpuPrimitive`
- `GpuPhaseParticipation`
- `GpuGeometryRange`
- `GpuVertexStream`
- `GpuPositionDecode`
- the existing material and material-set tables

Do not add a second mesh/renderable index space that competes with those tables.

### Material ownership at the pause

A Vanguard `.vmesh` does not embed `.vmat` payloads. Each mesh material slot contains a typed reference to a separate `.vmat` resource. The intended default chain is:

```text
vmesh submesh -> vmesh material slot -> vmat resource -> GpuMaterialHandle
```

`GpuPrimitive` carries the default resolved material. A `GpuMaterialSetHandle`, when valid, supplies per-instance primitive overrides; an invalid material set means use the primitive defaults. The production resource adapter that resolves `.vmesh` and `.vmat` resources into these GPU definitions is not implemented yet and belongs to the mesh-residency detour. Do not serialize every mesh-default material again in `StaticMeshComponent`.

### Known missing production pieces

- A production mesh `ResourceObject`/decoder and retained mesh dependency representation.
- Renderer-global geometry-buffer allocation, upload, residency, eviction, compaction/growth policy, and fence-safe reuse.
- The adapter from mesh metadata and material dependencies to existing GPU Scene definition handles.
- Readiness that distinguishes decoded metadata, minimum renderable residency, and optional higher-LOD/page residency.
- Failure/cancellation propagation from mesh residency back through proxy admission and world streaming.

The distant-proxy event path is structurally connected, but it cannot become a real rendered far mesh until this resource/residency path exists.

## Exact resume point after the detour

Resume Phase 9 with `StaticMeshComponent`:

1. Define its serialized data and reflection schema. Store the mesh reference plus only authored appearance/material overrides and component render flags.
2. Register it as a stable component through `ComponentRegistry`.
3. Acquire/request the mesh through `ComponentInitializeContext`; let the mesh residency owner resolve the stable GPU renderable and default materials.
4. Use `VisualComponent`/`IPlacedComponent` transform ownership and bounded `RenderingRuntime` admission. Do not scan ECS tables and do not introduce another bridge.
5. Bind the admitted `RenderScene` mesh proxy to the resolved `GpuRenderableHandle` and optional `GpuMaterialSetHandle`.
6. Preserve detach, disable, cancellation, stream-out, and retirement behavior already established by Phases 1–8.
7. After static mesh ownership is sound, implement light components and then `CameraComponent` against the existing RenderScene and camera-storage contracts.

No additional component-integration phase was agreed after Phase 9. Define any later phase explicitly rather than assuming a historical “Phase 10.”

## RED references to keep beside the work

- `common/world/src/prefabProxyMeshNodeInstance.cpp`
- `common/world/src/runtimeSystemNodeStreaming.cpp`
- `common/world/include/runtimeSystemRendering.h`
- `common/world/src/runtimeSystemRendering.cpp`
- RED mesh/node-instance resource loading and appearance/material setup paths

Retain RED's ownership and ordering ideas, but use Vanguard resource objects, RHI buffers, GPU Scene handles, and cooked formats.
