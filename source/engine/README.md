# Engine orchestration

`engine` is the composition layer above portable Application contracts and below product clients. It owns lifecycle
adapters for concrete engine subsystems without forcing low-level modules to depend on the application framework.

I/O, Filesystem, Jobs, Frame Pipeline, Input, Resources, Resource Streaming, World, Game World, and World Session are the first managed services. I/O owns the low-level asynchronous worker and publishes the
I/O system capability. Filesystem requires I/O, owns the physical file-manager lifetime, and publishes the filesystem
capability. Its default launch layout uses the application root as the engine root, `root/data` as the game-data root,
and `root/cache` as the derived-data root. Jobs starts after Filesystem when it is present, follows the active application
profile, publishes the scheduler capability, and rejects shutdown with outstanding work, deferrals, builders, or
counters. Resources requires Filesystem and Jobs, owns the registry followed by the asynchronous pipeline, publishes
typed registry and pipeline capabilities, and refuses quiesce or shutdown while requests, handles, jobs, preparations,
or resident resources remain live. Resource Streaming requires Resources, Filesystem, I/O, and Jobs; it owns decoder,
loose-source, and VPAK mount registration plus the bounded staging layer, while mounted readers and callback state remain
caller-owned. It refuses quiesce or shutdown while loads, reads, or staging allocations remain active. World registers the
`vworld` decoder, owns the startup-world request and handle, and creates the world streaming grid and asynchronous executor
after the startup resource is ready. The service explicitly rejects shutdown until the application releases that state, so
its reverse dependency ordering is guaranteed rather than hidden in a product loop. Runtime and editor use the same adapters;
entity materialization and rendering services can consume the World capability without taking ownership of its resources.

Input requires Frame Pipeline and a platform-supplied `IInputBackend`. It consumes the bounded platform batch in the Input phase and publishes persistent keyboard, mouse, and gamepad state, one-frame transitions, ordered buffered events, focus state, last-active-device identity, hot-plug state, rumble output, and loss/reset telemetry. The physical layer is allocation-free per frame and emits synthetic releases on focus loss, device removal, explicit reset, or queue overflow. Gameplay mappings and editor command contexts remain a separate consumer layer.

Game World depends on World and Resource Streaming. Once the startup world is ready it owns one Flecs-backed `GameWorld` and the cell-streaming runtime system, while the loaded `vworld`, grid, and executor remain owned by World. It registers the `vcell` and `vprefab` decoders, advances transactional cell materialization through the normal Game World synchronization points, and requires an explicit all-node drain before shutdown. Optional product callbacks register concrete component schemas and consume non-cell proxy events without changing service ownership.

Streaming Observer is the producer-facing layer above Game World's low-level streaming input. Cameras, players, vehicles, editor viewports, or dedicated-server interest sources register fixed-capacity generational handles and publish position, velocity, validity, and an observer class. During `PreSimulation`, the service copies all enabled and positioned observers into one immutable snapshot, predicts their streaming positions with configurable class speed caps, selects an explicit primary observer for camera-relative priority, and passes the snapshot to Game World before its `Simulation` participant runs. It allocates nothing per frame, rejects stale handles and non-finite state, retains no caller memory, and requires registrations to be explicitly released. Until a producer is available, a loaded world's origin is used as a clearly reported bootstrap fallback rather than embedding a permanent camera in Game World.

World Session is the transaction coordinator above those engine-wide services. It owns no service instance; it owns the active relationship between a mounted package set, the installed project input mapping, one loaded world, and one Flecs Game World. `Begin` optionally mounts the package set, requests the catalog's default `vinput` at critical priority, chooses an explicit world reference or the catalog startup world, and does not publish `Running` until the mapping is installed and the world is ready for Game World materialization. `RequestStop(ReleaseWorld)` drains Game World and releases the world while retaining packages and the project mapping in the stable `Mounted` state. `RequestStop(ReleaseEverything)` additionally cancels outstanding startup work, unmounts the package set, and returns to `Idle`. Failed starts also require this explicit cleanup path. EngineHost rejects shutdown while a session remains live.

## Frame Pipeline

Frame Pipeline is the compiled main-thread schedule for a running runtime or editor. Subsystems register stable participant IDs during service startup, select an application profile, choose one global phase, and declare explicit same-or-earlier-phase dependencies. `Compile` validates the complete graph once, rejects missing or inactive dependencies, dependencies on later phases, cycles, duplicates, unsupported affinity, and capacity overflow, then freezes a deterministic allocation-free execution schedule.

| Order | Phase | Contract |
|---:|---|---|
| 1 | `PlatformEvents` | Consume platform events already published by the outer platform pump. |
| 2 | `Input` | Build the immutable input snapshot for this frame. |
| 3 | `BeginFrame` | Begin frame-scoped engine work. |
| 4 | `PreSimulation` | Prepare simulation inputs and pending transactions. |
| 5 | `FixedSimulation` | Run zero or more fixed steps under the catch-up budget. |
| 6 | `Simulation` | Advance variable-step gameplay and the Flecs world. |
| 7 | `WorldStreaming` | Evaluate and commit world residency work. |
| 8 | `PostSimulation` | Finalize simulation outputs and proxy changes. |
| 9 | `Presentation` | Produce interpolated presentation state. |
| 10 | `Render` | Submit rendering work when a renderer is registered. |
| 11 | `EndFrame` | Finish telemetry and frame-scoped work. |

The platform adapter still pumps native events before every application-state tick. This is intentional: loading, error, transition, and shutdown states must remain responsive even though only the long-lived running state executes Frame Pipeline. The `PlatformEvents` phase is therefore for consuming and translating the published event batch, not for calling the operating-system pump a second time.

Frame timing uses a monotonic high-resolution clock. Real delta is clamped, simulation delta applies the current time scale, and each fixed invocation receives a deterministic absolute fixed time distinct from variable simulation time. Fixed-step catch-up is bounded, excess simulation time is reported rather than hidden, and pause/time-scale changes requested during execution take effect at the next frame boundary. CPU target pacing is available before presentation owns synchronization; `Presentation` pacing means the presentation participant owns the blocking present operation.

Each executed frame records global, per-phase, and per-participant timings and invocation counts. A participant failure permanently fails the pipeline and reports the frame, phase, participant ID, related ID, stable name, and message. Frame-pool reset and memory-metric rollover occur exactly once after an executed frame, including a participant-failure frame. Worker affinity is rejected until a real Jobs-backed phase executor is implemented; it is never silently treated as main-thread work.
