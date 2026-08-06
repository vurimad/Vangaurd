# VanguardMemory

The `memory` project integrates the complete REDengine memory implementation as a
Vanguard-owned working fork. The objective is to retain its mature allocator,
pool, ownership, metrics, and debugging work while preventing RED-era
dependencies from becoming permanent Vanguard architecture.

## Current boundaries

- `source/imported/common/redMemory` contains the complete working memory image.
- `source/imported/common/redSystem` is a private compatibility dependency.
- `compat` owns direct backend integration.
- `include/vanguard/memory/pool.hpp` is the single sanctioned public adaptation
  boundary for RED's compile-time pool and resolver machinery. Callers use only
  Vanguard names and macros.
- `include/vanguard/memory` is the future stable Vanguard-facing boundary.
- Windows x64 is the first active platform.
- Console and Linux implementations remain present but are not built on
  Windows.

## Pool contract

Vanguard exposes RED's real hierarchical pool behavior through canonical
`PoolId` values and the complete typed-pool contract. Allocations, frees,
reallocations, budgets, metrics, frame-pool reset, registry traversal, custom
pool declaration, class pool resolution, polymorphic deletion, typed arrays,
and container-facing pool references are supported.

`MemoryBlock` retains its originating pool. New subsystems select their explicit
pool; the legacy overload routes to `PoolId::Engine`.

See `docs/architecture/memory-pools.md`.

## Initial exclusions from the Windows target

- Legacy process-wide `new` and `delete` overrides.
- DLL compatibility operators.
- Durango, Orbis, and Linux platform implementations.
- Console-only memory analyzers and call-stack collectors.

Allocator families, pools, hooks, metrics, ownership types, Wwise/ICU adapters,
and Windows diagnostics remain part of the compiled image unless a concrete
dependency or compiler failure requires a documented temporary exclusion.

## Verified baseline

The Windows x64 memory image builds and the public facade smoke test passes in:

- Debug
- Development
- Profile
- Shipping

The smoke and pool conformance tests initialize the RED-derived hierarchy,
validate aligned allocation/reallocation, budgets, metrics, traversal, frame
lifecycle, invalid requests, and multithreaded cross-pool allocation.

## Dependency rule

The imported compatibility system exists only to support the RED-derived
implementation. New code must not add dependencies on it. The permanent public
direction is:

```text
system <- memory <- higher Vanguard modules
```

Normal Vanguard callers may not name `red::` symbols. The implementation is
quarantined in `compat` plus the single `pool.hpp` adaptation boundary needed
to retain RED's compile-time type resolution without rebuilding it.

## Modernization rule

Integration fixes may correct compiler incompatibilities, platform defects,
undefined behavior, and build assumptions. Large internal redesigns wait until
the full image builds and its tests establish a behavioral baseline.

See `docs/migration/redmemory-full-image.md` for the integration contract and
the next modernization steps.
