# Editor Session State and Layout Persistence

## Purpose

Vanguard Editor must resume the user's working environment instead of rebuilding it on every launch, and it must support reusable named layouts without changing project or document content.

This design extends the existing `EditorUserStateService`; it does not add a parallel layout service, settings store, ImGui ini file, serializer, panel-specific persistence file, or crash-recovery owner.

The existing service remains the single persistence authority for editor UI/session state. The existing ImGui memory serialization remains the dock/window-layout codec. The existing panel registry remains the panel identity/instance authority. The existing filesystem publication primitives remain the durability mechanism.

## Product model

### Session state

Session state is automatic and continuous. It contains the current presentation plus content context needed to resume the editor where it was left.

P0 session state includes:

- primary-window placement, monitor, mode, maximized state and visibility intent;
- ImGui dock tree, split ratios, tabs, floating/detached windows and active tabs;
- panel existence/visibility and stable `(panel type, instance)` identity;
- panel presentation state;
- panel content state;
- typed preferences and shortcut overrides currently owned by user state;
- document/content restore intent where a production owner exists;
- active tool/mode restore intent, with Play mode always normalized back to edit mode.

Unsaved document contents are explicitly not session-state data. Session restore may reopen a document so a separate recovery system can offer recovery, but this subsystem never becomes a second recovery owner.

### Named layout

A named layout is explicitly user-saved and reusable across projects. It contains presentation only:

- primary/floating window arrangement;
- dock tree and tab arrangement;
- panel visibility and instance arrangement;
- panel presentation blobs.

A named layout never contains content state. Applying one must not change project, close documents, discard unsaved work, reopen content, change Asset Browser location/search/filter scope, or restore another panel's content selection.

Panels absent from a target layout are hidden, not destroyed. Their content/local state remains resident and can reappear unchanged when the panel is shown again.

## Ownership

`EditorUserStateService` evolves into the editor session/layout persistence authority while preserving its existing service identity and capability during migration.

It owns:

- artifact discovery and storage paths;
- parsing/encoding;
- schema versioning and migration;
- capture orchestration;
- autosave/checkpoint publication;
- last-known-good fallback;
- named-layout CRUD/apply;
- per-artifact failure isolation;
- multi-instance session promotion;
- startup restore and safety fallback.

It is the only editor code that reads or writes persisted session/layout files.

`EditorUiPersistencePrivateService` continues to own ImGui layout byte capture/apply and monitor repair for detached viewports. `EditorPanelRegistryCore` continues to own panel identity/instances. Panels only implement an optional state-provider contract and never touch persistence files.

## Serialization format

The exact-current audit found that Vanguard already owns one canonical human-readable wire grammar: `.vproject`. It is versioned UTF-8 text with LF line endings, deterministic field order, a `<format> major.minor` header, `key = "value"` assignments, `#` comments, and JSON-compatible string escapes. Its lexical implementation currently lives privately in `source/projects/src/project.cpp`.

P0 promotes that existing lexical grammar into reusable `source/serialization` text-document primitives and refactors `projects::Parse` / `projects::Write` to consume them. Session/layout persistence then uses the same shared codec. This is a reuse/extraction of an existing Vanguard format, not a new editor-only JSON/YAML serializer or third-party dependency.

The shared text codec provides:

- deterministic canonical output suitable for diffs;
- explicit document kind and schema version;
- bounded UTF-8 parsing with Vanguard `serialization::Result`-style failures;
- the existing `.vproject` quoted-string escaping rules;
- a stable ASCII field-key grammar;
- allocation-light field iteration over source text plus caller-owned decoded strings;
- schema-level typed values encoded canonically as quoted scalars;
- repeated hierarchical fields for ordered collections;
- canonical chunked text encoding for opaque panel bytes where no native text representation exists;
- preservation of unconsumed key/value fields so a newer document is not downgrade-destroyed.

ImGui's existing settings payload is already text. Session/layout documents therefore store it as ordered text records/chunks instead of base64-wrapping the complete ini image. Opaque binary panel-provider payloads use deterministic bounded chunk encoding until/unless a provider chooses a text-native payload contract.

New session and layout artifacts use this shared text codec. Current binary `VUSR` 1.0 remains a supported legacy migration input only. The existing project-local `Saved/Editor/user-state.vstate` is not silently abandoned or overwritten before successful migration.

The new text format may retain the `.vstate` extension because the document preamble identifies the encoding and schema. Binary-vs-text detection happens before schema dispatch. Named layouts use a distinct layout artifact name/extension so corruption and failure policy are isolated from session state.

