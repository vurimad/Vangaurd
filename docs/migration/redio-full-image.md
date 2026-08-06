# RED I/O full-image adaptation

## Decision

Vanguard carries `redIO` as a complete working image. I/O queueing,
cancellation, worker shutdown, overlapped Windows behavior, memory throttling,
and cache ownership are tightly coupled; copying isolated pieces would discard
the production contract while retaining its complexity.

## Provenance and ownership

- The implementation image is `source/imported/common/redIO`.
- Its source/header inventory and contents match
  `D:/root/R6.Root/Mainline/dev/src/common/redIO`; Vanguard owns a separate
  Premake definition because generated project ownership belongs to this repo.
- `redIOCompat` compiles the complete image and debugger visualizer.
- The active Windows build selects the Win32 system-file and worker sources.
  Linux and Orbis sources remain in the image but are excluded from a Windows
  target, matching RED's platform selection.
- `source/io/compat` translates lifecycle and global-system access.
- `source/io/include/vanguard/io/io.hpp` is the supported engine boundary.
- `source/jobs` consumes `redIOCompat` but no longer owns its project.

Original RED copyright and provenance remain on imported files.

## Preserved RED contracts

- synchronous native files and platform handles;
- asynchronous file-handle cache with explicit reference ownership;
- GAME, UI, AUDIO, and video queues with weighted service;
- async tokens, callback results, caller-owned and allocator-owned buffers;
- cancellation/loading contexts and observer-distance updates;
- worker wake, overlapped I/O, queue sorting, bulk submission, and shutdown;
- the bounded I/O allocator, cache reuse, throttling, and runtime metrics;
- byte, queue, in-flight, memory, final-build, and profiler statistics.

The compatibility image retains RED's assertions and fatal misuse behavior.
The Vanguard boundary does not silently repair malformed tokens, invalid
lifetimes, unmatched bulk boundaries, or file-handle misuse.

## Vanguard decisions

- Normal call sites use `vanguard::io`; `::io` and `red::` remain inside the
  adaptation boundary.
- Memory, Diagnostics, and Containers are explicit initialization
  prerequisites.
- The RED global worker is exposed as `vanguard::io::System()`.
- Restart after shutdown is rejected because RED's global `AsyncIO` object is a
  one-lifetime service.
- Filesystem/VFS policy is deliberately deferred to `source/filesystem`.

No I/O scheduling, worker, allocator, cache, or file algorithm was invented in
this phase.
