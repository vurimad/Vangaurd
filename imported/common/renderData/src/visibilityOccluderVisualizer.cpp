/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "visibilityOccluderVisualizer.h"
#include "visibilityOccluderMeshResource.h"
#include "renderVertices.h"
#include "renderer.h"


namespace vis
{

	OccluderVisualizer::OccluderVisualizer()
	{
	}

	OccluderVisualizer::~OccluderVisualizer()
	{
	}


	void OccluderVisualizer::SetResource( const THandle<IOccluderResource>& resource )
	{
		m_resource = resource;
		m_solidMesh.Reset();
		m_wireMesh.Reset();
	}

	const TRenderPtr<IRenderResource>& OccluderVisualizer::GetSolidMesh()
	{
		if ( m_solidMesh == nullptr )
		{
			BuildMeshes();
		}
		return m_solidMesh;
	}

	const TRenderPtr<IRenderResource>& OccluderVisualizer::GetWireMesh()
	{
		if ( m_wireMesh == nullptr )
		{
			BuildMeshes();
		}
		return m_wireMesh;
	}


	void OccluderVisualizer::BuildMeshes()
	{
		if ( m_resource == nullptr )
		{
			return;
		}

		if ( auto mesh = red::StaticCast<OccluderMeshResource>( m_resource ) )
		{
			const auto& srcVertices = mesh->GetVertices();
			const Uint32 numVertices = srcVertices.Size();
			red::DynArray<DebugVertex> vertices( numVertices, red::PoolDebug() );
			for ( Uint32 i = 0; i < numVertices; ++i )
			{
				vertices[ i ].Set( srcVertices[ i ], math::Color::WHITE() );
			}

			const auto& srcIndices = mesh->GetIndices();
			const Uint32 numIndices = srcIndices.Size();
			red::DynArray<Uint32> solidIndices( numIndices, red::PoolDebug() );
			for ( Uint32 i = 0; i < numIndices; ++i )
			{
				solidIndices[ i ] = srcIndices[ i ];
			}
			if ( mesh->GetTwoSided() )
			{
				// Push again, in reverse order
				solidIndices.Reserve( solidIndices.Size() + numIndices );
				for ( Uint32 i = 0; i < numIndices; i += 3 )
				{
					solidIndices.PushBack( srcIndices[ i + 0 ] );
					solidIndices.PushBack( srcIndices[ i + 2 ] );
					solidIndices.PushBack( srcIndices[ i + 1 ] );
				}
			}
			m_solidMesh = GRender->UploadDebugMesh( vertices, solidIndices );

			red::DynArray<Uint32> wireIndices{ red::PoolDebug() };
			red::HashSet<Uint32> addedEdges{ red::PoolDebug() };
			for ( Uint32 triIdx = 0; triIdx < numIndices / 3; ++triIdx )
			{
				for ( Uint32 i = 0; i < 3; ++i )
				{
					Uint16 i0 = srcIndices[ triIdx * 3 + i ];
					Uint16 i1 = srcIndices[ triIdx * 3 + ( ( i + 1 ) % 3 ) ];

					// Don't add duplicate edges, makes things a bit cleaner.
					if ( i0 > i1 )
					{
						Swap( i0, i1 );
					}
					Uint32 edgeKey = ( i0 << 16 ) | i1;
					if ( addedEdges.Insert( edgeKey ).IsSuccessful() )
					{
						wireIndices.PushBack( i0 );
						wireIndices.PushBack( i1 );
					}
				}
			}
			m_wireMesh = GRender->UploadDebugMesh( vertices, wireIndices );
		}
		else
		{
			RED_FATAL( "Unknown occluder type, cannot generate visualization" );
			m_solidMesh.Reset();
			m_wireMesh.Reset();
		}
	}

}
