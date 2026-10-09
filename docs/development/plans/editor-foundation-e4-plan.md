# Editor Foundation E4 - Asset Browser and Runtime Delivery

## Status and authorization

The user explicitly authorized **E4 definition work**, then **P0**, **P1**, **P2 production implementation**,
**P3 production implementation**, later **P4.0, P4.1 and P4.2**, the pre-P4.3 editor-wide project-texture
generalization correction, **P4.3**, **P4.4**, then **P4.6**, later **P4.5**, then **P4.7**, **P4.8**, **P4.9**,
**P5.1**, **P5.2**, **P5.3** and **P6**. P2 production authorization was the exact user
message `okay start`; P3 was explicitly authorized on 2026-10-03 with
`okay start P3, re-evaluate first so you dont make something already there`; P4.0/P4.1 were explicitly authorized
on 2026-10-03 with `ok start P4.0 and .1`, followed by P4.2 with `start .2`; the pre-P4.3 correction was explicitly
authorized with `Yes start` after the user clarified editor-wide texture-preview reuse, P4.3 was explicitly
authorized on 2026-10-04 with `okay start`, P4.4 was explicitly authorized on 2026-10-04 with `start next`, P4.6
was explicitly authorized on 2026-10-04 with `okay start (be quick, dont compile or run tests as ususal)`, and the
user later returned to the skipped **P4.5** with `start next`, explicitly authorized **P4.7** with `start`, authorized
**P4.8** with `start .8`, authorized **P4.9** with `start`, then authorized **P5.1** with `start`, **P5.2** with
`P5.2`, **P5.3** with `okay start .3`, and **P6** with `ok start`.
Those
authorizations are independent of the preceding Editor visual foundation's formal acceptance record: no
separate user message explicitly closing/accepting visual P2 is recorded before E4 Definition authorization.
This document is the durable E4 contract produced and advanced through those authorized gates.

**Definition, P0, P1, P2, P3, P4.0, P4.1, P4.2, the pre-P4.3 generalization correction, P4.3, P4.4, P4.5,
P4.6, P4.7, P4.8, P4.9, P5.1, P5.2, P5.3 and P6 are CLOSED; E4 is CLOSED. P4 remains closed at its
source/static gate, while P6 is the completed focused execution/acceptance gate.** P3 closed on 2026-10-03 at the source/static gate after the required exact-current
re-evaluation, frontend review and final backend/facade recheck all completed without unresolved BLOCKER/HIGH/MEDIUM
findings. No Premake generation, compilation, executable tests or native editor execution was performed for P3.

The accepted backend/editor prerequisites remain closed:

- Project Asset Foundation E1/E2;
- Editor Foundation E3.

The visual foundation remains the current presentation baseline, but E4 Definition does not invent or
retroactively infer a missing user P2 acceptance. Exact-current visual source contains the later stale
borderless hit-test correction; that correction and any remaining visual-acceptance bookkeeping stay in
the visual-foundation phase. E4 must consume all accepted owners rather than reopening them.

## Objective

E4 turns the accepted project-asset backend into the editor's first real high-surface content tool.
It has two inseparable responsibilities:

1. provide a production multi-instance **Project / Asset Browser** over the current project source
   catalog, import/build/delivery state and authored reference index; and
2. close the ordinary editor workflow from a supported project source through explicit first import,
   build/reimport, committed loose publication and runtime-backed preview/loading.

The browser is a frontend over existing authorities. It is not a file-backed shadow database and it
must not become an editor-only cooker, resource loader, dependency graph or filesystem transaction
owner.

## Exact-current production baseline

### Project/source authority already exists

`assets::SourceDatabase` is the project source inventory and authored-reference authority. Its current
rows already retain:

- physical source path and stable source identity;
- root/read-only facts and source/metadata issues;
- parsed VMETA identity, importer version, settings and declared outputs;
- source classification/import state;
- source/primary output resource types; and
- authored forward/backward reference scan state.

`SourceQuery` plus `VisitResources` already provides allocation-free metadata traversal with folder,
recursive, type, issue, import-state and reference-state filtering. `GetForwardReferences` and
`GetBackwardReferences` already provide the authored dependency/referencer views needed by E4.
Browser rows therefore do not need VADI or runtime loads.

Rows and returned spans are borrowed from the current catalog and are invalidated by later catalog
replacement. E4 UI code must never retain `SourceRecord*` or borrowed reference spans across owner
ticks.

### Shared project mutation/build authority already exists

`asset_streaming::ProjectAssetPipeline` is the accepted owner-thread integration and command surface.
It already owns or composes:

- source rescan/classification/reference reconciliation;
- durable `AssetFileTransaction` recovery and source mutation;
- move/rename, duplicate, retarget and delete;
- undo/redo for those committed authored mutations;
- directory observation and bounded authoritative reconciliation;
- automatic incremental recooking and explicit reimport;
- `ProjectAssetBuildSystem`, BuildGraph, VADI and VDDC ownership;
- build freshness/status queries;
- committed loose-resource materialization/publication;
- runtime delivery/publication status; and
- bounded VDDC/loose maintenance.

E4 commands must delegate to this surface. The editor panel must not call filesystem mutation,
`BuildGraph`, `DependencyIndex`, VDDC, `LooseResourceMaterializer` or `ResourceStreamer::PublishLoose`
directly.

### Runtime delivery already exists

Committed outputs already flow through `ProjectLoosePublisher` / `LooseResourceMaterializer` into the
ordinary `ResourceStreamer` loose overlay. Runtime requests use the existing decoders and immutable
resource generations. The editor workspace already performs bounded texture-generation replacement
through `ResourceStreamer::Request` / `Replace` and `TextureResidencyRuntime::ReplaceTexture`, and only
reports a resident delivery after the renderer accepts the new generation.

E4 preview must consume that same path. There is no E4-specific resource format, preview decoder or
parallel loose registry.

### E3 already supplies the editor framework E4 needs

The accepted editor shell already provides:

- multi-instance panel types and retained panel instance state;
- panel focus reconciliation;
- Local command contexts with the focused panel's `receiverData`;
- extension-owned command registration and teardown;
- transactional context-menu surfaces;
- declarative menu/toolbar contribution;
- Background Activity / progress presentation; and
- layout and typed preference persistence.

The E4 browser must use those contracts rather than implementing private shortcut routing, popup
dispatch or another settings store.

## Definition gaps exposed by the first real consumer

E4 is intentionally allowed to add bounded missing capabilities when the browser proves they are
actually absent. Definition identified the following real gaps.

### Gap A - explicit first import/adoption is not exposed

`SourceDatabase::ClassifySources` correctly marks a recognized source without VMETA as
`SourceImportState::Candidate`, recording the unique live importer and its available version. VMETA
also states that a new source receives its durable `AssetId` when it is adopted.

However, the exact-current public `ProjectAssetPipeline` command surface exposes reimport, move,
duplicate, retarget, delete and undo/redo, but no production first-import/adoption command. Tests and
fixtures currently manufacture VMETA themselves; a production editor must not do that.

E4 therefore adds the **smallest shared first-import capability** to the existing assets / project
pipeline ownership. It must remain usable by headless frontends and must not live in an ImGui panel.

The contract is:

- input is one current recognized `Candidate` already inside a writable registered project source root;
- the graph must be quiescent under the existing `CanMutateSources` rules;
- the current row must still be the exact candidate selected by the caller;
- the live importer owns the default canonical settings and output declarations;
- the project pipeline generates the persistent `AssetId` with bounded collision retry;
- VMETA is encoded by the existing codec and staged/committed through `AssetFileTransaction`;
- source bytes are not rewritten merely to adopt the file;
- after commit, the existing rescan/classification/reference/publication reconciliation becomes
  authoritative;
- the newly adopted asset is seeded through the existing recook/build coordination rather than a new
  import queue; and
- failed/cancelled build work may report the newly adopted asset as failed/stale, but must not roll back
  a durable successful authored import or replace an older valid runtime generation with invalid data.

The source-format contract gains only the bounded importer-owned default metadata/settings hook needed
to author a valid first VMETA. The pipeline, not the callback, owns identity, importer/version stamping,
filesystem commit and subsequent build scheduling. The callback performs no I/O and owns no registry.

For E4 v1, **Import means adopting a supported Candidate that is already inside a writable project
source root.** The current Vanguard platform/editor has no production native file-picker or dropped-file
path delivery contract. Copying an arbitrary external file into the project is therefore not silently
implemented with ad-hoc Win32/SDL calls in E4. A later explicit platform/file-dialog seam may extend the
workflow without changing the import owner.

### Gap B - transactional import-settings mutation is not exposed

VMETA stores importer-owned versioned settings bytes, but there is no public project-pipeline command
for changing them. E4 settings UI must not edit the sidecar directly.

E4 adds one bounded shared settings mutation:

- target one current adopted asset, not a Candidate or invalid metadata row;
- require a writable root, exact live importer version and the existing source-mutation exclusion;
- preserve the asset ID, importer identity/version and existing output identity/type contract;
- accept only a canonical settings version/byte sequence produced by that format's real settings codec;
- encode and commit the complete updated VMETA through `AssetFileTransaction`;
- reconcile the source catalog only after durable commit; and
- request reimport through the existing automatic recook/reimport coordinator.

E4 does not introduce a universal reflected importer-property system. The first production settings
editor is texture-specific and reuses `TextureBuildDescription` plus the texture tool's canonical
settings codec. A matching bounded decode function may be exposed by `textureTools` because current
encoding is public while parsing is private. Other importers remain metadata-visible but settings-readonly
until they publish an equally owned presentation/codec contract.

### Gap C - browsing needs a stable UI-agnostic query model, not retained catalog pointers

The catalog may be atomically replaced by observation/reconciliation between editor frames. No consumer
can retain `SourceRecord` references across that boundary, and a gigantic project cannot afford to scan,
copy or re-index the complete 262,144-row source catalog independently for every panel or every frame.

E4 therefore permits one shared **Project Asset Catalog/query index** keyed by `SourceDatabase::GetGeneration()`.
The catalog is a frontend-neutral system component, not an ImGui/Slate/panel model. Editor panels are only
clients of its query API. A headless tool could consume the same catalog without linking editor UI code.
It is explicitly non-authoritative:

- `SourceDatabase` remains source inventory, identity and authored-reference truth;
- `ProjectAssetPipeline`/BuildGraph/VADI/VDDC remain accepted/build/delivery truth;
- the catalog owns no filesystem watcher, importer registry, AssetId allocation, source mutation, build queue,
  runtime resource request, dependency authority or GPU/preview work;
- commands always revalidate stable identity against the live owners rather than trusting a catalog row; and
- source-generation replacement invalidates the complete catalog snapshot rather than retaining borrowed rows.

The catalog may own compact immutable query acceleration: root-relative path/name bytes, a dense folder tree,
row-to-folder mappings and bounded query match masks. These are disposable acceleration structures, not a
second asset database and not UI representation.

## Browser product contract

### Panel and instance ownership

The production browser is a built-in E4 extension using `EditorPanelTypePolicy::Multiple`. Each instance
owns only presentation state such as:

- current root/folder;
- search text;
- type/import/build/status filters;
- list/grid presentation choice if both exist;
- selected stable asset/source identity;
- detail/preview tab state; and
- current preview request handle.

Instances share the generation-keyed read model and backend owners. Independent browser instances must
be able to show different folders/filters/selections simultaneously.

No browser instance owns a copy of `SourceDatabase`, a BuildGraph, watcher or `ResourceStreamer`.

### Selection and contextual commands

E4 registers an Asset Browser Local command-context type. The existing E3 panel-focus bridge pushes that
context with the focused browser instance as `receiverData`. Command query/invoke therefore operates on
the exact focused instance and its selection without a global singleton selection hack.

At minimum, E4 defines contextual commands for:

- Import current Candidate;
- Reimport selected adopted asset;
- Move/Rename;
- Duplicate;
- Delete;
- Undo/Redo project asset mutation where valid;
- Show dependencies;
- Show referencers; and
- Refresh/rescan only where an explicit manual reconciliation action is useful.

The existing context-menu surface composes those commands transactionally. Menus and toolbars call the
same command IDs; they do not duplicate operation logic.

