/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "defaultAllocator.h"
#include "pool.h"

namespace game
{
	RED_MEMORY_POOL( PoolGMPL, red::memory::NullAllocator, RED_MEMORY_API );

	RED_MEMORY_POOL( PoolGMPL_Abstract, red::memory::NullAllocator, RED_MEMORY_API );
}

namespace AI
{
	RED_MEMORY_POOL( PoolAI, red::memory::DefaultAllocator, RED_MEMORY_API );
}

namespace red
{
namespace memory
{
	RED_MEMORY_POOL( PoolUI, red::memory::NullAllocator, RED_MEMORY_API );
}
}

RED_MEMORY_DECLARE_POOL_STORAGE( game::PoolGMPL, RED_MEMORY_API );
RED_MEMORY_DECLARE_POOL_STORAGE( game::PoolGMPL_Abstract, RED_MEMORY_API );
RED_MEMORY_DECLARE_POOL_STORAGE( AI::PoolAI, RED_MEMORY_API );
RED_MEMORY_DECLARE_POOL_STORAGE( red::memory::PoolUI, RED_MEMORY_API );