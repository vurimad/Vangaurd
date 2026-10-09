# Editor Asset Browser P0 - External Import and Adoption

## Status and authorization

The user explicitly authorized this new Asset Browser follow-up with `okay start` after the E4 browser was
manually exercised and the missing external-file ingestion workflow became visible. This is **not** the historical
Editor Foundation E4 P0. E4 remains closed; this document defines the follow-up Asset Browser P0 program.

Current status: **Definition and P0.1-P0.5 implementation are CLOSED. The focused Debug + Development backend/editor/
window/platform matrix is GREEN and the exact-current P0 adversarial review is 0 BLOCKER / 0 HIGH / 0 MEDIUM. The
ordinary Project Cardinal manual acceptance is the remaining P0.6 gate. `Replace Source...` remains deliberately deferred until production owns an identity/settings/
history-compatible source-replacement transaction.**

The first manual P0.6 pass exposed and closed one additional production catalog gap: physical source directories with zero
files were not represented because source inventory returned files only. Empty directories are now inventoried by the same
bounded physical scan, published as separate `SourceFolder` metadata in the SourceDatabase generation, and consumed by the
generation-fenced ProjectAssetCatalog. The browser still performs no filesystem traversal. Focused Debug + Development
regression proof covers empty/nested folders, directory-only add/remove refresh, import into an empty folder, and stale physical
destination rejection; the visible smoke must be repeated against the rebuilt editor.

## Why this phase exists

E4 deliberately stopped at adopting a `Candidate` that already existed beneath a writable project source root.
Its durable contract explicitly deferred arbitrary external-file selection/drop delivery because there was no
production platform seam for those paths at that time.

The completed backend is therefore only the second half of an ordinary user workflow:

`project source Candidate -> ImportCandidate -> VMETA/AssetId -> recook/publication -> preview`

The missing first half is:

`external physical file -> project source root`

Asset Browser P0 closes that gap without moving filesystem, identity, import, build, publication or runtime
ownership into the browser.

## Product objective

From an empty writable Asset Browser folder, a user can invoke **Import...**, choose one or more external files,
and have every accepted item enter the existing project asset pipeline as a normal authored source with durable
identity, build/publication status and restart-safe behavior.

The browser remains a frontend. It does not write `.vmeta`, allocate AssetIds, copy files with ad-hoc Win32/SDL
code, own a private import queue, cook assets, publish loose resources, or load runtime resources merely to browse.

## Exact-current baseline that must be reused

- `assets::SourceDatabase` remains project-source inventory, classification and authored-reference authority.
- `assets::AssetFileTransaction` remains durable authored-file mutation/recovery authority. It already supports
  create-only staging, conditional replacement, move and delete under configured writable roots.
- `asset_streaming::ProjectAssetPipeline` remains the shared editor/headless mutation, rescan/reconcile, undo/redo,
  automatic-recook and publication owner.
- `ProjectAssetPipeline::ImportCandidate` already performs exact Candidate generation/importer revalidation,
  importer-owned default description creation, bounded generated-identity collision admission, sidecar commit,
  history, rescan and existing recook handoff.
- `EditorProjectAssetAccess` already exposes Candidate import and adopted-asset mutation through the workspace owner.
- `ProjectAssetCatalog` already supplies generation-keyed metadata-only folders/search/rows without runtime loads.
- The current project workspace exposes one writable project source root at `ProjectWorkspace::assets`.
- `SourceRoot` currently has only physical directory + read-only state. The browser therefore has no user-facing
  root display metadata and currently renders `Root 1`.

## Exact-current gaps confirmed for P0

### Gap A - no external-source ingestion command

There is no shared production command that takes a physical file outside the managed source roots and creates a
new project source through the existing transaction owner. `ImportCandidate` intentionally requires the source to
already exist in the current `SourceDatabase` generation.

P0 must add this capability below editor UI and keep it usable by a headless frontend.

### Gap B - no production file-dialog capability exposed to editor extensions

