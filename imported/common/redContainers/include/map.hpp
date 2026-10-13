/*
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

namespace red {

//////////////////////////////////////////////////////////////////////////

template < typename TKey, typename TValue, typename TSortPredicate >
RED_INLINE Map< TKey, TValue, TSortPredicate >::Map( const red::memory::Pool& pool )
	: m_keys( pool )
	, m_values( pool )
	, m_flags( 0 )
{}

template < typename TKey, typename TValue, typename TSortPredicate >
RED_INLINE Map< TKey, TValue, TSortPredicate >::Map( Uint32 initialCapacity, const red::memory::Pool& pool )
	: m_keys( pool )
	, m_values( pool )
	, m_flags( 0 )
{
	m_keys.Reserve( initialCapacity );
	m_values.Reserve( initialCapacity );
}

template < typename TKey, typename TValue, typename TSortPredicate >
RED_INLINE Map< TKey, TValue, TSortPredicate >::Map( const Map& other )
	: m_keys( other.m_keys )
	, m_values( other.m_values )
	, m_flags( other.m_flags )
{}

template < typename TKey, typename TValue, typename TSortPredicate >
RED_INLINE Map< TKey, TValue, TSortPredicate >::Map( Map&& other )
	: m_keys( std::forward< KeysType >( other.m_keys ) )
	, m_values( std::forward< ValuesType >( other.m_values ) )
	, m_flags( other.m_flags )
{
	other.m_flags = 0;
}

template < typename TKey, typename TValue, typename TSortPredicate >
RED_INLINE Map< TKey, TValue, TSortPredicate >::Map( const ArraySpan< std::pair< TKey, TValue > >& sortedSpan )
{
	const Uint32 newSize = sortedSpan.Size();
	m_keys.Resize( newSize );
	m_values.Resize( newSize );
	for ( Uint32 i = 0; i < newSize; ++i )
	{
		m_keys[ i ] = sortedSpan[ i ].first;
		m_values[ i ] = sortedSpan[ i ].second;
	}
	m_flags = 0;

	RED_FATAL_ASSERT( std::is_sorted( m_keys.Begin(), m_keys.End(), SortPredicate() ) );
	return *this;
}

template < typename TKey, typename TValue, typename TSortPredicate >
RED_INLINE Map< TKey, TValue, TSortPredicate >::~Map()
{
}

//////////////////////////////////////////////////////////////////////////

template < typename TKey, typename TValue, typename TSortPredicate >
RED_INLINE Map< TKey, TValue, TSortPredicate >& Map< TKey, TValue, TSortPredicate >::operator=( const Map& other )
{
	if ( this != &other )
	{
		m_keys = other.m_keys;
		m_values = other.m_values;
		m_flags = other.m_flags;
	}
	return *this;
}

template < typename TKey, typename TValue, typename TSortPredicate >
RED_INLINE Map< TKey, TValue, TSortPredicate >& Map< TKey, TValue, TSortPredicate >::operator=( const ArraySpan< std::pair< TKey, TValue > >& sortedSpan )
{
	const Uint32 newSize = sortedSpan.Size();
	m_keys.Resize( newSize );
	m_values.Resize( newSize );
	for ( Uint32 i = 0; i < newSize; ++i )
	{
		m_keys[ i ] = sortedSpan[ i ].first;
		m_values[ i ] = sortedSpan[ i ].second;
	}
	m_flags = 0;

	RED_FATAL_ASSERT( std::is_sorted( m_keys.Begin(), m_keys.End(), SortPredicate() ) );
	return *this;
}

template < typename TKey, typename TValue, typename TSortPredicate >
RED_INLINE Map< TKey, TValue, TSortPredicate >& Map< TKey, TValue, TSortPredicate >::operator=( Map&& other )
{
	Map( std::move( other ) ).Swap( *this );
	return *this;
}

template < typename TKey, typename TValue, typename TSortPredicate >
void Map< TKey, TValue, TSortPredicate >::Swap( Map& other )
{
	using std::swap;

	m_keys.Swap( other.m_keys );
	m_values.Swap( other.m_values );
	swap( m_flags, other.m_flags );
}

//////////////////////////////////////////////////////////////////////////

template < typename TKey, typename TValue, typename TSortPredicate >
RED_INLINE TValue& Map< TKey, TValue, TSortPredicate >::operator[]( const TKey& key )
{
	return GetRef( key );
}

template < typename TKey, typename TValue, typename TSortPredicate >
RED_INLINE const TValue& Map< TKey, TValue, TSortPredicate >::operator[]( const TKey& key ) const
{
	Uint32 index = 0;
	if ( !FindInternal( key, index ) )
	{
		RED_FATAL_ASSERT( false, "Key does not exist in Map. Cannot continue." );
	}
	return m_values[ index ];
}

template < typename TKey, typename TValue, typename TSortPredicate >
template < typename TComparableType >
RED_INLINE TValue& Map< TKey, TValue, TSortPredicate >::GetRef( const TComparableType& key, const TValue& defaultValue )
{
	Uint32 index = 0;
	if ( !FindInternal( key, index ) )
	{
		m_keys.InsertAt( index, key );
		m_values.InsertAt( index, defaultValue );
	}
	return m_values[ index ];
}

template < typename TKey, typename TValue, typename TSortPredicate >
template < typename TComparableType >
RED_INLINE TValue& Map< TKey, TValue, TSortPredicate >::GetRef( const TComparableType& key, TValue&& defaultValue /* = TValue() */ )
{
	Uint32 index = 0;
	if ( !FindInternal( key, index ) )
	{
		m_keys.InsertAt( index, key );
		m_values.InsertAt( index, std::move(defaultValue) );
	}
	return m_values[index];
}

