/**
 * Copyright (c) 2017-2021 CDProjekt Red, Inc. All Rights Reserved.
 */

#ifndef _RED_HASH_HPP_
#define _RED_HASH_HPP_

namespace red
{

	// Algorithm: Murmurhash3
	// Calculate hash at compile&run time
	namespace murmur
	{
		constexpr Uint32 c1 = 0xcc9e2d51;
		constexpr Uint32 c2 = 0x1b873593;
		constexpr Uint32 n = 0xe6546b64;

		constexpr Uint32 Murmur3KeyChunks(const char* s, size_t length)
		{
			return (length > 0 ? static_cast<Uint32>(s[0]) : 0)
				+ (length > 1 ? (static_cast<Uint32>(s[1]) << 8) : 0)
				+ (length > 2 ? (static_cast<Uint32>(s[2]) << 16) : 0)
				+ (length > 3 ? (static_cast<Uint32>(s[3]) << 24) : 0);
		}

		constexpr Uint32 Murmur3KeyChunks(const char* s)
		{
			return Murmur3KeyChunks(s, 4);
		}

		constexpr Uint32 Murmur3Key(Uint32 k)
		{
			return (((k * c1) << 15) | ((k * c1) >> 17)) * c2;
		}

		constexpr Uint32 Murmur3HashRound(Uint32 k, Uint32 hash)
		{
			return (((hash ^ k) << 13) | ((hash ^ k) >> 19)) * 5 + n;
		}

		constexpr Uint32 Murmur3Loop(const char* key, size_t length, Uint32 hash)
		{
			return length == 0 ? hash : Murmur3Loop(key + 4, length - 1, Murmur3HashRound(Murmur3Key(Murmur3KeyChunks(key)), hash));
		}

		constexpr Uint32 Murmur3End0(Uint32 k)
		{
			return (((k * c1) << 15) | ((k * c1) >> 17)) * c2;
		}

		constexpr Uint32 Murmur3End1(Uint32 k, const char* key)
		{
			return Murmur3End0(k ^ static_cast<Uint32>(key[0]));
		}

		constexpr Uint32 Murmur3End2(Uint32 k, const char* key)
		{
			return Murmur3End1(k ^ (static_cast<Uint32>(key[1]) << 8), key);
		}
		constexpr Uint32 Murmur3End3(Uint32 k, const char* key)
		{
			return Murmur3End2(k ^ (static_cast<Uint32>(key[2]) << 16), key);
		}

		constexpr Uint32 Murmur3End(Uint32 hash, const char* key, int rem)
		{
			return rem == 0 ? hash : hash ^ (rem == 3 ? Murmur3End3(0, key) : rem == 2 ? Murmur3End2(0, key) : Murmur3End1(0, key));
		}

		constexpr Uint32 Murmur3Final1(Uint32 hash)
		{
			return (hash ^ (hash >> 16)) * 0x85ebca6b;
		}
		constexpr Uint32 Murmur3Final2(Uint32 hash)
		{
			return (hash ^ (hash >> 13)) * 0xc2b2ae35;
		}
		constexpr Uint32 Murmur3Final3(Uint32 hash)
		{
			return (hash ^ (hash >> 16));
		}

		constexpr Uint32 Murmur3Final(Uint32 hash, size_t length)
		{
			return Murmur3Final3(Murmur3Final2(Murmur3Final1(hash ^ static_cast<Uint32>(length))));
		}

		constexpr Uint32 Murmur3Value(const char* key, size_t length, Uint32 seed)
		{
			return Murmur3Final(Murmur3End(Murmur3Loop(key, length / 4, seed), key + (length / 4) * 4, length & 3), length);
		}

		constexpr size_t StrLen(const char* str)
		{
			size_t result{};
			while (*str)
			{
				++result;
				++str;
			}

			return result;
		}

		constexpr Uint32 Murmur3(const char *key, size_t length, Uint32 seed)
		{
			return murmur::Murmur3Value(key, length, seed);
		}
	}

	// This is disabled as we don't have support for console platforms yet - so we use the basic non-smid implementation.
	//#define RED_HASH_128_SIMD

	// Algorithm: Murmurhash3
	// Calculated at run time
	// Adapted from the source code obtained at https://code.google.com/p/smhasher/
	class CHash128
	{
	protected:
#ifdef RED_HASH_128_SIMD
#	if defined( RED_COMPILER_MSC )

