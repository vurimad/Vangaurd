/**
* Copyright (c)2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../redSystem/include/redSystemPublic.h"
#include "../../redMemory/include/redMemoryPublic.h"
#include "../../redMemory/include/uniquePtr.h"
#include "../../redContainers/include/redContainersPublic.h"
#include "../../redCompression/include/redCompressionPublic.h"
#include "../../redCore/include/redCorePublic.h"
#include "../../redIO/include/redIOPublic.h"
#include "../include/redFileSystemPublic.h"

#include "system.h"

RED_MEMORY_POOL_STATIC( PoolFileSyncService, red::memory::DefaultAllocator );