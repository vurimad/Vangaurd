/**
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "renderStats.h"
#include "materialStructures.h"
#include "materialModifier.h"

static_assert( GeometryBatcherStats::NumMaterialModifiers == EMATMOD_MAX, "Number of material modifiers changed" );

//*********************************************************************************************************************************************************//
//*********************************************************************************************************************************************************//

GeometryBatcherStats::GeometryBatcherStats()
{
	red::Memset( this, 0, sizeof( GeometryBatcherStats ) );
	m_smallestInstancedBatch = UINT32_MAX;
}

GeometryBatcherStats::~GeometryBatcherStats()
{

}

GeometryBatcherStats& GeometryBatcherStats::Append( const GeometryBatcherStats& other )
{
#ifndef RED_CONFIGURATION_FINAL
	m_numDrawCalls						+= other.m_numDrawCalls;
	m_numMeshProxies					+= other.m_numMeshProxies;

	m_numInstances						+= other.m_numInstances;
	m_numInstancedBatches 				+= other.m_numInstancedBatches;
	m_biggestInstancedBatch 			= Max( m_biggestInstancedBatch, other.m_biggestInstancedBatch );
	m_smallestInstancedBatch 			= Min( m_smallestInstancedBatch, other.m_smallestInstancedBatch );

	m_numStaticInstanceBufferBinding	+= other.m_numStaticInstanceBufferBinding;
	m_numDynamicInstanceBufferBinding	+= other.m_numDynamicInstanceBufferBinding;

	m_numChunks 						+= other.m_numChunks;
	m_numTriangles						+= other.m_numTriangles;
	m_numVertices						+= other.m_numVertices;

	m_numChunksStatic					+= other.m_numChunksStatic;
	m_numTrianglesStatic				+= other.m_numTrianglesStatic;
	m_numVerticesStatic					+= other.m_numVerticesStatic;

	m_numChunksSkinned					+= other.m_numChunksSkinned;
	m_numTrianglesSkinned				+= other.m_numTrianglesSkinned;
	m_numVerticesSkinned				+= other.m_numVerticesSkinned;

	m_visibleObjects					+= other.m_visibleObjects;
	m_visibleVegetationSystemInstances	+= other.m_visibleVegetationSystemInstances;

	m_numParticlesRendered				+= other.m_numParticlesRendered;
	m_numParticleEmittersRendered		+= other.m_numParticleEmittersRendered;

	for ( Uint32 i = 0; i < NumMaterialModifiers; ++i )
	{
		m_materialModifierChanges[ i ] += other.m_materialModifierChanges[ i ];
	}

#endif // !RED_CONFIGURATION_FINAL

	return *this;
}

//*********************************************************************************************************************************************************//
//*********************************************************************************************************************************************************//

void GeometryBatcherStats::AppendDrawCall( Uint8 vertexFactory, Uint32 numVertices, Uint32 numIndices, Uint32 fragCount, Uint32 instanceCount )
{
#ifndef RED_CONFIGURATION_FINAL

	const Uint32 totalNumTriangles = instanceCount * numIndices / 3 * fragCount;
	const Uint32 totalNumVertices = instanceCount * numVertices;

	if ( ( vertexFactory == MVF_MeshStatic ) || ( vertexFactory == MVF_MeshStaticVehicle ) || ( vertexFactory == MVF_MeshDestructible ) )
	{
		m_numChunksStatic += instanceCount;
		m_numTrianglesStatic += totalNumTriangles;
		m_numVerticesStatic += totalNumVertices;
	}
	if ( ( vertexFactory == MVF_MeshSkinned ) || ( vertexFactory == MVF_GarmentMeshSkinned ) || ( vertexFactory == MVF_MeshSkinnedVehicle ) || ( vertexFactory == MVF_MeshExtSkinned ) || ( vertexFactory == MVF_GarmentMeshExtSkinned ) )
	{
		m_numChunksSkinned += instanceCount;
		m_numTrianglesSkinned += totalNumTriangles;
		m_numVerticesSkinned += totalNumVertices;
	}

	m_numDrawCalls += 1;
	m_numChunks += instanceCount;
	m_numTriangles += totalNumTriangles;
	m_numVertices += totalNumVertices;

	m_numInstances += instanceCount;
	m_numInstancedBatches += instanceCount > 1 ? 1 : 0;
	m_biggestInstancedBatch = Max( m_biggestInstancedBatch, instanceCount );
	m_smallestInstancedBatch = Min( m_smallestInstancedBatch, instanceCount );

#endif // !RED_CONFIGURATION_FINAL
}

