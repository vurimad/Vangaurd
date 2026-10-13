/**
* Copyright (C)2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

// this file is auto generated and contains registration function for rtti types in this project
#include "../rtti/resourceMaterial/typeRegistryFuncImpl.h"

extern void RegisterMultilayerResourceLoaders();
extern void RegisterMultilayerResourceSavers();

RED_MODULE(resourceMaterial)
{
	RegisterResourceMaterialRTTI();

	RegisterMultilayerResourceLoaders();
	RegisterMultilayerResourceSavers();
}
