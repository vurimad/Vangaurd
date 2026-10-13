/*
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"

#include "../include/jsonAllocator.h"

 // this file is auto generated and contains registration function for rtti types in this project
#include "../rtti/resource/typeRegistryFuncImpl.h"

RED_MODULE(resource)
{
	RegisterResourceRTTI();

	extern void Register2DArrayLoader();
	extern void Register2DArraySaver();
	Register2DArrayLoader();
	Register2DArraySaver();

	extern void RegisterJsonResourceLoader();
	RegisterJsonResourceLoader();
	// Not Implemented
	// NOTE: each Json based serialiser implements their own saver
	//extern void RegisterJsonResourceSaver();
	//RegisterJsonResourceSaver();

	extern void RegisterXmlResourceLoader();
	RegisterXmlResourceLoader();
	// Not Implemented
	//extern void RegisterXmlResourceSaver();
	//RegisterXmlResourceSaver();

	res::json::InitializeJsonMemoryPool();
}
