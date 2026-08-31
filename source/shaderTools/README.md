# Shader Tools

`shaderTools` owns Vanguard's offline shader compiler integration. It compiles
authoring source into a platform-native program, normalizes compiler reflection
into Vanguard resource and pipeline-interface facts, and emits deterministic
`.vshader` documents. Compiler SDK types never cross its public API.

Reflection describes what the shader requires: descriptor kinds and access,
bounded or unbounded arrays, constant layouts, stage visibility, graphics I/O,
and compute group dimensions. It deliberately does not choose descriptor heap
placement, material binding policy, root signatures, or bound versus bindless
execution. Those decisions belong to the renderer and RHI pipeline layers.

The module is tooling-only. Runtime shader loading and pipeline creation depend
only on cooked Vanguard formats.

## Asset build integration

`ShaderAssetCompiler` registers source-shader to `vshader` compilation with the
shared asset build system. Canonical build settings carry source identity,
program/permutation identity, entry points, defines, optimization policy and
compiler profile. The requested asset target selects DXIL or SPIR-V; target,
settings and source content therefore participate in the normal DDC key.

Dependency planning recursively resolves source includes through an injected
source provider. Every include content digest and the offline compiler
fingerprint are recorded as build prerequisites before cache lookup. Compilation
loads includes through the same provider and verifies each payload against the
prepared prerequisite digest. A source modified between planning and execution
is rejected instead of being published under a stale cache key.

The provider is intentionally independent of physical files. A project source
store can supply disk content today and the shared editor source-asset layer can
supply in-memory revisions later. This module performs no directory watching,
change notification or editor orchestration.
