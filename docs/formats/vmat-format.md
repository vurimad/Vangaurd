# Vanguard material resource (`vmat`)

`vmat` is Vanguard's renderer-agnostic cooked material resource. Version 1 uses the common deterministic document envelope with magic `VMAT` and one CRC64-protected `MATL` section. The section stores metadata and parameter bytes together because materials are small, memory-resident resources; large textures and buffers remain independently streamable dependencies.

## Shader-derived contract

The shader binding-layout fingerprint is the material ABI. Cooking requires an opened `vshader` and an explicit selection of material-owned constant buffers and descriptor bindings. This selection will normally come from shader-language annotations, but it is an importer/compiler input rather than a hardcoded engine table.

| Stored record | Source of truth | Runtime use |
|---|---|---|
| Constant buffer | `vshader` space, binding and byte size | Allocate/upload an exact block |
| Parameter | Reflected offset, size, stride, scalar shape and matrix order | Editing metadata and direct byte updates |
| Resource binding | Reflected space, binding, array index and descriptor kind | Descriptor construction |
| Technique | Compatible `vpipeline` resource | Pass/technique selection |

Constants are copied byte-for-byte into zero-initialized reflected buffers. The cooker rejects unknown or ambiguous names, duplicate overrides, incorrect sizes, unsupported material descriptor kinds and required unbound resources. It never repacks a vector or matrix according to a Vanguard-defined material structure.

## Techniques and pipelines

A material may name multiple techniques, such as G-buffer, forward, depth or shadow passes. Every technique supplies an opened `vpipeline` during cooking. At least one pipeline shader reference must match the material shader resource, permutation, binding-layout fingerprint and pipeline-interface fingerprint. Technique names are canonical identities and must be unique.

Pipeline state is not duplicated in `vmat`. Blending, culling, depth/stencil, vertex layout and attachment policy remain in `vpipeline`.

## Dependencies and VPAK

The reader produces a sorted and deduplicated dependency list:

- the layout-authority `vshader`, required;
- all technique `vpipeline` resources, required;
- every valid descriptor resource with its required, optional or soft classification.

VPAK does not interpret material records. Package assembly copies this dependency list into the VPAK index and stores the complete `vmat` as a small memory-resident segment. Opening the logical package resource reproduces the standalone byte stream exactly.

## Authoring boundary

Material graphs, templates, inherited instances and friendly parameter names may exist above this format. They are flattened before cooking so runtime loading never walks inheritance chains or recompiles graphs. No PBR model is prescribed: static meshes, skinned meshes, cloth, hair, terrain and project-specific shading models can each select a different shader-derived interface without changing `vmat`.