No Vanguard editor/platform API currently exposes an open-file dialog. The bundled SDL3 already provides
`SDL_ShowOpenFileDialog`, including multi-selection, but it is asynchronous and its callback may execute on a
different thread. Asset Browser must not call SDL directly or mutate project state from that callback.

P0 therefore requires one reusable platform/editor dialog seam that copies callback results into bounded owned
storage and delivers completion back on the editor/main owner thread.

### Gap C - file-drop intent exists but delivery does not

`WindowFlag::AcceptFileDrop` exists, but the SDL window backend currently neither maps it to a drop-delivery
contract nor translates SDL drop-file events into Vanguard window/editor events. Drag/drop ingestion is therefore
a real later P0 gate, not a one-line browser callback.

### Gap D - root presentation is not user-facing

The catalog exposes root indices only. Asset Browser currently formats those indices as `Root N`. P0 must expose a
small presentation descriptor from the project/editor composition so the primary root reads **Project Assets**
(or an equally explicit configured project-facing label) without teaching `SourceDatabase` about editor labels.

## Frozen user workflow

### Import entry points

The first production UI exposes the same import action from:

1. an always-visible **Import...** action in the Asset Browser header/toolbar;
2. right-click on empty content space in a writable folder; and
3. right-click on a folder, importing into that folder.

Right-click on an asset remains asset-specific. It must not overload the asset context menu with an unrelated
destination import action.

### Destination semantics

- Import targets exactly one current writable project source folder.
- The default destination filename is the selected external file's basename, preserving its extension.
- P0 never overwrites an existing source or sidecar and never silently generates a numbered replacement name.
  A collision is an explicit per-item failure that leaves existing authored bytes untouched.
- Destination validation uses the same source-root containment/link policy as all other project mutations.
- External import does not accept an already-managed project source as a shortcut. Existing in-project Candidates
  use **Import Candidate**; adopted project sources use Move/Duplicate/Replace Source workflows. This prevents an
  ambiguous second meaning for importing a file already owned by the project.

### Importer admission

- Importer recognition is performed by the existing live `SourceImporterDescriptor::recognizeSource` callbacks.
- Exactly one live importer must recognize the destination source path. Unsupported or ambiguous files are rejected
  before any managed project file is created.
- The importer must expose the existing authoring hooks `createImportDescription` and `validateSettings`; otherwise
  the UI reports that the format is recognized but has no production default-import authoring contract yet.
- P0 does not duplicate importer extension lists in editor UI merely to populate a native file filter. The first
  dialog may show all files and authoritative importer preflight decides support. A later descriptor-owned display
  filter may be added if it proves useful.

Texture import is currently authoring-complete for png/jpg/jpeg/tif/tiff/exr/dds. Mesh and shader importers
currently recognize sources but do not expose first-import authoring defaults; extending those format-owned defaults
is a separate explicit format gate rather than hidden browser logic.

### Source truth and provenance

The copied file beneath Project Assets becomes the project's authoritative authored source. P0 does **not** persist
the user's original external absolute path into VMETA: such a path is machine-local and must not become portable
project identity.

Consequently:

- **Reimport** means rebuild/re-publish from the current project source bytes.
- A future **Replace Source...** action may select a new external file and transactionally replace those project
  source bytes while preserving AssetId/importer/output identity and existing settings when compatible.
- **Open Source Location** reveals the project-owned authored source, not the original external picker location.

## Shared backend contract

The external-import command is one authored transaction per file. The command must not implement "copy then later
adopt" as two unrelated durable commits. Successful external import atomically authors both the source file and its
VMETA sidecar so a crash/failed second step cannot strand a user-visible half-import created by the editor command.

The shared owner therefore needs a narrow external-ingest operation with semantics equivalent to:

1. validate owner/quiescent mutation state and current writable destination root;
2. require the destination source and sidecar both to remain absent;
3. derive the canonical root-relative source identity from the destination;
4. resolve exactly one live authoring-capable importer against the destination path;
5. create a frozen importer-owned default `SourceImportDescription` exactly once;
6. validate settings/output declarations using the existing rules;
7. generate AssetId/output identities with the existing bounded collision checks against project + runtime
   occupancy;