### Folder/search/filter model

Browsing is metadata-only. Folder expansion, list/grid rows, search, sort and context-menu construction
must not request resource payloads or walk an asset dependency closure.

The browser presents all truthful catalog states, including:

- Candidate/unimported;
- imported/current;
- unsupported/discovery unavailable;
- ambiguous importer;
- missing/invalid/unreadable metadata;
- importer unavailable/version mismatch/source mismatch;
- reference scan unavailable/failed; and
- failed/stale/current build/delivery states where an output exists.

Unsupported/invalid assets remain visible; the UI does not hide them merely because they cannot build.

### Details and dependencies

The selected-item details pane can show stable identity, importer/version, source/output types, current
settings summary, issues, freshness/build reasons, delivery state, and authored dependencies/referencers.

Dependency/referencer UI uses the source database's existing forward/backward authored-reference index.
That authored-reference view is the E4 baseline. VADI build dependencies and runtime streaming dependencies
remain separate concepts and are not silently exposed through the same UI. If a later E4 gate needs committed
build dependency/referencer details, it must add a narrow owner-level query rather than letting the frontend
touch `DependencyIndex` directly. E4 must not merge these domains into one ambiguous "dependency" database.

## Applied versus built versus delivered state

The browser must make the three clocks visible instead of treating "Save settings" as equivalent to
"runtime is updated":

1. **Applied/authored:** current source + VMETA has durably committed and the catalog accepted it.
2. **Built:** `QueryBuildFreshness` says the exact output is current against the accepted authored state.
3. **Delivered:** the committed generation is published through the ordinary loose-resource path. For
   supported resident resources, delivery status also exposes whether the actual consumer has accepted/
   reacquired that exact generation. Publication and resident acceptance are distinct substates and must
   not be collapsed into one success bit.

After a settings edit or reimport request the old committed artifact may remain usable while a new build
is pending or failed. The UI must show that as stale/pending/failed rather than either hiding the old
preview or claiming the new authored state is already live.

## Preview and thumbnail contract

### Initial supported preview

E4's first runtime-backed visual preview is **texture/image**. Metadata details are available for all
catalog rows. Mesh scene preview is explicitly not required for E4 because the roadmap already permits
image/metadata preview before native geometry display.

Texture preview must follow the ordinary runtime path:

`ProjectAssetPipeline committed output`
-> loose publication
-> `ResourceStreamer::Request`
-> existing texture decoder/resource handle
-> `TextureResidencyRuntime::RequestTexture`
-> bindless-ready texture residency
-> private residency-manager TextureResidencyInfo::texture
-> `EditorUiService::RegisterTexture`
-> browser image.

The preview retains the real `TextureDemandHandle` for as long as the UI may sample the RHI texture.
Closing/changing the selection releases the editor texture registration first at the safe UI boundary,
then retires the backing preview only after every UI frame that captured the old registration has crossed
the retained import/submission boundary. This mirrors the accepted scene-output texture retirement shape.
Only after that point may the preview owner release its runtime demand/resource. E4 never keeps a naked RHI
texture alive after releasing the runtime demand that owns its residency.

The project workspace already depends on Resource Streaming and Rendering and already coordinates
project texture replacement. E4 adds one project-scoped **Asset Preview capability** composed by the
editor/workspace service layer. Its exact type/API name is frozen in P0, but ownership is not negotiable:
it is the single bounded preview/cache seam over the existing runtime owners rather than a panel-local
loader. The panel receives stable preview handles/state; backend lifetime remains in the workspace owner.

That preview access is a **narrow editor capability**, not permission for an extension to discover
`RenderingService`, `ResourceStreamingService` or mutable `ProjectAssetPipeline` through global service
lookup. The workspace owns request/resource/residency backing state and returns only bounded project-texture
preview status/identity through `EditorProjectAssetAccess`; raw RHI texture/sampler state is not exposed through
that project-asset facade. A later reusable editor presentation seam owns `EditorUiService` registration and
consumer visualization policy without broadening every extension's workspace access.

When a preview selection changes or a browser closes, E4 may cancel/reset only that preview's caller-owned
request/streaming interest and texture demand. It must **not** call
`ProjectAssetPipeline::CancelAutomaticRecook`, because automatic recook/reimport is project-wide shared
work already surfaced through Background Activity. Reimport remains shared project-owned activity; the
browser must not offer per-item cancellation unless a lower owner later exposes an explicit per-intent
cancellation token/contract.

### Thumbnail policy

Thumbnails are a bounded cache of presentation results keyed by immutable identity and exact content/
resource generation. They are not another DerivedData cache and are never source/build authority.

Initial rules:

- metadata/list rows do not require a thumbnail;
- only visible/recently requested items enter the bounded preview queue;
- maximum active preview loads and retained GPU/CPU thumbnail bytes are explicit configuration caps;
- selection preview has priority over background thumbnails;
- stale generation results are discarded rather than retargeted to a newer row;
- project close/extension deactivation cancels admission and drains owned preview work before service
  teardown; and
- a failed thumbnail never changes asset import/build state.

Direct project-texture thumbnail identity is stable output identity + exact committed build/content fingerprints;
it has no separate preview-recipe version because it displays the real project texture backing. Future rendered
thumbnail producers (material/mesh/prefab, etc.) may add an explicit recipe/version to their own cache identity.
Asset identity alone remains insufficient because it would reuse stale imagery after a successful rebuild.

If a retained thumbnail image is eventually useful across sessions, that is a later bounded editor cache
with its own version/key/eviction contract; E4 does not store thumbnail truth in VDDC/VADI by default.

## Runtime replacement policy

E4 does not claim universal hot reload.

For textures, the current editor/workspace path already has a real generation-aware replacement owner.
E4 may use that supported path and report the generation as delivered only after it reaches the existing
accepted resident state.

For a resource type with no safe current-owner replacement/reacquire contract:

- publish the new loose generation normally;
- later new requests resolve the new generation;
- keep existing loaded users on their valid old generation; and
- present an explicit "reload preview/world required" state instead of pretending existing references
  were updated.

Broad shader/PSO reload and arbitrary resident object replacement remain outside E4.

## Lifecycle and concurrency

All source/catalog/build mutation remains on the existing owner thread and obeys current graph exclusion.
UI callbacks may request commands but must not perform project-sized scan/build/materialization work inline.

Required ordering on project/editor shutdown or extension deactivation:

1. stop browser command/preview admission;
2. cancel or release browser-owned preview requests and UI texture registrations;
3. discard browser read-model/panel state that is not already owned by E3 persistence;
4. unregister E4 surfaces/commands/panel types through normal extension teardown; and
5. let the existing ProjectWorkspace/ProjectAssetPipeline shutdown stop observation/recook, drain worker
   state, unpublish owned loose sources and close VADI/VDDC in its accepted order.

No E4 callback may outlive its extension owner or hold a borrowed catalog row across a pipeline swap.

## REDengine reference and intentional divergence

REDengine is used as architectural reference, not as a code/API template.

Relevant RED ownership observed in `D:\root\R6.Root\Mainline\dev\src`:

- `editor/editorCommon/include/assetBrowser.h` centralizes selection/navigation, import/reimport and
  reference views in one AssetBrowser frontend;
- `editor/editorCommon/src/assetBrowser.cpp` forwards import/reimport operations to a command runner rather
  than doing importer/file work in the widget callback;
- backend resource commands own pending import/commit/abort lifetime, and successful import publication
  triggers resource reload notification only after resource persistence succeeds;
- asset browser thumbnail generation is a separate bounded/presentation concern with listeners rather
  than the authoritative depot model; and
- preview lifetime is explicitly owned and destroyed rather than leaked from the browser widget.

Vanguard intentionally diverges where its accepted architecture is stronger/different:

- `SourceDatabase` replaces RED depot/browser database authority for project sources;
- `ProjectAssetPipeline` + BuildGraph/VADI/VDDC own builds instead of RED resource commands;
- `AssetFileTransaction` owns durable authored-file commit/recovery;
- `ResourceStreamer` loose publication and runtime generation handles own runtime delivery;
- E3 command/context/panel surfaces replace RED wx/global browser dispatch; and
- E4 preview uses Vanguard's real texture resource/residency/UI-texture path rather than a RED preview
  framework.

The reusable RED lesson is the ownership split: the browser dispatches commands and owns presentation;
backend resource/import owners perform durable work and publish only successful results.

## Explicit non-goals

E4 does not include:

- E5 scene documents, hierarchy, inspector transactions or general reflected property editing;
- E6 scene viewport controllers or mesh/world preview rendering;
- every importer-specific settings editor;
- arbitrary external-file picker/drop integration without a production platform contract;
- source-control/Perforce integration;
- asset tags/favorites as new authoritative project metadata;
- importing every file at startup;
- loading meshes or full dependency closures to render browser rows;
- a second source catalog, dependency index, BuildGraph, DDC, filesystem transaction manager or runtime
  resource loader;
- broad shader/PSO live reload;
- unbounded preview/thumbnail CPU or GPU work; or
- screenshot-pixel acceptance tests.

## Ordered execution gates

### Definition - architecture and phase lock

**Status: CLOSED - explicitly authorized 2026-10-02 and reconciled by source/static Definition review.**

Definition is read-only with respect to production source. It maps exact-current E1/E2/E3/runtime seams,
traces RED reference ownership, records real gaps and locks the bounded phase shape below. It may update
planning/documentation only. No generation, build, executable test or native editor run belongs here.

### P0 - exact-current gap and evidence lock

**Status: CLOSED - explicitly authorized by the user on 2026-10-02 and completed as exact-current source/planning work only.**

P0 re-reads exact-current source immediately before implementation and freezes the concrete signatures,
capacities and proof matrix for:

- first-import/default-metadata capability;
- transactional settings mutation;
- browser read-model generation/lifetime;
- workspace-safe command/preview access;
- texture preview resource/residency/UI lifetime; and
- multi-instance panel/local-context ownership.

P0 is source/planning only. It does not authorize production implementation, generation, build, executable
tests or native editor execution.

#### P0 lock A - SourceDatabase generation and browser snapshot lifetime

ProjectAssetPipeline::GetAcceptedAssetGeneration() is not a complete lifetime fence for browser rows.
ProjectWorkspaceService also exposes direct source rescan/classification/reference-scan operations, and
those can replace or mutate SourceDatabase rows/reference spans without changing the pipeline's accepted
generation. P2 therefore requires one small shared source-catalog generation seam rather than guessing from
pipeline state.

P1 exposes the current source/reference limits as public constants instead of duplicating their private
source_database.cpp literals, and adds one non-authoritative observability method:

    [[nodiscard]] u64 SourceDatabase::GetGeneration() const noexcept;

The database owns a nonzero monotonic u64 generation. It advances exactly once after each successful public
mutation that can replace or change borrowed row/reference data: Rebuild/Rescan, Swap, SetReferences,
ScanReferences and ClassifySources. Failed mutations do not advance it. Swap does not swap generation values:
the receiver advances from its own prior generation after storage is exchanged and never imports the donor's
generation. Zero is reserved and wrap skips zero. ScanReferences may use an internal no-generation
reference-apply helper so the successful public
operation advances once after both links and scan states are installed. The generation is an owner-thread UI
invalidation token, not synchronization, source truth or a replacement for the pipeline accepted generation.

The shared Project Asset Catalog copies rows only while the current source generation is stable. It records
that generation with the snapshot and retains no `SourceRecord` pointer, metadata span or reference span.
The catalog is system-owned/query-oriented rather than extension-widget-owned. A compact row records only
stable query/command facts:

- source identity, source-root index and dense folder/name identifiers rather than a per-row `AbsolutePath`;
- source issues and metadata parse result;
- parsed AssetId when present;
- import state, importer id/version, source type and primary output type; and
- reference-scan state.

Canonical source paths are represented as root-relative UTF-8 in shared contiguous storage and a dense folder
tree; repeated folder prefixes are not copied into every row. Physical absolute paths are reconstructed or
re-resolved against the authoritative workspace only when an operation/details query requires one. Rows use
dense `u32` IDs/indices and no per-row heap allocation, `String`, shared pointer, UObject or UI payload.

