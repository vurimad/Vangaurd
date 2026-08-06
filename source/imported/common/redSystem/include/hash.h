/**
 * Copyright (c) 2013-2021 CDProjekt Red, Inc. All Rights Reserved.
 */

#ifndef _RED_HASH_H_
#define _RED_HASH_H_

#include "utility.h"

// For RotateLeft
#include "bitUtils.h"

// For __m128i
#if defined( RED_PLATFORM_WINPC )
#include <emmintrin.h>
#endif

#if defined( _MSC_VER )
RED_DISABLE_WARNING_MSC( 4307 ) /* unsigned int overflow */
#endif

constexpr Uint32 RED_FNV_OFFSET_BASIS32 = 2166136261u;
constexpr Uint32 RED_FNV_PRIME32        = 16777619u;

constexpr Uint64 RED_FNV_OFFSET_BASIS64 = 14695981039346656037ull;
constexpr Uint64 RED_FNV_PRIME64        = 1099511628211ull;

constexpr Uint32 RED_MURMURHASH_SEED    = 0x5eedba5e;

namespace red
{
	using THash32 = Uint32;
	using THash64 = Uint64;

	template< typename T >
	constexpr THash32 CalculateArrayHash32( const T* key, Uint32 length );

	template< typename T >
	constexpr THash32 CalculatePtrHash32( const T* ptr );

	// Algorithm: Murmurhash3
	// Calculate hash at compile&run time
	constexpr THash32 CalculatePathHash32( const AnsiChar* path );

	constexpr THash32 CalculatePathHash32( const AnsiChar* path, size_t pathLength );

	// Algorithm: Murmur32
	// Calculated at compile&run time
	constexpr THash32 CalculatePathHash32( const Char* path );

	// Algorithm: FNV-1a
	// Calculate hash for a path string at compile&run time ( it will normalize it )
	constexpr THash64 CalculatePathHash64( const AnsiChar* buffer, THash64 hash = RED_FNV_OFFSET_BASIS64 );

	// Algorithm: FNV-1a
	// Calculated at compile&run time ( it will normalize path )
	constexpr THash64 CalculatePathHash64( const Char* buffer, THash64 hash = RED_FNV_OFFSET_BASIS64 );

	// Algorithm: FNV-1a
	// Calculated at compile&run time
	constexpr THash32 CalculateHash32( const void* buffer, size_t bufferSize, THash32 baseHash = RED_FNV_OFFSET_BASIS32 );

	// Algorithm: FNV-1a
	// Calculated at compile&run time
	constexpr THash32 CalculateHash32( const Uint8* buffer, size_t bufferSize, THash32 baseHash = RED_FNV_OFFSET_BASIS32 );

	// Algorithm: FNV-1a
	// Calculated at compile&run time; bufferLength indicates number of Uint32 elements in buffer
	constexpr THash32 CalculateHash32FromUint32Array( const Uint32* buffer, size_t bufferSize, THash32 baseHash = RED_FNV_OFFSET_BASIS32 );

	// Algorithm: FNV-1a
	// Calculated at compile&run time
	constexpr THash32 CalculateHash32( const AnsiChar* str, THash32 baseHash = RED_FNV_OFFSET_BASIS32 );

	// Algorithm: FNV-1a
	// Calculates one hash out of 2 hashes
	constexpr THash32 CombineHashes32( THash32 a, THash32 b );

	// Algorithm: inspired by boost hash_combine in 64-bit version, based on public domain code from MurmurHash by Austin Appleby
	// Calculates one hash out of 2 hashes
	constexpr THash64 CombineHashes64( THash64 a, THash64 b );

	// Algorithm: FNV-1a
	// Calculate ANSI string hash at compile&run time
	constexpr THash32 CalculateAnsiHash32( const AnsiChar* str, THash32 baseHash = RED_FNV_OFFSET_BASIS32 );

	// Algorithm: FNV-1a
	// Calculate lower case ANSI string hash at compile&run time
	constexpr THash32 CalculateAnsiHash32LowerCase( const AnsiChar* buffer, THash32 baseHash = RED_FNV_OFFSET_BASIS32 );

	// Algorithm: FNV-1a
	// Calculate unicode string hash at compile&run time
	constexpr THash32 CalculateUnicodeHash32( const Char* buffer, THash32 baseHash = RED_FNV_OFFSET_BASIS32 );

	// Algorithm: FNV-1a
	// Calculate lower case unicode string hash at compile&run time
	constexpr THash32 CalculateUnicodeHash32LowerCase( const Char* buffer, THash32 baseHash = RED_FNV_OFFSET_BASIS32 );

	// Algorithm: FNV-1a
	// Calculated at compile&run time
	constexpr THash64 CalculateHash64( const void* buffer, size_t bufferSize, THash64 baseHash = RED_FNV_OFFSET_BASIS64 );

	// Algorithm: FNV-1a
	// Calculated at compile&run time
	constexpr THash64 CalculateHash64( const Uint8* buffer, size_t bufferSize, THash64 baseHash = RED_FNV_OFFSET_BASIS64 );

	// TODO rename to match convention (length vs unbounded)
	// Algorithm: FNV-1a
	// Calculated at compile&run time
	constexpr THash64 CalculateAnsiHash64( const AnsiChar* buffer, THash64 baseHash = RED_FNV_OFFSET_BASIS64 );
	constexpr THash64 CalculateHash64( const AnsiChar* buffer, THash64 baseHash = RED_FNV_OFFSET_BASIS64 ); // TODO remove

	// TODO rename to match convention (length vs unbounded)
	// Algorithm: FNV-1a
	// Calculated at compile&run time
	constexpr THash64 CalculateHash64WithLength( const AnsiChar* buffer, size_t bufferSize, THash64 baseHash = RED_FNV_OFFSET_BASIS64 );

	// Algorithm: FNV-1a
	// Calculated at compile&run time
	constexpr THash64 CalculateHash64SkipWhitespaces( const AnsiChar* buffer, size_t bufferSize, THash64 baseHash = RED_FNV_OFFSET_BASIS64 );

	// Algorithm: FNV-1a
	// Calculated at compile&run time
	struct CompileTimeHash32
	{
		template < unsigned int N >
		explicit constexpr CompileTimeHash32( const AnsiChar( &str )[ N ] )
			: m_hash( CalculateHash32( str ) )
		{
		}

		const THash32 m_hash;
	};

	// Algorithm: Murmurhash3
	// Calculated at run time
	// Adapted from the source code obtained at https://code.google.com/p/smhasher/
	class CHash128;

	RED_FORCE_INLINE CHash128 CalculateHash128( const void* buffer, size_t len, Uint32 seed = RED_MURMURHASH_SEED );

	RED_FORCE_INLINE CHash128 CalculateHash128( const AnsiChar* buffer, Uint32 seed = RED_MURMURHASH_SEED );

}

#include "hash.hpp"

#endif // _RED_HASH_H_
