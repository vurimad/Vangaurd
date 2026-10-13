/**
* Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "sha256.h"
#include "sha256prv.h"

namespace crypto
{

	void CalculateSha256( const void* buffer, const red::Uint64 size, Ssha256& outHash )
	{
		sha256::Hasher hasher;

		hasher.Update( (const red::Uint8*)buffer, size );
		hasher.Final( (red::Uint8*) &outHash );
	}

} // crypto

