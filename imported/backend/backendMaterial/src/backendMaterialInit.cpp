/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

// this file is auto generated and contains registration function for rtti types in this project
#include "../rtti/backendMaterial/typeRegistryFuncImpl.h"

RED_MODULE( backendMaterial )
{
#ifdef RED_PLATFORM_WINPC
	RegisterBackendMaterialRTTI();
#endif
}
