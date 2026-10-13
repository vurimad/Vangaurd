/**
 * Copyright (c) 2020 CD Projekt Red. All Rights Reserved.
 */

#include "../../redMemory/include/block.h"
#include "../../redMemory/include/types.h"
#include "../../redMemory/include/threadMonitor.h"

#if defined( RED_PLATFORM_DURANGO ) || defined( RED_PLATFORM_WIN64 ) || defined( RED_PLATFORM_WIN32 )
#define TELEMETRY_USE_MUTEXT_TSLF
#endif

#ifdef TELEMETRY_USE_MUTEXT_TSLF
	#include "../src/lockingMutexDynamicTlsfAllocator.h"
#endif


#ifndef _RED_MEMORY_TELEMETRY_ALLOCATOR_H_
#define _RED_MEMORY_TELEMETRY_ALLOCATOR_H_

namespace red
{
	namespace memory
	{
		class SystemAllocator;
		class DefaultAllocator;
	}
}

namespace red
{
	struct TelemetryAllocatorMetrics
	{
		red::memory::AllocatorMetrics metrics;
		red::memory::TLSFAllocatorMetrics tlsfAllocatorMetrics;
	};

	struct TelemetryAllocatorParameter
	{
		red::memory::SystemAllocator * systemAllocator;
		red::memory::DefaultAllocator * defaultAllocator;
	};

	constexpr red::memory::u32 c_maxTelemetryThreads = 16;

	class RED_CORE_API TelemetryAllocator
	{
	public:
		RED_MEMORY_DECLARE_ALLOCATOR( TelemetryAllocator, TelemetryAllocatorMetrics, 8 );

		TelemetryAllocator();
		~TelemetryAllocator();

		void Initialize( const TelemetryAllocatorParameter & parameter );
		void Uninitialize();

		red::memory::Block Allocate( red::memory::u32 size );
		red::memory::Block AllocateAligned( red::memory::u32 size, red::memory::u32 alignment );
		red::memory::Block Reallocate( red::memory::Block & block, red::memory::u32 size );
		red::memory::Block ReallocateAligned( red::memory::Block & block, red::memory::u32 size, red::memory::u32 alignment );
		void Free( red::memory::Block & block );

		Bool OwnBlock( red::memory::u64 block ) const;
		red::memory::u64 GetBlockSize( red::memory::u64 address ) const;

		void RegisterCurrentThreadAsTelemetry();

	private:
		TelemetryAllocator( const TelemetryAllocator & );
		TelemetryAllocator & operator=( const TelemetryAllocator& );

		void OnThreadDied( red::memory::ThreadId id );
		static void NotifyOnThreadDied( red::memory::ThreadId id, void * userData );

		Bool IsInitialized() const;
		
		red::memory::Block ReallocateFromTLSFAllocator( red::memory::Block & block, red::memory::u32 size );
		red::memory::Block ReallocateFromTLSFAllocator( red::memory::Block & block, red::memory::u32 size, red::memory::u32 alignment );

		Bool IsInTelemetryThread() const;

#ifdef TELEMETRY_USE_MUTEXT_TSLF
		RED_ALIGN( 64 ) LockingMutexDynamicTLSFAllocator m_tlsfAllocator;
#else
		RED_ALIGN( 64 ) red::memory::LockingDynamicTLSFAllocator m_tlsfAllocator;
#endif

		red::memory::SimpleArray<red::memory::u32, c_maxTelemetryThreads> m_telemetryThreadIdList;

		red::memory::DefaultAllocator * m_defaultAllocator;
		red::memory::SystemAllocator * m_systemAllocator;

		const red::memory::ThreadIdProvider * m_threadIdProvider;
		red::memory::ThreadIdProvider m_threadIdProviderStorage;

		red::memory::ThreadMonitor* m_threadMonitor;
	};

	RED_CORE_API TelemetryAllocator & AcquireTelemetryAllocator();
	void InitializeAllocators();
}

#endif
