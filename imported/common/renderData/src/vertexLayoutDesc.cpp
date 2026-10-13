/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "vertexLayoutDesc.h"
#include "bufferChunkType.h"
#include "gpuWrapApiVertexFormats.h"
#include "../../gpuApi/include/gpuApiLimits.h"


RTTI_BEGIN_TYPE_IN_NAMESPACE( VertexLayoutDesc, GpuWrapApi );
	RTTI_PROPERTY( m_elements );
	RTTI_PROPERTY( m_slotStrides );
	RTTI_PROPERTY( m_slotMask );
	RTTI_PROPERTY( m_hash );
RTTI_END_TYPE();

RTTI_BEGIN_ENUM_IN_NAMESPACE( eIndexBufferChunkType, GpuWrapApi );
	RTTI_ENUM_OPTION( IBCT_IndexUInt );
	RTTI_ENUM_OPTION( IBCT_IndexUShort );
	RTTI_ENUM_OPTION( IBCT_Max );
RTTI_END_ENUM();



namespace GpuWrapApi
{
	void VertexLayoutDesc::UpdateStrides()
	{
		m_slotMask = 0;
		m_slotStrides.Resize( GpuApi::VERTEX_LAYOUT_MAX_SLOTS );
		red::Memset( m_slotStrides.TypedData(), 0, m_slotStrides.DataCapacity() );
		for ( Uint32 i = 0; i < m_elements.Size(); ++i )
		{
			if ( !m_elements[i].IsEmpty() )
			{
				const Uint8 slotIndex = m_elements[i].m_streamIndex;
				RED_ASSERT( m_slotStrides[slotIndex] <= 239, "Storing slot stride in a byte to save memory, so we can't exceed 255.");	// Largest attribute is 16 bytes, so make sure we're at no more than 239 before adding to avoid overflow.
				m_slotStrides[slotIndex] += GpuWrapApi::VertexPacking::GetPackingTypeSize( m_elements[i].m_type );

				m_slotMask |= ( 1 << slotIndex );
			}
		}
	}

	Bool VertexLayoutDesc::AddElements( const red::ArraySpan<const VertexPacking::PackingElement>& newElements, Uint8 slotOverride, GpuWrapApi::VertexPacking::EStreamType slotTypeOverride )
	{
		if ( newElements.Empty() )
		{
			return true;
		}
		RED_ASSERT( slotOverride < GpuApi::VERTEX_LAYOUT_MAX_SLOTS || slotOverride == invalidOverride, "Slot override out of range: %d", slotOverride );

		// Count how many we're adding.
		const Uint32 numToAdd = newElements.Size();

		// Check if we're going to overflow.
		RED_ASSERT( m_elements.Size() + numToAdd <= m_elements.Capacity(), "Too many PackingElements in one VertexLayoutDesc!" );

		for ( Uint32 i = 0; i < numToAdd; ++i )
		{
			PackingElement& newlyAdded = m_elements.EmplaceBack();
			newlyAdded = newElements[ i ];

			if ( slotOverride != invalidOverride )
			{
				newlyAdded.m_streamIndex = slotOverride;
				newlyAdded.m_streamType = slotTypeOverride;
			}
		
			RED_ASSERT( newlyAdded.m_streamIndex < GpuApi::VERTEX_LAYOUT_MAX_SLOTS, "Stream index out of range!" );
		}

		UpdateStrides();
		UpdateHash();

		return true;
	}


	Int32 VertexLayoutDesc::GetElementOffset( const GpuWrapApi::VertexPacking::PackingElement& element ) const
	{
		Int32 offset = 0;
		for ( Uint32 i = 0; i < GpuApi::VERTEX_LAYOUT_MAX_ELEMENTS; ++i )
		{
			if ( m_elements[i].IsEmpty() )
			{
				break;
			}

			if ( m_elements[i].m_streamIndex == element.m_streamIndex )
			{
				if ( m_elements[i] == element )
				{
					return offset;
				}

				offset += GpuWrapApi::VertexPacking::GetPackingTypeSize( m_elements[i].m_type );
			}
		}

		return -1;
	}

	Int32 VertexLayoutDesc::GetUsageOffset( GpuWrapApi::VertexPacking::ePackingUsage usage, Uint8 usageIndex ) const
	{
		// Not optimal, but it's simple and shouldn't really need to be used in speed-critical places.
		for ( Uint32 i = 0; i < GpuApi::VERTEX_LAYOUT_MAX_ELEMENTS; ++i )
		{
			if ( m_elements[i].IsEmpty() )
			{
				break;
			}

			if ( m_elements[i].m_usage == usage && m_elements[i].m_usageIndex == usageIndex )
			{
				return GetElementOffset( m_elements[i] );
			}
		}

		return -1;
	}

	Int32 VertexLayoutDesc::FindHighestIndexForUsage( GpuWrapApi::VertexPacking::ePackingUsage usage ) const
	{
		Int32 highestIndex = -1;
		for ( Uint32 i = 0; i < GpuApi::VERTEX_LAYOUT_MAX_ELEMENTS; ++i )
		{
			if ( m_elements[i].IsEmpty() )
			{
				break;
			}

			if ( m_elements[i].m_usage == usage && m_elements[i].m_usageIndex > highestIndex )
			{
				highestIndex = m_elements[i].m_usageIndex;
			}
		}

		return highestIndex;
	}

	void VertexLayoutDesc::UpdateHash()
	{
		const Uint32 elemCount = m_elements.Size();
		GpuWrapApi::VertexPacking::PackingElement tempElements[ GpuApi::VERTEX_LAYOUT_MAX_ELEMENTS ];
		red::Memcpy( tempElements, m_elements.TypedData(), m_elements.DataSize() );
		for ( size_t i = m_elements.Size(); i < GpuApi::VERTEX_LAYOUT_MAX_ELEMENTS; ++i )
		{
			tempElements[ i ] = GpuWrapApi::VertexPacking::PackingElement::END_OF_ELEMENTS;
		}
		
		m_hash = RED_FNV_OFFSET_BASIS32;
		for ( Uint32 i = 0; i < elemCount; ++i )
		{
			m_hash = red::CalculateHash32( &tempElements[ i ], sizeof( GpuWrapApi::VertexPacking::PackingElement ), m_hash );
		}
	}

#ifndef RED_CONFIGURATION_FINAL
	bool VertexLayoutDesc::IsValid() const
	{
		Uint32 slotBiggestElementSizeBytes[ GpuApi::VERTEX_LAYOUT_MAX_SLOTS ] = {};
		for( auto elem : m_elements )
		{
			if( elem.IsEmpty() )
			{
				break;
			}

			const Uint32 elementSizeBytes = VertexPacking::GetPackingTypeSize( elem.m_type );
			slotBiggestElementSizeBytes[ elem.m_streamIndex ] = std::max(
				slotBiggestElementSizeBytes[ elem.m_streamIndex ], elementSizeBytes );
		}
		for( Uint32 slot = 0; slot < GpuApi::VERTEX_LAYOUT_MAX_SLOTS; ++slot )
		{
			if( slotBiggestElementSizeBytes[ slot ] > 0 )
			{
				// See Direct3D 11 specification chapter 4.4.6 Element Alignment
				// https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm#4.4.6%20Element%20Alignment
				
				const Uint32 requiredAlignment = std::min( slotBiggestElementSizeBytes[ slot ], 4u );
				if( m_slotStrides[ slot ] % requiredAlignment != 0 )
				{
					return false;
				}
			}
		}
		return true;
	}
#endif // #ifndef RED_CONFIGURATION_FINAL

} // end namespace GpuWrapApi
