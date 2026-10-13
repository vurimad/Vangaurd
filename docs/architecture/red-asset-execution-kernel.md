# RED asset authority adoption

Status: **approved architectural direction; bootstrap execution-kernel proof,
Gate 0 authored-resource durability, Stage 1.1 source resource-database,
Stage 1.2 real ResourceLoader/SystemInstance lifecycle, Stage 1.3 bitmap-texture
domain and Stage 2.1 texture-shadow proofs complete; broader domain migration is
not yet complete**.

This document records the decision to adopt the actual RED asset model and
backend into Vanguard rather than continue to maintain a Vanguard-authored
approximation of RED `backendData`.

This is the same kind of adoption already used for Vanguard Jobs and RED Math:
retain the authoritative upstream implementation, preserve its behavioral
model, make only narrow compatibility changes required by the host, document
every local patch, and adapt Vanguard around RED rather than rewriting RED from
inspection.

The decision is broader than adopting a cooking executor. For migrated asset
domains, RED becomes the authoritative authoring, metadata, dependency,
freshness, cooking, and cleanup system. Existing Vanguard asset machinery is
migration scaffolding until cutover has been proved; it is not a second
permanent authority.

## Decision

For a migrated domain the target ownership is:

```text
Vanguard Editor / .vproject / headless tools
                    |
                    v
          VanguardAssetHost process
                    |
     +--------------+----------------+
     |                               |
     v                               v
RED authored-resource truth       RED derived truth
ResourceEditorDepot               resdb::Database
CResource serialization           backendData dependency DB
ResourceMetadata                  dependency hash/cache
ResourceImportMetadata            Task / TaskSystem
import options/provenance          Cooker / Generator
                                   cooked depot / data cache
                    |
                    v
        Vanguard runtime/export bridge
                    |
          ResourceStreamer / VPAK
```

Vanguard remains free to provide project bootstrap, editor presentation,
runtime packaging/loading, telemetry, memory/filesystem services and durability
infrastructure. Those facilities must not independently decide whether a RED
asset is current, what its dependency graph means, or whether it must be
recooked.

## Authority model

### Authored source truth

For migrated domains, the canonical authored asset is the RED uncooked
resource stored through `ResourceEditorDepot`, including the metadata carried by
that resource.

Authoritative authored state includes:

- RED `CResource` serialized bytes;
- the logical RED `ResourcePath` used by the depot;
- embedded `ResourceMetadata`;
- embedded `ResourceImportMetadata`;
- importer options and source provenance recorded by RED;
- any domain-specific RED resource state required to reproduce cooking.

External files such as PNG, TIFF, FBX or GLB may remain import sources, but they
are not a second asset database. Import provenance and reimport options belong
to the canonical RED resource metadata once the import has succeeded.

### Rebuildable source index

RED `resdb::Database` is a rebuildable index/cache over the authored resource
depot. It accelerates discovery and change processing but is not irreplaceable
user-authored data.

Deleting or rebuilding the resource database may cost time. It must not destroy
the authored resource state described above.

### Derived truth

The following are RED-owned derived state for migrated domains:

- backendData dependency records;
- dependency hash/cache state;
- task freshness and rebuild decisions;
- cooked resource outputs;
- cooker/generator output metadata;
- optional RED data-cache entries;
- stale-output and abandoned-generated-dependency cleanup decisions.

These are reconstructable from authored RED resources, tools and configuration.
They are important for daily performance, but they are not the source of months
of authored game work.

### Vanguard state that may remain

The following may remain outside RED because they do not need to become asset
authority:

- `.vproject` as project bootstrap and configuration;
- Vanguard Editor UI and headless frontends;
- runtime resource loading and streaming;
- VPAK/package construction;
- runtime-facing resource formats during the migration;
- telemetry and diagnostics;
- Vanguard memory/filesystem abstractions where they can host RED faithfully;
- Vanguard Jobs' retained RED Jobs 2 implementation;
- a generic crash-safe file transaction/depot persistence primitive;
- content-addressed storage technology from VDDC if used strictly as physical
  storage beneath RED-owned semantics.

## Existing Vanguard asset systems are migration scaffolding

The current Vanguard asset stack remains useful while RED ownership is proved,
but it is not the target authority for migrated domains.

After successful domain cutover, the following are expected to retire as
authoritative systems:

- `SourceDatabase`;
- VMETA as canonical source metadata;
- `BuildSystem` and `BuildGraph` as the canonical build scheduler;
- `DependencyIndex` / VADI as the canonical dependency authority;
- VDDC as the canonical cache/freshness authority;
- `IncrementalRecooker` as a separate recook authority;
- most `ProjectAssetPipeline` freshness/reconciliation/publication machinery;
- compiler-registry abstractions that exist only to duplicate RED importer or
  cooker ownership.

Retirement is **not immediate**. Until a RED domain is proven and cut over, its
current Vanguard path remains the production path and must not be weakened.

Useful low-level implementations do not need to be discarded merely because
their current authority is retired. A VDDC storage primitive or
`AssetFileTransaction` mechanism may survive underneath RED if it remains a
mechanism rather than an independent source of asset truth.

## Adoption rule: preserve RED, do not imitate RED

The authoritative RED source tree for this work is rooted at:

```text
D:\root\R6.Root\Mainline\dev\src
```

The following rules are normative:

1. **Do not rewrite RED algorithms from inspection.** If Vanguard needs RED's
   resource model, import metadata, task state machine, dependency scheduling,
   cooker/generator orchestration or cleanup behavior, retain that source.
2. **Do not maintain a second partial implementation.** Once RED owns a
   migrated responsibility, the old Vanguard implementation is transitional
   and must be deleted or reduced to an adapter.
3. **Import required semantic dependencies rather than approximating them.** A
   convenience wrapper is acceptable; a new behavioral clone is not.
4. **Keep imported-source patches minimal and auditable.** Every behavior change
   against the RED image must be documented with its upstream file and reason.
5. **Prefer configuration or narrow platform cuts to redesign.** For example,
   Vanguard may restrict RED's historical multi-platform texture work to the
   active Vanguard target without replacing the cooker model.
6. **Retain provenance.** Imported source must have an `UPSTREAM.md` comparable
   to Jobs and Math, recording exact origin, retained image, exclusions,
   dependency images and compatibility patches.
7. **A code path is not accepted because it looks RED-like.** Fidelity comes
   from retained source, explicit adapters, provenance and behavioral tests.

## Preferred host: a dedicated AssetHost process

RED's asset backend expects process-wide services and globals such as the file
manager, resource loader, editor depot, reflection registrations, resource-path
cache and backendData system instance. Embedding those assumptions directly into
the interactive editor would make ownership and shutdown unnecessarily fragile.

The preferred target is therefore a long-lived local process:

```text
VanguardEditor / nanovanguard
          |
       local IPC
          |
          v
VanguardAssetHost.exe
  - retained RED system/resource/reflection stack
  - ResourceEditorDepot
  - ResourceLoader / resource database
  - backendData SystemInstance
  - TaskSystem
  - selected real RED importers/cookers/generators
  - retained RED Jobs 2
```

This process boundary provides four useful properties without changing RED
asset semantics:

- RED process-wide globals have one deliberate lifetime;
- importer/cooker failures cannot directly corrupt Editor process state;
- long top-level builds do not block the editor UI thread;
- editor and headless tooling share the exact same authoritative backend.

The AssetHost may apply non-authoritative admission around incoming work to
bound memory or request pressure. It must not replace RED `Task`/`TaskSystem`
freshness, dependency or cleanup decisions with Vanguard equivalents.

## Transplant boundary

`backendData` is not a standalone scheduling library. A faithful host requires
the RED resource/runtime substrate that its semantics depend on.

The complete long-lived host is expected to retain a narrow closure containing:

- RED system/core/container/memory/filesystem support required by the selected
  resource stack;
- reflection/serialization and `res::ResourcePath` support;
- `CResource` serialization/load/save;
- `ResourceEditorDepot` and its mount mapping;
- resource loading and source resource database;
- backendData `SystemInstance`, `SystemContext`, `Task`, `TaskSystem`;
- backendData dependency database/cache/types/collector;
- cooker/generator task implementations and output collection;
- the data cache only when a selected flow needs it;
- selected concrete importer/cooker modules required by the migrated domain.