## Storage and artifact model

All user-local storage is rooted through `filesystem::paths::GetUserEditorDirectory()`; editor code does not discover AppData directly.

The logical P0 layout is:

```text
<UserEditorRoot>/Vanguard/
  Sessions/
    Global/
      Instances/<instance-id>.vstate
      latest-checkpoint.ref
      last-session.ref
    Projects/<project-id>/
      Instances/<instance-id>.vstate
      latest-checkpoint.ref
      last-session.ref
  Layouts/
    <layout-id>.vlayout
    index.vlayout-index
```

Exact path spelling may follow existing `AbsolutePath` conventions, but these ownership rules are fixed:

- automatic checkpoints only rewrite the current process's instance record;
- every successful checkpoint atomically updates `latest-checkpoint.ref` to that already-validated instance record; racing processes may replace only this small reference, never each other's session payload;
- clean exit atomically updates the relevant `last-session.ref` to that validated instance record;
- one process never autosaves over another process's live instance record;
- personal layouts are per-user and cross-project;
- project-shared layouts are not part of P0 and remain a P2 feature;
- shipped built-in layouts are read-only and remain a later P1 surface.

Project sessions are keyed by stable `projects::ProjectId`, not only by filesystem path.

Startup considers both references. A valid `latest-checkpoint.ref` is the crash-resume candidate; `last-session.ref` records the most recently cleanly closed instance. Each session payload carries a non-zero per-instance `session.checkpoint.sequence`, and each reference carries the exact `checkpoint.sequence` it committed. The referenced payload must match that sequence (canonical or LKG) before it is eligible. This detects the crash/race window where an instance payload was replaced but its tiny reference was not, without inventing a process-global clock or ordering between concurrently running editors. Racing successful reference publications remain last-writer-wins. This separation is what permits a force-killed process to resume from its last committed checkpoint while preserving the requested clean-exit semantics for `last-session.ref`.

Legacy migration source remains:

```text
<ProjectSavedRoot>/Editor/user-state.vstate
```

When no user-local project session exists, the service may import a valid legacy VUSR 1.0 file into the new project-session artifact. The legacy file remains intact until the new artifact is validated and published.

## Durability and recovery

Every persisted artifact uses the existing filesystem publication primitives and the current VUSR stage/validate/replace discipline generalized behind one artifact publisher.

For each mutable artifact:

1. encode a complete candidate image;
2. write a unique same-directory stage with exclusive creation;
3. flush the stage;
4. reopen and fully validate it;
5. rotate the last validated canonical to one last-known-good sibling;
6. atomically replace the canonical;
7. remove only stages owned by this process;
8. periodically remove stale abandoned stages using a bounded age/count policy.

Load order is canonical, then LKG, then defaults. A bad named layout must not inhibit session autosave; write inhibition becomes artifact-local instead of service-global.

Documents from a newer editor are parsed conservatively: understood fields are consumed, unknown fields/panel payloads are retained verbatim through the text document model, and an older writer must not downgrade-destroy them.

One corrupt panel blob resets only that panel's state. One missing plugin leaves its opaque panel payload preserved for a later run when the plugin returns.

## Save cadence and threading

Existing 2-second quiet-window autosave behavior is retained for meaningful UI changes and extended to all session-state providers.

P0 also adds a dirty periodic checkpoint with a target cadence of 45 seconds so a process kill normally loses only a short window even if unrelated edits keep resetting the ordinary quiet timer. Active native/ImGui drag-resize-layout interactions remain an explicit exception: disk publication is deferred until the interaction settles, as required below.

Meaningful dirtiness includes:

- dock/layout revision;
- native window placement/mode/maximized revision;
- panel open/visibility revision;
- panel-state provider revision;
- shortcut/preference revision;
- document/content-context revision when such an owner exists.

Disk writes remain suppressed during active drag/resize/layout settling and publish after the interaction settles. Primary-native settling is tracked independently from ImGui settling; the 45-second checkpoint never overrides either active-interaction guard.

Main-thread work captures bounded immutable snapshots only. Encoding and filesystem I/O should move off the main thread when existing service/job ownership permits, with a main-thread target below 1 ms. Clean-exit quiesce still performs an authoritative final publication before dependent UI/window teardown.

## Primary native window

The current editor primary is always created visible, centered, 1920x1080. P0 changes editor startup so the primary window is created hidden with the ordinary default geometry, then `EditorUserStateService` applies restored state before first show.

The service captures/restores:

