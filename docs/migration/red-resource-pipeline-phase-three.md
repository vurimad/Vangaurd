# RED Resource Pipeline Adaptation — Phase 3

## RED sources studied

The orchestration behavior was adapted primarily from:

- `redReflection/include/resourceLoader.h`
- `redReflection/include/resourceLoaderTypes.h`
- `redReflection/include/resourceLoaderScheduler.h`
- `redReflection/include/resourceToken.h`
- `redReflection/src/resourceLoader.cpp`
- `redReflection/src/resourceLoaderScheduler.cpp`
- `redReflection/src/resourceToken.cpp`
- `unitTestsReflection/src/resourceLoaderSchedulerTests.cpp`
- `unitTestsReflection/src/resourceLoaderTests.cpp`
- `unitTestsReflection/src/resourceTokenTests.cpp`

The previous Vanguard tree was inspected for integration lessons. It continued
to call RED's global loader and retained RED depot, reflection, and cooked
resource assumptions, so none of that public contract was carried forward.

## RED behavior retained

- A resource token owns a Jobs wait counter and completion deferral.
- Dependencies are represented by shared resource tokens.
- Dependency work is kicked off asynchronously.
- Parent context and priority propagate to dependencies.
- Destroying the last unfinished interest requests cancellation.
- Failure and cancellation complete the wait counter without reporting success.
- Loaded objects are held through strong/weak handle semantics.

## Vanguard contract

`ResourcePipeline` registers one generic Phase 2 loader adapter per resource
type. `PipelineRequest` represents one caller interest, while the underlying
`ResourceRequest` remains the coalesced operation token.

Every operation owns a Vanguard Jobs completion counter held open by a
completion deferral. Parent construction is dispatched with child counters as
Jobs dependencies. No worker waits on child resources.

The loader contract has two Jobs stages:

1. `DiscoverDependenciesFunction` declares required and optional typed
   references.
2. `ConstructResourceFunction` receives completed dependency handles and
   publishes one runtime object or a failure.

Concrete I/O and decode implementations will be attached in Phase 4. They do
not change graph ownership or completion semantics.

## Cancellation

Root requests and parent edges are counted separately. Cancelling one request
removes only its root interest. When the total reaches zero:

- queued or running work becomes cancelled;
- dependency interests are released recursively;
- shared dependencies continue while another parent still needs them;
- an executing loader observes cancellation through `LoadContext`;
- late publication is rejected by the Phase 2 registry;
- a completed object is evicted, with destruction deferred by strong handles.

This is explicit cooperative cancellation. Vanguard does not terminate a
thread or pretend cancelled work succeeded.

## Failure and cycle behavior

Required child failure becomes `DependencyFailure` in its parent and retains a
pointer to the causal operation. `FailureTrace` walks that chain for logs,
editor inspection, and tooling. Optional failure is exposed through
`LoadContext::DependencyError` with an empty handle.

The graph performs iterative reachability checks before attaching every edge.
A cycle terminates with `DependencyCycle`; it cannot become a permanent Jobs
counter deadlock. Dependency counts are bounded by `PipelineConfig`.

## Priority behavior

Priorities map onto Vanguard Jobs:

- Background and Low → Latent
- Normal and High → CriticalPath
- Critical → Immediate

Promotion updates the shared operation and all dependencies already discovered.
New dependencies and subsequent stages inherit the promoted value. The current
RED Jobs image does not support removing and reinserting an already-submitted
job, so Vanguard does not fake that behavior.

## Deliberately rejected

- RED depot paths, extensions, bootstrap state, and archive IDs.
- RED serialized resource headers and reflection packages.
- RED global `GResourceLoader`.
- hidden main-thread-only waits as the normal orchestration path;
- automatic repair of loader misuse;
- cancellation that kills work still shared by another graph.

## Conformance coverage

The Phase 3 suite covers:

- diamond dependency fan-in and shared-child construction;
- simultaneous caller coalescing;
- required and optional failures;
- causal failure traces;
- dependency-cycle termination;
- priority promotion;
- cancellation with a still-interested sibling;
- cancellation of the last interest during construction;
- unknown loader failure;
- stage drain, eviction, registry removal, and shutdown.

The concurrent suite is also repeated as a soak during hardening.
