/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/

// this makes sure this header is NOT included directly from outside core
#ifndef RED_MODULE_core
	#error "Do not include precompile headers directly, please use corePublic.h"
#endif

#include "coreApi.h"
#if defined( VANGUARD_ASSET_HOST )
#include <new>
#include <type_traits>
#include <utility>
#include "../../redSystem/include/redSystemPublic.h"
#include "../../redMemory/include/redMemoryPublic.h"
#include "../../redContainers/include/redContainersPublic.h"
#include "../../redCore/include/redCoreApi.h"
#include "../../redCore/include/corePool.h"
#include "../../redReflection/include/redReflectionPublic.h"
#else
#include "coreInternal.h"
#include "../../resource/include/resourcePublic.h"
#endif
