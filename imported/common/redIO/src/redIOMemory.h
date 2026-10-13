/*
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

namespace io
{

RED_MEMORY_POOL_STATIC( PoolAsyncIO, red::memory::DefaultAllocator );

void InitializeMemoryPools();

}
