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

Source-level implementation complete; no executable validation. `SourceDatabase::ClassifySources` consumes existing CompilerDescriptors, verifies descriptor validity/unique IDs, and stores only classification/identity/type values. It does not retain descriptor or tool pointers or add another compiler registry. Registration and tool lifetime must remain stable during classification. Successful rescans reset classification, which the owner must rerun.

The source reference is `backendData/generator.cpp`: EnumerateGenerators and IGenerator::Create use generator-owned CanGenerate. Vanguard retains its explicit compiler descriptors rather than introducing RTTI discovery. A new optional recognizeSource callback is a filename routing hint only. Metadata-selected compiler IDs and versions are resolved directly; no unavailable compiler is silently substituted. Ambiguous matches, missing hooks, invalid metadata and version mismatches are distinct states. SourceMismatch means the filename hint disagrees, not proof that decoding is impossible. Source issues remain independent from classification; a missing or conflicting source is never made buildable by a candidate hint.

Mesh compiler recognition delegates to Assimp's supported-extension query through meshTools, without a catalog-owned format list. The Assimp helper creates temporary importer state; classification is an explicit tool operation, not per-frame/per-query work. Other compiler descriptors without hooks remain explicitly DiscoveryUnavailable for unassigned sources; existing metadata can still select their registered IDs. Format-specific routing expansion and project compiler composition must use existing tools, not an extension switch in the source database.

`VisitResources` implements explicit RED-style filtered traversal with folder/direct-child, source/output type, issue masks and import-state filters. Callers receive borrowed records and may stop early. It adds no collection allocations, disk I/O, copies or locks. This is a linear filtered traversal, not a claim of indexed arbitrary search. Exact path/asset/output lookup retains the existing indexes. Output-type matching includes all authored outputs, not only a compiler's primary type. Folder matching uses directory boundaries and existing case-insensitive path policy.

### Correction pass and E1C.3 - source implementation complete

Root validation is shared by Rebuild and Rescan; duplicate paths are checked through the retained path index rather than a second temporary map. Rescan's source/sidecar pairing remains necessary input normalization. MeshAssetCompiler now gathers Assimp extensions once during initialization, not once per source; its existing instance owns that capability list.

Editor composition supplies the already-read ProjectDescriptor to the existing ProjectWorkspaceService. The service resolves roots through source/projects and owns one shared SourceDatabase, with const queries and explicit owner-thread RescanSources/ClassifySources operations. No second project service, compiler registry or parser was added. Readers must finish before rescan/classification and service destruction; no asynchronous catalog jobs or locks were added.

`nanovanguard sources inspect <project> [--format jsonl]` uses the same project resolver and SourceDatabase. It uses a local native filesystem manager for absolute-path reads, without installing a global manager or mounting runtime resources. Empty successful scans emit a zero-count summary; failed scans emit no partial inventory.

Both consumers currently compose the project's explicit writable Assets root. Additional read-only roots remain explicitly supplied, never guessed from Plugins children. A complete compiler set is not yet composed in either consumer: records start NotClassified, and the service accepts existing descriptors for explicit classification. Actual tool composition, importer settings and source/dependency callbacks belong to E1E. Do not create a second registration list just for browsing.

Source review: reopening reconstructs inventory from source/sidecar truth; runtime output identities remain path-independent. Failed enumeration preserves the previous catalog, successful rescans invalidate borrowed pointers, and orphan sidecars retain missing-source records. No source files are mutated and no streaming source ownership is changed. The other thread's ResourceStreamingService remains the sole configured mount owner.

Verification: scoped diff and source/call-site review only. No generation, compilation, tests, test authoring, editor launches or CLI runs. Restart/relocation/rescan/shutdown behavior and the added CLI linker declarations remain unverified at runtime. Existing Premake globs cover new files.

**Zero E1C implementation subphases remain; next is E1D**, followed by E1E build integration and E1F file operations. Executable validation is deferred debt, not a passing result.

Verification: source review only; no compilation, project generation, tests or test authoring.

### E1B source-level implementation

`assets/asset_metadata.hpp` now defines AssetId, importer-owned versioned settings and stable output keys, with bounded VMETA 1 text codecs and output-to-runtime identity mapping. The runtime ResourceId/ResourceReference contracts are unchanged. Within-document duplicate keys/hash collisions are rejected. See `docs/formats/vmeta-format.md` for format and migration/identity policy.

No file mutation, automatic identity assignment, catalog-wide collision enforcement or importer migration execution is claimed: those require E1C/E1E/E1F ownership. Labels and cooking overrides are not yet fields in VMETA 1 and require a later explicit schema extension. No builds or tests were run or authored; codec validation remains deferred.

- E1C: headless source inventory, explicit source roots, status and indexed queries.
- E1D: authored references and referencers, separate from VADI build dependencies and runtime load dependencies.
- E1E: project-backed adapters for existing compilers, BuildGraph and DDC; explicit first import.
- E1F: shared explicit file operations, recovery, rescan, relocation and headless closure.

E2 retains automatic watching/reimport scheduling, full incremental-change integration and cache lifecycle. No editor-only asset backend or second dependency scheduler should be introduced.
