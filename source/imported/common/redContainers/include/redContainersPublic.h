/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/
#pragma once


#ifdef RED_CONTAINER_ALLOW_DEFAULT_POOL
#define RED_CONTAINER_DEFAULT_VALUE = red::PoolDefault()
#else
#define RED_CONTAINER_DEFAULT_VALUE
#endif


// red-system dependencies
#include "../../redSystem/include/redSystemPublic.h"

// red-memory dependencies (quite obvious)
#include "../../redMemory/include/redMemoryPublic.h"

//-----------------------------------------------------------------

#include "redContainersApi.h"

// exported stuff
#include "containersCommon.h"
#include "algorithms.h"
#include "policies.h"
#include "pool.h"
#include "hash.h"
#include "string/string.h"
#include "string/stringView.h"
#include "pair.h"
#include "hashSet.h"
#include "hashMap.h"
#include "arraySpan.h"
#include "dynArray.h"
#include "sortedArray.h"
#include "staticArray.h"
#include "bitSet.h"
#include "bitSetDynamic.h"
#include "set.h"
#include "map.h"
#include "idAllocator.h"
#include "containersTypesFormatters.h"
