# E2 project import, incremental changes and local cache plan

## Status and purpose

E2.1 is accepted after an observation-enablement repair; the earlier acceptance
was premature and is corrected in the E2.1 section below. A later E2.2 follow-up
review found one callback-importer classification defect, one test-cleanup defect,
and a terminology improvement. Those corrections are implemented, locally
validated and independently re-accepted with no remaining blocker/high/medium
finding. E2.3 is accepted under its detailed gate plan: P0-P5 are complete and the
complete P6 matrix is green. Fresh post-P5 P7
generation/build/execution/hygiene validation passed before a stricter P5 closure
review identified four medium proof/documentation gaps; those gaps are repaired on
the current tree, exact-tree final validation/hygiene is green, and the final
independent re-review reported `0 blocker / 0 high / 0 medium`. E2.4 P0 is closed;
P1 owned committed-delivery/shared-status implementation and focused validation are
complete, and its final independent CURRENT-tree review reported `0 blocker / 0 high
/ 0 medium`. P1 is closed. The user subsequently authorized P2 and then P3; exact
fenced publication/retry and supported resident replacement/retirement are complete
and closed after exact-tree validation and final independent CURRENT-tree reviews at
`0 blocker / 0 high / 0 medium`. The user subsequently authorized P4; removal/restart/
bounds/teardown is complete and closed after the final post-fix exact-tree validation,
hygiene, and independent CURRENT-tree review at `0 blocker / 0 high / 0 medium`. The user
subsequently authorized E2.4 P5 through P6 end-to-end. P5 is complete and independently reviewed
at `0 blocker / 0 high / 0 medium`; P6 is complete and the final independent CURRENT-tree E2.4
closure review reported `0 blocker / 0 high / 0 medium`. E2.4 is closed. E2.5
P0-P3 are now closed. P4 was subsequently authorized and directed through closure,
then correctly reopened when a later audit found the renderer retirement endpoint
and project retirement endpoint had no production composition caller. Editor
composition now owns that bridge: exact renderer evidence is reported to the single
project pipeline, `Busy` remains unacknowledged for retry, and only `Accepted`/
`NotTracked` acknowledge the exact renderer id. The focused production-seam
regression and renderer shutdown regression pass in Debug and Development. With
the external reviewer unavailable, the user explicitly authorized the final
exact-CURRENT self-review; it returned **0 blocker / 0 high / 0 medium**. P4 is
closed. P5 subsequently closed at final independent **0 blocker / 0 high / 0
medium**. P6 was then authorized; fresh generation, Debug/Development
compile/link, focused executable validation and hygiene/scope checks are complete,
with only the already-documented unrelated `geometryAllocatorTests` GPU-scene
oracle remaining non-green. The final docs-inclusive independent exact-CURRENT
P6 review returned **0 blocker / 0 high / 0 medium**. **P6 and E2.5 are closed.**
The user subsequently authorized **E2.6 definition work only**. A first
independent review returned 0 blocker / 0 high / 0 medium; a stricter second
audit found **0 blocker / 0 high / 1 medium** in the proposed per-output
Generated-dependency freshness semantics. The dedicated contract is corrected
and final strict exact-CURRENT re-review returned **0 blocker / 0 high /
0 medium**. **The E2.6 definition is closed.** The user subsequently authorized
P0, P1 and P2 separately and in order. P0 closed after a corrected 0 blocker / 3
high / 2 medium preflight and final independent 0 blocker / 0 high / 0 medium
review. P1 closed after focused Debug/Development validation and final independent
0 blocker / 0 high / 0 medium review. P2 then closed after its corrected 0
blocker / 2 high / 1 medium implementation preflight, fresh focused
Debug/Development compile/link and executable validation, scope/dependency
audits, and final independent **0 blocker / 0 high / 0 medium** review. P3 was
then separately authorized and closed after the bounded nanovanguard command /
protocol implementation, exact-current Debug and Development build plus 20/20
runtime invocations in each configuration, production CLI smoke, dependency /
scope / hygiene audit, and final independent **0 blocker / 0 high / 0 medium**
review. P4 was subsequently authorized; its implementation, focused validation,
production smoke and final independent reviews are complete. The user subsequently
changed the remaining intermediate-gate policy so broad Premake/build/test/native
validation is batched into E2.6 P6 while independent exact-CURRENT checks remain
required. Under that policy **P4 is closed** at 0 blocker / 0 high / 0 medium; the
historical unrelated dirty world-session Premake audit was deferred to P6, not an
intermediate closure blocker; fresh P6 generation now passes that audit. The user
subsequently authorized **P5 only**.
P5's deterministic matrix is source-complete and its final independent
exact-CURRENT review returned **0 blocker / 0 high / 0 medium**; under the same
policy **P0-P5 are closed**. The user subsequently authorized P6. Fresh Premake
generation, serialized Debug/Development compile/link, owner/test execution,
42/42 headless matrices per configuration, rebuilt production nanovanguard
scratch execution and final dependency/scope/hygiene checks are complete. P6
found and corrected one test-only create-vs-overwrite fixture defect without a
production freshness change. The final independent exact-CURRENT P6 closure
review returned **0 blocker / 0 high / 0 medium**. **P6 and E2.6 are closed;
the complete E2 phase is closed.**
This document
is the durable execution contract for the E2 asset-foundation subphases. It
refines the phase-level roadmap in
[editor roadmap](editor-foundation-study-and-roadmap.md).

