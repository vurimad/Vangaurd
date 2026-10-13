/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#define _CRT_SECURE_NO_WARNINGS

#if defined( VANGUARD_ASSET_HOST )
#include <functional>
#include <new>
#include <type_traits>
#include <utility>
#endif

#ifndef RED_CONTAINER_ALLOW_DEFAULT_POOL
#define RED_CONTAINER_ALLOW_DEFAULT_POOL
#endif


#include "../../../common/redMemory/include/redMemoryPublic.h"
#include "../../../common/resource/include/resourcePublic.h"
#if defined( VANGUARD_ASSET_HOST )
#include "../include/backendEngineApi.h"
#include "../../../common/core/include/coreApi.h"
#include "../../../common/redImage/include/redImageApi.h"
#endif

#if defined( RED_PLATFORM_WINPC ) && !defined( VANGUARD_ASSET_HOST )
#include "backendEngineInternal.h"
#endif

