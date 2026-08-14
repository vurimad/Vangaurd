# Jobs benchmark methodology

`jobsBenchmarks` provides a small, repeatable local scheduler baseline. It is
not a shipping performance claim and does not fail a build for being slower on
a busy workstation.

## Measurements

- **Enqueue** measures the main producer's time to create and submit 32,768
  Vanguard logical tasks into one open fence group, divided by task count.
- **Completion throughput** measures the same batch from first submission
  through counter completion.
- **Idle wake** allows workers to sleep, then measures an immediate-priority
  dispatch until the callback begins on a worker. The counter wait occurs after
  the timestamp and is not part of the reported interval.
- **Parallel throughput** writes 1,048,576 independent array elements through
  RED parallel-for with a maximum batch size of 256.

The executable performs a warm-up, then reports the median and p95 of nine
batch samples. Wake latency uses 101 samples. It uses Windows'
`QueryPerformanceCounter` and runs with four workers so results can be compared
on the same machine and build configuration.

## Comparison rules

Compare only runs with the same:

- machine, power mode, worker count, and background load;
- Vanguard revision;
- compiler toolset and build configuration;
- debugger/profiler attachment state.

Use `Profile` for tracked baselines. Run the executable at least three times
and preserve the raw console output with machine metadata. Investigate a
repeatable regression; do not tune around one outlier. Debug results are useful
for correctness-oriented observation only.

These microbenchmarks do not represent open-world frame workloads. Streaming,
render submission, animation, physics, and editor workloads require separate
trace-driven benchmarks with frame-time distributions and oversubscription
analysis.
