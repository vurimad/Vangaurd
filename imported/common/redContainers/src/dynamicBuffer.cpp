/*
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

namespace red
{
	
//////////////////////////////////////////////////////////////////////////

DynamicBuffer::DynamicBuffer( const red::memory::Pool& pool )
	: m_capacity( 0 )
{
	red::Memcpy( &m_buffer, &pool, sizeof( red::memory::Pool ) );
}

DynamicBuffer::~DynamicBuffer()
{
	RED_FATAL_ASSERT( m_capacity == 0, "Destroying a dynamic buffer, but it still contains elements that cannot be freed" );
}

void DynamicBuffer::ReleaseBuffer( Uint32 elementSize, Uint32 alignment )
{
	// ctremblay: only for DTOR
	Uint64 poolAddress = *(Uint64*)( red::OffsetPtr( m_buffer, CalcPoolStorageOffset( m_capacity, elementSize ) ) );
	void* pool = reinterpret_cast< void* >( poolAddress );
	red::memory::Free(reinterpret_cast<red::memory::Pool&>(pool), m_buffer);
	*((Uint64*)&m_buffer) = poolAddress; // ctremblay: DynArray seems to be reused after dtor is called... Especially in script.
	m_capacity = 0;
}

void DynamicBuffer::ResizeBuffer( Uint32 capacity, Uint32 elementSize, Uint32 alignment, void ( *moveFunc )( void*, void*, Uint32, const void* ) /* = nullptr */ )
{
	RED_WARNING( capacity < m_capacity || ( capacity - m_capacity ) < 0x1000000, "DynamicBuffer capacity is rapidly growing. Is that intended?" );

	if ( capacity != m_capacity )
	{		
		// because some may want to "initialize" object as following:
		// TypeUsingDynamicBuffer var;
		// memset( &var, 0, sizeof( var ) );
		if ( m_buffer == nullptr )
		{
			red::Memcpy( &m_buffer, &red::PoolDefault::GetInstance(), sizeof( red::memory::Pool ) );
		}

		void* pool = 0;
		red::Memcpy( &pool, GetPoolStoragePtr( elementSize ), sizeof( red::memory::Pool ) );

		// if m_capacity == 0 then m_buffer stores information about pool
		// at this point pool was copied to another storage and we can safely reset m_buffer value
		if ( m_capacity == 0 )
		{
			m_buffer = nullptr;
		}

		// if buffer is non-empty we need to reserve additional space for pool storage
		const Uint32 toAllocate = CalcBufferWithPoolStorageSize( capacity, elementSize );
		const Uint32 toMove = std::min( m_capacity, capacity ) * elementSize;
		// if there's moveFunc and some elements that need to be moved
		if ( moveFunc != nullptr && toMove > 0 )
		{
			void* newBuffer = red::memory::AllocateAligned( reinterpret_cast< red::memory::Pool& >( pool ), toAllocate, alignment );
			moveFunc( newBuffer, m_buffer, toMove, this );
			red::memory::Free( reinterpret_cast< red::memory::Pool& >( pool ), m_buffer );
			m_buffer = newBuffer;
		}
		else
		{
			m_buffer = red::memory::ReallocateAligned( reinterpret_cast< red::memory::Pool& >( pool ), m_buffer, toAllocate, alignment );
		}
		// this should be assigned AFTER reallocation and move is done
		m_capacity = capacity;

		// store memory pool in the right place
		red::Memcpy( GetPoolStoragePtr( elementSize ), &pool, sizeof( red::memory::Pool ) );
	}
}

void* DynamicBuffer::GetPoolStoragePtr( Uint32 elementSize ) const
{
	if ( m_capacity == 0 )
	{
		return const_cast< void** >( &m_buffer );
	}
	else
	{
		return red::OffsetPtr( m_buffer, CalcPoolStorageOffset( m_capacity, elementSize ) );
	}
}

Uint64 DynamicBuffer::CalcPoolStorageOffset( Uint32 capacity, Uint32 elementSize )
{
	const Uint64 offset = static_cast< Uint64 >( capacity ) * elementSize;
	const Uint64 alignedOffset = AlignUp( offset, __alignof( void* ) );
	ALWAYSENABLED_RED_FATAL_ASSERT( !( alignedOffset & 0xFFFFFFFF00000000 ), "Detected integer overflow for offset: %llu aligned: %llu capacity: %u elementSize: %u", offset, alignedOffset, capacity, elementSize );
	return alignedOffset;
}

Uint32 DynamicBuffer::CalcBufferWithPoolStorageSize( Uint32 capacity, Uint32 elementSize )
{
	if ( capacity == 0 )
	{
		return 0;
	}

	const Uint64 offset = CalcPoolStorageOffset( capacity, elementSize ) + sizeof( void* );
	ALWAYSENABLED_RED_FATAL_ASSERT( !( offset & 0xFFFFFFFF00000000 ), "Detected integer overflow for offset: %llu capacity: %u elementSize: %u", offset, capacity, elementSize );
	return static_cast< Uint32 >( offset );
}
}
