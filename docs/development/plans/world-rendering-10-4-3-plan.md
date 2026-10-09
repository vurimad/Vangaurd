# 10.4.3 renderer/world draw contract and fixture compatibility

## Status and purpose

**P0, the corrected P1, P1.5, P2, P3, P4, P5 and P6 are closed; 10.4.3 is complete.** P0 completed current-tree/RED architecture
proof and clean contract review. The user explicitly authorized P1 on 2026-09-16. A
later duplicate-authority audit reopened P1 and reduced it to the owner-respecting
draw-interface seam described below. On 2026-09-17 the user explicitly inserted P1.5
before P2 to align the world-renderer boundary more closely with RED's useful ownership
model and to minimize data copying/dangling policy views. Its final CURRENT-tree review
is clean at 0 blocker / 0 high / 0 medium. The user then explicitly authorized P2 on
2026-09-17 with the constraint to stay close to RED's useful lifecycle model. P2 focused
validation is green and its final independent CURRENT-tree post-correction review is clean
at 0 blocker / 0 high / 0 medium. The user subsequently authorized P3 end to end on
2026-09-17 with the same RED-alignment/no-duplicate-authority constraint. P3 is closed
with focused static-surface ABI validation and two independent CURRENT-tree reviews at
0 blocker / 0 high / 0 medium. On 2026-09-17 the user explicitly authorized P4 after a
fresh many-pass flexibility audit. P4 is now closed on the existing per-phase
`pipelines::AttachmentSignature` / `MeshDrawPhaseContext` seams without establishing a
global GBuffer/pass contract or duplicate phase/scheduling ownership; its final independent
CURRENT-tree review is 0 blocker / 0 high / 0 medium. P5 was then explicitly authorized on
2026-09-17, including a structural repair of the pre-existing renderer-feature shader contract
through canonical shader build settings rather than a bootstrap bypass. P6 was explicitly
authorized on 2026-09-17 and is now closed. Its exact validation includes corrected standalone
fixture cleanup plus exact terminal source-generation retirement evidence. The first closure
review's production blocker and child-oracle medium were corrected, and the fresh post-correction
independent CURRENT-tree closure review reports **0 blocker / 0 high / 0 medium**.

10.4.1 established source/session ownership. 10.4.2 established typed renderer
resource discovery and cooked shader/pipeline closure. This phase makes the
selected renderer, world runtime, static-mesh/material path, render graph and
cooker agree on one draw contract before camera/output integration in 10.4.4-5.

This is a cross-agent contract. An implementing agent records evidence and exact
validation here and in the rendering progress document. A separate agent reviews
the current tree before a gate is marked complete. `.planning/` and `.pi/` are
working coordination state rather than architectural authority. The active task
triplet is `.planning/world-rendering-10-4-3-draw-contract/`; `.pi/` retains its
existing global coordination settings and receives no task-specific duplicate.

## Read-first context

- Root `AGENTS.md` and [development document map](../README.md).
- [World/rendering integration study](../studies/world-rendering-integration-study.md),
  especially G3, G7 and sections 10.4.2-10.4.5.
- [Geometry rendering study](../studies/geometry-rendering-study.md).
- [Shader/pipeline runtime contract](../../../source/rendering/docs/shader-pipeline-runtime.md).
- Material and mesh residency designs and execution plans linked by the document
  map. Historical checkpoints provide context, not completion authority.
- REDengine is the primary architectural and behavioral baseline. P0 must trace
  exact RED ownership, lifecycle, phase/technique flow, frame-loop handoff,
  render-graph construction, submission ordering and failure behavior before
  finalizing Vanguard's contract. Record exact paths/symbols and the conclusion
  each source supports. Every divergence must be named and justified. Vanguard's
  GPU-driven bindless material/resource model is the intentional rendering
  divergence: preserve material-ID-per-instance and global descriptor-domain
  access while applying RED's surrounding ownership and lifecycle structure.

## Existing authority to preserve

- `RenderingService` owns the selected renderer, sealed `RenderPhaseRegistry`,
  shader map, native binding-layout lifetime, pipeline factories and descriptor
  domains.
- `GameWorldService` owns world startup and creates one `RenderingRuntime`; the
  runtime binds the existing command system, phase registry and mesh residency
  before streamed component activation.
