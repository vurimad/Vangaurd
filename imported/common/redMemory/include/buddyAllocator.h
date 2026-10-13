/**
 * Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_MEMORY_INCLUDE_BUDDY_ALLOCATOR_H_
#define _RED_MEMORY_INCLUDE_BUDDY_ALLOCATOR_H_

#include "../src/buddyAllocator.h"
#include "../src/staticBuddyAllocator.h"
#include "../src/dynamicBuddyAllocator.h"
#include "../src/lockingDynamicBuddyAllocator.h"

namespace red
{
namespace memory
{
	class BuddyAllocator;
	class StaticBuddyAllocator;
	class DynamicBuddyAllocator;
	class LockingDynamicBuddyAllocator;
}
}

#endif