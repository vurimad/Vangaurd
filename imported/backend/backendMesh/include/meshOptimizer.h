/**
* Copyright (c) 2020 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "backendMeshApi.h"

namespace mesh
{
	void InitializeMeshOptimizerLibrary();

	// terrain stuff
	struct TerrainOptimizationParams
	{
		Float targetError = 5e-3f;
		Float reductionRation = 10.f; // percentage value. How many triangles should be in simplified mesh
        Uint32 nbImportantVertices = 0;
        Float maxEdgeLength = 10000.f;
		Bool simplify = true;
        Bool doConsistencyCheck = false;
		Bool optimizeVertexCache = true;
		Bool optimizeOverdraw = true;
		Bool optimizeVertexFetch = true;
	};

	struct TerrainOptimizationInput
	{
		red::ArraySpan<const Vector3> vertices;
		red::ArraySpan<const Uint32> indices;
        red::ArraySpan<const Float> vertexImportance;
	};

	struct TerrainOptimizationOutput
	{
		explicit TerrainOptimizationOutput( const red::memory::Pool& pool )
			: vertices( pool ), indices( pool )
		{}
		red::DynArray<Vector3> vertices;
		red::DynArray<Uint32> indices;
	};

	BACKEND_MESH_API void ApplyTerrainMeshOptimization( TerrainOptimizationOutput* out, const TerrainOptimizationInput& in, const TerrainOptimizationParams& params );
	BACKEND_MESH_API void ApplyVertexCacheOptimization( red::ArraySpan< Uint16 > indices, Uint32 numVertices );
	BACKEND_MESH_API void ApplyVertexCacheOptimization( red::ArraySpan< Uint32 > indices, Uint32 numVertices );


    BACKEND_MESH_API Uint32 SimplifyGeometryForMinimapGenerator( red::ArraySpan<Uint32> lodIndices, red::ArraySpan<const Vector3> positions, red::ArraySpan<const Uint32> indices, Uint32 targetIndicesCount, Float targetError );
}
