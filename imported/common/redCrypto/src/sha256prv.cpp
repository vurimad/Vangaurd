/**
* Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "sha256prv.h"

#define SHA2_SHFR(x, n)    (x >> n)
#define SHA2_ROTR(x, n)   ((x >> n) | (x << ((sizeof(x) << 3) - n)))
#define SHA2_ROTL(x, n)   ((x << n) | (x >> ((sizeof(x) << 3) - n)))
#define SHA2_CH(x, y, z)  ((x & y) ^ (~x & z))
#define SHA2_MAJ(x, y, z) ((x & y) ^ (x & z) ^ (y & z))
#define SHA256_F1(x) (SHA2_ROTR(x,  2) ^ SHA2_ROTR(x, 13) ^ SHA2_ROTR(x, 22))
#define SHA256_F2(x) (SHA2_ROTR(x,  6) ^ SHA2_ROTR(x, 11) ^ SHA2_ROTR(x, 25))
#define SHA256_F3(x) (SHA2_ROTR(x,  7) ^ SHA2_ROTR(x, 18) ^ SHA2_SHFR(x,  3))
#define SHA256_F4(x) (SHA2_ROTR(x, 17) ^ SHA2_ROTR(x, 19) ^ SHA2_SHFR(x, 10))
#define SHA2_UNPACK32(x, str)                 \
{                                             \
	*((str) + 3) = (red::Uint8) ((x)      );       \
	*((str) + 2) = (red::Uint8) ((x) >>  8);       \
	*((str) + 1) = (red::Uint8) ((x) >> 16);       \
	*((str) + 0) = (red::Uint8) ((x) >> 24);       \
}
#define SHA2_PACK32(str, x)                   \
{                                             \
	*(x) =   ((red::Uint32) *((str) + 3)      )    \
	| ((red::Uint32) *((str) + 2) <<  8)    \
	| ((red::Uint32) *((str) + 1) << 16)    \
	| ((red::Uint32) *((str) + 0) << 24);   \
}

namespace crypto
{
	namespace sha256
	{

		const red::Uint32 Hasher::st_table[64] = {
			0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
			0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
			0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
			0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
			0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
			0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
			0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
			0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
			0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
			0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
			0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
			0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
			0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
			0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
			0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
			0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
		};

		Hasher::Hasher()
		{
			m_h[0] = 0x6a09e667;
			m_h[1] = 0xbb67ae85;
			m_h[2] = 0x3c6ef372;
			m_h[3] = 0xa54ff53a;
			m_h[4] = 0x510e527f;
			m_h[5] = 0x9b05688c;
			m_h[6] = 0x1f83d9ab;
			m_h[7] = 0x5be0cd19;
			m_pos = 0;
			m_total = 0;
		}

		void Hasher::Update( const red::Uint8* message, const red::Uint64 size )
		{
			const red::Uint64 remainingBuffer = SHA224_256_BLOCK_SIZE - m_pos;
			const red::Uint64 remainingLength = size < remainingBuffer ? size : remainingBuffer;

			red::Memcpy( &m_block[m_pos], message, remainingLength );
			if ( m_pos + size < SHA224_256_BLOCK_SIZE )
			{
				m_pos += size;
				return;
			}

			const red::Uint64 extraLength = size - remainingLength;
			const red::Uint64 blockCount = extraLength / SHA224_256_BLOCK_SIZE;
			const red::Uint8* shifted_message = message + remainingLength;
			Transform( m_block, 1 );
			Transform( shifted_message, blockCount );

			const red::Uint32 finalLen = extraLength % SHA224_256_BLOCK_SIZE;
			red::Memcpy( m_block, &shifted_message[blockCount << 6], finalLen );

			m_pos = finalLen;
			m_total += (blockCount + 1) << 6;
		}

		void Hasher::Final( red::Uint8* digest )
		{
			const red::Uint32 block_nb = (1 + ((SHA224_256_BLOCK_SIZE - 9) < (m_pos % SHA224_256_BLOCK_SIZE)));
			const red::Uint64 len_b = (m_total + m_pos) << 3;
			const red::Uint32 pm_len = block_nb << 6;
			red::Memzero( m_block + m_pos, pm_len - m_pos );

			m_block[ m_pos ] = 0x80;
			SHA2_UNPACK32(len_b, m_block + pm_len - 4);

			Transform( m_block, block_nb );
			for ( int i=0 ; i<8; i++ )
			{
				SHA2_UNPACK32(m_h[i], &digest[i << 2]);
			}
		}

		void Hasher::Transform( const red::Uint8* message, const red::Uint64 blockCount )
		{
			red::Uint32 w[64];
			red::Uint32 wv[8];
			red::Uint32 t1, t2;

			for ( red::Uint64 i=0; i<blockCount; ++i )
			{
				const auto* sub_block = message + (i << 6);

				for ( int j=0; j<16; j++ )
				{
					SHA2_PACK32( &sub_block[j << 2], &w[j] );
				}

				for ( int  j = 16; j < 64; j++)
				{
					w[j] =  SHA256_F4(w[j -  2]) + w[j -  7] + SHA256_F3(w[j - 15]) + w[j - 16];
				}

				for ( int j = 0; j < 8; j++ )
					wv[j] = m_h[j];

				for ( int j = 0; j < 64; j++ )
				{
					t1 = wv[7] + SHA256_F2(wv[4]) + SHA2_CH(wv[4], wv[5], wv[6]) + st_table[j] + w[j];
					t2 = SHA256_F1(wv[0]) + SHA2_MAJ(wv[0], wv[1], wv[2]);
					wv[7] = wv[6];
					wv[6] = wv[5];
					wv[5] = wv[4];
					wv[4] = wv[3] + t1;
					wv[3] = wv[2];
					wv[2] = wv[1];
					wv[1] = wv[0];
					wv[0] = t1 + t2;
				}

				for ( int j = 0; j < 8; j++ )
					m_h[j] += wv[j];
			}
		}

	} // sha256

} // crypto