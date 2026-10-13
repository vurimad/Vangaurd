/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "memoryFileWriter.h"

CMemoryFileWriter::CMemoryFileWriter( red::DynArray< Uint8 >& data )
	: IFile( FF_MemoryBased | FF_Writer )
	, m_data( red::DynArrayAccessor::GetRef( data ) )
	, m_offset( data.Size() )
{
}

CMemoryFileWriter::~CMemoryFileWriter() = default;

// Serialize data buffer of given size
void CMemoryFileWriter::Serialize( void* buffer, size_t size )
{
	if ( size )
	{
		if ( m_offset + size > m_data.Size() )
		{
			m_data.Grow( static_cast< Uint32 >( m_offset + size ) - m_data.Size(), sizeof( Uint8 ), __alignof( Uint8 ) );
		}

		void* writeBuffer = reinterpret_cast< void* >( reinterpret_cast< red::MemUint >( m_data.Data() ) + m_offset );
		red::Memcpy( writeBuffer, buffer, size );
		m_offset += static_cast< Uint32 >( size );
	}
}

// Get position in file stream
Uint64 CMemoryFileWriter::GetOffset() const
{
	return m_offset;
}

// Get size of the file stream
Uint64 CMemoryFileWriter::GetSize() const
{
	return m_data.Size();
}

// Seek to file position
void CMemoryFileWriter::Seek( Int64 offset )
{
	//ASSERT( offset <= (Int32)m_data.Size() )
	m_offset = static_cast< Uint32 >( offset );
}

void CMemoryFileWriter::Flush()
{
}

void CMemoryFileWriter::SetFileNameForDebug( const red::String& str )
{
	m_debugFileName = str;
}

const char* CMemoryFileWriter::GetFileNameForDebug() const
{
	return m_debugFileName.AsChar();
}


CMemoryFileWriterWithDebugName::CMemoryFileWriterWithDebugName(red::DynArray< Uint8 >& data, const String& debugName)
	: TBaseClass( data )
	, m_debugName( debugName )
{}

const char* CMemoryFileWriterWithDebugName::GetFileNameForDebug() const
{
	return m_debugName.AsChar();
}


CMemoryFileWriterExternalBuffer::CMemoryFileWriterExternalBuffer( void* buffer, Uint32 size )
	: IFile( FF_MemoryBased | FF_Writer ) 
	, m_buffer( buffer )
	, m_offset( 0 )
	, m_realSize( 0 )
	, m_size( size )
{
}

CMemoryFileWriterExternalBuffer::~CMemoryFileWriterExternalBuffer() = default;

// Serialize data buffer of given size
void CMemoryFileWriterExternalBuffer::Serialize( void* buffer, size_t size )
{
	if ( size )
	{
		size_t freeSpace = m_size - m_offset;
		size = Clamp< size_t >( size, 0, freeSpace );

		void* writeBuffer = reinterpret_cast< void* >( reinterpret_cast< red::MemUint >( m_buffer ) + m_offset );
		red::Memcpy( writeBuffer, buffer, size );
		m_offset += static_cast< Uint32 >( size );
		m_realSize = Max( m_offset, m_realSize );
	}
}

Uint64 CMemoryFileWriterExternalBuffer::GetOffset() const
{
	return m_offset;
}

Uint64 CMemoryFileWriterExternalBuffer::GetSize() const
{
	// Pretty sure GetSize() should return the size of the file we are writing, all the other IFiles work like this
	return m_realSize;
}

void CMemoryFileWriterExternalBuffer::Seek( Int64 offset )
{
	m_offset = static_cast< Uint32 >( offset );
}

void CMemoryFileWriterExternalBuffer::Flush()
{
}

//

CMemoryFileWriterExternalUniqueBuffer::CMemoryFileWriterExternalUniqueBuffer( red::UniqueBuffer& buffer )
	: IFile( FF_MemoryBased | FF_Writer )
	, m_buffer( buffer )
	, m_offset( 0 )
	, m_realSize( 0 )
{
}

CMemoryFileWriterExternalUniqueBuffer::~CMemoryFileWriterExternalUniqueBuffer() = default;

void CMemoryFileWriterExternalUniqueBuffer::Serialize( void* buffer, size_t size )
{
	if( size )
	{
		if( m_offset + size > m_buffer.Size() )
		{
			m_buffer.Reallocate( m_buffer.Size() + static_cast< Uint32 >( m_offset + size ) + RED_KILO_BYTE( 256 ) );
		}

		void* writeBuffer = reinterpret_cast< void* >( reinterpret_cast< red::MemUint >( m_buffer.Data() ) + m_offset );
		red::Memcpy( writeBuffer, buffer, size );
		m_offset += static_cast< Uint32 >( size );
		m_realSize = Max( m_offset, m_realSize );
	}
}

Uint64 CMemoryFileWriterExternalUniqueBuffer::GetOffset() const
{
	return m_offset;
}

Uint64 CMemoryFileWriterExternalUniqueBuffer::GetSize() const
{
	// Pretty sure GetSize() should return the size of the file we are writing, all the other IFiles work like this
	return m_realSize;
}

void CMemoryFileWriterExternalUniqueBuffer::Seek( Int64 offset )
{
	m_offset = static_cast< Uint32 >( offset );
}

void CMemoryFileWriterExternalUniqueBuffer::Flush()
{
}

//

CMemoryFileBufferedWriter::CMemoryFileBufferedWriter( void* buffer, Uint32 size, red::DynArray< Uint8 >& data )
	: IFile( FF_MemoryBased | FF_Writer )
	, m_staticWriter( buffer, size )
	, m_dynamicWriter( data )
	, m_usesDynamicWriter( false )
{
}

CMemoryFileBufferedWriter::~CMemoryFileBufferedWriter() = default;

void CMemoryFileBufferedWriter::Serialize( void* buffer, size_t size )
{
	if ( m_usesDynamicWriter )
	{
		m_dynamicWriter.Serialize( buffer, size );
	}
	else
	{
		if ( m_staticWriter.CanSerialize( size ) )
		{
			m_staticWriter.Serialize( buffer, size );
		}
		else
		{
			m_dynamicWriter.Serialize( m_staticWriter.GetData(), m_staticWriter.GetRealSize() );
			m_dynamicWriter.Serialize( buffer, size );
			m_usesDynamicWriter = true;
		}
	}
}

Uint64 CMemoryFileBufferedWriter::GetOffset() const
{
	return m_usesDynamicWriter ? m_dynamicWriter.GetOffset() : m_staticWriter.GetOffset();
}

Uint64 CMemoryFileBufferedWriter::GetSize() const
{
	return m_usesDynamicWriter ? m_dynamicWriter.GetSize() : m_staticWriter.GetSize();
}

void CMemoryFileBufferedWriter::Seek(Int64 offset)
{
	m_usesDynamicWriter ? m_dynamicWriter.Seek( offset ) : m_staticWriter.Seek( offset );
}

void CMemoryFileBufferedWriter::Flush()
{
}

void* CMemoryFileBufferedWriter::GetData() const
{
	return m_usesDynamicWriter ? m_dynamicWriter.GetData() : m_staticWriter.GetData();
}

Uint32 CMemoryFileBufferedWriter::GetRealSize() const
{
	return m_usesDynamicWriter ? static_cast< Uint32 >( m_dynamicWriter.GetSize() ) : m_staticWriter.GetRealSize();
}
