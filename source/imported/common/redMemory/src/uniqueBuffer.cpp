/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "uniqueBuffer.h"

namespace red
{
	UniqueBuffer::UniqueBuffer( UniqueBuffer && rvalue )
		:	m_buffer( rvalue.Release() ),
			m_size( rvalue.m_size ),
			m_alignment( rvalue.m_alignment ),
			m_allocator( rvalue.m_allocator )
	{
		rvalue.m_size = 0;
	}

	UniqueBuffer::~UniqueBuffer()
	{
		if( m_buffer )
		{
			internal::UniqueBufferAllocatorInterface* allocator = reinterpret_cast< internal::UniqueBufferAllocatorInterface* >( m_allocator.buffer );
			allocator->Free( m_buffer );
		}
	}

	void UniqueBuffer::Reset()
	{
		UniqueBuffer().Swap( *this );
	}

	void * UniqueBuffer::Release()
	{
		void * buffer = m_buffer;
		m_buffer = nullptr;
		return buffer;
	}

	void UniqueBuffer::Reallocate( Uint32 size )
	{
		internal::UniqueBufferAllocatorInterface* allocator = reinterpret_cast< internal::UniqueBufferAllocatorInterface* >( m_allocator.buffer );		
		m_buffer = allocator->ReallocateAligned( m_buffer, size, m_alignment );
		m_size = size;
	}

	void UniqueBuffer::PatchSize( Uint32 smallerSize )
	{
		RED_FATAL_ASSERT( smallerSize <= m_size ); // allow equal, but not greater since that would imply growing the buffer
		m_size = smallerSize;
	}

	void UniqueBuffer::Swap( UniqueBuffer & value )
	{
		std::swap( m_buffer, value.m_buffer );
		std::swap( m_size, value.m_size );
		std::swap( m_alignment, value.m_alignment );
		std::swap( m_allocator, value.m_allocator );
	}

	UniqueBuffer & UniqueBuffer::operator=( UniqueBuffer && rvalue )
	{
		UniqueBuffer( std::move( rvalue ) ).Swap( *this );
		return *this; 
	}

	//-----

	internal::UniqueBufferPoolAllocator::UniqueBufferPoolAllocator( const red::memory::Pool * pool )
	{
		static_assert( sizeof( red::memory::Pool ) == sizeof( void* ), "Memory pool must have the same size as a pointer" );
		red::Memcpy( &m_buffer, pool, sizeof( red::memory::Pool ) );
	}

	void * internal::UniqueBufferPoolAllocator::ReallocateAligned( void * inputBuffer, Uint32 size, Uint32 alignment )
	{
		auto proxy = reinterpret_cast<red::memory::Pool*>(&m_buffer);
		return memory::ReallocateAligned( *proxy, inputBuffer, size, alignment );
	}

	void internal::UniqueBufferPoolAllocator::Free( void * buffer )
	{
		auto proxy = reinterpret_cast<red::memory::Pool*>(&m_buffer);
		memory::Free( *proxy, buffer );
	}

	const red::memory::Pool* internal::UniqueBufferPoolAllocator::GetPool() const
	{
		return reinterpret_cast<const red::memory::Pool*>(&m_buffer);
	}

}
