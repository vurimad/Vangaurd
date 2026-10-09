# E2.5 - local cache lifecycle and safe generation reclamation

## Status and authorization

Defined for cross-agent implementation. E2.4 is closed and E2.5 P0-P3 are
closed. The user subsequently authorized P4 and directed `close p4`. A later
audit reopened P4 after finding the missing production renderer-to-project
composition handoff. The editor composition owner, renderer lifecycle window and
focused production-seam regression now close that gap. Fresh Debug/Development
validation and exact-CURRENT self-review report **0 BLOCKER / 0 HIGH / 0 MEDIUM**.
P4 is closed. P5 is the next E2.5 gate and P6 follows it. E2.6 remains separate
and unauthorized.

## Purpose

Complete two related but distinct project-local storage lifecycles:

1. immutable content-addressed VDDC build records; and
2. generation-addressed materialized loose `.vres` files produced from those
   records for `ResourceStreamer`.

A build must be able to restart and reuse an exact VDDC record without running
its compiler, recover safely when the record named by current VADI state is
absent or invalid, and enforce a configured VDDC disk budget. Superseded loose
generations deliberately retained by E2.4 must become reclaimable only after
streaming readers, resource generations and resident/GPU owners prove retirement.
The two domains use separate identities, protection evidence, limits and status;
neither may infer the other's deletability.

This is editor/headless tooling infrastructure. `ResourceStreamer` participates
only by exposing the lifetime of loose generations it already owns. A packaged
shipped runtime continues to resolve packages/explicit development loose input
without requiring VADI, VDDC or the E2.5 maintenance coordinator.

## Read-first handoff context

Read root `AGENTS.md`, [document map](../README.md),
[E2 contract](project-asset-e2-plan.md),
[E2.4 contract](project-asset-e2-4-plan.md),
[foundation progress](../progress/project-asset-foundation-progress.md), and the
[editor roadmap](editor-foundation-study-and-roadmap.md). Read
`docs/architecture/incremental-recooking.md` and the VADI, VDDC, VPAS, VMETA and
VTXN specifications under `docs/formats/`.

The completed E2.4 planning triplet under
`.planning/e2-4-committed-publication-replacement/` is historical evidence, not
permission to extend E2.4. On E2.5 P0 authorization, create
`.planning/e2-5-local-cache-lifecycle/{task_plan,findings,progress}.md`; keep the
task plan as the active execution contract, findings as source evidence, and
progress as the exact command/result ledger.

## Existing owners and source entry points

- `source/assets/include/vanguard/assets/assets.hpp` and `src/assets.cpp`:
  `BuildSystem`, build fingerprints, memory-cache policy, VDDC lookup/store,
  hit/miss/corruption statistics and compiler execution.
- `source/assets/include/vanguard/assets/derived_data_artifact_source.hpp` and
  implementation: validated VDDC open/read and the lifetime of
  `ArtifactSetReader`. Current source permits multiple independently initialized
  artifact-source instances against one physical cache root.
- `source/assets/include/vanguard/assets/asset_index.hpp` and
  `src/asset_index.cpp`: committed VADI records, transactions, dependency
  evaluation and persistence.
- `source/assets/include/vanguard/assets/asset_graph.hpp`, recooker sources and
  tests: active/staged build ownership, retained build output and transactional
  index publication.
- `source/assetStreaming/include/vanguard/asset_streaming/project_asset_pipeline.hpp`
  and implementation: shared editor/headless command surface, E2.3 automatic
  coordination, and E2.4 committed/published/resident status and retry evidence.
- `source/assetStreaming/src/project_loose_publisher.cpp` and
  `source/assets/src/loose_resource_materializer.cpp`: VDDC readers used to make
  generation-addressed loose output, its canonical physical filename and owned
  staging path.
- `source/streaming`: open loose/package readers and descriptor generations.
  These are loose-delivery lifetimes, not a second DDC authority.
- `source/filesystem`: bounded enumeration, file metadata and deletion
  primitives. Reuse them; do not add native filesystem calls to asset owners.

`BuildSystem`/a common `DerivedDataArtifactSource` cache-root authority retain
VDDC maintenance authority and `DependencyIndex` retains VADI authority.
`ProjectAssetPipeline` may request VDDC maintenance and expose its status, but
must not mirror the cache index, choose VDDC victims from private duplicate
metadata, or delete VDDC files itself.

