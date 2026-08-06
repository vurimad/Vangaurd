/**
 * Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "utils.h"
#include "defaultAllocator.h"
#include "vault.h"
#include "../include/utils.h"
#include "../include/memoryAnalyzerUtils.h"
#include "../include/block.h"

namespace red
{
namespace memory
{
	void RegisterCurrentThread( const AnsiChar* threadName )
	{
		AcquireDefaultAllocator().RegisterCurrentThread( threadName );

		RED_MEMORY_MA_SET_THREAD_INFO( threadName );
	}

	ThreadMonitor* GetThreadMonitor()
	{
		return AcquireVault().GetThreadMonitor();
	}

	void ProcessNextFrame()
	{
		ResetFrameAllocators();

		PrepareMetricsForNextFrame();

		RED_MEMORY_MA_MARK_NEW_FRAME();
	}

	void MemcpyBlock( u64 destination, u64 source, u64 size )
	{
		const void * sourceAddr = reinterpret_cast< const void* >( source );
		void * destinationAddr = reinterpret_cast< void* >( destination );
		Memcpy( destinationAddr, sourceAddr, size );
	}

	void MemcpyBlock( Block & destination, const Block & source )
	{
		const void * sourceAddr = reinterpret_cast< const void* >( source.address );
		void * destinationAddr = reinterpret_cast< void* >( destination.address );
		Memcpy( destinationAddr, sourceAddr, std::min( destination.size, source.size ) );
	}
}
}
