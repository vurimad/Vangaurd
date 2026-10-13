/*
* Copyright (c) 2014-16 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "policies.h"

namespace red {

//////////////////////////////////////////////////////////////////////////

namespace internal {
	template< typename TElement >
	RED_INLINE constexpr Uint32 CalculateExtraSpaceForAlignment()
	{
		return sizeof( TElement ) == alignof( TElement ) ? 0 : alignof( TElement ) - 1;
	}
}

template < typename TElement, typename THashPolicy >
RED_INLINE HashSet< TElement, THashPolicy >::HashSet( const red::memory::Pool& pool )
	: m_elements( nullptr )
	, m_buckets( nullptr )
	, m_size( 0 )
	, m_capacity( 0 )
{
	SetPool( pool );
}

template < typename TElement, typename THashPolicy >
RED_INLINE HashSet< TElement, THashPolicy >::HashSet( Uint32 initialCapacity, const red::memory::Pool& pool )
	: m_elements( nullptr )
	, m_buckets( nullptr )
	, m_size( 0 )
	, m_capacity( 0 )
{
	SetPool( pool );
	Rehash( initialCapacity );
}

template < typename TElement, typename THashPolicy >
RED_INLINE HashSet< TElement, THashPolicy >::HashSet( const HashSet& other )
	: m_elements( nullptr )
	, m_buckets( nullptr )
	, m_size( 0 )
	, m_capacity( 0 )
	, m_pool( other.m_pool )
{
	const Uint32 otherSize = other.m_size;
	Rehash( otherSize );
	for ( Uint32 i = 0; i < otherSize; ++i )
	{
		Insert( other.m_elements[ i ] );
	}
}

template < typename TElement, typename THashPolicy >
RED_INLINE HashSet< TElement, THashPolicy >::HashSet( HashSet&& other )
	: m_elements( other.m_elements )
	, m_buckets( other.m_buckets )
	, m_size( other.m_size )
	, m_capacity( other.m_capacity )
	, m_elementsPool( std::move( other.m_elementsPool ) )
	, m_pool( other.m_pool )
{
	other.m_elements = nullptr;
	other.m_buckets = nullptr;
	other.m_size = 0;
	other.m_capacity = 0;
}

template < typename TElement, typename THashPolicy >
RED_INLINE HashSet< TElement, THashPolicy >::HashSet( std::initializer_list< TElement > initializerList, const red::memory::Pool& pool )
	: HashSet( pool )
{
	Rehash( static_cast< Uint32 >( initializerList.size() ) );
	for ( const TElement& element : initializerList )
	{
		Insert( element );
	}
}

template < typename TElement, typename THashPolicy >
RED_INLINE HashSet< TElement, THashPolicy >::~HashSet()
{
	typedef typename policies::DestructorExecutorSelector< TElement >::Type DestructorExecutor;

	DestructorExecutor::Execute( m_elements, m_size );
	m_size = 0;
	Rehash( 0 ); // Shrink
}

//////////////////////////////////////////////////////////////////////////

template < typename TElement, typename THashPolicy >
RED_INLINE HashSet< TElement, THashPolicy >& HashSet< TElement, THashPolicy >::operator=( const HashSet& other )
{
	HashSet( other ).Swap( *this ); 
	return *this;
}

template < typename TElement, typename THashPolicy >
RED_INLINE HashSet< TElement, THashPolicy >& HashSet< TElement, THashPolicy >::operator=( HashSet&& other )
{
	HashSet( std::move( other ) ).Swap( *this ); 
	return *this;
}

template < typename TElement, typename THashPolicy >
RED_INLINE HashSet< TElement, THashPolicy >& HashSet< TElement, THashPolicy >::operator=( std::initializer_list< TElement > initializerList )
{
	HashSet( initializerList ).Swap( *this );
	return *this;
}

template < typename TElement, typename THashPolicy >
RED_INLINE void HashSet< TElement, THashPolicy >::Swap( HashSet& other )
{
	using std::swap;
	swap( m_elements, other.m_elements );
	swap( m_buckets, other.m_buckets );
	swap( m_size, other.m_size );
	swap( m_capacity, other.m_capacity );
	m_elementsPool.Swap( other.m_elementsPool );
	swap( m_pool, other.m_pool );
}

//////////////////////////////////////////////////////////////////////////

template < typename TElement, typename THashPolicy >
template < typename TCompatibleType >
const TElement& HashSet< TElement, THashPolicy >::GetRef( const TCompatibleType& element, const TElement& defaultValue )
{
	TElement* foundElement = FindInternal( element );
	return ( foundElement != nullptr ) ? *foundElement : *Insert( defaultValue ).Iterator();
}

template < typename TElement, typename THashPolicy >
RED_INLINE void HashSet< TElement, THashPolicy >::GetElements( DynArray< TElement >& elements ) const
{
	elements.Clear();
	elements.Reserve( m_size );
	for ( Uint32 i = 0; i < m_size; ++i )
	{
		elements.PushBack( m_elements[ i ] );
	}
}

template < typename TElement, typename THashPolicy >
ArraySpan< const TElement > HashSet< TElement, THashPolicy >::Elements() const
{
	return red::ArraySpan< const TElement >( m_elements, m_size );
}

//////////////////////////////////////////////////////////////////////////

template < typename TElement, typename THashPolicy >
RED_INLINE Bool HashSet< TElement, THashPolicy >::operator==( const HashSet& other ) const
{
	const Uint32 size = Size();
	if ( size != other.Size() )
	{
		return false;
	}

	for ( Uint32 i = 0; i < size; ++i )
	{
		if ( !other.FindInternal( m_elements[ i ] ) )
		{
			return false;
		}
	}

	return true;
}

template < typename TElement, typename THashPolicy >
RED_INLINE Bool HashSet< TElement, THashPolicy >::operator!=( const HashSet& other ) const
{
	return !( *this == other );
}

//////////////////////////////////////////////////////////////////////////

template < typename TElement, typename THashPolicy >
typename HashSet< TElement, THashPolicy >::Result HashSet< TElement, THashPolicy >::Insert( const TElement& element )
{
	Uint32 hash = 0;
	TElement* elementFound = FindInternal( element, &hash );
	if ( elementFound != nullptr )
	{
		return Result::Failure( iterator( elementFound ) );
	}
	return Result::Success( iterator( InsertNoFail( element, hash ) ) );
}

template < typename TElement, typename THashPolicy >
typename HashSet< TElement, THashPolicy >::Result HashSet< TElement, THashPolicy >::Insert( TElement&& element )
{
	Uint32 hash = 0;
	TElement* elementFound = FindInternal( element, &hash );
	if ( elementFound != nullptr )
	{
		return Result::Failure( iterator( elementFound ) );
	}
	return Result::Success( iterator( InsertNoFail( std::forward< TElement >( element ), hash ) ) );
}

template < typename TElement, typename THashPolicy >
template < typename TElementRef >
TElement*  HashSet< TElement, THashPolicy >::InsertNoFail( TElementRef&& element, Uint32 hash )
{
	// Make sure there's enough room for new element
	if ( m_size == m_capacity )
	{
		Upsize();
	}

	// Insert element
	TElement* newElement = ::new ( m_elements + m_size ) TElement( std::forward< TElementRef >( element ) );

	// Try to insert into bucket head
	const Uint32 bucketIndex = hash % m_capacity;
	Bucket* bucket = m_buckets + bucketIndex;
	if ( !bucket->IsUsed() )
	{
		bucket->m_hash = hash;
		bucket->m_index = m_size;
		++m_size;
		return newElement;
	}

	// Insert into linked list
	const Uint32 newBucketElementId = m_elementsPool.AllocateBlockIndex();
	BucketElement* newBucketElement = static_cast< BucketElement* >( m_elementsPool.GetBlock( newBucketElementId ) );
	RED_FATAL_ASSERT( newBucketElement, "" );
	newBucketElement->m_hash = hash;
	newBucketElement->m_index = m_size;
	newBucketElement->m_nextId = bucket->m_nextId;
	bucket->m_nextId = newBucketElementId;

	++m_size;

	return newElement;
}

template < typename TElement, typename THashPolicy >
template < typename... Args >
typename HashSet< TElement, THashPolicy >::Result HashSet< TElement, THashPolicy >::Emplace( Args&&... args )
{
	// Make sure there's enough room for new element
	if ( m_size == m_capacity )
	{
		Upsize();
	}

	// Insert element
	TElement* newElement = new ( m_elements + m_size ) TElement( std::forward< Args >( args )... );

	// Try to insert into bucket head
	Uint32 hash = HashFunc::GetHash( *newElement );
	const Uint32 bucketIndex = hash % m_capacity;
	Bucket* bucket = m_buckets + bucketIndex;
	if ( !bucket->IsUsed() )
	{
		bucket->m_hash = hash;
		bucket->m_index = m_size;
		++m_size;
		return Result::Success( iterator( newElement ) );;
	}

	// Search for duplicate in linked list
	Uint32 foundIndex;
	Bool found = ( bucket->m_hash == hash && EqualFunc::Equal( m_elements[ foundIndex = bucket->m_index ], *newElement ) );
	Uint32 currentId = bucket->m_nextId;
	while ( !found && currentId != INVALID_INDEX )
	{
		const BucketElement* current = static_cast< const BucketElement* >( m_elementsPool.GetBlock( currentId ) );
		found = ( current->m_hash == hash && EqualFunc::Equal( m_elements[ foundIndex = current->m_index ], *newElement ) );
		currentId = current->m_nextId;
	}

	// Found duplicate -> destroy created (new) element and return iterator to the previous one
	if ( found )
	{
		newElement->~TElement();
		return Result::Failure( iterator( m_elements + foundIndex ) );
	}

	// Insert into linked list
	const Uint32 newBucketElementId = m_elementsPool.AllocateBlockIndex();
	BucketElement* newBucketElement = static_cast< BucketElement* >( m_elementsPool.GetBlock( newBucketElementId ) );
	RED_FATAL_ASSERT( newBucketElement, "" );
	newBucketElement->m_hash = hash;
	newBucketElement->m_index = m_size;
	newBucketElement->m_nextId = bucket->m_nextId;
	bucket->m_nextId = newBucketElementId;

	++m_size;

	return Result::Success( iterator( newElement ) );
}

template < typename TElement, typename THashPolicy >
template < typename TCompatibleType >
typename HashSet< TElement, THashPolicy >::Result HashSet< TElement, THashPolicy >::Remove( const TCompatibleType& element )
{
	if ( !m_size )
	{
		return Result::Failure();
	}

	const Uint32 hash = HashFunc::GetHash( element );
	const Uint32 bucketIndex = hash % m_capacity;

	Bucket* bucket = m_buckets + bucketIndex;

	// Check head
	if ( bucket->IsUsed() )
	{
		Uint32 index;
		if ( bucket->m_hash == hash && EqualFunc::Equal( m_elements[ index = bucket->m_index ], element ) )
		{
			// Remove element
			RemoveElementAt( index );

			// Update list
			if ( bucket->m_nextId == INVALID_INDEX )
			{
				bucket->SetUnused();
			}
			else
			{
				BucketElement* next = static_cast< BucketElement* >( m_elementsPool.GetBlock( bucket->m_nextId ) );
				*( BucketElement* ) bucket = *next;
				m_elementsPool.FreeBlock( next );
			}

			return Result::Success( iterator( &m_elements[ index ] ) );
		}

		// Check linked list
		Uint32* currentId = &bucket->m_nextId;
		while ( *currentId != INVALID_INDEX )
		{
			BucketElement* current = static_cast< BucketElement* >( m_elementsPool.GetBlock( *currentId ) );
			Uint32 index;
			if ( current->m_hash == hash && EqualFunc::Equal( m_elements[ index = current->m_index ], element ) )
			{
				// Remove element
				RemoveElementAt( index );

				// Update linked list
				*currentId = current->m_nextId;
				m_elementsPool.FreeBlock( current );

				return Result::Success( iterator( &m_elements[ index ] ) );
			}
			currentId = &current->m_nextId;
		}
	}

	return Result::Failure();
}

template < typename TElement, typename THashPolicy >
RED_INLINE typename HashSet< TElement, THashPolicy >::Result HashSet< TElement, THashPolicy >::Remove( iterator it )
{
	return Remove( *it.m_current );
}

template < typename TElement, typename THashPolicy >
void HashSet< TElement, THashPolicy >::RemoveElementAt( Uint32 index )
{
	--m_size;
	if ( index < m_size )
	{
		// Move the last element into free slot
		m_elements[ index ] = std::move( m_elements[ m_size ] );
		m_elements[ m_size ].~TElement();

		// Update pointer to moved element
		const TElement& movedKey = m_elements[ index ];
		const Uint32 hash = HashFunc::GetHash( movedKey );
		const Uint32 bucketIndex = hash % m_capacity;

		Bucket* bucket = m_buckets + bucketIndex;
		if ( bucket->IsUsed() )
		{
			if ( bucket->m_index == m_size )
			{
				bucket->m_index = index;
				return;
			}

			Uint32 currentId = bucket->m_nextId;
			while ( currentId != INVALID_INDEX )
			{
				BucketElement* current = static_cast< BucketElement* >( m_elementsPool.GetBlock( currentId ) );
				if ( current->m_index == m_size )
				{
					current->m_index = index;
					return;
				}
				currentId = current->m_nextId;
			}
		}

		RED_ASSERT( !"Element to remove was not found in bucket. Should _never_ happen since its hash is there." );
		return;
	}

	m_elements[ index ].~TElement();
}

//////////////////////////////////////////////////////////////////////////

template < typename TElement, typename THashPolicy >
RED_INLINE void HashSet< TElement, THashPolicy >::Clear()
{
	typedef typename policies::DestructorExecutorSelector< TElement >::Type DestructorExecutor;

	if ( m_capacity )
	{
		DestructorExecutor::Execute( m_elements, m_size );
		m_size = 0;

		m_elementsPool.Clear();
		for ( Uint32 i = 0; i < m_capacity; ++i )
		{
			m_buckets[ i ].SetUnused();
			m_buckets[ i ].m_nextId = static_cast< Uint32 >( INVALID_INDEX );
		}

#if defined( RED_MEMORY_FORCE_DEBUG_ALLOCATOR )
		Shrink();
#endif
	}
}

template < typename TElement, typename THashPolicy >
RED_INLINE void HashSet< TElement, THashPolicy >::Reserve( Uint32 newCapacity )
{
	if ( m_capacity < newCapacity )
	{
		Rehash( newCapacity );
	}
}

template < typename TElement, typename THashPolicy >
RED_INLINE void HashSet< TElement, THashPolicy >::Shrink()
{
	Rehash( m_size );
}

template < typename TElement, typename THashPolicy >
RED_INLINE void HashSet< TElement, THashPolicy >::Upsize()
{
	RED_ASSERT( m_size == m_capacity );

	// Grow to 150 % of the new size (and the minimum of 4)
	const Uint32 newCapacity = std::max( ( Uint32 ) 4, m_size + ( m_size >> 1 ) );
	Rehash( newCapacity );
}

template < typename TElement, typename THashPolicy >
void HashSet< TElement, THashPolicy >::Rehash( Uint32 newCapacity )
{
	RED_FATAL_ASSERT( newCapacity >= m_size, "" );

	if ( newCapacity == m_capacity )
	{
		return;
	}

	if ( !newCapacity )
	{
		RED_FREE( GetPoolRef(), m_elementsPool.Data() );
		m_elementsPool.Deinit();
		m_elements = nullptr;
		m_capacity = 0;
		return;
	}

	// Copy old data aside
	Uint32 oldSize			= m_size;
	Uint32 oldCapacity		= m_capacity;
	Pool oldElementsPool;
	oldElementsPool			= std::move( m_elementsPool );
	Bucket* oldBuckets		= m_buckets;
	TElement* oldElements	= m_elements;

	// Allocate single memory block for the whole hash set
	const Uint32 newPoolSize		= newCapacity * sizeof( BucketElement );
	const Uint32 newBucketsSize		= newCapacity * sizeof( Bucket );
	const Uint32 newElementsSize = ( newCapacity * sizeof( TElement ) ) + internal::CalculateExtraSpaceForAlignment< TElement >();
	const Uint32 newMemorySize		= newPoolSize + newBucketsSize + newElementsSize;
	Uint8* newMemory = ( Uint8* ) RED_ALLOCATE( GetPoolRef(), newMemorySize );
	const Uint64 newMemoryEnd = reinterpret_cast< Uint64 >( newMemory ) + newMemorySize;

	// Initialize new internal containers
	m_capacity = newCapacity;
	m_size = 0;
	m_elementsPool.Init( newMemory, newPoolSize, sizeof( BucketElement ) );
	newMemory += newPoolSize;
	m_buckets = ( Bucket* ) newMemory;
	newMemory += newBucketsSize;
	newMemory = reinterpret_cast< Uint8* >( red::AlignAddress( newMemory, alignof( TElement ) ) );
	RED_FATAL_ASSERT( reinterpret_cast< Uint64 >( newMemory ) + ( newCapacity * sizeof( TElement ) ) <= newMemoryEnd, "Out of memory buffer" );
	RED_UNUSED( newMemoryEnd );
	m_elements = ( TElement* ) newMemory;
	RED_ASSERT( IsAligned( m_elements, alignof( TElement ) ), "Buffer for elements is not properly aligned" );

	for ( Uint32 i = 0; i < newCapacity; ++i )
	{
		m_buckets[ i ].SetUnused();
		m_buckets[ i ].m_nextId =
			static_cast< Uint32 >( INVALID_INDEX );
	}

	if ( oldSize )
	{
		// Move all elements to new containers
		for ( Uint32 i = 0; i < oldCapacity; ++i )
		{
			Bucket* oldBucket = oldBuckets + i;
			if ( oldBucket->IsUsed() )
			{
				TElement* element = oldElements + oldBucket->m_index;
				InsertNoFail( std::move( *element ), oldBucket->m_hash );
				element->~TElement();

				// Move linked list
				Uint32 currentId = oldBucket->m_nextId;
				while ( currentId != INVALID_INDEX )
				{
					BucketElement* current = static_cast< BucketElement* >( oldElementsPool.GetBlock( currentId ) );
					TElement* linkedElement =
						oldElements + current->m_index;
					InsertNoFail(
						std::move( *linkedElement ),
						current->m_hash );
					linkedElement->~TElement();
					currentId = current->m_nextId;
				}
			}
		}
	}

	// Delete old containers
	if ( oldCapacity )
	{
		RED_FREE( GetPoolRef(), oldElementsPool.Data() );
	}
}

//////////////////////////////////////////////////////////////////////////

template < typename TElement, typename THashPolicy >
template < typename TCompatibleType >
RED_INLINE typename HashSet< TElement, THashPolicy >::iterator HashSet< TElement, THashPolicy >::Find( const TCompatibleType& element ) const
{
	const TElement* found = FindInternal( element );
	return found ? iterator( found ) : End();
}

template < typename TElement, typename THashPolicy >
template < typename TCompatibleType >
RED_INLINE const TElement* HashSet< TElement, THashPolicy >::FindPtr( const TCompatibleType& element ) const
{
	return FindInternal( element );
}

template < typename TElement, typename THashPolicy >
template < typename TCompatibleType >
RED_INLINE Bool HashSet< TElement, THashPolicy >::Exist( const TCompatibleType& element ) const
{
	return FindInternal( element ) != nullptr;
}

template < typename TElement, typename THashPolicy >
template < typename TCompatibleType >
RED_INLINE TElement* HashSet< TElement, THashPolicy >::FindInternal( const TCompatibleType& element, Uint32* outHash /* = nullptr */ )
{
	return const_cast< TElement* >( const_cast< const HashSet< TElement, THashPolicy >* >( this )->FindInternal( element, outHash ) );
}

