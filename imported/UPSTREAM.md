# Retained upstream source provenance

This repository-level `imported/` tree contains auditable upstream source and
dependency images. Vanguard-owned adapters, contracts, and tests remain under
`source/`; imported images are kept as close to their authoritative source as
possible.

## RED asset-backend image

The first retained source closure for the standalone `VanguardAssetHost` was
copied on 2026-10-13 from:

```text
D:\root\R6.Root\Mainline\dev\src
```

The copied folders were byte-for-byte identical to that historical RED tree at
the time of retention. The immutable manifests below preserve that baseline;
documented local integration patches are listed separately later in this file.

| Retained image | Authoritative source | Files | Bytes | Manifest |
|---|---|---:|---:|---|
| `imported/common/resource` | `common/resource` | 89 | 710,478 | `manifests/common-resource.sha256.tsv` |
| `imported/common/archive` | `common/archive` | 24 | 82,068 | `manifests/common-archive.sha256.tsv` |
| `imported/common/redCrypto` | `common/redCrypto` | 18 | 29,282 | `manifests/common-redCrypto.sha256.tsv` |
| `imported/backend/backendData` | `backend/backendData` | 125 | 547,676 | `manifests/backend-backendData.sha256.tsv` |
| `imported/backend/backendEngine` | `backend/backendEngine` | 423 | 2,933,397 | `manifests/backend-backendEngine.sha256.tsv` |
| `imported/common/core` | `common/core` | 111 | 459,898 | `manifests/common-core.sha256.tsv` |
| `imported/common/redImage` | `common/redImage` | 85 | 1,888,826 | `manifests/common-redImage.sha256.tsv` |
| `imported/common/renderBackend` | `common/renderBackend` | 29 | 167,365 | `manifests/common-renderBackend.sha256.tsv` |
| `imported/common/renderData` | `common/renderData` | 234 | 1,572,910 | `manifests/common-renderData.sha256.tsv` |
| `imported/common/resourceMaterial` | `common/resourceMaterial` | 64 | 386,589 | `manifests/common-resourceMaterial.sha256.tsv` |
| `imported/backend/backendMaterial` | `backend/backendMaterial` | 75 | 388,705 | `manifests/backend-backendMaterial.sha256.tsv` |
| `imported/common/resourceMesh` | `common/resourceMesh` | 68 | 413,769 | `manifests/common-resourceMesh.sha256.tsv` |
| `imported/backend/backendMesh` | `backend/backendMesh` | 129 | 1,479,412 | `manifests/backend-backendMesh.sha256.tsv` |
| `imported/backend/backendResources` | `backend/backendResources` | 189 | 1,286,708 | `manifests/backend-backendResources.sha256.tsv` |
| `imported/external/DirectXTex` | `external/DirectXTex` | 59 | 27,225,878 | `manifests/external-DirectXTex.sha256.tsv` |
| `imported/external/tinyexr` | `external/tinyexr` | 2 | 467,746 | `manifests/external-tinyexr.sha256.tsv` |
| `imported/external/fbxsdk` | `external/fbxsdk` | 237 | 36,455,728 | `manifests/external-fbxsdk.sha256.tsv` |
| `imported/external/meshoptimizer` | `external/meshoptimizer` | 48 | 1,299,474 | `manifests/external-meshoptimizer.sha256.tsv` |
| selected `imported/common/engine` mesh-buffer helper | selected `common/engine` files | 3 | 7,845 | `manifests/common-engine-meshbuffer.sha256.tsv` |

Each manifest records the SHA-256, byte count, and relative path of every file
in the retained folder. Their own SHA-256 values are:

| Manifest | SHA-256 |
|---|---|
| `common-resource.sha256.tsv` | `980b15440377a5bae018991e25cf43879e4e4db646e04ffce257704c951bb06f` |
| `common-archive.sha256.tsv` | `aa68135e0517284c95190b755109643e2ccb320912fc8d102d3f51fb74bd5e5a` |
| `common-redCrypto.sha256.tsv` | `c3f4f1fd6d494eb6550845ad1b4ffaf980637240ba7ff8f3bfd0228613d581b9` |
| `backend-backendData.sha256.tsv` | `7eac447863a61318e9bcfa5febcc4aa218d609e82fad02aed391315594556eef` |
| `backend-backendEngine.sha256.tsv` | `5b8a26e97d80d41230601070a6083946e31815a3611b6c18fc073cb2a16041fa` |
| `common-core.sha256.tsv` | `24abf3601044fec8f882e061888e752eae9f0e33384db8e1fc7d95d61d6a86f6` |
| `common-redImage.sha256.tsv` | `c5a739a932bbe69aaa67bd0e696a6935843eb665d520551565768604bca664da` |
| `common-renderBackend.sha256.tsv` | `d26ce3d810810af4799a615ca4c76db1498995146bfd6b68ef64fa8c8b61da42` |
| `common-renderData.sha256.tsv` | `94b6a9e84aa9972b8c96d6755543c37491137d410edde370fecf920ce3198ac1` |
| `common-resourceMaterial.sha256.tsv` | `1cb5e0192958bb39be41cc423037ca051675b4110020aa4acff9432da3fd2641` |
| `backend-backendMaterial.sha256.tsv` | `044d4eb7b051b9f0123580016caaebd123804ca5dfc2f7f94ad8cac545effd13` |
| `common-resourceMesh.sha256.tsv` | `2a0c692d810aad34dc2d781202ca66359f5fd2b005897462db74cdc70045e6e9` |
| `backend-backendMesh.sha256.tsv` | `a87842fa3f49ae475655f48c53ba2d0e08809a05c143843ca677a2e13ae99446` |
| `backend-backendResources.sha256.tsv` | `49315ad539b2e412681e472d6374ae37dbd8dbb0dd84bfd093f2a7b811dd6dfb` |
| `external-DirectXTex.sha256.tsv` | `224fea284c3cb735acedd0105702b7951208975ddce87e7cdd234e37d121b7d1` |
| `external-tinyexr.sha256.tsv` | `cbb43612af49e2e677c9c04579ab20c629157954b466c4110a54735e90896bba` |
| `external-fbxsdk.sha256.tsv` | `d6bcc7bfbdf25d85f92dba11973828db31ad9361a300eb173dcd7ec19db68a8b` |
| `external-meshoptimizer.sha256.tsv` | `b2675533871d504827c1d671497388df4aa1bafcbe2301c69963104c34f62cb7` |
| `common-engine-meshbuffer.sha256.tsv` | `54a104566bf4c74eb968a0d2878311cd006e93ef8951f9f79af252b27274e3c2` |

### Stage 2.2 standalone retained dependencies

The material-shadow closure needs two historical files that sit outside the
retained folder images above. They are retained byte-for-byte rather than folded
into or retroactively changing any immutable baseline manifest:

