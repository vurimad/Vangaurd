# E2.4 - committed publication, stale status and resident replacement

## Status and authorization

Defined for cross-agent implementation. E2.3 was independently reviewed and
accepted: fresh Premake generation and Debug/Development builds and executions
of assetGraphTests, assetRecookerTests, assetStreamingTests and materialToolsTests
passed. The earlier agent ledger records a transient material-preview lifecycle
failure; later runs passed, but that transient is not claimed fixed.

E2.4 P0 architecture proof was explicitly authorized and closed on 2026-09-16
after an independent current-tree read-only review found no blocker/high/medium
contradiction in its written execution contract. P1 implementation and focused
Debug/Development validation subsequently closed with final independent verdict **0
blocker / 0 high / 0 medium**. The user then explicitly authorized P2 and later P3. P2
exact fenced publication/retry implementation is complete and closed after fresh
exact-tree Debug/Development validation and a final independent CURRENT-tree re-review
reported **0 blocker / 0 high / 0 medium**. P3 supported resident replacement and
retirement is also complete and closed after fresh focused Debug/Development validation
and a final independent CURRENT-tree review at **0 blocker / 0 high / 0 medium**. The user
then explicitly authorized P4. Removal/restart/bounds/teardown is complete and closed after
the final post-fix exact-tree validation, hygiene, and independent CURRENT-tree review at
**0 blocker / 0 high / 0 medium**.
The user subsequently authorized P5 through P6 end-to-end. P5 is complete and its final
independent CURRENT-tree review reported **0 blocker / 0 high / 0 medium**. P6 is complete and
E2.4 is closed: complete Debug/Development generation/build/execution/hygiene validation passed
and the final independent CURRENT-tree E2.4 closure review reported **0 blocker / 0 high / 0
medium**. Stop at E2.4; E2.5 cache maintenance and E2.6 end-to-end closure remain separate work
and require separate authorization.

## Read-first handoff context

Read root `AGENTS.md`, [document map](../README.md),
[E2 contract](project-asset-e2-plan.md),
[foundation progress](../progress/project-asset-foundation-progress.md),
[editor roadmap](editor-foundation-study-and-roadmap.md) and
[E2.3 contract](project-asset-e2-3-plan.md).
Read `docs/architecture/incremental-recooking.md`, and the relevant VADI, VDDC,
VPAS, VMETA and VTXN specifications under `docs/formats/`.
The E2.3 planning triplet under
`.planning/e2-3-incremental-recook-coordination/` is historical implementation
evidence, not permission to restart or expand that task.

E2.2's accepted asset baseline is comparison evidence, not successful build or
publication authority. VPAS does not replay transient work after restart.
E2.3 coordinates opt-in automatic work, canonical importer request resolution,
one transactional recook batch, bounded pending/recovery state and pre-commit
freshness rejection. Automatic success currently ends at committed VADI/build
state; it deliberately does not publish loose output or replace residents.

## Existing owners and source entry points

- `source/assetStreaming/include/vanguard/asset_streaming/project_asset_pipeline.hpp`
  and `src/project_asset_pipeline.cpp`: shared owner surface, automatic worker
  completion, manual request publication, observation reconciliation/removal,
  publication bookkeeping and source-mutation guards.
- `source/assetStreaming/private/vanguard/asset_streaming/project_asset_recook_state.hpp`
  and implementation: pending/active evidence, recovery and lifetime ownership.
- `source/assetStreaming/include/vanguard/asset_streaming/project_loose_publisher.hpp`
  and implementation: committed VADI lookup, generation-addressed materialization,
  runtime dependency descriptors and ResourceStreamer::PublishLoose.
- `source/assets/include/vanguard/assets/loose_resource_materializer.hpp` and
  implementation: validated immutable loose output installation.
- `source/streaming/include/vanguard/streaming/streaming.hpp` and implementation:
  winning loose/package resolution, PublishLoose, UnregisterLoose, OpenSource
  and independently owned range sources.
- `source/resources/include/vanguard/resources/resources.hpp` and implementation:
  registry/handle generations, resource pipeline requests and lifecycle state.
  Trace actual reload support; an enum named Reloading is not implementation proof.
- Current resident owners and their tests: discover the actual texture/material/
  geometry service entry points from current source. Do not guess module names
  or substitute a private loader when the consumer lacks a replacement seam.

