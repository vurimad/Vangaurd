/**
* Copyright (c) 2014 CD Projekt Red. All Rights Reserved.
*/

#ifndef _RED_MEMORY_NON_ATOMIC_SHARED_PTR_HPP_
#define _RED_MEMORY_NON_ATOMIC_SHARED_PTR_HPP_

namespace red
{
	template< typename T, typename PoolType >
	class EnableNonAtomicSharedFromThis;

	template< typename T1, typename PoolType1, typename T2, typename PoolType2 >
	void InternalDoEnableNonAtomicShared( EnableNonAtomicSharedFromThis< T1, PoolType1 >* enableSharedFromThis,  SharedStorage< T1, internal::NonAtomicSharedStorage, PoolType2 >* sharedPtr );

	template< typename T, typename U, typename PoolTypeU >
	RED_MEMORY_INLINE void InternalEnableShared( T* ptr, SharedStorage< U, internal::NonAtomicSharedStorage, PoolTypeU > * sharedPtr, typename T::_NonAtomicSharedFromThisType* = nullptr )
	{
		if( ptr )
		{
			InternalDoEnableNonAtomicShared( static_cast< EnableNonAtomicSharedFromThis< typename T::_NonAtomicSharedFromThisType, typename T::_NonAtomicSharedFromThisPoolType >* >( ptr ), sharedPtr );
		}
	}

	template< typename T, typename... Args >
	RED_MEMORY_INLINE NonAtomicSharedPtr< T > CreateNonAtomicSharedPtr( Args && ... args )
	{
		NonAtomicSharedPtr< T > ptr( ( RED_NEW( T )( std::forward< Args >( args )...) ) );
		return ptr;
	}

	template< typename T, typename PoolType, typename... Args >
	NonAtomicSharedPtr< T, PoolType > CreateNonAtomicSharedPtr( Args && ... args )
	{
		NonAtomicSharedPtr< T, PoolType > ptr( ( RED_NEW( T, PoolType )( std::forward< Args >( args )...) ) );
		return ptr;
	}
}

#endif 