template < typename TKey, typename TValue, typename TSortPredicate >
RED_INLINE void Map< TKey, TValue, TSortPredicate >::GetKeys( DynArray< TKey >& keys ) const
{
	keys = m_keys;
}

template < typename TKey, typename TValue, typename TSortPredicate >
RED_INLINE void Map< TKey, TValue, TSortPredicate >::GetValues( DynArray< TValue >& values ) const
{
	values = m_values;
}

template < typename TKey, typename TValue, typename TSortPredicate >
RED_INLINE ArraySpan< const TKey > Map< TKey, TValue, TSortPredicate >::Keys() const
{
	return m_keys;
}

template < typename TKey, typename TValue, typename TSortPredicate >
RED_INLINE ArraySpan< TValue > Map< TKey, TValue, TSortPredicate >::Values()
{
	return m_values;
}

template < typename TKey, typename TValue, typename TSortPredicate >
RED_INLINE ArraySpan< const TValue > Map< TKey, TValue, TSortPredicate >::Values() const
{
	return m_values;
}

template < typename TKey, typename TValue, typename TSortPredicate >
RED_INLINE ArraySpan< TValue > Map< TKey, TValue, TSortPredicate >::ValueRange( const TKey& start, const TKey& end )
{
	MakeClean();
	SortPredicate predicate;
	typename KeysType::iterator startIter = std::lower_bound( m_keys.Begin(), m_keys.End(), start, predicate );
	const Uint32 startOffset = static_cast< Uint32 >( startIter - m_keys.Begin() );
	typename KeysType::iterator endIter = std::lower_bound( m_keys.Begin(), m_keys.End(), end, predicate );
	const Uint32 endOffset = static_cast< Uint32 >( endIter - m_keys.Begin() );
	return ArraySpan< TValue >( m_values ).Slice( startOffset, endOffset );
}

