/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#ifndef _RED_MEMORY_INTRUSIVE_SHARED_STORAGE_HPP_
#define _RED_MEMORY_INTRUSIVE_SHARED_STORAGE_HPP_

namespace red
{
namespace internal
{
	RED_MEMORY_INLINE IntrusiveSharedStorage::IntrusiveSharedStorage()
		: m_pointee( nullptr )
	{}

	RED_MEMORY_INLINE IntrusiveSharedStorage::IntrusiveSharedStorage( void* pointer )
		: m_pointee( pointer )
	{}

	RED_MEMORY_INLINE IntrusiveSharedStorage::IntrusiveSharedStorage( const IntrusiveSharedStorage & copyFrom )
		: m_pointee( copyFrom.m_pointee )
	{}
	
	RED_MEMORY_INLINE IntrusiveSharedStorage::IntrusiveSharedStorage( IntrusiveSharedStorage && rvalue  )
		: m_pointee( nullptr )
	{
		AssignRValue( std::forward< IntrusiveSharedStorage >( rvalue ) );
	}

	RED_MEMORY_INLINE void* IntrusiveSharedStorage::Get() const
	{
		return m_pointee;
	}

	template< typename T >
	RED_MEMORY_INLINE void IntrusiveSharedStorage::Destroy()
	{
		T * typedPointer = static_cast< T* >( m_pointee );
		RED_DELETE( typedPointer );
	}

	template< typename T, typename PoolType >
	RED_MEMORY_INLINE void IntrusiveSharedStorage::Destroy()
	{
		T * typedPointer = static_cast< T* >(m_pointee);
		RED_DELETE( typedPointer, PoolType );
	}

	template< typename T >
	RED_MEMORY_INLINE bool IntrusiveSharedStorage::Release()
	{
		// Here we expect T::Release to return the current count of reference. 
		// If returned count is 0, release reference. 
		T * typedPtr = static_cast< T* >( m_pointee );
		if( typedPtr && typedPtr->Release() == 0 )
		{
			return true;
		}

		return false;
	}

	template< typename T >
	RED_MEMORY_INLINE void IntrusiveSharedStorage::AddRef()
	{
		T * typedPtr = static_cast< T* >( m_pointee );
		if( typedPtr )
		{
			typedPtr->AddRef();
		}
	}

	RED_MEMORY_INLINE void IntrusiveSharedStorage::Swap( IntrusiveSharedStorage & swapWith )
	{
		std::swap( m_pointee, swapWith.m_pointee );
	}

	RED_MEMORY_INLINE IntrusiveSharedStorage::~IntrusiveSharedStorage()
	{}

	RED_MEMORY_INLINE void IntrusiveSharedStorage::AssignRValue( IntrusiveSharedStorage && rvalue )
	{
		if( this != &rvalue )
		{
			Swap( rvalue );
		}
	}
}
}

#endif
