/**
 * Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_MEMORY_ALLOCATOR_IDENTIFIERS_SERIALIZER_H_
#define _RED_MEMORY_ALLOCATOR_IDENTIFIERS_SERIALIZER_H_

namespace red
{
namespace memory
{
	class Serializer;

	template< typename AllocatorType >
	void SerializeAllocatorIdentifiers( AllocatorType * allocator, Serializer & serializer );
}
}

#include "allocatorIdentifiersSerializer.hpp"

#endif