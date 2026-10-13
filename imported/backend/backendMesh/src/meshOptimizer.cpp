/**
* Copyright (c) 2020 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "meshOptimizer.h"
#include "../../../common/renderData/include/renderMeshBlob.h"

#if defined( VANGUARD_ASSET_HOST )
#define assert( expr ) RED_FATAL_ASSERT( expr )
#endif

namespace ext
{
	#include "../../../external/meshoptimizer/src/meshoptimizer.h"
	#include "../../../external/meshoptimizer/src/allocator.cpp"
	#include "../../../external/meshoptimizer/src/indexgenerator.cpp"

	#include "../../../external/meshoptimizer/src/vcacheanalyzer.cpp"
	#include "../../../external/meshoptimizer/src/vcacheoptimizer.cpp"

	#include "../../../external/meshoptimizer/src/vfetchanalyzer.cpp"
	#include "../../../external/meshoptimizer/src/vfetchoptimizer.cpp"

	#include "../../../external/meshoptimizer/src/overdrawoptimizer.cpp"
	#include "../../../external/meshoptimizer/src/overdrawanalyzer.cpp"

    // auszakow: I've made some modifications in meshopt_simplify.
    // So, contact me if you need to use the function
	    #include "../../../external/meshoptimizer/src/simplifier.cpp"
}

#if defined( VANGUARD_ASSET_HOST )
#undef assert
#endif

//#pragma optimize("",off)



namespace mesh
{
	void* AllocateHook( size_t size )
	{
		return RED_ALLOCATE_ALIGNED( red::PoolBackend, size, 16 );
	}

	void DeallocateHook( void * block )
	{
		RED_FREE( red::PoolBackend, block );
	}

	class CMeshBlobIndexProcess : public rend::IMeshBlobIndexProcess
	{

	public:
		virtual void ProcessIndices( red::ArraySpan<Uint16> indices, Uint32 numVertices ) override
		{
			ApplyVertexCacheOptimization( indices, numVertices );
		}

		virtual void ProcessIndices( red::ArraySpan<Uint32> indices, Uint32 numVertices ) override
		{
			ApplyVertexCacheOptimization( indices, numVertices );
		}
	};

	void InitializeMeshOptimizerLibrary()
	{
		ext::meshopt_setAllocator( AllocateHook, DeallocateHook );

		rend::RenderMeshBlob::RegisterIndexProcessor( red::UniquePtr< CMeshBlobIndexProcess >::Create() );
	}

    static void RemoveInvertedTriangles( red::DynArray<Uint32>& indices, red::ArraySpan< const Vector3 > positions )
    {
        RED_FATAL_ASSERT( (indices.Size() % 3) == 0 );
        for( Uint32 i = 0; i < indices.Size(); )
        {
            const Uint32 i0 = indices[i + 0];
            const Uint32 i1 = indices[i + 1];
            const Uint32 i2 = indices[i + 2];

            const Vector3& p0 = positions[i0];
            const Vector3& p1 = positions[i1];
            const Vector3& p2 = positions[i2];

            const Vector3 v01 = p1 - p0;
            const Vector3 v02 = p2 - p0;
            const Vector3 n = v02.Cross( v01 ).Normalized();

            const Float d = n.Dot( Vector3::EZ() );
            if( d < 0.f )
            {
                indices.RemoveAt( i, i + 3 );
            }
            else
            {
                i += 3;
            }
        }
    }

	void ApplyTerrainMeshOptimization( TerrainOptimizationOutput* out, const TerrainOptimizationInput& in, const TerrainOptimizationParams& params )
	{
		const size_t vertexStride = sizeof( in.vertices[0] );
		const Float kOptOverdrawThreshold = 1.01f;

		// simplify
		if( params.simplify )
		{ 
			const Uint32 numIndices = in.indices.Size();

			red::DynArray<Vector3>& lodVerts = out->vertices;
			red::DynArray<Uint32>& lodIndices = out->indices;
			

			const Float* vertex_importance = nullptr;
            if( in.vertexImportance.Size() )
            {
                RED_FATAL_ASSERT( in.vertexImportance.Size() == in.vertices.Size() );
                vertex_importance = &in.vertexImportance[0];
            }
			
            const auto& inIndices = in.indices;
            const auto& inVertices = in.vertices;
            const Uint32 numInputIndices = inIndices.Size();

            const size_t targetIndexCount = Max( size_t( 128 ), size_t( (numInputIndices + params.nbImportantVertices * 3) * (params.reductionRation * 0.01f) ) );
            const Float targetError = params.targetError;

			lodIndices.Resize( numInputIndices );
			const Uint32 numLODindices = (Uint32)ext::meshopt_simplify( &lodIndices[0], &inIndices[0], numInputIndices, (Float*)&inVertices[0], inVertices.Size(), vertexStride, targetIndexCount, targetError, vertex_importance, params.maxEdgeLength, params.doConsistencyCheck );
			lodIndices.Resize( numLODindices );
            {
                ext::meshopt_optimizeVertexCache( &lodIndices[0], &lodIndices[0], lodIndices.Size(), inVertices.Size() );
                ext::meshopt_optimizeOverdraw( &lodIndices[0], &lodIndices[0], lodIndices.Size(), &inVertices[0].X, inVertices.Size(), vertexStride, kOptOverdrawThreshold );

                //RemoveInvertedTriangles( lodIndices, inVertices );

                lodVerts.Clear();
                lodVerts.PushBack( inVertices );
                const size_t numUsedVertices = ext::meshopt_optimizeVertexFetch( &lodVerts[0], &lodIndices[0], lodIndices.Size(), &inVertices[0], inVertices.Size(), vertexStride );
                lodVerts.Resize( (Uint32)numUsedVertices );

#if 0 // for tests and debugging
                lodVerts.Reserve( lodIndices.Size() );
                red::DynArray<Uint32> tmpIndices{ red::PoolBackend() };
                tmpIndices.Reserve( lodIndices.Size() );
                for( Uint32 i = 0; i < lodIndices.Size(); ++i )
                {
                    lodVerts.PushBack( in.vertices[lodIndices[i]] );
                    tmpIndices.PushBack( i );
                }
                lodIndices = std::move( tmpIndices );
#endif
            }
			
		}
		else
		{
			out->vertices.PushBack( in.vertices );
			out->indices.PushBack( in.indices );

			red::ArraySpan<Uint32> indices = out->indices;
			red::ArraySpan<Vector3> vertices = out->vertices;

			ext::meshopt_optimizeVertexCache( &indices[0], &indices[0], indices.Size(), in.vertices.Size() );
			ext::meshopt_optimizeOverdraw( &indices[0], &indices[0], indices.Size(), &in.vertices[0].X, in.vertices.Size(), vertexStride, kOptOverdrawThreshold );

			const size_t nbUsedVertices = ext::meshopt_optimizeVertexFetch( &vertices[0], &indices[0], indices.Size(), &in.vertices[0], in.vertices.Size(), vertexStride );
			out->vertices.Resize( (Uint32)nbUsedVertices );
		}
	}

	template <typename T>
	static void ApplyVertexCacheOptimizationImpl( T* indices, Uint32 numIndices, Uint32 numVertices, Uint32 baseVertexOffset )
	{
		RED_FATAL_ASSERT( indices );
		RED_FATAL_ASSERT( numIndices && numIndices % 3 == 0, "Must be non empty multiple of 3" );

		for( Uint32 i = 0; i < numIndices; ++i )
		{
			RED_WARNING( indices[i] >= baseVertexOffset && indices[i] < numVertices, "%d / %u", (Uint32)indices[i], numVertices );

			if( indices[i] >= numVertices )
				return;
		}

		ext::meshopt_optimizeVertexCache( indices, indices, numIndices, numVertices );
	}

	void ApplyVertexCacheOptimization( red::ArraySpan< Uint16 > indices, Uint32 numVertices )
	{
		if( !indices.Empty() )
		{
			ApplyVertexCacheOptimizationImpl<Uint16>( &indices[0], indices.Size(), numVertices, 0 );
		}
	}

	void ApplyVertexCacheOptimization( red::ArraySpan< Uint32 > indices, Uint32 numVertices )
	{
		if( !indices.Empty() )
		{
			ApplyVertexCacheOptimizationImpl<Uint32>( &indices[0], indices.Size(), numVertices, 0 );
		}
	}

    Uint32 SimplifyGeometryForMinimapGenerator( red::ArraySpan<Uint32> lodIndices, red::ArraySpan<const Vector3> positions, red::ArraySpan<const Uint32> indices, Uint32 targetIndicesCount, Float targetError )
    {
        RED_FATAL_ASSERT( lodIndices.Size() >= indices.Size() );
        return (Uint32)ext::meshopt_simplify( &lodIndices[0], &indices[0], indices.Size(), (Float*)&positions[0], positions.Size(), sizeof( positions[0] ), targetIndicesCount, targetError, nullptr, FLT_MAX, false );
    }

}

//#pragma optimize("",on)