Loose-generation reclamation remains an `assetStreaming` coordination concern:
`ProjectLoosePublisher` owns the materialized path contract,
`ResourceStreamer` owns open source/load lifetimes, resource/resident services
own their generations, and `ProjectAssetPipeline` already owns project delivery
bookkeeping. E2.5 may add only the narrow retirement/protection handoff required
between those owners. It must not move runtime loading or residency ownership
into the pipeline.

`AssetFileTransaction` remains the authored-source transaction owner and must
not be reused as a journal for either disposable storage domain.

## Current-tree facts that constrain the design

1. VDDC records are immutable and content-addressed by exact build fingerprint
   under a two-byte fan-out. Store already stages, flushes, validates and moves
   a complete record.
2. `BuildSystem::Execute` checks memory, then VDDC, then invokes the compiler.
   `BuildDisposition::CacheHit` does not currently distinguish memory from
   persistent hits, although aggregate persistent counters exist.
3. Invalid VDDC is rejected and corrupt/content-mismatched data is currently
   deleted directly by `BuildSystem` lookup. That bypass is not coordinated with
   other artifact readers, and lazy lookup alone is insufficient when VADI says
   an output is up to date and therefore no build reaches the lookup path.
4. VADI stores the exact build/content fingerprints and artifacts for current
   committed outputs, but `DependencyIndex` has no general bounded committed-
   record enumeration API for a cache owner to construct reachability.
5. `BuildSystem::ClearCache()` clears only the memory cache. Preserve that
   meaning; persistent maintenance needs an explicitly named operation.
6. `BuildSystem` and `ProjectLoosePublisher` currently initialize distinct
   `DerivedDataArtifactSource` instances against the same configured VDDC root.
   An open `ArtifactSetReader` from either instance owns a live file reader.
   Instance-local deletion admission is therefore insufficient.
7. VDDC startup recovery currently recursively deletes every `*.tmp` below its
   root, while record stores use a generic generated temporary name. The current
   scan does not prove that each match is an abandoned VDDC publication.
8. E2.4 separately tracks committed delivery, descriptor-visible and resident-
   current generations, explicit retries and retiring old generations. Cache
   maintenance must consume owner-provided protection evidence; it must not
   infer those states from filenames.
9. A loose output is named from resource identity plus build fingerprint and is
   materialized as an immutable `.vres` generation. E2.4 overwrites current
   publication bookkeeping when a newer generation wins and deliberately
   defers deletion of older scanned files because no owner-proven generation
   retirement query exists yet.

## RED architectural evidence and Vanguard deviations

The implementation agent must refresh and record exact RED source evidence in
P0 before editing production source. The starting evidence is:

- `backend/backendData/include/dataCache.h` and `src/dataCache.cpp` keep cooked
  cache access behind one cache interface and separate exact prerequisite-hash
  lookup from latest-by-path lookup and resource retrieval.
- `backend/backendData/include/dependencyCache.h` and
  `src/dependencyCache.cpp` retain dependency facts behind their cache owner,
  refresh stale file hashes, and persist bounded cache data rather than asking
  frontend code to own it.
- `backend/backendDataBuild/src/cleanServerCacheCommandlet.cpp` treats cleanup
  as explicit maintenance, batches deletion, and distinguishes dependency-
  database rows from stored files. Its policies are generator/server-specific;
  its orphan-cleaning function is unfinished and is not proof of a safe generic
  collector.

Vanguard intentionally differs: E2.5 is a local immutable SHA-256 VDDC plus
committed VADI design. Do not copy RED's PostgreSQL, cache-server protocols,
GET_LATEST generator policy, formats or commandlet ownership. Reuse RED's
separation of cache access, dependency authority and explicit maintenance while
keeping Vanguard's existing owners and portable project roots.

## Required behavior and correctness boundaries

1. **Exact restart hits.** A cold process with a valid exact VDDC record must
   return the same build/content/artifact evidence without invoking the
   compiler. Report memory hit, persistent hit and compiler build distinctly;
   aggregate counters alone are not sufficient per-operation evidence.
2. **Current-record repair through the existing scheduler.** Before VADI-backed
   restoration/publication treats a committed record as usable, validate its
   exact VDDC key and content. A missing, corrupt, truncated or mismatched record
   makes that output repair-required. `ProjectAssetPipeline` must translate this
   into the existing E2.3 automatic-recook admission/coalescing machinery and
   canonical importer request resolution; E2.5 adds no repair queue, scheduler
   or direct compiler path. Repair respects manual graph work, observation,
   source-mutation and maintenance exclusion. A cache miss recompiles and only a
   successful existing VADI transaction replaces or reaffirms committed evidence.
   Do not delete or rewrite the last committed VADI record before repair
   succeeds, and preserve E2.4's last-valid publication/resident on failure,
   cancellation or staleness.