- last normal logical position;
- last normal logical extent;
- display/monitor association;
- window mode/fullscreen where applicable;
- maximized state.

Minimized state is not restored as a startup intent; a prior minimized editor reopens usable rather than hidden in the taskbar.

`WindowManager` synchronizes its requested placement to backend event state, so a maximize event does not retain an independent normal restore rectangle. `EditorUserStateService` therefore tracks the last non-minimized/non-maximized primary placement while running and persists that normal rectangle separately from the maximized flag. It consumes the existing window event journal by state revision so a normal move/resize immediately followed by maximize/fullscreen in the same editor frame is not lost; move/resize events belonging to the same native revision as a maximize/minimize/mode transition are deliberately not mistaken for a normal rectangle. Startup applies the repaired normal rectangle first and then requests maximize. This keeps a later in-session Restore action returning to the user's pre-maximize size instead of the monitor-sized maximized bounds.

Restore reuses the existing `ResolveEditorUiSavedPlacement` topology/DPI/work-area mapping. No second monitor-repair implementation is introduced.

Startup sequence:

1. `WindowService` creates the editor primary hidden;
2. editor UI/services start against the hidden native window;
3. user state loads canonical/LKG/legacy/default;
4. primary placement is repaired against current displays and applied through `WindowManager::RequestState`;
5. maximized state is applied through existing window action/state APIs;
6. ImGui layout and panel instances are restored;
7. panel restore intent is handed to providers;
8. primary visibility is enabled;
9. data-dependent panel intent finishes asynchronously as its data becomes available.

If state is missing/corrupt/unusable, the same sequence shows the ordinary default window/layout without a flash of an intermediate arrangement.

## Panel persistence contract

Panel identity remains the stable `(EditorPanelTypeId, EditorPanelInstanceId)` pair.

Panel types may register an optional persistence descriptor containing conceptually:

```text
StateVersion
CaptureState(scope) -> opaque bytes or nothing
RestoreState(scope, version, bytes) -> result
MigrateState(scope, fromVersion, bytes) -> migrated bytes/result
GetStateRevision() -> monotonic revision
```

`scope` is either `Presentation` or `Content`.

Session capture stores both scopes. Named layouts store presentation only.

The central service treats provider payloads as opaque bounded bytes and encodes them canonically in the text artifact. Provider failure is isolated to that panel instance/scope. A syntactically valid artifact with one semantically corrupt provider blob quarantines that blob instead of rejecting unrelated session state. Missing types/plugins retain their opaque payloads unchanged. Future-minor unknown `panel.<instance>.*` fields retain the decoded parent `(type, instance)` provenance and are only re-emitted while that exact panel identity still exists; instance reuse by another type must not inherit the old provider's unknown fields.

Panel providers must tolerate missing fields and older compatible versions. Data-dependent values are restored as intent; restore callbacks must not block startup waiting for project/catalog/world data.

## Asset Browser first adopter

The Asset Browser is the first panel provider.

Presentation state includes:

- Grid / Compact Grid / Details mode;
- thumbnail size;
- sort descriptor;
- browser split widths and other presentation-only pane sizes.

Session/content state includes:

- durable navigation `{root, folderPath}`;
- durable expanded tree locations;
- tree/content scroll intent;
- selected folder/asset by stable identity where available;
- search text;
- import/build/status filter chips and scope;
- recursive scope;
- navigation history.

`ProjectAssetCatalogFolderId` and every generation-local catalog handle are forbidden in persisted state.

## Other P0 adopters

All use the same provider contract.

- Viewport/Scene: camera pose/orientation/projection, view/shading mode, gizmo/snapping/grid/overlay presentation, viewport arrangement; world/content identifiers remain session-only.
- Inspector: locked/pinned target in content scope; collapsed section presentation.
- Hierarchy/outliner: expansion/scroll/filter intent with stable content IDs where available.
- Console/log: filters, level toggles and scroll-lock.

If a current owner does not yet expose a stable production concept (for example a general asset-document manager), the persistence contract is defined but no fake owner is invented.

## Runtime layout application

The startup-only `LoadUserStateLayout` invariant is preserved. A separate private live-apply path is added for named layouts.

Live apply is admitted only between ImGui frames and while no dock/window drag/resize is settling. It:

