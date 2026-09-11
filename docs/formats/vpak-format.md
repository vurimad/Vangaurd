# Vanguard Package Format

Status: VPAK 1.0, pre-freeze normative draft.

`.vpak` is an immutable, self-indexed container for cooked runtime resources.
It is not a resource schema and does not interpret `.vmesh`, `.vtex`, world,
audio, or ECS data.

## Global rules

- All integers are little-endian.
- No C++ structure is dumped to disk.
- All reserved fields and alignment padding are zero.
- File offsets are absolute.
- Resource entries are strictly ordered by `ResourceId`.
- Segment entries are grouped by resource. Distinct physical ranges cannot overlap; exact content-deduplicated aliases are allowed.
- The index occupies the exact end of the file.
- CRC-32/ISO-HDLC protects the header.
- CRC-64/XZ protects the complete index and every stored segment.
- CRC values detect corruption; they are not authenticity signatures.

Resource paths are relative UTF-8 byte strings. `\` becomes `/` and ASCII
letters are folded to lowercase. Absolute paths, drive separators, control
bytes, empty segments, `.` segments, `..` segments, repeated separators,
trailing separators, and the platform-special characters `:*?"<>|` are
invalid. `ResourceId` is CRC-64/XZ of the canonical path. The builder rejects
an observed hash collision by comparing canonical paths.

## File order

```text
96-byte header
optional DATA000 package-set boot record
zero alignment padding
resource payload segment 0
zero alignment padding
resource payload segment 1
...
16-byte index alignment
32-byte index header
resource table
segment table
dependency table
optional canonical debug-path bytes
```

The writer initially stores a zero header. It writes the final header only
after all payloads and the index have been written successfully.

## Package header

The header is exactly 96 bytes.

| Offset | Size | Field |
|---:|---:|---|
| 0 | 4 | Magic `VPAK` |
| 4 | 1 | Byte order (`1` = little-endian) |
| 5 | 1 | Header encoding (`1`) |
| 6 | 2 | Header size (`96`) |
| 8 | 2 | Format major version |
| 10 | 2 | Format minor version |
| 12 | 4 | Package flags |
| 16 | 8 | Exact file size |
| 24 | 8 | Absolute index offset |
| 32 | 8 | Index size |
| 40 | 8 | Package ID |
| 48 | 8 | Build ID |
| 56 | 4 | Resource count |
| 60 | 4 | Segment count |
| 64 | 4 | Dependency count |
| 68 | 4 | Debug-path byte count |
| 72 | 8 | Index CRC64 |
| 80 | 8 | Package-set boot-record offset, or zero |
| 88 | 4 | CRC32 of bytes 0-87 |
| 92 | 4 | Reserved, zero |

Package flags are deterministic build, debug paths present, and package-set boot record present. Unknown flags are rejected. The
package-set flag and offset must either both be absent or both be present. In VPAK 1.0 the boot record begins immediately after the
96-byte package header, so a root package stores offset `96`; ordinary packages store zero. The header CRC covers this offset.

## DATA000 package-set boot record

`DATA000.vpak` may carry the package-set boot record. The record is directly readable after validating the ordinary VPAK header and does
not require mounting the package, reading its resource index, or invoking a resource decoder. Ordinary VPAKs remain valid and have no boot
record.

The boot header remains 96 bytes. Schema 1.2 adds a renderer bootstrap reference
in the payload; readers also accept schema 1.1, which has no renderer reference.
This is a package-set metadata revision, not a new VPAK container version:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 4 | Magic `VPST` |
| 4 | 1 | Byte order (`1` = little-endian) |
| 5 | 1 | Boot encoding (`1`) |
| 6 | 2 | Boot-header size (`96`) |
| 8 | 2 | Boot major version (`1`) |
| 10 | 2 | Boot minor version (`2`; readers also accept `1`) |
| 12 | 4 | Boot flags, currently zero |
| 16 | 8 | Exact boot-record size |
| 24 | 8 | Stable game ID |
| 32 | 8 | Package-set build ID, equal to the root VPAK build ID |
| 40 | 8 | Startup-world ResourceId |
| 48 | 4 | Startup-world ResourceTypeId |
| 52 | 4 | Target-platform ID |
| 56 | 8 | Default-input-mapping ResourceId |
| 64 | 4 | Default-input-mapping ResourceTypeId (`VINP`) |
| 68 | 4 | External package count |
| 72 | 2 | Package-entry size (`80`) |
| 74 | 2 | Reserved, zero |
| 76 | 8 | CRC-64/XZ of the complete payload after the header |
| 84 | 4 | Reserved, zero |
| 88 | 4 | CRC-32/ISO-HDLC of bytes 0-87 |
| 92 | 4 | Reserved, zero |

