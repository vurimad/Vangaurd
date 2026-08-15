# Shader and Pipeline Runtime

The runtime boundary is intentionally narrow:

```text
cooked vshader -> RenderShader -> immutable RHI shader handles
cooked vpipeline + resolved RenderShader generations + render-graph attachment signature
    -> RequestRenderPipeline -> asynchronous PipelineCache -> immutable RHI pipeline handle
```

`RenderShader` owns native shader stages and their content identities. Reloading creates a new object generation; outstanding pipeline-cache requests retain the old RHI shader handles until their asynchronous creation work finishes.

`RequestRenderPipeline` does not bind resources or record commands. It validates resource identity, creates or resolves the vertex layout, converts fixed-function state, incorporates concrete render-target formats into the SHA-256 pipeline key, and hands a deep-copied description to the cache. The Render Graph will later decide when a pipeline is requested and when its completed handle is consumed.

The primary binding path is bindless. Global resource and sampler descriptor domains are renderer-owned interface resources supplied during pipeline materialization. Shader reflection remains representation-neutral, so materials and cooked pipeline documents do not encode a per-draw descriptor strategy. Fixed layouts remain an explicit exceptional input rather than an alternative renderer architecture.

Source compilation remains tool-side. Slang must emit backend-native bytecode, normalized reflection, dependency information, and a compiler/options fingerprint into `shaders::BuildDescription`. Shipping runtime code consumes only cooked documents and does not load a compiler.
