# Concrete rendering components -- 9C

See [the RED light-foundation review](light-foundation-red-review.md) for the
ownership comparison and the photometry, property-update and GPU-selection
contracts identified before the maturity pass. The current implementation and
remaining lighting-consumer gates are in the [light contract](../../rendering/docs/light-contract.md).

Source checkpoint: 2026-09-09. Implementation and static review only. No project
generation, compilation, test authoring or test execution was performed. Neither
the 9B.2 proof gates nor the 9C executable proof gates are closed by this work.

## Saved data and registration

StaticMeshComponent, LightComponent and CameraComponent are stable objects created
through ComponentRegistry::RegisterObject. Their *ComponentData structs are the
schema-decoded values required by that existing factory contract. Placement and
parent attachment remain in the existing prefab/materializer/TransformRuntime
path. Requests, handles, residency demands and drawable bindings are transient.

ManagedGameWorldService registers the three built-in schemas before the application
registration callback. Standalone worlds can call RegisterStaticMeshComponent,
RegisterLightComponent and RegisterCameraComponent before sealing their registry.
Tools can register the schemas returned by the corresponding Get*Schema functions
with reflection and use the existing schema/prefab writers.

The static mesh schema stores a typed ResourceReference, visual scale, layer and
visibility masks, and shadow/decal flags. Its mesh field is a required dependency:
ordinary cooked prefab loading includes and loads it. Explicit object creation
can use the component's resource-request fallback. This slice does not switch to
soft dependencies and silently omit the mesh from package dependency expansion.

References identify logical runtime outputs, not DDC files, FBX paths, .vmeta
filenames or build fingerprints. The source asset registry and persistent
asset/output-to-resource mapping remain editor work. Default material assignments
remain in the mesh; the component does not duplicate them.

## Static mesh preparation and lifetime

Worker initialization validates data and captures ComponentInitializeContext
resource access. Main-thread attachment acquires/requests the resource through
that access. RenderingRuntime has a bounded intrusive pending queue: default
capacity 65,536 and 128 progress visits per frame, with round-robin requeue.
There are no new component-table scans, per-poll allocations or scene-wide locks.

```text
Inactive -> Loading -> Preparing -> Prepared
                         failure -> Failed
disable/detach -> cancel pending work and release independent ownership
```

The component requests its own MeshDemandHandle, applies metadata bounds through
VisualComponent, waits for topology, then requests the shared MeshDrawableBinding.
Only actual binding readiness permits CPU mesh-proxy admission. Prepared means
the component retains a ready binding; proxy admission can still be pending.
Inherited admission/binding accessors expose that distinction.

MeshResidencyManager and MaterialResidencyRuntime own the material closure. Failed
load/preparation/admission enters the existing runtime failure channel. Rapid
re-enable waits for a previous closure's withdrawal instead of treating that
transient state as failure. Each component releases only its own ownership.

Proxy admission supplies current placed transform, conservative mesh bounds,
visibility properties and a retained CPU mesh handle. VisualComponent schedules
normal relink after admission. Disable cancels pending admission or uses existing
proxy retirement, then releases preparation ownership. Detach/uninitialize remove
pending nodes before component destruction. Destructors reject unfinished
lifecycle ownership in all configurations.

## Renderer composition and cameras

RenderingRuntimeConfig::meshDrawPhases supplies explicit phase keys and attachment
signatures, copied during world initialization. Source storage must survive that
initialization. Keys must be valid and unique; count is bounded by
MaximumRenderPhases. A bound renderer registry validates membership. Selected
meshes must satisfy 9B's complete and compatible phase-coverage contract.

ManagedGameWorldService binds the engine MeshResidencyManager, RenderCommandSystem
and RenderPhaseRegistry. Standalone composition calls BindMeshResidency and
BindCommands before initialization. Components never invent attachment formats.
An empty context list supports worlds without mesh/camera components but cannot
prepare one of those components.

CameraComponent provides a perspective, on-demand camera in existing storage.
Its phase set is resolved once from configured phases at attachment. Existing
viewport owners select its GetCamera() handle; component creation does not choose
an active viewport camera. Projection/masks are saved; placed transforms supply
pose. Enable changes camera state; detach unregisters the camera.

Transform workers enqueue each changed camera once on an intrusive atomic stack.
The queue is capped (default 1,024); overflow reports a worker relink failure.
No camera storage mutation or renderer join occurs on a transform worker.
After joining the transform tail, ManagedGameWorldService calls
RenderingRuntime::FlushCameraTransforms before frame view preparation. This uses
RenderCommandSystem's existing previous-frame join boundary. Standalone drivers
must do the same. TransformRuntime::IsProcessing provides an O(1) drain guard;
the diagnostic GetStats scan is not used. Dirty-camera nodes have backlinks;
structural cancellation after the producer join unlinks one node in O(1).

The [performance preflight](rendering-performance-preflight.md) records subsequent
admission/retirement complexity fixes and the ownership constraints for 9D.

LightComponent supports directional, point and spot lights. Local lights use
conservative range bounds. Directional lights use explicit Global spatial mode:
the existing unindexed candidate list retains them independently of scene extent,
bounds and frustum rejection, while visibility/layer masks still apply. Their
small placement box is only transform bookkeeping, not an influence volume.
Relink, disable, retirement and removal reuse existing owners. Direct light proxy
creation also selects Global for directional lights, and kind updates migrate
between global and bounded collection. GPU publication already carries the light
kind and normalized transform-derived direction. Actual lighting pixels remain
part of the deferred drawing path.

To author the initial directional light, set
`LightComponentData::kind = static_cast<u8>(rendering::RenderLightKind::Directional)`;
its placed-component orientation supplies direction. Range can be zero.

The light maturity pass adds validated `SetProperties`, schema-v2 layer/visibility
masks, stable proxy identity across enable/disable, latest-data admission, and
world-metre range bounds independent of scale. Directional intensity is lux;
point/spot intensity is candela; color is linear Rec.709. GPU records now carry
an explicit active bit, with generation and shadow-index guards for consumers.
Per-view GPU list ownership, lighting evaluation and actual shadows remain open.

## 9D scene ownership and deferred proof

StaticMeshComponent now supplies its ready MeshDrawableBinding during admission.
RenderScene retains independent ownership and publishes only its corresponding
renderable. Component release therefore cannot withdraw a mesh still owned by
the live scene. The const component accessor remains an owner-thread borrow.

Mesh binding replacement/clear preserves the previous accepted drawable until
the new GPU binding revision is accepted. Resolution visits only queued changed
payloads under a fixed budget. `RetainMeshDrawable` transfers the accepted closure
to later CPU recording ownership; releases must follow the recording join. GPU
work remains protected by existing withdrawal and fence retirement. Existing
transform relinks remain the bounds authority. Phase 10 supplies GPU-driven indexed
indirect drawing. Component-level mesh replacement/hot reload, per-instance material
overrides and editor source references remain separate work.

The final authorized verification batch must cover schema/prefab materialization,
scripted scene creation, pending load/cancel, shared meshes, disable/re-enable
across withdrawal, missing/wrong assets, admission exhaustion, movement before and
after admission, camera queue overflow/join order, light/camera enable/removal,
world/level stream-out and shutdown. Native preparation requires valid shader and
material layouts plus explicit phase contexts. No executable results are claimed.
