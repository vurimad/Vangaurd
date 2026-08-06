/*
* Copyright (c) 2015-16 CD Projekt Red. All Rights Reserved.
*/

#pragma once

namespace red {

//////////////////////////////////////////////////////////////////////////

RED_INLINE DynArrayAccessor::DynArrayAccessor()
	: m_size( 0 )
{
}

RED_INLINE DynArrayAccessor::~DynArrayAccessor()
{
	// do not release buffer
	// DynArrayAccessor only "wraps" an array responsible for memory (de)allocation
}

//////////////////////////////////////////////////////////////////////////

RED_INLINE void DynArrayAccessor::Clear()
{
	m_size = 0;
}

RED_INLINE void DynArrayAccessor::Resize( Uint32 size, Uint32 elementSize, Uint32 alignment )
{	
	RED_WARNING( size < m_size || ( size - m_size ) < 0x1000000, "DynArrayAccessor size is rapidly growing. Is that intended?" );

	if ( size != m_size )
	{
		if ( size > Capacity() )
		{
			Uint32 newCapacity = ArrayImplUtils::CalcResizeCapacity( size, Capacity() );
			BufferType::ResizeBuffer( newCapacity, elementSize, alignment );
		}
		m_size = size;
	}
}

RED_INLINE void DynArrayAccessor::ResizeExact( Uint32 size, Uint32 elementSize, Uint32 alignment )
{	
	RED_WARNING( size < m_size || ( size - m_size ) < 0x1000000, "DynArrayAccessor size is rapidly growing. Is that intended?" );

	if ( size != m_size )
	{
		if ( size > Capacity() )
		{
			BufferType::ResizeBuffer( size, elementSize, alignment );
		}
		m_size = size;
	}
}

RED_INLINE void DynArrayAccessor::Grow( Uint32 amount, Uint32 elementSize, Uint32 alignment )
{	
	Resize( m_size + amount, elementSize, alignment );
}

RED_INLINE void DynArrayAccessor::GrowExact( Uint32 amount, Uint32 elementSize, Uint32 alignment )
{	
	ResizeExact( m_size + amount, elementSize, alignment );
}

RED_INLINE void DynArrayAccessor::Reserve( Uint32 capacity, Uint32 elementSize, Uint32 alignment )
{	
	if ( Capacity() < capacity )
	{
		BufferType::ResizeBuffer( capacity, elementSize, alignment );
	}
}

RED_INLINE void DynArrayAccessor::Shrink( Uint32 elementSize, Uint32 alignment )
{
	if ( Capacity() > m_size )
	{
		BufferType::ResizeBuffer( m_size, elementSize, alignment );
	}
}

RED_INLINE void DynArrayAccessor::Swap( DynArrayAccessor& other )
{
	using std::swap;
	swap( m_buffer, other.m_buffer );
	swap( m_capacity, other.m_capacity );
	swap( m_size, other.m_size );
}

//////////////////////////////////////////////////////////////////////////

RED_INLINE void DynArrayAccessor::Create( void *ptr )
{
	new ( ptr ) DynArrayAccessor();
}

//////////////////////////////////////////////////////////////////////////

RED_INLINE void DynArrayAccessor::SetPool( const red::memory::Pool& pool )
{
	RED_FATAL_ASSERT( m_capacity == 0, "Cannot change pool for already allocated DynamicBuffer" );
	red::Memcpy( &m_buffer, &pool, sizeof( red::memory::Pool ) );
}

RED_INLINE const red::memory::Pool& DynArrayAccessor::GetPool( Uint32 elementSize ) const
{
	return *reinterpret_cast< red::memory::Pool* >( DynamicBuffer::GetPoolStoragePtr( elementSize ) );
}

//////////////////////////////////////////////////////////////////////////

RED_INLINE DynArrayAccessor& DynArrayAccessor::GetRef( const void* ptr )
{
	return *static_cast< DynArrayAccessor* >( const_cast< void* >( ptr ) );
}

template< typename TElement >
RED_INLINE DynArrayAccessor& DynArrayAccessor::GetRef( DynArray< TElement >& arr )
{
	return reinterpret_cast< DynArrayAccessor& >( arr );
}

} // red