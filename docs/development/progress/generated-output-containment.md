# Generated-output containment — 2026-09-16

Filesystem-backed tests now use the shared tests/support/scratch_directory.hpp
helper: platform temp/VanguardTests/configuration/suite. Authored bunny/JPEG
fixtures and engine shader sources retain separate repository input paths.
Diagnostics smoke writes a process-specific log under platform temp/VanguardTests.

Editor filesystem cache remains the project's configured DerivedData directory.
Its log opens after project resolution under configured Saved/Logs. Intermediate
processing belongs in configured Intermediate. Packaged game DATA stays beside
the packaged executable; no package placement was changed by this task.
The standalone default filesystem service now uses platform user-cache storage.
The old root cache contained only bootstrapImage VADI/VDDC artifacts and was
removed. VanguardEditor.log was already absent when cleanup reached it.

Validation: Premake generation, engine-service audit and diff check passed.
Development editor and 26 affected test projects compiled/linked successfully.
Two additional projects remained blocked at this checkpoint: entitiesTests included a
stale deleted rendering-integration header; geometryAllocatorTests had
mesh_draw_layout_tests.cpp mesh type errors. Neither blocker was changed in this task.

22 Development executables passed, launched from the engine root, including
assetStreamingTests, filesystemTests, engineServicesTests, assetIndexTests,
packagePlannerTests, meshesTests, texturesTests, materialToolsTests,
textureToolsTests and textureResidencyServiceTests. Final root inspection found
no vanguard_* artifacts, cache directory or .log files.

Executed failures retained separately: packagesTests reports checksummed
duplicate-package-number rejection; renderingTests reports paged GPU Scene
addressing mismatch. materialRuntimeServiceTests reaches native frame processing
but fails the camera surface depth-state contract and ultimately a material
scene-binding teardown assertion. These are not claimed passing or corrected.
Logs are build/contained-output-*.log and build/contained-output-run-*.log.
Editor was compiled, not launched; project log placement is source-verified.
