/*
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

namespace red {

//////////////////////////////////////////////////////////////////////////
// Dynamic array of 'compressed' integer types.
// Usable for array of 2 or 4 byte integers.
//////////////////////////////////////////////////////////////////////////

template < Uint32 BitSize, typename TElement = Uint32, typename TStorage = Uint64 >
class PackedArray
{
public:

	typedef TElement						ElementType;
	typedef TStorage						StorageType;

	// Construct array which uses specified 'pool' for memory allocation
	RED_INLINE PackedArray( const red::memory::Pool& pool RED_CONTAINER_DEFAULT_VALUE );
	// Copy constructor
	RED_INLINE PackedArray( const PackedArray& other );
	// Move constructor
	RED_INLINE PackedArray( PackedArray&& other );
	// Construct array of size equal to 'initialSize', using specified 'pool' for memory allocation
	RED_INLINE explicit PackedArray( Uint32 initialSize, const red::memory::Pool& pool );

	// Copy assignment
	RED_INLINE PackedArray& operator=( const PackedArray& other );
	// Move assignment
	RED_INLINE PackedArray& operator=( PackedArray&& other );
	// Swap data with 'other'
	RED_INLINE void Swap( PackedArray& other );

	// Get pointer to the allocated buffer
	RED_INLINE void* Data() { return m_data.Data(); }
	// Get const pointer to the allocated buffer
	RED_INLINE const void* Data() const { return m_data.Data(); }
	// Get number of elements in the array
	RED_INLINE Uint32 Size() const { return m_size; }
	// Returns true if array contains 0 elements
	RED_INLINE Bool Empty() const { return m_size == 0; }

	// Returns true if 'other' contains the same elements
	RED_INLINE Bool operator==( const PackedArray& other ) const;
	// Returns true if 'other' does not contain the same elements
	RED_INLINE Bool operator!=( const PackedArray& other ) const;

	// Set value of element at specified 'index' to 'val'
	RED_INLINE void Set( Uint32 index, TElement val );
	// Get value of element at specified 'index'
	RED_INLINE TElement Get( Uint32 index ) const;
	// Set value of all elements to zero
	RED_INLINE void ResetAll();

	// Remove all elements from the array
	RED_INLINE void Clear();
	// Change number of elements stored in the array
	RED_INLINE void Resize( Uint32 size );
	// Change internal buffer capacity so that it contains the minimum number of elements needed to store current number of elements
	RED_INLINE void Shrink();

	// Set 'pool' that will be used for memory allocation
	RED_INLINE void SetPool( const red::memory::Pool& pool ) { m_data.SetPool( pool ); }
	// Get memory pool used for memory allocation
	RED_INLINE const red::memory::Pool& GetPool() const { return m_data.GetPool(); }

private:

	typedef DynArray< TStorage >	StorageData;

	static const Uint32 STORAGE_BITS				= sizeof( TStorage ) * 8;
	static const Uint32	STORAGE_INDEX_SHIFT			= red::alg::Log2< STORAGE_BITS / BitSize >::Value;
	static const Uint32 VALUE_INDEX_MASK			= ( STORAGE_BITS / BitSize ) - 1;
	static const TStorage VALUE_MASK				= ( TStorage( 1 ) << BitSize ) - 1;

	StorageData						m_data;
	Uint32							m_size;

	static_assert( std::numeric_limits< TStorage >::is_integer, "Only integer types can be used as storage type for BitSetBase" );
	static_assert( !std::numeric_limits< TStorage >::is_signed, "Only unsigned types can be used as storage type for BitSetBase" );
	static_assert( ( STORAGE_BITS % BitSize ) == 0, "Number of bits of storage type should be multiple of BitSize" );
};

} // red

#include "packedArray.hpp"