Agents should implement and close the subphases in order. The first subphase
whose status is not `complete` is the next candidate, not automatic authorization.
Implementation requires an explicit user instruction to start that subphase. Each closure must
update this document and [foundation progress](../progress/project-asset-foundation-progress.md) with separate
source-review, generation, compilation, linkage and executed-test claims.

## Existing authority that E2 must reuse

- `SourceDatabase` owns physical source inventory, stable AssetIds, outputs and
  authored-reference facts.
- `AssetFileTransaction` owns durable authored-file commit and restart recovery.
- `ProjectAssetPipeline` is the shared owner-thread integration and command
  surface for editor and headless frontends.
- `BuildSystem`, `BuildGraph`, `IncrementalRecooker`, VADI and VDDC own build
  planning, dependency invalidation, execution, committed build state and cache.
- `ProjectLoosePublisher` and `ResourceStreamer` own committed loose delivery and
  runtime resource resolution. E2 may coordinate them but must not replace them.
- E1F operations already own move, rename, duplicate, delete, retarget and
  session-local undo/redo. E2 observes external changes and coordinates their
  consequences; it must not add parallel file-operation or undo systems.

No E2 subphase may introduce a second source registry, dependency graph, build
scheduler, DDC, filesystem transaction layer, runtime loader or editor-private
asset backend.

## Cross-subphase invariants

- OS directory notifications are lossy path hints, never authoritative asset
  transactions. Reconciliation against disk and the current source catalog is
  authoritative.
- Source and sidecar notifications may arrive separately, in either order, and
  during partial writes. A burst is coalesced before classification.
- Watcher overflow, root replacement or an unrepresentable event requests a
  bounded authoritative rescan; it must not silently discard possible changes.
- E1F-owned writes are committed before reconciliation. Their intermediate
  staging, backup and journal files must not become source assets or trigger a
  duplicate competing operation.
- Work per owner tick is bounded. Large copies, branch switches and overflow
  recovery may span ticks without blocking the frontend or losing later hints.
- Only one incremental recook batch owns graph work at a time. Changes arriving
  during it accumulate for a later batch.
- A build captures the source, settings, importer and dependency revisions it
  represents. Completion that is stale against current revisions cannot replace
  committed VADI state or the last valid published artifact.
- Failed, cancelled or stale builds preserve the last committed artifact and
  report why current source state is not represented by it.
- External deletion produces a missing asset and explicit dependent diagnostics;
  it does not erase authored references or silently rewrite dependents.
- Authored schema version, importer version, runtime format version and build
  fingerprint remain distinct version domains.
- Shutdown stops new observation first, drains or cancels owned work through the
  existing owners, and releases callbacks before any referenced service closes.

## Ordered implementation subphases

### E2.1 - external change observation and bounded reconciliation

**Status:** **complete and accepted after repair.** The earlier "complete"
claim was premature: a later Codex review found that `EnableObservation` did not
require `CanMutateSources()` before starting a session, so a generation-zero
authoritative rescan could begin while live `BuildGraph` work owned the graph.
The common observation-enablement path now rejects a busy graph with
`ProjectAssetPipelineResult::Busy` before any worker, observer, accumulator or
session side effect; a deterministic regression proves the absence of startup
side effects. That repair closed E2.1 without implementing E2.2 behavior; E2.2
was subsequently implemented and is now complete as recorded below.

Add the narrow filesystem/platform observation seam needed for configured source
roots and integrate its lifecycle with `ProjectAssetPipeline`. Notifications
carry path/root hints and explicit overflow or rescan-required state into an
owner-thread accumulator. Coalesce bursts, wait out incomplete source/sidecar
pairs and partial writes, exclude E1F transaction artifacts, and process a
bounded amount per tick. Reconciliation must use the existing source scan and
publication-reconciliation path rather than mutating `SourceDatabase` rows from
watcher callbacks.

The observer must support clean start, stop and restart, reject callbacks after
shutdown, and retain enough dirty-root state that overflow or allocation failure
degrades to an authoritative rescan. Do not start cooking in this subphase.

**Source-level implementation:**

- `filesystem/directory_observation.hpp` adds the observation seam:
  `DirectoryObservationKind`, `DirectoryObservation` (path + root index +
  kind), `DirectoryObservationCallback`, `DirectoryObserverConfig` and the
  `DirectoryObservationSource` interface (`Start`/`Stop`/`IsRunning`). It is
  header-only, so the `filesystem` module gains no runtime dependency.
- `asset_streaming/platform_directory_observer.hpp/.cpp` implements the seam on
  Windows with one overlapped `ReadDirectoryChangesW` worker per root. A
  zero-byte completion and every reported I/O failure become `Overflow`;
  `Stop()` signals, cancels, joins and deletes every worker before returning.
- `asset_streaming/source_change_accumulator.hpp/.cpp` is the owner-thread
  accumulator. `Publish` is thread-safe, records only coalesced path hints and
  never touches `SourceDatabase`, publications or the build graph. Coalescing is
  by case-insensitive path key; deferral is tick-based with a forced maximum;
  inspection/promotion per tick is bounded. Generation-scoped acknowledgement
  prevents an older reconciliation from consuming later ordinary hints or
  overflow. Overflow/allocation failure requests an authoritative rescan while
  represented pending entries are retired incrementally rather than bulk-cleared
  on an owner tick. E1F-owned
  `.vtxn-stage`, `.vtxn-backup`, journal and journal `.stage` paths are
  suppressed; the suffix policy is owned by `assets`
  (`AssetTransactionStageSuffix`/`AssetTransactionBackupSuffix`).