Candidate/invalid/unsupported rows remain representable even when there is no AssetId. Commands never trust
the copied row as authority. Candidate commands carry source generation plus source identity/importer/version;
the editor command facade resolves the current physical path from the live SourceDatabase before mutation.
Adopted-asset commands carry AssetId plus that asset's accepted generation and revalidate against live owners.

P2 uses the exposed current source hard bound of 262,144 rows and introduces no lower asset-count limit. Its
IDs are `u32` so the representation does not itself prevent a later SourceDatabase limit increase. P2 planning
tightens P0's broad 256 MiB presentation allowance: the complete additional catalog/query working set has a
**128 MiB hard peak cap**, including the currently published snapshot, a staged replacement snapshot and active
query scratch/results. Normal steady-state catalog/query memory targets **less than 64 MiB at the current maximum
source count**. Query-result storage has a **16 MiB hard sub-budget**; a complete 262,144-row match mask is only
32 KiB, so even hundreds of independent browser queries do not require copied row arrays. Identical queries may
share immutable results, but correctness never depends on sharing.

Folder enumeration uses one dense `u32` row permutation with direct/subtree ranges. Search/filter results are
bit masks plus counts, not copied rows or `u32` result vectors. Text search performs case-insensitive comparison
using the existing path/string semantics while scanning the compact immutable snapshot; P2 does not add a large
trigram/token inverted index by default. Expensive full-project text work is cancellable and may execute on Jobs;
main-thread publication/enumeration remains bounded. Extending an existing search may narrow the previous match
mask rather than restart from all rows.

Selected authored references are read on demand rather than retained from SourceDatabase spans. The absolute
ceiling remains 4,096 forward and 4,096 backward references with an explicit truncated bit, but the browser/details
surface consumes them in bounded pages instead of eagerly allocating the maximum for every selected asset.
SourceDatabase remains the complete authored-reference owner.

Source/import facts and asynchronous build/delivery facts are separate clocks. A source-generation change rebuilds
the immutable catalog. Build/publication/resident changes must not rebuild path/folder/string storage. P2 keeps a
small status overlay and evaluates expensive `QueryBuildFreshness` only for bounded visible/selected work; it never
runs that build-planning query across the complete catalog. Until owner-backed freshness has been evaluated, the
truthful state is Unknown/NotEvaluated rather than guessed. `GetOutputDeliveryStatus` remains an owner query and
does not cause a runtime resource request.

Snapshot construction is transactional at the read-model level: capture a source generation, copy/build away
from the published snapshot, recheck the generation, then atomically publish only a complete snapshot. Stale worker
completion, allocation failure or any hard-budget failure is discarded; the previous complete snapshot remains
available and the catalog reports stale/unavailable state. No prefix snapshot is ever published.

#### P0 lock B - importer-owned first-import defaults and settings validation

SourceImporterDescriptor gains two optional authoring hooks. Existing import/build behavior remains valid
when either hook is null.

    struct SourceImportDescription
    {
        SourceImportDescription() noexcept
            : settings(memory::pools::Assets::GetInstance()),
              outputs(memory::pools::Assets::GetInstance()) {}
        u32 settingsVersion = 0;
        containers::DynamicArray<u8> settings;
        containers::DynamicArray<AssetOutput> outputs;
    };

    using CreateSourceImportDescriptionFunction =
        bool (*)(const SourceRecord& candidate, SourceImportDescription& output, void* userData) noexcept;

    using ValidateSourceSettingsFunction =
        bool (*)(u32 settingsVersion, containers::ArraySpan<const u8> settings, void* userData) noexcept;

SourceImporterDescriptor appends createImportDescription and validateSettings. IsValid() does not require
them. MakeDirectSourceImporterDescriptor appends those two callbacks as trailing optional parameters with
nullptr defaults so every current call site remains source-compatible while an authoring importer can supply
them without reconstructing the descriptor by hand.

Both callbacks are deterministic/pure with respect to project mutation: no filesystem I/O, identity
generation, transaction, registry, build scheduling or runtime publication. The default callback receives
the classified Candidate facts/path and supplies only settings version/bytes and output declarations.
The pipeline supplies AssetId, importer id/version and owns commit. The validator accepts only that importer's
current canonical settings representation.

SourceImportDescription remains bounded by the existing VMETA limits: settings <= 256 KiB, outputs <= 4,096,
encoded VMETA <= 1 MiB. A callback failure or invalid result is non-mutating.

Texture is the first authoring implementation:

- expose TextureBuildSettingsVersion = 1;
- add public DecodeTextureBuildSettings(bytes, TextureBuildDescription&), leaving output unchanged on failure
  and sharing the current strict V1 parser/validation;
- PNG/JPEG/TIFF/EXR defaults use Image2D, automatic color space, identity channels, no route flags and the
  current profiles::Color cooking profile;
- DDS defaults use PreservedDds, automatic color space, identity channels, no route flags,
  ddsMipTailCount = 1 and profile 0, the current canonical non-streamable preserved-DDS recipe;
- the first declaration is exactly output key main with textures::TextureResourceType; and
- first-import adoption additionally verifies that the chosen defaults are compatible with the selected
  Candidate source kind; normal settings validation remains the canonical byte/settings-version contract.

E4 does not add default-authoring hooks to mesh/shader/material importers merely to make every Candidate
importable. Rows whose importer lacks the hook remain truthfully visible with Import disabled.

#### P0 lock C - shared authored-file operations

The low-level durable sidecar writes live beside the accepted asset operation executors, not inside the
browser and not as ad-hoc file I/O in ProjectAssetPipeline:

    [[nodiscard]] AssetOperationResult ExecuteAssetAdopt(
        const AssetOperationContext& context,
        const filesystem::AbsolutePath& source,
        SourceImporterId expectedImporter,
        u32 expectedImporterVersion,
        AssetId newIdentity,
        const SourceImportDescription& description,
        AssetOperationFailure* failure = nullptr) noexcept;

    [[nodiscard]] AssetOperationResult ExecuteAssetUpdateSettings(
        const AssetOperationContext& context,
        AssetId asset,
        u32 settingsVersion,
        containers::ArraySpan<const u8> settings,
        AssetOperationFailure* failure = nullptr) noexcept;

AssetOperationResult adds CandidateUnavailable and SettingsInvalid; existing ReadOnlySource,
ImporterUnavailable, SourceUnavailable, TargetConflict, IdentityConflict, TransactionFailed and LimitExceeded
continue to carry the other concrete failures.

ProjectAssetPipeline invokes createImportDescription exactly once after Candidate revalidation and before
identity collision search. The resulting frozen description supplies every output path checked during the
bounded ID loop and is passed unchanged to ExecuteAssetAdopt.

ExecuteAssetAdopt revalidates a current writable Candidate with exactly MetadataMissing and no competing
source/identity/invalid-metadata issue plus exact expected live importer/version. It requires nonempty declared
outputs including the importer's primary output, validates the frozen description's settings with the current
importer's validateSettings callback plus the completed metadata after the caller supplies newIdentity,
requires the sidecar target to remain absent, and stages only source.vmeta. It never rewrites source bytes.

ExecuteAssetUpdateSettings revalidates one current writable MetadataSelected asset and exact live
importer/version, requires validateSettings, preserves AssetId/importer/importerVersion and the complete
output key/type declaration exactly, and stages only the complete replacement VMETA sidecar. It does not
mutate source bytes or cooked/runtime state.

Both operations use the existing AssetFileTransaction Ready -> collect -> Prepare -> Commit contract and
return successful logical commit for CommittedCleanupPending exactly like the existing move/duplicate/delete
executors. They do not rescan, cook, publish or update history themselves.

The existing transaction gains conditional sidecar staging rather than relying on a stale catalog check:

    StageCreate(path, bytes)
    StageWriteIfUnchanged(path, expectedBytes, replacementBytes)

Import uses create-only staging so a sidecar that appeared after Candidate validation returns the existing
transaction Conflict result instead of being overwritten. Settings reads the current <=1 MiB VMETA, verifies
it still matches the catalog-selected bytes, and uses compare-and-replace staging so an external settings
edit cannot be lost. These are bounded extensions of AssetFileTransaction, not a second transaction system.

#### P0 lock D - ProjectAssetPipeline first import and settings commands

The shared/headless project owner adds:

    struct ProjectAssetCandidateImportRequest
    {
        filesystem::AbsolutePath sourcePath;
        resources::ResourcePath sourceIdentity;
        assets::SourceImporterId importer = assets::InvalidSourceImporterId;
        u32 importerVersion = 0;
        u64 sourceGeneration = 0;
    };

    struct ProjectAssetSettingsUpdateRequest
    {
        assets::AssetId asset;
        u64 acceptedGeneration = 0;
        u32 settingsVersion = 0;
        containers::ArraySpan<const u8> settings;
    };

    [[nodiscard]] ProjectAssetPipelineResult ImportCandidate(
        const ProjectAssetCandidateImportRequest& request,
        assets::AssetId& importedAsset,
        ProjectAssetRestoreReport* report = nullptr,
        ProjectAssetPipelineFailure* failure = nullptr) noexcept;

    [[nodiscard]] ProjectAssetPipelineResult UpdateAssetSettings(
        const ProjectAssetSettingsUpdateRequest& request,
        ProjectAssetRestoreReport* report = nullptr,
        ProjectAssetPipelineFailure* failure = nullptr) noexcept;

Both commands require the existing CanMutateSources() exclusion and a Ready AssetFileTransaction. They capture
the sidecar path in the existing bounded operation history before commit, so first import and settings changes
participate in the current Undo/Redo owner:

- undo of first import removes the sidecar and returns the row to Candidate after reconciliation;
- redo restores the exact same committed VMETA/AssetId, not a newly generated identity; and
- settings undo/redo restores the exact previous/current sidecar bytes.

Candidate import first checks sourceGeneration == SourceDatabase::GetGeneration() and revalidates exact
path/source identity/importer/version/current Candidate state. It calls createImportDescription once and
validates the frozen description before generating an identity. The pipeline then generates AssetId candidates
with the existing maximumIdentityGenerationAttempts cap (default 32). Raw collision admission does not use LookupAsset or
LookupOutput eligibility because conflicted identities can intentionally be absent from those lookups.
SourceDatabase therefore adds read-only ContainsAssetIdentity(AssetId) and ContainsOutputPath(ResourcePath)
queries that report map occupancy regardless of row issues. For every declared output the pipeline also uses
one read-only ResourceStreamer::ContainsResourcePath(ResourcePath) integration query to reject any mounted
package/loose non-project identity. These occupancy queries expose no registry mutation. Exhausting the bounded
attempts remains IdentityGenerationFailed.

A Candidate generation mismatch/current-row mismatch returns OperationFailed with CandidateUnavailable in the
embedded AssetOperationFailure. A settings accepted-generation mismatch returns OperationFailed with the
existing SourceUnavailable operation result. P0 does not add another outer ProjectAssetPipeline result family;
callers requery current source state after either stale-operation failure.

After a durable operation commit, both commands call the existing Rescan(current roots) path. That path
prepares/persists/accepts the catalog atomically and seeds the existing automatic-recook pending state when
that policy is enabled. It does not synchronously cook in the authored transaction. If automatic recook is
disabled, the authored state remains truthfully dirty until an explicit existing reimport/build request.
Post-commit source reconciliation failure returns existing CommittedReconciliationPending; transaction cleanup
debt returns existing CommittedCleanupPending. Neither condition rolls back a durable authored commit.

#### P0 lock E - editor frontend access and multi-instance command routing

E4 extensions do not gain mutable ResourceStreamer, RenderingService or texture-residency globals. They also
do not need a second editor asset backend. The editor shell composes one narrow EditorProjectAssetAccess from
the mutable workspace plus editor UI lifetime owner, while EditorExtensionActivationContext keeps its existing
const workspace view and separately exposes:

    [[nodiscard]] EditorProjectAssetAccess& GetProjectAssets() const noexcept;

The access is a thin owner-thread facade over the existing ProjectAssetPipeline plus workspace-owned
preview runtime. Its command methods cover only the E4 operations ImportCandidate,
UpdateAssetSettings, RequestAssetReimport, Move/Rename, Duplicate, Delete, Undo/Redo and the existing
freshness/delivery queries. It does not expose BuildGraph, VADI/VDDC, loose publication, mutable
SourceDatabase or runtime services.

