# Vanguard Desktop Benchmark Harness

This harness keeps compilation and orchestration on the development machine while
running Vanguard and Tracy locally in the benchmark machine's interactive Windows
session.

## Benchmark host contract

- SSH alias: `vanguard-bench`
- Interactive user: `Ark`
- Benchmark root: `E:\VanguardBench`
- Scheduled task: `VanguardBenchProfile`
- Runtime configuration: `Profile`, x64
- Normal capture: 18 seconds with a 2 second warm-up
- Runtime window extent: `1280x720` by default, passed explicitly through
  `--window-width` / `--window-height`
- The runner switches its query thread to per-monitor DPI awareness and rejects the
  run unless the actual Vanguard client pixel extent matches the requested extent.
- Reported statistics: final 12 seconds of exact `Application/Frame` zones
- Validity gate: exact `RenderFlow/Resolve` activity must reach the end of the trace
- Synthetic render-graph/resource probes are explicitly removed from the benchmark
  process environment before Vanguard launches.

The laptop wrapper refuses an unrelated `Vanguard.exe` before it deploys any
payload, then checks again immediately before launching the task to close that
race. The desktop runner independently refuses an unrelated `Vanguard.exe` and
only stops the process instance that it launched itself. It also rejects
locked-console captures, activates Vanguard before warm-up, and verifies that
Vanguard owns the foreground window so background application throttling cannot
silently become a benchmark mode.

## Run

From the repository root:

```powershell
.\scripts\benchmark\run_desktop_benchmark.ps1
```

The wrapper builds the current Profile Runtime with serialized outer MSBuild,
deploys the Runtime payload, Project Cardinal project file, app-local DirectX 12
Agility runtime, and Tracy tools, registers/updates the interactive scheduled
task, runs the benchmark, validates it, and copies the result back to:

```text
build\profiling\desktop_runs\<run-id>\
```

Useful options:

```powershell
.\scripts\benchmark\run_desktop_benchmark.ps1 -SkipBuild
.\scripts\benchmark\run_desktop_benchmark.ps1 -Label owner_ready
.\scripts\benchmark\run_desktop_benchmark.ps1 -CaptureSeconds 18 -WarmupSeconds 2
.\scripts\benchmark\run_desktop_benchmark.ps1 -WindowWidth 1920 -WindowHeight 1080
```

Do not use an RDP session for benchmark captures. The scheduled task is registered
as `Interactive` and expects `Ark` to be logged into the physical console session.
