/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "../../renderData/include/renderMeshBlob.h"
#include "renderMeshCompiler.h"
#include "meshPackingSource.h"
#include "../../redFileSystem/include/memoryFileWriter.h"
#if !defined( VANGUARD_ASSET_HOST )
#include "../../renderData/include/speedTreeWind.h"
#endif

using red::DynArray;

namespace tools
{
	/*----------------------------------------------------------------------------------------------------------
		RenderMeshBlobCompiler
	----------------------------------------------------------------------------------------------------------*/

	RenderMeshBlobCompiler::RenderMeshBlobCompiler( Settings& settings )
		: m_settings( settings )
	{
	}

	RenderMeshBlobCompiler::~RenderMeshBlobCompiler()
	{
	}

	Bool RenderMeshBlobCompiler::Compile( const IMeshCompilationSource& src, IRenderMeshCompilerProcess* process, THandle< IRenderResourceBlob > & outBlob ) // in/out blob
	{
		// TODO!!!: Check CRC or sth

		// Check num lods
		if ( src.GetNumMeshLods() == 0 )
		{
			RED_LOG_WARNING( "MeshCompiler: Mesh '%s' has no LODs.", src.GetMeshName().AsChar() );
			return false;
		}

		// Validate mesh data
		if( !src.ValidateMeshData() )
		{
			RED_LOG_WARNING( "MeshCompiler: !!! CORRUPTED MESH DATA !!!" );
			RED_LOG_WARNING( "MeshCompiler: Mesh %hs contains corrupted data and will not be cooked. Expect graphical glitches.", src.GetMeshName().AsChar() );
			return false;
		}

		// Pack mesh data
		MeshPackingSource packer;
		if( !src.GetPackingSource( &packer ) )
		{
			RED_LOG_WARNING( "MeshCompiler: Couldn't get packing source for mesh %hs.", src.GetMeshName().AsChar() );
			return false;
		}

		if ( !packer.m_vertexDataSize || !packer.m_indexDataSize )
		{
			RED_LOG_WARNING( "MeshCompiler: Either vertex or index data is empty, thus mesh %hs won't be cooked.", src.GetMeshName().AsChar() );
			return false;
		}

		// ------ Set header ------
		rend::RenderMeshBlob::Header header;
		// mesh cooked header
		header.m_version = rend::RenderMeshBlob::FILE_VERSION;
		header.m_quantizationScale = src.GetQuantizationScale();
		header.m_quantizationOffset = src.GetQuantizationOffset();
		header.m_vertexBufferSize = packer.m_vertexDataSize;
		header.m_indexBufferSize = packer.m_indexDataSize;
		header.m_indexBufferOffset = red::AlignUp( header.m_vertexBufferSize, 1024u );

		// Get cached bone positions
		{
			DynArray< Vector4 > bonePositions;
			src.GetBonePositions( bonePositions );
			header.m_bonePositions = bonePositions;
		}

		// Get cached custom data
		{
			DynArray< Uint8 > customData;
			Uint32 elemStride = 0;
			header.m_customDataElemStride = src.GetCustomData( customData );
			header.m_customData = customData;
		}

		// Get cached topology data
		{
			for ( Uint32 i = 0; i < packer.m_chunks.Size(); ++i )
			{
				rend::TopologyData topologyData;
				topologyData.m_dataStride = src.GetTopologyData( i, topologyData.m_data );
				topologyData.m_metadataStride = src.GetTopologyMetadata( i, topologyData.m_metadata );

				header.m_topology.PushBack( topologyData );
			}

		}

		// Setup LODs
		header.m_renderLODs.Reserve( packer.m_lods.Size() );
		for ( const auto& lod : packer.m_lods )
		{
			header.m_renderLODs.PushBack( lod.m_distance );
		}

		// Setup chunks
		header.m_renderChunkInfos.Reserve( packer.m_chunks.Size() );
		for( const auto& chunk : packer.m_chunks )
		{
			rend::Chunk& newChunk = header.m_renderChunkInfos.EmplaceBack();
			newChunk.m_lodMask			= chunk.m_lodMask;
			newChunk.m_renderMask		= chunk.m_renderMask;
			newChunk.m_vertexFactory	= chunk.m_vertexFactory;
			newChunk.m_numVertices		= chunk.m_numVertices;
			newChunk.m_numIndices		= chunk.m_numIndices;


			// setup index stream
			{
				newChunk.m_chunkIndices.type		= GpuWrapApi::IBCT_IndexUShort;
				newChunk.m_chunkIndices.byteOffset	= chunk.m_streams.m_indices; // where did the packed indices ended up ?

				if( process )
				{
					red::ArraySpan< Uint16 > indices( reinterpret_cast<Uint16*>(packer.m_renderData.TypedData() + header.m_indexBufferOffset + newChunk.m_chunkIndices.byteOffset), newChunk.m_numIndices );

					process->ProcessIndices( indices, chunk.m_numVertices );

					// Mark header process flag
					header.m_dataProcessing |= (Uint16)rend::RenderMeshBlob::RenderMeshBlobProcess::VertexCacheOptimization;
				}
			}

			// setup vertex stream
			{
				// setup VertexLayoutDesc to be serialized in the RenderMeshBlob
				GpuWrapApi::VertexLayoutDesc desc( chunk.m_vertexPacking );
				newChunk.m_chunkVertices.m_vertexLayout = desc;

				const Uint32 numByteOffsets = RED_ARRAY_COUNT( chunk.m_streams.m_vertices );
				newChunk.m_chunkVertices.m_byteOffsets.Resize( numByteOffsets );
				for( Uint32 i=0; i < numByteOffsets; ++i )
				{
					newChunk.m_chunkVertices.m_byteOffsets[i] = chunk.m_streams.m_vertices[i];
				}
			}
		}

			// Compile wind params if exists
#if !defined( VANGUARD_ASSET_HOST )
			{
				CSpeedTreeWind* speedTreeWind = nullptr;
			src.GetWindParams( (void**) &speedTreeWind );
			if( speedTreeWind )
			{
				DynArray< Uint8 > cookedSystemData;
				CMemoryFileWriterWithDebugName writer( cookedSystemData, src.GetMeshName() );
				SerializeWindParams( writer, *speedTreeWind );
					header.m_speedTreeWind = cookedSystemData;
				}
			}
#endif

		outBlob.Reset( RED_NEW( rend::RenderMeshBlob ) );
		rend::RenderMeshBlob* meshBlob = static_cast< rend::RenderMeshBlob* >( outBlob.Get() );
		meshBlob->m_header = header;
		meshBlob->m_renderBuffer = DataBuffer::Copy( packer.m_renderData.TypedData(), packer.m_renderData.DataSize() );

		meshBlob->PreprocessData();

		return true;
	}
}//tools
