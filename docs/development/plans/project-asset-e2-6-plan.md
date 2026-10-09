# E2.6 - headless surface, lifecycle and E2 validation closure

## Status and authorization

The user explicitly authorized **E2.6 definition work only** on 2026-09-18,
after E2.5 P0-P6 closed at independent exact-CURRENT **0 BLOCKER / 0 HIGH /
0 MEDIUM**.

This document is the durable E2.6 cross-agent definition. A first independent
review returned **0 BLOCKER / 0 HIGH / 0 MEDIUM**, but a stricter second
exact-CURRENT audit found **0 BLOCKER / 0 HIGH / 1 MEDIUM** in the generated-
dependency semantics of the proposed build-freshness query. The contract below
was corrected, and the final strict exact-CURRENT re-review returned **0
BLOCKER / 0 HIGH / 0 MEDIUM**. **The E2.6 definition is closed. The user
subsequently authorized P0 only with `start P0`. P0 completed as a read-only
exact-CURRENT architecture/task-plan lock; after correcting a 0 BLOCKER / 3 HIGH
/ 2 MEDIUM preflight, the final strict independent P0 closure review returned
0 BLOCKER / 0 HIGH / 0 MEDIUM. P0 is closed. The user then separately
authorized P1 with `start P1`. P1 implemented the neutral configuration
helper, editor migration and private GPU-free nanovanguard bootstrap, passed
focused Debug and Development validation, and final independent exact-CURRENT
closure review returned 0 BLOCKER / 0 HIGH / 0 MEDIUM. P1 is closed. The user
then separately authorized P2 with `start P2` and later directed `close P2`.
P2 implemented the shared ProjectAssetPipeline surface below, passed fresh
Premake plus focused Debug/Development compile, link and executable validation,
and final independent exact-CURRENT closure review returned **0 BLOCKER / 0 HIGH
/ 0 MEDIUM**. P2 is closed. The user then authorized P3 with `start Next P` and
later directed `close it`. P3 implemented the bounded nanovanguard command and
protocol surface below, passed fresh Premake plus exact-current Debug and
Development build/executable validation, production CLI smoke and dependency /
scope / hygiene audit, and final independent exact-CURRENT closure review
returned **0 BLOCKER / 0 HIGH / 0 MEDIUM**. **P3 is closed.** The user then
explicitly authorized **P4 only** with `start P4`. P4 implementation, focused
Debug/Development execution, rebuilt production CLI smoke, scope/hygiene audit,
and final independent exact-CURRENT reviews are complete at **0 BLOCKER / 0 HIGH
/ 0 MEDIUM**. The user subsequently changed the validation policy for remaining
intermediate gates: coherent implementation plus independent exact-CURRENT review
closes an intermediate P, while broad Premake/build/test/native validation is
batched into P6. Under that policy **P4 is closed**. A historical fresh Premake
attempt was blocked by an unrelated modified
`source/engine/src/world_session_service.cpp` source-audit violation; that is
historical P4 evidence that was deferred to P6, not a P4 closure blocker. Fresh
P6 generation now passes the engine-service audit.
The user subsequently authorized **P5 only** with `START`. P5's deterministic
matrix implementation is now source-complete and final independent exact-CURRENT
review returned **0 BLOCKER / 0 HIGH / 0 MEDIUM**. Under the updated
intermediate-gate policy **P5 is closed**. The user subsequently authorized
**P6** with `okay start P6`. Fresh generation, Debug/Development compile/link,
owner-suite execution, the complete headless matrix, production CLI scratch
execution and final dependency/scope/hygiene validation are complete. The fresh
exact-CURRENT independent P6 closure review returned **0 BLOCKER / 0 HIGH /
0 MEDIUM**. **P6, E2.6 and the complete E2 phase are closed.**

The active definition/execution ledger is
.planning/e2-6-headless-lifecycle-closure/{task_plan,findings,progress}.md.

## Purpose

E2.6 closes the E2 project-asset foundation by exposing the behavior already
owned by ProjectAssetPipeline to a real GPU-free headless frontend, filling the
small shared query/reimport gaps that frontend exposure proves necessary, then
validating startup, restart, active-work cancellation, shutdown and the complete
E2 workflow.

E2.6 is a frontend/composition and closure phase. It must not create a new
asset backend.

## Mandatory read-first set

Before P0 implementation work, read:

1. root AGENTS.md;
2. project-asset-e2-plan.md;
3. project-asset-e2-3-plan.md;
4. project-asset-e2-4-plan.md;
5. project-asset-e2-5-plan.md;
6. project-asset-foundation-progress.md;
7. editor-foundation-study-and-roadmap.md;
8. ProjectAssetPipeline header, implementation and tests;
9. editor ProjectWorkspaceService header/implementation and editor Premake;
10. nanovanguard README, command registry, project commands, Windows entry point,
    tests and Premake;
11. assets ProjectAssetBuildSystem, DependencyIndex/VADI and IncrementalRecooker;
12. resources/streaming initialization and shutdown code/tests; and
13. VMETA, VPAS, VADI, VDDC and VTXN contracts.

P0 must also refresh exact RED source evidence before production edits. The
definition-time RED notes below are inherited from the exact evidence already
recorded by E2.5 because the reference checkout is not reachable through the
current tool-visible roots.

## Definition-time exact-CURRENT facts

### ProjectAssetPipeline is already the shared backend

ProjectAssetPipeline is explicitly documented as the shared owner-thread
project cooker/publication lifecycle consumed by editor and headless frontends.
It already exposes:

- source observation enable/disable/tick/status;
- accepted asset/change/diagnostic/invalidation state;
- automatic-recook enable/disable/retry/cancel/tick/status;
- manual output Request/Poll/Wait/Cancel;
- committed/published/resident delivery status;
- authoritative Rescan and Reconcile;
- E1F move, generated-identity duplicate, retarget, delete and session-local
  undo/redo;
- VDDC maintenance request/retry/cancel/tick/status/diagnostics; and
- loose-generation maintenance request/retry/cancel/tick/status/diagnostics.

E2.6 therefore does **not** add a new shared session service or another
project-asset owner. Each frontend composes one ProjectAssetPipeline instance
for its own process/session and calls the same public behavior.

### Editor ownership stays in ProjectWorkspaceService

The editor currently owns SourceDatabase, TextureAssetCompiler and
ProjectAssetPipeline inside ManagedProjectWorkspaceService. That service also
requires RenderingService because it owns the exact texture-generation
retirement bridge established by E2.5.

That ownership remains valid for editor. E2.6 does not move the pipeline out of
ProjectWorkspaceService merely to make a CLI.

The editor currently constructs project-asset configuration locally:

- DerivedData/ProjectAssets/VDDC;
- DerivedData/ProjectAssets/Index;
- Intermediate/ProjectAssets/Loose;
- Intermediate/ProjectAssets/operations.vtxn;
- TargetPlatform::WindowsD3D12; and
- SHA-256 of the exact identity bytes
  "vanguard.editor.project-assets.texture.v1".

Headless composition must derive the same roots, target and settings fingerprint
for the same project/tool configuration. Refactoring that derivation must not
silently change existing build keys.

### Headless must remain GPU-free at link and runtime boundaries

Using EngineHost Headless would avoid selecting RenderingService at runtime,
but the current monolithic engine Premake target itself links rendering,
pipelineCache and rhi. Pulling engine into nanovanguard would therefore drag an
unnecessary rendering/RHI dependency surface into the headless tool.

The current assetStreamingTests already prove the required lower-level direct
composition without GPU/rendering:

memory/diagnostics/containers/io/reflection/filesystem/jobs
-> ResourceRegistry
-> ResourcePipeline
-> ResourceStreamer
-> SourceDatabase + compiler/importer
-> ProjectAssetPipeline

