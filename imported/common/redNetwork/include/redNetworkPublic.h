/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../redSystem/include/redSystemPublic.h"
#include "../../redContainers/include/redContainersPublic.h"
#include "../../redMemory/include/redMemoryApi.h"

#include "redNetworkApi.h"

#if defined( RED_PLATFORM_WIN32 ) || defined( RED_PLATFORM_WIN64 ) || defined( RED_PLATFORM_DURANGO )
#	include "platformWindows.h"
#elif defined( RED_PLATFORM_ORBIS )
#	include "platformOrbis.h"
#elif defined( RED_PLATFORM_LINUX )
#	include "platformLinux.h"
#else
#	error No red network implementation for current platform
#endif