In schema 1.2, the payload starts with a 16-byte prefix: renderer catalog
ResourceId (`u64`), ResourceTypeId (`u32`, `VRCT` for the geometry renderer), and
reserved zero (`u32`). ID and type must both be zero or both be present. The
prefix is included in the payload CRC and record size. Schema 1.1 starts directly
with the package entries. The catalog and its required dependency closure are
placed in DATA000 by the package-set planner; resource decoding remains the
streamer's responsibility.

The root package is implicit package number zero and is not repeated in its own catalog. External package entries are strictly ordered by
package number from `1` through `999`. Their filenames are derived canonically as `DATA001.vpak` through `DATA999.vpak`; arbitrary paths
are never accepted from package data.

Each external package entry is exactly 80 bytes:

```text
package number          u32
entry flags             u32
mount priority          i32
reserved                u32
package ID              u64
build ID                u64
exact file size         u64
index CRC-64/XZ         u64
whole-file SHA-256      u8[32]
```

Exactly one of required or optional must be set. Override may accompany either availability flag. IDs, sizes, checksums, and the SHA-256
digest must be populated. Package count and total record size are bounded before allocation, all arithmetic is overflow-checked, and the
record must end before any resource payload or index bytes. Resource segments are rejected if they overlap the boot record.

## Index header

The index header is exactly 32 bytes and contains:

```text
VPKI magic
format major/minor
index-header size
resource-entry size
segment-entry size
dependency-entry size
resource count
segment count
dependency count
debug-path byte count
```

All counts must exactly match the package header and the calculated index
size. Trailing or omitted index data is invalid.

## Resource entry

Each resource entry is exactly 64 bytes:

```text
ResourceId             u64
ResourceTypeId         u32
ResourceFlags          u32
logicalSize            u64
firstSegment           u32
segmentCount           u32
firstDependency        u32
dependencyCount        u32
debugPathOffset        u32
debugPathSize          u32
contentCrc64           u64
reserved               u64
```

Segment and dependency ranges must be in bounds and every table slot must be
owned by exactly one resource. Self-dependencies, invalid dependency IDs, and
invalid dependency types are rejected. `contentCrc64` covers the concatenated
logical, decompressed resource bytes.

Current resource flags are startup, optional, streamable, and editor-only.

## Segment entry

Each segment entry is exactly 40 bytes:

```text
absolute stored offset u64
stored size            u64
logical size           u64
stored CRC64           u64
codec                   u8
alignment log2          u8
segment flags           u8
reserved                u8
reserved               u32
```

Codec `0` is uncompressed and requires equal stored/logical sizes. Codec `1`
is a raw independent LZ4 block. Each compressed block can therefore be read
and decoded without preceding segments.

Two logical segment entries may reference the same physical offset only when
stored size, logical size, codec, stored CRC64, and alignment are compatible.
The builder keys reusable stored payloads by SHA-256 and confirms all persisted
metadata before aliasing. The reader rejects partial overlaps and aliases with
conflicting metadata.

Current segment flags are inline, streamable, and memory-resident. They are
placement and scheduling hints; the package reader does not silently preload
data.

## Dependency entry

Each dependency entry is exactly 16 bytes:

```text
ResourceId             u64
ResourceTypeId         u32
dependency kind        u8
reserved               u8
reserved               u16
```

Kinds are `0` required, `1` optional, and `2` soft. Required failures fail the
parent load. Optional failures are reported to the decoder but do not fail the
parent. Soft dependencies are retained as metadata and do not create automatic
runtime load edges.

The cooker derives these entries from reflected resource-reference fields.
The schema decoder checks the manifest against the deserialized object before
publication, so stale or incomplete dependency metadata is an integrity error.

## Mounting

Mount priority is outside the wire format. Resolution considers packages from
highest priority to lowest. Later mounts win at equal priority. Packages do
not embed machine-specific absolute paths or require an external RED-style
resource-path cache.