template < typename TKey, typename TValue, typename TSortPredicate >
RED_INLINE ArraySpan< const TValue > Map< TKey, TValue, TSortPredicate >::ValueRange( const TKey& start, const TKey& end ) const
{
	MakeClean();
	SortPredicate predicate;
	typename KeysType::const_iterator startIter = std::lower_bound( m_keys.Begin(), m_keys.End(), start, predicate );
	const Uint32 startOffset = static_cast< Uint32 >( startIter - m_keys.Begin() );
	typename KeysType::const_iterator endIter = std::lower_bound( m_keys.Begin(), m_keys.End(), end, predicate );
	const Uint32 endOffset = static_cast< Uint32 >( endIter - m_keys.Begin() );
	return ArraySpan< const TValue >( m_values ).Slice( startOffset, endOffset );
}

//////////////////////////////////////////////////////////////////////////

template < typename TKey, typename TValue, typename TSortPredicate >
RED_INLINE Bool Map< TKey, TValue, TSortPredicate >::operator==( const Map& other ) const
{
	MakeClean();
	other.MakeClean();
	return ( m_keys == other.m_keys && m_values == other.m_values );
}

template < typename TKey, typename TValue, typename TSortPredicate >
RED_INLINE Bool Map< TKey, TValue, TSortPredicate >::operator!=( const Map& other ) const
{
	MakeClean();
	other.MakeClean();
	return ( m_keys != other.m_keys || m_values != other.m_values );
}

//////////////////////////////////////////////////////////////////////////

template < typename TKey, typename TValue, typename TSortPredicate >
RED_INLINE typename Map< TKey, TValue, TSortPredicate >::Result Map< TKey, TValue, TSortPredicate >::Insert( const TKey& key, const TValue& value )
{
	Uint32 index = 0;
	if ( !FindInternal( key, index ) )
	{
		m_keys.InsertAt( index, key );
		m_values.InsertAt( index, value );
		return Result::Success( iterator( this, index ) );
	}
	return Result::Failure( iterator( this, index ) );
}

template < typename TKey, typename TValue, typename TSortPredicate >
RED_INLINE typename Map< TKey, TValue, TSortPredicate >::Result Map< TKey, TValue, TSortPredicate >::Insert( const TKey& key, TValue&& value )
{
	Uint32 index = 0;
	if ( !FindInternal( key, index ) )
	{
		m_keys.InsertAt( index, key );
		m_values.InsertAt( index, std::forward< TValue >( value ) );
		return Result::Success( iterator( this, index ) );
	}
	return Result::Failure( iterator( this, index ) );
}

template < typename TKey, typename TValue, typename TSortPredicate >
RED_INLINE void Map< TKey, TValue, TSortPredicate >::InsertUnsorted( const TKey& key, const TValue& value )
{
	m_keys.PushBack( key );
	m_values.PushBack( value );
	MakeDirty( iterator( this, m_keys.Size() - 1 ) );
}

template < typename TKey, typename TValue, typename TSortPredicate >
RED_INLINE void Map< TKey, TValue, TSortPredicate >::InsertUnsorted( const TKey& key, TValue&& value )
{
	m_keys.PushBack( key );
	m_values.PushBack( std::forward< TValue >( value ) );
	MakeDirty( iterator( this, m_keys.Size() - 1 ) );
}

template < typename TKey, typename TValue, typename TSortPredicate >
RED_INLINE typename Map< TKey, TValue, TSortPredicate >::Result Map< TKey, TValue, TSortPredicate >::Set( const TKey& key, const TValue& value )
{
	Uint32 index = 0;
	if ( FindInternal( key, index ) )
	{
		m_values[ index ] = value;
	}
	else
	{
		m_keys.InsertAt( index, key );
		m_values.InsertAt( index, value );
	}
	return Result::Success( iterator( this, index ) );
}

template < typename TKey, typename TValue, typename TSortPredicate >
RED_INLINE typename Map< TKey, TValue, TSortPredicate >::Result Map< TKey, TValue, TSortPredicate >::Set( const TKey& key, TValue&& value )
{
	Uint32 index = 0;
	if ( FindInternal( key, index ) )
	{
		m_values[ index ] = std::forward< TValue >( value );
	}
	else
	{
		m_keys.InsertAt( index, key );
		m_values.InsertAt( index, std::forward< TValue >( value ) );
	}
	return Result::Success( iterator( this, index ) );
}

