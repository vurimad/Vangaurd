#include "build.h"
#include "meshPacker.h"
#include "mesh.h"

#include "../../redMath/include/float16compressor.h"

#include "meshDataViewRO.h"

namespace MeshPackerUtils
{

Uint32 GetMeshStreamSize( EMeshStreamType streamType )
{
	switch ( streamType )
	{
		case MST_Position_3F:				return 3 * sizeof( Float );
		case MST_SkinningIndices_4U8:		return 4 * sizeof( Uint8 );
		case MST_SkinningWeights_4F:		return 4 * sizeof( Float );
		case MST_SkinningIndicesExt_4U8:	return 4 * sizeof( Uint8 );
		case MST_SkinningWeightsExt_4F:		return 4 * sizeof( Float );
		case MST_Color_U32:					return 1 * sizeof( Uint32 );
		case MST_TexCoord0_2F:				return 2 * sizeof( Float );
		case MST_TexCoord1_2F:				return 2 * sizeof( Float );
		case MST_Normal_3F:					return 3 * sizeof( Float );
		case MST_Tangent_3F:				return 3 * sizeof( Float );
		case MST_Binormal_3F:				return 3 * sizeof( Float );
		case MST_Index_U16:					return 1 * sizeof( Uint16 );
		case MST_DestructionIndices_2U16:	return 2 * sizeof( Uint16 );
		case MST_Multilayer_1F:				return 1 * sizeof( Float );
		case MST_WindBranchData_4F:			return 4 * sizeof( Float );
		case MST_LeafLod_1F:				return 1 * sizeof( Float );
		case MST_LeafCardCorner_3F:			return 3 * sizeof( Float );
		case MST_LodPosition_3F:			return 3 * sizeof( Float );
		case MST_BranchData_7F:				return 7 * sizeof( Float );
		case MST_FrondData_4F:				return 4 * sizeof( Float );
		case MST_LeafAnchors_3F:			return 3 * sizeof( Float );
		case MST_LeafWindData_4F:			return 4 * sizeof( Float );
		case MST_GarmentFlags_U32:			return 1 * sizeof( Uint32 );
		case MST_MorphOffset_3F:			return 3 * sizeof( Float );
		case MST_VehicleDmgNormalFront_3F:	return 3 * sizeof( Float );
		case MST_VehicleDmgNormalSides_3F:	return 3 * sizeof( Float );
		case MST_VehicleDmgPosFront_3F:		return 3 * sizeof( Float );
		case MST_VehicleDmgPosSides_3F:		return 3 * sizeof( Float );
		case MST_MorphVertexData_3F:		return 3 * sizeof( Float );
		case MST_FoliageBoneId_I16:			return 1 * sizeof( Int16 );
		case MST_LightBlockerIntensity_1F:	return 1 * sizeof( Float );
	}

	RED_FATAL( "Invalid stream type" );
	return 0;
}

Uint32 GetMeshRenderStreamSize( ERenderStreamType streamType )
{
	switch ( streamType )
	{
		case RST_Position_4US:				return 4 * sizeof( Uint16 );
		case RST_Position_4F:				return 4 * sizeof( Float );
		case RST_SkinningIndices_4U8N:		return 4 * sizeof( Uint8 );
		case RST_SkinningWeights_4U8:		return 4 * sizeof( Uint8 );
		case RST_SkinningIndicesExt_4U8N:	return 4 * sizeof( Uint8 );
		case RST_SkinningWeightsExt_4U8:	return 4 * sizeof( Uint8 );
		case RST_Normal_Dec4:				return 1 * sizeof( Uint32 );
		case RST_Tangent_Dec4:				return 1 * sizeof( Uint32 );
		case RST_Binormal_Dec4:				return 1 * sizeof( Uint32 );
		case RST_TexCoord0_2F16:			return 2 * sizeof( Uint16 );
		case RST_TexCoord1_2F16:			return 2 * sizeof( Uint16 );
		case RST_Color_U32:					return 1 * sizeof( Uint32 );
		case RST_Index_U16:					return 1 * sizeof( Uint16 );
		case RST_DestructionIndices_2U16:	return 2 * sizeof( Uint16 );
		case RST_VehicleDmgNormalFront_Dec4:return 1 * sizeof( Uint32 );
		case RST_VehicleDmgNormalSides_Dec4:return 1 * sizeof( Uint32 );
		case RST_VehicleDmgPosFront_4F:		return 4 * sizeof( Float );
		case RST_VehicleDmgPosSides_4F:		return 4 * sizeof( Float );
		case RST_LightBlockerIntensity_1F:	return 1 * sizeof( Float );
	}

	RED_FATAL( "Invalid stream type" );
	return 0;
}

Uint32 GetFatVertexElementOffset( EMeshStreamType streamType )											/// @todo DP: mesh.cpp already has such function, merge
{
	switch ( streamType )
	{
	case MST_Position_3F: return offsetof( SMeshVertex, m_position ); break;
	case MST_SkinningIndices_4U8: return offsetof( SMeshVertex, m_indices ); break;
	case MST_SkinningWeights_4F: return offsetof( SMeshVertex, m_weights ); break;
	case MST_SkinningIndicesExt_4U8: return offsetof( SMeshVertex, m_indicesExt ); break;
	case MST_SkinningWeightsExt_4F: return offsetof( SMeshVertex, m_weightsExt ); break;
	case MST_Color_U32: return offsetof( SMeshVertex, m_color ); break;
	case MST_TexCoord0_2F: return offsetof( SMeshVertex, m_uv0 ); break;
	case MST_TexCoord1_2F: return offsetof( SMeshVertex, m_uv1 ); break;
	case MST_Normal_3F: return offsetof( SMeshVertex, m_normal ); break;
	case MST_Tangent_3F: return offsetof( SMeshVertex, m_tangent ); break;
	case MST_Binormal_3F: return offsetof( SMeshVertex, m_binormal ); break;
	case MST_DestructionIndices_2U16: return offsetof( SMeshVertex, m_destruction ); break;
	case MST_Multilayer_1F: return offsetof(SMeshVertex, m_multilayer); break;
	case MST_VehicleDmgNormalFront_3F: return offsetof( SMeshVertex, m_vehicleDamageNormal ); break;
	case MST_VehicleDmgPosFront_3F: return offsetof( SMeshVertex, m_vehicleDamagePosition ); break;
	default: RED_FATAL( "No such stream" ); break;
	}

	return 0;
}

Uint32 MeshStreamTypeToIndex( EMeshStreamType streamType )
{
	Uint64 index = red::BitUtils::Log2( static_cast< Uint64 >( streamType ) );
	RED_FATAL_ASSERT( index < MAX_MESH_STREAMS, "Index out of bounds" );
	return ( Uint32 ) index;
}

}

