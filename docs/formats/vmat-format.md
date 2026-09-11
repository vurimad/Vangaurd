# Vanguard material resource (`vmat`)

`vmat` is Vanguard's renderer-agnostic cooked material resource. The current
format is version 1.2 with metadata wire version 3. It uses the common
deterministic document envelope with magic `VMAT` and one CRC64-protected `MATL`
section. The section stores metadata and parameter bytes together because
materials are small, memory-resident resources; large textures and future buffer
resources remain independently streamable dependencies. Readers accept only the
current format; development cooks and caches are rebuilt after a contract change.

## Shader-derived contract

An annotated `vshader` material contract is the material ABI. It carries separate full
domain and program-layout fingerprints; descriptor binding-layout identity is not a
substitute for either. The shader compiler reflects one annotated parameter type and an
ordered set of logical resource roles. `WriteMaterial` requires that contract directly;
no manual material-interface description or compatibility reader exists.

| Stored record | Source of truth | Runtime use |
|---|---|---|
| Parameter range | Annotated `vshader` parameter type and reflected byte size | Allocate/upload exact raw bytes |
| Parameter | Reflected offset, size, stride, scalar shape and matrix order | Editing metadata and direct byte updates |
| Resource role | Annotated role, explicit dense slot, coarse shader kind, required flag, and offline-policy concrete asset type | Resolve only resources of the sealed expected asset type into the GPU material resource range |
| Technique | Compatible `vpipeline` resource | Pass/technique selection |

Constants are copied byte-for-byte into zero-initialized reflected buffers. The
cooker rejects unknown or ambiguous names, duplicate overrides, incorrect sizes,
wrong concrete resource types, unsupported assigned resource kinds, and required
unbound resources. An unsupported kind may remain only optional and unbound. The
current offline policy maps texture roles to `VTEX`; it does not invent cooked
asset types for buffers, samplers, or acceleration structures. The expected type
is persisted per resource slot and revalidated when the file opens. The cooker
never repacks a vector or matrix according to a Vanguard-defined material
structure.

## Techniques and pipelines

A material may name multiple techniques, such as G-buffer, forward, depth or
shadow passes. Every technique supplies an opened `vpipeline` during cooking.
At least one pipeline shader must carry the material contract, and every shader
that carries one must match the layout-authority shader's material-domain and
material-layout fingerprints and graph permutation. Technique names are canonical
identities and must be unique.

Pass programs may use different shader resources, entry points, binding layouts
and pipeline interfaces. Each pipeline still validates those identities against
its own retained shader dependencies. If it references the layout-authority
shader itself, that reference must match its exact resource generation and shader
fingerprints. Non-material stages may have no material contract. A conflicting
material-bearing stage is rejected even if another stage matches. The wire layout
is unchanged; the material compiler policy fingerprint advances for this rule.

The canonical builder can select per-technique entry points over the same
finalized generated source, graph permutation, defines and compile settings.
Callers submit `MaterialCanonicalBuildSet::Programs()` for the complete program
set, followed by its existing pipeline and material requests. `Program()` remains
the primary layout-authority program. Variant shaders remain dependencies of their
pipelines; they do not introduce another material instance or resource owner.

Pipeline state is not duplicated in `vmat`. Blending, culling, depth/stencil, vertex layout and attachment policy remain in `vpipeline`.

## Dependencies and VPAK

The reader produces a sorted and deduplicated dependency list:

- the layout-authority `vshader`, required;
- all technique `vpipeline` resources, required;
- every valid descriptor resource with its required, optional or soft classification.

VPAK does not interpret material records. Package assembly copies this dependency
list into the VPAK index and stores the complete `vmat` as a small memory-resident
segment. Required edges enter normal closure, optional edges enter closure only
when requested, and soft edges remain recorded identity-only references that
never pull their target into the package. Opening the logical package resource
reproduces the standalone byte stream exactly.

## Authoring boundary

Material graphs, templates, inherited instances and friendly parameter names may exist above this format. They are flattened before cooking so runtime loading never walks inheritance chains or recompiles graphs. No PBR model is prescribed: static meshes, skinned meshes, cloth, hair, terrain and project-specific shading models can each select a different shader-derived interface without changing `vmat`.
