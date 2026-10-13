/**
* Copyright (C)2020 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "imageLoader.h"
#include "../../../common/redFileSystem/include/file.h"

namespace img
{
	// Allows image loader to load an image from memory.
	class MemoryReader final : public IFile // NOTE was: public img::BaseImageLoader::Reader
	{
	public:
		MemoryReader( const void* data, const Uint32 size )
			: IFile( FF_Reader | FF_MemoryBased )
			, m_data( static_cast< const Uint8* >( data ) )
			, m_size( size )
			, m_offset( 0 )
		{}

		virtual Uint64 GetOffset() const override
		{
			return m_offset;
		}

		virtual Uint64 GetSize() const override
		{
			return m_size;
		}

		virtual void Seek( const Int64 offset ) override
		{
			m_offset = offset;
		}

		virtual void Serialize( void* outData, const size_t size ) override
		{
			RED_FATAL_ASSERT( m_offset + size <= m_size, "Out of bound read" );
			red::Memcpy( outData, m_data + m_offset, size );
			m_offset += size;
		}

		virtual void Flush() override {}

	private:
		const Uint8*	m_data;
		Uint64			m_size;
		Uint64			m_offset;
	};
}