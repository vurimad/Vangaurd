# Shared project and asset foundation

## E1A - shared project workspace

Source-level implementation: `projects::ProjectWorkspace` and `ResolveWorkspace` now own reusable directory resolution, filename validation and lexical non-overlap checks. Editor bootstrap, its workspace service and nanovanguard layout validation consume the same resolver. The existing project parser and physical I/O paths remain in use. No new services, locks, build graph or runtime resource machinery were introduced.

The reusable backend belongs to `source/projects` and `source/assets`; the editor is a consumer. Engine/plugin source roots are read-only by policy and must be explicitly registered in the source-catalog slice. Resolving the project Plugins directory does not implement that registration. Physical link containment must be settled before file mutation operations; lexical paths alone do not authorize them.

Verification: source/diff inspection only. No project generation, compilation, test execution or test authoring in this slice. Runtime behavior is not yet verified.

## Remaining agreed slices

### E1C first slice - source database

`assets/source_database.hpp` now implements a headless source table and path/AssetId/output indexes. Explicit source roots default read-only; overlapping roots and duplicate source paths are rejected. A checked inventory can be rebuilt through the existing filesystem reader. Missing sources, missing/unreadable/invalid metadata and conflicting identities stay visible as records. Conflicting asset/output lookups refuse to choose a winner. No importer, build system, renderer or editor service is required.

The structural reference is `backendData/resourceDatabase`: enumerate inputs, scan records, apply completed tables, then borrow records through queries. This slice implements synchronous exclusive-owner rebuild, not parallel Jobs dispatch. Readers must finish before rebuild; borrowed pointers/spans expire on successful rebuild. No per-record or per-query locks were added. Staged scan data exists only during rebuild, not as a per-frame snapshot system.

The initial database slice required a complete caller-provided inventory. E1C.1 below supplies the physical enumeration adapter. The catalog does not generate IDs, persist another authoritative database, track build state, detect external non-catalog resource collisions, or mutate source files. E1D owns reference edges; VADI remains build-record authority.

### E1C.1 - checked filesystem inventory and rescan

Source implementation complete, executable validation deferred. `filesystem::ScanFiles` adds a bounded checked physical scan without changing the inherited FindFiles callers. On Windows it distinguishes normal enumeration completion from errors and refuses reparse points in the root/ancestor chain and discovered entries. Other platforms explicitly return UnsupportedPlatform. It does not take a filesystem snapshot or protect against an external actor changing files/links during scanning; callers requiring consistency must coordinate those changes. This is not an authorization primitive for future mutation operations.

`SourceDatabase::Rescan` validates explicit non-overlapping roots, enumerates all roots before rebuilding, combines source/sidecar pairs and maps orphan sidecars back to missing source records. Scan failures leave the previous catalog unchanged. Complete empty scans may remove old rows; missing/inaccessible roots are failures, not empty inventories. Source paths ending in `.vmeta` are reserved metadata, not independently imported sources. The scan is read-only and does not generate sidecars or IDs.

E1C.1 implementation is followed by E1C.2 below and then E1C.3.

Source and diff review only; no generation, compilation or tests run/authored.

### E1C.2 - compiler discovery and queries

Source-level implementation complete; no executable validation. `SourceDatabase::ClassifySources` consumes format-owned `SourceImporterDescriptor` values, verifies descriptor validity/unique IDs, and stores only classification/identity/type values. It does not retain descriptor or tool pointers or add another registry. Tool lifetime must remain stable during classification. Successful rescans reset classification, which the owner must rerun.

The source reference is `backendData/generator.cpp`: EnumerateGenerators and IGenerator::Create use generator-owned CanGenerate. Vanguard retains explicit source importer descriptors rather than introducing RTTI discovery. The optional recognizeSource callback is a filename routing hint only. Metadata-selected importer IDs and versions are resolved directly; no unavailable importer is silently substituted. Ambiguous matches, missing hooks, invalid metadata and version mismatches are distinct states. SourceMismatch means the filename hint disagrees, not proof that decoding is impossible. Source issues remain independent from classification; a missing or conflicting source is never made buildable by a candidate hint.

Mesh importer recognition uses Assimp's supported extensions gathered by meshTools, without a catalog-owned format list. Classification is an explicit tool operation, not per-frame/per-query work. Importer descriptors without recognition hooks remain explicitly DiscoveryUnavailable for unassigned sources; existing metadata can still select their registered IDs. Format-specific routing expansion and project importer composition must use existing tools, not an extension switch in the source database.

`VisitResources` implements explicit RED-style filtered traversal with folder/direct-child, source/output type, issue masks and import-state filters. Callers receive borrowed records and may stop early. It adds no collection allocations, disk I/O, copies or locks. This is a linear filtered traversal, not a claim of indexed arbitrary search. Exact path/asset/output lookup retains the existing indexes. Output-type matching includes all authored outputs, not only an importer's primary type. Folder matching uses directory boundaries and existing case-insensitive path policy.

### Correction pass and E1C.3 - source implementation complete

Root validation is shared by Rebuild and Rescan; duplicate paths are checked through the retained path index rather than a second temporary map. Rescan's source/sidecar pairing remains necessary input normalization. MeshAssetCompiler now gathers Assimp extensions once during initialization, not once per source; its existing instance owns that capability list.

Editor composition supplies the already-read ProjectDescriptor to the existing ProjectWorkspaceService. The service resolves roots through source/projects and owns one shared SourceDatabase, with const queries and explicit owner-thread RescanSources/ClassifySources operations. No second project service, compiler registry or parser was added. Readers must finish before rescan/classification and service destruction; no asynchronous catalog jobs or locks were added.

`nanovanguard sources inspect <project> [--format jsonl]` uses the same project resolver and SourceDatabase. It uses a local native filesystem manager for absolute-path reads, without installing a global manager or mounting runtime resources. Empty successful scans emit a zero-count summary; failed scans emit no partial inventory.

Both consumers currently compose the project's explicit writable Assets root. Additional read-only roots remain explicitly supplied, never guessed from Plugins children. A complete importer set is not yet composed in either consumer: records start NotClassified, and the service accepts existing descriptors for explicit classification. Actual tool composition, importer settings and source/dependency callbacks belong to E1E. Do not create a second registration list just for browsing.

Source review: reopening reconstructs inventory from source/sidecar truth; runtime output identities remain path-independent. Failed enumeration preserves the previous catalog, successful rescans invalidate borrowed pointers, and orphan sidecars retain missing-source records. No source files are mutated and no streaming source ownership is changed. The other thread's ResourceStreamingService remains the sole configured mount owner.

Verification: scoped diff and source/call-site review only. No generation, compilation, tests, test authoring, editor launches or CLI runs. Restart/relocation/rescan/shutdown behavior and the added CLI linker declarations remain unverified at runtime. Existing Premake globs cover new files.

**Zero E1C implementation subphases remain; next is E1D**, followed by E1E build integration and E1F file operations. Executable validation is deferred debt, not a passing result.

### E1D.1 - authored-reference representation and index

Source-level implementation complete; executable validation remains deferred. `SourceDatabase` now owns RED-style authored links and keeps forward- and backward-sorted views. A link records the existing source `AssetId`, the target's typed runtime `ResourceReference`, required/optional/soft semantics, primary/inner location and in-place status. The source must be a unique catalog asset; the target is deliberately allowed to be absent. Callers resolve an existing target separately through `LookupOutput`, so missing targets remain queryable instead of requiring cached target-state flags.

`SetReferences` transactionally replaces the complete link index, rejects invalid sources and untyped targets, removes exact duplicates and preserves the previous index on failure. Catalog rebuild clears links because their source rows and scan results are no longer current. Forward and backward queries return borrowed contiguous spans using binary search; they perform no allocation, I/O or locking.

This is only the shared representation/apply half of scanning. It does not parse arbitrary source text, reuse VADI build dependencies as authored links, infer runtime loading behavior, rewrite files or add an editor registry. E1D.2 supplies format-owned scanning through existing tool ownership. E1D.3 connects scan status and editor/headless queries and closes restart/rescan behavior.

**Two E1D implementation subphases remain: E1D.2 and E1D.3.**

### E1D.2 - format-owned reference scanning

Source-level implementation complete; executable validation remains deferred. Format-owned source importer descriptors carry reference-scanning callbacks; low-level artifact compiler descriptors do not know about project catalogs or authored documents. `AssetReferenceCollector` accepts typed targets plus required/optional/soft, primary/inner and in-place properties without knowing about the source database or editor.

The current direct production importers supply the callback. Mesh settings report explicitly authored default-material and skeleton references; texture, shader and input-mapping scanners validate their formats and report an empty reference set. Shader include files and mesh external files remain source/build dependencies rather than being mislabeled as runtime-output referencers. Canonical material program, pipeline and value inputs are internal compiler inputs, not authored project formats, so their compiler descriptors no longer masquerade as source importers.

Direct scanning consumes the existing `BuildRequest`, allowing simple format code to reuse its parser and the project-backed source/settings adapter. Native multi-output formats may scan authored source directly through the same importer descriptor. The source database does not open or decode arbitrary formats, and no editor parser or new dependency scheduler was added. Exact duplicates remain legal scanner output and are collapsed by E1D.1's sorted database apply step, matching repeated references in authored documents.

**One E1D implementation subphase remains: E1D.3 - scan coordination, visible per-source status, editor/headless queries and restart/rescan closure.**

### E1D.3 - scan coordination and lifecycle closure

Source-level implementation complete; executable validation remains deferred. `SourceDatabase::ScanReferences` performs a synchronous exclusive-owner scan over the current catalog. It validates one supplied view of source importer descriptors, asks a caller-owned loader for borrowed source/metadata bytes, and invokes the format-owned scanner immediately. A direct importer first constructs its existing `BuildRequest`; a native importer may scan the authored document directly. E1E remains responsible for complete importer composition; the database does not independently open formats or retain source buffers.

Every source exposes `SourceReferenceScanState`: NotScanned, Scanned, ImporterUnavailable, InputUnavailable, ScannerUnavailable or ScanFailed. These states can be filtered through the existing `SourceQuery`. A successful source replaces its previous forward links, while a source-level failure retains its previous links and records the failure state. Invalid registries, invalid output contracts and allocation failures preserve the complete previous index and states. The final combined table is still applied transactionally through E1D.1.

