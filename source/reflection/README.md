# Vanguard Reflection

This module adapts the complete RED `redReflection` image behind a Vanguard
boundary. The imported image includes RTTI registration and lookup, native and
scripted type metadata, properties and functions, handles and weak handles,
variants, reflective value access, object-package readers/writers, binary and
text serialization, resource references/loaders, and the script-expression
support present in the source module.

The compatibility closure also imports `redConfig`, `commProtocol`,
`commChannel`, `redNetwork`, RED Lexer, and the complete header-only legacy
`redJobs` value facility because the original monolithic reflection library
directly depends on them. Generated Bison output and the RED type registry are
checked in as reproducible source inputs; building the engine does not require
RED's proprietary RTTI generator.

`redConfig` also includes the two original `gameServices` error-contract
headers it directly references. This is a header dependency only, not a
partial adaptation of the game-services runtime; that runtime remains a
future complete module image.

The same rule applies to the two engine tick-enum headers used only by RED's
script thread-safety monitor. They preserve the exact upstream ABI without
pretending that Vanguard has already adapted RED's full `engine` module.

## Boundary rule

RED reflection and RED object-package serialization are compatibility
facilities. They may be used to adapt copied RED systems, validate behavior,
and migrate data. They are not Vanguard's durable asset or package format.
Vanguard `.vpak` and future Vanguard resource schemas remain independent and
must not acquire RED depot paths, headers, versions, or bootstrap files.

Normal engine code includes only
`<vanguard/reflection/reflection.hpp>`. Imported headers and RED names remain
inside compatibility projects. Additional RED behavior is exposed only when a
dependent module proves it needs that behavior, without replacing or
reimplementing the underlying RED implementation.

## Vanguard schema metadata

The public module also owns Vanguard's stable `Schema` and `SchemaField`
registry. This metadata is independent of RED RTTI and uses Vanguard stable
type/field identities, versions, flags, offsets, kinds, and allocator-aware
array operations. The `schemas` module consumes it to produce `VOBJ` and
reflected dependency traversal.
