/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include "dataTypes.h"

// this file is auto generated and contains registration function for rtti types in this project
#include "../rtti/backendData/typeRegistryFuncImpl.h"

RED_MODULE( backendData )
{
#ifdef RED_PLATFORM_WINPC
	RegisterBackendDataRTTI();
#endif

	RED_INITIALIZE_MEMORY_POOL( data::PoolDataSystem, red::PoolBackend, red::memory::AcquireDefaultAllocator(), 0 );
}
