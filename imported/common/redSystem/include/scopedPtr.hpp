/**
* Copyright (c) 2020 CDProjekt Red, Inc. All Rights Reserved.
*/

#pragma once

namespace red
{
	template< typename T >
	RED_INLINE DefaultScopedPtrDeleter< T >::DefaultScopedPtrDeleter()
	{}

	template< typename T >
	RED_INLINE void DefaultScopedPtrDeleter< T >::operator()( T* ptr ) const
	{
		static_assert( !std::is_polymorphic< T >::value || std::has_virtual_destructor< T >::value, "Virtual dtor is missing." );
		if ( ptr )
		{
			ptr->~T();
			std::free( ptr );
		}
	}

	template< typename T, typename DeleterType >
	RED_INLINE ScopedPtr< T, DeleterType >::ScopedPtr( T* object )
		: m_ptr( object )
	{}

	template< typename T, typename DeleterType >
	RED_INLINE ScopedPtr< T, DeleterType >::~ScopedPtr() noexcept
	{
		DeleterType()( m_ptr );
		m_ptr = nullptr;
	}

	template< typename T, typename DeleterType >
	RED_INLINE T* ScopedPtr< T, DeleterType >::Get() const
	{
		return m_ptr;
	}

	template< typename T, typename DeleterType >
	RED_INLINE void ScopedPtr< T, DeleterType >::Reset( T* object )
	{
		ScopedPtr< T, DeleterType >( object ).Swap( *this );
	}

	template< typename T, typename DeleterType >
	RED_INLINE void ScopedPtr< T, DeleterType >::Swap( ScopedPtr< T, DeleterType >& other )
	{
		std::swap( m_ptr, other.m_ptr );
	}

	template< typename T, typename DeleterType >
	RED_INLINE T* ScopedPtr< T, DeleterType >::operator->() const
	{
		RED_ASSERT( m_ptr, "Dereference of invalid pointer" );
		return m_ptr;
	}

	template< typename T, typename DeleterType >
	RED_INLINE T& ScopedPtr< T, DeleterType >::operator*() const
	{
		RED_ASSERT( m_ptr, "Dereference of invalid pointer" );
		return *m_ptr;
	}

	template< typename T, typename DeleterType >
	RED_INLINE ScopedPtr< T, DeleterType >::operator bool() const
	{
		return m_ptr != nullptr;
	}

	template< typename T, typename DeleterType >
	RED_INLINE bool ScopedPtr< T, DeleterType >::operator!() const
	{
		return m_ptr == nullptr;
	}
}