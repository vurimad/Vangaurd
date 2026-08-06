# World

The `world` module owns Vanguard's deterministic `.vworld` global manifest and `.vcell` streamed-world resources. A world indexes spatial hierarchy and distant representations; a cell stores compiled entity placements and instance state. Reusable component defaults remain in `.vprefab`.

## vworld

`.vworld` is the lightweight resource opened before spatial streaming begins. It contains double-precision global bounds, stable cell identities, typed `.vcell` references, coarse-to-fine containment, activation and retention distances, query reference points, categories, flags and priorities. Level zero is the finest cell level; larger levels are progressively coarser. Parent bounds must contain child bounds and a parent level must be greater than its child's level.

Distant proxy records provide long-range representations without materializing entities. Each proxy references a `.vmesh`; the mesh supplies its material dependencies. A proxy carries streaming, pre-boost and secondary query points, streaming and near-hide distances, may be nested beneath a coarser proxy, and names the cells or finer proxies that replace it. Required children gate replacement: the runtime must keep the far proxy visible and resident until every required child reports render-ready, not merely file-loaded. `ProxyOnly` representations intentionally have no replacement requirement.

Cell and proxy resources are emitted as soft dependencies. This lets VPAK preserve package affinity and dependency metadata without causing the resource registry to load the entire world when `.vworld` opens.

## Runtime streaming grid

`WorldStreamingGrid` compiles immutable world records into structure-of-arrays distance queries and persistent bit masks. Predicted observer positions feed the main and retention queries; the render camera feeds secondary-reference and near-auto-hide queries. The selector combines those masks with explicit residency locks, schedules stream-ins by descending priority then ascending distance, and emits stream-out commands after hysteresis or near replacement takes effect.

Commands are an explicit integration boundary. The caller submits resource work and must report completion through `NotifyStreamInComplete` or `NotifyStreamOutComplete`; the grid never pretends that a request completed. A distant proxy remains anti-streaming locked until every required child is render-ready. Nested proxy readiness is evaluated recursively so a coarse representation does not return merely because an intermediate proxy was replaced by its own detailed children.

The opened `WorldFile` must outlive its grid. Shutdown refuses while any node is loaded or in flight, making incorrect lifetime sequencing visible to the caller.

## Streaming execution

`WorldStreamingExecutor` connects the grid to the shared ResourcePipeline, whose dependency graph runs on Vanguard Jobs. It maps world priority tiers onto resource load priorities, submits requests without blocking the update thread, inherits pipeline request coalescing and dependency fan-in, retains generational resource handles, and polls completion on later updates.

Resource availability does not imply world activation or renderer readiness. The executor reports `ResourceAvailable`, after which the Flecs materializer or render-proxy consumer explicitly calls `SetReady`. Stream-out reports `ReleaseRequested` and preserves the strong resource handle until the downstream consumer calls `CompleteRelease`. An in-flight request that leaves range is explicitly cancelled. `FailResident` covers the distinct case where loading succeeded but downstream validation or attachment failed; it releases residency and preserves a diagnostic failure without advertising readiness.

`WorldStreamingGrid::RequestShutdown` is the explicit detach boundary. It suppresses new stream-ins and selects every resident node for stream-out, including normally locked or always-resident nodes. The executor and downstream consumers must still complete their ordinary cancellation and release handshakes; shutdown never discards live handles implicitly.

The grid, pipeline and Jobs runtime must outlive the executor. Executor shutdown refuses requesting, resident, failed or release-pending nodes.

## vcell

The cell uses a double-precision world origin and single-precision local placement transforms. This preserves large-world precision while keeping every streamed placement compact and directly usable. Activation groups are stored as contiguous placement ranges, and a separate sorted entity lookup provides stable-ID access without tying serialization order to runtime lookup order.

Each placement contains its prefab reference, hierarchy parent, transform, bounds, streaming reference point and distances, layer mask, visibility mask, priority and lifecycle flags. Sparse instance overrides support replacing, adding or removing a prefab component. Add/replace values are serialized through Vanguard schemas and carry independent SHA-256 fingerprints. Named entity-reference slots are resolved after entity creation; local references are validated during cooking while world references may target unloaded cells.

VPAK treats `.vcell` as an opaque resource and preserves its prefab and component-resource dependency table. `CellResource` is the validated loaded representation, and the entity streaming runtime materializes its prefab components into Flecs, applies overrides, resolves reference slots, and acknowledges readiness after the world transaction commits. No Flecs identifier is persisted.
