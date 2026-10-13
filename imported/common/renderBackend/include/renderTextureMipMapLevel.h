/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once
#include "../../redMemory/include/uniqueBuffer.h"

namespace tools
{
	/// Texture mipmap level
	struct MipMap
	{
		Uint32							m_width;			// Width of the mipmap level
		Uint32							m_height;			// Height of the mipmap level
		Uint32							m_depth;			// Depth of the mipmap level
		Uint32							m_pitch;			// Data buffer pitch
		red::UniqueBuffer				m_data;				// Data buffer.

		RED_FORCE_INLINE MipMap()
			: m_width( 0 )
			, m_height( 0 )
			, m_depth( 0 )
			, m_pitch( 0 )
		{}

		RED_FORCE_INLINE MipMap( MipMap &&other )
			: m_width( other.m_width )
			, m_height( other.m_height )
			, m_depth(other.m_depth)
			, m_pitch( other.m_pitch )
			, m_data( std::move( other.m_data ) )
		{}

		RED_FORCE_INLINE MipMap( Uint32 width, Uint32 height, Uint32 depth, Uint32 pitch )
			: m_width( width )
			, m_height( height )
			, m_depth( depth )
			, m_pitch( pitch )
		{
			m_data = red::CreateUniqueBuffer< rend::PoolTexture >( depth * height * pitch, 16 );
		}
	};
	// Mipmap array type
	typedef red::DynArray< MipMap > MipArray;
};