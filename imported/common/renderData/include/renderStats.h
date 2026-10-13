/**
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "../../redContainers/include/fixedArray.h"

class IRenderProxy;
class IRenderProxyLight;

//*********************************************************************************************************************************************************//
//*********************************************************************************************************************************************************//

struct GeometryBatcherStats
{
	Uint32 m_numDrawCalls;

	Uint32 m_numMeshProxies;

	Uint32 m_numInstances;
	Uint32 m_numInstancedBatches;
	Uint32 m_biggestInstancedBatch;
	Uint32 m_smallestInstancedBatch;
	// TODO batch size histogram

	// how many static/dynamic instance buffer was rebound
	Uint32 m_numStaticInstanceBufferBinding;
	Uint32 m_numDynamicInstanceBufferBinding;

	Uint32 m_numChunks;
	Uint32 m_numTriangles;
	Uint32 m_numVertices;

	Uint32 m_numChunksSkinned;
	Uint32 m_numTrianglesSkinned;
	Uint32 m_numVerticesSkinned;

	Uint32 m_numChunksStatic;
	Uint32 m_numTrianglesStatic;
	Uint32 m_numVerticesStatic;

	Uint32 m_visibleObjects;
	Uint32 m_visibleVegetationSystemInstances;

	Uint32 m_numParticlesRendered;
	Uint32 m_numParticleEmittersRendered;

	static constexpr Uint32 NumMaterialModifiers = 27;
	red::FixedArray< Uint32, NumMaterialModifiers > m_materialModifierChanges;

	// - - - - - - - - - - - - - - - - - - - - - - - - - -

	RENDER_DATA_API GeometryBatcherStats();

	RENDER_DATA_API ~GeometryBatcherStats();

	// - - - - - - - - - - - - - - - - - - - - - - - - - -

	RENDER_DATA_API GeometryBatcherStats& Append( const GeometryBatcherStats& other );

	RENDER_DATA_API void AppendDrawCall( Uint8 vertexFactory, Uint32 numVertices, Uint32 numIndices, Uint32 fragCount, Uint32 instanceCount );

	// - - - - - - - - - - - - - - - - - - - - - - - - - -
};


//*********************************************************************************************************************************************************//
//*********************************************************************************************************************************************************//

struct VisibilityBVHStats
{
	//visibility bvh tree traversal stats
	Uint32 m_bvhMaxHeight{ 0 };
	Uint32 m_bvhMaxObjectsPerNode{ 0 };
	Uint32 m_bvhTotalObjectsProcessed{ 0 };
	Uint32 m_bvhExceptionalObjects{ 0 };
	Uint32 m_bvhNodesCount{ 0 };
	Uint32 m_bvhLeafsCount{ 0 };
	Uint32 m_bvhMaxStackDepth{ 0 };
	Uint32 m_bvhNumberOfThreadsDispatched{ 0 };
	Uint32 m_bvhNodesProcessedCount{ 0 };

	RENDER_DATA_API VisibilityBVHStats& Append( const VisibilityBVHStats& other );
};

struct VisibilityGridStats
{
	//visibility Grid traversal stats
	Uint32 m_totalGridCellsTraversed{ 0 };
	Uint32 m_totalGridCellsPassedVisibility{ 0 };
	Uint32 m_totalGridCellsWithOneElem{ 0 };

	Uint32 m_sceneGridCellsTraversed{ 0 };
	Uint32 m_sceneGridCellsPassedVisibility{ 0 };
	Uint32 m_sceneGridCellsWithOneElem{ 0 };

	Uint32 m_cascadesGridCellsTraversed{ 0 };
	Uint32 m_cascadesGridCellsPassedVisibility{ 0 };
	Uint32 m_cascadesGridCellsWithOneElem{ 0 };

	Uint32 m_distShadowsGridCellsTraversed{ 0 };
	Uint32 m_distShadowsGridCellsPassedVisibility{ 0 };
	Uint32 m_distShadowsGridCellsWithOneElem{ 0 };

	Uint32 m_localShadowsGridCellsTraversed{ 0 };
	Uint32 m_localShadowsGridCellsPassedVisibility{ 0 };
	Uint32 m_localShadowsGridCellsWithOneElem{ 0 };