- `RenderPhaseRegistry` maps durable `RenderPhaseKey` values to compact runtime
  `RenderPhaseId` values. Cooked materials store durable keys. No serialized or
  hard-coded compact phase index is allowed.
- `MeshResidencyManager` owns renderable-topology validation and draw preparation.
  `ValidateMeshDrawLayout` owns bounded VMESH/pipeline vertex-layout validation.
- Shader reflection and pipeline files own compiled interfaces and pipeline state.
  Material and pipeline owners retain material resources and native pipeline
  lifetime. The render-flow allocator owns graph resources, not materials.
- `FrameRenderer` and geometry graph nodes own graph attachment use, depth clear,
  phase recording and indirect draw consumption.
- Existing material, mesh, shader and pipeline cookers plus BuildSystem/VDDC/VADI
  own cooked output. `bootstrapImage` is a temporary development-content frontend,
  not packaged-game architecture.

No gate may add a second phase registry, shader map, pipeline factory, material
owner, world rendering runtime, cooker/cache, direct-draw fallback, CPU visible-set
readback, per-entity renderer scan or editor-private runtime path.

## Current-tree facts that P0 must confirm

These are observations, not accepted final design decisions:

1. `RenderingService` currently initializes, registers standard phases and seals
   its registry before material/mesh residency and world startup.
2. `GameWorldService::BeginWorld` creates `RenderingRuntime` and binds mesh
   residency plus commands/phases before registering the runtime systems.
3. Cooked material techniques use durable DepthPrepass/Opaque keys; mesh residency
   resolves those keys against the sealed registry and validates the selected
   pipeline before preparing a draw.
4. The static-surface input contract is split between
   `mesh_draw_layout.{hpp,cpp}`, shader reflection, generated surface shaders and
   cooker-authored vertex/instance declarations. `MeshDrawInstance` is a 16-byte
   GPU-written per-instance record at binding 15; `StaticSurfaceDrawContext` is a
   24-byte draw constant contract.
5. Geometry depth clear follows each view's `ReverseDepth` flag (`0.0` reversed,
   `1.0` forward). The development fixture currently cooks depth and opaque
   pipelines with `LessEqual`; current executable evidence reports the camera
   surface depth-state contract failing.
6. GBuffer pipeline literals currently describe three colors
   (R8G8B8A8 UNorm, two R16G16B16A16 Float) and D32 depth, while renderer feature
   pipelines and graph declarations have their own format declarations. P0 must
   identify the authoritative definition and every consumer before consolidation.
7. Fixture mesh bounds are manually authored. P0 must establish format semantics
   and the correct local-space AABB/sphere derivation rather than merely widening
   values until culling passes.

## Cross-phase invariants

- A renderer contract is immutable for an active renderer device generation.
  Structured draw compatibility is owned once by the renderer owner that consumes it;
  worlds do not mirror attachment-policy tables before renderable attachment.
- Prefer one bounded owner copy over a retained caller span with implicit lifetime.
  Small renderer-generation values such as `RenderPhaseSet` may cross the world seam
  by value when that removes a longer-lived pointer/cache.
- Durable phase keys cross cooked/runtime boundaries. Compact IDs remain local to
  the sealed registry and are resolved once at the existing bounded owner seams.
- Attachment and depth compatibility are explicit. No convenient default output
  format, implicit reverse-depth assumption or pipeline coercion is allowed.
- Shader reflection is authoritative for compiled program interfaces. Existing shared
  C++ ABI structs define engine-written static-surface data. Renderer startup installs
  the canonical native b0 layout through the existing material binding-layout owner
  and validates the existing global descriptor-domain prerequisites; it does not own
  a parallel expected-ABI/domain fact bag. Each compiled static-surface material shader
  is validated against the canonical register/space/size/stage ABI at bounded
  material-technique admission before readiness. Native global descriptor-domain
  presence/type is validated separately from reflection metadata.
- Validation is bounded by renderer features, material techniques and VMESH
  layout records. It performs no scene-wide/entity-wide scan and introduces no
  per-draw lock or runtime compilation.
- Package and editor-loose sources produce the same typed cooked contract through
  existing asset/build/streaming owners. Runtime code contains no fixture path.
- Failure before world activation is specific and rollback-safe. A mismatched
  renderer/material/mesh contract cannot partially attach a drawable world.