- `ProjectAssetPipeline` gains `EnableObservation` (owned platform backend or a
  borrowed injected source), `DisableObservation`, `IsObservingSources`,
  `TickObservation` and `GetObservationStatus`. One persistent pipeline-owned
  worker performs authoritative inventory/classification/reference scanning,
  loose materialization/publication removal/pruning and old-catalog retirement;
  owner ticks only poll/handoff state and inspect at most the configured
  reconciliation-operation budget. Catalog and reconcile payload handoff uses
  constant-time swaps and O(1) lookup indexes. Enabling observation immediately
  starts a generation-zero authoritative catch-up, so changes made while
  observation was disabled are recovered without counting as an observed-change
  rescan. `DisableObservation` refuses pre-existing dirty work; after `Stop()`
  becomes the callback lifetime barrier it rechecks for a stop-window hint and,
  if necessary, performs one authoritative synchronous catch-up before reporting
  success. Full pipeline shutdown stops callbacks, drains/joins the worker and
  then tears down owned publications. Manual `Rescan` remains synchronous and
  otherwise unchanged.
- `assetStreaming/tests/project_asset_pipeline_tests.cpp` adds a deterministic
  `ManualObservationSource` and scenarios for external create/modify/delete,
  rename bursts, source/sidecar order, duplicate and case-only hints,
  partial-write deferral, overflow-to-rescan, transaction-artifact exclusion,
  bounded multi-tick promotion and reconciliation with late hints, shutdown and
  restart catch-up, callbacks racing with `DisableObservation::Stop`, active
  shutdown, and unchanged manual rescan. A supplemental Windows test exercises
  real `ReadDirectoryChangesW` parsed-path delivery, stop/restart, case-only
  rename and missing-root recovery.

**Exit gate (met 2026-09-15):** focused tests prove external create, modify, delete
and rename bursts; source/sidecar arrival in both orders; duplicate hints;
case-only path changes where supported; partial-write deferral;
overflow-to-rescan; bounded multi-tick progress; E1F self-write suppression; and
shutdown/restart with pending notifications. Native Windows coverage proves a
real parsed path cannot false-pass through Overflow and that a temporarily
missing watched root recovers. Physical `.vtxn-stage`, `.vtxn-backup`, configured
journal and journal `.stage` files are excluded by authoritative full rescan.
Existing manual `Rescan` behavior remains valid.

Verification executed 2026-09-15: `premake5.exe vs2022` passed the Vanguard
engine-service audit and regenerated the VS2022 workspace. Focused Debug x64 and
Development x64 `assetStreamingTests.vcxproj` builds succeeded with serialized
outer MSBuild `/m:1` under the project warning-as-error configuration. The Debug
executable passed five consecutive runs, including the native watcher cases, and
the Development executable passed with `0 failed checks`. `git diff --check`
returned exit 0; the only output was existing LF-to-CRLF working-copy notices.
The original acceptance claimed no blocker, high or medium finding; that claim
was premature and is superseded by the repair below.

**Enablement repair (2026-09-15).** A later Codex review found that
`EnableObservationOn` configured accumulator/session state, created and started
the persistent observation worker, started the `DirectoryObservationSource` and
submitted the generation-zero authoritative rescan without first requiring
`CanMutateSources()`. A caller could therefore enable observation while live
`BuildGraph` work owned the graph, violating the mutation/build exclusion
invariant. The fix hoists the logic into one shared predicate: the build/source
exclusion check now lives in an `Impl::CanMutateSources()` helper used by both
the public `ProjectAssetPipeline::CanMutateSources()` and the observation
enablement common path, and `EnableObservationOn` returns
`ProjectAssetPipelineResult::Busy` before any side effect when that predicate is
false. Both public `EnableObservation` overloads (platform-owned and injected)
share this path, so they cannot diverge.

Regression: `assetStreamingTests` now holds a real blocking `BuildGraph` request
so `CanMutateSources()` is false, calls the injected `EnableObservation`, and
requires `Busy`; it proves the `ManualObservationSource` start counter is
unchanged, no session is running, and observation/reconcile counters do not
advance. It then releases the request, requires a successful enable, drains the
generation-zero catch-up and disables cleanly.

Repair verification executed 2026-09-15: `premake5.exe vs2022` passed; focused
Debug x64 and Development x64 `assetStreamingTests.vcxproj` builds succeeded with
serialized outer MSBuild `/m:1`; the Debug executable passed six runs (one
initial plus five consecutive) and the Development executable passed once, all
with `0 failed checks`; `git diff --check` returned exit 0 and no test filesystem
residue remained. A fresh source-level acceptance review of the observation
lifecycle (enablement exclusion, generation-zero ordering/acknowledgement,
worker lifetime, command/completion synchronization, disable/shutdown, bounded
owner ticks, catalog-swap/reclamation and publication ownership, artifact
exclusion and late-overflow retention) found no further blocker, high or medium
issue.

### E2.2 - authoritative change classification and invalidation state

