#pragma once

namespace red
{
	constexpr Uint16 SwapBytes16( Uint16 bytes )
	{
		return ( bytes & 0xFF ) << 8 | ( bytes & 0xFF00 ) >> 8;
	}

	constexpr Uint32 SwapBytes32( Uint32 bytes )
	{
		return  ( bytes << 24 ) |
				( bytes & 0xFF00 ) << 8 |
				( ( bytes >> 8 ) & 0xFF00 ) |
				( bytes >> 24 );
	}

	constexpr Uint64 SwapBytes64( Uint64 bytes )
	{
		return
			( bytes << 56 ) |
			( bytes & 0xFF00ULL ) << 40 |
			( bytes & 0xFF0000ULL ) << 24 |
			( bytes & 0xFF000000ULL ) << 8 |
			( ( bytes >> 8 ) & 0xFF000000ULL ) |
			( ( bytes >> 24 ) & 0xFF0000ULL ) |
			( ( bytes >> 40 ) & 0xFF00ULL ) |
			( bytes >> 56 );
	}
}
