/**
 * Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "locklessSlabAllocator.h"
#include "systemAllocator.h"
#include "threadIdProvider.h"
#include "slabChunk.h"
#include "slabHeader.h"
#include "threadMonitor.h"
#include "vault.h"
#include "flags.h"
#include "scopedLock.h"

#ifdef RED_PLATFORM_DURANGO
#include <xmem.h>
#endif

namespace red
{
namespace memory
{
	LocklessSlabAllocator::SlabChunkAllocator::SlabChunkAllocator()
		:	m_systemAllocator( nullptr ),
			m_nextChunkAddress( 0 ),
			m_firstChunkAddress( 0 ),
			m_lastChunkAddress( 0 ),
			m_extraMemoryBytes( 0 ),
			m_chunkCount( 0 ),
			m_freeChunkCount( 0 )
	{
		std::memset( m_freeChunks, 0, sizeof( m_freeChunks ) );
	}

	void LocklessSlabAllocator::SlabChunkAllocator::Initialize( u32 chunkCount, u32 flags, const SystemBlock& extraMemory )
	{
		if( extraMemory != NullSystemBlock() )
		{
			u64 address = extraMemory.address;
			u64 endAddress = address + extraMemory.size;
			while( address < endAddress )
			{
				SystemBlock block = {};
				block = ComputeRequiredSystemBlockForSlabChunk( address );
				const u64 nextChunkAddress = block.address + block.size;
				if( nextChunkAddress >= endAddress )
				{
					break;
				}

				atomic::Increment32( &m_chunkCount );

				address = nextChunkAddress;
				SlabChunk * chunk = CreateSlabChunk( block );
				ForceFree( 0, chunk );
				++m_freeChunkCount;

				m_extraMemoryBytes += block.size;
			}
		}


		for( u32 count = 0; count != chunkCount; ++count )
		{
			SlabChunk * chunk = AllocateChunk( flags );
			ForceFree( 0, chunk );
			++m_freeChunkCount;
		}
	}

	SlabChunk * LocklessSlabAllocator::SlabChunkAllocator::Allocate( u32 allocatorId, u32 flags )
	{
		SlabChunk * chunk = nullptr;
		
		for( u32 index = allocatorId; index != c_maxThreadCount && !chunk; ++index )
		{
			chunk = TryTakingFreeChunk( index );
		}

		// We do not have a chunk yet. 
		// Might be either there is no chunk available in most relevant list, or massive contention problem.
		// Loop through the whole array of free chunk list to find one.
		for( u32 index = 0; index != c_maxThreadCount && !chunk; ++index )
		{
			chunk = TryTakingFreeChunk( index );
		}

		// Did we found a chunk in the end ? Yes, take it, else create a new one.
		if( chunk )
		{
			atomic::Decrement32( &m_freeChunkCount );
			return chunk;
		}

		return AllocateChunk( flags );
	}

	SlabChunk * LocklessSlabAllocator::SlabChunkAllocator::TryTakingFreeChunk( u32 id )
	{
		SlabChunk * chunk = nullptr;
		FreeChunks & chunks = m_freeChunks[ id ];
		if( chunks.chunkList && chunks.lock.TryAcquire() )
		{
			// We got the lock! But list could have changed between that time. Check again.
			if( chunks.chunkList )
			{
				chunk = chunks.chunkList;
				chunks.chunkList = chunk->nextFree;
			}

			chunks.lock.Release(); 
		}

		return chunk;
	}

	SlabChunk * LocklessSlabAllocator::SlabChunkAllocator::AllocateChunk( u32 flags )
	{
		atomic::Increment32( &m_chunkCount );

		SystemBlock block = {};

		{
			// Gotta make sure that multiple thread won't get same chunk address. 
			// No worries, it will be very fast. 
			// If it turns out to be a problem, it could be implemented using atomics.
			ScopedLock< SpinLock > scopedLock( m_monitor );
			block = ComputeRequiredSystemBlockForSlabChunk( m_nextChunkAddress );
			const u64 nextChunkAddress = block.address + block.size;
			if( nextChunkAddress >= m_lastChunkAddress )
			{
				// Out of memory.
				return nullptr;
			}
			m_nextChunkAddress = block.address + block.size;
		}
		
		block = m_systemAllocator->Commit( block, flags ); // this allocator is cpu only and need read/write access.
		
		return block.address ? CreateSlabChunk( block ) : nullptr;
	}

	void LocklessSlabAllocator::SlabChunkAllocator::Free( u32 allocatorId, SlabChunk * chunk )
	{
		atomic::Increment32( &m_freeChunkCount );

		for( u32 index = allocatorId; index != c_maxThreadCount; ++index )
		{
			FreeChunks & availableChunk = m_freeChunks[ index ];
			if( availableChunk.lock.TryAcquire() )
			{
				chunk->nextFree = availableChunk.chunkList;
				availableChunk.chunkList = chunk;
				availableChunk.lock.Release();
				return;
			}
		}

		// Restart check from first local allocator. Give chunk to the first one that give lock.
		for( u32 index = 0; index != c_maxThreadCount; ++index )
		{
			FreeChunks & availableChunk = m_freeChunks[ index ];
			if( availableChunk.lock.TryAcquire() )
			{
				chunk->nextFree = availableChunk.chunkList;
				availableChunk.chunkList = chunk;
				availableChunk.lock.Release();
				return;
			}
		}

		// If we reach this point, we have a lot of contention.
		// Multiple thread are freeing full chunk or/and trying to recycle memory
		// This should be extremely rare. 
		// We got to give memory back, so lock FreeChunks list bound to this thread.
		ForceFree( allocatorId, chunk );
	}

	void LocklessSlabAllocator::SlabChunkAllocator::ForceFree( u32 allocatorId, SlabChunk * chunk )
	{
		FreeChunks & availableChunk = m_freeChunks[ allocatorId ];
		ScopedLock< SpinLock > lock( availableChunk.lock );
		chunk->nextFree = availableChunk.chunkList;
		availableChunk.chunkList = chunk;
	}

	void LocklessSlabAllocator::SlabChunkAllocator::SetSystemAllocator( SystemAllocator * allocator )
	{
		m_systemAllocator = allocator;
	}
	
	void LocklessSlabAllocator::SlabChunkAllocator::SetNextMemoryAddress( u64 addressStart, u64 addressEnd )
	{
		m_firstChunkAddress = addressStart;
		m_nextChunkAddress = addressStart;
		m_lastChunkAddress = addressEnd;
	}

	void LocklessSlabAllocator::SlabChunkAllocator::BuildMetrics( LocklessSlabAllocatorMetrics & metrics )
	{
		metrics.metrics.consumedSystemMemoryBytes = ( m_nextChunkAddress - m_firstChunkAddress ) + m_extraMemoryBytes;
		metrics.metrics.bookKeepingBytes = 0;
		metrics.freeChunkCount = m_freeChunkCount;
		metrics.usedChunkCount = m_chunkCount - m_freeChunkCount;
	}

	LocklessSlabAllocator::LocklessSlabAllocator()
		:	m_extraMemory( NullSystemBlock() ),
			m_systemAllocator( nullptr ),
			m_threadIdProvider( nullptr ),
			m_flags( Flags_CPU_Read_Write ),
			m_threadMonitor( nullptr )
	{
		m_threadIdProvider = &m_threadIdProviderStorage;
		m_threadIdDictionary.Fill( 0 );
		red::Memzero( &m_threadNameDictionary, c_maxThreadNameLength * c_maxThreadCount );
	}

	LocklessSlabAllocator::~LocklessSlabAllocator()
	{}

	void LocklessSlabAllocator::Initialize( const LocklessSlabAllocatorParameter & parameter )
	{
		m_systemAllocator = parameter.systemAllocator;
		m_threadMonitor = parameter.threadMonitor;

		RED_MEMORY_ASSERT( m_systemAllocator, "LocklessSlabAllocator need access to SystemAllocator." );
		RED_MEMORY_ASSERT( m_threadMonitor, "LocklessSlabAllocator need access to ThreadMonitor." );
		
		m_flags = parameter.flags;
		m_reservedMemoryRange = m_systemAllocator->ReserveVirtualRange( parameter.virtualRangeSize, m_flags );

#ifdef RED_PLATFORM_DURANGO

		CONSOLE_TYPE consoleType = ::GetConsoleType();

		// This memory is only available on Xbox One or Xbox One S - Xbox One X and above don't need it
		if( ( consoleType == CONSOLE_TYPE_XBOX_ONE ) || ( consoleType == CONSOLE_TYPE_XBOX_ONE_S ) )
		{
			u8* extraMemoryBuffer = NULL;
			SIZE_T byteCount = 0;
			XMEM_AUX_TITLE_MEM_CONTEXT context = XMemGetAuxiliaryTitleMemory( &(PVOID)extraMemoryBuffer, &byteCount );
			if((context == NULL) || (byteCount != RED_MEGA_BYTE( 256 )))
			{
				// Something went wrong, so free auxiliary memory
				XMemReleaseAuxiliaryTitleMemory( context );
			}
			else
			{
				m_extraMemory.address = AddressOf( extraMemoryBuffer );
				m_extraMemory.size = byteCount;
			}
		}

#endif
		
		m_slabChunkAllocator.SetSystemAllocator( m_systemAllocator );
		m_slabChunkAllocator.SetNextMemoryAddress( m_reservedMemoryRange.start, m_reservedMemoryRange.end );
		m_slabChunkAllocator.Initialize( parameter.initialChunkCount, parameter.flags, m_extraMemory );

		const SlabAllocatorParameter slabAllocatorParameter = 
		{
			m_reservedMemoryRange,
			&m_slabChunkAllocator,
			m_extraMemory.address,
			m_flags
		};

		for( u32 id = 0; id != c_maxThreadCount; ++id )
		{
			SlabAllocator & allocator = m_allocators[ id ];
			allocator.SetId( id );
			allocator.Initialize( slabAllocatorParameter );
		}

		m_threadMonitor->RegisterOnThreadDiedSignal( LocklessSlabAllocator::NotifyOnThreadDied, this );
	}

	void LocklessSlabAllocator::Uninitialize()
	{
		RED_MEMORY_ASSERT( m_threadMonitor, "LocklessSlabAllocator need access to ThreadMonitor." );
		RED_MEMORY_ASSERT( m_systemAllocator, "LocklessSlabAllocator need access to SystemAllocator." );
		
		m_threadMonitor->UnregisterFromThreadDiedSignal( LocklessSlabAllocator::NotifyOnThreadDied, this );
		m_systemAllocator->ReleaseVirtualRange( m_reservedMemoryRange ); 
	}

	Block LocklessSlabAllocator::Allocate( u32 size )
	{
		SlabAllocator * allocator = AcquireLocalSlabAllocator();
		if( allocator )
		{
			return allocator->Allocate( size );
		}

		ALWAYSENABLED_RED_MEMORY_FATAL( "Tried to acquire non-existing slab allocator. Probably slab allocator wasn't registered on current thread." );
		return NullBlock();
	}

	Block LocklessSlabAllocator::AllocateAligned( u32 size, u32 alignment )
	{
		SlabAllocator * allocator = AcquireLocalSlabAllocator();
		if( allocator )
		{
			return allocator->AllocateAligned( size, alignment );
		}

		ALWAYSENABLED_RED_MEMORY_FATAL( "Tried to acquire non-existing slab allocator. Probably slab allocator wasn't registered on current thread." );
		return NullBlock();
	}

	Block LocklessSlabAllocator::Reallocate( Block & block, u32 size )
	{
		SlabAllocator * allocator = AcquireLocalSlabAllocator();

		if( allocator )
		{
			return allocator->Reallocate( block, size );
		}

		ALWAYSENABLED_RED_MEMORY_FATAL( "Tried to acquire non-existing slab allocator. Probably slab allocator wasn't registered on current thread." );
		return NullBlock();
	}

	Block LocklessSlabAllocator::ReallocateAligned( Block & block, u32 size, u32 alignment )
	{
		return Reallocate( block, RoundUp( size, alignment ) );
	}

	void LocklessSlabAllocator::Free( Block & block )
	{
		const u32 id = m_threadIdProvider->GetCurrentId();
		SlabAllocator * allocator = GetSlabAllocator( id );
		if( allocator )
		{
			allocator->Free( block );
		}
		else
		{
			// If there are no allocator local to this thread, than we are 100% sure the memory block was not allocated by this thread.
			// Simply push to local freelist. Owner will process it when it needs to.
			SlabHeader* header = GetSlabHeader( block );
			block.size = header->blockSize;
#ifdef RED_MEMORY_ENABLE_EXTENDED_SLAB_ALLOCATOR_REPORT
			GetSlabAllocatorFromHeader( header )->AddFreedMemory( header->blockSize );
#endif
			PushBlockToFreeList( block.address );
		}
	}

	void LocklessSlabAllocator::RegisterCurrentThread( const AnsiChar* threadName )
	{
		const u32 id = m_threadIdProvider->GetCurrentId();

		if( std::find( m_threadIdDictionary.Begin(), m_threadIdDictionary.End(), id ) == m_threadIdDictionary.End() )
		{
			auto iter = RegisterThread( id, threadName );
			RED_MEMORY_ASSERT( iter != m_threadIdDictionary.End(), "No more space for Thread in LocklessSlabAllocator." );
			if( iter != m_threadIdDictionary.End() )
			{
				m_threadMonitor->MonitorCurrentThread();	
			}
		}	
	}

	bool LocklessSlabAllocator::IsCurrentThreadRegistered() const
	{
		const ThreadId id = m_threadIdProvider->GetCurrentId();
		return InternalIsThreadBoundToALocalSlabAllocator( id );
	}

	SlabAllocator * LocklessSlabAllocator::AcquireLocalSlabAllocator()
	{
		const u32 id = m_threadIdProvider->GetCurrentId();
		return GetSlabAllocator( id );
	}

	SlabAllocator * LocklessSlabAllocator::AcquireSlabAllocator( ThreadId id )
	{
		SlabAllocator * allocator = GetSlabAllocator( id );
		if( !allocator )
		{
			ThreadIdDictionary::iterator iter = RegisterThread( id, "Unknown Thread" );
			if( iter != m_threadIdDictionary.End() )
			{
				return &m_allocators[ std::distance( m_threadIdDictionary.Begin(), iter ) ];
			}
		}

		return allocator;
	}

	SlabAllocator * LocklessSlabAllocator::GetSlabAllocator( ThreadId id )
	{
		// TODO pretty fast if c_slabMaxThreadCount is less than 16 as the whole dictionary fit in one cache line.
		// However, might be better to go with a simple hash/modulo look up. Like this we can have more than 16.
		
		for( u32 index = 0; index != c_maxThreadCount; ++index )
		{
			if( m_threadIdDictionary[ index ] == id )
				return &m_allocators[ index ];
		}

		return nullptr;
	}

	LocklessSlabAllocator::ThreadIdDictionary::iterator LocklessSlabAllocator::RegisterThread( ThreadId id, const AnsiChar* threadName )
	{
		RED_MEMORY_ASSERT( threadName, "Given thread name is invalid" );

		for( ThreadIdDictionary::iterator iter = m_threadIdDictionary.Begin(), end = m_threadIdDictionary.End(); iter != end; ++iter )
		{
			if( atomic::CompareExchange32( reinterpret_cast< atomic::TAtomic32* >( &( *iter ) ), id, 0 ) == 0 )
			{
				AnsiChar* name = m_threadNameDictionary[ static_cast< u32 >( std::distance( m_threadIdDictionary.Begin(), iter ) ) ];
				red::Strcpy( name, threadName, c_maxThreadNameLength );
				return iter;
			}
		}

		return m_threadIdDictionary.End();
	}

	void LocklessSlabAllocator::NotifyOnThreadDied( ThreadId id, void * userData )
	{
		LocklessSlabAllocator * allocator = static_cast< LocklessSlabAllocator * >( userData );
		allocator->OnThreadDied( id ); 
	}

	void LocklessSlabAllocator::OnThreadDied( ThreadId id )
	{
		SlabAllocator * localAllocator = GetSlabAllocator( id );
		if( localAllocator )
		{
			localAllocator->ForceProcessFreeList();
			auto index = static_cast< u32 >( std::distance( m_allocators, localAllocator ) );
			m_threadIdDictionary[ index ] = 0;
		}
	}

	void LocklessSlabAllocator::BuildMetrics( LocklessSlabAllocatorMetrics & metrics )
	{
		m_slabChunkAllocator.BuildMetrics( metrics );
		metrics.metrics.smallestBlockSize = c_slabMinAllocSize;
		metrics.metrics.largestBlockSize = c_slabMaxAllocSize;

		u64 waste = 0;
		u64 systemMemory = 0;

		for( u32 index = 0; index != c_maxThreadCount; ++index )
		{
			auto & threadCacheInfo = metrics.threadCacheInfo[ index ];
			threadCacheInfo.threadId = m_threadIdDictionary[ index ];
			red::Strcpy( threadCacheInfo.threadName, m_threadNameDictionary[ index ], c_maxThreadNameLength );
			m_allocators[ index ].BuildMetrics( threadCacheInfo.slabAllocatorInfo );
			metrics.metrics.bookKeepingBytes += threadCacheInfo.slabAllocatorInfo.metrics.bookKeepingBytes;
			metrics.metrics.consumedMemoryBytes += threadCacheInfo.slabAllocatorInfo.metrics.consumedMemoryBytes;
			waste += ( threadCacheInfo.slabAllocatorInfo.metrics.consumedSystemMemoryBytes - threadCacheInfo.slabAllocatorInfo.metrics.consumedMemoryBytes );
			systemMemory += threadCacheInfo.slabAllocatorInfo.metrics.consumedSystemMemoryBytes;
		}

		metrics.waste = waste;
		metrics.wastePercent = ( waste / static_cast< double >( systemMemory ) ) * 100.0;
	}

	SlabAllocator * LocklessSlabAllocator::InternalAcquireLocalAllocator( ThreadId id )
	{
		return AcquireSlabAllocator( id );
	}

	void LocklessSlabAllocator::InternalRegisterThread( ThreadId id )
	{
		RegisterThread( id, "Unknown Thread" );
	}

	void LocklessSlabAllocator::InternalSetThreadIdProvider( const ThreadIdProvider * provider )
	{
		m_threadIdProvider = provider;
	}

	SlabChunk * LocklessSlabAllocator::InternalAllocateChunk()
	{
		return m_slabChunkAllocator.AllocateChunk( m_flags );
	}

	void LocklessSlabAllocator::InternalSetFreeChunk( ThreadId id, SlabChunk * chunk )
	{
		SlabAllocator * localAllocator = AcquireSlabAllocator( id );
		const u64 index = std::distance( m_allocators, localAllocator ); 
		m_slabChunkAllocator.ForceFree( static_cast< u32 >( index ), chunk );
	}

	void LocklessSlabAllocator::InternalMarkAllLocalSlabAllocatorAsTaken()
	{
		for( u32 index = 0; index != c_maxThreadCount; ++index )
		{
			if( m_threadIdDictionary[ index ] == 0 )
			{
				m_threadIdDictionary[ index ] = 0xcfcf;
			}
		}
	}

	bool LocklessSlabAllocator::InternalIsThreadBoundToALocalSlabAllocator( ThreadId id ) const
	{
		return std::find( m_threadIdDictionary.Begin(), m_threadIdDictionary.End(), id ) != m_threadIdDictionary.End();
	}

	u64 LocklessSlabAllocator::GetBlockSize( u64 address ) const
	{
		return GetSlabBlockSize( address );
	}

	LocklessSlabAllocator & AcquireLocklessSlabAllocator()
	{
		return AcquireVault().GetLocklessSlabAllocator();
	}
}
}
