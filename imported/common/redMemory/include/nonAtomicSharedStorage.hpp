/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#ifndef _RED_MEMORY_NON_ATOMIC_SHARED_STORAGE_HPP_
#define _RED_MEMORY_NON_ATOMIC_SHARED_STORAGE_HPP_

#include "operators.h"

namespace red
{
namespace internal
{
	struct NonAtomicRefCount
	{
		RED_USE_MEMORY_POOL( red::PoolRefCount );

		typedef Int32 RefCountType;

		NonAtomicRefCount();

		RefCountType strong;
		RefCountType weak;
	};

	RED_MEMORY_INLINE NonAtomicRefCount::NonAtomicRefCount()
		:	strong( 1 ),
			weak( 1 )
	{}	

	RED_MEMORY_INLINE NonAtomicSharedStorage::NonAtomicSharedStorage()
		:	m_pointee( nullptr ),
			m_refCount( nullptr )
	{}

	RED_MEMORY_INLINE NonAtomicSharedStorage::NonAtomicSharedStorage( void * pointer )
		:	m_pointee( pointer ),
			m_refCount( RED_NEW( NonAtomicRefCount ) )
	{}

	
	RED_MEMORY_INLINE NonAtomicSharedStorage::NonAtomicSharedStorage( const NonAtomicSharedStorage & copyFrom )
		:	m_pointee( copyFrom.m_pointee ),
			m_refCount( copyFrom.m_refCount )
	{}
	
	RED_MEMORY_INLINE NonAtomicSharedStorage::NonAtomicSharedStorage( NonAtomicSharedStorage && rvalue )
		:	m_pointee( nullptr ),	
			m_refCount( nullptr )			
	{
		AssignRValue( std::forward< NonAtomicSharedStorage >( rvalue ) );
	}

	RED_MEMORY_INLINE NonAtomicSharedStorage::~NonAtomicSharedStorage()
	{}

	RED_MEMORY_INLINE void * NonAtomicSharedStorage::Get() const
	{
		return m_pointee;	
	}

	template< typename T >
	RED_MEMORY_INLINE void NonAtomicSharedStorage::Destroy()
	{
		static_assert( sizeof( T ) > 0, "Cannot delete pointer to incomplete type. Did you forgot the include?" );
		static_assert( !std::is_polymorphic< T >::value || std::has_virtual_destructor< T >::value, "Virtual dtor is missing." );

		T * typedPointer = static_cast< T* >( m_pointee );
		RED_DELETE( typedPointer );
	}

	template< typename T, typename PoolType >
	RED_MEMORY_INLINE void NonAtomicSharedStorage::Destroy()
	{
		static_assert(sizeof( T ) > 0, "Cannot delete pointer to incomplete type. Did you forgot the include?");
		static_assert(!std::is_polymorphic< T >::value || std::has_virtual_destructor< T >::value, "Virtual dtor is missing.");

		T * typedPointer = static_cast< T* >( m_pointee );
		RED_DELETE( typedPointer, PoolType );
	}

	template< typename T >
	RED_MEMORY_INLINE bool NonAtomicSharedStorage::Release()
	{
		return InternalRelease();
	}
	
	RED_MEMORY_INLINE void NonAtomicSharedStorage::ReleaseWeak()
	{
		if ( m_refCount )
		{
			RED_FATAL_ASSERT( m_refCount->weak > 0, "Ref counter underflow" );

			if ( --( m_refCount->weak ) == 0 )
			{
				RED_DELETE( m_refCount );
				m_refCount = nullptr;
			}
		}
	}
	
	template< typename T >
	RED_MEMORY_INLINE void NonAtomicSharedStorage::AddRef()
	{
		InternalAddRef();
	}
	
	RED_MEMORY_INLINE void NonAtomicSharedStorage::AddRefWeak()
	{
		if( m_refCount )
		{
			++(m_refCount->weak);
		}
	}

	RED_MEMORY_INLINE void NonAtomicSharedStorage::UpgradeFromWeakToStrong( NonAtomicSharedStorage & upgradeFrom )
	{
		if( upgradeFrom.m_refCount && upgradeFrom.m_refCount->strong != 0 )
		{
			++upgradeFrom.m_refCount->strong;
			m_refCount = upgradeFrom.m_refCount;
			m_pointee = upgradeFrom.m_pointee;
		}
	}

	RED_MEMORY_INLINE void NonAtomicSharedStorage::Swap( NonAtomicSharedStorage & swapWith )
	{
		std::swap( m_refCount, swapWith.m_refCount );
		std::swap( m_pointee, swapWith.m_pointee );  
	}

	RED_MEMORY_INLINE const void* NonAtomicSharedStorage::InternalGetRefCountStorage() const
	{
		return m_refCount;
	}

	RED_MEMORY_INLINE void NonAtomicSharedStorage::AssignRValue( NonAtomicSharedStorage && rvalue )
	{
		if( this != &rvalue )
		{
			Swap( rvalue );
		}
	}

    RED_MEMORY_INLINE Int32 NonAtomicSharedStorage::GetRefCount() const
	{
		return m_refCount ? m_refCount->strong : 0;
	}

	RED_MEMORY_INLINE Int32 NonAtomicSharedStorage::GetWeakRefCount() const
	{
		return m_refCount ? m_refCount->weak : 0;
	}

	RED_MEMORY_INLINE void NonAtomicSharedStorage::InternalAddRef()
	{
		if( m_refCount )
		{
			++(m_refCount->strong);
		}
	}

	RED_MEMORY_INLINE bool NonAtomicSharedStorage::InternalRelease()
	{
		if ( m_refCount )
		{
			RED_FATAL_ASSERT( m_refCount->strong > 0, "Ref counter overflow" );

			if ( --( m_refCount->strong ) == 0 )
			{
				ReleaseWeak();
				return true;
			}
		}

		return false;
	}
}
}

#endif
