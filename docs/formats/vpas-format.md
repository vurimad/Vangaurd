# Project Asset Accepted Snapshot (VPAS) format

## Purpose and ownership

VPAS retains its historical **Project Asset Accepted Snapshot** format name,
FourCC, version, filenames, and wire layout. Current implementation terminology
calls the retained in-memory comparison/restart state the **accepted asset
baseline** (`ProjectAssetAcceptedBaseline`). This naming change does not alter
the persisted format or its ownership semantics.

`project-asset-accepted.vpas` is the E2.2 project-local durable baseline used by
`asset_streaming::ProjectAssetPipeline` to compare the current source catalog
after restart. It lives beside the dependency-index state under
`ProjectAssetBuildConfig::dependencyIndexRoot`.

VPAS is historical accepted comparison evidence only. It is not a source
database, dependency index, replay journal, build cache, filesystem transaction
log, undo record, or runtime hot-reload state. Loading it never mutates
`assets::SourceDatabase`.

The sibling stage file is `project-asset-accepted.vpas.tmp`. The save protocol
serializes the complete proposed accepted baseline, writes and flushes the stage,
reads it back and verifies the exact bytes, then replaces the target with the
completed sibling stage using `filesystem::ReplaceFile`. A failure before that
replace leaves the previous target intact.

## Encoding

All integer fields are unsigned little-endian. Boolean values are one byte and
must be `0` or `1`. Enum values use one byte unless stated otherwise and must be
within the enum's currently defined range. Strings are encoded as `u32 byteCount`
followed by exactly that many UTF-8 bytes; there is no trailing NUL in the file.
Filesystem paths must parse as absolute paths of the required file/directory
kind. Resource IDs and type IDs must be valid where the in-memory contract
requires them.

No C++ object representation, pointer, span, padding, hash-table storage, or
allocator state is serialized.

## Version 1.0 header

The header is exactly 128 bytes.

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 4 | FourCC `VPAS` (`u32`) |
| 4 | 2 | major version = `1` |
| 6 | 2 | minor version = `0` |
| 8 | 4 | header size = `128` |
| 12 | 4 | accepted asset count |
| 16 | 4 | tombstone count |
| 20 | 4 | total output count |
| 24 | 4 | total authored-reference count |
| 28 | 4 | source diagnostic count |
| 32 | 4 | reserved, zero |
| 36 | 8 | complete file size in bytes |
| 44 | 8 | accepted global generation |
| 52 | 32 | SHA-256 of bytes `[128, fileSize)` |
| 84 | 44 | reserved, all zero |

The implementation currently requires the exact magic, header size, major and
minor version. A mismatch in any of those compatibility fields is
`Incompatible`; malformed file size, checksum, reserved fields, counts, canonical
ordering, or payload data is `Corrupt`.

The complete file is additionally bounded by
`ProjectAssetChangeStateLimits::maximumSerializedBytes`. Header counts are
validated against the current configured asset/tombstone/output/reference/
diagnostic limits before payload allocation or trust.

## Payload

The payload contains `assetCount` accepted asset records in canonical ascending
`AssetId` order. Each record is followed immediately by its declared output rows
and authored-reference rows.

### Accepted asset record

1. `AssetId`: 16 bytes.
2. source root: encoded string, absolute directory path.
3. source identity: `u64 ResourceId`.
4. physical source path: encoded string, absolute file path.
5. source-content SHA-256: 32 bytes.
6. settings SHA-256: 32 bytes.
7. importer ID: `u64`.
8. authored importer version: `u32`.
9. available importer version: `u32`.
10. settings version: `u32`.
11. source type ID: `u32`.
12. source-issue bits: `u32`.
13. import state: `u8`.
14. reference-scan state: `u8`.
15. accepted presence: `u8` (`Present`, `Missing`, or `Conflicting`).
16. has-source-content-fingerprint: `u8` boolean.
17. per-asset accepted generation: `u64`, nonzero and no newer than the global
    generation.
18. output-row count for this asset: `u32`.
19. authored-reference-row count for this asset: `u32`.