Classification invalidates the reference index and resets scan states because a changed importer interpretation makes old links unsafe. A successful catalog rebuild likewise publishes new rows with no references; failed rescans leave the previous catalog, links and states untouched. The editor workspace only delegates to this shared API and exposes the const database for forward/backward queries. Headless tools can use the same API directly; there is no editor-owned asset backend.

The lifecycle matches the retained resource-database pattern: enumerate/classify, scan into per-source results, preserve old links for individual scan failures, then publish forward/backward sorted tables. No background scheduler, watcher, database cache file, per-query lock, source rewrite or build/runtime dependency conversion was added.

**Zero E1D implementation subphases remain. Next is E1E - project-backed compiler/build/DDC integration and committed loose-artifact publication.**

Verification: source review only; no compilation, project generation, tests or test authoring.

### E1E.1 - checked project source-data boundary

Source-level implementation complete; executable validation remains deferred. `ProjectSourceData` and `ProjectSourceDataProvider` now provide one shared, bounded reader for the exact source file and `.vmeta` sidecar represented by a `SourceRecord`. The provider is owned by `source/assets`, not the editor, and accepts the existing filesystem manager, catalog row and target platform. The editor workspace uses it for E1D reference scanning instead of requiring callers to invent their own file loader. The same borrowed view is the input boundary for the importer-owned build-request creation in E1E.2.

Loading refuses missing/conflicting catalog rows, enforces source/metadata byte limits, reparses the sidecar and compares its AssetId, importer/version, settings and complete output mapping with the catalog row. A sidecar changed after the last rescan therefore reports `CatalogOutOfDate`; it is never combined implicitly with stale catalog settings or identities. Source bytes are the exact bytes read for the request and later fingerprinting. The provider owns one reusable buffer pair, performs no filesystem scan, creates no jobs and retains no complete project working set.

Re-evaluation found an important E1E seam before build composition: `CompilerDescriptor` is currently serving both as a low-level artifact compiler and as source-importer discovery. That direct relationship is valid for a simple PNG-to-VTEX or FBX-to-VMESH route, but it cannot represent one Vanguard-authored material document producing VSHADER, multiple VPPL resources and VMAT through several existing artifact compilers. E1E.2 must separate importer-owned source interpretation/build-request creation from the existing compiler/DDC registry. It must not add a second build scheduler or replace the existing material canonical builder.

**Four E1E implementation subphases remain:**

1. **E1E.2 - importer/build-request composition:** introduce the minimal source-importer boundary, adapt current simple importers, and permit one authored source to emit multiple typed build requests through its format-owned frontend.
2. **E1E.3 - project BuildSystem/BuildGraph/DDC ownership:** initialize the existing cooker against the resolved DerivedData root, register the composed compiler set once, resolve generated dependencies through exact source/output identity, and commit successful VADI records.
3. **E1E.4 - committed loose-artifact publication:** materialize exact successful outputs transactionally and supply immutable `LooseResourceDescriptor` records to the existing Resource Streaming service without scanning DerivedData.
4. **E1E.5 - lifecycle and restart closure:** preserve last-valid publications across failed builds, define rescan/build invalidation ordering, restore catalog/build state without eager cooking, and close shutdown ownership. Automatic watching and broad background reimport policy remain E2.

Verification for E1E.1: scoped source and call-site review plus `git diff --check`. No generation, compilation, tests, test authoring, editor launch or CLI execution.

### E1E.2 - importer and build-request composition

Source-level implementation complete; executable validation remains deferred. Project source interpretation is now explicitly separate from low-level artifact compilation. `SourceImporterDescriptor` owns filename recognition, authored-reference scanning and translation of one exact metadata-declared output into a `BuildRequest`. `CompilerDescriptor` is again only the BuildSystem/DDC compiler contract. The source database and editor workspace consume importer descriptors and retain no callbacks after classification or scanning.

`MakeDirectSourceImporterDescriptor` adapts the existing one-source/one-output mesh, texture, shader and input-mapping tools without adding another compiler, registry or scheduler. Their importer and compiler share the same stable ID/version and tool instance. `CreateSourceBuildRequest` accepts only an output declared by the current `.vmeta`, preserves the provider's exact source/metadata bytes and target, and verifies custom importer results before handing them to BuildSystem. A native authored format can provide one callback that creates a different typed request for each declared output, which supports material-like documents producing shader, pipeline and material artifacts through the existing specialized compilers.

The canonical material program, pipeline and value compilers are intentionally not project source importers. Their inputs are generated intermediate descriptions, not user-authored files. A material source importer will be registered only when the authored material document codec/frontend exists; inventing an extension or serializing the current in-memory canonical build set would create a false source-of-truth format. Current texture candidate discovery covers the built-in PNG, JPEG, TIFF, OpenEXR and DDS routes; plugin formats without a filename contract remain metadata-selected rather than guessed.

**Three E1E implementation subphases remain:**

1. **E1E.3 - project BuildSystem/BuildGraph/DDC ownership.**
2. **E1E.4 - committed loose-artifact publication.**
3. **E1E.5 - lifecycle and restart closure.**

Verification for E1E.2: source, call-site and scoped-diff review plus `git diff --check`. No generation, compilation, tests, test authoring, editor launch or CLI execution.

### E1E.3 - project BuildSystem, BuildGraph and DDC ownership

Source-level implementation complete; executable validation remains deferred. `ProjectAssetBuildSystem` is the shared project cooking owner in `source/assets`. A host supplies the resolved artifact-cache and dependency-index directories, target/settings identity, one composed compiler set and its matching source-importer set. Initialization copies and validates the importer descriptors, initializes the existing `BuildSystem` persistent VDDC cache, registers each existing compiler exactly once, restores the existing VADI `DependencyIndex`, and binds the existing asynchronous `BuildGraph` to both. The editor is only a future consumer of this owner; no editor compiler registry, scheduler, cache or dependency table was added.

One request starts from a typed metadata-declared output. The owner resolves that output to its unique `SourceRecord`, requires the exact registered importer version, reloads the bounded source and sidecar through `ProjectSourceData`, asks the format-owned importer to create the existing `BuildRequest`, and submits it to `BuildGraph`. Generated dependencies follow the same output lookup and request construction path. The graph already coalesces work, schedules Jobs, consumes VDDC hits and publishes successful build plans/results into VADI; this layer does not reproduce any of those mechanisms.

The source side of `BuildRequest` now uses the source-root-relative path, including the source extension. Cooked output identity remains the stable AssetId/output-key path. This distinction is required for shader-relative includes and mesh auxiliary files: moving an asset with its sidecar keeps its cooked identity, while source-file relationships continue to use real depot-relative names. Duplicate relative source identities across mounted roots are reported and refused rather than resolving an arbitrary file.

Shutdown first refuses live graph requests/operations, then saves VADI, closes the index, and finally closes the compiler/DDC owner. Importer and compiler callback state must outlive the project build owner, and source-catalog mutation requires all graph requests to be released. E1E.5 will connect these constraints to rescan/restart policy; they are explicit here rather than hidden behind locks or copied catalog state.

**Two E1E implementation subphases remain:**

1. **E1E.4 - committed loose-artifact publication.**
2. **E1E.5 - lifecycle and restart closure.**

Verification for E1E.3: source, ownership, graph/index call-path and scoped-diff review plus `git diff --check`. No generation, compilation, tests, test authoring, editor launch or CLI execution.

### E1E.4 - committed loose-artifact publication

Source-level implementation complete; executable validation remains deferred. `ProjectLoosePublisher` is the shared bridge between committed project build state and the existing runtime streamer. Given one typed output identity, it reads the exact successful VADI record, requires the configured target, reconstructs that resource from its recorded VDDC segments through `LooseResourceMaterializer`, computes the runtime CRC during the existing validation read, and publishes one `LooseResourceDescriptor` through `ResourceStreamer`. It does not scan DerivedData, infer output filenames, schedule builds or maintain another resource registry.

Physical loose files are opaque, immutable build generations under the resolved project loose root. Their path is derived from the stable resource identity and exact build fingerprint rather than an authored filename or semantic extension. Materialization writes a sibling temporary file and replaces the generation only after segment bounds, byte count and SHA-256 validation succeed. A failed build has no committed record for publication; a failed materialization or streamer update leaves the previously published descriptor intact. Superseded physical generations are deliberately retained for E1E.5 cleanup and restart recovery.

Only VADI dependencies marked `Generated` become runtime streaming dependencies, using the same Required, Optional and Soft mapping as package planning. Source, tool and settings dependencies remain build invalidation inputs and never enter runtime load fan-in. `ResourceStreamer::PublishLoose` atomically installs or replaces the immutable descriptor under its existing source lock. Requests that already opened the previous file retain their owned source, later resolutions see the new generation, and changing the resource type behind an existing stable identity is rejected.

The publisher lives in the narrow `assetStreaming` integration module so core assets remain independent of runtime streaming and the streamer remains independent of project/DDC tooling. It is callable only after a `GraphRequest` succeeds and its VADI record has committed. E1E.5 will give the project/editor service ownership of this bridge, drive it from completed requests, restore committed publications on restart without eager cooking, and define safe stale-generation cleanup. Publication does not force-reload an already resident runtime object; reload policy belongs to lifecycle/invalidation coordination.

**One E1E implementation subphase remains: E1E.5 - lifecycle and restart closure.** Automatic watching and broad background reimport policy remain E2.

Verification for E1E.4: source, ownership, materialization, dependency-mapping, streaming replacement and scoped-diff review plus `git diff --check`. No generation, compilation, tests, test authoring, editor launch or CLI execution.

### E1E.5 - lifecycle and restart closure

Source-level implementation complete; executable validation remains deferred. `ProjectAssetPipeline` in the generic `assetStreaming` integration module now composes the existing `ProjectAssetBuildSystem`, VADI/VDDC state, `ProjectLoosePublisher` and `ResourceStreamer` relationship. A frontend supplies its already composed compiler/importer descriptors and resolved project directories, giving editor and headless composition the same owner rather than requiring editor-private registries, schedulers, dependency tables or caches.

`ProjectAssetRequest` retains the existing asynchronous `GraphRequest` and its exact typed output identity. Polling a successful request first persists the graph-committed VADI state, then materializes and atomically publishes that committed generation. A failed or cancelled graph request, an index-save failure, or a loose publication failure never replaces the previous runtime descriptor. Publication failures remain retryable while the successful request is retained. Requests must be released before source mutation or shutdown, matching the underlying graph's real lifetime rather than hiding it behind a second job system.

