# RED Memory Full-Image Integration

## Decision

Vanguard retains the complete REDengine memory source image as a working fork
instead of recreating the subsystem a class at a time. Memory is mature
infrastructure and is low in the engine dependency graph. Reusing the complete
implementation lets Vanguard reserve original engineering effort for systems
that define the new engine: world representation, ECS, streaming, rendering,
tools, and the editor.

This is a source integration, not a permanent architectural endorsement. The
imported implementation is private and the Vanguard-facing API remains small.

## Source boundaries

```text
system
      |
      v
memory public facade
      |
      +--> complete imported redMemory working image
      |
      +--> private imported redSystem compatibility image
```

- `source/imported/common/redMemory` retains the complete upstream snapshot.
- `source/imported/common/redSystem` supplies private dependencies required by
  that snapshot.
- `source/memory/compat` is the quarantined adapter and the only integration
  area permitted to include RED headers or name `red::` symbols.
- `source/memory/include/vanguard/memory` is the only supported dependency for
  new Vanguard code.
- `external/ittnotify` retains the profiling interface expected by the imported
  implementation.

The compatibility system includes RED-era logging and diagnostics needed by
memory internals. It is not Vanguard's future logging API and must not leak into
higher engine modules.

## Compiled Windows x64 image

The initial target compiles the allocator families, pools, ownership support,
hooks, metrics, Windows diagnostics, and Wwise/ICU allocation adapters.

The full source snapshot also retains platform-specific implementations that
cannot belong to the Windows target. Premake excludes:

- Durango, Orbis, and Linux implementations;
- console-only analyzers and call-stack collectors;
- DLL compatibility operators;
- legacy process-wide `new` and `delete` overrides;
- `hooks.cpp`, whose symbols are already owned by the upstream-listed
  `hookTypes.cpp`;
- `flexibleSystemAllocator.cpp`, which is not part of the active upstream
  Windows implementation.

Exclusion from a target does not mean deletion from the working image.

## Integration corrections

Only narrow corrections required to compile the image with the Vanguard
toolchain are currently allowed:

- obsolete standard-library header replacement;
- removal of an obsolete dynamic exception specification;
- correction of a process-memory report condition;
- safe coexistence with the workspace `NOMINMAX` policy;
- accurate source selection where legacy translation units duplicate symbols.

Further behavioral changes require tests or benchmarks that demonstrate the
old and new behavior.

## Public facade baseline

The first `vanguard::memory` facade provides:

- explicit initialization;
- initialization-state query;
- aligned byte allocation;
- explicit release through an owning `MemoryBlock`.

The facade exposes canonical Vanguard pool identifiers without exposing RED
types, allocator classes, macros, or headers. Pool-backed allocation,
reallocation, budgets, metrics, frame lifecycle, and complete hierarchy
inspection are now Vanguard contracts. Higher-level typed allocation,
allocator-aware containers, specialized arenas, and editor presentation build
on this boundary.

The normal Vanguard implementation delegates through a private
`vanguard::memory::backend` contract. Only its compatibility implementation
knows which imported allocator fulfills that contract.

## Verification

`memorySmoke` builds with Vanguard warnings treated as errors and
passes in Debug, Development, Profile, and Shipping on Windows x64. It verifies:

- initialization and root-pool availability;
- 16-byte and 64-byte alignment;
- writable allocations at representative small sizes;
- correct release and block invalidation;
- rejection of zero-sized, invalid-alignment, and oversized requests.

Imported projects are temporarily compiled at warning level 3 without warnings
as errors. Vanguard-owned facade and test code remains warning-clean under the
strict workspace policy.

## Next steps

1. Supply measured per-platform pool budgets through engine configuration.
2. Connect pool snapshots to Vanguard diagnostics and editor inspection.
3. Add allocator-aware typed construction and Containers integration.
4. Add guarded global allocation routing only after bootstrap order and
   third-party-library behavior are tested.
5. Benchmark fragmentation, contention, peak committed memory, and allocation
   latency before modernizing allocator internals.
