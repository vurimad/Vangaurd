# E3.1 - shell ownership and static extension lifecycle

## Status and authorization

Defined for cross-agent implementation. The user explicitly authorized E3.1 P0 on
2026-09-17. P0 is closed after the final fresh independent CURRENT-tree re-review
reported 0 BLOCKER / 0 HIGH / 0 MEDIUM. The user subsequently explicitly authorized
P1; its bounded catalog/identity implementation is now closed after focused Debug +
Development compile/link/executable validation, production-editor compile/link
integration proof, final hygiene and fresh independent CURRENT-tree review at
0 BLOCKER / 0 HIGH / 0 MEDIUM. The user subsequently explicitly authorized P2; its
shell-service/successful-activation implementation is now closed after focused Debug +
Development lifecycle tests, production-editor compile/link integration, owner-level
main-thread lifecycle enforcement/fatal destructor proof, final hygiene and fresh
independent exact-tree review at 0 BLOCKER / 0 HIGH / 0 MEDIUM. The user subsequently
explicitly authorized P3; its bounded owned-registration, rollback and shutdown
implementation is now closed after fresh Debug + Development focused executable proof,
production Debug editor compile/link integration, final hygiene and fresh independent
exact-CURRENT review at 0 BLOCKER / 0 HIGH / 0 MEDIUM. The user subsequently explicitly
authorized P4; its static compiled probe/project-selection integration is now closed
after fresh Premake generation, Debug + Development focused executable validation,
production Debug editor integration, final hygiene and fresh exact-CURRENT independent
review at 0 BLOCKER / 0 HIGH / 0 MEDIUM. The user subsequently explicitly authorized
P5; its focused failure/lifecycle matrix is now closed after exact-current Debug +
Development focused executable validation, audit/hygiene and fresh independent
exact-CURRENT review at 0 BLOCKER / 0 HIGH / 0 MEDIUM. The user subsequently explicitly
authorized P6; final validation/documentation/acceptance is now closed after fresh
Premake, exact-current Debug + Development focused execution, native editor bootstrap/
workspace proof including a project-selected compiled probe, complete scope/hygiene and
literal command-ledger validation, and fresh independent exact-CURRENT review at
0 BLOCKER / 0 HIGH / 0 MEDIUM. **E3.1 is accepted.** E3.2 definition is recorded in
its separate durable contract; E3.2 P0-P6 are closed and **E3.2 is accepted** after
final independent exact-CURRENT 0 BLOCKER / 0 HIGH / 0 MEDIUM review. The subsequently
authorized E3.3 definition and P0 architecture/test-shape lock are now closed at
independent exact-CURRENT 0 BLOCKER / 0 HIGH / 0 MEDIUM; P0 was source/planning review
only and made no implementation/build/test/native claim. E3.3 P1-P6 subsequently
closed and E3.3 is accepted. E3.4 remains outside this contract; its definition and P0
source/planning review subsequently closed at fresh post-fix 0 BLOCKER / 0 HIGH /
0 MEDIUM while P1-P6 and E3.5+ remain unauthorized/unstarted.

Post-acceptance hygiene note: the static probe and typed probe registrar described
below are **historical E3.1 proof scaffolding**. They were retired after later real
command/panel ownership domains supplied the same lifecycle/rollback evidence. The
current production shell contains no probe registration API or probe extension; the
retained static catalog fixtures live only under `editor/tests/support/`.

## Objective

Establish one deterministic workspace-scoped editor shell service that activates
known statically compiled extensions and owns their complete lifetime. A selected
extension can initialize bounded private state and, in later subphases, contribute
commands, panels and shell surfaces through typed registries without central shell
dispatch changes. Activation failure and shutdown leave no live callback,
registration or extension state behind.

E3.1 is lifecycle infrastructure only. Its proof extension records activation and
teardown; it does not add a production command, panel, menu, toolbar, layout file,
diagnostics console or asset workflow.

## Mandatory read-first set

Read, in order:

1. root `AGENTS.md`;
2. [E3 parent plan](editor-foundation-e3-plan.md);
3. [editor roadmap](editor-foundation-study-and-roadmap.md), especially E0/E3 and
   scheduling;
