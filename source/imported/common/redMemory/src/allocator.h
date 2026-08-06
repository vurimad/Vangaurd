/**
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_MEMORY_ALLOCATOR_H_
#define _RED_MEMORY_ALLOCATOR_H_

#include "../include/proxyTypeId.h"
#include "allocatorMetrics.h"
#include "proxy.h"

#define RED_MEMORY_DECLARE_ALLOCATOR_NAME( name ) RED_MEMORY_INLINE static const char* GetName() { static const char* _allocatorName = #name; return _allocatorName; }

#define RED_MEMORY_DECLARE_ALLOCATOR( name, metricsName, defaultAlignment )	\
			RED_MEMORY_DECLARE_METRICS_ALIAS( metricsName )					\
			RED_MEMORY_DECLARE_PROXY( name, defaultAlignment );				\
			RED_MEMORY_DECLARE_ALLOCATOR_NAME( name )						\
			RED_MEMORY_PROXY_TYPE_ID( name )

#endif
