/*
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

namespace red
{

template< Uint32 MaxSize >
RED_FORCE_INLINE BitSetAtomicLatch<MaxSize>::BitSetAtomicLatch()
{
	ClearAll_MemoryOrderRelaxed();
}

template< Uint32 MaxSize >
RED_FORCE_INLINE Bool BitSetAtomicLatch<MaxSize>::Get( Uint32 index ) const
{
	RED_FATAL_ASSERT( index < Size(), "Out of range index %u (size=%u)", index, Size() );
	const Uint32 wordIndex = GetWordIndex( index );
	const Uint32 bitIndex = GetBitIndex( index );
	const Uint64 bitVal = 1ULL << bitIndex;

	return m_bits[ wordIndex ] & bitVal;
}

template< Uint32 MaxSize >
RED_FORCE_INLINE void BitSetAtomicLatch<MaxSize>::Set_MemoryOrderRelease( Uint32 index )
{
	Bool unused = false;
	Set_MemoryOrderRelease( index, unused );
}

template< Uint32 MaxSize >
RED_FORCE_INLINE void BitSetAtomicLatch<MaxSize>::Set_MemoryOrderRelease( Uint32 index, Bool& outPrevWasSet )
{
	RED_FATAL_ASSERT( index < Size(), "Out of range index %u (size=%u)", index, Size() );
	const Uint32 wordIndex = GetWordIndex( index );
	const Uint32 bitIndex = GetBitIndex( index );
	const Uint64 bitVal = 1ULL << bitIndex;

	// Can only set atomically, so OR is fine, no need for cmpxchg
	const Uint64 prevVal = atomic::Or64( atomic::alias_cast64( &m_bits[ wordIndex ] ), bitVal );
	outPrevWasSet = ( prevVal & bitVal ) != 0;
}

template< Uint32 MaxSize >
RED_FORCE_INLINE void BitSetAtomicLatch<MaxSize>::ClearAll_MemoryOrderRelaxed()
{
	Memzero( m_bits, sizeof( m_bits ) );
}

template< Uint32 MaxSize >
void BitSetAtomicLatch<MaxSize>::Clear_MemoryOrderRelease( Uint32 index )
{
	RED_FATAL_ASSERT( index < Size(), "Out of range index %u (size=%u)", index, Size() );
	const Uint32 wordIndex = GetWordIndex( index );
	const Uint32 bitIndex = GetBitIndex( index );
	const Uint64 bitVal = 1ULL << bitIndex;
	atomic::And64( atomic::alias_cast64( &m_bits[ wordIndex ] ), ~bitVal );
}

template< Uint32 MaxSize >
void BitSetAtomicLatch<MaxSize>::Clear_MemoryOrderRelaxed( Uint32 index )
{
	RED_FATAL_ASSERT( index < Size(), "Out of range index %u (size=%u)", index, Size() );
	const Uint32 wordIndex = GetWordIndex( index );
	const Uint32 bitIndex = GetBitIndex( index );
	const Uint64 bitVal = 1ULL << bitIndex;

	// NOT atomic
	m_bits[ wordIndex ] &= ~bitVal;
}

template< Uint32 MaxSize >
RED_FORCE_INLINE Uint32 BitSetAtomicLatch<MaxSize>::FindNextSet( Uint32 index ) const
{
	if ( index >= Size() )
		return Size();

	Uint32 wordIndex = index >> 6; // div 64
	const Uint32 bitIndex = index & 63; // mod 64

	// NOTE: bits in a word are set from LSB to MSB, so BitScanForward for LSB to MSB.
	const Uint64 searchMask = 0xFFFFFFFFFFFFFFFFULL << bitIndex;
	const Uint64 firstWordToCheck = m_bits[ wordIndex ] & searchMask;
	if ( firstWordToCheck != 0 ) // #todo: bitscan function has a bad API, should just be Bool with an index outparam...
	{
		Uint32 nextBitIndex = (Uint32)red::BitUtils::BitScanForward( firstWordToCheck );
		return GetIndex( wordIndex, nextBitIndex );
	}

	// Look in the next words
	wordIndex += 1;
	for( ; wordIndex < RED_ARRAY_COUNT_U32( m_bits ); ++wordIndex )
	{
		const Uint64 nextWordToCheck = m_bits[ wordIndex ];
		if ( nextWordToCheck != 0 )
		{
			const Uint32 nextBitIndex = (Uint32)red::BitUtils::BitScanForward( nextWordToCheck );
			return GetIndex( wordIndex, nextBitIndex );
		}
	}

	return Size();
}

template< Uint32 MaxSize >
RED_FORCE_INLINE Uint32 BitSetAtomicLatch<MaxSize>::PopulationCount() const
{
	Uint32 count = 0;
	for( Uint32 i = 0; i < INTERNAL_SIZE; ++i )
	{
		count += red::BitUtils::PopulationCount< StorageType >( m_bits[ i ] );
	}
	return count;
}

} // red
