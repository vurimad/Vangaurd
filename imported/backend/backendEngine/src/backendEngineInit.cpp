/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "backendMemory.h"

// this file is auto generated and contains registration function for rtti types in this project
#include "../rtti/backendEngine/typeRegistryFuncImpl.h"

RED_MODULE( backendEngine )
{
#ifdef RED_PLATFORM_WINPC
	RegisterBackendEngineRTTI();
	tools::InitializeMemoryPools();
#endif
}