//*********************************************************************************************************************************************************//
//*********************************************************************************************************************************************************//

VisibilityBVHStats& VisibilityBVHStats::Append( const VisibilityBVHStats& other )
{
#ifndef RED_CONFIGURATION_FINAL

	m_bvhMaxHeight = Max( other.m_bvhMaxHeight, m_bvhMaxHeight );
	m_bvhMaxObjectsPerNode = Max( m_bvhMaxObjectsPerNode, other.m_bvhMaxObjectsPerNode );
	m_bvhTotalObjectsProcessed += other.m_bvhTotalObjectsProcessed;
	m_bvhExceptionalObjects = Max( m_bvhExceptionalObjects, other.m_bvhExceptionalObjects );
	m_bvhLeafsCount += other.m_bvhLeafsCount;
	m_bvhNodesCount += other.m_bvhNodesCount;
	m_bvhMaxStackDepth = Max( other.m_bvhMaxStackDepth, m_bvhMaxStackDepth );
	m_bvhNumberOfThreadsDispatched += other.m_bvhNumberOfThreadsDispatched;
	m_bvhNodesProcessedCount += other.m_bvhNodesProcessedCount;

#endif // !RED_CONFIGURATION_FINAL

	return *this;
}

static const res::ResourcePath g_resourceWithoutPath;
void VisibilitySystemStats::CollectUsedResource( const res::ResourcePath& path, Uint32 chunkIndex, Uint32 numInstances )
{
	res::ResourcePath pathToUse = path;
	if( !path.IsValid() )
		pathToUse = g_resourceWithoutPath;

	VisibleMeshKey key{ pathToUse, chunkIndex };
	{
		RED_SCOPE_LOCK( m_numResourcesUsedLock );
		m_numResourcesUsed[key] += numInstances;
	}
}

//*********************************************************************************************************************************************************//
//*********************************************************************************************************************************************************//

VisibilitySystemStats::VisibilitySystemStats()
{

}

VisibilitySystemStats::~VisibilitySystemStats()
{

}

VisibilitySystemStats& VisibilitySystemStats::Append( const VisibilitySystemStats& other )
{
#ifdef USE_PROFILER
	m_numOccBufTrisRasterized += other.m_numOccBufTrisRasterized;
	m_numOccBufTrisScheduledBroadPhase += other.m_numOccBufTrisScheduledBroadPhase;
	m_numOccBufTrisScheduledFillPhase += other.m_numOccBufTrisScheduledFillPhase;
#endif

#ifndef RED_CONFIGURATION_FINAL

	m_numObjectsTested						+= other.m_numObjectsTested;
	m_numObjectsVisible						+= other.m_numObjectsVisible;
	
	m_multiFrustumNumTargetsTotal						+= other.m_multiFrustumNumTargetsTotal;
	m_multiFrustumNumTargetsPassedTodVis				+= other.m_multiFrustumNumTargetsPassedTodVis;
	m_multiFrustumNumTargetsTested						+= other.m_multiFrustumNumTargetsTested;
	m_multiFrustumNumTargetsPassedOverallFrusumCulling	+= other.m_multiFrustumNumTargetsPassedOverallFrusumCulling;
	m_multiFrustumNumTargetsPassedFrusumCulling			+= other.m_multiFrustumNumTargetsPassedFrusumCulling;
	m_multiFrustumNumTargetsPassedShadowOcclusion		+= other.m_multiFrustumNumTargetsPassedShadowOcclusion;
	m_multiFrustumNumTargetsVisible						+= other.m_multiFrustumNumTargetsVisible;
	m_multiFrustumNumFrustumsTested						+= other.m_multiFrustumNumFrustumsTested;
	m_multiFrustumNumFrustumsPassed						+= other.m_multiFrustumNumFrustumsPassed;

	m_singleFrustumNumTotalTargets			+= other.m_singleFrustumNumTotalTargets;
	m_singleFrustumNumTestedTargets			+= other.m_singleFrustumNumTestedTargets;
	m_singleFrustumNumPassedTargets			+= other.m_singleFrustumNumPassedTargets;

	m_mainSceneNumTestedTargets					+= other.m_mainSceneNumTestedTargets;
	m_mainSceneNumPassedTargets					+= other.m_mainSceneNumPassedTargets;

	m_cascadesNumTestedTargets				+= other.m_cascadesNumTestedTargets;
	m_cascadesNumPassedTargets				+= other.m_cascadesNumPassedTargets;

	m_localShadowsNumTestedTargets			+= other.m_localShadowsNumTestedTargets;
	m_localShadowsNumPassedTargets			+= other.m_localShadowsNumPassedTargets;

	m_numOccluderSectorsTested				+= other.m_numOccluderSectorsTested;
	m_numOccluderSectorsPassed				+= other.m_numOccluderSectorsPassed;
	m_numOccludersTested					+= other.m_numOccludersTested;
	m_numOccludersPassed					+= other.m_numOccludersPassed;

	m_numOccBufVisibilityTests				+= other.m_numOccBufVisibilityTests;
	m_numOccBufTilesTests					+= other.m_numOccBufTilesTests;
	m_numOccBufEarlyOuts					+= other.m_numOccBufEarlyOuts;
	m_numOccBufClippedTests					+= other.m_numOccBufClippedTests;
	m_numOccBufPassedLateVisibilityTests	+= other.m_numOccBufPassedLateVisibilityTests;
	m_numOccBufTrisInTilesRasterized		+= other.m_numOccBufTrisInTilesRasterized;
	m_numOccBufTriBatchesRasterized			+= other.m_numOccBufTriBatchesRasterized;

#ifdef USE_STATIC_PROXIES_BVH_TREE
	m_staticProxiesBVHStats.Append( other.m_staticProxiesBVHStats );
	m_staticProxiesBVHStatsForRTXCulling.Append( other.m_staticProxiesBVHStatsForRTXCulling );
#else
	m_visGridStats.Append( other.m_visGridStats );
	m_RTXVisGridStats.Append( other.m_RTXVisGridStats );
#endif

	for ( const auto& iter : other.m_numResourcesUsed )
	{
		m_numResourcesUsed[ iter.Key() ] += iter.Value();
	}

#endif // !RED_CONFIGURATION_FINAL

	return *this;
}