Reuse these owners. No second registry, publication database, runtime loader,
dependency graph, cache, generic hot-reload framework or GPU retirement system.
Keep authored references, build dependencies and runtime dependencies separate.

## Required behavior and correctness boundaries

1. Distinguish authored/accepted state, committed build fingerprint, published
   descriptor generation and resident generation. A successful cook is not a
   successful publication; a new descriptor is not a replaced resident. Expose
   truthful bounded per-output status/reasons through ProjectAssetPipeline.
2. Capture a complete owned completion/publication handoff before E2.3 resets
   active evidence or releases its batch. Include affected generated outputs,
   not just submitted root identities. Use committed VADI as authority; never
   inspect transaction-staged records as publishable output.
3. Fence the interval AFTER successful recook commit and BEFORE descriptor
   publication. New authored changes, partial/unreadable sidecars, changed
   settings/importers/dependencies or a newer committed result must prevent an
   obsolete delivery from being labeled current. E2.3's pre-commit gate remains
   necessary but is not a substitute for this delivery fence. Document the
   residual external-write race honestly; no atomic disk-wide claim without a
   mechanism. A snapshot-only watcher-generation check is insufficient: serialize
   the final change-generation/overflow validation with the short descriptor-group
   visibility commit (or use an equivalently strong accumulator-owned barrier) so
   a tracked callback cannot land between the final check and publication. Keep
   project-sized resolution/materialization outside that barrier and define lock
   ordering with the streamer explicitly. A later rejection does not roll back an
   already committed VADI.
4. Serialize materialization/descriptor installation with existing graph,
   reconciliation and manual-operation ownership. Carry owned identity and
   fingerprint tokens across worker boundaries; do not borrow catalog spans.
   Verify that the committed record selected by the publisher is the intended
   generation, not whichever record happens to exist when a delayed job runs.
5. Materialize using ProjectLoosePublisher/LooseResourceMaterializer and install
   through ResourceStreamer. Validate target, artifact integrity and runtime
   dependency table. Never overwrite the old generation's file in place or
   route automatic work through an uncontrolled manual convenience wrapper.
6. Preserve last-valid delivery on build failure/cancel/stale, index failure,
   materialization failure, descriptor installation failure and resident reload
   failure. Report each separately. Publication retry must not rerun the compiler
   when the committed artifact is still valid; explicit retry/newer-change policy
   must avoid per-tick failure loops.
7. Preflight bookkeeping capacity before visible descriptor installation.
   If installation succeeds but later reconciliation fails, report a committed
   delivery with reconciliation pending, not a rollback that never occurred.
   Define recovery/idempotence for every visibility boundary.
8. Multi-output delivery needs an explicit policy. Do not claim project-wide
   atomic publication from repeated single-output PublishLoose calls. In P0
   determine which dependency-related outputs require atomic grouping to avoid
   incompatible generations; add only a narrow batch capability to the existing
   streamer owner if proven necessary. Otherwise expose partial progress/failure
   honestly and prove supported consumers remain safe.
9. PublishLoose already preserves requests that opened the previous source;
   later source resolution sees the replacement descriptor. This is file-reader
   safety, NOT proof of decoded/resident replacement. Trace registry request
   deduplication, strong/weak handles, resident bindings and actual reload path.
   Replace through the owning resource/service boundary. Keep old readers and
   resident objects alive until those owners release them. GPU consumers retire
   through their existing real fence/queue ownership, never a guessed frame delay.
10. P0 must name the supported resident consumer(s), replacement API and exact
    acceptance tests. Prove at least one real production consumer path through
    ResourceStreamer, not a fake decoder alone. Do not claim all texture/material/
    geometry consumers hot-reload from one proof. If a required path is absent,
    identify the narrow owner extension and unresolved scope before implementation;
    do not silently downgrade E2.4 to descriptor replacement only.
11. Deletion/removal must define future-resolution policy, package fallback and
    retained-reader behavior separately. Reconciliation currently removes/prunes
    loose publications: trace and correct any path that destroys last-valid data
    still owned by delivery/readers/residents. Preserve authored referencers and
    explicit missing diagnostics; never guess replacement identities.