Initialization classifies the current source catalog and refreshes authored-reference scans before starting the cooker. It then restores every current metadata-declared output that has a valid committed VADI/VDDC record; missing or unavailable records are reported without triggering eager cooking or preventing the remaining valid project from opening. `Rescan` is one ordered owner operation: it refuses live graph work, replaces the physical inventory, reclassifies through the registered importers, refreshes authored references, reconciles committed publications, and unpublishes outputs no longer present in the catalog. Already published outputs are retained across a rescan until a later successful build replaces them.

The pipeline records only the loose descriptors it owns so shutdown can unregister those sources before closing the publisher, saving/closing VADI, and closing BuildGraph/BuildSystem/VDDC. This is lifecycle bookkeeping, not another resource registry: resolution remains in `ResourceStreamer` and build authority remains in VADI. Generation cleanup is conservative. A successful replacement may delete only its superseded immutable file after the streamer swap; restart reconciliation scans only the dedicated loose-publication root and deletes strictly shaped stale generation or staging names for identities that were successfully restored. Unknown files and generations whose records could not be restored are retained, and files still held by readers are deferred.

**No E1E implementation subphases remain. E1E is source-level complete.** The next asset-foundation phase is **E1F - editor asset operations and transactions**. Automatic filesystem watching, broad background reimport scheduling and resident-resource reload policy remain E2 work.

Verification for E1E.5: source, ownership, request/commit/publication ordering, restart reconciliation, rescan exclusion, shutdown ordering, stale-generation safety and scoped-diff review plus `git diff --check`. No generation, compilation, tests, test authoring, editor launch or CLI execution.

### E1F - editor asset operations and transactions

E1F is divided into four implementation subphases. The reusable implementation belongs to the shared assets/project integration; editor UI and headless tools consume it instead of owning a second catalog, dependency graph, filesystem transaction system or build scheduler.

1. **E1F.1 - mutation preflight and format-owned rewrite contract:** plan reference retargeting against the current E1D index, require writable sources and exact live importer versions, and make schema owners responsible for producing rewritten source/metadata bytes without performing I/O.
2. **E1F.2 - durable filesystem transaction and recovery:** stage complete source/sidecar replacements and file moves under a bounded journal, validate the staged set, keep catalog readers outside the commit window, and deterministically recover or roll back after interruption.
3. **E1F.3 - move, rename, duplicate, delete and retarget execution:** preserve stable AssetIds across moves, assign new identities for duplicates, apply reference policy for deletion/replacement, and invalidate/reconcile through the existing project asset pipeline only after a committed source transaction.
4. **E1F.4 - editor/headless commands, undo/redo and lifecycle closure:** expose the shared operations to frontends, retain bounded inverse transaction records, coordinate live build requests and rescans, and close restart/shutdown behavior. Automatic external watching remains E2.

### E1F.1 - mutation preflight and format-owned rewrite contract

Source-level implementation complete; executable validation remains deferred. `PlanAssetReferenceReplacement` is a checked, write-free operation over the current `SourceDatabase`. It accepts one typed old/new runtime-output pair, requires the replacement to resolve to a current catalog output of the same type, walks the existing reverse authored-reference index, and groups the exact affected links by source so a later executor loads each document once. Missing old targets remain legal because E1D deliberately retains broken references.

Every affected source must have a current successful reference scan, belong to a writable registered source root, and resolve to one exact-version live `SourceImporterDescriptor` with a rewrite hook. Read-only engine/plugin inputs, retained links from failed scans, unavailable/version-mismatched importers, and formats without rewrite support fail preflight with source/importer/target diagnostics. Planning is transactional in memory: any failure leaves the caller's previous plan unchanged and performs no filesystem I/O, catalog mutation, cooking, publication or editor work.

`RewriteSourceReferencesFunction` extends the existing format-owned importer boundary rather than teaching the source database to parse arbitrary documents. A format receives the borrowed catalog row/source data plus typed replacements and must return complete rewritten source bytes and metadata without writing files or retaining input spans. E1F.2 owns validation, staging and commit; E1F.3 will add concrete format rewriters and invoke them. Authored links remain distinct from VADI build dependencies and runtime streaming dependencies.

**Three E1F implementation subphases remain: E1F.2, E1F.3 and E1F.4.**

Verification for E1F.1: source, ownership, reference-index, importer-boundary and scoped-diff review plus `git diff --check`. No generation, compilation, tests, test authoring, editor launch or CLI execution.

### E1F.2 - durable filesystem transaction and recovery

Source-level implementation complete; executable validation remains deferred. `AssetFileTransaction` is a bounded single-owner transaction for writes, moves and deletes under explicitly supplied writable source roots. Collection is memory-only. `Prepare` records original target existence in a checksummed VTXN journal, atomically publishes its Staging state, writes and byte-verifies sibling staging files, then atomically advances the journal to Prepared. Move and delete inputs must already exist; every moved or replaced file is bounded, and moves are restricted to one registered root so the underlying write-through rename does not silently become a cross-volume copy.

`Commit` runs only while catalog/build/frontend readers are excluded. Existing targets are renamed to deterministic sibling backups before replacement; moves and deletes retain enough physical state for reverse-order rollback. Once every mutation succeeds, an atomically replaced Committed journal becomes the durable commit point. Cleanup after that point is roll-forward only: failure returns `CommittedCleanupPending` and retains the journal for startup recovery instead of reporting an ambiguous rollback. Before the commit marker, any failure rolls the complete set back; if rollback itself cannot finish, the owner enters `RecoveryRequired` and leaves the Prepared journal authoritative.

Initialization performs recovery before accepting new work. A checksummed Staging journal removes only verified staging files and refuses unexpected backups; Prepared rolls back; Committed preserves the new files and finishes backup/staging cleanup. Malformed, oversized, conflicting or out-of-root journals are never executed. All mutation targets, their existing ancestors, and the configured root/ancestor chain pass the shared Windows mutation-path validator, which rejects reparse points. The filesystem replacement helper now also accepts an explicit manager, so headless/project owners do not require a second global filesystem instance.

This layer deliberately owns no AssetIds, source-sidecar pairing, reference policy, catalog rescan, cooking, publication or undo stack. Physical files can be momentarily between per-file renames inside the exclusive commit window, and a process interruption can leave that state on disk until journal recovery; no ordinary multi-file filesystem primitive can make arbitrary source and sidecar paths atomically visible together. E1F.3 supplies the asset-aware operation plan and invokes this transaction as one unit. The VTXN 1 representation and recovery rules are recorded in `docs/formats/vtxn-format.md`.

**Two E1F implementation subphases remain: E1F.3 and E1F.4.**

Verification for E1F.2: source, filesystem primitive, containment, journal-state, rollback/roll-forward, failure-state and scoped-diff review plus `git diff --check`. No generation, compilation, tests, test authoring, editor launch or CLI execution.

### E1F.3 - move, rename, duplicate, delete and retarget execution

Source-level implementation complete; executable validation remains deferred. The shared `ProjectAssetPipeline` now owns the E1F.2 transaction when the catalog has writable roots and exposes move/rename, duplicate, retarget and delete operations; a read-only-only pipeline remains usable without mutation state. Every operation requires the existing build graph to be quiescent, commits the complete authored-file set first, and only then runs the existing source rescan, reference scan and loose-publication reconciliation. A committed transaction whose rescan fails is reported distinctly as `CommittedReconciliationPending`; it is never presented as a rolled-back edit.

Move writes the destination source and sidecar and deletes the old pair in one transaction while preserving the original `AssetId` and output identities. Duplicate requires a caller-supplied, catalog-unique `AssetId`, derives new output identities, and retargets only links explicitly classified as in-place self references. Delete refuses externally referenced outputs unless the caller supplies type-compatible replacements; all affected writable documents are rewritten once before the deleted source/sidecar pair joins the same transaction. Read-only roots, stale reference scans, target collisions, missing exact-version importers and unsupported rewrite/relocation formats fail before commit.

Opaque importer settings remain format-owned. `RelocateSourceFunction` produces complete relocated source/metadata bytes; mesh and shader implementations rewrite their embedded canonical source paths, while texture and input-mapping importers explicitly declare path-independent relocation. The mesh importer also implements the E1F.1 reference-rewrite hook for its material and skeleton settings. Generic execution verifies identity/schema preservation before staging and does not mutate VADI build dependencies or runtime streaming dependency ownership.

Pipeline initialization performs durable transaction recovery before classifying and scanning sources, so a recovered committed edit is reflected by the first catalog used for builds. A cleanup-pending committed operation still triggers reconciliation and returns `CommittedCleanupPending` as well as marking the restore report; the transaction remains recovery-required and rejects further mutations until lifecycle recovery. Frontend commands, identity generation policy, bounded inverse records and undo/redo remain E1F.4.

**One E1F implementation subphase remains: E1F.4.**

Verification for E1F.3: source, identity, importer-boundary, reference-policy, transaction/commit ordering, pipeline reconciliation, recovery ordering and scoped-diff review plus `git diff --check`. No generation, compilation, tests, test authoring, editor launch or CLI execution.

### E1F.4 - editor/headless commands, undo/redo and lifecycle closure

Source-level implementation complete; executable validation remains deferred. `ProjectAssetPipeline` is now the shared owner-thread command surface for editor and headless frontends. Move, duplicate, retarget and delete retain bounded inverse records only after their filesystem commit becomes durable. A new command clears redo history, while undo and redo capture the current affected states and replay the requested exact source/sidecar states through the same E1F.2 transaction before the existing rescan, reference scan and loose-publication reconciliation. This avoids unsafe global reverse-reference substitutions and adds no second catalog, scheduler or build dependency graph.

History is bounded by record count, per-file transaction limits and an aggregate byte budget. Each record contains only the exact files the command can mutate. Oversized or unreadable inverse state rejects the command before mutation. The history is session-local: it is cleared at shutdown and is not presented as durable cross-restart undo. Durable restart authority remains the checksummed VTXN journal, recovered before the first catalog classification and build request. A cleanup-pending or reconciliation-pending commit remains visibly committed and retains its inverse record; further replay waits for transaction recovery and a quiescent build graph.