8. commit source bytes + encoded VMETA through one `AssetFileTransaction`;
9. capture both authored paths in the existing operation history so Undo removes both and Redo restores the exact
    same source bytes + VMETA/AssetId;
10. run the existing Rescan/Reconcile path and seed the existing automatic recook/publication owner.

The picker/import-coordination producer separately rejects a selected physical input that is already inside a managed
project source root before snapshotting it. P0.1 intentionally receives no original picker path, so machine-local
origin cannot leak into asset identity/provenance; an in-project Candidate continues to use `ImportCandidate`.

Build/cook/publication failure after authored commit never rolls back a valid imported source. The browser reports
authored/built/published/resident state separately exactly as E4 already does.

## Multi-file and cancellation semantics

- The UI may select multiple external files in one dialog.
- Each item is an independent durable transaction with an independent result. Earlier successful imports remain
  committed if a later item fails.
- Cancellation stops admission of not-yet-started items. It does not roll back already committed items.
- The result surface must distinguish at least: Success, Cancelled, Unsupported, Ambiguous importer, Authoring
  unavailable, Destination conflict, Read-only destination, Input unavailable/read failure, Limit exceeded,
  Transaction failure, Identity generation failure and Committed reconciliation/cleanup pending.
- No item may report Success until its authored source + VMETA commit is durable.

## Responsiveness / work ownership

External file reads and other project-sized preparation must not become unbounded per-frame browser work. The final
P0 implementation uses the existing cancellable asynchronous I/O owner for external source capture rather than
inventing a second asset scheduler. Durable transaction commit/reconciliation remains serialized by `ProjectAssetPipeline`.

The implementation plan must explicitly prove that native dialog callbacks never call Asset Browser, SourceDatabase
or ProjectAssetPipeline directly from a foreign thread.

The exact-current operation history also imposes a non-obvious requirement on create-only import. An import inverse
initially contains two absent files, so its current history payload is zero bytes; Undo later has to capture the now
present source + VMETA in order to make Redo exact. If that future replay cost is not reserved when the import history
record is pushed, older history can consume the budget and a perfectly valid import can become non-undoable. P0.1
therefore reserves the exact encoded source + VMETA replay cost in history accounting even when the inverse payload
itself is empty. Stored payload bytes remain separate from the charged reservation. Replay propagates the larger of the
captured opposite-state payload and the record's existing reservation, making the create-only reservation stable across
repeated Undo/Redo cycles. Import is rejected before commit if its future replay cost cannot fit
`maximumOperationHistoryBytes`.

The production project configuration currently makes `maximumOperationHistoryBytes == maximumOperationFileBytes`
(256 MiB). Because the future replay also contains VMETA, external source admission cannot simply allow a full
`maximumOperationFileBytes` source. P0.1 computes the admissible source limit from the live configuration and the
encoded VMETA size; it never relies on the nominal 256 MiB file limit alone.

## P0 implementation gates

### P0.1 - headless external-ingest backend

**Status: COMPLETE / focused proof GREEN (Debug + Development).**

Add and prove the smallest shared external-file import command below editor UI. This gate owns destination/path
validation, unique authoring-importer preflight, atomic source+VMETA transaction, identity generation, history and
normal reconciliation/recook handoff. No native file dialog or browser wiring yet.

The shared pipeline operation is source-content oriented rather than picker-path oriented: the managed destination
path and an owned/caller-stable byte snapshot are project-asset inputs; the original external absolute path is not
asset identity. The editor-side ingestion coordinator may asynchronously capture an external file, then hand that
owned snapshot to the normal owner-thread pipeline operation. This keeps the headless pipeline portable and prevents
machine-local picker paths from leaking into project truth.

P0.1 is split internally into two correctness gates before UI:

1. **P0.1a transaction/history primitive** - atomically create source + VMETA, share Candidate import's metadata and
   generated-identity validation, and add future-replay history reservation so Undo/Redo remains exact.
2. **P0.1b pipeline operation** - classify one unmanaged destination with the live importer registry, create the
   synthetic Candidate facts required by the existing importer-owned default hook, perform bounded identity
   admission, call the shared primitive, rescan/reconcile, and seed normal recook/publication.

