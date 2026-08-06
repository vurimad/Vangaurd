# RED filesystem full-image adaptation

## Decision

Vanguard carries `redFileSystem` as a complete working image. Its file manager,
paths, stream wrappers, safe writes, compression framing, platform operations,
and FileSync override share ownership and failure contracts; selecting slices
would recreate those contracts incompletely.

The dependency path is:

`System -> Memory -> Diagnostics -> Containers -> I/O -> Filesystem`

## Ownership

- `redCompressionThirdPartyCompat` builds RED's bundled codec sources without
  the RED PCH.
- `redCompressionCompat` builds RED's compression wrappers and Oodle path.
- `redFileSystemCompat` builds the complete Windows filesystem image and the
  non-Shipping FileSync path.
- `filesystemCompat` owns lifecycle translation and RED global-manager
  assignment.
- `filesystem` owns the supported `vanguard::filesystem` API.
- `filesystemTests` owns conformance and stress coverage.

Linux and Orbis sources remain in the imported image but are excluded from the
active Windows target, matching RED's platform selection. Archive/package
mounting is not folded into this module because it is a separate RED subsystem.

## Preserved contracts

- absolute directory/file path distinction and path parsing;
- raw, buffered, mapped, memory, external-buffer, and null streams;
- append and temporary-file-backed safe writes;
- copy, move, delete, timestamps, read-only state, wildcard traversal, and
  temporary paths;
- internal chunked LZ4 metadata, compression, decompression, seek rules, and
  corruption assertions;
- internal RED file-format, version, skippable-block, and compressed-number
  helpers plus public text-file helpers;
- development FileSync client/server service and file-manager override;
- RED memory pools, explicit failures, assertions, and stream ownership.

The Vanguard layer does not repair invalid seeks or malformed state. It also
does not introduce `std::filesystem`, CRT allocation, or a second filesystem
algorithm.

RED wire-format helpers are compatibility-private. Vanguard serialization,
resources, and archives must not publish or generate those formats.

## Integration patches

Only import-path relocation was needed in the filesystem image. Compression
needed Oodle include relocation and a narrow restoration of the CRT `assert`
macro while compiling Doboz headers under RED's PCH.

RED's error handler can send assertion text to a native console stream that is
not visible in every host. The existing RED assertion entry now mirrors fatal
details to `stderr` before invoking the original crash handler, preserving the
break/fail-fast behavior while making contract violations visible in tools and
CI.

## Verification

All four configurations must compile and pass `filesystemTests`. Debug,
Development, and Profile additionally prove the FileSync-enabled image links
and stages its runtime dependencies. Shipping proves RED Final correctly
removes FileSync. I/O and Jobs suites remain regression gates.