//*********************************************************************************************************************************************************//
//*********************************************************************************************************************************************************//

LightingStats& LightingStats::Append( const LightingStats& other )
{
	m_numVisibleLightChannels		+= other.m_numVisibleLightChannels;
	m_maskVisibleLightChannels		|= other.m_maskVisibleLightChannels;
	m_numVisibleGIVolumes			+= other.m_numVisibleGIVolumes;
	m_numVisibleEnvProbes			+= other.m_numVisibleEnvProbes;
	m_numLoadedLocalEnvProbes		+= other.m_numLoadedLocalEnvProbes;
	m_numLoadedGlobalEnvProbes		+= other.m_numLoadedGlobalEnvProbes;
	m_numStreamedWorldEnvProbes		+= other.m_numStreamedWorldEnvProbes;
	m_numStreamedComponentEnvProbes	+= other.m_numStreamedComponentEnvProbes;
	m_numPendingStreamingEnvProbes	+= other.m_numPendingStreamingEnvProbes;
	m_numErrorEnvProbes				+= other.m_numErrorEnvProbes;
	m_numLoadedCubes				= Max( m_numLoadedCubes, other.m_numLoadedCubes );
	m_numVisibleFogVolumes			+= other.m_numVisibleFogVolumes;
	m_numVisibleFogParticles		+= other.m_numVisibleFogParticles;

	m_numPointLights				+= other.m_numPointLights			;	
	m_numSpotLights					+= other.m_numSpotLights			;		
	m_numAreaLights					+= other.m_numAreaLights			;	
	m_numLightWithShadows			+= other.m_numLightWithShadows		;	
	m_numOfLocalShadowsVisibilityQueries += other.m_numOfLocalShadowsVisibilityQueries;

	return *this;
}

//*********************************************************************************************************************************************************//
//*********************************************************************************************************************************************************//