- 10.4.3 does not select a camera, create presentation output, submit application
  frames, implement shutdown draining, or claim pixels. Those remain 10.4.4-7.

## Ordered gates

### P0 — current-tree architecture proof and reviewed execution contract

**Accepted read-only architecture gate. No production implementation was performed.**

Trace and record:

1. Renderer initialization order from catalog load through shader reflection,
   native binding layouts, pipelines, sealed phases, material/mesh residency and
   service publication. Include every rollback path relevant to a contract error.
2. World startup order from `Configure`/`BeginWorld` through runtime binding,
   system registration, cell materialization and component attach/post-attach.
   Identify the exact last safe point for contract injection and the first point
   at which a renderable can observe it.
3. End-to-end durable phase flow: material cook record -> VMAT decode -> material
   residency -> mesh technique resolution -> geometry shell/bin -> frame phase.
   Prove compact IDs are never persisted or guessed.
4. Static-surface ABI end to end: VMESH formats/streams, generated shader inputs,
   `VG_DRAW` instance fields, push constants, descriptor spaces/domains, material
   parameter/resource interface, pipeline interface fingerprints and native
   pipeline creation. Classify what already validates and what is missing.
5. Attachment/depth flow: graph declarations and actual targets, camera/view
   reverse-depth policy, clear values, depth comparisons/writes, GBuffer formats,
   exact versus deferred pipeline signatures and output resolve. Name one owner
   for each fact and all consumers that currently duplicate it.
6. Fixture correctness: material techniques, shader/pipeline dependencies,
   vertex/index data, winding/culling/two-sided state, local bounds, transforms,
   depth convention and package closure. Distinguish reusable renderer recipes
   from the temporary fixture frontend.
7. Current failing evidence relevant to this phase, including the camera-surface
   depth-state failure and mesh-layout test compilation drift. Do not absorb the
   unrelated material-scene teardown, package checksum or paged-addressing failures
   unless tracing proves a direct 10.4.3 cause.
8. RED's corresponding phase/technique, vertex-layout, depth and world/render
   activation boundaries, including where frame setup, view work, global scene
   work, graph construction, command recording, queue submission and presentation
   occur in the frame loop. Record exact source evidence and differences; do not
   infer parity from names. A contract without this RED trace cannot pass P0.

P0 deliverables:

- A findings section appended to this document, or a dedicated study linked here,
  with exact current-tree file/symbol evidence and blocker/high/medium findings.
- A final P1-P6 contract refined from that evidence, naming exact owners and data
  flow. Any unresolved ownership or format question remains a blocker.
- A validation matrix separating source review, project generation, compilation,
  linkage, executable tests, native-RHI execution and image/runtime proof.
- A fresh independent read-only current-tree review. P0 closes only with no
  unresolved blocker/high/medium contradiction in the written contract.

P0 must not edit production source, formats, cookers or tests; generate projects;
build; run executables; or relabel existing failures as fixed. Documentation and
read-only source/RED inspection are the whole gate.

### P1 — selected renderer draw-contract ownership

**Closed after correction.** Authorized by the user on 2026-09-16, P1 was initially
implemented and reviewed, then reopened on 2026-09-17 after a CURRENT-tree duplicate-
authority audit found the contract was broader than the existing architecture required.
The correction reduced the implementation without starting P2, passed focused build/
runtime validation, and finished with independent 0 blocker / 0 high / 0 medium review.

- `source/rendering/include/vanguard/rendering/renderer_draw_contract.hpp` now owns
  only the standard portable mesh-draw attachment contexts required by world
  composition: DepthPrepass = D32/sample1/no colors and Opaque = R8G8B8A8 UNorm +
  two R16G16B16A16 Float colors + D32/sample1. Durable phase identity remains in
  `standardRenderPhases` / the sealed registry.
- Static-surface ABI authority remains where it already existed: binding/instance/
  draw structs in `mesh_draw_layout`, compiled register/space/stage/interface facts
  in shader reflection/fingerprints, and global Resources/Samplers in RenderingService.
  No second ABI identity/version bag is published.
- `RenderingService::GetMeshDrawPhases()` publishes the standard phase-context span
  only for an enabled available renderer in the closed P1 implementation. P1.5 later
  removes that public world-facing publication after moving the contexts into their
  actual consumer owner. The canonical b0/space0 24-byte
  Vertex|Pixel layout is installed through the existing material binding-layout
  owner when no caller override is supplied; there is no separate public native
  static-surface interface API.