and shutdown in reverse ownership order.

E2.6 headless commands reuse those existing concrete owners directly. A private
nanovanguard RAII/context helper may own that command-scoped composition, but it
is frontend code, not a shared backend/service and not a new registry/scheduler
implementation.

### Current nanovanguard has two concrete closure gaps

1. RegisterProjectCommands registers sources inspect under a sources parent that
   RegisterBuiltinCommands never registers. CommandRegistry rejects children
   whose parent is missing. E2.6 must repair normal registration; it must not
   special-case dispatch.
2. Current filesystem-backed nanovanguard tests create a working-directory
   fixture. E2.6 test changes must use tests/support/scratch_directory.hpp and
   leave no repository-root residue.

### The shared pipeline surface has two definition-time gaps

The parent E2 contract requires build state/reasons and manual reimport.

Current public pipeline state is insufficient for two exact frontend needs:

- delivery status does not expose the per-output VADI build-freshness reason
  such as Missing, SourceChanged, CompilerChanged or DependenciesChanged; and
- there is no explicit asset/source reimport command. RetryAutomaticRecook only
  retries a terminal automatic coordinator; it is not arbitrary reimport.

E2.6 may add only the smallest shared ProjectAssetPipeline capabilities needed:

1. a **read-only per-output build freshness query** whose implementation uses
   the exact same preparation semantics as authoritative recook planning:
   resolve the canonical current request, prepare its BuildPlan, materialize
   every non-soft Generated dependency's committed content fingerprint from the
   current DependencyIndex/VADI using the same shared helper/logic as
   IncrementalRecooker, then call DependencyIndex::Evaluate. The query and
   IncrementalRecooker must share one implementation of that generated-
   dependency materialization rule rather than copy it into frontend/query code.
   Frontend code must not reach into VADI directly;
2. an **explicit reimport request for a current accepted asset/output set** that
   seeds only the existing recook/build coordinator and dependency authority.
   It must not create a second dirty queue, fake a source mutation or bypass
   VDDC.

P0 freezes exact API names/types and whether multi-output reimport is one
asset-level operation or a bounded sequence owned by ProjectAssetPipeline.

### Session-local state remains session-local

Observation queues, active requests, maintenance operations, cancellation flags
and E1F undo/redo history are process/session-local.

A new one-shot nanovanguard process cannot truthfully observe or cancel work
owned by an earlier process without a daemon/IPC system. E2.6 adds no daemon or
IPC.

Live status/cancellation therefore belongs to the current foreground command or
long-running watch/sync session. Ctrl+C or an equivalent caller cancellation
signal requests cancellation through existing ProjectAssetPipeline APIs.

## RED definition-time evidence and intentional differences

The E2.5 evidence records that RED:

- keeps cooked cache access behind its dataCache owner;
- keeps dependency facts behind dependencyCache; and
- exposes explicit cache cleanup through a commandlet rather than making the
  command frontend the cache owner.

E2.6 adopts that ownership separation: nanovanguard asks the existing Vanguard
owners to perform work and serializes their result.

Vanguard deliberately retains its local immutable VDDC, committed VADI,
ProjectAssetPipeline, bounded command registry and explicit project roots. It
does not copy RED cache-server/PostgreSQL protocols, GET_LATEST policy or a
global commandlet framework.

P0 must refresh exact RED source paths/lifecycle before any production edit.

## Locked ownership and dependency model

### No new shared project-asset service

There is no E2.6 ProjectAssetSession service and no second shared lifecycle
owner.

- Editor continues to own its SourceDatabase + ProjectAssetPipeline in
  ProjectWorkspaceService.
- nanovanguard owns its own command-scoped SourceDatabase +
  ProjectAssetPipeline instance.
- Both call the same ProjectAssetPipeline behavior and share only neutral
  configuration/identity helpers and public query/reimport capabilities.

### Frontend-neutral configuration helper is allowed

E2.6 may add a pure helper in an existing lower tooling layer to derive the
canonical project-asset configuration from neutral absolute roots, target and
tool-settings identity.

It owns no state and performs no I/O/build/publication.

For the current production texture composition:

- windows-x64 maps to TargetPlatform::WindowsD3D12;
- the existing settings identity bytes
  "vanguard.editor.project-assets.texture.v1" remain byte-for-byte unchanged
  until an actual semantic/version change is authorized; and
- editor/headless must compare equal for VDDC, Index, Loose and VTXN roots and
  resulting settings fingerprint.

P0 must freeze the exact helper location/API and fail-closed handling of
unsupported or ambiguous project targets. Do not make projects depend on assets.

### Headless composition is private and direct

Asset-owning nanovanguard commands may use one private command-scoped context
which directly owns the already-existing lower-level objects required by
ProjectAssetPipeline.

It may initialize exactly the global/runtime facilities required by those
owners, then own one:

- ResourceRegistry;
- ResourcePipeline;
- ResourceStreamer;
- SourceDatabase;
- production TextureAssetCompiler/importer set used by the current editor; and
- ProjectAssetPipeline.

This does not create parallel engine abstractions; it instantiates the existing
classes once for the tool process.

The headless target must not link engine, rendering, rhi, pipelineCache, window,
input, world or editor solely for E2.6 commands.

### Callback/lifetime ordering

Compiler/importer callback state outlives ProjectAssetPipeline.
ResourceStreamer outlives ProjectAssetPipeline.
ResourcePipeline/Registry outlive ResourceStreamer.
Jobs/filesystem/I/O outlive every owner that borrows them.

Shutdown is the exact reverse of successful acquisition, with
ProjectAssetPipeline::Shutdown as the authoritative observer/recook/maintenance
drain boundary.

Editor retains its existing rendering-retirement pump and dependency. No
rendering type crosses into assetStreaming.

## Headless command/protocol contract

### Required command capabilities

P0 freezes exact spellings, but the bounded nanovanguard asset domain must cover:

- status snapshot;
- authoritative rescan;
- manual build of one selected current output;
- explicit reimport of one current accepted asset/output set;
- long-running watch/sync with observation + automatic recook;
- VDDC maintenance;
- loose-generation maintenance;
- move;
- generated-identity duplicate;
- retarget; and
- delete with explicit dependent/replacement policy.

sources inspect remains a lower read-only inventory command and receives its
missing parent registration.

No one-shot persistent undo/redo or cancel-old-process command is exposed.

asset status is **lifecycle-aware**, not a promise of zero filesystem activity:
constructing ProjectAssetPipeline may perform its already-owned VTXN recovery,
VPAS/VADI restoration and loose-publication reconciliation before the status
snapshot exists. The command must report committed cleanup/reconciliation
pending honestly. It must not add any new authored-source mutation beyond the
existing recovery contract. Callers that need inventory-only inspection use
sources inspect.

### Asset selection

Frontend asset lookup should use project-root-relative source identity/path plus
optional output key, then use accepted ProjectAssetState to resolve the canonical
AssetId/output reference.

Do not make users provide guessed internal ids or parse VMETA independently in
the CLI. Missing/conflicting/ambiguous accepted state fails closed.

P0 freezes any canonical AssetId/resource string representation emitted in
JSONL.

### Build state and reasons

The headless status model must distinguish:

- authored/accepted generation and source issues;
- per-output VADI build freshness + dirty reason;
- automatic-recook state and dirty reasons;
- committed/published/resident delivery state;
- observation status/counters;
- VDDC maintenance status; and
- loose-maintenance status.

The CLI consumes public ProjectAssetPipeline snapshots/queries only.

### Reimport

Reimport is not "delete cache" and not "force compiler ignoring VDDC".

It selects a current accepted asset, resolves its current build requests, and
submits them through the existing recook/build/VADI authority. Exact cache hits
remain valid.

