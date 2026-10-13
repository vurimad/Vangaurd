# Vanguard Assets

`source_database.hpp` provides the headless source inventory table and identity queries. `Rescan` uses checked physical enumeration and existing filesystem readers; failed directory enumeration does not become asset deletion. Source/sidecar pairs and orphan sidecars share one source row. Rebuild/rescan/classification are exclusive owner operations and queries borrow records until the next update. `ClassifySources` consumes existing compiler descriptors without retaining callbacks; optional filename hints identify candidates, not successful imports. `VisitResources` filters borrowed records by folder, types, issues and classification with no I/O or result-array allocation. The editor workspace and nanovanguard source inspection now consume this database; executable validation and full compiler composition remain deferred.

Source metadata: `asset_metadata.hpp` provides the headless VMETA 1 codec, persistent 128-bit source identity and stable named-output mapping to existing runtime resource references. See [VMETA format](../../docs/formats/vmeta-format.md). Project-wide identity checks and safe file publication remain separate integration work.

`assets` owns Vanguard's headless authoring-to-runtime build contracts. It is
a tool-side module; runtime resource loading does not depend on it.

## Current cooking-core contract

- Explicit compiler registration by source and output resource type.
- Stable compiler identity and mandatory tool version.
- Lightweight dependency discovery before compilation.
- Source, generated, and tool dependency roles.
- Required and optional dependency requirements.
- SHA-256 prerequisite fingerprints containing source content, metadata,
  settings, target platform, compiler identity/version, and sorted dependency
  identities/content.
- Multi-output, multi-segment artifact emission with explicit primary output,
  alignment, and runtime/editor flags.
- Bounded in-memory exact derived-data cache with deterministic FIFO eviction.
- Optional persistent exact derived-data cache using RED-style content-addressed
  fan-out directories.
- Versioned Vanguard `VDDC` records with SHA-256 semantic integrity validation.
- Same-directory temporary writes, read-back validation, and atomic immutable
  publication.
- Corrupt-record removal, interrupted-publication recovery, and publication
  race convergence.
- Thread-safe compiler registry, cache, and telemetry. Compiler callbacks run
  outside internal locks.
- RED-style two-stage builds: synchronous dependency planning followed by
  asynchronous execution over Vanguard Jobs.
- Transitive generated-dependency resolution with diamond deduplication,
  priority mapping, dependency fan-in, and cycle rejection.
- Shared move-only request handles with explicit status, failure, wait, output
  copy, and cancellation contracts.
- Required-dependency failure and cancellation propagation without compiling
  invalid dependants.
- Persistent `VADI` dependency records containing source/compiler state,
  resolved dependencies, content fingerprints, and artifact manifests.
- Forward, reverse, and transitive affected-resource queries with exact
  source/compiler/dependency dirty classification.
- Deterministic atomic index publication with SHA-256 integrity, restart
  loading, settings invalidation, and corruption recovery.
- RED-derived incremental recook planning over source-change batches.
- Exact unchanged-event suppression, transitive reverse invalidation, and
  minimal non-overlapping root selection.
- Transactional index staging with committed-reader isolation, atomic batch
  commit, and complete rollback after build failure or cancellation.
- Move-only asynchronous recook handles with polling, waiting, cancellation,
  bounded planning, and lifetime telemetry.
- Versioned, integrity-checked `VPMF` package manifests.
- `VADI`-driven required/optional dependency closure and target-aware,
  policy-driven runtime/editor artifact selection.
- Deterministic package plans and SHA-256-derived VPAK build identities.
- Bounded per-resource artifact acquisition, VPAK assembly, strict read-back
  validation, and atomic publication.

The in-memory backend remains useful for hot process-local reuse. The
persistent backend survives tool/editor restarts and can be shared by
independent local builders. Editor notifications, distributed cache transport,
disk-budget maintenance, and advanced package partitioning are later phases
built on the contracts established here.

## RED authority migration

The current module is production Vanguard code and remains valid until a domain
is cut over. It is no longer the target authority model.

The approved direction is to adopt the actual RED authored-resource and
`backendData` implementation. For migrated domains, RED uncooked resources and
embedded import metadata become source truth, and RED `Task`/`TaskSystem` owns
dependency freshness, cooking and cleanup. `SourceDatabase`, VMETA, VDDC, VADI,
BuildGraph and `IncrementalRecooker` are migration-era authorities that are to
retire after a proven cutover rather than become a permanent shell around RED.

The canonical decision, durability gate and migration constraints are in
[`docs/architecture/red-asset-execution-kernel.md`](../../docs/architecture/red-asset-execution-kernel.md).

Vanguard runtime streaming, VPAK packaging, `.vproject`, telemetry and useful
low-level storage/transaction primitives may remain. A retained primitive must
not become a second freshness or dependency authority beside RED.