`GenerateAssetId` obtains 128 bits from the platform cryptographic random source and applies the UUID-v4 version/variant shape. The generated-duplicate command retries bounded catalog collisions and exposes the selected identity only after commit. Callers that import externally assigned identities may still use the explicit-identity duplicate API. Resource-path collision checking remains in the existing duplicate/catalog validation.

The command surface reports busy live-build state, unavailable undo/redo, identity-generation failure, history-limit failure, pre-commit operation failure, committed cleanup pending and committed reconciliation pending distinctly. Automatic filesystem watching, background reimport scheduling, resident-resource hot reload and persistent undo history remain outside E1F.

**No E1F implementation subphases remain. E1F is source-level complete.** The next asset-foundation phase is **E2 - external change observation and reimport coordination**.

Verification for E1F.4 (executed 2026-09-15): `premake5.exe vs2022` regenerated the VS2022 workspace and the engine-service audit passed. The focused Debug x64 `assetStreaming.vcxproj` build (MSBuild `/m:1 /v:minimal`) compiled and linked (`assets.lib`, `assetStreaming.lib`). The new `assetStreamingTests.vcxproj` compiled and linked, and `assetStreamingTests.exe` executed with exit 0 and zero failed checks. The executed cases cover duplicate (canonical generated identity) -> undo -> redo for exact source and sidecar bytes, move -> undo with AssetId preservation and destination-pair removal, retarget -> undo restoring exact prior authored bytes, delete-with-replacement -> undo restoring the deleted pair and the rewritten referrer, redo clearing on a new committed command, a rejected pre-commit move leaving history counts unchanged, record-count eviction with a third undo unavailable and the original path absent, aggregate-byte destination eviction from a growing reverse snapshot, live-request rejection of mutation/undo/redo, and session-history clearing across shutdown/reinitialize. Building and running the test exposed and fixed one real E1F.2 defect: `AssetFileTransaction::Initialize` validated a root probe named `.vtxn-root-check`, which `AbsolutePath::AddFilePath` mangles by resolving the leading `.` as a current-directory component and dropping the preceding separator, so no writable root could validate and every E1F mutation initialization failed. The probe is now `vtxn-root-check`. Not exercised: fault-injected `CommittedCleanupPending`/`CommittedReconciliationPending`, physical mid-transaction VTXN crash recovery, and a forced first-attempt generated-identity collision (the platform RNG is not injectable). No editor launch or CLI execution. `git diff --check` is clean apart from the existing LF-to-CRLF notices.

### E2 - external change observation and reimport coordination

E2 is divided into six ordered implementation subphases. Their durable scope,
ownership invariants, exit gates and agent handoff procedure are recorded in
[`project-asset-e2-plan.md`](../plans/project-asset-e2-plan.md). Future agents should
implement only the first non-complete subphase and update both that plan and this
ledger when it closes.

1. **E2.1 - external change observation and bounded reconciliation:** consume
   lossy path hints, coalesce source/sidecar bursts and overflow, and drive the
   existing authoritative rescan path under a bounded owner-thread budget.
2. **E2.2 - authoritative change classification and invalidation state:** compare
   accepted asset baselines, preserve identity where proven, diagnose
   ambiguity/missing/conflicting inputs and produce canonical change/status data.
3. **E2.3 - incremental recook coordination and stale completion rejection:**
   feed changes into the existing `IncrementalRecooker`, retain one in-flight
   batch with accumulated later work, and reject completions for obsolete input
   revisions.
4. **E2.4 - committed publication, stale status and resident replacement:**
   publish only current graph-committed output through existing owners, preserve
   the last valid artifact on failure/staleness and coordinate supported runtime
   replacement through `ResourceStreamer`.
5. **E2.5 - local cache lifecycle and safe generation reclamation:** prove
   restart VDDC hits and repair, bounded VDDC cleanup, and eventual reclamation
   of superseded loose generations while protecting every committed, in-flight,
   reader, resource and resident owner.
6. **E2.6 - headless surface, lifecycle and validation closure:** expose shared
   observation/reimport/status/cancellation/maintenance commands and close the
   complete E2 workflow with focused executable validation.

**E2.1, E2.2 and E2.3 are accepted, and E2.4 P0-P6 are closed. E2.4 P5's final independent
CURRENT-tree review reported 0 blocker / 0 high / 0 medium, and the final P5/P6 CURRENT-tree E2.4
closure review also reported 0 blocker / 0 high / 0 medium. E2.4 P1 owns the
exact committed-delivery handoff/shared status, P2 owns exact fenced publication and
retry, P3 owns overlapping resource generations and resident replacement, and P4
closes removal/restart/bounds/teardown. E2.5 P0-P4 are also closed. Its first P4
closure was later reopened for a missing production renderer-to-project retirement
bridge; that bridge is now implemented in editor composition and the user-authorized
exact-CURRENT self-review returned 0 blocker / 0 high / 0 medium. E2.5 P5 is also
closed after its final independent 0 blocker / 0 high / 0 medium review. P6 was
subsequently authorized; its fresh generation, Debug/Development compile/link,
focused execution and hygiene/scope checks are complete. The final docs-inclusive
independent exact-CURRENT P6 review returned 0 blocker / 0 high / 0 medium.
**P6 and E2.5 are closed.** E2.6 implementation remains outside current
authorization; definition-only work was subsequently authorized as recorded
below.
E2.2 retains bounded
accepted source/change evidence in `ProjectAssetPipeline` without replacing
`SourceDatabase` or VADI authority. P1 defines the public/internal state and query
contracts; P2 performs deterministic comparison, tombstone/diagnostic handling
and separate typed source/build versus cooked-output invalidation evidence. Valid
callback importers without descriptor `sourceType` remain supported and retain
canonical dirty change/output evidence without a fabricated typed seed; E2.3 owns
that later importer/build-request translation. P3 gives
manual and observed reconciliation the same isolated candidate preparation and
transactional owner accept seam, including accepted authored-reference seeding
and coherent source fingerprinting. P4 adds the documented VPAS 1.0 durable
accepted baseline (`docs/formats/vpas-format.md`) under `dependencyIndexRoot` with
explicit version/checksum/bounds, stage+flush+read-back verification and atomic
replacement. Restart restores accepted generations, tombstones, outputs,
canonical references/diagnostics and backward missing-output referencers without
replaying prior changes; true I/O remains distinct from recoverable
missing/corrupt/incompatible/over-limit state. Observed project-sized persistence
runs on the persistent worker, leaving only final atomic replace and O(1) swaps on
the bounded owner tick.

P5 covers the complete durable E2.2 exit matrix, including the final independent-
review gaps for cross-root same-relative-path moves and retained-sidecar
`SourceMissing` tombstones. P6 fresh `premake5.exe vs2022` passed the Vanguard
engine-service audit; focused Debug x64 and Development x64
`assetStreamingTests.vcxproj` builds completed under `/m:1` with 0 warnings and 0
errors, and both current executables reported `0 failed checks`. `git diff
--check` exited 0 with only existing LF-to-CRLF notices, the AssetStreaming source
layout contains no headers under `src`, and the scoped leakage review found no
E2.3 recooker/BuildGraph scheduling, VADI mutation or duplicate watcher/DB/
scheduler/cache/transaction/runtime authority. A fresh independent current-
snapshot audit reported **no blocker/high/medium finding** across persistence,
restart/no-replay, bounds, bounded observation ownership, architecture and the
full matrix. P7 then updated the durable records and format documentation. Scoped
execution state lives in `.planning/e2-2-authoritative-change-classification/`.

A later Codex follow-up review found one substantive E2.2 issue despite that
earlier acceptance: support classification incorrectly required a descriptor-level
`sourceType`, even though valid callback importers can create typed build requests
through `SourceImporterDescriptor::createBuildRequest`. E2.2 now separates support
from typed invalidation-seed availability. Focused coverage proves callback initial
acceptance, content modification and VPAS restart/no-replay with no invented source
type. The same follow-up makes the directory-shaped snapshot-I/O fixture cleanup
checked and verifies the whole `vanguard_asset_streaming_tests` root is absent.
Current implementation terminology is `ProjectAssetAcceptedBaseline` / "accepted
asset baseline"; VPAS retains its historical format name, version, wire layout and
filenames. Fresh local Premake/Debug/Development/hygiene validation passed, and the
follow-up independent acceptance verified callback initial/change/restart-no-replay,
cleanup/test-root absence, naming/VPAS invariants, header layout, rename
completeness, diff hygiene and the untouched E2.3 boundary. No blocker/high/medium
finding remains in the follow-up.

### E2.4 handoff preparation

