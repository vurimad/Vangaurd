# RED format boundary

## Decision

Vanguard studies RED serialization and archive architecture but does not adopt
RED wire formats.

The RED archive image inspected in
`D:/root/R6.Root/Mainline/dev/src/common/archive` fixes the `RADR` magic,
archive version 12, RED file IDs, hashes, segment tables, resource dependency
tables, debug-build metadata, and RED resource assumptions into its format.
Importing it wholesale would require the RED depot and cooking ecosystem that
Vanguard explicitly does not own.

## Retained concepts

- fixed and verified wire sizes;
- format/version checks before table access;
- absolute offsets and segment metadata;
- data alignment and padding;
- stored versus logical sizes;
- checksums and explicit load/save results;
- runtime-fast and inspector/editor use cases sharing one representation.

## Rejected contracts

- RED archive and package magic/version values;
- RED resource paths, IDs, dependency tables, and depot layout;
- RED reflection packages and resource serialization;
- RED file-version constants and compressed-number encoding;
- RED chunked-LZ4 framing;
- required RED sidecars or generated bootstrap files.

The physical filesystem continues to compile its complete RED compatibility
image, but those wire helpers are no longer part of
`vanguard::filesystem`. The supported durable-format boundary begins at
`vanguard::serialization`.

