# VanguardSystem

`VanguardSystem` is the lowest-level Vanguard library. It defines the portable
compile-time and failure-handling contract required by every other module.

## Responsibilities

- Detect supported operating systems, architectures, byte order, and compilers.
- Validate the active Vanguard build configuration.
- Provide fixed-width, namespaced primitive aliases.
- Provide compiler attributes and optimization hints.
- Write allocation-free emergency diagnostics.
- Report assertions and fatal failures.

## Explicit exclusions

This module does not own:

- General-purpose strings or containers.
- Heap, virtual-memory, or allocator APIs.
- Clocks, threads, atomics, or synchronization.
- Structured or asynchronous logging.
- File access, paths, GUIDs, hashing, or encoding.
- Crash dumps or telemetry.
- Runtime, resource, world, or editor concepts.

Those capabilities belong to later modules and may depend on `VanguardSystem`.
`VanguardSystem` may never depend on them.

## Allocation and threading contract

Public operations perform no Vanguard heap allocation. Emergency diagnostics
use bounded stack storage and direct operating-system output. Operations are
safe to call before engine initialization and during fatal failure handling.

Assertion failure reporting may be called concurrently. Messages can interleave
until the dedicated diagnostic system introduces a serialized emergency sink;
correctness must never depend on message ordering.

## Failure semantics

- `VG_ASSERT` evaluates its expression only when assertions are enabled and
  terminates on failure.
- `VG_VERIFY` evaluates its expression in every build and is fatal on failure
  when assertions are enabled.
- `VG_ENSURE` evaluates in every build, reports failure, and returns `false`
  without terminating.
- `VG_FATAL` is enabled in every build and does not return.
- Debugger breaks occur only when a debugger is attached.
- Fatal failure always terminates even without a debugger.

Assertions report programmer defects. External data and recoverable runtime
conditions require normal validation and error propagation.
