/**
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_MEMORY_SERIALIZER_HPP_
#define _RED_MEMORY_SERIALIZER_HPP_

namespace red
{
namespace memory
{
	template< typename T >
	RED_MEMORY_INLINE Bool Serializer::Serialize( const T & object )
	{
		return Serializer::Serialize( &object, sizeof( T ) );
	}
}
}

#endif
