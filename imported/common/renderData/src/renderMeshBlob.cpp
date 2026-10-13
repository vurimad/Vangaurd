/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "renderMeshBlob.h"
#include "bufferChunkType.h"
#include "../../redFileSystem/include/memoryFileReader.h"
#include "../../redFileSystem/include/memoryFileWriter.h"
#include "materialStructures.h"
#include "meshEnum.h"
#include "../../redMemory/include/poolRoot.h"
#include "../../redReflection/include/resource.h"

// #pragma optimize("",off)

//**************************************************************************************************************************//
//**************************************************************************************************************************//

namespace
{
	static red::UniquePtr<rend::IMeshBlobIndexProcess>		s_blobProcess;
}

void rend::RenderMeshBlob::RegisterIndexProcessor( red::UniquePtr<rend::IMeshBlobIndexProcess>&& ptr )
{
	s_blobProcess = std::move( ptr );
}

//**************************************************************************************************************************//
//**************************************************************************************************************************//

RTTI_BEGIN_TYPE_IN_NAMESPACE( VertexBufferChunk, rend );
{
	RTTI_PROPERTY( m_vertexLayout );
	RTTI_PROPERTY( m_byteOffsets );
}
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( IndexBufferChunk, rend );
{
	RTTI_PROPERTY( type );
	RTTI_PROPERTY( byteOffset );
}
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( Chunk, rend );
{
	RTTI_PROPERTY( m_chunkVertices );
	RTTI_PROPERTY( m_chunkIndices );
	RTTI_PROPERTY( m_numVertices );
	RTTI_PROPERTY( m_numIndices );
	RTTI_PROPERTY( m_materialId );
	RTTI_PROPERTY( m_vertexFactory );
	RTTI_PROPERTY( m_baseRenderMask );
	RTTI_PROPERTY( m_mergedRenderMask );
	RTTI_PROPERTY( m_renderMask );
	RTTI_PROPERTY( m_lodMask );
}
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( TopologyData, rend );
{
	RTTI_PROPERTY( m_data );
	RTTI_PROPERTY( m_metadata );
	RTTI_PROPERTY( m_dataStride );
	RTTI_PROPERTY( m_metadataStride );
}
RTTI_END_TYPE();

const Uint32 rend::RenderMeshBlob::FILE_VERSION = (Uint32)RenderMeshBlobVersions::VER_MAX - 1;


RTTI_BEGIN_TYPE_IN_NAMESPACE( RenderMeshBlobHeader, rend );
{
	RTTI_PROPERTY( m_version );
	RTTI_PROPERTY( m_dataProcessing );
	RTTI_PROPERTY( m_bonePositions );
	RTTI_PROPERTY( m_renderLODs );
	RTTI_PROPERTY( m_renderChunks );
	RTTI_PROPERTY( m_renderChunkInfos );
	RTTI_PROPERTY( m_speedTreeWind );
	RTTI_PROPERTY( m_customData );
	RTTI_PROPERTY( m_customDataElemStride );
	RTTI_PROPERTY( m_topologyData );
	RTTI_PROPERTY( m_topologyDataStride );
	RTTI_PROPERTY( m_topologyMetadata );
	RTTI_PROPERTY( m_topologyMetadataStride );
	RTTI_PROPERTY( m_topology );
	RTTI_PROPERTY( m_quantizationScale );
	RTTI_PROPERTY( m_quantizationOffset );
	RTTI_PROPERTY( m_vertexBufferSize );
	RTTI_PROPERTY( m_indexBufferSize );
	RTTI_PROPERTY( m_indexBufferOffset );
}
RTTI_END_TYPE();


RTTI_BEGIN_TYPE_IN_NAMESPACE( RenderMeshBlob, rend );
{
	RTTI_PARENT_TYPE( IRenderResourceBlob );
	RTTI_PROPERTY( m_header );
	RTTI_PROPERTY( m_renderBuffer );
}
RTTI_END_TYPE();	

//**************************************************************************************************************************//
//**************************************************************************************************************************//

rend::RenderMeshBlob::RenderMeshBlob()
	: m_renderBuffer( rend::PoolRenderBlobsBuffers() )
{
}

rend::RenderMeshBlob::~RenderMeshBlob()
{
}

Uint32 rend::RenderMeshBlob::GetVersion() const
{
	return m_header.m_version;
}

Bool rend::RenderMeshBlob::IsVersionCurrent() const
{
	return m_header.m_version == FILE_VERSION;
}
Bool rend::RenderMeshBlob::IsVersionValid() const
{
	return m_header.m_version >= (Uint32)RenderMeshBlobVersions::VER14_RenderInCascadesFlagAdded;
}

