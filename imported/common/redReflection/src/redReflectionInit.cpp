/**
* Copyright (c)2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

// this file is auto generated and contains registration function for rtti types in this project
#include "../rtti/redReflection/typeRegistryFuncImpl.h"
#include "reflectionPool.h"
#include "mathCommon.h"
#include "absoluteFilepath.h"

RED_MODULE(redReflection)
{
	RegisterRedReflectionRTTI();

	extern void RegisterCoreScriptFunctions();
	RegisterCoreScriptFunctions();

	RegisterMathTypeAliases();

	extern void RegisterSingleChannelCurveTypeCreator();
	RegisterSingleChannelCurveTypeCreator();

	extern void RegisterMultiChannelCurveTypeCreator();
	RegisterMultiChannelCurveTypeCreator();

	RegisterFileTypeAliases();

	red::InitializeReflectionMemoryPools();
	red::InitializeContainerMemoryPools();
}