**Status:** **complete and independently re-accepted after follow-up; depends on
accepted E2.1.** P1's bounded
public/internal accepted-state/query representation, P2 deterministic comparison/
canonical invalidation generation, P3 transactional candidate preparation +
manual/observed owner acceptance, and P4 VPAS restart persistence are implemented.
P5 covers the durable exit matrix. A later review found that E2.2 conflated
callback-importer support with descriptor-level typed-seed availability, and also
found non-hermetic cleanup for the directory-shaped snapshot I/O fixture. The
support/seed split, callback initial/change/restart regression, checked cleanup,
and accepted-asset-baseline terminology correction are now implemented and pass
focused Debug/Development validation. Independent follow-up review verified the
callback behavior, cleanup, naming/VPAS invariants, validation/hygiene gates and
E2.3 boundary with no remaining blocker/high/medium finding. E2.3 was not started.

#### Purpose and authority

Compare each complete reconciled `SourceDatabase` candidate with the last
accepted asset baseline and publish one canonical, bounded change set.
`SourceDatabase` remains authoritative for the current inventory, metadata,
classification and authored-reference facts. The retained baseline is only the
historical comparison/restart input; it must not become a second mutable source
registry. `DependencyIndex`/VADI remains authoritative for committed build
dependencies and outputs. `ProjectAssetPipeline` owns the owner-thread acceptance
and query surface.

The comparison is part of accepting a successful manual or observed rescan. A
candidate catalog, its classified state, its change set and its new accepted
asset baseline become visible together. Any scan, classification, reference scan,
comparison, allocation or snapshot-save failure preserves the previous live
catalog, accepted asset baseline, change set and statuses. Preparation, comparison,
staged-snapshot serialization and the atomic snapshot replacement are pre-commit:
any failure there discards the candidate and preserves every previous accepted
state. Once the catalog and accepted asset baseline/change state have committed, a
later retirement/cleanup or publication-reconciliation failure is post-commit:
the accepted state remains committed and is reported as cleanup/reconciliation
pending, never as a rollback of the acceptance. The accepted generation is
monotonic. Repeating an identical reconciliation emits no changes and does not
invent a new invalidation.

#### Canonical state and change model

Retain a bounded record for every uniquely identified asset plus source-row
diagnostics for rows that cannot safely own an `AssetId` (missing/invalid
metadata, duplicate identity and other conflicts). An accepted asset record
contains enough canonical evidence to compare without rereading the old source:

- stable `AssetId`, stable source-root identity, root-relative source identity
  and diagnostic physical path;
- SHA-256 source-content fingerprint;
- metadata/importer identity, authored importer version, available importer
  version, settings schema version and SHA-256 settings fingerprint;
- canonical output key/type identities;
- canonical authored-reference evidence: the actual accepted links
  `(from AssetId, to ResourceReference, DependencyKind,
  AssetReferenceLocation, inPlace)`, sorted/deduplicated with the same canonical
  ordering `SourceDatabase` uses, plus the per-source reference-scan state. A
  digest may be stored additionally for equality acceleration, but a digest alone
  is insufficient: candidate reference seeding, failed-source preservation,
  restart restoration and missing-output referencer diagnostics all need the
  actual links;
- source issue, import state and reference-scan state;
- presence/tombstone state and the accepted generation.

Source fingerprint and reference evidence must describe one coherent source
version. Candidate preparation must capture the SHA-256 of the exact bytes the
reference scanner read: hash the same loaded content buffer the scanner used, or,
when a separate bounded streaming read is required (for example rows where no
scanner runs), detect that the file changed during preparation and fail/retry
instead of accepting a fingerprint from one source version and references from
another. Reuse `crypto::Sha256Builder` and the existing source-data provider; do
not add a source cache. A row whose source cannot be read has no fingerprint and
remains a source-row diagnostic, never a fabricated content change.

Change reasons are composable flags, not a mutually exclusive enum. At minimum
they represent `New`, `ContentChanged`, `SettingsChanged`,
`ImporterVersionChanged`, `Moved`, `Missing`, `Restored`, `Unsupported` and
`Conflicting`. Output-set/type changes and authored-reference changes must also
remain explicit when they affect invalidation or diagnostics. A record carries
the previous/current evidence that frontends need, but never borrowed
`SourceRecord` pointers.

For each accepted change set, publish two distinct, bounded invalidation lists.
They are invalidation evidence only: E2.2 must not call
`IncrementalRecooker::Request`, `BuildGraph::Request`, publish artifacts, replace
resident resources or remove VADI records.

- **Canonical source/build invalidation seeds** for later E2.3 translation: when
  an importer exposes a descriptor-level source type, emit the typed current
  source identity `ResourceReference(root-relative source identity, sourceType)`
  for each changed source whose build inputs may be dirty (new,
  content/settings/importer-changed, restored, or moved onto a known output).
  Valid callback importers are supported without a descriptor `sourceType`; their
  dirty evidence remains in the canonical change record (plus output evidence)
  without fabricating a typed seed. E2.3 owns translating those records through
  importer/build-request resolution. For typed seeds, this is the identity space
  `IncrementalRecooker` consumes:
  `AssetChange::identity` is resolved with
  `DependencyIndex::GetDirectDependants(identity)` and must be a
  dependency-index *input* identity, not a cooked output identity.
- **Previous/current cooked output identities** for diagnostics,
  output-set/type changes, deletion/missing state and later
  publication/removal logic. Use current outputs for present assets and previous
  outputs for missing/removed/retargeted assets, deduplicated.