3. **Root-scoped VDDC authority.** Every writer and every
   `DerivedDataArtifactSource`/`ArtifactSetReader` opened against the same
   canonical cache root participates in one shared admission and lease domain.
   A private lock or reader count inside one source instance is insufficient.
   P0 must define root identity, compatible registration, initialization and
   final shutdown without introducing a frontend-owned cache or an unbounded
   process-global registry.
4. **Exact VDDC protection.** A build fingerprint is protected when reachable
   from committed VADI, a live/staged build or index transaction, retained
   recook/delivery retry evidence, or an open VDDC reader. Owners contribute
   exact keys through the smallest bounded query/lease interfaces. Absence from
   one owner is never sufficient proof of unreachability. Preserve dependency
   direction: `ProjectAssetPipeline` may pass bounded immutable project/delivery
   protections into an `assets` maintenance request, but `assets` must not
   depend on or callback into `assetStreaming`.
5. **One VDDC deletion path.** Budget victims, corrupt/content-mismatched lookup
   cleanup and abandoned-temporary recovery all pass through the root-scoped
   authority. The final check plus deletion must prevent a store, reader open or
   newly protected commit from racing the operation. A stale copied list or a
   Windows sharing violation is not a safety mechanism. Delete refusal/I/O
   failure is deferred and reported, not reclassified as corruption or retried
   in a tight owner-tick loop.
6. **Owned temporary proof.** Replace broad `*.tmp` recovery with an exact VDDC
   staging-name/layout contract or equally strong ownership proof. Startup may
   reclaim only a matching incomplete VDDC stage, only when the root-scoped
   authority proves no live producer/reader owns it. Apply the equivalent strict
   rule to canonical `.vres.tmp` materialization stages under the loose root.
7. **Bounded VDDC disk budget.** Measure actual owned VDDC file bytes with
   checked arithmetic. Protect every reachable key first, then choose only
   unprotected canonical `.vddc` records until the configured VDDC budget is
   met. P0 must lock a deterministic victim order that survives restart without
   depending on unreliable access-time updates; prefer immutable record age
   with build-fingerprint tie-breaking unless current filesystem capabilities
   disprove it. If protected bytes alone exceed budget, retain them all and
   report the over-budget condition.
8. **Loose-generation retirement and reclamation.** Capture a bounded record of
   each superseded canonical `.vres` path before current publication bookkeeping
   advances. A loose generation remains protected while it is the current
   descriptor, is owned by an open/active `ResourceSource` or load, is required
   by retained publication/retry state, or belongs to a current/retiring resource
   or resident generation. Reclaim it only after all relevant owners explicitly
   prove retirement. Do not infer safety from descriptor replacement, absence
   from the current publication table, successful decoding, elapsed frames or a
   failed filesystem delete. Define bounded overflow recovery without losing
   candidate paths.
9. **Separate domains and limits.** VDDC keys and loose physical generations
   are never interchangeable. Give their scans, protection counts, removed
   bytes, failures, configured budgets/retention limits and over-limit reasons
   separate status. A VDDC record may become eligible while its materialized
   loose generation remains live, and vice versa. Neither domain borrows the
   other's budget or uses deletion in one as proof for the other.
10. **Strict containment.** Operate only below the configured VDDC or loose root
    and only on that domain's canonical fan-out records or provably owned stages.
    Reject path escape, symlink/reparse traversal and malformed candidates.
    Never delete source, sidecar, VADI, VPAS, VTXN, current loose publication,
    package, log or user-state data; never broadly clean the repository or
    project root.
11. **Crash and restart behavior.** VDDC and loose generations remain
    individually atomic. Maintenance deletion is idempotent and must not require
    a second durable cache database or an authored project transaction journal.
    After interruption, the next bounded scan reconstructs candidates from
    canonical filenames plus current owner evidence, reclaims only provably
    abandoned owned stages, and continues safely.
12. **Bounded scheduling.** Project-sized enumeration, validation and deletion
    run on an existing worker/job seam. Owner ticks perform bounded admission,
    handoff and status work. Define limits for candidates, bytes, errors and
    retained status in both domains. A new explicit request or newer state may
    retry; ordinary ticks do not spin on a terminal failure.