12. Bound pending delivery, status, resident operations and captured evidence.
    Keep project-sized materialization, I/O and reclamation off normal owner ticks
    using existing Jobs/worker seams. Overflow requests explicit recovery rather
    than losing successful outputs. Shutdown stops producers, fences callbacks,
    cancels/drains delivery and resident work, then closes owners in proven order.
    E2.4 may protect live generations; broad cache pruning belongs to E2.5.
13. Restart reconstructs publication from validated committed build state and
    current source truth using existing initialization/reconciliation. Do not add
    VPAS transient replay or another durable publication journal by default.

## Ordered implementation gates

### P0 - architecture proof and scoped execution plan

On implementation authorization create
`.planning/e2-4-committed-publication-replacement/{task_plan,findings,progress}.md`.
Trace completion/reset, committed fingerprint capture, publication commit,
registry reload and supported resident retirement paths. Record the post-build
freshness fence, grouping policy, reader/file retention, package fallback,
failure/retry and bounded-worker ownership decisions. Resolve architecture gaps
before source edits; report blockers rather than weaken requirements.

### P1 - owned delivery handoff and shared status

Add bounded completion evidence and per-output authored/build/published/resident
status to the existing pipeline owner. Preserve E2.3 transaction semantics and
manual request behavior. Do not report runtime current from build success alone.

### P2 - fenced committed publication and retry

Coordinate generation-addressed materialization, committed-record/freshness
checks, capacity preflight and streamer installation. Preserve last-valid output
across every failure class; implement idempotent retry without unnecessary cook.
Implement the P0 grouping policy without inventing an external registry.

### P3 - supported resident replacement and retirement

Use the proven production consumer path, loading through ResourceStreamer and
swapping only at its owner boundary. Prove old handles/readers/bindings remain
safe and failed replacement keeps last-valid resident state. Exercise real
fence-backed retirement for any GPU path claimed complete.

**Closed 2026-09-16.** `ResourceRegistry`/`ResourcePipeline` now support distinct
candidate generations with old-handle retention, canonical resident-only replacement
ownership and exact-generation eviction. `ResourceStreamer` forwards replacement. VTEX
uses a fresh candidate path on the same stable GPU residency identity (not cross-
generation `BeginTransition`); failed replacement preserves the old BindlessReady
resident and explicit retry reuses the retained uploaded candidate. CPU resident truth
advances only after manager acceptance, and old GPU state retires through existing real
graphics/compute/copy fences. A neutral pipeline resident-evidence seam reports exact
published-vs-resident generation/fingerprint status. Fresh focused Debug/Development
validation and final independent CURRENT-tree review are clean at **0 blocker / 0 high /
0 medium**. P3 does not claim bounded replacement history/churn; that remains P4.

### P4 - removal, restart, bounds and teardown

Integrate observation/manual removal without erasing references or prematurely
pruning live files. Cover package fallback, bounded delivery storms/overflow,
sticky errors, restart reconstruction and live-work shutdown/reinitialize.

**Final closure validation 2026-09-16.** Project removal now uses atomic batch loose unregister while
opened loose/package sources keep their exact pinned generations; superseded physical
loose files are conservatively retained instead of being deleted without owner proof.
Broad stale-cache pruning remains E2.5. Restart reconstructs from accepted/current source
truth plus current committed VADI state and does not replay transient automatic work or
add a publication journal. `ResourcePipeline` retained operations, `ResourceRegistry`
generation slots, output status/resident evidence, and failed VTEX replacement-candidate
GPU bytes are explicitly bounded with deterministic recovery. Retained VTEX candidates
preserve P3 no-reupload retry; overflow keeps the old resident authoritative. Streamer
source sessions, automatic-publication shutdown settlement, texture demand IDs and public
uploader request IDs now have teardown/reinitialize ownership that prevents live-source
shutdown and stale-session ABA. The final docs-inclusive audit found two closure mismatches:
ordinary manual/observed removal still unregistered one output at a time, and one durable
progress paragraph still called P3 unstarted. Ordinary reconciliation now gathers the full
removal set and commits one `UnregisterLooseBatch`; a busy-member regression proves failed
admission leaves every earlier output visible. Post-documentation repetition also exposed
that a terminal `ResourcePipeline` operation could be reclaimed before its captured plan
worker returned; per-operation scheduled-job ownership now closes that lifetime. Fresh
post-fix Premake generation and the exact-tree serialized Debug/Development build and
executable matrix are green. Final hygiene is clean and the independent CURRENT-tree review
reported **0 blocker / 0 high / 0 medium**. **Closed 2026-09-16.**