template < typename TKey, typename TValue, typename TSortPredicate >
template < typename... Args >
RED_INLINE typename Map< TKey, TValue, TSortPredicate >::Result Map< TKey, TValue, TSortPredicate >::Emplace( const TKey& key, Args&&... args )
{
	Uint32 index = 0;
	if ( !FindInternal( key, index ) )
	{
		m_keys.InsertAt( index, key );
		m_values.EmplaceAt( index, std::forward< Args >( args )... );
		return Result::Success( iterator( this, index ) );
	}
	return Result::Failure( iterator( this, index ) );
}

template < typename TKey, typename TValue, typename TSortPredicate >
template < typename... Args >
RED_INLINE void Map< TKey, TValue, TSortPredicate >::EmplaceUnsorted( const TKey& key, Args&&... args )
{
	m_keys.PushBack( key );
	m_values.EmplaceBack( std::forward< Args >( args )... );
	MakeDirty( iterator( this, m_keys.Size() - 1 ) );
}

template < typename TKey, typename TValue, typename TSortPredicate >
RED_INLINE typename Map< TKey, TValue, TSortPredicate >::Result Map< TKey, TValue, TSortPredicate >::Remove( const TKey& key )
{
	Uint32 index = 0;
	if ( FindInternal( key, index ) )
	{
		m_keys.RemoveAt( index );
		m_values.RemoveAt( index );
		return Result::Success( iterator( this, index ) );
	}
	return Result::Failure();
}

template < typename TKey, typename TValue, typename TSortPredicate >
RED_INLINE typename Map< TKey, TValue, TSortPredicate >::Result Map< TKey, TValue, TSortPredicate >::Remove( const_iterator it )
{
	Uint32 index = it.m_index;
	m_keys.RemoveAt( index );
	m_values.RemoveAt( index );
	return Result::Success( iterator( this, index ) );
}

template < typename TKey, typename TValue, typename TSortPredicate >
RED_INLINE typename Map< TKey, TValue, TSortPredicate >::Result Map< TKey, TValue, TSortPredicate >::RemoveValue( const TValue& value )
{
	typename ValuesType::iterator it = std::find( m_values.Begin(), m_values.End(), value );
	if ( it != m_values.End() )
	{
		Uint32 index = static_cast< Uint32 >( it - m_values.Begin() );
		m_keys.RemoveAt( index );
		m_values.RemoveAt( index );
		if ( m_keys.Size() < 2 )
		{
			SetIsDirty( false );
		}
		return Result::Success( iterator( this, index ) );
	}
	return Result::Failure();
}

//////////////////////////////////////////////////////////////////////////

template < typename TKey, typename TValue, typename TSortPredicate >
template < typename TComparableType >
RED_INLINE typename Map< TKey, TValue, TSortPredicate >::iterator Map< TKey, TValue, TSortPredicate >::Find( const TComparableType& key )
{
	Uint32 index = 0;
	if ( FindInternal( key, index ) )
	{
		return iterator( this, index );
	}
	return End();
}

template < typename TKey, typename TValue, typename TSortPredicate >
template < typename TComparableType >
RED_INLINE typename Map< TKey, TValue, TSortPredicate >::const_iterator Map< TKey, TValue, TSortPredicate >::Find( const TComparableType& key ) const
{
	Uint32 index = 0;
	if ( FindInternal( key, index ) )
	{
		return const_iterator( this, index );
	}
	return End();
}

template < typename TKey, typename TValue, typename TSortPredicate >
template < typename TComparableType >
RED_INLINE Bool Map< TKey, TValue, TSortPredicate >::Find( const TComparableType& key, TValue& value ) const
{
	Uint32 index = 0;
	Bool res = false;
	if ( ( res = FindInternal( key, index ) ) )
	{
		value = m_values[ index ];
	}
	return res;
}

template < typename TKey, typename TValue, typename TSortPredicate >
template < typename TComparableType >
RED_INLINE TValue* Map< TKey, TValue, TSortPredicate >::FindPtr( const TComparableType& key )
{
	return const_cast< TValue* >( const_cast< const Map* >( this )->FindPtr( key ) );
}