The browser registers exactly one E4 Local command-context type shared by all browser instances.
EditorPanelTypePolicy::Multiple remains the panel policy. Each panel instance owns its folder/filter/selection
state; the accepted focus bridge passes that panel state as receiverData to the Local context. No per-instance
context type and no process-global current asset selection is introduced.

The existing framework bounds remain authoritative: 256 panel types, 512 panel instances, 256 command context
types, 512 commands, 1,024 handlers and 64 simultaneously active command contexts. E4 adds one panel type,
one Local context type and a small fixed command set; it does not raise those limits in P2/P3.

#### P0 lock F - workspace-owned texture preview capability

The workspace owns the project-texture preview runtime records; `EditorProjectAssetAccess` is the only
extension-facing project-asset route. Asset Browser is the first consumer, not the owner: the same capability is
intended for Properties/inspectors, Material Graph, VFX Graph and future editor surfaces that display real project
texture assets. Renderer-produced images remain a separate producer class and meet project textures only at the
lower Editor UI consumption/lifetime layer.

The current preview status surface, generalized before P4.3, is:

    enum class ProjectTexturePreviewPriority : u8 { Background, Interactive, Focused };
    enum class ProjectTexturePreviewState : u8
    {
        Queued,
        LoadingResource,
        WaitingForCurrentGeneration,
        LoadingResidency,
        Ready,
        Failed,
        Retiring
    };

    struct ProjectTexturePreviewInfo
    {
        ProjectTexturePreviewState state = ProjectTexturePreviewState::Queued;
        resources::ResourceReference resource;
        u32 resourceGeneration = 0;
        assets::BuildFingerprint buildFingerprint;
        assets::BuildFingerprint contentFingerprint;
    };

The caller owns one generation-checked `ProjectTexturePreviewHandle` representing one logical interest. The
extension-facing API is:

    RequestTexturePreview(output, priority, handle)
    UpdateTexturePreviewPriority(handle, priority)
    QueryTexturePreview(handle, info)
    ReleaseTexturePreview(handle, capturedUiFrame)

`QueryTexturePreview` is observational; panel/widget polling never drives backend progress. The workspace frame
participant owns bounded progression. Logical interests attach to fixed workspace-owned shared backings keyed by
the exact direct-texture publication identity (`ResourceReference` + published build fingerprint + published content
fingerprint). A backing, not every caller interest, owns the `PipelineRequest`, exact `ResourceHandle`, later
`TextureDemandHandle`, Ready physical texture ownership and state machine. Multiple consumers of the same exact
publication therefore share resource/residency work while retaining independent interest lifetimes and priorities.

Request accepts only a typed committed texture output and a Background/Interactive/Focused priority. The highest
live interest controls queued admission priority; joining a higher-priority interest promotes an already-live
`PipelineRequest` through the resource pipeline's existing promotion path. A pre-Ready publication change may
retarget the shared non-visible backing after dropping stale work. `Ready` and `Retiring` generations are immutable:
they are never silently retargeted underneath a UI consumer.

The direct texture path then:

1. requests that output through ordinary ResourceStreamer::Request;
2. on load completion acquires and retains the exact ResourceHandle;
3. re-queries delivery and, if the published fingerprint pair changed, releases the stale completion and
   waits/reacquires the current generation rather than retargeting the old result;
4. before any `RequestTexture`, classifies same-path residency by exact identity, never numeric generation ordering
   because resource generations wrap: an exact current generation may be coalesced; a current-generation mismatch
   may wait only when residency `GetInfo` proves its in-flight replacement target generation exactly equals the
   retained CPU generation and the replacement state is `Uploading`/`InstallationPending`, or
   `Failed && replacementRetryRequired` for the existing resident-delivery retry owner; any other mismatch must
   reacquire the current publication or fail closed; `NotFound` may
   create the first residency; and `Ambiguous` fails closed;
5. calls TextureResidencyRuntime::RequestTexture only when that classification permits it;
6. accepts Ready only when TextureRuntimeInfo is BindlessReady, its resource generation matches
   ResourceHandle::GetGeneration(), and private TextureResidencyManager::GetInfo content evidence matches the
   captured publication; and
7. retains a strong reference to the exact accepted physical RHI texture generation so a later in-place residency
   replacement cannot silently change what an already-visible preview handle represents.

Preview is a Request-only consumer. It never calls ResourceStreamer::Replace or
TextureResidencyRuntime::ReplaceTexture; the existing workspace resident-delivery coordinator remains the sole
replacement owner. After replacement, preview observes/reacquires the current generation through Request.

The workspace preview backend does not own an editor sampler, color/display transform or `EditorTextureHandle`, and
does not expose raw RHI texture/sampler refs through `EditorProjectAssetAccess`. Sampler choice, checkerboard/channel/
normal-map/HDR visualization and UI registration are presentation policy. A later reusable editor presentation seam
combines a Ready project-texture backing with `EditorUiService`; each consumer owns an independent
`EditorTextureHandle` because the current UI registry does not refcount shared registrations. On final visible release,
the consumer unregisters its UI handle first at frame F and transfers the release frame to the preview backend; the
exact Ready physical texture/ResourceHandle/TextureDemandHandle remain pinned until a later UI frame.

The initial workspace preview configuration is explicitly bounded:

- maximum logical preview interests, including future retirement ownership: 512;
- maximum fixed shared preview backings: 512;
- maximum actively progressing unique backings: 32;
- maximum Ready unique backings: 64;
- maximum new preview starts per editor tick: 4;
- maximum owner-driven preview state/residency checks per editor tick: 32.

Asset Browser thumbnail metadata/LRU and any CPU thumbnail-pixel budget are browser presentation policy and do not
live in the generic project-texture preview header. P4.7 may define a browser-local bounded record count, but P4 does
not allocate copied thumbnail images, a second GPU cache or a persistent thumbnail artifact. `TextureResidencyRuntime`
remains the physical GPU-byte-budget owner.

Direct project-texture backing identity is `ResourceReference` + published build fingerprint + published content
fingerprint. There is no preview-recipe version for direct residency/display. Recipe identity belongs to future
renderer-produced material/mesh/prefab preview images. Paths/timestamps alone are never authoritative identity.

#### P0 lock G - later focused proof matrix

No proof is executed in P0. The final P6 gate must use the smallest focused existing targets/fixtures capable
of establishing these contracts:

1. Source generation: each successful borrowed-data mutation advances the SourceDatabase generation; failed
   mutations do not; swap invalidates both sides.
2. First import: PNG/image and DDS Candidates author valid canonical VMETA without changing source bytes;
   stale generation/read-only/missing-default/transaction failures are non-mutating; project/runtime output-ID
   collisions retry within the existing 32-attempt cap; commit joins existing undo/redo; reconciliation seeds
   normal recook/publication.
3. Settings: typed texture decode/encode round-trip is canonical; invalid/stale settings are rejected;
   AssetId/importer/output declarations remain identity-stable; undo/redo and post-commit reconciliation are
   correct.
4. Browser model: Candidate/unsupported/invalid/imported rows copy without borrowed pointers; a source
   generation change invalidates the whole read model; selected refs copy with the 4,096/direction truncation
   contract; capacity failure publishes no prefix; list/folder/search/context construction issues zero runtime
   resource requests.
5. Multi-instance commands: at least two browser instances keep independent folder/filter/selection and the
   focused panel Local receiverData is the sole contextual command target.
6. Preview: ordinary ResourceStreamer -> texture residency is the only visual path; publication changes reject
   stale completions; ready generation/fingerprint matching is exact; a 33rd active record and 65th Ready
   residency remain queued/deferred under the frozen caps; selection outranks thumbnail work; UI unregister
   precedes F+1 demand/resource release; replacement proves workspace owns Replace while preview only
   reacquires; close/quiesce leaves zero live preview demands.
7. Restart/ordinary editor: committed VMETA/VADI/VDDC/loose state is reused after restart, and one ordinary
   Project Cardinal editor smoke/manual review exercises the production browser/texture preview as needed.

P6 remains the only ordinary generation/build/executable/native gate. Any later input-driving automation still
requires the explicit warning immediately before it runs.

### P1 - shared first-import and settings mutation

**Status: CLOSED on 2026-10-02.**

Implement only the missing shared backend capabilities required by the real frontend:

- importer-owned default metadata/settings/output description;
- transactional Candidate adoption on `ProjectAssetPipeline`;
- transactional adopted-asset settings update preserving identity/output contract; and
- existing recook/publication handoff after commit.

Keep the APIs headless-capable. No browser UI or preview implementation belongs in P1. Intermediate
validation is source/static only unless an actual blocker requires otherwise.

### P2 - browser read model, multi-instance panel and selection context

**Status: CLOSED on 2026-10-03 at the source/static gate.** Final exact-current editor review returned
0 BLOCKER / 0 HIGH / 0 MEDIUM. Final backend review found three MEDIUMs (same-generation retry suppression,
query-result catalog-identity ABA, and declaration-order primary-output fallback); all three were corrected and
the narrow exact-current recheck returned 0 BLOCKER / 0 HIGH / 0 MEDIUM. No Premake generation, build, executable
test, native editor run, input automation, ResourceStreamer browser instrumentation proof or performance benchmark
was run in P2. At that P2 closure checkpoint P3-P6 were still unauthorized; P3 was later explicitly authorized and
is now closed at its source/static gate. P4.0/P4.1/P4.2, the pre-P4.3 generalization, P4.3, P4.4, P4.5, P4.6, P4.7,
P4.8 and P4.9 were later explicitly authorized and are now closed; P4 is closed at source/static scope. P5.1, P5.2
and P5.3 were subsequently authorized and are also closed at source/static scope; P6 was later explicitly authorized
and is closed as the focused E4 execution/acceptance gate.

P2 architecture is planned against Epic's local Unreal Engine source under `D:\UnrealEngine`, specifically the
AssetRegistry, ContentBrowserData, ContentBrowserAssetDataSource and SAssetView boundaries. Vanguard retains the
useful ideas: an authority/query system below the UI, short-lived compiled queries, callback/sink enumeration,
asynchronous/cancellable discovery/search, user-path prioritization and explicit CPU-versus-memory indexing choices.
Vanguard intentionally does **not** copy Unreal's UObject-heavy data-source layer, per-item shared payload objects,
or broad duplicate tag indexes. Vanguard's source model is smaller and can use dense IDs, contiguous arenas and
fixed budgets directly.

P2 adds one production, frontend-neutral Project Asset Catalog/query component plus the built-in E4 browser
extension that consumes it. The system/UI boundary is mandatory:

- the catalog/query component has no ImGui/editor-widget/panel dependency and is usable by headless/tool callers;
- `SourceDatabase` and `ProjectAssetPipeline` remain the only authoritative source/project-asset owners;
- the catalog snapshot is immutable after publication and shared by every browser instance;
- panel instances own only small interaction state: current root/folder ID, query/filter description, query handle,
  selected stable identity and view preferences;
- no panel owns copied rows, a source database, build graph, watcher, resource streamer or runtime-resource handle;
- one Local command context type is shared by all instances, with the exact focused panel state as `receiverData`;
  and
- browsing/query/details construction issues zero ResourceStreamer/ResourcePipeline requests. Runtime requests begin
  only in the separately authorized P4 preview path.

The catalog/query API is built for the current **262,144-source** hard limit and must remain responsive at that
limit. Its baseline data shape is dense rows + one shared UTF-8 arena/folder tree + `u32` index permutations and
query match masks. There are no per-row heap allocations. Folder navigation must use indexed direct/subtree ranges,
not a full-catalog scan per frame. Text/filter work is compiled once per query, cancellable, generation-fenced and
performed in bounded batches; query enumeration exposes callbacks/pages over row IDs rather than materializing UI
objects for the full result set.

The additional P2 catalog/query memory contract is a 128 MiB hard peak and <64 MiB normal steady-state target at
the current maximum source count, with 16 MiB reserved as the hard query-result/match-mask budget. Capacity failure
is visible and non-destructive: retain the prior complete generation and refuse the newer snapshot/query rather than
growing without bound. P2 records allocated/used bytes, row/folder counts, active query count, scanned-row count and
query/rebuild timings so the final gate can detect accidental O(N)-per-frame or memory regressions.

