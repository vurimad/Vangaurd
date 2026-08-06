# RED Vanguard AAA Engineering Standard

## Mission

RED Vanguard is a high-performance open-world game engine with a state-of-the-art
editor. Runtime and editor are two clients of the same engine contracts. Every
major system must therefore be designed for shipping performance, authoring,
inspection, validation, automation, and long-term evolution.

Vanguard is not a renamed build of REDengine. REDengine is a source of proven
ideas, production experience, and mature subsystem implementations. We may
carry a complete working RED image behind a quarantined compatibility boundary
when preserving it is the deliberate engineering choice. Vanguard still owns
the public contract, naming, dependency graph, formats, tests, and evolution.

The goal is not maximum short-term code volume. The goal is a coherent engine
that remains understandable and performant after years of development.

## Non-negotiable rules

### 1. Contracts before implementations

Every subsystem begins with a written responsibility, public contract,
dependency list, ownership model, threading model, failure policy, and test
strategy. An implementation is not accepted merely because it compiles.

### 2. Dependencies flow in one direction

Low-level modules never depend on runtime systems, tools, editor code, or game
code. Runtime modules never depend on editor modules. Cycles between modules are
architecture defects and must not be hidden by build-system tricks.

### 3. Editor and runtime share truth

The editor must exercise the same resource, world, rendering, physics, and
serialization contracts used by the runtime. Editor convenience code may sit
above those contracts but may not create a second engine implementation.

### 4. Headless operation is mandatory

Importing, validation, generation, cooking, packaging, world processing, and
testing must work without launching the graphical editor. Editor actions should
call reusable commands or services rather than contain the only implementation
of an operation.

### 5. Performance is designed and measured

Hot paths require explicit data layout, ownership, lifetime, allocation, cache,
threading, and synchronization decisions. Performance claims require
benchmarks, representative workloads, or profiler evidence.

No hidden heap allocation, blocking I/O, global lock, or unbounded work is
allowed in a declared hot path.

### 6. Memory ownership is visible

APIs must make ownership and lifetime understandable. Allocator choice,
alignment, memory category, and destruction responsibility must not depend on
unstated conventions. Frame, level, streaming, editor-document, and persistent
lifetimes must remain distinguishable.

### 7. Concurrency is part of the contract

Thread affinity, concurrent access, cancellation, waiting, and shutdown
behavior must be documented. A module must not silently create threads or block
worker threads. Determinism requirements must be explicit.

### 8. Persistent formats are products

Every Vanguard format has:

- A unique identity and magic value.
- An explicit version and compatibility policy.
- Defined byte order, alignment, and validation rules.
- Stable resource identities and recorded dependencies.
- Corruption and truncation handling.
- Deterministic generation where practical.
- Tests using retained fixtures from earlier versions.

Editable source data and cooked runtime data are separate contracts. Runtime
formats favor direct, bounded, streaming-friendly access. Authoring formats
favor fidelity, stable identity, diagnostics, and migration.

### 9. Failure behavior is intentional

Assertions detect programmer errors; they do not replace input validation.
Recoverable failures carry actionable context. Fatal failures preserve the
earliest useful evidence and terminate predictably. Shipping builds must never
depend on debug-only side effects.

### 10. Observability is built in

Major systems expose useful names, counters, timings, memory usage, state, and
failure context to diagnostics and editor tooling. Instrumentation must be
cheap when disabled and bounded when enabled.

### 11. Public APIs stay small

Modules expose only deliberate public headers. Implementation headers remain
private. Avoid global state, public mutable data, umbrella headers, and
convenience dependencies that increase rebuild cost or coupling.

### 12. Modern C++ is used deliberately

Vanguard uses standard C++ facilities when they satisfy the engine contract.
Custom primitives are justified by measurable performance, platform behavior,
debuggability, serialization requirements, or ownership needs—not habit.