4. [editor UI integration](../designs/editor-ui-integration.md), especially editor
   ownership, input/focus, required editor boundary and non-goals;
5. `editor/framework/include/vanguard/editor/editor_ui.hpp` and
   `editor/framework/src/editor_ui.cpp`;
6. `editor/private/vanguard/editor/editor_application.hpp` and
   `editor/src/editor_application.cpp`;
7. `editor/private/vanguard/editor/editor_project_service.hpp` and its source;
8. `source/application/include/vanguard/application/{service,engine_host}.hpp`
   and host implementation/tests;
9. `source/projects/include/vanguard/projects/project.hpp`, project parser tests
   and `docs/formats/vproject-format.md`; and
10. `editor/premake5.lua`, `editor/README.md` and `editor/tests/README.md`.

After authorization, create and activate
`.planning/e3-1-editor-shell-extension-lifecycle/` with `task_plan.md`,
`findings.md` and `progress.md`. Copy this contract's P0-P6 gates into the task
plan without weakening them. Record exact current-tree evidence and every command
result in the appropriate file.

## Current Vanguard facts

1. `EditorApplication::Compose` reads and validates one explicit `.vproject`, then
   registers one `EngineHost` service graph and one application state machine.
2. `ProjectWorkspaceService` is a main-thread `EditorWorkspace` service owning the
   retained project descriptor and roots. The project descriptor already contains
   a bounded, duplicate-checked list of up to 128 plugin technical names.
3. `EditorUiService` is also a main-thread `EditorWorkspace` service. It depends on
   the workspace, frame pipeline, window, input and rendering services and already
   owns `EditorPanelRegistry`, hosts, textures and UI-frame state.
4. `EditorPanelRegistry` already rejects duplicate type/instance IDs, enforces
   capacities and refuses to unregister a type with live instances. E3.1 must not
   add another panel registry. Later ownership integration must destroy instances
   before unregistering their type.
5. `EngineHost` already validates service dependencies, starts them in compiled
   order, rolls back startup and shuts down through quiesce/drain/stop/shutdown.
   A shell service depending on UI/workspace naturally tears down before them.
6. The present editor is one workspace per process. E3.1 does not invent live
   project switching or a second nested service graph. Descriptor tables may be
   process-static; active extension instances and contributions are workspace-
   scoped.
7. `paths.plugins` resolves a project directory, but no approved dynamic native
   extension loader exists. `.vproject` plugin names currently carry validated
   configuration only.
8. Editor framework implementation is compiled directly into the editor target;
   there is no focused C++ editor-foundation test target yet. P0 must select the
   smallest Premake test seam without duplicating runtime ownership.

## RED architectural evidence to refresh in P0

The implementation agent must re-read and cite exact current RED paths/lines before
editing production source. Starting evidence:

- `dev/src/editor/editorCommon/include/framework.h` centralizes views, focus, actions,
  shortcut contexts and input dispatch beneath one framework rather than giving
  each tool its own loop.
- `dev/src/editor/editorCommon/src/controlWidget.cpp` pushes shortcut contexts on focus and
  pops them in reverse order, demonstrating that contextual behavior follows owned
  focus lifetime.
- `dev/src/editor/editorCommon/src/dockingManager.cpp` binds docking events in its
  constructor, unbinds them in its destructor and routes pane registration through
  one manager with registration/visibility events.
- `dev/src/editor/editorCommon/src/editorResourceEditorManager.cpp` enumerates registered
  editor factories centrally and asks each factory to service a request instead of
  hard-coding every editor into the caller.

Vanguard intentionally differs from RED: it uses Dear ImGui docking, the existing
`EngineHost` service lifecycle, explicit bounded descriptors and static compiled
extensions. Do not copy wxWidgets ownership, RTTI-wide discovery, interop paths or
global mutable event patterns. Preserve RED's useful separation of centralized
shell ownership, contextual routing and factory-based tool creation.

## Locked E3.1 ownership model

### One host, no second container