Do not link all of historical `backendResources` merely to obtain this closure.
Its project pulls unrelated audio, localization, character, entity, Houdini,
FBX and other backend dependencies. Retain authoritative source images as
needed, but activate the smallest proven build subset.

The bootstrap execution-kernel proof is intentionally narrower than that
complete production host. It may own scratch source/cooked depots and a small
`data::SystemContext` adapter while leaving source `resdb`, watcher and
`ResourceLoader` pointers null, but only when source inspection proves the
selected task/cooker path never dereferences those services. This is a staging
boundary, not permission to replace their production semantics.

### First retained-source closure

Vanguard already retains most of the low-level RED substrate needed by the
AssetHost. The first host should reuse those retained images rather than import
duplicates:

- `redCore`, `redSystem`, `redMemory`, `redContainers`, `redMath`;
- `redIO` and Vanguard's RED-compatible async-I/O initialization;
- `redFileSystem` and the existing `GFileManager` compatibility owner;
- `redJobs2` and the existing Vanguard Jobs compatibility owner;
- `redReflection`, including `ResourcePath`, `CResource`, `ResourceLoader`,
  `GResourceLoader`, resource-path cache and resource-depot globals;
- `redConfig`, comm protocol/channel, compression and the other already-retained
  dependencies of `redReflection`.

New retained source images for the first AssetHost are expected to be:

- `common/resource`;
- `common/archive` (also used by backendData dependency hashing/cache code);
- `common/redCrypto` (backendData dependency hashes use RED's SHA-1
  implementation);
- `backend/backendData`;
- `backend/backendEngine` as an upstream image, while compiling only the narrow
  helper surface required by the first host.

Retained upstream source lives under the repository-level `imported/` tree, not
under `source/`. `source/` is reserved for Vanguard-owned production modules;
`imported/` is the auditable retained-source image and dependency substrate. For
example, the first asset images are expected at `imported/common/resource`,
`imported/common/archive`, `imported/common/redCrypto` and
`imported/backend/backendData`. Existing retained RED foundations are relocated
to the same repository-level tree.

For the complete host, the initial backendEngine build surface is intentionally
small: the original resource-path classification helpers, depot-directory
scanning helper and tiny resource-database provider interface used by
backendData/resdb. The bootstrap CSV target does not link backendEngine
implementation files; it retains the complete image now so those later helpers
remain upstream-auditable. Build exclusion is not permission to rewrite them in
Vanguard form.

`ResourceMetadata` / `ResourceImportMetadata` and import command support are the
next narrow backendEngine slice when authored import/reimport is enabled. They
are not required merely to prove that the retained `TaskSystem` and cooker
kernel can cook a pre-existing scratch RED resource. The bootstrap does not use
`SystemInstance`; Stage 1.2 below activates its real local lifecycle separately.

The first host does not need RED data-cache servers, PostgreSQL, historical RED
commandlets, editor/render/audio systems, or the whole `backendResources`
project. Concrete domain cooker/importer modules are added only when that domain
is migrated.

In particular, CSV does not require `backendResources`. `C2dArray` is itself the
canonical `.csv` resource in `common/resource`; the resource module registers its
CSV loader/saver. During backendData cooking, no specialized `ICooker` claims a
CSV, so `cooker::Task` follows its original generic fallback: load the source
`CResource` and save the cooked binary resource. `backendResources`'
`C2dArrayFactory` is editor/factory convenience for creating a new empty table,
not part of the first scratch cook semantics.

The historical backendData `build.h` includes broad app/backend/engine headers.
That is build coupling, not evidence that all those systems are semantic owners
of `TaskSystem`. The retained AssetHost build may make a narrow compatibility
cut around those includes while compiling the original backendData source files
that own asset behavior.

### First semantic proof resource

The first runnable RED island should use a scratch `C2dArray` / `.csv` resource
from `common/resource`. It is already a registered RED resource type, and the
generic cooker task has an authoritative no-custom-cooker fallback that loads a
source `CResource` and writes a cooked resource.

That fixture is deliberately chosen because it proves the important machinery
without pulling a production domain closure:

1. scratch source/cooked depot access through RED resource paths;
2. dependency/hash cache initialization;
3. real `data::Task` / `TaskSystem` resolve and build phases;
4. the real generic cooker fallback and cooked resource serialization;
5. persistent dependency metadata;
6. process restart;
7. a no-recook second build when inputs/tool state are unchanged.

`ResourceEditorDepot`, source `resdb::Database`, its filesystem watcher and
the full `data::SystemInstance` remain part of the complete AssetHost
lifecycle below. The bootstrap proof must not be reported as having exercised
those systems.

#### Bootstrap kernel proof completed 2026-10-13

The first standalone `VanguardAssetHost` now compiles a CSV-only subset of the
retained RED images and uses Vanguard-owned adapters only at the host boundary:

- `ScratchDepot : res::IBackendResourceDepot` maps one absolute scratch source
  root and one absolute scratch cooked root;
- `HostSystemContext : data::SystemContext` supplies those depots, source and
  cooked dependency hash caches, a real `dependency::BinaryDatabase`, target
  platform PC and RED's error handler;
- source/cooked `ResourceLoader`, source resource DB, output collector and
  remote data cache are null because the selected CSV fallback does not access
  them;
- the host invokes the retained `data::TaskSystem`, `cooker::Task`,
  dependency collector/cache/database code, RED Jobs 2, resource serialization
  and `C2dArray::ParseData`;
- `backendResources` and backendEngine implementation files are not linked;
  the complete backendEngine source image remains retained for later slices.

The checked-in target directly compiles the selected retained translation units
into the executable rather than activating the historical RED module projects.
That is deliberate for the bootstrap: the missing historical generated
`typeRegistryFuncImpl.h` files are module force-link registries, while the
selected translation units already contain their static RTTI registrators.
Vanguard reflection initialization supplies the retained base reflection/resource
registration and the host explicitly initializes `data::PoolDataSystem`. This
bootstrap does not fake or replace generated RTTI; a later wider module slice
must revisit generated registration if it relies on types not linked directly.

`tools/assetHost/src/red_asset_host_bridge.cpp` contains only the link seams that
would otherwise pull broad historical modules into this CSV proof: the exact
`data::TargetPlatform` RTTI enum registration from backendData, the base
`dependency::Database` destructor, and byte-for-byte-equivalent extension
predicates for `IsCsvResource` / `IsJsonResource` from backendEngine. The normal
backendEngine implementation remains authoritative for the production host.

The AssetHost target also makes its historical runtime DLL dependencies explicit
instead of relying on broad Vanguard runtime deployment: it copies the retained
NVTools DLL and the configuration-appropriate retained Oodle DLL into the tool
output directory after build. No remote data-cache service, PostgreSQL server,
historical commandlet runtime, or editor/render stack is deployed for this proof.

The proof used a fresh scratch `probe.csv` containing two semicolon-separated
data rows. A closure-validation rerun preserved the exact process result lines.
In the first process:

```text
RESULT success=1 cook_writes=1 task_files_built=0 build_reason=0x00001000 database_preexisting=0 source_cache_preexisting=0 cooked_cache_preexisting=0 persisted=1
```

The cooked output was 473 bytes with SHA-256
`3662d1d2d14975cb37a84c22169e319b2ac5a32c79bd210136ab32b4bb8052f8`.
After clean process exit, a second process loaded the persisted dependency
database and both hash caches and reported:

```text
RESULT success=1 cook_writes=0 task_files_built=0 build_reason=0x00000000 database_preexisting=1 source_cache_preexisting=1 cooked_cache_preexisting=1 persisted=1
```

The cooked SHA-256 and last-write timestamp remained unchanged, and the binary
dependency database timestamp also remained unchanged. RED's source/cooked hash
cache files were explicitly saved again before service teardown, analogous to
the persistence work historical `SystemInstance::Shutdown()` performs. The
bootstrap never constructs `SystemInstance`. Therefore "no-op" in this proof
means **no recook and no cooked-output rewrite**, not zero filesystem activity.
`task_files_built` is zero on both runs for this generic CSV fallback and is not
a useful recook discriminator; the task-level freshness witness is
`build_reason` together with the cooked-depot writer count and unchanged output.

