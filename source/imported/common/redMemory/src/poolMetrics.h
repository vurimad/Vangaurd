/**
* Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
*/

#ifndef _RED_MEMORY_POOL_METRICS_H_
#define _RED_MEMORY_POOL_METRICS_H_

#include "../../redMemory/include/poolTypes.h"
#include "../../redSystem/include/redThreadsAtomic.h"

namespace red
{
namespace memory
{
	struct PoolMetrics
	{
		PoolHandle handle;

		atomic::TAtomic64 bytesAllocated;

#ifdef RED_MEMORY_ENABLE_EXTENDED_METRICS
		atomic::TAtomic64 bytesAllocatedPeak;

		atomic::TAtomic64 bytesAllocatedPerFrame;
		atomic::TAtomic64 bytesAllocatedPerFramePrevious;
		atomic::TAtomic64 bytesAllocatedPerFramePeak;

		atomic::TAtomic64 bytesDeallocatedPerFrame;
		atomic::TAtomic64 bytesDeallocatedPerFramePrevious;
		atomic::TAtomic64 bytesDeallocatedPerFramePeak;

		atomic::TAtomic32 allocationCount;
		atomic::TAtomic32 allocationCountPeak;

		atomic::TAtomic32 allocationPerFrame;
		atomic::TAtomic32 allocationPerFramePrevious;
		atomic::TAtomic32 allocationPerFramePeak;

		atomic::TAtomic32 deallocationPerFrame;
		atomic::TAtomic32 deallocationPerFramePeak;
#endif
	};

	void UpdatePoolAllocateMetrics( PoolMetrics & metrics, u64 size );
	void UpdatePoolDeallocateMetrics( PoolMetrics & metrics, u64 size );
	void UpdatePoolReallocateMetrics( PoolMetrics & metrics, u64 inputSize, u64 ouputSize );
	void ResetPoolMetrics( PoolMetrics& metrics );
	void PreparePoolMetricsForNextFrame( PoolMetrics& metrics );
}
}

#endif