No frontend-owned dirty queue or artificial SourceChanged event is allowed.

### Current-session cancellation

For the current process:

- manual request -> ProjectAssetRequest::Cancel;
- automatic work -> CancelAutomaticRecook;
- VDDC maintenance -> CancelVddcMaintenance;
- loose maintenance -> CancelLooseMaintenance;
- observation -> DisableObservation/ProjectAssetPipeline shutdown ownership.

The platform console handler may only publish a small cancellation flag/event.
The owner thread observes it and calls engine APIs. The signal handler itself
must not call ProjectAssetPipeline or destroy engine state.

### Stable semantic event model

Do not have each command hand-build unrelated human/JSONL semantics.

P3 must define one bounded semantic result/event model rendered to either human
or JSONL.

For JSONL:

- schema remains 1 unless P0 proves a compatibility reason to bump;
- every line includes schema, event and operation;
- enum/status values use stable string tokens rather than implementation
  ordinals;
- field order is deterministic;
- diagnostics are bounded and explicitly report truncation;
- stdout is reserved for the command protocol; and
- one successfully parsed asset operation produces exactly one terminal
  completed, failed or cancelled event after any progress records.

Current exit codes remain:

- 0 Success;
- 2 UsageError;
- 10 InvalidProject;
- 20 OperationFailed;
- 30 Cancelled;
- 70 InternalFailure.

P0/P3 freeze exact mapping from ProjectAssetPipeline/build/maintenance results
to those process codes. Rich state remains in JSONL rather than multiplying
process exit codes.

Malformed invocation behavior with --format=jsonl must also be decided
explicitly; current parser chooses output format only after parsing and can emit
human usage errors first. P3 must make the protocol deterministic rather than
claiming all errors are JSONL when they are not. Parser-level usage failure may
use a distinct usage-error event/rule because no asset operation has started.

## Maintenance contract

VDDC and loose maintenance remain separate owner domains.

If a convenience "all" command is exposed, it runs them **sequentially** under
the existing admission rules; it must not start both concurrently.

Terminal mapping must distinguish:

- Complete/satisfied -> success;
- Cancelled -> exit 30;
- Failed/LimitExceeded -> operation failure;
- Deferred/Busy/retry-required -> explicit non-success unless the user selected
  a separately documented best-effort/report-only mode.

The CLI never chooses VDDC victims and never calls physical loose deletion.

## Initialization, restart and shutdown

### Headless startup

For an asset command:

1. resolve and validate the .vproject/workspace using existing project APIs;
2. derive the canonical project-asset target/configuration;
3. initialize required process facilities in the exact order frozen by P0;
4. initialize ResourceRegistry -> ResourcePipeline -> ResourceStreamer;
5. initialize the supported production compiler/importer callback owner;
6. establish the source root/inventory;
7. initialize ProjectAssetPipeline, allowing its existing VTXN recovery, VPAS
   accepted state, VADI/VDDC, publisher and managed-loose-root startup;
8. report restore/committed-pending state honestly; and only then
9. enable command-specific observation/automatic recook.

Document-only nanovanguard commands keep their current lightweight path.

### Restart

Restart reconstructs truth from authored source/VMETA, VPAS historical accepted
state, committed VADI, exact VDDC, canonical loose delivery and VTXN recovery.

It does not replay observation queues, active requests, cancellation flags,
automatic transient queues or E1F undo history.

Warm restart proof must distinguish PersistentCacheHit from MemoryCacheHit and
CompilerBuilt and prove zero compiler calls for the exact persistent hit.

### Headless shutdown

1. stop accepting a new tool cancellation request;
2. stop observation producers/current foreground activity;
3. call ProjectAssetPipeline::Shutdown, which owns cancellation/join/settlement
   of observation, automatic work and maintenance;
4. only after successful pipeline shutdown, shut down the compiler/tool callback
   owner;
5. shut down ResourceStreamer;
6. shut down ResourcePipeline;
7. shut down ResourceRegistry; then
8. shut down Jobs/filesystem/I/O/other process facilities in their established
   reverse order.

If ProjectAssetPipeline shutdown returns Busy/failure, callback/streamer owners
must remain alive and the tool reports failure rather than destructing borrowed
state underneath the pipeline.

Editor continues using EngineHost reverse lifecycle and its existing renderer
retirement pump.

## Explicit non-goals

E2.6 does not add:

- a second project-asset session/backend/service;
- another source registry, VADI, DDC, graph, recooker or scheduler;
- another implementation of ResourceRegistry/ResourcePipeline/ResourceStreamer;
- engine/rendering/RHI linkage to nanovanguard merely for asset commands;
- editor UI/panels/menus/asset browser;
- remote build/cache server, daemon or IPC;
- cross-process cancellation/status;
- persistent undo/redo;
- generic force-compile/cache-bypass;
- dynamic native importer/plugin discovery;
- broad packaging/cook-all policy;
- generic *.tmp cleanup or foreign-file deletion; or
- assetStreaming -> rendering/editor dependencies.

## Ordered implementation gates

### P0 - definition lock and exact-CURRENT architecture preflight

Read-only; no production edits/builds/tests by default.

Freeze:

- exact RED commandlet/cache ownership evidence;
- pure shared config/target/settings identity helper;
- exact GPU-free headless acquisition/shutdown order and required libraries;
- exact per-output build-freshness query contract, including one shared
  Generated-dependency materialization path with IncrementalRecooker before
  DependencyIndex::Evaluate;
- exact reimport contract and whether multi-output aggregation needs a narrow
  pipeline API;
- command vocabulary and asset locator grammar;
- semantic event model, JSONL tokens/ordering and exit mapping;
- current-session cancellation/signal behavior;
- maintenance terminal semantics;
- exact files/targets/tests for P1-P6; and
- fault-injection/source-review boundaries.

Refresh the planning triplet. Obtain fresh independent exact-CURRENT
**0 BLOCKER / 0 HIGH / 0 MEDIUM** review. Stop before P1.

#### P0 exact-CURRENT architecture lock - 2026-09-18

The user authorized **P0 only** with `start P0`. The decisions below are the
read-only implementation contract for later numbered gates; they do not authorize
P1 or any production edit.

**Neutral identity/configuration helper**

- P1 adds a pure public helper in assetStreaming,
  `include/vanguard/asset_streaming/project_asset_configuration.hpp` plus
  `src/project_asset_configuration.cpp`.
- The helper accepts already-resolved `DerivedData` and `Intermediate` roots and
  exactly one already-selected target token. It does not accept
  `projects::ProjectDescriptor`/`ProjectWorkspace`, perform I/O, or own state, so
  no `assetStreaming -> projects` dependency is introduced.
- The only E2.6 target mapping is exact `windows-x64` ->
  `assets::TargetPlatform::WindowsD3D12`; unsupported selected tokens fail closed
  and leave the caller's output unchanged.
- It derives exactly the CURRENT editor identity:
  `DerivedData/ProjectAssets/VDDC`, `DerivedData/ProjectAssets/Index`,
  `Intermediate/ProjectAssets/Loose`,
  `Intermediate/ProjectAssets/operations.vtxn`, and the SHA-256 fingerprint of
  the exact bytes `vanguard.editor.project-assets.texture.v1`. A frontend-neutral
  refactor is not a settings-version change.
- Editor continues to select/pass `windows-x64` unconditionally, preserving its
  current behavior even if the project descriptor lists other targets.
  Nanovanguard owns project-target selection: explicit `--target` must be
  declared by the loaded project; without `--target`, exactly one declared target
  is required. Multiple declared targets are ambiguous and fail closed. The
  selected token then goes through the same pure helper.

**GPU-free private nanovanguard context and lifecycle**