template < typename TKey, typename TValue, typename TSortPredicate >
template < typename TComparableType >
RED_INLINE const TValue* Map< TKey, TValue, TSortPredicate >::FindPtr( const TComparableType& key ) const
{
	Uint32 index = 0;
	if ( FindInternal( key, index ) )
	{
		return &m_values[ index ];
	}
	return nullptr;
}

template < typename TKey, typename TValue, typename TSortPredicate >
RED_INLINE typename Map< TKey, TValue, TSortPredicate >::iterator Map< TKey, TValue, TSortPredicate >::FindValue( const TValue& value )
{
	MakeClean();
	typename ValuesType::const_iterator it = std::find( m_values.Begin(), m_values.End(), value );
	if ( it != m_values.End() )
	{
		return iterator( this, static_cast< Uint32 >( it - m_values.Begin() ) );
	}
	return End();
}

template < typename TKey, typename TValue, typename TSortPredicate >
RED_INLINE typename Map< TKey, TValue, TSortPredicate >::const_iterator Map< TKey, TValue, TSortPredicate >::FindValue( const TValue& value ) const
{
	MakeClean();
	typename ValuesType::const_iterator it = std::find( m_values.Begin(), m_values.End(), value );
	if ( it != m_values.End() )
	{
		return const_iterator( this, static_cast< Uint32 >( it - m_values.Begin() ) );
	}
	return End();
}

template < typename TKey, typename TValue, typename TSortPredicate >
template < typename TComparableType >
RED_INLINE Bool Map< TKey, TValue, TSortPredicate >::KeyExist( const TComparableType& key ) const
{
	MakeClean();
	SortPredicate predicate;
	typename KeysType::const_iterator it = std::lower_bound( m_keys.Begin(), m_keys.End(), key, predicate );
	return ( it != m_keys.End() && !predicate( key, *it ) );
}

template < typename TKey, typename TValue, typename TSortPredicate >
template < typename TComparableType >
RED_INLINE Bool Map< TKey, TValue, TSortPredicate >::FindInternal( const TComparableType& key, Uint32& outIndex ) const
{
	MakeClean();
	SortPredicate predicate;
	typename KeysType::const_iterator it = std::lower_bound( m_keys.Begin(), m_keys.End(), key, predicate );
	outIndex = static_cast< Uint32 >( it - m_keys.Begin() );
	return ( it != m_keys.End() && !predicate( key, *it ) );
}

//////////////////////////////////////////////////////////////////////////

template < typename TKey, typename TValue, typename TSortPredicate >
RED_INLINE void Map< TKey, TValue, TSortPredicate >::Clear()
{
	SetIsDirty( false );
	m_keys.Clear();
	m_values.Clear();
}

template < typename TKey, typename TValue, typename TSortPredicate >
RED_INLINE void Map< TKey, TValue, TSortPredicate >::Reserve( Uint32 newCapacity )
{
	m_keys.Reserve( newCapacity );
	m_values.Reserve( newCapacity );
}

template < typename TKey, typename TValue, typename TSortPredicate >
RED_INLINE void Map< TKey, TValue, TSortPredicate >::Shrink()
{
	m_keys.Shrink();
	m_values.Shrink();
}

//////////////////////////////////////////////////////////////////////////

template < typename TKey, typename TValue, typename TSortPredicate >
RED_INLINE void Map< TKey, TValue, TSortPredicate >::SetPool( const red::memory::Pool& pool )
{
	RED_FATAL_ASSERT( m_keys.m_capacity == 0, "Cannot change pool for already allocated Map" );
	m_keys.SetPool( pool );
	m_values.SetPool( pool );
}

template < typename TKey, typename TValue, typename TSortPredicate >
RED_INLINE const red::memory::Pool& Map< TKey, TValue, TSortPredicate >::GetPool() const
{
	return m_keys.GetPool();
}

