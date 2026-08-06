# RED adaptation record

The `.vcell` and `.vworld` implementations are derived from RED's compiled streaming-sector and node-streaming architecture rather than its editor object serialization.

Primary references:

- `common/world/include/worldCompiledSector.h`
- `common/world/src/worldCompiledSector.cpp`
- `common/worldStreaming/include/worldStreamingSector.h`
- `common/worldStreaming/src/worldStreamingSector.cpp`
- `common/worldEntities/include/worldNodeEntity.h`
- `common/worldEntities/include/entityInstanceData.h`
- `common/worldEntities/include/entityInstanceData.hpp`
- `common/world/include/worldPrefab.h`
- `common/world/include/prefabProxyMeshNode.h`
- `common/world/include/prefabProxyMeshNodeInstance.h`
- `common/world/src/worldPrefab.cpp`
- `common/world/src/prefabProxyMeshNode.cpp`
- `common/world/src/prefabProxyMeshNodeInstance.cpp`
- `common/world/include/worldNodeStreamingGrid.h`
- `common/world/src/worldNodeStreamingGrid.cpp`
- `common/world/include/worldNodeStreamingProxy.h`
- `common/world/src/worldNodeStreamingProxy.cpp`
- `common/world/include/streamingProxyQuery.h`
- `common/world/src/streamingProxyQuery.cpp`
- `common/world/include/runtimeSystemNodeStreaming.h`
- `common/world/src/runtimeSystemNodeStreaming.cpp`
- `common/redReflection/src/resourceLoader.cpp`
- `common/redReflection/src/resourceLoaderScheduler.cpp`
- `common/redReflection/src/resourceToken.cpp`
- `common/worldEntities/include/worldNodeEntityProxyMesh.h`
- `common/worldEntities/src/worldNodeEntityProxyMesh.cpp`

Retained concepts include a precompiled placement table, stable global entity identity, contiguous activation ranges, a sorted identity lookup, cell-local transforms, precomputed streaming reference data, typed resource dependency collection, entity-reference resolution after creation, and instance overrides keyed by stable component identity.

RED's node class handles, patched node pointers, RTTI packages, depot resource paths and game-specific node flags are replaced by Vanguard prefab references, schema-serialized override values, stable resource IDs, portable wire records and generic placement flags. The runtime format contains no Flecs entity IDs.

For `.vworld`, retained concepts include prefab proxy meshes, hierarchical ancestor proxies, secondary/query reference points, distance-boosted long-range residency, near auto-hide thresholds, proxy-first streaming priority, child attachment accounting and anti-streaming protection until replacement content is ready. Vanguard expresses the same contracts through typed `.vmesh` references and explicit cell/proxy readiness edges. It does not import RED meshes, prefabs, sectors, resource depot paths or renderer objects.

The runtime selector directly adapts RED's `StreamingProxyQuery` structure-of-arrays layout, SIMD four-wide distance test, two-dimensional query mask, multi-observer union, and node-streaming mask algebra. In particular, the main predicted-observer query is intersected with the camera-based secondary query, the near query excludes anti-streaming-locked proxies before being subtracted, and explicit locks are united last. Stream-in candidates preserve RED's descending priority and ascending distance order. The numeric priority tiers are retained exactly while the public names remain Vanguard-native.

Vanguard keeps the boundary explicit: selection emits commands and completion notifications advance lifecycle state. Resource loading is connected through Vanguard ResourcePipeline and Jobs, while renderer attachment and Flecs materialization are not hidden inside the query object. This avoids introducing RED depot paths or runtime object types.

The execution adapter now retains RED's separation between asynchronous resource acquisition, node attachment, node detachment and deferred destruction. RED resource tokens and loader scheduling map onto Vanguard PipelineRequest and generational ResourceHandle objects; RED job batches map onto the Jobs-backed dependency pipeline. Vanguard preserves the explicit attachment boundary as resource-available and release-requested events because Flecs activation is the next layer. Depot paths, RTTI resource classes, node handles and RuntimeScene ownership are intentionally not imported.
