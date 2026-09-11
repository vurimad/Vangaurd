# Shaders

The Shaders module owns Vanguard's cooked `vshader` contract. It does not compile source languages and does not create GPU objects. Tool-side compiler adapters emit native payloads and normalized reflection through `BuildDescription`; runtime renderer backends consume validated `ShaderFile` records.

The design follows RED's separation of native shader entries, reflection records, stable program identity, and PSO compatibility metadata, while replacing RED's global shader caches with independently addressable Vanguard resources.

## Ownership boundaries

- `vshader` owns permutation identity, native stage payloads, descriptor and constant layouts, stage interfaces, and specialization constants.
- A future pipeline resource owns rasterizer, depth/stencil, blend, topology, render-target formats, sample count, and the selected shader permutation.
- Descriptor binding compatibility uses `BindingLayoutFingerprint()`.
- Material-driven shaders additionally expose separate domain and material-layout fingerprints; pipelines and materials must match both exactly.
- Pipeline descriptors and PSO caches bind against `PipelineInterfaceFingerprint()` and the selected shader permutation.
- `LayoutFingerprint()` covers the complete normalized interface and is used for whole-resource validation.

No source HLSL/Slang, editor graph, fixed-function PSO state, or backend object is stored in the runtime interface.

## Runtime path

`ShaderFile::Open` validates the Vanguard binary header, section table, section checksums, native bytecode SHA-256 digests, canonical reflection layout, all bounds and counts, and all three layout fingerprints before making the resource visible.

Descriptor reflection preserves the native CBV, SRV, UAV and sampler namespaces, so equal numeric registers such as `b0`, `t0` and `s0` may coexist in one register space. Fixed arrays carry a bounded descriptor count. Unbounded arrays carry `BindingFlags::Bindless` together with `UnboundedDescriptorCount`; runtime device capacity is selected by the renderer and is intentionally not cooked into shader identity.

`ValidatePipeline` provides the backend-independent compatibility gate. It checks program kind, primitive class, vertex inputs, render-target count and numeric classes, depth-output requirements, dual-source blending, and optional binding/pipeline fingerprints. GPU-specific PSO creation remains a renderer responsibility.

Every native stage stores both its stable entry-point identity and the original entry-point string. The identity participates in deterministic resource fingerprints; the string is retained because native shader creation requires it and must never depend on a runtime hash-to-name table.

`rendering::RenderShader` is the runtime owner of the RHI shader handles created from a validated document. It selects only bytecode compatible with the active backend, creates every stage transactionally, rolls back partial creation, exposes immutable shader identities to pipeline resolution, and deliberately contains no binding, command recording, draw, or dispatch methods.
