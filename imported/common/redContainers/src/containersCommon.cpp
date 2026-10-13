/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/


#include "build.h"
#include "containersCommon.h"

namespace red
{
	void InitializeContainerMemoryPools()
	{
		RED_INITIALIZE_MEMORY_POOL( PoolString, red::PoolDebug, red::memory::AcquireDefaultAllocator(), RED_MEGA_BYTE( 64 ) );	
	}
}

namespace red
{
namespace memory
{

#ifdef RED_COMPILER_MSC
	RED_DISABLE_WARNING_MSC( 4073 )
	#pragma init_seg( lib )
#endif

	PoolStorage StaticPoolStorage< red::PoolString >::storage = 
	{
		MakeAllocatorStorage( &red::memory::AcquireDefaultAllocator(), PoolString::GetHandle() ),
		0,
		0,
		nullptr,
		PoolString::GetHandle(),
		PoolString::AllocatorType::TypeId
	};
}
}
