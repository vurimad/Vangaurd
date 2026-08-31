# Entities

The `entities` module is the transaction boundary between cooked world data and live Flecs storage. It converts stable component schemas, `.vprefab` defaults and `.vcell` placement overrides into a validated batch of ECS operations. Cooked files never contain Flecs entity or component identifiers.

## Materialization flow

1. `ComponentRegistry` validates the exact C++ size/alignment of each reflection schema, maps it to one toggleable component type in one ECS world, and is sealed before decoding starts.
2. `CellMaterializer::QueueCell` selects initially active placements, resolves their prefabs, validates hierarchy and override rules, derives runtime-stable identities, and decodes every selected component into owned staging memory.
3. Only after the whole cell validates are entity creates and component Add/Set/Disable operations queued in one ECS-owned command batch.
4. The owning game world commits its normal entity-first/component-second synchronization point.
5. `PrepareActivation` requires an exact successful batch receipt and verifies every expected entity and component. Stable object components cross an entity-assembly barrier: the complete sibling set is created first, the complete set is initialized second, and every object attaches only after initialization succeeds for every sibling. Authored component and owning-entity enabled states are installed before initialization, so disabled objects complete the lifecycle without becoming operational. Any failure destroys the created set in reverse order.
6. After the coordinator has prepared all available cells, `PublishActivation` runs the attach epilogue: stable identities are published to `EntityReferenceRegistry`, and each cell waits for every required reference before changing to `Active`.
7. Release first prepares the complete destruction command batch, then unregisters stable identities before any entity destruction can become visible. `CompleteRelease` retires the cell record only after no entity remains live.

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

`PrepareActivationGroups` verifies committed entities and components and completes attachment for every available group without exposing its identities. A separate coordinator-wide `PublishActivationGroups` epilogue then publishes the prepared reference partitions and updates required-reference readiness. Groups independently track `PendingActivation`, `PendingReferences`, `Active`, `PendingRelease`, and `Failed`. `ReleaseActivationGroup` removes the matching lease; dependency retain counts are decremented and only groups reaching zero are cancelled or destroyed. The always-active/base selection remains owned by the cell and cannot be removed through a group lease.

## Streaming runtime integration

`CellStreamingSystem` is the cell-side consumer of `WorldStreamingExecutor`. It owns the world-scoped component/reference registries and materializer, converts loaded `CellResource` objects into Flecs command batches, and acknowledges executor readiness only from the post-world-flush epilogue. Base cells and incremental groups complete their available attachment work across the coordinator before any corresponding identity publication pass begins. Multiple cells loaded in one frame receive two publication passes so mutually dependent required references can resolve before readiness is evaluated.

Stable object components are created behind an entity-assembly barrier and initialized in parallel. Their initialization context resolves already-created siblings, acquires or requests resources through the process resource system, carries the cell I/O priority, and exposes the Jobs continuation context. The materializer polls one retained completion counter that includes callback child jobs, then performs RED's two serialized sibling-wide lifecycle passes: every component attaches first, and only then does every component receive `PostAttach`. Reference publication cannot begin until both passes finish. Stream-out that races initialization becomes a deferred release and cannot reclaim component or staging storage before that counter completes.

Stable components retain independent component-local and owning-entity enabled inputs. `IsEnabled()` is their effective conjunction. Like RED's `IComponent::Enable`, a component transition notifies `OnEnabled` only after attachment and never substitutes detach/reattach for enablement. Entity transitions update the entire sibling set before callbacks run; enable callbacks follow attachment order and disable callbacks run in reverse. `ComponentRuntime` consumes committed `DisabledEntity` marker changes after each world flush, applies the corresponding Flecs entity state so ordinary value components stop matching systems, and then updates stable components. Direct stable-component transitions go through `ComponentDirectory::SetComponentEnabled`.

Before initialization begins, every stable object exposing the placed-component capability is registered with `TransformRuntime` and hard-attached to its entity's stable placeholder root. This mirrors RED's floating-component binding stage: initialization can resolve the stable transform relationship, while transform propagation remains in the transform CPU stage and reference publication remains behind the later attach epilogue. The materializer retains each binding handle. Reverse teardown invokes component detach and uninitialization while that relationship is still valid, then removes the transform binding before destroying component storage; rollback follows the same reverse ownership order.

Stream-out follows the reverse order: prepare the complete destruction batch, unregister stable identities, commit Flecs destruction, complete the materializer release, and only then release the executor's strong resource handle. Successfully loaded resources that fail cell identity, schema, prefab or attachment validation are reported through `FailResident` and never become ready. Distant-proxy events are forwarded through the configured callback for the renderer-side consumer rather than being silently consumed.

`MakeCellDecoder` supplies the cross-platform `ResourceStreamer` decoder descriptor. `CellResource` owns validated copied tables, so asynchronous staging memory can be returned immediately after decode.

The decoder also retains strong generational handles for every required `vprefab` dependency used by a placement. Materialization resolves prefab data from those cell-owned handles, so a dependency cannot be evicted between asynchronous cell construction and the later Flecs transaction. The callback resolver remains only as an explicit fallback for tools and isolated callers.

## Current boundary

This layer now also owns `WorldRenderBridge`, the component-neutral adapter from exact committed ECS changes to RenderScene proxies. The ECS journal carries both stable authored identity and the dense Flecs index/generation used by runtime consumers. Repeated writes coalesce by runtime entity/component rather than causing full table scans. RenderScene owns a sparse paged producer directory keyed by that runtime handle and a short intrusive contributor chain; the bridge therefore keeps no per-entity allocation, ownership map, or nested contributor map. Existing mesh, light, and decal contributors update typed payload fields in place, retaining their proxy handles, spatial membership, and GPU identities; only real component addition/removal changes lifetime. Project code registers contributor and shared-state translators, while RenderScene remains independent of Flecs and authored component schemas. Activation changes are translated and capacity-checked as one publication envelope, world-session generations invalidate stale work, and RenderScene/GPU Scene lifetime remains responsible for storage still referenced by published work. Native Flecs observers may append exact dirty identities through the tiny ECS capture boundary, but never perform renderer work in observer callbacks. A cold full-world validator exists for rebuild and diagnostics, not the frame hot path.

Automatic frame ordering between GameWorld flush, bridge capture, RenderScene prepare/commit, collection and retirement belongs to the engine frame-pipeline integration layer. Renderer-side distant-proxy policy and broader resource-readiness aggregation remain above this module.
