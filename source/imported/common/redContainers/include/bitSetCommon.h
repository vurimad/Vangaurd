/*
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

namespace red {

class BitSetImplUtils
{
public:

	// Find index of next set bit, starting from the specified 'index' (included); if no such bit was found 'MaxSize' is returned
	template < typename TBitSet >
	RED_INLINE static Uint32 FindNextSet( const TBitSet& bitset, Uint32 index );
	// Find index of next cleared bit, starting from the specified 'index' (included); if no such bit was found 'MaxSize' is returned
	template < typename TBitSet >
	RED_INLINE static Uint32 FindNextClear( const TBitSet& bitset, Uint32 index );

	// Returns true if any of the bits is set to 1
	template < typename TBitSet >
	RED_INLINE static Bool IsAnySet( const TBitSet& bitset );
	// Returns true if any of the bits specified by 'other' is set to 1
	template < typename TBitSet >
	RED_INLINE static Bool IsAnySet( const TBitSet& bitset, const TBitSet& other );
	// Returns true if none of bits is set to 1
	template < typename TBitSet >
	RED_INLINE static Bool IsNoneSet( const TBitSet& bitset );
	// Returns true if none of bits specified by 'other' is set to 1
	template < typename TBitSet >
	RED_INLINE static Bool IsNoneSet( const TBitSet& bitset, const TBitSet& other );
	template < typename TBitSet >
	// Returns true if all bits are set to 1
	RED_INLINE static Bool IsAllSet( const TBitSet& bitset );
	// Returns true if all bits specified by 'other' are set to 1
	template < typename TBitSet >
	RED_INLINE static Bool IsAllSet( const TBitSet& bitset, const TBitSet& other );

	// Bit-wise AND called on 'this' and 'other', storing result in 'this'
	template < typename TBitSet >
	RED_INLINE static void And( TBitSet& bitset, const TBitSet& other );
	// Bit-wise OR called on 'this' and 'other', storing result in 'this'
	template < typename TBitSet >
	RED_INLINE static void Or( TBitSet& bitset, const TBitSet& other );
	// Bit-wise MINUS called on 'this' and 'other', storing result in 'this'
	template < typename TBitSet >
	RED_INLINE static void Minus( TBitSet& bitset, const TBitSet& other );
	// Bit-wise XOR called on 'this' and 'other', storing result in 'this'
	template < typename TBitSet >
	RED_INLINE static void Xor( TBitSet& bitset, const TBitSet& other );

	// Returns the number of set bits
	template< typename TBitSet >
	RED_INLINE static Uint32 PopulationCount( TBitSet& bitset );
};

} // red

#include "bitSetCommon.hpp"
