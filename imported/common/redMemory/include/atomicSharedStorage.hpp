/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#ifndef _RED_MEMORY_ATOMIC_SHARED_STORAGE_HPP_
#define _RED_MEMORY_ATOMIC_SHARED_STORAGE_HPP_

#include "../../redSystem/include/redThreadsAtomic.h"
#include "operators.h"

namespace red
{
namespace internal
{
	struct AtomicRefCount
	{
		RED_USE_MEMORY_POOL( red::PoolRefCount );

		typedef atomic::TAtomic32 RefCountType;

		AtomicRefCount();
	
		RefCountType strong;
		RefCountType weak;
	};
}
}

RED_MEMORY_API_TEMPLATE template RED_MEMORY_API void* red::memory::internal::NewHelper< red::internal::AtomicRefCount >( red::memory::u32 );

namespace red
{
namespace internal
{

	RED_MEMORY_INLINE AtomicRefCount::AtomicRefCount()
		:	strong( 1 ),
			weak( 1 )
	{}

	RED_MEMORY_INLINE AtomicSharedStorage::AtomicSharedStorage()
		:	m_pointee( nullptr ),
			m_refCount( nullptr )
	{}
	
	RED_MEMORY_INLINE AtomicSharedStorage::AtomicSharedStorage( void * pointer )
		:	m_pointee( pointer ),
			m_refCount( RED_NEW_WITHOUT_HOOKS( AtomicRefCount, red::memory::HookType_Memory_Marking ) )
	{}

	RED_MEMORY_INLINE AtomicSharedStorage::AtomicSharedStorage( const AtomicSharedStorage & copyFrom )
		:	m_pointee( copyFrom.m_pointee ),
			m_refCount( copyFrom.m_refCount )
	{}
	
	RED_MEMORY_INLINE AtomicSharedStorage::AtomicSharedStorage( AtomicSharedStorage && rvalue )
		:	m_pointee( nullptr ),
			m_refCount( nullptr )
	{
		AssignRValue( std::forward< AtomicSharedStorage >( rvalue ) );
	}

	RED_MEMORY_INLINE void * AtomicSharedStorage::Get() const
	{
		return m_pointee;
	}

	template< typename T >
	RED_MEMORY_INLINE void AtomicSharedStorage::Destroy()
	{
		static_assert( sizeof( T ) > 0, "Cannot delete pointer to incomplete type. Did you forgot the include?" );
		static_assert( !std::is_polymorphic< T >::value || std::has_virtual_destructor< T >::value, "Virtual dtor is missing." );

		T * typedPointer = static_cast< T* >( m_pointee );
		RED_DELETE( typedPointer );
	}

	template< typename T, typename PoolType >
	RED_MEMORY_INLINE void AtomicSharedStorage::Destroy()
	{
		static_assert(sizeof( T ) > 0, "Cannot delete pointer to incomplete type. Did you forgot the include?");
		static_assert(!std::is_polymorphic< T >::value || std::has_virtual_destructor< T >::value, "Virtual dtor is missing.");

		T * typedPointer = static_cast< T* >(m_pointee);
		RED_DELETE( typedPointer, PoolType );
	}

	template< typename T >
	RED_MEMORY_INLINE bool AtomicSharedStorage::Release()
	{
		return InternalRelease();
	}
	
	RED_MEMORY_INLINE void AtomicSharedStorage::ReleaseWeak()
	{
		if ( m_refCount )
		{
			Int32 postDecremented = atomic::Decrement32( &m_refCount->weak );
			RED_FATAL_ASSERT( postDecremented >= 0, "Ref counter underflow" );

			if ( postDecremented == 0 )
			{
				RED_DELETE( m_refCount );
				m_refCount = nullptr;
			}
			
		}
	}
	
	template< typename T >
	RED_MEMORY_INLINE void AtomicSharedStorage::AddRef()
	{
		InternalAddRef();
	}
	
	RED_MEMORY_INLINE void AtomicSharedStorage::AddRefWeak()
	{
		if( m_refCount )
		{
			atomic::Increment32( &m_refCount->weak );
		}
	}

	RED_MEMORY_INLINE void AtomicSharedStorage::UpgradeFromWeakToStrong( AtomicSharedStorage & upgradeFrom )
	{
		if( upgradeFrom.m_refCount ) // if true, always true for the lifetime of this function
		{
			while( 1 )
			{
				AtomicRefCount::RefCountType count = atomic::FetchValue32( &upgradeFrom.m_refCount->strong );
				if( count == 0 )
				{
					return;
				}

				if( atomic::CompareExchange32( &upgradeFrom.m_refCount->strong, count + 1, count ) == count )
				{
					m_refCount = upgradeFrom.m_refCount;
					m_pointee = upgradeFrom.m_pointee;
					return;
				}
			}
		}
	}

	RED_MEMORY_INLINE void AtomicSharedStorage::Swap( AtomicSharedStorage & swapWith )
	{
		std::swap( m_refCount, swapWith.m_refCount );
		std::swap( m_pointee, swapWith.m_pointee );
	}

	RED_MEMORY_INLINE const void* AtomicSharedStorage::InternalGetRefCountStorage() const
	{
		return m_refCount;
	}

	RED_MEMORY_INLINE AtomicSharedStorage::~AtomicSharedStorage()
	{}

	RED_MEMORY_INLINE void AtomicSharedStorage::AssignRValue( AtomicSharedStorage && rvalue )
	{
		if( this != &rvalue )
		{
			Swap( rvalue );
		}
	}

	RED_MEMORY_INLINE Int32 AtomicSharedStorage::GetRefCount() const
	{
		return m_refCount ? atomic::FetchValue32( &m_refCount->strong ) : 0;
	}

	RED_MEMORY_INLINE Int32 AtomicSharedStorage::GetWeakRefCount() const
	{
		return m_refCount ? atomic::FetchValue32( &m_refCount->weak ) : 0;
	}

	RED_MEMORY_INLINE void AtomicSharedStorage::InternalAddRef()
	{
		if( m_refCount )
		{
			atomic::Increment32( &m_refCount->strong );
		}
	}

	RED_MEMORY_INLINE bool AtomicSharedStorage::InternalRelease()
	{
		if ( m_refCount )
		{
			Int32 postDecremented = atomic::Decrement32( &m_refCount->strong );
			RED_FATAL_ASSERT( postDecremented >= 0, "Ref counter underflow" );

			if ( postDecremented == 0 )
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
