# concurrency

The `concurrency` project adapts RED's proven low-level threading image for
Vanguard. It preserves full-barrier atomic modifications, thread creation and
lifecycle, synchronization primitives, affinity, naming, priority, sleep, and
yield behavior.

Thread bridge objects use fixed inline storage owned by
`vanguard::concurrency::Thread`. Creating a thread wrapper performs no CRT or
engine-heap allocation. OS thread-stack and handle creation remains the
responsibility of the imported platform thread implementation.

`atomic.hpp` is the inline public adaptation boundary for the imported RED
WinAPI atomic operations, like Memory's compile-time pool boundary. Callers
continue to use only `vanguard::concurrency::Atomic`. Other threading operations
use `concurrencyCompat`, the quarantined bridge to compiled `redSystem`.

On the supported MSVC x64 target, `GetValue()` uses the imported aligned volatile
load with acquire semantics under the workspace's explicit `/volatile:ms` setting.
It is not a full fence or an ownership operation. All modifications, including
`SetValue()`, still use the imported full-barrier interlocked operations. No
out-of-line atomic backend calls, heap allocation, or additional storage are
introduced. Boolean storage remains 32 bits to preserve the existing layout.
Consumers built outside this workspace must also use `/volatile:ms`; this is a
Windows compiler contract, not a portable ISO C++ volatile synchronization claim.

The initial Windows x64 contract includes:

- 32-bit and 64-bit integral, boolean, and pointer atomics;
- mutex, spin lock, light mutex, reader/writer lock and spin lock;
- semaphore, condition variable, and manual-reset event;
- scoped exclusive and shared locks;
- inheritable thread objects with RED-compatible lifecycle operations;
- thread identity, main-thread identity, affinity, name, priority, sleep,
  yield, and hardware-concurrency queries.
