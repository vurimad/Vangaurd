# Vanguard Schemas

The schemas module owns Vanguard's durable reflected-object format and the
runtime traversal that connects reflected resource fields to dependency
loading. It uses Vanguard reflection, binary primitives, resource identities,
containers, and memory pools.

## RED-derived architecture

This implementation was adapted after studying:

- `redReflection/src/packageObjectSerializer.cpp`;
- `redReflection/src/serializationBinaryStructureMapper.cpp`;
- `redReflection/src/serializationBinarySaver.cpp`;
- `redReflection/include/serializationFileTables.h`;
- `redReflection/src/rttiClass.cpp`;
- `redReflection/src/rttiPointerTypesImpl.cpp`.

The retained principles are stable type/property/reference identities,
explicit property byte ranges, declaration-order-independent output, strict
type matching, unknown-field skipping, initialized defaults for absent fields,
and recursive resource-import gathering.

Vanguard does not persist RED names, file tables, RTTI objects, export/import
indices, depot paths, object handles, or structure dumps.

## Supported values

Version 1 supports booleans, integers, floats, explicitly sized enumerations,
Vanguard strings, nested structures, typed resource references, fixed blobs,
and allocator-aware Vanguard `DynamicArray` values containing any supported
non-array element type.

Nested arrays are rejected by version 1. They require a distinct array-shape
descriptor rather than an ambiguous recursive callback.

## Registration and loading

Schema and field names are lowercase stable identifiers. Schema names allow
dotted namespaces; field names use lowercase letters, digits, and underscores.
Both use deterministic 64-bit FNV-1a identities.

Descriptors and field arrays are non-owning static metadata. Registration and
unregistration are composition-root operations and must not race
serialization or editor inspection.

The object passed to `ReadObject` must already be constructed and initialized
with its schema defaults. Unknown and absent fields leave those defaults
unchanged. Transactional callers deserialize into a staging object and publish
only after success.

`VisitDependencies` recursively visits resource-reference fields in structures
and arrays and classifies them as required, optional, or soft. This is
Vanguard's equivalent of RED's import-mapping pass.

The normative layout is documented in
[`docs/formats/vobj-format.md`](../../docs/formats/vobj-format.md).
