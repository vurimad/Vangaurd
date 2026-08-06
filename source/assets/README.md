# Vanguard Assets

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

## RED-derived architecture

The implementation was designed after studying `backendData`'s generator,
cooker, dependency, output collector, data cache, task, and task-system
implementation.
Retained principles include dependency discovery before expensive work,
content-based prerequisite hashes, tool-version invalidation, target-platform
identity, explicit output lists, exact cache lookup, and generation only after
dependencies have been resolved.

Vanguard uses explicit registration instead of RTTI class enumeration, SHA-256
instead of RED's SHA-1, Vanguard resource identities, and Vanguard memory and
container services.

The persistent cache preserves RED's split between exact prerequisite lookup
and content retrieval, plus its fan-out loose-file organization and lazy
loading. It deliberately does not use RED dependency database, resource, or
archive formats. The record wire contract is documented in
`docs/formats/vddc-format.md`.

The asynchronous graph preserves RED `backendData`'s separate dependency and
execution phases, one task per generated resource, shared dependency tasks,
cycle failure, Jobs counters for fan-in, and terminal failure propagation.
Vanguard owns the public API, identities, memory, containers, Jobs boundary,
and cancellation contract. See `docs/architecture/asset-build-graph.md`.

The persistent index adapts RED `dependency::ResourceInfo` and
`BinaryDatabase` behavior while replacing `.binary_deps` with Vanguard's
integrity-protected `VADI` contract. See
`docs/architecture/asset-dependency-index.md`.

The incremental recooker adapts RED `dataTask` and `dataTaskSystem` phase
transitions into Vanguard change planning, Jobs graph submission, and atomic
index transactions. See `docs/architecture/incremental-recooking.md`.

The packaging layer adapts RED archive content selection and ordered archive
building while emitting only Vanguard `VPMF` and VPAK contracts. See
`docs/architecture/package-assembly.md`.
