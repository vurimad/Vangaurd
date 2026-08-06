# Pipeline Cache

The Pipeline Cache module owns runtime native-pipeline lifecycle. It consumes the concrete SHA-256 keys produced by `vpipeline`, coalesces concurrent requests, and creates backend objects through bounded Vanguard Jobs workers.

## RED lineage

RED's DX12 PSO cache reserves one entry per runtime hash, transitions it through unknown, pending, valid, or invalid states, compiles asynchronously, and invalidates entries when dependent shaders disappear. Vanguard retains those proven semantics while replacing collision-prone 32-bit keys, fixed 8192-entry storage, borrowed async descriptors, and immediate destruction assumptions.

Vanguard adds:

- collision-safe full 256-bit key comparison;
- explicit `Pending`, `Valid`, and `Invalid` request states;
- retained creation payloads with declared ownership callbacks;
- concurrent request coalescing;
- a configurable hard entry budget and bounded creation concurrency;
- generational invalidation, allowing old valid handles to remain usable until their last request releases them;
- structured backend failure evidence;
- warmup requests and editor-readable telemetry;
- refusal to shut down while work or request handles remain.

## NVRHI adapter

NVRHI integration belongs in the renderer, not in this module. The renderer will retain a build payload containing the loaded `nvrhi::IShader` handles, translated pipeline descriptor, input layout, and framebuffer information. Its `Backend::create` callback calls the appropriate NVRHI device function and returns a retained wrapper as `NativePipeline`.

The backend's `destroy` callback releases that wrapper. A `PipelineRequest` exposes the borrowed native object only while the request remains alive. This makes invalidation safe: a new concrete-key generation can be created immediately, while an old NVRHI handle is destroyed only after all users of that generation release their requests.

Neither NVRHI structures nor driver cache blobs enter `vpipeline`, VPAK, or portable fingerprints.

## Warmup and persistence

`Warmup` uses the same request path as gameplay, so precaching cannot bypass coalescing, limits, error handling, or telemetry.

`NativeCacheStore` owns backend-native persistence. It records an opaque driver blob behind an identity containing the backend and cache-schema versions, vendor/device and adapter identities, driver version, backend compatibility digest, and engine-build digest. Records are SHA-256 validated, size bounded, written to a validated temporary file, and atomically replaced with write-through filesystem semantics.

At renderer startup, `Restore` validates the record before passing it to the NVRHI/backend importer. At controlled save points or device shutdown, `CaptureAndPublish` asks the backend to serialize its current native cache and publishes it. Corruption, incompatible identities, abandoned publications, and backend rejection remove the unusable record and leave the renderer free to recreate pipelines normally.
