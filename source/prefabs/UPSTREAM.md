# RED adaptation record

The module follows RED's separation between `ent::EntityTemplate` and world entity placements. The reusable resource owns a compiled component layout, resource dependencies, stable component identities and creation-ready serialized data. World placement, streaming distance, transform, global instance identity and per-instance overrides remain outside this module.

Primary references:

- `common/worldEntities/include/entityTemplate.h`
- `common/worldEntities/src/entityTemplate.cpp`
- `common/worldEntities/include/entityLayout.h`
- `common/worldEntities/src/entityLayout.cpp`
- `common/worldEntities/include/entityTemplateInclude.h`
- `common/worldEntities/include/worldNodeEntity.h`

The runtime data model is translated because RED's serialized RTTI objects and component instances cannot be reused with Vanguard reflection, schemas, resource IDs, VPAK and the future Flecs materializer. The ownership split, compilation boundary, asynchronous dependency model, stable authored identities and separate instance-data concept are retained.
