/*
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include "../../redMemory/include/poolUtils.h"

#include "redIOMemory.h"

namespace io
{
	void InitializeMemoryPools()
	{
		RED_INITIALIZE_MEMORY_POOL( PoolAsyncIO, red::PoolEngine, red::memory::AcquireDefaultAllocator(), RED_MEGA_BYTE( 1 ) );
	}
}

