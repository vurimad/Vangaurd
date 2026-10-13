/*
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "jobScopeMemoryAllocator.h"
#include "../../redMemory/include/systemAllocator.h"
#include "../../redMemory/include/deserializer.h"
#include "../../redMemory/include/allocatorIdentifiersSerializer.h"

namespace job
{
namespace prv
{
namespace
{
	const Uint32 c_frameAllocatorSize = RED_KILO_BYTE( 256 );
	const Uint32 c_numberOfFrames = 1;
}

	JobScopeMemoryAllocator::JobScopeMemoryAllocator()
	{
		red::Memzero( &m_threadIdDictionary[0], m_threadIdDictionary.DataSize() );
	}

	JobScopeMemoryAllocator::~JobScopeMemoryAllocator()
	{
	}

	void JobScopeMemoryAllocator::Initialize( const JobScopeMemoryAllocatorParameter& parameter )
	{
		m_systemAllocator = parameter.systemAllocator;
		RED_FATAL_ASSERT( m_systemAllocator, "JobScopeMemoryAllocator need access to SystemAllocator." );
		m_threadMonitor = parameter.threadMonitor;
		RED_FATAL_ASSERT( m_threadMonitor, "JobScopeMemoryAllocator need access to ThreadMonitor." );
		m_threadMonitor->RegisterOnThreadDiedSignal( JobScopeMemoryAllocator::NotifyOnThreadDied, this );
	}

	void JobScopeMemoryAllocator::Uninitialize()
	{
		if ( m_systemAllocator && m_threadMonitor )
		{
			m_threadMonitor->UnregisterFromThreadDiedSignal( JobScopeMemoryAllocator::NotifyOnThreadDied, this );
			m_threadMonitor->Uninitialize();

			for ( auto& allocator : m_allocators )
			{
				allocator.Uninitialize();
			}

			m_systemAllocator = nullptr;
		}
	}

	red::memory::Block JobScopeMemoryAllocator::Allocate( Uint32 size )
	{
		red::memory::FrameAllocatorWithFallback* allocator = AcquireLocalFrameAllocator();
		if ( allocator )
		{
			return allocator->Allocate( size );
		}

		return red::memory::NullBlock();
	}

	red::memory::Block JobScopeMemoryAllocator::AllocateAligned( Uint32 size, Uint32 alignment )
	{
		red::memory::FrameAllocatorWithFallback* allocator = AcquireLocalFrameAllocator();
		if ( allocator )
		{
			return allocator->AllocateAligned( size, alignment );
		}

		return red::memory::NullBlock();
	}

	red::memory::Block JobScopeMemoryAllocator::Reallocate( red::memory::Block& block, Uint32 size )
	{
		red::memory::FrameAllocatorWithFallback* allocator = AcquireLocalFrameAllocator();
		if ( allocator )
		{
			return allocator->Reallocate( block, size );
		}

		return red::memory::NullBlock();
	}

	red::memory::Block JobScopeMemoryAllocator::ReallocateAligned( red::memory::Block& block, Uint32 size, Uint32 alignment )
	{
		red::memory::FrameAllocatorWithFallback* allocator = AcquireLocalFrameAllocator();
		if ( allocator )
		{
			return allocator->ReallocateAligned( block, size, alignment );
		}

		return red::memory::NullBlock();
	}

	void JobScopeMemoryAllocator::Free( red::memory::Block& block )
	{
		red::memory::FrameAllocatorWithFallback* allocator = AcquireLocalFrameAllocator();
		if( allocator )
		{
			return allocator->Free( block );
		}
	}

	void JobScopeMemoryAllocator::Reset()
	{
		red::memory::FrameAllocatorWithFallback* allocator = AcquireLocalFrameAllocator();
		if ( allocator )
		{
			return allocator->Reset();
		}
	}

	bool JobScopeMemoryAllocator::OwnBlock( Uint64 block ) const
	{
		const red::memory::FrameAllocatorWithFallback* allocator = AcquireLocalFrameAllocator();
		if ( allocator )
		{
			return allocator->OwnBlock( block );
		}

		return false;
	}

	Uint64 JobScopeMemoryAllocator::GetBlockSize( Uint64 address ) const
	{
		const red::memory::FrameAllocatorWithFallback* allocator = AcquireLocalFrameAllocator();
		RED_MEMORY_ASSERT( allocator, "Tried to acquire non-existing job scope allocator.Probably job scope allocator wasn't registered on current thread." );
		if ( allocator )
		{
			return allocator->GetBlockSize( address );
		}

		return 0;
	}

	void JobScopeMemoryAllocator::RegisterCurrentThread()
	{
		const ThreadId id = m_threadIdProvider.GetCurrentId();

		if ( std::find( m_threadIdDictionary.Begin(), m_threadIdDictionary.End(), id ) == m_threadIdDictionary.End() )
		{
			auto it = RegisterThread( id );
			RED_FATAL_ASSERT( it != m_threadIdDictionary.End(), "No more space for thread in JobScopeMemoryAllocator" );
			if ( it != m_threadIdDictionary.End() )
			{
				auto index = std::distance( m_threadIdDictionary.Begin(), it );
				red::memory::FrameAllocatorWithFallback& allocator = m_allocators[ index ];
				red::memory::FrameAllocatorParameter param =
				{
					m_systemAllocator,
					c_frameAllocatorSize,
					c_numberOfFrames,
					red::memory::Flags_CPU_Read_Write | red::memory::Flags_No_Coalesce_Blocks
				};

				allocator.Initialize( param );

				m_threadMonitor->MonitorCurrentThread();
			}
		}
	}

	void JobScopeMemoryAllocator::BuildMetrics( JobScopeMemoryAllocatorMetrics& metrics )
	{
		red::Memzero( &metrics, sizeof( metrics ) );

		for( Uint32 index = 0; index != c_maxThreadCount; ++index )
		{
			auto & threadCacheInfo = metrics.threadCacheInfo[ index ];
			threadCacheInfo.threadId = m_threadIdDictionary[ index ];
			m_allocators[ index ].BuildMetrics( threadCacheInfo.frameAllocatorInfo );
			metrics.metrics.consumedSystemMemoryBytes += threadCacheInfo.frameAllocatorInfo.metrics.metrics.consumedSystemMemoryBytes;
			metrics.metrics.consumedMemoryBytes += threadCacheInfo.frameAllocatorInfo.metrics.metrics.consumedMemoryBytes;
		}

		metrics.waste = metrics.metrics.consumedSystemMemoryBytes - metrics.metrics.consumedMemoryBytes;
		metrics.wastePercent = ( metrics.waste / static_cast< double >( metrics.metrics.consumedSystemMemoryBytes ) ) * 100.0;
	}

	void JobScopeMemoryAllocator::SerializeMetrics( red::memory::Serializer& serializer )
	{
		JobScopeMemoryAllocatorMetrics metrics;
		BuildMetrics( metrics );

		SerializeAllocatorIdentifiers( this, serializer );

		serializer.Serialize( static_cast< Uint32 >( sizeof( metrics ) ) );
		serializer.Serialize( &metrics, sizeof( metrics ) );
	}

	Uint64 JobScopeMemoryAllocator::Debug_GetOutOfBudgetBytes() const
	{
		const red::memory::FrameAllocatorWithFallback* allocator = AcquireLocalFrameAllocator();
		RED_MEMORY_ASSERT( allocator, "Tried to acquire non-existing job scope allocator.Probably job scope allocator wasn't registered on current thread." );
		if ( allocator )
		{
			return allocator->Debug_GetOutOfBudgetBytes();
		}

		return 0;
	}

	red::memory::FrameAllocatorWithFallback* JobScopeMemoryAllocator::AcquireLocalFrameAllocator()
	{
		const ThreadId id = m_threadIdProvider.GetCurrentId();
		return GetFrameAllocator( id );
	}

	const red::memory::FrameAllocatorWithFallback* JobScopeMemoryAllocator::AcquireLocalFrameAllocator() const
	{
		const ThreadId id = m_threadIdProvider.GetCurrentId();
		return GetFrameAllocator( id );
	}

	red::memory::FrameAllocatorWithFallback* JobScopeMemoryAllocator::GetFrameAllocator( ThreadId id )
	{
		for ( Uint32 index = 0; index < c_maxThreadCount; ++index )
		{
			if ( m_threadIdDictionary[ index ] == id )
			{
				return &m_allocators[ index ];
			}
		}

		ALWAYSENABLED_RED_MEMORY_FATAL( "Tried to acquire non-existing job scope allocator. Probably job scope allocator wasn't registered on current thread." );
		return nullptr;
	}

	const red::memory::FrameAllocatorWithFallback* JobScopeMemoryAllocator::GetFrameAllocator( ThreadId id ) const
	{
		for ( Uint32 index = 0; index < c_maxThreadCount; ++index )
		{
			if ( m_threadIdDictionary[ index ] == id )
			{
				return &m_allocators[ index ];
			}
		}

		return nullptr;
	}

	JobScopeMemoryAllocator::ThreadIdDictionary::iterator JobScopeMemoryAllocator::RegisterThread( ThreadId id )
	{
		for ( ThreadIdDictionary::iterator it = m_threadIdDictionary.Begin(), end = m_threadIdDictionary.End(); it != end; ++it )
		{
			if ( atomic::CompareExchange32( reinterpret_cast< atomic::TAtomic32* >( &( *it ) ), id, 0 ) == 0 )
			{
				return it;
			}
		}

		return m_threadIdDictionary.End();
	}

	void JobScopeMemoryAllocator::OnThreadDied( ThreadId id )
	{
		red::memory::FrameAllocatorWithFallback* localAllocator = GetFrameAllocator( id );
		if ( localAllocator )
		{
			localAllocator->Uninitialize();
			auto index = std::distance( m_allocators, localAllocator );
			m_threadIdDictionary[ static_cast< Uint32 >( index ) ] = 0;
		}
	}

	void JobScopeMemoryAllocator::NotifyOnThreadDied( ThreadId id, void * userData )
	{
		JobScopeMemoryAllocator* allocator = static_cast< JobScopeMemoryAllocator* >( userData );
		allocator->OnThreadDied( id ); 
	}

	void LogJobScopeMemoryAllocatorMetrics( red::memory::ProxyTypeId allocatorId, red::memory::Deserializer& deserializer )
	{
		Uint32 metricsSize{ 0 };
		deserializer.Deserialize( metricsSize );
		RED_FATAL_ASSERT( metricsSize == sizeof( JobScopeMemoryAllocatorMetrics ), "Allocator specific metrics is bigger then expected." );

		JobScopeMemoryAllocatorMetrics metrics;
		red::Memzero( &metrics, sizeof( JobScopeMemoryAllocatorMetrics ) );
		Uint32 dataRead{ 0 };
		deserializer.Deserialize( &metrics, metricsSize, dataRead );
		RED_FATAL_ASSERT( dataRead == metricsSize, "Inconsistent memory stream data." );

		RED_LOG( "Memory: \tJob Scope Memory Allocator Information:" );

		RED_LOG( "Memory: \t\tSystem Memory Consumed: %" PRIu64, metrics.metrics.consumedSystemMemoryBytes );
		RED_LOG( "Memory: \t\tMemory Consumed: %" PRIu64, metrics.metrics.consumedMemoryBytes );
		RED_LOG( "Memory: \t\tMemory Waste: %" PRIu64, metrics.waste );
		RED_LOG( "Memory: \t\tMemory Waste Percent: %.2f", metrics.wastePercent );

		RED_LOG( "Memory: +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++" );

		const char * headerPattern = "Memory: | %10s | %10s | %10s | %10s | %8s |";
		const char * rowPattern = "Memory: | %10" PRIu32 " | %10" PRIu64 " | %10" PRIu64 " | %10" PRIu64 " | %10" PRIu64 " | %8.2f |";

		RED_LOG( "Memory: Thread Cache Informations" );
		RED_LOG( "Memory: ================================================================" );
		RED_LOG( headerPattern, "Thread ID", "Used", "Reserved", "OOB", "Waste", "% Waste" );
		RED_LOG( "Memory: ================================================================" );

		const auto & threadsMetric = metrics.threadCacheInfo;

		for ( Uint32 index = 0; index != c_maxThreadCount; ++index )
		{
			const auto & threadMetric = threadsMetric[ index ];
			if ( threadMetric.threadId )
			{
				const auto & localInfo = threadMetric.frameAllocatorInfo;
				const Uint64 used = localInfo.metrics.metrics.consumedMemoryBytes;
				const Uint64 reserved = localInfo.metrics.metrics.consumedSystemMemoryBytes;
				const Uint64 waste = reserved - used;
				const double percentWaste = reserved ? ( waste / static_cast< double >( reserved ) ) * 100.0 : 0.0;

				RED_LOG(
					rowPattern,
					threadMetric.threadId,
					used,
					reserved,
					localInfo.outOfBudgetUsedBytes,
					waste,
					percentWaste );
			}
		}

		RED_LOG( "Memory: ================================================================" );

		RED_LOG( "Memory: -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-" );

		RED_LOG_FLUSH_AND_WAIT();
	}
}
}