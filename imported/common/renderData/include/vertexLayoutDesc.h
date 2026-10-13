/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "gpuWrapApiVertexPacking.h"

namespace GpuWrapApi
{
	struct RENDER_DATA_API VertexLayoutDesc
	{
		RTTI_DECLARE_TYPE( VertexLayoutDesc );

		static const Uint8				invalidOverride = (Uint8)-1;
		static const Uint32				invalidHash		= (Uint32)-1;

		typedef red::StaticArray< VertexPacking::PackingElement, GpuApi::VERTEX_LAYOUT_MAX_ELEMENTS > TPackingElementArray;
		typedef red::StaticArray< Uint8, GpuApi::VERTEX_LAYOUT_MAX_SLOTS > TSlotStridesArray;

		TPackingElementArray			m_elements;
		TSlotStridesArray				m_slotStrides;
		Uint32							m_slotMask;		// Bitmask of which slots have elements (non-zero stride)
		Uint32							m_hash;			// Hash of this layout, for comparison with other layouts
		static_assert( GpuApi::VERTEX_LAYOUT_MAX_ELEMENTS <= 32, "Slot mask may need to be bigger" );

		VertexLayoutDesc()
		{
			ClearElements();
		}

		VertexLayoutDesc( const VertexPacking::PackingElement* inElements )
		{
			RED_ASSERT( m_elements.Empty(), "Only init this way if it is empty." );
			Uint32 elemIndex = 0;
			while ( elemIndex < GpuApi::VERTEX_LAYOUT_MAX_ELEMENTS && !inElements[ elemIndex ].IsEmpty() )
			{
				m_elements.PushBack( inElements[ elemIndex ] );
				++elemIndex;
			}

			UpdateStrides();
			UpdateHash();
		}

		VertexLayoutDesc( const red::ArraySpan<const VertexPacking::PackingElement>& elements )
		{
			ClearElements();
			AddElements( elements );
		}

		VertexLayoutDesc( const VertexLayoutDesc& other )
		{
			ClearElements();
			AddElements( other.m_elements );
		}

		Bool operator==( const VertexLayoutDesc &other ) const
		{
			if ( m_elements != other.m_elements )
			{
				return false;
			}
			return true;
		}

		Bool operator!=( const VertexLayoutDesc &other ) const
		{
			return !operator==( other );
		}

		VertexLayoutDesc& operator=( const VertexLayoutDesc& other )
		{
			if ( this == &other )
			{
				return *this;
			}

			ClearElements();
			AddElements( other.m_elements );

			return *this;
		}

		// Clear elements
		RED_INLINE void ClearElements()
		{
			m_slotMask = 0;
			m_elements.Clear();
			m_slotStrides.Clear();
			m_hash = invalidHash;
		}

		/// Update cached strides. If you're only using AddElements to fill the Desc, this is not required to be manually called. If
		/// you're filling m_elements directly, call this once it is filled, before creating a layout.
		void UpdateStrides();

		/// Add elements from newElements until an empty PackingElement is found, or GpuApi::VERTEX_LAYOUT_MAX_ELEMENTS elements.
		/// If slotOverride != -1, it and slotTypeOverride will be used to replace any slot/slotType info in the input elements.
		Bool AddElements( const red::ArraySpan<const VertexPacking::PackingElement>& newElements, Uint8 slotOverride = invalidOverride, GpuWrapApi::VertexPacking::EStreamType slotTypeOverride = GpuWrapApi::VertexPacking::ST_Invalid );

		/// Get offset of an element within its slot. -1 if no matching element is found.
		Int32 GetElementOffset( const VertexPacking::PackingElement& element ) const;
		Int32 GetUsageOffset( GpuWrapApi::VertexPacking::ePackingUsage usage, Uint8 usageIndex ) const;
		/// -1 if usage not found.
		Int32 FindHighestIndexForUsage( VertexPacking::ePackingUsage usage ) const;
		/// Get the byte stride of the given slot. 0 if slot not used.

		Uint8 GetSlotStride( Uint32 slot ) const
		{
			RED_ASSERT( slot < m_slotStrides.Size(), "Asking for a stride of unused vertex stream." );
			return m_slotStrides[slot];
		}

		void UpdateHash();

		RED_INLINE Uint32 GetHash() const
		{
			RED_ASSERT( m_hash != invalidHash, "VertexLayoutDesc hash is invalid, UpdateHash was not called?" );
			return m_hash;
		}

#ifndef RED_CONFIGURATION_FINAL
		bool IsValid() const;
#endif

		// Friendly serialization operator
		static void SerializePackingElement_DEPRECATED( IFile& file, VertexPacking::PackingElement& packingElement )
		{
			RED_ASSERT( file.IsReader(), "Writing the old way!!! DEBUG!!!" );

			// #TODO: figure this casting thing out, a few people had the assert failing with
			// error C2666: 'operator ==': 18 overloads have similar conversions
			// buildmachine had no problems with it
			static_assert( (Int8)VertexPacking::PT_Max == (Int8)31, "ePackingType enum changed" );
			static_assert( (Int8)VertexPacking::PS_Max == (Int8)24, "ePackingUsage enum changed" );
			static_assert( (Int8)VertexPacking::ST_Max == (Int8)2, "eSlotType enum changed" );

			Uint32 type = packingElement.m_type;
			file << type;
			packingElement.m_type = (VertexPacking::ePackingType)type;

			Uint32 usage = packingElement.m_usage;
			file << usage;
			packingElement.m_usage = (VertexPacking::ePackingUsage)usage;

			file << packingElement.m_usageIndex;
			file << packingElement.m_streamIndex;

			Uint8 slotType = packingElement.m_streamType;
			file << slotType;
			packingElement.m_streamType = (VertexPacking::EStreamType)slotType;
		}

		// THIS OPERATOR IS DEPRECATED
		friend void operator<<( IFile& file, VertexLayoutDesc& vertexLayoutDesc )
		{
			// IMPORTANT: THIS FUNCTION SHOULD ONLY BE USED FOR READING OLD MESHES.
			RED_ASSERT( file.IsReader(), "Writing the old way!!! DEBUG!!!" );

			Uint32 elementsCount;
			file << elementsCount;
			file << vertexLayoutDesc.m_slotMask;

			vertexLayoutDesc.m_elements.Resize( elementsCount );

			for ( Uint32 i = 0; i < elementsCount; ++i )
			{
				SerializePackingElement_DEPRECATED( file, vertexLayoutDesc.m_elements[ i ] );
			}

			if ( file.IsReader() )
			{
				vertexLayoutDesc.UpdateStrides();
				vertexLayoutDesc.UpdateHash();
			}
		}
	};


} // end namespace GpuWrapApi