- Reverse depth is not a renderer-global contract field. It remains per camera/view;
  graph clear and shell compatibility continue to derive/check that existing state.
- Publication occurs only after renderer resource/catalog setup, RHI/domains, frame
  resources, GPU Scene, sealed standard phases, lower material owners, material
  pipeline cache and selected renderer `PrepareRendererPipelines` succeed. No
  canonical static-surface material shader or P3 reflection validation is required.
- Disabled rendering exposes no mesh draw phase contexts. Normal shutdown,
  `AbandonRendererOwners` and `RollbackRendererDevice` invalidate draw-interface
  availability and material layouts before descriptor-domain/RHI teardown.
- Focused Debug validation covers disabled semantics, published phase/attachment
  facts, isolated domain-role and visibility rejection/rollback, valid startup/shutdown,
  and a production-path default-layout forwarding proof: with no caller layout override,
  a real streamed VMAT reaches a ready native technique through
  `MaterialResidencyRuntime`, binds the returned pipeline and accepts an exact 24-byte
  `StaticSurfaceDrawContext` push-constant update, then drains and shuts down cleanly.
  The ordinary material
  runtime suite still fails later in the already-classified P4-adjacent
  camera-surface/main-thread/teardown chain and is not counted as a P1 pass.

The earlier settled-tree 0/0/0 review applied to the broader pre-correction design.
The first correction-specific re-review found **0 BLOCKER / 0 HIGH / 2 MEDIUM**: one
missing behavioral proof for default-layout forwarding and one stale closure-status
contradiction. Both were corrected. The strengthened production-path proof binds the
actual returned native pipeline and records the exact 24-byte
`StaticSurfaceDrawContext`; two final CURRENT-tree delta re-reviews report
**0 BLOCKER / 0 HIGH / 0 MEDIUM**. Final status hygiene passed. No P2 work is
authorized by this closure.

### P1.5 — RED-aligned world/renderer ownership and minimum-copy consolidation

**Closed after implementation, post-fix validation and independent review.**
See [RED/Vanguard world-to-renderer ownership alignment](../studies/red-world-renderer-ownership-alignment-study.md).

- Direct RED inspection confirms the world runtime owns/adopts the render scene and
  proxy lifecycle while renderer proxy/material/stage/graph owners resolve draw-stage
  and PSO compatibility. Vanguard keeps that ownership rule while retaining its
  bindless/GPU-driven divergences.
- Topology-enabled `MeshResidencyManager::Initialize` validates and copies the selected
  `MeshDrawPhaseContext` values once into manager-owned renderer-generation storage.
  It never retains the caller span. The geometry-only standalone initialization path
  remains unchanged.
- `RequestDrawable` no longer accepts attachment contexts. Retained drawables no
  longer carry a context copy or compare per-caller policy. `StaticMeshComponent`
  requests the shared drawable without relaying renderer policy.
- `RenderingRuntimeConfig::meshDrawPhases`, the per-world dynamic copy and
  `GetMeshDrawPhases()` are removed. The runtime also drops its `RenderPhaseRegistry*`;
  durable-key resolution stays renderer-side.
- `RenderingService::GetMeshDrawPhases()` is removed. The service resolves a compact
  default camera `RenderPhaseSet` once from the sealed registry and returns that small
  renderer-generation value by value. `RenderingRuntime::BindCommands` stores the
  value; `CameraComponent` does not keep another phase cache.
- Disabled rendering keeps an empty default camera set and unavailable mesh residency.
  No borrowed attachment-policy span or test-only replacement API is introduced.
- The first implementation re-review found one MEDIUM in standalone composition:
  `RenderingRuntime::BindCommands` could accept arbitrary compact phase bits. The
  corrected bind takes the sealed registry only transiently, validates every set bit,
  and retains only the command owner plus the 64-bit phase set. The final CURRENT-tree
  re-review reports **0 BLOCKER / 0 HIGH / 0 MEDIUM**.