RenderingStats& RenderingStats::operator = ( const RenderingStats& other )
{
	m_lastGpuFrameTime = other.m_lastGpuFrameTime;

	m_batcherStats = other.m_batcherStats;
	m_batcherStatsShadows = other.m_batcherStatsShadows;
	m_batcherStatsDistantShadows = other.m_batcherStatsDistantShadows;
	m_batcherStatsRainMap = other.m_batcherStatsRainMap;
	m_batcherStatsDebugFragments = other.m_batcherStatsDebugFragments;

	m_distantShadowsStats = other.m_distantShadowsStats;

	m_visibilitySystemStats = other.m_visibilitySystemStats;

	m_lightingStats = other.m_lightingStats;

	m_collectedMeshProxies.Exchange( other.m_collectedMeshProxies.GetValue() );
	m_collectedDecalProxies.Exchange( other.m_collectedDecalProxies.GetValue() );
	m_collectedLightProxies.Exchange( other.m_collectedLightProxies.GetValue() );
	m_visibleClusteredInstances.Exchange( other.m_visibleClusteredInstances.GetValue() );
	m_visibleFoliageInstances.Exchange( other.m_visibleFoliageInstances.GetValue() );

	for ( Uint32 i = 0; i < NUM_CASCADES; ++i )
	{
		m_batcherStatsCascades[ i ] = other.m_batcherStatsCascades[ i ];
	}

	m_numCascadeProxiesTotal.Exchange( other.m_numCascadeProxiesTotal.GetValue() );
	m_numCascadeChunksTotal = other.m_numCascadeChunksTotal;
	m_numCascadeTrianglesTotal = other.m_numCascadeTrianglesTotal;
	m_numCascadeProxiesFrustumCulled = other.m_numCascadeProxiesFrustumCulled;
	m_numCascadeProxiesExlusionFiltered.Exchange( other.m_numCascadeProxiesExlusionFiltered.GetValue() );

	m_numDynamicShadowsRegions = other.m_numDynamicShadowsRegions;

	m_numActiveCpuParticles = other.m_numActiveCpuParticles;
	m_maxCpuParticlesAllocated = other.m_maxCpuParticlesAllocated;
	m_particleVBFillRate = other.m_particleVBFillRate;
	m_dplFillRate = other.m_dplFillRate;

	m_shadowCastingLightsStats = other.m_shadowCastingLightsStats;

    m_queryOnlyBoxStats = other.m_queryOnlyBoxStats;

	return *this;
}

RenderingStats::RenderingStats()
{

}

RenderingStats::~RenderingStats()
{

}

void RenderingStats::Reset()
{
	*this = RenderingStats();
}

void RenderingStats::Append( const RenderingStats& other )
{
	m_lastGpuFrameTime += other.m_lastGpuFrameTime;

	m_batcherStats.Append( other.m_batcherStats );
	m_batcherStatsShadows.Append( other.m_batcherStatsShadows );
	m_batcherStatsDistantShadows.Append( other.m_batcherStatsDistantShadows );
	m_batcherStatsRainMap.Append( other.m_batcherStatsRainMap );
	m_batcherStatsDebugFragments.Append( other.m_batcherStatsDebugFragments );
	for ( Uint32 i = 0; i < NUM_CASCADES; ++i )
	{
		m_batcherStatsCascades[ i ].Append( other.m_batcherStatsCascades[ i ] );
	}
	m_distantShadowsStats.Append( other.m_distantShadowsStats );
	m_lightingStats.Append( other.m_lightingStats );

	m_visibilitySystemStats.Append( other.m_visibilitySystemStats );

	m_collectedMeshProxies.ExchangeAdd( other.m_collectedMeshProxies.GetValue() );
	m_collectedDecalProxies.ExchangeAdd( other.m_collectedDecalProxies.GetValue() );
	m_collectedLightProxies.ExchangeAdd( other.m_collectedLightProxies.GetValue() );
	m_visibleClusteredInstances.ExchangeAdd( other.m_visibleClusteredInstances.GetValue() );
	m_visibleFoliageInstances.ExchangeAdd( other.m_visibleFoliageInstances.GetValue() );

	m_numCascadeProxiesTotal.ExchangeAdd( other.m_numCascadeProxiesTotal.GetValue() );
	m_numCascadeChunksTotal += other.m_numCascadeChunksTotal;
	m_numCascadeTrianglesTotal += other.m_numCascadeTrianglesTotal;
	m_numCascadeProxiesFrustumCulled += other.m_numCascadeProxiesFrustumCulled;
	m_numCascadeProxiesExlusionFiltered.ExchangeAdd( other.m_numCascadeProxiesExlusionFiltered.GetValue() );

	m_numDynamicShadowsRegions += other.m_numDynamicShadowsRegions;

	m_numActiveCpuParticles += other.m_numActiveCpuParticles;
	m_maxCpuParticlesAllocated += other.m_maxCpuParticlesAllocated;
	m_particleVBFillRate += other.m_particleVBFillRate;
	m_dplFillRate += other.m_dplFillRate;

	// Hacky - copy only if there is something to copy. This bypasses situation when we append partial
	// stats from particles and it assigns an empty m_shadowCastingLightsStats and we loose stuff.
	// Probably could do some insertion from one to another.
	if ( !other.m_shadowCastingLightsStats.IsEmpty() )
	{
		m_shadowCastingLightsStats = other.m_shadowCastingLightsStats;
	}
}


