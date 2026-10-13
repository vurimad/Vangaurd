/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "gpuWrapApiVertexPacking.h"

RTTI_BEGIN_ENUM_IN_NAMESPACE( ePackingType, GpuWrapApi, VertexPacking );
	RTTI_ENUM_OPTION( PT_Invalid );
	RTTI_ENUM_OPTION( PT_Float1 );
	RTTI_ENUM_OPTION( PT_Float2 );
	RTTI_ENUM_OPTION( PT_Float3 );;
	RTTI_ENUM_OPTION( PT_Float4 );
	RTTI_ENUM_OPTION( PT_Float16_2 );
	RTTI_ENUM_OPTION( PT_Float16_4 );
	RTTI_ENUM_OPTION( PT_UShort1 );
	RTTI_ENUM_OPTION( PT_UShort2 );
	RTTI_ENUM_OPTION( PT_UShort4 );
	RTTI_ENUM_OPTION( PT_UShort4N );
	RTTI_ENUM_OPTION( PT_Short1 );
	RTTI_ENUM_OPTION( PT_Short2 );
	RTTI_ENUM_OPTION( PT_Short4 );
	RTTI_ENUM_OPTION( PT_Short4N );
	RTTI_ENUM_OPTION( PT_UInt1 );
	RTTI_ENUM_OPTION( PT_UInt2 );
	RTTI_ENUM_OPTION( PT_UInt3 );
	RTTI_ENUM_OPTION( PT_UInt4 );
	RTTI_ENUM_OPTION( PT_Int1 );
	RTTI_ENUM_OPTION( PT_Int2 );
	RTTI_ENUM_OPTION( PT_Int3 );
	RTTI_ENUM_OPTION( PT_Int4 );
	RTTI_ENUM_OPTION( PT_Color );
	RTTI_ENUM_OPTION( PT_UByte1 );
	RTTI_ENUM_OPTION( PT_UByte1F );
	RTTI_ENUM_OPTION( PT_UByte4 );
	RTTI_ENUM_OPTION( PT_UByte4N );
	RTTI_ENUM_OPTION( PT_Byte4N );
	RTTI_ENUM_OPTION( PT_Dec4 );
	RTTI_ENUM_OPTION( PT_Index16 );
	RTTI_ENUM_OPTION( PT_Index32 );
	RTTI_ENUM_OPTION( PT_Max );
RTTI_END_ENUM();

RTTI_BEGIN_ENUM_IN_NAMESPACE( ePackingUsage, GpuWrapApi, VertexPacking );
	RTTI_ENUM_OPTION( PS_Invalid ); 
	RTTI_ENUM_OPTION( PS_SysPosition );
	RTTI_ENUM_OPTION( PS_Position );
	RTTI_ENUM_OPTION( PS_Normal );
	RTTI_ENUM_OPTION( PS_Tangent );
	RTTI_ENUM_OPTION( PS_Binormal );
	RTTI_ENUM_OPTION( PS_TexCoord );
	RTTI_ENUM_OPTION( PS_Color ); 
	RTTI_ENUM_OPTION( PS_SkinIndices );
	RTTI_ENUM_OPTION( PS_SkinWeights );
	RTTI_ENUM_OPTION( PS_DestructionIndices );
	RTTI_ENUM_OPTION( PS_MultilayerPaint ); // obsolete, unused
	RTTI_ENUM_OPTION( PS_InstanceTransform );
	RTTI_ENUM_OPTION( PS_InstanceLODParams );
	RTTI_ENUM_OPTION( PS_InstanceSkinningData );
	RTTI_ENUM_OPTION( PS_PatchSize );
	RTTI_ENUM_OPTION( PS_PatchBias );
	RTTI_ENUM_OPTION( PS_ExtraData );
	RTTI_ENUM_OPTION( PS_VehicleDmgNormal );
	RTTI_ENUM_OPTION( PS_VehicleDmgPosition );
	RTTI_ENUM_OPTION( PS_PositionDelta );
	RTTI_ENUM_OPTION( PS_LightBlockerIntensity );
	RTTI_ENUM_OPTION( PS_BoneIndex );
	RTTI_ENUM_OPTION( PS_Padding );
	RTTI_ENUM_OPTION( PS_PatchOffset );
	RTTI_ENUM_OPTION( PS_Max );
RTTI_END_ENUM();

RTTI_BEGIN_ENUM_IN_NAMESPACE( EStreamType, GpuWrapApi, VertexPacking );
	RTTI_ENUM_OPTION( ST_Invalid );
	RTTI_ENUM_OPTION( ST_PerVertex );
	RTTI_ENUM_OPTION( ST_PerInstance );
	RTTI_ENUM_OPTION( ST_Max );
RTTI_END_ENUM();

RTTI_BEGIN_TYPE_IN_NAMESPACE( PackingElement, GpuWrapApi, VertexPacking );
	RTTI_PROPERTY( m_type );
	RTTI_PROPERTY( m_usage );
	RTTI_PROPERTY( m_usageIndex );
	RTTI_PROPERTY( m_streamIndex );
	RTTI_PROPERTY( m_streamType );
RTTI_END_TYPE();

namespace GpuWrapApi
{
	Float Dot3( Float x1, Float y1, Float z1, Float x2, Float y2, Float z2 )
	{
		return x1*x2 + y1*y2 + z1*z2;
	}

	void Cross( Float x1, Float y1, Float z1, Float x2, Float y2, Float z2, Float& x3, Float& y3, Float& z3 )
	{
		x3 = y1 * z2 - y2 * z1;
		y3 = x2 * z1 - x1 * z2;
		z3 = x1 * y2 - x2 * y1;
	}

	namespace VertexPacking
	{		
		const PackingElement PackingElement::END_OF_ELEMENTS = { VertexPacking::PT_Invalid, VertexPacking::PS_Invalid, 0, 0, VertexPacking::ST_Invalid };


		void IBufferWriter::WriteFloats( const Float* data, Uint32 numFloats )
		{
			for ( Uint32 i = 0; i < numFloats; ++i )
				WriteFloat( data[i] );
		}