The bootstrap persists the dependency database when changed, saves both hash
caches, then shuts down Vanguard Jobs, filesystem, I/O and diagnostics in reverse
host order. The current Vanguard memory, containers and reflection compatibility
surfaces are process-lifetime initializers and expose no shutdown entry point;
their state therefore ends with the short-lived AssetHost process after all
dependent services have been drained. This is distinct from the complete RED
`SystemInstance` shutdown path exercised by the Stage 1.2 project proof below.

The initial proof used a disposable VS2022 x64 Development harness while the
repository-wide engine-source audit blocked fresh Premake generation. After that
unrelated audit debt was corrected, `premake5.exe vs2022` completed normally and
generated the canonical `VanguardAssetHost.vcxproj`. The checked-in project then
built successfully with outer MSBuild `/m:1` in both x64 Development and Debug.
The Development output contains `VanguardAssetHost.exe`, the retained
`nvToolsExt64_1.dll` and release `oo2ext_7_win64.dll`; Debug contains the same
NVTools DLL and `oo2ext_7_win64_debug.dll`. This exercises the target's real
postbuild packaging rather than only checking those source paths statically.

The canonical Development executable was then rerun from a new scratch root in
two separate processes using the same deterministic two-row `probe.csv`. The
first and second `RESULT` lines were exactly the values recorded above. The
cooked output was again 473 bytes with SHA-256
`3662d1d2d14975cb37a84c22169e319b2ac5a32c79bd210136ab32b4bb8052f8`;
its last-write timestamp remained unchanged across the second process, as did
the binary dependency database timestamp. The two dependency hash-cache files
were saved again during shutdown persistence as expected. The bootstrap proof
is therefore reproduced through the normal repository generation, build,
deployment and executable path rather than relying on the disposable harness.

Only after that proof is green should the first concrete texture/material/model
module closure be activated.

## AssetHost lifecycle

The complete production AssetHost must follow RED's real process/global
lifetime assumptions rather than hide them behind an unrelated Vanguard
ownership model. The bootstrap kernel above is deliberately not a claim that
the complete lifecycle has already been exercised.

### Startup

1. initialize the already-retained Vanguard/RED memory, I/O, filesystem, Jobs 2
   and reflection compatibility services; the base `GFileManager` must exist;
2. run durable source-transaction recovery **before** exposing a source depot,
   resource DB or watcher;
3. initialize the retained `resource` module so its RTTI and resource
   loader/saver factories are registered;
4. initialize retained backendData RTTI/memory-pool registration and any
   selected concrete cooker/importer modules before their RTTI lookup is used;
5. construct `ResourceEditorDepot` from project engine/game/mount mappings,
   load the relevant ResourcePath/directory caches, then publish the RED editor
   and general resource-depot globals;
6. create the source `ResourceLoader` for the editor depot and publish
   `GResourceLoader`;
7. construct `data::SystemInstance`, call `InitSourceDepot()`, then
   `InitCookedDepot()` (or source-as-cooked only for a generator flow), set
   platform/options, and `WaitUntilInitialised()`;
8. create the `SystemContext`, the real cooker/generator `TaskSystem` and its
   progress/error reporter;
9. admit builds through `TaskSystem.Build(paths)`.

`SystemInstance::InitSourceDepot()` installs RED's source resource-database
filesystem watcher. That watcher deliberately replaces the process-global
`GFileManager` with a forwarding wrapper so writes/moves/deletes can be tracked.
This is an important lifetime fact, not an implementation detail to ignore.

### Shutdown

1. stop admitting work and finish/destroy all `TaskSystem` work and reporters;
2. shut down any selected RED global caches while Jobs remains alive;
3. call `SystemInstance::Shutdown()` while Jobs, source/cooked depots, loaders
   and the filesystem remain alive; this synchronously removes the source DB
   watcher/restores the original `GFileManager`, then flushes resource DB,
   dependency DB and dependency hash-cache persistence;
4. destroy `SystemInstance` while the filesystem is still live, because its
   owned cooked depot may save its directory listing cache on destruction;
5. clear `GResourceLoader`, shut down/destroy the source loader;
6. clear the general/editor depot globals, then destroy the source editor depot
   while the filesystem is still live so its directory listing cache can save;
7. save any explicit ResourcePath cache required by the host configuration;
8. shut down the base filesystem/I/O service only after every RED depot/watcher
   is gone;
9. shut down Jobs after no RED counters/loaders/caches can enqueue work.

The ordering above is mandatory. In particular, shutting down Vanguard's base
filesystem before `SystemInstance::Shutdown()` would leave the resource DB
watcher wrapping a destroyed manager.

### Stage 1.1 source resource-database proof completed 2026-10-13

The scratch AssetHost now compiles and owns the real retained
`resdb::Database`. Its proved lifecycle is deliberately narrower than the full
`data::SystemInstance` lifecycle above:

1. initialize `DurableResourceDepot`, including abandoned transaction recovery;
2. initialize and synchronously converge `resdb::Database` against that depot;
3. install the resource-database filesystem watcher;
4. bind the durable depot's post-`PublishCommit()` path sink to
   `Database::EnqueueCommittedPaths()`;
5. after a durable cohort commit, finish transaction cleanup and synchronously
   run `Database::Update()`;
6. admit `TaskSystem::Build()` only after the update has converged;
7. finish and destroy the TaskSystem/context used for admitted work;
8. detach the committed-path sink, remove the watcher, shut down the database,
   then shut down the durable depot.

The committed-path callback only enqueues paths. It cannot run `Database::Update()`
itself because `PublishCohort()` still owns the depot's exclusive publication
gate at that point, while the database scan re-enters depot readers. Updating in
the callback would therefore self-deadlock. The explicit post-publication update
is the required admission barrier.

The database's memoized recursive-forward lookup is also invalidated whenever a
full or incremental scan replaces the resource/link tables. The Stage 1.1 proof
forces an old recursive lookup into that cache, commits a changed dependency,
then requires the new recursive dependency and the absence of the old one before
TaskSystem admission.

Canonical Development and Debug clean-process output now also proves the narrow
quoted-path tweak scanner, binary-CSV fail-closed behavior and non-OK scan-status
admission gate:

```text
STAGE11_DB_RESULT success=1 recovery_before_db=1 initial_db=1 recursive_initial=1 tweak_scan=1 binary_csv_failed_closed=1 failed_scan_rejected=1 fullscan_artifacts_filtered=1 incremental_artifacts_filtered=1 durable_commit=1 db_stale_before_update=1 db_converged=1 recursive_converged=1 task_admitted_after_db=1 tasks_succeeded=1 exact_files=1 artifacts_absent=1 recovery_found=0 recovery_rolled_back=0
```

After arming a two-resource transaction in `Applying` and terminating the
process, the next Development and Debug process reports the same successful
proof with:

```text
recovery_found=1 recovery_rolled_back=1
```

The dependency-scanner link seam used by this proof is intentionally restricted
to the synthetic Stage 1.1 CSV/file-table surface. Entity, mesh, prefab and
appearance resources that need the omitted embedded-package scanners fail
closed. This is not a production scanner replacement and real project resources
must not be admitted through this seam.

### Stage 1.2 real ResourceLoader/SystemInstance proof completed 2026-10-13

Stage 1.2 moves the integration proof into a disposable but structurally real
Vanguard project, `games/RED Asset Test/red-asset-test.vproject`, and replaces
the bootstrap's hand-wired source/cooked context with the real local lifecycle:

1. validate the exact disposable project identity before project-local cache
   directories can be created;
2. initialise `DurableResourceDepot` and finish transaction recovery before any
   resource database or loader observes the authored tree;
3. publish that durable depot as both editor and general source depot;
4. create the real uncooked source `ResourceLoader` and publish
   `GResourceLoader`;
5. construct `data::SystemInstance`, call `InitSourceDepot()` and
   `InitCookedDepot()`, wait for initialisation, then obtain its real
   `SystemContext`;
