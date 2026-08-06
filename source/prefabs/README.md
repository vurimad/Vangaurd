# Prefabs

`prefabs` owns Vanguard's deterministic cooked `.vprefab` resource. A prefab is a reusable, ECS-neutral entity prototype. It is not a live entity and contains no Flecs identifiers.

`PrefabResource` is the runtime resource wrapper around the validated immutable file. Streamed cells retain its resource handle for as long as their placements can materialize or activate, keeping resource lifetime generational and explicit.

The cooked resource contains a stable entity hierarchy, stable component identities, schema/version identities, independently checksummed schema-serialized component blobs, a coalesced typed resource-dependency table, source and content fingerprints, and strict read limits. Component records are grouped by entity for direct materialization. Entities and components are canonicalized so input ordering cannot change cooked bytes.

`CookPrefab` uses registered Vanguard schemas to serialize component objects and extract their resource dependencies. This keeps resource discovery and serialization under one reflection contract. The future Flecs adapter will load each component blob through its schema and install the resulting value into a live Flecs entity.

The input is deliberately an already resolved prototype. Prefab includes/inheritance, authoring overrides, editor transactions and source-file tracking belong to the shared asset/editor layer and will compile into this flat runtime form. World transforms, streamed placement, stable world-instance IDs and instance overrides belong to `vcell`, not `.vprefab`.