		struct SHash128Simd
		{
			__m128i m_value;

			RED_INLINE SHash128Simd()
				: m_value(_mm_setzero_si128())
			{}

			RED_INLINE SHash128Simd(const SHash128Simd& other)
				: m_value(other.m_value)
			{
			}

			// Comparisons
			RED_INLINE Bool operator==(const SHash128Simd& other) const
			{
				// SIMD4.1
				// return _mm_testc_si128( m_value, other.m_value ) == 1;

				// SIMD2
				__m128i result = _mm_cmpeq_epi32(m_value, other.m_value);
				return _mm_movemask_epi8(result) == 0xffff;
			}

			RED_INLINE Bool operator<(const SHash128Simd& other) const
			{
				// SIMD2
				// Compare each as a set of 4 int32s
				__m128i lt = _mm_cmplt_epi32(m_value, other.m_value);
				__m128i gt = _mm_cmpgt_epi32(m_value, other.m_value);

				// _mm_movemask_epi8 seems to put the most significant part into the least significant bits of the mask...
				int ltmask = _mm_movemask_epi8(lt);
				int gtmask = _mm_movemask_epi8(gt);

				for (Uint32 i = 0; i < (sizeof(__m128i) / sizeof(int)); ++i)
				{
					// So we test the 8421 bits
					if (ltmask & 0x0000000f)
					{
						return true;
					}
					else if (gtmask & 0x0000000f)
					{
						return false;
					}

					// And shift right upon equality for the next lesser significant bits
					ltmask >>= 4;
					gtmask >>= 4;
				}

				return false;
			}

			RED_INLINE Uint64 operator[](Uint32 index) const
			{
				return reinterpret_cast<const Uint64*>(&m_value)[index];
			}

			RED_INLINE Uint64& operator[](Uint32 index)
			{
				return reinterpret_cast<Uint64*>(&m_value)[index];
			}
		};

		typedef SHash128Simd SHash128;
#	else
#	error CHash128 internal type not implemented for this compiler
#	endif
#else
		struct SHash128Std
		{
			static const Uint32 NUM_PARTS = 2u;
			static const Uint32 SIZE = NUM_PARTS * sizeof(Uint64);

			Uint64 m_parts[NUM_PARTS];

			RED_INLINE SHash128Std()
			{
				red::Memzero(m_parts, SIZE);
			}

			RED_INLINE SHash128Std(const SHash128Std& other)
			{
				red::Memcpy(m_parts, other.m_parts, SIZE);
			}

			RED_INLINE SHash128Std& operator=( const SHash128Std& other )
			{
				if ( this != &other )
				{
					red::Memcpy( m_parts, other.m_parts, SIZE );
				}

				return *this;
			}

			RED_INLINE Bool operator==(const SHash128Std& other) const
			{
				return red::Memcmp(&m_parts, &other.m_parts, SIZE) == 0;
			}

			RED_INLINE Bool operator<(const SHash128Std& other) const
			{
				for (Uint32 i = 0; i < NUM_PARTS; ++i)
				{
					if (m_parts[i] < other.m_parts[i])
					{
						// Less than
						return true;
					}
					else if (m_parts[i] > other.m_parts[i])
					{
						// Greater than
						return false;
					}
				}

				// All parts are equal
				return false;
			}

			RED_INLINE Uint64 operator[](Uint32 index) const
			{
				return m_parts[index];
			}

			RED_INLINE Uint64& operator[](Uint32 index)
			{
				return m_parts[index];
			}
		};

		typedef SHash128Std SHash128;

#endif // RED_HASH_128_SIMD

	public:
		static const Uint32 NUM_PARTS_64 = 2u;

	protected:
		static const Uint64 BIG_CONSTANT_ONE = 0xff51afd7ed558ccd;
		static const Uint64 BIG_CONSTANT_TWO = 0xc4ceb9fe1a85ec53;
		static const Uint64 BIG_CONSTANT_THREE = 0x87c37b91114253d5;
		static const Uint64 BIG_CONSTANT_FOUR = 0x4cf5ad432745937f;
		static const size_t BLOCK_SIZE = sizeof(SHash128);

	protected:
		SHash128 m_hash;

