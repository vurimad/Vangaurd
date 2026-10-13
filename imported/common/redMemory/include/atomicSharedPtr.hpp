/**
* Copyright (c) 2014 CD Projekt Red. All Rights Reserved.
*/

#ifndef _RED_MEMORY_ATOMIC_SHARED_PTR_HPP_
#define _RED_MEMORY_ATOMIC_SHARED_PTR_HPP_

namespace red
{
	template< typename T, typename PoolType >
	class EnableAtomicSharedFromThis;

	template< typename T1, typename PoolType1, typename T2, typename PoolType2 >
	void InternalDoEnableAtomicShared( EnableAtomicSharedFromThis< T1, PoolType1  >* enableSharedFromThis, SharedStorage< T2, internal::AtomicSharedStorage, PoolType2 >* sharedPtr );

	template< typename T, typename U, typename PoolTypeU >
	RED_MEMORY_INLINE void InternalEnableShared( T* ptr, SharedStorage< U, internal::AtomicSharedStorage, PoolTypeU > * sharedPtr, typename T::_AtomicSharedFromThisType* = nullptr )
	{
		if( ptr )
		{
			InternalDoEnableAtomicShared( static_cast< EnableAtomicSharedFromThis< typename T::_AtomicSharedFromThisType, typename T::_AtomicSharedFromThisPoolType >* >( ptr ), sharedPtr );
		}
	}

	template< typename T, typename... Args >
	RED_MEMORY_INLINE AtomicSharedPtr< T > CreateAtomicSharedPtr( Args && ... args )
	{
		AtomicSharedPtr< T > ptr( ( RED_NEW( T )( std::forward< Args >( args )...) ) );
		return ptr;
	}

	template< typename T, typename PoolType, typename... Args >
	AtomicSharedPtr< T, PoolType > CreateAtomicSharedPtr( Args && ... args )
	{
		AtomicSharedPtr< T, PoolType > ptr( ( RED_NEW( T, PoolType )( std::forward< Args >( args )...) ) );
		return ptr;
	}
}

#endif
