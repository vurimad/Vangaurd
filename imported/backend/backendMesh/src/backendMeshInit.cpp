/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

// this file is auto generated and contains registration function for rtti types in this project
#include "../rtti/backendMesh/typeRegistryFuncImpl.h"
#include "meshOptimizer.h"

RED_MODULE(backendMesh)
{
#ifdef RED_PLATFORM_WINPC
	RegisterBackendMeshRTTI();
#endif

	mesh::InitializeMeshOptimizerLibrary();
}