6. bind the durable committed-path sink to the SystemInstance-owned source
   resource database and retain the Stage 1.1 synchronous update/admission gate;
7. durably author `stage12_probe.csv`, load the resulting `C2dArray` through
   `SystemContext::SourceLoader()`, and verify its cells;
8. cook through the real `TaskSystem` using that SystemInstance context;
9. destroy TaskSystem work, detach the committed-path sink, call
   `SystemInstance::Shutdown()` and destroy the SystemInstance while its borrowed
   source loader/depot are still alive, then clear/shut down those source globals
   and finally destroy the durable source depot.

The first Development process reports a required build:

```text
STAGE12_SYSTEM_RESULT success=1 recovery_before_system=1 real_source_wiring=1 authored_preexisting=0 database_ready=1 loader_loaded=1 task_succeeded=1 cooked_preexisting=0 build_reason=0x00001000 files_built=0 restart_reuse=1 globals_cleared=1
```

The next Development process, and Debug verification processes against the
persisted dummy-project state, report:

```text
STAGE12_SYSTEM_RESULT success=1 recovery_before_system=1 real_source_wiring=1 authored_preexisting=1 database_ready=1 loader_loaded=1 task_succeeded=1 cooked_preexisting=1 build_reason=0x00000000 files_built=0 restart_reuse=1 globals_cleared=1
```

`restart_reuse=1` includes the unchanged cooked-file timestamp witness; as in the
bootstrap CSV path, `files_built` is not the recook discriminator. The real
source database, watcher, source/cooked dependency hash caches, cooked depot,
cooked loader and binary dependency database are owned by `SystemInstance`.
Remote data-cache services, server/worker task accumulation and the loose
dependency-database backend are intentionally unavailable in AssetHost and fail
closed rather than pulling their unneeded service closure.

The command is also fail-closed against an arbitrary root: the negative proof
returns `stage12=project-identity-mismatch` before creating the supplied path.
This closes the local ResourceLoader/SystemInstance lifecycle gate, not the
asset-domain scanner gate. The Stage 1.1 scanner seam still rejects domains whose
authoritative embedded scanners are not active in the build.

## Resource identity boundary

RED and Vanguard currently use different 64-bit path identities:

- RED `res::ResourcePath`: lowercase, `\\` separators, FNV-1a 64-bit;
- Vanguard runtime `ResourcePath`: lowercase, `/` separators, CRC64.

The first asset-authority surgery does **not** require a runtime identity flag
day. Both IDs can be derived deterministically from the same logical path text.
The runtime/export bridge may therefore translate the path into both identities
without a permanent mapping database.

Migrating Vanguard runtime identity to RED can be considered later and must not
be bundled into the initial authored-resource cutover without evidence that it
simplifies the system.

## Gate 0: authored-resource durability

RED becomes trustworthy source authority only after its persistence layer meets
Vanguard's data-loss bar.

Historical RED `SaveResource()` is not sufficient for that bar as currently
implemented: the destination writer may open/truncate the canonical resource
before serialization has successfully completed. A serializer or write failure
can therefore destroy the previous known-good authored file.

Historical `FOF_SafeWrite` is not proof of safety for this migration. The
relevant implementation still opens the target too early for the required
crash semantics.

The fix belongs **beneath RED asset semantics**, at the depot/filesystem
persistence boundary. It must not create a second asset database.

### Single-resource persistence contract

For an existing canonical resource:

1. serialize the complete new resource without truncating the canonical file;
2. write it to a sibling staging file on the same filesystem;
3. validate the staged bytes as a complete RED resource;
4. flush file contents and required filesystem metadata according to the host
   durability contract;
5. atomically replace the canonical path;
6. remove abandoned staging state during deterministic recovery.

Required crash outcomes:

- before atomic replace: the old complete canonical resource remains;
- after atomic replace: the new complete canonical resource is visible;
- no crash point exposes a partially serialized canonical resource.

#### Single-resource Gate 0 proof completed 2026-10-13

The durability layer keeps historical RED serialization authoritative and
changes only the physical publication boundary. `VanguardAssetHost` now compiles
the real retained `ResourceEditorDepot`/filesystem-depot implementation, its
narrow retained archive support, `resourceSaver.cpp`, the CSV saver, and the RED
filesystem watcher. Vanguard-owned `DurableResourceDepot` subclasses
`ResourceEditorDepot` itself and can therefore be installed through the normal
`res::SetEditorDepot()` global. Default `ResourceSavingOptions`, backendData
source access, ResourceBackend's injected depot, and direct
`GetEditorDepot()->CreateResourceWriter()` callers all share the same durability
and publication gate rather than a side wrapper that could be bypassed.

Historical `res::SaveResource()` still serializes one complete file image in
memory and performs one final writer `Serialize()`. The durable writer does not
open or truncate the canonical path when it is created; that complete image is
synchronously staged, verified and committed through `AssetFileTransaction`
before control returns to `SaveResource()`. RED's subsequent `UnmarkModified()`
therefore occurs only after the durable Committed journal exists. The retained
resource module also exposes `SerializeResource()`, factored from that same RED
serialization sequence, so compound imports can serialize every member before
any namespace publication starts. Current retained direct writer users in the
activated host slice were audited and each supplies one complete buffer in one
`Serialize()` call; this bounded complete-image writer is not a general streaming
file API.

The proof's newly constructed `C2dArray` objects explicitly disable the optional
deferred-buffer prepass because they have no loader-owned deferred buffers. The
complete Stage 1 host must exercise that prepass with its real ResourceLoader
lifetime; this is a loader-lifecycle validation item, not a durability bypass.

The transaction substrate was strengthened for unconditional replacement of an
existing file. Its previous Applying sequence moved `target -> .vtxn-backup`
before publishing `.vtxn-stage -> target`, which was recoverable but created a
target-absent namespace window. Vanguard filesystem now exposes an atomic
same-directory replace-with-backup primitive backed by `ReplaceFileW` on
Windows. `AssetFileTransaction` uses that operation for unconditional existing
writes, so one kernel publication simultaneously installs the verified stage
and preserves the exact old target as rollback evidence. Conditional/CAS writes
retain their prior conflict-sensitive path. `filesystemTests` and `assetsTests`
cover the primitive and Applying-state rollback.

The canonical Development and Debug AssetHost builds exercise the source-authority
witnesses in scratch authored roots. A failed serializer leaves the old canonical
bytes exact and the resource Modified; successful create/replacement clears
Modified only after durable publication:

```text
GATE0_RESULT success=1 rejected_save_preserved=1 failed_save_modified=1 first_save=1 replace_save=1 exact_replacement=1 artifacts_absent=1 recovery_found=0 recovery_rolled_back=0
```

The rejected-save witness uses a host-only saver that fails after RED has asked
the depot for a writer but before serialized bytes are published. The previous
canonical bytes remained exact and the resource remained Modified. A successful
CSV create and replacement both cleared Modified only after the transaction
completed; the replacement file matched the exact expected RED CSV bytes and no
stage/backup/journal artifacts remained.

Restart recovery was then exercised across separate AssetHost processes. One
process intentionally stopped after `ApplyPrepared()` had atomically installed
an interrupted candidate while preserving the old resource and durable Applying
journal:

```text
GATE0_ARM_RESULT success=1 applying_target=1 backup_present=1 journal_present=1 prior_recovery=0
```

A new AssetHost process initialized the durable depot before exposing the authored
root. Initialization found the Applying journal, restored the exact pre-image,
cleaned transaction artifacts, and reported:

```text
GATE0_RESULT success=1 rejected_save_preserved=1 failed_save_modified=1 first_save=1 replace_save=1 exact_replacement=1 artifacts_absent=1 recovery_found=1 recovery_rolled_back=1
```

