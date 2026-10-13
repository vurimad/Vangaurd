/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#ifndef _RED_MEMORY_PAGE_SYSTEM_ALLOCATOR_TYPE_H_
#define _RED_MEMORY_PAGE_SYSTEM_ALLOCATOR_TYPE_H_

#if defined( RED_PLATFORM_WINPC )
#include "systemPageAllocatorWin.h"
#elif defined( RED_PLATFORM_DURANGO )
#include "systemPageAllocatorDurango.h"
#elif defined( RED_PLATFORM_ORBIS )
#include "systemPageAllocatorOrbis.h"
#elif defined( RED_PLATFORM_LINUX )
#include "systemPageAllocatorLinux.h"
#endif

namespace red
{
namespace memory
{
#if defined( RED_PLATFORM_WINPC )
	typedef SystemPageAllocatorWin PlatformSystemPageAllocator;
#elif defined( RED_PLATFORM_DURANGO )
	typedef SystemPageAllocatorDurango PlatformSystemPageAllocator;
#elif defined( RED_PLATFORM_ORBIS )
	typedef SystemPageAllocatorOrbis PlatformSystemPageAllocator;
#elif defined( RED_PLATFORM_LINUX )
	typedef SystemPageAllocatorLinux PlatformSystemPageAllocator;
#endif

	RED_MEMORY_API SystemPageAllocator & AcquireSystemPageAllocator();
}
}

#endif
