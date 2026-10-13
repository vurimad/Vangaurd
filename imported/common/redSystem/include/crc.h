/*
 * Copyright (c) 2013 CDProjekt Red, Inc. All Rights Reserved.
 */
#pragma once

// Use these defines to selectively enable and disable which of the CRC sizes
// uses a faster but larger sliced table algorithm versus the regular simple table
// implementation. The sliced table based approaches sacrifice additional memory
// to generate a larger lookup table which speeds up the calculation.
// The normal table approaches are 1kb for CRC32 and 2kb for CRC64 while the
// sliced tables used 4kb for CRC32 and 16kb for CRC64.
#define CRC32_USE_SLICED_TABLE_IMPLEMENTATION
#define CRC64_USE_SLICED_TABLE_IMPLEMENTATION

namespace red
{

constexpr Uint32 InitialValueCRC32 = 0;
constexpr Uint64 InitialValueCRC64 = 0ull;

REDSYSTEM_API Uint32 CalculateCRC32( const void* buffer, Uint32 size, Uint32 existingCrc = InitialValueCRC32 );
REDSYSTEM_API Uint64 CalculateCRC64( const void* buffer, Uint32 size, Uint64 existingCrc = InitialValueCRC64 );


// The private functions are exported for testing purposes
namespace prv
{
#ifdef CRC32_USE_SLICED_TABLE_IMPLEMENTATION
REDSYSTEM_API Uint32 CalculateCRC32_Sliced( const void* buffer, Uint32 size, Uint32 existingCrc );
#endif

#ifdef CRC64_USE_SLICED_TABLE_IMPLEMENTATION
REDSYSTEM_API Uint64 CalculateCRC64_Sliced( const void* buffer, Uint32 size, Uint64 existingCrc );
#endif

REDSYSTEM_API Uint32 CalculateCRC32_Simple( const void* buffer, Uint32 size, Uint32 existingCrc );
REDSYSTEM_API Uint64 CalculateCRC64_Simple( const void* buffer, Uint32 size, Uint64 existingCrc );
} // prv

} // red
