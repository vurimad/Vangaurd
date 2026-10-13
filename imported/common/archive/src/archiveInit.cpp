/**
* Copyright (c) 2007-2016 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include "../../../common/redSystem/include/module.h"

#include "common.h"

RED_MODULE(archive)
{
	RED_INITIALIZE_MEMORY_POOL( archive::PoolArchive, red::PoolEngine, red::memory::AcquireDefaultAllocator(), 0 );
}
