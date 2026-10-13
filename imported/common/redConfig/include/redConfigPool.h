/**
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "redConfigApi.h"

namespace InGameConfig
{
	RED_MEMORY_POOL( PoolInGameConfig, red::memory::DefaultAllocator, RED_CONFIG_API );
	RED_MEMORY_POOL( PoolInGameConfigResource, red::memory::DefaultAllocator, RED_CONFIG_API );

	RED_CONFIG_API void InitializeMemoryPools();
}