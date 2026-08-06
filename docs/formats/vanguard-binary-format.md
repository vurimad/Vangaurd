# Vanguard binary document envelope

Status: version 1, normative.

This envelope is Vanguard-owned. It deliberately shares no magic, version
number, structure dump, resource identifier, or required sidecar file with
REDengine.

VPAK is a specialized segmented container and uses the same canonical binary
primitives but not this document envelope. A package needs thousands or
millions of independently checksummed range-readable payload segments; mapping
each one to a generic document section would duplicate its index and make the
wrong abstraction durable. VPAK's normative layout is defined separately in
`vpak-format.md`.

## Encoding rules

- Multibyte numbers are little-endian.
- Integers use fixed two's-complement widths.
- Floating-point values use IEEE-754 binary32/binary64 bit patterns.
- Boolean values are one byte and must be `0` or `1`.
- Unsigned variable integers use canonical LEB128, limited to 64 bits.
- Signed variable integers use zig-zag mapping followed by unsigned LEB128.
- Alignment padding must contain zero bytes.
- C++ padding, pointers, container layouts, `sizeof(T)`, and compiler ABI are
  never persisted.

## Document header

The version-1 header is exactly 40 bytes.

| Offset | Size | Field |
|---:|---:|---|
| 0 | 4 | Format-specific FourCC magic |
| 4 | 1 | Byte order (`1` = little-endian) |
| 5 | 1 | Envelope encoding version (`1`) |
| 6 | 2 | Header size |
| 8 | 2 | Format major version |
| 10 | 2 | Format minor version |
| 12 | 2 | Document flags |
| 14 | 2 | Reserved, zero |
| 16 | 8 | Exact file size |
| 24 | 8 | Absolute section-table offset |
| 32 | 4 | Section count |
| 36 | 4 | CRC32 of bytes 0–35 |

Each concrete format owns its FourCC and version range. A major mismatch is
rejected. Minor versions are accepted only through an explicit bounded range.

The current document flags are:

- bit 0: contains editor data;
- bit 1: deterministic build.

Unknown flags are rejected. New meanings therefore require a format/envelope
compatibility decision instead of being silently ignored.

## Section descriptor

Each descriptor is exactly 48 bytes.

| Offset | Size | Field |
|---:|---:|---|
| 0 | 4 | Format-owned section FourCC |
| 4 | 2 | Section major version |
| 6 | 2 | Section minor version |
| 8 | 4 | Section flags |
| 12 | 2 | Codec |
| 14 | 1 | Alignment as `log2(bytes)` |
| 15 | 1 | Reserved, zero |
| 16 | 8 | Absolute stored-data offset |
| 24 | 8 | Stored byte size |
| 32 | 8 | Logical byte size after decoding |
| 40 | 8 | CRC64 of stored bytes |

Section descriptors are ordered by stored offset. Stored ranges cannot overlap
each other, the header, or the section table. All ranges must fit the exact
declared file size. An uncompressed section must have equal stored and logical
sizes.

Current section flags are optional, editor-only, and streamable. Current codec
IDs are none, LZ4, and Kraken. These numeric IDs are Vanguard assignments; RED
chunked-LZ4 framing is not used.

## Safety and compatibility

Readers apply caller-provided maximum file size, section count, and alignment
limits before allocating or traversing tables. Table storage and checksum
scratch buffers remain caller-owned.

Header CRC32 is CRC-32/ISO-HDLC: reflected polynomial `0xEDB88320`, initial
value `0xffffffff`, and final XOR `0xffffffff`. Section CRC64 is CRC-64/XZ:
reflected polynomial `0xC96C5795D7870F42`, initial value
`0xffffffffffffffff`, and final XOR `0xffffffffffffffff`. Both support
incremental continuation through the public API.

CRC detects accidental corruption only. Signed packages and hostile-content
authentication require a separate cryptographic layer.

Formats must register a unique magic, extension, ownership module, current
version, compatibility window, and migration policy before becoming durable.
Editor/cooker code writes the same envelope read by runtime code.
