# Materials

The Materials module owns Vanguard's cooked `vmat` contract. A material is a small immutable runtime resource containing shader-derived parameter bytes, logical typed resource parameters and named `vpipeline` techniques.

## Ownership boundaries

- `vshader` owns native shader bytecode and normalized reflection.
- `vpipeline` owns fixed-function state and the selected shader permutation.
- `vmat` selects the material-owned portion of that shader interface and supplies logical values and resource references without storing descriptor locations.
- `vtex` and future buffer resources own the referenced payloads.
- Global descriptor domains, bindless indices, GPU material records, residency and native objects belong to the renderer.
- Source graphs, inheritance and instances belong to authoring and are flattened by the cooker.

The format has no built-in PBR, cloth, hair, terrain or decal schema. A cooker selects material-owned parameter blocks from shader reflection and supplies logical resource-parameter declarations from the material interface produced by the shader toolchain. `WriteMaterial` copies reflected offsets, sizes, strides and scalar types but never descriptor spaces, registers or bindless indices. Each named technique is checked against an opened `PipelineFile` and must select a compatible shader resource and permutation.

## Runtime and streaming

`MaterialFile::Open` validates the document header, section layout, CRC64, SHA-256 content fingerprint, canonical ordering, record domains, byte ranges and dependency types before publishing any state. Parameter blocks are already laid out for GPU upload. Runtime code does not rebuild a schema by name and does not translate material values.

`Dependencies()` returns a canonical, deduplicated list containing the shader, every technique pipeline and every referenced resource. VPAK stores `vmat` as an opaque memory-resident segment and copies those dependencies into its resource table. A streamed mesh therefore resolves as:

```text
Flecs render entity -> vmesh -> material slot -> vmat
                                           |-> vpipeline -> vshader
                                           `-> vtex / buffer resources
```

The generic asset layer will later own source watching, inheritance flattening, DDC lookup and recooking. None of those editor services are embedded in this runtime module.
