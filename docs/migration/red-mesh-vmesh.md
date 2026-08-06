# RED mesh study and Vanguard `vmesh` adaptation

## RED source studied

The Vanguard mesh contract was derived after studying these REDengine areas:

- `resourceMesh/include/mesh.h`
- `resourceMesh/include/meshChunk.h`
- `resourceMesh/include/meshVertex.h`
- `resourceMesh/include/meshPacker.h`
- `resourceMesh/src/mesh.cpp`
- `resourceMesh/src/meshPacker.cpp`
- `renderData/include/renderMeshBlob.h`
- `renderBackend/include/renderMeshCompilationSource.h`
- `renderer/src/renderMesh.h`
- `renderer/src/renderMesh.cpp`
- `renderer/src/renderMeshStreaming.h`
- `renderer/src/renderMeshStreaming.cpp`
- `backendMesh/include/lodGenerationUtils.h`
- `backendMesh/src/lodGenerationUtils.cpp`
- `backendMesh/include/meshOptimizer.h`
- `backendMesh/src/meshOptimizer.cpp`

RED importer/editor code was also searched to identify the authoring/runtime boundary. Assimp is not part of RED's durable runtime mesh contract and is therefore not treated as one in Vanguard.

## Behavior retained

- Editable/import streams are cooker input, not shipping runtime state.
- Cooked chunk metadata addresses packed vertex and index storage directly.
- Vertex layouts and byte offsets are explicit.
- Index width, vertex/index counts, material selection, topology, LOD membership, render flags, bounds, and quantization are cooked.
- Renderer construction consumes packed bytes and does not rebuild source geometry.
- Mesh CPU and GPU ownership remain separate.
- Geometry bulk data is suitable for asynchronous streaming and upload.
- Material and other resource dependencies are discoverable before renderer creation.

## RED assumptions rejected

- `.mesh`/`w2mesh` identity and RED serialization.
- `CMesh`, RTTI handles, deferred buffers, and render-object pointers as the persistent contract.
- Embedded local material instances and appearance ownership inside the geometry resource.
- Fixed material vertex factories and hardcoded stream taxonomies tied to RED shaders.
- Eight-bit chunk LOD masks and the renderer's fixed four-LOD storage.
- Monolithic `RenderMeshBlob` version history and deprecated compatibility fields.
- Direct `GpuApi` structures in runtime mesh metadata.
- Loading the full render blob merely to inspect dependencies or LOD metadata.

## Vanguard contract

`vmesh` version 1 uses Vanguard's binary envelope and owns two sections: checksummed `META` and streamable `GEOM`. Stable cooker-local identifiers are canonicalized into dense runtime indexes. Buffer pages cover each logical buffer exactly, carry SHA-256 digests, and can be range-read into caller-owned storage. Materials and skeletons are external typed resource references. NVRHI mappings remain renderer implementation details.

The headless `meshTools` cooker now owns importer-neutral source streams and submeshes. Its private meshoptimizer v1.2 backend follows RED's backend-mesh processing sequence—deduplication, LOD generation, cache and overdraw optimization, fetch remapping, and cooked emission—without retaining RED formats or exposing third-party types. Assimp remains a future adapter beneath this contract; it will produce Vanguard records and never write `vmesh` directly.

RED's production LOD path is a cooker/editor operation driven through a proprietary Houdini asset and RED `CMesh`/data-builder contracts. Copying that implementation would introduce the RED format and tool-session dependencies Vanguard is explicitly avoiding. Vanguard instead retains the reusable design: LOD generation happens over unpacked geometry before final packing, generated chunks retain their source chunk/material correspondence, complete vertex-stream sets are rebuilt for each result, selection thresholds are explicit cooked metadata, and failure to reach a reduction target remains observable.

Vanguard implements that contract with the pinned modern meshoptimizer simplifier. LOD0 remains unchanged, generated levels are opt-in, and each level simplifies directly from LOD0 using position plus supported decoded shading attributes. Stable submesh identity and material slot identity are identical at every LOD. Border locking is conservative by default for independently simplified material subsets, and direct normalized quadric error plus target-reached state are returned to editor/cooker telemetry.

## Tests

`meshesTests` proves:

- deterministic output under reordered cooker records;
- bounded metadata parsing;
- LOD and submesh canonicalization;
- persisted submesh identity across LOD records;
- mixed per-submesh vertex-layout metadata;
- direct page range reads;
- page integrity validation without full geometry residency;
- metadata corruption rejection;
- exact buffer-page coverage;
- mandatory position streams;
- duplicate stable identifier rejection;
- strict LOD threshold ordering.

`meshToolsTests` additionally proves importer-neutral multi-stream cooking, duplicate-vertex collapse, deterministic emission, runtime reopening, direct-from-LOD0 attribute-aware reduction, full per-LOD stream emission, direct normalized quadric-error reporting, and stable submesh identity across every LOD.

## Deferred capabilities

- A pinned Assimp adapter and importer conformance corpus.
- Specialized project cooking profiles beyond the built-in static and four-influence skinned policies.
- Skeleton and morph-target resource formats.
- Renderer/NVRHI buffer creation and residency management.
- Ray-tracing BLAS build policy and backend-specific alignment evidence.
