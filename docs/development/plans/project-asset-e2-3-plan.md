# E2.3 - incremental recook coordination and stale completion rejection

## Status and authorization

**Complete and accepted on 2026-09-15.** E2.1 and the E2.2 follow-up are
independently accepted. E2.3 P0 architecture proof, P1 shared bounded
state/surface, P2 evidence capture/request translation, P3 first-cook/
transactional incremental execution, and P4 pre-commit freshness/stale rejection
are implemented as of 2026-09-15. The user has now explicitly authorized P5
end-to-end after the earlier P6/P7 closure. Every P6 gap that was
test-only is now covered and green in Debug/Development, and the fresh P7 build/
execution/hygiene matrix passes. The first independent current-tree review found
four medium test/documentation closure gaps; all four are repaired and the affected
Debug/Development AssetStreaming reruns are green. The final independent repaired-
tree re-review reported zero blocker/high/medium findings, so P7 is complete. P5 is
now implemented and the complete P6 matrix, including the formerly P5-owned rows,
passed the earlier fresh Debug/Development validation. A later strict P5 closure
review found four medium proof/documentation gaps; the current tree now repairs all
four, focused Debug/Development are green, and exact-tree full validation/hygiene
passes. The final independent current-tree E2.3 re-review reported `0 blocker / 0
high / 0 medium`; P5, P6 and E2.3 are closed. Do not continue into E2.4 without
separate authorization.

## Read-first context

Read repository `AGENTS.md`, [E2 contract](project-asset-e2-plan.md),
[foundation progress](../progress/project-asset-foundation-progress.md), and
[editor roadmap](editor-foundation-study-and-roadmap.md).
Read `docs/architecture/incremental-recooking.md`, `docs/formats/vadi-format.md`,
`docs/formats/vpas-format.md`, and the relevant VMETA/VTXN contracts.
Inspect current source rather than treating historical validation as proof of
the current tree. Preserve the shared dirty checkout.

E2.2 produces an accepted asset baseline: bounded durable comparison evidence,
not a duplicate source registry or build cache. VPAS restart restores baseline
facts, but does not replay transient changes or dirty queues. SourceDatabase
retains inventory/authored-reference ownership; VADI retains successful build
dependency authority. Compare current sources with committed build state when
recovering work after restart; do not add dirty replay to VPAS.

## Scope and ownership

ProjectAssetPipeline coordinates one owned automatic batch and a bounded pending
set, using existing Jobs, BuildSystem, BuildGraph, IncrementalRecooker and VADI.
Expose opt-in automatic coordination, explicit reimport, progress, dirty reasons,
failure and cancellation on the shared headless surface. Exact API names are an
implementation decision documented in P0. Do not introduce a private scheduler,
second dependency database, source registry, cache or filesystem transaction.

E2.4 owns new automatic loose publication, stale publication status and resident
replacement. E2.3 must not route automatic completions through a convenience
request wrapper that publishes output as a side effect. Preserve existing manual
request behavior without claiming automatic runtime hot reload.

## Source entry points

- `source/assetStreaming/include/vanguard/asset_streaming/project_asset_pipeline.hpp`
  and its implementation/tests: shared owner integration, source-mutation guards,
  observation handoff, existing request publication behavior.
- `source/assetStreaming/include/vanguard/asset_streaming/project_asset_change_state.hpp`
  and implementation: accepted facts, change records, typed seeds, output evidence.
- `source/assets/include/vanguard/assets/project_asset_build_system.hpp` and
  implementation: canonical source loading/importer request resolver.
- `source/assets/include/vanguard/assets/asset_recooker.hpp` and implementation:
  identity translation, affected roots, batch transaction/finalization.
- `source/assets/include/vanguard/assets/asset_graph.hpp`, `asset_index.hpp`,
  and their implementations/tests: graph ownership and VADI transaction rules.

## Mandatory correctness decisions

1. Capture each newly accepted generation's change/output evidence before the
   next acceptance can replace it. Merge by canonical identity without dropping
   previous-side move/delete invalidation. A no-change acceptance must not erase
   queued work. Do not hold borrowed spans across catalog/baseline swaps.