static Uint32 CalculateInputStreamStride( const SMeshStreamPart& part )
{
	Uint32 size = 0;
	for ( const auto& streamMapping : part.m_streamMappings )
		size += MeshPackerUtils::GetMeshStreamSize( streamMapping.m_inputStreamType );
	return size;
}

static Uint32 CalculatOutputStreamStride( const SMeshStreamPart& part )
{
	Uint32 size = 0;
	for ( const auto& streamMapping : part.m_streamMappings )
		size += MeshPackerUtils::GetMeshRenderStreamSize( streamMapping.m_outputStreamType );
	return size;
}

static const SMeshStream* GetStreamFromChunk( const SMeshChunkPacked& chunk, EMeshStreamType streamType )
{
	for ( auto& stream : chunk.m_streams )
	{
		if ( stream.m_type == streamType )
		{
			return &stream;
		}
	}

	return nullptr;
}

static Float PackFloat( Float val, Float scale, Float offset )
{
	Float v = ( val - offset ) * scale;
	if ( v < 0.0f ) v = 0.0f;
	if ( v > 1.0f ) v = 1.0f;
	return v;
}

Uint8 ToUnsignedByteN( Float data )
{
	if ( data <= 0.0f )
	{
		return 0;
	}
	else if ( data >= 1.0f )
	{
		return 255;
	}
	else
	{
		return ( Uint8 ) ( data * 255.0f );
	}
}