	public:
		RED_INLINE CHash128() {}
		RED_INLINE CHash128(const void* buffer, size_t len, Uint32 seed = RED_MURMURHASH_SEED) { Calculate(buffer, len, seed); }
		RED_INLINE CHash128(const AnsiChar* buffer, Uint32 seed = RED_MURMURHASH_SEED) { Calculate(buffer, red::Strlen(buffer) * sizeof(AnsiChar), seed); }
		RED_INLINE CHash128(const UniChar* buffer, Uint32 seed = RED_MURMURHASH_SEED) { Calculate(buffer, red::Strlen(buffer) * sizeof(UniChar), seed); }
		RED_INLINE CHash128(const CHash128& other) : m_hash(other.m_hash) {}

		RED_INLINE Bool operator==(const CHash128& other) const
		{
			return m_hash == other.m_hash;
		}

		RED_INLINE Bool operator!=(const CHash128& other) const
		{
			return !(*this == other);
		}

		RED_INLINE Bool operator<(const CHash128& other) const
		{
			return m_hash < other.m_hash;
		}

		RED_INLINE Uint64 operator[](Uint32 index) const
		{
			return m_hash[index];
		}

		RED_INLINE Uint64& operator[](Uint32 index)
		{
			return m_hash[index];
		}

		RED_INLINE Bool IsZero() const { return *this == CHash128(); }

		RED_INLINE Bool ToString(AnsiChar* buf, const Uint32 bufSize) const
		{
			if (bufSize < STRING_BUF_SIZE + 1) // count the null termination
				return false;

			red::SNPrintFUnsafe(buf, bufSize, "%08X-%08X-%08X-%08X",
				(m_hash[0] >> 32) & 0xFFFFFFFF,
				(m_hash[0] >> 0) & 0xFFFFFFFF,
				(m_hash[1] >> 32) & 0xFFFFFFFF,
				(m_hash[1] >> 0) & 0xFFFFFFFF);

			return true;
		}

	protected:
		static const Uint32 STRING_BUF_SIZE = 4 * 9 + 1; // Hash128 is saved as: FFFFFFFF-FFFFFFFF-FFFFFFFF-FFFFFFFF

		RED_INLINE Uint64 fmix64(Uint64 k)
		{
			k ^= k >> 33;
			k *= BIG_CONSTANT_ONE;
			k ^= k >> 33;
			k *= BIG_CONSTANT_TWO;
			k ^= k >> 33;

			return k;
		}

		RED_INLINE void Calculate(const void* buffer, size_t len, Uint32 seed)
		{
			using red::BitUtils::RotateLeft;

			const Uint8* data = static_cast<const Uint8*>(buffer);
			const size_t nblocks = len / BLOCK_SIZE;

			Uint64 h1 = seed;
			Uint64 h2 = seed;

			//----------
			// body
			const Uint64* blocks = static_cast<const Uint64*>(buffer);

			for (size_t i = 0; i < nblocks; ++i)
			{
				Uint64 k1 = blocks[i * 2 + 0];
				Uint64 k2 = blocks[i * 2 + 1];

				k1 *= BIG_CONSTANT_THREE;
				k1 = RotateLeft(k1, 31);
				k1 *= BIG_CONSTANT_FOUR;
				h1 ^= k1;

				h1 = RotateLeft(h1, 27);
				h1 += h2;
				h1 = h1 * 5 + 0x52dce729;

				k2 *= BIG_CONSTANT_FOUR;
				k2 = RotateLeft(k2, 33);
				k2 *= BIG_CONSTANT_THREE;
				h2 ^= k2;

				h2 = RotateLeft(h2, 31);
				h2 += h1;
				h2 = h2 * 5 + 0x38495ab5;
			}

			//----------
			// tail

			const Uint8* tail = data + nblocks * BLOCK_SIZE;

			Uint64 k1 = 0;
			Uint64 k2 = 0;

			switch (len & (BLOCK_SIZE - 1))
			{
			case 15: k2 ^= static_cast<Uint64>(tail[14]) << 48;
			case 14: k2 ^= static_cast<Uint64>(tail[13]) << 40;
			case 13: k2 ^= static_cast<Uint64>(tail[12]) << 32;
			case 12: k2 ^= static_cast<Uint64>(tail[11]) << 24;
			case 11: k2 ^= static_cast<Uint64>(tail[10]) << 16;
			case 10: k2 ^= static_cast<Uint64>(tail[9]) << 8;
			case  9: k2 ^= static_cast<Uint64>(tail[8]) << 0;
				k2 *= BIG_CONSTANT_FOUR;
				k2 = RotateLeft(k2, 33);
				k2 *= BIG_CONSTANT_THREE;
				h2 ^= k2;

			case  8: k1 ^= static_cast<Uint64>(tail[7]) << 56;
			case  7: k1 ^= static_cast<Uint64>(tail[6]) << 48;
			case  6: k1 ^= static_cast<Uint64>(tail[5]) << 40;
			case  5: k1 ^= static_cast<Uint64>(tail[4]) << 32;
			case  4: k1 ^= static_cast<Uint64>(tail[3]) << 24;
			case  3: k1 ^= static_cast<Uint64>(tail[2]) << 16;
			case  2: k1 ^= static_cast<Uint64>(tail[1]) << 8;
			case  1: k1 ^= static_cast<Uint64>(tail[0]) << 0;
				k1 *= BIG_CONSTANT_THREE;
				k1 = RotateLeft(k1, 31);
				k1 *= BIG_CONSTANT_FOUR;
				h1 ^= k1;
			};

			//----------
			// finalization

			h1 ^= len;
			h2 ^= len;

			h1 += h2;
			h2 += h1;

			h1 = fmix64(h1);
			h2 = fmix64(h2);

			h1 += h2;
			h2 += h1;

			m_hash[0] = h1;
			m_hash[1] = h2;
		}
	};

