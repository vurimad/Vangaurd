# Upstream design record

The entity materialization lifecycle follows the RED architecture studied in these sources:

- `D:/root/R6.Root/Mainline/dev/src/common/worldEntities/src/entity.cpp`
- `D:/root/R6.Root/Mainline/dev/src/common/worldEntities/src/entityScene.cpp`
- `D:/root/R6.Root/Mainline/dev/src/common/worldEntities/include/runtimeSystemEntityTransactor.h`
- `D:/root/R6.Root/Mainline/dev/src/common/worldEntities/src/runtimeSystemEntityTransactor.cpp`
- `D:/root/R6.Root/Mainline/dev/src/common/core/include/worldGlobalNodeID.h`
- `D:/root/R6.Root/Mainline/dev/src/common/core/src/worldGlobalNodeIDUtils.cpp`
- `D:/root/R6.Root/Mainline/dev/src/common/world/include/worldGlobalNodeRuntimeResolver.h`
- `D:/root/R6.Root/Mainline/dev/src/common/world/src/worldGlobalNodeRuntimeResolverImpl.h`
- `D:/root/R6.Root/Mainline/dev/src/common/world/src/worldGlobalNodeRuntimeResolverImpl.cpp`
- `D:/root/R6.Root/Mainline/dev/src/common/world/include/worldGlobalNodeRef.h`
- `D:/root/R6.Root/Mainline/dev/src/common/world/src/worldGlobalNodeRef.cpp`
- `D:/root/R6.Root/Mainline/dev/src/common/world/src/runtimeSystemNodeStreaming.cpp`
- `D:/root/R6.Root/Mainline/dev/src/common/world/include/runtimeSystemNodeStreaming.h`
- `D:/root/R6.Root/Mainline/dev/src/common/world/include/worldStreamingListener.h`
- `D:/root/R6.Root/Mainline/dev/src/common/world/include/worldPrefabNodeInstance.h`
- `D:/root/R6.Root/Mainline/dev/src/common/world/src/worldPrefabNodeInstance.cpp`
- `D:/root/R6.Root/Mainline/dev/src/common/world/include/worldNodeGroup.h`
- `D:/root/R6.Root/Mainline/dev/src/common/world/include/worldPrefab.h`

The retained mechanics are staged construction before world visibility, stable hierarchical entity identities, complete validation before publication, a world-scoped hash registry with read-heavy synchronization, weak/generational runtime resolution, registration in the attach epilogue, unregistration before streamed-out notification and destruction, explicit resolver/activation barriers, queued structural transactions, generation-aware cancellation, reverse teardown, separate parallel/serial operation stages, an explicit synchronization fence, final attach/detach epilogues, and group-owned instance lists that are created and unregistered independently. Vanguard adds indexed unresolved waiters, required-reference dependency closure, and multiple explicit owners because `.vcell` records those policies directly and editor/gameplay systems may overlap. Vanguard replaces RED RTTI, packages, entity classes and runtime-system component ownership with reflection schemas, `.vprefab`/`.vcell`, Flecs components and Vanguard world transactions. No RED file format or public namespace crosses this module boundary.
