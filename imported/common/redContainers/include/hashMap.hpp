/*
* Copyright (c) 2014-16 CD Projekt Red. All Rights Reserved.
*/

#pragma once

namespace red {

//////////////////////////////////////////////////////////////////////////

template < typename TKey, typename TValue, typename THashPolicy >
RED_INLINE HashMap< TKey, TValue, THashPolicy >::HashMap( const red::memory::Pool& pool )
	: m_buckets( nullptr )
	, m_size( 0 )
	, m_capacity( 0 )
{
	SetPool( pool );
}

template < typename TKey, typename TValue, typename THashPolicy >
RED_INLINE HashMap< TKey, TValue, THashPolicy >::HashMap( Uint32 initialCapacity, const red::memory::Pool& pool )
	: m_buckets( nullptr )
	, m_size( 0 )
	, m_capacity( 0 )
{
	SetPool( pool );
	Rehash( initialCapacity );
}

template < typename TKey, typename TValue, typename THashPolicy >
RED_INLINE HashMap< TKey, TValue, THashPolicy >::HashMap( const HashMap& other )
	: m_buckets( nullptr )
	, m_size( 0 )
	, m_capacity( 0 )
	, m_pool( other.m_pool )
{
	Rehash( other.Size() );
	for ( auto it = other.Begin(), end = other.End(); it != end; ++it )
	{
		Insert( it.Key(), it.Value() );
	}
}

template < typename TKey, typename TValue, typename THashPolicy >
RED_INLINE HashMap< TKey, TValue, THashPolicy >::HashMap( HashMap&& other )
	: m_buckets( other.m_buckets )
	, m_size( other.m_size )
	, m_capacity( other.m_capacity )
	, m_bucketsPool( std::move( other.m_bucketsPool ) )
	, m_pool( other.m_pool )
{
	other.m_buckets = nullptr;
	other.m_size = 0;
	other.m_capacity = 0;
}

template < typename TKey, typename TValue, typename THashPolicy >
RED_INLINE HashMap< TKey, TValue, THashPolicy >::~HashMap()
{
	Clear();
	Rehash( 0 );  // Shrink
}

//////////////////////////////////////////////////////////////////////////

template < typename TKey, typename TValue, typename THashPolicy >
RED_INLINE HashMap< TKey, TValue, THashPolicy >& HashMap< TKey, TValue, THashPolicy >::operator=( const HashMap& other )
{
	if( &other != this )
	{
		Clear();
		Rehash( other.Size() );
		for ( auto it = other.Begin(), end = other.End(); it != end; ++it )
		{
			Insert( it.Key(), it.Value() );
		}
	}
	
	return *this;
}

template < typename TKey, typename TValue, typename THashPolicy >
RED_INLINE HashMap< TKey, TValue, THashPolicy >& HashMap< TKey, TValue, THashPolicy >::operator=( HashMap&& other )
{
	HashMap( std::move( other ) ).Swap( *this );
	return *this;
}

template < typename TKey, typename TValue, typename THashPolicy >
void HashMap< TKey, TValue, THashPolicy >::Swap( HashMap& other )
{
	using std::swap;
	swap( m_buckets, other.m_buckets );
	swap( m_size, other.m_size );
	swap( m_capacity, other.m_capacity );
	m_bucketsPool.Swap( other.m_bucketsPool );
	swap( m_pool, other.m_pool );
}

//////////////////////////////////////////////////////////////////////////

template < typename TKey, typename TValue, typename THashPolicy >
RED_INLINE TValue& HashMap< TKey, TValue, THashPolicy >::operator[]( const TKey& key )
{
	struct DefaultValue
	{
		RED_INLINE TValue operator()() const { return TValue(); }
	};
	return GetRefOrInsert( key, DefaultValue() );
}

template < typename TKey, typename TValue, typename THashPolicy >
RED_INLINE const TValue& HashMap< TKey, TValue, THashPolicy >::operator[]( const TKey& key ) const
{
	Uint32 bucketIndex;
	const BucketElement* element = nullptr;
	RED_VERIFY( FindInternal( key, bucketIndex, element ) );
	RED_FATAL_ASSERT( element, "Key does not exist in hashmap. Cannot continue." );
	return element->m_value;
}

template < typename TKey, typename TValue, typename THashPolicy >
template < typename TCompatibleType >
RED_INLINE TValue& HashMap< TKey, TValue, THashPolicy >::GetRef( const TCompatibleType& key, const TValue& defaultValue /* = TValue() */ )
{
	struct ValueRef
	{
		const TValue& m_valueRef;
		RED_INLINE ValueRef( const TValue& valueRef ) : m_valueRef( valueRef ) {};
		RED_INLINE const TValue& operator()() const { return m_valueRef; }
	};
	return GetRefOrInsert( key, ValueRef( defaultValue ) );
}

template < typename TKey, typename TValue, typename THashPolicy >
template < typename TCompatibleType, typename TValueProxy >
TValue& HashMap< TKey, TValue, THashPolicy >::GetRefOrInsert( const TCompatibleType& key, const TValueProxy& valueProxy )
{
	const Uint32 hash = HashFunc::GetHash( key );

	// Check if key already exists
	Uint32 bucketIndex = 0;
	if ( m_size )
	{
		bucketIndex = hash % m_capacity;

		// Already exists? -> return value
		ElementIndex elementIndex = m_buckets[ bucketIndex ];
		while ( elementIndex != InvalidElementIndex )
		{
			BucketElement* element = static_cast< BucketElement* >( m_bucketsPool.GetBlock( elementIndex ) );
			if ( element->m_hash == hash && EqualFunc::Equal( element->m_key, key ) )
			{
				return element->m_value;
			}
			elementIndex = element->m_nextIndex;
		}
	}

	// Make sure there's enough room for new element
	if ( PreInsert() || !m_size )
	{
		bucketIndex = hash % m_capacity; // Recalculate bucket index as it might change after rehash
	}

	// Insert
	BucketElement* newElement = new ( m_bucketsPool ) BucketElement( key, valueProxy() );
	RED_FATAL_ASSERT( newElement, "" );
	newElement->m_hash = hash;

	newElement->m_nextIndex = m_buckets[ bucketIndex ];
	m_buckets[ bucketIndex ] = m_bucketsPool.GetBlockIndex( newElement );

	++m_size;

	// Return new value
	return newElement->m_value;
}

template < typename TKey, typename TValue, typename THashPolicy >
RED_INLINE void HashMap< TKey, TValue, THashPolicy >::GetKeys( DynArray< TKey >& keys ) const
{
	keys.Clear();
	keys.Reserve( Size() );
	for ( auto it = Begin(), end = End(); it != end; ++it )
	{
		keys.PushBack( it.Key() );
	}
}

template < typename TKey, typename TValue, typename THashPolicy >
RED_INLINE DynArray< TKey > HashMap< TKey, TValue, THashPolicy >::GetKeys() const
{
	DynArray< TKey > result{ GetPool() };
	result.Reserve( Size() );
	for ( auto it = Begin(), end = End(); it != end; ++it )
	{
		result.PushBack( it.Key() );
	}
	return result;
}

template < typename TKey, typename TValue, typename THashPolicy >
RED_INLINE void HashMap< TKey, TValue, THashPolicy >::GetValues( DynArray< TValue >& values ) const
{
	values.Clear();
	values.Reserve( Size() );
	for ( auto it = Begin(), end = End(); it != end; ++it )
	{
		values.PushBack( it.Value() );
	}
}

template < typename TKey, typename TValue, typename THashPolicy >
RED_INLINE DynArray< TValue > HashMap< TKey, TValue, THashPolicy >::GetValues() const
{
	DynArray< TValue > result{ GetPool() };
	result.Reserve( Size() );
	for ( auto it = Begin(), end = End(); it != end; ++it )
	{
		result.PushBack( it.Value() );
	}
	return result;
}

//////////////////////////////////////////////////////////////////////////

template < typename TKey, typename TValue, typename THashPolicy >
RED_INLINE Bool HashMap< TKey, TValue, THashPolicy >::operator==( const HashMap& other ) const
{
	if ( Size() != other.Size() )
	{
		return false;
	}

	for ( auto it = Begin(), end = End(); it != end; ++it )
	{
		if ( const TValue* otherValue = other.FindPtr( it.Key() ) )
		{
			if ( it.Value() != *otherValue )
			{
				return false;
			}
		}
		else
		{
			return false;
		}
	}

	return true;
}

template < typename TKey, typename TValue, typename THashPolicy >
RED_INLINE Bool HashMap< TKey, TValue, THashPolicy >::operator!=( const HashMap& other ) const
{
	return !( *this == other );
}

//////////////////////////////////////////////////////////////////////////

template < typename TKey, typename TValue, typename THashPolicy >
RED_INLINE typename HashMap< TKey, TValue, THashPolicy >::Result HashMap< TKey, TValue, THashPolicy >::Insert( const TKey& key, const TValue& value )
{
	return InsertInternal< InsertPolicy >( key, value );

}

template < typename TKey, typename TValue, typename THashPolicy >
RED_INLINE typename HashMap< TKey, TValue, THashPolicy >::Result HashMap< TKey, TValue, THashPolicy >::Insert( const TKey& key, TValue&& value )
{
	return InsertInternal< InsertPolicy >( key, std::forward< TValue >( value ) );
}

template < typename TKey, typename TValue, typename THashPolicy >
RED_INLINE typename HashMap< TKey, TValue, THashPolicy >::Result HashMap< TKey, TValue, THashPolicy >::Set( const TKey& key, const TValue& value )
{
	return InsertInternal< SetPolicy >( key, value );
}

template < typename TKey, typename TValue, typename THashPolicy >
RED_INLINE typename HashMap< TKey, TValue, THashPolicy >::Result HashMap< TKey, TValue, THashPolicy >::Set( const TKey& key, TValue&& value )
{
	return InsertInternal< SetPolicy >( key, std::forward< TValue >( value ) );
}

template < typename TKey, typename TValue, typename THashPolicy >
template < typename... Args >
RED_INLINE typename HashMap< TKey, TValue, THashPolicy >::Result HashMap< TKey, TValue, THashPolicy >::Emplace( const TKey& key, Args&&... args )
{
	return InsertInternal< InsertPolicy >( key, std::forward< Args >( args )... );
}

template < typename TKey, typename TValue, typename THashPolicy >
template < typename TDuplicatePolicy, typename TValueRef, typename... Args >
typename HashMap< TKey, TValue, THashPolicy >::Result HashMap< TKey, TValue, THashPolicy >::InsertInternal( const TKey& key, TValueRef&& value, Args&&... args )
{
	const Uint32 hash = HashFunc::GetHash( key );

	// Check if key already exists
	Uint32 bucketIndex = 0;
	if ( m_size )
	{
		bucketIndex = hash % m_capacity;

		// Already exists? -> Fail
		ElementIndex elementIndex = m_buckets[ bucketIndex ];
		while ( elementIndex != InvalidElementIndex )
		{
			BucketElement* element = static_cast< BucketElement* >( m_bucketsPool.GetBlock( elementIndex ) );
			if ( element->m_hash == hash && EqualFunc::Equal( element->m_key, key ) )
			{
				return TDuplicatePolicy::Execute( this, bucketIndex, element, value );
			}
			elementIndex = element->m_nextIndex;
		}
	}

	// Make sure there's enough room for new element
	if ( PreInsert() || !m_size )
	{
		bucketIndex = hash % m_capacity; // Recalculate bucket index as it might change after rehash
	}

	// Insert
	BucketElement* newElement = new ( m_bucketsPool ) BucketElement( key, std::forward< TValueRef >( value ), std::forward< Args >( args )... );
	RED_FATAL_ASSERT( newElement, "" );
	newElement->m_hash = hash;
	newElement->m_nextIndex = m_buckets[ bucketIndex ];
	m_buckets[ bucketIndex ] = m_bucketsPool.GetBlockIndex( newElement );

	++m_size;

	return Result::Success( iterator( this, bucketIndex, newElement ) );
}

template < typename TKey, typename TValue, typename THashPolicy >
typename HashMap< TKey, TValue, THashPolicy >::Result HashMap< TKey, TValue, THashPolicy >::Remove( const TKey& key )
{
	if ( !m_size )
	{
		return Result::Failure();
	}

	const Uint32 hash = HashFunc::GetHash( key );
	const Uint32 bucketIndex = hash % m_capacity;

	ElementIndex* currentIndex = &m_buckets[ bucketIndex ];
	while ( *currentIndex != InvalidElementIndex )
	{
		BucketElement* currentElement = static_cast< BucketElement* >( m_bucketsPool.GetBlock( *currentIndex ) );
		if ( currentElement->m_hash == hash && EqualFunc::Equal( currentElement->m_key, key ) )
		{
			iterator itNext( this, bucketIndex, currentElement );
			++itNext;
			*currentIndex = currentElement->m_nextIndex;
			currentElement->Destroy();
			m_bucketsPool.FreeBlock( currentElement );
			--m_size;
			return Result::Success( itNext );
		}
		currentIndex = &currentElement->m_nextIndex;
	}

	return Result::Failure();
}

template < typename TKey, typename TValue, typename THashPolicy >
typename HashMap< TKey, TValue, THashPolicy >::Result HashMap< TKey, TValue, THashPolicy >::Remove( const_iterator it )
{
	if ( !m_size )
	{
		return Result::Failure();
	}

	ElementIndex* currentIndex = &m_buckets[ it.m_bucketIndex ];
	while ( *currentIndex != InvalidElementIndex )
	{
		BucketElement* currentElement = static_cast< BucketElement* >( m_bucketsPool.GetBlock( *currentIndex ) );
		if ( currentElement == it.m_element )
		{
			iterator itNext( this, it.m_bucketIndex, currentElement );
			++itNext;
			*currentIndex = currentElement->m_nextIndex;
			currentElement->Destroy();
			m_bucketsPool.FreeBlock( currentElement );
			--m_size;
			return Result::Success( itNext );
		}
		currentIndex = &currentElement->m_nextIndex;
	}

	return Result::Failure();
}

template < typename TKey, typename TValue, typename THashPolicy >
RED_INLINE typename HashMap< TKey, TValue, THashPolicy >::Result HashMap< TKey, TValue, THashPolicy >::RemoveValue( const TValue& value )
{
	for ( iterator it = Begin(), end = End(); it != end; ++it )
	{
		if ( it.Value() == value )
		{
			return Remove( it );
		}
	}

	return Result::Failure();
}

template < typename TKey, typename TValue, typename THashPolicy >
RED_INLINE Bool HashMap< TKey, TValue, THashPolicy >::PreInsert()
{
	if ( m_size + 1 > m_capacity )
	{
		// Grow to 150 % of the new size (and the minimum of 4)
		const Uint32 newCapacity = std::max( (Uint32) 4, m_size + ( m_size >> 1 ) );
		Rehash( newCapacity );
		return true;
	}
	return false;
}

//////////////////////////////////////////////////////////////////////////

template < typename TKey, typename TValue, typename THashPolicy >
template < typename TCompatibleType >
RED_INLINE typename HashMap< TKey, TValue, THashPolicy >::iterator HashMap< TKey, TValue, THashPolicy >::Find( const TCompatibleType& key )
{
	Uint32 bucketIndex;
	BucketElement* element;
	return FindInternal( key, bucketIndex, element ) ? iterator( this, bucketIndex, element ) : End();
}

template < typename TKey, typename TValue, typename THashPolicy >
template < typename TCompatibleType >
RED_INLINE typename HashMap< TKey, TValue, THashPolicy >::const_iterator HashMap< TKey, TValue, THashPolicy >::Find( const TCompatibleType& key ) const
{
	Uint32 bucketIndex;
	const BucketElement* element;
	return FindInternal( key, bucketIndex, element ) ? const_iterator( this, bucketIndex, element ) : End();
}

template < typename TKey, typename TValue, typename THashPolicy >
template < typename TCompatibleType >
RED_INLINE Bool HashMap< TKey, TValue, THashPolicy >::Find( const TCompatibleType& key, TValue& value ) const
{
	Uint32 bucketIndex;
	const BucketElement* element;
	if ( FindInternal( key, bucketIndex, element ) )
	{
		value = element->m_value;
		return true;
	}
	return false;
}

template < typename TKey, typename TValue, typename THashPolicy >
template < typename TCompatibleType >
RED_INLINE TValue* HashMap< TKey, TValue, THashPolicy >::FindPtr( const TCompatibleType& key )
{
	Uint32 bucketIndex;
	BucketElement* element;
	return FindInternal( key, bucketIndex, element ) ? &element->m_value : nullptr;
}

template < typename TKey, typename TValue, typename THashPolicy >
template < typename TCompatibleType >
RED_INLINE const TValue* HashMap< TKey, TValue, THashPolicy >::FindPtr( const TCompatibleType& key ) const
{
	Uint32 bucketIndex;
	const BucketElement* element;
	return FindInternal( key, bucketIndex, element ) ? &element->m_value : nullptr;
}

template < typename TKey, typename TValue, typename THashPolicy >
RED_INLINE typename HashMap< TKey, TValue, THashPolicy >::iterator HashMap< TKey, TValue, THashPolicy >::FindValue( const TValue& value )
{
	for ( iterator it = Begin(), end = End(); it != end; ++it )
	{
		if ( it.Value() == value )
		{
			return it;
		}
	}
	return End();
}

template < typename TKey, typename TValue, typename THashPolicy >
RED_INLINE typename HashMap< TKey, TValue, THashPolicy >::const_iterator HashMap< TKey, TValue, THashPolicy >::FindValue( const TValue& value ) const
{
	for ( const_iterator it = Begin(), end = End(); it != end; ++it )
	{
		if ( it.Value() == value )
		{
			return it;
		}
	}
	return End();
}

template < typename TKey, typename TValue, typename THashPolicy >
template < typename TCompatibleType >
RED_INLINE Bool HashMap< TKey, TValue, THashPolicy >::KeyExist( const TCompatibleType& key ) const
{
	Uint32 bucketIndex;
	const BucketElement* element;
	return FindInternal( key, bucketIndex, element );
}

template < typename TKey, typename TValue, typename THashPolicy >
template < typename TCompatibleType >
RED_INLINE Bool HashMap< TKey, TValue, THashPolicy >::FindInternal( const TCompatibleType& key, Uint32& bucketIndexOut, BucketElement*& elementOut )
{
	return const_cast< const HashMap< TKey, TValue, THashPolicy >* >( this )->FindInternal( key, bucketIndexOut, const_cast< const BucketElement*& >( elementOut ) );
}

template < typename TKey, typename TValue, typename THashPolicy >
template < typename TCompatibleType >
Bool HashMap< TKey, TValue, THashPolicy >::FindInternal( const TCompatibleType& key, Uint32& bucketIndexOut, const BucketElement*& elementOut ) const
{
	if ( !m_size )
	{
		return false;
	}

	const Uint32 hash = HashFunc::GetHash( key );
	const Uint32 bucketIndex = hash % m_capacity;

	ElementIndex elementIndex = m_buckets[ bucketIndex ];
	while ( elementIndex != InvalidElementIndex )
	{
		const BucketElement* element = static_cast< const BucketElement* >( m_bucketsPool.GetBlock( elementIndex ) );
		if ( element->m_hash == hash && EqualFunc::Equal( element->m_key, key ) )
		{
			bucketIndexOut = bucketIndex;
			elementOut = element;
			return true;
		}
		elementIndex = element->m_nextIndex;
	}

	// Not found
	return false;
}

//////////////////////////////////////////////////////////////////////////

template < typename TKey, typename TValue, typename THashPolicy >
RED_INLINE void HashMap< TKey, TValue, THashPolicy >::Clear()
{
	if ( m_capacity )
	{
		if ( m_size )
		{
			for ( auto it = Begin(), end = End(); it != end; ++it )
			{
				it.m_element->Destroy();
			}
			m_size = 0;
		}

		m_bucketsPool.Clear();
		for ( Uint32 i = 0; i < m_capacity; ++i )
		{
			m_buckets[ i ] = InvalidElementIndex;
		}
		m_size = 0;

#if defined( RED_MEMORY_FORCE_DEBUG_ALLOCATOR )
		Shrink();
#endif
	}
}

template < typename TKey, typename TValue, typename THashPolicy >
RED_INLINE void HashMap< TKey, TValue, THashPolicy >::Reserve( Uint32 newCapacity )
{
	if ( m_capacity < newCapacity )
	{
		Rehash( newCapacity );
	}
}

template < typename TKey, typename TValue, typename THashPolicy >
RED_INLINE void HashMap< TKey, TValue, THashPolicy >::Shrink()
{
	Rehash( m_size );
}

template < typename TKey, typename TValue, typename THashPolicy >
void HashMap< TKey, TValue, THashPolicy >::Rehash( Uint32 newCapacity )
{
	RED_FATAL_ASSERT( newCapacity >= m_size, "" );

	if ( newCapacity == m_capacity )
	{
		return;
	}

	if ( !newCapacity )
	{
		RED_FREE( GetPoolRef(), m_bucketsPool.Data() );
		m_capacity = 0;
		return;
	}

	// Allocate single memory block for the whole hashmap
	const Uint32 newBucketsPoolSize = newCapacity * sizeof( BucketElement );
	const Uint32 newBucketsSize = newCapacity * sizeof( ElementIndex );
	const Uint32 newMemorySize = newBucketsPoolSize + newBucketsSize;
	void* newMemory = RED_ALLOCATE_ALIGNED( GetPoolRef(), newMemorySize, Max( std::alignment_of<TKey>(), std::alignment_of<TValue>() ) );
	RED_FATAL_ASSERT( newMemory, "" );

	// Initialize new internal containers
	Pool newBucketsPool;
	newBucketsPool.Init( newMemory, newBucketsPoolSize, sizeof( BucketElement ) );
	ElementIndex* newBuckets = ( ElementIndex* ) ( (Uint8*) newMemory + newBucketsPoolSize );
	for ( Uint32 i = 0; i < newCapacity; ++i )
	{
		newBuckets[ i ] = InvalidElementIndex;
	}

	if ( m_size )
	{
		// Move all elements to new containers
		for ( auto it = Begin(), end = End(); it != end; ++it )
		{
			const Uint32 hash = it.m_element->m_hash;
			const Uint32 bucketIndex = hash % newCapacity;

			// Copy into new element
			BucketElement* newElement = new( newBucketsPool ) BucketElement( std::move( *it.m_element ) );
			RED_FATAL_ASSERT( newElement, "" );
			newElement->m_hash = hash;

			newElement->m_nextIndex = newBuckets[ bucketIndex ];
			newBuckets[ bucketIndex ] = newBucketsPool.GetBlockIndex( newElement );

			// Destroy old element
			it.m_element->Destroy();
		}
	}

	// Delete old containers
	if ( m_capacity )
	{
		RED_FREE( GetPoolRef(), m_bucketsPool.Data() );
	}

	// Set new containers
	m_capacity = newCapacity;
	m_buckets = newBuckets;
	m_bucketsPool = std::move( newBucketsPool );
}

//////////////////////////////////////////////////////////////////////////

template < typename TKey, typename TValue, typename THashPolicy >
RED_INLINE void HashMap< TKey, TValue, THashPolicy >::SetPool( const red::memory::Pool& pool )
{
	RED_FATAL_ASSERT( m_capacity == 0, "Cannot change pool for already allocated HashMap" );
	red::Memcpy( &m_pool, &pool, sizeof( red::memory::Pool ) );
}

template < typename TKey, typename TValue, typename THashPolicy >
RED_INLINE const red::memory::Pool& HashMap< TKey, TValue, THashPolicy >::GetPool() const
{
	return const_cast< HashMap* >( this )->GetPoolRef();
}

template < typename TKey, typename TValue, typename THashPolicy >
RED_INLINE red::memory::Pool& HashMap< TKey, TValue, THashPolicy >::GetPoolRef()
{
	return reinterpret_cast< red::memory::Pool& >( m_pool );
}

//////////////////////////////////////////////////////////////////////////

template < typename TKey, typename TValue, typename THashPolicy >
template < typename TValueRef >
RED_INLINE typename HashMap< TKey, TValue, THashPolicy >::Result HashMap< TKey, TValue, THashPolicy >::InsertPolicy::Execute( HashMap* map, Uint32 bucketIndex, BucketElement* element, TValueRef&& value )
{
	RED_UNUSED( value );
	return Result::Failure( iterator( map, bucketIndex, element ) );
}

template < typename TKey, typename TValue, typename THashPolicy >
template < typename TValueRef >
RED_INLINE typename HashMap< TKey, TValue, THashPolicy >::Result HashMap< TKey, TValue, THashPolicy >::SetPolicy::Execute( HashMap* map, Uint32 bucketIndex, BucketElement* element, TValueRef&& value )
{
	element->m_value = std::forward< TValueRef >( value );
	return Result::Success( iterator( map, bucketIndex, element ) );
}

//////////////////////////////////////////////////////////////////////////
// TElement

template < typename TKey, typename TValue, typename THashPolicy >
template < typename... Args >
RED_INLINE HashMap< TKey, TValue, THashPolicy >::BucketElement::BucketElement( const TKey& key, Args&&... args )
	: m_key( key )
	, m_value( std::forward< Args >( args )... )
{}

template < typename TKey, typename TValue, typename THashPolicy >
RED_INLINE HashMap< TKey, TValue, THashPolicy >::BucketElement::BucketElement( BucketElement&& element )
	: m_key( std::move( element.m_key ) )
	, m_value( std::move( element.m_value ) )
{}

//////////////////////////////////////////////////////////////////////////
// ElementType

template < typename TKey, typename TValue, typename THashPolicy >
RED_INLINE HashMap< TKey, TValue, THashPolicy >::ElementType::ElementType( const KeyType& key, ValueType& value )
	: m_key( key )
	, m_value( value )
{
}

//////////////////////////////////////////////////////////////////////////
// iterator

template < typename TKey, typename TValue, typename THashPolicy >
RED_INLINE HashMap< TKey, TValue, THashPolicy >::iterator::iterator()
{
}

template < typename TKey, typename TValue, typename THashPolicy >
RED_INLINE HashMap< TKey, TValue, THashPolicy >::iterator::iterator( const iterator& it )
{
	*this = it;
}

template < typename TKey, typename TValue, typename THashPolicy >
RED_INLINE HashMap< TKey, TValue, THashPolicy >::iterator::iterator( HashMap* map, Uint32 bucketIndex, BucketElement* element )
	: const_iterator( map, bucketIndex, element )
{
}

template < typename TKey, typename TValue, typename THashPolicy >
RED_INLINE HashMap< TKey, TValue, THashPolicy >::iterator::iterator( HashMap* map )
	: const_iterator( map )
{
}

template < typename TKey, typename TValue, typename THashPolicy >
RED_INLINE typename HashMap< TKey, TValue, THashPolicy >::iterator& HashMap< TKey, TValue, THashPolicy >::iterator::operator=( const iterator& it )
{
	const_iterator::operator=( it );
	return *this;
}

template < typename TKey, typename TValue, typename THashPolicy >
RED_INLINE TValue& HashMap< TKey, TValue, THashPolicy >::iterator::Value() const
{
	RED_FATAL_ASSERT( this->m_element, "" );
	return this->m_element->m_value;
}

template < typename TKey, typename TValue, typename THashPolicy >
RED_INLINE typename HashMap< TKey, TValue, THashPolicy >::ElementType HashMap< TKey, TValue, THashPolicy >::iterator::operator*() const
{
	RED_FATAL_ASSERT( this->m_element, "" );
	return ElementType( this->m_element->m_key, this->m_element->m_value );
}

template < typename TKey, typename TValue, typename THashPolicy >
RED_INLINE typename HashMap< TKey, TValue, THashPolicy >::iterator& HashMap< TKey, TValue, THashPolicy >::iterator::operator++()
{
	const_iterator::operator++();
	return *this;
}

//////////////////////////////////////////////////////////////////////////
// const_iterator

template < typename TKey, typename TValue, typename THashPolicy >
RED_INLINE HashMap< TKey, TValue, THashPolicy >::const_iterator::const_iterator()
	: m_map( nullptr )
{
}

template < typename TKey, typename TValue, typename THashPolicy >
RED_INLINE HashMap< TKey, TValue, THashPolicy >::const_iterator::const_iterator( const const_iterator& it )
{
	*this = it;
}

template < typename TKey, typename TValue, typename THashPolicy >
RED_INLINE HashMap< TKey, TValue, THashPolicy >::const_iterator::const_iterator( const HashMap* map, Uint32 bucketIndex, const BucketElement* element )
	: m_map( const_cast< HashMap* >( map ) )
	, m_bucketIndex( bucketIndex )
	, m_element( const_cast< BucketElement* >( element ) )
{
}

template < typename TKey, typename TValue, typename THashPolicy >
RED_INLINE HashMap< TKey, TValue, THashPolicy >::const_iterator::const_iterator( const HashMap* map )
	: m_map( const_cast< HashMap* >( map ) )
	, m_bucketIndex( 0 )
	, m_element( nullptr )
{
	// Move to the first element
	if ( !m_map->m_size )
	{
		m_bucketIndex = m_map->m_capacity;
	}
	else while ( m_bucketIndex < m_map->m_capacity )
	{
		if ( m_map->m_buckets[ m_bucketIndex ] != InvalidElementIndex )
		{
			m_element = static_cast< BucketElement* >( m_map->m_bucketsPool.GetBlock( m_map->m_buckets[ m_bucketIndex ] ) );
			break;
		}

		++m_bucketIndex;
	}
}

template < typename TKey, typename TValue, typename THashPolicy >
RED_INLINE typename HashMap< TKey, TValue, THashPolicy >::const_iterator& HashMap< TKey, TValue, THashPolicy >::const_iterator::operator=( const const_iterator& it )
{
	m_map			= it.m_map;
	m_bucketIndex	= it.m_bucketIndex;
	m_element		= it.m_element;
	return *this;
}

template < typename TKey, typename TValue, typename THashPolicy >
RED_INLINE const TKey& HashMap< TKey, TValue, THashPolicy >::const_iterator::Key() const
{
	RED_FATAL_ASSERT( m_element, "" );
	return m_element->m_key;
}

template < typename TKey, typename TValue, typename THashPolicy >
RED_INLINE const TValue& HashMap< TKey, TValue, THashPolicy >::const_iterator::Value() const
{
	RED_FATAL_ASSERT( m_element, "" );
	return m_element->m_value;
}

template < typename TKey, typename TValue, typename THashPolicy >
RED_INLINE const typename HashMap< TKey, TValue, THashPolicy >::ElementType HashMap< TKey, TValue, THashPolicy >::const_iterator::operator*() const
{
	RED_FATAL_ASSERT( m_element, "" );
	return ElementType( m_element->m_key, m_element->m_value );
}

template < typename TKey, typename TValue, typename THashPolicy >
RED_INLINE Bool HashMap< TKey, TValue, THashPolicy >::const_iterator::operator==( const const_iterator& it ) const
{
	return m_element == it.m_element;
}

template < typename TKey, typename TValue, typename THashPolicy >
RED_INLINE Bool HashMap< TKey, TValue, THashPolicy >::const_iterator::operator!=( const const_iterator& it ) const
{
	return m_element != it.m_element;
}

template < typename TKey, typename TValue, typename THashPolicy >
RED_INLINE typename HashMap< TKey, TValue, THashPolicy >::const_iterator& HashMap< TKey, TValue, THashPolicy >::const_iterator::operator++()
{
	// Reached the end
	if ( m_bucketIndex >= m_map->m_capacity )
	{
		return *this;
	}

	// Check next element on the list
	if ( m_element->m_nextIndex != InvalidElementIndex )
	{
		m_element = static_cast< BucketElement* >( m_map->m_bucketsPool.GetBlock( m_element->m_nextIndex ) );
		return *this;
	}

	// Check next non-empty bucket
	while ( ++m_bucketIndex < m_map->m_capacity )
	{
		if ( m_map->m_buckets[ m_bucketIndex ] != InvalidElementIndex )
		{
			m_element = static_cast< BucketElement* >( m_map->m_bucketsPool.GetBlock( m_map->m_buckets[ m_bucketIndex ] ) );
			return *this;
		}
	}

	// Reached the end
	m_element = nullptr;
	return *this;
}

} // red