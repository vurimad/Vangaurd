# Vanguard pipeline resource (`vpipeline`)

`vpipeline` is Vanguard's portable cooked pipeline-template resource. Version 1 represents graphics, compute, and ray-tracing pipelines without embedding D3D, Vulkan, Metal, console, or driver-specific structures.

## RED lineage and Vanguard boundary

RED's renderer builds a complete PSO identity from shaders and fixed-function state, coalesces concurrent requests, tracks pending/valid/invalid states, precaches combinations through Jobs, and persists backend cache data. Vanguard retains that proven layering while replacing raw API-structure hashing, 32-bit identities, fixed tables, and renderer-hardcoded combination data.

`vpipeline` is the portable layer only. Native PSO objects, backend pipeline-library blobs, driver cache UUIDs, and warmup manifests belong to later renderer/cache layers and must never alter this resource's format.

## Container and integrity

The file uses the common Vanguard document header with magic `VPLN`, version `1.0`, little-endian encoding, deterministic flag, and one CRC64-protected `PIPE` section. The section contains metadata wire version 1, the stored template fingerprint, and the canonical descriptor.

The reader validates the header, section table, checksum, bounds, enum domains, counts, cross-references, canonical ordering, and recomputed template fingerprint before exposing a `PipelineFile`.

## Pipeline kinds

| Kind | Portable contents |
|---|---|
| Graphics | One `vshader` dependency, vertex streams and attributes, topology, rasterizer, multisample, depth/stencil, blend, dynamic states, and attachment policy |
| Compute | One compute `vshader` dependency and portable identity |
| Ray tracing | One or more library `vshader` dependencies, recursion/payload/attribute limits, and named general, triangle-hit, or procedural-hit groups |

Ray-tracing groups are cooked data. Renderer code does not maintain RED-style hardcoded shader tables.

## Shader dependencies

Every shader reference stores the Vanguard resource ID plus three SHA-256 identities:

- shader permutation;
- binding layout;
- pipeline interface.

This lets the resource graph track the dependency while the pipeline compiler and runtime development checks reject stale or incompatible reflection. The `vpipeline` does not duplicate native shader bytecode.

## Attachment policy

A graphics template chooses one of two explicit policies:

- `Exact` cooks color/depth formats, numeric classes, and sample count into the template.
- `Deferred` leaves the attachment signature to the render graph when a concrete PSO is requested.

Deferred attachment selection is not a runtime format translation. The renderer supplies native format IDs already selected for the backend; those IDs specialize the concrete PSO key and are passed directly to native pipeline creation. Exact templates reject a different supplied signature.

## Dynamic-state separation

Viewport, scissor, blend constants, stencil reference, depth bias, depth bounds, primitive topology, and fragment shading rate can be declared dynamic. Values covered by dynamic depth-bias and depth-bounds policy are normalized out of the static template. This prevents command-time state from multiplying PSO identities or producing misleading cache misses.

## Canonical identity

Input is normalized before serialization:

- shaders by resource and fingerprints, with ray-group indices remapped;
- vertex streams by binding;
- vertex attributes by location;
- ray-tracing groups by stable name;
- floating-point negative zero to positive zero;
- dynamic fixed-function values to canonical defaults.

The SHA-256 template fingerprint covers this canonical descriptor. The concrete key covers the template fingerprint and, for graphics, the selected attachment signature. Reordering tool output therefore produces identical bytes and keys, while a format or interface change produces a different concrete identity.

## Runtime cache boundary

Vanguard's `pipelineCache` module consumes concrete keys and creates native PSOs asynchronously. It provides RED-style request coalescing, explicit pending/valid/invalid lifecycle, bounded Jobs dispatch, structured failure evidence, warmup, generational invalidation, and editor-visible telemetry. The renderer supplies the NVRHI-backed creation and destruction callbacks, while retained request handles keep old native objects alive safely across invalidation.

Backend-native cache data is implemented by `NativeCacheStore`. It is keyed by adapter, driver, backend, cache schema, and build identity and is published atomically outside `vpipeline`; see `vnative-pipeline-cache-format.md`.
