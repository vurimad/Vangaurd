# REDsystem study: Vanguard phase one

## Scope

This record covers the REDengine material studied while defining the first
`VanguardSystem` contract. It is not a claim that the complete RED `redSystem`
module has been migrated.

## REDengine sources studied

- `redSystem/include/types.h`
- `redSystem/include/architecture.h`
- `redSystem/include/compilerExtensions.h`
- `redSystem/include/settings.h`
- `redSystem/include/assert.h`
- `redSystem/include/redSystemApi.h`
- `redSystem/src/assert.cpp`
- `redSystem/src/dbgUtilsWin32.cpp`
- `redSystem/premake5.lua`

The complete module file inventory was also reviewed to identify responsibilities
that should not remain coupled.

## Concepts retained

- Fixed-width types are established at the lowest dependency level.
- Platform, architecture, compiler, and configuration decisions are explicit.
- Assertions distinguish disabled diagnostics from always-enabled fatal paths.
- Programmer assertions are fatal when enabled, matching the studied RED
  contract; recoverable reporting has a separate Vanguard `VG_ENSURE` API.
- Emergency diagnostics can operate before higher engine systems initialize.
- Debugger breaks are separated from process termination.
- Platform implementations are hidden behind a small public contract.

## Historical assumptions rejected

- Global primitive aliases.
- A configurable engine-wide `Char` type and `TXT()` macro.
- Broad `redSystem` ownership of logging, files, GUIDs, encoding, threading,
  clocks, crash reporting, and utility containers.
- Legacy compiler and pre-C++20 compatibility branches.
- Copied console SDK checks before Vanguard has corresponding platform targets.
- Native DLL decoration in every low-level public declaration.
- Precompiled-header dependence for the lowest-level library.
- Variadic `printf` assertion macros as the long-term formatting contract.

## Vanguard decisions

- Canonical code namespace: `vanguard`.
- Canonical text encoding: UTF-8; Windows wide strings remain an API boundary.
- Language level: C++20 for this phase.
- Initial desktop target: Windows x64, with explicit Linux and ARM64 detection
  retained for later platform implementations.
- Build configurations: Debug, Development, Profile, and Shipping.
- Warnings are errors for Vanguard-owned code.
- Emergency output uses bounded stack storage and direct platform calls.
- Public headers reside under `include/vanguard/system`.

## Deferred work

- A structured result/error model.
- Testable assertion-handler injection.
- Crash context, call stacks, minidumps, and recovery attachments.
- Structured logging and sinks.
- Platform virtual memory.
- Clocks, threads, atomics, and synchronization.
- Hashing, identifiers, and text conversion.

Each deferred item requires its own contract and migration record.
