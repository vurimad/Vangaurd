/**
 * Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "../include/metricsUtils.h"
#include "metricsUtils.h"
#include "vault.h"

namespace red
{
namespace memory
{
	void AddAllocateMetric( PoolHandle handle, const Block & block )
	{
		AcquireVault().AddAllocateMetric( handle, block );
	}

	void AddFreeMetric( PoolHandle handle,const Block & block )
	{
		AcquireVault().AddFreeMetric( handle, block );
	}
	
	void AddReallocateMetric( PoolHandle handle,const Block & input, const Block & output )
	{
		AcquireVault().AddReallocateMetric( handle, input, output );
	}

	u64 GetTotalBytesAllocated()
	{
		return AcquireVault().GetTotalBytesAllocated();
	}

	u32 GetTotalAllocationCount()
	{
		return AcquireVault().GetTotalAllocationCount();
	}

	void StartMemoryCapture( u32 bufferSize, OutOfProfilerMemoryCallback outOfProfilerMemoryCallback )
	{
		AcquireVault().StartMemoryCapture( bufferSize, std::move( outOfProfilerMemoryCallback ) );
	}

	void StartMemoryCapture( const char * filename )
	{
		AcquireVault().StartMemoryCapture( filename );
	}

	void StopMemoryCapture()
	{
		AcquireVault().StopMemoryCapture();
	}

	void WriteCurrentFrameTick()
	{
		AcquireVault().WriteCurrentFrameTick();
	}

	void ResetMemoryCapture()
	{
		AcquireVault().ResetMemoryCapture();
	}

	red::memory::Stream& GetMemoryCaptureStream()
	{
		return AcquireVault().GetMemoryCaptureStream();
	}

	i64 GetTotalBytesAllocated( PoolHandle handle, Bool includeChildren )
	{
		return AcquireVault().GetTotalBytesAllocated( handle, includeChildren );
	}

	void GetRuntimePoolMetrics( PoolHandle handle, RuntimePoolMetrics & poolMetrics )
	{
		AcquireVault().GetRuntimePoolMetrics( handle, poolMetrics );
	}

	void PrepareMetricsForNextFrame()
	{
		AcquireVault().PrepareMetricsForNextFrame();
	}

	void ResetMetrics( PoolHandle handle )
	{
		AcquireVault().ResetMetrics( handle );
	}

	void GetRuntimeMetrics( RuntimeMetrics & metrics )
	{
		const SystemAllocator & systemAllocator = AcquireVault().GetSystemAllocator(); 
		metrics.totalPhysicalMemory = systemAllocator.GetTotalPhysicalMemoryAvailable();
		metrics.availablePhysicalMemory = systemAllocator.GetCurrentPhysicalMemoryAvailable();
		metrics.totalBytesAllocated = GetTotalBytesAllocated();
		metrics.totalAllocationCount = GetTotalAllocationCount();

		metrics.cpuBytesAllocated = GetTotalBytesAllocated< PoolCPU >();
		metrics.cpuAllocationCount = 0; // TODO

		metrics.gpuBytesAllocated = GetTotalBytesAllocated< PoolGPU >();

		static constexpr PoolHandle s_poolGPUMirrorHandle = red::CalculateAnsiHash32( "PoolGPUMirror" );
		if ( IsPoolRegistered( s_poolGPUMirrorHandle ) )
		{
			if ( AcquireVault().GetContributeToParentMetrics( s_poolGPUMirrorHandle ) )
			{
				metrics.gpuBytesAllocated += GetTotalBytesAllocated( s_poolGPUMirrorHandle );
			}
		}

		metrics.gpuAllocationCount = 0; // TODO

		metrics.cpuPoolBudget = GetPoolBudget< PoolCPU >();
		metrics.gpuPoolBudget = GetPoolBudget< PoolGPU >();
	}

	void SerializeAllocatorMetrics( Serializer & serializer, Deserializer & deserializer, const char * poolName )
	{
		AcquireVault().SerializeAllocatorMetrics( serializer, deserializer, poolName );
	}

	void SerializePoolMetrics( Serializer & serializer )
	{
		AcquireVault().SerializePoolMetrics( serializer );
	}
}
}
