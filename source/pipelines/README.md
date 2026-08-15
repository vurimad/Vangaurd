# Pipelines

The Pipelines module owns Vanguard's portable cooked `vpipeline` contract. It records shader dependencies, fixed-function state, dynamic-state policy, vertex layouts, render-pass attachment policy, and ray-tracing shader groups. It does not create GPU objects or store driver-specific cache data.

The architecture follows RED's separation between shader identity, full pipeline state, asynchronous PSO creation, and backend cache persistence. Vanguard strengthens the identity model with canonical serialization and SHA-256 keys, separates dynamic values from static PSO identity, and allows render-graph formats to specialize a portable pipeline template without recooking it.

## Ownership boundaries

- `vshader` owns native stage bytecode and normalized reflection.
- `vpipeline` owns portable graphics, compute, and ray-tracing pipeline templates.
- The render graph supplies a concrete attachment signature when a graphics pipeline uses deferred attachments.
- A renderer backend translates a validated concrete pipeline into a native PSO.
- The future runtime pipeline cache owns request coalescing, asynchronous creation, lifecycle state, warmup, telemetry, and backend-native cache persistence.

`ValidateShaderCompatibility` is the backend-independent gate between `vshader` reflection and a pipeline instance. `CalculateConcretePipelineKey` produces the stable key used by the future runtime cache.

Vertex attributes retain an explicit portable GPU format and the native semantic string in addition to their normalized reflection identity. This prevents runtime inference from losing signed, normalized, packed, or integer format information and allows native input-layout creation without hardcoded semantic dictionaries.

`rendering::RequestRenderPipeline` resolves the exact shader generations referenced by a validated pipeline, rejects stale fingerprints and duplicate stages, translates portable state into RHI descriptions, specializes deferred attachment formats, and submits immutable creation data to the asynchronous pipeline cache. Bindless descriptor domains and any exceptional fixed layouts are supplied as pipeline-interface resources; material resources do not choose the binding model. The factory creates no command list and performs no rendering work.