	RENDER_DATA_API VisibilityGridStats& Append( const VisibilityGridStats& other );
};

//*********************************************************************************************************************************************************//
//*********************************************************************************************************************************************************//

struct VisibilitySystemStats
{
	//general visibility system stats
	Uint32	m_numObjectsTested{ 0 };
	Uint32	m_numObjectsVisible{ 0 };

	// multi-frustum test stats
	Uint32	m_multiFrustumNumTargetsTotal{ 0 };
	Uint32	m_multiFrustumNumTargetsPassedTodVis{ 0 };
	Uint32	m_multiFrustumNumTargetsTested{ 0 };
	Uint32	m_multiFrustumNumTargetsPassedOverallFrusumCulling{ 0 };
	Uint32	m_multiFrustumNumTargetsPassedFrusumCulling{ 0 };
	Uint32	m_multiFrustumNumTargetsPassedShadowOcclusion{ 0 };
	Uint32	m_multiFrustumNumTargetsVisible{ 0 };
	Uint32	m_multiFrustumNumFrustumsTested{ 0 };
	Uint32	m_multiFrustumNumFrustumsPassed{ 0 };

	// single-frustum test stats
	Uint32	m_singleFrustumNumTotalTargets{ 0 };
	Uint32	m_singleFrustumNumTestedTargets{ 0 };
	Uint32	m_singleFrustumNumPassedTargets{ 0 };

	//Specific passes test stats
	Uint32	m_mainSceneNumTestedTargets{ 0 };
	Uint32	m_mainSceneNumPassedTargets{ 0 };

	Uint32	m_cascadesNumTestedTargets{ 0 };
	Uint32	m_cascadesNumPassedTargets{ 0 };

	Uint32	m_localShadowsNumTestedTargets{ 0 };
	Uint32	m_localShadowsNumPassedTargets{ 0 };

	Uint32 m_numOccluderSectorsTested{ 0 };
	Uint32 m_numOccluderSectorsPassed{ 0 };
	Uint32 m_numOccludersTested{ 0 };
	Uint32 m_numOccludersPassed{ 0 };

	//stats specific to tests against Occlusion buffer
	Uint32 m_numOccBufVisibilityTests{ 0 };
	Uint32 m_numOccBufTilesTests{ 0 };
	Uint32 m_numOccBufEarlyOuts{ 0 };
	Uint32 m_numOccBufClippedTests{ 0 };
	Uint32 m_numOccBufPassedLateVisibilityTests{ 0 };
	Uint32 m_numOccBufTrisRasterized{ 0 };
	Uint32 m_numOccBufTrisScheduledBroadPhase{ 0 };
	Uint32 m_numOccBufTrisScheduledFillPhase{ 0 };
	Uint32 m_numOccBufTrisInTilesRasterized{ 0 };
	Uint32 m_numOccBufTriBatchesRasterized{ 0 };

	VisibilityGridStats m_visGridStats;
	VisibilityGridStats m_RTXVisGridStats;
#ifdef USE_STATIC_PROXIES_BVH_TREE
	VisibilityBVHStats m_staticProxiesBVHStats;
	VisibilityBVHStats m_staticProxiesBVHStatsForRTXCulling;
#endif

	// - - - - - - - - - - - - - - - - - - - - - - - - - -

	struct VisibleMeshKey
	{
		res::ResourcePath path;
		Uint32 chunkIndex = 0;

		RED_FORCE_INLINE bool operator == ( const VisibleMeshKey& rhs ) const
		{
			return path == rhs.path && chunkIndex == rhs.chunkIndex;
		}

		RED_FORCE_INLINE Uint32 CalcHash() const
		{
			return static_cast< Uint32 >( path.GetHash() ) ^ chunkIndex;
		}
	};

	//how many times each resource (mesh) is used
	red::HashMap< VisibleMeshKey, Uint32 > m_numResourcesUsed{ red::PoolDebug() };
	red::RWSpinLock m_numResourcesUsedLock;
    RENDER_DATA_API void CollectUsedResource( const res::ResourcePath& path, Uint32 chunkIndex, Uint32 numInstances );