- P1 adds private
  `tools/nanovanguard/private/vanguard/nanovanguard/project_asset_context.hpp`
  and `tools/nanovanguard/src/project_asset_context.cpp`. It is command-scoped,
  not a new service/session/backend.
- `main_windows.cpp` remains the process owner of memory and containers. The
  asset context acquires only facilities it needs and does not already inherit:
  diagnostics -> I/O -> reflection initialization -> filesystem -> jobs. Current
  reflection has initialization but no shutdown API, so it is process-lifetime
  once initialized. `texture_tools::Initialize()` is likewise idempotent and has
  no shutdown API.
- After project/workspace/config resolution, command-owned acquisition is:
  `TextureAssetCompiler` -> initial `SourceDatabase::Rescan` ->
  `ResourceRegistry` -> `ResourcePipeline` -> `ResourceStreamer` ->
  `ProjectAssetPipeline`. Compiler/importer callback storage and the streamer
  therefore outlive the project pipeline.
- Rollback and normal close reverse only successfully acquired stages:
  `ProjectAssetPipeline` -> compiler -> streamer -> resource pipeline -> registry
  -> jobs -> filesystem -> I/O -> diagnostics. Process-lifetime reflection and
  texture-tools registration are not fictitiously shut down.
- `ProjectAssetPipeline::Initialize` may return
  `CommittedReconciliationPending` after becoming live. Headless treats any
  result with `pipeline.IsInitialized()==true` as an acquired pipeline and must
  call `Shutdown()` before releasing compiler/streamer owners. If shutdown still
  fails/returns Busy, the private context is retained through process exit and
  the command returns lifecycle/internal failure; borrowed lower owners must not
  be stack-destroyed underneath the live pipeline.
- Concretely the private context uses a heap-backed implementation with explicit
  `Close()`. Normal destruction releases only a successfully closed impl. If the
  final bounded drain/Shutdown cannot safely close a live pipeline, ownership of
  that impl is intentionally retained for the remaining one-shot process
  lifetime so C++ member destruction cannot invalidate borrowed callbacks or
  resource owners before OS teardown.
- CURRENT editor has a related lifecycle defect: its failed-initialize branch
  shuts the texture compiler down immediately even when a
  `CommittedReconciliationPending` result leaves the pipeline live. P0 records
  this exact-CURRENT defect but does not edit it. P4 lifecycle/restart work owns
  the narrow editor correction/regression; P1 changes editor only to consume the
  neutral configuration helper and otherwise preserves current behavior.

**Direct GPU-free link surface**

- Nanovanguard must use the direct lower-layer closure already proven by
  `assetStreamingTests`, `streamingTests` and `textureToolsTests`, rather than
  linking monolithic `engine`.
- The direct closure to freeze in P1 is the union of the CURRENT proven test
  surfaces: `assetStreaming`, `streaming`, `textureTools`, `assets`, `textures`,
  `packages`, `schemas`, `reflection`, `resources`, `crypto`, `serialization`,
  `filesystem`, `io`, `jobs`, `diagnostics`, `concurrency`, `containers`,
  `memory`, `system`, and existing `projects`; compatibility links
  `packagesCompat`, `serializationCompat`, `filesystemCompat`,
  `redFileSystemCompat`, `redCompressionCompat`,
  `redCompressionThirdPartyCompat`, `redCoreCompat`, `redIOCompat`,
  `diagnosticsCompat`, `containersCompat`, `concurrencyCompat`, `jobsCompat`,
  `redJobsCompat`, `redContainersCompat`, `redMathCompat`, and
  `redSystemCompat`; texture/image links `bcCodecs`, `compressonatorCore`,
  `libtiff`, `openexrCore`, `openjph`, `libjpegTurbo`, `libpng`, and `zlib`;
  `nvToolsExt64_1`; configuration-appropriate Oodle; non-Shipping FileSync/udt;
  and the already-required Windows libraries `Bcrypt`, `Advapi32`, `Dbghelp`,
  `Psapi`, `Shlwapi`, `Shell32`, `Version`, and `ws2_32`. P1 may remove a member
  only when the generated direct-link proof shows it is genuinely unnecessary;
  it may not substitute a broader engine/GPU aggregate target.
- P1's dependency audit must show no new `engine`, `rendering`, `rhi`,
  `pipelineCache`, `window`, `input`, `world`, `gameWorld` or `editor` link or
  include dependency for headless asset commands.

**One authoritative Generated-dependency preparation seam**

- CURRENT source has two related implementations: IncrementalRecooker tolerates
  a missing required Generated VADI row so `DependencyIndex::Evaluate` can mark
  the parent dirty, while pipeline pre-commit/delivery validation is stricter and
  rejects a missing required row. P2 must preserve both policies while removing
  duplicated materialization logic.
- The shared owner becomes `DependencyIndex`: add an IndexResult-returning
  `MaterializeGeneratedDependencyContent(BuildPlan&, preferredRecords={})`
  operation in `asset_index.hpp/.cpp`. Preferred transaction records win over
  committed rows; otherwise committed `Find` is used. Soft Generated dependencies
  are skipped, missing Optional Generated dependencies are ignored, and missing
  Required Generated dependencies are left without fabricated content while the
  function continues materializing all available rows and finally returns
  `IndexResult::NotFound`. Hard index/allocation/limit failures retain their exact
  result.
- IncrementalRecooker and the public freshness query accept `Success` or
  `NotFound`, then call `DependencyIndex::Evaluate`; this preserves CURRENT
  recooker dirty-reason semantics. Pipeline pre-commit and exact-delivery
  validators pass their staged preferred records where applicable and require
  strict `Success`, preserving their CURRENT stronger fence.
- The anonymous `MaterializeStoredGeneratedDependencies` in `asset_recooker.cpp`
  and `MaterializeFreshGeneratedDependencies` in `project_asset_pipeline.cpp`
  are replaced by this one VADI-owned implementation; frontend code never reads
  VADI directly.

**Public per-output freshness query**

- P2 adds a semantically read-only pipeline query for one typed output, returning
  a small `ProjectAssetBuildFreshness` value containing the output and exact
  `assets::DirtyReason`; caller output is unchanged on failure. Hard index
  preparation errors are represented in `ProjectAssetPipelineFailure` by an
  added `assets::IndexResult index` field instead of being collapsed to an enum
  ordinal or frontend guess.
- Sequence is canonical current Resolve -> `BuildSystem::Prepare` -> shared
  Generated materialization -> committed `DependencyIndex::Evaluate`.
- To keep the multi-step committed read coherent without adding a new snapshot
  generation mechanism, the query returns `Busy` while accepted/source truth is
  being reconciled or while work can publish/commit VADI (active observation
  reconcile, automatic admission, non-quiescent BuildGraph/manual request, or
  active VADI transaction). Merely having an already-committed unsaved index is
  not Busy. P2 regression locks this behavior. A later implementation may only
  relax Busy if it first supplies an equally coherent single-snapshot index
  operation; it may not return a mixed pre/post-commit DirtyReason.
- Invalid/untyped output is `InvalidArgument`; canonical current resolution or
  Prepare failure is `BuildRequestFailed` with existing exact build evidence;
  index materialization hard failure is `BuildRequestFailed` plus the exact
  `index` result. `DirtyReason::Missing` itself is a successful freshness result.

**Explicit reimport through the existing recooker owner**

- P2 adds one asset-level `RequestAssetReimport(AssetId, ...)` operation. It
  always selects the complete current accepted output set for that asset;
  P3 exposes no output-key narrowing for reimport. Selection and canonical
  request resolution are staged completely and bounded before owner state is
  changed, so a missing/conflicting/unsupported output mutates nothing.
