# concurrency

The `concurrency` project adapts RED's proven low-level threading image for
Vanguard. It preserves full-barrier atomic operations, thread creation and
lifecycle, synchronization primitives, affinity, naming, priority, sleep, and
yield behavior.

Thread bridge objects use fixed inline storage owned by
`vanguard::concurrency::Thread`. Creating a thread wrapper performs no CRT or
engine-heap allocation. OS thread-stack and handle creation remains the
responsibility of the imported platform thread implementation.

Public and normal source code contain no RED headers or `red::` names.
`concurrencyCompat` is the quarantined bridge to the already imported and
compiled `redSystem` implementation.

The initial Windows x64 contract includes:

- 32-bit and 64-bit integral, boolean, and pointer atomics;
- mutex, spin lock, light mutex, reader/writer lock and spin lock;
- semaphore, condition variable, and manual-reset event;
- scoped exclusive and shared locks;
- inheritable thread objects with RED-compatible lifecycle operations;
- thread identity, main-thread identity, affinity, name, priority, sleep,
  yield, and hardware-concurrency queries.