Uint32 rend::RenderMeshBlob::GetDataSize() const
{
	return m_renderBuffer.Size();
}

Bool rend::RenderMeshBlob::HasTopologyData() const
{
	return !m_header.m_topologyData.Empty() && !m_header.m_topologyMetadata.Empty();
}

void rend::RenderMeshBlob::OnSerialize( class IFile& file )
{
	IRenderResourceBlob::OnSerialize( file );

	// HACK : This should probably actually be done in OnPostLoad, but that can result in the CMesh getting OnPostLoad
	// before us, and so it tries to use the blob before we have a chance to do any updates.
	if ( file.IsReader() )
	{
		if ( m_header.m_version != FILE_VERSION )
		{
			UpgradeVersion( false );

			// #TODO: This would be a useful thing, except that it results in huge stalls in editor...
			//if ( CResource* parent = Cast<CResource>( GetParent() ) )
			//{
			//	parent->MarkModified();
			//}
		}

		PostProcessBlob();
	}
}

void rend::RenderMeshBlob::OnPreSave( const PreSaveContext& context )
{
	PreprocessData();
}

EMaterialVertexFactory VER16_RemapVertexFactory( Uint8 mvf )
{
	switch ( mvf )
	{
	/*MVF_Invalid*/								case 0: return MVF_Invalid;
	/*MVF_Terrain*/								case 1: return MVF_Terrain;
	/*MVF_MeshStatic*/							case 2: return MVF_MeshStatic;
	/*MVF_MeshSkinned*/							case 3: return MVF_MeshSkinned;
	/*MVF_MeshExtSkinned*/						case 4: return MVF_MeshExtSkinned;
	/*MVF_GarmentMeshSkinned*/					case 5: return MVF_GarmentMeshSkinned;
	/*MVF_GarmentMeshExtSkinned*/				case 6: return MVF_GarmentMeshExtSkinned;
	/*MVF_MeshSpeedTree*/						case 7: return MVF_MeshSpeedTree;
	/*MVF_ParticleBilboard*/					case 8: return MVF_ParticleBilboard;
	/*MVF_Unused1*/								case 9: RED_HALT("Unused MeshVertexFactory! MVF_Unused1"); return MVF_Invalid;
	/*MVF_ParticleParallel*/					case 10: return MVF_ParticleParallel;
	/*MVF_Unused2*/								case 11: RED_HALT("Unused MeshVertexFactory! MVF_Unused2"); return MVF_Invalid;
	/*MVF_ParticleMotionBlur*/					case 12: return MVF_ParticleMotionBlur;
	/*MVF_Unused3*/								case 13: RED_HALT("Unused MeshVertexFactory! MVF_Unused3"); return MVF_Invalid;
	/*MVF_ParticleSphereAligned*/				case 14: return MVF_ParticleSphereAligned;
	/*MVF_Unused4*/								case 15: RED_HALT("Unused MeshVertexFactory! MVF_Unused4"); return MVF_Invalid;
	/*MVF_ParticleVerticalFixed*/				case 16: return MVF_ParticleVerticalFixed;
	/*MVF_Unused5*/								case 17: RED_HALT("Unused MeshVertexFactory! MVF_Unused5"); return MVF_Invalid;
	/*MVF_ParticleTrail*/						case 18: return MVF_ParticleTrail;
	/*MVF_ParticleFacingTrail*/					case 19: return MVF_ParticleFacingTrail;
	/*MVF_ParticleScreen*/						case 20: return MVF_ParticleScreen;
	/*MVF_ParticleBeam*/						case 21: return MVF_ParticleBeam;
	/*MVF_ParticleFacingBeam*/					case 22: return MVF_ParticleFacingBeam;
	/*MVF_Unused6*/								case 23: RED_HALT("Unused MeshVertexFactory! MVF_Unused6"); return MVF_Invalid;
	/*MVF_Unused7*/								case 24: RED_HALT("Unused MeshVertexFactory! MVF_Unused7"); return MVF_Invalid;
	/*MVF_Debug*/								case 25: return MVF_Debug;
	/*MVF_DrawBuffer*/							case 26: return MVF_DrawBuffer;
	/*MVF_Fullscreen*/							case 27: return MVF_Fullscreen;
	/*MVF_MeshSkinnedVehicle*/					case 28: return MVF_MeshSkinnedVehicle;
	/*MVF_MeshStaticVehicle*/					case 29: return MVF_MeshStaticVehicle;
	/*MVF_MeshProcedural*/						case 30: return MVF_MeshProcedural;
	/*MVF_MeshDestructible*/					case 31: return MVF_MeshDestructible;
	/*MVF_MeshDestructibleSkinned*/				case 32: return MVF_MeshDestructibleSkinned;
	/*MVF_MeshSkinnedLightBlockers*/			case 33: return MVF_MeshSkinnedLightBlockers;
	/*MVF_MeshExtSkinnedLightBlockers*/			case 34: return MVF_MeshExtSkinnedLightBlockers;
	/*MVF_GarmentMeshSkinnedLightBlockers*/		case 35: return MVF_GarmentMeshSkinnedLightBlockers;
	/*MVF_GarmentMeshExtSkinnedLightBlockers*/	case 36: return MVF_GarmentMeshExtSkinnedLightBlockers;
	/*MVF_Max*/									case 37: return MVF_Max;
	default: RED_HALT( "Invalid MeshVertexFactory! MVF: %d", mvf ); return MVF_Invalid;
	}
}