- Fresh post-fix focused target builds and the P1/P1.5 executable matrix pass. A normal
  dependency-rebuilding material-test build is presently stopped by unrelated
  `assets/src/derived_data_artifact_source.cpp` incomplete-`ArtifactSetReader::Impl`
  errors in the shared tree; isolated focused target builds against the current built
  dependency set pass with 0 warnings / 0 errors. The ordinary material suite retains
  the same known P4-adjacent failure chain and is not counted as passing.

### P2 — world installation and activation ordering

Refined by P1.5; explicitly authorized on 2026-09-17 and closed after focused validation
and final independent review. The current implementation/proof keeps the existing
`GameWorldService::BeginWorld` order: it binds shared mesh residency, renderer commands,
transient registry validation and the by-value default camera phase set to the one
`RenderingRuntime` before system registration and `GameWorld::Initialize`. The world runs
all active `OnInitialize` and then all `OnSetup` calls before becoming initialized, while
cell materialization/attachment starts only on later frame processing. `RenderingRuntime`
therefore creates its one owned scene or adopts the supplied existing scene before streamed
renderable attachment. No attachment-context projection or per-world phase registry/cache
is installed. Headless/device-disabled rendering remains valid until a component actually
requires unavailable camera/mesh capability.

The end-to-end proof also found and corrected a pre-activation reflection blocker: built-in
static-mesh/light/camera schema field metadata used camelCase names even though stable field
names reject uppercase bytes. Those metadata strings are now snake_case without changing C++
layout or renderer policy ownership. Focused Debug x64 `engineServicesTests.vcxproj` builds
with `/m:1` at 0 warnings / 0 errors; the executable passes both owned-scene and externally
owned existing-scene adoption/teardown checks. The four focused P1/P1.5 material modes remain
exit 0. The first independent P2 audit is 0 blocker / 0 high / 1 medium, with the sole medium
being stale P2 authorization text corrected in this documentation pass. The fresh
post-correction CURRENT-tree re-review is **0 blocker / 0 high / 0 medium**. P2 is closed;
at that closure point P3 remained separately unauthorized, and it was subsequently
authorized and closed without changing the P2 lifecycle contract.

### P3 — static-surface ABI closure

**Closed after explicit 2026-09-17 end-to-end authorization, focused validation and two
independent clean CURRENT-tree reviews.** P3 closes only the proven missing validation
between a generated/cooked static-surface shader and the renderer-owned native draw
interface; it does not start P4 attachment/depth/render-graph work.

- Static-surface classification requires both a standard DepthPrepass/Opaque durable
  phase and `IsStaticSurfaceDrawPipeline` on the VPPL. That helper is factored from the
  existing binding-15 16-byte per-instance `MeshDrawInstance` + exact `VG_DRAW0/1`
  `R32G32UInt` ABI, and `ValidateMeshDrawLayout` reuses it without weakening its other
  safety checks. Generic POSITION-only Opaque remains a legal non-static technique.
- At bounded `MaterialResidencyRuntime::RequestTechnique` admission, classified static
  shaders must reflect an exact b0, space 0, 24-byte (`sizeof(StaticSurfaceDrawContext)`)
  ConstantBuffer visible to Vertex|Fragment before native shader/PSO admission.
  Malformed reflection is rejected as `InvalidMaterial`.
- The native draw layout remains the existing canonical RHI-interned layout installed
  through renderer material-interface ownership. RequestTechnique proves that exact
  `BindingLayoutRef` is present; it does not create a second description/fingerprint/
  registry. Required Resources/Samplers remain the existing RenderingService/RHI-owned
  domains forwarded through `PipelineInterfaceResources`.
- Existing ShaderFile/VPPL/VMAT/material fingerprint closure, material-program layout/
  materialization ownership and global bindless resource model remain unchanged. No
  per-draw ABI validation or P4 policy was added.
- Debug x64 serialized `/m:1` builds of `renderingTests`,
  `materialRuntimeServiceTests` and `geometryAllocatorTests` pass. Four focused service
  modes pass. The P3 proof chains in `geometryAllocatorTests.exe` complete successfully,
  including the reflection mismatch matrix, generic-Opaque preservation and malformed
  real-static admission proof with canonical layout present; the executable later fails
  an unrelated GPU Scene totals expectation at `geometry_allocator_tests.cpp:2952` and
  texture-residency teardown, so the whole suite is not claimed green. The ordinary
  rendering/material executables retain their separately classified unrelated failures.
