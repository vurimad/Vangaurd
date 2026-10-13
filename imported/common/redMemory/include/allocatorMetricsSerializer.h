/**
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_MEMORY_ALLOCATOR_METRICS_SERIALIZER_H_
#define _RED_MEMORY_ALLOCATOR_METRICS_SERIALIZER_H_

namespace red
{
namespace memory
{
	class Serializer;
	class Deserializer;

	template< typename AllocatorType >
	void SerializeAllocatorMetrics( void * allocator, Serializer & serializer );
}
}

#include "allocatorMetricsSerializer.hpp"

#endif