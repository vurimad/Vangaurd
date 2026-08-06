# Vanguard mesh resource (`vmesh`)

`vmesh` is Vanguard's platform-cooked geometry resource. Version 1 stores bounded runtime metadata and GPU-ready buffer pages. Runtime code performs no source-scene traversal, triangulation, tangent generation, vertex remapping, LOD generation, quantization, or format translation.

## RED lineage and Vanguard boundary

RED separates editable `SMeshChunkPacked` streams from a cooked `rend::RenderMeshBlob`. The blob stores LOD distances, render chunks, packed vertex/index offsets, quantization parameters, topology data, and a contiguous render buffer that is uploaded directly. Vanguard retains this proven cooked-data boundary and the chunk-to-packed-buffer relationship.

Vanguard does not copy RED's `.mesh` serialization, `CMesh`, `RenderMeshBlob` version history, material instances, vertex-factory enum, render masks, fixed four-LOD runtime assumption, or GPU API structures. `vmesh` has Vanguard identities, typed resource dependencies, extensible stream formats, explicit limits, deterministic ordering, and independently verifiable pages.

## Container

The file uses the Vanguard binary document envelope with magic `VMSH`, version `1.0`, little-endian encoding, and deterministic output.

| Section | Purpose |
|---|---|
| `META` | Mesh identity, bounds, quantization, fingerprints, buffers, pages, vertex layouts and streams, material slots, LODs, and submeshes |
| `GEOM` | Aligned GPU-ready vertex, index, ray-tracing-position, and extensible custom buffer pages |

`META` is checksummed as a section and loaded eagerly. `GEOM` is streamable. Every geometry page records its range, alignment, destination-buffer range, flags, and SHA-256 digest, allowing a package/range reader to validate and upload one page without reading the complete mesh.

## Canonical layout

Cooker records use stable local identifiers. The writer canonicalizes:

- buffers by identifier;
- pages by buffer identifier and logical buffer offset;
- vertex layouts by stable local identifier and each layout's streams by semantic, semantic index, binding, and byte offset;
- material slots by identifier;
- LODs by level;
- submeshes by LOD and stable identity.

Buffer, layout, and material identifiers are remapped to dense file indexes. Submesh identity and name hashes are persisted so editor selections, overrides, recooking, and cross-LOD correspondence do not depend on array order. Buffer pages must cover every declared logical buffer byte exactly once with no gaps or overlap. Each LOD owns a contiguous submesh range. LOD levels begin at zero and are contiguous; minimum screen coverage is positive, no greater than one, and strictly decreases.

Every page intersecting the coarsest LOD's referenced vertex or index ranges must carry `RequiredForLowestLod`. The writer and reader both enforce this guarantee, allowing bootstrap residency code to trust the flag rather than rediscovering mandatory byte ranges. Additional pages may conservatively carry the flag.

## Geometry contract

A vertex layout owns one or more vertex streams; each stream declares semantic, semantic index, GPU storage format, binding, buffer, byte offset, and stride. Every layout requires position stream zero. Each submesh selects its layout, material slot, and index buffer and carries direct vertex/index ranges, topology, bounds, flags, and stable identity. This permits mixed layouts in one mesh without renderer-side reconstruction. Index buffers are explicitly 16- or 32-bit.

The format stores native-ready numeric layouts but no NVRHI objects. The renderer maps Vanguard formats to NVRHI/native input attributes once when constructing runtime mesh objects. The bulk bytes themselves are uploaded unchanged.

Materials are external typed resource references. A future `vmat` cooker derives its parameter layout from shader reflection; meshes merely name slots and depend on material resources. Skeletons are external resources as well, preventing mesh geometry from becoming the ownership boundary for rigs and animation.

## VPAK relationship

VPAK treats a `vmesh` as an opaque resource of type `VMSH`; it does not duplicate or interpret mesh metadata. The mesh compiler is responsible for publishing the material references, and the skeleton reference for skinned meshes, as typed generated dependencies so package planning records them in the VPAK dependency table. Concatenating a packaged mesh's logical artifact segments must reproduce the exact standalone `vmesh` byte sequence.