The [E2.4 cross-agent contract](../plans/project-asset-e2-4-plan.md) is defined
with P0-P6 gates, source-owner entry points, post-cook publication freshness,
last-valid retention, supported production resident replacement/retirement and
deterministic validation requirements. The user first authorized P0 on 2026-09-16
and subsequently authorized **P1 only** after P0 closed. Current-tree P0 architecture proof found three implementation
blockers that must be resolved by the existing owners in later authorized gates:
exact bounded post-commit delivery evidence does not survive E2.3 reset; the
current publisher is not exact-generation/final-freshness fenced; and the resource
registry/pipeline cannot overlap an old resident generation with a newly current
one. P0 selects VTEX through `TextureResidencyRuntime` as the narrowest production
resident proof because the lower texture residency/renderer path already provides
stable GPU identity, candidate cutover and real fence-backed retirement. Descriptor
publication must group changed non-soft Generated dependency closures atomically
when exact-content compatibility requires it, and superseded loose generations
remain protected until reader/resident retirement is proven. A final independent
current-tree read-only review reported **0 blocker / 0 high / 0 medium contradiction or overclaim** in the
P0 contract and agreed the architecture/planning gate can close with the three
recorded implementation blockers carried into later P1-P3 work. P1 has now
implemented the exact post-commit handoff and bounded typed per-output shared status:
successful validator evidence is retained only after VADI commit and adopted before
automatic reset; no-work/failure/cancel/stale/transaction-failure paths do not hand
off; delivery count/bytes are preflighted before commit; manual publication success
and failure record truthful committed/installed status while resident state remains
explicitly untracked. Deterministic coverage includes same-output coalescing,
capacity rejection before VADI visibility, restart/no-transient-delivery replay and
manual materialization failure, plus one-entry status churn proving uncooked
reconciliation probes consume no bounded status capacity and removed installed
outputs recycle otherwise-unowned status rows. Fresh Premake plus serialized `/m:1` Debug and
Development `assetRecookerTests`/`assetStreamingTests` builds and executions pass;
`assetStreamingTests` reports `0 failed checks` in both configurations. The final
independent CURRENT-tree P1 read-only closure review re-checked the production
handoff/status ownership and all deterministic edge proofs above and reported **0
blocker / 0 high / 0 medium**. P1 is closed. The user then explicitly authorized P2.
P2 now materializes the exact retained committed token, revalidates canonical build
evidence around materialization, uses an accumulator-held final generation/overflow
fence with a prebuilt no-allocation atomic streamer descriptor-group swap, and retains
failed exact tokens for explicit same-token publication retry without recompilation.
Connected non-soft Generated dependencies publish only with coherent pending records
or exact already-installed dependency content. Successful publication records
`Installed` descriptor truth while resident truth remains explicitly untracked. The
first P2 closure review found one blocker in final snapshot ordering, one high in
reverse retry-blocked Generated closure and one medium in early publication-failure
stickiness. Those are repaired: the final snapshot now begins before the last canonical
validation and is reused through `CommitIfUnchanged`; reverse connectivity is resolved
before retry gating; and preflight/dispatch or pre-output failures become explicit-retry
only without converting ordinary Busy admission into failure. Deterministic tests cover
the final Resolve watcher race and reverse retry-blocked parent/child component. Fresh
Premake plus serialized `/m:1` Debug/Development `assetRecookerTests` and
`assetStreamingTests` builds/executions are green; hygiene passes; and the final
independent CURRENT-tree re-review reported **0 blocker / 0 high / 0 medium**. P2 is
closed. The user then authorized P3. P3 adds distinct overlapping resource generations,
canonical resident-only pipeline replacement ownership and exact-generation eviction;
`ResourceStreamer` forwards the replacement path. VTEX replaces a newer CPU resource
generation on the same stable GPU residency identity through a fresh candidate path,
not cross-generation `BeginTransition`. The old BindlessReady resident survives failed
replacement; explicit retry reuses the retained uploaded candidate without another
upload, CPU resident truth advances only after manager acceptance, and old GPU state
retires through the existing graphics/compute/copy fence owner. A neutral
`ProjectAssetPipeline` resident-evidence seam reports Current only for exact published
build/content fingerprints and retains stale/retry truth otherwise. Fresh Premake plus
serialized `/m:1` Debug/Development resources/resourcePipeline/streaming/rendering/
textureResidencyService/assetStreaming validation is green, executable tests pass,
hygiene/scope checks pass, and the final independent whole-P3 CURRENT-tree review
reported **0 blocker / 0 high / 0 medium**. P3 is closed. The user subsequently
authorized P4, which now closes removal/restart/bounds/teardown and the replacement-
history/churn claim that P3 deliberately left open.
E2.3's fresh independent review passed generation, focused Debug/Development
builds/executions and hygiene. The earlier material-preview test transient remains
recorded as a caveat, not claimed fixed.

### E2.3 implementation progress

The [E2.3 cross-agent contract](../plans/project-asset-e2-3-plan.md) records the
completed E2.3 execution contract with ordered P0-P7 gates. E2.4 P0-P6 are closed; P1-P5
each completed focused validation where applicable and independent blocker/high/medium
review at **0 blocker / 0 high / 0 medium**. The user subsequently authorized E2.4 P5
through P6 end-to-end; P6 and the final E2.4 closure review are complete at **0 blocker / 0 high /
0 medium**.
P0-P4 are implemented: automatic
coordination owns bounded pending/active state, captures E2.2 evidence before
retirement, translates callback/importer requests through canonical owned
`ProjectAssetBuildSystem::Resolve`, executes first-cook and indexed recooks through
one existing `IncrementalRecooker`/VADI transaction, and rejects stale completions
before VADI commit using fresh source/settings/importer/compiler/dependency evidence
plus the watcher-generation fence. The later E2.4 P1-P4 gates add exact committed
publication and supported resident replacement on top of that closed E2.3 coordinator;
this paragraph describes the E2.3 implementation boundary, not the current E2.4 state.

P6's test-only gaps are also closed. Deterministic coverage now includes a complete
source+sidecar burst queued during another live batch and eventual next-batch
processing, repeated owner ticks proving one in-flight batch, settings/importer/
dependency stale races, dirty and missing VADI startup recovery without VPAS dirty
replay, and byte-for-byte retention of a manually published last-valid loose
resource across stale rejection. Existing tests cover first cook, untyped callback
first/edit, unchanged suppression, transitive/root-minimized recook, move/delete/
restore diagnostics, low-level failure/cancel rollback, direct-disk stale rejection,
watcher-race rejection, manual contention and committed-reader VADI isolation.

Fresh P7 validation on 2026-09-15 ran `premake5.exe vs2022` successfully with the
Vanguard engine-service audit, then serialized x64 `/m:1` Debug and Development
builds for `assetGraphTests`, `assetRecookerTests`, `assetStreamingTests` and
`materialToolsTests`. All eight executable runs passed; `assetStreamingTests`
reported `0 failed checks` in both configurations and `materialToolsTests` reported
its Phase 2 checks passed. `git diff --check` exited 0 with only the shared tree's
existing LF-to-CRLF notices; AssetStreaming header placement is clean, both owned
test roots are absent, and no focused test/MSBuild process remains.

The first independent P6/P7 closure review found zero blocker/high findings and
four medium test/documentation gaps. Those were repaired without P5/E2.4 production
changes, and the affected `assetStreamingTests` reruns again passed in Debug and
Development. A final independent read-only review of the repaired tree reported
`0 blocker / 0 high / 0 medium`; P7 is complete.

P5 was subsequently authorized explicitly and is now implemented. The coordinator
uses authoritative recovery after accepted capture overflow, sticky terminal
failure/recovery with explicit/newer-accepted retry triggers, bounded retained retry
entries, retained invalidation evidence across failed/cancelled/stale recovery, and
no owner-tick dispatch retry loop. Deterministic pipeline coverage now adds failed
root, active cancellation, request-resolution diagnostics, VADI commit-I/O rollback,
overflow authority/irrecoverable bounds, both contention directions, source-mutation
exclusion, and live-work shutdown/reinitialize while preserving last-valid manual
publication and never performing E2.4 automatic publication/replacement.

Fresh post-P5 P7 validation again passes Premake/engine audit, all four focused
targets in x64 Debug and Development under outer `/m:1`, and all eight executable
runs. AssetStreaming reports zero failed checks in both configurations; hygiene,
header placement, owned test-root cleanup and focused-process cleanup are green.
The latest stricter P5 closure review then found four medium proof/documentation
gaps rather than a new production defect: distinct old-identity retention after
failed/cancelled work, direct evidence of exactly one authoritative recovery plan,
same-pipeline clearing of `recoveryRequired` after successful recovery, and the
stale status paragraph above. The current regression now uses a failed batch plus
successive source moves under a one-input bound to make all three runtime proofs
observable, and the stale paragraph is reconciled. Focused Debug execution is green;
focused Development is green as well, and fresh exact-tree Premake plus the complete
Debug/Development four-target matrix and hygiene now pass. The final independent
re-review reported `0 blocker / 0 high / 0 medium`. E2.3 is **complete and accepted**.
E2.4 P0 is complete; P1 is complete, independently reviewed and closed with **0
blocker / 0 high / 0 medium**. P2 is also complete, independently re-reviewed and
closed at **0 blocker / 0 high / 0 medium** after exact-tree validation. P3 is likewise
complete and independently reviewed at **0 blocker / 0 high / 0 medium** after focused
Debug/Development production validation. P4 is complete and closed. Earlier exact-tree validation and independent reviews were
green, but the required docs-inclusive review subsequently found one HIGH (ordinary
manual/observed removal still used single-output unregister) and one MEDIUM (the stale
phase-status paragraph repaired above). Ordinary reconciliation now collects the complete
undesired set and uses one all-or-none `UnregisterLooseBatch`; a busy-second-output
regression proves failure cannot expose a removed prefix. Final Development repetition
also exposed a retained-operation reuse race, now fixed by keeping each `PipelineOperation`
unreclaimable until its scheduled plan/construct jobs return. Post-fix Premake generation,
serialized Debug/Development compile/link and the full focused executable matrix are green;
final hygiene is clean and the fresh independent re-review reported **0 blocker / 0 high /
0 medium**. P4 preserves package
fallback and pinned old readers, conservatively retains physical superseded loose files,
reconstructs restart truth from accepted/current source + VADI without transient replay,
bounds resource-operation/registry-generation/status/resident retention, settles automatic
publication before teardown, refuses streamer shutdown with live sources, and prevents
texture demand/uploader request ABA across reinitialize. Broad cache pruning remains E2.5.
E2.4 P5 is complete and independently closed at **0 blocker / 0 high / 0 medium**. P5 adds
deterministic exact no-churn, post-commit source/settings/importer/dependency freshness rejection,
missing/corrupt committed VDDC, transitive A->B->C atomic visibility/retry, capacity-before-
visibility, and old-open streaming source/range replacement proof without production-source
changes. P6 then passed fresh Premake VS2022 generation and the engine-service audit; serialized
`/m:1` x64 Debug and Development builds for `assetRecookerTests`, `resourcesTests`,
`resourcePipelineTests`, `streamingTests`, `rendering`, `geometryAllocatorTests`,
`textureResidencyServiceTests`, and `assetStreamingTests`; and execution of all seven runnable test
targets in both configurations. `assetStreamingTests` reports zero failed checks, the texture
residency service's source/DDC/loose/package/GPU integration passes, and geometry/runtime executes
the D3D12 resident replacement/material/fence path. Two stale test-only assumptions were corrected
during this validation (direct mesh-draw test includes and the current invalid explicit-fence-wait
failure code); no production defect was exposed. Final diff/audit/header/residue/process/
dependency/scope hygiene is clean. The final independent CURRENT-tree P5/P6 closure audit reported
**0 blocker / 0 high / 0 medium** after rechecking the deterministic matrix, separate generation/
build-link/execution/GPU-fence evidence, both rendering test-only fixes, durable-record consistency
and the E2.5/E2.6 scope boundary. P6 and E2.4 are closed. This sentence records
the historical state at E2.4 closure; E2.5 was subsequently authorized and its
current progress is recorded below. E2.6 was still unauthorized at that
historical checkpoint.
### E2.5 handoff preparation