| Retained file | Authoritative source | SHA-256 |
|---|---|---|
| `imported/common/gpuApi/include/gpuApiLimits.h` | `D:\root\R6.Root\Mainline\dev\src\common\gpuApi\include\gpuApiLimits.h` | `7b4285fcbf44dd375fb48b64524748bc9c3d53f813bb179c2e9d590167dbc1b6` |
| `imported/engine/shaders/include/materialModifiersConstantBuffers.fx` | `D:\root\R6.Root\Mainline\engine\shaders\include\materialModifiersConstantBuffers.fx` | `da4ec6f37212b9de4689db155f8f69af6351c3e47ad7052effae86ed322fd020` |

Both local hashes exactly match their authoritative historical source files.

### Why these images are retained

- `common/resource` owns `C2dArray`, resource editor-depot support, and the
  CSV resource loader/saver used by the first scratch proof.
- `common/archive` is a direct dependency of the resource/backendData closure.
- `common/redCrypto` supplies RED SHA-1 used by backendData dependency hashes.
- `backend/backendData` is the authoritative RED task, dependency, freshness,
  cooker/generator, and derived-state implementation being adopted.
- `backend/backendEngine` is retained as the authoritative helper/metadata
  image. The first host intentionally compiles only the narrow helper surface
  proven necessary; retaining the complete image is not permission to activate
  the historical engine/editor closure wholesale.
- Stage 1.3 retains `common/core`, `common/redImage`, `common/renderData`,
  `common/renderBackend`, `common/resourceMaterial`, and
  `backend/backendMaterial` so the first real asset domain can use RED's
  authoritative PNG -> `CBitmapTexture` import, texture blob compilation and
  `RealTextureCooker` path without activating the historical renderer/editor.
- Stage 1.3 also retains the historical DirectXTex and tinyexr dependency images.
  They remain byte-identical to their immutable manifests; DirectXTex supplies
  the real BC7 compression implementation exercised by the host proof.
- Stage 2.3 retains `common/resourceMesh`, `backend/backendMesh`, and
  `backend/backendResources` as provenance images but compiles only the narrow
  mesh/FBX translation units listed in the Stage 2.3 proof below. It also retains
  the historical Autodesk FBX SDK 2016.1.2 image, meshoptimizer, and the three
  `common/engine` mesh-buffer helper files required by RED's CPU mesh packing
  path. Retention is not permission to activate those historical projects
  wholesale.

### Existing dependency images reused

The first AssetHost should reuse the already-retained RED substrate in this
`imported/` tree, including `redCore`, `redSystem`, `redMemory`,
`redContainers`, `redMath`, `redIO`, `redFileSystem`, `redJobs2`,
`redReflection`, `redConfig`, `redCompression`, `commProtocol`, and
`commChannel`. Their Vanguard compatibility behavior remains documented by
the corresponding module provenance records under `source/`.

### Deliberate exclusions for the first host

The first CSV proof did **not** retain or activate `backendResources`, FBX,
`backendMesh`, `backendMaterial`, `backendAnimation`,
`backendCharacter`, `backendEntities`, `backendWorld`, `backendREA`,
`backendPipeline`, PostgreSQL, remote data-cache services, RED commandlets,
editor/render/audio systems, or Houdini integration.

Stage 1.3 deliberately changes only the `backendMaterial` part of that statement:
the AssetHost now compiles four selected bitmap-texture translation units plus
their narrow `resourceMaterial`, `renderData`, `renderBackend`, and `redImage`
dependencies. `backendResources`, `backendMesh`, `backendAnimation`, world/entity
domains, and the historical renderer/scene remain inactive.

`C2dArray` is itself the canonical `.csv` resource. No CSV `IImporter` is
required, and backendData's generic cooker fallback is sufficient for the
scratch cook/restart/no-recook proof.

### Local compatibility patches

The manifests above remain the immutable baseline of the historical source
images as originally retained. The following deliberately narrow local changes
are applied on top for the standalone AssetHost build:

- `backend/backendData/src/build.h`
  - Adds a `VANGUARD_ASSET_HOST` branch that replaces the historical broad
    engine/app/world/network PCH closure with the retained low-level RED public
    headers used by the selected backendData translation units.
  - Restores `RED_CONTAINER_ALLOW_DEFAULT_POOL`, which historical
    `backendDataPublic.h` defines, and the standard-library/PCH prerequisites
    required by the retained RED utility headers.
  - Defines RapidJSON's assertion hook in terms of RED assertions before
    `resourcePublic.h` is included. This avoids the retained
    `redSystem/include/assert.h` shadowing the CRT `assert.h` through MSVC's
    include search path.
  - The normal historical include branch is unchanged.
- `backend/backendData/src/cooker.cpp`
  - The AssetHost build excludes the JSON loader/parser branch because the
    bootstrap target is deliberately CSV-only.
  - Adds the direct Jobs 2 builder include that the CSV/generic path needs;
    historically it arrived transitively through the JSON headers.
  - The generic CSV fallback itself is unchanged.
- `backend/backendData/src/dataTask.cpp`
  - Omits the unused historical `engineWatchdog.h` include only for
    `VANGUARD_ASSET_HOST`.
  - Compiles the remote data-cache download body out of the first host and
    replaces it with a fail-closed assertion/fatal path. The host deliberately
    supplies no remote data cache, so entering this path is a contract failure.
  - The normal historical task/download path is unchanged.
- `backend/backendData/src/dataTaskSystem.cpp`
  - Omits the unused historical `engineWatchdog.h` include only for
    `VANGUARD_ASSET_HOST`.
  - Explicitly value-initializes the two aggregate `job::Counter` members that
    modern MSVC no longer accepts as omitted aggregate members because the
    retained constructor is explicit. Counter state remains the historical
    default state.
- `backend/backendData/src/dependencyBinaryDatabase.cpp`
  - Adds explicit `Uint32` casts for the two index-entry span counts. The
    historical values are already bounded by the binary format's `Uint32`
    fields; this only makes the narrowing explicit for modern C++17.
- `backend/backendData/include/dataSystemInstance.h` and
  `backend/backendData/src/dataSystemInstance.cpp`
  - For `VANGUARD_ASSET_HOST` only, omit ownership of the optional remote
    data-cache and server/worker task-accumulator services. This local host does
    not deploy those services, so nonempty data-cache or server/worker
    configuration fails closed instead of pulling their service closure into
    the process.
  - The source depot/loader/resource database, dependency caches, cooked
    depot/loader, dependency database, context creation and shutdown paths are
    retained and exercised by the Stage 1.2 project proof.
- `backend/backendData/src/dependencyDatabase.cpp`
  - The AssetHost retains the real binary dependency-database factory used by
    `SystemInstance::InitCookedDepot()` and compiles out only the unused
    loose-directory database branch. Directory database requests therefore
    fail closed instead of activating the omitted loose JSON database closure.
