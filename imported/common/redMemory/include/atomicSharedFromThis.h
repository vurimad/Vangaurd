/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#ifndef _RED_MEMORY_ATOMIC_SHARED_FROM_THIS_H_
#define _RED_MEMORY_ATOMIC_SHARED_FROM_THIS_H_

#include "atomicSharedPtr.h"
#include "atomicWeakPtr.h"

namespace red
{
	template< typename T, typename PoolType = void >
	class EnableAtomicSharedFromThis
	{
	public:

		typedef T _AtomicSharedFromThisType;
		typedef PoolType _AtomicSharedFromThisPoolType;

		AtomicSharedPtr< T, PoolType > SharedFromThis() const;

	protected:

		EnableAtomicSharedFromThis();
		EnableAtomicSharedFromThis( const EnableAtomicSharedFromThis& );
		EnableAtomicSharedFromThis & operator=( const EnableAtomicSharedFromThis & );
		~EnableAtomicSharedFromThis();

	private:

		AtomicWeakPtr< T, PoolType > m_weakPtr;

		template< typename T1, typename PoolType1, typename T2, typename PoolType2 >
		friend void InternalDoEnableAtomicShared( EnableAtomicSharedFromThis< T1, PoolType1 >* enableSharedFromThis, SharedStorage< T2, internal::AtomicSharedStorage, PoolType2 > * sharedPtr );
	};
}

#include "atomicSharedFromThis.hpp"

#endif
