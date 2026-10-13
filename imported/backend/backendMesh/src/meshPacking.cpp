/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"

#include "meshPacking.h"
#include "../../../common/resourceMesh/include/mesh.h"
#include "../../../common/resourceMaterial/include/material.h"
#include "../../../common/resourceMaterial/include/materialDefinition.h"
#include "../../../common/resourceMaterial/include/materialTemplate.h"
#include "../../../common/resourceMesh/include/meshPacker.h"

#include "../../../common/engine/include/renderMeshBufferWriter.h"

#include "../../../common/renderData/include/gpuWrapApiVertexFormats.h"
#include "../../../common/redContainers/include/map.h"
#include "../../../common/resourceMesh/include/meshDataViewRO.h"

//////////////////////////////////////////////////////////////////////////
// usings
using red::AlignOffset;
using red::DynArray;
using red::Map;


namespace Helper
{

	static Uint64 ComputeBufferHash64( const GrowableMemoryBuffer& buffer )
	{
		return red::CalculateHash64( buffer.Data(), buffer.Size(), RED_FNV_OFFSET_BASIS64 );
	}

	// TODO: move to mesh utils ?

	static Bool IsSolidShadowMaterial( THandle< IMaterial > material )
	{
		// TODO: figure out if we want to use shadow meshes
		return true;
	}

	static Bool IsSolidShadowMaterial( const mesh::DataViewRO& view, const Int32 materialIndex )
	{
		const auto name = view.GetMaterialName(materialIndex);
		if ( name == RED_NAME_CONSTEXPR_NOREG( "volume" ) )
			return false;

		// generic material case
		const auto data = view.GetMaterialData(materialIndex);
		return IsSolidShadowMaterial(data);
	}

}

//------------------

CMeshPackingSource::PackedChunk::PackedChunk()
	: m_numVertices( 0 )
	, m_numIndices( 0 )
	, m_renderMask( (EMeshChunkFlags)0 )
	, m_lodMask( 1 )
	, m_layerOffset( 0 )
	, m_vertexFactory( 0 )
	, m_sourceChunks( red::PoolBackend() )
	, m_materialId( red::PoolBackend() )
{
}

//------------------

CMeshPackingSource::PackedStreams::PackedStreams()
{
	red::Memzero( &m_vertices, sizeof(m_vertices) );
	m_indices = nullptr;
}

CMeshPackingSource::PackedStreams::~PackedStreams()
{
}

//------------------

CMeshPackingSource::CMeshPackingSource()
	: m_vertices( red::PoolBackend() )
	, m_indices( red::PoolBackend() )
	, m_chunks( red::PoolBackend() )
	, m_lods( red::PoolBackend() )
{
}

CMeshPackingSource::~CMeshPackingSource()
{
	red::alg::ClearPtr( m_vertices );
	red::alg::ClearPtr( m_indices );

	m_chunks.Clear();
	m_lods.Clear();
}

CMeshPackingSource::PackedBuffer* CMeshPackingSource::CreateVertexBuffer( GrowableMemoryBuffer*& memory )
{
	if ( !memory )
	{
		return nullptr;
	}

	// compute CRC
	const Uint64 hash = Helper::ComputeBufferHash64( *memory );

	// already exists ?
	PackedBuffer* cachedBuffer = nullptr;
	for ( Uint32 i=0; i<m_vertices.Size(); ++i )
	{
		if ( m_vertices[i]->m_hash == hash )
		{
			// delete temporary shit
			RED_DELETE( memory );
			memory = nullptr;

			// return shared buffer
			return m_vertices[i];
		}
	}

	// create new buffer entry
	PackedBuffer* buffer = RED_NEW( PackedBuffer )();
	buffer->m_hash = hash;
	buffer->m_placement = 0;
	buffer->m_data = memory; // keep alive

	// insert
	m_vertices.PushBack( buffer );
	return buffer;
}

