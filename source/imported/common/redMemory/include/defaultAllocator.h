/**
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_MEMORY_INCLUDE_DEFAULT_ALLOCATOR_H_
#define _RED_MEMORY_INCLUDE_DEFAULT_ALLOCATOR_H_

#include "redMemoryInternal.h"
#include "flags.h"
#include "../src/defaultAllocator.h"
#include "../src/nullAllocator.h"
#ifdef RED_PLATFORM_CONSOLE
#include "../src/consoleDebugAllocator.h"
#endif

namespace red
{
namespace memory
{
	class SystemAllocator;

	class DefaultAllocator;
	class NullAllocator;
	
	RED_MEMORY_API DefaultAllocator & AcquireDefaultAllocator();
	RED_MEMORY_API NullAllocator & AcquireNullAllocator();

	RED_MEMORY_API SystemAllocator & AcquireSystemAllocator();
	RED_MEMORY_API SystemAllocator & AcquireFlexibleSystemAllocator();
}
}

#endif

