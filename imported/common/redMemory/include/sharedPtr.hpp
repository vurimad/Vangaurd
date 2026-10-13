/**
* Copyright (c) 2014 CD Projekt Red. All Rights Reserved.
*/

#ifndef _RED_MEMORY_SHARED_PTR_HPP_
#define _RED_MEMORY_SHARED_PTR_HPP_

namespace red
{
	template< typename T, typename... Args >
	RED_MEMORY_INLINE SharedPtr< T > CreateSharedPtr( Args && ... args )
	{
		SharedPtr< T > ptr( ( RED_NEW( T )( std::forward< Args >( args )...)  ) );
		return ptr;
	}

	template< typename T, typename PoolType, typename... Args >
	RED_MEMORY_INLINE SharedPtr< T, PoolType > CreateSharedPtr( Args && ... args )
	{
		SharedPtr< T, PoolType > ptr( ( RED_NEW( T, PoolType )( std::forward< Args >( args )...)  ) );
		return ptr;
	}
}

#endif 
