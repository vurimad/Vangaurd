/**
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/
#pragma once

namespace crypto
{
	struct MD5
	{
		template <class OutputIterator>
		static void Uint32ToByte( Uint32 n, OutputIterator& first )
		{

			*first++ = n & 0xff;
			*first++ = ( n >> 8 ) & 0xff;
			*first++ = ( n >> 16 ) & 0xff;
			*first++ = ( n >> 24 ) & 0xff;
		}

		template <class OutputIterator>
		static void Uint32ToHex( Uint32 n, OutputIterator& first )
		{
			const char* hexChars = "0123456789abcdef";

			Uint32 b;

			b = n & 0xff;
			*first++ = hexChars[b >> 4];
			*first++ = hexChars[b & 0xf];

			b = ( n >> 8 ) & 0xff;
			*first++ = hexChars[b >> 4];
			*first++ = hexChars[b & 0xf];

			b = ( n >> 16 ) & 0xff;
			*first++ = hexChars[b >> 4];
			*first++ = hexChars[b & 0xf];

			b = ( n >> 24 ) & 0xff;
			*first++ = hexChars[b >> 4];
			*first++ = hexChars[b & 0xf];
		}

		template <class Container>
		void Digest( Container& container )
		{
			container.Resize( 16 );
			auto it = container.Begin();

			Uint32ToByte( m_a, it );
			Uint32ToByte( m_b, it );
			Uint32ToByte( m_c, it );
			Uint32ToByte( m_d, it );
		}

		template <class Container>
		void HexDigest( Container& container )
		{
			container.Resize( 32 );
			auto it = container.Begin();

			Uint32ToHex( m_a, it );
			Uint32ToHex( m_b, it );
			Uint32ToHex( m_c, it );
			Uint32ToHex( m_d, it );
		}

		Uint32 m_a = 0x67452301;
		Uint32 m_b = 0xefcdab89;
		Uint32 m_c = 0x98badcfe;
		Uint32 m_d = 0x10325476;
	};

	// calculate MD5 hash from a given content
	// NOTE: **** DO NOT USE FOR ANYTHING REQUIRING REAL SECURITY!!! IT IS INSCECURE BY TODAY'S STANDARDS ***
	extern REDCRYPTO_API void CalculateMD5_Insecure( const void* buffer, const red::Uint64 size, MD5& outHash );

} // crypto