The [E2.5 cross-agent contract](../plans/project-asset-e2-5-plan.md) is defined
with P0-P6 gates. It separates immutable VDDC maintenance from materialized
loose `.vres` generation reclamation. VDDC lifecycle authority remains in a
root-scoped `assets` owner shared by every artifact-source instance, VADI
authority remains in `DependencyIndex`, and `ProjectAssetPipeline` coordinates
existing E2.3 repair admission plus E2.4 delivery status without becoming a
second cache or scheduler. Superseded loose files remain under publisher/
streamer/resource/resident lifetime proof and are reclaimed only after every
owner retires them.

The definition originally recorded these gaps before P1 implementation:
per-result cache origin is not yet distinct, VADI lacks a general bounded
committed-record enumeration query, BuildSystem and ProjectLoosePublisher own
separate artifact-source instances against one cache root, corrupt lookup and
broad `*.tmp` cleanup bypass a shared deletion authority, repair needs explicit
entry through the existing E2.3 scheduler, and E2.4 has no owner-proven loose-
generation retirement query. No E2.5 production source, build, test or
maintenance operation was started during definition. The user later explicitly
authorized P0; `.planning/e2-5-local-cache-lifecycle/` contains the current
architecture proof and review history. P0 has frozen root-shared VDDC reader/
writer/publication/delete coordination with a stale-snapshot protection epoch,
exact three-way build origin, bounded/status fail-safe rules, existing-E2.3 repair
ownership, managed loose generation origin + exclusive retire/delete admission,
and fence-complete neutral resident retirement handoff without
`assetStreaming -> rendering`. The latest independent finding additionally
requires exact persistent VDDC validity before any durable BuildSystem result can
flow into graph/VADI: compiler persistence failure must fail that durable result,
and a memory hit must restore a missing/corrupt exact VDDC from retained output
without recompiling or fail. Final fresh independent CURRENT-tree re-review after
that correction returned **0 BLOCKER / 0 HIGH / 0 MEDIUM**. P0 is closed.

The user subsequently explicitly authorized P1. P1 implemented the shared
assets-owned VDDC root authority and exact build-origin/publication foundations:
reader-specific persistent-hit protection transfer, exact durable-VDDC proof for
all durable origins, one durable VADI protection domain per root, exact durable
build/content/artifact token binding, bounded committed/staged VADI + BuildGraph +
delivery protection snapshots, owned VDDC stage recovery and authority-routed
corrupt cleanup. Focused Debug x64 and Development x64 builds and runtime tests
passed for `assetsPersistentCacheTests`, `assetIndexTests`, `assetGraphTests` and
`assetStreamingTests`; assetStreaming reported `0 failed checks` in both
configurations. The engine-service audit and scoped `git diff --check` passed.
Two fresh independent CURRENT-tree P1 closure audits each returned
**0 BLOCKER / 0 HIGH / 0 MEDIUM**. P1 is closed.

The user subsequently explicitly authorized E2.5 P2 end to end and later directed
`close P2`. P2 now validates the complete exact VADI-referenced VDDC record before
restart/restoration reuse, committed-delivery reuse and already-installed manual/
observation reconciliation. Missing/corrupt/truncated/descriptor-mismatched current
records enter bounded `CacheRepairRequired` state and resolve only through the
existing canonical `ProjectAssetBuildSystem` request, E2.3 automatic pending state,
`IncrementalRecooker`, `BuildGraph` and VADI transaction. Stale repair markers are
pruned after accepted output removal/rename; forced repair outputs are mandatory
graph roots even when old VADI topology contains a dirty dependant whose current
request dropped the old edge; and authoritative recovery revalidates current
VADI/VDDC truth so repair identity can be reconstructed even after the original
repair preparation could not retain it. Disabled automatic-recook state remains
disabled on repair-preparation failure, failed repair stays sticky/no-spin, and
the committed VADI plus E2.4 last-valid published/resident generation survive
repair failure until exact repair succeeds.

P2 closure reviews were corrective rather than ceremonial. The first fresh review
reported **0 BLOCKER / 1 HIGH / 1 MEDIUM** and drove synchronous whole-record
validation plus stale repair-marker pruning. Later stricter independent audits
reported **0 BLOCKER / 2 HIGH / 1 MEDIUM** around forced-root minimization,
authoritative recovery reconstruction and reconciliation/disabled lifecycle; all
were fixed before closure. Deterministic tests cover restart missing/corrupt repair,
last-valid retention, sticky compiler failure + explicit retry, stale marker
pruning, lost repair identity while disabled/manual reconciliation, and stale old
D->F topology with current D dropping F while forced F remains a repair root.
Final serialized `/m:1` Debug x64 and Development x64 builds passed for
`assetsPersistentCacheTests`, `assetIndexTests`, `assetGraphTests` and
`assetStreamingTests`; every executable exited 0 and assetStreaming reported
`0 failed checks` in both configurations. The engine-service audit passed; scoped
diff/whitespace hygiene found no whitespace errors beyond existing LF-to-CRLF
working-copy notices. Final fresh independent CURRENT-tree P2 re-review reported
**0 BLOCKER / 0 HIGH / 0 MEDIUM**. P2 is closed. P3 was subsequently completed
before P4 authorization; current P4 evidence follows. E2.6 was still
unauthorized at that historical checkpoint.

The user subsequently authorized E2.5 P4 and directed `close p4`. P4 now uses a
streaming-owned bounded exact managed-loose generation authority for descriptor,
source/load and decoded-object origin lifetime, with exclusive tombstoned
`RetireLooseGeneration` deletion admission. ProjectAssetPipeline reserves
superseded canonical loose candidates before both manual and automatic visibility,
keeps committed reservations through settlement, performs bounded restart
rediscovery, preserves current/stage/foreign files, and exposes separate loose
maintenance bounds/status/retry.

Rendering now retains exact neutral source-generation identity through real
G/C/C fence and native RHI retirement and exposes non-destructive peek plus
explicit acknowledgement. Renderer quiesce finalizes the last native retirements
while texture residency remains alive so dependent composition can consume the
completed evidence during drain; renderer drain fails closed if evidence is still
undelivered. The project-side retirement report distinguishes `Accepted`,
`NotTracked`, `Busy`, `InvalidArgument` and `InvalidState`.

A later audit correctly reopened the first P4 closure because those two endpoints
still had no production caller. That HIGH is now corrected in editor composition:
`ProjectWorkspaceService` owns the single production texture-domain
`ProjectAssetPipeline`, uses the real ResourceStreaming streamer and texture
compiler, and boundedly pumps exact renderer `{resourcePath, resourceGeneration,
retirementId}` evidence. `Busy` leaves the renderer record untouched for retry;
only `Accepted` and `NotTracked` acknowledge the exact renderer id. The ordinary
editor running tick invokes the same production pump after `RunFrame`. No
assetStreaming/rendering dependency was added.

Fresh Premake generation passed. Serialized MSVC 14.44.35207 `/m:1` Debug and
Development builds passed for `streamingTests`, `assetStreamingTests`,
`rendering`, `engine`, `textureResidencyServiceTests`,
`assetsPersistentCacheTests`, `assetIndexTests` and `assetGraphTests`; every
runnable executable in that set exited 0 in both configurations. The broader
`geometryAllocatorTests` target builds in both configurations and its P4
texture/uploader/residency path passes, but the executable later stops on an
unrelated concurrent material/static-surface aggregate GPU-scene stats oracle:
current totals are `20/15/9/9/0` while the stale unchanged assertion expects
`19/14/8/8/0`. That broad executable is therefore not claimed green. The
production-seam `editorAssetRetirementTests` exercises the real EngineHost, project
workspace, ResourceStreaming, Rendering, D3D12/RHI and production pipeline: Busy
preserves the same retirement id, retry delivers the exact retirement and
acknowledges once, terminal NotTracked also acknowledges once, and graceful
shutdown succeeds. Debug and Development both pass. The engine audit and scoped
whitespace/dependency/delete-authority checks are clean for P4. With the external
reviewer unavailable, the user explicitly authorized a final exact-CURRENT
self-review; it returned **0 BLOCKER / 0 HIGH / 0 MEDIUM** and kept the later broad
GPU-scene aggregate-oracle drift outside P4. At that historical P4 checkpoint, P5
was the next gate; the current P5 closure state is recorded immediately below.

The user subsequently authorized E2.5 P5 by directing the next gate to start. Its
deterministic matrix is implemented and focused validation is green. The final
independent exact-CURRENT closure review returned **0 BLOCKER / 0 HIGH /
0 MEDIUM**, so P5 is closed. The matrix adds wrong-key/wrong-content current
VDDC repair; repair-specific cancellation/staleness; exact importer/compiler key
changes; root-shared reader/writer/corrupt-lookup maintenance refusal and bounded
retry; equal-age and checked-byte VDDC maintenance; reciprocal VDDC/loose
over-limit status; malformed/wrong-shard/outside-root preservation; active VDDC
and loose maintenance lifecycle; project relocation/owned-stage restart recovery;
and an active streamer load pin across loose generation replacement. The one P5
production correction reclaims only exact canonical owned `.vres.tmp` stages and
still delegates physical mutation to `ResourceStreamer::RetireLooseGeneration`.
Arbitrary `.tmp`, malformed/misplaced files and outside-root paths remain
untouched. Fresh serialized Debug and Development builds/runs pass for
`assetsPersistentCacheTests`, `assetStreamingTests` and `streamingTests`; engine
audit and scoped hygiene checks pass. Runtime reparse/junction construction and
literal `u64` filesystem rollover remain portability-sensitive source-reviewed
guards rather than fabricated fixtures. At that P5 checkpoint P6 had not started;
it was subsequently authorized and the current P6 evidence follows.

