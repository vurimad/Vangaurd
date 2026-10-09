# E3.7 demonstration extension and E3 closure plan

## Status and authorization

The user explicitly authorized **E3.7 P0**, **P1** and finally **P2** on 2026-09-28.

E3.7 deliberately uses only three gates. It is an integration/closure slice, not a new
editor subsystem:

- **P0** - architecture and acceptance-shape lock;
- **P1** - production demonstration extension implementation; and
- **P2** - focused compile/proof, documentation reconciliation and E3 acceptance.

**P0, P1 and P2 are CLOSED. E3.7 is ACCEPTED and Editor Foundation E3 is ACCEPTED.** P0
closed after exact-current architecture/acceptance locking plus lightweight document/static
hygiene. P1 then implemented the production demonstration extension and closed after exact-current
source/static review and lightweight hygiene. P2 completed the focused final compile/proof described
below. No broad E3.1-E3.6 regression matrix or interactive input-driving native automation ran.

## Purpose

Close Editor Foundation E3 with one real extension that consumes the framework rather
than adding another framework. The extension must prove that an independently registered
tool can use E3.1-E3.6 without changing central shell dispatch or reaching around the
extension registration surface.

The demonstration is production editor code, not a hidden test extension. It is compiled
into the normal production extension catalog and is **ProjectSelectable**, so ordinary
projects do not activate it unless their `.vproject` selects its technical name.

## Locked architecture

### Extension identity and catalog ownership

- Technical name: `editor-demo-tool`.
- Selection policy: `EditorExtensionSelectionPolicy::ProjectSelectable`.
- Stable extension id: `0x6564333764656d01ull`.
- Production provider: `GetEditorDemonstrationExtensions()` declared beside the other
  production extension providers and appended by `GetEditorProductionExtensions()`.
- The production catalog remains the sole compiled catalog owner. E3.7 adds no second
  catalog, DLL loader, plugin directory scan, ABI or hot-unload path.

### Public E3 surface only

Activation/deactivation must use `EditorExtensionActivationContext` and the owned access
it exposes. The extension may allocate its own bounded private state, but it must not
obtain or mutate `EditorCommandService`, `EditorUiService`, `EditorShellService`, panel
registry internals, UserState internals, surface-registry internals or ImGui settings
storage directly.

The demonstration registers exactly one coherent tool surface:

1. **Panel** - one singleton panel, default instance `1`, with stable type identity and a
   simple bounded state display/control.
2. **Command** - one command with one handler in the existing Workspace context. The command
   mutates only the extension's private demonstration state, ensures the demo panel is open,
   and uses `Ctrl+Shift+D` as its default shortcut. No new context type is needed.
3. **Main menu** - one command contribution under the existing Tools anchor.
4. **Main toolbar** - one command contribution placed against the existing toolbar anchor.
5. **Context menu** - one extension-owned context-menu surface requested by the panel and
   containing the same command.
6. **Status/activity** - one `EditorActivityKind::Status` item from one extension-owned
   activity source. It is bounded, non-cancellable and reflects the same private state.
7. **Typed preference** - one boolean user preference (for example, whether detailed demo
   status is shown), registered through `RegisterPreferenceProvider`. Its revision changes
   only when the preference changes and it is captured/validated/applied through the
   normal E3.6 path.

The same command identity is used by direct command routing, menu, toolbar, context menu
and shortcut routing. E3.7 must not invent per-surface behavior callbacks.

### Stable identity allocation

The P1 implementation uses these E3.7-owned stable identities; names are descriptive in
source and must not contain phase/gate terminology:

| Role | Stable value |
|---|---:|
| Extension | `0x6564333764656d01` |
| Panel type | `0x65643337646d7001` |
| Command | `0x65643337636d6401` |
| Context-menu surface | `0x6564333763746d01` |
| Main-menu contribution | `0x656433376d6e7501` |
| Toolbar contribution | `0x6564333774627201` |
| Context contribution | `0x6564333763746301` |
| Activity source | `0x6564333761637401` |
| Preference provider | `0x6564333770726601` |
| Preference field | `0x6564333770726602` |

Panel instance id and activity item id are each `1` within their owning stable type/source.

### Lifetime and failure rules

- Shell ownership remains authoritative. Every command, handler, panel type,
  panel instance, surface contribution, activity source and preference provider is owned
  by the extension activation generation.
- Activation is all-or-fail. Returning false relies on the existing Shell owned-registration
  rollback; no registration may survive a failed activation.
- Private state lifetime follows the existing production-extension pattern: allocate before
  registration, publish through `privateState`, and release in deactivation after Shell has
  revoked owned callbacks/registrations.
- Panel callbacks and activity/preference callbacks touch only live extension state.
- No callback performs filesystem, asset-build, renderer, world or long-running work.
- No test mode, automation flag, telemetry hook or phase marker is added to production code.

## P0 - architecture and acceptance-shape lock

P0 closes when the exact implementation boundary above is documented against the current
E3 APIs and the final proof is narrowed to what is necessary for E3.7. P0 changes planning
and documentation only.

Required P0 checks:

