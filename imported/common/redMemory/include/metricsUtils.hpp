/**
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_MEMORY_METRICS_UTILS_HPP_
#define _RED_MEMORY_METRICS_UTILS_HPP_

#include "pool.h"
#include "redMemoryApi.h"

namespace red
{
namespace memory
{
	template< typename T >
	RED_MEMORY_INLINE u64 GetTotalBytesAllocated()
	{
		static_assert( std::is_base_of< Pool, T >::value, "Utility function can only be use for Pools." );
		return GetTotalBytesAllocated( T::GetHandle() );
	}
}
}

#endif
