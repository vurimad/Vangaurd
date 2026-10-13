/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#ifndef _RED_MEMORY_NON_ATOMIC_SHARED_STORAGE_H_
#define _RED_MEMORY_NON_ATOMIC_SHARED_STORAGE_H_

namespace red
{
namespace internal
{
	struct NonAtomicRefCount;

	class RED_MEMORY_API NonAtomicSharedStorage
	{
	protected:

		NonAtomicSharedStorage();
		NonAtomicSharedStorage( void * pointer );

		NonAtomicSharedStorage( const NonAtomicSharedStorage & copyFrom );
		NonAtomicSharedStorage( NonAtomicSharedStorage && rvalue );

		void * Get() const;

		template< typename T >
		void Destroy();

		template< typename T, typename PoolType >
		void Destroy();

		template< typename T >
		bool Release();

		template< typename T >
		void AddRef();

		void ReleaseWeak();
		void AddRefWeak();

		Int32 GetRefCount() const;
		Int32 GetWeakRefCount() const;

		void UpgradeFromWeakToStrong( NonAtomicSharedStorage & upgradeFrom );

		void Swap( NonAtomicSharedStorage & swapWith );

		const void* InternalGetRefCountStorage() const;

	protected:

		~NonAtomicSharedStorage();

	private:

		void InternalAddRef();
		bool InternalRelease();
		void InternalSet( void * pointer );
		void InternalClear();
		void InternalGetPointerAddress( void *& pointer );

		void AssignRValue( NonAtomicSharedStorage && rvalue );

		void * m_pointee;
		NonAtomicRefCount * m_refCount;

		friend class SharedStorageAttorney;
	};
}
}

#include "nonAtomicSharedStorage.hpp"

#endif