		void IBufferWriter::WriteUnsignedBytes( const Uint8* data, Uint32 numBytes )
		{
			for ( Uint32 i = 0; i < numBytes; ++i )
				WriteUnsignedByte( data[i] );
		}

		void IBufferWriter::WriteUnsignedBytesN( const Float* data, Uint32 numFloats )
		{
			for ( Uint32 i = 0; i < numFloats; ++i )
				WriteUnsignedByteN( data[i] );
		}

		void IBufferWriter::WriteSignedBytesN( const Float* data, Uint32 numFloats )
		{
			for ( Uint32 i = 0; i < numFloats; ++i )
				WriteSignedByteN( data[i] );
		}

		void IBufferWriter::WriteUnsignedShorts( const Uint16* data, Uint32 numShorts )
		{
			for ( Uint32 i = 0; i < numShorts; ++i )
				WriteUnsignedShort( data[i] );
		}

		void IBufferWriter::WriteUnsignedShortsN( const Float* data, Uint32 numFloats )
		{
			for ( Uint32 i = 0; i < numFloats; ++i )
				WriteUnsignedShortN( data[i] );
		}

		void IBufferWriter::WriteSignedShortsN( const Float* data, Uint32 numFloats )
		{
			for ( Uint32 i = 0; i < numFloats; ++i )
				WriteSignedShortN( data[i] );
		}

		void IBufferWriter::WriteFloats16( const Float* data, Uint32 numFloats )
		{
			for ( Uint32 i = 0; i < numFloats; ++i )
				WriteFloat16( data[i] );
		}

		RED_INLINE Bool HasVertexStream( ePackingUsage usage, Uint8 usageIndex, const PackingVertexLayout& vertexLayout )
		{
			Uint32 i = usage * PackingVertexLayout::USAGE_INDEX_COUNT + usageIndex;
			ePackingType format = vertexLayout.m_streams[ i ].m_format;
			return format != PT_Invalid;
		}

		//! Get stream float
		RED_INLINE const Float* GetVertexStreamFloat( ePackingUsage usage, Uint8 usageIndex, const PackingVertexLayout& vertexLayout, const void* vertexData )
		{
			Uint32 i = usage * PackingVertexLayout::USAGE_INDEX_COUNT + usageIndex;
			ePackingType format = vertexLayout.m_streams[ i ].m_format;
			RED_UNUSED( format );
			RED_ASSERT( format == PT_Float1 || format == PT_Float2 || format == PT_Float3 || format == PT_Float4 );
			const Uint8* offset = (const Uint8*) vertexData + vertexLayout.m_streams[ i ].m_offset;
			return (const Float*) offset;
		}

		//! Get stream float
		RED_INLINE Float* GetVertexStreamFloatForWriting( ePackingUsage usage, Uint8 usageIndex, const PackingVertexLayout& vertexLayout, void* vertexData )
		{
			Uint32 i = usage * PackingVertexLayout::USAGE_INDEX_COUNT + usageIndex;
			ePackingType format = vertexLayout.m_streams[ i ].m_format;
			RED_UNUSED( format );
			RED_ASSERT( format == PT_Float1 || format == PT_Float2 || format == PT_Float3 || format == PT_Float4 );
			const Uint8* offset = (const Uint8*) vertexData + vertexLayout.m_streams[ i ].m_offset;
			return (Float*) offset;
		}

		//! Get stream byte
		RED_INLINE const Uint8* GetVertexStreamByte( ePackingUsage usage, Uint8 usageIndex, const PackingVertexLayout& vertexLayout, const void* vertexData )
		{
			Uint32 i = usage * PackingVertexLayout::USAGE_INDEX_COUNT + usageIndex;
			ePackingType format = vertexLayout.m_streams[ i ].m_format;
			RED_UNUSED( format );
			RED_ASSERT( format == PT_UByte4 || format == PT_UByte4N || format == PT_Color );
			const Uint8* offset = (const Uint8*) vertexData + vertexLayout.m_streams[ i ].m_offset;
			return offset;
		}

		//! Get stream byte
		RED_INLINE Uint8* GetVertexStreamByteForWriting( ePackingUsage usage, Uint8 usageIndex, const PackingVertexLayout& vertexLayout, void* vertexData )
		{
			Uint32 i = usage * PackingVertexLayout::USAGE_INDEX_COUNT + usageIndex;
			ePackingType format = vertexLayout.m_streams[ i ].m_format;
			RED_UNUSED( format );
			RED_ASSERT( format == PT_UByte4 || format == PT_UByte4N || format == PT_Color );
			Uint8* offset = (Uint8*) vertexData + vertexLayout.m_streams[ i ].m_offset;
			return offset;
		}

		//! Get stream short
		RED_INLINE const Uint16* GetVertexStreamShort( ePackingUsage usage, Uint8 usageIndex, const PackingVertexLayout& vertexLayout, const void* vertexData )
		{
			Uint32 i = usage * PackingVertexLayout::USAGE_INDEX_COUNT + usageIndex;
			ePackingType format = vertexLayout.m_streams[i].m_format;
			RED_UNUSED( format );
			RED_ASSERT( format == PT_UShort2 );
			const Uint8* offset = (const Uint8*)vertexData + vertexLayout.m_streams[i].m_offset;
			return (const Uint16*)offset;
		}

		//! Get stream short
		RED_INLINE Uint16* GetVertexStreamShortForWriting(ePackingUsage usage, Uint8 usageIndex, const PackingVertexLayout& vertexLayout, void* vertexData)
		{
			Uint32 i = usage * PackingVertexLayout::USAGE_INDEX_COUNT + usageIndex;
			ePackingType format = vertexLayout.m_streams[i].m_format;
			RED_UNUSED( format );
			RED_ASSERT( format == PT_UShort2 );
			Uint8* offset = (Uint8*)vertexData + vertexLayout.m_streams[i].m_offset;
			return (Uint16*)offset;
		}