static Uint16 ToUnsignedShortN( Float data )
{
	if ( data <= 0.0f )
	{
		return 0;
	}
	else if ( data >= 1.0f )
	{
		return std::numeric_limits< Uint16 >::max();
	}
	else
	{
		return ( Uint16 )( data * std::numeric_limits< Uint16 >::max() );
	}
}

static void PackStream4US( const serialization::BufferHandleRO& in, const SQuantizationParams& quantizationParams, DataBuffer* out )
{
	const math::Vector4 scale = quantizationParams.m_scale;
	const math::Vector4 offset = quantizationParams.m_offset;

	Uint32 inputCount = in.GetSize() / ( 3 * sizeof( Float ) );

	*out = DataBuffer( inputCount * 4 * sizeof( Uint16 ) );

	const Float* src = static_cast< const Float * >( in.Data() );
	Uint16* dst = static_cast< Uint16 * >( out->Data() );

	for ( Uint32 i = 0; i < inputCount; ++i )
	{
		dst[ 0 ] = ToUnsignedShortN( PackFloat( src[ 0 ], scale.X, offset.X ) );
		dst[ 1 ] = ToUnsignedShortN( PackFloat( src[ 1 ], scale.Y, offset.Y ) );
		dst[ 2 ] = ToUnsignedShortN( PackFloat( src[ 2 ], scale.Z, offset.Z ) );
		dst[ 3 ] = ToUnsignedShortN( 1.0f );

		src += 3;
		dst += 4;
	}
}

static void PackStream4F( const serialization::BufferHandleRO& in, DataBuffer* out )
{
	Uint32 inputCount = in.GetSize() / ( 3 * sizeof( Float ) );

	*out = DataBuffer( inputCount * 4 * sizeof( Float ) );

	const Float* src = static_cast< const Float * >( in.Data() );
	Float* dst = static_cast< Float * >( out->Data() );

	for ( Uint32 i = 0; i < inputCount; ++i )
	{
		dst[ 0 ] = src[ 0 ];
		dst[ 1 ] = src[ 1 ];
		dst[ 2 ] = src[ 2 ];
		dst[ 3 ] = 1.0f;

		src += 3;
		dst += 4;
	}
}

static void PackStreamDec3( const serialization::BufferHandleRO& in, DataBuffer* out )
{
	Uint32 inputCount = in.GetSize() / ( 3 * sizeof( Float ) );

	*out = DataBuffer( inputCount * 1 * sizeof( Uint32 ) );

	const Float* src = static_cast< const Float * >( in.Data() );
	Uint32* dst = static_cast< Uint32 * >( out->Data() );

	for ( Uint32 i = 0; i < inputCount; ++i )
	{
		Float cx = ::Clamp < Float >( src[ 0 ], 0.0f, 1.0f );
		Float cy = ::Clamp < Float >( src[ 1 ], 0.0f, 1.0f );
		Float cz = ::Clamp < Float >( src[ 2 ], 0.0f, 1.0f );

		Uint32 vx = ( Uint32 ) ( 1023.0f * cx );
		Uint32 vy = ( Uint32 ) ( 1023.0f * cy );
		Uint32 vz = ( Uint32 ) ( 1023.0f * cz );
		Uint32 vw = 1;

		Uint32 px = ( vx & 1023 );
		Uint32 py = ( vy & 1023 ) << 10;
		Uint32 pz = ( vz & 1023 ) << 20;
		Uint32 pw = ( vw & 3 ) << 30;

		*dst = px + py + pz + pw;

		src += 3;
		dst += 1;
	}
}

