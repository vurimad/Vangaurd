/**
* Copyright (c) 2018 CDProjekt Red, Inc. All Rights Reserved.
*/

#pragma once

#include "../../redCore/include/redCorePublic.h"
#include "../../redContainers/include/redContainersPublic.h"

#include "redJobs2Api.h"
#include "jobMemoryPools.h"

#if defined( RED_PLATFORM_ORBIS ) || defined( RED_PLATFORM_DURANGO )
	#define RED_CONSOLE_CORE_7_SUPPORT
#endif

#if defined(RED_PLATFORM_DURANGO)
	// Xbox specific optimisation
	// ctremblay: Unfortunaly, we get freeze on XSX. We need to investigate further
	//#define USE_RESOURCE_THROTTLER_THREADS
#endif
