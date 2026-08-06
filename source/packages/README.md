# Vanguard Packages

The packages module owns Vanguard's `.vpak` shipping container and mount
precedence. It is independent of ECS, reflection, asset schemas, and concrete
resource loaders.

The design adapts the proven shape of RED's archive system:

- immutable payload segments followed by a sorted index;
- explicit resource-to-segment and resource-to-dependency ranges;
- caller-controlled alignment;
- a header committed only after payload and index completion;
- read-only runtime metadata and separate tool-side writing;
- package layering for base content, patches, DLC, and mods.

Vanguard owns every persisted value. No `RADR` magic, RED archive version,
RED path hash, RED compression wrapper, reflected package, or required RED
sidecar is accepted or generated.

## Runtime use

The caller owns file handles. `PackageReader::Open` reads and validates
metadata from one handle. Reads accept an independent handle, allowing the
resource/streaming layer to provide per-thread handles or asynchronous range
I/O without a hidden lock in the package object.

```cpp
vanguard::packages::PackageReader package;
auto result = package.Open(packageFile);
const auto* resource = package.Find("world/cell_12_08.vcell");

result = package.ReadResource(
    independentPackageFile,
    *resource,
    output,
    outputSize,
    compressedScratch,
    compressedScratchSize);
```

`MountTable` is deliberately explicit and non-owning. Higher numeric priority
wins; the most recently mounted package wins at equal priority. Mount mutation
must be externally serialized. Immutable readers and lookups may be shared
after publication.

## Tool-side use

`PackageWriter` writes segment data as resources are added and retains only
metadata plus one compression scratch buffer. This avoids staging an entire
package in memory.

Equal stored payloads are deduplicated through SHA-256 content identity plus
size, codec, CRC-64, and alignment validation. Logical segments may share one
exact physical range; partial or metadata-conflicting aliases are rejected.

Deterministic builds require resources to be submitted in ascending
`ResourceId` order. The cooker should sort its build plan before writing.
Failure during payload or final header output invalidates the writer and
leaves the zero header in place, so a partial file cannot be mistaken for a
valid package.

VPAK v1 supports uncompressed and raw LZ4 blocks. Compression that does not
reduce a segment is stored uncompressed. The LZ4 payload is a raw block; it
does not use RED's compression framing.

The normative layout is in
[`docs/formats/vpak-format.md`](../../docs/formats/vpak-format.md).

Tool-side manifest selection, `VADI` closure, artifact filtering, deterministic
build identity, and atomic publication are supplied by the Assets packaging
layer. See
[`docs/architecture/package-assembly.md`](../../docs/architecture/package-assembly.md)
and [`docs/formats/vpmf-format.md`](../../docs/formats/vpmf-format.md).
