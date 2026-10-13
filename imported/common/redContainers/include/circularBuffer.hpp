/*
* Copyright (c) 2015-16 CD Projekt Red. All Rights Reserved.
*/

#include "policies.h"

namespace red {

//////////////////////////////////////////////////////////////////////////

template < typename TElement >
RED_INLINE CircularBuffer< TElement >::CircularBuffer( const red::memory::Pool& pool )
	: DynamicBuffer( pool )
	, m_size( 0 )
	, m_front( 0 )
	, m_back( 0 )
{
}

template < typename TElement >
RED_INLINE CircularBuffer< TElement >::CircularBuffer( const CircularBuffer& other )
	: DynamicBuffer( other.GetPool() )
	, m_size( other.m_size )
	, m_front( other.m_front )
	, m_back( other.m_back )
{
	InternalCopy( other );
}

template < typename TElement >
RED_INLINE CircularBuffer< TElement >::CircularBuffer( CircularBuffer&& other )
{
	m_buffer = other.m_buffer;
	m_capacity = other.m_capacity;
	m_size = other.m_size;
	m_front = other.m_front;
	m_back = other.m_back;

	other.m_capacity = 0;
	other.m_size = 0;
	other.m_front = 0;
	other.m_back = 0;

	// put back other buffer's pool information
	red::Memcpy( &other.m_buffer, GetPoolStoragePtr( sizeof( TElement ) ), sizeof( red::memory::Pool ) );
}

template < typename TElement >
RED_INLINE CircularBuffer< TElement >::CircularBuffer( Uint32 initialCapacity, const red::memory::Pool& pool )
	: DynamicBuffer( pool )
	, m_size( 0 )
	, m_front( 0 )
	, m_back( 0 )
{
	ResizeBuffer( initialCapacity );
}

template < typename TElement >
RED_INLINE CircularBuffer< TElement >::~CircularBuffer()
{
	InternalDestruct();
	ResizeBuffer( 0 );
}

//////////////////////////////////////////////////////////////////////////

template < typename TElement >
RED_INLINE typename CircularBuffer< TElement >::iterator CircularBuffer< TElement >::Begin()
{
	return iterator(this, 0);
}

template < typename TElement >
RED_INLINE typename CircularBuffer< TElement >::iterator CircularBuffer< TElement >::End()
{
	return iterator(this, m_size);
}

template < typename TElement >
RED_INLINE typename CircularBuffer< TElement >::const_iterator CircularBuffer< TElement >::Begin() const
{
	return const_iterator(this, 0);
}

template < typename TElement >
RED_INLINE typename CircularBuffer< TElement >::const_iterator CircularBuffer< TElement >::End() const
{
	return const_iterator(this, m_size);
}

template < typename TElement >
RED_INLINE typename CircularBuffer< TElement >::reverse_iterator CircularBuffer< TElement >::RBegin()
{
	return reverse_iterator(End());
}

template < typename TElement >
RED_INLINE typename CircularBuffer< TElement >::reverse_iterator CircularBuffer< TElement >::REnd()
{
	return reverse_iterator(Begin());
}

template < typename TElement >
RED_INLINE typename CircularBuffer< TElement >::reverse_const_iterator CircularBuffer< TElement >::RBegin() const
{
	return reverse_const_iterator(End());
}

template < typename TElement >
RED_INLINE typename CircularBuffer< TElement >::reverse_const_iterator CircularBuffer< TElement >::REnd() const
{
	return reverse_const_iterator(Begin());
}

//////////////////////////////////////////////////////////////////////////

template < typename TElement >
RED_INLINE CircularBuffer< TElement >& CircularBuffer< TElement >::operator=( const CircularBuffer& other )
{
	CircularBuffer( other ).Swap( *this );
	return *this;
}

template < typename TElement >
RED_INLINE CircularBuffer< TElement >& CircularBuffer< TElement >::operator=( CircularBuffer&& other )
{
	CircularBuffer( std::move( other ) ).Swap( *this );
	return *this;
}

template < typename TElement >
RED_INLINE void CircularBuffer< TElement >::Swap( CircularBuffer& other )
{
	using namespace std;
	swap( m_buffer, other.m_buffer );
	swap( m_capacity, other.m_capacity );
	swap( m_size, other.m_size );
	swap( m_front, other.m_front );
	swap( m_back, other.m_back );
}

//////////////////////////////////////////////////////////////////////////

template < typename TElement >
RED_INLINE void CircularBuffer< TElement >::InternalCopy( const CircularBuffer& other )
{
	typedef typename policies::CopyConstructorExecutorSelector< TElement >::Type CopyConstructorExecutor;

	const Uint32 otherSize = other.Size();
	if ( m_capacity < otherSize )
	{
		ResizeBuffer( ArrayImplUtils::CalcResizeCapacity( otherSize, m_capacity ) );
	}
	if ( otherSize > 0 )
	{
		TElement* data = TypedData();
		// if other buffer is continuous just copy construct it linearly
		if ( other.m_front <= other.m_back )
		{
			CopyConstructorExecutor::Execute( data, &other.Front(), otherSize );
		}
		else
		{
			// otherwise first copy construct elements [ front, capacity )
			const Uint32 otherCapacity = other.Capacity();
			const Uint32 otherFront = other.m_front;
			const Uint32 frontSize = otherCapacity - otherFront;
			CopyConstructorExecutor::Execute( data, &other.Front(), frontSize );
			// and then [ 0, back ]
			const Uint32 otherBack = other.m_back;
			CopyConstructorExecutor::Execute( data + frontSize, other.TypedData(), otherBack + 1 );
		}
		m_front = 0;
		m_back = otherSize - 1;
	}
	else
	{
		// if the otherSize is 0 make sure m_front and m_back are zero as well. 
		// in the case that we are copying from a buffer with size of 0 but capacity > 0,
		// other.m_back could be > 0 so using that as our m_back value would be invalid
		m_front = m_back = 0;
	}
	m_size = otherSize;
}

template < typename TElement >
RED_INLINE void CircularBuffer< TElement >::InternalDestruct()
{
	typedef typename policies::DestructorExecutorSelector< TElement >::Type	DestructorExecutor;

	if ( m_size > 0 )
	{
		TElement* data = TypedData();
		// if buffer is continuous just destroy elements linearly
		if ( m_front <= m_back )
		{
			DestructorExecutor::Execute( data + m_front, m_size );
		}
		else
		{
			// otherwise first destroy elements [ front, size )
			DestructorExecutor::Execute( data + m_front, m_capacity - m_front );
			// and then [ 0, back ]
			DestructorExecutor::Execute( data, m_back + 1 );
		}
		m_front = m_back = 0;
	}
	m_size = 0;
}

//////////////////////////////////////////////////////////////////////////

template < typename TElement >
RED_INLINE TElement& CircularBuffer< TElement >::operator[]( Uint32 i )
{
	RED_FATAL_ASSERT( i < m_size, "Index out of bounds" );
	const Uint32 realIndex = ( m_front + i ) % m_capacity;
	return TypedData()[ realIndex ];
}

template < typename TElement >
RED_INLINE const TElement& CircularBuffer< TElement >::operator[]( Uint32 i ) const
{
	RED_FATAL_ASSERT( i < m_size, "Index out of bounds" );
	const Uint32 realIndex = ( m_front + i ) % m_capacity;
	return TypedData()[ realIndex ];
}

template < typename TElement >
RED_INLINE TElement& CircularBuffer< TElement >::Front()
{
	RED_FATAL_ASSERT( m_size > 0, "CircularBuffer is empty" );
	return TypedData()[ m_front ];
}

template < typename TElement >
RED_INLINE const TElement& CircularBuffer< TElement >::Front() const
{
	RED_FATAL_ASSERT( m_size > 0, "CircularBuffer is empty" );
	return TypedData()[ m_front ];
}

template < typename TElement >
RED_INLINE TElement& CircularBuffer< TElement >::Back()
{
	RED_FATAL_ASSERT( m_size > 0, "CircularBuffer is empty" );
	return TypedData()[ m_back ];
}

template < typename TElement >
RED_INLINE const TElement& CircularBuffer< TElement >::Back() const
{
	RED_FATAL_ASSERT( m_size > 0, "CircularBuffer is empty" );
	return TypedData()[ m_back ];
}

//////////////////////////////////////////////////////////////////////////

template < typename TElement >
RED_INLINE Bool CircularBuffer< TElement >::operator==( const CircularBuffer& other ) const
{
	if ( m_size != other.m_size )
	{
		return false;
	}
	const TElement* myData = TypedData();
	const TElement* otherData = other.TypedData();
	Uint32 myIndex = m_front;
	Uint32 otherIndex = other.m_front;
	for ( Uint32 i = 0; i < m_size; i++ )
	{
		if ( *( myData + myIndex ) != *( otherData + otherIndex ) )
		{
			return false;
		}
		myIndex = ( myIndex + 1 ) % m_capacity;
		otherIndex = ( otherIndex + 1 ) % other.m_capacity;
	}
	return true;
}

template < typename TElement >
RED_INLINE Bool CircularBuffer< TElement >::operator!=( const CircularBuffer& other ) const
{
	return !( *this == other );
}

//////////////////////////////////////////////////////////////////////////

template < typename TElement >
RED_INLINE void CircularBuffer< TElement >::PushFront( const TElement& element )
{
	typedef typename policies::CopyConstructorExecutorSelector< TElement >::Type CopyConstructorExecutor;

	ExtendFront();
	CopyConstructorExecutor::Execute( TypedData() + m_front, &element );
}

template < typename TElement >
RED_INLINE void CircularBuffer< TElement >::PushFront( TElement&& element )
{
	typedef typename policies::MoveConstructorExecutorSelector< TElement >::Type MoveConstructorExecutor;

	ExtendFront();
	MoveConstructorExecutor::Execute( TypedData() + m_front, &element );
}

template < typename TElement >
RED_INLINE void CircularBuffer< TElement >::PushBack( const TElement& element )
{
	typedef typename policies::CopyConstructorExecutorSelector< TElement >::Type CopyConstructorExecutor;

	ExtendBack();
	CopyConstructorExecutor::Execute( TypedData() + m_back, &element );
}

template < typename TElement >
RED_INLINE void CircularBuffer< TElement >::PushBack( TElement&& element )
{
	typedef typename policies::MoveConstructorExecutorSelector< TElement >::Type MoveConstructorExecutor;

	ExtendBack();
	MoveConstructorExecutor::Execute( TypedData() + m_back, &element );
}

template < typename TElement >
RED_INLINE void CircularBuffer< TElement >::PopFront()
{
	RED_FATAL_ASSERT( m_size > 0, "CircularBuffer is empty" );
	ReleaseFront();
}

template < typename TElement >
RED_INLINE void CircularBuffer< TElement >::PopBack()
{
	RED_FATAL_ASSERT( m_size > 0, "CircularBuffer is empty" );
	ReleaseBack();
}

template < typename TElement >
RED_INLINE void CircularBuffer< TElement >::ExtendFront()
{
	if ( m_size == m_capacity )
	{
		Reserve( ArrayImplUtils::CalcResizeCapacity( m_size + 1, m_capacity ) );
	}

	if ( m_size++ > 0 )
	{
		m_front = Wrap( m_front - 1 );
	}
	else
	{
		m_back = m_front;
	}
}

template < typename TElement >
RED_INLINE void CircularBuffer< TElement >::ExtendBack()
{
	if ( m_size == m_capacity )
	{
		Reserve( ArrayImplUtils::CalcResizeCapacity( m_size + 1, m_capacity ) );
	}

	if ( m_size++ > 0 )
	{
		m_back = Wrap( m_back + 1 );
	}
	else
	{
		m_front = m_back;
	}
}

template < typename TElement >
RED_INLINE void CircularBuffer< TElement >::ReleaseFront()
{
	RED_FATAL_ASSERT( m_size > 0, "CircularBuffer is empty" );

	typedef typename policies::DestructorExecutorSelector< TElement >::Type DestructorExecutor;

	--m_size;
	DestructorExecutor::Execute( TypedData() + m_front );
	m_front = Wrap( m_front + 1 );
	if ( !m_size )
	{
		m_front = 0;
		m_back = 0;
	}
}

template < typename TElement >
RED_INLINE void CircularBuffer< TElement >::ReleaseBack()
{
	RED_FATAL_ASSERT( m_size > 0, "CircularBuffer is empty" );

	typedef typename policies::DestructorExecutorSelector< TElement >::Type DestructorExecutor;

	--m_size;
	DestructorExecutor::Execute( TypedData() + m_back );
	m_back = Wrap( m_back - 1 );
	if ( !m_size )
	{
		m_front = 0;
		m_back = 0;
	}
}

//////////////////////////////////////////////////////////////////////////

template < typename TElement >
RED_INLINE Bool CircularBuffer< TElement >::Exist( const TElement& element ) const
{
	const TElement* data = TypedData();
	if ( m_front == m_back )
	{
		return false;
	}

	if ( m_front < m_back )
	{
		const TElement* begin = data + m_front;
		const TElement* end = data + m_back + 1;
		return std::find( begin, end, element ) != end;
	}
	else
	{
		{
			const TElement* begin = data;
			const TElement* end = data + m_back + 1;
			if ( std::find( begin, end, element ) != end )
			{
				return true;
			}
		}
		{
			const TElement* begin = data + m_front;
			const TElement* end = data + m_capacity;
			return std::find( begin, end, element ) != end;
		}
	}
}

//////////////////////////////////////////////////////////////////////////

template < typename TElement >
RED_INLINE void CircularBuffer< TElement >::Clear()
{
	InternalDestruct();
}

template < typename TElement >
RED_INLINE void CircularBuffer< TElement >::Reserve( const Uint32 capacity )
{
	typedef typename policies::MoveConstructorExecutorSelector< TElement >::Type MoveConstructorExecutor;
	typedef typename policies::MoveAssignmentExecutorSelector< TElement >::Type MoveAssignmentExecutor;
	typedef typename policies::DestructorExecutorSelector< TElement >::Type DestructorExecutor;

	if ( capacity > m_capacity )
	{
		const Uint32 oldCapacity = m_capacity;
		ResizeBuffer( capacity );
		if ( m_size > 0 && m_back < m_front )
		{
			// move [ front, size ) forwards
			const Uint32 offset = m_capacity - oldCapacity;
			// the number of elements we need to move
			const Uint32 toMove = oldCapacity - m_front;
			// construct only the elements that fit into newly created "hole" (which size is equal to "offset")
			const Uint32 toConstruct = std::min( offset, toMove );
			// the rest of elements needs to be move-assigned
			const Uint32 toAssign = toConstruct < toMove ? toMove - toConstruct : 0;
			TElement* front = TypedData() + m_front;
			MoveConstructorExecutor::Execute( front + offset + toAssign, front + toAssign, toConstruct );
			MoveAssignmentExecutor::Execute( front + offset, front, toAssign );
			// destruct the same amount of objects as constructed
			DestructorExecutor::Execute( front, toConstruct );
			m_front += offset;
		}
	}
}

template < typename TElement >
RED_INLINE void CircularBuffer< TElement >::MakeContiguous()
{
	if (m_size == 0)
	{
		// buffer is empty
		m_front = m_back = 0;
	}
	else
	{
		TElement* data = TypedData();
		if (m_back < m_front)
		{
			// move [ front, size ) back so that there's no space between "back" and "front"
			const Uint32 offset = m_front - m_back - 1;
			ArrayImplUtils::MoveBackwards(data + m_back + 1, offset, m_capacity - m_front);
			m_front -= offset;
		}
		else
		{
			// move everything to the beginning
			ArrayImplUtils::MoveBackwards(data, m_front, m_back - m_front + 1);
			m_front = 0;
			m_back = m_size - 1;
		}
	}
}

template < typename TElement >
RED_INLINE void CircularBuffer< TElement >::Shrink()
{
	if ( m_capacity > m_size )
	{
		ResizeBuffer( m_size );
	}
}

template< typename TElement >
template< typename SortFunction >
RED_INLINE void CircularBuffer< TElement >::Sort(SortFunction&& sortFunction)
{
	if ( Empty() || Size() == 1u )
	{
		return;
	}
	std::sort( CircularArrayIterator< TElement >(this, 0), CircularArrayIterator< TElement >( this, m_size ), sortFunction);
}

template< typename TElement >
template< typename SortFunction >
RED_INLINE void CircularBuffer< TElement >::SortLastN(SortFunction&& sortFunction, Uint32 numElements)
{
	RED_FATAL_ASSERT(numElements <= m_size);

	if (Empty() || Size() == 1u)
	{
		return;
	}
	auto begin = CircularArrayIterator< TElement >(this, m_size - numElements);
	auto end = CircularArrayIterator< TElement >(this, m_size);
	std::sort(begin, end, sortFunction);
}

template < typename TElement >
RED_INLINE void CircularBuffer< TElement >::ResizeBuffer( Uint32 newCapacity )
{
	RED_WARNING( newCapacity < m_capacity || ( newCapacity - m_capacity ) < 0x1000000, "CircularBuffer capacity is rapidly growing. Is that intended?" );

	DynamicBuffer::ResizeBuffer( newCapacity, sizeof( TElement ), __alignof( TElement ), m_size == 0 ? nullptr : MoveAfterReallocation );

	m_front = 0u;
	m_back = ( m_size > 0u ) ? ( m_size - 1 ) : 0u;
}

template < typename TElement >
void CircularBuffer< TElement >::MoveAfterReallocation( void* dst, void* src, Uint32 num, const void* context )
{
	//as a result - all the items are moved to be organized as FXXXXXB-----

	typedef typename policies::MoveConstructorExecutorSelector< TElement >::Type	MoveConstructorExecutor;
	typedef typename policies::DestructorExecutorSelector< TElement >::Type			DestructorExecutor;

	const CircularBuffer* buffer = reinterpret_cast< const CircularBuffer* >( context );
	
	const Uint32 currentSize = buffer->m_size;

	if ( currentSize > 0 )
	{
		TElement* tSrc = reinterpret_cast< TElement* >( src );
		TElement* tDst = reinterpret_cast< TElement* >( dst );

		const Uint32 front = buffer->m_front;
		const Uint32 back = buffer->m_back;

		if ( front <= back )
		{
			//the organization of the array is ---FXXXB----

			TElement* srcFront = tSrc + front;

			if ( num < currentSize )
			{
				MoveConstructorExecutor::Execute( tDst, srcFront, num );
			}
			else
			{
				MoveConstructorExecutor::Execute( tDst, srcFront, currentSize );
			}
			
			DestructorExecutor::Execute( srcFront, currentSize );
		}
		else
		{
			//the organization of the array is XXXB----FXXXX

			TElement* srcFront = tSrc + front;
			
			const Uint32 backSize = back + 1;
			const Uint32 frontSize = currentSize - backSize;

			if ( num < currentSize )
			{
				if ( frontSize < num )
				{
					MoveConstructorExecutor::Execute( tDst, srcFront, frontSize );
					
					auto spaceLeft = num - frontSize;

					if ( backSize < spaceLeft )
					{
						MoveConstructorExecutor::Execute( tDst + frontSize, tSrc, backSize );
					}
					else
					{
						MoveConstructorExecutor::Execute( tDst + frontSize, tSrc, spaceLeft );
					}
				}
				else
				{
					MoveConstructorExecutor::Execute( tDst, srcFront, num );
					//no back copy, no more space
				}
			}
			else
			{
				MoveConstructorExecutor::Execute( tDst, srcFront, frontSize );
				MoveConstructorExecutor::Execute( tDst + frontSize, tSrc, backSize );
			}

			DestructorExecutor::Execute( srcFront, frontSize );
			DestructorExecutor::Execute( tSrc, backSize );
		}
	}
}

//////////////////////////////////////////////////////////////////////////

template < typename TElement >
RED_INLINE void CircularBuffer< TElement >::SetPool( const red::memory::Pool& pool )
{
	RED_FATAL_ASSERT( m_capacity == 0, "Cannot change pool for already allocated CircularBuffer" );
	red::Memcpy( &m_buffer, &pool, sizeof( red::memory::Pool ) );
}

template < typename TElement >
RED_INLINE const red::memory::Pool& CircularBuffer< TElement >::GetPool() const
{
	return *reinterpret_cast< red::memory::Pool* >( DynamicBuffer::GetPoolStoragePtr( sizeof( TElement ) ) );
}

//////////////////////////////////////////////////////////////////////////

template < typename TElement >
RED_INLINE Uint32 CircularBuffer< TElement >::Wrap( Uint32 index ) const
{
	// if the index is too low (unsigned int so 0xFFFFFFFF) set it to the last element
	if ( index > m_capacity ) return m_capacity - 1;
	// if the index is too big, set it to 0
	else if ( index == m_capacity ) return 0;
	// otherwise index is fine
	return index;
}

} // red
