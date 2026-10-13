/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#ifndef _RED_MEMORY_INCLUDE_FIXED_SIZE_ALLOCATOR_H_
#define _RED_MEMORY_INCLUDE_FIXED_SIZE_ALLOCATOR_H_

#include "redMemoryInternal.h"
#include "defaultAllocator.h"

#include "../src/locklessStaticFixedSizeAllocator.h"
#include "../src/dynamicFixedSizeAllocator.h"

namespace red
{
namespace memory
{
	class LocklessStaticFixedSizeAllocator;
	class DynamicFixedSizeAllocator;
}
}

#endif
