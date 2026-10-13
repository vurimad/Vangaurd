/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#ifndef _RED_MEMORY_FRAME_ALLOCATOR_PUBLIC_H_
#define _RED_MEMORY_FRAME_ALLOCATOR_PUBLIC_H_

#include "redMemoryInternal.h"

#include "../src/frameAllocator.h"
#include "../src/locklessFrameAllocator.h"
#include "../src/frameAllocatorWithFallback.h"

#ifndef RED_CONFIGURATION_FINAL
#include "../src/locklessDebugBanFrameAllocator.h"
#endif

namespace red
{
namespace memory
{
	class FrameAllocator;
	class LocklessFrameAllocator;

#ifndef RED_CONFIGURATION_FINAL
	class LocklessDebugBanFrameAllocator;
#endif

	class FrameAllocatorWithFallback;
}
}

#endif