- `common/resource/include/rapidjson.h`
  - Shortens the retained RapidJSON relative include path by one `../` after
    the imported tree moved from `source/imported/` to repository-root
    `imported/`. It continues to resolve the same retained
    `imported/external/rapidjson` image.
- `common/resource/include/backendResourceDepot.h` and
  `common/resource/src/backendResourceDepot.cpp`
  - Add `ResolveWritableResourcePath()`. The default preserves historical depot
    behavior by returning `TranslateResourcePath()`; durable editor depots can
    ask for the physical path that owns a write without changing generic path
    translation semantics.
- `common/resource/include/resourceEditorDepotImpl.h` and
  `common/resource/src/resourceEditorDepotImpl.cpp`
  - Override `ResolveWritableResourcePath()` with the existing historical
    `TranslateWritableResourcePath()` rule: update the mounted layer that already
    owns a resource, otherwise choose the last matching writable mapping. This
    prevents durable publication from silently changing overlay ownership.
- `common/resource/include/resourceSaver.h` and
  `common/resource/src/resourceSaver.cpp`
  - Extract the existing in-memory serialization sequence into
    `SerializeResource()`, which returns one complete RED file image without
    publishing it. `SaveResource()` calls the same helper and retains its normal
    write/unmark/size-check semantics. Compound transactions use the helper so
    every member is fully serialized before namespace publication begins.
- `backend/backendData/src/fileSystemFilter.h` and
  `backend/backendData/src/fileSystemFilter.cpp`
  - Add one shared transaction-artifact predicate for `.vtxn-stage`,
    `.vtxn-backup`, `.vtxn.stage`, and `.vtxn` paths.
  - The incremental watcher suppresses those artifacts and exposes
    `PushResourcePath()` so durable transactions can enqueue canonical logical
    paths after their Committed journal is durable.
- `backend/backendData/include/resourceDatabase.h` and
  `backend/backendData/src/resourceDatabase.cpp`
  - Apply the same transaction-artifact predicate to authoritative/full-scan
    filtering.
  - Add `EnqueueCommittedPaths()`, which forwards committed canonical paths into
    the installed watcher. The complete host must run/await the normal database
    update before admitting dependent cook/import success.
  - Invalidate the memoized recursive-forward dependency lookup whenever a full
    or incremental scan replaces the resource/link tables (and on `Clear()`).
    Without this, a recursive query made before an authored commit could remain
    stale even after the normal database update converged.
  - For `VANGUARD_ASSET_HOST` only, compile out the editor-facing
    `DatabaseWrapper : IResourceDatabaseProvider` RTTI adapter at the end of the
    translation unit. The standalone host owns `resdb::Database` directly, so
    retaining that unused wrapper would pull backendEngine's broad editor
    umbrella without changing database behavior.
- `backend/backendEngine/src/resourceBackend.cpp`
  - Return immediately when `res::SaveResource()` fails. Thumbnail mutation,
    GPU invalidation and `m_resourceSaved.Notify()` therefore occur only after
    the durable save succeeded and RED has unmarked the resource.
- `backend/backendEngine/src/build.h`
  - Adds an AssetHost-only minimal PCH surface for the selected importer/metadata
    translation units and suppresses the historical `backendEngineInternal.h`
    umbrella. Normal backendEngine builds are unchanged.
- `backend/backendEngine/src/resourceMetadata.cpp`
  - Omits only the JSON/Bink classifier hack in the AssetHost build, avoiding the
    unrelated broad resource-utils closure while preserving ordinary metadata
    lookup and import-metadata behavior for `.xbm`.
- `common/core/src/build.h`
  - Replaces the broad core/resource internal umbrella with the low-level public
    headers required by the retained Stage 1.3 translation units when building
    the AssetHost.
- `common/redImage/src/build.h`
  - Uses the public redImage surface under `VANGUARD_ASSET_HOST`; the historical
    `redImageInternal.h` path remains unchanged elsewhere.
- `common/renderBackend/src/build.h`
  - Adds an AssetHost-only minimal public-header closure sufficient for the real
    texture compiler, without pulling renderer/scene implementation modules.
- `common/renderData/src/build.h`
  - Adds the matching minimal public-header closure used by texture blobs and
    image processing in the standalone host.
- `common/renderData/src/renderingMemory.cpp`
  - Omits unused render-proxy/skinning/render-scene includes only for AssetHost.
    `rend::InitializeMemoryPools()` itself remains the historical implementation
    and is called process-scope by the Stage 1.3 mode; no live renderer starts.
- `common/renderData/src/imageProcessing.cpp`
  - Keeps the historical DirectXTex BC6H/BC7 hardware-compression algorithm.
    AssetHost adds only an assert compatibility bridge, local suppression of the
    retained tinyexr MSVC C5208 warning, and a test-evidence counter incremented
    after a successful D3D11 hardware BC7 compression. The counter does not
    select or replace the compression path.
- `common/resourceMaterial/src/build.h`
  - Adds the minimal AssetHost public-header prerequisites for the retained
    bitmap texture implementation instead of the broad render-data umbrella.
- `common/resourceMaterial/src/bitmapDataBuffer.cpp`
  - Allows the existing deferred-buffer direct allocation/access operations when
    `VANGUARD_ASSET_HOST` is defined even though `NO_EDITOR` remains globally
    defined. This is the narrow headless-authoring seam needed to construct RED
    source texture data; normal `NO_EDITOR` runtime builds still fail closed.
- `common/resourceMaterial/src/bitmapTexture.cpp`
  - Removes live `GRender` upload/flush behavior only in AssetHost while retaining
    source data, authored/cooked render blobs, post-load validation, and texture
    serialization. The host remains a source/cook process, not a renderer.
- `common/resourceMaterial/src/sourceTextureData.cpp`
  - Uses RED assertions for three source-texture editing checks so the retained
    authoring code compiles correctly in the standalone host include environment.
- `common/resourceMaterial/src/texture.cpp`
  - Replaces renderer-only resource cloning with a null result in AssetHost and
    includes only the render-resource type needed by the retained texture base.
- `backend/backendMaterial/src/build.h`
  - Adds the narrow public-header/PCH closure required by the selected bitmap
    importer/cooker translation units and leaves the historical internal umbrella
    unchanged for ordinary builds.
- `backend/backendMaterial/include/bitmapTextureCooker.h` and
  `backend/backendMaterial/src/bitmapTextureCooker.cpp`
  - Avoid the historical renderer header/default `GRender` dependency in
    AssetHost. The existing `IGpuDataProcessor` remains optional and the host
    passes the null processor path; RED's real image compressor remains active.
    AssetHost also ignores the importer's legacy eager multi-platform flags and
    emits exactly the cooking platform selected by its live backendData context.
    The host-only selection defaults to `PLATFORM_None` and therefore fails
    closed until that context has supplied an explicit target. Ordinary RED
    builds retain the historical flag-driven behavior unchanged.
