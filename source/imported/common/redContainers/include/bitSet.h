/**
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

#pragma once

#include "bitSetCommon.h"
#include "algorithms.h"
#include "string/string.h"

class BitSetSerializer;

namespace red {

template < Uint32 MaxSize, typename TStorage >
class BitSetBase
{
public:

	typedef TStorage	StorageType;

	// Default constructor
	RED_INLINE BitSetBase();

	// Get pointer to the buffer storing bits
	RED_INLINE const TStorage* Data() const { return m_bits; }
	// Get number of bits stored in the set (equal to 'MaxSize')
	static constexpr Uint32 Size() { return MaxSize; }

	// Returns true if 'other' has the same bits set
	RED_INLINE Bool operator==( const BitSetBase& other ) const;
	// Returns true if 'other' does not have the same bits set
	RED_INLINE Bool operator!=( const BitSetBase& other ) const;
	// Returns true if 'other' doesn't match and the first non-matching bit in 'other' is a 1. Checks left to right.
	RED_INLINE Bool operator<( const BitSetBase& other ) const;

	// Returns true if bit at specified 'index' is set
	RED_INLINE Bool Get( Uint32 index ) const;
	// Set bit at specified 'index' (set its value to 1)
	RED_INLINE void Set( Uint32 index );
	// Clear bit at specified 'index' (set its value to 0)
	RED_INLINE void Clear( Uint32 index );
	// Set bit at specified 'index' to be on or off
	RED_INLINE void Set( Uint32 index, Bool value );
	// Toggle bit at specified 'index' (change 1 to 0 and 0 to 1)
	RED_INLINE void Toggle( Uint32 index );
	// Set all bits to 1
	RED_INLINE void SetAll();
	// Set all bits to 0
	RED_INLINE void ClearAll();
	// Toggle all bits
	RED_INLINE void ToggleAll();

	RED_INLINE void SetBits( Uint32 index, TStorage value );

	// Find index of next set bit, starting from the specified 'index' (included); if no such bit was found 'MaxSize' is returned
	RED_INLINE Uint32 FindNextSet( Uint32 index ) const { return BitSetImplUtils::FindNextSet( *this, index ); }
	// Find index of next cleared bit, starting from the specified 'index' (included); if no such bit was found 'MaxSize' is returned
	RED_INLINE Uint32 FindNextClear( Uint32 index ) const { return BitSetImplUtils::FindNextClear( *this, index ); }

	// Returns true if any of the bits is set to 1
	RED_INLINE Bool IsAnySet() const { return BitSetImplUtils::IsAnySet( *this ); }
	// Returns true if any of the bits specified by 'other' is set to 1
	RED_INLINE Bool IsAnySet( const BitSetBase& other ) const { return BitSetImplUtils::IsAnySet( *this, other ); }
	// Returns true if none of bits is set to 1
	RED_INLINE Bool IsNoneSet() const { return BitSetImplUtils::IsNoneSet( *this ); }
	// Returns true if none of bits specified by 'other' is set to 1
	RED_INLINE Bool IsNoneSet( const BitSetBase& other ) const { return BitSetImplUtils::IsNoneSet( *this, other ); }
	// Returns true if all bits are set to 1
	RED_INLINE Bool IsAllSet() const { return BitSetImplUtils::IsAllSet( *this ); }
	// Returns true if all bits specified by 'other' are set to 1
	RED_INLINE Bool IsAllSet( const BitSetBase& other ) const { return BitSetImplUtils::IsAllSet( *this, other ); }

	// Bit-wise AND called on 'this' and 'other', storing result in 'this'
	RED_INLINE void operator&=( const BitSetBase& other ) { BitSetImplUtils::And( *this, other ); }
	// Bit-wise OR called on 'this' and 'other', storing result in 'this'
	RED_INLINE void operator|=( const BitSetBase& other ) { BitSetImplUtils::Or( *this, other ); }
	// Bit-wise MINUS called on 'this' and 'other', storing result in 'this'
	RED_INLINE void operator-=( const BitSetBase& other ) { BitSetImplUtils::Minus( *this, other ); }
	// Bit-wise XOR called on 'this' and 'other', storing result in 'this'
	RED_INLINE void operator^=( const BitSetBase& other ) { BitSetImplUtils::Xor( *this, other ); }

	// Bit-wise AND called on 'this' and 'other'
	RED_INLINE BitSetBase operator&( const BitSetBase& other ) const { BitSetBase out = *this; out &= other; return out; }
	// Bit-wise OR called on 'this' and 'other'
	RED_INLINE BitSetBase operator|( const BitSetBase& other ) const { BitSetBase out = *this; out |= other; return out; }
	// Bit-wise MINUS called on 'this' and 'other'
	RED_INLINE BitSetBase operator-( const BitSetBase& other ) const { BitSetBase out = *this; out -= other; return out; }
	// Bit-wise XOR called on 'this' and 'other'
	RED_INLINE BitSetBase operator^( const BitSetBase& other ) const { BitSetBase out = *this; out ^= other; return out; }

	// Returns the number of set bits
	RED_INLINE Uint32 PopulationCount() const { return BitSetImplUtils::PopulationCount( *this ); }

	//Adding this to allow us to send bitsets via interop; mayber there's another way? cwalder
	// Reinterpres the bit data in this object as a String
	RED_INLINE String AsString() const;

	// Populate the bit data of this object with the reinterpreted data from the string WARNING: this String should have been preiously been obtained via ConvertToString
	RED_INLINE void BuildFromString( const String &inString );


private:

	static const Uint32 STORAGE_BITS				= sizeof( TStorage ) * 8;
	static const Uint32 INTERNAL_SIZE				= ( MaxSize + STORAGE_BITS - 1 ) / STORAGE_BITS;
	static const Uint32 INDEX_SHIFT					= red::alg::Log2< STORAGE_BITS >::Value;
	static const Uint32 BIT_MASK					= STORAGE_BITS - 1;
	static const TStorage LAST_ENTRY_MASK			= ( MaxSize % STORAGE_BITS ) ? ( TStorage( 1 ) << ( MaxSize % STORAGE_BITS ) ) - 1 : TStorage( -1 );

	RED_INLINE Uint32 StorageIndex( Uint32 index ) const { return index >> INDEX_SHIFT; }
	RED_INLINE TStorage Bit( Uint32 index ) const { return TStorage( 1 ) << ( index & BIT_MASK ); }
	RED_INLINE Uint32 InternalSize() const { return INTERNAL_SIZE; }
	RED_INLINE TStorage LastEntryMask() const { return LAST_ENTRY_MASK; }

	TStorage m_bits[ INTERNAL_SIZE ];

	friend class BitSetImplUtils;
	friend class ::BitSetSerializer;

	static_assert( std::numeric_limits< TStorage >::is_integer, "Only integer types can be used as storage type for BitSetBase" );
	static_assert( !std::numeric_limits< TStorage >::is_signed, "Only unsigned types can be used as storage type for BitSetBase" );
};

//////////////////////////////////////////////////////////////////////////

template< typename T >
class Mask;

template<>
class Mask<Uint32> : public BitSetBase< 32, Uint32 >
{
public:
	RED_INLINE explicit Mask( Uint32 mask )
	{
		const_cast< Uint32& >(*Data()) = mask;
	}

	RED_INLINE Mask( const BitSetBase& base )
		: BitSetBase{ base }
	{}
};

template<>
class Mask<Uint64> : public BitSetBase< 64, Uint64 >
{
public:
	RED_INLINE explicit Mask( Uint64 mask )
	{
		const_cast< Uint64& >(*Data() ) = mask;
	}

	RED_INLINE Mask( const BitSetBase& base )
		: BitSetBase{ base }
	{}
};

template < Uint32 MaxSize >
using BitSet = BitSetBase< MaxSize, Uint32 >;

template < Uint32 MaxSize >
using BitSet64 = BitSetBase< MaxSize, Uint64 >;

//////////////////////////////////////////////////////////////////////////

template< typename TBitSet >
struct BitSetIteratorFindNextSet
{
	static Uint32 Find( const TBitSet& bitset, Uint32 startIndex )
	{
		return bitset.FindNextSet( startIndex );
	}
};

template< typename TBitSet >
struct BitSetIteratorFindNextClear
{
	static Uint32 Find( const TBitSet& bitset, Uint32 startIndex )
	{
		return bitset.FindNextClear( startIndex );
	}
};

template< typename TBitSet, typename TFindPolicy = BitSetIteratorFindNextSet<TBitSet> >
class BitSetConstIterator
{
public:
	RED_INLINE explicit BitSetConstIterator( const TBitSet& bitset )
		: m_bitset( bitset )
		, m_index( TFindPolicy::Find( m_bitset, 0 ) )
	{}

	RED_INLINE Bool IsValid() const
	{
		return m_index < m_bitset.Size();
	}

	RED_INLINE Uint32 GetIndex() const
	{
		return m_index;
	}

	RED_INLINE void FindNext()
	{
		m_index = TFindPolicy::Find( m_bitset, m_index + 1 );
	}

protected:
	const TBitSet& m_bitset;
	Uint32 m_index;
};

template< typename TBitSet, typename TFindPolicy = BitSetIteratorFindNextSet<TBitSet> >
class BitSetIterator : public BitSetConstIterator< TBitSet, TFindPolicy >
{
	using TBaseClass = BitSetConstIterator< TBitSet, TFindPolicy >;

protected:
	using TBaseClass::m_bitset;
	using TBaseClass::m_index;

public:
	RED_INLINE explicit BitSetIterator( const TBitSet& bitset )
		: TBaseClass( bitset )
	{}

	RED_INLINE void ClearCurrent()
	{
		const_cast< TBitSet& >( m_bitset ).Clear( m_index );
	}

	RED_INLINE void SetCurrent()
	{
		const_cast< TBitSet& >( m_bitset ).Set( m_index );
	}
};

} // red

#include "bitSet.hpp"