The cooked-output list is **not** claimed to be directly suitable for
`IncrementalRecooker::Request`. Passing a cooked output as
`AssetChange::identity` queries only that output's dependants (and returns
`UntrackedChange` for a leaf), so it is not a correct seed and can skip
rebuilding the output itself. E2.3 owns the translation from canonical change
evidence and any available typed seeds to `AssetChange` values, including
callback-importer resolution and the ordinary-`BuildGraph` path for
new/untracked/moved identities with no committed VADI record.

#### Identity and classification rules

- Asset identity comes only from a valid, unique `.vmeta` `AssetId`. Path or
  byte equality is evidence for diagnostics, never authority to assign or merge
  identity.
- The same unique `AssetId` at a different source location is `Moved`; the ID
  and output identities remain stable. Location is the stable source-root
  identity plus the root-relative source identity, because two different roots
  can yield the same root-relative identity. `Moved` may coexist with content,
  settings or importer reasons.
- An old present identity absent from the candidate becomes a retained `Missing`
  tombstone. A candidate row that carries a valid, unique `AssetId` but reports
  `SourceMissing` (the source file is absent while its `.vmeta` sidecar remains)
  is also `Missing`, not a present healthy asset; `SourceDatabase::Rescan`
  normalizes an orphan sidecar back into such a row. `Missing` preserves the
  previous content fingerprint, location evidence where appropriate, output
  identities, canonical authored-reference evidence and referencer diagnostics.
  Reappearance of that identity is `Restored`, optionally combined with `Moved`
  or other changed reasons. A never-accepted identity is `New`. Invalid or
  duplicate identities remain conflict diagnostics and never receive an
  arbitrary winner.
- Missing/unreadable/invalid metadata and duplicate asset/output/source
  identities remain separate source-row diagnostics. Never select an arbitrary
  duplicate winner or infer a move. Multiple plausible content/path matches are
  reported as ambiguous move evidence only.
- `Unsupported` covers a transition into a non-buildable importer state and
  remains distinct from conflicts. Transitions back to a supported state are
  visible even when source bytes are unchanged.
- Settings comparison includes schema version and canonical settings bytes.
  Importer comparison distinguishes authored importer selection/version from
  the currently registered importer version. Low-level compiler/version dirty
  evaluation remains VADI/BuildSystem authority.
- Preserve E1D authored references from sources that remain present. Targets may
  be missing. A missing asset exposes its previous output identities and the
  current backward authored references to those outputs, so deletion never
  silently erases dependent diagnostics.

#### Restart and persistence

Persist the accepted asset baseline in the explicitly versioned, bounded,
checksummed VPAS snapshot format beside the existing dependency-index state.
Use staged write, flush/read-back validation and same-directory atomic
replacement following the established VADI save protocol. Reuse the protocol
only, not VADI's result classification: E2.2 has its own loader result
semantics. Corrupt, incompatible or over-limit VPAS data is reported and
conservatively rebuilt from the current catalog (a missing or unusable file
bootstraps as `New`); a true filesystem I/O failure is reported as an I/O failure
and must not claim reliable recovery. VPAS data is never trusted partially
and never causes source mutation. The baseline is not VMETA, VADI, VDDC, VTXN,
undo history or runtime data. Add a format document when the on-disk layout is
implemented.

On first initialization with no accepted asset baseline, the current valid catalog is
classified as `New`. On restart with a valid VPAS image, restore per-asset accepted
state/evidence (fingerprints, canonical references, outputs, tombstones,
diagnostics and the accepted generation) without requiring a cook. An identical
catalog after restart produces an **empty current change set**: pre-restart
changes are historical evidence and are never re-emitted as new notifications or
consumed by E2.3 as new work. A historical "last changes" view is not required
by E2.2. Do not claim a cache hit or last-valid artifact state here; those belong
to E2.4/E2.5.

#### Public/query surface and limits

Expose borrowed, owner-thread queries through `ProjectAssetPipeline` for the
current accepted generation, canonical change records, per-asset state,
unidentified/conflicting source-row diagnostics, invalidation outputs and
referencers of missing outputs. Returned spans/pointers are invalidated by the
next successful acceptance or shutdown. Configuration must bound snapshot
records, tombstones, changes, outputs, diagnostics and serialized bytes; limit
failure preserves the previous accepted state.

#### Explicit non-goals

No cooking, graph scheduling, cancellation, stale-completion logic, artifact
publication, runtime hot reload, DDC eviction, asset-browser UI, metadata repair
or guessed rename/identity assignment. E2.3 consumes the E2.2 invalidation data
and owns recook coordination.

**Exit gate:** deterministic tests cover initial/new state; content-only,
settings-byte/settings-version, authored/available importer-version and output
set/type changes; supported-to-unsupported and recovery; valid external move;
move plus content change; missing and restored tombstones; ambiguous move
evidence; missing, invalid and duplicate sidecars/identities without arbitrary
winners; deletion with preserved referencers; deduplicated previous/current
output invalidation; reference-only change; repeated no-op reconciliation;
transactional failure preserving the old accepted state; configured limits;
clean restart restoration; missing snapshot bootstrap; and corrupt/incompatible
snapshot recovery. Valid callback importers without descriptor `sourceType` must
also remain supported across initial acceptance, modification, and clean restart;
their dirty change/output evidence is retained without fabricating a typed source
seed or replaying work after restart. Focused Debug and Development production/test targets must
compile and link, tests must execute with zero failures, and `git diff --check`
must pass before E2.2 can be marked complete.

