/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/


#ifndef _RED_MEMORY_INCLUDE_LINEAR_ALLOCATOR_H_
#define _RED_MEMORY_INCLUDE_LINEAR_ALLOCATOR_H_

#include "redMemoryInternal.h"
#include "../src/locklessStaticLinearAllocator.h"
#include "../src/dynamicLinearAllocator.h"

namespace red
{
namespace memory
{
	class LocklessStaticLinearAllocator;
	class DynamicLinearAllocator;
}
}

#endif