#if 0
// TODO:
// change vertexFactory property type in rend::Chunk from Uint8 to RTTI supported EMaterialVertexFactory enum
// in order to do so, the OnPropertyTypeMismatch has to be called, but we have no way to figure out what is the rend::Chunk object we are currently processing.
// Some kind of Context would be useful, with pointer to current object
Bool rend::RenderMeshBlob::OnPropertyTypeMismatch( CName propertyName, const rtti::Property* existingProperty, const rtti::Variant& readValue )
{
	if ( propertyName == RED_NAME_CONSTEXPR_NOREG( "vertexFactory" ) )
	{
		Uint8 oldVertexFactory;
		readValue.Get( oldVertexFactory );
		EMaterialVertexFactory newVertexFactory = VER16_RemapVertexFactory( oldVertexFactory );
		existingProperty->Set( this, &newVertexFactory );
		return true;
	}
	return IRenderResourceBlob::OnPropertyTypeMismatch( propertyName, existingProperty, readValue );
}
#endif

GpuWrapApi::eBufferChunkType VER7_ConvertBufferChunkTypeToInstancedOnly( Uint8 type )
{
	/*
	Following BufferChunkTypes were removed (their original values are shown for reference):
	BCT_VertexMeshStaticPositionOnly			= 1,
	BCT_VertexMeshStaticSimple					= 2,
	BCT_VertexMeshStaticExtended				= 3,
	BCT_VertexMeshSkinnedSimple					= 4,
	BCT_VertexMeshSkinnedExtended				= 5,
	BCT_VertexMeshExtSkinnedSimple				= 6,
	BCT_VertexMeshExtSkinnedExtended			= 7,
	BCT_VertexMeshGarmentSkinnedSimple			= 8,
	BCT_VertexMeshGarmentSkinnedExtended		= 9,
	BCT_VertexMeshExtGarmentSkinnedSimple		= 10,
	BCT_VertexMeshExtGarmentSkinnedExtended		= 11,
	BCT_VertexMeshSpeedTreeSimple				= 12,
	BCT_VertexMeshSpeedTreeExtended				= 13,

	so a few conversions have to be taken into consideration:
	*/

	Uint8 adjustedType = 0;
	if ( type >= 1 /*BCT_VertexMeshStaticPositionOnly*/ && type <= 13 /*BCT_VertexMeshSpeedTreeExtended*/ )
	{
		/*
		if an old (non-instanced) BCT is read it has to be transformed to a corresponding Instanced BCT.
		Fortunately for us, the instanced ones are shown in the same order, so a simple addition can be done.
		Corresponding Instanced BCTs start now from index 4, so shifting by 3 is enough.
		*/
		adjustedType = type + 3;
	}
	else if ( type >= 14 )
	{
		/*
		a BCT other than the removed ones is read, the one just after the removed ones was BCT_VertexWater = 14.
		To adjust to new format, we just have to subtract 13 to account for the removed BCTs.
		*/
		adjustedType = type - 13;
	}

	// this is BCT_VertexBillboard
	RED_ASSERT( adjustedType < GpuWrapApi::eBufferChunkType::BCT_Max, "Invalid BufferChunkType after conversion." );
	return (GpuWrapApi::eBufferChunkType)adjustedType;
}

void ConvertToSNorm( Uint16& value )
{
	constexpr auto minValue = std::numeric_limits< Int16 >::min();
	Int32 signedValue32 = (Int32)value + minValue;
	value = (Uint16&)signedValue32;
}

