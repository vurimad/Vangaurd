# Shader Tools

`shaderTools` owns Vanguard's offline shader compiler integration. It accepts
authoring source and produces compiler-neutral data suitable for deterministic
`.vshader` cooking. Compiler SDK types never cross its public API.

The module is tooling-only. Runtime shader loading and pipeline creation depend
only on cooked Vanguard formats.