Fresh E2.5 P6 `premake5.exe vs2022` exited 0 and passed the engine-service
audit. Serialized `/m:1` Debug and Development compile/link passed for
`assetsPersistentCacheTests`, `assetIndexTests`, `assetGraphTests`,
`assetStreamingTests`, `streamingTests`, `textureResidencyServiceTests`,
`editorAssetRetirementTests`, `geometryAllocatorTests`, `rendering` and
`engine`. Seven focused executables pass in both configurations:
`assetsPersistentCacheTests`, `assetIndexTests`, `assetGraphTests`,
`assetStreamingTests`, `streamingTests`, `textureResidencyServiceTests` and
`editorAssetRetirementTests`. `geometryAllocatorTests` builds and executes in
both but reproduces the same unrelated GPU-scene aggregate oracle,
`20/15/9/9/0` current versus `19/14/8/8/0` hard-coded, so that broad
executable is not claimed green. Fresh engine/header-layout, tracked/no-index
whitespace, generated-output, scratch-residue, dependency-direction,
delete-authority, E2.6-leakage and process-residue checks are clean. The only
retained files are helper-local: five canonical VDDCs per `asset_index_tests`
configuration and three geometry fixtures per failed geometry configuration; no
repository/source cache/output residue remains. No E2.5 implementation fix was
needed in P6. A fresh final docs-inclusive independent exact-CURRENT review
returned **0 BLOCKER / 0 HIGH / 0 MEDIUM**, accepting the build/runtime
distinctions, known unrelated geometry failure, helper-local scratch residue and
the current ownership/dependency/delete-authority seams. **P6 and E2.5 are
closed.** The user subsequently authorized **E2.6 definition work only**.
The dedicated definition now preserves ProjectAssetPipeline as the sole shared
backend, keeps editor ownership in ProjectWorkspaceService, and defines a
private command-scoped GPU-free nanovanguard composition using the existing
ResourceRegistry/ResourcePipeline/ResourceStreamer rather than linking the
monolithic engine/rendering/RHI surface. It also records the required shared
root/target/settings identity parity, the missing per-output VADI freshness
query and explicit reimport seam, current-session cancellation/no-daemon
semantics, stable human/JSONL protocol requirements, and ordered P0-P6 gates.
A first independent exact-CURRENT definition review returned 0/0/0; a stricter
second audit returned **0 BLOCKER / 0 HIGH / 1 MEDIUM** because the planned
per-output VADI freshness query did not explicitly reproduce authoritative
IncrementalRecooker Generated-dependency content materialization before
DependencyIndex::Evaluate. The contract now requires one shared preparation
path and query/recooker parity regression. Final strict exact-CURRENT re-review
returned **0 BLOCKER / 0 HIGH / 0 MEDIUM**, so the **E2.6 definition is
closed**. No production source, Premake, build or executable test work was
performed during definition. The user then authorized **P0 only**. Fresh
exact-CURRENT P0 review locked the neutral target/config split, GPU-free direct
headless lifecycle/link closure, one VADI-owned Generated-dependency preparation
seam, coherent committed-VADI freshness semantics, recooker-owned explicit
reimport, stable CLI/protocol/cancellation/maintenance behavior, exact later-gate
targets/tests and proof boundaries. Fresh RED backend source was not reachable
under `/engine` and `/gpt` was empty, so E2.5 RED references remain inherited
comparison evidence only rather than a claimed fresh read. The first independent
P0 preflight returned **0 BLOCKER / 3 HIGH / 2 MEDIUM**; all findings were
corrected in the planning contract. Final strict independent exact-CURRENT P0
review returned **0 BLOCKER / 0 HIGH / 0 MEDIUM** and verified no P1 leakage.
No production source, Premake, generation, build or executable test work was
performed for P0. The user then separately authorized **P1 only**. P1 added the
pure shared configuration helper, migrated editor derivation without changing
its P4-owned partial-init behavior, and added the private command-scoped
GPU-free nanovanguard ProjectAssetContext plus direct lower-layer Premake/runtime
closure and dedicated integration target. Fresh VS2022 generation passed;
focused Debug and Development compile/link passed; assetStreamingTests,
nanovanguardProjectAssetTests and editorAssetRetirementTests executed
successfully in both configurations. Generated/source audits found no
engine/rendering/rhi/pipelineCache/window/input/world/gameWorld/editor dependency
in the headless P1 path. Final independent exact-CURRENT P1 review returned
**0 BLOCKER / 0 HIGH / 0 MEDIUM**. **E2.6 P0 and P1 are closed. P2-P6 remain
unstarted and separately unauthorized.**

The user subsequently authorized **E2.6 P2 only** with `start P2` and later
directed `close P2`. P2 now owns one shared
`DependencyIndex::MaterializeGeneratedDependencyContent` seam used by
IncrementalRecooker, strict pipeline validation and the new public freshness
query with the required tolerant-versus-strict missing-Generated policies. The
pipeline exposes exact per-output `DirtyReason` without treating an already
committed unsaved VADI as Busy, and complete-current-asset explicit reimport uses
the existing automatic coordinator plus its single `IncrementalRecooker` with
bounded `CacheRepair`/`ExplicitReimport` force reasons. Policy-off explicit work
does not sweep unrelated project recovery or dormant repair state; policy-on work
merges into the same coordinator; exact VDDC hits remain valid.

The P2 exact-CURRENT implementation preflight returned **0 BLOCKER / 2 HIGH / 1
MEDIUM**; all three findings were corrected before final validation. During
closure, a later read-only audit raised one HIGH around disabling policy near
publication. An initial broad guard on any publishable retained delivery was
proven wrong by 15 existing `assetStreamingTests` failures and removed. Final
semantics preserve E2.4's phased lifecycle: retained committed delivery may
survive policy disable for later re-enable/shutdown handling, while explicit
activity and `publicationSettlementPending` prevent abandoning actual in-flight
settlement. Existing Busy gates for owned automatic admission, observation
reconciliation, VDDC maintenance and loose-retirement admission remain in force.
The broad same-tick `TickAutomaticRecook()` publication change was
not adopted.

Fresh `premake5.exe vs2022` passed the engine-service audit. Serialized `/m:1`
x64 Debug and Development compile/link passed for `assetIndexTests`,
`assetRecookerTests`, `assetStreaming`, `assetStreamingTests`,
`nanovanguardProjectAssetTests`, `nanovanguard`,
`editorAssetRetirementTests` and `editor`. The applicable test executables
`assetIndexTests`, `assetRecookerTests`, `assetStreamingTests`,
`nanovanguardProjectAssetTests` and `editorAssetRetirementTests` were actually
executed in both configurations and all exited 0. Final source/dependency/scope
and whitespace audits were clean apart from the existing tracked LF-to-CRLF
notices. The independent exact-CURRENT P2 closure review returned **0 BLOCKER /
0 HIGH / 0 MEDIUM** and explicitly accepted the final retained-delivery/
publication-settlement distinction. **E2.6 P2 is closed. P3-P6 remain unstarted
and separately unauthorized.** A post-reconciliation docs-only review found one
MEDIUM ambiguity in parent-plan Busy-gate wording; the wording was corrected and
the final docs-only exact-CURRENT recheck returned **0 BLOCKER / 0 HIGH / 0
MEDIUM**.

### E2.6 P3 - nanovanguard commands and protocol closure

**Complete and independently accepted.** The user separately authorized P3 with
`start Next P` and later directed `close it`; P4-P6 were not authorized. P3
repairs the normal `sources` parent and adds only `asset status`,
`asset rescan`, `asset build`, `asset reimport` and `asset watch`. The
frontend uses the existing GPU-free
`ProjectAssetContext`/`ProjectAssetPipeline` ownership, accepted-state
source/output resolution, one semantic human/JSONL event model and the
process-local Windows cancellation flag. No
maintenance/move/duplicate/retarget/delete command, cross-process cancel/status
mechanism, second recooker/queue/BuildGraph/session backend or
`assetStreaming -> rendering` dependency was added.

The final watch correction handles genuine automatic Failed/RecoveryRequired by
closing through `ProjectAssetContext`, so pipeline shutdown drains retained
lower automatic admission including poll-dispatch failure. Command-issued lower
CancelledBatch remains distinguishable and maps to cancelled/30; historical
`activeRoots` is not treated as ownership. Deterministic owner-visible
cancellation tests cover pending and active work. The Windows console handler
was source-reviewed and only publishes the interlocked process-local flag; **no
physical Ctrl+C/Ctrl+Break execution is claimed**.

Exact-current closure validation: fresh `premake5.exe vs2022` passed the
engine-service audit; serialized `/m:1` Debug builds of
`nanovanguardTests`, `nanovanguardProjectAssetTests` and `nanovanguard`
passed, followed by **20/20** actual Debug invocations (`nanovanguardTests`
plus 19 fresh-process project modes). The same three Development builds passed
and the same **20/20** invocations were actually executed successfully. Rebuilt
Debug production smoke proved exactly the five P3 asset children, parser JSONL
usage-error exit 2 with operation `asset-status`, successful scratch
`sources inspect`/status/rescan/build/reimport, and corrupted-source watch
failed/20. Dependency/scope/hygiene was clean: direct GPU-free closure only, no
forbidden aggregate/rendering/RHI links, no frontend DependencyIndex/VMETA
parser/IncrementalRecooker/BuildGraph ownership, no P4 command descriptors,
clean tracked diff check apart from expected LF-to-CRLF notices and clean
untracked whitespace/conflict/final-newline checks.

Review chronology closed every late finding: implementation audit **0 BLOCKER /
4 HIGH / 2 MEDIUM**; watch re-review **0 BLOCKER / 2 HIGH / 0 MEDIUM**; then a
fresh reviewer found one additional HIGH around Failed-with-retained-admission
poll-dispatch teardown. The final direct-close/shutdown-boundary repair was
re-reviewed independently at **0 BLOCKER / 0 HIGH / 0 MEDIUM**. **E2.6 P3 is
closed. P4-P6 remain unstarted and separately unauthorized.**

### E2.6 P4 - maintenance, explicit mutation, lifecycle and restart

**Closed at final independent exact-CURRENT 0 BLOCKER / 0 HIGH / 0 MEDIUM under
the user's updated intermediate-gate validation policy.** The user explicitly
authorized P4 with `start P4`. The headless frontend now exposes maintenance plus move, generated-ID
duplicate, retarget and explicit-policy delete through the existing
ProjectAssetPipeline owners. It adds no second backend, persistent CLI undo/redo,
cross-process cancellation service, frontend victim/deletion authority or
`assetStreaming -> rendering` dependency. Handler semantic usage failures use a
normal failed terminal with exit 2; parser `usage-error` remains parser-only.

The command/restart matrix is scratch-backed and fresh-process where lifecycle
requires it. A two-process move/restart pair proves the persisted moved AssetId is
the current SourceDatabase/accepted source after reopening while undo/redo history
is empty and both maintenance domains begin Idle with no retry state. The editor
partial-init correction preserves TextureAssetCompiler/rendering callback owners
whenever a failed ProjectAssetPipeline initialization left the pipeline live, and
the existing pipeline-first shutdown remains authoritative.

