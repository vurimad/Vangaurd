# Imported jobs provenance

Adapter-owned builders, counters, and completion deferrals are
placement-constructed inside `vanguard::memory` allocation envelopes. This
changes only adapter storage ownership; RED Jobs 2 scheduling, fence,
dependency, and destruction semantics are unchanged.

## Scheduler

- Authoritative RED source:
  `D:\root\R6.Root\Mainline\dev\src\common\redJobs2`
- Integration reference:
  `D:\RED Vanguard\dev\src\common\redJobs2`
- Retained image:
  `source/imported/common/redJobs2`
- Active platform: Windows x64

The previous Vanguard image differed from upstream in three runtime files:

- `jobMemoryPools.h` exposes explicit initialization and shutdown entry points;
- `jobMemoryPools.cpp` tears down per-thread job-scope allocators;
- `jobSystem.cpp` invokes that teardown during scheduler shutdown.

Those changes preserve lifecycle work that REDengine originally obtained from
global module shutdown ordering.

Vanguard adds instrumentation-object overloads for maximum-batch parallel-for
dispatch in `jobRunner.h` and `jobBuilder.h`. The overloads execute the existing
RED path and do not alter scheduler algorithms.

Vanguard also adds diagnostic-only queue depth accounting in
`jobDispatcherQueue.h/.hpp`, surfaced internally through `jobDispatcher.h` and
`jobSystem.h/.cpp`. The counter is reserved before a lock-free push, rolled back
on failure, and decremented after a successful pop. It does not participate in
scheduling decisions.

Vanguard adds a narrow `AnalyzeCounter(const Counter&)` forwarding function in
`jobCounterFunctions.h/.cpp`. It delegates directly to the existing RED
`Dispatcher::AnalyzeCounter` implementation; graph traversal and logging are
unchanged.

The imported WinPC memory settings enable RED extended thread registration.
Accordingly, RED Jobs defines its supported worker ceiling as 27 rather than
the non-extended fallback of 11.

## Compatibility dependencies

The shared compatibility depot retains the previous Vanguard-verified images
of:

- `redSystem`
- `redMemory`
- `redMath`
- `redContainers`
- `redIO`
- `redNetwork`
- `redCompression`
- `redCore`
- `redJobs2`

Jobs currently compiles System, Memory, Math, Containers, IO, Core, and Jobs
compatibility projects. Network and Compression remain provenance-complete
source dependencies but are not linked into the public jobs module.

## NVIDIA Tools Extension

RED's Development/Debug profiler enables NVTX on Windows.

- Retained path: `source/imported/external/nvToolsExt`
- Header API version: `NVTX_VERSION 1`
- Original integration source:
  `D:\RED Vanguard\dev\external\nvToolsExt`
- DLL SHA-256:
  `81BD0159A73411791FA1606FE2426B763CC055AC1A50C141CE7E890392ECE10A`
- Import-library SHA-256:
  `23582BD2FCF6131E4137FB8011B394DFF7581A9AD021753E839695217446EF4D`
- Supported target: Windows x64
- Public API exposure: none

The NVIDIA notice and redistribution terms are carried in `nvToolsExt.h`.
Distribution owners must verify that the applicable NVIDIA software agreement
permits redistribution before shipping the DLL.
