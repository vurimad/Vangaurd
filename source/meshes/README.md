# Meshes

The Meshes module owns Vanguard's cooked `vmesh` contract. It does not import DCC files, optimize source geometry, create GPU objects, or contain renderer-backend types. Tool-side cookers provide already validated and packed buffer pages; runtime code reads bounded metadata and requests only the pages needed for resident LODs.

The design follows RED's separation between editable mesh chunks and the cooked `rend::RenderMeshBlob`: compact chunk and LOD metadata point directly into packed vertex/index storage, positions carry explicit quantization, authoring arrays disappear from shipping data, and renderer upload does not rebuild source geometry. Vanguard replaces RED's fixed vertex factories, eight-bit LOD masks, monolithic legacy resource serialization, and embedded material instances with extensible stream descriptors, explicit LOD records, typed material dependencies, per-page SHA-256 integrity, and Vanguard-owned versioning.

## Assimp boundary

Assimp belongs in a future headless `meshTools` importer adapter, not in this runtime module. The adapter converts `aiScene` into Vanguard-owned import records while preserving source node/material identities and emitting diagnostics. The Vanguard mesh cooker then owns coordinate normalization, validation, tangent policy, optimization, LOD generation, quantization, page planning, dependency extraction, and `vmesh` emission.

No Assimp type, enum, material convention, index ordering, allocator, filesystem call, or version identity crosses the adapter boundary. Procedural geometry, USD, glTF-specific tooling, and editor-authored geometry must be able to feed the same cooker without Assimp.

## Runtime contract

`MeshFile::Open` loads and validates only the checksummed metadata section. Packed geometry remains in the streamable `GEOM` section. `ReadPage` performs a bounded range read and validates the page's SHA-256 digest before the bytes may be uploaded. The caller owns page storage, scheduling, cancellation, residency, GPU allocation, and NVRHI translation.

Metadata is immutable after opening. Separate `MeshFile` instances may be used concurrently. Calls operating on the same `IFile` require external serialization because the current filesystem contract is seek based.
