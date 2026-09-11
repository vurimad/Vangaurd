# Vanguard shader resource (`vshader`)

`vshader` is Vanguard's platform-cooked shader resource. The current format is
version 1.4 with metadata wire version 5. It stores normalized GPU interface
metadata and one native bytecode payload for each stage. Readers accept only the
current format; runtime code performs no source compilation and no intermediate
shader translation.

## RED lineage and Vanguard boundary

RED compiles preprocessed HLSL permutations into native stage blobs, hashes them for deduplication, stores reflection separately, maps stable static shader names to stage hashes, and carries allowed render-target/input-layout combinations into PSO precaching. Vanguard retains those architectural properties but does not copy `shader.cache`, `staticshader.cache`, their version history, or their fixed input-layout enums.

Each Vanguard shader is an independently addressable resource suitable for the DDC, resource graph, and VPAK. The compiler adapter may initially be synthetic and later use Slang/DXC without changing this runtime format.

## Container

The file uses the common Vanguard binary document header with magic `VSHD`,
version `1.4`, little-endian encoding, deterministic flag, and exactly two
checksummed sections.

| Section | Purpose |
|---|---|
| `META` | Program identity, fingerprints, stage records, normalized reflection, and pipeline interface |
| `CODE` | Concatenated native stage payloads in canonical stage order |

Every stage record stores its native format, entry-point identity, byte range, and SHA-256 digest. The section CRC detects transport corruption; the stage digest validates the precise bytecode identity used by the permutation.

## Canonical metadata

Writer input is normalized before emission:

- stages by stage;
- descriptor bindings by space and binding;
- constant buffers by space and binding;
- constant members by byte offset;
- vertex inputs by location;
- fragment outputs by location and blend source;
- specialization constants by identifier.

Duplicate stages, overlapping bindings, duplicate input/output locations, invalid constant ranges, out-of-range stage masks, malformed bytecode, and inconsistent program kinds are rejected.

The normalized metadata contains:

- program kind and stable program identity;
- source/compiler-derived permutation digest;
- compiler/toolchain fingerprint;
- native format and bytecode for every stage;
- descriptor binding kind, access, array count, space, binding, and stage visibility;
- exact constant-buffer member offsets, sizes, strides, scalar types, matrix dimensions, and major order;
- vertex input and fragment output signatures;
- specialization constants;
- compute thread-group dimensions;
- PSO-relevant interface flags and primitive class.

For material programs, metadata also stores one exact `MaterialDomainContract`:
the stable domain name, schema version, legal stage mask, input/output type
fingerprints, and required offline shader-capability mask. The same canonical
domain fingerprint is checked by the frozen material frontend, MPGI compiler
input, reflection probe, final native compile, pipeline reference, and VMAT.
Capability bits describe offline target requirements such as 16/64-bit numeric
types, writable or multisampled resources, comparison sampling, and acceleration
structures; they are not queries of the active runtime device.

Every logical material-resource role stores both its full reflected type
fingerprint and a compact reconstructable runtime shape. The shape records
texture dimension/array/multisample/sample form, typed/structured/raw buffer
form and structured stride, filtering/comparison sampler form, and resource
access. It participates in canonical material-layout identity. Invalid family/
shape combinations and unknown shape flags are rejected; the digest remains the
complete compatibility authority while the shape is the bounded runtime view-
construction contract.

## Fingerprints

Three SHA-256 identities intentionally have different invalidation domains.

| Fingerprint | Consumers |
|---|---|
| Binding layout | Materials, descriptor layouts, root signatures, parameter upload |
| Pipeline interface | Vertex layouts, render-target compatibility, PSO validation and cache keys |
| Complete layout | Resource integrity and broad tooling comparisons |

A change to vertex inputs therefore does not invalidate cooked material parameter values. A descriptor-layout change does not masquerade as a render-target-format change.

## PSO placement

`vshader` states what a pipeline must satisfy; it does not choose the complete pipeline. Vanguard's `vpipeline` resource owns:

- shader resource/permutation reference;
- rasterizer state;
- depth/stencil state;
- blend state;
- primitive topology;
- exact render-target/depth formats or an explicit render-graph-deferred attachment policy;
- sample count;
- vertex layout selection;
- dynamic-state policy;
- portable template and concrete PSO cache identity.

At cook time the pipeline compiler validates these fields against the shader's normalized interface. At runtime the renderer repeats the inexpensive compatibility check in development builds, creates the native PSO, and caches it using `vpipeline`'s concrete SHA-256 key. Backend-native cache blobs and PSO warm-up manifests remain separate resources so they can be invalidated by adapter, driver, backend, and build identity without recooking portable pipeline templates.
