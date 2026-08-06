# Game World

The `gameWorld` module is the runtime composition root above ECS. It owns one ECS world and coordinates caller-owned runtime systems in a deterministic lifecycle.

## Lifecycle contract

Runtime systems have fixed numeric IDs. Active systems are selected by world mode and sorted by ID before initialization.

| Operation | Order |
|---|---|
| Initialize | Ascending system ID |
| Setup | Ascending system ID |
| Attach game | Ascending system ID |
| Begin frame | Ascending system ID |
| End frame | Ascending system ID |
| Post-flush epilogue | Ascending system ID |
| Detach game | Descending system ID |
| Post-world detach | Descending system ID |
| Uninitialize | Descending system ID |

Game, preview, thumbnail and headless worlds can activate different system sets without changing registration order. Failed initialization or setup rolls back initialized systems in reverse order and shuts down the newly created ECS world, allowing a clean retry.

Detach and post-world detach are intentionally separate. Detach removes the live game association; post-world detach is the barrier where systems finish work that must outlive the immediate callback, such as draining queued proxy removal or reference relinking. Shutdown refuses to proceed while a game remains attached, post-detach work remains, or stable entities are still materialized.

## Frame boundary

`Tick` flushes incoming entity actions followed by component actions, calls runtime-system begin-frame hooks, progresses the Flecs pipeline when ticking is enabled, calls end-frame hooks, then repeats the entity-first/component-second flush for work produced during the frame. `OnAfterWorldFlush` runs only after that transaction commits, providing the explicit epilogue where systems may publish readiness or acknowledge destruction. Shutdown runs the same epilogue after its final queue drain.

## Current boundary

Stable gameplay component schemas and transactional `.vprefab`/`.vcell` materialization live in the `entities` module and commit through these world sync points. The cell streaming runtime uses the post-flush epilogue for reference publication, readiness and ordered resource release. Renderer-side proxy systems can use the same boundary next.