### E2.3 - incremental recook coordination and stale completion rejection

**Status:** complete and accepted on 2026-09-15 under the detailed E2.3 gate plan.
P0-P5 are implemented and the complete P6 deterministic matrix passes. Fresh post-P5 P7
generation/build/execution/hygiene validation was green before the strict P5 closure
review; its four medium proof/documentation gaps are repaired on the current tree,
with exact-tree final validation/hygiene green and final independent re-review clean
at `0 blocker / 0 high / 0 medium`. E2.4 P1, P2 and P3 are complete and closed after
their focused exact-tree validation and final independent reviews reported `0 blocker /
0 high / 0 medium`.

The detailed cross-agent contract is
[E2.3 implementation plan](project-asset-e2-3-plan.md). Read it before starting;
its pre-commit freshness gate and first-cook/callback translation requirements
are mandatory, not optional follow-up work.

Feed the canonical E2 change set into the existing `IncrementalRecooker` through
`ProjectAssetPipeline`. Maintain one in-flight recook batch, merge and deduplicate
later changes for the next batch, preserve dependency ordering and expose
progress, reasons and cancellation through the shared surface. Reuse existing
Jobs and graph ownership; do not create a watcher thread build queue.

Capture the revisions/fingerprints represented by each request. Before accepting
a completion, verify that its source, settings, importer and relevant dependency
state are still current. Cancel when useful, but correctness must rely on stale
completion rejection because cancellation may race with completion. Serialize
E1F mutation and manual rescan against live graph ownership using the established
pipeline rules.

**Exit gate:** focused tests now prove first cook of a newly discovered supported
asset, transitive dependent rebuild, burst coalescing, changes arriving during a
batch, cancellation, failure, stale success rejection, one-in-flight ownership,
and eventual processing of the accumulated later batch. Current P6 coverage also
proves source/settings/importer/dependency stale evidence, watcher-race rejection,
dirty/missing VADI restart recovery, and last-valid loose-publication retention for
stale rejection. The P5-owned automatic failure/cancellation, VADI commit-I/O,
overflow/bounds, no-busy-loop, bidirectional contention, retry/newer-change and
live-work teardown/reinitialize rows are now implemented and deterministic. Final
independent current-tree review is clean; the E2.3 exit gate is closed.

### E2.4 - committed publication, stale status and resident replacement

**Status:** P0 architecture proof completed and independently reviewed on
2026-09-16 with no blocker/high/medium contradiction in the written execution
contract. P1 owned delivery handoff/shared status implementation and focused
Debug/Development validation are complete; final independent CURRENT-tree review
reported **0 blocker / 0 high / 0 medium**, so P1 is closed. P2 exact fenced
publication/retry implementation is complete and closed; exact-tree validation passed
and the final independent CURRENT-tree re-review reported **0 blocker / 0 high / 0
medium**. P3 supported resident replacement/retirement is complete and closed: generic
resource generations overlap safely, the VTEX production path migrates newer resource
generations on one stable GPU residency identity, failed replacement preserves the old
resident with explicit retained-candidate retry, resident status is exact, and old GPU
state retires through existing real fences. Fresh focused Debug/Development validation
passed and final independent CURRENT-tree review reported **0 blocker / 0 high / 0
medium**. P4 removal/restart/bounds/teardown implementation is complete: old loose
generations are conservatively retained while owned, package fallback/removal is atomic,
restart uses accepted/current source plus VADI truth without transient replay, operation/
registry/status/resident retention is bounded, and shutdown/reinitialize ownership is
session-safe. A final docs-inclusive audit caught that ordinary reconciliation had not yet
adopted shutdown's atomic batch removal and that one progress paragraph was stale; both are
repaired. Final repetition also exposed and fixed a retained-operation lifetime race by
requiring scheduled plan/construct jobs to return before operation storage can be
reclaimed. Post-fix exact-tree Debug/Development compile/link and executable validation are
green, final hygiene is clean, and the fresh independent CURRENT-tree review reported
**0 blocker / 0 high / 0 medium**. P4 is closed. The user subsequently authorized E2.4 P5
through P6 end-to-end. P5 is complete and independently closed at **0 blocker / 0 high / 0
medium**. P6 fresh Premake generation, the complete serialized Debug/Development build matrix,
the complete executable matrix and final hygiene/scope checks are green; the final independent
CURRENT-tree E2.4 closure review reported **0 blocker / 0 high / 0 medium**. P6 and E2.4 are closed.
E2.5 P0-P4 are closed. At that E2.4 checkpoint E2.6 remained outside the
authorization; current E2.6 definition status is recorded below.
Read the detailed
[E2.4 cross-agent contract](project-asset-e2-4-plan.md) before implementation;
its post-cook publication fence and supported resident proof are mandatory.

For a current successful recook, persist the graph-committed VADI state and use
the existing loose publisher and `ResourceStreamer` replacement seam to deliver
the committed generation. Preserve the last valid descriptor on failed,
cancelled, stale, index-save or publication results and expose that it is stale
relative to authored state. Retry only through explicit existing ownership; do
not report an unpublished result as current.

Coordinate removal and external deletion without silently invalidating live
readers or authored references. Add only the narrow resident-resource replacement
or invalidation hook proven necessary by supported consumers; do not introduce a
general hot-reload framework or direct loader bypass.

**Exit gate:** tests prove successful replacement, last-valid retention for each
failure class, stale-to-current transition after a later success, publication
retry, deletion/missing behavior, active-reader safety and supported resident
replacement through `ResourceStreamer`.