- Successful admission returns `Success` after the complete force set is queued;
  the caller drives the already-existing `TickAutomaticRecook()` and
  `GetAutomaticRecookStatus()` surface to terminal. In the explicit-only
  (automatic policy disabled) case, that coordinator state belongs solely to
  this reimport and returns to Disabled on terminal completion, so no second
  request/status ledger is introduced. Cancellation uses the existing
  `CancelAutomaticRecook()` batch authority. When automatic policy is already
  enabled, the explicit force merges into the same bounded pending/next-batch
  state and terminal observation is the shared coordinator becoming quiescent;
  no per-reimport cross-process token is promised.
- Reimport reuses the existing `AutomaticRecookState` + one existing
  `IncrementalRecooker`; it must not add a second queue, graph, recooker or
  frontend scheduler. Generalize the existing repair-only forced-output metadata
  into bounded forced-output rows with reason flags at least
  `CacheRepair` and `ExplicitReimport`. Both reasons feed the same
  `forcedOutputs subset-of explicitOutputs` recooker path. Add
  `ExplicitReimport` to public recook dirty reasons/status rather than pretending
  the request is cache repair or `SourceChanged`.
- Explicit reimport is a manual use of that coordinator, not permission to sync
  unrelated project work. When automatic accepted-change policy is disabled, a
  reimport may temporarily drive the same coordinator without scheduling
  StartupRecovery and returns it to Disabled after its terminal state. When
  automatic policy is already enabled, reimport may merge transactionally into
  the bounded pending set for the next batch. This may require separating the
  automatic-policy bit from transient explicit coordinator activity; it does not
  create another work owner.
- Current-session recovery/retry preserves `ExplicitReimport` force markers from
  retained active/pending state. Cache-repair markers continue to be
  reconstructed from durable VADI/VDDC truth. Explicit reimport markers are
  transient and are not persisted/reconstructed after process restart.
- Exact VDDC/retained hits remain valid. Reimport never deletes VDDC, bypasses
  cache, fabricates a source mutation, or invokes compiler work merely to defeat
  an exact cache hit. Existing stale pre-commit validation, committed-delivery
  retention, cancellation and retry/newer-change recovery remain authoritative.

**Command vocabulary, target/asset locators and build selection**

- P3 registers the missing `sources` parent before current `sources inspect`,
  plus root `asset` with children `status`, `rescan`, `build`, `reimport`, and
  foreground `watch`. P4 adds `maintain`, `move`, `duplicate`, `retarget`, and
  `delete`. `asset maintain --domain vddc|loose|all` is one bounded maintenance
  command and avoids inventing another maintenance owner.
- Every command takes `<project>` first and accepts the common optional
  `--target <token>` selection described above. `rescan`, `watch`, and `maintain`
  are project-wide and take no asset source. Asset-selected commands use a
  lexical UTF-8 `<source>` relative to the project's Assets root, normalized to
  `/`; absolute paths and lexical `.`/`..` escape are rejected. Optional
  `--output <key>` is an exact VMETA output key and resolution uses accepted
  `ProjectAssetState` only. The CLI does not accept guessed AssetId/resource-id/
  VADI identifiers.
- `status <project> [source] [--output <key>]` reports project lifecycle/status
  always; with a source it adds that accepted asset and either the named output
  or all of its current outputs. `build <project> <source>` always means one
  current output: omitting `--output` is accepted only when the selected asset
  has exactly one current output. `reimport <project> <source>` intentionally has
  no `--output` and always means the full current accepted asset output set.
- Missing/non-Present/issue-bearing/conflicting accepted state, invalid key,
  unsupported target/type, or ambiguous source/output fails closed. JSONL may
  emit AssetId only as canonical 32 lowercase hex bytes (the same spelling used
  by `assets/<32hex>/<key>`) with output key in a separate field.

**Semantic protocol and exits**

- JSONL schema remains `1`. Every record starts in deterministic order with
  `schema`, `event`, `operation`; enum/status values use stable lower-kebab
  strings, diagnostics are bounded and report truncation, and stdout is reserved
  for protocol output.
- Stable event tokens are `status`, `progress`, `diagnostic`, `completed`,
  `failed`, `cancelled`, plus parser-only terminal `usage-error`. A successfully
  parsed asset operation emits zero or more nonterminal records and exactly one
  `completed`/`failed`/`cancelled` terminal.
- Stable operation tokens are `sources-inspect`, `asset-status`, `asset-rescan`,
  `asset-build`, `asset-reimport`, `asset-watch`, `asset-maintain`, `asset-move`,
  `asset-duplicate`, `asset-retarget`, and `asset-delete`. Maintenance domain is
  command data/state, not a second operation identity.
- Before structural command parsing, a bounded raw-argument prepass recognizes a
  syntactically valid `--format jsonl`/`--format=jsonl` only to select the usage
  error renderer. Duplicate/conflicting/invalid format options remain ordinary
  parser usage errors; without a valid preselection they render human output.
- Handler-side semantic command-usage validation after structural parsing emits
  the normal `failed` terminal while preserving exit `2`; only the structural
  parser emits terminal `usage-error`.
- Exit mapping is exact: `0` completed success; `2` parser/semantic usage error; `10`
  invalid project/document/workspace/declared-or-supported-target selection;
  `20` parsed locator/pipeline/build/maintenance/deferred/committed-pending
  operation failure; `30` current-session cancellation; `70` process/bootstrap/
  protocol/unsafe-shutdown internal failure.

**Current-session cancellation and maintenance**

- Windows uses `SetConsoleCtrlHandler` for Ctrl+C/Ctrl+Break. The callback does
  only an interlocked/atomic process-local cancellation publication. The owner
  loop observes it once and calls the currently owned API: manual request cancel,
  `CancelAutomaticRecook`, `CancelVddcMaintenance`, `CancelLooseMaintenance`, or
  observation/pipeline shutdown. No pipeline/resource owner is called from the
  console callback and no cross-process cancellation/status is implied.
- Foreground cancellation keeps bounded polling/draining until the owner reaches
  a terminal state before teardown.
- `maintain all` runs VDDC to terminal first and starts loose maintenance only on
  VDDC success. Complete with goal satisfied maps to `0`; Cancelled to `30`;
  Failed/LimitExceeded/scan/I/O failure and Deferred/StreamingBusy/
  ProtectionChanged/retry-required map to explicit `20`. There is no E2.6
  best-effort mode, no parallel maintenance, no CLI victim selection, and no
  frontend loose-file deletion.

**Exact later-gate files/targets and proof boundary**

- P1: the new shared configuration helper; editor config call site; private
  nanovanguard asset context; nanovanguard Premake; focused `assetStreamingTests`;
  and a new GPU-free `nanovanguardProjectAssetTests` integration target sourced
  from a dedicated `tools/nanovanguard/integration/` directory so it does not
  collide with the existing `nanovanguardTests` wildcard.
- P2: `asset_index.hpp/.cpp`, `asset_recooker.cpp`,
  `project_asset_recook_state.hpp/.cpp`, `project_asset_pipeline.hpp/.cpp`, with
  `assetIndexTests`, `assetRecookerTests`, `assetStreamingTests`, and the new
  headless integration target as applicable.
- P3: nanovanguard command-line/builtin/project-asset/context/Windows-console
  files, `nanovanguardTests`, and `nanovanguardProjectAssetTests`; every new
  filesystem-backed test uses `tests/support/scratch_directory.hpp`.
- P4: nanovanguard mutation/maintenance/lifecycle integration plus the narrow
  CURRENT editor partial-init lifecycle defect above and focused
  `assetStreamingTests` only where a real shared-backend defect is proven.
