# C++ and dependency policy

## Purpose

Vanguard owns its engine contracts. The C++ standard library and third-party
libraries are implementation tools, not automatic architectural authorities.

The decision is made facility by facility. "Standard" does not automatically
mean acceptable, and "custom" does not automatically mean faster or better.

The binding engine-service routing and enforced exception process are defined
in [`engine-service-usage.md`](engine-service-usage.md). When Vanguard owns an
equivalent facility, engine and editor code must use it.

## Standard-library facilities allowed in public low-level contracts

The following are normally acceptable when their exact semantics fit:

- Fixed-width integers from `<cstdint>`.
- Size and pointer-difference types from `<cstddef>`.
- Type traits, concepts, and compile-time utilities.
- `std::byte`.
- `std::source_location`.
- `std::bit_cast`, endian declarations, and bounded algorithms.
- Atomics only after platform code generation and memory ordering are reviewed.

These facilities do not transfer resource ownership and do not require the
standard heap.

Once Containers is available in a module's dependency layer, public and
internal non-owning views use `containers::ArraySpan` and
`containers::StringView`. `std::span` and `std::string_view` are permitted only
below Containers, or through a narrow reviewed exception.

## Facilities requiring an explicit module decision

These may be useful internally but must not silently define engine-wide
contracts:

- `std::string` and owning standard containers.
- `std::filesystem`.
- Standard threads, mutexes, condition variables, and futures.
- Polymorphic allocators.
- Regular expressions and locale-dependent text processing.
- Iostreams.
- Random-number engines.

Approval requires documented allocation, ABI, determinism, performance,
debugging, and platform behavior.

## Facilities forbidden as default engine policy

- CRT allocation and raw allocating `new`/`delete` in ordinary engine code.
- Console or debugger logging outside approved bootstrap/failure layers.
- Direct RED logging from Vanguard-owned modules.
- Exceptions for ordinary runtime control flow.
- `std::async`.
- Implicit global-locale behavior.
- Persisting the in-memory representation of standard containers.
- APIs that hide ownership or allocation behind convenience return types.
- Unbounded formatting or allocation in failure and crash paths.

## Vanguard-owned facilities

Vanguard will own facilities when engine requirements demand control:

- Allocators, arenas, pools, tags, tracking, and virtual memory.
- Hot-path containers and owning strings.
- Jobs, fibers or task scheduling, worker waits, and cancellation.
- File paths, asynchronous I/O, archives, and package access.
- Reflection, stable names, resources, handles, and serialization.
- Logging, tracing, profiling, crash reporting, and telemetry.
- Deterministic math or random facilities where required.

## REDengine use

REDengine itself uses selected standard headers and facilities beneath RED-owned
contracts. Vanguard follows the useful principle, not RED's exact age-specific
compatibility layer.

For every RED-derived subsystem:

1. Record the RED files studied.
2. Identify behavior, layout, and platform constraints.
3. Decide which concepts survive.
4. Reimplement or deliberately retain a complete working image behind a
   Vanguard contract.
5. Prove behavior with tests and performance evidence.

Direct translation is allowed only when preserving a proven implementation is a
conscious decision and the resulting code meets current Vanguard standards.
Reproducibly adapted RED source may retain facilities RED used in that same
layer, but new Vanguard orchestration around it must use Vanguard services.

## Third-party dependencies

Every third-party dependency requires:

- Exact version or commit.
- Source and license.
- SHA-256 hash for acquired binaries.
- Supported-platform list.
- Build options and local patches.
- Owning Vanguard module.
- Upgrade and security responsibility.
- Public API exposure decision.

Third-party types should not cross Vanguard public boundaries unless the
dependency is intentionally part of that boundary.