		//! Pack position X
		RED_INLINE Float PackPositionX( const PackingParams& params, Float val )
		{
			Float v = (val - params.m_positionPackOffsetX) * params.m_positionPackScaleX;
			if ( v < 0.0f ) v = 0.0f;
			if ( v > 1.0f ) v = 1.0f;
			return v;
		}

		//! Pack position Y
		RED_INLINE Float PackPositionY( const PackingParams& params, Float val )
		{
			Float v = (val - params.m_positionPackOffsetY) * params.m_positionPackScaleY;
			if ( v < 0.0f ) v = 0.0f;
			if ( v > 1.0f ) v = 1.0f;
			return v;
		}

		//! Pack position Z
		RED_INLINE Float PackPositionZ( const PackingParams& params, Float val )
		{
			Float v = (val - params.m_positionPackOffsetZ) * params.m_positionPackScaleZ;
			if ( v < 0.0f ) v = 0.0f;
			if ( v > 1.0f ) v = 1.0f;
			return v;
		}

		//! Unpack position X
		RED_INLINE Float UnpackPositionX( const PackingParams& params, Float val )
		{
			return val / params.m_positionPackScaleX + params.m_positionPackOffsetX;
		}

		//! Unpack position Y
		RED_INLINE Float UnpackPositionY( const PackingParams& params, Float val )
		{
			return val / params.m_positionPackScaleY + params.m_positionPackOffsetY;
		}

		//! Unpack position Z
		RED_INLINE Float UnpackPositionZ( const PackingParams& params, Float val )
		{
			return val / params.m_positionPackScaleZ + params.m_positionPackOffsetZ;
		}

		//! Initialize packing params from range of values
		void InitPackingParams( PackingParams& packParams, Float minX, Float minY, Float minZ, Float maxX, Float maxY, Float maxZ )
		{
			const Float rangeX = maxX - minX;
			const Float rangeY = maxY - minY;
			const Float rangeZ = maxZ - minZ;

			packParams.m_positionPackOffsetX = minX;
			packParams.m_positionPackOffsetY = minY;
			packParams.m_positionPackOffsetZ = minZ;
			packParams.m_positionPackScaleX = (rangeX > 0.0001f) ? ( 1.0f / rangeX ) : 0.0f;
			packParams.m_positionPackScaleY = (rangeY > 0.0001f) ? ( 1.0f / rangeY ) : 0.0f;
			packParams.m_positionPackScaleZ = (rangeZ > 0.0001f) ? ( 1.0f / rangeZ ) : 0.0f;
		}


		Uint32 GetPackingTypeSize( ePackingType type )
		{
			switch ( type )
			{
			case PT_Float1:		return 4;
			case PT_Float2:		return 8;
			case PT_Float3:		return 12;
			case PT_Float4:		return 16;
			case PT_Float16_2:	return 4;
			case PT_Float16_4:	return 8;
			case PT_Color:		return 4;
			case PT_UByte1:		return 1;
			case PT_UByte1F:	return 1;
			case PT_UByte4:		return 4;
			case PT_UByte4N:	return 4;
			case PT_Byte4N:		return 4;
			case PT_Dec4:		return 4;
			case PT_Short1:		return 2;
			case PT_Short2:		return 4;
			case PT_Short4:		return 8;
			case PT_Short4N:	return 8;
			case PT_UShort1:	return 2;
			case PT_UShort2:	return 4;
			case PT_UShort4:	return 8;
			case PT_UShort4N:	return 8;
			case PT_UInt1:		return 4;
			case PT_UInt2:		return 8;
			case PT_UInt3:		return 12;
			case PT_UInt4:		return 16;
			case PT_Int1:		return 4;
			case PT_Int2:		return 8;
			case PT_Int3:		return 12;
			case PT_Int4:		return 16;
			case PT_Index16:	return 2;
			case PT_Index32:	return 4;
			default:			RED_HALT( "Invalid element type" );
			}
			static_assert( PT_Max == 31, "ePackingType has changed. update here!" );

			return 0;
		}


		//! Calculate size of element list
		Uint32 CalcElementListSize( const PackingElement* elements )
		{
			Uint32 totalSize = 0;

			while ( elements && elements->m_type != PT_Invalid )
			{
				totalSize += GetPackingTypeSize( elements->m_type );
				++elements;
			}

			return totalSize;
		}

		//! Get number of elements in a set of packing elements.
		Uint32 GetElementsCount( const PackingElement* elements )
		{
			Uint32 numElements = 0;
			while ( elements && elements->m_type != PT_Invalid )
			{
				++elements;
				++numElements;
			}
			return numElements;
		}

		//! How many streams are used by these elements
		Uint32 GetStreamCount( const red::ArraySpan<const VertexPacking::PackingElement>& elements )
		{
			Int32 highestStream = -1;
			for ( const auto& element : elements )
			{
				if ( element.m_streamIndex > highestStream )
				{
					highestStream = element.m_streamIndex;
				}
			}
			return highestStream + 1;
		}

		//! How many per-vertex streams are used by these elements
		Uint32 GetPerVertexStreamCount( const PackingElement* elements )
		{
			Int32 highestStream = -1;
			while ( elements && elements->m_type != PT_Invalid )
			{
				if ( elements->m_streamType == ST_PerVertex && elements->m_streamIndex > highestStream )
				{
					highestStream = elements->m_streamIndex;
				}
				++elements;
			}
			return highestStream + 1;
		}

		//! Calculate sizes of element list
		void CalcElementListSizes( const red::ArraySpan<const VertexPacking::PackingElement>& elements, Uint32* sizes, Uint32 numSizes )
		{
			for ( const auto& element : elements )
			{
				// Make sure this element's slot isn't out of range. If it is, we won't write anything, but the caller probably needs to provide a bigger buffer...
				if ( element.m_type != PT_Invalid && element.m_streamIndex < numSizes )
				{
					sizes[element.m_streamIndex] += GetPackingTypeSize( element.m_type );
				}
			}
		}