- P5: deterministic E2 matrix centered on `nanovanguardProjectAssetTests` and the
  exact existing owner suites (`assetStreamingTests`, `assetRecookerTests`,
  `assetIndexTests`, `assetsPersistentCacheTests`, `streamingTests`,
  `textureToolsTests`, `editorAssetRetirementTests`) only where their contract is
  exercised.
- P6: fresh Premake plus serialized Debug/Development compile/link and execution
  of the affected E2 union, actual nanovanguard scratch invocations, dependency/
  hygiene audits, and no production edit by default.
- Deterministic runtime proof owns ordinary startup/rollback, config equality,
  DirtyReason parity, reimport/cache-hit/failure/cancel/stale/retry, Busy,
  maintenance, restart and human/JSONL terminal equivalence. Existing callback/
  barrier and scratch-file seams may inject compiler/cache/index/publication
  failures. Literal asynchronous process-crash timing, privileged Windows
  reparse/junction creation and a physical Ctrl+C console event are not claimed
  deterministic unless a portable seam exists; journal/restart state,
  owner-visible cancellation injection and source review cover those boundaries.
  Generation, compile/link, executable runtime, source review and injected proof
  remain separately reported.

**RED evidence boundary:** the fresh P0 search found no reachable RED backend
checkout under `/engine`, and `/gpt` is empty. The exact E2.5 RED references to
`dataCache`, `dependencyCache`, and `cleanServerCacheCommandlet` remain inherited
comparison evidence only; P0 does **not** claim a fresh RED source read. Fresh
CURRENT Vanguard ownership/lifecycle source is authoritative for this lock.

**P0 closure:** final independent exact-CURRENT read-only review returned
**0 BLOCKER / 0 HIGH / 0 MEDIUM** and accepted the corrected configuration,
lifecycle/link, Generated-dependency, freshness, reimport, command/protocol,
cancellation/maintenance and proof-boundary locks. No production C++, Premake,
generation, compile/link or executable test work was performed for P0. **P0 is
closed. Stop before P1; P1 remains separately unauthorized.**

### P1 - neutral identity/configuration + GPU-free headless bootstrap

Implement the smallest pure shared configuration/identity derivation so editor
and headless produce the same roots/target/settings fingerprint.

Add a private nanovanguard command-scoped GPU-free composition using the
existing direct owners. Do not yet expose the full asset command set.

Required proof:

- exact editor/headless config equality;
- existing fingerprint unchanged;
- windows-x64 mapping explicit/fail-closed;
- command-scoped lower-owner acquisition/rollback/shutdown;
- no engine/rendering/rhi/pipelineCache/window/input/world/editor link or include
  introduced into nanovanguard by this bootstrap;
- editor current behavior/defaults unchanged; and
- focused Debug/Development bootstrap + editor regression validation.

Independent closure required. Stop before P2.

### P2 - shared ProjectAssetPipeline surface completion

Add only the shared capabilities proven missing by frontend definition:

- read-only per-output VADI build freshness/dirty-reason query; and
- explicit current accepted asset/output reimport request through the existing
  recook/build authority.

No CLI-only dependency-index access, second pending queue or force-cache-miss
path.

The freshness query must not implement a simplified Resolve/Prepare/Evaluate
path. It reuses/refactors the authoritative recooker preparation sequence so
Generated dependency content is materialized from committed VADI before
Evaluate in both callers.

Add deterministic ProjectAssetPipeline regressions for:

- UpToDate;
- Missing;
- SourceChanged from source/target/source-input fingerprint drift;
- CompilerChanged from compiler/type/version drift;
- DependenciesChanged;
- committed Generated dependency content change, proving the public query and
  authoritative recook planning return the same DirtyReason;
- missing/optional/soft Generated dependency handling remains identical to
  authoritative recook preparation;
- unsupported/missing/conflicting current asset;
- multi-output reimport;
- Busy/live-work exclusion;
- failure/cancel/stale retention; and
- retry/newer-change recovery.

Independent closure required. Stop before P3.

**P2 closure (2026-09-18).** The implementation keeps one
`DependencyIndex`-owned Generated-dependency materializer, one existing
`IncrementalRecooker`/automatic coordinator, and one shared
`ProjectAssetPipeline` surface. The initial exact-CURRENT implementation
preflight returned **0 BLOCKER / 2 HIGH / 1 MEDIUM**; explicit-only recovery
isolation, disabled dormant-repair/policy-transition isolation, and exact
uninitialized-index `InvalidState` mapping were corrected before final proof.
Deterministic regressions cover the required freshness/reimport matrix,
including committed Generated-content `DependenciesChanged` parity and
missing/Optional/Soft Generated handling.

Fresh `premake5.exe vs2022` passed the engine-service audit. Serialized `/m:1`
x64 Debug and Development compile/link passed for `assetIndexTests`,
`assetRecookerTests`, `assetStreaming`, `assetStreamingTests`,
`nanovanguardProjectAssetTests`, `nanovanguard`,
`editorAssetRetirementTests` and `editor`. In both configurations the five
applicable test executables (`assetIndexTests`, `assetRecookerTests`,
`assetStreamingTests`, `nanovanguardProjectAssetTests` and
`editorAssetRetirementTests`) were actually executed and exited 0.
Scope/dependency/hygiene checks found no stale duplicate materializer or repair
field, frontend VADI access, second recooker/session/backend, forbidden
assetStreaming rendering/RHI dependency, forbidden nanovanguard aggregate link,
or P3 asset-root/console-cancellation leakage; whitespace checks were clean
apart from the existing tracked LF-to-CRLF notices.

A late read-only audit correctly challenged policy disable around automatic
publication. A first broad correction that also blocked every retained
`HasPublishableDelivery()` contradicted the established E2.4 phased lifecycle
and caused 15 existing `assetStreamingTests` failures, so it was rejected.
The final tested behavior preserves retained committed delivery across policy
disable for later re-enable/shutdown handling, while `DisableAutomaticRecook()`
returns `Busy` for transient explicit activity and for the true in-flight
visibility-settlement window, `publicationSettlementPending`; the pre-existing
Busy gates for owned automatic admission, observation reconciliation, VDDC
maintenance and loose-retirement admission remain unchanged. The established
`TickAutomaticRecook()` phase-success semantics were not changed. Fresh
post-correction Debug/Development proof is green, and the final independent
exact-CURRENT closure review returned **0 BLOCKER / 0 HIGH / 0 MEDIUM**. **P2
is closed; stop before P3. P3-P6 remain separately unauthorized.**
A post-reconciliation docs-only review found one MEDIUM parent-plan wording
ambiguity around the unchanged Busy gates; the wording was corrected and the
final docs-only exact-CURRENT recheck returned **0 BLOCKER / 0 HIGH / 0 MEDIUM**.

### P3 - nanovanguard commands and protocol

Repair normal sources parent registration and add the bounded asset command
domain for:

- status;
- rescan;
- build;
- reimport; and
- long-running watch/sync.

Add semantic human/JSONL rendering, exact exit mapping and current-session
Ctrl+C cancellation. New filesystem-backed tests use the scratch helper.

Required proof:

- document-only commands remain lightweight;
- no GPU/rendering dependency;
- stable source-path/output-key resolution;
- no cross-process status/cancel claim;
- deterministic JSONL string tokens and one terminal event;
- malformed/usage errors have defined format behavior;
- event-storm/overflow bounded progress; and
- foreground cancellation drains cleanly.

Independent closure required. Stop before P4.

P3 closed on 2026-09-19 after the final exact-current watch teardown correction.
The implementation repairs the normal `sources` parent; registers only
`asset status`, `asset rescan`, `asset build`, `asset reimport` and
`asset watch`; uses one bounded semantic human/JSONL model with deterministic
raw valid-JSONL usage preselection; resolves declared targets and accepted
source/output state; and uses the process-local Windows Ctrl+C/Ctrl+Break flag
seam without calling pipeline APIs from the console handler. Cancellation proof
is deterministic owner-visible injection plus source review of the console
handler; no physical console-event test and no cross-process status/cancel
semantics are claimed.

