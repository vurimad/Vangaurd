/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#ifndef _RED_MEMORY_NON_ATOMIC_SHARED_FROM_THIS_HPP_
#define _RED_MEMORY_NON_ATOMIC_SHARED_FROM_THIS_HPP_

namespace red
{
	template< typename T, typename PoolType >
	RED_MEMORY_INLINE EnableNonAtomicSharedFromThis< T, PoolType >::EnableNonAtomicSharedFromThis()
	{}

	template< typename T, typename PoolType >
	RED_MEMORY_INLINE EnableNonAtomicSharedFromThis< T, PoolType >::EnableNonAtomicSharedFromThis( const EnableNonAtomicSharedFromThis & )
	{}

	template< typename T, typename PoolType >
	RED_MEMORY_INLINE EnableNonAtomicSharedFromThis< T, PoolType > & EnableNonAtomicSharedFromThis< T, PoolType >::operator=( const EnableNonAtomicSharedFromThis & )
	{
		return *this;
	}
	
	template< typename T, typename PoolType >
	RED_MEMORY_INLINE EnableNonAtomicSharedFromThis< T, PoolType >::~EnableNonAtomicSharedFromThis()
	{}

	template< typename T, typename PoolType >
	RED_MEMORY_INLINE NonAtomicSharedPtr< T, PoolType > EnableNonAtomicSharedFromThis< T, PoolType >::SharedFromThis() const
	{
		return NonAtomicSharedPtr< T, PoolType >( m_weakPtr );
	}

	template< typename T1, typename PoolType1, typename T2, typename PoolType2 >
	void InternalDoEnableNonAtomicShared( EnableNonAtomicSharedFromThis< T1, PoolType1 >* enableSharedFromThis, SharedStorage< T2, internal::NonAtomicSharedStorage, PoolType2 >* sharedPtr )
	{
		// It is required to create NonAtomicSharedPtr with exactly the same pool as you used in EnableNonAtomicSharedFromThis.
		// Make sure that you use the same pool in both definition (or don't specify any pool in both).
		enableSharedFromThis->m_weakPtr = *static_cast< NonAtomicSharedPtr< T2, PoolType2 >* >( sharedPtr );
	}
}

#endif