void rend::RenderMeshBlob::UpgradeVersion( Bool allowMeshOptimization )
{
	if ( m_header.m_version < (Uint32)RenderMeshBlobVersions::VER8_BufferChunkTypeReplacedWithVertexLayout )
	{
		RED_LOG_ERROR( "Ancient content. Expect bugs." );
		return;
	}

	if ( m_header.m_version < (Uint32)RenderMeshBlobVersions::VER11_RTTIChunks )
	{
		// Move all chunk informations from old array to new array
		// Read the chunk data
		CMemoryFileReaderExternalBuffer systemDataReader( m_header.m_renderChunks.Data(), (Uint32)m_header.m_renderChunks.DataSize() );
		red::DynArray< rend::Chunk_DEPRECATED > oldChunks{ rend::PoolRenderBlobs() };
		systemDataReader.m_version = m_header.m_version;
		systemDataReader << oldChunks;



		if ( m_header.m_version < (Uint32)RenderMeshBlobVersions::VER9_AddedDrawBufferVertexFactory )
		{
			// Update vertex factories
			for ( auto& chunk : oldChunks )
			{
				// Adjust vertex factory
				if ( chunk.m_vertexFactory >= MVF_Fullscreen )    // Added MVF_DrawBuffer
				{
					chunk.m_vertexFactory += 1u;
				}
			}
		}




		if ( m_header.m_version < (Uint32)RenderMeshBlobVersions::VER10_SNormPositions )
		{
			// Convert normalized positions to signed values(required by DXR) if they use PT_UShort4N vertex packing
			Bool needQuantizeUpdate = false;
			for ( rend::Chunk_DEPRECATED& chunk : oldChunks )
			{
				GpuWrapApi::VertexPacking::PackingElement* positionElement = nullptr;
				for ( Uint32 i = 0; i < chunk.m_chunkVertices.m_vertexLayout.m_elements.Size(); ++i )
				{
					auto& element = chunk.m_chunkVertices.m_vertexLayout.m_elements[ i ];
					if ( element.m_usage == GpuWrapApi::VertexPacking::PS_Position && element.m_type == GpuWrapApi::VertexPacking::PT_UShort4N )
					{
						positionElement = &element;
						break;
					}
				}
				if ( positionElement != nullptr )
				{
					needQuantizeUpdate = true;

					positionElement->m_type = GpuWrapApi::VertexPacking::PT_Short4N;
					chunk.m_chunkVertices.m_vertexLayout.UpdateHash();

					Uint8 slot = positionElement->m_streamIndex;
					Uint32 stride = chunk.m_chunkVertices.m_vertexLayout.m_slotStrides[ slot ];
					Uint32 offset = chunk.m_chunkVertices.m_byteOffsets[ slot ];
					offset += chunk.m_chunkVertices.m_vertexLayout.GetElementOffset( *positionElement );

					Uint16* vertexData = static_cast<Uint16*>( red::OffsetPtr( m_renderBuffer.Data(), offset ) );
					for ( Uint16 i = 0; i < chunk.m_numVertices; ++i )
					{
						// Only convert if we haven't already converted. Expect all W components to be 1.0
						if ( vertexData[ 3 ] == 0xFFFF )
						{
							ConvertToSNorm( vertexData[ 0 ] );
							ConvertToSNorm( vertexData[ 1 ] );
							ConvertToSNorm( vertexData[ 2 ] );
							ConvertToSNorm( vertexData[ 3 ] );
						}
						vertexData += stride / 2;
					}
				}
			}
			if ( needQuantizeUpdate )
			{
				m_header.m_quantizationOffset += m_header.m_quantizationScale * 0.5f;
				m_header.m_quantizationScale = m_header.m_quantizationScale * 0.5f;
			}
		}



		m_header.m_renderChunkInfos.Resize( oldChunks.Size() );
		for ( Uint32 c=0; c<oldChunks.Size(); c++ )
		{
			Chunk& newChunk = m_header.m_renderChunkInfos[ c ];
			const Chunk_DEPRECATED& oldChunk = oldChunks[ c ];

			newChunk.m_chunkVertices.m_vertexLayout = oldChunk.m_chunkVertices.m_vertexLayout;
			newChunk.m_chunkVertices.m_byteOffsets.Resize( RENDER_MESH_NUM_STREAMS );
			red::Memcpy( newChunk.m_chunkVertices.m_byteOffsets.TypedData(), oldChunk.m_chunkVertices.m_byteOffsets, newChunk.m_chunkVertices.m_byteOffsets.DataSize() );
			
			newChunk.m_chunkIndices.byteOffset = oldChunk.m_chunkIndices.byteOffset;
			newChunk.m_chunkIndices.type = oldChunk.m_chunkIndices.type;

			newChunk.m_materialId = oldChunk.m_materialId;
			newChunk.m_numIndices = oldChunk.m_numIndices;
			newChunk.m_numVertices = oldChunk.m_numVertices;
			newChunk.m_baseRenderMask = (Uint16)oldChunk.m_baseRenderMask;
			newChunk.m_mergedRenderMask = (Uint16)oldChunk.m_mergedRenderMask;
			newChunk.m_vertexFactory = oldChunk.m_vertexFactory;
			newChunk.m_lodMask = oldChunk.m_lodMask;
		}

		// Kill the old shit
		m_header.m_renderChunks.Clear();
		m_header.m_version = (Uint32)RenderMeshBlobVersions::VER11_RTTIChunks;
	}

	if ( m_header.m_version < (Uint32)RenderMeshBlobVersions::VER12_MeshTopologyPerChunk )
	{
		m_header.m_topology.Resize( m_header.m_renderChunkInfos.Size() );

		if ( !m_header.m_topologyData.Empty() && !m_header.m_topologyMetadata.Empty() )
		{
			rend::TopologyData& topology = m_header.m_topology[ 0 ];
			topology.m_data = m_header.m_topologyData;
			topology.m_metadata = m_header.m_topologyMetadata;
			topology.m_dataStride = m_header.m_topologyDataStride;
			topology.m_metadataStride = m_header.m_topologyMetadataStride;
		}

		m_header.m_topologyData.Clear();
		m_header.m_topologyMetadata.Clear();
		m_header.m_topologyDataStride = 0;
		m_header.m_topologyMetadataStride = 0;

		m_header.m_version = (Uint32)RenderMeshBlobVersions::VER12_MeshTopologyPerChunk;
	}

	if ( m_header.m_version < (Uint32)RenderMeshBlobVersions::VER13_TopologyDataMismatch )
	{
		if ( m_header.m_topology.Size() < m_header.m_renderChunkInfos.Size() )
		{
			const Uint32 diffCount = m_header.m_renderChunkInfos.Size() - m_header.m_topology.Size();
			m_header.m_topology.Reserve( m_header.m_renderChunkInfos.Size() );

			for ( Uint32 i = 0; i < diffCount; ++i )
			{
				m_header.m_topology.PushBack( TopologyData() );
			}
		}

		m_header.m_version = (Uint32)RenderMeshBlobVersions::VER13_TopologyDataMismatch;
	}

	if ( m_header.m_version < (Uint32)RenderMeshBlobVersions::VER14_RenderInCascadesFlagAdded )
	{
		for ( auto& chunk : m_header.m_renderChunkInfos )
		{
			chunk.m_baseRenderMask |= MCR_Cascades;
			chunk.m_mergedRenderMask |= MCR_Cascades;
		}

		m_header.m_version = (Uint32)RenderMeshBlobVersions::VER14_RenderInCascadesFlagAdded;
	}

	if( m_header.m_version < (Uint32)RenderMeshBlobVersions::VER15_VertecCacheOptimization )
	{
		// Gonna be handled by the ProcessData call
		m_header.m_version = (Uint16)RenderMeshBlobVersions::VER15_VertecCacheOptimization;
	}

	if ( m_header.m_version < (Uint32)RenderMeshBlobVersions::VER16_VertexFactoriesCleanup )
	{
		for ( auto& chunk : m_header.m_renderChunkInfos )
		{
			chunk.m_vertexFactory = VER16_RemapVertexFactory( chunk.m_vertexFactory );
		}
		m_header.m_version = (Uint16)RenderMeshBlobVersions::VER16_VertexFactoriesCleanup;
	}

	if ( m_header.m_version < (Uint32)RenderMeshBlobVersions::VER17_IndexBuffersAlignedTo4 )
	{
		/*
		Index data in render blob is aligned to 2
		However, when we create dynamic decals for chunks, we bind index buffer as ByteAddressBuffer and fetch data using Load3(offset), the offset param has to be aligned to 4
		This code will adjust offsets for index data for all chunks and fix the buffer
		*/

		/*
		Explanation for the concept of ChunkIndexPair, it is introduced to avoid errors in meshes with duplicated chunks.
		Let's say that we have a mesh with 8 chunks, notice that chunk1 and chunk4 have the same byte offset for indices, also chunk2 needs adjusting of the offset to be multiple of 4
		ChunkIndex	ByteOffset	Tris
		0			0			2
		1			12			7
		2			54			4
		3			78			4
		4			12			7
		5			102			4
		6			126			4
		7			140			4

		When no chunk index pair would be checked, we would end up with offsets like this:
		ChunkIndex	ByteOffset	Tris
		0			0			2
		1			12			7
		2			56			4	<- adjusted by 2
		3			80			4	<- adjusted by 2 (from adjusting chunk2)
		4			16			7	<- adjusted by 2 to 14 (from adjusting chunk2) and then again, because 14 is not aligned to 4
		5			108			4	<- adjusted by 4 to 106 (from adjusting chunk2 and chunk4) and then again, because 106 is not aligned to 4
		6			132			4	<- adjusted by 6 to 132 (from adjusting chunk2, chunk4 and chunk5)
		7			148			4	<- adjusted by 6 to 146 (from adjusting chunk2, chunk4 and chunk5) and then again, because 146 is not aligned to 4
		Now copying over index ranges to new buffer, we will first copy proper data when copying chunk1. Then, when copying chunk4 we will override data for chunk2 and 3 because of incorrect offset

		Having info that chunks 1 and 4 are duplicated, we end up with following byteOffsets:
		ChunkIndex	ByteOffset	Tris
		0			0			2
		1			12			7
		2			56			4	<- adjusted by 2, because 54 is not aligned to 4
		3			80			4	<- adjusted by 2 (from adjusting chunk2)
		4			12			7	<- not adjusted, we copy byteOffset from chunk1
		5			104			4	<- adjusted by 2 (from adjusting chunk2)
		6			128			4	<- adjusted by 2 (from adjusting chunk2)
		7			144			4	<- adjusted by 2 to 142 (from adjusting chunk2) and then again, because 142 is not aligned to 4
		*/
		struct ChunkIndexPair
		{
			Uint32 srcChunk;
			Uint32 dstChunk;
		};
		red::DynArray< ChunkIndexPair > chunkPairs { rend::PoolRenderBlobs() };
		for ( Uint32 i = 0; i < m_header.m_renderChunkInfos.Size(); ++i )
		{
			const auto& thisChunk = m_header.m_renderChunkInfos[ i ];
			for ( Uint32 j = i + 1; j < m_header.m_renderChunkInfos.Size(); ++j )
			{
				const auto& nextChunk = m_header.m_renderChunkInfos[ j ];
				if ( thisChunk.m_chunkIndices.byteOffset == nextChunk.m_chunkIndices.byteOffset )
				{
					chunkPairs.PushBack( { i, j } );
				}
			}
		}

		// first copy final offsets to an array, we will be modifying those
		red::DynArray< Uint32 > chunkIndicesOffsets{ rend::PoolRenderBlobs() };
		chunkIndicesOffsets.Reserve( m_header.m_renderChunkInfos.Size() );

		// calculate final offsets, indices are stored after vertex data, so adjusted offsets will be checked
		for ( auto& chunk : m_header.m_renderChunkInfos )
		{
			chunkIndicesOffsets.PushBack( chunk.m_chunkIndices.byteOffset + m_header.m_indexBufferOffset );
		}

		// accumulate how much the index buffer will grow due to adjusting offsets
		Uint32 sizeIncrease = 0;
		for ( Uint32 i = 0; i < chunkIndicesOffsets.Size(); ++i )
		{
			for ( const auto& chunkPair : chunkPairs )
			{
				if ( i == chunkPair.dstChunk )
				{
					chunkIndicesOffsets[ i ] = chunkIndicesOffsets[ chunkPair.srcChunk ];
				}
			}

			const Uint32 adjustedOffset = red::AlignUp( chunkIndicesOffsets[ i ], 4u );
			const Uint32 diff = adjustedOffset - chunkIndicesOffsets[ i ];
			if ( diff > 0 )
			{
				sizeIncrease += diff;
				// adjust each offset AFTER current, accumulating changes
				for ( Uint32 j = i; j < chunkIndicesOffsets.Size(); ++j )
				{
					chunkIndicesOffsets[ j ] += diff;
				}
			}
		}

		// bring offsets back to being relative to index data start
		for ( auto& offset : chunkIndicesOffsets )
		{
			offset -= m_header.m_indexBufferOffset;
		}
	
		// change renderBlob only if the size changed
		if ( sizeIncrease > 0 )
		{
			const Uint32 newSize = m_renderBuffer.Size() + sizeIncrease;

			DataBuffer renderBuffer = DataBuffer( newSize, m_renderBuffer.GetAlignment(), m_renderBuffer.GetPool() );
			red::Memzero( renderBuffer.Data(), renderBuffer.Size() );

			// copy vertices
			red::Memcpy( renderBuffer.Data(), m_renderBuffer.Data(), m_header.m_vertexBufferSize );

			// copy indices chunk by chunk to adjusted offsets
			for ( Uint32 i = 0; i < chunkIndicesOffsets.Size(); ++i )
			{
				const Uint32 dstOffset = m_header.m_indexBufferOffset + chunkIndicesOffsets[ i ];
				const Uint32 srcOffset = m_header.m_indexBufferOffset + m_header.m_renderChunkInfos[ i ].m_chunkIndices.byteOffset;

				Uint32 chunkIndicesDataSize = 0;
				switch ( m_header.m_renderChunkInfos[i].m_chunkIndices.type )
				{
					case GpuWrapApi::IBCT_IndexUInt:	chunkIndicesDataSize = m_header.m_renderChunkInfos[ i ].m_numIndices * sizeof( Uint32 ); break;
					case GpuWrapApi::IBCT_IndexUShort:	chunkIndicesDataSize = m_header.m_renderChunkInfos[ i ].m_numIndices * sizeof( Uint16 ); break;
					default:							RED_HALT( "Unknown index type: %d", m_header.m_renderChunkInfos[i].m_chunkIndices.type );
				}

				RED_ASSERT( dstOffset + chunkIndicesDataSize <= renderBuffer.Size(), "Chunk index data will not fit in new buffer, destOffset: %d, indexDataSize: %d, bufferSize: %d", dstOffset, chunkIndicesDataSize, renderBuffer.Size() );

				// source and destination index data pointers
				void* dstPtr = red::OffsetPtr( renderBuffer.Data(), dstOffset );
				void* srcPtr = red::OffsetPtr( m_renderBuffer.Data(), srcOffset );

				// simple copy
				red::Memcpy( dstPtr, srcPtr, chunkIndicesDataSize );
			}

			// adjust index offsets in render blob header
			m_header.m_indexBufferSize += sizeIncrease;
			for ( Uint32 i = 0; i < chunkIndicesOffsets.Size(); ++i )
			{
				m_header.m_renderChunkInfos[ i ].m_chunkIndices.byteOffset = chunkIndicesOffsets[ i ];
			}

			// replace buffer
			m_renderBuffer = std::move( renderBuffer );
		}
		m_header.m_version = (Uint16)RenderMeshBlobVersions::VER17_IndexBuffersAlignedTo4;
	}

	if ( m_header.m_version < (Uint32)RenderMeshBlobVersions::VER18_MeshChunkFlagsCleanup )
	{
		for ( Uint32 c = 0; c<m_header.m_renderChunkInfos.Size(); c++ )
		{
			Chunk& chunk = m_header.m_renderChunkInfos[ c ];

			EMeshChunkRenderMask oldMaskCopy = static_cast<EMeshChunkRenderMask>( chunk.m_baseRenderMask );

			// Below I deliberately don't check distant shadows flag. It should be enough to check "Cascades" and "LocalShadows"
			chunk.m_renderMask = (EMeshChunkFlags)0;
			if ( ( oldMaskCopy & MCR_Scene ) != 0 )									*(Uint16*)&chunk.m_renderMask |= EMeshChunkFlags::MCF_RenderInScene;
			if ( ( oldMaskCopy & ( MCR_Cascades | MCR_LocalShadows | MCR_Cascade1 | MCR_Cascade2 | MCR_Cascade3 | MCR_Cascade4 | MCR_DistantShadows ) ) != 0 )
																						*(Uint16*)&chunk.m_renderMask |= EMeshChunkFlags::MCF_RenderInShadows;
			if ( ( oldMaskCopy & MCR_IsTwoSided ) != 0 )							*(Uint16*)&chunk.m_renderMask |= EMeshChunkFlags::MCF_IsTwoSided;
			if ( ( oldMaskCopy & MCR_IsRayTracedEmissive ) != 0 )					*(Uint16*)&chunk.m_renderMask |= EMeshChunkFlags::MCF_IsRayTracedEmissive;
			if ( ( oldMaskCopy & MCR_IsGIVisible ) != 0 )							*(Uint16*)&chunk.m_renderMask |= EMeshChunkFlags::MCF_IsGIVisible;
			if ( ( oldMaskCopy & MCR_IsGITransparent ) != 0 )						*(Uint16*)&chunk.m_renderMask |= EMeshChunkFlags::MCF_IsGITransparent;
			if ( ( oldMaskCopy & MCR_PrefabProxy ) != 0 )							*(Uint16*)&chunk.m_renderMask |= EMeshChunkFlags::MCF_IsPrefabProxy;
		}

		m_header.m_version = (Uint16)RenderMeshBlobVersions::VER18_MeshChunkFlagsCleanup;
	}

	// TODO : Handle further incremental upgrades here
	static_assert( FILE_VERSION == (Uint32)RenderMeshBlobVersions::VER18_MeshChunkFlagsCleanup, "Version has been bumped. Need to make upgrades?" );
}

