/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

// this file is auto generated and contains registration function for rtti types in this project
#include "../rtti/backendResources/typeRegistryFuncImpl.h"

RED_MODULE(backendResources)
{
#ifdef RED_PLATFORM_WINPC
	RegisterBackendResourcesRTTI();
#endif
}
