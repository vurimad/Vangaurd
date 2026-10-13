/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "jsonAllocator.h"
#include "../../redMemory/include/poolUtils.h"

namespace res
{
namespace json
{
	void InitializeJsonMemoryPool()
	{
		RED_INITIALIZE_MEMORY_POOL( PoolJSON, red::PoolResource, red::memory::AcquireDefaultAllocator(), RED_MEGA_BYTE( 2 ) );
	}
}
}
