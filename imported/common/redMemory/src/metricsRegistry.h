/**
 * Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_MEMORY_METRICS_REGISTRY_H_
#define _RED_MEMORY_METRICS_REGISTRY_H_

#include "poolMetrics.h"
#include "poolConstant.h"
#include "metricsUtils.h"

namespace red
{
namespace memory
{
	struct Block;

	const u32 c_maxMetricsCount = c_poolMaxCount;
	const u32 c_metricsHashTableSize = 1 << 8;

	class MetricsRegistry
	{
	public:

		MetricsRegistry();
		~MetricsRegistry();

		PoolMetrics& RegisterPoolMetrics( PoolHandle handle ) const;

		void SetMirroredPool( PoolHandle handle );

		void OnAllocate( PoolHandle handle, const Block & block ); 
		void OnDeallocate( PoolHandle handle, const Block & block );
		void OnReallocate( PoolHandle handle, const Block & inputBlock, const Block & outputBlock );

		void ResetMetrics( PoolHandle handle );

		void PrepareMetricsForNextFrame();

		u64 GetPoolTotalBytesAllocated( PoolHandle handle ) const; 
		u32 GetPoolAllocationCount( PoolHandle handle ) const; 
		u64 GetTotalBytesAllocated() const;
		u32 GetTotalAllocationCount() const;
		void GetRuntimePoolMetrics( PoolHandle handle, RuntimePoolMetrics & poolMetrics ) const;

	private:
		PoolMetrics* FindPoolMetrics(PoolHandle handle) const;

		struct Entry
		{
			PoolHandle handle;
			Bool isMirrored;
			Entry *next;
			PoolMetrics metrics;
		};

		mutable SimpleArray< Entry, c_poolMaxCount > m_metricsStorage;
		mutable SimpleArray< Entry*, c_metricsHashTableSize > m_metricsHashTable;
	};
}
}

#include "metricsRegistry.hpp"

#endif