		//! Pack vertex data from vertex stream
		void PackElement( IBufferWriter* writer, const PackingElement& element, const PackingParams& params, const PackingVertexLayout& vertexLayout, const void* vertexData )
		{
			// Don't pack per-instance data...?
			if ( element.m_streamType == ST_PerInstance )
				return;


			switch ( element.m_usage )
			{
			case PS_Position:
				{
					const Float* streamData = GetVertexStreamFloat( element.m_usage, element.m_usageIndex, vertexLayout, vertexData );
					if ( element.m_type == PT_Float3 )
					{
						writer->WriteFloats( streamData, 3 );
					}
					else if ( element.m_type == PT_Short4N )
					{
						Float temp[4];
						temp[0] = PackPositionX( params, streamData[0] ) * 2.0f - 1.0f;
						temp[1] = PackPositionY( params, streamData[1] ) * 2.0f - 1.0f;
						temp[2] = PackPositionZ( params, streamData[2] ) * 2.0f - 1.0f;
						temp[3] = 1.0f;
						writer->WriteSignedShortsN( temp, 4 );
					}
					else
					{
						RED_HALT( "Invalid packing format for POSITION" );
					}
					break;
				}
			case PS_PositionDelta:
				{
					const Float* streamData = GetVertexStreamFloat( element.m_usage, element.m_usageIndex, vertexLayout, vertexData );
					if ( element.m_type == PT_Dec4 )
					{
						const Float x = PackPositionX( params, streamData[ 0 ] );
						const Float y = PackPositionY( params, streamData[ 1 ] );
						const Float z = PackPositionZ( params, streamData[ 2 ] );
						writer->WriteDec3N( x, y, z );
					}
					else
					{
						RED_HALT( "Invalid packing format for PS_PositionDelta" );
					}
					break;
				}
			case PS_Normal:
			case PS_Tangent:
			case PS_Binormal:
				{
					const Float* streamData = GetVertexStreamFloat( element.m_usage, element.m_usageIndex, vertexLayout, vertexData );
					if ( element.m_type == PT_Float3 )
					{
						writer->WriteFloats( streamData, 3 );
					}
					else if ( element.m_type == PT_UByte4N )
					{
						writer->WriteSignedBytesN( streamData, 3 );
						writer->WriteSignedByteN( 1.0f );
					}
					else if ( element.m_type == PT_Dec4 )
					{
						if ( element.m_usage == PS_Tangent && HasVertexStream( PS_Binormal, 0, vertexLayout ) )
						{
							Float sign = 1.0f;
							const Float* streamDataNormal = GetVertexStreamFloat( PS_Normal, 0, vertexLayout, vertexData );
							const Float* streamDataBinormal = GetVertexStreamFloat( PS_Binormal, 0, vertexLayout, vertexData );

							Float cbx = 1.0f, cby = 0.0f, cbz = 0.0f;

							Cross( streamDataNormal[0], streamDataNormal[1], streamDataNormal[2], streamData[0], streamData[1], streamData[2], cbx, cby, cbz );

							if ( Dot3( cbx, cby, cbz, streamDataBinormal[0], streamDataBinormal[1], streamDataBinormal[2] ) < 0.0f )
							{
								sign = -1.0f;
							}

							writer->WriteDec4N( streamData[0] * .5f + .5f, streamData[1] * .5f + .5f, streamData[2] * .5f + .5f, sign*.5f + .5f );
						}
						else
						{
							writer->WriteDec3N( streamData[0] * .5f + .5f, streamData[1] * .5f + .5f, streamData[2] * .5f + .5f );
						}
					}
					else
					{
						RED_HALT( "Invalid packing format for NORMAL/TANGENT/BINORMAL" );
					}
					break;
				}

			case PS_TexCoord:
				{
					const Float* streamData = GetVertexStreamFloat( element.m_usage, element.m_usageIndex, vertexLayout, vertexData );
					if ( element.m_type == PT_Float1 )
					{
						writer->WriteFloat( streamData[0] );
					}
					else if ( element.m_type == PT_Float2 )					/// @todo DP: use switch or some array based num components & type deduction
					{
						writer->WriteFloats( streamData, 2 );
					}
					else if ( element.m_type == PT_Float3 )
					{
						writer->WriteFloats( streamData, 3 );
					}
					else if ( element.m_type == PT_Float4 )
					{
						writer->WriteFloats( streamData, 4 );
					}
					else if ( element.m_type == PT_Float16_2 )
					{
						writer->WriteFloats16( streamData, 2 );
					}
					else if ( element.m_type == PT_Float16_4 )
					{
						writer->WriteFloats16( streamData, 4 );
					}
					else
					{
						RED_HALT( "Invalid packing format for TEXCOORD" );
					}
					break;
				}

			case PS_Color:
				{
					const Uint8* streamData = GetVertexStreamByte( element.m_usage, element.m_usageIndex, vertexLayout, vertexData );
					if ( element.m_type == PT_Float4 )
					{
						Float temp[4];
						temp[0] = streamData[0] / 255.0f;
						temp[1] = streamData[1] / 255.0f;
						temp[2] = streamData[2] / 255.0f;
						temp[3] = streamData[3] / 255.0f;
						writer->WriteFloats( temp, 4 );
					}
					else if ( element.m_type == PT_Color || element.m_type == PT_UByte4 || element.m_type == PT_UByte4N )
					{
						writer->WriteColor( *(const Uint32*)streamData );
					}
					else
					{
						RED_HALT( "Invalid packing format for COLOR" );
					}
					break;
				}

			case PS_SkinIndices:
				{
					const Uint8* streamData = GetVertexStreamByte( element.m_usage, element.m_usageIndex, vertexLayout, vertexData );
					if ( element.m_type == PT_UByte4 )
					{
						writer->WriteUnsignedBytes( streamData, 4 );
					}
					else
					{
						RED_HALT( "Invalid packing format for BLENDINDICES" );
					}
					break;
				}

			case PS_SkinWeights:
				{
					const Float* streamData = GetVertexStreamFloat( element.m_usage, element.m_usageIndex, vertexLayout, vertexData );
					if ( element.m_type == PT_UByte4 || element.m_type == PT_UByte4N )
					{
						writer->WriteUnsignedBytesN( streamData, 4 );
					}
					else if ( element.m_type == PT_Float4 )
					{
						writer->WriteFloats( streamData, 4 );
					}
					else if ( element.m_type == PT_Float16_4 )
					{
						writer->WriteFloats16( streamData, 4 );
					}
					else
					{
						RED_HALT( "Invalid packing format for BLENDWEIGHTS" );
					}
					break;
				}
			case PS_BoneIndex:
			{
				const Uint8* streamData = GetVertexStreamByte( element.m_usage, element.m_usageIndex, vertexLayout, vertexData );
				if ( element.m_type == PT_UInt1 )
				{
					writer->WriteUnsignedInt( streamData[0] );
				}
				else
				{
					RED_HALT( "Invalid packing format for BONEINDEX" );
				}
				break;
			}
			case PS_DestructionIndices:
				{
					const Uint16* streamData = GetVertexStreamShort( element.m_usage, element.m_usageIndex, vertexLayout, vertexData );
					if ( element.m_type == PT_UShort2 )
					{
						writer->WriteUnsignedShorts( streamData, 2 );
					}
					else
					{
						RED_HALT( "Invalid packing format for DESTRGROUPS" );
					}
					break;
				}

			case PS_ExtraData:
				{
					const Float* streamData = GetVertexStreamFloat( element.m_usage, element.m_usageIndex, vertexLayout, vertexData );
					if ( element.m_type == PT_Float4 )
					{
						writer->WriteFloats( streamData, 4 );
					}
					else if ( element.m_type == PT_Float16_4 )					/// @todo DP: use switch or some array based num components deduction
					{
						writer->WriteFloats16( streamData, 4 );
					}
					else if ( element.m_type == PT_Float2 )
					{
						writer->WriteFloats( streamData, 2 );
					}
					else if ( element.m_type == PT_Float16_2 )
					{
						writer->WriteFloats16( streamData, 2 );
					}
					else
					{
						RED_HALT( "Invalid packing format for EXTRA DATA" );
					}
					break;
				}

			case PS_VehicleDmgNormal:
				{
					const Float* streamData = GetVertexStreamFloat( element.m_usage, element.m_usageIndex, vertexLayout, vertexData );
					if ( element.m_type == PT_Float3 )
					{
						writer->WriteFloats( streamData, 3 );
					}
					else if (element.m_type == PT_Dec4)
					{
						writer->WriteDec3N( streamData[0] * .5f + .5f, streamData[1] * .5f + .5f, streamData[2] * .5f + .5f );
					}
					else
					{
						RED_HALT( "Invalid packing format for VEHICLE_DMG_NORMAL" );
					}
					break;
				}
			case PS_VehicleDmgPosition:
				{
					const Float* streamData = GetVertexStreamFloat( element.m_usage, element.m_usageIndex, vertexLayout, vertexData );
					if ( element.m_type == PT_Float3 )
					{
						writer->WriteFloats( streamData, 3 );
					}
					else if ( element.m_type == PT_Float4 )
					{
						writer->WriteFloats( streamData, 4 );
					}
					else
					{
						RED_HALT( "Invalid packing format for VEHICLE_DMG_POS" );
					}
					break;
				}
			case PS_LightBlockerIntensity:
			{
				const Float* streamData = GetVertexStreamFloat(element.m_usage, element.m_usageIndex, vertexLayout, vertexData );
				if ( element.m_type == PT_Float1 )
				{
					writer->WriteFloat( streamData[ 0 ] );
				}
				else
				{
					RED_HALT("Invalid packing format for LIGHT_BLOCKER_INTENSITY");
				}
				break;
			}

			default:
				RED_HALT( "Invalid packing element" );
			}
		}

