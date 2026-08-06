# VADI asset dependency index format

`VADI` is Vanguard's persistent asset dependency index. It is tool/editor data and is never required by the shipped runtime.

All integers are unsigned little-endian. Resource paths are stored as Vanguard's stable 64-bit logical path identities; no RED depot or
resource format is embedded.

## Header

The header is 128 bytes.

| Offset | Size | Field |
|---:|---:|---|
| 0 | 4 | Magic `VADI` |
| 4 | 2 | Major version |
| 6 | 2 | Minor version |
| 8 | 4 | Header size, currently 128 |
| 12 | 4 | Record count |
| 16 | 4 | Total dependency count |
| 20 | 4 | Total artifact count |
| 24 | 4 | Reserved, zero |
| 28 | 8 | Complete file size |
| 36 | 8 | Publication generation |
| 44 | 32 | Global settings SHA-256 |
| 76 | 32 | Payload SHA-256 |
| 108 | 20 | Reserved, zero |

Major-version or settings-fingerprint mismatches invalidate the complete index. A reader may accept a newer minor version only when its
header and record contract remain compatible.

## Record

Records are sorted by output path and type. Each fixed record descriptor is followed immediately by its dependencies and artifacts.

| Field | Size |
|---|---:|
| Source path identity | 8 |
| Source type | 4 |
| Output path identity | 8 |
| Output type | 4 |
| Target platform | 1 |
| Reserved | 3 |
| Compiler identity | 8 |
| Compiler version | 4 |
| Source-input SHA-256 | 32 |
| Build SHA-256 | 32 |
| Content SHA-256 | 32 |
| Dependency count | 4 |
| Artifact count | 4 |

## Dependency

Each dependency entry is 48 bytes.

| Field | Size |
|---|---:|
| Resource path identity | 8 |
| Resource type | 4 |
| Content SHA-256 | 32 |
| Role | 1 |
| Requirement | 1 |
| Reserved | 2 |

## Artifact

Each artifact entry is 32 bytes.

| Field | Size |
|---|---:|
| Resource path identity | 8 |
| Resource type | 4 |
| Segment | 4 |
| Flags | 2 |
| Alignment log2 | 1 |
| Reserved | 1 |
| Byte count | 8 |
| Reserved | 4 |

## Validation

Readers reject truncated fields, invalid enum values, invalid or untyped identities, duplicate output paths, nonzero reserved fields,
count-limit violations, trailing bytes, incompatible settings, size mismatches, and payload-digest mismatches.

Writers publish only after the temporary file has been flushed and read back. Replacement occurs within the same directory through
Vanguard Filesystem, preserving the previous complete index until the new file is ready.