The number of `Missing` asset rows must equal the header tombstone count.

### Output row

1. output key: encoded string. The key follows the asset-metadata key rules
   (1..64 bytes, lowercase ASCII letters/digits plus `-` and `_`).
2. resource path ID: `u64`.
3. resource type ID: `u32`.

Rows for one asset are strictly canonical by output key; duplicate keys are not
valid. The resource path ID is independently recomputed from `AssetId` + key
using the canonical `assets/<asset-id>/<key>` identity and must match the stored
resource reference.

### Authored-reference row

1. `from AssetId`: 16 bytes and must equal the owning accepted asset.
2. target resource path ID: `u64`.
3. target resource type ID: `u32`.
4. dependency kind: `u8`.
5. authored reference location: `u8`.
6. in-place flag: `u8` boolean.
7. reserved: `u8`, zero.

Rows for one asset are strictly canonical by target resource identity and
reference details. Targets may be absent: that is required so accepted
missing-output referencers survive restart.

### Source diagnostic row

After all asset/output/reference records, exactly `diagnosticCount` rows follow:

1. source root: encoded absolute directory path.
2. source identity: `u64 ResourceId`.
3. physical source path: encoded absolute file path.
4. parsed `AssetId`: 16 bytes (may be invalid when no identity was safely parsed).
5. source-issue bits: `u32`.
6. metadata result: `u8`.
7. import state: `u8`.
8. reference-scan state: `u8`.
9. has-parsed-asset-identity: `u8` boolean.
10. diagnostic reason bits: `u32`.

Diagnostic rows must be in the same canonical ordering used by E2.2 comparison.
A diagnostic-only accepted image still requires a nonzero global generation.

No trailing payload bytes are permitted.

## Derived state rebuilt on load

The following are deliberately not serialized and are rebuilt from validated
durable rows:

- per-asset output/reference spans into flat owned storage;
- accepted-asset lookup;
- canonical backward authored-reference index, including references whose target
  output is currently missing.

The following transient acceptance products are deliberately neither serialized
nor rebuilt as restart work and are empty immediately after a valid load:

- current `changes`;
- previous/current change output arrays;
- previous/current change reference arrays;
- invalidation seeds;
- invalidation outputs.

The current source catalog is always prepared and compared against the loaded
baseline through the normal E2.2 candidate path. Therefore an identical clean
restart preserves the global/per-asset accepted generations and produces an
empty current change/invalidation set instead of replaying pre-restart work.

## Load and recovery semantics

Loading is failure-atomic. The parser builds and validates a complete temporary
accepted asset baseline; the caller's baseline changes only by the final swap after
every field, bound, checksum, canonical-order rule, and derived index succeeds.
No prefix of a bad file is trusted.

- `Success`: the complete image is trusted as the historical baseline.
- `NotFound`: no durable baseline exists; the current catalog is conservatively
  accepted as a new bootstrap and a fresh VPAS is written.
- `Corrupt`, `Incompatible`, or `LimitExceeded`: the unusable image is reported,
  discarded, and the current catalog is conservatively rebuilt as the new
  baseline.
- `IoFailure`, `OutOfMemory`, or internal invalid-state failure: initialization
  does not claim recovery and does not accept a newer baseline.

Filesystem presence probing distinguishes true missing-file errors from access
or other I/O errors on the supported Windows tool platform. Existing-but-
unreadable state is never relabeled as corruption and deleted as recovery.

## Acceptance transaction

Manual and startup acceptance serialize, flush, read-back verify, and atomically
replace VPAS before the live `SourceDatabase` and accepted asset baseline are swapped.
If persistence fails, the previous live catalog and accepted in-memory baseline
remain unchanged.

Observed reconciliation performs serialization, stage write, flush, and read-back
verification on the persistent observation worker. The bounded owner tick only
performs the final same-directory atomic replace immediately before its O(1)
source/accepted swaps. Post-commit publication/reconciliation failure does not
roll accepted state back and is reported as committed reconciliation pending.