The production browser then supplies:

- generation-keyed non-authoritative read model;
- root/folder navigation, search and filters;
- truthful source/import/build/delivery badges/status;
- multiple independent panel instances;
- focused browser Local command context; and
- selected-item metadata/dependency/referencer details.

Rows/navigation/context construction must prove they do not issue runtime resource requests.

### P3 - real asset commands and texture import settings

**Status: CLOSED on 2026-10-03 at the source/static gate.** The required exact-current re-evaluation reused the
existing ProjectAssetPipeline, AssetFileTransaction, asset-operation, reconciliation, observation, recook and
undo/redo owners instead of creating parallel backend machinery. The final Asset Browser/UI adversarial review
returned 0 BLOCKER / 0 HIGH / 0 MEDIUM. The backend/facade review initially found the stale undo/redo external-edit
race and transaction-readiness query mismatch; those were corrected with transaction-authored commit postconditions,
digest/absence CAS replay plus mutation preconditions, stale-history invalidation on conflict, and the narrow
`CanStartAssetOperation()` readiness predicate. The final exact-current backend/facade recheck returned
0 BLOCKER / 0 HIGH / 0 MEDIUM. No Premake generation, build, executable test, native editor run or input automation
was performed in P3.

Connect the UI to shared backend operations through the E3 command route:

- Import Candidate;
- Reimport;
- Move/Rename, Duplicate, Delete;
- Undo/Redo where valid;
- context-menu and ordinary surface contributions; and
- texture settings decode/edit/canonical encode/apply.

The browser must expose operation failures/progress using the accepted E3 diagnostics/activity surfaces.
It must show authored/applied state separately from build/delivery freshness.

### P4 - bounded runtime texture preview and thumbnails

Add the shared project/editor preview access and first real texture preview:

- committed output request through ResourceStreamer;
- real texture residency demand;
- generation-exact UI texture registration;
- bounded active preview/thumbnail work and cache;
- stale result rejection; and
- observation/reacquire reporting after the workspace-owned texture replacement path advances.

The preview capability boundary should be mature enough that later asset-type-specific preview producers can
integrate without changing Asset Browser ownership or inventing a second lifetime/cache system. P4 does not,
however, implement those producers: mesh, material and prefab preview/thumbnail rendering are explicitly deferred
to a separately planned renderer-facing effort with their own scene/camera/lighting/output requirements.

No mesh/material/prefab preview scene renderer, generic scene-preview implementation, or second decoder path is
added in P4.

P4 is deliberately split into focused sub-phases so renderer/resource lifetime, browser presentation and
background-thumbnail pressure do not land as one coupled change. This decomposition is planning only; no P4
production sub-phase begins until the user explicitly authorizes it.

Once authorized, each P4.1-P4.8 sub-phase closes with its own exact-current scoped source/static/adversarial review
before the next begins. The ordinary compile/test/native proof remains deferred to P6.

#### P4.0 - exact-current preview re-evaluation and implementation lock

Read-only/source-static preflight only. Re-study the exact-current `ResourceStreamer` request lifecycle,
`TextureResidencyRuntime`, private residency texture access, `EditorUiService` texture registration/frame retention,
scene-output retirement helper, `EditorProjectAssetAccess`, ProjectWorkspace replacement ownership and current Asset
Browser draw/query code. Confirm what already exists before editing and change the frozen P0 shape only if exact-current
source proves it necessary. Produce the exact owner/file/test-target map and close any BLOCKER/HIGH/MEDIUM design gap.

No production edits, build, tests or native editor execution belong in P4.0.

#### P4.1 - preview capability ownership and API shell

The initial P4.1 closure created only the narrow project-scoped preview owner and extension-facing contract:

- generation-checked logical preview-handle ownership and a narrow request/query/release surface;
- fixed workspace-owned slot/state storage and frozen configuration values;
- `EditorProjectAssetAccess` as the only extension-facing route; and
- no `EditorTextureHandle`, mutable runtime owner or panel-local loader in the workspace contract.

P4.1 established ownership/lifetime storage without yet making a preview Ready or changing Asset Browser visuals.
Its initial closure used 512 inline caller slots, per-slot nonwrapping generation plus per-workspace owner-cookie ABA
fencing and a lazy shared sampler. The later editor-wide generalization pass below supersedes the sampler and
one-caller-one-backing parts of that initial shape while preserving the handle/owner-cookie invariants. The preview
virtual hooks are private on `ProjectWorkspaceService` and reachable by extensions only through
`EditorProjectAssetAccess`. P4.1 contains no `ResourceStreamer::Request`, `TextureResidencyRuntime::RequestTexture`,
replacement call, `EditorTextureHandle`, UI registration or thumbnail work.

#### P4.2 - ResourceStreamer request and publication fencing

Implement the first half of preview progression:

- admit only a typed committed texture output;
- capture the exact currently published `ResourceReference`, build fingerprint and content fingerprint;
- start only ordinary `ResourceStreamer::Request` work and retain the exact acquired `ResourceHandle` generation;
- re-query publication after asynchronous completion and discard/requeue any pre-Ready completion whose publication
  fingerprints advanced; and
- keep preview strictly Request-only: no `ResourceStreamer::Replace`, no automatic-recook cancellation and no UI
  texture registration.

P4.2 ends with a publication-fenced exact CPU resource generation, not a GPU/UI preview.

The current P4.2 implementation remains request-only. Admission accepts a typed VTEX only when
`ProjectAssetOutputDeliveryStatus` carries a committed, concrete published generation (`Installed` or the retained
last-live `Stale` publication) with nonempty build/content fingerprints and a concrete loose physical path. After the
pre-P4.3 editor-wide correction, shared backing records own the `PipelineRequest`, retained `ResourceHandle` and
captured published fingerprints; logical caller interests only reference those backings. Background/Interactive/
Focused map to Background/Normal/High streaming priority, and higher live interests promote an in-flight shared
pipeline request.

The workspace frame participant now advances unique backings under the fixed start/progress/state-check bounds;
`QueryTexturePreview` is observational. On completion the backing re-queries publication, drops any completion whose
published fingerprint pair advanced, and verifies the acquired texture resource actually opened the currently
published loose physical generation before retaining it. An older already-loaded CPU generation therefore transitions
to `WaitingForCurrentGeneration` and is released/re-requested later; preview never calls `Replace`. While the retained
CPU handle remains pre-Ready in the P4.3 handoff state, owner-driven state checks continue revalidating publication so
a later publication advance also releases/requeues that handle rather than letting stale CPU ownership cross into
residency work.

P4.2 intentionally stops with the exact CPU handle parked in the `LoadingResidency` handoff state. It does not call
`TextureResidencyRuntime::RequestTexture`, does not expose sampler/texture UI data as Ready, and does not implement
P4.4 Ready eviction/LRU, F->F+1 retirement or post-Ready supersession. Owner-driven pre-Ready start/progress bounds were
pulled forward by the editor-wide generalization because progress may not depend on whether an arbitrary consumer is
currently drawing/polling.

#### Pre-P4.3 editor-wide project-texture generalization - CLOSED

The user explicitly clarified that project texture preview is a heavily reused editor capability rather than an Asset
Browser thumbnail subsystem. A fresh exact-current audit therefore corrected the contract before texture-residency
ownership lands:

- `ProjectTexturePreviewPriority::{Background, Interactive, Focused}` replaces browser-role `Thumbnail/Selection`;
- 512 generation/owner-cookie-fenced logical interests attach to fixed shared exact-publication backings;
- the backing owns resource request/CPU generation state while each consumer retains independent interest priority and
  lifetime; same exact publication work is therefore not multiplied by graph/property/browser references;
- `UpdateTexturePreviewPriority` recomputes the highest live backing priority and promotes an in-flight resource request;
- `QueryTexturePreview` is observation-only; `DriveAssetFrame` owns bounded starts and state checks, so hidden/collapsed
  panels cannot stall global backend progression;
- generic 32-progressing/64-Ready/4-start/32-state-check accounting is per unique backing, not per logical consumer;
- browser-only thumbnail record/pixel-cache budgets leave the generic project-preview header;
- the workspace no longer creates/exports a preview sampler and the public info no longer exposes raw RHI texture or
  sampler refs / `displaySrgb`; presentation sampling, color visualization and `EditorTextureHandle` registration stay
  outside the project residency backend; and
- pre-Ready backings may retarget to a newer publication, but `Ready`/`Retiring` backings are immutable publication
  generations and never silently change underneath a consumer.

P4.3 must additionally preserve two audited invariants. First, same-path residency classification happens before
`RequestTexture` and uses exact equality only, never numeric generation ordering: exact current generation coalesces;
a mismatch may wait only when residency `GetInfo` proves `replacementResourceGeneration` exactly equals the retained
CPU generation and replacement state is `Uploading`/`InstallationPending`, or `Failed && replacementRetryRequired`
for the existing resident-delivery retry owner; any unmatched mismatch
must reacquire current publication or fail closed; `NotFound` may create the first residency; `Ambiguous` fails closed.
Second, Ready retains a strong ref to the exact accepted physical RHI texture generation, not just the mutable residency
identity, so an in-place runtime replacement cannot change an already-visible preview before explicit old/new UI handoff.

#### P4.3 - texture residency and exact Ready qualification

Advance the exact P4.2 resource through the renderer-owned texture residency path. P4.3 uses the new read-only
`TextureResidencyRuntime::FindDemandedResource` admission lookup rather than `FindResidentResource`: the latter is
deliberately Ready-only and can hide a demanded loading generation, so its `NotFound` is not safe authority to create a
second same-path residency identity.

- classify every actively demanded same-path runtime record before requesting using exact equality only: coalesce an
  exact generation only while it is `MipTailLoading`, initial `InstallationPending`, or `BindlessReady`;
  wait on a mismatch only when `GetInfo.replacementResourceGeneration` exactly equals the retained CPU generation and
  replacement state is `Uploading`/`InstallationPending`, or `Failed && replacementRetryRequired` for the existing
  resident-delivery retry owner; otherwise reacquire current publication
  or fail closed; create only when the all-demanded lookup returns `NotFound`; fail closed on `Ambiguous`;
- call `TextureResidencyRuntime::RequestTexture` only for that exact `ResourceHandle` when the classification permits;
- accept Ready only when `TextureRuntimeInfo` is BindlessReady and its resource generation matches the handle;
- verify the lower residency/source/content evidence still matches the captured committed publication;
- re-check the published fingerprint pair before exposing Ready; and
- retain a strong ref to the exact accepted physical RHI texture generation privately in the backing. Do not expose
  raw RHI/sampler state through `EditorProjectAssetAccess` and do not add UI registration here.

The preview-specific residency-pressure cap is enforced before issuing a new demand: Ready backings plus P4.3 backings
that already own a `TextureDemandHandle` may not exceed 64. Waiting behind the workspace replacement owner does not
create a speculative preview demand and therefore does not consume one of those held preview-residency slots.

Preview never calls `TextureResidencyRuntime::ReplaceTexture`; the existing workspace resident-delivery coordinator
remains the sole replacement owner. P4.3 still has no Asset Browser texture registration.

P4.3 is closed at the source/static gate. The exact-current implementation owns one `TextureDemandHandle` and one
private owning `rhi::Texture` per admitted shared backing, preserves the retained exact `ResourceHandle`, and reports
`Ready` only after runtime/manager generation, path, source, content fingerprint and final publication/physical-source
checks all agree. Preflight additionally found that the existing Ready-only `FindResidentResource` lookup could hide a
demanded loading generation; the narrow read-only `FindDemandedResource` lookup was added and is now the sole preview
admission authority. Fresh backend and editor/phase-isolation reviews both returned 0 BLOCKER / 0 HIGH / 0 MEDIUM.
No Premake generation, compilation, executable tests, editor launch or native input was performed for P4.3.

#### P4.4 - bounded scheduling, retirement and supersession

Make the backend production-bounded and safe for UI ownership before any browser starts sampling it:

- 512 logical interests including future retirement ownership and at most 512 fixed shared backings;
- 32 actively progressing unique resource/residency backings;
- 64 live preview residency demands, including Ready/Retiring ownership;
- 4 new starts and 32 owner-driven state/residency checks per editor tick;
- priority scheduling by Background < Interactive < Focused, with queued/in-flight promotion from the highest live
  interest and no background admission allowed to starve focused work; and
- shared UI-frame retirement: unregister at frame F, retain ResourceHandle/TextureDemandHandle until a later UI
  frame, reusing the scene-output F-to-F+1 predicate instead of inventing another rule.

Pending release cancels only caller-owned preview interest and never project-wide recook. Pre-Ready publication
changes may safely discard/reacquire within the same not-yet-visible request. A Ready backing that may already be
sampled by UI is different: it must not be silently retargeted or released in place. When publication advances, the
old Ready backing stays pinned until its browser unregisters/releases it; the newer publication is acquired through a
new preview handle (or an equivalently explicit two-generation handoff) and only then replaces the UI registration.

P4.4 contains no Asset Browser visual redesign and starts no background thumbnails.

P4.4 is closed at the source/static gate. The exact-current implementation generalizes the scene-output
F-to-later-frame rule into one private `CanRetireEditorUiFrameResource` predicate used by both owners. A Ready backing
max-accumulates every releasing sharer's captured UI frame; after the final sampled interest releases it enters
`Retiring` and preserves its exact `ResourceHandle`, `TextureDemandHandle` and private owning `rhi::Texture` until a
later UI frame. Retiring work shares the existing rotating 32-check budget. Project Workspace receives the same
`FrameContext.frame` already consumed by Editor UI `BeginFrame`, avoiding a reverse ProjectWorkspace-to-EditorUi service
dependency.

Scheduling now uses cumulative non-borrowable reserves. Of 32 progressing backings, Background may occupy at most 30,
Background+Interactive at most 31, and total work at most 32. Preview residency is charged by every live
`TextureDemandHandle`, including Retiring: ordinary Background may occupy at most 61, ordinary
Background+Interactive at most 62, ordinary total at most 63, and the final slot of the hard 64-demand bound is reserved
for one explicit immutable-generation N-to-N+1 overlap. That overlap records the exact predecessor backing index plus a
non-reused backing-allocation identity, so later generations cannot keep transition headroom stale and recycled slots
cannot satisfy the predecessor proof by ABA. Once that exact predecessor retires, the successor rejoins ordinary
accounting; temporary priority-demotion pressure merely blocks further admission until it drains and never evicts a live
Ready backing.

Ready and Retiring publications remain immutable. A current publication is requested through a distinct preview handle,
and later UI presentation must make the new generation Ready/register it before unregistering and releasing the old
generation. `QueryTexturePreview` remains observational, public project-asset access remains evidence-only, preview still
never calls `ReplaceTexture`, and no UI registration, thumbnail traffic or browser visual redesign was added in P4.4.
Two final independent exact-current reviews returned **0 BLOCKER / 0 HIGH / 0 MEDIUM** after review-discovered cumulative
reserve and exact-predecessor headroom defects were corrected. No Premake generation, compilation, executable tests,
editor launch or native input was performed for P4.4.

#### P4.5 - mature Asset Browser content surface and folders

Give the browser presentation itself a dedicated pass before live thumbnail traffic is connected. Preserve the
frontend-neutral catalog/query authority while establishing the production content surface:

- clear root/folder navigation, Up/breadcrumb behavior and folder presentation;
- main-content folder entries where the chosen presentation benefits from them, with ordinary enter/double-click
  navigation and search/recursive scope remaining explicit;
- asset rows/cards with a stable thumbnail/placeholder region and truthful import/status treatment;
- folder entries as metadata-only UI with normal editor icons, never runtime preview requests;
- texture placeholders ready for later P4 thumbnail binding, while mesh/material/prefab and other unsupported visual
  producers remain type/icon/metadata presentation only;
- clipped/virtualized folder and asset traversal with no full-catalog per-frame scan;
- exact selection, right-click/context-command and multi-instance behavior retained; and
- per-instance content-view/layout state only for presentation modes actually kept; and
- one polished presentation mode first; list/grid duality is added only if both modes have clear product value rather
  than maintaining two half-finished surfaces.

P4.5 is UI/catalog work only. Browsing, folder traversal, search and placeholder drawing remain resource-request free.

P4.5 is closed at the source/static gate. The existing bounded catalog/query model remains authoritative; no second
browser data owner was added. The production content surface now keeps one list/table presentation with clipped direct
child folders and clipped asset results, a fixed 24x24 metadata-only placeholder slot per row, and compact Name/Type/
Import/State columns. Direct child folders appear only in ordinary direct browsing; text search, import filtering and
recursive scope suppress folder rows so query semantics remain explicit. Folder rows open by double-click or Enter,
the Up action remains, and clickable Root/ancestor breadcrumbs resolve exact catalog folder prefixes. Root labels are
consistent and 1-based. Folder/path buffers retain the full maximum resource path plus the terminator, and folder identity
is committed only after the bounded path append succeeds. Exact asset selection, right-click/context commands and both
Asset Browser instances remain independent. The fixed texture placeholder is deliberately only metadata presentation
for later P4.7 replacement; folder/list/search rendering makes no runtime texture/resource request. Fresh preflight found
2 HIGH / 1 MEDIUM presentation gaps; after implementation the final exact-current reread returned
**0 BLOCKER / 0 HIGH / 0 MEDIUM**. Scoped source/static hygiene found no trailing whitespace/conflict markers, no
Background/Interactive/thumbnail request path, and only the existing selected-detail P4.6 `Focused` preview request.
No Premake generation, compilation, executable tests, editor/native/GPU execution or input automation was performed.

#### P4.6 - selected texture preview UX

Connect exactly one focused visual consumer before background thumbnails. A selected typed texture output requests
`ProjectTexturePreviewPriority::Focused`; a reusable editor presentation seam binds the Ready project-texture backing
to an independent `EditorTextureHandle` with presentation-owned sampler/color policy, then renders the production
preview surface with correct aspect/display handling and explicit Queued/Loading/Waiting/Failed state presentation.

Selection change, catalog rebase, panel close and unsupported/non-texture selection must unregister the UI texture
first and release the backend preview with the captured UI frame. Cleanup must have an explicit panel-close/
deactivation route and cannot rely on a future panel `Draw()` call, because closed panels are not drawn. Authored
settings, build freshness, publication and resident truth remain distinct in the details UI. A pending/failed newer
authored or build state may continue to show the last committed/delivered preview only when it is explicitly presented
as that older live artifact; it must never be labeled as representing the newer state. Post-Ready publication advance
uses the P4.4 explicit old/new-handle handoff; the displayed backing is never mutated underneath the UI. P4.6 adds no
thumbnail requests.

P4.6 is closed at the source/static gate. The selected Asset Browser texture is the sole visual consumer and requests
only `Focused` interest. `EditorProjectAssetAccess` remains evidence-only; a separate
`EditorProjectTexturePreviewPresentationAccess` may register only an exact Ready preview into `EditorUi`, while the
panel owns clamp sampling, sRGB/linear display policy, aspect-aware presentation and the returned `EditorTextureHandle`.
Neutral preview evidence now includes delivered width/height, texture dimension, array-layer count and sRGB truth;
non-2D or arrayed shapes are released and truthfully labeled rather than reaching the UI renderer, and the presentation
bridge independently rejects any physical texture shape the renderer cannot bind. Supersession registers the new UI
texture before unregistering/releasing the old one, and selection change, catalog rebase, unsupported selection,
window-X close, panel destruction and extension deactivation all route through unregister-first/captured-frame teardown.
The generic panel visibility callback covers immediate visibility changes, pending close commits and singleton reopen.
The final fresh exact-current reviewer reread returned **0 BLOCKER / 0 HIGH / 0 MEDIUM** after shape-validation and
visibility-callback findings were corrected. No Premake generation, compilation, executable tests, editor launch,
native/GPU execution or input automation was performed for P4.6. P4.5 was still intentionally unexecuted at that
checkpoint; P4.5 and P4.7 were authorized and closed later.

#### P4.7 - visible texture thumbnails

Only after selected preview is correct may the browser start background thumbnail work. Connect visible/recent
texture cards to the same project-texture preview capability with `Background` priority and progressive placeholder ->
Ready binding. Browser-local thumbnail metadata/LRU bounds belong here rather than in the generic project-preview API.
Scrolling/filtering/folder changes must release or LRU-evict low-priority thumbnail interest within the P4.4 bounds.
Off-screen work may not grow unbounded, selection preview may not be starved by thumbnails, and UI texture retirement
must follow the same frame-safe ordering.

There is no copied thumbnail image, persistent thumbnail file, VDDC/VADI thumbnail truth, second GPU cache or runtime
request for folders/non-texture assets in P4.7. Dense/list presentation may remain metadata-only initially; only a
surface that actually draws thumbnails creates thumbnail demand.

P4.7 is closed at the source/static gate. Asset Browser keeps a browser-local fixed array of 256 thumbnail metadata/LRU
records, with a hard 128-record share per browser instance, while all resource/loading/residency ownership remains in the
existing project-texture preview backend. Only clipped visible adopted rows whose primary output is a texture lazily
resolve one unique texture output and request a `Background` logical preview interest. Folder rows, non-texture assets and
surfaces that do not draw thumbnails remain runtime-request free. Request failure is retry-throttled, recently visible
records retain interest for eight UI frames, query/folder/filter restarts flush panel thumbnail state, and off-screen
records are frame-bounded LRU-released. There is no CPU thumbnail-pixel copy, persistent thumbnail artifact or second GPU
cache.

Ready thumbnails bind only through the existing Ready-only presentation bridge, reuse the panel-owned clamp sampler and
sRGB/linear display policy, reject unsupported non-array/non-2D shapes, and aspect-fit inside the existing 24x24 content
slot. Every actually drawn thumbnail captures its UI frame; release unregisters `EditorTextureHandle` first and then
releases the logical preview with that captured frame. Panel close, destroy, query replacement and extension teardown all
have explicit release paths. Transient release failures detach the browser record from panel lifetime and are retried by
a rotating bounded eight-record drain every drawn panel frame, so a freed panel pointer is never retained and one group
of failing records cannot starve later detached records.

Selected texture priority remains dominant. The selected row never creates/recreates a `Background` thumbnail. When a
texture becomes the `Focused` selected consumer, the browser requests the Focused interest first so same-publication work
can share/promote the existing backing, then unregisters/releases the same-asset thumbnail. This also prevents an obsolete
thumbnail generation from pinning P4.4's single supersession residency slot indefinitely; if the Focused request cannot
admit immediately, the Background interest is still dropped to create retry headroom. P4.7 deliberately does not perform
general new-publication thumbnail reacquisition/handoff; that remains P4.8 convergence work.

The adversarial review first exposed a dangling-owner teardown hazard and two-panel metadata-pool unfairness; those were
closed by extension-owned release authority, failed-release owner detachment and the 128-per-panel share. A later HIGH
found that an old selected asset thumbnail could retain the exact supersession predecessor; the Focused-first selected
handoff and rotating detached-release retry closed that case. The final fresh exact-current reread returned
**0 BLOCKER / 0 HIGH / 0 MEDIUM**. Scoped static searches found only the intended selected `Focused` and row `Background`
preview requests, no `Interactive` row request, and no direct `ResourceStreamer`, `RequestTexture`, `ReplaceTexture`,
VDDC/VADI thumbnail or persistent-thumbnail path in Asset Browser. No Premake generation, compilation, executable tests,
editor/native/GPU execution or input automation was performed.

#### P4.8 - publication-change convergence and failure truth

Exercise and harden the exact-current convergence path while preview/thumbnail consumers are live:

- reimport/recook/publication advances reject/reacquire pre-Ready stale work and use the explicit P4.4 old/new handle
  handoff for already-visible Ready work;
- workspace resident delivery remains the only `Replace`/`ReplaceTexture` owner;
- failed rebuild/reimport never gets presented as a current delivered preview merely because an older artifact exists;
- rapid selection, folder/filter churn and multiple browser instances do not leak handles or cross-wire UI textures;
- failure/reacquire/status reporting stays bounded and truthful; and
- panel-local cancellation never cancels shared project recook.

