# Jobs capability boundary

Vanguard exposes RED Jobs 2 behavior without manufacturing scheduler features
that RED does not implement.

| Capability | RED Jobs 2 support | Vanguard status |
|---|---|---|
| Priority queues | Four bounded global queues | Exposed |
| Dependency counters | Reference-counted counter graph | Exposed |
| Explicit/full fences | Builder phase semantics | Exposed |
| Parallel-for and epilogues | Team sizing and batching | Exposed |
| Continuations | Parent completion extension | Exposed |
| Completion deferrals | Counter held by a manually released fake job | Exposed |
| Blocker analysis | Textual counter/job/deferral traversal | Exposed through `Counter::Analyze()` |
| Job execution profiling | Generic RED instrumentation scopes per `JobName` | Preserved |
| Windows worker ceiling | 27 with RED extended memory registration | Exposed |
| Job cancellation | Not implemented | Not exposed |
| Direct worker-utilization metric | Not implemented | Not exposed |
| Structured dependency-graph export | Not implemented; analyzer writes logs | Not exposed |
| Structured wait-reason API | Not implemented; analyzer writes logs | Not exposed |
| Reinitialize after shutdown | Pool restart contract not implemented | Not exposed |

## Counter analysis

RED's debugger registers counters and completion deferrals. `AnalyzeCounter`
walks registered counters and waiting lists, classifies each discovered counter
as runnable or waiting, and logs blocking jobs and deferrals. It is a
synchronous diagnostic dump, not an editor data model. Vanguard delegates to
that implementation and does not reinterpret its graph.

Enable it with `Config::enableDebugger`, then call `Counter::Analyze()` on the
counter of interest. Timed-out RED waits also invoke the same analyzer.

## Profiling and utilization

RED wraps each executed job in the instrumentation object represented by
Vanguard's `JobName`. The generic RED profiler can therefore show execution
spans per worker. RED Jobs 2 does not calculate a utilization percentage or
publish per-worker accumulated busy/idle time.

A future Vanguard profiling module may expose RED profiler capture and derive
views from those recorded spans. Jobs must not invent a utilization counter
that RED never maintained.

## Worker count

The imported Windows memory configuration enables
`RED_MEMORY_ENABLE_EXTENDED_THREAD_REGISTRATION`. RED consequently defines
`RED_MAX_JOB_THREADS` as 27 and its memory allocators reserve 64 thread slots.
The actual worker count is:

```text
min(requested workers, max(1, hardware threads - 1))
```

Vanguard accepts explicit requests through 27 and preserves RED's hardware
cap.

## Lifecycle

RED's original scheduler relies on engine-global pool lifetime. The retained
adaptation adds orderly shutdown for job-scope allocators, but its pool
initialization flag and imported pool registry do not provide a verified second
initialization. Vanguard therefore keeps the scheduler one-shot per process.