template < typename TElement, typename THashPolicy >
template < typename TCompatibleType >
const TElement* HashSet< TElement, THashPolicy >::FindInternal( const TCompatibleType& element, Uint32* outHash /* = nullptr */ ) const
{
	if ( !m_size )
	{
		if ( outHash != nullptr )
		{
			*outHash = HashFunc::GetHash( element );
		}
		return nullptr;
	}

	const Uint32 hash = HashFunc::GetHash( element );
	if ( outHash != nullptr )
	{
		*outHash = hash;
	}
	const Uint32 bucketIndex = hash % m_capacity;

	// Search
	const Bucket* bucket = m_buckets + bucketIndex;
	if ( bucket->IsUsed() )
	{
		Uint32 currentIndex;
		if ( bucket->m_hash == hash && EqualFunc::Equal( m_elements[ currentIndex = bucket->m_index ], element ) )
		{
			return m_elements + currentIndex;
		}

		// Search in linked list
		Uint32 currentId = bucket->m_nextId;
		while ( currentId != INVALID_INDEX )
		{
			const BucketElement* current = static_cast< const BucketElement* >( m_elementsPool.GetBlock( currentId ) );
			Uint32 linkedIndex;
			if ( current->m_hash == hash && EqualFunc::Equal( m_elements[ linkedIndex = current->m_index ], element ) )
			{
				return m_elements + linkedIndex;
			}
			currentId = current->m_nextId;
		}
	}

	// Not found
	return nullptr;
}