		void UnpackElement( IBufferReader* reader, const PackingElement& element, const PackingParams& params, const PackingVertexLayout& vertexLayout, void* vertexData )
		{
			// Don't pack per-instance data...?
			if ( element.m_streamType == ST_PerInstance )
				return;


			if ( element.m_usage == PS_Position )
			{
				Float* streamData = GetVertexStreamFloatForWriting( element.m_usage, element.m_usageIndex, vertexLayout, vertexData );
				if ( element.m_type == PT_Float3 )
				{
					reader->ReadFloat( streamData[0] );
					reader->ReadFloat( streamData[1] );
					reader->ReadFloat( streamData[2] );
				}
				else if ( element.m_type == PT_UShort4N )
				{
					Float x, y, z, w;
					reader->ReadUnsignedShortN( x );
					reader->ReadUnsignedShortN( y );
					reader->ReadUnsignedShortN( z );
					reader->ReadUnsignedShortN( w ); // ignored
					streamData[0] = UnpackPositionX( params, x );
					streamData[1] = UnpackPositionY( params, y );
					streamData[2] = UnpackPositionZ( params, z );
				}
				else if ( element.m_type == PT_Short4N )
				{
					Float x, y, z, w;
					reader->ReadSignedShortN( x );
					reader->ReadSignedShortN( y );
					reader->ReadSignedShortN( z );
					reader->ReadSignedShortN( w ); // ignored
					streamData[0] = UnpackPositionX( params, x * 0.5f + 0.5f );
					streamData[1] = UnpackPositionY( params, y * 0.5f + 0.5f );
					streamData[2] = UnpackPositionZ( params, z * 0.5f + 0.5f );
				}
				else
				{
					RED_HALT( "Invalid packing format for POSITION" );
				}
			}
			else if ( element.m_usage == PS_Normal || element.m_usage == PS_Tangent || element.m_usage == PS_Binormal )
			{
				Float* streamData = GetVertexStreamFloatForWriting( element.m_usage, element.m_usageIndex, vertexLayout, vertexData );
				if ( element.m_type == PT_Float3 )
				{
					reader->ReadFloat( streamData[0] );
					reader->ReadFloat( streamData[1] );
					reader->ReadFloat( streamData[2] );
				}
				else if ( element.m_type == PT_UByte4N )
				{
					Float dummy;
					reader->ReadSignedByteN( streamData[0] );
					reader->ReadSignedByteN( streamData[1] );
					reader->ReadSignedByteN( streamData[2] );
					reader->ReadSignedByteN( dummy );
				}
				else if ( element.m_type == PT_Dec4 )
				{
					if ( element.m_usage == PS_Tangent )
					{
						Float x, y, z, w;
 						reader->ReadDec4N( x, y, z, w );
						streamData[0] = ( x - .5f ) * 2.f;
						streamData[1] = ( y - .5f ) * 2.f;
						streamData[2] = ( z - .5f ) * 2.f;
					}
					else
					{
						Float x, y, z;
						reader->ReadDec3N( x, y, z );
						streamData[0] = ( x - .5f ) * 2.f;
						streamData[1] = ( y - .5f ) * 2.f;
						streamData[2] = ( z - .5f ) * 2.f;
					}
				}
				else
				{
					RED_HALT( "Invalid packing format for NORMAL/TANGENT/BINORMAL" );
				}
			}
			else if ( element.m_usage == PS_TexCoord )
			{
				Float* streamData = GetVertexStreamFloatForWriting( element.m_usage, element.m_usageIndex, vertexLayout, vertexData );
				if ( element.m_type == PT_Float1 )
				{
					reader->ReadFloat( streamData[0] );
				}
				else if ( element.m_type == PT_Float2 )
				{
					reader->ReadFloat( streamData[0] );
					reader->ReadFloat( streamData[1] );
				}
				else if ( element.m_type == PT_Float3 )
				{
					reader->ReadFloat( streamData[0] );
					reader->ReadFloat( streamData[1] );
					reader->ReadFloat( streamData[2] );
				}
				else if ( element.m_type == PT_Float4 )
				{
					reader->ReadFloat( streamData[0] );
					reader->ReadFloat( streamData[1] );
					reader->ReadFloat( streamData[2] );
					reader->ReadFloat( streamData[3] );
				}
				else if ( element.m_type == PT_Float16_2 )
				{
					reader->ReadFloat16( streamData[0] );
					reader->ReadFloat16( streamData[1] );
				}
				else if ( element.m_type == PT_Float16_4 )
				{
					reader->ReadFloat16( streamData[0] );
					reader->ReadFloat16( streamData[1] );
					reader->ReadFloat16( streamData[2] );
					reader->ReadFloat16( streamData[3] );
				}
				else
				{
					RED_HALT( "Invalid packing format for TEXCOORD" );
				}
			}
			else if ( element.m_usage == PS_Color )
			{
				Uint8* streamData = GetVertexStreamByteForWriting( element.m_usage, element.m_usageIndex, vertexLayout, vertexData );
				if ( element.m_type == PT_Float4 )
				{
					Float r, g, b, a;
					reader->ReadFloat( r );
					reader->ReadFloat( g );
					reader->ReadFloat( b );
					reader->ReadFloat( a );
					streamData[0] = (Uint8)( r * 255.f );
					streamData[1] = (Uint8)( g * 255.f );
					streamData[2] = (Uint8)( b * 255.f );
					streamData[3] = (Uint8)( a * 255.f );
				}
				else if ( element.m_type == PT_Color || element.m_type == PT_UByte4 || element.m_type == PT_UByte4N )
				{
					reader->ReadColor( *(Uint32*)streamData );
				}
				else
				{
					RED_HALT( "Invalid packing format for COLOR" );
				}
			}
			else if ( element.m_usage == PS_SkinIndices )
			{
				Uint8* streamData = GetVertexStreamByteForWriting( element.m_usage, element.m_usageIndex, vertexLayout, vertexData );
				if ( element.m_type == PT_UByte4 )
				{
					reader->ReadUnsignedByte( streamData[0] );
					reader->ReadUnsignedByte( streamData[1] );
					reader->ReadUnsignedByte( streamData[2] );
					reader->ReadUnsignedByte( streamData[3] );
				}
				else
				{
					RED_HALT( "Invalid packing format for BLENDINDICES" );
				}
			}
			else if( element.m_usage == PS_BoneIndex )
			{
				Uint8* streamData = GetVertexStreamByteForWriting( element.m_usage, element.m_usageIndex, vertexLayout, vertexData );
				if( element.m_type == PT_UInt1 )
				{
					Uint32 boneIndex = 0;
					reader->ReadUnsignedInt( boneIndex );
					streamData[ 0 ] = (Uint8)boneIndex;
				}
				else
				{
					RED_HALT( "Invalid packing format for BONEINDEX" );
				}
			}
			else if ( element.m_usage == PS_SkinWeights )
			{
				Float* streamData = GetVertexStreamFloatForWriting( element.m_usage, element.m_usageIndex, vertexLayout, vertexData );
				if ( element.m_type == PT_UByte4 || element.m_type == PT_UByte4N )
				{
					reader->ReadUnsignedByteN( streamData[0] );
					reader->ReadUnsignedByteN( streamData[1] );
					reader->ReadUnsignedByteN( streamData[2] );
					reader->ReadUnsignedByteN( streamData[3] );
				}
				else if ( element.m_type == PT_Float4 )
				{
					reader->ReadFloat( streamData[0] );
					reader->ReadFloat( streamData[1] );
					reader->ReadFloat( streamData[2] );
					reader->ReadFloat( streamData[3] );
				}
				else if ( element.m_type == PT_Float16_4 )
				{
					reader->ReadFloat16( streamData[0] );
					reader->ReadFloat16( streamData[1] );
					reader->ReadFloat16( streamData[2] );
					reader->ReadFloat16( streamData[3] );
				}
				else
				{
					RED_HALT( "Invalid packing format for BLENDWEIGHTS" );
				}
			}
			else if (element.m_usage == PS_DestructionIndices)
			{
				Uint16* streamData = (Uint16*)GetVertexStreamShortForWriting(element.m_usage, element.m_usageIndex, vertexLayout, vertexData);
				if (element.m_type == PT_UShort2)
				{
					reader->ReadUnsignedShort( streamData[0] );
					reader->ReadUnsignedShort( streamData[1] );
				}
				else
				{
					RED_HALT("Invalid packing format for DESTRGROUPS");
				}
			}
			else if( element.m_usage == PS_MultilayerPaint )
			{
				Float* streamData = GetVertexStreamFloatForWriting( element.m_usage, element.m_usageIndex, vertexLayout, vertexData );
				if( element.m_type == PT_Float1 )
				{
					reader->ReadFloat( streamData[ 0 ] );
				}
				else
				{
					RED_HALT( "Invalid packing format for LAYERPAINT" );
				}
			}
			else if ( element.m_usage == PS_ExtraData )
			{
				Float* streamData = GetVertexStreamFloatForWriting( element.m_usage, element.m_usageIndex, vertexLayout, vertexData );
				if ( element.m_type == PT_Float4 )
				{
					reader->ReadFloat( streamData[0] );
					reader->ReadFloat( streamData[1] );
					reader->ReadFloat( streamData[2] );
					reader->ReadFloat( streamData[3] );
				}
				else if ( element.m_type == PT_Float16_4 )
				{
					reader->ReadFloat16( streamData[0] );
					reader->ReadFloat16( streamData[1] );
					reader->ReadFloat16( streamData[2] );
					reader->ReadFloat16( streamData[3] );
				}
				else if ( element.m_type == PT_Float2 )
				{
					reader->ReadFloat( streamData[0] );
					reader->ReadFloat( streamData[1] );
				}
				else if ( element.m_type == PT_Float16_2 )
				{
					reader->ReadFloat16( streamData[0] );
					reader->ReadFloat16( streamData[1] );
				}
				else
				{
					RED_HALT( "Invalid packing format for EXTRA DATA" );
				}
			}
			else if ( element.m_usage == PS_VehicleDmgNormal )
			{
				Float* streamData = GetVertexStreamFloatForWriting( element.m_usage, element.m_usageIndex, vertexLayout, vertexData );
				if ( element.m_type == PT_Float3 )
				{
					reader->ReadFloat( streamData[0] );
					reader->ReadFloat( streamData[1] );
					reader->ReadFloat( streamData[2] );
				}
				else if ( element.m_type == PT_Dec4 )
				{
					Float x, y, z;
					reader->ReadDec3N( x, y, z );
					streamData[0] = ( x - .5f ) * 2.f;
					streamData[1] = ( y - .5f ) * 2.f;
					streamData[2] = ( z - .5f ) * 2.f;
				}
				else
				{
					RED_HALT( "Invalid packing format for VEHICLE_DMG_NORMAL" );
				}
			}
			else if ( element.m_usage == PS_VehicleDmgPosition )
			{
				Float* streamData = GetVertexStreamFloatForWriting( element.m_usage, element.m_usageIndex, vertexLayout, vertexData );
				if ( element.m_type == PT_Float3 )
				{
					reader->ReadFloat( streamData[0] );
					reader->ReadFloat( streamData[1] );
					reader->ReadFloat( streamData[2] );
				}
				else if ( element.m_type == PT_Float4 )
				{
					reader->ReadFloat( streamData[0] );
					reader->ReadFloat( streamData[1] );
					reader->ReadFloat( streamData[2] );
					reader->ReadFloat( streamData[3] );
				}
				else
				{
					RED_HALT( "Invalid packing format for VEHICLE_DMG_POS" );
				}
			}
			else if ( element.m_usage == PS_LightBlockerIntensity )
			{
				Float* streamData = GetVertexStreamFloatForWriting( element.m_usage, element.m_usageIndex, vertexLayout, vertexData );
				if ( element.m_type == PT_Float1 )
				{
					reader->ReadFloat( streamData[0] );
				}
				else
				{
					RED_HALT("Invalid packing format for LIGHT_BLOCKER_INTENSITY");
				}
			}
			else
			{
				RED_HALT( "Invalid packing element" );
			}
		}