- `backend/backendMaterial/src/importBitmapTexture.cpp`
  - Uses the retained redImage/public dependencies needed by the standalone host
    and preserves the real PNG import, texture-group selection, source texture,
    render-blob compilation, and `ResourceImportMetadata` creation path.
- `backend/backendMaterial/src/textureCooking.cpp`
  - Restricts the AssetHost `RealTextureCooker` to `CBitmapTexture`/`.xbm` so the
    first real domain does not silently activate texture arrays, cubemaps,
    multilayer masks, or reflection-probe closures. Cooker version 8 is unchanged.
- `backend/backendData/src/dependencyUtils.cpp`
  - Keeps RED's historical `CollectMergeableDependencies()` implementation used
    by `MaterialCooker`, including recursive HARD dependency lookup, backlink
    closed-set pruning and deterministic post-load ordering.
  - Under `VANGUARD_ASSET_HOST` only, omits unrelated TweakDB/script/game,
    dependency-export/debug and task-runner helpers so the material cooker does
    not pull those broad domains into the standalone host.
- `common/renderData/include/materialDataProvider.h`
  - Adds explicit forward declarations for `IRenderResource` and
    `TechniqueFeatures`, replacing historical PCH/include-order assumptions for
    the selected render-data RTTI translation units without changing either type.
- `common/renderData/include/materialModifier.h`
  - Under `VANGUARD_ASSET_HOST`, resolves the unchanged historical material
    modifier constant-buffer include from its retained repository-local shader
    path. Ordinary RED builds keep the original source-tree-relative include.
- `common/resourceMaterial/src/material.cpp`
  - Compiles out live renderer update/resource extraction only for the headless
    AssetHost. Specialized skin/hair/foliage/gradient/multilayer/terrain resource
    extraction fails closed instead of activating those omitted domains; ordinary
    authored/cooked material and supported parameter behavior is retained.
- `common/resourceMaterial/src/materialDefinition.cpp`
  - Omits the live renderer include and returns no runtime render resource in the
    headless AssetHost while preserving the material-definition resource data.
- `common/resourceMaterial/src/materialInstance.cpp`
  - Omits renderer/debug-page dependencies and live render-resource creation in
    AssetHost. Specialized omitted resource parameter type matching and legacy
    cube/texture-array pointer-conversion branches fail closed; ordinary texture,
    scalar/vector/color/name parameter serialization remains active.
- `common/resourceMaterial/src/materialParameter.cpp`
  - Keeps core/fundamental and bitmap-texture parameter RTTI, but under
    `VANGUARD_ASSET_HOST` does not create or register resource properties for the
    seven omitted skin/hair/foliage/gradient/multilayer/terrain parameter domains.
    Their property accessors return null in the host rather than dragging those
    resource implementations online.
- `common/resourceMaterial/src/materialTemplate.cpp`
  - Honors the existing `NO_RUNTIME_MATERIAL_COMPILATION` host policy around
    shader-cache/compiler-only dependencies, omits live renderer upload in the
    headless host, and directly includes the retained render public enums needed
    by the selected material RTTI/default-value path.
- Stage 2.3 adds narrowly guarded mesh/import compatibility changes:
  - `backend/backendResources/src/build.h`, `fbxCommon.cpp`, and
    `fbxMeshImporter.cpp` keep the native RED FBX importer while excluding the
    unused FBX exporter/editor closure, supplying the selected host PCH
    prerequisites, and using the real CPU mesh-data flush without GPU upload.
  - `backend/backendMesh/src/build.h`, `meshCompilationSource.cpp`,
    `meshCooking.cpp`, `meshDataView.cpp`, and `meshOptimizer.cpp` preserve
    ordinary mesh dependency collection, packing, CPU render-blob compilation
    and `cooker::SaveResource`. Before source loading, the headless cooker now
    validates every serialized export class against the narrow registered RTTI
    closure so omitted specialized mesh parameters cannot be silently skipped;
    road-proxy material dependency/conversion paths are excluded and road-proxy
    meshes are rejected explicitly. Physics, garment, destruction, morph-target,
    material-layer reimport and SpeedTree-wind specializations therefore fail
    closed without activating their historical domains.
  - `common/resourceMesh/include/mesh.h`,
    `include/meshMaterialBuffer.h`, `src/build.h`, `mesh.cpp`,
    `meshAppearance.cpp`, `meshMaterialBuffer.cpp`, and `meshUtils.cpp`
    make the canonical mesh resource usable in a `NO_EDITOR` authoring host:
    CPU authored/cooked state and editor material-buffer mutation required by the
    importer remain active, while renderer uploads/debug rendering and omitted
    physics/occluder helpers stay inactive.
  - `common/renderBackend/src/build.h` and `renderMeshCompiler.cpp`,
    `common/renderData/src/build.h` and `include/bufferChunkType.h`, plus the
    selected `common/engine` `renderMeshBufferWriter` header/source remove
    historical PCH/include-order assumptions and exclude only the optional
    SpeedTree wind payload. RED's ordinary CPU mesh packing/compiler path is
    retained.

The original copy was verified before these patches with zero missing files,
zero extra files, and zero SHA-256 mismatches against the authoritative source.
Only the exact files listed below had their retained read-only attribute cleared
when a local integration patch was required. The current retained-tree audit is
65 changed files and zero missing baseline files across the retained images
covered by these manifests. The three-file `common-engine-meshbuffer` manifest
is intentionally partial; two unrelated pre-existing `common/engine` headers
remain outside its scope and are not Stage 2.3 additions. Their current local
SHA-256 values are:

