# Engine service usage policy

Vanguard-owned runtime and editor code uses Vanguard services whenever the
engine owns an equivalent capability. Building an allocator, logger, jobs
system, concurrency layer, filesystem, container library, profiler, or other
engine facility and then bypassing it defeats tracking, budgets, telemetry,
debugging, editor inspection, platform control, and optimization.

## Binding rule

A standard-library, CRT, operating-system, or third-party facility may be used
directly only when all of the following are true:

1. Vanguard does not provide the required equivalent.
2. The facility's allocation, ownership, threading, determinism, ABI, and
   failure behavior fit the subsystem contract.
3. RED used the same kind of facility in the equivalent layer, or Vanguard has
   a documented reason to differ.
4. The exception is narrow, reviewed, and recorded in
   `configs/engine/engine-code-audit-allowlist.txt`.

The fact that a facility is in the C++ standard library is not sufficient.

## Required engine routes

| Need | Required route |
|---|---|
| Dynamic memory, pools, arenas, tags | `vanguard::memory` |
| Runtime/editor logging and structured diagnostics | `vanguard::diagnostics` |
| Threads, locks, events, atomics | `vanguard::concurrency` |
| Scheduled asynchronous work | `vanguard::jobs` |
| Engine math and SIMD | `vanguard::math` |
| Future owning strings and containers | Vanguard container contracts |
| Future files, paths, and asynchronous I/O | Vanguard filesystem/I/O contracts |
| Future timing, tracing, profiling, telemetry | Vanguard diagnostics/profiling contracts |

Placement construction is permitted because it does not acquire storage.
Non-owning and compile-time standard facilities approved by
`cpp-and-dependencies.md` remain permitted.

## Layer exceptions

The following are architectural boundaries, not general permission:

- System assertions and fatal reporting may use bounded, allocation-free CRT
  formatting and direct debugger/OS output.
- The root memory implementation may acquire pages or backing blocks below
  itself.
- Diagnostics bootstrap may use allocation-free emergency output before normal
  sinks exist.
- Quarantined imported, reproducibly adapted RED images, and third-party source
  retain their proven internals until their owning integration explicitly
  replaces them.
- Tests and benchmarks may use console output and test-only standard helpers.

These exceptions must not leak into ordinary engine module code.

## Enforcement

`scripts/audit-engine-code.ps1` rejects known bypasses in Vanguard-owned engine
code, including CRT allocation, raw heap expressions, console output, direct
RED logging, and resource-owning standard facilities.

The audit runs during Premake generation and before every Vanguard-owned
runtime or compatibility project build. Any exception requires an exact
file-and-rule allowlist entry with a technical reason.