		//! Pack single vertex into multiple stream buffers.
		void PackVertexMultiStream( IBufferWriter** streamWriters, Uint32 numStreams, const PackingElement* elements, const PackingParams& packParams, const PackingVertexLayout& vertexLayout, const void* vertexData )
		{
			RED_FATAL_ASSERT( elements != nullptr );
			RED_FATAL_ASSERT( numStreams != 0 );

			IBufferWriter* writer = *(streamWriters++);
			while ( true )
			{
				if ( elements->m_type == PT_Invalid )
				{
					if ( --numStreams == 0 )
						break;

					writer = *(streamWriters++);   // Move to next stream.
					++elements;                    // Skip separating invalid element.
					continue;
				}

				PackElement( writer, *elements, packParams, vertexLayout, vertexData );
				++elements;
			}
		}

		RENDER_DATA_API void PackVertexPosOnlyFast( IBufferWriter* streamWriter, const PackingParams& packParams, const Float* positions3F )
		{
			Float temp[ 4 ];
			temp[ 0 ] = PackPositionX( packParams, positions3F[ 0 ] ) * 2.0f - 1.0f;
			temp[ 1 ] = PackPositionY( packParams, positions3F[ 1 ] ) * 2.0f - 1.0f;
			temp[ 2 ] = PackPositionZ( packParams, positions3F[ 2 ] ) * 2.0f - 1.0f;
			temp[ 3 ] = 1.0f;
			streamWriter->WriteSignedShortsN( temp, 4 );
		}

