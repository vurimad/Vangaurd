# Assimp upstream

- Source: https://github.com/assimp/assimp
- Version: 6.0.5 plus 63 upstream commits
- Commit: `a939f2479513a4d92060f2348909886b67d548aa`
- Source checkout: `D:\vendors\assimp`
- Source date recorded by the previous Vanguard integration: 2026-07-15
- License: BSD 3-Clause (`LICENSE`)
- Imported content: public headers, core importer/post-process sources, OBJ, FBX, glTF 1/2/GLB, and their required contrib sources
- Local source patches: none
- Local integration shim: `vanguard/assimp_vanguard_io.*` confines Assimp's
  owning I/O interface and reports canonical opened dependencies to mesh tools
- Generated files: `include/assimp/config.h`, `include/assimp/revision.h`, and `contrib/zlib/zconf.h`
- Public API exposure: none; Assimp remains private to offline asset tooling

The curated file set follows the already validated `D:\RED Vanguard` Assimp integration. Exporters and unrelated importers are disabled to keep compile time and dependency surface controlled. Runtime resources, packages, and mesh streaming must never depend on Assimp types.

`assimpSmoke` verifies the enabled/disabled importer registry and parses an in-memory OBJ triangle. This catches an incomplete source snapshot without introducing an engine-facing importer API.