- Full and P3-scoped `git diff --check` pass with only existing LF-to-CRLF warnings.
  Two fresh independent CURRENT-tree closure reviews report **0 BLOCKER / 0 HIGH /
  0 MEDIUM**.

### P4 — attachment, depth and fixture correction

**Closed after explicit user authorization, focused validation and a clean independent
CURRENT-tree review.** Reuse the existing per-phase
`pipelines::AttachmentSignature` values as the portable attachment/sample facts for the
currently implemented DepthPrepass/Opaque mesh passes. `MeshDrawPhaseContext` is the
selected-renderer support map for mesh phases, while `RenderPhaseRegistry` remains phase
identity/sort authority and the render graph remains physical RT/lifetime/use/order owner.
Do not add a global GBuffer/pass contract, second attachment registry or generic pass
scheduler. A registered material phase participates in mesh topology only when the selected
renderer also supplies its draw context. Correct the fixture pipelines, local mesh bounds,
cell placement bounds and normal BuildSystem recipe invalidation. Keep reverse-depth policy
per camera/view and frame-output format selection with the actual output owner in 10.4.5.

The closed implementation follows that contract: current camera graph nodes consume the
standard signatures while retaining physical RT ownership; mesh topology filters registered
techniques through the manager-owned selected-renderer draw contexts; bootstrap PSOs match
the fixture camera's reverse-depth default; exact local/sphere and transformed placement
bounds are derived; compiler/source/settings v4 use ordinary BuildSystem/VADI invalidation;
and the lower MaterialTools proof stays renderer-independent with concrete signatures
supplied by the engine/native proof. Focused Debug x64 builds pass, P4-specific fixture
artifacts validate in a contained bootstrap run, and the final review is 0/0/0. At P4
closure, full bootstrap package publication was still blocked later by a separately classified
pre-P4 renderer-feature single-push-constant/shared-module mismatch; P4 did not weaken or
redesign that unrelated owner contract. P5 subsequently repaired that mismatch structurally.

### P5 — deterministic regression matrix

Explicitly authorized on 2026-09-17 and now closed after focused validation and a clean
independent CURRENT-tree closure review. Reuse existing exact phase/fingerprint/attachment/
package-parity owner tests and add only missing negatives: missing static b0, explicit
`VG_DRAW0/1` corruption, real-shell forward/reverse depth mismatch, invalid local mesh bounds
and invalid cell placement bounds. The production static proof consumes the standard
DepthPrepass/Opaque attachment helpers (R8/R16F/R16F + D32), while MaterialTools remains
renderer-independent. Changing the renderer-feature compile-family define is proven to alter
ordinary shader build identity through canonical settings.

The P4-era shared GPU Scene shader mismatch is repaired structurally in P5: feature metadata
selects one compile family via the existing define/settings path, the shared shader exposes one
matching push-constant family, and bootstrap forwards that define. Runtime keeps its existing
single-push-constant policy. A fresh contained bootstrap run succeeds with `catalog=Success
entries=17 resources=45`, exit 0 and a reopened VPAK. Focused builds are green; standalone
meshes/world/shader/pipeline/material/assets/MaterialTools suites pass, while the three known
aggregate baselines reproduce only after their P5-relevant proofs. No project wiring changed,
so Premake regeneration was deferred to P6 and has now been completed successfully.

Final P5 review is **0 blocker / 0 high / 0 medium**. A second independent read found one
documentation-only stale P4 status statement; it was corrected to historical wording before
closure and required no production/test change.

### P6 — exact validation and independent closure review

Explicitly authorized on 2026-09-17. Regenerate with Premake, build focused projects with
serialized outer MSBuild, run the accepted matrix, perform hygiene/root-output
checks and obtain an independent CURRENT-tree blocker/high/medium review. Record
compile, link, test, native-RHI and runtime/image claims separately. Updating
10.4.3 to complete does not close 10.4 or Phase 10.

Current P6 evidence is complete for the accepted validation matrix and the fresh post-correction
independent CURRENT-tree closure review is clean at 0 blocker / 0 high / 0 medium:

- `premake5.exe vs2022`: pass / exit 0.
- All 12 focused Debug x64 projects, including `bootstrapImage`, build/link with outer `/m:1`.
- Eight standalone owner suites pass exit 0; all four focused material-runtime startup modes pass;
  fresh contained bootstrap publication/reopen succeeds with 17 entries / 45 resources and a
  259688-byte VPAK.
