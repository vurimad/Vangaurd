/*
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "memoryBufferFile.h"

//------------------------------------------------------------------------------

GrowableMemoryBuffer::GrowableMemoryBuffer( const Uint32 blockSize, const Uint32 blockInitCount )
	: m_start( nullptr )
	, m_end( nullptr )
	, m_ptr( nullptr )
{
	red::memory::DynamicLinearAllocatorParameter params{ 
		&red::memory::AcquireSystemAllocator(),
		blockSize, 
		red::memory::Flags::Flags_CPU_Read_Write 
	};
	m_allocator.Initialize( params );

	m_allocation = m_allocator.Allocate( blockSize * blockInitCount );
	m_start = reinterpret_cast< Uint8* >( m_allocation.address );
	m_ptr = m_start;
}

GrowableMemoryBuffer::~GrowableMemoryBuffer() = default;

void* GrowableMemoryBuffer::Data()
{
	return m_start;
}

const void* GrowableMemoryBuffer::Data() const
{
	return m_start;
}

Uint32 GrowableMemoryBuffer::BlockSize() const
{
	return m_allocator.GetChunkSize();
}

Uint32 GrowableMemoryBuffer::Size() const
{
	return static_cast<Uint32>( m_ptr - m_start );
}

Uint32 GrowableMemoryBuffer::Capacity() const
{
	return static_cast<Uint32>( m_allocation.size );
}

bool GrowableMemoryBuffer::Read( Uint32 offset, void* buffer, Uint32 size ) const
{
	RED_FATAL_ASSERT( offset + size <= m_allocation.size, "Cannot read outside the buffer range. (%u,%u) is outside of (0,%u)", offset, offset + size, m_allocation.size );

	const Uint8* readStart = m_start + offset;
	if ( m_start <= readStart && readStart < m_ptr && readStart + size <= m_ptr )
	{
		red::Memcpy( buffer, readStart, size );
		return true;
	}
	return false;
}

bool GrowableMemoryBuffer::Write( Uint32 offset, const void* buffer, Uint32 size )
{
	RED_FATAL_ASSERT( static_cast<Uint64>( offset ) + size < std::numeric_limits<Uint32>::max() );
	if ( EnsureSize( offset + size ) )
	{
		Uint8* writeStart = m_start + offset;
		Uint8* writeEnd = writeStart + size;
		red::Memcpy( writeStart, buffer, size );
		m_ptr = std::max( writeEnd, m_ptr );
		return true;
	}
	return false;
}

bool GrowableMemoryBuffer::EnsureSize( Uint32 size )
{
	if ( size > Capacity() )
	{
		const Uint32 sizeToAlloc = red::memory::RoundUp( size, BlockSize() );
		const auto block = m_allocator.Reallocate( m_allocation, sizeToAlloc );
		RED_FATAL_ASSERT( m_allocation.address == block.address );
		m_allocation = block;
	}
	return size <= Capacity();
}

//------------------------------------------------------------------------------

MemoryBufferFileWriter::MemoryBufferFileWriter( GrowableMemoryBuffer& buffer )
	: IFile( FF_Writer | FF_MemoryBased )
	, m_buffer( buffer )
	, m_offset( 0ull )
{
}

MemoryBufferFileWriter::~MemoryBufferFileWriter() = default;

void MemoryBufferFileWriter::Serialize( void* buffer, size_t size )
{
	RED_FATAL_ASSERT( m_offset < std::numeric_limits<Uint32>::max(), "Offset %llu exceeds maximum buffer size", m_offset );
	RED_FATAL_ASSERT( size < std::numeric_limits<Uint32>::max(), "Size %llu exceeds maximum buffer size", size );

	bool result = m_buffer.Write( static_cast<Uint32>( m_offset ), buffer, static_cast<Uint32>( size ) );
	if ( !result )
	{
		HandleIOError( "Internal error, could not write to the buffer" );
	}
	else
	{
		m_offset += size;
	}
}

Uint64 MemoryBufferFileWriter::GetOffset() const
{
	return m_offset;
}

Uint64 MemoryBufferFileWriter::GetSize() const
{
	return m_buffer.Size();
}

void MemoryBufferFileWriter::Seek( Int64 offset )
{
	m_offset = offset;
}

void MemoryBufferFileWriter::Flush()
{
}

//------------------------------------------------------------------------------

MemoryBufferFileReader::MemoryBufferFileReader( const GrowableMemoryBuffer& buffer )
	: IFile( FF_Reader | FF_MemoryBased )
	, m_buffer( buffer )
	, m_offset( 0ull )
{
}

MemoryBufferFileReader::~MemoryBufferFileReader() = default;

void MemoryBufferFileReader::Serialize( void* buffer, size_t size )
{
	RED_FATAL_ASSERT( m_offset < std::numeric_limits<Uint32>::max(), "Offset %llu exceeds maximum buffer size", m_offset );
	RED_FATAL_ASSERT( size < std::numeric_limits<Uint32>::max(), "Size %llu exceeds maximum buffer size", size );

	bool result = m_buffer.Read( static_cast<Uint32>( m_offset ), buffer, static_cast<Uint32>( size ) );
	if ( !result )
	{
		HandleIOError( "Error reading from memory buffer Read Span (%llu,%llu) from (0,%u)", m_offset, size, m_buffer.Size() );
	}
	else
	{
		m_offset += size;
	}
}

Uint64 MemoryBufferFileReader::GetOffset() const
{
	return m_offset;
}

Uint64 MemoryBufferFileReader::GetSize() const
{
	return m_buffer.Size();
}

void MemoryBufferFileReader::Seek( Int64 offset )
{
	m_offset = offset;
}

void MemoryBufferFileReader::Flush()
{
}

//------------------------------------------------------------------------------