| Patched file | Local SHA-256 |
|---|---|
| `common/resource/include/rapidjson.h` | `ef061d8b1fb54c633425afa2a275ed8de93f4cda4d2de3c8a5ff4fa704f5bfac` |
| `common/resource/include/backendResourceDepot.h` | `29bd691ffc71063db57dbf1b6b8bbd73d823f64f9a9c32a4ca7a784dd3eaf137` |
| `common/resource/include/resourceEditorDepotImpl.h` | `483b3033cf6ce6a0e77ee0992d739ade154dae801d8ac961ff6514c601e8c8ab` |
| `common/resource/include/resourceSaver.h` | `fd6eb8ef39ac3e5a8f3b5f21929cf24b295e48fadb66220601e556fad7dc5fd5` |
| `common/resource/src/backendResourceDepot.cpp` | `3260cb2812c7349e0563e2f468ba0e3b33c5cb3e91d64be3ccf9f184b25e7ab4` |
| `common/resource/src/resourceEditorDepotImpl.cpp` | `4ed77369046dd04aebbe27153b5202f60bf9009f1f7c015c14a51fcf18d8a6a4` |
| `common/resource/src/resourceSaver.cpp` | `923bb1d2533306d07033c45c7ea920023a8620f95335e1094775b752b3464982` |
| `backend/backendData/src/build.h` | `197d9bcf8f590cfa1617235a34ebda07a13fa52cfb2817025247e6d117efb14d` |
| `backend/backendData/src/cooker.cpp` | `d69323d11335a3bb27f706afd56c3942de04728ea9816046b2abe1c166ecba1c` |
| `backend/backendData/src/dataTask.cpp` | `89060b2fdf93aff8e2a2d049f9880ef96ff3437a2e3c7cbb78af449e8aff713a` |
| `backend/backendData/src/dataTaskSystem.cpp` | `76dd4738fe7f96124643e9839ac73c2b5267108cae4e74f864b935805d480960` |
| `backend/backendData/src/dependencyBinaryDatabase.cpp` | `632909bea559d8566babfa26772a78764c8622941e75ae06ad5e1d4477664f49` |
| `backend/backendData/include/dataSystemInstance.h` | `f104571c2ab03c1c84ed4d8b29b0784e9e9b043e1bee4975f141baee7b6a5e2d` |
| `backend/backendData/src/dataSystemInstance.cpp` | `115c68afbd3e4e22a55d0f77daf357dfbcc03dbd66bac547562a88b264fd105c` |
| `backend/backendData/src/dependencyDatabase.cpp` | `adfc85e22b4360cc9566fe58aef56339d28a31349107d088b18de8b3232eb035` |
| `backend/backendData/src/dependencyUtils.cpp` | `7cfeda393bae0c87268b0ab9fe2ac5190d2b5823869c4445d90685eb2dc2532c` |
| `backend/backendData/include/resourceDatabase.h` | `a2bc97686b54390b699a677a603335a6468c2ea180f833d2cc112c9c3ec1b4fe` |
| `backend/backendData/src/fileSystemFilter.cpp` | `27540fde542c0c3864ec35822413b437bf466b987f8d4e52a78d20cf9a4fb48e` |
| `backend/backendData/src/fileSystemFilter.h` | `318535555b4bb70fa7d9e7a32f5b5b0ee30abed9fce6557b5af86d4e968e0aac` |
| `backend/backendData/src/resourceDatabase.cpp` | `25015c0d9514f2ead5954b4583175406ce7f269113f96ea9ceb8417d0cb9c964` |
| `backend/backendEngine/src/resourceBackend.cpp` | `92a5a852085b00a69480d22de126032d7348c35bd4f97069017a81cb1fcf9722` |
| `backend/backendEngine/src/build.h` | `cc3827ce461d3fec771a2333a2534bf852f4060a49ca834b3acf20bc7aee97dc` |
| `backend/backendEngine/src/resourceMetadata.cpp` | `d886cc668d12655e6b9ccb2eb0db921a77ff60808bb16005efd0edfdc20f591b` |
| `common/core/src/build.h` | `fcd0ac4265c8c122adf67d82b90089773a434f6cf2da84a3e98d7cf7b5f56c7a` |
| `common/redImage/src/build.h` | `7b9d79d0e6ccc251e56e1376eec1ecad58e8c083ab421527c91d00a040b38886` |
| `common/renderBackend/src/build.h` | `1f186e2ad5e05ee44d5b28a42058e58f9a1a67caeaa76017332994f9787d2b47` |
| `common/renderData/src/build.h` | `f74b0089ca9170ba68df3a10d6f5c93581b209372b99494104749e21c37b7a68` |
| `common/renderData/src/imageProcessing.cpp` | `759c28db2973a928970375e9f2e95db2a942f99738d799416a8463ac521b8c6d` |
| `common/renderData/src/renderingMemory.cpp` | `9a267a8fa1d89b2a19ea0b737b5930751ecd64d5dfdb3898c3ef8911b61353dd` |
| `common/renderData/include/materialDataProvider.h` | `67495cc83d3056c932ef495aba0e0bd9be9a1fb5e72b5225ecebd025af9b4ec3` |
| `common/renderData/include/materialModifier.h` | `5e35548729a47561350726b6d317a2660f8ce7f67b9ccfbee60f259891860921` |
| `common/resourceMaterial/src/bitmapDataBuffer.cpp` | `2ff3fa55763b1252a1c2f09c36a49d81fabff3bec56025e31347b49537f0278e` |
| `common/resourceMaterial/src/bitmapTexture.cpp` | `cf3d6bdb67c3016f8c92a5b0ea5a6490f394b04626b948039175b7f9cbe6ddc1` |
| `common/resourceMaterial/src/build.h` | `eb4cf4225b35b042e1b126ae901a1fce4fca86629bca940e1ab1b347e6326bb2` |
| `common/resourceMaterial/src/material.cpp` | `9cb7fd08b3a6bf5fe29eb23d90ee5b35724bdc4adf0616cff09d2248c0dc565c` |
| `common/resourceMaterial/src/materialDefinition.cpp` | `a43a993662ca1df9a0735c7771922f2f28f249091c4f39af55e1f03f9b1994fe` |
| `common/resourceMaterial/src/materialInstance.cpp` | `217148ce130855d9102ea83c25249117b67363e321f30bf3bd6eb3a7dd42f7a8` |
| `common/resourceMaterial/src/materialParameter.cpp` | `0ba5b5aa6ef0f6d1ad1944e951fb44eb311238cf9e36cf83341a3c4e12dbb652` |
| `common/resourceMaterial/src/materialTemplate.cpp` | `897b2abc710f4bad1c2084d7468aca4ddb15c60fa02f3b8fab311deb6cb70c29` |
| `common/resourceMaterial/src/sourceTextureData.cpp` | `42bea18115f38869599497220d21beb527e3f23520780d8d5af16ee93ff19239` |
| `common/resourceMaterial/src/texture.cpp` | `a74e1a3f101c8efbde48c9bddc93fbe3b5802eaa60d1637d3bcee3c5eae5af4d` |
| `backend/backendMaterial/include/bitmapTextureCooker.h` | `5e51c2f6ddec0d311d022075fd37ee21ad1ceec3a3c83f2a277e40f3f657a9ed` |
| `backend/backendMaterial/src/bitmapTextureCooker.cpp` | `3702658161535cb43cd4e874e582f44d3a99770b9dff6d3f91f8f92c97e7e595` |
| `backend/backendMaterial/src/build.h` | `0cff46049f6b88b46e7ee867c641006a45886fba2f22460cdd3f1b45a91a0d75` |
| `backend/backendMaterial/src/importBitmapTexture.cpp` | `43359f8de4b2256389b2ca353d4fa0d6e5c17d221a262731e60029e1f420208e` |
| `backend/backendMaterial/src/textureCooking.cpp` | `ab72b9021d65c480719e13aa4163a66a45e2b35051a5b8b9a1eb9bfaeff76363` |
| `backend/backendMesh/src/build.h` | `16d092c341a6537657e03dc8fb3282b51b95584bc67dfdfe7ca5793e75a9ba6f` |
| `backend/backendMesh/src/meshCompilationSource.cpp` | `56c246e94bac91725fb80eee7a9fd475dd137b4a7c24f63f85dd5b5d776b271d` |
| `backend/backendMesh/src/meshCooking.cpp` | `b5affb6be100aeae78bc2364435989e9d9a33c5830bd3547bb69592ebb9c7a22` |
| `backend/backendMesh/src/meshDataView.cpp` | `0278e84c6368deb2f2afcf7b3faa060b0da002438ec08fdc3b7ca8374b9e8001` |
| `backend/backendMesh/src/meshOptimizer.cpp` | `1b4a779cc6456b7111090cb329c76a23f7b8192e4c5a890ba5ef8e60198ea658` |
| `backend/backendResources/src/build.h` | `f13b73cce800ea9cf9ec09ac02961a00de8cc1136cb1d9f9fd46bb0ec54f80cb` |
| `backend/backendResources/src/fbxCommon.cpp` | `40ebf009dd0e7b560ca3dca815279b24304d14888e07b84d0840631974c7f2a6` |
| `backend/backendResources/src/fbxMeshImporter.cpp` | `51e22725a8c403950d3ec9c83221719e7d1c4a9d72bfffba0df086cb29f85fb0` |
| `common/engine/include/renderMeshBufferWriter.h` | `6cbe1e9de7a3691d0e955517abfba98da63b65dd66dcb5bc9717243fb9f27a38` |
| `common/engine/src/renderMeshBufferWriter.cpp` | `9cc87a7ae1ecdba1e242eef4d9d729b7798c209cd3cfc7f7c0a7cb2d10c5a448` |
| `common/renderBackend/src/renderMeshCompiler.cpp` | `ab7450e8baa07be60e9cd9ff19d0191e87ce4e67bf3951f2e88315acd372d230` |
| `common/renderData/include/bufferChunkType.h` | `1c9a656d7866bf0ece3a4eab5c7cf159362cc3b617586273eafee58f676872f1` |
| `common/resourceMesh/include/mesh.h` | `0def32bd76d249948508bc9c0e2ce0867c415c166eb2dcde1b65a83e019fa8cc` |
| `common/resourceMesh/include/meshMaterialBuffer.h` | `cffc5ecedaf05569e836cb74a5b8c2c9d8cdac9337acb4724c314e477ff95999` |
| `common/resourceMesh/src/build.h` | `8ce1070d8749fbb5752a6fa356c2e7b37d8d3360d6350dabdd3ca4f6cfa626dc` |
| `common/resourceMesh/src/mesh.cpp` | `b43e88517b2a71bf92951636b820af96af5a1ef7a8d5d24df1362c6a82aeed44` |
| `common/resourceMesh/src/meshAppearance.cpp` | `f79317238bf9e5eea28a39ef1293040187287d95dafa8c8f7e2a40491dd53a17` |
| `common/resourceMesh/src/meshMaterialBuffer.cpp` | `93e814ad3ef66609dfcd0480491954463422cf9b37276779c576d082f275267e` |
| `common/resourceMesh/src/meshUtils.cpp` | `d06621982f2d0e0250d138a9b7a9bb95e02c7250f8baee2a9c784ace77197123` |

