# Vanguard Package Manifest Format

Status: VPMF 1.0.

`.vpmf` is the deterministic, tool-side input to Vanguard package planning. It names typed cooked-resource roots and records the target
and selection policy. It does not contain source assets, cooked payloads, physical offsets, or RED depot metadata.

## Global rules

- All integers are little-endian.
- No C++ structure is dumped to disk.
- Roots are strictly ordered by `ResourceId`, then `ResourceTypeId`.
- Duplicate IDs, duplicate roots, unknown flags, nonzero reserved fields, trailing bytes, and unsupported versions are rejected.
- CRC-32/ISO-HDLC protects the semantic header and CRC-64/XZ protects the complete root table.
- Resource identities use the same Vanguard canonical-path CRC-64/XZ contract as VPAK and `VADI`.

## Header

The header is exactly 64 bytes:

```text
magic `VPMF`             u32
byte order (1)           u8
encoding (1)             u8
header size (64)         u16
major version            u16
minor version            u16
manifest flags           u32
target platform          u8
default VPAK codec       u8
data alignment log2      u8
reserved                 u8
package ID               u64
root count               u32
root-entry size (16)     u16
reserved                 u16
payload size             u64
payload CRC64            u64
header CRC32             u32
reserved                 u64
```

Manifest flags select generated-dependency closure, optional generated dependencies, editor-only artifacts, and VPAK debug paths.
Unknown flags are rejected.

## Root entry

Each root entry is exactly 16 bytes:

```text
ResourceId               u64
ResourceTypeId           u32
root flags               u8
reserved                 u8
reserved                 u16
```

Root flags are `Startup` and `Optional`. A missing required root fails planning. A missing optional root is omitted. Startup and optional
flags become VPAK resource scheduling metadata; they do not alter the resource schema.

## Compatibility

Readers accept exactly major version 1 and minor version 0. Future incompatible meanings require a major version change. Additional
policy is added only through previously reserved flag values or a compatible minor version with an explicitly updated acceptance range.
