/**
* Copyright (c) 2007-2016 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "meshOccluderCompilationSource.h"


namespace mesh
{

	VisOccluderMeshCompilationSource::VisOccluderMeshCompilationSource( const CMesh& mesh, Uint32 lod )
		: m_mesh( &mesh )
		, m_meshView( mesh )
	{
		mesh::utils::GetLODChunks( m_meshView, lod, m_chunkIndices );
	}

	String VisOccluderMeshCompilationSource::GetMeshName() const
	{
		return m_mesh->GetFriendlyName();
	}

	Uint32 VisOccluderMeshCompilationSource::GetNumChunks() const
	{
		return m_chunkIndices.Size();
	}

	auto VisOccluderMeshCompilationSource::GetChunkData( Uint32 chunkIndex ) const -> ChunkData
	{
		ChunkData chunkData;

		const Uint32 chunkIndexInMesh = m_chunkIndices[chunkIndex];

		chunkData.m_vertices.Reserve( m_meshView.GetChunkVertexCount( chunkIndexInMesh ) );
		chunkData.m_indices.Reserve( m_meshView.GetChunkIndexCount( chunkIndexInMesh ) );

		const auto positionStream = m_meshView.GetChunkStreamData( chunkIndexInMesh, MST_Position_3F );
		for ( mesh::StreamIteratorRO<Vector3> it( positionStream ); it; ++it )
		{
			chunkData.m_vertices.PushBack( it.Get() );
		}

		const auto indexStream = m_meshView.GetChunkStreamData( chunkIndexInMesh, MST_Index_U16 );
		for ( mesh::StreamIteratorRO<Uint16> it( indexStream ); it; ++it )
		{
			chunkData.m_indices.PushBack( it.Get() );
		}

		mesh::ChunkFlags chunkFlags = m_meshView.GetChunkFlags( chunkIndexInMesh );
		chunkData.m_twoSided = chunkFlags.m_isTwoSided;

		return chunkData;
	}

	Bool VisOccluderMeshCompilationSource::GetChunkTwoSided( Uint32 chunkIndex ) const
	{
		const Uint32 chunkIndexInMesh = m_chunkIndices[chunkIndex];
		mesh::ChunkFlags chunkFlags = m_meshView.GetChunkFlags( chunkIndexInMesh );
		return chunkFlags.m_isTwoSided;
	}

}