Add one editor-shell/extension-host service owned by the existing editor module.
It is `EditorWorkspace` scoped, main-thread affine, and explicitly depends on
`ProjectWorkspaceService` and `EditorUiService`. Add only narrower dependencies
proved necessary in P0. Register it during `EditorApplication::Compose`; do not
create a second application, frame loop, scheduler or service graph.

The shell service is the live owner of extension instances. The compiled catalog
is immutable description/configuration, not a service locator. Extension
activation cannot register new `EngineHost` services after host compilation.
Future feature modules requiring engine services must compose them explicitly
before `EngineHost::Compile`, outside an activation callback.

### Static catalog and project selection

Use bounded descriptors with stable extension ID, canonical technical name,
display/debug name, explicit required-extension IDs, activation/deactivation
entry points and opaque descriptor-owned data only where required. The public
surface must contain no ImGui, SDL or native window type.

The catalog is supplied explicitly by compiled editor composition. Do not depend
on linker initialization order, RTTI enumeration, filesystem scanning or mutable
process-global self-registration. Validate before any extension becomes visible:

- nonzero IDs, canonical technical names and valid callbacks;
- unique IDs and technical names;
- bounded descriptor and dependency counts;
- every dependency present and selected/always-built as appropriate;
- no self-dependency or cycle; and
- a deterministic topological activation order with stable technical identity as
  the tie-breaker. `.vproject` list order does not create hidden precedence.

Canonical technical-name syntax is owned by the projects module because the same
names are the `.vproject` matching key. E3 must expose/reuse the existing projects
validator rather than copy the private `project.cpp::IsTechnicalName` grammar into a
second authority.

Production catalog backing is immutable process-static data: the descriptor table,
dependency-ID arrays, technical/display strings and any descriptor-owned opaque data
outlive every editor `EngineHost`. `EditorApplication` retains the small shell
registration config as a member, just like `ProjectWorkspaceConfig`, because
`EngineHost::RegisterService` retains raw `userData` until service construction.
Never pass a Compose-stack config/span/pointer into the service descriptor. The shell
may borrow the immutable process-static catalog for its lifetime and owns only its
bounded validation/selection/order/active-state arrays; it does not copy the entire
compiled catalog merely to extend lifetime. Focused tests may use non-static fixture
catalogs only when the fixture explicitly outlives host Start/Shutdown.

Built-in shell extensions are explicitly marked and selected by the executable.
Project extension names select only matching compiled descriptors. An unknown,
unavailable or dependency-incomplete requested extension fails startup with the
offending technical name and stable failure code; it is not ignored and no DLL is
loaded from `paths.plugins`.

### Activation, rollback and teardown

`OnInitialize` validates catalog/selection/topology only; it does not activate an
extension. Activation runs in shell `OnStart`, only after the shell dependencies are
running and outside an active UI frame. Each extension receives a narrow activation context containing
its identity, workspace access and typed registrar/query surfaces approved for
E3.1. It does not receive `EngineHost` mutation, an ImGui context or a universal
service/message bus.

Each activated extension owns a bounded registration ledger. E3.1 establishes the
owner identity and rollback mechanism; later E3 registries add typed registration
kinds. Do not implement arbitrary untyped `void*` cleanup callbacks as the public
ownership contract. A registry either returns a generation-checked owned token or
supports removing all entries for one extension owner. For a valid live owner, the
lifecycle removal path must begin with idempotent, allocation-free, non-failing
logical owner revocation: after revocation returns, no callback, factory, visitor or
ordinary/new dispatch path may resolve that owner generation. The registry's own
owner-removal path retains teardown-only access to the already-owned records so it
can invoke required destroy/unsubscribe callbacks synchronously while extension state
is still alive; it may not create new entries or re-enter ordinary dispatch. Physical
cleanup may report a diagnostic afterward, but any unreclaimed record must already be
neutralized so later registry/shutdown code cannot call extension callbacks or
factory data. Teardown order is:

```text
stop new extension callbacks
  -> revoke owner callback/dispatch reachability
  -> destroy extension-owned instances/subscriptions in reverse order
  -> unregister extension-owned types/descriptors
  -> invoke extension deactivate as final private-state teardown
```

