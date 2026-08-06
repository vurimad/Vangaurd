/**
* Copyright (c) 2007-20 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "nullFile.h"

//----

void CNullFileWriter::Serialize( void* buffer, size_t size )
{
	RED_UNUSED( buffer );

	m_pos += size;
	m_maxSize = math::Max< Uint64 >( m_pos, m_maxSize );
}

Uint64 CNullFileWriter::GetOffset() const
{
	return m_pos;
}

Uint64 CNullFileWriter::GetSize() const
{
	return m_maxSize;
}

void CNullFileWriter::Seek( Int64 offset )
{
	m_pos = offset;
}

void CNullFileWriter::Flush()
{
}

//----