The real mapped editor depot is also exercised with two overlapping `base\`
mounts. Generic translation chooses the later mapping, an existing resource is
durably written back to the earlier mapping that actually owns it, a new resource
is created in the later writable mapping, and default `ResourceSavingOptions`
resolve the globally installed durable depot:

```text
GATE0_MAPPED_RESULT success=1 generic_last=1 existing_owner=1 new_last=1 global_durable=1 default_depot=1 exact_existing=1 exact_new=1 unmodified=1 artifacts_absent=1
```

### Compound import persistence contract

A format such as GLB may create or replace a cohort of mesh, material, texture
and optional composition resources. Per-file atomicity is not sufficient: a
crash halfway through publication must not leave an incoherent mixture of old
and new authored resources.

The depot therefore needs a bounded cohort transaction with staged members and
recoverable commit intent. All members are serialized and validated before
canonical publication begins. Recovery must deterministically converge to one
complete cohort generation; it must never silently accept a partial mixture.

Windows does not provide one atomic namespace operation for replacing N files.
The cohort guarantee is therefore an **AssetHost visibility guarantee**, not a
claim that all N directory entries change in one kernel operation. While an
Applying transaction swaps members, the AssetHost must hold the authored-source
publication gate: no resource-DB update, TaskSystem admission, import response,
or other authoritative reader may accept that intermediate namespace. The
durable Committed journal is the cohort linearization point. On process start,
transaction recovery runs before `ResourceEditorDepot`, its resource database,
or its watcher is exposed; an interrupted Applying generation is rolled back
before any authoritative RED reader can observe it.

Transaction-owned stage, backup and journal files are not resources. They must
be invisible to both resource-DB full enumeration and the incremental
filesystem watcher. The stock RED watcher currently forwards successful writer,
move and delete paths into the resource DB, so the retained integration needs
one narrow shared transaction-artifact predicate at that boundary. Canonical
resource paths are released to the RED change stream only after the durable
cohort outcome is known.

Vanguard `AssetFileTransaction` is a candidate implementation substrate for
this durability layer. If reused, its asset-specific freshness/authority logic
must not survive into the RED authority boundary. RED still decides what the
resources mean and when they need to be rebuilt; the transaction layer only
makes the requested filesystem state durable.

#### Compound Gate 0 proof completed 2026-10-13

`DurableResourceDepot` exposes a bounded cohort API over the same transaction:
all logical members are serialized first, then staged and verified while shared
depot readers continue to see the old generation. `ApplyPrepared()` acquires an
exclusive authored-publication gate. That gate remains held across every member
swap until `PublishCommit()` durably records the Committed journal. Only then are
authoritative readers released onto the new generation. If Applying fails,
`AssetFileTransaction` rolls touched members back before the gate opens. If that
rollback itself cannot complete and the transaction enters `RecoveryRequired`,
the process deliberately keeps the exclusive gate closed and fails closed until
restart recovery.

The Committed journal is the logical publication point; backup/journal cleanup
after that point is transaction-owned housekeeping. A
`CommittedCleanupPending` result therefore remains a successful authored commit,
while the non-Ready transaction state prevents another write until deterministic
restart cleanup completes.

The canonical Development and Debug executables both prove the clean two-member
cohort path. Prepare leaves the old generation visible, a concurrent depot reader
blocks during Applying, no canonical logical paths reach the RED change queue
before commit, exactly both cohort paths are released after the Committed journal,
and transaction artifacts remain filtered:

```text
GATE0_COHORT_RESULT success=1 staged=1 prepared_old_visible=1 no_paths_before_commit=1 reader_blocked=1 committed_paths=1 reader_released=1 exact_cohort=1 artifacts_absent=1 watcher_filtered=1 recovery_found=0 recovery_rolled_back=0 recovery_preimage=1
```

Another process intentionally exits with both new members installed under an
Applying journal and both old backups present:

```text
GATE0_COHORT_ARM_RESULT success=1 applying_a=1 applying_b=1 backups_present=1 journal_present=1
```

The next process recovers before depot exposure, restores the complete previous
two-resource generation, and then repeats the clean cohort proof:

```text
GATE0_COHORT_RESULT success=1 staged=1 prepared_old_visible=1 no_paths_before_commit=1 reader_blocked=1 committed_paths=1 reader_released=1 exact_cohort=1 artifacts_absent=1 watcher_filtered=1 recovery_found=1 recovery_rolled_back=1 recovery_preimage=1
```

Transaction publication uses the base filesystem rather than RED's forwarding
watcher, so the durability layer owns a synchronous committed-path sink. It fires
only after `PublishCommit()` succeeds and while the publication gate is still
closed. The Gate 0 proof binds that sink to the real retained
`FileSystemWatcher::PushResourcePath()` and verifies its timing. The retained
resource database exposes `EnqueueCommittedPaths()` for the Stage 1 binding; the
host must then run/await the normal database update before TaskSystem admission or
reporting import success.

One shared retained backendData predicate excludes `.vtxn-stage`,
`.vtxn-backup`, `.vtxn.stage`, and `.vtxn` artifacts from both incremental watcher
input and resource-database full-scan filtering. The retained ResourceBackend save
path also returns immediately on failed `res::SaveResource()`, so thumbnail/GPU
side effects and `m_resourceSaved.Notify()` happen only after a successful durable
save.

The Gate 0 executable originally stopped before the live resource database
because its historical binary dependency scanner crosses into broad unretained
asset-domain closures. Stage 1.1 now links the real `resourceDatabase.cpp` and
proves the post-commit enqueue/update/admission lifecycle with the deliberately
restricted, fail-closed scratch scanner seam described above. Stage 1.2 now also
proves the real local ResourceLoader and `SystemInstance` lifecycle. Full domain
scanner fidelity remains later Stage 1 integration work. Gate 0 itself remains the completed persistence/publication
contract and has not been broadened into asset-domain authoring.

Gate 0 is mandatory before a RED resource depot is allowed to hold irreplaceable
project authoring truth.

## Import and reimport

RED's intended import flow is authoritative where an upstream importer exists:

```text
external source
    -> ResourceImportCommand
    -> IImporter::FindImporter / DoImport
    -> CResource
    -> ResourceMetadata / ResourceImportMetadata
    -> durable ResourceEditorDepot save
```

`ResourceImportMetadata` carries original/intermediate source paths, provenance,
source checksum, version-control information and import options. RED reimport
reconstructs options from the existing resource metadata when new options are
not supplied.

This is why VMETA should not remain a competing canonical metadata authority
after a domain is cut over.

### GLB exception

No stock RED GLTF/GLB importer has been found in the inspected backend modules.
Vanguard therefore may keep its GLB parsing/frontend code, but that frontend
must terminate in RED-owned resources and metadata rather than the current
Vanguard BuildRequest/BuildGraph authority.

The target shape is:

```text
Vanguard GLB parser
    -> RED-facing compound importer/coordinator
    -> canonical RED mesh/material/texture resources
    -> RED import metadata/options
    -> RED backendData cooking/freshness
```

Embedded textures should become independent RED-owned resources/tasks where the
resource model permits it. The compound-import transaction described above is
what protects the authored cohort.

## RED owns incremental recooking

There is no need to wrap RED with a new Vanguard `IncrementalRecooker`.

RED's coherent recook/freshness model is integrated into `data::Task` and
`TaskSystem`, including stored dependency loading, source/rescan/built dependency
checks, output validation, tool/file-version changes, cache use, rebuilding,
metadata update and stale-output cleanup.

A thin AssetHost change-trigger controller is acceptable:

1. source watcher/resource DB reports changed resources;
2. controller chooses roots that should be evaluated;
3. controller calls `TaskSystem.Build(paths)`.

That controller must not independently decide which outputs are stale or
whether a resource needs rebuilding. Those decisions remain RED's.

## Runtime bridge

Initial adoption should not require the renderer/resource streamer to consume
historical RED runtime formats directly.

The conservative first bridge is derived and disposable:

```text
RED canonical cooked resource
       -> deterministic export/conversion
       -> current Vanguard .vtex/.vmesh/.vmat/... artifact
       -> loose development overlay or VPAK