- Native-RHI execution is positive in the default-layout mode: it reaches the real command-list/
  pipeline path, accepts the canonical 24-byte static-surface push-constant payload, and exits 0
  after the standalone fixture explicitly acknowledges only its own completed generation-
  retirement evidence.
- `textureResidencyServiceTests` passes exit 0 with a fresh-process negative shutdown proof: one
  exact old source generation is deliberately left unacknowledged after real queue/native
  retirement, and the child succeeds only when graceful host shutdown fails specifically with
  `HostFailureCode::QuiesceFailure` from `RenderingService`. The parent accepts only the child's
  exact dedicated success exit, preventing setup/I/O failure or fatal teardown from false-passing.
- `assetStreamingTests` passes with 0 failed checks, preserving the exact project-pipeline
  `(resource,generation)` acceptance/retry contract.
- The first final CURRENT-tree review found **1 blocker / 0 high / 1 medium**. The blocker was a
  production omission in terminal sourceful residency retirement: `TextureResidencyManager::Retire`
  did not attach the exact generation evidence needed to release a project-pipeline
  `residentRetirementPending` candidate. The medium was a subprocess-oracle gap where top-level
  child initialization failures could be recorded without preventing the dedicated success exit.
- CURRENT corrects both findings. Final sourceful retirement now retains exact
  `(resource path,generation,retirement id)` evidence through real queue/native completion and
  explicit owner acknowledgement, and child mode cannot report success after any recorded
  top-level initialization failure. Normal service tests, the negative subprocess, and low-level
  renderer tests all exercise exact terminal-generation evidence; standalone material fixtures
  perform the final seal/native flush before acknowledging only completed evidence they own.
- Full-tree `git diff --check` passes with only existing line-ending warnings, and no generated
  executable/library/package/log was emitted at repository root.

The earlier P6 blocker is resolved without weakening production semantics. Sourceful retirement
evidence remains non-destructive through `PeekCompletedGenerationRetirements` and requires
explicit acknowledgement. In standalone fixtures with no project asset pipeline, the fixture may
acknowledge evidence that it itself owns after completion; where a real `ProjectAssetPipeline`
exists, exact downstream acceptance remains `ReportResidentRetirement(...)` before renderer
acknowledgement. `RenderingService::OnQuiesce` and final retirement checks continue to fail closed
on undelivered evidence. No engine/rendering -> assetStreaming dependency or renderer auto-ack was
added. `geometryAllocatorTests`, ordinary `materialRuntimeServiceTests` and `renderingTests` retain
their previously classified aggregate baselines and are not claimed green.

P6 is **closed**. The fresh post-correction independent CURRENT-tree review reports
**0 blocker / 0 high / 0 medium**, and the accepted validation/hygiene matrix is complete.
Therefore 10.4.3 is complete. This does not authorize or start 10.4.4+.

## Definition-of-done boundary

10.4.3 is complete only when the selected renderer contract reaches a world before
renderable attachment, cooked phase keys resolve through the sealed registry, the
static-surface ABI and pipeline/attachment/depth state are validated coherently,
and the corrected package and loose artifacts pass the accepted deterministic
matrix. It does not claim an active camera, viewport output, pixels, shutdown
draining or performance.

## P0 evidence and review ledger

P0 was explicitly authorized on 2026-09-16 for read-only CURRENT Vanguard/RED
architecture tracing and planning/documentation updates only. No production source,
formats, cookers or tests were edited; no project generation, build, executable,
native-RHI run, application launch, pixel/image proof or performance proof was run.

Accepted owner/data-flow contract, refined by the P1 duplicate-authority correction
and P1.5 ownership consolidation:

- `RenderingService` is the selected-renderer composition owner, not a second authority
  for phase keys, static-surface ABI, descriptor domains or depth direction. Structured
  mesh-draw compatibility is copied once into the actual renderer consumer,
  `MeshResidencyManager`; it is no longer published as a world-facing policy table.
  Native shaders, layouts, pipelines and descriptor handles remain in their existing
  owners and follow normal shutdown, abandon and rollback lifetime paths.
- `GameWorldService` binds existing shared renderer owners before streamed/renderable
  components can observe world rendering state. Camera defaults cross that boundary
  only as a small renderer-generation `RenderPhaseSet` by value; no world retains an
  attachment-policy span or `RenderPhaseRegistry*`.