void rend::RenderMeshBlob::PreprocessData()
{
	if( 0 == (m_header.m_dataProcessing & (Uint32)RenderMeshBlobProcess::VertexCacheOptimization) && s_blobProcess )
	{
		for( const auto& chunk : m_header.m_renderChunkInfos )
		{
			switch( chunk.m_chunkIndices.type )
			{
			case GpuWrapApi::eIndexBufferChunkType::IBCT_IndexUInt:
			{
				Uint32* indices = reinterpret_cast<Uint32*>(chunk.m_chunkIndices.byteOffset + m_header.m_indexBufferOffset + (Uint8*)m_renderBuffer.Data());

				s_blobProcess->ProcessIndices( red::ArraySpan<Uint32>( indices, chunk.m_numIndices ), chunk.m_numVertices );

			} break;

			case GpuWrapApi::eIndexBufferChunkType::IBCT_IndexUShort:
			{
				Uint16* indices = reinterpret_cast<Uint16*>(chunk.m_chunkIndices.byteOffset + m_header.m_indexBufferOffset + (Uint8*)m_renderBuffer.Data());

				s_blobProcess->ProcessIndices( red::ArraySpan<Uint16>( indices, chunk.m_numIndices ), chunk.m_numVertices );

			} break;

			}
		}

		m_header.m_dataProcessing |= (Uint16)RenderMeshBlobProcess::VertexCacheOptimization;
	}
}

