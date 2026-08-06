# jobs

`jobs` is Vanguard's supported task-scheduling contract. Its backend retains
RED's complete production scheduler: bounded priority queues, dependency
counters, worker threads, waiting lists, parallel-for batching, continuations,
job-scope allocators, debugger data, and profiler instrumentation.

Adapter objects with dynamic lifetimes—builders, counters, and completion
deferrals—allocate through `vanguard::memory`. Their envelopes retain the exact
memory block returned by the engine allocator, so destruction returns storage
through the same tracked path. Task payloads use the same memory facade.

Normal runtime and editor code includes only `vanguard/jobs/jobs.hpp`.
`jobsCompat` is the only adapter allowed to include RED Jobs/Core/Containers/IO
headers. Imported implementation projects remain under `Engine/Compatibility`
in Visual Studio.

## Public contract

The initial Windows x64 API provides:

- runtime, editor, and headless-tool configurations;
- priorities and worker scheduling;
- single-thread-owned builders;
- explicit and full fences;
- move-only dependency counters with finite or infinite waits;
- move-only RED completion deferrals that hold counters open;
- RED counter blocker/dependency/deferral analysis;
- ordinary tasks and dependency-gated tasks;
- parallel-for jobs, epilogues, and maximum batch sizes;
- continuation builders created from a running `JobContext`;
- dispatcher index and worker-thread queries;
- explicit registration for external producer threads;
- up to 27 RED worker threads on Windows with extended memory registration;
- lock-free scheduler snapshots for editor and runtime diagnostics;
- physical queue depths and logical submission/completion/wait totals;
- shutdown refusal while builders, counters, or jobs remain outstanding.

`Task` and `ParallelTask` use Vanguard memory for their move-only callable
payload. They do not use `std::function`, `std::async`, standard threads, or
standard synchronization.

## Lifetime rules

- Jobs initialization and shutdown are serialized composition-root operations.
- The imported scheduler supports one initialize/shutdown lifecycle per process.
- A `Builder` is owned and used by one thread.
- `Fence::None` appends work to the builder's current parallel dispatch group.
  `DispatchFence()` closes that group explicitly. The caller must close it
  before a dependency transition, a full-fence dispatch, counter extraction,
  or builder destruction. Assert-enabled configurations report misuse at the
  Vanguard boundary; Vanguard never inserts the missing fence or changes the
  requested dependency phases.
- A `JobContext` is valid only during its callback. A continuation builder must
  be constructed during that callback.
- A `JobName` and its static string must outlive every job dispatched with it.
  The compatibility layer tracks in-flight references and asserts on violation.
- Every `Counter` and `Builder` must be destroyed before shutdown.
- Every unfinished `CompletionDeferral` must be finished or destroyed before
  shutdown. Destroying a deferral finishes it, matching RED.
- A deferral's static debug name and debug user data must remain valid until
  the deferral finishes when the RED job debugger is enabled.
- Every submitted logical job must complete before shutdown.
- External producer threads call `RegisterCurrentThread()` before allocating or
  dispatching jobs.

These are scheduler correctness requirements, not optional style guidance.

## Editor and runtime

Editor and tool configurations preserve RED's larger authoring queues and
all-critical-path tool mode. The same scheduler implementation is used by
runtime and editor so dependency behavior cannot drift between cooked play,
play-in-editor, asset processing, and headless tools.

`GetSchedulerStats()` is safe to sample from editor panels and runtime health
checks without scheduler locks. Queue depths are physical runnable entries in
RED's four global MPMC queues. Submitted, completed, and outstanding counts are
Vanguard logical jobs, so one parallel-for dispatch counts once even though RED
may create several physical queue entries. The fields form an approximate
concurrent snapshot, not a transaction; they can advance while being copied.

RED already supplies two diagnostic paths:

- `Counter::Analyze()` emits RED's textual blocker graph, including dependent
  jobs and completion deferrals, when `Config::enableDebugger` is enabled.
- Every `JobName` is a RED profiler instrumentation object, so RED's generic
  profiler records job execution scopes.

RED Jobs 2 does not provide cancellation, a direct worker-utilization
percentage, structured graph export, or restartable pool lifecycle. Vanguard
does not simulate those capabilities. See
[`docs/architecture/jobs-capabilities.md`](../../docs/architecture/jobs-capabilities.md).

## Validation

`jobsTests` covers:

- lifecycle and worker identity;
- bulk dispatch and explicit fences;
- dependency ordering;
- parallel-for batching and epilogues;
- continuation completion;
- completion deferral hold/release and shutdown behavior;
- RED counter analysis entry point;
- timed waits;
- clean shutdown.

`jobsStress` covers:

- concurrent submission from three external producer threads;
- RED's 27-worker Windows ceiling, capped by available hardware threads;
- 24,576 independently submitted jobs;
- a 1,024-link dependency chain;
- 1,048,576 parallel-for element visits;
- outstanding-work and shutdown invariants.

`jobsSoak` covers:

- all four physical queue-depth gauges with every worker pinned;
- eight reproducible 2,048-node DAGs with up to four dependencies per node;
- mixed explicit/full fence behavior;
- shutdown rejection during active work and while handles remain live;
- cumulative telemetry convergence.

`jobsBenchmarks` is an informational performance executable, not a unit test.
Its methodology and interpretation rules are in
[`BENCHMARKS.md`](BENCHMARKS.md).
