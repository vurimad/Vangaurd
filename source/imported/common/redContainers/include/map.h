/*
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "dynArray.h"
#include "arraySpan.h"

//////////////////////////////////////////////////////////////////////////
// Map implementation based on two sorted arrays.
// - insertion/removal - time O(n)
// - access - time O(log(n))
// - iteration same as for array
// - iterated elements are sorted
// - iterators are constant
// - no additional memory overhead for storing elements
//////////////////////////////////////////////////////////////////////////

class MapSerializer;

namespace red {

template < typename TKey, typename TValue, typename TSortPredicate = std::less< TKey > >
class Map
{
	typedef	DynArray< TKey > KeysType;
	typedef DynArray< TValue > ValuesType;

public:

	typedef TKey KeyType;
	typedef TValue ValueType;

	struct ElementType
	{
	public:

		RED_INLINE ElementType( const KeyType& key, ValueType& value );
		RED_INLINE const KeyType& Key() const { return m_key; }
		RED_INLINE const ValueType& Value() const { return m_value; }
		RED_INLINE ValueType& Value() { return m_value; }

	private:

		const KeyType& m_key;
		ValueType& m_value;
	};

	// std compatibility
	typedef KeyType key_type;
	typedef ValueType mapped_type;
	typedef ElementType value_type;

	struct const_iterator
	{
		typedef MapIteratorTag Tag;
		typedef const ElementType* PtrType;
		typedef const ElementType& RefType;
		typedef Uint32 DiffType;

		// std compatibility
		// TODO: implement all the other currently unneeded stuff to make it truly conformant to the random_access_iterator concept. You'll know you need it if it every fails to compile for some reason.
		// In the meantime, O(1) access vs O(N) during lookup
		typedef std::random_access_iterator_tag iterator_category;
		typedef ElementType value_type;
		typedef DiffType difference_type;
		typedef PtrType pointer;
		typedef RefType reference;

		RED_INLINE const_iterator();
		RED_INLINE const_iterator( const const_iterator& it );
		RED_INLINE const_iterator& operator=( const const_iterator& it );
		RED_INLINE const TKey& Key() const;
		RED_INLINE const TValue& Value() const;
		RED_INLINE const ElementType operator*() const;
		RED_INLINE const_iterator& operator++();
		RED_INLINE const_iterator operator+( DiffType count ) const;
		RED_INLINE DiffType operator-( const const_iterator& it ) const;
		RED_INLINE Bool operator==( const const_iterator& it ) const;
		RED_INLINE Bool operator!=( const const_iterator& it ) const;
		RED_INLINE Bool operator<( const const_iterator& it ) const;
		RED_INLINE Bool operator<=( const const_iterator& it ) const;
		RED_INLINE Bool operator>( const const_iterator& it ) const;
		RED_INLINE Bool operator>=( const const_iterator& it ) const;

	protected:

		RED_INLINE const_iterator( const Map* map, Uint32 index );

		Map* m_map;
		Uint32 m_index;

		friend class Map;
	};

	struct iterator : public const_iterator
	{
		typedef MapIteratorTag Tag;
		typedef ElementType* PtrType;
		typedef ElementType& RefType;
		typedef Uint32 DiffType;

		// std compatibility
		typedef std::random_access_iterator_tag iterator_category;
		typedef ElementType value_type;
		typedef DiffType difference_type;
		typedef PtrType pointer;
		typedef RefType reference;

		RED_INLINE iterator();
		RED_INLINE iterator( const iterator& it );
		RED_INLINE iterator& operator=( const iterator& it );
		RED_INLINE TValue& Value();
		RED_INLINE ElementType operator*() const;
		RED_INLINE iterator& operator++();
		RED_INLINE iterator operator+( DiffType count ) const;

	private:

		RED_INLINE iterator( Map* map, Uint32 index );

		using const_iterator::m_map;
		using const_iterator::m_index;

		friend class Map;
	};
	
	typedef ContainerOpResult< iterator > Result;
	typedef TSortPredicate SortPredicate;

	// Construct map which uses specified 'pool' for both arrays
	RED_INLINE Map( const red::memory::Pool& RED_CONTAINER_DEFAULT_VALUE );
	// Construct map having initial capacity set to 'initialCapacity', both arrays will use specified 'pool' for memory allocation
	RED_INLINE explicit Map( Uint32 initialCapacity, const red::memory::Pool& );
	// Copy constructor
	RED_INLINE Map( const Map& other );
	// Move constructor
	RED_INLINE Map( Map&& other );
	// Copy keys and values from SORTED array span of pairs <key, value>
	RED_INLINE Map( const ArraySpan< std::pair< TKey, TValue > >& sortedSpan );
	// Destructor
	RED_INLINE ~Map();

	// Copy assignment
	RED_INLINE Map& operator=( const Map& other );
	// Move assignment
	RED_INLINE Map& operator=( Map&& other );
	// Copy keys and values from SORTED array span of pairs <key, value>
	RED_INLINE Map& operator=( const ArraySpan< std::pair< TKey, TValue > >& sortedSpan );
	// Swap data with 'other'
	RED_INLINE void Swap( Map& other );

	// Get number of elements in the map
	RED_INLINE Uint32 Size() const { return m_keys.Size(); }
	// Get size of stored data in bytes
	RED_INLINE Uint32 DataSize() const { return m_keys.Size() * ( sizeof( TKey ) + sizeof( TValue ) ); }
	// Get maximum number of elements that can be stored in the map without need of reallocation
	RED_INLINE Uint32 Capacity() const { return m_keys.Capacity();	}
	// Get maximum size of data which can be stored in map without need of reallocation, in bytes
	RED_INLINE Uint32 DataCapacity() const { return m_keys.Capacity() * ( sizeof( TKey ) + sizeof( TValue ) ); }
	// Returns true if map contains 0 elements
	RED_INLINE Bool Empty() const {	return m_keys.Size() == 0;	}	

	// Get iterator pointing to the first element in the map
	RED_INLINE iterator Begin() { MakeClean(); return iterator( this, 0 ); }
	// Get iterator pointing to the element next after the last one
	RED_INLINE iterator End() { MakeClean(); return iterator( this, Size() ); }
	// Get const_iterator pointing to the first element in the map
	RED_INLINE const_iterator Begin() const { MakeClean(); return const_iterator( this, 0 ); }
	// Get const_iterator pointing to the element next after the last one
	RED_INLINE const_iterator End() const { MakeClean(); return const_iterator( this, Size() ); }

	// Get reference to the value associated with specified 'key'; if 'key' is not present in the map, new entry will be added with value set to TValue default
	RED_INLINE TValue& operator[]( const TKey& key );
	// Get reference to the value associated with specified 'key'; if 'key' is not present in the map, fatal assert is reported
	RED_INLINE const TValue& operator[]( const TKey& key ) const;
	// Get reference to the value associated with specified 'key'; if 'key' is not present in the map, new entry will be added with value set to 'defaultValue'
	template < typename TComparableType >
	RED_INLINE TValue& GetRef( const TComparableType& key, const TValue& defaultValue );

	// Get reference to the value associated with specified 'key'; if 'key' is not present in the map, new entry will be added with value set to 'defaultValue' via move
	template < typename TComparableType >
	RED_INLINE TValue& GetRef( const TComparableType& key, TValue&& defaultValue = TValue() );

	// Get DynArray of keys
	RED_INLINE void GetKeys( DynArray< TKey >& keys ) const;
	// Get DynArray of (possibly duplicated) values
	RED_INLINE void GetValues( DynArray< TValue >& values ) const;
	// Access a view of the keys
	RED_INLINE ArraySpan< const TKey > Keys() const;
	// Access a view of the values
	RED_INLINE ArraySpan< TValue > Values();
	RED_INLINE ArraySpan< const TValue > Values() const;
	// Access a view of some of the values
	RED_INLINE ArraySpan< TValue > ValueRange( const TKey& start, const TKey& end );
	RED_INLINE ArraySpan< const TValue > ValueRange( const TKey& start, const TKey& end ) const;
	
	// Returns true if 'other' contains the same elements
	RED_INLINE Bool operator==( const Map& other ) const;
	// Returns true if 'other' does not contain the same elements
	RED_INLINE Bool operator!=( const Map& other ) const;

	// Insert ( key, value ) pair to the map; if key was already present in the map, returned 'Result' denotes failure; returns iterator to ( key, value ) pair
	RED_INLINE Result Insert( const TKey& key, const TValue& value );
	// Insert ( key, value ) pair to the map; if key was already present in the map, returned 'Result' denotes failure; returns iterator to ( key, value ) pair
	RED_INLINE Result Insert( const TKey& key, TValue&& value );
	// Insert ( key, value ) pair to the map; may break keys order and doesn't keep keys uniqueness
	RED_INLINE void InsertUnsorted( const TKey& key, const TValue& value );
	// Insert ( key, value ) pair to the map; may break keys order and doesn't keep keys uniqueness
	RED_INLINE void InsertUnsorted( const TKey& key, TValue&& value );
	// Set 'value' for specified 'key'; if key wasn't present in the map, new ( key, value ) pair is added; returns iterator to ( key, value ) pair
	RED_INLINE Result Set( const TKey& key, const TValue& value );
	// Set 'value' for specified 'key'; if key wasn't present in the map, new ( key, value ) pair is added; returns iterator to ( key, value ) pair
	RED_INLINE Result Set( const TKey& key, TValue&& value );
	// Construct value = TValue( args... ) element under 'key' entry; if key was already present in the map, returned 'Result' denotes failure; returns iterator to ( key, value ) pair
	template < typename... Args >
	RED_INLINE Result Emplace( const TKey& key, Args&&... args );
	// Construct value = TValue( args... ) element under 'key' entry; may break keys order and doesn't keep keys uniqueness
	template < typename... Args >
	RED_INLINE void EmplaceUnsorted( const TKey& key, Args&&... args );
	// Remove entry for a given 'key'; if there's no such element, returned 'Result' denotes failure; if successful 'Result' contains iterator to the element next after removed one
	RED_INLINE Result Remove( const TKey& key );
	// Remove element specified by iterator 'it'; if there's no such element, returned 'Result' denotes failure; if successful 'Result' contains iterator to the element next after removed one
	RED_INLINE Result Remove( const_iterator it );
	// Remove first entry with given 'value'; if there's no such element, returned 'Result' denotes failure; if successful 'Result' contains iterator to the element next after removed one
	RED_INLINE Result RemoveValue( const TValue& value );

	// Get iterator to the entry with specified 'key'; returns End() if 'key' was not found
	template < typename TComparableType >
	RED_INLINE iterator Find( const TComparableType& key );
	// Get const_iterator to the entry with specified 'key'; returns End() if 'key' was not found
	template < typename TComparableType >
	RED_INLINE const_iterator Find( const TComparableType& key ) const;
	// Get 'value' associated with specified 'key'; returns true iff specified 'key' was found;
	template < typename TComparableType >
	RED_INLINE Bool Find( const TComparableType& key, TValue& value ) const;
	// Get pointer to the value associated with specified 'key'; returns nullptr if 'key' was not found
	template < typename TComparableType >
	RED_INLINE TValue* FindPtr( const TComparableType& key );
	// Get const pointer to the value associated with specified 'key'; returns nullptr if 'key' was not found
	template < typename TComparableType >
	RED_INLINE const TValue* FindPtr( const TComparableType& key ) const;
	// Get iterator to the first entry with given 'value'; returns End() if 'value' was not found
	RED_INLINE iterator FindValue( const TValue& value );
	// Get const_iterator to the first entry with given 'value'; returns End() if 'value' was not found
	RED_INLINE const_iterator FindValue( const TValue& value ) const;
	// Returns true if specified 'key' is present in the map
	template < typename TComparableType >
	RED_INLINE Bool KeyExist( const TComparableType& key ) const;

	// Remove all elements from the map
	RED_INLINE void Clear();
	// Change map capacity to 'newCapacity'
	RED_INLINE void Reserve( Uint32 newCapacity );
	// Change map capacity so that it is equal to map size
	RED_INLINE void Shrink();

	// Set 'pool' that will be used for memory allocation
	RED_INLINE void SetPool( const red::memory::Pool& pool );
	// Get memory pool used for memory allocation
	RED_INLINE const red::memory::Pool& GetPool() const;

	// Returns true if map is in 'dirty' state
	RED_INLINE Bool IsDirty() const { return ( m_flags & Flag_IsDirty ) != 0; }
	// Change map state to 'clean' (insertion sort)
	RED_INLINE void MakeClean() const;

	// Merge keys missing in this map from other map. Uses other.Size() extra space.
	// Assumes values for the same keys would also be the same or it doesn't matter if different
	RED_INLINE void MergeInPlace( const Map& other );

private:

	enum Flags : Uint32
	{
		Flag_IsDirty = RED_FLAG( 0 ),
	};

	KeysType m_keys;
	ValuesType m_values;
	Uint32 m_flags;

	template < typename TComparableType >
	RED_INLINE Bool FindInternal( const TComparableType& key, Uint32& outIndex ) const;

	RED_INLINE void SetIsDirty( Bool isDirty );
	RED_INLINE void MakeDirty( const_iterator it );
	RED_INLINE void StableSort();

	friend class ::MapSerializer;
};

//////////////////////////////////////////////////////////////////////////
// Enable c++11 range-based for loop

template < typename TKey, typename TValue, typename TSortPredicate >
RED_INLINE typename Map< TKey, TValue, TSortPredicate >::iterator begin( Map< TKey, TValue, TSortPredicate >& map )
{
	return map.Begin();
}

template < typename TKey, typename TValue, typename TSortPredicate >
RED_INLINE typename Map< TKey, TValue, TSortPredicate >::iterator end( Map< TKey, TValue, TSortPredicate >& map )
{
	return map.End();
}

template < typename TKey, typename TValue, typename TSortPredicate >
RED_INLINE typename Map< TKey, TValue, TSortPredicate >::const_iterator begin( const Map< TKey, TValue, TSortPredicate >& map )
{
	return map.Begin();
}

template < typename TKey, typename TValue, typename TSortPredicate >
RED_INLINE typename Map< TKey, TValue, TSortPredicate >::const_iterator end( const Map< TKey, TValue, TSortPredicate >& map )
{
	return map.End();
}

} // red

#include "map.hpp"

