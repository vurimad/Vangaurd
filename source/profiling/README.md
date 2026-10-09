# profiling

`profiling` is Vanguard's engine-facing profiling boundary. Engine code uses the `VG_PROFILE_*` macros from `vanguard/profiling/profiling.hpp`; direct Tracy includes are confined to this module and the vendored Tracy target.

The `Profile` configuration enables Tracy. Debug, Development and Shipping compile the profiling macros to no-ops. This keeps ordinary development measurements and shipping behavior independent from profiler instrumentation while preserving one production-code path for profile captures.

Initial integration provides CPU scope zones, dynamic zone names, application frame marks and thread naming. GPU/RHI profiling should be integrated through the existing RHI ownership boundary rather than by allowing renderer code to reach around Vanguard's command-list and queue abstractions.

## Capture workflow

1. Generate the normal VS2022 projects with `premake5.exe vs2022`.
2. Build the desired application in the `Profile|x64` configuration. The Runtime output is `bin/Runtime/Profile/Vanguard.exe`.
3. Start a Tracy profiler UI built from the same pinned upstream revision in `external/tracy/UPSTREAM.md`.
4. Launch the Profile application and connect the Tracy UI to the local client.

The initial Runtime capture exposes:

- `Application/Frame`, `Application/PumpEvents` and `Application/StateTick`;
- Vanguard thread names, including the main thread and Jobs workers;
- each ordinary Jobs task under its real `JobName`;
- Jobs builder creation/destruction, dispatch, dependency and counter-extraction operations;
- `RenderGraph/ScheduleNodes` and each render-node execution under its render-node name.

These zones are diagnostic evidence, not performance acceptance criteria. Repeatable performance budgets and regression gates remain Vanguard-owned metrics. Tracy is used to explain where measured time goes, including context-switch and scheduling gaps that ordinary aggregate timers cannot attribute.
