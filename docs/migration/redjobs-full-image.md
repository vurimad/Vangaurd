# RED Jobs complete-image adaptation

## Decision

Vanguard retains the complete RED Jobs 2 scheduler and its proven low-level
dependency image. Replacing its lock-free queues, counter graph, waiting-list
protocol, worker wakeup behavior, parallel-for batching, and job-scope memory
in one step would discard mature concurrency engineering without improving
Vanguard's architecture.

The implementation remains quarantined:

```text
system + memory + concurrency
             |
shared RED compatibility depot
             |
       redJobsCompat
             |
         jobsCompat
             |
            jobs
```

Only `vanguard::jobs` is supported engine API.

## Preserved scheduler semantics

The adaptation preserves:

- bounded queues separated by latency/critical/render/immediate priority;
- local worker queues and global MPMC scheduling queues;
- full-barrier RED atomics and semaphore wakeups;
- reference-counted dependency counters;
- locked waiting-list flushes that prevent premature dependency release;
- explicit fences for parallel dispatch and full fences for ordered dispatch;
- main-thread counter flushing that can perform eligible work;
- parallel-for team sizing, batching, shared state, and epilogues;
- continuation counters extending parent-job completion;
- job-scope thread allocators;
- worker affinity, naming, stack sizing, profiler registration, and debugger
  stack traces;
- editor and tool queue presets.

## Vanguard boundary

Callable payloads are Vanguard-owned and move-only. They allocate from the
Vanguard memory facade, cross the adapter as function pointer/state packets,
and are destroyed exactly once when RED finishes the logical job. The public
contract does not expose RED pools, counters, instrumentation objects,
containers, IO priorities, or namespaces.

Static `JobName` objects wrap imported profiler instrumentation in fixed,
compile-time-checked storage. In-flight reference tracking prevents profiler
metadata from being destroyed while queued jobs still reference it.

Builders and counters use private compatibility objects. Their live counts,
together with outstanding logical payloads, guard shutdown.

The compatibility image now exposes diagnostic-only physical queue gauges.
Vanguard maps them into `SchedulerStats` alongside logical submitted,
completed, outstanding, wait, timeout, builder, and counter totals. RED types
remain behind the adapter.

RED completion deferrals are exposed as move-only Vanguard handles. Creation,
manual completion, destructor completion, debugger registration, and counter
reference behavior delegate to RED's `CompletionDeferral`.

RED's existing textual counter analyzer is exposed without conversion to a
Vanguard graph model. Cancellation, direct utilization metrics, structured
graph export, and restart are intentionally absent because RED Jobs 2 does not
implement those contracts.

## Lifecycle correction

The previous Vanguard integration discovered that REDengine relied on global
module ordering to destroy per-thread job-scope allocators before root memory.
That explicit shutdown correction is retained.

The imported scheduler is intentionally one-shot per process. Vanguard refuses
reinitialization after shutdown until the complete imported pool registry gains
a proven restart contract.

Shutdown refuses while:

- a public builder exists;
- a public counter exists;
- a submitted logical job has not finished.

## Validation strategy

Correctness validation includes ordered and unordered dispatch, dependency
chains, continuations, parallel-for epilogues, batching, timed waits,
multi-producer submission, thread registration, and lifecycle invariants.

Every supported build configuration must:

1. compile the entire solution;
2. pass `jobsTests`;
3. pass `jobsStress`;
4. pass `jobsSoak`;
5. run `jobsBenchmarks` as an informational Profile baseline when scheduler
   code or toolchains change;
6. pass all earlier System, Memory, Diagnostics, and Concurrency tests;
7. keep RED names and includes out of `source/jobs/include` and ordinary
   `source/jobs/src`.

Enqueue cost, logical completion throughput, idle wake latency, and
parallel-for throughput now have a local microbenchmark. Contention, worker
utilization, and trace-driven open-world streaming baselines remain dedicated
follow-up work.
