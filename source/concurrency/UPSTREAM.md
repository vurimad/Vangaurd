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

All audited files are byte-identical between original RED and the previous
Vanguard integration except `redThreadsThread.cpp`. The previous integration
corrected its VTune include and library paths; that corrected file is already
present in the shared imported image.
