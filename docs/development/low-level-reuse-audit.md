# Low-level reuse audit — 2026-09-06

## Scope and validation

Source-only follow-up to the rendering lock audit: reuse the existing imported atomic operations and inspect Memory, Concurrency, Jobs, Containers, IO, Filesystem, Math, Diagnostics, and System for duplicated foundations. No compilation, Premake generation, tests, disassembly, or benchmarks were run. This is not a proof that every synchronization algorithm in the engine is correct. Shared unrelated worktree changes were left alone.

## Atomic correction implemented

`source/concurrency/include/vanguard/concurrency/atomic.hpp` previously implemented every operation directly with intrinsics. `GetValue()` used compare-exchange even when no ownership change was intended. It now calls the existing `red::WinAPI::AtomicOps32`, `AtomicOps64`, and `AtomicOpsPtr` from `source/imported/common/redSystem/include/redThreadsAtomicWinAPI.inl` inline. The imported file is unchanged; this is reuse, not a copied second implementation or an out-of-line backend thunk.

The public API, interlocked modifications, return-old/return-new conventions, and four-byte boolean storage remain. Storage alignment is explicit and size/alignment assertions cover all representations. `GetValue()` now follows the imported `FetchValue()` aligned load. The workspace explicitly selects `/volatile:ms`; acquire ordering relies on the supported MSVC x64 compiler contract, not ISO C++ volatile semantics. External consumers must use the same option. See [Microsoft's volatile interpretation documentation](https://learn.microsoft.com/en-us/cpp/build/reference/volatile-volatile-keyword-interpretation) and [acquire/release guarantees](https://learn.microsoft.com/en-us/cpp/cpp/volatile-cpp).

Source review covered the particularly sensitive RHI resource identity/AddRef/Release paths, retirement queue sequence publication, epoch/bucket serialization, allocator generation-terminal checks, and representative resource-registry/pipeline-cache publication. Identity ownership still changes through CAS; queue payload publication still uses interlocked `SetValue`; epoch transactions still hold their existing lock. No read in those inspected paths required treating `GetValue()` as a full store/load fence. The repository-wide call-site inventory also contains statistics, cancellation, and lifetime probes; it is not a substitute for eventual concurrent validation.

This change does not remove allocator locks, Jobs telemetry increments, submission locks, or reference counting. It reduces read-side RMW traffic; no measured frame-time improvement is claimed.

## Foundation inventory

| Area | Actual implementation / evidence | Decision |
| --- | --- | --- |
| Memory | `source/memory/compat/red_memory_backend.cpp` calls imported pool `AllocateAligned`, `ReallocateAligned`, `Free`, and pool accounting; `memory/pool.hpp` exposes the compile-time pool machinery. | Already reused. Keep Vanguard pool routing and allocation ownership. |
| Locks, events, threads | `source/concurrency/compat/red_concurrency_backend.cpp` constructs imported mutexes/spinlocks/RW locks/events and derives `ThreadBridge` from `red::Thread`. | Already reused. Fixed-storage lifetime adaptation is not a second lock implementation. |
| Atomics | Former duplicated intrinsics in `concurrency/atomic.hpp`. | Corrected in this pass as described above. |
| Jobs | `source/jobs/compat/red_jobs_backend.cpp` uses the imported scheduler, builders and counters. It additionally maintains wrapper lifetime/statistics counters. | Already reused underneath. Extra counter traffic remains a separate performance review, not justification to replace the scheduler. |
| Containers | `source/containers/include/vanguard/containers/containers.hpp` exposes the imported container image, including lock-free queues. | Already reused. |
| Filesystem | `source/filesystem/compat/red_filesystem_backend.cpp` owns an imported `CFileManager`; the public filesystem surface adapts the imported image. | Already reused, not `std::filesystem` or an independently implemented file manager. |
| Asynchronous IO | `source/io/compat/red_io_backend.cpp` initializes imported `::io` and exposes `::io::GAsyncIO`; public types come from the imported IO headers. | Already reused. |
| Math | `source/math/UPSTREAM.md` records the reproducibly adapted inline/SIMD image under `source/math/adapted`. | Adapted code rather than per-operation delegation is intentional here. |
| Logging | `source/diagnostics/compat/red_logger_backend.cpp` uses the imported logger and sinks, but its callback and user-data slots are `std::atomic`. | Small remaining primitive-reuse candidate; not changed here. |
| System timing | `source/system/src/time.cpp` directly calls QPC/frequency on Windows and monotonic clock APIs on Linux. | Real independent tiny OS wrapper; no evidence that replacing it improves the hot path. Keep the low-layer dependency boundary for now. |
| Emergency diagnostics | `source/system/src/assert.cpp` and `debug_win32.cpp` use bounded formatting and direct output/termination. | Deliberate initialization-independent fatal path; do not route it through the initialized logger. |

The broad production-source pattern scan excluded imported/adapted/reference snapshots, tests, and benchmarks. It found the two Diagnostics `std::atomic` slots and no additional direct `_Interlocked` implementations outside the imported code after the correction. This does not mean every platform API or every custom data structure was exhaustively reviewed.

## Bounded follow-ups, not automatic rewrites

1. **Diagnostics callback slots:** the compatibility source already sees the imported atomics, so reuse need not introduce a dependency on the public Concurrency library. Preserve function-pointer handling and the callback/user-data lifetime contract. Two independent atomic slots do not guarantee an indivisible pair during concurrent replacement; inspect the setter's permitted lifecycle before treating a type substitution as a complete correction. Do not claim an automatic performance win: the existing loads already have acquire semantics.
2. **RHI retirement queue:** `source/rhi/nvrhi/src/resource_lifetime.cpp` contains its own bounded MPMC `RetirementQueue`, although imported lock-free containers are available. Compare bounded capacity, allocation behavior, failed-push recovery, and epoch accounting before deciding whether an existing container is a drop-in replacement. No replacement was made merely because both are called queues.
3. **Jobs wrapper counters:** the scheduler is already imported; review whether each added global modification is needed for shutdown safety or only telemetry. Do not remove lifecycle protection with statistics.

These are audit findings, not new render-graph prerequisites. The next rendering synchronization pass can remain focused on the previously identified actual lock contention.