P5 deterministic closure adds exact post-commit/pre-publication source/settings/importer/
dependency staleness, missing/corrupt committed-artifact handling, transitive Generated atomic
visibility/retry, unchanged reconciliation, configured capacity, and old-source replacement proof.
The final P5 independent review reported **0 blocker / 0 high / 0 medium**. P6 validation then
regenerated VS2022 projects and passed serialized `/m:1` x64 Debug and Development builds for
`assetRecookerTests`, `resourcesTests`, `resourcePipelineTests`, `streamingTests`, `rendering`,
`geometryAllocatorTests`, `textureResidencyServiceTests`, and `assetStreamingTests`; all seven
executable suites passed both configurations, including the real D3D12 VTEX/residency/fence path.
Diff/header/residue/process/dependency/scope hygiene is clean. The final independent CURRENT-tree
P5/P6 closure review reported **0 blocker / 0 high / 0 medium**, confirming the P5 matrix, P6
evidence, rendering test-only fixes, durable-record consistency and no E2.5/E2.6 implementation
leakage. E2.4 is closed.

### E2.5 - local cache lifecycle and safe generation reclamation

**Status:** E2.5 P0-P5 are closed. P5's final independent exact-CURRENT review
returned **0 BLOCKER / 0 HIGH / 0 MEDIUM**. The first P4 closure was later reopened by a
valid HIGH finding: no production owner connected exact renderer retirement
evidence to `ProjectAssetPipeline`. Editor `ProjectWorkspaceService` now owns the
single production texture-domain pipeline and the bounded renderer-to-project
handoff. `Busy` preserves the renderer record, while `Accepted` and `NotTracked`
acknowledge the exact retirement id. Renderer quiesce finalizes the evidence before
dependent drain, and renderer drain fails closed if delivery remains incomplete.
`editorAssetRetirementTests` plus the renderer shutdown regression pass in Debug
and Development. The user-authorized exact-CURRENT P4 self-review returned **0
BLOCKER / 0 HIGH / 0 MEDIUM**. P5's deterministic matrix has fresh
Debug/Development runtime coverage across the focused assets/assetStreaming/
streaming targets and is closed. P6 is authorized and its fresh generation,
Debug/Development compile/link, seven-executable focused runtime matrix and
hygiene/scope checks are complete. `geometryAllocatorTests` builds and executes
in both configurations but is not claimed green because it reproduces only the
previously classified unrelated `20/15/9/9/0` versus `19/14/8/8/0` GPU-scene
aggregate oracle. The final docs-inclusive independent exact-CURRENT P6 review
returned **0 BLOCKER / 0 HIGH / 0 MEDIUM**, so **P6 and E2.5 are closed**.
At the E2.5 closure checkpoint E2.6 implementation had not started and was
unauthorized. Definition-only work was subsequently authorized; current status
is recorded in the E2.6 section below. The detailed E2.5 execution contract is
[project-asset-e2-5-plan.md](project-asset-e2-5-plan.md).

Complete two separate project-local storage lifecycles: immutable VDDC records
under the existing VDDC/VADI owners, and superseded materialized `.vres`
generations deliberately retained by E2.4. Cover restart restoration, cache-hit
origin, corruption/missing-entry repair, bounded VDDC budget maintenance and
loose-generation reclamation after streamer/resource/resident retirement.
Cleanup may delete only data proven unreachable from every owner in its domain.
It must never delete authored sources, sidecars, transaction recovery state,
current loose delivery or the only committed artifact needed for restoration.

If the existing cache API lacks a required ownership query or prune operation,
add the smallest general capability to its current owner. Do not duplicate cache
metadata inside `ProjectAssetPipeline`.

**Exit gate:** executed tests prove cold build then restart cache hit without
compiler execution, missing/corrupt entry repair through the existing scheduler,
root-shared VDDC reader safety, bounded VDDC pruning, and eventual old `.vres`
reclamation only after streaming/resource/resident retirement. Also prove
interrupted-maintenance restart, strict owned-stage cleanup and project
relocation within the established portable roots.

### E2.6 - headless surface, lifecycle and validation closure