```

The bridge is not authored truth and may be deleted/rebuilt at any time. Its
only purpose is to let RED own authoring/cooking before runtime format migration
is decided.

The following Vanguard runtime contracts are intentionally preserved across the
RED authored-resource cutover:

- `.vmesh`, `.vmat`, `.vtex`, `.vshader`, `.vpipeline` and other Vanguard
  runtime artifacts remain derived runtime representations;
- loose development overlays and `ResourceStreamer` remain the runtime loading
  path;
- VPAK remains the deployable package/container format;
- renderer residency, GPU Scene, RenderFlow and the RHI remain Vanguard-owned;
- descriptor domains, bindless descriptor indices and GPU handles remain
  renderer/runtime state and must not become RED authored-resource state.

In particular, RED materials and textures describe logical resource semantics,
not Vanguard descriptor placement. The derived `.vmat`/`.vtex` closure carries
logical typed dependencies into the current renderer, and the renderer resolves
those dependencies to residency identities and bindless descriptors exactly as
it does today. A migration change that stores a Vanguard descriptor handle,
bindless index, GPU Scene index, RenderFlow identity or native RHI object in a
canonical RED resource crosses the ownership boundary and is rejected.

Likewise, VPAK assembly remains downstream of cooking. It packages committed
derived runtime artifacts and does not become RED authored truth. The provenance
of a `.vmesh` or `.vmat` may change from a Vanguard-authored source to a RED
canonical resource without changing the runtime package architecture.

Longer term, Vanguard may evaluate consuming RED cooked serialization directly.
That is a separate decision and is not implied by the authored-resource cutover.

### Editor resource names and canonical RED extensions

Canonical RED resources keep their real RED resource extensions on disk during
the initial migration. Examples include `.mesh`, `.xbm`, `.ent`, `.app`, `.rig`
and `.anims`. These extensions participate in RED resource-class lookup,
factory/importer selection, cooker dispatch and existing tooling; renaming them
at the depot boundary would create broad compatibility work for no semantic
benefit.

The Vanguard Editor may present friendly resource type names, icons, extension-
hidden labels, or other purely visual aliases. For example, an authored
`skin_albedo.xbm` may be displayed as a Texture in the browser without changing
its canonical `res::ResourcePath`. Logs, dependency graphs, commandlets and
AssetHost APIs continue to use the true RED path. Runtime-derived files keep
their Vanguard extensions (`.vmesh`, `.vmat`, `.vtex`, ...); authored RED and
derived Vanguard names are different layers rather than competing identities.

### Mesh derived-runtime bridge

For meshes, the derived bridge should exploit the close correspondence between
RED's cooked mesh representation and Vanguard's `vmesh` contract. `CMesh`
remains the authoritative RED resource. Vanguard does not unpack it into a new
source mesh and run a second independent geometry cook.

The preferred path is:

```text
CMesh
    -> RED mesh cooking
    -> rend::RenderMeshBlob
    -> thin deterministic Vanguard exporter
    -> .vmesh
