/*
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../redSystem/include/bitUtils.h"

namespace red {

//////////////////////////////////////////////////////////////////////////

template < typename TBitSet >
RED_INLINE Uint32 BitSetImplUtils::FindNextSet( const TBitSet& bitset, Uint32 index )
{
	typedef typename TBitSet::StorageType StorageType;

	const Uint32 size = bitset.Size();
	if ( index >= size )
	{
		return size;
	}

	Uint32 storageIndex = bitset.StorageIndex( index );

	const Uint32 bitIndex = index & TBitSet::BIT_MASK;
	// check remaining bits in the same storage cell
	{
		const StorageType bits = bitset.m_bits[ storageIndex ] >> bitIndex;		// shifting bit specified by "index" to least significant position
		if ( bits != 0 )
		{
			// BitScanForward returns position of first bit set (where 0 is position of the least significant bit)
			return index + static_cast< Uint32 >( red::BitUtils::BitScanForward< StorageType >( bits ) );
		}
	}
	index += ( TBitSet::STORAGE_BITS - bitIndex );

	const Uint32 internalSize = bitset.InternalSize();
	while ( ++storageIndex < internalSize )
	{
		const StorageType bits = bitset.m_bits[ storageIndex ];
		if ( bits != 0 )
		{
			return index + static_cast< Uint32 >( red::BitUtils::BitScanForward< StorageType >( bits ) );
		}
		index += TBitSet::STORAGE_BITS;
	}

	return size;
}

//////////////////////////////////////////////////////////////////////////

template < typename TBitSet >
RED_INLINE Uint32 BitSetImplUtils::FindNextClear( const TBitSet& bitset, Uint32 index )
{
	// Find clear bit by just inverting the bits and finding the first set bit... maybe there's a better way?

	typedef typename TBitSet::StorageType StorageType;

	const Uint32 size = bitset.Size();
	if ( index >= size )
	{
		return size;
	}

	Uint32 storageIndex = bitset.StorageIndex( index );

	const Uint32 bitIndex = index & TBitSet::BIT_MASK;
	// check remaining bits in the same storage cell
	{
		const StorageType bits = (~bitset.m_bits[ storageIndex ]) >> bitIndex;		// shifting bit specified by "index" to least significant position
		if ( bits != 0 )
		{
			// BitScanForward returns position of first bit set (where 0 is position of the least significant bit)
			return index + static_cast< Uint32 >( red::BitUtils::BitScanForward< StorageType >( bits ) );
		}
	}
	index += ( TBitSet::STORAGE_BITS - bitIndex );

	const Uint32 internalSize = bitset.InternalSize();
	while ( ++storageIndex < internalSize )
	{
		const StorageType bits = ~bitset.m_bits[ storageIndex ];
		if ( bits != 0 )
		{
			return index + static_cast< Uint32 >( red::BitUtils::BitScanForward< StorageType >( bits ) );
		}
		index += TBitSet::STORAGE_BITS;
	}

	return size;
}

//////////////////////////////////////////////////////////////////////////

template < typename TBitSet >
RED_INLINE Bool BitSetImplUtils::IsAnySet( const TBitSet& bitset )
{
	const Uint32 size = bitset.InternalSize();
	for ( Uint32 i = 0; i < size; ++i )
	{
		if ( bitset.m_bits[ i ] != 0 )
		{
			return true;
		}
	}
	return false;
}

template < typename TBitSet >
RED_INLINE Bool BitSetImplUtils::IsAnySet( const TBitSet& bitset, const TBitSet& other )
{
	const Uint32 size = std::min( bitset.InternalSize(), other.InternalSize() );
	for ( Uint32 i = 0; i < size; ++i )
	{
		if ( ( bitset.m_bits[ i ] & other.m_bits[ i ] ) != 0 )
		{
			return true;
		}
	}
	return false;
}

template < typename TBitSet >
RED_INLINE Bool BitSetImplUtils::IsNoneSet( const TBitSet& bitset )
{
	const Uint32 size = bitset.InternalSize();
	for ( Uint32 i = 0; i < size; ++i )
	{
		if ( bitset.m_bits[ i ] != 0 )
		{
			return false;
		}
	}
	return true;
}

template < typename TBitSet >
RED_INLINE Bool BitSetImplUtils::IsNoneSet( const TBitSet& bitset, const TBitSet& other )
{
	const Uint32 size = std::min( bitset.InternalSize(), other.InternalSize() );
	for ( Uint32 i = 0; i < size; ++i )
	{
		if ( ( bitset.m_bits[ i ] & other.m_bits[ i ] ) != 0 )
		{
			return false;
		}
	}
	return true;
}

template < typename TBitSet >
RED_INLINE Bool BitSetImplUtils::IsAllSet( const TBitSet& bitset )
{
	typedef typename TBitSet::StorageType StorageType;

	const Uint32 size = bitset.InternalSize();
	const StorageType val = StorageType( -1 );
	for ( Uint32 i = 0; i < size - 1; ++i )
	{
		if ( bitset.m_bits[ i ] != val )
		{
			return false;
		}
	}
	return bitset.m_bits[ size - 1 ] == bitset.LastEntryMask();
}

template < typename TBitSet >
RED_INLINE Bool BitSetImplUtils::IsAllSet( const TBitSet& bitset, const TBitSet& other )
{
	typedef typename TBitSet::StorageType StorageType;

	const Uint32 size = std::min( bitset.InternalSize(), other.InternalSize() );
	for ( Uint32 i = 0; i < size; ++i )
	{
		const StorageType otherBits = other.m_bits[ i ];
		if ( ( bitset.m_bits[ i ] & otherBits ) != otherBits )
		{
			return false;
		}
	}
	return true;
}

//////////////////////////////////////////////////////////////////////////

template < typename TBitSet >
RED_INLINE void BitSetImplUtils::And( TBitSet& bitset, const TBitSet& other )
{
	const Uint32 size = std::min( bitset.InternalSize(), other.InternalSize() );
	for ( Uint32 i = 0; i < size; ++i )
	{
		bitset.m_bits[ i ] &= other.m_bits[ i ];
	}
}

template < typename TBitSet >
RED_INLINE void BitSetImplUtils::Or( TBitSet& bitset, const TBitSet& other )
{
	const Uint32 size = std::min( bitset.InternalSize(), other.InternalSize() );
	for ( Uint32 i = 0; i < size; ++i )
	{
		bitset.m_bits[ i ] |= other.m_bits[ i ];
	}
}

template < typename TBitSet >
RED_INLINE void BitSetImplUtils::Minus( TBitSet& bitset, const TBitSet& other )
{
	const Uint32 size = std::min( bitset.InternalSize(), other.InternalSize() );
	for ( Uint32 i = 0; i < size; ++i )
	{
		bitset.m_bits[ i ] &= ~other.m_bits[ i ];
	}
}

template < typename TBitSet >
RED_INLINE void BitSetImplUtils::Xor( TBitSet& bitset, const TBitSet& other )
{
	const Uint32 size = std::min( bitset.InternalSize(), other.InternalSize() );
	for ( Uint32 i = 0; i < size; ++i )
	{
		bitset.m_bits[ i ] ^= other.m_bits[ i ];
	}
}

template< typename TBitSet >
RED_INLINE Uint32 red::BitSetImplUtils::PopulationCount( TBitSet& bitset )
{
	typedef typename TBitSet::StorageType StorageType;

	Uint32 count = 0;

	if ( bitset.InternalSize() > 0 ) // dynamic bitset can be empty
	{
		const Uint32 size = bitset.InternalSize();
		for ( Uint32 i = 0; i < size - 1; ++i )
		{
			count += red::BitUtils::PopulationCount< StorageType >( bitset.m_bits[ i ] );
		}
		count += red::BitUtils::PopulationCount< StorageType >( bitset.m_bits[ size - 1 ] & bitset.LastEntryMask() );	
	}

	return count;
}

} // red