The two levels of segmentation serve different contracts. VPAK segments are storage, compression, and asynchronous-I/O units. `vmesh` pages are destination-buffer ranges with GPU alignment, residency flags, and SHA-256 integrity. A mesh compiler may align artifact boundaries with the document prefix and geometry pages, but it must retain any document padding so logical concatenation remains exact.

The generic `ResourceStreamer` still reconstructs complete resources for ordinary decoders, but meshes have a partial-residency path. `BuildStorageSegments` divides a cooked vmesh into one metadata segment followed by independently decodable geometry-page segments while preserving the exact standalone byte sequence. The vmesh section table is written beside the document header, so opening metadata never requires reading a geometry tail.

`packages::ResourceFileReader` exposes a seekable logical resource view over VPAK. It decodes and validates only the segment intersecting a requested range and retains one decoded-segment cache. `MeshFile::Open` therefore reads only the metadata segment, while `MeshFile::ReadPage` transparently fetches only the package segment containing that page. Compressed segment CRC and vmesh page SHA-256 validation remain independent layers.

`CollectLodPages` resolves a LOD to its exact deduplicated vertex and index page set. The lowest LOD can consequently be bootstrapped without reconstructing higher-detail geometry. The interface remains renderer-agnostic: a renderer residency manager may schedule the resolved package segment reads asynchronously and upload their already packed bytes directly when the graphics layer is introduced.

## Assimp

Assimp is an editor/tool import implementation, not a format dependency. A pinned adapter may read FBX, OBJ, Collada, or glTF into a Vanguard import scene. It must not emit `vmesh` directly. The Vanguard cooker remains authoritative for coordinate and unit policy, validation, deterministic optimization, LODs, packing, fingerprints, dependencies, and diagnostics.

This boundary avoids making Assimp's scene model, post-process settings, material conventions, or upgrade behavior part of Vanguard's permanent asset contract.

## Current cooker

The headless `meshTools` module accepts importer-neutral submeshes and stream views. Its private meshoptimizer v1.2 integration performs multi-stream vertex deduplication, direct-from-LOD0 attribute-aware simplification, post-transform cache optimization, optional overdraw optimization, vertex-fetch remapping across every stream, and direct indexed `vmesh` emission. Each emitted LOD owns dense complete vertex and index buffers, while stable submesh and material identities remain unchanged across levels. Temporary third-party allocations are routed through Vanguard's Assets memory pool. Runtime `meshes` does not link meshoptimizer and does not decode a meshoptimizer-specific representation.

The cooker follows RED's production boundary: deduplication, optimization, and all LOD simplification operate on full-precision data, then the final compact LOD is packed. The default `RuntimeStatic` profile emits a dedicated `R16G16B16A16SNorm` position binding decoded by the mesh-wide scale and bias, `R10G10B10A2UNorm` normals and tangents, half-precision texture coordinates, normalized 8-bit float colors and joint weights, and unchanged integer joint indices. Shading attributes are interleaved while position remains separate for position-only passes. Compact LODs use 16-bit indices when they contain at most 65,536 vertices and 32-bit indices otherwise.

Packing is selected through a startup-registered declarative cooking profile. Profiles define the resulting mesh kind, required source streams, source-to-stored format conversions, and interleaved binding groups; they may reject unmatched streams or preserve them byte-exact in dedicated bindings. `RuntimeStatic`, `RuntimeSkinned4`, and `PreserveSource` are built in, while specialized cloth, vegetation, vehicle, and project layouts can be registered without changing `vmesh`. Skinned profiles require and persist an external skeleton resource reference. The cooked file remains self-describing and has no runtime dependency on the registry. Packed pages are already direct-upload data: GPU vertex-input normalization and a position scale/bias operation in the shader are decoding, not an intermediate runtime translation.

Meshlets are intentionally outside the current contract. If renderer requirements justify them later, they will be introduced through a versioned format extension with explicit residency, culling, and backend policies rather than maintained as unused metadata today.