```

`rend::RenderMeshBlob` supplies the already-cooked geometry representation:
packed vertex/index bytes, chunk records, vertex layouts and offsets,
quantization and cooked LOD data. The exporter translates those records into
`vmesh` buffers/pages/layouts/submeshes and copies or streams compatible packed
byte ranges directly. It must not invoke Vanguard's meshoptimizer-based source
cooker merely to deduplicate, simplify, regenerate LODs, optimize, repack or
requantize geometry that RED has already cooked. That would add cost and create
semantic drift.

The rest of the runtime mesh closure is read from `CMesh` and preserved, not
removed. The exporter must carry every gameplay/rendering-relevant semantic into
the Vanguard derived closure, including as applicable:

- material-slot identities and material dependencies;
- external and local material-instance meaning, with local instances exported
  to their corresponding derived runtime material resources rather than lost;
- mesh appearance/material remapping semantics;
- mesh-to-rig/skeleton identity and the bone-name/remap data required to bind
  skinning correctly;
- rig matrices, bone epsilon data, LOD bone masks and float-track information
  when required by runtime deformation/animation semantics;
- LOD selection semantics, chunk membership, render/shadow flags, bounds and
  quantization/decode parameters.

Storage does not need to be member-for-member identical to `CMesh`. A semantic
may live in `.vmesh` metadata or in another deterministic derived runtime
resource when that produces a cleaner runtime boundary. The requirement is
lossless preservation of runtime meaning. RED implementation details such as
live `TRenderPtr`/handle state, allocator/container types, editor-only source
UUID bookkeeping, temporary import structures and obsolete compatibility fields
are not runtime asset semantics and need not be copied.

Two known schema differences require explicit representation rather than a
lossy conversion:

1. RED cooked mesh LODs are distance-based while current `vmesh` stores minimum
   screen coverage. The bridge must preserve RED's LOD policy directly (for
   example by extending the runtime LOD record with an explicit selection mode)
   instead of inventing a distance-to-screen-coverage approximation.
2. `CMesh` appearances can remap materials while current `vmesh` binds one
   material slot per submesh. The derived Vanguard closure must retain the named
   appearance/remap sets rather than flattening them to a single material
   assignment or duplicating geometry.

The performance target is therefore metadata translation plus bounded
page/segment emission and integrity hashing, not a second geometry cook. Where a
RED packed stream format maps directly to a Vanguard runtime stream format, its
payload should pass through byte-for-byte. Large meshes should be exportable by
streaming ranges from the RED cooked blob into Vanguard artifact/VPAK-ready
segments without constructing another complete mesh-sized intermediate buffer.

## Performance constraints

RED is coherent, but adoption must still satisfy Vanguard's daily-performance
bar.

Known risks that require measurement rather than assumption:

- source resource DB initialization performs full depot enumeration/sort;
- unchanged files can short-circuit by timestamp and watcher updates are
  incremental after initialization;
- changed dependency hashing can read a whole large file into a file-sized
  buffer;
- TaskSystem concurrency is primarily task-count based, not byte-admission
  based;
- bitmap import/compression remains expensive even with AssetHost explicitly
  selecting one active platform; current evidence points at per-mip Float32
  conversion, CPU mip generation, BC7 compression and repeated hardware-device
  setup rather than console-platform duplication.

These are reasons for narrow compatibility/configuration work, not reasons to
reimplement RED's asset authority.

Before real-project cutover, measure at least:

- cold and warm AssetHost startup;
- full source DB enumeration and warm watcher update;
- small texture import;
- 2048/4096 texture compression;
- full scratch copy of `lieutenantHead.glb`;
- one embedded texture changed;
- no-semantic-change model reimport;
- no-op recook;
- dependency-driven downstream recook;
- output-topology shrink and cleanup;
- exact/latest cache behavior used by the selected RED flow;
- restart after deleting rebuildable derived DB/cache state;
- peak memory for one or several large changed sources.

There must be no permanent path where Vanguard and RED both scan, hash, decide
freshness, build and publish the same migrated resource.

## Migration plan

### Stage 0 - durability and provenance

1. retain exact RED source images and provenance records;
2. implement and crash-test the durable single-resource writer — **complete**;
3. implement and crash-test compound resource cohort commit/recovery — **complete**;
4. keep the production Vanguard asset path unchanged.

### Stage 1 - standalone RED island

Build a scratch-only `VanguardAssetHost` that can:

1. initialize the retained RED resource/reflection/filesystem stack;
2. mount a scratch `ResourceEditorDepot`;
3. initialize resource loader/database and backendData source/cooked state;
4. run a real RED cooker/generator through `TaskSystem`;
5. produce cooked output;
6. shut down cleanly;
7. restart and reuse persisted derived state;
8. demonstrate no-op incremental behavior.

No Editor integration is required for this stage.

Stage 1 is split into bounded integration gates:

- **Stage 1.1 — source resource database lifecycle: complete.** Real retained
  `resdb::Database`, watcher installation/removal, crash-recovery-before-scan,
  durable committed-path enqueue, synchronous post-commit convergence,
  recursive-cache invalidation and TaskSystem admission ordering are proved on
  scratch data in Development and Debug.
- **Stage 1.2 — real source loader/SystemInstance lifecycle: complete.** The
  disposable `RED Asset Test` project runs the real source
  `ResourceLoader`/`GResourceLoader`, SystemInstance-owned source database and
  caches, cooked depot/loader/dependency database, real `SystemContext` and
  `TaskSystem`. Development proves first cook plus process-restart no-recook;
  Debug proves the persisted lifecycle/reuse path, and all process globals are
  cleared before base-service teardown.
- **Stage 1.3 — first real bitmap-texture domain: complete.** The disposable
  project imports PNG through the real RED bitmap importer, authors canonical
  `.xbm` with import metadata, uses the historical hardware BC7 path, converges
  the authoritative source database, cooks through `TaskSystem`, and proves
  process-restart no-recook plus changed-source incremental recook in Development
  and Debug. The retained build remains narrow and does not activate
  `backendResources`, mesh, world, entity or historical renderer/scene domains.
- Later Stage 1 slices must replace or expand the Stage 1.1 fail-closed scanner
  seam with the authoritative RED scanner implementations required by each
  admitted resource domain before any real project asset is allowed through it.

### Stage 2 - shadow real domains

Run RED against scratch/mirrored texture/material/model data. Compare source
inventory, import metadata, dependency discovery, cooked output, restart
behavior, incremental recooking, cleanup, memory and wall time against the
current production path.

Project Cardinal and irreplaceable source assets must not be mutated during
this phase.

#### Stage 2.1 — bitmap-texture source shadow: complete 2026-10-13

The first shadow slice keeps RED authoritative while adding only the missing
external-source notification boundary. `VanguardAssetHost --stage2-texture-shadow`
reuses Vanguard's `PlatformDirectoryObserver` as a lossy hint source for the
project-local PNG root, then reverse-maps those hints exclusively through
persisted `ResourceMetadata` / `ResourceImportMetadata::GetAssetPath()`. The
observer does not hash sources or decide cooked freshness. A pending binding is
reimported with the real RED importer, restoring the saved same-class import
options before `SetResourcePath()` / `SetSourcePath()`, the canonical `.xbm` is
durably saved, the RED source database is converged, and `TaskSystem` alone
decides whether recooking is required.

The disposable corpus contains the existing 720x720 Brick fixture plus new
project-local 2048 color, 4096 color and 2048 `_n` normal fixtures. A cold run
imported/cooked the three new resources in 143,099 ms; a warm run imported zero
resources and reached the observer with zero seed work. Canonical RED load
evidence reports the color textures as `TEXG_Generic_Color` with
`TCM_QualityColor` and the normal fixture as `TEXG_Generic_Normal` with
`TCM_Normalmap`, which maps to BC5, with gamma disabled for the normal texture.

Controlled source edits prove the non-authoritative observer boundary on large
inputs. A 2048 edit produced one pending binding, one successful real reimport,
hardware BC7 evidence and a nonzero RED build reason in roughly 11.5 seconds
from the edit to completion. A 4096 edit produced the same correctness result
in roughly 110 seconds. During the 4096 path the process reached
2,146,172,928 bytes peak working set (about 2.00 GiB) and 2,631,737,344 bytes
private memory (about 2.45 GiB).

A follow-up active-platform audit corrected the initial interpretation of that
cost. `VanguardAssetHost` already defines `NO_CONSOLE_SDK`, so the historical
bitmap cooker was compiling only one PC render blob despite the importer setting
all four legacy platform flags; `cookWINDOWS_SERVER` aliases the same PC entry in
that cooker. AssetHost now makes that constraint explicit rather than relying on
the compile-time console exclusion: its texture stages configure the bitmap
import cooker from the actual `data::Context::TargetPlatform()`, the host-only
selection fails closed before a context supplies a target, and non-AssetHost RED
builds retain their historical multi-platform behavior.

Instrumented semantic edits after that hardening show where the remaining cost
actually sits. The 2048 color edit spent 17,910 ms in RED reimport, 1 ms in
source-database convergence and 1,985 ms in `TaskSystem`. The 4096 color edit
spent 110,140 ms in RED reimport, 1 ms in database convergence and 14,353 ms in
`TaskSystem`. Both runs observed hardware BC7, produced nonzero RED build
reasons and changed the canonical/cooked outputs. The 4096 cliff is therefore
already a single-platform importer/compression problem, not wasted console
platform work. Exact-current source inspection makes per-mip Float32 conversion,
CPU mip generation and BC7 compression prime profiling targets; the retained
hardware BC7 helper also creates/releases a D3D11 device/context for each mip.
Those internals require separate performance work and must not be weakened by
changing RED texture quality or freshness semantics merely to improve the
benchmark.

No-semantic-change behavior is also proved on the 2048 fixture: rewriting the
exact same PNG bytes still triggers the real importer and hardware BC7 path,
but RED returns zero build reasons. The canonical `.xbm` bytes remain identical
(its durable-save timestamp may advance) and the cooked hash and timestamp stay
unchanged. Development and Debug both reproduce the live observer/reimport path;
the immediately repeated Debug same-bytes pass is stable at zero build reasons.

Source removal/rename is deliberately not interpreted as ownership transfer.
RED exposes no authoritative rule that a missing external PNG deletes its
canonical `.xbm` or that an unpaired rename retargets import metadata. A
reversible removal/rename proof therefore reports the bound resource as an
orphan, performs no reimport/build, retains the canonical and cooked bytes, and
surfaces the condition for authoring policy to resolve. Texture output topology
is one canonical/one cooked output here, so there is no supported source-delete
cleanup to perform at this boundary.

The current Stage 2.1 command remains a bounded acceptance harness rather than
the final long-lived service loop: reverse bindings use a 64-bit mask and reject
more than 64 entries, and the one-shot observer stops after the first pending or
overflow hint. The proof also collapses event-kind detail after routing and
classifies a bound path that is missing at evaluation time as an orphan. A
production controller must use an unbounded/coalescing index, stay resident or
drain to quiescence, and preserve event kind for diagnostics. Those limits do
not weaken the authority proof: source hints select RED resources to evaluate,
while RED metadata, database, dependency hashes and `TaskSystem` remain the
semantic owners.

#### Stage 2.2 — material dependency shadow: acceptance complete 2026-10-13

The material shadow activates only the retained resource/cooker/RTTI closure
needed for a headless `CMaterialTemplate` plus `CMaterialInstance`. The
disposable fixture is `materials/stage22_probe.mt` and
`materials/stage22_probe.mi`; its single pixel-shader `ProbeTexture` parameter
references one of the already-proved `.xbm` resources. A base-less `.mi` is not
used because RED material post-load/save semantics require a real definition for
faithful parameter retention.

`VanguardAssetHost --stage2-material-shadow` converges the fixture to
`textures/brick.xbm`. `--stage2-material-shadow-edit` performs the controlled
semantic mutation by converging the same parameter to
`textures/color2048.xbm` through `CMaterialInstance::WriteParameter()` and the
durable RED resource save path. The command then builds only the `.mi` root.
RED's database/scanner discovers the `.mt` and `.xbm` dependencies, so the
observed three-task graph is not a Vanguard-authored dependency list.

The first Development run produced build reasons `.mi=0x00001002`,
`.mt=0x00001000`, `.xbm=0` and both cooked material files. A fresh-process
restart returned zero for all three reasons. Switching the texture parameter
through RED APIs rebuilt only the `.mi` with reason `0x00000003`, and the next
unchanged process again returned all-zero reasons. RED defines that `0x3` as
`SOURCE_DEPS_CHANGED | BUILT_DEPS_CHANGED`; the harness never forces the material
task.

The downstream-dependency proof uses the separate Stage 2.1 source-shadow path
rather than a material-side shortcut. While its non-authoritative observer was
active, one pixel of only the disposable project-local `Color2048.png` was
changed. Stage 2.1 reported exactly one pending binding, one real RED reimport,
one admitted texture and one nonzero texture build reason. On the following
process, building only the `.mi` root returned material reason `0x3` while the
texture task itself was already current at zero. RED metadata, dependency hashes
and `TaskSystem` therefore own both the upstream texture freshness decision and
the downstream material reevaluation.

The headless material subset deliberately fails closed on specialized resource
parameters for skin, hair, foliage, gradients, multilayer setup/mask and terrain.
Those classes are neither created nor registered as material parameter resources
in `VANGUARD_ASSET_HOST`; ordinary texture/scalar/vector/color/name parameters
remain admitted. Renderer resource creation/upload is disabled in the host and
runtime material compilation remains excluded by `NO_RUNTIME_MATERIAL_COMPILATION`.
This keeps authored/cooked material semantics without activating the historical
renderer, render scene, `backendResources`, mesh, world/entity, backend-world or
broad editor closures.

`MaterialCooker` directly handles the `.mi` and retains historical
`CollectMergeableDependencies()` behavior. The `.mt` task is not a
`MaterialCooker` root; backendData cooks it through the normal no-`ICooker`
source-to-cooked fallback. The fixture intentionally has no shader techniques,
so Stage 2.2 proves serialization, dependency and cooker authority rather than
claiming renderer-ready visual material compilation.

Development and Debug `VanguardAssetHost` builds are clean at zero warnings and
zero errors. Both deterministic fixture modes stabilize to zero build reasons on
unchanged reruns; the settled Debug material run is also all-zero. Stage 1.1,
Stage 1.2 and Stage 1.3 regressions remain green, the generated AssetHost project
still contains no forbidden broad-domain source activation, and the Stage 2.2
wrong-project guard returns code 6 before runtime/project-local initialization.

#### Stage 2.3 — native RED FBX mesh shadow: acceptance complete 2026-10-13

The first mesh gate deliberately exercises RED's own FBX importer before any GLB
translation work. The user fixture
`C:\Users\Ark\Desktop\Model 2\lieutenantHead.fbx` is never edited in place;
the host seeds a disposable copy under
`SourceAssets/REDAssetHost/models/lieutenantHead.fbx`. The final Desktop and
project-local SHA-256 values are both
`43eaa9f102ba2ae984321921c52916cfdd4d061924fa3df10d0e5c1018a41472`.

`VanguardAssetHost --stage2-mesh-shadow` resolves `CMesh` by the canonical
`.w2mesh` extension, finds the registered `CFBXMeshImporter` for `fbx`,
creates its real `FBXMeshImportOptions`, invokes `IImporter::DoImport`, saves
the resulting `CMesh` durably with RED import metadata, updates the RED source
database, and admits only that mesh root to `TaskSystem`. The matching
`MeshCooker` reports historical version 23. Dependencies are not supplied by
the harness.

The FBX importer's historical fallback to
`engine\materials\metal_base.remt` remains intact. The disposable authored
depot provides a minimal valid `CMaterialTemplate` at that exact path, so the
proof does not require a broad engine content depot or a host-specific material
substitution rule. The native importer produces one authored `CMesh`; it does
not create a separate material/texture authored cohort. Therefore the Gate 0
compound publication rule is not exercised by this native FBX path. A future
GLB-to-RED importer/coordinator may still need compound publication when one GLB
creates mesh/material/texture resources together.

The first successful import yielded one LOD, five chunks, five materials, 30
bones, one appearance and a real CPU render-resource blob. Its material names are
`Lieutenant_Head.002`, `body.001`, `Lieutenant_Jacket.001`,
`Lieutenant_Head.003`, and `lens.001`. RED discovered two tasks from the mesh
root: the mesh first cooked with reason `0x00001002` and the fallback material
with `0x00001000`. A fresh process immediately returned zero for both and left
the cooked mesh timestamp unchanged.

The controlled semantic-reimport mode
`--stage2-mesh-shadow-skinned` changes only the RTTI-exposed
`importAsSkinnedMesh` field in the persisted native FBX import options. It does
not rewrite the FBX. The real importer then produced five skinned chunks and 13
bones; RED reported mesh reason `0x00000001` while the already-current material
remained zero. Repeating skinned mode returned all-zero reasons. Returning to the
canonical static mode caused one more native reimport and mesh reason
`0x00000001`; the next canonical process again returned all zero. This proves
that the importer options live in RED metadata and that RED, not a Vanguard
force-build rule, owns the resulting recook.

The host retains the ordinary FBX scene conversion, `mesh::DataBuilder`,
vertex/index packing, meshoptimizer, CPU `RenderMeshCompiler` blob creation,
recursive dependency collection, inplace-resource handling and
`cooker::SaveResource`. Under `VANGUARD_ASSET_HOST`, GPU upload/debug-render
paths and unused FBX export code are absent, while physics, garment, destruction,
morph-target, SpeedTree-wind and material-layer reimport specializations fail
closed. The mesh cooker first validates every serialized export class against
the host's selected RTTI closure, so an omitted specialized mesh parameter is
rejected instead of being silently skipped by RED's normal loader behavior.
Road-proxy material dependency/conversion handling is excluded and road-proxy
meshes are rejected explicitly. `NO_EDITOR` and
`NO_RUNTIME_MATERIAL_COMPILATION` remain enabled.

The generated project contains 11 selected `resourceMesh`, seven
`backendMesh`, three `backendResources`, three `renderBackend`, 17
`renderData`, and one standalone engine mesh-buffer helper translation unit.
It contains no backend-world, world, world-entity, physics, garment,
backend-visibility, backend-REA, historical renderer or render-scene source
activation. Development and Debug builds pass; settled Debug Stage 2.3 returns
all-zero reasons. Stage 1.1, Stage 1.2 and Stage 1.3 remain green, the Stage 2.1
observer proof passes with a reversible disposable-source mutation/restoration,
Stage 2.2 remains all-zero, and both Stage 2.3 wrong-project modes return code 6
before runtime/project-local mutation.

### Stage 3 - domain source cutover

Move texture, material and model authoring to canonical RED resources once Gate
0 and the shadow gates are green. The old Vanguard authority stays available
only as migration/fallback scaffolding while the cutover is proven.

### Stage 4 - sole RED cook/freshness authority

For migrated domains, backendData becomes the only dependency/freshness/recook
authority. Disable duplicate Vanguard scan/hash/build/publication work for those
domains.

### Stage 5 - runtime/export convergence

Use the derived runtime bridge or, if separately approved, consume RED cooked
serialization directly. Keep packaging/runtime semantics independent of
authoring-system implementation.

### Stage 6 - remove parallel Vanguard authority

Delete or collapse the migrated portions of VMETA/VADI/VDDC/BuildGraph/
IncrementalRecooker/ProjectAssetPipeline after data-safety, restart and
performance parity are established.

## Correctness invariants

The following are mandatory throughout migration:

1. A failed save never destroys the last complete authored resource.
2. A compound import never becomes an accepted mixed old/new authored cohort.
3. Deleting rebuildable RED DB/cache/cooked state never deletes authored work.
4. RED `Task`/`TaskSystem` remains the sole freshness and recook authority after
   a domain cutover.
5. Vanguard may bound admission or isolate process failures without duplicating
   RED dependency/build semantics.
6. The first migration does not require changing runtime `ResourceId` identity.
7. A runtime export bridge is derived state and never becomes source truth.
8. Imported RED behavioral changes remain minimal, documented and reviewable.
9. Editor and headless tooling invoke the same AssetHost/backend semantics.
10. No migration experiment writes Project Cardinal or another irreplaceable
    project until scratch/mirror crash and restart gates pass.

## Validation strategy

### Upstream fidelity

- provenance identifies the exact RED source image;
- retained code is used for semantic behavior rather than rewritten clones;
- local patches are reviewable and documented;
- narrow build exclusions are explicit rather than silently replacing modules.

### Data safety

- inject failure/crash before, during and after every persistence phase;
- prove old-or-new complete single-resource visibility;
- prove coherent compound-cohort recovery;
- prove derived DB/cache deletion and rebuild from authored RED resources;
- prove repeated restart/recovery is idempotent.

### Daily performance

- record cold/warm startup and source scan separately from cook work;
- record no-op and one-change incremental behavior;
- record peak resident/source buffers as well as wall time;
- compare real texture/model workloads in scratch/mirror mode;
- reject permanent duplicate RED+Vanguard work.

## Review rule for future changes

When new asset logic is proposed for a responsibility RED already owns, the
default review question is:

> Why is this behavior being reimplemented instead of retaining or adapting the
> authoritative RED implementation?

New Vanguard logic is justified only for a real host boundary, missing format
frontend such as GLB, runtime/export concern, durability primitive beneath RED,
or a measured incompatibility that cannot be configured or narrowly patched.
That exception must be documented.

The burden of proof is on divergence, not on adoption.
