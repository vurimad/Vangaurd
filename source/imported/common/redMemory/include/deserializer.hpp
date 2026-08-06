/**
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_MEMORY_DESERIALIZER_HPP_
#define _RED_MEMORY_DESERIALIZER_HPP_

namespace red
{
namespace memory
{
	template< typename T >
	RED_MEMORY_INLINE void Deserializer::Deserialize( T & object )
	{
		u32 readSize = 0;
		Deserializer::Deserialize( &object, sizeof( T ), readSize );
		RED_MEMORY_ASSERT( readSize == sizeof( T ), "Inconsistent data format." );
	}
}
}

#endif