Any future edit inside one of those folders must be minimal, must preserve RED
semantics unless an explicitly approved incompatibility requires otherwise, and
must be recorded here with the upstream file, exact change, and reason.

### Vanguard-owned bootstrap, Stage 1.1 and Stage 1.2 compatibility seams

The standalone CSV proof keeps host-only link glue outside the retained source
images in `tools/assetHost/src/red_asset_host_bridge.cpp`:

- it repeats the `data::TargetPlatform` RTTI enum registration from historical
  `backendData/src/dataPlatform.cpp` without pulling that file's unrelated
  `common/app/include/appPath.h` closure;
- it supplies `IsCsvResource` and `IsJsonResource` with the same extension tests
  as historical `backendEngine/src/resourceUtils.cpp`, avoiding that file's
  resourceMaterial/bink classification closure. This is a CSV-bootstrap link
  seam only; it is not a replacement implementation for backendEngine.

The checked-in `VanguardAssetHost` Premake target directly compiles the selected
retained translation units so their static RTTI registrators are linked without
the historical generated module `typeRegistryFuncImpl.h` aggregators. It
explicitly initializes `data::PoolDataSystem` and copies the retained NVTools
and configuration-appropriate Oodle runtime DLLs to the tool output directory.
Fresh VS2022 generation now succeeds, and the canonical target has been built in
both x64 Development and Debug with those postbuild runtime copies present. The
canonical Development executable also reproduces the two-process CSV
cook/restart/no-recook proof from a fresh scratch root.
The Gate 0 slice additionally compiles the real retained
`ResourceEditorDepot`/filesystem-depot implementation, the narrow retained
`common/archive` support it requires, the real `resourceSaver.cpp` and CSV saver,
and backendData's filesystem watcher. Vanguard-owned `DurableResourceDepot`
subclasses `ResourceEditorDepot`, so the object installed through
`SetEditorDepot()` is itself the durable publication boundary; default
`ResourceSavingOptions`, backendData source access, and direct editor-depot
writers cannot bypass it. That Gate 0 dependency slice requires no
`backendResources` or backendEngine implementation translation unit; the current
combined AssetHost target later adds only the selected Stage 1.3 importer and
metadata helpers documented below.

Gate 0 intentionally did not link `resourceDatabase.cpp`; its full historical
dependency scanner crosses into unretained `worldEntities`, `resourceMesh` and
`world` implementation closures. Stage 1.1 now compiles the real retained
`resdb::Database` translation unit and exercises its real cache, full/incremental
update, watcher and recursive-lookup state. The host-local
`tools/assetHost/src/stage1_resource_database_bridge.cpp` supplies only the link
surface needed to keep that lifecycle proof narrow: depot directory enumeration,
simple path classifiers, plain-text CSV dependency discovery, historical-style
quoted-path `.tweak` discovery, ordinary RED file-table import discovery, and
link-file handling. CR2W/binary CSV currently fails closed because faithfully
walking C2dArray cells would require the omitted serialization-loader closure;
asset types that require omitted embedded entity/prefab/appearance scanners fail
closed for the same reason. Stage 2.3 extends only the now-activated
`mesh`/`w2mesh` case: it locates RED's serialized
`localMaterialBuffer.rawDataHeaders/rawData`, scans each embedded material
instance with the same RED file-table import walker, rejects unexpected inplace
imports and publishes those embedded imports as the historical mesh soft
dependencies. This is the exact dependency shape exercised by the native FBX
fixture; it is not a generic Vanguard mesh dependency model. Non-OK
resource-database scan status blocks `TaskSystem` admission. The bridge remains
a narrow bootstrap compatibility seam, not authorization to approximate
unsupported RED domain scanners.