	template< typename T >
	constexpr THash32 CalculateArrayHash32( const T* key, Uint32 length )
	{
		// Set up the internal state
		Uint32 offset = 0;
		THash32 a = 0x9e3779b9;					// the golden ratio; an arbitrary value
		THash32 b = 0x9e3779b9;					// the golden ratio; an arbitrary value
		THash32 c = 0x0;						// variable initialization of internal state
		const Uint32 originalLength = length;

#define HashMix(a,b,c)				 \
		{							 \
		a=a-b;  a=a-c;  a=a^(c>>13); \
		b=b-c;  b=b-a;  b=b^(a<<8);  \
		c=c-a;  c=c-b;  c=c^(b>>13); \
		a=a-b;  a=a-c;  a=a^(c>>12); \
		b=b-c;  b=b-a;  b=b^(a<<16); \
		c=c-a;  c=c-b;  c=c^(b>>5);  \
		a=a-b;  a=a-c;  a=a^(c>>3);  \
		b=b-c;  b=b-a;  b=b^(a<<10); \
		c=c-a;  c=c-b;  c=c^(b>>15); \
		}

		// Handle case when key length is >= 12 chars
		while (length >= 12)
		{
			a = a + (key[offset + 0] + ((Uint32)key[offset + 1] << 8) + ((Uint32)key[offset + 2] << 16) + ((Uint32)key[offset + 3] << 24));
			b = b + (key[offset + 4] + ((Uint32)key[offset + 5] << 8) + ((Uint32)key[offset + 6] << 16) + ((Uint32)key[offset + 7] << 24));
			c = c + (key[offset + 8] + ((Uint32)key[offset + 9] << 8) + ((Uint32)key[offset + 10] << 16) + ((Uint32)key[offset + 11] << 24));
			HashMix(a, b, c);
			offset += 12;
			length -= 12;
		}

		// Handle the last 11 chars. All the case statements fall through
		c = c + originalLength;
		switch (length)
		{
		case 11: c = c + ((Uint32)key[offset + 10] << 24);
		case 10: c = c + ((Uint32)key[offset + 9] << 16);
		case 9: c = c + ((Uint32)key[offset + 8] << 8);
		case 8: b = b + ((Uint32)key[offset + 7] << 24);
		case 7: b = b + ((Uint32)key[offset + 6] << 16);
		case 6: b = b + ((Uint32)key[offset + 5] << 8);
		case 5: b = b + ((Uint32)key[offset + 4]);
		case 4: a = a + ((Uint32)key[offset + 3] << 24);
		case 3: a = a + ((Uint32)key[offset + 2] << 16);
		case 2: a = a + ((Uint32)key[offset + 1] << 8);
		case 1: a = a + ((Uint32)key[offset + 0]);
		}
		HashMix(a, b, c);

#undef HashMix

		return c;
	}

