# Light foundation review against RED

2026-09-09. Static source comparison; no builds, test execution or test authoring.
This is a foundation review, not a claim that lighting or shadows render yet.

Follow-up: the [light maturity source pass](../../rendering/docs/light-contract.md)
now implements canonical units/validation, stable visibility and property edits,
coherent influence bounds, schema masks and GPU eligibility/shadow guards. The
findings below record the pre-pass baseline. Per-view GPU list ownership, lighting
evaluation, shadows and executable verification remain open as listed there.

## Verdict

Keep the current component -> RenderingRuntime -> RenderScene proxy -> GPU Scene
ownership. It is a suitable foundation and does not need another light manager.
Do not freeze the current minimal property/GPU interpretation as the final lighting
contract. Units, updates and per-view eligibility need explicit closure before
the first lighting consumer is considered production-ready.

## RED evidence

Paths below are under `D:/root/R6.Root/Mainline/dev/src/common`.

- `worldEntities/include/lightComponent.h:15`: LightComponent derives from the
  visual component and RenderLightBase. It retains loaded texture/IES resources
  and the render proxy, rather than making the renderer depend on the component.
- `worldEntities/src/lightComponent.cpp:138`: bounds are recomputed from the
  placed transform and light shape/radius.
- `worldEntities/src/lightComponent.cpp:312`: enabling changes proxy visibility.
  It does not destroy and recreate the proxy just to switch the light off/on.
- `worldEntities/src/lightComponent.cpp:333`: proxy initialization transfers
  units, intensity, EV, gamma color/temperature, radius, shadow policy, attenuation,
  channels and feature participation. These are intentional interpretation
  boundaries, not merely a bag of shader floats.
- `worldEntities/src/lightComponent.cpp:394`: point, spot and area proxy variants
  are chosen through the world rendering runtime. Resource/render ownership stays
  separate from component placement.
- `renderData/include/renderLightBase.h:98`: attenuation policy and the subsequent
  RenderLightBase accessors distinguish radius, unit, intensity and light type.
- `renderer/src/renderProxyLight.cpp:146`: the proxy carries explicit light units;
  initialization derives final intensity rather than assuming an unnamed scalar
  has the same meaning for every light shape.
- `renderer/src/renderProxyLight.cpp:438`: per-view distance/fade/black-light
  eligibility is distinct from resource/proxy lifetime.
- `renderer/src/tiledDeferred.cpp:181`: sun/moon direction, angular size and color
  enter through frame base-lighting constants. RED's global sun path is not just
  a point light with a very large radius. A local light's `m_directional` property
  is not evidence that it is that global sun path.

## What Vanguard already gets right

Stable reflected component objects use existing placed/visual lifecycle and
bounded world admission. RenderScene owns proxy payloads; GPU Scene owns stable
generational identities and publication/retirement. The GPU publisher copies
light kind, normalized transform direction, color, intensity and cone cosines.
No component pointer is needed by the lighting shader.

Directional proxies use Global collection. This is an intentional Vanguard
adaptation: reuse the existing retained candidate list and skip spatial rejection,
while preserving masks and visibility filtering. It does not claim to copy RED's
environment-driven sun implementation. Global membership survives movement and
is removed through existing proxy retirement/destruction. Kind changes migrate
spatial membership at the existing mutation boundary.

The 96-byte GpuLight layout already has sourceRadius, sourceLength and shadowData
fields. `shadowData` defaults to InvalidGpuSceneIndex; a zero-initialized-looking
`GpuLight{}` does not accidentally select shadow record zero. `castsShadow` is
currently a request flag, not proof that a shadow allocation exists.

## Baseline contracts identified by the review

1. **Photometry and color.** Component/proxy intensity and RGB currently pass
   through without an explicit unit/color-space contract. Before shading, define
   linear working-space RGB, direction sign, cone half-angle convention, local
   attenuation/range cutoff and type-dependent intensity semantics. Recommended
   canonical shader inputs are directional illuminance and local luminous
   intensity, with any authored lumen/EV conversion on the CPU. This is a
   recommendation, not implemented photometric conversion. Do not infer shader
   semantics from the existing default `intensity = 1`.

2. **Property changes and stable identity.** The component currently exposes
   initial saved data, not RED-style color/intensity/radius setters. Its enable
   implementation retires/recreates a proxy; that is correct for visibility but
   creates unnecessary identity churn and is a poor basis for fades/shadow caches.
   Extend existing proxy visibility/property updates for ordinary changes. Handle
   edits during pending admission explicitly. Range/type edits must update
   conservative bounds and payload as one accepted change; merely calling
   UpdateLightProxy with a larger range leaves old spatial bounds unchanged.
   Do not introduce a second property queue or mutate published frame data.

3. **GPU eligibility.** CPU collection respects visibility and layer masks, but
   GpuLight publication currently has no explicit active flag and does not carry
   the proxy's layer mask. A future shader must not iterate every allocated light
   record and assume it is eligible. Establish per-view selected light indices
   (and an explicit GPU active/disabled contract) before consuming the table.
   Separate global lights from local cluster assignment after common selection.
   This also prevents hidden/retiring records from contributing accidentally.

4. **Shadow policy versus ownership.** Preserve the invalid shadow index until a
   real owner allocates and publishes shadow data. The castsShadow request must
   not become an unchecked shadow-buffer index. Directional cascade policy and
   local shadow resources belong to renderer/frame owners, not components.

5. **Resource-backed extensions.** IES/projection resources, source shape and
   shadow/lighting channels are legitimate future extensions. Resolve them with
   existing ResourceReference/request/handle machinery and retained renderer
   dependencies. Do not serialize descriptor indices or copy RED's complete
   feature set into 9C before it has consumers.

## Next bounded lighting work

Close the interpretation contract, add stable visibility/property updates with
coherent bounds, and define per-view global/local light selection. The first
directional-light test should exercise that real path. Area lights, IES, cookies,
environment-driven sun animation and advanced shadows can follow without replacing
the component/proxy ownership foundation.

Deferred verification must include light changes during admission, off/on without
identity replacement, range expansion across cells, kind changes, masked/hidden
global lights, multiple views, out-of-extent directional transforms, removal with
frames in flight, and invalid shadow references. Source inspection alone does
not close these executable gates.
