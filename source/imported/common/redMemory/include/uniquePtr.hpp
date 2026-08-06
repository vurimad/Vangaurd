/**
* Copyright (c) 2014 CD Projekt Red. All Rights Reserved.
*/

#ifndef _RED_MEMORY_UNIQUE_PTR_HPP_
#define _RED_MEMORY_UNIQUE_PTR_HPP_

#include "assert.h"

namespace red
{
namespace memory
{
namespace internal
{
	struct CreateUniquePtrFromPool
	{
		template< typename T, typename PoolType, typename... Args >
		static UniquePtr< T, PoolType > Create( Args && ... args  )
		{
			return CreateUniquePtr< T, PoolType >( std::forward< Args >( args )... );
		}
	};

	struct CreateUniquePtrDefault
	{
		template< typename T, typename, typename... Args >
		static UniquePtr< T > Create( Args && ... args )
		{
			return CreateUniquePtr< T >( std::forward< Args >( args )... ); 
		}
	};
}
}

	template< typename PtrType, typename DeleterType >
	RED_MEMORY_INLINE UniquePtr< PtrType, DeleterType >::UniquePtr()
	{}

	template< typename PtrType, typename DeleterType >
	RED_MEMORY_INLINE UniquePtr< PtrType, DeleterType >::UniquePtr( std::nullptr_t )
	{}

	template< typename PtrType, typename DeleterType >
	RED_MEMORY_INLINE UniquePtr< PtrType, DeleterType >::UniquePtr( PtrType * pointer )
		:	m_storage( pointer )
	{}

	template< typename PtrType, typename DeleterType >
	RED_MEMORY_INLINE UniquePtr< PtrType, DeleterType >::UniquePtr( PtrType * pointer, const DeleterType & destroyer )
		:	m_storage( pointer, destroyer )
	{}

	template< typename PtrType, typename DeleterType >
	RED_MEMORY_INLINE UniquePtr< PtrType, DeleterType >::UniquePtr( PtrType * pointer, DeleterType && destroyer )
		:	m_storage( std::move( pointer ), std::forward< DeleterType >( destroyer ) )
	{ 
		static_assert( !std::is_reference< DeleterType >::value, "rvalue deleter bound to reference."); 
	}

	template< typename PtrType, typename DeleterType >
	RED_MEMORY_INLINE UniquePtr< PtrType, DeleterType >::UniquePtr( UniquePtr&& moveFrom ) 
		:	m_storage( std::forward< StorageType >( moveFrom.m_storage ) )
	{}

	template< typename PtrType, typename DeleterType >
	template< typename U, typename V > 
	RED_MEMORY_INLINE UniquePtr< PtrType, DeleterType >::UniquePtr( UniquePtr< U, V > && moveFrom ) 
		:	m_storage(  moveFrom.ReleaseOwnership(), std::forward< DeleterType >( moveFrom.GetDeleter() ) )
	{}

	template< typename PtrType, typename DeleterType >
	RED_MEMORY_INLINE UniquePtr< PtrType, DeleterType >::~UniquePtr() 
	{}

	template< typename PtrType, typename DeleterType >
	RED_MEMORY_INLINE UniquePtr< PtrType, DeleterType >& UniquePtr< PtrType, DeleterType >::operator=( UniquePtr && moveFrom )
	{ 
		UniquePtr( std::move( moveFrom ) ).Swap( *this );
		return *this;
	}

	template< typename PtrType, typename DeleterType >
	template<typename U, typename V > 
	RED_MEMORY_INLINE UniquePtr< PtrType, DeleterType >& UniquePtr< PtrType, DeleterType >::operator=( UniquePtr< U, V > && moveFrom )
	{
		UniquePtr( std::move( moveFrom ) ).Swap( *this );
		return *this;
	}

	template< typename PtrType, typename DeleterType >
	RED_MEMORY_INLINE PtrType & UniquePtr< PtrType, DeleterType >::operator*() const
	{
		PtrType * ptr = m_storage.Get();
		RED_MEMORY_ASSERT( ptr != nullptr, "null pointer access is illegal." );
		return *ptr;
	}

	template< typename PtrType, typename DeleterType >
	RED_MEMORY_INLINE PtrType * UniquePtr< PtrType, DeleterType >::operator->() const
	{
		PtrType * ptr = m_storage.Get();
		RED_MEMORY_ASSERT( ptr != nullptr, "null pointer access is illegal." );
		return ptr;
	}

