/**
* Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
*/
#pragma once

namespace crypto
{
	namespace sha256
	{

		class Hasher
		{
		public:
			static const red::Uint32 SHA224_256_BLOCK_SIZE = (512 / 8);
			static const red::Uint32 DIGEST_SIZE = (256 / 8);

			Hasher();

			void Update( const red::Uint8* message, const red::Uint64 size );
			void Final( red::Uint8* digest );

		protected:
			void Transform( const red::Uint8* message, const red::Uint64 blockCount );

			static const red::Uint32 st_table[64];

			red::Uint64 m_total;
			red::Uint64 m_pos; // 64 bit just because the generated ASM is a little bit faster

			red::Uint8 m_block[ 2*SHA224_256_BLOCK_SIZE ];
			red::Uint32 m_h[8];
		};


	} // sha256

} // crypto