	RENDER_DATA_API VisibilitySystemStats();
	RENDER_DATA_API ~VisibilitySystemStats();

	// - - - - - - - - - - - - - - - - - - - - - - - - - -

	RENDER_DATA_API VisibilitySystemStats& Append( const VisibilitySystemStats& other );
};

struct QueryOnlyBoxStats
{
    static constexpr Uint32 c_maxUsages = 16;
	Uint32 m_maxBoxes = 0;
    Uint32 m_numBoxes = 0;
    Uint32 m_numVisible = 0;
    Bool m_flagUsagesOverflow = false;
    struct Usage
    {
        CName name;
        Uint32 count;
    };
    red::StaticArray<Usage, c_maxUsages> m_usages;
};

struct LocalShadowsTrianglesCount
{
	Uint32 m_staticCached{ 0 };
	Uint32 m_static{ 0 };
	Uint32 m_dynamic{ 0 };
};

struct LocalShadowsStats
{
	RENDER_DATA_API LocalShadowsStats();
	RENDER_DATA_API ~LocalShadowsStats();

	RENDER_DATA_API Bool GetNumOfShadowTrianglesRendered( const IRenderProxy* rlp, LocalShadowsTrianglesCount & out ) const;

	RED_FORCE_INLINE void AppendNumOfStaticShadowTrianglesRendered( const IRenderProxyLight* rlp, Uint32 staticTrisCount )
	{
		m_trianglesRenderedToShadowmapsByProxyLight[ rlp ].m_static += staticTrisCount;
	}

	RED_FORCE_INLINE void AppendNumOfDynamicShadowTrianglesRendered( const IRenderProxyLight* rlp, Uint32 dynamicTrisCount )
	{
		m_trianglesRenderedToShadowmapsByProxyLight[ rlp ].m_dynamic += dynamicTrisCount;
	}

	RED_FORCE_INLINE void SetNumOfStaticShadowTrianglesCached( const IRenderProxyLight* rlp, Uint32 staticTrisCountCached )
	{
		m_trianglesRenderedToShadowmapsByProxyLight[ rlp ].m_staticCached = staticTrisCountCached;
	}

	RED_FORCE_INLINE void RemoveProxy( const IRenderProxyLight* rlp )
	{
		m_trianglesRenderedToShadowmapsByProxyLight.Remove( rlp );
	}

	RED_FORCE_INLINE Bool IsEmpty() const
	{
		return m_trianglesRenderedToShadowmapsByProxyLight.Empty();
	}


private:
	red::HashMap< const IRenderProxyLight*, LocalShadowsTrianglesCount > m_trianglesRenderedToShadowmapsByProxyLight { red::PoolDebug() };
};

//*********************************************************************************************************************************************************//
//*********************************************************************************************************************************************************//

struct DistantShadowsCascadeStats
{
	Float m_collectProxiesTime{ 0.0f };
	Float m_collectElementsTime{ 0.0f };
	Float m_renderTime{ 0.0f };
	Uint32 m_meshesRendered{ 0 };
	Uint32 m_danglingProxiesReleased{ 0 };
	Float m_releaseDanglingProxiesTotalTime{ 0.0f };
	Float m_releaseDanglingProxiesMaxTime{ 0.0f };

	RENDER_DATA_API DistantShadowsCascadeStats& Append( const DistantShadowsCascadeStats& other );
};

struct DistantShadowsStats
{
	static constexpr Uint32 MaxLevels = 4;

	red::FixedArray< DistantShadowsCascadeStats, MaxLevels > m_cascadeStats;

	RENDER_DATA_API DistantShadowsStats& Append( const DistantShadowsStats& other );
};

//*********************************************************************************************************************************************************//
//*********************************************************************************************************************************************************//

struct LightingStats
{
	// Light channels
	Uint32			m_numVisibleLightChannels{0};
	Uint32			m_maskVisibleLightChannels{0};

	// Light channels
	Uint32			m_numVisibleGIVolumes{ 0 };

	// Env probes
	Uint32			m_numVisibleEnvProbes{0};
	Uint32			m_numLoadedLocalEnvProbes{0};
	Uint32			m_numStreamedWorldEnvProbes{ 0 };
	Uint32			m_numStreamedComponentEnvProbes{ 0 };
	Uint32			m_numLoadedGlobalEnvProbes{0};
	Uint32			m_numPendingStreamingEnvProbes{0};
	Uint32			m_numErrorEnvProbes{0};
	Uint32			m_numLoadedCubes{0};

