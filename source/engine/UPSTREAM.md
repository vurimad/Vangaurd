# Upstream provenance

The I/O, Filesystem, and Jobs service ordering follows RED's game and editor launch policy: low-level I/O starts first,
the application instance establishes its physical file manager and configured roots, and Jobs starts afterward before
higher engine systems submit work. Vanguard replaces manual launcher initialization with owned service lifecycles and
deterministic reverse dependency shutdown. Vanguard keeps its own `root/data/cache` launch layout and does not inherit
RED's game-specific directory identities. Unlike RED launch paths that commonly left Jobs shutdown disabled and
therefore shut I/O down first, Vanguard requires a clean scheduler, shuts Jobs down explicitly, destroys Filesystem,
and only then tears down I/O.

RED's application instance creates its resource depot after the physical file manager and creates the shared resource
loader after the depot; shutdown destroys the loader before the depot and file manager. Vanguard retains that ownership
shape without adopting RED depot paths or formats: the managed Resources service requires Filesystem and Jobs, owns its
Vanguard registry before its pipeline, and destroys the pipeline before the registry.

RED keeps its resource depot and asynchronous sources alive beneath the shared resource loader. Vanguard expresses the
same lifetime direction through an explicit storage adapter: Resource Streaming registers source-backed loaders into the
already-running Vanguard pipeline, remains alive while any decoder or mount can serve requests, and is destroyed before
Resources. This preserves the proven ownership rule while keeping VPAK and Vanguard logical paths independent of RED's
depot and archive formats.

The Frame Pipeline was checked against RED's running-state and `baseEngine` main-loop structure. Vanguard retains the
single engine-owned tick, monotonic delta measurement and clamp, ordered input/simulation/streaming/presentation work,
and end-of-frame memory/profiling boundary. Vanguard expresses the implicit subsystem sequence as a validated compiled
participant graph, adds bounded fixed-step catch-up and per-participant telemetry, and keeps all Vanguard formats and
service identities independent.

RED's `IInputSystem` separates device collection, reset/refresh, connection queries, rumble, persistent key/axis state,
and buffered physical input from the higher `gameInput` manager that owns contexts, mappings, dead zones, sensitivity,
and listener dispatch. Vanguard retains this split. Its managed Input service consumes a platform-neutral backend in the
compiled Input frame phase, while SDL replaces RED's platform-specific Win32/XInput device implementations. Explicit
overflow recovery, generation-bearing gamepad identities, immutable frame snapshots, and synthetic release events make
the same lifetime and state guarantees visible in Vanguard's service architecture.

RED separates its long-lived engine/resource infrastructure from `BaseGameSession` and `State_Session`. A session owns
the runtime scene/game-instance attachment and performs ordered detach and shutdown without destroying the engine-wide
resource loader. Vanguard's World Session service retains that boundary while making the transaction explicitly
pollable: package ownership, active world loading, Flecs materialization, world-only release, and complete release have
typed states and failure results instead of being distributed through product-state callbacks.

RED's world streaming owns independent content observers rather than reading one hard-coded player or camera. An observer
publishes a valid reference position, velocity context, and debug identity; the streaming update collects valid observers
into a stable context, and camera or server systems control observer lifetime. Vanguard retains that separation and RED's
default forward-prediction caps for on-foot, ground-vehicle, and air-vehicle contexts. Raw observer pointers and destructor
detachment are replaced by fixed-capacity generational handles, copied producer state, explicit unregistering, and an
immutable per-frame snapshot suitable for both runtime cameras and editor viewports.
