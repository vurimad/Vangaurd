/**
 * Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_MEMORY_ALLOCATOR_IDENTIFIERS_SERIALIZER_HPP_
#define _RED_MEMORY_ALLOCATOR_IDENTIFIERS_SERIALIZER_HPP_

#include "../../redSystem/include/assert.h"

namespace red
{
namespace memory
{
	template< typename AllocatorType >
	void SerializeAllocatorIdentifiers( AllocatorType * allocator, Serializer & serializer )
	{
		RED_FATAL_ASSERT( allocator != nullptr, "Given allocator does not exist." );
		RED_UNUSED( allocator );
		serializer.Serialize(allocator->TypeId);
		// data contains allocator specific metrics
		serializer.Serialize( static_cast< u8 >( 1 ) );
		serializer.Serialize( allocator->GetName(), 64 );
	}
}
}

#endif