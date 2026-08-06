# Repository layout

The repository tree expresses ownership. It is not a copy of REDengine's
historical depot layout.

```text
<repository-root>/
|-- .github/                    CI and repository automation
|-- build/
|   |-- premake/              Shared Premake policy
|   |-- projects/             Generated IDE projects; ignored
|   `-- obj/                  Generated intermediates; ignored
|-- configs/
|   |-- engine/
|   |-- editor/
|   `-- platforms/
|-- docs/
|   |-- architecture/
|   |-- engineering/
|   |-- formats/
|   |-- development/
|   `-- migration/
|-- external/
|   |-- manifests/
|   `-- patches/
|-- schemas/
|   |-- reflection/
|   |-- resources/
|   |-- messaging/
|   `-- settings/
|-- shaders/
|   |-- source/
|   |-- include/
|   `-- tests/
|-- data/
|   |-- engine/
|   |-- editor/
|   `-- tests/
|-- source/
|   |-- imported/
|   |   `-- common/            Shared quarantined RED source images
|   |-- system/
|   |-- diagnostics/
|   |-- memory/
|   |-- containers/
|   |-- math/
|   |-- concurrency/
|   |-- jobs/
|   |-- io/
|   |-- filesystem/
|   |-- serialization/
|   |-- crypto/
|   |-- reflection/
|   |-- resources/
|   |-- packages/
|   |-- schemas/
|   |-- assets/
|   |-- shaders/
|   |-- materials/
|   |-- pipelines/
|   |-- pipelineCache/
|   |-- world/
|   |-- streaming/
|   |-- rendering/
|   |-- physics/
|   |-- animation/
|   |-- audio/
|   |-- input/
|   |-- navigation/
|   |-- networking/
|   |-- gameplay/
|   |-- editor/
|   |-- tools/
|   `-- applications/
|-- tests/
|   |-- unit/
|   |-- integration/
|   |-- performance/
|   |-- determinism/
|   |-- formats/
|   `-- fixtures/
|-- benchmarks/
|-- samples/
|-- scripts/
|-- tools/
|-- bin/                       Generated binaries; ignored
`-- premake5.lua
```

## Directory meanings

`source/system` is the only universal dependency. It owns compile-time platform
facts and emergency failure handling, not general engine services.

`source/diagnostics` owns logging and diagnostic sinks. Its bootstrap logger is
independent of Vanguard memory, while later telemetry, tracing, crash evidence,
and editor ingestion build on the same public contract.

`source/memory` through `source/resources` form the low-level engine substrate,
but there is intentionally no vague physical `foundation` or catch-all `core`
directory.

`source/assets` owns authoring, importing, generation, cooking, dependency
tracking, packaging, and derived data. It must remain usable headlessly.

`source/editor` contains only editor-specific orchestration and presentation.
Editor code may consume runtime modules; runtime modules may not consume editor
code.

`source/tools` contains headless executable implementations. `tools/` at the
repository root contains pinned development executables such as Premake.

`source/applications` contains composition roots. Applications choose and
initialize modules but do not become shared implementation libraries.

## Module shape

When activated, a library module uses:

```text
source/<module>/
|-- include/vanguard/<module>/  Deliberate public API
|-- src/                        Private implementation
|-- tests/                      Fast module contract tests
|-- benchmarks/                 Module-local performance baselines
|-- premake5.lua
`-- README.md                   Contract, exclusions, ownership, threading
```

Placeholder files only preserve planned directories. A directory does not
become an accepted module until its contract, Premake project, implementation,
and tests are added.

## Visual Studio organization

Premake owns the generated Visual Studio hierarchy. Projects are grouped by
architectural role:

```text
Engine/
|-- Runtime/
|-- Tools/
`-- Compatibility/
Tests/
|-- System/
|-- Memory/
|-- Containers/
|-- Diagnostics/
|-- Concurrency/
|-- Jobs/
|-- IO/
|-- Filesystem/
|-- Serialization/
|-- Packages/
|-- Reflection/
|-- Resources/
|-- Streaming/
|-- Schemas/
|-- Crypto/
|-- Assets/
|-- Shaders/
|-- Pipelines/
`-- Pipeline Cache/
Benchmarks/
`-- Jobs/
```

Runtime projects organize files under `Public API` and `Source`.
Compatibility-facing translation units use `Compatibility`. Imported working
images use explicit filters such as `Imported/RED Memory/Public` and
`Imported/RED System/Private`. Test executables place their files under
`Tests`.

Generated `.sln`, `.vcxproj`, and `.vcxproj.filters` files must not be edited
manually; changes belong in the module's `premake5.lua`.