P4.8 does not absorb P5 restart/application-lifecycle hardening and does not introduce mesh/material/prefab preview
rendering.

P4.8 is closed at the source/static gate. `ProjectAssetPipeline` now exposes an allocation-free
`ProjectAssetOutputPublicationStatus` snapshot containing only resource identity, authored generation, committed/published
build+content fingerprints and publication/build truth; it deliberately excludes the owning physical-path string and
resident fields. `EditorProjectAssetAccess` forwards that observational snapshot, letting clipped thumbnail rows observe
publication identity without copying `AbsolutePath` storage or creating a second authority. The existing full delivery
status remains for selected details that genuinely need resident information.

Selected previews and thumbnails now converge after they are already Ready. Each consumer keeps explicit candidate versus
displayed ownership. Published fingerprint changes reject stale candidates and request the new exact publication while the
last delivered UI binding remains visible. A Ready replacement is registered first and only then retires the previous UI/
preview ownership; the presentation bridge itself independently rechecks that the Ready backing still matches the current
published fingerprint pair before it can register an `EditorTextureHandle`. Pre-Ready resource/residency stale rejection
continues to use the existing workspace backend path rather than duplicating progression in UI code.

Failure truth is explicit and bounded. A Failed candidate is released and retried on an eight-UI-frame cadence; a fresh
request may retarget a shared exact-publication Failed backing back to `Queued`, preserving its existing interests,
identity and effective priority while Ready/Retiring generations remain immutable. Unsupported texture shape is instead
deterministic for one exact publication and stays suppressed until the published fingerprint pair changes. Older selected
previews remain visibly labeled as last-delivered when newer authored/build state is pending, publication status is
unavailable, the newer preview failed or the newer shape is unsupported. Thumbnails retain their last-delivered image with
an explicit `!` marker/tooltip under the same conditions rather than silently presenting the old artifact as current.

Multi-instance convergence is extension-wide. If either open Asset Browser selects a texture, both panels suppress that
asset's `Background` thumbnail path. The `Focused` request is admitted/promoted first, then all same-asset thumbnail
records across both panel owners are retired, so another browser cannot pin an obsolete supersession predecessor. This
retirement also closes a same-frame ImGui identity hazard found during review: a displayed thumbnail sampled in UI frame F
cannot unregister while the UI is still `BuildingFrame` F. It may unregister after the UI frame advances, or in the same
numbered frame only after `EndFrame` has completed and the UI service is `Ready`; the latter is required for pending
visibility/close callbacks, because `EndFrame` resolves/retains draw-list texture identities before `CommitPanelFrame`
dispatches those callbacks. Backend release still receives the captured UI frame, so producer ownership remains governed
by the P4.4 later-frame retirement fence. The selected path retries same-asset cleanup every selected frame, and ownerless
drain uses the same gate, so a later-drawn panel cannot invalidate a current-frame identity.

The backend-focused exact-current audit returned **0 BLOCKER / 0 HIGH / 0 MEDIUM**. The frontend audit found one HIGH—the
same-frame thumbnail UI-identity invalidation above—and no other BLOCKER/HIGH/MEDIUM issue; the phase-aware unregister
gate plus next-frame selected cleanup closes that exact path on source reread. Scoped static searches show only the intended
selected `Focused` and thumbnail `Background` preview requests, no `Interactive` browser request, no browser
`CancelAutomaticRecook`, direct `ResourceStreamer`/`RequestTexture` or `ReplaceTexture`, and no trailing whitespace or
conflict markers. No Premake generation, compilation, executable tests, editor/native/GPU execution or input automation
was performed for P4.8.

#### P4.9 - P4 source/static closure

Perform fresh exact-current adversarial reviews of preview ownership/generation fencing, bounds/retirement, browser UI
integration/folders and replacement convergence. Reconcile durable documentation and scoped source/static hygiene.
P4 closes only with no unresolved P4-attributable BLOCKER/HIGH/MEDIUM issue.

**Status: CLOSED on 2026-10-04 at the source/static gate.** The user explicitly authorized P4.9 with `start`.
Fresh exact-current frontend and backend adversarial reviews both returned **0 BLOCKER / 0 HIGH / 0 MEDIUM**. The
frontend review independently rechecked the P4.8 same-frame thumbnail identity correction end-to-end: a thumbnail
authored before row selection cannot unregister its `EditorTextureHandle` while the same UI frame is still
`BuildingFrame`; same-frame teardown becomes legal only after `EndFrame` returns the UI to `Ready`, later-frame
cleanup remains legal, cross-panel and detached/ownerless drains use the same gate, close callbacks run after
`EndFrame`, and backend preview release still receives the original captured UI frame for later producer retirement.

The same review found no regression in selected/thumbnail candidate/displayed convergence, bounded eight-frame Failed
retry, publication-scoped unsupported-shape suppression, extension-wide Focused selection suppression, the fixed
256-record/128-per-panel thumbnail pool, folder/query clipper integration or reset-before-query-restart behavior. The
backend review independently reconfirmed allocation-free publication status, current-publication registration fencing,
Failed same-publication reactivation, Ready/Retiring immutability, equality-only resource generation handling, the
32-progressing/64-residency caps and cumulative priority reserves, exact predecessor index + nonzero `u64` supersession
identity, later-frame retirement and workspace-only replacement ownership. Static searches show only the intended
selected `Focused` and visible-thumbnail `Background` requests in the browser, no browser recook cancellation/direct
ResourceStreamer/`ReplaceTexture`, and no P4-attributable source/static hygiene issue. No production-source correction
was required in P4.9.

Per the E4 gate structure, P4.9 does not run the broad compile/test/native proof matrix; P6 remains the ordinary
execution gate, while P5 separately owns restart/lifecycle/production-integration hardening.
No Premake generation, compilation, executable tests, editor/native/GPU execution or input automation was performed
for P4.9. P4 is therefore closed at the source/static gate; at that P4 closure checkpoint P5 and P6 remained separately
unauthorized.

### P5 - lifecycle, restart and production-integration hardening

P5 is split into three independently authorized source/static sub-phases. Completing one sub-phase is not
authorization to begin the next. Broad build/test/native execution remains deferred to P6.

#### P5.1 - live project/browser integration convergence

Close the live source-level handoff from project observation and accepted-source/catalog generation changes into
Asset Browser refresh/query/selection state. Re-audit mutation/reimport/undo/redo activity while one or two browser
instances are open, stale selection rebinding/clearing, bounded query restart, operation-state convergence and command
invalidation/cancellation semantics. Panel-local teardown must never cancel shared project recook or invent a second
observation/catalog owner. Close P5.1 only with no unresolved P5.1-attributable BLOCKER/HIGH/MEDIUM issue.

**Status: CLOSED on 2026-10-04 at the source/static gate.** The user explicitly authorized P5.1 with `start`.
Exact-current review found that live catalog/source convergence needed hardening in both the browser client and the
workspace observation owner.

The Asset Browser now distinguishes a catalog-generation rebind from a real selected-asset semantic change. A durable
adopted selection is rebound without discarding panel-local mutation text, dirty texture-settings edits or the live
preview only when the exact AssetId, per-asset accepted generation, source identity, importer/version, import/read-only
state, primary-output type, resolved primary output and ambiguity state are unchanged. The rebind still invalidates
build freshness because dependency changes elsewhere may make the selected asset stale. Any real selected-asset
semantic change continues through the existing full `SelectRow` reset path. All adopted mutation presentation,
including Apply Texture Settings, now requires both the live SourceDatabase generation and current per-asset accepted
generation to match the selection before enabling; backend mutation admission remains the authority.

Observation integration now preserves retry semantics instead of converting retry-armed reconciliation results into a
terminal FramePipeline failure. `DriveAssetFrame` treats `ReconciliationFailed`, `SnapshotSaveFailed` and
`CommittedReconciliationPending` as non-terminal owner states while unexpected/invalid observation states still fail
closed. The pipeline explicitly owns generation-zero lifecycle catch-up with `initialCatchUpRequired`: it is set when
observation starts, survives every failed/requeued catch-up path, restarts an authoritative generation-zero pass before
ordinary accumulator work, and clears only after that pass actually completes. Callback generations arriving during
catch-up remain queued for later ordinary reconciliation, automatic recook cannot enable until observation really
converges, missing-worker invariants remain terminal `InvalidState`, and DisableObservation/status truth both treat a
pending initial catch-up as outstanding reconciliation work.

Fresh exact-current frontend closure and backend sanity rereads both returned **0 BLOCKER / 0 HIGH / 0 MEDIUM** after
the corrections. Queries still fail stale rather than exposing mismatched rows, multi-instance Local `receiverData`
remains panel-specific, Candidate import retains its existing source-generation admission fence, and browser code still
owns no observer, shared-recook cancellation, direct ResourceStreamer request or `ReplaceTexture` path. Scoped static
hygiene is clean aside from existing line-ending warnings. No Premake generation, compilation, executable tests,
editor/native/GPU execution or input automation was performed. At that P5.1 closure checkpoint P5.2, P5.3 and P6 were
separately unauthorized; P5.2 was later explicitly authorized and is recorded below.

#### P5.2 - preview and teardown lifecycle hardening

Close cross-owner teardown ordering for selected preview and thumbnails across hide/close, panel destroy, extension
deactivation, shell quiesce, workspace shutdown and UI/rendering/resource-streaming teardown. Re-audit UI unregister
before preview release, captured-frame producer retirement, detached thumbnail drain, stale-handle/owner-cookie behavior,
Ready/Retiring immutability and failure/abort paths. Preserve workspace-only resident replacement ownership and shared
preview work. Close P5.2 only with no unresolved P5.2-attributable BLOCKER/HIGH/MEDIUM issue.

**Status: CLOSED on 2026-10-04 at the source/static gate.** The user explicitly authorized P5.2 with `P5.2`.
Exact-current teardown review found two HIGH lifetime defects and corrected them without changing the existing owner
split.

Selected-preview presentation now uses the same captured-frame rule already used by thumbnails. A committed context-menu
command can clear selection after `panel.draw` has authored `ImGui::Image` in the current `BuildingFrame`; displayed
preview teardown therefore defers while that exact frame is still BuildingFrame, becomes legal when the same frame has
finished into `Ready` or on a later frame, unregisters the `EditorTextureHandle` before backend release, and forwards
the original captured UI frame for producer retirement. The panel retries any deferred selected-preview cleanup before
catalog early returns on the next draw. Pending visibility close commits after `EndFrame`, panel destruction is
Ready-only, and shell owner revocation destroys panel state before extension deactivation/free, preserving the same
ordering across close/destroy/deactivate and two-panel use.

Workspace resource-request teardown now treats `PipelineRequest::Reset` as cancellation/detach rather than a completion
barrier. When the last interest leaves an unfinished `LoadingResource` preview backing, that backing becomes
request-owning `Retiring`, remains inside the existing 32-progressing/cumulative-priority budget, cannot be retargeted,
and recycles only after true request completion. Workspace quiesce/shutdown explicitly waits unfinished preview requests
before resetting preview state and likewise drains the adjacent resident-delivery request before resident-drive reset.
This closes ResourceStreaming's strict zero-active-load/read lifecycle boundary rather than relying on asynchronous read
cancellation to finish in time.

The synchronous drain is source-statically safe: workspace teardown runs while its required Rendering,
ResourceStreaming and Jobs dependencies are still live; ResourceStreaming keeps Jobs and I/O live beneath it; source
reads complete through I/O callbacks; preparation completion releases the resource-pipeline deferral; and plan/construct
work is `AnyWorker`. Completion therefore requires no later editor/UI frame. Final exact-current review also confirmed
all remaining preview request resets are terminal/no-request or post-drain, request-retiring work cannot escape the
progression cap, TextureDemand/ResourceHandle/private texture and supersession predecessor lifetime remain intact,
`QueryTexturePreview` remains observational, and the sole project `ReplaceTexture` call remains workspace resident
delivery.

Independent frontend and backend post-fix closure reviews both returned **0 BLOCKER / 0 HIGH / 0 MEDIUM**. Scoped static
hygiene is clean aside from the existing line-ending warning. Per the user constraint, no Premake generation,
compilation, executable tests, editor/native/GPU execution or input automation was performed. P5.3 and P6 remain
separately unauthorized.

