/**
 * Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_MEMORY_LOCKLESS_SLAB_ALLOCATOR_H_
#define _RED_MEMORY_LOCKLESS_SLAB_ALLOCATOR_H_

#include "../include/utils.h"
#include "../include/threadConstant.h"
#include "../include/threadIdProvider.h"
#include "../include/simpleArray.h"
#include "../include/systemBlock.h"
#include "allocatorMetrics.h"
#include "allocator.h"
#include "slabAllocator.h"
#include "slabChunkAllocatorInterface.h"
#include "spinLock.h"

namespace red
{
namespace memory
{
	class SystemAllocator;
	class ThreadMonitor;

	struct LocklessSlabAllocatorMetrics
	{
		AllocatorMetrics metrics;

		u32 usedChunkCount;
		u32 freeChunkCount;
		u64 waste;
		double wastePercent;

		struct ThreadCacheMetric
		{
			u32 threadId;
			red::AnsiChar threadName[ c_maxThreadNameLength ];
			SlabAllocatorMetrics slabAllocatorInfo;
		};

		ThreadCacheMetric threadCacheInfo[ c_maxThreadCount ];
	};

	struct LocklessSlabAllocatorParameter
	{
		u64 virtualRangeSize;
		u32 initialChunkCount;
		u32 flags;
		SystemAllocator * systemAllocator;
		ThreadMonitor * threadMonitor;
	};

	class RED_MEMORY_API LocklessSlabAllocator
	{
	public:

		RED_MEMORY_DECLARE_ALLOCATOR( LocklessSlabAllocator, LocklessSlabAllocatorMetrics, 8 );

		typedef u32 ThreadId;

		LocklessSlabAllocator();
		~LocklessSlabAllocator();

		void Initialize( const LocklessSlabAllocatorParameter & parameter );
		void Uninitialize();

		Block Allocate( u32 size );
		Block AllocateAligned( u32 size, u32 alignement );
		Block Reallocate( Block & block, u32 size );
		Block ReallocateAligned( Block & block, u32 size, u32 alignment );
		void Free( Block & block );

		bool OwnBlock( u64 block ) const;
		u64 GetBlockSize( u64 address ) const;

		void RegisterCurrentThread( const AnsiChar* threadName );
		bool IsCurrentThreadRegistered() const;

		void BuildMetrics( LocklessSlabAllocatorMetrics & metrics );

		// UNIT TEST ONLY
		void InternalSetThreadIdProvider( const ThreadIdProvider * provider );
		SlabAllocator * InternalAcquireLocalAllocator( ThreadId id );
		void InternalRegisterThread( ThreadId id );
		SlabChunk * InternalAllocateChunk();
		void InternalSetFreeChunk( ThreadId id, SlabChunk * chunk );
		void InternalMarkAllLocalSlabAllocatorAsTaken();
		bool InternalIsThreadBoundToALocalSlabAllocator( ThreadId id ) const;

	private:

		class RED_MEMORY_API SlabChunkAllocator : public SlabChunkAllocatorInterface
		{
		public:
			SlabChunkAllocator();
			
			void Initialize( u32 chunkCount, u32 flags, const SystemBlock& extraMemory );

			virtual SlabChunk * Allocate( u32 allocatorId, u32 flags ) override final;
			virtual void Free( u32 allocatorId, SlabChunk * chunk ) override final;

			void SetSystemAllocator( SystemAllocator * allocator );
			void SetNextMemoryAddress( u64 addressStart, u64 addressEnd );

			SlabChunk * AllocateChunk( u32 flags );
			void ForceFree( u32 allocatorId, SlabChunk * chunk );

			void BuildMetrics( LocklessSlabAllocatorMetrics & metrics );

		private:

			struct FreeChunks
			{
				SlabChunk * chunkList;
				SpinLock lock;
			};

			SlabChunk * TryTakingFreeChunk( u32 id );

			FreeChunks m_freeChunks[ c_maxThreadCount ];
			SystemAllocator * m_systemAllocator;

			u64 m_nextChunkAddress;
			u64 m_firstChunkAddress;
			u64 m_lastChunkAddress;
			u64 m_extraMemoryBytes;
			atomic::TAtomic32 m_chunkCount;
			atomic::TAtomic32 m_freeChunkCount;
			SpinLock m_monitor; // m_nextChunkAddress need to be protected. Multiple thread might want a Chunk at same time.
		};

		typedef SimpleArray< ThreadId, c_maxThreadCount > ThreadIdDictionary;
		typedef SimpleArray< red::AnsiChar[ c_maxThreadNameLength ], c_maxThreadCount > ThreadNameDictionary;

		LocklessSlabAllocator( const LocklessSlabAllocator& );
		LocklessSlabAllocator & operator=( const LocklessSlabAllocator& );

		SlabAllocator * AcquireLocalSlabAllocator();
		SlabAllocator * AcquireSlabAllocator( ThreadId id );
		SlabAllocator * GetSlabAllocator( ThreadId id );
		
		ThreadIdDictionary::iterator RegisterThread( ThreadId id, const AnsiChar* threadName );

		void OnThreadDied( ThreadId id );  
		static void NotifyOnThreadDied( ThreadId id, void * userData );
		
		RED_ALIGN( 64 ) ThreadIdDictionary m_threadIdDictionary;
		RED_ALIGN( 64 ) SlabAllocator m_allocators[ c_maxThreadCount ];
		ThreadNameDictionary m_threadNameDictionary;
	
		VirtualRange m_reservedMemoryRange;
		SystemBlock m_extraMemory;
		
		SystemAllocator * m_systemAllocator;
		SlabChunkAllocator m_slabChunkAllocator;

		const ThreadIdProvider * m_threadIdProvider; // For Unit Test only. Could be removed in final build.
		ThreadIdProvider m_threadIdProviderStorage;

		u32 m_flags;
		ThreadMonitor * m_threadMonitor;
	};

	RED_MEMORY_API LocklessSlabAllocator & AcquireLocklessSlabAllocator();
}
}

#include "locklessSlabAllocator.hpp"

#endif