//////////////////////////////////////////////////////////////////////////

template < typename TKey, typename TValue, typename TSortPredicate >
RED_INLINE void Map< TKey, TValue, TSortPredicate >::SetIsDirty( Bool isDirty )
{
	if ( isDirty )
	{
		m_flags |= Flag_IsDirty;
	}
	else
	{
		m_flags &= ( ~Flag_IsDirty );
	}
}

template < typename TKey, typename TValue, typename TSortPredicate >
RED_INLINE void Map< TKey, TValue, TSortPredicate >::MakeDirty( const_iterator it )
{
	const Uint32 size = Size();
	if ( size < 2 )
	{
		// if less than 2 elements it's automatically clean
		SetIsDirty( false );
		return;
	}
	if ( IsDirty() )
	{
		// no need to change state of already dirty map
		// OR an array after "remove" operation (cause it doesn't change the order)
		return;
	}
	const Uint32 index = it.m_index;
	if ( index < size )
	{
		Bool dirty = false;
		SortPredicate pred;
		if ( index > 0 )
		{
			dirty = !pred( m_keys[ index - 1 ], m_keys[ index ] );
		}
		if ( !dirty && index < size - 1 )
		{
			dirty = !pred( m_keys[ index ], m_keys[ index + 1 ] );
		}
		if ( dirty )
		{
			SetIsDirty( true );
		}
	}
}

template < typename TKey, typename TValue, typename TSortPredicate >
RED_INLINE void Map< TKey, TValue, TSortPredicate >::MakeClean() const
{
	if ( IsDirty() )
	{
		const_cast< Map* >( this )->StableSort();
	}
}

template < typename TKey, typename TValue, typename TSortPredicate >
RED_INLINE void Map< TKey, TValue, TSortPredicate >::StableSort()
{
	const Uint32 size = Size();
	SortPredicate pred;
	for ( Uint32 i = 1; i < size; ++i )
	{
		TKey key = std::move( m_keys[ i ] );
		TValue val = std::move( m_values[ i ] );
		Uint32 j = i;
		while ( j > 0 && pred( key, m_keys[ j - 1 ] ) )
		{
			m_keys[ j ] = std::move( m_keys[ j - 1 ] );
			m_values[ j ] = std::move( m_values[ j - 1 ] );
			j--;
		}
		m_keys[ j ] = std::move( key );
		m_values[ j ] = std::move( val );
	}
	SetIsDirty( false );
}

//////////////////////////////////////////////////////////////////////////
// ElementType

template < typename TKey, typename TValue, typename TSortPredicate >
RED_INLINE Map< TKey, TValue, TSortPredicate >::ElementType::ElementType( const KeyType& key, ValueType& value )
	: m_key( key )
	, m_value( value )
{
}

//////////////////////////////////////////////////////////////////////////
// const_iterator

template < typename TKey, typename TValue, typename TSortPredicate >
RED_INLINE Map< TKey, TValue, TSortPredicate >::const_iterator::const_iterator()
	: m_map( nullptr )
	, m_index( 0 )
{
}

template < typename TKey, typename TValue, typename TSortPredicate >
RED_INLINE Map< TKey, TValue, TSortPredicate >::const_iterator::const_iterator( const const_iterator& it )
{
	*this = it;
}

template < typename TKey, typename TValue, typename TSortPredicate >
RED_INLINE Map< TKey, TValue, TSortPredicate >::const_iterator::const_iterator( const Map* map, Uint32 index )
	: m_map( const_cast< Map* >( map ) )
	, m_index( index )
{
}

template < typename TKey, typename TValue, typename TSortPredicate >
RED_INLINE typename Map< TKey, TValue, TSortPredicate >::const_iterator& Map< TKey, TValue, TSortPredicate >::const_iterator::operator=( const const_iterator& it )
{
	m_map = it.m_map;
	m_index = it.m_index;
	return *this;
}