CMeshPackingSource::PackedBuffer* CMeshPackingSource::CreateIndexBuffer( GrowableMemoryBuffer*& memory )
{
	// compute CRC
	const Uint64 hash = Helper::ComputeBufferHash64( *memory );

	// already exists ?
	PackedBuffer* cachedBuffer = nullptr;
	for ( Uint32 i=0; i<m_indices.Size(); ++i )
	{
		if ( m_indices[i]->m_hash == hash )
		{
			// delete temporary shit
			RED_DELETE( memory );
			memory = nullptr;

			// return shared buffer
			return m_indices[i];
		}
	}

	// create new buffer entry
	PackedBuffer* buffer = RED_NEW( PackedBuffer )();
	buffer->m_hash = hash;
	buffer->m_placement = 0;
	buffer->m_data = memory; // keep alive

	// insert
	m_indices.PushBack( buffer );
	return buffer;
}

namespace helper
{
	// Descriptor of a mesh stream, defines destination property (pointer to member of "fat" SMeshVertex), stream type and number of components.
	template <class T>
	struct MeshStreamDesc
	{
		EMeshStreamType m_streamType;
		T SMeshVertex::* m_dstElement;
		Uint8 m_numComponents;
	};

	// Descriptors of all Float based "simple" mesh streams.
	// By "simple" mesh stream we mean a stream with data that is just an array of simple values, copied one by one from source to "fat" vertex property.
	// Note, that some float streams require more custom values placement in the destination "fat" vertex, i.e. MST_WindBranchData_4F etc.
	// They are not considered to be "simple", they are regarded as "complex" and are not listed here below. They are handled manually.
	static const MeshStreamDesc<Float> meshFloatStreamDescs[] =
	{
		{ MST_Position_3F,				reinterpret_cast<Float SMeshVertex::*>(&SMeshVertex::m_position),   3 },
		{ MST_Normal_3F,				reinterpret_cast<Float SMeshVertex::*>(&SMeshVertex::m_normal),     3 },
		{ MST_SkinningWeights_4F,		reinterpret_cast<Float SMeshVertex::*>(&SMeshVertex::m_weights),    4 },
		{ MST_SkinningWeightsExt_4F,	reinterpret_cast<Float SMeshVertex::*>(&SMeshVertex::m_weightsExt), 4 },
		{ MST_TexCoord0_2F,				reinterpret_cast<Float SMeshVertex::*>(&SMeshVertex::m_uv0),        2 },
		{ MST_TexCoord1_2F,				reinterpret_cast<Float SMeshVertex::*>(&SMeshVertex::m_uv1),        2 },
		{ MST_Tangent_3F,				reinterpret_cast<Float SMeshVertex::*>(&SMeshVertex::m_tangent),    3 },
		{ MST_Binormal_3F,				reinterpret_cast<Float SMeshVertex::*>(&SMeshVertex::m_binormal),   3 },
		{ MST_Multilayer_1F,			reinterpret_cast<Float SMeshVertex::*>(&SMeshVertex::m_multilayer), 1 },
		{ MST_LeafLod_1F,				reinterpret_cast<Float SMeshVertex::*>(&SMeshVertex::m_extraData0), 1 },
		{ MST_LodPosition_3F,			reinterpret_cast<Float SMeshVertex::*>(&SMeshVertex::m_extraData0), 3 },
		{ MST_LeafCardCorner_3F,		reinterpret_cast<Float SMeshVertex::*>(&SMeshVertex::m_extraData1), 3 },
		{ MST_LeafAnchors_3F,			reinterpret_cast<Float SMeshVertex::*>(&SMeshVertex::m_extraData1), 3 },
		{ MST_LeafWindData_4F,			reinterpret_cast<Float SMeshVertex::*>(&SMeshVertex::m_extraData2), 4 },
		{ MST_MorphOffset_3F,			reinterpret_cast<Float SMeshVertex::*>(&SMeshVertex::m_extraData0), 3 },
		{ MST_VehicleDmgNormalFront_3F,		reinterpret_cast<Float SMeshVertex::*>(&SMeshVertex::m_vehicleDamageNormal), 3 },
		{ MST_VehicleDmgNormalSides_3F,	reinterpret_cast<Float SMeshVertex::*>(&SMeshVertex::m_vehicleDmgNormalSides), 3 }, // obsolete
		{ MST_VehicleDmgPosFront_3F,	reinterpret_cast<Float SMeshVertex::*>(&SMeshVertex::m_vehicleDamagePosition), 3 },
		{ MST_VehicleDmgPosSides_3F,	reinterpret_cast<Float SMeshVertex::*>(&SMeshVertex::m_vehicleDmgPosSides), 3 }, // obsolete
		{ MST_MorphVertexData_3F,		reinterpret_cast<Float SMeshVertex::*>(&SMeshVertex::m_extraData1), 3 },
		{ MST_LightBlockerIntensity_1F,	reinterpret_cast<Float SMeshVertex::*>(&SMeshVertex::m_lightBlockerData), 1 },

	};