//////////////////////////////////////////////////////////////////////////

template < typename TElement, typename THashPolicy >
RED_INLINE void HashSet< TElement, THashPolicy >::Union( const HashSet& other )
{
	const Uint32 otherSize = other.m_size;
	for ( Uint32 i = 0; i < otherSize; ++i )
	{
		Insert( other.m_elements[ i ] );
	}
}

template < typename TElement, typename THashPolicy >
RED_INLINE void HashSet< TElement, THashPolicy >::Intersection( const HashSet& other )
{
	HashSet newSet( Capacity(), GetPool() );
	const Uint32 otherSize = other.m_size;
	for ( Uint32 i = 0; i < otherSize; ++i )
	{
		TElement& element = other.m_elements[ i ];
		if ( FindInternal( element ) != nullptr )
		{
			newSet.Insert( element );
		}
	}
	Swap( newSet );
}

template < typename TElement, typename THashPolicy >
RED_INLINE void HashSet< TElement, THashPolicy >::Difference( const HashSet& other )
{
	const Uint32 otherSize = other.m_size;
	for ( Uint32 i = 0; i < otherSize; ++i )
	{
		Remove( other.m_elements[ i ] );
	}
}

//////////////////////////////////////////////////////////////////////////

template < typename TElement, typename THashPolicy >
RED_INLINE void HashSet< TElement, THashPolicy >::SetPool( const red::memory::Pool& pool )
{
	RED_FATAL_ASSERT( m_capacity == 0, "Cannot change pool for already allocated HashSet" );
	red::Memcpy( &m_pool, &pool, sizeof( red::memory::Pool ) );
}

template < typename TElement, typename THashPolicy >
RED_INLINE const red::memory::Pool& HashSet< TElement, THashPolicy >::GetPool() const
{
	return const_cast< HashSet* >( this )->GetPoolRef();
}

template < typename TElement, typename THashPolicy >
RED_INLINE red::memory::Pool& HashSet< TElement, THashPolicy >::GetPoolRef()
{
	return reinterpret_cast< red::memory::Pool& >( m_pool );
}

} // red