The binding facility-by-facility rules are defined in
[`cpp-and-dependencies.md`](cpp-and-dependencies.md).

If Vanguard provides an allocator, logger, concurrency primitive, jobs system,
math type, profiler, filesystem, container, or equivalent service, all normal
engine and editor code uses that service. Direct standard-library, CRT, OS, or
RED access requires a narrow reviewed exception. The rule is mechanically
enforced by the engine-service source audit.

Warnings are treated as errors in Vanguard code. Unsafe conversions, lifetime
ambiguity, and platform-size assumptions are resolved explicitly.

### 12.1 Naming is uniform across visibility boundaries

All Vanguard C++ functions use `PascalCase`, including public namespace APIs,
member functions, private helpers, and file-local or static functions. Function
visibility does not change spelling. Local variables and parameters use
`camelCase`, member variables use the `m_` prefix, types use `PascalCase`, and
namespaces use lowercase names. Language and platform-mandated names such as
operators, `main`, and `wWinMain` retain their required spelling. Imported and
quarantined compatibility code retains its upstream naming.

### 13. Tests accompany contracts

Every module provides fast contract tests. Serialization and format code adds
round-trip, malformed-input, compatibility, and determinism tests. Concurrent
code adds shutdown, cancellation, saturation, and stress coverage. Performance
critical code adds benchmarks with recorded workload descriptions.

### 14. Generated state never becomes source state

IDE projects, object files, binaries, caches, cooked data, logs, and generated
code live in designated output directories. A clean checkout is sufficient to
reproduce generated state using documented tools.

### 15. No ungoverned REDengine ports

For every RED-derived subsystem, record:

- REDengine files and modules studied.
- Behavior and constraints retained.
- Historical assumptions rejected.
- Vanguard contract and naming chosen.
- Tests proving the rewritten behavior.
- Known differences and deferred capabilities.

A complete RED subsystem image may be preserved when partial reimplementation
would create more risk than value. It remains quarantined, keeps its
provenance, compiles behind a compatibility project, and is exposed only
through a Vanguard-owned boundary. Legacy names, compatibility macros,
platform branches, and module boundaries do not become normal Vanguard
surface area merely because the implementation was retained.

## Module acceptance checklist

A module is ready to become a dependency only when:

- Its responsibility and exclusions are documented.
- Its public dependency graph is valid.
- Ownership and threading behavior are explicit.
- Debug, Development, Profile, and Shipping configurations compile.
- Contract tests pass.
- Failure paths are tested.
- Public headers do not leak private or editor dependencies.
- Persistent output is versioned and validated, if applicable.
- Relevant performance baselines exist.
- Migration notes exist when REDengine informed the implementation.

## Current phase

The active substrate consists of System, Memory, Diagnostics, Concurrency,
Math, Containers, Jobs, I/O, Filesystem, Serialization, Packages, Reflection,
Schemas, Resources, Streaming, and Crypto. The current headless Assets
boundary establishes RED-derived cooking contracts: stable source identity,
compiler registration, dependency discovery, build fingerprints, derived-data
cache lookup, cooked artifact emission, and a persistent local DDC with atomic
publication and integrity recovery. Its RED-derived asynchronous build graph
adds transitive resolution, shared operations, Jobs counter fan-in, priorities,
cycle detection, failure propagation, and explicit cancellation. The
persistent `VADI` dependency index adds deterministic durable build records,
forward/reverse queries, transitive invalidation, exact dirty classification,
atomic publication, and corruption recovery. The RED-derived incremental
recooker adds unchanged-event suppression, bounded transitive invalidation,
minimal root selection, asynchronous batch handles, explicit cancellation,
and all-or-nothing index transactions. VPAK selection and deterministic
assembly now add versioned manifests, dependency closure, artifact policy,
stable build identity, content-addressed payload deduplication, bounded
streaming assembly, read-back validation, and atomic publication. Editor
integration and concrete shader, texture, mesh, and material formats build
above these generic contracts.
