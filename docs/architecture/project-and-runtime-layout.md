# Editor project and runtime image layout

This document defines the separation between Vanguard's editable project workspace and its immutable shipped runtime image. Authored source data, generated derived data, and packaged runtime data are different ownership domains and must not be mixed.

## Editor project workspace

```text
MyGame/
|-- MyGame.vproject
|-- Assets/
|   |-- Worlds/
|   |-- Meshes/
|   |-- Textures/
|   |-- Materials/
|   |-- Shaders/
|   |-- Audio/
|   `-- Prefabs/
|-- DerivedData/
|   |-- Windows/
|   |   |-- Development/
|   |   |   |-- Artifacts/
|   |   |   |-- Records/
|   |   |   `-- Index/
|   |   `-- Shipping/
|   `-- Shared/
|-- Config/
|   |-- Project/
|   |-- Cooking/
|   `-- Packaging/
|-- Plugins/
|-- Intermediate/
|-- Saved/
`-- Builds/
    `-- Windows/
        |-- Development/
        `-- Shipping/
```

`MyGame.vproject` is a shared authoring/tool project definition, consumed by the editor and headless tools through `source/projects`. It establishes project identity, source and derived-data roots, target platforms, plugins, cooking policy, package construction policy, and default editor/runtime worlds. It is never required by a shipped runtime.

The normative grammar, validation levels, identity rules, and transactional creation contract are defined in [`../formats/vproject-format.md`](../formats/vproject-format.md). The editor and `nanovanguard` must consume the same shared parser and validator; neither product may maintain a private interpretation.

### Assets

`Assets` is the authoritative source-data tree. It contains original files such as PNG, TIFF, OpenEXR, FBX, glTF, WAV, shader source, and authored world or prefab documents. Cooked `.vxxx` resources must not be written beside these sources.

Every independently managed source asset has a sidecar whose name includes the source extension:

```text
tower.fbx
tower.fbx.vmeta

tower_albedo.png
tower_albedo.png.vmeta
```

Including the source extension avoids collisions between different source formats with the same stem. A `.vmeta` sidecar owns the stable `AssetId`, selected importer and importer version, authored import settings, labels, explicit cooking overrides, and other source-control-relevant editor metadata. Generated fingerprints and transient build state belong in `DerivedData`, not in `.vmeta`.

Moving a source file together with its `.vmeta` preserves its `AssetId`. Worlds, prefabs, materials, and other resources refer to stable identity rather than the current source path.

### DerivedData

`DerivedData` is generated, disposable, and reconstructable from `Assets`, metadata, project configuration, and tools. It is the physical home of the local derived-data cache and contains successfully cooked `.vworld`, `.vcell`, `.vprefab`, `.vmesh`, `.vtex`, `.vmat`, `.vshader`, `.vaudio`, and future Vanguard resource artifacts.

Artifacts should be content-addressed rather than physically mirror source paths. A representative physical path is:

```text
DerivedData/Windows/Development/Artifacts/7A/31/<fingerprint>.vtex
```

The shared source asset registry maintains the following mapping; the editor asset browser presents it rather than owning a separate registry:

```text
AssetId -> current source path and metadata -> build fingerprint -> current derived artifact
```

Deleting `DerivedData` may cause a full recook but must never lose authored work. `Intermediate` is reserved for incomplete or temporary compiler output, while `DerivedData` contains only successfully published cache records and artifacts.

### Builds

`Builds` contains complete deployable images assembled from selected derived artifacts. It is output, not an editor asset root. VPAK construction consumes committed derived artifacts and package policy; it does not import source assets while writing an archive.

## Shipped runtime image

The normal runtime image contains the executable and a small set of opaque, numbered package files:

```text
MyGame/
|-- MyGame.exe
|-- DATA000.vpak
|-- DATA001.vpak
|-- DATA002.vpak
|-- DATA003.vpak
`-- DATA004.vpak
```

The runtime image does not include `.vproject`, source assets, `.vmeta`, `DerivedData`, package recipes, or compiler intermediates. Package filenames carry no world, asset-category, or feature semantics. A VPAK is a physical storage container, not a world or source directory.

### DATA000 package-set root

`DATA000.vpak` is the root of the runtime package set and replaces a separate boot-manifest file. A directly readable package-set boot record identifies the game and build before the resource system is running. The record contains:

- game/project identity and build identity;
- target platform and format/engine compatibility requirements;
- the catalog of required and optional `DATA###.vpak` files;
- package IDs, integrity hashes, mount priorities, and package roles;
- the startup `.vworld` and project-default `.vinput` typed resource identities;
- encryption key identifiers and signature information.

The boot record is not an ordinary streamed resource. The filesystem and low-level package reader must be able to find, bound-check, and validate it without first mounting the package or invoking a resource decoder. A small fixed-location root structure may point to the bounded package-set catalog elsewhere within `DATA000.vpak`.

`DATA000.vpak` should remain relatively small and stable, containing the package-set record, essential defaults, startup presentation resources, and other true bootstrap data. Bulk world cells, textures, meshes, audio, and optional content belong in subsequent numbered packages.

All required package indexes may be mounted during startup without reading package payloads into memory. Actual resource segments and pages remain demand-loaded through Resource Streaming.

### Runtime startup

```text
MyGame.exe
    -> locate and open DATA000.vpak
    -> read and validate the package-set boot record
    -> establish game identity, target, and compatibility
    -> mount DATA000 and the required numbered packages
    -> register concrete resource decoders
    -> request the startup .vworld by ResourceId
    -> load mandatory world dependencies
    -> materialize the initial world cells
    -> begin ordinary spatial and resource streaming
```

The executable locates the game root from an explicit platform launch argument when supplied, otherwise from its defined installation relationship. It must not scan arbitrary directories looking for a plausible `DATA000.vpak`.

At runtime, `PackageSetMount` owns the validated package metadata for the mounted image. It derives canonical numbered filenames from the
DATA000 catalog, distinguishes missing optional packages from missing required packages, checks package identity and index integrity, and
publishes the complete reader set to `ResourceStreamer` in one transaction. DATA000 and its external packages are therefore never
partially visible to resource requests. Stored segment CRCs remain the normal on-demand payload integrity check; full package SHA-256 is
reserved for explicit installation/paranoid verification rather than every launch.

### Numbered package policy

The number of VPAKs is determined by configured target size, platform constraints, installation groups, patch stability, streaming affinity, and deterministic placement. One VPAK per world and one VPAK per asset category are both rejected policies.

Resource-to-package assignment must be stable across incremental builds. Changing one texture must not reshuffle unrelated resources between `DATA002.vpak` and `DATA003.vpak`. Rebalancing is an explicit packaging operation. Runtime systems resolve ResourceIds and never depend on the physical package number that currently owns a resource.

Higher-priority numbered packages may provide update or override layers, but their role remains package-set metadata rather than filename semantics.

### Writable runtime data

The installed runtime image is treated as immutable and may reside in a read-only or store-managed directory. Saves, logs, user configuration, crash reports, and telemetry buffers belong in a platform user-data location such as:

```text
%LOCALAPPDATA%/<Company>/<Game>/
|-- Config/
|-- Logs/
|-- Saves/
`-- Crashes/
```

## Editor startup and play

```text
VanguardEditor.exe MyGame.vproject
    -> read the project definition
    -> establish Assets and DerivedData roots
    -> load and validate the asset metadata/index
    -> detect missing or stale derived artifacts
    -> cook required assets
    -> open the configured editor world
```

Editor play uses the same ResourceId, decoder, resource-streaming, world-loading, and materialization contracts as the shipped runtime. Current `DerivedData` artifacts form a high-priority loose-resource overlay above existing development packages:

```text
Existing DATA packages       priority 0
Current DerivedData output   priority 100
```

This permits an incrementally recooked resource to replace a packaged version immediately without rebuilding every VPAK. The overlay changes physical resolution only; it does not create editor-only resource identity or a second runtime loading architecture.

## End-to-end ownership

```text
Assets + .vmeta
        -> import and cook
DerivedData/*.vxxx
        -> deterministic package assembly
DATA000.vpak + DATA001...N.vpak
        -> runtime resource and world streaming
Flecs-backed game-world materialization
```