void rend::RenderMeshBlob::PostProcessBlob()
{
	// convert old Float4 skinning data to Uint4 format
	for ( auto& chunk : m_header.m_renderChunkInfos )
	{
		GpuWrapApi::VertexPacking::PackingElement* skinningDataElement = nullptr;
		
#ifndef RED_CONFIGURATION_FINAL
		if( !chunk.m_chunkVertices.m_vertexLayout.IsValid() )
		{
			RED_LOG_ERROR( "Renderer: Invalid vertex layout. The mesh may be broken on some GPUs." );
			//RED_ASSERT( 0 && "Renderer: Invalid vertex layout. The mesh may be broken on some GPUs." );
		}
#endif
		
		for ( Uint32 i = 0; i < chunk.m_chunkVertices.m_vertexLayout.m_elements.Size(); ++i )
		{
			auto& element = chunk.m_chunkVertices.m_vertexLayout.m_elements[ i ];
			if ( element.m_usage == GpuWrapApi::VertexPacking::PS_InstanceSkinningData )
			{
				if ( element.m_type == GpuWrapApi::VertexPacking::PT_Float4 )
				{
					skinningDataElement = &element;
					break;
				}
			}
		}
		if ( skinningDataElement )
		{
			skinningDataElement->m_type = GpuWrapApi::VertexPacking::PT_UInt4;
			chunk.m_chunkVertices.m_vertexLayout.UpdateHash();
		}
	}
}