Final validation used the already-regenerated VS2022 workspace from a fresh
`premake5.exe vs2022` run whose engine-service audit passed. Serialized
`/m:1` Debug builds for `nanovanguardTests`,
`nanovanguardProjectAssetTests` and `nanovanguard` passed;
`nanovanguardTests` plus all 19 fresh-process project-asset modes were actually
executed for **20/20** Debug invocations. The same three Development targets then
built successfully and the same **20/20** executable invocations passed.
Rebuilt Debug production smoke verified exactly the five P3 asset children,
JSONL `usage-error`/`asset-status` exit 2 behavior, successful real-scratch
`sources inspect`, status, rescan, build and reimport, and corrupted-source
watch termination as one failed terminal with exit 20. Dependency/scope audit
confirmed the direct GPU-free lower-owner closure, no
`assetStreaming -> rendering`, no frontend DependencyIndex/VMETA parser /
IncrementalRecooker/BuildGraph ownership and no P4 command registration. Tracked
`git diff --check` was clean apart from expected LF-to-CRLF notices; the seven
untracked nanovanguard source/doc files also passed trailing-whitespace,
conflict-marker and final-newline checks.

Review chronology was intentionally stricter than the first clean pass: the
implementation audit returned **0 BLOCKER / 4 HIGH / 2 MEDIUM** and all findings
were corrected; a later watch review returned **0 BLOCKER / 2 HIGH / 0 MEDIUM**
for stale `activeRoots` ownership and CancelledBatch mapping and those were
corrected; after that clean re-review, a fresh reviewer found one further HIGH
where lower poll-dispatch failure can expose `Failed` while retaining automatic
admission. The rejected `RetryAutomaticRecook()` workaround could loop on
repeated dispatch failure. Final code instead terminates genuine
Failed/RecoveryRequired through `CloseAndFinish`, making
`ProjectAssetPipeline::Shutdown` the blocking reclamation boundary;
command-owned CancelledBatch remains the graceful cancelled/30 path. The final
independent exact-CURRENT re-review of that direct-close state returned **0
BLOCKER / 0 HIGH / 0 MEDIUM**. **P3 is closed; stop before P4. P4-P6 remain
separately unauthorized.**

### P4 - maintenance, explicit mutation, lifecycle and restart

Expose VDDC/loose maintenance plus the E1F operations required for headless
parity:

- move;
- generated-identity duplicate;
- retarget; and
- delete with explicit dependent/replacement policy.

Close command-level restart/shutdown behavior. Also correct the P0-proven CURRENT
editor partial-initialization lifetime defect narrowly: when
`ProjectAssetPipeline::Initialize` returns a non-Success result but leaves the
pipeline initialized (notably `CommittedReconciliationPending`), editor must not
shut its TextureAssetCompiler/callback owner down before the live pipeline has
been safely shut down.

Required proof:

- VDDC then loose sequential "all" semantics;
- maintenance retry/cancel/status/diagnostics;
- no CLI-side victim/deletion authority;
- move preserves AssetId;
- duplicate gets fresh AssetId;
- delete never silently strands referencers;
- VTXN remains authored mutation authority;
- committed cleanup/reconciliation pending is distinct;
- restart has no transient queue/history replay; and
- shutdown keeps callback/streamer owners alive until pipeline shutdown succeeds;
  and
- focused editor regression proves the failed/partial-init branch preserves that
  same callback-lifetime ordering.

No persistent CLI undo/redo.

P4 freezes the mutation frontend grammar before implementation:

- `asset maintain <project> --domain vddc|loose|all [--target <token>]
  [--format human|jsonl]`. Each one-shot process starts a fresh maintenance
  request. A terminal owner status with `retry-required=true` is reported and
  exits 20; a later process is a fresh `Request*Maintenance`, not a claim that
  transient `Retry*Maintenance` state persisted across process restart. The
  existing backend retry APIs remain covered by their owner tests.
- `asset move <project> <source> <destination> [--target <token>]
  [--format human|jsonl]` and
  `asset duplicate <project> <source> <destination> ...`. `source` is a
  current accepted asset locator. `destination` is a lexical UTF-8 path
  relative to the same project Assets root, uses the same slash normalization /
  absolute-path / `.` / `..` rejection as other asset locators, and need not
  already exist. Move preserves AssetId; duplicate calls the pipeline's bounded
  generated-identity API and never accepts a caller-supplied AssetId.
- `asset retarget <project> <source> --output <key>
  --replacement-source <source> --replacement-output <key> ...`. The three
  mapping options are repeatable and their occurrence counts must match. Every
  previous output is resolved from the primary accepted `source`; every
  replacement source/output is resolved independently through current accepted
  ProjectAssetState. At least one mapping is required.
- `asset delete <project> <source> --dependents reject|replace ...`. `reject`
  forbids replacement mapping options and passes an empty replacement set to the
  backend, which fails closed with `ReferencesRemain` if external authored
  referencers exist. `replace` requires one or more repeatable
  `--output` / `--replacement-source` / `--replacement-output` triples,
  resolved exactly as for retarget. Missing mappings for still-referenced
  outputs remain a backend `ReferencesRemain` failure. The frontend never
  enumerates victims, edits authored files, or deletes files itself.
- P4 mutation failure/status output uses stable lower-kebab tokens for the exact
  `AssetOperationResult` (including `replacement-unavailable`,
  `references-remain`, `rewrite-unavailable`, and transaction/limit
  failures). Maintenance diagnostics likewise serialize the existing bounded
  VDDC and loose-maintenance diagnostic reasons; they are not collapsed into a
  generic `operation-failed`.

Exact-current implementation and focused acceptance are complete. The command
surface uses one stable `asset-maintain` operation token; handler-side semantic
usage validation emits `failed` while preserving exit 2 and structural parser
failures alone emit `usage-error`. Debug and Development each passed the complete
36-mode project-asset matrix, including a two-process moved-source restart proof,
and the rebuilt Debug production CLI passed real scratch maintain/move/duplicate /
retarget/delete flows. The editor lifecycle fix and nanovanguard P4 surface each
received independent exact-current **0 BLOCKER / 0 HIGH / 0 MEDIUM** review.
The user then changed the validation policy so intermediate Ps no longer wait on
a fresh broad Premake/build/test/native batch; that comprehensive validation is
reserved for P6 while independent exact-CURRENT review remains mandatory. The
historical fresh Premake attempt stopped on an unrelated concurrent world-session
source-lineage comment. Do not edit that unrelated file to manufacture a pass.
With no remaining P4 BLOCKER/HIGH/MEDIUM finding, **P4 is closed** under the
updated policy. At that checkpoint P5-P6 remained separately unauthorized.

### P5 - deterministic E2 end-to-end matrix

Using one supported production compiler/importer and approved scratch project,
prove at minimum:

1. cold external add -> reconcile/classify -> build -> VADI -> loose publication;
2. clean restart -> exact persistent VDDC hit with zero compiler calls;
3. source/settings/dependency change -> correct affected rebuild;
4. per-output build freshness reasons match authoritative VADI evaluation;
5. explicit reimport uses existing recook/build ownership;
6. stale/failure/cancel preserves last-valid committed/publication truth;
7. move preserves AssetId;
8. duplicate gets new AssetId;
9. delete with dependants reports explicit state and supported replacement path;
10. watcher burst/partial write/overflow -> bounded authoritative recovery;
11. active build blocks conflicting rescan/E1F/maintenance as contracted;
12. VDDC maintenance protects committed/staged/reader/writer/publication owners;
13. loose maintenance protects current/source/load/object/resident owners;
14. cancellation during build/watch/VDDC/loose maintenance;
15. active-work shutdown followed by clean reinitialize;
16. no transient cancel/watch/undo state after restart;
17. editor/headless canonical config equality;
18. human/JSONL equivalent terminal semantics; and
19. no repository-root/generated/cache residue outside approved paths.