	template< typename T >
	constexpr THash32 CalculatePtrHash32( const T* ptr )
	{
		return CalculateHash32( &ptr, sizeof( T* ) );
	}

	constexpr THash32 CalculatePathHash32( const AnsiChar* path )
	{
		return murmur::Murmur3( path, murmur::StrLen( path ), RED_MURMURHASH_SEED );
	}

	constexpr THash32 CalculatePathHash32( const AnsiChar* path, size_t pathLength )
	{
		return murmur::Murmur3( path, pathLength, RED_MURMURHASH_SEED );
	}

	constexpr THash32 CalculatePathHash32( const Char* path )
	{
		AnsiChar buf[ 512 ]{};

		// ultra lame unicode->Ansi conversion
		const Char* read = path;
		AnsiChar* write = buf;
		while ( *read && write < &buf[ RED_ARRAY_COUNT_U32( buf ) - 1] )
		{
			*write++ = static_cast< AnsiChar >( *read++ );
		}
		*write = 0;

		// calculate path from ANSI string
		return CalculatePathHash32( buf );
	}

	constexpr AnsiChar ToLower( const AnsiChar c )
	{
		return ( c >= 'A' && c <= 'Z' ) ? ( c - 'A' ) + 'a' : c;
	}

	constexpr Char ToLower( const Char c )
	{
		return ( c >= static_cast< Char >( 'A' ) && c <= static_cast< Char >( 'Z' ) ) ? ( c - static_cast< Char >( 'A' ) ) + static_cast< Char >( 'a' ) : c;
	}

	// Algorithm: FNV-1a
	// Calculate hash for large input at compile&run time
	template< typename T, typename F >
	constexpr THash32 CalculateHash32( F&& functor, const T* buffer, THash32 baseHash = RED_FNV_PRIME32 )
	{
		auto hash = baseHash;
		while ( const auto next = *buffer )
		{
			hash ^= functor( next );
			hash *= RED_FNV_PRIME32;
			++buffer;
		}

		return hash;
	}

	constexpr THash64 CalculatePathHash64( const AnsiChar* buffer, THash64 baseHash)
	{
		auto hash = baseHash;
		if( buffer )
		{
			while( const auto next = *buffer )
			{
				hash ^= next == '/' ? '\\' : ToLower( next );
				hash *= RED_FNV_PRIME64;

				++buffer;
			}
		}
		return hash;
	}

	constexpr THash64 CalculatePathHash64( const Char* buffer, THash64 baseHash)
	{
		auto hash = baseHash;
		if ( buffer )
		{
			while ( const auto next = *buffer )
			{
				hash ^= static_cast< AnsiChar >( next ) == '/' ? '\\' : ToLower( static_cast< AnsiChar >( next ) );
				hash *= RED_FNV_PRIME64;

				++buffer;
			}
		}
		return hash;
	}

	constexpr THash32 CalculateHash32( const void* buffer, size_t bufferSize, THash32 baseHash )
	{
		auto data = static_cast< const Uint8* >( buffer );
		auto hash = baseHash;
		while ( bufferSize-- )
		{
			hash ^= *data++;
			hash *= RED_FNV_PRIME32;
		}

		return hash;
	}

	constexpr THash32 CalculateHash32( const Uint8* buffer, size_t bufferSize, THash32 baseHash )
	{
		auto data = static_cast< const Uint8* >( buffer );
		auto hash = baseHash;
		while ( bufferSize-- )
		{
			hash ^= *data++;
			hash *= RED_FNV_PRIME32;
		}

		return hash;
	}

	constexpr THash32 CalculateHash32( const AnsiChar* buffer, THash32 baseHash )
	{
		auto hash = baseHash;
		if ( buffer )
		{
			while ( const auto next = *buffer )
			{
				hash ^= next;
				hash *= RED_FNV_PRIME32;
				++buffer;
			}
		}

		return hash;
	}

	constexpr THash32 CalculateAnsiHash32( const AnsiChar* str, THash32 baseHash )
	{
		return CalculateHash32( str, baseHash );
	}

	constexpr THash32 CalculateHash32FromUint32Array( const Uint32* buffer, size_t bufferSize, THash32 baseHash )
	{
		auto data = buffer;
		auto hash = baseHash;
		while ( bufferSize-- )
		{
			hash ^= *data++;
			hash *= RED_FNV_PRIME32;
		}

		return hash;
	}