The Stage 1.1 host binds `DurableResourceDepot`'s post-`PublishCommit()` canonical
path sink to `Database::EnqueueCommittedPaths()`, completes transaction cleanup,
then synchronously runs `Database::Update()` before `TaskSystem::Build()` is
allowed. Startup transaction recovery runs before `Database::Initialise()` or
watcher installation. Development and Debug runtime proofs cover clean commit,
interrupted-`Applying` rollback on the next process, full/incremental transaction
artifact filtering, stale-before-update/converged-after-update witnesses, and
recursive dependency-cache invalidation. The Stage 1.1 proof itself has no
dependency on backendEngine implementation code, `backendResources`, `world`,
`worldEntities`, `resourceMesh`, or `resourceMaterial`; the current combined
AssetHost binary contains only the later Stage 1.3-selected backendEngine and
texture-domain translation units documented below.

Stage 1.2 now compiles the real retained `dataSystemInstance.cpp`,
`dependencyDatabase.cpp` and unchanged `singleTreeResourceDepot.cpp`. The host
installs the durable editor/general source depot globals and a real uncooked
`ResourceLoader`/`GResourceLoader` before `SystemInstance::InitSourceDepot()`;
the SystemInstance then owns the source resource database/watcher and hash cache,
cooked depot/loader, binary dependency database and cooked hash cache. After
initialisation, the durable committed-path sink targets the SystemInstance-owned
source database and the Stage 1.1 scan-status gate remains the admission barrier
for `TaskSystem` work.

The Stage 1.2 fixture is the disposable Vanguard project
`games/RED Asset Test/red-asset-test.vproject`. `--stage1-system` validates that
exact project identity, including its project UUID, before filesystem
initialisation can create project-local cache directories. The proof durably
authors `stage12_probe.csv`, loads it through the real source `ResourceLoader`,
cooks it through a `SystemContext` created by the real `SystemInstance`, then
destroys the SystemInstance before the borrowed source loader/depot. Development
proves first cook and process-restart no-recook; Debug repeats the real lifecycle
and persisted no-recook path. Project Cardinal and the real lieutenantHead GLB
are not touched.

Remote data-cache services, server/worker task accumulation, the loose
dependency-database backend, `backendResources`, and the omitted asset-domain
scanner closures remain inactive. Those are explicit future domain decisions,
not implicit dependencies of the local ResourceLoader/SystemInstance proof.

The retained `ResourceBackend::SaveResource()` ordering remains checked as a
source invariant: failed `res::SaveResource()` returns before thumbnail-cache
mutation, GPU-size invalidation, or `m_resourceSaved.Notify()`.

### Stage 1.3 bitmap-texture domain proof

Stage 1.3 activates the first real RED asset domain without expanding into the
historical renderer or unrelated material/resource families. The
`VanguardAssetHost --stage1-texture` path initializes only redImage and
renderData CPU memory pools in addition to the existing AssetHost runtime, then
uses RTTI registration from the selected translation units to discover the real
PNG `CBitmapTexture` importer and `RealTextureCooker` version 8.

The disposable fixture is `games/RED Asset Test/SourceAssets/REDAssetHost/Brick.png`.
It was copied from the user-supplied source before the proof; all mutation tests
operate only on that project-local copy. The resulting canonical authored RED
resource is `Assets/REDAssetHost/textures/brick.xbm`. The source `ResourceLoader`
loads that `.xbm` with `skipUploadRenderData` so no historical GPU texture is
instantiated in the headless host.

The first Development import proved the real D3D11 hardware BC7 branch was used,
not merely that a BC7-formatted blob was produced: the host-only evidence counter
is incremented only after successful `DirectX::Compress(ID3D11Device*, ...)`
inside the existing `D3D_DRIVER_TYPE_HARDWARE` path. The authored `.xbm` then
converged through the authoritative source database and cooked through the real
backendData task/cooker pipeline. A fresh process restart produced build reason
zero and reused the existing cooked output.

For incremental invalidation, one pixel of only the disposable PNG copy was
changed and `--stage1-texture-reimport` ran the same real importer again. The
authored `.xbm` SHA-256 changed, the hardware BC7 branch ran again, backendData
reported a nonzero source-change build reason, and the cooked file timestamp was
rewritten. The compressed cooked bytes happened to remain identical for that
one-bit source perturbation; this does not weaken the recook proof because task
freshness and the output publication timestamp independently show the cook ran.
The following normal restart again produced build reason zero/no recook.

Full x64 Development and Debug `VanguardAssetHost` rebuilds complete with zero
warnings and zero errors. Both configurations reproduce the Stage 1.3 persisted
no-recook path, and the Stage 1.1 resource-database and Stage 1.2 real
`SystemInstance` lifecycle proofs remain green. At the Stage 1.3 closure point,
the generated AssetHost project contained only the selected texture-domain
translation units: four `resourceMaterial`, four `backendMaterial`, nine
`renderData`, and one `renderBackend` source file. Stage 2.2 intentionally adds
the narrow material translation units documented below; it still does not
activate `backendResources`, `resourceMesh`, world, world-entity, backend-world,
the historical renderer, or render-scene source files.

### Stage 2.2 material shadow proof

Stage 2.2 adds the smallest headless material closure needed for a canonical
`CMaterialTemplate` plus `CMaterialInstance` dependency graph. The generated
AssetHost target now contains 11 selected `resourceMaterial`, five
`backendMaterial`, 13 `renderData`, and one `renderBackend` translation units.
The material-specific additions include `materialCooking.cpp`, the material
resource/parameter/template implementation, `pipelineStateObject.cpp`, and the
three render-data RTTI owners `materialFeatureFlags.cpp`,
`materialStructures.cpp`, and `renderPublicEnums.cpp`. `renderer.cpp`,
`backendResources`, mesh, world/entity, backend-world and render-scene sources
remain absent from the generated project.

`MaterialCooker` retains RED's historical `CollectMergeableDependencies()`
algorithm through the narrow `dependencyUtils.cpp` compatibility branch. The
host admits ordinary texture/scalar/vector/color/name material parameters and
fails closed on specialized skin, hair, foliage, gradient, multilayer and
terrain resource-parameter domains rather than retaining those unrelated
resource implementations. Renderer upload/runtime material compilation is also
kept inactive under the existing `NO_EDITOR` and
`NO_RUNTIME_MATERIAL_COMPILATION` host policies.

