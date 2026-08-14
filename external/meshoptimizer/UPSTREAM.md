# meshoptimizer upstream

- Source: https://github.com/zeux/meshoptimizer
- Version: 1.2
- Git tag: `v1.2`
- Tag commit: `9d9890c73011d75920af614485296d1e03e95448`
- License: MIT (`LICENSE.md`)
- Repository copy: `external/meshoptimizer/upstream`
- Imported content: upstream `src/`, `LICENSE.md`, and `README.md`
- Local source patches: none
- Supported Vanguard platforms: Windows x86-64 initially; upstream supports major desktop and console-class C++ targets
- Owning Vanguard module: `meshTools`
- Public API exposure: none; all meshoptimizer types remain private to `meshTools`
- Upgrade responsibility: Vanguard asset-pipeline owners

The SHA-256 of the sorted `src` manifest, encoded as UTF-8 `filename<TAB>lowercase-file-sha256<LF>`, is:

`1a45a47c6ec6e1480ffe334689581a867fd53710128a2ed2771deb9b156955f7`

Additional verification:

- `LICENSE.md`: `2c420c79ff65dea863ce1990a173ebd1e6e5778116d498e54738ae5002799509`
- `src/meshoptimizer.h`: `fa3373e1017f186518bf9b16728e05ef7b85bdd4560e817b8e81fc41b429da3b`

Vanguard builds the upstream sources in a separate Premake static-library project. The integration installs Vanguard Memory allocator hooks before any meshoptimizer operation. No meshoptimizer codec is part of the `vmesh` version-1 runtime contract.

The current build excludes meshoptimizer's meshlet construction and meshlet-codec translation units. The pinned upstream files remain unmodified for provenance, but Vanguard neither compiles nor calls that feature until a future versioned renderer contract requires it.