	constexpr THash32 CombineHashes32( const THash32 a, const THash32 b )
	{
		return static_cast< THash32 >( ( ( static_cast< THash32 >( ( RED_FNV_OFFSET_BASIS32 ^ a ) * static_cast< THash64 >( RED_FNV_PRIME32 ) ) ) ^ b ) * static_cast< THash64 >( RED_FNV_PRIME32 ) );
	}

	constexpr THash64 CombineHashes64( const THash64 a, const THash64 b )
	{
		constexpr auto m = THash64( 0xc6a4a7935bd1e995ULL );
		auto k = b * m;
		k ^= k >> 47;
		return (a ^ (k * m)) * m;
	}

	constexpr THash32 CalculateAnsiHash32LowerCase( const AnsiChar* buffer, THash32 baseHash )
	{
		auto hash = baseHash;
		if ( buffer )
		{
			while ( const auto next = *buffer )
			{
				hash ^= ToLower( next );
				hash *= RED_FNV_PRIME32;
				++buffer;
			}
		}
		return hash;
	}

	constexpr THash32 CalculateUnicodeHash32( const Char* buffer, THash32 baseHash )
	{
		auto hash = baseHash;
		while ( const auto next = *buffer )
		{
			hash ^= next;
			hash *= RED_FNV_PRIME32;
			++buffer;
		}

		return hash;
	}

	constexpr THash32 CalculateUnicodeHash32LowerCase( const Char* buffer, THash32 baseHash )
	{
		auto hash = baseHash;
		if ( buffer )
		{
			while ( const auto next = *buffer )
			{
				hash ^= ToLower( next );
				hash *= RED_FNV_PRIME32;
				++buffer;
			}
		}
		return hash;
	}

	constexpr THash64 CalculateHash64( const void* buffer, size_t bufferSize, THash64 baseHash )
	{
		auto data = static_cast< const Uint8* >( buffer );
		auto hash = baseHash;
		while ( bufferSize-- )
		{
			hash ^= *data++;
			hash *= RED_FNV_PRIME64;
		}

		return hash;
	}

	constexpr THash64 CalculateHash64( const Uint8* buffer, size_t bufferSize, THash64 baseHash )
	{
		auto data = static_cast< const Uint8* >( buffer );
		auto hash = baseHash;
		while ( bufferSize-- )
		{
			hash ^= *data++;
			hash *= RED_FNV_PRIME64;
		}

		return hash;
	}

	// TODO rename to CalculateAnsiHash64 (kept as primary to preserve current callstack)
	constexpr THash64 CalculateHash64( const AnsiChar* buffer, THash64 baseHash )
	{
		auto hash = baseHash;
		if ( buffer )
		{
			while ( const auto next = *buffer )
			{
				hash ^= next;
				hash *= RED_FNV_PRIME64;
				++buffer;
			}
		}

		return hash;
	}

	constexpr THash64 CalculateAnsiHash64( const AnsiChar* buffer, THash64 baseHash )
	{
		return CalculateHash64( buffer, baseHash );
	}

	// TODO rename to match convention (length vs unbounded)
	// CalculateHash64
	constexpr THash64 CalculateHash64WithLength( const AnsiChar* buffer, size_t bufferSize, THash64 baseHash )
	{
		auto hash = baseHash;
		while ( bufferSize-- )
		{
			hash ^= *buffer++;
			hash *= RED_FNV_PRIME64;
		}

		return hash;
	}

	constexpr THash64 CalculateHash64SkipWhitespaces( const AnsiChar* buffer, size_t bufferSize, THash64 baseHash )
	{
		auto hash = baseHash;
		while ( bufferSize-- )
		{
			const auto c = *buffer++;
			if ( c != ' ' && c != '\t' && c != '\r' && c != '\n' )
			{
				hash ^= c;
				hash *= RED_FNV_PRIME64;
			}
		}

		return hash;
	}

	RED_FORCE_INLINE CHash128 CalculateHash128( const void* buffer, size_t len, Uint32 seed )
	{
		return CHash128( buffer, len, seed );
	}

	RED_FORCE_INLINE CHash128 CalculateHash128( const AnsiChar* buffer, Uint32 seed )
	{
		return CHash128( buffer, seed );
	}

}

#endif // _RED_HASH_HPP_