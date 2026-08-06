# RED Containers full-image adaptation

## Decision

Vanguard carries RED Containers as a complete working image. This preserves
the proven allocator-aware behavior required by RED-derived modules and avoids
reimplementing individual containers before their transitive users arrive.

## Ownership

- `source/imported/common/redContainers` is the quarantined source image.
- `redContainersCompat` compiles every RED Containers implementation and
  debugger integration file.
- `source/containers/compat` owns initialization translation.
- `source/containers/include/vanguard/containers` is the supported Vanguard
  dependency boundary.
- `source/jobs` no longer owns the RED Containers compatibility project.

## Preserved RED contracts

- owning containers retain an explicit `memory::Pool`;
- growth and reallocation return through that pool;
- copies may select a destination pool;
- moves transfer storage and preserve valid moved-from pool state;
- construction and destruction policies distinguish trivial and non-trivial
  elements;
- checked iterators remain configuration-controlled;
- hash maps and sets retain their single-allocation layouts and policies;
- strings retain the RED string-pool initialization and explicit-pool forms;
- lock-free queues retain their fixed-capacity concurrency algorithms.

## Vanguard policy

Vanguard does not enable `RED_CONTAINER_ALLOW_DEFAULT_POOL`. Owning containers
must receive an explicit pool unless RED's type contract already owns a
dedicated pool, as `String` does. This makes allocation ownership visible and
keeps memory telemetry useful to runtime and editor tooling.

No container algorithm was replaced with a newly invented implementation in
this phase.