	// Fog volumes
	Uint32			m_numVisibleFogVolumes{0};
	Uint32			m_numVisibleFogParticles{0};

	// Lights
	Uint32			m_numPointLights{0};
	Uint32			m_numSpotLights{0};
	Uint32			m_numAreaLights{0};
	Uint32			m_numLightWithShadows{ 0 };
	Uint32			m_numOfLocalShadowsVisibilityQueries{0};


	// - - - - - - - - - - - - - - - - - - - - - - - - - -

	RENDER_DATA_API LightingStats& Append( const LightingStats& other );

};

//*********************************************************************************************************************************************************//
//*********************************************************************************************************************************************************//

struct RenderingStats
{
	static constexpr Uint32 NUM_CASCADES = 4;

	// last overall time of GPU work
	Float m_lastGpuFrameTime{ 0.0f };

	GeometryBatcherStats m_batcherStats;
	GeometryBatcherStats m_batcherStatsShadows;
	GeometryBatcherStats m_batcherStatsDistantShadows;
	GeometryBatcherStats m_batcherStatsRainMap;
	GeometryBatcherStats m_batcherStatsDebugFragments;

	// batcher stats for each cascade
	red::FixedArray< GeometryBatcherStats, NUM_CASCADES > m_batcherStatsCascades;

	VisibilitySystemStats m_visibilitySystemStats;

    QueryOnlyBoxStats m_queryOnlyBoxStats;

	// render proxy stats
	red::Atomic< Uint32 > m_collectedMeshProxies{ 0 };
	red::Atomic< Uint32 > m_collectedDecalProxies{ 0 };
	red::Atomic< Uint32 > m_collectedLightProxies{ 0 };
	red::Atomic< Uint32 > m_visibleClusteredInstances{ 0 };
	red::Atomic< Uint32 > m_visibleFoliageInstances{ 0 };

	// general shadows for shadow cascades
	red::Atomic< Uint32 > m_numCascadeProxiesTotal{ 0 };			//!< Total number of mesh proxies rendered into the shadow cascades
	Uint32 m_numCascadeChunksTotal{ 0 };							//!< Total number of mesh chunks rendered into the shadow cascades
	Uint32 m_numCascadeTrianglesTotal{ 0 };							//!< Total number of triangles rendered into the shadow cascades
	Uint32 m_numCascadeProxiesFrustumCulled{ 0 };					//!< Number of proxies that were culled because they are not in the frustum
	red::Atomic< Uint32 > m_numCascadeProxiesExlusionFiltered{ 0 };	//!< Number of proxies that were optimized because of visibility exclusion

	LocalShadowsStats	m_shadowCastingLightsStats;
	DistantShadowsStats	m_distantShadowsStats;
	
	LightingStats		m_lightingStats;

	// specialized shadows for dynamic shadows
	Uint32 m_numDynamicShadowsRegions{ 0 };							//!< Number of allocated and rendered dynamic shadowmap regions

	// particle stats
	Uint32 m_numActiveCpuParticles{ 0 };							//!< Number of cpu particles simulated
	Uint32 m_maxCpuParticlesAllocated{ 0 };							//!< Max particles allocated
	Float  m_particleVBFillRate{ 0.0f };							//!< % full of particle vertex buffer
	Uint32 m_dplFillRate { 0 };										//!< % full of DPL texture

	// - - - - - - - - - - - - - - - - - - - - - - - - - -

	RENDER_DATA_API RenderingStats();

	RENDER_DATA_API ~RenderingStats();

	// - - - - - - - - - - - - - - - - - - - - - - - - - -

	RENDER_DATA_API RenderingStats& operator = ( const RenderingStats& other );

	// - - - - - - - - - - - - - - - - - - - - - - - - - -

	RENDER_DATA_API void Reset();

	RENDER_DATA_API void Append( const RenderingStats& other );
};

//*********************************************************************************************************************************************************//
//*********************************************************************************************************************************************************//
