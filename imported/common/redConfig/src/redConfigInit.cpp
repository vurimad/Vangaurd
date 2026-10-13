/**
* Copyright (c)2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "redConfigPool.h"

RED_MODULE( redConfig )
{
	InGameConfig::InitializeMemoryPools();
}