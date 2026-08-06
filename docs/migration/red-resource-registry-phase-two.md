# RED Resource Registry Adaptation — Phase 2

## RED sources studied

The Phase 2 behavior was derived from REDengine's `redReflection` resource
coordination layer, primarily:

- `redReflection/include/resourceLoader.h`
- `redReflection/include/resourceLoaderTypes.h`
- `redReflection/include/resourceToken.h`
- `redReflection/src/resourceLoader.cpp`
- `redReflection/src/resourceToken.cpp`
- `unitTestsReflection/src/resourceLoaderTests.cpp`
- `unitTestsReflection/src/resourceTokenTests.cpp`

## Behavior retained

- A synchronized path-to-weak-token registry coalesces repeated load requests.
- A request token is shared ownership of one operation and exposes completion,
  failure, path identity, and the loaded object.
- Loaded resources use strong and weak handle semantics.
- Loader work is explicitly scheduled and completion is explicit.
- Cancellation and failure never become successful completion.
- User callbacks are not invoked while the registry lock is held.

## Vanguard contract

Vanguard expresses these behaviors through `ResourceRegistry`,
`ResourceRequest`, `ResourceHandle`, and `WeakResourceHandle`. Registry storage,
request control blocks, and resources allocate from the Resources memory pool.
Synchronization uses Vanguard Concurrency, and maps and arrays use Vanguard
Containers.

Vanguard adds a generation to each registry slot. Eviction changes that
generation, so a weak handle or completed token can never silently bind to a
different incarnation of the same path.

The loader boundary is deliberately explicit:

1. `Request` creates or shares an operation.
2. The registered callback calls `BeginLoading`.
3. The loader calls exactly one of `Publish` or `Fail`, unless the request is
   cancelled.
4. A publication after failure, cancellation, eviction, or replacement is
   rejected; Vanguard does not repair caller misuse.

## RED assumptions rejected

- RED depot paths and depot bootstrap state.
- RED resource headers, reflected package serialization, and cooked formats.
- RED archive IDs and required sidecar files.
- RED public names or `red::` types in Vanguard-facing headers.
- Hidden synchronous file access in the registry.

Vanguard resources retain the engine's CRC-64/XZ path identity shared with
VPAK v1. Storage and decoding remain Vanguard-owned and will attach through
the loader callback without changing this registry contract.

## Deferred to Phase 3

- dependency graph construction and cycle handling;
- Jobs scheduling and priority;
- request dependency fan-in and failure propagation;
- cancellation propagation into queued and running jobs;
- asynchronous I/O and decode staging.

The Phase 2 conformance suite covers concurrent coalescing, explicit
transitions, shared completion, strong and weak handles, deferred eviction,
generation invalidation, cancellation, rejected late publication, retry,
loader removal, statistics, and shutdown ownership checks.
