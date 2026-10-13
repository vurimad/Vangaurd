/**
 * Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_MEMORY_PROXY_TYPE_ID_H_
#define _RED_MEMORY_PROXY_TYPE_ID_H_

#include "../../redSystem/include/hash.h"

namespace red
{
namespace memory
{
	typedef u32 ProxyTypeId;

	template< typename Allocator, typename >
	struct ProxyHasTypeId;

#define RED_MEMORY_PROXY_TYPE_ID( name ) enum TypeIdEnum : red::memory::ProxyTypeId { TypeId = red::CalculateHash32( #name ) }

}
}

#include "proxyTypeId.hpp"

#endif
