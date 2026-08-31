# Asset build graph

The asset build graph turns a root `BuildRequest` into a transitive set of
deduplicated cooking operations. Dependency discovery is synchronous and
bounded; compilation, DDC access, and fan-in execute asynchronously on
Vanguard Jobs.

## RED-derived design

The implementation was adapted from RED `backendData`:

- `backendData/include/dataTask.h`
- `backendData/include/dataTaskSystem.h`
- `backendData/src/dataTask.cpp`
- `backendData/src/dataTaskSystem.cpp`

The retained architecture has two explicit phases:

1. Resolve the complete generated-resource dependency graph, share repeated
   tasks, and reject cycles.
2. Dispatch one Jobs task per operation, with each task depending on the
   completion counters of its generated dependencies.

Dependency completion now releases a short admission task rather than the
compiler task directly. Admission reserves the compiler's declared byte cost,
dispatches work when it fits, or records the operation in a priority/FIFO ready
queue and returns. It never blocks a Jobs worker while waiting for memory.

This retains RED's important behavior without importing RED resource paths,
dependency databases, cache records, archive formats, or public names.
Vanguard uses its own build requests, SHA-256 identities, memory pools,
containers, Jobs adapter, and result types.

## Example

```text
             shared texture
                /      \
          material A  material B
                \      /
                world cell
```

The shared texture is resolved and compiled once. Both materials wait on its
completion counter. The world cell waits on both material counters. No worker
blocks: Jobs releases each operation only when its dependencies finish.

If the texture fails and is required, both materials finish with
`DependencyFailed`, and the world cell fails without invoking its compiler.
If a dependency is optional, its failure does not prevent the dependant from
running.

## Public contract

- `BuildSystem::Prepare` selects the compiler and discovers/sorts dependencies.
- A compiler intended for `BuildGraph` execution provides a
  `BuildResourceEstimate`; direct synchronous `BuildSystem` use may omit it,
  but the graph rejects an unknown estimate.
- `BuildSystem::Execute` fingerprints, checks the DDC, compiles on a miss, and
  publishes the result.
- `BuildGraph::Request` reserves queued-request bytes before copying source,
  metadata, and settings, then owns those copies until the operation becomes
  terminal.
- Equal request identity, content, metadata, settings, output, and target
  coalesce onto one operation while that operation is reusable.
- `GraphRequest` is a move-only interest in a shared operation. It exposes
  status, failure, waiting, result copying, and explicit cancellation.
- Dropping the final external/dependency interest requests cancellation.
  Running compiler callbacks observe it only through
  `CompileContext::IsCancellationRequested`; the graph does not silently
  repair compiler behavior.
- `Shutdown` explicitly refuses while requests or operations are live.

The resolver callback is invoked only for dependencies marked `Generated`.
It maps a cooked resource identity to a source-side `BuildRequest`; its output
identity must exactly match the discovered dependency. Source, tool, and
already-fingerprinted dependencies remain inputs to the local build plan and
do not become graph nodes.

## Bounds and lifetime

Known operations, generated edges per operation, active execution bytes, and
queued request-copy bytes have explicit limits. Active execution reserves:

```text
max(compilerTransientBytes + artifactBytes, 3 * artifactBytes)
```

This covers the current compiler/artifact and post-compile copy overlap. All
arithmetic is checked. An operation larger than the entire active-byte budget
fails with `LimitExceeded`; it never executes outside the cap.

Operations remain owned by the graph until shutdown so request handles and
Jobs dependency counters never reference reclaimed memory. Heavy payloads do
not: terminal operations release copied request bytes, dependency-only outputs
are released after publication, and externally requested root output remains
only until its final `GraphRequest` is released. `BuildSystem`, Vanguard Jobs,
and resolver state must outlive the graph.

`ArtifactWriter` exposes read-only maximum and remaining artifact/byte capacity
to compilers. Its sticky error is authoritative: a failed writer returns its
specific result before cancellation or generic `CompileFailed` classification.

Compiler descriptors remain registered until every graph operation is
terminal. Resolver and compiler callbacks execute without the graph's main
state lock held. Graph initialization and shutdown are lifecycle operations,
not concurrent request operations.

## Validation

`assetGraphTests` proves:

- diamond dependency deduplication and execution order;
- coalesced root requests;
- required-dependency failure propagation;
- synchronous cycle rejection;
- cancellation propagation into a running compiler;
- nonblocking byte admission with priority/FIFO waiters;
- rejection of oversized and unestimated graph operations;
- queued-request byte admission before ownership copies;
- writer-capacity reporting and specific sticky failures;
- terminal request/dependency/root-output payload release;
- operation/request telemetry; and
- refusal to shut down with live handles.

The graph intentionally stops at cooked `BuildOutput`. Incremental orchestration is supplied above it by
[`IncrementalRecooker`](incremental-recooking.md); VPAK selection and editor notification remain higher-level consumers.

Persistent dependency indexing is now supplied by `DependencyIndex`. When one
is attached, a graph operation publishes its resolved plan and output before
reporting success. Index persistence remains an explicit batch transaction owned by the orchestration layer.
