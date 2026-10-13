/**
* Copyright (c)2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "fileSyncService.h"

void InitializeFileSyncMemoryPools()
{
#if defined( RED_FILE_SYNC_SERVICE_ENABLED )
	RED_INITIALIZE_MEMORY_POOL( PoolFileSyncService, red::PoolDefault, red::memory::AcquireDefaultAllocator(), RED_MEGA_BYTE( 10 ) );
#endif
}