13. **Portable roots.** Cache, loose and index locations remain configured roots
    selected by existing initialization. VADI/VDDC and canonical loose names use
    stable identities/fingerprints, not embedded machine paths. Moving a complete
    project and its DerivedData/Intermediate trees must not invalidate records
    merely because the parent absolute directory changed.
14. **Truthful status.** Expose requested/running/complete/failed state and
    separate VDDC/loose scanned, protected, eligible, removed and failed
    record/byte counts, limits and over-limit reasons through the shared
    pipeline/headless surface. Keep source acceptance, build, publication,
    resident and maintenance status separate.
15. **No scope drift.** Do not add a remote/shared cache, background daemon, new
    VDDC/VRES format, editor UI, package pruning, speculative artifact
    deduplication or E2.6 command-line/frontend closure.

## Ordered implementation gates

### P0 - architecture proof and scoped execution plan

After explicit authorization, create and activate the scoped planning triplet.
Refresh the RED evidence; trace exact VDDC store/open lifetimes, VADI committed
and transaction state, graph/recooker activity, E2.4 delivery/resident retention,
every independently initialized artifact source, corrupt-record and temporary
deletion, canonical `.vres` materialization, streamer source/load lifetime,
resource/resident retirement, filesystem metadata/containment and shutdown
order. Name the root-scoped VDDC authority, compatible registration rules,
protection contributors, admission/lease lock order, existing repair-admission
path, loose-retirement handoff, worker boundaries, deterministic victim order
and separate status limits. P0 must receive a fresh independent current-tree
review with no blocker/high/medium finding before it closes; P1 requires later
explicit authorization. Resolve gaps rather than weakening requirements.

### P1 - shared VDDC authority, origin and protection foundations

**Status:** closed on 2026-09-17. The implementation established the assets-owned
same-root authority, exact memory/persistent/compiler origin, exact durable-VDDC
proof and publication handoff, reader-specific persistent-hit transfer, one
durable protection domain per root, bounded VADI/graph/delivery protection
snapshots, owned stage recovery and authority-routed corrupt cleanup. Focused Debug
x64 and Development x64 builds/runtime tests passed for
`assetsPersistentCacheTests`, `assetIndexTests`, `assetGraphTests` and
`assetStreamingTests`; the engine-service audit and scoped `git diff --check`
passed. Two fresh independent CURRENT-tree closure audits reported
**0 BLOCKER / 0 HIGH / 0 MEDIUM**. P2 was not started.

Add the smallest general APIs to the existing owners so an exact build result
reports memory/persistent/compiler origin, every source instance for one cache
root shares reader/store/deletion admission, and bounded committed/active/
delivery protection evidence can be supplied without reversing module
dependencies. Route corrupt-record cleanup through this authority and establish
provably owned VDDC/loose stage naming before startup cleanup. Do not expose
mutable internal arrays or mirror the cache index in `ProjectAssetPipeline`.
Add focused owner-level lifetime/race tests.

### P2 - current-record validation and repair

**Status:** closed on 2026-09-17. The implementation validates complete exact
VADI-referenced VDDC records during restart/restoration, committed-delivery reuse,
and synchronous/observation reconciliation, including already-installed outputs.
Missing/corrupt/truncated/descriptor-mismatched exact current records enter bounded
`CacheRepairRequired` state, resolve canonical `OwnedBuildRequest` evidence and use
only the existing E2.3 automatic-recook / `IncrementalRecooker` / `BuildGraph` /
VADI transaction. Forced repair outputs are mandatory graph roots; stale repair
markers are pruned when their current request disappears; and authoritative
recovery exact-revalidates current VADI/VDDC truth to reconstruct lost repair
identity. Disabled-state semantics, sticky no-spin failure/explicit retry, Busy
admission, P1 root protection/lease ordering and E2.4 last-valid publication/
resident retention remain intact.