		void UnpackVertices( IBufferReader* reader, const PackingElement* packingElement, const PackingParams& packParams, const PackingVertexLayout& vertexLayout, void* vertexData, Uint32 numVertices, Uint32 slotIndex )
		{
			RED_ASSERT( vertexLayout.m_stride != 0 );

			Uint8* vertexPtr = ( Uint8* ) vertexData;

			// Unpack vertices
			for ( Uint32 i=0; i<numVertices; ++i )
			{
				for ( auto elem = packingElement; elem && elem->m_type != VertexPacking::PT_Invalid; ++elem )
				{
					if ( elem->m_streamIndex == slotIndex )
					{
						UnpackElement( reader, *elem, packParams, vertexLayout, vertexPtr );
					}
				}

				// Move to next vertex
				vertexPtr += vertexLayout.m_stride;
			}
		}
	
		void FilterElementsBySlotIndex( red::DynArray<PackingElement>& outFilteredElements, const PackingElement* elements, Uint32 slotIndex )
		{
			const PackingElement* packElement = elements;
			if ( packElement != nullptr )
			{
				while ( packElement->m_type != PT_Invalid )
				{
					if ( packElement->m_streamIndex == slotIndex )
					{
						outFilteredElements.PushBack( *packElement );
					}
					++packElement;
				}
			}

			outFilteredElements.PushBack( PackingElement::END_OF_ELEMENTS );
		}