2. Recooker AssetChange uses input/source identity space: Request calls VADI
   GetDirectDependants. A cooked output is not interchangeable with that input.
   Newly discovered sources have no committed VADI record and currently produce
   UntrackedChange. Define first-cook roots through the canonical importer build
   resolver; keep initial roots and indexed dependent recooks under safe existing
   graph/index transaction ownership. Do not silently discard untracked work.
3. Valid callback importers may have no descriptor sourceType. They remain
   supported and retain dirty change/output evidence. Resolve real BuildRequests
   through createBuildRequest; never fabricate a typed source identity or copy a
   competing importer resolver into AssetStreaming. A narrow reusable resolver
   seam in ProjectAssetBuildSystem is permitted if needed.
4. Capture source bytes/fingerprints, settings, importer/version, target and
   relevant dependency evidence represented by the requests. An accepted
   generation alone is NOT a freshness proof: catalog reconciliation is guarded
   against live graph work while external disk writes can still occur.
5. Freshness rejection must happen BEFORE successful VADI commit. Current
   RecookBatch::Poll/Wait finalize the transaction when roots finish, and
   IncrementalRecooker::Poll calls CommitTransaction. Checking after Poll reports
   Succeeded is too late. Extend the existing owner with a narrow pre-commit
   validation/finalization seam if required; preserve its default callers and
   rollback/isolation behavior. Do not commit and then try to undo VADI.
6. Lossy watcher hints are insufficient to prove freshness. Validate captured
   evidence coherently at the acceptance boundary, with an explicit conservative
   recovery policy for unreadable/partial writes and hints arriving during
   validation. Document what concurrent external-write races the implementation
   can detect; do not claim atomic disk-wide validation without a mechanism.
7. Only one owned automatic batch is active. Manual graph requests, E1F writes,
   rescan/reconciliation and automatic transactions must obey established live
   graph guards. Changes arriving during work remain pending for eventual retry.
8. Failure, cancellation or stale completion rolls back staged VADI and does not
   replace last-valid publications. Cached artifacts may exist without accepted
   state; artifact creation is not publication. No tight automatic failure loop:
   retain diagnostic/dirty evidence and define explicit retry/new-change policy.
9. Bound pending identities, captured bytes/evidence and operations per owner
   tick. Overflow requests authoritative recovery, not silent truncation. Move
   project-sized planning/I/O/reclamation off the owner tick using existing Jobs.
   RecookBatch destruction cancels and waits: do not hide blocking destruction
   inside a normal tick. Shutdown may drain through documented existing owners.

## Ordered implementation gates

### P0 - architecture proof and executable task plan

Before source edits, record the actual resolver, first-cook, VADI pre-commit,
external-write freshness, worker lifetime and manual-build exclusivity design in
task-local findings. Trace source calls, not just prose. Identify any narrow
assets-layer API extensions. If a requirement cannot be met within these owners,
report the blocker rather than weakening the contract. Create scoped
`.planning/e2-3-incremental-recook-coordination/task_plan.md`, `findings.md` and
`progress.md` when implementation is authorized. Keep validation claims separate.

### P1 - shared bounded state and surface

Define opt-in lifecycle, one-batch state, pending/retry semantics, dirty reasons,
progress and cancellation. Preserve existing manual APIs and guards.

### P2 - evidence capture and request translation

Consume accepted evidence exactly once into owned pending state; support new,
modified, moved, missing/restored and callback-imported sources. Derive indexed
reverse dependencies from VADI, not authored-reference facts. Missing,
unsupported or conflicting sources remain explicit diagnostics, not guessed
metadata repair or erased dependent references.

### P3 - first cook and transactional incremental execution

Reuse canonical request construction, graph dependency ordering/deduplication
and recooker transaction ownership. Prove first roots, transitive dependants,
burst coalescing and later-batch eventual processing.

### P4 - pre-commit freshness and stale rejection

Implement the P0 acceptance seam and captured-evidence validation. Reject stale
success before VADI commit; preserve pending current work and last-valid output.
Cancellation is an optimization, never the correctness mechanism.