Closure review history is preserved rather than collapsed: the first fresh review
reported **0 BLOCKER / 1 HIGH / 1 MEDIUM** and drove whole-record synchronous
validation plus stale repair-marker pruning. Later stricter independent audits
reported **0 BLOCKER / 2 HIGH / 1 MEDIUM** and drove mandatory forced roots,
authoritative recovery reconstruction, and reconciliation/disabled-state fixes.
After those corrections, final serialized `/m:1` Debug x64 and Development x64
build/runtime matrices passed for `assetsPersistentCacheTests`, `assetIndexTests`,
`assetGraphTests` and `assetStreamingTests`; all executables exited 0 and
assetStreaming reported `0 failed checks` in both configurations. The engine-
service audit and scoped whitespace/diff hygiene passed. Final fresh CURRENT-tree
independent re-review reported **0 BLOCKER / 0 HIGH / 0 MEDIUM**. P3/P4/E2.6 were
not started by this closure.

Validate VADI-referenced VDDC during restart/restoration and before committed
delivery reuse. Missing or invalid exact records enter explicit repair state and
enter only the existing E2.3 automatic-recook scheduler through canonical
request resolution and the existing transactional graph. Prove admission
exclusion, coalescing, success restoring cache/index coherence and failure
retaining last-valid publication/resident state without compiler loops,
parallel schedulers or transient-state replay.

### P3 - bounded, race-free VDDC maintenance

Implement configured-budget measurement, deterministic unprotected victim
selection and cache-owner deletion admission. Keep scans and I/O off owner ticks;
bound each batch and status payload. Protect open readers and every P0 owner,
handle sharing/I/O failures as deferred work, and make cancellation/shutdown
leave the cache structurally valid.

### P4 - loose-generation reclamation and lifecycle integration

Capture superseded `.vres` generations before bookkeeping replacement, integrate
streamer/resource/resident retirement evidence, and reclaim only after explicit
owner proof. Integrate both maintenance domains' requests/status with
`ProjectAssetPipeline`; prove initialization, interruption, restart, relocation
and teardown ordering. Maintenance must not start while incompatible graph/
source mutation or delivery work owns relevant state; incoming work must acquire
protection/admission or receive a truthful Busy/deferred result.

**Current implementation/validation status (2026-09-17):** implemented. Exact
streaming generation leases and exclusive retire/delete admission, bounded
project candidate reservation/settlement/restart maintenance, exact neutral
renderer retirement peek/ack and graceful native-retirement ordering are present.
The project retirement-report result distinguishes acknowledgement-safe,
retryable and invalid outcomes without adding a rendering dependency. A later
closure audit found that the production caller was missing; the editor
`ProjectWorkspaceService` now owns the real texture-domain `ProjectAssetPipeline`
and boundedly bridges renderer evidence to it. Busy remains unacknowledged;
Accepted/NotTracked acknowledge the exact renderer retirement id. Rendering
quiesce finalizes evidence before dependent drain, and renderer drain fails closed
if delivery is still incomplete. `editorAssetRetirementTests` proves that exact
production seam in Debug and Development. Fresh Premake plus Debug/Development
streaming, assetStreaming, rendering/engine,
texture-residency-service and core E2.5 asset tests are green. The broader
`geometryAllocatorTests` target compiles in both configurations and passes the P4
texture/residency path, but later exits on an unrelated concurrent GPU-scene
aggregate stats oracle (`20/15/9/9/0` current vs `19/14/8/8/0` stale expected);
that broad executable is not claimed green by P4. Fresh exact-CURRENT self-review
after the composition correction returned **0 BLOCKER / 0 HIGH / 0 MEDIUM**,
explicitly classifying that later aggregate-oracle drift outside P4. **P4 is
closed. P5 later closed as recorded below. At the P5 checkpoint P6 had not yet
started; it was subsequently authorized.** E2.6 has not been entered.

### P5 - deterministic regression matrix

At minimum cover:

- cold build, process restart and persistent hit with compiler call count zero;
- distinct memory-hit, persistent-hit and built reporting;
- missing, truncated, corrupt, wrong-key and wrong-content current VDDC repair;
- failed/cancelled/stale repair preserving committed VADI and last-valid
  descriptor/resident truth;
- source/settings/importer/compiler/dependency changes producing exact new keys;
- budget below total and below protected bytes, deterministic ties, checked
  byte overflow and empty cache;
- protection of committed VADI, staged transaction, active build, retained
  retry and open `ArtifactSetReader` keys across at least the BuildSystem and
  ProjectLoosePublisher source instances sharing one cache root;
- reader-open/store/commit/corrupt-lookup races against VDDC deletion, delete
  refusal and bounded later retry;
- replacement that captures the old `.vres` path before advancing current
  bookkeeping, then protects it independently for an open streamer source/load,
  retained retry, old resource generation and retiring resident/GPU generation;
