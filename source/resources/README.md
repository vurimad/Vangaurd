# Vanguard Resources

Resources are split into phases so each layer has a complete, testable
contract before the next one depends on it.

## Phase 1 — identity and lifecycle contracts

Implemented here:

- compact 64-bit `ResourcePath`, following RED's runtime path architecture;
- one canonical logical-path policy shared by loose files and VPAK;
- stable resource-type identities;
- typed and untyped path references;
- explicit load, reload, eviction, failure, and cancellation states;
- allocation-free and thread-safe path hashing.

Vanguard retains its existing CRC-64/XZ `ResourceId` contract because it is
part of VPAK v1. This deliberately differs from RED's FNV path hash while
preserving the proven compact-path design.

## Phase 2 — registry and shared ownership

Implemented here:

- loader registration by stable `ResourceTypeId`;
- one registry entry per logical resource path;
- one shared `ResourceRequest` control block per active operation;
- request coalescing for queued, loading, and already-loaded resources;
- explicit loader transitions through `BeginLoading`, `Publish`, and `Fail`;
- explicit caller cancellation with late-completion rejection;
- strong loaded-resource handles and lockable weak handles;
- deferred eviction while strong handles exist;
- monotonically changing generations that make old weak handles stale;
- registry counters suitable for diagnostics and editor inspection;
- shutdown refusal while external requests or handles remain alive.

The registry owns coordination and object lifetime, not I/O scheduling. A
registered loader receives the shared request outside the registry lock. It
must explicitly begin the operation and eventually publish a matching
`ResourceObject`, fail it, or observe cancellation and stop. Phase 3 will put
the asynchronous Jobs pipeline behind that unchanged boundary.

```cpp
void BeginMeshLoad(
    resources::ResourceRegistry& registry,
    const resources::ResourceRequest& request,
    void* context) noexcept
{
    if (!registry.BeginLoading(request))
    {
        return;
    }

    // Phase 3 will schedule I/O, dependencies, and decoding here.
}

resources::ResourceRequest request =
    registry.Request(resources::ResourceReference(path, meshType));
request.Wait();
resources::ResourceHandle mesh = request.Acquire();
```

## Phase 3 — asynchronous dependency orchestration

Implemented in `resource_pipeline.hpp`:

- move-only caller requests layered over shared registry operations;
- asynchronous dependency discovery and construction on Vanguard Jobs;
- Jobs counter/deferral completion gates, matching RED token semantics;
- required and optional dependency edges;
- diamond-graph request coalescing and non-blocking dependency fan-in;
- iterative dependency-cycle detection;
- priority promotion propagated through the discovered graph;
- causal failure traces;
- per-caller cancellation and last-interest cancellation propagation;
- cooperative cancellation for a loader already executing;
- automatic release and eviction when graph interest disappears;
- bounded dependency counts and editor-facing pipeline statistics.

`PipelineRequest::Cancel` removes one caller, not the shared operation. An
operation is cancelled only when it has no root callers and no parent
dependency edges. A completed resource remains alive while its request or a
strong `ResourceHandle` needs it.

```cpp
Failure DiscoverMeshDependencies(
    ResourceReference mesh,
    DependencyBuilder& dependencies,
    void* loaderState) noexcept
{
    dependencies.Add(materialReference);
    dependencies.Add(
        editorPreviewReference,
        DependencyRequirement::Optional);
    return Failure::None;
}

ResourceObject* ConstructMesh(
    const LoadContext& context,
    Failure& failure,
    void* loaderState) noexcept
{
    if (context.IsCancellationRequested())
    {
        failure = Failure::Cancelled;
        return nullptr;
    }

    // Required handles are guaranteed valid here. The resulting resource
    // copies any handles it must retain after construction.
    return CreateMesh(context.Dependency(0), failure);
}

PipelineRequest request =
    pipeline.Request(meshReference, LoadPriority::High);
request.Wait();
ResourceHandle mesh = request.Acquire();
```

Priority promotion changes the operation's effective priority immediately and
is inherited by newly discovered dependencies and later stages. A RED Jobs
stage already submitted to a queue is not silently removed and reinserted.

The loader descriptor also supports an optional asynchronous preparation
stage. A successfully started stage must call `PreparationRequest::Complete`
exactly once, including after cancellation. The pipeline fans this completion
into the same Jobs dependency graph, so construction never blocks a worker on
I/O.

## Phase 4 — runtime storage and streaming

Implemented by `source/streaming` behind the Phase 3 loader boundary:

- loose-file and mounted VPAK source resolution with deterministic priority;
- asynchronous segment range reads through Vanguard I/O;
- direct final-buffer reads for raw segments and bounded scratch for LZ4;
- stored-segment and complete-resource CRC validation;
- explicit cancellation propagation to active I/O contexts;
- a hard staging-memory budget using the Streaming memory pool;
- typed decoder registration and editor-facing streaming statistics.

The resource pipeline remains independent of VPAK and filesystem policy.
Storage prepares validated bytes; the registered type decoder constructs the
resource only after required dependencies and asynchronous preparation are
complete.

## Planned phases

1. Identity and lifecycle contracts — complete.
2. Resource registry, loader registration, shared loaded handles, generations,
   request coalescing, and explicit cancellation — complete.
3. Dependency graph, asynchronous Jobs pipelines, failure propagation, and
   loading priorities — complete.
4. Loose-file and VPAK sources, segment streaming, integrity validation, and
   memory budgets — complete.
5. Reflection-driven Vanguard object serialization and resource schemas —
   stable metadata, `VOBJ` primitives, arrays, evolution, dependency
   traversal, async schema decoding, manifest verification, and the concrete
   shader-derived material schema path are established.
6. Editor source assets, hot reload, revision tracking, diagnostics, and
   inspection APIs.

RED's resource loader, depot, token, references, serializers, and metrics
implementation remains compiled inside `redReflectionCompat` as the behavioral
reference. Vanguard does not adopt RED depot paths, resource headers, bootstrap
files, or cooked formats.
