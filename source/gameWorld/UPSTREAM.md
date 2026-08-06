# Upstream design record

The runtime-system lifecycle mirrors the architecture studied in RED while replacing RED entity storage with Flecs and Vanguard-owned interfaces. The primary references are:

- `D:/root/R6.Root/Mainline/dev/src/common/worldRuntimeScene/include/worldRuntimeSystem.h`
- `D:/root/R6.Root/Mainline/dev/src/common/worldRuntimeScene/src/worldRuntimeSystem.cpp`
- `D:/root/R6.Root/Mainline/dev/src/common/worldRuntimeScene/include/worldRuntimeScene.h`
- `D:/root/R6.Root/Mainline/dev/src/common/worldRuntimeScene/src/worldRuntimeScene.cpp`
- `D:/root/R6.Root/Mainline/dev/src/common/worldRuntimeScene/src/worldRuntimeSystemsProvider.cpp`
- `D:/root/R6.Root/Mainline/dev/src/common/worldEntities/src/entityScene.cpp`
- `D:/root/R6.Root/Mainline/dev/src/common/worldEntities/include/runtimeSystemEntityTransactor.h`
- `D:/root/R6.Root/Mainline/dev/src/common/worldEntities/src/runtimeSystemEntityTransactor.cpp`

The preserved ideas are fixed system identifiers, mode-filtered system sets, forward setup, reverse teardown, explicit attach/detach/post-detach phases, readiness aggregation, queued entity actions and queued live component transactions at controlled sync points. RED package formats, RTTI, game-instance classes and renderer proxy types are not dependencies of this module.
