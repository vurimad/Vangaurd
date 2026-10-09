# Vanguard repository agent guidance

## Build and validation

- **HARD USER RULE - NO TESTS BY DEFAULT during ordinary/intermediate work.** Do
  not add, expand, build, or execute unit, integration, native/UI, regression,
  performance, acceptance, or other behavioral test suites for ordinary edits,
  bug fixes, refactors, documentation, small features, or intermediate phase gates.
  A few changed lines must never turn into a long validation cycle.
- **Final-gate exception:** the last explicitly authorized phase for a work item
  (`P6` or whatever that plan's final gate is) must leave the relevant source
  compiling and run the smallest focused proof tests necessary to establish
  closure. This final compile/proof requirement applies even when the work did not
  introduce a new engine system. Keep it narrow: do not turn final acceptance into
  a broad regression matrix, unrelated rebuild sweep, or exhaustive native/UI run.
- A separate intermediate exception exists when the task introduces a genuinely
  new engine system/subsystem and validation is critical to establishing that new
  system is viable. State why both conditions apply before using it, and keep that
  validation narrowly focused on the new system.
- Broad matrices, unrelated regressions, interactive native automation, or
  exhaustive acceptance suites still require explicit user authorization. Prefer
  source review and lightweight static hygiene outside the two exceptions above.
- Existing plans, checklists, historical validation gates, or another agent's
  recommendation do not override this rule. When they demand more validation than
  the user rule permits, trim to the focused final-gate proof or record the extra
  checks as deferred/not run.
- Use the repository's Premake build. Never introduce or use CMake.
- Generate Visual Studio projects with `premake5.exe vs2022`.
- Build focused generated projects under `build/projects/vs2022`.
- Use serialized outer MSBuild execution (`/m:1`) in this shared workspace.
- Keep compilation, linkage, behavioral tests, and source review as distinct claims.
- Never report a test or runtime result that was not actually executed.
- Keep generated output out of the repository root. Filesystem-backed tests use
  `tests/support/scratch_directory.hpp` (platform temp, configuration, suite).
  Keep authored input paths separate from scratch paths. Editor caches use the
  project's configured DerivedData location, processing output uses Intermediate,
  and logs/user state use Saved. Standalone runtime machine caches use platform
  user-cache storage. Packaged DATA belongs beside the packaged executable.
  Development runnable images use an explicit build/image directory, never the
  repository root.
- **HARD USER RULE - KEEP THE REPOSITORY ROOT CLEAN.** Never create or leave
  generated files directly in the repository root: no compiler objects, temporary
  probes, reports, CSV traces, dumps, screenshots, caches, or diagnostic logs.
  Give compilers and profiling tools explicit output paths inside `build/`, the
  project's `Saved/` or `Intermediate/`, or a dedicated temporary directory.
  Do not run tools with the repository root as their working directory if they
  write relative output filenames. Fix the producing code or command; adding
  ignore rules does not prevent physical root pollution. Do not add new root
  files without the user's explicit request. After any build, experiment, or
  profiling activity, check for and remove only artifacts created by that work.

## Shared-worktree safety

- This is a shared, dirty checkout. Existing tracked and untracked changes belong
  to the user or another task unless proven otherwise.
- Inspect `git status --short` and the relevant diff before editing.
- Preserve unrelated changes. Do not reset, revert, clean, or broadly format the
  worktree.
- Make narrow edits and use the repository's existing style and ownership.

## Test and proof scaffolding isolation

- **Non-negotiable production-tree cleanliness invariant:** production source
  trees contain product/engine implementation only. Agent workflow artifacts,
  chat/worker identities, checkpoint labels, development-plan phase/task IDs,
  temporary probes, proof harnesses, automation drivers, fixtures, and other
  test-only behavior must never be placed in production-looking source folders.
  This is a placement rule as well as a naming rule.
- Development-plan/task identifiers such as `P0`, `P5`, `E3.3`, ticket numbers,
  or temporary milestone names are documentation/history, not product concepts.
  Do not put them in production source filenames, production identifiers, plugin
  technical names, environment variables, runtime log contracts, or persistent
  formats.
- Do not put agent names/IDs, conversation/session IDs, review checkpoints,
  temporary approval/gate vocabulary, or similar process metadata into source
  identifiers, runtime strings, file formats, plugin contracts, environment
  variables, or product filenames.
- Test/proof/diagnostic code must have unmistakably test-only names (`test`,
  `fixture`, `harness`, `diagnostic`) and live under an explicit test/support
  tree such as `<module>/tests/support/`, never beside ordinary production
  implementations merely because it needs access to them.
- Placement examples are intentionally strict:
  - **BAD:** `editor/framework/src/editor_ui_proof.cpp`
  - **BAD:** `editor/extensions/editor_static_probe_extension.cpp` when the
    implementation exists only for validation/automation.
  - **GOOD:** `editor/tests/support/native_ui_integration_test_harness.cpp`
  - **GOOD:** a dedicated `*Test` executable that compiles production sources
    plus explicitly listed files under `tests/support/`.
- Test-only code must not live under ordinary `src/`, `framework/`, `private/`,
  `extensions/`, `runtime/`, or equivalent product trees solely to gain access
  to internals. Add a narrow production API/owner when functionality is truly a
  product requirement; otherwise keep the implementation in the test tree.
- Integration harnesses that need the real product composition should use a
  dedicated test executable/target that compiles the production sources plus
  explicitly listed test support. Do not link test support into the normal
  product target merely for convenience. Never pull `tests/support/**` through
  a production wildcard. Any exceptional case that truly cannot use a dedicated
  target must be documented and reviewed first; activation must still require an
  explicit test-only path and normal production services/catalogs/behavior must
  not depend on the harness or its descriptors.
- A product target must never include a test-support directory, test plugin,
  test application entry point, test catalog, or automation CLI through a
  wildcard. Production headers/sources must not include files from `tests/`.
- Test/proof filenames themselves must be unambiguous. Avoid vague names such as
  `probe.cpp`, `proof.cpp`, `temp.cpp`, or `debug.cpp`; use names that state the
  role, for example `*_integration_test_harness.cpp`, `*_test_fixture.cpp`, or
  `temporary_*_test.cpp` for intentionally short-lived scaffolding.
- Long-lived executable tests should use behavior-based test names/messages, not
  development phase/checkpoint labels. Phase/task history belongs in plans and
  progress documents.
- Test support may consume real production APIs and observe real production
  behavior. Production code must not consume test-support behavior. If a test
  prototype becomes required product functionality, move that functionality
  behind the proper production owner/API and delete the test dependency.
- Detailed repository-wide examples and review checks are in
  `docs/development/test-support-isolation.md`. Read it before adding any
  integration harness, proof executable, fixture plugin, or automation driver.

## Architecture boundaries

- REDengine is the primary architectural and behavioral reference for Vanguard.
  The RED source tree is available locally at `D:\root\R6.Root\Mainline\dev\src`
  (RED paths in findings/plans such as `common/game/src/cameraSystem.cpp` resolve
  under that root). Trace the relevant RED ownership, lifecycle, data flow and failure behavior
  before defining or changing an engine system. This is especially mandatory for
  application/frame loops, world-to-render handoff, render graph construction,
  queue/submission ordering, resource lifetime and shutdown. Record exact RED
  source evidence and make every intentional deviation explicit. Vanguard's
  GPU-driven bindless material/resource design is an intentional rendering
  divergence; preserve it while applying RED's surrounding ownership and lifecycle
  model. A scoped exception in one editor/asset task does not establish precedent.
- Reuse existing engine services and owners. Do not add duplicate registries,
  schedulers, dependency databases, caches, filesystem transaction systems, or
  runtime resource loaders.
- Keep authored asset references, build dependencies, and runtime streaming
  dependencies separate.
- `SourceDatabase` owns source inventory and authored-reference facts.
- `BuildSystem`, `BuildGraph`, `IncrementalRecooker`, VADI, and VDDC retain build
  and cache authority.
- `AssetFileTransaction` retains durable filesystem commit and restart-recovery
  authority.
- `ProjectAssetPipeline` is the shared owner-thread integration and command
  surface for editor and headless frontends.
- `ResourceStreamer` remains runtime resource-resolution authority.
- Commit authored filesystem changes before catalog rescan and publication
  reconciliation. A committed cleanup/reconciliation warning is not a rollback.
- Do not mutate project sources while live build requests own graph work.

## Current asset-foundation references

Read the relevant sections before changing asset-foundation code:

- `docs/development/README.md` (document map and current agent entry points)
- `docs/development/plans/project-asset-e2-plan.md`
- `docs/development/plans/project-asset-e2-3-plan.md` (when working on E2.3)
- `docs/development/plans/project-asset-e2-4-plan.md` (when working on E2.4)
- `docs/development/plans/project-asset-e2-5-plan.md` (when working on E2.5)
- `docs/development/plans/project-asset-e2-6-plan.md` (when working on E2.6)
- `docs/development/plans/editor-foundation-study-and-roadmap.md`
- `docs/development/progress/project-asset-foundation-progress.md`
- `docs/formats/vmeta-format.md`
- `docs/formats/vtxn-format.md`

## Current editor-shell references

Read the relevant sections before changing editor shell, command, panel,
extension, menu, diagnostics/progress or layout code:

- `docs/development/README.md` (document map and current agent entry points)
- `docs/development/plans/editor-foundation-study-and-roadmap.md`
- `docs/development/designs/editor-ui-integration.md`
- `docs/development/plans/editor-foundation-e3-plan.md`
- `docs/development/plans/editor-foundation-e3-1-plan.md` (when working on E3.1)
- `docs/development/plans/editor-foundation-e3-2-plan.md` (when working on E3.2)
- `docs/development/plans/editor-foundation-e3-3-plan.md` (when working on E3.3)
- `docs/development/plans/editor-foundation-e3-4-plan.md` (when working on E3.4)
- `docs/development/plans/editor-foundation-e3-5-plan.md` (when working on E3.5)
- `docs/development/plans/editor-foundation-e3-6-plan.md` (when working on E3.6)
- `docs/development/plans/editor-foundation-e3-7-plan.md` (when working on E3.7)
- `editor/README.md`

E3 must extend the existing `EngineHost`, `ProjectWorkspaceService`,
`EditorUiService` and `EditorPanelRegistry`; it must not add a second application
loop, service container, panel registry, input pump or presentation path. Editor
extensions are statically compiled first. `.vproject` plugin entries select known
descriptors and do not authorize a dynamic native plugin ABI or directory loader.

For a scoped planning-with-files task, treat its active `task_plan.md` as the
execution contract, `findings.md` as evidence, and `progress.md` as the exact
validation ledger. Update those files as work proceeds; do not place volatile
task status in this stable repository guidance.