static void PackStreamDec4( const serialization::BufferHandleRO& in, DataBuffer* out )
{
	Uint32 inputCount = in.GetSize() / ( 4 * sizeof( Float ) );

	*out = DataBuffer( inputCount * 1 * sizeof( Uint32 ) );

	const Float* src = static_cast< const Float * >( in.Data() );
	Uint32* dst = static_cast< Uint32 * >( out->Data() );

	for ( Uint32 i = 0; i < inputCount; ++i )
	{
		Float cx = ::Clamp < Float >( src[ 0 ], 0.0f, 1.0f );
		Float cy = ::Clamp < Float >( src[ 1 ], 0.0f, 1.0f );
		Float cz = ::Clamp < Float >( src[ 2 ], 0.0f, 1.0f );
		Float cw = ::Clamp < Float >( src[ 3 ], 0.0f, 1.0f );
		
		Uint32 vx = ( Uint32 ) ( 1023.0f * cx );
		Uint32 vy = ( Uint32 ) ( 1023.0f * cy );
		Uint32 vz = ( Uint32 ) ( 1023.0f * cz );
		Uint32 vw = ( Uint32 ) ( 3.0f * cw );

		Uint32 px = ( vx & 1023 );
		Uint32 py = ( vy & 1023 ) << 10;
		Uint32 pz = ( vz & 1023 ) << 20;
		Uint32 pw = ( vw & 3 ) << 30;

		*dst = px + py + pz + pw;

		src += 4;
		dst += 1;
	}
}

static void PackStreamFloat16( const serialization::BufferHandleRO& in, DataBuffer* out )
{
	Uint32 inputCount = in.GetSize() / sizeof( Float );

	*out = DataBuffer( inputCount * sizeof( Uint16 ) );

	const Float* src = static_cast< const Float * >( in.Data() );
	Uint16* dst = static_cast< Uint16 * >( out->Data() );

	for ( Uint32 i = 0; i < inputCount; ++i )
	{
		*dst = Float16Compressor::Compress( *src );

		src += 1;
		dst += 1;
	}
}

static void PackStreamU8( const serialization::BufferHandleRO& in, DataBuffer* out )
{
	Uint32 inputCount = in.GetSize() / sizeof( Float );

	*out = DataBuffer( inputCount * sizeof( Uint8 ) );

	const Float* src = static_cast< const Float * >( in.Data() );
	Uint8* dst = static_cast< Uint8 * >( out->Data() );

	for ( Uint32 i = 0; i < inputCount; ++i )
	{
		*dst = ToUnsignedByteN( *src );

		src += 1;
		dst += 1;
	}
}

static void CopyStream( const serialization::BufferHandleRO& in, DataBuffer* out )
{
	*out = DataBuffer::Copy( in.Data(), in.GetSize() );
}

static void InterleaveStream( const void* input, Uint32 inputStrideBytes, Uint32 inputCount, void* output, Uint32 outputStrideBytes, Uint32 outputOffsetBytes )
{
	const Uint8* in = reinterpret_cast< const Uint8 * >( input );
	Uint8* out = reinterpret_cast< Uint8 * >( output );

	for ( Uint32 i = 0; i < inputCount; ++i )
	{
		// Move to the correct position inside of the output block
		out += outputOffsetBytes;

		// Copy whole input block
		red::Memcpy( out, in, inputStrideBytes );
		
		// Advance to next input and output block
		in += inputStrideBytes;
		out += outputStrideBytes;
	}
}

Bool CMeshPacker::Pack( const CMesh& mesh, SPackedMesh& outPackedMesh )
{
	if ( !ValidateMesh( mesh ) )
	{
		return false;
	}

	if ( !ProcessMesh( mesh, outPackedMesh ) )
	{
		return false;
	}

	return true;
}

Bool CMeshPacker::ValidateMesh( const CMesh& meshData )
{
	return true;
}

Uint32 CalculateTotalSize( const red::DynArray< DataBuffer >& buffers )
{
	Uint32 size = 0;
	for ( const auto& buffer : buffers )
	{
		size += buffer.Size();
	}
	return size;
}