	template< typename PtrType, typename DeleterType >
	RED_MEMORY_INLINE PtrType * UniquePtr< PtrType, DeleterType >::Get() const
	{
		return m_storage.Get(); 
	}

	template< typename PtrType, typename DeleterType >
	RED_MEMORY_INLINE UniquePtr< PtrType, DeleterType >::operator typename UniquePtr< PtrType, DeleterType >::bool_operator () const
	{ 
		return m_storage.Get() != nullptr ? &BoolConversion::valid : 0;
	}

	template< typename PtrType, typename DeleterType >
	RED_MEMORY_INLINE bool UniquePtr< PtrType, DeleterType >::operator !() const
	{
		return m_storage.Get() == nullptr;
	}

	template< typename PtrType, typename DeleterType >
	RED_MEMORY_INLINE PtrType * UniquePtr< PtrType, DeleterType >::ReleaseOwnership()
	{
		return m_storage.Release();
	}

	template< typename PtrType, typename DeleterType >
	RED_MEMORY_INLINE void UniquePtr< PtrType, DeleterType >::Reset( PtrType * pointer )
	{
		UniquePtr< PtrType, DeleterType >( pointer, GetDeleter() ).Swap( *this );
	}

	template< typename PtrType, typename DeleterType >
	RED_MEMORY_INLINE void UniquePtr< PtrType, DeleterType >::Swap( UniquePtr & swapWith )
	{
		m_storage.Swap( swapWith.m_storage );
	}

	template< typename PtrType, typename DeleterType >
	RED_MEMORY_INLINE DeleterType & UniquePtr< PtrType, DeleterType >::GetDeleter()
	{
		return m_storage.GetDestructor();
	}

	template< typename PtrType, typename DeleterType >
	RED_MEMORY_INLINE const DeleterType & UniquePtr< PtrType, DeleterType >::GetDeleter() const
	{
		return m_storage.GetDestructor();
	}

	template< typename PtrType, typename DeleterType >
	template< typename... Args >
	RED_MEMORY_INLINE UniquePtr< PtrType, DeleterType > UniquePtr< PtrType, DeleterType >::Create( Args && ... args )
	{
		typedef typename std::conditional< 
			std::is_base_of< red::memory::Pool, DeleterType >::value, 
			memory::internal::CreateUniquePtrFromPool,
			memory::internal::CreateUniquePtrDefault
		>::type Creator;

		return Creator::template Create< PtrType, DeleterType >( std::forward< Args >( args )... );
	}

	template< typename LeftType, typename RightType, typename DeleterType >
	RED_MEMORY_INLINE bool operator==( const UniquePtr< LeftType, DeleterType> & leftPtr, const UniquePtr< RightType, DeleterType> & rightPtr )
	{
		return leftPtr.Get() == rightPtr.Get();
	}

	template< typename LeftType, typename RightType, typename DeleterType >
	RED_MEMORY_INLINE bool operator!=( const UniquePtr< LeftType, DeleterType > & leftPtr, const UniquePtr< RightType, DeleterType > & rightPtr )
	{
		return leftPtr.Get() != rightPtr.Get();
	}

	template< typename LeftType, typename RightType, typename DeleterType >
	RED_MEMORY_INLINE bool operator<( const UniquePtr< LeftType, DeleterType > & leftPtr, const UniquePtr< RightType, DeleterType > & rightPtr )
	{
		return leftPtr.Get() < rightPtr.Get();
	}

	template< typename T, typename... Args >
	RED_MEMORY_INLINE UniquePtr< T > CreateUniquePtr( Args && ... args )
	{
		// ctremblay: MSVC2017 weird bug. I need to wrap by parenthesis else if new operator is explicitly deleted, it won't compile.
		UniquePtr< T > ptr( ( RED_NEW( T )( std::forward< Args >( args )...) ) );
		return ptr;
	}

	template< typename T, typename PoolType, typename... Args >
	RED_MEMORY_INLINE UniquePtr< T, PoolType > CreateUniquePtr( Args && ... args )
	{
		UniquePtr< T, PoolType > ptr( ( RED_NEW( T, PoolType )( std::forward< Args >( args )...) ) );
		return ptr;
	}

	template< typename T >
	RED_MEMORY_INLINE UniquePtr< T > MakeUniquePtr( T* ptr )
	{
		UniquePtr< T > result( ptr );
		return result;
	}
}

#endif