**Status:** E2.5 is closed. The user explicitly authorized **E2.6 definition
work only** on 2026-09-18. A first review was 0/0/0, then a stricter independent
exact-CURRENT audit found **0 BLOCKER / 0 HIGH / 1 MEDIUM** because the proposed
freshness query did not explicitly share IncrementalRecooker's committed
Generated-dependency materialization before VADI Evaluate. The
[E2.6 contract](project-asset-e2-6-plan.md) is corrected and the final strict
exact-CURRENT re-review returned **0 BLOCKER / 0 HIGH / 0 MEDIUM**. The user
then authorized **P0 only**. P0 performed the exact-CURRENT read-only
architecture/task-plan lock, corrected an independent **0 BLOCKER / 3 HIGH /
2 MEDIUM** preflight, and received final independent **0 BLOCKER / 0 HIGH /
0 MEDIUM** acceptance. The user then separately authorized **P1 only**. P1
implemented the neutral config/identity helper, editor migration and private
GPU-free nanovanguard bootstrap. Focused Debug and Development generation,
compile/link and executable validation completed, and final independent
exact-CURRENT review returned **0 BLOCKER / 0 HIGH / 0 MEDIUM**. **The
definition, P0 and P1 are closed.** The user then separately authorized P2 with
`start P2` and later directed `close P2`. P2 added the one shared
Generated-dependency preparation seam, public committed-VADI freshness query and
complete accepted-asset explicit reimport through the existing automatic
coordinator/recooker. Its initial implementation preflight was **0 BLOCKER / 2
HIGH / 1 MEDIUM** and all findings were corrected. Fresh Premake generation,
serialized Debug/Development compile/link for the focused eight-target surface,
actual execution of the five applicable focused tests in both configurations,
and final scope/dependency/hygiene checks passed. A late publication-disable
audit led to a rejected over-broad `HasPublishableDelivery()` guard; exact-current
regressions proved the established E2.4 lifecycle instead retains committed
delivery across policy disable. The new publication-specific fence blocks true
in-flight `publicationSettlementPending`, explicit activity independently blocks
policy transitions, and the pre-existing Busy gates for owned automatic
admission, observation reconciliation, VDDC maintenance and loose-retirement
admission remain unchanged. Final independent exact-CURRENT P2 closure
review returned **0 BLOCKER / 0 HIGH / 0 MEDIUM**. P3 then implemented only the
bounded `status`/`rescan`/`build`/`reimport`/`watch` nanovanguard
surface plus the shared semantic protocol and current-session cancellation seam.
Fresh final validation passed in Debug and Development, production smoke and
scope/hygiene; the final independent exact-CURRENT P3 closure review returned
**0 BLOCKER / 0 HIGH / 0 MEDIUM**. **P3 is closed.** The user subsequently
authorized **P4 only**. P4 maintenance/mutation/lifecycle implementation,
focused Debug/Development execution, production CLI smoke, dependency/scope /
hygiene audit and final independent reviews are complete at **0 BLOCKER / 0 HIGH
/ 0 MEDIUM**. The user subsequently changed the validation policy so comprehensive
Premake/build/test/native execution is batched into P6 rather than repeated at
each intermediate gate; independent exact-CURRENT review remains mandatory.
Accordingly **P4 is closed**. The historical unrelated world-session source-audit
failure was retained as deferred P6 validation context. The user subsequently
authorized **P5 only**. P5 adds test-only deterministic production-texture
cold/restart/dependency/active-owner/protocol/containment proof and composes it
with the established lower-owner E2 suites; final independent exact-CURRENT review
returned **0 BLOCKER / 0 HIGH / 0 MEDIUM**, so **P5 is closed** under the updated
policy. No P5 build/runtime pass is claimed; one Debug test-target build attempt
stopped on two new test-source diagnostics that were corrected without a rerun.
At that historical P5 checkpoint broad compile/link/execution remained P6 and P6
was unstarted/separately unauthorized. The user subsequently authorized P6; the
fresh final validation and independent **0 BLOCKER / 0 HIGH / 0 MEDIUM** review
recorded above close P6, E2.6 and complete E2.

Expose the completed E2 operations and observations through the shared headless
surface: observation status, pending reconciliation, manual rescan/reimport,
build state/reasons, cancellation and bounded cache maintenance. Frontends call
the same `ProjectAssetPipeline` behavior; this subphase adds no editor widgets or
editor-only backend.

Close initialization, restart and shutdown ordering across observer, pipeline,
recooker, VADI/VDDC, publisher and streamer. Run the focused production and test
targets with Premake-generated projects, exercise restart and event-storm cases,
inspect the scoped diff, and record all unexercised fault-injection boundaries.

**Exit gate:** the phase-level E2 workflow succeeds end to end: externally add a
supported asset, classify and build it, restart and hit DDC, change it and rebuild
affected outputs, move it without losing identity, and delete it with explicit
dependent state. Failed or stale work preserves the last valid artifact, cache
maintenance respects every owner, and editor/headless consumers require no
parallel backend.

The dedicated E2.6 contract freezes direct frontend composition of the existing
ProjectAssetPipeline rather than a new shared session service: editor retains
ProjectWorkspaceService ownership, while nanovanguard uses a private
command-scoped GPU-free composition of the existing lower resource/streaming
owners. It also defines the two narrow shared-surface gaps to close before CLI
exposure: per-output VADI build-freshness reasons and explicit accepted-asset
reimport through existing recook/build authority.

## Agent execution and handoff protocol

1. Read `AGENTS.md`, this plan, the E2 roadmap section, the current progress
   ledger, and the VMETA/VTXN format contracts before changing source.
2. Work only on the first non-complete subphase. Later subphases may be inspected
   for interface compatibility but are not implementation scope.
3. Inspect current APIs before designing. Names in this plan identify ownership
   and behavior, not permission to invent an interface that source already owns.
4. Keep a scoped planning-with-files directory when using that workflow. Its
   `task_plan.md` is the active execution contract; `findings.md` holds evidence;
   `progress.md` records exact commands and outcomes.
5. Preserve the dirty shared worktree. Do not reset, clean, broadly format or
   modify rendering/shader code for E2 asset work.
6. Use Premake, focused generated projects and serialized outer MSBuild `/m:1`.
   Never substitute CMake and never turn source presence into a build/test claim.
7. At closure, update the subphase status here and add a concise implementation
   and verification entry to `docs/development/progress/project-asset-foundation-progress.md`. Record
   deferred or fault-injection-only coverage explicitly.
8. Stop at the subphase boundary for review. A review may close the subphase;
   only then does the next numbered section become the active task.
