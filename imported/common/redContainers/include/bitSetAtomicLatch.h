/*
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "bitSetCommon.h"

namespace red
{
	// There's no setall, since if > 64 bits it wouldn't be atomically setting all words
	template< Uint32 MaxSize >
	class BitSetAtomicLatch
	{
	public:
		//#tbd: can specialize template for 32bits
		typedef Uint64	StorageType;
		static_assert( MaxSize > 0 && ( MaxSize % ( 8 * sizeof( StorageType ) ) == 0 ), "Should divide evenly for simple bitmask without junk bits at end" );

		BitSetAtomicLatch();
		BitSetAtomicLatch( const BitSetAtomicLatch& ) = delete;
		BitSetAtomicLatch& operator=( BitSetAtomicLatch& ) = delete;

		Bool Get( Uint32 index ) const;

		void Set_MemoryOrderRelease( Uint32 index );
		void Set_MemoryOrderRelease( Uint32 index, Bool& outPrevWasSet );

		void ClearAll_MemoryOrderRelaxed();
		void Clear_MemoryOrderRelaxed( Uint32 index );
		void Clear_MemoryOrderRelease( Uint32 index );

		static constexpr Uint32 Size() { return MaxSize; }
		Uint32 FindNextSet( Uint32 index ) const;

		Uint32 PopulationCount() const;

	private:
		static const Uint32 c_wordShift = 6; // mult/div by 64
		static const Uint32 c_wordMask = 63; // mod 64
		constexpr Uint32 GetWordIndex( Uint32 index ) const { return index >> c_wordShift; }
		constexpr Uint32 GetBitIndex( Uint32 index ) const { return index & c_wordMask; }
		constexpr Uint32 GetIndex( Uint32 wordIndex, Uint32 bitIndex ) const { return ( wordIndex << c_wordShift ) + bitIndex; }

		static const Uint32 INTERNAL_SIZE = MaxSize / ( 8 * sizeof( StorageType ) );
		StorageType m_bits[ INTERNAL_SIZE ]; // don't need to round up because of size condition
	};
}

#include "bitSetAtomicLatch.hpp"
