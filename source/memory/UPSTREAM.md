# Imported memory provenance

## RED memory image

- Source: `D:\root\R6.Root\Mainline\dev\src\common\redMemory`
- Imported file count: 352
- Imported byte count: 1,183,797
- Import date: 2026-07-27
- SHA-256 of the sorted `relative-path<TAB>file-SHA-256` manifest:
  `CDB0CF4CDC890E893467C3E494C5AA40CF986F6191D2D4EFDD2AFCC3FDB1EFF8`

The complete image is retained under `source/imported/common/redMemory`. Windows x64 builds
exclude other platform implementations but do not remove them from the source
snapshot.

Three previously proven integration corrections from the earlier Vanguard
attempt are applied to the working image:

- Replace obsolete MSVC `<xstddef>` use with `<cstddef>`.
- Remove a legacy dynamic exception specification macro.
- Use the already-computed process-report filtering decision.

The private compatibility image also guards its `NOMINMAX` declaration so it
can coexist with Vanguard workspace policy. The unlisted legacy `hooks.cpp`
translation unit is retained in the snapshot but excluded because
`hookTypes.cpp`, which is part of RED's original project manifest, owns the same
symbols.

## Private REDsystem compatibility image

- Integration source: `D:\RED Vanguard\dev\src\common\redSystem`
- Original RED source:
  `D:\root\R6.Root\Mainline\dev\src\common\redSystem`
- Imported file count: 162
- Purpose: private compatibility dependency for the complete RED-derived memory
  implementation.

The compatibility image contains the earlier Vanguard port's small set of
build, compiler, JSON, version-identity, and thread corrections. It is not the
public Vanguard system API. New Vanguard modules must include
`vanguard/system/*`, not REDsystem headers.

## Policy

Imported code keeps its original copyright notices. Semantic adaptations are
recorded here or in later migration records. No `red::` name from the imported
implementation is considered a permanent Vanguard public contract.