Implemented on 2026-09-15: the existing recooker now exposes an optional
default-preserving pre-commit validator; VADI provides a bounded owned snapshot of
staged transaction records without changing committed-reader isolation; automatic
project validation freshly resolves/prepares represented requests, compares source
input/compiler/type/dependency evidence, uses staged generated-dependency content
where appropriate, and fences direct validation with the observation generation/
overflow token. A no-work completion is also validated before being accepted.
Stale/unreadable/inconsistent evidence rejects before commit and retains the
last-valid VADI state. This remains an E2.3 build-state gate only: it adds no
automatic loose publication or runtime resident replacement.

### P5 - failure, cancellation, bounds and teardown

Cover failed roots/resolution/I/O/commit, overflow recovery, no busy retry loop,
manual-operation contention, safe nonblocking ticks and shutdown/reinitialize.
P5 is now authorized and implemented with fresh executable validation green: terminal
automatic failure/recovery is sticky until explicit retry or genuinely newer
accepted truth; accepted capture overflow requests one authoritative recovery pass;
retained failed/cancelled/stale invalidations survive recovery rebuilding; retry
entries are bounded by admitted asset capacity; orchestration dispatch failures do
not create owner-tick retry loops; and shutdown remains the intentional blocking
cancel/drain boundary. This policy still ends at committed VADI/build state and does
not automatically publish loose resources or replace resident runtime data.

### P6 - deterministic regression matrix

Tests must cover first cook without VADI; valid untyped callback first cook/edit;
unchanged suppression; transitive rebuild/root deduplication; source/sidecar burst;
changes during a batch and eventual next batch; move old/new identity and delete/
restore diagnostics; failure and cancellation rollback; stale success after a
source/settings/importer/dependency change; a disk edit while accepted generation
has not advanced; hints arriving during freshness validation; one-in-flight and
manual graph contention; bounded ticks/overflow; teardown with live work; restart
with baseline but no transient replay and dirty/missing build-state recovery.
Assert VADI committed-reader isolation and unchanged last-valid publications on
failure/cancel/stale. Use deterministic controlled jobs/barriers rather than sleeps
for races. Native watcher smoke tests are supplemental, not sole correctness proof.

### P7 - validation and independent review handoff

Use Premake VS2022 and focused serialized MSBuild /m:1. Run affected assets-layer
tests when recooker/graph/index APIs change, plus assetStreamingTests in Debug and
Development. Record exact commands/results separately for generation, compile,
link and executed tests; verify cleanup, header placement and git diff --check.
Update durable E2/progress records with actual outcomes and remaining limits.
Request independent review; do not self-label independent acceptance. Stop at
E2.3. E2.4 requires separate user authorization.

Validation executed on 2026-09-15 after the P6 test additions: Premake VS2022 and
the engine-service audit passed; serialized x64 `/m:1` Debug and Development builds
of `assetGraphTests`, `assetRecookerTests`, `assetStreamingTests` and
`materialToolsTests` succeeded; all eight executables passed. `git diff --check`,
AssetStreaming header placement, owned test-root cleanup and focused process checks
also pass. The first independent review reported `0 blocker / 0 high / 4 medium`;
the restart-proof, current-publication, explicit stale-reason and parent-status
findings were repaired without P5/E2.4 production changes, and post-repair
`assetStreamingTests` again passes in Debug and Development. The final repaired-
tree independent re-review reported `0 blocker / 0 high / 0 medium`; P7 is complete.
After P5, fresh Premake generation/audit and the complete four-target serialized
Debug/Development build+execution matrix pass again; hygiene/layout/test-root/process
checks also pass on the pre-proof-repair P5 tree. The subsequent strict P5 review's
four medium proof/documentation gaps are now repaired; focused Debug is green on the
exact repaired tree, Development is green as well, and the full closure matrix/
hygiene passes. The final independent re-review reported `0 blocker / 0 high / 0
medium`; P5/P6/E2.3 are accepted. E2.4 remains inactive and requires separate
authorization.

## Pasteable handoff

Implement only E2.3 using this document and the E2 contract as authority. Begin
with P0 and maintain the scoped planning triplet. Preserve unrelated changes;
reuse existing owners and do not implement E2.4 publication/replacement. Do not
weaken first-cook/callback support, pre-commit stale rejection or bounded owner
work to get tests green. If compilation is not authorized in your session,
perform source checks only and explicitly leave P7 executable validation open.
