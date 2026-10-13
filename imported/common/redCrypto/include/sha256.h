/**
* Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
*/
#pragma once

namespace crypto
{
	struct Ssha256
	{
		static constexpr Uint32 NUM_BYTES = 32;
		red::Uint8 m_value[ NUM_BYTES ];
	};

	// calcualte SHA256 hash from a given content
	extern REDCRYPTO_API void CalculateSha256( const void* buffer, const red::Uint64 size, Ssha256& outHash );

} // crypto