//*********************************************************************************************************************************************************//
//*********************************************************************************************************************************************************//

LocalShadowsStats::LocalShadowsStats()
{

}

LocalShadowsStats::~LocalShadowsStats()
{

}

Bool LocalShadowsStats::GetNumOfShadowTrianglesRendered( const IRenderProxy* rlp, LocalShadowsTrianglesCount & out ) const
{
	const IRenderProxyLight* proxyLight = ( const IRenderProxyLight* ) ( rlp );
	auto entry = m_trianglesRenderedToShadowmapsByProxyLight.Find( proxyLight );
	if ( entry != m_trianglesRenderedToShadowmapsByProxyLight.End() )
	{
		out = entry.Value();
		return true;
	}

	return false;
}

//*********************************************************************************************************************************************************//
//*********************************************************************************************************************************************************//

static_assert( DistantShadowsStats::MaxLevels == MAX_DISTANT_SHADOWS_LEVELS, "Max distant shadow levels do not match" );

DistantShadowsCascadeStats& DistantShadowsCascadeStats::Append( const DistantShadowsCascadeStats& other )
{
	m_collectProxiesTime += other.m_collectProxiesTime;
	m_collectElementsTime += other.m_collectElementsTime;
	m_renderTime += other.m_renderTime;
	m_meshesRendered += other.m_meshesRendered;
	m_danglingProxiesReleased += other.m_danglingProxiesReleased;
	m_releaseDanglingProxiesTotalTime += other.m_releaseDanglingProxiesTotalTime;
	m_releaseDanglingProxiesMaxTime += other.m_releaseDanglingProxiesMaxTime;

	return *this;
}

DistantShadowsStats& DistantShadowsStats::Append( const DistantShadowsStats& other )
{
	for ( Uint32 i = 0; i < MAX_DISTANT_SHADOWS_LEVELS; ++i )
	{
		m_cascadeStats[ i ].Append( other.m_cascadeStats[ i ] );
	}

	return *this;
}

RENDER_DATA_API VisibilityGridStats& VisibilityGridStats::Append( const VisibilityGridStats& other )
{
#ifndef RED_CONFIGURATION_FINAL
	this->m_totalGridCellsTraversed += other.m_totalGridCellsTraversed;
	this->m_totalGridCellsPassedVisibility += other.m_totalGridCellsPassedVisibility;
	this->m_totalGridCellsWithOneElem += other.m_totalGridCellsWithOneElem;

	this->m_sceneGridCellsTraversed += other.m_sceneGridCellsTraversed;
	this->m_sceneGridCellsPassedVisibility += other.m_sceneGridCellsPassedVisibility;
	this->m_sceneGridCellsWithOneElem += other.m_sceneGridCellsWithOneElem;

	this->m_cascadesGridCellsTraversed += other.m_cascadesGridCellsTraversed;
	this->m_cascadesGridCellsPassedVisibility += other.m_cascadesGridCellsPassedVisibility;
	this->m_cascadesGridCellsWithOneElem += other.m_cascadesGridCellsWithOneElem;

	this->m_distShadowsGridCellsTraversed += other.m_distShadowsGridCellsTraversed;
	this->m_distShadowsGridCellsPassedVisibility += other.m_distShadowsGridCellsPassedVisibility;
	this->m_distShadowsGridCellsWithOneElem += other.m_distShadowsGridCellsWithOneElem;
	
	this->m_localShadowsGridCellsTraversed += other.m_localShadowsGridCellsTraversed;
	this->m_localShadowsGridCellsPassedVisibility += other.m_localShadowsGridCellsPassedVisibility;
	this->m_localShadowsGridCellsWithOneElem += other.m_localShadowsGridCellsWithOneElem;
#endif
	return *this;
}