- Cooked/runtime boundaries carry durable `RenderPhaseKey`; compact
  `RenderPhaseId` remains local to the sealed runtime registry and is never persisted
  or guessed.
- P1/startup installs the canonical b0/space0/24-byte Vertex|Pixel native layout
  through the existing material binding-layout owner when no caller override is
  supplied; existing Resources/Samplers domains remain authoritative. P1 publishes no
  parallel ABI identity/fact bag. Because the renderer catalog has no canonical
  static-surface material shader, P3 validates each compiled material shader's
  register/space/byte-size/stages against the existing static-surface ABI authorities
  at bounded `MaterialResidencyRuntime::RequestTechnique` admission before readiness.
  Required native descriptor-domain presence/type is validated separately from reflection.
- Static-surface material/resource access remains GPU-driven and bindless through
  material IDs, GPU Scene stable identities and global descriptor domains. No
  per-material CPU binding packets or material ownership are added to the render-flow
  allocator.
- The portable world-facing phase contexts supply DepthPrepass/Opaque attachment
  signatures and sample count only. Graph nodes keep physical graph-resource ownership;
  depth direction remains per camera/view; output/swapchain selection remains 10.4.5.
- Fixture correction is bounded to authoritative reverse-depth state, derived local
  mesh AABB/sphere, transformed cell placement bounds and normal BuildSystem
  invalidation of shared renderer contract/template inputs. `bootstrapImage` remains
  development-content frontend rather than packaged-game architecture.
- RED evidence establishes frame-global versus per-view preparation, graph build,
  command recording, synchronization/submission and presentation ordering. Vanguard
  keeps its stronger durable-phase and GPU-driven bindless model and does not import
  RED's mutable append-order stage identity, fixed-slot material binding or mutable
  camera-storage/flow-group ownership.

Implementation findings carried forward as later-gate work originally included three
HIGHs. P1/P1.5/P2 closed the world phase-contract/ownership path, P3 closed the
per-compiled-material-shader static-surface reflection seam at bounded technique admission,
and P4 has now closed the reverse-depth fixture mismatch plus attachment/bounds/invalidation
work without creating a second pass, attachment, ABI, cache or dependency authority. The
prior mesh-layout test compilation drift is no longer present. At P4 closure a separate
pre-P4 renderer-feature single-push-constant/shared-module defect still blocked full bootstrap
package publication; P5 subsequently repaired that defect structurally through canonical
shader build settings without reclassifying it as P4 work.

Independent review history: the first fresh CURRENT-tree review found 0 BLOCKER /
1 HIGH / 2 MEDIUM contradictions in the written P0 contract. They were corrected in
documentation only: reflection validation moved from renderer startup to bounded
material-technique admission, native descriptor-domain validation was separated from
reflection metadata, and P4/P5 were expanded to include cell placement bounds. The
post-correction re-review reported **0 BLOCKER / 0 HIGH / 0 MEDIUM** contradictions.
A second independent CURRENT-tree cross-check also reported a clean 0/0/0 contract.

P1 was authorized by the user on 2026-09-16 and the later duplicate-authority
correction is closed with fresh focused build/runtime validation and final independent
0 blocker / 0 high / 0 medium review. P2 was explicitly authorized on 2026-09-17 and is
closed with focused validation and a final independent 0 blocker / 0 high / 0 medium
post-correction re-review. P3 was subsequently authorized end to end on 2026-09-17 and
is closed with focused ABI validation and two independent 0 blocker / 0 high / 0 medium
CURRENT-tree reviews. P4 was subsequently authorized under the corrected many-pass model
and is closed with focused validation and a final independent 0 blocker / 0 high / 0 medium
CURRENT-tree review. P5 was subsequently authorized on 2026-09-17 and is closed with focused
validation plus a final independent 0 blocker / 0 high / 0 medium CURRENT-tree review. P6 was
subsequently authorized on 2026-09-17; its affected validation is complete and the earlier
retirement-handoff test fallout is resolved without weakening production lossless semantics.
P6 closed after a fresh post-correction independent CURRENT-tree review at 0 blocker / 0 high /
0 medium. 10.4.3 is complete; 10.4.4+ is not started.
