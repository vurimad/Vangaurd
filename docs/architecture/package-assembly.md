# Package selection and assembly

Vanguard package assembly is the headless layer that converts a versioned `VPMF` manifest and committed `VADI` build state into a
validated, atomically published VPAK. It does not compile assets and does not interpret texture, mesh, material, shader, world, or audio
schemas.

## RED-derived architecture

The implementation was designed after studying RED:

- `backendDataBuild/src/archivesCommon.h/.cpp`
- `backendDataBuild/src/archivesResourceDistribution.h/.cpp`
- `backendDataBuild/src/archivesResourceProcessing.h/.cpp`
- `backendDataBuild/src/archivesBuilding.h/.cpp`
- `common/archive/src/build.h/.cpp`

Retained principles include explicit archive content groups, dependency-database-driven selection, deterministic resource ordering,
wide resource processing with ordered publication, segment-level compression, payload-before-index writing, a zero/invalid header until
completion, read-only runtime metadata, and detailed build identity.

For runtime bootstrap, RED's game depot was also inspected for its ordered archive-set opening, strict archive-header validation, separate
synchronous metadata and asynchronous payload handles, override ordering, and exclusion of already-shadowed resources. Vanguard retains
the useful separation between validating storage metadata and requesting resource payloads, but `DATA000.vpak` now owns an explicit,
bounded package-set catalog. Runtime startup therefore does not scan hardcoded content, patch, DLC, language, or mod directories to infer
the package set.

Vanguard rejects RED archive sets, depot paths, `RADR`, RED file hashes, timestamp identity, language sidecars, resource-path caches, and
archive hash sidecars. `VPMF`, `VADI`, cooked artifact descriptors, SHA-256 build identity, and VPAK are Vanguard-owned contracts.

## Planning

`PackagePlanner::Prepare` resolves every manifest root from the committed dependency index. Only generated dependencies create runtime
resource edges or closure work; source and tool dependencies remain cooking inputs and never enter VPAK.

Required generated dependencies are included when closure is enabled. Optional generated dependencies are included only when requested,
but their optional runtime edge remains recorded even when another package is expected to provide them. Missing required resources,
target mismatches, missing runtime artifacts, duplicate artifact segments, and configured bounds are explicit failures.

Editor-only artifacts are excluded by default. Artifact flags map to VPAK placement hints:

- non-streamable artifacts become inline segments;
- streamable artifacts become independently streamable segments;
- memory-resident artifacts retain that scheduling hint;
- a resource is editor-only only when all selected segments are editor-only;
- root startup and optional flags become resource flags.

Resources, segments, and dependencies are sorted deterministically. A SHA-256 digest over the semantic plan produces the VPAK build ID,
so repeated planning of identical committed state is stable regardless of discovery order.

## Assembly and publication

The assembler requests one resource's artifact bytes at a time through a caller-owned callback, validates every byte count against
`VADI`, and immediately passes that bounded resource to `PackageWriter`. It therefore does not stage the complete package in memory.

`PackageWriter` aligns each unique stored payload, applies deterministic LZ4 policy, and deduplicates equal stored segments by SHA-256,
logical/stored sizes, codec, CRC-64, and alignment compatibility. Logical segment entries may reference one exact shared physical range.
Partial overlap or conflicting metadata for a shared offset is invalid.

Atomic publication uses an explicit sibling temporary path:

1. remove an abandoned temporary;
2. write and flush the complete VPAK;
3. reopen it through the strict runtime `PackageReader`;
4. verify package ID, build ID, resource count, tables, bounds, aliases, and integrity;
5. atomically replace the target;
6. remove the temporary after every failure.

## Numbered package-set planning

`PackageSetPlanner` partitions an already prepared `PackageBuildPlan`; it does not rediscover assets or introduce a second dependency
graph. The startup world, default input mapping, and both resources' transitive required runtime dependencies are forced into `DATA000`. Remaining resources retain their
previous numbered-package ownership when a valid placement state is supplied. Newly introduced resources use deterministic first-fit
placement against the configured target size and receive the lowest available number from `DATA001` through `DATA999`.

The planner accounts for the package header, resource/index records, worst-case debug paths, effective package-wide payload alignment,
payload sizes, and the embedded DATA000 catalog. Target size is a placement objective; the hard package and bootstrap limits are enforced
contracts. A resource that no longer fits its persisted package produces `RebalanceRequired` instead of silently moving and destabilizing
an installed image. An explicit rebalance permits the caller to generate a new placement epoch.

Placement ownership is persisted in the Vanguard `VPLS` record. Version 1.0 uses a fixed 64-byte header and sorted 16-byte entries holding
the typed resource identity and package number. Header CRC-32 and payload CRC-64 protect the record; game ID, target platform, versions,
sizes, reserved fields, ordering, and bounds are validated before it can influence planning.

Package IDs are stable per game ID and package number. Package build IDs cover the source build plus assigned resource identities and
content fingerprints. The package-set build ID covers the game, target, startup world, default input mapping, and every numbered package. This separates stable
package identity from the content revision stored in that package.

## Package-set publication

`PackageSetAssembler` publishes only into a directory without existing target DATA files. It writes every numbered package to a sibling
temporary file, validates it through `PackageReader`, and computes its whole-file SHA-256. External packages are completed first so their
actual IDs, build IDs, file sizes, index CRCs, and digests can populate the DATA000 catalog. DATA000 is then assembled and validated last.

Commit order is external packages first and `DATA000.vpak` last. DATA000 is therefore the image commit marker: a visible root never points
at an external package that this publication did not finish. Any staging or commit failure removes temporary files and any targets moved
by the failed transaction. Existing committed DATA files are never overwritten by this path.

## Current boundary

The complete headless path now covers manifest persistence, dependency closure, artifact selection, stable numbered placement,
persisted placement ownership, deterministic layout inputs, segment placement hints, content-addressed payload deduplication, streaming
assembly, authenticated package catalogs, strict validation, and package-set atomic publication.

Editor package authoring UI, patch/delta generation, access-telemetry-guided rebalancing, signing, and encryption remain higher-level or
later operational systems. They must extend these contracts without introducing RED formats.