External physical-file reading is deliberately not performed by P0.1b. That I/O belongs to the P0.2/P0.3 reusable
dialog/import-coordination layer, which uses the engine async-I/O owner and invokes P0.1b only after it owns a complete
bounded snapshot. The durable project mutation itself remains serialized on the project owner thread in this first phase;
large-file transaction streaming/worker commit is a later optimization only if profiling proves the bounded commit
stall unacceptable. P0 does not create a second mutation scheduler merely to hide that cost.

Exact-current implementation uses `ProjectAssetExternalImportRequest { destinationPath, sourceBytes }`, the shared
`assets::ExecuteAssetCreateAndAdopt` transaction primitive, and pure `assets::EncodeAssetImportMetadata` preflight so
staged/history admission charges the exact encoded VMETA length rather than the 1 MiB schema ceiling. Focused tests
cover non-mutating admission failures, exact source/VMETA/AssetId creation, replay-stable history under byte pressure,
normal automatic recook + loose publication, and restart/reconcile identity stability.

### P0.2 - reusable native open-file dialog seam

Expose asynchronous multi-file open through a reusable editor/platform capability backed by SDL3 rather than direct
Asset Browser SDL calls. Copy callback paths before return, fence callback lifetime, and deliver completion on the
main/editor owner thread. Cancel and backend error are distinct terminal states.

### P0.3 - Asset Browser import UX + root cleanup

Replace `Root N` presentation with meaningful root information. Add toolbar, empty-space and folder **Import...**
entry points. Queue selected files against a captured destination identity, revalidate that destination before each
commit, present per-file progress/results, and refresh/rebind through normal catalog generations.

### P0.4 - file drop

Finish the existing `AcceptFileDrop` intent through the window/SDL event stack and route dropped physical paths to
the same P0 import command. Drop is another input producer, never another import implementation.

### P0.5 - source lifecycle completion

Expose clear current-source actions: Reimport, Open Source Location, and only after its transactional contract is
defined, Replace Source.... Candidate adoption remains available for files placed into the project by external tools.

### P0.6 - focused proof / manual acceptance

Run focused Debug+Development backend/editor tests and then ordinary Project Cardinal manual smoke. No broad solution
build is required merely for this phase. Preserve the shared dirty checkout and serial build policy.

## Acceptance matrix

P0 does not close until all of the following are true:

- empty folder -> Import... -> one supported file -> durable adopted asset -> build/publication/preview;
- multi-select yields explicit per-item outcomes with no hidden overwrite or partial-file state;
- unsupported, ambiguous and recognized-but-authoring-unavailable inputs create no project file;
- destination source or sidecar collision creates no mutation;
- read-only/stale destination is rejected before commit;
- source bytes under Project Assets exactly match the selected external input on successful import;
- source + VMETA appear atomically from the authored-operation point of view and Undo/Redo covers both;
- generated AssetId/output identity collision retries remain bounded and never replace existing resource identity;
- restart preserves imported identity and reuses normal project build/cache/loose state;
- Reimport uses the project source and preserves AssetId/output contract;
- cancel leaves already committed items valid and prevents later items from starting;
- dialog callback execution cannot mutate project/editor owner state off-thread;
- browse/search/folder traversal still performs zero runtime resource requests;
- right-click empty space, folder and asset each produce context-appropriate actions;
- the primary source root is presented as a meaningful project-facing name, not `Root 1`;
- final exact-current adversarial review is 0 BLOCKER / 0 HIGH / 0 MEDIUM for the P0 scope.

## Explicit non-goals for this P0

- Grid/List/Column view redesign, favorites and saved searches.
- Reference Viewer / Size Map / Asset Audit equivalents.
- Source-control integration.
- Cross-project migration/export.
- Persisting machine-local original import paths in VMETA.
- Browser-owned cooker, file transaction, source database, build graph, runtime loader or GPU cache.
- Solving Asset Browser multi-instance product semantics; that discussion remains separate as requested by the user.
