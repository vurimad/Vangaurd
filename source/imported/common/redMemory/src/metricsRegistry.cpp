/**
 * Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "metricsRegistry.h"
#include "assert.h"
#include "../include/block.h"

namespace red
{
namespace memory
{
	MetricsRegistry::MetricsRegistry()
	{
		std::memset( &m_metricsStorage, 0, sizeof( m_metricsStorage ) );
		m_metricsHashTable.Fill( nullptr );
	}

	MetricsRegistry::~MetricsRegistry()
	{}

	void MetricsRegistry::OnAllocate( PoolHandle handle, const Block & block )
	{
		if ( block.size > 0 )
		{
			if ( PoolMetrics* metrics = FindPoolMetrics( handle ) )
			{
				UpdatePoolAllocateMetrics( *metrics, block.size );
			}
		}
	}
	
	void MetricsRegistry::OnDeallocate( PoolHandle handle, const Block & block )
	{
		if ( block.size > 0 )
		{
			if ( PoolMetrics* metrics = FindPoolMetrics( handle ) )
			{
				UpdatePoolDeallocateMetrics( *metrics, block.size );
			}
		}
	}
	
	void MetricsRegistry::OnReallocate( PoolHandle handle, const Block & inputBlock, const Block & outputBlock )
	{
		if ( PoolMetrics* metrics = FindPoolMetrics( handle ) )
		{
			UpdatePoolReallocateMetrics( *metrics, inputBlock.size, outputBlock.size );
		}
	}

	void MetricsRegistry::ResetMetrics( PoolHandle handle )
	{
		if ( PoolMetrics* metrics = FindPoolMetrics( handle ) )
		{
			ResetPoolMetrics( *metrics );
		}
	}

	void MetricsRegistry::PrepareMetricsForNextFrame()
	{
		for ( Entry* entry : m_metricsHashTable )
		{
			for ( Entry* currentEntry = entry; currentEntry != nullptr; currentEntry = currentEntry->next )
			{
				PreparePoolMetricsForNextFrame( currentEntry->metrics );
			}
		}
	}

	u64 MetricsRegistry::GetPoolTotalBytesAllocated( PoolHandle handle ) const
	{
		if ( PoolMetrics* metrics = FindPoolMetrics( handle ) )
		{
			return metrics->bytesAllocated;
		}
		else
		{
			return 0;
		}
	}

	u32 MetricsRegistry::GetPoolAllocationCount( PoolHandle handle ) const
	{
#ifdef RED_MEMORY_ENABLE_EXTENDED_METRICS
		if ( PoolMetrics* metrics = FindPoolMetrics( handle ) )
		{
			return metrics->allocationCount;
		}
		else
		{
			return 0;
		}
#else
		RED_UNUSED( handle );
		return 0;
#endif
	}

	u64 MetricsRegistry::GetTotalBytesAllocated() const
	{
		u64 bytes = 0;
		for( auto iter = m_metricsStorage.Begin(), end = m_metricsStorage.End(); iter != end; ++iter )
		{
			if ( !iter->isMirrored )
			{
				bytes += iter->metrics.bytesAllocated;
			}
		}
		return bytes; 
	}

	u32 MetricsRegistry::GetTotalAllocationCount() const
	{
		u32 allocationCount = 0;
#ifdef RED_MEMORY_ENABLE_EXTENDED_METRICS
		for ( const auto & entry : m_metricsStorage )
		{
			if ( !entry.isMirrored )
			{
				allocationCount += entry.metrics.allocationCount;
			}
		}
#endif
		return allocationCount;
	}

	void MetricsRegistry::GetRuntimePoolMetrics( PoolHandle handle, RuntimePoolMetrics & poolMetrics ) const
	{
#ifdef RED_MEMORY_ENABLE_EXTENDED_METRICS
		if ( PoolMetrics* metrics = FindPoolMetrics( handle ) )
		{
			poolMetrics.bytesAllocated = metrics->bytesAllocated;
			poolMetrics.allocationCount = metrics->allocationCount;
			poolMetrics.bytesAllocatedPeak = metrics->bytesAllocatedPeak;
			poolMetrics.bytesAllocatedPerFrame = metrics->bytesAllocatedPerFrame;
			poolMetrics.bytesAllocatedPerFramePrevious = metrics->bytesAllocatedPerFramePrevious;
			poolMetrics.bytesDeallocatedPerFrame = metrics->bytesDeallocatedPerFrame;
			poolMetrics.bytesDeallocatedPerFramePrevious = metrics->bytesDeallocatedPerFramePrevious;
			poolMetrics.allocationPerFrameCount = metrics->allocationPerFrame;
			poolMetrics.allocationPerFrameCountPrevious = metrics->allocationPerFramePrevious;
		}
		else
		{
			poolMetrics.bytesAllocated = 0;
			poolMetrics.allocationCount = 0;
			poolMetrics.bytesAllocatedPeak = 0;
			poolMetrics.bytesAllocatedPerFrame = 0;
			poolMetrics.bytesAllocatedPerFramePrevious = 0;
			poolMetrics.bytesDeallocatedPerFrame = 0;
			poolMetrics.bytesDeallocatedPerFramePrevious = 0;
			poolMetrics.allocationPerFrameCount = 0;
			poolMetrics.allocationPerFrameCountPrevious = 0;
		}
#else
		RED_UNUSED( handle );
		RED_UNUSED( poolMetrics );
#endif
	}

	PoolMetrics* MetricsRegistry::FindPoolMetrics( PoolHandle handle ) const
	{
		const u32 bucketIndex = handle % c_metricsHashTableSize;
		for( Entry* entry = m_metricsHashTable[ bucketIndex ]; entry != nullptr; entry = entry->next )
		{
			if( entry->handle == handle )
			{
				return &( entry->metrics );
			}
		}

		return nullptr;
	}

	PoolMetrics& MetricsRegistry::RegisterPoolMetrics( PoolHandle handle ) const
	{
		struct Predicate
		{
			bool operator()( const Entry& entry ) const { return entry.handle == c_poolNodeInvalid; }
		};

		auto iter = std::find_if( m_metricsStorage.Begin(), m_metricsStorage.End(), Predicate() );
		Entry& newEntry = *iter;
		newEntry.handle = handle;
		
		const u32 bucketIndex = handle % c_metricsHashTableSize;
		Entry * currentEntry = m_metricsHashTable[ bucketIndex ];
		Entry ** entryBackPointer = m_metricsHashTable.Data() + bucketIndex;
		while( currentEntry != nullptr )
		{
			if ( currentEntry->handle == handle )
			{
				// in case of hash collision and multiple pool registration
				// clear new entry
				newEntry.handle = c_poolNodeInvalid;
				// and return already registered metrics
				return currentEntry->metrics;
			}

			entryBackPointer = &currentEntry->next;
			currentEntry = currentEntry->next;
		}

		*entryBackPointer = &newEntry;

		return newEntry.metrics;
	}

	void MetricsRegistry::SetMirroredPool( PoolHandle handle )
	{
		const u32 bucketIndex = handle % c_metricsHashTableSize;
		for ( Entry* entry = m_metricsHashTable[bucketIndex]; entry != nullptr; entry = entry->next )
		{
			if ( entry->handle == handle )
			{
				entry->isMirrored = true;
				return;
			}
		}
	}

}
}
