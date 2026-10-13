/**
* Copyright (c) 2007-2016 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#ifndef NO_RESOURCE_COOKING

#include "meshCompilationSource.h"
#include "../../../common/resourceMesh/include/mesh.h"
#include "meshPacking.h"
#include "../../../common/resourceMesh/include/meshDataViewRO.h"
#include "../../../common/resourceMesh/include/meshUtils.h"
#if !defined( VANGUARD_ASSET_HOST )
#include "../../../common/resourceMesh/include/meshParamSpeedTreeWind.h"
#endif
#include "../../../common/resourceMesh/include/meshParamGpuBuffer.h"

#include "../../../common/renderBackend/include/meshPackingSource.h"


//////////////////////////////////////////////////////////////////////////
// usings
using red::OffsetPtr;
using red::DynArray;


/*----------------------------------------------------------------------------------------------------------
	CMeshCompilationSource ( CMesh wrapper used during render resource blob compilation )
	TODO: Move to external cooker so it's not part of the engine
----------------------------------------------------------------------------------------------------------*/

CMeshCompilationSource::CMeshCompilationSource(const CMesh& mesh, ECookingPlatform platform, Bool verbose )
	: m_mesh( &mesh )
	, m_view( mesh )
	, m_verbose( verbose )
{
}

String CMeshCompilationSource::GetMeshName() const
{
	return m_mesh->GetFriendlyName();
}

Uint64 CMeshCompilationSource::GetResourceHash() const
{
	return m_mesh->GetPath().GetHash();
}

Vector4 CMeshCompilationSource::GetQuantizationScale() const
{
	return m_mesh->GetBoundingBox().CalcSize() / 2.0f;
}

Vector4 CMeshCompilationSource::GetQuantizationOffset() const
{
	return m_mesh->GetBoundingBox().Min + m_mesh->GetBoundingBox().CalcSize() / 2.0f;
}

void CMeshCompilationSource::GetBonePositions( DynArray< Vector4 >& outPositions ) const
{
	red::DynArray< Vector3 > position3{ red::PoolBackend() };
	mesh::utils::GetBonePositions(m_view, position3);

	for (const auto& v : position3)
		outPositions.PushBack(v);
}

Uint32 CMeshCompilationSource::GetCustomData( red::DynArray< Uint8 >& outData ) const
{
	auto customGpuBuffer = m_mesh->GetParameter< mesh::MeshParamGpuBuffer >();

	if (customGpuBuffer)
	{
		const DataBuffer& customData = customGpuBuffer->GetBufferData();
		if( customData.Size() > 0 )
		{
			outData.Resize( customData.Size() );
			red::Memcpy( outData.TypedData(), customData.Data(), customData.Size() );
			return customGpuBuffer->GetStride();
		}
	}

	return 0;
}

Uint32 CMeshCompilationSource::GetTopologyData( Uint32 chunkIndex, red::DynArray< Uint8 >& outData ) const
{
	if ( auto topologyData = m_mesh->GetParameter< mesh::MeshParamTopologyData >() )
	{
		const DataBuffer& customData = topologyData->GetBufferData();
		if ( !customData.Empty() && chunkIndex < topologyData->GetNumChunks() )
		{
			Uint32 size = topologyData->GetDataSize( chunkIndex );
			Uint32 offset = topologyData->GetDataOffset( chunkIndex );

			const Uint8* dataPtr = static_cast< const Uint8* >( customData.Data() );

			outData.Resize( size );
			red::Memcpy( outData.TypedData(), dataPtr + offset, size );
			return topologyData->GetStride();
		}
	}
	return 0;
}

Uint32 CMeshCompilationSource::GetTopologyMetadata( Uint32 chunkIndex, red::DynArray< Uint8 >& outData ) const
{
	if ( auto topologyMetadata = m_mesh->GetParameter< mesh::MeshParamTopologyMetadata >() )
	{
		const DataBuffer& customData = topologyMetadata->GetBufferData();
		if ( !customData.Empty() && chunkIndex < topologyMetadata->GetNumChunks() )
		{
			Uint32 size = topologyMetadata->GetDataSize( chunkIndex );
			Uint32 offset = topologyMetadata->GetDataOffset( chunkIndex );

			const Uint8* dataPtr = static_cast< const Uint8* >( customData.Data() );

			outData.Resize( size );
			red::Memcpy( outData.TypedData(), dataPtr + offset, size );
			return topologyMetadata->GetStride();
		}
	}

	return 0;
}

Uint32 CMeshCompilationSource::GetNumMeshLods() const
{
	return m_view.GetNumLODs();
}

