# Asset cooking

Vanguard's asset pipeline is a headless build system shared by command-line
tools and the editor. Runtime loading does not depend on authoring code.

## Cooking-core flow

```text
SourceAsset + BuildRequest
          |
          v
compiler lookup by source/output type
          |
          v
lightweight dependency discovery
          |
          v
sorted dependency identities and content digests
          |
          v
SHA-256 prerequisite fingerprint
          |
     +----+----+
     |         |
 exact hit   cache miss
     |         |
     |       compile
     |         |
     |       validate explicit primary artifact
     |         |
     |       exact cache store
     +----+----+
          |
          v
multi-output, multi-segment cooked artifacts
```

The build fingerprint contains the source identity and type, requested output
type, compiler identity and version, target platform, source content digest,
source metadata digest, build-settings digest, and sorted dependency identity,
role, requirement, and content digest. Hash encoding is explicitly
little-endian and does not hash C++ object padding.

This follows RED `backendData`'s separation between lightweight dependency
collection and expensive generation/cooking. It also retains RED's tool
version, target platform, prerequisite hash, explicit outputs, exact cache
lookup, and rebuild-on-input-change principles. Vanguard uses its own resource
identities, contracts, SHA-256 fingerprints, memory pools, and containers.

## Ownership and threading

- The build system copies registered compiler descriptors.
- Compiler callback state remains caller-owned and must outlive registration.
- Dependency and artifact byte storage is copied into Vanguard-owned arrays.
- Registry, cache, and telemetry state are synchronized.
- Compiler callbacks execute without an internal build-system lock held.
- A compiler cannot be unregistered while one of its builds is active.
- Cache limits are explicit; eviction is deterministic FIFO.

## Deliberate current boundary

The in-memory cache proves process-local keys, invalidation, copying, limits,
and eviction. The persistent local DDC uses exact SHA-256 filenames, versioned
Vanguard records, read-back validation, atomic immutable publication,
corruption rejection, and abandoned-temporary recovery. It is suitable for
reuse across tool and editor processes but is not a distributed cache.

Concrete formats must not be smuggled into the generic cooking core. Materials
remain shader-layout-derived, and runtime artifacts may be emitted directly in
GPU-consumable target layouts by their registered compiler.

## Asynchronous graph

The cooking core now exposes separate `Prepare` and `Execute` stages. The
RED-derived asset build graph recursively prepares generated dependencies,
deduplicates shared operations, rejects cycles, then dispatches compilation
over Vanguard Jobs. Parent jobs depend on child completion counters, providing
non-blocking fan-in and deterministic required-dependency failure propagation.

Request handles expose explicit terminal state, build error, waiting, output
copy, and cancellation. Cancellation propagates only when an operation has no
remaining external or dependency interest. Running compilers observe it
through `CompileContext::IsCancellationRequested`.

See [`asset-build-graph.md`](asset-build-graph.md) for the full contract and
RED provenance.

The persistent `VADI` dependency index now records successful graph results,
survives editor restarts, classifies exact dirty reasons, and provides forward,
reverse, and transitive affected-resource queries. See
[`asset-dependency-index.md`](asset-dependency-index.md).

The incremental recooker now consumes source-change batches, suppresses
unchanged events, expands dirty outputs through the reverse index, submits only
non-overlapping roots to the asynchronous graph, and commits all resulting
index publications as one atomic transaction. Failure and cancellation roll
the complete batch back. See
[`incremental-recooking.md`](incremental-recooking.md).

The following phases remain:

1. editor build progress, error, notification, and hot-reload integration;
2. persistent-cache disk budgets and optional shared/distributed transports;
3. advanced package partitioning, patch generation, signing, and encryption;
4. concrete shader, texture, mesh, material, world, and audio compilers.