### P5 - deterministic regression matrix

Cover automatic first publication; transitive/multi-output delivery; unchanged
suppression; successful stale-to-current replacement; source/settings/importer/
dependency changes after cook but before publication; a newer committed result
overtaking delayed delivery; build failure/cancel/stale and index-save failure;
artifact missing/corrupt, materialization and streamer install failure; capacity
failure before visibility; publication retry without compiler execution; failed
resident reload; an opened old range source, strong/weak handles and in-flight
resident use across replacement; removal with active readers/package fallback;
explicit missing referencers; bounded bursts/overflow; restart; teardown with
work/callbacks active. Assert exact published/committed/resident fingerprints and
last-valid retention, not merely operation success. Use deterministic job gates
for races. Fake-decoder tests supplement, not replace, production consumer proof.

**Closed 2026-09-16.** Deterministic coverage now includes exact unchanged installed-state
suppression; source-byte/settings/importer/dependency drift after commit but before publication;
missing and corrupt exact committed VDDC artifacts; three-node non-soft Generated publication
atomicity/conflict/retry; configured capacity before visibility; and an already-open old streaming
source/range queue across descriptor replacement. Failure rows assert exact committed versus
published versus resident fingerprints, preserve last-valid state, stay sticky without compiler
spin, and explicitly retry the same committed token without recompilation. Existing resource and
VTEX tests supply real strong/weak-generation and production resident/fence-backed proof. Focused
Debug/Development `assetStreamingTests` and `streamingTests` builds and executions pass. The final
independent CURRENT-tree P5 review reported **0 blocker / 0 high / 0 medium**.

### P6 - validation, durable closure and independent review

Generate with Premake VS2022. Build focused affected projects using MSBuild /m:1
and run Debug/Development assetStreamingTests plus affected assets, streaming,
resources and supported resident-consumer tests. Record exact commands and
generation/compile/link/test results separately. GPU/fence claims require actual
execution with prerequisites recorded; unavailable runtime proof stays open.
Check diff hygiene, header placement, test residue and E2.5/E2.6 leakage.
Update E2/progress records with outcomes and limits, then request independent
review. Do not self-label independent acceptance. Stop at E2.4.

**Closed 2026-09-16.** Fresh Premake VS2022 generation
passed the Vanguard engine-service audit. Serialized outer MSBuild `/m:1`, x64, passed in Debug and
Development for `assetRecookerTests`, `resourcesTests`, `resourcePipelineTests`, `streamingTests`,
`rendering`, `geometryAllocatorTests`, `textureResidencyServiceTests`, and `assetStreamingTests`.
All seven executable suites passed in both configurations; `rendering` is the library-only build
target. The executed geometry/runtime coverage includes the D3D12 texture-residency/material
replacement and real GPU-fence-backed path, and the texture residency service's source/DDC/loose/
package/GPU integration passes. Two stale test-only assumptions were corrected during validation:
the mesh draw-layout test now includes the concrete declarations it uses directly, and the retained-
import smoke test expects the current `QueueOrCommandScopeMismatch` failure for an invalid explicit
fence-wait descriptor. No production correction was required. Diff/audit/header/residue/process/
dependency/scope hygiene is clean, with no E2.5 or E2.6 implementation leakage. The final
independent CURRENT-tree P5/P6 closure review reported **0 blocker / 0 high / 0 medium** after
rechecking the deterministic matrix, the separate generation/build-link/execution/GPU-fence
evidence, both rendering test-only contract fixes, scope boundaries and all six durable records.
P6 and E2.4 are closed.

## Pasteable agent instruction

Implement only E2.4 under this contract after explicit authorization. Start with
P0 and maintain the scoped planning triplet. Reuse existing publication, streamer,
registry and resident owners. Do not equate descriptor publication with resident
reload, ignore the post-cook freshness interval, or add E2.5 cache maintenance.
Preserve unrelated work. If builds/runtime execution are not authorized or
available, record source-only progress and leave the executable gates open.
