# Renderer catalog (.vrcat)

The renderer catalog is an ordinary streamed resource with type `VRCT`. DATA000
schema 1.2 may reference it; nonpackage callers may supply an explicit typed
reference to RenderingService. The catalog contains no native handles or asset
filesystem paths. Existing shader-map and pipeline-factory owners consume it.

Version 1.0 uses little-endian encoding. Its 24-byte header contains magic `VRCT`
(`u32`), major/minor (`u16` each), entry count (`u32`), entry size (`u32`, 92),
and CRC-64/XZ of the entry payload (`u64`). The exact file length must match the
header and payload. The catalog contains between 1 and 256 entries.

Each 92-byte entry contains:

| Field | Encoding |
|---|---|
| Case-sensitive feature name | 64 bytes, nonempty and null terminated |
| Shader reference | ResourceId `u64`, type `u32` (`VSHD`) |
| Pipeline reference | ResourceId `u64`, type `u32` (`VPLN`) |
| Constants convention | `u32`: 0 = none, 1 = one push-constant buffer |

Writers zero unused name bytes. Duplicate feature-name hashes, unknown constants
conventions, and invalid typed references are rejected. Cooker dependency records
declare each referenced shader and pipeline as required generated resources.

The constants convention describes renderer ABI intent. Register, space, byte
size and stage visibility come from the referenced shader reflection. Startup
checks the selected renderer's required names, program kinds, constants policy,
and matching shader/pipeline pairing before existing native factories run.

Exact attachment signatures stay in pipeline resources. A deferred output
pipeline requires an explicit signature from the output owner; the catalog
cannot select a swapchain or viewport format. Disabled rendering and callers
using the existing explicit startup arrays remain supported.
