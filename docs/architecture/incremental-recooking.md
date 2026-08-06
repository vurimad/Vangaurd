# Incremental recooking

The incremental recooker converts editor or watcher change batches into the smallest safe set of asynchronous build-graph requests. It
is the orchestration layer above the persistent dependency index and below packaging and editor notification.

## RED-derived design

The state transitions and invalidation policy were adapted from RED `backendData`:

- `backendData/include/dataTask.h`
- `backendData/src/dataTask.cpp`
- `backendData/include/dataTaskSystem.h`
- `backendData/src/dataTaskSystem.cpp`

RED tasks distinguish stored-dependency lookup, stored-dependency validation, updated-dependency collection, dependency waiting, output
validation, output generation, metadata publication, and cleanup. Vanguard retains that separation through `DependencyIndex::Evaluate`,
`BuildSystem::Prepare`, `BuildGraph::Request`, and the index transaction boundary. Build reasons remain explicit: source input, compiler,
dependency set/content, missing output, or forced upstream work.

Vanguard does not retain RED depot paths, `.binary_deps`, cache formats, task names, or resource serialization. The public contract uses
Vanguard resource identities, SHA-256 fingerprints, Jobs, memory pools, containers, DDC, `VADI`, and move-only request ownership.

## Change planning

For each changed source identity, the recooker finds its directly produced outputs, resolves their current source bytes, metadata,
settings, and target, prepares their dependencies, and compares the resulting state with the committed index.

An unchanged watcher event completes synchronously without opening an index transaction. A dirty direct output expands through the
reverse dependency graph. The affected set is then minimized to outputs that have no dirty dependant:

```text
source texture -> cooked texture -> material -> world cell
```

A texture edit affects all three cooked outputs, but only `world cell` is submitted as a root. The build graph recursively requests and
deduplicates its dependencies. Every affected node is therefore considered exactly once without separately submitting overlapping
roots.

The affected set describes invalidation, not guaranteed compiler invocations. An intermediate rebuild can produce identical content;
the downstream node then remains an exact DDC hit and does not run its compiler.

## Atomic publication

Before roots are issued, the recooker opens a `DependencyIndex` transaction. Successful graph publications are staged and remain
invisible to committed readers. When every root succeeds, one commit serializes and validates the complete new `VADI` image and
atomically replaces the previous index.

Any root failure, cancellation, publication error, or commit error rolls back all staged records. A failed batch cannot leave a mixture
of old and new dependency state. Only one recook batch may own a recooker at a time, making the transaction and editor-visible batch
boundary unambiguous.

## Public contract

- `IncrementalRecooker::Initialize` requires a build graph bound to the same build system and dependency index.
- The resolver maps an indexed cooked output to its current complete `BuildRequest`; the recooker copies the request before returning
  from the callback.
- `Request` copies the change identities, performs bounded planning, and returns a move-only `RecookBatch`.
- `Poll` and `Wait` finalize the index transaction only after every root reaches a terminal state.
- `Cancel` explicitly propagates through the root graph handles.
- Releasing a live batch cancels and waits before destroying its operation.
- Shutdown refuses while a batch handle remains active.
- Batch and lifetime statistics expose input, unchanged, dirty, affected, root, success, failure, and cancellation counts.

## Validation

`assetRecookerTests` proves unchanged-event suppression, transitive affected-set expansion, minimized-root submission, content-identical
downstream DDC reuse, successful atomic commit, failed-build rollback, cancellation rollback, committed-reader isolation, explicit
untracked-change failure, telemetry, and restart persistence.

Package selection and deterministic VPAK assembly are now supplied by the
[`package assembly`](package-assembly.md) layer. Editor progress, diagnostics,
notification, and hot reload consume the same batch state but do not belong
inside the headless recooker.
