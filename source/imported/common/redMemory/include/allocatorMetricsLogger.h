/**
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_MEMORY_ALLOCATOR_METRICS_LOGGER_H_
#define _RED_MEMORY_ALLOCATOR_METRICS_LOGGER_H_

#include "proxyTypeId.h"

namespace red
{
namespace memory
{
	class Deserializer;

	RED_MEMORY_API const char* GetAllocatorName( ProxyTypeId proxyId );

	void LogAllocatorMetrics( ProxyTypeId proxyId, void* proxy );

	// Metrics logger used for all core allocators
	RED_MEMORY_API void CoreAllocatorsMetricsLogger( ProxyTypeId proxyId, Deserializer & deserializer );
	RED_MEMORY_API void CoreAllocatorsMetricsJsonWriter( ProxyTypeId proxyId, Deserializer & deserializer );
}
}

#endif 