1. captures current content state so it cannot be overwritten by layout application;
2. reconciles target panel instances exactly for presentation: create/show target instances, hide live instances absent from target, never destroy solely because they are absent;
3. applies target primary/floating window presentation;
4. loads target ImGui settings bytes;
5. repairs detached placements against current monitor topology;
6. rebuilds/resynchronizes cached dock IDs and panel placement caches;
7. restores target panel presentation blobs only;
8. restores the preserved content state if any provider was transiently affected;
9. marks automatic session state dirty, but does not modify the saved named layout.

Documents and unsaved work are never closed or replaced by this path.

## Named layout model and commands

P0 named-layout commands are registered through the existing editor command service and surfaced under `Window / Layout` (or the closest existing menu anchor if `Window` is introduced by the shell surface model):

- Save Current Layout As...;
- Save Layout;
- Switch Layout;
- Revert Layout;
- Rename Layout;
- Duplicate Layout;
- Delete Layout;
- Reset to Default Layout.

The active layout has a stable layout ID separate from display name. Names are validated for non-empty value, uniqueness and invalid filesystem/control characters. Overwrite/delete require confirmation or an undoable/non-blocking equivalent already supported by the shell.

The modified marker is computed from a canonical hash of the current presentation snapshot versus the saved active-layout presentation. Session/content changes do not make a layout appear modified.

P0 never silently saves arrangement edits back into a named layout. The proposed P1 preference `Auto-save changes to active layout` remains opt-in.

Command-palette search integrates through the real command-search owner when present; layout code does not invent a private palette.

## Content/bootstrap limits

The editor currently requires an explicit `.vproject` path before workspace composition. The project-local session service therefore cannot by itself implement `last opened project`.

The same persistence module will later own a small global pre-workspace index beneath the user-editor root, containing only bootstrap-safe facts such as last project ID/path and the last global session reference. This is not a second settings system.

If that project is missing/moved, bootstrap falls back to the project picker once a production picker owner exists. Until then, the explicit-project launch contract remains unchanged; P0 session work must not invent a fake picker.

Likewise, open asset-editor/document restoration becomes active only as real document owners exist.

## Migration

The first schema change must include a production VUSR 1.0 migration decoder before any writer stops producing/accepting that legacy state.

Migration rules:

- validate legacy binary VUSR with the existing binary serializer;
- decode panels, ImGui bytes, shortcuts and preferences;
- map them into the new text session schema;
- synthesize missing primary-window and panel-blob fields as defaults;
- publish the new user-local instance artifact only after complete validation;
- preserve the legacy project-local file;
- report the load as migrated;
- never rewrite a newer/unknown source document destructively.

Every released text schema then gains an explicit migration step to the next supported schema.

## Safety reset and failure behavior

P0 exposes both:

- a command-line force-default-layout option consumed before user-state layout application; and
- a discoverable editor menu command.

Force-default bypasses session/layout presentation restore but does not delete persisted files. It allows recovery from unusable layout state without manual filesystem intervention.

Missing project/content/plugin items generate at most one non-blocking startup notice per category. Parse/corruption fallback also generates one non-blocking notice and logs the concrete cause. Startup itself remains successful whenever the normal editor can otherwise compose.

## P0/P1/P2 boundary

P0 contains:

- automatic per-user/per-project session restore;
- global no-project fallback storage plumbing;
- primary/floating window and dock restoration;
- generic panel state contract;
- Asset Browser plus required core-panel adopters;
- personal named layouts and live switching;
- per-instance session files;
- LKG/corruption recovery;
- safety reset;
- migration and focused automated coverage.

P1 contains built-in read-only layouts, layout import/export, layout-switch shortcuts, selection restoration where cheap/stable, and optional auto-save-to-active-layout.

P2 contains source-controlled project-shared layouts and contextual/event-associated automatic layout switching.

Project-shared layouts are intentionally not pulled into the first implementation.

## Validation contract

The implementation must ultimately prove:

- session save -> restore -> canonical compare;
- migration from every released schema, beginning with binary VUSR 1.0;
- corrupt and truncated canonical with LKG fallback;
- monitor removal/reorder/resolution/DPI changes;
- concurrent editor instances without interleaved/corrupt writes;
- per-panel `RestoreState(SaveState())` idempotence;
- arbitrary multi-monitor/floating/split-view editor state resumes visually and functionally;
- process kill loses only the bounded checkpoint window;
- switching layouts preserves project, documents, unsaved work and panel content state;
- disabling then re-enabling a panel plugin preserves opaque state;
- force-default starts the editor without deleting state.

Repository policy forbids broad tests during intermediate implementation. These proofs are accumulated as focused source/static review and executed at the final authorized validation gate, except where a genuinely new serialization subsystem requires the smallest viability proof.