The disposable fixture is `materials/stage22_probe.mt` plus
`materials/stage22_probe.mi`, with one pixel-shader `ProbeTexture` parameter.
The canonical mode binds the already-proved `textures/brick.xbm`; the dedicated
semantic-edit mode binds the project-local `textures/color2048.xbm` through
`CMaterialInstance::WriteParameter()` plus the same durable RED save path. Both
modes converge the real RED source database and build only the `.mi` root through
`TaskSystem`; RED discovers the `.mt` and `.xbm` dependency tasks. The material
instance is handled by `MaterialCooker`; the `.mt` task has no material cooker and
therefore uses backendData's normal no-`ICooker` source-to-cooked fallback.

The first Development material run produced exactly three tasks and build
reasons `.mi=0x00001002`, `.mt=0x00001000`, `.xbm=0`, with both cooked material
files present. An immediate process restart returned reason zero for all three.
Switching the texture parameter through RED APIs rebuilt only the material
instance with reason `0x00000003`; the unchanged rerun returned all-zero reasons.
`0x00000003` is RED's own `SOURCE_DEPS_CHANGED | BUILT_DEPS_CHANGED` bitmask, not
a Vanguard force-build code path.

Dependency propagation was then proved independently through the Stage 2.1
external-source boundary: one pixel of only the disposable project-local
`Color2048.png` was changed while the Stage 2.1 observer was active. Exactly one
binding was observed, reimported and rebuilt by RED. A subsequent build with only
the `.mi` root again returned material reason `0x00000003` while the texture task
was already current at reason zero. No watcher-owned freshness rule or manual
material invalidation was used.

Final harness hardening makes each material mode converge its own target binding
before validation, reports whether that binding actually changed, and then
stabilizes to a true no-op on repetition. Development and Debug builds complete
with zero warnings/errors; the settled Debug material proof returns all-zero
reasons. Stage 1.1, Stage 1.2 and Stage 1.3 regressions remain green, and the
Stage 2.2 wrong-project guard returns code 6 before runtime/project-local
initialization.

### Stage 2.3 native RED FBX mesh shadow proof

Stage 2.3 intentionally uses RED's historical FBX frontend first rather than
Vanguard's GLB parser. `VanguardAssetHost --stage2-mesh-shadow` copies the
user-supplied
`C:\Users\Ark\Desktop\Model 2\lieutenantHead.fbx` only when the disposable
project-local source
`SourceAssets/REDAssetHost/models/lieutenantHead.fbx` is absent. The Desktop
source remained byte-identical throughout the proof at SHA-256
`43eaa9f102ba2ae984321921c52916cfdd4d061924fa3df10d0e5c1018a41472`;
the final project-local source hash matches it exactly.

The real registered `CFBXMeshImporter` consumes that project-local FBX and
authors one canonical
`Assets/REDAssetHost/models/stage23_lieutenant_head.w2mesh`. The importer keeps
its historical `engine\materials\metal_base.remt` fallback unchanged; the
test project supplies a minimal valid `CMaterialTemplate` at that exact path
instead of retaining an engine-wide material depot. Because the native importer
returns one `CMesh` rather than a resource cohort, Gate 0 compound publication
is not required for this native FBX gate. The later GLB translation path remains
the place where a mesh/material/texture cohort may require compound publication.

The headless host keeps the real RED CPU mesh construction path: FBX scene
conversion, `mesh::DataBuilder`, vertex/index packing, meshoptimizer, the RED
render-mesh compiler's CPU blob creation, resource metadata/import options,
source database admission and `MeshCooker` version 23. GPU upload/debug-render
paths are omitted under `VANGUARD_ASSET_HOST`; physics, garment, destruction,
morph-target, SpeedTree-wind and editor material-layer specializations fail
closed rather than activating their historical domains. The host cooker
preflights the serialized mesh export table and rejects any class missing from
its selected RTTI closure, preventing RED's normal unknown-export skip behavior
from silently discarding unsupported mesh parameters. Road-proxy meshes are
also rejected before their historical proxy material/conversion specialization
can participate. `NO_EDITOR` and `NO_RUNTIME_MATERIAL_COMPILATION` remain
defined.

The first successful Development import reported one LOD, five chunks, five
material slots, 30 bones, one appearance and a non-null CPU render blob. The
material names were `Lieutenant_Head.002`, `body.001`,
`Lieutenant_Jacket.001`, `Lieutenant_Head.003` and `lens.001`. Building
only the mesh root let RED discover the fallback material task; the cold reasons
were mesh `0x00001002` and fallback material `0x00001000`. An immediate
fresh process returned reason zero for both tasks and preserved the cooked mesh
timestamp.

`--stage2-mesh-shadow-skinned` exercises a semantic reimport without modifying
either FBX file. It copies the previously serialized RED import options through
RTTI, changes only `importAsSkinnedMesh`, invokes the same
`IImporter::DoImport`, persists the new import options and lets `TaskSystem`
reevaluate only the mesh root. The edit changed the imported structure to five
skinned chunks and 13 bones and produced mesh reason `0x00000001` while the
material task stayed at zero. Repeating the edit mode returned all-zero reasons.
Returning to canonical static mode reimported once with mesh reason
`0x00000001`, then the next canonical process again returned all-zero reasons.

Both x64 Development and Debug hosts build and the settled Debug Stage 2.3 run
returns all-zero reasons. Stage 1.1, Stage 1.2 and Stage 1.3 regressions remain
green. The Stage 2.1 observer/reimport proof was rerun with one reversible
project-local `Color2048.png` pixel edit and then the exact-byte restoration;
both directions observed exactly one binding and completed a real RED reimport
and nonzero texture build. Stage 2.2 remains settled at all-zero reasons.
Both Stage 2.3 modes reject a non-test-project root with code 6 before runtime
initialization.

The generated AssetHost project contains exactly 11 selected
`common/resourceMesh`, seven `backend/backendMesh`, three
`backend/backendResources`, three `common/renderBackend`, 17
`common/renderData`, and one standalone `common/engine` mesh-buffer helper
translation unit. No backend-world, world, world-entity, physics, garment,
backend-visibility, backend-REA, historical `renderer.cpp`, or render-scene
source file is compiled. The Autodesk FBX SDK DLL is copied from the retained
Debug/Release image appropriate to the host configuration.

These host/build and documented retained-source integration changes do not alter
the retained RED baseline manifests.
The baseline manifests above continue to describe the unmodified upstream image;
Vanguard-owned host glue is intentionally not folded into those manifests.

## Repository-level imported-tree relocation

The existing retained dependency tree was moved from `source/imported/` to
repository-root `imported/` so `source/` remains Vanguard-owned module space.
The move preserved all 1,842 pre-existing files. A relocation audit found 13
Vanguard-added Premake project `location` paths whose old depth would have
escaped the repository after the move; those paths were shortened by one
`../` so they continue to target `build/%{_ACTION}/projects` under the
repository root. Imported sibling/include/temp relationships were left
unchanged.

This relocation is a repository-layout compatibility correction, not a RED
behavior change.