	// Descriptors of all Uint8 based "simple" mesh streams.
	static const MeshStreamDesc<Uint8> meshUint8StreamDescs[] =
	{
		{ MST_SkinningIndices_4U8,     reinterpret_cast<Uint8 SMeshVertex::*>(&SMeshVertex::m_indices),    4 },
		{ MST_SkinningIndicesExt_4U8,  reinterpret_cast<Uint8 SMeshVertex::*>(&SMeshVertex::m_indicesExt), 4 }
	};

	// Descriptors of all Uint32 based "simple" mesh streams.
	static const MeshStreamDesc<Uint32> meshUint32StreamDescs[] =
	{
		{ MST_Color_U32,               reinterpret_cast<Uint32 SMeshVertex::*>(&SMeshVertex::m_color),     1 }
	};

	static const Uint32 c_numMeshFloatStreamDescs = sizeof( meshFloatStreamDescs ) / sizeof( meshFloatStreamDescs[0] );
	static const Uint32 c_numMeshUint8StreamDescs = sizeof( meshUint8StreamDescs ) / sizeof( meshUint8StreamDescs[0] );
	static const Uint32 c_numMeshUint32StreamDescs = sizeof( meshUint32StreamDescs ) / sizeof( meshUint32StreamDescs[0] );

	// Copies stream element one-at-a-time (like Vector3 for MST_Position_3F).
	template <class T>
	struct MeshStreamIterator
	{
		const Uint8* m_data;
		const Uint8* m_dataEnd;
		T SMeshVertex::* m_dstElement;      //< Pointer to destination element within "fat" SMeshVertex structure.
		Uint32 m_stride;
		Uint8 m_numComponents;


		RED_INLINE void WriteElement( SMeshVertex* dstVertex )
		{
			if ( m_data >= m_dataEnd )
				return;                           // Old ExtractFatVertices allowed the streams to end prematurely, so I allow this also here.

			const T* srcData = reinterpret_cast<const T*>( m_data );
			T* dstData = &(dstVertex->*m_dstElement);
			for ( Uint32 i = 0; i < m_numComponents; ++i )
				dstData[i] = srcData[i];
			m_data += m_stride;
		}
	};

	// Scan all mesh streams in a given descriptors array, find the ones that are used (non empty) and prepare stream iterators for them.
	template <class T>
	Uint32 PrepareIteratorsForUsedMeshStreams( MeshStreamIterator<T>* dstIterators, const mesh::DataViewRO& meshView, const Uint32 chunkIndex,
	                                           const MeshStreamDesc<T>* meshStreamDescs, Uint32 numMeshStreamDescs )
	{
		Uint32 numUsedStreams = 0;
		for ( Uint32 i = 0; i < numMeshStreamDescs; ++i )
		{
			const MeshStreamDesc<T>& streamDesc = meshStreamDescs[i];
			mesh::ChunkStreamDataRO stream = meshView.GetChunkStreamData( chunkIndex, streamDesc.m_streamType );
			if ( !stream.Empty() )
			{
				MeshStreamIterator<T>& streamIter = dstIterators[numUsedStreams++];
				streamIter.m_data = reinterpret_cast<const Uint8*>(stream.m_data);
				streamIter.m_dataEnd = streamIter.m_data + (stream.m_stride * stream.m_count);
				streamIter.m_dstElement = streamDesc.m_dstElement;
				streamIter.m_numComponents = streamDesc.m_numComponents;
				streamIter.m_stride = stream.m_stride;
			}
		}
		return numUsedStreams;
	}

	// "Fat" vertex extractor, extracts vertices one-by-one from mesh chunk streams to "fat" SMeshVertex objects.
	class FatVertexExtractor
	{
	public:
		FatVertexExtractor( const mesh::DataViewRO& view, const Uint32 chunkIndex );

