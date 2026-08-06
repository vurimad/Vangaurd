/**
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_MEMORY_INCLUDE_METRICS_UTILS_H_
#define _RED_MEMORY_INCLUDE_METRICS_UTILS_H_

#include "redMemoryApi.h"
#include "redMemoryInternal.h"

namespace red
{
namespace memory
{
	class Serializer;
	class Deserializer;
	class Stream;

	// Return bytes allocated for all pools.
	RED_MEMORY_API u64 GetTotalBytesAllocated();

	// Return number of allocations for all pools.
	RED_MEMORY_API u32 GetTotalAllocationCount();

	// Return bytes allocated from provided pool and all child pools.
	template< typename PoolType >
	u64 GetTotalBytesAllocated();

	struct RuntimeMetrics
	{
		u64 totalPhysicalMemory;
		u64 availablePhysicalMemory;

		u64 totalBytesAllocated;
		u64 cpuBytesAllocated;
		u64 gpuBytesAllocated;

		u64 cpuPoolBudget;
		u64 gpuPoolBudget;

		u32 totalAllocationCount;
		u32 cpuAllocationCount;
		u32 gpuAllocationCount;
	};

	RED_MEMORY_API void GetRuntimeMetrics( RuntimeMetrics & metrics );

	using OutOfProfilerMemoryCallback = red::FixedSizeFunction< void() >;
	RED_MEMORY_API void StartMemoryCapture( u32 bufferSize, OutOfProfilerMemoryCallback outOfProfilerMemoryCallback = []{} );
	RED_MEMORY_API void StartMemoryCapture( const char * filename );
	RED_MEMORY_API void StopMemoryCapture();
	RED_MEMORY_API void WriteCurrentFrameTick();
	RED_MEMORY_API void ResetMemoryCapture();
	RED_MEMORY_API red::memory::Stream& GetMemoryCaptureStream();

	RED_MEMORY_API void SerializeAllocatorMetrics( Serializer & serializer, Deserializer & deserializer, const char * poolName );
	RED_MEMORY_API void SerializePoolMetrics( Serializer & serializer );
}
}

#include "metricsUtils.hpp"

#endif
