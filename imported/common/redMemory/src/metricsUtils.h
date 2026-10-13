/**
 * Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_MEMORY_METRICS_UTILS_H_
#define _RED_MEMORY_METRICS_UTILS_H_

#include "../include/poolTypes.h"
#include "../../redSystem/include/hash.h"

namespace red
{
namespace memory
{
	struct Block;

	struct RED_MEMORY_API RuntimePoolMetrics
	{
		i64 bytesAllocated;
		i64 bytesAllocatedPeak;
		i64 bytesAllocatedPerFrame;
		i64 bytesAllocatedPerFramePrevious;
		i64 bytesDeallocatedPerFrame;
		i64 bytesDeallocatedPerFramePrevious;
		i32 allocationCount;
		i32 allocationPerFrameCount;
		i32 allocationPerFrameCountPrevious;
	};

	RED_MEMORY_API void AddAllocateMetric( PoolHandle handle, const Block & block );
	RED_MEMORY_API void AddFreeMetric( PoolHandle handle,const Block & block );
	RED_MEMORY_API void AddReallocateMetric( PoolHandle handle,const Block & input, const Block & output );

	RED_MEMORY_API i64 GetTotalBytesAllocated( PoolHandle handle, Bool includeChildren = true );
	RED_MEMORY_API u64 GetTotalBytesAllocated();

	RED_MEMORY_API void GetRuntimePoolMetrics( PoolHandle handle, RuntimePoolMetrics & poolMetrics );

	RED_MEMORY_API void PrepareMetricsForNextFrame();

	RED_MEMORY_API void ResetMetrics( PoolHandle handle );

	RED_MEMORY_API void WriteCurrentFrameTick();
}
}

#endif
