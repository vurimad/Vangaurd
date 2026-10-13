/**
* Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "poolMetrics.h"

namespace red
{
namespace memory
{
namespace
{
	void UpdateMax( atomic::TAtomic64& currentMax, const atomic::TAtomic64& value )
	{
		while ( 1 )
		{
			atomic::TAtomic64 prevMax = currentMax;
			if ( prevMax < value )
			{
				if ( atomic::Exchange64( &currentMax, value ) == prevMax )
				{
					break;
				}
			}
			else
			{
				break;
			}
		}
	}
}

	void UpdatePoolAllocateMetrics( PoolMetrics & metrics, u64 size )
	{
		atomic::ExchangeAdd64( &metrics.bytesAllocated, size );

#ifdef RED_MEMORY_ENABLE_EXTENDED_METRICS
		
		atomic::ExchangeAdd64( &metrics.bytesAllocatedPerFrame, size );
		atomic::Increment32( &metrics.allocationCount );
		atomic::Increment32( &metrics.allocationPerFrame );

		UpdateMax( metrics.bytesAllocatedPeak, metrics.bytesAllocated );

		/* MSVC debug iterator will make this code break. Those metrics are not very useful so I'll drop them for now.
		metrics.bytesAllocatedPeak = std::max( metrics.bytesAllocatedPeak, metrics.bytesAllocated );
		metrics.bytesAllocatedPerFramePeak = std::max( metrics.bytesAllocatedPerFramePeak, metrics.bytesAllocatedPerFrame );
		metrics.allocationCountPeak = std::max( metrics.allocationCountPeak, metrics.allocationCount );
		metrics.allocationPerFramePeak = std::max( metrics.allocationPerFramePeak, metrics.allocationPerFrame );
		*/
#endif
	}

	void UpdatePoolDeallocateMetrics( PoolMetrics  & metrics, u64 size )
	{
		atomic::ExchangeAdd64( &metrics.bytesAllocated, -static_cast< i64 >( size ) );

#ifdef RED_MEMORY_ENABLE_EXTENDED_METRICS	
		
		atomic::ExchangeAdd64( &metrics.bytesDeallocatedPerFrame, size );
		atomic::Decrement32( &metrics.allocationCount );
		atomic::Increment32( &metrics.deallocationPerFrame );

		/* MSVC debug iterator will make this code break. Those metrics are not very useful so I'll drop them for now.
		metrics.bytesDeallocatedPerFramePeak = std::max( metrics.bytesDeallocatedPerFramePeak, metrics.bytesDeallocatedPerFrame );
		metrics.deallocationPerFramePeak = std::max( metrics.deallocationPerFramePeak, metrics.deallocationPerFrame );
		*/
#endif
	}

	void UpdatePoolReallocateMetrics( PoolMetrics  & metrics, u64 inputSize, u64 outputSize )
	{		
#ifdef RED_MEMORY_ENABLE_EXTENDED_METRICS	
		if( inputSize )
		{
			UpdatePoolDeallocateMetrics( metrics, inputSize );
		}

		if( outputSize )
		{
			UpdatePoolAllocateMetrics( metrics, outputSize );
		}
#else
		const i64 size = outputSize - inputSize;
		atomic::ExchangeAdd64( &metrics.bytesAllocated, size );
#endif
	}

	void ResetPoolMetrics( PoolMetrics& metrics )
	{
		atomic::Exchange64( &metrics.bytesAllocated, 0 );

#ifdef RED_MEMORY_ENABLE_EXTENDED_METRICS
		atomic::Exchange64( &metrics.bytesAllocatedPerFrame, 0 );
		atomic::Exchange64( &metrics.bytesDeallocatedPerFrame, 0 );
		atomic::Exchange32( &metrics.allocationCount, 0 );
		atomic::Exchange32( &metrics.allocationPerFrame, 0 );
#endif
	}

	void PreparePoolMetricsForNextFrame( PoolMetrics& metrics )
	{
#ifdef RED_MEMORY_ENABLE_EXTENDED_METRICS
		atomic::Exchange64( &metrics.bytesAllocatedPerFramePrevious, metrics.bytesAllocatedPerFrame );
		atomic::Exchange64( &metrics.bytesDeallocatedPerFramePrevious, metrics.bytesDeallocatedPerFrame );
		atomic::Exchange32( &metrics.allocationPerFramePrevious, metrics.allocationPerFrame );

		atomic::Exchange64( &metrics.bytesAllocatedPerFrame, 0 );
		atomic::Exchange64( &metrics.bytesDeallocatedPerFrame, 0 );
		atomic::Exchange32( &metrics.allocationPerFrame, 0 );
#else
		RED_UNUSED( metrics );
#endif
	}
}
}
