# Asset dependency index

The asset dependency index is the durable build-state layer above the asynchronous cooking graph. It records enough information to
decide whether a cooked output remains current and to discover every output affected by a changed source, tool, or generated resource
after an editor or cooker restart.

## RED-derived architecture

The design was adapted from RED `backendData`:

- `dependencyDatabase.h`
- `dependencyBinaryDatabase.h/.cpp`
- `dependencyTypes.h`
- `dependencyTypesImpl.h/.cpp`
- `dependencyBinaryFileFormat.hpp`
- `dataTask.h`

Vanguard retains RED's central `ResourceInfo` idea: one replaceable record per cooked resource containing source identity, tool identity
and version, target platform, prerequisite state, dependency list, output list, and final content state. It also retains global settings
invalidation, version rejection, changed-state tracking, and explicit save.

Vanguard does not copy RED's `.binary_deps`, RTTI-polymorphic dependency serialization, depot locations, timestamps, SHA-1 hashes, or
unprotected final-file writes. The index uses Vanguard resource references, fixed dependency roles, SHA-256, deterministic ordering,
bounded parsing, payload integrity, same-directory temporary publication, read-back validation, and atomic replacement.

## Record model

Each output record contains:

- source and cooked-output resource references;
- target platform;
- compiler identity and version;
- a source-input fingerprint covering source bytes, metadata, settings, identities, and target;
- final build and content fingerprints;
- sorted dependency identity, role, requirement, and content fingerprint;
- artifact resource, segment, flags, alignment, and byte count.

The output resource path is the primary key. A path cannot silently acquire a different resource type; that is an explicit publication
error.

## Queries and invalidation

Forward dependencies are stored directly in each record. The reverse index is rebuilt lazily from durable records and then reused for
editor queries. A source identity also forms an implicit edge to its cooked output, so changing a source begins invalidation at the
correct graph node.

For this graph:

```text
source texture -> cooked texture -> material -> world cell
```

`CollectAffected(source texture)` returns the cooked texture, material, and world cell. Shared dependencies appear once. Cycles cannot
cause unbounded traversal because affected outputs are visited once.

`Evaluate` distinguishes:

- missing record;
- changed source input;
- changed compiler identity/version;
- changed dependency set or content;
- exact up-to-date state.

Generated dependency content must be materialized in the `BuildPlan` before evaluation, matching the same explicit contract used by the
asynchronous build graph.

## Publication and ownership

A `BuildGraph` can be initialized with an optional `DependencyIndex`. Every successful build or DDC hit publishes its resolved plan and
output before the graph operation becomes successful. Publication failure is visible as `IndexPublicationFailed`; the graph does not
pretend durable build state was updated.

The graph does not save the index after every resource. Tools may publish and explicitly call `Save`, while `IncrementalRecooker` opens
a transaction for an entire change batch. Publications made during a transaction are staged and invisible to committed readers.
`CommitTransaction` publishes one validated atomic index image; `RollbackTransaction` discards every staged replacement. Shutdown
refuses active transactions or unsaved state.

## Persistence

The index is stored as `asset-dependencies.vadi`. Records are sorted by output identity before serialization, independent of parallel
Jobs completion order. The complete payload is protected by SHA-256.

Saving writes `asset-dependencies.vadi.tmp`, flushes it, reads it back, validates the exact bytes, and atomically replaces the active
index using Vanguard Filesystem. Startup removes an abandoned temporary file. Corrupt or globally incompatible indexes are explicitly
reported as `Recovered`, removed, and rebuilt from future successful builds.

The binary contract is documented in [`../formats/vadi-format.md`](../formats/vadi-format.md).

## Current boundary

The index provides durable state, exact dirty classification, forward/reverse queries, transitive affected-set discovery, and atomic
batch transactions. Incremental recook orchestration is supplied by `IncrementalRecooker`; see
[`incremental-recooking.md`](incremental-recooking.md). Packaging selection and editor notification remain consumers above these
headless contracts.
