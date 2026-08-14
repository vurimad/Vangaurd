# Vanguard repository layout

Vanguard separates architectural ownership from build output and third-party provenance. New code must follow the layouts
below so physical folders, include paths, Premake projects, and Visual Studio filters describe the same boundaries.

## Repository roots

| Path | Ownership |
|---|---|
| `source/` | Engine modules and contained compatibility adaptations |
| `runtime/` | Runtime executable composition and platform entry points |
| `editor/` | Editor executable composition and platform entry points |
| `tools/` | Standalone command-line and asset-pipeline programs |
| `external/` | Pinned, repository-contained third-party dependencies |
| `games/` | Engine-agnostic game projects used with Vanguard |
| `shaders/` | Shader source and shared shader includes |
| `schemas/` | Source schemas used by format and asset tooling |
| `configs/` | Engine and tool configuration checked into source control |
| `data/` | Repository-owned bootstrap/test data, not a runtime working directory |
| `docs/` | Repository-wide architecture and engineering documents |
| `scripts/` | Generation, validation, migration, and maintenance scripts |
| `samples/`, `benchmarks/`, `tests/` | Cross-module examples, performance programs, and integration tests |
| `build/` | Generated projects, objects, libraries, and test executables |
| `bin/` | Deployable runtime, editor, and tool layouts |
| `cache/` | Local derived data and other disposable caches |

`build/`, `bin/`, and `cache/` are outputs. Source code and durable authored data never belong there.

## Engine module layout

```text
source/<module>/
  include/vanguard/<module>/   public API
  private/vanguard/<module>/   private cross-translation-unit API
  src/                         implementation translation units only
  tests/                       focused module tests
  docs/                        module design and implementation documents
  compat/                      contained compatibility adapter, when required
  premake5.lua
  README.md
  UPSTREAM.md                  implementation lineage, when required
```

Public consumers include `<vanguard/<module>/<header>.hpp>`. Private headers use the same qualified style but are visible only
to the owning project and its explicitly declared compatibility adapter. A header must not be placed beside `.cpp` files in
`src/`. `include/vanguard/<module>` is namespace structure under one include root; it is not an accidental include-inside-include
layout and must not be flattened.

Contained backend subprojects use the same rule beneath their owner, for example `source/window/sdl` and `source/rhi/nvrhi`.
Imported or mechanically adapted bodies retain their upstream physical structure so updates remain auditable; Vanguard-facing
adapters still follow the module layout above.

## Applications and tools

Applications and standalone tools do not expose an engine public API. Their reusable internal contracts live under
`private/vanguard/<application>/`, implementations under `src/`, and operating-system entry points under
`platform/<platform>/`. Tests may include the private API explicitly through their own Premake project.

## Visual Studio organization

Premake filters mirror ownership rather than raw directories:

- `Public API`
- `Private API`
- `Source`
- `Platform/<platform>` when applicable
- `Compatibility` or `Imported` only for contained adaptations
- `Tests`
- `Documentation`

The repository generation audit rejects Vanguard-owned headers in `src/` and duplicate nested include roots. This prevents the
layout from degrading as modules grow.