template < typename TKey, typename TValue, typename TSortPredicate >
RED_INLINE const TKey& Map< TKey, TValue, TSortPredicate >::const_iterator::Key() const
{
	RED_FATAL_ASSERT( m_map, "Invalid Map const_iterator" );
	return m_map->m_keys[ m_index ];
}

template < typename TKey, typename TValue, typename TSortPredicate >
RED_INLINE const TValue& Map< TKey, TValue, TSortPredicate >::const_iterator::Value() const
{
	RED_FATAL_ASSERT( m_map, "Invalid Map const_iterator" );
	return m_map->m_values[ m_index ];
}

template < typename TKey, typename TValue, typename TSortPredicate >
RED_INLINE const typename Map< TKey, TValue, TSortPredicate >::ElementType Map< TKey, TValue, TSortPredicate >::const_iterator::operator*() const
{
	RED_FATAL_ASSERT( m_map, "Invalid Map const_iterator" );
	return ElementType( m_map->m_keys[ m_index ], m_map->m_values[ m_index ] );
}

template < typename TKey, typename TValue, typename TSortPredicate >
RED_INLINE typename Map< TKey, TValue, TSortPredicate >::const_iterator& Map< TKey, TValue, TSortPredicate >::const_iterator::operator++()
{
	m_index++;
	return *this;
}

template < typename TKey, typename TValue, typename TSortPredicate >
RED_INLINE typename Map< TKey, TValue, TSortPredicate >::const_iterator Map< TKey, TValue, TSortPredicate >::const_iterator::operator+( DiffType count ) const
{
	return const_iterator( m_map, m_index + count );
}

template < typename TKey, typename TValue, typename TSortPredicate >
RED_INLINE typename Map< TKey, TValue, TSortPredicate >::const_iterator::DiffType Map< TKey, TValue, TSortPredicate >::const_iterator::operator-( const const_iterator& it ) const
{
	RED_FATAL_ASSERT( m_map == it.m_map, "Calculating difference between const_iterators from two different Maps" );
	return m_index - it.m_index;
}

template < typename TKey, typename TValue, typename TSortPredicate >
RED_INLINE Bool Map< TKey, TValue, TSortPredicate >::const_iterator::operator==( const const_iterator& it ) const
{
	return m_index == it.m_index && m_map == it.m_map;
}

template < typename TKey, typename TValue, typename TSortPredicate >
RED_INLINE Bool Map< TKey, TValue, TSortPredicate >::const_iterator::operator!=( const const_iterator& it ) const
{
	return m_index != it.m_index || m_map != it.m_map;
}

template < typename TKey, typename TValue, typename TSortPredicate >
RED_INLINE Bool Map< TKey, TValue, TSortPredicate >::const_iterator::operator<( const const_iterator& it ) const
{
	RED_FATAL_ASSERT( m_map == it.m_map, "Comparing const_iterators of different Maps" );
	return m_index < it.m_index;
}

template < typename TKey, typename TValue, typename TSortPredicate >
RED_INLINE Bool Map< TKey, TValue, TSortPredicate >::const_iterator::operator<=( const const_iterator& it ) const
{
	RED_FATAL_ASSERT( m_map == it.m_map, "Comparing const_iterators of different Maps" );
	return m_index <= it.m_index;
}

template < typename TKey, typename TValue, typename TSortPredicate >
RED_INLINE Bool Map< TKey, TValue, TSortPredicate >::const_iterator::operator>( const const_iterator& it ) const
{
	RED_FATAL_ASSERT( m_map == it.m_map, "Comparing const_iterators of different Maps" );
	return m_index > it.m_index;
}

template < typename TKey, typename TValue, typename TSortPredicate >
RED_INLINE Bool Map< TKey, TValue, TSortPredicate >::const_iterator::operator>=( const const_iterator& it ) const
{
	RED_FATAL_ASSERT( m_map == it.m_map, "Comparing const_iterators of different Maps" );
	return m_index >= it.m_index;
}

//////////////////////////////////////////////////////////////////////////
// iterator