		//! Pack indices into buffer
		void PackIndices( IBufferWriter* writer, ePackingType type, const void* indexData, Uint32 numIndices, const Uint32 baseVertexIndex )
		{
			if ( type == PT_Index16 )
			{
				writer->Align( sizeof( Uint16 ) );

				const Uint16* indexPtr = ( const Uint16* ) indexData;
				for ( Uint32 i=0; i<numIndices; ++i )
				{
					RED_FATAL_ASSERT( (Uint32)*indexPtr + baseVertexIndex < 65536, "Index buffer indices were wrapper" );
					writer->WriteUnsignedShort( *indexPtr + baseVertexIndex );
					++indexPtr;
				}
			}
			else if ( type == PT_Index32 )
			{
				writer->Align( sizeof( Uint32 ) );

				const Uint32* indexPtr = ( const Uint32* ) indexData;
				for ( Uint32 i=0; i<numIndices; ++i )
				{
					RED_FATAL_ASSERT( (Uint32)*indexPtr + baseVertexIndex < 65536, "Index buffer indices were wrapper" );
					writer->WriteUnsignedInt( *indexPtr + baseVertexIndex );
					++indexPtr;
				}
			}
			else
			{
				RED_HALT( "Invalid index pack format" );
			}
		}
	}

	Bool MapPackingElementToSemanticAndIndex( EVertexPackingPlatform platform, const GpuWrapApi::VertexPacking::PackingElement& element, const AnsiChar*& outSemantic, Uint32& outIndex )
	{
		const AnsiChar* semanticName = nullptr;
		Uint32 semanticIndex = element.m_usageIndex;

		switch ( element.m_usage )
		{
		case GpuWrapApi::VertexPacking::PS_SysPosition:
			{
				switch ( platform )
				{
				case VERTPACKPLATFORM_NonOrbis:			semanticName = "SV_Position"; break;
				case VERTPACKPLATFORM_Orbis:			semanticName = "S_POSITION"; break;
				default:								RED_FATAL( "Invalid" );
				}
			}
			break;
		case GpuWrapApi::VertexPacking::PS_Position:				semanticName = "POSITION"; break;
		case GpuWrapApi::VertexPacking::PS_Normal:					semanticName = "NORMAL"; break;
		case GpuWrapApi::VertexPacking::PS_Tangent:					semanticName = "TANGENT"; break;
		case GpuWrapApi::VertexPacking::PS_Binormal:				semanticName = "BINORMAL"; break;
		case GpuWrapApi::VertexPacking::PS_TexCoord:				semanticName = "TEXCOORD"; break;
		case GpuWrapApi::VertexPacking::PS_Color:					semanticName = "COLOR"; break;
		case GpuWrapApi::VertexPacking::PS_SkinIndices:				semanticName = "BLENDINDICES"; break;
		case GpuWrapApi::VertexPacking::PS_SkinWeights:				semanticName = "BLENDWEIGHT"; break;
		case GpuWrapApi::VertexPacking::PS_DestructionIndices:		semanticName = "DESTRGROUPS"; break;
		case GpuWrapApi::VertexPacking::PS_MultilayerPaint:			semanticName = "LAYERPAINT"; break;
		case GpuWrapApi::VertexPacking::PS_InstanceTransform:		semanticName = "INSTANCE_TRANSFORM"; break;
		case GpuWrapApi::VertexPacking::PS_InstanceLODParams:		semanticName = "INSTANCE_LOD_PARAMS"; break;
		case GpuWrapApi::VertexPacking::PS_InstanceSkinningData:	semanticName = "INSTANCE_SKINNING_DATA"; break;
		case GpuWrapApi::VertexPacking::PS_VehicleDmgNormal:		semanticName = "VEHICLE_DMG_NORMAL"; break;
		case GpuWrapApi::VertexPacking::PS_VehicleDmgPosition:		semanticName = "VEHICLE_DMG_POS"; break;
		case GpuWrapApi::VertexPacking::PS_LightBlockerIntensity:	semanticName = "LIGHT_BLOCKER_INTENSITY"; break;
		case GpuWrapApi::VertexPacking::PS_BoneIndex:				semanticName = "BONEINDEX"; break;

		case GpuWrapApi::VertexPacking::PS_PatchSize:				semanticName = "TESS_PATCH_SIZE"; break;
		case GpuWrapApi::VertexPacking::PS_PatchBias:				semanticName = "TESS_PATCH_BIAS"; break;
		case GpuWrapApi::VertexPacking::PS_PatchOffset:				semanticName = "PATCH_OFFSET"; break;

		case GpuWrapApi::VertexPacking::PS_ExtraData:				semanticName = "EXTRA_DATA"; break;		

		case GpuWrapApi::VertexPacking::PS_Padding:				return false;

		default:
			RED_LOG_ERROR( "GpuApi: Unsupported VertexPacking usage" );
			return false;
		}

		outSemantic = semanticName;
		outIndex = semanticIndex;

		return true;
	}

}
