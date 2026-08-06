/**
 * Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_MEMORY_POOL_TYPES_H_
#define _RED_MEMORY_POOL_TYPES_H_

#include "types.h"
#include "../../redSystem/include/hash.h"

namespace red
{
namespace memory
{
	typedef THash32 PoolHandle;

	struct PoolStorage;

	struct PoolParameter
	{
		const char * name;
		PoolStorage * storage;
		u64 budget;
		PoolHandle parentHandle;
	};
}
}

#endif