void rend::Chunk_DEPRECATED::SerializeVertexBufferChunk( IFile& file, VertexBufferChunk_DEPRECATED& b )
{
	RED_ASSERT( file.m_version >= (Uint32)RenderMeshBlob::RenderMeshBlobVersions::VER8_BufferChunkTypeReplacedWithVertexLayout, "Unsupported (old) version of mesh data." );

	// layout override is not set for any cooked mesh data, just when creating additional meshes at runtime, so we don't need to try to save that.
	for ( Uint32 i = 0; i < RENDER_MESH_NUM_STREAMS; ++i )
	{
		file << b.m_byteOffsets[ i ];
	}
	
	file << b.m_vertexLayout;
}

void rend::Chunk_DEPRECATED::SerializeIndexBufferChunk( IFile& file, IndexBufferChunk_DEPRECATED& b )
{
	RED_ASSERT( file.m_version >= (Uint32)RenderMeshBlob::RenderMeshBlobVersions::VER8_BufferChunkTypeReplacedWithVertexLayout, "Unsupported (old) version of mesh data." );

	// Buffer offset
	file << b.byteOffset;

	Uint8 type = b.type;
	file << type;
	b.type = ( GpuWrapApi::eIndexBufferChunkType )type;
}

void rend::Chunk_DEPRECATED::SerializeMaterialVertexFactory( IFile& file, Uint8& vertexFactory )
{
	Uint8 val = ( Uint32 )vertexFactory;
	file << val;
	vertexFactory = val;
}

void rend::operator<<( IFile& file, Chunk_DEPRECATED& chunk )
{
	// IMPORTANT: THIS FUNCTION SHOULD ONLY BE USED FOR READING OLD MESHES.
	RED_ASSERT( file.IsReader(), "Writing the old way!!! DEBUG!!!" );

	chunk.SerializeMaterialVertexFactory( file, chunk.m_vertexFactory );
	chunk.SerializeVertexBufferChunk( file, chunk.m_chunkVertices );
	chunk.SerializeIndexBufferChunk( file, chunk.m_chunkIndices );
	file << chunk.m_numVertices;
	file << chunk.m_numIndices;
	file << chunk.m_materialId;
	file << chunk.m_baseRenderMask;
	file << chunk.m_mergedRenderMask;
	file << chunk.m_lodMask;
}

//**************************************************************************************************************************//
//**************************************************************************************************************************//

