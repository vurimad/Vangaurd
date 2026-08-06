# RED Vanguard

RED Vanguard is a high-performance, data-oriented open-world game engine and
editor. It is a new engine with its own contracts, formats, terminology, and
architecture.

REDengine is used as an implementation source and reference. Mature low-level
subsystems may enter as complete working forks, then compile behind quarantined
compatibility boundaries. Vanguard modules expose only Vanguard contracts,
formats, terminology, and dependencies; RED implementation details may not
leak into normal engine code.

The active low-level baseline consists of `system`, `memory`, `diagnostics`,
`concurrency`, `math`, `containers`, `io`, `filesystem`, `serialization`,
`packages`, `jobs`, `resources`, `streaming`, `reflection`, `schemas`, and
`crypto`. The backend-independent `shaders`, `pipelines`, `pipelineCache`, and
`meshes` modules establish cooked GPU-resource contracts without requiring a
graphics API. The headless `assets` module now establishes source identity,
compiler registration, dependency discovery, deterministic build
fingerprints, derived-data caching, and cooked artifact emission.
Shader-derived resource schemas are validated through `streaming`. Imported RED implementation images
compile only behind compatibility projects and Vanguard public adaptation
boundaries. The Vanguard-owned `.vpak` runtime container is documented in
[`docs/formats/vpak-format.md`](docs/formats/vpak-format.md).
The reflected `VOBJ` layout is documented in
[`docs/formats/vobj-format.md`](docs/formats/vobj-format.md).
The streamable cooked mesh layout is documented in
[`docs/formats/vmesh-format.md`](docs/formats/vmesh-format.md).
The cooking pipeline is documented in
[`docs/architecture/asset-cooking.md`](docs/architecture/asset-cooking.md).
The persistent derived-data record is documented in
[`docs/formats/vddc-format.md`](docs/formats/vddc-format.md).
The editor project workspace, disposable derived-data tree, numbered runtime
packages, and `DATA000.vpak` bootstrap contract are documented in
[`docs/architecture/project-and-runtime-layout.md`](docs/architecture/project-and-runtime-layout.md).

Engineering rules and the purpose of the project are defined in
[`docs/engineering/aaa-standards.md`](docs/engineering/aaa-standards.md).
The planned repository structure is recorded in
[`docs/architecture/repository-layout.md`](docs/architecture/repository-layout.md),
and use of the C++ standard library and third-party code is governed by
[`docs/engineering/cpp-and-dependencies.md`](docs/engineering/cpp-and-dependencies.md).

## Generate Visual Studio projects

Install Premake 5, place `premake5.exe` in the repository root or
`tools/premake/`, then run:

```bat
generate-vs2022.bat
```

Generated projects are written to `build/projects/vs2022`.