- verify the current production catalog aggregation seam;
- verify `EditorExtensionActivationContext` exposes every required owned registration;
- verify panel draw can request the extension-owned context menu;
- verify activity Status rows and typed preferences already have bounded ownership;
- verify `editor/premake5.lua` already includes production `extensions/**.cpp` so P1 does
  not need a new production build system path;
- lock P1/P2 scope and explicit non-goals; and
- run only lightweight documentation/static hygiene.

## P1 - production demonstration extension

**Status: CLOSED; explicitly authorized 2026-09-28.**

Implement the single production extension described above. Expected production changes are
narrow: one extension implementation, one provider declaration and one production-catalog
aggregation entry. Existing registries/services remain unchanged unless exact-current source
shows a genuine missing public capability; such a gap must be reported before widening scope.

P1 added `editor/extensions/editor_demonstration.cpp`, declared its production provider and
appended that provider to the existing production catalog. It added no service, registry,
build-system path, test hook or private-owner dependency. P1 ran no build/test/native execution;
the focused final compile/proof remains P2-owned.

## P2 - focused final E3 proof and closure

**Status: CLOSED; explicitly authorized 2026-09-28. E3.7 and E3 are accepted.**

P2 is the final gate, so it follows the repository final-gate rule: leave the relevant
source compiling and run the smallest focused proofs needed for closure. It must not revive
the historical broad all-E3 regression matrix.

The locked final proof surface is:

1. fresh Premake VS2022 generation;
2. serialized x64 Debug + Development compile/link of the production editor containing the
   demonstration extension;
3. one focused demonstration-extension proof under `editor/tests/**` that establishes
   project selection, activation, owned registration shape, same-command routing,
   status/preference behavior and complete owner teardown without product test hooks;
4. one noninteractive Debug native/lifecycle run using a test-owned project that selects
   `editor-demo-tool`, proving the production catalog can activate it and shut down cleanly;
5. reuse already accepted E3.1-E3.6 evidence for generic docking/input/persistence machinery
   instead of rerunning their broad matrices; and
6. final source/static hygiene and documentation reconciliation.

If a final proof genuinely requires physical mouse/focus-driving native automation, warn the
user immediately before that execution. It is not a default P2 requirement because E3.3
already established the generic dock/detach/input behavior.

### P2 executed evidence

The final gate stayed intentionally narrow:

- fresh `premake5.exe vs2022` generation: **PASS**;
- `editorShellTests` x64 Debug and Development, serialized outer MSBuild `/m:1`: **PASS**;
- focused `editorShellTests --demonstration-extension` in Debug and Development: **PASS**;
- production `editor` / `VanguardEditor.exe` x64 Debug and Development compile/link with the
  demonstration extension in the production catalog: **PASS**;
- `editorApplicationLifecycleIntegrationTest` x64 Debug compile/link: **PASS**;
- noninteractive isolated-project run selecting `editor-demo-tool` with
  `--demonstration-extension-path`: **PASS, exit 0**. That path uses the real production catalog,
  command service, panel/UI owner, Activity service and Shell typed-preference owner; it invokes
  the demo command, observes the resulting Status row, applies/recaptures the boolean preference,
  then requests an ordinary clean exit;
- scoped `git diff --check`, explicit trailing-whitespace search and production contamination/
  private-owner search: **clean**, apart from the existing Git LF-to-CRLF warning for
  `editor/premake5.lua`.

The first focused Shell-test link exposed that the deliberately small fixture did not link the
production definitions of `EditorActivitySourceWriter::Add` and
`EditorPanelContext::RequestContextMenu`. P2 kept that fixture small by adding explicit
test-only link shims under `editor/tests/support/`; the real production editor and lifecycle
executable compile and execute the real implementations. No product behavior was altered for
that fixture linkage.

No broad E3 regression suite was rerun. Generic docking/input/persistence and activation-failure
rollback remain supported by the already accepted E3.1-E3.6 evidence and the exact-current Shell
ownership contract; E3.7's new production consumer was proven by the focused executions above.

## E3.7 acceptance matrix

| Scenario | Required outcome |
|---|---|
| Extension absent from project selection | Normal editor behavior; demo stays inactive |
| Project selects `editor-demo-tool` | Descriptor resolves from the compiled production catalog and activates |
| Demo panel active | Stable singleton instance exists; the Workspace command can reopen it after close |
| Direct/menu/toolbar/context/shortcut command path | Same command identity and handler semantics |
| Demo status | One bounded status row reflects current demo state without backend work |
| Demo preference | Normal E3.6 capture/validate/apply round trip; no private UserState access |
| Activation failure in focused fixture | Shell rollback leaves zero demo-owned registrations |
| Deactivation/shutdown | Owner revocation removes every demo contribution/callback before state release |
| Production editor compile | Debug and Development compile/link successfully at final gate |

## Explicit non-goals and stop boundary

- No dynamic native plugin ABI, directory loader, hot reload/unload or scripting.
- No asset browser, document model, hierarchy, inspector, viewport tooling or gizmos; those
  belong to later editor roadmap phases.
- No new command, panel, menu, activity, persistence or service architecture.
- No second application loop, service container, registry, scheduler or persistence file.
- No visual-theme overhaul.
- No broad E3.1-E3.6 regression rerun merely to close E3.7.

E3.7 is the final E3 gate. E4 and all later editor-roadmap work remain unauthorized until the
user explicitly starts them.
