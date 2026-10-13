/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#ifndef _RED_MEMORY_ATOMIC_SHARED_FROM_THIS_HPP_
#define _RED_MEMORY_ATOMIC_SHARED_FROM_THIS_HPP_

namespace red
{
	template< typename T, typename PoolType >
	RED_MEMORY_INLINE EnableAtomicSharedFromThis< T, PoolType >::EnableAtomicSharedFromThis()
	{}

	template< typename T, typename PoolType >
	RED_MEMORY_INLINE EnableAtomicSharedFromThis< T, PoolType >::EnableAtomicSharedFromThis( const EnableAtomicSharedFromThis & )
	{}

	template< typename T, typename PoolType >
	RED_MEMORY_INLINE EnableAtomicSharedFromThis< T, PoolType > & EnableAtomicSharedFromThis< T, PoolType >::operator=( const EnableAtomicSharedFromThis & )
	{
		return *this;
	}

	template< typename T, typename PoolType >
	RED_MEMORY_INLINE EnableAtomicSharedFromThis< T, PoolType >::~EnableAtomicSharedFromThis()
	{}

	template< typename T, typename PoolType >
	RED_MEMORY_INLINE AtomicSharedPtr< T, PoolType > EnableAtomicSharedFromThis< T, PoolType >::SharedFromThis() const
	{
		return AtomicSharedPtr< T, PoolType >( m_weakPtr );
	}

	template< typename T1, typename PoolType1, typename T2, typename PoolType2 >
	void InternalDoEnableAtomicShared( EnableAtomicSharedFromThis< T1, PoolType1 >* enableSharedFromThis, SharedStorage< T2, internal::AtomicSharedStorage, PoolType2 > * sharedPtr )
	{
		// It is required to create (Atomic)SharedPtr with exactly the same pool as you used in Enable(Atomic)SharedFromThis.
		// Make sure that you use the same pool in both definition (or don't specify any pool in both).
		enableSharedFromThis->m_weakPtr = *static_cast< AtomicSharedPtr< T2, PoolType2 >* >( sharedPtr );
	}
}

#endif