- eventual `.vres` deletion after every owner retires, plus overflow recovery
  that rediscovers a candidate rather than deleting it without proof;
- separate VDDC and loose limits/status, including each domain independently
  over its limit while the other remains below it;
- malformed filenames, foreign `.tmp` files, unexpected files and path/reparse
  escape attempts in both roots;
- interrupted maintenance, exact owned-stage recovery, restart and project
  relocation; and
- bounded owner ticks, repeated requests, cancellation, shutdown and
  reinitialize with no test residue outside the scratch helper.

Use real owner seams. Fault injection may cover failures that cannot be induced
portably, but record that distinction.

**P5 closure status (2026-09-18):** the complete matrix above has deterministic
coverage and the focused Debug/Development runtime set is green. A fresh
independent exact-CURRENT closure review returned **0 BLOCKER / 0 HIGH /
0 MEDIUM**, so **P5 is closed**. The only P5 production
correction was exact canonical owned `.vres.tmp` recovery; physical mutation still
goes through `ResourceStreamer::RetireLooseGeneration`, while arbitrary/malformed/
misplaced files remain foreign. `assetsPersistentCacheTests`,
`assetStreamingTests` and `streamingTests` all build and run in Debug and
Development with serialized `/m:1`; the engine audit and scoped hygiene checks
pass. Privileged reparse creation and literal `u64` filesystem rollover are not
fabricated in tests: the exact containment/reparse guards are source-reviewed and
checked bounded arithmetic is exercised with portable fixtures. At that P5
checkpoint P6 had not started; it was subsequently authorized and its current
validation evidence is recorded below. E2.6 remains unauthorized.

### P6 - validation, closure and independent review

Run fresh `premake5.exe vs2022`, focused Debug and Development builds with
serialized outer MSBuild `/m:1`, then execute every applicable test binary.
Keep generation, compilation/linkage, execution, source review and fault-
injection claims separate. Run diff/whitespace, header-layout, generated-output,
scratch-residue, dependency-direction and E2.6-leakage checks. Update this plan,
the parent E2 plan and foundation progress with exact evidence. Obtain a fresh
current-tree independent blocker/high/medium review, then stop; do not start
E2.6.

**P6 closure status (2026-09-18):** fresh
`premake5.exe vs2022` exited 0 with the engine-service audit passing. Serialized
`/m:1` Debug and Development compile/link passed for
`assetsPersistentCacheTests`, `assetIndexTests`, `assetGraphTests`,
`assetStreamingTests`, `streamingTests`, `textureResidencyServiceTests`,
`editorAssetRetirementTests`, `geometryAllocatorTests`, `rendering` and
`engine`. The first seven test executables in that matrix pass in both
configurations; `assetStreamingTests` reports zero failed checks and the
streaming asynchronous, texture-residency integration and production
editor-retirement paths pass. `geometryAllocatorTests` executes in both
configurations and fails only the already documented unrelated GPU-scene
aggregate oracle, `20/15/9/9/0` versus hard-coded `19/14/8/8/0`; it is not
claimed green. Engine/header-layout, tracked/no-index whitespace,
generated-output, scratch-residue, dependency-direction, delete-authority,
E2.6-leakage and process-residue checks are clean. The only retained files are
inside approved scratch helpers: five canonical VDDCs per `asset_index_tests`
configuration and three geometry fixtures per failed geometry configuration.
No repository/source cache/output residue or focused build/test process remains.
No production E2.5 edit was needed during P6. A fresh final docs-inclusive
independent exact-CURRENT review returned **0 BLOCKER / 0 HIGH / 0 MEDIUM**,
confirming the validation ledger, ownership/source seams, helper-local scratch
classification, dependency direction, delete authority and phase boundaries.
**P6 and E2.5 are closed. E2.6 remains unstarted and unauthorized.**

## Pasteable agent instruction

Implement only E2.5 after explicit authorization, using this document as the
durable contract. Start at P0, create the scoped planning triplet, refresh the
named RED and Vanguard evidence, resolve both the root-scoped VDDC authority and
the separate loose-generation retirement path, obtain independent P0 review,
and stop before P1 without separate authorization. Extend existing VDDC/VADI,
pipeline, publisher and streamer owners rather than creating another cache,
index or scheduler. Preserve every committed, active, publishing, reading and
resident generation; make repair and both deletion domains race-safe and
bounded; use Premake and `/m:1`; and do not start E2.6.