Bool CMeshPacker::ProcessMesh( const CMesh& mesh, SPackedMesh& outPackedMesh )
{
#ifdef RED_CONFIGURATION_FINAL
	// no runtime packing in final game
	return false;
#else
	// Extract and store quantization base for the mesh
	m_quantizationParameters = SQuantizationParams( mesh.GetBoundingBox() );
	
	const auto& chunks = mesh.DEPRECATED_GetChunks();

	// Gather stream chunks
	red::DynArray< SStreamChunk > streamChunks{ red::PoolBackend() };
	for ( const auto& chunk : chunks )
	{
		ProcessChunk( chunk, streamChunks );
	}

	// Calculate final blob size
	Uint32 totalSize = 0;
	for ( const auto& streamChunk : streamChunks )
	{
		totalSize += CalculateTotalSize( streamChunk.m_data );
	}

	DataBuffer finalBlob{ totalSize };

	PackedMeshParts meshParts{ red::PoolBackend() };
	meshParts.Reserve( chunks.Size() );

	Uint32 blobOffset = 0;

	for ( Uint32 i = 0; i < streamChunks.Size(); ++i )
	{
		Uint32 streamSize = CalculateTotalSize( streamChunks[ i ].m_data );

		// Merge single streams into aggregate streams and write them to final blob
		MergeOutputStreamData( streamChunks[ i ].m_parts, streamChunks[ i ].m_data, static_cast< Uint8 * >( finalBlob.Data() ) + blobOffset );

		SPackedMeshPart packedMeshPart;
		packedMeshPart.m_size = streamSize;
		packedMeshPart.m_offset = blobOffset;
		
		for ( const auto& part : streamChunks[ i ].m_parts )
		{
			for ( const auto& mapping : part.m_streamMappings ) 
			{
				packedMeshPart.m_renderFormat |= mapping.m_outputStreamType;
			}
		}

		blobOffset += streamSize;
	}

	outPackedMesh.m_parts = meshParts;
	outPackedMesh.m_data = std::move( finalBlob );

	return true;
#endif // RED_CONFIGURATION_FINAL
}

void CMeshPacker::ProcessChunk( const SMeshChunkPacked& chunk, red::DynArray< SStreamChunk >& streamChunks )
{
	SStreamChunk streamChunk;

	// Calculate stream mapping for the chunk
	ComputeStreamMapping( chunk.m_streamMask, streamChunk.m_parts );

	// Pack chunk stream data
	PackOutputStreamData( chunk, streamChunk.m_parts, streamChunk.m_data );

	// Store mapping
	streamChunks.PushBack( std::move( streamChunk ) );
}