#### P5.3 - restart/cache reuse and P6 proof freeze

Close source-level restart/reopen behavior around existing VMETA/VADI/VDDC/loose-publication state, project observation
catch-up, automatic recook, catalog reconstruction and texture preview/resident convergence. Warm restart must reuse
valid committed/cached truth rather than create an editor-only rebuild/cache path, while stale/missing/failed state stays
truthful and recoverable through existing owners. Reconcile final E4 documentation and freeze the smallest focused P6
proof matrix. Close P5.3 only with no unresolved P5-attributable BLOCKER/HIGH/MEDIUM issue.

**Status: CLOSED on 2026-10-04 at the source/static gate.** The user explicitly authorized P5.3 with
`okay start .3`. Exact-current review confirmed that restart reconstruction remains in the production owners: current
VMETA/source truth is rescanned/reclassified/fingerprinted; the persisted accepted snapshot is comparison evidence;
VADI is reopened through `DependencyIndex`; exact VDDC records are payload-fingerprint validated; desired loose
publication descriptors are reconciled through `ProjectLoosePublisher`; generation-zero observation catch-up completes
before workspace automatic recovery is enabled; and current outputs evaluate through the existing VADI so `UpToDate`
records do not recompile/reimport. Missing/corrupt VADI or VDDC remains explicit dirty/cache-repair work through the
existing automatic-recook owner rather than a second editor recovery path.

The frontend restart path reconstructs volatile catalog/panel state rather than persisting borrowed UI state. Fresh
panels begin with no selection/query result/drafts/preview/thumbnail presentation handles or publication-scoped failure
suppression; ProjectAssetCatalog rebuilds incrementally from SourceDatabase, adopted selection may rebind by AssetId,
Candidate selection clears, and two browser instances remain independent. Fresh workspace preview handles are fenced by
the process-monotonic owner cookie plus per-interest generation and therefore cannot resurrect a prior service's handle.

One backend HIGH was found and corrected. Fresh pipeline publication bookkeeping is session-local, so startup
`Reconcile()` republishes every desired descriptor. Previously that path always rematerialized the deterministic loose
`.vres` from VDDC and atomically replaced the target even when the committed loose file was already exact. That violated
the restart contract and made warm startup perform unnecessary project-sized writes. The existing materializer now
validates an existing target against the exact selected VDDC payload using bounded streaming SHA-256, while the target
read computes the CRC64 required by the ordinary ResourceStreamer descriptor. Exact size+digest returns Success and
reuses the target without rewrite. Missing/mismatched/unreadable loose state retains the existing temporary -> staged
size+digest verification -> atomic `ReplaceFile` fallback; missing/corrupt VDDC fails closed. No second cache, loader,
publication owner or residency owner was introduced.

Same-process cleanup remains conservative: successful pipeline shutdown unregisters loose descriptors and releases its
managed root only after active source/object leases are gone; session-local maintenance/request/publication state is not
replayed. Resident delivery and texture preview reacquire from current publication evidence. Workspace resident delivery
remains the only project `TextureResidencyRuntime::ReplaceTexture` owner and preview remains Request-only with
observational `QueryTexturePreview`.

Fresh frontend restart review, backend post-fix review and an independent focused loose-reuse integrity review all
returned **0 BLOCKER / 0 HIGH / 0 MEDIUM**. Scoped source/static hygiene is clean aside from existing line-ending
warnings. Per the user constraint, no Premake generation, compilation, executable tests, editor/native/GPU execution or
input automation was performed. At that P5.3 checkpoint P6 remained separately unauthorized; it was later explicitly
authorized with `ok start` and is recorded below.

### P6 - focused execution and E4 closure

P6 is the only ordinary execution gate. P5.3 froze the smallest focused proof set below, and the user later explicitly
authorized its execution with `ok start`.

1. **Generation/build/link:** run Premake only if exact-current project structure requires regeneration, then focused
   Debug + Development compile/link for the touched shared asset/asset-streaming/editor targets and the focused existing
   test executables below. Do not broaden into an all-solutions validation pass without evidence.
2. **`assetsPersistentCacheTests` loose-reuse oracle:** extend/use the existing materializer fixture to prove an exact
   deterministic target can be made read-only and still returns Success with the correct byte count/CRC because it is
   reused rather than replaced; a mismatched target follows the existing atomic rematerialization path once writable;
   missing/corrupt VDDC fails closed and preserves prior target truth.
3. **`assetStreamingTests` restart/cache oracle:** reuse the existing real ProjectAssetPipeline restart fixtures. For a
   clean VMETA/VPAS/VADI/VDDC/loose restart, assert accepted/current identity survives, descriptor reconciliation
   succeeds, startup recovery reaches Idle with no additional compiler call for `UpToDate` outputs, and current loose
   publication is immediately requestable. Keep the existing dirty-source/missing-VADI and missing/corrupt-VDDC cases as
   the negative proof that recovery uses the same recooker/compiler and does not replay transient queues.
4. **Browser/headless model oracle:** use the existing catalog/shell focused surfaces to prove initial catalog rebuild,
   stale-query restart, adopted-AssetId rebind versus Candidate clear, current-generation mutation enablement, two
   independent browser instances, and zero ResourceStreamer request from folder/list/search traversal alone.
5. **Runtime delivery/preview lifetime oracle:** use the existing editor asset-retirement/workspace fixture to prove
   committed loose -> ResourceStreamer -> texture residency -> UI registration, N-to-N+1 resident replacement with
   workspace-only `ReplaceTexture`, preview reacquire rather than replace, selected/thumbnail captured-frame retirement,
   the frozen 32-progressing/64-residency bounds, and quiesce leaving zero preview/resource-request ownership before a
   clean same-process workspace reconstruction.
6. **Ordinary production smoke:** one Project Cardinal editor launch/manual review of Candidate/import/settings or
   reimport, two Asset Browser instances, current texture preview/thumbnail and restart/reopen reuse as needed to cover
   the production composition not represented headlessly. Native input-driving automation remains separately gated and
   requires the explicit warning immediately before any such run.

Interactive mouse/keyboard/focus automation is not authorized by Definition. Warn the user immediately
before any later explicitly requested input-driving native automation.

**P6 status: CLOSED on 2026-10-04. E4 status: CLOSED.** The focused backend matrix passed in Debug and Development:
`assetsPersistentCacheTests` covered exact warm-loose reuse/byte-count/CRC and fail-closed VDDC behavior, while
`assetStreamingTests` covered clean restart/cache recovery plus existing dirty/missing-cache negative paths. The
production-used browser policy oracle passed in `editorShellTests` Debug and Development, covering initial catalog sync,
stale-query fencing/restart, adopted AssetId rebind versus Candidate clear, current-generation mutation enablement,
independent panel-local state and zero preview request from catalog sync/search/filter traversal. The production workspace
preview/replacement oracle passed in `editorAssetRetirementTests` Debug and Development, including committed loose ->
ResourceStreamer -> texture residency -> UI registration, shared interests, priority promotion, N -> N+1 preview
reacquire, unregister-before-release captured-frame retirement, the frozen 32-progressing/64-residency bounds, quiesce
drain/reset and stale owner-cookie rejection. The default wrapper and both child proofs passed.

Final adversarial review found one production MEDIUM in the frozen residency partition: if ordinary Background or
Interactive reserve admission failed early, the special supersession path could previously consume a physical slot below
64. The production classifier now permits that exception only at `held == 63`, only when no supersession slot is already
active, and only for an exact same-output differing-publication predecessor whose index and monotonic nonzero backing
identity are retained. The same review found a proof-coverage MEDIUM because the oracle pinned only the 32/64 headline
bounds. It now compile-time pins the full frozen numeric contract (512 interests, 32 progressing, 64 residency,
4 starts/tick and 32 state checks/tick) and directly asserts that held=62 cannot use the special slot while held=63 can
only under the exact predecessor/no-existing-slot conditions. Fresh Debug and Development `editorAssetRetirementTests`
builds/runs passed after both corrections; the independent exact-current rereview returned 0 BLOCKER / 0 HIGH / 0 MEDIUM.

The current Development editor then built successfully. A first post-fix attempt encountered a transient
`redReflectionCompat.pch: Invalid argument` compiler-intermediate failure outside E4; after its compiler children drained,
a focused serial `redReflectionCompat` rebuild passed and the serial editor retry passed without source changes. Project
Cardinal was then launched twice from that exact-current binary without scripted
mouse/keyboard/focus input. Both launches produced a responsive `Project Cardinal - Vanguard Editor` main window; both
accepted the normal close request and exited cleanly. Because input-driving automation was not authorized, this native
smoke does not claim manual Candidate/import/settings, two-browser or preview-click interactions; those focused behaviors
are established by the production-backed executable oracles above. Final exact-current ownership/bounds/lifetime/browser
review and scoped hygiene leave **0 BLOCKER / 0 HIGH / 0 MEDIUM** attributable to E4.

Premake regeneration was not made green: the known source-lineage audit still rejects unrelated RenderFlow/scheduler
files. E4 did not modify those files or weaken the audit. Durable `editor/premake5.lua` includes production extension
sources and the focused test dependencies; the current generated `editor.vcxproj` contained `editor_asset_browser.cpp`
for the successful focused Development editor build.

E4 therefore closes at this focused acceptance gate. Any later editor-foundation phase remains separate work.

## E4 exit contract

E4 is complete when all of the following are true in the ordinary production architecture:

1. A supported source copied/dropped into a writable project source root appears as a Candidate without
   requiring a successful build record.
2. The user can explicitly Import it; the shared project pipeline assigns durable identity/default
   metadata transactionally and builds through the existing BuildGraph/VDDC path.
3. The browser reports truthful import/build/error/progress state and remains metadata-only while browsing.
4. Folder navigation, search/filtering and at least two browser instances operate independently.
5. Dependencies and referencers come from the existing authored-reference index without loading closure.
6. Move/Rename, Duplicate, Delete, Reimport and supported settings edits invoke shared project commands,
   preserve accepted identity/reference policy and surface failures.
7. A texture output can be previewed through the real committed loose resource, runtime decoder and
   texture residency path; preview work is bounded and lifetime-safe.
8. Reimport/settings changes distinguish applied, built and delivered state, and never silently display a
   stale/failed candidate as current.
9. Restart reuses committed VADI/VDDC/loose state rather than eagerly reimporting everything.
10. Project/editor close with observation/recook/preview work active drains/cancels through existing owners
    without callbacks or borrowed rows surviving teardown.

## Files and owners expected to matter

P0 does not authorize editing these files; this is the exact-current implementation map for later gates:

- `source/assets/include/vanguard/assets/source_database.hpp`
- `source/assets/src/source_database.cpp`
- `source/assets/include/vanguard/assets/asset_file_transaction.hpp`
- `source/assets/src/asset_file_transaction.cpp`
- `source/assets/include/vanguard/assets/asset_operations.hpp`
- `source/assets/src/asset_operations.cpp`
- `source/assetStreaming/include/vanguard/asset_streaming/project_asset_pipeline.hpp`
- `source/assetStreaming/src/project_asset_pipeline.cpp`
- `source/textureTools/include/vanguard/texture_tools/texture_asset_compiler.hpp`
- `source/textureTools/src/texture_asset_compiler.cpp`
- `source/streaming/include/vanguard/streaming/streaming.hpp`
- `source/streaming/src/streaming.cpp`
- `editor/private/vanguard/editor/editor_project_service.hpp`
- `editor/src/editor_project_service.cpp`
- `editor/private/vanguard/editor/editor_shell_service.hpp`
- `editor/extensions/editor_shell_service.cpp`
- `editor/framework/include/vanguard/editor/editor_ui.hpp`
- the existing editor texture-retirement helper/scene-output seam used to share the F-to-F+1 lifetime rule;
- `editor/private/vanguard/editor/editor_static_extensions.hpp`
- `editor/extensions/editor_production_extensions.cpp`
- a new E4 production browser extension translation unit under `editor/extensions/`
- focused existing asset/editor test targets only in P6 if implementation requires proof expansion.

P1 and each later gate revalidate this file list again because this is a shared dirty checkout and concurrent
work may change source boundaries before implementation begins.