template < typename TKey, typename TValue, typename TSortPredicate >
RED_INLINE Map< TKey, TValue, TSortPredicate >::iterator::iterator()
	: const_iterator( nullptr, 0 )
{
}

template < typename TKey, typename TValue, typename TSortPredicate >
RED_INLINE Map< TKey, TValue, TSortPredicate >::iterator::iterator( const iterator& it )
{
	*this = it;
}

template < typename TKey, typename TValue, typename TSortPredicate >
RED_INLINE Map< TKey, TValue, TSortPredicate >::iterator::iterator( Map* map, Uint32 index )
	: const_iterator( map, index )
{
}

template < typename TKey, typename TValue, typename TSortPredicate >
RED_INLINE typename Map< TKey, TValue, TSortPredicate >::iterator& Map< TKey, TValue, TSortPredicate >::iterator::operator=( const iterator& it )
{
	const_iterator::operator=( it );
	return *this;
}

template < typename TKey, typename TValue, typename TSortPredicate >
RED_INLINE TValue& Map< TKey, TValue, TSortPredicate >::iterator::Value()
{
	RED_FATAL_ASSERT( m_map, "Invalid Map iterator" );
	return m_map->m_values[ m_index ];
}

template < typename TKey, typename TValue, typename TSortPredicate >
RED_INLINE typename Map< TKey, TValue, TSortPredicate >::ElementType Map< TKey, TValue, TSortPredicate >::iterator::operator*() const
{
	RED_FATAL_ASSERT( m_map, "Invalid Map iterator" );
	return ElementType( m_map->m_keys[ m_index ], m_map->m_values[ m_index ] );
}

template < typename TKey, typename TValue, typename TSortPredicate >
RED_INLINE typename Map< TKey, TValue, TSortPredicate >::iterator& Map< TKey, TValue, TSortPredicate >::iterator::operator++()
{
	const_iterator::operator++();
	return *this;
}

template < typename TKey, typename TValue, typename TSortPredicate >
RED_INLINE typename Map< TKey, TValue, TSortPredicate >::iterator Map< TKey, TValue, TSortPredicate >::iterator::operator+( DiffType count ) const
{
	return iterator( m_map, m_index + count );
}

template < typename TKey, typename TValue, typename TSortPredicate /*= std::less< TKey > */>
RED_INLINE void red::Map<TKey, TValue, TSortPredicate>::MergeInPlace( const Map& other )
{
	// Ensure both maps are ready to merge
	MakeClean();
	other.MakeClean();

	SortPredicate pred;

	// InsertUnsorted grows the end; this allows us to traverse the map while inserting into it
	const Uint32 premergeSize = Size();
	const Uint32 otherSize = other.Size();

	Reserve( premergeSize + otherSize );
	Uint32 thisIndex = 0;
	Uint32 otherIndex = 0;

	Bool isDirty = false;

	while ( thisIndex < premergeSize && otherIndex < otherSize )
	{
		const auto& thisKey = m_keys[ thisIndex ];
		const auto& otherKey = other.m_keys[ otherIndex ];

		if ( pred( otherKey, thisKey ) ) // otherKey < thisKey
		{
			isDirty = true; // Dirty since inserting smaller key after thisKey
			m_keys.PushBack( otherKey );
			m_values.PushBack( other.m_values[ otherIndex ] );
			otherIndex += 1;
		}
		else if ( pred( thisKey, otherKey ) ) // thisKey < otherKey, try to catch up to otherMap
		{
			thisIndex += 1;
		}
		else // thisKey == otherKey
		{
			thisIndex += 1;
			otherIndex += 1;
		}
	}

	// Merge in leftovers. Not a dirty operation since they must be larger than thisMap keys, and otherMap is already sorted.
	while ( otherIndex < otherSize )
	{
		m_keys.PushBack( other.m_keys[ otherIndex ] );
		m_values.PushBack( other.m_values[ otherIndex ] );
		otherIndex += 1;
	}

	Shrink();

	if ( isDirty )
	{
		SetIsDirty( true );
		MakeClean();
	}
}

} // red