void CMeshPacker::ComputeStreamMapping( Uint64 meshStreamMask, MeshStreamParts& parts )
{
	bool quantizePosition = true;

	if ( meshStreamMask & MST_Position_3F )
	{
		SMeshStreamPart part;
		part.m_streamMappings.PushBack( SMeshStreamMapping( MST_Position_3F, quantizePosition ? RST_Position_4US : RST_Position_4F ) );
		parts.PushBack( part );
	}

	Uint64 tangentSpaceMask = MST_Normal_3F | MST_Tangent_3F;

	if ( ( meshStreamMask & tangentSpaceMask ) == tangentSpaceMask )
	{
		SMeshStreamPart part;
		part.m_streamMappings.PushBack( SMeshStreamMapping( MST_Normal_3F, RST_Normal_Dec4 ) );
		part.m_streamMappings.PushBack( SMeshStreamMapping( MST_Tangent_3F, RST_Tangent_Dec4 ) );

		if ( meshStreamMask & MST_Binormal_3F )
		{
			part.m_streamMappings.PushBack( SMeshStreamMapping( MST_Binormal_3F, RST_Binormal_Dec4 ) );
		}

		// TODO: Can we have texture coordinate stream without tangent basis?
		if ( meshStreamMask & MST_TexCoord0_2F )
		{
			part.m_streamMappings.PushBack( SMeshStreamMapping( MST_TexCoord0_2F, RST_TexCoord0_2F16 ) );
		}

		parts.PushBack( part );
	}

	if ( meshStreamMask & MST_Color_U32 )
	{
		SMeshStreamPart part;
		part.m_streamMappings.PushBack( SMeshStreamMapping( MST_Color_U32, RST_Color_U32 ) );

		if ( meshStreamMask & MST_TexCoord1_2F )
		{
			part.m_streamMappings.PushBack( SMeshStreamMapping( MST_TexCoord1_2F, RST_TexCoord1_2F16 ) );
		}

		parts.PushBack( part );
	}

	if ( meshStreamMask & MST_SkinningIndices_4U8 )
	{
		SMeshStreamPart part;
		part.m_streamMappings.PushBack( SMeshStreamMapping( MST_SkinningIndices_4U8, RST_SkinningIndices_4U8N ) );
		parts.PushBack( part );
	}

	if ( meshStreamMask & MST_SkinningWeights_4F )
	{
		SMeshStreamPart part;
		part.m_streamMappings.PushBack( SMeshStreamMapping( MST_SkinningWeights_4F, RST_SkinningWeights_4U8 ) );
		parts.PushBack( part );
	}

	if ( meshStreamMask & MST_SkinningIndicesExt_4U8 )
	{
		RED_WARNING( meshStreamMask & MST_SkinningIndices_4U8, "Have SkinningIndicesExt, but not SkinningIndices!" );
		SMeshStreamPart part;
		part.m_streamMappings.PushBack( SMeshStreamMapping( MST_SkinningIndicesExt_4U8, RST_SkinningIndicesExt_4U8N ) );
		parts.PushBack( part );
	}

	if ( meshStreamMask & MST_SkinningWeightsExt_4F )
	{
		RED_WARNING( meshStreamMask & MST_SkinningWeights_4F, "Have SkinningWeightsExt, but not SkinningWeights!" );
		SMeshStreamPart part;
		part.m_streamMappings.PushBack( SMeshStreamMapping( MST_SkinningWeightsExt_4F, RST_SkinningWeightsExt_4U8 ) );
		parts.PushBack( part );
	}

	if ( meshStreamMask & MST_DestructionIndices_2U16 )
	{
		SMeshStreamPart part;
		part.m_streamMappings.PushBack( SMeshStreamMapping( MST_DestructionIndices_2U16, RST_DestructionIndices_2U16 ) );
		parts.PushBack(part);
	}

	if (meshStreamMask & MST_Multilayer_1F)
	{
		SMeshStreamPart part;
		part.m_streamMappings.PushBack(SMeshStreamMapping(MST_Multilayer_1F, RST_Multilayer_1F));
		parts.PushBack(part);
	}

	if (meshStreamMask & MST_VehicleDmgNormalFront_3F)
	{
		SMeshStreamPart part;
		part.m_streamMappings.PushBack(SMeshStreamMapping(MST_VehicleDmgNormalFront_3F, RST_VehicleDmgNormalFront_Dec4));
		parts.PushBack(part);
	}

	if (meshStreamMask & MST_VehicleDmgNormalSides_3F)
	{
		SMeshStreamPart part;
		part.m_streamMappings.PushBack(SMeshStreamMapping(MST_VehicleDmgNormalSides_3F, RST_VehicleDmgNormalSides_Dec4));
		parts.PushBack(part);
	}

	if (meshStreamMask & MST_VehicleDmgPosFront_3F)
	{
		SMeshStreamPart part;
		part.m_streamMappings.PushBack(SMeshStreamMapping(MST_VehicleDmgPosFront_3F, RST_VehicleDmgPosFront_4F));
		parts.PushBack(part);
	}

	if (meshStreamMask & MST_VehicleDmgPosSides_3F)
	{
		SMeshStreamPart part;
		part.m_streamMappings.PushBack(SMeshStreamMapping(MST_VehicleDmgPosSides_3F, RST_VehicleDmgPosSides_4F));
		parts.PushBack(part);
	}

	if ( meshStreamMask & MST_LightBlockerIntensity_1F )
	{
		SMeshStreamPart part;
		part.m_streamMappings.PushBack( SMeshStreamMapping( MST_LightBlockerIntensity_1F, RST_LightBlockerIntensity_1F ) );
		parts.PushBack( part );
	}

	if ( meshStreamMask & MST_Index_U16 )
	{
		SMeshStreamPart part;
		part.m_streamMappings.PushBack( SMeshStreamMapping( MST_Index_U16, RST_Index_U16 ) );
		parts.PushBack( part );
	}
}

