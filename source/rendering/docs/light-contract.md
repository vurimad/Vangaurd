# Light foundation contract

Source checkpoint: 2026-09-09. This closes the bounded light-foundation source
pass following the [RED comparison](../../entities/docs/light-foundation-red-review.md).
Compilation, project generation, test authoring and execution remain deferred.

## Canonical data

`render_light.hpp` defines the common component/proxy validation contract.
RGB is non-negative linear Rec.709. Directional intensity is illuminance in lux;
point/spot intensity is luminous intensity in candela. The component default of
one is one of the relevant units, not exposure compensation. Importers/editor
controls must convert other authored units and gamma colors at their boundary.
No lumen, EV or temperature conversion is implemented in this slice.

The placed local +Z axis follows emitted rays. A directional surface-to-light
vector is its negative. GPU publication normalizes this axis. Range is a finite
world-space cutoff in metres, independent of placement/visual scale. Local range
must be positive; directional range may be zero and does not limit influence.
Spot angles are half angles in radians, with `0 <= inner <= outer < pi/2` and
positive outer angle. Equal angles describe a hard edge. Unused cone values must
still be finite, non-negative and ordered.

The lighting consumer must implement inverse-square local attenuation with a
smooth finite range cutoff and an explicit near-source regularization policy.
Directional illumination has no distance attenuation. Cone falloff must handle
equal angles without dividing by zero. That shader evaluator is still deferred;
the data contract alone does not produce photometrically validated pixels.

## Lifetime and edits

LightComponent admits a proxy even when disabled. Enable/disable updates
visibility while retaining proxy/GPU identity. Detach and uninitialize use the
existing visual-proxy release path. Pending admission applies the latest data
and enabled state in the owner-thread admission epilogue; rejection rolls back
the admitted proxy and reports through RenderingRuntime.

`SetProperties` is a main-thread operation, rejected during transform processing.
It validates before accepting data and updates an existing proxy transactionally.
Range/type changes refresh component world bounds through the existing transform
queue. A rejected edit retains data and proxy properties; it may leave a harmless
transform refresh scheduled for the old data. There is no automatic retry queue:
callers must handle false and retry at an allowed mutation boundary.

RenderScene derives local bounds from position and range at creation, property
update and relink admission. Spot bounds use a conservative sphere. Directional
lights use Global membership and a small bookkeeping box. Type changes move
between the existing spatial lists. Influence edits use the existing per-proxy
relink admission gate; outstanding relinks return Busy rather than overwriting
new bounds later. GPU journal admission precedes committed property changes.

Light schema version 2 adds layer and visibility masks; version 1 remains readable
with the data struct's default masks. No runtime pointer or GPU index is saved.

## GPU and view contract

GpuLight remains 96 bytes. Bit 0 means shadow intent; bit 1 means active.
Publication sets active only for visible lights with a nonzero visibility mask,
positive intensity and a nonblack color. Disabled records retain their allocation.
`shadowData` stays invalid until a renderer shadow owner supplies real data.
`HasGpuLightShadowData` checks intent and the invalid-index sentinel; the future
shadow owner must also validate its own table bounds and lifetime.

A shader must consume lights selected for its view, never assume that every
allocated record is eligible. Reuse RenderScene light-payload collection: it
already applies visibility/layer masks and global versus bounded collection.
Resolve selected proxies to GPU index/generation, retain them for the accepted
frame, then separate global lights from local cluster assignment. The new
`IsGpuLightActive` helper checks generation, active state and the view visibility
mask; CPU layer selection remains mandatory. Retirement need not overwrite an
old record still used by an accepted frame: new selections exclude retired
proxies, and existing GPU lifetime/fence rules govern storage reuse.

10.2.4 adds retained per-view directional index/generation lists and a simple
unshadowed diffuse consumer; see the current checkpoint below. Cluster assignment,
shadow allocations and cascades remain deferred. Area
lights, IES, cookies and environment-driven sun animation remain extensions of
the existing owners, not reasons to add another light manager.

## 10.2.4 directional diffuse source checkpoint

The camera pass reads the persistent GPU light records through at most eight
selected identities per view. Selection is under the existing family scene seal,
with one family read guard and layer/visibility filtering. Global proxies now
have exclusive membership in the existing spatial index, separate from bounded
out-of-range entries. No duplicate light payload or registry is retained. Exceeding
the per-view budget fails preparation; it never silently drops contributions.

The shader checks page bounds, generation, active state and directional kind,
then computes `baseColor * sum(color * lux * max(dot(N, -direction), 0)) / pi`.
The result is unshadowed linear HDR. Emissive accumulation is explicitly excluded
from this milestone. Normal-zero clear values produce black background; no lights
also produces black. CameraColor uses RGBA16Float, with a separate output-format
resolve PSO. Exposure, tone mapping, PBR and a shared lighting API remain later
studies. Compilation and executable/photometric proof remain deferred to 10.3.

## Deferred executable gates

The authorized final batch must cover schema v1/v2 defaults; edits while admission
is pending; initially disabled lights and off/on identity retention; failed
property/GPU journal admission; range expansion across cells; scaled placement;
point/spot/directional transitions; concurrent relink admission and Busy retry;
masked global lights across views and outside the spatial extent; active/generation
GPU fields; retirement with frames in flight; invalid shadow references; and
first directional-light pixels once a lighting consumer exists.
