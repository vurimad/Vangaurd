/*
 * Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
 */

#pragma once

#include "../../../common/redFileSystem/include/fileSys.h"
#include "../../../common/redMemory/include/uniqueBuffer.h"

namespace tools
{

// Simple batched text writer to use with RapidJSON so that there's a constant sized buffer
// which does not reallocate or require a huge amount of memory.
class BatchedTextWriter
{
public:
	typedef char Ch;

	explicit BatchedTextWriter( IFile& file )
		: m_buffer( red::CreateUniqueBuffer< red::PoolBackend >( RED_MEGA_BYTE( 4 ), 16 ) )
		, m_begin( static_cast< char* >( m_buffer.Get() ) )
		, m_current( m_begin )
		, m_end( m_begin + m_buffer.GetSize() )
		, m_file( file )
	{
	}

	~BatchedTextWriter()
	{
		Flush();
	}

	void Put( const Ch ch )
	{
		*m_current++ = ch;
		if ( m_current == m_end )
		{
			Flush();
		}
	}

	void Flush()
	{
		m_file.Serialize( m_begin, static_cast< Uint32 >( m_current - m_begin ) );
		RED_FATAL_ASSERT( !m_file.HasErrors() );
		m_current = m_begin;
	}

private:
	red::UniqueBuffer m_buffer;
	char* m_begin;
	char* m_current;
	char* m_end;

	IFile& m_file;
};

}