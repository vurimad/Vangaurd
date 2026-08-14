# ECS

The `ecs` module owns Vanguard's Flecs runtime and the boundary between persistent asset identities and transient ECS handles.

## Identity contract

- `EntityId` is the stable 64-bit identity stored by worlds, scenes, prefabs, save data and editor transactions.
- `Entity` is a transient Flecs handle containing an index and generation. It is valid only while its matching stable identity is materialized in one `ecs::World`.
- Unloading an entity destroys its transient handle. Loading the same `EntityId` later creates a new generation, so stale handles cannot silently address the replacement.
- Serialized data must never contain Flecs entity values.

## Structural mutation

Producers may call `QueueCreate` and `QueueDestroy` concurrently. These calls only append commands; they do not mutate Flecs. `FlushActions` is an explicit, serialized synchronization point that applies a batch and reports created, destroyed and rejected actions. Duplicate creation and destruction of unknown identities are reported as rejections.

Live component structure follows the same rule. Component types are registered during serialized setup with `RegisterComponent<T>`, which returns a runtime token scoped to that exact ECS world. Producers may then queue Add, Set and Remove operations without touching Flecs. `FlushComponentActions` preserves queue order and reports every successful or rejected operation. It does not silently turn duplicate Add into Set, or Set on a missing component into Add.

Every successful entity or component operation also enters a bounded, sequence-numbered committed-change journal. Independent subscribers retain their own cursors, receive exact stable entity/component identities and command-batch ownership, and are told when they fell behind the retention window. This journal is the primary incremental integration boundary for rendering and other derived worlds; it avoids scanning Flecs tables to rediscover writes that the engine already committed.

Native Flecs observers use `CaptureNativeComponentChange` only as a tiny journal append boundary. They do not invoke render-scene work, allocate derived payloads, wait for jobs or publish derived worlds while Flecs is mutating storage. Duplicate queued/observer notifications are legal and coalesce in downstream epochs.

Queued Set values own a copy in the Gameplay pool until commit. Nothrow copy construction and destruction are preserved for non-trivial C++ component values, and Flecs performs the final assignment through its registered lifecycle hooks. Runtime component tokens and Flecs IDs are never serialized; cooked data uses stable schema identities mapped to these tokens during materialization.

Multi-command publication uses `CommandBatch`. Every command carries its owning batch, pending create/destroy identities are centrally reserved, and other producers cannot submit commands against those identities until the batch is retired. Sealing prevents late additions. A receipt remains `Pending` across the entity-first/component-second flush boundary and becomes `Succeeded` only when every command commits; any rejected command makes it `Failed`. Cancellation removes only commands and staged values owned by that batch, including when an earlier structural flush has already committed part of the work.

The composition owner must serialize `FlushActions`, `FlushComponentActions`, `Progress`, native Flecs access and `Shutdown`. Vanguard does not silently close or repair incorrectly ordered world operations.

## Native integration

Most engine interfaces should include `ecs.hpp`, which keeps Flecs opaque. Systems that intentionally register or query Flecs components include `native.hpp` and use `ecs::Native(world)`. This confines third-party types to the ECS implementation boundary.

Flecs allocations, reallocations and string duplication are routed through the Gameplay memory pool. Flecs messages are routed through the Entity diagnostics category. Vanguard currently builds only the module, system, pipeline and timer add-ons; networking, REST, scripting and other tool-oriented add-ons are excluded.

## Current boundary

This foundation supplies world ownership, stable identity, generational safety, centrally reserved command batches, exact completion receipts, lifecycle-safe values, native system execution and statistics. Stable component schemas and transactional prefab/cell materialization live in the `entities` module; entity-reference relinking and streamed-world coordination remain above it.