The extension's private state remains valid throughout owned-registration cleanup.
`deactivate` is the final state-teardown callback; it must not run before a panel,
subscription, type or descriptor whose callback/factory data can still reference
that state has been removed. E3.1 adds no separate extension quiesce hook: shell
admission closes first, and E3.1 has no extension worker callback source to drain.

If activation of extension N fails, first revoke N's owner generation, then remove
N's partial owned registrations before deactivating N; for N-1 through 0 in exact
reverse activation order, revoke the owner, remove its registrations, then invoke
its final `deactivate`, all **before `OnStart` returns failure**. Preserve the first
failure for reporting while performing complete logical revocation and best-effort
physical cleanup. Revocation/cleanup is idempotent and may not throw. A physical
cleanup diagnostic is allowed only after callbacks are already unreachable. The
service never reports successful start with a partial selected extension set.

`EngineHost` marks initialize/start attempts before invoking the callbacks. A failed
Initialize later receives Shutdown only; a failed Start receives Stop + Shutdown,
with no Quiesce/Drain. Normal unwind performs global reverse-order Quiesce, then
Drain, Stop and Shutdown passes. Therefore normal `OnQuiesce` closes admission,
walks extensions in reverse activation order, revokes each owner generation, removes
each owner's external registrations while `EditorUiService` is still live/mutable,
then invokes that extension's final `deactivate` only after its callback-bearing
registrations are gone. `OnDrain` proves no admitted callback or owned registration
remains. `OnStop` is an idempotent fallback/private-state cleanup path (not the
normal place to remove UI registrations), and `OnShutdown` asserts/clears final empty
state before dependencies disappear. Failed Start must self-unwind partial
registrations before returning because its Quiesce is skipped. Because `EngineHost`
destroys the dependent shell instance before its dependency instances, no path may
reach shell destruction while a dependency still owns a callback into extension
private state.

### Threading and callback boundary

Catalog validation, activation, registry mutation and teardown are main-thread
only. E3.1 adds no worker. An extension may retain a dependency pointer only while
the host's declared service dependency and its active lifetime prove that pointer
valid. Future worker producers communicate through typed bounded handoffs owned by
their domain; no worker may invoke extension or UI callbacks directly.

## Public status and diagnostics

Expose bounded read-only shell status sufficient for tests and later UI:

- catalog count, selected count and active count;
- shell lifecycle state;
- current/failed extension ID and technical name;
- stable failure category (invalid descriptor, duplicate, unknown selection,
  missing dependency, cycle, capacity, activation, cleanup/lifecycle); and
- activation sequence/generation suitable for rejecting stale handles.

Do not retain unbounded error strings or callback history. Human-readable messages
may accompany stable codes, and failures should also use the existing diagnostics
owner. Status is not a second log.

## Required implementation gates

### P0 - current-tree architecture proof and execution plan

After explicit authorization, create the planning triplet, refresh all mandatory
Vanguard and RED evidence, and record:

- exact shell service descriptor, dependencies and lifecycle-stage mapping;
- descriptor/catalog bounds and deterministic order;
- `.vproject` selection rules and built-in policy;
- rollback and reverse teardown algorithm;
- owned-registration representation that later typed registries can extend;
- precise failure/status model;
- focused Premake test target/seam and affected build targets; and
- the E0F boundary and confirmation that E2 implementation is not linked into the
  shell core translation units. The current editor executable already links
  `assetStreaming` for pre-existing work, so this is a source/API ownership rule,
  not a claim that the whole editor target has no E2 library linkage.

P0 is documentation/source study only. It closes after a fresh independent
current-tree review reports no blocker/high/medium contradiction. P1 then requires
separate explicit authorization.

### P1 - identities, descriptors and catalog validation

Implement the smallest toolkit-free public/private types for extension identity,
descriptor, scope/selection, dependency declarations, failures and status. Add a
bounded catalog builder/validator with deterministic topological ordering. Cover
invalid descriptors, duplicate ID/name, missing/self dependencies, cycles,
capacity and deterministic tie-breaking. Do not activate extensions yet.

### P2 - shell service and successful activation path

Register the main-thread `EditorWorkspace` shell service with the dependencies
locked in P0. Feed it the explicit immutable compiled catalog and retained project
selection. Activate built-in and selected extensions in validated order, expose
truthful status and prove that runtime/non-editor profiles do not initialize the
service. Do not add a dynamic loader or allow activation to mutate the host graph.