Bool CMeshCompilationSource::ValidateMeshData() const
{
	const Uint32 numChunks = m_view.GetNumChunks();

	Bool hasValidData = true;
	for (Uint32 i = 0; i < numChunks && hasValidData; ++i)
	{
		const auto numVertices = m_view.GetChunkVertexCount(i);

		// check indices range
		for (mesh::StreamIteratorRO<Uint16> it(m_view.GetChunkStreamData(i, MST_Index_U16)); it; ++it)
		{
			if (*it >= numVertices)
			{
				hasValidData = false;
				break;
			}
		}

		// TODO: check bone indices
		// TODO: check material names
		// TODO: check material type assignment (layered/not layered)
	}

	return hasValidData;
}

Bool CMeshCompilationSource::GetPackingSource( tools::MeshPackingSource* source ) const
{
	// Pack mesh data
	Uint32 vertexDataSize = 0;
	Uint32 indexDataSize = 0;
	Uint32 topologyDataSize = 0;
	Uint32 topologyMetadataSize = 0;

	CMeshPackingSource packer;
	if ( !packer.Pack( m_mesh, vertexDataSize, indexDataSize, topologyDataSize, topologyMetadataSize, nullptr, m_verbose ) )
		return false;

	source->m_vertexDataSize = vertexDataSize;
	source->m_indexDataSize = indexDataSize;
	source->m_topologyDataSize = topologyDataSize;
	source->m_topologyMetadataSize = topologyMetadataSize;

	DynArray< Uint8 >& renderData = source->m_renderData;
	const Uint32 indexBufferOffset = red::AlignUp( vertexDataSize, 1024u );

	//const Uint32 topologyBufferOffset = red::AlignUp( indexBufferOffset, 1024u );
	//const Uint32 topologyMetaBufferOffset = red::AlignUp( topologyBufferOffset, 1024u );
	//const Uint32 totalDataSize = topologyMetaBufferOffset + topologyMetadataSize;
	const Uint32 totalDataSize = indexBufferOffset + indexDataSize;

	renderData.Resize( totalDataSize );
	packer.CopyVertexData( OffsetPtr( renderData.TypedData(), 0 ), vertexDataSize );
	packer.CopyIndexData( OffsetPtr( renderData.TypedData(), indexBufferOffset ), indexDataSize );
	//packer.CopyTopologyData( OffsetPtr( renderData.TypedData(), topologyBufferOffset ), topologyDataSize );
	//packer.CopyTopologyMetadata( OffsetPtr( renderData.TypedData(), topologyMetaBufferOffset ), topologyMetadataSize );

	DynArray< tools::MeshPackingSource::PackedLod >& lods = source->m_lods;
	DynArray< tools::MeshPackingSource::PackedChunk >& chunks = source->m_chunks;

	// Setup lods
	lods.Resize( packer.m_lods.Size() );
	red::Memcpy( lods.Data(), packer.m_lods.Data(), packer.m_lods.DataSize() );
	
	// Setup chunks
	chunks.Reserve( packer.m_chunks.Size() );
	for ( const auto& chunk : packer.m_chunks )
	{
		tools::MeshPackingSource::PackedChunk newChunk;
		newChunk.m_lodMask			= chunk.m_lodMask;
		newChunk.m_renderMask		= chunk.m_renderMask;
		newChunk.m_vertexFactory	= chunk.m_vertexFactory;
		newChunk.m_numVertices		= chunk.m_numVertices;
		newChunk.m_numIndices		= chunk.m_numIndices;
		newChunk.m_vertexPacking	= chunk.m_vertexPacking;

		// setup index stream
		newChunk.m_streams.m_indices = chunk.m_streams.m_indices->m_placement; // where did the packed indices ended up ?

		for ( Uint32 i = 0; i < RED_ARRAY_COUNT(chunk.m_streams.m_vertices); ++i )
		{
			if ( chunk.m_streams.m_vertices[ i ] )
			{
				newChunk.m_streams.m_vertices[ i ] = chunk.m_streams.m_vertices[ i ]->m_placement;
			}
		}
		chunks.PushBack( newChunk );
	}

	return true;
}

void CMeshCompilationSource::GetWindParams( void** windParams ) const
{
#if defined( VANGUARD_ASSET_HOST )
	*windParams = nullptr;
#else
	if( auto windParam = m_mesh->GetParameter< mesh::MeshParamSpeedTreeWind >() )
	{
		*windParams = (void*)&windParam->m_speedTreeWind;
	}
#endif
}
#else
RED_NO_EMPTY_FILE();
#endif
