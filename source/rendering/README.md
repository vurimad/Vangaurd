# Vanguard Rendering Foundation

The `rendering` module will connect cooked rendering formats to the format-independent RHI. It does not expose NVRHI and does not make the RHI depend on `.vshader` or any other asset schema.

Vanguard's production renderer is bindless-first. Shader reflection remains representation-neutral, while renderer policy will compile it into pipeline interfaces, global descriptor domains and compact GPU-visible records. Materials and draw submissions will not construct immutable per-material descriptor packs or execute a parallel fixed-binding path.

Fixed binding may be introduced later as an isolated policy for constrained platforms. It must use the same logical material and shader interfaces and must not alter cooked asset schemas or the bindless runtime path.

## Pipeline ownership

`rendering::PipelineCache` is the renderer-facing asynchronous pipeline entry point. It deep-copies pipeline creation
descriptions, retains every referenced shader, binding layout and global descriptor domain for the duration of the redJobs creation task, and
coalesces requests by the concrete SHA-256 key produced by `vpipeline`. Successful requests expose a borrowed
`rhi::PipelineRef` while the request remains alive. Invalidating an entry retires the underlying RHI pipeline only after
the final request releases its generation.

The cache must be shut down before RHI shutdown. Backend-native disk persistence remains a separate private adapter;
neither native cache blobs nor NVRHI objects enter this module's public API.
