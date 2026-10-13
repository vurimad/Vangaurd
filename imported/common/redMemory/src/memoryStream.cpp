#include "build.h"
#include "memoryStream.h"
#include "functions.h"
#include "assert.h"
#include "systemAllocatorType.h"

namespace red
{
namespace memory
{
namespace
{
	const u32 c_memoryStreamBufferSize = RED_MEGA_BYTE( 2 );
	const u32 c_minAllocationChunkSize = RED_KILO_BYTE( 64 );
}

	MemoryStream::MemoryStream( u32 memoryFlags )
		:	m_writeMarker( 0 ),
			m_readMarker( 0 ),
			m_nextVirtualAddress( 0 ),
			m_allocator( nullptr ),
			m_virtualRange( NullVirtualRange() ),
			m_memoryFlags( memoryFlags )
	{
	}

	void MemoryStream::Initialize( SystemAllocator * systemAllocator, const u32 bufferSize, const u32 initialAllocation )
	{
		RED_MEMORY_ASSERT( !m_allocator, "Memory stream was already initialized." );
		m_allocator = systemAllocator;
		m_virtualRange = m_allocator->ReserveVirtualRange( bufferSize ? RoundUp( bufferSize, c_minAllocationChunkSize ) : c_memoryStreamBufferSize, m_memoryFlags );
		m_nextVirtualAddress = m_virtualRange.start;
		const SystemBlock block = AllocateBlock( initialAllocation ? RoundUp( initialAllocation, c_minAllocationChunkSize ) : c_minAllocationChunkSize );
		RED_MEMORY_ASSERT( block != NullSystemBlock(), "Failed to commit memory." );
		RED_UNUSED( block );

		m_writeMarker = m_virtualRange.start;
		m_readMarker = m_virtualRange.start;
	}

	void MemoryStream::Uninitialize()
	{
		if ( m_allocator )
		{
			m_allocator->ReleaseVirtualRange( m_virtualRange );
		}

		m_allocator = nullptr;
	}

	void MemoryStream::ResetMarkers()
	{
		m_writeMarker = m_readMarker = m_virtualRange.start;
	}

	SystemBlock MemoryStream::AllocateBlock(u32 size)
	{
		RED_MEMORY_ASSERT( m_allocator, "Memory stream is not initialized." );
		SystemBlock block = { m_nextVirtualAddress, RoundUp( size, c_minAllocationChunkSize ) };
		block = m_allocator->Commit( block, m_memoryFlags );

		if ( block.address )
		{
			m_nextVirtualAddress = block.address + block.size;
			return block;
		}

		return NullSystemBlock();
	}

	u32 MemoryStream::OnDataAvailableToWrite() const
	{
		return static_cast< u32 >( m_virtualRange.end - m_writeMarker );
	}

	u32 MemoryStream::OnDataAvailableToRead() const
	{
		return static_cast< u32 >( m_writeMarker - m_readMarker );
	}

	Bool MemoryStream::OnWriteBuffer( const void * buffer, u32 size )
	{
		RED_MEMORY_ASSERT( m_allocator, "Memory stream is not initialized." );
		RED_MEMORY_ASSERT( buffer != nullptr, "Invalid data was given to memory stream." );

		if ( m_writeMarker + size > m_virtualRange.end )
		{
			RED_LOG_ERROR( "No enough memory to store new data." );
			return false;
		}

		if ( m_writeMarker + size > m_nextVirtualAddress )
		{
			const SystemBlock block = AllocateBlock( size );
			RED_MEMORY_ASSERT( block != NullSystemBlock(), "Failed to commit memory." );
			RED_UNUSED( block );
		}

		RED_MEMORY_ASSERT( m_writeMarker + size <= m_nextVirtualAddress, "Given data overflow internal buffer." );
		Memcpy( reinterpret_cast< void * >( m_writeMarker ), buffer, size );
		m_writeMarker += size;
		return true;
	}

	Bool MemoryStream::OnReadBuffer( void * buffer, u32 size, u32 & readSize )
	{
		RED_MEMORY_ASSERT( m_allocator, "Memory stream is not initialized." );

		if ( m_readMarker >= m_writeMarker )
		{
			RED_LOG_ERROR( "No data available to read." );
			return false;
		}

		const u32 availableDataToRead = static_cast< u32 >( m_writeMarker - m_readMarker );
		readSize = availableDataToRead < size ? availableDataToRead : size;
		Memcpy( buffer, reinterpret_cast< void * >( m_readMarker ), readSize );
		m_readMarker += readSize;
		return true;
	}

	void* MemoryStream::OnGetReadData() const
	{
		RED_MEMORY_ASSERT( m_allocator, "Memory stream is not initialized." );
		return reinterpret_cast< void * >( m_readMarker );
	}

	void MemoryStream::OnSeekReadIndex( u32 size )
	{
		RED_MEMORY_ASSERT( m_allocator, "Memory stream is not initialized." );
		RED_MEMORY_ASSERT( m_readMarker + size <= m_writeMarker, "Memory stream read marker has to be less or equal to write marker." );
		m_readMarker += size;
	}
}
}