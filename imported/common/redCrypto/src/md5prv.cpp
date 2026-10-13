/**
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "md5prv.h"

#include "md5.h"

#include "../../redContainers/include/dynArray.h"

namespace crypto
{
	namespace md5
	{
		const red::StaticArray<Uint32, 64> k_array =
		{
			0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee,
			0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
			0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be,
			0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
			0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa,
			0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
			0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed,
			0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
			0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c,
			0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
			0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05,
			0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
			0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039,
			0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
			0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
			0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391
		};

		const red::StaticArray< Uint32, 64> s_array =
		{
			7, 12, 17, 22,  7, 12, 17, 22,  7, 12, 17, 22,  7, 12, 17, 22,
			5,  9, 14, 20,  5,  9, 14, 20,  5,  9, 14, 20,  5,  9, 14, 20,
			4, 11, 16, 23,  4, 11, 16, 23,  4, 11, 16, 23,  4, 11, 16, 23,
			6, 10, 15, 21,  6, 10, 15, 21,  6, 10, 15, 21,  6, 10, 15, 21
		};

		Uint32 LeftRotate( Uint32 x, Uint32 c )
		{
			return ( x << c ) | ( x >> ( 32 - c ) );
		}

		void ResetArray( MD5_Context& ctx )
		{
			ctx.m_arrayFirst = ctx.m_array.Begin();
		}

		void BytesToArray( MD5_Context& ctx, const Uint8*& first, red::StaticArray<Uint32, 16>::iterator arrayLast )
		{
			for ( ; ctx.m_arrayFirst != arrayLast; ++ctx.m_arrayFirst )
			{
				*ctx.m_arrayFirst = *first++;
				*ctx.m_arrayFirst |= *first++ << 8;
				*ctx.m_arrayFirst |= *first++ << 16;
				*ctx.m_arrayFirst |= *first++ << 24;
			}
		}

		void TrueBitToArray( MD5_Context& ctx, const Uint8*& first, std::ptrdiff_t chunkLength )
		{
			switch ( chunkLength % 4 )
			{
			case 0:
				*ctx.m_arrayFirst = 0x00000080;
				break;
			case 1:
				*ctx.m_arrayFirst = *first++;
				*ctx.m_arrayFirst |= 0x00008000;
				break;
			case 2:
				*ctx.m_arrayFirst = *first++;
				*ctx.m_arrayFirst |= *first++ << 8;
				*ctx.m_arrayFirst |= 0x00800000;
				break;
			case 3:
				*ctx.m_arrayFirst = *first++;
				*ctx.m_arrayFirst |= *first++ << 8;
				*ctx.m_arrayFirst |= *first++ << 16;
				*ctx.m_arrayFirst |= 0x80000000;
				break;
			}
			++ctx.m_arrayFirst;
		}

		void ZerosToArray( MD5_Context& ctx, red::StaticArray<Uint32, 16>::iterator arrayLast )
		{
			for ( ; ctx.m_arrayFirst != arrayLast; ++ctx.m_arrayFirst )
			{
				*ctx.m_arrayFirst = 0;
			}
		}

		void OriginalLengthBitsToArray( MD5_Context& ctx, Uint64 originalLengthBits )
		{
			originalLengthBits &= 0xffffffffffffffff;
			*ctx.m_arrayFirst++ = ( originalLengthBits ) & 0x00000000ffffffff;
			*ctx.m_arrayFirst++ = ( originalLengthBits & 0xffffffff00000000 ) >> 32;
		}

		void HashChunk( MD5_Context& ctx )
		{
			Uint32 A = ctx.m_hash.m_a;
			Uint32 B = ctx.m_hash.m_b;
			Uint32 C = ctx.m_hash.m_c;
			Uint32 D = ctx.m_hash.m_d;

			Uint32 F;
			unsigned int g;

			for ( unsigned int i = 0; i < 64; ++i )
			{
				if ( i < 16 )
				{
					F = ( B & C ) | ( ( ~B ) & D );
					g = i;
				}
				else if ( i < 32 )
				{
					F = ( D & B ) | ( ( ~D ) & C );
					g = ( 5 * i + 1 ) & 0xf;
				}
				else if ( i < 48 )
				{
					F = B ^ C ^ D;
					g = ( 3 * i + 5 ) & 0xf;
				}
				else
				{
					F = C ^ ( B | ( ~D ) );
					g = ( 7 * i ) & 0xf;
				}

				Uint32 D_temp = D;
				D = C;
				C = B;
				B += LeftRotate( A + F + k_array[i] + ctx.m_array[g], s_array[i] );
				A = D_temp;
			}

			ctx.m_hash.m_a += A;
			ctx.m_hash.m_b += B;
			ctx.m_hash.m_c += C;
			ctx.m_hash.m_d += D;
		}

		void Update( MD5_Context& ctx, const void* buffer, const red::Uint64 size )
		{
			const Uint8* first = (Uint8*)buffer;
			const Uint8* last = (Uint8*)buffer + size;
			const Uint64 originalLengthBits = std::distance( first, last ) * 8;

			ctx.m_array.Resize( 16 );

			std::ptrdiff_t chunkLength;
			while ( ( chunkLength = std::distance( first, last ) ) >= 64 )
			{
				ResetArray( ctx );
				BytesToArray( ctx, first, ctx.m_array.End() );
				HashChunk( ctx );
			}

			ResetArray( ctx );
			BytesToArray( ctx, first, ctx.m_array.Begin() + chunkLength / 4 );
			TrueBitToArray( ctx, first, chunkLength );

			if ( chunkLength >= 56 )
			{
				ZerosToArray( ctx, ctx.m_array.End() );
				HashChunk( ctx );

				ResetArray( ctx );
				ZerosToArray( ctx, ctx.m_array.End() - 2 );
				OriginalLengthBitsToArray( ctx, originalLengthBits );
				HashChunk( ctx );
			}
			else
			{
				ZerosToArray( ctx, ctx.m_array.End() - 2 );
				OriginalLengthBitsToArray( ctx, originalLengthBits );
				HashChunk( ctx );
			}
		}
	}
}