/**
* Copyright (C)2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "defaultAllocator.h"
#include "locklessSlabAllocator.h"
#include "gpuAllocator.h"
#include "../include/allocatorMetricsLogger.h"
#include "../include/tlsfAllocator.h"
#include "../include/fixedSizeAllocator.h"
#include "../include/stackAllocator.h"
#include "../include/linearAllocator.h"
#include "../include/unsafeDynamicLinearAllocator.h"
#include "../include/circularAllocator.h"
#include "../include/frameAllocator.h"
#include "../include/bigSizeAllocator.h"
#include "../include/wwiseAllocator.h"
#include "../include/icuAllocator.h"

namespace red
{
namespace memory
{

#ifdef RED_MEMORY_ENABLE_REPORT
	extern FILE* s_oomJsonFile;

namespace
{
	const u32 c_tlsfHistogramWidth = 80;

	void OutputTLFHistogram( const TLSFAllocatorMetrics & tlsfMetrics, u32 maxGraphWidth )
	{
		u64 maxBlockCount = 0;
		u64 maxTotalAllocSize = 0;
		u32 lastIndexToProcess = 0;
		const auto & blockMetrics = tlsfMetrics.freeBlocksMetrics;
		for ( u32 index = 0; index != blockMetrics.Size(); ++index )
		{
			const auto & blockMetric = blockMetrics[ index ];
			maxBlockCount = std::max( maxBlockCount, blockMetric.totalCount );
			maxTotalAllocSize = std::max( maxTotalAllocSize, blockMetric.totalSize );
			lastIndexToProcess = blockMetric.totalSize != 0 ? index : lastIndexToProcess;
		}

		const float totalSizeStep = maxTotalAllocSize / static_cast<float>( maxGraphWidth );
		const float blockCountStep = maxBlockCount / static_cast<float>( maxGraphWidth );

		RED_MEMORY_LOG( "Memory: Free block Histogram. X -> Total Size, * -> Block Count." );

		for ( u32 index = 0; index != lastIndexToProcess + 1; ++index )
		{
			const auto & blockMetric = blockMetrics[ index ];

			if ( blockMetric.totalCount )
			{
				char sizeGraphBlock[ 128 ] = { 0 };
				char countGraphBlock[ 128 ] = { 0 };
				u32 sizeMarkerCount = static_cast< u32 >( blockMetric.totalSize / totalSizeStep );
				u32 countMarkerCount = static_cast< u32 >( blockMetric.totalCount / blockCountStep );

				std::memset( sizeGraphBlock, 'X', sizeMarkerCount );
				std::memset( countGraphBlock, '*', countMarkerCount );

				SNPrintFUnsafe( sizeGraphBlock + sizeMarkerCount, 128 - sizeMarkerCount, " %" PRIu64, blockMetric.totalSize );
				SNPrintFUnsafe( countGraphBlock + countMarkerCount, 128 - countMarkerCount, " %" PRIu64, blockMetric.totalCount );

				RED_MEMORY_LOG( "Memory: <%15" PRIu64 " |%s", blockMetric.blockSize, sizeGraphBlock );
				RED_MEMORY_LOG( "Memory:  %15s |%s", " ", countGraphBlock );
			}
		}
	}

#ifdef RED_MEMORY_ENABLE_EXTENDED_SLAB_ALLOCATOR_REPORT
	void LogLocklessSlabAllocatorWaste( const LocklessSlabAllocatorMetrics & locklessSlabAllocatorMetrics )
	{
		u64 totalFreed = 0;
		u64 totalSystemMemory = 0;

		const auto & threadsMetric = locklessSlabAllocatorMetrics.threadCacheInfo;

		for ( u32 index = 0; index != c_maxThreadCount; ++index )
		{
			const auto & threadMetric = threadsMetric[ index ];
			if ( threadMetric.threadId )
			{
				const auto & localInfo = threadMetric.slabAllocatorInfo;

				const Bool leakDetected = localInfo.freedMemoryBytes > localInfo.freedAfterLastProcessMemoryBytes + RED_MEGA_BYTE( 1 );
				const u64 freed = !leakDetected ? localInfo.freedMemoryBytes : localInfo.freedAfterLastProcessMemoryBytes;


				totalFreed += freed;
				totalSystemMemory += localInfo.metrics.consumedSystemMemoryBytes;
			}
		}

		u64 totalWaste = locklessSlabAllocatorMetrics.waste + totalFreed;
		Double totalWastePercent = ( totalWaste / static_cast< double >( totalSystemMemory ) ) * 100.0;

		RED_MEMORY_LOG( "Memory: \t\tMemory Freed: %" PRIu64, totalFreed );
		RED_MEMORY_LOG( "Memory: \t\tMemory Waste: %" PRIu64 " (including freed memory)", totalWaste );
		RED_MEMORY_LOG( "Memory: \t\tMemory Waste Percent: %.2f%%", totalWastePercent );
	}
#endif

	void OutputLocklessSlabAllocatorTable( const LocklessSlabAllocatorMetrics & locklessSlabAllocatorMetrics )
	{
#ifdef RED_MEMORY_ENABLE_EXTENDED_SLAB_ALLOCATOR_REPORT
		RED_MEMORY_LOG( "Memory: =========================================================================================================================================================" );

		RED_MEMORY_LOG_FLUSH();
		const char * headerPattern = "Memory: | %10s | %-32s | %10s | %10s | %10s | %10s | %10s | %10s | %10s | %10s |";
		const char * rowPattern = "Memory: | %10" PRIu32 " | %-32s | %10" PRIu64 " | %10" PRIu64 " | %10" PRIu64 " | %10.2f | %10" PRIu64 " | %10" PRIu64 " | %10" PRIu64 " | %10.2f |";

		RED_MEMORY_LOG( "Memory: Thread Cache Informations" );
		RED_MEMORY_LOG( "Memory: =========================================================================================================================================================" );
		RED_MEMORY_LOG( headerPattern, "Thread ID", "Thread Name", "Used", "Reserved", "Waste", "% Waste", "Freed", "Leaked", "Waste2", "% Waste2" );
		RED_MEMORY_LOG( "Memory: =========================================================================================================================================================" );
#else
		RED_MEMORY_LOG( "Memory: =====================================================================================================" );

		RED_MEMORY_LOG_FLUSH();
		const char * headerPattern = "Memory: | %10s | %-32s | %10s | %10s | %10s | %10s |";
		const char * rowPattern = "Memory: | %10" PRIu32 " | %-32s | %10" PRIu64 " | %10" PRIu64 " | %10" PRIu64 " | %10.2f |";

		RED_MEMORY_LOG( "Memory: Thread Cache Informations" );
		RED_MEMORY_LOG( "Memory: =====================================================================================================" );
		RED_MEMORY_LOG( headerPattern, "Thread ID", "Thread Name", "Used", "Reserved", "Waste", "% Waste" );
		RED_MEMORY_LOG( "Memory: =====================================================================================================" );
#endif

		const auto & threadsMetric = locklessSlabAllocatorMetrics.threadCacheInfo;

		for ( u32 index = 0; index != c_maxThreadCount; ++index )
		{
			const auto & threadMetric = threadsMetric[ index ];
			if ( threadMetric.threadId )
			{
				const auto & localInfo = threadMetric.slabAllocatorInfo;
				const u64 used = localInfo.metrics.consumedMemoryBytes;
				const u64 reserved = localInfo.metrics.consumedSystemMemoryBytes;
				const u64 waste = reserved - used;
				const double percentWaste = reserved ? ( waste / static_cast< double >( reserved ) ) * 100.0 : 0.0;

#ifdef RED_MEMORY_ENABLE_EXTENDED_SLAB_ALLOCATOR_REPORT
				const Bool leakDetected = localInfo.freedMemoryBytes > localInfo.freedAfterLastProcessMemoryBytes + RED_MEGA_BYTE( 1 );
				const u64 freed = !leakDetected ? localInfo.freedMemoryBytes : localInfo.freedAfterLastProcessMemoryBytes;
				const u64 leaked = !leakDetected ? 0 : localInfo.freedMemoryBytes - localInfo.freedAfterLastProcessMemoryBytes;
				const u64 wasteWithFreed = reserved - used + freed;
				const double percentWasteWithFreed = reserved ? ( wasteWithFreed / static_cast< double >( reserved ) ) * 100.0 : 0.0;
#endif

#ifdef RED_MEMORY_ENABLE_EXTENDED_SLAB_ALLOCATOR_REPORT
				RED_MEMORY_LOG(
					rowPattern,
					threadMetric.threadId,
					threadMetric.threadName,
					used,
					reserved,
					waste,
					percentWaste,
					freed,
					leaked,
					wasteWithFreed,
					percentWasteWithFreed );
#else
				RED_MEMORY_LOG(
					rowPattern,
					threadMetric.threadId,
					threadMetric.threadName,
					used,
					reserved,
					waste,
					percentWaste );
#endif
			}
		}

#ifdef RED_MEMORY_ENABLE_EXTENDED_SLAB_ALLOCATOR_REPORT
		RED_MEMORY_LOG( "Memory: =========================================================================================================================================================" );
#else
		RED_MEMORY_LOG( "Memory: =====================================================================================================" );
#endif

		RED_MEMORY_LOG_FLUSH();
	}

	enum BaseAllocatorMetricsFlags : u32
	{
		MetricsFlags_MemoryNotConsumed = RED_FLAGS( 0 ),
		MetricsFlags_FreeBlocks = RED_FLAGS( 1 ),
	};

	double CalculateMemoryConsumedPercent( const AllocatorMetrics& metrics )
	{
		double consumedRatio = 0.0;
		if( metrics.consumedSystemMemoryBytes != 0 )
		{
			consumedRatio = metrics.consumedMemoryBytes / static_cast< double >( metrics.consumedSystemMemoryBytes );
		}
		
		return consumedRatio * 100.0;
	}

	u64 CalculateMemoryNotConsumedBytes( const AllocatorMetrics& metrics )
	{
		return metrics.consumedSystemMemoryBytes - metrics.bookKeepingBytes - metrics.consumedMemoryBytes;
	}

	double CalculateMemoryNotConsumedPercent( const AllocatorMetrics& metrics )
	{
		double notConsumedRatio = 0.0;
		if( metrics.consumedSystemMemoryBytes != 0 )
		{
			notConsumedRatio = CalculateMemoryNotConsumedBytes( metrics ) / static_cast< double >( metrics.consumedSystemMemoryBytes );
		}

		return notConsumedRatio * 100.0;
	}

	double CalculateBookKeepingPercent( const AllocatorMetrics& metrics )
	{
		double bookKeepingRatio = 0.0;
		if( metrics.consumedSystemMemoryBytes != 0 )
		{
			bookKeepingRatio = metrics.bookKeepingBytes / static_cast< double >( metrics.consumedSystemMemoryBytes );
		}

		return bookKeepingRatio * 100.0;
	}

	double CalculateMemoryFragmentationPercent( const AllocatorMetrics& metrics )
	{
		u64 notConsumedBytes = CalculateMemoryNotConsumedBytes( metrics );

		double fragmentationRatio = 0.0;
		if( notConsumedBytes != 0 )
		{
			fragmentationRatio = ( notConsumedBytes - metrics.largestBlockSize )
				/ static_cast< double > ( notConsumedBytes );
		}

		return fragmentationRatio * 100.0;
	}

	void LogBaseAllocatorMetrics( const AllocatorMetrics& metrics, u32 flags = MetricsFlags_MemoryNotConsumed )
	{
		RED_MEMORY_LOG( "Memory: \t\tSystem Memory Consumed: %" PRIu64, metrics.consumedSystemMemoryBytes );
		RED_MEMORY_LOG( "Memory: \t\tMemory Consumed: %" PRIu64 " (%.2f%%)", metrics.consumedMemoryBytes, CalculateMemoryConsumedPercent( metrics ) );

		if( flags & MetricsFlags_MemoryNotConsumed )
		{
			RED_MEMORY_LOG( "Memory: \t\tMemory Not Consumed: %" PRIu64 " (%.2f%%)", CalculateMemoryNotConsumedBytes( metrics ), CalculateMemoryNotConsumedPercent( metrics ) );
		}

		if( metrics.bookKeepingBytes != 0 )
		{
			RED_MEMORY_LOG( "Memory: \t\tBook Keeping Overhead: %" PRIu64 " (%.2f%%)", metrics.bookKeepingBytes, CalculateBookKeepingPercent( metrics ) );
		}

		if( flags & MetricsFlags_FreeBlocks )
		{
			RED_MEMORY_LOG( "Memory: \t\tSmallest Free Block: %" PRIu64, metrics.smallestBlockSize );
			RED_MEMORY_LOG( "Memory: \t\tLargest Free Block: %" PRIu64, metrics.largestBlockSize );
		}
	}

	void LogBaseAllocatorFragmentation( const AllocatorMetrics& metrics )
	{
		RED_MEMORY_LOG( "Memory: \t\tMemory Fragmentation: %.2f%%", CalculateMemoryFragmentationPercent( metrics ) );
	}

	void LogLocklessSlabAllocatorMetrics( const LocklessSlabAllocatorMetrics& metrics )
	{
		LogBaseAllocatorMetrics( metrics.metrics, MetricsFlags_MemoryNotConsumed | MetricsFlags_FreeBlocks );

#ifdef RED_MEMORY_ENABLE_EXTENDED_SLAB_ALLOCATOR_REPORT
		LogLocklessSlabAllocatorWaste( metrics );
#endif

		RED_MEMORY_LOG( "Memory: \t\tUsed Chunks: %" PRIu32, metrics.usedChunkCount );
		RED_MEMORY_LOG( "Memory: \t\tFree Chunks: %" PRIu32, metrics.freeChunkCount );

		RED_MEMORY_LOG( "Memory: +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++" );

		OutputLocklessSlabAllocatorTable( metrics );
	}

	void LogTLSFAllocatorMetrics( const TLSFAllocatorMetrics& metrics )
	{
		LogBaseAllocatorMetrics( metrics.metrics, MetricsFlags_MemoryNotConsumed | MetricsFlags_FreeBlocks );
		LogBaseAllocatorFragmentation( metrics.metrics );

		RED_MEMORY_LOG( "Memory: +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++" );

		OutputTLFHistogram( metrics, c_tlsfHistogramWidth );
	}

	void LogTLSFAllocator( TLSFAllocatorMetrics& metrics )
	{
		RED_MEMORY_LOG( "Memory: \tTLSFAllocatorMetrics Allocator Info" );
		LogTLSFAllocatorMetrics( metrics );
		
		RED_MEMORY_LOG_FLUSH();
	}

	void LogBigSizeAllocatorMetrics( const BigSizeAllocatorMetrics& metrics )
	{
		LogBaseAllocatorMetrics( metrics.metrics, MetricsFlags_FreeBlocks );

		double virtualRemainingRatio = 0.0;
		if( metrics.freeListSize != 0 )
		{
			virtualRemainingRatio = 1.0f - ( metrics.metrics.consumedSystemMemoryBytes
				/ static_cast< double >( metrics.virtualRangeSize ) );
		}

		RED_MEMORY_LOG( "Memory: \t\tVirtual Range Remaining: %" PRIu64" (%.2f%%)",
			metrics.virtualRangeSize - metrics.metrics.consumedSystemMemoryBytes, virtualRemainingRatio * 100.0 );

		double fragmentationRatio = 0.0;
		if( metrics.freeListSize != 0 )
		{
			fragmentationRatio = ( metrics.freeListSize - metrics.metrics.largestBlockSize )
				/ static_cast< double > ( metrics.freeListSize );
		}

		RED_MEMORY_LOG( "Memory: \t\tVirtual Range Fragmentation: %.2f%%", fragmentationRatio * 100.0 );

		RED_MEMORY_LOG( "Memory: \t\tAllocation List Count: %" PRIu32, metrics.allocationListCount );
		RED_MEMORY_LOG( "Memory: \t\tAllocation List Max Count: %" PRIu32, metrics.allocationListMaxCount );
		RED_MEMORY_LOG( "Memory: \t\tFree List Count: %" PRIu32, metrics.freeListCount );
		RED_MEMORY_LOG( "Memory: \t\tFree List Max Count: %" PRIu32, metrics.freeListMaxCount );
	}

	void LogBigSizeAllocator( BigSizeAllocatorMetrics& metrics )
	{
		RED_MEMORY_LOG( "Memory: \tBigSizeAllocatorMetrics Allocator Info" );
		LogBigSizeAllocatorMetrics( metrics );

		RED_MEMORY_LOG_FLUSH();
	}

	void LogDefaultAllocator( DefaultAllocatorMetrics& metrics )
	{
		RED_MEMORY_LOG( "Memory: \tDefault Allocator Info" );
		const AllocatorMetrics & defaultAllocatorMetric = metrics.metrics;
		LogBaseAllocatorMetrics( defaultAllocatorMetric, MetricsFlags_MemoryNotConsumed );

		RED_MEMORY_LOG( "Memory: -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-" );

		const LocklessSlabAllocatorMetrics & locklessSlabAllocatorMetrics = metrics.locklessSlabAllocatorMetrics;
		RED_MEMORY_LOG( "Memory: \tDefault Allocator Lockless SLAB Information:" );
		LogLocklessSlabAllocatorMetrics( locklessSlabAllocatorMetrics );

		RED_MEMORY_LOG( "Memory: -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-" );

		const TLSFAllocatorMetrics & tlsfMetrics = metrics.tlsfAllocatorMetrics;
		RED_MEMORY_LOG( "Memory: \tDefault Allocator TLSF Information:" );
		LogTLSFAllocatorMetrics( tlsfMetrics );

		RED_MEMORY_LOG( "Memory: -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-" );

		const BigSizeAllocatorMetrics & bigSizeAllocatorMetrics = metrics.bigSizeAllocatorMetrics;
		RED_MEMORY_LOG( "Memory: \tDefault Allocator Big Size Allocator Information:" );
		LogBigSizeAllocatorMetrics( bigSizeAllocatorMetrics );

		RED_MEMORY_LOG( "Memory: -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-" );
		RED_MEMORY_LOG( "Memory: ***********************************************************************" );

		RED_MEMORY_LOG_FLUSH();
	}

	void LogFixedSizeAllocator( FixedSizeAllocatorMetrics& metrics )
	{
		RED_MEMORY_LOG( "Memory: \tFixedSizeAllocatorMetrics Allocator Info" );
		LogBaseAllocatorMetrics( metrics.metrics, MetricsFlags_MemoryNotConsumed | MetricsFlags_FreeBlocks );

		RED_MEMORY_LOG_FLUSH();
	}

	void LogStackAllocator( StackAllocatorMetrics& metrics )
	{
		RED_MEMORY_LOG( "Memory: \tStackAllocatorMetrics Allocator Info" );
		LogBaseAllocatorMetrics( metrics.metrics );

		RED_MEMORY_LOG_FLUSH();
	}

	void LogLinearAllocator( LinearAllocatorMetrics& metrics )
	{
		RED_MEMORY_LOG( "Memory: \tLinearAllocatorMetrics Allocator Info" );
		LogBaseAllocatorMetrics( metrics.metrics, MetricsFlags_MemoryNotConsumed | MetricsFlags_FreeBlocks );
		RED_MEMORY_LOG_FLUSH();
	}

	void LogCircularAllocator( CircularAllocatorMetrics& metrics )
	{
		RED_MEMORY_LOG( "Memory: \tCircularAllocatorMetrics Allocator Info" );
		LogBaseAllocatorMetrics( metrics.metrics );

		RED_MEMORY_LOG_FLUSH();
	}

	void LogFrameAllocator( FrameAllocatorMetrics& metrics )
	{
		RED_MEMORY_LOG( "Memory: \tFrameAllocatorMetrics Allocator Info" );
		LogBaseAllocatorMetrics( metrics.metrics );

		RED_MEMORY_LOG( "Memory: \t\tFree Block: %" PRIu64, metrics.freeBlockSize );

		RED_MEMORY_LOG_FLUSH();
	}

	void LogFrameAllocatorWithFallback( FrameAllocatorWithFallbackMetrics& metrics )
	{
		RED_MEMORY_LOG( "Memory: \tFrameAllocatorWithFallbackMetrics Allocator Info" );
		LogBaseAllocatorMetrics( metrics.metrics.metrics );

		RED_MEMORY_LOG( "Memory: \t\tFree Block: %" PRIu64, metrics.metrics.freeBlockSize );
		RED_MEMORY_LOG( "Memory: \t\tOut of budget used bytes: %" PRIu64, metrics.outOfBudgetUsedBytes );

		RED_MEMORY_LOG_FLUSH();
	}

#ifdef RED_PLATFORM_ORBIS
	void LogGpuAllocator( GpuAllocatorMetrics& metrics )
	{
		RED_MEMORY_LOG( "Memory: \tGpu Allocator Info" );

		const AllocatorMetrics & baseAllocatorMetric = metrics.metrics;
		LogBaseAllocatorMetrics( baseAllocatorMetric );

		RED_MEMORY_LOG( "Memory: -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-" );

		const TLSFAllocatorMetrics & tlsfMetrics = metrics.tlsfAllocatorMetrics;

		RED_MEMORY_LOG( "Memory: \tGpu Allocator TLSF Information:" );
		LogTLSFAllocatorMetrics( tlsfMetrics );

		RED_MEMORY_LOG( "Memory: -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-" );

		RED_MEMORY_LOG( "Memory: +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++" );

		const BigSizeAllocatorMetrics & bigSizeAllocatorMetrics = metrics.bigSizeAllocatorMetrics;

		RED_MEMORY_LOG( "Memory: \tGpu Allocator Big Size Allocator Information:" );

		RED_MEMORY_LOG( "Memory: \t\tSystem Memory Consumed: %" PRIu64, bigSizeAllocatorMetrics.metrics.consumedSystemMemoryBytes );
		RED_MEMORY_LOG( "Memory: \t\tMemory Consumed: %" PRIu64, bigSizeAllocatorMetrics.metrics.consumedMemoryBytes );
		RED_MEMORY_LOG( "Memory: \t\tSmallest Free Block: %" PRIu64, bigSizeAllocatorMetrics.metrics.smallestBlockSize );
		RED_MEMORY_LOG( "Memory: \t\tLargest Free Block: %" PRIu64, bigSizeAllocatorMetrics.metrics.largestBlockSize );
		RED_MEMORY_LOG( "Memory: \t\tAllocation List Count: %" PRIu32, bigSizeAllocatorMetrics.allocationListCount );
		RED_MEMORY_LOG( "Memory: \t\tAllocation List Max Count: %" PRIu32, bigSizeAllocatorMetrics.allocationListMaxCount );
		RED_MEMORY_LOG( "Memory: \t\tFree List Count: %" PRIu32, bigSizeAllocatorMetrics.freeListCount );
		RED_MEMORY_LOG( "Memory: \t\tFree List Max Count: %" PRIu32, bigSizeAllocatorMetrics.freeListMaxCount );

		RED_MEMORY_LOG( "Memory: -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-" );

		RED_MEMORY_LOG( "Memory: ***********************************************************************" );

		RED_MEMORY_LOG_FLUSH();
	}
#endif

	void LogWwiseAllocator( WwiseAllocatorMetrics& metrics )
	{
		RED_MEMORY_LOG( "Memory: \tWwiseAllocatorMetrics Allocator Info" );
		LogBaseAllocatorMetrics( metrics.metrics );

		RED_MEMORY_LOG( "Memory: -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-" );

		const TLSFAllocatorMetrics & tlsfMetrics = metrics.tlsfAllocatorMetrics;
		const BigSizeAllocatorMetrics & bigSizeMetrics = metrics.bigSizeAllocatorMetrics;

		RED_MEMORY_LOG( "Memory: \tWwiseAllocator TLSF Information:" );
		LogTLSFAllocatorMetrics( tlsfMetrics );

		RED_MEMORY_LOG( "Memory: \tWwiseAllocator BigSize Information:" );
		LogBigSizeAllocatorMetrics( bigSizeMetrics );

		RED_MEMORY_LOG_FLUSH();
	}

	void WriteBaseAllocatorMetricsToJson( const AllocatorMetrics& metrics, u32 flags = MetricsFlags_MemoryNotConsumed )
	{
		std::fprintf( s_oomJsonFile, "\"system_memory_consumed_kb\":%.2f", metrics.consumedSystemMemoryBytes / 1024.0f );
		std::fprintf( s_oomJsonFile, ",\"memory_consumed_kb\":%.2f", metrics.consumedMemoryBytes / 1024.0f );

		if( flags & MetricsFlags_MemoryNotConsumed )
		{
			std::fprintf( s_oomJsonFile, ",\"memory_not_consumed_kb\":%.2f", CalculateMemoryNotConsumedBytes( metrics ) / 1024.0f );
		}

		if( metrics.bookKeepingBytes != 0 )
		{
			std::fprintf( s_oomJsonFile, ",\"book_keeping_overhead_kb\":%.2f", CalculateMemoryNotConsumedBytes( metrics ) / 1024.0f );
		}

		if( flags & MetricsFlags_FreeBlocks )
		{
			std::fprintf( s_oomJsonFile, ",\"smallest_free_block_bytes\":%" PRIu64, metrics.smallestBlockSize );
			std::fprintf( s_oomJsonFile, ",\"largest_free_block_bytes\":%" PRIu64, metrics.largestBlockSize );
		}
	}

	void LogICUAllocator( ICUAllocatorMetrics& metrics )
	{
		RED_MEMORY_LOG( "Memory: \tICUAllocatorMetrics Allocator Info" );

		RED_MEMORY_LOG( "Memory: \t\tSystem Memory Consumed: %" PRIu64, metrics.metrics.consumedSystemMemoryBytes );
		RED_MEMORY_LOG( "Memory: \t\tMemory Consumed: %" PRIu64, metrics.metrics.consumedMemoryBytes );
		RED_MEMORY_LOG( "Memory: \t\tSmallest Free Block: %" PRIu64, metrics.metrics.smallestBlockSize );
		RED_MEMORY_LOG( "Memory: \t\tLargest Free Block: %" PRIu64, metrics.metrics.largestBlockSize );

		RED_MEMORY_LOG( "Memory: -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-" );

		const TLSFAllocatorMetrics & tlsfMetrics = metrics.tlsfAllocatorMetrics;

		RED_MEMORY_LOG( "Memory: \tICUAllocator TLSF Information:" );

		RED_MEMORY_LOG( "Memory: \t\tSystem Memory Consumed: %" PRIu64, tlsfMetrics.metrics.consumedSystemMemoryBytes );
		RED_MEMORY_LOG( "Memory: \t\tBook Keeping overhead: %" PRIu64, tlsfMetrics.metrics.bookKeepingBytes );
		RED_MEMORY_LOG( "Memory: \t\tSmallest Free Block: %" PRIu64, tlsfMetrics.metrics.smallestBlockSize );
		RED_MEMORY_LOG( "Memory: \t\tLargest Free Block: %" PRIu64, tlsfMetrics.metrics.largestBlockSize );

		RED_MEMORY_LOG( "Memory: +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++" );

		OutputTLFHistogram( tlsfMetrics, c_tlsfHistogramWidth );

		RED_MEMORY_LOG_FLUSH();
	}

	void LogICUAllocatorMetrics( Deserializer & deserializer )
	{
		u32 metricsSize{ 0 };
		deserializer.Deserialize( metricsSize );
		RED_MEMORY_ASSERT( metricsSize == sizeof( ICUAllocatorMetrics ), "Allocator specific metrics is bigger then expected." );

		ICUAllocatorMetrics metrics;
		Memzero( &metrics, sizeof( ICUAllocatorMetrics ) );
		u32 dataRead{ 0 };
		deserializer.Deserialize( &metrics, metricsSize, dataRead );
		RED_MEMORY_ASSERT( dataRead == metricsSize, "Inconsistent memory stream data." );

		LogICUAllocator( metrics );
	}

	void WriteBaseAllocatorFragmentationToJson( const AllocatorMetrics& metrics )
	{
		std::fprintf( s_oomJsonFile, ",\"memory_fragmentation_percent\":%.2f", CalculateMemoryFragmentationPercent( metrics ) );
	}

	void WriteLocklessSlabAllocatorToJson( const LocklessSlabAllocatorMetrics& metrics )
	{
		WriteBaseAllocatorMetricsToJson( metrics.metrics, MetricsFlags_MemoryNotConsumed | MetricsFlags_FreeBlocks );
		std::fprintf( s_oomJsonFile, ",\"used_chunks\":%" PRIu32, metrics.usedChunkCount );
		std::fprintf( s_oomJsonFile, ",\"free_chunks\":%" PRIu32, metrics.freeChunkCount );
	}

	void WriteTLSFAllocatorToJson( const TLSFAllocatorMetrics& metrics )
	{
		WriteBaseAllocatorMetricsToJson( metrics.metrics, MetricsFlags_MemoryNotConsumed | MetricsFlags_FreeBlocks );
		WriteBaseAllocatorFragmentationToJson( metrics.metrics );
	}

	void WriteBigSizeAllocatorToJson( const BigSizeAllocatorMetrics& metrics )
	{
		WriteBaseAllocatorMetricsToJson( metrics.metrics, MetricsFlags_FreeBlocks );
		std::fprintf( s_oomJsonFile, ",\"virtual_range_remaining_kb\":%.2f", ( metrics.virtualRangeSize - metrics.metrics.consumedSystemMemoryBytes ) / 1024.0f );

		double fragmentationRatio = 0.0;
		if( metrics.freeListSize != 0 )
		{
			fragmentationRatio = ( metrics.freeListSize - metrics.metrics.largestBlockSize )
				/ static_cast< double > ( metrics.freeListSize );
		}

		std::fprintf( s_oomJsonFile, ",\"virtual_range_fragmentation_percent\":%.2f", fragmentationRatio * 100.0 );
		std::fprintf( s_oomJsonFile, ",\"allocation_list_count\":%" PRIu32, metrics.allocationListCount );
		std::fprintf( s_oomJsonFile, ",\"allocation_list_max_count\":%" PRIu32, metrics.allocationListMaxCount );
		std::fprintf( s_oomJsonFile, ",\"free_list_count\":%" PRIu32, metrics.freeListCount );
		std::fprintf( s_oomJsonFile, ",\"free_list_max_count\":%" PRIu32, metrics.freeListMaxCount );
	}

#ifdef RED_PLATFORM_ORBIS
	void WriteVirtualGpuAllocatorToJson( const VirtualGPUAllocatorMetrics & metrics )
	{
		std::fprintf( s_oomJsonFile, "\"system_memory_preallocated_kb\":%.2f", metrics.physicalBudgetPreallocated / 1024.0f );
		std::fprintf( s_oomJsonFile, ",\"virtual_space_prereserved_kb\":%.2f", metrics.virtualSpacePrereserved / 1024.0f );
		std::fprintf( s_oomJsonFile, ",\"virtual_slots_free\":%" PRIu32, metrics.virtualSlotsFree );
		std::fprintf( s_oomJsonFile, ",\"physical_memory_free_kb\":%.2f", metrics.physicalMemoryFree / 1024.0f );
		std::fprintf( s_oomJsonFile, ",\"lowest physical_memory_free_kb\":%.2f", metrics.lowestPhysicalFreeEver / 1024.0f );
		std::fprintf( s_oomJsonFile, ",\"physical_pages_free\":%" PRIu32, metrics.physicalPagesFree );
		std::fprintf( s_oomJsonFile, ",\"max_physical_pages_in_single_allocation\":%" PRIu32, metrics.maxPhysicalPagesPerSlot );
		std::fprintf( s_oomJsonFile, ",\"waste_percent\":%.3f", metrics.wastePercent );
	}

	void WriteGpuAllocatorToJson( GpuAllocatorMetrics& metrics )
	{
		const AllocatorMetrics & baseAllocatorMetric = metrics.metrics;
		WriteBaseAllocatorMetricsToJson( baseAllocatorMetric );

		std::fprintf( s_oomJsonFile, ",\"tlsf_allocator\":{" );
		const TLSFAllocatorMetrics & tlsfMetrics = metrics.tlsfAllocatorMetrics;
		WriteTLSFAllocatorToJson( tlsfMetrics );
		std::fprintf( s_oomJsonFile, "}" );

		std::fprintf( s_oomJsonFile, ",\"big_size_allocator\":{" );
		const BigSizeAllocatorMetrics & bigSizefMetrics = metrics.bigSizeAllocatorMetrics;
		WriteBigSizeAllocatorToJson( bigSizefMetrics );
		std::fprintf( s_oomJsonFile, "}" );
	}
#endif

	void WriteDefaultAllocatorToJson( DefaultAllocatorMetrics& metrics )
	{
		const AllocatorMetrics & defaultAllocatorMetric = metrics.metrics;
		WriteBaseAllocatorMetricsToJson( defaultAllocatorMetric, MetricsFlags_MemoryNotConsumed );

		std::fprintf( s_oomJsonFile, ",\"lockless_slab_allocator\":{" );
		const LocklessSlabAllocatorMetrics & locklessSlabAllocatorMetrics = metrics.locklessSlabAllocatorMetrics;
		WriteLocklessSlabAllocatorToJson( locklessSlabAllocatorMetrics );
		std::fprintf( s_oomJsonFile, "}" );

		std::fprintf( s_oomJsonFile, ",\"tlsf_allocator\":{" );
		const TLSFAllocatorMetrics & tlsfMetrics = metrics.tlsfAllocatorMetrics;
		WriteTLSFAllocatorToJson( tlsfMetrics );
		std::fprintf( s_oomJsonFile, "}" );

		std::fprintf( s_oomJsonFile, ",\"big_size_allocator\":{" );
		const BigSizeAllocatorMetrics & bigSizeAllocatorMetrics = metrics.bigSizeAllocatorMetrics;
		WriteBigSizeAllocatorToJson( bigSizeAllocatorMetrics );
		std::fprintf( s_oomJsonFile, "}" );
	}

	void WriteFixedSizeAllocatorToJson( const FixedSizeAllocatorMetrics& metrics )
	{
		WriteBaseAllocatorMetricsToJson( metrics.metrics, MetricsFlags_MemoryNotConsumed | MetricsFlags_FreeBlocks );
	}

	void WriteStackAllocatorToJson( const StackAllocatorMetrics& metrics )
	{
		WriteBaseAllocatorMetricsToJson( metrics.metrics );
	}

	void WriteLinearAllocatorToJson( const LinearAllocatorMetrics& metrics )
	{
		WriteBaseAllocatorMetricsToJson( metrics.metrics, MetricsFlags_MemoryNotConsumed | MetricsFlags_FreeBlocks );
	}

	void WriteCircularAllocatorToJson( const CircularAllocatorMetrics& metrics )
	{
		WriteBaseAllocatorMetricsToJson( metrics.metrics );
	}

	void WriteFrameAllocatorToJson( const FrameAllocatorMetrics& metrics )
	{
		WriteBaseAllocatorMetricsToJson( metrics.metrics );
		std::fprintf( s_oomJsonFile, ",\"free_block_bytes\":%" PRIu64, metrics.freeBlockSize );
	}

	void WriteFrameAllocatorWithFallbackToJson( const FrameAllocatorWithFallbackMetrics& metrics )
	{
		WriteBaseAllocatorMetricsToJson( metrics.metrics.metrics );
		std::fprintf( s_oomJsonFile, ",\"free_block_bytes\":%" PRIu64, metrics.metrics.freeBlockSize );
		std::fprintf( s_oomJsonFile, ",\"out_of_budget_used_kb\":%.2f", metrics.outOfBudgetUsedBytes / 1024.0f );
	}

	void WriteWwiseAllocatorToJson( const WwiseAllocatorMetrics& metrics )
	{
		WriteBaseAllocatorMetricsToJson( metrics.metrics );

		std::fprintf( s_oomJsonFile, ",\"tlsf_allocator\":{" );
		const TLSFAllocatorMetrics & tlsfMetrics = metrics.tlsfAllocatorMetrics;
		WriteTLSFAllocatorToJson( tlsfMetrics );
		std::fprintf( s_oomJsonFile, "}" );
	}

	void DeserializeUnknownAllocatorMetrics( Deserializer & deserializer )
	{
		u32 metricsSize{ 0 };
		deserializer.Deserialize( metricsSize );

		// skip unknown allocator metrics
		deserializer.Seek( metricsSize );

		RED_MEMORY_LOG( "Memory: \tUnknown Allocator Metrics Info. Write custom allocator metrics logger." );
		RED_MEMORY_LOG_FLUSH();
	}

	template< typename MetricsType >
	void DeserializeAllocatorMetrics( Deserializer & deserializer, MetricsType & metrics )
	{
		u32 metricsSize{ 0 };
		deserializer.Deserialize( metricsSize );
		RED_MEMORY_ASSERT( metricsSize == sizeof( MetricsType ), "Allocator specific metrics is bigger then expected." );

		Memzero( &metrics, sizeof( MetricsType ) );
		u32 dataRead{ 0 };
		deserializer.Deserialize( &metrics, metricsSize, dataRead );
		RED_MEMORY_ASSERT( dataRead == metricsSize, "Inconsistent memory stream data." );
	}

	template< typename Allocator >
	void LogAllocatorMetrics( void* proxy, void (* func )( typename Allocator::AllocatorMetricsType& ) )
	{
		Allocator* allocator = static_cast< Allocator* >( proxy );

		typename Allocator::AllocatorMetricsType metrics;
		allocator->BuildMetrics( metrics );

		func( metrics );
	}
}

#endif

	const char* GetAllocatorName( ProxyTypeId proxyId )
	{
		switch( proxyId )
		{
		case DynamicTLSFAllocator::TypeId:
			return "DynamicTLSFAllocator";

		case LockingDynamicTLSFAllocator::TypeId:
			return "LockingDynamicTLSFAllocator";

		case StaticTLSFAllocator::TypeId:
			return "StaticTLSFAllocator";

		case DefaultAllocator::TypeId:
			return "DefaultAllocator";

		case LocklessStaticFixedSizeAllocator::TypeId:
			return "LocklessStaticFixedSizeAllocator";

		case DynamicFixedSizeAllocator::TypeId:
			return "DynamicFixedSizeAllocator";

		case DynamicStackAllocator::TypeId:
			return "DynamicStackAllocator";

		case StaticStackAllocator::TypeId:
			return "DynamicStackAllocator";

		case LocklessStaticLinearAllocator::TypeId:
			return "LocklessStaticLinearAllocator";

		case DynamicLinearAllocator::TypeId:
			return "DynamicLinearAllocator";

		case LinearAllocator::TypeId:
			return "LinearAllocator";

		case CircularAllocator::TypeId:
			return "CircularAllocator";

		case FrameAllocator::TypeId:
			return "FrameAllocator";

		case LocklessFrameAllocator::TypeId:
			return "LocklessFrameAllocator";

		case FrameAllocatorWithFallback::TypeId:
			return "FrameAllocatorWithFallback";

#ifndef RED_CONFIGURATION_FINAL
		case LocklessDebugBanFrameAllocator::TypeId:
			return "LocklessDebugBanFrameAllocator";
#endif

		case BigSizeAllocator::TypeId:
			return "BigSizeAllocator";

		case WwiseAllocator::TypeId:
			return "WwiseAllocator";

		case ICUAllocator::TypeId:
			return "ICUAllocator";

#ifdef RED_PLATFORM_DURANGO
		// #HACK: "Fake Allocator" exists in renderData, just for some gpuApi stuff. Don't really care about full
		// metrics right now, just having a sensible OOM report when D3D runs out. Name is different because
		// GPUFakeAllocator doesn't really give a good indication of what's happening, unless you already know :) And
		// again, just want to have a reasonable report for now.
		case red::CalculateHash32("GPUFakeAllocator"):
			return "D3DAllocator";
#endif

		default:
			return "<Unknown>";
		}
	}

	void LogAllocatorMetrics( ProxyTypeId proxyId, void* proxy )
	{
#ifdef RED_MEMORY_ENABLE_REPORT

		switch( proxyId )
		{
		case DynamicTLSFAllocator::TypeId:
			LogAllocatorMetrics< DynamicTLSFAllocator >( proxy, LogTLSFAllocator );
			break;

		case LockingDynamicTLSFAllocator::TypeId:
			LogAllocatorMetrics< LockingDynamicTLSFAllocator >( proxy, LogTLSFAllocator );
			break;

		case StaticTLSFAllocator::TypeId:
			LogAllocatorMetrics< StaticTLSFAllocator >( proxy, LogTLSFAllocator );
			break;

		case DefaultAllocator::TypeId:
			LogAllocatorMetrics< DefaultAllocator >( proxy, LogDefaultAllocator );
			break;

		case LocklessStaticFixedSizeAllocator::TypeId:
			LogAllocatorMetrics< LocklessStaticFixedSizeAllocator >( proxy, LogFixedSizeAllocator );
			break;

		case DynamicFixedSizeAllocator::TypeId:
			LogAllocatorMetrics< DynamicFixedSizeAllocator >( proxy, LogFixedSizeAllocator );
			break;

		case DynamicStackAllocator::TypeId:
			LogAllocatorMetrics< DynamicStackAllocator >( proxy, LogStackAllocator );
			break;

		case StaticStackAllocator::TypeId:
			LogAllocatorMetrics< DynamicStackAllocator >( proxy, LogStackAllocator );
			break;

		case LocklessStaticLinearAllocator::TypeId:
			LogAllocatorMetrics< LocklessStaticLinearAllocator >(proxy, LogLinearAllocator);
			break;

		case DynamicLinearAllocator::TypeId:
			LogAllocatorMetrics< DynamicLinearAllocator >( proxy, LogLinearAllocator );
			break;

		case LinearAllocator::TypeId:
			LogAllocatorMetrics< LinearAllocator >( proxy, LogLinearAllocator );
			break;

		case CircularAllocator::TypeId:
			LogAllocatorMetrics< CircularAllocator >( proxy, LogCircularAllocator );
			break;

		case FrameAllocator::TypeId:
			LogAllocatorMetrics< FrameAllocator >( proxy, LogFrameAllocator );
			break;

		case LocklessFrameAllocator::TypeId:
			LogAllocatorMetrics< LocklessFrameAllocator >( proxy, LogFrameAllocator );
			break;

		case FrameAllocatorWithFallback::TypeId:
			LogAllocatorMetrics< FrameAllocatorWithFallback >( proxy, LogFrameAllocatorWithFallback );
			break;

#ifndef RED_CONFIGURATION_FINAL
		case LocklessDebugBanFrameAllocator::TypeId:
			LogAllocatorMetrics< LocklessDebugBanFrameAllocator >( proxy, LogFrameAllocator );
			break;
#endif

		case BigSizeAllocator::TypeId:
			LogAllocatorMetrics< BigSizeAllocator >( proxy, LogBigSizeAllocator );
			break;

#ifdef RED_PLATFORM_ORBIS
		case GpuAllocator::TypeId:
			LogAllocatorMetrics< GpuAllocator >( proxy, LogGpuAllocator );
			break;
#endif

		case WwiseAllocator::TypeId:
			LogAllocatorMetrics< WwiseAllocator >( proxy, LogWwiseAllocator );
			break;

		case ICUAllocator::TypeId:
			LogAllocatorMetrics< ICUAllocator >( proxy, LogICUAllocator );
			break;

#ifdef RED_PLATFORM_DURANGO
		// #HACK: "Fake Allocator" exists in renderData, just for some gpuApi stuff. Don't really care about full
		// metrics right now, just having a sensible OOM report when D3D runs out.
		case red::CalculateHash32("GPUFakeAllocator"):
			break;
#endif

		default:
			RED_LOG_ERROR( "Unknown allocator %lu. This should never happend.", proxyId );
			break;
		}
#else
		RED_UNUSED( proxyId );
		RED_UNUSED( proxy );
#endif
	}

	void CoreAllocatorsMetricsLogger( ProxyTypeId proxyId, Deserializer & deserializer )
	{
#ifdef RED_MEMORY_ENABLE_REPORT

		switch ( proxyId )
		{
		case DynamicTLSFAllocator::TypeId:
		case LockingDynamicTLSFAllocator::TypeId:
		case StaticTLSFAllocator::TypeId:
			{
				TLSFAllocatorMetrics metrics;
				DeserializeAllocatorMetrics( deserializer, metrics );
				LogTLSFAllocator( metrics );
			}
			break;

		case WwiseAllocator::TypeId:
			{
				WwiseAllocatorMetrics metrics;
				DeserializeAllocatorMetrics( deserializer, metrics );
				LogWwiseAllocator( metrics );
			}
			break;

		case ICUAllocator::TypeId:
			LogICUAllocatorMetrics( deserializer );
			break;

		case DefaultAllocator::TypeId:
			{
				DefaultAllocatorMetrics metrics;
				DeserializeAllocatorMetrics( deserializer, metrics );
				LogDefaultAllocator( metrics );
			}
			break;
		case LocklessStaticFixedSizeAllocator::TypeId:
		case DynamicFixedSizeAllocator::TypeId:
			{
				FixedSizeAllocatorMetrics metrics;
				DeserializeAllocatorMetrics( deserializer, metrics );
				LogFixedSizeAllocator( metrics );
			}
			break;

		case DynamicStackAllocator::TypeId:
		case StaticStackAllocator::TypeId:
			{
				StackAllocatorMetrics metrics;
				DeserializeAllocatorMetrics( deserializer, metrics );
				LogStackAllocator( metrics );
			}
			break;

		case UnsafeDynamicLinearAllocator::TypeId:
		case LocklessStaticLinearAllocator::TypeId:
		case DynamicLinearAllocator::TypeId:
		case LinearAllocator::TypeId:
			{
				LinearAllocatorMetrics metrics;
				DeserializeAllocatorMetrics( deserializer, metrics );
				LogLinearAllocator( metrics );
			}
			break;

		case CircularAllocator::TypeId:
			{
				CircularAllocatorMetrics metrics;
				DeserializeAllocatorMetrics( deserializer, metrics );
				LogCircularAllocator( metrics );
			}
			break;

		case FrameAllocator::TypeId:
		case LocklessFrameAllocator::TypeId:
#ifndef RED_CONFIGURATION_FINAL
		case LocklessDebugBanFrameAllocator::TypeId:
#endif
			{
				FrameAllocatorMetrics metrics;
				DeserializeAllocatorMetrics( deserializer, metrics );
				LogFrameAllocator( metrics );
			}
			break;

		case FrameAllocatorWithFallback::TypeId:
			{
				FrameAllocatorWithFallbackMetrics metrics;
				DeserializeAllocatorMetrics( deserializer, metrics );
				LogFrameAllocatorWithFallback( metrics );
			}
			break;

		case BigSizeAllocator::TypeId:
			{
				BigSizeAllocatorMetrics metrics;
				DeserializeAllocatorMetrics( deserializer, metrics );
				LogBigSizeAllocator( metrics );
			}
			break;

#ifdef RED_PLATFORM_ORBIS
		case GpuAllocator::TypeId:
			{
				GpuAllocatorMetrics metrics;
				DeserializeAllocatorMetrics( deserializer, metrics );
				LogGpuAllocator( metrics );
			}
			break;
#endif

		default:
			DeserializeUnknownAllocatorMetrics( deserializer );
			break;
		}

#else
		RED_UNUSED( proxyId );
		RED_UNUSED( deserializer );
#endif
	}

	void CoreAllocatorsMetricsJsonWriter( ProxyTypeId proxyId, Deserializer & deserializer )
	{
#ifdef RED_MEMORY_ENABLE_REPORT

		switch( proxyId )
		{
		case DynamicTLSFAllocator::TypeId:
		case LockingDynamicTLSFAllocator::TypeId:
		case StaticTLSFAllocator::TypeId:
		{
			TLSFAllocatorMetrics metrics;
			DeserializeAllocatorMetrics( deserializer, metrics );
			WriteTLSFAllocatorToJson( metrics );
		}
		break;

		case WwiseAllocator::TypeId:
		{
			WwiseAllocatorMetrics metrics;
			DeserializeAllocatorMetrics( deserializer, metrics );
			WriteWwiseAllocatorToJson( metrics );
		}
		break;

		case DefaultAllocator::TypeId:
		{
			DefaultAllocatorMetrics metrics;
			DeserializeAllocatorMetrics( deserializer, metrics );
			WriteDefaultAllocatorToJson( metrics );
		}
		break;

		case LocklessStaticFixedSizeAllocator::TypeId:
		case DynamicFixedSizeAllocator::TypeId:
		{
			FixedSizeAllocatorMetrics metrics;
			DeserializeAllocatorMetrics( deserializer, metrics );
			WriteFixedSizeAllocatorToJson( metrics );
		}
		break;

		case DynamicStackAllocator::TypeId:
		case StaticStackAllocator::TypeId:
		{
			StackAllocatorMetrics metrics;
			DeserializeAllocatorMetrics( deserializer, metrics );
			WriteStackAllocatorToJson( metrics );
		}
		break;

		case UnsafeDynamicLinearAllocator::TypeId:
		case LocklessStaticLinearAllocator::TypeId:
		case DynamicLinearAllocator::TypeId:
		case LinearAllocator::TypeId:
		{
			LinearAllocatorMetrics metrics;
			DeserializeAllocatorMetrics( deserializer, metrics );
			WriteLinearAllocatorToJson( metrics );
		}
		break;

		case CircularAllocator::TypeId:
		{
			CircularAllocatorMetrics metrics;
			DeserializeAllocatorMetrics( deserializer, metrics );
			WriteCircularAllocatorToJson( metrics );
		}
		break;

		case FrameAllocator::TypeId:
		case LocklessFrameAllocator::TypeId:
#ifndef RED_CONFIGURATION_FINAL
		case LocklessDebugBanFrameAllocator::TypeId:
#endif
		{
			FrameAllocatorMetrics metrics;
			DeserializeAllocatorMetrics( deserializer, metrics );
			WriteFrameAllocatorToJson( metrics );
		}
		break;

		case FrameAllocatorWithFallback::TypeId:
		{
			FrameAllocatorWithFallbackMetrics metrics;
			DeserializeAllocatorMetrics( deserializer, metrics );
			WriteFrameAllocatorWithFallbackToJson( metrics );
		}
		break;

		case BigSizeAllocator::TypeId:
		{
			BigSizeAllocatorMetrics metrics;
			DeserializeAllocatorMetrics( deserializer, metrics );
			WriteBigSizeAllocatorToJson( metrics );
		}
		break;

#ifdef RED_PLATFORM_ORBIS
		case GpuAllocator::TypeId:
		{
			GpuAllocatorMetrics metrics;
			DeserializeAllocatorMetrics( deserializer, metrics );
			WriteGpuAllocatorToJson( metrics );
		}
		break;
#endif

		default:
			AllocatorMetrics metrics;
			metrics.consumedMemoryBytes = 0;
			metrics.bookKeepingBytes = 0;
			metrics.consumedSystemMemoryBytes = 0;
			metrics.largestBlockSize = 0;
			metrics.smallestBlockSize = 0;
			DeserializeUnknownAllocatorMetrics( deserializer );
			WriteBaseAllocatorMetricsToJson(metrics);
			break;
		}
#else
		RED_UNUSED( proxyId );
		RED_UNUSED( deserializer );
#endif
	}
}
}