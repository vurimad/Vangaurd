# Imported concurrency provenance

The Vanguard thread bridge differs only in ownership plumbing: it is
placement-constructed in fixed Vanguard inline storage instead of acquiring an
untracked CRT block. RED thread behavior and explicit lifecycle remain
unchanged.

The complete concurrency image is retained in the shared imported `redSystem`
snapshot at `source/imported/common/redSystem`.

- Authoritative source:
  `D:\root\R6.Root\Mainline\dev\src\common\redSystem`
- Integration reference:
  `D:\RED Vanguard\dev\src\common\redSystem`
- Active platform: Windows x64

The coherent image includes RED atomic, platform atomic, thread, Windows thread,
thread types, common synchronization, thread identity, and reader/writer spin
lock headers and implementations.

`include/vanguard/concurrency/atomic.hpp` now delegates directly to the unchanged
`redThreadsAtomicWinAPI.inl` operations. The former duplicated intrinsic bodies
are removed. The facade preserves its method names, supported widths, deleted
copy operations, return conventions, and 32-bit boolean representation. Reads
use RED's `FetchValue`; modifications continue to use interlocked operations.
The workspace explicitly selects `/volatile:ms` for the MSVC x64 load contract.
Only the prerequisite platform/type headers are included, not the entire
threading or logger interface. See `docs/development/low-level-reuse-audit.md`
for the source-review boundary and remaining follow-ups.

All audited files are byte-identical between original RED and the previous
Vanguard integration except `redThreadsThread.cpp`. The previous integration
corrected its VTune include and library paths; that corrected file is already
present in the shared imported image.