Keep portable runtime proof, fault injection and source review distinctly
reported. Do not redesign production unless this matrix proves a real defect.

Independent closure required. Stop before P6.

P5 was subsequently authorized with `START` and is closed under the updated
intermediate-gate policy after coherent test-only implementation, source-level
sanity and a fresh independent exact-CURRENT **0 BLOCKER / 0 HIGH / 0 MEDIUM**
review. No production owner/API redesign was required. The P5 additions in
`nanovanguardProjectAssetTests` wrap the real `TextureAssetCompiler` only at its
existing descriptor callbacks so the deterministic matrix can count/barrier real
production compilation without exposing test telemetry in product code. They add
source-level executable proof for cold external add -> authoritative rescan ->
VADI/loose publication, fresh-process persistent-hit/zero-compile restart,
source/settings/real generated Tool-dependency drift, active-build Busy/cancel /
shutdown/reinitialize ownership, shared SemanticEvent human/JSONL rendering, and
scratch-path containment/cleanup.

The remaining rows intentionally compose those headless modes with the exact
existing owners rather than duplicating them: public VADI DirtyReason and
Generated-dependency materialization, explicit reimport and last-valid
failure/cancel/stale/retry, move/duplicate/restart identity, watcher partial-write /
burst/overflow recovery, VDDC protection/cancellation, loose current/source/load /
object/resident protection/cancellation, and the shared editor/headless config
helper. The production texture importer does not author asset references, so the
dependant-delete row uses the real shared-owner referencer rejection/replacement
proof together with the existing headless delete/replacement command coverage;
P5 does not invent a test-only production reference format.

One Debug `nanovanguardProjectAssetTests` build attempt was started during P5
despite the batching policy and stopped on two newly added test-source diagnostics:
a hidden `workspace` name (C4458) and ambiguous unqualified `io`. Both source
issues were corrected, but the build was deliberately not rerun. **No P5
compile/link/executable pass is claimed.** Fresh Premake, Debug/Development build
and execution, production scratch invocation, dependency/hygiene audit and the
complete 19-row runtime evidence remain the P6 gate. Physical Ctrl+C/Ctrl+Break
delivery also remains unexecuted and unclaimed. **Stop before P6; P6 remains
unauthorized and unstarted.**

### P6 - final validation and complete E2 closure

Run fresh Premake VS2022 generation, serialized /m:1 Debug and Development
compile/link and every applicable freshly rebuilt executable.

Validation union must include:

- nanovanguard parser/protocol tests;
- the new headless project-asset integration target;
- assetStreaming tests;
- affected assets/streaming/textureTools tests;
- editor compile/link and focused project-retirement/shared-config regressions;
- actual nanovanguard executable commands against scratch projects; and
- any focused E2.1-E2.5 targets required by the final dependency surface.

Also run:

- engine/header-layout audit;
- tracked/no-index diff/whitespace checks;
- generated-output/scratch checks;
- dependency/link audit proving nanovanguard does not gain forbidden GPU/editor
  dependencies;
- duplicate-authority/delete-authority/E2 scope searches; and
- stable help/JSONL/exit-code checks.

Report generation, compile/link, executable behavior, source review and fault
injection separately. Preserve any unrelated known shared-tree failure honestly;
do not relabel a broad executable green.

Update this plan, parent E2 plan, foundation progress, document map and planning
triplet. Obtain a fresh exact-CURRENT independent BLOCKER/HIGH/MEDIUM review.

Only after a clean review may P6, E2.6 and the complete E2 phase close.

### P6 closure evidence - 2026-09-22

Fresh `premake5.exe vs2022` generation returned exit 0 and passed the Vanguard
engine-service audit. Serialized `/m:1` x64 Debug and Development compile/link
passed for `assetIndexTests`, `assetRecookerTests`,
`assetsPersistentCacheTests`, `assetGraphTests`, `assetStreaming`,
`assetStreamingTests`, `streamingTests`, `textureToolsTests`,
`nanovanguardTests`, `nanovanguardProjectAssetTests`, `nanovanguard`,
`editorAssetRetirementTests` and `editor`. In each configuration the nine
applicable owner/test executables passed, and the fresh-process
`nanovanguardProjectAssetTests` matrix passed all **42/42** invocations in the
required restart/dependency order.

P6 exposed one test-only cold-matrix defect before closure: source/profile
"rewrite" helpers used the deliberately create-only nanovanguard
`WriteFileDurable` path, so existing files were not changed and the drift checks
short-circuited before `Rescan`. The helpers now use the filesystem
`CreateFileWriter` overwrite/truncate path. The focused Debug rerun and both
complete 42/42 matrices pass. Production freshness behavior was not changed;
source/settings drift still evaluates `SourceChanged`, rebuild returns
`UpToDate`, and generated dependency drift remains `DependenciesChanged`.

Fresh rebuilt production `nanovanguard` scratch execution in Debug and
Development covered help, JSONL parser usage/exit 2, `sources inspect`,
status/rescan/build/reimport, failed watch termination, `maintain all`,
move plus fresh-process status, duplicate, retarget and delete reject/replace.
The corrupted-source watch path emitted one failed terminal with exit 20.
Approved scratch project roots were removed afterward. Physical
Ctrl+C/Ctrl+Break delivery remains intentionally **unexecuted and unclaimed**;
the accepted proof remains deterministic owner-visible cancellation plus console
handler source review.

Final audits passed: engine/header-layout audit, `git diff --check` (only
working-copy line-ending notices), E2-specific generated/scratch residue and
untracked-file hygiene, process residue, command/protocol/exit-code checks, and
the dependency/link/authority scope proving no forbidden headless GPU/editor
aggregate dependency or second asset authority. The final independent
exact-CURRENT P6 review returned **0 BLOCKER / 0 HIGH / 0 MEDIUM**. Therefore
**P6 is closed, E2.6 is closed, and complete E2 is closed.**

## Cross-gate invariants

- ProjectAssetPipeline remains the shared behavior owner.
- Frontends own separate process/session instances; no new shared session service.
- SourceDatabase remains source/reference authority.
- BuildSystem/BuildGraph/IncrementalRecooker/VADI/VDDC remain build/cache authority.
- AssetFileTransaction/VTXN remains authored mutation/recovery authority.
- ResourceStreamer remains loose source/runtime authority and
  RetireLooseGeneration remains final managed loose physical-delete authority.
- Rendering retirement remains editor composition only.
- Headless stays GPU-free at both link and runtime composition boundaries.
- Same project/tool configuration means same roots/target/settings fingerprint.
- Session-local observation/build/cancel/undo state is never presented as durable.
- No frontend directly mutates VADI/VDDC/loose physical files.
- Every numbered gate requires separate user authorization.

## Definition exit

Definition closes only after a fresh exact-CURRENT independent read-only review
finds no blocker/high/medium contradiction in this document and its parent/status
updates. The strict re-review after the Generated-dependency correction returned
**0 BLOCKER / 0 HIGH / 0 MEDIUM** on 2026-09-18, so the definition is closed.
At that historical definition checkpoint, P0 was not yet authorized. The user
subsequently authorized **P0 only** with `start P0`; P0 later closed at final
independent exact-CURRENT **0 BLOCKER / 0 HIGH / 0 MEDIUM** as recorded above.
That P0 closure does **not** authorize P1.
