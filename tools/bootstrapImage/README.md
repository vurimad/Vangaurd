# Development bootstrap image

`bootstrapImage` cooks Vanguard's deterministic minimal development world and publishes the normal runtime `DATA000.vpak` image through the production asset index, package planner, numbered-package planner, and package-set assembler.

The image contains the startup input mapping, world, always-loaded cell, prefab, static mesh, material, depth and opaque pipelines, and their compiled shaders. The prefab creates one camera, one directional light, and one orange two-sided quad. Every artifact uses the normal Vanguard cooked format and reaches the package through generated dependency records; there is no test-only resource injection path.

Invoke `bootstrapImage <runtime-image-directory> <shader-source-directory>`. Visual Studio supplies the active configuration's `bin/Runtime/<Config>` directory and the repository rendering-shader directory automatically. The tool publishes `DATA000.vpak` in the runtime image directory, never infers the engine repository as a game root, and refuses to overwrite an existing image; remove or relocate an old development image deliberately before producing a replacement.

Persistent cooker/DDC state uses the platform user-cache location and is never written into `bin`.

The 10.4.2 image adds a renderer catalog and 17 shader/pipeline pairs: the 13
geometry compute stages, GBuffer visualization, directional diffuse, camera
output resolve, and optional geometry diagnostics. The image now contains 45
resources. DATA000's typed catalog reference roots their closure in package zero.
Both renderer programs and generated surface shaders use `ShaderAssetCompiler`
and the existing BuildSystem to track source, includes, settings and compiler
identity. Procedural world/material/pipeline recipes retain explicit versions.

The cooker reopens the package and verifies the renderer artifacts through
`ResourceFileReader`, including compressed payloads. This verifies cooking and
packaging; it does not validate native pipeline creation or rendered pixels.
The output owner still must supply the deferred resolve attachment signature
as part of the later viewport/output integration.
