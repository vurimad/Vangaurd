# Upstream design record

## Flecs

Vanguard integrates the repository-contained Flecs 4.1.6 amalgamation from `external/flecs/upstream`. It is compiled as a static C dependency with a deliberately small add-on set. The public Vanguard boundary keeps `ecs_world_t` opaque unless a caller explicitly includes `vanguard/ecs/native.hpp`.

## RED architecture studied

The stable identity and deferred structural-action boundary follows the responsibilities studied in RED's entity scene and runtime scene code, especially:

- `D:/root/R6.Root/Mainline/dev/src/common/worldEntities/src/entityScene.cpp`
- `D:/root/R6.Root/Mainline/dev/src/common/worldEntities/include/entityScene.h`
- `D:/root/R6.Root/Mainline/dev/src/common/worldRuntimeScene/src/worldRuntimeScene.cpp`
- `D:/root/R6.Root/Mainline/dev/src/common/worldRuntimeScene/include/worldRuntimeScene.h`
- `D:/root/R6.Root/Mainline/dev/src/common/worldEntities/include/runtimeSystemEntityTransactor.h`
- `D:/root/R6.Root/Mainline/dev/src/common/worldEntities/src/runtimeSystemEntityTransactor.cpp`

Vanguard does not copy RED package, RTTI or serialized entity-instance formats. Those dependencies are replaced by Vanguard stable IDs, Vanguard containers and a Flecs runtime. The retained design ideas are queued structural work, separate pending and executing component transactions, explicit synchronization points, distinct persistent/runtime identity and controlled scene ownership.

RED's live component transactor schedules component transfers and removals, executes a bounded active batch through Jobs, detaches and uninitializes removed components, refreshes entity services, and invokes completion callbacks. Vanguard maps the structural portion to ordered Flecs Add, Set and Remove commands. Flecs lifecycle hooks replace object-level attach/uninitialize for ordinary data components; expensive engine integrations will register explicit materialization observers in the schema layer.