### P3 - owned registration, failure rollback and shutdown

Implement the bounded extension owner/ledger and complete partial-activation,
quiesce, drain, reverse deactivation, stop and shutdown behavior. Historically this
gate used a typed test-only probe registration because later real domains were not yet
implemented. That probe seam is now retired; current production ownership records are
real command/context/handler/panel registrations. Prove all handles/callbacks are
invalid after owner removal and that a fresh host can start after failure or clean
shutdown.

### P4 - historical static test extension and project selection integration

The original gate added a separately declared static test/probe extension outside
central host dispatch.
Exercise built-in-only, known project-selected, unknown project-selected and
dependency-selected configurations. The proof extension may record lifecycle and
owned test state only; no production panel/command/menu is part of E3.1. This fixture
is historical and no longer exists in the production extension catalog. Confirm
`paths.plugins` is not scanned or executed and the project descriptor is not
mutated.

### P5 - focused failure and lifecycle matrix

At minimum, execute deterministic tests for:

- empty catalog and built-in-only startup;
- multiple independent extensions and dependency chains;
- stable ordering regardless of input descriptor permutation;
- every descriptor/dependency/capacity rejection;
- unknown and dependency-incomplete `.vproject` selections;
- failure before registration, after one owned registration and after earlier
  extensions activated;
- exact reverse cleanup, cleanup idempotence and first-failure retention;
- quiesce rejecting new callbacks/registrations;
- no active registration or extension state after shutdown;
- stale owner/token rejection across a new activation generation; and
- editor-profile activation versus runtime/headless exclusion.

Tests must not require a GPU or interactive desktop for core lifecycle coverage.
Native editor startup is a separate integration claim.

### P6 - validation, documentation and acceptance

Run fresh Premake VS2022 generation, focused Debug and Development builds using
serialized outer MSBuild (`/m:1`), both focused test executables/configurations,
and the narrow editor bootstrap/workspace validation that P0 proves applicable.
Run `git diff --check`, explicit whitespace checks for untracked files, header-
layout checks and scoped searches for dynamic loading, duplicate service/panel
registries, E2 implementation leakage and E3.2+ scope drift. Record exact commands
and outcomes; do not imply interactive UI proof from headless tests.

Obtain a fresh independent current-tree review of ownership, rollback, shutdown,
bounds, project selection, tests and documentation. Resolve every blocker/high/
medium finding before marking E3.1 accepted. Update the E3 parent plan, roadmap,
document map and planning triplet, then stop. At E3.1 acceptance, E3.2 remained
unauthorized; its later separately authorized work is recorded in the E3.2 contract.

## Acceptance matrix

| Scenario | Required outcome |
|---|---|
| Valid built-in catalog | Deterministic activation; truthful active status |
| Known project extension | Activated only from compiled descriptor selection |
| Unknown project name | Startup fails with stable code/name; no partial shell |
| Missing dependency/cycle | Rejected before activation |
| Extension N activation failure | N partial state removed; N-1..0 reverse cleanup |
| Quiesce | New callbacks/registrations rejected before dependencies stop |
| Clean shutdown | Zero active extensions and owned registrations |
| New host generation | Old owner/tokens rejected; no stale callback |
| Runtime profile | Editor shell absent |
| Plugin directory content | Never scanned or executed by E3.1 |

## Explicit non-goals and stop boundary

- No command registry, shortcuts or input-context implementation (E3.2).
- No production panel/dockspace/focus implementation (E3.3).
- No menus, toolbars or context-menu composition (E3.4).
- No console, progress or backend status adapter (E3.5).
- No layout or preference persistence (E3.6).
- No final demonstration tool or E3 closure claim (E3.7).
- No DLL/plugin ABI, hot reload/unload, scripting, marketplace or directory loader.
- No change to E2 asset behavior, runtime streaming or rendering ownership.
- No broad editor directory reorganization or unrelated formatting.

The implementing agent stops after the authorized numbered gate, reports exact
validation performed, and leaves every later gate unauthorized.
