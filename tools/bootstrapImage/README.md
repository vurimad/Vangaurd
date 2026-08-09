# Development bootstrap image

`bootstrapImage` cooks Vanguard's deterministic minimal development world and publishes the normal runtime `DATA000.vpak` image through the production asset index, package planner, numbered-package planner, and package-set assembler.

The image contains `input/default.vinput`, `prefabs/bootstrap.vprefab`, `world/bootstrap.vcell`, and `world/bootstrap.vworld`. The world has one always-loaded cell and one component-free entity, providing a real VPAK-to-resource-streaming-to-Flecs startup path without renderer content.

Invoke `bootstrapImage <runtime-image-directory>`. Visual Studio supplies the active configuration's `bin/Runtime/<Config>` directory automatically. The tool publishes `DATA000.vpak` there, never infers the engine repository as a game root, and refuses to overwrite an existing image; remove or relocate an old development image deliberately before producing a replacement.

Persistent cooker/DDC state uses the platform user-cache location and is never written into `bin`.
