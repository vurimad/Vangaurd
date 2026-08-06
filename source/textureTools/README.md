# textureTools

Offline, importer-neutral texture cooking for Vanguard. The module converts caller-owned source images into deterministic, directly uploadable `vtex` documents. Runtime code never links the codec libraries.

The architecture follows RED's proven split between compilation source, texture-group policy, float-domain image processing, per-mip codec conversion, and final runtime serialization. Vanguard owns the profiles and `vtex` contract; no RED depot path, resource type, file format, or GPU API crosses this boundary.

## Production contract

| Area | Contract |
|---|---|
| Source formats | R8, RG8, RGBA8, RGBA16 UNorm, RGBA16F, and RGBA32F with arbitrary row/slice pitch and unaligned little-endian component storage |
| Dimensions | 1D, 2D, 2D arrays, 3D volumes, cube faces, and cube arrays |
| Runtime formats | R8, RG8, RGBA8, RGBA16F, BC1, BC2, BC3, unsigned/signed BC4 and BC5, unsigned/signed BC6H, and BC7 |
| Color | sRGB inputs are linearized before filtering and encoded only when the target supports sRGB sampling |
| Mips | Deterministic full-footprint area filtering; odd extents and 3D depth never discard source texels |
| Cubes | Direction-space 4x4 angular filtering across all six faces, cross-face bilinear taps, oriented edge/corner fixup, and cube-array isolation |
| Normals | Float-domain filtering followed by unit-length renormalization; the BC5 runtime representation stores XY |
| Alpha test | Per-source-image coverage preservation with the maximum residual error reported in `CookReport` |
| Scheduling | Explicit Serial, Jobs, or Automatic execution; serial and Jobs output is byte-identical |
| Publication | Canonical subresource ordering, direct-upload pitches, limits, hashes, and atomic higher-level asset publication through the existing pipeline |

Built-in profiles cover color, alpha color, normals, masks, generic data, UI, and HDR sources. Applications may register project profiles during startup; the registry seals on the first cook. Unknown flag bits, invalid color-space/format combinations, non-finite floating-point source data, malformed pitches, overflowing limits, and unsupported scheduler state are rejected before publication.

## Source import

The source-import boundary consumes caller-owned encoded bytes and produces an owned, importer-neutral `SourceTexture`; it stores no physical source path. Importers are explicitly registered by stable identifier and version, selected through signature-first probing, and the registry seals on the first import. Decoder output is bounded, validated against the selected importer identity/version and source fingerprint, and may feed `CookTexture` directly.

The built-in cross-platform backends use libpng for PNG, libjpeg-turbo for JPEG, libtiff for TIFF, and OpenEXR Core for OpenEXR. PNG signature and CRC processing is strict, palette/grayscale/transparency/interlace variants are normalized to RGBA, and native 16-bit PNG precision is retained as RGBA16 UNorm. JPEG is decoded to RGBA8 using the quality-oriented integer DCT path. TIFF accepts stripped or tiled, contiguous or planar grayscale/RGB images, normalizes all eight orientations, preserves 8/16-bit unsigned and 16/32-bit floating-point precision, and converts associated alpha to Vanguard's straight-alpha source contract. OpenEXR accepts ordinary scanline and one-level tiled images, preserves half or float precision in linear space, supports RGB(A), Y(A), or one unambiguous layered RGB(A) set, and rejects deep, multipart, subsampled, UINT, and multilevel tiled sources explicitly. Decoder dimensions, output storage, and codec scratch allocations are budgeted; decoded pixels and TIFF/OpenEXR internal allocations use the Assets memory pool. WIC, COM, and Windows codec libraries are deliberately absent, so editor and cooker decoding follows one implementation on every host.

DDS uses a separate precompressed-source boundary because its BC blocks are already runtime GPU data rather than pixels to be filtered. Legacy DXT1/DXT3/DXT5, ATI1/ATI2, signed BC4/BC5, and DX10 BC1 through BC7/BC6H containers preserve their exact mip, array, volume, and complete cube subresources. Compatible data is copied directly into deterministic `vtex` output without decompression or recompression. Truncated or trailing payloads, typeless formats, incomplete cubes, impossible mip chains, premultiplied alpha declarations, and requested channel swizzles are rejected explicitly. The parser is platform-neutral and does not link DirectXTex or a graphics API.

Import settings explicitly select texture usage, linear/sRGB interpretation, and RGBA channel mapping, including zero/one constants. Six separately imported square images can be assembled into canonical +X, -X, +Y, -Y, +Z, -Z order. Horizontal 4x3 and vertical 3x4 cube crosses can also be extracted byte-exactly when their occupied cells already use the documented canonical face orientation. These operations derive new fingerprints from every prerequisite source.

Block-compressed mips are decomposed into independent BC blocks and dispatched through Vanguard Jobs. `CookSettings::Execution` makes scheduling explicit: `Serial` is the byte-reference path, `Jobs` requires a running scheduler, and `Automatic` uses Jobs only when the composition root has initialized it. The synchronous `CookTexture` boundary waits for all blocks before deterministic `vtex` publication.

Mipmapped cubes are filtered as one spherical signal per array layer. Every generated texel uses angular-domain quadrature and may gather bilinear taps from neighboring faces; each mip then reconciles its 12 oriented edge pairs and eight three-face corners. The final 1x1 level is shared by all six faces. Color filtering remains linear-light, normal maps are renormalized after seam resolution, and alpha coverage is preserved over the complete cube rather than independently per face.

## Verification and performance

`textureToolsTests` covers serial/Jobs equality, independent BC block decoding, byte-exact DDS-to-vtex preservation, DDS container rejection paths, profile sealing, PNG/JPEG decoding, tiled and oriented 16-bit TIFF, signed HDR float TIFF, ZIP-compressed float OpenEXR, importer limits and truncation, alpha and normal policies, malformed and non-finite input, odd 1D and 3D extents, cube face ordering, all 12 oriented cube-edge relationships, corner/final-mip continuity, cube arrays, runtime `vtex` reopening, and all supported build configurations.

`textureToolsBenchmarks` cooks a deterministic 512×512 BC7 corpus three times through both execution paths, verifies byte equality, and prints MPix/s and speedup. Run the Development build on target workstation hardware when changing codecs, presets, compiler versions, Jobs batching, or CPU topology.

RDO and ISPC BC7E are not part of the baseline. They require corpus-based image-quality, cooking-time, and final VPAK-size acceptance data before a profile version may select them. GPU cooking is likewise an optional future backend; the CPU path remains the deterministic reference implementation.
