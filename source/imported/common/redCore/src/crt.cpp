/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/

#include "build.h"
#include "crt.h"


Uint64 ACalcBufferHash64Merge( const void* buffer, size_t sizeInBytes, Uint64 previousHash )
{
	const Uint8* data = reinterpret_cast<const Uint8*>( buffer );
	Uint64 hash = previousHash;
	while( sizeInBytes-- )
	{
		hash ^= *data++;
		hash *= 0x100000001B3;
	}
	return hash;
}