Focused Debug and Development builds passed for `nanovanguardTests`,
`nanovanguardProjectAssetTests`, `assetStreamingTests`, `nanovanguard`,
`editorAssetRetirementTests` and `editor`. The exact-current project-asset suite
passed 36/36 fresh-process modes in each configuration after the final restart
test correction; applicable lower/editor tests also executed green. Rebuilt Debug
production smoke passed real scratch status/maintenance/move/restart/duplicate /
retarget/delete flows. Dependency/scope/hygiene is clean, and final independent
nanovanguard/restart plus editor lifecycle reviews both returned **0 BLOCKER / 0
HIGH / 0 MEDIUM**. Physical Ctrl+C/Ctrl+Break delivery remains unexecuted; the
established deterministic process-local injection/source-review boundary is
unchanged.

Fresh `premake5.exe vs2022` is presently blocked by an unrelated dirty
`source/engine/src/world_session_service.cpp` source-lineage-comment audit
violation. That concurrent file is outside P4 and was not changed here. After
these runs the user changed the remaining validation policy: intermediate Ps close
on coherent implementation plus independent exact-CURRENT review, while broad
Premake/build/test/native acceptance is batched into P6. The failed fresh Premake
attempt is therefore deferred P6 context rather than a P4 closure blocker.
**P4 is closed.** The user subsequently authorized P5 only; its current closure
state is recorded below.

E2.6 P5 is now source-complete and closed under the user's updated intermediate
validation policy. The deterministic additions remain test-only: the headless
integration harness forwards the real production `TextureAssetCompiler` descriptor
while counting/barriering compile callbacks, and adds cold external add -> rescan ->
VADI/loose publication, fresh-process persistent-hit with zero compiler calls,
source/settings/generated Tool-dependency drift, active-build Busy/cancellation /
shutdown/reinitialize, shared SemanticEvent human/JSONL parity and scratch
containment proof. Existing exact owner suites provide the complementary explicit
reimport, stale/failure/cancel rollback, dependent-delete/replacement, watcher,
VDDC/loose lifetime protection and maintenance-cancellation rows; move/duplicate /
restart and shared editor/headless config coverage remain authoritative.

The first independent P5 preflight found 0 blocker / 2 high / 2 medium proof gaps;
the implementation closed them. A later exact-current review found one medium in
test-harness shutdown lifetime; the harness was changed to heap ownership with
production-compatible fail-stop retention, and the fresh post-fix independent
review returned **0 BLOCKER / 0 HIGH / 0 MEDIUM**. Source-only checks found no
remaining ambiguous I/O qualification, stack-owned P5 harness or trailing
whitespace. One Debug `nanovanguardProjectAssetTests` build attempt was started
during P5 despite the batching policy and failed on the new test source's hidden
`workspace` name plus ambiguous `io`; both were corrected, but no build was rerun
and **no P5 compile/link/runtime pass is claimed**. Full fresh generation,
Debug/Development execution, production scratch matrix and hygiene remained P6
at that checkpoint. The user subsequently authorized P6, whose exact-current
closure evidence follows.

### E2.6 P6 - final validation and complete E2 closure

Fresh `premake5.exe vs2022` passed with the engine-service audit. Serialized
`/m:1` x64 Debug and Development builds passed the 13-target final dependency
union, the nine applicable owner/test executables passed in each configuration,
and `nanovanguardProjectAssetTests` passed the complete **42/42** fresh-process
matrix in each configuration with the required restart ordering.

The first Debug cold-matrix run exposed a test-only rewrite defect: the fixture
used nanovanguard's create-only durable writer while attempting to replace an
existing source and VMETA. The helpers were changed to the existing filesystem
overwrite/truncate writer; the focused mode then passed and both complete 42/42
matrices are green. No production freshness logic or architecture changed.

Fresh rebuilt production `nanovanguard` scratch execution in Debug and
Development covered help, JSONL usage/exit 2, sources inspect,
status/rescan/build/reimport, failed-watch termination, maintain-all,
move plus fresh-process status, duplicate, retarget and delete reject/replace.
The corrupted-source watch path produced one failed terminal with exit 20, and
approved scratch project roots were absent after cleanup. Engine/header-layout,
diff/whitespace, generated/scratch, process, dependency/link, authority/scope and
stable protocol/exit-code audits are clean. The final independent exact-CURRENT
P6 closure review returned **0 BLOCKER / 0 HIGH / 0 MEDIUM**.

Physical Ctrl+C/Ctrl+Break remains intentionally unexecuted and unclaimed; the
deterministic owner-visible cancellation/source-review boundary is unchanged.
**E2.6 P6 is closed. E2.6 is closed. Complete E2 is closed.**

Development documents are
grouped by purpose; use the [document map](../README.md) for current paths.

### E2.1 - external change observation and bounded reconciliation

**Complete and accepted after repair.** The durable contract is `docs/development/plans/project-asset-e2-plan.md`. A later Codex review found the initial acceptance premature: `EnableObservation` could start an observation session and a generation-zero authoritative source rescan while live `BuildGraph` work owned the graph. The enablement common path now rejects a busy graph with `ProjectAssetPipelineResult::Busy` before any side effect, and a deterministic regression covers it.

`filesystem/directory_observation.hpp` adds the observation seam: `DirectoryObservationKind`, a lossy `DirectoryObservation` (path, watched-root index, kind), a `DirectoryObservationCallback`, `DirectoryObserverConfig` and the `DirectoryObservationSource` interface. It is header-only and adds no runtime dependency to the `filesystem` module. `asset_streaming/platform_directory_observer.hpp/.cpp` implements the seam on Windows with one overlapped `ReadDirectoryChangesW` worker per root; dropped-change zero-byte completions and reported I/O failures become explicit `Overflow`, and `Stop()` signals, cancels, joins and deletes every worker before returning. Non-Windows platforms report the backend unavailable.

`asset_streaming/source_change_accumulator.hpp/.cpp` is the callback-to-owner accumulator. `Publish` is mutex-protected and records only coalesced path/overflow hints; it never mutates `SourceDatabase`, publications or the build graph. Case-insensitive coalescing, tick-based settle/forced deferral, generation-scoped acknowledgement and bounded per-tick inspection prevent both partial-write starvation and loss of hints arriving during an older reconciliation. Overflow/allocation failure requests authoritative recovery while represented pending slots are reclaimed incrementally rather than bulk-destroyed on an owner tick. E1F-owned `.vtxn-stage`, `.vtxn-backup`, configured journal and journal `.stage` paths are suppressed in callbacks and excluded from authoritative scans.

`ProjectAssetPipeline` gains `EnableObservation` (owned platform backend or borrowed injected source), `DisableObservation`, `IsObservingSources`, `TickObservation` and `GetObservationStatus`. A persistent observation worker performs project-sized authoritative scanning/classification/reference work, loose materialization/publication removal/pruning and previous-catalog reclamation. `TickObservation` performs bounded polling and O(1) handoff/bookkeeping using preallocated arrays/hash indexes and a configurable reconciliation-operation budget. Complete catalog/reconcile payloads are exchanged with constant-time swaps; old state is destroyed on the worker. Every observation start launches a generation-zero authoritative catch-up. `DisableObservation` refuses already dirty/active state and, after `Stop()` closes the callback lifetime, catches any callback that raced with stop before reporting success. Full shutdown stops producers and drains/joins the worker before owner teardown. Manual `Rescan` remains synchronous.

`assetStreaming/tests/project_asset_pipeline_tests.cpp` adds deterministic injected-observer scenarios for external create, modify and delete; rename bursts; source/sidecar arrival in both orders; duplicate/case-only hints; partial-write deferral and repair; late overflow; root-index mismatch; physical transaction-artifact full-rescan exclusion; bounded multi-tick promotion and `maximumReconcileOperationsPerTick=1` progress; active/pending stop refusal; a callback delivered from inside `Stop()`; queued-change recovery across shutdown/reinitialize/re-enable; active-reconcile shutdown; and unchanged manual `Rescan`. Supplemental Windows coverage exercises real parsed `ReadDirectoryChangesW` path delivery without Overflow fallback, clean stop/restart, case-only rename and missing-root retry/recovery.

Verification for E2.1 (executed 2026-09-15): `premake5.exe vs2022` passed the Vanguard engine-service audit and regenerated the VS2022 workspace. Focused Debug x64 and Development x64 `assetStreamingTests.vcxproj` builds succeeded using serialized outer MSBuild `/m:1`. The Debug executable passed six runs (one initial plus five consecutive) with `0 failed checks`, including the native Windows watcher coverage; the Development executable also passed with `0 failed checks`. `git diff --check` returned exit 0 with only existing LF-to-CRLF working-copy notices, and the test left no filesystem residue. The initial claim that no blocker, high or medium issue existed was premature: a later Codex review found that `EnableObservation` did not require `CanMutateSources()` before starting a session. The repair, its deterministic regression and the focused Debug/Development reruns are recorded in `project-asset-e2-plan.md`; a fresh source-level observation-lifecycle review found no further blocker, high or medium issue. Deliberately outside this closure: E2.2 change classification, automatic cooking, stale-completion rejection, runtime hot reload and cache maintenance.

### E1B source-level implementation

`assets/asset_metadata.hpp` now defines AssetId, importer-owned versioned settings and stable output keys, with bounded VMETA 1 text codecs and output-to-runtime identity mapping. The runtime ResourceId/ResourceReference contracts are unchanged. Within-document duplicate keys/hash collisions are rejected. See `docs/formats/vmeta-format.md` for format and migration/identity policy.

No file mutation, automatic identity assignment, catalog-wide collision enforcement or importer migration execution is claimed: those require E1C/E1E/E1F ownership. Labels and cooking overrides are not yet fields in VMETA 1 and require a later explicit schema extension. No builds or tests were run or authored; codec validation remains deferred.

- E1C: headless source inventory, explicit source roots, status and indexed queries.
- E1D: authored references and referencers, separate from VADI build dependencies and runtime load dependencies.
- E1E: project-backed adapters for existing compilers, BuildGraph and DDC; explicit first import.
- E1F: shared explicit file operations, recovery, rescan, relocation and headless closure.

E2 retains automatic watching/reimport scheduling, full incremental-change integration and cache lifecycle. No editor-only asset backend or second dependency scheduler should be introduced.
