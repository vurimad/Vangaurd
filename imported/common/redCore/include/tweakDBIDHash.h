/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

namespace game 
{
	namespace data
	{
		namespace prv
		{
			static constexpr Uint32 crc32( Uint32 crc, const char *buf, size_t len );

			static constexpr Uint32 crc32_combine( Uint32 crc1, Uint32 crc2, Int64 len2 );
		}
	}
}

#include "tweakDBIDHash.hpp"
