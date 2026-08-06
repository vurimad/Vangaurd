# Materials

The Materials module owns Vanguard's cooked `vmat` contract. A material is a small immutable runtime resource containing exact shader-derived constant-buffer bytes, typed descriptor resources, named `vpipeline` techniques, and the fingerprints required to reject stale interfaces before renderer object creation.

## Ownership boundaries

- `vshader` owns native shader bytecode and normalized reflection.
- `vpipeline` owns fixed-function state and the selected shader permutation.
- `vmat` selects the material-owned portion of that shader interface and supplies its values and resource references.
- `vtex` and future buffer resources own the referenced payloads.
- NVRHI descriptor sets, constant-buffer allocations, residency and native objects belong to the renderer.
- Source graphs, inheritance and instances belong to authoring and are flattened by the cooker.

The format has no built-in PBR, cloth, hair, terrain or decal schema. A cooker explicitly selects material-owned constant buffers and descriptor bindings from an opened `ShaderFile`; `WriteMaterial` copies their reflected offsets, sizes, strides and scalar types. Values whose names or sizes do not exactly match reflection are rejected. Each named technique is checked against an opened `PipelineFile` and must select the same shader resource, permutation, binding-layout fingerprint and pipeline-interface fingerprint.

## Runtime and streaming

`MaterialFile::Open` validates the document header, section layout, CRC64, SHA-256 content fingerprint, canonical ordering, record domains, byte ranges and dependency types before publishing any state. Parameter blocks are already laid out for GPU upload. Runtime code does not rebuild a schema by name and does not translate material values.

`Dependencies()` returns a canonical, deduplicated list containing the shader, every technique pipeline and every bound resource. VPAK stores `vmat` as an opaque memory-resident segment and copies those dependencies into its resource table. A streamed mesh therefore resolves as:

```text
Flecs render entity -> vmesh -> material slot -> vmat
                                           |-> vpipeline -> vshader
                                           `-> vtex / buffer resources
```

The generic asset layer will later own source watching, inheritance flattening, DDC lookup and recooking. None of those editor services are embedded in this runtime module.

## RED lineage

The design retains RED's useful separation between material definitions, instances, typed parameters, shader techniques and extracted render data. Vanguard deliberately excludes RED serialization, depot paths, runtime inheritance chains, hardcoded material classes and renderer-owned objects.
