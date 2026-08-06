# Vanguard reflected object format

Status: version 1, normative.

`VOBJ` stores one reflected Vanguard value. It can be a loose resource, a VPAK
payload, or a nested structure. Its bytes do not depend on C++ declaration
order, compiler padding, RED RTTI, or its absolute containing-file position.

## Object header

The header is exactly 48 bytes.

| Offset | Size | Field |
|---:|---:|---|
| 0 | 4 | `VOBJ` magic |
| 4 | 1 | byte order (`1` = little-endian) |
| 5 | 1 | encoding version (`1`) |
| 6 | 2 | header size (`48`) |
| 8 | 8 | stable schema type ID |
| 16 | 2 | schema version |
| 18 | 2 | object flags |
| 20 | 8 | exact object size |
| 28 | 8 | field-table offset (`48`) |
| 36 | 4 | field count |
| 40 | 4 | reserved, zero |
| 44 | 4 | CRC32 of bytes 0–43 |

Flag bit 0 marks deterministic output. Bit 1 states that editor-only fields are
present. Unknown flags are rejected.

## Field records

Each field record is exactly 40 bytes. Records are sorted by stable field ID.

| Offset | Size | Field |
|---:|---:|---|
| 0 | 8 | stable field ID |
| 8 | 8 | stable value type ID; arrays store the element type |
| 16 | 8 | data offset relative to the object header |
| 24 | 8 | exact data size |
| 32 | 1 | value kind |
| 33 | 1 | array element kind, otherwise zero |
| 34 | 2 | persisted field flags |
| 36 | 4 | reserved, zero |

IDs are strictly increasing. Data ranges occur in the same order, use zero
padding to an 8-byte boundary relative to their own object, never overlap, and
exactly end at the declared object size. Explicit ranges let readers skip
removed fields without knowing their historical C++ layout.

Required, editor-only, optional-dependency, and soft-dependency flags are
persisted. Transient fields are never written.

## Values

Primitive values use canonical Vanguard binary encoding. Enumerations store
their declared 1-, 2-, 4-, or 8-byte underlying integer. Resource references
contain a 64-bit path ID followed by a 32-bit expected resource type.

Strings contain a canonical variable-byte length followed by UTF-8 bytes.
Nested structures contain a complete nested `VOBJ`.

Dynamic arrays contain a canonical variable element count followed by elements
in index order without inter-element padding. Nested structure elements remain
self-describing `VOBJ` objects. Version 1 rejects nested arrays.

Fixed blob fields store exactly the byte count declared by schema metadata.
They are deliberate byte payloads, never implicit C++ object dumps.

## Evolution and safety

Readers accept only their schema's explicit version window. A known field must
retain its stable value type and kind. Unknown fields are skipped. Missing
fields retain initialized defaults, while missing active required fields fail.

Readers bound object bytes, field count, string bytes, array elements, expanded
array allocation, and nesting depth before allocation or traversal.

The header checksum protects the header. Payload integrity belongs to the
containing VPAK segment, document section, or registered loose-resource CRC.
