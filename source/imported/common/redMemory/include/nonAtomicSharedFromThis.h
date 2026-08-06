/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#ifndef _RED_MEMORY_NON_ATOMIC_SHARED_FROM_THIS_H_
#define _RED_MEMORY_NON_ATOMIC_SHARED_FROM_THIS_H_

#include "nonAtomicSharedPtr.h"
#include "nonAtomicWeakPtr.h"

namespace red
{
	template< typename T, typename PoolType = void >
	class EnableNonAtomicSharedFromThis
	{
	public:

		typedef T _NonAtomicSharedFromThisType;
		typedef PoolType _NonAtomicSharedFromThisPoolType;

		NonAtomicSharedPtr< T, PoolType > SharedFromThis() const;

	protected:

		EnableNonAtomicSharedFromThis();
		EnableNonAtomicSharedFromThis( const EnableNonAtomicSharedFromThis& );
		EnableNonAtomicSharedFromThis & operator=( const EnableNonAtomicSharedFromThis & );
		~EnableNonAtomicSharedFromThis();

	private:

		NonAtomicWeakPtr< T, PoolType > m_weakPtr;

		template< typename T1, typename PoolType1, typename T2, typename PoolType2 >
		friend void InternalDoEnableNonAtomicShared( EnableNonAtomicSharedFromThis< T1, PoolType1 >* enableSharedFromThis, SharedStorage< T2, internal::NonAtomicSharedStorage, PoolType2 > * sharedPtr );
	};
}

#include "nonAtomicSharedFromThis.hpp"

#endif