void CMeshPacker::PackSingleStream( const SMeshStream* stream, ERenderStreamType renderStreamType, DataBuffer* outBuffer )
{
	auto buffer = stream->m_data.Editor_LoadContentDirect();
	RED_FATAL_ASSERT( buffer.Data() );

	switch ( renderStreamType ) {
	case RST_Position_4US:
		PackStream4US( buffer, m_quantizationParameters, outBuffer );
		break;
	case RST_Position_4F:
	case RST_VehicleDmgPosFront_4F:
		PackStream4F( buffer, outBuffer );
		break;
	case RST_Normal_Dec4:
	case RST_Tangent_Dec4: // TODO: This will require more care
	case RST_Binormal_Dec4:
	case RST_VehicleDmgNormalFront_Dec4:
		PackStreamDec3( buffer, outBuffer );
		break;
	case RST_TexCoord0_2F16:
	case RST_TexCoord1_2F16:
		PackStreamFloat16( buffer, outBuffer );
		break;
	case RST_Color_U32:
	case RST_Index_U16:
	case RST_SkinningIndices_4U8N:
	case RST_SkinningIndicesExt_4U8N:
	case RST_DestructionIndices_2U16:
	case RST_Multilayer_1F:
	case RST_LightBlockerIntensity_1F:
		CopyStream( buffer, outBuffer );
		break;
	case RST_SkinningWeights_4U8:
	case RST_SkinningWeightsExt_4U8:
		PackStreamU8( buffer, outBuffer );
		break;
	}
}

void CMeshPacker::PackOutputStreamData( const SMeshChunkPacked& chunk, const MeshStreamParts& streamParts, red::DynArray< DataBuffer >& outputStreamData )
{
	for ( const auto& streamPart : streamParts )
	{
		for ( const auto& streamMapping : streamPart.m_streamMappings )
		{
			const SMeshStream* stream = GetStreamFromChunk( chunk, streamMapping.m_inputStreamType );
			
			// Convert a single stream from source format to packed format according to mapping			
			DataBuffer streamData;
			PackSingleStream( stream, streamMapping.m_outputStreamType, &streamData );
			
			outputStreamData.PushBack( std::move( streamData ) );
		}
	}
}

void CMeshPacker::MergeOutputStreamData( const MeshStreamParts& streamParts, const red::DynArray< DataBuffer >& outputStreamData, void* output )
{
	Uint32 k = 0;

	for ( Uint32 i = 0; i < streamParts.Size(); ++i )
	{
		const SMeshStreamPart& streamPart = streamParts[ i ];

		Uint32 outputOffset = 0;

		for ( Uint32 j = 0; j < streamPart.m_streamMappings.Size(); ++j )
		{
			const DataBuffer& streamData = outputStreamData[ k ];

			Uint32 srcSize = streamData.Size();
			Uint32 srcStride = CalculateInputStreamStride( streamPart );
			Uint32 dstStride = CalculatOutputStreamStride( streamPart );

			// Store how far inside of the output block, elements of this stream should
			// be interleaved
			outputOffset += dstStride;

			// Interleave selected streams into output buffer
			Uint32 inputCount = srcSize / srcStride;
			InterleaveStream( streamData.Data(), srcStride, inputCount, output, dstStride, outputOffset );

			k += 1;
		}
	}
}
