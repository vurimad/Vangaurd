/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#ifndef _RED_MEMORY_ATOMIC_SHARED_STORAGE_H_
#define _RED_MEMORY_ATOMIC_SHARED_STORAGE_H_

namespace red
{
namespace internal
{
	struct AtomicRefCount;

	class AtomicSharedStorage
	{
	protected:
		AtomicSharedStorage();
		AtomicSharedStorage( void * pointer );

		AtomicSharedStorage( const AtomicSharedStorage & copyFrom );
		AtomicSharedStorage( AtomicSharedStorage && rvalue );

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
		
		void UpgradeFromWeakToStrong( AtomicSharedStorage & upgradeFrom );

		void Swap( AtomicSharedStorage & swapWith );

		const void* InternalGetRefCountStorage() const;

	protected:

		~AtomicSharedStorage();

	private:

		void InternalAddRef();
		bool InternalRelease();
		void InternalSet( void * pointer );
		void InternalClear();
		void InternalGetPointerAddress( void *& pointer );

		void AssignRValue( AtomicSharedStorage && rvalue );

		void * m_pointee;
		AtomicRefCount * m_refCount;

		friend class SharedStorageAttorney;
	};
}
}

#include "atomicSharedStorage.hpp"

#endif
