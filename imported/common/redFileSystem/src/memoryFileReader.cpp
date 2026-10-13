/**
* Copyright (c) 2007-20 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "memoryFileReader.h"

/////////////////////////////////////////////////////////////////////////////////////////////

CMemoryFileReader::CMemoryFileReader( const red::DynArray< Uint8 >& data, uintptr_t offset )
	: IFile( FF_MemoryBased | FF_Reader )
	, m_data( data.TypedData() )
	, m_dataSize( data.Size() )
	, m_offset( offset )
{
	RED_FATAL_ASSERT( offset <= m_dataSize, "Offset %llu is outside the valid range 0 - %llu", offset, m_dataSize );
}

CMemoryFileReader::CMemoryFileReader( const Uint8* data, size_t dataSize, uintptr_t offset )
	: IFile( FF_MemoryBased | FF_Reader )
	, m_data( data )
	, m_dataSize( dataSize )
	, m_offset( offset )
{
	RED_FATAL_ASSERT( offset <= m_dataSize, "Offset %llu is outside the valid range 0 - %llu", offset, dataSize );
}

CMemoryFileReader::~CMemoryFileReader() = default;

// Serialize data buffer of given size
void CMemoryFileReader::Serialize( void* buffer, size_t size )
{
	RED_FATAL_ASSERT( m_offset + size <= m_dataSize, "Reading %llu - %llu which is outside of valid range 0 - %llu", m_offset, m_offset + size, m_dataSize );
	// Check for final so we don't buffer overrun
	if( m_offset + size > m_dataSize )
	{
		size = m_dataSize - m_offset;
	}

	red::Memcpy( buffer, &m_data[ m_offset ], size );
	m_offset += size;
}

// Get position in file stream
Uint64 CMemoryFileReader::GetOffset() const
{
	return m_offset;
}

// Get size of the file stream
Uint64 CMemoryFileReader::GetSize() const
{
	return m_dataSize;
}

// Seek to file position
void CMemoryFileReader::Seek( Int64 offset )
{
	RED_FATAL_ASSERT( 0 <= offset && static_cast< uintptr_t >( offset ) <= m_dataSize, "Seeking to %lld is outside of valid range 0 - %llu", offset, m_dataSize );
	// Do this check again for final
	if ( 0 <= offset && static_cast< uintptr_t >( offset ) <= m_dataSize )
	{
		m_offset = static_cast< uintptr_t >( offset );
	}
}

void CMemoryFileReader::Flush()
{
}

/////////////////////////////////////////////////////////////////////////////////////////////

CMemoryFileReaderWithBuffer::CMemoryFileReaderWithBuffer( Uint32 size )
	: CMemoryFileReader( nullptr, 0, 0 )
	, m_dataPtr( size, red::PoolEngine() )
{
	SetData( m_dataPtr.TypedData() );
	SetSize( size );
}

CMemoryFileReaderWithBuffer::~CMemoryFileReaderWithBuffer() = default;

/////////////////////////////////////////////////////////////////////////////////////////////

CMemoryFileReaderExternalBuffer::CMemoryFileReaderExternalBuffer( const void* buffer, Uint32 size, const char* debugName )
	: IFile( FF_MemoryBased | FF_Reader )
	, m_buffer( buffer )
	, m_size( size )
	, m_offset( 0 )
	, m_debugName( debugName )
{
}

CMemoryFileReaderExternalBuffer::~CMemoryFileReaderExternalBuffer() = default;

void CMemoryFileReaderExternalBuffer::Serialize( void* buffer, size_t size )
{
	RED_FATAL_ASSERT( m_offset + size <= m_size, "Reading %u - %llu which is outside of valid range 0 - %u", m_offset, m_offset + size, m_size );
	// Do this check again for final so we don't read past the end of the file
	if ( m_offset + size <= m_size )
	{
		red::Memcpy( buffer, red::OffsetPtr( m_buffer, m_offset ), size );
		m_offset += static_cast< Uint32 >( size );
	}
	else
	{
		red::Memset( buffer, 0, size );
	}
}

Uint64 CMemoryFileReaderExternalBuffer::GetOffset() const
{
	return m_offset;
}

Uint64 CMemoryFileReaderExternalBuffer::GetSize() const
{
	return m_size;
}

void CMemoryFileReaderExternalBuffer::Seek( Int64 offset )
{
	RED_FATAL_ASSERT( 0 <= offset && static_cast< uintptr_t >( offset ) <= m_size, "Seeking to %lld is outside of valid range 0 - %llu", offset, m_size );
	m_offset = static_cast< Uint32 >( offset );
}

void CMemoryFileReaderExternalBuffer::Flush()
{
}

const char* CMemoryFileReaderExternalBuffer::GetFileNameForDebug() const
{
	if ( m_debugName != nullptr )
	{
		return m_debugName;
	}
	return IFile::GetFileNameForDebug();
}

/////////////////////////////////////////////////////////////////////////////////////////////
