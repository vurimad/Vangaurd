/*
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "bitSetCommon.h"
#include "algorithms.h"
#include "dynArray.h"

class BitSetDynamicSerializer;

namespace red {

template < typename TStorage >
class BitSetDynamicBase
{
public:

	typedef TStorage					StorageType;
	typedef red::PoolDefault	DefaultPool; 

	// Construct bit set which uses specified 'pool' for memory allocation
	RED_INLINE BitSetDynamicBase( const red::memory::Pool& pool RED_CONTAINER_DEFAULT_VALUE );
	// Copy constructor
	RED_INLINE BitSetDynamicBase( const BitSetDynamicBase& other );
	// Move constructor
	RED_INLINE BitSetDynamicBase( BitSetDynamicBase&& other );
	// Construct bit set of given 'initialSize' which uses specified 'pool' for memory allocation
	RED_INLINE explicit BitSetDynamicBase( Uint32 initialSize, const red::memory::Pool& pool );
	// Destructor
	RED_INLINE ~BitSetDynamicBase();

	// Copy assignment
	RED_INLINE BitSetDynamicBase& operator=( const BitSetDynamicBase& other );
	// Move assignment
	RED_INLINE BitSetDynamicBase& operator=( BitSetDynamicBase&& other );
	// Swap bits with other
	RED_INLINE void Swap( BitSetDynamicBase& other );

	// Get pointer to the buffer storing bits
	RED_INLINE const StorageType* Data() const { return m_bits.TypedData(); }
	// Get number of bits stored in the set
	RED_INLINE Uint32 Size() const { return m_bitSize; }

	// Returns true if 'other' has the same bits set
	RED_INLINE Bool operator==( const BitSetDynamicBase& other ) const;
	// Returns true if 'other' does not have the same bits set
	RED_INLINE Bool operator!=( const BitSetDynamicBase& other ) const;

	// Returns true if bit at specified 'index' is set
	RED_INLINE Bool Get( Uint32 index ) const;
	// Set bit at specified 'index' (set its value to 1)
	RED_INLINE void Set( Uint32 index );
	// Clear bit at specified 'index' (set its value to 0)
	RED_INLINE void Clear( Uint32 index );
	// Toggle bit at specified 'index' (change 1 to 0 and 0 to 1)
	RED_INLINE void Toggle( Uint32 index );
	// Set all bits to 1
	RED_INLINE void SetAll();
	// Set all bits to 0
	RED_INLINE void ClearAll();
	// Toggle all bits
	RED_INLINE void ToggleAll();

	// Find index of next set bit, starting from the specified 'index' (included); if no such bit was found 'MaxSize' is returned
	RED_INLINE Uint32 FindNextSet( Uint32 index ) const { return BitSetImplUtils::FindNextSet( *this, index ); }
	// Find index of next cleared bit, starting from the specified 'index' (included); if no such bit was found 'MaxSize' is returned
	RED_INLINE Uint32 FindNextClear( Uint32 index ) const { return BitSetImplUtils::FindNextClear( *this, index ); }

	// Returns true if any of the bits is set to 1
	RED_INLINE Bool IsAnySet() const { return BitSetImplUtils::IsAnySet( *this ); }
	// Returns true if any of the bits specified by 'other' is set to 1
	RED_INLINE Bool IsAnySet( const BitSetDynamicBase& other ) const { return BitSetImplUtils::IsAnySet( *this, other ); }
	// Returns true if none of bits is set to 1
	RED_INLINE Bool IsNoneSet() const { return BitSetImplUtils::IsNoneSet( *this ); }
	// Returns true if none of bits specified by 'other' is set to 1
	RED_INLINE Bool IsNoneSet( const BitSetDynamicBase& other ) const { return BitSetImplUtils::IsNoneSet( *this, other ); }
	// Returns true if all bits are set to 1
	RED_INLINE Bool IsAllSet() const { return BitSetImplUtils::IsAllSet( *this ); }
	// Returns true if all bits specified by 'other' are set to 1
	RED_INLINE Bool IsAllSet( const BitSetDynamicBase& other ) const { return BitSetImplUtils::IsAllSet( *this, other ); }

	// Bit-wise AND called on 'this' and 'other', storing result in 'this'
	RED_INLINE void operator&=( const BitSetDynamicBase& other ) { BitSetImplUtils::And( *this, other ); }
	// Bit-wise OR called on 'this' and 'other', storing result in 'this'
	RED_INLINE void operator|=( const BitSetDynamicBase& other ) { BitSetImplUtils::Or( *this, other ); }
	// Bit-wise MINUS called on 'this' and 'other', storing result in 'this'
	RED_INLINE void operator-=( const BitSetDynamicBase& other ) { BitSetImplUtils::Minus( *this, other ); }
	// Bit-wise XOR called on 'this' and 'other', storing result in 'this'
	RED_INLINE void operator^=( const BitSetDynamicBase& other ) { BitSetImplUtils::Xor( *this, other ); }

	// Returns the number of set bits
	RED_INLINE Uint32 PopulationCount() const { return BitSetImplUtils::PopulationCount( *this ); }

	// Change number of bits stored in bits set
	RED_INLINE void Resize( Uint32 size );
	// Change internal buffer capacity so that it contains the minimum number of elements needed to store current number of bits
	RED_INLINE void Shrink();
	// Remove element specified by 'index'; reorders bits such that the last bit is moved to the position of removed element
	RED_INLINE void RemoveAtReorder( Uint32 index );

	// Set pool that will be used for memory allocation
	RED_INLINE void SetPool( const red::memory::Pool& pool ) { m_bits.SetPool( pool ); }
	// Get memory pool used for memory allocation 
	RED_INLINE const red::memory::Pool& GetPool() const { return m_bits.GetPool(); }
	// Get data size of the internal array
	RED_INLINE Uint32 DataSize() const { return m_bits.DataSize(); }
	// Get data capacity of the internal array
	RED_INLINE Uint32 DataCapacity() const { return m_bits.DataCapacity(); }

private:

	typedef DynArray< TStorage > StorageData;

	static const Uint32 STORAGE_BITS				= sizeof( TStorage ) * 8;
	static const Uint32 INDEX_SHIFT					= red::alg::Log2< STORAGE_BITS >::Value;
	static const Uint32 BIT_MASK					= STORAGE_BITS - 1;

	RED_INLINE Uint32 StorageIndex( Uint32 index ) const { return index >> INDEX_SHIFT; }
	RED_INLINE TStorage Bit( Uint32 index ) const { return TStorage( 1 ) << ( index & BIT_MASK ); }
	RED_INLINE Uint32 InternalSize() const { return m_bits.Size(); }
	RED_INLINE TStorage LastEntryMask() const { return ( m_bitSize % STORAGE_BITS ) ? ( TStorage( 1 ) << ( m_bitSize % STORAGE_BITS ) ) - 1 : TStorage( -1 ); }

	StorageData			m_bits;
	Uint32				m_bitSize;

	friend class BitSetImplUtils;
	friend class ::BitSetDynamicSerializer;

	static_assert( std::numeric_limits< TStorage >::is_integer, "Only integer types can be used as storage type for BitSetDynamicBase" );
	static_assert( !std::numeric_limits< TStorage >::is_signed, "Only unsigned types can be used as storage type for BitSetDynamicBase" );
};

//////////////////////////////////////////////////////////////////////////

using BitSetDynamic = BitSetDynamicBase< Uint32 >;
using BitSet64Dynamic = BitSetDynamicBase< Uint64 >;

} // red

#include "bitSetDynamic.hpp"
