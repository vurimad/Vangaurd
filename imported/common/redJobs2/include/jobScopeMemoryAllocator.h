/*
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../redMemory/include/frameAllocator.h"
#include "../../redMemory/include/threadIdProvider.h"
#include "../../redMemory/include/threadMonitor.h"
#include "../../redMemory/include/threadConstant.h"
#include "../../redContainers/include/fixedArray.h"

namespace red
{
namespace memory
{
	class SystemAllocator;
	class Serializer;
	class Deserializer;
}
}

namespace job
{
namespace prv
{
	using red::memory::c_maxThreadCount;

	struct JobScopeMemoryAllocatorMetrics
	{
		red::memory::AllocatorMetrics metrics;

		Uint64 waste;
		double wastePercent;

		struct ThreadCacheMetric
		{
			Uint32 threadId;
			red::memory::FrameAllocatorWithFallbackMetrics frameAllocatorInfo;
		};

		ThreadCacheMetric threadCacheInfo[ c_maxThreadCount ];
	};

	struct JobScopeMemoryAllocatorParameter
	{
		red::memory::SystemAllocator* systemAllocator;
		red::memory::ThreadMonitor* threadMonitor;
	};

	class REDJOBS2_API JobScopeMemoryAllocator
	{
	public:
		RED_MEMORY_DECLARE_ALLOCATOR( JobScopeMemoryAllocator, JobScopeMemoryAllocatorMetrics, 8 );

		typedef Uint32 ThreadId;

		JobScopeMemoryAllocator();
		~JobScopeMemoryAllocator();

		void Initialize( const JobScopeMemoryAllocatorParameter& parameter );
		void Uninitialize();

		red::memory::Block Allocate( Uint32 size );
		red::memory::Block AllocateAligned( Uint32 size, Uint32 alignment );
		red::memory::Block Reallocate( red::memory::Block& block, Uint32 size );
		red::memory::Block ReallocateAligned( red::memory::Block& block, Uint32 size, Uint32 alignment );
		void Free( red::memory::Block& block );
		void Reset();

		bool OwnBlock( Uint64 block ) const;
		Uint64 GetBlockSize( Uint64 address ) const;

		void RegisterCurrentThread();

		void BuildMetrics( JobScopeMemoryAllocatorMetrics& metrics );
		void SerializeMetrics( red::memory::Serializer& serializer );

		Uint64 Debug_GetOutOfBudgetBytes() const;

	private:
		typedef red::FixedArray< ThreadId, c_maxThreadCount > ThreadIdDictionary;

		red::memory::FrameAllocatorWithFallback* AcquireLocalFrameAllocator();
		const red::memory::FrameAllocatorWithFallback* AcquireLocalFrameAllocator() const;
		red::memory::FrameAllocatorWithFallback* GetFrameAllocator( ThreadId id );
		const red::memory::FrameAllocatorWithFallback* GetFrameAllocator( ThreadId id ) const;

		ThreadIdDictionary::iterator RegisterThread( ThreadId id );

		void OnThreadDied( ThreadId id );
		static void NotifyOnThreadDied( ThreadId id, void * userData );

		RED_ALIGN( 64 ) ThreadIdDictionary m_threadIdDictionary;
		RED_ALIGN( 64 ) red::memory::FrameAllocatorWithFallback m_allocators[ c_maxThreadCount ];

		red::memory::ThreadIdProvider m_threadIdProvider;
		red::memory::SystemAllocator* m_systemAllocator;
		red::memory::ThreadMonitor* m_threadMonitor;
	};

	void LogJobScopeMemoryAllocatorMetrics( red::memory::ProxyTypeId allocatorId, red::memory::Deserializer& deserializer );

}
}