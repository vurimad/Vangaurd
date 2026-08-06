# Imported logger provenance

The RED logger is a coherent slice of the complete `redSystem` image already
retained at `source/imported/common/redSystem`.

- Authoritative source:
  `D:\root\R6.Root\Mainline\dev\src\common\redSystem`
- Integration reference:
  `D:\RED Vanguard\dev\src\common\redSystem`
- Import date of the shared image: 2026-07-27

The logger slice consists of:

- `include/log.h`
- `include/logMessage.h`
- `include/loggerSink.h`
- `include/loggerFileSink.h`
- `src/log.cpp`
- `src/logMessage.cpp`
- `src/logger.h`
- `src/logger.cpp`
- `src/loggerWorker.h`
- `src/loggerWorker.cpp`
- `src/loggerLocklessQueue.h`
- `src/loggerLocklessQueue.hpp`
- `src/loggerSink.cpp`
- `src/loggerFileSink.cpp`

All fourteen files are byte-for-byte identical between the authoritative source
and the previous Vanguard integration reference. They are compiled once inside
`redSystemCompat`; duplicating them in the diagnostics tree would
produce duplicate global logger state and symbols.
