/**
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_MEMORY_INCLUDE_STACK_ALLOCATOR_H_
#define _RED_MEMORY_INCLUDE_STACK_ALLOCATOR_H_

#include "redMemoryInternal.h"
#include "defaultAllocator.h"

#include "../src/staticStackAllocator.h"
#include "../src/dynamicStackAllocator.h"

namespace red
{
namespace memory
{
	class StaticStackAllocator;
	class DynamicStackAllocator;
}
}

#endif