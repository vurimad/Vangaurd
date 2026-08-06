# RED concurrency image

## Decision

Vanguard reuses the complete, proven RED low-level concurrency implementation
already retained inside `source/imported/common/redSystem`. The implementation
is compiled once by `redSystemCompat`; it is not duplicated or rewritten
piecemeal.

The supported dependency path is:

```text
system
   |
redSystemCompat
   |
concurrencyCompat
   |
concurrency
```

`concurrencyCompat` is the only concurrency project allowed to include RED
headers or name RED types. Normal engine code includes only
`vanguard/concurrency/*`.

## Imported image

The coherent upstream image contains:

- 32-bit and 64-bit platform atomics;
- thread creation, naming, affinity, priority, joining, and detaching;
- mutex, spin lock, light mutex, and reader/writer synchronization;
- semaphores, condition variables, and manual-reset events;
- thread identity, sleeping, yielding, and main-thread tracking.

Source provenance and the one known previous-integration correction are recorded
in `source/concurrency/UPSTREAM.md`.

## Vanguard contract

The public API deliberately preserves the mature operational semantics and
recognizable method names needed by the future jobs adaptation, while removing
RED namespaces, headers, build definitions, and ownership from consumers.

Atomics are a direct Vanguard-named adaptation of RED's Windows interlocked
implementation. Synchronization and threads delegate through fixed private
storage and a bridge object to the copied RED implementation. Compile-time size
checks prevent silent ABI drift.

The first contract supports Windows x64. A new platform must provide and test
equivalent semantics before being listed as supported.

## Validation

`concurrencyTests` checks:

- main-thread identity and hardware concurrency;
- multiple RED-backed threads incrementing a shared atomic;
- recursive mutex behavior;
- shared/exclusive reader-writer spin locking;
- semaphore signaling;
- manual-reset event signaling.

The public and ordinary private source directories are scanned to ensure RED
names and headers remain confined to `compat`.

## Next dependency

This phase establishes the primitives expected by RED's jobs implementation.
When jobs exposes a missing memory or concurrency operation, Vanguard will
materialize that operation at the owning public boundary instead of allowing a
RED API to escape.