		void ExtractVertex( SMeshVertex* outVertex );

	private:
		Uint32 numUsedFloatStreams;
		Uint32 numUsedUint8Streams;
		Uint32 numUsedUint32Streams;
		Bool hasComplexElements;
		MeshStreamIterator<Float> usedFloatStreams[c_numMeshFloatStreamDescs];
		MeshStreamIterator<Uint8> usedUint8Streams[c_numMeshUint8StreamDescs];
		MeshStreamIterator<Uint32> usedUint32Streams[c_numMeshUint32StreamDescs];
		mesh::StreamIteratorRO<Uint32> destructionIndsStreamIter;
		mesh::StreamIteratorRO<Vector4> windBranchDataStreamIter;
		mesh::StreamIteratorRO<Float> branchDataStreamIter;
		mesh::StreamIteratorRO<Vector4> frondDataStreamIter;
		mesh::StreamIteratorRO<Int16> boneIdStreamIter;
	};

	FatVertexExtractor::FatVertexExtractor( const mesh::DataViewRO& view, const Uint32 chunkIndex )
	{
		numUsedFloatStreams = PrepareIteratorsForUsedMeshStreams( usedFloatStreams, view, chunkIndex, meshFloatStreamDescs, c_numMeshFloatStreamDescs );
		numUsedUint8Streams = PrepareIteratorsForUsedMeshStreams( usedUint8Streams, view, chunkIndex, meshUint8StreamDescs, c_numMeshUint8StreamDescs );
		numUsedUint32Streams = PrepareIteratorsForUsedMeshStreams( usedUint32Streams, view, chunkIndex, meshUint32StreamDescs, c_numMeshUint32StreamDescs );
		destructionIndsStreamIter = view.GetChunkStreamData( chunkIndex, MST_DestructionIndices_2U16 );
		windBranchDataStreamIter = view.GetChunkStreamData( chunkIndex, MST_WindBranchData_4F );
		branchDataStreamIter = view.GetChunkStreamData( chunkIndex, MST_BranchData_7F );
		frondDataStreamIter = view.GetChunkStreamData( chunkIndex, MST_FrondData_4F );
		boneIdStreamIter = view.GetChunkStreamData( chunkIndex, MST_FoliageBoneId_I16 );

		hasComplexElements = destructionIndsStreamIter.IsValid() | windBranchDataStreamIter.IsValid()
		                   | branchDataStreamIter.IsValid() | frondDataStreamIter.IsValid()
		                   | boneIdStreamIter.IsValid();
	}

	void FatVertexExtractor::ExtractVertex( SMeshVertex* outVertex )
	{
		// Write all "simple" elements (Float, Uint8, Uint32).
		for ( MeshStreamIterator<Float>* streamIter = usedFloatStreams; streamIter != usedFloatStreams + numUsedFloatStreams; ++streamIter )
			streamIter->WriteElement( outVertex );

		for ( MeshStreamIterator<Uint8>* streamIter = usedUint8Streams; streamIter != usedUint8Streams + numUsedUint8Streams; ++streamIter )
			streamIter->WriteElement( outVertex );

		for ( MeshStreamIterator<Uint32>* streamIter = usedUint32Streams; streamIter != usedUint32Streams + numUsedUint32Streams; ++streamIter )
			streamIter->WriteElement( outVertex );

		if ( hasComplexElements )     // Optimization, most meshes do not use that.
		{
			// Write "complex" elements, the ones that cannot be handled by generic MeshStreamIterator code above.
			if ( destructionIndsStreamIter )     // This tests both, if the stream is present, and if it already ended.
			{
				outVertex->m_destruction[0] = (*destructionIndsStreamIter) >> 16;
				outVertex->m_destruction[1] = (*destructionIndsStreamIter) & 0xffff;
				++destructionIndsStreamIter;
			}

			if ( windBranchDataStreamIter )
			{
				outVertex->m_uv1[0] = (*windBranchDataStreamIter)[0];
				outVertex->m_uv1[1] = (*windBranchDataStreamIter)[1];
				outVertex->m_extraData0[3] = (*windBranchDataStreamIter)[2];
				outVertex->m_extraData1[3] = (*windBranchDataStreamIter)[3];
				++windBranchDataStreamIter;
			}

			if ( branchDataStreamIter )
			{
				const Float* srcData = &branchDataStreamIter.Get();
				outVertex->m_extraData1[0] = srcData[0];
				outVertex->m_extraData1[1] = srcData[1];
				outVertex->m_extraData2[0] = srcData[2];
				outVertex->m_extraData2[1] = srcData[3];
				outVertex->m_extraData2[2] = srcData[4];
				outVertex->m_extraData1[2] = srcData[5];
				outVertex->m_extraData2[3] = srcData[6];
				++branchDataStreamIter;
			}

			if ( frondDataStreamIter )
			{
				outVertex->m_extraData1[0] = (*frondDataStreamIter)[0];
				outVertex->m_extraData1[1] = (*frondDataStreamIter)[1];
				outVertex->m_extraData1[2] = (*frondDataStreamIter)[2];
				outVertex->m_extraData2[0] = (*frondDataStreamIter)[3];
				++frondDataStreamIter;
			}

			if ( boneIdStreamIter )
			{
				outVertex->m_extraData0[0] = static_cast<float>(*boneIdStreamIter);
				++boneIdStreamIter;
			}
		}
	}

