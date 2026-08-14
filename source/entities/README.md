# Entities

The `entities` module is the transaction boundary between cooked world data and live Flecs storage. It converts stable component schemas, `.vprefab` defaults and `.vcell` placement overrides into a validated batch of ECS operations. Cooked files never contain Flecs entity or component identifiers.

## Materialization flow

1. `ComponentRegistry` validates the exact C++ size/alignment of each reflection schema, maps it to one toggleable component type in one ECS world, and is sealed before decoding starts.
2. `CellMaterializer::QueueCell` selects initially active placements, resolves their prefabs, validates hierarchy and override rules, derives runtime-stable identities, and decodes every selected component into owned staging memory.
3. Only after the whole cell validates are entity creates and component Add/Set/Disable operations queued in one ECS-owned command batch.
4. The owning game world commits its normal entity-first/component-second synchronization point.
5. `CompleteActivation` requires an exact successful batch receipt, verifies every expected entity and component, publishes stable identities to `EntityReferenceRegistry`, and waits for every required reference before changing the cell state to `Active`.
6. Release first prepares the complete destruction command batch, then unregisters stable identities before any entity destruction can become visible. `CompleteRelease` retires the cell record only after no entity remains live.

Cancellation removes only operations owned by that cell's command batch and destroys their staged values. Cancellation after any entity becomes live is converted into the ordinary release transaction. Resource generations are checked at activation, cancellation and release boundaries so stale asynchronous completions cannot publish or remove a newer cell.

## Identity and component contracts

The placement ID is the prefab root entity ID. Child IDs are deterministic hashes of the placement and prefab-local stable entity ID, and every result is collision-checked against both the pending cell and live world. Parent links retain stable IDs rather than transient Flecs handles.

One serialized schema maps to one ordinary Flecs component type, so an entity can contain at most one instance of that schema. Repeated authored data must be represented by an aggregate component schema or child entities, not duplicate component records. Component instance IDs remain useful for sparse prefab overrides, but do not weaken Flecs component cardinality.

`WorldPlacement` retains world translation as double precision by combining the cell origin with the compact local placement. Disabled component values remain installed in toggleable Flecs storage and can be enabled later. Editor-only entity subtrees are excluded transitively, and initial materialization includes only default-active activation groups unless explicitly expanded in `MaterializationConfig`.

## Stable entity references

`EntityReferenceRegistry` is one world-scoped, reader/writer-locked identity service. Serialized references remain stable source ID, named slot ID, target ID and required/optional policy; transient Flecs handles never enter cooked data. Registration resolves existing waiters immediately, and unregistration invalidates incoming links before the target entity is destroyed. Re-publishing the same stable identity resolves those waiters to the new Flecs generation and increments the reference revision.

Required world links participate in cell readiness, while optional links remain observable without blocking activation. Required local links close the initial placement selection transitively, so an active source cannot deadlock while waiting for a target in an otherwise inactive activation group. Gameplay code may wrap named slots in `TypedEntityReference<Tag>` to keep unrelated reference roles distinct at compile time.

The registry partitions each cell into independently owned publication sets. The base partition represents initial materialization; later activation groups publish and withdraw only their own identities, outgoing references, and incoming-target availability. Cross-partition waiters therefore relink without unregistering the rest of the cell.

## Incremental activation groups

`AcquireActivationGroup` creates an explicit `(cell, group, owner)` lease. It computes the transitive closure of required-local references and local placement parents, retains every group in that closure, and queues one exact Flecs command batch for each group that transitions from zero owners to one. Additional gameplay or editor owners coalesce onto the same pending or active group instead of duplicating entities.

`SynchronizeActivationGroups` verifies committed entities and components, publishes the group's reference partition, and independently tracks `PendingActivation`, `PendingReferences`, `Active`, `PendingRelease`, and `Failed`. `ReleaseActivationGroup` removes the matching lease; dependency retain counts are decremented and only groups reaching zero are cancelled or destroyed. The always-active/base selection remains owned by the cell and cannot be removed through a group lease.

## Streaming runtime integration

`CellStreamingSystem` is the cell-side consumer of `WorldStreamingExecutor`. It owns the world-scoped component/reference registries and materializer, converts loaded `CellResource` objects into Flecs command batches, and acknowledges executor readiness only from the post-world-flush epilogue. Multiple cells loaded in one frame receive two activation passes so mutually dependent required references can all publish before readiness is evaluated.

Stream-out follows the reverse order: prepare the complete destruction batch, unregister stable identities, commit Flecs destruction, complete the materializer release, and only then release the executor's strong resource handle. Successfully loaded resources that fail cell identity, schema, prefab or attachment validation are reported through `FailResident` and never become ready. Distant-proxy events are forwarded through the configured callback for the renderer-side consumer rather than being silently consumed.

`MakeCellDecoder` supplies the cross-platform `ResourceStreamer` decoder descriptor. `CellResource` owns validated copied tables, so asynchronous staging memory can be returned immediately after decode.

The decoder also retains strong generational handles for every required `vprefab` dependency used by a placement. Materialization resolves prefab data from those cell-owned handles, so a dependency cannot be evicted between asynchronous cell construction and the later Flecs transaction. The callback resolver remains only as an explicit fallback for tools and isolated callers.

## Current boundary

This layer now also owns `WorldRenderBridge`, the component-neutral adapter from exact committed ECS changes to RenderScene proxies. The ECS journal retains stable entity/component identities and command-batch ownership for independent consumers; repeated writes coalesce by entity/component rather than causing full table scans. Project code registers mesh, light, decal and shared-state translators, while RenderScene remains independent of Flecs and authored component schemas. Activation changes are translated and capacity-checked as one publication envelope, world-session generations invalidate stale work, and proxy detach acknowledgements wait until all published scene readers release the removed generation. Native Flecs observers may append exact dirty identities through the tiny ECS capture boundary, but never perform renderer work in observer callbacks. A cold full-world validator exists for rebuild and diagnostics, not the frame hot path.

Automatic frame ordering between GameWorld flush, bridge capture, RenderScene prepare/commit, collection and retirement belongs to the engine frame-pipeline integration layer. Renderer-side distant-proxy policy and broader resource-readiness aggregation remain above this module.