	static void ExtractFatIndices(const mesh::DataViewRO& view, const Uint32 chunkIndex, red::DynArray< Uint16 >& outIndies)
	{
		const auto numIndices = view.GetChunkIndexCount(chunkIndex);
		outIndies.Resize(numIndices);

		Uint16* writePtr = outIndies.TypedData();
		for (mesh::StreamIteratorRO<Uint16> it(view.GetChunkStreamData(chunkIndex, MST_Index_U16)); it; ++it, ++writePtr)
		{
			*writePtr = *it;
		}
	}

} // helper

Bool CMeshPackingSource::Pack( const CMesh* meshData, Uint32& outVertexDataSize, Uint32& outIndexDataSize, Uint32& outTopologyDataSize, Uint32& outTopologyMetadataSize, const Box* boundingBoxOverride /*=nullptr*/, Bool verbose /*=true*/ )
{
	PC_SCOPE( CMeshPackingSource_Pack );
	const mesh::DataViewRO view(*meshData);

	// unpack mesh data
	if (view.GetNumChunks() == 0)
		return false;

	const Uint32 numLODs = view.GetNumLODs();
	const Uint32 numChunks = view.GetNumChunks();
	const Uint32 numMaterials = view.GetNumMaterials();

	// prepare memory
	m_lods.Reserve( numLODs );
	m_chunks.Reserve( numChunks*2 ); // we may have shadow chunks

	// Prepare LOD stuff
	for (Uint32 i = 0; i < numLODs; ++i)
	{
		PackedLod newLod;
		newLod.m_distance = view.GetLODDistance(i);
		m_lods.PushBack( newLod );
	}

	// Pack data into render mesh chunks
	const auto& sourceChunks = meshData->DEPRECATED_GetChunks();
	DynArray< Uint32 > shadowChunksToMerge{ red::PoolBackend() };
	Uint32 maxNumVertexElements = 0;
	for ( Uint32 chunkIndex = 0; chunkIndex<sourceChunks.Size(); ++chunkIndex )
	{
		const auto& sourceChunk = sourceChunks[chunkIndex];

		Bool skipPacking = false;
		const EMeshChunkFlags renderMask = sourceChunk.m_chunkRenderMask;

		// Compute new render mask
		if ( skipPacking || renderMask == 0 )
			continue; // chunk is not going to be renderer on it's own, it's always merged

		// Copy obvious data to chunk
		PackedChunk& newChunk = m_chunks.EmplaceBack();
		newChunk.m_materialId = sourceChunk.m_materialID;
		newChunk.m_renderMask = renderMask;
		newChunk.m_lodMask = view.GetChunkLodMask(chunkIndex);
		newChunk.m_numIndices = (sourceChunk.m_chunkRenderMask & MCF_IsTwoSided) ? 2 * sourceChunk.m_numIndices : sourceChunk.m_numIndices;
		newChunk.m_numVertices = sourceChunk.m_numVertices;

		// Setup vertex packing
		newChunk.m_vertexFactory = (Uint8)MeshUtilities::GetVertexFactoryForMeshChunk( sourceChunk );

		newChunk.m_vertexPacking = MeshUtilities::GetVertexPackingForVertexType( sourceChunk.m_vertexType );

		maxNumVertexElements = Max( maxNumVertexElements, GpuWrapApi::VertexPacking::GetElementsCount( newChunk.m_vertexPacking.TypedData() ) );

		// Only one chunk for normal geometry
		SourceChunk sourceChunkInfo;
		sourceChunkInfo.m_baseVertexIndex = 0;
		sourceChunkInfo.m_chunkIndex = chunkIndex;
		newChunk.m_sourceChunks.PushBack( sourceChunkInfo );
	}

	// Find first non-masked material
	CName nonMaskedMaterial;
	for ( Uint32 i=0; i<numMaterials; ++i )
	{
		if ( Helper::IsSolidShadowMaterial( view, i ) )
		{
			nonMaskedMaterial = view.GetMaterialName(i);
			break;
		}
	}

	// stats
	if ( verbose )
	{
		RED_LOG( "Engine: Build %d candidate chunks, %d LODs from %d chunks in mesh %hs",
			m_chunks.Size(), numLODs, numChunks, meshData->GetFriendlyName().AsChar() );
	}

	// initialize GLOBAL packing parameters for the whole mesh packing
	GpuWrapApi::VertexPacking::PackingVertexLayout vertexLayout = MeshUtilities::GetVertexLayout();

	// Determine packing params ( quantization extents )
	GpuWrapApi::VertexPacking::PackingParams packingParams;
	const Box& boundingBox = boundingBoxOverride != nullptr ? *boundingBoxOverride : meshData->GetBoundingBox();
	GpuWrapApi::VertexPacking::InitPackingParams( 
		packingParams,
		boundingBox.Min.X, boundingBox.Min.Y, boundingBox.Min.Z,
		boundingBox.Max.X, boundingBox.Max.Y, boundingBox.Max.Z );

	// start packing, try not to pack something that we already have
	red::DynArray< GpuWrapApi::VertexPacking::PackingElement > filteredElements{ red::PoolBackend() };
	filteredElements.Reserve( maxNumVertexElements * 2u );                         // x2 - making room for stream end guardians.
	Uint32 initialPackedVertexSize = 0;
	Uint32 initialPackedIndexSize = 0;

	for ( PackedChunk& chunk : m_chunks )
	{
		// Get number of streams in the packer
		const Uint32 numStreams = Min( (Uint32)RENDER_MESH_NUM_STREAMS, GpuWrapApi::VertexPacking::GetPerVertexStreamCount( chunk.m_vertexPacking.TypedData() ) );

		// Calculate packing sizes
		Uint32 vertexSizes[ RENDER_MESH_NUM_STREAMS ] = { 0 };
		GpuWrapApi::VertexPacking::CalcElementListSizes( chunk.m_vertexPacking, vertexSizes, RENDER_MESH_NUM_STREAMS );

		// Create packing data
		GrowableMemoryBuffer* packedVertexData[ RENDER_MESH_NUM_STREAMS ] = { nullptr };
		GrowableMemoryBuffer* packedIndexData = RED_NEW( GrowableMemoryBuffer )( );

		// Pack chunks (each of the source chunks)
		for ( Uint32 chunkIndex = 0; chunkIndex < chunk.m_sourceChunks.Size(); ++chunkIndex )
		{
			// Get first source chunk
			const Uint32 sourceChunkIndex = chunk.m_sourceChunks[ chunkIndex ].m_chunkIndex;
			const Uint32 sourceChunkVertexOffset = chunk.m_sourceChunks[ chunkIndex ].m_baseVertexIndex;
			const auto& sourceChunk = sourceChunks[ sourceChunkIndex ];
			filteredElements.Clear();

			// Pack vertices
			// TODO: HACK, this will be replaced by proper mesh packer in the next step
			{
				CRenderMeshBufferWriter writers[ RENDER_MESH_NUM_STREAMS ];
				GpuWrapApi::VertexPacking::IBufferWriter* writerPtrs[ RENDER_MESH_NUM_STREAMS ];

				for ( Uint32 streamIndex = 0; streamIndex < numStreams; ++streamIndex )
				{
					if ( vertexSizes[ streamIndex ] && !packedVertexData[ streamIndex ] )
					{
						packedVertexData[ streamIndex ] = RED_NEW( GrowableMemoryBuffer )( );

						// Filter elements by streamIndex, so we don't iterate over elements unused in this stream in PackVertices/PackVertex.
						GpuWrapApi::VertexPacking::FilterElementsBySlotIndex( filteredElements, chunk.m_vertexPacking.TypedData(), streamIndex );

						const Uint32 verifyoffset = packedVertexData[ streamIndex ]->Size() / vertexSizes[ streamIndex ];
						RED_FATAL_ASSERT( verifyoffset == sourceChunkVertexOffset, "Vertex packing offset mismatch: %d != %d", verifyoffset, sourceChunkVertexOffset );

						writers[ streamIndex ] = CRenderMeshBufferWriter( *packedVertexData[ streamIndex ] );
						writerPtrs[ streamIndex ] = &writers[ streamIndex ];
					}
				}

				Uint32 numVertices = view.GetChunkVertexCount( sourceChunkIndex );
				helper::FatVertexExtractor vertexExtractor( view, sourceChunkIndex );

				SMeshVertex fatVertex;
				red::Memset( &fatVertex, 0, sizeof( fatVertex ) );     // We assume the streams will have the same amount of vertices, so there is no need to fill up data with defaults more than once.

				if ( chunk.m_vertexFactory == MVF_Terrain )
				{
					// Optimized terrain path
					RED_ASSERT( numStreams == 1, "It's required that terrain is position only." );
					mesh::ChunkStreamDataRO stream = view.GetChunkStreamData( sourceChunkIndex, MST_Position_3F );
					const Float* srcReadPtr = ( reinterpret_cast<const Float*>( stream.m_data ) );
					for ( Uint32 i = 0; i < numVertices; ++i )
					{
						GpuWrapApi::VertexPacking::PackVertexPosOnlyFast( writerPtrs[ 0 ], packingParams, srcReadPtr );
						srcReadPtr += 3;
					}
				}
				else
				{
					for ( Uint32 i = 0; i < numVertices; ++i )
					{
						// Extract one vertex from mesh chunk streams.
						vertexExtractor.ExtractVertex( &fatVertex );

						// Pack vertex into multiple output render data streams.
						GpuWrapApi::VertexPacking::PackVertexMultiStream( writerPtrs, numStreams, filteredElements.TypedData(), packingParams, vertexLayout, &fatVertex );
					}
				}

				for ( Uint32 streamIndex = 0; streamIndex < numStreams; ++streamIndex )
					initialPackedVertexSize += writers[ streamIndex ].GetOffset();
			}

			// Pack indices
			{
				CRenderMeshBufferWriter writer( *packedIndexData );
				if ( chunk.m_vertexFactory == MVF_Terrain )
				{
					// We don't want "fat" when live editing the terrain. Will just memcopy the indices as we know they're all 16 bit.
					const mesh::ChunkStreamDataRO chunkIndicesStreamRO = view.GetChunkStreamData( sourceChunkIndex, MST_Index_U16 );
					writer.WriteUnsignedShorts( (Uint16*)chunkIndicesStreamRO.m_buffer.Data(), chunkIndicesStreamRO.m_count );
				}
				else
				{
					// Regular, flexible way of packing indices
					red::DynArray< Uint16 > fatIndices{ red::PoolBackend() };
					helper::ExtractFatIndices( view, sourceChunkIndex, fatIndices );

					Uint32 indexCount = fatIndices.Size();
					red::DynArray< Uint16 > sideIndices( indexCount, red::PoolBackend() );
					red::Memcpy( sideIndices.Data(), fatIndices.Data(), fatIndices.DataSize() );

					if ( ( sourceChunk.m_chunkRenderMask & MCF_IsTwoSided ) && (indexCount > 0) )
					{
						sideIndices.Grow( indexCount );
						for ( Uint32 j = 0; j < indexCount - 2; j += 3 )
						{
							sideIndices[ j + indexCount ] = fatIndices[ j ];
							sideIndices[ j + indexCount + 1 ] = fatIndices[ j + 2 ];
							sideIndices[ j + indexCount + 2 ] = fatIndices[ j + 1 ];
						}
						indexCount *= 2;
					}

					GpuWrapApi::VertexPacking::PackIndices( &writer, GpuWrapApi::VertexPacking::PT_Index16, sideIndices.TypedData(), indexCount, sourceChunkVertexOffset );
				}
				initialPackedIndexSize += writer.GetOffset();
			}
		}

		// Create final buffers
		for ( Uint32 streamIndex = 0; streamIndex < numStreams; ++streamIndex )
		{
			chunk.m_streams.m_vertices[ streamIndex ] = CreateVertexBuffer( packedVertexData[ streamIndex ] );
		}

		// Create final index buffer
		chunk.m_streams.m_indices = CreateIndexBuffer( packedIndexData );
	}

	// compute data layout
	ComputeLayout( 16, 4, outVertexDataSize, outIndexDataSize );

	if ( verbose )
	{
		// final post merge stats
		RED_LOG( "Engine: Initial data size: %1.2fKB vertex, %1.2fKB index", 
			initialPackedVertexSize / 1024.0f, initialPackedIndexSize / 1024.0f );

		RED_LOG( "Engine: Final data size: %1.2fKB vertex, %1.2fKB index (%d vertex buffers, %d index buffers)", 
			outVertexDataSize / 1024.0f, outIndexDataSize / 1024.0f, 
			m_vertices.Size(), m_indices.Size() );
	}

	// packed
	return true;
}

void CMeshPackingSource::ComputeLayout( const Uint32 vertexDataAlignment, const Uint32 indexDataAlignment, Uint32& outVertexDataSize, Uint32& outIndexDataSize )
{
	outVertexDataSize = 0;
	outIndexDataSize = 0;

	for ( PackedBuffer* buffer : m_vertices )
	{
		buffer->m_placement = (Uint32) AlignOffset( outVertexDataSize, vertexDataAlignment );
		outVertexDataSize = buffer->m_placement + buffer->m_data->Size();
	}

	for ( PackedBuffer* buffer : m_indices )
	{
		buffer->m_placement = (Uint32) AlignOffset( outIndexDataSize, indexDataAlignment );
		outIndexDataSize = buffer->m_placement + buffer->m_data->Size();
	}
}

void CMeshPackingSource::CopyVertexData( void* outputMemory, const Uint32 bufferSize ) const
{
	for ( PackedBuffer* buffer : m_vertices )
	{
		Uint8* targetData = (Uint8*) outputMemory + buffer->m_placement;
		const Uint32 sizeLeft = bufferSize - buffer->m_placement;
		red::Memcpy( targetData, buffer->m_data->Data(), std::min( sizeLeft, buffer->m_data->Size() ) );
	}
}

void CMeshPackingSource::CopyIndexData( void* outputMemory, const Uint32 bufferSize  ) const
{
	for ( PackedBuffer* buffer : m_indices )
	{
		Uint8* targetData = (Uint8*) outputMemory + buffer->m_placement;
		const Uint32 sizeLeft = bufferSize - buffer->m_placement;
		red::Memcpy( targetData, buffer->m_data->Data(), std::min( sizeLeft, buffer->m_data->Size() ) );
	}
}

void CMeshPackingSource::CopyTopologyData(void* outputMemory, const Uint32 bufferSize) const
{
	for ( PackedBuffer* buffer : m_topologyData )
	{
		Uint8* targetData = (Uint8*)outputMemory + buffer->m_placement;
		const Uint32 sizeLeft = bufferSize - buffer->m_placement;
		red::Memcpy(targetData, buffer->m_data->Data(), std::min(sizeLeft, buffer->m_data->Size()));
	}
}

void CMeshPackingSource::CopyTopologyMetadata(void* outputMemory, const Uint32 bufferSize) const
{
	for ( PackedBuffer* buffer : m_topologyMetadata )
	{
		Uint8* targetData = (Uint8*)outputMemory + buffer->m_placement;
		const Uint32 sizeLeft = bufferSize - buffer->m_placement;
		red::Memcpy(targetData, buffer->m_data->Data(), std::min(sizeLeft, buffer->m_data->Size()));
	}
}